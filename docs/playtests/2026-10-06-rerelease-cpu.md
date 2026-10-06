# Q2 rerelease CPU rendering, 2026-10-06

The installed `694ccc53` executable still misses both CPU frame-time targets.
Builds `41c128d0`, `acf06fa7` and `694ccc53` ran retail `base1` from the
stationary authored spawn, with swap interval zero and dummy audio delivery.
Each case excluded 119 presented warm-up frames and measured the next 600.
All game threads used physical cores 0–7 through affinity `0-7,12-19`, with
seven raster workers. No debugger or sampling profiler was attached to these
timing runs. The executable remained unchanged throughout each run.

| Build | Drawable | Median frame (ms) | p99 frame (ms) | Mean CPU render (ms) |
| --- | --- | ---: | ---: | ---: |
| `41c128d0` | 640×400 | 11.209 | 37.976 | 8.946 |
| `acf06fa7` | 640×400 | 11.077 | 40.083 | 8.279 |
| `694ccc53` | 640×400 | 8.292 | 11.785 | 6.281 |
| `41c128d0` | 320×200 | 6.279 | 10.208 | 4.710 |
| `acf06fa7` | 320×200 | 5.711 | 9.034 | 4.203 |
| `694ccc53` | 320×200 | 5.809 | 9.210 | 3.992 |

The latest group defers triangle attribute math until depth admission, fills
cleared viewports in bulk, and evaluates ordinary fog attenuation with native
SIMD arithmetic. At 640×400 the median fell by 25.1% from `acf06fa7`, and p99
fell from 40.083 ms to 11.785 ms. The 320×200 full frame did not improve, despite
its lower mean rendering time. Neither case meets its target, 4 ms at 640×400
and 2 ms at 320×200. The group also contains Q1 state-admission and original
module startup changes; this comparison does not isolate each rendering edit.
Wall-clock frames include simulation, scene construction, rendering,
presentation and pacing. CPU render means are differences between the two
inclusive `timers report` tables, not per-frame percentiles. The simulation
continues in real time, so these runs are not identical frozen-state replays.

A separate six-second userspace profile on installed `acf06fa7` located the
remaining work. It is diagnostic and is excluded from the timing table.
Raster-thread entries accounted for these shares of all sampled userspace CPU
time: packed fog exponentiation 15.51%, depth fog rows 8.07%, triangle depth
interpolation 7.22%, cached span shading 6.14% and mip sampling 5.34%. The
largest main-thread entry was viewport clear, at 4.21% of all sampled CPU time
or 12.12% when the report is normalized to that thread. The original reports
use perf's absolute percentages despite filtering by thread; the denominator
is shared across the main and raster reports.

Across the profile's 1,200 warmed frames, the stable-plane change reduced
planarity fallback to four world candidates per frame. The span path wrote
303,806,400 pixels and generic triangles wrote 70,482,363. There were two
raster queue dispatches per frame. The remaining bottleneck is no longer the
earlier repeated MD5 pose construction.

All six timing cases reached the requested drawable and `base1`, exited zero,
and retained the receipt-qualified installed artifact. Private run receipts:
`q2-rerelease-baseq2-cpu-cd24u_5b/result.json`,
`q2-rerelease-baseq2-cpu-zrl09at5/result.json`,
`q2-rerelease-baseq2-cpu-0v58dhur/result.json`, and
`q2-rerelease-baseq2-cpu-5hwpc8di/result.json`,
`q2-rerelease-baseq2-cpu-n0ajw24_/result.json`, and
`q2-rerelease-baseq2-cpu-noq3o337/result.json` under the qualified timing folder.
The diagnostic receipt is `acf06fa7-current-rr-cpu640-d9gz_azs/result.json`.
