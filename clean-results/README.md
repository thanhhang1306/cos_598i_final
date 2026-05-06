# Results

Curated `.out` files from Slurm runs are split by experiment family:

- `baseline/` contains the baseline reproduction logs.
- `extension/` is reserved for extension-query logs.

Filenames follow the `--output=` directives in `slurm/*.sh`: `<job-name>_<slurm-job-id>.out`.

## Build provenance

Runs in this directory were produced by the binary at `build/release/` compiled with `-march=skylake-avx512 -mtune=skylake-avx512` (top-level README, Setup step 2).

## Coverage

| Filename pattern | Source script |
|---|---|
| `baseline/tpch_sf1_<jobid>.out` | `slurm/run_tpch_sf1.sh` |
| `baseline/tpch_sf1_vec_<jobid>.out` | `slurm/run_tpch_sf1_vec.sh` |
| `baseline/tpch_sf1_simd_<jobid>.out` | `slurm/run_tpch_sf1_simd.sh` |
| `baseline/tpch_sf3_<jobid>.out` | `slurm/run_tpch_sf3.sh` |
| `baseline/tpch_sf10_<jobid>.out` | `slurm/run_tpch_sf10.sh` |
| `baseline/tpch_sf10_mt_<jobid>.out` | `slurm/run_tpch_sf10_mt.sh` |
| `baseline/tpch_sf10_simd_<jobid>.out` | `slurm/run_tpch_sf10_simd.sh` |
| `baseline/tpch_sf10_numa_<jobid>.out` | `slurm/run_tpch_sf10_numa.sh` |
| `baseline/tpch_sf30_<jobid>.out` | `slurm/run_tpch_sf30.sh` |
| `baseline/tpch_sf30_mt_<jobid>.out` | `slurm/run_tpch_sf30_mt.sh` |
| `baseline/ssb_sf1_<jobid>.out` | `slurm/run_ssb_sf1.sh` |
| `baseline/ssb_sf30_<jobid>.out` | `slurm/run_ssb_sf30.sh` |
| `baseline/ssb_sf30_mt_<jobid>.out` | `slurm/run_ssb_sf30_mt.sh` |
| `baseline/ssb_sf30_vec_<jobid>.out` | `slurm/run_ssb_sf30_vec.sh` |
| `baseline/stream_numa_<jobid>.out` | `slurm/run_stream_numa.sh` |
| `baseline/apple_results.out` | Apple Silicon comparison data |
