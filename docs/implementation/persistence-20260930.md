# Application persistence integration, 2026-09-30

This packet connects the application to the existing staged shared-save
coordinator. It contributes to B30 and B34. Both tasks remain open. The source
completion gate is still active; no build configuration, compiler, parser,
test, executable, sanitizer, benchmark or gameplay run qualified this packet.

`qa_application_persistence_capture` acquires the application's serial
persistence operation and constructs the exact owner inventory from the
committed configuration. Sixteen shared owners have concrete application
dispatch: strings, actors, session, world, configuration, roster, cvars,
application, combat, inventory, pickups, targets, controls, modes, equipment and
commands.
Native Q1/Q2/Q3 and QC instances have concrete provider dispatch. Other
services supply actual producer descriptors through
`qa_application_persistence_ops.owners`. Missing producers fail capture; the
coordinator never invents absent state for an installed service. Duplicate,
shadowed, unselected or differently identified producers fail admission.

The generic image still requires every shared owner exactly once and every
selected provider once. Every producer uses an explicit codec and owned bytes.
The persistence operation blocks public application mutation while capture
checks configuration generation, publication generation, actor revision and
safe owner boundaries. Ordered strings are compared after capture to reject
late producer interning. Producers remain responsible for their own internal
revision and callback boundaries.

`QACF` version 1 records configuration generation and the complete existing
launch identity codec. Its SHA256 is the envelope's composition digest, computed
over the launch identity bytes. Decode starts from an empty draft and validates
the candidate's prepared content, options and exact instance set before
accepting restored configuration publication. `QAAO` version 1 contains the
existing `QAAP` metadata and `QAMT` authored map continuation with checked
64-bit extents. Pending authored travel and nextserver survive alongside the
random generator, publication stamps and ordered mode identities. `QAPV`
version 1 retains provider kind, map binding, Q1/Q2 server flags and the source
continuation. Its record descriptor pins the selected instance identity and
backend. Payloads are never raw structs.

`qa_application_persistence_restore` constructs a fresh application, actor
namespace and ordered string table. Saved session elapsed time is available
before provider preparation. The restored configuration transaction prepares
real provider instances without running ordinary publication callbacks.
The mandatory application-owned content helper constructs map geometry, world
and actual provider services, attaches the ordered selected
providers/components/policies and retains their content. The optional external
`prepare_content` hook prepares detached consumers afterward. Neither runs
source initialize, spawn, connect or begin
callbacks, create a roster or reset saved actors. Q1 authored service options
use `qa_q1_game_maps_bind`, never `qa_q1_game_begin_map`.

The generic coordinator restores provider continuation, then the canonical
roster for scoped role lookup, then private modes and
equipment continuation, before shared world, combat, inventory, pickups and
targets. Inventory group descriptors can therefore resolve all native owners.
Those owners reconnect to
authoritative source storage. Native provider codecs must separate private
continuation from shared-store reconnection where their existing typed restore
APIs require pickup/combat/inventory state. Targets restore once through their
saved native or guest source descriptor. Q3's concrete byte codec combines native and
authored continuations and restores private source state before reconnecting
pickup observations, inventory catalogs and mover damage admissions after
the shared owners. Its constructor binds the empty authored service. Q2's
explicit portable codec is dispatched directly and reconnects after shared
stores. Q1 uses the source-only prepare ticket and late finish; ordinary direct
restore retains its existing behavior.

After every record restores, the optional `reconnect` callback rebuilds source
leases and body/collision/target/pickup bindings against the candidate stores.
Final world restoration preserves retained link state and broadphase order;
session restoration resolves real source callback IDs and clock continuation.
The validation callback checks all service-specific references. Canonical actor
and ordered string bytes must still equal the saved foundation. Candidate
construction failure disposes the candidate. If teardown fails, the explicit
`retained_on_failure` output transfers the candidate for caller-owned retry;
the sole candidate pointer is never lost. Successful publication exchanges
the active application pointer once, with no allocations or source callbacks.
The displaced owner transfers to the caller for separate teardown. Candidate
options and callbacks must retain isolated external services until publication;
they cannot publish into active consumers or write user files during prepare.

`persistence_fields.h` adds shared explicit nested codecs for physics,
collision and attack provenance to the existing source IO implementation. They
cover every declared field, remap actor provenance, check enum domains and
encode only the active damage-cause union branch. Owning producers validate
semantic item/model/provider identities and the allowed numeric state. Nested
movement codecs cover each of the five active state/profile union branches,
ground provenance, signed source shorts, retained result fields and owned
contact arrays with explicit trace planes and surface fields.

The second packet adds `persistence_gameplay.h` and concrete version-1 owner
streams: `QACOMBAT`, `QAINVENT`, `QAPICKUP`, `QATARGET` and `QACTRLS`.
Combat retains raw primary authority separately from ordered protection
reservoirs and inventory-backed power cells. It records ordered policy
identities, callback capabilities, local damage admissions and protection
claims. External source values must match their decoded native/guest owner;
the shared codec never writes over that source memory. Power-cell dependencies
validate after inventory import.

Inventory retains canonical local/native-only entries and source primary
entries, count policies and capacities, ordered item admissions, labels,
actions, overlap rules and source group state. Pickup claims belong to the
pickup record, which encodes their actual registration relation, resource
storage serial, current/stale status, ordered rules and observation owners.
An inventory claim without a saved registration fails capture. Callback
descriptors come from the restored source owner. Inventory and pickup
restoration construct owned scratch stores; partial decoding remains reachable
by ordinary scratch cleanup, and failed application restore discards the
isolated candidate.

Item labels remain separately owned strings, matching actual inventory
admission. Their explicit byte fields decode into the scratch group's owned
allocation and never intern new values into the canonical saved string table.
Definitions-only weapon catalogs retain their native use callbacks. Stateful
item groups retain the separate admission restriction on weapon use.

Saved logical serial counters and lease numbers are retained inside the new
actor registry, preserving ordering and stale-resource tests. Their candidate
actor IDs replace the original handles, and inventory claim pointers are
allocated afresh. Duplicate, zero and out-of-namespace live lease identities
fail decode. Source descriptors adopt these candidate handles privately;
late native/mode/equipment reconnection validates current leases and creates
no replacement catalogs. Target descriptors preserve callback capabilities,
source dialect, raw authored fields and binding serials; their derived indices
are regenerated.

Controls retain active state/profile/result, standing/current bounds,
ground/water/view state, command sequence/buttons, cinematic provider identity,
saved mode/damageability/view, guest mode flags and rerelease `pml.origin`.
Every retained motion discontinuity has its own explicit body/view, deadline,
revision and reason. Active movement or deferred retirement fails capture.
Restoration loads continuation directly and invokes no input or source movement
callbacks. Before publication, all seven newly dispatched shared records are
recaptured and compared byte-for-byte with the saved records. Lease regeneration
or other reconnection changes therefore fail the candidate transaction.

Remaining B30 work includes concrete codecs and application consumers for
resources, campaign/hub worlds, progression, events, navigation/bots,
connections, prediction, presentation, audio, input and media. Complete
QVM/native module private continuations,
original save adapters, migrations, level-entry autosave consumers,
recovery/demo journal consumers, MVD/GTV and VCR remain required. The mandatory
owner interface provides no substitute for these implementations.

Scoped tracked and new-file whitespace inspection passed. Source inspection
traced configuration ticket consumption, cvar/native-Q1 candidate ticket
ownership, selected provider descriptors, failure disposal and allocation-free
pointer exchange. Runtime behavior remains unverified. The packet's source
identities are frozen in `/tmp/qa-persistence-20260930.sha256` for independent
review; subsequent repairs require a refreshed manifest. The second packet
also requires root registration of its five codec translation units and the
application-owned source descriptor constructor. Independent source review
does not substitute for the later permitted build/runtime qualification.

The next console packet adds `console_save.h` and `commands_save.c` for actual
`qa_console` continuation. It retains ordered command registrations, lifetime
contributions and documentation, aliases with source dialect, queued/deferred
chunks with original byte extents and offsets, script completion sentinels and
caller contexts, wait state, alias recursion count, retired owner/client IDs,
startup text and declared limits. Command bytes remain byte arrays, including
embedded NUL from content scripts. Copied text and documentation reuse the
existing cvar codec's owned field helpers without canonical string interning.
The actual console contains no independent time field; wait preserves its
source frame count.

The decoder constructs a scratch console while retaining candidate routing
callbacks. Existing installed command handlers must match the restored command
name, dispatch owner, registration lifetime and engine flag. Saved placeholders
with no handler remain source fallback declarations. Lifetime and context
resolvers must qualify actual owners and clients and map the captured actor
registry/publication correctly. They cannot turn stale contexts into a fresh
publication or execute commands. Canonical actor references remap through the
candidate session. Complete decode, owned lifetime checks and unchanged
candidate byte checks precede the dynamic state exchange; partial records are
reachable by the ordinary console destructor. `QACM` version 1 dispatches every
actual application/provider console using a sorted provider/role/seat scope,
including physical-console aliases. The decoder requires the exact candidate
console set and cross-checks the saved command generation against application
metadata. Connections restore before commands so qualified network client
identities can be available. Capture rechecks the actual command record after
producer validation; restore compares candidate command bytes before and after
final validation and rechecks shared continuations. The application descriptor
retains qualified foundation string-owner identities and the declared frontend
owner, upgrades only the exact saved current publication to the candidate actor
registry, and keeps stale publications inactive. Actual application constructors
currently declare scalar session/client zero; unqualified nonzero values fail
restoration and cannot be invented from connection slots. The generic owner
codec passed independent source review. Application scope and resolver helpers
were inspected separately; complete runtime qualification remains deferred.

The registration-owner accessor reads the actual ordinary command handler's
lifetime owner using its exact registered name and dispatch owner. Network
qualification uses this value instead of the public dispatch owner, which is
zero for its shared engine commands. It executes no callback and leaves the
output unchanged when the registration is absent. The final provider loop
also qualifies prepared guest primary inventory leases after shared inventory
import and native source finish, before application publication. The guest
finish checks all prepared bindings before promoting their private ownership.

Detached consumers may install their actual empty service routing and command
handlers through `prepare_services` immediately after restored application
creation and before content/provider construction. The candidate already owns
its foundation and holds the persisting operation lease. Failure follows the
same fallible disposal path, so callback resources stay alive until source
teardown succeeds, including a candidate returned through `retained_on_failure`.
The later `prepare_content` hook still prepares consumers that depend on the
selected map and providers.

`persistence_slots.h` exposes concrete save-file inspection and discovery using
the retained filesystem root. Each immediate regular `.sav` file is read through
the existing stable snapshot path and decoded with full digest and owner-set
validation. The resulting metadata describes the envelope and does not claim
installed-content compatibility. Discovery retains unreadable and malformed
rows with their actual error; memory or enumeration failure preserves the
caller's output. A missing directory yields an empty listing. The caller supplies
the real user save directory. Reserved transition names, child links and invalid
slot paths are excluded, and results sort by exact contained path.
