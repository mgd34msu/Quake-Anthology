# Installed-build MIKE verification sweep, 2026-10-07

The sweep is in progress. All 35 issues have one evidence comment in Linear.
The comments distinguish passing checks, incomplete criteria and reproduced
failures. A comment does not mean the issue passes.

Every session uses qa-c from
41861a12354c6caad56a347e32e170f887a81ab3, built at 07:59:29.410595 CDT and
installed at 08:12:00.553790 CDT. Its device 57, inode 165642975, size 141,676,896
and mtime 1791377969410595032 ns remain unchanged. Runtime helpers are pinned
by path/stat too. Each launch receives a fresh copy of the owner's 34 profile
files. Private displays and actual SDL output keep video/audio off the
owner's desktop; real PulseAudio output goes to a private null sink and
captured monitor.

The evidence root is named mike-sweep-20261007. Each issue's Linear comment
gives its absolute bundle path; paths below are relative to that root.
PNGs show the symptom spot, JSON/JSONL retains inputs and observed state,
console logs preserve failures, and sound bundles retain PCM and delivery
comparisons. Read-only functional observers make no frame-time or FPS claim.
Historical checks and failed helpers are labeled separately.

| Issue | Defect | Current installed-build evidence |
|---|---|---|
| THE-139 / MIKE-01 | Q1 movement/jump | Classic/RR CPU/GL real Space/W/release; RR GL impulse 270, friction/gravity match qsrc at float rounding scale. Quiet cadence remains open. |
| THE-140 / MIKE-02 | Q2 spawn facing | All four edition/renderer cases match selected retail base1 spawn yaw 135 in Source/control/completed world. |
| THE-146 / MIKE-03 | Q2 barrel crash | Actual damage/death/removal and continued frames in both editions/backends; GL late cleared-world PNGs pass. Late CPU images have explicit exclusions. |
| THE-151 / MIKE-04 | Q2RR video/crash | CPU/GL world/weapon/HUD and combat continue; short completion routes quit0. No campaign-wide claim. |
| THE-153 / MIKE-05 | Q3 mouse snaps back | Native/Original CPU/GL each pass eight real stationary/moving direction-retention checks. |
| THE-159 / MIKE-06 | Q3 wrong sounds | All four scopes play correct authored pickup PCM into private output; other events remain partial. Original CPU fire exposes THE-592. |
| THE-163 / MIKE-07 | Q3 pickup/keys | Native CPU/GL natural grant/hide, keys 1/3 and real shot pass. Original CPU shot state passes but world becomes stale; Original GL shot unqualified. |
| THE-167 / MIKE-08 | Shareware option | CPU/GL catalog-backed menus contain no shareware row. |
| THE-171 / MIKE-09 | Custom monsters | 396 available creatures in 16 rosters plus2 defaults; exact Q2RR whole-roster mixed launch passes CPU/GL. Per-class combat excluded. |
| THE-172 / MIKE-10 | Custom Start | Three mixed world/arsenal combinations start/play/quit0 on CPU/GL; two additional arsenals accept fire and consume ammo. |
| THE-180 / MIKE-11 | Split-screen | Four Q1 classic/RR CPU/GL virtual-controller and join/remove/disconnect passes; Q2 classic CPU/GL startup failures; four Q2RR/Q3 cases observer-unqualified. |
| THE-185 / MIKE-12 | Ported menus | Ten CPU/GL pages visually compared; artwork/layout match. Sound choice formatting differs; older Custom references do not qualify parity. |
| THE-190 / MIKE-13 | Q2 smoothness/aim/sounds | All-four event/output checks; projectile muzzle equations match qsrc. RR actual eye trace and quiet cadence remain open. |
| THE-201 / MIKE-15 | Q2 dark video | Retail intro frames 30/55/80 exact source RGBA; actual GL scale within 0.500001 RGB byte, CPU two-stage scale within 1 byte at gamma 1. Owner/default gamma remains open. |
| THE-208 / MIKE-16 | Q3 menu Start | FAIL: actual Native CPU/NVIDIA GL Play exits1 before gameplay. |
| THE-213 / MIKE-17 | Transparency | Q2RR CPU/GL TRANS33 windows pass. MG3 fence becomes opaque at distance on CPU and GL; embedded lower mips lose palette255. GL public screenshot readback separately returns black. |
| THE-217 / MIKE-18 | Visible Q1 triggers | Four entry corridors clear; complete end-slipgate/submission criterion remains bounded. |
| THE-225 / MIKE-19 | Q3 black text/HUD | Native CPU/GL chat, FIGHT! centerprint and score boxes pass; Original chat/HUD pass, centerprint unqualified. |
| THE-233 / MIKE-20 | Missing particles | Q1 impact/explosion world images and CPU/GL batches; Q2RR genuine Blaster/Rocket/Rail/BFG effects. Additional trails/events remain partial. |
| THE-251 / MIKE-21 | Q1 slipgate crash | Physical skill/episode gates and fog expiry survive in classic CPU/GL, with public quit0. |
| THE-261 / MIKE-22 | Vanilla autosave | Classic v5 and RR v6 text save/load/resave; no shared fallback in these vanilla sessions. |
| THE-280 / MIKE-23 | Teleporter facing | Classic and RR differing preteleport angles become authored90; exact mixed CPU/GL common camera also follows forced angle. |
| THE-295 / MIKE-24 | Q1 jump sound | Named dry-jump event/voice across editions/backends; classic accepted PCM delivery. RR sustained output and swim unqualified after rejecting four-byte matches. |
| THE-305 / MIKE-25 | Explosion sound | Named rocket event/voice in both editions/backends; classic accepted PCM delivery. Recovered longer RR mixed-output correlations need numerical replay; grenade remains partial. |
| THE-316 / MIKE-26 | Menu sounds | Captured menu event/voice stages; classic long PCM delivery. Recovered RR menu3 correlations need replay; isolated-cue output and classic CPU select/change remain partial. |
| THE-333 / MIKE-27 | RR SP/layout/load filter | CPU/GL physical all-four skills, retail filter counts and original text save/load pass. |
| THE-345 / MIKE-28 | Difficulty persistence | All-four physical gates now reach ordinary e1m1 in classic/RR CPU/GL; exact retail exclusions/monsters and text reload retained. |
| THE-358 / MIKE-29 | Q1 music | Committed tracks4/6/8, retail stream paths and active players retained; sustained classic output. RR music output and EOF rollover remain unqualified. |
| THE-380 / MIKE-30 | Q2 jump/duck/floor/SFX | All four cases: two Space jumps, duck box32→4→32, stable feet and positive jump output. Blocked low-ceiling stand and quiet cadence open. |
| THE-395 / MIKE-31 | Spin/Q1 pickup hide | Q1 natural accepted weapon/ammo touches show same-frame model 0/hidden/no DRAW; Q2 completed spin matrices match clocks. Armor/quad and full visual matrix remain partial. |
| THE-410 / MIKE-32 | Console/FOV | Q1 and all-four Q2 routes; Native CPU/GL and Original Q3 GL alias/slash/plain world-FOV checks. Original Q3 CPU remains blocked before that stage. |
| THE-416 / MIKE-33 | Q3 sky | Native CPU/GL three positions each on red q3dm1 and q3dm7; Original CPU/GL three red-sky positions. Other Original map unqualified. |
| THE-423 / MIKE-35 | Death/respawn crash | Recovery: native CPU and Original CPU pass genuine repeated rocket/plasma deaths and Mouse1 respawns; native GL meets combat checks but exits1 on public quit. Current installed GL rechecks remain open. |
| THE-431 / MIKE-36 | Exact combined startup | Required Q1 map/Q3 move+Ranger+weapons/Q2RR monsters launches, traverses Normal gate, applies forced camera and quits0 on CPU/GL. |
| THE-438 / MIKE-37 | Q1 status bar | Four basic stock HUD captures; keys/powerups/armor/split remain partial and THE-587 backtile fails. |

THE-196 / MIKE-14 and THE-420 / MIKE-34 are In Progress outside the 35-issue
In Review sweep. Stock Q3 landing rebound is unchanged pending the owner's
decision. The supervisor makes Done transitions. At11:10 the supervisor
reported MIKE05/08/09/21/22/24/33/36 closed after evidence review; this
agent made no Done transitions.

## Q1 gameplay and output

Shared raw packets live under _q1-common; per-issue bundles link to them.
The classic CPU core records gravity 800, friction 4, stopspeed 100,
acceleration 10 and maxspeed 320. Actual default Space leaves ground and
returns; W reaches 320 and release stops. Sparse first-core samples alone
do not prove individual dt or exact mover equality.

The RR GL movement comparison records90 actual entry/return pairs.
Directly observed jump impulse is270 before gravity, and actual observed
dt is0.10000000149 under the functional debugger. Six ordinary grounded
friction steps differ from qsrc by at most7.528051e-6 units/s; thirteen
free-air gravity steps by at most8.583e-6. Grounded release travels43.276001
units before stopping. Doubled edge friction and unsaturated acceleration
are not isolated, and debugger timing is not normal-play cadence.
THE-139/q1-rerelease-id1/gl/movement-qsrc-comparison records equations,
matched phases, traces and limits.

Together, core/remainder/final routes now physically traverse all four
skills on both editions/backends, then enter ordinary e1m1. Retail skill
exclusions are46/33/18/18 and monster counts 10/23/42/42. Classic writes v5;
RR writes its original v6 text format. Public save/load/resave retains
skill/native state. Prior missed-ledge helpers are preserved, not classified
as game failures. Final classic GL and RR CPU Nightmare sessions quit0.
Their preteleport yaw differs from authored90, which becomes the actual
settled body/view yaw afterward.

Classic CPU naturally accepts the SSG and shell ammo: inventory grants,
model 0, nonsolid/hidden state and absent actor-matched DRAW occur in the
same frame. RR GL final naturally accepts its SSG too. These are gameplay
touches, not console-granted pickup evidence. Q1 particle sessions use
disclosed public equipment setup and actual Mouse1; shotgun impact and
rocket explosion images accompany Source/scene/backend particle records.
Other trail, swim, grenade and lightning subcases remain unqualified.

Saved classic PCM was compared to the recorded private monitor offsets.
All403 core,1054 CPU remainder and702 GL final accepted nonzero buffers
matched. RR CPU core35, GL extended74, CPU remainder29, CPU final21 and
GL final7 claimed exact matches are four-byte stereo-frame coincidences;
they do not qualify sustained output. The original summaries remain with
this correction. RR device output is22050Hz signed16 stereo and its monitor
is48000Hz, so raw byte identity is the wrong comparison. A recovered report
compares four longer CPU/GL menu3 and rocket mixed buffers after conversion
and64-frame edge trimming: left-channel correlations0.998980–0.999996,
normalized RMS errors0.30–4.52%. Those numerical metrics were recovered,
not rerun during recovery. They do not prove isolated cues, RR jump/music
output, latency or EOF rollover. Committed music targets identify map
sounds4/6/8 and the actual installed streams.

THE-213/q1-rerelease-mg3/gl/recovery-20261007 contains a new bounded GL
diagnostic with copied owner34 settings. Same-frame X window captures show
holes/slime near the grate and an opaque tan strip far away. Returned
presented RGBA buffers and public screenshot PNGs are black while those
window captures show the scene. This proves a separate readback failure on
the private Xvfb/llvmpipe session; the older dark route captures remain
unqualified. Native front-buffer selection is recorded, with no claim
about a driver-level cause. The new session quit normally; all11 owned
PID/start tokens are absent and original settings/artifact stayed unchanged.

The actual grate draw uses GT666 alpha testing. Its64x64 base mip has1288
transparent texels; all lower authored mips are opaque. Independently read
retail hub.bsp data agrees: no index255 in lower mips, with mip2/3 wholly
index240 (tan fullbright). The common loader imports all four levels without
regenerating them. CPU and GL therefore share the defective mip data.
The selected GPU fragment mip was not observed.

THE-180/split/final-result-matrix.json records ten cases: four Q1 classic/RR
CPU/GL passes for independent virtual controllers, public fourth-player
join/removal, held-input disconnect and surviving route reassignment, then
normal quit0. These do not qualify physical controllers. Q2 classic CPU/GL
exit1 before world publication because their source edict capacity is
rejected; the rejected numeric values were not captured. Q2RR/Q3 reached
world/control/player state, but the exactly-one perspective VIEW observer
also counted weapon/HUD views and blocked the input/lifecycle checks.
Those four are observer misses. Original failures, censuses and final GL
images are retained; THE-180 remains In Progress.

## Q2 gameplay, effects and cinematic

q2-batch-handoff.json and q2-comment-drafts.json index the completed cases.
q2-cleanup-review.json records144 owned PID/start tokens absent and 31
unchanged current stat pins. Longer partial helper routes retain explicit
errors; final short completion runs in all four scopes quit normally.

Untouched spawn yaw 135 agrees between retail entity, Source/control and
completed world. Real shots cause barrel damage/death and retirement.
Classic/RR GL have late cleared-world PNGs; classic CPU's late cinematic
PNG is withdrawn as barrel evidence. A recycled RR CPU DRAW slot is not
a surviving barrel.

Standing/duck dimensions are32/4/32, with stable classic 0.125 and RR0.03125
feet height. Viewheight22→-2 produces a24-unit world-eye drop. Two real
Space jumps include a held-land/release/repress cycle and named jump output.
Classic uses stock C, RR copied-profile CTRL. Actual low-ceiling blocked
stand is not captured.

THE-190/aim-comparison pairs4 classic and 14 RR actual projectile launches
with source-phase muzzle equations. Origin residual is0; direction errors
are at rounding scale. Classic completed shot cameras follow recoil;
RR actual eye-trace endpoint is missing. Actual Source steps are100 ms
classic and 25 ms RR with intermediate presentation frames; quiet cadence
still needs separate measurement. Pickup/door transit assistance and the
private classic windowed-mouse setting are disclosed.

THE-395/q2-completed-spin-comparison records two actual completed transforms
per EF_ROTATE item in each Q2 scope. Maximum wrapped yaw residual from
edition-specific client-clock equations is5.565005e-6 degrees; off-axis
pitch/roll components are0. PNG timing is retained separately rather than
assigned to an unmatched transform census. Quad is not qualified.

RR glass uses textures/e1u1/wndow0_3, alpha 0.3300000131,
SRC_ALPHA/ONE_MINUS_SRC_ALPHA and no depth write. CPU/GL images show
machinery through it. Actual Blaster/Rocket/Rail/BFG firings, advancing
Source clocks/ammo and completed effect images accompany particle batches.
Public give-all setup is explicit.

THE-201/offline-oracle independently decodes ntro.cin frames 30/55/80 and
matches all retail source RGBA bytes. At gamma 1, GL direct 1920x1080
bilinear presentation differs by at most0.500001 RGB byte. CPU first
renders 640x400, then uses the installed SDL_UpperBlitScaled surface path
into 1920x1080. That actual chain matches within 1 byte everywhere, mean
errors 0.001258/0.000796/0.000617, with0 channels over 1 among 6,220,800
per frame. The original48–50 maxima modeled the wrong single-pass
scaling chain and are withdrawn as fidelity conclusions. No uniform
attenuation appears in these frames; owner/default brightness remains open.

## Q3 input, presentation and failures

Q3-SWEEP-READOUT.md and q3-issue-index.json retain every scope and cleanup.
Native and Original CPU/GL each pass eight real stationary/moving mouse
retention checks. Actor-matched successful world cameras follow actual
Source angles; a later HUD material view is not used as a camera proxy.

All four scopes naturally collect the shotgun and play the authored
sound/misc/w_pkup.wav into private output. The independently checked
Original pickup PCM matches retail 22050 Hz mono signed 16,12144 samples;
prepared 26435-sample PCM matches the original117/256 resampler, and
all 10 retained accepted output buffers match the monitor.

Native CPU/GL also pass keys 1/3, actual firing, completed after-shot world
images, ten public plain/slash FOV alias checks and requested chat/centerprint/
HUD alpha. Original CPU actual shot consumes a shell, then audio rejection
leaves world presentation stale. Original GL pickup/keys/FOV pass, but its
shot is unqualified. Original direct chat/HUD and red-sky images pass;
no actual centerprint occurred, and its other-map sky is unqualified.

Three failures retain distinct custody:

- THE-208/q3-native-cpu and q3-native-gl preserve actual Home→Play→Q3→
  Intro→Difficulty PLAY activation. Both exit1 before gameplay. The log
  reports campaign/Source Shutdown and CLIENT/ENGINE settings cleanup;
  Original CLI launches do not prove this menu path.
- THE-589 preserves actual Native addbot after map travel, exit1 with
  BotAISetupClient client1 already setup. Repeated bot-death/respawn proof
  for THE-423 is blocked; no kill-command proxy is substituted.
- original-cpu-shot-presentation-skip preserves THE-592. After shotgun fire,
  shells 10→9 and event23 occur at Source 5750 ms. Later GAME/command reaches
  11300/11340 ms while successful world remains5550 ms and the log rejects a
  retained audio operation. Recovered actual cuts supersede the labeled
  initial empty copy. Original GL map travel also reproduces existingTHE-585
  console/ownership failure; it is separate from this audio symptom.

The coordinator inspected Native menu failure PNGs/logs and representative
Original chat/sky images. All owned Q3 processes are absent, GPU0 X77 is
released and all three installed package files still match their stat pins.
No foreign socket or process was removed.

## Custom menus and remaining queue

THE-185/UI retains ten full CPU/GL comparison pairs against historical
TypeScript references. Layout/art/fonts match visibly with conditional and
saved-state differences listed. References lack a commit stamp. Sound has
an integer-formatting defect,22050.000000 Hz; older Custom references have
different states/versions and do not establish exact parity.

THE-431 and THE-172 retain three actual Custom configurations on CPU/GL.
The required Q1classic start/Q3movement/Ranger/Q3weapons/Q2RRmonsters case
traverses Normal gate, takes authored camera yaw 90 and quits0. Two additional
world/arsenal combinations fire, consume shells 25→24, move and quit0.
They do not qualify campaigns, viewmodels, every creature or audio.

The supervisor's11:49 fix order is THE-208, THE-423/THE-589, THE-592,
THE-213, THE-588, THE-586, THE-587, then the ten
In Progress M1 items with their exact listed gaps. THE-588 separates custom
QASV autosave naming from vanilla Q1 autosave slots. THE-586 corrects the
duplicated top-left skill centerprint. THE-587 restores status-bar backtile.
No source/install change is part of this evidence checkpoint.

## Qualified menu-start fix after the frozen sweep

THE-208/MIKE-16 is now In Review. Commits6ba913e0,45c22e6d and3d4e1efd
remove the premature campaign-constructor requirement, the two exact
paused-queue byte checks, and the common engine-view owner mismatch.
The captured propagated console rejection was the normal arena-start
`addbot crash` command: source owner1, common engine view1 owned by0.
The earlier unbound-view predicate capture was expected false before
registration and was excluded from the root-cause finding.

The candidate was built2026-10-07 12:46:46CDT. Six rebuilt existing tests
pass. Fresh copied-owner CPU and private NVIDIA GL actual menu routes
reach completed q3dm0 gameplay and normal public quit. Installation at
12:50CDT used tools/install_qualified_build.py with the GL copied-profile
receipt; executable and native companions are byte-equal to the candidate.
Installed receipt: installed-m0-menu-20261007.json in the recovery cache.

The installed qfiles/qa-c was then checked through the same real keyboard
Home/Play/Q3/campaign/intro arena/Start route on CPU and NVIDIA GL:
/tmp/qa-private-av-wbmzefw2 and /tmp/qa-private-av-nhr407qu. Each retains
seven route/live PNGs, input and console logs, completed world state,
private actual SDL output and normal quit0. All owned tokens are absent,
owner34 originals unchanged. These debugger observations prove startup,
not performance or audio fidelity. A Slack install notice requests that
menu retest. The frozen418 sweep above remains historical evidence.

A first Original Q3 shotgun recheck on this build naturally obtains the
weapon, selects key3, consumes shells10→9 and emits event23. World frames
continue past the event and public quit0. It does not reproduce THE-592,
so no audio fix or closure is claimed; the previous failing packet is
retained. Bot travel/death, THE-213 and the later queued fixes remain open.

## Qualified native Q3 bot travel after the sweep

THE-589 is In Review after f8308097,6a2e6c90 and daaa4fa6. Map travel
now rebuilds the native allocation pool, carries dynamic local bots through
the shared roster and begins retained clients once. The splash-aim query
uses map navigation, as qsrc BotPredictVisiblePosition does; it no longer
mistakes a bot-library client number for a physical client slot.

The daaa4fa6 candidate was built2026-10-07 13:24:23CDT and installed at
13:29:43CDT with tools/install_qualified_build.py and the copied-owner GL
qualification. Installed receipt: installed-m0-bot-travel-20261007.json.
Shipped CPU /tmp/qa-private-av-udpsunn2 and NVIDIA GL
/tmp/qa-private-av-0pvddgrt both pass q3dm1→q3dm7→q3dm1 travel, retain Crash,
join Sarge/Visor/Bitterman and complete later frames before public quit0.
Owner34 originals and package pins are unchanged; owned tokens are absent.

THE-423 remains partial. GL records three genuine bot deaths followed by
physical Mouse1 respawns, with rocket, shotgun and rocket-splash causes.
CPU records one such cycle; the second death had already respawned before
the requested press. Neither proves a long Original-module match. These
debugger observations make no performance claim.

## Installed transient audio and fence-mip fixes

THE-592 is In Review after 724b9779. The shared mixer accepts a finite
position for a transient Q3 event that has no persistent audio actor; it
does not allocate a position queue entry. Explicit-origin playback remains
unchanged. The retained failing packet in `/tmp/qa-private-av-6_goape5`
records this operation returning false on the earlier build. The shared
audio probe also confirms nonfinite positions remain rejected and positions
for persistent actors still queue normally.

The 756c2ce7 candidate was built at 13:42:38 CDT and installed at 13:54:16
with `tools/install_qualified_build.py`. Receipt:
`installed-m0-audio-fence-20261007.json`. Shipped Original Q3 CPU
`/tmp/qa-private-av-3qgsozav` and NVIDIA GL `/tmp/qa-private-av-8gneq9nc`
both record genuine shotgun pickup, weapon keys, three one-shell Mouse1
shots, later current-world frames, physical movement and public quit0.
CPU observes three formerly rejected actorless position operations, all
accepted without queue allocation. GL has no such rare operation in this
run, but records 4,671 audio submissions without rejection or observer error.
The initial GL forward walk was blocked by the retail wall; the retained
clip trace confirms that result, and the final route proves a 141.822-unit
backward walk. No movement code was changed to satisfy the route.

THE-213 / MIKE-17 is In Review after 756c2ce7. The shared Q1 texture
preparation regenerates fence mips from the base image and stops before a
mip loses all cutout or visible texels. Retail `{mgrate3` lower authored
mips had no palette-255 transparency; its last two were solid tan. Shipped
MG3 CPU `/tmp/qa-private-av-h_9lu5nj` and NVIDIA GL
`/tmp/qa-private-av-dvaszpxx` now show the grate lattice and slime through
the holes at near and distant viewpoints. The prepared four-mip image
retains alpha on both sides of the common alpha-test threshold.

Both issues used fresh copies of the 34 owner settings files, private
displays and captured private SDL output, unchanged package pins and owner
originals, and recorded owned-process cleanup. Root reviewed the actual
window PNGs. These attached-debugger checks prove behavior, not speed or
an isolated shotgun waveform. Earlier Q2 rerelease glass evidence remains
historical; this Q1 mip fix does not claim a new Q2 glass retest.

## Installed Q1 skill-message and status-border proof

The 9ccde2b3 candidate was built at 14:09:15 CDT and installed at 14:19:25
with the copied-owner GL qualification through the required installer.
Receipt: `installed-m0-save-hud-20261007.json`. Exact candidate and then
shipped checks cover classic and rerelease, CPU and NVIDIA GL at 640x400.
Shipped bundles are `/tmp/qa-private-av-_w49xpdm` (classic CPU),
`/tmp/qa-private-av-jtvfnm48` (rerelease CPU),
`/tmp/qa-private-av-x9nit_3p` (classic GL), and
`/tmp/qa-private-av-l0sv1y7_` (rerelease GL).

Each uses genuine W input through the retail start-map Normal hall.
`THE586-normal-hall-first.png` and `THE586-normal-hall-repeat.png` show
one replacing skill message, horizontally centered at 35% height. The
actual shared HUD draw is at (320,140), with no centerprint copy in console
notify. `THE587-exposed-status-band.png` shows the WAD backtile on both
sides of the bar; returned scene data records two repeat-UV panel draws
covering the exposed edges. Root opened all four shipped window captures.
All complete later world frames, public quit0, unchanged owner originals
and package pins, and cleanup of recorded owned processes. These functional
debugger checks make no performance or audio-fidelity claim.

THE-587 is In Review. THE-586 stays In Progress for the added ordinary
Q3 diagnostic-line join report. Its shared bot diagnostic correction is
committed as cb59918d but not yet qualified or installed at this checkpoint.
THE-588 separate-slot naming is installed; vanilla original load and resave
pass, but the combined QASV restore fails with a captured player-archive
canonical-table/settings-root rejection in `/tmp/qa-private-av-68cn9khr`.
That bundle preserves both coexisting save files and the failed restore.
No combined-save load pass is claimed.

## Combined save restore and Q3 notify proof at 15:01 install

This record supersedes the failed restore and uninstalled notify status
above. The 1ff8f190 candidate was built at 14:59:06 CDT and installed at
15:01:38 through `tools/install_qualified_build.py`. Receipt:
`installed-m0-custom-restore-notify-20261007.json`. Exact candidate
`/tmp/qa-private-av-v_c34dzi/qualification.json` passed stock and combined
gameplay, public load/resave and normal quit with copied owner settings.
The qualified installation contains the shared bot diagnostic fix
cb59918d and rejected-console-line fix bc3315ea as well.

THE-588 shipped repetition is `/tmp/qa-private-av-1vkd4s_o`. In the same
private profile, `autosave-q1-classic-id1.sav` is 40,570 bytes of original
v5 text and `autosave-custom-q1-classic-id1.sav` is 290,717 bytes of QASV.
The vanilla bytes remain exactly unchanged after combined launch. Public
load in each file's own mode replaces the actual world/session, retains
the intended providers and resumes completed gameplay. Public resaves
remain v5 and QASV respectively. The combined session uses Q1 classic
start, Q3 movement/Ranger/weapons and Q2 rerelease monsters. Both launches
quit normally with code zero. Root opened the stock and combined restored
gameplay PNGs. Package pins and the original 34 owner settings files are
unchanged; all owned processes are cleaned up. This is functional
debugger-attached proof, with no audio or performance claim.

Restore corrections are 328c6ee8, 63720db4, 3e4d5fcc and 1ff8f190.
Restored seat archives no longer compare against the old frontend cvar
table; map callback binding does not rerun fresh-rule initialization;
compacted resource IDs retain their saved serials; restored Q3 inventory
can reconnect its imported leases before the restore flag is cleared.
No save fields, asset bytes, fingerprints or user-facing formats were added.
The actual shared serial parser preserves IDs 64,65,66,67,84,85 and chooses
86 next, with overflow rejected.

Failure-return count across the seven changed restore files versus the
9ccde2b3 installation: zero added and two removed, 481 to 479 source lines
matching `return fail`, `return *_fail` or `application_fault`. Four
existing valid-state rejection conditions were narrowed. The scope excludes
unrelated protected save work and is not a runtime reachability count.
Full method and removed sites are retained in
`/tmp/qa-the588-proof-20261007/persistence-failure-path-count.json`.
THE-588 is In Review. Separate M9 THE-595 tracks original v5 import from a
custom session; this round proves each file in its own mode only.

THE-586 shipped Q3 GL notify proof is `/tmp/qa-private-av-aokb_1oj`.
`THE586-ordinary-notify-lines.png` visibly shows the two genuine public
echo outputs on separate ordinary notify rows, and the actual console draw
contains both eligible row sequences. Startup bot and renderer diagnostic
rows are also separate. The private route sets `con_notifytime 30` and
uses a real F8 binding after closing the console, then captures before
another console toggle clears notify. These overrides affect only the
copied profile. Actual SDL output is captured privately, world frames
advance, public quit returns zero, and package/profile pins and owned
cleanup pass. Root opened the PNG. THE-586 is In Review; the supervisor
has closed THE-587 after its four-case proof.

The same Q3 route proves stock 1023-byte command truncation and dispatch
of both valid suffix commands exactly once. THE-145's non-Q3 rejection
cases still need their live checks before its final review transition.

## Original Q3 weapon keys and continued world after firing

THE-592's requested GL movement recheck passes on the 15:01 installation
in `/tmp/qa-private-av-ucg44oys`. Its client route and qualification finished
before the app-server interruption. Three separate Mouse1 shots each use
one shell and retain current source/world frames, then real S input moves
141.453491 units with a matching completed world. Root opened
`THE592-after-real-s.png`. The shared audio consumer records 4,792 calls
with no rejected return or observer error. No rare actorless position call
occurs in this GL run; the earlier CPU packet proof remains separate.

THE-562's outstanding weapon-key recheck is covered by that GL run and a
new current-installed CPU run, `/tmp/qa-private-av-8v71pe4s`. In both,
real key 1 selects gauntlet and real key 3 selects the authored picked-up
shotgun. Raw usercmd, original GAME player state and CGAME selected weapon
all agree on 1 and then 3. The exact successful world captures match source
time 4400 and 5500 ms. The corresponding PNGs display the retail weapon
selection and world; root opened CPU shotgun and GL gauntlet captures.
CPU also proves three shots and a later 198.319824-unit S walk. Both
routes use the copied owner profile, private display and captured SDL
output, public quit zero, unchanged artifact/profile/helper pins and
cleanup of owned processes. No stock movement, presentation flags or
Source state were injected. Attached functional observers make no timing
or isolated sound-fidelity claim. These proofs cover the key-selection
gap after the existing packed-sort/fog fixes; THE-562 is ready for review.

## Classic Q1 surfaces on the installed build

THE-213 now has classic id1 e1m1 CPU and NVIDIA GPU0 GL captures on the
15:01 installation: `/tmp/qa-private-av-51mbqa2o` and
`/tmp/qa-private-av-xazvynpi`. Root opened spawn, entrance corridor,
water, submerged teleporter and exit slipgate images. Both renderers show
textured surfaces and complete world views at these locations; no uniform
solid-color replacement or black world appears in the inspected images.
Every PNG was captured from the stopped private window after a fresh,
phase-matching completed presentation. The actual scene/frame sequences
match, source clocks advance, and the ordinary entrance uses real W input.
The later inspection vantages use explicitly recorded public noclip and
keyboard movement; they are not an owner gameplay-route reproduction.

The authored BSP census finds no `{` fence texture or palette-255 texel
at any mip in classic start/e1m1 (81 textures combined). These maps cannot
prove a classic fence fix that their content does not contain. The exact
owner solid-color spot remains unidentified, so this evidence bounds the
classic observation rather than claiming the original symptom reproduced.
The previously reviewed MG3 distant-fence and Q2 rerelease TRANS33 evidence
remain separate. Both new classic runs copied the owner34 settings, kept
their originals and installed package pins unchanged, captured private SDL
output, quit normally with exit zero and left no owned process running.
Attached functional observers make no timing or sound-fidelity claim.

The first capture helper stalled inside its broad synchronous census
before the PNG call; its uninstrumented log cannot narrow that helper
stage further. The replacement uses scalar scene metadata and performs
no debugger pixel scan. That failed helper run is not an engine defect.

## Q2 local status bar on the installed build

THE-593 is implemented by 388df05f and a40b8010. Local native Q2 now feeds
the existing original layout interpreter with the same typed stats and
installed picture resources used by network presentation. Generic vitals
and inventory tiles are suppressed when that source HUD is active.
The candidate was built at 15:39 CDT and qualified against a fresh copy
of the owner's 34 settings files before installation at 15:55 CDT.
The installer receipt is `installed-m0-q2-hud-20261007.json` under the
recovery cache; the installed source revision is a40b8010.

Four subsequent installed-binary base1 checks passed:

| Game | Renderer | Evidence bundle |
| --- | --- | --- |
| Q2 classic | CPU | `/tmp/qa-private-av-v128lln8` |
| Q2 classic | NVIDIA GPU0 GL | `/tmp/qa-private-av-2p9xycad` |
| Q2 rerelease | CPU | `/tmp/qa-private-av-a9gd2v7l` |
| Q2 rerelease | NVIDIA GPU0 GL | `/tmp/qa-private-av-4yw2eead` |

Root opened the relevant PNGs. They show original status-bar numbers and
icons, with no generic Health/Armor/weapon tiles. Each matching completed
scene executed the source layout and resolved its installed pictures;
health was 100 in both the player view and HUD stats. Zero ammo, armor
and timer values correctly leave those conditional displays absent.
Real W input moved the player and advanced the world. These captures do
not exercise an active powerup timer, full inventory, or every pickup.

Classic owner-profile captures have actual FOV 120, cl_gun 1 and hand 2.
The original client hides its view weapon for FOV above 90
(`quake-2/client/cl_ents.c:1298-1304`). Public commands in the private copy
set FOV 90, hand 0 and cl_gun 1; subsequent CPU and GL images show the
blaster. No view-weapon code was changed. The rerelease retains the owner's
FOV 120 and shows its blaster. Original renderer hand-2 handling is distinct
from the server's centered projectile source; these checks make no claim
that changing hand has updated every client userinfo consumer.

All four runs reached advancing retail gameplay and quit publicly with
exit zero. Owner files, installed package and helper pins stayed unchanged,
and recorded owned processes were cleaned up. Audio was contained and
captured privately, but no isolated sound-fidelity claim is made. Attached
functional observers provide no performance measurements. Original Q2
modules, mixed-mode HUDs and full campaign behavior remain outside this
four-case proof.


## THE-180 / MIKE-11: installed split-screen matrix after recovery

All ten native edition/backend cases pass on source 338020d1, built
16:57 CDT and installed 17:00 CDT through the qualified installer.
The candidate first reached retail Q2 rerelease gameplay with a fresh
copy of the owner's 34 settings files, rendered its source HUD, moved
from real keyboard input, produced captured private SDL audio, and
quit publicly with exit zero. The original profile stayed unchanged.
The installer receipt is `installed-m0-rr-coop-hud-20261007.json` under
the recovery cache.

Three source fixes address the failed earlier matrix. 2fcd18a4 converts
canonical cvar flags for the active dialect and writes the real Q2
client reservation before allocation. 5e9c208d retains bounded storage
for all four local Q2 client samples, including a fourth seat joining a
retained world. fc9d3163 supplies the rerelease coop status bar's missing
`loc_rstring` argument count. That malformed string consumed `endif` as
text and canceled presentation even when the lives condition was false.
The correction matches rerelease `g_statusbar.h:49-55`; the shared HUD
interpreter is unchanged. Builds and all six configured checks pass.

| Native content | Renderer | Evidence bundle |
| --- | --- | --- |
| q2-rerelease-baseq2 | CPU | `/tmp/qa-private-av-5hz_k7ob` |
| q2-rerelease-baseq2 | GL | `/tmp/qa-private-av-jyd17e9l` |
| q2-classic-baseq2 | CPU | `/tmp/qa-private-av-_j7s4env` |
| q2-classic-baseq2 | GL | `/tmp/qa-private-av-3wzgqavc` |
| q3-baseq3 | CPU | `/tmp/qa-private-av-2qr0hr0b` |
| q3-baseq3 | GL | `/tmp/qa-private-av-owf1q6zy` |
| q1-classic-id1 | CPU | `/tmp/qa-private-av-_6wlhgkg` |
| q1-classic-id1 | GL | `/tmp/qa-private-av-yp681gr3` |
| q1-rerelease-id1 | CPU | `/tmp/qa-private-av-cp3x7p13` |
| q1-rerelease-id1 | GL | `/tmp/qa-private-av-u6zomylq` |

Each case records three actual SDL virtual-controller instances routed
to distinct full player actors and DRAW-bearing world cameras. Separate
look, movement and neutral release affect the selected seat. Public
Controls adds a fourth player; real W moves that player's body and
camera independently. Removing that seat preserves the survivors.
Disconnecting the first controller while held releases its input, and
remaining controllers operate their surviving routes. All recorded
players are alive at the qualified control cuts; Q2 mode, allocated
client rows and actual actor/slot bindings agree.

Root inspected each four-viewport PNG. Every case quits publicly with
exit zero, preserves the installed package and owner settings, and
cleans up recorded owned processes. The exact index is
`/tmp/qa-the180-split-recovery-v3-20261007/installed338-matrix.json`; each
bundle contains event/console logs, sampled state, qualification results
and PNGs for the input and seat-lifecycle steps. Earlier failed results
remain preserved and are superseded only within this native matrix.

These are private SDL virtual-device and X keyboard checks, not physical
controller hardware proof. The startup observer detaches before the
input route; the GL matrix uses private software GL with no GPU binds.
No performance or audio-fidelity claim follows from these runs, which
use `--no-audio`. Original modules and combined configurations are
outside this ten-case proof.

## THE-423 / MIKE-35: Original bot combat and map travel

Source 18b27fa1 builds with all six configured checks passing. All 3,061
SDK source inputs byte-match the committed source. It was built at
17:09:53 CDT and installed at 17:21:17 CDT through
`tools/install_qualified_build.py`, using the exact candidate's fresh
copied-owner-profile qualification. All three installed files are
byte-equal to that qualified package. The receipt is
`installed-m0-original-combat-20261007.json` under the recovery cache.

The Original CPU candidate bundle `/tmp/qa-private-av-cemhxhvb` passes
public map travel through q3dm1, q3dm7 and q3dm1, adds actual skill-5
Sarge, Visor and Bitterman bots, then records five genuine bot deaths
(two rocket and three plasma) followed by physical Mouse1 respawns.
Actor-matched obituary, dead player state, manual-respawn eligibility,
actual attack input and a later alive state with advancing command and
spawn counts establish each cycle. The current world continues before
public quit exits zero. Root inspected the final travel and respawn PNGs.
The original 34 settings files and candidate pins stay unchanged, and
all recorded owned processes are removed.

The fixes retain one shared admission and presentation path. Fresh
Original bots enter the existing navigation admission. Source bot
entity numbers remain stable through removals, preserving the original
WORLD/NONE values. A replacement GAME host now starts before CGAME binds
it; no weaker host check or fabricated context is added.

Earlier exact installed338 native CPU bundle
`/tmp/qa-private-av-31b07e5d` qualifies nine bot deaths and Mouse1 respawns,
including three rocket and two plasma cycles, continued world frames
and quit zero. Native GPU0 GL bundle `/tmp/qa-private-av-ncsqqbrm` meets
the combat and survival checks with nine physical respawns, including
two rocket and two plasma cycles, but public quit exits one on
X BadWindow from X_TranslateCoords. Its entire qualification is failed;
the caller and resource ownership still require a stack capture. Current
installed GL checks remain open, so THE-423 stays In Progress.

These functional checks use private displays, captured private SDL Pulse
output and read-only debugger observations. Public overrides in the
private profile set unlimited FFA and manual respawn, confirmed from
actual GAME consumer values. Original entry is direct, excluding menu
proof. Its autosave still reports the existing THE-585 console-ownership
defect; save behavior is excluded. No timing, full campaign, module
composition or audio-fidelity claim follows from this bounded proof.
