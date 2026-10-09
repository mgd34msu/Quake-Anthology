# Retained recipe resources

THE-2874 removes the per-call resource-history proof from `qa_executable_recipe_current`. A published recipe already owns each admitted resource and retains its pool, catalog and VFS views. Its current check still checks the catalog generation, pool and lookup policy. Closing the recipe releases those resources. No resource hash or replacement cache is added.

The removed loop normalized paths and searched history/origins for every held resource on repeated live calls. Actual old/new ownership fixtures cover parent destruction, history clearing and trimming, final release, file append, lookup-policy changes and catalog/pool changes. GCC and Clang plain and ASan/UBSan runs preserved those results. The existing changed-policy rejection remains; one redundant resource rejection is removed and no failure path is added.

The isolated source `84bc7b01d3adea3ff5627c7cf87c8fd43949022c`, based on `873128bf`, passed full production, ASan and allocation-instrumented builds and all seven configured core checks in each.

Production Q1 classic e1m1 CPU 320×200 was measured before/after/after/before. Each run uses a fresh copy of the same owner profile, caps and swap interval zero, affinity `0-7,12-19`, 1,798 warm presents and 600 measured consecutive presents. Timers and allocation instrumentation are off; no debugger or other owned heavy workload ran. The same retained fixed-memory SDL present observer records both artifacts.

| Run | Median ms | p99 ms |
| --- | ---: | ---: |
| Before 1 | 9.146 | 11.390 |
| After 1 | 1.594 | 3.920 |
| After 2 | 1.610 | 4.010 |
| Before 2 | 9.430 | 11.977 |

Actual frame reports establish e1m1, one client and a 320×200 drawable. Root inspected the before/after PNGs showing the same spawn view. Pixels and simulation clocks are not claimed identical. All runs quit normally; owner files remained unchanged and recorded owned process tokens were absent after cleanup. This is a stationary present-interval comparison on private headless Wayland/pixman, not a GPU, movement-feel, sound or campaign claim.

An initial tiled-compositor run returned a 636×372 drawable and was excluded; its receipt is retained. Evidence is under `/tmp/qa-the2874-recipe-current-20261009`, `/tmp/qa-the2874-recipe-isolated-build-20261009` and `/tmp/qa-the2874-recipe-frame-20261009`.

The whole-frame allocation gate still has other callers to migrate. This slice does not install a new owner executable.
