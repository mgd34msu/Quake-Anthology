# Required integration calls

These are concrete handoff obligations for the remaining graph nodes. A subsystem's source handoff does not complete its application integration. Build and runtime checks follow the complete baseline source gate in `dependencies.json`.

## Session and gameplay

- The application actor-release fanout must retire world, combat, inventory, pickups, and every attached provider's private state, including providers that do not own the actor. Q2 uses `qa_q2_actor_released`; duplicate owner notification is harmless. Generation reuse must not inherit private arsenal, monster, or mod state.
- The application physics router must dispatch `qa_q2_physics_read`, `qa_q2_physics_write`, and `qa_q2_physics_touch`. Q2 arsenal ticks run explicitly in the actor's owning source turn. Independent character and arsenal selection must not schedule the same actor twice.
- Q3 foreign input selections call `qa_q3_arsenal_step` through `qa_q3_controls`; native commands use the Q3 wrapper. Bind movement phase/effect callbacks with prediction authority so prediction cannot apply server damage.
- Q2 map setup supplies the source `.mat` surface names from texinfo through shared world loading.
- B08 still needs original Q1 Quake64 movement. The unsupported branch is unfinished scope.

## Input, display, and commands

- The application owns SDL initialization, the event pump, and final SDL shutdown. Forward SDL events to the one input platform registry. Four seat routes retain independent button, text, gyro, and haptic state.
- Detach input from its window before display destruction or restart. Destroy input before seats, console, and SDL. A renderer must be destroyed before its display.
- Original client-module command adapters call `qa_input_client_dispatch` with their explicit producer instance. Ordinary user-console dispatch passes zero. A console command's owner is not a substitute for the producer instance.
- B21 persists typed seat bindings and controller settings; B34 applies those profiles to actual devices and command builders.

## Scene and media

- Advance shared image/material animations at frame start. Forward movie frame callbacks and text rendering into world/model submission. Prepare shadow casters, render portal children before their parents, submit world/models/effects, finish stencil work, and finalize the frame before synchronous backend execution.
- Keep scene resources alive until execution returns. `QA_SCENE_COMMAND_IMAGE` is the image revision replacement barrier. Material videos use distinct playback identities even when they share immutable source bytes.
- Audio engine bus attachment transfers ownership. Removing a cinematic must detach its raw stream through the engine; do not independently free an attached stream. Pair engine and device pause policy.

## Network and compatibility

- B28 supplies actual gameplay, snapshots, prediction, and seat ownership to the committed codecs. B29 supplies download/browser/admin consumers, including mDNS port 5353. B30 supplies demo/MVD/GTV/TCP consumers.
- Re-admission on composition change retains connection identity and seats through `connections_restart`. Restore must update the unified receiver's actor registry and authenticated controlled-actor callback.
- Q3 connection callbacks must implement clear-active, download-size, and rejected-pure-snapshot behavior. Q2 MVD frame reads require max-client and dummy-slot policy.
- B23 still needs concrete QVM game, cgame, UI, primary, equipment, and combat host adapters. The committed executor alone does not supply those features.
