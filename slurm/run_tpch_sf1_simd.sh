#!/bin/bash
#SBATCH --job-name=tpch-sf1-simd
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=8G
#SBATCH --time=00:30:00
#SBATCH --exclusive
#SBATCH --constraint=skylake
#SBATCH --output=results/tpch_sf1_simd_%j.out
#SBATCH --error=results/tpch_sf1_simd_%j.err

set -euo pipefail

PROJECT="${PROJECT:-${SLURM_SUBMIT_DIR:-$(pwd)}}"

BIN="$PROJECT/build/release/run_tpch"
DATA="$PROJECT/data/tpch/sf1/"

[[ -x "$BIN" ]] || { echo "missing binary: $BIN (build it first)"; exit 1; }
[[ -d "$DATA" ]] || { echo "missing data dir: $DATA"; exit 1; }

echo "=== Job info ==="
echo "Job ID:   ${SLURM_JOB_ID:-N/A}"
echo "Node:     ${SLURM_NODELIST:-N/A}"
echo "Start:    $(date -Iseconds)"
echo "Project:  $PROJECT"
echo
echo "=== CPU ==="
lscpu | grep -E "Model name|Socket|Core|Thread|MHz"
echo
echo "=== NUMA ==="
numactl --hardware
echo
echo "=== Kernel ==="
echo "randomize_va_space: $(cat /proc/sys/kernel/randomize_va_space)"
echo "================"

run_simd_config() {
   local label="$1" hash="$2" join="$3" sel="$4" proj="$5"
   echo
   echo "############################################"
   echo "### $label (SIMDhash=$hash SIMDjoin=$join SIMDsel=$sel SIMDproj=$proj)"
   echo "############################################"
   SIMDhash="$hash" SIMDjoin="$join" SIMDsel="$sel" SIMDproj="$proj" \
      numactl --cpunodebind=0 --membind=0 "$BIN" 5 "$DATA" 1
}

run_simd_config "scalar-baseline" 0 0 0 0
run_simd_config "hash-only"       1 0 0 0
run_simd_config "join-only"       0 1 0 0
run_simd_config "sel-only"        0 0 1 0
run_simd_config "proj-only"       0 0 0 1
run_simd_config "hash+join"       1 1 0 0
run_simd_config "all-simd-on"     1 1 1 1

echo
echo "End: $(date -Iseconds)"
