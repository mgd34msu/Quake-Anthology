# Q2 CLIENT common body/query adoption

THE-344, THE-2861 and THE-2874. The actual actor-page body owns received
collision state. Q2 CLIENT publication borrows one load-sized ordered actor
list and resolved model handles; prediction uses the same `qa_world` trace
and contents loop as the server. The old frontend clipping/contents scans,
model parsing, query admission and separate scratch are deleted. Default
server ordering and caller rules are retained.

Registry release invalidates the generation, cleans its canonical body owner,
then calls the observer. Attachment release uses load-sized actor-slot tickets
in the existing snapshot arena. Nested release, reattachment and observer slot
reuse preserve captured order; no child array or generic heap sort remains.
The unused SDK attachment-transport operation still allocates traversal storage.

## Behavior evidence

Eight actual-source GCC/Clang plain/sanitizer comparisons cover BSP38, classic
and rerelease bounds, ties, rotated inline brushes, contents, viewer exclusion,
generation reuse and teardown. The shared-loop optimization additionally
compares 19,980 boundary queries per run across three caller/target families,
points, boxes, capsules, stationary/startsolid, missile expansion, epsilon
and inverted packed Source bounds. Defined outputs match and hot heap/admission
calls are zero. Capsules and inline models bypass the box reject. Current pose
is read before rejection; linked bounds are not treated as current state.

The native Q2 startsolid replacement is a separately named correction:
`quake-2/client/cl_pred.c:129-138` replaces a trace on startsolid too. The old
frontend omitted that condition; an exit from actor 42 now correctly retains
actor 42. It is excluded from unchanged-output claims. Attachment components
compare 168,758 bytes in each compiler/sanitizer pair and remove 4,239
allocation/free pairs, including recursive observer reuse.

Worker fixtures link actual source; controlled physical admission/config
boundaries are explicitly substituted. They are not full-module gameplay.

## Pinned component timings

CLIENT: affinity `0-7,12-19`, no debugger, 512 warm-up batches and 4,096
samples, 32 traces plus 16 contents queries over the same BSP38/16 solids.
A1/B1/B2/A2. Each semantic transcript is equal. Publication is a separate
1.63–1.71 microsecond operation, previously a no-op.

| Edition | Old mean median, us | Common mean median, us | Old p99 A1/A2, us | Common p99 B1/B2, us |
| --- | ---: | ---: | --- | --- |
| Q2 classic | 48.576 | 37.8155 | 76.792 / 56.971 | 44.041 / 44.711 |
| Q2 rerelease | 49.081 | 38.136 | 79.851 / 62.422 | 60.691 / 49.301 |

This removes the initial migration's 29% regression. It is a bounded query
component result, not a frame-time speedup. The sole body/collision samplers
are inline in the existing internal header; the old definitions are deleted.

The coordinator rebuilt the native live-bound-field 48-query workload against
the final joint SDK (32 traces plus 16 broad-phase queries, two owned module
instances, 120 warm-up/600 samples, same affinity, no debugger). All 256-byte
query/touch outputs match. Hot heap, per-candidate IPC and descriptor resolution
are zero. Process snapshots show no competing compiler during the run.

| Edition | A1/B1/B2/A2 medians, us | A1/B1/B2/A2 p99, us |
| --- | --- | --- |
| Q2 classic | 8.960 / 9.260 / 9.310 / 9.155 | 14.240 / 12.920 / 13.511 / 15.200 |
| Q2 rerelease | 8.760 / 9.320 / 9.200 / 8.730 | 12.581 / 14.020 / 9.350 / 9.150 |

Every candidate median is below the 9.6-us acceptance limit. This is not a
relative improvement in every phase. Prior intermediate and contaminated
diagnostics remain separate from these final receipts.

## Full engine integration

Frozen staged source tree: `75945d56e6c1fd0762aa09f7cc689f0d2631b29d`,
13 source/test paths. Production, ASan/UBSan and allocation-gate builds pass;
platform-services, core, archive, VFS, BSP, image and model checks pass in each.
No unrelated uncommitted PlayerState/name/cvar changes entered those builds.

The exact candidate reaches CPU gameplay and quits normally on a private
Wayland display with a fresh read-only-source owner-profile copy:

- Q2 classic base1: `/tmp/qa-private-wayland-lp05x3b0`.
- Q2 rerelease base1: `/tmp/qa-private-wayland-6senb026`.
- Q2 rerelease base1 with Q3 movement: `/tmp/qa-private-wayland-6qpex2lu`.

The coordinator viewed all three world/weapon/HUD PNGs. All exits are zero,
42 original profile files stay unchanged and all 28 recorded owned process
tokens per run are cleaned up. Rerelease center text visibly overlaps vertically;
this is an unresolved nonfatal presentation observation, not a fidelity pass.
Dummy audio proves no sound behavior. These stationary checks do not prove
real input, GL, original guest gameplay, live save round trips or travel.

This partial slice is not installed. THE-344 retains Q1/QW/Q3 prediction bypasses,
THE-2873 retains the entity-role pose migration, and the five-edition whole-frame
allocation target remains open.

## Receipts

Coordinator: `/tmp/qa-the344-q2-body-final-20261009` (build/check logs,
frozen source, candidate, live.json and native-broadphase ABBA). Worker:
`/tmp/qa-the344-q2-body-adoption-20261009` and its `optimization/aabb-proof`
(actual-source comparisons and CLIENT timings). Attachment release:
`/tmp/qa-the2874-release-storage-20261009`.
