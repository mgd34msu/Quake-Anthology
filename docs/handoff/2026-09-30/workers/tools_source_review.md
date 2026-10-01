# tools_source_review handoff, 2026-09-30

## Current assignment and safe boundary

Root assigned the native Q3 ClientThink pipeline. My exclusive implementation
paths are the two new files:

- `src/app/application/native_q3_control.c`
- `src/app/application/native_q3_control.h`

Both are frozen. There is no independent peer acceptance of this new2 packet yet.
This is an implementation handoff with explicit unresolved integration work.
No further code changes or executable validation were performed after the freeze.
Root remains the owner of factory/public service declarations and TU integration;
movement_resume owns control.c, control_frame.c/.h, session and raw intake.
No descendants were created.

Exact manifest: `/tmp/qa-native-q3-control-20260930.sha256`

```
7bdacb632c39ffc43f89cc6984791eccdb42bdc2c20ba3a71f24e6ae2b26a553  src/app/application/native_q3_control.c
955189781842b104e407625b2c837a25203eefb2bbf1d96bfa70878fec69d79f  src/app/application/native_q3_control.h
```

Evidence: `/tmp/qa-native-q3-control-source-20260930.md`.
The final whole files were reread, SHA256 checked twice, and scoped whitespace
inspection emitted no diagnostics. All validation is source-only. The global
gate forbids project execution, including builds, compilers, tests, scripts,
parsers, syntax checks, sanitizers and game runs.

## Actual producer and clock contract

movement_resume atomically removed the former helper/projection/callback block
from control.c. control_frame.h includes native_q3_control.h. Actual ClientSpawn,
async raw queue drain and G_RunClient call the new public private helper
`application_control_q3_client_think`.

Raw transport receipt remains separate from applied selected controls. Async
physical native commands drain through this helper. Bots and cached synchronous
clients retain the genuine pers.cmd/lastCmdTime at receipt; G_RunClient sets the
actual source GAME time and invokes the helper once at its admitted source actor
phase. Spawn now passes the original received engine command, retained before
pers.cmd.serverTime is overwritten by the source entry time. It no longer forces
an invented 100ms move.

The helper borrows the real native console/provider lifetime, preserves
CONFIGURING, temporarily changes IDLE to ADVANCING, and restores the prior
operation on every exit. READY is admitted only during CONFIGURING. Source
policy is called once. Original wrapped policy msec is retained for timer
actions, separately from post-fixed mathematical serverTime-minus-PS-commandTime
clamped to 0..200 for the real qa_session_command_call.

qa_session_command_call supplies the actual owner, actor, clock kind, completed
clock metadata and command interval, with no fabricated world frame, debt or
counter advance. Movement's source_usercmd elapsed now derives that actual
admission interval, including the selected NQ clamp. Generic MIXED still uses
its genuine host bundle.

A source-admitted zero movement interval still enters selected movement and the
post-movement sequence. Selected Q3 completion uses actual result command time,
matching runtime3534-3535 followed by writeQ3MovementState. Foreign selected
movement uses accepted raw serverTime. BG extrapolation receives that final PS
command time. My earlier raw-time-wins-selected-Q3 interpretation was wrong and
was explicitly corrected to root and the core owner.

## Current ordinary pipeline

The new TU runs special intermission/scoreboard/follow policy first. Ordinary
clients then run inactivity, reward expiry, real movement type/gravity/speed,
source think_prepare, selected movement, source movement completion, eventTime,
BG conversion, pending predictable events, fireHeld cleanup, result bounds and
water, scoped snapped currentOrigin, ClientEvents, real link and trigger touches,
scope exit, BotTestAAS, Impacts, eventTime, buttons and dead respawn/timer actions.

Spectator movement runs source origin publication, trigger touches, unlink and
spectator buttons. Follow and scoreboard retain their distinct existing native
client helpers. Full actor generation is rechecked after effect callbacks.
No complete death/follow/END acceptance is claimed by this packet.

The Missionpack single-player intermission mask changes only the effective
movement command, leaves true pers.cmd intact, appends genuine centerview at
2000..2500ms, and sets source PM_SPINTERMISSION. Movement preserves that real
source policy instead of replacing it through generic player_mode.

## Effects, contacts and currentOrigin

No local gameplay RNG or event counter is introduced. True source gauntlet
preparation, fire, holdable and falling effects remain core-owned. Selected
control suppresses its premature generic world links, trigger gameplay,
immediate movement touches and Q2 contact callbacks under source_usercmd. The
actual movement result still owns the recorded contacts and body/state.

The new owner copies contact actor identities before event callbacks can retire
the real result. Up to 32 use stack storage; larger genuine foreign results use
checked allocation. WORLD contacts resolve the actual source world binding.
Impacts preserves order, deduplicates full actor identities, calls BOT self-touch
before the contacted actor, and checks liveness after callbacks.

Trigger candidates use the actual capped world-query order, source trigger
contents, spectator teleporter/door filter, actual item trajectory/range and
collision geometry. Candidate query and box bounds stay fixed. Each item test
rereads live PS origin after prior teleport effects; each spectator filter
rereads live session team. Jumppad cleanup occurs at this genuine trigger tail;
the extra native no-command actor-frame cleanup has been removed by the core.

CurrentOrigin is the core's real source r overlay, distinct from canonical PS
body. The owner starts it from snapped BG pos.base, runs ClientEvents/link/trigger
effects, and closes it on success or failure while preserving a prior error.
Full actor release clears the core overlay. Real body setters update an active
overlay. Wire NULL-link and current body view use the actual overlay; PS remains
precise. BotTestAAS receives actual post-scope currentOrigin before Impacts.

## Raw projection, gauntlet and holdable

Selected Q3 keeps full int32 source angles and signed int8 axes. Foreign angles
use binary64 raw-plus-source-delta arithmetic before the actual final float
store. Q2 classic words subtract the selected actual deltas modulo 16 bits.
Q2 rerelease angles subtract actual selected degree deltas. Axis scale is 320/127
for NQ/QW and 200/127 for Q2, including -128 input. Jump/crouch use the donor's
positive/negative upmove rules, not native PM threshold guesses. QW attack bits
remain attack-only; no synthesized QW jump bit.

movement_resume added the actual
`application_control_frames_q3_move(..., bool use_holdable, ...)` contract.
The argument comes from the effective source movement command after its genuine
Missionpack mask. Foreign selected button words are not retagged. Same-world
foreign selected Q3 arsenal consumes the source's genuine known gauntlet result,
avoiding a second damage/RNG probe. Original-primary selection gates remain a
required source core/factory dependency.

## Concrete open dependencies and next work

1. **BotTestAAS parent is absent.** `application_bots_test_aas` is called by new2,
   but bots_private.h and bots.c have no declaration/body. bots_resume explicitly
   stopped for the requested handoff. bot_team_policy_owner wrote draft
   source_match.c/.h algorithms: actual setup availability, cached source cvars,
   global navigation, fuzzy point/+10 trace fallback, cluster reads and exact
   prints. Parent globals/cache codec/setup/public API/real print/navigation and
   application source admission/lifetime still need integration and review.
   Never substitute an optional no-op or infer availability from app.bots NULL.
2. **Factory original-primary gate is absent.** Core9 declares and consumes
   `hooks.primary_attack_allowed` for native gauntlet PRE and FIRE. Current
   providers.c has no binding. Root must bind the actual selected arsenal and
   original primary policy. Missing source owner currently errors honestly.
3. **Core9 needs its separate whole peer.** q3_source_resume froze 25 paths at
   `/tmp/qa-q3-source-core-v9-20260930.sha256`, manifest hash
   `a7b29487efb9459ad6322acca997c4b32800acffa29e8f88d27cae9ffb622f64`.
   It supplies real prepare, ClientEvents signed live loop, reward expiry,
   movement COMMAND|VIEW writes, actual water, currentOrigin, death-related
   holders, native event/frame/jumppad state and release guards. Read the current
   core manifest/peer verdict before claiming producer closure.
4. **Movement/raw queue is separately owned and reviewed.** Confirm its latest
   manifest and source peer verdict, effective bindings, source-usercmd phases,
   copied PS state writer, genuine result freshness and independent roles. No
   shadow selected state should replace source PS or transport receipt history.
5. **Client callers need their own final review.** Native client owner repaired
   original Spawn command lineage and selected equipment speed. Actual native Q3
   equipment must expose its own Scout/Haste; guest Q3 equipment explicitly fails
   because no typed speed capability exists. The original GAME fallback is used
   only for genuine non-Q3 equipment, as the donor does. Follow, source clocks,
   disconnect, death/respawn callback liveness and source session ownership remain
   dependencies of the full caller packet.
6. **ClientEndFrame source water is open.** Frozen END2 originally read selected
   control water. I reported that it must read actual pre-teleport source gentity
   water. Core now supplies qa_q3_client_movement_water_read; source owner/root
   and END owner were notified. Do not infer water from selected controls or
   post-teleport contents. END repair and peer acceptance are separate.
7. **Root TU/service integration is open.** Register the new TU and bind the
   real producer dependencies only after source review. No full compile or
   runtime readiness is claimed. Root should assign an independent whole-new2
   peer against the exact manifest and full donors/actual callers, then repair
   concrete findings under exclusive scope and refreeze.

The next session should begin with those current exact packets and their peer
statuses, not reapply older control/session changes. movement_resume is the
current writer; my earlier control/session implementation lane was replaced.

## Historical tools/camera/LLM and other reviews

These are historical messages from earlier assignments, not current readiness.
Root reported tools31 committed as `ccba335`; its original manifest/evidence are
`docs/implementation/tools-continuation-20260930.sha256` and `.md`. Producer
accepted pending-owner guards, genuinely empty HTTP construction, complete
unsent SSE/parser lineage and impossible successful pre-HTTP terminal rejection,
internal observer readiness, source-reachable canceled orphan states, completed
profiler aggregate order and immutable effort-table pointer/count qualification.
Camera codec restrictions on traveled/duration were removed because actual
source arithmetic admits negative, overflow, infinity and NaN values. Those
repairs and root's checkpoint report are personally known from the messages;
I am not issuing a new tools/HTTP/LLM/camera/capture acceptance here.

The tools contract still requires a nonpumping zero-transfer HTTP cut with exact
next ID, honest rejection of live auth sockets/unresolved submitted or retry jobs,
owned private settings strings/dictionary, stable installed console callback
addresses through candidate exchange, real object aliases and allocator/observer
contexts, and full decode/binding/immutable resource equality before publication.

Root later reported accepted settings2 checkpoint and assigned CheckCvars2;
font9 and EVENTS owners/callers were separately reviewed in earlier lanes. Their
manifests rotated and caller integration continued. Treat their current exact
acceptance and aggregate closure as root-owned records. Do not infer current
frontend aggregate, save, capture or full-baseline readiness from these historical
review messages.

