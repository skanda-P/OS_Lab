#!/usr/bin/env bash
# ============================================================================
# run_experiments.sh: Automates running Parts I - IV experiments on trace files.
# Usage: ./run_experiments.sh <trace_dir> <results_dir>
# Example 1: ./run_experiments.sh samples results_samples
# Example 2: ./run_experiments.sh benchmarks results_benchmarks
# ============================================================================

set -e

TRACE_DIR=${1:-samples}
RESULTS_DIR=${2:-results_samples}
OUTPUT_CSV="$RESULTS_DIR/results.csv"
HIST_DIR="$RESULTS_DIR/hist"

echo "======================================================================"
echo " Running Lab 4 Experiments on: $TRACE_DIR"
echo " Saving results and plots to  : $RESULTS_DIR"
echo "======================================================================"

echo "Building simulator..."
make -s

mkdir -p "$HIST_DIR"

echo "Initializing $OUTPUT_CSV..."
echo "trace,pt,tlb,frames_cap,accesses,distinct_pages,frames_used,cycles,pt_util,hash,tlb_hits,tlb_misses,tlb_hit_rate,faults,repl_faults,fault_rate,sim_seconds" > "$OUTPUT_CSV"

for TRACE in "$TRACE_DIR"/*.txt; do
    [ -f "$TRACE" ] || continue
    TRACE_NAME=$(basename "$TRACE")
    echo "Processing $TRACE_NAME..."

    for PT in single two; do
        # Part I & II: Unlimited frames, No TLB
        ./memsim --trace "$TRACE" --pt "$PT" --csv --hist "$HIST_DIR/${TRACE_NAME}.${PT}.csv" >> "$OUTPUT_CSV"

        # Part III: Unlimited frames, With TLB
        ./memsim --trace "$TRACE" --pt "$PT" --tlb --csv >> "$OUTPUT_CSV"

        # Part IV: Bounded memory (fractional sizes of 2^20)
        for FRAC in 0.5 0.25 0.125 0.0625; do
            ./memsim --trace "$TRACE" --pt "$PT" --tlb --frames-frac "$FRAC" --csv >> "$OUTPUT_CSV"
        done

        # Part IV: Bounded memory (small absolute frame limits)
        for FRAMES in 4096 1024 256 64 16; do
            ./memsim --trace "$TRACE" --pt "$PT" --tlb --frames "$FRAMES" --csv >> "$OUTPUT_CSV"
        done
    done
done

echo "Simulation runs finished! Results saved to $OUTPUT_CSV."

# Generate plots inside the results directory
echo "Generating plots in $RESULTS_DIR..."
if command -v python.exe &> /dev/null; then
    python.exe plot_results.py --results-dir "$RESULTS_DIR"
elif command -v python3 &> /dev/null; then
    python3 plot_results.py --results-dir "$RESULTS_DIR"
elif command -v python &> /dev/null; then
    python plot_results.py --results-dir "$RESULTS_DIR"
fi

echo "All results and plots saved successfully in $RESULTS_DIR."
