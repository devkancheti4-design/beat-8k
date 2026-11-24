#!/bin/bash
# stack_all.sh [sets...]: peak stack of each build of beat8k, measured with a painted stack (stack_probe_beat.c),
# and edai's measured the same way (stack_probe_edai.c). Default: the main set. macOS on Apple silicon.
. "$(dirname "$0")/env.sh"
cd $ROOT; mkdir -p build
ASK=${@:-main}
for set in $ASK; do python3 tools/to_lines.py $(setdir $set)/problems.json > build/$set.lines; done
probe() {  # label, flags...
  local label=$1 r=0 s=0; shift
  cc -std=c99 -O2 -ffp-contract=off -fno-stack-protector -Isrc "$@" tools/stack_probe_beat.c src/beat8k.c -o build/sp_tmp -lpthread || return
  for set in $ASK; do
    ./build/sp_tmp < build/$set.lines > build/sp_out.txt 2> /dev/null
    r1=$(awk '$2=="R" && $1>m {m=$1} END{print m+0}' build/sp_out.txt); s1=$(awk '$2=="S" && $1>m {m=$1} END{print m+0}' build/sp_out.txt)
    [ $r1 -gt $r ] && r=$r1; [ $s1 -gt $s ] && s=$s1
  done
  printf "%-44s x->y problems %5d B   sequences %5d B\n" "$label" $r $s
}
echo "peak stack on: $ASK (static data 0, heap 0 in every build)"
probe "64-bit"
probe "64-bit, deep" -DBT_DEEP
probe "32-bit" -DBT_VAL32
probe "32-bit, deep" -DBT_VAL32 -DBT_DEEP
probe "32-bit, 12 examples, float, no sequences" -DBT_VAL32 -DBT_FLOAT -DBT_MAX_EXAMPLES=12 -DBT_NO_SEQ
cc -std=c11 -O2 -I$CH/brain/include tools/stack_probe_edai.c $CH/brain/lib/macos-arm64/libaa.a -o build/sp_edai -lpthread
for set in $ASK; do ./build/sp_edai < build/$set.lines 2>&1 >/dev/null | tail -1 | sed "s/^/edai 1.2, $set: /"; done
