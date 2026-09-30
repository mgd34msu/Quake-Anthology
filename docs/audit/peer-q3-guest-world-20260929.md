# Independent Q3 guest ownership review, 2026-09-29

Reviewer: world/network worker. This is a bounded source review of the application Q3 guest constructor, role ownership and map retirement packet. It is not runtime validation or acceptance of the complete compatibility baseline.

## Coverage

Read the complete guest owner, role, client, server, export and private-header files. Client/export/private-header files subsequently changed for additional client effects; their earlier inspection does not accept the newer changes. The accepted frozen ownership files are:

| File | SHA-256 |
|---|---|
| `src/app/application/guest_q3.c` | `455faee1c4e2fa43bf2cb744b5031010f572f180b8c5ed29b69cf9f64d9afe2d` |
| `src/app/application/guest_q3_roles.c` | `8c2cb739e787b5cfe88c8ddfa64b16cf4fde4a5a409b4860070d166af1d6f5d7` |

Read complete `src/compat/q3_host/{host,common,cvars,client_input,game_records}.c` and `include/qa/q3_host.h`. Read the console command data structures, contribution registration/removal, dispatch re-selection and owner retirement in `src/console/commands.c`; remaining console code was not reviewed in this packet. Read the relevant application retirement/finalization callers. Metadata inspection matched the frozen host manifest; no build, compiler, parser, test or runtime was executed.

## Confirmed defect and repair

P1, application guest map admission: the previous `application_q3_guest_spawn_map` marked the map ready and returned success when its provider had no GAME role. The application invokes this API for its selected ENTITIES owner. A UI/CGAME-only provider could therefore report a published authored world without running any source game entity initialization. The owner added the GAME-role rejection before retained text, role restarts or client state mutation, and removed the late successful return. Independently reread that correction in the owner hash above.

## Refutation results

- Role cleanup retains the role on host/executor rejection. Host destruction returns false before freeing the host. The role clears its host pointer only after consumption, updates provider aliases after each consumed executor, and handles the native runner's consumed-on-error contract separately.
- Engine call depth and executor activity block source callback destruction. World callback depth blocks freeing contexts still borrowed by body/collision bindings. Owned live actors block host destruction; borrowed projections preserve the foreign world's body authority.
- Immutable artifact cache references survive role recreation. Role-local executor/image/module references are released separately, while cached declarations and compatibility metadata remain owned by the engine until all roles retire.
- Map retirement calls source shutdown and closes portal claims while the old shared geometry still exists. Role restart occurs only after actor retirement. Application placement of the retirement hook remains a separate integration obligation owned by the coordinator.
- Role service-owner identities separate command/catcher/cvar lifetime from actor owner. Console contribution removal keeps contributions from other roles; dispatch reselects registrations after callbacks can mutate the registry. Shared retained cvars survive source role recreation.
- The host game-data getter removes its QVM address tag before returning offsets to input hook consumers; native addresses remain unchanged.

No additional confirmed constructor/role/host lifetime defect was found in the inspected frozen paths. This conclusion does not cover the newly changing client-effect packet or all compatibility syscalls.

## Superseding native activation review

Matched all four hashes in `/tmp/qa-q3-native-activation-freeze.txt`: owner `77477900609670936ba07fb1b1da975fd5f9e0411c660db0d7c672cf82743ce0`, roles `6f36d8de07cedf4c586fa4e4e8d28a1e7d3c67642fdba17829b52c3a930d2a78`, exports `2e109139c36246b1e17bd0027219e35d3364a1ace05be22f31b81848d777a081`, and private header `19dd5118de5b110f7e1546a1731a7ba5f3909158c2e70d7d432828e61b9b2a6e`.

Independently read the complete updated role implementation and private header, changed owner entry and client-role initialization paths, the actual application native-options producer, and native module inspection, instance construction, `dllEntry` binding and host bootstrap dispatch. Native preparation now retains metadata and typed options without creating an executable instance. The options borrow the application-lifetime runner context; cached declarations and launch metadata remain retained. Activation requires an attached constructed provider and idle guest owner before executable creation. Engine call depth spans executable creation, bridge attachment and input attachment. A failed activation is latched; a successfully created partial executor stays reachable for teardown. UI API validation passes through the same activation boundary. Already committed roles can run their shutdown while detached.

No additional confirmed defect was found in these bounded admission and ownership changes. This source-only disposition supersedes the earlier constructor hashes for this activation repair; it does not accept all Q3 compatibility behavior. A separate plausible native-core concern remains: a syscall during constructor bootstrap sets the Q3 host's native memory context, while a later constructor failure can reclaim that context. Actor retirement and portal cleanup inspected here do not read that guest memory, so no reachable post-failure read was demonstrated. The existing native OS destructor/import-closure ordering concern is also outside this activation diff.

## Open scope

The guest owner reported and disabled a false bot allocator fallback: merely setting allocated/bot did not create canonical actors or synchronize source-internal ClientConnect/Begin. Its actual roster/bot adapter remains required. Secondary Q3 map/actor qualification, frontend effects and complete snapshots/save integration remain implementation work. None is accepted as complete by this review.

## Superseding local client-effect review

Independently read the new complete `guest_q3_effects.c`, updated server-command consumption, complete exports/private header, and the corrected delegated-only bot allocator. The inspected frozen identities are clients `d4ba34dcaba74924cc529a57bf8594783d3c87f9134892ba6355001a0c0d6153`, exports `3f05844543995038d7b1337c535c21f1f8d6cb0eb8ed9b98a3c76e1277220c0d`, effects `cb4617e3fdc8360fe1775fc90e5b48ebdcfeaf32cf240a4e47d0982ac07290b3`, private header `5cfd9d228960fc03038ebb286039a40b21ed38acadfd6824ff76559604b90837`, and server `03e6cf1dc1f402a625b0f208aec111f0986d71a39cf928262f8bea9c8347d8a5`.

Changed system info updates the local gamestate before invoking the required outer prediction/frontend callback. A pending flag survives callback failure, so retry does not skip the effect merely because the local configstring already matches. Cgame initialization applies initial local system info before source INIT. Map restart clears command records while preserving sequence, disconnect changes only the local connection and suppresses ordinary argv delivery, and level-shot remains a required outer effect. Engine call depth leases every external effect callback. The delegated bot allocator rejects configured seat ordinals; absent a real allocator it returns unsupported instead of fabricated success. No additional confirmed defect was found in these bounded changed paths. Actual frontend callbacks, bot admission, snapshots and gameplay execution remain outside this acceptance.
