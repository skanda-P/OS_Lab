#!/usr/bin/env bash
# ============================================================================
# run_experiments.sh: Automates running Lab 5 tasks across the 6 test images
# Usage: ./run_experiments.sh [iterations]
# Example: ./run_experiments.sh 100
# ============================================================================

set -e

ITERS=${1:-100}
IMG_DIR="images"
IMAGES=("1" "2" "3" "4" "5" "6")
OUTPUT_CSV="results.csv"

echo "======================================================================"
echo " Running Lab 5 Experiments across 6 images: ${IMAGES[*]}"
echo " Iterations per run: $ITERS"
echo " Saving results to: $OUTPUT_CSV"
echo "======================================================================"

echo "Building all binaries..."
make clean
make -j4 part1.out \
         part2_1_variant1.out part2_1_variant2.out \
         part2_2_variant1.out part2_2_variant2.out \
         part2_3.out part2_3_variant1.out part2_3_variant2.out \
         part3_1_variant1_A.out part3_1_variant1_B.out \
         part3_1_variant2_A.out part3_1_variant2_B.out \
         part3_2_variant1_A.out part3_2_variant1_B.out \
         part3_2_variant2_A.out part3_2_variant2_B.out

echo "image,width_x_height,total_pixels,part,variant,compute_ms,total_ms,speedup" > "$OUTPUT_CSV"

for IMG in "${IMAGES[@]}"; do
    IMG_PATH="$IMG_DIR/$IMG.ppm"
    [ -f "$IMG_PATH" ] || continue

    DIM=$(head -n 3 "$IMG_PATH" | tail -n 1)
    echo "----------------------------------------------------------------------"
    echo "Processing Image $IMG.ppm (Dimensions: $DIM)..."
    echo "----------------------------------------------------------------------"

    # Part 1: Baseline (1 iteration, scaled to ITERS for fair speedup comparison)
    echo "  Running Part 1 (Baseline)..."
    P1_OUT=$(./part1.out "$IMG_PATH" output_part1.ppm)
    P1_COMPUTE=$(echo "$P1_OUT" | awk '{print ($3 + $4 + $5) * '$ITERS'}')
    P1_TOTAL=$(echo "$P1_OUT" | awk '{print ($2 + $3 + $4 + $5 + $6) * '$ITERS'}')
    echo "$IMG,$DIM,Part1,Baseline,$P1_COMPUTE,$P1_TOTAL,1.00" >> "$OUTPUT_CSV"

    # Part 2.1: Variant 1 & Variant 2
    echo "  Running Part 2.1 (Threads + Atomic)..."
    P21_V1=$(./part2_1_variant1.out "$IMG_PATH" output_part2_1_variant1.ppm "$ITERS")
    C_MS=$(echo "$P21_V1" | grep -o 'Compute Time: [0-9.]*' | awk '{print $3}')
    T_MS=$(echo "$P21_V1" | grep -o 'Total Time: [0-9.]*' | awk '{print $3}')
    SPD=$(awk "BEGIN {printf \"%.2f\", $P1_COMPUTE / $C_MS}")
    echo "$IMG,$DIM,Part2_1,Variant1,$C_MS,$T_MS,$SPD" >> "$OUTPUT_CSV"

    P21_V2=$(./part2_1_variant2.out "$IMG_PATH" output_part2_1_variant2.ppm "$ITERS")
    C_MS=$(echo "$P21_V2" | grep -o 'Compute Time: [0-9.]*' | awk '{print $3}')
    T_MS=$(echo "$P21_V2" | grep -o 'Total Time: [0-9.]*' | awk '{print $3}')
    SPD=$(awk "BEGIN {printf \"%.2f\", $P1_COMPUTE / $C_MS}")
    echo "$IMG,$DIM,Part2_1,Variant2,$C_MS,$T_MS,$SPD" >> "$OUTPUT_CSV"

    # Part 2.2: Variant 1 & Variant 2
    echo "  Running Part 2.2 (Processes + Pipes)..."
    P22_V1=$(./part2_2_variant1.out "$IMG_PATH" output_part2_2_variant1.ppm "$ITERS")
    C_MS=$(echo "$P22_V1" | grep -o 'Compute Time: [0-9.]*' | awk '{print $3}')
    T_MS=$(echo "$P22_V1" | grep -o 'Total Time: [0-9.]*' | awk '{print $3}')
    SPD=$(awk "BEGIN {printf \"%.2f\", $P1_COMPUTE / $C_MS}")
    echo "$IMG,$DIM,Part2_2,Variant1,$C_MS,$T_MS,$SPD" >> "$OUTPUT_CSV"

    P22_V2=$(./part2_2_variant2.out "$IMG_PATH" output_part2_2_variant2.ppm "$ITERS")
    C_MS=$(echo "$P22_V2" | grep -o 'Compute Time: [0-9.]*' | awk '{print $3}')
    T_MS=$(echo "$P22_V2" | grep -o 'Total Time: [0-9.]*' | awk '{print $3}')
    SPD=$(awk "BEGIN {printf \"%.2f\", $P1_COMPUTE / $C_MS}")
    echo "$IMG,$DIM,Part2_2,Variant2,$C_MS,$T_MS,$SPD" >> "$OUTPUT_CSV"

    # Part 2.3: Variant 1 & Variant 2
    echo "  Running Part 2.3 (Processes + SHM + Semaphores)..."
    P23_V1=$(./part2_3_variant1.out "$IMG_PATH" output_part2_3_variant1.ppm "$ITERS")
    C_MS=$(echo "$P23_V1" | grep -o 'Compute Time: [0-9.]*' | awk '{print $3}')
    T_MS=$(echo "$P23_V1" | grep -o 'Total Time: [0-9.]*' | awk '{print $3}')
    SPD=$(awk "BEGIN {printf \"%.2f\", $P1_COMPUTE / $C_MS}")
    echo "$IMG,$DIM,Part2_3,Variant1,$C_MS,$T_MS,$SPD" >> "$OUTPUT_CSV"

    P23_V2=$(./part2_3_variant2.out "$IMG_PATH" output_part2_3_variant2.ppm "$ITERS")
    C_MS=$(echo "$P23_V2" | grep -o 'Compute Time: [0-9.]*' | awk '{print $3}')
    T_MS=$(echo "$P23_V2" | grep -o 'Total Time: [0-9.]*' | awk '{print $3}')
    SPD=$(awk "BEGIN {printf \"%.2f\", $P1_COMPUTE / $C_MS}")
    echo "$IMG,$DIM,Part2_3,Variant2,$C_MS,$T_MS,$SPD" >> "$OUTPUT_CSV"

    # Part 3.1: Variant 1 & Variant 2
    echo "  Running Part 3.1 (Sockets: A=S1+S2, B=S3)..."
    ./part3_1_variant1_A.out 12345 "$IMG_PATH" "$ITERS" > /dev/null &
    SRV_PID=$!
    sleep 0.05
    P31_V1=$(./part3_1_variant1_B.out 127.0.0.1 12345 output_part3_1_variant1.ppm)
    wait $SRV_PID
    C_MS=$(echo "$P31_V1" | grep -o 'Compute Time: [0-9.]*' | awk '{print $3}')
    T_MS=$(echo "$P31_V1" | grep -o 'Total Time: [0-9.]*' | awk '{print $3}')
    SPD=$(awk "BEGIN {printf \"%.2f\", $P1_COMPUTE / $C_MS}")
    echo "$IMG,$DIM,Part3_1,Variant1,$C_MS,$T_MS,$SPD" >> "$OUTPUT_CSV"

    ./part3_1_variant2_A.out 12345 "$IMG_PATH" "$ITERS" > /dev/null &
    SRV_PID=$!
    sleep 0.05
    P31_V2=$(./part3_1_variant2_B.out 127.0.0.1 12345 output_part3_1_variant2.ppm)
    wait $SRV_PID
    C_MS=$(echo "$P31_V2" | grep -o 'Compute Time: [0-9.]*' | awk '{print $3}')
    T_MS=$(echo "$P31_V2" | grep -o 'Total Time: [0-9.]*' | awk '{print $3}')
    SPD=$(awk "BEGIN {printf \"%.2f\", $P1_COMPUTE / $C_MS}")
    echo "$IMG,$DIM,Part3_1,Variant2,$C_MS,$T_MS,$SPD" >> "$OUTPUT_CSV"

    # Part 3.2: Variant 1 & Variant 2
    echo "  Running Part 3.2 (Sockets: A=S1, B=S2+S3)..."
    ./part3_2_variant1_A.out 12345 "$IMG_PATH" "$ITERS" > /dev/null &
    SRV_PID=$!
    sleep 0.05
    P32_V1=$(./part3_2_variant1_B.out 127.0.0.1 12345 output_part3_2_variant1.ppm)
    wait $SRV_PID
    C_MS=$(echo "$P32_V1" | grep -o 'Compute Time: [0-9.]*' | awk '{print $3}')
    T_MS=$(echo "$P32_V1" | grep -o 'Total Time: [0-9.]*' | awk '{print $3}')
    SPD=$(awk "BEGIN {printf \"%.2f\", $P1_COMPUTE / $C_MS}")
    echo "$IMG,$DIM,Part3_2,Variant1,$C_MS,$T_MS,$SPD" >> "$OUTPUT_CSV"

    ./part3_2_variant2_A.out 12345 "$IMG_PATH" "$ITERS" > /dev/null &
    SRV_PID=$!
    sleep 0.05
    P32_V2=$(./part3_2_variant2_B.out 127.0.0.1 12345 output_part3_2_variant2.ppm)
    wait $SRV_PID
    C_MS=$(echo "$P32_V2" | grep -o 'Compute Time: [0-9.]*' | awk '{print $3}')
    T_MS=$(echo "$P32_V2" | grep -o 'Total Time: [0-9.]*' | awk '{print $3}')
    SPD=$(awk "BEGIN {printf \"%.2f\", $P1_COMPUTE / $C_MS}")
    echo "$IMG,$DIM,Part3_2,Variant2,$C_MS,$T_MS,$SPD" >> "$OUTPUT_CSV"

done

echo "======================================================================"
echo " Experiments completed! Results saved to $OUTPUT_CSV"
echo "======================================================================"

