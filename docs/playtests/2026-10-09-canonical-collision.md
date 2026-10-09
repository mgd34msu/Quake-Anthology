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

## Follow-up: captured candidate row access

The callback-free trace clip loop now reads the already captured slot directly. Membership retirement removes/clears the row before an external unlink callback. Public getters and callback-driven visitors retain their lifetime checks. The source fields remain live. This matches the direct linked-entity access in WinQuake/world.c:821-827 and quake-2/server/sv_world.c:525-537.

Exact production/ASan builds and both seven core checks passed. Six native SDK roles, role/order/capacity, full reads and unrelinked mutation checks passed again; all gameplay/query bytes remained identical.

Pinned medians / p99 microseconds per 48-query batch:

| Comparison and edition | A1 | B1 | B2 | A2 |
| --- | ---: | ---: | ---: | ---: |
| historical / classic | 9.600 / 16.260 | 10.331 / 10.490 | 9.711 / 10.211 | 9.691 / 9.990 |
| historical / rr | 10.171 / 10.350 | 9.540 / 12.910 | 9.541 / 18.381 | 9.610 / 13.720 |
| immediate-live / classic | 10.150 / 12.330 | 9.910 / 10.131 | 9.870 / 13.790 | 9.950 / 10.110 |
| immediate-live / rr | 10.075 / 13.540 | 9.660 / 12.980 | 9.500 / 12.870 | 10.200 / 10.340 |

The 9.6 us target remains open: classic candidate phases are 9.711-10.331 us; rerelease phases are 9.500-9.660 us. The immediate-live comparison is lower, but the historical classic comparison does not establish recovered performance. No consistent whole-workload speedup is claimed. Evidence: `/tmp/qa-the2873-candidate-row-20261009` and `/tmp/qa-the2873-candidate-row-final-20261009`. One retained comparison executable initially lacked execute permission (exit 126); it ran no module, was corrected, and the complete immediate-live ABBA restarted with the failed attempt preserved outside valid results.

## Dropped experiment: owner-only projection

Tree 50db2af7 decoded only the passing actor's owner tuple. Production/ASan builds, both core suites and actual SDK behavior checks passed; 24 gameplay records and 40 owner-transition records matched. Diagnostic solid reads fell from 96 to 80 and flag reads from 64 to 48, but timing did not improve.

Historical ABBA median / p99 microseconds:

| Edition | A1 | B1 | B2 | A2 |
| --- | ---: | ---: | ---: | ---: |
| classic | 10.090 / 14.340 | 10.510 / 14.050 | 10.230 / 10.410 | 9.640 / 9.810 |
| rerelease | 9.580 / 13.570 | 9.940 / 10.100 | 9.730 / 9.881 | 9.840 / 14.090 |

The immediate-live comparison also showed no gain. All candidate medians exceeded 9.6 us. The three source changes were dropped; the experiment is retained only as evidence under `/tmp/qa-the2873-owner-projection-20261009` and `/tmp/qa-the2873-owner-projection-final-20261009`.

## Shared-save compatibility

The initial canonical migration widened untagged collision fields. The single codec now retains their compact native encoding, imports it into canonical state on read, and appends sparse canonical fields only where a native word cannot retain the state. Ordinary saves keep their original bytes. No format choice or second reader was added.

Actual writers from 23356edb's parent produced 471 payloads that the repaired reader decoded and rewrote byte-identically. Another 207 mixed/opaque cases round-tripped without loss. GCC, Clang and both ASan/UBSan variants passed. The owner profile's Q2 rerelease, Q3 and recovery world records also decoded and rewrote identically: 537/268, 70/11 and 390/197 body/spatial counts respectively. This component check does not establish a full saved-session restore.

Exact staged source 01ee20c4 passed full production and ASan builds and both seven core suites. Evidence: `/tmp/qa-the2873-persistence-compat-20261009` and `/tmp/qa-the2873-one-pass-final-20261009`. The transient untagged widened layout was never installed in qa-c and is not guessed by the reader; the inspected owner records use the original compact layout.

## Q1 metadata resolved at load

Q1 contact faces now retain canonical surface flags and authored texture names.
The contact query reads those records directly; it no longer resolves texinfo or
texture metadata. BSPX BRUSHLIST decode tables are temporary load records and
are freed after constructing canonical cells, terminals and model ranges. The
first authored range, including an empty range, retains its original meaning.

Full production and ASan/UBSan builds and both seven core suites pass. The
integrated public-query fixture reproduces all 5,334,908 bytes across nine
datasets in both builds. Each build performs 36,864 serial/concurrent comparisons
using two scratches on the same const geometry, with unchanged loaded snapshots
and zero watched query heap calls. Retail Q1/Q2/Q2RR/Q3 maps, opaque Q1 terminals,
currents and missing Q3 visibility retain their native output.

The contact-specific GCC/Clang sanitizer fixtures preserve 6,187 rows per dataset
for retail, missing and external textures and BSPX solid/water/opaque cases.
Texture/texinfo query lookups fall from 12,374 to zero. This is a structural count,
not a latency claim. Evidence: `qa-the2873-q1-cold-20261009` and
`qa-the2873-cold-public-20261009/results.json`.

Shared-world mutation and concurrent portal updates are outside the const
geometry proof. The renderer's separate submission admission-stamp journal
remains a primitive/allocation bypass and is being migrated to `qa_stamp_set`.

## Dropped experiment: direct spatial clipping

The callback-free trace and point queries were tried as one spatial walk instead of collecting actor IDs first. Candidate order, tied contacts, exclusions, poses, owner transitions and failed-output behavior matched. Cold queries with more than eight candidates made zero heap calls instead of two. Full production/ASan builds, both core suites, six SDK roles and all 24 gameplay records passed.

Three versions were measured: embedded result storage, borrowed result pointers, and borrowed once-per-query sampler errors. None established a consistent improvement. The final exact tree 05c07898 produced these historical ABBA median / p99 microseconds per unchanged 48-query batch:

| Edition | A1 | B1 | B2 | A2 |
| --- | ---: | ---: | ---: | ---: |
| classic | 9.580 / 9.711 | 10.260 / 14.810 | 10.320 / 19.171 | 9.691 / 9.850 |
| rerelease | 9.680 / 9.990 | 9.680 / 10.191 | 9.820 / 9.970 | 9.580 / 13.450 |

All 16 historical and immediate-live phases preserved exact results and zero warm native-read, borrow and controller heap counts. The new-build medians did not all meet 9.6 us. The source experiment was dropped; retained evidence is under `/tmp/qa-the2873-one-pass-20261009`, `/tmp/qa-the2873-one-pass-pointer-20261009` and `/tmp/qa-the2873-one-pass-error-20261009`. This does not close THE-2861. The serial-versus-concurrent map comparison remains the public two-scratch proof above; it is not a module/controller concurrency claim.
