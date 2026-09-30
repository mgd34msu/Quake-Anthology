# Application roster and persistence producers, 2026-09-29

This source packet contributes to B30 and the canonical player roster. It does
not complete B30. No configure, compiler, parser, test, executable, game,
benchmark or sanitizer was run.

`map_players_private.h` owns the actual player roster and travel types. Native
local map admission and public remote admission use the same `publish_player`
implementation: canonical source actor allocation, selected native character
and arsenal admission, guest connect/begin, shared body/combat/inventory,
controls, mode membership, original spawn selection and configured loadout.
Remote requests retain full connection and seat identities, an application
seat and an explicit or automatically selected source slot. Seat/source
collisions fail before admission. Source callback failure after mutation faults
the application and leaves ownership for teardown; no allocating rollback is
claimed. Remote actors are looked up from this roster after map travel.

Travel copies active remote records and connection text into the next roster,
retains their source client indices and captures the actual canonical carry
state. Candidate local seat conflicts and source client conflicts fail
preparation. A row is reserved per canonical actor before publication, so
nested source bot admission cannot reallocate live player record addresses.
Detached dynamic rows become reusable empty rows, preserving active indices.

`application_players_guest_attach/detach` records supplementary guest bindings
by actual provider owner, launch digest, source slot and full canonical actor
identity. Source-created bots retain their already initialized source body,
combat and inventory. Shared native arsenal/Q3 role bindings, controls and
mode membership are admitted without replaying source connect/begin.

`QAPR` version 1 has a 52-byte header, 104 fixed bytes per roster row plus counted
texts and 40 bytes per guest binding, 60 bytes per authored point and 16 bytes
per Q1 point. Actor references are explicit generation/slot pairs with presence
flags, translated through the candidate checkpoint history. It retains canonical
and configured actors independently, remote client/seat generations, source and
client slots, character owner, names/team/skin/userinfo, deferred spawn deadlines,
guest identity bindings, ordered map spawn points and Q1 selector last point.
Restore constructs a separate roster, validates generations/owners/digests and
fields, rebuilds the Q1 selector with actual candidate services and publishes the
roster only after validation. It does not run player spawn/connect callbacks.

`save_private.h` exposes actual application foundation capture/decode/free and
finish. Root's isolated owner constructor calls
`application_save_session_create(options,image,out,error)` before any shared
owner borrows actors or strings. Saved capacities and mixed order come from the
session payload; callback contexts belong to the new application. Providers
rebuild authoritative bindings before `application_save_foundation_finish`
applies the world and resolves native Q1/QC scheduler callbacks.

`QAAP` version 1 has a 212-byte fixed header, counted stable catalog product
identities and 12 bytes per ordered mode identity. It explicitly stores the
31-word random continuation/front/rear/draw count, current map, lifecycle,
catalog/publication/command/map stamps, map presentation/geometry choices,
primary mode and readiness flags. Remaining controls, event queues, console,
campaign, audio, input, presentation and provider state belong to their actual
required codecs; this metadata never substitutes for them.

Complete configuration capture, decode from an empty draft and prepared content
validation use `qa_launch_identity_encode/decode/match`. Independent source
review found and reported its retired actor provenance mismatch; the network
owner repaired it before this consumer was connected. Original save actor
reallocation still needs an inverse saved identity mapping before canonical
byte matching can qualify those reordered scopes.

Two confirmed integration defects were reported to the owning workers: restored
owner failure teardown freed movement/control/visual arrays before live actor
release callbacks; source GAME_INIT admitted bots before the next roster was
transferred, which fails on initial maps and loses new records during travel.
Their repairs require independent reread. Source-created bot travel, foreign
native Q2 character admission and complete guest combat/engine/private-module
continuation still require their actual producer handoffs. Complete application
save publication remains unavailable until every required owner is represented.

The packet freezes source identities in
`/tmp/qa-persistence-application-20260929.sha256`. Whitespace metadata inspection
covers each listed source, including untracked files. Independent source review
is requested through the root. Runtime behavior is unverified.
