# Q3 guest round restart source packet

This packet implements the GAME lifecycle used by a retained Q3 round cut. It
retains the application, world, map geometry, frontend leases, source host,
logical client slots, and transport owners. The application executor separately
retires every canonical actor before reset and allocates new generations during
ordered client admission. This document records source inspection only. No
compiler, executable, test, generator, or gameplay run has occurred.

## Source contracts

The behavioral oracle is [Quake Anthology TS](https://github.com/mgd34msu/Quake-Anthology-TS).
Its actual guest constructor
passes `options.now()`, `options.seed`, and the map-restart flag to GAME_INIT in
`src/app/bootstrap/simulation/q3/guest-runtime.ts`. Its `timeMilliseconds` getter
returns the same source clock. The native built-in round path in
`src/app/bootstrap/simulation/runtime.ts` admits three 100 ms frames, reconnects
clients in slot order, then admits a fourth frame. Those frames call GAME at
ENTRY time before advancing the source clock at EXIT. Ordinary Q3 frames retain
their existing end-time convention.

The current TypeScript guest map-restart path constructs a fresh QVM owner.
There is no production caller of its native `Quake3VmModule` constructor that
proves retained native DLL globals equivalent. The retained-owner implementation
therefore uses the original Quake III SDK only for that genuine missing native
restart contract. `code/qcommon/vm.c` reloads a native library and resets QVM RAM;
`code/server/sv_game.c` calls GAME_SHUTDOWN with restart=true before restart and
GAME_INIT with restart=true afterward. `code/server/sv_ccmds.c` supplies the
three-plus-one frame sequence and connected clients, including clients that
have not entered the world yet. `code/server/sv_client.c` preserves the last
usercmd and calls ClientBegin without replaying ClientThink.

The original SDK samples a new initialization seed. The selected TypeScript
guest contract supplies its retained initial seed. This implementation retains
the exact initial native C guest seed producer and passes that same value to
restart initialization. It does not manufacture a replacement seed or reread a
mutable map artifact.

## Actual owner transitions

`guest_q3_restart.c` admits a completed, attached, initialized GAME owner with
idle source, host, input, world, and bot services. The complete connected source
inventory must have real canonical actor bindings. Connected clients need not
have completed ClientBegin. Pending bot admission, input motion, retirement,
script operations, and active navigation crossings reject the cut.

Successful ordinary GAME initialization and source-client drain capture the
actual loaded `g_gametype` and `sv_maxclients` integers and clear their real
modified flags. Fast restart compares current values against those retained
map-admission fields. Changed compatibility requests require ordinary map
replacement. The private guest codec preserves these fields with the original
initialization seed; missing source cvars never become fabricated defaults.

Begin records the connected slot mask and actual source-bot roster attachment
mask. Genuine GAME Shutdown(true) runs before canonical retirement, while old
clients, actors and bot command routes remain attached. It closes source input
hooks and permits only scoped source Shutdown callbacks to issue ClientCommand;
native admission additionally requires the active import and current instance.
Canonical retirement clears old actor bindings while retaining userinfo,
bot identity, entry time, last usercmd, local gamestate, snapshot rings, and
reliable histories. Reset runs under APPLICATION_CONFIGURING only after the
entire canonical registry is empty and the real source shutdown succeeded. It
clears retired projection contexts, closes actual portal claims, resets located
entity/client descriptors and source slots, and resets the parser over the same
owned entity text. The host keeps its services, console, cvars and external
frontend lifetime. Original input hooks are attached again after the source
image reset and before genuine Init(true).

QVM restart writes original immutable initialized bytes and zeroed BSS into the
same RAM owner and resets execution state. It retains qualified instruction
bindings on the unchanged original image. Native restart keeps the outer
`qa_native_instance` and host but genuinely unloads the old library, materializes
a fresh physical copy of the original admitted module and dependencies, and
binds its real dllEntry/GetGameAPI, exports, import tables, and ffi descriptions.
The dependencies are copied from the original creation arguments before those
borrowed arguments expire. No VFS lookup or current file read reconstructs them.
Their actual immutable ELF SONAMEs are captured with those bytes. Admission
rejects a shared declared filename or SONAME; after OS load, each matching
dependency mapping must resolve to its exact admitted private file before
source exports run. Undeclared platform dependencies remain platform services.
This resets original DLL globals, including the BotAI static owners that cannot
survive a retained-library Shutdown/Init shortcut.

Engine allocations remain in the stable native owner. Actual source shutdown
performs module frees; cached engine strings and cvar shadows remain valid.
Address-bound entry and write observers reject restart admission. Declaration
regions retain their immutable RVA identity and actual callback bindings; the
instrumented runner's real module unload/load events retire and rebind their
execution addresses. Its new wire operation executes the same checked reload
in the original child instance and returns the refreshed image identity. Wire
version 4 rejects a mismatched runner/client package.
The real instrumented image query qualifies its current base, extent, and
module-event generation at admission. Reload requires exactly the original
unload and fresh load events before rebinding source entries. A retained
NODELETE image cannot silently leave instruction callbacks on the old mapping.

Only a successful original reload permits the matching restart=true Init.
Failed shutdown consumes its call at the real source boundary and cannot
publish restart readiness. Failed reload leaves initialization unavailable.
The guest round owner latches the first failure and rejects ordinary source use
and private save capture during an unfinished or failed cut.

## Clients and clocks

Settlement consumes the actual `qa_session_active_frame` supplied by
`qa_session_round_step`. It requires Q3 ENTRY, a 100 ms interval, the original
retained source owner, the expected source start time, and an increasing actual
frame ordinal. It calls GAME_RUN_FRAME once. It does not invoke the ordinary
provider frame fanout or call BotAI_START_FRAME again. A guest that actually
allocates a fresh source bot during GAME_RUN_FRAME uses the existing completed
source bot/retirement admission under that exact active-frame scope.

Client publication runs under APPLICATION_CONFIGURING, with an IDLE admission
available for qualified outer callers. Each carried slot reconnects once in
ascending order after three completed source frames. The newly allocated actor
must match its actual source slot and retained human seat. GAME receives
ClientConnect(firstTime=false, isBot) and ClientBegin with the retained usercmd
available through the genuine source callback. The source's own userinfo writes
during restart remain authoritative. Accepted source bots restore their real
roster binding through the existing owning admission API.

Local-only clients queue `map_restart` in their actual retained reliable owner;
the outer transport cut queues the remote projection. Reconnect does not clear
the gamestate, snapshot/usercmd rings, reliable ACKs, or reliable command cursor.
A carried denial invokes genuine ClientDisconnect exactly once before canonical
retirement, even when Connect rejected before connected=true. The owned denial
text remains readable until actual client retirement drains it.

Pending restart deadlines and network time read the qualified source-provider
clock. GAME_RUN_FRAME arguments use that admitted frame's ENTRY time. These are
distinct during settlement: network/pending reads observe EXIT after the frame
completes, while GAME private level time remains its original ENTRY argument.
The world-replacement readers use that same installed EXIT clock and retained
initial seed without requiring unchanged fast-restart cvars. They copy the
actual connected source client, last usercmd, entry time, bot flag, and owned
userinfo before the old canonical generations are retired.
Original slow map replacement additionally captures the exact current and
latched cvar registry and surviving client userinfo after successful genuine
Shutdown(true). That source handoff precedes portal, client and actor retirement;
the replacement's actual Init receives restart=true, as the installed guest
contract requires.
Its actual Connect(false) is followed by installation of the accepted carried
raw usercmd before ClientBegin, matching the TypeScript guest begin caller.
This does not issue a ClientThink or advance command transport sequence. The
new connection retains its genuine admission timestamp; the guest transition
does not supply an old source timestamp.
Snapshot epoch flags belong to the real outer frontend/network producer.
The retained original GAME/CGAME extension also owns its source-local server
count bit in the guest engine. Actual round begin toggles that bit once before
GAME restart, as original `SV_MapRestart` does. Its initial zero value and exact
0/4 continuation are saved in the guest codec; ordinary local publication reads
this actual field. The transport's separately owned wire bit remains its real
network projection.
Local snapshot reads also preserve the donor's signed 32-bit parsed-entity
cursor qualification. A snapshot that has left the actual 2048-entity history
is unavailable even when its message still occupies the 32-message ring.

## Failure ownership and remaining integration

`qa_native_destroy_owned` and `qa_native_host_destroy_owned` clear their owner
pointer only after genuine consumption. An OS unload rejection retains the
original loader handle, import closures, artifact files, and every host callback
context for a later cleanup attempt. A source shutdown or runner transport fault
can still consume its owner, and the cleared pointer records that distinction.
Successful loader reference release also requires actual image retirement:
Linux checks its original path and base in `dl_iterate_phdr`, and Windows checks
the original address without acquiring another loader reference. Both also
qualify retirement of exact old private dependency paths, so retained dependency
globals cannot silently survive reload. An unacquired unrelated alias does not
hold a failed factory open. If an owned image remains mapped, cleanup retains
the host, closures, files, and contexts. Later
cleanup only observes a released handle and never releases that reference twice.
After the native owner genuinely clears, its retained Q3 host clears borrowed
native, memory, and located-record aliases before fallible host cleanup. A
later console or portal cleanup rejection therefore retains useful service
context without a dangling executor pointer.
QVM role destruction first qualifies source execution, write delivery,
publication, and lifecycle callback scopes. It destroys the VM while its source
host still exists, clears the borrowed VM and located-memory aliases, and only
then performs fallible host cleanup. Native Q2 application cleanup mirrors the
actual core lifecycle after Shutdown, so a source callback failure that already
consumed shutdown cannot strand the retained host behind another shutdown call.
QVM source attempt tracking starts at the actual first admitted opcode or bound
hook, after setup and trace admission. A partially performed GAME/CGAME/UI Init
therefore receives one Shutdown during cleanup; an unattempted rejection does
not manufacture an initialized source. Shutdown likewise preserves its marker
when argument setup rejects before source entry.
An unattempted Shutdown also retains genuine portal and projection bindings;
the retirement caller returns before mutating those source-visible services.
Native factory cleanup follows the same rule: a failed factory returns its
retained partial owner when the OS rejects cleanup, and actual producer guards
must prevent activation of that failed owner. Reconstruction abort retains its
whole borrowed service phase when target unload is rejected.
Runner cleanup requires actual OS process retirement. A successful wait or a
qualified already absent child consumes the connection; failed wait, termination
or handle closure retains it and its outer factory owner for a later cleanup.
Source protocol failure alone cannot stand in for that OS result.
Ordinary map replacement consumes already shut-down GAME/CGAME executors and
hosts after canonical retirement, while their real bot services remain alive.
It retains the actual role descriptors, original artifacts, paths and argument
owners for later ordinary role reconstruction. A live UI that actually borrows
the retiring bot runtime rejects that destruction cut. No source Shutdown is
replayed by this physical consumption step. Pure bot runtime destruction checks
every live provider's actual host borrow, including old failed owners retained
outside the current roster, before consuming any shared service state.

Standalone original CGAME/UI roles can borrow the constructed native GAME's
actual client-service owner through a typed lease. Detached construction resolves
the genuine candidate WORLD/ENTITIES binding in its candidate provider inventory;
it never fabricates attachment or source clients. Each selected human HUD/menu
seat owns its actual role executor and physical source slot. Client getters and
source Init wait for real Connect/Begin and published gamestate. Commands and
effects use the leased native GAME. CGAME reads that GAME's completed EXIT time;
UI keeps real frontend time. Command arguments stay in the actual original
role's token owner. Physical role destruction releases
that lease only after the executor and host are consumed, including failed close
continuations.

Travel shuts down native-bound UI roles before canonical retirement when their
actual GAME pointer or physical seat changes. After the old executors consume,
standalone client descriptors reconstruct against the admitted next topology
before map source Init; client-role Init still waits for source admission. The
QVM private envelope records the logical native source owner and physical slot,
and qualifies the real lease topology without gameplay reads while restored GAME
wire owners remain pending. Original native module private memory remains
unqualified. Original client prejoin binds only the real physical actor slot;
Connect/Begin keep their genuine lifecycle effects. The accepted first network
command enters the original source command row before Begin without increasing
its Think sequence, command history or transport acknowledgement.
Original continued configstrings retain the complete source command in the
8192-byte BIG_INFO_STRING owner, including its prefix, final quote and terminator;
its private codec qualifies that same actual command state.

Native Q2 frontend factories require idle parent and real nullable child heaps.
The actual last application release clears borrowed engine aliases but retains
the linked unbound row while parent/child captures or audio retirement prevent
cleanup. Pending frontend destruction retries that owned row before effects or
unlinking, so source/heap capture tokens do not outlive their storage.

Root owns canonical retirement, map and roster publication, the outer delayed
restart intent, local snapshot epoch, source bot cut, real network cut, rankings,
frontend binding, and new translation-unit registration. Native Quake Live's
core restart-ready lifecycle is represented, but the guest fast-cut entry still
requires the established Q3 vmMain entity/player ABI. Quake Live layout remains
unqualified. An original native GAME full private checkpoint also remains
unqualified; this restart lifecycle does not invent one.
