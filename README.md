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

### 2. Build

```bash
mkdir -p build/release && cd build/release
cmake -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_CXX_FLAGS_RELEASE="-O3 -DNDEBUG -march=skylake-avx512 -mtune=skylake-avx512" \
      -DCMAKE_C_FLAGS_RELEASE="-O3 -DNDEBUG -march=skylake-avx512" ../..
make -j8
```

The explicit `-march=skylake-avx512` targets Adroit's Skylake compute nodes regardless of which login node compiled the binary. The default `-march=native` would inherit the login node's CPU (currently Cascade Lake-SP), which produces a binary that may contain instructions not present on Skylake.

This produces:
- `run_tpch` -- TPC-H benchmark runner
- `run_ssb` -- Star Schema Benchmark runner
- `test_all` -- unit tests
- `run_prim` -- primitive micro-benchmarks

### 3. Verify build (data-independent tests)

```bash
./test_all --gtest_filter="GTEQ*:Scatter*:Partition*:Join*:HashGroupT*:HashGroupSmallBuf*:Hashtable*:BlockRelation*:Mmap*:PartitionedDeque*:Stack*"
```

All 19 tests should pass. TPC-H and SSB tests will fail at this point (no data yet).

### 4. Populate perf event cache (recommended)

`run_tpch` and `run_ssb` use the vendored `jevents` library to read CPU performance counters via Intel's per-CPU event catalog. Without it, the benchmark still produces query timings, but the `Bandwidth`, `all_rd`, `stores`, `loads`, and `mem_stall` columns will be zero, and you'll see warnings like `Cannot parse qualifier mem_load_retired.l3_miss` on every run.

```bash
python3 3rdparty/jevents/event_download.py
```

One-time download into `~/.cache/pmu-events/`. The bundled downloader has been ported to Python 3 and updated for Intel's current perfmon repo and JSON format -- the upstream copy targets a defunct host and won't work as-is.

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

For SF=30:

```bash
./build/dbgen -b dists.dss -v -s 30
mkdir -p ../../data/ssb/30/cached
cp *.tbl ../../data/ssb/30/
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

### Slurm (Adroit)

Use the batch scripts in `slurm/` to run benchmarks on a compute node. All scripts request an exclusive node and dump CPU/NUMA/kernel provenance to the job log. Single-threaded scripts additionally pin to socket 0 with local memory via `numactl --cpunodebind=0 --membind=0` (approximating the paper's single-socket setup).

```bash
# From the project root, one-time:
mkdir -p results

# Submit any of the scripts below:
sbatch slurm/<script>.sh

# Check status:
squeue --me

# Logs land in results/<job-name>_<jobid>.{out,err}
```

Submit from the repo root -- the scripts' `--output=results/...` directives are interpreted relative to the submission directory.

#### Available scripts

| Script               | Reproduces                          | Data needed       |
|----------------------|-------------------------------------|-------------------|
| `run_tpch_sf1.sh`    | TPC-H SF1, single-threaded, 5 reps  | `data/tpch/sf1/`  |
| `run_tpch_sf1_vec.sh`| TPC-H SF1, vectorSize sweep (powers of 2, 8..32768), 1 thread, 5 reps each | `data/tpch/sf1/` |
| `run_tpch_sf1_simd.sh`| TPC-H SF1, SIMD primitive ablation (7 configs), 1 thread, 5 reps each | `data/tpch/sf1/` |
| `run_tpch_sf10.sh`   | TPC-H SF10, single-threaded, 5 reps | `data/tpch/sf10/` |
| `run_tpch_sf10_mt.sh`| TPC-H SF10, threads={1,16,32}, 5 reps each | `data/tpch/sf10/` |
| `run_tpch_sf10_simd.sh`| TPC-H SF10, SIMD primitive ablation (7 configs), 1 thread, 5 reps each | `data/tpch/sf10/` |
| `run_tpch_sf10_numa.sh`| TPC-H SF10, NUMA topology (16t/socket-0, 16t/8+8 split, 32t/full), 5 reps each | `data/tpch/sf10/` |
| `run_ssb_sf1.sh`     | SSB SF1, single-threaded, 5 reps    | `data/ssb/1/`     |
| `run_ssb_sf30.sh`    | SSB SF30, single-threaded, 5 reps   | `data/ssb/30/`    |
| `run_ssb_sf30_mt.sh` | SSB SF30, threads={1,16,32}, 5 reps each | `data/ssb/30/`    |

#### Pinning to a specific node

For final measurement runs, you can pin to one physical node so all results come from the same silicon:

```bash
sbatch --nodelist=adroit-16 slurm/run_ssb_sf1.sh
```

## Notes

### Performance counter overhead

Once the jevents cache is populated (Setup step 4), all perf-counter columns are filled. Actually collecting these counters adds measurable overhead -- per-query timings will be noticeably higher than they were when the events silently failed to resolve. This is expected and not a regression.
