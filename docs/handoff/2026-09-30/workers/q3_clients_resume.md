# Native Q3 clients, commands, and modes handoff

Snapshot date: 2026-09-30. This document covers the work owned by `/root/q3_clients_resume` and the dependencies needed to finish it. It is a source handoff, not a claim of complete Q3 behavior or complete project parity.

## Current disposition

The exact 20-file `current5` packet is frozen. Its manifest is `/tmp/qa-native-q3-clients-owned-current5-20260930.sha256`. A final `sha256sum -c` returned `OK` for all 20 files. A trailing-whitespace search over all 20 returned no matches. A source search over `src` and `include` returned no old `qa_modes_q3_session`, `qa_mode_q3_session`, `QA_MODE_Q3_SESSION_`, `fixedq3_clients`, or `q3_team.` uses.

**The complete 20-file packet has not received independent whole-file source acceptance.** The parent requested a handoff before that review. Do not label it accepted because neighboring source/session/vote/wire packets received their own reviews. No compiler, build, test, script, parser, game, or executable validation was run for this work. Source reading, editing, hashes, and whitespace checks were the verification boundary. Hold the exact files until the parent releases the packet for fixes or assigns its review.

Several genuine producers now exist in neighboring work, but their full caller integration is still being reviewed. The most substantial remaining dependencies are real pre-Connect bot admission, selected guest Q3 equipment capabilities, true native TEAM per-client state, complete native Match/Think/EndFrame settlement, and the independent review of this packet.

## All 20 owned files and hashes

```text
59086a13b81a47b0f7b1b44f3bacc045729afac3fd2f83cf386e23bc44a649ea  src/app/application/native_q3_clients.c
b016e2e65387a653303904f5194377dd631877bf8e5c3b5a2ba6d635d136d6b9  src/app/application/native_q3_clients.h
78ce885590342d56627fd324c4c5d68a01b8894c95a09d1bcb72ff8395e4b6bf  src/app/application/native_q3_chat.c
2b798e38a0a4ec79e42c8b3a0cd7ab47ef8998bec8bed6ac65f106fe2de1e782  src/app/application/native_q3_chat.h
b873ab2dec49318b077c1adba9b812e749e7792041c4ec534f5a9b2a28a6d0a9  src/app/application/commands.c
1e2ec5d1ff8aa424f643a4eff4efe5d59b5b1e28150e859f3ed386471df948f7  src/app/application/match.c
9d313efde3ea0103a5eba841a2bc61b461c33ea9977ca71dd7f2a32f2dbf8bf7  src/gameplay/q3/console.c
b400cefa0bc83810867ccaf5522036735e66860a170b5c1b2652a397ee253bae  src/gameplay/modes/q3_session.c
9f032b8edef3fc19afc1cb3420d5f0a065d3dfd4c2de99d338cacd8dc243a605  src/gameplay/modes/teams.c
186d3e1021a846ef72c4e53171e7365d009ecf3ee68420f8a690dcd03cfac543  src/gameplay/modes/commands.c
ef24da979e3ee1faf657e6cf5988e382b70ba158198dcd19dfa3bb80d2eccbb4  src/gameplay/modes/vote.c
d0933bb4cc8cf5d6c1fa05cb6d237f02f23c2df6753be6a06a4f2b33aa4e3e36  src/gameplay/modes/flags.c
97b616739d9a8100627c7120351e28f41ec73e7e27272c9ebf650014bbb4fbaa  src/gameplay/modes/arena.c
6585b66a0f4b50e153ed7d48c7146ee66ebf184f553ad8592a6abb4a72e29373  src/gameplay/modes/internal.h
c8221f2b0b4843e278a1827bf032a138f45fe3d5f2019a331776dba3b001464b  src/gameplay/modes/checkpoint.c
573674c86371f6181b8c7234065c10f95f697ade618fef0e5ff8cdf9ed54b842  src/gameplay/modes/save.c
3da1ccaf87233e9fc521c5e3ce0a1076ef47a9adb54c992aec43a82e1ded26d8  include/qa/modes.h
2c899f9cccc48be729182ecd48b09ff33c4ce3e2a3d2676cf350be1846586e26  include/qa/modes_save.h
b429dda699ba866c2d41b8e6d534a7bdfe44d5b231956da6a8c70a1e8b8f0e30  include/qa/modes_q3_session.h
8c908df75b6fdc755b89cc38e48049a7ced2991631038f10b422cf8d145376d7  include/qa/modes_q3_clients.h
```

Ownership boundaries matter because other workers are still editing adjacent code:

- `src/gameplay/modes/core.c` and `src/gameplay/modes/match.c` were transferred to `/root/native_q3_log_owner`. Do not edit them under the old client lane. The handoff hashes, before the new Match implementation, were respectively `4891041fa4782edb31ebf19ad278ebe6b1e28c7139c6caad6bc7820fe8b96f4b` and `d47660b0746f00b2abc81a42db81e23d47480257690dc7f1450541ca3666dbce`.
- `src/gameplay/modes/team_info.c` was transferred to `/root/bot_team_policy_owner`.
- The temporary, unregistered mode-side `src/gameplay/modes/q3_client_items.c/.h` were deleted. True TossClientCubes now belongs to `src/gameplay/q3/client_items.c` and the GAME source owner. Do not revive the mode-side producer.
- GAME source records, Q3 public client/player headers, pool/client backing, and their source codec have a single writer: `/root/q3_source_resume`.
- The true wire holder belongs to `/root/q3_wire_resume`; movement/kernel/session command ownership belongs to `/root/movement_resume`; real ClientThink orchestration belongs to `/root/tools_source_review` in new `native_q3_control.c/.h`.

## Architecture that must survive continuation

The actual native Q3 `QA_ROLE_ENTITIES` GAME owns its fixed physical 64 client records, source `pers`, seven raw `sess` integers, native PS fields, retained CalculateRanks counts/sorted array/follow slots, TEAM level state, match continuation, and `generic1`. It does so independently of the selected primary scoring/rule mode. A canonical actor may have a Q1 or Q2 character owner while occupying a genuine Q3 physical client slot. Never qualify the native source client by `actor.owner == GAME owner`, by the selected character owner, or by a guessed namespace slot.

The selected mode retains its genuine chosen scores, teams/member contract, rules, and effects. No hidden Q3 rule mode is created for a Q3 world with foreign selected rules. Native source state is not copied into mode-owned authoritative session storage.

The parent settled the rule-pass distinction explicitly:

- Actual native Q3 GAME plus a genuine chosen native Q3/Team Arena rule provider uses the real native source control pass. The chosen native rule provider may differ from the ENTITIES GAME provider.
- Q1/Q2 GAME with chosen native Q3 rules keeps the real generic chosen-rule pass.
- Chosen guest Q3 rules keep their actual guest pass. Do not run hidden native rule control for a guest rule provider.
- Q3 GAME with foreign chosen Q1/Q2 rules retains the foreign rule pass. Explicit GAME commands, clients, TEAM state, and source observations still use actual Q3 GAME authority; they do not impose foreign frag limits, score resets, or readiness policy.

`application_native_q3_mode_source_provider(qa_application *, qa_mode_id)` is the newly exported pure resolver. For the exact primary chosen native Q3 rule mode, it resolves native ENTITIES GAME when present; otherwise it returns the genuine chosen provider. All ten owned Q3 mode adapters use it. `application_native_q3_mode_source` additionally proves primary mode identity, selected Q3 rule source, native constructed GAME, and ENTITIES identity before declaring a native source pass. `/root/q3_round_resume` was asked to use this same resolver for native-mode clock/warmup ownership; the exported declaration and all owned callers are frozen in current5.

## Source sessions, selected modes, and persistence

The actual seven source session values are all signed 32-bit integers: `team`, `spectator_time_ms`, `spectator_state`, `spectator_client`, `wins`, `losses`, `team_leader`. They live on the GAME fixed client record as `qa_q3_client_session`, accessible by actor or physical slot. The source slot writer uses `QA_Q3_CLIENT_SESSION_*` masks and preserves raw wrapped values, including values that do not fit generic enum/bool domains. Native Connect resets these fields at the same genuine client reset as `pers`.

All native clients/chat readers were migrated to GAME session APIs. Old mode-owned sess7 APIs, types, fixed arrays, reconnect/restore/capture paths, and codec fields were removed. The remaining mode `q3_spectator_*` fields are chosen-rule member projection used by other rule backends; the public comment now states that distinction. `modes/q3_session.c` contains only the generic chosen-Q3 member initialize/team/follow, chosen Begin state reset, and generic retained-rule round reset. `modes_q3_session.h` retains the generic spectator enum and round reset; `modes_q3_clients.h` retains the chosen Begin declaration.

Unused mode `q3_clients_ready`, voting/non-spectator counter holders, and mode `q3_team` scalar/status/timer backing were removed. Actual source counts and TEAM state now belong to GAME. Generic selected-rule `q3_defend_count`, `q3_assist_count`, `q3_capture_count`, and four f32 TEAM statistics remain because other backends still use their genuine chosen-rule producers. Native PS awards use source GAME setters; native TEAM f32 runtime migration remains open below.

Current mode checkpoint version is **12**. The private signature is eight bytes `QAMODES\0`, with private schema version **10**. Capture, detached typed restore, private header, and typed/private field traversal were updated together. This is an intentional incompatible layout change; older mode checkpoint/private schemas are rejected.

Source-owned objective metadata is stored as `mode_object.q3_source_owned` and in typed/private object codecs. It must not retain generic physics or timer ownership. Detached validation rejects source-owned objects with generic `has_physics`, `next_ns`, or `expire_ns`; it no longer rejects solely because chosen rules are foreign. `source_objects_current` uses the genuine typed `q3_source_object` hook at the real capture/reconnect/restore boundaries to prove current source binding. Do not replace that with canonical owner equality.

Owned public hooks and actual app initializer bindings include:

- `q3_source_match_exit` -> `application_native_mode_q3_source_match_exit`.
- `q3_team_status_bound` -> `application_native_mode_q3_team_status_bound`.
- `q3_source_object` -> `application_native_q3_objective_bound`.
- `q3_source_object_view` -> `application_native_q3_objective_view`.
- Physical client slot/rank/count/team/vote/follow/readiness adapters -> actual client lane helpers.

The new pure source-object view hook takes `qa_mode_object_view *` as retained input/output; true Obelisk/model continuation supplies visible/frame/health/phase observations. `/root/native_q3_objectives_owner` owns the implementation and `objects.c` delegation. Its public field and app binding are included in current5.

Declared selected-mode effect APIs, implemented by the transferred modes owner, are `qa_modes_q3_source_phase`, `qa_modes_q3_source_reset_teams`, and `qa_modes_q3_source_frame`. They perform chosen-Q3 effects/ancillary updates while true GAME owns control timers. No hidden foreign-rule score reset is allowed. The legacy native readiness branch should be deleted only when the real native END/Match pass and its postgame/bot services are accepted; the Match owner explicitly warned against deleting it before that caller migration.

## Real native client lifecycle

`source()` proves constructed/attached/nonclosing actual ENTITIES native GAME, live application/full actor, selected-mode availability for shared effects, and a genuine source client slot. `slot_source` and `slot_current` support disconnected or never-authored fixed rows without inventing a canonical actor. All source callbacks that can publish output must requalify the original full actor or stable physical row afterwards.

### Connect

The source userinfo copy is bounded to 1023 bytes plus terminator. The real IP filter runs first. A banned result returns the exact nonfatal denial immediately, before password/settings/flag reads. Remaining admission uses actual source `SVF_BOT`, `ip=localhost`, and the copied `g_password` vmCvar; it preserves the source `none` password exemption. Accepted/denied is distinct from a source error.

Actual wire Connect precedes source pers/sess reset, then source CONNECTING/chosen roster projection, then `application_native_q3_session_client_connect`, LOG/UserinfoChanged, first-time reliable connected print, and genuine `application_native_q3_rank`. The physical bot allocator in `map_players.c` now sets source flag 8 before password checks. Local configured humans now receive actual `ip=localhost` from the producer. Bots use seat `UINT32_MAX`, not a local launch seat, in the wire holder.

**True G_BotConnect is still absent here.** The existing bot population/library setup occurs after source publication. The bot worker confirms that a callback returning accepted before real initialization would be false coverage. The genuine initialized population must exist before Connect, and its real admission/denial callback must run at the donor site after session setup and before LOG/UserinfoChanged. This requires round/bot producer work plus one narrow client caller edit after freeze release.

### UserinfoChanged

It uses real engine fixed-slot userinfo, including retained disconnected userdata and the constructor's genuine empty userdata on a never-authored slot. It rejects bad local info bytes into the source `\\name\\badinfo` buffer, applies the actual fixed-row source userinfo producer, and writes `CS_PLAYERS` at 544 + slot. It does not invent a sanitized engine SetUserinfo writeback. Bot model/team choice reads actual source BOT and copied gametype. Rename print and distinct LOG occur at source sites; physical-row/full-actor qualification follows output.

Signed source integer formatting uses the source AddInt behavior, including its unusual INT32_MIN byte output. This applies to team/max-health/wins/losses/task/leader config values, teamtask userinfo updates, and scoreboard integers. Do not replace it with libc decimal formatting just because libc seems more conventional; raw wrapped session parsing makes MIN reachable.

The slot form `application_native_q3_client_userinfo_changed_slot` is needed by source SetLeader even on disconnected rows. Native vote/session helpers have migrated to it and actual GAME session masks; they no longer require mode-owned sess7.

### Begin, Spawn, and Respawn

Begin unlinks the real shared body, performs genuine source ClientBegin PS/reset, resets chosen-Q3 Begin fields only when a genuine native chosen-Q3 rule pass exists, sets CONNECTED, and runs source Spawn. It creates genuine physical teleport-in event 42, prints entry where gametype permits, emits distinct Begin LOG, performs real wire Begin, and runs CalculateRanks.

Spawn reads actual source sess team. For spectator spawn it obtains the real Q3 intermission spawn point/target/RNG through `application_q3_find_intermission_pose`, rather than requiring the chosen generic member to be spectator. Ordinary source spawn still uses `application_q3_player_spawn_pose`. It uses actual retained raw engine usercmd, sets the genuine source begin command/spectator, calls `qa_q3_spawn_player`, clears readyToExit, clears the real selected-control body base via `application_control_body_reset`, applies selected movement mode, and initializes the source inactivity deadline/warning from ENTRY and cached `g_inactivity`.

A concrete command bug was corrected: keep a separate original received engine command **before** setting `pers.cmd.serverTime = ENTRY`, and pass that original command to genuine ClientThink. Example ENTRY 3000, retained raw time 0, spawn commandTime 2900: the donor clamps raw to 2000 and does not manufacture 100 ms of movement. Passing forced 3000 would be wrong. The Think reviewer re-read and confirmed this narrow source fix; that is not whole-packet acceptance.

Respawn explicitly performs `qa_q3_client_copy_body_queue` before Spawn, then a genuine teleport-in event. The source owner removed implicit native corpse copying from ClientSpawn; legacy nonnative selected-character behavior remains separate. SetTeam performs corpse copying at its own source predeath site when health is already nonpositive. Never invoke generic synthetic death as a corpse/drop substitute.

### SetTeam, follow, and source commands

Ranked ACTIVATE calls real direct SetTeam("free"), bypassing console team cooldown exactly as the donor. CmdTeam has the real 5-second source switch clock and tournament loss writes. Team choice/balance uses actual physical source clients/sessions, with the source ignore-slot/PS-clientNum distinctions. Tournament/max-player policy reads retained CalculateRanks state, not fresh generic member counts.

SetTeam writes true source session masks, source teamState BEGIN, genuine direct force-death with MOD_SUICIDE 20/flags 32/amount 100000 and final health 0, real source leadership callbacks, reliable team announcement, UserinfoChanged, and Begin. It only changes chosen mode join/team effects when the real native selected-Q3 pass is eligible. Requalify the incoming actor after leadership/UserinfoChanged/print callbacks.

Follow keeps numeric physical source targets and automatic -1/-2 sentinels. Disconnected client fields remain valid source rows. StopFollowing writes true PERS_TEAM = spectator before sess team/state, clears actual PMF_FOLLOW through the movement/native-policy writer, then restores real source BOT/clientNum semantics. Source FOLLOW copies belong to genuine PS backing; chosen member or renderer observation must not reconstruct them.

`application_command_fallback` resolves actual ENTITIES GAME for explicit Q3 client invocations and source console IP filters, even with foreign chosen rules. Native bot commands use `application_native_q3_client_text` with a real Q3 context and actual dispatcher. The generic public actor-command API retains chosen-character dialect; it is not silently forced to Q3.

Console source client print now uses the real reliable sink. `where` reads true source entity state and preserves source integer/vector bounds. CmdGive awards use `qa_q3_client_award` for genuine PS domains. Explicit levelshot checks copied cheats, real shared health, cached gametype 0, calls true BeginIntermission, then sends `clientLevelShot`. `abort_podium` belongs to real ServerCommand handling; it was deliberately not added to intermission ClientThink.

## Chat, logging, rankings, and scoreboard

Native chat qualifies actual ENTITIES GAME/full actor/physical client, uses source max clients and cached gametype, and reads `dedicated` from its real engine cvar owner. Reliable `say`, team chat, tell, voice, team voice, voice tell, game commands, and intermission fallback use the actual source fixed rows. Recipient eligibility reads real inuse/pers CONNECTED/BOT/session team; tournament spectator restrictions are retained.

Three outputs remain distinct: true source LOG via `application_native_q3_log`, console print via `application_native_q3_console_print`, and reliable source client commands via `application_native_q3_send_command`. LOG must never be collapsed into print, MESSAGE, HUD, or guessed event history. Dedicated prints preserve the real dedicated topology. The real LOG worker supplied a builtin LOG kind and an actual sink path; neighboring validation still needs its own source review.

Team location comes from `application_native_q3_team_location` and the true GAME location timer/team_location state, not a second mode location cache. The TEAM owner supplies actual CheckTeamStatus and real source sorted-client behavior. The dedicated `q3_team_status_bound` hook proves installed source capability before generic TEAM_INFO bypass; no broad kind/rules guess is sufficient.

Voice taunts use real source last-killed/last-hurt/reward producers. `enemy_source_present` and `enemy_source_slot` belong to the retained physical **gentity** enemy reference, separate from canonical enemy provenance. They survive canonical retirement, reconnect, Begin, and Spawn; the source taunt clear is the actual writer that nulls them. This closes the donor case where A kills V, A disconnects, and V's vtaunt still reads A's retained source client fields for death_insult/self echo. Do not resolve the enemy through a live-actor-only API.

`application_rankings_source_effect` now exists with real SPECTATOR, ACTIVATE, SCOREBOARD, and DROP_BOT branches. SPECTATOR writes true source sess spectator/FREE then Spawn; ACTIVATE calls direct SetTeam("free"); SCOREBOARD sends a genuine source command; DROP_BOT authors the genuine retained wire request. It does not recursively disconnect rankings while the ranking frame owns `busy`. Ordinary ranking frame failures are caught at the ranking-frame scope; direct disconnect flush/logout stays strict, matching the donor's direct lifecycle.

Scoreboard uses true retained GAME CalculateRanks counts and sorted physical slots, actual pers connection/enter/ping, actual source PS/entity observations, and true GAME red/blue team scores. It sends the donor 14 integers per row through the actual reliable sink, with the bounded 1024-byte entry payload, connecting ping -1 and normal ping cap 999. Accuracy retains wrapped hits*100, signed division, and wrapped quotient, including INT32_MIN/-1. Integer formatting retains AddInt semantics. Generic QA_MODE events are not scoreboard transport.

Lifecycle Connect/Begin/Disconnect call real `application_native_q3_rank`. That new producer belongs to `/root/q3_pool_source_peer`, using actual fixed64 records, retained sorted-array tails, SDK/TS qsort, source follow slots/counts/PS ranks, and actual score services independently of selected mode identity. Its Match tail and actual source callers still require whole integration review.

## Native DROP contract and coordinator ordering

The parent chose the native TypeScript engine contract explicitly. TS host dropClient authors a retained emitted request; the coordinator later performs ranking, bots/network, and simulation source retirement. The original SDK synchronous SV_DropClient ordering differs and must not be mixed into the native port as a synchronous pers mutation.

The wire holder owns pending DROP/reason/full actor and the real transport callback-once state. Reliable overflow also authors this same true request. No generic event substitute or invented immediate disconnect stage was added.

`application_native_q3_clients_drain` runs only after ranking busy and session/modes/world/combat callbacks release. It allows real IDLE/ADVANCING/CONFIGURING phases, resolves actual ENTITIES native GAME, and reads exact retained pending full actor/source slot. It then calls one private actual disconnect body with transport enabled and finally the real canonical roster retirement helper.

The final current5 body ordering is:

1. One actual rankings disconnect.
2. Actual per-client bot shutdown(false), while old actor/source/wire remain live.
3. Optional actual wire drop_transport callback once, before GAME retirement.
4. Source fixed follower StopFollowing, genuine teleport-out for connected nonspectator, actual TossClientItems/persistent item and Harvester cube producers.
5. Distinct ClientDisconnect LOG at the source site before tournament winner mutation.
6. Genuine tournament sorted-tail winner write/UserinfoChanged.
7. Source unlink/deactivate/DISCONNECTED, sess team FREE, chosen connection projection, empty CS_PLAYERS, CalculateRanks, and actual wire disconnect retaining engine userinfo.
8. Canonical/session roster retirement through `application_players_native_q3_retire`.

This was corrected in the last edit: the old draft did rank disconnect twice and transport before bot shutdown. Current5 has a private `client_disconnect(..., bool drop_transport, ...)`; public native detach uses it without transport, and drain uses it with transport. Do not regress the order.

Persistence's ordinary and INITIAL callers drain after rank busy=false. The fast-round caller drains after final strict ranking, before wire round finish/snapshot. Actual publication drains the old source before native carry/handoff, since those reject still-pending DROP. The private roster helper now permits CONFIGURING only during the real fast cut or old publication lease with actual old routing/provider/map-source qualification. Broad public mutation guards remain intact.

## Settings and ClientThink contracts

All copied game vmCvar reads use `application_native_q3_settings_integer/number/string/snapshot`. These are actual retained cached snapshots initialized/updated by source lifecycle; there is no live-registry fallback. Force-set changes only the real registry and leaves same-invocation cached values stale until true UpdateCvars. The source owner confirmed that GAME options rules.gametype is a real projection of this cache at source init/load/frame; do not reject that existing consumer merely because it is a projection.

Think policy uses connected pers, true source session/commandTime, raw signed usercmd serverTime, ENTRY clamp +200/-1000, signed msec, FOLLOW nonpositive exception, >200 cap, copied pmove_fixed/pmove_msec, and real out-of-range force-set 8/33 semantics. Raw original words reach source pers before policy. Ordinary received command intake must update real pers cmd and lastCmdTime and actual wire command, then defer BOT/synchronous clients; the later real G_RunClient uses retained pers.cmd with ENTRY time. No fake transport sequence advance is added for internal Spawn/RunClient Think.

`application_native_q3_client_deferred` is the shared pure BOT/cache g_synchronousClients query. `application_native_q3_source_client_run` proves the literal actual PHYSICS frame and full physical client, then runs only the source deferred branch. The actual session source_actor phase producer was repaired by movement owner to set the real source clock PHYSICS before copying/invoking; no fabricated frame is accepted. Secondary component-only Q3 pers DISCONNECTED is a no-op before primary GAME qualification.

Special Think reads true Match intermission time, clears TALK/FIRING eFlags, updates real source raw buttons, and authors readyToExit only on ATTACK|USE rising edge. SCOREBOARD/FOLLOW spectator source branches complete without inventing positive physical movement. The true main pipeline is now owned by `native_q3_control.c/.h`; it must consume policy exactly once and continue actual events/triggers/inactivity/body/source feedback in donor order. READY admission is only actual CONFIGURING source initialization; ordinary remote Begin is RUNNING/IDLE.

Movement parameters use source noclip, real shared health, GAME session, copied gravity/speed, and actual selected control/native foreign policy holder. Spectator gets PM_SPECTATOR/speed 400 and retains existing gravity. Foreign PM writes preserve actual bob/flags/time/movementDir; selected-Q3 scalars stay kernel-owned.

The last current5 multiplier change follows the donor selected-equipment contract:

- Resolve genuine selected `QA_ROLE_EQUIPMENT`.
- Native Q3 equipment must be live/attached/nonclosing, have a genuine full-actor player read, and carry QA_Q3_EQUIPMENT membership. Use that owner's product and actual persistent/haste state for Scout 1.5 or Haste 1.3.
- Non-Q3 equipment uses the donor GAME source fallback.
- Selected guest Q3 equipment has **no typed speed capability yet**, so this path reports that missing capability explicitly. It must not quietly use native GAME state.

The movement owner was alerted that source Scout scaling and environment/kernel scaling could duplicate; they are settling the true PREP/source environment contract. The main Think reviewer also raised selected arsenal primaryAttackAllowed/gauntlet ownership; the real source services, not a generic guessed weapon, must supply it.

## TEAM objects and genuine source item producers

True cube tossing is `qa_q3_client_toss_cube(game, actor, source_team, timeout, &cube, error)`. Source GAME resets actual PS.generic1 FIRST, checks genuine opened dynamic physical capacity before RNG, reads actual neutralObelisk source pose +44 or zero, uses source RNG/yaw/velocity, and creates a single real LaunchItem actor with source expiry and common spawnflags. `team_restriction` is not cube team: it is a distinct persistent-item mask. The source owner corrected cube team to actual common spawnflags with its source codec.

The client wrapper reads the returned actual item and calls `application_native_q3_objective_admitted` for metadata adoption once. No mode-side second allocation, duplicate source physics, or invented timer clearing occurs.

The objective owner provides actual same-actor source-object adoption/pickup/drop/expiry/return through new modes source helpers and app callbacks. Generic `objects.c` physics/frame/touch/drop/free branches skip genuine source-owned objects. The owned `flags.c` reset/touch and `arena.c` Obelisk touch/damage/reaction paths now also skip source-owned objects before generic effects, preventing source actor release or duplicate scoring. Native true Obelisk continuation supplies admission/reaction/timers/pain/die; chosen mode metadata is an observation/effect projection.

Native source PERS defend/assist/capture now has true GAME fields and `qa_q3_client_award`; objective capture/assist callbacks use it. Generic selected-rule award counters remain independent. Four native TEAM f32 timestamps currently remain live reads/writes in mode statistics until the genuine per-native-client TEAM pers migration lands. That is a real remaining ownership defect, not a completed source continuation.

## Open dependencies and concrete continuation tasks

1. **Independent complete current5 review.** Read all 20 whole files, actual TypeScript/SDK donor sites, the final actual callers, and neighboring source APIs. Review lifecycle order, raw integer formatting, disconnected fixed-row lifetime, full actor requalification after callbacks, foreign character/rules composition, source objects, and codec boundaries. Fix accepted findings only after the parent releases ownership; create a new complete manifest when any file changes.

2. **True native G_BotConnect.** `/root/bots_resume` reports real bot AI population/library admission still happens after source clients publish. Establish genuine initialized population/settings before Connect, expose actual admission/denial service, and wire it at source Connect after session setup before LOG/UserinfoChanged. Preserve configured/dynamic bot physical slot/flag/raw userinfo/skill/character lineage. Do not add an accepted stub.

3. **Selected guest Q3 equipment capability.** The current movement helper honestly errors for guest Q3 equipment because the actual getter does not exist. The parent needs a genuine typed selected-equipment speed/state service for that backend, with source qualification, then the caller can consume it. Keep non-Q3 fallback and native selected-Q3 state distinct.

4. **True per-client TEAM pers.** `/root/native_q3_objectives_owner` says its current helper no longer reads/writes mode q3_defend/assist/capture as native PS authority, but the four mode f32 timestamps are still source TEAM runtime authority. `/root/q3_source_resume` next sourcecore10 and `/root/bot_orders_owner` are coordinating real per-native-client six counters/four f32 fields and APIs. Migrate native objective runtime to those fields, retain chosen-mode projection only, and remove obsolete mode backing only if other genuine backends no longer consume it. Preserve f32 writes, source zero-timestamp assist behavior, and lastHurtCarrier=-5.

5. **Real TEAM hurt/death/frag-defense producers.** Parent is assigning a separate source lane. True physical source classname/raw team/base state must drive native defense logic; foreign selected FFA may project both bases into neutral shared metadata and cannot be used as source team authority. Preserve generic selected-mode counters/effects independently. Actual source player-die rankings must precede source Kill LOG/obituary/rewards; dynamic source worker owns genuine log/EV_OBITUARY/drop ordering.

6. **Full native Match and END settlement.** `/root/native_q3_log_owner` owns new native Match and transferred modes core/match. Actual GAME timers/session write/pose/client move belong there; selected effects remain chosen-rule qualified. `/root/q3_round_resume` owns actual source END adapters and clock/resolver migration. Check real order: physical ClientEndFrame, native match/team status, votes, settings CheckCvars, source serverinfo. Once full native pass/postgame/bot producer is accepted, delete dead generic native readiness/session/timer branches atomically; retain other backends' genuine pass.

7. **Real ClientThink integration.** `/root/tools_source_review` owns new native_q3_control; `/root/movement_resume` owns selected control/session source-command qualifier/nested invocation. Ensure actual received raw input writes native pers.cmd/lastCmdTime and wire history, async humans run immediately, BOT/cached synchronous defer to G_RunClient, source accepted commandTime completes from real movement, raw full32 angle words/delta authority are preserved, inactivity warnings/drop use true source fields/request sink, and events/triggers/post-Pmove/policy occur once. Verify foreign selected movement source PM policy, view command, FOLLOW copy, and source body/combat/inventory ownership.

8. **END retained water finding.** `/root/model_owner_restore` found native_q3_end_frame draft passes raw selected-control water type to source END. True native control already maps Q1 -3/-4/-5 to source 32/16/8 and commits source water before ClientEvents/triggers. END must use `qa_q3_client_movement_water_read`, especially after teleport/event changes; raw -3 has both lava/slime bits. `/root/q3_wire_producers` forwarded the finding to the actual owner `/root/frontend_model_inventory`. The genuine `qa_q3_client_end_prepare` dependency was also reported absent/unfrozen at that review cut; confirm its actual implementation now before claiming END closure.

9. **Native source-body/FOLLOW retired PS details.** Wire/source owner next packet must expose genuine fixed PM/score/source-body and retired scalar/client-pointer/model observations. FOLLOW must retain donor copied PS scalars/stats/persistant while preserving shared body/combat/inventory authority. Never derive copied follower PERS_TEAM/score/rank from its own session or chosen mode when actual followed PS owns that copied publication.

10. **Source spawn with foreign selected character.** Native GAME now runs real body/link/PM continuation regardless selected character. Confirm the genuine foreign selected character health/inventory/reset callback is invoked at native Spawn and that it does not replay native grants or corpse copies. Spectator source pose is fixed; ordinary player pose, selected movement reset, original retained raw command, and source inactivity are current5 but need full mixed-role source review.

11. **Cached cheats/console callback lifetime.** Current5 console commands use the real source client sink and cheat callback. The actual installed `application_native_cheats_enabled` was reported reading live app cvar for native Q3; parent was asked to repair it to actual cached source cheats while preserving other backends. Shader owner also requested true provider borrow/post-callback qualification in `application_native_q3_console_print`; that bounded function belongs outside this packet and must be checked at its actual installed caller.

12. **Source object view integration.** The new public pure getter field and app binding are frozen here. Objective owner must finish its real `objects.c` delegation and actual Obelisk/model getter, then peer the whole source-owned read path. Validate source-owned metadata reconstruction against actual candidate restore routing, not chosen rules enum or canonical owner equality.

13. **Safe old-cut pending DROP.** INITIAL, ordinary, final round, and old publication drain callers are visible in current source, but their full mixed callbacks/cut sequence still needs independent review. Ensure retained pending DROP is drained while old source is routed and all busy owners released, before carry_refresh/world handoff/wire round finish. Keep direct disconnect backend errors strict and transport once-state retained. No new synchronous SDK reorder.

14. **Persistence integration.** Reconcile typed checkpoint12/private modes10 with the current P bundle save/restore and real source GAME schema. Detached source-object qualification, candidate routing, disconnected source userdata/sess/PS rows, pending DROP, rank busy/private continuation, and imported fixed sessions must survive the actual continuation contract. Do not claim full saved-game acceptance from field serialization alone.

## Neighboring owners and entry points

| Area | Owner | Actual files/APIs |
| --- | --- | --- |
| GAME pool, clients/pers/sess/PS, source codecs | `/root/q3_source_resume` | `qa/game_q3_clients.h`, client types, source_state/client/player/source helpers; actual fixed64 owner |
| Wire admission/reliable/userinfo/DROP | `/root/q3_wire_resume` | `native_q3_wire_state.c/.h`; connect/begin/disconnect/command/send/drop/read/transport/source userdata |
| PS/entity/native foreign PM/FOLLOW backing | `/root/q3_wire_producers` | `qa/game_q3_wire.h`, wire helpers; policy_read/policy/ready/detach/follow/view |
| Real selected control and source command scope | `/root/movement_resume` | control/session/source roster admission, raw command projection, body reset, real flags |
| Genuine ClientThink orchestration | `/root/tools_source_review` | new `native_q3_control.c/.h` |
| Map/client source admission and round/publication adapters | `/root/q3_round_resume` | `map_players.c`, providers/native_modes/native_maps, q3 round caller contracts |
| Actual source settings | `/root/q3_settings_owner` | `native_q3_settings.c/.h`, pure cached read/force-set/source CheckCvars |
| IP filter source owner | `/root/q3_ipfilters_owner` | `native_q3_ipfilters.c/.h`, filter and console actual fixed1024 state |
| Ranking/P/private persistence | `/root/persistence_resume` | rankings, INITIAL/ordinary after-busy drain, bundle continuation |
| Bots population/source lifecycle | `/root/bots_resume` | real bot services, `application_bots_client_shutdown`, admission initialization gap |
| GAME session cvar lifecycle | `/root/native_q3_session_owner` | `native_q3_session.c/.h`; init/connect/read/write/carry actual raw sess7 |
| Native votes and leadership/postgame | `/root/native_q3_votes_owner` | `native_q3_votes.c/.h`; real source pers counts/sess/eFlags/CS, SetLeader/CheckTeamLeader; podium services |
| Real source CalculateRanks | `/root/q3_pool_source_peer` | `native_q3_rank.c/.h`, `application_native_q3_rank` |
| LOG and source Match control | `/root/native_q3_log_owner` | native log/match; transferred modes core/match |
| TEAM state/objectives/Obelisks | `/root/native_q3_objectives_owner` | native objectives, q3_objective_source, bounded objects.c; actual TEAM/model state |
| TEAM status/location | `/root/bot_team_policy_owner` | new native team status, transferred team_info.c, real bound capability |
| Dynamic death/items/obituary/source output | `/root/q3_dynamic_wire_owner` | true death/rewards/Drop_Item and source effects |
| Real ClientEndFrame adapter | `/root/frontend_model_inventory` | new native_q3_end_frame; retained-water/source feedback dependency |
| TEAM pers/director coordination | `/root/bot_orders_owner` | true GAME team pers producer/API coordination for sourcecore10 |

The source behavioral oracle is the local TypeScript project, especially `src/content/q3/team-arena/client-admission.ts`, client-spawn/client-think/client-policy/client-effects/commands/session/team/match modules, `src/app/bootstrap/simulation/runtime.ts`, `src/app/bootstrap/simulation/arsenal/q3-source.ts`, and `src/app/bootstrap/application.ts`. Use the actual available source files rather than importing behavioral claims from a previous answer. Original SDK differences are reference material; the parent has explicitly selected native TypeScript contracts where they differ.

Continue from current5; do not restart the migration or resurrect deleted mode session/cube owners. The next concrete parent action is assigning complete independent source review of this exact packet while other owners finish their authentic producer/caller gaps. Subsequent edits require an explicit released file boundary and a new full manifest.

