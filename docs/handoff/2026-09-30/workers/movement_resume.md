# Movement and source command handoff, September 30, 2026

The current outer caller packet is UNACCEPTED. Root requested a complete new-session handoff and froze code. This agent made no source edits after that request, and no source edits after the final context compaction. The latest four confirmed review findings below are still present in the frozen source. Do not read the earlier nine-file acceptance as acceptance of this follow-on.


## Execution constraints and ownership

- Strict source-only gate. No configure, build, compiler, tests, game, parser, generator, script, syntax, sanitizer, benchmark or executable validation. Read source, edit owned source, hash and whitespace checks only while implementation was active.
- At the handoff root explicitly said no new implementation, no executables/CMake. This snapshot used source reads, hashes and `/tmp` document edits only.

- No descendants. File ownership is exclusive; coordinate existing independent owners before any future edits.
- My nine-file roster is the exact manifest below. `include/qa/scheduler.h` and `src/session/scheduler.c` are unchanged from the earlier accepted nine. The other seven differ from that accepted manifest.
- Do not edit `guest_input_private.h` or `guest_input_profile.c`: root assigned `/root/q1_resume`.
- Do not edit `native_q3_control.c/.h`: root assigned `/root/tools_source_review`.
- Do not edit Q1/Q2/Q3 kernels, QC files, QVM primitive files, provider/roster, persistence, network or frontend files from this lane.

## Frozen current owner inventory

Current snapshot manifest: `/tmp/qa-handoff-movement_resume-20260930.sha256`. Earlier accepted checkpoint manifest: `/tmp/qa-movement-source-domains-20260930.sha256`. The latter is stale for seven current files and must not be reused to accept current source.

| File | Current SHA-256 |
| --- | --- |
| include/qa/scheduler.h | b3186b0ce4627d19985ed812216b7c0cdd75fa3f6a854f11f0a30dc76b1bb8a4 |
| include/qa/session.h | c36276204fefdad4bc5b2bdb49fe299959d51bd6be226fc9d95eff7893c52ef3 |
| src/session/scheduler.c | c01c3efd1a6822faacc46a65c6205a3f59ad606450a87514cf39033b57996e47 |
| src/session/session.c | 478c996c0a8cb0a38c3d3a62d24466ac39d508786caeb3741bc6a185a6dd8f1c |
| src/app/application/control.c | 9981674848964d71fe1f87d210ef2bf5f71d9e518fb018f1080f56f872e219a7 |
| src/app/application/control_frame.c | 270600690afccfd816b685631264094cea551d7116ac39966a50c34e6eabe61b |
| src/app/application/control_frame.h | ac98e6a17300d2618294fc0f556617429a5aefceb2e2eedc591c22c27900a95c |
| src/app/application/arsenal.c | feaef4d2482ce250c488e54294560a08cc75644a109dbcd6b189d08ae84eaecd |
| src/app/application/arsenal_guest.c | 21bd9db8aabf791842afb9e54ed20420ac5ee76644a9ea1f60da00f35c604ecb |

Only this nine-file hash inventory was freshly read for this handoff. External manifests listed below are owner records, often earlier freezes whose shared files have since rotated. Reconcile actual current hashes and current owner acceptance before composing packets.

## Four confirmed unrepaired source-peer findings

Reviewer: `/root/movement_source_peer`. No follow-on acceptance was issued.

1. **Classic QC NQ physical source turn is missing with a foreign canonical Character.** `arsenal.c:106` source_actor routes native Q1 but no QC equivalent. `control_frame.c:1196` qualifies `native_nq` only for Q1. Actual QC NQ map + matching selected NQ + foreign Character creates a retained turn in prepare; session canonical actor routing may skip an undued foreign execution owner, so the turn reaches end_commands and aborts without physics/PreThink/Think/PostThink. Donor `quake-typescript/src/app/bootstrap/simulation/runtime.ts:4608` uses actual map `preparesQ1Clients` plus selected NQ independently of canonical Character. Route actual QC source physical NQ client through source_actor using its real `.command_actor` qualification, then suppress canonical duplicate actual source turns.
   - Important implementation trap: `application_control_frames_actor` currently invokes QC `before_actor` even when the parked turn has already been consumed. Native Q1 has no corresponding QC force_retouch hook; widening routing naively can execute QC force_retouch twice when canonical execution is also QC. Add an actual transient full-actor/provider/frame completion marker, or equivalent genuine once-per-frame source admission. Existing `frame_owned/owned_frame_number` is set in PREPARE too and is not a completion marker. Keep checkpoint-busy ephemeral markers unserialized and clear them at actual end/abort/release.
2. **Nonraw selected NQ source callbacks still select canonical execution.** `control.c:1486` begins `execution=control_execution`; only three raw source tags override with true primary. Actual QC NQ source + selected NQ + foreign Character therefore skips `application_qc_control_phase`. Actual native Q1 map + foreign Character skips native source THINK even though source PreThink/afterPhysics helpers exist. Donor `runtime.ts:3308-3333` installs NQ callbacks from actual source independently of Character. Qualify actual context frame/command provider by real `.command_actor`, use it for the physical source pipeline, and retain canonical fallback only when no actual source client exists. Do not replace canonical ownership in the actor registry.
3. **Original QVM input callback interval incorrectly wraps.** `arsenal_guest.c:435` computes uint32 serverTime subtraction, then values above INT32_MAX become zero. Donor `compat/qvm/game-input.ts:143-144` uses mathematical binary64 difference before outer clamp0..1000 or slice clamp1..200. Concrete serverTime INT32_MAX, commandTime INT32_MIN gives donor outer1000/slice200 but C outer0/slice1. Use signed int64 mathematical difference for this actual input interval, keeping real guest PM timer arithmetic separate. No fix landed before freeze.
4. **Original QVM source USE_HOLDABLE bit is lost during foreign selected movement.** `arsenal_guest.c:272` projects only source attack into non-Q3 command, then writes `(source.buttons & ~5u) | (applied.buttons & 5u)`. Raw source button4 with foreign selected Q2 and no hook change becomes zero, so retained original source arsenal loses holdable. Donor `game-input.ts:161-175` updates full original usercmd only for genuine effective source input/output, and `game-equipment-movement.ts:87-96` clears bit4 conditionally only while real equipment owns holdable. Preserve source controls that the foreign selected dialect does not represent. Do not add raw holdable to foreign selected buttons to compensate. No fix landed.

## Further open contracts and remaining caller prerequisites

- **QVM absolute aim is suspected incomplete, not yet a peer-confirmed finding.** `arsenal_guest.c:350` builds a temporary QA_MOVEMENT_Q3 default with only real delta words and previous view; generic QC semantic aim decodes unsigned angles. Donor `game-input.ts:180-189` calls `movement/q3/view.ts:4-12`: source-specific intermission values, health/dead preservation, signed-short pitch and clamp16000. The declared original profile can have different intermission constants from native5/6. Trace and provide the actual source-relative absolute aim without pretending a default typed movement state is the live original PS owner. Preserve full32 raw words when unchanged.
- **Classic original native Q2 output movement consumer is absent.** `application_native_q2_move` invokes genuine original ClientThink and imported classic Pmove reaches `guest_native_q2_services.c` movement_prepare. That callback currently carries foreign-character bounds/flight/gravity but ignores actual QC output mode/stance/body. `/root/native_resume` confirmed and planned a narrow services owner assignment; no edit from this lane. Accepted RR original Pmove/dimensions adapter is its own authority and must remain separate.
- **Original native Q2 camera crouch getter is missing.** `control.c:2173` camera currently reads selected typed flags or actual original Q3 helper; it does not read actual original Q2 PS duck state. `/root/q2_resume` identified exact full-source qualified helper needed: `application_q2_control_crouched(engine, actor, bool *, error)`. Underlying original public PS flags are classic byte offset16 or RR u16 offset28, duckbit1. It must use real `client_for/client_live` actor/host/instance qualification, not only raw player-state read. Root owns released adapter2; future reassignment needed before edit. Blocked stand-up camera must use genuine source crouch.
- **Classic QC NQ local intermission/local cinematic view is absent.** `application_control_outputs` covers combat health, actual control cutscene, typed Q2 intermission/chase, native Q3 source match intermission and real original QVM/Q2 PS gates. `/root/qc_resume` source read found no actual private NQ localClientIntermission/localClientView producer or retained state in current guest_qc files. Donor `runtime.ts:454-470` suppresses publications for those actual source conditions. Do not invent a boolean from generic mode to claim closure. Add actual local QC message/view owner/continuation before exact gate. Native Q1 intermission qualification also lacks an actual available getter in this lane.
- **QVM selected equipment pose/speed owner is absent.** Accepted actual QVM body adapter accepts a qualified optional equipment environment. Current caller passes NULL honestly; only real bounds outputs are integrated. Donor `game-equipment-movement.ts` actual pose and movement environment hooks remain separate producer work. Do not synthesize typed equipment state as the original source owner.
- **QW reliable source actions lack a genuine after-EndFrame action queue.** Network owner identified pause/kill/setinfo/chat etc must run with true per-client group after QW EndFrame; UDP pump execution is wrong. Need actual retained text plus reconstructible callback identity/private codec and real producer/caller owner. Signon remains true outside-frame phase. No implementation from this lane.
- **QW wire commandAge needs a separate actual QC receipt clock producer.** Network owner reported at freeze that raw `qa_application_control_qw_commands` sets `input.source_time_ns=clock.frame.time_ns` and its last-command observer returns that. Between frames it is genuine completed session EXIT. Donor wire commandTime instead uses the distinct retained private QC `game.timeSeconds` ENTRY. Real command admission may correctly remain EXIT; do not rewrite it to satisfy wire age. Add a separately source-qualified actual QC received-command timestamp to the true wire intake/owner and use it for commandAge, without guessing from current session time. Network owner will include exact donor evidence in its handoff; this late report was not independently reviewed by this lane.
- **Native Q3 actual source assignment producer ordering requires review.** Our source_movement_state callback is implemented and provider bound, but actual independent nonprimary Q3 spawn can happen before admit_control. `/root/q3_source_resume` was notified that unconditional source producer hooks in independent role spawns may have no real selected control yet; actual primary Connect/Begin occurs after admission. Ensure true source producer placement without inventing a control or swallowing a required producer failure. This is pending owner response/review.
- Full scalar/collision/weapon/campaign/mod/network parity and executable verification remain outside bounded source packets. Native original Q3 body outputs stay unsupported without a genuine ABI trace declaration. Do not weaken gates.

## Current implementation: real scheduler and command admission domains

The earlier accepted scheduler stays unchanged. `qa_think_scope` is tagged WORLD_FRAME or SOURCE_COMMAND, containing the genuine literal `qa_source_frame` or `qa_source_command`, plus independent callback due clamp `time_ns`, actual source interval start and elapsed. One real due/cancel/order authority remains shared. No fake frame fallback, no dual compatibility callbacks. Native Q1 callbacks can run under admitted command; classic QC canonical scheduler callback is WORLD-only and true QC command thinker calls its raw helper.

Keep four domains separate: actual completed world clock/counters; source-private QC/native callback time; actual host bundle elapsed; physical command/slice duration. MIXED ordinary input uses real host bundle elapsed. Native source_usercmd uses real accepted command minus source PS commandTime interval. Original QVM uses actual currently admitted original move/slice interval. Selected NQ then applies its actual source clamp. Worldframes and scheduler debt are never advanced to serve a command.

Session changes in current snapshot:

- Optional pure `qa_component.command_actor(state, session, full_actor)` recognizes actual physical source membership independently of canonical selected Character.
- `qa_session_command_call(session, owner, actor, elapsed_ns, callback, state, error)` allows canonical execution or genuine source-qualified actor. Nested invocation requires source qualification. All actual command scopes remain nonoverlapping. It preserves the actual current invocation parent, literal source kind/provider/actor, source active ENTRY or completed clock time, completed_frame_number, real elapsed, real host interval, and restores stepping/host continuation. No source phase/counter/debt fabrication.
- In actual source_actor dispatch `source->clock.frame.phase=QA_ENTITY_PHYSICS` is published to the real active source clock before copying and invoking. Required by true native G_RunClient helper; earlier invocation-copy-only phase was wrong.
- `qa_session_actor_allocation_ready(session, owner)` is a pure exact !faulted/!transitioning + actual registered nonretiring component predicate. QC replacement uses it during ordinary release while notification_depth makes qa_session_safe false. Never allocate during real world/component retirement transitions.

## Current implementation: true raw Q3 and QW source intake

Public implemented helper signatures:

```c
bool qa_application_control_q3_command(qa_application *, qa_actor_id, uint64_t sequence,
                                     const qa_q3_usercmd *, qa_error *);
bool qa_application_control_qw_commands(qa_application *, qa_actor_id,
                                      const qa_movement_command *, size_t, qa_error *);
```

Root owns their public header declarations; implementations are in control_frame.c. Real primary ENTITIES source, actual executable `.command_actor`, current full actor/control/lifetime, source family and actual clock are mandatory. Chosen selected movement is an independent authority, never raw dialect retagging.

Q3:

- Full32 angle words, int8 -128 axes, buttons, weapon, actual serverTime and separate uint64 transport sequence are retained literally.
- Actual native receipt calls real wire command and real `qa_q3_client_received_command` pers.cmd/lastCmdTime once before queue/defer.
- Native BOT or cached g_synchronousClients uses actual client_deferred query, retains latest input without queue; real physical G_RunClient executes it at actual source PHYSICS ENTRY. Async native clients queue one raw source command.
- Original QVM/native original Q3 executes genuine Think synchronously during intake under qa_session_command_call plus original arsenal input context; never queued for later reliable/packet order. Network peer reread this repair and confirmed the original order counterexample closed, without aggregate acceptance.
- First-enter wire seed before canonical remoteBegin is separately network owned. It seeds original engine command only, without queueing an extra movement or acknowledging an extra transport command.
- Genuine native ClientSpawn/GRun synchronous helper calls actual native pipeline without inventing network sequence, raw receipt or history.

QW:

- Actual raw group1..20 keeps one literal transport sequence and exact raw dialect, independent chosen movement. Real QW EndFrame required; no zero-worldframe or no-frame drain.
- Recursive >50ms split uses actual floored halves and second impulse0, source input command/slice applications wrap true raw source before foreign selected projection. Current source profile has no configurable maxcmd field, so bounded default50 is used.
- QW source host application interval is sum of real physical slice durations only when actual global source applications/output claims active; individual actual physical slices remain separate.
- Real raw context.source_command supplies foreign selected classic QC/native QW source input/think. Transport completion occurs once at real group post, not once per split.
- Actual last raw observer `application_control_last_qw_command(app, actor, &raw, &source_time_ns, &present, error)` is from true queue/input owner; it never derives raw input from velocity. Q3 observer similarly preserves actual raw fields.

## Control context, native Q3 borrowed ownership and source post order

`application_control_context` now distinguishes true native source_usercmd, effective source_holdable intent, original source_guestcmd, raw source_qwcmd, and actual source_input_applied. It holds literal source raw command independently of selected command. `application_control_provider/time/elapsed` access genuine frame or genuine command by actual domain.

`control_move` currently selects true primary map for all three raw tags; peer source read matches donor true map source, independently selected Character/effects. The nonraw NQ gap above remains.

Actual native ClientThink helper moved atomically from control.c to root-assigned new `native_q3_control.c/.h` owned by `/root/tools_source_review`. There are no duplicate old helper definitions. `control_frame.h` includes new header. The whole new source pipeline is unaccepted pending its own owner/peer and this caller packet.

Our source_usercmd generic path publishes body/result and selected role phases but suppresses premature generic link/triggers/contact gameplay. Q2 contacts are not executed early. Actual new native post pipeline owns source BG publish, predictable event time, pending events, ClientEvents, snapped link/currentOrigin scope, triggers, original origin restore, BotTestAAS/impacts, buttons/respawn/timers. The real completed result getter returns existing control result rather than a shadow owner:

```c
const qa_movement_result *application_control_q3_result(application_provider *, qa_actor_id, qa_error *);
bool application_control_frames_q3_move(qa_application *, const qa_source_command *,
    const qa_movement_command *, bool use_holdable, qa_error *);
```

The native new2 caller passes actual post-Missionpack effective `call.movement.buttons & 4` into semantic source_holdable. Foreign selected buttons stay actual donor attack-only. If selected native Q3 arsenal equals actual source, retained actual gauntlet_contact is supplied as known, avoiding a second damage probe.

Native selected Q3 actual input options now read copied source settings at true source command stage: fixed/step, dmflags bit32 noFootsteps, source spectator masks. Spectator disables fixed/noFootsteps and uses trace0x10001. Source-authored PM type is preserved; generic normal-mode bridge is suppressed unless an actual qualified mod mode is published. Scout source speed multiplier is not doubled by the generic environment.

Real private PM writers:

```c
bool application_control_q3_flags(application_provider *, qa_actor_id, uint32_t clear, uint32_t set, qa_error *);
bool application_control_q3_policy(application_provider *, qa_actor_id, uint8_t fields,
                                   const qa_q3_wire_policy *, qa_error *);
bool application_control_q3_source_state(application_provider *, qa_actor_id,
    const qa_q3_player_state *, uint32_t fields, qa_error *);
```

Flags/policy write only actual selected Q3 control state, plus actual currently borrowed kernel state when present. Foreign selected source PM remains actual native source holder, wire masked setter chooses that owner. Source_state handles actual COMMAND/EVENTS/FRAME/JUMPPAD/DELTAS/VIEW assignments from real Connect/Begin/Spawn source producers, durable plus actual borrowed kernel cell. It resolves real positive jumppad source ref; zero becomes null. Foreign selected source player remains sole owner and receives no typed write. Borrowed cell clears on actual cleanup, park, release or abort, so it never becomes a persistent mirror.

## Active output capability, actual consumers and body continuation

Accepted QC producer registry returns detached real claims/read values. Our new caller provides:

```c
bool application_control_output_admit(const qa_application *, qa_actor_id, uint8_t channels, qa_error *);
bool application_control_outputs(const qa_application *, qa_actor_id, application_client_outputs *, qa_error *);
bool application_control_body_request(qa_application *, qa_actor_id, qa_bounds current,
    application_client_outputs *, qa_error *);
void application_control_body_reset(qa_application *, qa_actor_id);
```

Capability requires actual live selected controls and real attached primary source. Reject stance for NQ and actual classic QC QW. Original Q3 requires exact admitted mode/body interface; body unsupported without accepted real QVM adapter. Original RR Q2 body requires artifact-qualified actual body declaration.

Active read suppresses health<=0 and actual control cutscene, reads actual original QVM PM intermission profile or original Q2 PS intermission, typed Q2 intermission/chase, native Q3 source match intermission. Missing genuine local QC/NQ gates remain explicit above. Never substitute generic primary mode to fake them.

Typed prepare and phase refresh use optional real has_mode/stance/body outputs only. Current body bounds remain separate from requested output bounds; blanket environment.has_body_bounds=current was removed. Fixed pose is a different authored request/precedence. Donor `players.ts:256-287`, `runtime.ts:2746` and RR native-input source support this distinction; peer confirmed.

NQ or actual classic QC QW selected movement captures original current bounds once on first actual custom hull publication. After outputs clear, it requests saved base until real accepted bounds equal base, then clears continuation. No empty unretained/unseen/body-less row is created; a cleared body-only row is removed without deleting real seen/sequence/intent state. Actual spawn/travel calls body_reset only. Round owner central admit_control and actual native ClientSpawn bound reset; do not use cutscene toggles or generic motion reset.

Camera applies real published view offset only when stance is absent/true or actual source no longer crouched. Native typed flags and actual original QVM helper are integrated. Original Q2 real source crouch remains open as above. Published output affects final camera, not a false persistent movement view offset owner.

## Original QVM input/body ownership and cancellation

Actual `application_guest_input` owns opaque `application_guest_q3_control *` within existing original input_attach/detach lifetime. No extra role holder. Root-assigned q1 owns body adapter and profile source retention. True outer profile.move hook after actual source_input(before), actual physical client/source record and selected movement==original provider begins body scope. True slices reuse outer. Caller passes optional equipment=NULL because actual equipment producer is absent.

Current body begin signature:

```c
begin(owner, current_move_call, actual_client_call, actor, physical_slot,
      player_address, movement_address, equipment_or_NULL, &stack_scope, error);
```

Pass true `scope->call` client envelope after current move token. No manufactured token. Adapter cancels genuine ancestor client directly, not only the outer Pmove target that intercept could consume while ClientThink resumes. After success/failure/retirement/cancellation always unwind body scope then actual input scope. Skip source continuation and source_input(after) when real body scope retired/cancelled or pending true QVM cancellation.

Pure actual `qa_qvm_call_cancelled(current_call, &pending, error)` observes ancestor pending cancellation without consuming it or weakening strict once-only qa_qvm_cancel. Current caller queries before duplicate ancestor cancellation and after proceed. Source mode cleanup still restores conditional bytes only when exact real retained source record remains, regardless of pending cancel.

Outer original guest_client_scope now retains actual located table/stride and physical slot/player/movement record. `record_current` rechecks true VM data addresses/strides, full actor, source entity->client pointer, movement->player pointer, and physical slot within live entity/client bounds. It permits actual source entity high-water growth; exact count equality was corrected after QVM primitive owner review. `current` includes actual pending cancellation. Body adapter has corresponding exact pointer qualification across its callbacks.

Input inventory is seven genuine function descriptors max; body duck binding descriptor and identity join existing original input descriptors. Actual qa_qvm_checkpoint_functions validates whole VM hooks. Nested input codec is QAG3IN version2 with body_binding persisted and constructor identity equality checked on restore. Enclosing original QAG3PV2 input is opaque and delegates nested validation, so native owner confirmed no enclosing version change required.

Attach body after real input hook construction; detach body before original input hook leases. A detach failure retains actual context for retry, not freed callback storage. NativeQ3/no body declaration gives null body owner and unsupported capability.

Source raw usercmd hooks now run even when selected movement is foreign. Full raw int8 -128 and full32 words preserved. Actual stance changes true upmove slice byte. Actual mode output temporarily mutates original source PM type using admitted exact profile constants, restores iff bytes still equal our authored type and real record remains. Genuine nested chosen movement uses actual currently applying source input interval accessor, independent of bundle host interval; source scope.input_active true and actual full identity required.

The confirmed interval and holdable defects above are in these new callers and remain unrepaired. The suspected actual absolute aim gap also needs source review before freeze.

## CONTROL5 portable state and import order

Current real control input owner:

- Existing actual actor/provider/latest/sequence/arsenal/item/impulse/seen/retained metadata.
- New domain enum SELECTED0, QW_SOURCE1, Q3_SOURCE2 and actual `source_time_ns`.
- New has_body_base boolean and exact original base bounds if true.
- Actual transient q1 prethink actor/provider/frame/last host sequence/deferred marker; transient real borrowed kernel pointer; actual current command/context. Ephemeral markers are not serialized and checkpoint is busy while admitted.

Current real groups include original metadata, actual producer-origin bot boolean, domain/time, count and literal commands. Actual bot-origin is separately qualified by actual live roster getter AND genuine native bot-seat getter. Only receive_bot writes queued arsenal/item intent. false bot origin with nonzero queued intent rejected; retained intent requires both real lineage checks. Restored native seats are prepared before control import and genuine getter permits restoring; final bot agreement validates actual roster later.

Exact CONTROL5 field extension order is fixed:

1. Retained input metadata -> domain u8/source_time u64 -> has_body_base bool -> exact bounds iff present -> literal latest command iff seen.
2. Group metadata including actual bot -> domain u8/source_time u64 -> count/literal commands.

Saved raw source domain requires actual primary ENTITIES provider and genuine prepared physical source client membership, not just canonical QW/Q3 execution/provider kind/time. Actual prepared getters: Q1 prepared native slot permits continuation_pending without callbacks; native Q3 pure prepared fixed slot; original Q3 real guest actor slot; classicQC pure imported connected+spawned OWNED/BORROWED binding. Do not call executable `.command_actor` before source restore_finish.

Actual import runs before foundation final world clocks/source reconnect, so raw source_time upper bound is qualified at final controls capture, after foundation_finish restored genuine session clocks. Existing first/repeated persistence_shared_match capture supplies that validation; do not invent an extra restore callback or early defaultclock comparison.

Persistence owner `/root/persistence_resume` rotated actual save.c to QACTRLS5 and QA_SAVE_CONTROLS schema5, old4 rejected explicitly. It has independent source acceptance, but composing acceptance still waits this control owner packet. See external manifest below.

## Earlier review findings and their disposition

These are prior actual source review records, not blanket acceptance of the current follow-on. Earlier bounded nine acceptance had twice-matched hashes and source/whitespace only. Current code grew afterward.

| Finding | Disposition/evidence |
| --- | --- |
| Reserved classicQC QW clients without packet fell through actor_frame | Repaired reserved physical client skip after actual before_actor. Donor world/session/actor-execution.ts executeQuakeCActor always skips isReservedClient. Current QC NQ dual admission still separate open. |
| Parked A resumed then died in before_actor, leaving scopes active for B | Repaired immediate actual turn abort/consume before returning dead actor. |
| No selected Q1 real clock lease / weapon-only frame integration | Repaired actual world lease and weapon frame callers in earlier accepted nine, with native retained clock separate from command/private interval. |
| Native Q1 callback changed time but left stale g.elapsed | Q1 owner repaired scoped interval publication/restoration under actual lease, Q2 peer reviewed donor entity-services.ts:307-310. |
| Native/QC fractional nanosecond due projection admitted early | Actual source owners use ceil for eligibility projection and raw deadline retained as authority; raw future requeues after genuine interval. Pusher true local deadline bypass distinct actual kernel. |
| Selected Q1 inactive completed time should use start_ns | Reviewer withdrew after donor runtime.ts:4759-4764 frame-exit publication. Actual completed shared EXIT is correct; privateQC currentTime still independent ENTRY. Do not revert to start_ns. |
| Selected Q1 lease dedup same provider earlier role skips arsenal time | Reviewer withdrew after rereading exact pointer comparison; first same-provider role already recognizes actual arsenal. No patch needed. |
| Native map Q1 player PreThink missing with foreign arsenal | Repaired actual source presence/input/prethink independently of selected arsenal. Donor runtime3329/4503/4530. |
| Native map Q1 afterPhysics/wetsuit inverse missing with foreign arsenal | Repaired actual world source afterPhysics paired independently; selected arsenal weapon-only. Source Q1 owner also corrected genuine wetsuit body/motion producer order. |
| Selected Q1 weapon with Q3 movement used stale prior view/once guard | Repaired selected weapon phase input refresh from actual current movement view and genuine per-slice callbacks. |
| Typed Q2 movement never emitted selected weapon phase | Repaired true post-Pmove weapon phase before source afterPhysics, preserving input scope. |
| Q2 weapon/source PostThink uses raw command aim instead of actual Pmove result | Repaired actual result.view_angles carried through weapon and completed source body/PostThink, including classic raw angle_words +delta. |
| Native NQ retained turns based only canonical execution / actual source reservoir absent when Character+arsenal foreign | Actual Q1 source owner added true source_bind_client reservoir/membership, round prejoin caller and command_actor; native source routing landed. ClassicQC equivalent remains open above. |
| Pending Q1 PreThink active qualification excluded slice/passive output | Input activity now actual global attached source subscriptions incl slices; accepted QC output registry additionally actual retained output claims. Actual local output gate missing remains open. |
| Deferred Q1 source PreThink used first queued command | Repaired exact last received nonbot host sequence +actual map frame marker, then executed inside real qualified input scope only at that sequence. |
| Generated bots wrongly treated as host pending commands | Actual portable bot-origin producer flag added; bot-only source prethink eager, host selection filters nonbot. Enclosing priorCONTROLS4 used for that earlier origin addition. |
| Decoder accepts impossible bot origin/intent or fake native-seat lineage | Repaired count1 real bot/seat+roster lineage; falseorigin nonzero queued intent rejected; retained nonzero intent genuine sameproducer. Getter drift restored by bots owner before accepted checkpoint. |
| Selected Q3 StopFollowing requires actual PM_FLAGS owner | Actual fullgeneration writer landed; only true selectedQ3 gets write, foreign source holder remains distinct. |
| Native actual source_actor clock remained ENTRY while invoked copy saidPHYSICS | Actual source clock.phase publication repaired current session.c. |
| Native Q3 PM source speed doubleScout / noFootsteps / generic mode override | Current source profile copies realcacheddmflags, source speed applied once, actual authored PM type preserved. New2 owner reviews exact caller. |
| Native source_useHoldable intent lost after foreign command attack-only projection | Current semantic source_holdable parameter from actual postmask source command landed; different original QVM lost bit4 remains open above. |
| Native source gauntlet double actual damage probe | Current exact source arsenal uses known genuine contact. |
| Raw saved canonical fallback without physical source membership | Current domain_owner requires actual primary ENTITIES+prepared membership, persistence peer reread favorable. |
| Body continuation creates empty unsavable row | Current body helper avoids empty creation/removes truly empty cleared body-only row. |
| Original Q3 delayed raw queue reordered reliable messages | Current synchronous intake repaired; network peer reread actual caller. |
| Outer source pointer retarget survived before/after input | Current exact located pointers/table stride/actor checks and actual cancellation observer landed. |
| Exact entity count equality falsely cancels after source spawn/high-water growth | Current physical slot livebounds check replaced countidentity. |
| Body cancels onlymove then ClientThink resumes | Actual ancestor client token passed to current adapter; adapter directancestor cancellation landed. Primitive strictcancel kept. |

## External accepted / pending dependencies to reconcile

These are owner-reported source verdicts or old manifests read at handoff; none prove this full caller pipeline. Shared QC/Q1 files rotate, so use current owner freeze, not concatenated stale manifests.

| Owner / packet | Manifest and status |
| --- | --- |
| Q1 native source reservoir | `/tmp/qa-q1-source-client-reservoir-20260930.sha256`, owner says independent Q2 source accepted/frozen. Actual map roster integration separately owned. |
| Q1 prepared membership | `/tmp/qa-q1-prepared-client-read-20260930.sha256`, header e2f09635..., runtime06833df3... freeze was source reviewed; later same shared Q1 runtime may differ, reconcile owner. |
| Q1 input/wetsuit | `/tmp/qa-q1-source-input-wetsuit-20260930.sha256`, source kernel input observer permits character-only source reservoir; actual producer input fields/afterPhysics reviewed earlier. |
| QVM exact profile2 | `/tmp/qa-qvm-body-trace-profile-20260930.sha256`: guest_input_private.h19e27d2216c0b7800ecb86859794bda192e2470d1d9eec2ce2bf732706b88675; guest_input_profile.c cb9436040d23a22e1c516fc287ad93736e490ae79f66ba9cdad89a0bf14a5076. Root owner q1 says accepted. Retains actual duck/bodyTrace declarations, builtin9751 digestduck32561/callback224/mask28. |
| QVM real body adapter2 | `/tmp/qa-qvm-body-control-20260930.sha256`: guest_q3_control.c5a1d4654359bb906fcb9eb8cf997980cb05fb44fdd30570d93ea5bcc6ed16d2f; .h a71d1bd0411edea8841b0a7dccc36f5566c84f3ac905ea9438125ef55a9671a8. Current manifest read. Independent latest adapter verdict must come from q1/root; do not claim inherited previous adapter version accepted. |
| QVM foundation actualsourcecall/scratch/cancel | `/tmp/qa-qvm-source-call-20260930.sha256`, root event owner said refrozen accepted5. Includes qvm.h, execute.c/internal/memory/source_call. Real token source callback and stack-safe scratch; no fake trace/world substitute. |
| Original native Q2/RR adapter2 | `/tmp/qa-q2-guest-control-20260930.sha256`: C3daf4d1baf1f2357a9ffb4a3aad184e426d00858bedef1835c23952cdbb5883e; H7a17887322d09dc47aff57d4254b0c6a20ddc9694eba49fc810beb466dfd8ae9. q2 owner says independent peer source accepted. Runtime actual fullsource/Pmove/dimensions/PMTrace; capability qualified prepareddeclaration separately. |
| Native Q2 real lifecycle3 | `/tmp/qa-native-q2-control-lifecycle-caller3-20260930.sha256`, native owner reported separate source acceptance with adapter2. |
| QC raw output registry13 | `/tmp/qc-client-output-source-20260930.sha256`, owner said independent peer accepted13. Actual outputs lease/codec6 claims retained acrossclear/release untilsourceclose; consumer capability/active gate still our open pipeline. Shared QC files subsequently lifecycle rotated. |
| QC reserved lifecycle | `/tmp/qc-qw-lifecycle-source-20260930.sha256` current recorded11; sourcepeer reopened confirmed reserved FREE removal divergence, QC owner repaired then renewed source review pending. Earlier lifecycle7/11 freezes do not establish current full acceptance. Pure preparedmembership and runtimecommand_actor consumers exactdependency. |
| Native Q3 new ClientThink2 | `/tmp/qa-native-q3-control-source-20260930.md`, new native_q3_control.c/.h owner tools, no final aggregate acceptance at handoff. Source ring/links/impacts/world shared origin continuation actual source pipeline. |
| Native Q3 sourcePS/kernel hook | q3_source owner actual qa_q3_hooks.source_movement_state plus masks; round providers typed binding landed. SourceCore9 and other source packet manifests exist; exact final owner verdict needed. |
| Native Q3 client admission/run | q3_clients owner policy/special/deferred/source_run/movementparams/source actualsess7/ready/bodyreset. Requires native pipeline and control context. Exact final owner packet needed. |
| Native Q3 wire actual policy | q3_wire producer real masked `qa_q3_wire_player_policy_update` and services movement_policy, selected callback binder q3_wire owner. No mirror holder. Exact latestpacket owner must qualify; earlier wire5 accepted then released. |
| Native original Q3 role14 | `/tmp/qa-native-client-roles-unit-20260930.sha256`, native owner realcommand_actor and opaqueinput continuation qualification. Original QAG3IN2 enclosing unchanged. |
| Network raw Q3 runtime2 | `/tmp/qa-network-q3-source-command-runtime-20260930.sha256`, readrecord runtime header5766b838..., commands686964d3..., network owner exactpacket separate review. |
| Network raw Q3 callers3 | `/tmp/qa-network-q3-source-command-callers-20260930.sha256`, appnetwork b4ee3128..., publicnetwork a76e3a09..., frontend3618ed91..., owner pending coherenthelpers. |
| Persistence source5 | `/tmp/qa-application-persistence-source5-20260930.sha256`, save.c37d41b11415218708a324324e09b67e3d1c6730d35e6d346695661d346ec7478, persistence owner reports independent whole-file source acceptance. Actual QACTRLS5/schema5 clockfinalcapture. Composition waits unaccepted CONTROL5 caller. |

## Next steps after explicit session resumption

1. Read root's full handoff and all actual owners' newest `/tmp/qa-handoff-*` records first. Do not restart or rewrite accepted source. Reestablish exclusive owner roster and current hash snapshots. The earlier old nine accepted manifest is not the current caller packet.
2. Repair the four confirmed current findings, first real QC NQ source_actor+once-per-actual-frame callback marker and actual nonrawNQ source execution, then QVM mathematical callback elapsed and source-unrepresented raw control preservation. Review new transient fields against persistencebusy rather than codec fabrication.
3. Close actual originalQ2 camera/classic Pmove output consumers with assigned owners. Trace QVM source absoluteaim and source assignment lifecycle ordering. Treat real missing local QC/NQ eligibility and selected QVM equipment fields as genuine producer work.
4. Finish source pipeline dependencies: nativeQ3 new2, genuine sourcePS hook/provider/roster, QC revised reservedlifecycle, bot roster/native seat getters, raw network and QW reliable actions. All producer/caller changes require full graph source review, not only own definitions.
5. Freeze complete real owner roster/current hashes. Request `/root/movement_source_peer` whole source read against donors, plus all independently owned dependencies. Root explicitly says outer caller20 remains unaccepted. Only root integrates coherent reviewed packets and frequent commits.
6. Preserve strict source-only gate until the user/root changes it. Current source hashes or whitespace do not prove runtime behavior. No build/test/source game/parser validation was performed in this lane.

## Evidence precision

This handoff distinguishes command/source reads, inherited owner-reported acceptance, confirmed peer counterexamples, and suspected unfinished source contracts. No current remote HEAD, build, runtime, complete baseline parity, or whole pipeline acceptance claim is made. The body/QC/NQ and original QVM defects above prevent completion. Source root had paused for a user-requested new-session handoff, not a sandbox error.

