# MIKE-13 and MIKE-14: Quake II aim, sound and pacing

The shipped `qa-c` at `fe1c0a07` passes the reported aim and missing-sound checks on retail `base1` in classic Quake II and the 2023 rerelease. Classic Source frames advance at 100 ms; rerelease frames advance at 25 ms. Client views interpolate between them. **MIKE-14 remains open for rerelease CPU frame latency.**

The shared command and presentation fixes preserve float view angles and the current camera, while world entities interpolate between native Source frames. Weapon and monster muzzle events now reach the shared sound recipes. An entity loop is retired when its emitting actor dies; one-shot sounds survive that retirement. Brush sounds retain their authored spatial origin instead of being replaced with the brush entity's often-zero origin. Classic one-shots use the brush center, following `SV_StartSound` in `quake-2/server/sv_send.c`. Rerelease brush loops use the point nearest each listener. Moving brushes update that position with their actual motion. Ordinary entities keep their existing origin behavior.

The classic retail `CL_GetEntitySoundOrigin` only copies `lerp_origin` and leaves a FIXME for brush models; it does not implement the center calculation for entity loops. The unified brush helper deliberately handles that unresolved case and also serves the frame's audio-position refresh. It does not duplicate asset data or add serialized presentation state.

Four actual-executable runs used genuine X11 controls, installed content and read-only debugger observations. All exited normally without debugger writes to game state.

| Check | Classic | 2023 rerelease |
|---|---:|---:|
| Blaster muzzle error versus Source projection | 0.00000268 units | 0.00000154 units |
| Direction error versus Source projection | 0.000000065 | 0.000000059 |
| Source view direction versus rendered view at first shot, dot product | 0.999999986 | 1.000000007 |
| Blaster mixer gains, left/right | 255 / 255 | 255 / 255 |
| Weapon pickup mixer gains | 255 / 255 | 255 / 255 |
| Door start mixer gains | 106 / 107 | 112 / 111 |
| Door end mixer gains | 48 / 48 | 101 / 100 |
| Positive moving-door loop observations | 9 | 16 |
| Accepted nonzero PCM queues in pickup/door run | 436 | 285 |

The rerelease uses its eye trace when projecting a shot; it is compared with that Source calculation, rather than forcing a parallel ray. Genuine mouse input changes the retained view angle. The four runs observed additional client presentations between Source frames. Fire and movement used ordinary gameplay; the pickup/door route used public cheats and noclip only to reach the authored entities. Dummy SDL output proves decoding, mixing and device acceptance, without proving physical speaker output.

Eight separate timing runs used no debugger and ran serially with other games and builds stopped. Each measured 200 presentation intervals after warm-up at 320×200, with default audio. The moving runs held W for 500 ms, released it, and applied twelve two-pixel mouse movements. Whole-loop wall time includes simulation, rendering, scheduling and the engine delay. Process CPU includes raster workers; a small presentation logger adds overhead.

| Edition/output | Idle median / p95 | Input-window median / p95 |
|---|---:|---:|
| Classic CPU | 9.10 / 10.99 ms | 8.07 / 10.13 ms |
| Classic GL | 8.98 / 10.46 ms | 9.79 / 11.74 ms |
| Rerelease CPU | 46.61 / 56.75 ms | 26.98 / 33.70 ms |
| Rerelease GL | 17.11 / 19.97 ms | 14.75 / 16.78 ms |

These samples identify the remaining rerelease CPU problem. They are not a controlled comparison between editions: the installed classic configuration selects `hand 2`, hiding the weapon, while the rerelease selects `hand 0`. GL used a real NVIDIA RTX 5060 Ti context with swap interval 1. All maxima remain in the evidence, including one 304.98 ms classic GL wall-time outlier without a corresponding CPU or presentation spike; its cause is not attributed.

[Machine-readable evidence](2026-10-05-q2-audio-aim-pacing.json) records the exact artifact, Source commits, measured state, clocks and verification bounds. Raw receipts remain in the private playtest archive. These checks cover the reported `base1` behavior, without claiming complete campaign or performance qualification.
