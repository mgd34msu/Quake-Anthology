# C implementation plan

This plan ports `../quake-typescript` into a native C engine. It carries forward the unified Anthology product and the full interoperability intent established in its documentation and reaffirmed by the project owner. Original sources in `../qsrc` are fidelity references. Assets in `../qfiles` remain external inputs.

Implementation has not started. This is the proposed architecture and work sequence derived from the [review](review.md). Local progress commits are authorized. The owner will add a remote later.

## Product contract

Build one engine, world, and executable that combines the useful capabilities of Quake, QuakeWorld, Quake II, Quake III Arena, their rereleases, expansions, and mods. Preserve complete native configurations and enable composition across sources. Current compatibility gaps are work to address, not reasons to shrink the intended product.

The common engine separates four responsibilities:

| Responsibility | C design |
|---|---|
| Shared services | One owner for actors, spatial queries, resources, rendering, audio, input, files, downloads, saves, and connection lifecycle |
| Selectable behavior | Source-specific movement, character, weapons, monsters, equipment, combat, inventory, rules, and presentation operating through those services |
| Compatibility adapters | File formats, network dialects, demos, source commands/cvars, bytecode, native ABIs, and original module layouts |
| Map/campaign behavior | Authored target graphs, doors, hazards, mission gates, keys/sigils, hubs, bosses, exits, and progression |

Selections remain independent, including per-player and per-actor choices. A character model must not implicitly choose movement or damage rules. Game mode, teams, and scoring remain independent settings. Native presets select coherent original defaults, with classic and rerelease behavior kept distinct.

Multiple independent mods must coexist across source games, including their actors, items, rules, commands, HUD/effects, teams, and objectives. Each instance retains private state and explicit lifetime. Grapples and offhand grenades retain their independent source mechanics, placement, controls, and operation outside their original modes.

Provide a dedicated menu with individual controls for non-game-type mods. A configuration change is prepared and validated as a whole, then committed at a session safe point. Rejected changes leave the active configuration untouched. Dependency changes, resource lifetimes, and private instance state participate in the same transaction.

Map progression survives crossover. A replacement monster still fulfills the encounter's mission responsibilities. Foreign objective modes need authored or generated objective/spawn adaptation. Authored map mechanisms stay with the map by default; future finer-grained overrides must preserve their obligations.

All other Anthology features remain in scope: CPU/GL output, source/guest UIs, four local seats, remote play, prediction, bots/navigation, audio/music, controllers/gyro/haptics, accessibility/localization, saves, demos/MVD/GTV, cinematics, browser/admin/downloads, progression, diagnostics/cameras, and LLM assistance. Preserve the ranking-provider interface and its documented unavailable-backend state rather than inventing an unrelated online service.

## C architecture

Use C17 for the engine and normal native platform APIs. Start with Linux x64, the existing delivery target. Keep portable core code and explicit OS boundaries. The shipped runtime must not depend on Bun or TypeScript. Existing external reference tools can assist migration without becoming engine dependencies.

A resolved session plan binds content and source behaviors once. One simulation owner controls actor identity, source schedules, mutations, and transitions. Source-specific providers own only their private behavior state and guest storage. They do not own competing collision worlds or independently advance copies of the same actor.

Use compact structs, generational handles, stable entity slots, indexed resource handles, and lifetime-specific memory arenas. Keep frequently traversed fields contiguous where profiling supports it. Keep uncommon game-specific fields in typed extension storage. Avoid both a giant universal entity struct and a generic ECS framework whose ordering obscures original callbacks.

Simulation remains serial initially because source callback order and immediate mutations are observable. Parallelize independently owned work such as asset decoding, scene preparation, rendering, and audio production. Publish bounded immutable frame data. Bound queued work so threading cannot trade throughput for unacceptable input lag.

An intended application interface is small:

```c
qa_result qa_session_open(const qa_launch *launch, qa_session **out);
qa_result qa_session_advance(qa_session *session,
	uint64_t elapsed_ns, const qa_input_batch *input, qa_publication *out);
qa_result qa_session_save(qa_session *session, const qa_save_request *request);
qa_result qa_session_travel(qa_session *session, const qa_map_request *request);
void qa_session_close(qa_session *session);
```

These are design sketches, not implemented declarations. `elapsed_ns` supplies elapsed monotonic duration explicitly; replay supplies recorded durations. Source clock conversion and time debt belong to the scheduler, without hidden wall-clock reads. The session owns scheduling, source callbacks, composition, save boundaries, and publication. Callers should not coordinate guest synchronization or several game loops. Actor operations remain explicit typed interfaces for the relevant owner.

| Area | Owns |
|---|---|
| `content` | Catalog, mounts, identities, resource formats, selection resolution |
| `session` | Source clocks, scheduling, commit order, mod lifecycle, travel, save safe points, publication |
| `world` | Actor generations, source-slot mappings, body/link state, spatial indexing, collision |
| `gameplay` | Movement, weapons, actors, combat/inventory policies, mission obligations, match rules, and source editions |
| `compat` / `guest` | QC, QVM, native CPU/ABI, declarations, source projections, private module state |
| `render` | Shared scene/material/model interpretation, CPU and GL backends, capture, resource lifetime |
| `audio` | Source sound policy, media decoding, sample timing, mixing, device delivery |
| `network` / `persistence` | Protocols, prediction, demos, shared/original saves, version migration |
| `app` / `platform` | Menus, seats, console, settings, services, SDL/OS/device boundaries |

The shared player-bot system should retain the project's Q3-derived foundation and gain the useful capabilities of other sources. Source monster AI remains distinct behavior over the same world. Foreign maps need usable navigation generation and actual path execution.

Providers receive narrow typed capabilities for their work. Their contexts cannot own independent worlds, clocks, connection registries, or outer game loops. Authoritative guest fields remain in guest storage where required; host views and spatial links must have declared synchronization boundaries rather than become competing owners.

## Mod execution and continuity contracts

Each guest instance has an isolated address space and private globals, heap, and runtime state. Aliases must observe the same underlying memory. Committed writes and callback-visible world mutations become visible before a synchronous nested call returns. Reentry records track ownership and commit order; nested callbacks cannot defer observable changes until the end of a frame. Checkpoints and saves require zero active guest reentry depth at a defined session safe point.

Composition retains ordered transforms and observers, a single replacement for an operation, and synchronous continuations that can invoke the canonical mutation only once. Disable, actor reuse, travel, and restoration must retire the correct owner while preserving surviving instances. Missing adapters for arbitrary mod internals remain implementation work; a declared boundary is not permission to narrow the product.

An Anthology save contains a versioned manifest, authoritative host state, and opaque provider chunks with content digests and schema identities. Write saves atomically. Restore into a candidate session, validate and reconnect ownership, then replace the active session only after restoration succeeds. Portable provider state is preferred; otherwise pin the saved execution backend until an explicit, tested conversion exists. Preserve supported original imports, and reject legacy exports that cannot represent active state.

One connection authority owns seats, identity, reliability, and lifetime. Protocol adapters encode the state each original dialect can represent. Versioned Anthology packets carry full mixed-session state through explicit encoders; never serialize in-memory C structs. Session composition cannot silently disappear when connecting, recording a demo, or restoring a save.

## Architecture alternatives considered

Two independently developed designs converged on one authoritative world with private provider contexts. Use the design with explicit guest ownership, nested-call visibility, save restoration, and connection contracts as the base. Incorporate atomic configuration changes, independent selections, precise composition ordering, and backend conversion rules from the other design.

Reject separate family worlds, whole-engine context swaps, per-entity RPC, and a literal translation of the large TypeScript simulation class. Stable storage and narrow direct calls suit C while preserving the source-specific behavior the shared engine must expose.

## Numerical and execution choices

Use ordinary C `float` for gameplay where it preserves required behavior. Do not recreate JavaScript double-precision intermediates solely to match a port artifact. Keep wider arithmetic only where required for correctness. Preserve RNG ownership, integer conversion/wrap, source timing, collision decisions, and network prediction behavior.

Start with explicit floating-point compiler policy and no global fast-math. Contraction, reassociation, and changed exceptional-value behavior require qualification in the affected subsystem. [GCC's optimization documentation](https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html) explains why these options can change results. Exact emulated x87/SSE behavior belongs in the compatibility runtime, separate from ordinary gameplay math.

Compile Anthology's built-in implementations to native C. Retain QC and QVM execution for original programs, including server, client-game, and UI roles. Preserve required native-module execution and semantic composition. The current project's useful instruction caches, memory tracking, ABI knowledge, and runtime services are starting mechanisms, not a mandatory C object model.

Early in implementation, compare a compact C execution approach with an available native-code translation approach on a real callback-heavy workload. An existing C CPU core is worth evaluating before writing another full CPU. Select it only if it preserves the necessary hooks, faults, aliases, nested calls, code invalidation, private instances, and save behavior. No dependency or JIT is selected by this plan.

Direct host loading is useful only for qualified combinations. It cannot alone cover foreign operating systems, 32-bit modules in a 64-bit host, instruction-region hooks, or independently instantiated guest address spaces. Faster backends must preserve functionality, and the compatibility path stays available. Replacing a currently required module path with compiled behavior requires complete behavioral qualification, not recognizing its filename or digest.

## Performance work

Optimize representations and repeated work first. Candidate changes are compact guest registers/operands, retained ABI plans, reusable collision scratch, stable actor indexes, immutable static meshes, GPU buffers, adjacent UI batching, revision-based shadow invalidation, and bounded frame-command arenas.

Move audio production to preallocated storage and a producer that is independent of frame stalls. Preserve source sample timing, channel/loop policies, per-seat sound, and media synchronization. Reduce queue latency only after underrun and event-timing measurements.

Keep source draw ordering and full material/effect support. Preserve the software renderer and optimize its data flow before adding SIMD or tile parallelism. GPU animation, batching, and SIMD are welcome when their output preserves required behavior.

Measure identical content, selections, commands, seeds, simulated time, actor population, and quality. Compare semantic state, decisive events, packet contracts, images, and PCM as appropriate. Native C does not need to execute the same host instruction count as TypeScript. Approved numerical changes need relevant gameplay/reference checks instead of blind TypeScript byte equality.

Record median, p95, p99, maximum stalls, cold load, save/load, memory, allocations, and growth over repeated travel. Separate CPU stages, GPU work, and presentation waits. The first native Q2 rerelease gate is meeting its 25 ms interval with measured headroom. Adopt rendered-frame targets from named hardware, resolution, and workloads after the first playable measurements. No arbitrary FPS number or speedup multiplier is already approved.

## Implementation sequence

Follow the documented code-first sequence: build and integrate, reach live human play, optimize, and then finish broad hardening. Focused checks belong with each implementation unit. Exhaustive verification infrastructure must not delay the first playable engine.

1. **Establish a small C foundation and the first reference cases.** Add the build, platform boundary, error/resource ownership, numeric helpers, actor handles, and a minimal headless runner. Translate the relevant source tests into independent contract cases for lifecycle, collision, callbacks, and saves. Record the existing product matrix without requiring every cell to be automated before coding. Use GCC and Clang, with address/undefined-behavior sanitizer builds for exercised C paths.

2. **Reach a real playable path through permanent engine code.** Load actual content through the shared catalog/world, drive input and movement, render and play sound, execute a weapon and an enemy, and perform a save and map transition. Integrate a minimal CPU path and real GL path early. This bootstrap remains part of the final product.

3. **Prove interoperability before bulk content completion.** Exercise a Q2 world with Q1 movement and a Q3 character/arsenal. Also exercise a Q1 rerelease campaign with foreign characters and independently selected player behavior. Include foreign monsters, real pickups, moving brushes, mission gates, bots, two local seats, and a remote peer as their relevant services join. Add at least two independent cross-game mods using actual QC/QVM/native execution, with save, process restart, travel, disable/re-enable, and retained private state. Grow these into coherent full workflows rather than isolated fake callbacks.

4. **Resolve the native execution performance risk in parallel.** Preserve exact module imports and the shared-world composition boundaries. Run the full 495-record `base1` workload and an actual classic Q2 module. Measure nested calls and end-to-end ticks. Reject an accelerator that loses required callbacks, mods, save continuation, or platform coverage. Keep ordinary content work moving independently of this lane.

5. **Expand the shared feature union across all games and editions.** Port the project's remaining weapons, creatures, pickups, modes, campaigns, movement/prediction, bot/navigation, render/audio/media, UI, and service functionality into the common owners. Native classic and rerelease cases accompany mixed cases. Preserve Q1 mission-pack gates, Q2 hub/revisit state, Q1 horde, Q2 Tag/DeathBall, and Team Arena objectives/progression. Fill required composition adapters instead of classifying unfinished combinations away.

6. **Complete continuity and interoperability workflows.** Cover the protocol matrix in both directions with native peers, full Anthology networking, four seats, prediction corrections, loss/reordering, downloads, reconnect, demos, settings, original/shared save imports, and fresh-process continuation. A new internal save representation may be more compact, but existing supported saves need a tested import or conversion path. Legacy export must reject unrepresentable state. Preserve level-entry-only autosaves.

7. **Qualify sustained play and delivery.** Run campaigns and competitive modes, mixed sessions and combinations, actual mod packages, repeated travel/load/restart, CPU/GL changes, devices, and shutdown through the compiled artifact. Reconcile the finite declared official configuration space with explicit results. Use generated histories and invariants for the unbounded mod/input domain. Publish measured performance only for the tested hardware and workflows.

These are dependency milestones, not calendar estimates. Work can proceed in parallel where ownership and interfaces are clear. A narrow playable milestone is evidence of progress, not a reduced final scope.

## Acceptance and local commits

Every implementation commit should identify the behavior delivered, tests or live checks, measured performance where affected, and remaining limitations. Use coherent bounded commits and review the combined tree. Keep assets, credentials, machine-local caches, and unrelated existing files out of those commits.

Preserve native protocols where an original peer can represent the session. Use Anthology's protocol for complete cross-game state. A server cannot give an unchanged original client new map formats or rendering capabilities. Keep those external limits distinct from missing functionality in this engine.

A faster result qualifies only when it preserves the required work and output. Lower quality, missing effects, fewer entities, reduced AI updates, altered mission rules, dropped mods, or a removed renderer do not meet the optimization requirement. Defects in the TypeScript implementation are corrected rather than copied.

The first implementation task after this review is the C build and minimal content/world foundation leading directly to the first playable mixed path. A remote repository is not required for local progress.
