#pragma once

#include <memory>

#include "benchmarks/Config.hpp"
#include "common/runtime/Concurrency.hpp"
#include "common/runtime/Database.hpp"
#include "common/runtime/Types.hpp"
#include "vectorwise/Operators.hpp"
#include "vectorwise/Query.hpp"
#include "vectorwise/QueryBuilder.hpp"

struct Q1Builder : public Query, private vectorwise::QueryBuilder {
   enum {
      sel_date,
      sel_date_grouped,
      selScat,
      result_proj_minus,
      result_proj_plus,
      disc_price,
      charge,
      returnflag,
      linestatus,
      sum_qty,
      sum_base_price,
      sum_disc_price,
      sum_charge,
      sum_discount,
      avg_qty,
      avg_price,
      avg_disc,
      count_order
   };
   struct Q1 {
      types::Numeric<12, 2> one = types::Numeric<12, 2>::castString("1.00");
      types::Date c1 = types::Date::castString("1998-09-02");
      std::unique_ptr<vectorwise::Operator> rootOp;
   };
   Q1Builder(runtime::Database& db, vectorwise::SharedStateManager& shared,
             size_t size = 1024)
       : QueryBuilder(db, shared, size) {}
   std::unique_ptr<Q1> getQuery();
};

std::unique_ptr<runtime::Query>
q1_hyper(runtime::Database& db,
         size_t nrThreads = std::thread::hardware_concurrency());
std::unique_ptr<runtime::Query>
q1_vectorwise(runtime::Database& db,
              size_t nrThreads = std::thread::hardware_concurrency(),
              size_t vectorSize = 1024);

struct Q3Builder : private vectorwise::QueryBuilder {
   enum {
      sel_order,
      sel_cust,
      cust_ord,
      j1_lineitem,
      j1_lineitem_grouped,
      sel_lineitem,
      result_project,
      l_orderkey,
      o_orderdate,
      o_shippriority,
      result_proj_minus
   };
   struct Q3 {
      std::string building = "BUILDING";
      types::Char<10> c1 =
          types::Char<10>::castString(building.data(), building.size());
      types::Date c2 = types::Date::castString("1995-03-15");
      types::Date c3 = types::Date::castString("1995-03-15");
      types::Numeric<12, 2> one = types::Numeric<12, 2>::castString("1.00");
      int64_t revenue = 0;
      int64_t sum = 0;
      int64_t count = 0;
      size_t n = 0;
      std::unique_ptr<vectorwise::Operator> rootOp;
   };
   Q3Builder(runtime::Database& db, vectorwise::SharedStateManager& shared,
             size_t size = 1024)
       : QueryBuilder(db, shared, size) {}
   std::unique_ptr<Q3> getQuery();
};

std::unique_ptr<runtime::Query>
q3_hyper(runtime::Database& db,
         size_t nrThreads = std::thread::hardware_concurrency());
std::unique_ptr<runtime::Query>
q3_vectorwise(runtime::Database& db,
              size_t nrThreads = std::thread::hardware_concurrency(),
              size_t vectorSize = 1024);

struct Q4Builder : private vectorwise::QueryBuilder {
   enum {
      sel_lineitem,
      lineitem_orderkey,
      sel_order,
      sel_order2,
      order_matches,
      order_matches_grouped,
      orderpriority,
      order_count_input,
      order_count
   };
   struct Q4 {
      types::Date c1 = types::Date::castString("1993-07-01");
      types::Date c2 = types::Date::castString("1993-10-01");
      std::unique_ptr<vectorwise::Operator> rootOp;
   };
   Q4Builder(runtime::Database& db, vectorwise::SharedStateManager& shared,
             size_t size = 1024)
       : QueryBuilder(db, shared, size) {}
   std::unique_ptr<Q4> getQuery();
};

std::unique_ptr<runtime::Query>
q4_hyper(runtime::Database& db,
         size_t nrThreads = std::thread::hardware_concurrency());
std::unique_ptr<runtime::Query>
q4_vectorwise(runtime::Database& db,
              size_t nrThreads = std::thread::hardware_concurrency(),
              size_t vectorSize = 1024);

struct Q5Builder : private vectorwise::QueryBuilder {
   enum {
      sel_region,
      sel_ord,
      sel_ord2,
      join_reg_nat,
      join_cust,
      join_ord,
      join_ord_nationkey,
      join_line,
      join_line_nationkey,
      join_supp,
      result_project,
      join_supp_line,
      n_name,
      n_name2,
      selScat,
      result_proj_minus,
      sum
   };
   struct Q5 {
      types::Numeric<12, 2> one = types::Numeric<12, 2>::castString("1.00");
      types::Date c1 = types::Date::castString("1994-01-01");
      types::Date c2 = types::Date::castString("1995-01-01");
      std::string region = "ASIA";
      types::Char<25> c3 =
          types::Char<25>::castString(region.data(), region.size());
      std::unique_ptr<vectorwise::Operator> rootOp;
   };
   Q5Builder(runtime::Database& db, vectorwise::SharedStateManager& shared,
             size_t size = 1024)
       : QueryBuilder(db, shared, size) {}
   std::unique_ptr<Q5> getQuery();
   std::unique_ptr<Q5> getNoSelQuery();
};

std::unique_ptr<runtime::Query>
q5_hyper(runtime::Database& db,
         size_t nrThreads = std::thread::hardware_concurrency());
std::unique_ptr<runtime::Query>
q5_vectorwise(runtime::Database& db,
              size_t nrThreads = std::thread::hardware_concurrency(),
              size_t vectorSize = 1024);

runtime::Relation q5_no_sel_hyper(runtime::Database& db);
std::unique_ptr<runtime::BlockRelation>
q5_no_sel_vectorwise(runtime::Database& db,
                     size_t nrThreads = std::thread::hardware_concurrency());

class Q6Builder : public vectorwise::QueryBuilder {

 public:
   struct Q6 {
      types::Date c1 = types::Date::castString("1994-01-01");
      types::Date c2 = types::Date::castString("1995-01-01");
      types::Numeric<12, 2> c3 = types::Numeric<12, 2>::castString("0.05");
      types::Numeric<12, 2> c4 = types::Numeric<12, 2>::castString("0.07");
      types::Numeric<12, 2> c5 = types::Numeric<12, 2>(types::Integer(24));
      size_t n;
      int64_t aggregator = 0;
      std::unique_ptr<vectorwise::Operator> rootOp;
   };

   std::unique_ptr<Q6> getQuery();
   Q6Builder(runtime::Database& db, vectorwise::SharedStateManager& shared,
             size_t size = 1024)
       : QueryBuilder(db, shared, size) {}
};

runtime::Relation
q6_hyper(runtime::Database& db,
         size_t nrThreads = std::thread::hardware_concurrency());
runtime::Relation
q6_vectorwise(runtime::Database& db,
              size_t nrThreads = std::thread::hardware_concurrency(),
              size_t vectorSize = 1024);

struct Q9Builder : public Query, private vectorwise::QueryBuilder {
   enum {
      nation_supplier,
      part_partsupp,
      pspp,
      xlineitem,
      ordersx,
      n_name,
      ps_supplycost,
      xlineitem_ord,
      disc_price,
      total_cost,
      sel_part,
      l_extendedprice,
      l_discount,
      l_quantity,
      result_proj_minus,
      amount,
      o_year,
      sum_profit
   };
   struct Q9 {
      types::Varchar<55> contains = types::Varchar<55>::castString("green");
      types::Numeric<12, 2> one = types::Numeric<12, 2>::castString("1.00");
      std::unique_ptr<vectorwise::Operator> rootOp;
   };
   Q9Builder(runtime::Database& db, vectorwise::SharedStateManager& shared,
             size_t size = 1024)
       : QueryBuilder(db, shared, size) {}
   std::unique_ptr<Q9> getQuery();
};

std::unique_ptr<runtime::Query>
q9_hyper(runtime::Database& db,
         size_t nrThreads = std::thread::hardware_concurrency());
std::unique_ptr<runtime::Query>
q9_vectorwise(runtime::Database& db,
              size_t nrThreads = std::thread::hardware_concurrency(),
              size_t vectorSize = 1024);

struct Q10Builder : private vectorwise::QueryBuilder {
   enum {
      sel_order,
      sel_order2,
      sel_lineitem,
      order_line,
      o_custkey,
      result_proj_minus,
      revenue,
      group_c_custkey,
      group_revenue
   };
   struct Q10 {
      types::Date c1 = types::Date::castString("1993-10-01");
      types::Date c2 = types::Date::castString("1994-01-01");
      types::Char<1> returned = types::Char<1>::castString("R");
      types::Numeric<12, 2> one =
          types::Numeric<12, 2>::castString("1.00");
      std::unique_ptr<vectorwise::Operator> rootOp;
   };
   Q10Builder(runtime::Database& db, vectorwise::SharedStateManager& shared,
              size_t size = 1024)
       : QueryBuilder(db, shared, size) {}
   std::unique_ptr<Q10> getQuery();
};

runtime::Relation
q10_hyper(runtime::Database& db,
          size_t nrThreads = std::thread::hardware_concurrency());
runtime::Relation
q10_vectorwise(runtime::Database& db,
               size_t nrThreads = std::thread::hardware_concurrency(),
               size_t vectorSize = 1024);

struct Q12Builder : private vectorwise::QueryBuilder {
   enum {
      sel_lineitem,
      sel_lineitem2,
      lineitem_orders,
      lineitem_orders_grouped,
      o_orderpriority,
      high_line_count_input,
      low_line_count_input,
      l_shipmode,
      high_line_count,
      low_line_count
   };
   struct Q12 {
      types::Date c1 = types::Date::castString("1994-01-01");
      types::Date c2 = types::Date::castString("1995-01-01");
      types::Char<10> mail = types::Char<10>::castString("MAIL");
      types::Char<10> ship = types::Char<10>::castString("SHIP");
      types::Char<15> highPriorities[2] = {
          types::Char<15>::castString("1-URGENT"),
          types::Char<15>::castString("2-HIGH")};
      std::unique_ptr<vectorwise::Operator> rootOp;
   };
   Q12Builder(runtime::Database& db, vectorwise::SharedStateManager& shared,
              size_t size = 1024)
       : QueryBuilder(db, shared, size) {}
   std::unique_ptr<Q12> getQuery();
};

std::unique_ptr<runtime::Query>
q12_hyper(runtime::Database& db,
          size_t nrThreads = std::thread::hardware_concurrency());
std::unique_ptr<runtime::Query>
q12_vectorwise(runtime::Database& db,
               size_t nrThreads = std::thread::hardware_concurrency(),
               size_t vectorSize = 1024);

struct Q14Builder : public vectorwise::QueryBuilder {
   enum {
      promo_part,
      sel_lineitem,
      sel_lineitem2,
      lineitem_part,
      result_proj_minus,
      revenue,
      promo_revenue_input
   };
   struct Q14 {
      types::Date c1 = types::Date::castString("1995-09-01");
      types::Date c2 = types::Date::castString("1995-10-01");
      types::Numeric<12, 2> one =
          types::Numeric<12, 2>::castString("1.00");
      types::Varchar<25> promo = types::Varchar<25>::castString("PROMO");
      int64_t promoRevenue = 0;
      int64_t totalRevenue = 0;
      std::unique_ptr<vectorwise::Operator> rootOp;
   };
   Q14Builder(runtime::Database& db, vectorwise::SharedStateManager& shared,
              size_t size = 1024)
       : QueryBuilder(db, shared, size) {}
   std::unique_ptr<Q14> getQuery();
};

runtime::Relation
q14_hyper(runtime::Database& db,
          size_t nrThreads = std::thread::hardware_concurrency());
runtime::Relation
q14_vectorwise(runtime::Database& db,
               size_t nrThreads = std::thread::hardware_concurrency(),
               size_t vectorSize = 1024);

struct Q15Builder : public vectorwise::QueryBuilder {
   enum {
      sel_lineitem,
      sel_lineitem2,
      compact_suppkey,
      result_proj_minus,
      revenue,
      supplier_no,
      total_revenue
   };
   struct Q15 {
      types::Date c1 = types::Date::castString("1996-01-01");
      types::Date c2 = types::Date::castString("1996-04-01");
      types::Numeric<12, 2> one =
          types::Numeric<12, 2>::castString("1.00");
      int32_t zero = 0;
      std::unique_ptr<vectorwise::Operator> rootOp;
   };
   Q15Builder(runtime::Database& db, vectorwise::SharedStateManager& shared,
              size_t size = 1024)
       : QueryBuilder(db, shared, size) {}
   std::unique_ptr<Q15> getQuery();
};

runtime::Relation
q15_hyper(runtime::Database& db,
          size_t nrThreads = std::thread::hardware_concurrency());
runtime::Relation
q15_vectorwise(runtime::Database& db,
               size_t nrThreads = std::thread::hardware_concurrency(),
               size_t vectorSize = 1024);

struct Q17Builder : public vectorwise::QueryBuilder {
   enum {
      group_l_partkey,
      group_quantity,
      group_count_input,
      group_count,
      sel_part_brand,
      sel_part,
      part_matches,
      part_quantity,
      part_count,
      part_count_x5,
      part_quantity_minus_one,
      part_threshold,
      lineitem_matches,
      lineitem_threshold,
      compact_quantity,
      compact_extendedprice,
      sel_lineitem
   };
   struct Q17 {
      types::Char<10> brand = types::Char<10>::castString("Brand#23");
      types::Char<10> container = types::Char<10>::castString("MED BOX");
      int64_t five = 5;
      int64_t minusOne = -1;
      int64_t zero = 0;
      int64_t sum_extendedprice = 0;
      std::unique_ptr<vectorwise::Operator> rootOp;
   };

   Q17Builder(runtime::Database& db, vectorwise::SharedStateManager& shared,
              size_t size = 1024)
       : QueryBuilder(db, shared, size) {}
   std::unique_ptr<Q17> getQuery();
};

runtime::Relation
q17_hyper(runtime::Database& db,
          size_t nrThreads = std::thread::hardware_concurrency());
runtime::Relation
q17_vectorwise(runtime::Database& db,
               size_t nrThreads = std::thread::hardware_concurrency(),
               size_t vectorSize = 1024);

struct Q18Builder : public Query, private vectorwise::QueryBuilder {
   enum {
      l_orderkey,
      l_quantity,
      sel_orderkey,
      orders_matches,
      customer_matches,
      c_name,
      c_name2,
      lineitem_matches,
      o_custkey,
      o_orderdate,
      o_totalprice,
      group_c_name,
      group_o_custkey,
      group_l_orderkey,
      group_o_orderdate,
      group_o_totalprice,
      group_sum,
      lineitem_matches_grouped,
      compact_quantity,
      compact_l_orderkey
   };
   struct Q18 {
      uint64_t zero = 0;
      types::Numeric<12, 2> qty_bound =
          types::Numeric<12, 2>::castString("300");
      std::unique_ptr<vectorwise::Operator> rootOp;
   };
   Q18Builder(runtime::Database& db, vectorwise::SharedStateManager& shared,
              size_t size = 1024)
       : QueryBuilder(db, shared, size) {}
   std::unique_ptr<Q18> getQuery();
   std::unique_ptr<Q18> getGroupQuery();
};

std::unique_ptr<runtime::Query>
q18_hyper(runtime::Database& db,
          size_t nrThreads = std::thread::hardware_concurrency());
std::unique_ptr<runtime::Query>
q18_vectorwise(runtime::Database& db,
               size_t nrThreads = std::thread::hardware_concurrency(),
               size_t vectorSize = 1024);
std::unique_ptr<runtime::Query>
q18group_vectorwise(runtime::Database& db,
                    size_t nrThreads = std::thread::hardware_concurrency(),
                    size_t vectorSize = 1024);
