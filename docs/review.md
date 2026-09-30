# TypeScript engine review

Reviewed September 26, 2026. Source: [Quake Anthology TS](https://github.com/mgd34msu/Quake-Anthology-TS/tree/5d6c6e311db6749c0b8f8ffca53ebe2bae69b24e), including its existing working-tree changes. The review made no edits to that checkout. The C destination had no commits or engine implementation.

This is a cross-subsystem review with bounded execution checks. It does not establish exhaustive gameplay, campaign, device, or original-engine parity. The [implementation plan](plan.md) incorporates the findings.

## Product intent

The source documentation establishes a unified engine with the complete useful feature union of all the games. The central instructions are in [project-plan.md](https://github.com/mgd34msu/Quake-Anthology-TS/blob/5d6c6e311db6749c0b8f8ffca53ebe2bae69b24e/docs/project-plan.md), especially lines 39–82 and 104–160, and the [common/extension boundary](https://github.com/mgd34msu/Quake-Anthology-TS/blob/5d6c6e311db6749c0b8f8ffca53ebe2bae69b24e/docs/functional-targets/README.md), lines 11–22.

One session owns world identity, bodies, spatial queries, clocks, callbacks, combat, inventory, and transitions. Shared services serve every game. Formats and protocols have adapters. Intentional gameplay differences remain selectable behaviors. This architecture permits individual players and actors to combine sources without giving several engines authority over the same world.

Campaign selection includes authored keys, sigils, targets, puzzles, bosses, exits, hubs, and progression. Replacing a monster must preserve its encounter obligations. Changing weapons or movement must not bypass mission gates. The documents explicitly require Q2 characters completing a Q1 rerelease campaign, with independent player movement and weapons. They also require objective/spawn adaptation for foreign maps; a missing adapter is unfinished work.

Interoperability extends beyond gameplay. CPU and GL rendering, bots/navigation, audio, local seats, native and mixed networking, saves, demos, media, menus, configuration, downloads, accessibility, localization, progression, tools, and LLM assistance remain in scope. Independent mods cover weapons, items, actors, protection, inputs, presentation, commands, teams, scores, and objectives. The scope is broader than projectile examples.

Classic selections retain classic behavior. Rerelease features remain available through their selected behavior, rather than silently changing an all-classic preset. Shared infrastructure must preserve these differences while offering the combined capabilities.

The user's current C request supersedes historical TypeScript-only execution and tooling restrictions. Ordinary C `float` is welcome where it preserves required behavior. Existing implementation bugs and incidental TypeScript arithmetic are not compatibility requirements. Original source and retail observations help adjudicate fidelity, while Anthology's declared behavior defines intentional extensions. This is consistent with [verification-plan.md](https://github.com/mgd34msu/Quake-Anthology-TS/blob/5d6c6e311db6749c0b8f8ffca53ebe2bae69b24e/docs/verification-plan.md), lines 7–13.

## Architecture and strengths

The inspected `src` tree contains 2,080 TypeScript files and approximately 366,000 lines including nearby source documentation. There are 661 files under `tests`. File size alone is not a defect, but it establishes that this is a substantial engine and compatibility project.

The application flow is `main.ts` → startup/application → resolved content recipe → `SharedSimulation` → session stepping → network and per-seat presentation. `ExecutableRecipe` separates geometry, entity source, movement, character, weapons, enemies, equipment, combat, inventory, rules, presentation, modules, and timing. See `src/contracts/content.ts:300` and `src/app/bootstrap/application.ts:4535`.

Several design choices should survive the rewrite:

- Actor handles include generations, while guest pointers and original entity slots retain separate identities.
- Live body state and the bounds visible at the last spatial link are distinct.
- Each gameplay mutation has an owner. Nested damage, touch, use, and death callbacks observe changes at the appropriate boundary.
- Source clock and traversal behavior survive independently of network format and rendering cadence.
- Mod instances retain private state, resources, declarations, and checkpoints, including two instances of the same program.
- Content identity includes edition, artifact digest, and mount precedence. Equal filenames do not imply interchangeable assets.
- Saves preserve semantic state and resource identities rather than renderer handles or host closures.

These are the accumulated interoperability knowledge worth retaining. Their current object representation is replaceable.

## Guest execution dominates the examined TypeScript Q2 workload

The Q2 paths called native execute original x86 or x64 machine code through TypeScript CPU emulation. The rerelease path maps a Windows x64 PE into a private guest address space and initializes its CPU, ABI, and Windows runtime. See `src/app/bootstrap/simulation/rerelease-guest-source.ts:26` and `:49`.

Historical evidence in [tick-execution.md](https://github.com/mgd34msu/Quake-Anthology-TS/blob/5d6c6e311db6749c0b8f8ffca53ebe2bae69b24e/docs/tick-execution.md), lines 66–80, attributes 84.69% of a sampled workload to interpreter execution, guest memory, and ABI conversion. That profile belongs to an earlier revision. It explains the likely cause but is not a fresh measurement or a promised C speedup.

A fresh probe ran the current checkout through the actual dedicated `Application`, loading complete retail `base1`, selecting Q2 movement/character and Q3 weapons, and admitting one idle client. It executed 100 active ticks, with the last 50 used for the following statistics. All 495 entity records remained present.

| Measured work | Median | p95 | Maximum |
|---|---:|---:|---:|
| Original RunFrame, including engine callbacks | 48.28 ms | 79.65 ms | 105.71 ms |
| Complete application step | 53.86 ms | 84.68 ms | 113.47 ms |

The rerelease interval is 25 ms. All 50 measured application steps exceeded it. The run executed 15,441,218 guest instructions and 50,731 nested ABI calls across its 100 active ticks. Source/configuration hashes for 2,090 inputs remained unchanged during the probe.

This was one instrumented run on an AMD Ryzen 9 5900X, pinned to CPU 8, using Bun 1.3.14. A full policy checker ran concurrently. Rendering and physical audio were absent. The fixed guest wall clock makes the workload repeatable, while timing used the real performance clock. The result establishes an over-budget workload, not uncontended throughput, graphical FPS, or a before/after comparison. The [evidence record](evidence/review-2026-09-26.json) retains the numbers and limitations.

The first C design response is to implement built-in gameplay as compiled C calling shared services directly. That removes guest instruction execution from this path. The TypeScript benchmark demonstrates a cost in the donor implementation; it does not establish a need to reproduce its CPU emulator in C.

Original external mods still require their program contracts. Prefer host-native execution for compatible machine-code modules, and implement actual QC/QVM bytecode support separately. Some binary composition paths use instruction-region interception, committed-write observations, nested callbacks, independent address spaces, and restoration. See `src/compat/q2/native-mod-region.ts:74`. Identify which external artifacts need such mechanisms, then choose an appropriate adapter or fallback. Keep those requirements out of built-in C gameplay.

If an external-binary case does require CPU translation or emulation, the donor's instruction caches and ABI knowledge are useful reference material. Their value is specific to that case. No measured C speedup or whole-engine bottleneck ranking follows from this review.

## Rendering has structural opportunities

The material evaluator builds transformed per-pass vertex objects. The GL backend traverses and packs them into client arrays, validates attributes, and submits draws. It reuses backing storage, but still repeats object traversal and CPU packing. See `src/materials/evaluate.ts:96`, `src/render/gl/buffers.ts:74`, and `src/render/gl/renderer.ts:292`.

The C design should retain static indexed meshes in GPU buffers, use compact draw records, and reuse dynamic upload storage. Suitable animation and material work can move to shaders after fidelity checks. Source pass ordering, transparency, fog, lightstyles, palettes, fullbrights, rerelease shadows, models, and attachments must survive.

Additional opportunities have concrete source mechanisms:

- `src/render/worker.ts:167` snapshots, encodes, sizes, and transfers command graphs. C can publish bounded frame buffers by handle without reproducing JavaScript serialization.
- `src/ui/common/draw.ts:25` emits individual picture/glyph commands. Adjacent compatible quads can be batched while retaining draw order and clipping.
- `src/render/scene/shadows.ts:174` hashes dynamic caster geometry using temporary float arrays and SHA-256. Owner-issued mesh, pose, material, and light revisions can replace content hashing where complete invalidation is reliable. Static geometry already caches its digest.
- `src/render/scene/visibility.ts:80` uses per-view object/collection work. Reused traversal scratch, visit generations, and decompressed-PVS caches are candidates.

These are verified mechanisms and optimization hypotheses, not measured graphical gains. The CPU renderer remains a required backend with real clipping, interpolation, depth, stencil, blending, and texture semantics.

## Audio responsiveness deserves its own redesign

Application rendering precedes the audio pump at `src/app/bootstrap/application.ts:4361`. The pump normally keeps at least 80 ms queued and uses at least 200 ms for its initial fill, subject to device limits. It also copies retained PCM while refilling. See `src/audio/engine.ts:500`.

A dedicated producer with preallocated voices, paint buffers, and PCM ring storage can make audio less dependent on a long simulation/render frame. The device callback should consume prepared samples. Source channel replacement, sound positions, sample timing, loop phases, attenuation, per-seat routing, and media synchronization remain requirements. Queue duration is not itself a measured end-to-end latency result.

## Shared state should become cheaper and easier to audit

`SharedSimulation` is 6,846 lines and `Application` is 4,750 lines. Scheduling, mod bindings, inventories, source projections, saves, travel, and presentation joins have accumulated in these owners. The C port should split decisions by ownership while keeping an explicit simulation order.

Spatial linking clones body/vector/bounds records, queries allocate arrays, publication reconstructs snapshots, and actor traversal can rebuild/sort after registry changes. See `src/world/spatial/index.ts:61`, `src/world/actors/body.ts:23`, and `src/app/bootstrap/simulation/runtime.ts:6815`.

Use stable slots, generational handles, compact hot fields, scratch buffers, and source-order indexes. Preserve observable allocation/reuse order, collision tie-breaking, link-time state, and callback visibility. Dirty tracking must observe every relevant writer, including guests and restoration. Removing copies by adding a second mutable authority would defeat the project.

## A reproduced save defect must not be inherited

An otherwise native Q1 session with an enabled stateful component accepts original v5/v6 save export even though those formats omit its component checkpoint. The eligibility check at `src/app/bootstrap/simulation/runtime.ts:6213` checks several mixed selections but not active independent mod state. `src/app/bootstrap/application.ts:3239` then writes only the original Q1 save.

The reproducer loaded real id1 `progs.dat` and `e1m1`, with a stateful prepared component fixture based on the existing mod-save test. A shared checkpoint retained one mod. Both original exports succeeded with 163 entities and no component checkpoint. The lead reran this result independently. This proves the simulation/export boundary gap; it was not a menu-driven production-mod playthrough.

The C contract should reject a legacy export that cannot represent the session and offer the full shared save. Successful export must never quietly discard a component. Existing shared saves and original save workflows also need tested import/continuation in the C engine.

## Evidence quality and remaining uncertainty

Current target status records 22 of 23 accepted functional targets, with native execution performance open. It explicitly limits claims about full campaigns, every combination, physical devices, native peers, and pixel parity. Some older plans and guides still describe obsolete checkpoints. Their product requirements remain useful; their completion counts are not current evidence.

The formal registry in `verification/suites.json` contains eight unbound gameplay obligations with null commands and schedules. [The verifier README](https://github.com/mgd34msu/Quake-Anthology-TS/blob/5d6c6e311db6749c0b8f8ffca53ebe2bae69b24e/tools/verify/README.md) says no engine runs through that registry. Focused tests and local integration receipts supply much of the actual evidence. Reuse useful checks, then connect the relevant ones to each C implementation increment. Do not spend the opening phase building another exhaustive framework before playable code.

The review ran these bounded checks:

| Check | Result |
|---|---|
| Full `bun run typecheck` | 28 diagnostics, all in tests |
| Guest ABI, memory, floating point, QVM hooks, movement | 137 passed |
| Network, persistence, settings, verification tooling | 87 passed, 1 failed |
| Rendering, worker transport, input lifetime, audio | 53 passed, 4 native-GL tests skipped |
| Actor order, provider clocks, mod composition/lifetime, save ownership | 22 passed, 1 skipped |
| Current KEX recorded-viewpoint test | 1 passed |
| Full policy checker | Stopped after 601 seconds without completion; unverified |

The failing network test expects separate KEX frame histories. The newer implementation stores the split players in a shared frame history, and the corresponding current viewpoint test passes. This is evidence of stale test expectations, not independent proof that the retail protocol is correct. No source correction was made during this review.

Exact test commands, probe identity, input state, and temporary raw-log locations are recorded in the evidence JSON. No full test suite, foreground graphical playthrough, device listening test, or native-peer matrix was run. A broad all-games acceptance claim would therefore be premature.

## Assessment

The project supplies an implementation of unified Quake behavior and a detailed account of difficult compatibility boundaries. My architectural assessment favors retaining its shared-world intent with native C gameplay and direct service calls. The review identifies guest execution, representation/copying, graphics preparation, and frame-coupled audio as areas to redesign; gains across the complete engine remain unmeasured.

The rewrite should preserve the product and its ownership contracts while replacing mechanisms that work poorly in C. Full interoperability remains the target even where the present checkout has gaps. Success means the games and mixed configurations work correctly and smoothly through real user workflows, with measured performance and explicit completion evidence.
