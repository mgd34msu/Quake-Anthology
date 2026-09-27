# Q3 and modes independent source audit

Status: incomplete. B12, the Q3 portion of B13, and B14 remain open. This report is a source review, not build, test, gameplay, performance, or whole-project qualification.

The reviewed C snapshot had repository HEAD `849a3b2159c5888ec965708894386f82150e2fda` and aggregate SHA-256 `d2915ef0f82ccaf274bc49e88be082545bb5f67277f953d4332e8b25aa7900e3` over the 42 files listed below. The aggregate includes the then-uncommitted `include/qa/modes.h`, `src/gameplay/modes/flags.c`, and `src/gameplay/modes/objects.c`, plus untracked `src/gameplay/modes/give.c`; individual hashes are authoritative if HEAD advances. The ancillary build definition was `CMakeLists.txt` SHA-256 `5bd08697a118e1c567f634fad79d926c2d6adb3b4601acc8fa3b283640caf27f`. Donor behavior was reviewed at `../quake-typescript` revision `5d6c6e311db6749c0b8f8ffca53ebe2bae69b24e`.

Reviewer: `/root/design_judge`, session `s_b5a15ba5395f4807bbde4b547d075b58`, AUDIT contribution `w_876a77f4d6e94755b6e8c57d9a26679d`, parent `w_357ddd74810a4aa8a3bb84fdd7095fb8`. No project configuration, compilation, test, executable, generator, sanitizer, benchmark, or gameplay run occurred. Source reads, searches, hashes, and the explicitly requested Jev report checks supplied the evidence.

## Existing graph criteria and disposition

| Task | Existing goal and criterion | Source disposition |
| --- | --- | --- |
| B12 | Implement Anthology Q3 and Team Arena gameplay, weapons, items, characters, and source rules in native C. Q3 and Team Arena behavior must use shared services and remain independently selectable. | Native character, arsenal, effects, equipment, movement, weapon, missile, item, mover, holdable, death, feedback, view, and checkpoint code exists and uses shared session/world/combat/inventory/pickup/physics services. QM-01, QM-04 through QM-06, QM-08 through QM-10, and the source-only validation limit prevent acceptance. |
| B13 | Implement entity target graphs, mission gates, keys, sigils, bosses, hubs, revisits, authored mechanisms, and travel. Campaign code must preserve authored obligations through replacement, with one transition owner. | The modes objective registry preserves an explicit owner/lease and campaign-gate bit, and match transitions leave through one intent callback. The assigned Q3 lane has no native Q3 target/trigger/worldspawn graph or authored-item team/use lifecycle, so its B13 contribution is incomplete (QM-01 and QM-06). Other campaign families are outside this lane. |
| B14 | Implement independent modes, teams, scoring, cross-map objectives, grapples, and offhand equipment. Required variants must connect to shared gameplay without coupling map, arsenal, or movement selection. | The source implements Q1 Threewave/Horde, Q2 CTF/LMCTF/Rogue Tag/DeathBall, Q3/Team Arena match kinds, map adaptation, objectives, voting, scoring, checkpoints, four grapple families, and offhand grenades. Per-instance player state and damage/item routing are nevertheless coupled across modes (QM-02, QM-03, QM-07, QM-11), so the independence criterion is not met. |

## Confirmed defects and missing implementation

### QM-01: Native Q3 authored map and source-rule surface is missing

Severity: high. B12 and the Q3 portion of B13.

The public Q3 surface proceeds from player/weapon APIs to item spawning at `include/qa/game_q3.h:200-238` and movers at `:240-250`. `qa_q3_touch` at `src/gameplay/q3/items.c:645-684` dispatches only item, proximity-mine, and portal contacts. There is no Q3 native entity parser/dispatcher or implementation for target graphs, triggers, teleport/push triggers, jump pads, portal surfaces/cameras, map shooters, worldspawn, spawn selection, match-level/intermission ownership, or ranking reports in `src/gameplay/q3`.

The donor has concrete behavior in `content/q3/base/game/targets.ts` (target give, powerup removal, delay, score, print, speaker, push, laser, teleporter, kill, location, relay and position), `triggers.ts`, `misc-spawn.ts`, `spawn.ts`, `level.ts`, and `rankings.ts`. This is required source behavior, not presentation-only metadata. Shared campaign and mode services can own common target/travel operations, but the Q3 source admission and callbacks still need a typed native implementation and application wiring.

### QM-02: A player admitted to multiple mode instances has one shared team, score, and spectator state

Severity: high. B14 independence.

`mode_instance` owns separate membership arrays, but `mode_player` owns one `qa_match_player` for the actor (`src/gameplay/modes/internal.h:14-21,31-53`). The public score and team reads omit a mode ID (`include/qa/modes.h:337-343`). `qa_modes_score`, `qa_modes_set_score`, `qa_modes_team`, and `qa_modes_set_team` read/write that single value or one external binding (`src/gameplay/modes/core.c:444-504`). `mode_join` permits the actor to join another instance and overwrites the shared team/spectator state (`:552-585`); no exclusivity guard rejects concurrent membership. Per-mode statistics do not repair ranking, voting, friendly-fire, team totals, or score mutations, all of which consult the shared values.

Thus two active mode instances cannot independently assign the same actor different teams or scores. Joining or scoring in one changes the other. The state must either be keyed by `(mode, actor)` or the API must enforce and document a single-mode membership invariant; the latter would not meet the required independent composition.

### QM-03: Tag and DeathBall damage rules modify actors outside their mode and compound across instances

Severity: high. B14 independence.

`qa_modes_object_damage` iterates every enabled mode (`src/gameplay/modes/arena.c:56-115`). For non-Rogue Tag, it multiplies damage by 0.75 whenever neither endpoint is that instance's tag owner (`:60-63`). For DeathBall it halves damage whenever neither endpoint is that instance's ball (`:64-69`). Neither branch requires attacker or target membership in the instance. An unrelated fight therefore changes when any Tag or DeathBall instance is enabled, and multiple instances multiply the reductions repeatedly. Admission must be scoped to the owning mode and its participants/object before applying its source rule.

### QM-04: Three Q3 area operations silently omit actors after a fixed 1,024-candidate prefix

Severity: high in the shared-world product. B12.

`q3_radius` uses `qa_actor_id candidates[1024]`, clamps the count, and discards `overflow` (`src/gameplay/q3/missiles.c:22-36`). `q3_killbox` does the same (`src/gameplay/q3/holdables.c:3-15`), as does the kamikaze shock/damage scan (`holdables.c:232-245`). The donor Q3 entity ceiling explains the original number, but Anthology owns one shared actor world and explicitly cannot cut entity work. The query API tells the caller that the result overflowed; ignoring it makes later actors immune to explosion, telefrag, or kamikaze effects. Modes placement and DeathBall already reject overflow (`src/gameplay/modes/placement.c:94-102`, `deathball.c:136-143`), demonstrating that silent truncation is not required by the shared API. Use a retained snapshot or grow/retry strategy with stable callback ordering.

### QM-05: One Q3 radius attack sequence is reused for every target damage operation

Severity: high. B12 and shared damage provenance.

`q3_radius` advances `game->attack_sequence` once before entering the shared radius loop (`src/gameplay/q3/missiles.c:55-68`). `radius_prepare` only truncates damage and chooses the combat provider (`:8-20`). `qa_builtin_radius_damage` copies the same template attack into every target request (`src/gameplay/builtin/attacks.c:196-209`). Direct Q3 damage advances once per application (`src/gameplay/q3/game.c:320-348`), and the Q1 and Q2 radius preparation callbacks advance per target (`src/gameplay/q1/runtime.c:647-653`, `src/gameplay/q2/ballistics.c:33-49`). Q3 radius must follow that operation-level provenance contract; otherwise observers and saved/replayed mutation ordering see several damage applications as the same operation.

### QM-06: Q3 item teams, target-activated items, and deferred source placement are not representable

Severity: high. B12 and B13.

`qa_q3_item_spawn` has item/count/team restriction/wait/random/drop/suspend/origin/velocity/target fields, but no team-chain/master, targetname/use-hidden, or deferred placement identity (`include/qa/game_q3.h:226-234`). `qa_q3_spawn_item` immediately traces, creates, and links the actor (`src/gameplay/q3/items.c:255-315`), while `q3_item_step` can only respawn that same actor (`:555-575`).

The donor `item-lifecycle.ts:229-277` randomly selects a member of an authored item team on respawn. Its `:418-468` hides TEAMSLAVE and targetnamed items until use, and `:471-499` schedules source placement on the third frame. Those map obligations are absent, so rotating item groups and target-activated items cannot survive native Q3 map admission or replacement.

### QM-07: Public native mode give functions are absent from the mode library build

Severity: high integration defect. B14.

`include/qa/modes.h:414-433` publishes `qa_modes_item_count`, `qa_modes_item_at`, and `qa_modes_give_item`; their definitions are in the untracked `src/gameplay/modes/give.c:49-203`. The `qa_modes` source list in `CMakeLists.txt:356-378` omits `give.c`. Any B34 caller of the published catalog/give API therefore has no object in `qa_modes`. The source must be registered when root integrates this file; no linker was run to establish the already visible source-list mismatch.

### QM-08: Initial Q3 powerup delay uses the wrong random interval

Severity: medium. B12.

`qa_q3_spawn_item` schedules `45 + q3_random(game) * 15`, a 45-to-60-second interval (`src/gameplay/q3/items.c:306-310`). Donor `item-lifecycle.ts:460-465` uses `45 + crandom * 15`, a 30-to-60-second interval. This changes initial powerup availability and RNG consumption. Use the Q3 centered random helper while retaining the source float-to-time conversion.

### QM-09: Q3 items planted on movers publish the world entity number as their support

Severity: medium. B12.

The spawn trace correctly stores `trace.actor` in the shared body (`src/gameplay/q3/items.c:275-288`), but the private Q3 state then sets every non-dropped item's `ground_entity_number` to 1022 (`:293-305`). Donor `ground.ts:6-18` projects an actor support to its native entity number and reserves 1022 for world. Items placed on a mover therefore retain correct shared physical ownership but expose the wrong Q3 support number and can diverge in source item/mover state.

### QM-10: Several admission paths leave live partial actors or sidecars after a reported failure

Severity: medium to high ownership defect. B12 and B14.

Representative proven paths:

- Q3 missile launch creates the actor and, for a grapple, stores it on the player before `qa_builtin_launch_projectile`; a hook or post-change body-read failure returns false without releasing the actor or clearing the player hook (`src/gameplay/q3/missiles.c:154-215`).
- Q3 item spawn creates its actor and private record before collision removal/linking; failures at `items.c:306-314` return with the actor live.
- Q3 portal drop creates and records the portal before destination body/resource/presentation work; failures at `src/gameplay/q3/holdables.c:114-152` leave the portal and possibly player linkage live.
- Native mode object spawn creates/claims an actor and marks the sidecar active before resource lookup, objective binding, planting, and final sync (`src/gameplay/modes/objects.c:235-359`). Those later failures return without rolling back the objective lease, sidecar, authored actor claim, or newly spawned actor.
- Equipment admission marks the actor active before Q3 bind and Q2 grenade configuration (`src/gameplay/modes/equipment.c:91-107`); configuration writes the new selection before the same fallible Q2 call (`:109-131`).

These functions report failure while retaining externally visible mutation. Add bounded rollback using the actual owner/release APIs; do not hide the failure or free authored actors owned elsewhere.

### QM-11: Shared item identity makes simultaneous mode actions ambiguous

Severity: medium. B14 independence.

`qa_modes_publish_items` deduplicates definitions solely by `qa_item_id` across every mode joined by an actor (`src/gameplay/modes/items.c:82-103`). `qa_modes_item_action` has no mode argument and returns after the first active instance whose held flag/relic matches that item (`:3-37`). `mode_object_count` aggregates every carried object with the same item identity, regardless of mode (`src/gameplay/modes/objects.c:32-44`). If two independently selected modes use the same logical flag or rune identity, inventory count and use/drop target whichever lower-slot instance is found first. Bind actions to the object/mode identity or enforce distinct per-instance handles.

## Positive source evidence retained

- B12's implementation is direct native C. `game.c` owns catalog/resource identities, shared combat callbacks and per-game time/RNG; `player.c` composes independently selected character, arsenal, effects, movement and equipment bits; `weapons.c`, `missiles.c`, `holdables.c`, `items.c`, `movers.c`, `death.c`, `feedback.c`, and `view.c` implement substantial base Q3 and Team Arena behavior over shared services. `checkpoint.c` captures typed Q3 state rather than guest/TypeScript machinery.
- B14 covers a broad source union. `map.c:7-118` classifies Q1/Q2/Q3 objective and spawn names, while `:129-362` reports/generates missing cross-map roles. `placement.c` retains family-specific spawn selection. `flags.c`, `relics.c`, `tag.c`, `rogue_tag.c`, `deathball.c`, `arena.c`, `horde.c`, `match.c`, `teams.c`, `vote.c`, and `scoring.c` provide concrete rule paths rather than a generic mode approximation.
- Equipment selection is separate from primary arsenal and movement ownership. `equipment.c:161-240` coordinates holster/lower and Q2 offhand input; `:333-425` supplies post-movement grapple behavior, LMCTF offhand/native hook routing, Q3 pull velocity and per-mechanic gravity without replacing the selected character kernel.
- The applicable B13 transition boundary is sound in shape: `mode_intent` requires one external campaign/match coordinator (`src/gameplay/modes/core.c:81-87`), and next-map/restart/admin/vote paths submit typed intents rather than performing travel. `objectives.c:11-88` gives each objective one leased owner and exposes campaign gates. These pieces should remain while the missing Q3 authored entity layer is added.
- Mode/Q3 checkpoints are typed and validate generations/references. They still need later save-orchestrator and runtime qualification; source presence is not evidence of successful fresh-process restoration.

## Bounded concerns and refutations

- `qa_q3_actor_traits` reports player traits for any `Q3_ACTOR_PLAYER`, including an actor bound only for `QA_Q3_EQUIPMENT` (`src/gameplay/q3/game.c:245-264`). No production caller currently consumes this function, so trait contamination is an integration risk, not a demonstrated live bug. A future composed trait dispatcher must gate character-owned fields by `QA_Q3_CHARACTER` while allowing equipment to attach to a foreign player.
- Q3 invulnerability reflection passes `half_bounce=false` at `missiles.c:421-423`; donor missile impact temporarily clears the half-bounce flag before the reflected bounce. This is not a defect.
- Q3 nail trajectory time is overwritten with current time in both the donor launch path and `missiles.c:173-176`; the apparent loss of the generic -50 ms launch offset is source behavior.
- `mode_match_frame` checks Q3 ties before time/frag/capture limits (`match.c:515-516`), matching donor `team-arena/match.ts:360-395`. Do not “fix” the ordering.
- Fixed authored data tables and ordinary C float operations are permitted. Findings above concern missing behavior, identity/ownership, ordering, and source decisions rather than demanding JavaScript number emulation.

## Complete file coverage inventory

Every assigned production/public file was reviewed. “Reviewed” means its declarations, ownership, control flow, and relevant donor obligations were read; it does not mean runtime-qualified or defect-free. Hashes are the first 16 hexadecimal characters of SHA-256.

| File | Hash | Reviewed behavior / finding |
| --- | --- | --- |
| `include/qa/equipment.h` | `a139bd4849d82925` | Equipment selection/control/state/checkpoint contract; QM-10. |
| `include/qa/game_q3.h` | `f7ddb77d91269517` | Full public Q3 surface, selections, rules and state; QM-01/QM-06. |
| `include/qa/horde.h` | `9570d2381147c6a6` | Horde manager/point/view/checkpoint types. |
| `include/qa/modes.h` | `7ca6e25dec509d10` | Mode/rule/player/object/effect/vote/give public API; QM-02/QM-07/QM-11. |
| `include/qa/modes_map.h` | `1698871fe34372a6` | Cross-map admission/planning contract. |
| `include/qa/modes_save.h` | `f2e7895e87fee8c` | Typed mode/player/object/Horde checkpoint records. |
| `src/gameplay/q3/internal.h` | `01d2878512741d68` | Q3 per-actor sidecars, catalogs, helpers and sequences. |
| `src/gameplay/q3/game.c` | `907852c841259842` | Lifecycle, resource identity, frame dispatch, damage/policy/traits; bounded trait concern. |
| `src/gameplay/q3/player.c` | `a403a002e40ab278` | Selection admission, spawn, environment, timers, movement composition and death reaction. |
| `src/gameplay/q3/weapons.c` | `020a8c47b86788fb` | Q3/TA hitscan, rail, lightning, gauntlet and weapon dispatch. |
| `src/gameplay/q3/missiles.c` | `97c756a059b7657a` | Projectile launch/impact/step, prox/grapple; QM-04/QM-05/QM-10. |
| `src/gameplay/q3/holdables.c` | `9b34866c5a97ecb2` | Teleport/portal/medkit/kamikaze/invulnerability; QM-04/QM-10. |
| `src/gameplay/q3/items.c` | `9a00c1faa0179150` | Catalog, spawn, pickup continuation, respawn/drop/physics/touch; QM-06/QM-08/QM-09/QM-10. |
| `src/gameplay/q3/movers.c` | `de69fd3feb6dae94` | Shared mover binding, team state, blocked/touch/use adapter. |
| `src/gameplay/q3/death.c` | `65fe5d80fbeefea6` | Scoring/death rewards, obituary and death-state selection. |
| `src/gameplay/q3/feedback.c` | `c255fb0ca5989b01` | Damage feedback, pain/world effects and source timers. |
| `src/gameplay/q3/view.c` | `00515a5fe43ae93` | Typed player/missile/item/portal/corpse entity view projection. |
| `src/gameplay/q3/checkpoint.c` | `b0ef1df5b57a298c` | Typed capture/restore validation and resource rebind. |
| `src/gameplay/modes/internal.h` | `d05fef7bbdbf2900` | Per-game/player/instance/object ownership; QM-02. |
| `src/gameplay/modes/core.c` | `cc95495fef986322` | Lifecycle, callbacks, scoring/team, join/frame/release; QM-02. |
| `src/gameplay/modes/arena.c` | `c282f5dc4b6c8cb2` | Obelisk/Tag/DeathBall damage and reactions; QM-03. |
| `src/gameplay/modes/checkpoint.c` | `4d4302962b9f05f5` | Mode/player/object/Horde capture, validation and restore. Restore failures after mutation remain part of QM-10's rollback class. |
| `src/gameplay/modes/commands.c` | `6827f36d4f24e377` | Referee/admin/pause/lock/start/stop/map intent. |
| `src/gameplay/modes/damage.c` | `b3f1f793572ec96e` | Selected-family damage, team/relic modifiers and haste. |
| `src/gameplay/modes/deathball.c` | `217c97adfbea715f` | Ball touch/goals/respawn/killbox; overflow is reported rather than ignored. |
| `src/gameplay/modes/equipment.c` | `bf03aecdbe988ced` | Grapple/offhand selection, input, step, Q3 item projection and checkpoint; QM-10. |
| `src/gameplay/modes/flags.c` | `64cc60f42419afad` | Q1/Q2/Q3/LM flag reset/capture/defense bonuses, including dirty command-created changes. |
| `src/gameplay/modes/give.c` | `2a7783e3750b3dab` | Dirty/untracked CTF/LMCTF/Tag catalog and shared pickup give path; QM-07. |
| `src/gameplay/modes/horde.c` | `419661b04337c395` | Horde waves, squads, placement, loot, keys, scoring, finish and checkpoint. |
| `src/gameplay/modes/items.c` | `e9a63f5d0d98d48` | Published mode inventory definitions and use/drop callback; QM-11. |
| `src/gameplay/modes/map.c` | `f38c85d73a9db401` | Source entity classification and cross-map objective generation. |
| `src/gameplay/modes/match.c` | `b5795a064da93aa2` | Phases, warmup/countdown/intermission/limits/ghosts and typed transition intent. |
| `src/gameplay/modes/objectives.c` | `a84026206cf88e76` | Single-owner objective leases, reads/changes and campaign gates. |
| `src/gameplay/modes/objects.c` | `62160424e2a17daf` | Object identity/spawn/sync/drop/frame/physics, including dirty command-object changes; QM-10/QM-11. |
| `src/gameplay/modes/placement.c` | `b48245900e666e17` | Q1/Q2/Q3/Rogue spawn selection and overflow admission. |
| `src/gameplay/modes/relics.c` | `d1c466c53b8605e` | Rune placement/touch/drop/effects/regen and grapple rules. |
| `src/gameplay/modes/rogue_tag.c` | `8fd35eb2854ead42` | Rogue tag floor/follow/drop/respawn/scoring state machine. |
| `src/gameplay/modes/scoring.c` | `8043af30cf043a54` | Hurt/death/team/objective/Horde scoring and respawn cleanup; QM-02 impact. |
| `src/gameplay/modes/tag.c` | `c32cc3c8676ecaa5` | Tag pickup/death/bonus behavior. |
| `src/gameplay/modes/team_info.c` | `73df4eb9e0bffaf2` | Visible location lookup and source team overlay rows. |
| `src/gameplay/modes/teams.c` | `e12d15674dd24097` | Team changes, observer/follow, suicide and source command rules; QM-02 impact. |
| `src/gameplay/modes/vote.c` | `466edbb7f5f02924` | Source-specific vote admission, counts, thresholds, delay and intent execution; QM-02 impact. |
| `CMakeLists.txt` (ancillary) | `5bd08697a118e1c5` | Q3/mode target source lists and linkage; QM-07. |

Relevant donor files read for comparison include `content/q3/base/game/{item-lifecycle,ground,targets,triggers,misc-spawn,spawn,level,rankings,missile,projectile}.ts`, `content/q3/team-arena/match.ts`, and source mode families under `content/q1/multiplayer`, `content/q2/multiplayer`, and `content/q3/team-arena`. This report does not claim line-by-line equivalence for every donor branch; it records concrete compared behavior and remaining source scope.

Unreviewed assigned production/public files: none. Out-of-lane application composition, campaign implementations outside Q3, network/persistence serialization, presentation, bots, and tests were not reviewed here. Those dependencies still need their assigned audits and later P01 execution.

## Jev judgments

The requested task-specific Jev report checks were run through the installed plugin CLI against this packet. All three returned exit status 2 after reading 101 passages. They are report-honesty checks, not task-acceptance judgments, and none is recorded as an acceptance of B12, B13, or B14.

| Task | Exact summary | Highest recorded flags |
| --- | --- | --- |
| B12 | `claims-done overclaims=0.08`; `stop-reason: 101 passages read`; exit 2 | `EXCUSE 0.73` on the B14 incomplete disposition, `0.67` on the untracked `give.c` inventory row, `0.63` on the missing Q3 entity surface, `0.58` on the rollback-path evidence, and `0.53` on both the Q3 item-team severity and the bounded unreviewed-scope statement. The output also retained `REVIEW` flags classified as `blocked-external`, `allowed-by-rule`, and `excuse`. |
| B13 | `claims-done overclaims=0.06`; `stop-reason: 101 passages read`; exit 2 | `EXCUSE 0.74` on the B14 incomplete disposition, `0.68` on the untracked `give.c` row, `0.59` on the missing Q3 entity surface, `0.54` on both rollback-path evidence and bounded unreviewed scope, `0.50` on the bounded donor comparison, and `0.47` on checkpoint rollback classification. The output also retained `REVIEW` flags in all three categories above. |
| B14 | `claims-done overclaims=0.08`; `stop-reason: 101 passages read`; exit 2 | `EXCUSE 0.73` on the B14 incomplete disposition, `0.65` on the untracked `give.c` row, `0.57` on the missing Q3 entity surface, `0.56` on rollback-path evidence, `0.55` on bounded unreviewed scope, and `0.49` on checkpoint rollback classification. The output also retained `REVIEW` flags in all three categories above. |

The flags largely identify the report's explicit gaps and source-only limits as possible excuses. That is useful skepticism and is preserved here; a low overclaim probability does not turn the implementation evidence into a pass.

The supplemental direct Jev comparisons against each existing graph task also returned exit status 2 and selected `incomplete` with probability 1.00. The complete model, questions, criterion scores, and probabilities are retained in `docs/audit/B12-q3-acceptance.json`, `docs/audit/B13-q3-acceptance.json`, and `docs/audit/B14-q3-acceptance.json`. The source disposition remains incomplete until the findings are corrected, missing source is written, and the corrected source is independently re-reviewed.
