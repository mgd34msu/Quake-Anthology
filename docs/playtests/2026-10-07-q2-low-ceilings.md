# Q2 crouch release under a retail low ceiling

THE-380 / MIKE-30 low-ceiling posture passed in builtin Q2 classic and
rerelease on CPU and GL. These four cases used installed `fac87ea0`, built
at 20:07:38 CDT and installed at 20:19:09 CDT on 2026-10-07.
The movement and shooting jank part of THE-380 remains unresolved.

Each run copied the owner's 34 saved configuration and JSON files into a fresh
private profile. Bindings were unchanged, saves were excluded, and the original
profile remained unchanged. Game windows used private Xvfb displays, and actual
audio output was captured from private audio servers. No owner display,
speakers, or graphics devices were exposed.

## Retail obstruction and real input

The case used the lower metal support in retail `base1`. The floor top is
`z=-244`; the support spans `x=512..560`, `y=-256..-192`, and
`z=-208..-128`. Its underside leaves a 36-unit gap above the floor.
The retained BSP geometry identifies floor/support brushes `646/312` in
classic and `267/590` in rerelease.

| Body | Mins | Maxs | Height |
| --- | --- | --- | ---: |
| Crouched | `(-16, -16, -24)` | `(16, 16, 4)` | 28 |
| Standing | `(-16, -16, -24)` | `(16, 16, 32)` | 56 |

Positioning was assisted. Public `noclip`, real mouse input, and held W moved
the player to the clear lower approach. Natural traversal to this location was
not established. Public `set cheats 1` and `in_nograb 0` affected only the copied
profile. Noclip was turned off before the player fell onto the retail floor;
ordinary collision remained enabled for every posture phase. Cheats were reset
to zero before quit.

Real C input drove classic `+movedown`; real Control drove rerelease crouch.
The player approached the support with crouch held, stopped beneath it,
released crouch, and then backed out with S. Held classic commands recorded
`up=-400` and `buttons=128`. Held rerelease commands recorded `up=0` and
`buttons=144`, including the crouch bit `16`.

One real one-pixel mouse movement accompanied crouch, followed by four
alternating one-pixel movements after release. RR CPU and both GL cases also
used one real 16-pixel mouse movement before the neutral command sample.
These view inputs did not write movement state directly.

## Neutral release stays ducked until clear

Every accepted release sample had `up=forward=side=0` and `buttons=0`.
Pmove retained flags `5` (ducked and on ground), zero velocity, and the same
origin before and after that neutral command. The actual standing-box query
used identical start/end positions with `maxs.z=32`. It returned
`all_solid=true` and `QA_TRACE_HIT_WORLD` under the support.

Backing clear produced non-solid standing queries, cleared the duck flag,
and restored `maxs.z=32` with flags `4`. The four photographed posture moments
kept the feet within 0.125 units of the same retail floor in every case.

| Edition and backend | Neutral command sequence | Blocked standing traces | Clear standing traces | Recovery | Public, client, GDB exit |
| --- | ---: | ---: | ---: | --- | --- |
| Classic CPU | 413 | 13 | 5 | `maxs.z=4 → 32` | `0, 0, 0` |
| Rerelease CPU | 215 | 1 | 4 | `maxs.z=4 → 32` | `0, 0, 0` |
| Classic GL | 416 | 18 | 3 | `maxs.z=4 → 32` | `0, 0, 0` |
| Rerelease GL | 217 | 1 | 3 | `maxs.z=4 → 32` | `0, 0, 0` |

All listed blocked traces were successful queries with `all_solid=true` against
WORLD. Read-only GDB observed the actual body, Pmove commands, and shared
`qa_world_trace` requests/results. The observer made no inferior calls or state
writes. Existing duck logic in `src/movement/q2/classic.c` and
`src/movement/q2/rerelease.c` retained the crouched body while standing was
blocked, consistent with the stock Q2 and rerelease movement rule. This bounded
posture case required no engine change.

## Evidence and review

Private evidence references use bundle names, without workstation paths.
Each runtime bundle contains `user/evidence/result.json`, raw posture and
trace records, four PNGs, the host `result.json`, copied-profile receipts,
private audio output, and recorded process cleanup.

| Scope | Private runtime bundle |
| --- | --- |
| Classic CPU | `qa-private-av-xt91se_k` |
| Rerelease CPU | `qa-private-av-d0ifrlos` |
| Classic GL | `qa-private-av-3wwfdhkk` |
| Rerelease GL | `qa-private-av-mmmbgzwc` |

The consolidated record is
`qa-the380-low-ceiling-gl-20261007/low-ceiling-readout.json`, with its
`READOUT.md`, exact latest supervisor comment in `latest-verification.json`,
and per-case pin receipts. Earlier incomplete packets are preserved and
excluded from the four passes.

Both GL cases reported actual `Mesa` vendor and
`llvmpipe (LLVM 22.1.8, 256 bits)` renderer, with
`4.6 (Compatibility Profile) Mesa 26.2.2-arch1.1` and GLSL `4.60`.
The observer read the retained GL capability strings populated from the driver.
These were software GL cases; NVIDIA GPU0 was not exercised.

The capture worker inspected all 16 accepted PNGs as actual retail gameplay.
The coordinator additionally inspected six CPU symptom/recovery cuts and four
GL neutral-release/recovery cuts.

All three installed artifacts, the exact installation receipt, and the current
and earlier helpers remained unchanged across the captures. The final receipt
records 40 unchanged file pins. Original owner settings remained unchanged,
and every recorded owned process was absent after cleanup. Every accepted case
quit through the public console and returned zero.

This record supplies the missing low-ceiling posture evidence for the four
edition/backend scopes. Debugger observation supplies no timing result, and
captured private audio supplies no sound-fidelity qualification. Movement and
shooting jank, cause 4, and separate cadence evidence remain open. THE-380 is
not closed by these posture passes.
