#!/bin/bash
# timing.sh: time to answer the main set's 300 problems on this computer, one process at a time: edai 1.2 through its
# own runner (runners/computer/run300.c and harness.c, unmodified), each build of beat8k, and the table solver beat8k
# came from (challengers/beat in edai's history, with its piece cap removed).
. "$(dirname "$0")/env.sh"
mkdir -p $ROOT/build/timing && cd $ROOT/build/timing
cp $CH/runners/computer/run300.c $CH/runners/computer/harness.c $CH/runners/computer/harness.h $CH/runners/computer/stream_data.h .
cc -std=c11 -O2 -I$CH/brain/include -I. run300.c harness.c $CH/brain/lib/macos-arm64/libaa.a -o edai300
t() { local s=$(python3 -c 'import time; print(time.time())'); "$@" > /dev/null 2>&1; python3 -c "import time; print('%.1f s' % (time.time() - $s))"; }
printf "%-46s %s\n" "edai 1.2 (its own runner)" "$(t ./edai300)"
python3 $ROOT/tools/gen_problems.py $CH/problems/problems.json problems.h
for cfg in "beat8k, 64-bit|" "beat8k, 64-bit, deep|-DBT_DEEP" "beat8k, 32-bit|-DBT_VAL32" "beat8k, 32-bit, deep|-DBT_VAL32 -DBT_DEEP" "beat8k, 32-bit, 12 examples, float, no seq|-DBT_VAL32 -DBT_FLOAT -DBT_MAX_EXAMPLES=12 -DBT_NO_SEQ"; do
  label=${cfg%%|*}; fl=${cfg#*|}
  cc -std=c99 -O2 -ffp-contract=off -I$ROOT/src -I. $fl $ROOT/tools/main8k.c $ROOT/src/beat8k.c -o b8 && printf "%-46s %s\n" "$label" "$(t ./b8)"
done
cp $CH/challengers/beat/c/main.c $CH/challengers/beat/c/beat.c $CH/challengers/beat/c/beat.h $CH/challengers/beat/c/deep.c .
cc -std=c99 -O2 -ffp-contract=off -DBT_MAXQ=16384 -o oldp main.c beat.c 2>/dev/null && printf "%-46s %s\n" "the table solver (1.4 MB of tables)" "$(t ./oldp)"
