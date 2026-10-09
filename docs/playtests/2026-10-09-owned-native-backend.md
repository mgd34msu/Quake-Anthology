# THE-2861: owned native backend and live fields

The SDK and application now construct native modules through the same owned
process backend. Process resources live beneath `native_host`; the legacy pipe
runner, helper protocol and libffi path are deleted. Broad-phase queries read
resolved fields directly from owned module backing instead of copied link-time
state. QuakeC and Q3 entity bindings use the same field decoder.

Implementation commits: `21313625`, `62ccfea7`, `38facbb3`. The latter two fix
fresh emulated CPU SSE setup and valid SDK client command delivery through the
common console. `24b98093` prepares private runtime mount points for qualifying
the renamed helper without modifying installed files.

## SDK proof

All six actual Source ABI fixtures pass production and ASan-controller runs:
Q2 classic game; Q2 rerelease game and cgame; ELF32 Q3 game, cgame and ui.
Each uses two isolated instances, an actual dependency library, file input,
temporary files, Source save/restore, environment/current directory, clocks,
calendar and entropy. Rerelease JSON save exports and Q3 cgame command
registration, dispatch and retirement also pass. Source modules and the
production helper are not sanitizer-instrumented.

Four Q2 game-state records match the historical SDK byte for byte. Real time
and entropy are checked independently. Q3 body clipping uses the original
`CONTENTS_BODY` and `MASK_SHOT` rules, not the fixture's earlier incorrect solid
mask. Controlled QC/QVM field-reader checks cover live writes, slot lifetimes,
collision admission and restore; they are not retail campaign proof.

An additional Q2 Source command changes origin and solidity without relinking,
inside the same linked area. The new backend immediately reads the new origin,
returns the expected trace fraction and stops colliding with `SOLID_NOT`. The
historical SDK returns stale values. Both editions and both instances pass in
production and ASan-controller runs. Counted native reads, borrows and
controller heap calls are zero during those queries.

## Measurement

Sequential ABBA runs use affinity `0-7,12-19`, no debugger, 120 warm batches and
600 measured batches per run. Each batch contains 48 trace/touch queries in a
small, unchanged world. Exact result bytes and candidate order match.

| Edition/backend | Median us, first/second run | p99 us, first/second run |
| --- | ---: | ---: |
| Q2 classic, historical | 9.665 / 9.610 | 9.830 / 19.660 |
| Q2 classic, live fields | 13.125 / 12.960 | 24.780 / 17.330 |
| Q2 rerelease, historical | 9.620 / 9.530 | 9.760 / 9.720 |
| Q2 rerelease, live fields | 12.870 / 13.620 | 23.921 / 18.290 |

No speedup was measured. The historical path serves copied state; the new path
samples live fields. Both report zero counted native reads, borrows and
controller allocations in this fixed workload. These numbers do not establish
retail frame-time improvement or AD/e1m1 combat performance.

## Installed artifact

Binary source `38facbb3`, built October 9 at 05:04:56 CDT, was installed at
05:24:57 through `tools/install_qualified_build.py`. All five installed files
passed direct byte comparisons. Sixteen accepted private headless Wayland
scopes cover copied-profile menu defaults, menus and five native game families
on CPU/GL, and empty-content menus. Actual world/HUD PNGs were reviewed and
normal quits recorded. Q3 GL needed a 20-second capture; its initial
three-second awaiting-snapshot screen was not accepted as gameplay.

The owner's 42 profile files are unchanged. No recorded test process remains.
Audio was contained through dummy delivery. This qualification does not prove
physical input, audio cues, legacy multiplayer, complete persistence or every
external mod. The initial Q1 autosave warning is recorded on THE-2845.

Raw logs, source fixtures, timings, screenshots and receipts are indexed in
the [THE-2861 evidence comments](https://linear.app/the-artificery/issue/THE-2861).
