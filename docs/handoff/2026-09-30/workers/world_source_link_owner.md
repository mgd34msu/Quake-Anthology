# World source link owner handoff — 2026-09-30

## Active task and disposition

Parent requested independent, source-only review of all five dynamic Q3 wire files in candidate 3, with genuine native source/session/world consumers and TS/SDK callbacks. The user has requested a handoff. Review stops here. **Candidate 3 is not independently accepted by this reviewer.** The five files were read completely, but the requested donor/caller coverage is not complete. No confirmed new defect has been reported in this packet.

No project code changed during this review. No compiler, parser, script, generator, configure, build, test, game, benchmark, or other project executable ran. No  mutation or descendant agent occurred.

## Exact packet held

Manifest: `/tmp/qa-q3-dynamic-wire-candidate3-20260930.sha256`

Manifest SHA256: `94b63a115c3307e73b115b4a7a78ff3ef8d73d460e3fc9f33993ebfc6d98d2fb`


| File | SHA256 |
| --- | --- |
| `src/gameplay/q3/items.c` | `905e161bad31a5e58b9ca97b0caf05e42d96f1f4991e1b4f9a5c4d565f929f47` |
| `src/gameplay/q3/missiles.c` | `61d05b63fe33466dabaf9a9db2b2b8b8a393ed092be82dba4fb989d2ec7049f8` |
| `src/gameplay/q3/death.c` | `cc993955f4a05e419daa1b533355f378ff688ed091f1367632b95b5c0b9ca0e5` |
| `src/gameplay/q3/holdables.c` | `b107b6fde4244ab845c1e0caaa280ed788c30a51cf448a5f99d2f5da21acfdc8` |
| `src/gameplay/q3/feedback.c` | `2ed23927f0b9322de4bf561dcdf6b3f66c855eeda5214fdd1bbfadaa73fbf101` |

Corresponding core v9 manifest: `/tmp/qa-q3-source-core-v9-20260930.sha256`, 25 entries, manifest SHA256 `a7b29487efb9459ad6322acca997c4b32800acffa29e8f88d27cae9ffb622f64`. All 25 hashes matched once during this review. **The full 25 files were not source-read by this reviewer.** Their relevant consumer regions were read as listed below. This is not core v9 acceptance.

## Prior findings checked in current source

Parent identified the three previous authority defects. Each current repair was read in the complete candidate files and traced into actual native source/session consumers:

- `items.c:276`: denied-powerup same-team checks read actual native `clients[slot].session.team`; canonical combat team remains the foreign-actor fallback.
- `death.c:167`: death-reward same-team checks read each actual native session team, retaining combat team for foreign actors.
- `holdables.c:64`: teleport spectator gating qualifies the true native client and reads actual `session.team == 3`; selected character traits remain the foreign fallback.
- `items.c:474`: a physical source row that has neither real item lifecycle nor authored map item returns `QA_ERROR_NOT_FOUND`. The actual TEAM qualification consumer in `native_q3_objectives.c:695` treats that result as a non-objective and propagates other failures.
- `items.c:497` and the map item constructors admit only item definitions of `QA_Q3_ITEM_TEAM` into objective hooks. The actual TEAM bound consumer also checks the true product item kind.

These repairs are source-confirmed. They do not establish whole-packet acceptance or mixed-game runtime parity.

## Coverage completed

Complete five-file reads:

- `items.c`, all 1292 lines, including finish/adoption, original pickup and continuation, visibility/event lifetime, respawn, dropped TEAM expiry/NODROP, motion and touch dispatch.
- `missiles.c`, all 1036 lines, including real dynamic constructors, owner/team source numbers, grapple/prox attach and trigger, impact/explosion, event lifetime, and motion/link ordering.
- `death.c`, all 606 lines, including logs/obituary source fields, real ES drop origin/angles, cleanup, fixed eight-body queue, copy/sink/motion and kamikaze reassignment.
- `holdables.c`, all 552 lines, including teleport, portal constructors/use, kamikaze constructor/area/quake effects and real physical client iteration.
- `feedback.c`, all 254 lines, including native death callbacks and native END prepare, source PM_DEAD policy, source world effects, damage feedback, source flags and genuine loop sound.

Actual consumer reads:

- Complete `src/app/application/native_q3_end_frame.c`. Actual source END frame qualification, physical client/session retention, water read, END prepare, BG conversion and pending predictable publication are wired in that order.
- Complete `src/gameplay/q3/map/items.c`, including true authored item initialization/finish, TEAM adoption, team-chain respawn and pickup scheduling.
- `src/app/application/native_q3_objectives.c:320-430` and `530-745`: real definition/adoption, item admission/drop/clear/expiry/NODROP and native source bound/view qualification.
- `src/gameplay/q3/source_wire.c:1-245`, `365-750`, `840-1335`: actual physical wire rows, body copy, true PS/shared authority, BG conversion, source link, ES/body read, temp/predictable/addEvent/event expiry consumers. The remainder was not read in full.
- `src/gameplay/q3/source_effects.c:1-220`: actual shared body/currentOrigin scope, source intermission and score event producers. Remainder not read in full.
- `src/gameplay/q3/source_entities.c:1-210`: fixed client/source binding, actor generation qualification and client admission. Remainder not read in full.
- `src/gameplay/q3/game.c:462-510` and `730-815`: actual actor traits, event expiry before source actor dispatch, physical END iteration.
- `src/gameplay/q3/player.c:1210-1400` and `1510-1635`: timer tail, native world effects, actual source command finish/timer caller, movement water and source touch policy.
- Actual selected character trait routing in `src/app/application/services.c:65-115`, `350-445`, `731-770`, and Q1 trait producer `src/gameplay/q1/runtime.c:1434-1472`.

TS donor reads under `/home/buzzkill/Projects/quake-typescript/src/content/q3/`:

- Complete `team-arena/client-effects.ts`: actual world effects/feedback/timer/powerup/END ordering and pending predictable events.
- Relevant complete item motion callback bodies in `base/game/item-motion.ts`, including bounce/run/launch/drop and save callback registration.
- `base/game/item-lifecycle.ts:240-530`: respawn, Touch_Item original/complete, actual supply ownership and FinishSpawningItem callbacks.
- `base/game/misc.ts:1-165`: killbox/teleport and authored portal callbacks.
- `team-arena/client-spawn.ts:225-280`: actual body queue copy.
- `base/game/personal-portal.ts:60-170`: real drop/carried flag/portal touch/enable/destination callbacks. Remainder not read in full.
- `base/game/weapon.ts:259-345`: actual kamikaze start, area and shake callbacks.

SDK donor reads under `/home/buzzkill/Projects/qsrc/quake-iii-arena/code/game/`:

- `g_active.c:1-205` and `1100-1235`: world effects, damage feedback, source loop sound and ClientEndFrame.
- `g_client.c:375-473`: real CopyToBodyQueue.
- `g_weapon.c:1030-1165`: real KamikazeDamage and G_StartKamikaze.

## Refuted apparent defects

- Native END EF_CONNECTION is authored into source entity flags and then overwritten by BG conversion from PS. Both SDK ClientEndFrame and the TS donor do this same ordering. Do not report that overwrite as a defect without new evidence.
- Native corpse code overwrites copied source ground using canonical body ground. The SDK alone uses copied ES ground, but the TS behavioral oracle deliberately calls `writeGround(body, entity.binding.body.read().ground, ...)` and bases corpse gravity on the shared body. Do not call this a defect on SDK evidence alone.

## Unresolved authority question

`holdables.c:512-525` asks the generic selected CHARACTER `actor_traits` callback whether each genuine native source client is grounded; only absent traits use the Q3 source player's ground number. TS `weapon.ts:324` uses the canonical shared body's `ground !== null`; SDK uses genuine client PS ground. Actual application traits first select CHARACTER, and Q1 traits can read their own source physics ONGROUND flag instead of the canonical body. This appears able to make a native Q3 kamikaze consume the wrong quake RNG and skip/add grounded velocity kicks with mixed CHARACTER/MOVEMENT selections. **This is an unresolved candidate, not a confirmed defect:** no complete admission/movement trace was read to prove those ground authorities can differ in a reachable configuration. The next reviewer should trace the real Q1 selected-character binding and movement ground updates, then either refute or report a concrete scenario to `q3_dynamic_wire_owner` and root before acceptance.

## Remaining holds before independent acceptance

1. Read the exact TS and SDK missile/grapple/proximity callback bodies and native lifecycle consumers; current review read complete C candidate missile logic but has not read those donor bodies.
2. Read exact TS/SDK death cleanup/reward/drop callbacks, beyond body queue and kamikaze callbacks already read.
3. Finish required source/session/world caller coverage in core v9 and confirm timer ownership against actual command admission. No full core v9 acceptance by this reviewer exists.
4. Resolve the kamikaze grounded-traits question above.
5. Recheck the current frozen manifest and scoped whitespace immediately before a final bounded verdict. If any writer changes the packet, use the new manifest and reread the affected complete files/call paths.

No outer-family, full campaign, mod composition, network/runtime or executable parity claim is authorized by this record.

## Earlier completed work in this lane

### World bounds plus origin API

Implemented only `include/qa/world.h` and `src/world/body.c`: `qa_world_link_bounds_at(world, actor, absolute_bounds, origin_override, error)` passes explicit world bounds and an optional finite link-snapshot origin into existing `link_body`. Existing API delegates with a null origin. Authoritative body origin remains unchanged; existing publication generation, callback, serial and spatial checks are retained.

Frozen manifest `/tmp/qa-world-source-link-origin-20260930.sha256`:

- world.h `ff9253e99c22e63aad84bf4e06a6a29e186e63b8404d18f5dfa238469dbce416`
- body.c `c8f200a623c7aece5cf88989714c403fad82bc26ee14816a40b578f004c33cef`

`movement_source_peer` independently accepted the complete two files and actual Q3 wire caller, with repeated exact hashes and scoped whitespace. Root already received acceptance. This lane made no commits.

### QVM foundation and actual body adapter review

Independent source review found a real nested evaluation-floor defect: nested default/explicit evaluation reservation could lower the active source scratch lease floor. `application_event_owner` repaired default floor inheritance and rejection of explicit lower floors. Refreshed complete foundation five and actual adapter two were source-accepted after whole reads, repeated hashes and scoped whitespace, with no executable run.

Foundation manifest `/tmp/qa-qvm-source-call-20260930.sha256`; accepted execute.c SHA256 `ad2aecbb837529962d89a6afdb1d5dadd5c02d632cc7fbfc7cad84ed615fcf2e` (other four entries retained). Adapter manifest `/tmp/qa-qvm-body-control-20260930.sha256`; accepted C SHA256 `5a1d4654359bb906fcb9eb8cf997980cb05fb44fdd30570d93ea5bcc6ed16d2f`, header `a71d1bd0411edea8841b0a7dccc36f5566c84f3ac905ea9438125ef55a9671a8`. Adapter targets genuine retained client-envelope ancestor cancellation and requalifies real actor/source pointers before and after fallible callbacks and scratch cleanup.

The separate outer movement caller packet was not accepted by this lane. Earlier acceptance applies to those exact frozen packets only; rehash before relying on it.

