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
numbers, and held immutable download. The current session cold version is three.
Candidate callbacks rebind to the actual isolated Source and ContentGraph.
Resource and view IDs map to candidate ownership. The acquisition receipt is
checked against the retained view rather than reconstructed from the filesystem.
Restore installs no peer and starts no signon; the generic save owner installs
the actual restored peer together with its canonical membership and history.

The bootstrap cold leaf retains the actual challenge table, pending requests,
retry state, accepted download URL, real constructor generation and committed
client ID. Candidate callback inventories bind without calling identity,
prepare, commit, Source startup, transport send or admission. The upper
retirement owner clears that ID after its exact canonical client is detached.

## Required joins still open

Network owns generic runtime dispatch, physical Source claim binding, disconnect
ordering, upper bootstrap installation and generic cold peer registration.
Its checkpoint callers must supply the real active or candidate reference graph.
This lower owner supplies the native bootstrap, authenticated endpoint matching
and game-channel state. Root owns build registration and publication.

The application Q2 publisher owner must bind every host callback to the actual
compiled or external Source GAME. The client constructor must create the real
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
public ABI entity geometry, full actor references and held sound acquisitions
qualify those records. The existing raw message queue remains available to its
native receiver. Complete fog observer continuation, split-group authority and
the residual TEMP representation/consumer joins are still being implemented.

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
