# Native Q3 objectives handoff, September 30 2026

Writer `/root/native_q3_objectives_owner` froze the current seven-file draft at root's new-session handoff request. No further code edits are authorized in this turn. This is implemented source awaiting independent review and integration, not accepted or executed behavior.

## Exact frozen packet

Manifest: `/tmp/qa-q3-objectives-draft-20260930.sha256`. All seven hashes matched on the latest check. The first six files are new. `objects.c` contains bounded existing-file changes owned by this writer.

```text
263f134c45dc4c3fe111d37847fd4b3d059b55acc1d5b8a673efa5c86d5c73ce  src/app/application/native_q3_objectives.c
8f527c0e680c48244f16e650e1900e4d7f33d9c0945fc5f0e3df5a2a4316954e  src/app/application/native_q3_objectives.h
739702aa83d330dd14324706e98d1f8accd97a342f423a0caf0f54755dea0771  src/gameplay/modes/q3_objective_source.c
b7620146e900acbe1efd74136d41f41b11c0cd67a763e759bba22033f3cd85f5  src/gameplay/modes/q3_objective_source.h
1114c5ecd05d2164837a4c8ba0f5f44abbebe0c7805a319aca3c3b717c6b64d3  src/gameplay/q3/source_objectives.c
c44987ce6931ecb926e9199292e3703c7ed9c31d16cc104352d13b7514c221c5  src/gameplay/q3/source_objectives.h
72f4e095f861547f63444812a034656af63061932ce27ca6b636490e3abc1813  src/gameplay/modes/objects.c
```

Independent peer `/root/q2_resume` is reading all seven files, real donor code, and actual sourcecore/map/dynamic/provider/modes consumers. They have not accepted the packet or supplied their final report at this handoff. They were told that the hashes are now frozen, that acceptance requires their own final hash check, and that sourcecore10 migration will require another review. Source reads and whitespace checks completed without reported whitespace defects. No execution validation occurred.

## Ownership and design

Native GAME owns the real 1024-slot physical source pool, fixed clients, raw seven-field sessions, TEAM scalar state, PS flag powerups, token `generic1`, and native PERS award counters. Source ownership is independent of selected score-mode enum and provider. Canonical actors may have a foreign owner; the adapter qualifies the actual full actor generation against the physical source binding and prepared/published TEAM item or native Obelisk continuation.

The app adapter uses the actual primary `QA_ROLE_ENTITIES` Q3 GAME provider and the published selected primary mode. It borrows the provider console lifetime and requalifies source/provider/mode identity after effect callbacks. The helper retains the same actor as mode metadata and effects projection. It never allocates another flag/cube actor, adds generic physics, or starts a second expiry timer. Objective capacity is the existing real actor capacity, not a new private source pool.

`q3_objective_source.c` borrows `mode_q3_objective_services` for one source operation. Source scalar state is read/written through `qa_q3_source_team_state_read/write`. Physical item scans query live source entity count, including changes caused by nested effects, rather than walking a copied objective list. Client iteration uses genuine fixed slots and native raw session teams. Shared selected-mode score, member, inventory, and general statistics are projections.

Root deliberately removed the unused `application_native_q3_objective_spawn` API and the unused `q3_obelisk_create` helper. There is no generic mode-produced source objective constructor routing claim. Existing generic `qa_modes_spawn_object` is not rewired by this packet. That future contract must preserve real source actor allocation and deferred authored admission.

## Implemented FLAG and CUBE paths

`application_native_q3_objective_admitted` adopts actual TEAM items at genuine source SpawnItem pending and Finish phases. Pending adoption reads true map-item backing and leaves FinishSpawningItem deferred. Completion refreshes mode metadata home/visibility from the real source body/item. Disabled source items receive no admission callback. Normal dynamic TEAM construction admits once at its actual source completion.

The dropped callback adopts the actual LaunchItem actor before ordinary source link completion. It reads prepared item/binding state and does not demand a ready network snapshot. Source item code owns motion and the real 30-second flag expiry. Cube admission occurs after true TossClientCube writes source spawnflags; persistent-item team restriction is not reused as cube TEAM.

TEAM initialization occurs at the genuine post-authored/findteams checkTeamItems phase. CTF initializes statuses through the source transient CS23 `0` then `00`; 1FCTF initializes neutral status. The helper preserves other true GAME scores, warmup, and neutral-Obelisk ownership while resetting TeamGameState fields.

Pickup implements original own-flag return, enemy/neutral flag acquisition, capture, assist windows and source f32 comparisons, native messages, raw PS flags, flag status/taken-time effects, source TEMP sounds, source gesture, visual awards, PERS capture/assist awards, native ranking calls, shared bound score effects, and canonical held-item projection. Zero-point source AddScore calls still produce source ScorePlum/ranking when source warmup permits. Harvester IT_TEAM pickup reads actual dropped spawnflags, changes true native PS generic1 if opposing, and retires the actual cube. Overload TEAM item pickup retires the actual item.

Personal teleporter/portal flag drop uses neutral/red/blue priority, genuine source Drop_Item, then actual PS clear. It does not return/reset the flag. Actual source flags-cleared callbacks project shared inventory/member changes only after the native PS write or death memset. If ReturnFlag precedes death PS clear, reset retains a pending previous holder until that actual clear completes. Expiry and NODROP return differ in source print effects. Base reset uses genuine source RespawnItem. Current TS publication qualification rejects RespawnItem on a still-pending source item; no fake Finish is supplied.

## Implemented genuine Obelisk path

`source_objectives.c` implements the authored missionpack ET_TEAM model constructor plus its genuine second G_Spawn trigger/combat actor. Red/blue eligibility and neutral 1F/Harvester eligibility match source. Overload uses canonical shared combat health/takedamage/no-knockback; Harvester uses the real trigger contents. The native continuation contains model reference, think kind, and next-think time. Raw TEAM is solely common source spawnflags. Neutral Harvester continuation correctly has no activator/model reference.

Floor placement retains source `s.origin` separately from true body/trajectory origin. A clear trace retains authored origin plus one in `s.origin` while placing the body at trace end; startsolid restores original source origin; suspended placement retains original origin. Ground source numbers, raw spawnflags assignment, trigger link/readiness, model binding, model link/readiness, and admission order follow the real source constructor.

Per-actor combat admission implements CheckObeliskAttack before chosen generic mode damage policy. It uses actual native sessions, same-team veto, source attacked-time throttles, and source TEMP sounds. Source think implements f32 RunThink eligibility, regen, respawn, real modelindex2/frame updates, and native events. Pain updates real model ratio/frame/event then source score. Die uses two app effect stages: source opposing team score/gesture/ranks first, actual physical damageability/respawn deadline/model event second, attacker score/award/PERS capture third, and attacked-time reset last.

App Harvester delivery consumes true generic1, emits source message/team score/gesture, awards wrapped score and native PERS captures, clears true tokens, runs native ranking, and emits genuine capture sound. It does not run a generic mode Obelisk touch or add another timer.

`application_native_q3_objective_view` is a pure observer. For Obelisks it derives phase from the true continuation and frame/health fraction/visibility from the actual ET_TEAM model wire. It does not mirror physical state in mode metadata or replay lifecycle callbacks. A neutral trigger with no model reference is not invented into a visible generic model. Actual item visibility is read from genuine prepared/published item state.

`objects.c` delegates source object and leased objective reads to this pure getter. It skips generic sync/physics/link, touch, drop, and frame/timer behavior for source-owned metadata. Client owner's `flags.c` guards generic source-owned flag reset/touch. Client owner's `arena.c` guards source-owned Obelisk generic admission/touch/reaction. Those files are outside this seven-file manifest.

## Current external integration verified by source read

- `providers.c` binds actual TEAM init, admitted/pickup/drop/dropped/expired/NODROP, completed PS-clear, and all five Obelisk settings/admitted/touch/die/pain hooks.
- `app/application/match.c` binds `q3_source_object` proof and `q3_source_object_view` pure read hooks; their public modes hook declarations exist.
- Round owner proved initial and travel publication install candidate modes/primary-mode readiness before real source reset and authored spawn. Detached mode construction does not spawn these native objectives. Restore bypasses constructor/init callbacks.
- Map owner routes missionpack red/blue/neutral authored POINT model through `qa_q3_source_obelisk_spawn`; it does not publish a fake generic ready actor. Prepared/finished item callbacks are TEAM-only.
- Dynamic owner supplies true item spawn/read/adopt/respawn/drop APIs, genuine dropped prelink callback, and completed death-PS-clear callback. A valid physical nonitem returns `QA_ERROR_NOT_FOUND` from item_spawn_read, so full physical scans skip TEMP/POINT/Obelisk rows.
- Sourceowner's `source_effects.c` has actual native `qa_q3_client_award`; it updates genuine player PERS counters and active copied FOLLOW PERS. Our native capture/assist producers use that API, not mode q3 counter authority.
- Source continuation/private codec has Obelisk kind and model/think/deadline without duplicated TEAM. `source_wire.c` now explicitly preserves OBELISK raw GENERAL wire. `view.c` has an Obelisk kind branch.
- Genuine source final reconnect in `q3/save.c` calls pure `q3_obelisk_reconnect` after the restored canonical combat owner exists. It only reattaches admission and does not run a constructor, think, score, or publication callback. App objectives reconnect is pure physical-owner/base-identity qualification.

These are live source-read observations, not peer acceptance or runtime proof. Other singlewriters' files are not frozen by this seven-file manifest.

## Remaining holds and next sourcecore10 work

1. Root common `app/application/services.c` still lacks a call to `qa_q3_source_obelisk_reaction` on the latest handoff source search. It must route the actual physical native GAME source target once at the common reaction boundary, independent of selected character/effects provider. A handled native Obelisk must bypass generic mode-object reaction; do not double-dispatch through Q3 before-reaction too.
2. True per-native-client TEAM pers state is not implemented in this packet. Current pickup/capture helper still reads/writes four mode-stat q3 f32 timestamps. General mode captures/assists/recoveries are selected-rule statistics, with `mode_stat_add` policy. Native PERS capture/assist counts have already moved to true GAME. Do not claim complete native TEAM telemetry ownership yet.
3. Root assigned native fragBonuses/checkHurtCarrier to `/root/bot_orders_owner`. Their new helper pair is still absent, investigation/API agreement only. Generic selected-mode flags.c derives selected teams/bases and cannot provide true native TEAM mechanics under foreign selected rules. Actual death/hurt integration remains required.
4. Agreed sourcecore10 type is `qa_q3_source_player_team_state`, stored as `qa_q3_native_client.team`. Fields are signed32 `captures`, `base_defense`, `carrier_defense`, `flag_recovery`, `frag_carrier`, `assists`; finite f32 `last_hurt_carrier_ms`, `last_returned_flag_ms`, `flag_since_ms`, `last_fragged_carrier_ms`. Native constructor clears them; ordinary respawn preserves them. Counts wrap signed32; opposite-team capture writes source last-hurt value -5; assist comparisons include initial zero timestamps and f32 rounding.
5. Agreed true APIs are `qa_q3_client_team_state_read(constgame, uint32_t source_slot, &state, error)` and `qa_q3_client_team_state_write(game, uint32_t source_slot, &state, error)`, with actor calls resolved through `qa_q3_native_client_slot`. They must land in sourcecore10 after core9 freeze. Add stateless app/helper services for native TEAM-pers read/write, then migrate this packet's capture/recovery/assist counters and all four timestamp reads/writes. Read current source tuple at each original mutation stage after intervening score callbacks, avoiding stale whole-state overwrites. Preserve legitimate chosen-mode statistics as projections.
6. Native source AddScore and actual death/hurt ordering need joint review with the new frag helper. This packet's score service currently checks true GAME warmup, emits genuine source ScorePlum, mutates the real selected bound shared score with `qa_modes_add_score`, and runs actual native `application_native_q3_rank`. The helper must not substitute chosen-mode warmup, native-client population, or enum for actual GAME ownership. Parent must decide any distinct source_score contract needed for composition.
7. Source post-spawn checkTeamItems warning producers were absent at the last review. Map owner requested root authorization to add them immediately after TEAM init and before registeredItems save: genuine registered-items bits for CTF/1F flags and actual live classnames for Overload/Harvester Obelisks. Do not add an app duplicate warning pass.
8. Actual authored Obelisk ET_TEAM model frontend qualification still needs review. Current `q3/view.c` entity_read rejects map POINT rows because its no-native-entry branch accepts only authored mover kinds. The new Obelisk-kind branch covers the separate GENERAL trigger, not its ET_TEAM model. True source model wire is present, and the pure objective getter reads it, but this does not prove built-in/native CGAME rendering and asset binding. Follow the actual frontend route and implement the genuine model projection if it uses this accessor.
9. Root owns registration of the new TUs after whole-packet peer acceptance. Independent review must close exact defects against the final hashes; sourcecore10 changes need refreshed hashes and review. The source-only validation gate remains in effect.

## Behavioral reference files

Primary donor: `../quake-typescript/src/content/q3/team-arena/team.ts`, read in full. Exact load/checkTeamItems phase: `../quake-typescript/src/app/bootstrap/simulation/q3/runtime.ts` around 498 and 539-565. Actual item-lifecycle published-item predicate and deferred FinishSpawningItem ordering were read/coordinated with map owner. Source RunThink f32 behavior is in Q3 game/entities.ts. Original source counterpart is `../qsrc/quake-iii-arena/code/game/g_team.c`, including complete Obelisk bodies. These are local development references, not repository user-facing links.

