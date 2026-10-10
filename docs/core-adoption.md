# Core primitive adoption

This records implemented primitive and event-system adoption. The latest
owner order completes the output ring and system events, then remaining common
types. No checker extensions are part of this work.

Older issue sections retain their own scoped remaining work. A row with remaining
work is not complete. Protocol records, foreign module memory, immutable snapshots,
prediction history and game-specific rule state are distinct from a second
authoritative engine store; preserve their required fields and numeric rules.

## Common types: THE-344

The engine primitives and their current consumers are listed below. Foreign
module ABI records, legacy packet records, immutable save/prediction projections
and authored game rule tails retain their required fields. They do not own a
second selected engine state.

| Capability | One definition and implementation | Migrated consumers and deleted copies |
| --- | --- | --- |
| Entity identity and player identity | `include/qa/actors.h:13,53`; `src/world/actors_internal.h:17`, created by `src/session/session.c:308` | Q1/Q2/Q3 attachments and QC/QVM/native bindings use the actor registry. The Q1 frontend's separate actor array/type and Q2/Q3 generic player identity stores are deleted. Source client numbering and source userinfo remain module boundary data. |
| Body and visual fields | `include/qa/world.h:7,12`; the shared entity store in `src/world/entity_internal.h` | Application control, compiled gameplay, QC, owned native hosts and client prediction use `qa_body_state`; source/body field adapters select `qa_body_vector_kind`. Presentation and source rule tails use `qa_entity_visual`; the ten-field Unified model copy is deleted. |
| Flare and world text | `include/qa/world.h:21,27` | Source publication, retained output, snapshots, the frame codec and rendering share `qa_entity_flare` and `qa_entity_text`. `3292af26` deletes the application/Unified flare copies; `c5c91b1d` deletes the Unified text record and per-frame text string copies. Native source emission and the existing save adapter convert only at their boundaries. |
| Current player state | `include/qa/player_state.h:9`, owned by `src/app/application/internal.h:158` | Control, selected movement, built-in services and prediction use `qa_player_state`. Q3's generic current fields and the full-state bridge are deleted. Combat/inventory/score reads retain their shared owners; game-specific respawn, spectator, weapon and animation rules remain rule tails. |
| Usercmd | `include/qa/usercmd.h:22`; `src/input/commands.c:227,371` | Human seats, both bot paths, command admission, simulation, history and Unified delivery use `qa_usercmd`. The five former input/source/Unified/network command types are deleted. `882194e8` also deletes the Unified builder wrapper and its builder/frame aliases. Protocol and module encoders alone apply native widths. See THE-869 below. |
| Item and inventory | `include/qa/inventory.h:10,28`; `src/gameplay/inventory_internal.h:47` | QC/QVM/native and compiled sources use common item IDs, definitions, entries and item bits. The three item-bit definitions and selected-equipment catalog copy/scan are deleted. Authored pickup rules remain source data. |
| Weapon request | `include/qa/equipment_weapon_slot.h:10,13`; `src/gameplay/modes/weapon_slot.c` | Equipment and QVM request/status/cancel/save paths share the request status and slot state. The second QVM enum and identity adapter are deleted. Original weapon firing phases remain rule data. |
| Damage and armor | `include/qa/gameplay.h:42,83`; `src/gameplay/combat_internal.h:36`, `src/gameplay/combat.c:1011,1014` | Built-in, QC, QVM and owned native sources enter the actor-indexed combat owner. Shared player UI uses `qa_armor`; its separate Unified armor type and converter are deleted. Original damage arithmetic is policy selected by this owner. |
| Output events and source ownership | `include/qa/builtin.h:100`, `include/qa/actors.h:48`; output ring under THE-870 | Compiled/QC/native/QVM outputs share builtin records, message arguments and prompt choices. `5a6f88e0` replaces both component/presentation owner records with `qa_source_owner` and deletes the second codec layout. Protocol-specific payloads retain their original discriminators. |
| HUD and bot observations | `include/qa/hud.h:16`; `src/ui/hud.c:7`; `include/qa/bots_player.h:5` | Local, legacy and Unified centerprint producers use the common center state; the second Q3 store/API is deleted. `6f516484` deletes Q3's duplicate bot player observation and its field-copy adapter; bot chat/combat/catalog and native observation use the common record. Original HUD layout interpreters remain presentation rules. |
| Cvars and role rule IDs | `include/qa/console.h:109`, `src/console/cvars_private.h:162`; `include/qa/ruleset.h:6` | Cvars are handles into one canonical-name/alias table. Movement, clocks and console policy use `qa_ruleset_id`; their duplicate enum types/casts are deleted. The withdrawn collision-family and by-name cvar audits are outside this slice. |
| System events | `include/qa/platform_events.h:41`; the queue under THE-864 | Physical input, text, console commands, packet records and frame time use `qa_sys_event`. Foreign SDL/socket records convert at intake. |

The deleted type names and old movement/clock/console enum casts have no
remaining references in `include`, `src` or `tests`. Their replaced wrappers
and field-copy adapters are deleted, rather than retained as alternate APIs.
Required packet/ABI projections are listed above; they preserve external
layouts and are not additional engine primitive implementations.

Verification at `6f516484`: the normal engine build and existing seven core
suites (`platform_services`, `core`, `archive`, `vfs`, `bsp`, `image`, `model`)
passed. Its build log is
`/tmp/qa-normal-common-bot-state-20261010/build.log`. These are source/build and
component checks. No new installation, input/audio gameplay sweep, whole-frame
allocation result or performance result is claimed by this report.

## Geometry and live entities: THE-2873 / THE-2861

| Capability | Canonical implementation and migrated callers | Remaining bypass |
| --- | --- | --- |
| Const geometry and caller scratch | `include/qa/collision.h:89,103,120`; `src/world/collision/geometry.c:9,68,404,422`. World, frontend, host, prediction, effects and navigation callers pass their load-owned scratch. | None found in the query caller audit. Mutable world/portal updates remain serial. |
| Q1/Q2/Q3 kernel scratch | `src/world/collision/q1.c:99,571`, `q2.c:59,122`, `q3.c:34,89`. Continuations, expanded planes, clipping caches and marks are in caller storage. | Loaded brushes and patches contain no query visit marks. |
| Canonical contents and surface bits | `include/qa/collision_bits.h`, `src/world/collision/contents_bits.c:48,67,83,145,156`; loaders and live-field boundaries import canonical bits. Module/protocol/save boundaries export original representations. | Old pairwise contents converter and callers are deleted. |
| Entity pose rules | `qa_collision_target.pose_rules` and the single basis/normal/link implementation in `src/world/collision/contents.c:19,25,35` supply original entity-role policy independently of geometry and caller contact/merge rules. All current target/link callers migrate. | The old kernel-owned pose bodies and link expansions are deleted. Current mixed-role parity and loaded-geometry serial/concurrent proof pass. |
| Render point-leaf queries | `qa_collision_point_leaf` supplies visibility selection, Q2 secondary-cluster probing and point-light indexing at `src/render/scene/world.c:807,985` and `world/legacy/lighting.c:604`. Frontend map and standalone brush constructors and Q3 model registration retain common geometry; cold resource lookup uses `src/world/body.c:156`. | `qa_scene_world_leaf` and its renderer-owned BSP walk are deleted. Render topology remains for surface/PVS data. This slice does not establish adoption of every geometry operation. |
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

TA-3192 render-leaf adoption passed the normal production build and all seven
existing core suites. Logs: `/tmp/qa-ta3192-leaf-20261010-build.log` and
`/tmp/qa-ta3192-leaf-20261010-core.log`. The common floating-point split rule
matches the original render point-leaf rule; this is not a claim of identical
rounding to the deleted double-precision walk. No installation, live visual
check or performance result is claimed for this slice.

## Whole-frame allocation: THE-2874 / THE-873 / THE-882

The common arena is `src/core/arena.c`; the fixed pool is `src/core/pool.c`.
Sealed storage never falls back to the heap and records capacity exhaustion.
Actor pages, spatial snapshots, visibility scratch, frame leases and retained
scene/native workspaces use storage prepared at load.

The 2026-10-10 THE-2874 census used the existing link-time allocation gate and
600-frame listen servers bound to localhost, with copied owner settings,
private X displays and dummy audio. Each case had 120 warm-up and 480 measured
playing frames. Changes were limited to reported callers and the named DOSBox
receive allocation.

| Reported caller | Adopted storage or read path |
| --- | --- |
| `src/render/cpu/raster.c:220,813` | Worker creation reserves an 8,192-triangle batch. The triangle append no longer reallocates. Overflow uses the existing flush/direct raster path and preserves drawing. |
| `src/gameplay/q2/entities/state.c:67` | Authored fields belong to the common map arena. Spawn, checkpoint restore and original-save import use that ownership. Map entry resets it and source preparation seals it; entity-slot reuse no longer frees an individual field array. |
| `src/network/admin/owner.c:450` | Master refresh compares current names with retained names before copying. Unchanged frames allocate and free nothing. |
| `src/network/dosbox.c:71,225` | Socket creation owns 256 fixed packet slots and a borrowed receive buffer. Delivery, queue overflow and receive no longer allocate or free packets. |

| Listen-server case | Initial post-warm-up calls | Final post-warm-up calls |
| --- | --- | --- |
| Q1 classic e1m1 | Zero | Zero |
| Q2 classic base1 | One realloc and one free | Zero |
| Q3 q3dm1 | 480 malloc/free pairs | Zero |

Every final frame reports zero malloc, calloc, realloc, frees, requested bytes,
null results, size overflow and capacity exhaustion. All three runs quit with
exit 0; owner profile bytes remained unchanged and owned processes exited.
The normal production build and seven existing core suites pass.

The additional TA-873/TA-882 census on 2026-10-10 covers the requested combined
configuration and module boundaries. Each run again completed 600 playing
frames, with 120 warm-up and 480 measured frames, using copied owner settings,
a private X display and dummy audio.

| Added listen-server case | Initial heap calls / name lookups | Repeat heap calls / name lookups |
| --- | --- | --- |
| Q1 dm1 world, Q3 movement, Q2 rerelease monster provider, Q2 classic client/HUD | 0 / 0 | 0 / 0 |
| Q2 rerelease base1, installed native `baseq2/game_x64.dll` | 0 / 8,640 | 0 / 0 |
| Q3 q3dm1, original QVM game and cgame | 446 malloc + 446 calloc + 892 frees / 0 | 0 / 0 |

The QVM calls came from `qa_qvm_observe_writes_owned` and retirement around
`q3g_call`. `src/compat/qvm/memory.c` now retains inactive registrations and
their range storage until VM destruction, reuses them only after delivery
returns, and excludes them from active checkpoint inventories. The native
`maxclients` reads came from `src/app/application/network_unified.c:59` and now
use the provider handle resolved at bind. No other steady-state callers were
changed for this census.

Native startup also needed to reuse its cold-prepared engine, declare existing
common-table aliases to the module, and accept the module's `ctf=0` default.
These fixes remove valid-state rejections rather than add admission layers.
All final measured frames report zero heap calls, requested bytes, failed
allocations, capacity exhaustion and name lookups. Each run exits normally;
original profile bytes are unchanged and owned processes exit.

The observation covers engine/static calls across threads inside
`qa_frontend_step`. DSO/libc internals, other allocation APIs, outer save drain,
pacing and shutdown are excluded. The combined dm1 map selects the Q2 monster
provider but has no authored monsters. The QVM run logs one skipped
`control/move` feature with no diagnostic; this is an allocation census, not
movement acceptance. Unmeasured editions, input scenarios and module internals
are not established by these cases. No timing comparison or new installation
was performed.

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

`include/qa/usercmd.h:22` defines the one engine command.
`src/input/commands.c:227` builds it from `qa_input_command_intent`, using one
rule-set scale/button/angle policy. Human sampling at `commands.c:371` calls
that builder; bots fill the same intent and call it at
`src/bots/ai/frame.c:111` and `src/app/application/bots_submit.c:82`.

Human callers are `src/app/frontend/frame.c:385`, `remote_input.c:68`,
`network_q1_client.c:981`, `remote_q2_presentation.c:394` and
`remote_unified_input.c:171`. `882194e8` deletes the second Unified builder
implementation and its two type aliases. The former bot-only Q3 builder and
five input/source/Unified/network command types have no remaining callers.
`qa_usercmd_equal` also serves admission and prediction, without a second
command comparison implementation.

Conversion between command bases is in `src/input/commands.c`.
`include/qa/native_host_q2_wire.h:10` encodes both native Q2 API layouts for
all three host writers. `include/qa/network_q3.h:67` projects the Q3 ABI for
control, arsenal, bot source commands and the network client. These are
boundary adapters; they do not build a second engine command or rescale bot
intent separately. Legacy protocol records retain their native field widths.

The Q2 packet adapter now retains the rerelease float angles required by
`qsrc/quake2-rerelease-dll/rerelease/game.h:431`. One angle projection helper in
`include/qa/network_q2.h` serves sampled and queued input. The KEX codec compares
and emits the actual floats; received commands, prediction and host delivery
retain them. Classic packet fields still encode original shorts. Network
checkpoints accept the existing short-angle record and retain new exact floats
in an optional trailing section; no user-facing format choice is added.

Verification: the normal engine build and seven existing core suites pass.
The core command-angle check covers classic and both KEX codecs, preserving a
change smaller than one short-angle step through full and delta packets. Build
log: `/tmp/qa-normal-q2-float-command-20261010/build.log`. The checkpoint extension
is source-reviewed, not claimed as a live save/restore round trip. No new
installation, original-module gameplay or frame-time result is claimed.

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

TA-3192 migrates command-context equality to `qa_command_context_equal` in
`src/console/text.c`. Console dispatch/release/persistence, physical input,
configuration, native/QVM services, and NQ/Q2/Q3/Unified client bindings call
that implementation. Their copied field-by-field comparisons are deleted.
Caller masks preserve intentionally omitted fields; recipient policy still
requires an absent script. Nullable script text has one pointer-equal fast
path and one fallback comparison. This removes the comparison copy from
`remote_unified_input.c`; it does not claim that all script names are interned.
The normal build and seven existing core suites pass. Other listed identity
and render-leaf adoption gaps remain open under TA-3192.

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

Scene, audio and buttons now use that same `qa_game_family`, deleting their
three duplicate enums and every corresponding type/token caller. One
`qa_product_ruleset` at `src/content/catalog/catalog.c:4` replaces eight
intrinsic product mappings. Original role clocks, invalid-input fallbacks,
button arithmetic, audio looping and structure/protocol/save numeric values
remain unchanged. Component evidence and exact remaining scope are in
`docs/playtests/2026-10-09-common-family-types.md`.

## Cvar handles: THE-2859

The one handle implementation is `include/qa/console.h:213` and
`src/console/cvars.c:1140`. Resolve at bind or registration, then read the common
value table by handle. The table owns alias and dialect conversion; callers do
not retain copied values or introduce lookup context checks.

| Caller | Adoption |
| --- | --- |
| Compiled Q1 and original QC policy | `src/gameplay/q1/runtime.c` binds common handles before spawn; Q1/QW movement, limits, pause, chat and addon rules read them. The named application callback/context is deleted. |
| Q2 source settings | `src/gameplay/q2/source.c:20` binds the source handles; `qa_q2_source_value` reads them. |
| Q3 source controls/settings | `src/app/application/native_q3_console.c:65` and `native_q3_settings.c:353,522,681` retain common references instead of fixed-name polling. |
| Q3 loading and authored HUD | `src/presentation/q3_native/loading.c:77,382` retains `sv_running`; `mission_hud.c:308` and `mission_hud_menu.c:49` retain `cg_hudFiles`. Authored menu references refresh on declaration revision. These migrations are in e61f9d5f and c1537cff. |
| Master refresh and rerelease RCON | `src/network/admin/owner.c:275,465` binds the fixed policy handles when the source registry changes. Administration adoption invalidates the bindings. |
| Early source administration | `src/app/frontend/source_admin.c:150` binds password, filter, publication and dedicated handles for ordinary binding and restore. Policy/authentication readers use those handles. |
| QuakeWorld host | `src/app/frontend/network_qw.c:20` binds download, authentication and spectator-limit handles at creation and when the source view changes. Pump and admission use indexed reads; the cold declaration checks remain named. |
| Native Q3 receiver | `src/app/application/native_q3_remote_role.c:58,242` binds the cheat setting before constructor callbacks and uses it for policy reads. |

Native ABI cvars remain derived module objects; unchanged polls skip guest
rewrites. Name reads remain for declarations, typed console text, external
module string syscalls and cold persistence. Fixed packet-policy readers in `src/app/frontend/network.c` now retain
handles in its server/client binders (THE-3177). Query, admission,
authorization, RCON and remote download permission read those handles.
Q2 command permission and password-change callbacks bind their handles in
`src/app/application/native_q2_console.c:115`. Remaining names in these files
are registration, configuration, explicit console commands or menu reads.

The existing frame allocation gate now counts `qa_cvars_find` in the same
`qa_frontend_step` interval (`src/console/cvars.c:1035`,
`src/core/allocation_gate.c:55`). Diagnostic builds retain fixed-storage
name/caller attribution; normal builds have no lookup observer. Both the
initial census and its one repeat ran e1m1, base1 and q3dm1 as 600-frame
localhost listen servers. Every case recorded zero name lookups, heap calls
and pool overflows in all 480 frames after the 120-frame warm-up. The initial
census reported no readers to migrate, so this slice changes no cvar reader.

The runs used private Xvfb displays, contained dummy audio and fresh copies of
the owner's profile. Each exited normally, left the source profile unchanged
and cleaned up its owned processes. Logs and receipts are in
`/tmp/qa-cvar-gate-{initial,final}-20261010/`. These cases do not exercise every
external packet-policy branch or guest string syscall; their packet-policy migration is separately checked under THE-3177. No timing gain,
audio proof, rerelease census or new installation is claimed.

## Entity store: THE-2876

`src/world/actors_internal.h:9,17` is the single store: actor pages contain
identity, generation, player and body columns; the registry owns the liveness
free-bit words and slot-indexed area links. `qa_actors_create` at
`src/world/actors.c:118` sizes every page, source index and link at load.
Claim/release at `actors.c:222,325` update that store without allocating a page.
`src/session/session.c:308,792,805` routes session creation, claims and releases
through it. World body access at `src/world/collision/world_internal.h:83`
returns the body in the actor page, rather than a world-owned body array.

| Migrated consumer | Common path |
| --- | --- |
| Player and map bindings | `src/app/application/map_players.c:86,1101` resolve actor/source identity through the registry. |
| Body binding, release and clipping | `src/world/body.c:27,363`; `src/world/collision/world.c:161,181` use actor-page bodies and the registry's live identity. |
| External native entity bindings | `src/compat/native_host/world.c:16,327` resolve the shared actor and read live boundary fields; native edicts remain module-format data. |
| Area-grid link/unlink | `src/world/spatial.c:42,59` modify the registry's u32 previous/next links. Q3 inserts at the head; Q1/Q2 at the tail. No per-link allocation remains. |
| Authored target routing | `src/campaign/targets.c:405` updates one changed slot in the target and authored-order indexes. Bind/unbind and targetname edits call it at `targets.c:212,223,254`. Q1 maps (`maps/runtime.c:182`), Q2 routes (`entities/routes.c:26`) and Q3 maps (`map/runtime.c:266`) use the same index. |

The code check finds no secondary world body owner, heap-allocated area-link
node or actor-revision-driven whole-capacity target rebuild in these paths.
Foreign edicts and source slot numbers are module boundaries, not another
engine identity owner. THE-3177 removes the duplicate collision-family enum; native contents
annotations use the common game-family type. Caller trace rules are the
remaining policy step and do not own identity or liveness.

The existing normal build/core suites pass. The THE-2859 census and repeat on
e1m1, base1 and q3dm1 also record zero frame heap calls and pool overflows after
warm-up. No new timing,
demo comparison or target-refresh counter is claimed. This section supplies
the requested code/adoption check; supervisor closure remains separate.

## Output event ring: THE-870

The application owns one load-sized `qa_event_ring` in
`src/app/application/events.c:158`. Built-in, Q2 map/player, Q3 map, original
protocol and equipment emitters admit one tagged transaction through that
file. Unified views append to the same transaction at
`src/app/application/unified_events.c:538`; they have no second journal.
Protocol codecs retain original widths and encode at the network consumer.
QuakeC raw Write buffers are load-sized private construction scratch, not
another admitted-event store.

All thirteen frontend consumers use `qa_application_event_read` at
`src/app/application/events.c:1153`: native composition, source effects,
Q2 messages, player events, equipment, QC messages, map events, audio,
Q2 host, QW host, particles, frame and NQ host. Per-family arrays, indexed
accessors and the order-reconstruction journal are deleted. The Unified
receiver uses the same ring implementation at
`src/app/frontend/remote_unified_events.c:199`, with independent presentation
and simulation cursors over one store. Resource and component metadata use
page leases and interned ids. Persistent events, sign-on and world text retain
those pages for their actual lifetimes; transient lookup retirement does not
invalidate them.

Capacity handling in `src/app/application/events.c:244` preserves admitted
history, omits new transient effects and closes affected reliable channels.
Omitted baseline output prevents an incomplete new sign-on. QuakeC makestatic
removes its entity only after append. Publication has no sign-on failure path
after commit. No runtime bypass of the common admitted-output store was found
in the migrated emitters or thirteen consumers. Foreign raw protocol buffers
and cold checkpoint adapters remain format boundaries. Transport send pressure
now uses the non-overwriting admission described under THE-2864.

Normal builds and all seven existing core suites passed for `53067776`,
`febb3e12` and `fb0d0d6e`; logs are under
`/tmp/qa-normal-{signon-commit,output-capacity,map-output-admission}-20261010/`.
These are implementation and component checks. No installation, live combined
mode, legacy-server round trip or measured speedup is claimed for this slice.

## System intake: THE-864

`qa_sys_event` in `include/qa/platform_events.h:43` is the one timestamped
system record. `src/platform/events.c:55` admits it into fixed common event
pages; `src/platform/events.c:138` visits pending records in admission order.
Consumers retire their own leases, so a deferred packet does not block key
releases. The old platform byte allocator and head-only consumer API are
deleted. Packet admission reserves input and clock capacity; destructive
reads happen only after admission.

SDL input enters at `src/platform/input.c:2494`, stdin at
`src/platform/console.c:22`, and datagrams at
`src/platform/network_events.c:5`. `src/app/frontend/network.c:6220` collects
every current runtime and discovery source. OS game-socket reads live in the
transport boundary, including native IPX and KEX discovery. Child-module IPC
and the optional OAuth callback are separate service protocols. One dispatcher
at `src/app/frontend/frame.c:410` handles input, console, packets and host time;
recovery supplies records to that dispatcher. No direct gameplay intake bypass
was found among these callers. Loopback send admission remains THE-2864.

The existing core suite checks retained packet bytes across 4,096 independently
retired key releases, FIFO dispatch, and input/time admission under pressure.
Normal builds and seven existing core suites passed for `d4310588` and
`057908b4`; logs are in `/tmp/qa-normal-independent-input-retirement-20261010/`
and `/tmp/qa-normal-queued-host-clock-20261010/`. This report covers current
implementation and component checks, not a new installed-game qualification.

## Host phases: THE-865

`src/app/frontend/frame.c:785` owns the host frame: physical intake and event
drain, commands, server advance, another physical intake and event/command
drain, then client command construction, prediction, scene, audio and
completion. Both intakes are nonblocking and remain active during constructor,
settings and restart waits; unavailable input consumers defer their records.
The sole frontend call to `qa_application_advance` is at `frame.c:917`.
`frontend_replay_frame` at `frame.c:1072` feeds the same phases using recorded
timing without physical intake. Its separate simulation loop is deleted.
Q2 final-ACK retirement at `src/app/frontend/network_q2_host.c:807` consumes
queued packets without another physical receive. No second frontend server
advance or physical receive in that retirement path remains.

`7c42f439` and `057908b4` passed one normal build and the seven existing core
suites each. Logs are in `/tmp/qa-normal-two-physical-drains-20261010/` and
`/tmp/qa-normal-queued-host-clock-20261010/`. Combined gameplay, installed
startup and original-protocol interoperability were not rerun in this slice.

## Platform services: THE-866

`src/platform/services.c:41` owns performance and clock reads, `:207` entropy,
and `:294` calendar conversion. Audio, render, frontend, gameplay and module
service adapters call these APIs; SDL event timestamps stay inside platform.
The campaign's unused result counter/frequency fields are deleted. Current
source inspection found no actual OS clock, entropy or calendar read outside
platform; guest import symbol tables and injected gameplay clocks are module
boundaries, not duplicate OS implementations. Native process services use the
same APIs in `src/platform/native_process.c`. IPX and KEX game reads enter the
shared event queue through the transport collector described under THE-864.

The existing `platform_services` suite checks clock bounds, calendar conversion
and entropy writes. It passes with the other six core suites on `057908b4`.
No Windows/native-IPX runtime, installed gameplay or timing claim is made by
this current-source close-out.

## Output retirement between ticks: THE-909

`src/app/application/unified_events.c:526` retires the common output ring only
through the minimum local and network cursor. The network minimum at
`src/app/frontend/network.c:6538` includes Unified, NQ, QW and Q2 peers.
`src/app/application/network_unified_server.c:576` uses actual reliable
receipts for peer retirement. Host turns without a source tick no longer clear
an unsent inventory, sound or message record. The former independent Unified
journal and wholesale per-host clear are deleted by the THE-870 migration.

`src/app/frontend/network_unified.c:381` releases the shared publication
boundary after capture even when a peer retains its own pending output; peer
pressure does not wait for every recipient before another source frame.
No separate wholesale-clear bypass was found in current output retirement.
Current normal/core checks pass; prior installed inventory proof belongs to
THE-909's existing Linear record and was not rerun for this close-out.

## Loopback admission: THE-2864

One endpoint FIFO is implemented in `src/network/transport.c:658`, with all
packet and byte storage allocated by `qa_net_loopback_bind` at load. Host
routing forwards to that same endpoint. The overwrite-oldest branch and its
synthetic dropped-packet record are deleted. Full leaves existing data intact,
keeps receive open, and increments `qa_net_transport_full_count`.

Connected send callers use `qa_net_send_result`. NQ, QW and Q2 check capacity
before destructive command encoding. NQ fragments and ACKs stay in their channel
until `qa_q1_peer_send` admits them (`src/network/q1/channels.c:83,96,242`).
Q3 uses the same prepare/commit rule. Unified receipt flags and retransmission
counters advance only after acceptance. The rerelease channel retains unsent
fragments in its existing pending list (`src/network/q2/kex_channel.c:55,76`);
reliable completion still requires a peer ACK. Its immediately accepted single
transient packet keeps the direct, allocation-free emission path. Handshake
preflight retains the input/request when a local reply cannot be admitted.
No second loopback queue or raw-loopback overwrite bypass remains.

The normal build and seven core suites pass. Existing core tests now verify a
64 KB FIFO packet surviving Full and retry (`tests/core_test.c:729`), and native
serverinfo plus signon over 8 KB delivered once through a one-packet loopback
queue in NQ 15, Fitz 666 and RMQ 999, including a blocked ACK (`:762`). Q3 and
rerelease fragment checks also exercise repeated preparation and blocked retry
turns. This is native transport/channel proof; the application was not launched
and no new `qfiles/qa-c` was installed for this slice.

## Collision policy and remaining cvar callers: TA-3177

The packet-policy slice uses retained common handles in
`src/app/frontend/network.c:173,353` for Q3 query/status, authorization,
admission, RCON and remote download permission. All client registration,
restore and rebind paths call the client binder. Server view changes rebind
its policy handles. Q2 command permission and password-change callbacks use
`src/app/application/native_q2_console.c:115`; the existing restore binder
also refreshes those handles. No lookup cache or new failure path was added.
Remaining name reads in these files are cold declarations/configuration,
explicit console operations or menus. `src/gameplay/q2/source.c:20` already
binds its per-tick source handles; its named rule projection helpers are
called by configuration and cvar-change operations.

The normal build and seven core suites pass for this slice. No live external
packet round trip or additional census is claimed. The duplicate collision-family enum and all its type/token callers are
deleted from C headers, source and tests. Native contents/surface boundaries
use the one `qa_game_family` type. `qa_bsp_family` remains a file format.
Existing continuation tags stay 1..3 through the one boundary codec in
`src/persistence/source_io.c:111`; physics, contacts, world collisions,
portal claims, recipe portals and remote prediction use that codec. The
existing movement-result core check protects those stable tag values.
Caller trace behavior is bound through the immutable `qa_trace_behaviors` table
in `src/world/collision/contents.c:4`. The policy carries its descriptor,
contents mask and query options; the per-query family selector and rerelease
boolean are deleted. Movement, gameplay queries, bot navigation, native hosts
and prediction select the descriptor at their policy boundary. The shared
world/body path reads hull/box, ownership and merge behavior directly; native
contents formats remain boundary annotations. The existing serialized family
and merged-contents fields still reconstruct the descriptor on restore.
The normal build and seven core suites pass. This slice does not claim a
new live combined-mode or legacy-protocol round trip.
