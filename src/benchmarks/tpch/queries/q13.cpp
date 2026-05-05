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
#include <string.h>
#include <unordered_map>
#include <vector>

using namespace runtime;
using namespace std;

// select
//   c_count,
//   count(*) as custdist
// from (
//   select
//     c_custkey,
//     count(o_orderkey) as c_count
//   from
//     customer left outer join orders on
//       c_custkey = o_custkey
//       and o_comment not like '%special%requests%'
//   group by
//     c_custkey
// ) as c_orders
// group by
//   c_count
// order by
//   custdist desc,
//   c_count desc

namespace {

struct Q13Row {
   int64_t c_count;
   int64_t custdist;
};

bool containsSpecialRequests(const types::Varchar<79>& comment) {
   static constexpr const char* special = "special";
   static constexpr const char* requests = "requests";
   static constexpr size_t specialLen = 7;
   static constexpr size_t requestsLen = 8;

   if (comment.len < specialLen + requestsLen) return false;
   auto specialPos = static_cast<const char*>(
       memmem(comment.value, comment.len, special, specialLen));
   if (!specialPos) return false;
   auto afterSpecial = specialPos + specialLen;
   auto remaining = comment.value + comment.len - afterSpecial;
   return memmem(afterSpecial, remaining, requests, requestsLen) != nullptr;
}

Relation makeQ13Result(vector<Q13Row> rows) {
   sort(rows.begin(), rows.end(), [](const Q13Row& left, const Q13Row& right) {
      if (left.custdist != right.custdist)
         return left.custdist > right.custdist;
      return left.c_count > right.c_count;
   });

   Relation result;
   result.insert("c_count", make_unique<algebra::BigInt>());
   result.insert("custdist", make_unique<algebra::BigInt>());

   auto& c_count = result["c_count"].typedAccessForChange<int64_t>();
   auto& custdist = result["custdist"].typedAccessForChange<int64_t>();
   c_count.reset(rows.size());
   custdist.reset(rows.size());

   for (auto row : rows) {
      c_count.push_back(row.c_count);
      custdist.push_back(row.custdist);
   }
   result.nrTuples = rows.size();
   return result;
}

Relation finishQ13(
    Database& db,
    const vector<pair<types::Integer, int64_t>>& orderCountsByCustomer) {
   unordered_map<int32_t, int64_t> orderCounts;
   orderCounts.reserve(orderCountsByCustomer.size());
   for (auto& group : orderCountsByCustomer)
      orderCounts[group.first.value] += group.second;

   unordered_map<int64_t, int64_t> customerDistribution;
   auto& customer = db["customer"];
   auto c_custkey = customer["c_custkey"].data<types::Integer>();
   customerDistribution.reserve(64);
   for (size_t i = 0; i < customer.nrTuples; ++i) {
      auto count = int64_t(0);
      auto found = orderCounts.find(c_custkey[i].value);
      if (found != orderCounts.end()) count = found->second;
      customerDistribution[count] += 1;
   }

   vector<Q13Row> rows;
   rows.reserve(customerDistribution.size());
   for (auto& group : customerDistribution)
      rows.push_back({group.first, group.second});
   return makeQ13Result(move(rows));
}

vector<pair<types::Integer, int64_t>> collectQ13Groups(BlockRelation& grouped) {
   vector<pair<types::Integer, int64_t>> counts;
   auto custkeyAttr = grouped.getAttribute("c_custkey");
   auto countAttr = grouped.getAttribute("c_count");
   for (auto& block : grouped) {
      auto n = block.size();
      auto c_custkey =
          reinterpret_cast<types::Integer*>(block.data(custkeyAttr));
      auto c_count = reinterpret_cast<int64_t*>(block.data(countAttr));
      for (size_t i = 0; i < n; ++i)
         counts.emplace_back(c_custkey[i], c_count[i]);
   }
   return counts;
}

} // namespace

NOVECTORIZE Relation q13_hyper(Database& db, size_t nrThreads) {
   using hash = runtime::CRC32Hash;

   auto resources = initQuery(nrThreads);
   (void)resources;

   auto& orders = db["orders"];
   auto o_custkey = orders["o_custkey"].data<types::Integer>();
   auto o_comment = orders["o_comment"].data<types::Varchar<79>>();

   auto orderCountGroups = make_GroupBy<types::Integer, int64_t, hash>(
       [](auto& acc, auto&& value) { acc += value; }, int64_t(0), nrThreads);

   tbb::parallel_for(
       tbb::blocked_range<size_t>(0, orders.nrTuples, morselSize),
       [&](const tbb::blocked_range<size_t>& r) {
          auto locals = orderCountGroups.preAggLocals();
          for (size_t i = r.begin(), end = r.end(); i != end; ++i)
             if (!containsSpecialRequests(o_comment[i]))
                locals.consume(o_custkey[i], int64_t(1));
       });

   vector<pair<types::Integer, int64_t>> orderCounts;
   mutex orderCountsMutex;
   orderCountGroups.forallGroups([&](auto& groups) {
      vector<pair<types::Integer, int64_t>> local;
      local.reserve(groups.size());
      for (auto block : groups)
         for (auto& group : block) local.emplace_back(group.k, group.v);
      lock_guard<mutex> lock(orderCountsMutex);
      orderCounts.insert(orderCounts.end(), local.begin(), local.end());
   });

   auto result = finishQ13(db, orderCounts);
   leaveQuery(nrThreads);
   return result;
}

std::unique_ptr<Q13Builder::Q13> Q13Builder::getQuery() {
   using namespace vectorwise;

   auto result = Result();
   previous = result.resultWriter.shared.result->participate();

   auto r = make_unique<Q13>();
   auto orders = Scan("orders");
   Select(Expression().addOp(
       primitives::sel_not_contains_special_requests_Varchar_79_col,
       Buffer(sel_orders, sizeof(pos_t)), Column(orders, "o_comment")));

   HashGroup()
       .pushKeySelVec(Buffer(sel_orders),
                      Buffer(orders_grouped, sizeof(pos_t)))
       .addKey(Column(orders, "o_custkey"), Buffer(sel_orders),
               primitives::hash_sel_int32_t_col,
               primitives::keys_not_equal_sel_int32_t_col,
               primitives::partition_by_key_sel_int32_t_col,
               Buffer(orders_grouped, sizeof(pos_t)),
               primitives::scatter_sel_int32_t_col,
               primitives::keys_not_equal_row_int32_t_col,
               primitives::partition_by_key_row_int32_t_col,
               primitives::scatter_sel_row_int32_t_col,
               primitives::gather_val_int32_t_col,
               Buffer(group_c_custkey, sizeof(types::Integer)))
       .addValue(Buffer(count_input, sizeof(int64_t)),
                 primitives::aggr_init_plus_int64_t_col,
                 primitives::aggr_count_star,
                 primitives::aggr_row_plus_int64_t_col,
                 primitives::gather_val_int64_t_col,
                 Buffer(c_count, sizeof(int64_t)));

   result.addValue("c_custkey", Buffer(group_c_custkey))
       .addValue("c_count", Buffer(c_count))
       .finalize();

   r->rootOp = popOperator();
   return r;
}

Relation q13_vectorwise(Database& db, size_t nrThreads, size_t vectorSize) {
   using namespace vectorwise;

   WorkerGroup workers(nrThreads);
   SharedStateManager shared;
   unique_ptr<runtime::Query> grouped;

   workers.run([&]() {
      Q13Builder builder(db, shared, vectorSize);
      auto query = builder.getQuery();
      query->rootOp->next();
      auto leader = barrier();
      if (leader)
         grouped = move(
             dynamic_cast<ResultWriter*>(query->rootOp.get())->shared.result);
   });

   if (!grouped) return makeQ13Result({});
   return finishQ13(db, collectQ13Groups(*grouped->result));
}
