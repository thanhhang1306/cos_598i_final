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
using vectorwise::primitives::Char_10;
using vectorwise::primitives::Char_15;

// select
//   l_shipmode,
//   sum(case
//     when o_orderpriority = '1-URGENT' or o_orderpriority = '2-HIGH'
//     then 1 else 0
//   end) as high_line_count,
//   sum(case
//     when o_orderpriority <> '1-URGENT' and o_orderpriority <> '2-HIGH'
//     then 1 else 0
//   end) as low_line_count
// from
//   orders,
//   lineitem
// where
//   o_orderkey = l_orderkey
//   and l_shipmode in ('MAIL', 'SHIP')
//   and l_commitdate < l_receiptdate
//   and l_shipdate < l_commitdate
//   and l_receiptdate >= date '1994-01-01'
//   and l_receiptdate < date '1995-01-01'
// group by
//   l_shipmode

// Holdout expectation: the selective shipmode/date predicates leave only about
// 31K lineitems probing a 1.50M-row orders lookup, then a 2-group conditional
// count. Vectorwise should do well on the filter pipeline, but total time can
// be dominated by the large order-priority hash build; Hyper should be close or
// faster when the tiny probe/output does not amortize vector engine overhead.

NOVECTORIZE std::unique_ptr<runtime::Query> q12_hyper(Database& db,
                                                      size_t nrThreads) {
   using namespace types;
   using hash = runtime::CRC32Hash;

   auto resources = initQuery(nrThreads);

   auto c1 = Date::castString("1994-01-01");
   auto c2 = Date::castString("1995-01-01");
   auto mail = Char<10>::castString("MAIL");
   auto ship = Char<10>::castString("SHIP");
   auto urgent = Char<15>::castString("1-URGENT");
   auto high = Char<15>::castString("2-HIGH");

   auto& ord = db["orders"];
   auto o_orderkey = ord["o_orderkey"].data<Integer>();
   auto o_orderpriority = ord["o_orderpriority"].data<Char<15>>();

   Hashmapx<Integer, Char<15>, hash> orderPriorities;
   tbb::enumerable_thread_specific<runtime::Stack<
       decltype(orderPriorities)::Entry>>
       orderEntries;

   PARALLEL_SCAN(ord.nrTuples, orderEntries, {
      entries.emplace_back(orderPriorities.hash(o_orderkey[i]), o_orderkey[i],
                           o_orderpriority[i]);
   });
   orderPriorities.setSize(ord.nrTuples);
   parallel_insert(orderEntries, orderPriorities);

   auto& li = db["lineitem"];
   auto l_orderkey = li["l_orderkey"].data<Integer>();
   auto l_shipdate = li["l_shipdate"].data<Date>();
   auto l_commitdate = li["l_commitdate"].data<Date>();
   auto l_receiptdate = li["l_receiptdate"].data<Date>();
   auto l_shipmode = li["l_shipmode"].data<Char<10>>();

   using counts_t = tuple<int64_t, int64_t>;
   auto groupOp = make_GroupBy<Char<10>, counts_t, hash>(
       [](auto& acc, auto&& value) {
          get<0>(acc) += get<0>(value);
          get<1>(acc) += get<1>(value);
       },
       make_tuple(int64_t(0), int64_t(0)), nrThreads);

   tbb::parallel_for(
       tbb::blocked_range<size_t>(0, li.nrTuples, morselSize),
       [&](const tbb::blocked_range<size_t>& r) {
          auto locals = groupOp.preAggLocals();
          for (size_t i = r.begin(), end = r.end(); i != end; ++i) {
             if ((l_shipmode[i] == mail || l_shipmode[i] == ship) &&
                 l_commitdate[i] < l_receiptdate[i] &&
                 l_shipdate[i] < l_commitdate[i] &&
                 l_receiptdate[i] >= c1 && l_receiptdate[i] < c2) {
                auto priority = orderPriorities.findOne(l_orderkey[i]);
                if (priority) {
                   auto& group = locals.getGroup(l_shipmode[i]);
                   if (*priority == urgent || *priority == high)
                      get<0>(group) += 1;
                   else
                      get<1>(group) += 1;
                }
             }
          }
       });

   auto& result = resources.query->result;
   auto shipmodeAttr = result->addAttribute("l_shipmode", sizeof(Char<10>));
   auto highAttr = result->addAttribute("high_line_count", sizeof(int64_t));
   auto lowAttr = result->addAttribute("low_line_count", sizeof(int64_t));

   groupOp.forallGroups([&](auto& groups) {
      auto n = groups.size();
      auto block = result->createBlock(n);
      auto shipmode = reinterpret_cast<Char<10>*>(block.data(shipmodeAttr));
      auto highCount = reinterpret_cast<int64_t*>(block.data(highAttr));
      auto lowCount = reinterpret_cast<int64_t*>(block.data(lowAttr));
      for (auto groupBlock : groups)
         for (auto& group : groupBlock) {
            *shipmode++ = group.k;
            *highCount++ = get<0>(group.v);
            *lowCount++ = get<1>(group.v);
         }
      block.addedElements(n);
   });

   leaveQuery(nrThreads);
   return move(resources.query);
}

std::unique_ptr<Q12Builder::Q12> Q12Builder::getQuery() {
   using namespace vectorwise;

   auto result = Result();
   previous = result.resultWriter.shared.result->participate();

   auto r = make_unique<Q12>();

   auto orders = Scan("orders");
   auto lineitem = Scan("lineitem");
   Select(Expression()
              .addOp(
                  primitives::
                      sel_equal_to_Char_10_col_Char_10_val_or_Char_10_val,
                  Buffer(sel_lineitem, sizeof(pos_t)),
                  Column(lineitem, "l_shipmode"), Value(&r->mail),
                  Value(&r->ship))
              .addOp(BF(primitives::selsel_less_Date_col_Date_col),
                     Buffer(sel_lineitem, sizeof(pos_t)),
                     Buffer(sel_lineitem2, sizeof(pos_t)),
                     Column(lineitem, "l_commitdate"),
                     Column(lineitem, "l_receiptdate"))
              .addOp(BF(primitives::selsel_less_Date_col_Date_col),
                     Buffer(sel_lineitem2, sizeof(pos_t)),
                     Buffer(sel_lineitem, sizeof(pos_t)),
                     Column(lineitem, "l_shipdate"),
                     Column(lineitem, "l_commitdate"))
              .addOp(BF(primitives::selsel_greater_equal_Date_col_Date_val),
                     Buffer(sel_lineitem, sizeof(pos_t)),
                     Buffer(sel_lineitem2, sizeof(pos_t)),
                     Column(lineitem, "l_receiptdate"), Value(&r->c1))
              .addOp(BF(primitives::selsel_less_Date_col_Date_val),
                     Buffer(sel_lineitem2, sizeof(pos_t)),
                     Buffer(sel_lineitem, sizeof(pos_t)),
                     Column(lineitem, "l_receiptdate"), Value(&r->c2)));

   HashJoin(Buffer(lineitem_orders, sizeof(pos_t)), conf.joinAll())
       .setProbeSelVector(Buffer(sel_lineitem), conf.joinSel())
       .addBuildKey(Column(orders, "o_orderkey"), conf.hash_int32_t_col(),
                    primitives::scatter_int32_t_col)
       .addBuildValue(Column(orders, "o_orderpriority"),
                      primitives::scatter_Char_15_col,
                      Buffer(o_orderpriority, sizeof(Char_15)),
                      primitives::gather_col_Char_15_col)
       .addProbeKey(Column(lineitem, "l_orderkey"), Buffer(sel_lineitem),
                    conf.hash_sel_int32_t_col(),
                    primitives::keys_equal_int32_t_col);

   Project().addExpression(
       Expression()
           .addOp(primitives::proj_q12_high_priority_count,
                  Buffer(high_line_count_input, sizeof(int64_t)),
                  Buffer(o_orderpriority), Value(r->highPriorities))
           .addOp(primitives::proj_q12_low_priority_count,
                  Buffer(low_line_count_input, sizeof(int64_t)),
                  Buffer(o_orderpriority), Value(r->highPriorities)));

   HashGroup()
       .pushKeySelVec(Buffer(lineitem_orders),
                      Buffer(lineitem_orders_grouped, sizeof(pos_t)))
       .addKey(Column(lineitem, "l_shipmode"), Buffer(lineitem_orders),
               primitives::hash_sel_Char_10_col,
               primitives::keys_not_equal_sel_Char_10_col,
               primitives::partition_by_key_sel_Char_10_col,
               Buffer(lineitem_orders_grouped, sizeof(pos_t)),
               primitives::scatter_sel_Char_10_col,
               primitives::keys_not_equal_row_Char_10_col,
               primitives::partition_by_key_row_Char_10_col,
               primitives::scatter_sel_row_Char_10_col,
               primitives::gather_val_Char_10_col,
               Buffer(l_shipmode, sizeof(Char_10)))
       .addValue(Buffer(high_line_count_input),
                 primitives::aggr_init_plus_int64_t_col,
                 primitives::aggr_plus_int64_t_col,
                 primitives::aggr_row_plus_int64_t_col,
                 primitives::gather_val_int64_t_col,
                 Buffer(high_line_count, sizeof(int64_t)))
       .addValue(Buffer(low_line_count_input),
                 primitives::aggr_init_plus_int64_t_col,
                 primitives::aggr_plus_int64_t_col,
                 primitives::aggr_row_plus_int64_t_col,
                 primitives::gather_val_int64_t_col,
                 Buffer(low_line_count, sizeof(int64_t)));

   result.addValue("l_shipmode", Buffer(l_shipmode))
       .addValue("high_line_count", Buffer(high_line_count))
       .addValue("low_line_count", Buffer(low_line_count))
       .finalize();

   r->rootOp = popOperator();
   return r;
}

std::unique_ptr<runtime::Query> q12_vectorwise(Database& db, size_t nrThreads,
                                               size_t vectorSize) {
   using namespace vectorwise;
   WorkerGroup workers(nrThreads);
   vectorwise::SharedStateManager shared;

   std::unique_ptr<runtime::Query> result;
   workers.run([&]() {
      Q12Builder builder(db, shared, vectorSize);
      auto query = builder.getQuery();
      query->rootOp->next();
      auto leader = barrier();
      if (leader)
         result = move(
             dynamic_cast<ResultWriter*>(query->rootOp.get())->shared.result);
   });

   return result;
}
