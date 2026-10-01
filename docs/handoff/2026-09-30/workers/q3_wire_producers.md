# Q3 wire producer handoff — 2026-09-30

Owner: `/root/q3_wire_producers`. Workspace: `/home/buzzkill/Projects/quake-anthology`.

Code edits stopped at the parent's handoff request. This file and the manifests are outside the repository. No configure, build, compiler, test, game, project script, parser, generator, syntax check, sanitizer, CMake edit, or  mutation was performed by this worker. The global source-only gate remains closed.

## Exact owned files and handoff cut

Exclusive ownership was limited to these three files:

| File | Current handoff5 SHA256 |
| --- | --- |
| `include/qa/game_q3_wire.h` | `8e6ff7326daf4a68557e0e14b649ff5b7dd45f340547de8759982e3429a96539` |
| `src/gameplay/q3/source_wire.h` | `0c4d68eada8cd10cc0e63242578e3a891fcfc42a0db71986ac07524f3d1f3374` |
| `src/gameplay/q3/source_wire.c` | `c8221a2816cb919bb54b18ab453163432c06bf9e98f5a071930b6c2ffa01707b` |

Current, unfinished cut manifest: `/tmp/qa-q3-wire-producers-handoff5-20260930.sha256`.

Producer5 is **unreviewed and incomplete**. Its public accessor declarations have no implementations yet. Do not treat the current files as the accepted producer4 hashes.

## Accepted producer4

Accepted manifest: `/tmp/qa-q3-wire-producers-native4-20260930.sha256`.

| File | Accepted producer4 SHA256 |
| --- | --- |
| `include/qa/game_q3_wire.h` | `89a3b36dcfe14b251b5fb7b0bb40e09235686e920a8430058826d48e73a8d7bb` |
| `src/gameplay/q3/source_wire.h` | `0c4d68eada8cd10cc0e63242578e3a891fcfc42a0db71986ac07524f3d1f3374` |
| `src/gameplay/q3/source_wire.c` | `34f9a303d52fb418713204b0580c941e94fb73e7f73bfacd8bdc9ffb4d8bc30f` |

`/root/movement_source_peer` independently accepted the complete three-file packet after reading the native donor, actual source constructors/callers, lifetime and codec boundaries. The peer matched hashes twice and checked scoped whitespace. This worker then rechecked all three hashes and whitespace. Root reported the packet committed as `205c3fed6ad83062ee5249145bd6a400f6139b1a` and explicitly released it for the next unit. No remote inclusion claim is made here.

Producer4's acceptance covers these source helpers and their inspected dependencies; it does not establish execution or full game parity. The separately owned native wire, control, END, objectives and source-core units retain their own review requirements.

### Producer4 behavior

- Native snapshots use `qa_q3_wire_native_visibility_read`, a distinct 128-leaf observer over the actual current published body's absolute bounds. It preserves encounter order, deduplicates all nonnegative clusters across the returned 128 leaves, uses the first two unique areas **including -1**, and sets `last_cluster=0`.
- The genuine original/guest source link helper remains separate: sixteen stored clusters, original overflow/last-cluster and area policy. Do not replace that policy with the native observer.
- `qa_q3_wire_mode` contains only genuine selected match `score`. Native PS rank, persistent team, generic1, defend, assist and captures read the true GAME player fields.
- Readonly PM policy observes actual selected-Q3 control fields or the genuinely authored native holder when another movement family is selected. Observation never caches a selected movement copy.
- Retained native FOLLOW source fields are read through `qa_q3_client_follow_read`. The wire reader combines those copied source fields with the **follower's own live** body origin/velocity, health, armor, weapon bits, mapped ammunition and genuine unmapped special-ammo holder. It does not query the current follow target at observation.
- Native weapon/ammo authority reads cover all thirteen genuine Q3 item mappings, independent of base/missionpack product, matching the native `records.ts` binding. Genuine no-armor reads zero.
- True post-copy source writes update active copied PS through `q3_client_follow_player`: ready status, loop sound, whole/masked PM assignments, FOLLOW clearing, damage PERS fields, external events, and BG/pending entity-event dequeue. Native view-command mutation updates the true copied source view/delta fields. Special ammo remains the follower's own authority.
- `q3_wire_client_follow_copy` writes only the existing wire-owned source hits, attacker, attackee armor, ready status and loop sound at actual END copy. The source owner integrated the real call in `client_begin.c`; copied deaths use PERS slot8, not slot7.
- `qa_q3_wire_link` and `qa_q3_wire_body_read` use the genuine core `q3_source_body_read` currentOrigin overlay. The link publishes that source origin through `qa_world_link_bounds_at(...,&origin,...)` even when the caller's explicit override is NULL. PS body reads stay canonical and precise.
- Wire capture rejects an active source currentOrigin scope via `q3_source_origins_idle`.

Donors inspected: `../quake-typescript/src/app/bootstrap/simulation/network-q3.ts:175-178`, `src/content/q3/base/shared/player-state.ts:173-223`, `src/content/q3/base/records.ts:245-282`, and `src/content/q3/base/shared/entity-shared.ts:19-43`. All paths after the first are under the same TypeScript checkout.

## What producer5 actually changed

Only these changes exist beyond accepted4:

1. Added a narrow public type and three declarations in `game_q3_wire.h`:

   ```c
   typedef struct qa_q3_wire_client_body {
       qa_body_state current;
       qa_shape_kind model_shape;
   } qa_q3_wire_client_body;
   bool qa_q3_wire_client_source_body_read(const qa_q3_game *, uint32_t,
                                          qa_q3_wire_client_body *, qa_error *);
   bool qa_q3_wire_client_source_pm_read(const qa_q3_game *, uint32_t,
                                        int32_t *, qa_error *);
   bool qa_q3_wire_client_source_score_read(const qa_q3_game *, uint32_t,
                                           int32_t *, qa_error *);
   ```

   **Definitions do not exist yet.** `/root/native_q3_votes_owner` agreed to this narrow body type and migrated its mutable postgame caller from `qa_q3_wire_body` and `collision.shape` to this type and `model_shape`. Only actual bounds/model are exposed; no unused last-link/full collision state is synthesized.

2. At wire create/reset, all64 fixed GameClient PM holders now mark their real zero source constructor state authored (`foreign_policy_written=true`). This creates no actors. The donor constructs all64 GameClients and their PM-zero PS before admission. This delta remains unreviewed.

3. Added explicit `Q3_ACTOR_OBELISK` retained GENERAL source-field passthrough in the entity reader switch, at the source owner's request. Its actual `source_objectives.c` constructor owns source position/body, links and commits readiness. No second trajectory was introduced. This delta remains unreviewed.

`source_wire.h` remains identical to accepted4. No retained score field, body-attachment field, model metadata field, alias/podium field or additional codec primitive was invented in this worker's files.

## Required fixed-client semantics

The source fixed client pool is64 physical PS records. New pure PM/score reads must use that physical domain, not configured maxClients, canonical actor slot, in_use, or an invented actor. Arena's sorted_clients tail is retained64, and the donor reads sorted[1] even when fewer than two clients are active. Untouched true constructor PS score/PM zero is legitimate.

The real gentity `.client` pointer is a separate source identity from PS.clientNum and from `kind==PLAYER`:

- `content/q3/base/game/entities.ts:92` installs pointers only below configured maxClients; `activateClient:97` reinstalls a pointer; `initializeClients:133` installs them after reset.
- `records.ts:175-184` deactivates a client by clearing the source record actor/active/borrowed fields, while retaining entity.client and its native GameClient PS.
- Dynamic victory models can borrow a fixed source client pointer. The pointed source PS may be disconnected or have no canonical actor. Do not require a live actor to mutate that actual aliased PS.

The native absent record body is exactly the donor's ZERO_BODY. Do not retain a copied canonical origin, velocity, bounds or other current-body state after source record absence. Source r.model, contents and ownerNum remain separate real source metadata. PreviousLink is a genuine historical link owner, not current body authority; do not fabricate it in an absent broad world view.

### Confirmed body-detach phase gap

TypeScript `team-arena/client-admission.ts:360` calls deactivateClient, which immediately nulls the source record actor even when the borrowed canonical actor remains live. Current C `qa_q3_client_disconnect` calls wire detach/unlink and sets in_use=false but retains binding.actor until later registry release. Existing `qa_q3_wire_client_read` still reads the live canonical body while that reference remains.

Core next10 must author a genuine source body-attached/deactivated state or accessor at this actual source producer. Do **not** guess record absence from in_use=false or from the PM retirement marker. Once that state exists, the narrow fixed body read and existing fixed source-origin projection must return true ZERO_BODY at the correct source phase, while propagating failures for actual attached bodies.

### PM and score retirement

Current `qa_q3_wire_client_detach` is called by real `qa_q3_client_disconnect` before unlink, model-index clearing and connection/in_use changes. It qualifies the exact source actor, reads actual selected movement, and transfers selected-Q3 PM scalars into the native fixed holder before control retirement. Foreign PM uses its already-authored holder. `movement_detached` is retained in the wire codec.

Score has no equivalent genuine retired cell yet. Live score currently comes from the real selected primary match callback. Native FOLLOW score comes from actual copied source PS.persistant[0]. Donor PM and score survive actual disconnect because GameClient PS remains retained (`client-admission.ts:328-365`, `records.ts:175-184`). Before implementing the new fixed score read, capture true source score at the real detach stage while the actual owner can still be read; never substitute zero after a failed live score read.

The source owner owns the new score type/cell and portable/typed codec contract. Names were only proposed, not settled. The contemplated retired cell is constructor0 and read only once the live authority is genuinely unavailable. `qa_q3_client_score_reset` must reset it at its real source reset producer, along with active copied FOLLOW score. That function currently only resets the optional copied PS. Avoid a second live canonical score authority.

For PM accessors, prefer true current selected/native source policy while attached and true retained policy after actual retirement; preserve full actor generation qualification across provider callbacks. If a current binding fails, report it. Do not reinterpret that failure as absent source state.

## Core next10 and postgame dependencies

`/root/q3_source_resume` owns core types, physical/client identity, native state, lifecycle and codec. It was closing/freeze-reviewing source9 when the handoff arrived. It explicitly deferred new pointer, retired score and body metadata contracts until root peer acceptance/commit/release of9. No next10 field names may be assumed.

Core9 corrections reported by that owner include genuine predictable-ring and eventSequence copied-PS writes, flag-powerup/token/award copied-PS writes, completed commandTime mirroring plus selected-holder COMMAND write, and saved FOLLOW validation requiring unused canonical body/health/armor/weapons/ammo words zero. Input live-copy validation remains separate. Verify those owner hashes/status from its handoff; they are not certified by this worker.

`/root/native_q3_votes_owner` owns new source/application postgame files. It proposed PODIUM and VICTORY_MODEL kinds with source-owned full qa_q3_entity, source_client alias (-1/null or true fixed slot), timestamp, nextthink, think, count, physics bounce/object flag, and level podium_players[3]. These require core-approved types/codec and source constructors before wire integration.

Required wire follow-up after those types land:

- Read genuine postgame full entity state directly from its source owner; do not regenerate copied s fields from a renderer or event report.
- Link solid must write into that true full entity owner, not an unused ordinary wire source.solid mirror. Keep constructor readiness at the actual tail and permit genuine constructor links before readiness.
- G_AddEvent on a victory model with an actual `.client` alias writes the **fixed source PS.externalEvent/Parm/Time** (including an active FOLLOW PS), leaves model.s.event untouched, and writes the model's real eventTime. No model-local external-event shadow.
- Expiry clears aliased fixed PS.externalEvent only when the model's own s.event was nonzero before expiry cleared it, matching `base/game/entities.ts:244-247`.
- Podium model source reads use actual source r.model capsule versus BOX, true local mins/maxs (ZERO_BODY if source actor absent), source binding flags/ownerNum, and genuine retained s. Arena does not guarantee a second live actor.

## Other owner coordination

- `/root/bots_resume`: new public `qa_bot_source_player_state` callback fields are present, has_player, pm_type, score, last_hurt_client, last_hurt_mod. It deliberately rejects missing capabilities. It needs actual pointer qualification from core and these unfinished PM/score accessors, not all64 PLAYER-kind guesses.
- `/root/q3_wire_resume`: its separately frozen world-cut5 manifest is `/tmp/qa-native-q3-wire-world-cut-resume-20260930.sha256`. Native current_view now consumes the true128 reader and score-only adapter. It also corrected native `cm_noAreas`: query actual live scoped cvar and bypass area connectivity only, leaving area bits and guest policy unchanged. Original guest retains16. Verify its separate peer outcome from its handoff.
- `/root/tools_source_review`: genuine native control ordering and source currentOrigin bracket. It uses actual overlay -> events -> wire_link(NULL) -> triggers -> finish scope. No persistent snapped canonical origin is allowed.
- END water issue was corrected by the separate END owner and independently accepted by `/root/model_owner_restore`: `/tmp/qa-native-q3-end-frame-water-20260930.sha256`, C prefix `4b97a515`, header prefix `bcb818`. END reads the actual retained Q3 source water, which native Think authored after mapping foreign movement contents, through `qa_q3_client_movement_water_read`. It must not use later raw selected-control water.

## Next actions after handoff

1. Read core/source9, wire consumer, END/control and postgame handoffs; verify their actual frozen/committed/released boundaries. Keep the source-only gate closed until root explicitly changes it.
2. Settle core next10 actual client-pointer, body-attachment, r.model/contents/history and retired-score ownership/types/codec. No fields are settled yet.
3. Implement the three declared fixed accessors against those genuine owners. Physical64 constructor/disconnected/tail cases must work without fabricated actors or fallback on provider failures.
4. Integrate real score retirement/reset and source body-deactivate transitions. Preserve current full actor generations and genuine FOLLOW source state. Confirm the actual selected-Q3 kernel PM scalar write path also updates the active FOLLOW source domains before treating all fixed PM observations as closed; that full path was not independently retraced in unfinished5.
5. Integrate genuine postgame full entity/link/event alias handling after approved enum/state definitions land.
6. Read actual source donors and callers, then freeze a complete bounded manifest and obtain independent source review. Producer5 has no review or runtime result. are root responsibilities; this worker must not mutate  or CMake.

Do not restore deleted helper scripts, create documentation inside the repository for this handoff, or claim parity from these source checks.

