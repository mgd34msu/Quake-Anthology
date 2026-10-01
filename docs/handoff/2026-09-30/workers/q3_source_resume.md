# Q3 source-core handoff — 2026-09-30

## Stop state and authority

Agent: `/root/q3_source_resume`. Workspace: `/home/buzzkill/Projects/quake-anthology`.


Behavioral oracle: the TypeScript checkout `/home/buzzkill/Projects/quake-typescript`; original source references in `/home/buzzkill/Projects/qsrc`. Native C layout may differ, but observable source behavior, private continuation, caller ordering, and actual state ownership must remain genuine. Shared body/combat/inventory/selected score owners are authoritative; do not create a second canonical world, infer native PS from event history, fabricate missing source state as zeros, or mistake API declarations for producer completion.

## Exact frozen core9 packet — NOT accepted yet

Manifest: `/tmp/qa-q3-source-core-v9-20260930.sha256`.

Manifest SHA256: `a7b29487efb9459ad6322acca997c4b32800acffa29e8f88d27cae9ffb622f64`.


Hold all these exact files until an independent reader reviews the entire actual producer/lifecycle/codec packet and either accepts it or identifies specific repairs. Any repair requires replacing the manifest and re-reviewing the changed producer and continuation. Do not silently broaden this frozen packet to include next10 fields.

```text
7bb0641cfb1175ce30345536823d519ea4d0e8b043151f80791f6ffd1a7e8467  include/qa/game_q3.h
f94ffaa645a1c38905cb214237f1b3aaebc9d59ee63d214118b01ed80018a897  include/qa/game_q3_client.h
802385b30321e5e6b453c91da9e3c1cc71820cffc6b83cadd5feea207ebdb3b9  include/qa/game_q3_client_types.h
d70aecc390b0ff6d13d65becc635000e7cce524b599ed3667303e1fa74a46f38  include/qa/game_q3_clients.h
d14d22b1a0980fc207e8c87004e3c1877d1f83c31dd640c7adc11ac25b697467  include/qa/game_q3_configstrings.h
d989f95659114bf751ad01ab282856075d7f58a1602550ac878fc7ea2d736cc5  include/qa/game_q3_map.h
c812b95038c1b4723e63eb577e78ed40538e1a985bd31f1a82c551c9a73f04fd  include/qa/game_q3_source.h
df7a061da02274ece55217b0789fb30ce7677bd1a3ff6e81784c918bbb9e5d31  include/qa/game_q3_source_types.h
6250c19dc40984da4ee9e362c409ea0af74ccd6e9e6ecd4d76ca7d92757ebd74  src/gameplay/q3/internal.h
4da716044c721543bacd692a7078929f69c126f78b66fb7bb118a2e46e343eac  src/gameplay/q3/game.c
7804ca92886e458e30ee5f064a8bbb5b1ff1e6eb7ec1bf3f5715c8b1b4a99afd  src/gameplay/q3/source_entities.c
029c8e795966577274a94dada1c23c4f203692bf72d9b12d21de9f7032f540a3  src/gameplay/q3/player.c
4ff45a64019888ca943e2a9038459f44a96a174fbc1b9d5e12b5d69dc0868f86  src/gameplay/q3/view.c
9b827ff5bfec2e115da240a3f3f7a736d08eeaeca7cac2dd404d929ad9f07b46  src/gameplay/q3/weapons.c
7f3b2406eab6254ca891e5f399c0eb79dea0854f2e39d8b07c42751e6121c89b  src/gameplay/q3/client_begin.c
e73f5365a505d2ddec68f71f786e437c895f16f2ba6c389f3103f2e795c7bfb6  src/gameplay/q3/client_commands.c
0cc67d560ba56615b6c6fb4525db29c507bf720cf59a09886c601a0d916245a4  src/gameplay/q3/client_items.c
01c80bfb4439dd0a83b003cbb79bff448499896ae291de1df0a26734f8618a20  src/gameplay/q3/client_private.h
2e522e2e2f46345ce3a788d09727d0dd7f12d24e095b45cf45f203a04dfa924c  src/gameplay/q3/checkpoint.c
91fd8ea67876034a8aa86b5ad3c46896f8c758c791f32db71853000994ec4cd4  src/gameplay/q3/save.c
3260953d377abd6d383b16886e04886b5f884e7eb19f0e1f28a2cd3fd69f912b  src/gameplay/q3/configstrings.c
51286b16a5fed2c590230c2cfff4ffc613bfcea40ea6109708527266e5817fa6  src/gameplay/q3/source_effects.c
2e1542a612551315c4ce27bf4e39863a36d39e6fc5bb5b3e75905d099ff18a8a  src/gameplay/q3/map/runtime.c
f16c10ae602f0a82939af37aebed2b95f3ba1b363b4c2f69e15d872da9a095f1  src/gameplay/q3/map/checkpoint.c
0cc81803e0c97fa47f85d3f560464481c821ed5283d18755b0271c736c577260  src/gameplay/q3/map/internal.h
```

New core translation units `client_items.c` and `source_effects.c` need root's CMake registration if not already integrated. Verify current registrations in the new session; this handoff did not edit or re-check CMake.

Parent previously reported accepted pool27/CMake `f864428`, native session/votes migration `1f85e3a`, and wire producer4 `205c3fed`. Those are parent reports, not freshly independently verified  facts here. The last available  status read still showed several core files as untracked. Root/new session must reconcile actual HEAD/index/remote and accepted manifests; do not claim that core9 isted ored from this handoff.

## Current ownership and dependent workers

This agent owns the 25 core paths above. Root owns, CMake, integration, the selected-world source reaction glue, and the actual primary-attack service/provider binding.

Other owners, whose files must not be overwritten while their packets are frozen:

- `q3_map_wire_owner`: `map/spawn.c`, `map/items.c`, `map/movers.c`, `map/misc.c`, `map/triggers.c`, `map/targets.c`, and native mover implementation. Genuine map constructors/readiness/link/think/blocked callback/sound fields. Core map runtime/checkpoint/header belong to this agent.
- `q3_dynamic_wire_owner`: `items.c`, `missiles.c`, `death.c`, `holdables.c`, `feedback.c`. Genuine dynamic constructors, physical temp events, corpse/portal/kamikaze, feedback and actual native END effects. Latest candidate3 was being prepared; obtain that owner's handoff and exact peer status.
- `q3_wire_producers`: `source_wire.c`, `source_wire.h`, `include/qa/game_q3_wire.h`. Actual PS/ES/link cache/wire fields, nested continuation, PM owner bridge. Producer4 was accepted/released by root; a new unit has begun on fixed client PM/score/body reads and explicit Obelisk passthrough. Do not treat the mutable next unit as covered by producer4 acceptance.
- `q3_shader_remap_owner`: shader remap owner/public/private files and target router changes. Accepted shader5 plus separate accepted native print adapter. Offers integrated caller review after core freeze.
- `tools_source_review`: NEW `application/native_q3_control.c/.h`, actual ClientThink intake/pre-PMove/post-PMove/events/link/touch/impact pipeline. Draft/freeze status must be read from its handoff; core9 is its dependency.
- `frontend_model_inventory`: NEW `application/native_q3_end_frame.c/.h`, genuine physical client END and spectator FOLLOW. END2 was frozen; a later required water-holder read migration was sent before handoff. Verify whether that repair was landed/refrozen.
- `q3_pool_source_peer`: NEW `application/native_q3_rank.c/.h`, actual CalculateRanks. It no longer owns source pool review in this wave. The ranking tail needs actual match CheckExitRules, not merely getter helpers.
- `native_q3_log_owner`: NEW `application/native_q3_match.c/.h`, actual source match/intermission/exit rules.
- `native_q3_objectives_owner`: NEW application objective bridge/helper and NEW `q3/source_objectives.c/.h`, actual Obelisk model + trigger/source actors and per-trigger combat admission.
- `q3_clients_resume`: native clients/chat/session/console producers and mode-source-session migration. Owns `console.c` narrow changes; do not include that file in core25.
- `movement_resume`: actual selected Q3 controls writer, borrowed kernel cell, native command/session qualifier and arsenal source actor routing.
- `q3_round_resume`: providers/map/load/reset/native_modes/settings lifecycle and real source callbacks.
- `bot_team_policy_owner`: accepted native TEAM location/status producer, then new source_match/bot producer work. Core map query is its dependency.
- `persistence_resume`: accepted whole application save caller source5; native bundle QAN3v2/schema3. Core9/private continuation is a separate dependency hold.
- `native_q3_votes_owner`: NEW application and source postgame four-file producer packet, proposed next10 kinds/alias client backing. It just reported independent bounded acceptance of `/tmp/qa-native-q3-postgame-20260930.sha256`; missing core callers/mover/presentation/client alias are explicitly not accepted by that review.
- `bot_orders_owner`: proposed NEW `source_team_combat.c/.h` next10 TEAM hurt/frag bonuses. No source implementation edits were made before handoff. Type/API agreement only, described below.
- `bots_resume`: bot source service adapter; requires genuine fixed player pointer/PM/score state. No fake all64 pointer or live-only surrogate is allowed.

## Accepted earlier substrate and limits

The earlier pool v8 manifest is `/tmp/qa-q3-source-pool-v8-20260930.sha256`, SHA256 `bc33e58b0e163e75ba5d93a9a745389dd132be752473fe9e49817db4da26bceb`. Peer `q3_pool_source_peer` gave bounded source acceptance for all27 after source reads, hash checks, and scoped whitespace. It covers physical pool/client/body queue/map/teleport/continuation substrate only.

That review repaired actual dynamic classnames/server flags, enemy ownership/reset behavior, physical client order for kamikaze, separately sequenced RNG draws, genuine teleport OUT/IN temp entities, foreign teleport identity qualification, and the NOCLIENT clear in the true PROX_PLAYER non-invulnerable explosion branch. It did not close all Q3 events, wire, visibility/link, settings, shader, FOLLOW, Harvester, or runtime parity.

Enemy is an actual gentity field, not gclient/PS. Canonical enemy provenance and an explicit physical enemy slot/present pair remain separate on `q3_actor`. ClientConnect/ClientSpawn do not clear them. Level/entity reset and explicit voice-taunt clear do. Retained physical enemy client observations survive disconnect without fabricating a canonical actor. Zero is a valid source client slot and absence has an explicit bool. Native chat consumes the retained physical slot.

## Physical source pool and map/load lifecycle

- Physical source rows are 1024, independent of canonical actor slots/owners. Clients occupy 0..63, world is1022, none is1023. Native configured maxClients may be smaller than64. Body queue has eight real persistent physical source actors, not a counter-only proxy.
- `qa_q3_source_actor_slot` qualifies live full canonical actor and exact physical binding. `qa_q3_native_client_slot` additionally qualifies configured source client domain. Neither canonical owner nor canonical slot is a source-client identity.
- Genuine G_Spawn allocation/free/reuse ordering and classname/owner/in_use/never_free metadata are retained. Actual S.number is separate from physical row identity; BG publication can assign source clientNum. Codecs must not insist that S.number always equals physical slot.
- Ordinary map initialization creates the actual world/body queue and registers the real fry sound. Save candidate map binding must bind only an empty source owner (`qa_q3_maps_bind_restore`), without world/body/actor constructor/event replay before private import.
- Source map load tail after authored spawn/find-teams is actual TEAM item checking, registered-items publication/precache, then team shader remap/settings update. Initial `settings.register` copies cvars only and performs NO constructor team remap. Earlier proposed extra constructor-remap reset phase was revoked after donor read.
- `level_state_reset` clears the genuine shader registry/newSession/TEAM/match/transients. Fast reset preserves original seed/startup options according to the actual reset owner; do not replay saved events or read a live registry to repair restored cached settings.
- Cached `qa_q3_rules.gravity` is distinct from shared `game->physics.gravity`. Authored world gravity force-sets the real cvar/shared world side effect, but pendulum/Aim calculations consume copied cached gravity until real load-tail or ENTRY G_UpdateCvars.
- Map runtime allocation preserves authored S.origin/S.angles versus actual current body origin/angles and last-linked source bounds. Generic map body angles begin0; source authored S.angles are not body angles. Genuine typed brush/mover/trigger constructors own their later writes/link calls.
- Generic allocation must not globally mark source rows ready. Typed constructors finish actual S fields, then call `q3_wire_entity_ready`. Pending map items retain actual GENERAL state until delayed FinishSpawningItem; objective admission can consume prepared metadata without early constructor/body replay.

Current public source pool/lifecycle APIs are exactly in `include/qa/game_q3_source.h`:

```c
qa_q3_source_bind_client(game, slot, fullactor, error);
qa_q3_source_bind_world(game, fullactor, error);
qa_q3_native_client_slot(game, fullactor, &slot, error);
qa_q3_source_actor_slot(game, fullactor, &slot, error);
qa_q3_source_entity_count(game, &count, error);
qa_q3_source_max_clients(game, &max, error);
qa_q3_source_binding_read(game, slot, &binding, error);
qa_q3_source_new_session_read/set(game, ..., error);
qa_q3_source_team_location_time_read/set(game, ..., error);
qa_q3_source_spawnflags_read(game, fullactor, &raw_spawnflags, error);
qa_q3_source_match_context_read(game, &product, &installed_start_ms, error);
```

The installed match-context getter is pure and usable inside actual END/CalculateRanks. `qa_q3_round_read` still requires safe transition/destruction state and must not be substituted inside active source callbacks.

## Clock, source frame, command and END routing

- `qa_q3_source_clock` returns actual native GAME ENTRY signed32 milliseconds. `qa_session_clock` between frames is completed EXIT and is not the native clock for settings, command, log, vote, source RNG, or END effects.
- `source_settings_update(context, frame, error)` runs at actual source ENTRY after setting native `now_ms`, before source entity/client logic.
- `qa_q3_source_run_actor(game, fullactor, frame, error)` requires actual provider, Q3 clock, phase `QA_ENTITY_PHYSICS`, and current real frame identity. Physical `source_actor_ran[1024]` prevents duplicate native source execution independent of canonical selected execution.
- Source event expiry runs on every real in_use physical row before typed-kind dispatch, including generic map entities. INACTIVE/FREED/WAITING rows skip ordinary dispatch; ACTIVE rows run source behavior.
- Real `source_client_run` is invoked in actual source client order. Normal async humans with no admitted command return from G_RunClient without synthetic movement or jumppad reset. The old generic actor_frame jumppad reset is suppressed for real native bound clients and retained for nonnative selected-Q3 fallback.
- `component.command_actor` is a pure real full-actor native client qualifier. Session accepts canonical selected execution OR this source qualifier, and nested native commands require true source qualification. Do not broaden arbitrary nested command execution.
- Physical ClientEndFrame iterates real in_use native clients in fixed order, then invokes actual `source_end_frame`. Actual mode frame/votes/CheckCvars/TEAM status/CS tail must run once per real source frame, including zero/multiple native frames per outer app advance. Generic outer native mode-frame duplicates were removed by root/round worker.
- Providers currently bind source world init, TEAM check, client run, client END, selected movement source-state writer, and source END. Root was implementing actual primary attack binding when handoff began; verify it exists rather than assume the declaration proves completion.

Native hooks in core9 include:

```c
bool (*source_world_init)(void *, qa_error *);
bool (*source_team_items)(void *, qa_error *);
bool (*source_client_run)(void *, qa_actor_id, const qa_source_frame *, qa_error *);
bool (*source_client_end)(void *, qa_actor_id, const qa_source_frame *, qa_error *);
bool (*source_end_frame)(void *, const qa_source_frame *, qa_error *);
bool (*source_movement_state)(void *, qa_actor_id, const qa_q3_player_state *, uint32_t, qa_error *);
bool (*primary_attack_allowed)(void *, qa_actor_id);
bool (*source_log)(void *, const char *, qa_error *);
bool (*client_print)(void *, qa_actor_id, const char *, qa_error *);
```

Actual `primary_attack_allowed` must read the real selected arsenal/original primary weapon slot policy. Donor allows source attack only when selectedArsenal is null and the original primary slot is selected. Both native pre-PMove gauntlet probe and ClientEvents FIRE require the hook; absence is an error, denied selection skips the source attack. No movement-kind guess/default true.

## Fixed native client ownership and session7

`qa_q3_native_client clients[64]` and `q3_actor client_actors[64]` are actual fixed source records. Raw sess7 lives in GAME, independent of selected mode/rules/character/movement. Do not reintroduce mode fixed64 sess backing, hidden Q3 rule modes, or actor-membership based reads for disconnected retained sessions.

Raw sess fields are signed32 `team`, `spectator_time_ms`, `spectator_state`, `spectator_client`, `wins`, `losses`, `team_leader`. Masks are TEAM/TIME/STATE/CLIENT/WINS/LOSSES/LEADER bits0..6, ALL127. Arbitrary wrapped source words are retained; configured game/client policies separately interpret them. Source enum NOT/FREE/FOLLOW/SCOREBOARD is0..3.

```c
qa_q3_client_session_slot_read(game, slot, &seven, error);
qa_q3_client_session_read(game, actor, &seven, error);
qa_q3_client_session_slot_write(game, slot, mask, &seven, error);
```

Slot session reads/writes support all fixed64 rows, including disconnected/retained tails above current configured max. Actor convenience requires an actual current native binding. Connect resets whole client including sess7; Begin/Spawn preserve sess. Source session initialize reads cached GAME game_type and retained native nonSpectator count; it does not refresh live module cache or require selected primary mode equality.

Native pers/gclient fields include real accepted usercmd, connection, netname bytes36, local_client, initial_spawn, predict-item-pickup, pmove_fixed, team_info, max_health, enter_time, team_state state integer, team_location, switch time, ping, vote counts, old/buttons/latched buttons, inactivity deadline/warning, old_origin, ready_to_exit, and active source copied FOLLOW PS. Connect/Spawn reset true button/inactivity/ready domains; actual spawn wrapper then writes the cached g_inactivity source deadline and warningfalse before initial Think.

Public operations are in `game_q3_clients.h`: fixed/actor reads; Connect/Begin/Disconnect; separate dropped-items/cube/copy-body queue; spectator/StopFollowing; raw flag updates; accepted/received cmd; button edge/latch; gesture consumption; flags; source team location/state/ready/rank/persistent team/tokens; inactivity; FOLLOW; command-time completion; teleport temp event; ping/initial spawn/team switch; source flags/taunt; fixed-slot and actor ClientUserinfoChanged.

The native initial command seeding API is `qa_q3_player_begin_command(game, actor, const qa_q3_usercmd *, error)` from `game_q3_client.h`. It seeds true angle words for actual source spawn/view; it does not execute the command. Actual pers.cmd retains the complete accepted source dialect, even when selected movement is foreign. Received command sets actual last_command_ms from source ENTRY. Raw command angle words are copied at accepted source intake, not inferred from selected movement degrees.

Source spawned native foreign CHARACTER now still moves the real shared body to the source spawn pose, preserves selected dimensions/combat policy, sets view, executes killbox/link and real selected PM holds. The previous foreign-character early return is removed for actual native GAME clients. Independent nonnative Q3 role birth remains separate and must not demand a selected control before admission.

Native ClientSpawn does not implicitly create a corpse. Actual respawn wrapper must call `qa_q3_client_copy_body_queue` before Spawn; SetTeam copies only when actual health<=0 at its true predeath phase. Nonnative legacy selected-Q3 spawn behavior remains separate.

## Counts, rankings, score and TEAM/match state

Actual GAME CalculateRanks state:

```c
qa_q3_source_client_counts {
    int32_t num_connected, num_non_spectator, num_playing, num_voting;
    int32_t num_team_voting[2], follow1, follow2;
    uint32_t sorted_clients[64];
};
qa_q3_source_client_counts_read/write(game, ..., error);
qa_q3_client_rank(game, source_slot, int32_rank, error);
qa_q3_client_persistent_team(game, source_slot, int32_team, error);
```

Counts validation enforces voting<=playing<=nonSpectator<=connected, each <=configured max, nonnegative team counts and their sum<=voting, follow slots -1..63, sorted tail slots0..63, and a unique configured prefix. Actual producer overwrites only the connected sorted prefix and retains dead tail entries. Source PERS_RANK reads real `player.rank`, not selected mode.rank. Selected score remains the existing score owner until the explicit next10 retirement handoff; do not invent retained source score0 for inactive rows.

Actual source player fields added in9: rank, persistent_team, generic1/tokens, defend/assist/captures, portal_id, existing source awards. Public token read/write updates actual source generic1 and active copied PS; native TEAM helper must use it rather than mode.stats.tokens.

Actual GAME TEAM holder:

```c
qa_q3_source_team_state {
    int32_t team_scores[4], warmup_time_ms;
    float last_flag_capture_ms;
    int32_t last_capture_team, red_status, blue_status, neutral_status;
    int32_t red_taken_ms, blue_taken_ms;
    int32_t red_obelisk_attacked_ms, blue_obelisk_attacked_ms;
    bool initialized;
    qa_actor_id neutral_obelisk;
};
qa_q3_source_team_state_read/write(game, ..., error);
```

Actual native GAME team state is independent of selected mode teams/rules. Selected modes only receive qualified shared projections. The map owner now writes warmupTime0 before worldspawn CS effects and -1 only when cached doWarmup && !cached restarted; it reported this repair before handoff. Do not infer warmup from selected mode.phase or the live g_restarted value after its force-reset.

Actual GAME match holder:

```c
qa_q3_source_match_state {
    int32_t intermission_time_ms, intermission_queued_ms, exit_time_ms;
    uint64_t warmup_modification_count;
    qa_string_id changemap;
    bool ready_to_exit, restarted;
    qa_vec3 intermission_origin, intermission_angles;
};
qa_q3_source_match_state_read/write(game, ..., error);
```

`warmup_modification_count` preserves the full copied settings metadata uint64; it is not truncated to signed32. `changemap` is nullable source string identity, cleared by genuine ExitLevel. Core validation checks finite team capture float/poses, real source string and valid full-generation neutral reference. The match helper must own actual producers/order, not duplicate these fields locally.

Core intermission helpers are concrete:

- `qa_q3_client_move_intermission(game, actor, origin, angles, error)` writes actual body position/view, native PM_INTERMISSION5, PSflags/powerups0, source S.type/model/loop/event/flags0, authored S.origin/angles, contents0 and flags-cleared projection. It assigns VIEW only; it does NOT recompute delta angles or call SetClientViewAngle. It retains source lifetime across callbacks.
- `qa_q3_client_connecting(game, slot, error)` writes true fixed pers.connection CONNECTING.
- `qa_q3_client_score_reset(game, slot, error)` currently clears active copied PERS_SCORE. The app resets actual selected score via real mode setter separately. A genuine retired fixed score cell is a next10 dependency, not present in9.

## Genuine FOLLOW state and preserve-authority rule

The native TS oracle is `content/q3/base/shared/player-state.ts:173-223` with `copyFrom(..., "preserve-authority")`. It copies actual native source PS scalars/arrays but preserves the follower's actual shared origin/velocity/health/armor/weapons/ammo. Earlier whole-stored-body wording was explicitly corrected by root. Do not alias the followed target at read time or preserve a target body/inventory snapshot as authority.

Native client `followed_player` + `has_followed_player` are genuine source copied PS continuation. Copy input validates finite source vectors/product and genuine holdable/persistent source item indices. The producer clears unused copied canonical origin/velocity, health/armor/weapons stat words, all ammo; retains actual source scalars/arrays; preserves follower vote flags **0x84000** (EF_VOTED0x4000 | EF_TEAMVOTED0x80000), ORs FOLLOW4096, and writes actual PM policy/source selected holder.

Actual copy also authors existing source PS fields: command time, flags/clientNum/deltas/ground, weapon/phase/time, animations, grapple/view/height, event ring and external events, damage feedback, powerups/generic1/frame/jumppad, rank/PERS_TEAM, **PERS_KILLED8** (not slot7), spawnCount4/playerEvents5, maxHealth/deadYaw, ping and awards9..14, holdable/persistent. Native wire `q3_wire_client_follow_copy` authors the existing wire hits/attacker/attackeeArmor/ready/loop owners at the real copy phase; own special/mapped ammo remains untouched.

Public API:

```c
qa_q3_client_follow_read(game, slot, &raw_source_ps, &present, error);
qa_q3_client_follow_copy(game, actor, const qa_q3_player *, error);
qa_q3_client_follow_clear(game, slot, error);
qa_q3_client_follow_scoreboard(game, actor, bool_enabled, error);
```

Wire source PS read first checks authentic copied-source presence, then merges actual FOLLOWER canonical body/combat/weapons/ammo. It does not reread target. Corresponding true PM, ready, loop, feedback PERS, external events and BG dequeue writes go through internal `q3_client_follow_player(game, slot)` optional pointer. Core actual tokens/flag powers/award visuals/counters/predictable ring/command completion/rank/persistentTeam/jumppad writes update copied source domains when active. Source special ammo remains own actual ammo authority, never copied target ammo.

StopFollowing mutates genuine PERS_TEAM3, session state, PM_FOLLOW/BOT/clientNum. It retains other copied source scalars until actual Connect/Begin/Spawn reset; it must not erase a full copied PS merely because FOLLOW bit clears. SCOREBOARD uses actual masked PM flag writer and updates active source copy.

Saved active FOLLOW backing validation separately requires canonical-unused words allzero; input live target validation still accepts actual nonzero body/inventory before producer clears them. Peer should inspect active/inactive saved backing and every subsequent source setter for stale copied scalar domains. Core9 is not independently accepted yet.

## Genuine ClientThink core phase APIs

These functions are present in core9 and consumed by the separate native control owner:

```c
qa_q3_client_reward_expire(game, actor, error);
qa_q3_client_think_prepare(game, actor, const_cmd, &old_event_sequence, &effective_cmd, error);
qa_q3_client_movement_complete(game, actor, command_time_ms, viewangles,
                              viewheight, actual_movement_ground, error);
qa_q3_client_movement_water(game, actor, waterlevel, watertype, error);
qa_q3_client_movement_water_read(game, actor, &waterlevel, &watertype, error);
qa_q3_client_think_event_time(game, actor, old_event_sequence, error);
qa_q3_client_fire_held_finish(game, actor, error);
qa_q3_client_events(game, actor, old_event_sequence, error);
qa_q3_client_spectator_origin(game, actor, error);
qa_q3_client_touch_policy(game, actor, &native, &touchable, &door_trigger, error);
qa_q3_client_jumppad_finish(game, actor, error);
qa_q3_client_think_finish(game, actor, admitted_msec, error);
qa_q3_client_think_complete(game, actor, final_command_time, error);
```

Exact source ordering is the native control owner's responsibility:

1. Actual command policy/intake/inactivity. Reward expiration precedes source PM type/gravity/speed reads/writes.
2. Pre-PMove source helper releases grapple on released attack, captures old event sequence before gauntlet, probes allowed real gauntlet, consumes actual entity FORCE_GESTURE into pers.cmd/effective buttons, performs native invulnerability expansion, and stores actual oldOrigin. Input/output aliasing is supported by copying cmd first.
3. Actual selected movement executes, even when a positive admitted source policy interval rounds final movement elapsed to0. Genuine selected-Q3 final commandTime is result.state.q3.command_time_ms; foreign selected movement uses accepted native cmd time. The earlier claim that raw accepted time always wins selectedQ3 was revoked by control owner after full donor read.
4. Core movement_complete writes native time/view/height/ground and actual selected COMMAND|VIEW holder. Physical ground maps world1022, bound actor source slot, absent1023.
5. First timestamp update, real BG publish/pending, then FireHeld reset only if actual PS.eFlags !FIRING. Bounds and true native water are assigned at the actual donor boundary after pending and before ClientEvents. Water conversion of legacy negative contents belongs control: -3→32, -4→16, -5→8, other negative0.
6. Genuine snapped currentOrigin scope begins before ClientEvents. ClientEvents reads the actual source ring with signed wrapped source semantics: oldSequence|0, oldest=(liveSequence-2)|0, signed clamp, loop against LIVE sequence re-read after callbacks, full-generation reacquisition. Event11/12 fall damage checks true S.type PLAYER and dmflags8; event23 allowed source FireWeapon; holdable25..29 actual source effects. Generic Pmove native effects defer these gameplay calls so they do not duplicate.
7. Source wire link reads current real scope origin; triggers/ordered impacts; second eventTime timestamp; jumppad reset only actual touch tail; finish removes scope without snapping body; buttons/respawn/timers at source finish.

`think_event_time` is timestamp-only. FireHeld is a separate API because invoking timestamp twice must not clear it at an unintended phase. Forced respawn threshold uses exact wrapped integer seconds*1000, not float conversion; source strict time comparison is preserved.

Selected Q3 movement backing has a genuine source writer `application_control_q3_source_state(provider, actor, sourcePlayer, fields, error)` implemented by movement owner and bound by providers. It writes durable selected control and the current borrowed kernel cell during Pmove, preventing source death/respawn/FOLLOW writes from being discarded when the kernel commits. It returns success for foreign movement without pretending that foreign state is Q3.

Masks in `game_q3.h`: COMMAND1, EVENTS2, FRAME4, JUMPPAD8, DELTAS16, VIEW32, ALL63. Actual native Connect/Begin/Spawn write ALL; command/movement completion writes named fields; jumppad tail writes JUMPPAD. Independent nonnative role spawn does not call this actual source hook before controls exist. Native kernel INPUT_BEGIN seeds true source command/frame/ring/jumppad/eFlags and INPUT_END commits actual kernel eFlags. Source predictable ring writes synchronize the borrowed kernel event counter at genuine effect phases.

## Actual transient r.currentOrigin scope

Donor `entity-shared.ts` defines a genuine temporary currentOrigin view. The scoped snapped origin is not a stored canonical PS body or renderer cache. A real r setter writes canonical body and updates the active view. Scope finalization restores the prior view by removing overlay; it does not replay a canonical body write or snap PS.

Core private storage `q3_current_origin current_origins[64]` contains full actor, active origin, active bool. Public begin requires actual native source command/full actor and rejects a nested active scope. Full-generation actor release clears the exact scope. Capture/restore/destructor reject active scope. Portable source reconnect requires idle scopes.

```c
qa_q3_client_current_origin(game, actor, snapped_origin, error); /* begin scope */
qa_q3_client_current_origin_finish(game, actor, error);          /* remove scope */
qa_q3_source_current_origin_read(game, actor, &origin, error);
q3_source_body_read(game, actor, &body, error);                  /* private true r view */
q3_source_origin_written(game, actor, new_actual_origin);       /* actual setter bridge */
q3_source_origins_idle(game);
```

`q3_source_body_read` reads the actual canonical body, retains source owner during callback, requalifies exact full actor/physical binding, then overlays active source currentOrigin only. Wire link/body read and actual weapon attack geometry read this true source view; wire PS authority reads continue direct precise canonical body. Dynamic teleport/holdable reads were migrated by their owner and actual teleport body setter calls source_origin_written, so a teleport inside ClientEvents changes the source origin used by later link/triggers.

Control owns a started flag and cleans only a scope it started; it preserves primary errors on cleanup. If original actor retires during events/triggers, it stops live work and release has already invalidated the source scope; a reused row never inherits the transient origin.

## Damage, initial death and selected character separation

Real committed feedback calls wire damage producer before feedback accumulation/reaction, using outcome mutation.before health/armor. No event-history reconstruction. Native q3_damage null attacker/inflictor substitutes actual world actor1022; unbound foreign attacker remains distinct from a native world/source owner.

Core private helpers are concrete:

```c
q3_source_initial_death(game, const qa_damage_outcome *, bool *admitted, error);
q3_source_death_effects(game, const qa_damage_outcome *, error);
```

Initial native death admits only genuine physical client, real DEATH outcome, no stale outcome, real native match intermission_time==0 and actual source PM !=DEAD3. It releases hook, checks true source S.eFlags TICKING (not inferred PS bit), schedules actual attached mine PROX_DISCARD, writes true native PM_DEAD before source ranking/effects, and records an exact transient admitted-outcome token only when selected Q3 CHARACTER needs its later body reaction. It no longer uses player.dead as native source admission cache or writes a foreign-character duplicate dead flag.

Once admitted, actual `qa_q3_ranking_death` precedes source Kill log/obituary/rewards/items/flag clear. Selected character owns body bounds/collision/animation/health reaction. The exact transient token is keyed by full actor, attack sequence/time/provider so the selected native character can finish its body reaction once without redoing native GAME rewards. Actual source rank/death effects also run for foreign selected CHARACTER through the common committed outcome route. Do not fabricate damage/outcomes to force this path.

Native source death cleanup preserves native PS weapon/loop until actual donor writes; dynamic source cleanup already clears powerups and flags projection. Core does not repeat those PS clears. It authors source S.weapon/powerups/loop0 and real respawnTime. Native gib authors G_AddEvent64, S.type INVISIBLE10, contents0/takedamagefalse rather than synthetic NODRAW+unlink; native corpse gib was similarly repaired in core9. Nonnative selected-Q3 legacy remains separate.

Corpse state has actual `physics_object` instead of using S.eFlags NODRAW to mean physics disabled. Eight initial bodies have flags0/physicsfalse. Actual copy uses `q3_wire_copy_body` to copy published player S/apos and snapped S.pos into existing corpse trajectory/current body; source flags/animations/events/powerups/loop mutations happen at true copy phase. Stationary sink mutates retained pos.base without invented body/link updates; final sink clears physicsObject/unlinks without adding NODRAW. Native invisible/gibbed source copy still advances the queue; native noDrop tests source authored origin.

## Item/objective/Obelisk contracts

Objective source hooks in core9:

```c
objective_dropped(context, actual_item, item_index, error);
objective_admitted(context, actual_item, item_index, bool_finished, error);
objective_expired(context, actual_item, item_index, error);
objective_nodrop(context, actual_item, item_index, error);
source_flags_cleared(context, actual_player, error);
```

Hooks adopt existing actual G_Spawn actor; they must not allocate a second objective, replay body placement, or turn pending200ms items into finished items. Prepared source item getter returns true SpawnItem metadata and exact NOT_FOUND for a genuine no-item row. Map SpawnItem sends admitted(false), Finish sends admitted(true). Dynamic Team_CheckDroppedItem uses the same launched actor. NoDrop has separate real message behavior from timed dropped expiry.

True Harvester cube toss `qa_q3_client_toss_cube(game, actor, source_team, timeout_seconds, &out, error)` clears actual generic1 FIRST; checks real G_EntitiesFree; picks actual cube item; uses ENTRY%360 and separately sequenced GAME RNG; then reads actual native neutral Obelisk **S.pos.base +44** (no neutral means exact ZERO without+44); true LaunchItem; writes actual common actor.spawnflags=source team and exact wrapped timeout. Do not use persistent item generic1/team_restriction for cube spawnflags, pass a mode neutral-origin approximation, or run duplicate mode physics/timers.

Public source drop/respawn/adopt/read helpers are declared in `game_q3.h` and defined by dynamic item/death owner. Actual dropped-item source angles/origin use published S.apos/base and S.pos/base. Actual flags clear callback is invoked after raw PS write, not before; it only clears shared carrier inventory/member projection, not source flag spawn/reset.

Actual source award API `qa_q3_client_award(game, actor, qa_q3_source_award, int32_delta, error)` writes real player PERS counters and active copied source PS. Source award enums are actual PERS indices impressive9/excellent10/defend11/assist12/gauntlet13/capture14. CmdGive uses this helper rather than mode stats. `qa_q3_source_award_visual` changes actual award flags/reward time separately. TEAM helpers must preserve visual/PERS ordering.

`qa_q3_source_score_plum` allocates real TEMP65 before actual score mutation, targets true source client via SINGLECLIENT, writes otherEntityNum/time score. TEAM sound allocates true TEMP47/BROADCAST32. Team gesture scans actual fixed source client order and writes true gentity FORCE_GESTURE. These are not event-report proxies.

Obelisk core9 contract:

- Kind `Q3_ACTOR_OBELISK` appended after TEMPORARY.
- Public projected `qa_q3_obelisk_state { model; int32 team,next_think_ms; think; }`.
- Actual private union continuation `qa_q3_obelisk_continuation { model; next_think_ms; think; }`; no duplicated team. Actual common `q3_actor.spawnflags` is sole raw team owner. Actual shared combat owns health and takedamage; no duplicate damageable flag.
- Think enum `QA_Q3_OBELISK_NONE/REGEN/RESPAWN`. NONE is genuine Harvester touch path; Overload admission applies only REGEN/RESPAWN. Neutral Harvester actual trigger.model remains null; red/blue triggers retain their actual model actor.
- Core actor dispatch calls `q3_obelisk_step`; source touch owner calls genuine Obelisk touch. Actual source reaction is `qa_q3_source_obelisk_reaction(game, outcome, &handled, error)` through common selected source reaction, not an extra before_reaction duplicate.
- `q3_obelisk_reconnect` rebinds actual per-trigger combat admission at final pure portable restore binding phase after actual canonical combat exists. Core save.c currently loops these actors before final bindings_current and clearing source_restored.
- Settings hook returns actual cached `qa_q3_obelisk_settings {health,regen_period_seconds,regen_amount,respawn_delay_seconds}` at real producer; no shadow settings holder.
- Hooks admitted/touch/pain/die are typed. Die stages are `QA_Q3_OBELISK_DIE_TEAM_SCORE`, then source disables real takedamage/sets RESPawn/next/model255/frame2/G_AddEvent69, then `...PLAYER_SCORE`, then attackedTimes0. Pain source model ratio/event70/frame1 precedes app AddScore(max1,truncdamage/10).
- Wire owner added explicit raw GENERAL Obelisk passthrough after producer4 release; obtain that new file hash/peer status. Native authored POINT ET_TEAM model presentation remains an explicit view integration concern; wire correctness alone does not prove presentation completion.

## Native END effects

Core declaration `qa_q3_client_end_prepare(game, actor, waterlevel, watertype, bool *publish, error)` has its actual definition in dynamic-owned feedback.c, outside core25. Owner reported it landed just before this handoff.

It qualifies actual native sess spectator policy; expires powerups first; true native match intermission suppresses later effects/publication; uses `qa_q3_client_world_effects` without re-running timers or selected-effects gates; actual source PM_DEAD guards damage feedback; updates true S EF_CONNECTION then SetClientSound; leaves source invulnerability-expanded state alone; returns publish=true only for current same full native binding. The separate app END wrapper performs final BG/pending and spectator resolution. Verify precise donor order and copied source PS writes in whole peer review.

END must read true GAME water through `qa_q3_client_movement_water_read`, not reconstruct from selected controls after a source callback has mutated native water. The END worker was notified to reopen/refreeze its earlier END2 packet for this migration; status was not independently verified here.

Source sounds retain actual integer loop index in wire client state. Presentation qa_string_id loop resource is separate. True fry index is registered at source Init and SetClientSound writes retained integer; observation must not register sounds or resolve a path afresh to a changed CS index. Actual G_AddEvent differs from predictable source ring; Pain/Battlesuit/world/regeneration producers were migrated accordingly by core/dynamic owners.

## Shader/configstring and map location continuation

Source inline shader registry is actual GAME state: fixed128 rows, source path bytes64 and source float time. Level reset clears it; real target use remap runs before target lookup through `.remap_shader=q3_shader_remap_target`, and source target serial/fullgeneration qualifies continuation. Portable target binding equality now includes remap callback. Typed capture/prepare/commit/private shader codec hooks are installed in core9. Restore does not replay remaps/configstrings.

Source configstrings retain presence via allocated string even for empty text. Missing→present-empty is a real mutation and not skipped; capture/prepare/private codec preserve empty rows. Native setter commits source slot before external notification; callback failure leaves mutation committed and faults actual caller, not a falsely atomic rollback.

Per-slot transient uint64 `configstring_revisions[1024]` increments before authored write/notification, preflights overflow, and suppresses retired outer map-event publication if nested callback supersedes same slot. This catches A→B→A, which equality alone cannot. Public `qa_q3_configstring_revision(game,index,&revision,error)` is consumed by native wire before/after every send, including single/final chunks. Revisions are lease metadata, not serialized gameplay; idle level reset/restore resets them. Do not serialize or replay mutation notices on restore.

Model/sound registration uses actual CS model start32/sound start288 table owner. The pure native wire sound observer reads retained integer source index; it does not register a resource.

Map checkpoint/private map codec is version5. Six actual source sound integers are retained and validated0..255: noiseIndex, soundLoop, and mover start/end sound slots. Distinct `noise_index` and `sound_loop` must not be conflated. Source wait_ms is native float, private codec f32/finite. Blocked callback enum is true NONE/DOOR on mover definition; only genuine door/plat constructors author DOOR, other classes do not gain Blocked_Door from classname guesses.

Map thinker due dispatch rounds retained signed32 due to float first, then compares that float against actual integer native now. It does not use wrapped integer subtraction. Retained callback enum with due0 is valid because G_RunThink clears schedule before callback and many source callbacks retain the function.

Map `location_head` and each actual location `path_next` form true source target_location nextTrain chain. Physical ascending location link scan prepends real full actors; source health is location id. Checkpoint validates live chain kind/fullgeneration/cycles before commit. Actual retarget setter updates native mover.target and item.spawn.target as well as prepared map metadata.

`qa_q3_map_team_location_read(game, sourceclient, &qa_q3_map_team_location, &found,error)` and `qa_q3_map_location_count_set` are core9. Query has real source currentOrigin, source head walk, finite201326592 squared bound, equal ties replace, actual collision PVS+areas. Empty head returns false before body/geometry; source leaf is lazy until a distance-qualified candidate. It retains game and requalifies full source client/location after body/collision queries, then requalifies and rereads final selected result so a later callback cannot publish an earlier retired location. It does not add donor-invented in_use/connected filters to ChatLocation. Count clamping is an actual ChatLocation write, not a query normalization.

## Typed/private codec closure and review checklist

- Typed native checkpoint version9; portable native signature QAQ3SAVE version9; map version5.
- Explicit source scalar/private codecs cover native fixed sess7, client buttons/inactivity/oldOrigin/ready/copiedPS, dense64 source client actors/PS, pool1024 bindings/source count, actual newSession/fry index/portal sequence/location timer/counts/TEAM/match, physicsObject/portal state, mover float wait/blocked callback, Obelisk model/think/deadline, map sound integers/location head, empty configstrings and shader registry.
- Raw source PS fields are encoded individually, not raw host structs. Source usercmd carries all actual native words; source angle/event/counter int32 patterns preserve wrapped bits.
- Nested wire state is `qa_buffer wire_state` in checkpoint. Capture/free use real wire owner. Restore prepares nested wire/configstrings/shader/physical rows and validates candidate against saved physical bindings and actual LINKED restored world link_count/cache before any source row commit; wire commit is nonfallible.
- Actual app save restores foundation/world first, then source owner. Empty save candidate map binding avoids constructor replay. App outer native bundle was reported QAN3v2/schema3/source5 accepted; its final immutable provider recapture depends on exact current core private version.
- Fixed wire64 retained S includes disconnected/unbound fields; dynamic active rows require actual constructor readiness. Linked caches must qualify real published link count/geometry; unlinked rows may retain previous cache but it is not current linked membership.
- Full source read/reconnect qualification is required beyond helper acceptance. Core9 is still unreviewed. In particular inspect every declaration's actual definition/caller, genuine clock/frame, mutable copiedPS domains, source/effects selected-character boundaries, obelisk reconnect, candidate precommit validation, and callback fullgeneration requalification.
- No source peer acceptance implies build/run parity. Runtime source gate remains closed until user/root explicitly reopens it.

## Next10 — planned dependencies, NOT present in frozen9

Root explicitly ordered stable core9 review first and held postgame/fixed client/retired score/TEAM telemetry for a separate version10 wave. Several independent owners have drafted new TUs against these proposed types. Their declarations/calls do not create core backing. Do not compile or claim completion merely because new files exist.

### Genuine gentity.client identity and fixed pointer lifetime

Donor verified by wire/bots owners:

- `content/q3/base/game/entities.ts:92`: constructor installs actual entity.client only index<options.maxClients.
- `activateClient` reinstalls actual pointer.
- `pool.initialize` resets all64 GameClients but assigns entity client pointers only index<maxClients.
- `records.attach` installs actual pointer for player records.
- `records.deactivateClient:175-184` leaves entity.client pointer while setting entity actor=null/in_use=false.
- True entity release clears metadata/pointer.

Current9 all64 source actor kind PLAYER does NOT establish this pointer and must never be used as has_player=true. Physical source slot, borrowed gentity.client pointer identity, PS.clientNum, canonical actor generation and presence are distinct.

Wire owner's proposed next10 actual binding field is `int32_t client_slot` with -1=null,0..63 fixed gclient pointer, also usable on dynamic victory models that borrow a fixed gclient. This exact field is only a proposal pending root/new-session agreement; it is not in core9. Constructors/reset/bind/activate/deactivate/free and private/typed codecs must all author/preserve it at real source phases. World/body/item/model rows default null unless genuine producer attaches a client. Dynamic borrowed aliases must not count as new fixed clients in ranks/connection loops.

Bot source_chat needs a genuine fixed read returning actual `{present,has_player,pm_type,score,last_hurt_client,last_hurt_mod}`. Rank chat requires present&&pointer. Visible-enemy dead test reads pointer+PM even absent source actor and treats PM!=NORMAL0 as dead. Hit chat requires pointer and actual retained hurt fields without requiring presence. Existing `qa_q3_client_taunt_read` supplies retained hurt fields; full wire PS read rejects retired/no-body actors and is not a substitute for fixed pointer/PM/score. No final public source accessor name was agreed before stop; bots service type is `qa_bot_source_player_state`, adapter owned by bots_resume.

### Actual source deactivation versus canonical retirement

Current9 `qa_q3_client_disconnect` writes in_usefalse/model0/disconnected pers and detaches PM, but retains source_entities[slot].actor until registry release. Donor deactivateClient nulls source actor immediately even when borrowed canonical actor remains live. This is a concrete next10 lifetime boundary, not solved by claiming canonical retirement already occurred.

New source fixed body observation must return genuine ZERO_BODY when actual source actor is absent, including at real disconnect deactivation, while retaining client pointer/PS as donor does. Author an actual deactivated/body-attached/source actor state at its producer or clear genuine source binding at the true phase and migrate callers safely. Do not infer source actor absence from PMdetached, in_usefalse, canonical owner, or eventual registry release. Coordinate app wire_disconnect/native binding callers that still need the captured physical slot during teardown.

### Retained PM and score without duplicate canonical authority

Wire actual foreign policy backing already exists and `qa_q3_wire_client_detach` transfers real selected PM scalars to fixed native holder before controls retire. Live selected Q3 controls remain sole PM authority until genuine handoff. Fixed constructor PM0 is actual GameClient constructor state, not guessed observer defaults.

Proposed score backing is `qa_q3_native_client.retired_score` int32, constructor0, captured at real detach/deactivation before canonical selected score disappears, used only after actual owner handoff. Live source score should use genuine existing selected source score capability; active copied FOLLOW PERS_SCORE is authentic source copied state. No score0 fallback when required owner is unavailable. Exact root decision on unified live/retired/source score authority is still needed, especially borrowed PS aliases and retained inactive sorted tails.

Wire proposed `source_pm_read/score_read` pure fixed accessors and a narrow private retired score read/write (names not final). `qa_q3_client_score_reset` must clear true retained score cell and active copied PERS_SCORE at genuine Exit, plus actual selected score owner. No mode-member or event-history reconstruction for retired state.

### Fixed source body/model metadata

Do not duplicate live canonical body. Actorless source row body is donor ZERO_BODY. Some source r metadata (actual model shape/contents/previous link) survives independently and must be captured only at real setter/retirement phases if required.

Wire proposes narrow `qa_q3_wire_client_body { qa_body_state current; qa_shape_kind model_shape; }` for podium consumers, instead of broad `qa_q3_wire_body` that would invent last_link/collision fields. Actual source model metadata may need BOX/CAPSULE/INLINE plus inlineindex/contents; do not save a stale collision.owner actor as a second authority. No final model field type or getter name was settled before stop.

### Genuine pers.teamState telemetry agreement

Agreed type/member for next10: `qa_q3_source_player_team_state` in `qa_q3_native_client.team`. Existing `team_state` state integer and `team_location` are separate actual fields; avoid duplicate state when deciding a unified layout.

Exact agreed telemetry fields:

```c
int32_t captures, base_defense, carrier_defense, flag_recovery, frag_carrier, assists;
float last_hurt_carrier_ms, last_returned_flag_ms, flag_since_ms, last_fragged_carrier_ms;
```

All counter signed32 patterns are valid (wrapping source increments, no nonnegative validator). Timestamp fields are finite binary32 and may be negative, including donor -5 after opposite-team capture. Constructor/ClientConnect resets whole pers/team telemetry to0; ordinary ClientSpawn preserves pers telemetry and only changes BEGIN/ACTIVE state at actual phase.

Agreed API names:

```c
qa_q3_client_team_state_read(game, uint32_t slot, &state, error);
qa_q3_client_team_state_write(game, uint32_t slot, const state *, error);
```

Actor consumers resolve genuine native slot. APIs are planned, not defined in9; note existing singular `qa_q3_client_team_state(game, actor, int32_state,error)` setter already exists and must coexist/migrate deliberately. Owner `bot_orders_owner` had made no NEW source_team_combat.c/.h edits when gate closed. It will own actual Team_CheckHurtCarrier/Team_FragBonuses and read original g_local.h199-215/TS PlayerTeamState37-52, without a mode telemetry mirror.

### Postgame/podium/victory borrowed-client requirements

Independent postgame four-file producer exists in NEW `application/native_q3_postgame.c/.h` and NEW `gameplay/q3/source_postgame.c/.h`; latest owner reported bounded independent acceptance of its manifest only. Missing core10 callers/types are not accepted by that review. Read `/tmp/qa-handoff-native_q3_votes_owner-20260930.md` if it exists and verify exact current manifest.

Requested source kinds: `Q3_ACTOR_PODIUM`, `Q3_ACTOR_VICTORY_MODEL` appended, with actual union `.state.postgame`:

```c
qa_q3_entity entity;
int32_t source_client, timestamp, nextthink, think, count;
float physics_bounce;
bool physics_object;
```

Requested actual level full-generation bindings `qa_actor_id podium_players[3]`, reset at true source Init/spawn reset, preserved/remapped by typed/private continuation.

source_client=-1 for podium,0..63 real fixed borrowed gclient for model; it must agree with genuine gentity.client identity rather than duplicate an independent client authority. Source thinker values from NEW private header: NONE0, PLACEMENT1, CELEBRATE_START2, CELEBRATE_STOP3.

New source definitions on disk (not supported by9 kinds yet):

```c
qa_q3_source_postgame_client_state(game, fixed_slot, &qa_q3_player_state, error);
qa_q3_source_spawn_victory_pads(game, error);
qa_q3_source_abort_podium(game, error);
q3_postgame_step(game, actor, error);
```

Requested hook `bool (*postgame_cvar_integer)(void*,const char*,int32_t*,qa_error*)` uses actual live engine g_podiumDist/g_podiumDrop trap at genuine producer. This differs from copied module settings and must not be replaced by a cached guessed default.

Actual postgame S snapshot is retained full `qa_q3_entity`, not classification synthesized during read. Choose one real full S owner; wire must consume it and avoid a duplicate mutable source_wire S authority. Link cache/r metadata remain separate true source owners. G_AddEvent on a victory model must author borrowed original fixed gclient PS, including disconnected/absent source actor, not model-local event shadow. Original sorted client tail reads fixed64 even when fewer than2 nonSpectators; source pointer/retired score state is required.

Victory model classname borrows current fixed client pers.netname. Userinfo changes must be observed through actual client alias, not a frozen canonical definition or copied classname. Existing source binding string-id read may need a genuine pure nullable classname reader or producer-owned immutable netname identity; no observation-time string registration/default model name. Preserve present-empty versus absent semantics.

Actual model physics has clip mask0x10001 (SOLID|PLAYERCLIP); podium1. Generic nonPLAYER physics_read currently mask1 and needs a real source model eligibility/mask branch. NEW q3_postgame_step already implements actual physicsObject G_RunItem path for displaced models; shared native mover bridge remains open.

Critical mover alias behavior: donor G_MoverPush reads/rotates/saves **borrowed original client.ps.origin**, mutates model S.pos.base AND actual borrowed client PSorigin/deltaYaw, then sets model currentOrigin from that borrowed PS. Marking model NATIVE_PLAYER with its own canonical body is incorrect. Add a genuine borrowed-client read/write capability/adapter, keeping original player body/PS and model body distinct; do not create a second canonical client.

Source view/presentation must expose real podium model and victory copied animation/weapon/client appearance. Current `qa_q3_entity_read` supports typed existing entities/mapMover but POINT ET_TEAM and new postgame need explicit genuine handling. Wire full S acceptance does not prove renderer or mover behavior.

### Next10 codec/lifecycle obligations

- Bump typed/private source version coherently beyond9 when actual new fields/kinds land. Root explicitly suggested version10; map version changes only if map schema changes again.
- Encode actual gentity.client identity/null state, body detach/retirement metadata, real retired score/PM handoff state, raw TEAM telemetry, full postgame entity/think/timer/physics state, podium actor references, aliased client semantics and any producer-owned model/classname metadata. Remap only genuine full actor/string refs, not fixed client slot identity.
- Validate source null sentinel -1 versus valid slot0, fixed64 bounds/real attached-pointer lifecycle, active raw postgame kind and aliases, finite f32 fields, enum thinkers, full actor refs, genuine source cache/world link generations and unused duplicated canonical words. Do not demand live actors for actual retained disconnected client state.
- Reset only actual source constructor/Init domains; ClientSpawn preserves pers telemetry; deactivate retains client pointer/PS; true free clears pointer/dynamic row fields. Save import remains empty candidate/no constructor or event replay.
- Add actual new kind dispatch in game actor frame, touch/combat/view/wire/mover/physics lanes and pure reconnect before claiming producer completion. Header enum additions alone are incomplete.
- Integrate actual app postgame/match effects and true fixed bot source state callbacks; remove obsolete mode source backing in same wave. Do not add hidden Q3 mode state to support independently selected rules.

## Resume sequence

1. Read root's handoff and all relevant worker handoffs; confirm source gate/user authorization and exact live manifests. No automatic executable validation from this handoff.
2. Recheck frozen core25 hashes. Assign an independent whole-source reader (none was assigned at stop). Include actual public/private contracts, ordinary producers, native mixed selections, callback lifecycle and saved continuation, plus independently owned external dependencies.

4. Only after core9 release, implement a bounded core10 state unit with wire/bot/postgame/TEAM owners using genuine field ownership above. Freeze/review it independently. Keep missing runtime verification and remaining game parity explicit.


