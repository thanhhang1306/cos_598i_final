#!/bin/bash
#SBATCH --job-name=stream-numa
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=32
#SBATCH --mem=8G
#SBATCH --time=00:30:00
#SBATCH --exclusive
#SBATCH --constraint=skylake
#SBATCH --output=results/stream_numa_%j.out
#SBATCH --error=results/stream_numa_%j.err

set -euo pipefail

PROJECT="${PROJECT:-${SLURM_SUBMIT_DIR:-$(pwd)}}"

BIN="$PROJECT/3rdparty/stream/stream"

[[ -x "$BIN" ]] || { echo "missing binary: $BIN (build with: gcc -O3 -fopenmp -march=skylake-avx512 -DSTREAM_ARRAY_SIZE=80000000 -DNTIMES=20 3rdparty/stream/stream.c -o 3rdparty/stream/stream)"; exit 1; }

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

echo
echo "############################################"
echo "### A. 16 threads, socket 0, local memory"
echo "############################################"
OMP_NUM_THREADS=16 numactl --cpunodebind=0 --membind=0 "$BIN"

echo
echo "############################################"
echo "### B. 16 threads, socket 0, remote memory (membind=1)"
echo "############################################"
OMP_NUM_THREADS=16 numactl --cpunodebind=0 --membind=1 "$BIN"

echo
echo "############################################"
echo "### C. 16 threads, 8+8 across sockets, interleaved memory"
echo "###    physcpubind=$SPLIT_CPUS"
echo "############################################"
OMP_NUM_THREADS=16 numactl --physcpubind="$SPLIT_CPUS" --interleave=all "$BIN"

echo
echo "############################################"
echo "### D. 32 threads, both sockets, interleaved memory"
echo "############################################"
OMP_NUM_THREADS=32 numactl --interleave=all "$BIN"

echo
echo "End: $(date -Iseconds)"
