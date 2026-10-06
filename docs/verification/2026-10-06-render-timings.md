# Retail render measurements, 2026-10-06

These results do not meet the performance targets. The current CPU result does
not demonstrate a speedup from the brush-span renderer.

The latest measured executable was built from `c334bd85` with GCC, RelWithDebInfo and
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
| `b60ef5e3`, CPU | 640×400 | 47.744 | 60.916 | <4 |
| `d10d5a05`, CPU | 640×400 | 52.881 | 108.189 | <4 |
| `c334bd85`, CPU | 640×400 | 49.505 | 78.423 | <4 |
| `b60ef5e3`, CPU | 320×200 | 25.835 | 38.862 | <2 |
| `d10d5a05`, CPU | 320×200 | 26.561 | 39.388 | <2 |
| `c334bd85`, CPU | 320×200 | 24.420 | 37.030 | <2 |

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

Q1 classic `e1m1`, with no fog, used the same artifact and measurement procedure:

| Resolution | Median, ms | p99, ms | `cpu_render` mean, ms | `scene_build` mean, ms | Target, ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| 640×400 | 14.927 | 17.824 | 12.341 | 1.134 | <4 |
| 320×200 | 7.704 | 9.746 | 6.042 | 0.993 | <2 |

Fog alone therefore does not account for the slow CPU path. A separate bounded
counter capture on a private, coherently linked `315c9708` Clang renderer found
354,197 span-covered pixels, 248,415 span writes, 456,358 triangle-covered pixels
and 130,918 triangle fragment calls in one Q2 rerelease frame. That capture used
640×480, not either resolution in the timing tables. Its 39 synchronous batches
posted and joined workers 172 times. It proves that rerelease world spans were
active; it is not a frame-time measurement or a shipped production counter.

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
