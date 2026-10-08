# Q2 rerelease CPU brush work

THE-196 / MIKE-14 measured the installed `989b43c5` build on retail `base1`.
Optional counters added by `6bfeea32` separate edge sorting, span generation,
worker span writes, surface rebuilds and the actual present callback.

The two runs were sequential, pinned to logical CPUs `0-7,12-19`, with no
debugger or sampling profiler. Each copied the owner's 34 settings files,
kept the simulation settings, and used explicit CPU render dimensions.
Fullscreen and presentation caps were disabled for this bounded measurement.
Public reads confirmed all four cap aliases and swap interval were zero,
and `sv_fps` remained 40. Actual drawable sizes matched each case.

| Measurement | 640 x 400 | 320 x 200 |
| --- | ---: | ---: |
| Frame median, ms | 5.414529 | 3.901923 |
| Frame p99, ms | 14.632088 | 5.676999 |
| Warm completed frames before sample | 838 | 831 |
| Sample completed frames | 600 | 600 |
| Edge sort mean, ms per present | 0.079012 | 0.071918 |
| Span generation mean, including sort | 0.430202 | 0.259777 |
| Span write worker elapsed sum per present | 3.032754 | 0.918337 |
| Surface cache rebuild mean, ms per present | 0.250925 | 0.044657 |
| Surface rebuild count in 720-frame timer window | 2054 | 304 |
| Present copy/update mean, ms per present | 0.261508 | 0.081920 |
| CPU render scope mean, ms | 4.315654 | 2.912857 |
| Scene build scope mean, ms | 0.751644 | 0.713471 |

The work counters and existing scopes use a separate warmed 720-frame report
interval. Worker span time is accumulated across parallel jobs; it includes
depth and color writes, and is not frame wall time. The present callback
includes copying/scaling and the SDL window-surface update. It excludes the
earlier renderer gamma preparation. These overlapping scopes must not be added
as independent frame costs.

Edge sorting accounts for about 1.5% and 1.8% of the respective frame medians.
It is not the material cost in this measured workload. No scanline-bucket
rewrite was made, and no speedup is claimed. The original Q2 bucket approach
remains the reference for future span work: `ref_soft/r_edge.c:57-58` and
`R_InsertNewEdges` at line 688. Both CPU frame targets remain open.

Private bundles `qa-private-av-w5w5jt0l` and `qa-private-av-6z5ud3x4` retain
`timing-result.json`, public reads, stage reports and completed-present records.
Both exited through public quit with code zero. Owner settings and all three
installed artifacts stayed unchanged; the owned games were absent after
cleanup. Timing used a private Xvfb display and no audio. Present-completion
records include the retained observer's file-I/O overhead and private display
presentation. This is a stationary-map measurement, not physical-desktop,
sound, campaign or cross-build fidelity evidence.
