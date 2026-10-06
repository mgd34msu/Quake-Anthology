# Shipped owner checks, 2026-10-06

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
