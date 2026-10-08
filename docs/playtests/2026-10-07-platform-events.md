# Common event queue: first installed slice

THE-864 changes input delivery to one fixed platform queue. This record covers
installed `99a631ec7c33a1e1d63c09adcfdfed2c4b24aec6`, built 2026-10-07
23:06:04 CDT and installed 23:14:19 CDT. It does not close U1: the lower
transport split, remaining clock consumers and combined-mode proof were still
in progress when these captures ran.

The queue reserves 1,024 records and a 1 MiB payload arena at creation. SDL
input, dedicated console lines, packet bytes and time have copied records.
The consumer retains a packet at the head when the decoder is waiting for
its preceding delivery. Queue and dedicated-stdin fixtures passed strict GCC
and Clang builds with ASan/UBSan. They check copied storage, overflow, retained
heads, UTF-8 and EOF behavior, and no allocations during bounded queue/line
operations. The strict executable and core test also built; core tests passed.

## Installed functional checks

Three sequential runs used fresh copies of all 34 owner settings files,
authenticated private displays and isolated audio output. A read-only observer
sampled completed frames. Actual W press and release changed the held sources
and latest movement command; source clocks and command sequences advanced.
The retained raw SDL logger records Space and C only, so no raw W event-log
claim is made.

| Scope | Private packet | Source clock, before to released | Forward command, held to released |
| --- | --- | --- | --- |
| Q1 classic CPU | `qa-private-av-rv2ws4j0` | 31.433883 to 31.865964 s | positive to 0 |
| Q2 rerelease software GL | `qa-private-av-cwmczxwj` | 10.050 to 10.225 s | positive to 0 |
| Original Q3 CPU | `qa-private-av-ete1910g` | 42.300 to 43.350 s | 127 to 0 |

All observed completed frames had an empty queue and zero dropped records.
Each scope passed twelve typed plain/slash FOV reads and writes, with both
aliases pointing to one canonical entry and the completed world using the
requested 90/120 FOV at the same pose. Explicit say and editable literal
`qa410 chat; fov 33` reached the HUD; FOV stayed 120. Unknown words remained
unknown commands. These also supplement THE-410 / MIKE-32.

The private audio captures were nonzero, but do not attribute individual
sounds. Each run returned public quit code 0, preserved the original settings,
all three installed files and helper pins, and left no recorded owned process
running. Q2 GL used Mesa llvmpipe on the private Xvfb route, not a GPU timing
run. The retained helper has no public composition flags, so no combined run
was made or inferred.

The review index is `/tmp/qa-u1-common-console-99a-20261007/U1-three-scope-index.json`.
Each packet retains `U1-bounded-readout.json`, `THE410-bounded-readout.json`,
event and console logs, actual audio output and screenshots under
`user/evidence`. Representative world and received-chat images were inspected.

## Pinned frame cost

Q2 rerelease `base1` CPU measurements were sequential, pinned to CPUs
`0-7,12-19`, without a debugger or sampling profiler. The copied owner profile
kept its simulation settings and FOV. Fullscreen, FPS caps and swap interval
were explicitly disabled; actual drawable dimensions and public cvar reads
were checked. Each result uses 600 completed intervals after more than 800
warm intervals. Private presentation and the retained observer's file I/O are
included. Audio was disabled for timing.

| Installed source | CPU dimensions | Median, ms | p99, ms |
| --- | --- | ---: | ---: |
| Previous `989b43c5` | 640 x 400 | 5.414529 | 14.632088 |
| First queue slice `99a631ec` | 640 x 400 | 5.534371 | 14.537665 |
| Previous `989b43c5` | 320 x 200 | 3.901923 | 5.676999 |
| First queue slice `99a631ec` | 320 x 200 | 4.223223 | 7.474798 |

No speedup is claimed. This pair does not isolate queue overhead from variable
cache work, simulation cadence and private presentation. Both CPU targets
remain open. The new packets are `qa-private-av-1c66d746` and
`qa-private-av-vo8r67qi`, each with `timing-result.json` and the warmed stage
reports. Both quit normally and preserved original settings and installed
files. Previous work counters are recorded in
[the CPU brush report](2026-10-07-cpu-brush-work.md).
