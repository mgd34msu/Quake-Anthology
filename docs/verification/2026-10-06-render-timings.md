# Retail render measurements, 2026-10-06

These results do not meet the performance targets. Outside activity prevents an
isolated speedup claim for the latest group.

The latest measured executable was built from `733077cb`, including the prepared
four-pixel fog arithmetic, vertex clipping/projection reuse and exact world
visibility reuse. GCC and Clang full builds and their registered checks passed.
The installed testing executable matched the build byte for byte.

The twelve retail cases each exited normally with an unchanged artifact,
120 warm-up frames and 600 measured presentation intervals. GL used the RTX 3090
at swap interval zero. The starting views were Q1 `e1m1`, Q2 and Q2 rerelease
`base1`, and Q3 `q3dm1`. These are default native sessions with enabled dummy
audio delivery, real elapsed time, and no debugger or profiler attached.

| Game | Output | Resolution | Median, ms | p99, ms | Target, ms |
| --- | --- | ---: | ---: | ---: | ---: |
| Q1 | GL | 1920×1080 | 5.018 | 9.199 | <2 |
| Q1 | CPU | 640×400 | 10.837 | 13.359 | <4 |
| Q1 | CPU | 320×200 | 6.222 | 8.502 | <2 |
| Q2 | GL | 1920×1080 | 2.082 | 3.363 | <2 |
| Q2 | CPU | 640×400 | 9.069 | 12.745 | <4 |
| Q2 | CPU | 320×200 | 6.003 | 8.010 | <2 |
| Q2 rerelease | GL | 1920×1080 | 3.720 | 6.438 | <2 |
| Q2 rerelease | CPU | 640×400 | 18.858 | 48.951 | <4 |
| Q2 rerelease | CPU | 320×200 | 13.505 | 25.444 | <2 |
| Q3 | GL | 1920×1080 | 3.445 | 4.528 | <2 |
| Q3 | CPU | 640×400 | 17.737 | 20.674 | <4 |
| Q3 | CPU | 320×200 | 8.634 | 10.566 | <2 |

This project's other games, debuggers and compilers were stopped. One-second
process observations detected outside activity above half a CPU core in every
case. Another project's test used approximately 19–20 cores during the Q1 GL
run; that result does not establish a renderer regression. Nice 19 does not
make a host quiet, and the monitor cannot exclude short activity between samples.

Q2 rerelease CPU 640×400 recorded warmed inclusive means of 14.170 ms in
`cpu_render`, 2.824 ms in `scene_build`, and 1.593 ms in `application`. At
320×200, rendering remained 8.917 ms and scene building 2.681 ms. Q3 GL spent
2.342 ms building the scene and 0.671 ms submitting GL work. These are still
implementation targets, not achieved frame budgets. The lower CPU observations
measure a group of changes under different outside activity and simulation
progress; no isolated improvement is attributed to one commit.

## Earlier measurements

The earlier measured executable was built from `92acfd4a` with GCC, RelWithDebInfo and
warnings treated as errors. The complete GCC and Clang builds and their six
registered CTest checks each passed. The testing copy, `qa-c`, was compared byte
for byte with that build after installation.

The latest four-game group used each retail starting view: Q1 `e1m1`, Q2 and
Q2 rerelease `base1`, and Q3 `q3dm1`. Every run completed normally with the
installed artifact unchanged, 120 warm-up frames and 600 measured intervals.
GL used the RTX 3090 with swap interval zero. These are default native sessions.

| Game | Output | Resolution | Median, ms | p99, ms | Target, ms |
| --- | --- | ---: | ---: | ---: | ---: |
| Q1 | GL | 1920×1080 | 1.777 | 3.320 | <2 |
| Q1 | CPU | 640×400 | 11.091 | 13.888 | <4 |
| Q1 | CPU | 320×200 | 6.652 | 8.309 | <2 |
| Q2 | GL | 1920×1080 | 1.959 | 3.350 | <2 |
| Q2 | CPU | 640×400 | 9.734 | 12.662 | <4 |
| Q2 | CPU | 320×200 | 6.853 | 8.837 | <2 |
| Q2 rerelease | GL | 1920×1080 | 3.620 | 6.493 | <2 |
| Q2 rerelease | CPU | 640×400 | 21.760 | 50.777 | <4 |
| Q2 rerelease | CPU | 320×200 | 15.915 | 29.074 | <2 |
| Q3 | GL | 1920×1080 | 3.512 | 4.269 | <2 |
| Q3 | CPU | 640×400 | 18.584 | 21.222 | <4 |
| Q3 | CPU | 320×200 | 9.154 | 11.300 | <2 |

This project's other games, debuggers and compiler jobs were stopped for this
group. One-second process observations found no outside process exceeding half
a core in the Q1 GL, Q2 GL and Q2 CPU 640×400 runs. Outside activity was observed
in the other nine runs. Activity below the threshold or between samples is not
excluded. The results do not establish an isolated speedup from the prepared
triangle rows, image-update dependencies or exact point-light cache.

In this group Q2 rerelease GL spent an inclusive mean of 2.167 ms building the
scene and 0.863 ms submitting GL work. CPU 640×400 spent 17.221 ms rendering and
2.748 ms building the scene. A separate 199 Hz user-CPU profile of the same
installed artifact, including its renderer threads, attributed 26.86% of samples
to `depth_fog_rows` and 5.26% to `blend_fog`. That diagnostic run exited normally
after 900 frames; its sampled timings are not frame-time evidence.

The workload was stationary at the retail Q2 rerelease `base1` spawn. Each run
excluded 120 warm-up frames and measured 600 presentation intervals, using real
elapsed time, `timers on` and `timers report`, with no debugger or sampling
profiler attached. Vsync was disabled. Audio remained enabled with SDL dummy
delivery. GL ran on **NVIDIA GeForce RTX 3090**, driver 610.57.04. This host also
has an RTX 5060 Ti; these GL results are not measurements of that GPU.

| Build / renderer | Resolution | Median, ms | p99, ms | Target, ms |
| --- | --- | ---: | ---: | ---: |
| `b60ef5e3`, GL | 1920×1080 | 11.985 | 18.076 | <2 |
| `91cd1558`, GL | 1920×1080 | 10.055 | 13.037 | <2 |
| `d10d5a05`, GL | 1920×1080 | 7.880 | 11.334 | <2 |
| `c334bd85`, GL | 1920×1080 | 4.326 | 7.424 | <2 |
| `c8d26292`, GL | 1920×1080 | 3.666 | 6.414 | <2 |
| `b95ea767`, GL | 1920×1080 | 3.635 | 6.383 | <2 |
| `efe9e317`, GL | 1920×1080 | 3.920 | 6.841 | <2 |
| `b60ef5e3`, CPU | 640×400 | 47.744 | 60.916 | <4 |
| `d10d5a05`, CPU | 640×400 | 52.881 | 108.189 | <4 |
| `c334bd85`, CPU | 640×400 | 49.505 | 78.423 | <4 |
| `c8d26292`, CPU | 640×400 | 23.280 | 52.069 | <4 |
| `b95ea767`, CPU | 640×400 | 23.489 | 52.056 | <4 |
| `efe9e317`, CPU | 640×400 | 22.574 | 50.915 | <4 |
| `b60ef5e3`, CPU | 320×200 | 25.835 | 38.862 | <2 |
| `d10d5a05`, CPU | 320×200 | 26.561 | 39.388 | <2 |
| `c334bd85`, CPU | 320×200 | 24.420 | 37.030 | <2 |
| `c8d26292`, CPU | 320×200 | 16.659 | 28.633 | <2 |
| `b95ea767`, CPU | 320×200 | 16.955 | 29.040 | <2 |
| `efe9e317`, CPU | 320×200 | 16.004 | 28.694 | <2 |

The `d10d5a05` runs waited for active compilers and debuggers to exit before
starting. A separate project's CPU-intensive test process was observed after
that preflight. These runs therefore need repetition during a fully quiet
interval, especially the CPU tail measurements. The recorded values are
observations, not qualified evidence of an isolated change's speedup.

The five `c334bd85` runs also waited for this project's compilers, debuggers and
other game instances to return. One-second process CPU observations detected
another project's Rust test at nice 19 during all five runs, including activity
of approximately one to five cores during the GL run. These measurements retain
that interference limit. Lower priority does not make the host fully quiet.
The monitor cannot exclude short activity between samples or below half a core.

The five `c8d26292` runs stopped this project's compilers, debuggers and other
game instances. The same process monitor found no outside process exceeding
half a core during the rerelease CPU 640×400 and Q1 CPU 320×200 runs. It recorded
browser activity of approximately one core during GL, rerelease CPU 320×200
and Q1 CPU 640×400. This is a bounded observation of process activity, not proof
of complete host isolation. All five runs completed normally, retained the
installed artifact and collected 600 post-warm-up intervals.

Warmed inclusive timer means from `d10d5a05`:

| Scope | GL 1080p, ms | CPU 640×400, ms | Required action |
| --- | ---: | ---: | --- |
| `gl_submit` | 2.537 | — | Combine lightmap stages and batch compatible resident world geometry. |
| `scene_build` | 3.382 | 4.340 | Remove repeated sampling, resource searches and command copies. |
| `cpu_render` | — | 52.209 | Check actual span coverage and remove repeated raster worker barriers. |
| `application` | 0.862 | 2.340 | Keep native clock/input behavior while reducing measured simulation work. |
| `window_present` | 0.040 | 0.470 | Preserve requested CPU framebuffer dimensions when the window changes. |

Warmed inclusive timer means from `c334bd85`:

| Scope | GL 1080p, ms | CPU 640×400, ms | CPU 320×200, ms |
| --- | ---: | ---: | ---: |
| `gl_submit` | 0.915 | — | — |
| `scene_build` | 2.752 | 3.290 | 3.147 |
| `cpu_render` | — | 44.247 | 19.194 |
| `application` | 0.585 | 1.862 | 1.707 |
| `window_present` | 0.035 | 0.386 | 0.123 |

The resident world batches and combined lightmap pass are included in this
build. Submission is now below 1 ms in the recorded GL workload; scene building
remains the largest GL frame scope. The total frame target is still unmet.

Warmed inclusive timer means from `c8d26292`:

| Scope | GL 1080p, ms | CPU 640×400, ms | CPU 320×200, ms |
| --- | ---: | ---: | ---: |
| `gl_submit` | 0.846 | — | — |
| `scene_build` | 2.259 | 2.968 | 2.865 |
| `cpu_render` | — | 18.560 | 11.950 |
| `application` | 0.485 | 1.797 | 1.353 |
| `window_present` | 0.036 | 0.368 | 0.124 |

This build combines the ordered raster queue, physical-core worker selection,
worker fog/gamma, fog-capable brush spans, exact MD5 bounds rejection and Source
clock/correctness changes. These results measure the group, not an isolated
change. CPU raster work and scene construction remain the next targets.

Q1 classic `e1m1`, with no fog, used the same artifact and measurement procedure:

| Build | Resolution | Median, ms | p99, ms | `cpu_render` mean, ms | `scene_build` mean, ms | Target, ms |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `c334bd85` | 640×400 | 14.927 | 17.824 | 12.341 | 1.134 | <4 |
| `c334bd85` | 320×200 | 7.704 | 9.746 | 6.042 | 0.993 | <2 |
| `c8d26292` | 640×400 | 11.479 | 14.290 | 9.328 | 1.094 | <4 |
| `efe9e317` | 640×400 | 11.152 | 13.511 | 9.269 | 0.951 | <4 |
| `c8d26292` | 320×200 | 6.881 | 8.404 | 5.326 | 0.949 | <2 |
| `efe9e317` | 320×200 | 6.997 | 9.072 | 5.431 | 0.901 | <2 |

Fog alone therefore does not account for the slow CPU path. A separate bounded
counter capture on a private, coherently linked `315c9708` Clang renderer found
354,197 span-covered pixels, 248,415 span writes, 456,358 triangle-covered pixels
and 130,918 triangle fragment calls in one Q2 rerelease frame. That capture used
640×480, not either resolution in the timing tables. Its 39 synchronous batches
posted and joined workers 172 times. It proves that rerelease world spans were
active; it is not a frame-time measurement or a shipped production counter.

A separate `c8d26292` check exercised the actual `timers` enable, disabled
retention and reset paths in all four retail presets. Its rerelease eight-frame
640×480 sample recorded 2,833,640 span-covered pixels, 1,987,320 span writes,
3,657,232 triangle-covered pixels, 1,050,983 triangle fragment calls and writes,
24 worker dispatches and 216 worker posts/joins. The 4,424 admitted brush draws
retain the earlier sample's 553 draws per frame. Both drawing paths remain
active, while the queue dispatches three times per frame. This counter run is
separate from the frame-time workloads and is not a matched-clock speedup test.

`gl_completion` has no calls after `91cd1558`. Both `gl_submit` and `scene_build`
must fall below 1 ms for the 2 ms GL goal. Timer scopes report inclusive means;
they are not GPU execution times or per-scope percentiles.

The measured default Q2 rerelease preset uses the compiled builtin provider.
`frontend_launch` selects the original module only when requested, and
`kind_for` maps the builtin rerelease clock to `APPLICATION_PROVIDER_Q2`.
Consequently the native child-process import path is not the source of these
default-preset timings. Its cost remains relevant to original-module sessions.

The builds include several changes: brush spans, recovery frame-path removal,
GL state caching, menu/UI fixes and restored entity effects. Real elapsed time
also changes simulation progress between runs. The table does not attribute
the entire difference to any one commit.

The `b95ea767` PVS-subtree change did not produce a convincing frame-time gain
on this stationary map. Its GL warmed `gl_submit` and `scene_build` means were
0.905 and 2.159 ms. CPU 640×400 recorded `cpu_render` 18.629 ms and `scene_build`
2.788 ms; CPU 320×200 recorded 12.058 and 2.731 ms. All three runs retained the
artifact, exited normally and measured 600 intervals. The process monitor
observed browser and assistant activity above half a core during these runs;
they do not establish a fully isolated performance change.

A separate 199 Hz user-CPU profile on actual `c8d26292` rerelease 640×400
attributed 25.92% of samples to depth fog rows and 13.88% to fog blending.
It captured all render workers and retained the authored world draws. This is
a sampling profile, separate from the unprofiled timing procedure. Commit
`a45cd440` replaces the hot fog math with native floats and prepared color
blending. Its shipped whole-frame and post-change observations are recorded below.

The `efe9e317` five-run group completed normally with unchanged installed
artifact and 600 intervals per run. This project's other games, debuggers and
compilers were stopped. One-second observations still found outside assistant,
browser or desktop-agent CPU activity above half a core during every run.
These limits prevent a fully isolated speedup claim. All targets remain unmet.

| Warmed scope | GL 1080p, ms | CPU 640×400, ms | CPU 320×200, ms |
| --- | ---: | ---: | ---: |
| `gl_submit` | 0.947 | — | — |
| `scene_build` | 2.283 | 2.849 | 2.578 |
| `cpu_render` | — | 17.602 | 11.633 |
| `application` | 0.585 | 1.945 | 1.310 |
| `window_present` | 0.036 | 0.408 | 0.121 |

Actual post-change image checks retained the world draws. Q1 and classic Q2
had four matching state/clock captures each with zero RGB changes. Rerelease
Q2 matched gameplay state on all four captures with differing frontend clocks;
266–913 pixels changed out of 307,200, with maximum RGB difference 3. Q3's
later two matching gameplay-state captures had identical RGBA; earlier weapon
animation and picture clocks differed. No general animation equivalence is
claimed. Public CPU counter enable, disable retention and reset checks passed
for all four presets.

A separate short Q1 trace identified 68 image-update drains whose updated
storage was absent from the queued draw samplers. Cached brush rows already
retain pinned baked pixels. These are unnecessary global barriers. Image
updates must drain only draws that read the affected storage; real texture
and compositor dependencies still require ordering. This trace changes timing
and is not a frame-time result.
