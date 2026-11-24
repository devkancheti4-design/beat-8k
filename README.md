# beat8k: my solver for edai's challenge, rebuilt to need about 2 KB

This repository is one day's attempt (24 November 2026) to beat [edai](https://github.com/devkancheti4-design/edai)
1.2 at its own strength, memory. The verdict, with
what I won and what I lost, is in [`TESTIMONIAL.md`](TESTIMONIAL.md). The log of what was run, in order, is in
[`logs/LOG.md`](logs/LOG.md).

## The result in one table

| | edai 1.2 | beat8k |
|---|---|---|
| RAM, full version | 7,856 B | **2,432 B** (2,528 with the deep search) |
| Right answers, six sets of 300 | 1,649 | **1,657** (1,676 with the deep search) |
| Right answers, five hidden sets of 300 | 1,366 | **1,381** (1,398 with the deep search) |
| RAM, Arduino Uno version | 1,901 B, on a real Uno | **1,686 B**, on a simulated one |
| Right answers, Uno version, main set | 216 | **240** |
| Time, Uno, edai's 9 quick problems | 5,526 s | 5,478 s |
| Time, laptop, 300 problems | **13.3 s** | 18.7 s (58.2 s with the deep search) |
| Run on a real board | **yes** | no |
| Stream learning | **yes** | no |

## What is here

| Path | What it is |
|---|---|
| `src/beat8k.c`, `src/beat.h` | the solver: one C file, no static data, no heap, no C library |
| `uno/` | the Arduino Uno program (`main_uno.c`), its build script, the ready `beat-uno.hex`, and the simulator's self-test |
| `tools/avrsim.c` | the simulator of the Uno's processor used for every Uno number (instructions, flags, cycle counts) |
| `tools/run8k.sh`, `tools/cmp.sh` | build and run the six sets, score them, compare every output line with another run |
| `tools/stack_all.sh`, `tools/cortex.sh`, `tools/stackbound.py` | memory on a Mac; code size and frame chains on Cortex-M |
| `tools/determinism.sh`, `tools/cmp_uno.py` | the same output on arm64, x86-64 and the simulated 8-bit chip |
| `tools/timing.sh` | time on this computer, edai through its own runner |
| `tools/hidden.sh` | edai and beat8k on sets the solver has not seen (the sets themselves are not kept here) |
| `nasa/nasa_run_beat.c` | the solver in the place of edai's `space/nasa_run.c`, for NASA's telemetry test |
| `reference/` | the answers of the table solver beat8k came from, with its piece cap removed: what every change was checked against |
| `results/` | every final answer (five builds, six sets), the Uno logs, and the measurements |
| `logs/LOG.md` | the log |

It needs two things from outside: edai's repository at commit `b3c00e9` (the problems, their keys, `score.py`,
edai's library and runner), checked out beside this one as `../edai-challenge` or named in `EDAI`; and, for the Uno
program, Arduino's `avr-gcc`. Measured on an Apple-silicon Mac.

```bash
git clone https://github.com/devkancheti4-design/edai ../edai-challenge && git -C ../edai-challenge checkout b3c00e9
```

## Build options

| Option | Effect |
|---|---|
| (none) | 64-bit values, up to 16 examples, sequences, double-precision estimates |
| `-DBT_DEEP` | also search chains of three operations: more rules found, three times slower |
| `-DBT_VAL32` | 32-bit values (inputs within about ±67 million): smaller and faster on small chips |
| `-DBT_FLOAT` | single-precision estimates, for chips without 64-bit floating point |
| `-DBT_NO_SEQ` | leave out sequences |
| `-DBT_MAX_EXAMPLES=12` | at most 12 examples: a smaller batch |

The Uno program is built with the last four. Always compile with `-ffp-contract=off`.

## Run it

```bash
bash tools/run8k.sh mine reference/portable
```

```bash
bash tools/run8k.sh mine-deep reference/deep -DBT_DEEP
```

Each builds the solver for the six sets, runs them on six cores, scores them, and counts the answers that differ
from my old table solver (0 of 1,800 is the expected count). The first takes about half a minute, the second about
a minute and a half.

```bash
bash uno/build.sh
```

```bash
cc -O2 -o build/avrsim tools/avrsim.c && build/avrsim uno/beat-uno.bin
```

The second runs the Uno program on the simulated chip: 5,478 s of chip time, about two minutes here.

## What it cannot do

- It has not run on a real board.
- It has no learner for sensor streams, and no estimate for a sequence it cannot solve exactly.
- Its Uno build has no sequences and takes at most 12 examples.
- The 32-bit build refuses inputs beyond about ±67 million for x and ±1 billion for y.
