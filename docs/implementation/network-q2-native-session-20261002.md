# Native Q2 session and cold wire ownership

This implementation supplies native Q2 bootstrap and game-channel ownership
beneath the existing network runtime. Source factories, upper admission,
frontend operations and the shared content graph are being joined by their
owners. B27–B34 completion and executable behavior remain unverified.
The resumed user instruction keeps implementation moving without review gates,
packet freezes or new test infrastructure. No executable validation ran.

## Live ownership

`network_q2_session.h` admits only an existing, genuine `qa_net_connect` through
the sole runtime. The runtime owns transport polling, connection generations,
canonical seat membership, and command authority. The Q2 session owns native
channel sequencing, reliable queues, fragments, signon pages, command replay,
wire frame history, decoded service records, and one hosted download holder.
It creates no simulation world or Source clock.

Initial signon queues serverdata and its config request as one packet. Each
config or baseline page queues its records and continuation together. Encoding
uses a copy of the negotiated codec; queue failure retains the prior codec,
signon, replay, frame and download state. Initial canonical restart and lower
resets commit only after the complete signon packet is accepted.
The nofail acceptance hook then adopts already allocated HOST config
beforeimages. A game-state loan keeps its staged beforeimages separate from
the last accepted namespace; the HOST capsule retains both extents.

The server hooks require an actual physical GAME player receipt with canonical
full actor, Source owner, and Source edict. The host supplies real configstrings,
baselines, frame players, recipient filtering, and Source input application.
Its console expands macros before signon or gameplay routing. Source owns
userinfo application, begin, gameplay commands, and retirement. The real lower
connection retains native raw userinfo and applies Q2PRO info deltas before the
Source callback. It owns the fourteen engine settings, emits the actual FPS
setting response, and paces client wire frames from the actual Source interval.
Wire-only player and entity filters clone immutable Source publication and keep
the actual acknowledged wire history; they create no gameplay state.

The bootstrap retains authenticated pending connect requests during the sole
transport pump. At the enclosing idle point it prepares the genuine Source
claim, attaches canonical membership, commits the allocated client ID into the
real Source roster, applies initial userinfo, and replies client_connect.
Native KEX admission requires the actual LAN proof and ordered social IDs.
Client handshake retries refresh genuine local identity and wait for actual
LAN readiness. The real remote CLIENT constructor can retain a preparation
generation until it supplies its actual canonical claim and hooks.

Fresh native CLIENT downloads now allocate an actual exclusive filesystem stage
through the enclosing Network namespace. The allocator advances past real
staging-name collisions without borrowing or deleting another owner. Cold import
uses a separate preparation namespace and excludes the retained logical nonce.
The physical Source and CLIENT constructors both bind these actual stage owners.

An accepted native block retains its owned bytes, immutable destination offset,
written prefix cursor, committed byte count, and seal/publication/refresh stages.
A failed write resumes only its remaining suffix; a failed `nextdl` queue retains
the already written block without appending it again. The lower client retains
that exact service record and receive batch for recoverable I/O/allocation/queue
failure instead of retiring the session. Invalid protocol records retain their
existing retirement policy. Q2RC version fourteen includes this continuation and the actual accepted
stage prefix. Candidate reconstruction writes only an unpublished private prefix;
an accepted publication instead authenticates the actual contained installed target
against all saved bytes and retains that target's sealed identity. Import sends
no publication, cleanup or sync operation. Ordinary continuation finishes the
held filesystem durability work before content refresh and checked retirement.
The completed refresh is retained when checked retirement must retry. Installed
publication is recorded independently of a later directory cleanup/sync failure.

The pure constructor recipe codec reuses the lower request and CLIENT policy
field codecs. It retains endpoint, protocol, qport, physical seat, negotiated
request, policy and composition as values. An unselected policy remains literal
empty state; selected policies retain the constructor's zero history and inflate
defaults and require its real config, inventory and command extents. Process
reader and context pointers are rejected. No Source, content graph or admission
callback is invoked by these field codecs.

The bootstrap handles native challenge/connect, ping and Source-backed
status/info replies. Unrecognized binary master/browser, other-family and
rcon payloads remain with the enclosing connectionless dispatcher. There is
one borrowed runtime transport and no second receive loop.

The client retains the complete owned service batch before calling Source
outside transport polling. Each retained frame receives that complete batch,
the original receive time, and the actual connection identity. Raw KEX service
seat markers remain 0 for broadcast and 1 through 8 for selected recipients.
They do not establish a canonical actor or lobby player.

Directory selection and game preparation have retained operation generations.
WAITING holds polling until the actual external operation can resume. RECEIVING
allows the current batch and subsequent native download blocks to arrive while
the real content operation remains pending. Source startup must run once in
that operation. The client sends begin only after the actual preparation reports
READY. Remaining stufftext requires the genuine client Source command binding.

`qa_network_q2_client_serverdata` reads the actual accepted serverdata. Classic
wire fps remains zero because that field is absent; the authentic Source profile
defines its interval. Rerelease or KEX wire fps remains the received value.
API3 versus API2023 presentation is a Source profile decision.

Complete command groups receive an independent logical command number when
the real lower queue admits them. The sent receipt carries that number and
the actual packet sequence. Reliable fragmentation keeps unsent groups queued;
it emits no sent receipt until the command datagram was actually included.
Rerelease prediction can therefore retain its true packet-to-command mapping.
If a sent notification fails, the lower owner retains that accepted packet,
command group, send time and per-seat completion cursor. It retries only the
unfinished Source notifications before consuming further received messages;
it does not resend the accepted command. This continuation is cold-saved.

Slower Q2PRO client frames consume the genuine whole Source motion receipt.
Its physical actor and edict, link count, creation frame/origin and eight
link-time history cells are independent of recipient visibility. The real
Source producer and its cold owner retain those facts. Lower frame projection
uses the native old-origin rules, while Source pacing retains the full
64-bit Source counter separately from the wire frame word.

Hosted download policy reads the actual Source cvars and mounted VFS. One
immutable resource and its full acquisition receipt remain held across native
1024-byte blocks. Archive map restrictions inspect the actual selected member.
Download replacement, signon restart, retirement, and completion release that
holder. The inventory getter exposes the held view, resource, and acquisition
for the enclosing ContentGraph capture lease. It never reopens a path.
Qualified Source model downloads may also own nonempty derived wire bytes,
such as a model with qualified external skin names. Transfers seek and chunk
those bytes while the original immutable resource and opening remain held as
provenance; this does not create a synthetic Source resource or file.

## Cold continuation

`network_q2_wire_save.h` writes explicit native fields. It preserves exact float
bits, actual channel sequences and retry times, queue and fragment extents,
physical frame slots and ring cursor, decoder configs including explicit empty
strings, baselines, stream state, rich records, and mutable negotiated codec
fields. Q2PRO serverdata flags belong to the mutable codec; the admitted channel
identity retains its original handshake flags.

An unfinished compressed native download retains its accepted compressed
segments and each actual output extent. Cold decoding reconstructs only the
private deflate dictionary and partial codec state from those segments. It
discards decoded bytes and emits no service, gameplay, startup, admission,
publication, command, or transport callback. Host zlib pointers are never saved.

`network_q2_session_save.h` saves the lower session's signon, replay counters,
command backup and allocated command storage, retained batch cursor and stufftext
offset, loading generation, preparation phase, native userinfo/settings,
Source interval and protocol frame counters, independent logical command
numbers, staged disconnect reason/queue/send/retirement marker, and held immutable
download and its optional derived wire artifact. Genuine memory catalog
downloads retain their owned Source bytes and actual view without a file
resource or fabricated acquisition. The current session cold
version is fourteen. Candidate callbacks receive
the actual isolated runtime and rebind to its genuine Source and ContentGraph.
Resource and view IDs map to candidate ownership. The acquisition receipt is
checked against the retained view rather than reconstructed from the filesystem.
Restore installs no peer and starts no signon; the generic save owner installs
the actual restored peer together with its canonical membership and history.

The bootstrap cold leaf retains the actual challenge table, pending requests,
retry state, accepted download URL, real constructor generation and committed
client ID. Candidate callback inventories bind without calling identity,
prepare, commit, Source startup, transport send or admission. The upper
retirement owner clears that ID after its exact canonical client is detached.

`network_q2_host_save.c` owns the frontend HOST capsule. It retains real
ordered canonical connection claims and epochs, full Source actor receipts,
one-human LOCAL groups, config beforeimages, pending event packets and journal
cursors, the bootstrap, and the Source/frame-qualified unicast cache. The
cache includes the actual map revision, distinguishing another load of the
same BSP when Source frame and time restart. HOST capsule version five rebinds
that revision through its genuine saved current Source anchor. The
capsule records offline `local_only` explicitly. It restores custody before
the generic runtime admits the saved claims; callbacks bind to that actual
candidate runtime without readmission or GAME startup.

Partial map travel preserves the preceding publication as an archival owner
and separately retains any current candidate publication and install cursor.
Each LOCAL connection retains its actual preceding Source map revision until
the genuine local refresh cursor runs. Checkpoint and import use its retained
player receipt; ordinary commands and flushes still require the current human
actor. The capsule distinguishes historical receipts explicitly when map
revision values are rebound in the isolated candidate.
Each LOCAL claim also saves its actual canonical composition, which changes
only when that connection's restart succeeds rather than at the discovery
publisher's earlier install boundary.
An archive owns its prior resource bytes and numbering and supplies no live
Source callbacks. Process-only current binders transfer at the ordinary travel
creation boundary. Failed import cleanup frees custody without replaying
Source disconnect. The parent Network owner installs this capsule in QANF24.

Q2PRO frames retain an explicit client-number-field receipt, distinguishing a
real chase player zero from an omitted field. The decoder seeds the first
effective identity from actual serverdata and inherits later omitted identity
from the delta base. Actual serverdata flags and minor version also select the
negotiated config namespace, including the enhanced 13,630-entry layout. The
namespace allocation survives reset and cold capture independently of the
initial decoder policy.

Original Source precache registers an exact held resource, view and issued
opening. EVENTS20 preserves additional custody receipts when the same
immutable ResourceKey came from different genuine mount or link recipes.
Protocol records capture the key and selected custody at append time; HOST
transcoding consumes that exact receipt without selecting a later opening.
TEMP operands also retain their real primitive byte offsets and captured full
actors. Stufftext presentation and simulation records are linked at emission.

## Integration and remaining validation

Network supplies generic runtime dispatch, physical Source claim binding,
disconnect ordering, upper bootstrap installation and generic cold peer
registration. Its checkpoint callers supply the active or candidate reference
graph, including held download resource and view identities.
This lower owner supplies the native bootstrap, authenticated endpoint matching
and game-channel state. Root owns build registration and publication.

The application Q2 publisher binds host callbacks to the actual compiled or
external Source GAME. Its Original API2023 to PRIVATE4038 projection preserves
the real Source movement domain while mapping configs and resource indices into
the target codec's namespace. Per-peer resources retain original acquisitions;
the projection does not change SDK tables. LAYOUT masks retain the captured
recipient and actual API3 or API2023 grammar. NativeHost now invokes the real
rerelease SDK `Entity_IsVisibleToPlayer` export during this Source's returned
end-frame stage. It reconciles and requalifies both full physical bindings and
the Source frame around the call. The pure stage entity getter observes that
same admitted frame. The publisher owner is joining retained visibility and
first-viewer readiness; `instance_bits` does not establish a physical client
mask.
The client constructor must create the real
remote Source/view owner, console context, content preparation and download
consumer, frame observer, presentation timing, and teardown. A selected character
or frontend seat ordinal supplies none of those receipts.

Native KEX uses the actual LAN wrapper's admitted ordered lobby player IDs and
local join IDs. Those IDs must join genuine Source players and canonical network
membership. The lower Q2 session consumes that admission and borrows the existing
transport wrapper. The generic runtime now reserves eight native KEX seats;
other wire families retain their own capacity.

The application publisher now supplies authentic whole Source motion facts
through `qa_application_network_q2_motion`; the upper frame caller joins that
receipt with the actual per-client frame. Source link-time state, rather than
wire visibility history, owns native old-origin recovery.

The new `unified_q2_events` leaf projects original GAME service records at
append time into independent presentation and simulation streams. One packet
can publish multiple genuine events. Actual captured recipients, Source clock,
public ABI entity geometry, full actor references and actual sound precache registrations
qualify those records. The existing raw message queue remains available to its
native receiver. The Original client owns retained partial fog state, layout
and inventory with actual map/admission/retirement resets and cold continuation.
Service seat markers qualify genuine canonical connection groups. Rerelease
duplicate keys use the shared HOST cache, checked before actual raw and normalized
packet publication and remembered afterward. The pure cache has explicit graph
capture/restore and is retained by the installed HOST cold wrapper.

Original `WriteEntity` records its full actor and exact byte offset at the real
write, with the Engine namespace number supplied by the genuine Source bridge.
Pending NativeHost messages save these references through checkpoint actor
history, including retired provenance. The append-time observer follows that
full identity to its actual current SDK binding when geometry is still live.
It never turns a reused entity slot into another actor's reference.

The actual Original import observer also captures primitive muzzle, beam and
sound entity words from the executing SDK table before queue publication.
Existing `WriteEntity` receipts retain their original identity and offset;
sustain IDs and magnitudes remain literal fields. Source precache registration
and emitted resource spelling/key receipts prevent later configstring reuse
from changing a copied sound or image message. EVENTS retains those receipts
and validates their immutable resource keys against the retained dictionary.

`frontend/network_q2_events` decodes copied Original Source packets and
reencodes each record with the admitted lower server codec. Captured connection
IDs, epochs, seats and remote indices determine native KEX service markers.
Foreign full actors and resources pass through the actual HOST namespace
producer. Network owns queue acceptance and its journal cursor continuation.

Original sound reads the actual import-time SDK origin, bounds, solid and server
flags. It preserves native channel, reliability and PHS rules and captures the
actual multicast or unicast recipients before publishing. Frontend audio uses
those full recipient identities. API2023 Source sound uses its native index-width
flag, while actual KEX wire retains its word index; the decoder records this
Source-only distinction explicitly. Lossless residual TEMP records preserve all
decoded fields and genuine full actor references; their actual remote and
normalized consumer joins remain with the corresponding owners.

Hosted disconnect retries retain one reason and atomically queued notice. The
retiring channel accepts only native acknowledgements, progresses actual pending
fragments and calls its enclosing retirement marker once after notice delivery.
An incomplete reliable notice returns pending progress without inventing an
I/O error; actual transport and Source callback errors remain failures.
An incoming native client disconnect retains its reason and Source retirement
callback separately, with no outgoing notice. A failed Source callback remains
owned for retry and cold recovery.
Production channel send commits its scalar sequencing only after transport
acceptance; failed sends keep their genuine queued and fragment backing bytes.
Server frame cursors and history also commit only for actually included datagrams.

B29 still requires the complete remote content consumer, HTTP/native policy,
metadata and package handling, discovery, master heartbeat, status/info replies,
rcon, browser and hosting operations, and cleanup. This packet's immutable host
download sender is one part of that work. None of these required functionalities
is declared unsupported or exempt.

## Source evidence

The behavioral callers are `quake-typescript/src/app/bootstrap/network/q2.ts`,
`q2-client-receiver.ts`, `q2-downloads.ts`, the simulation Q2 GAME hosts,
`quake-typescript/src/network/q2` channel, codecs, command replay and service
readers, and the original Q2 `server/sv_user.c` and `qcommon/net_chan.c`.
Rerelease and KEX donors retain their own protocol fields and authentic seat
markers. Q2PRO engine settings and info handling additionally read the actual
q2repro server/user.c, server/entities.c, server/send.c, common/msg.c and
shared/shared.c. Classic PM_DEAD is wire value two; the rerelease movement
domain uses four. Generic runtime and persistence callers are read dependencies.

The packet includes the new session and cold leaves, three actual private state
headers, bootstrap/control/cold leaves, and the complete existing Q2
channel/messages/frames/handshake files under the authorized extraction loans.
The existing wire public header and fog body change
only the conflicting wire type name to `qa_q2_wire_fog`; `qa_q2_fog_read/write`
symbols and the separate gameplay `qa_q2_fog` type remain intact. The independent
source reader completed the original twenty-four-file lower read alongside
implementation. Its two download findings were repaired: native empty files
emit percentage zero, and positive numeric offset overflow saturates before
the file-length clamp. New bootstrap and settings bodies continue to receive
source feedback without holding implementation. Integration, build checks,
runtime behavior, performance, and final project completion remain unverified.

## Recovery compiler validation

The resumed production compiler commands were run with `-fsyntax-only` for the
Network owner, Q2 CLIENT/Source/download/presentation/restore owners, lower Q2
client delivery, Q2 HOST and CLIENT graph codecs, Unified graph and HUD, and
application Q2 frame publication. All checked translation units passed the
actual strict production flags. This validates their compilation boundaries;
no game, remote transfer, cold load, campaign or multiplayer execution was run
in this worker.

## Returned prepared Unified frames

The actual Unified family leaves now retain their pending FRAME without executing
prepare again during import: Q1 fields version 4, Q2 `QUQ34`, normalized Q3
`QUQ35`, events `QUEV6`, and RR HUD fields version 2. They save the actual pending
document and its prepared clock/receipt. Q1 and Q3 rebind their borrowed input
pointer to the parent's decoded FRAME after comparing the complete encoded
pending document; Q2 and events qualify their owned pending documents against
that same parent. RR HUD retains its separate prepared document and clock.

Their checkpoint predicates require returned callbacks while admitting the
actual pending token. Ordinary idle predicates continue to reject preparation.
QURP checkpoint and restored-session binding require the actual presentation
`checkpoint_returned` callback; physical sampling remains subject to ordinary
idle. A failed lower import aborts its pending token before checked destruction.
The five changed family translation units and the QURP/Network graph callers
passed the strict production compiler commands. Runtime pending-frame cold
capture/publication has not been exercised by this worker.

The actual Network import-abort dispatcher now resolves Q2 through its physical
application Source association, its Network Q2 owner, and the separate retained
Q2 Source receipt. Generic native sources retain their existing typed producer.
Neutral pre-QINS cleanup calls that dispatcher. The Source receipt owner retains
actual unfinished import and successful unclaimed-release discard facts; it
makes no completed recipient or command-authority claim.
