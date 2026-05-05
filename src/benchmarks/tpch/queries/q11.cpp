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
//   ps_partkey,
//   sum(ps_supplycost * ps_availqty) as value
// from
//   partsupp,
//   supplier,
//   nation
// where
//   ps_suppkey = s_suppkey
//   and s_nationkey = n_nationkey
//   and n_name = 'GERMANY'
// group by
//   ps_partkey
// having
//   sum(ps_supplycost * ps_availqty) > (
//     select sum(ps_supplycost * ps_availqty) * 0.0001
//     from partsupp, supplier, nation
//     where ps_suppkey = s_suppkey
//       and s_nationkey = n_nationkey
//       and n_name = 'GERMANY')
// order by
//   value desc

namespace {

using StockValue = types::Numeric<12, 2>;

struct Q11Row {
   types::Integer ps_partkey;
   StockValue value;
};

Relation makeQ11Result(const vector<Q11Row>& rows) {
   Relation result;
   result.insert("ps_partkey", make_unique<algebra::Integer>());
   result.insert("value", make_unique<algebra::Numeric>(12, 2));

   auto& ps_partkey =
       result["ps_partkey"].typedAccessForChange<types::Integer>();
   auto& value = result["value"].typedAccessForChange<StockValue>();

   ps_partkey.reset(rows.size());
   value.reset(rows.size());

   for (auto row : rows) {
      ps_partkey.push_back(row.ps_partkey);
      value.push_back(row.value);
   }
   result.nrTuples = rows.size();
   return result;
}

Relation finishQ11(const vector<pair<types::Integer, StockValue>>& groups) {
   unordered_map<int32_t, StockValue> valueByPart;
   valueByPart.reserve(groups.size());
   for (const auto& group : groups)
      valueByPart[group.first.value] += group.second;

   int64_t totalRaw = 0;
   for (const auto& group : valueByPart) totalRaw += group.second.getRaw();

   vector<Q11Row> rows;
   rows.reserve(valueByPart.size());
   for (const auto& group : valueByPart) {
      if (static_cast<__int128>(group.second.getRaw()) * 10000 > totalRaw)
         rows.push_back({types::Integer(group.first), group.second});
   }

   sort(rows.begin(), rows.end(), [](const Q11Row& left,
                                     const Q11Row& right) {
      if (left.value != right.value) return left.value > right.value;
      return left.ps_partkey < right.ps_partkey;
   });
   return makeQ11Result(rows);
}

vector<pair<types::Integer, StockValue>> collectQ11Groups(
    BlockRelation& grouped) {
   vector<pair<types::Integer, StockValue>> values;
   auto partkeyAttr = grouped.getAttribute("ps_partkey");
   auto valueAttr = grouped.getAttribute("value");
   for (auto& block : grouped) {
      auto n = block.size();
      auto partkey =
          reinterpret_cast<types::Integer*>(block.data(partkeyAttr));
      auto value = reinterpret_cast<StockValue*>(block.data(valueAttr));
      for (size_t i = 0; i < n; ++i) values.emplace_back(partkey[i], value[i]);
   }
   return values;
}

} // namespace

NOVECTORIZE Relation q11_hyper(Database& db, size_t nrThreads) {
   using hash = runtime::CRC32Hash;

   auto resources = initQuery(nrThreads);
   (void)resources;

   auto germany = types::Char<25>::castString("GERMANY");

   auto& nation = db["nation"];
   auto n_nationkey = nation["n_nationkey"].data<types::Integer>();
   auto n_name = nation["n_name"].data<types::Char<25>>();

   Hashset<types::Integer, hash> germanNations;
   tbb::enumerable_thread_specific<
       runtime::Stack<decltype(germanNations)::Entry>>
       nationEntries;
   auto foundNations = PARALLEL_SELECT(nation.nrTuples, nationEntries, {
      if (n_name[i] == germany) {
         entries.emplace_back(germanNations.hash(n_nationkey[i]),
                              n_nationkey[i]);
         found++;
      }
   });
   germanNations.setSize(foundNations);
   parallel_insert(nationEntries, germanNations);

   auto& supplier = db["supplier"];
   auto s_suppkey = supplier["s_suppkey"].data<types::Integer>();
   auto s_nationkey = supplier["s_nationkey"].data<types::Integer>();

   Hashset<types::Integer, hash> germanSuppliers;
   tbb::enumerable_thread_specific<
       runtime::Stack<decltype(germanSuppliers)::Entry>>
       supplierEntries;
   auto foundSuppliers = PARALLEL_SELECT(supplier.nrTuples, supplierEntries, {
      if (germanNations.contains(s_nationkey[i])) {
         entries.emplace_back(germanSuppliers.hash(s_suppkey[i]),
                              s_suppkey[i]);
         found++;
      }
   });
   germanSuppliers.setSize(foundSuppliers);
   parallel_insert(supplierEntries, germanSuppliers);

   auto& partsupp = db["partsupp"];
   auto ps_partkey = partsupp["ps_partkey"].data<types::Integer>();
   auto ps_suppkey = partsupp["ps_suppkey"].data<types::Integer>();
   auto ps_availqty = partsupp["ps_availqty"].data<types::Integer>();
   auto ps_supplycost =
       partsupp["ps_supplycost"].data<types::Numeric<12, 2>>();

   auto groupOp = make_GroupBy<types::Integer, StockValue, hash>(
       [](auto& acc, auto&& value) { acc += value; }, StockValue(), nrThreads);

   tbb::parallel_for(
       tbb::blocked_range<size_t>(0, partsupp.nrTuples, morselSize),
       [&](const tbb::blocked_range<size_t>& r) {
          auto locals = groupOp.preAggLocals();
          for (size_t i = r.begin(), end = r.end(); i != end; ++i) {
             if (!germanSuppliers.contains(ps_suppkey[i])) continue;
             auto value = StockValue::buildRaw(ps_supplycost[i].getRaw() *
                                               ps_availqty[i].value);
             locals.consume(ps_partkey[i], value);
          }
       });

   vector<pair<types::Integer, StockValue>> groups;
   mutex groupsMutex;
   groupOp.forallGroups([&](auto& grouped) {
      vector<pair<types::Integer, StockValue>> local;
      local.reserve(grouped.size());
      for (auto block : grouped)
         for (auto& group : block) local.emplace_back(group.k, group.v);
      lock_guard<mutex> lock(groupsMutex);
      groups.insert(groups.end(), local.begin(), local.end());
   });

   auto result = finishQ11(groups);
   leaveQuery(nrThreads);
   return result;
}

std::unique_ptr<Q11Builder::Q11> Q11Builder::getQuery() {
   using namespace vectorwise;

   auto result = Result();
   previous = result.resultWriter.shared.result->participate();

   auto r = make_unique<Q11>();

   auto nation = Scan("nation");
   Select(Expression().addOp(
       BF(primitives::sel_equal_to_Char_25_col_Char_25_val),
       Buffer(sel_nation, sizeof(pos_t)), Column(nation, "n_name"),
       Value(&r->germany)));

   auto supplier = Scan("supplier");
   HashJoin(Buffer(nation_supplier, sizeof(pos_t)), conf.joinAll())
       .addBuildKey(Column(nation, "n_nationkey"), Buffer(sel_nation),
                    conf.hash_sel_int32_t_col(),
                    primitives::scatter_sel_int32_t_col)
       .addProbeKey(Column(supplier, "s_nationkey"),
                    conf.hash_int32_t_col(),
                    primitives::keys_equal_int32_t_col);

   auto partsupp = Scan("partsupp");
   HashJoin(Buffer(supplier_partsupp, sizeof(pos_t)), conf.joinAll())
       .addBuildKey(Column(supplier, "s_suppkey"), Buffer(nation_supplier),
                    conf.hash_sel_int32_t_col(),
                    primitives::scatter_sel_int32_t_col)
       .addProbeKey(Column(partsupp, "ps_suppkey"),
                    conf.hash_int32_t_col(),
                    primitives::keys_equal_int32_t_col);

   Project().addExpression(
       Expression()
           .addOp(primitives::proj_sel_plus_int32_t_col_int32_t_val,
                  Buffer(supplier_partsupp),
                  Buffer(compact_partkey, sizeof(int32_t)),
                  Column(partsupp, "ps_partkey"), Value(&r->zero))
           .addOp(primitives::proj_sel_multiplies_int64_t_col_int32_t_col,
                  Buffer(supplier_partsupp),
                  Buffer(part_value, sizeof(int64_t)),
                  Column(partsupp, "ps_supplycost"),
                  Column(partsupp, "ps_availqty")));

   HashGroup()
       .addKey(Buffer(compact_partkey), conf.hash_int32_t_col(),
               primitives::keys_not_equal_int32_t_col,
               primitives::partition_by_key_int32_t_col,
               primitives::scatter_sel_int32_t_col,
               primitives::keys_not_equal_row_int32_t_col,
               primitives::partition_by_key_row_int32_t_col,
               primitives::scatter_sel_row_int32_t_col,
               primitives::gather_val_int32_t_col,
               Buffer(group_partkey, sizeof(int32_t)))
       .addValue(Buffer(part_value), primitives::aggr_init_plus_int64_t_col,
                 primitives::aggr_plus_int64_t_col,
                 primitives::aggr_row_plus_int64_t_col,
                 primitives::gather_val_int64_t_col,
                 Buffer(group_value, sizeof(int64_t)));

   result.addValue("ps_partkey", Buffer(group_partkey))
       .addValue("value", Buffer(group_value))
       .finalize();

   r->rootOp = popOperator();
   return r;
}

Relation q11_vectorwise(Database& db, size_t nrThreads, size_t vectorSize) {
   using namespace vectorwise;

   WorkerGroup workers(nrThreads);
   SharedStateManager shared;
   unique_ptr<runtime::Query> grouped;

   workers.run([&]() {
      Q11Builder builder(db, shared, vectorSize);
      auto query = builder.getQuery();
      query->rootOp->next();
      auto leader = barrier();
      if (leader)
         grouped = move(
             dynamic_cast<ResultWriter*>(query->rootOp.get())->shared.result);
   });

   if (!grouped) return makeQ11Result({});
   return finishQ11(collectQ11Groups(*grouped->result));
}
