#!/bin/bash
# hidden.sh <problems-dir>... : edai 1.2 and beat8k on sets the solver has not seen. Each <problems-dir> holds a
# problems.json and an answers.json in the challenge's format, inside a folder named after the set. edai runs through
# its own runners/computer/run300.c and harness.c, unmodified (only the problem table is generated, by the challenge
# history's challengers/beat/to_stream_data.py). The key is only ever read by the challenge's score.py. The generated
# copies of the problems are deleted afterwards.
. "$(dirname "$0")/env.sh"
cd $ROOT
one() {
  local dir=$1 d=$ROOT/out/hidden/$2
  mkdir -p $d/edai $d/b64 $d/deep $d/uno
  python3 $CH/challengers/beat/to_stream_data.py $dir/problems.json $dir/answers.json $d/edai/stream_data.h
  cp $CH/runners/computer/run300.c $CH/runners/computer/harness.c $CH/runners/computer/harness.h $d/edai/
  cc -std=c11 -O2 -I$CH/brain/include -I$d/edai $d/edai/run300.c $d/edai/harness.c $CH/brain/lib/macos-arm64/libaa.a -o $d/edai/run300 && $d/edai/run300 > $d/edai/answers.txt
  python3 tools/gen_problems.py $dir/problems.json $d/problems.h
  for v in "b64|" "deep|-DBT_DEEP" "uno|-DBT_VAL32 -DBT_FLOAT -DBT_MAX_EXAMPLES=12 -DBT_NO_SEQ"; do
    b=${v%%|*}; fl=${v#*|}
    cc -std=c99 -O2 -ffp-contract=off -Isrc -I$d $fl tools/main8k.c src/beat8k.c -o $d/$b/beat && $d/$b/beat | sed -E 's/ ms=[0-9]+//' > $d/$b/answers.txt
  done
  rm -f $d/problems.h $d/edai/stream_data.h $d/edai/run300 $d/edai/*.c $d/edai/*.h $d/*/beat
}
for dir in "$@"; do one $dir $(basename $(dirname $dir)) & done; wait
printf "%-10s %-24s %s\n" set solver score
for dir in "$@"; do
  name=$(basename $(dirname $dir)); d=$ROOT/out/hidden/$name
  for v in "edai:edai 1.2" "b64:beat8k" "deep:beat8k, deep" "uno:beat8k, Uno build"; do
    b=${v%%:*}; printf "%-10s %-24s %s\n" $name "${v#*:}" "$(python3 $CH/score.py $d/$b/answers.txt --key $dir/answers.json)"
  done
done
