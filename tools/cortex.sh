#!/bin/bash
# cortex.sh: code size and the deepest chain of stack frames of beat8k on Cortex-M0+ and Cortex-M33, for each build,
# from objects compiled by clang; and the code size of edai's libraries read the same way (elfsize.py).
. "$(dirname "$0")/env.sh"
cd $ROOT; mkdir -p build/cm
one() {  # label, target, cpu, build flags...
  local label=$1 tgt=$2 cpu=$3; shift 3
  clang --target=$tgt -mcpu=$cpu -Os -ffreestanding -fno-stack-protector -ffp-contract=off -Isrc "$@" -fstack-usage -c src/beat8k.c -o build/cm/o.o 2>/dev/null
  mv build/cm/o.su build/cm/o_keep.su 2>/dev/null
  clang --target=$tgt -mcpu=$cpu -Os -ffreestanding -fno-stack-protector -ffp-contract=off -Isrc "$@" -S src/beat8k.c -o build/cm/o.s 2>/dev/null
  printf "%-34s %s\n" "$label" "$(python3 tools/elfsize.py build/cm/o.o | tail -1 | sed 's/.*o\.o: //')"
  python3 tools/stackbound.py build/cm/o.s build/cm/o_keep.su bt_solve bt_solve_sequence 2>&1 | sed 's/^/      /' | cut -c1-200
}
for t in "thumbv6m-none-eabi cortex-m0plus" "thumbv8m.main-none-eabi cortex-m33"; do
  set -- $t
  echo "== $2"
  one "64-bit" $1 $2
  one "64-bit, deep" $1 $2 -DBT_DEEP
  one "32-bit" $1 $2 -DBT_VAL32
  one "32-bit, deep" $1 $2 -DBT_VAL32 -DBT_DEEP
  one "32-bit, 12 examples, float, no seq" $1 $2 -DBT_VAL32 -DBT_FLOAT -DBT_MAX_EXAMPLES=12 -DBT_NO_SEQ
done
echo "== edai 1.2 libraries, same tool"
for l in cortex-m0plus cortex-m33; do printf "%-34s %s\n" "edai $l" "$(python3 tools/elfsize.py $CH/brain/lib/$l/libaa.a | tail -1 | sed 's/.*libaa\.a: //')"; done
