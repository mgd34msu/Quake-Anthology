# Native Q3 wire handoff

This is the handoff for `/root/q3_wire_resume` on 2026-09-30. The user requested a new-session handoff. Root froze the latest five-file packet; do not change source until the next session explicitly releases its exact files. No project executable validation has occurred. All acceptance below is bounded source review.

## Exclusive ownership

Workspace: `/home/buzzkill/Projects/quake-anthology`.

Owned files:

- `src/app/application/native_q3_wire.c`
- `src/app/application/native_q3_wire.h`
- `src/app/application/native_q3_wire_state.c`
- `src/app/application/native_q3_wire_state.h`
- `src/app/application/presentation.c`

No descendants were used. The active gate permits reading, editing released source, hashes, and whitespace inspection. Do not run builds, compilers, tests, games, project scripts, parsers, generators, or executable validation. Do not edit other lanes' files. Freeze exact files and obtain independent whole-source review before handing a packet to root.

## Latest exact frozen packet

Manifest: `/tmp/qa-native-q3-wire-world-cut-resume-20260930.sha256`.

Manifest SHA256: `e8bf55bceed7b9e61a5729ccf0d141b05f12547bcb52ef6be29000b185afc230`.

| File | SHA256 |
| --- | --- |
| native_q3_wire.c | 2b0a60596ea2c05bb73238cc834c4ae2752622042ff23b811ab2829c8cc492e7 |
| native_q3_wire.h | 710cd1875b5f25c353c8a42239743acc24935dffaadf3b172efc5441113b453f |
| native_q3_wire_state.c | fd5a374dc1ef4dc457e025e579de89a0fcf75ff221561b345e6281573758cfb2 |
| native_q3_wire_state.h | 9ed0bbb70c18f82c1561fc54062e7e3473f3d2cee65df6c18256d3ee40fd364d |
| presentation.c | 8f818a257b2759de359414114bb34696a4fd2974950869d249fb02b3c1e50136 |

Whitespace inspection of these exact paths passed after final refreeze. `native_source_peer` is the independent whole-five reviewer. The reviewer read the whole prior revision and all donor/source bodies, accepted the UI, map/carry, native128 and score adapter boundaries, and independently confirmed the CS parser defect. Root then reopened the packet for the full CS/BCS repair. The latest `fd5a374d` revision was sent for a fresh whole-five review; its final verdict was still pending when this handoff was requested. Do not apply the historical `0352485f` verdict to this latest revision.

The previous accepted client-cycle packet has a separate historical manifest `/tmp/qa-native-q3-wire-client-cycle-resume-20260930.sha256`, manifest SHA `2ab7cd793e37ce382deed5ca5252cca9ebe81b05bed184385ed61f35ddc31133`. `q3_settings_owner` completed whole-five source review on that earlier packet. It is not the latest world-cut freeze.

## Real ownership and namespaces

The native wire holder belongs to the actual builtin Q3 GAME provider and its constructor's genuine `qa_world *source_world`. Detached construction must never take the old live `application->world` instead of that constructor world.

Wire clients retain actual physical source slots, complete actor generations, engine userinfo, last raw engine command, reliable histories for their true consumers, local gamestate/snapshot/prediction histories, entry clock, and pending DROP requests. They do not own gameplay PS, entity fields, sessions, ranking, movement, combat, inventory, or source event rings. Those remain with the true GAME/shared selected owners.

Only real bots or concrete original CGAME reader leases retain native reliable queues. `gclient.local_client`/`ip==localhost` is not a reader qualification. UI has a lifetime/source lease but no reliable or snapshot reader. Remote reliability belongs solely to the actual network channel; do not mirror its sequence, ACK, or history into the native source holder.

`application_native_q3_wire_client_admission_read` permits the actual newly bound client during a round cut, but rejects pending DROP and stale full source binding. Stable `client_read` rejects `round_pending`. Fixed-slot `source_userinfo_read` preserves real userinfo after disconnect; a never-connected unadmitted slot returns the genuine engine constructor's empty string. It does not fabricate an actor or admission.

## Native source PS/entity/visibility dependency

The final enclosing helper packet is frozen and independently SOURCE ACCEPTED by `movement_source_peer`:

Manifest: `/tmp/qa-q3-wire-producers-native4-20260930.sha256`.

- `include/qa/game_q3_wire.h`: `89a3b36dcfe14b251b5fb7b0bb40e09235686e920a8430058826d48e73a8d7bb`
- `src/gameplay/q3/source_wire.h`: `0c4d68eada8cd10cc0e63242578e3a891fcfc42a0db71986ac07524f3d1f3374`
- `src/gameplay/q3/source_wire.c`: `34f9a303d52fb418713204b0580c941e94fb73e7f73bfacd8bdc9ffb4d8bc30f`

All three hashes were rechecked against this manifest. This is helper acceptance; actual GAME constructors, source END/FOLLOW/control callers and the enclosing GAME codec are separate dependencies.

Public `qa_q3_wire_mode` now contains only genuine selected match `score`. The native adapter no longer reads mode telemetry/session mirrors for rank, team, defend/assist/capture counters, or Harvester tokens. Source PS reads the true native GAME fields: `rank`, `persistent_team`, `generic1`, `defend_count`, `assist_count`, and `captures`. A genuine source END follow-copy holder is read before the normal PS builder; selected body/inventory authority stays with the follower's actual owner.

Native observation uses `qa_q3_wire_native_visibility_read`, whose cluster array has 128 entries. It reads the actual current published linked bounds, calls the real 128-leaf query, retains all unique nonnegative clusters, the first two unique areas including `-1`, and `last_cluster=0`. Original engine link/entity visibility keeps its separate SDK 16-cluster/overflow policy. The native selector consumes the new128 result; original guest selection retains16.

The native observer's area-connectivity context reads the actual native scoped live `cm_noAreas.number`. Nonzero makes connectivity true; otherwise negative areas are false and real geometry connectivity is queried. This must not mutate shared collision geometry or its area-bit policy. Original guest context has no native cvar pointer. Native current_view qualifies the real scoped registry/cvar before selection.

Actual source constructors must write/mark ready their physical entity row. Baseline/snapshot iteration first reads `source_binding.in_use` and skips inactive physical rows. Do not synthesize zeros for incomplete active producers.

The selected movement binding uses actual `application_control_q3_flags` and `application_control_q3_policy` writers. Policy writes only authored PM fields and can update the currently borrowed real Pmove kernel state. Foreign selected movement uses GAME's genuine native PM backing, rather than reconstructing a Q3 movement state during observation.

## Clock, raw command and bot observation contracts

`application_q3_wire_time` reads the actual provider's completed Q3 source clock. After real frames it requires `QA_FRAME_EXIT`; milliseconds are the low32 bits of `time_ns/1000000`. Native CGAME presentation uses the true GAME time through its actual source lease. UI uses real presentation milliseconds.

`application_native_q3_wire_command_seed(provider,slot,raw,error)` is a strict connected pre-Begin lifecycle seed. It updates the actual last engine command and received marker without adding prediction history, increasing its ordinal, or invoking Think. `network.c` has the genuine first-enter caller before canonical Begin.

`application_native_q3_wire_command` admits only genuine raw Q3 intake and appends the true engine command history. It rejects pending DROP/round suspension. `control_frame.c` now calls it from raw source-domain intake together with `qa_q3_client_received_command`; no observed movement is used to reconstruct input. Actual synchronous ClientSpawn/G_RunClient continuations do not append engine input history. The CMD_BACKUP64 reader accepts command0 and negative numbers inside the real zero-initialized retained window, rejecting future or expired numbers.

Native builtin bot AI has an actual per-producer-frame `bot_cycle_begin`/`bot_cycle_end` lease. Begin qualifies real source frame fields, host time, GAME/world/source owner; it clears each bot's selected physical-number cache once. The AI snapshot callback selects current native source visibility lazily and retains only selected physical numbers, not copied transport state. `bots_resume` wired begin/end around the actual native `qa_bots_frame` producer, with end on every success/failure. Original loaded GAME SDK host observation may legitimately use its prior transport snapshot and is a separate source caller. Bot source readiness and final caller review remain external holds.

## Actual original CGAME/UI source leases

The wire client lease owns/guards the actual native source provider's physical lifetime. It records true receiver owner, seat, source slot, role-owned command arguments, and whether the borrower is an actual CGAME reader. Role executor and fallible host destruction happen before unbind. GAME deconstruction must preflight `wire_destroy_ready`, including outstanding role leases, before destroying IP/settings/votes/TEAM services. Ordinary `wire_idle` checks calls, not live lease count.

Pure `client_topology_read` returns exact actual source provider pointer, source owner, receiver, seat and physical slot. It works during detached pre-source construction and restore. Actual original roles retain `client_source` pointer and `source_owner` in addition to the native lease. Pure arguments also remain usable before gameplay qualification.

The shared `leased_client` runtime helper requires admitted full native source binding, genuine Begin, nonbot, nonDROP, exact seat, and no round cut. All gameplay/snapshot/time/client effects use that gate. Constructor binding may occur before Connect/Begin/GS; getters and role Init may not.

Native client Command routes through the leased actual GAME source actor, not the CGAME-only provider's empty GAME mirror. SystemInfo and server-command effects call the actual app receiver/seat effect once. Text is copied before external callbacks; callback leases and full source actor/revision checks reject retirement/replacement before continuation. Effects must use exact CGAME-owned cvars/context, not native GAME/global fallback.

The native role caller14 was separately source accepted; its exact manifest is owned by `native_resume`. Source-aware UI retirement must call UI Shutdown while old source actors/commands still live when source provider pointer or seat-slot topology changes. Consumed primary-CGAME descriptors must rebuild before local source publication; role Init waits for real physical Begin and GS.

## UI-only gamestate and current configstrings

The latest wire publication distinguishes `gamestate_needed` and `snapshot_needed`.

- A concrete UI-only lease receives a genuine initial gamestate after true Begin.
- UI-only publication creates no snapshot ring and no reliable queue.
- Adding an actual CGAME reader later refreshes the actual GS before its first snapshot.
- Actual CS mutation directly updates current GS for genuine UI-only leases, after source CS revision/text qualification. CGAME continues its real reliable command and SystemInfo effect ordering.
- Source CS callbacks use actual committed slot revision before and after every single/multipart send. Same-live-owner supersession stops old publication benignly; retirement/send failure remains failure. Owned text and recipient full-generation/revision qualification cover all requested recipients, including remote-only output recipients.

`presentation.c` skips native UI/CGAME draw, menu admission and held-key release until the actual source GS capability is available. It does not create disconnected source rows. UI-only initial publication and later current CS are now real producers on disk; whole latest-five review remains pending.

## CS/BCS decoder and private wire version4

The latest native decoder now matches the actual TS `network/q3/client-server-command.ts`, `core/numeric.ts:194`, and SDK `cl_cgame.c` contract:

- Recognize only exact `bcs0`, `bcs1`, `bcs2`; `bcsExtra` is an ordinary command.
- Missing fragment is empty, extra arguments are ignored.
- `bcs0` stores the raw authored `cs <argv1-or-empty> "<argv2-or-empty>` string with the actual snprintf8192 truncation. It does not parse an index yet.
- `bcs1/2` append to the constructor-empty buffer or its retained prior complete value; there is no synthetic active-phase gate.
- The full8192 bound includes prefix, final quote and NUL, matching the actual donor continuation arithmetic.
- `bcs2` appends the quote, tokenizes the actual complete buffer, and uses that actual completed text for subsequent effects.
- A real final `cs` uses saturating ASCII nativeAtoi of argv1-or-empty. Nonnumeric prefixes map to0, digit prefixes such as `5junk` map to5, and invalid final indices fail at the genuine CS stage.
- CS value is the exact joined argv2 onward, obtained from the real tokenizer's owned `args_text` after skipping argv1 plus its separator. Empty and extra argument forms are valid.

Removed duplicate `big_configstring_index` and `big_configstring_active` fields. The native private wire format is now version4. It serializes only the actual retained whole string, length, allocation presence, and exact8192 storage boundary. Arbitrary pending raw prefixes/no-start/prior complete buffers are legitimate source state; the codec no longer prematurely requires numeric prefix or final quote. No wire public capture/restore/finish signature changed. QAN3 treats it as opaque payload.

The **separate original guest** parser is on disk and aligned, but its codec is incomplete at the handoff. `native_resume` reports original `clients.c` hash prefix `357c63e4` in `/tmp/qa-native-next-context-parser-draft8-handoff-20260930.sha256`, frozen and unreviewed. Its private/save fields still have synthetic numeric index/active metadata and old strict validators. A normal pending nonzero bcs0 can fail save until the next session removes those fields/validators and bumps original `QAG3ST2` to3. Do not claim original decoder/codec parity or acceptance. Root had not yet granted its save writer when the handoff freeze arrived.

## Native DROP and reliable overflow

Root explicitly confirmed native TS behavior: engine.dropClient authors a real source-visible retained request; actual coordinator later performs cleanup. This is not SDK SV_DropClient's separate synchronous GAME_CLIENT_DISCONNECT contract. Do not flip source pers flags early or reorder source predecessor effects to imitate SDK synchronous retirement.

The actual retained queue overflow advances source sequence65 without overwriting its64 retained history entries, transfers a preallocated real DROP reason and marks pending. It does not abort broadcasts to other recipients or create an event-history mirror. Pending runtime capability/retention/console/snapshot reads are rejected. The pending reason/sequence65 is valid only with the actual request in the private codec.

The proper consuming order is one rankings disconnect, actual bot shutdown while GAME/wire/source actor remain live, actual optional transport callback, true native source Disconnect effects/unlink/session/CS/rank/wire cleanup, then canonical retirement. `q3_clients_resume` found its earlier drain called ranking twice and transport before bot shutdown, and is consolidating one private disconnect body with an optional drop_transport flag. That external repair is not yet frozen/reviewed here. Native source Disconnect itself must preserve real StopFollowing, teleport event, items/cubes, log and tournament effects before source retirement.

Transport DROP copies its reason, marks callback entry before a fallible external call, retains physical wire, and postqualifies GAME/row revision/full actor/source binding. Delivered callbacks must not replay on cleanup retry. Wire map/carry cuts reject pending DROP until the actual safe old-source drain consumes it.

## Fast restart, ordinary map and full replacement

Fast `round_begin` cuts old actor bindings and begun markers but preserves local GS/snapshots/prediction history until the actual map_restart command is consumed. It toggles server-count bit4 once. Local/bot restart commands are retained for their real readers; remote network channels own their own reliable command/ACK without a mirrored source counter. Actual new Connect(false)/Begin must rebind genuine fresh GAME rows. Strict fast `round_finish` requires every retained client genuinely Begun.

New ordinary `map_begin` applies to the attached same native GAME/same actual world pointer at an idle source cut, after actual old source/rank/bot shutdown and safe DROP drain, before canonical actor retirement. It preserves real engine userinfo, reliable/ACK/consumed command history, last raw engine command/received flag, and entry time. It clears old actor bindings, GS, snapshot entities/rings, selected bot map, local prediction history/ordinal, server id, weapon and sensitivity; marks a replacement cut and toggles bit4 once. It does not call ClientDisconnect between levels.

`map_finish` uses genuine `carry_finish` source connection qualification and permits real connected pre-Begin remotes; local/bots complete actual Begin. Source owner confirmed `qa_q3_maps_reset` retains constructor-owned max_clients, so this applies to unchanged capacity. Changed sv_maxclients needs a fresh true full replacement/constructor, not fake wire resizing.

Full replacement initially imports a pure captured old carry into a detached fresh candidate before retirement. New `carry_refresh(candidate,final_carry,error)` replaces that prepared transport with the final old capture after actual old Shutdown/rank/bot callbacks and safe DROP drain, while old source actor identities still exist. It requires imported replacement_pending/round_pending candidate, no actual Connect/Begin/GS/snapshot publication, no callbacks, same app/registry, genuine capacity and human seat/role topology. Actual role leases and frontend/services/world identity are preserved. All fallible decode/qualification completes before replacing clients. Final source bit is `final_old_bit ^ 4`, rather than toggling candidate twice. Checkpoint restore retains its saved bit unchanged.

`q3_round_resume` landed the final replacement hook in mutable callers: `publication.c` after retire_map_services while old routing/actors are live calls native clients_drain then `application_q3_world_restart_shutdown`. The latter reconciles the final real wire inventory against the prepared roster, marks genuinely dropped rows retiring, updates actual userinfo/bot/raw command, captures final scoped cvars and wire, then invokes detached carry_refresh. New clients or generation changes outside the prepared roster fail honestly. Final cvars import occurs attached before real source settings registration/Init. Final carry_finish follows genuine new admissions/publication.

Those new caller mutations still need their exact freeze and whole caller peer review. At last inspection ordinary map_begin/map_finish had no actual call sites yet; `q3_round_resume` was still adding that same-provider publication cut. Do not call the ordinary transition or final refresh fully integrated until the actual caller packet is frozen and accepted.

## Settings and persistence domains

Native GAME owns its real scoped engine cvar registry. Independent source vmCvar cache QAGC can lag live cvars and must preserve its actual cached continuation. IP filters QAGI, votes QAGV, TEAM capability, native GAME state and native wire each have real separate owners. Do not infer source settings cache from the registry at observation or reconstruct gameplay from wire records.

I completed independent whole-source review of the final save5 file and its new helper/factory/finish graph:

- Manifest `/tmp/qa-application-persistence-source5-20260930.sha256`.
- `src/app/application/save.c` SHA `37d41b11415218708a324324e09b67e3d1c6730d35e6d346695661d346ec7478`.
- Whole1640 lines read, repeated hash and exact-path whitespace matched.
- QACTRLS5/schema5 raw-domain/time and prepared-body-base bindings inspected in actual control_frame codec.
- Native QAN3v2/header56/schema3 includes genuine QAGV and TEAM presence/bound markers; imports GAME, cache, IP, votes, wire in dependency order.
- Saved-bound-only TEAM reconnect happens late after source/shared foundation/settings and before wire_finish.
- Final shared and full-provider byte recapture remains immutable after all reconnect/validation.
- Actual coordinator orders CONNECTIONS after CVARS/before COMMANDS, MEDIA before application finish/reconnect.

Bound whole-file source acceptance was sent to root/persistence. External current Q3 private9/raw7/map5 enclosing codec, movement CONTROL5 whole caller peer, and seven external frontend record imports remain separate holds. New native wire private4 fits the unchanged opaque QAN3 wrapper. Original guest codec repair is a distinct outstanding dependency.

Actual source_shutdown_admitted is retained in the application lifetime: fresh successful ordinary application true; isolated restore/baseline candidates false; final nofail publication grants candidate true and displaced false. Healthy source shutdown precedes composition/providers and final destroy latch, so real bot exit callbacks can use live source wire. Do not run source Shutdown for inert candidate disposal or repeat consumed callbacks.

## Remaining holds and next work

1. Obtain the whole latest-five `e8bf55bc` verdict from `native_source_peer`. A source review of `0352485f` is historical; latest CS/BCS codec changes require fresh exact review.
2. Root controls release of these frozen five. Do not edit them first.
3. Close the separate original guest private/save continuation mismatch: remove synthetic numeric/active metadata and validators, version QAG3ST3, obtain whole original packet review.
4. Obtain exact frozen ordinary map + final replacement refresh caller packet from `q3_round_resume`; source-review actual shutdown/drain/canonical cut/real admissions/new local publication ordering.
5. Finish actual one-body native DROP drain ordering with `q3_clients_resume` and its independent caller peer. Preserve native TS asynchronous coordinator boundary.
6. Close native GAME constructors/END/control/FOLLOW and enclosing source private9/raw7/map5 reviews; helper4 acceptance does not substitute for them.
7. Obtain exact movement raw CONTROL5/current prepared-body/source capabilities and bot native cycle/setup caller acceptance. Original/custom bot paths remain separate.
8. Close real frontend q3_client_effect factory and exact CGAME console/cvar/command context helper with `frontend_resume`/`native_resume`. Check actual receiver owner+seat+source lease, no GAME/global fallback. Frontend SystemInfo, prediction restart, levelShot and disconnect effects must retain proper real source lifetimes.
9. Complete seven external frontend persistence imports and final fresh recapture; save5 source acceptance does not imply those new callers are complete.
10. Report source acceptance honestly. No builds, game execution, tests or whole Q3 parity validation have run.

Current lanes: root integration; native_resume original roles/parser/codec; native_source_peer this five; q3_source_resume GAME pools/sessions/codec; q3_wire_producers accepted helper4; q3_clients_resume ClientConnect/Begin/Think/Disconnect/drop; q3_round_resume publication/map/restart; movement_resume raw source controls; bots_resume bot cycles/adapters; network_resume physical/native host/seed; frontend_resume effects/persistence; persistence_resume save.

