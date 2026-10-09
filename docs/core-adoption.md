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
| Body | `include/qa/world.h:7`, `src/world/entity_internal.h:12`, `src/world/body.c:340,351,369,400`. Application controls, QC, owned native Q2/Q3 hosts and Unified prediction use the shared world. | Legacy prediction bypasses remain in `src/app/frontend/remote_q1_prediction.c:60`, `remote_q2_prediction.c:56` and `src/network/q3/prediction_scene.c:363`. Project received solids and poses into common linked bodies, retain caller trace/order rules and delete all three independent scan/merge paths. |
| Body field selection | `src/world/live_fields.c:96` decodes live external backing. | Five-field selection/update is copied in `src/app/application/guest_q3_component_records_calls.c:45,121` and `native_q2_records_calls.c:82,303`. Resolve the external field kind into one common typed selector; retain module encodings at the boundary. |
| Current player movement and view | `include/qa/movement.h:136` is the common movement type. Actor-indexed controls in `src/app/application/internal.h:158` own the selected movement and completed view/bounds/ground/water. | Q2 retains the same selected data in `include/qa/game_q2_wire.h:20`, stored at `src/gameplay/q2/internal.h:95` and recopied in `wire.c:665`. Q3 duplicates movement/view fields in `include/qa/game_q3.h:135` and bridges them in `src/app/application/control.c:3079` and `src/gameplay/q3/player.c:2083`. Remove duplicate current-state custody, preserving genuinely source-specific mixed-role PM fields and exact ABI projections. A complete common PlayerState owner is still open. |
| Player roster, life and travel | Common roster is `src/app/application/map_players_private.h:40`; combat and inventory have shared owners. | Generic view/life/connection fields coexist with rule tails in Q1/Q2/Q3 player records. Q2 score mirrors are `src/gameplay/q2/player/state.c:440,498`. Common travel combat is copied alongside Q2 carry health in `map_players_private.h:13` and `include/qa/game_q2_player.h:166`. Move current generic fields to their common owner; retain prior coop/spawn history and original rule tails. |
| Item identity and inventory | `qa_item_id`, `qa_item_definition`, `qa_inventory_entry` and `qa_item_bit` in `include/qa/inventory.h`; one definition lookup in `src/gameplay/inventory.c:1020`. QC/QVM/native Q2 use the same item-bit type; their three former definitions are deleted. Equipment presentation at `src/app/application/equipment_presentation.c:305` reads the selected item through that lookup. Its catalog copy, scan and allocation are deleted. | No alternate selected-equipment definition lookup remains in this caller. Other current item consumers remain subject to the full custody audit. |
| Weapon request state | `qa_weapon_request_status` in `include/qa/equipment_weapon_slot.h:10` now serves equipment and QVM requests/status/cancellation/checkpoints. The duplicate QVM enum and identity translation are deleted. | Original game weapon phases and foreign ABI constants remain rule data. Other common weapon custody still needs the full caller audit. |
| Damage | One actor-indexed combat store in `src/gameplay/combat_internal.h`, and dispatch in `src/gameplay/combat.c:984`. Built-in, QC, QVM and owned-native callers enter `qa_combat_apply` / `qa_combat_run_source`; foreign actors bind that same store. | No alternate engine damage store or dispatcher found in the current caller audit. Q1/Q2/Q3 authored damage/armor arithmetic remains distinct original rule policy. This source audit does not prove every foreign authored instruction's gameplay. |
| HUD state | Shared `qa_hud` in `src/ui/hud.c`. Original layout interpreters supply each game's presentation rules. | Q3 centerprint has separate storage at `src/presentation/q3_native/hud.h:50` and mutation at `hud.c:84`; Unified uses shared HUD but discards Q3 position/width. Move all centerprint callers to one per-seat store with caller layout/reveal/fade policy, then delete Q3's copy. |
| Cvars | One common table and canonical/alias definitions; current handle migration is recorded under THE-2859. Original Q3 trace/contact policy now reads two handles bound at `src/compat/q3_host/host.c:317`; its named reads at `game_spatial.c:34` are deleted. | Fixed-name hot callers remain in `remote_q1_camera.c:12`, `unified_player.c:872` and `frontend/tools.c:518`. Native ABI shadow refresh also needs its bound-handle/changed-row adoption. A valid lookup is a plain canonical-name/alias lookup, with no added context or validation layer. |

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

## Geometry and live entities: THE-2873 / THE-2861

| Capability | Canonical implementation and migrated callers | Remaining bypass |
| --- | --- | --- |
| Const geometry and caller scratch | `include/qa/collision.h:89,103,120`; `src/world/collision/geometry.c:9,68,404,422`. World, frontend, host, prediction, effects and navigation callers pass their load-owned scratch. | None found in the query caller audit. Mutable world/portal updates remain serial. |
| Q1/Q2/Q3 kernel scratch | `src/world/collision/q1.c:99,571`, `q2.c:59,122`, `q3.c:34,89`. Continuations, expanded planes, clipping caches and marks are in caller storage. | Loaded brushes and patches contain no query visit marks. |
| Canonical contents and surface bits | `include/qa/collision_bits.h`, `src/world/collision/contents_bits.c:48,67,83,145,156`; loaders and live-field boundaries import canonical bits. Module/protocol/save boundaries export original representations. | Old pairwise contents converter and callers are deleted. |
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
