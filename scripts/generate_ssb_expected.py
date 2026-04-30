#!/usr/bin/env python3
"""Generate SSB test expectations from raw .tbl files.

The C++ tests store expected SSB answers in CSV fixtures, while Q1.1-Q1.3
store raw numeric constants directly in src/test/ssb.cpp. This script computes
the same reference answers from the raw Star Schema Benchmark .tbl data without
using the query engine under test.
"""

from __future__ import annotations

import argparse
from collections import defaultdict
from pathlib import Path


CITY_PAIR = {"UNITED KI1", "UNITED KI5"}


def rows(path: Path):
    with path.open("r", encoding="utf-8") as handle:
        for line in handle:
            fields = line.rstrip("\n").split("|")
            if fields and fields[-1] == "":
                fields.pop()
            yield fields


def money(value: int) -> str:
    return f"{value}.00"


def write_csv(path: Path, values: dict[tuple, int]) -> None:
    with path.open("w", encoding="utf-8") as handle:
        for key, value in sorted(values.items()):
            columns = [str(item) for item in key]
            columns.append(money(value))
            handle.write(",".join(columns) + "\n")


def load_dimensions(data_dir: Path):
    dates = {}
    date_year_1993 = set()
    date_yearmonth_199401 = set()
    date_year_week_1994_6 = set()
    date_year_1992_1997 = {}
    date_dec_1997 = {}
    date_year_1997_1998 = {}

    for row in rows(data_dir / "date.tbl"):
        datekey = int(row[0])
        year = int(row[4])
        yearmonthnum = int(row[5])
        yearmonth = row[6]
        weeknuminyear = int(row[11])

        dates[datekey] = year
        if year == 1993:
            date_year_1993.add(datekey)
        if yearmonthnum == 199401:
            date_yearmonth_199401.add(datekey)
        if year == 1994 and weeknuminyear == 6:
            date_year_week_1994_6.add(datekey)
        if 1992 <= year <= 1997:
            date_year_1992_1997[datekey] = year
        if yearmonth == "Dec1997":
            date_dec_1997[datekey] = year
        if year in (1997, 1998):
            date_year_1997_1998[datekey] = year

    parts_q21 = {}
    parts_q22 = {}
    parts_q23 = {}
    parts_mfgr_1_or_2 = set()
    parts_q42 = {}
    parts_q43 = {}

    for row in rows(data_dir / "part.tbl"):
        partkey = int(row[0])
        category = row[3]
        brand = row[4]
        mfgr = row[2]

        if category == "MFGR#12":
            parts_q21[partkey] = brand
        if "MFGR#2221" <= brand <= "MFGR#2228":
            parts_q22[partkey] = brand
        if brand == "MFGR#2221":
            parts_q23[partkey] = brand
        if mfgr in ("MFGR#1", "MFGR#2"):
            parts_mfgr_1_or_2.add(partkey)
            parts_q42[partkey] = category
        if category == "MFGR#14":
            parts_q43[partkey] = brand

    suppliers_america = set()
    suppliers_asia = set()
    suppliers_europe = set()
    suppliers_asia_nation = {}
    suppliers_us_city = {}
    suppliers_city_pair = {}
    suppliers_america_nation = {}

    for row in rows(data_dir / "supplier.tbl"):
        suppkey = int(row[0])
        city = row[3]
        nation = row[4]
        region = row[5]

        if region == "AMERICA":
            suppliers_america.add(suppkey)
            suppliers_america_nation[suppkey] = nation
        if region == "ASIA":
            suppliers_asia.add(suppkey)
            suppliers_asia_nation[suppkey] = nation
        if region == "EUROPE":
            suppliers_europe.add(suppkey)
        if nation == "UNITED STATES":
            suppliers_us_city[suppkey] = city
        if city in CITY_PAIR:
            suppliers_city_pair[suppkey] = city

    customers_asia_nation = {}
    customers_us_city = {}
    customers_city_pair = {}
    customers_america_nation = {}
    customers_america = set()

    for row in rows(data_dir / "customer.tbl"):
        custkey = int(row[0])
        city = row[3]
        nation = row[4]
        region = row[5]

        if region == "ASIA":
            customers_asia_nation[custkey] = nation
        if nation == "UNITED STATES":
            customers_us_city[custkey] = city
        if city in CITY_PAIR:
            customers_city_pair[custkey] = city
        if region == "AMERICA":
            customers_america.add(custkey)
            customers_america_nation[custkey] = nation

    return {
        "dates": dates,
        "date_year_1993": date_year_1993,
        "date_yearmonth_199401": date_yearmonth_199401,
        "date_year_week_1994_6": date_year_week_1994_6,
        "date_year_1992_1997": date_year_1992_1997,
        "date_dec_1997": date_dec_1997,
        "date_year_1997_1998": date_year_1997_1998,
        "parts_q21": parts_q21,
        "parts_q22": parts_q22,
        "parts_q23": parts_q23,
        "parts_mfgr_1_or_2": parts_mfgr_1_or_2,
        "parts_q42": parts_q42,
        "parts_q43": parts_q43,
        "suppliers_america": suppliers_america,
        "suppliers_asia": suppliers_asia,
        "suppliers_europe": suppliers_europe,
        "suppliers_asia_nation": suppliers_asia_nation,
        "suppliers_us_city": suppliers_us_city,
        "suppliers_city_pair": suppliers_city_pair,
        "suppliers_america_nation": suppliers_america_nation,
        "customers_asia_nation": customers_asia_nation,
        "customers_us_city": customers_us_city,
        "customers_city_pair": customers_city_pair,
        "customers_america_nation": customers_america_nation,
        "customers_america": customers_america,
    }


def compute(data_dir: Path):
    dim = load_dimensions(data_dir)

    q11 = 0
    q12 = 0
    q13 = 0

    q21 = defaultdict(int)
    q22 = defaultdict(int)
    q23 = defaultdict(int)
    q31 = defaultdict(int)
    q32 = defaultdict(int)
    q33 = defaultdict(int)
    q34 = defaultdict(int)
    q41 = defaultdict(int)
    q42 = defaultdict(int)
    q43 = defaultdict(int)

    for row in rows(data_dir / "lineorder.tbl"):
        custkey = int(row[2])
        partkey = int(row[3])
        suppkey = int(row[4])
        orderdate = int(row[5])
        quantity = int(row[8])
        extendedprice = int(row[9])
        discount = int(row[11])
        revenue = int(row[12])
        supplycost = int(row[13])
        profit = revenue - supplycost

        if (
            orderdate in dim["date_year_1993"]
            and quantity < 25
            and 1 <= discount <= 3
        ):
            q11 += extendedprice * discount
        if (
            orderdate in dim["date_yearmonth_199401"]
            and 26 <= quantity <= 35
            and 4 <= discount <= 6
        ):
            q12 += extendedprice * discount
        if (
            orderdate in dim["date_year_week_1994_6"]
            and 26 <= quantity <= 35
            and 5 <= discount <= 7
        ):
            q13 += extendedprice * discount

        year = dim["dates"].get(orderdate)
        if year is not None:
            brand = dim["parts_q21"].get(partkey)
            if brand is not None and suppkey in dim["suppliers_america"]:
                q21[(year, brand)] += revenue

            brand = dim["parts_q22"].get(partkey)
            if brand is not None and suppkey in dim["suppliers_asia"]:
                q22[(year, brand)] += revenue

            brand = dim["parts_q23"].get(partkey)
            if brand is not None and suppkey in dim["suppliers_europe"]:
                q23[(year, brand)] += revenue

            c_nation = dim["customers_america_nation"].get(custkey)
            if (
                c_nation is not None
                and suppkey in dim["suppliers_america"]
                and partkey in dim["parts_mfgr_1_or_2"]
            ):
                q41[(year, c_nation)] += profit

        year = dim["date_year_1992_1997"].get(orderdate)
        if year is not None:
            c_nation = dim["customers_asia_nation"].get(custkey)
            s_nation = dim["suppliers_asia_nation"].get(suppkey)
            if c_nation is not None and s_nation is not None:
                q31[(c_nation, s_nation, year)] += revenue

            c_city = dim["customers_us_city"].get(custkey)
            s_city = dim["suppliers_us_city"].get(suppkey)
            if c_city is not None and s_city is not None:
                q32[(c_city, s_city, year)] += revenue

            c_city = dim["customers_city_pair"].get(custkey)
            s_city = dim["suppliers_city_pair"].get(suppkey)
            if c_city is not None and s_city is not None:
                q33[(c_city, s_city, year)] += revenue

        year = dim["date_dec_1997"].get(orderdate)
        if year is not None:
            c_city = dim["customers_city_pair"].get(custkey)
            s_city = dim["suppliers_city_pair"].get(suppkey)
            if c_city is not None and s_city is not None:
                q34[(c_city, s_city, year)] += revenue

        year = dim["date_year_1997_1998"].get(orderdate)
        if year is not None:
            category = dim["parts_q42"].get(partkey)
            s_nation = dim["suppliers_america_nation"].get(suppkey)
            if (
                category is not None
                and s_nation is not None
                and custkey in dim["customers_america"]
            ):
                q42[(year, s_nation, category)] += profit

            brand = dim["parts_q43"].get(partkey)
            s_city = dim["suppliers_us_city"].get(suppkey)
            if (
                brand is not None
                and s_city is not None
                and custkey in dim["customers_america"]
            ):
                q43[(year, s_city, brand)] += profit

    return {
        "q11": q11 * 10000,
        "q12": q12 * 10000,
        "q13": q13 * 10000,
        "q21": dict(q21),
        "q22": dict(q22),
        "q23": dict(q23),
        "q31": dict(q31),
        "q32": dict(q32),
        "q33": dict(q33),
        "q34": dict(q34),
        "q41": dict(q41),
        "q42": dict(q42),
        "q43": dict(q43),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--data-dir", default="data/ssb/1", type=Path)
    parser.add_argument("--out-dir", default="src/test", type=Path)
    args = parser.parse_args()

    expected = compute(args.data_dir)
    args.out_dir.mkdir(parents=True, exist_ok=True)

    for query in ("q21", "q22", "q23", "q31", "q32", "q33", "q34", "q41", "q42", "q43"):
        write_csv(args.out_dir / f"{query}_ssb_expected.csv", expected[query])

    print(f"q11 = {expected['q11']}")
    print(f"q12 = {expected['q12']}")
    print(f"q13 = {expected['q13']}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
