# Persistence owner schema, 2026-09-29

This packet implements the shared envelope and concrete actor, string, session,
scheduler and world producers. B30 remains incomplete. Application owner codecs,
original import/export, migrations, MVD/GTV owners and VCR workflows remain
required work. The record inventory is a completeness check; it is not evidence
that every listed producer already exists.

## Shared file layout

All integers use explicit little-endian codecs. Float fields use the network
codec's explicit IEEE field encoding. No C struct, padding, pointer, callback
address, process registry namespace or host file descriptor enters a file.

`QASV` version 1 has an 88-byte header. It stores its eight-byte signature,
version, header extent, complete file extent, purpose, owner count, simulation
elapsed nanoseconds, configuration generation, world generation and composition
SHA256. Each record has a 56-byte header with owner kind, schema version, three
counted identity strings, reserved flags, payload extent and content SHA256.
Strings contain instance identity, codec schema identity and execution/backend
identity. The last 32 bytes are SHA256 of every preceding byte, including all
headers and identities. A malformed shared signature never falls back to an
original save parser. Unrecognized versions require an explicit migration.

Every shared owner below occurs exactly once. Uninstalled owners need an
explicit absent value in that owner's declared codec. Each selected provider
instance occurs once with its independent private continuation. A configuration
producer must compare its exact selected instance set with those records. An
empty payload, missing shared record, duplicate shared owner or duplicate
provider instance fails image admission.

| Owner record | Required authoritative fields and reconnect obligations |
|---|---|
| Strings | Exact ordered counted bytes, including embedded NULs; saved field references index this table and are checked before use. The string codec rejects duplicate entries. |
| Resources | Logical content identity, mount precedence, selected map/program/declaration/interface digests and decoding parameters; reconnect resource handles after content validation. Cache allocation capacity is not continuation. |
| Configuration | Every world/preset/edition/map/environment choice; ordered selected instances and program/backend/artifact/options identities; every scoped role/selector/definition binding; each independently enabled mod; modes, equipment, local/remote/bot seats, loadouts, authored monster substitutions and weapon behaviors. Product references resolve by stable catalog identity. Restore cannot insert preset defaults. |
| Session | Elapsed time, actor/component capacity, mixed ordering, next provider order; each component owner, exact clock config, host origin, elapsed/debt/frame/paused state; every live actor execution binding; scheduler registrations, order and pending actor/callback/due/sequence/boundary. Provider state owns callback identity. Restore resolves addresses from the candidate provider. |
| Actors | Every allocated or vacant host slot through high water, its generation, live flag, owner, definition, source-slot association and permanent retirement; restore creates a fresh registry namespace and saved-reference history. |
| World | Authoritative and stored body values, saved ground references, distinct retained link state/bounds/count, effective/stored/retained collision policies, collision ownership, external binding kinds, attachment anchor/follow/offset/order, storage/collision identities, world sequence counters and exact per-sector broadphase membership order. A linked body without membership retains suspended collision. Providers rebuild bindings before candidate restoration. |
| Combat | Each actor's health, mass, armor, damage flags and team; primary storage ownership; ordered regular/powered protection reservoirs, policies and private stores, matching item/cell reservoirs, leases and ownership identities reconstructed rather than copied. Pending composition state must be idle or explicitly represented. |
| Inventory | Local primary entries, externally owned primary entries, source counter policy/capacity, native-only entries, ordered item groups and definitions/actions, overlap rules, pickup claims and exact owning instance. External owners restore source memory first and adopt the same canonical store. Saved leases and token addresses are replaced with candidate leases. |
| Pickups | All live offer owners, pickup options and availability/respawn/drop/claim continuation; reservations cannot be omitted. Provider callbacks and canonical claims rebuild once. |
| Targets | Every authored target field, dynamic name/target changes, delay ownership and scheduled delayed actors; owner bindings rebuild from restored provider state. The target name index can be regenerated from those fields. |
| Campaign | Current location/unit, source mission gates/keys/sigils/server flags, completion/progression and retained hub worlds; each retained world is a validated shared snapshot with nested campaign worlds prohibited. Pending transition expression, landmarks, actor cause, carry state, revision and completion state survive. |
| Modes | Complete qa_modes_checkpoint, each mode identity/source/rules, shared team/player scores, mode bindings, external objectives, pickups/flags/relics/obelisk/horde state, match/election/referee timing and queued intent/event continuation. Mode/resource/actor references reconnect to the candidate. |
| Equipment | Each actor's current and pending selection, controls, edge flags, slot activation/holster/lower state, pending configuration, jump/teleport continuation and source grenade/grapple state. Preserve independently selected source mechanics. |
| Progression | Current player/arena/campaign records and dirty/publication state; provider ranking requests, events and subscriptions remain separately owned. Restore must not write a candidate's external user files before publication. |
| Roster | Every local/remote/dynamic source client record, canonical actor and source projection, names/team/skin/model/userinfo, admission/connection/begin/retirement state, respawn/carry state, seat ID, source slot, source spawn selectors and map spawn selection RNG/history. |
| Controls | All active movement continuations and selected profiles; authoritative bounds/ground/water/view/input sequence/buttons, saved cutscene character/view/mode/damageability, Q2 rerelease pml origin, pending/current mode flags and every retained motion discontinuity. Stable owner identity replaces provider pointers. Retained movement results and contacts need an explicit declared lifetime or a codec. |
| Cvars | Ordered definitions/value/default/reset/latched state, flags, revisions, private source ownership and metadata; restored registry and producer declarations agree. |
| Commands | Script stack/source positions, macro/expansion/queue/condition state and command execution continuation; queued command text and ownership, wait and alias state; function pointers reconnect through registered commands. |
| Events | Builtin/Q2-map/Q3-map/Q2-player/protocol event queues in order, every counted string/payload, all protocol reference offsets/actor generations and reliability/destination/signon metadata. Arena pointers are encoded as values. |
| Navigation | Content identity and mutable routing/world bindings, disabled areas/travel costs, dynamic movers/teleports/hazards and routes; regenerated immutable navigation has checked content identity. |
| Bots | Shared bot library variables/RNG/resource mappings, navigation/perception histories, goal/weapon weights and selectors, movement paths/timing, action bindings/edges, chat pool/queues/timing, complete AI/player connections and source-internal bot pending admission. In-memory same-binding qa_bots snapshots alone do not meet this codec. |
| Connections | One connection authority, generation/seat ownership, phases/endpoints/protocol/composition, reliability sequence/toggles/acks/fragment queues/retry deadlines/download continuity, source adapter gamestate/signon and connected runtime lifetime. OS sockets are reopened after validation. |
| Prediction | Every seat's command and prediction history, correction/snapshot acknowledgments, epoch and sequence/time state, full movement continuation/profile and source protocol state. |
| Presentation | Client gamestate/configstrings/baselines/snapshots/reliable commands/source cgame/UI state, scene effects/HUD/UI/console/cameras/shader remaps/Q2 visuals and retained output needed after replay. GPU objects are regenerated from checked content. |
| Audio | Logical sound/music sources, stream/sample cursor, scheduling/mix state, playlist and source ownership; device handles reopen. |
| Input | Local seat/device selection, controller/gyro/haptic/accessibility controls, held/edge state, overlays, focus and console/UI capture ownership. Device IDs resolve through the platform boundary. |
| Media | CIN/RoQ playback and decoder checkpoints, picture/pending frame timing, captions, authored cinematic/nextserver continuity and media resource identities. |
| Application | Application random words/front/rear/draw count, current map/nextserver, publication/catalog generations, lifecycle state, map/source/primary-mode identities and pending route ownership. Running callbacks, admissions and half-publications are forbidden capture boundaries. |
| Provider instances | Full native Q1/Q2/Q3/expansion/rerelease state and every external QC/QVM/native instance's program/module/private memory/host continuation; clients/UI/bots/source callbacks and resource/actor mappings. Pin content, schema and ABI. Host-only native checkpoints cannot stand in for module private state. |

## Implemented concrete codecs

`QAAR` version 1 stores a 16-byte actor header and 24 bytes per slot. Generation
zero is valid. Vacant slot history and permanently retired generations survive.

`QAST` version 1 stores ordered counted strings with 64-bit extents. Candidate
interning checks the exact saved table index. No process-local identity is
trusted without the table.

`QASS` version 1 stores a 64-byte session header, 140 bytes per component,
16 bytes per execution binding, 16 bytes per scheduler registration and 40 bytes
per scheduled think. All clock/frame fields and tie-breaking orders are explicit.
Scheduler callback resolution holds an operation lease and cannot mutate or
destroy its scheduler. Actor mutation during resolution fails validation.

`QAWD` version 1 stores a 32-byte world header, 439 bytes per body and 16 bytes
per broadphase membership. The candidate restores membership in saved list order,
including Q3's observable head insertion. It validates saved sector placement
against restored geometry and validates attachment cycles. External body and
collision bindings must already have been rebuilt by their real producers.

`QADM` version 1 stores a 64-byte demo header and digest-checked records with
52-byte headers, counted payloads and 32-byte digests. Records carry sequence,
explicit simulation duration/time and native protocol identity. The first record
is a full shared keyframe. Seeking restores an isolated candidate from the
nearest preceding keyframe, replays records and publishes after validation.
Unfinished recovery admits only the last complete checked prefix; a complete
block with an invalid digest fails. Original demos/MVD/GTV require their source
framing and transport owners in addition to these shared records.

`QAPR` version 1 stores the real application roster, including full saved actor
provenance, connection/seat generations, source slots, guest instance digests,
owned texts, authored spawn points, Q1 selector history and deferred deadlines.
`QAAP` version 1 stores the application random state and publication/command/map
stamps, current map, ordered mode identities and stable map product identities.
See the separate roster packet for its source and integration limits.

## Source-only verification and remaining boundaries

The source-only gate remains active. No configure, compiler, parser, test,
executable, game, benchmark or sanitizer was run. Scoped tracked whitespace
inspection passed. `/tmp/qa-persistence-foundation-20260929.sha256` freezes the
current shared foundation/demo/recovery source/header identities for independent review.

There is no complete application save/restore caller yet. The isolated application
factory now calls application_save_session_create and qa_session_create_restored
before combat/inventory/world borrow the registry. Actual foundation capture,
decode and staged finish use the concrete owner codecs; final scheduler restore
resolves native Q1 and QC callback IDs. Complete configuration capture/decode and
prepared-content validation use the sole qa_launch_identity codec. Resources,
selected providers and remaining owners still require reconstruction before
publication. Ordinary map loading must not initialize over restored actors or
reuse old namespace handles.

The guest producer's existing QC checkpoint includes VM and host state. QVM host
checkpoint imports are bound; application client/engine/roster/bot coupling is
still required. Native module private continuation and a full bot library codec
remain required. Network reliability queues/runtime prediction codecs and
MVD/GTV recording/broadcast remain producer gaps. The filesystem WRITE stream now exposes qa_fs_stream_sync, supplied by the
network owner. Demo/recovery commits sync each complete digest block on the
retained stream handle. Durability behavior has not been executed under the
source-only gate.

The existing primary-adoption packet was independently reread before consuming
its contracts. All eleven identities matched its frozen hash list. Reviewed
adopt/detach/host-disconnect branches produced no confirmed defect. This review
does not qualify runtime behavior or its remaining integration gaps.
