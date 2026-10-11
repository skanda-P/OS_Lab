#!/usr/bin/env bash
# ============================================================================
# make_samples.sh: Extracts representative sample traces from the real benchmark files
# ============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BENCHMARK_DIR="$SCRIPT_DIR/benchmarks"
SAMPLE_DIR="$SCRIPT_DIR/samples"

# Number of requests to extract per benchmark (default: 100,000 requests)
SAMPLE_SIZE=${1:-100000}

if [ ! -d "$BENCHMARK_DIR" ]; then
    echo "Error: Benchmark directory not found at $BENCHMARK_DIR"
    exit 1
fi

mkdir -p "$SAMPLE_DIR"
echo "Extracting $SAMPLE_SIZE lines from each benchmark file into $SAMPLE_DIR..."

COUNT=0
# Loop over each real benchmark trace
for BENCH in "$BENCHMARK_DIR"/*_memoryTrace.txt; do
    [ -f "$BENCH" ] || continue
    FILENAME=$(basename "$BENCH")
    PREFIX="${FILENAME%_memoryTrace.txt}"
    OUTFILE="$SAMPLE_DIR/${PREFIX}_sample.txt"

    echo "  -> Sampling $FILENAME -> ${PREFIX}_sample.txt"
    # Take header line + SAMPLE_SIZE request lines
    head -n $((SAMPLE_SIZE + 1)) "$BENCH" > "$OUTFILE"
    COUNT=$((COUNT + 1))
done

echo "Successfully extracted $COUNT benchmark samples into $SAMPLE_DIR."
