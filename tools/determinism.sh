#!/bin/bash
# determinism.sh [cflags...]: the same source, built in different ways and for different processors, must print the
# same lines. Builds the solver for the six sets with several compiler settings, for arm64 and for x86-64 (run under
# Rosetta on an Apple-silicon Mac), runs all 1,800 problems each way, and counts the lines that differ from the first.
. "$(dirname "$0")/env.sh"
cd $ROOT
build() {  # name, compiler command...
  local name=$1; shift
  for s in $SETS; do
    d=out/det/$name/$s; mkdir -p $d; python3 tools/gen_problems.py $(setdir $s)/problems.json $d/problems.h
    ("$@" -std=c99 -Isrc -I$d tools/main8k.c src/beat8k.c -o $d/beat && $d/beat | sed -E 's/ ms=[0-9]+//' > $d/answers.txt) &
  done; wait
}
cmpto() {  # name, reference name, label
  local n=0
  for s in $SETS; do n=$((n + $(paste -d'|' out/det/$1/$s/answers.txt out/det/$2/$s/answers.txt | awk -F'|' '$1!=$2' | wc -l))); done
  printf "  %-46s %s of 1800 lines differ\n" "$3" "$n"
}
build a64-O2      cc -arch arm64 -O2 -ffp-contract=off "$@"
build a64-O0      cc -arch arm64 -O0 -ffp-contract=off "$@"
build a64-O3      cc -arch arm64 -O3 -ffp-contract=off "$@"
build a64-Os      cc -arch arm64 -Os -ffp-contract=off "$@"
build a64-O2-prag cc -arch arm64 -O2 "$@"
build x64-O2      cc -arch x86_64 -O2 -ffp-contract=off "$@"
build x64-O0      cc -arch x86_64 -O0 -ffp-contract=off "$@"
echo "against arm64, clang -O2 -ffp-contract=off ($*):"
cmpto a64-O0 a64-O2 "arm64, -O0"
cmpto a64-O3 a64-O2 "arm64, -O3"
cmpto a64-Os a64-O2 "arm64, -Os"
cmpto a64-O2-prag a64-O2 "arm64, -O2, only the #pragma (no flag)"
cmpto x64-O2 a64-O2 "x86-64 (Rosetta), -O2"
cmpto x64-O0 a64-O2 "x86-64 (Rosetta), -O0"
