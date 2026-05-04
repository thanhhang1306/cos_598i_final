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
#include <iostream>

using namespace runtime;
using namespace std;
using vectorwise::primitives::Char_15;

// select
//   o_orderpriority,
//   count(*) as order_count
// from
//   orders
// where
//   o_orderdate >= date '1993-07-01'
//   and o_orderdate < date '1993-10-01'
//   and exists (
//      select *
//      from lineitem
//      where l_orderkey = o_orderkey
//        and l_commitdate < l_receiptdate
//   )
// group by
//   o_orderpriority

// Holdout expectation: SF1 bottleneck is the semi-join, after building and
// deduplicating about 1.38M late lineitem order keys and probing only 57K
// date-filtered orders. Hyper should be strong on the large hash/group build;
// Vectorwise gets little help from the tiny final 5-group aggregate and may be
// limited by high-cardinality grouping/scatter work, like a smaller Q18.

NOVECTORIZE std::unique_ptr<runtime::Query> q4_hyper(Database& db,
                                                     size_t nrThreads) {
   using namespace types;
   using hash = runtime::CRC32Hash;

   auto resources = initQuery(nrThreads);

   auto& li = db["lineitem"];
   auto l_orderkey = li["l_orderkey"].data<Integer>();
   auto l_commitdate = li["l_commitdate"].data<Date>();
   auto l_receiptdate = li["l_receiptdate"].data<Date>();

   const int64_t zero = 0;
   auto lateOrderGroups = make_GroupBy<Integer, int64_t, hash>(
       [](auto& acc, auto&& value) { acc += value; }, zero, nrThreads);

   tbb::parallel_for(
       tbb::blocked_range<size_t>(0, li.nrTuples, morselSize),
       [&](const tbb::blocked_range<size_t>& r) {
          auto locals = lateOrderGroups.preAggLocals();
          for (size_t i = r.begin(), end = r.end(); i != end; ++i)
             if (l_commitdate[i] < l_receiptdate[i])
                locals.getGroup(l_orderkey[i]) = 1;
       });

   Hashset<Integer, hash> lateOrders;
   tbb::enumerable_thread_specific<runtime::Stack<decltype(lateOrders)::Entry>>
       lateEntries;
   std::atomic<size_t> nrLateOrders;
   nrLateOrders = 0;
   lateOrderGroups.forallGroups([&](auto& groups) {
      auto& entries = lateEntries.local();
      size_t found = 0;
      for (auto block : groups)
         for (auto& group : block) {
            entries.emplace_back(lateOrders.hash(group.k), group.k);
            found++;
         }
      nrLateOrders.fetch_add(found);
   });

   lateOrders.setSize(nrLateOrders);
   parallel_insert(lateEntries, lateOrders);

   auto& ord = db["orders"];
   auto o_orderkey = ord["o_orderkey"].data<Integer>();
   auto o_orderdate = ord["o_orderdate"].data<Date>();
   auto o_orderpriority = ord["o_orderpriority"].data<Char<15>>();

   auto c1 = Date::castString("1993-07-01");
   auto c2 = Date::castString("1993-10-01");
   auto orderPriorityGroups = make_GroupBy<Char<15>, int64_t, hash>(
       [](auto& acc, auto&& value) { acc += value; }, zero, nrThreads);

   tbb::parallel_for(
       tbb::blocked_range<size_t>(0, ord.nrTuples, morselSize),
       [&](const tbb::blocked_range<size_t>& r) {
          auto locals = orderPriorityGroups.preAggLocals();
          for (size_t i = r.begin(), end = r.end(); i != end; ++i) {
             if (o_orderdate[i] >= c1 && o_orderdate[i] < c2 &&
                 lateOrders.contains(o_orderkey[i])) {
                locals.consume(o_orderpriority[i], int64_t(1));
             }
          }
       });

   auto& result = resources.query->result;
   auto priorityAttr =
       result->addAttribute("o_orderpriority", sizeof(Char<15>));
   auto countAttr = result->addAttribute("order_count", sizeof(int64_t));

   orderPriorityGroups.forallGroups([&](auto& groups) {
      auto n = groups.size();
      auto block = result->createBlock(n);
      auto priority = reinterpret_cast<Char<15>*>(block.data(priorityAttr));
      auto count = reinterpret_cast<int64_t*>(block.data(countAttr));
      for (auto groupBlock : groups)
         for (auto& group : groupBlock) {
            *priority++ = group.k;
            *count++ = group.v;
         }
      block.addedElements(n);
   });

   leaveQuery(nrThreads);
   return move(resources.query);
}

std::unique_ptr<Q4Builder::Q4> Q4Builder::getQuery() {
   using namespace vectorwise;
   auto result = Result();
   previous = result.resultWriter.shared.result->participate();

   auto r = make_unique<Q4>();

   auto lineitem = Scan("lineitem");
   Select(Expression().addOp(BF(primitives::sel_less_Date_col_Date_col),
                             Buffer(sel_lineitem, sizeof(pos_t)),
                             Column(lineitem, "l_commitdate"),
                             Column(lineitem, "l_receiptdate")));
   HashGroup()
       .pushKeySelVec(Buffer(sel_lineitem),
                      Buffer(order_matches_grouped, sizeof(pos_t)))
       .addKey(Column(lineitem, "l_orderkey"), Buffer(sel_lineitem),
               primitives::hash_sel_int32_t_col,
               primitives::keys_not_equal_sel_int32_t_col,
               primitives::partition_by_key_sel_int32_t_col,
               Buffer(order_matches_grouped, sizeof(pos_t)),
               primitives::scatter_sel_int32_t_col,
               primitives::keys_not_equal_row_int32_t_col,
               primitives::partition_by_key_row_int32_t_col,
               primitives::scatter_sel_row_int32_t_col,
               primitives::gather_val_int32_t_col,
               Buffer(lineitem_orderkey, sizeof(int32_t)));

   auto orders = Scan("orders");
   Select(Expression()
              .addOp(BF(primitives::sel_less_Date_col_Date_val),
                     Buffer(sel_order, sizeof(pos_t)),
                     Column(orders, "o_orderdate"), Value(&r->c2))
              .addOp(BF(primitives::selsel_greater_equal_Date_col_Date_val),
                     Buffer(sel_order, sizeof(pos_t)),
                     Buffer(sel_order2, sizeof(pos_t)),
                     Column(orders, "o_orderdate"), Value(&r->c1)));

   HashJoin(Buffer(order_matches, sizeof(pos_t)), conf.joinAll())
       .setProbeSelVector(Buffer(sel_order2), conf.joinSel())
       .addBuildKey(Buffer(lineitem_orderkey), primitives::hash_int32_t_col,
                    primitives::scatter_int32_t_col)
       .addProbeKey(Column(orders, "o_orderkey"), Buffer(sel_order2),
                    conf.hash_sel_int32_t_col(),
                    primitives::keys_equal_int32_t_col);

   HashGroup()
       .pushKeySelVec(Buffer(order_matches),
                      Buffer(order_matches_grouped, sizeof(pos_t)))
       .addKey(Column(orders, "o_orderpriority"), Buffer(order_matches),
               primitives::hash_sel_Char_15_col,
               primitives::keys_not_equal_sel_Char_15_col,
               primitives::partition_by_key_sel_Char_15_col,
               Buffer(order_matches_grouped, sizeof(pos_t)),
               primitives::scatter_sel_Char_15_col,
               primitives::keys_not_equal_row_Char_15_col,
               primitives::partition_by_key_row_Char_15_col,
               primitives::scatter_sel_row_Char_15_col,
               primitives::gather_val_Char_15_col,
               Buffer(orderpriority, sizeof(Char_15)))
       .addValue(Buffer(order_count_input, sizeof(int64_t)),
                 primitives::aggr_init_plus_int64_t_col,
                 primitives::aggr_count_star,
                 primitives::aggr_row_plus_int64_t_col,
                 primitives::gather_val_int64_t_col,
                 Buffer(order_count, sizeof(int64_t)));

   result.addValue("o_orderpriority", Buffer(orderpriority))
       .addValue("order_count", Buffer(order_count))
       .finalize();

   r->rootOp = popOperator();
   return r;
}

std::unique_ptr<runtime::Query> q4_vectorwise(Database& db, size_t nrThreads,
                                              size_t vectorSize) {
   using namespace vectorwise;
   WorkerGroup workers(nrThreads);
   vectorwise::SharedStateManager shared;

   std::unique_ptr<runtime::Query> result;
   workers.run([&]() {
      Q4Builder builder(db, shared, vectorSize);
      auto query = builder.getQuery();
      query->rootOp->next();
      auto leader = barrier();
      if (leader)
         result = move(
             dynamic_cast<ResultWriter*>(query->rootOp.get())->shared.result);
   });

   return result;
}
