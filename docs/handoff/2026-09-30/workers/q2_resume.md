# q2_resume handoff, September 30 2026

Root requested a new-session handoff and no new code edits. This agent has stopped implementation. No compiler, build, game, tests, project scripts, parsers, generators, or benchmarks were run.  use in the latest task was read-only whitespace verification. Root owns integration,  writes, CMake, and.

## Current review disposition

The current task is the independent TEAM/Obelisk objective draft7 source review from `native_q3_objectives_owner`. The complete seven packet files were read. Surrounding donor and caller review was substantial but incomplete at the handoff boundary. The packet is **not accepted**. Do not convert this handoff into whole-graph acceptance.


```text
263f134c45dc4c3fe111d37847fd4b3d059b55acc1d5b8a673efa5c86d5c73ce  src/app/application/native_q3_objectives.c
8f527c0e680c48244f16e650e1900e4d7f33d9c0945fc5f0e3df5a2a4316954e  src/app/application/native_q3_objectives.h
739702aa83d330dd14324706e98d1f8accd97a342f423a0caf0f54755dea0771  src/gameplay/modes/q3_objective_source.c
b7620146e900acbe1efd74136d41f41b11c0cd67a763e759bba22033f3cd85f5  src/gameplay/modes/q3_objective_source.h
1114c5ecd05d2164837a4c8ba0f5f44abbebe0c7805a319aca3c3b717c6b64d3  src/gameplay/q3/source_objectives.c
c44987ce6931ecb926e9199292e3703c7ed9c31d16cc104352d13b7514c221c5  src/gameplay/q3/source_objectives.h
72f4e095f861547f63444812a034656af63061932ce27ca6b636490e3abc1813  src/gameplay/modes/objects.c
```

## Source observations and confirmed open behavior

1. Native GAME player TEAM counters and f32 times still use mode-member statistics in the frozen producer. `q3_objective_source.c` capture/recovery/pickup/assist paths read and mutate `stats.captures`, `stats.assists`, `stats.recoveries`, `q3_last_hurt_carrier_ms`, `q3_last_returned_ms`, `q3_flag_since_ms`, and `q3_last_fragged_carrier_ms`. The donor `content/q3/team-arena/team.ts` touchOurFlag/touchEnemyFlag and frag/hurt callbacks use actual native `client.pers.teamState`. This is more than a telemetry distinction: `mode_stat_add` in `modes/core.c:54` can suppress counters under a selected LMCTF countdown/finished mode, while native source GAME TEAM mechanics should retain their own counters; assist eligibility currently reads mode-owned times. Writer and root already acknowledge this as sourcecore10 work. Six signed32 counters and four f32 times must move to actual `qa_q3_native_client.team`; read/write APIs must preserve raw physical slot/full generation and reread actual state after intervening callbacks. General chosen-mode statistics can remain effects projections. True PERS capture/assist awards are already produced by `qa_q3_client_award` in `q3/source_effects.c:108`; that does not close the native pers.teamState hold.

2. The common reaction binding is absent on the actual latest source read. Searching all `src` for `qa_q3_source_obelisk_reaction` returned only its definition in `q3/source_objectives.c:304`. No genuine services.c caller invokes it yet. Therefore the implemented source pain/death producer cannot be claimed integrated. Root must dispatch once by actual primary native GAME target identity, independently of selected character/effects provider, and avoid double generic mode reaction.

3. Authored Obelisk model presentation is a confirmed current source-path gap. `q3/map/spawn.c:584` retains the authored model as `QA_Q3_MAP_POINT`; `q3/view.c:7` rejects a map row without a native q3_actor unless it is an authored mover. Actual `application/visuals.c:54` calls that accessor and propagates its error; `frontend/visuals.c:295` skips only NOT_FOUND and otherwise fails the submission. Separately, source-owned trigger adoption does not populate `mode_object.value.model`, so the mode visual route does not provide the authored model resource either. Donor `team.ts:608` and SDK `g_team.c` SP_team_*obelisk retain a linked ET_TEAM model plus a distinct GENERAL trigger. The raw model wire state and pure objective getter are present, but neither closes the built-in frontend accessor/resource path. This was already raised by the writer as a dependency hold; the actual visual caller route is now independently confirmed.

4. `application_native_q3_objectives_reconnect` is currently defined/declarared but has no caller anywhere in `src`. Its source-owner/base identity qualification is therefore not integrated. The genuine final native reconnect in `q3/save.c:650` does call pure `q3_obelisk_reconnect`, which restores combat admission without constructor/think/event replay. These are different qualifications. Complete app/frontend aggregate restore ordering remains unreviewed here and must be closed before claiming restored objective consistency.

5. `checkTeamItems` warnings remain absent on the latest targeted reads. Native `application_native_q3_source_team_items` currently only invokes TEAM initialization. Donor `app/bootstrap/simulation/q3/runtime.ts:498` also checks genuine registered flag bits and true live Obelisk classnames. Native post-map phase `q3/map/spawn.c:707` invokes the hook before registered-items serialization, so that is the real producer boundary to complete. Do not supply a duplicate later app warning pass.

6. Source fragBonuses/checkHurtCarrier producers are still assigned separately to `bot_orders_owner`; they were not reviewed or accepted as implemented by this packet. Their actual source GAME state, native raw sess teams, base/flag identity, source health and AddScore/death/hurt ordering remain joint dependency work.

No further confirmed changed-hunk defect was established in the seven-file read before handoff. This statement does not accept unreviewed lifecycle or source helper graphs.

## What was source-read

Complete packet: app objectives C/H, modes source objectives C/H, q3 source objectives C/H, and all 985 lines of modes objects.c. Donor reads covered TS TEAM flags/capture/assist and all Obelisk spawn/regen/respawn/pain/death/touch/attack bodies, TS runtime checkTeamItems phase, SDK g_team.c Team_TouchOurFlag and complete Obelisk callbacks/constructors. Earlier partial donor portions were reread where output had truncated. The whole TS team file was not independently re-read in one contiguous final pass; non-objective location/team-status portions are not a full acceptance claim.

Related source reads covered actual providers hook installation, app native source cvar-to-rules qualification, actual authored Obelisk map spawn, item spawn-read absence classification, actual source touch routing, source_effects awards/scoreplum/gesture/team-state, source_wire raw entity projection, native save reconnect, modes core score/member/object lookup and release behavior, modes checkpoint/source-owned physics/timer rejection and codec fields, application visuals and actual frontend submission error handling. These related owners are outside the frozen seven-file manifest.

The source wire preservation hold is now resolved on disk: `q3/source_wire.c:928` explicitly includes Q3_ACTOR_OBELISK in the raw GENERAL-preserving branch. It is not frozen by objective draft7 and needs the source owner's own packet disposition.

Remaining review before final acceptance: actual dynamic item/LaunchItem callback graph in full; sourcecore/checkpoint constructor/import/finalization graph in full; real actor release/store replacement under nested callbacks for adopt, dropped base/child scans, flags_cleared and inventory publication; actual primary-mode prepublication/restore caller sequencing; selected shared score versus distinct source AddScore ownership under foreign composition; genuine frontend model/resource consumer graph. Generic source-owned mode sync/touch/drop/frame paths skip duplicate physics/allocation/timers in the read packet, and the native authored Obelisk constructor creates its true second physical trigger. Those bounded observations are not whole-graph acceptance.

## Completed original-native Q2 adapter and pending crouch getter

Previously implemented and independently source-accepted new adapter2 is `/tmp/qa-q2-guest-control-20260930.sha256`:

```text
3daf4d1baf1f2357a9ffb4a3aad184e426d00858bedef1835c23952cdbb5883e  src/app/application/guest_q2_control.c
7a17887322d09dc47aff57d4254b0c6a20ddc9694eba49fc810beb466dfd8ae9  src/app/application/guest_q2_control.h
```

Movement source peer accepted full adapter2 after donor/public ABI/trace/body/lifetime reads and two hash checks. Native lifecycle caller3 `/tmp/qa-native-q2-control-lifecycle-caller3-20260930.sha256` was independently source-accepted by this agent: real prepare, observer selection, post-Init/pre-SpawnEntities activation, post-disconnect retirement suspend, pre-restore suspend, initialized-and-map_ready-only final activation, post-Shutdown close before host destruction, retained ownership on failed removal, and constructor-failure final cleanup. Five hashes last checked together matched. Original new files were released to root; do not edit without narrow reassignment. Movement capability/output/full input subscriptions remain separately scoped.

Movement still needs **unimplemented** pure `application_q2_control_crouched(engine, actor, bool *out, error)` for blocked stand-up camera behavior. The actual raw public API is `qa_native_host_q2_player_state(host, physical slot 1..256, &owned_buffer, error)`. Classic Q2 PM_FLAGS is byte at offset 16; rerelease API2023 PM_FLAGS is uint16 at offset 28; PMF_DUCKED is bit 1 in both. A correct helper must reuse the accepted adapter's private `client_for`/`client_live` full actor, connected+begun physical client, actual host/instance and generation qualifications and retain `engine.calls` over the read, then reacquire after any fallible observation. Raw PS alone is insufficient generation proof. Do not reconstruct this value from selected movement controls. Camera condition is donor `!has_stance || requested_crouch || !actual_source_crouched`. Root was notified that narrow ownership/resumed implementation is required. No helper was added, and no new public API claim is accepted.

## Other completed source packets

- Q2 carry/disconnect4 accepted and root committed e0b9792; released. Existing userinfo1/report checkpoint 8cf4863 and Medic cue bb43 retained. Broader rerelease dead-carry reset, full flags/armor and wider disconnect parity remain open.
- Q2 actual GAME RNG2 accepted and released; bots use existing q2_random, not an alternate seed.
- Shared inventory count-read2 `/tmp/qa-inventory-count-read-20260930.sha256` independently accepted by movement source peer and released. Genuine live no-store/missing item returns true zero; stale generation/owner/current-store/revision/callback failures propagate; success-only output and balanced owner lifetime.
- Q1 weapon/think8 and source-input/wetsuit4 accepted and released; later source-client reservoir9 accepted within producer scope. Pure prepared membership reader2 `/tmp/qa-q1-prepared-client-read-20260930.sha256` independently accepted: pending continuation is permitted only for pure full-generation prepared observation; runtime getter and actual command qualifier remain strict. Dual source/canonical movement graph is separate.
- Rankings core7 `/tmp/qa-application-rankings-core-resume-20260930.sha256`, rankings.c c97c2e2884cabe87808bf3ded72961efbc9ab06755dfd1dd380323a7e79b3f79, bounded delta accepted. Actual native client drain graph stayed separate; clients owner now reports frozen `/tmp/qa-native-q3-clients-owned-current5-20260930.sha256` (20 whole files), which this agent has not independently read/reviewed. Do not infer its acceptance from the rankings disposition.

Prior source acceptances were communicated to root and relevant owners. They are bounded source dispositions, not compilation, runtime or complete project parity.

