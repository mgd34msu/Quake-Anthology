# THE-2861 / THE-2874: live fields and fixed spatial snapshots

Spatial captures reserve their actor lists and nesting frames when the world is
created. Queries borrow and return frames from that fixed pool. The small stack
capture remains available; deeper captures no longer allocate or resize. Restore
clears links while retaining the pool. Exhaustion returns the existing capture
failure and increments an overflow counter.

The caller supplies the nesting budget. Application worlds currently reserve 32
frames; the callback-free prediction world reserves one. The 32-frame choice is
a bounded policy, not evidence that arbitrary native callback recursion fits it.
Capacity, active frames, peak depth and overflow are observable.

Shared body-field descriptors resolve complete spatial layouts when the adapter
binds addresses. Sampling still reads live module words. Sparse and unprepared
layouts retain nullable reads; no position, bounds or collision values are cached.

## Verification

The exact staged source tree was built in production and with ASan/UBSan. The
seven core checks passed in both builds. All six actual native SDK role fixtures
passed in production and with an instrumented controller: Q2 classic game,
Q2 rerelease game and cgame, and Q3 game, cgame and UI. Their 48 stable gameplay
records match the prior fixture output. Guest modules and the process bootstrap
were not instrumented by the controller sanitizer build.

The spatial fixture passed GCC and Clang, plain and sanitized. It preserved the
order of 17 mixed-family candidates through nested queries, actor retirement and
replacement, and checkpoint restore. It performed 1,002 nested captures with zero
watched query heap calls. Deliberate pool exhaustion was counted once without
truncating a successful capture.

The live-field fixture passed 3,270 comparisons per compiler variant, including
unaligned and noncontiguous words, sparse layouts, source mutations without
rebind, pose selection and failed-reference output preservation.

Evidence packets:

- `qa-the2861-layout-the2874-pool-build-20261009`: full builds and core checks.
- `qa-the2861-spatial-layout-20261009`: descriptor and sampler checks.
- `qa-the2874-spatial-pool-20261009`: nesting, exhaustion and restore checks.
- `qa-the2861-spatial-layout-final-20261009/integration-summary.json`: SDK role
  comparisons and pinned timing data.

## Pinned timings

The unchanged workload performs 48 queries per batch, with 120 warm-up batches
and 600 samples per phase. Runs used affinity `0-7,12-19`, sequential A/B/B/A,
and no debugger. A is the retained pre-owned-backend reference; B is this slice.
Each cell is median / p99 in microseconds.

| Game | A1 | B1 | B2 | A2 |
| --- | --- | --- | --- | --- |
| Q2 classic | 9.470 / 9.670 | 10.060 / 15.711 | 9.590 / 13.321 | 9.821 / 15.871 |
| Q2 rerelease | 9.660 / 13.940 | 9.370 / 9.721 | 9.470 / 12.690 | 9.630 / 9.840 |

All eight query result records were byte-identical. Warm controller heap calls,
process memory reads and borrow requests were zero; direct mapped live reads
remain active. The classic B1 median exceeds 9.6 us, so THE-2861's performance
close-out remains open. These results do not claim a full-frame speedup or a
zero-allocation frame loop. Other frame-path allocations still require migration
and measurement under THE-2874, THE-873 and THE-882.
