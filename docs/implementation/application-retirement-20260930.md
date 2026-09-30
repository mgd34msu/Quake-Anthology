# Application retirement, 2026-09-30

This B34 packet connects `qa_configuration_hooks.retire` to actual application teardown. `qa_application_retire_sources` and `qa_application_destroy` both own complete world retirement. Configuration destruction keeps its current snapshot until the retirement callback succeeds.

Ordinary retirement retains its existing order: guest/bot idle admission, `before_world_change`, provider map retirement, bot and portal cleanup, `world_retired`, canonical world retirement, then component and policy detach. Healthy native Q2 providers still run their original `ClientDisconnect` callbacks while their actors are live. Normal map travel uses this same existing sequence and never selects the terminal fallback.

Complete application retirement selects a different order only when a constructed native Q2 provider has an actual host whose runner satisfies `qa_native_terminal`. An application fault flag, an ordinary direct-backend error, or a private-checkpoint rejection alone does not qualify it. The terminal sequence is:

1. Admit idle guest/bot owners and run `before_world_change`.
2. Retire healthy providers in roster order, preserving their original live-world disconnect/shutdown callbacks.
3. Call `qa_session_retire_world` on the application's real session. Canonical actor IDs become invalid before their release notifications. The session cancels scheduled work; the application releases actual world bodies, combat and inventory claims, equipment, player controls, and other actor state. Attached source components receive their normal release notifications.
4. Retire only the deferred terminal native Q2 providers' map metadata, then close bots and portals and run `world_retired`.
5. Detach policies/components, release routing, equipment, modes, and the provider roster. Configuration destruction publishes the null candidate and releases its snapshot owners.

The native terminal-release packet supplies the matching real observer path. `qa_native_host_actor_released` qualifies the released full generation against the actual host registry and uses parent-owned cached table metadata only on an idle poisoned runner. It clears matching native slot metadata after canonical invalidation. Healthy host notifications still query the actual source. `application_native_q2_client_disconnect` retains fallible primary detach for live actors, skips an inaccessible source callback only for a terminal runner, and requires the existing exact borrowed-slot detach. Terminal cgame shutdown and final host destruction independently enforce `qa_native_host_terminal_retired`. Cached identities never substitute for live private state or a checkpoint.

Failed world, service, policy, or component retirement retains the current configuration snapshot, provider array, and current attachment flags for retry. Successful earlier cleanup remains owned and its existing flags describe the partial state. Snapshot leases may retain detached source providers; their native module and borrowed frontend/application services stay alive until the final provider release. Publishing a null candidate completes bookkeeping without repeating source shutdown.

Source inspection traced the real configuration transaction, application owner/publication/lifetime, session/actor observers, native Q2 primary detach and host lifetime, frontend callbacks, and bot cleanup. The TypeScript behavioral reference `src/world/session/session.ts` retains separate world, resource, provider, and connection owners; this packet preserves those observable lifetimes with the C authorities. Independent review of the frozen source packet remains required before integration.

Verification is source-only. No configure, build, compiler, syntax checker, test, executable, sanitizer, generator, or benchmark ran. This packet does not establish stock artifact full-private admission, runtime cancellation/teardown success, B34 completion, or BASELINE completion.
