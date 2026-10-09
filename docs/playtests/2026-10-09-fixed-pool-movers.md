# Fixed storage for spatial snapshots and movers

THE-2874 moves spatial snapshots and Q1/Q2/Q3 mover scratch onto `qa_pool`, backed by the existing arena. Owners reserve and seal storage during load. Leases, candidate collection and rollback do not grow storage during play; exhaustion returns the existing operation failure and increments a bounded counter.

The spatial snapshot free list and Q3's separate rollback allocation are removed. Spatial queries retain their eight-actor stack path. Default reservations admit 32 nested leases; mover owners can reserve a larger rollback budget because team parts may save an actor more than once. Q3 retains its original 1,024-entry limit.

Verification:

- The isolated staged source at `f0bb5cc31c155f3f73bd2255ed1d3fbf3fe5c5a0`, based on `a570f13a`, built fully in production and ASan configurations. All seven configured core checks passed in both.
- GCC and Clang, with and without ASan/UBSan, ran the actual old and new mover implementations. The 3,854-case fixture compared float words, bodies, links, ground state, trajectories and callback order. Its wrapped hot allocation count fell from 18,092 allocations and 5,064 frees to zero; this is a component measurement, not a frame-time claim.
- Shared-pool spatial fixtures preserved snapshot membership and callback order, including nested reuse, exhaustion and subsequent reuse. Pool fixtures exercised alignment, capacity, cold failure and unchanged live leases.
- The production and ASan SDK role sweep exercised Q2 classic/rerelease and Q3 game/cgame/ui through the owned backend. Module outputs and Q2 broad-phase results matched the retained fixtures. That sweep used a composed candidate containing these exact pool changes plus separately verified core changes.

Private receipts and source snapshots are under `/tmp/qa-the2874-mover-pool-isolated-build-20261009`, `/tmp/qa-the2874-mover-pool-20261009`, `/tmp/qa-the2874-spatial-shared-pool-20261009`, `/tmp/qa-the2874-pool-20261009` and `/tmp/qa-the2874-sdk-roles-20261009`.

This does not close the whole-frame allocation gate. Renderer staging, frame publication and other live callers still require migration. This slice has not replaced the installed owner executable.
