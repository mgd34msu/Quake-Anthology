# THE-213 / MIKE-17 classic provider with retail fence geometry

CPU and software GL captured a genuine fence near and far using **classic id1
gameplay and presentation with the unmodified `test/test_shadow.bsp` from the
shipping rerelease id1 PAK**. The coordinator independently viewed all four
near/far window PNGs and confirmed that the red room background remains visible
through the black lattice. This is the alternate retail BSP proof accepted by
the supervisor's Verification comment; it does not reproduce vanilla `e1m1`
or Mike's original unknown location.

The captured installation was `2ada5706`, built at 22:37:27 CDT and installed at
22:45:40 CDT on 2026-10-07. Actual native state recorded product
`q1-classic-id1`, `QA_EDITION_CLASSIC`, program `QA_Q1_ID1`, and source edition
`QA_Q1_CLASSIC`. The selected geometry product was rerelease id1; presentation
remained classic id1. The public launch selected `--game q1-classic-id1`,
`--map-game q1-rerelease-id1`, and `--map test/test_shadow`.

Each PNG captured the owned private window while the game was stopped after
completed presentation. Request phase, frame, scene sequence, and returned
present matched. Both scenes submitted two draws using the authored `{FENCE`
texture with `QA_ALPHA_GT666`. Their actual cameras were in BSP air with an
unobstructed submitted fence target in view.

| Renderer | Near frame | Far frame | Closest clear submitted face, near / far |
| --- | --- | --- | --- |
| CPU | 1678 | 1848 | 86.96 / 284.78 units |
| Software GL | 1675 | 1841 | 88.03 / 284.08 units |

Software GL reported Mesa `llvmpipe (LLVM 22.1.8, 256 bits)`, with no hardware GPU
descriptor. CPU and GL cameras differ; these images establish visible holes at
both distances rather than exact camera or pixel equality. Manually selected
PNG samples also distinguish red background pixels from dark lattice bars;
their coordinates and RGB values are retained in the local proof packet.

The retail texture is 64 by 64. Its four authored mip levels contain respectively
2401, 484, 56, and 2 palette-index-255 texels. All four near/far scenes recorded
the same RGBA image metadata and alpha summaries under
`QA_SCENE_LINEAR_MIPMAP_LINEAR` and repeat wrapping:

| Prepared level | Alpha zero | Alpha below 170 | Alpha at least 170 |
| --- | --- | --- | --- |
| 64 × 64 | 2401 | 2401 | 1695 |
| 32 × 32 | 484 | 704 | 320 |
| 16 × 16 | 56 | 198 | 58 |

These are actual submitted texture summaries, with the byte threshold matching
the greater-than-0.666 alpha test. The prepared image exposed these three levels;
the authored 8 by 8 level is asset census data, not a captured renderer level.
Texture summaries support the inspected window images and do not independently
prove visual transparency or the mip selected by each fragment.

THE-883 restores the original Quake submodel bounds expansion in the shared
collision loader. The fence's stored model `*1` bounds are `[705,409,-63]` through
`[959,408,63]`. Every captured spawn, near, and far scene recorded actual admitted
`QA_COLLISION_Q1` bounds `[704,408,-64]` through `[960,409,64]`. THE-894 fixes world
precache ownership: the entry matching `receipt.map_path` retains the selected
map resource instead of reopening it through classic provider content. Ordinary
models and sounds retain their provider content. This genuine mixed launch now
reaches gameplay after the two separately retained startup failures.

Positioning used public `noclip` and actual keyboard input from the reused
inspection route. The copied profile received route bindings, `cl_run=0`,
forward/back/up speeds of `100`, and yaw/pitch speeds of `45`; earlier route
setup commands are also retained. `cl_run` was restored to its original `1`
before quit. Other private route speeds and bindings were not restored. Gamma
was untouched and recorded as `1.0`. No direct pose writes or inferior function
calls were used.

Each run used a fresh copy of all 34 owner settings files, authenticated private
display and audio servers, and actual private audio output. Both public quits
returned zero. Original owner settings and all 24 artifact, package, and helper
stat pins stayed unchanged. Recorded owned PID/start tokens and descendants
were absent after cleanup, and all owned audio/display processes stopped.

Retained evidence uses capture IDs `qa-private-av-sbyfturi` for CPU and
`qa-private-av-coutf1wy` for software GL. Each contains spawn and near/far window
PNGs, matching census/window receipts, `result.json`, and qualification/cleanup
records. The local summary is `qa-the213-classic-retail-fence-proof-20261007.json`;
the frozen reused helper bundle is `qa-the213-classic-retail-shadow-world-fixed-20261007`.

Original classic `start` and `e1m1` contain no authored fence, and the owner retest
remains separate. The earlier classic/MG3 composition's unsupported-classname
failure also remains a separate gap. This proof makes no campaign, GPU0,
performance, sound fidelity, or original WinQuake fence-support claim.
