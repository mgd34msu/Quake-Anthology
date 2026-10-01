# Bot source integration handoff — 2026-09-30

The work is frozen at the user's requested new-session handoff. No further implementation, CMake, compilation, tests, gameplay, generated-code tools, parsers, or executable validation were run after the freeze. This document records source inspection and existing independent source-review results. It does not establish complete BotAI, mixed-game composition, runtime behavior, or project parity.

## Resume rules and ownership

- Workspace: `/home/buzzkill/Projects/quake-anthology`.

- Strict source-only gate remains closed: source reads/edits, hashes and whitespace review only. Do not build, compile, run tests/game/scripts/parsers/generators, or use execution as a substitute for the actual source audit. No descendants were created by this worker.
- `/root/bots_resume` is the single writer for existing `src/bots/**`, bot public interfaces and application `bots.c`, `bots_private.h`, `bots_projection.c`, `bots_save.c`, `bots_submit.c`, `bots_round.c/h`. Coordinate explicit grants before editing other owner paths.
- Sibling exclusive new-file lanes: `/root/bot_orders_owner` owns `source_chat.c/.h` (previously Orders/Events); `/root/bot_source_goal_owner` owns new `source_setup.c/.h` (previously Goal); `/root/bot_team_policy_owner` owns new `source_match.c/.h` (previously Policy); `/root/bots_source_peer` now owns new application `bot_world.c/.h` rather than editing parent adapters.
- Do not restore deleted scripts or add local-machine directory references to user-facing documentation. No such restoration occurred in this packet.

## Accepted checkpoints and exact records


The accepted integration28 record is `/tmp/qa-bots-source-integration-20260930.sha256`, manifest SHA-256 `5e5b0b3dfaf18eb7fe76ffb536ec614f2eb9a11a5c733204fd34c38b1868819c`. Independent peer `/root/bots_source_peer` read the complete bounded packet, verified all 28 hashes twice and scoped whitespace, and accepted source semantics. That acceptance covers that exact recorded base, not subsequent chat callers.

Its exact 28 paths are:

```
include/qa/bots.h
src/app/application/bots.c
src/app/application/bots_private.h
src/app/application/bots_projection.c
src/app/application/bots_round.c
src/app/application/bots_round.h
src/app/application/bots_save.c
src/app/application/bots_submit.c
src/bots/ai/chat.c
src/bots/ai/session.c
src/bots/ai/checkpoint.c
src/bots/ai/combat.c
src/bots/ai/console.c
src/bots/ai/decision.c
src/bots/ai/frame.c
src/bots/ai/internal.h
src/bots/ai/reset.c
src/bots/ai/roster.c
src/bots/ai/save.c
src/bots/ai/team.c
src/bots/ai/source_orders.c
src/bots/ai/source_orders.h
src/bots/ai/source_team_policy.c
src/bots/ai/source_team_policy.h
src/bots/ai/source_events.c
src/bots/ai/source_events.h
src/bots/ai/source_goal.c
src/bots/ai/source_goal.h
```

Accepted new helper records at that base:

| Helper | Manifest | C hash prefix | H hash prefix |
| --- | --- | --- | --- |
| Text Orders | `/tmp/qa-bot-source-orders-draft-20260930.sha256` | `37fee80c` | `5b6d3872` |
| Team Policy | `/tmp/qa-bot-source-team-policy-20260930.sha256` | `0adbc40a` | `5424c445` |
| Events | `/tmp/qa-bot-source-events-20260930.sha256` | `b4a13765` | `c0b5b909` |
| Long-term goals | `/tmp/qa-bot-source-goal-20260930.sha256` | `0de76bb5` | `46b0fbf4` |

Current same-map original-retention record `/tmp/qa-bots-original-handoff-20260930.sha256`, manifest hash `05d77abc115ca6c63accbb1aee06d1a5cfadcd57375434a756798f995750075e`, covers `bots_round.c`, `bots_round.h`, `bots_private.h`. Native source peer independently qualified the retention delta and actual restart consumers; broad new `bots.c` changes require their own review.

## Current freeze: parent chat callers are unreviewed

Fresh read/hash snapshot is `/tmp/qa-handoff-bots_resume-current-20260930.sha256`. It contains the current 12 changed parent paths, unchanged `bots_private.h` dependency and all six new Chat/Setup/Match helper paths. Exact per-file hashes are in that file. It is an inventory, not acceptance.

The original parent chat caller draft `/tmp/qa-bots-source-chat-callers-draft-20260930.sha256` has manifest hash `f8034c52a0e04e20bafb5d7c3c0ccc9ef5cf7207e82ab08ceec8db940435c2ab`. Its 11 hashes and scoped whitespace passed before the announced current-source observer/intermission amendment. It is now superseded for `bots.h`, `internal.h`, `frame.c`, and `decision.c`. `src/app/application/bots.c` is the additional changed parent path. None of this revised parent packet has independent whole-packet acceptance.

Exact current parent paths:

```
include/qa/bots.h
src/bots/ai/internal.h
src/bots/ai/chat.c
src/bots/ai/checkpoint.c
src/bots/ai/save.c
src/bots/ai/roster.c
src/bots/ai/reset.c
src/bots/ai/console.c
src/bots/ai/frame.c
src/bots/ai/combat.c
src/bots/ai/decision.c
src/app/application/bots.c
```

Current `bots_private.h` is unchanged at `31d80019fb85a4fcbde6e00625e26e5c3a6146edf9d0ced64c8c0bc29057c493`. All accepted app projection/round/save/submit and source session/team helpers retain their separate base record; do not extend acceptance of their dependencies to revised parent files automatically.

## Accepted source Chat pair; parent integration pending

`/tmp/qa-bot-source-chat-20260930.sha256` is independently accepted, manifest hash `f3346259b7fc79228916d9a58aa163a916a3e58b044d888353ebcd3aa02d376b`.

- `source_chat.c`: `33579a0414b3c6ecc52a3c83cd60cf0aa7b9c3b3d83f296954832729aee0407e`.
- `source_chat.h`: `240f166dbf7bce8eadc22a2bb0fdea73de131d9b5629759a2ae5825b7bc7f34f`.
- Peer reviewed complete C522/H36 plus donor and dependencies, twice hashes and whitespace. Final repair added immediate full-actor qualification after three effectful initial-template counts that can print and retire the bot. The pair is frozen; root must handle checkpoint/TU registration.

Per-bot `source_chat` owns `chat_to`, `last_frame_health`, `last_hit_count`, `enter_game_chat`. Population `source_chat` owns six independent maxclient cache cells: active, first, last, first_name, last_name, opponent. Constructor/full BotResetState clears these; ordinary respawn preserves them. Chat product uses actual `services.team_arena`, not an alternate product cache.

Exported helpers implement enter/exit/start/end/death/kill/enemy-suicide/hit-talking/hit-no-death/hit-no-kill/random chats, ChatTime, valid-chat-position, visible-enemies and genuine ChatTest. ChatTime returns two seconds but still genuinely reads bounded CHAT_CPM; retain the call. HitTalking has two actual ChatTime calls. Team death/end vtaunt can report ready while preserving previous destination/time; team kill/random/start vtaunt refusal differs. Reply does not update `chat_to`.

Parent on-disk changes:

- Embedded per-bot/global Chat state; save schema bumped from accepted4 to draft5; typed checkpoint copies all six global cells and the full per-bot continuation. Removed old two exit-chat cache cells and ordinary `chat_pending`; shutdown-specific pending state remains.
- `chat.c` now contains actual configstring client-name/easy-name/Print helpers. Old generic active-client/opponent/exit implementation removed. Shutdown invokes real source ExitGame helper.
- Console delegates valid position to source helper, uses 36-byte reply bot name, and successful Stand starts enemy search at source `time+1`; destination retained.
- Frame samples true PS before reliable console intake; retains previous inventory through outer console; actual inventory update occurs only in the deathmatch nonintermission block. Snapshot then air then queued messages then TeamAI then enter-game one-shot then node dispatch/end health+hits.
- Frame/setup end writes `source_chat.last_frame_health` from inventory and `last_hit_count` from sampled curPs.persistant[1]. FindEnemy owns separate mutable `last_health`; frame does not overwrite that source field.
- EnterGame is one-shot for `!enter_game_chat && enter_time > time-8`; mark attempted even refusal, real ChatTime and Stand entry on success.
- Intermission entry does full reset then EndLevel chat/send. Leaving Intermission uses StartLevel duration or default2 before Stand. Observer full reset. Death/Respawn resets movement/goals/avoid state only, preserving source event/order/chat/inventory continuation. Death chat drives actual delayed respawn and Talk. Stand damage invokes HitTalking with two duration reads and correct rounded expression. SeekLtg random-chat prefix and Fight suicide/kill/hit chat prefixes are wired.
- `DECISION_CALL` stops a decision turn if real callback retires its full-generation actor. These caller edits remain unreviewed together.

Current source predicate amendment is deliberately separate from detached projection flags. Donor `ai-combat.ts:292–299` makes observer sampled PM_SPECTATOR2 OR current CS_PLAYERS team3; intermission current GAME clock OR sampled PM_FREEZE4/PM_INTERMISSION5. New `bot_ai_source_observer`/`bot_ai_source_intermission` read those actual current producers at the source call sites. `services.source_intermission` is bound in app `bots.c` to `qa_q3_source_match_state_read().intermission_time_ms` for primary native Q3. Other source families currently reject pending actual SourceBotGame integration. The native projection still carries old `.intermission=pmType5||6`; real AI predicates now bypass that field, but other generic callers need source review.

## Mandatory genuine fixed source player-state producer is pending

Public `qa_bot_source_player_state` currently exists with `{present,has_player,pm_type,score,last_hurt_client,last_hurt_mod}` and `services.source_player_state`. No actual app callback binding exists yet. Keep the missing-producer rejection; do not fill invented defaults or equate all64 source PLAYER-kind rows with a `.client` pointer.

True donor pointer behavior: `q3/entities.ts` constructor gives `.client` only rows below actual maxclients; activate/initialize reinstall actual pointer; records attach does likewise; `records.ts:175–184` deactivate clears actor/active but retains pointer. Source release can clear the pointer. Static PS PM0/score0 before binding are real constructors for genuine pointer rows; absence of live actor does not license a substitute live-player lookup.

`/root/q3_source_resume` owns actual pointer metadata/lifecycle/codec. `/root/q3_wire_producers` was authorized to implement fixed PM/score readers after wire4 release. Declarations now exist in `include/qa/game_q3_wire.h`:

```
qa_q3_wire_client_source_pm_read(const qa_q3_game *, uint32_t, int32_t *, qa_error *)
qa_q3_wire_client_source_score_read(const qa_q3_game *, uint32_t, int32_t *, qa_error *)
```

No corresponding implementation was found during freeze reads. Actual full `qa_q3_wire_player_read` needs live body/movement (except genuine stored FOLLOW) and cannot serve all inactive fixed rows. Planned fixed producer reads true live PM/score, constructor state for genuinely initialized unbound rows, and captures both before actual control/mode retirement; existing detach already retained PM but not score. Body actor-null must happen at true source deactivate even if canonical actor is still live. `qa_q3_client_taunt_read` already supplies actual fixed last-hurt fields.

Chat rank requires present+real pointer; current enemy death PM uses real fixed pointer even when not present; Hit chat reads actual hurt through existing pointer. Parent adapter must qualify each exact semantic branch, not require presence universally or invent fallback. This also serves native postgame consumers.

## Setup pair is a frozen draft, not integrated

`/tmp/qa-bot-source-setup-20260930.sha256`, manifest hash `7d8a03e2332fe86333f4e23f71d490f85ffad5cb88b01829e63ce4ad141003dd`:

- C `02090b18143a059c7dd7a04ae58c5d7638bf546e41323f55e1cd1201be3b9d02`.
- H `0d3d80deb5592a1143c6cf67cb0a28a78cffc59ff511c7cc6ff28e91fbc227b8`.
- Worker requested independent review; no acceptance received here. No parent include/embedding/caller/services/codec has landed.

Stable `source_setup` state is `team[144]`, `map_restart`, tagged `progress` EMPTY/SETTING_UP(stage)/FAILED(failure stage,error)/COMPLETE. Actual stages include ALLOCATED through CHAT_GENDER, PUBLISHED, MOVE_STATE, WALKER, COUNTED, SCHEDULED, INTERBRED, SESSION. SESSION exists in source type but genuine setupClient never assigns it. AAS/character failures carry no error; item/weapon/chat failures carry actual error codes. Setup status is a logical source property; full decision reset preserves it and settings.team, while clearing mapRestart and setupCount.

Agreed helper split: parent acquires/resource-loads source state, then publishes actual tables/inuse/physical identity/setupCount4/enterTime/PUBLISHED. New helper owns movement allocation, walker characteristic, increment count/COUNTED, actual ChatTest, schedule/SCHEDULED, genuine interbreed mutation/INTERBRED, restart sessionRead, COMPLETE. Movement allocation handle0 is an accepted source result; do not add a false nonzero exhaustion error. Newly acquired curPs is source constructor-zero and available, not an early source PS sample.

Four-tick helper decrements setupCount; fourth tick gets raw userinfo, performs exact source InfoSetValueForKey sex mutation/Print behavior, writes raw userinfo, sends requested `team` when real mapRestart false and actual GAME gametype is not tournament, lowercase-only chat gender, true physical client chat identity/configstring name, source frame health/hits and real alternatives.

Required callbacks are agreed but absent from public/app integration:

```
get_userinfo(void *, qa_actor_id, char *, size_t, qa_error *)
set_userinfo(void *, qa_actor_id, const char *, qa_error *)
source_game_type(void *, int32_t *, qa_error *)
```

Source GAME gametype for this helper must come from actual copied GAME settings: `ai-combat.ts:814` uses `context.game.gameType`; native runtime.ts292 reads the real cached `g_gametype`. `application_native_q3_settings_integer` is the genuine native getter. Raw source userinfo can consume native wire userinfo/read + genuine UserinfoChanged; current generic key/value `services.userinfo` cannot preserve source invalid/overflow behavior.

Never initialize map_restart from admission.restart. Donor ai-state constructorfalse and resetDecisionState clears raw offset6020; setupClient does not write it. Settings.team must be actual launch request retained as source bounded144 text, not current configstring team.

### Unresolved acquired/inuse/count topology

Existing roster still performs all resources/movement/walker/session before table publication; sets chat name too early; assumes every occupied row increments count and has all nonzero handles. True donor `ai-main.ts:285–358` acquires an initially not-inuse source row, keeps failed setup progress, then publishes inuse BEFORE movement/walker/count/test. A callback failure after publication must preserve genuine published state and stage, without rollback/replay of chat/RNG/count/session effects.

Before wiring the helper, add genuine acquired vs inuse state and stage-qualified count/handle topology. PUBLISHED/MOVE_STATE/WALKER are not yet counted; COUNTED+ are. Movement0 must remain legal. Source frame/schedule/shutdown should filter actual inuse, while pure disposal can consume failed resources. Duplicate setup of inuse rows rejects per donor; do not retry/replay effectful publication tail. Update private schema, typed checkpoints (current allocation uses count but iterates every occupied row), actor/source lookup tables, teardown decrements, restore requirements and bounds together. Failed actual weight loads release genuine handles exactly as source does rather than using stored stale numbers as valid resources. This is unresolved design work, not implemented code.

## Match/TestAAS pair is an unreviewed draft with one confirmed branch defect

`/tmp/qa-bot-source-match-draft-20260930.sha256`, manifest hash `17d612f71f8430c5ea9ba070bef92351d790bac6e3fcc8c64f157cba030175a6`:

- C `d02bc11092b53b39b7ba9c205abb7356719a8a35bf3549a9064810a9a6e2460c`.
- H `c17f152151d5922abc567d6ca7a7c8f8fd0248fa8307c2221806151cf6c2bcf1`.
- Worker reports whitespace/conflict scan clean; independent peer only received draft notice, no acceptance. Parent fields/services/callers/wrappers/public declarations/codec are not implemented.

Population `source_match` owns six real copied BotCvar cells, each `value[256]`, numeric float, integer, modification_count, registered: bot_testsolid, bot_testclusters, bot_interbreedchar, bot_interbreedbots, bot_interbreedcycle, bot_interbreedwrite. Also actual interbreed flag and match count. Registration must snapshot each tracked cell immediately after its actual registration callback, not after the entire loop. Setup validates copies. Private codec/checkpoint must include the actual six cells and fields.

Real helpers cover Interbreeding activation, InterbreedEndMatch genetic rank/fuzzy save/interbreed/mutate/counters and TestAAS point/cluster/Print. Activation requires actual `exit_level` and `insert_console_command`, internal roster `bot_ai_source_shutdown_client`, actual source bot state iteration and true runtime RNG. Parent must invoke activation at donor frame454, after source memorydump/routingcache/pause branches, not just fabricate an alwaysfalse interbreed flag. Those complete cached frame branches remain open in the current frame implementation.

Confirmed draft defect to repair on resume: activation was changed to `services.source_game_type` just before freeze. Donor ai-main.ts511–518 instead tests `context.gameType`; ai-context.ts194 returns deathmatch.gametype, represented by `b->source_goals.game_type`. Setup's `context.game.gameType` is different. Revert Match activation to the real deathmatch cache; keep GAME cached service for Setup. This distinction was verified by direct source reads and sent root/worker.

Exact nested capability agreed: ephemeral `size_t source_match_exit_depth` lives only around genuine services.exit_level. It is not serialized and must reject captures/restores/readiness while active. EndMatch public wrapper may accept busy only with actual depth, nonrestore/nonshutdown/nonspawn source state and real initialized leased runtime, preserving previous busy state. Draft exports pure `qa_bots_interbreed_end_admitted` getter and public qa_bots_interbreed_end_match/TestAAS. Parent has not added the field, declaration or admission guards yet. Check true lease qualification during independent review rather than broad busy bypass.

Application consumer declarations/bodies remain absent:

```
application_native_q3_match_bots_end(application_provider *, qa_error *)
application_bots_test_aas(application_provider *, qa_vec3, qa_error *)
```

`/root/native_q3_log_owner` already placed real ExitLevel call to match_bots_end first under true ENTITIES/source console scope and exposes `application_native_q3_match_exit_level`; it queues actual transition commands/session work rather than immediate world replacement. `/root/tools_source_review` new native Think already calls TestAAS after true currentOrigin scope ends/read and before Impacts. Both include owned bots_private.h; declarations missing. TestAAS must use actual source availability, initialized map navigation and genuine Print; absent required owner is not an empty optional callback. Human-only native maps may lack actual population because current prepare/publish is bot-seat-driven; source BotAI constructor/setup availability needs proper integration before these callbacks can be honestly available.

TestAAS donor ai-main.ts117–136 updates actual two cached cvars, solid precedence, checks actual navigation.ready, true point-area +10z trace fallback, genuine cluster read and exact Print strings (including source typo `\remtpy area`). Do not replace source Print with log/HUD/event.

## Existing source lifetime and frame owners to preserve

- `close_bots` rejects any all-live original GAME host borrowed runtime before physical population/runtime/navigation/service disposal. `application_q3_guest_bots_borrowed` scans every physical live provider including failed pending old owners. Broad `application_bots_can_destroy` remains idle-only so source Shutdown/retirement can happen.
- Ordinary publication now keeps old runtime through actual old role/executor consumption/deconstruct; terminal pending-owner drains occur before pure free. Old hosts may import bot services during actual native image destructors; do not detach aliases prematurely.
- Qualified original same-map restart=true retains exact initialized/loaded library, navigation/content/resources through old physical executor retirement, then rebinds real services/namespace to fresh provider before source Init. No synthetic BotAISetup/LoadMap, flag flips or Init replay. Preflight current+latched bot_enable, true old post-Shutdown registry, and final fresh post-QACV/latch registry all validate library readiness. Ordinary new maps use normal fresh lifecycle.
- Native `application_bots_frame_at` qualifies source/frame/host and begins actual wire BotCycle only for native Q3 population, calls qa_bots_frame, and ends token on success/failure before clearing producing. Current native lazy snapshot map stores physical source numbers from true current PS/entity/link/PVS; it is cleared at AI frame start and retained afterward for continuation. Actual original loaded GAME transport snapshots remain its own path.
- Source session lastGoal fields, shutdown phases/same-actor retries, physical source_clients schedule/frame order, >200 current-frame captured think interval, Orders/Policy/Events/Goal codecs and real inventory order were in accepted integration28. Ordinary death does not clear the event cache/obituary counters/order state. True Observer/Intermission/level full reset clears inventory/projection positions but retains sampled curPs/settings/handles/source identity. Do not regress these while adding Setup.

## Full combat/activation and mixed source remain open

The accepted helpers do not complete all source AI nodes. Current decision/combat still use generic canonical current-player visibility/target filtering, attack/movement/chase/retreat/activation behavior and selected weapon knowledge. Complete source battle inventory/rememberEnemyPosition ordering, actual source flags/state, predict-obstacle/activation stack, movement result flags/waiting/roam/attack and map script/node-switch behavior require real donor tracing. `move_goal` currently has generic blocked-time random escape. Activation stores full actors and checks current bodies/movement; do not call this full source BotAI parity.

Current native PS projection reads real qa_q3_wire_player_read once and overlays actual selected arsenal weapon/phase. Q1/Q2/nativeQ3 arsenals have real typed readers; selected original guest arsenal is explicitly unsupported. Preserve actual selected weapon/item map and RNG/clock semantics; do not supply guessed zero weapon or duplicate authoritative PS. Entity event and Botlib sensory reads consume actual source physical row/event time/current availability; source current absent entity intentionally becomes genuine default EntityState(number0) in the event callback.

Current primary non-Q3 paths are incomplete: actor.slot namespace fallback, fallback projection, no retained arbitrary nonplayer configstrings, and empty console poll are not genuine SourceBotGame. Native-only event/policy/intermission callbacks reject these pending producers. Do not claim arbitrary Q1/Q2/Q3 composition complete from shared canonical adapters.

## New non-Q3 SourceBotGame lane

Root assigned `/root/bots_source_peer` sole new `src/app/application/bot_world.c/.h`; parent adapters remain single-writer here. At freeze the worker had a typed API proposal, not accepted implementation. See that worker's handoff for final files/hashes.

Full donor `../quake-typescript/src/app/bootstrap/simulation/bot-world.ts` was read. Genuine sensory namespace/private continuation includes actor→physical IDs and reverse full actors, dynamic IDs64..1021, free LIFO, generation map/nextGeneration, begun set, actual userinfo/arbitrary configstrings/model indices/GameMemory. Player ID is actual selected movement client's source slot; world1022; actor.slot is not authority. Fresh baseQ3 PS/ES constructors per observation provide intentional constructor fields; actual body/movement/client/combat/arsenal overrides come from true primary and selected source owners. Pointer exists only with genuine actor+movement+clientInfo+body; no native fixed all64 assumption. Real non-Q3 hurt/events/eventTime zeros are explicit SourceBotGame construction semantics, not fake native-Q3 fallback.

Proposed opaque holder services: source Q1/Q2 and real max_clients/base_model_count; metadata(fullactor)→present/model/classname/frame/hidden/max_health; movement→has_player/client/viewangles/viewheight; actual client info→name/skin/spectator/score; bot_actor/world_actor/selected_weapon; real source clock/intermission and source userinfo/connect/begin/drop/print/console/message/activate/exit callbacks. Holder owns sensory construction, IDs/full generations/strings/models/memory and pure codec/restore aliases. Verify max_clients from actual selected source owner, not choices seat count.

Lazy selected-number snapshot cache is in ApplicationBots host, not bot-world itself: donor `simulation/bots.ts:428–443` builds true present+linked+!hidden list for non-Q3, current selected Q3 visibility for native, caches perbot, clears each actual AI frame. Coordinate actual holder/parent owner and codec; do not copy transport snapshots. Current true GAME RNG source calls already use Q1/Q2 actual checkpointed host RNG. Primary Q2 intermission and Q1 clock source semantics remain to wire. Userinfo must have actual Q1/Q2 source effects. Pure restore must remap true namespace/generations without constructor callbacks.

## Native Connect is an independent open lifecycle boundary

`/root/q3_clients_resume` reports real Connect presently performs source wire/pers/session then LOG/UserinfoChanged/ranks, missing actual G_BotConnect/initialized BotAI callback before LOG. Current parent app population/library setup is still later `application_bots_publish` after canonical admission, so supplying a pretend accepted callback is invalid. Design actual native source BotAI setup/availability and true bot admission before ClientConnect callback, with source partial-state/restart/denial ordering; coordinate wrapper and population/table ownership. Avoid double source shutdown: high-level source bot director vs actual ClientDisconnect order must stay distinct.

## Next concrete work when user resumes


2. Complete actual fixed source `.client` pointer plus fixed PM/score continuation/lifecycle/codec and bind parent source_player_state to genuine pointer/current fields/taunt producer. Review copied FOLLOW and deactivate semantics.
3. Independently review revised parent Chat12 predicate/state/caller packet with accepted Chat2 plus actual producers; announce a new full freeze instead of reusing superseded draft11 acceptance. Register/checkpoint helper pair through root.
4. Resolve acquired/inuse/counted source table and partial resource topology, preserving real Setup stages and publication effects. Add actual settings.team/mapRestart/progress, raw userinfo/GAME copied type callbacks, schema/checkpoint/reset/topology/teardown changes, then call frozen Setup helper and replace old premature setup effects.
5. Correct Match gametype draft branch; embed/cache/codec actual globals and ephemeral ExitLevel depth, source shutdown helper/public declarations, genuine EXEC_INSERT/ExitLevel/app wrappers and source availability. Wire exact frame/setup/native consumers and independently review the complete caller graph.
6. Implement the new bot_world sensory/private owner and integrate its real non-Q3 namespace/PS/config/userinfo/current snapshots/events/source clocks without gameplay copies. Close selected original arsenal observation and composition producer dependencies.


