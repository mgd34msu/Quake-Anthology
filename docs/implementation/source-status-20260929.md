# Source implementation status, September 29, 2026

This is a snapshot of the live checkout, including uncommitted implementation. It is not a release-readiness or runtime-coverage report. All percentages are unverified engineering estimates of the required source implementation in each named area, rounded to five percent. They are not calculated from line counts, file counts, passing tests, or ledger task counts. Family and current-work estimates incorporate the active source owners' reports; shared-service estimates use the current source inventory, public contracts and audit evidence. Source presence does not establish correctness or completeness. Unclosed donor coverage can change these estimates.

Every row's completeness, state and remaining-work assessment is provisional and unverified. The written-source column identifies inspected source or owner-reported implementation, not verified feature functionality. The comparison that engine libraries are further along than the application is also an unverified engineering assessment. The category names below are reporting definitions, not verification outcomes. An exhaustive donor-to-C behavior mapping has not established the percentages.

"Mostly written" means the subsystem has substantial C implementation but still needs the work listed. "Active" means implementation or integration is underway, not that every listed subfeature has a dedicated worker. "Remaining" means the complete workflow still needs implementation, even where supporting libraries exist. No whole game family or engine subsystem is being accepted as fully complete in this table. A bounded independently reviewed source packet is distinguished from complete feature acceptance below.

The [dependency graph](../dependency-graph.md) gives exact prerequisites. B00-B34 and AUDIT must finish before BASELINE; P01 compilation/functionality checks, P02 deep performance, P03 enhancement and RELEASE follow. Compilation, runtime behavior, performance and release readiness remain unverified.

## Engine and shared services

| Area | Estimated source completion | State | Written source | Work still required | Graph |
| --- | ---: | --- | --- | --- | --- |
| Core C utilities and platform files | ~95% | Mostly written | Memory arenas, binary/text access, errors, hashes, strings, numeric helpers, POSIX/Windows file boundaries | Remaining source acceptance and complete application startup use | B01 |
| Content: archives | ~95% | Mostly written | PAK and ZIP-family readers, ordered extraction, corruption checks | Close full required-format/source acceptance | B02 |
| Content: mounts and shared resources | ~90% | Mostly written | Loose/archive mounts, precedence, identities, cached immutable resources, writable overlays | Downloads/remounting, final application resource lifecycle | B03 |
| Content: BSP maps | ~90% | Mostly written | Q1/Q2/Q3 format readers, entities, visibility, extension records | Close variant/extension coverage and consumers | B04 |
| Content: models and images | ~85% | Mostly written | Alias/skeletal models, sprites, indexed/raster images, palettes, transforms and metadata | Full replacement/attachment coverage and presentation consumers | B05 |
| World: actors, bodies and collision | ~90% | Mostly written | Generational actors, source slots, authoritative bodies, spatial queries, all three BSP collision families | Complete guest body projection and remaining source-specific contracts | B06 |
| Session: clocks and callbacks | ~90% | Active | Source clocks, scheduling, ordered actor turns, safe retirement/admissions, nested invocation, source arsenal callback | Remaining guest/composition lifecycle and checkpoint integration | B07 |
| Movement: source kernels | ~85% | Mostly written | NetQuake, QuakeWorld, Q2 classic/rerelease, Q3, entity physics and prediction kernels | Selected guest ingress, mode/posture/environment projection | B08 |
| Movement: application controls/local players | ~65% | Active | Canonical control records, selected movement, camera/cutscene state, native seat admission | Complete guest commands, local input loop, travel edge cases | B08, B20, B34 |
| Shared combat, inventory and pickups | ~80% | Active | Damage/armor policies, inventory, grants, provenance, composition operations, pickup observation registry | Complete foreign guest mutation adapters and production pickup observations | B09, B25, B26 |
| Video: shared scene/world/model assembly | ~85% | Active | Retained resources/geometry, world surfaces, patches, models, visibility, portals, lights/effects | Foreign appearance lighting and final frame/event consumers | B16 |
| Video: materials/shaders | ~85% | Active | Parsing, stages, animation/deformation, coordinates, remapping, shared ordering | Bind all source presentation and movie consumers | B16, B19 |
| Video: OpenGL renderer | ~85% | Mostly written | Native GL calls, programs/passes, retained buffers/textures, depth/stencil/fog/blending, readback | Actual application frame loop, restart/capture lifecycle consumers | B18, B34 |
| Video: software renderer | ~85% | Mostly written | Clipping, rasterization, texture sampling, depth/stencil/fog/blending, capture | Actual backend selection/frame loop and remaining source acceptance | B17, B34 |
| Video: SDL display/context | ~85% | Mostly written | Window/context lifetime, presentation and replacement/restart APIs | Startup/settings/backend restart integration | B18, B21, B34 |
| Audio: sound/music/mixer | ~85% | Mostly written | Decoders, sound bank, voices/listeners, per-seat policy, mixing, streams, music, reverb, device output | Application event draining, seat listeners, settings/restart lifetime | B19, B34 |
| Media: cinematics and animated materials | ~85% | Mostly written | Q2 CIN, RoQ, Ogg/Theora/Vorbis, playback clocks, audio feeding, scene images/material movies | Frontend playback and campaign travel completion | B19, B13, B32 |
| Input: devices and bindings | ~85% | Mostly written | Keyboard/mouse/controller/gyro/haptics/text, four seats, hotplug/focus/reassignment handling | Complete frontend event loop, join/leave/menu/game routing | B20, B32, B34 |
| Console, cvars, profiles and settings | ~85% | Mostly written | Source commands/cvars, aliases/queues, config persistence, profiles, bindings and restart services | Application registrations; complete save continuation for queued/private state | B21, B30, B34 |
| Fonts, localization and captions | ~85% | Mostly written | Shared glyph/font resources, source font exports, layout/wrapping, localization/caption services | All menu/HUD/accessibility workflow consumers | B21, B32 |

## Native gameplay, campaigns and compatibility

| Area | Estimated source completion | State | Written source | Work still required | Graph |
| --- | ---: | --- | --- | --- | --- |
| Q1/QW: base gameplay | ~80-85% | Active | Characters, weapons/projectiles, items/powers, monsters, native physics/game callbacks | Remaining QW/team distinctions, family coverage and application/save integration | B10 |
| Q1: expansions and rereleases | ~70-75% | Active | Hipnotic/Rogue/addon families; hazards, rotation/train, addon visuals and field triggers written; native weapon observations added | Remaining addon brush/cinematic/controller paths, Rogue/CTF continuation, family coverage and independent review | B10, B13 |
| Q1: authored map interactions | ~70% | Active | Targets, triggers, doors/platforms, rotation/train, addon field triggers, mission gates, keys/sigils/boss/finale logic | Remaining addon brush/cinematic paths, typed mover consumers and foreign target mutation | B10, B13 |
| Q2: weapons/items/player | ~75-85% | Active | Native weapon families, items/powers, player lifecycle, environment, spectators/intermission; native pickup observations independently source-reviewed | Selected arsenal cadence/input, composed effects, source observation consumers, scoring and persistence | B11 |
| Q2: monsters, expansions and rerelease | ~70-80% | Active | Broad roster/move tables, corpse/revival/reinforcement paths; Widow/Widow2 packet reviewed | Other species attack/melee fidelity, presentation/debris and full family closure | B11 |
| Q2: authored map interactions | ~75-85% | Active | Brush movers, targets/triggers, turrets, routes, Q64/rerelease controls and checkpoints | Final selected callbacks, foreign target/campaign/presentation consumers | B11, B13 |
| Q3/Team Arena: weapons/player/items | ~80-90% | Active | Weapons/missiles, players, items/holdables, damage/death/feedback/views | Remaining source projections, pickup observations, application/save consumers | B12 |
| Q3/Team Arena: authored map interactions | ~85-95% | Active | Typed targets/triggers/movers/shooters/items/spawns and private map checkpoints | Complete scene/audio/travel/save consumers and donor coverage | B12, B13 |
| Match modes, teams, objectives and equipment | ~75% | Mostly written | Shared teams/scoring/match phases, flags, arena/horde/tag/deathball/relics, equipment | All independent selections, once-only scoring/ranking/objectives, persistence | B14 |
| Campaign orchestration | ~50% | Active | Family authored callbacks, progression/travel flags and queued transitions | Complete endings/media acknowledgment, unit/revisit state and save orchestration | B13, B30 |
| Map loading/publication | ~85% | Active | Shared BSP/collision publication, native spawning, transactional admission and force reload | Guest map adapters and remaining source/refutation review | B13, B15, B34 |
| Player admission and travel carry | ~60-65% | Active | Fresh actor roster, source client ordinals, selected hulls/loadouts, native carry and respawn | Guest admission, unit/private continuation, foreign spawn edge cases | B08, B13, B34 |
| Typed map/media travel | ~80% | Active | Revision-guarded queued routes, landmarks, nextserver chains and explicit completion | Central commands, intermission and frontend playback acknowledgment | B13, B19, B34 |
| Product catalog and launch configuration | ~80% | Active | Installed products/editions, independent roles/presets, retained drafts/snapshots, catalog rebase and staged prepare/commit | Remaining runtime constructors, qualification and complete launch workflows | B15, B25 |
| QuakeC interpreter/profiles | ~85% | Mostly written | QC execution, memory/records, profiles and private instances | Close profile/extension and continuation coverage | B22 |
| QuakeC shared engine host | ~70% | Active | Typed calls/entities, spatial/movement/physics services and checkpoint wrapper | Shared combat/inventory projection, rerelease bot/UI/debug imports | B22, B09 |
| QuakeC application integration | ~45% | Active | Constructor dispatch, imports/resources/map/client/frame/message adapters, qualified declaration/input and secondary map initializer source | Complete foreign role projection and source-map markers, protocol consumers, save owner and client/travel lifecycle | B22, B25, B30, B34 |
| QVM interpreter/records | ~85% | Mostly written | QVM loading/execution/memory/records/intrinsics and profiles | Full profile closure and role continuation integration | B23 |
| Q3 guest engine syscalls | ~75% | Active | World/cvars/files/input/scripts/bots; new scene/audio/media/font bridge | Complete trap coverage, real frontend/server binding and role ownership | B23, B24, B32 |
| QVM/native Q3 application roles | ~35% | Active | Construction dispatch, map/frame/client/server-store/cleanup adapters, source-client effects and qualified QVM input; native execution staged until committed activation | Complete canonical admission/bot roster, secondary map ownership, native mixed hooks, snapshots, frontend events, unload lifetime and saves | B23, B24, B34 |
| Native module execution/ABIs | ~60% | Mostly written | Native loader/ABI records, host imports, direct execution and explicit runner boundary | Full Q2/Q3 app owners and complete module mutable-state continuation | B24, B30, B34 |
| Simultaneous cross-game mod composition | ~25% | Active | Independent selections, shared operations/admissions, private instance foundations | Actual QC/QVM/native instances composed through all selected roles, lifecycle/save/rollback | B25 |

## Bots, networking and player-facing workflows

| Area | Estimated source completion | State | Written source | Work still required | Graph |
| --- | ---: | --- | --- | --- | --- |
| Bots: navigation and source libraries | ~80% | Active | AAS/native navigation resources, routes/prediction, movement/goals/chat/knowledge libraries | Typed mover/door metadata and selected prediction environment | B26 |
| Bots: native decisions/combat/team/chat | ~65% | Active | New native combat/decisions, ordinary actions, team/chat and checkpoint source | Activation/advanced aim/team continuations and full decision integration | B26 |
| Bots: application population | ~55% | Active | Runtime/population construction, canonical observations, semantic input, pickup goals; new root publication/retirement/readiness consumers awaiting source review | Text commands, remaining mover metadata, guest observations and save reconstruction | B26, B34 |
| Networking: transports/reliability/codecs | ~80% | Mostly written | Q1/QW/Q2/Q3/rerelease/Anthology dialects, channels, reliability, UDP/IPX/KEX/SOCKS boundaries | Complete connected application consumers and protocol coverage | B27 |
| Networking: session/prediction helpers | ~65% | Mostly written | Protocol client/server/session stores, histories, deltas and source prediction kernels | Shared authoritative command/snapshot/travel/reconnect owner | B28 |
| Networking: actual integrated remote play | ~15% | Remaining | Supporting protocol/session/control services | Complete server/client loops, canonical mixed identities, prediction/replay and travel | B28, B34 |
| Downloads and content acquisition | ~0-5% | Remaining | Archive/VFS/remount foundations | Actual download owner, requests, cancellation, validation and publication | B29 |
| Server browser/discovery/admin workflows | ~10% | Remaining | Protocol discovery/handshake and console foundations | Browser model/UI, hosting/admin authentication, rotation and command routing | B29, B32 |
| Saves: subsystem checkpoints | ~65% | Active | Typed native game/map/mode/navigation/bot/VM checkpoints | Full mutable native module state, rebuilt actor/resource bindings and explicit codecs | B30, B22-B26 |
| Saves: complete engine files/recovery | ~5% | Remaining | File/transaction primitives and subsystem checkpoints | Unified save owner, original formats/export rejection, migration, level-entry autosaves/recovery | B30 |
| Demos/MVD/GTV/VCR application workflows | ~15% | Remaining | Q1 demo and Q2 MVD/GTV/protocol record support | Unified recording/replay/journal owner and complete timeline/seek workflows | B30, B28 |
| Progress/achievements/Q3 unlock services | ~75% | Mostly written | Durable progress and arena progression services | All gameplay round/archive and frontend consumers | B31 |
| Lobby and ranking services | ~65% | Active | Shared lobby/ranking lifecycles and reviewed Q3 ranking producers | Accounts/player mapping, begin/end, once-only event routing and UI | B31, B32 |
| Native menus and independent mod controls | ~35-45% | Active | Controller/input/draw, staged individual mod controls, shared draft rebase, library launch and ranking account workflows; two list-input repairs independently source-reviewed | Complete match/settings/saves/browser/lobby menus and frontend consumers | B32 |
| Per-seat HUD | ~20-30% | Active | Shared stats/inventory/messages/captions/score/timer core; Q2/rerelease layout and wheel/carousel packets independently source-reviewed | Source/frontend consumers and outer leases, remaining family layouts, Q1 authored wheels and POI/directional workflows | B32 |
| Q3 guest presentation | ~90% | Active | Reviewed render/picture/remap/assets/audio/cinematic/portal packet | Final bridge review and actual frontend resources/frame consumers | B32, B16, B23 |
| Camera paths/editor tools | ~0-5% | Active | Gameplay camera/cutscene control foundations; new camera/tool owner implementation underway | Complete camera grammar/path/spline/controller, editor and user commands | B33 |
| Capture and visual diagnostic workflows | ~20% | Active | Renderer color/depth/stencil readback and image foundations; capture/tool owner underway | Exclusive capture naming, commands, visual diagnostics and lifecycle | B33 |
| LLM assistance/provider/cancellation | ~0% | Active | Donor provider/authorization/stream scope mapped; shared native HTTP contract/backend underway | Provider transports, authorization/preferences, stream/cancel, model catalogs and ordinary command workflow | B33 |
| Complete application/bootstrap/dedicated/package | ~30% | Active | Shared application owner/configuration/world/native providers/events/map/control source | Platform loop, service factories, guest dispatch, network/saves/tools, dedicated mode and delivery | B34 |

## Current workers

Live listing shows the coordinator and seven running workers, without descendants.

| Worker | Current implementation |
| --- | --- |
| Q1 | Remaining addon brush/controllers; native weapon metadata and observation/frame lifetime repairs |
| Q2 | Qualified QVM input/movement/weapon hooks, native role cadence, player modes/scoring |
| Tools, formerly Q3/UI | Camera/capture/diagnostics and LLM providers over shared native HTTP; UI packets frozen for integration |
| Bots | Frozen bot population/navigation/actions packet; independent Q1/Q2 observations and UI source reviews |
| QC | Qualified QuakeC declarations/input/client lifecycle, imports/protocol/checkpoint integration |
| Q3 presentation/guest | Committed native activation, shutdown/unload lifetime and guest role consumers |
| Map/travel | Qualified guest admission consumers, native carry, travel/campaign integration and independent guest activation review |
| Coordinator | Shared contracts, source review, selected role routing, factories and executable/build integration |

Ledger snapshot: plan revision 7, cursor 835, 41 tasks; 25 reported in progress, nine historically reported complete, seven pending. The historical B00-B07/B09 completion reports are not current accepted whole-task completion; their unresolved criteria remain behind AUDIT. The board also retains an older B14 stalled contribution and a brief-scope attention flag. Claim-overlap checks are not repository correctness judgments. This report does not turn estimates into Jev task acceptance.

Completed bounded source work includes the launch metadata lease core, native Q1/Q2/Q3 carry slices, Q3 native activation admission repair, Q2 pickup observation boundary, the common menu input repair, and Q2 HUD/tokenizer/wheel packets. The exact hashes, inspected contracts and excluded consumers are recorded in the relevant peer reports. These are completed source-review units, not complete game-family or application acceptance.

Evidence: current source/header inventory; `src/main.c`; `CMakeLists.txt`; current application provider constructor dispatch; `docs/source-map.json`; original criteria from Jev revision 7; current worker reports; and bounded source reports under `docs/audit` and `docs/implementation`. Earlier audit snapshots describe historical missing work and are not used as proof that newly added code remains absent.

The largest remaining dependency chains are guest host/app projection into real cross-game composition; connected network sessions into saves/replay; and complete service consumers into menus/HUD and the executable. All required functionality remains in BASELINE. Compilation/functionality evaluation, deep performance work and later enhancement are downstream of that source-completion gate and remain unverified.
