# THE-2873 canonical collision proof, 2026-10-09

Contents and surface flags now enter the canonical two-word domain at BSP load or the live module field boundary. Trace kernels, actor collision rows, movement and shared snapshots keep those bits. Native game/QC/QVM and original protocol boundaries export their original signed or unsigned words. The pairwise conversion API and its 32-iteration mask loop are deleted, together with every caller.

Q1 current terminals retain water, direction and the original terminal tag. The single ordinary PointContents boundary folds currents to water; raw traces and TruePointContents retain the original terminal. A Q2 mover in a Q1 world therefore still sees the current. Unknown Q1 terminals keep their signed token without using its magnitude as an allocation index.

## Correctness

- Full production and ASan builds passed; all seven core checks passed in each build.
- The codec passed 146,371 comparisons in each GCC/Clang normal and sanitizer run against the compiled previous converters.
- Nine datasets passed before/after/sanitizer comparison: Q1 start/e1m1, rerelease e1m1, Q2 and rerelease base1, Q3 q3dm1, and private no-VIS/current/opaque fixtures. All 5,334,908 native result bytes per complete stream matched.
- Two caller-owned scratch sets queried the same map concurrently: 4,096 canonical sample comparisons per dataset, 36,864 per candidate build. Contents/surface words, opaque terminals, contacts, leaf lists and visibility results matched serial. Common loaded-geometry byte snapshots stayed unchanged. The public fixture does not directly snapshot the private kernel allocations; the caller-scratch source migration and kernel component fixtures supply that separate evidence.
- Watched query allocation calls were zero in the candidate. The Q1 baseline made 74,880 watched calls over 720 timed batches; the other baselines already made zero.
- All six native SDK roles passed production and sanitizer lifecycles. Full-body, role/order/capacity and unrelinked-field mutation checks passed. All 24 first-104-byte gameplay records matched the previous live build. Both Q2 editions stopped reading modelindex for non-brush candidates (64 reads to zero); other semantic read counts stayed unchanged.
- Actual Q3 record-writer and native contents/surface-width fixtures passed. Shared records carry canonical words; legacy protocol and module records retain their native widths.

## Pinned timings

Runs were sequential on affinity 0-7,12-19 with no debugger. Each phase used 120 warm-up batches and 600 measured batches of the same 48 queries. ABBA means retained baseline, candidate, candidate, retained baseline. Values below are median / p99 microseconds per batch; they are not frame times.

| Retail map | A1 | B1 | B2 | A2 |
| --- | ---: | ---: | ---: | ---: |
| q1-e1m1 | 11592.674 / 12350.420 | 11511.422 / 12183.496 | 11538.193 / 11804.469 | 11464.082 / 12188.658 |
| q2-base1 | 46.971 / 52.441 | 46.681 / 52.161 | 47.472 / 54.041 | 47.261 / 55.881 |
| q2rr-base1 | 63.491 / 77.312 | 62.681 / 68.181 | 62.931 / 77.922 | 63.381 / 73.591 |
| q3-q3dm1 | 62.431 / 72.411 | 62.411 / 71.682 | 64.181 / 81.252 | 63.521 / 74.512 |

No material public-query latency speedup is claimed. Phase variation overlaps the small differences. Q1 load-time work and caller scratch increase cold cost: e1m1 load was about 3.7-3.9 ms before and 29.3-30.1 ms after; its candidate scratch retains about 10.26 MB. These replace on-query work/allocation and remain explicit load/memory costs, rather than hidden improvements.

### THE-2861 live-read batch

The historical cached baseline remains faster than the current live reader. The 9.6 microsecond target is still open; the following numbers do not close it. Every phase retained identical 256-byte results and zero native read/borrow calls or controller heap calls inside the measured batch.

| Comparison and edition | A1 | B1 | B2 | A2 |
| --- | ---: | ---: | ---: | ---: |
| historical cache / classic | 9.730 / 10.150 | 10.230 / 13.321 | 10.980 / 13.981 | 9.671 / 18.471 |
| historical cache / rr | 9.740 / 9.930 | 10.000 / 10.150 | 9.980 / 11.820 | 9.750 / 12.790 |
| preceding live / classic | 10.511 / 14.261 | 10.170 / 13.980 | 10.200 / 10.551 | 10.970 / 14.850 |
| preceding live / rr | 9.935 / 13.810 | 9.831 / 9.990 | 10.140 / 19.110 | 10.130 / 15.250 |

Requested post-e6bb3bbf historical ABBA medians were classic 9.581 / 11.011 / 10.941 / 9.770 us and rerelease 9.671 / 10.750 / 10.690 / 10.030 us. Those numbers also missed the 9.6 us target. Further live-reader work must preserve unrelinked field changes and avoid copied caches.

## Evidence and limits

The frozen build source was d14fa80f. Evidence packets: `/tmp/qa-the2873-canonical-final-20261009`, `/tmp/qa-the2873-public-canonical-20261009`, `/tmp/qa-the2873-selective-decode-20261009`, and the codec/ABI component packets referenced by their reports. These are bounded engine/API checks with real retail map content and actual SDK modules, not full campaign or installed gameplay acceptance. The installed qa-c has not changed in this slice.
