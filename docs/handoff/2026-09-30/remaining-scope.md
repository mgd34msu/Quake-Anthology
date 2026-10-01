# Complete original requirements and dependency scope

Source: docs/dependencies.json, schema 1, plan revision 7. This appendix reproduces original goals/prerequisites/criteria. Stored task status is historical bookkeeping and is deliberately not presented as fresh acceptance.

## AUDIT — Audit all prior work against the dependency graph

Have independent source readers establish what every existing task actually implements and have Jev judge that evidence against the task's existing graph goal and acceptance criteria; fix confirmed defects and unsupported completion claims.

Prerequisites: none.

Acceptance criteria:

- Every written production source file, public interface, build definition, and existing task report is assigned to a review lane and recorded with a source revision or snapshot.
- For each B00-B34 task, the audit states its existing goal and criteria, source evidence for each criterion, donor behavior comparisons, missing functionality, known defects, and the limits of source-only validation.
- Jev judgments are actually obtained and recorded for task evidence against the existing graph criteria. Unavailable judgments do not count as passes. Reports alone do not count as judgments.
- Confirmed defects are corrected by the source owner and independently re-reviewed. Missing source functionality and unsupported completion claims remain open in their original tasks.
- No project configuration, compilation, tests, executable runs, sanitizer runs, benchmarks, or gameplay evaluation occur during this audit; those remain after BASELINE.

## B00 — Register the implementation graph

Publish the complete dependency graph and current user rules to vibecheck-jev; keep a reviewable local copy.

Prerequisites: none.

Acceptance criteria:

- Ledger and local graph cover all 23 donor functional targets; baseline completion precedes gameplay evaluation and deep enhancement.

## B01 — C build and core foundation

Implement the C17 build, error and ownership types, checked binary access, file I/O, memory utilities, and native executable entry.

Prerequisites: none.

Acceptance criteria:

- C17 build definitions, core implementation and native entry are written and source-reviewed; no Bun runtime dependency. Compilation and runtime validation are deferred to P01.

## B02 — Archive readers

Implement shared PAK and ZIP/PK3/PK4 readers with ordered entries, normalized paths, stored/deflated extraction, and corruption checks.

Prerequisites: B01.

Acceptance criteria:

- Archive readers implement ordered extraction and malformed-input rejection; stored data is shared without repeated copying. Source review covers ownership and bounds; runtime validation is deferred to P01.

## B03 — Mounts and shared resource ownership

Implement loose/archive mounts, precedence, content identity, caching, lifetimes, and writable overlays.

Prerequisites: B02.

Acceptance criteria:

- One mount/resource service resolves identities and shares immutable data across game families without merging distinct content.

## B04 — Map formats

Implement Q1/Q2/Q3 map formats and required variants, record readers, entity text, visibility, and extensions.

Prerequisites: B01.

Acceptance criteria:

- Donor-supported BSP variants have checked C readers; source records and extensions retain required semantics.

## B05 — Models and images

Implement model/image formats, palettes, sprites, animation data, replacements, and attachments.

Prerequisites: B01.

Acceptance criteria:

- All required donor formats have C readers and common retained resources; no supported format silently drops records.

## B06 — World identity and collision

Implement generational actors, source slots, authoritative bodies, link state, geometry, collision, and spatial queries.

Prerequisites: B04.

Acceptance criteria:

- One shared world implements source link timing, allocation/reuse, query shapes, and tie ordering; source review covers those contracts.

## B07 — Session scheduling and lifetime

Implement source clocks, callback order, mutation authority, session lifecycle, transition coordination, and component lifetime.

Prerequisites: B06.

Acceptance criteria:

- Scheduling and nested mutation have explicit owners; creation, teardown, actor reuse, and rollback are implemented and source-reviewed.

## B08 — Movement and player commands

Implement all selected Q1/QW/Q2/Q3 and rerelease movement policies, player command processing, and prediction kernels.

Prerequisites: B06, B07.

Acceptance criteria:

- Native C kernels support independent per-player movement with required source cadence, bounds, and numeric decisions.

## B09 — Combat inventory and composition

Implement shared damage, inventory, pickups, capacities, effects, component operations, and attack provenance.

Prerequisites: B07.

Acceptance criteria:

- Each mutation has one authority; ordered transforms, observers, replacements, and private state retain required behavior.

## B10 — Q1 built-in gameplay

Implement Anthology Q1/QW base, expansions, rerelease, monsters, weapons, items, and source gameplay in native C.

Prerequisites: B08, B09.

Acceptance criteria:

- Full Q1 family roster and behavior are implemented through shared services, including mission-pack and rerelease distinctions.

## B11 — Q2 built-in gameplay

Implement Anthology Q2 classic, expansions, rerelease, monsters, weapons, items, and source gameplay in native C.

Prerequisites: B08, B09.

Acceptance criteria:

- Full Q2 family roster and behavior use compiled gameplay; stock and rerelease selections do not invoke CPU emulation.

## B12 — Q3 built-in gameplay

Implement Anthology Q3 and Team Arena gameplay, weapons, items, characters, and source rules in native C.

Prerequisites: B08, B09.

Acceptance criteria:

- Q3 and Team Arena behavior is implemented through shared services and remains independently selectable.

## B13 — Campaigns and authored interactions

Implement entity target graphs, mission gates, keys, sigils, bosses, hubs, revisits, authored mechanisms, and travel.

Prerequisites: B10, B11, B12.

Acceptance criteria:

- Campaign code preserves authored obligations when actors or equipment are replaced; transitions have one owner.

## B14 — Modes objectives and equipment

Implement independent match modes, teams, scoring, cross-map objective adaptation, grapples, and offhand equipment.

Prerequisites: B10, B11, B12.

Acceptance criteria:

- Required modes and equipment variants are connected to shared gameplay without coupling map, arsenal, or movement selection.

## B15 — Product catalog and configuration

Implement installed products, editions, launch recipes, independent selections, preset defaults, and mod catalog.

Prerequisites: B03, B13, B14.

Acceptance criteria:

- Configuration resolves the full feature union with atomic changes and explicit source identities.

## B16 — Scene materials and resources

Implement shared scene, visibility, materials, models, lights, effects, and frame/resource ownership.

Prerequisites: B03, B04, B05.

Acceptance criteria:

- Required family-specific presentation behavior uses one scene/resource system with CPU/GL backend contracts.

## B17 — CPU renderer

Implement the complete software rasterizer and source rendering semantics.

Prerequisites: B16.

Acceptance criteria:

- Clipping, textures, palettes, interpolation, depth, stencil, fog, and blending are implemented and source-reviewed; execution checks follow BASELINE.

## B18 — GL renderer and display

Implement native GL rendering, retained buffers, SDL window/context lifecycle, capture, and backend restart.

Prerequisites: B16.

Acceptance criteria:

- Required render features are wired to real native APIs with correct resource lifetime; no frame-time tuning gate.

## B19 — Audio and media

Implement decoding, music, source voice policy, mixing, per-seat audio, cinematics, synchronization, and devices.

Prerequisites: B03, B01.

Acceptance criteria:

- All required audio/media formats and policies have shared C implementations and source-reviewed lifecycle handling.

## B20 — Input and local seats

Implement keyboard/mouse/controllers, gyro, haptics, text input, device lifetime, and four local seats.

Prerequisites: B01, B08.

Acceptance criteria:

- Per-seat routing and state remain independent; focus loss, unplug, and reassignment release owned input correctly.

## B21 — Console settings localization

Implement commands, cvars, profiles, bindings, settings, text/font resources, localization, and accessibility.

Prerequisites: B01, B03.

Acceptance criteria:

- Source command semantics and persistent configuration are exposed through shared services.

## B22 — QC compatibility

Implement external QuakeC programs, host imports, private instances, composition, and checkpoints.

Prerequisites: B03, B07, B09.

Acceptance criteria:

- Original QC roles and required extensions connect to shared services; built-in gameplay remains native C.

## B23 — QVM compatibility

Implement external QVM game, cgame, and UI roles, syscall profiles, private instances, composition, and checkpoints.

Prerequisites: B03, B07, B09.

Acceptance criteria:

- Required QVM semantics and source profiles work through shared engine boundaries without another world.

## B24 — Native module compatibility

Implement required original native module ABIs using host-native execution where suitable and scoped fallbacks where required.

Prerequisites: B03, B07, B09.

Acceptance criteria:

- Required binary callbacks, private instances, composition, and saved state are implemented; no default port of TS CPU machinery.

## B25 — Independent mod integration

Integrate multiple cross-game mod instances, controls, ownership, dependency order, presentation, commands, and objectives.

Prerequisites: B14, B15, B22, B23, B24.

Acceptance criteria:

- Actual module adapters compose through common operations; enable/disable, private state, and rollback are implemented.

## B26 — Bots and navigation

Implement one Q3-derived player-bot core, useful rerelease features, shared navigation generation, and source monster paths.

Prerequisites: B06, B08, B09, B14.

Acceptance criteria:

- Bots use ordinary engine actions and selected gameplay; foreign maps have usable navigation implementation.

## B27 — Network transport and codecs

Implement original protocol dialects, transport, reliability, per-seat connections, IPX/KEX requirements, and Anthology packets.

Prerequisites: B01, B07.

Acceptance criteria:

- Explicit codecs preserve required dialects; one connection authority serves native and mixed sessions.

## B28 — Network session and prediction

Integrate authoritative remote play, commands, snapshots, prediction, travel, reconnect, and full mixed-session identities.

Prerequisites: B08, B09, B15, B27.

Acceptance criteria:

- Native and Anthology session state is wired to gameplay with correct command ownership and replay behavior.

## B29 — Downloads browser and administration

Implement downloads, remounting, server discovery, hosting, authenticated administration, and rotation.

Prerequisites: B03, B21, B27.

Acceptance criteria:

- Required acquisition/discovery/admin flows have production consumers and cleanup behavior.

## B30 — Saves recovery and demos

Implement shared/original saves, migration, level-entry autosaves, recovery, demos, MVD/GTV, journals, and VCR.

Prerequisites: B07, B13, B25, B28.

Acceptance criteria:

- Persistence retains all owners; legacy exports reject unrepresentable state; save/demo formats have explicit codecs.

## B31 — Progression and player services

Implement progress, achievements, records, Q3 unlocks, lobbies, and ranking-provider interface.

Prerequisites: B13, B14, B27.

Acceptance criteria:

- Required local/player services have consumers; only the previously approved unavailable ranking backend remains an extension point.

## B32 — Menus HUD and guest presentation

Implement native menus, per-seat HUD, source/guest UIs, content selection, independent mod controls, and all settings.

Prerequisites: B15, B16, B19, B20, B21, B25, B29, B30, B31.

Acceptance criteria:

- Every required menu/HUD/service workflow is connected to real engine implementations.

## B33 — Tools cameras capture and LLM

Implement camera/tools/diagnostics/capture and existing LLM provider, cancellation, and command integration.

Prerequisites: B16, B21, B27.

Acceptance criteria:

- Required public tools and assistance have native application consumers; no invented external service.

## B34 — Application integration and packaging

Wire the complete baseline executable, dedicated mode, startup/shutdown, delivery layout, and asset discovery.

Prerequisites: B10, B11, B12, B13, B14, B15, B17, B18, B19, B20, B21, B25, B26, B28, B29, B30, B31, B32, B33.

Acceptance criteria:

- All required subsystem implementations have actual source callers and build definitions; no placeholders or disconnected feature implementations. Build execution is deferred to P01.

## BASELINE — Complete baseline release code

Finish the entire baseline implementation before gameplay evaluation and the deep performance pass.

Prerequisites: B00, B01, B02, B03, B04, B05, B06, B07, B08, B09, B10, B11, B12, B13, B14, B15, B16, B17, B18, B19, B20, B21, B22, B23, B24, B25, B26, B27, B28, B29, B30, B31, B32, B33, B34, AUDIT.

Acceptance criteria:

- All required production code and application integration are written with reviewed ownership. This is the source-completion gate; compilation, execution checks, gameplay evaluation and final qualification follow in P01 and later phases.
- AUDIT is complete: all pre-recovery work has source evidence judged by Jev against its existing graph task criteria, and confirmed source defects and unsupported completion claims have been addressed.

## P01 — Build verification and full functionality evaluation

After all baseline source is complete, compile, validate correctness and evaluate full campaigns, native/mixed modes, mods, saves, networking, rendering, audio and devices; fix defects.

Prerequisites: BASELINE.

Acceptance criteria:

- Compile with GCC and Clang, run boundary/ownership/logic checks and sanitizers, then evaluate complete campaigns and interoperability. Record actual coverage and resolve required behavior failures without removing functionality.

## P02 — Deep performance pass

Profile the complete engine and make deep CPU, GPU, memory, allocation, threading, and latency improvements.

Prerequisites: P01.

Acceptance criteria:

- Matched real workloads demonstrate improvements while preserving required behavior, content, effects, and simulation work.

## P03 — Enhancement and polish

Complete presentation, interaction, responsiveness, and further worthwhile enhancements on the completed engine.

Prerequisites: P02.

Acceptance criteria:

- Enhancements preserve the entire feature union and have proportionate validation.

## RELEASE — Final full-project qualification

Qualify the completed and enhanced engine for delivery with all declared required functionality.

Prerequisites: P03.

Acceptance criteria:

- Required campaigns, configurations, mod and peer workflows, packaging, and performance results are accounted for; limitations are explicit and no required work is silently dropped.

## Functional targets

The source map is scope accounting, not proof of source/runtime completion. Its plan revision is 6 while the dependency graph is revision7; reconcile this recorded metadata drift against the actual current plan.

| Target | Responsibility | Implementation tasks |
|---|---|---|
| T01 | Session and resource lifetime | B03, B07, B15, B16, B18, B19, B20, B21, B28, B30, B32, B34 |
| T02 | Content and asset loading | B02, B03, B04, B05, B15, B16, B19, B29 |
| T03 | Rendering and visual effects | B05, B16, B17, B18, B32 |
| T04 | Audio and music | B19 |
| T05 | Input and local players | B08, B20, B21, B28, B32 |
| T06 | Console, cvars, and profiles | B21, B32 |
| T07 | Network connections and prediction | B08, B27, B28 |
| T08 | Downloads and content acquisition | B03, B29 |
| T09 | Server discovery and administration | B21, B27, B29, B32 |
| T10 | Gamecode and mod execution | B09, B10, B11, B12, B15, B22, B23, B24, B25, B30, B32 |
| T11 | Collision, movement, and scale | B05, B06, B07, B08, B09, B16, B28 |
| T12 | Combat, rosters, pickups, and equipment | B09, B10, B11, B12, B14 |
| T13 | Bots, AI, and navigation | B10, B11, B12, B26 |
| T14 | Map entities and campaigns | B04, B07, B10, B11, B12, B13, B19, B30 |
| T15 | Match modes and objectives | B14, B15, B28, B29, B31, B32 |
| T16 | Saves, autosaves, and recovery | B07, B13, B22, B23, B24, B25, B30, B32 |
| T17 | Menus and HUD | B15, B16, B20, B21, B25, B32 |
| T18 | Localization and accessibility | B19, B21, B32 |
| T19 | Progression and player services | B13, B14, B31, B32 |
| T20 | Demos, recording, and replay | B27, B28, B30, B32 |
| T21 | Cinematics and animated media | B16, B19, B32 |
| T22 | Cameras, diagnostics, and tools | B16, B18, B21, B33 |
| T23 | LLM assistance | B21, B33 |

## Reference source directory allocation

### src/app

Tasks: B07, B08, B09, B10, B11, B12, B13, B14, B15, B16, B18, B19, B20, B21, B22, B23, B24, B25, B26, B28, B29, B30, B31, B32, B33, B34.

Split bootstrap simulation, gameplay, selection, presentation, networking, persistence, and services into their typed C owners. B34 owns startup and final application wiring; it does not absorb the large donor simulation class.

### src/audio

Tasks: B19.

Shared mixer, sample resources, source voice policies, geometry, and native device delivery.

### src/bots

Tasks: B26.

One player-bot core, shared navigation, source skills and objective behavior.

### src/camera

Tasks: B33.

Camera services and source camera behavior.

### src/capture

Tasks: B18, B33.

Renderer readback and public capture consumers.

### src/compat

Tasks: B10, B11, B12, B22, B23, B24, B25, B32.

Compile built-in behavior as C; retain external QC, QVM and native module contracts in scoped adapters. Connect guest game, cgame, UI and composition roles to shared services.

### src/console

Tasks: B21.

Shared command registration, parsing, execution, completion, and source semantics.

### src/content

Tasks: B02, B03, B09, B10, B11, B12, B14, B15, B25.

Separate archive/mount/cache ownership, catalog/configuration, gameplay roster data, equipment selection, and mod manifests.

### src/contracts

Tasks: B01, B03, B06, B07, B08, B09, B14, B16, B22, B23, B24, B25, B27, B32.

Translate required contracts into owning C module interfaces; TypeScript type wrappers and built-in guest-address plumbing are not required architecture.

### src/core

Tasks: B01, B07, B08, B21.

Native binary/math/memory/RNG support, source scheduling, and shared commands/cvars; retain required numeric decisions rather than JavaScript wrappers.

### src/debug

Tasks: B33.

Diagnostics and debug drawing consumers.

### src/formats

Tasks: B04, B05.

Map readers belong to B04; model and image readers belong to B05. Common checked binary access comes from B01.

### src/guest

Tasks: B24.

Treat CPU, ABI, executable-loader, operating-system, and floating-point machinery as reference for required external binaries only. Prefer host-native paths and implement a scoped fallback only where a concrete compatibility requirement needs it.

### src/input

Tasks: B20.

Shared devices and independent local-seat routing, commands, gyro, haptics, and focus lifetime.

### src/llm

Tasks: B33.

Provider, credential, model/effort, cancellation, and public command integration.

### src/materials

Tasks: B16.

Shared material interpretation and retained render resources for both backends.

### src/media

Tasks: B19, B16.

Shared movie/image/audio decode and timing in B19, with material texture consumers in B16.

### src/movement

Tasks: B08.

Shared movement operations and independently selectable source kernels.

### src/network

Tasks: B27, B28, B29, B30, B31.

Split transport/dialect codecs, session prediction, discovery/download/admin, recorded protocols, and player-service contracts. One connection authority serves them.

### src/persistence

Tasks: B30.

Save codecs, shared snapshots, import/export eligibility, recovery, and demo continuity.

### src/platform

Tasks: B01, B18, B19, B20, B27, B33, B34.

Use native OS/library APIs for files, display, audio, input, transport, provider services, and lifecycle. Remove Bun FFI and worker serialization.

### src/render

Tasks: B16, B17, B18, B33.

Shared scene/commands/resource ownership, complete CPU backend, native GL backend, and capture/debug consumers.

### src/settings

Tasks: B15, B21, B29, B32.

Resolve source selections in B15, persist common values in B21, expose server settings in B29 and menu controls in B32.

### src/text

Tasks: B21, B32.

Shared localization, glyph/font resources, wrapping, captions, and native UI consumers.

### src/types

Tasks: B05.

Replace PNG TypeScript declarations with the actual C image-decoder API. No declaration-only runtime subsystem is needed.

### src/ui

Tasks: B15, B19, B20, B21, B25, B29, B30, B31, B32.

B32 owns menus/HUD; typed owners supply content, media, input, settings, mods, server browsing, saves, and player services.

### src/world

Tasks: B06, B07, B09, B10, B11, B12, B13, B14, B25, B26.

One actor/body/link/spatial authority in B06; scheduling in B07; gameplay, campaigns, modes, composition, and AI belong to their dedicated owners.

