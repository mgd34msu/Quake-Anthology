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
