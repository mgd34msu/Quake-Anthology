# Required integration calls

These are concrete handoff obligations for the remaining graph nodes. A subsystem's source handoff does not complete its application integration. Build and runtime checks follow the complete baseline source gate in `dependencies.json`.

## Session and gameplay

- The application actor-release fanout must retire world, combat, inventory, pickups, and every attached provider's private state, including providers that do not own the actor. Q2 uses `qa_q2_actor_released`; duplicate owner notification is harmless. Generation reuse must not inherit private arsenal, monster, or mod state.
- The application physics router must dispatch `qa_q2_physics_read`, `qa_q2_physics_write`, and `qa_q2_physics_touch`. Q2 arsenal ticks run explicitly in the actor's owning source turn. Independent character and arsenal selection must not schedule the same actor twice.
- Q3 foreign input selections call `qa_q3_arsenal_step` through `qa_q3_controls`; native commands use the Q3 wrapper. Bind movement phase/effect callbacks with prediction authority so prediction cannot apply server damage.
- Bind `qa_combat_hooks.reaction` to one target-based dispatcher. `qa_combat_apply` already invokes it for native canonical damage. Select the target character/monster provider and invoke its public damage-reaction callback exactly once, including foreign attacks. An attacking arsenal must not invoke a second pain/death callback after damage returns.
- The Q3 target's `qa_q3_before_reaction` collects shared damage feedback through `qa_combat_hooks.before_reaction`. Its source `ClientEndFrame` calls `qa_q3_player_end_frame` once to publish the accumulated feedback.
- Route `qa_builtin_services.motion_changed` to the actor's selected movement owner after the provider commits shared body changes. Its typed teleport/launch/reset payload carries optional view angles, angular kick, and hold duration. Update movement continuation so a later movement phase cannot overwrite the committed teleport.
- Combat admission composes provider-owned effects. Bind the ordinary `invulnerable` hook to Q1/Q2 live timers; use request-aware `damage_allowed` for Team Arena bubbles, preserving the namespaced juiced bypass. Providers expire their own timers, never another owner's primary trait. Trait edits use `qa_combat_read_traits`, not effective reads. Shared inventory definitions use `qa_inventory_bind_definitions` while keeping counts in primary storage; save groups distinguish definitions-only metadata from guest storage leases.
- Reaction dispatch also fans out to attached item-provider reactions such as Q2 spheres, separately from the selected character's single pain/death callback. Advance `qa_q2_actor_tick` for foreign actors carrying Q2 item/power state without repeating the turn for Q2-owned actors.
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
- Use the shared `qa_media_library` with the selected VFS view, then construct `qa_cinematic` instances with distinct bus/target identities. Application restoration supplies a candidate audio engine and reserved bus IDs. The constructor suppresses provisional codec audio and restores saved raw queues only after decoder state is accepted.
- Prepare `qa_material_movies` once at global frame start and bind its resolver to world/model movie callbacks. All world/model submissions in that frame share the prepared result. Scene frame sequence numbers must advance on reset. Seat cinematics use `qa_cinematic_fullscreen`; campaign completion queues the saved seat transition through the application owner.
- Original source cinematic handle tables and campaign/UI admission remain application adapter work. P01 includes mono-RoQ loops, which now reset the PCM sample cursor on a new pass rather than inheriting the donor's discontinuity error.

## Network and compatibility

- B28 supplies actual gameplay, snapshots, prediction, and seat ownership to the committed codecs. B29 supplies download/browser/admin consumers, including mDNS port 5353. B30 supplies demo/MVD/GTV/TCP consumers.
- Re-admission on composition change retains connection identity and seats through `connections_restart`. Restore must update the unified receiver's actor registry and authenticated controlled-actor callback.
- Q3 connection callbacks must implement clear-active, download-size, and rejected-pure-snapshot behavior. Q2 MVD frame reads require max-client and dummy-slot policy.
- B23 still needs concrete QVM game, cgame, UI, primary, equipment, and combat host adapters. The committed executor alone does not supply those features.
