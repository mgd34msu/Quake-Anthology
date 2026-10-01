# QuakeC actor dispatch source packet, 2026-09-30

This packet corrects the QC actor turn around the shared scheduler and physics
services. It is source work for B22. Compilation, tests, executable runs,
sanitizers and benchmarks remain behind the full-project BASELINE gate.

The write scope is `src/app/application/guest_qc.c`,
`src/app/application/guest_qc_internal.h`,
`src/app/application/guest_qc_checkpoint.c`,
`src/app/application/guest_qc_declared.c` and
`src/compat/qc_host/physics.c`. The source-clock follow-up additionally owns
`include/qa/physics.h`, `src/movement/entity/pushers.c`,
`src/persistence/source_io.c` and narrow version changes in
`src/gameplay/modes/checkpoint.c` and `src/gameplay/modes/save.c`.
Root owns application service integration, build definitions and Git.
The control follow-up also owns `src/app/application/guest_qc_input.c`.

## Behavioral evidence

The source references below are paths in
[Quake Anthology TypeScript](https://github.com/mgd34msu/Quake-Anthology-TS).

- `src/app/bootstrap/simulation/actor-execution.ts:124-148`
  captures the QC outer movetype before callbacks. PUSH enters the local pusher
  kernel. STEP moves, runs due think, then checks source water. Ordinary actors
  run think first. Captured NONE skips movement and captured NOCLIP uses the
  explicit Q1 noclip procedure. The other moving arms call shared physics.
- `src/app/bootstrap/simulation/physics.ts:616-628` reads
  live motion when shared physics starts. Capturing the entire toss or fly
  procedure across think would change the oracle's behavior.
- `src/compat/qc/actor-state.ts:34-50` maps raw QC NONE and
  NOCLIP to stationary for shared physics. The C adapter keeps NOCLIP as a
  separate native enum for other consumers, so the ordinary moving arm skips
  shared movement when think has changed its live movetype to NOCLIP.
- `src/app/bootstrap/simulation/quakec-source.ts:1433-1439`
  runs one classic think per turn, including a classic QuakeWorld source. The
  qualified mod actor scheduler can repeat due QuakeWorld callbacks.
- `src/compat/qc/mod-clients.ts:87-101` and
  `src/compat/qc/mod-provider.ts:822-835` run qualified client
  thinks before their declared client frame calls. The native qualified
  `begin_frame` path and its early return from `actor_frame` remain separate.
- `src/app/bootstrap/simulation/quakec-source.ts:1107-1112`
  checks the raw QuakeWorld `lastruntime` float before a nonclient turn. The
  native unqualified QuakeWorld path preserves that check and store.
- `quakec-source.ts:1346-1357` passes original source time to classic contact
  callbacks, including contacts inside a due think. Qualified contact and
  blocked callbacks use `this.frame.time` in `mod-provider.ts:449-453,500-505`.
  Qualified owned actor turns retain the outer step frame, while
  `runClientThink:822-835` temporarily supplies callback time for client calls.
- `src/movement/q1/water-transition.ts:2-8`,
  `quakec-source.ts:1448-1459` and `mod-provider.ts:851-866` define the source
  water transition and splash policy. Source initialization sets waterlevel to
  one. Leaving water preserves raw empty or solid contents in waterlevel,
  including negative values.
- `src/movement/q1/pusher.ts:43-51,108-113,129-144`
  advances and rolls back local time with donor binary64 addition/subtraction
  and a final binary32 store. Think timing compares the captured deadline with
  the actual local time read after the push. `compat/qc/pusher-host.ts` reads
  raw binary32 `ltime` and `nextthink`; the native projection in
  `app/bootstrap/simulation/native-q1-pusher.ts` reads source fields directly.
- Native `foundation/entity-services.ts:363-371` deliberately rounds
  `schedule` and `scheduleAt` deadlines to binary32. Native `nextThink` remains
  a source-owned double property; the shared nanosecond scheduler is a
  projection rather than the authority for local pusher timing.
- `quakec-source.ts:996-1036` separates one mixed source pre/post pair from
  selected movement phases. Mixed source frametime uses its actual source
  input frame: `runtime.ts:4558-4560` supplies the current host bundle interval,
  clamped to 1–100 milliseconds for selected NetQuake. Accepted source jump impulses clear the actual shared ground
  and consume the selected command's jump action. Postthink projects the
  computed ground and translates foreign water contents into source Q1 values.
- `quakec-source.ts:1048-1098` reads QW centered origin without an intermediate
  binary32 store, inverse-centers it at the final entity store, and keeps
  `v_angle` separate from authored entity `angles`. QW prethink preserves
  fixangle, computes roll through the donor angle vectors and returns for
  spectators before setting frametime or running prethink/think.
- `quakec-client-adapter.ts:7-24,28-46` translates classic input from the
  original command angle words and jump actions. Its raw Q2/Q3 angle-word
  conversion is separate from qualified input's delta-aware aim projection.
  `runtime.ts:3340-3342` applies source spectator free movement; it does not
  derive foreign or QW movement mode from raw source movetype.
- `quakec-source.ts:1069-1076` reads QW movement cvars and optional source
  maxspeed/gravity fields. `quakeworld-cvars.ts:4-5` supplies their actual engine
  defaults, and `core/cvars/index.ts:320-322` stores numeric values as binary32.

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

Private engine checkpoints now write and require version 7. Version 3 retains
the old BEFORE-think contract; version 4 retains the earlier integer deadline
projection. Version 5 lacks actual passive output lease membership, and version
6 lacks the actual QW prepared stage and reserved OWNED client contract. These
versions are rejected instead of silently changing continuation semantics.
Root separately validates restored scheduler boundary metadata.

The shared Q1 pusher now uses `qa_q1_pusher_clock` source seconds. QC fields
promote their actual binary32 values without clamping positive, zero or negative
deadlines. Move-time subtraction and velocity scaling use binary64 arithmetic;
displacement and local advance store their final binary32 results. A blocked
push rereads the live pusher and subtracts the interval from that current local
clock before the blocked callback and forward actor rollback. The think gate
uses the captured original deadline and the actual post-push local clock, then
clears the current deadline before invoking the real provider callback.
Float stores admit the finite nearest-binary32 rounding range before narrowing;
values below the overflow midpoint still round to finite `FLT_MAX`. Nonfinite
results and values at or above that midpoint fail before an out-of-range cast.

`qa_physics_push.q1_elapsed_seconds` carries the actual Q1 interval. Explicit
displacement pushes and Q2 pushes retain a zero interval. The portable physics
codec now preserves both source-second values as f64. Mode saves write and
require private version 8 and typed checkpoint version 10, rejecting old integer
clock bit interpretations. Native Q1 and Q2 owners separately migrate their
actual producers, projection and private codecs.

## Integration and remaining work

Root exposes the water helper in shared application declarations and routes
the QC branch of `physics_q1_water_transition` to it. Colliding toss actors
therefore use the same source water policy as post-STEP actors. Root's generic
single-dispatch scheduler API and captured-procedure physics API are separate
review packets.

The source-clock migration requires coordinated native Q1 producer and private
codec acceptance, plus the Q2 codec/type migration; the shared edits alone do
not establish complete pusher behavior. Classic player movement callback
integration remains coordinated with the actual control/session producer;
qualified client scheduling retains its existing frame contract. Complete QW
missile traversal, media admission, save integration and executable behavior
require their own acceptance evidence.

The control follow-up is still in source integration. QC `prepare_frame`
initializes actual frame time before NetQuake client preparation;
`begin_frame` retains the real StartFrame or qualified frame callbacks. The
classic control helpers preserve the distinct NQ actor turn, QW command group
and mixed source boundaries. The control owner supplies those actual admitted
boundaries and suppresses the legacy player arm only for a handled source turn.
The control owner also qualifies reserved QC clients independently of input
availability. The pure query reads the actual physical reserved slot, complete
actor ID and retained source client roster. It accepts owned source slots and
foreign borrowed client bindings, without using canonical ownership or the
presence of a queued command to decide whether the source owns a client slot.
This lets the caller retain beforeActor force-retouch while suppressing legacy
client physics and callbacks when a QW or mixed client has no command turn.
A separate pure profile query reports actual qualified input subscriptions,
including movement-slice stages. The native Q1 map caller must read activity
across actually attached providers, rather than only the current actor's
command stage. The donor also registers passive output claims as active input
listeners. The QC query now includes actual retained output channel claims.
The qualified output owner validates original field declarations, publishes
detached values after admission and source calls, retains claims across client
release, and closes claims on source engine teardown. The movement consumer
rotation and its original source capability gates remain separate acceptance
dependencies. See `qc-client-outputs-20260930.md` for the bounded owner contract.
Genuine command-only scopes retain their actual completed-clock admission
metadata. Classic callbacks still use the source game's real current time,
which can precede the completed world clock after frame exit. Their due-think
check reads the raw source deadline and exact incoming source interval,
clears it and cancels the scheduler projection before the original indexed
callback. The callback runs in the real session think invocation; no world
frame or frame counter is manufactured. Stores during the active command
keep subsequent source deadlines and their scheduler projection coherent.

The scheduler callback now consumes a tagged `qa_think_scope` and requires its
WORLD variant. It keeps the literal admitted frame separate from the scheduler's
due-time projection. Before clearing `nextthink`, it compares the raw guest
binary32 deadline against the source-second frame end and computes callback
time from that deadline. Positive deadlines project upward to nanoseconds, so
a fractional-nanosecond deadline cannot become an earlier integer deadline.
If the raw comparison still defers a projected candidate, it reschedules past
that frame's integer end. Deadlines beyond the integer clock remain in the
guest field and are reconsidered on actor turns; no UINT64_MAX callback stands
in for them. Mixed and QW input use the actual command scope and the separate
source input interval, without changing a copy of the world frame.
The pusher callback remains a separate consumer of the real local-clock
kernel. That kernel has already cleared the deadline before entry; the
callback cancels its shared projection and invokes the guest at retained
source-current time, without rerunning the ordinary world deadline gate.

Qualified input scopes can now be parked without closing them or replaying
guest callbacks. A parked handle owns the original command/slice nodes, saved
raw input words and parent pointer. Resume reconnects those same nodes; abort
restores and frees their actual saved fields. Generation checks prevent
restoration into a recycled actor. The engine registry rejects save, restore
and teardown while a handle is parked, and parent scopes cannot close or park
while a parked descendant retains them. The turn owner disposes handles on
retirement and every failed continuation.
Resume validates the retained reference against the same complete actor ID.
A nested same-actor input scope also restores its saved fields when its outer
scope is parked. Capture, restore and teardown reject actual source admissions,
including command-only entry when no world frame is active.

Whole control acceptance still requires the exact mixed outer boundary,
genuine command-only context, source interval producer and body projection
integration. The QW precision owner supplies the real double centered-origin
representation; the QC helpers retain that value until inverse source stores.
No execution or complete control fidelity is claimed for the current draft.

The native `infokey` consumer now reads the canonical player's actual retained
raw userinfo string. A temporary parse reads arbitrary source keys without
creating a second retained dictionary. World-slot queries still read engine
cvars, and unconnected or dynamic physical slots retain the original empty
result. Connected clients require the actual userinfo owner and complete guest
slot binding. The source-named engine buffers preserve stable negative QW
string IDs and their existing VM continuation codec. Native names use physical
slot numbers with 1024-byte buffers; declared mod names use original references
and the source's minimum 1024-byte capacity.

The source evidence is `quakec-source.ts:343-347`,
`compat/qc/mod-provider.ts:332-338` and `compat/qc/memory.ts:125-139` in the
TypeScript repository. The actual canonical accessor lives in
`src/app/application/network_qw.c`, declared by
`include/qa/application_network_qw.h`; root must include that application TU.
The map-player owner supplies initial local QW userinfo before Connect/Begin,
and the network owner updates the same raw record for real received setinfo.
A missing connected-client string remains an explicit missing producer,
without a getter-side name/skin/team reconstruction.

## Review freeze

The initial bounded six-file SHA-256 manifest is
`/tmp/qc-actor-dispatch-20260930.sha256`, accepted by independent Q1 source review
and committed by root at `3e6c032`. The two-file callback-time follow-up is
`/tmp/qc-actor-time-20260930.sha256`, accepted by independent Q1 source review
and committed by root at `7aec7ba`. The two-file frametime follow-up is
`/tmp/qc-actor-frametime-20260930.sha256`, accepted by independent Q1 source
review and committed by root at `b6ab79d`. The shared source-clock follow-up is
`/tmp/qc-pusher-clock-20260930.sha256`. It changes the six shared/codec paths
listed above and this report.
The shared clock packet and coordinated native Q1/Q2 producer and codec packets
were independently source reviewed and committed by root at `30a767a`.
Independent source review must read the frozen files against the cited oracle
paths and root API declarations. Whitespace and digest checks are allowed before
BASELINE; runtime checks are not acceptance evidence for this packet because
none have been run.
