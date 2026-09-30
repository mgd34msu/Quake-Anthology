# Native terminal actor-release metadata, 2026-09-30

An already-poisoned native runner cannot answer `ENTITY_GET`. Actual session
world retirement invalidates canonical actor generations before release
observers run. The native host release observer previously attempted that remote
query before clearing its bindings, so genuine retirement itself failed on the
terminated source.

`qa_native_terminal_entity_table` now returns only the last parent-owned entity
table metadata. It requires a genuinely terminal isolated runner with drained
native callbacks and validates the cached count/capacity/storage against the
parent's slot allocation. It performs no transport, source call, memory read,
allocation or mutation. The returned source addresses have no admitted memory
use; the host uses the capacity solely to inspect existing parent bindings.

The terminal branch of `qa_native_host_actor_released` requires the actual host
canonical registry and an already-invalidated full actor generation. It clears
only exact matching owned, borrowed or world bindings and retained-client flags.
The genuine host-owned release callback still runs where supplied; a subsequent
table qualification also remains cache-only for the terminal source. Healthy
notifications retain the original table query and callback sequence. World
bindings are cleared here only for the terminal branch after real retirement.

The actual chain is `qa_session_retire_world` -> `qa_actors_clear` -> session
release observer -> application shared combat/inventory/world cleanup -> owner
component and other provider notifications -> host slot cleanup. A failure keeps
the provider graph alive for safe retry. The existing terminal destroy gate
continues to require actual retirement of every remaining cached generation.
Ordinary client cleanup still detaches genuine combat/inventory claims before
its exact borrowed-binding detach; a live source snapshot failure is preserved.

Terminal cgame map retirement likewise skips impossible original `Shutdown` only
after the actual host terminal-retired/idle qualifier passes. Healthy Shutdown
remains unchanged. Host destroy still uses its consume-versus-reject contract;
post-consumed failure clears only the consumed host pointer, retains the engine
and frontend lease, and the next retirement call releases those remaining owners.

Outer publication ordering remains a separate reviewed producer: drain owners
and run the before-world-change guard, retire healthy native/Q3 map services
while their live canonical world is available, perform actual canonical world
retirement, then retire terminal source metadata and remaining services before
provider detach. Moving healthy cleanup after actor invalidation would erase
their own client identities and skip original disconnect behavior. No source
snapshot is invented for a poisoned live primary.

Verification is source-only: full core/host query and actual canonical release
call paths inspected, scoped whitespace check and frozen SHA-256 inventory.
No compiler, parser, build, executable or test ran. This packet supplies native
metadata cleanup; the separate outer ordering and complete artifact-private
metadata remain necessary before any baseline completion claim.
