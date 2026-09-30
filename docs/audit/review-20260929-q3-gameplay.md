# Q3 and shared gameplay source review, 2026-09-29

This is a bounded source review of existing code. No engine build, compiler, test, parser, executable, sanitizer, benchmark or gameplay run was performed. Findings below are confirmed by source control flow, not runtime reproduction. This packet does not complete AUDIT, any game-family task, or BASELINE.

The worker has no exposed external session UUID. No Jev session identity was invented. The coordinator owns the existing AUDIT ledger contribution and must record the handoff.

## Confirmed fixes

| ID | Severity | Source | Failure scenario and change |
|---|---|---|---|
| QG01 | high | `src/gameplay/inventory.c:425`, `src/gameplay/operation.c:56`, `include/qa/operation.h:37` | A hook admission prepared on an exposed inventory operation makes operation destruction reject teardown. Inventory destruction formerly ignored that rejection, freed the inventory and returned success. All four operations now receive the same nonmutating destruction preflight before any operation is freed. Pending tokens remain usable after rejection. |
| QG02 | high | `src/gameplay/combat.c:459` | Final external armor validation and write were chained without checking actor/binding/protection ownership between callbacks. A successful validation callback could retire or replace the binding; the subsequent old writer would still run. The exact existing ownership check now separates validation from write. |
| QG03 | medium | `src/gameplay/q3/checkpoint.c:151` | A player admission reservation could remain open while checkpoint restore replaced the player array. This violates the documented begin/commit contract. Restore now rejects any pending player reservation before allocation or publication. |
| QG04 | high | `src/gameplay/q3/death.c:68`, `src/gameplay/q3/missiles.c`, `src/gameplay/q3/movers.c`, `src/gameplay/q3/player.c:1064` | Several cross-actor observations retained pointers to the current Q3 slot. A foreign combat/body/mover callback could retire the current actor and reuse its slot while the observed actor remained valid. Death rewards could credit the replacement killer; missile launch/impact/grapple/mine attachment and triggering could update replacement state; mover fallback could update a replacement player; invulnerability expansion could update a replacement moving player. These changed paths reacquire the original full actor ID after observations and stop when it no longer has the required Q3 kind. Expansion also reacquires state in its caller and returns movement removal. |
| QG06 | high | `src/gameplay/q3/checkpoint.c:145`, `src/gameplay/q3/game.c:189`, `src/gameplay/q3/missiles.c:227` | Independent Q2 review found that an external world body callback could restore the complete Q3 actor array or destroy the provider while a missile helper retained its old actor pointer. Session and combat idleness did not reject a world callback. Restore and destruction now require world idleness, and the origin helper reacquires the original missile after body observation. This correction uses the world reviewer's new `qa_world_idle` API. |
| QG05 | medium | `src/gameplay/q3/player.c:642` | The fire event sink can retire the firing actor. Arsenal execution then called `qa_q3_fire_weapon`, which rejected the missing actor and turned an allowed retirement into a failed command. Arsenal execution now reacquires the original player after the event and ends the retired command successfully. |

QG04 covers the particular changed paths, not every possible Q3 callback path. The code uses retained arrays indexed by full actor identity; checking the observed actor alone does not validate the other actor whose cached pointer will be used.

## Scope and confirmation

Read the full current bodies for the files marked below. Shared world body-read and combat binding guards were also traced so a callback failure for the observed actor was not confused with retirement of a different actor. The Q3 donor `game/weapon.ts` and `game/hitscan.ts` were read for accuracy ordering. Lightning and shotgun intentionally evaluate accuracy after damage in that donor, so this review did not change that behavior. Q3 item spawn, item completion, projectile, corpse and holdable control flow received source review; this is not an exhaustive game-parity claim.

`git diff --check` passed for the changed source paths before freeze. The complete current patch and the hashes below identify the reviewed packet. The foundations reviewer independently accepted QG01 and QG02 after reading complete lifecycle and ownership contracts. The Q2 reviewer found QG06 and accepted its correction after checking all ten updated source hashes, all changed hunks and affected paths. No further confirmed defect was found in that bounded packet. Unchanged source bodies were not exhaustively reread by that peer. Builds and runtime verification remain deferred by the baseline phase rule.

## Frozen source hashes

| Path | SHA-256 |
|---|---|
| `include/qa/operation.h` | `85ff155e3975110127a9fb19c710036cad12e6289318751edd3e54ae5e83389e` |
| `src/gameplay/operation.c` | `c69338daad24b6ddd89680a2497bfa0e8a4374aff8b86ce613177ad8145d9af7` |
| `src/gameplay/inventory.c` | `fe6f355a2c9dca9e6c30ad1859452672e44adf9dbe77baf36a9347c5538573d8` |
| `src/gameplay/combat.c` | `63a57835dd2c44d091482985d172f51381328148e45401434f60565b2050422f` |
| `src/gameplay/q3/checkpoint.c` | `a75796b2112e371d74ad2895bf500f80067c36e2a9199e9915885e18f8bb60be` |
| `src/gameplay/q3/game.c` | `84e0ef89ac21e1b9013ea2eb5ce87fe8399926aaffa4429031d276e1b1797fe7` |
| `src/gameplay/q3/death.c` | `b9daffebdc9c4fb1fefb80840ef812f2fc819b15f13a1e489022b96c45dcaf9c` |
| `src/gameplay/q3/movers.c` | `f91dbfa2831c05ebf9a9849cc99722c55110d8352b5cad16870f1d96b687155a` |
| `src/gameplay/q3/player.c` | `9e8dc792fc7bce2c055b443ab313f3a7a485b1e17b3b92ab3b2f6f941ff945ef` |
| `src/gameplay/q3/missiles.c` | `af177f4321fc28bb2ae4ce937d6dfe70def962bbc7b3b83833125844e0353350` |

## File coverage

A full read does not imply every behavior has been proved correct. Uncovered files remain explicitly outside this packet.

| Path | Coverage |
|---|---|
| `src/gameplay/armor.c` | full source read |
| `src/gameplay/builtin/attacks.c` | full source read |
| `src/gameplay/builtin/random.c` | full source read |
| `src/gameplay/builtin/services.c` | full source read |
| `src/gameplay/combat.c` | full source read |
| `src/gameplay/combat_internal.h` | full source read |
| `src/gameplay/inventory.c` | full source read |
| `src/gameplay/inventory_internal.h` | full source read |
| `src/gameplay/modes/arena.c` | uncovered in this packet |
| `src/gameplay/modes/checkpoint.c` | full source read |
| `src/gameplay/modes/commands.c` | uncovered in this packet |
| `src/gameplay/modes/core.c` | full source read |
| `src/gameplay/modes/damage.c` | uncovered in this packet |
| `src/gameplay/modes/deathball.c` | uncovered in this packet |
| `src/gameplay/modes/equipment.c` | uncovered in this packet |
| `src/gameplay/modes/flags.c` | uncovered in this packet |
| `src/gameplay/modes/give.c` | uncovered in this packet |
| `src/gameplay/modes/horde.c` | uncovered in this packet |
| `src/gameplay/modes/internal.h` | full source read |
| `src/gameplay/modes/items.c` | uncovered in this packet |
| `src/gameplay/modes/map.c` | uncovered in this packet |
| `src/gameplay/modes/match.c` | uncovered in this packet |
| `src/gameplay/modes/objectives.c` | uncovered in this packet |
| `src/gameplay/modes/objects.c` | focused: drop direction consumer lines 636-680; remainder uncovered |
| `src/gameplay/modes/placement.c` | uncovered in this packet |
| `src/gameplay/modes/relics.c` | uncovered in this packet |
| `src/gameplay/modes/rogue_tag.c` | uncovered in this packet |
| `src/gameplay/modes/scoring.c` | uncovered in this packet |
| `src/gameplay/modes/tag.c` | uncovered in this packet |
| `src/gameplay/modes/team_info.c` | uncovered in this packet |
| `src/gameplay/modes/teams.c` | uncovered in this packet |
| `src/gameplay/modes/vote.c` | uncovered in this packet |
| `src/gameplay/operation.c` | full source read |
| `src/gameplay/pickups.c` | full source read |
| `src/gameplay/policies.c` | full source read |
| `src/gameplay/provenance.c` | full source read |
| `src/gameplay/q3/checkpoint.c` | full source read |
| `src/gameplay/q3/death.c` | full source read |
| `src/gameplay/q3/feedback.c` | full source read |
| `src/gameplay/q3/game.c` | full source read |
| `src/gameplay/q3/holdables.c` | full source read |
| `src/gameplay/q3/internal.h` | full source read |
| `src/gameplay/q3/items.c` | full source read |
| `src/gameplay/q3/map/checkpoint.c` | full source read |
| `src/gameplay/q3/map/internal.h` | uncovered in this packet |
| `src/gameplay/q3/map/items.c` | uncovered in this packet |
| `src/gameplay/q3/map/misc.c` | uncovered in this packet |
| `src/gameplay/q3/map/movers.c` | uncovered in this packet |
| `src/gameplay/q3/map/runtime.c` | uncovered in this packet |
| `src/gameplay/q3/map/spawn.c` | uncovered in this packet |
| `src/gameplay/q3/map/targets.c` | uncovered in this packet |
| `src/gameplay/q3/map/triggers.c` | uncovered in this packet |
| `src/gameplay/q3/missiles.c` | full source read |
| `src/gameplay/q3/movers.c` | full source read |
| `src/gameplay/q3/player.c` | full source read |
| `src/gameplay/q3/view.c` | full source read |
| `src/gameplay/q3/weapons.c` | full source read |

## Remaining concerns

- This worker has not completed all authored Q3 map files or most mode files. Their coverage is listed explicitly above.
- Whole-provider lifetime and all possible cross-actor callback paths are not qualified by this bounded patch. Further findings must name a concrete path and respect the public callback contracts.
- Complete checkpoint publication, game-family interoperability, gameplay behavior and performance remain outside this source-only packet.

## Independent checks of other packets

The Q2 packet was reviewed without editing Q2 files. Its seven source hashes matched the frozen register in `review-20260929-q2.md`. Read the complete correction diff, complete beam/controller and Widow power helper bodies, focused entity spawn/health projection, checkpoint publication and retirement, actor invocation/destruction, and the unsigned timer helper. Traced `q2_checkpoint_idle`, `q2m_retire_monster` and its reclamation owner. Before correction, independently confirmed that direct `FOUND_TARGET` could reach a mission callback with `current_actor` clear, permit game destruction, and resume through freed game state. The final typed wrapper holds the existing actor invocation through this path, so destruction now rejects it. The other reviewed corrections retain pending state, compare original extension/controller identity, and preserve the Widow timers. The dormant Widow helper's no-enemy behavior matches `widow/common.ts`; gameplay callers and damage multiplier consumers remain absent as recorded by its author. No additional confirmed defect was found in this bounded Q2 packet. This is source acceptance, not full Q2 implementation or runtime verification.

Separately checked the presentation worker's LMCTF view hook at `src/app/application/match.c`, SHA-256 `ad32a2c2cb736827c0949c5a9726657e5372327f8fa5ebcc271ac5ff7a4dc290`. The hook requires active controls and the full actor identity before reading canonical camera angles. `control.c` retains camera pitch while Q2/Q3 body orientation flattens pitch. The drop consumer in `modes/objects.c:644` converts the supplied view to the toss direction, matching the donor flags and runes' preference for player view state followed by body orientation. The fallback still uses world body angles when no matching controls exist. No confirmed defect was found in that targeted change; other modes and actual gameplay were not qualified by this check.
