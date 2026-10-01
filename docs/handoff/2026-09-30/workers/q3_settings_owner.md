# Native frontend frame-time handoff — partial source work

Frozen for the user's new-session handoff. This packet is not independently accepted and has no compile or runtime qualification. Continue from the existing files; do not treat the missing mirror implementation as complete.

## Owned current files

Only these new repository paths were edited for the active assignment:

| Path | SHA256 |
| --- | --- |
| `src/app/frontend/frame_time.c` | `9de52c01d9ab6398d4b42f7b0199d7dedea416a9490d39fa207f15749ba6db94` |
| `src/app/frontend/frame_time.h` | `5ffd833eaef4111d69061d70d387c5f7378f56336aa46771484f767e39ef270e` |

Both whole files were reread after the edit, these hashes were read twice, and the trailing-whitespace scan had no matches. There is no manifest acceptance, independent review, executable validation, or caller integration. The assignment inherited its parent model and reasoning effort; no model override or descendant was used.

Current actual public signatures:

```c
typedef struct frontend_frame_time_controls {
    double timescale, fixedtime, host_framerate, camera_mode;
} frontend_frame_time_controls;

bool frontend_frame_time_register(qa_cvars *, uint64_t owner, qa_error *);
bool frontend_frame_time_controls_read(const qa_cvars *, frontend_frame_time_controls *, qa_error *);
bool frontend_frame_time_transform(qa_console_dialect, double supplied_milliseconds,
    const frontend_frame_time_controls *, bool dedicated, bool local_server,
    double *source_milliseconds, qa_error *);
bool frontend_frame_time_sample(const qa_cvars *, double supplied_milliseconds,
    bool dedicated, bool local_server, double *source_milliseconds, qa_error *);
```

Implemented only: actual source-registry registration, actual registry reads, and the pure donor delta transform. No numeric cache or time accumulator was introduced.

## Actual timing donor and numeric authority

Primary behavioral donor: `../quake-typescript/src/app/bootstrap/frame-time.ts`, read completely.

`registerFrameTimeCvars`:

- Q1/QuakeWorld: timescale `1`, host_framerate `0`, flags `0`; existing Q1 definitions are left alone.
- Q2 classic/rerelease: timescale `1`, fixedtime `0`, both Q2 CHEAT `32`.
- Q3: timescale `1`, CHEAT `512` plus SYSTEMINFO `8`; fixedtime `0` and com_cameraMode `0`, CHEAT `512`.

`readFrameTimeControls` uses the registry's binary32 numeric values. Q3 fixedtime uses the genuine signed integer field; other dialects use the numeric field. cameraMode uses the integer field. No cached vmCvar mirror is involved in this stateless read. TS `core/cvars/index.ts` numeric storage uses `Math.fround`; C `qa_cvar_view.number` is float.

`sourceFrameMilliseconds` validates finite controls and finite/nonnegative supplied milliseconds, then:

- Q1/QuakeWorld: positive host_framerate returns its seconds multiplied by 1000 without the ordinary clamp. Otherwise timescale zero selects supplied delta, nonzero multiplies it, then clamps to 1..100 ms.
- Q2: nonzero fixedtime is returned directly, including signed/fractional donor values. Otherwise timescale zero returns supplied delta; nonzero multiplies and clamps only the minimum to 1 ms.
- Q3: truncate supplied ms; binary32 timescale; truncated fixedtime/cameraMode. Nonzero fixedtime wins. Otherwise, when scale or cameraMode is nonzero, binary32 input times binary32 scale is rounded binary32 again; reject nonfinite product or product outside [-2147483648,2147483648), then truncate. Clamp minimum to 1 only when scale is nonzero. Maximum is 200 local nondedicated or 5000 dedicated/remote ms.

Current C stores binary32 intermediate values with volatile float objects and returns double ms. Signed/fractional outputs have deliberately not been silently converted to the existing unsigned nanosecond API. The real frontend/application clock-domain integration remains open; root must decide the genuine signed/double authority rather than clamp negative Q2/Q3 output implicitly.

One source-level numeric qualification remains before acceptance: the general pure transform accepts arbitrary finite double controls/delta, but its double-to-float casts have not been qualified for values beyond the binary32 finite range. Actual registry values already have float storage and an actual uint64-nanosecond delta fits binary32 finite magnitude; that narrower fact does not prove the broader helper domain. Preserve TS `Math.fround` overflow behavior when repairing/qualifying this boundary.

## Real producer order; no invented wall clock

Read donor `application.ts:4450–4535`: sourceFrameMilliseconds runs on supplied elapsed before capture `frameTime(sourceMilliseconds, true)`. Application elapsed is advanced by the capture-adjusted source delta. Platform input retains supplied elapsed for its wall-duration argument; network socket polling receives actual `performance.now()` separately. Source registry selection is `Application.sourceCvars` at `application.ts:553`: actual Q3 guest registry, Q3 source host registry, Q2 server registry, Q1 source registry, then QuakeC source registry. Do not select an arbitrary engine/wire registry as the source authority.

`createStartupSource` in `../quake-typescript/src/app/bootstrap/startup-source.ts` and `application.ts:1319` register the real time controls in the selected source preparation/command phase. Current C frame_time_register has no caller yet.

Current C `src/app/frontend/frame.c` was read completely: qa_frontend_step currently capture-adjusts the supplied uint64 delta before advancing `f->time_ns`, then passes the replaced delta to input/network/application/presentation. This is not yet the donor source-transform-before-capture producer. Parent `frontend_resume` owns the caller changes; this lane did not edit frame.c.

Local `f->time_ns` already exists and must not be duplicated by a new owner. Remote donor `remote-application.ts:2108–2183`, `remote-seats.ts:1–60`, and full `frame-clock.ts` show separate actual channel continuations: PresentationTime optional committed milliseconds equals `(previous ?? supplied wall_now-wall_elapsed)+presentation_delta`; RemoteSeatChannel owns sourceElapsed/sourceFrameElapsed/frameNumber. No such new state is implemented here. Coordinate actual channel owners with `network_resume`; do not invent `performance.now()` in C or add unused canonical clock state.

## Missing role/mirror/SystemInfo implementation

Absent, not implemented:

- frontend aggregate factory/lifecycle holder;
- real per-CGAME role holder, explicit installed timeCvars policy, physical-current callback;
- coalesced owner/name subscriptions, mirror write subscriptions, actual observer tokens;
- ordinary post-CGAME Init mirror bind and close at physical lease retirement;
- SystemInfo parsing, last-applied full info string, once-timescale guard;
- copied continuation/private codec, pure candidate import, late callback reconnect without refresh;
- any source.c/lifetime.c/frame.c/aggregate persistence integration.

Design discussed with frontend_resume, but no corresponding APIs were added to frame_time.h.

Actual donor `../quake-typescript/src/app/bootstrap/q3-client.ts:238–327`:

- `refreshSystemInfo` applies all complete backslash name/value pairs except `cl_allowdownload`, case-insensitively.
- If genuine optional timeCvars is installed, skip its owner-dialect time fields (Q1 two, Q2 two, Q3 three) while applying SystemInfo.
- Independently, skip timescale if the incoming full info string equals this role's last applied info string. Retain the full last-applied string after processing defined info.
- Afterward, refresh the bound SharedCvarMirror if present; otherwise refresh only the owner's time fields when timeCvars exists.
- `create` finishes client initialization, then `bindFrameTime`. Bind closes the previous mirror, returns when optional owner is absent or aliases client registry, otherwise refreshes SystemInfo before constructing the new mirror.
- Main local original GAME and native GAME creation paths explicitly supply timeCvars at `application.ts:2497` and `2519`. This policy is true in both; native_source is not its proof. Remote paths omit it and need their own actual missing-owner profile.

`core/cvars/mirror.ts` was read completely. Real SharedCvarMirror contract:

- owner and mirror are distinct registries in the same actual session and dialect;
- constructor registers each mirror row with actual owner reset text and flags, refreshes values before subscriptions;
- one real owner/name binding is shared across all mirrors through a WeakMap fanout;
- a mirror write force-sets owner unless this mirror is refreshing;
- an owner value publication refreshes ALL names of each subscribed mirror;
- close removes per-mirror bindings and removes owner binding only after the last subscriber leaves.

Q3 client names are the three Q3 time controls PLUS actual declared collision-map controls, filtered by real owner.find. `../quake-typescript/src/world/collision/q3/settings.ts`, read completely, defines cm_noAreas `0` CHEAT, cm_noCurves `0` CHEAT, cm_playerCurveClip `1` ARCHIVE|CHEAT. Q1/Q2 owners use their own two time names plus these three rows, if actually declared. This mirror is not the separate source gameplay-setting initialization/serverSettings producer.

Reserve the donor's one actual value-binding slot per name; merely observing without reserving it would permit a binding collision the donor rejects. Coalesce real owner/name subscriptions, not a numeric shadow. For mirror refresh, suppress its post-observer tokens at EVENT ADMISSION while force-writing every name; restoring refreshing=false before queued callbacks run must not cause feedback.

## Actual role authority and integration owners

`native_resume` installed NEW `include/qa/application_q3_client.h` and actual `src/app/application/guest_q3_exports.c` accessor:

```c
bool qa_application_q3_client_context_read(qa_application *, qa_actor_owner receiver,
    uint32_t seat, qa_application_q3_client_context *, qa_error *);
```

The typed view carries exact session, receiver, seat, source_client, service_owner, frontend_lifetime, console, client cvars, actual GAME source_owner/source_cvars, command_context, actual source frame/time, native_source, and actual role.initialized. It can admit initial SystemInfo before CGAME Init while GAME Begin/gamestate context is real. The source clock is startup frame0 or completed EXIT for later frames. This accessor and header remain another owner's moving packet; no independent acceptance is claimed here.

Its actual source_cvars pointer proves registry topology only, not that optional timeCvars was installed. The role owner must retain explicit factory policy. `initialized` provides the actual post-CGAME Init bind boundary.

`frontend_resume` owns `src/app/frontend/source.c` and its private frontend_source_lease. The lease already stores exact source/role/service_owner/console/command; the factory sets host.frontend_lifetime to that physical lease. It must own the new role pointer, close it before actual registry/group retirement, and expose per-role capture/import through the existing physical group order.

Parent confirmed a physical-current callback can qualify the linked actual lease/session/service_owner/console/cvars/frontend_lifetime plus actual application ownership independently of source EXIT. It is NOT implemented. The typed client effect accessor alone cannot be used by source cvar observers during a GAME frame because donor SharedCvarMirror assertCurrent permits those mutations.

Pure candidate source role factories occur after foundation cvars import. During prepare/import, do not call the executable-context accessor or run registration/refresh/effects. Proposed role codec preserves true installed-owner policy, source-owner identity, bound-name edges, and last-applied info; resolve actual restored role/registry identities and reconnect callbacks only in final finish after core ownership is restored. Do not duplicate registry values already captured by actual QACV owners, and do not refresh during finish.

## Unpublished cvar publication seam snapshot

Root assigned `frontend_baseline` exclusive `src/console/cvars.c`, `src/console/cvars_private.h`, and NEW `include/qa/console_cvar_observer.h`. These are another owner's draft, not this lane's edits. On-disk hashes read at this handoff snapshot:

| Path | SHA256 |
| --- | --- |
| `include/qa/console_cvar_observer.h` | `91571fad914832f92b9d652eac6e9ba66734a38e7084e47691e9f9330cd894f1` |
| `src/console/cvars.c` | `e1433294e0fd12246c259d468f0a8bffbcb6ff112ea1feffd5f168fdd6d135b8` |
| `src/console/cvars_private.h` | `99e3b4389e0a3c5db9b503f41f02fec9f7217286c4c94a4d793843a263400a66` |

Header was read completely; whole modified implementation was NOT reviewed by this lane. Owner confirmed the same frozen hashes and documented its own packet at `/tmp/qa-handoff-frontend_baseline-20260930.md`. The observer APIs/helpers/fanout/suppression/fences landed, but PUBLIC MUTATION WRAPPERS DID NOT: mutation_begin/end are unused and no FIFO drains run. The seam is currently unusable and unaccepted. Generic console.h and cvar-save were unchanged. Finish the actual wrappers before installing mirror observers.

Public draft signatures are qa_cvars_observe(registry,name,owner,callback,user,&token,error), qa_cvars_unobserve(registry,token), qa_cvars_observer_suppress(registry,token,bool,error), qa_cvars_observer_idle(registry). Callback is bool `(void *, qa_cvars *, const char *, qa_error *)`; canonical name is borrowed only for that callback.

Agreed required producer semantics: publish exactly at existing binding.changed sites, including accepted equal writes, never latch-only staging/unrelated metadata. Drain synchronously after the outer setter finishes flags/effects/cleanup. Nested mutations append FIFO. Registration-order observer inventory is admitted before publication; admission-time suppression excludes refresh-produced feedback events even when suppression is cleared before drain ends. Tokens are registry-scoped/nonreused; retirement detaches before deleting variables; destruction/restore require complete drain. False callbacks propagate failure after all admitted events drain; already published state remains. Owner must qualify actual implementation and lifecycle, freeze its exact packet, and obtain independent review. This lane did not claim coverage from header alone.

## Next concrete work

1. Independently review/fix/freeze the actual observer seam; confirm its real setter wrapping covers nested/accepted-equal notifications and callback borrow/retirement rules.
2. Qualify/repair the pure transform's binary32 cast boundary and get independent source review of both frame_time files.
3. Add genuine frame-time aggregate and per-physical-CGAME role owner with explicit optional time authority, own last-applied info, actual binding reservations/coalesced observer edges, and deep text lifetimes.
4. Add genuine SystemInfo producer with exact exclusions/once-timescale guard, pre-Init initial effect admission, and explicit post-Init subscription bind.
5. Add pure role continuation codec and late restored callback binding without registration, refresh, update replay, or source calls; coordinate source.c group codec/caller ownership.
6. Resolve signed/double source delta clock authority with root, then parent wires real source registry registration/sample BEFORE capture adjustment, keeping supplied monotonic network/input duration separate.
7. Coordinate any real remote-channel continuation with network_resume; do not duplicate local f->time_ns.
8. Freeze the whole finished owner packet and send it to an independent source reviewer. Full source-baseline execution remains gated until root authorizes it.

Do not claim this partial packet closes the frontend baseline or native-role lifecycle.

