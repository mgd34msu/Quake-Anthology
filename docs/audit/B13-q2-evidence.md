# Q2 independent source audit

Status: source coverage complete; implementation incomplete. B11 and the Q2 part of B13 remain open. No compilation, tests, game execution, or benchmarks were performed.

The review covers the current working tree over `c6f7db5c4715416393d63af7e5c994ba59194caf`, including uncommitted and untracked Q2 production files. This reviewer authored Q1, not the Q2 implementation. The SHA-256 inventory below fixes the initial snapshot; changed files require re-review. Donor revision: `5d6c6e311db6749c0b8f8ffca53ebe2bae69b24e`.

The governing ledger is plan 6. Its AUDIT addition does not relax the existing B11/B13 goals or criteria. The initial local dependencies file said plan 5; B11 and B13 wording matched plan 6. The original task goals and criteria below remain the acceptance contract.

## Existing criteria

B11 goal: Implement Anthology Q2 classic, expansions, rerelease, monsters, weapons, items, and source gameplay in native C.

B11 criterion: Full Q2 family roster and behavior use compiled gameplay; stock and rerelease selections do not invoke CPU emulation.

B13 goal: Implement entity target graphs, mission gates, keys, sigils, bosses, hubs, revisits, authored mechanisms, and travel.

B13 criterion: Campaign code preserves authored obligations when actors or equipment are replaced; transitions have one owner.

This report judges the Q2 contribution to B13; other campaign families and the transition owner are covered by other lanes. Source evidence cannot establish runtime fidelity or performance. Missing production consumers remain implementation gaps, even when an interface exists.

## Findings

### Q2-01. Monster campaign lifecycle is missing

Severity: high. B11 and B13.

`monsters/core.c:1251` (`qa_q2_monster_spawn`) initializes state, bodies, combat, and an animation deadline. It has no source startup continuation that counts the encounter, resolves `target` into a path corner or `point_combat`, drops walkers to the floor, or calls the authored mission owner. `qa_q2_monster_spawn_options` in `include/qa/game_q2.h:208` contains no mission callback or authored route/death/drop contract. `entities/lifecycle.c:156` (`qa_q2_entity_use`) routes items and map mechanisms but does not dispatch monster use.

`monsters/combat.c:2137` (`q2m_die`) handles native death animation and gibs, but does not perform the source first-death mission/counter notification, authored `item` drop, or `deathtarget`/`target` dispatch. Gib branches return before the ordinary `QA_BUILTIN_DEATH` event, so a presentation event cannot supply a uniform substitute. A source search of all Q2 production files found no monster total/kill counter or mission consumer. Other map counters only track goals/secrets.

Donor evidence: `src/content/q2/foundation/monsters/index.ts:449` counts admission or calls `mission.spawned`; `:461` implements startup and route selection; `:625` wraps first death; `:635-651` counts kills, drops authored items, and dispatches mission/death targets. These are actual source behavior, not optional presentation. Required fix: one typed authored mission owner and concrete native admission/start/use/death calls, retaining obligations for replacement actors. Existing B34 integration is still pending and does not close this missing native contract.

### Q2-02. Triggered monsters materialize immediately without source placement

Severity: high. B11 and B13.

`monsters/core.c:265` (`activate_triggered`) clears the triggered state, sets collision, links, and acquires the supplied activator synchronously. `qa_q2_monster_action:780` calls it immediately for USE. It never schedules the classic 0.1-second or rerelease frame delay, runs the source triggered-monster placement/killbox, or applies the source player/notarget/ambush filtering before acquisition. Donor `foundation/monsters/index.ts:180` schedules `sourceTriggerSpawn`; `:523` performs placement and subsequent startup. Thus a trigger can make a monster solid inside another actor and change callback order. This requires a retained typed spawn continuation, not an unconditional direct wake.

### Q2-03. Monster trait edits latch composed invulnerability into base state

Severity: high. B11 and shared composition.

`monsters/core.c:36` (`q2m_refresh`) fills `context.combat` using `qa_combat_read`. That shared API ORs provider protection into `invulnerable` in `src/gameplay/combat.c:96-102`. `monsters/actions.c:235` (`set_duck`) then changes only `can_take_damage` and writes the entire composed value with `qa_combat_set_traits`. Similar writes occur in `core.c:286,326,886`, death paths in `monsters/combat.c`, projectile trait changes in `gibs.c:99`, `mines.c:86`, and `nuke.c:37`, map damageability in `entities/scenery.c:24`, and `player/death.c:309`, which reuses the composed read taken at `:169`. The shared setter writes `traits->invulnerable` to authoritative base state at `src/gameplay/combat.c:405`. A timed or mod protection active during duck therefore persists after its owner expires. Required fix: read uncomposed traits with `qa_combat_read_traits` for every trait mutation and change only the intended fields; preserve composed reads for gameplay decisions.

### Q2-04. Native console pickup helper has no implementation

Severity: high. B11.

`items/internal.h:70` declares `q2_item_console_pickup`; `player/commands.c:365,400,481` calls it for `give all`, `give power shield`, and individual non-ammo pickup grants. A whole-tree C/header symbol search finds no definition. Donor `base/player/commands.ts:266-271` actually creates a temporary item, spawns it, runs its ordinary touch/pickup continuation, then removes it. The C command path currently has no such function body. Required fix: implement the ordinary pickup route, preserving shared replacement callbacks and item presentation/side effects; the direct start-item grant is not an equivalent replacement.

Refuted hypothesis: `qa_q2_item_give` bypassing the ordinary touch route for starting items matches donor `foundation/items.ts:222-249`, which directly calls grant and finish. This is not recorded as a defect.

### Q2-05. Player-wide native operations omit foreign character providers

Severity: high. B11 and B13 interoperability criterion.

`player/obituary.c:78-80` resolves the attacker only through Q2's private actor/client state and discards any actor without an admitted Q2 client. Thus a valid Q1/Q3-character attacker is treated as absent when the Q2 native obituary selects the score recipient. `player/intermission.c:125-158` clears end-unit cooperative Q2 keys only on Q2 clients. `player/death.c:263-276` decides whether all cooperative players died from the same private set, so a surviving foreign character cannot prevent its restart decision. `player/commands.c:44-77,232-246` similarly builds scoreboards and delivers chat only to native Q2 clients; `player/spectator.c:16-31,52-56,198-237` restricts chase and squad respawn candidates to that set. The shared player roster already exists and is used by `qa_q2_players_camera`, item admission, and spawn-distance selection, so this is not a lack of common actor identity.

These source-local assumptions also exist in portions of the prototype; copying them does not satisfy the required independent character/arsenal/mode composition. Required fix: shared roster/identity/team/character lifecycle queries and selected-provider hooks for per-player actions. B34 may route them, but current native APIs must not silently omit foreign participants. Q2 cause text may retain source fallbacks; shared scoring must retain the actual attacker identity.

### Q2-06. Rerelease parasite proboscis is replaced with immediate melee

Severity: high. B11.

`monsters/actions.c:3857-3864` treats `parasite_fire_proboscis` as an immediate 2-damage melee operation. No Q2 C retained proboscis entity/state exists. The wait and pull-wait callbacks fall through to no-op returns at `:4136-4144`; retract/break callbacks match the generic `break_` sound classification instead of operating a projectile. Donor `rerelease/monsters/base-variants/parasite.ts:66-81` explicitly fires a retained proboscis, loops drain frames until its state changes, retracts it, and coordinates break/pull waits. Required fix: native projectile/owner state and exact callback transitions over shared physics/combat, not callback-name approximations.

### Q2-07. Rerelease monster forced interpolation fields are dropped

Severity: medium. B11 source rendering behavior.

The C frame data contains nonnegative `lerp_frame` values (`monsters/moves.c:1596,1616,18272,18279`), but `monsters/core.c:140-244` never reads that field while advancing/dispatching frames. `present_animation` (`:120`) and the public `qa_q2_monster_view` have no old-frame/render-flag projection for it. Donor `foundation/monsters/index.ts:620` sets `oldFrame` and render flag `1 << 22` for those frames. Required fix: retain and publish the source interpolation override, including checkpoint state where it persists. This is missing source functionality, independent of later visual polish.

### Q2-08. Blocked doors/platforms pass a null output pointer

Severity: high. B11 and B13. Corrected and independently source-reviewed by the root agent in commit `9b1dfaef2d781f1246e244e039b97c01bdc8be1d`. Runtime validation remains deferred.

Initial `projectiles.c:11` (`q2_target_creature`) unconditionally assigns `*player = false` and writes through the pointer again during classification. `entities/doors.c:434` and `entities/brushes.c:594` pass NULL from their ordinary blocked callbacks. This is a direct null write whenever these callbacks run. The focused correction supplies a local discarded result when callers do not request player classification. No runtime validation has been performed.

### Q2-09. Nuke blinding retains a pointer across scratch reallocation

Severity: high. B11. Corrected and independently source-reviewed by the root agent in commit `9b1dfaef2d781f1246e244e039b97c01bdc8be1d`. Runtime validation remains deferred.

Initial `nuke.c:47` saves `snapshot->sort` as `blinded`, then damages nearby actors. Those callbacks can spawn actors and grow the shared registry. At `:86`, `qa_builtin_players` reserves against the new registry capacity; `builtin/services.c:53-72` frees and replaces snapshot storage on growth, while copying only the `ids` portion. The following `blinded[i]` accesses therefore use freed storage. The focused correction acquires two independent retained invocation scratch frames: damage/blinded state remains in the first, and the subsequent ordered player query uses the second. Both frames remain active across nested calls and are released on every outer return. No runtime validation has been performed.

### Q2-10. Classic killbox can report success while another foreign overlap remains

Severity: high. B11 and B13. Corrected and independently source-reviewed by the root agent in commit `9b1dfaef2d781f1246e244e039b97c01bdc8be1d`. Runtime validation remains deferred.

Initial `entities/killbox.c:33-40` handles a foreign victim that survives as a nonsolid corpse by retracing once and returning success whenever the hit differs from that victim. A second blocking actor is therefore left alive while the caller treats placement as clear; a world-solid result could also be accepted. The native-victim branch continues its loop. The focused correction continues for a different actor and requires a genuinely non-solid terminal trace, preserving damage order. Donor `base/player/spawns.ts:99-115` establishes the repeat-until-clear source loop. The prototype itself rejects a surviving foreign victim unconditionally; the C integration must use shared collision evidence rather than discard foreign capabilities.

Refuted hypothesis: rerelease killbox's registry-order traversal matches donor `rerelease/killbox.ts:8`; it is not required to use the spatial query order used by Q3 radius damage.

### Q2-11. Absent authored text reaches strlen as NULL

Severity: high. B13.

`entities/lifecycle.c:71` stores `q2_field_id(..., "message")`, which returns zero for an absent field (`entities/state.c:208-214`). `qa_strings_cstr(...,0)` returns NULL (`src/core/strings.c:108-114`). `entities/target_effects.c:327` calls `strlen(message)` while validating a `target_lightramp`, and `entities/scenery.c:657` does the same when using a `target_string`. The donor rejects a missing/invalid lightramp message safely (`base/entities/targets.ts:145-150`) and permits empty target strings. Native optional text must preserve empty-string semantics at these consumers; the existing `q2_field_text` already supplies that rule for authored lookups. Related trigger classname comparisons (`entities/triggers.c:338,361`) also assume optional actor traits always supply a nonzero classname.

### Q2-12. Trap gib capture reuses undersized scratch storage

Severity: high. B11. Focused correction written; root independent source review pending.

Initial `gibs.c:373-380` acquires a retained scratch frame and copies every physics-bound native actor into its `snapshot.ids`, without reserving current capacity. `ballistics.c:279-299` reserves only when creating a new frame. Reusing an inactive frame after registry growth can therefore write past the retained ids capacity. The correction reserves `g->capacity` before the copy and deactivates the frame if reserve fails; no nested callback runs between reservation and capture. The frame stays active through `trap_capture_run`, so nested operations cannot reuse it. This matches the existing explicit-reserve contract in `monsters/combat.c:2030` (`kill_widow2_stalkers`). No runtime validation was performed.

### Q2-13. Medic and Widow summons collapse distinct source behavior

Severity: high. B11.

`monsters/actions.c:2147` (`spawn_action`), reached by the substring fallback at `:4042`, handles medic start/determine/grow callbacks by toggling steering and emitting one generic event at the parent origin. Finish spawns one infantry 72 units forward. Widow ready/check callbacks similarly emit a generic event or spawn one stalker forward. `monsters/combat.c:2412` (`q2m_spawn_reinforcement`) creates that actor directly without the source spawn-point and floor-fit checks and accounts for every rerelease medic child as one slot.

Donor `rerelease/monsters/base-variants/medic.ts:219-305` retains selected reinforcement classes and their strengths, budgets remaining slots, probes several placements including a rear fallback, controls source hold/next frames, grows effects at accepted placements, and initializes each accepted child's source state/target. Donor `missionpacks/monsters/widow/common.ts:31-48` probes two lateral stalker placements, validates grounded placement, runs immediate startup and source attack selection, and accounts for each successful child. Those behavior paths are absent from the generic native fallback. Carrier has separate implemented callbacks earlier in the dispatcher; this finding does not claim all carrier logic is missing.

### Q2-14. Common monster AI drops authored movement and continuation rules

Severity: high. B11 and authored route continuation in B13.

`monsters/ai.c:2361` (`q2m_run_ai`) ignores nonzero authored distance for STAND and TURN. STAND never performs the source `pauseTime` expiry transition to walking, and TURN only changes yaw, omitting source target acquisition. SOLDIER_MOVE faces the enemy and walks, but omits the donor prone-shot eligibility/`soldier_stand_up` dispatch. The differences are visible directly against `foundation/monsters/ai.ts:260-331`. Nonzero STAND distances are present in native frame data (`moves.c:531-536`), so these are executable source decisions, not hypothetical fields. Native charge also reverses failure handling: `if (!q2m_face_enemy(...) || !q2m_alive(...)) return q2m_alive(...)` reports success for an actual face/yaw error while the actor remains live. Required fix: implement each shared AI state's actual source behavior over the existing physics and perception services, preserving source edition/product conditions.

These findings establish incomplete implementation. Reviewed paths without findings are not a claim of exhaustive behavioral parity.

## Criterion evidence

| Existing criterion | Concrete implemented evidence | Missing or contradicted evidence | Disposition |
| --- | --- | --- | --- |
| B11: Full Q2 family roster and behavior use compiled gameplay; stock and rerelease selections do not invoke CPU emulation. | `game.c:239` (`qa_q2_create`) installs a native session component and initializes native definitions/items/player/entities/monsters; `game.c:284` binds the component to `qa_session_add`. `monsters/roster.c` supplies edition/product definitions, and `core.c:1251` admits native monsters. `weapon_frame.c` and `weapon_fire.c` implement native frame/fire continuations; `projectiles.c`, `mines.c`, and `nuke.c` retain native projectile state over shared world/physics/combat. `player/frame.c:15` calls `qa_q2_weapon_tick`; this lane has no CPU-emulation stock-game execution path. | Q2-01/02/04/06/07/13/14 prove missing source lifecycle, callable implementation, retained parasite behavior, interpolation, summoning, and AI. Q2-03/05 contradict protection ownership and mixed-provider participation. Corrected Q2-08/09/10/12 do not repair those feature gaps. Application selection/routing remains a B34 integration obligation. | **Incomplete**. Native C architecture is present; full behavior is not. |
| B13: Campaign code preserves authored obligations when actors or equipment are replaced; transitions have one owner. | `entities/lifecycle.c` supplies native spawn/use/touch/think dispatch; `entities/state.c` binds authored target data to shared target lookup/use; `entities/doors.c`, `trains.c`, `triggers.c`, `routes.c`, `targets.c`, and rerelease/Q64 modules contain real authored mechanics. Shared item/target hooks are callable integration boundaries. | Q2-01/02 omit monster startup, target/use and mission/death obligations. Q2-05 loses foreign-player participation in cooperative end-unit/restart rules. Q2-11 permits null text to reach ordinary authored uses. Q2-14 loses pause-to-walk route continuation. This Q2 lane does not independently establish a complete replacement-obligation owner or single assembled travel owner; those require the campaign/application lanes as well as these corrections. | **Incomplete for Q2**, and insufficient evidence here for whole-project B13 acceptance. |

The criterion comparison uses the existing graph without narrowing its goals. Public hooks, a matching roster, and immutable data alone do not satisfy connected behavior. Deferred compilation/runtime work is not the reason for these incomplete dispositions; concrete source gaps are.

## Coverage method and limits

All 84 initial Q2 production/header files in the inventory were covered. All implementation logic and public/private contracts were read. Immutable `moves.c` arrays received a complete read-only structural/data comparison rather than a claim of manually reading every repetitive initializer: all 70 move sets, 1,031 move descriptors, 15,724 frames, and 1,875 actions matched their donor fields, normalized float values, callback order, next-frame sentinel and per-array offsets/counts. A complete expression comparison covered all 212 classic and 290 rerelease muzzle vectors. No project code or generator executed; these were text parsing operations.

The donor/C frame data share 14 descriptors whose array length differs from `lastFrame-firstFrame+1` (including classic floater activate and boss2 fidget). They need source-intent resolution before full fidelity acceptance; matching malformed/extra donor data is not behavioral proof. Native `q2m_animation` bounds-checks against the entire flattened set, whereas the donor indexes within the individual move. Reachability and the intended corrections are not established by this packet.

Unreviewed C files in this assigned initial inventory: **none**. Donor comparison was targeted to criteria and concrete behavior paths, not an exhaustive proof of every branch in every TypeScript file. In particular the complete source policy matrix for ballistic variants and LMCTF plasma remains unproven despite full C-file review. Whole-save assembly, network codecs, shared physics internals, modes, rendering, compat loaders and B34 application wiring belong to other lanes; this packet does not silently count those as reviewed or complete. No build, compilation, tests, gameplay, performance measurements, or runtime acceptance are claimed.

## Jev judgments

Not yet requested. Ledger progress is not a judgment.

## File coverage and initial snapshot

Initial hashes are preserved below. Coverage means source inspection or the explicitly stated immutable-data comparison, not execution or a parity pass.

| File | Lines | SHA-256 | Coverage |
| --- | ---: | --- | --- |
| `include/qa/game_q2.h` | 381 | `4412a441f5cae1d51d763b8e852db8765748ff6ab7517fdb73155f1e3ef677ee` | Read public contracts, state and checkpoints; findings Q2-01/02 |
| `include/qa/game_q2_entities.h` | 409 | `cb3d822e83e07a9de9a5433c5ef6e5c03b18e359e0a68e10a53b7c94f5b7531b` | Read complete authored entity, motion, presentation, shared services and checkpoint contracts |
| `include/qa/game_q2_items.h` | 158 | `61a56f10f93615407589ebca06f3a02bbc335895cedecbb494a4a8c89cc28b96` | Read complete item catalog, grants, controls and checkpoint contracts |
| `include/qa/game_q2_monsters.h` | 152 | `8648e096ab6dddfc530789d262c07cbd01f658288b7d417fc201e9def34d308e` | Read public spawn/view/route/checkpoint contracts |
| `include/qa/game_q2_player.h` | 265 | `07142a9ea90c9c40c986793c89fda21e488dd91e8e5484a2b597367b9ccec37b` | Read complete selected character services, state/carry/checkpoint contracts |
| `src/gameplay/q2/ballistics.c` | 529 | `0e869206023e7016258968eb211f0133ed381f7ff36a5e4b0dba972a3a6e4b5a` | Read lead, water, rail, heatbeam, chainfist; source policy comparison remains open |
| `src/gameplay/q2/checkpoint.c` | 279 | `dedb5df6cbba2a9c49f833eaaafccdea85ce3414dba6b2b57e52b1154dc2a5b0` | Read capture/restore and actor/resource reference boundaries |
| `src/gameplay/q2/definitions.c` | 127 | `5dc9880bc9160012e4c3865103f4cacc589c74678222edaab9f72f0c36a73975` | Read native weapon installation and edition/product tables |
| `src/gameplay/q2/entities/brushes.c` | 654 | `83061727c5290b3bcda60c6ee7ddf4f3bef377853808934c6edb3ce1c9ff42c3` | Read all source; authored dispatch, lifecycle, shared service and continuation review |
| `src/gameplay/q2/entities/checkpoint.c` | 388 | `81b3e43a46dd7ae4c77a97f93305cbb754f1f047da4682b11b27a089fc6df8dc` | Read all source; authored dispatch, lifecycle, shared service and continuation review |
| `src/gameplay/q2/entities/checkpoint_internal.h` | 61 | `684d2c2ba6f230317e4a4ffc98005dd77cff09cd9a23e91228fc0e2f4745173e` | Read array/resource/fog/visual/inventory validation helpers |
| `src/gameplay/q2/entities/doors.c` | 478 | `8be5990f55d7e6ac474bd1551da32601e574a8947fb39b1b3809ce55b91419b1` | Read all source; authored dispatch, lifecycle, shared service and continuation review |
| `src/gameplay/q2/entities/goals.c` | 236 | `53a870cca9475b7b85f53a508698259fa3930cb2dbbda37ba49c98f10db8414a` | Read all source; authored dispatch, lifecycle, shared service and continuation review |
| `src/gameplay/q2/entities/internal.h` | 135 | `bfd641ca39589175fa72dffb44f9047baa3c3bd8dbee60d65ca8f32f45c333a8` | Read authored mechanism and lifecycle declarations |
| `src/gameplay/q2/entities/killbox.c` | 128 | `60a39d8c664a0ae546d2a9fc165df5fe91a44934baf815d6379f60288308d5d6` | Read all source; authored dispatch, lifecycle, shared service and continuation review |
| `src/gameplay/q2/entities/lifecycle.c` | 392 | `ce102f9aa6bf43cb04b4b5ae54f01625cc0acadaa1c9e0b2c0cdd5fde7258824` | Read all source; authored dispatch, lifecycle, shared service and continuation review |
| `src/gameplay/q2/entities/lights.c` | 192 | `fdd1446f91a345db13b090b66fd1573ab9f6e4360edfeb3d811dedcfe4a2a097` | Read all source; authored dispatch, lifecycle, shared service and continuation review |
| `src/gameplay/q2/entities/motion.c` | 242 | `91080031d6910945d1f75cad6aa14eb255f728ec8550af9ebee4aa4353675693` | Read all source; authored dispatch, lifecycle, shared service and continuation review |
| `src/gameplay/q2/entities/movers.c` | 181 | `e923ad8eeac4e20a2b0a62e367a93af26808825a76e1a81b06ad43aba38a711d` | Read all source; authored dispatch, lifecycle, shared service and continuation review |
| `src/gameplay/q2/entities/q64.c` | 488 | `11fca95095b31325f240a192285b9c72fbf33690105ed83ccdae4643d1e4d38e` | Read all source; authored dispatch, lifecycle, shared service and continuation review |
| `src/gameplay/q2/entities/rerelease.c` | 874 | `079de346d58e7431ebfdb21811fe8d278ce6e69906d98f3eddaa1951ed18600c` | Read all source; authored dispatch, lifecycle, shared service and continuation review |
| `src/gameplay/q2/entities/routes.c` | 186 | `180e5e03441b126034c7a607977ba08a3407bdbfefa56c6d434d8adc340b7a09` | Read all source; authored dispatch, lifecycle, shared service and continuation review |
| `src/gameplay/q2/entities/scenery.c` | 973 | `3189940ada6c42b3de724624caedbb46ba6f62cffe8c51ce9724f967250b9bf2` | Read all source; authored dispatch, lifecycle, shared service and continuation review |
| `src/gameplay/q2/entities/state.c` | 630 | `fcaddbe11066d83515564ad470b17df837c8f749f7cabf03c4a3b4190fd47ad8` | Read all source; authored dispatch, lifecycle, shared service and continuation review |
| `src/gameplay/q2/entities/target_effects.c` | 670 | `e3c204d1bbb662688ef065ac38d91813116a19ca63b38f102a34dd58ce3d114b` | Read all source; authored dispatch, lifecycle, shared service and continuation review |
| `src/gameplay/q2/entities/targets.c` | 494 | `d7661cfc5606d803f922cb5aa7c5567a3bcbf7a130981882aa131ad4e683d7d3` | Read all source; authored dispatch, lifecycle, shared service and continuation review |
| `src/gameplay/q2/entities/teleport.c` | 98 | `70eed8cdf317657a25db2b853c1e0d9938d96f6ac6070832f61f4d233384faee` | Read all source; authored dispatch, lifecycle, shared service and continuation review |
| `src/gameplay/q2/entities/trains.c` | 263 | `833075b1efe44d102d7aba105492a89f909c67cb7bd30e5c8ef37564c88f3f5a` | Read all source; authored dispatch, lifecycle, shared service and continuation review |
| `src/gameplay/q2/entities/triggers.c` | 409 | `6b277cc77e8fb6c6a7bce34d417a6fd9b8242bdc7b988cf93ce3113174a660ff` | Read all source; authored dispatch, lifecycle, shared service and continuation review |
| `src/gameplay/q2/entities/turrets.c` | 289 | `759f0ff18bec0efe3cb8ea8263151f494ec00384bae65c2ed280dc37cbb6c777` | Read all source; authored dispatch, lifecycle, shared service and continuation review |
| `src/gameplay/q2/game.c` | 456 | `a329199b144db6f968051226caef9e35e29ed18042c29d35cb267b0ba1be6395` | Read lifecycle, retirement, damage provenance, inventory consumption |
| `src/gameplay/q2/gibs.c` | 384 | `0f66b0dbf9ad328e6f2fc2f6203a7ebdc5051620165ad57344fa82584c280671` | Read gib/debris/growth/trap orbit lifecycle and physics ownership |
| `src/gameplay/q2/grapple.c` | 869 | `53c88ba4749e830c28b7119ee5d4dfb6160b89949047d8e231bfaf75ff278478` | Read complete CTF/LMCTF hook launch/attach/pull/release, independent controls and selected movement hooks |
| `src/gameplay/q2/grapple_weapon.c` | 154 | `3956660bf592cd4201fd129b79f70d90286c65cb3e3d993dae9639d953ec3e2e` | Read source grapple weapon frames and independent equipment resume/holster/tick/hold |
| `src/gameplay/q2/hand_grenades.c` | 389 | `ec1df2d40a92d5b3e1be7183069b1594cb7b936018919a4211da401279389f5d` | Read independent frame/effect state, ammo reservation/refund and nested revision guards |
| `src/gameplay/q2/internal.h` | 324 | `13b5a7e9652b80d94c5d84045211accddf16f4a579d5cb2d42f2c179bd8842fd` | Read ownership, typed state and service declarations |
| `src/gameplay/q2/items/catalog.c` | 343 | `d80f088573e77f4affaeaa6c282967969e6c38025953c60c61af9d634ce52b20` | Read full catalog, product filters, inventory actions and identity installation |
| `src/gameplay/q2/items/checkpoint.c` | 245 | `e941b01d3aef4f33ccae4f1138892da28ced372c4da3e3f81bb8adb953897e29` | Read capture/restore, reference remap, resource/array validation and candidate cleanup |
| `src/gameplay/q2/items/companions.c` | 604 | `b84d1e7553b3bf8dbace751f17110f8ef60a6fab5b786e0fdf475e41f61e3b63` | Read spheres/decoy admission, pain, tick, touch, expiry and foreign visual/camera services |
| `src/gameplay/q2/items/drop.c` | 401 | `22c4c98e5a9c851d8e5dce6e1542bceba9585af815d035880edaf033e611f26a` | Read ownership consumption, native/direct grants, mapped clears and start-items parsing; source start-grant hypothesis refuted |
| `src/gameplay/q2/items/grants.c` | 297 | `e7ffbf2ff998fca5e3a99b4a9eb76f81d7e3b0485152f7aeea5e38a8361d3a3d` | Read source item grants and shared supply mapping |
| `src/gameplay/q2/items/internal.h` | 103 | `e8fba40e38a884601a5e9924250e1e50a176668086b2cc61b37a4e0c20bc3b84` | Read all private item and continuation declarations; Q2-04 missing implementation |
| `src/gameplay/q2/items/lifecycle.c` | 406 | `842b95cc2db249a1271730e972fc8ff7d10c66629c085c1fd13516800642c932` | Read admission, drop-to-floor, respawn teams, mega decay and retirement |
| `src/gameplay/q2/items/powers.c` | 216 | `fd817b0574424fb15903c4bdf19ba0940a7a192474f26c2b6d97621aa19003df` | Read inventory leases, power fuel and native timed controls |
| `src/gameplay/q2/items/random.c` | 208 | `d703fe661a4232450c0536209104269215dbed29108f522a570de6a356120c82` | Read classic/Rogue/rerelease random categories, restrictions and replacement lifecycle |
| `src/gameplay/q2/items/touch.c` | 242 | `4c8b05e23938789c7680cbe76063d3d337d5547b51a4a94d7bad2848ed6008ba` | Read shared pickup continuation and source attempted-pickup targets; compared donor touch |
| `src/gameplay/q2/lmctf_plasma.c` | 271 | `7ff802a24aecfbcbf69906e3fbb84f699755c96eead20b3f34dd528fa1f68165` | Read fire, impact, spread and bounce; source policy comparison remains open |
| `src/gameplay/q2/mines.c` | 1022 | `adc6b0531bbeacad0aa9b87955bf7a4a30a272b6fea6123a48d98e122091b1d4` | Read prox/Tesla/trap/bad-area lifecycle, scans, nested callbacks and shared item/combat calls |
| `src/gameplay/q2/mission_bolts.c` | 248 | `1058004632c4979adc10b80a516bc82bbd3d4f12114ae4080227ab04ca3cbb96` | Read bolt damage and classic/rerelease homing trajectory control |
| `src/gameplay/q2/monsters/actions.c` | 4150 | `a730cc2f0ac4b9126bafca8ef9fea454325924c16946d782bcecfc710359525a` | Read complete dispatcher/helpers and compared parasite, summoning and trait mutations; Q2-03/06/13 |
| `src/gameplay/q2/monsters/ai.c` | 2504 | `4ae3bb24d6bcd4c7903695e9898a970ac160e243075a6f3ba50c4ca05b5e48de` | Read complete perception, acquisition, attack selection, movement and checkpoints; donor AI comparison Q2-14 |
| `src/gameplay/q2/monsters/beam.c` | 421 | `d9f90cf9a29f3a5e6b2a928f3e44efb897aaad7c7462af6d87c6b4751a8c27ab` | Read complete retained beam, boss exploder, think and ownership paths |
| `src/gameplay/q2/monsters/checkpoint.c` | 586 | `a7f22f026dd85cd1464905317a632c10f4dcadaddd3fffbb48749060c2c7810a` | Read complete typed monster capture/restore, actor references and validation |
| `src/gameplay/q2/monsters/combat.c` | 2776 | `cb3664d2ff19378896b618ed29697ff4efb6cfa29900f5cf1ed056ee757ad221` | Read complete attacks, source pain/death/gibs, reinforcement and environment/touch paths; Q2-01/03/13 |
| `src/gameplay/q2/monsters/core.c` | 1766 | `cbb73dc7125cb16d0c706a59056a9161e8c488938c9a11e805bed16f38ee3917` | Read complete admission, dispatch, animation, physics, traits and lifecycle; Q2-01/02/03/07 |
| `src/gameplay/q2/monsters/internal.h` | 383 | `06d8b6b30294bb5e0a89a56e258be396a285883c9983e96e57535db8e37d43f7` | Read private state and callback contracts |
| `src/gameplay/q2/monsters/moves.c` | 19131 | `8fc8935b3f53b6e58ef86a7fed29519ffb3ec09df3696cc6feb1b8e6e5e488d4` | Complete read-only structural and donor-field comparison: 70 sets, 1031 moves, 15724 frames, 1875 actions; consumer gaps Q2-07/14 |
| `src/gameplay/q2/monsters/muzzle_data.h` | 507 | `cb64b5cf27edfc3720c757955c42cdd5206a499aebe6f574e8d0bcd7d55e0e48` | Complete donor expression comparison: 212 classic and 290 rerelease vectors |
| `src/gameplay/q2/monsters/roster.c` | 616 | `0f8365fb9c382ace35bad585a6f7f5444ff1a6fd7417c5ce3e5047811e040a5f` | Read complete edition/product/variant roster and source definitions |
| `src/gameplay/q2/monsters/spawn.c` | 171 | `d812d716f51d293f2729fb5d9418acb27816ce6baba9a0669544a3dc578f6197` | Read placement/growth; compared Rogue and rerelease donor placement |
| `src/gameplay/q2/nuke.c` | 356 | `6bc4f2c6ad6a73df5327e81b09ee0a48b5615afd82bfbc218a97273deb523f05` | Read launch/countdown/damage/blind/quake lifecycle; Q2-09 corrected |
| `src/gameplay/q2/player/admission.c` | 411 | `da51b2692d6ac1ae0159f601e82cddad25f2ddebb5460fda0f571da03b2d3270` | Read userinfo, passwords, connection, shared coop inventory, native admission and disconnect |
| `src/gameplay/q2/player/checkpoint.c` | 246 | `d05f3d8493ac1b09969b730dd8ca3ccaa6e55c8528302243250f3b8167d70127` | Read complete per-player/global capture and restore, arrays, references and resource boundaries |
| `src/gameplay/q2/player/commands.c` | 734 | `9ada4fa1033d073e529fa00e097caf4e42bcf5509acc2a5ff5d28b5c4c70ff42` | Read complete console/inventory/chat/scoreboard routes; Q2-04/05 |
| `src/gameplay/q2/player/death.c` | 341 | `1ca6800008a50149a22665a9ac5c46ccab66a94705e4ff9d6476c8d95f4a0e7d` | Read death/gib/corpse/carry/drop/restart paths; Q2-03/05 |
| `src/gameplay/q2/player/environment.c` | 212 | `b57f0c5714a9fa7a38e0ec3c54c0d42a96ce73d5617fbaef860b9bfd7bbf1b42` | Read air, water/slime/lava, falling damage and source edition timings |
| `src/gameplay/q2/player/equipment.c` | 211 | `14943955a48c5c1b6741b51f1eac44c6e09ded941f636ea10efafac5ebcba3a2` | Read hunter camera, flashlight, compass/navigation and retained route buffers |
| `src/gameplay/q2/player/frame.c` | 365 | `08036d61a380d57fafc9729acd1d9b83854442d30607f47d16d03437f5c6908a` | Read source command/frame/feedback/noise phases and callback liveness checks |
| `src/gameplay/q2/player/intermission.c` | 272 | `5df8cd4f135e0796449ad1614e8368a7b084c22d4f216c30ace152fbc8718e97` | Read camera, end-unit keys, fade, carry reset and transition owner call; Q2-05 |
| `src/gameplay/q2/player/internal.h` | 76 | `7a1c60d1138aa8c75d5e0ed0e2f2c21d78d4de0e1caef907fd72b991106aaca7` | Read selected movement/presentation/transition dependencies and shared state declarations |
| `src/gameplay/q2/player/map_spawns.c` | 175 | `538bd48060722a31884028641497d36c7524d613774802b1536c4379a2a2c39f` | Read authored player starts, classic map repairs and rerelease stuck/floor handling |
| `src/gameplay/q2/player/obituary.c` | 181 | `e712083edaa226b173d1696449cf01afb27ad7819f08456944b670a9e05bc0f7` | Read classic/rerelease cause mapping, text args and score routing; Q2-05 |
| `src/gameplay/q2/player/spawn.c` | 321 | `c03096260a0ea7bdd593d923c2f0fb64e16bb6edee6ea23bb4d11be73350468d` | Read source spawn/respawn, loadout restoration, killbox and selected movement handoff |
| `src/gameplay/q2/player/spawn_select.c` | 558 | `46683185e6e1ee017921b9d0eb0a62a3c44ff1767b5ffdcd0634b6e502263708` | Read all classic/rerelease/landmark/lava/cooperative placement routes |
| `src/gameplay/q2/player/spectator.c` | 276 | `37d278fbe626217358aba3c4a024b03df8b0fa531d255a4e0316b39c703d5c3a` | Read chase and cooperative squad/lives respawn geometry; Q2-05 |
| `src/gameplay/q2/player/state.c` | 334 | `16345cf2a1492e41d406162d1a4ebd3a4e56646da1b0b32c5e715c9f1e330aff` | Read configure, carry/inventory capture/restore, base trait edits and player projection |
| `src/gameplay/q2/player/view.c` | 475 | `5ef4bfc991991b9d3ef773feb10bc583c654d205b82235ca80bf3282e6c46af2` | Read feedback, animation, camera/weapon offsets, blends, timed effects and HUD output |
| `src/gameplay/q2/projectiles.c` | 1271 | `86733a77d22fa621489d6dc9b0f48a05c3f7d81aeea32fd65c31890c0bfdc225` | Read complete projectile lifecycle, shared physics/touch, BFG/tracker callbacks and trajectory controls; Q2-08 corrected |
| `src/gameplay/q2/random.c` | 80 | `ed43c2d0b65782b1885e8794dc91431c9350f7ec23ccb96fd3571d346c4817ae` | Read PRNG and bounded distributions; no statistical execution |
| `src/gameplay/q2/weapon_fire.c` | 564 | `5a8c89d9d8c4d61cd9fcaa2b8c87cd365e26e2031dfa33fd4fb8b275e1ffe1d3` | Read complete native weapon fire dispatch, shot parameters, lag scope and projectile creation |
| `src/gameplay/q2/weapon_frame.c` | 208 | `175316203700226306c81d9a056789dd46b165b619db67ac1adea2a3ab3103df` | Read classic/rerelease cadence and phase continuations |
| `src/gameplay/q2/weapon_presentation.c` | 207 | `e286880ef2e5a9f453a5397ac481129eda98fcfbbca91b0ce2f3b710ed601872` | Read sound, noise, recoil, muzzle and aim events |
| `src/gameplay/q2/weapon_state.c` | 526 | `fa81ddf6c680ac49c8e0f7fe569360e91f092dafbd1e420e71b71f1a62cd1c8d` | Read selection, handoff, ammo fallback, state validation and timed power inputs |
| `src/gameplay/q2/weapon_throw.c` | 277 | `5a1754a69bb14356ffb59bc13dc52bf0c136c4351c80b9d6539e36a1a4d493d7` | Read complete grenade/trap/Tesla/prox/nuke throw frame state and cooking paths |

## Post-correction snapshot

The initial snapshot remains above. The source-reviewed focused corrections change these file hashes; Q2-08/09/10 are committed in `9b1dfaef2d781f1246e244e039b97c01bdc8be1d`, and Q2-12 is awaiting independent root review.

| File | SHA-256 after correction |
| --- | --- |
| `src/gameplay/q2/entities/killbox.c` | `0f50fa01f2ecd3389ee2e3b1ec9d0d5994c7b4bd7b679e705f4bc92cc912db39` |
| `src/gameplay/q2/gibs.c` | `ace51fa657a1aa4a1fab84ce7c45f72fd5ef2b6fc1d2b0e2940a1d251f53b9b0` |
| `src/gameplay/q2/nuke.c` | `f9d846e1eb4dea4eb58167b33afb0df6eac58330051462078ea6c4ddbf7a843e` |
| `src/gameplay/q2/projectiles.c` | `a7c260b6f03e2696a04995ad66bad52edb3374e6c31663ad4a27a49c7b43a78e` |

## Task-specific source excerpts

These excerpts accompany the full-file source review and hashes; omitted lines are not claimed absent merely because they are outside an excerpt.

### src/gameplay/q2/monsters/core.c:265

```text
265: static bool activate_triggered(q2m_context *context, qa_actor_id activator,
266:                                qa_error *error) {
267:   struct qa_q2_monster *monster = context->monster;
268:   if (!monster->triggered || monster->dead)
269:     return true;
270:   monster->triggered = false;
271:   monster->activator = activator;
272:   monster->air_ns = q2m_after(context->game->now_ns, 12.0);
273:   monster->can_take_damage = true;
274:   context->actor->physics.solid = QA_PHYSICS_BOX;
275:   context->actor->physics.motion =
276:       monster->definition->locomotion == Q2M_STATIONARY ? QA_PHYSICS_STATIONARY
277:                                                         : QA_PHYSICS_STEP;
278:   qa_actor_collision collision = {
279:       .family = QA_COLLISION_Q2,
280:       .shape = QA_SHAPE_BOX,
281:       .contents = (int32_t)UINT32_C(0x02000000),
282:       .role = QA_COLLISION_SOLID,
283:       .monster = true,
284:   };
285:   context->combat.can_take_damage = true;
286:   if (!qa_combat_set_traits(context->game->services.combat, context->actor->id,
287:                             &context->combat, error) ||
288:       !qa_world_set_collision(context->game->services.world, context->actor->id,
289:                               &collision, error) ||
290:       !q2m_link(context, error))
291:     return false;
292:   if (activator.registry != 0)
293:     return q2m_found_target(context, activator, error);
294:   return true;
295: }
296: 
```

### src/gameplay/q2/monsters/combat.c:2412

```text
2412: bool q2m_spawn_reinforcement(q2m_context *context, const char *classname,
2413:                              q2m_spawned_by spawned_by, qa_vec3 origin,
2414:                              qa_actor_id *spawned, qa_error *error) {
2415:   if (spawned != NULL)
2416:     *spawned = (qa_actor_id){0};
2417:   if (!q2m_alive(context) || classname == NULL)
2418:     return true;
2419:   qa_actor_definition definition;
2420:   if (!qa_builtin_resource(&context->game->services, classname, &definition,
2421:                            error))
2422:     return false;
2423:   qa_builtin_spawn spawn = {
2424:       .owner = context->game->options.owner,
2425:       .definition = definition,
2426:       .body = {.origin = origin, .angles = context->body.angles},
2427:       .link = false,
2428:   };
2429:   qa_actor_id child;
2430:   if (!qa_builtin_spawn_actor(&context->game->services, &spawn, &child, error))
2431:     return false;
2432:   qa_q2_monster_spawn_options options = {
2433:       .classname = classname,
2434:       .health_multiplier = 1.0f,
2435:       .enemy = context->monster->enemy,
2436:       .commander = context->actor->id,
2437:       .summoned = true,
2438:   };
2439:   if (!qa_q2_monster_spawn(context->game, child, &options, error)) {
2440:     qa_error original = *error;
2441:     qa_error ignored = {0};
2442:     qa_session_release(context->game->services.session, child, &ignored);
2443:     *error = original;
2444:     return false;
2445:   }
2446:   if (spawned != NULL)
2447:     *spawned = child;
2448:   if (!q2m_alive(context))
2449:     return true;
2450:   q2_actor *child_actor = context->game->actors[child.slot];
2451:   if (child_actor != NULL && qa_actor_id_equal(child_actor->id, child) &&
2452:       child_actor->monster != NULL)
2453:     child_actor->monster->spawned_by = spawned_by;
2454:   if (spawned_by == Q2M_SPAWN_WIDOW)
2455:     ++context->monster->monster_used;
2456:   else if (spawned_by == Q2M_SPAWN_MEDIC &&
2457:            context->game->options.edition == QA_Q2_RERELEASE)
2458:     ++context->monster->monster_used;
2459:   else if (context->monster->monster_slots > 0)
2460:     --context->monster->monster_slots;
2461:   return q2m_emit(context, QA_BUILTIN_TELEPORT, "q2:spawn", 0, origin, origin,
2462:                   1.0f, error);
2463: }
2464: 
2465: static qa_attack environmental_attack(q2m_context *context, int means,
```

### src/gameplay/q2/player/obituary.c:72

```text
72: bool q2_player_obituary(qa_q2_game *g, q2_actor *a, const qa_damage_outcome *outcome, qa_error *e) {
73:     int raw = outcome->request.attack.cause.kind == QA_CAUSE_Q2
74:                   ? outcome->request.attack.cause.source.q2.means_of_death
75:                   : 0;
76:     int means = raw & ~0x8000000;
77:     q2_actor *attacker = q2_actor_get(g, outcome->request.attack.attacker, false, NULL);
78:     if (attacker && (!attacker->client || !attacker->client->info.connected))
79:         attacker = NULL;
80:     bool self = attacker == a, friendly = (raw & 0x8000000) || (g->options.cooperative && attacker);
81:     bool rr = g->options.edition == QA_Q2_RERELEASE,
82:          no_loss = outcome->request.attack.cause.kind == QA_CAUSE_Q2 &&
83:                    outcome->request.attack.cause.source.q2.no_point_loss;
84:     const q2_obituary *environment = NULL, *kill = NULL;
```

### src/gameplay/q2/monsters/core.c:1410

```text
1410:       monster->fly_max_distance = 200.0f;
1411:       break;
1412:     case Q2M_FLYER:
1413:       monster->alternate_fly = true;
1414:       monster->fly_buzzard = true;
1415:       monster->fly_acceleration = 15.0f;
1416:       monster->fly_speed = 165.0f;
1417:       monster->fly_min_distance = 45.0f;
1418:       monster->fly_max_distance = 200.0f;
1419:       break;
1420:     case Q2M_HOVER:
1421:       monster->alternate_fly = true;
1422:       monster->fly_acceleration = 20.0f;
1423:       monster->fly_speed = 120.0f;
1424:       monster->fly_min_distance = 150.0f;
1425:       monster->fly_max_distance = 350.0f;
1426:       monster->yaw_speed = 18.0f;
1427:       break;
1428:     default:
1429:       break;
1430:     }
1431:   }
1432:   actor->monster = monster;
1433:   if (!qa_builtin_resource(&game->services, options->classname,
1434:                            &monster->classname, error) ||
1435:       !qa_builtin_resource(&game->services, definition->model, &monster->model,
1436:                            error) ||
1437:       !initialize_body(game, actor, monster, error) ||
1438:       !initialize_combat(game, actor, monster, error)) {
1439:     actor->monster = NULL;
1440:     actor->physics_bound = false;
1441:     free(monster);
1442:     return false;
1443:   }
1444:   qa_actor_collision collision = {
1445:       .family = QA_COLLISION_Q2,
1446:       .shape = QA_SHAPE_BOX,
1447:       .contents = (int32_t)UINT32_C(0x02000000),
1448:       .role = QA_COLLISION_SOLID,
1449:       .monster = true,
1450:   };
1451:   if (monster->triggered) {
1452:     if (!qa_world_set_collision(game->services.world, id, NULL, error)) {
1453:       actor->monster = NULL;
1454:       actor->physics_bound = false;
1455:       free(monster);
1456:       return false;
1457:     }
1458:   } else if (!qa_world_set_collision(game->services.world, id, &collision,
1459:                                      error)) {
1460:     actor->monster = NULL;
1461:     actor->physics_bound = false;
1462:     free(monster);
1463:     return false;
1464:   }
1465:   q2m_context context = {.game = game, .actor = actor, .monster = monster};
1466:   if (!q2m_refresh(&context, error)) {
1467:     actor->monster = NULL;
1468:     actor->physics_bound = false;
1469:     free(monster);
1470:     return false;
1471:   }
1472:   size_t frame_count =
1473:       (size_t)(monster->move->last_frame - monster->move->first_frame + 1);
1474:   monster->frame = monster->move->first_frame +
1475:                    (int)fminf((float)(frame_count - 1),
1476:                               floorf(q2m_random(game) * (float)frame_count));
1477:   monster->next_frame_ns = q2m_after(game->now_ns, 0.1);
1478:   monster->initialized = true;
1479:   if (!monster->triggered && !q2m_link(&context, error)) {
1480:     actor->monster = NULL;
1481:     actor->physics_bound = false;
1482:     free(monster);
1483:     return false;
1484:   }
1485:   if ((options->spawnflags & UINT32_C(65536)) != 0 &&
1486:       game->options.edition == QA_Q2_RERELEASE) {
1487:     if (!qa_combat_set_health(game->services.combat, id, 0.0f, error) ||
1488:         !q2m_refresh(&context, error) || !q2m_die(&context, error))
1489:       return false;
1490:     for (size_t guard = 0; guard < 1024 && q2m_alive(&context) &&
1491:                            !monster->corpse && !monster->gibbed;
1492:          ++guard) {
1493:       monster->next_frame_ns = 0;
1494:       if (!q2m_animation(&context, error))
1495:         return false;
1496:     }
1497:   }
1498:   return q2m_alive(&context) ? present_animation(&context, error) : true;
1499: }
1500: 
1501: void q2_monster_release_state(q2_actor *actor) {
1502:   if (actor == NULL)
1503:     return;
1504:   free(actor->monster);
1505:   actor->monster = NULL;
1506: }
1507: 
1508: bool q2_monster_traits(qa_q2_game *game, qa_actor_id id,
```

### src/gameplay/q2/monsters/combat.c:2137

```text
2137: bool q2m_die(q2m_context *context, qa_error *error) {
2138:   if (!q2m_alive(context))
2139:     return true;
2140:   struct qa_q2_monster *monster = context->monster;
2141:   if (monster->gibbed)
2142:     return true;
2143: 
2144:   if (context->game->options.edition == QA_Q2_RERELEASE &&
2145:       (monster->definition->species == Q2M_FLOATER ||
2146:        monster->definition->species == Q2M_FLYER))
2147:     return rerelease_flying_explosion(context, error);
2148: 
2149:   if (monster->definition->species == Q2M_TURRET_DRIVER &&
2150:       monster->turret_attached) {
2151:     if (!qa_q2_turret_driver_detach(context->game, context->actor->id, error))
2152:       return false;
2153:     if (!q2m_alive(context))
2154:       return true;
2155:     if (!q2m_refresh(context, error))
2156:       return false;
2157:     monster = context->monster;
2158:   }
2159: 
2160:   if (monster->resurrect_target.registry != 0 &&
2161:       monster->resurrect_target.slot < context->game->capacity) {
2162:     q2_actor *patient = context->game->actors[monster->resurrect_target.slot];
2163:     if (patient != NULL &&
2164:         qa_actor_id_equal(patient->id, monster->resurrect_target) &&
2165:         patient->monster != NULL)
2166:       patient->monster->resurrecting = false;
2167:     monster->resurrect_target = (qa_actor_id){0};
2168:     monster->medic = false;
2169:   }
2170: 
2171:   if (!monster->dead && monster->commander.registry != 0 &&
2172:       monster->commander.slot < context->game->capacity) {
2173:     q2_actor *commander = context->game->actors[monster->commander.slot];
2174:     if (commander != NULL &&
2175:         qa_actor_id_equal(commander->id, monster->commander) &&
2176:         commander->monster != NULL) {
2177:       if (monster->spawned_by == Q2M_SPAWN_MEDIC &&
2178:           context->game->options.edition == QA_Q2_RERELEASE) {
2179:         if (commander->monster->monster_used > 0)
2180:           --commander->monster->monster_used;
2181:       } else if (monster->spawned_by == Q2M_SPAWN_WIDOW) {
2182:         if (commander->monster->monster_used > 0)
2183:           --commander->monster->monster_used;
2184:       } else {
2185:         ++commander->monster->monster_slots;
2186:       }
2187:     }
2188:   }
2189: 
2190:   bool crushed = monster->last_attack.cause.kind == QA_CAUSE_Q2 &&
2191:                  monster->last_attack.cause.source.q2.means_of_death == 20;
2192:   q2m_species species = monster->definition->species;
```

### src/gameplay/q2/entities/lifecycle.c:156

```text
156:     return q2_scenery_spawn(g, a, handled, e);
157: }
158: bool qa_q2_entity_use(qa_q2_game *g, qa_actor_id id, qa_actor_id other, qa_actor_id activator,
159:                       qa_error *e) {
160:     q2_actor *a = g ? q2_actor_get(g, id, false, NULL) : NULL;
161:     if (a && a->projectile.kind == Q2_PROJECTILE_NONE && a->item &&
162:         (a->item->spawn.spawnflags & 1)) {
163:         a->item->spawn.spawnflags &= ~1u;
164:         return qa_q2_item_enable(g, id, e);
165:     }
166:     if (!a || !a->entity || !a->entity->usable || a->projectile.kind != Q2_PROJECTILE_NONE)
167:         return true;
168:     if (a->entity->kind == Q2E_DYNAMIC_LIGHT)
169:         return q2_light_use(g, a, e);
170:     if (a->entity->kind == Q2E_CAMERA)
171:         return q2_q64_use(g, a, activator, e);
172:     bool handled;
173:     if (g->options.edition == QA_Q2_RERELEASE) {
174:         if (!q2_rerelease_entity_use(g, a, other, activator, &handled, e))
175:             return false;
176:         if (handled || !q2_actor_live(g, id))
177:             return true;
178:     }
179:     if (!q2_trigger_use(g, a, other, activator, &handled, e))
180:         return false;
181:     if (handled || !q2_actor_live(g, id))
182:         return true;
183:     if (!q2_target_use(g, a, other, activator, &handled, e))
184:         return false;
185:     if (handled || !q2_actor_live(g, id))
186:         return true;
187:     if (!q2_mover_use(g, a, other, activator, &handled, e))
188:         return false;
189:     if (handled || !q2_actor_live(g, id))
190:         return true;
191:     return q2_scenery_use(g, a, other, activator, &handled, e);
192: }
193: bool q2_entity_tick(qa_q2_game *g, q2_actor *a, qa_error *e) {
194:     if (a->projectile.kind != Q2_PROJECTILE_NONE || !a->entity)
195:         return true;
196:     if (!q2_scenery_prethink(g, a, e))
197:         return false;
198:     if (!q2_actor_live(g, a->id) || a->entity->think == Q2ET_NONE || a->entity->due_ns > g->now_ns)
```

### src/gameplay/q2/entities/target_effects.c:318

```text
318:     case Q2E_LASER:
319:         s->usable = false;
320:         return q2_entity_schedule(g, a, Q2ET_LASER_START, 1);
321:     case Q2E_LIGHTRAMP: {
322:         const char *message = qa_strings_cstr(qa_session_strings(g->services.session), s->message);
323:         if (strlen(message) != 2 || message[0] < 'a' || message[0] > 'z' || message[1] < 'a' ||
324:             message[1] > 'z' || message[0] == message[1] || !s->target || g->options.deathmatch)
325:             return qa_session_release(g->services.session, a->id, e);
326:         s->direction = qa_v3((float)(message[0] - 97), (float)(message[1] - 97), 0);
327:         return true;
328:     }
329:     case Q2E_EARTHQUAKE:
330:         if (!s->count)
331:             s->count = 5;
332:         if (!s->speed)
333:             s->speed = 200;
334:         s->wait = 0;
335:         return true;
336:     case Q2E_STEAM:
337:         s->usable = false;
338:         return s->target ? q2_entity_schedule(g, a, Q2ET_STEAM, 1) : steam_start(g, a, e);
```

### src/gameplay/q2/entities/scenery.c:650

```text
650:         s->stage = 1;
651:         return q2_entity_schedule(g, a, Q2ET_SCENERY, (float)g->frame_ns / Q2_NS);
652:     case Q2S_STRING: {
653:         const char *text = qa_strings_cstr(qa_session_strings(g->services.session), s->message);
654:         size_t length = strlen(text);
655:         for (q2_actor *member = g->first_actor; member;) {
656:             q2_actor *next = member->live_next;
657:             q2_entity_state *m = member->entity;
658:             if (m && m->count &&
659:                 (s->team ? m->team == s->team : qa_actor_id_equal(a->id, member->id))) {
660:                 char c = m->count > 0 && (size_t)m->count <= length ? text[m->count - 1] : 0;
661:                 m->visual.frame = c >= '0' && c <= '9' ? c - '0'
662:                                   : c == '-'           ? 10
663:                                   : c == ':'           ? 11
664:                                                        : 12;
665:                 if (!q2_entity_show(g, member, e))
```

### src/gameplay/q2/player/intermission.c:125

```text
125:         if (combat.health <= 0) {
126:             if (rr && a->client->has_coop &&
127:                 (p->rules.coop_instanced_items || p->rules.coop_squad_respawn))
128:                 a->client->coop.health = a->client->coop.maximum_health;
129:             if (!qa_q2_player_spawn(g, a->id, true, NULL, e))
130:                 return false;
131:         }
132:         if (!q2_actor_live(g, a->id))
133:             continue;
134:         if (end_unit && g->options.cooperative)
135:             for (size_t j = 0; j < qa_q2_item_count(g); j++) {
136:                 const qa_q2_item_definition *d = qa_q2_item_at(g, j);
137:                 if (d->kind != QA_Q2_ITEM_KEY)
138:                     continue;
139:                 qa_inventory_entry entry;
140:                 qa_error missing = {0};
141:                 if (!qa_inventory_entry_read(g->services.inventory, a->id, d->item, &entry,
142:                                              &missing)) {
143:                     if (missing.code == QA_ERROR_NOT_FOUND)
144:                         continue;
145:                     if (e)
146:                         *e = missing;
147:                     return false;
148:                 }
149:                 entry.count = 0;
150:                 if (!qa_inventory_configure(g->services.inventory, a->id, &entry, NULL, NULL, e))
151:                     return false;
152:                 if (!q2_actor_live(g, a->id))
153:                     break;
154:             }
155:     }
156:     if (rr && end_unit && !(flags & 16) &&
157:         !q2_map_event(
158:             g, &(qa_q2_map_event){.kind = QA_Q2_MAP_END_UNIT, .resource = map_id, .flags = flags},
159:             e))
```

### ../quake-typescript/src/content/q2/foundation/monsters/index.ts:625

```text
625:     const { state, entity, game } = context;
626:     this.sourceCombatHooks?.beforeKilled(context);
627:     if (!state.dead) {
628:       const commander = state.commander === null ? null : game.entity(state.commander);
629:       const commanderState = commander === null ? undefined : this.contexts.get(commander.actor.id)?.state;
630:       if (commander !== null && commanderState !== undefined) {
631:         if (state.spawnedBy === "carrier" && commander.classname === "monster_carrier") commanderState.monsterSlots++;
632:         else if (state.spawnedBy === "medic" && commander.classname === "monster_medic_commander") { if (game.options.edition === "rerelease") commanderState.monsterUsed -= state.monsterSlots; else commanderState.monsterSlots++; }
633:         else if (state.spawnedBy === "widow" && commander.classname.startsWith("monster_widow") && commanderState.monsterUsed > 0) commanderState.monsterUsed--;
634:       }
635:       const mission = this.hooks.mission?.(entity.actor.id);
636:       if (mission == null && !state.goodGuy && !state.doNotCount && (game.options.edition === "classic" || (entity.spawnflags & 65536) === 0)) game.counters.killedMonsters++;
637:       entity.enemy = reaction.attacker;
638:       if (game.options.edition === "classic" && (entity.motion === "push" || entity.motion === "stop" || entity.motion === "stationary")) {
639:         definition.die(context, reaction); game.show(entity); return undefined;
640:       }
641:       entity.touch = null;
642:       entity.flags &= ~3;
643:       const item = entity.spawn.values.get("item");
644:       if (item !== undefined && item.length > 0) {
645:         if (this.hooks.dropItem === undefined) throw new Error("Monster authored item drop requires source item services");
646:         this.hooks.dropItem(entity.actor, game, item);
647:       }
648:       if (mission == null) {
649:         if (entity.deathTarget.length > 0) entity.target = entity.deathTarget;
650:         if (entity.target.length > 0) game.useTargets(entity, reaction.attacker);
651:       } else mission.killed(reaction.attacker);
652:     }
653:     definition.die(context, reaction);
654:     game.show(entity);
655:     return undefined;
```
