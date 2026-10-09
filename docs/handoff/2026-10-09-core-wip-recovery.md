# Core checkpoint recovery, 2026-10-09

All 32 changed paths from `wip/the-2874-2026-10-09` at `1ded36361bbe3bd8db04c9fa6970abdfb620beb0` are recovered or superseded on verified main at `c7a11027`. This checkpoint can be deleted. Keep the unrelated THE-2859 and THE-2873 checkpoints: they still contain unresolved drafts.

The comparison uses committed file bytes, not commit titles. “Exact” below means the named main commit contains the same file as the checkpoint. Later verified changes may extend it. No checkpoint capability was deliberately dropped.

| Recovery | Main commit | Exact checkpoint paths |
|---|---|---|
| Exact, five paths | `a570f13a` | `docs/playtests/2026-10-09-allocation-gate.md`, `include/qa/allocation_gate.h`, `src/core/allocation_gate.c`, `src/app/frontend/frame.c`, `src/app/frontend/lifetime.c` |
| Exact, ten paths | `2aadaea7` | `include/qa/physics.h`, `include/qa/pool.h`, `src/core/pool.c`, `src/gameplay/q3/game.c`, `src/movement/entity.c`, `src/movement/entity/internal.h`, `src/movement/entity/pushers.c`, `src/movement/entity/q3_mover.c`, `src/world/collision/world_internal.h`, `src/world/spatial.c` |
| Exact, eight paths | `873128bf` | `include/qa/input.h`, `src/input/commands.c`, `src/app/application/arsenal_guest.c`, `src/app/application/control.c`, `src/app/application/control_frame.c`, `src/app/application/guest_qc_input.c`, `src/app/application/native_q2_client_stages.c`, `src/app/application/native_q3_control.c` |
| Exact, one path | `b535090b` | `src/app/composition/recipe_import.c` |
| Exact, three paths | `c7a11027` | `src/network/unified/channel_state.c`, `src/network/unified/session_continuation.c`, `src/network/unified/session_identity.c` |
| Superseded, four paths | `c7a11027` | `src/network/unified/channel.c`, `src/network/unified/channel_internal.h`, `src/network/unified/channel_save.c`, `src/network/unified/channel_storage.c` |
| Composed recovery, one path | `a570f13a`, `2aadaea7`, `c7a11027` | `CMakeLists.txt`: allocation instrumentation, shared pool and channel storage are registered. The allocation option moved; `315e0925` additionally registers the shared ruleset. |

The four superseded channel files replace repeated reliable-message gathering with a contiguous receive-head lease. Future messages remain bounded pages, with the same callback admission and retirement behavior. GCC/Clang and sanitizer comparisons preserve the 16,869,981-byte transcript, including the existing 16,865,235-byte anchor; component heap calls fall from 1,032 allocations/frees to zero. Production, ASan and both seven-check core suites pass at source tree `ec2138a1`. All 66 actual session receipt/continuation checks pass in both builds. See [fixed channel storage](../playtests/2026-10-09-fixed-channel-storage.md).

The other verified slices and their evidence are recorded in [allocation measurement](../playtests/2026-10-09-allocation-gate.md), [fixed mover pools](../playtests/2026-10-09-fixed-pool-movers.md), [command conversion](../playtests/2026-10-09-command-space.md), and [recipe lifetime](../playtests/2026-10-09-recipe-resource-lifetime.md). Recovery does not imply whole-frame zero allocation or completion of those issues. No executable installation is part of this cleanup.
