# Shared CLIENT queries and explicit entity pose

THE-344, THE-2873 and THE-2861. Verified source tree
`c2927c604a0681271e6da07f41f5372a2843787c`, based on `d58ca3b1`.

Q1/QW and Q3 CLIENT prediction now use the same actor/body/world query loop
as Q2. Received records remain protocol/history data; the current CONTROL
body is canonical. Q3 CLIP/CONTENTS borrow its retained source spatial fields.
Ordered membership and caller rules preserve the original QW strict-nearest
and Q3 allsolid replacement behavior. Literal source entity numbers are
resolved without a separate lookup table or per-query actor scan. The old
prediction trace/contents loops, model parsing and independent scratch are
deleted. Q3's single-model trigger overlap remains an original narrow phase.

Entity-role data now supplies brush rotation, normal restoration and link
bounds independently of the loaded geometry and caller contact rules.
One shared implementation preserves Q1 no rotation, Q2 negative-Euler normal
restoration and Q3 transpose restoration. All current target/link callers
supply their original role. The common leaf API also accepts caller data:
collision retains the authored root, while Q1 Mod_PointInLeaf uses node0,
float DotProduct and the original back-side tie, including node-less leaf0.
The scene Q1 contents copy is deleted.

Production, ASan/UBSan and allocation-gate builds and seven core checks in
each pass for the exact frozen source. Actual-source GCC/Clang plain and
sanitized Q1/Q3 query, wire, pose/link and leaf components pass. Q1 named stock
corrections (player order/boxes, startsolid and leaf arithmetic) are recorded
separately from ordinary byte parity. The Q3 matrix covers the initial-world
allsolid case where an early broad-phase cull would wrongly change its winner.
The correction preserves the original post-row stop in the common loop.

The current public geometry rerun passes all 18 production/sanitized retail
and derived cases. Each compares 128 serial samples with 4,096 full-query sets
from two simultaneous scratch owners. Canonical trace/contents/contact/leaf/
PVS output, prior archived bytes and the existing loaded-array snapshots
agree exactly; watched hot malloc/calloc/realloc calls are zero. This proves
read-only loaded geometry with separate scratch, not concurrent mutable-world
or native-module writes. ASan/UBSan is not a race detector.

Pinned component timing uses cores 0-7,12-19, prepared binaries, coordinated
compiler holds and no debugger. These are not whole-frame measurements.

| Workload | A1 / B1 / B2 / A2 median (us) | A1 / B1 / B2 / A2 p99 (us) |
| --- | --- | --- |
| Q1 CLIENT, 48 queries | 190.134 / 140.263 / 140.563 / 189.104 | 293.637 / 156.934 / 203.404 / 235.885 |
| Q3 CLIENT, 48 queries | 593.883 / 294.116 / 294.962 / 597.667 | 667.274 / 368.458 / 372.108 / 640.654 |
| Native Q2 classic, 48 queries | 8.900 / 9.460 / 9.300 / 9.020 | 9.110 / 9.571 / 14.590 / 9.191 |
| Native Q2 rerelease, 48 queries | 8.795 / 9.230 / 9.220 / 8.630 | 14.130 / 9.660 / 10.530 / 11.991 |

Q1 and Q3 paired medians improve about 25.95% and 50.6%. Q1 uses 100 warm-up
batches and 600 measured batches; Q3 uses 200 and 600. Native SDK queries use
120 warm-up batches and 600 samples. All native phases preserve the same
256-byte trace/touch output with zero native read/borrow/heap calls. Current
candidate medians satisfy the 9.6-us target; they are slower than this pair's
baseline, so no native speedup is claimed. The earlier standalone pose timing
recorded Q3 +1.38%; this adoption does not claim every isolated role operation
became faster.

Remaining exact bypasses: renderer leaf traversal at
`src/render/scene/world.c:768,811,986` and
`src/render/scene/world/legacy/lighting.c:603` is the next retained-geometry
migration. Q3 snapshot growth at `src/network/q3/peer_internal.h:18`, frontend
cold projection validation and legacy saved-projection cleanup are outside
this query slice. The old content fingerprint and whole-frame allocations
remain explicitly open. No full game, legacy-server interoperability,
original-module gameplay, audio or qualified install is claimed here.

Evidence bundles:

- `/tmp/qa-the344-common-client-pose-build-20261009`: exact source, three build/check logs and current native ABBA.
- `/tmp/qa-the344-q1-query-adoption-20261009`: query/leaf/stock oracle comparisons and quiet timing receipt.
- `/tmp/qa-the344-q3-query-adoption-20261009`: query/wire/allsolid comparisons and quiet timing receipt.
- `/tmp/qa-the2873-pose-live-refresh-20261009`: refreshed eight-variant role/link parity matrix.
- `/tmp/qa-the2873-common-query-concurrency-20261009`: current public serial/concurrent rerun and provenance.
