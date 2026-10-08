# CPU fog spans and remaining Q2 rerelease frame cost

## Rejected sampler register-return change

A candidate kept SSE2 sampled colors in registers through mip blending and
packing, removing temporary color stores and reloads. The existing actual
production-kernel comparison passed 13,800 cases on both SSE2 and the
forced portable branch with exact old/new RGBA, depth and fog recurrence.
That numerical result did not translate into a reliable retail speedup.
The candidate was removed; the installed `41861a12` is unchanged.

Four sequential native Q2 rerelease `base1` CPU cases used fresh copies of
the same 34 owner settings, affinity `0-7,12-19`, exact drawables, caps,
swap interval and timedemo zero, FOV 120, `r_smp` zero and `sv_fps` 40.
No Source clock setting was written. Each case measured 600 actual present
intervals after at least 832 warm presents, without a debugger, profiler,
compiler or another game in the runtime lane.

| Resolution | Median ms, installed → candidate | p99 ms, installed → candidate | Warm presents |
|---|---:|---:|---:|
| 640×400 | 6.993359 → 7.668928 | 60.214933 → 64.916463 | 840 / 840 |
| 320×200 | 5.008772 → 4.869014 | 9.619575 → 9.533473 | 841 / 832 |

Separate warmed timer intervals put mean CPU rendering at
6.097882 → 6.378267 ms for 640×400 and 4.094585 → 4.019728 ms for 320×200.
Scene build was 0.826511 → 1.022493 ms and 0.753104 → 0.719667 ms.
Span counts match exactly within each pair; live triangle/skin work varies,
and 320×200 worker posts/joins vary too. Private presentation, observer and
timer overhead are included. The smaller 320×200 median does not offset
the slower 640×400 result or establish a reliable gain.

All cases quit normally with code 0. All 36 recorded owned process tokens
were independently absent afterward. Artifact packages, helper pins and
the original profile stayed unchanged during measurement. Before timing,
the exact candidate also reached Original Q3 gameplay and quit normally
with a private copy of the owner's profile. Its known THE-585 save/recovery
text was outside that qualification.

After rejecting the change, rebuilding the restored SDK source produced
an executable byte for byte equal to the installed `41861a12`. The rejected
source and numeric evidence remain privately reproducible. Receipts:
`qa-the196-vector-return-plan-1twtnq1y/paired-result.json`,
`pair-comparison.json`, `the196-result.md`, and the four referenced raw
case receipts. The candidate qualification is `qa-private-av-uprnf4k1`.
THE-196 and both CPU targets remain open.

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
valid unlit preblend gamma. Q2 global, height-only and combined fog also
remained visibly active and matched exactly between versions. This is
measured CPU output preservation, not GL pixel qualification
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


## Current installed Q2 rerelease baseline, THE-196

Installed ee1cb36a was measured after the combat checks had ended. Two
sequential native retail base1 runs used copied owner settings, private
Xvfb, affinity 0-7,12-19, caps/swap/timedemo zero, no debugger, profiler,
compiler, other game or audio. Each samples 600 present intervals after
835 or more warmed presents; actual Source sv_fps remains40.

| Drawable | Median ms | p99 ms | Separate warmed render mean ms |
|---|---:|---:|---:|
| 640×400 | 6.319329 | 25.055940 | 5.410893 |
| 320×200 | 4.440944 | 7.157502 | 3.667408 |

Warmed scene-build means are0.809013 and0.729394 ms, respectively.
These are a fresh baseline, not an attributed improvement over the
earlier418 run. Present intervals include private presentation and
observer overhead; the renderer means cover a separate720-call window.
Both public quits return0, artifact and original profile remain
unchanged, and the actual game processes are absent. CPU targets remain
open. Raw receipts: `/tmp/qa-private-av-bqc3ugvn/timing-result.json` and
`/tmp/qa-private-av-ifew4_8a/timing-result.json`.

## Rejected four-pixel sampling packet

A proposed SSE2 packet in `brush_spans.c` processed four interior,
depth-passing bilinear or trilinear samples together. It preserved the
eight-pixel perspective steps and the scalar border, tail and depth-reject
paths. A bounded 2,048-case comparison matched raw RGBA, native float depth,
stencil, floating-point exceptions and write counts for SSE2 and portable
builds. That protected output, but did not establish a speedup.

Quiet sequential retail base1 pairs used the same copied owner settings,
affinity 0-7,12-19, native 40 Hz scheduling, zero caps and swap interval,
FOV 120 and `r_smp` zero. Each sampled 600 presentation intervals after
600 or more warm intervals, without a debugger, profiler, compiler,
another game or audio. Public quit returned zero in every case; original
settings and artifact pins stayed unchanged, and owned processes were gone.

| Drawable | Baseline median ms | Packet median ms | Baseline p99 ms | Packet p99 ms |
| --- | ---: | ---: | ---: | ---: |
| 640×400 | 6.888961 | 6.936296 | 63.440397 | 67.923743 |
| 320×200 | 4.486770 | 4.578407 | 7.159342 | 8.836406 |

Separate warmed renderer means increased from 5.756012 to 6.008994 ms
and from 3.675107 to 3.742872 ms. The candidate was reverted in both the
repository and runtime build checkout. No production change was committed.
These single pairs include private presentation and observer overhead;
live model work varied. They reject this candidate for the measured case,
without assigning its cost to an unmeasured instruction or cache effect.

Evidence: `qa-the196-cached-span-four-20261007/root-build-result.json` and
`qa-the196-vector-return-plan-3d7oipq_/paired-result.json`.

## Common texture-mode policy, THE-196 / THE-843

Commit `31b55a7a` makes ordinary mipmapped wall and skin images use the
existing common texture-mode setting in CPU and GL. The copied owner
profile requests `GL_LINEAR_MIPMAP_NEAREST`; ordinary images previously
retained hardcoded trilinear filtering. Explicit UI, sky, streamed and
non-mipmapped images retain their declared policy, and the existing
Source-Q3 dynamic policy retains its precedence.

This is a setting-correctness change. Respecting the requested filter can
change pixels; it is not an equal-output arithmetic optimization or a
change to the default. The same quiet paired method gave:

| Drawable | Baseline median ms | Candidate median ms | Baseline p99 ms | Candidate p99 ms |
| --- | ---: | ---: | ---: | ---: |
| 640×400 | 6.251893 | 6.042487 | 23.028877 | 16.278325 |
| 320×200 | 4.538981 | 4.505836 | 7.198402 | 8.597802 |

Separate warmed renderer means were 5.398882 → 5.015525 ms and
3.673583 → 3.490497 ms. World-span counts match at 640×400; live model
work varies, and span counts vary at 320×200. Each run sampled 600
intervals, preserved the owner profile and build pins, quit with zero,
and cleaned up its owned processes. Both CPU targets remain open.

Evidence: `qa-the196-vector-return-plan-k1c6k8xn/paired-result.json`.
These performance runs do not by themselves prove live GL resident
texture changes or qualify installation of the candidate.

### Policy checks and installation

The exact `31b55a7a` candidate passed native Q2 rerelease base1 checks in
CPU and GPU0 GL with fresh copies of the 34 owner settings files. Each
route captured the untouched baseline, changed `gl_texturemode` through
the real console, restored `r_textureMode`, captured the resulting world
frames and quit normally with zero. The original profile and candidate
files stayed unchanged; recorded owned processes were absent after cleanup.

CPU observations confirmed actual drawn WALL sampler changes, with UI and
SKY retaining literal filters. GL observations confirmed actual min/mag
`TexParameteri` calls and the same resident world texture changing and
restoring. These API calls are not an independent driver-state query.
Root viewed all six completed-world PNGs. No SKIN image was sampled in
these retained banks, so that category remains unqualified.

The three files were installed through `tools/install_qualified_build.py`
at 18:31:29 CDT from the build completed at 18:09:39 CDT. The installer
recorded byte equality for the executable and both native companions.
THE-843 is In Review; THE-196 remains In Progress. Slack received the
new-build note with the remaining CPU targets stated explicitly.

Evidence: `qa-private-av-noh7h37y` for CPU,
`qa-private-av-jmlzmtmq` for GL, and
`installed-m0-common-texture-mode-20261007.json` for installation.
Three earlier observer attempts were unqualified and remain retained:
`u8zjpqcb` rejected a symlink-containing input directory before launch,
`fem9_2c4` lost its cached world-view record, and `9lw1f3_a` rejected a
stale observer target. No production fix was made for these fixture errors.

A separate Original Q3 CPU attempt, `qa-private-av-uk0py81z`, loaded
Source image entries but did not qualify: the observer read the ordinary
frontend frame after Source commands had retired. Loaded image metadata
alone does not prove Source gameplay sampling. The Source-specific policy
still precedes ordinary image handling in code; live Source policy proof
remains a separate gap. These checks do not establish every game's,
remote play's or combined mode's complete filtering behavior.

## Direct Q2 height-fog rays

The recovered `fog.c` work replaces cubic fitting of view-ray lengths with
direct four-lane norms. Its scalar tail and portable path use the same
point-ray calculation only for geometry needing height fog. Q2's original
shader normalizes `world_pos - vieworg` at each fragment; see
`q2repro/src/refresh/shader.c:543`. Global and height fog still blend in
their original order, with the existing CPU byte conversion at each stage.
Other fog kinds, sky classification, target-alpha behavior and exponent
helpers retain their previous semantics.

A reused bounded component comparison checks the candidate against a
scalar direct-ray reference derived from the original shader equations.
Both SSE2 and portable builds ran 576 queued cases covering 49,206,144
pixels, four rounding modes, scalar tails, geometry and sky. Scalar RGBA
matched exactly. SSE2 changed 46 channels by one byte each, with maximum
RGB and alpha delta one and no alpha-zero changes. Depth and stencil bits
matched; warmed fog allocated nothing. This is bounded component evidence,
not a universal GPU error bound. The old cubic approximation is not an
exact reference for the original ray normalization.

The candidate built with the production strict flags and passed the six
configured CTests. A fresh copied-owner retail base1 CPU launch reached
gameplay, exercised and restored the common texture policy, captured
completed frames and quit normally with zero. Original settings and build
pins remained unchanged, and owned processes were absent.

The quiet paired method held `31b55a7a` and the candidate's requested
texture filtering identical. Only `fog.c` differs in production sources.

| Drawable | Baseline median ms | Candidate median ms | Baseline p99 ms | Candidate p99 ms |
| --- | ---: | ---: | ---: | ---: |
| 640×400 | 6.069082 | 6.140548 | 16.125733 | 16.215796 |
| 320×200 | 4.364568 | 4.186719 | 6.357198 | 5.784937 |

Separate warmed renderer means were 4.974889 → 4.947368 ms and
3.412933 → 3.260665 ms. Span and worker counts match at both sizes;
animated triangle work and scene-build time vary. Every case sampled
600 presentation intervals after 832 or more warm intervals, preserved
the owner settings and artifact pins, quit with zero and cleaned up its
owned processes. There was no debugger, profiler, compiler, other game
or audio during these pinned runs.

The 640×400 pair shows no improvement in total median. The 320×200 pair
is modestly lower, but still misses 2 ms. Both CPU targets remain open;
this result does not establish a general frame-time speedup.

Evidence: `qa-the196-direct-fog-rays-20261007/root-results.json`,
`qa-private-av-_7xifk75/installer-shaped-qualification.json` and
`qa-the196-vector-return-plan-etpe2uwf/paired-result.json`.

The direct-ray source is committed and pushed as `98225ced`. The qualified
candidate was installed at 18:39:19.680938 CDT, with all three runtime
files byte-equal to the build. Receipt:
`installed-m0-direct-fog-rays-20261007.json`.

## Rejected final fog packing candidate

Two final SSE2 packing calls were replaced temporarily with integer
conversion because every lane was already an integral byte. The reused
bounded comparison ran 576 cases and 49,206,144 pixels in each SSE2 and
portable build. RGBA, depth and stencil matched exactly across four
rounding modes; warmed fog allocated nothing. Strict build, six configured
CTests and copied-owner gameplay/normal-quit qualification passed.

The quiet sequential retail pair held texture filtering and fog equations
identical to installed `98225ced`. Each case sampled 600 presentation
intervals after at least 835 warm intervals, pinned to physical cores 0–7,
without a debugger, profiler, compiler, audio or another game.

| Drawable | Baseline median ms | Candidate median ms | Baseline p99 ms | Candidate p99 ms |
| --- | ---: | ---: | ---: | ---: |
| 640×400 | 6.010870 | 5.876943 | 16.325701 | 15.206207 |
| 320×200 | 4.283349 | 4.340015 | 6.045136 | 6.585097 |

Separate warmed renderer means were 4.915826 → 4.776615 ms and
3.351086 → 3.373569 ms. Span counts and written pixels match within each
pair. Source cadence remains 40 Hz; caps and swap interval are zero,
FOV is 120 and `r_smp` is zero in all four runs. Every run preserved
settings/build pins, quit with zero and left no owned processes.

The result is small and mixed, so the candidate was reverted in both
the repository and SDK. The installed executable was never replaced.
This is not a speedup claim, and both CPU targets remain open.

Evidence: `qa-the196-integral-fog-pack-20261007/root-results.json`,
`qa-private-av-9_4e7ixf/installer-shaped-qualification.json` and
`qa-the196-vector-return-plan-xc_7fu16/paired-result.json`.

## Rejected duplicated surface borders

The next candidate stored a duplicated one-texel ring around each lit mip
inside the existing 32 MiB arena. The span sampler then used four adjacent
taps without repeating half-texel boundary clamps. Filtering, interpolation,
lighting, depth, fog and cache stamps remained unchanged. Returned stride
was derived from the retained slot width, including cache hits.

The reused component called the actual surface builder twice after a color
and light revision change. Interior rows and rebuilt borders were checked,
then RGBA, native float depth, stencil, write counts and floating-point state
were compared over 2,048 cases in each SSE2 and portable span/cache wrapper.
Both passed across all four rounding modes, including 1×N and N×1 mips.
The small-block fixture does not qualify arena admission or eviction.

Strict build and six configured CTests passed. Exact-candidate gameplay
with a fresh copy of the 34 owner settings reached retail base1, exercised
and restored filtering, captured completed frames and quit with zero.
The coordinator viewed all three PNGs. Original settings and artifact pins
stayed unchanged; recorded owned processes were absent.

The quiet sequential pair used installed `98225ced` as the baseline, the
same filter/fog/caps/FOV/40 Hz settings, affinity `0-7,12-19`, and 600
presentation intervals after at least 833 warm intervals. No debugger,
profiler, compiler, audio or other game ran during measurement.

| Drawable | Baseline median ms | Candidate median ms | Baseline p99 ms | Candidate p99 ms |
| --- | ---: | ---: | ---: | ---: |
| 640×400 | 6.003911 | 5.923569 | 16.206950 | 15.172958 |
| 320×200 | 4.364767 | 4.324656 | 6.156010 | 6.173379 |

Separate warmed renderer means were 4.934278 → 4.808488 ms and
3.383911 → 3.355497 ms. Span work matched within each pair, with zero
cache rejects. Every run quit with zero, preserved settings/build pins
and left no owned processes.

The full-frame differences are small. Padding also reduces the number of
surfaces that fit in the unchanged arena, particularly tiny mips. The
candidate was reverted in the repository and SDK; the installed executable
was not replaced. Both CPU targets remain open. No broad speedup or cache
pressure qualification is claimed.

Evidence: `qa-the196-padded-surfaces-20261007/root-build-result.json`,
`qa-private-av-toxi38wn/installer-shaped-qualification.json` and
`qa-the196-vector-return-plan-cfjiirzu/paired-result.json`.

## Installed Original Q3 filter check

Installed `98225ced` passed the private GPU0 GL route with the Original
Q3 module on retail q3dm1 and a fresh copy of the 34 owner settings.
The observer now records Source image-use bits at entry to
`qa_render_source_report`, before they are cleared. Each accepted report
matches the completed N+1 frame, phase, renderer, controls and returned
Source world view. This fixes the observer's evidence cut; no engine code
was changed.

The actually used mip image `gfx/2d/numbers/zero_32b.tga`, Source ordinal
51 / GL name 53, followed the common mode from linear-mipmap-nearest to
nearest-mipmap-nearest and back. The same GL name received real min/mag
parameter calls 9984/9728 on change and 9985/9729 on restoration.
The actually used no-mip `*white`, ordinal 1 / GL name 3, retained linear
filtering. Use bits establish use in that Source frame, not a particular
world or HUD draw; submitted GL calls are not an independent driver query.

All three completed gameplay PNGs were viewed. Public quit returned zero,
the original profile and installed files stayed unchanged, and an
independent PID/start-token check found all 16 owned processes absent.
The pre-existing Original Q3 autosave warning remains tracked separately
under THE-585. This debugger-assisted check makes no timing claim.

Evidence: `qa-private-av-ls0nyznc/user/evidence/policy-checks.json`,
`completed-policy-captures.jsonl`, `gl-tex-parameter-calls.jsonl`, three
PNGs, parent `qualification-result.json` and `root-owned-cleanup.json`.
THE-843 remains open for retail Q1 classic/rerelease and Q2 classic CPU/GL
checks, actual skin samples, and the other unqualified paths. This run
does not close THE-196's CPU frame-time targets.
