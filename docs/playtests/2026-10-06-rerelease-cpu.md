# Q2 rerelease CPU rendering, 2026-10-06

The installed `acf06fa7` executable still misses both CPU frame-time targets.
The preceding `41c128d0` build and this build ran retail `base1` from the
stationary authored spawn, with swap interval zero and dummy audio delivery.
Each case excluded 119 presented warm-up frames and measured the next 600.
All game threads used physical cores 0–7 through affinity `0-7,12-19`, with
seven raster workers. No debugger or sampling profiler was attached to these
timing runs. The executable remained unchanged throughout each run.

| Build | Drawable | Median frame (ms) | p99 frame (ms) | Mean CPU render (ms) |
| --- | --- | ---: | ---: | ---: |
| `41c128d0` | 640×400 | 11.209 | 37.976 | 8.946 |
| `acf06fa7` | 640×400 | 11.077 | 40.083 | 8.279 |
| `41c128d0` | 320×200 | 6.279 | 10.208 | 4.710 |
| `acf06fa7` | 320×200 | 5.711 | 9.034 | 4.203 |

The 320×200 case improved, but remains above the 2 ms target. At 640×400 the
median barely changed and the tail became worse; the 4 ms target remains open.
Wall-clock frames include simulation, scene construction, rendering,
presentation and pacing. CPU render means are differences between the two
inclusive `timers report` tables, not per-frame percentiles. The simulation
continues in real time, so these runs are not identical frozen-state replays.

A separate six-second userspace profile on installed `acf06fa7` located the
remaining work. It is diagnostic and is excluded from the timing table.
Of raster-thread samples, packed fog exponentiation accounted for 15.51%,
depth fog rows 8.07%, triangle depth interpolation 7.22%, cached span shading
6.14% and mip sampling 5.34%. The largest main-thread entry was viewport clear
at 4.21% of that thread's samples. These percentages have separate thread
denominators and must not be added together.

Across the profile's 1,200 warmed frames, the stable-plane change reduced
planarity fallback to four world candidates per frame. The span path wrote
303,806,400 pixels and generic triangles wrote 70,482,363. There were two
raster queue dispatches per frame. The remaining bottleneck is no longer the
earlier repeated MD5 pose construction.

All four timing cases reached the requested drawable and `base1`, exited zero,
and retained the receipt-qualified installed artifact. Private run receipts:
`q2-rerelease-baseq2-cpu-cd24u_5b/result.json`,
`q2-rerelease-baseq2-cpu-zrl09at5/result.json`,
`q2-rerelease-baseq2-cpu-0v58dhur/result.json`, and
`q2-rerelease-baseq2-cpu-5hwpc8di/result.json` under the qualified timing folder.
The diagnostic receipt is `acf06fa7-current-rr-cpu640-d9gz_azs/result.json`.
