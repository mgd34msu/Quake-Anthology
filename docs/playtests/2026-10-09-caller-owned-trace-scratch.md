# Caller-owned trace and visibility scratch

THE-2873 moves collision query buffers out of loaded Q1/Q2/Q3 geometry. Each caller owns load-created scratch; trace and point-contents APIs take const geometry. Local, Source, remote, unified, prediction and native SDK callers use that API. Main and child views have separate visibility scratch.

One `qa_stamp_set` replaces the eight hand-written visit-epoch implementations in collision, visibility and bot routing. Bot route storage is prepared with the graph. Q1 uses the existing arena with load reservations and sealing; its bounded clip cache belongs to the caller. The old query buffers and epoch implementations are removed in this slice.

Verified build-input tree: `fc4c49036d545badfadf4c0e680325aafcde1279`. Production and ASan builds passed all seven core checks. Integration found a small-map reservation defect: the unchanged one-node movement fixture needed 3,291 bytes but had 3,072. Load sizing now adds 4,096 bootstrap bytes; the fixture passes without query growth or test changes.

## Behavior evidence

- Public geometry: Q1 start/e1m1, rerelease e1m1, Q2 and rerelease base1, Q3 q3dm1, and a valid Q3 empty-VIS variant. All 21 baseline/production/ASan runs produce identical trace, contents, leaf-list, fat-PVS and full PVS/PHS bytes. Two independent scratches run 4,096 concurrent queries per map without changing the common geometry snapshot or calling the wrapped heap functions.
- Native SDK: all six game/client/UI roles pass on the owned backend under production and ASan. Game-state bytes, Q2/Q3 traces, query roles/order/capacity, and live Source mutations without re-linking retain their existing behavior. Sampled native reads, borrows and controller heap calls are zero in the warm query batch.
- Visibility: six retail maps retain identical surface IDs, masks and bounds across 512 view conditions and matching passes; independent callers and forced epoch wrap pass.
- Frontend lifecycle: local, Source, remote and unified scratch pairs have independent seat/child storage. Allocation-failure cleanup preserves the previous pair and retires temporary ownership.
- Bot routing: 851 routes retain identical output; graph preparation removes the two first-query allocations in this bounded workload.

Evidence folders beneath `/tmp/`: `qa-the2873-scratch-build-20261009`, `qa-the2873-public-geometry-20261009`, `qa-the2873-integrated-fixture-20261009`, `qa-the2873-q1-small-map-20261009`, `qa-the2873-visibility-20261009`, `qa-the2873-frontend-lifecycle-20261009`, and `qa-the2873-bot-routing-20261009`. Component packets also cover Q1/Q2/Q3 kernel parity, simultaneous callers and epoch wrap.

## Pinned timing

This correctness slice does **not** claim a speedup. THE-2861's 9.6 µs target remains open. ABBA uses the unchanged 48-query batch, 120 warm-up batches and 600 measured batches per phase, sequentially pinned to `0-7,12-19`, without a debugger or overlapping qualification/build jobs. A is the retained historical SDK; B is this integrated live-field build.

| Edition | Phase | Median µs | p99 µs |
| --- | --- | ---: | ---: |
| Classic | A1 | 9.790 | 10.130 |
| Classic | B1 | 11.160 | 15.520 |
| Classic | B2 | 10.811 | 14.400 |
| Classic | A2 | 9.910 | 13.260 |
| Rerelease | A1 | 9.571 | 9.770 |
| Rerelease | B1 | 10.540 | 10.830 |
| Rerelease | B2 | 10.540 | 13.380 |
| Rerelease | A2 | 9.771 | 10.080 |

Raw samples and commands are in the integrated fixture folder. Every phase exits normally and its query-result bytes match A.

### ID-only role projection

The next THE-2861 step requests only collision roles from the same live decoder and resolves each raw area-query row once. Full getters retain their original records. In the identical 48-query batch, instrumented ID queries read solid 32 times; flags, model and owner reads each fall from 32 to zero. The instrumentation is excluded from timing runs.

Production/ASan builds and seven core checks each pass. All six SDK roles, full getter behavior, role/order/count/capacity and Source changes without re-linking pass. Source tree: `68389bd8c6f6787861ee43506b99967747376665`; packet: `/tmp/qa-the2873-role-fields-20261009`.

| ABBA comparison | Edition | A1 / A2 median µs | B1 / B2 median µs | A1 / A2 p99 µs | B1 / B2 p99 µs |
| --- | --- | --- | --- | --- | --- |
| Historical / role projection | Classic | 9.510 / 9.460 | 10.350 / 9.970 | 15.550 / 9.690 | 15.510 / 13.831 |
| Historical / role projection | Rerelease | 9.510 / 9.520 | 10.530 / 10.110 | 16.240 / 12.861 | 13.720 / 10.541 |
| Live scratch / role projection | Classic | 10.850 / 10.480 | 10.250 / 9.720 | 14.501 / 18.160 | 10.401 / 9.930 |
| Live scratch / role projection | Rerelease | 10.390 / 10.490 | 10.165 / 10.060 | 14.530 / 15.050 | 18.190 / 13.641 |

This reduces the measured live-query cost, but the 9.6 µs acceptance target remains unmet. Raw samples are in `abba-results.json` and `live-abba-results.json`; all 16 phases exit normally with equal query-result bytes and zero sampled native read/borrow/controller heap calls.

## Bounds and remaining work

Upfront PVS/PHS preparation costs load time and retained memory. Single cold-load observations, rather than latency medians, show Q1 start 3.924→30.099 ms, classic e1m1 3.618→28.347 ms, rerelease e1m1 4.405→54.275 ms, and Q3 q3dm1 7.807→14.705 ms. One pre-reserved Q1 caller retains about 10–11 MB of allocator storage; the historical load measurement excludes its later lazy query arenas, so this is not a net-increase claim.

Generated whole-world diagonal Q1 cross-policy queries can exceed the sealed reservation. They count overflow; full-envelope fidelity is not claimed. Existing libc `qsort` may allocate internally, outside executable-level heap wrapping; THE-2874 must cover it. The zero-allocation results above apply to the measured query workloads.

Canonical contents and surface flags at load remain part of THE-2873. Current explicit portal, area, material, resource and reference-count updates retain their existing state ownership. No game-window/audio, full campaign, or legacy-network interoperability claim follows from these component checks. Installation qualification is recorded separately.
