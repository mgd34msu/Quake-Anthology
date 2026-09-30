# C implementation plan

This plan ports [Quake Anthology TS](https://github.com/mgd34msu/Quake-Anthology-TS) into a native C engine. It carries forward the unified Anthology product and the full interoperability intent established in its documentation and reaffirmed by the project owner. Original game sources provide fidelity references. Game assets remain external inputs.

Baseline implementation is underway. The [dependency graph](dependency-graph.md) and [machine-readable plan](dependencies.json) carry the 41 tasks published to vibecheck-jev. The immediate [recovery audit](audit/README.md) checks all prior work against the original task criteria after discovery that ledger judgments were not running. Progress commits are published to [Quake Anthology](https://github.com/mgd34msu/Quake-Anthology).

For this session, use GPT-6.1 Sol for all agents and subagents, with medium, high, or xhigh effort. Keep a soft cap of eight agents across the full tree, including the coordinator. Temporary excess is allowed when a concrete task needs it. The bounded review and its confirmed repairs are recorded in `docs/audit/review-20260929.md`; baseline implementation resumed on September 29 under the original feature criteria. Continue source review during implementation.

Keep licensing and copyright notices in the root `LICENSE` file. Do not add per-file notices, SPDX headers, or invented contributor attribution.

The current sequence is to write the complete baseline C code, review its quality and correctness during construction, finish baseline integration, then evaluate gameplay and make deep performance improvements and polish. This order supersedes earlier live-play and performance gates in the review and donor documentation.

Use the TypeScript prototype as an executable reference for its working features, user workflows, and current performance. Extract the behavior each subsystem must provide, then design its C implementation around native execution and shared resources. Useful prototype tests supply regression cases after their expectations are checked against the product requirements and original behavior. Prototype defects and runtime workarounds do not become C requirements.

Do not copy source files from the TypeScript project or original game engines into this project. Write C implementations from the required behavior and contracts. Remove duplicated services, avoidable allocation, repeated preparation and TypeScript-specific machinery during implementation. Authored immutable data can be represented in shared C tables; this does not authorize transplanting engine implementations.

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

Implement Anthology's built-in gameplay directly in C. Stock games, expansions, rereleases, and their built-in cross-game combinations must use compiled gameplay calling shared services. Loading a Q2 map or choosing rerelease behavior must not start a CPU emulator. Guest addresses, instruction dispatch, and emulated operating-system services do not belong in that path.

For each subsystem, identify its required behavior and choose the C implementation that provides it. Delete work caused solely by the donor language or runtime before optimizing loops that perform it. Retain a compatibility mechanism only when a concrete external program, format, or observable contract requires it.

| Donor mechanism | C implementation direction |
|---|---|
| CPU emulation to execute built-in game behavior | Compiled C gameplay with direct calls into the shared world |
| Guest-memory projection and instruction-region hooks for built-in composition | Typed authoritative state and explicit gameplay operations that selected components can extend |
| JavaScript numeric wrappers and BigInt for ordinary engine arithmetic | C `float`, fixed-width integers, and ordinary native pointers where appropriate |
| Promise chains around synchronous simulation | Direct calls under the explicit source scheduler; asynchronous jobs only for actual independent work |
| Worker serialization of engine-owned render objects | Owned frame buffers and resource handles shared with the render thread |
| Repeated immutable object construction and deep copies | Stable storage, explicit lifetime, reusable buffers, and snapshots only where consumers need them |
| Bun FFI and manually packed host-library calls | Native headers and normal library calls |

Keep semantic distinctions such as live positions versus last-linked collision bounds. They require separate values, not a copy of the donor's object-allocation machinery. Compatibility checks at external boundaries remain necessary in C.

A resolved session plan binds content and source behaviors once. One simulation owner controls actor identity, source schedules, mutations, and transitions. Built-in behavior uses typed C state and direct service calls. External modules retain their required private state behind their compatibility adapters. Providers do not own competing collision worlds or independently advance copies of the same actor.

Use compact structs, generational handles, stable entity slots, indexed resource handles, and lifetime-specific memory arenas. Arrange fields around their access patterns during implementation, then refine layout during the later performance pass. Keep uncommon game-specific fields in typed extension storage. Avoid both a giant universal entity struct and a generic ECS framework whose ordering obscures original callbacks.

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
| `compat` | External QC/QVM programs, native module ABIs, declarations, and private module state; execution adapters selected for actual requirements |
| `render` | Shared scene/material/model interpretation, CPU and GL backends, capture, resource lifetime |
| `audio` | Source sound policy, media decoding, sample timing, mixing, device delivery |
| `network` / `persistence` | Protocols, prediction, demos, shared/original saves, version migration |
| `app` / `platform` | Menus, seats, console, settings, services, SDL/OS/device boundaries |

The shared player-bot system should retain the project's Q3-derived foundation and gain the useful capabilities of other sources. Source monster AI remains distinct behavior over the same world. Foreign maps need usable navigation generation and actual path execution.

Providers receive narrow typed capabilities for their work. Their contexts cannot own independent worlds, clocks, connection registries, or outer game loops. Authoritative guest fields remain in guest storage where required; host views and spatial links must have declared synchronization boundaries rather than become competing owners.

## Shared code and runtime resources

Reuse common implementations and their runtime data throughout the engine. Family-specific formats and intentional behavior differences supply adapters or selected policies to shared services. Do not create parallel Q1/Q2/Q3 copies of the filesystem, resource cache, actor registry, spatial index, renderer, mixer, input system, connection lifecycle, or save coordinator.

| Resource or work | Sharing and lifetime |
|---|---|
| Asset bytes and decoded assets | Resolve mount precedence first, then cache by content identity and decoding parameters. Reuse identical immutable data across actors, game behaviors, mods, and local seats. Equal filenames alone are insufficient. |
| Textures, meshes, and sound samples | Retain shared GPU objects, immutable geometry, and decoded sample storage. Actor instances retain only their pose, material overrides, voice cursors, and other changing state. |
| World collision and visibility | Keep one world geometry representation and spatial index. Reuse preprocessing and query scratch; cache derived visibility only when its cluster, area state, and other inputs match. |
| Gameplay and movement | Share common operations and kernels. Bind selected policies when configuration changes. Preserve source ordering and decisive numeric differences through explicit policies or specialized kernels where needed. |
| Frame, trace, and audio work | Reuse bounded buffers and pools. Track high-water usage. Avoid routine heap allocation in simulation, draw submission, and audio mixing. |
| Presentation and network extraction | Reuse authoritative state and revision information. Construct the views each consumer needs without rebuilding or copying every actor for every seat or consumer. |

Keep one owner for mutable state. Share immutable mod assets and engine services while preserving each mod instance's required private state. Sharing resources must not merge independent inventories, RNG streams, guest globals, prediction histories, or per-seat effects.

Shared code must also suit hot loops. Prefer simple functions over layers that only forward calls. Resolve format and behavior choices outside repeated work where possible. Keep common algorithms and source-specific differences explicit. The later performance pass can specialize measured hot kernels without duplicating unrelated engine services.

## Mod execution and continuity contracts

Each mod instance retains private state. Built-in C components use explicit instance contexts. External programs receive the isolation required by their ABI and composition contract; an emulated address space is specific to backends that need one. Aliases must observe the same underlying memory. Committed writes and callback-visible world mutations become visible before a synchronous nested call returns. Nested callbacks cannot defer observable changes until the end of a frame. Saves require a session safe point with no active module callbacks; any guest checkpoint also requires zero active guest reentry depth.

Composition retains ordered transforms and observers, a single replacement for an operation, and synchronous continuations that can invoke the canonical mutation only once. Disable, actor reuse, travel, and restoration must retire the correct owner while preserving surviving instances. Missing adapters for arbitrary mod internals remain implementation work; a declared boundary is not permission to narrow the product.

An Anthology save contains a versioned manifest, authoritative host state, and opaque provider chunks with content digests and schema identities. Write saves atomically. Restore into a candidate session, validate and reconnect ownership, then replace the active session only after restoration succeeds. Portable provider state is preferred; otherwise pin the saved execution backend until an explicit, tested conversion exists. Preserve supported original imports, and reject legacy exports that cannot represent active state.

One connection authority owns seats, identity, reliability, and lifetime. Protocol adapters encode the state each original dialect can represent. Versioned Anthology packets carry full mixed-session state through explicit encoders; never serialize in-memory C structs. Session composition cannot silently disappear when connecting, recording a demo, or restoring a save.

## Architecture alternatives considered

The selected architecture uses one authoritative world, compiled C gameplay, private component contexts, atomic configuration changes, and explicit save and connection ownership. Callback visibility and composition order are behavior contracts. Guest memory and CPU execution are implementation choices restricted to external compatibility cases that require them.

Reject separate family worlds, whole-engine context swaps, per-entity RPC, and a literal translation of the large TypeScript simulation class. Stable storage and narrow direct calls suit C while preserving the source-specific behavior the shared engine must expose.

## Numerical and execution choices

Use ordinary C `float` for gameplay where it preserves required behavior. Do not recreate JavaScript double-precision intermediates solely to match a port artifact. Keep wider arithmetic only where required for correctness. Preserve RNG ownership, integer conversion/wrap, source timing, collision decisions, and network prediction behavior.

Start with explicit floating-point compiler policy and no global fast-math. Contraction, reassociation, and changed exceptional-value behavior require qualification in the affected subsystem. [GCC's optimization documentation](https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html) explains why these options can change results. Exact emulated x87/SSE behavior belongs in the compatibility runtime, separate from ordinary gameplay math.

The execution decision starts with the program being run:

1. **Built-in Anthology behavior:** compile the C implementation and call it directly. Recreate source behavior through the shared engine's typed operations. Original executables can supply comparison evidence without becoming production dependencies.
2. **External QC or QVM programs:** support their actual bytecode contracts, including server, client-game, and UI roles. Implement that support in C. Choose an execution strategy that fulfills the external contract; the later performance pass can refine it independently of built-in gameplay.
3. **Native modules compatible with the host:** prefer ordinary native execution through the required ABI. Provide independent instance state and the composition interfaces the module needs. Source-available components may be compiled for the host when that preserves the required behavior.
4. **Required binaries that cannot execute through those paths:** identify the exact obstacle, such as a foreign architecture or operating system, private-state isolation, or binary-level interception. Evaluate a scoped loader, ABI bridge, instrumentation, translation, or emulation solution for that case. Do not port the TypeScript CPU/runtime by default.

Inventory the required external modules during implementation so compatibility constraints are visible. Implement their callbacks, private instances, aliases, save continuation, and cross-game composition on the selected path, with source review during construction. Run correctness checks and qualify complete live module workflows after baseline completion. A fallback must cover required binary cases that native execution cannot support. Full mod interoperability remains required, but an external binary's execution restrictions must not become restrictions on every built-in actor or world operation.

## Implementation order and quality

The exact prerequisites and acceptance criteria live in [dependencies.json](dependencies.json). The [graph view](dependency-graph.md) exposes every task and edge. [source-map.json](source-map.json) assigns all 23 donor functional targets and every top-level source directory to implementation tasks. These mappings account for scope; they do not prove that a feature works.

1. **Build the complete baseline, B00 through B34.** Implement the foundation, content and formats, shared world and gameplay, campaigns and modes, rendering, audio, input, configuration, compatibility, networking, persistence, services, UI, and application integration. Resolve each subsystem's ownership and interfaces before connecting its consumers. Source implementation can overlap when published interfaces suffice. Dependencies must be complete before dependent work is accepted.
2. **Review code quality throughout baseline construction.** Check actual code for duplicated services, unnecessary abstraction, incorrect ownership, unchecked binary boundaries, memory lifetime errors, missing behavior, and disconnected consumers. Review source only during this phase and correct defects found by inspection. Do not configure builds, compile, run tests, execute programs, or run sanitizers until the entire baseline source is written.
3. **Finish the full baseline, BASELINE.** Every B00 through B34 task must be complete. All required code must exist with complete build definitions and real application callers. This is source completion; successful compilation and runtime behavior are evaluated next. Partial gameplay, a parser utility, or a compiled shell does not satisfy this gate. The baseline includes original configurations and mixed configurations, all required editions, modules, renderers, and services. Only the previously approved unavailable ranking backend remains an extension point.
4. **Build and evaluate the completed baseline, P01.** Compile with GCC and Clang, run focused correctness checks and sanitizers, and fix failures. Then run campaigns and competitive modes, mixed sessions, actual mod packages, saves and fresh-process restoration, both protocol directions, four local seats, demos, devices, rendering, audio, and lifecycle transitions. Fix required behavior failures and record what was exercised.
5. **Make deep performance improvements, P02.** Profile the complete engine on matched workloads. Improve CPU, GPU, memory, threading, loading, and latency while retaining the required behavior and work. The performance section below defines the evidence for this phase.
6. **Enhance and polish, P03.** Improve presentation, interaction, and responsiveness after the complete engine and its performance pass exist.
7. **Qualify the final release, RELEASE.** Account for all required gameplay, configurations, mods, native peers, packaging, and performance results. Record actual platform coverage and remaining external limitations.

Build execution, test execution, program runs, early playability, frame-time targets, profiling, campaign playthroughs, and pixel polish must wait until baseline source completion. Do not divert implementation into those later phases. Efficient native code, shared resources, and removal of unnecessary donor machinery are construction decisions and remain part of baseline work.

## Performance after baseline completion

P02 follows BASELINE and P01. Measure compiled gameplay through direct engine services and measure required external-module workloads separately. The TypeScript probe provides a comparison case; it does not prescribe the C execution path.

Compare identical content, selections, commands, seeds, simulated time, actor population, and quality. Compare semantic state, decisive events, packet contracts, images, and PCM as appropriate. Approved numerical changes need relevant behavior checks instead of blind TypeScript byte equality. Removing entire donor stages is a valid improvement when their required results remain intact.

Record median, p95, p99, maximum stalls, cold load, save/load, memory, allocation, and growth over repeated travel. Separate CPU stages, GPU work, and presentation waits. Inspect copying, decode/upload counts, draw submissions, and audio underruns. Set rendered-frame targets for named hardware, resolution, and workloads during this phase. Gameplay deadlines such as the Q2 rerelease 25 ms interval are evaluated here with measured headroom. No arbitrary FPS number or speedup multiplier is already approved.

Use the findings to improve collision scratch reuse, actor traversal, static geometry, GPU submission, shadow invalidation, audio scheduling, and any remaining external-runtime costs. SIMD, batching, and parallel work are candidates where they preserve required output and ordering. Keep queued work bounded and measure input and audio latency as well as throughput.

A faster result qualifies only when it preserves the required work and output. Lower quality, missing effects, fewer entities, reduced AI updates, altered mission rules, dropped mods, or a removed renderer do not meet the optimization requirement.

## Integration, evidence, and local commits

Use vibecheck-jev to claim tasks, report bounded results, and distinguish reported completion from verified completion. The coordinator owns plan publication, shared staging, and local commits. Workers own explicit paths and contribute without changing the donor sources or external assets. Update the local graph and ledger together when scope or prerequisites change.

Every implementation commit identifies the delivered behavior, relevant correctness checks, and remaining work. Baseline commits do not need live-play or performance measurements. Keep assets, credentials, machine-local caches, and unrelated existing files out of commits. Review the combined tree before accepting dependent work.

Preserve native protocols where an original peer can represent the session. Use Anthology's protocol for complete cross-game state. An unchanged original client cannot acquire new map formats or rendering capabilities from a server. Keep those external limits distinct from missing functionality in this engine.

Baseline completion and final qualification are separate milestones. Correct prototype defects instead of preserving them, and keep implementing until the full required baseline is connected before beginning the later evaluation and enhancement phases.
