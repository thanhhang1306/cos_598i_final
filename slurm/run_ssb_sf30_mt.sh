#!/bin/bash
#SBATCH --job-name=ssb-sf30-mt
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=32
#SBATCH --mem=64G
#SBATCH --time=03:00:00
#SBATCH --exclusive
#SBATCH --constraint=skylake
#SBATCH --output=results/ssb_sf30_mt_%j.out
#SBATCH --error=results/ssb_sf30_mt_%j.err

set -euo pipefail

# Disable WorkerGroup's hard-coded thread-i-to-CPU-i pinning so the
# numactl mask below actually controls worker placement.
export unpinWorkers=1

PROJECT="${PROJECT:-${SLURM_SUBMIT_DIR:-$(pwd)}}"

BIN="$PROJECT/build/release/run_ssb"
DATA="$PROJECT/data/ssb/30/"

[[ -x "$BIN" ]] || { echo "missing binary: $BIN (build it first)"; exit 1; }
[[ -d "$DATA" ]] || { echo "missing data dir: $DATA (generate SF=30 data -- see README Data Generation section)"; exit 1; }

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

# Sweep: 1 (baseline), 16 (one socket = 16 cores), 32 (both sockets).
# Pin to socket 0 for runs that fit; interleave memory when spanning sockets.
for t in 1 16 32; do
   echo
   echo "############################################"
   echo "### nrThreads=$t"
   echo "############################################"
   if (( t <= 16 )); then
      numactl --cpunodebind=0 --membind=0 "$BIN" 5 "$DATA" "$t"
   else
      numactl --interleave=all "$BIN" 5 "$DATA" "$t"
   fi
done

echo
echo "End: $(date -Iseconds)"
