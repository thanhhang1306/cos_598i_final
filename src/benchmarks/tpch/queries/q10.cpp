#include "benchmarks/tpch/Queries.hpp"
#include "common/runtime/Hash.hpp"
#include "common/runtime/Types.hpp"
#include "hyper/GroupBy.hpp"
#include "hyper/ParallelHelper.hpp"
#include "tbb/tbb.h"
#include "vectorwise/Operations.hpp"
#include "vectorwise/Operators.hpp"
#include "vectorwise/Primitives.hpp"
#include "vectorwise/QueryBuilder.hpp"
#include "vectorwise/VectorAllocator.hpp"
#include <algorithm>
#include <mutex>
#include <unordered_map>
#include <vector>

using namespace runtime;
using namespace std;

// select
//   c_custkey,
//   c_name,
//   sum(l_extendedprice * (1 - l_discount)) as revenue,
//   c_acctbal,
//   n_name,
//   c_address,
//   c_phone,
//   c_comment
// from
//   customer,
//   orders,
//   lineitem,
//   nation
// where
//   c_custkey = o_custkey
//   and l_orderkey = o_orderkey
//   and o_orderdate >= date '1993-10-01'
//   and o_orderdate < date '1994-01-01'
//   and l_returnflag = 'R'
//   and c_nationkey = n_nationkey
// group by
//   c_custkey,
//   c_name,
//   c_acctbal,
//   c_phone,
//   n_name,
//   c_address,
//   c_comment
// order by revenue desc
// limit 20

namespace {

using Revenue = types::Numeric<12, 4>;

struct Q10Row {
   types::Integer c_custkey;
   types::Char<25> c_name;
   Revenue revenue;
   types::Numeric<12, 2> c_acctbal;
   types::Char<25> n_name;
   types::Varchar<40> c_address;
   types::Char<15> c_phone;
   types::Varchar<117> c_comment;
};

Relation makeQ10Result(const vector<Q10Row>& rows) {
   Relation result;
   result.insert("c_custkey", make_unique<algebra::Integer>());
   result.insert("c_name", make_unique<algebra::Char>(25));
   result.insert("revenue", make_unique<algebra::Numeric>(12, 4));
   result.insert("c_acctbal", make_unique<algebra::Numeric>(12, 2));
   result.insert("n_name", make_unique<algebra::Char>(25));
   result.insert("c_address", make_unique<algebra::Varchar>(40));
   result.insert("c_phone", make_unique<algebra::Char>(15));
   result.insert("c_comment", make_unique<algebra::Varchar>(117));

   auto& c_custkey =
       result["c_custkey"].typedAccessForChange<types::Integer>();
   auto& c_name = result["c_name"].typedAccessForChange<types::Char<25>>();
   auto& revenue = result["revenue"].typedAccessForChange<Revenue>();
   auto& c_acctbal =
       result["c_acctbal"].typedAccessForChange<types::Numeric<12, 2>>();
   auto& n_name = result["n_name"].typedAccessForChange<types::Char<25>>();
   auto& c_address =
       result["c_address"].typedAccessForChange<types::Varchar<40>>();
   auto& c_phone = result["c_phone"].typedAccessForChange<types::Char<15>>();
   auto& c_comment =
       result["c_comment"].typedAccessForChange<types::Varchar<117>>();

   c_custkey.reset(rows.size());
   c_name.reset(rows.size());
   revenue.reset(rows.size());
   c_acctbal.reset(rows.size());
   n_name.reset(rows.size());
   c_address.reset(rows.size());
   c_phone.reset(rows.size());
   c_comment.reset(rows.size());

   for (auto row : rows) {
      c_custkey.push_back(row.c_custkey);
      c_name.push_back(row.c_name);
      revenue.push_back(row.revenue);
      c_acctbal.push_back(row.c_acctbal);
      n_name.push_back(row.n_name);
      c_address.push_back(row.c_address);
      c_phone.push_back(row.c_phone);
      c_comment.push_back(row.c_comment);
   }
   result.nrTuples = rows.size();
   return result;
}

Relation finishQ10(Database& db,
                   const vector<pair<types::Integer, Revenue>>& revenues) {
   unordered_map<int32_t, Revenue> revenueByCustomer;
   revenueByCustomer.reserve(revenues.size());
   for (const auto& revenue : revenues)
      revenueByCustomer[revenue.first.value] += revenue.second;

   unordered_map<int32_t, types::Char<25>> nationName;
   auto& nation = db["nation"];
   auto n_nationkey = nation["n_nationkey"].data<types::Integer>();
   auto n_name = nation["n_name"].data<types::Char<25>>();
   for (size_t i = 0; i < nation.nrTuples; ++i)
      nationName.emplace(n_nationkey[i].value, n_name[i]);

   vector<Q10Row> rows;
   rows.reserve(min<size_t>(20, revenueByCustomer.size()));
   auto& customer = db["customer"];
   auto c_custkey = customer["c_custkey"].data<types::Integer>();
   auto c_name = customer["c_name"].data<types::Char<25>>();
   auto c_address = customer["c_address"].data<types::Varchar<40>>();
   auto c_nationkey = customer["c_nationkey"].data<types::Integer>();
   auto c_phone = customer["c_phone"].data<types::Char<15>>();
   auto c_acctbal = customer["c_acctbal"].data<types::Numeric<12, 2>>();
   auto c_comment = customer["c_comment"].data<types::Varchar<117>>();

   for (size_t i = 0; i < customer.nrTuples; ++i) {
      auto revenue = revenueByCustomer.find(c_custkey[i].value);
      if (revenue == revenueByCustomer.end()) continue;
      rows.push_back({c_custkey[i],
                      c_name[i],
                      revenue->second,
                      c_acctbal[i],
                      nationName[c_nationkey[i].value],
                      c_address[i],
                      c_phone[i],
                      c_comment[i]});
   }

   sort(rows.begin(), rows.end(), [](const Q10Row& left, const Q10Row& right) {
      if (left.revenue != right.revenue) return left.revenue > right.revenue;
      return left.c_custkey < right.c_custkey;
   });
   if (rows.size() > 20) rows.resize(20);
   return makeQ10Result(rows);
}

vector<pair<types::Integer, Revenue>> collectQ10Groups(BlockRelation& grouped) {
   vector<pair<types::Integer, Revenue>> revenues;
   auto custkeyAttr = grouped.getAttribute("c_custkey");
   auto revenueAttr = grouped.getAttribute("revenue");
   for (auto& block : grouped) {
      auto n = block.size();
      auto c_custkey =
          reinterpret_cast<types::Integer*>(block.data(custkeyAttr));
      auto revenue = reinterpret_cast<Revenue*>(block.data(revenueAttr));
      for (size_t i = 0; i < n; ++i)
         revenues.emplace_back(c_custkey[i], revenue[i]);
   }
   return revenues;
}

} // namespace

NOVECTORIZE Relation q10_hyper(Database& db, size_t nrThreads) {
   using hash = runtime::CRC32Hash;

   auto resources = initQuery(nrThreads);
   (void)resources;

   auto c1 = types::Date::castString("1993-10-01");
   auto c2 = types::Date::castString("1994-01-01");
   auto returned = types::Char<1>::castString("R");
   auto one = types::Numeric<12, 2>::castString("1.00");
   auto zero = Revenue::castString("0.0000");

   auto& orders = db["orders"];
   auto o_orderkey = orders["o_orderkey"].data<types::Integer>();
   auto o_custkey = orders["o_custkey"].data<types::Integer>();
   auto o_orderdate = orders["o_orderdate"].data<types::Date>();

   Hashmapx<types::Integer, types::Integer, hash> orderCustomers;
   tbb::enumerable_thread_specific<runtime::Stack<
       decltype(orderCustomers)::Entry>>
       orderEntries;

   auto foundOrders = PARALLEL_SELECT(orders.nrTuples, orderEntries, {
      if (o_orderdate[i] >= c1 && o_orderdate[i] < c2) {
         entries.emplace_back(orderCustomers.hash(o_orderkey[i]),
                              o_orderkey[i], o_custkey[i]);
         found++;
      }
   });
   orderCustomers.setSize(foundOrders);
   parallel_insert(orderEntries, orderCustomers);

   auto& lineitem = db["lineitem"];
   auto l_orderkey = lineitem["l_orderkey"].data<types::Integer>();
   auto l_extendedprice =
       lineitem["l_extendedprice"].data<types::Numeric<12, 2>>();
   auto l_discount = lineitem["l_discount"].data<types::Numeric<12, 2>>();
   auto l_returnflag = lineitem["l_returnflag"].data<types::Char<1>>();

   auto groupOp = make_GroupBy<types::Integer, Revenue, hash>(
       [](auto& acc, auto&& value) { acc += value; }, zero, nrThreads);

   tbb::parallel_for(
       tbb::blocked_range<size_t>(0, lineitem.nrTuples, morselSize),
       [&](const tbb::blocked_range<size_t>& r) {
          auto locals = groupOp.preAggLocals();
          for (size_t i = r.begin(), end = r.end(); i != end; ++i) {
             if (!(l_returnflag[i] == returned)) continue;
             auto custkey = orderCustomers.findOne(l_orderkey[i]);
             if (custkey)
                locals.consume(*custkey,
                               l_extendedprice[i] * (one - l_discount[i]));
          }
       });

   vector<pair<types::Integer, Revenue>> revenues;
   mutex revenuesMutex;
   groupOp.forallGroups([&](auto& groups) {
      vector<pair<types::Integer, Revenue>> local;
      local.reserve(groups.size());
      for (auto block : groups)
         for (auto& group : block) local.emplace_back(group.k, group.v);
      lock_guard<mutex> lock(revenuesMutex);
      revenues.insert(revenues.end(), local.begin(), local.end());
   });

   auto result = finishQ10(db, revenues);
   leaveQuery(nrThreads);
   return result;
}

std::unique_ptr<Q10Builder::Q10> Q10Builder::getQuery() {
   using namespace vectorwise;

   auto result = Result();
   previous = result.resultWriter.shared.result->participate();

   auto r = make_unique<Q10>();
   auto orders = Scan("orders");
   Select(Expression()
              .addOp(BF(primitives::sel_less_Date_col_Date_val),
                     Buffer(sel_order, sizeof(pos_t)),
                     Column(orders, "o_orderdate"), Value(&r->c2))
              .addOp(BF(primitives::selsel_greater_equal_Date_col_Date_val),
                     Buffer(sel_order, sizeof(pos_t)),
                     Buffer(sel_order2, sizeof(pos_t)),
                     Column(orders, "o_orderdate"), Value(&r->c1)));

   auto lineitem = Scan("lineitem");
   Select(Expression().addOp(
       BF(primitives::sel_equal_to_Char_1_col_Char_1_val),
       Buffer(sel_lineitem, sizeof(pos_t)),
       Column(lineitem, "l_returnflag"), Value(&r->returned)));

   HashJoin(Buffer(order_line, sizeof(pos_t)), conf.joinAll())
       .setProbeSelVector(Buffer(sel_lineitem), conf.joinSel())
       .addBuildKey(Column(orders, "o_orderkey"), Buffer(sel_order2),
                    conf.hash_sel_int32_t_col(),
                    primitives::scatter_sel_int32_t_col)
       .addBuildValue(Column(orders, "o_custkey"), Buffer(sel_order2),
                      primitives::scatter_sel_int32_t_col,
                      Buffer(o_custkey, sizeof(int32_t)),
                      primitives::gather_col_int32_t_col)
       .addProbeKey(Column(lineitem, "l_orderkey"), Buffer(sel_lineitem),
                    conf.hash_sel_int32_t_col(),
                    primitives::keys_equal_int32_t_col);

   Project().addExpression(
       Expression()
           .addOp(primitives::proj_sel_minus_int64_t_val_int64_t_col,
                  Buffer(order_line),
                  Buffer(result_proj_minus, sizeof(int64_t)), Value(&r->one),
                  Column(lineitem, "l_discount"))
           .addOp(primitives::proj_multiplies_sel_int64_t_col_int64_t_col,
                  Buffer(order_line), Buffer(revenue, sizeof(int64_t)),
                  Column(lineitem, "l_extendedprice"),
                  Buffer(result_proj_minus)));

   HashGroup()
       .addKey(Buffer(o_custkey), conf.hash_int32_t_col(),
               primitives::keys_not_equal_int32_t_col,
               primitives::partition_by_key_int32_t_col,
               primitives::scatter_sel_int32_t_col,
               primitives::keys_not_equal_row_int32_t_col,
               primitives::partition_by_key_row_int32_t_col,
               primitives::scatter_sel_row_int32_t_col,
               primitives::gather_val_int32_t_col,
               Buffer(group_c_custkey, sizeof(int32_t)))
       .addValue(Buffer(revenue), primitives::aggr_init_plus_int64_t_col,
                 primitives::aggr_plus_int64_t_col,
                 primitives::aggr_row_plus_int64_t_col,
                 primitives::gather_val_int64_t_col,
                 Buffer(group_revenue, sizeof(int64_t)));

   result.addValue("c_custkey", Buffer(group_c_custkey))
       .addValue("revenue", Buffer(group_revenue))
       .finalize();

   r->rootOp = popOperator();
   return r;
}

Relation q10_vectorwise(Database& db, size_t nrThreads, size_t vectorSize) {
   using namespace vectorwise;

   WorkerGroup workers(nrThreads);
   SharedStateManager shared;
   unique_ptr<runtime::Query> grouped;

   workers.run([&]() {
      Q10Builder builder(db, shared, vectorSize);
      auto query = builder.getQuery();
      query->rootOp->next();
      auto leader = barrier();
      if (leader)
         grouped = move(
             dynamic_cast<ResultWriter*>(query->rootOp.get())->shared.result);
   });

   if (!grouped) return makeQ10Result({});
   return finishQ10(db, collectQ10Groups(*grouped->result));
}
