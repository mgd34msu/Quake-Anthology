# Native Q3 player effects source packet

This packet owns `src/presentation/q3_native/player_fx.h`, `player_fx.c`, and `player_fx_save.c`. It is a source-only implementation packet for B16/B32/B34. The original B00-B34/BASELINE SOURCE GATE REMAINS CLOSED to configure/build/compiler/test/script/parser/generator/syntax/runtime/performance execution. This packet does not claim source completion, an independent review, Jev judgment, compilation, rendering, or gameplay result.

The remaining-scope handoff was read before implementation. No configure, build, compiler, test, project executable, project script, parser, generator, syntax check, runtime check, performance run, child agent, or Git command was used in this lane. Verification was a complete manual source read against the donors and actual C interfaces.

## Donors and coverage

The complete player presentation portion of `/home/buzzkill/Projects/quake-typescript/src/content/q3/presentation/players.ts` was read, including body composition, shadow/splash, flag, powerup, token, kamikaze, breath/dust, invulnerability, and medkit methods. The SDK donor was `/home/buzzkill/Projects/qsrc/quake-iii-arena/code/cgame/cg_players.c`, especially lines 1528-1607, 1641-1981, 1985-2175, and 2232-2577. Numeric identities came from that SDK's `code/game/bg_public.h` powerup, animation, game-type, and persistent-field enums.

| Behavior | Implementation and actual dependency |
|---|---|
| Connection/talk/award/friend sprites | `sprites` preserves donor priority and raw local PS team/clientNum decisions. Real registered shaders go to `qa_q3_presentation_entity`. |
| Shadow | `shadow` requests the bound world box trace, uses its retained source/contact normal and plane, submits a genuine temporary mark through `q3n_marks_impact`, and patches the real split-body refs with RF_SHADOW_PLANE for mode 3. |
| Splash | `splash` uses world-only liquid/solid queries and liquid surface trace, then submits the authored four-vertex wake with real shader handle. |
| Body effects and replacement composition | `body_powerups` preserves invisibility exclusivity, base/quad/regen/battlesuit pass order and time gate. `body_submit` receives the actual observed actor, authored ref, part, and donor base identity. Final customShader mutation stays on the authored ref, including later torso attachment inheritance. |
| Flags | `flag` uses real torso/pole tag interpolation, flag skin/model handles, velocity-derived swing, original stand/run animation records, source frame time, and private flag lerp/yaw continuation. Older models use donor trailing flags. |
| Powerup light/audio/haste | `powerups` preserves final CG_Player ordering. Lights consume the shared real `q3n_events_rand` stream; flight uses native looping audio; haste uses actual smoke/local-entity allocator and source trail cadence. |
| Breath/dust | `breath` uses actual head tag origin/axes and world-only contents. Its clock remains in the exact raw-number client-info row. `dust` uses the source position base, entity-inclusive `q3n_events_trace`, actual SURF_DUST, leg animation and dust cadence. Both use the shared smoke allocator and its RNG. |
| Harvester tokens | `tokens` preserves token cap, incremental trail insertion, spacing, team model, bob, and authored axes. The trail belongs to the full-actor centity, rather than a global source-number array. |
| Kamikaze | `kamikaze` implements live three-skull and dead one-skull paths with genuine models, trail flip, source-clock orbit formulas, authored torso origin/lighting/shadow flags. |
| Persistent mission powerups | `mission` submits actual guard/scout/doubler/ammo-regen model handles from cloned authored torso refs. |
| Invulnerability and medkit | `mission` uses retained `ClientInfo.dynamic`, its real qualified write and codec. No durable duplicate clock exists in player_fx. Shell growth/shrink, source medkit rise and original fade byte arithmetic remain intact. |
| Pure continuation codec | `q3n_player_fx_codec` records every flag lerp field, yaw/swing, skull count and all ten positions. It validates finite geometry and legal selected flag animations. The native aggregate supplies schema, actor admission, candidate restore, and atomic publication. |

## Ownership and integration contract

The actual C body/pose/attachment, client-info/media registry, events/RNG/local/marks, frame/entity, presentation backend, and application source observer interfaces were read before the new implementation. The packet contains no guessed asset path and no registration/I/O during a player frame. S and PS remain borrowed observations. Authored references and private presentation continuation are the only mutable actor presentation fields.

`q3n_player_fx_submit` is called once after `q3n_player_body_build` advances the real split-body pose. It owns sprite/shadow/splash/token/body effects/mission effects/weapon/final powerup submission ordering, including genuine lower/upper/head missing-model early returns. The core must not separately submit those body parts or repeat the weapon callback. The body builder's camera-mode and invalid-client early return yields zero parts and is respected.

The final `q3n_player_fx_backend` requires bound-world trace/contents callbacks, actual body hiding and composition submission callbacks, and the real player-weapon callback. Every callback or renderer submission is followed by a source-cut requalification and a reobservation of the exact physical entity/full actor/raw clientNum plus unchanged client CS/media revisions. Breath additionally qualifies its separately indexed raw-number client-info revision. Whole-frame ownership remains with the native core; the child temporarily leases the real event owner while it allocates effects and calls the backend.

The render-review owner exclusively added `q3n_player_fx_state player_fx` to the real `q3n_entity`. Actor generation reuse resets that private continuation through its actual centity reset. Client-info remains the owner of donor shared client clocks; corpse/victory bodies may legitimately borrow their original raw clientNum's model and clocks.

The core owner was sent the exact final header and requested to wire the callbacks, both new `.c` files into its actual build source list, and `q3n_player_fx_codec` into the real native entity aggregate. Those shared core/build integrations are owned elsewhere and require whole-source peer review together with this packet before completion can be claimed.

## Bounded source differences and review limits

The SDK breath source indexes `clientinfo[currentState.number]`; this packet keeps that genuine row for valid player numbers and skips an out-of-client body-queue number rather than accessing outside the table. It retains raw clientNum for all client media and mission powerups. Harvester skull storage is safely qualified by the full actor instead of writing outside the SDK's global client-indexed array for a non-client body. Negative native Harvester token counts are rejected at this presentation boundary.

`CG_LightVerts`/the TS `lightVerts` utility has no player composition caller in the supplied donors and is not part of the rendering path introduced here. This packet does not invent a consumer for it.

Independent source peer review found and prompted two narrow donor corrections in `player_fx.c`: arbitrary integer `cg_shadows` values retain the donor's 0/1/3 branch behavior without an invented range clamp; freshly authored model refs retain zero RGBA/axes, with explicit white RGBA confined to the donor sprite path. The repaired three-file cut is frozen for peer reread of the actual paths and callers. Peer acceptance of that repaired cut and Jev acceptance remain pending. All executable validation remains deferred under the closed original source gate; no manifest is required.
