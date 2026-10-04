# Native frame costs

The latest measured Q3 CPU frame takes **24.309 ms**, including **16.645 ms**
of raster execution; latest interval-1 GL takes **17.123 ms**. Removing a quadratic settings
lookup cuts shared scene work. Replacing duplicate SDL texture layers reduces
CPU presentation to about **0.3 ms**. Raster execution is now the largest CPU cost.

## Workload

These measurements use native Q3 `q3dm1`, 640×480, seed 2246822519,
camera position `(212, 2360, 56.125)` and angles `(0, -45, 0)`.
Simulation advances by exactly 50 ms per frame. Each run warms up for 30
frames, then records 30 individual complete frames without attack input.
The machine has a Ryzen 9 5900X, 24 logical CPUs, and an NVIDIA RTX 5060 Ti
using driver 610.57.04. GL reports the NVIDIA hardware renderer; its actual
SDL swap interval is 1 in the controls. A separate run explicitly selects 0.

Wall time surrounds the actual frontend step. Calling-thread and aggregate
process CPU clocks cover the same interval. Frame reports, GPU query results
and final pixel capture are collected outside that interval. Builds use
optimized production GCC and Clang targets with warnings treated as errors.

## Complete frame results

Times are medians in milliseconds. Rows from `2b5e343c` use identical timed
diagnostic code, including material scopes. Earlier rows use fewer scopes;
their comparisons include the later diagnostic overhead.

| Commit | CPU frame | CPU raster | CPU process time | GL frame |
| --- | ---: | ---: | ---: | ---: |
| `af139299` | 202.948 | 174.854 | Not recorded | 30.755 |
| `3a1b201b` | 182.777 | 153.645 | Not recorded | 31.014 |
| `20d077b4` | 184.117 | 154.652 | 183.060 | 31.314 |
| `094a0d3f` | 137.570 | 110.106 | 180.096 | Not run |
| `8521761b` | 114.061 | 85.384 | 144.310 | 29.512 |
| `2b5e343c` | 108.890 | 78.267 | 154.229 | 30.074 |
| `b91eb2eb` | 89.707 | 59.141 | 179.565 | 29.074 |
| `1a132170` | 90.725 | 59.263 | 179.188 | 31.228 |
| `69dd5649` | 79.971 | 49.511 | 152.860 | Not run |
| `f143350a` | 78.613 | 48.876 | 150.490 | 28.836 |

A later subdivision uses seven additional scopes. These runs share identical
timed code; display and draw metadata are collected after measurement. Their
material totals include more observer overhead than the standard runs above.

| Commit | CPU frame | CPU raster | CPU process time |
| --- | ---: | ---: | ---: |
| `f143350a`, subdivision | 81.310 | 49.820 | 153.838 |
| `84a881d9` | 79.914 | 48.660 | 150.202 |
| `428ec046` | 49.500 | 18.392 | 183.634 |
| `e900075f` | 50.046 | 18.470 | 183.143 |
| `d164f40c` | 46.575 | 17.489 | 176.752 |
| `436272c5` | 51.426 | 19.668 | 183.142 |
| `70dd4397` | 33.838 | 16.922 | 154.816 |
| `60336d6b` | 34.343 | 17.446 | 150.564 |
| `e22f4a54` | 27.269 | 18.167 | 147.397 |
| `f630ea7b` | 26.875 | 17.766 | 146.278 |
| `86d1f51c` | 25.855 | 16.790 | 153.053 |
| `008d0701` | 25.993 | 16.706 | 153.770 |
| `fc471593` | 25.669 | 16.638 | 151.515 |
| `8773a8f3` | 24.680 | 16.624 | 150.164 |
| `7a078e87` | 25.210 | 16.844 | 152.449 |
| `086dea5a` | 24.309 | 16.645 | 151.845 |

With the same subdivision, GL takes 31.411 ms on `e900075f` and 30.501 ms
on `436272c5`. Material submission falls from 13.104 to 12.399 ms on CPU
and from 13.302 to 12.395 ms on GL. CPU raster and presentation costs rise
in the latest run, so these material cuts do not establish a whole-frame CPU
gain. SDL RenderPresent varies from 8.430 to 10.376 ms across these controls.

`70dd4397` removes the shared settings lookup's quadratic traversal and includes
the unit-color raster cut. CPU frame time falls 34.2% from `436272c5`; GL falls
33.0% to 20.446 ms. Material submission drops to 4.234 ms CPU and 4.600 ms GL.
Aggregate CPU work falls to 154.816 ms. Raster and presentation also vary between
runs, so the full frame reduction cannot be attributed to the lookup alone.

Balancing fixed bands by bounding-box work on `60336d6b` establishes no latency
gain: frame and raster medians rise slightly, and frame P95 rises from 36.428
to 39.349 ms. `86d1f51c` replaces that partition with smaller contiguous row jobs
claimed dynamically by the existing workers and caller.

`e22f4a54` replaces the outer SDL software renderer and streaming texture with
one retained RGBA surface and native window-surface presentation. On the actual
X11 run, presentation falls from 8.515 to 0.281 ms, complete frame time falls
20.6%, and frame P95 falls to 28.931 ms. Raster time varies upward; the large
reduction is in the presentation path. Only after-loop diagnostic metadata
changes to report the actual native surface; removing that branch reproduces
the previous diagnostic source exactly.

`f630ea7b` passes each fragment's actual depth/stencil admission to the existing
writer, avoiding a repeated test. Its matched frame median falls from 27.269 to
26.875 ms and raster from 18.167 to 17.766 ms: a modest improvement.

Dynamic scheduling on `86d1f51c` then reduces frame latency by 3.8% to 25.855 ms
and raster by 5.5% to 16.790 ms. Frame P95 falls from 27.832 to 26.473 ms.
Aggregate process CPU rises from 146.278 to 153.053 ms, so this gain trades more
CPU work for lower elapsed time. All 30 recorded states/counts, final engine and
retained display RGBA, and actual native RGB match both preceding controls.
The timed diagnostic source is unchanged.

Reading prepared triangle attributes directly on `008d0701` removes stack
copies and shrinks generated code, but establishes no frame latency gain.
Sharing bilinear coordinate wrapping on `fc471593` reduces the next frame
median from 25.993 to 25.669 ms; raster changes only from 16.706 to 16.638 ms.
These are small changes on the shared machine, not a substantial speedup.
Both preserve all 30 states/counts, engine/display RGBA and native RGB.

On the same `86d1f51c` artifact, GL takes 19.015 ms with actual swap interval 1
and 20.602 ms with actual interval 0. Swap itself takes 8.560 and 8.700 ms;
disabling synchronization establishes no speed gain. GPU render elapsed is
2.947 ms in both cases, separate from the complete CPU-clocked frame. All 30
states/counts and final GL pixels match each other and the previous GL control.
The driver is unchanged; the uncapped case uses the existing
`+set r_swapInterval 0` startup setting. An unrelated user game stayed active
through both runs and consumed about one CPU core. These are shared-machine
measurements with project build/runtime jobs excluded, not uncontended timings.

Indexing names within the canonical cvar owner on `8773a8f3` reduces CPU
material submission from 4.328 to 3.734 ms and scene construction from 6.019
to 5.184 ms. The complete CPU frame falls from 25.669 to 24.680 ms, while
raster execution stays at 16.624 ms. GL material work falls from 4.268 to
3.800 ms, but its complete frame rises from 19.015 to 19.753 ms: no whole-frame
GL gain is established. All 30 states/counts and same-backend engine pixels
match; CPU retained RGBA and native RGB also match.

`086dea5a` combines unchanged-plane clipping-copy removal, direct lookup in
the existing sorted material table, reuse of the archive name index, and
duplicate GL state-call removal. The matched CPU frame measures 24.309 ms
and GL 17.123 ms; all 30 state/count rows and final same-backend images match.
The unrelated user game active during `7a078e87` is absent in this pair, so
the frame difference is not isolated from changed background load. Startup
and peak RSS stay near the preceding lazy-archive result.

A separate coarse outer-flush scope on `7a078e87` measures 17.336 ms CPU
execution: 8.612 ms in external flushes and 8.637 ms median per-frame
execution minus flush. Flush includes scheduling, pixel workers, joins and
exception reconciliation; the remainder includes preparation, clears,
bookkeeping and unwrapped immediate paths. Its added clocks make it an
attribution run rather than a matched control. The identical observer on
`086dea5a` produces anomalous 50.767 ms complete frames and 35.849 ms CPU
execution. It establishes no preparation-cost gain; its cause is unverified.

The subsequent Source material self-copy/planning removal (`3e274a1e`) and
prepared-geometry projection reuse (`5bd5ff30`) pass all default optimized GCC
and Clang build targets. Their bounded old/new fixtures preserve outputs,
errors and floating-point flags. Matched whole-frame benefit is still pending.
Both proposed Q2 axial endpoint shortcuts made complete geometry queries
slower on GCC and Clang and were withdrawn.

Ordered command batching reduces matched frame time by 38.1% and raster time
by 62.2%. Aggregate CPU work rises: the gain comes from parallel scheduling,
not less total CPU work. The batch preserves command order within disjoint
row bands and uses the existing raster kernel.

On `b91eb2eb`, the nearest-rank 95th percentile is 91.594 ms CPU and
29.934 ms GL. The wider pool lowers elapsed time while increasing aggregate
CPU work. A process snapshot confirms 23 CPU raster workers plus the caller;
it does not establish each worker's individual contribution.

## Loading and peak memory

Lazy archive payload acquisition on `7a078e87` preserves the existing parser,
retained directory identity, member sharing and saved layout. The same workload
reaches its readiness marker at 19.52 seconds instead of 35.53 seconds on both
backends. These are launch-to-marker observations with 250 ms polling, not
individual internal startup-stage clocks or a cold-cache benchmark.

| Backend | Previous peak RSS, GiB | Current peak RSS, GiB | Previous run, s | Current run, s |
| --- | ---: | ---: | ---: | ---: |
| CPU | 5.913 | 2.639 | 38.536 | 23.027 |
| GL | 5.981 | 2.706 | 37.539 | 21.521 |

Peak RSS covers the entire child process, including startup. It is not a
steady-state memory measurement. All 30 gameplay/count records and final
engine pixels match the preceding same-backend control; CPU retained RGBA
and native RGB also match. Complete-frame medians are 25.210 ms CPU and
18.101 ms GL. This establishes lower loading cost and peak memory, while
the CPU frame shows no latency improvement.

## Current costs and optimization targets

These are inclusive medians on `086dea5a` CPU and interval-1 GL.
Both include the subdivision scopes.
Nested durations overlap, so the rows must not be added together.

| Scope | CPU ms | GL ms |
| --- | ---: | ---: |
| Scene construction | 5.122 | 4.863 |
| Material submission, 554 calls | 3.723 | 3.596 |
| World submission, including its materials | 3.664 | 3.515 |
| Renderer execution / GL submission | 16.645 | 1.787 |
| CPU native presentation / GL swap | 0.237 | 8.217 |

The first GL baseline separately measured 3.596 ms median GPU elapsed time
around renderer execution. GPU intervals overlap CPU work and presentation;
they are not additional complete-frame milliseconds.

Reducing Source vertex storage, adding canonical texel lookup tables and
sharing triangle preparation produced no complete-frame improvement on
`1a132170`. The final frame contains no Source vertex arrays, so full Source
array copies were not a cause of this native workload's cost. The lookup
tables and triangle preparation also have non-Source callers.

The guarded opaque fragment path on `69dd5649` reduces both elapsed raster
time and aggregate CPU work. Preparing material constants once per stage and
skipping unused texture derivatives on `f143350a` preserve output; their
standard matched run does not establish a material-stage speedup.

The earlier subdivision attributes about 0.63 ms to mesh deformation and
0.81 ms to frame draw assembly. Its generic image-variant helper is not
reached, but a later attribution run observes about 1,040 calls per frame
through the actual Source image path. Image upload validation runs about
1,507 times. Those scopes nest and their extra clocks add about 7 ms to
material timing; that diagnostic run cannot serve as a speed comparison.
Gamma remains 1, so exponentiation is not reached. The validated cuts remove
unused color table construction, repeated admitted intensity scans and
display queries that only need backend/fullscreen state.

Source image handling also calls the shared renderer-settings lookup about
1,040 times per frame. Its old ordinal iteration rescans the linked registry
for each row. `70dd4397` replaces that quadratic traversal with the existing
linear lookup while preserving physical rows and alias exclusion. `2433048c`
reuses the raster kernel's existing weighted sum for exact unit-color
triangles. Both pass optimized GCC/Clang builds and focused old/new production
comparisons. Their matched pair preserves all states and pixels while reducing
the shared scene cost from about 17 ms to 6 ms.

`8773a8f3` indexes names inside the existing canonical cvar value owner,
covering live rows, aliases, prepared edits and restored values. The ordered
enumeration and saved field order stay unchanged. Actual old/new production
comparisons pass on GCC and Clang across 106,157 lookups, 77 lifecycle phases,
6,103 mutations and 5,878,229 identical saved bytes. An isolated lookup with
about 1,200 rows falls from roughly 3.5–3.8 microseconds to 13–21 nanoseconds;
the integrated frame comparison is pending.

Selecting SDL's default accelerated blitter did not materially improve
presentation and changed readback alpha during genuine saved-image
restoration. `e900075f` restores the original software blitter. Every later
completed CPU pair retains exact display RGBA as well as engine RGBA.
The native-surface path preserves the same authored RGBA in its single retained
frame. Its actual RGB888 native target has no alpha channel; all native RGB
components match, while retained capture also preserves the authored alpha.

## Fidelity and limits

Each completed iteration preserves all 30 recorded gameplay states, actor
identities, source clocks and draw/index counts against its renderer's
baseline. Final RGBA bytes also match exactly within each renderer:

- CPU: `e8dbb9cd341bfc270ce00104905454fdfd717df440721207db4170ba3f0b56e7`
- GL: `46c7ef0a833be8f00e91b4f0ea96f91cf5d132cbe407486748e63724d21dd2a6`

CPU and GL images differ. These checks establish preservation of this fixed
workload, not cross-renderer parity, campaign coverage or general playability.
Peak RSS is a whole-run measurement: the bounded archive comparison reduces
the CPU run from 7,623,504 KiB on `8521761b` to 6,260,920 KiB on `2b5e343c`.
It does not measure steady-frame memory use or isolate every allocation.
