# Results

`.out` files from slurm runs. Filenames follow the `--output=` directives in `slurm/*.sh`: `<job-name>_<slurm-job-id>.out`.

## Build provenance

Runs in this directory were produced by the binary at `build/release/` compiled with `-march=skylake-avx512 -mtune=skylake-avx512` (top-level README, Setup step 2).

## Coverage

| Filename pattern | Source script |
|---|---|
| `tpch_sf1_<jobid>.out` | `slurm/run_tpch_sf1.sh` |
| `tpch_sf1_vec_<jobid>.out` | `slurm/run_tpch_sf1_vec.sh` |
| `tpch_sf1_simd_<jobid>.out` | `slurm/run_tpch_sf1_simd.sh` |
| `tpch_sf10_<jobid>.out` | `slurm/run_tpch_sf10.sh` |
| `tpch_sf10_mt_<jobid>.out` | `slurm/run_tpch_sf10_mt.sh` |
| `tpch_sf10_simd_<jobid>.out` | `slurm/run_tpch_sf10_simd.sh` |
| `tpch_sf10_numa_<jobid>.out` | `slurm/run_tpch_sf10_numa.sh` |
| `ssb_sf1_<jobid>.out` | `slurm/run_ssb_sf1.sh` |
| `ssb_sf30_<jobid>.out` | `slurm/run_ssb_sf30.sh` |
| `ssb_sf30_mt_<jobid>.out` | `slurm/run_ssb_sf30_mt.sh` |
| `stream_numa_<jobid>.out` | `slurm/run_stream_numa.sh` |
