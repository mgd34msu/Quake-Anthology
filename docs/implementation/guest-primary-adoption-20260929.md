# Guest primary inventory adoption, 2026-09-29

This packet is source implementation under B23, B24, B25 and B34. It does not close those tasks. No configure, compiler, parser, test, executable, sanitizer or benchmark was run.

`qa_inventory_adopt_primary` admits an external primary on an existing local store. Overlapping canonical counts transfer into the original source entry's declared capacity and arithmetic policy. Values that the source cannot represent without changing the count fail admission. Native-only entries stay in the same store. Item groups, definitions, pickup claims and their leases retain their original owners. A conflicting live external primary is rejected. The exact same binding can be admitted again without replacing its lease.

Source readers and writes are checked against actor generation and inventory revision. Admission requires no borrowed store callbacks. All source values and adopted values are prepared before writes. If a source write fails while the store is still current and unchanged, earlier count writes are restored from the snapshot. Rollback failure remains a failed admission. Source callbacks that invalidate the actor or change inventory cannot authorize publication of the binding.

`qa_inventory_detach_primary` requires the exact actor generation, primary serial and context. It snapshots every source primary entry, retains native-only entries and existing groups, then changes the same store back to local storage and advances its storage identity. Source snapshot failure leaves the binding attached so its context cannot be freed as if detachment succeeded.

Client primary adoption now follows original source ClientBegin or the source-internal bot admission boundary. Initial actor binding occurs before source player initialization, so adopting there would copy counts before original source spawn resets its inventory. Primary readers use the full actor-to-source-slot binding instead of accepting new input, permitting a final inventory snapshot after input retirement.

`qa_q3_host_detach_actor` removes a generation-matched borrowed slot only after source calls, host calls, world callbacks and input motion drain. Borrowed slots never installed the host's body/collision bindings, so detach clears only their source projection and does not release or unlink the canonical actor. Application client drain snapshots its primary before clearing the source projection. Actual canonical actor release remains the owning provider's job.

Denied client connections queue cleanup. Ordinary disconnect consumes the original callback once and drains cleanup even if that callback failed. A pending source bot can also be freed before its admission boundary. Map retirement detaches live primary stores while their source records still exist, before provider teardown.

The root owns production roster hooks `application_players_guest_attach(app, provider, source_slot, actor, info, error)` and `application_players_guest_detach(app, provider, source_slot, actor, error)`. Their declarations remain in `guest_projection_private.h` until shared declaration integration. Dynamic roster routing, complete source combat callbacks, public inventory profiles, native private projection, guest continuation codecs and one shared bot runtime with real navigation remain required work. No unsupported guard is treated as completed functionality.

The exact eleven-file freeze is `/tmp/qa-primary-adoption-20260929.sha256`. Tracked whitespace inspection passed. Independent peer source review is requested through the root. Runtime behavior remains unverified.
