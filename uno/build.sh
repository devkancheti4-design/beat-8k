#!/bin/bash
# build.sh [extra flags]: builds the Arduino Uno program (beat-uno.elf, .hex, .bin) and prints its size.
# Needs avr-gcc: Arduino's own by default (AVR core 1.8.x); set AVR to another toolchain's bin folder.
#   -DUNO_GEN   take the problems from problems_gen.h (made by tools/gen_uno_problems.py) instead of edai's quick 9
AVR=${AVR:-$HOME/Library/Arduino15/packages/arduino/tools/avr-gcc/7.3.0-atmel3.6.1-arduino7/bin}
cd "$(dirname "$0")"
CF="-mmcu=atmega328p -Os -std=gnu99 -ffunction-sections -fdata-sections -fno-inline-small-functions -fno-move-loop-invariants -fno-tree-scev-cprop -I../src -I. -DBT_VAL32 -DBT_FLOAT -DBT_MAX_EXAMPLES=12 -DBT_NO_SEQ -DF_CPU=16000000UL"
$AVR/avr-gcc $CF "$@" -c ../src/beat8k.c -o beat8k.o || exit 1
$AVR/avr-gcc $CF "$@" -c main_uno.c -o main_uno.o || exit 1
$AVR/avr-gcc -mmcu=atmega328p -Os -Wl,--gc-sections -Wl,--relax -o beat-uno.elf main_uno.o beat8k.o -lm || exit 1
$AVR/avr-objcopy -O ihex -R .eeprom beat-uno.elf beat-uno.hex
$AVR/avr-objcopy -O binary -R .eeprom beat-uno.elf beat-uno.bin
$AVR/avr-size beat-uno.elf | tail -1 | awk '{printf "flash %d of 32256 bytes (code %d + initialised data %d); static RAM %d bytes (data %d + bss %d)\n", $1 + $2, $1, $2, $2 + $3, $2, $3}'
rm -f beat8k.o main_uno.o
