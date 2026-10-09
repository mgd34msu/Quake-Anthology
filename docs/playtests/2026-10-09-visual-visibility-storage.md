# THE-2874 / THE-873 visibility query storage

Visibility queries now reuse map-sized storage owned by the frontend view, native-character composition or unified network recipient. Parent and clip/mirror views have separate workspaces. Map admission and restore construct them; queries do not allocate them. The three old frame-arena/frame-lease allocations and the public raw-storage API are deleted in this slice.

The one constructor reserves the workspace plus the larger of the map's Q1 PVS size and its Q2 headnode traversal stack. It reserves both possibilities regardless of the currently selected provider. Provider changes on the same physical map cannot require a larger workspace.

Exact old/new/ASan comparisons cover six retail geometries, 768 serial passes and 256 concurrent passes per geometry. Concurrent lanes have separate application/world owners sharing immutable geometry. Nested child queries retain the parent result. These checks compare PVS bytes and 24 actor outcomes directly. Separate Q2 classic/rerelease checks compare 256 PVS/PHS headnode results per map. First and warm preparation calls make no heap allocations. The Q2/Q3 application fixtures exercise foreign-world visibility, not a running native module.

Sequential ABBA component measurements used affinity `0-7,12-19`, 120 warm-up pairs and 600 samples, without a debugger. Each pair prepares parent and child views and queries 24 actors in each.

| Workload | Old median / p99, us | New median / p99, us |
|---|---:|---:|
| Q1 e1m1 PVS and actors | 5.50 / 6.21 | 5.48 / 6.05 |
| Q2 base1 foreign-world visibility | 3.21 / 4.17 | 3.21 / 3.31 |
| Q3 q3dm1 foreign-world visibility | 3.23 / 3.38 | 3.19 / 4.83 |

There is no material median change or speedup claim. Q3's first new run has a higher tail; its second run has p99 3.47 us. These are query timings, not frame timings.

Production, full ASan/UBSan and allocation-gate builds passed all seven core checks. Their frozen source tree is `c65c40388a45b23ce51ed4a087a03ad3c5543f32`. Four private copied-owner-profile CPU runs reached gameplay, rendered an inspected screenshot and quit normally after 1,000 frames:

| Session | Map | Measured successful frames | Heap calls in measured frames |
|---|---|---:|---:|
| Q1 classic | e1m1 | 878 | 27,455 |
| Q2 rerelease | base1 | 878 | 8,474,082 |
| Q3 | q3dm1 | 878 | 321,631 |
| Q1 world with Q3 movement/character | start | 878 | 243,140 |

All four have zero null allocation results, size overflows and reserved-capacity exhaustions. The owner profile remained unchanged and all recorded owned process tokens are retired. These runs use a private headless Wayland compositor and dummy audio. They provide startup/presentation evidence, not audio, interactive play or performance proof. No build was installed.

THE-2874 remains open. The gate still reports the heap calls above. In particular, existing per-body leaf membership allocates on first actor query, with identical old/new counts in the component fixtures. That separate cache is the next slice. Native-character map/video retirement and shared/original restore order were also checked in source. Two older standalone restore helpers have no direct callers; their rollback paths are not claimed as live proof.

Evidence is in `/tmp/qa-the2874-visual-scratch-20261009/`: `REPORT.md`, `comparison.json`, `headnode-comparison.json`, `bench-runs.json` and `bench-summary.json`. Full builds and checks are in `/tmp/qa-the2874-visual-scratch-build-20261009/`; `live/results.json` records exact arguments, artifact identity, captures, containment, profile preservation and cleanup for each run. Fixture executables were removed after preserving source and exact result arrays.
