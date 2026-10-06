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
