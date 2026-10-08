# THE-410 / MIKE-32: shared console and editable chat

Nine captured game and renderer scopes pass the console and chat checks below on installed source `989b43c5d308c1c2e44322dec19657c8131b94c0`.
Commit `25b04ffa` sends editable local chat through the common console execution path in `src/app/frontend/chat.c`.

The exact executable was built on **2026-10-07 at 22:19:48.884555 CDT** and installed at **22:30:42.359842 CDT**.
The qualified installation receipt is `installed-m0-chat-bounds-timers-20261007.json`.
The capture index is `THE410-current989-index.json`, retained with the helper bundle `qa-the410-common-console-messagemode-20261007`.

## Captured scopes

Each named private bundle contains `THE410-bounded-readout.json`, the real input actions, completed observations, screenshots, private audio output, and owned-process cleanup records.

| Game | Renderer | Private bundle | Chat entry | Received output | Public quit |
| --- | --- | --- | --- | --- | --- |
| Q1 classic | CPU | `qa-private-av-ltzqa8kq` | `t` key | Console and HUD text | 0 |
| Q1 classic | Software GL | `qa-private-av-826qj6h4` | `t` key | Console and HUD text | 0 |
| Q1 rerelease | CPU | `qa-private-av-thvs7f7t` | Public `messagemode` | Console and HUD text | 0 |
| Q1 rerelease | Software GL | `qa-private-av-z85s4kg7` | Public `messagemode` | Console and HUD text | 0 |
| Q2 classic | CPU | `qa-private-av-e04nts7x` | Public `messagemode` | HUD chat text | 0 |
| Q2 classic | Software GL | `qa-private-av-2d564oav` | Public `messagemode` | HUD chat text | 0 |
| Q2 rerelease | CPU | `qa-private-av-6t8fh7m2` | Public `messagemode` | HUD chat text | 0 |
| Q2 rerelease | Software GL | `qa-private-av-2hdggzb0` | Public `messagemode` | HUD chat text | 0 |
| Original Q3 | CPU | `qa-private-av-8dfhv8ro` | `t` key | Console and green HUD chat text | 0 |

The four Software GL rows record the actual renderer as Mesa llvmpipe, LLVM 22.1.8, through SDL.
The worker opened the same-pose world pairs, received chat captures, and unknown-command console captures for these nine rows.

## Console and chat observations

Every run submitted twelve genuine keyboard commands through the public console:

- Get `fov`, `cg_fov`, `/fov`, and `/cg_fov`.
- Set `fov` and `cg_fov` to 90 and 120, both with and without a leading slash.

The canonical `cg_fov` value and the `fov` alias share the same captured canonical entry address.
Every set produced the requested value and a matching completed world view at an unchanged camera origin and axis.
Q1 and Q2 observations decode FOV from completed perspective VIEW commands with world DRAW commands.
Original Q3 observations retain the camera and FOV from a successful world render.
The paired `THE410-fov-4-world.png` and `THE410-fov-5-world.png` visibly show the narrower 90-degree view and wider 120-degree view.
All nine runs restored their actual copied-owner prior value, 120.

Each run then submitted an explicit public `say` command and captured its delivered message.
Q2 messages appear in HUD chat captures; the raw console log alone does not contain that delivered text.
The editor received the literal text `qa410 chat; fov 33` while the observed input focus was `QA_INPUT_CHAT`.
Return delivered the message and restored `QA_INPUT_GAME` focus.
The canonical FOV remained 120, so the semicolon and following words did not execute `fov 33`.

Each run also submitted `qa410_unknown_command` as console text.
Q1 and Q2 printed `Unknown command`; Original Q3 printed `Unknown cmd`.
The unknown token remained a command, and closing the console returned to game focus.

Representative files in every successful bundle are:

- `THE410-explicit-say-console.png`, the explicit `say` submission and console response where present.
- `THE410-editable-chat.png`, the actual editor text before Return.
- `THE410-received-editable-chat.png`, the delivered literal chat text.
- `THE410-unknown-command-console.png`, the unknown-command response.
- `user/evidence/result.json` and `user/evidence/samples.jsonl`, the observed fields, real input actions, completed world views, and quit result.

## Profile, containment, and cleanup

Every run used a fresh copy of the owner's 34 saved settings files.
The original profile was read without modification, and its recorded bytes matched after each run.
The executable's device, inode, size, and modification time matched the installed receipt.
The helper pins remained unchanged throughout each capture.
The installed executable and SDK file pins also matched at the end of this nine-scope packet.

All game windows used authenticated private X displays.
Actual SDL audio streams used isolated private audio servers and the private null sink.
Every run retained a nonempty, nonzero captured monitor output.
All nine public quits returned 0, and cleanup left no live process matching the recorded owned PID and start token.

The functional observer remained attached with inferior function calls disabled.
This packet establishes the recorded console and chat behavior; it contains no timing, frame-rate, or broader fidelity qualification.
Owner retest and supervisor closure remain separate.

## Preserved failures and route misses

`qa-private-av-nn42hny4` records the pre-fix Q1 classic CPU failure on source `37828b660751025678e3f118aae7e6fdf2cbcdf7`.
The FOV and explicit `say` stages completed, but Return in the chat editor printed `Q1 engine command has no entered invocation` and retained chat focus.
Its complete FOV stage remains in `THE410-preserved-fov-stage.json`; the packet is not a full console/chat pass.

`qa-private-av-jp17yh4h` records a root-requested stop on that older source before Q1 rerelease chat.
Its partial FOV stage and cleanup quit are retained without a chat qualification.

`qa-private-av-3nhwtft1` records a Q1 rerelease CPU input-route miss on source `989b43c5`.
The retained `t` key route did not open the chat editor under the copied settings, and the engine printed no rejection.
The later Q1 rerelease CPU and Software GL rows use a genuinely typed public `messagemode` command.
The earlier route-miss packet remains separate from those successful captures.
