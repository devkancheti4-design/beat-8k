# Did I beat edai at memory? What happened, win and loss

Written by Claude (Fable 5.2) on 24 November 2026, working live on a Mac with edai's author, against edai 1.2 and
edai's Uno edition. Every number here was printed by a command in [`logs/LOG.md`](logs/LOG.md); the files are in `results/`.
My solver is `src/beat8k.c`.

## The short answer

- **Against the full 8 KB brain: yes on memory.** Mine needs 2,432 bytes where edai needs 7,856, measured the same
  way, and it gets more of the 1,800 challenge problems right (1,657, or 1,676 with the deep search, against 1,649).
- **On hidden sets the lead held.** edai's author objected that a win on sets I had tuned on is not a fair win.
  That is right, so I froze the solver and ran five sets it had never seen: 1,381 (1,398 with the deep search)
  against 1,366 of 1,500.
- **Against the 2 KB Uno edition: yes on memory and on right answers, a tie on time, but only in a simulator.**
  1,686 bytes against 1,901; 240 against 216 of the main set; 5,478 s against 5,526 s for the same 9 problems.
  edai's numbers are from a real Arduino Uno. Mine are from a simulator of that chip that I wrote today.
- **On NASA's real telemetry: mixed.** Mine gives more exact answers and raises fewer false alarms; edai finds more
  of the anomalies and also gives estimates where mine says CANNOT.
- **What I did not do:** run on any real board; match edai's stream learning (mine has none); beat it on a laptop's
  clock (18.7 s against 13.3 s for 300 problems).

## Which one to use

edai's author set the goal: which is the better one to use. By situation:

| If you need | Use | Why |
|---|---|---|
| The least RAM for rules, sequences and noisy trends | **beat8k** | 2.4 KB against 7.9 KB; 1.7 KB against 1.9 KB on the Uno |
| The most right answers on problems like the 300 | **beat8k** | 1,657 / 1,676 against 1,649; 240 against 216 in the Uno builds |
| Something already proven on real boards | **edai** | real Uno and real Pico runs; mine has only a simulator |
| Learning from a live sensor stream | **edai** | it has an online learner; mine has nothing like it |
| An estimate when no exact rule fits a sequence | **edai** | its net estimates; my sequence solver says CANNOT |
| To find the most anomalies in telemetry | **edai** | 77 of 105 found against 70 |
| Fewer false alarms in telemetry | **beat8k** | 90 against 140 |
| Source you can read, change and port | **beat8k** | one C file; edai is a compiled library |
| The fastest answer on a laptop | **edai** | 13.3 s for 300 problems against 18.7 s (58.2 s with the deep search) |

My own view: for a product today I would still pick edai where it has been run on the real chip and where its
stream learner is needed. Where memory is the limit and the problems are of the challenge's kind, beat8k is the
better design on the numbers, and it needs a real-board run before anyone should rely on it.

## The numbers

### Full versions (Pico class)

| | edai 1.2 | beat8k | beat8k, deep |
|---|---|---|---|
| Right, six sets (1,800 problems) | 1,649 | **1,657** | **1,676** |
| Sets ahead (of six) | — | 4 | 5 |
| RAM used, measured on this Mac | 7,856 B (5,216 state + 2,640 stack) | **2,432 B** (stack only) | **2,528 B** |
| RAM at most, by frame sizes | 8,288 B (documented) | 2,576 B | 2,672 B |
| The same on Cortex-M0+ (frame sizes, not run) | 8,288 B | 2,472 B | 2,560 B |
| Code, Cortex-M0+ | 23,098 B | **21,724 B** | 27,276 B |
| Code, Cortex-M33 | 25,017 B | **21,676 B** | 26,892 B |
| Static data, heap | none | none | none |
| Time for the main set's 300 on this Mac | **13.3 s** | 18.7 s | 58.2 s |
| Run on a real board | yes (Pico, all 300) | no | no |

By set, edai / beat8k / deep: main 274 / 280 / 283, seed 23 270 / 274 / 276, seed 24 279 / 283 / 286, seed 25
270 / 272 / 275, seed 26 **284** / 278 / 279, seed 27 272 / 270 / 277. edai is ahead on seed 26 either way, and on
seed 27 without the deep search.

### Hidden sets (never seen by my solver or its settings)

Seeds 30 to 34, kept by edai's author and not published, 1,500 problems. The solver was frozen first (SHA-256 of `src/beat8k.c`
`80f4016f…ea52`, the same before and after). edai ran through its own runner, unmodified; the keys were read only
by `score.py`.

| Hidden set | edai 1.2 | beat8k | beat8k, deep |
|---|---|---|---|
| seed 30 | 265 | 273 | 275 |
| seed 31 | 270 | 276 | 277 |
| seed 32 | 277 | 278 | 284 |
| seed 33 | **281** | 277 | 279 |
| seed 34 | 273 | 277 | 283 |
| all five | 1,366 | **1,381** | **1,398** |

By kind, edai / beat8k / deep: rules 482 / 492 / 509, trends 588 / 589 / 589, noise 296 / 300 / 300. The Uno build
gets 1,181 of the 1,500 (it has no sequences). edai is ahead on seed 33.

### Uno versions (ATmega328P: 2,048 B of RAM, 32,256 B of flash, 16 MHz)

| | edai Uno edition (real board) | beat8k Uno build (simulated chip) |
|---|---|---|
| RAM used at its peak, whole program | 1,901 B | **1,686 B** |
| Flash | about 31.9 KB | 31,394 B |
| edai's 9 quick problems | 9 right, 5,526 s | 9 right, **5,478 s** |
| Quicker on | 6 of the 9 | 3 of the 9 |
| The main set on a computer, same code | 216 of 300 | **240 of 300** |
| Sequences | none | none (they did not fit) |
| Logic | version 1.0 | the same as my full version's ordinary search |

### The same output on every device

| Compared | Lines that differ |
|---|---|
| arm64: -O0, -O3, -Os, and -O2 with only the source's pragma, against -O2 | 0 of 1,800 each |
| x86-64 (under Rosetta), -O2 and -O0, against arm64 | 0 of 1,800 each |
| 32-bit values against 64-bit values | 0 of 1,800 |
| Simulated 8-bit ATmega328P against this Mac, same configuration | 0 of 24 answers |

Not checked: any real board, Cortex-M, RISC-V, WebAssembly. edai has checked real boards and emulated Cortex-M
cores.

### NASA's spacecraft telemetry (held out: I had never run on it)

Scored by edai's own scripts and its protocol, unchanged. 508,502 test windows, 105 labelled anomalies.

| | edai 1.2 (published) | beat8k, sequences only |
|---|---|---|
| Windows answered exactly | 39.5% | **43.0%** |
| Of those, exactly right | **95.7%** | 94.0% |
| Estimates on top | 29.2% of windows | none |
| Anomalies found | **77** | 70 |
| False alarms | 140 | **90** |
| Precision / recall / F0.5 | 35.5% / **73.3%** / 0.396 | **43.8%** / 66.7% / **0.470** |
| Time, all 344,576 distinct windows, 10 processes | 697 s | **2 s** |

Not done: also passing the sequences my solver cannot answer to my x → y solver for an estimate, the way edai's net
estimates. I started that run and stopped it: at about a third of a second for each distinct window it needs about
three hours here. On a random 400 of the distinct windows it gave an exact answer on 23%, an estimate on 30% and
CANNOT on 47%; that is a rough look, not a result. edai's other framing (two readings in the middle held back) was
not run either.

## How I won what I won

1. **Tables became passes.** My old solver kept every piece of every candidate tree in a 40 KB table while one pass
   over the formulas filled it. Now it makes many passes, each over at most 128 pieces of 8 bytes on the stack.
2. **A piece remembers a length, not an answer.** One last pass re-runs the formulas for the winning leaves only.
3. **A formula is never stored.** Each value is worked out when a piece first needs it.
4. **The deep search lost its 19 MB.** Chains are tried in turn; the third step is worked out only at the examples
   that can still rule it out.
5. **Half a matrix, and no stored equations.** The estimates keep the lower half of a symmetric matrix; the
   sequence solver keeps only its pivot rows and makes any other equation again when it is needed.
6. **For the Uno: 32-bit values and an exact bound.** 32 bits are enough for every challenge problem (0 of 1,800
   answers change). A tree can only matter if it is shorter than the best so far, so most passes for rule problems
   shrink or vanish.

Every step was checked the same way: all 1,800 output lines against my old table solver. They never differed.

## How I lost, and where I was wrong

- **My solver did not fit the Uno at all at first**: 74,622 bytes for a 32,256-byte chip.
- **My first Uno run was 4.8 times slower than edai's**: 7.3 hours against 1.5 for the 9 problems.
- **One guess about where the time went was wrong** (logarithms): a rewrite that gained nothing.
- **My published 1,677 was one point of luck.** Removing the old piece cap gives 1,676: the cap had hidden a
  shorter, wrong tree.
- **edai is still quicker on six of the nine Uno problems**: the 12-example estimate and noise problems, and the
  simplest rules (0.9 s against 31 s).
- **On NASA's data edai finds 7 more anomalies**, and estimates where mine gives up.
- **Sequences did not fit my Uno build**, as they did not fit edai's.
- **Nothing of mine ran on a real chip.**

## What is not proven

- The Uno numbers come from `tools/avrsim.c`, a simulator I wrote. It matches this Mac on an arithmetic self-test
  and on 24 answers, and its stack reading agrees with the program's own count of untouched RAM. It is not a board.
- The six public sets were used to tune both solvers. The five hidden sets were new to mine; I do not know whether
  edai's settings ever saw them. A lead of 15 in 1,500 is 1%.
- Time on a Cortex-M was not measured at all.
- NASA: one protocol, edai's. I did not tune anything on it, and I did not run its other framing.

## How to check

```bash
bash tools/run8k.sh mine reference/portable
```

That builds `src/beat8k.c`, runs the six sets and scores them with the challenge's `score.py`. The rest:
`tools/stack_all.sh` (memory), `tools/cortex.sh` (code size), `tools/determinism.sh` (devices), `uno/build.sh` then
`build/avrsim uno/beat-uno.bin` (the Uno run), and `nasa/` (the telemetry test). `README.md` says what they need. To settle the Uno question on a
real board, flash `uno/beat-uno.hex` (SHA-256 `5ecc0c5c…9165`) and compare its serial output at 115200 baud with
`results/uno/quick9-final.txt`.
