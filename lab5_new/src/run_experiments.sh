#!/usr/bin/env bash
# ============================================================================
# run_experiments.sh - Lab 5 benchmark driver (single machine / loopback).
#
#   ./run_experiments.sh                       # defaults below
#   ITERS=1000 REPS=3 IMAGES="1 2 3" ./run_experiments.sh
#
#   ITERS   iterations (assignment: 1000; must be a multiple of 100 for variant 2)
#   REPS    repetitions per configuration; the MEAN is reported
#   IMAGES  image numbers in images/N.ppm   (large images take a long time:
#           baseline + 14 parallel runs per repetition, each ~ITERS full passes)
#
# What is measured
#   * baseline  : baseline_bench.out - single thread, image read once, no
#                 allocation in the loop, same per-pixel kernels and same amount
#                 of work as the parallel programs (see part1/baseline_bench.cpp).
#   * speedup   : mean baseline compute time of the SAME variant / mean parallel
#                 compute time.  Ideal limits: 3 for Part II (3 stages),
#                 2 for Part III (2 computers) - rows above the limit are flagged.
#   * output_ok : the final image's FNV hash equals the baseline's (proves the
#                 parallel result is bit-identical to the single-thread result).
#   * Part III  : here it runs over 127.0.0.1 (one machine). For the real
#                 two-computer experiment run `make part3_X_variantY_A` on one
#                 machine and `make part3_X_variantY_B IP=<A's IP>` on the other
#                 and compare with the loopback numbers.
# ============================================================================
set -u
cd "$(dirname "$0")"

ITERS=${ITERS:-1000}
REPS=${REPS:-3}
IMAGES=${IMAGES:-"1 2 3"}
IMG_DIR=${IMG_DIR:-images}
OUT_CSV=${OUT_CSV:-results.csv}
PORT=${PORT:-12345}
RUN_PART3=${RUN_PART3:-0}   # 1 = also run Part III over loopback (default: skipped; real two-computer runs are done separately)

echo "Building..."
make clean >/dev/null
TARGETS="baseline_bench.out part2_1_variant1.out part2_1_variant2.out part2_2_variant1.out part2_2_variant2.out part2_3_variant1.out part2_3_variant2.out"
if [ "$RUN_PART3" = 1 ]; then
  TARGETS="$TARGETS part3_1_variant1_A.out part3_1_variant1_B.out part3_1_variant2_A.out part3_1_variant2_B.out part3_2_variant1_A.out part3_2_variant1_B.out part3_2_variant2_A.out part3_2_variant2_B.out"
fi
make -j4 $TARGETS || { echo "build failed"; exit 1; }

echo "image,width,height,pixels,iterations,reps,part,variant,baseline_ms,compute_ms,total_ms,speedup,ideal_limit,exceeds_limit,output_ok" > "$OUT_CSV"

field() { grep -o "$1: [0-9.]*" | head -1 | awk '{print $NF}'; }
mean()  { awk '{s+=$1} END{printf "%.3f", s/NR}'; }

# run_cfg <part> <variant> <limit> <baseline_ms> <baseline_hash> <command producing report on stdout...>
run_cfg() {
  local part=$1 variant=$2 limit=$3 base_ms=$4 base_hash=$5; shift 5
  local comp="" tot="" ok=yes out c t h
  for ((r=0; r<REPS; r++)); do
    out=$("$@" 2>&1) || { echo "  !! $part variant $variant FAILED:"; echo "$out" | tail -3; ok=FAILED; continue; }
    c=$(echo "$out" | field "Compute Time"); t=$(echo "$out" | field "Total Time")
    h=$(echo "$out" | grep -o "Output hash: [0-9a-f]*" | tail -1 | awk '{print $3}')
    [ "$h" = "$base_hash" ] || ok=MISMATCH
    comp+="$c"$'\n'; tot+="$t"$'\n'
  done
  if [ -z "$comp" ]; then echo "$IMG,$W,$H,$PIX,$ITERS,$REPS,$part,$variant,$base_ms,NA,NA,NA,$limit,NA,$ok" >> "$OUT_CSV"; return; fi
  local cm tm sp ex
  cm=$(echo -n "$comp" | mean); tm=$(echo -n "$tot" | mean)
  sp=$(awk "BEGIN{printf \"%.2f\", $base_ms/$cm}")
  ex=$(awk "BEGIN{print ($sp>$limit)?\"YES\":\"no\"}")
  echo "$IMG,$W,$H,$PIX,$ITERS,$REPS,$part,$variant,$base_ms,$cm,$tm,$sp,$limit,$ex,$ok" >> "$OUT_CSV"
  printf "  %-8s V%s  compute %10s ms  speedup %5s (limit %s)  output:%s\n" "$part" "$variant" "$cm" "$sp" "$limit" "$ok"
}

# run_sock <split> <variant> <baseline_ms> <baseline_hash>
run_sock() {
  local s=$1 v=$2 base_ms=$3 base_hash=$4
  sock_once() {
    ./part3_${s}_variant${v}_A.out $PORT "$IMG_PATH" $ITERS >/dev/null 2>&1 &
    local A=$!; sleep 0.2
    ./part3_${s}_variant${v}_B.out 127.0.0.1 $PORT output_part3_${s}_variant${v}.ppm
    local rc=$?; wait $A; return $rc
  }
  run_cfg "Part3_$s" "$v" 2 "$base_ms" "$base_hash" sock_once
}

for IMG in $IMAGES; do
  IMG_PATH="$IMG_DIR/$IMG.ppm"
  [ -f "$IMG_PATH" ] || { echo "skip $IMG_PATH (missing)"; continue; }
  read -r W H < <(awk 'NR==1{next} /^#/{next} {print $1, $2; exit}' "$IMG_PATH")
  PIX=$((W*H))
  echo "======== image $IMG  ${W}x${H}  ($PIX px)  ITERS=$ITERS  REPS=$REPS ========"

  for V in 1 2; do
    bl=""; bh=""
    for ((r=0; r<REPS; r++)); do
      o=$(./baseline_bench.out $V "$IMG_PATH" output_baseline.ppm $ITERS)
      bl+="$(echo "$o" | field "Compute Time")"$'\n'
      bh=$(echo "$o" | grep -o "Output hash: [0-9a-f]*" | awk '{print $3}')
    done
    BASE=$(echo -n "$bl" | mean)
    echo "-- Variant $V   baseline = $BASE ms"
    echo "$IMG,$W,$H,$PIX,$ITERS,$REPS,Baseline,$V,$BASE,$BASE,NA,1.00,1,no,yes" >> "$OUT_CSV"

    run_cfg Part2_1 $V 3 $BASE $bh ./part2_1_variant$V.out "$IMG_PATH" output_part2_1_variant$V.ppm $ITERS
    run_cfg Part2_2 $V 3 $BASE $bh ./part2_2_variant$V.out "$IMG_PATH" output_part2_2_variant$V.ppm $ITERS
    run_cfg Part2_3 $V 3 $BASE $bh ./part2_3_variant$V.out "$IMG_PATH" output_part2_3_variant$V.ppm $ITERS
    if [ "$RUN_PART3" = 1 ]; then
      run_sock 1 $V $BASE $bh
      run_sock 2 $V $BASE $bh
    fi
  done
done

rm -f output_baseline.ppm
echo "Done -> $OUT_CSV"
