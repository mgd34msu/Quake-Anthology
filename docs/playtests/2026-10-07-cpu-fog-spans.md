# CPU fog spans and remaining Q2 rerelease frame cost

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
Shading, texture sampling and the full-frame fog pass remain the next costs to
reduce. THE-196 and THE-566 remain open; this change does not close choppiness.

Evidence identifiers: `mike14-fog-carry-20261007/span-pixel-comparison.json`,
`qa-private-av-61sji88c/console-qualification.json`,
`qa-the196-paired-plan-2_pz70g3/paired-result.json` and
`qa-private-av-k0xuzay4/minimal-profile-result.json`.
