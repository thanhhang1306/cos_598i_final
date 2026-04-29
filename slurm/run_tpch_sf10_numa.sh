#!/bin/bash
#SBATCH --job-name=tpch-sf10-numa
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=32
#SBATCH --mem=64G
#SBATCH --time=02:30:00
#SBATCH --exclusive
#SBATCH --constraint=skylake
#SBATCH --output=results/tpch_sf10_numa_%j.out
#SBATCH --error=results/tpch_sf10_numa_%j.err

set -euo pipefail

# Disable WorkerGroup's hard-coded thread-i-to-CPU-i pinning so the
# numactl mask below actually controls worker placement.
export unpinWorkers=1

PROJECT="${PROJECT:-${SLURM_SUBMIT_DIR:-$(pwd)}}"

BIN="$PROJECT/build/release/run_tpch"
DATA="$PROJECT/data/tpch/sf10/"

[[ -x "$BIN" ]] || { echo "missing binary: $BIN (build it first)"; exit 1; }
[[ -d "$DATA" ]] || { echo "missing data dir: $DATA (generate SF=10 data -- see README Data Generation section)"; exit 1; }

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

read -ra socket0 < <(numactl --hardware | sed -n 's/^node 0 cpus:[[:space:]]*//p')
read -ra socket1 < <(numactl --hardware | sed -n 's/^node 1 cpus:[[:space:]]*//p')
S0_HALF=$(IFS=,; echo "${socket0[*]:0:8}")
S1_HALF=$(IFS=,; echo "${socket1[*]:0:8}")
SPLIT_CPUS="$S0_HALF,$S1_HALF"

echo
echo "Derived CPU bindings:"
echo "  socket 0 (first 8): $S0_HALF"
echo "  socket 1 (first 8): $S1_HALF"
echo "  16-thread split:    $SPLIT_CPUS"

run_with_monitor() {
   "$@" &
   local cmd_pid=$!
   (
      sleep 5
      if kill -0 "$cmd_pid" 2>/dev/null; then
         echo
         echo "--- monitor snapshot (pid=$cmd_pid) ---"
         echo "thread -> CPU placement (tid, psr=running CPU):"
         ps -L -p "$cmd_pid" -o tid,psr,comm 2>/dev/null || true
         echo
         echo "memory placement per NUMA node:"
         numastat -p "$cmd_pid" 2>/dev/null || true
         echo "--- end snapshot ---"
         echo
      fi
   ) &
   local sampler_pid=$!
   wait "$cmd_pid"
   wait "$sampler_pid" 2>/dev/null || true
}

echo
echo "############################################"
echo "### A. 16 threads, socket 0 only, local memory"
echo "############################################"
run_with_monitor numactl --cpunodebind=0 --membind=0 "$BIN" 5 "$DATA" 16

echo
echo "############################################"
echo "### B. 16 threads, 8+8 across sockets, interleaved memory"
echo "###    physcpubind=$SPLIT_CPUS"
echo "############################################"
run_with_monitor numactl --physcpubind="$SPLIT_CPUS" --interleave=all "$BIN" 5 "$DATA" 16

echo
echo "############################################"
echo "### C. 32 threads, both sockets, interleaved memory"
echo "############################################"
run_with_monitor numactl --interleave=all "$BIN" 5 "$DATA" 32

echo
echo "End: $(date -Iseconds)"
