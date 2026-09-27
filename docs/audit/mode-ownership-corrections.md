# B14 mode ownership corrections

This is a source correction packet for QM-02, QM-03, QM-11, and the mode/equipment
admission portion of QM-10 in `docs/audit/q3-modes.md`. It supplements that frozen
audit. It does not close B14 or the other Q3 findings.

Reviewed on 2026-09-27 against base commit
`93f8146e850fd38bf0e6e82cfd461436c48e4ca8` and the working changes listed below.
The tracked owned diff from that commit has SHA-256
`09749534f47e00157b75dcb3881eef0997ed4dc865136056a7cd748c62462f36`.
The additional untracked `src/gameplay/modes/give.c` has SHA-256
`d71bc645371d86518fb214d2bbf08d7cdf6388be289b304fd4edaca04eac4548`.
These identify the source before the coordinator's formatting and integration.

## Original acceptance standard

The existing plan remains unchanged. `docs/dependencies.json:227` records B14:

- Goal: "Implement independent match modes, teams, scoring, cross-map objective adaptation, grapples, and offhand equipment."
- Criterion: "Required modes and equipment variants are connected to shared gameplay without coupling map, arsenal, or movement selection."

The donor's `docs/functional-targets/README.md:20` defines game modes as an
independent rules selection. Its lines 12-22 distinguish one shared service from
selectable source behavior, and require the full feature union.
`docs/functional-targets/status.md:89` explicitly retains simultaneous cross-game
composition as unfinished required work. The C design therefore supports multiple
mode instances sharing the canonical world and player. It does not restrict an
actor to one mode to avoid state conflicts.

## Changes and source evidence

### QM-02: player state belongs to a mode

`include/qa/modes.h:128` now separates canonical `qa_match_player` connection
identity from `qa_mode_player_state`. Each instance retains its own team, score,
wins/losses, spectator state, follow target, ready state, and team leader.
`qa_mode_player_view` identifies both the mode and the canonical connection.
`src/gameplay/modes/internal.h` stores those values in instance members, with a
separate instance-owned table for external score/team bindings.

The public read/write/binding APIs require `qa_mode_id`.
`core.c:423` resolves the exact member; `core.c:435` binds that member's external
owner; `core.c:472` and `core.c:527` read its score/team. There is no implicit
fallback from a mode team to shared combat team. Leases retain mode identity,
actor generation, and serial. External read/write callbacks are followed by
member and binding identity checks. Unbinding preserves the last source score
and team only while the same binding still owns that member.

The caller migration covers team admission and observer/follow commands in
`teams.c`, ranking and match transitions in `match.c`, votes and leader actions
in `vote.c`, team reports in `team_info.c`, spawn selection in `placement.c`, and
scoring/objective actions in `scoring.c`, `flags.c`, `horde.c`, `arena.c`,
`deathball.c`, and `tag.c`. Connection name, bot status, and connected status
remain shared. Respawn, spectator, and intermission hooks carry the originating
mode identity so application composition can arbitrate their physical effect.

`include/qa/modes_save.h` and `checkpoint.c:68` define checkpoint version 2.
Members save their mode-owned player state and external owner identity. External
score/team values remain with their source owner. `checkpoint.c:303` restores
the declared binding through a mode-aware resolver. Validation checks object
ownership and carrier membership before restore begins, including base, ball,
tag, dropped-object, held-flag, and held-relic references.

### QM-03: damage selects the actual instance

`arena.c:27`, `qa_modes_object_damage`, receives an explicit mode ID. It returns
without modification for disabled instances, another mode's object, or an
ordinary target that is not a nonspectating participant. It no longer loops over
all enabled Tag/DeathBall instances. Obelisk awards and ball last-touch tracking
only credit participants in the object's own instance. Ball touch also checks
membership. `damage.c` scopes Threewave immunity to its member target;
`relics.c` scopes grapple rules to its enabled instance and participants.

The admitted source rule is preserved. Donor
`src/content/q2/missionpacks/modes/tag.ts:45` truncates damage to three quarters
when neither actor owns the tag. `deathball.ts:83` sets ball damage to one and
halves other damage unless the ball caused it; lines 84-103 retain the source
means-of-death knockback table. The corresponding C rules remain in `arena.c`.
This packet changes ownership and dispatch, not those intentional source rules.

### QM-11: inventory handles identify their mode

`items.c:5`, `mode_inventory_item`, interns and retains one handle per mode/source
item pair. The key includes provider owner, mode slot, mode generation, and
source item name. Source identifiers remain available as `qa_mode_item.source_item`;
the inventory definition carries the distinct scoped handle.

`items.c:81`, `qa_modes_item_action`, requires the explicit mode and its scoped
handle. The shared inventory action callback resolves that handle to its one
mode, rather than selecting the first instance with the same source item.
`objects.c:33` aggregates carried counts only within that mode and source item.
Missing entries use shared canonical inventory admission, including native
supplemental entries when a foreign provider owns the primary inventory.
`give.c:204` likewise requires mode identity and the scoped handle.

`items.c:104` replaces the actor's existing descriptor lease atomically through
`qa_inventory_replace_definitions`; it no longer closes the old definitions
before the replacement can succeed or allocates a copy of the old catalog.
Mode item mappings are checkpointed. Restore rejects duplicate mappings and
inventory-handle collisions across saved modes. Objective bindings and saved
external objective records also retain mode identity; zero mode identity denotes
the shared campaign channel. Binding rejects nonexistent mode identities.

### QM-10: staged mode object and equipment admission

`objects.c:158`, `spawn_object`, reserves the authored actor before its first
external body read. It stages the mode object and objective registration without
exposing an active object. Resources, source placement, body/collision updates,
and link callbacks complete before a missing local combat record is created.
`objectives.c:13` reserves duplicate identity without making it discoverable;
its commit publishes the prepared binding without callbacks or allocation.

Authored body and combat changes retain their original storage serials. Failure
restores body, collision, link state, health, and traits only if the actor
generation and exact storage owner still match. Checks between callback-bearing
restoration operations avoid overwriting a replacement owner. A cleanup failure
is returned. Newly created actors and Rogue helper bases are released on
failure; authored actors are not retired by cleanup. A missing Q1 floor keeps
the original successful omission behavior after cleanup. Same-base reservations
also prevent callback reentry from creating duplicate bases.

`equipment.c:52` prepares Q3 selection, Q2 grenade state, and canonical ammo
admission without publishing the new equipment selection. At `equipment.c:65`,
inventory validation is the final operation that may invoke a provider. Native
Q2/Q3 validations then check retained identities, followed by callback-free,
allocation-free commits and local selection publication. Failed preparation
aborts each reservation. Existing cooked grenade state remains unchanged until
commit. A requested source grapple holster/release can retain its normal source
side effects; the transaction prevents partial admission of the new selection,
not cancellation of an already requested physical holster.

## Shared dependency review

The source review included these related changes owned by other workers:

- `gameplay.h`/`combat.c`: `qa_combat_storage_serial` identifies the live exact
  primary binding. `world.h`/`body.c` retain the equivalent body serial. External
  body reads/writes and link callbacks reject same-slot replacement; legal actor
  retirement suppresses stale link notification.
- `inventory.h`/`inventory.c`: staged admission appends only missing native
  entries after validating the store, revision, and owners. Commit has no
  provider calls or allocation. Definition replacement preserves an existing
  external override when refreshing its old native fallback. Source snapshots
  expose `primary_native_count` and `primary_external`; callbacks now check
  store revision immediately in `binding_count` and `binding_at` before further
  indexed enumeration. These changes are committed in the base snapshot.
- `game_q2.h`, `q2/hand_grenades.c:111`, and `q2/game.c:21`: grenade validation
  uses the retained body storage serial, not an external body read. Commit
  publishes a prepared actor extension and retained values only. These changes
  are committed in the base snapshot.
- `game_q3.h`, `q3/player.c:32`, `q3/internal.h`, and `q3/game.c`: Q3 begin
  reserves a per-slot token without creating a player sidecar or selections.
  Validate/commit read retained values only. Rollback clears only its exact
  reservation and accepts prior retirement cleanup. The Q3 worker owns these
  uncommitted dependencies. `player.c` contains the token change; the other three
  files also contain unfinished map work, requiring selective integration or the
  later coherent map handoff.

## Reviewed correction files

All changed owned production files were source-reviewed. This list is the
bounded correction coverage, not a second claim to cover every Q3 source file.

- `include/qa/modes.h`
- `include/qa/modes_save.h`
- `src/gameplay/modes/arena.c`
- `src/gameplay/modes/checkpoint.c`
- `src/gameplay/modes/core.c`
- `src/gameplay/modes/damage.c`
- `src/gameplay/modes/deathball.c`
- `src/gameplay/modes/equipment.c`
- `src/gameplay/modes/flags.c`
- `src/gameplay/modes/give.c`
- `src/gameplay/modes/horde.c`
- `src/gameplay/modes/internal.h`
- `src/gameplay/modes/items.c`
- `src/gameplay/modes/match.c`
- `src/gameplay/modes/objectives.c`
- `src/gameplay/modes/objects.c`
- `src/gameplay/modes/placement.c`
- `src/gameplay/modes/relics.c`
- `src/gameplay/modes/scoring.c`
- `src/gameplay/modes/tag.c`
- `src/gameplay/modes/team_info.c`
- `src/gameplay/modes/teams.c`
- `src/gameplay/modes/vote.c`

`include/qa/equipment.h` was read but did not require a contract change. No
changed owned file is intentionally left unread. Files outside this list and
the bounded shared dependency reads retain the original audit's coverage and
open findings; this packet does not reassess them in full.

## Checks and remaining integration

`git diff --check` passed for the tracked owned correction paths. Source searches
found no current outside caller of the changed mode APIs or old
`qa_match_player` layout in `src`, `include`, or `tests`. No per-file copyright,
SPDX, or license notice was introduced. These are source checks. No build,
configure, compiler, test, project executable, benchmark, or gameplay was run.

The following work still prevents full B14 acceptance:

1. `give.c` remains absent from the mode source list in `CMakeLists.txt` at this
   snapshot. Root owns that QM-07 correction and the coherent commit.
2. `src/main.c` and `src/session` have no `qa_modes_` or `qa_equipment_` callers.
   B34 must compose mode damage, scoring, inventory actions, source equipment,
   actor release, player phases, and events with the selected shared services.
   Calls must carry the intended mode identity; composition must not substitute
   the first enabled mode. One mode's spectator/respawn/intermission intent must
   not silently seize canonical body ownership from another active mode.
3. B30 must serialize/remap checkpoint version 2 fields, mode identities, actor
   references, scoped item strings, external owner bindings, and the shared
   inventory snapshot partition. The typed restore contract uses a prepared
   replacement world/modes instance; it is not an in-place rollback operation.
4. Q3's dependent admission token changes need coordinator integration. Other
   Q3 admission defects in QM-10 and other findings remain with their assigned
   owners. This packet does not claim those corrected.

The named mode ownership defects have source corrections ready for independent
review. The original B14 goal and criterion remain open until the required
application composition exists. Report-honesty and task-acceptance judgments are
recorded separately so an honest incomplete report cannot be mistaken for
completed implementation.
