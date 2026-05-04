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
//   s_suppkey,
//   s_name,
//   s_address,
//   s_phone,
//   total_revenue
// from
//   supplier,
//   (
//     select
//       l_suppkey as supplier_no,
//       sum(l_extendedprice * (1 - l_discount)) as total_revenue
//     from lineitem
//     where l_shipdate >= date '1996-01-01'
//       and l_shipdate < date '1996-04-01'
//     group by l_suppkey
//   ) revenue
// where s_suppkey = supplier_no
//   and total_revenue = (select max(total_revenue) from revenue)

namespace {

using Revenue = types::Numeric<12, 4>;

struct Q15Row {
   types::Integer s_suppkey;
   types::Char<25> s_name;
   types::Varchar<40> s_address;
   types::Char<15> s_phone;
   Revenue total_revenue;
};

Relation makeQ15Result(const vector<Q15Row>& rows) {
   Relation result;
   result.insert("s_suppkey", make_unique<algebra::Integer>());
   result.insert("s_name", make_unique<algebra::Char>(25));
   result.insert("s_address", make_unique<algebra::Varchar>(40));
   result.insert("s_phone", make_unique<algebra::Char>(15));
   result.insert("total_revenue", make_unique<algebra::Numeric>(12, 4));

   auto& s_suppkey =
       result["s_suppkey"].typedAccessForChange<types::Integer>();
   auto& s_name = result["s_name"].typedAccessForChange<types::Char<25>>();
   auto& s_address =
       result["s_address"].typedAccessForChange<types::Varchar<40>>();
   auto& s_phone = result["s_phone"].typedAccessForChange<types::Char<15>>();
   auto& total_revenue =
       result["total_revenue"].typedAccessForChange<Revenue>();

   s_suppkey.reset(rows.size());
   s_name.reset(rows.size());
   s_address.reset(rows.size());
   s_phone.reset(rows.size());
   total_revenue.reset(rows.size());

   for (auto row : rows) {
      s_suppkey.push_back(row.s_suppkey);
      s_name.push_back(row.s_name);
      s_address.push_back(row.s_address);
      s_phone.push_back(row.s_phone);
      total_revenue.push_back(row.total_revenue);
   }
   result.nrTuples = rows.size();
   return result;
}

Relation finishQ15(Database& db,
                   const vector<pair<types::Integer, Revenue>>& revenues) {
   if (revenues.empty()) return makeQ15Result({});

   auto maxRevenue = revenues.front().second;
   for (const auto& revenue : revenues)
      if (revenue.second > maxRevenue) maxRevenue = revenue.second;

   unordered_map<int32_t, Revenue> winningSuppliers;
   for (const auto& revenue : revenues)
      if (revenue.second == maxRevenue)
         winningSuppliers.emplace(revenue.first.value, revenue.second);

   auto& supplier = db["supplier"];
   auto s_suppkey = supplier["s_suppkey"].data<types::Integer>();
   auto s_name = supplier["s_name"].data<types::Char<25>>();
   auto s_address = supplier["s_address"].data<types::Varchar<40>>();
   auto s_phone = supplier["s_phone"].data<types::Char<15>>();

   vector<Q15Row> rows;
   rows.reserve(winningSuppliers.size());
   for (size_t i = 0; i < supplier.nrTuples; ++i) {
      auto revenue = winningSuppliers.find(s_suppkey[i].value);
      if (revenue == winningSuppliers.end()) continue;
      rows.push_back(
          {s_suppkey[i], s_name[i], s_address[i], s_phone[i], revenue->second});
   }
   sort(rows.begin(), rows.end(), [](const Q15Row& left, const Q15Row& right) {
      return left.s_suppkey < right.s_suppkey;
   });
   return makeQ15Result(rows);
}

vector<pair<types::Integer, Revenue>> collectQ15Groups(BlockRelation& grouped) {
   vector<pair<types::Integer, Revenue>> revenues;
   auto suppkeyAttr = grouped.getAttribute("supplier_no");
   auto revenueAttr = grouped.getAttribute("total_revenue");
   for (auto& block : grouped) {
      auto n = block.size();
      auto suppkey =
          reinterpret_cast<types::Integer*>(block.data(suppkeyAttr));
      auto revenue = reinterpret_cast<Revenue*>(block.data(revenueAttr));
      for (size_t i = 0; i < n; ++i)
         revenues.emplace_back(suppkey[i], revenue[i]);
   }
   return revenues;
}

} // namespace

NOVECTORIZE Relation q15_hyper(Database& db, size_t nrThreads) {
   using hash = runtime::CRC32Hash;

   auto resources = initQuery(nrThreads);
   (void)resources;

   auto c1 = types::Date::castString("1996-01-01");
   auto c2 = types::Date::castString("1996-04-01");
   auto one = types::Numeric<12, 2>::castString("1.00");
   auto zero = Revenue::castString("0.0000");

   auto& lineitem = db["lineitem"];
   auto l_suppkey = lineitem["l_suppkey"].data<types::Integer>();
   auto l_shipdate = lineitem["l_shipdate"].data<types::Date>();
   auto l_extendedprice =
       lineitem["l_extendedprice"].data<types::Numeric<12, 2>>();
   auto l_discount = lineitem["l_discount"].data<types::Numeric<12, 2>>();

   auto groupOp = make_GroupBy<types::Integer, Revenue, hash>(
       [](auto& acc, auto&& value) { acc += value; }, zero, nrThreads);

   tbb::parallel_for(
       tbb::blocked_range<size_t>(0, lineitem.nrTuples, morselSize),
       [&](const tbb::blocked_range<size_t>& r) {
          auto locals = groupOp.preAggLocals();
          for (size_t i = r.begin(), end = r.end(); i != end; ++i) {
             if (l_shipdate[i] >= c1 && l_shipdate[i] < c2)
                locals.consume(l_suppkey[i],
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

   auto result = finishQ15(db, revenues);
   leaveQuery(nrThreads);
   return result;
}

std::unique_ptr<Q15Builder::Q15> Q15Builder::getQuery() {
   using namespace vectorwise;

   auto result = Result();
   previous = result.resultWriter.shared.result->participate();

   auto r = make_unique<Q15>();
   auto lineitem = Scan("lineitem");
   Select(Expression()
              .addOp(BF(primitives::sel_less_Date_col_Date_val),
                     Buffer(sel_lineitem, sizeof(pos_t)),
                     Column(lineitem, "l_shipdate"), Value(&r->c2))
              .addOp(BF(primitives::selsel_greater_equal_Date_col_Date_val),
                     Buffer(sel_lineitem, sizeof(pos_t)),
                     Buffer(sel_lineitem2, sizeof(pos_t)),
                     Column(lineitem, "l_shipdate"), Value(&r->c1)));

   Project().addExpression(
       Expression()
           .addOp(primitives::proj_sel_plus_int32_t_col_int32_t_val,
                  Buffer(sel_lineitem2),
                  Buffer(compact_suppkey, sizeof(int32_t)),
                  Column(lineitem, "l_suppkey"), Value(&r->zero))
           .addOp(primitives::proj_sel_minus_int64_t_val_int64_t_col,
                  Buffer(sel_lineitem2),
                  Buffer(result_proj_minus, sizeof(int64_t)), Value(&r->one),
                  Column(lineitem, "l_discount"))
           .addOp(primitives::proj_multiplies_sel_int64_t_col_int64_t_col,
                  Buffer(sel_lineitem2), Buffer(revenue, sizeof(int64_t)),
                  Column(lineitem, "l_extendedprice"),
                  Buffer(result_proj_minus)));

   HashGroup()
       .addKey(Buffer(compact_suppkey), conf.hash_int32_t_col(),
               primitives::keys_not_equal_int32_t_col,
               primitives::partition_by_key_int32_t_col,
               primitives::scatter_sel_int32_t_col,
               primitives::keys_not_equal_row_int32_t_col,
               primitives::partition_by_key_row_int32_t_col,
               primitives::scatter_sel_row_int32_t_col,
               primitives::gather_val_int32_t_col,
               Buffer(supplier_no, sizeof(int32_t)))
       .addValue(Buffer(revenue), primitives::aggr_init_plus_int64_t_col,
                 primitives::aggr_plus_int64_t_col,
                 primitives::aggr_row_plus_int64_t_col,
                 primitives::gather_val_int64_t_col,
                 Buffer(total_revenue, sizeof(int64_t)));

   result.addValue("supplier_no", Buffer(supplier_no))
       .addValue("total_revenue", Buffer(total_revenue))
       .finalize();

   r->rootOp = popOperator();
   return r;
}

Relation q15_vectorwise(Database& db, size_t nrThreads, size_t vectorSize) {
   using namespace vectorwise;

   WorkerGroup workers(nrThreads);
   SharedStateManager shared;
   unique_ptr<runtime::Query> grouped;

   workers.run([&]() {
      Q15Builder builder(db, shared, vectorSize);
      auto query = builder.getQuery();
      query->rootOp->next();
      auto leader = barrier();
      if (leader)
         grouped = move(
             dynamic_cast<ResultWriter*>(query->rootOp.get())->shared.result);
   });

   if (!grouped) return makeQ15Result({});
   return finishQ15(db, collectQ15Groups(*grouped->result));
}
