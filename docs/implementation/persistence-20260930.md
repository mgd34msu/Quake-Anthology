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

The navigation byte codec preserves the existing typed owner's map identity,
generation/world revision, area overrides, blocked edges and admission durations.
It bounds decoded tables by the candidate graph, consumes the complete stream,
then uses the owner's atomic node/edge validation and cache invalidation path.
Application construction must still admit the exact provider/profile graph.

The first bot asset packet explicitly encodes fuzzy-weight topology, names and
learned values, and each character's skill and active tagged characteristics.
Private resource strings remain owned asset text and do not change the canonical
string table. Exact floating bits retain the IEEE results allowed by the actual
character and weight owners. Decode checks complete extents and topology before
publishing ordinary reference-counted objects; failure reaches every partial
string/table allocation. This packet supplies real asset codecs for the later
runtime handle and private AI owner. It does not claim that the existing
same-binding bot checkpoint is a portable continuation.

The next bot asset packet covers actual weapon/projectile and item declarations,
including sparse physical weapon slots, source capacities/counts, named projectile
binding, inventory indices, timing/ballistics, bounding vectors and item respawn
metadata. Fixed source strings are counted values with their original bounds.
Table byte extents precede allocations, and the real immutable asset restorers
validate declared fields and rebuild selectors without source callbacks. Restored
weapon counts and projectile indices must agree with the saved declarations.
These resources support the later exact runtime handle/cache admission owner.
# Bot library variables and action inputs

The `QABVARS` version 1 record stores the real library variable table in source
list order, including owned name/text, text allocation capacity, exact numeric
value, flags and modified state. Decode constructs a detached table, rejects
duplicates using the original bounded ASCII name comparison, and rebuilds both
list and bucket order. Complete byte validation precedes replacement. No numeric
parser, source callback, canonical string allocation or asset-cache replacement
runs. Consumers of old variable pointers must be absent; the enclosing runtime
restorer must rebind its derived variable lookup pointers after import.

The `QABACTN` version 1 record stores the actual action capacity, initialized
state and every allocated input slot, including source jump-history flags,
think time, direction/speed, view angles and weapon. Shutdown preserves the
original capacity even with no allocated inputs, and the record retains that
state. Decode enforces the original source allocation limits and minimum byte
extent, consumes the complete stream and exchanges input storage while retaining
actual services. It invokes no command, setup, reset or frame callback.

Both are real private-owner building blocks for the remaining full bot runtime
and population record. They do not yet encode chat, goals, movement, BSP,
observations, shared asset cache identity or runtime handle tables. Source-only
checks cover field declarations, ownership on every failure path and scoped
whitespace; builds and executable checks remain deferred at the baseline gate.

The subsequent bot movement record captures every physical movement slot and
its complete source state, all avoid spots and saved walk progress. Its cached
variable bindings are qualified against the original fixed source role names;
an admitted variable from another role cannot replace gravity or a weapon
index. Legal absent bindings from incomplete setup remain absent. Actual
candidate navigation qualifies saved walk edges, and query scratch is
invalidated after complete decode. The observation record retains complete
physical entity fields and retired actor provenance, native numeric order, and
module insertion/hash/free topology. Goal snapshot arrays are derived at the
next source read rather than serialized as uninitialized scratch.

`QABREQS` stores actual constructor capacities and options for isolated runtime
and optional population construction. Owned decoded include/date/time strings
must survive the runtime's lifetime. Source BSP tables now have an explicit
codec preserving raw-source digest/size, exact record and property capacities,
duplicate epairs, quoted source spans and actual partial parse outcomes. It
does not invoke a lexer during restoration.

The remaining goal-owner codec needs the actual allocated level pool extent.
The private owner now records `level_capacity` at allocation, transfers it with
staged map publication and clears it with pool destruction. A reconfiguration
changes the next-map option while preserving the current pool, so that option
cannot certify the current allocation extent. This field follows the existing
allocation directly and adds no gameplay admission or callback.

The chat asset and system records now retain complete shared source tables,
cooldowns, ordered chat states, physical console messages and occupied/free
partition, revisions and actual asset identities. The runtime asset registry
captures all five real ordered library caches and handle-only references.
Distinct weight objects retain their learned values while sharing the original
immutable topology. Its retained construction references stay alive until
private owners import their exact registry references.

The complete goal record preserves current allocated level pools separately
from next-map policy, all physical handles/stacks/avoid entries, shared weight
wrappers and index maps, ordered map goals and retained source actor/name
bindings. Names keep their allocation capacities and exhausted goal-number
continuation. Source actor buckets rebuild using the actual producer hash in
the candidate namespace. Full decode and topology validation precede exchange
at the stable goal owner address; no navigation or source callback runs.

The `QABRUNT` version 1 record now retains complete runtime flags, clocks,
module presence and physical source handles. It frames the actual variable,
asset, action, BSP, goal, chat, movement and observation records, validates the
complete enclosing stream and qualifies saved map identity before importing
private state. Character handles retain unique source pointer identity; every
weapon selector must use the runtime's actual global weapon configuration.
Closed modules preserve absent libraries and child owners alongside retained
map, BSP, observations and shutdown action capacity. Import preserves stable
runtime and child service addresses and invokes no setup, map-load, command or
source frame callback. A failed import remains isolated and pending for ordinary
candidate teardown.

The `QABPOPU` version 1 record retains every physical AI state, player and
inventory field, activation slot, scheduler clock, command sequence and actual
actor/client lookup. Snapshot allocation extents are qualified against the
restored actor registry; combined ID/sort allocation ownership matches the
original producer. Each goal handle is qualified against its bot's actual
client. Historical actor/client lookup capacity may be smaller than the current
actor registry, and restoration preserves that extent. Complete stream and
owner topology validation precede candidate-only exchange. Failure cleanup
releases scratch storage without destroying shared runtime handles.

The runtime asset, goal, runtime continuation and population packets passed
independent source review, frozen manifest checks and scoped whitespace checks.
The application must construct these owners from retained `QABREQS` options,
qualify the actual map, provider, navigation and mode services, then import the
runtime before the population. The ordinary runtime constructor already creates
the empty action owner; restoration does not require ordinary setup. Application
aggregate registration and runtime qualification remain open, so these source
packets do not close `QA_SAVE_BOTS` or the B30 acceptance gate. Builds and
executable checks remain deferred by the baseline source-completion gate.

The application bot and navigation owners now have explicit `QABAPP` and
`QANAPP` version 1 records. They retain actual constructor requirements,
complete runtime/population bodies, original guest provider namespaces,
physical bot seats, metadata weapon handle, cached control values and retained
pickup snapshot extents/references. The bot script view records its actual
launch-view or installed catalog-product origin, ordered mount/archive/prefix
qualification and mutable reference flags. Restoration does not probe for a
default personality or replay script reads to reconstruct those flags. The
decoded requirement strings remain owned until after runtime destruction.

Every actual navigation graph retains its original selected asset resource and
mount ordinal. The immutable graph codec records the complete source-conditioned
topology and profile, while the application record retains shared graph order
and map/seat/historical target bindings. Actual asset edge declarations use one
pure producer helper in both ordinary graph construction and restore
qualification; source mode, flags, hints, entity bindings and timing cannot be
substituted under the same asset identity. Full decode precedes adjacency and
estimate-topology reconstruction. Query scratch is fresh, with saved retired
actor provenance retained in the facade binding. No geometry trace, source
entity callback or ordinary navigation construction runs during restore.

Candidate preparation requires the pinned map, actual shared world/physics,
empty modes owner and selected provider routing table before guest host bot
binding. It owns partial bot/navigation allocations immediately, so failed
source construction remains reachable through ordinary retryable application
teardown. After shared actors, roster, controls and modes restore, navigation
state imports before full runtime and population state. Final qualification
checks actual provider namespaces, metadata handle, unique seat actor/active
seat identities, roster/population agreement and shared graph bindings, then
recaptures complete bot/navigation bytes. Successful completion clears all
borrowed save-image spans. The immutable graph packet and application owner
packet passed independent source review, frozen hash checks and scoped whitespace
checks; the root application factory and coordinator integration also passed
independent source review. These checks remain separate from runtime acceptance.
B30 and B34 remain open until their complete acceptance gates are met.

Q3 host checkpoints now use internal wire version 4. They retain the actual
file allocator counter, every live file's unique lifetime serial, the script
generation and bot shutdown state. Exhausted counters stay exhausted after
restore. File/script allocations remain staged and reachable through ordinary
failure cleanup. Independent source review accepted this bounded repair. New
public Q3 field functions reuse the existing network serializers; private client
and server checkpoint callers keep their existing schemas.

The nested application `QAG3ST` version 1 owner retains all 64 physical Q3
client rows, their seat mappings, source actor provenance, complete reliable
character arrays, current and retained user commands, server/client gamestates,
snapshot histories and their retained entity allocation extents. It preserves
all source-conditioned IEEE bits through the actual modern Q3 ABI field table;
these are explicit field streams, with no C structure padding or process
pointers. Pending retirement, bot admission, disconnect, system information and
continued configstring fields survive. Token arrays restore into private owned
storage and retain their exact joined argument text. A complete stream is
required before exchanging candidate client state. Prepared immutable entity
text remains at its existing allocation because role hosts borrow it.

Saved role construction now accepts the actual captured service generation and
already restored foundation owner identity. It validates the exact provider
identity/generation string, rejects duplicate role lifetime identities, and
creates the existing empty host/executor against stable borrowed services.
Restoration neither increments the role allocator nor interns a replacement
owner. Ordinary role creation still allocates its own generation and retains
that exact identity on the role descriptor.

The coupled `QAG3PV` version 1 body now implements `qa.q3.qvm` version 1 with
backend `qvm` inside the original `QAGC` and application `QAPV` envelopes. It
retains the actual ordered immutable artifact cache, source manifest digests,
physical role inventory, service-owner generations, lifecycle flags, role
arguments and keys, original `QAVM` executor/host records, and projection actor
contexts with exact private inventory lease serials. A dedicated constructor
owns a copy of the complete record, validates the enclosing stream and nested
application state before creating services, and pins the actual selected and
companion artifacts against their saved digests. It preserves source entity
text at a stable allocation and checks it against the real immutable map.
Saved registration order creates genuine empty hosts and executors against
prepared frontend heaps; physical role and artifact list order is restored
independently. No original init, spawn, command, frame or admission runs.

Source import installs the original VM continuation and host source bindings
before shared store restoration. The host needs the actual immutable geometry
and actor registry at this point, then reconstructs external body/collision
callbacks without reading or writing the source. Late WORLD restoration checks
those callbacks and restores the saved body/link/portal state. `Q3GD` version 3
also retains idle source input-retirement admission for pending disconnects;
active motion remains outside the save boundary. Lease contexts import as
prepared claims, with no shared registration or replacement serial. The
existing exact primary-lease finish promotes them after shared inventory import.

Late provider finish checks the actual roster/client/source-slot relationships,
local or remote service admission, and every bound private lease. It requires
the application's real collective portal-owner validator, after WORLD/session,
roster/control/modes/bots and frontend owners have restored. It then captures
the complete source body and compares it byte for byte before enabling source
entry and releasing the owned record. Every partial owner remains attached to
the isolated provider until its ordinary, fallible teardown succeeds. Candidate
frontend heap addresses stay stable; frontend codecs restore numeric source
handle tables and rebind the immutable renderer world before final agreement.
The host font syscall exports glyph/image handle records, not font heap pointers.

The application schema/factory/late finish and final post-consumer provider
recapture require root integration and independent source review. Artifact and
manifest versions must match qualified candidate content. Foreign callback
contexts require their actual external service owner; the host inventory records
values and presence, never process addresses. Installed writable host streams,
shared script defines and retained nonzero foreign command contexts remain
explicitly ineligible until detached owners exist. Incoming host preflight
rejects writable file records before executor import can reach a file callback.
Native Q3 private data remains separate: its existing checkpoint is host-only,
and requires an actual module data/pointer relocation schema before admission.
No builds, execution checks, runtime qualification or whole B30/B34 completion
are claimed by this source packet.

## Preconstruction content graph

`QACG` version 1 records the actual application and launch resource pools,
retained catalog snapshots, private VFS views, and pointer aliases. The readonly
visitor adds installed frontend catalogs, views and private pools. Provider
rows retain their original source catalog and complete source selection,
artifact/declaration resource IDs, ordered interface resources and selected
behavior ordinals, implementation identity, and the actual construction product
catalog. Physical launch resources retain their original order and IDs.

The enclosing stream and ownership edges finish before native content
admission. Real pools restore before views; catalogs adopt their one graph view
and qualify physical paths, kinds, digests and archive members against those
already admitted immutable archives. This path performs no catalog discovery,
mount-plan reconstruction or resource acquisition. Catalog-owned views remain
borrowed aliases. Each private pool or standalone view transfers once into its
real consumer; catalog holders acquire actual retained references. Partial
objects and metadata remain reachable through the isolated graph destructor.

Final candidate capture requires the same pointer ordinals and aliases and
complete consumer claims. Mapped native handles may have different filesystem
identity fields, so each original view record has an admission baseline: a
fresh complete VFS checkpoint must still equal that baseline before its saved
portable record is emitted. Pools, catalogs and provider metadata are captured
from their actual owners. Successful publication ends this normalization and
destroys registry metadata and extra references after all real holders own
their content. Later saves capture the current source state.

The application preloader, direct saved configuration construction, frontend
early ownership claims and final publication callers require independent source
review. The native Q3 private-module relocation dependency remains open. No
builds, tests or executable checks were run for this bounded graph packet.

## Retained Q3 READ descriptors

Q3 host restoration now adopts the already saved immutable READ file bytes,
path, digest, cursor, ZIP admission and lifetime serial into the host's actual
owned descriptor. It uses the existing retained-version representation without
opening a path or acquiring a VFS resource. A mapped loose-file identity can no
longer allocate a replacement cache ID or mark new mount references during
private import. Read and seek consume the same saved bytes and cursor; close
and failed staged import free the owned path/buffer. Re-capture emits the same
wire fields. Future ordinary file opens retain their existing native filesystem
path. The coupled guest preflight still rejects saved writable descriptors.

This packet changes no host or executor schema. Source-only ownership, cursor,
digest and re-capture paths were reviewed; no executable checks were run.

## Rankings and durable player progress

`QARK` version 1 preserves the actual idle rankings lifecycle: configured
provider and observer identities, endpoint/context/hook presence, service state,
match value and ownership flag, ordered player slots and complete account/reason
fields, and retained allocation capacity. Every occupied slot is explicit;
unused allocation extent is represented by false partition tags rather than
reading uninitialized realloc tails. Denied accounts retain source IEEE values,
including a nonfinite rank rejected by the original provider admission. Active
accounts require their original finite rank and unique account identity.

Configured providers and observers require readonly logical binding callbacks
to real candidate services. A provider must qualify its genuine backend against
the entire saved match/account continuation. Restore does not begin, log in,
join, poll, report, log out or finish a match. The real empty restored constructor
gates ordinary lifecycle operations; failed candidate destruction performs no
backend or observer callbacks. Publication ungates the candidate, and a replaced
source can relinquish its continuation after a successful external backend
ownership handoff so later teardown cannot finish the continued match. The
actual default application has an unconfigured backend and no observers.

`QAPP` version 1 preserves the actual progress store's private string dictionary
and IDs (including unused strings retained after a failed admission), ordered
rows, retained row allocation extent, complete physical hash table, durable
replacement nonce and reload-required flag. Hash topology is checked with the
same source hash/probe helpers, without replaying records. Counted UTF-8 strings
retain embedded NUL bytes. Progress restore creates its stable empty owner and
private dictionary without opening, reloading or writing the progress file.

The real retained destination root is qualified against the saved profile root
by native directory identity or an explicit readonly mapping resolver. Pending
captures first recheck its actual admitted path and identity, then emit the
saved portable root fields; publication discards that admission metadata.
The exact relative path stays with the owner. Record and reload remain gated
until whole-candidate publication. Every framing/row/hash check completes
before stable private ownership exchange. Optional progress presence belongs
to the application's genuine installed inventory, not an empty fallback.

Arena progression has no installed application facade in current production
callers. Its existing scores, awards and videos are actual archived cvars;
there is no second private progression store to fabricate. Root aggregate
construction, QA_SAVE_PROGRESSION registration, backend ownership transfer and
final whole-owner recapture remain separate integration requirements. These
owner codecs received source-only checks; no builds or executable tests ran.

The concrete application progression owner now wraps those actual owners in
`QAPR` version 1. Its fixed inventory contains rankings once and progress only
when the real profile root is installed. Enclosing presence, extent and trailing
byte checks finish before any nested owner changes. Early preparation checks
saved configured-backend/profile presence against the actual restored options;
configured embeddings require both complete readonly backend refs and a real
final ownership handoff. Root constructs the genuine pending owners before
external consumers and imports their full private records under its candidate
lease. Final validation re-captures and compares the entire original record.

The backend handoff is the last fallible operation after all candidate owners
validate. Both rankings lifecycles are fenced during that external callback,
which must preserve backend ownership on failure. Its explicit success result
distinguishes an active match transferred into the candidate from an unrelated
old match; only a genuinely transferred old continuation is relinquished.
Ordinary old teardown still closes unrelated matches. Publication then ungates
the candidate rankings/progress owners with no source callbacks or allocation.
The application now registers `qa.progression.application` version 1 and routes
capture/import to these actual owners. Mandatory outer-record preparation runs
before the content graph or application constructor. Restored construction
installs the pending rankings and optional progress heaps before any consumer;
it never reloads the progress file. Original native baseline reconstruction
also uses pending empty heaps, with an unconfigured rankings backend, so its
scratch application cannot alias or close the active ranking continuation.
That baseline has no ranking/progress consumer or private record import.

Capture compares progression again after external and shared/content checks.
Restore compares the whole record after all provider/frontend/shared owners
and content ownership readiness, then checks both applications' publication
conditions. Both applications retain the persistence lease during the external
handoff, preventing ordinary application reentry as well as rankings lifecycle
reentry. The guarded backend handoff is the last fallible call; its success
is followed only by progression publication, content admission metadata release
and the application pointer exchange. Failure keeps the source active and the
candidate pending for ordinary or retained retryable disposal. The progress
file itself remains the real
qualified profile backend: preserved `reload_required` causes its next ordinary
durable recovery to read that backend, without candidate-side file replay.

The application metadata now preserves the real completed outer-frame revision
in `QAAP` version 3 (220-byte fixed header). The actual frontend reports that
revision after successful output work and before map/restart intent draining;
source advance and restart settlement do not increment it. The owner rejects
busy/retiring/stopping frames and counter exhaustion. The frontend stopping
iteration omits this notification, preserving ordinary quit behavior. These
caller changes received source/diff checks only; executable qualification and
the remaining installed frontend/native owners remain open.
