#!/bin/bash
#SBATCH --job-name=ssb-sf30-vec
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=64G
#SBATCH --time=12:00:00
#SBATCH --exclusive
#SBATCH --constraint=skylake
#SBATCH --output=results/ssb_sf30_vec_%j.out
#SBATCH --error=results/ssb_sf30_vec_%j.err

set -euo pipefail

PROJECT="${PROJECT:-${SLURM_SUBMIT_DIR:-$(pwd)}}"

BIN="$PROJECT/build/release/run_ssb"
DATA="$PROJECT/data/ssb/30/"

[[ -x "$BIN" ]] || { echo "missing binary: $BIN (build it first)"; exit 1; }
[[ -d "$DATA" ]] || { echo "missing data dir: $DATA (generate SF=30 SSB data via 3rdparty/ssb-dbgen)"; exit 1; }

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

for v in 1 8 16 32 64 128 256 512 1024 2048 4096 8192 16384 32768 65536 131072 262144 524288 1048576 8388608; do
   echo
   echo "############################################"
   echo "### vectorSize=$v"
   echo "############################################"
   vectorSize="$v" numactl --cpunodebind=0 --membind=0 "$BIN" 5 "$DATA" 1
done

echo
echo "End: $(date -Iseconds)"
