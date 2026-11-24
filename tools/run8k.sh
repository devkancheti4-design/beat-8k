#!/bin/bash
# run8k.sh <name> <reference-dir> [cflags...]: builds src/beat8k.c for the six sets, runs them (one process a set),
# scores them with the challenge's score.py, and counts answers that differ from the reference's.
#   bash tools/run8k.sh mine reference/portable
#   bash tools/run8k.sh mine-deep reference/deep -DBT_DEEP
. "$(dirname "$0")/env.sh"
name=$1; ref=$2; shift 2
case $ref in /*) ;; *) ref=$PWD/$ref ;; esac
tot=0; difftot=0
for s in $SETS; do
  d=$ROOT/out/$name/$s; mkdir -p $d
  python3 $ROOT/tools/gen_problems.py $(setdir $s)/problems.json $d/problems.h
  (cd $d && cc -std=c99 -O2 -ffp-contract=off -I$ROOT/src -I. "$@" -o beat $ROOT/tools/main8k.c $ROOT/src/beat8k.c || exit 1
   s0=$(date +%s); ./beat > answers.txt 2> err.txt; echo $(( $(date +%s) - s0 )) > secs.txt) &
done; wait
for s in $SETS; do
  d=$ROOT/out/$name/$s
  r=$(python3 $CH/score.py $d/answers.txt --key $(setdir $s)/answers.json)
  nd=$(paste <(cut -d' ' -f1,2 $d/answers.txt) <(cut -d' ' -f1,2 $ref/$s.txt) | awk '$2!=$4' | wc -l | tr -d ' ')
  echo "$name $s: $r | differ from reference: $nd | $(cat $d/secs.txt) s"
  tot=$((tot + $(echo "$r" | awk '{print $2}'))); difftot=$((difftot + nd))
done
echo "$name TOTAL $tot of 1800, answers differing from the reference: $difftot"
