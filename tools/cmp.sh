#!/bin/bash
# cmp.sh <runA> <runB>: counts problems whose answer, kind, or length/score differ between two runs of run8k.sh
# (timing ignored).
. "$(dirname "$0")/env.sh"
n=0
for s in $SETS; do
  a=$ROOT/out/$1/$s/answers.txt; b=$ROOT/out/$2/$s/answers.txt
  d=$(paste -d'|' <(sed -E 's/ ms=[0-9]+//' $a) <(sed -E 's/ ms=[0-9]+//' $b) | awk -F'|' '$1!=$2' | wc -l | tr -d ' ')
  n=$((n + d))
done
echo "$1 vs $2: $n of 1800 lines differ (answer, kind, length or trend score)"
