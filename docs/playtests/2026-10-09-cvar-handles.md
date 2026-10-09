# THE-2859 cvar handle migration

The production artifact at `2fc05038dc6f7dde439251b0b872bde907b01fa6`
was built at 00:11:46 CDT and installed as `qfiles/qa-c` at 00:45 CDT.
Production and ASan builds passed the seven CTest checks: platform services,
core, archive, VFS, BSP, image and model. The latest migration commits are
`a4300449`, `91a1ebf5`, `5cab93f6`, `4e0a1088`, `2b2deaa6` and `2fc05038`.

The final diagnostic artifact sampled 600 gameplay frames after 600 warm
frames in each case. Every case quit normally at the public frame limit.

| Content | Map | Named cvar reads in sampled frames |
| --- | --- | ---: |
| Q1 classic | e1m1 | 0 |
| Q1 rerelease | e1m1 | 0 |
| Q2 classic | base1 | 0 |
| Q2 rerelease | base1 | 0 |
| Q3 | q3dm1 | 0 |

Counter evidence is in `cvar-count-result.json` under these local bundles,
in table order: `/tmp/qa-private-av-x2grs4db`,
`/tmp/qa-private-av-jhcbp5q2`, `/tmp/qa-private-av-7cwgr7q_`,
`/tmp/qa-private-av-ziyjtrgk`, `/tmp/qa-private-av-opslgek7`.
The observer unwinds named-read callers and makes these runs unsuitable for
timing. These stationary native runs do not cover every mod syscall or action.
The component parity packets separately cover Source edits, modification
counts, latches, imported bindings and Q3 handles.

## Production timing

The whole frame has not become faster in this comparison. Q1 classic e1m1,
CPU 320x200, was run sequentially before/after/after/before, pinned to
`0-7,12-19`, with caps zero, timers off and no debugger. Each run measures
600 successful present intervals after 1,198 warm presents. Both actual
stationary views were inspected. The full 42-file owner profile was copied;
the original remained unchanged.

| Run | Source | Median ms | p99 ms |
| --- | --- | ---: | ---: |
| Before 1 | 38963179 | 8.023 | 9.343 |
| After 1 | 2fc05038 | 8.207 | 9.500 |
| After 2 | 2fc05038 | 8.230 | 9.374 |
| Before 2 | 38963179 | 7.913 | 9.152 |

These are observational present-to-present timings, not CPU self time or
exact simulation/RNG comparisons. They remain above the 2 ms CPU320 target.
Raw evidence: `/tmp/qa-the2859-production-measurements-20261009/q1-cpu320-abba.json`.

The existing catalogue fixture shows a bounded access-cost improvement.
Root reproduced the GCC/Clang ABBA runs after AV checks ended, with identical
58,480-byte behavior/save outputs in all eight runs. GCC's warm due-check
median/p99 batch-average ns per access was 37.534/41.030 to 9.749/10.964
for shared Q1 and 39.367/40.969 to 9.265/11.841 for native Q3. This normally
infrequent check uses controlled Source/clock services and extracted functions;
its timings do not establish an installed-game FPS gain. Evidence:
`/tmp/qa-the2859-catalogue-controls-timing-20261009/root-reproduction.json`.

## Installation proof and remaining work

`tools/install_qualified_build.py` installed the exact five qualified files.
Sixteen copied-owner-profile private Wayland scopes cover CPU and llvmpipe GL
menus and all five native content editions, with normal exits and owned-PID
cleanup. Root reviewed actual PNGs. Q3 GL's first short capture still showed
its introduction; the 20-second rerun reached the retail world and replaced
that case's evidence. Dummy audio provides no sound proof; these captures
provide no physical GPU timing, input or campaign proof.

The installation receipt and reviewed qualification are under
`/tmp/qa-the2859-production-measurements-20261009/` as
`installed-build-verified.json` and `wayland-qualification-reviewed.json`.
THE-2859 is In Review, not Done. The frame-time target remains open.
Next are THE-2876/THE-2861 entity ownership and live columns, then THE-2873:
one caller-owned load-sized stamp set, immutable geometry and deletion of all
eight private epoch implementations. No checker rules are being added.
