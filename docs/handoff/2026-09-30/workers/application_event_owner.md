# Application event owner handoff — 2026-09-30

Workspace: `/home/buzzkill/Projects/quake-anthology`. Agent: `/root/application_event_owner`.

The user requested a complete new-session handoff. Implementation is held. The global source gate remains closed: no builds, configure, compilers, tests, scripts, parsers, generators, game/project executables, syntax checks, sanitizers or benchmarks. Only source reads, hashes and whitespace inspection have been performed in these lanes. Do not add descendants or modify CMake. Root owns integration and peer assignment.

## Current frozen player inventory helper

Only these two new files are owned by this lane:

| Path | SHA256 |
| --- | --- |
| `src/app/frontend/player_inventory.c` | `9fc2b5d1b53cb28652eb0f3dfc9bd63c99543105e35c9a14905a2b247db8686a` |
| `src/app/frontend/player_inventory.h` | `fcb8de5104e3bdcdf4fc9e5e24419577372fcea747c15275e45a2b4bfedb94ac` |

Manifest: `/tmp/qa-frontend-player-inventory-20260930.sha256`. Both hashes were checked against the frozen manifest; added-file whitespace inspection produced no diagnostics. No independent source peer has been assigned for this packet. It is **not source-accepted**. The frontend aggregate is incomplete and no final aggregate caller acceptance exists.

Agreed APIs with `/root/frontend_resume`:

```c
bool frontend_players_checkpoint(qa_frontend *, qa_buffer *, qa_error *);
bool frontend_players_restore(qa_frontend *, qa_bytes, qa_error *);
```

The leaf format is `QFPL`, version 1, with exact frontend seat count, dedicated flag and ordered explicit seat IDs. Frontend parent retains input, fonts, seats, HUD owner checkpoints and the aggregate. This lane changed no existing shared owner file. Parent agreed direct access to actual private `frontend_seat` fields in `src/app/frontend/internal.h`; no extra owner accessors were needed.

The implementation captures actual retained per-seat projection fields:

- Full `q2_actor` generation/slot provenance, including references to retired actors, through the genuine session actor-reference codec.
- Every `qa_q2_player_view` field: all five vectors, blend components, FOV, health/armor/ammo, score/flashes/layouts, selected/timer item identities, timer seconds, underwater and spectator flags.
- Three actual `q2_vitals` scalar records, nullable current immutable Health/Armor/Ammo labels, warning flags and maximum values. Current producer icons must be NULL.
- Exact cached `q2_timer_item`, `q2_timer.until_ns` and nullable independently owned `q2_timer_label`. The timer label alias must equal its owned copy and the current timer icon must be NULL.
- Both owned `q2_help_text` strings plus explicit NULL, empty-literal or owned-copy modes for `q2_help_lines`.
- Actual ordered `q2_scores`, score/ping/local/spectator scalars and their one packed copied `q2_score_names` backing allocation. Current producer team pointers must be NULL.
- `q2_view_ready`, `q2_help` and `q2_inventory` flags.

The projector really retains stale vitals after `frontend_player_retire`. Timer expiration/absence clears `until_ns` without necessarily clearing the cached timer item/label. Those values are preserved. Timer labels are not reconstructed from current inventory definitions. Score local/spectator flags are preserved as retained values, not recomputed from current actors. Names retain row order, packed ownership and NUL termination, including empty names.

`q1_monsters` and `q1_monster_label` are excluded: `seats.c` rebuilds them in the HUD data callback before each use. HUD-owned prints/messages and the application's pending event arrays are separate actual owners. This helper never runs player events, source callbacks, console commands, inventory observations, UI/menu actions or HUD draw callbacks.

Capture requires an actual frontend capture lease, an empty output buffer, a valid bounded seat count, no stepping and idle seat callbacks. Each local seat must have its stable actual frontend pointer and ID. Dedicated frontends do not construct or deliver local player projections, as verified in frame/round delivery; their exact dedicated profile is encoded with no local rows.

Import requires an isolated `source_restoring` frontend, no capture lease, no stepping, idle seat callbacks and stable actual seat addresses. It decodes every seat into temporary owned records and requires complete byte consumption before installing any projection fields. Installation calls the actual `frontend_player_retire` cleanup, then transfers the copied allocations and reconstructs aliases into those actual fields. There is no shadow store and no replay. Installation has no fallible operation after full parse; failed parse/allocation frees all temporary timer/help/row/name allocations.

The candidate session is intentionally supplied to source codecs. Item identities decode through actual session strings; read-side interning may change the isolated candidate table before the leaf parse completes. The outer candidate must be discarded on failure and the aggregate must enforce its genuine actor/string namespace and immutable-content contracts. This leaf does not make missing live actors or authoritative gameplay state. Retired projection references are allowed.

Malformed leaf input is bounded by the actual byte extent and native allocation extent. Score count must fit its row allocation and available scalar bytes. Packed name size must leave all score scalar rows, include at least one terminator byte per row, terminate every ordered name and contain no unowned trailing bytes. Help alias modes and text ownership must agree; owned help cannot be empty. Owned text rejects embedded NUL. View/vital floating fields require finite values. Booleans use the strict source codec. Unsupported icons/team pointers, unknown magic/version, seat/dedicated mismatches, reordered IDs, truncation, extra trailing bytes and a ready view without actor provenance fail qualification. Independent review must still assess these contracts and any missing semantic constraints.

Read evidence: complete `player_events.c`, actual seat struct, `seats.c` HUD reader and destruction, `presentation.c` view uses, `frame.c`/`round.c` event delivery, `save_fields.c`, `seat_inventory.c`, `capture.c` lease/callback guards, `src/persistence/source_io.c`, Q2 view/HUD declarations. Donor read: `../quake-typescript/src/app/bootstrap/seat-hud-state.ts` and real `ui.ts` receive/draw flow. The donor retains addressed source HUD state; the helper preserves the actual current C owner's narrower retained fields.

Parent was explicitly told that adding this leaf changes actual PRESENTATION contents: the aggregate schema/inventory must advance and require its component for both local and dedicated profiles. Missing old bytes must not be treated as equivalent to an empty installed projector. Actual aggregate wiring/import order remains parent work, followed by independent whole caller review.

## Accepted QVM foundation

The earlier QVM five-file packet is independently source-accepted by `/root/world_source_link_owner`. Final manifest: `/tmp/qa-qvm-source-call-20260930.sha256`.

| Path | SHA256 |
| --- | --- |
| `include/qa/qvm.h` | `1a2ede34bd8a8f20a4ec24015ad9fa359c77b48a9d7b5701e03a84e9461bd94a` |
| `src/compat/qvm/internal.h` | `d8d99b84ec3c8da176043913147add03521c92e2a55f495932f27d2079881cbb` |
| `src/compat/qvm/execute.c` | `ad2aecbb837529962d89a6afdb1d5dadd5c02d632cc7fbfc7cad84ed615fcf2e` |
| `src/compat/qvm/memory.c` | `87600db6c4f216abb1c50690e6e5a56e86c2863a604c0d5989e45fa96650a97b` |
| `src/compat/qvm/source_call.c` | `390ada33e4f9df076c54e9f9a3d101b05a8b6ff919f1e4153aa7162e36d652f7` |

Public APIs: `qa_qvm_invoke_source_callback`, `qa_qvm_source_scratch_qualify`, `qa_qvm_source_scratch`, and the pure current-token `qa_qvm_call_cancelled` observation. Existing native `qa_qvm_invoke_started` source-attempt edits were baseline in the whole-file freeze and were preserved.

Signed callbacks require the exact admitted VM image and a genuine current active callback token. Nonnegative pointers must be actual ENTER entries and use existing nested invocation. Negative pointers follow original `-1-trap` convention, allocate a real nested argument frame, open genuine HOST_SYSCALL scope and use existing intrinsic/role/ABI dispatch. They do not fabricate a public call token or directly bypass dispatch to options.syscall. Real source ownership, stack and failure/cancellation scopes restore on unwind.

Scratch is derived from immutable align16(data + literal + BSS), with the requested extent below actual image memory size minus 65536. Runtime repeats exact image/token checks and rejects overlap with the paused caller stack. It snapshots the actual bytes and raises the active inherited stack floor. Nested same-span leases restore in reverse order. A reviewer found that nested region/counter evaluation originally lowered the floor; the accepted repair makes default evaluation preserve the inherited floor and rejects explicit reservations below it.

Scratch restoration runs through genuine memory write observation/publication. The private cleanup helper still restores original RAM if delivery allocation/sequence qualification fails, reports that failure, and never claims successful delivery. Original execution failure is latched before cleanup and preserved. Actual active VM lifetime guards prevent destruction/reset during the lease. There are no new persistent VM fields or invented image metadata.

`qa_qvm_call_cancelled` qualifies the current token and reads actual pending cancellation without consuming or changing it. Strict once-only `qa_qvm_cancel` is unchanged. A real nested trace callback may cancel a retained ancestor client envelope; callers must observe pending cancellation to avoid another cancellation and suppress later effects.

Actual production guest reset uses `qa_qvm_restart_original` in `guest_q3_restart.c`; the replacement-image `qa_qvm_restart` has no source/test caller found in the scoped scan. Scratch therefore uses the admitted immutable image extents on the actual body-control path. General replacement restart semantics remain existing public VM behavior.

The independent peer accepted the repaired complete five source files after full module/image/dispatcher/executor/write-observer/lifecycle/donor review, repeated exact hashes and scoped whitespace checks. No executable validation occurred. Root owns subsequent registration and integration status; do not infer runtime acceptance from source acceptance.

## QVM body adapter and unaccepted outer caller context

`/root/q1_resume` owns actual `guest_q3_control.c/.h`. Last independently source-accepted adapter2 manifest seen: `/tmp/qa-qvm-body-control-20260930.sha256`, C `5a1d4654359bb906fcb9eb8cf997980cb05fb44fdd30570d93ea5bcc6ed16d2f`, H `a71d1bd0411edea8841b0a7dccc36f5566c84f3ac905ea9438125ef55a9671a8`. Both this lane and world peer accepted that bounded packet. Check current files before assuming this remains the latest freeze.

The real duck hook proceeds original source, leases 92 scratch bytes, writes original origin at +56 and requested min/max at +68/+80, and calls the actual signed movement callback with seven donor words: scratch, +56, +68, +80, +56, original player+140 client number, original movement trace mask. It reads allsolid and applies actual source bounds. It retains full actor/generation, located tables/strides/slot bounds and both entity->client and movement->player pointers. It observes cancellation around fallible writes, original proceed, callback return and scratch cleanup. No substitute world trace is used.

Two concrete qualification defects were repaired: source pointer retargeting despite unchanged actor/tables, and retirement/retargeting during scratch restoration's actual write observers. A further cancellation-domain defect was repaired: cancelling only Pmove is consumed by its intercept and can resume ClientThink. The adapter now receives and directly cancels the genuine retained client-envelope token supplied by the actual outer input owner. Pending ancestor cancellation remains pending through movement and is consumed at the correct client intercept.

`/root/movement_resume` owns broader `arsenal_guest.c`/control integration (caller20). It reported retaining actual located tables/strides and source player pointers, querying pending cancellation before effects, passing the genuine client-envelope token, installing the seventh real saved descriptor and QAG3IN2 identity. This lane found exact entity_count equality falsely retired valid scopes when source high-water grew; movement reported repairing it to slot bounds with exact addresses/strides/pointers. The whole movement20 packet was not accepted in this lane and remained separately active at last report. Its actual frozen whole-file review, lifecycle assembly and parent aggregate checks must not be replaced by adapter/foundation acceptance.

