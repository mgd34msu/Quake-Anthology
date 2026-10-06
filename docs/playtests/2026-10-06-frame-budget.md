# Retail frame budgets, installed `ff437555`

Twelve quiet timing cases used the shipped executable at the stationary retail
spawn: Q1 `e1m1`, Q2 and Q2 rerelease `base1`, and Q3 `q3dm1`. Swap interval
was zero. Actual GL renderer identification was NVIDIA RTX 3090. All threads
were pinned to physical cores 0–7 through affinity `0-7,12-19`; CPU cases used
seven raster workers. Audio remained enabled with dummy output. No debugger,
sampling profiler, compiler or other project game ran alongside these cases.

Each case measured its last 600 presented-frame intervals. Q1/Q2 cases excluded
119 preceding presents; Q3 excluded 859 because its command waits span more
presented frames. Inclusive timer means come from the warmed report difference:
600 calls for Q1/Q2 and 1,200 for Q3. They describe that broader warmed interval,
not the last-600 frame percentiles. Real-time simulation can differ between runs.

| Game | Output | Median frame (ms) | p99 frame (ms) | Mean scene build (ms) | Mean render (ms) |
| --- | --- | ---: | ---: | ---: | ---: |
| Q1 classic | GL 1920×1080 | 1.237 | 1.903 | 0.537 | 0.314 |
| Q1 classic | CPU 640×400 | 4.347 | 6.498 | 0.551 | 3.057 |
| Q1 classic | CPU 320×200 | 2.435 | 3.563 | 0.530 | 1.442 |
| Q2 classic | GL 1920×1080 | 1.447 | 2.367 | 0.553 | 0.403 |
| Q2 classic | CPU 640×400 | 4.703 | 6.464 | 0.617 | 3.258 |
| Q2 classic | CPU 320×200 | 3.169 | 4.346 | 0.581 | 1.993 |
| Q2 rerelease | GL 1920×1080 | 1.679 | 3.216 | 0.689 | 0.495 |
| Q2 rerelease | CPU 640×400 | 7.918 | 11.347 | 0.948 | 5.797 |
| Q2 rerelease | CPU 320×200 | 5.089 | 8.420 | 0.792 | 3.655 |
| Q3 | GL 1920×1080 | 2.532 | 2.949 | 1.395 | 0.716 |
| Q3 | CPU 640×400 | 15.104 | 17.236 | 1.560 | 12.520 |
| Q3 | CPU 320×200 | 7.401 | 10.059 | 1.542 | 5.302 |

Targets remain GL 1080p below 2 ms, CPU 640×400 below 4 ms, and CPU 320×200
below 2 ms. Q1 GL is below 2 ms at both median and p99. Classic Q2 and Q2
rerelease GL medians are below 2 ms, but their p99 values exceed it. Q3 GL and
every CPU case still miss their targets. Q3 CPU rendering is the largest cost;
Q3 scene construction also dominates its GL frame. These are measured retail
spawn cases, not a full-campaign or moving-camera performance qualification.

All twelve runs reached their requested map and drawable, exited zero, and
preserved the receipt-qualified installed artifact. Summary receipt:
`suite-ff437555.json`, plus the two rerelease CPU receipts listed in
`2026-10-06-rerelease-cpu.md`.

A separate Q3 CPU diagnostic recorded six seconds of userspace samples after
261 warm presents. Texture sample-level work accounted for 18.61% and `hypot`
for 14.68% of total sampled userspace CPU across all threads. The sampling
succeeded without lost samples, but the helper timed out while waiting for its
final timer report; cleanup exited zero. Its retained samples support bottleneck
ranking only. They do not constitute another passing timing or public-quit
check. Evidence: `ff437555-current-q3-cpu640-ixpogdee/sampling-recovery.json`.

## Installed `0a327175`: shared nearest-mip sampling

Five cases repeated the same quiet stationary-spawn measurements on the shipped
build after `98b1248f`. Resolution, physical-core affinity, seven raster workers,
dummy audio, swap interval, warm-up and 600 measured presents were unchanged.
The GL device was again RTX 3090. No compiler, debugger or sampling profiler ran
alongside the timing cases. Native built-in sessions were measured; the separate
external-module continuation change is not an Original-module speed claim.

| Game / output | Before median / p99 (ms) | Current median / p99 (ms) | Current mean scene / render (ms) |
| --- | ---: | ---: | ---: |
| Q3 CPU 640×400 | 15.104 / 17.236 | 13.477 / 16.824 | 1.572 / 11.476 |
| Q3 CPU 320×200 | 7.401 / 10.059 | 6.653 / 7.972 | 1.433 / 4.753 |
| Q2 rerelease CPU 640×400 | 7.918 / 11.347 | 7.226 / 10.677 | 0.830 / 5.567 |
| Q2 rerelease CPU 320×200 | 5.089 / 8.420 | 4.975 / 7.019 | 0.769 / 3.527 |
| Q3 GL 1920×1080 | 2.532 / 2.949 | 2.536 / 3.085 | 1.423 / 0.735 |

All five cases passed actual map, drawable, installed-artifact and normal-quit
checks. Receipt: `suite-0a327175.json`. CPU medians improved, but every case in
this table still misses its requested frame budget. The real-time simulation
limit above still applies; these are comparable workloads, not identical frozen
game-state replays. Q3 GL provides a control for the CPU-only sampling change.

A separate six-second Q3 CPU 640×400 profile ran after 261 warm presents and
finished with both public timer reports and a normal quit. Its 5K userspace
samples had no lost samples. Sample-level texture work remained the largest
self cost at 21.53% across all threads; fragment writes were 11.74%, and the
per-fragment rounding-mode query was 5.29%. Moving that query out of the pixel
loop and batching texture work are the next CPU targets. Sampling percentages
are diagnostic and are not frame-time percentiles. Evidence:
`0a327175-current-q3-wayland-cpu640-l08r9c8i/result.json`.

## Installed `8ba209ad`: occupied actors and Q3 settings reuse

All twelve cases repeated the same quiet native stationary-spawn workload,
affinity, seven CPU workers, enabled dummy audio, RTX 3090 GL swap interval
zero and 600 measured presents. Warm-up and inclusive timer-report limits
above still apply. Complete GCC and Clang builds and their registered checks
passed before installation. No debugger, profiler, compiler or other game
test ran alongside these measurements.

| Game | Output | Median frame (ms) | p99 frame (ms) | Mean scene build (ms) | Mean render (ms) |
| --- | --- | ---: | ---: | ---: | ---: |
| Q1 classic | GL 1920×1080 | 0.877 | 1.607 | 0.446 | 0.311 |
| Q1 classic | CPU 640×400 | 4.150 | 6.367 | 0.547 | 3.133 |
| Q1 classic | CPU 320×200 | 2.154 | 3.257 | 0.499 | 1.440 |
| Q2 classic | GL 1920×1080 | 1.085 | 1.983 | 0.474 | 0.399 |
| Q2 classic | CPU 640×400 | 4.219 | 5.719 | 0.537 | 3.166 |
| Q2 classic | CPU 320×200 | 2.963 | 4.308 | 0.589 | 2.038 |
| Q2 rerelease | GL 1920×1080 | 1.336 | 2.718 | 0.624 | 0.481 |
| Q2 rerelease | CPU 640×400 | 6.702 | 9.479 | 0.752 | 5.430 |
| Q2 rerelease | CPU 320×200 | 4.441 | 6.420 | 0.680 | 3.415 |
| Q3 | GL 1920×1080 | 1.944 | 2.341 | 1.088 | 0.692 |
| Q3 | CPU 640×400 | 13.467 | 15.632 | 1.279 | 11.259 |
| Q3 | CPU 320×200 | 6.074 | 7.574 | 1.166 | 4.677 |

Against the preceding `0a327175` five-case suite, Q3 GL median/p99 fell from
2.536/3.085 to 1.944/2.341 ms. Its mean application scope fell from 0.240 to
0.067 ms and scene build from 1.423 to 1.088 ms. Q3 CPU 640×400 median is
essentially unchanged, 13.477 to 13.467 ms; moving the rounding query alone
does not solve its rendering cost. CPU 320×200 fell from 6.653/7.972 to
6.074/7.574 ms. Q2 rerelease CPU fell from 7.226/10.677 to 6.702/9.479 ms
at 640×400 and from 4.975/7.019 to 4.441/6.420 ms at 320×200.

These are measurements of the combined source group, not isolated gains for
each commit. Simulation remains real-time. Q1 and classic Q2 GL meet the 2 ms
target at median and p99; Q2 rerelease and Q3 meet it only at median. Every
CPU case remains over budget. Q3 CPU rendering is still the largest measured
cost, so the next change replaces double pixel attributes/depth with native
float/fixed data and processes adjacent pixels with SIMD in the existing
shared renderer. All runs reached the actual requested map and drawable,
quit normally and preserved the installed artifact. Receipt:
`suite-8ba209ad.json`.

## Installed `d01326c6`: native pixel precision

The twelve cases repeated the same native stationary spawns, resolutions,
physical-core affinity, seven CPU workers, enabled dummy audio and 600 measured
presents. No compiler, debugger or other game check overlapped. The actual GL
device remained RTX 3090 with swap interval zero. A one-second process monitor
identified browser CPU activity during five cases; these are marked below and
are not quiet speed qualifications. The other seven passed that monitor. Short
activity between samples is not excluded. Warm-up and real-time simulation
limits above still apply.

| Game | Output | Median frame (ms) | p99 frame (ms) | Mean scene / render (ms) | Quiet monitor |
| --- | --- | ---: | ---: | ---: | --- |
| Q1 classic | GL 1920×1080 | 0.886 | 1.789 | 0.456 / 0.316 | Browser activity |
| Q1 classic | CPU 640×400 | 4.372 | 7.062 | 0.502 / 3.482 | Yes |
| Q1 classic | CPU 320×200 | 2.255 | 3.404 | 0.469 / 1.623 | Yes |
| Q2 classic | GL 1920×1080 | 1.102 | 2.058 | 0.484 / 0.404 | Yes |
| Q2 classic | CPU 640×400 | 4.857 | 6.433 | 0.548 / 3.835 | Yes |
| Q2 classic | CPU 320×200 | 3.058 | 4.153 | 0.510 / 2.289 | Yes |
| Q2 rerelease | GL 1920×1080 | 1.329 | 2.761 | 0.624 / 0.487 | Yes |
| Q2 rerelease | CPU 640×400 | 7.017 | 9.366 | 0.736 / 5.742 | Browser activity |
| Q2 rerelease | CPU 320×200 | 4.753 | 6.817 | 0.673 / 3.741 | Browser activity |
| Q3 | GL 1920×1080 | 1.943 | 2.318 | 1.086 / 0.700 | Browser activity |
| Q3 | CPU 640×400 | 12.914 | 15.756 | 1.219 / 11.217 | Yes |
| Q3 | CPU 320×200 | 6.211 | 7.141 | 1.138 / 4.963 | Browser activity |

The quiet Q3 CPU 640×400 render scope is essentially unchanged from `8ba209ad`,
11.259 to 11.217 ms. Its median/p99 is 12.914/15.756 ms versus
13.467/15.632 ms. Quiet Q1 CPU 640×400 worsened from 4.150/6.367 to
4.372/7.062 ms, and Q2 classic from 4.219/5.719 to 4.857/6.433 ms.
Native precision and ordinary four-pixel dispatch have not delivered the
required CPU budget. All CPU cases remain above their targets; this group
is not claimed as a renderer speedup. All twelve reached their actual map
and drawable, quit zero and retained the installed artifact. Receipts:
`suite-d01326c6.json`, `d01326c6-run-index.json`.

A separate six-second userspace profile of the installed Q3 CPU 640×400
case completed with both public timer reports and a normal quit. Raster
thread self samples put `sample_levels` at 36.78%, `cpu_sample_texture` at
12.04%, `fragment_row` at 15.31% and `cpu_write_fragment` at 10.42%. The
actual generic row still unpacks each lane and samples it separately, which
bypasses batched texture work for draws needing blending, gamma or lighting.
The next shared shader change batches their unrelated texture reads while
retaining per-pixel state/store order and framebuffer feedback. These sample
percentages diagnose cost; they are not timing percentiles. Evidence:
`d01326c6-current-q3-wayland-cpu640-dvh_1fcn/result.json`.

## Installed `10b63336`: shared generic texture packets

All twelve stationary native cases reached their requested retail map and
drawable, measured 600 presents after warm-up and quit zero. Both full compiler
builds and their registered checks passed. Affinity, seven CPU workers, enabled
dummy audio, RTX 3090 GL swap interval zero and real-time simulation match the
preceding suite. No compiler, debugger, profiler or other Anthology live check
overlapped. The whole-host monitor flagged activity in every case: six had
only the other project on disjoint physical cores; the remaining six also had
browser, desktop-agent or application activity overlapping this affinity.
None qualifies the strict whole-host quiet requirement; the numbers below are
observations, not a claimed qualified speedup. One-second monitoring cannot
exclude shorter interference.

| Game | Output | Median frame (ms) | p99 frame (ms) | Mean scene / render (ms) | Overlapping CPU activity |
| --- | --- | ---: | ---: | ---: | --- |
| Q1 classic | GL 1920×1080 | 0.960 | 1.822 | 0.511 / 0.329 | Yes |
| Q1 classic | CPU 640×400 | 4.034 | 7.170 | 0.498 / 3.145 | None sampled; disjoint project active |
| Q1 classic | CPU 320×200 | 2.169 | 3.185 | 0.482 / 1.500 | None sampled; disjoint project active |
| Q2 classic | GL 1920×1080 | 1.137 | 2.072 | 0.500 / 0.412 | None sampled; disjoint project active |
| Q2 classic | CPU 640×400 | 4.361 | 5.663 | 0.538 / 3.348 | None sampled; disjoint project active |
| Q2 classic | CPU 320×200 | 2.951 | 3.940 | 0.527 / 2.108 | Yes |
| Q2 rerelease | GL 1920×1080 | 1.404 | 2.628 | 0.653 / 0.507 | None sampled; disjoint project active |
| Q2 rerelease | CPU 640×400 | 6.843 | 10.041 | 0.742 / 5.701 | Yes |
| Q2 rerelease | CPU 320×200 | 4.623 | 6.914 | 0.692 / 3.587 | Yes |
| Q3 | GL 1920×1080 | 1.964 | 2.363 | 1.096 / 0.703 | Yes |
| Q3 | CPU 640×400 | 12.513 | 14.420 | 1.239 / 10.644 | None sampled; disjoint project active |
| Q3 | CPU 320×200 | 5.999 | 6.758 | 1.159 / 4.729 | Yes |

Q3 CPU 640×400 remains the largest case at 12.513/14.420 ms median/p99
with 10.644 ms mean render time. The preceding suite recorded
12.914/15.756 ms and 11.217 ms render time. This modest difference does not
meet the 4 ms target and the background condition differs. Every CPU case
remains over budget. GL medians remain below 2 ms, but only Q1 has a p99
below 2 ms in this suite. No all-game tail-latency success is claimed.
Receipts: `suite-10b63336.json`, `10b63336-run-index.json` and
`10b63336-background-observations.json`.
