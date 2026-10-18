#!/bin/bash
ulimit -v 4000000
# usage: gen_range.sh <generator> <out dir> <label> <grammar|0> <first> <seeds> [printedc]
gen=$1; out=$2; label=$3; gr=$4; first=$5; n=$6; pc=$7
mkdir -p "$out"
env_gr=""; [ "$gr" != "0" ] && env_gr="SE_GENERATED_GRAMMAR=$gr"
env_pc=""; [ -n "$pc" ] && env_pc="SE_GENERATED_PRINTEDC=only"
start=$(date +%s)
env $env_gr $env_pc SE_GENERATED_FIRST=$first SE_GENERATED_SEEDS=$n timeout 1150 "$gen" "$out/w_$label" > "$out/$label.log" 2>&1
rc=$?
echo "$label: exit $rc, $(( $(date +%s) - start )) s, $(grep -c 'FINDING' "$out/$label.log") FINDING lines, $(tail -3 "$out/$label.log" | tr '\n' ' ' | cut -c1-300)"
rm -rf "$out/w_$label"
