# World, session, movement, navigation and network source review

Reviewer: `/root/review_world_network`, GPT-6.1 Sol, high effort. Date: 2026-09-29.

This is a bounded source review and repair contribution to the coordinator's AUDIT work. It does not establish complete subsystem coverage, runtime correctness, BASELINE completion or full game parity. No compiler, executable, test runner, parser, sanitizer or benchmark was run. Existing unrelated edits were preserved. Only the coordinator stages and commits.

## Confirmed defects and repairs

| Severity | Finding | Source failure trace | Repair ownership |
| --- | --- | --- | --- |
| High, CONFIRMED | Application retains a consumed session after cleanup failure. | `qa_session_destroy` rejects unsafe calls/admissions before mutation, but an admitted call clears actors/components, closes the world and frees the session even if the recorded fault makes its result false. The former application finalizer treated every false result as an unconsumed owner, allowing retry against freed storage. | This contribution exposes `qa_session_destroy_ready` in `src/session/session.c:746` and documents consumption in `include/qa/session.h:78`. The query shares the destroy admission predicate. The application reviewer owns pointer clearing and retry handling in `src/app/application/lifetime.c` and `owner.c`. |
| High, CONFIRMED | A direct world callback can destroy a gameplay provider while its caller still uses it. | Q2 reviewer supplied `qa_q2_monster_action` -> `q2m_refresh` -> `qa_world_body_read` -> external binding `read` -> `qa_q2_destroy`. Outside a session actor turn, the session and Q2 local actor guards are idle. Returning from the callback reaches `q2m_alive` with a freed game. Independently read that call path and the world callback-depth increment. | This contribution exposes `qa_world_idle` in `src/world/body.c:64` and shares it with world destruction. Q2 reviewer owns provider destroy guards; application reviewer owns application destroy/finalize/configuration/publication guards. The query excludes callbacks and spatial visits; geometry admissions retain their separate ownership contract. |

The session query accepts NULL, rejects session/scheduler activity and either admission set, and allows destruction of a faulted idle session. After it returns true, the immediately following admitted destroy consumes the owner even if a release callback records a new fault. There are no callbacks between the application's final query and its destroy call. The world query returns false for NULL, matching the existing combat idle convention; nullable application callers skip the query for NULL worlds.

## Independent repair review

Read the application's complete `lifetime.c`, `owner.c` and `composition.c`, plus actor-release/motion-recording sections of `services.c`, the publication readiness boundary, selected control movement/cutscene functions, and complete `src/presentation/q3/audio.c`.

The application session repair clears session/world pointers after an admitted destroy, retains the outer owner when cleanup reports an error, and skips session-dependent provider draining on retry. `application_fault` does not dereference the session. World-idle guards reject direct body-callback teardown before destructive composition work and again at finalization. The application reviewer independently reviewed both shared queries and reported no defect in the narrow interface changes.

The targeted control review found no confirmed defect in the movement operation lease, first-entry cinematic saved-state preservation, or view-height restoration. A suspected repeated-cinematic reset was refuted: `application_record_motion_change` records a continuation and does not call the cinematic reset helper. The audio listener now obtains the same canonical actor identity service used by positional and looping sounds before contributing the listener. This is a targeted peer review, not clearance of every application/presentation file.

Peer source anchors: `owner.c:381` public destroy readiness; `lifetime.c:109` and `:155` finalizer readiness and consumption; `composition.c:5` configuration readiness; `publication.c:371` publication readiness; `control.c:862` prior-operation save and `:935` restoration; `control.c:1097` first-entry cinematic save; `control.c:1175` restored view height; `services.c:852` motion recording; `src/presentation/q3/audio.c:135` canonical listener mapping. `match.c:83` now prefers generation-matched admitted control view angles and otherwise reads the authoritative body; this narrow function was also reviewed.

Peer file SHA-256 snapshot after the final world-idle guards were read:

```text
47d2e312c18a8a21dccb08964d34bd16d4b4036a07382bd497341905cc8c7030  src/app/application/lifetime.c
a094e83a432af0f14c8b71f5408970ebb7d3a8ec7597aab527c9681e36f3cf59  src/app/application/owner.c
447ddb4d7e5dcfb3dfdf1af37be6ef8453c8aa832335e928a205d2f2048cccbb  src/app/application/composition.c
affb4184a689016f8c52db4f95739ab316651c14c77382bfabf46192d5653cb1  src/app/application/publication.c
8219912caa65972e40c02006759732732b702849c506d61e67fcbfdb2cdf5655  src/app/application/control.c
51c9ed85345a358d5024941c01df2bc48b99082e83cd93c10ddd2f710d2c8549  src/app/application/services.c
7af28936aad25b913570a2be20e014b2e07d9b371c118eb81c73f8619c77cf4f  src/presentation/q3/audio.c
```

## Other refuted findings

- Q3 mover rollback saves a yaw word through a float. The donor explicitly uses `f32(check.client.ps.deltaAngles.y)` for that saved field, so the conversion alone is not a demonstrated port regression.
- Q1 toss/step water processing returns early on unobstructed movement. The donor physics path also returns when the trace fraction is one before its water-transition code. No behavior change was made on the basis of that suspicion.
- Q3 portal visibility retains an area-mask accumulator. Its callback contract explicitly ORs into the supplied accumulator; clearing it between portal recursion would lose accumulated visibility.
- Q2 frame baseline lookup assumes sorted unique baseline entities. The public frame contract requires that ordering; absence of repeated per-lookup validation is not a defect.

## Inspection coverage

Complete source reads in this contribution:

```text
src/world/actors.c
src/world/body.c
src/world/spatial.c
src/world/collision/world.c
src/world/collision/world_internal.h
src/session/session.c
src/session/scheduler.c
src/session/configuration/transaction.c
src/movement/common.c
src/movement/prediction.c
src/movement/entity.c
src/movement/entity/internal.h
src/movement/entity/trajectory.c
src/navigation/checkpoint.c
src/navigation/runtime.c
src/navigation/world.c
src/navigation/internal.h
src/navigation/route.c
src/navigation/prediction.c
src/navigation/train.c
src/navigation/eligibility.c
src/navigation/graph.c
src/navigation/aas_validate.c
src/network/message.c
src/network/reliability.c
src/network/connections.c
src/network/transport.c
src/network/socks.c
src/network/unified/channel.c
src/network/unified/packet.c
src/network/q1/channels.c
src/network/q1/demos.c
src/network/q1/history.c
src/network/q2/channel.c
src/network/q2/frames.c
src/network/q2/zpacket.c
src/network/q2/codec.c
src/network/q2/connectionless.c
src/network/q3/channel.c
src/network/q3/clock.c
src/network/q3/visibility.c
src/network/q3/pure.c
src/network/q3/delta.c
src/network/q3/codec.c
src/network/q3/admission.c
include/qa/actors.h
include/qa/session.h
include/qa/scheduler.h
include/qa/world.h
```

Partial or targeted reads, requiring further complete review:

```text
src/movement/entity/q3_mover.c
src/network/q2/mvd.c
include/qa/physics.h
include/qa/navigation.h
include/qa/network_q3.h
include/qa/network_q2_messages.h
```

Selected donor comparisons were read from `../quake-typescript/src/content/q3/base/game/mover.ts`, `src/app/bootstrap/simulation/physics.ts`, `src/network/q3/visibility.ts`, `src/network/q3/clock.ts`, and `src/network/q2/frames.ts`. Those comparisons were read only and were not copied into the C tree.

Explicitly uninspected implementation files in the assigned source trees:

```text
src/world/collision/contents.c
src/world/collision/geometry.c
src/world/collision/internal.h
src/world/collision/q1.c
src/world/collision/q1/geometry.c
src/world/collision/q1/geometry.h
src/world/collision/q2.c
src/world/collision/q3.c
src/world/collision/q3/model.c
src/world/collision/q3/patch.c
src/world/collision/q3/patch.h
src/world/collision/q3/shared.h
src/session/configuration/draft.c
src/session/configuration/internal.h
src/session/configuration/presets.c
src/session/configuration/validate.c
src/movement/internal.h
src/movement/entity/monsters.c
src/movement/entity/pushers.c
src/movement/q1/common.c
src/movement/q1/common.h
src/movement/q1/netquake.c
src/movement/q1/quakeworld.c
src/movement/q2/classic.c
src/movement/q2/rerelease.c
src/movement/q3/local.h
src/movement/q3/move.c
src/movement/q3/slide.c
src/navigation/aas_file.c
src/navigation/aas_sample.c
src/navigation/asset.c
src/navigation/asset_internal.h
src/navigation/construct.c
src/navigation/estimates.c
src/navigation/graph_asset.c
src/navigation/nav_file.c
src/navigation/spatial.c
src/navigation/travel.c
src/network/dosbox.c
src/network/ipx.c
src/network/socket_private.h
src/network/q1/checksum.c
src/network/q1/commands.c
src/network/q1/discovery.c
src/network/q1/handshake.c
src/network/q1/netquake.c
src/network/q1/quakeworld.c
src/network/q1/scalar.c
src/network/q1/session.c
src/network/q1/token.c
src/network/q2/checksum.c
src/network/q2/classic_internal.h
src/network/q2/fog.c
src/network/q2/gtv.c
src/network/q2/handshake.c
src/network/q2/internal.h
src/network/q2/kex_channel.c
src/network/q2/kex_discovery.c
src/network/q2/kex_game.c
src/network/q2/kex_game_internal.h
src/network/q2/kex_lan.c
src/network/q2/kex_packet.c
src/network/q2/messages.c
src/network/q2/q2pro.c
src/network/q2/q2pro_fields.c
src/network/q2/q2pro_internal.h
src/network/q2/r1q2.c
src/network/q2/rerelease.c
src/network/q2/temp_entities.c
src/network/q2/vanilla.c
src/network/q3/client.c
src/network/q3/huffman.c
src/network/q3/huffman_internal.h
src/network/q3/messages.c
src/network/q3/pak_references.c
src/network/q3/peer_internal.h
src/network/q3/server.c
src/network/unified/commands.c
src/network/unified/composition.c
src/network/unified/document.c
src/network/unified/schema.c
src/network/unified/value_internal.h
```

Corresponding public headers not completely inspected:

```text
include/qa/collision.h
include/qa/movement.h
include/qa/navigation_asset.h
include/qa/network.h
include/qa/network_q1.h
include/qa/network_q1_channel.h
include/qa/network_q1_nq.h
include/qa/network_q1_qw.h
include/qa/network_q1_session.h
include/qa/network_q2.h
include/qa/network_q2_batch.h
include/qa/network_q2_kex.h
include/qa/network_q2_kex_game.h
include/qa/network_q2_mvd.h
include/qa/network_unified.h
```

## Checks and remaining work

`git diff --check -- include/qa/world.h src/world/body.c include/qa/session.h src/session/session.c` passed. Source comparison confirmed that each new predicate matches the previous rejection conditions and that nullable application consumers handle their differing NULL conventions. Four owned code/header files are frozen for coordinator review; the application and Q2 reviewers own their dependent call-site fixes.

Further source coverage is required for the explicitly uninspected files and partially read files above. Family movement, geometry kernels, protocol extensions, and complete end-to-end caller ownership remain outside this bounded review's conclusions. Runtime validation remains deferred under the user's source-only baseline rule. No failing runtime test is claimed, and no passing runtime result is claimed.

No real external agent session ID was exposed to this child, so it did not invent a Jev project-join identity. The coordinator was notified to record this bounded contribution against existing AUDIT work item `w_357ddd74810a4aa8a3bb84fdd7095fb8`, coordinator session `s_68a7c43280594f24b1cc6f98bd5aab9d`, plan revision 6. Direct ledger acceptance/judgment of this contribution is not claimed.
