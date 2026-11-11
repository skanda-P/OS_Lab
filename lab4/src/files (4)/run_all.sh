#!/usr/bin/env bash
# Usage: ./run_all.sh <trace_dir> [out.csv]
# Runs every experiment needed for Parts I-IV on every trace in <trace_dir>.
set -e
DIR=${1:?usage: ./run_all.sh <trace_dir> [out.csv]}
OUT=${2:-results.csv}
make -s
echo "trace,pt,tlb,frames_cap,accesses,distinct_pages,frames_used,cycles,pt_util,hash,tlb_hits,tlb_misses,tlb_hit_rate,faults,repl_faults,fault_rate,sim_seconds" > "$OUT"
mkdir -p hist
for T in "$DIR"/*; do
  B=$(basename "$T")
  for PT in single two; do
    # Parts I & II (no TLB, unlimited frames); hot-page CDF saved once
    ./memsim --trace "$T" --pt $PT --csv --hist "hist/${B}.${PT}.csv" >> "$OUT"
    # Part III (TLB, unlimited frames)
    ./memsim --trace "$T" --pt $PT --tlb --csv >> "$OUT"
    # Part IV (TLB on, shrinking physical memory)
    for FR in 0.5 0.25 0.125 0.0625 0.03125 0.015625; do
      ./memsim --trace "$T" --pt $PT --tlb --frames-frac $FR --csv >> "$OUT"
    done
    # Part IV, absolute small sizes (benchmarks may touch fewer than 2^19 pages)
    for FR in 4096 1024 256 64; do
      ./memsim --trace "$T" --pt $PT --tlb --frames $FR --csv >> "$OUT"
    done
  done
done
echo "wrote $OUT"
