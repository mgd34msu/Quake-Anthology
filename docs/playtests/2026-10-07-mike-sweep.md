# Installed-build MIKE verification sweep, 2026-10-07

The sweep is in progress. Its first checkpoint reproduces a native Q3 menu
startup failure and records bounded passing Original Q3 input and pickup
checks. It does not close all 35 issues.

Every session uses the installed `qa-c` from
`41861a12354c6caad56a347e32e170f887a81ab3`, built at
07:59:29.410595 CDT and installed at 08:12:00.553790 CDT. The executable
has device 57, inode 165642975, 141,676,896 bytes and mtime
1791377969410595032 ns. Runtime helpers are pinned too. Each launch receives
a fresh copy of the owner's 34 profile files. Display/authentication and
actual SDL audio output are private; PulseAudio goes to a null sink and
its captured monitor. The original profile and installed package are
unchanged.

The private evidence root is named `mike-sweep-20261007`. Each issue's
Linear sweep comment supplies its absolute bundle path. Bundle paths in
this document are relative to that root. PNGs preserve the symptom spot,
JSON/JSONL retains inputs and observed state, console logs retain failures,
and sound bundles include accepted PCM and monitor-delivery summaries.
Read-only functional observers are used where state is otherwise invisible;
their runs make no frame-time or FPS claim. Historical checks are identified
separately and are not counted as new installed-build evidence.

| Issue | Defect | Current sweep evidence |
|---|---|---|
| THE-139 / MIKE-01 | Q1 movement and jump | Four edition/renderer routes retain grounded input; RR GL observes impulse270 and actual steps; numerical qsrc comparison/cadence pending. |
| THE-140 / MIKE-02 | Q2 spawn facing | Classic/RR CPU source and completed world camera match authored yaw 135; GL review pending. |
| THE-146 / MIKE-03 | Q2 barrel crash | Classic/RR CPU damage/removal and continued play pass; GL and late cleared-world images pending. |
| THE-151 / MIKE-04 | Q2 rerelease video/crash | RR CPU plays/combat/quits 0; GL fire/effects quit 0, matched world/HUD review pending. |
| THE-153 / MIKE-05 | Q3 mouse snaps back | Original CPU: eight stationary/moving checks pass; other paths pending. |
| THE-159 / MIKE-06 | Q3 wrong sounds | Original CPU: authored weapon-pickup PCM and output delivery pass; other events pending. |
| THE-163 / MIKE-07 | Q3 weapon pickup/keys | Original CPU: natural shotgun grant/hide pass; keys/firing and other paths pending. |
| THE-167 / MIKE-08 | Shareware menu option | CPU/GL actual catalog-backed product menus contain no shareware option. |
| THE-171 / MIKE-09 | Custom monsters unavailable | CPU/GL list 396 available creatures in 16 rosters; actual Q2RR whole-roster mixed launch succeeds. Per-class/combat coverage excluded. |
| THE-172 / MIKE-10 | Custom Start crash | Three mixed world/arsenal combinations start/play/quit on CPU/GL; two additional arsenals fire and consume ammo. |
| THE-180 / MIKE-11 | Split-screen views/control | Pending. |
| THE-185 / MIKE-12 | Ported menus | Ten CPU/GL pages visually compared: design matches; Sound formatting differs and Custom references are not matched. |
| THE-190 / MIKE-13 | Q2 smoothness/aim/sounds | Classic/RR actual weapon/pickup/door voices and output retained; aim comparison and quiet cadence pending. |
| THE-201 / MIKE-15 | Q2 dark video | Pending. |
| THE-208 / MIKE-16 | Q3 menu startup | FAIL: native CPU and NVIDIA GL Start exit 1 before gameplay; reopened In Progress. |
| THE-213 / MIKE-17 | Transparency | Pending Q1/Q2 CPU/GL. |
| THE-217 / MIKE-18 | Visible Q1 trigger brushes | Classic/RR CPU/GL entry-corridor images clear; full end-slipgate/submission check pending. |
| THE-225 / MIKE-19 | Q3 black text/HUD | Pending. |
| THE-233 / MIKE-20 | Missing particles | Pending. |
| THE-251 / MIKE-21 | Q1 slipgate crash | Classic CPU/GL Normal slipgate, fog expiry and episode entry survive; game quit 0. |
| THE-261 / MIKE-22 | Vanilla Q1 autosave format | Classic CPU/GL autosave and public save/load/resave use v5; RR uses its native text v6. |
| THE-280 / MIKE-23 | Teleporter facing | Classic GL and exact mixed CPU/GL differing angles become authored90; RR differing-angle check pending. |
| THE-295 / MIKE-24 | Q1 jump sound | Actual dry-jump cue/positive retail voices/private output retained across cores; swim pending. |
| THE-305 / MIKE-25 | Q1 rocket explosion sound | Pending. |
| THE-316 / MIKE-26 | Q1 menu effects | Open/close menu2 and navigation menu1 reach private output; select/change menu3 pending. |
| THE-333 / MIKE-27 | Rerelease SP/layout/load filtering | RR GL all-four-skill physical gates, retail exclusions and native save/load pass; CPU fourth gate pending. |
| THE-345 / MIKE-28 | Difficulty gate persistence | Classic GL Easy/Normal/Hard and RR GL all four pass with exact retail counts/save-load; classic Nightmare pending. |
| THE-358 / MIKE-29 | Correct Q1 music | RR GL actual committed start4/e1m1=6/e1m2=8 retail streams/output; classic08 and EOF loop pending. |
| THE-380 / MIKE-30 | Q2 jump/crouch/floor/sound | Classic/RR CPU jump/repress, duck/stand and jump output pass; low ceiling, complete GL and cadence pending. |
| THE-395 / MIKE-31 | Item rotation and Q1 disappearance | Pending all four editions. |
| THE-410 / MIKE-32 | Console cvars/FOV | RR GL actual typed aliases and completed-world FOV agree; required other paths pending. |
| THE-416 / MIKE-33 | Broken Q3 sky | Pending. |
| THE-423 / MIKE-35 | Q3 death/respawn crash | Pending genuine bot deaths/respawns. |
| THE-431 / MIKE-36 | Exact mixed-game startup | CPU/GL exact configuration starts, traverses Normal gate and quits 0; actual common camera takes forced angle. |
| THE-438 / MIKE-37 | Q1 status bar | Four basic stock-bar/HUD captures retained; keys/powerups/armor/split and THE587 backtile remain open. |

THE-196 / MIKE-14 and THE-420 / MIKE-34 are already In Progress and are
outside the 35-issue In Review sweep. The supervisor, rather than this
agent, makes Done transitions. An incomplete criterion remains explicit;
remaining subjective movement feel is reserved for the owner after the
objective qsrc comparisons.

THE-208's `q3-native-cpu` bundle contains five menu PNGs, including
`THE208-start.png` with PLAY selected immediately before activation.
`raw-console-session.log` reports that campaign values require completed
Source Shutdown, then retained CLIENT/ENGINE settings-owner cleanup errors.
The actual game exits 1 before gameplay publication. This is a game failure,
distinct from earlier containment/helper setup failures. The coordinator
inspected the PLAY PNG and console log and independently confirmed every
recorded owned PID was absent after cleanup. Original CLI gameplay does not
qualify this menu path. Linear comment `19a60cc0-6f5e-4dc9-8ea1-c88bbaf88792`
records this failure.

The separate `q3-native-gl` bundle reproduces the same exit from PLAY with
NVIDIA GLX on an isolated GPU0 display, before gameplay publication. The
coordinator inspected its PNG/log and confirmed every recorded owned PID
absent. An earlier GPU-server readiness race is retained separately as a
helper setup failure. It is excluded from game-failure evidence.

The first classic CPU screenshots also expose two newly filed M0 defects:
THE-586 duplicates the Normal skill-hall message at the top-left instead of
centerprinting it once; THE-587 leaves black bands beside the status bar
instead of drawing `backtile`. Both are queued after the sweep alongside
THE-208. Their GL/rerelease captures are pending, and they do not count as
passes for the original status-bar issue.

THE-153's `q3-original-cpu` bundle preserves before/target/retained images
for left, right, up and down with both stationary and moving input. All
eight actual Source targets remain retained; the actor-matched world camera
follows them. Nominal mouse deltas become 4.751586914 degrees yaw and
2.373046875 degrees pitch after command quantization. Moving cuts report
320–320.451 units/s. A later fixed-wait helper calibration failed, so its
unreached stages are excluded. Public quit returns 0 and recorded owned
processes are absent.

The same session naturally collects the shotgun: Original player weapon
bits change 6 to 14, shells 0 to 10, event 19 has parameter 9, and the
authored item becomes EF_NODRAW. THE-163 retains that transition but does
not yet qualify its later key-selection/firing stages. THE-159's matching
sound bundle identifies `sound/misc/w_pkup.wav`, compares source/prepared
PCM against the retail asset and original resampling, and retains ten
accepted nonzero SDL buffers found in the private monitor. This proves the
captured pickup cue, not every Q3 sound or subjective audibility.

The first Q1 classic CPU core is retained once in
`_q1-common/classic-cpu-core`, with per-issue image/raw-evidence links.
Read-only console queries return gravity 800, friction 4, stopspeed 100,
acceleration 10 and maxspeed 320. Actual default Space leaves the ground
and returns; W reaches 320 units/s and release stops the player. Sparse
Source-time samples do not establish individual move timesteps or exact
qsrc friction/gravity equality. Normal-gate traversal changes to e1m1 with
skill 1, deathmatch/coop 0, 23 monsters and the exact 33 retail skill
exclusions. Level-entry autosave and public start/e1m1 save/load/resave
return original v5 text files; their sizes are retained in the bundle.
The coordinator independently compared all 403 accepted nonzero aligned
SDL buffers against the private monitor. They match byte for byte, but a
retained last-cue label does not establish isolated contribution to later
mixed buffers. Public quit is 0 and recorded processes are gone.

The first Q2 rerelease CPU base1 run is retained under
`common/q2-rerelease/cpu/qa-private-av-lxe2lean`; per-issue bundles refer to
that shared event/console/profile custody. Two actual jump events occur at
Source times 0.850 and 1.950 s after a landed-held/release/repress sequence.
CTRL changes maximum body height 32 to 4 to 32; standing feet remain at the
observed floor. A blocked low-ceiling stand is not captured. The sampled
Source interval is 25 ms with 369 matching transitions; attached-observer
FPS and frame cadence are excluded. Barrel actor 1:341:0 goes from live
Source/body/completed DRAW to absent in all four, followed by 554 frontend
frames and public quit 0. The before/after PNGs include the explosion; a
later fully cleared spot is not yet captured. Authored cells and door
checks use explicit public noclip-assisted positioning. Accepted sound
output and completed rotating item matrices are retained, with unproved
aim, weapon-effect, quad and other-edition/renderer criteria listed in the
readouts. Every recorded owned PID is independently absent afterward.

THE-185's `UI/review-cpu-cy19cvdt` bundle compares ten actual CPU 960x600
pages with retained TypeScript GL references. Artwork/crop, panel/title,
row/focus geometry and settings clipping match visibly; state/backend and
conditional-row differences are listed. The retained references lack a
commit stamp, and image differences do not establish pixel identity. Sound
has a concrete integer-choice formatting deviation (`22050.000000 Hz`);
the disabled-audio device row is a separate launch-state difference. The
enclosing run later stops on an optional per-class-selection helper
mismatch. Its prior menu captures are retained, without whole-run, enabled
audio, GL or broad Custom/in-game parity claims.

THE-431's `mixed/cpu` and `mixed/gl` bundles preserve real Custom menu
selection of Q1 classic `maps/start.bsp`, Q3 movement, Ranger, Q3 weapons,
Q2 rerelease monsters and single player. Gameplay follows startup debugger
detach. Keyboard/mouse traversal reaches the Normal gate with entry yaw
88.939819 degrees; forced-angle revision changes 2 to 4, body/retained
command/result yaw becomes 90, and the actual common emitted world camera
is 90.0000025 degrees after teleport and settling. CPU/GL retain 383/464
accepted cuts. Both public quits return 0. The coordinator inspected the
configuration and settled world PNGs and checked recorded client processes
absent. GL uses private Mesa llvmpipe; these audio-disabled functional
sessions make no sound or performance claim. The unrelated duplicate hall
message remains visible and is queued as THE-586.

The same batches' THE-167/THE-171 UI bundles retain the complete actual
catalog-backed product/creature tables and visible choices. There is no
shareware row; 396 available creatures across 16 supported rosters plus two
default rows make 398 choices. Actual Q2 rerelease whole-roster selection
is published in the mixed launch. Optional per-class Soldier navigation
was not completed; combat of every creature is not inferred from availability.
THE-172 preserves this exact launch with actual Q3 starting inventory,
while broader world/arsenal combinations remain separate criteria.

A Native Q3 direct-CLI CPU session reaches mouse/pickup/key/firing/FOV and
visual stages, then public `addbot sarge 5` after travel to q3dm1 exits 1.
The actual log records `BotAISetupClient: client 1 already setup` and a
lost physical source binding. This new M0 defect is THE-589, queued after
the sweep with THE-208/586/587. Prior passing stages are separate evidence;
no bot-death/respawn or normal-quit pass is claimed for this run. A prior
different-map PNG is labeled as context, not the closed failure window.

The later classic Q2 CPU packet (`qa-private-av-t5qjee21`) records
actual blaster damage, barrel death/removal, 1,461 continued frontend frames
and public quit 0. Its late cleared-barrel PNG was captured while cinematic
presentation owned the screen, so that visual claim is withdrawn. Numeric
removal and earlier world before/fire/after PNGs remain valid. Both Q2
editions start with body/source yaw 135 and a matching completed world
camera, as authored by the selected spawn. Classic C and rerelease
copied-profile CTRL produce duck/stand dimensions 32/4/32; two Space
jumps, positive jump voices and actual private output are retained. A
blocked low-ceiling stand and quiet frame/prediction cadence remain open.

The rerelease Q1 GL extended packet physically traverses Easy, Normal,
Hard and Nightmare. Its e1m1 skill exclusions are 46/33/18/18, with
10/23/42/42 monsters; save/load/resave preserves the native rules and
state. Rerelease text saves use v6, while classic remains v5. The
coordinator inspected the Nightmare gate, episode and restored PNGs.
Classic GL still lacks a successful physical Nightmare route; earlier
controller misses are preserved without being called game failures.

At 10:30 CDT the supervisor closed THE-251, THE-261, THE-167, THE-171
and THE-431 after reviewing their evidence; Linear now reports Done for
those five. This agent made no Done transitions. The other sweep entries
remain bounded or pending.

THE-588 is also queued after the sweep. The combined game's QASV autosave
uses the vanilla Q1 classic filename, `autosave-q1-classic-id1.sav`, so
the two session types overwrite the same slot. Its format is appropriate
for combined state; its slot must be distinct from vanilla product slots.
The supervisor's issue records the 236,264-byte shared save and matching
in-game checkpoint PNG. No hashing is needed to separate those names.

The final ten-page menu review now includes successful CPU and GL
receipts in `THE-185/UI/review-gl`. Complete side-by-side pairs preserve
layout/art/fonts with saved-state and conditional differences stated.
Custom's current summary is visibly denser than older references; their
version/state mismatch leaves exact Custom parity unqualified.

Additional Custom bundles under `THE-172/mixed` retain Q1 classic maps
with Q2 weapons/monsters and Q1 rerelease maps with Q1 classic weapons
and Q2 rerelease monsters, all retaining Q3 movement/Ranger. Both CPU/GL
pairs accept Mouse1, consume shells 25 to 24, move with W and quit 0.
Those results do not claim viewmodel rendering, campaigns or audio.
Earlier helper rejections of valid Q2 names/render cuts are preserved
separately with their observer corrections.

The RR GL movement packet directly observes the 270-unit jump impulse
before gravity and retains 90 entry/return pairs plus 14 friction-edge
traces. Recorded dt is 0.10000000149 under a functional debugger; it is
not normal-play cadence. Numerical comparison to qsrc is separate. Its
74 accepted aligned nonzero audio segments were independently compared
byte for byte with private output, with zero failures. These mixed
segments do not isolate a last-cue label's later contribution.
