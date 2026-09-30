# Q3 local snapshot publication

The ordinary application driver must publish one source snapshot after source,
player and mode completion. The final round driver must publish once after its
fourth settlement frame and client reactivation. Per-seat `DrawActiveFrame`
consumes the retained snapshots.

The behavioral references are `application.ts` lines 2438-2448 and 4569-4573,
`q3-client/client-state.ts` lines 80-89, and `network/q3/visibility.ts`.
The native implementation reads the original GAME's actual located entity and
player records, its retained link visibility, and shared collision geometry.
`application_q3_wire_time` reads the selected source provider's completed EXIT
clock. The GAME's earlier RUN_FRAME argument does not supply snapshot time.

Each local CGAME source client keeps its existing `q3g_client` snapshot message
sequence, reliable sequence, ping, and 32 retained snapshots. Publication
increments that actual message sequence. The complete source visible list is
limited to 256 entities by the source visibility algorithm itself. Its count
advances the source parse cursor with 32-bit wrap. The newest retained snapshot
stores both the starting cursor and the consumed count, so no second cursor
owner or checkpoint field is introduced.

The existing application guest checkpoint encodes those complete records,
retained entity allocation extents, sequences, and local role ownership. The
guest client reader also needs the source signed 32-bit parse-age check at 2048
entities. That reader change belongs to the guest lifecycle packet.

Modern original GAME event and entity words pass unchanged. The existing
explicit 1.16n ABI adapter translates its known source fields for the selected
CGAME ABI. All snapshots preserve the product from the actual GAME owner.

The reader requires an admitted, idle GAME source and a live connected, begun
client with the same physical source slot. It copies observations into the
existing local client owner and grants no body, input, collision or scheduling
authority. Failed publication follows the outer driver's failure handling.

This packet does not provide builtin Q3's complete wire producer. That producer
requires the real physical source allocator, full entity records, predictable
event rings, client persistent state and link-time visibility owners. The
renderer entity view and canonical actor slot cannot supply those contracts.

No configuration, compilation, test, game, parser, generator or executable was
run for this packet. Source review and integration of both real driver callers
are required before the global validation gate can open.
