# THE-196 / MIKE-14: shipped Q2 rerelease sound check

The current installed `98225ced` passes the bounded native Q2 rerelease
`base1` CPU sound-delivery check below. It was built at
18:32:53.854360 CDT and installed at 18:39:19.680938 CDT on 2026-10-07.
THE-196 remains In Progress: neither CPU frame-time target is met.

## Current installed check

The client used a fresh copy of all 34 owner settings, a private display
and captured SDL PulseAudio output on a private null sink. The original
profile and installed executable/runtime helpers stayed unchanged. Public
`quit` returned zero; all 12 recorded owned PID/start tokens were absent
afterward. No audio reached the desktop or hardware sound devices.

Real Space, using the saved `+moveup` binding, raised the player
42.943199 units. Real Mouse1 produced three owner blaster muzzle events.
The authored cells pickup granted item 36 from 0 to 50. Pickup and door
transit used public noclip positioning; ordinary collision-only navigation
of those routes is not qualified by this check.

All 48 accepted, nonzero SDL cuts match their retained private monitor
slices byte for byte: 351,980 selected bytes within an 8,640,000-byte
recording. Queued source and output are both 48 kHz. The coordinator
independently repeated these direct byte comparisons.

| Target | Retained asset | Resource ID | Live target windows in accepted output |
| --- | --- | ---: | ---: |
| Jump | `players/male/jump1.wav` | 417 | 8 |
| Blaster | `sound/weapons/blastf1a.wav` | 426 | 8 |
| Cells pickup | `sound/misc/am_pkup.wav` | 479 | 8 |
| Door start | `sound/doors/dr1_strt.wav` | 586 | 8 |
| Door end | `sound/doors/dr1_end.wav` | 588 | 8 |
| Moving-door loop | `sound/doors/dr1_mid.wav` | 587 | 8 |

Each counted target window records its actual asset/resource, positive
stereo and effects gain, live prepared position and a 64-sample prepared
window containing nonzero values inside the source interval of an accepted
monitor-matched cut.
The moving loop shares the eight door-start cuts; retired or replaced
loop snapshots are excluded. Eight incidental ammo cuts on the door
route account for the sixth output group and the 48-cut total.

Captured output directly proves mixed delivery. The target metadata and
nonzero paint/PCM arithmetic support each target's contribution, but do
not measure an isolated accumulator delta or isolated final cue. Generic
weapon-pickup audio, latency, subjective fidelity, all events, Original
modules, classic Q2 and GL sound coverage remain outside this check.

All 910 observed Source tick transitions match 25 ms / 40 Hz. Clock fields
match the returned-state predicate, and 917 intermediate client frames
are recorded. These debugger-assisted observations establish functional
cadence, not quiet frame-time performance or movement feel.

Evidence: `qa-private-av-wk79hw6_` contains the event log, prepared sample
windows, accepted raw buffers, monitor, client/parent results,
`root-audio-review.json` and `final-owned-cleanup.json`.
`qa-the196-current-audio-20261007/target-evidence-summary.json` indexes
the target windows and excluded snapshots. No production audio change
was required or claimed by this check.

## Earlier installed check

Selected sound delivery passes on `41861a12`, built 2026-10-07 at
07:59:29.410595 CDT and installed at 08:12:00.553790 CDT. THE-196 remains
open for CPU frame cost, interpolation/choppiness and Mike's retest.

One native Q2 rerelease `base1` CPU client used a fresh copy of the owner's
34 saved settings. Actual SDL PulseAudio output stayed on a private null
sink and monitor; no hardware sound devices or descriptors were available.
The display was private too. The original profile and the installed
executable/runtime helpers remained unchanged.

Real Space, bound to `+moveup`, raised the player 42.4476 units. The Source
emitted `*jump1.wav`, resolved to `players/male/jump1.wav`, with positive
mixer gain. This matches rerelease `p_client.cpp:3316`, where `pm.jump_sound`
starts `*jump1.wav`. Real mouse attack produced three owner blaster muzzle
events and corresponding `sound/weapons/blastf1a.wav` voices.

The authored `ammo_cells` entity, actor `1:110:0`, granted inventory item
36 from 0 to 50 cells and emitted `misc/am_pkup.wav`. This contact occurred
during public `noclip` positioning. Door traversal also used that assisted
route. These checks prove authored event delivery; they do not qualify
ordinary keyboard traversal of either route.

| Cue | Accepted nonzero SDL buffers found byte for byte in the private monitor |
|---|---:|
| Jump, `players/male/jump1.wav` | 8 |
| Blaster, `sound/weapons/blastf1a.wav` | 8 |
| Cells pickup, `sound/misc/am_pkup.wav` | 8 |
| Door start, `sound/doors/dr1_strt.wav` | 8 |
| Door end, `sound/doors/dr1_end.wav` | 8 |
| Separate incidental ammo pickup on the door route | 8 |

Door start/end voices and eight moving-door loop observations had positive
gain. The coordinator independently compared all 48 retained nonzero
buffers against the 8,478,720-byte monitor recording. They all match.
These buffers contain final mixed output. Individual cue contribution and
subjective audibility are not isolated by this check.

The raw aggregate result retains `qualified=false`. Its only failed
predicate is one client/server clock tuple among 1,241 sampler cuts: the
client clock is 17 ms ahead during the door route. The observer fences
other fields, but does not fence these two clock fields. The production
client clock advances before `client_pending` is set and clamps at Source
completion. This mixed sample cannot establish a returned-frame
interpolation defect. All sampled Source intervals are 25 ms and all 876
Source tick transitions match 40 Hz; the complete clock-bound predicate
remains unqualified. No performance claim uses this debugger-assisted
audio run.

Public `quit` returned 0. All 12 recorded owned processes were independently
absent afterward. No production or SDK code changed in this check.
Evidence is retained in the private `qa-private-av-il2rui1b` run directory:
`sound-readout.json`, `clock-bound-limit.json`, `result.json`, the accepted
raw output buffers, the monitor recording and `final-owned-cleanup.json`.
Original-module, classic Q2, GL and all-event sound coverage are outside
this check.
