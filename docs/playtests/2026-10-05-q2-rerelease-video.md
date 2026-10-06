# MIKE-04: Quake II rerelease video and early crash

The actual `qa-c` now renders `base1`, walks normally, presents separate health/armor/weapon tiles, and continues after firing at a barrel in the 2023 Quake II rerelease. The video checks cover CPU and GL output; the natural barrel check covers GL. These are bounded live checks of the reported startup and first-fire problems.

Two shared frontend faults were corrected. A looping sound emitted by a projectile could outlive its emitting actor within the same frame. Resolving that expired actor's audio identity made the frontend exit. Commit `59c89987` discards entity loops after the full emitting actor lifetime ends, before identity lookup. It retains ordinary one-shot and ambient events. The original `G_FreeEdict` clears the entity, including its looping `s.sound`; the original server also excludes empty entities from client frames. The corresponding reached C path is `src/app/frontend/map_events.c:frontend_event_sound`.

The second fault used Source-provided health/ammo values as proof a native HUD had already been drawn. That put the weapon's native corner overlay on top of the generic health tile. Commit `0f6d5671` makes the shared HUD use actual native HUD ownership instead. Health and armor remain visible; the weapon occupies its own tile. An unlimited-ammo Blaster has no extra Ammo 0 tile. A Shotgun shows its actual ammo once. This also fixes the classic Q2 presentation and the shared remote-Q1 ownership call. Q2's original `G_SetStats` only supplies an ammo icon/count for a weapon with an ammo item; the TypeScript HUD distinguishes Source vitals from native status presentation.

The four video/HUD sessions used build `6b4d8123`, the actual shipped executable, installed retail `base1`, genuine X11 keyboard input, and read-only debugger observations. Each started with the Blaster, walked with normal player physics, released the key, selected a Shotgun using the public `give`/`use` commands with cheats enabled for that controlled inventory check, rendered another 60 frames, and quit normally.

| Edition | Renderer | Health / armor | Shotgun ammo | Native HUD flag | Continued frames | Exit |
|---|---|---|---:|---|---:|---:|
| Classic | CPU | 100 / 0 | 10 | false | 60 | 0 |
| Classic | GL | 100 / 0 | 10 | false | 60 | 0 |
| 2023 rerelease | CPU | 100 / 0 | 10 | false | 60 | 0 |
| 2023 rerelease | GL | 100 / 0 | 10 | false | 60 | 0 |

Completed mapped-window screenshots show intact world geometry and the separate HUD tiles. The rerelease screenshots also show the actual Shotgun model. The installed classic `q2/baseq2/config.cfg` sets `hand "2"`, which intentionally hides the view weapon; the registered engine default remains `hand "0"`. Separate short CPU and GL sessions used the public `hand 0` command and visibly rendered the Blaster model, then continued 12 frames and quit normally. The installed setting and its startup execution were checked separately from those screenshots.

The natural rerelease combat session used the preceding `f504` build, which already contained the expired-loop fix. Genuine movement, mouse aim, and fire killed barrel actor 341 with 15 damage and triggered 108 chain damage on actor 340. Actor 341 disappeared from the actor registry, physical world, and Source game together. The world then rendered 251 more frames and quit normally. That session used only the public `hand 0` and `quit` console commands. Blaster and breaking-glass voices reached the mixer and produced accepted nonzero SDL dummy-device PCM queues. No game state was changed through GDB.

Machine-readable evidence is in [2026-10-05-q2-rerelease-video.json](2026-10-05-q2-rerelease-video.json). Raw screenshots and runtime receipts remain in the private playtest archive. Debugger observations affect timing, so these runs make no performance claim; dummy audio does not prove physical speaker output. They qualify the reported `base1` problems, without claiming every campaign map or menu.
