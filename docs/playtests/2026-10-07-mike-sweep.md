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
| THE-139 / MIKE-01 | Q1 movement and jump | Classic CPU grounded Space/W/release checks pass; exact qsrc dynamics, cadence and other paths pending. |
| THE-140 / MIKE-02 | Q2 spawn facing | Pending. |
| THE-146 / MIKE-03 | Q2 barrel crash | Rerelease CPU barrel removal plus 554 continued frames pass; classic/GL pending. |
| THE-151 / MIKE-04 | Q2 rerelease video/crash | Rerelease CPU plays and quits 0; GL pending. |
| THE-153 / MIKE-05 | Q3 mouse snaps back | Original CPU: eight stationary/moving checks pass; other paths pending. |
| THE-159 / MIKE-06 | Q3 wrong sounds | Original CPU: authored weapon-pickup PCM and output delivery pass; other events pending. |
| THE-163 / MIKE-07 | Q3 weapon pickup/keys | Original CPU: natural shotgun grant/hide pass; keys/firing and other paths pending. |
| THE-167 / MIKE-08 | Shareware menu option | Pending. |
| THE-171 / MIKE-09 | Custom monsters unavailable | Pending. |
| THE-172 / MIKE-10 | Custom Start crash | Pending. |
| THE-180 / MIKE-11 | Split-screen views/control | Pending. |
| THE-185 / MIKE-12 | Ported menus | Historical TypeScript reference PNGs retained; current comparison pending. |
| THE-190 / MIKE-13 | Q2 smoothness/aim/sounds | Pending current qsrc comparison and captured output. |
| THE-201 / MIKE-15 | Q2 dark video | Pending. |
| THE-208 / MIKE-16 | Q3 menu startup | FAIL: native CPU and NVIDIA GL Start exit 1 before gameplay; reopened In Progress. |
| THE-213 / MIKE-17 | Transparency | Pending Q1/Q2 CPU/GL. |
| THE-217 / MIKE-18 | Visible Q1 trigger brushes | Pending. |
| THE-225 / MIKE-19 | Q3 black text/HUD | Pending. |
| THE-233 / MIKE-20 | Missing particles | Pending. |
| THE-251 / MIKE-21 | Q1 slipgate crash | Pending. |
| THE-261 / MIKE-22 | Vanilla Q1 autosave format | Classic CPU level-entry autosave and public save/load/resave use original v5; other paths pending. |
| THE-280 / MIKE-23 | Teleporter facing | Pending. |
| THE-295 / MIKE-24 | Q1 jump sound | Pending. |
| THE-305 / MIKE-25 | Q1 rocket explosion sound | Pending. |
| THE-316 / MIKE-26 | Q1 menu effects | Pending. |
| THE-333 / MIKE-27 | Rerelease SP/layout/load filtering | Pending. |
| THE-345 / MIKE-28 | Difficulty gate persistence | Pending. |
| THE-358 / MIKE-29 | Correct Q1 music | Pending exact opened tracks, transitions and looping. |
| THE-380 / MIKE-30 | Q2 jump/crouch/floor/sound | Rerelease CPU jump/repress and duck/stand checks pass; blocked low-ceiling stand, classic/GL and cadence pending. |
| THE-395 / MIKE-31 | Item rotation and Q1 disappearance | Pending all four editions. |
| THE-410 / MIKE-32 | Console cvars/FOV | Pending. |
| THE-416 / MIKE-33 | Broken Q3 sky | Pending. |
| THE-423 / MIKE-35 | Q3 death/respawn crash | Pending genuine bot deaths/respawns. |
| THE-431 / MIKE-36 | Exact mixed-game startup | Pending. |
| THE-438 / MIKE-37 | Q1 status bar | Pending. |

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
