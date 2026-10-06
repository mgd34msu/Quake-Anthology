# Q2 rerelease CPU rendering, 2026-10-06

The installed `ff437555` executable still misses both CPU frame-time targets.
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
| `ff437555` | 640×400 | 7.918 | 11.347 | 5.797 |
| `41c128d0` | 320×200 | 6.279 | 10.208 | 4.710 |
| `acf06fa7` | 320×200 | 5.711 | 9.034 | 4.203 |
| `694ccc53` | 320×200 | 5.809 | 9.210 | 3.992 |
| `ff437555` | 320×200 | 5.089 | 8.420 | 3.655 |

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

## Installed `ff437555`: shared contact and raster changes

The next group reuses world-contact snapshots, runs ordinary native instructions
in hardware, rejects constant-depth covered blocks before fragment math, and
skips fog operations that cannot change rounded RGBA. These changes preserve
existing fallbacks, texture filtering, depth tests and alpha behavior. They
were installed after complete GCC/Clang builds and their registered checks.

Against `694ccc53`, the measured median fell 4.5% at 640×400 and 12.4% at
320×200. Both targets remain open. Dynamic simulation and the combined source
group prevent attribution of the full-frame difference to a single change.
Receipts: `q2-rerelease-baseq2-cpu-e22y8im1/result.json` and
`q2-rerelease-baseq2-cpu-02zjqdhs/result.json`.

A separate successful six-second userspace sample ranked span shading at
10.96%, depth fog rows at 10.65%, mip sampling at 7.97%, packed fog
exponentiation at 6.32%, generic fragment writing at 6.10%, and texture sampling
at 5.82%. Percentages use the total sampled userspace CPU across all threads;
they are not percentages of wall frame time. This diagnostic is excluded from
the timing table. Receipt: `ff437555-current-rr-cpu640-z0lq7kvi/result.json`.

## Installed `0a327175`: shared nearest-mip sampling

Quiet 600-present measurements after warm-up used the same stationary `base1`
spawn, physical-core affinity, seven raster workers and enabled dummy audio.
The CPU 640×400 median/p99 changed from 7.918/11.347 to 7.226/10.677 ms;
320×200 changed from 5.089/8.420 to 4.975/7.019 ms. Both requested budgets
remain open. The scene is simulated in real time, so this is not an identical
frozen-state comparison. Receipts: `q2-rerelease-baseq2-cpu-8xdftt7s/result.json`
and `q2-rerelease-baseq2-cpu-06rnqpun/result.json`.

Separate installed-build spawn images at both sizes retained lit metal walls,
floor, pickup models and the translucent machinery window. Those bounded
image checks do not establish whole-map parity. The small black beam cap was
also present in the earlier accepted image. Evidence:
`owner-jump-save-visibility-brush-0a327175-proof.json`.

The external Original rerelease DLL remains unqualified. A separate no-debugger
startup check reached its initialized save registry after 14.778 seconds and
progressed to later native callbacks, but had no public frames by the 25-second
cutoff. The final cleanup log contained an unsupported canonical-service import
error; its offending import and pre-cleanup timing were not captured, so it is
not assigned as the startup cause. All owned processes and the private display
were removed. This is not an Original-module startup or speedup pass. Evidence:
`original-rr-after-continuation-jku4z47t/summary.json`.

## Installed `8ba209ad`: actual Original startup failure

The no-debugger retry identified the missing import before cleanup:
`Bot_UnRegisterEdict`, GAME slot 49. The application reported it at 26.128
seconds and exited with status 1 at 26.229 seconds, before the 40-second
startup bound. The DLL's save registry initialized after 14.375 seconds, but
no public frame was reached. This is an Original-module startup failure,
not a performance result or a failure caused by the timed cleanup.

All owned processes and the private display were removed. Receipt:
`original-rr-import-boundary-9uqtcidf/summary.json`. The actual registration
services and their shared bot-observation integration remain to be implemented.
