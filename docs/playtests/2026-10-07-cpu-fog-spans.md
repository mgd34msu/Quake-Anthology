# CPU fog spans and remaining Q2 rerelease frame cost

## Zero-density fog follow-up, THE-566

Classic Q1's zero-density fog still selected fog fitting and generic fragment
shading. `qa_scene_frame_emit` now classifies inactive fog once on the copied
draw packet, before either renderer consumes it. Zero-amount CONSTANT,
EXP2 NO_EFFECT and zero-density EXP2 COLOR/RGB/ALPHA/RGBA become NONE.
OVERLAY, negative/nonzero density, Q2 global fog packets and the retained
numeric fog state are preserved. There is no new per-span or per-pixel check.

The qualified installed `3765bb6b` baseline and candidate differ only by this
shared draw classification in production code. Eight sequential retail e1m1
runs used the same copied 34 owner settings, private Xvfb/Openbox,
affinity `0-7,12-19`, no audio, debugger, profiler or concurrent build.
Each sampled 600 completed presents after 817–841 warm presents. Public
before/after reads matched: caps, swap interval and timedemo zero, FOV 120,
`r_smp` zero and `r_skyfog` 0.5. The variable NetQuake clock was unchanged.

| Edition | Drawable | Version | Median ms | p99 ms | Render mean ms |
| --- | --- | --- | ---: | ---: | ---: |
| Classic | 640×400 | Baseline | 2.913900 | 4.342889 | 2.283963 |
| Classic | 640×400 | Candidate | 2.502762 | 3.952092 | 1.878036 |
| Classic | 320×200 | Baseline | 1.903920 | 2.631894 | 1.427964 |
| Classic | 320×200 | Candidate | 1.717306 | 2.447640 | 1.244356 |
| Rerelease | 640×400 | Baseline | 3.082919 | 10.465315 | 2.595090 |
| Rerelease | 640×400 | Candidate | 3.187741 | 10.629399 | 2.673497 |
| Rerelease | 320×200 | Baseline | 2.117658 | 4.660216 | 1.645971 |
| Rerelease | 320×200 | Candidate | 2.099753 | 4.651035 | 1.633600 |

Classic medians improved 14.11% and 9.80%. The 320×200 renderer work counters
match exactly in both editions. At 640×400, live triangle/draw work varies;
classic span counts match, but the complete workload is not identical.
These single pairs support no gain on fogged rerelease: its 640×400 median
increased 3.40%, and its 320×200 reduction is only 0.85%. The rerelease
320×200 median still misses 2 ms. Its tails also remain above the targets.
All runs quit normally; all 72 recorded processes were absent afterward,
and owner settings and build files were unchanged.

Timing evidence: `qa-the566-zero-fog-plan-z6hot_01/paired-result.json`.
The exact candidate passed the full GCC build, six existing registered checks,
a strict Clang translation-unit check, and Original Q3 CPU gameplay with a
fresh copied owner profile and normal public quit. The visible Original
autosave/recovery ownership error is separately tracked by THE-585; this
startup/console qualification does not qualify saving. Candidate receipt:
`qa-private-av-5pfxkaa5/console-qualification.json`.

A separate offline comparison used actual `qa_scene_frame_emit` and the
production CPU renderer, with only the baseline/candidate frame object
changed. Complete RGBA and float-depth buffers matched exactly for 600
deterministic 64×48 draws, totaling 1,843,200 pixels per version. The 240
inactive-fog cases also matched their actual NONE reference. Preserved
OVERLAY, positive/negative EXP2 and active CONSTANT cases differed from NONE
and matched between versions. The cases include cached brush spans, generic
triangles, blending, overbright vertices, dynamic-lit models, alpha tests and
valid unlit preblend gamma. Q2 packets matched between versions; their chosen
small densities did not visibly alter output, so this is not active Q2 fog
proof. This is measured CPU output preservation, not GL pixel qualification
or a universal numerical bound. Evidence:
`qa-the566-zero-fog-20261007/pixels/result.json`.

Commit `41861a12` was pushed and its exact qualified build installed through
`tools/install_qualified_build.py` at 2026-10-07 08:12 CDT. Build time was
07:59 CDT. All three installed native files were byte-equal to the qualified
SDK outputs. The superseded `1c0ab5dc` binary package was removed after its
comparisons; the latest qualified `3765bb6b` fallback and prior evidence remain.

## Fog-fit carry, installed 3765bb6b

The shared cached-surface shader now retains its existing adaptive fog fit
across texture subdivisions. Texture coordinates still advance in the original
eight-pixel fixed-point steps. Every traversed pixel advances fog, including
pixels rejected by depth. The change adds no allocation or persistent cache.

The actual baseline and changed production shader processed 4,741,500 pixels
each in the offline comparison. Maximum RGBA difference was one byte per
channel; depth, disabled fog and rejected pixels matched exactly. Both versions
also stayed within one byte of the helper and analytic EXP2 reference for these
cases. This is measured numerical evidence, not a universal error bound.

The candidate passed the full GCC build, six registered checks and a strict
Clang check of the changed translation unit. Original Q3 CPU reached visible
gameplay, accepted public FOV/alias changes and quit normally with a private
copy of all 34 owner settings files. The source profile remained unchanged.

## Paired Q2 rerelease measurements

The byte-identical installed `1c0ab5dc` baseline and candidate were run
sequentially on retail base1. Their executable sources differ only by the fog
span change; subsequent baseline-history commits changed documentation. Native
runner and profile bytes matched. Each run used a fresh copy of the same owner
settings, private Xvfb/Openbox, affinity `0-7,12-19`, no audio, no debugger and
no profiler. Each captured 600 completed presents after 839–841 warm presents.
Public reads before and after confirmed caps, swap interval and timedemo zero,
FOV 120, `r_smp` zero and simulation 40 Hz.

Times below are milliseconds. Render means cover a separate warmed 720-call
timer interval; median and p99 use the final 600 actual presentation intervals.

| Drawable | Version | Median | p99 | Render mean | Scene mean | Application mean |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| 640×400 | Baseline | 6.964 | 61.105 | 6.082 | 0.802 | 0.410 |
| 640×400 | Candidate | 6.756 | 60.972 | 6.022 | 0.761 | 0.378 |
| 320×200 | Baseline | 4.889 | 8.637 | 4.026 | 0.698 | 0.258 |
| 320×200 | Candidate | 4.827 | 8.821 | 3.998 | 0.696 | 0.253 |

These pairs establish no reliable rendering speedup. Live triangle and skin
work varied slightly, tails were noisy, and private presentation, timers and
observer overhead are included. The 4 ms and 2 ms CPU targets remain missed.
Span counts matched within each resolution. All runs quit with status zero,
cleaned their owned processes and preserved settings and build files.

A separate six-second CPU640 diagnostic profile of the baseline collected
2,973 userspace samples with none lost. Its largest self costs were
`shade_rows` 12.18%, `depth_fog_rows` 11.81%, `sample_levels` 9.89% and
`fog_exp_four` 7.06%. Percentages are relative to all sampled game threads;
the diagnostic includes sampling overhead and is not a frame-time measurement.
Shading, texture sampling and the full-frame fog pass remain costs to reduce.
THE-196 remains open; this change does not close Q2 rerelease choppiness.

Evidence identifiers: `mike14-fog-carry-20261007/span-pixel-comparison.json`,
`qa-private-av-61sji88c/console-qualification.json`,
`qa-the196-paired-plan-2_pz70g3/paired-result.json` and
`qa-private-av-k0xuzay4/minimal-profile-result.json`.

## Paired Q1 measurements

The same `1c0ab5dc` baseline and installed `3765bb6b` were compared on retail
e1m1 in both editions. Rerelease worldspawn supplies EXP2 fog density 0.025
and RGB (171, 169, 234)/255; classic e1m1 has no authored fog. No fog setting
was added to either map. Only `brush_spans.c` differs in their production
sources; native runner and profile bytes match.

Each of the eight sequential runs used affinity `0-7,12-19`, a fresh copy of
the same 34 owner settings files, private Xvfb/Openbox and no audio, debugger
or profiler. Public reads before and after confirmed caps, swap interval and
timedemo zero, FOV 120, `r_smp` zero and `r_skyfog` 0.5. `sv_fps` read 20 in
every case; this is a setting readback, not a measured fixed NetQuake tick
rate. Each run captured 600 actual completed presentation intervals after
819–840 warm intervals, then quit normally. All owned processes were gone;
owner settings and build files remained unchanged.

| Edition | Drawable | Version | Median ms | p99 ms | Render mean ms |
| --- | --- | --- | ---: | ---: | ---: |
| Classic | 640×400 | Baseline | 3.201216 | 4.571434 | 2.580053 |
| Classic | 640×400 | Installed | 2.841614 | 4.163666 | 2.252108 |
| Classic | 320×200 | Baseline | 1.979121 | 2.770827 | 1.502731 |
| Classic | 320×200 | Installed | 1.903794 | 2.648304 | 1.411125 |
| Rerelease | 640×400 | Baseline | 3.460477 | 11.118970 | 2.942256 |
| Rerelease | 640×400 | Installed | 3.180206 | 10.815984 | 2.664379 |
| Rerelease | 320×200 | Baseline | 2.200100 | 4.812059 | 1.739938 |
| Rerelease | 320×200 | Installed | 2.083803 | 4.630906 | 1.619646 |

These single pairs show rerelease median reductions of 8.10% and 5.29%.
Classic reductions were 11.23% and 3.81%, despite lacking authored fog, so
the whole change cannot be attributed to eliminating exponential fog fits.
Span counts and written pixels match within each pair. Live triangle work
varied at 640×400; at 320×200, triangle counts and written pixels match.
Private presentation, timer and observer overhead remain included. The
rerelease 320×200 median still misses 2 ms, and all p99 values exceed the
corresponding 4 ms or 2 ms target.

Timing evidence: `qa-the566-q1-plan-szjxyuy0/paired-result.json`.

## Separate Q1 CPU320 profiles

Four separate six-second, 199 Hz userspace captures followed the timing runs.
They used the same private display, copied settings, affinity and builds,
with no debugger. Sampling began after 602–624 actual warm presents. Each
capture lost zero samples and ended with normal quit and complete owned
process cleanup. These samples include profiler overhead and are not the
frame-time measurements above.

The top five self costs, relative to all sampled game threads, were:

| Rank | Classic baseline | Classic installed | Rerelease baseline | Rerelease installed |
| --- | --- | --- | --- | --- |
| 1 | `shade_rows` 43.80% | `shade_rows` 36.10% | `shade_rows` 44.82% | `shade_rows` 35.08% |
| 2 | `mip_sample` 10.10% | `mip_sample` 11.90% | `mip_sample` 7.84% | `mip_sample` 9.51% |
| 3 | `sample_levels` 7.44% | `fragment_row` 7.45% | `fragment_row` 5.56% | `fragment_row` 7.33% |
| 4 | `fragment_row` 6.61% | `sample_levels` 6.80% | `sample_levels` 5.51% | `sample_levels` 5.93% |
| 5 | `fragment_store.isra.0` 3.41% | `fragment_store.isra.0` 4.72% | `fragment_store.isra.0` 3.19% | `fragment_store.isra.0` 4.44% |

Retained inline callchains confirm that the rerelease actually ran EXP2 span
fits. `cpu_fog_span_prepare → shade_span → shade_rows` appeared in 66 of
2,412 baseline sampled blocks and 11 of 2,209 installed blocks. Classic also
ran this code: 66 of 2,555 baseline blocks and nine of 2,310 installed blocks.
The current frontend emits EXP2 fog even when its density is zero, and the
span preparation enables it without checking density. Classic's reduction
therefore also removes wasted zero-density fits. Generic fragment fog samples
remain in all four cases; no full-frame depth-fog samples appeared.

The audit's estimate that fitting accounts for about 30% of the Q1 frame was
wrong for these measured cases. The observed gain is modest. `shade_rows`
includes texture filtering and pixel processing as well as fog, so its whole
self percentage cannot be assigned to fitting. The baseline already uses
the shared table-based `cpu_fog_exp`; these are not repeated libc `exp` calls.
Sample attribution and the single timing pairs do not establish a universal
cost or speedup across other maps, cameras or settings.

Profile evidence: `qa-the566-q1-profile-plan-3ck06xlv/separate-profiles-result.json`.
Raw rerelease captures are `qa-private-av-93fsz_cs` and `qa-private-av-5ya3meld`;
classic captures are `qa-private-av-7xg792nw` and `qa-private-av-f_2a7myu`.
