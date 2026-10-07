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
| THE-423 / MIKE-35 | Death/respawn crash | BLOCKED: no genuine repeated rocket/plasma deaths; Native addbot-after-travel exits1, filedTHE-589. |
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
