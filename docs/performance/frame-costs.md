# Native frame costs

The measured Q3 CPU frame is still too slow: **79.971 ms**, including
**49.511 ms** of raster execution. The latest GL measurement takes **31.228 ms**, with material
submission and presentation as its largest remaining measured costs.

## Workload

These measurements use native Q3 `q3dm1`, 640×480, seed 2246822519,
camera position `(212, 2360, 56.125)` and angles `(0, -45, 0)`.
Simulation advances by exactly 50 ms per frame. Each run warms up for 30
frames, then records 30 individual complete frames without attack input.
The machine has a Ryzen 9 5900X, 24 logical CPUs, and an NVIDIA RTX 5060 Ti
using driver 610.57.04. GL reports the NVIDIA hardware renderer; its actual
SDL swap interval is 1.

Wall time surrounds the actual frontend step. Calling-thread and aggregate
process CPU clocks cover the same interval. Frame reports, GPU query results
and final pixel capture are collected outside that interval. Builds use
optimized production GCC and Clang targets with warnings treated as errors.

## Complete frame results

Times are medians in milliseconds. Rows from `2b5e343c` use identical timed
diagnostic code, including material scopes. Earlier rows use fewer scopes;
their comparisons include the later diagnostic overhead.

| Commit | CPU frame | CPU raster | CPU process time | GL frame |
| --- | ---: | ---: | ---: | ---: |
| `af139299` | 202.948 | 174.854 | Not recorded | 30.755 |
| `3a1b201b` | 182.777 | 153.645 | Not recorded | 31.014 |
| `20d077b4` | 184.117 | 154.652 | 183.060 | 31.314 |
| `094a0d3f` | 137.570 | 110.106 | 180.096 | Not run |
| `8521761b` | 114.061 | 85.384 | 144.310 | 29.512 |
| `2b5e343c` | 108.890 | 78.267 | 154.229 | 30.074 |
| `b91eb2eb` | 89.707 | 59.141 | 179.565 | 29.074 |
| `1a132170` | 90.725 | 59.263 | 179.188 | 31.228 |
| `69dd5649` | 79.971 | 49.511 | 152.860 | Not run |

On `b91eb2eb`, the nearest-rank 95th percentile is 91.594 ms CPU and
29.934 ms GL. The wider pool lowers elapsed time while increasing aggregate
CPU work. A process snapshot confirms 23 CPU raster workers plus the caller;
it does not establish each worker's individual contribution.

## Current costs and optimization targets

These are inclusive medians on `69dd5649` for CPU and `1a132170` for GL.
The later change affects only CPU rasterization. Nested durations overlap, so the
rows must not be added together.

| Scope | CPU ms | GL ms |
| --- | ---: | ---: |
| Scene construction | 16.559 | 17.945 |
| Material submission, 554 calls | 11.665 | 12.782 |
| World submission, including its materials | 11.300 | 12.359 |
| Final scene sorting | 0.078 | 0.079 |
| Renderer execution / GL submission | 49.511 | 2.074 |
| SDL presentation / swap | 10.477 | 8.385 |

The first GL baseline separately measured 3.596 ms median GPU elapsed time
around renderer execution. GPU intervals overlap CPU work and presentation;
they are not additional complete-frame milliseconds.

Reducing Source vertex storage, adding canonical texel lookup tables and
sharing triangle preparation produced no complete-frame improvement on
`1a132170`. The final frame contains no Source vertex arrays, so full Source
array copies were not a cause of this native workload's cost. The lookup
tables and triangle preparation also have non-Source callers.

The guarded opaque fragment path on `69dd5649` reduces both elapsed raster
time and aggregate CPU work. Preparing material constants once per stage and
skipping unused texture derivatives are committed on `f143350a`; their matched
CPU and GL measurements are pending. The next scheduling investigation targets
the barrier between individual raster draws while preserving pixel order.

## Fidelity and limits

Each completed iteration preserves all 30 recorded gameplay states, actor
identities, source clocks and draw/index counts against its renderer's
baseline. Final RGBA bytes also match exactly within each renderer:

- CPU: `e8dbb9cd341bfc270ce00104905454fdfd717df440721207db4170ba3f0b56e7`
- GL: `46c7ef0a833be8f00e91b4f0ea96f91cf5d132cbe407486748e63724d21dd2a6`

CPU and GL images differ. These checks establish preservation of this fixed
workload, not cross-renderer parity, campaign coverage or general playability.
Peak RSS is a whole-run measurement: the bounded archive comparison reduces
the CPU run from 7,623,504 KiB on `8521761b` to 6,260,920 KiB on `2b5e343c`.
It does not measure steady-frame memory use or isolate every allocation.
