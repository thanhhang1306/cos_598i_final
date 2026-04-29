# Database Engines: Vectorization vs. Compilation

Reproduction and extension of [Kersten et al., PVLDB 2018](https://doi.org/10.14778/3275366.3275370) on Princeton's Adroit cluster.

Based on: https://github.com/TimoKersten/db-engine-paradigms

> **Where to start in the code:** `src/benchmarks/tpch/queries/` shows how query processing is implemented for both Typer (compiling) and Tectorwise (vectorizing).

## Prerequisites

Tested on Adroit (Red Hat Linux, Intel Skylake nodes):
- GCC 11.5.0 (system default, no module load needed)
- CMake 3.26.5 (system default)
- TBB (system-installed at `/usr/include/tbb`)

## Setup

### 1. Clone the repository

```bash
cd /scratch/network/<YourNetID>
git clone git@github.com:thanhhang1306/cos_598i_final.git
cd cos_598i_final
```

### 2. Patch CMakeLists.txt

Two changes from the original:

a) Bump C++ standard to 17 and CMake minimum to 3.10 (compatibility with GCC 11 and modern CMake):

```
cmake_minimum_required(VERSION 3.10 FATAL_ERROR)
set(CMAKE_CXX_STANDARD 17)
```

Also update the flag in `CMAKE_CXX_FLAGS` from `-std=c++14` to `-std=c++17`.

b) Suppress warnings-as-errors for the vendored Google Test (old version triggers `-Wdeprecated-copy` and `-Wmaybe-uninitialized` on GCC 11). Add immediately after the `add_subdirectory` for googletest:

```cmake
add_subdirectory("${CMAKE_BINARY_DIR}/googletest-src"
                 "${CMAKE_BINARY_DIR}/googletest-build")
target_compile_options(gtest PRIVATE -Wno-error)
target_compile_options(gtest_main PRIVATE -Wno-error)
target_compile_options(gmock PRIVATE -Wno-error)
target_compile_options(gmock_main PRIVATE -Wno-error)
```

No other changes to project source code are required.

### 3. Build

```bash
mkdir -p build/release && cd build/release
cmake -DCMAKE_BUILD_TYPE=Release ../..
make -j8
```

This produces:
- `run_tpch` -- TPC-H benchmark runner
- `run_ssb` -- Star Schema Benchmark runner
- `test_all` -- unit tests
- `run_prim` -- primitive micro-benchmarks

### 4. Verify build (data-independent tests)

```bash
./test_all --gtest_filter="GTEQ*:Scatter*:Partition*:Join*:HashGroupT*:HashGroupSmallBuf*:Hashtable*:BlockRelation*:Mmap*:PartitionedDeque*:Stack*"
```

All 19 tests should pass. TPC-H and SSB tests will fail at this point (no data yet).

## Data Generation

### TPC-H

```bash
cd /scratch/network/<YourNetID>/db-engine-paradigms

# Clone dbgen into 3rdparty 
git clone https://github.com/electrum/tpch-dbgen.git 3rdparty/tpch-dbgen
cd 3rdparty/tpch-dbgen
make -j4

# Generate SF=1 (~1 GB, for single-threaded experiments)
./dbgen -vf -s 1

# Place data where the project expects it
mkdir -p ../../data/tpch/sf1/cached
cp *.tbl ../../data/tpch/sf1/
```

For SF=10 (cache-miss scaling experiments):

```bash
./dbgen -vf -s 10
mkdir -p ../../data/tpch/sf10/cached
cp *.tbl ../../data/tpch/sf10/
```

### SSB

```bash
cd /scratch/network/<YourNetID>/db-engine-paradigms

# Clone ssb-dbgen into 3rdparty
git clone https://github.com/eyalroz/ssb-dbgen.git 3rdparty/ssb-dbgen
cd 3rdparty/ssb-dbgen
cmake -B build && cmake --build build

# Generate SF=1 (~600 MB)
./build/dbgen -b dists.dss -v -s 1

# Place data where the project expects it
mkdir -p ../../data/ssb/1/cached
cp *.tbl ../../data/ssb/1/
```

### Verify with full test suite

```bash
cd /scratch/network/<YourNetID>/db-engine-paradigms/build/release
./test_all
```

TPC-H tests (Q1, Q3, Q5, Q6, Q9, Q18) should all pass. SSB tests will show value mismatches -- this is expected. The hardcoded expected values in `src/test/ssb_expected.cpp` were generated with the original authors' specific ssb-dbgen version. The engine code is correct; only the reference data differs.

## Running the Benchmarks

### TPC-H

```bash
# From the project root:
cd build/release

# Usage: run_tpch <repetitions> <path-to-tbl-dir> [nrThreads]
./run_tpch 1 ../../data/tpch/sf1/

# With explicit thread count (e.g. 4 threads):
./run_tpch 1 ../../data/tpch/sf1/ 4
```

### SSB

```bash
# From the project root:
cd build/release

# Usage: run_ssb <repetitions> <path-to-tbl-dir> [nrThreads]
./run_ssb 1 ../../data/ssb/1/

# With explicit thread count (e.g. 4 threads):
./run_ssb 1 ../../data/ssb/1/ 4
```

## Notes

### Performance counter event catalog (jevents)

`run_tpch` and `run_ssb` use the vendored `jevents` library to read CPU performance counters. On first run you'll see warnings like:

```
Cannot parse qualifier mem_load_retired.l3_miss
Error resolving perf event mem_load_retired.l3_miss
```

These are non-fatal -- the benchmark still produces timings, but several output columns (`Bandwidth`, `all_rd`, `stores`, `loads`, `mem_stall`) will be zero. To populate them, download Intel's per-CPU event catalog into `~/.cache/pmu-events/`:

```bash
python3 3rdparty/jevents/event_download.py
```

The bundled `event_download.py` shipped by the upstream project is **Python 2** and points at the now-defunct `download.01.org/perfmon`. The script in this repo has been modernized:

- Python 3 syntax (`print()`, `urllib.request`, etc.)
- New source URL: `https://raw.githubusercontent.com/intel/perfmon/main`
- Tolerates the new 7-column `mapfile.csv`
- Handles stepping-range entries like `GenuineIntel-6-55-[56789ABCDEF]` (needed to disambiguate Skylake-SP from Cascade Lake-SP, which share model `0x55`) while keeping the on-disk filename in the no-stepping form (`GenuineIntel-6-55-core.json`) that jevents' C resolver expects
- Unwraps the post-2024 Intel JSON wrapper (`{"Header": ..., "Events": [...]}`) into the bare event array the bundled jevents parser requires
- Treats the optional `readme.txt` 404 as non-fatal

Once the cache is populated, all perf-counter columns are filled. Note that actually collecting these counters adds measurable overhead -- per-query timings will be noticeably higher than they were when the events silently failed to resolve. This is expected and not a regression.
