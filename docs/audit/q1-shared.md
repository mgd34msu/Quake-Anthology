# Q1, shared gameplay, movement, and campaign audit

This is a source audit against B08, B09, B10, and B13 in dependency plan 6. It is not a build, test, gameplay, performance, or whole-project completion report. Production edits are frozen except corrections explicitly assigned by the coordinator. The review is still in progress; the coverage inventory below records its limits.

The review began against `c6f7db5c4715416393d63af7e5c994ba59194caf` on `main`, including the frozen dirty Q1 implementation. The acceptance-packet snapshot is `9b1dfaef2d781f1246e244e039b97c01bdc8be1d`. A commit alone does not identify the dirty source: the modified and untracked paths are listed below, and the frozen packets retain source selections and selected file hashes. The coordinator committed the independently reviewed spawn correction as `c9bf514`; `849a3b2` only removed an attribution header from `movement/q2/classic.c` and changed no movement logic.

Reviewer: `/root/input_integration`, session `s_fcef533b1c394442a91432115ae4dd5c`, audit contribution `w_0cd47de1f32948fca5d4ad3a544b65df`, parent `w_357ddd74810a4aa8a3bb84fdd7095fb8`. No project configuration, compilation, tests, executables, generators, or benchmarks were run. Source searches and reads supplied the evidence. The animation import script was read, not executed.

## Criteria and disposition

| Task | Existing criterion | Source evidence and current limit |
| --- | --- | --- |
| B08 | Native C kernels support independent per-player movement with required source cadence, bounds, and numeric decisions. | `movement/common.c` dispatches tagged private movement states; Q1 NQ/QW, Q2 classic/rerelease, and Q3 kernels exist. Q2 classic retains eighth-unit wire state, rerelease accepts float commands, Q3 validates signed-byte integral commands and subdivides source time. Shared entity/pusher/monster/Q3 mover code exists. Original Quake64 remains explicitly rejected; concrete application selection, prediction ownership, and deferred touch integration are not complete. Keep open. |
| B09 | Each mutation has one authority; ordered transforms, observers, replacements, and private state retain required behavior. | `gameplay/operation.c` implements ordered transforms/observers and one replacement continuation; `combat.c` enforces source mutation observation and reaction boundaries; `inventory.c` owns canonical or explicitly bound storage; `pickups.c` checks complete grant authority and exact pickup lifetime. These are substantial implementations. The earlier source-complete claim must not be treated as verified integration: native protection composition and application lifecycle consumers are missing. Review of all bodies and source comparisons is not complete yet. |
| B10 | Full Q1 family roster and behavior are implemented through shared services, including mission-pack and rerelease distinctions. | Native Q1 weapons, projectiles, character state, monsters, pickups, mission controllers, and addon controllers exist. Q1-01 and Q1-02 are corrected in source. Missing authored behavior Q1-04, missing continuations Q1-05, remaining animation comparisons, and application integration prevent a complete-behavior claim. Several new boss files are frozen unfinished work. Keep open. |
| B13 | Campaign code preserves authored obligations when actors or equipment are replaced; transitions have one owner. | `campaign/targets.c` provides one generation-aware registry, typed field reads, source ordering, and deferred-use handoff; `campaign/unit.c` stages immutable departed worlds before publication; Q1 level/finale/source rules and spawn selection exist. Missing authored map classes, Q1 state capture, and replacement-obligation application wiring prevent completion. Keep open. |

## Confirmed defects and missing implementation

### Q1-01: Q1 damage never enables source momentum; cross-policy requests omit knockback

High severity, corrected in source and independently reviewed by the coordinator in `8320e03`. At the frozen packet snapshot, `src/gameplay/q1/runtime.c:185` `combat_context` never set `has_momentum_direction` or `momentum_direction`. `src/gameplay/policies.c:172` only applies Q1 impulse when that flag is set. Searching that snapshot found the declaration and consumption, but no writer. `q1_damage_typed` at `runtime.c:613` also left `qa_damage_request.knockback` zero; `q1_radius_typed` at `runtime.c:659` left `knockback_scale` zero. Thus both the native Q1 momentum branch and Q1 attacks delivered to a policy which consumes the common knockback field lacked their required input. The correction is described below; no runtime result is claimed.

Donor `../quake-typescript/src/content/q1/foundation/entity-services.ts:405` supplies unscaled request knockback and a normalized direction. Its `combatContext` at line 420 derives source momentum from the current projectile or linked inflictor bounds, excluding world damage; donor `world/gameplay/policies.ts:95` applies damage multiplied by eight. The native `.walk` predicate additionally accepts `QA_PHYSICS_STEP`, whereas the donor uses `isPlayer(target)`. Correcting only the missing vector would introduce monster push that the donor did not request. Use the shared target/player classification so foreign players qualify and ordinary STEP monsters do not. Root independently confirmed this finding. No runtime reproduction is claimed.

### Q1-02: Radius damage iterates a changing actor list during synchronous damage callbacks

High severity, corrected in source and independently reviewed by the coordinator in `8320e03`. At the frozen packet snapshot, `runtime.c:659` called `qa_builtin_radius_damage` without a captured candidate list. `src/gameplay/builtin/attacks.c:149` then advanced `qa_actors_next` between damage callbacks. A reaction that created a later-slot damageable actor could add it to the same blast; deletion/reuse could also change which generation was visited. Donor `entity-services.ts:454` takes `host.actors.observations()` first and rechecks those actor identities during the loop. The correction uses reusable snapshots in `q1/queries.c` and `builtin/services.c`, with nested-call ownership and release on both success and error. No runtime result is claimed.

### Q1-03: Rerelease spawn selection rejected a valid source RNG endpoint

Medium severity, corrected by the coordinator; independently re-reviewed in source. `src/campaign/q1/spawn.c:162` originally rejected `random >= 1`, but `src/gameplay/builtin/random.c:34` produces `(word & 32767) / 32767.0f`, including 1. Donor `content/q1/base/rules.ts:61` accepts that endpoint using `floor(random * (count - 1) + 0.5)`. The coordinator changed the guard to `random > 1`. All calls to `random_choice` are reached with a nonempty candidate list: the two preferred passes check `candidates->count`, and the forced fallback checks the original nonzero count has not changed. The corrected index stays in `[0, count-1]`. No build or runtime validation occurred.

### Q1-04: The native authored-entity catalog omits required expansion and addon behaviors

High severity, open missing implementation. `src/gameplay/q1/maps/runtime.c:300` classifies base maps and some Hipnotic entities; the omitted names below have no native registration/implementation in this subsystem. Donor `content/q1/missionpacks/world/index.ts` installs their modules. Concrete examples include:

| Donor module under `content/q1/` | Missing classes or behavior |
| --- | --- |
| `missionpacks/world/hipnotic-spawn.ts` | `func_spawn`, `func_spawn_small`: dormant spawn templates, model/solid/think restoration, multi-spawn/charm, clone placement, fog, and activation. This file was read in full. |
| `missionpacks/world/hipnotic-train.ts` | `func_train2`, `func_bobbingwater`, `func_pushable`. |
| `missionpacks/world/hipnotic-rotate.ts` | `info_rotate`, `path_rotate`, `rotate_object`, `func_rotate_entity`, `func_rotate_door`, `func_movewall`, `func_rotate_train`, `func_clock`. |
| `missionpacks/world/hipnotic-hazards.ts` | `trap_spike_mine`, lightning trap variants, `trap_tesla_coil`, `trap_gods_wrath`, `trap_gravity_well`. A dormant `monster_spikemine` entry is not the trap controller. |
| `missionpacks/world/rogue-time.ts`, `rogue-pendulum.ts`, `rogue-plats.ts` | Time-machine/core entities, `pendulum`, `func_new_plat`, `func_elvtr_button`. |
| `missionpacks/world/rogue-misc.ts`, `rogue-hazards.ts` | `rubble_generator`, `trigger_explosion`, earthquake variants, `buzzsaw`, lightning-trail nodes. |
| `addons/lights.ts`, `rope.ts`, `field-triggers.ts` | `dynamiclight`, `target_lightramp`, `light_candle`, `misc_rope`, `trigger_shelter_portal`. |
| `addons/triggers.ts`, `brushes.ts` | Timed counters, multitouch, corpse cleanup, sacrifice checks, healing/quad, silent teleport/cutscene/skill triggers, explosion repeater, bob/toss/shatter/debris/explode/hurt/fade/model/rotating/breakable brush behaviors. |
| `addons/monsters/ai/targets.ts` | MG3 path-corner occupied/pause state, negative-wait conversion, `target_cancelpause`, and `target_switchpath`. The native base `path` handler lacks these addon transitions. |

This table is a missing-scope inventory, not a claim that every donor branch in those modules has been independently checked. The smaller base/Hipnotic implementation that is present should be retained and completed through the existing shared services. No fallback to the original engine is appropriate.

### Q1-05: Native Q1 private-state checkpointing is not implemented

High severity, open missing implementation. The public Q1 API and source contain no full game capture/restore for actor private state, player timers, monster continuations, native RNG, map continuations, or live door groups. `qa_q1_mg3_progress_restore` restores one narrow persistent progress record. `q1/clone.c` duplicates a native actor for runtime cloning; it is not a whole-game save codec. The donor foundation checkpoint module and provider state-extension registration retain these continuations. `campaign/unit.c` safely retains already encoded world snapshots but cannot fill absent Q1 state. This is a B10/B13/B30 integration dependency, not evidence against immutable campaign snapshot storage.

### Q1-06: Native mixed armor lacks a bound source-profile owner

High severity, open composition gap. `runtime.c:185` fills only `armor.alive`; Q2 profile, edition/CTF behavior, and screen-facing geometry are absent. `gameplay/policies.c:93` sends that context to `qa_combat_absorb`; without a replacement lease, `gameplay/armor.c:65` correctly rejects Q2 regular or powered armor without an explicit Q2 profile. No current native source calls `qa_combat_reserve_protection` or `qa_combat_bind_protection` outside their definitions. Therefore a recipient running the native Q1 policy with Q2 protection has no concrete attached source-profile route today. The donor also requires a Q2 profile (`world/gameplay/armor.ts:15`); weakening that validation would lose source behavior. Complete B15/B34 protection composition with a typed source owner and native callbacks. This is not a finding that the armor formula itself is wrong.

### Q1-07: Original Quake64 movement remains unavailable

Open previously acknowledged product gap. `src/movement/q1/netquake.c:451` rejects `QA_Q1_QUAKE64`. The donor `src/movement/q1/netquake.ts:26` rejects the same unqualified profile. It is an inherited prototype limitation, not a native regression. Selecting another game's movement for Quake64 content is useful interoperability but does not implement the original movement option. B08 reports already retained this as open.

### Q1-08: New native boss sources are not all registered with the build

Open source-integration gap. At the reviewed snapshot, CMake registers `boss_small.c` but not the new `boss_children.c`, `boss_final.c`, `boss_oldnew.c`, `boss_projectiles.c`, or `sphere_points.c`, although frozen Q1 source references their functions. Root owns CMake and the chunk is not handed off as finished. This is based on textual registration review only; no compiler/linker result is claimed.

### Q1-09: The application does not yet consume the completed-looking subsystems

Open B34 integration. `src/main.c` currently exposes help/version/archive listing/BSP inspection and explicitly identifies the baseline as under construction. It has no gameplay session, selected movement/arsenal/character assembly, target graph, campaign travel, or release-fanout consumer. `qa_q1_game_create`, `qa_q1_level_create`, and `qa_q1_spawn_select` have definitions but no application callers. Library source presence is not an integrated game. This is consistent with B34 remaining open and is not a request to run the unfinished executable.

### Q1-10: Overlord destination selection uses host actor slots instead of the client roster

Medium severity, corrected in source and independently reviewed by the coordinator in `490c6a8`. The frozen `src/gameplay/q1/overlord.c:33` walked `qa_actors_next` and took the first actor classified as a player. Donor `content/q1/missionpacks/monsters/overlord.ts:24` takes `host.players()[0]`, whose order is the client roster. Actor retirement/reuse or mixed admission can make these orders differ, changing the origin and facing used to select the destination. The correction uses existing `q1_snapshot_players`/`qa_builtin_players` storage, reads the first roster actor without an extra native-player filter, and uses the shared world actor only when the roster is empty. A missing body retains the donor zero-vector fallback. Its two callers in Overlord teleport and Morph child placement now propagate acquisition failure through a typed result instead of silently using an empty roster. Marker iteration still uses host slots, whereas donor `game.entities` preserves insertion order; dynamic marker reuse remains an explicitly open ordering concern, separate from the corrected client selection.

## Refuted or bounded concerns

- Shared cargo grants accepting a backpack even when its mapped counters cannot increase initially looked suspicious. Donor `world/gameplay/pickups.ts:185` `giveResolvedCargo` returns true after the resolved grants, matching `qa_supply_cargo`'s weapon-offer path. Do not alter this merely to make acceptance mean a numerical increase. A replacement delegate can still refuse its own pickup.
- Unsupported-error strings alone do not establish missing controllers. The projectile dispatcher routes expansion/addon kinds before its base switch, and the mission action dispatcher delegates to concrete class controllers. Each allegedly missing case needs a reachable registered action and absent handler.
- Native float arithmetic is an explicit project choice. Removing TypeScript rounding wrappers is not itself a defect. Wire quantization, command/time integers, ordering, ownership, and authored decisions still need preservation.
- Q3 mover rollback deliberately restores shared body position as well as native trajectory bases. The prior B08 handoff records coordinator approval for that difference from the donor. It remains a later qualification scenario, not an automatically reverted fidelity defect.
- The duplicated X/Y side component in Q1 lightning rays matches donor `base/projectiles.ts:232` and the mission/addon helpers. The rare native player death-animation endpoint error also matches donor `base/player.ts:227-228`. These are not established C regressions. Fixing inherited behavior, if desired, needs a separate original-behavior decision.
- Shub zombie spawning deliberately counts eligible points and indexes the original unfiltered list (`addons/monsters/bosses/szombie.ts:93–98`); the native sequence matches. The Old One sphere-chunk callback deliberately does not reschedule itself (`oldnew-projectiles.ts`, `sphere_chunk`). Final-boss target cycling deliberately sorts client actor slots (`final.ts:62–63`); this differs from Overlord's first-client-roster rule and is not the same defect.

## Source coverage inventory

Coverage codes: **D** means function bodies/control flow read in detail; **P** means targeted bodies plus structural/source searches, with remaining branches not fully compared; **I** means inventoried only and still requiring substantive review. None means a runtime pass. A D file still needs the relevant later integration/runtime qualification.

Every production file in the assigned directories is inventoried below. P entries received substantive targeted-body review, not only a name search. They still contain unreviewed branches; the inventory does not turn that bounded review into full behavioral qualification. Generated frame rows and remaining controller branches need subsequent comparison. No whole-lane approval is issued.

All paths below are relative to the repository root. The following movement files received D review for their native dispatch, cadence, state ownership, traces and callback paths; donor branch-for-branch parity remains unqualified:

```text
D src/movement/common.c
D src/movement/internal.h
D src/movement/prediction.c
D src/movement/entity.c
D src/movement/entity/internal.h
D src/movement/entity/monsters.c
D src/movement/entity/pushers.c
D src/movement/entity/q3_mover.c
D src/movement/entity/trajectory.c
D src/movement/q1/common.c
D src/movement/q1/common.h
D src/movement/q1/netquake.c
D src/movement/q1/quakeworld.c
D src/movement/q2/classic.c
D src/movement/q2/rerelease.c
D src/movement/q3/local.h
D src/movement/q3/move.c
D src/movement/q3/slide.c
```

Shared mutation services were read across operation dispatch, source publication, pickup scopes, leases, canonical inventory, provenance and staged protection. Campaign files were read across travel, stage/commit ownership, authored target dispatch, Q1 finales and spawn policies:

```text
D src/gameplay/armor.c
D src/gameplay/combat.c
D src/gameplay/combat_internal.h
D src/gameplay/inventory.c
D src/gameplay/inventory_internal.h
D src/gameplay/operation.c
D src/gameplay/pickups.c
D src/gameplay/policies.c
D src/gameplay/provenance.c
D src/gameplay/builtin/attacks.c
D src/gameplay/builtin/random.c
D src/gameplay/builtin/services.c
D src/campaign/targets.c
D src/campaign/travel.c
D src/campaign/unit.c
D src/campaign/q1/finales.c
D src/campaign/q1/level.c
D src/campaign/q1/sources.c
D src/campaign/q1/spawn.c
```

Q1 coverage follows concrete native entrypoints and synchronous callbacks. P controller files were read at their spawn/admission, selected attack/reaction or continuation boundaries; unlisted internal branches remain unreviewed. `frames.c`/`frame_actions.h` were checked structurally against the importer and dispatch model; individual animation rows have not all been compared with the donor. `import_frames.py` was read in full and was never run.

```text
D src/gameplay/q1/addon_monsters.c
D src/gameplay/q1/armagon.c
D src/gameplay/q1/boss_children.c
D src/gameplay/q1/boss_final.c
D src/gameplay/q1/boss_internal.h
D src/gameplay/q1/boss_oldnew.c
D src/gameplay/q1/boss_projectiles.c
D src/gameplay/q1/boss_small.c
D src/gameplay/q1/boss_types.h
D src/gameplay/q1/character.c
D src/gameplay/q1/charm.c
D src/gameplay/q1/clone.c
D src/gameplay/q1/demodog.c
D src/gameplay/q1/dragon.c
D src/gameplay/q1/drops.c
D src/gameplay/q1/electric.c
D src/gameplay/q1/expansion_weapons.c
D src/gameplay/q1/explosions.c
P src/gameplay/q1/frame_actions.h
P src/gameplay/q1/frames.c
D src/gameplay/q1/grapple.c
D src/gameplay/q1/grapple_weapon.c
D src/gameplay/q1/gremlin.c
D src/gameplay/q1/gremlin_ai.c
D src/gameplay/q1/gremlin_weapons.c
D src/gameplay/q1/heavy.c
D src/gameplay/q1/hipnotic_weapons.c
D src/gameplay/q1/horde.c
D src/gameplay/q1/import_frames.py
D src/gameplay/q1/infected.c
D src/gameplay/q1/internal.h
D src/gameplay/q1/lavaman.c
D src/gameplay/q1/maps/addon_misc.c
D src/gameplay/q1/maps/bosses.c
D src/gameplay/q1/maps/hipnotic_misc.c
D src/gameplay/q1/maps/hipnotic_particles.c
D src/gameplay/q1/maps/internal.h
D src/gameplay/q1/maps/movers.c
D src/gameplay/q1/maps/observer.c
D src/gameplay/q1/maps/runtime.c
D src/gameplay/q1/maps/special.c
D src/gameplay/q1/maps/trains.c
D src/gameplay/q1/maps/triggers.c
D src/gameplay/q1/mg3_commands.c
D src/gameplay/q1/mg3_progress.c
D src/gameplay/q1/mg3_weapons.c
D src/gameplay/q1/mission_monsters.c
D src/gameplay/q1/monster_actions.c
D src/gameplay/q1/monster_lifecycle.c
D src/gameplay/q1/monsters.c
D src/gameplay/q1/morph.c
D src/gameplay/q1/obituary.c
D src/gameplay/q1/overlord.c
D src/gameplay/q1/pickups.c
D src/gameplay/q1/powers.c
D src/gameplay/q1/projectiles.c
D src/gameplay/q1/queries.c
D src/gameplay/q1/rocket_ogre.c
D src/gameplay/q1/rogue_weapons.c
D src/gameplay/q1/runtime.c
D src/gameplay/q1/scourge.c
D src/gameplay/q1/selection.c
D src/gameplay/q1/species.c
D src/gameplay/q1/sphere_points.c
D src/gameplay/q1/teleport.c
D src/gameplay/q1/weapons.c
D src/gameplay/q1/wrath.c
```

Related contracts and integration evidence:

```text
D include/qa/builtin.h
D include/qa/campaign.h
D include/qa/campaign_q1.h
D include/qa/campaign_q1_sources.h
D include/qa/game_q1.h
D include/qa/game_q1_maps.h
D include/qa/gameplay.h
D include/qa/inventory.h
D include/qa/movement.h
D include/qa/operation.h
D include/qa/physics.h
D include/qa/targets.h
D src/main.c
D CMakeLists.txt
I tests/archive_test.c
I tests/bsp_test.c
I tests/core_test.c
I tests/image_test.c
I tests/model_test.c
I tests/vfs_test.c
```

The six test files were searched for the reviewed APIs and their test entrypoints; they cover core/assets rather than these gameplay/movement/campaign services. Their unrelated bodies are explicitly unreviewed here. No relevant subsystem test source or executed test result was found. This is an evidence limit, not permission to run tests during the source-only phase.

Dirty Q1 snapshot paths: modified `boss_small.c`, `frame_actions.h`, `frames.c`, `import_frames.py`, `internal.h`, `monster_lifecycle.c`, `monsters.c`, `powers.c`, `projectiles.c`, `queries.c`, `runtime.c`, `species.c`; untracked `boss_children.c`, `boss_final.c`, `boss_internal.h`, `boss_oldnew.c`, `boss_projectiles.c`, `boss_types.h`, `sphere_points.c`. All are under `src/gameplay/q1/`. Their frozen contents remain part of the review, independent of HEAD.

## Jev judgments


Plan 6 was read and acknowledged through a new checked process. Earlier B08/B09/B10/B13 work histories were read; their available historical entries are reports, not proof that the implementation was inspected by Jev. B08, B10, and B13 owner reports already state remaining source/integration work. B09's owner report marks source completion while explicitly assigning later native/application consumers elsewhere.

Actual checks used the installed plugin's `scripts/vibecheck-jev.sh check report --project quake-anthology --task Bxx -`, with the evidence report supplied on stdin. All four returned exit 2, **flagged**, not approval. `src/jev/tools/check.ts` was read: this command judges report claims and stopping language; it does not inspect repository contents. No independent code-review verdict or model identity is inferred from these outputs.

| Task | Exact claims-done line | Exact stop-reason verdict | Exit |
| --- | --- | --- | --- |
| B08 | `claims-done overclaims=0.05` | `REVIEW allowed-by-rule 0.16` | 2 |
| B09 | `claims-done overclaims=0.07` | `REVIEW allowed-by-rule 0.11` | 2 |
| B10 | `claims-done overclaims=0.06` | `EXCUSE 0.45` | 2 |
| B13 | `claims-done overclaims=0.03` | `REVIEW excuse 0.22` | 2 |

Each output ended `stop-reason: 1 passages read`. These reports stated ongoing source audit, named concrete implementations and missing consumers, listed the confirmed findings, and explicitly left their tasks open. The B10 `EXCUSE` judgment is retained without relabeling it as a pass; the source audit and correction work continue.

The coordinator subsequently supplied `judge-task.mjs` to judge implementation evidence against the original task goal and criteria. Four explicit source packets were submitted, each under its 70,000-character limit without truncation. This differs from the plugin report-language check above. The request explicitly says to judge the work rather than credit an honest admission of incompleteness, and to respect the source-only phase.

| Task | Frozen packet | Characters | Exact response | Criterion `noul` | Exit |
| --- | --- | ---: | --- | ---: | ---: |
| B08 | [B08-q1-packet.md](B08-q1-packet.md) | 59,292 | [incomplete](B08-q1-acceptance.json) | 0.18 | 2 |
| B09 | [B09-q1-packet.md](B09-q1-packet.md) | 61,096 | [incomplete](B09-q1-acceptance.json) | 0.14 | 2 |
| B10 | [B10-q1-packet.md](B10-q1-packet.md) | 60,951 | [incomplete](B10-q1-acceptance.json) | 0.02 | 2 |
| B13 | [B13-q1-packet.md](B13-q1-packet.md) | 60,429 | [incomplete](B13-q1-acceptance.json) | 0.03 | 2 |

All four responses identify model `jev-1.13.0`, plan revision 6, and graph SHA256 `915c4c50da8fcb60ed206f09f2d325eb7755f4c6f3f48b1c3d97d83d2eb98bae`. The exact JSON also retains evidence hashes and timestamps. None is acceptance. The packets are bounded selections, and their omissions remain evidence limits; the source defects and missing consumers remain work to correct. Subsequent fixes must not rewrite these pre-correction packets or their results.

## Remaining written-source review

The inventory is not completion of AUDIT. The following conservative comparison ranges remain open. Ranges refer to the acceptance-packet snapshot; all paths in this table are under `src/gameplay/q1/`. The action enum has now been read, but its full row-to-donor comparison remains open with the animation data. This is 8,535 lines, including 8,153 animation rows/index lines. Full source/donor behavioral comparison is additional work.

| File | Remaining source ranges |
| --- | --- |
| `frame_actions.h` | 86–467 |
| `frames.c` | 101–7299, 7346–8299 |

The full native bodies of `maps/{bosses,movers,special,triggers}.c`, `internal.h`, and `boss_{children,final,oldnew,projectiles,small}.c` are now read. All 100 vectors in `sphere_points.c` were compared with donor `addons/monsters/bosses/sphere-points.ts`. This closes their written-source reread ranges, not all later integration qualification. Full `CMakeLists.txt` coverage is shared with [foundations.md](foundations.md) at `fd23b0bce3808dd9e66554ee86be7d2dc49eb772`; this lane additionally read its entire subsequent diff, consisting only of the four-line VFS test registration. The presentation reviewer did not claim full `movement.h` coverage, so this lane read that header in full itself, at SHA256 `eec3f8e39508e403db5f6f7b7d117f0e73f602fd4ddb351ffa60c42620c6ed85`. The six test files listed I remain unrelated-body exclusions in this lane; foundations and presentation explicitly record their full source coverage. No generated frame rows are declared reviewed merely because their importer or dispatch was read.

All other native Q1 controller, weapon, pickup, obituary and lifecycle bodies in the inventory have now been read. This includes the remainder of Armagon, Dragon, Lavaman, Gremlin, heavy-monster and Morph behavior, base monster dispatch, dropped items and both mission arsenals. Rogue grapple's three otherwise unused random draws were compared with `missionpacks/grapple.ts:71` and retained as source behavior.

The independent animation comparison uses native `frames.c` SHA256 `badd71a8b0d93a78eb2e4f8420e4cf547a6c5c38d04a92cb8cfebd0cc7d12d5e` and `frame_actions.h` SHA256 `155bf0514cb22a338b9d2f3bc488fe8c4cee6b6ea82c5bad8c4500317602d841`. Native frame indices 0–1495, at `frames.c:3429–4924`, and their ordered operations 0–1118 have now been manually compared. This covers all donor `base/frames.ts:5–1311` records, then all `missionpacks/monsters/tables/{grunt,rottweiler}.ts` records. Checked fields are pose numbers, next-frame targets, operation offsets/counts, ordered AI/actions, sound channel/attenuation/comparison/chance, solid removal, and light styles. No mismatch was found in this bounded comparison. Remaining frame indices 1496–3895, their operations, the sorted name index, and remapping pairs remain open. The conservative snapshot table above is retained until those comparisons close.

A textual action-reference check found only the intentionally range-dispatched Chthon and final-boss action members without separate literal C references, plus the enum count sentinel. The corresponding range branches were read in `monster_actions.c` and `boss_final.c`. This establishes dispatch references, not correctness of the still-uncompared donor rows. No comparison parser or generator was executed.

The next manual pass extended that comparison through native frame index 2756 and operation 2165, ending at `frames.c:6185`. Every remaining mission-pack table in the importer list was compared: Eel, Phantom Swordsman, Mummy, Scourge, Decoy, Wrath, Lavaman, Overlord, Morph, Dragon, Armagon and Gremlin. No mismatch was found in these records. The open frame rows are now indices 2757–3895, starting at `frames.c:6186`, plus operations beginning at 2166, the sorted name index and addon remapping pairs. The `gremlin_flip7` target points to virtual `gremlin_gib` index 3889; its virtual-row semantics will be checked with that final group. These are manual source comparisons only.

The parent approved a focused correction in `runtime.c` after the frozen acceptance packets, independently reviewed it, and committed its exact hunks as `8320e03`, preserving unrelated boss work. It now supplies shared target/player classification, current projectile or linked inflictor momentum geometry with world exclusion, normalized request direction and amount-based knockback, and a retained generation-aware radius candidate snapshot. This does not close the remaining AUDIT ranges or change the recorded incomplete Jev outcomes.

The subsequent Q1-10 correction was independently reviewed and committed as `490c6a8`, including the retained player-roster accessor in `queries.c`, its necessary header declarations, Overlord selection and its Morph caller. Source SHA256 values before independent review: `overlord.c` = `b84c0f51c8ff22036224716d9d0919d214cc7292a5445fcce7c6573ee8cd7ae9`; `morph.c` = `047694ceb94ff6fd977c6d26459fa48d0faa9215c58bd21b2702938839995f46`. No packet was regenerated and no build or executable was run.
