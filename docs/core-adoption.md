# Core primitive adoption

This is the implementation work list for the owner's 17:4x directive on
2026-10-09. Complete the primitives and migrate their callers before resuming
the event-system migration. No checker extensions are part of this work.

The initial source audit is against `73c9ffbe`. A row with remaining work is
not complete. Protocol records, foreign module memory, immutable snapshots,
prediction history and game-specific rule state are distinct from a second
authoritative engine store; preserve their required fields and numeric rules.

## Common types: THE-344

| Capability | Canonical definition and current adoption | Remaining implementation or caller migration |
| --- | --- | --- |
| Entity identity and lifetime | `include/qa/actors.h:12,43`, `src/world/actors_internal.h:9,16`; created by `src/session/session.c:308`. Q1/Q2/Q3 rule attachments and QC/QVM/native bindings use this registry. Q1 frontend actor lookup at `src/app/frontend/remote_q1_client.c:237` now reads this source index; its separate actor type, array, scans, growth and cleanup are deleted. | Native rule-tail allocations are also allocation-gate work; their original rules remain. CLIENT ownership and saved actor references remain canonical lifecycle data. |
| Body | `include/qa/world.h:7`, `src/world/entity_internal.h:12`; application controls, QC, owned native Q2/Q3 hosts and Unified prediction use the shared world. Q2 CLIENT publication and prediction now use it at `src/app/frontend/remote_q2_prediction.c:56,173`; their separate scan/merge/contents paths and scratch are deleted. Registry release forwards canonical body cleanup before observer reuse at `src/world/actors.c:317`. | Q1/QW and Q3 CLIENT also use the common query loop with original caller merge/pose data. Their independent entity trace/contents loops and scratch are deleted. Q3 trigger overlap keeps its single-model narrow phase. Exact source, byte parity, concurrency and pinned query evidence are in `docs/playtests/2026-10-09-shared-client-query.md`; snapshot storage and cold restore/validation remain outside this query slice. |
| Body field selection | `include/qa/world.h:12` defines one `qa_body_vector_kind` and inline accessor. QVM, native Q2 and declared QC bindings resolve to it at load; their five-way getter/setter chains and seed copies are deleted. `src/world/live_fields.c:96` retains external scalar decoding. | Module encodings and ownership remain boundary rules. The bounded comparison and joint engine-build evidence are in `docs/playtests/2026-10-09-body-field-selector.md`; this is not whole-module gameplay or a frame-time speedup. |
| Current player movement and view | `include/qa/player_state.h:9` defines the common current owner, stored once per actor in `src/app/application/internal.h:161`. All callers use `qa_player_state`. Q2 and Q3 borrow selected controls/body through the common owner and in-flight continuation at `control.c:2333` / `control_frame.c:1535`. Q3's ten generic fields, phase copies and full-movement bridge are deleted. | Foreign/inactive Q3 policy, original rule clocks, immutable checkpoint/history and ABI projections retain their required fields. Bounded byte, build and private stationary-gameplay evidence is in `docs/playtests/2026-10-09-q3-common-player-custody.md`; real input/audio and full application cold reconstruction remain open. Common roster/life custody remains in the next row. |
| Player roster, life and travel | Common roster is `src/app/application/map_players_private.h:40`; combat and inventory have shared owners. | Generic view/life/connection fields coexist with rule tails in Q1/Q2/Q3 player records. Q2 score mirrors are `src/gameplay/q2/player/state.c:440,498`. Common travel combat is copied alongside Q2 carry health in `map_players_private.h:13` and `include/qa/game_q2_player.h:166`. Move current generic fields to their common owner; retain prior coop/spawn history and original rule tails. |
| Item identity and inventory | `qa_item_id`, `qa_item_definition`, `qa_inventory_entry` and `qa_item_bit` in `include/qa/inventory.h`; one definition lookup in `src/gameplay/inventory.c:1020`. QC/QVM/native Q2 use the same item-bit type; their three former definitions are deleted. Equipment presentation at `src/app/application/equipment_presentation.c:305` reads the selected item through that lookup. Its catalog copy, scan and allocation are deleted. | No alternate selected-equipment definition lookup remains in this caller. Other current item consumers remain subject to the full custody audit. |
| Weapon request state | `qa_weapon_request_status` in `include/qa/equipment_weapon_slot.h:10` now serves equipment and QVM requests/status/cancellation/checkpoints. The duplicate QVM enum and identity translation are deleted. | Original game weapon phases and foreign ABI constants remain rule data. Other common weapon custody still needs the full caller audit. |
| Damage | One actor-indexed combat store in `src/gameplay/combat_internal.h`, and dispatch in `src/gameplay/combat.c:984`. Built-in, QC, QVM and owned-native callers enter `qa_combat_apply` / `qa_combat_run_source`; foreign actors bind that same store. | No alternate engine damage store or dispatcher found in the current caller audit. Q1/Q2/Q3 authored damage/armor arithmetic remains distinct original rule policy. This source audit does not prove every foreign authored instruction's gameplay. |
| HUD state | Shared `qa_hud` in `src/ui/hud.c`; one fixed `qa_hud_center_state` in `include/qa/hud.h:17` serves local, legacy and Unified producers. Q3's separate store/API/validator are deleted; source-owned rendering suppresses duplicate common output. Original layout interpreters retain game presentation rules. | Actual component drawing, full builds and bounded private gameplay are recorded in `docs/playtests/2026-10-09-shared-centerprint.md`. Live centerprint screenshots remain unproved by that stationary integration check; other generic HUD custody remains subject to the caller audit. |
| Cvars | One common table and canonical/alias definitions; current handle migration is recorded under THE-2859. Original Q3 trace/contact, Rogue team-face, tools debug width, QW camera/skin/sound, compiled Q1 policy, original QC fixed policy and native ABI shadow refresh use common handles/revisions. Their named hot reads or unchanged rewrites are deleted. Evidence is in the QW and common-cvar playtest records. | Fixed Q3 source/loading/authored HUD and remaining guest/frontend network readers are being migrated. Dynamic declarations/setters and external text imports remain boundary operations. A valid lookup is a plain canonical-name/alias lookup, with no added context or validation layer. |

The item-type comparison checks actual request/status/cancel and inventory
behavior, including high/private bits, signed counts, stale receipts and stored
numeric layouts. GCC, Clang and both sanitizer pairs produce identical output.
It is a component proof, not a full guest execution or checkpoint round trip.
Production, ASan/UBSan and allocation-gate builds and all seven core checks in
each pass for the isolated ten-source-path item-type slice. The full checklist
above remains open; this does not close THE-344 or whole-frame allocation.

The Q1 source-index migration admits missing actors through the existing CLIENT
boundary and retires prior-map actors once at loaded SETVIEW. Actual-source
components cover busy receive, generation reuse, retired metadata and actor
checkpoint references. Three engine builds and seven core checks each pass.
Private copied-profile CPU runs show classic/rerelease start, retail NetQuake
demo playback and the menu; all quit normally and their final images were
reviewed. This does not prove every legacy protocol or a frame-time speedup.

The Q2 custody slice preserves exact classic/rerelease save and packet fields
in actual-source component comparisons, including foreign movement with the
same dialect. All three engine builds and seven core checks each pass. Private
copied-profile classic/rerelease and rerelease-with-Q3-movement CPU runs reach
visible gameplay and quit normally; root reviewed the world/weapon/HUD images.
Original-module gameplay, real input/audio and full save round trips remain
unproved by those checks. Existing positional checkpoints consume the deleted
store's former bytes without reinstalling a second current-state owner.

## Geometry and live entities: THE-2873 / THE-2861

| Capability | Canonical implementation and migrated callers | Remaining bypass |
| --- | --- | --- |
| Const geometry and caller scratch | `include/qa/collision.h:89,103,120`; `src/world/collision/geometry.c:9,68,404,422`. World, frontend, host, prediction, effects and navigation callers pass their load-owned scratch. | None found in the query caller audit. Mutable world/portal updates remain serial. |
| Q1/Q2/Q3 kernel scratch | `src/world/collision/q1.c:99,571`, `q2.c:59,122`, `q3.c:34,89`. Continuations, expanded planes, clipping caches and marks are in caller storage. | Loaded brushes and patches contain no query visit marks. |
| Canonical contents and surface bits | `include/qa/collision_bits.h`, `src/world/collision/contents_bits.c:48,67,83,145,156`; loaders and live-field boundaries import canonical bits. Module/protocol/save boundaries export original representations. | Old pairwise contents converter and callers are deleted. |
| Entity pose rules | `qa_collision_target.pose_rules` and the single basis/normal/link implementation in `src/world/collision/contents.c:19,25,35` supply original entity-role policy independently of geometry and caller contact/merge rules. All current target/link callers migrate. | The old kernel-owned pose bodies and link expansions are deleted. Current mixed-role parity and loaded-geometry serial/concurrent proof pass. Renderer leaf traversal remains at `src/render/scene/world.c:768,811,986` and `world/legacy/lighting.c:603`; migrate it with retained model geometry before declaring full leaf adoption. |
| Visit marks | One `qa_stamp_set` in `include/qa/stamp.h:16`. Q2 trace/expanded, Q3 query/model load, common leaves, render PVS/source/surface and bot routing use it. | Eight historical visit-mark implementations are deleted. Retained render epochs refer to the common set. |
| Bound native fields | `src/world/live_fields.c:15,96,129`, `src/world/body.c:369,499`; native Q2/Q3, QC and broad-phase callers use live owned backing. | No copied authoritative candidate cache, per-candidate IPC or descriptor re-resolution found. Dynamic finite/bounds/solid checks remain at `body.c:377` and `live_fields.c:143,163,193`; external ABI staging is separate. |
| Native process backend | `src/compat/native_host/process_resources.c` and `src/compat/native/process.c`; SDK and shipped native modules share the owned backend. | Legacy pipe runner deleted. Q3 ABI staging at `src/compat/q3_host/memory.c:155` is not a broad-phase cache. |
| Geometry-derived leaf membership | `src/world/body.c:640`, `src/world/collision/geometry.c:524`; nine leaf consumers use one sealed visibility workspace and caller scratch. | Old returned-array APIs and query reallocations are deleted. Mutable actor membership remains single-owner state. |

Retained proofs are in the canonical-collision, caller-owned-trace-scratch and
shared-leaf-visibility playtest records. Public serial/concurrent components
compare 5,334,908 bytes and 36,864 complete samples per build. Later shared-leaf
components cover the changed helper. Current world/native query inputs match
the final measured SDK build. All eight final native broad-phase phases retain
identical 256-byte trace/touch output; candidate classic medians are
8.460/8.610 microseconds and rerelease medians 8.570/8.580 microseconds, below
the 9.6-microsecond limit. Public kernel timings are broadly unchanged; the
recorded pooled Q3 result is 1.10% above its pair, so they do not prove a speedup
or literal improvement in every phase.

Original Q3 trace cvar policy now reads retained handles. Actual shared-store
comparisons preserve masks, defaults, live edits, Source recreation and table
lifecycle across five dialect views. All three engine builds and seven core
checks each pass. This removes two fixed-name queries per original trace;
it is not a trace-timing result or closure of the remaining cvar callers.

## Whole-frame allocation: THE-2874 / THE-873 / THE-882

One arena (`src/core/arena.c`) and fixed pool (`src/core/pool.c`) supply reserved
storage; sealed overflow returns failure and records exhaustion. Actor pages,
spatial snapshots, visibility scratch and several scene/native workspaces are
load-sized. These component migrations do not establish a zero-allocation frame.

The current diagnostic observes engine/static-object allocation calls across
threads inside `qa_frontend_step`. DSO/libc internals, other allocation APIs,
outer save drain, pacing and shutdown are excluded. Broaden the observation to
the complete gameplay frame before closing the whole-frame requirement.

Attachment release now uses one load-sized ticket per actor slot in the existing
world snapshot arena. The generic heap child array and sorting call are deleted;
nested releases and slot reuse preserve captured order. Actual-source comparisons
record 4,239 allocation/free pairs becoming zero. This is release-path proof,
not whole-frame proof. The unused repository SDK transport operation still
allocates traversal scratch at `src/world/body.c:903`; its declaration and
definition are the only current references.

A temporary call-site census of the `73c9ffbe` diagnostic artifact completed
800-frame private copied-profile runs in all five editions, with 678 measured
steps each. All exited normally, with no null request, size overflow or reserved
capacity exhaustion. Every edition still fails the allocation target.

| Actual remaining caller | Required migration |
| --- | --- |
| `src/network/unified/frame.c:117`, `document.c:353,557`, `session.c:158,163` | Use load-sized common arena/pool frame and delivery custody; delete the heap fallback on the gameplay path. Preserve retained payload lifetime and acknowledgments. |
| `src/app/frontend/remote_unified_render.c:69,319,361` | Reuse retained presentation records and interned identity fields; stop cloning text and UI arrays for each frame. |
| `src/app/frontend/remote_unified_q2.c:194,206` | Retain immutable changed configuration and reuse player records, preserving original status layouts. |
| `src/app/frontend/unified_q3_client.c:192,204` | Retain bounded history and reliable command storage; stop deep-copying 64 command token records on every frame. |
| `src/render/scene/models/images.c:8`, `src/render/material/library.c:204` | Keep admitted image/material handles through model submission rather than creating copied names. |

The census is diagnostic attribution, not a timing comparison. The gate must
reach zero over full-frame runs in all five editions before these issues move
to In Review.

The selected-equipment caller now uses the existing inventory lookup. Actual
inventory component comparisons preserve active/inactive group selection,
mixed-source overlap, missing items, labels, ammunition and weapon status.
Across 600 unchanged publications, its 600 catalog allocations and frees
become zero. GCC/Clang plain and sanitizer pairs, all three engine builds and
seven core checks per build pass. This removes one attributed caller; the
whole-frame requirement remains open.

Native Q3 SystemInfo now retains its reliable configstring revision. Unchanged
frames do no text copying, parsing or cvar writes. Existing video reset and
retained-round admission invalidate it; restored services begin invalid without
adding a save field. Component comparisons cover changed revisions, manual
timescale edits, retry after failure and the actual service save codec. Over
600 unchanged frames, 1,200 allocation/free pairs and 1,200 cvar writes become
zero. Three engine builds and seven core checks per build pass. A changed
revision still copies its text; other frame allocation callers remain open.

## Jobs: THE-2871

One dispatcher is `include/qa/jobs.h:11` / `src/platform/jobs.c:125`. It owns
the engine threads, work claims and completion. MD5 jobs and immediate/queued
raster work call `qa_jobs_dispatch` at `src/render/cpu/raster.c:272,1259,1346`.
CPU and GL fallback skinning call that same adapter. No alternate engine thread
pool or parallel dispatcher remains in the current caller scan. Native module
processes and SDL/library internals are external execution boundaries.

Existing linked-render proofs compare RGBA/depth/stencil, retirement, four
retail MD5 models and rounding modes at 1/2/4/8 participants, with zero warm
dispatcher allocation. The 64-job median/p99 microseconds are 0.220/0.220,
7.790/11.630, 6.670/8.990 and 9.700/22.771 respectively, after 100 warm-up and
600 pinned samples. All six dispatcher implementation inputs remain identical
to the verified slice. These are component overheads, not a full-frame speedup.

## Usercmd: THE-869

`include/qa/input.h:178` defines the common command; `src/input/commands.c:231`
builds it and `:320` converts command spaces. Human seats, Unified input and
both bot paths use the same builder. Original protocol narrowing and module
ABI projections remain boundary operations.

Remaining duplicated conversion/projection bodies are
`src/app/application/guest_qc_input.c:274`,
`src/app/frontend/remote_prediction.c:480`,
`remote_unified_prediction.c:251`, `remote_q2_client.c:459`, the three native
Q2 ABI writers in `guest_native_q2_clients.c:311,381` and
`native_q2_client_stages.c:482`, and raw Q3 projections in `control_frame.c:339`,
`arsenal_guest.c:177`, frontend `network.c:6472` and `network/runtime/q3.c:63`.
The QVM scalar/delta-angle adapter now uses the existing converter. Eight
GCC/Clang original/candidate executions, with sanitizer and rounding-mode
cases, compare 6,440,048 command bytes per pair. Its isolated production,
ASan/UBSan and allocation-gate builds and all seven core checks each pass.
This does not close the remaining adapters or the full guest gameplay proof.

The KEX wire API still narrows float angles into `int16_t` in
`include/qa/network_q2.h:16` / `src/app/frontend/remote_q2_presentation.c:403`.
Preserve rerelease floats at that boundary while keeping classic short widths;
existing byte comparisons do not close this precision gap.

## Interned names and per-role rules

One string table implementation is `include/qa/strings.h` /
`src/core/strings.c`. Snapshot actors, providers, inventory and prediction names
carry existing IDs. Different namespaces resolve only at admission or external
serialization. Rendered labels, QC string operations and console parsing still
need text. Hash-table bucket keys are not content identity.

Q2 inventory prediction now compares already admitted weapon/ammunition IDs
through its existing accessor. The hot string conversion/comparison path is
deleted. Actual definition and inventory components preserve 56 profiles,
3,317 selections and 1,407 retained rows. Their old path makes 3,317 string
reads and 82,507 comparisons; the new path makes none and does no interning.
All three engine builds and seven core checks per configuration pass. This is
bounded ID-adoption evidence, not a frame-time claim.

Confirmed hot identity bypasses remain in
`remote_unified_prediction.c:37`, `remote_unified_render.c:257`,
`remote_unified_media.c:40,116,192,200`, `visuals.c:505,537`,
`src/render/scene/models.c:814`, `native_q3_remote_character.c:20,57` and
`remote_unified_input.c:24`. Move their already admitted identity into IDs or
resource handles; do not replace comparisons with per-frame interning or hashes.
Ordinary provider routing already caches the resolved actor/provider in
`src/app/application/services.c:268`; selector/reconfiguration text is separate.

`include/qa/ruleset.h` / `src/core/ruleset.c` replace the three former identical
movement/console/clock enums and 47 bridges. Each role retains its independent
selection. Numeric/save/command components preserve 125 role combinations and
original widths. This proves the type migration, not completion of every
subsystem's independent rule selection or live combined-mode adoption.

The common game-family type now lives in `include/qa/ruleset.h:4`. The one
rule-set descriptor supplies family through `src/core/ruleset.c:12`; seven
consumers use it, deleting three equivalent local switches. Their existing
invalid-input defaults and independently selected role clocks are preserved.
This does not merge authored Q1/Q2/Q3 policy or close the remaining per-role
selection audit.

## Q1/QC and native cvar adoption: THE-2859

Compiled Q1 binds 25 common handles before spawn in
`src/gameplay/q1/runtime.c`. Q1/QW limits, movement, pause, chat and addon
rules read those handles. The named application callback, its context and
exported admission-limit bridge are deleted. Original QC policy/developer
output uses its existing handle owner. Native cvars are derived ABI objects;
unchanged polls skip guest rewrites. Owning/borrowed string reads share one
scanner, and repeated existing Cvar_Get inputs borrow contiguous backing.

Proof is in `docs/playtests/2026-10-09-common-cvar-adoption.md`. Fixed Q3
source/loading/authored HUD readers remain open. New declarations, setters,
split strings and other native text imports still allocate. Whole-frame
zero allocation is not established.

The shared CLIENT query and explicit pose slice passes all three builds and
seven core checks each. Current native candidate medians are classic
9.460/9.300 us and rerelease 9.230/9.220 us, all below 9.6 us; this paired
baseline is faster, so no native speedup is claimed. Current public geometry
serial/two-scratch output and archived bytes agree in all 18 runs. Query
components improve Q1/Q3 medians about 26%/51%; whole-frame allocation,
renderer leaf admission and full gameplay/interop remain open. See
`docs/playtests/2026-10-09-shared-client-query.md`.
