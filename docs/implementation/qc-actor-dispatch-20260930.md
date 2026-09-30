# QuakeC actor dispatch source packet, 2026-09-30

This packet corrects the QC actor turn around the shared scheduler and physics
services. It is source work for B22. Compilation, tests, executable runs,
sanitizers and benchmarks remain behind the full-project BASELINE gate.

The write scope is `src/app/application/guest_qc.c`,
`src/app/application/guest_qc_internal.h`,
`src/app/application/guest_qc_checkpoint.c`,
`src/app/application/guest_qc_declared.c` and
`src/compat/qc_host/physics.c`. Root owns shared declarations, scheduler APIs,
physics APIs, application service integration, build definitions and Git.

## Behavioral evidence

- `../quake-typescript/src/app/bootstrap/simulation/actor-execution.ts:124-148`
  captures the QC outer movetype before callbacks. PUSH enters the local pusher
  kernel. STEP moves, runs due think, then checks source water. Ordinary actors
  run think first. Captured NONE skips movement and captured NOCLIP uses the
  explicit Q1 noclip procedure. The other moving arms call shared physics.
- `../quake-typescript/src/app/bootstrap/simulation/physics.ts:616-628` reads
  live motion when shared physics starts. Capturing the entire toss or fly
  procedure across think would change the oracle's behavior.
- `../quake-typescript/src/compat/qc/actor-state.ts:34-50` maps raw QC NONE and
  NOCLIP to stationary for shared physics. The C adapter keeps NOCLIP as a
  separate native enum for other consumers, so the ordinary moving arm skips
  shared movement when think has changed its live movetype to NOCLIP.
- `../quake-typescript/src/app/bootstrap/simulation/quakec-source.ts:1433-1439`
  runs one classic think per turn, including a classic QuakeWorld source. The
  qualified mod actor scheduler can repeat due QuakeWorld callbacks.
- `../quake-typescript/src/compat/qc/mod-clients.ts:87-101` and
  `../quake-typescript/src/compat/qc/mod-provider.ts:822-835` run qualified client
  thinks before their declared client frame calls. The native qualified
  `begin_frame` path and its early return from `actor_frame` remain separate.
- `../quake-typescript/src/app/bootstrap/simulation/quakec-source.ts:1107-1112`
  checks the raw QuakeWorld `lastruntime` float before a nonclient turn. The
  native unqualified QuakeWorld path preserves that check and store.
- `quakec-source.ts:1346-1357` passes original source time to classic contact
  callbacks, including contacts inside a due think. Qualified contact and
  blocked callbacks use `this.frame.time` in `mod-provider.ts:449-453,500-505`.
  Qualified owned actor turns retain the outer step frame, while
  `runClientThink:822-835` temporarily supplies callback time for client calls.
- `../quake-typescript/src/movement/q1/water-transition.ts:2-8`,
  `quakec-source.ts:1448-1459` and `mod-provider.ts:851-866` define the source
  water transition and splash policy. Source initialization sets waterlevel to
  one. Leaving water preserves raw empty or solid contents in waterlevel,
  including negative values.

## Native changes

QC pending thinks now use `QA_THINK_DURING_PHYSICS`. Actor turns choose the
captured outer movetype and run the scheduler at the corresponding source
point. STEP schedules from the live deadline after its movement callbacks.
Changing STEP to PUSH during movement does not suppress that turn's ordinary
think. PUSH keeps the real local-clock pusher path.

Unqualified turns use root's `qa_scheduler_run_once`; qualified turns retain
`qa_scheduler_run`. Both use the real execution provider and original source
frame. The callback clears `nextthink` before entry. Qualified actor callbacks
stage and restore `self`, `other` and callback `time` through indexed calls;
classic callbacks preserve their source clock setup. Resume points check the
full actor generation after trigger, movement, think, sound and diagnostic
callbacks.

Touch and blocked callbacks enter with original source time for classic sources
and qualified owned actor turns. Qualified client thinks retain their actual
binary32 callback time in a transient scope; nested contacts use that value.
The scope restores after indexed-call success or failure. Contact entry leaves
the current `frametime` unchanged and does not treat mutable `g.time` as its
clock authority. Capture, restore and teardown reject an active client scope.

Classic think leaves its callback time in `g.time`, as the oracle's `invoke`
restores only `self` and `other`. NONE, NOCLIP, STEP water and movement without
contacts no longer reset it merely because physics resumes. Classic named
callbacks explicitly enter with source-current time, so `PlayerPreThink` and
`PlayerPostThink` retain their actual clock entry while the separate control
order integration remains open. Due think also preserves the current guest
`frametime`: the oracle initializes it at frame entry and later `invoke` calls
stage only `time`, `self` and `other`. A lawful earlier guest store is retained.

Captured NOCLIP uses `qa_physics_step_source_motion` without overwriting the
guest's current motion. Other moving arms read live shared physics. Unsupported
nonclient outer movetypes fail at the same dispatch decision as the oracle.
Root's selected NOCLIP procedure bypasses generic attachment handling and keeps
the oracle's double multiplication/addition before the final float store.

`application_qc_water_transition(application_provider *, qa_actor_id,
qa_error *)` reads the authoritative body, queries raw Q1 world contents and
stores the original QC water fields. It emits the source splash sound when
available. Unqualified missing precaches produce the original diagnostic;
qualified missing media stays silent. The QC physics boundary accepts source
waterlevel values across its actual int32 storage range, including the negative
values produced by this transition.

Private engine checkpoints now write and require version 4. Version 3 retains
the old BEFORE-think contract and is rejected instead of silently replaying it
under the new source ordering. Root separately validates restored scheduler
boundary metadata.

## Integration and remaining work

Root must expose the water helper in shared application declarations and route
the QC branch of `physics_q1_water_transition` to it. That lets colliding toss
actors use the same source water policy as post-STEP actors. Root's generic
single-dispatch scheduler API and captured-procedure physics API are separate
review packets.

QC local pusher time still passes through the existing int64 nanosecond adapter
in `qc_host/physics.c` and `movement/entity/pushers.c`. This packet does not
establish IEEE source-local-clock parity. Classic player movement callback
integration remains root-owned; qualified client scheduling was traced and kept
in its existing frame contract. Complete QW missile traversal, media admission,
save integration and executable behavior require their own acceptance evidence.

## Review freeze

The initial bounded six-file SHA-256 manifest is
`/tmp/qc-actor-dispatch-20260930.sha256`, accepted by independent Q1 source review
and committed by root at `3e6c032`. The two-file callback-time follow-up is
`/tmp/qc-actor-time-20260930.sha256`, accepted by independent Q1 source review
and committed by root at `7aec7ba`. The two-file frametime follow-up is
`/tmp/qc-actor-frametime-20260930.sha256`. It changes `guest_qc.c` and this report.
Independent source review must read the frozen files against the cited oracle
paths and root API declarations. Whitespace and digest checks are allowed before
BASELINE; runtime checks are not acceptance evidence for this packet because
none have been run.
