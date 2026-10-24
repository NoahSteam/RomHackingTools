#!/bin/bash
# usage: replay15.sh <generator> <scratch dir> "<hex words>" [extra env...]  -- runs one function through the printed-C differential and prints the text
gen=$1; out=$2; w=$3
( ulimit -v 4000000; SE_GENERATED_PRINTEDC=only SE_GENERATED_PRINTEDC_WORDS="$w" SE_GENERATED_PRINTEDC_SHOW=1 timeout 300 $gen $out )
