#!/bin/bash

make -s build-sharpen

IMAGES=("1" "2" "3" "4" "5" "6" "7")
RUNS=5

for img in "${IMAGES[@]}";do
    echo "Processing $img.ppm ($RUNS runs)"

    for i in $(seq 1 $RUNS);do
        make run-sharpen INPUT="$img" OUTPUT="${img}_out" | grep "METRICS:"
    done | awk -v runs="$RUNS" '{
        read += $2
        s1 += $3
        s2 += $4
        s3 += $5
        write += $6
    }END {
        printf "Averages (ms) -> Read: %.2f | S1: %.2f | S2: %.2f | S3: %.2f | Write: %.2f\n\n",
                read/runs, s1/runs, s2/runs, s3/runs, write/runs
    }'
done
