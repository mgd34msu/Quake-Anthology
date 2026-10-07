# Shipped owner checks, 2026-10-06

## Copied-profile checks, installed `ab78171b` on 2026-10-07

The 03:51:48 CDT installation passed private copied-owner-profile startup before replacement. The installed executable then passed the following bounded checks with fresh copies of the same 34 settings files. Original profile bytes and installed file identity remained unchanged; public quits returned zero and all recorded owned processes were removed.

| Item | Installed check | Result and scope |
|---|---|---|
| THE-163 / MIKE-07 | Native Q3 `q3dm0`, CPU and GL; real movement onto the shotgun, then physical keys 1 and 3 | Weapon ownership 0→1, shells 0→10, autoswitch 3; selected/usercmd/actor weapon all followed 3→1→3. [Detailed pickup/audio bounds](2026-10-07-q3-pickup.md). |
| THE-153 / MIKE-05 | Native Q3 `q3dm1`, CPU and GL; stationary and moving left/right/up/down mouse input | All cases matched authored sensitivity and angle-word rounding, with no recentering through at least 1,000 ms of settled Source time. Source and completed camera agreed, allowing the actual authored movement bob. Startup debugger detached before input. Original mouse behavior and Mike's retest remain unproved. |
| THE-420 / MIKE-34 | Native and Original Q3 `q3dm1`, CPU and GL; Space hold through landing, release/repress, C hold/release, two public kill/Mouse1 respawns | Exactly one jump event per press; held flag persisted until release; world camera stood/crouched/stood at offsets 26→12→26. Respawn samples had zero velocity. These deaths started from grounded idle and do not independently exercise inherited nonzero corpse momentum. No new input replay fault was reproduced. |
| THE-431 / MIKE-36 | Exact Custom combination, GL: classic Q1 `start`, Q3 movement/Ranger/weapons, Q2 rerelease monsters, single player; real walk/jump/mouse/fire and Normal teleporter | Gameplay rendered, ammo 25→24, teleporter yaw 88.939819→90 persisted in body/completed motion/retained view/returned camera. Actual ENGINE input ownership was recorded; CLIENT/CGAME receivers were not exercised. Prior CPU proof remains separately dated. Repeated Normal centerprint also enters console notifications; the shared routing defect is tracked separately under THE-285. |

Evidence identifiers: `qa-the153-mouse-20261006/qualification.json`, `qa-the420-contained-20261007/ab78171b-consolidated-proof.json`, and `qa-private-av-mnoe1g9d/user/evidence/m0-reviewed-evidence.json`. Earlier missing-window-manager and camera-target observer failures are retained as helper failures, not gameplay defects. Original world-camera proof filters out later no-world HUD icon scenes. Input-state and combined-mode checks use bounded read-only debugger observations; none establish frame time. All displays and audio servers were private. Except for the separately recorded pickup output capture, audio was disabled. Owner retests remain pending.

THE-420 remains In Progress. A subsequent native GL check held Space for two seconds, released and pressed it again for 1.5 seconds, then held C for 1.5 seconds. X autorepeat was enabled at its unchanged default delay of 660 ms and rate of 25 Hz. Actual SDL key events retained their repeat flags. The four configured `com_maxfps` values and `r_swapInterval 0` were read back from the installed engine.

| Configured `com_maxfps` | Repeated Space key-downs, first/second hold | Repeated C key-downs | Jump events / real presses | Observed presentation cadence per second |
|---|---|---|---|---|
| 85 | 34 / 21 | 21 | 2 / 2 | 49.03 |
| 0 | 34 / 21 | 21 | 2 / 2 | 49.27 |
| 250 | 33 / 21 | 21 | 2 / 2 | 48.08 |
| 333 | 34 / 21 | 22 | 2 / 2 | 49.23 |

All eight real presses produced exactly eight jump events. The held-jump flag stayed set through 292 grounded samples, and 338 crouched samples retained the duck state. Release cleared the held state, and the next press jumped once. Each run quit normally, removed its owned processes and preserved the installed artifact and all 34 owner settings files. Evidence: `qa-the420-repeat-20261007/ab78171b-native-gl-autorepeat-proof.json`.

These are configured frame-rate conditions; the private software GL runs did not achieve 85, 250 or 333 fps. The missing visible-frame cap consumer is tracked under THE-583. Original Q3 autorepeat remains untested, and no new repeat-input production patch is supported by these results. Frame rate is a test condition, not an established cause. Original Q3 ignores a repeated button source in `cl_input.c:83–84`; `cl_keys.c:1031–1036` only counts key repeats. `PMF_JUMP_HELD` is checked, cleared and carried across substeps in `bg_pmove.c:370–378`, `1916–1918` and `2061–2062`.

An additional detached native GL check (`qa-private-av-69ycntmw/result.json`) recorded three jump events for three presses and 103 grounded-held samples without retriggering. An airborne public kill preserved nonzero corpse momentum; the first live post-respawn sample had zero horizontal velocity and vertical velocity from the subsequent gravity step. It does not measure velocity at the spawn function return.

## Q1 skill gates and persistence, THE-345 / MIKE-28

Native classic Q1 now has completed CPU and GL checks for all four authored skill gates in `start.bsp`, followed by the ordinary episode-one slipgate into `e1m1`. The checks use real keyboard input. No console skill assignment or position write selects the difficulty.

| Skill | Retail e1m1 excluded entities | Spawned monsters | CPU artifact | GL artifact |
|---|---|---|---|---|
| Easy, 0 | 46 | 10 | `bb7f2ec1` | `5ae7b91a` |
| Normal, 1 | 33 | 23 | `bb7f2ec1` | `5ae7b91a` |
| Hard, 2 | 18 | 42 | `5ae7b91a` | `5ae7b91a` |
| Nightmare, 3 | 18 | 42 | `5ae7b91a` | `ab78171b` |

Every case matched the exact excluded indices among the retail map's 369 authored entities and wrote the corresponding original-save skill header. The historical seven cases share four session receipts, recorded in `the345-gates-20261007/historical-seven-receipts.json`; their artifact identities remain separate.

The final GL Nightmare case ran on the 03:51:48 CDT `ab78171b` installation with a fresh copy of all 34 owner settings files. The hidden gate set public `skill` to 3 and canonical `g_spSkill` to 4. The next map spawned with skill 3, deathmatch 0 and coop 0. Public save, load and resave retained skill 3 and the 42-monster roster. Public quit returned zero; all 30 recorded owned process tokens were absent, the private display socket was removed, and the artifact and original settings bytes stayed unchanged. Evidence: `qa-private-av-0wjt9qrv/user/the345/qualified-gl-nightmare.json` and its final cleanup receipt. The historical CPU Nightmare case also completed load/resave.

The installed build contains the existing carry/rounding fixes `6b809853`, `66d5744b` and `3c5e8bfb`. Earlier route, observer and focus-helper failures remain separate from these successful gameplay checks. These runs cover native classic Q1; Original QC, rerelease, mods, audio and performance are not qualified here. Mike's retest remains pending.

## Earlier installed `685cab51`

Artifact: `qfiles/qa-c`, commit `685cab51`, 138,906,552 bytes. GCC and Clang full builds and the six registered checks passed. The executable and its twelve sibling native-runtime files matched the installation receipts.

| Item | Actual check | Result |
|---|---|---|
| MIKE-18 | Native Quake classic `e1m1`, CPU 640×400: start corridor and slipgate photographs after public keyboard/console navigation | No textured trigger volume in either view; public quit exited 0. |
| MIKE-19 | Original Q3 `q3dm1`, CPU and GL 960×600: stock HUD/score panels, restart centerprint, typed and received chat, notification expiry | Text and panel transparency present; no black font rectangles. Both public quit paths exited 0. |
| MIKE-16 | Native Q3 `q3dm1`, GL 960×600: W movement, mouse look, fire, public `map_restart 0`, quit | Camera changed, ammunition fell 100→95, restart rendered a fresh spawn and transparent FIGHT text; quit exited 0. |

These used the installed executable on private displays, without a debugger. Their processes and display servers were removed. Q1 used public `noclip`/`notarget` to photograph the two areas; this is a visibility check, not a movement or collision qualification. Q3 audio was disabled. No frame-time, all-map or all-feature claim follows from these cases.

Local evidence identifiers: `world-q1-classic-id1-cpu-685cab51-isbxad34` (`02-corridor.png`, `03-slipgate.png`) and `native-original-q3-685-proof.json`, whose three cases include exact actions, exit codes and screenshots. Evidence remains outside the repository; no installed game archives or assets are committed.

Still open in this round: MIKE-17 Q1/Q2 transparency and MIKE-20 particle retests on the latest artifact. Original Q2 rerelease still fails during guest startup: the GS null-context fault is replaced by a structured operand-monitor failure at guest PC `0x18013808d`. Its startup and effects are not qualified.

Additional installed artifact: `82e5cc4f`, 139,409,432 bytes. Both full compiler builds and their six registered checks passed; main executable and twelve runtime files matched the installed receipts.

| Item | Additional actual check | Result |
|---|---|---|
| MIKE-17 | Native Q1 rerelease MG3 `hub`, near/far grate views, CPU and GL 640×400 | Slime visible through grate holes, without an opaque sheet or pink edges. |
| MIKE-17 | Native Q2 rerelease `base1`, right-hand TRANS33 window, CPU and GL 640×400 | Machinery and orange fixture visible through the glass. |
| MIKE-20 | Native Q2 rerelease `base1`, stock `give all`/`use` commands with startup cheats enabled and genuine Mouse1 input, CPU and GL | Yellow blaster particles, gray rocket smoke/light, blue rail spiral/white impacts, and green BFG beams/particles. Rocket ammunition 100→98; rail 100→99; BFG cells 300→150 after three shots. |

All six qualified runs exited 0, kept the executable unchanged and removed their processes/private display. Root also inspected the CPU grate, both glass images, CPU rail and GL BFG images. `owner-alpha-effects-82-proof.json` records every case, command, selected photograph and cleanup. Audio was disabled. These do not qualify other surfaces/maps, Q1 particles, Original RR or frame-time targets. An earlier effect trial omitted the stock cheats setting and is excluded because it did not equip the requested weapons.

The installed Original RR retry now passes the earlier operand-monitor failure and executes `default.cfg`, then stops before public frames at an SDK callback CFG mismatch. Commit `dce85a30` fixes the registry lookup and passes bounded guest callback checks; its installed retry remains pending.

The additional Q1 classic `e1m1` MIKE-20 check passed on installed `82e5cc4f`, CPU and GL at 640×400. Public `god`, `notarget` and `impulse` commands equipped the stock shotgun and rocket launcher; genuine Mouse1 input produced white shotgun impact puffs, orange rocket explosion particles, fire/light and gray smoke. Shells fell 100→99 at the first photographed shot; rockets fell 100→96 after the firing sequence. Root inspected both rocket images. Both runs quit with exit 0 and removed their processes/private display. Audio was disabled. `owner-q1-effects-82-proof.json` records these bounded local effect checks.

Installed `bf27a858` includes the SDK callback-registry fix. A bounded Original RR retry reached `==== InitGame ====`, with the native child consuming CPU and the parent waiting for its response. The window remained black and no public gameplay frame was observed. The earlier CFG failure did not recur in that run; successful Original RR startup and gameplay remain unproven. Its diagnostic processes were terminated and removed.

The current installed `b9140af8` (139,652,928 bytes) passed the eight MIKE-17/20 checks below at 640×400. Both full compiler builds and their six registered checks passed before installation. The executable and twelve runtime files matched the installation receipts.

| Item | Current installed check | Result in CPU and GL |
|---|---|---|
| MIKE-17 | Q1 rerelease MG3 `hub`, near grate | Slime visible through the actual grate holes; no opaque sheet or pink fringe. Far views have recorded camera/geometry occlusion and do not establish a clean distant-view result. |
| MIKE-17 | Q2 rerelease `base1`, spawn TRANS33 glass | Machinery and orange fixture visible through the glass; three floor pickup models remain visible. |
| MIKE-20 | Q1 classic `e1m1`, shotgun and rocket launcher | White shotgun impacts; orange rocket explosion, gray smoke and illumination. Shells 100→99 at the first shot and 100→97 after the sequence; rockets 100→96. |
| MIKE-20 | Q2 rerelease `base1`, blaster, rocket, rail and BFG | Yellow blaster projectile/light, orange and gray rocket particles, blue rail spiral/white impact, green BFG particles/light. Rockets 100→96, rail 100→97, BFG cells 300→150. The GL capture also shows green BFG beams. |

The checks used genuine keyboard/mouse input and existing public commands, with private settings and displays. Weapon tests used stock equipment cheats. All eight public quits exited 0; every actual game process and the private display were removed, and the installed artifact remained unchanged. Root inspected selected grate, glass, shotgun, rocket, rail and BFG images. `owner-alpha-effects-b914-proof.json` records the actions, selected images and cleanup. Audio was disabled; these results establish the listed local visual effects, not audio, frame times, every effect, or Original RR startup.

MIKE-19 was also rechecked on installed `b9140af8`: Original Q3 `q3dm1`, CPU and GL 640×400, stock centerprint, typed chat, colored received chat, notification expiry, HUD numbers/head and score panels. Text was readable and transparent, with no black font rectangles. Genuine `/map_restart 0` console input produced a second game initialization and a restored world/HUD in both cases. Both public quits exited 0 and removed the actual processes/private display; the artifact stayed unchanged. Root inspected the CPU received-chat and GL restored-HUD images. `owner-q3-font-chat-restart-b914-proof.json` contains both qualified cases. An earlier private-helper run omitted the slash and sent chat instead; it is retained and excluded from restart proof. Audio was disabled. These qualify the listed Original Q3 display/restart paths, not all native Q3 menu/preset paths or audio.

## Installed `8ba209ad`: Native Q3 input after control changes

Fresh Native `q3dm1` preset/map sessions passed actual keyboard and mouse
checks in CPU and GL at 640×400. W moved 67.99 and 67.57 units respectively;
release cleared the raw forward command from 127 to zero. After the authored
ledge fall and landing, velocity and additional settled drift were zero.
Mouse input changed the view from `(0, -45, 0)` to
`(-2.37305, -49.75159, 0)`. That orientation persisted while idle and matched
the presentation camera. Genuine Mouse1 firing reduced stock machinegun
ammunition from 100 to 95 in both cases.

Both public quits exited zero, with no observer errors. The installed
executable stayed unchanged, and every owned game/debugger process and the
private display were removed. Receipt: `native-q3-input-8ba-proof.json`.
The existing read-only debugger observer recorded these state values without
inferior calls or writes; these runs are excluded from performance timings.
Earlier stale-helper-field and airborne-stop-predicate attempts are retained
and excluded. This check does not qualify Original Q3, pickups, audio,
multiplayer, split-screen or every map/weapon.

## Native CPU precision, installed `d01326c6`

Eleven real installed-build checks passed after the shared float-depth and
four-pixel renderer migration. The owner paths used the copied owner profile
and genuine default Space, mouse3 jump and mouse2 forward, without rebinding.
CPU CLI, GL CLI and genuine GL menu launch all survived the Normal slipgate
and fog cleanup, wrote original v5 saves, moved away with S, loaded and
resaved the exact origin/angles/health/weapon/ammo/items. The stock ammo 25
HUD remained. Saves measured 32,650–33,403 bytes.

Separate rendering routes inspected classic e1m1 corridor/slipgate trigger
visibility; Q2 rerelease base1 glass/world/HUD at CPU 640×400 and 320×200;
Native Q3 spawn textures/HUD; the MG3 hub grate; Original Q3 centerprint,
typed/delivered colored chat, expiry and score panels plus genuine public
map restart; Q1 shotgun/rocket particles; and Q2 rerelease blaster, rocket,
rail and BFG effects. The near grate exposes its green backing through holes;
its aligned far backing limits independent depth evidence. Brief residual
rocket smoke is documented. Effect/navigation cheats and private bindings
were separate from owner movement acceptance. Root inspected the stock
post-load HUD, rerelease spawn/glass, Q3 world/HUD and chat, and near grate.

All eleven actual quits exited zero, removed their processes/private display,
and preserved the owner settings and installed executable. These are bounded
views and actions, not all maps/campaigns/audio or a speed qualification. The
nonfatal demo-recording warning after classic load remains. Receipt:
`owner-cpu-renderer-d01326c6-proof.json`.

The actual Original Q2 rerelease retry no longer hit unsupported
`Bot_UnRegisterEdict` slot 49 within its bound, but still produced no public frame
in 40 seconds. Its parent repeatedly handled a retail CRT 24-byte node free
while the native child waited; the shared VM range code did quadratic scans
of unrelated allocations/mappings. `00644e83` simplifies that existing
shared mutation path. The new installed startup retry remains required; no
Original rerelease gameplay is claimed. Receipt:
`original-rr-import-boundary-jwbs8ojy/summary.json`.
## Original Q2 rerelease, installed `6f430efa`

The shared VM change passed full GCC/Clang builds and registered checks before
installation. A 40-second stock `base1` retry reached `InitGame` and progressed
beyond the earlier tiny-free loop, but produced no public frame. Its retained
stop metadata identifies a CFG dispatch thunk; sampled stop occupancy alone
does not establish the dominant CPU cost. All recorded processes and the
private display were removed. Receipt:
`original-rr-import-boundary-4rmn69xt/summary.json`.

A longer bounded retry then exited naturally with status 1 at 61.998 seconds.
The DLL emitted its entity-spawning and team-repair output before the first
application failure at 61.897 seconds: its physical client contained nonfinite
SDK motion. The error preceded cleanup; no public frame was produced. Actual
retail disassembly confirms the decoded player-state offsets, so neither an
invented offset change nor a zero-value fallback is justified. The failing
producer remains unresolved. The actual processes and private display were
removed. Receipt: `original-rr-import-boundary-axzwk5ka/summary.json`.


## Installed `10b63336`: owner routes after texture batching

All eleven current-build checks passed, using the same bounded routes as the
preceding fidelity suite. Default Space and the owner's raw mouse3 jump,
mouse2 forward/release, Normal slipgate cleanup and original v5
save→move away→load→resave passed through CPU CLI, GL CLI and genuine GL menu
launch. Player origin, angles, health, weapon, ammo and items were restored
exactly, with the stock 25-shell HUD. Owner settings stayed unchanged.

Separate visual checks retained invisible classic e1m1 triggers, rerelease
base1 glass at both CPU resolutions, the MG3 grate openings, Native Q3
world/HUD, Original Q3 transparent center/chat/score graphics and genuine
map restart, plus Q1 and Q2 rerelease stock weapon particles. Effect and
navigation cheats stayed separate from movement acceptance. Root inspected
current owner jump/load images, the near grate, delivered Q3 chat and
rerelease glass. The far grate's aligned backing and wall-occluded side
trigger photograph do not independently establish visibility.

All actual public quits exited zero, all owned processes/private display were
removed and the installed artifact remained unchanged. The nonfatal classic
load demo-recording warning remains. Audio, Original RR gameplay and CPU speed
are not qualified by these visuals. Receipt:
`owner-cpu-renderer-10b63336-proof.json`.

## Original Q2 rerelease startup, installed `d81a2d66`

The canonical player reader in `d3a009a5` preserved the real module's values
and exposed the failure precisely: spawn origin `(128,-320,33)`, velocity
`(0,0,0)` and view `(0,135,0)` were finite; all view-offset components were
NaN. The error appeared before cleanup at 60.688 seconds, followed by natural
exit 1. Receipt: `original-rr-import-boundary-50spm2j9/summary.json`.

Qsrc's fresh weapon-kick timers divide zero by zero at level time zero. The
native lifecycle omitted the two pre-client world turns from the original
server. `d81a2d66` restores actual `RunFrame(false)` turns for complete classic
and rerelease GAME modules, before `ReadLevel`. False matters: the rerelease
skips true turns before a player is spawned. References are
`quake-2/server/sv_init.c:258-266`, `q2repro/src/server/init.c:195-211`, and
`rerelease/p_weapon.cpp:63-80`. Both complete compiler builds and their
registered checks passed before installing the fix.

The new 90-second and 240-second installed retries produced neither a public
frame nor an application error before their cutoff. Static retail attribution
places the later sampled PCs inside the real JSON save writer. The application
starts that capture only after settling and client admission, so the late
work has progressed beyond the earlier motion rejection. The longer run's
mapping count grew from 6,869 at 135 seconds to 13,734 at 225 seconds. The
current byte allocator gives each small C++ allocation its own page/backing;
that design remains the next implementation target. Successful gameplay and
a complete autosave remain unproven.

Two separate six-second userspace profiles during that longer startup collected
39 parent and 298 child samples, with no lost samples or debugger. The sparse
parent capture includes backing lookup and allocation/mapping work. Most
child PCs are runtime-generated or unresolved; these profiles do not establish
a dominant percentage or qualify frame-time performance. All actual owned
processes and private display were removed. Receipts:
`original-rr-import-boundary-0vozfexn/summary.json`,
`original-rr-import-boundary-y24pu7ya/summary.json`.

The targeted installed Original classic Q2 regression could not begin: this
content installation has no original classic module. It exited 1 before
input/HUD actions and cleaned its process/display without changing settings
or the artifact. No classic gameplay success is claimed from that attempt.
Receipt: `original-classic-q2-d81a2d66-smoke-proof.json`.
