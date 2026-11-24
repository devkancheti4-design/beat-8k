# Log: trying to beat edai 1.2 at memory

Every entry is something that was actually run on the Mac this was done on, with the numbers it printed. Kept as the work went, not
written afterwards. Dates are 24 November 2026 unless an entry says otherwise.

## 1. The target (edai 1.2, re-measured)

- edai's repository now headlines memory: "a brain that learns and reasons in 8 KB" (5,216 bytes of state and at most
  3,072 bytes of stack). The 300-problem challenge, its five fresh sets, its scorer and my earlier challenger are in
  its history at commit `b3c00e9`; the compiled brain (`brain/lib`) is unchanged since then, still version 1.2.
- Checked out `b3c00e9` and re-scored the stored answer files with the repository's own `score.py`:

  | Set | edai 1.2 | beat 2 portable (151 KB RAM) | beat 2 deep (19 MB RAM) |
  |---|---|---|---|
  | main | 274 | 280 | 283 |
  | seed 23 | 270 | 274 | 276 |
  | seed 24 | 279 | 283 | 287 |
  | seed 25 | 270 | 272 | 275 |
  | seed 26 | 284 | 278 | 279 |
  | seed 27 | 272 | 270 | 277 |
  | all six | 1,649 | 1,657 | 1,677 |

- So the job is: the same answers as my own solvers, in less memory than edai's 7,856 bytes (5,216 of state plus the
  2,640 bytes of stack I measured for 1.2 on this Mac; edai documents at most 3,072 of stack, 8,288 in all).

## 2. What had to change in my solver

- My portable solver keeps four tables in static memory (147 KB): the pieces a tree search examines (40 KB), one-step
  formulas (21 KB), a cache of formulas already seen (32 KB) and groups of pieces (16 KB). The deep one adds 19 MB.
- Its table of pieces is capped at 1,024. Measured: about 90 of 300 problems in each set hit the cap, yet with the cap
  removed only 2 of 1,800 answers change and no score changes (1,657 either way). So the low-memory version can drop
  the cap and must match the uncapped answers.
- Trees of three and four pieces cannot be dropped: without them the six-set score falls from 1,657 to 1,626.
- Work the old solver does on the main set (242 rule-type problems): 99.8 million formulas visited, 316 million tests
  of a formula against a piece, 6.1 s.

## 3. First low-memory version (v1): same answers, no static memory

- Rewrote the rule search (`src/beat8k.c`). Instead of one pass over the formulas filling a 40 KB table of pieces, it
  makes many passes, each over a batch of at most 128 pieces (8 bytes each) that lives on the stack. A piece keeps only
  the length and cost of its best formula; one last pass re-runs the formulas to work out the winning leaves' answers.
  The one-step formula table (21 KB) became two 146-bit maps; the cache of seen formulas (32 KB) is gone.
- Result, `tools/run8k.sh v1`: **all 1,800 answers identical** to the uncapped portable solver; 1,657 of 1,800.
  Time 19-22 s per set on this Mac (the table version: 6 s).
- Memory, painted-stack probe on the main set (`tools/stack_probe_beat.c`, the same method as for edai): static 0,
  heap 0, **peak stack 3,760 bytes** (the trend code, still unchanged, is the deepest; sequences 2,672).
  edai 1.2 on the same probe: 2,640 bytes of stack, plus its 5,216-byte state = 7,856.
- Not done yet at this point: the deep search (the 19 MB one), shrinking the trend and sequence code, speed.

## 4. The deep search without its 19 MB

- The deep search (chains of three operations) kept every two-step formula in a hash table: 19 MB. Rewritten to try
  each chain in turn. The third step is worked out only at the examples a target needs, and a last `+c`, `*c`, `<<`
  or negation is skipped, because `a*f + b` already covers it.
- Reference to match: my old deep solver with its 1,024-piece cap removed scores 1,676, not the 1,677 I published.
  The cap had hidden one shorter, wrong tree on seed 24 (problem p149): one point of the old score was luck.
- Result (`tools/run8k.sh deep1 ... -DBT_DEEP`): **all 1,800 answers identical** to the uncapped deep reference;
  **1,676 of 1,800**; 82-86 s per set on this Mac.

## 5. Smaller estimates and sequences (same printed answers)

- Estimates: the Gaussian-process matrix is symmetric, so only its lower half is kept (136 numbers for 16 examples,
  not 256) and solved in place as L D L'. Leaving an example out no longer copies the examples.
- Sequences: Gauss-Jordan over rationals stored a 16 x 8 matrix (2 KB). Now only the pivot rows are kept (6 x 7);
  any other equation is made again from the sequence when it is needed, and goes through the same operations.
- Result: 0 of 1,800 output lines differ from v1 (answer, kind, length and trend score all the same).

## 6. One source, formulas worked out lazily (v3 to v6)

- A formula is no longer a list of 18 values: each value is worked out when a piece first needs it. That removed the
  last three 144-byte lists. First try (v3) was correct but slower (40 s per set against 20 s): two-example pieces
  made nearly every formula be worked out in full. Fixed with a two-value filter (a two-example piece can only fit
  `y = F + b` or `y = aF`) and by keeping the inner step of two-step formulas: 34 s per set.
- Counted on the main set: 2,329 passes (483 full, 1,846 cheap), 163 million formula visits, 5.1 billion quick
  checks of a formula against a group of pieces (31 per visit). That last number is where the time goes.
- Large frames kept apart (the batch, the estimates' matrix, the deep search's targets never share the stack), and
  condition masks packed to 16 bits. **Peak stack 2,304 bytes** on the main set, same probe as before
  (rule search; sequences 1,208). No static data, no heap.
- Verified again: 64-bit build 1,657 with 0 of 1,800 lines different; deep build 1,676 with 0 different.

## 7. edai's Uno edition, and a 32-bit build to meet it

- edai's author pointed out edai's Uno edition. Read it (`runners/uno` at `b3c00e9`): 782 bytes of state plus stack,
  **1,901 of 2,048 bytes of RAM at its peak on a real Uno**, 31.9 KB of flash, no sequences, 12 examples at most,
  an 8-unit net, version 1.0 logic, 32-bit inputs. On a computer the same code gets 216 of the main set's 242
  x -> y problems (so 216 of 300). 92 minutes for its 9-problem quick set on the real board.
- My solver compiled for that chip as it stood: 74,622 bytes (95,534 with deep). The chip has 32,256. 64-bit
  integers on an 8-bit processor are the reason.
- Measured that 32-bit values are enough for the challenge: the largest value in any of the 1,800 problems is
  641,601, and capping formula values at 2^30 instead of 10^12 changes **0 of 1,800** answers.
- Added a 32-bit build (`-DBT_VAL32`), single-precision estimates (`-DBT_FLOAT`), `-DBT_NO_SEQ` and
  `-DBT_MAX_EXAMPLES=12`. The 32-bit build with sequences and double estimates gives 0 of 1,800 lines different
  from the 64-bit one.
- First Uno link: 32,678 bytes, 422 too many. Then removed every 64-bit division from the 32-bit build (the divisor
  is never more than 12, so a byte-at-a-time division does), kept differences in 32 bits with widening products,
  and moved the constant tables to flash. **Second link: 28,902 of 32,256 bytes of flash, 0 bytes of static RAM.**
- No Uno here, and no simulator installed. Wrote one (`tools/avrsim.c`): the ATmega328P's instructions and flags,
  2 KB of RAM, the datasheet's cycle counts. The program boots in it and prints its banner; it simulates about
  47 times faster than the real chip.

## 8. First run on the (simulated) Uno: right, small, and slow

- Checked the simulator first (`uno/selftest.c`): the same arithmetic on the simulated chip and on this Mac, 4,000
  rounds of 8-, 16-, 32- and 64-bit integer operations, overflow checks and single-precision floating point. One
  checksum of six differed at first; it was my test (a 16-bit `int` overflow, undefined on the AVR), not the
  simulator. Corrected, all six checksums are equal.
- The 9 problems of edai's quick set, first Uno build (`out/uno_sim_run1.txt`): **9 of 9 right, every answer the same
  as this Mac's build of the same configuration** (the three estimates to the cent), **1,688 of 2,048 bytes of RAM
  used** (the program's own count of never-touched bytes, 360, agrees with the simulator's stack reading).
- **Time: 26,331 s of chip time (7.3 hours). edai's Uno edition on the real board: 5,526 s (1.5 hours). 4.8 times
  slower.** Per problem, mine / edai: 3,706 / 815, 4,217 / 966, 1,263 / 0.9, 794 / 835, 815 / 614, 1,228 / 348,
  7,431 / 2.7, 3,742 / 965, 3,135 / 980 seconds.

## 9. Making the Uno build faster (same answers every time)

Profiled inside the simulator (chip time by function and by source line). In order:

| Change | First problem (p000), chip time |
|---|---|
| first build | 3,706 s |
| an 8-bit pre-check before the wide products; 16-bit mask scans; no shared prologues (they were 23% of the time); a one-byte "already worked out" test | 1,800 s |
| two-example pieces gated by the formula's cost; pieces share a group's three examples | 1,608 s |
| the piece loop and the quick check rewritten for an 8-bit processor (bytes first) | 1,312 s |
| lazy fit test, logarithm only when a formula could win, faster logarithm (no gain: my guess about where the time went was wrong) | 1,348 s |
| 16-bit division when the value fits; last bytes read first | 1,182 s |

- The honest profile after all that, for a 12-example problem: 5% the first pass, 22% two-piece trees, **70%
  three- and four-piece trees** (eight passes over 33,000 cheap formulas with about 110 pieces each), 1% the
  estimate. No single hot spot is left: a third quick checks, a fifth the piece loop, a third working out values.
- All 9 again with that build (`out/uno_sim_v13.txt`): 9 of 9, same answers, **8,370 s (2.3 hours), 1.5 times
  edai's**, 1,682 bytes of RAM. Mine is faster on the two 8-example estimate problems (269 and 279 s against 835
  and 614) and far slower on rules edai answers in a second or two (364 s against 0.9; 2,188 s against 2.7).
- Why rules were slow: unless the single formula is under 8 bits, my search always goes on to piece trees. Added an
  exact bound: a tree can only matter if it is shorter than the best accepted so far (or within the limit), so a
  pass only tries formulas whose shortest possible leaf still fits, and a whole stage is skipped when nothing could.

## 10. The bound, and the final Uno numbers

- With the bound in (`out/uno_sim_v14.txt`): the three rule problems of the quick set went from 364, 459 and 2,188 s
  to **31, 45 and 44 s**. Estimate and noise problems are unchanged (the bound cannot help where nothing fits).
- **All 9: 5,478 s of chip time. edai's Uno edition on the real board: 5,526 s.** 9 of 9 right, the same answers as
  this Mac's build; **1,686 of 2,048 bytes of RAM used** (362 never touched); **31,394 of 32,256 bytes of flash**.
  Per problem, mine / edai: 1,182 / 815, 1,369 / 966, 31 / 0.9, 269 / 835, 279 / 614, 45 / 348, 44 / 2.7,
  1,241 / 965, 1,018 / 980 s. So: a tie in total; edai is quicker on six of the nine, mine on three.
- Every build verified again after every change (`out/verify_v*.txt`): 64-bit and 32-bit builds 1,657 with 0 of
  1,800 lines different from my table solver; deep builds 1,676 with 0 different; the Uno configuration on this Mac
  1,417 of 1,800 (it has no sequences): main 240, seeds 23-27: 234, 243, 232, 238, 230. edai's Uno edition
  publishes only the main set: 216.

## 11. Memory and code size, final (`out/stack_final.txt`, `out/cortex_v14.txt`)

- Peak stack on the main set, painted-stack probe on this Mac (static data 0, heap 0 in every build):
  64-bit 2,432; 64-bit deep 2,528; 32-bit 2,248; 32-bit deep 2,344; 32-bit with 12 examples, float, no sequences
  1,944. edai 1.2 on the same probe: 2,640 of stack plus its 5,216-byte state = 7,856.
- The deepest chain of frames by the compiler's own frame sizes (an upper bound for any input): this Mac 2,576
  (deep 2,672); Cortex-M0+ 2,472 (deep 2,560); Cortex-M33 2,280 (deep 2,360); the compiler's arithmetic helpers come
  on top. edai documents 5,216 + 3,072 = 8,288.
- Code and constants, from the compiled objects (`tools/elfsize.py`), Cortex-M0+ / Cortex-M33: 64-bit 21,724 /
  21,676; 64-bit deep 27,276 / 26,892; 32-bit 19,152 / 19,520; 32-bit deep 23,640 / 23,912. edai 1.2: 23,098 / 25,017.
  So without the deep search mine is smaller; with it, larger on the M0+.
- Not measured: time on a Cortex-M. I have no ARM linker or emulator here, so nothing of mine has run on one.

## 12. The same output on every device (`out/determinism_64.txt`, `tools/cmp_uno.py`)

- All 1,800 problems, 64-bit build, against arm64 clang -O2: arm64 -O0, -O3, -Os, and -O2 with only the source's
  `#pragma` (no compiler flag): **0 of 1,800 lines differ** each. x86-64 (run under Rosetta) -O2 and -O0: **0 of
  1,800** each. 32-bit against 64-bit values: 0 of 1,800.
- 8-bit: the simulated ATmega328P, built with avr-gcc 7.3, against this Mac's build of the same configuration:
  **24 of 24 answers the same** (the quick 9 and 15 more: 5 rules, 5 estimates, 5 noise; estimates to the cent).
- Not checked: a real board of any kind, a Cortex-M, RISC-V, WebAssembly (no compiler for it here).

## 13. Held-out real-world data: NASA's SMAP and MSL telemetry

- edai's author asked for edai's NASA test. NASA's files were on this Mac already (the author's own copy); read in
  place.
  Wrote `nasa/nasa_run_beat.c`, which takes the place of edai's `space/nasa_run.c` (same input, same output lines),
  and ran edai's own scripts and fixed protocol unchanged. I had never run my solver on this data: nothing was tuned.
- **Sequences only** (my sequence solver: an exact answer or CANNOT), all 703,770 windows, 344,576 distinct, 2 s
  with 10 processes (a median of 35 microseconds a window):

  | test windows | exact answers | exactly right | on channels |
  |---|---|---|---|
  | mine, SMAP | 45.5% | 93.9% | 50 of 54 |
  | mine, MSL | 28.3% | 95.0% | 21 of 27 |
  | mine, both | 43.0% | 94.0% | 71 of 81 |
  | edai's AM, both (published) | 39.5% | 95.7% | 65 of 81 |

  Mine answers more windows exactly and is a little less often right; edai also gives estimates on 29.2% more,
  where mine says CANNOT.
- **The protocol's detector** (edai's `detect.py`, fixed before scoring):

  | | found | false alarms | missed | precision | recall | F0.5 |
  |---|---|---|---|---|---|---|
  | mine, sequences only | 70 | 90 | 35 | 43.8% | 66.7% | 0.470 |
  | edai 1.2 (published) | 77 | 140 | 28 | 35.5% | 73.3% | 0.396 |

  edai finds 7 more of the 105 anomalies; mine raises 50 fewer false alarms.
- Started a second run in which a sequence my solver cannot answer goes on to my x -> y solver for an estimate
  (asked about two points beyond its examples). Stopped it: a sample of 400 distinct windows averaged 328 ms each,
  about three hours for all 344,576 with 10 processes. The sample gave exact 23%, estimate 30%, CANNOT 47%. Not a
  result; edai's other framing was not run either.

## 14. Time on this Mac, one process, machine otherwise idle (`results/timing-mac.txt`)

| The main set's 300 problems | Time |
|---|---|
| edai 1.2, through its own runner | 13.3 s |
| beat8k, 64-bit | 18.7 s |
| beat8k, 64-bit, deep | 58.2 s |
| beat8k, 32-bit | 18.3 s |
| beat8k, 32-bit, deep | 66.8 s |
| beat8k, Uno configuration | 21.0 s |
| my old table solver (1.4 MB of tables, uncapped) | 5.3 s |

So giving up the tables cost a factor of 3.5 in time on a laptop, and edai is 1.4 times quicker than my 64-bit build.

## 15. Where it ended

- Full version: 2,432 bytes against 7,856; 1,657 (1,676 deep) against 1,649; smaller code without the deep search;
  slower on a laptop; never on a real board.
- Uno version: 1,686 bytes against 1,901; 240 against 216 on the main set; 5,478 s against 5,526 s on the quick 9;
  simulator only.
- NASA: more exact answers and fewer false alarms for mine; more anomalies found and estimates for edai.
- The verdict by use is in `TESTIMONIAL.md`.

## 16. A test on hidden sets (24 November 2026, 21:10)

- edai's author objected that I had won on sets I had tuned on, and asked for a test on hidden sets. That is
  fair: my solver never sees a key when it solves, but it and its noise cut were tuned on the six public sets, so a
  lead on those sets proves little. A blind test was owed.
- Froze the solver first. SHA-256 of `src/beat8k.c` at 21:09:52, before any hidden set was opened:
  `80f4016fe924497cbf190f162f2124d5acdf7eff0c28517ae38d40f6a817ea52`. The same after the test.
- Found five sets on this Mac that I had never seen, seeds 30 to 34, kept by edai's author. Same shape as
  the challenge's sets (300 problems: 120 rules, 120 trends, 60 noise; 8 or 12 examples); 2 to 5 problems of each
  also occur in a public set (17 of 1,500). I read the problems only; the keys were read only by `score.py`. I did
  not open the generator that sits near them, and I kept no copy of the problems.
- `tools/hidden.sh`: edai 1.2 through its own `run300.c` and `harness.c`, unmodified; beat8k built from the frozen
  source. Results (`results/hidden/scores-seeds-30-34.txt`):

  | Hidden set | edai 1.2 | beat8k | beat8k, deep | beat8k, Uno build |
  |---|---|---|---|---|
  | seed 30 | 265 | 273 | 275 | 233 |
  | seed 31 | 270 | 276 | 277 | 236 |
  | seed 32 | 277 | 278 | 284 | 238 |
  | seed 33 | **281** | 277 | 279 | 237 |
  | seed 34 | 273 | 277 | 283 | 237 |
  | all five (1,500) | 1,366 | **1,381** | **1,398** | 1,181 |

  By kind, edai / beat8k / deep: rules 482 / 492 / 509 of 600; trends 588 / 589 / 589 of 600; noise 296 / 300 / 300
  of 300.
- So the lead held on problems my solver had never seen: +15 without the deep search, +32 with it, four sets of
  five. edai is ahead on seed 33 either way. The noise cut I was worried about held: 300 of 300 noise, 589 of 600
  trends.
- The low-memory solver and my old table solver again gave the same lines on every hidden problem: 0 of 1,500
  differ, and 0 of 1,500 for the deep ones.
- What this does not show: I do not know whether edai's settings ever saw these five sets (they are the author's);
  edai's Uno edition was not run on them (its library is for the Uno only); five sets are 1,500 problems, and a lead
  of 15 is 1%.
