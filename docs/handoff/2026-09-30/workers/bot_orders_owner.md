# bot_orders_owner handoff, 2026-09-30

Root requested an immediate freeze for a complete new-session handoff. No repository code was edited after the snapshot recorded below. No  compiler, configure/build, project executable, script, parser, diagnostic execution or test was run. Only source reads, owned source edits in the earlier chat packet, hashing, whitespace checks, and this explicitly requested /tmp handoff occurred.

## Ownership and current state

- Agent: /root/bot_orders_owner. Root owns CMake/publication and integration scheduling.
- New assigned lane: exclusively NEW src/gameplay/q3/source_team_combat.c and source_team_combat.h. BOTH ARE ABSENT, verified by ls at the freeze boundary. The lane is investigation and API coordination only; no helper implementation or header was created.
- q3_source_resume is the single writer of existing native types, internal.h, native client/reset state and codec. Its sourcecore9 was freezing; telemetry additions belong to sourcecore10 only after root releases9.
- native_q3_objectives_owner owns existing objective helpers/adapters and will migrate native pickup/capture telemetry only after real sourcecore10 backing exists. Its draft7 was being independently reviewed and was held stable.
- q3_dynamic_wire_owner owns death.c and the genuine native death-stage consumer. Root owns src/app/application/services.c harm/death/mode integration.
- No descendants, shared-file edits,  operations, CMake edits, or executable checks are authorized for this worker. Global source baseline gate remains closed.

## Source chat2, already complete and independently source accepted

Owned files exist and remain frozen:

| Path | SHA256 |
| --- | --- |
| src/bots/ai/source_chat.c | 33579a0414b3c6ecc52a3c83cd60cf0aa7b9c3b3d83f296954832729aee0407e |
| src/bots/ai/source_chat.h | 240f166dbf7bce8eadc22a2bb0fdea73de131d9b5629759a2ae5825b7bc7f34f |
| /tmp/qa-bot-source-chat-20260930.sha256 | f3346259b7fc79228916d9a58aa163a916a3e58b044d888353ebcd3aa02d376b |
| /tmp/qa-bot-source-chat-source-20260930.md | e30212f2da386f4e5e87b5374c0296f315569ff5633973ed605382ab5f5d4625 |

bots_source_peer independently read complete C522/H36, the entire 391-line TypeScript ai-chat.ts, and genuine helper/service dependencies. It accepted the repaired pair, checked hashes twice, and found no whitespace diagnostics. Root was sent the exact acceptance. This handoff does not independently verify a or publication.

The pair implements all eleven runtime chat functions, actual ranking/name/map/visibility/position helpers, ignored-but-genuine CHAT_CPM characteristic lookup before ChatTime=2, and the genuine runtime BotChatTest diagnostic. BotChatTest was never executed. Review found initial template counts can invoke the real Botlib bot_testichat diagnostic and retire an actor. All three count sites now retain the count locally and check the full actor immediately before additional query/RNG/effect/state continuation. Live branch/RNG/destination order is unchanged.

State/API is stable: perbot source_chat has chat_to, last_frame_health, last_hit_count, enter_game_chat; population source_chat has six independent maxclients cells. Existing last_health is BotFindEnemy's distinct field; last_chat_time and all stand/respawn/enter times retain parent ownership. bots_resume embedded/serialized/reset the four fields and six caches and added genuine enter/Stand/death/level/random/fight/console duration callers. HitTalking calls actual ChatTime twice. Its broader caller/private codec acceptance remains separate from the accepted owned pair. Full combat movement remains open. Actual native qa_bot_source_player_state producer must preserve fixed player-pointer/PM/score/hurt semantics; no fake has_player=true or live-only substitute is allowed. Source self curPs is the retained same pre-console qa_bot_player.source_state sample.

## Complete donor reads for the NEW TEAM lane

Behavior oracle: /home/buzzkill/Projects/quake-typescript.

- src/content/q3/team-arena/team.ts: complete imports/interfaces/TeamRuntime prefix and both checkHurtCarrier192..199 and fragBonuses201..268, plus helpers otherTeam/teamName/onSameTeam/award/clientOf.
- src/content/q3/base/game/state.ts: complete PlayerTeamState fields37..52.
- src/content/q3/base/game/combat.ts: actual checkHurtCarrier caller after client hurt/PS damage bookkeeping, before death NO_KNOCKBACK assignment218..228.
- src/content/q3/base/game/death.ts: genuine addScore119..129 and native player death basic score/reward→teamFragBonuses→suicide return/toss stage280..315.
- src/content/q3/base/game/utilities.ts: actual findEntity121..130, physical source slot scan with case-insensitive ASCII classname comparison and inuse.
- src/app/bootstrap/simulation/q3/runtime.ts: real createTeam315..331 and inPVS392..395; addScore delegates death owner; PVS means actual point-leaf cluster visibility AND areasConnected.
- src/core/math.ts: actual sub3/dot3/length3 operation order, with separate binary32 subtraction/products/adds/sqrt result.
- src/content/q3/base/game/save-values.ts: TeamValues285..314 native persistent values (six counters/four timestamps plus separate state/location).

Original SDK: /home/buzzkill/Projects/qsrc/quake-iii-arena/code/game.

- g_local.h199..215 complete playerTeamState_t declaration: state/location, six int counters, four float timestamps.
- g_team.c280..527 complete Team_FragBonuses and Team_CheckHurtCarrier, including both danger branches, duplicate v1 write, second branch's v2 base distance and all source effects.
- g_team.h constants/prototypes: base/missionpack bonus values,1000 radii,8000 danger timeout,10000 assist windows.

The initial attempted SDK path team.c was absent; the correct donor is g_team.c and was read in full for these functions.

## Agreed sourcecore10 telemetry contract, NOT PRESENT at snapshot

q3_source_resume agreed exact type/member and API names, and native_q3_objectives_owner was notified:

```c
typedef struct qa_q3_source_player_team_state {
    int32_t captures, base_defense, carrier_defense;
    int32_t flag_recovery, frag_carrier, assists;
    float last_hurt_carrier_ms, last_returned_flag_ms;
    float flag_since_ms, last_fragged_carrier_ms;
} qa_q3_source_player_team_state;
/* qa_q3_native_client.team owns this actual pers.teamState telemetry. */
bool qa_q3_client_team_state_read(const qa_q3_game *, uint32_t source_slot,
                                qa_q3_source_player_team_state *, qa_error *);
bool qa_q3_client_team_state_write(qa_q3_game *, uint32_t source_slot,
                                 const qa_q3_source_player_team_state *, qa_error *);
```

These are coordinated future declarations, not existing compiled/public APIs. Freeze-boundary rg found no type, fields, read/write APIs or TEAM combat helper/public hooks. Existing native_client.team_state and team_location retain actual state/location ownership and must not be duplicated. Actor wrappers must use qa_q3_native_client_slot to obtain the actual physical slot.

Counters permit every signed32 bit pattern and increments wrap, including overflow. Do not impose nonnegative validators. All four times are finite binary32 and initially0, including legitimate negative -5 lastHurtCarrier after opposite-team capture. Do not coerce to uint64/shared clock. Real source time is game->now_ms/qa_q3_source_clock converted to f32 at assignment; danger uses f32(f32(time)-lastHurtCarrier)<8000 and nonzero timestamp without a lower-bound test. Native ClientConnect/new fixed memory clears whole pers; ordinary ClientSpawn preserves pers telemetry while source team_state changes BEGIN/ACTIVE. Corewriter owns exact codec/checkpoint/reset implementation and version progression.

## Exact algorithms and effect ordering to implement

checkHurtCarrier returns immediately if either actual entity.client pointer is null. It chooses blue flag8 when target source session team1/red, otherwise red flag7. If that actual target PS powerup is nonzero OR actual generic1 is nonzero, and the two source sessions differ, assign attacker native team.last_hurt_carrier_ms=f32(source time). The helper itself has no game-type restriction. Actual damage caller alone gates source GT_CTF4 or product missionpack plus GT_1FCTF5. It intentionally does not check the neutral flag in1FCTF. This belongs after actual source damage/lastHurtClient/lastHurtMod bookkeeping, not a direct shortcut from shared transport/lastKilled cache.

fragBonuses initial refusal: target.client null, attacker null/client null, same actual entity, or onSameTeam(source gameType>=3 AND native session equality). Use true source sessions independent of selected mode/combat teams. otherTeam maps red1↔blue2 and leaves free0/spectator3 unchanged. Do not normalize them or add a helper-wide CTF/gameType filter. Source native death calls this even outside CTF.

victim-team flag=red7 for red, otherwise blue8. Enemy flag=neutral9 whenever source gameType5; otherwise blue8 for victim red, else red7. tokens=actual victim.ps.generic1 only for missionpack/gameType7 Harvester; otherwise0. Product bonus constants come from actual native product, not individual selected character.

Carrier branch if enemy flag or tokens nonzero:

1. Determine hasFlag from actual powerup (flag wins over tokens).
2. Set killer last_fragged_carrier_ms=f32(time) BEFORE AddScore.
3. Real AddScore(attacker,target.r.currentOrigin, hasFlag ? MP20:base2 : imul(imul(MP20:base2,tokens),tokens)). Each integer multiplication and increment wraps signed32; negative tokens remain nonzero.
4. Increment real native frag_carrier after AddScore (even if AddScore returns normally during warmup).
5. Broadcast exact PrintMsg text using current native killer.netname, source teamName RED/BLUE/SPECTATOR/FREE, and flag/skull carrier. PrintMsg stops at embedded NUL, replaces quotes with apostrophes, and uses real source server_command(-1, `print "..."`). No fake diagnostic replacement.
6. Iterate actual physical0..maxClients-1 in source order; for each inuse entity require real client pointer and matching opposing source session, then clear native last_hurt_carrier_ms=0. Return. A malformed inuse non-client row is a source error, not an assumed player.

Danger branch next: victim last_hurt_carrier_ms!=0 and f32(f32(now)-time)<8000. The SDK second danger branch removes the first's attacker flag restriction without a game-type/token condition; TS consolidates them. AddScore MP5/base2→increment killer carrier_defense→clear victim hurt timestamp→increment true PS PERS_DEFEND_COUNT11→source award visual EF_AWARD_DEFEND0x10000 with existing six-bit award clear mask0x38848 and integer rewardTime=now+2000 (signed wrap). Return. Do not add a flag/gametype restriction or reject future/negative timestamps.

Base/carrier branch:

- MPgameType6 Obelisk chooses killer native red/blue classname team_redobelisk/team_blueobelisk, otherwise returns for non-red/blue.
- MPgameType7 Harvester chooses team_neutralobelisk with no red/blue refusal here.
- Otherwise require killer native red/blue, choose team_CTF_redflag/team_CTF_blueflag, then scan physical0..maxClients-1 for first inuse real client with victim-team flag powerup nonzero. No carrier-team restriction. Missing inuse client pointer is error.
- Find first inuse classname match using physical0..source_count-1, true source binding.classname with ASCII case folding, skip genuine DROPPED_ITEM source items. Do not rely on selected objectives/list ordering or exact string-ID equality. If no base, return even if a carrier was found.
- Read real target/attacker/base r.currentOrigin, honoring existing q3_source_current_origin active command scope. Compute source f32 vector subtraction and each squared product/add, then f32(sqrt(double squared)). Use strict distance<1000. Actual PVS is Q3 pointLeaf→cluster visibility→area connectivity with source short circuit, not view cone/line trace/generic selected visible callback.
- If either target near base+PVS(base,target) or attacker near base+PVS(base,attacker), and current killer/victim source sessions differ: AddScore MP10/base1→increment native base_defense→PERS_DEFEND_COUNT increment→award visual/reward→return.
- If carrier exists and is not attacker, source intentionally writes v1 twice: only attacker-minus-carrier distance is used for first distance condition, paired with PVS(carrier,target). Second condition deliberately retains attackerDistance from BASE, paired with PVS(carrier,attacker). Do not fix that donor quirk. Require native session difference, then AddScore MP2/base1→increment carrier_defense→PERS_DEFEND_COUNT→visual/reward.

No RNG draw belongs to these helpers themselves. The effect owners retain any genuine downstream behavior.

## Real existing dependencies and API proposal

Existing true owners read:

- qa_q3_source_actor_slot / qa_q3_native_client_slot / source_entities physical bindings and client_actors. A non-client source actor is a legitimate no-op where donor checks pointer null; missing required native player ownership must not become fabricated zero state.
- qa_q3_source_current_origin_read and q3_source_body_read in source_effects.c preserve actual command currentOrigin and validate actor/physical generation after world read.
- q3_client_follow_player gives actual full copied source PS alias. Source powers/generic1 must respect this alias when present, otherwise native client_actors[slot].state.player owns them. Do not clone a persistent transport/PS cache or require unrelated PM/score services just to read these scalar owners. Existing qa_q3_client_tokens_read at snapshot reads raw client_actors.generic1 and does not consult copied alias; this is a dependency to settle, not a verified equivalent for every source PS path.
- qa_q3_client_award(...QA_Q3_AWARD_DEFEND,1,error) owns true PERS_DEFEND_COUNT and updates copied alias; qa_q3_source_award_visual owns PS eFlags and source rewardTime and updates alias.
- Source classname is already real qa_q3_source_binding.classname. Actual item spawn.dropped supplies item DROPPED_ITEM ownership; mapped flag/Obelisk constructor identities must be checked against full source generation.
- qa_world_geometry supplies actual shared collision geometry; qa_collision_point_leaf, qa_collision_cluster_visible(...false...), qa_collision_areas_connected implement real PVS. Reject absent geometry rather than guessed visibility. Existing map/runtime.c team-location path shows the real policy and int32 cluster/area bounds.
- Existing game options hooks.server_command is the true broadcast sink. It must be bound for the carrier message effect; no empty/default callback qualification.
- Existing qa_q3_source_score_plum emits true temporary EV_SCOREPLUM65 with SINGLECLIENT256, source entity number, amount and origin. It does NOT perform shared score or source ranks.

Proposed bounded private/new header API sent root (not confirmed or created before freeze):

```c
typedef struct qa_q3_source_team_combat_services {
    void *context;
    bool (*live)(void *, qa_error *);
    bool (*add_score)(void *, qa_actor_id, qa_vec3 current_origin,
                      int32_t source_amount, qa_error *);
} qa_q3_source_team_combat_services;
bool qa_q3_source_check_hurt_carrier(qa_q3_game *, qa_actor_id target,
                                   qa_actor_id attacker, qa_error *);
bool qa_q3_source_team_frag_bonuses(qa_q3_game *, qa_actor_id target,
                                  qa_actor_id attacker_or_none,
                                  const qa_q3_source_team_combat_services *, qa_error *);
```

Root must resolve actual public declaration/header/hook placement with corewriter before caller edits. A borrowed AddScore callback is needed because source game currently has no mode/rank owner capable of the complete effect. No guessed/default AddScore callback or second score authority.

Actual TS AddScore: client-null/warmupTime!=0 returns; otherwise true scorePlum→native PS score/shared bound score addition→if source gameType==GT_TEAM3 add actual source teamScores at PS PERS_TEAM3→source calculateRanks. Do not skip amount0 effects. Existing objective helper points() has actual warmup→source scorePlum→qa_modes_add_score→native rank order, but source GT_TEAM teamScores behavior and selected-mode duplication need exact root integration. Native objectives adapter has no exported general AddScore function at snapshot. Current selected source damage/death integration lives src/app/application/services.c and must preserve legitimate chosen-mode counters while avoiding duplicate native score/bonus/reward effects.

Native death stage ordering: genuine source obituary/log/PERS_KILLED/basic score/gauntlet/excellent/lastKill fields precede Team_FragBonuses, which precedes MOD_SUICIDE flag return and ordinary toss/nodrop. q3_dynamic_wire_owner owns death.c and has been notified. Root owns actual harm and source-death hooks in services.c. Full source/actor-generation checks and retained game observation_depth must cover effectful score/rank/server-command and real world queries before any continuation. No stale actor/slot pointer writes after callbacks.

## Frozen dependency snapshot, hashes only (not acceptance)

These existing files belong to other owners. Values are a read-only point-in-time snapshot; other workers may have later changes. No status/readiness claim follows.

| Path | SHA256 |
| --- | --- |
| include/qa/game_q3_client_types.h | 802385b30321e5e6b453c91da9e3c1cc71820cffc6b83cadd5feea207ebdb3b9 |
| include/qa/game_q3_clients.h | d70aecc390b0ff6d13d65becc635000e7cce524b599ed3667303e1fa74a46f38 |
| src/gameplay/q3/internal.h | 6250c19dc40984da4ee9e362c409ea0af74ccd6e9e6ecd4d76ca7d92757ebd74 |
| src/gameplay/q3/client_begin.c | 7f3b2406eab6254ca891e5f399c0eb79dea0854f2e39d8b07c42751e6121c89b |
| src/gameplay/q3/source_entities.c | 7804ca92886e458e30ee5f064a8bbb5b1ff1e6eb7ec1bf3f5715c8b1b4a99afd |
| src/gameplay/q3/save.c | 91fd8ea67876034a8aa86b5ad3c46896f8c758c791f32db71853000994ec4cd4 |
| src/gameplay/q3/death.c | cc993955f4a05e419daa1b533355f378ff688ed091f1367632b95b5c0b9ca0e5 |
| src/app/application/services.c | 09b52c89142817e745ea2ef7c4115abd0b770318e61919d3bf8177a2eaeff978 |
| src/app/application/native_q3_objectives.c | 263f134c45dc4c3fe111d37847fd4b3d059b55acc1d5b8a673efa5c86d5c73ce |
| src/gameplay/modes/q3_objective_source.c | 739702aa83d330dd14324706e98d1f8accd97a342f423a0caf0f54755dea0771 |

## Concrete unfinished work

1. Root releases accepted stable sourcecore9; corewriter implements/independently reviews sourcecore10 native telemetry type/storage/reset/private codec/read/write APIs with actual source fields.
2. Resolve true source player-pointer and copied-PS scalar semantics, genuine DROPPED_ITEM ownership and root complete AddScore capability/public declarations. Keep selected-mode counters separate.
3. Implement the still-absent source_team_combat.c/.h against those real owners, preserving all branch/effect/f32/signed32/order details above and full callback lifetimes.
4. Objectives owner migrates pickup/recovery/capture/assist timestamps/counters to corewriter's source pers; selected-mode telemetry remains its own projection.
5. Dynamic owner integrates helper at genuine death stage; root integrates exact source damage hurt stage and real score/rank/broadcast effects with no duplicates.
6. Read entire new pair and actual callers, freeze exact hashes/evidence, independent review through root, repair/review confirmed defects, checkpoint by root. Source-only gate remains closed; no compile/runtime or whole-project qualification is earned from source acceptance.

