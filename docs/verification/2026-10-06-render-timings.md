# Retail render measurements, 2026-10-06

These results do not meet the performance targets. The latest CPU result is
lower, but its rendering scope still exceeds the complete-frame target.

The latest measured executable was built from `c8d26292` with GCC, RelWithDebInfo and
warnings treated as errors. The complete GCC and Clang builds and their six
registered CTest checks each passed. The testing copy, `qa-c`, was compared byte
for byte with that build after installation.

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
| `b60ef5e3`, CPU | 640×400 | 47.744 | 60.916 | <4 |
| `d10d5a05`, CPU | 640×400 | 52.881 | 108.189 | <4 |
| `c334bd85`, CPU | 640×400 | 49.505 | 78.423 | <4 |
| `c8d26292`, CPU | 640×400 | 23.280 | 52.069 | <4 |
| `b60ef5e3`, CPU | 320×200 | 25.835 | 38.862 | <2 |
| `d10d5a05`, CPU | 320×200 | 26.561 | 39.388 | <2 |
| `c334bd85`, CPU | 320×200 | 24.420 | 37.030 | <2 |
| `c8d26292`, CPU | 320×200 | 16.659 | 28.633 | <2 |

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
| `c8d26292` | 320×200 | 6.881 | 8.404 | 5.326 | 0.949 | <2 |

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
