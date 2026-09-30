# Bounded independent network source review

The tools owner inspected the network owner's frozen source packet and actual
runtime reconnect, browser admission, downloads and platform staging source.
This is a source review only. No compiler, parser, test, game or program ran.

Two confirmed defects were reported to the network owner. Authenticated
reconnect could bind an endpoint already occupied by another peer. The repair
checks all occupied peers, excluding the reconnecting peer, before invoking
authentication or changing the shared connection table or transport state.
The tools owner re-read that ordering in runtime/session.c.

Broadcast browser replies previously treated an arbitrary responding address
as LAN provenance. The repair requires the caller's actual locality predicate
for broadcast queries. Receive invokes it under the callback lifetime guard
before adding the response with QA_SERVER_LAN provenance. The tools owner
re-read query admission and receive in browser/owner.c and the public hook.

The attached hash manifest identifies those re-read files. The review found no
additional confirmed defect in the bounded staging/download source inspection.
This does not establish full network parity, all platform filesystem behavior,
live HTTP resume or baseline/B33 completion. The network owner independently
reviewed the tools HTTP/LLM callback packet and confirmed the repaired final-SSE
observer lifetime guard.

The continued review found the frontend locality callback still classified
RFC private address ranges as LAN. The network owner replaced that callback
with actual loopback qualification; the tools owner re-read that repair.
Physical LAN discovery still needs the retained transport owner's interface
and netmask producer. This review does not treat private address ranges as
proof of locality.

The tools owner also inspected canonical launch identity decode/match and
download installation pumping. Explicit actor references restore full saved
generations through the candidate registry. Choice records have size/type
bounds, duplicate selection admission is checked, and match compares the
prepared snapshot's complete canonical choice/content identity. Downloads
remount after shared HTTP callbacks have returned. No additional confirmed
defect was found in these bounded source slices.

The network packet check observed five files changed during continued work.
The owner was asked to refresh the packet. The review manifest records the
actual current files read here and does not silently reuse stale hashes.
