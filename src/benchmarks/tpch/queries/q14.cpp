#include "benchmarks/tpch/Queries.hpp"
#include "common/runtime/Concurrency.hpp"
#include "common/runtime/Hash.hpp"
#include "common/runtime/Types.hpp"
#include "hyper/ParallelHelper.hpp"
#include "tbb/tbb.h"
#include "vectorwise/Operations.hpp"
#include "vectorwise/Operators.hpp"
#include "vectorwise/Primitives.hpp"
#include "vectorwise/QueryBuilder.hpp"
#include "vectorwise/VectorAllocator.hpp"
#include <atomic>
#include <deque>

using namespace runtime;
using namespace std;

// select
//   100.00 * sum(case
//     when p_type like 'PROMO%'
//     then l_extendedprice * (1 - l_discount)
//     else 0
//   end) / sum(l_extendedprice * (1 - l_discount)) as promo_revenue
// from
//   lineitem,
//   part
// where
//   l_partkey = p_partkey
//   and l_shipdate >= date '1995-09-01'
//   and l_shipdate < date '1995-10-01'

// Holdout expectation: the one-month lineitem slice is small, about 76K probes
// into the 200K-row part lookup, followed by conditional fixed-point revenue
// aggregation. Both engines should be fast; Vectorwise may gain on the date
// filter/arithmetic, while Hyper should stay competitive because the join and
// scalar aggregate are too small for a large vectorization payoff.

namespace {

types::Numeric<12, 6>
computePromoRevenue(types::Numeric<12, 4> promoRevenue,
                    types::Numeric<12, 4> totalRevenue) {
   if (totalRevenue.getRaw() == 0) return types::Numeric<12, 6>();
   auto raw = static_cast<__int128>(promoRevenue.getRaw()) * 100000000 /
              totalRevenue.getRaw();
   return types::Numeric<12, 6>::buildRaw(static_cast<long>(raw));
}

void fillQ14Result(Relation& result, types::Numeric<12, 4> promoRevenue,
                   types::Numeric<12, 4> totalRevenue) {
   result.insert("promo_revenue", make_unique<algebra::Numeric>(12, 6));
   auto& output =
       result["promo_revenue"].typedAccessForChange<types::Numeric<12, 6>>();
   output.reset(1);
   auto percentage = computePromoRevenue(promoRevenue, totalRevenue);
   output.push_back(percentage);
   result.nrTuples = 1;
}

Relation makeQ14Result(types::Numeric<12, 4> promoRevenue,
                       types::Numeric<12, 4> totalRevenue) {
   Relation result;
   fillQ14Result(result, promoRevenue, totalRevenue);
   return result;
}

} // namespace

NOVECTORIZE Relation q14_hyper(Database& db, size_t /*nrThreads*/) {
   using namespace types;
   using hash = runtime::CRC32Hash;
   using range = tbb::blocked_range<size_t>;

   auto c1 = Date::castString("1995-09-01");
   auto c2 = Date::castString("1995-10-01");
   auto one = Numeric<12, 2>::castString("1.00");

   auto& part = db["part"];
   auto p_partkey = part["p_partkey"].data<Integer>();
   auto p_type = part["p_type"].data<Varchar<25>>();

   Hashmapx<Integer, int64_t, hash> partPromo;
   tbb::enumerable_thread_specific<deque<decltype(partPromo)::Entry>>
       partEntries;

   PARALLEL_SCAN(part.nrTuples, partEntries, {
      auto isPromo = startsWith(p_type[i], "PROMO", 5) ? int64_t(1) : int64_t(0);
      entries.emplace_back(partPromo.hash(p_partkey[i]), p_partkey[i],
                           isPromo);
   });
   partPromo.setSize(part.nrTuples);
   parallel_insert(partEntries, partPromo);

   auto& li = db["lineitem"];
   auto l_partkey = li["l_partkey"].data<Integer>();
   auto l_shipdate = li["l_shipdate"].data<Date>();
   auto l_extendedprice = li["l_extendedprice"].data<Numeric<12, 2>>();
   auto l_discount = li["l_discount"].data<Numeric<12, 2>>();

   using sums_t = tuple<Numeric<12, 4>, Numeric<12, 4>>;
   auto zero = Numeric<12, 4>();
   auto sums = tbb::parallel_reduce(
       range(0, li.nrTuples, morselSize), make_tuple(zero, zero),
       [&](const range& r, sums_t local) {
          for (size_t i = r.begin(), end = r.end(); i != end; ++i) {
             if (l_shipdate[i] >= c1 && l_shipdate[i] < c2) {
                auto isPromo = partPromo.findOne(l_partkey[i]);
                if (isPromo) {
                   auto revenue = l_extendedprice[i] * (one - l_discount[i]);
                   get<1>(local) += revenue;
                   if (*isPromo) get<0>(local) += revenue;
                }
             }
          }
          return local;
       },
       [](sums_t left, sums_t right) {
          get<0>(left) += get<0>(right);
          get<1>(left) += get<1>(right);
          return left;
       });

   auto result = makeQ14Result(get<0>(sums), get<1>(sums));
   return result;
}

std::unique_ptr<Q14Builder::Q14> Q14Builder::getQuery() {
   using namespace vectorwise;

   auto r = make_unique<Q14>();

   auto part = Scan("part");
   Project().addExpression(Expression().addOp(
       primitives::proj_q14_promo_flag, Buffer(promo_part, sizeof(int64_t)),
       Column(part, "p_type"), Value(&r->promo)));

   auto lineitem = Scan("lineitem");
   Select(Expression()
              .addOp(BF(primitives::sel_less_Date_col_Date_val),
                     Buffer(sel_lineitem, sizeof(pos_t)),
                     Column(lineitem, "l_shipdate"), Value(&r->c2))
              .addOp(BF(primitives::selsel_greater_equal_Date_col_Date_val),
                     Buffer(sel_lineitem, sizeof(pos_t)),
                     Buffer(sel_lineitem2, sizeof(pos_t)),
                     Column(lineitem, "l_shipdate"), Value(&r->c1)));

   HashJoin(Buffer(lineitem_part, sizeof(pos_t)), conf.joinAll())
       .setProbeSelVector(Buffer(sel_lineitem2), conf.joinSel())
       .addBuildKey(Column(part, "p_partkey"), conf.hash_int32_t_col(),
                    primitives::scatter_int32_t_col)
       .addBuildValue(Buffer(promo_part), primitives::scatter_int64_t_col,
                      Buffer(promo_part), primitives::gather_col_int64_t_col)
       .addProbeKey(Column(lineitem, "l_partkey"), Buffer(sel_lineitem2),
                    conf.hash_sel_int32_t_col(),
                    primitives::keys_equal_int32_t_col);

   Project().addExpression(
       Expression()
           .addOp(primitives::proj_sel_minus_int64_t_val_int64_t_col,
                  Buffer(lineitem_part),
                  Buffer(result_proj_minus, sizeof(int64_t)), Value(&r->one),
                  Column(lineitem, "l_discount"))
           .addOp(primitives::proj_multiplies_sel_int64_t_col_int64_t_col,
                  Buffer(lineitem_part), Buffer(revenue, sizeof(int64_t)),
                  Column(lineitem, "l_extendedprice"),
                  Buffer(result_proj_minus, sizeof(int64_t)))
           .addOp(primitives::proj_multiplies_int64_t_col_int64_t_col,
                  Buffer(promo_revenue_input, sizeof(int64_t)),
                  Buffer(revenue), Buffer(promo_part)));

   FixedAggregation(
       Expression()
           .addOp(primitives::aggr_static_plus_int64_t_col,
                  Value(&r->promoRevenue), Buffer(promo_revenue_input))
           .addOp(primitives::aggr_static_plus_int64_t_col,
                  Value(&r->totalRevenue), Buffer(revenue)));

   r->rootOp = popOperator();
   return r;
}

Relation q14_vectorwise(Database& db, size_t nrThreads, size_t vectorSize) {
   using namespace vectorwise;

   Relation result;
   SharedStateManager shared;
   WorkerGroup workers(nrThreads);
   GlobalPool pool;
   atomic<int64_t> promoRevenue;
   atomic<int64_t> totalRevenue;
   promoRevenue = 0;
   totalRevenue = 0;

   workers.run([&]() {
      Q14Builder builder(db, shared, vectorSize);
      builder.previous = this_worker->allocator.setSource(&pool);
      auto query = builder.getQuery();
      auto n = query->rootOp->next();
      if (n) {
         promoRevenue.fetch_add(query->promoRevenue);
         totalRevenue.fetch_add(query->totalRevenue);
      }

      auto leader = barrier();
      if (leader) {
         fillQ14Result(
             result, types::Numeric<12, 4>::buildRaw(promoRevenue.load()),
             types::Numeric<12, 4>::buildRaw(totalRevenue.load()));
      }
   });

   return result;
}
