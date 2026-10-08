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

## Physical transport split

`2f6c2f46` replaces the lower receive operations with physical collection and
queued dispatch. SOCKS control, native IPX and KEX multicast reads now live in
transport code. Protocol adapters decode copied records and run hooks during
the common drain. An EMPTY boundary stays queued until saved logical deliveries
finish. Q2 final acknowledgements use the same intake and consumer. Frontend
clock reads use platform services.

Strict executable/core builds and core tests passed. Actual-source fixtures
passed copied localhost UDP/loopback payloads after physical storage overwrite,
KEX negotiation and dispatch-only hooks, saved LAN deliveries, interrupted
mDNS callback restore, timestamps and NULL continuation without fresh reads.
The mDNS fixture substitutes native multicast I/O, not the protocol or save
code. Clang is fully strict with ASan/UBSan. GCC ASan/UBSan retains one existing
sign-conversion warning exception in `message.c`; other units stay strict.
Real localhost SOCKS authentication/association and DOSBox registration,
reserved probes and deferred local/remote deliveries passed strict GCC ASan
and Clang ASan/UBSan. These fixtures launch no game.

The exact candidate passed fresh copied-owner-profile CPU and NVIDIA GL
Host/Ready/two-match/End/Close checks and normal public quit. It was built
23:31:15 CDT and installed 23:36:46 CDT through the qualified installer.
The receipt is `installed-u1-lower-intake-20261007.json`; all three installed
files directly match the qualified sources.

A new sequential pinned pair used the same stationary Q2 rerelease workload,
settings and observer as above:

| Installed source | CPU dimensions | Median, ms | p99, ms |
| --- | --- | ---: | ---: |
| Transport split `2f6c2f46` | 640 x 400 | 5.770822 | 15.030588 |
| Transport split `2f6c2f46` | 320 x 200 | 3.847471 | 5.638609 |

The packets are `qa-private-av-2nmkvpy5` and `qa-private-av-o6mly0ax`.
Each uses 600 intervals after warm-up, public quit 0, unchanged original
profile and installed files, and no remaining owned game. Results vary in
opposite directions between resolutions; no speedup or isolated overhead claim
is justified. CPU targets remain open. Installed per-game and combined checks
are recorded below.

## Installed transport split checks

The fixed `2f6c2f46` installation passed six console/input scopes: Q1 classic
CPU, Q1 rerelease CPU, Q2 classic CPU, Q2 rerelease software GL, native Q3 CPU
and original-module Q3 CPU. The final index is
`/tmp/qa-u1-common-console-lower-20261007/U1-six-scope-combined-index.json`.
All 72 plain/slash FOV commands were accepted, including canonical/alias
reads and writes with same-pose 90/120 world captures. Literal editable chat
containing a semicolon did not change FOV. Unknown commands stayed commands.
Real W presses reached the movement command and release cleared it; source
clocks advanced and completed queue observations were empty with zero drops.

A separate NVIDIA RTX 5060 Ti Custom run used the public menu to select a Q1
classic `start` world, Q3 movement, Ranger character and weapons, Q2 rerelease
monsters and single player. Actual mouse and W input crossed the Normal skill
teleporter, its forced angle survived settling, and public quit returned 0.
The coordinator inspected the spawned world and post-teleport images. This
run deliberately disabled audio; it does not prove mixed combat or sound.

Each console scope captured actual nonzero output on its private audio server.
That is delivery evidence, not attribution of individual cues. All seven runs
quit normally, kept the owner profile and three installed files unchanged,
and left no recorded owned processes running. The helper's raw SDL logger
records Space/C, so W evidence is from real input actions and consumed state.
The original/native Q3 console cases launched through the CLI, not menu Play.
Functional observers were attached; these runs make no performance claim.

The index preserves two visual limitations: an immediate combined spawn
capture was black before subsequent world captures, and Q2 classic CPU HUD
glyphs were clipped. No first-frame or complete renderer fidelity claim is
made. Native IPX and real multicast discovery remain unqualified by these
local transport fixtures and game runs.
