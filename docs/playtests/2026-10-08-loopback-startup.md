# Loopback startup and control checks

THE-913 is in review; THE-907 and THE-868 remain in progress. The first
qualified build recorded here is
`dee3546e270afe4d04236027ef61f76281b791c1`, built October 8 at
10:58:39 CDT. These checks used private displays and copied owner settings.
They do not establish release readiness or frame-time performance.

## Delayed connection

The shared server discarded pending challenges after ten seconds. The client
retained its token and retried CONNECT, so a slow client startup could leave it
waiting indefinitely. The fix removes age expiration and replaces the oldest
entry only when the existing 256-entry table is full. Original Q3 uses this
bounded replacement in `code/server/sv_client.c`, `SV_GetChallenge`.

A focused check through the actual loopback transport separately delayed
CONNECT delivery and pending attachment by eleven seconds. Before the fix,
neither reached the attachment callback; both do afterward, including against
the rebuilt SDK. A 257th distinct HELLO also receives a challenge afterward.
The fixture stops at attachment and does not pretend to prove gameplay.

Native Q3 `q3dm0` GL then reached the world and original HUD. The late diagnostic
read epoch 1, admitted and received state, live frame/media, and one peer at
both bootstraps. The last handshake was 12.94 seconds after launch. A private
window-close request exited normally with code zero. This diagnostic attached
a debugger briefly and supplies no timing or control proof.

## Actual controls

| Scope | Observed result | Remaining gap |
| --- | --- | --- |
| Original Q1 classic `start`, CPU and GL | Retail world/status bar, forward movement, jump and landing, shotgun flash and shells 25 to 24, released idle weapon, public quit 0 | Mouse turning was not established by the single small motion event |
| Native Q3 `q3dm0`, GL | Real mouse turn, later forward displacement, ammo 100 to 96, original HUD, public quit 0 | Jump/crouch phase captures do not establish each release; the final status query was queued |
| Mixed Q1 classic `start`, Q3 movement/ranger/weapons, Q2 rerelease monsters, CPU and GL | Real world, admitted/received seat, normal private window close 0 | Selected Q3 weapon is not visible; ammo stays 25; individual control responses remain unqualified |

Original Q1's public frame information advanced on both renderers:

| Renderer | Frame | Received source milliseconds | Input acknowledgment |
| --- | --- | --- | --- |
| CPU | 774 to 1170 | 10362 to 15130 | 762 to 1156 |
| GL | 373 to 838 | 5454 to 11325 | 340 to 825 |

Each row reports seat 0, epoch 1, admitted 1 and received 1. The Q3 GL console
response reports input acknowledgment 94; mixed CPU and GL report 20 and 16.
Those single endpoints do not prove acknowledgment advancement or associate
an acknowledgment with a particular key.

## Evidence bounds

The coordinator inspected the Q3 world, mouse, forward and console images,
the mixed CPU console, and original Q1 world, firing and GL console images.
Workers inspected every phase image in their own packets. Q1 and mixed
checks captured actual private audio output, but these records do not identify
individual sound cues. GL used software rendering where reported, so no
hardware GPU speed claim follows.

Local evidence indexes are `qa-the913-q3dm0-challenge-state-diagnostic.json`,
`qa-the913-q3dm0-dee3546e-control-observation.json`,
`qa-the913-challenge-combined-proof-20261008/result-index.json`, and original
Q1 packet reviews `tzsw5r59/worker-original-review.json` and
`mhg6ldo3/worker-original-gl-review.json`. The delayed-handshake fixture retains
`before.json` and `built.json` in `qa-the913-delayed-challenge-20261008`.

The original 34 profile files and five candidate application files stayed
unchanged in the completed runs. Recorded owned processes were cleaned up.
All sixteen private Wayland startup scopes reached the intended menu or
world and exited normally. The coordinator inspected every final capture.
The original 34 settings files are the complete current set of saved CFG/JSON
settings, and none was omitted from the copies. Both Q3 renderers show the
original HUD without generic tiles. The separate `q3dm1` CPU/GL regression
pair also reached gameplay and quit normally.

Installation through `tools/install_qualified_build.py` completed at
11:25:29 CDT using the exact copied-profile gameplay and Wayland receipts.
All five installed application files directly byte-match the candidate.
A Slack retest note was posted after installation. The receipt is
`installed-m0-loopback-startup-20261008.json`; the Wayland evidence is indexed
by `the913-dee3546e-wayland-20261008/qualification-reviewed.json`.
The mixed view-weapon fix is a subsequent change and is not included in this
installation.

The subsequent mixed diagnostic found native character weapon 2 and selected
arsenal weapon 3 in the same received frame. The selected shotgun row had
visibility enabled, with 25 shells in inventory; draw-gun was enabled. The receiving model renderer had
no consumer for its serialized Q3 animation, hands and attachment continuation.
It submitted the bare gun at the camera instead. The existing selected-weapon
renderer now consumes that continuation with the common camera, keeping its
animation and asset cache in the persistent received owner. Character state is
left intact. Both strict compilers, the full build and core checks pass. The
first live pair still showed no gun: an earlier compiled ownership marker can
skip the received continuation without observing a draw. Received Q3 weapons
now use the shared selected-weapon renderer before that ownership check; the
compiled default view is consumed. Live qualification of that change is
pending at that source step. The diagnostic index is
`qa-the907-mixed-1a6291ca-q3-view-state.json`.

The `b5d8813b` candidate, built at 12:04:30 CDT, then showed one recognizable
double-barrel view weapon in both mixed CPU and GL runs. The coordinator
inspected the CPU firing-phase and GL initial images; world and Q1 HUD remained
visible. Both runs quit normally with clean logs, unchanged owner settings and
candidate files, and all recorded owned processes stopped. The evidence index
is `qa-the907-common-view-weapon-proof-20261008/result-index.json`.

THE-907 remains open: Mouse1 was held about 3.2 seconds, but no distinct flash
was captured and visible ammo stayed at 25. Individual jump/mouse responses
and per-key acknowledgments are not established. Final source/ack endpoints
were CPU 4209/29 and GL 3801/25. These are single observations, not advancement
proof.

The same candidate was installed at 12:35:34 CDT through the qualified
installer. Its copied-profile CPU/GL gameplay receipt and sixteen private
Wayland menu/gameplay scopes passed; the coordinator viewed every final
Wayland image. All five installed files directly byte-match the candidate.
The install receipt is `installed-m0-selected-view-weapon-20261008.json`, and
a Slack retest note was posted. Standalone `q3dm0` GL retained one stock gun,
world and original HUD; actual mouse turning and ammo 100 to 94 were observed,
with a normal quit and no debugger. Mixed firing remains under investigation.

A subsequent bounded selected-arsenal read on that same build established a
shot. Actual held Mouse1 supplied attack 1 and selected shotgun 3; the natural
step returned successfully, emitted fire event 23 and consumed ammo 25 to 24.
The coordinator opened the later held-fire and released-fire images, which
both show 24 on the received Q1 HUD. The earlier short captures showing 25
therefore do not establish a firing or HUD publication defect. This diagnostic
used one debugger entry and natural return, with no inferior calls or writes;
it makes no timing claim. The run quit normally with unchanged owner settings
and candidate files and all ten recorded owned processes stopped. Its index
is `qa-the907-mixed-fire-state-20261008/firing-entry-return-evidence.json`.
The subsequent plain installed CPU/GL pair used actual `qfiles/qa-c`, with no
SDK overlay, preload or debugger. Both show forward displacement toward the
start-map doorway, a distinct rightward mouse turn, one selected shotgun and
ammo 25 to 24 with ejected shells and wall impact smoke. Actual copied bindings
remain `+forward`, `+jump` and `+attack`. Space press/release captures show a
later raised viewpoint, but do not establish a grounded jump transition or
velocity; that scope remains open. Each game quit normally with clean flushed
logs, unchanged owner settings and installed files. All 50 recorded owned
process tokens were absent after cleanup. The index is
`qa-the907-installed-b5-input-proof-20261008/result-index.json`.

Final received source/ack endpoints were CPU 5409/41 and GL 5004/37, with
epoch 1, admitted 1 and received 1. They are single endpoints and do not
establish per-key acknowledgment timing. Private audio captures contain
nonzero output; individual cue identity was not checked. GL is software
rendering, and neither run is a performance measurement. THE-907 remains
in progress for the precise jump response.
