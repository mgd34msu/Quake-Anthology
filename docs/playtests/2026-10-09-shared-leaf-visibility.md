# THE-2874 / THE-873: load-sized shared leaf visibility

One world-owned visibility arena now holds the derived leaf projections and area/cluster bitmaps. It is sized from actor capacity and the loaded geometry, reserved and sealed during load, and replaced at geometry admission. The old dynamically grown leaf arrays, returned-array APIs and all their callers are removed together.

The common aggregate preserves each protocol's original projection: Q1's first 16 non-solid leaf occurrences, Q2's first-128/16-cluster/headnode rules, and the distinct, ordered or sorted cluster rules used by the Q3 adapters. Scalar and bitmap visibility use one row selector. Actor bounds, storage serial and map ownership invalidate the derived result; authoritative body fields remain live. Prediction resets invalidate occupied entries in the existing reset loop. No full-arena clear runs each prediction frame.

The aggregate is borrowed until a changed query, reset, release, rebind or geometry admission. Mutable world calls retain their existing single-thread owner. Separate caller scratch sets can query immutable geometry concurrently.

## Memory and behavior

The hot body shrinks from 624 to 544 bytes; a 256-slot actor page shrinks from 173,056 to 152,576 bytes. The separate cache costs 928 bytes per actor. At 16,384 slots, the visibility arena payload is 19,185,664 bytes for e1m1, 18,186,240 for base1 and 17,154,048 for q3dm1. These figures exclude allocator headers and geometry/scratch storage; this trades reserved load memory for allocation-free queries.

Eight GCC/Clang plain and ASan/UBSan artifacts pass on four retail maps and two DAG fixtures. All 42 nonreference output comparisons match exactly. Each run exercises the actual Q2 recipient function across 24,576 cases per map, the changed Q3 projection bodies, independent query bounds, checkpoint/restore, prediction reset, live field rebinding, slot reuse and map admission. First and repeated queries make zero malloc/calloc/realloc/free calls. Twenty strict translation-unit checks pass. Native/SDK rows in these component fixtures are controlled; they do not establish retail-original module gameplay.

Full production, ASan/UBSan and allocation-instrumented builds pass all seven configured core suites. The actual owned native Q2 SDK fixture preserves its 256-byte trace/touch result throughout the pinned ABBA comparison. Each measured batch contains 48 queries, after 120 warm batches, with 600 measured batches, no debugger or profiler, and affinity `0-7,12-19`.

| Role | Phase | Median µs/batch | p99 µs/batch |
| --- | --- | ---: | ---: |
| Classic | A1 | 9.161 | 12.620 |
| Classic | B1 | 8.460 | 8.570 |
| Classic | B2 | 8.610 | 12.890 |
| Classic | A2 | 9.071 | 14.801 |
| Rerelease | A1 | 8.651 | 11.141 |
| Rerelease | B1 | 8.570 | 8.700 |
| Rerelease | B2 | 8.580 | 12.040 |
| Rerelease | A2 | 8.650 | 14.520 |

All candidate medians remain below the 9.6 µs THE-2861 baseline. Query batches make no controller heap, native read or native borrow calls. Component cache-hit timing improves large foreign-world projections, but small local Q2 metadata changes from 17.19 to 27.10 ns. Those batch means are not game-frame medians or p99; no frame-time speedup is claimed.

## Engine checks and remaining work

Four private headless-Wayland CPU640 sessions use fresh copies of the owner's saved profile: Q1 e1m1, Q2 rerelease base1, Q3 q3dm1, and Q1 start with Q3 movement/character. Each reaches gameplay, produces an inspected screenshot and quits normally after 1,000 frames. The original profile is unchanged and every recorded owned process is gone. Dummy audio supplies no sound proof, and these runs do not qualify an installation.

Whole-frame allocation remains nonzero across 878 measured frames: Q1 27,453 calls, Q2 rerelease 409,304, Q3 321,730 and combined 243,766. Null results, size overflow, capacity exhaustion and lost records are zero. THE-2874 remains open; these totals are observations, not an identical-tick performance comparison.

Evidence: `/tmp/qa-the2874-membership-aggregate-20261009/{report.json,runtime.json,strict.json,final-timing-summary.json,cold-storage.json}` and `/tmp/qa-the2874-membership-build-20261009/{source/source-attestation.json,native-broadphase/abba-results.json,live/results.json}`. The staged build-input tree is `587ded6b4d25079d6a5f6aa688b5455082cb186c`. No new qa-c installation.
