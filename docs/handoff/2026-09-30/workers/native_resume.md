# Native source owner handoff, 2026-09-30

## Frozen status

All edits stopped at the coordinator's source freeze. Current draft8 is an
incomplete, unreviewed next unit. It must not inherit acceptance from the earlier
role packet. Only source and metadata checks are recorded here.

Current draft manifest:
`/tmp/qa-native-next-context-parser-draft8-handoff-20260930.sha256`.
All eight current path hashes matched at handoff. Scoped tracked whitespace and
the new public header's no-index whitespace check had no diagnostics.

| Exact path | SHA256 |
| --- | --- |
| `include/qa/application_q3_client.h` | `e946070b177d5aa1e2dae21159497d607ffad487334a00f80e521a83cebc1d1b` |
| `include/qa/q3_host.h` | `55a1acf69e1c64c4934375d1f20a6e87713254330eb1d73f53306a6e9d34957e` |
| `src/compat/q3_host/host.c` | `cd342af25b0d63e16dd74e1ceec15777450011b2cfe77eba31924d990ab9eacc` |
| `src/app/application/guest_q3_exports.c` | `b63be25d4624c24cae9de99699b4124fbdd0d383b7c25a3035e04572543ffc75` |
| `src/app/application/guest_q3_clients.c` | `357c63e474803da4320215c41342aa84221071f773c02ea0ddca22643d806762` |
| `src/app/application/guest_q3_private.h` | `2151c339b7112c82f7078a5cc34e9dbf0b664d64f7188a27d56c67f8566a9490` |
| `src/app/application/guest_q3_save.c` | `036978d8134200b707ecf805ec1e8b2110d2482133e5e25bce254aa90dbf6a32` |
| `src/app/frontend/native_q2.c` | `7f872bf0df6de7579d5ec58af97a3f067e8688abbc1dded41652aef9681f4105` |

The private header and guest save source remain byte-for-byte unchanged from
the prior accepted14. They are included as exact dependencies because the new
live parser no longer maintains their obsolete BCS index/phase metadata.

## Prior accepted native14

`/tmp/qa-native-client-roles-unit-20260930.sha256` has manifest SHA256
`1e65c74bd6ded29f817c0b753174e182db6b419da3f6b99465556b13352d0e68`.
`native_source_peer` independently accepted the complete bounded packet and
qualified all fourteen hashes and tracked/new whitespace. The current draft
supersedes some of those paths, so that manifest is historical acceptance
evidence and will no longer match every current file.

The accepted14 inventory was:

- `src/app/application/guest_admission.c`
- `src/app/application/guest_q3.c`
- `src/app/application/guest_q3_clients.c`
- `src/app/application/guest_q3_effects.c`
- `src/app/application/guest_q3_exports.c`
- `src/app/application/guest_q3_private.h`
- `src/app/application/guest_q3_roles.c`
- `src/app/application/guest_q3_save.c`
- `src/app/application/guest_q3_server.c`
- `src/app/application/guest_q3_restart.c`
- `src/app/application/guest_q3_restart.h`
- `src/app/frontend/native_q2.c`
- `src/app/frontend/native_q2_save.h`
- `docs/implementation/guest-q3-restart-20260930.md`

That acceptance covered actual native GAME client lease construction, genuine
selected human CGAME/UI topology, arguments/command/effect ownership, physical
close retention, source retirement and descriptor rebuild, original prejoin
reservation, raw enter/carry command producers, private-v2 retained topology,
the disabled-bot source-frame gate, and native Q2 parent/child last-release,
failed factory and unbound-discard retention. It also covered the earlier BCS
8192-byte full-command capacity and ignored continuation index amendment.
It did not accept the later full arbitrary CS/BCS parser behavior or its new
codec representation. General caller integration remained separately owned.

The existing role topology intentionally does not execute native wire gameplay
getters during private restore finish. A CGAME provider can precede its GAME
wire owner in the restored inventory. Typed lease identities qualify source
pointer, logical source owner, receiver, seat and physical slot while wire
restore is pending; actual GAME wire finish later qualifies physical rows.

## Accepted Q2 rerelease movement lifecycle caller3

Manifest:
`/tmp/qa-native-q2-control-lifecycle-caller3-20260930.sha256`.
Manifest SHA256:
`3a01b66672d3a716d60becb1de94e5519c53b8cb2f23cbaafacee3314cdeff18`.
Its three current hashes still match. The actual adapter producer source-reviewed
the complete caller chain and accepted it. The coordinator supplied its accepted
release status for this unit.

| Exact path | SHA256 |
| --- | --- |
| `src/app/application/guest_native_q2.c` | `4767181bbb88cc120948811ed9bfb84219846cbdd54348fe9b7ed7cfec17a462` |
| `src/app/application/guest_native_q2_private.h` | `bad929cac6a6d22c747451e502cfff3d17d4c1f0db69efae4d3915710131bb56` |
| `src/app/application/guest_native_q2_save.c` | `6a50f3e73e4546850f5b1842dbd89d5c53c7df69b177ba4d964cc9a9acc4b56e` |

Frozen producer dependency:
`/tmp/qa-q2-guest-control-20260930.sha256`.

| Exact path | SHA256 |
| --- | --- |
| `src/app/application/guest_q2_control.c` | `3daf4d1baf1f2357a9ffb4a3aad184e426d00858bedef1835c23952cdbb5883e` |
| `src/app/application/guest_q2_control.h` | `7a17887322d09dc47aff57d4254b0c6a20ddc9694eba49fc810beb466dfd8ae9` |

`movement_source_peer` independently accepted the full producer after reading
the real rerelease ABI, donor dimensions/trace behavior and observer lifetime.
The actual producer reported matching combined five-file hashes after its
caller review. No producer edits were pending at handoff.

The engine now owns opaque `source_control`. Constructor preparation follows
the real declaration and existing attack/combat preparation. A prepared control
adapter enables actual native observation. After genuine source Init succeeds,
control activation occurs in the guarded source setup before SpawnEntities.
That setup may have `engine->calls > 0`; the producer explicitly admits it.

Ordinary map retirement disconnects real source clients before suspending the
observers. Private restore suspends them before source replacement. Restore
finish activates control only when `initialized && map_ready`; a genuinely
retired map remains observer-free until its next real spawn. Deconstruction
closes the control owner after actual Shutdown and before physical host release,
with a final idempotent close for failed constructors. Failed unobserve retains
the actual handles, context and complete engine for retry. This adds code and
observer ownership, not native private state authority or a new save schema.

## Current CGAME context contract

The new public header defines:

```c
bool qa_application_q3_client_context_read(
    qa_application *, qa_actor_owner receiver, uint32_t seat,
    qa_application_q3_client_context *, qa_error *);
```

The returned borrowed view contains:

- actual `session`, `receiver`, `seat` and physical `source_client`;
- real `service_owner` and host-owned `frontend_lifetime`;
- actual receiver `console`, `cvars` and `command_context`;
- real `source_owner`, `source_cvars`, `source_frame` and signed source milliseconds;
- `native_source`, derived from a real native client lease;
- `initialized`, copied from the real CGAME lifecycle marker after successful Init.

The producer is in `guest_q3_exports.c`. It selects the genuine receiver in the
installed routing inventory when one exists, otherwise the current provider
inventory. It rejects duplicate logical receiver entries. The selected provider
must belong to the same application and be constructed, attached and outside
pending close. The engine cannot be restoring or in a private round cut.

Exactly one ready, nonretired CGAME host must match the real seat. The role must
have its actual local client ordinal below64. Platform-only/remote CGAME services
without a real local GAME source return unsupported; the accessor does not
invent a local owner, registry or clock for that path. UI is never selected.

`qa_q3_host_client_context_read`, added to public `qa/q3_host.h` and the actual
host owner, reads the host's installed session/role/owner/service generation,
console/cvars/command and frontend lifetime. It rejects a retired host or GAME
role. It is a pure observer of the real options. The application accessor checks
CGAME role, same session, exact actor owner and service owner, command receiver
and seat, and Q3 command dialect.

The frontend lifetime pointer remains owned by the actual host. The view neither
retains nor releases it. A custom host can expose its actual null lifetime; a
frontend caller must require a matching concrete installed lease rather than
fabricate one. Frontend service groups can overlap while old hosts are retained,
so the real lifetime pointer and service generation are required alongside the
receiver/seat. A bare owner/seat match is insufficient for frontend group identity.

The context accessor intentionally permits `initialized == false` and nonzero
source-call counters. The real pre-Init SystemInfo effect runs after physical
source Begin and gamestate publication but before CGAME Init. Synchronous
source effects must read their retained actual receiver context in that scope.
It does not admit constructor topology alone as gameplay readiness.

For a native GAME source, it reads the real retained wire-client topology and
compares the actual source provider pointer with `role->client_source`, logical
owner, receiver, seat and physical ordinal. The source must belong to the same
application and be constructed/attached without pending close. `source_cvars`
comes from the actual native GAME's `application_native_q3_console_registry`.
True source time comes through `application_native_q3_wire_client_time`, which
requires genuine physical Begin and the live source binding. This is not the
CGAME-only provider's clock.

For an original GAME source, the accessor uses the same engine's actual GAME
host. GAME must be initialized, nonretired and map-ready. The source row must be
allocated, begun, nonbot, outside pending retirement and retain the real live
full-generation actor. The actual GAME host actor slot must match the role's
physical ordinal. A decoder Disconnect can clear `connected` before its frontend
callback; this context observer still qualifies the retained begun actor/host
binding, rather than losing the real effect receiver prematurely. It returns
that GAME host's actual cvar registry and genuine provider source time.

Both branches require the role's real gamestate service to return actual state
after Begin. A final session-clock read validates the real source owner and Q3
clock kind; nonzero source frames must be EXIT. Genuine initial clocks are
created as EXIT frame0 in `src/session/session.c:208`. The view retains the
actual full source frame, not only a derived frontend timestamp. UI presentation
keeps real/frontend milliseconds in its existing separate source-time branch.

Actual original GAME and receiver cvars commonly alias because the installed
generic services pass `app->cvars` to both real hosts. The view returns those
actual pointers. A native GAME's engine registry is separate from the default
receiver app registry. Neither arrangement is inferred or rewritten by this
accessor. `source_cvars` means actual GAME registry identity; it does not prove
that an optional TypeScript `timeCvars` owner or SharedCvarMirror is installed.
The new frontend frame owner must admit that actual time authority and mirror
subscription explicitly. It must preserve an absent time option when the real
donor path has none, and avoid constructing a mirror for aliased registries.

`frontend_resume` owns actual factory/effect consumers. `q3_settings_owner`
owns new `frame_time.c/.h`. Both received the installed draft signatures and
the pointer/clock distinctions. The initialized bit gives their actual later
bind stage a post-Init marker; factory construction is not that stage. Receiver
context, frame owner and their real consumers require one bounded independent
source review before acceptance. The current draft has not received that review.

## Critical incomplete CS/BCS codec integration

The live original decoder now follows the inspected SDK and TypeScript behavior:

- Only exact `bcs0`, `bcs1` and `bcs2` are special. `bcsExtra` remains ordinary.
- Missing argv2 is empty, extra arguments are ignored for fragments.
- `bcs0` writes the actual authored `cs <argv1> "<argv2>` prefix through8192-byte
  snprintf storage. It accepts missing/nonnumeric/out-of-range index spelling
  at this stage and retains actual truncation to8191 bytes.
- `bcs1` and `bcs2` lazily allocate constructor-empty storage and append to the
  actual retained buffer, including a previous completed buffer. There is no
  synthetic active/start gate. Full length including the final quote is bounded.
- `bcs2` appends quote/NUL, retokenizes that full buffer and uses it as the real
  effect text. The actual buffer remains retained afterward.
- Only the resulting `cs` command interprets argv1. The local integer helper
  matches donor ASCII whitespace, optional sign, decimal digit prefix and signed
  32-bit saturation. Nonnumeric/missing argv1 becomes0. Final negative or
  out-of-range configstring indices are rejected by the actual application path.
- CS payload is actual joined argv2 onward, obtained from the tokenizer-owned
  Q3 `args_text` after its argv1 and separator. No exactly-three-arguments or
  whole-decimal restriction remains. Empty and extra CS arguments are handled.

The donor references read were original SDK
`code/client/cl_cgame.c:200-252,304-332`, TypeScript
`src/network/q3/client-server-command.ts`, and `src/core/numeric.ts:183-227`.
The existing public gamestate setter still owns individual8192-byte and whole
16000-byte string storage limits.

**The live parser and old private codec currently disagree.**
`guest_q3_private.h` still contains `big_configstring_index` and
`big_configstring_active`. `guest_q3_save.c` still serializes both and rejects
raw buffers without a parsed numeric `cs` prefix/index match/final quote.
The new live decoder no longer writes these synthetic fields. For example,
pending `bcs0 5 "a"` now retains `cs 5 "a`, but the old index remains0 and a
source checkpoint fails its numeric-owner validator. This is a known new-unit
incompletion, not accepted behavior.

First resume action for this producer is to coordinate exclusive save-source
ownership, remove the synthetic index/active fields and their codec gates, and
save only actual raw buffer allocation presence, length and exact terminated
bytes. Validate the real8192 allocation extent and exact terminator without
manufacturing parser phase or interpreting a pending authored index. Bump the
nested QAG3ST state version2->3 with that field layout change. The enclosing
QAG3PV2 treats the state as a nested blob and does not itself hardcode the old
inner version, but its complete consumers must still be reread.

The native wire owner made the same real representation change independently:
only buffer/length/allocation, removing index/active and prefix/final-quote
checks, wire private version3->4. Its current next unit also matches exact
BCS/CS parsing and tokenizer-owned payload joining. Coordinate its actual final
frozen bytes and reviewers; older wire5 acceptance does not cover this next unit.

## Native Q2 detached candidate resources

The draft changes only actual candidate image construction at
`src/app/frontend/native_q2.c:833` to
`qa_scene_resources_create_detached(source->mounts,error)`. That row is genuinely
prepared/restored. Ordinary live lazy image creation at line60 still uses
`qa_scene_resources_create` with the real built-in image creation behavior.

The detached API is published in `qa/scene_resource_save.h`. Its owner reported
independent source acceptance of the resource packet. It allocates real owner
and name storage without generating images/default/policy. Actual QAIM and QARS
import installs exact image/built-in edges and policy, clearing detached only
after full finish. This one-call native candidate migration itself remains in
unreviewed draft8 and requires its real factory/restore consumer review.

Prior accepted native Q2 lifetime rules remain: factories preflight actual
parent and nullable child owners; consumed last-release clears borrowed engine
aliases but retains linked unbound owned rows while parent/child capture or
audio retirement fails. Source storage is freed/unlinked only after genuine
readiness and successful effects. Unbound discard retries retained constructor
or last-release storage. Pure child-idle reads actual images/fonts and import
activity even after the application pointer has been cleared.

## Open original classic Q2 output consumer

No edit was made to `src/app/application/guest_native_q2_services.c`; the narrow
classic movement callback ownership request remained ungranted at freeze.

The real original branch `application_native_q2_move` invokes actual source
ClientThink and bypasses generic typed `prepare_input`. Classic source Pmove
imports invoke the genuine `movement_prepare` callback in that services file.
It currently applies foreign-character canonical body bounds, standing view,
flight and gravity but does not consume actual typed source mode/stance/body
publications. The accepted `application_q2_control_outputs` helper qualifies
real classic184-byte public player state as well as rerelease296-byte state;
its output getter does not require the rerelease-only dimensions declaration.

The next narrow consumer should use that actual live source qualifier in the
classic Pmove callback. Existing typed preparation maps published mode to
environment mode, stance to `has_stance/crouched`, and optional body shape to a
requested hull while preserving current accepted bounds. Actual source health,
intermission and chase admission must remain qualified. Camera view offset is a
separate consumer and must retain the real crouch/stance guard. Do not use the
generic typed branch as proof this original callback already consumes outputs.
Rerelease source Pmove/dimensions observers remain the original source authority
for its stance/expansion trace and must not be replaced by the classic adapter.

## Capability limits and dependencies

Original external native Q3 movement.body remains unsupported without a genuine
artifact-qualified native Pmove/dimensions/trace ABI. The new QVM body adapter
does have an actual declared duck callback and source trace/scratch contract;
it is owned by the existing application_guest_input attachment, not a new role
pointer. `q1_resume` owns that QVM adapter and `movement_resume` owns its actual
arsenal/input-profile consumers. Their nested QAG3IN version1->2 adds the real
seventh duck function descriptor. Current QAG3PV2 already treats input storage
as an opaque blob and delegates QAG3IN validation, so no enclosing guest-save
version edit is required solely for that independent input descriptor change.

Real source lifetime reset/reload does not establish a whole original native
Q3 module private continuation. The QVM complete envelope stores real executor
memory and actual service state; original native modules need separately
qualified private globals, guest allocations, relocation and import-owner
authority. Do not claim mutable-module fidelity from an image memcpy, stable
outer host, reset, declaration entry address or body observer.

Native Q2 original GAME/LEVEL reconstruction and its engine/platform codecs are
separate from the movement observer owner. The existing baseline exchange
explicitly qualifies module/declaration ABI identity while the full producer
must separately qualify private continuation. Native Q2 cgame frontend private
continuation still rejects unqualified guest return allocations/catalogs. The
new declared dimensions producer and caller3 do not widen those capabilities.

The native wire owner reported a genuine UI-only GS producer in its new mutable
packet: initial GS after physical Begin for real UI lifetime leases, no UI
snapshot/reliable history, direct qualified UI-only configstring updates, and
real snapshot production only for CGAME readers/bots. That closes the known
producer design gap on current source, but obtain its new exact freeze and
independent acceptance before using it as completed integration evidence.

General slow-replacement consumers, genuine retained/fresh Botlib disposition,
actual post-Shutdown registry handoff, external role pending-close ordering,
frontend frame/effect callers and broad persistence integration remain assigned
to their respective owners. Consult their current frozen packets and handoffs
instead of extending the bounded native owner acceptance to those callers.

