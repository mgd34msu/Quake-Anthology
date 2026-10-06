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
