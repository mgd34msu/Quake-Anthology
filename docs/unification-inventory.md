# Shared capability inventory

Each shared capability has one implementation. Native, guest and network adapters translate their original data into that implementation. Game code retains original rules, including movement constants, spawn filters and QuakeC behavior.

The locations below were checked during the MIKE-23–36 work. “Merged” describes Source integration; shipped playtest proof is recorded separately. Open rows remain implementation work, not review gates.

| Capability | Existing copies or divergent paths | Single implementation | State |
|---|---|---|---|
| Pickup model rotation | `src/app/frontend/visuals.c:1047`, `remote_q1_presentation.c:69`, `remote_q2_presentation.c:671`, `remote_unified_render.c:326` | `src/app/frontend/legacy_render_policy.c:22`, `frontend_legacy_entity_angles`; adapters provide original model flags, effects and client time | Merged in 9f119249; native proof recorded below; Q1 rerelease CPU weapon rotation and Mike's retest pending, THE-395 / MIKE-31 |
| Forced view and command feedback | `src/app/application/control.c:2867`, `control.c:2896`, `control.c:3287`; native Q1 one-shot feedback at `native_q1_wire.c:773` | `control.c:744`, `command_angle_feedback`, with the existing motion-change revision and forced-view publisher | Shared publisher merged in 46b3aedb; mixed-world feedback admission remains open, MIKE-23/36 |
| Character standing/crouched posture | `src/app/application/control.c:2008`, `control.c:3607`, `guest_native_q2_services.c:436`; computed boxes overridden twice in each Q2 mover at `src/movement/q2/classic.c:432,440` and `rerelease.c:601,637` | Shared selected-character posture contract in application control; pmove computes its box from the stable template, current bounds remain separate | Shared posture merged in bee02e48; shipped keyboard/audio checks pending, MIKE-30 |
| Typed console command/cvar routing | `src/console/seat.c:238`, `src/app/application/guest_services.c:56`, `src/app/frontend/config_store.c:1456`; ENGINE and per-seat Source registries differ | One application console and canonical cvar table; module registration contributes names to that table, aliases resolve to the same entry, and explicit Source capabilities retain lifecycle identity | Typed input merged in e96ab55b; independent registries and handoff removal remain open, MIKE-32 |
| Q1 temporary-effect sound recipe | `src/app/frontend/particle_events.c:1134`, `remote_q1_effects.c:195`, `remote_unified_q1.c:449,624` | Existing `selected_effects_q1_temporary.c:13`, `frontend_fx_q1_temporary_sound`, then the shared audio engine | Recipe already shared; missing native delivery merged in 1729960d, MIKE-25 |
| Q2 temporary-effect sound recipe | `src/app/frontend/particle_events.c:996,1031`, `remote_q2_effects.c:275,278,287` repeat ricochet, impact and spark selection | Existing selected Q2 effects module; one pure sound recipe, adapters retain real recipient/provider/time | Merge in progress |
| Stock Q1 status bar | Native generic HUD in `src/app/frontend/seats.c`; received generic HUD in `remote_q1_hud.c`; common tiles in `src/ui/hud.c` | One stock-Q1 branch in shared HUD, one mounted WAD media owner and seat-relative layout; native/network supply client state | Shared drawer merged in d7d6c6d4; retail-WAD CPU component passed, shipped pixels pending, MIKE-27 |
| Physical command sampling | `src/input/commands.c:87`, `src/app/frontend/unified_input_command.c:40` repeat mouse/pitch-drift policy | Shared input command policy, with transport projection after sampling; preserve Unified binary64 values and original command widths/rounding | Open; no unverified movement root-cause claim |
| Q1 recipient entity visibility | `src/app/application/visuals.c:176,339`, `src/app/frontend/network_nq.c:879`, `network_qw.c:942`, `src/app/application/network_qw.c:403`, `native_q1_wire_qw.c:258` | Existing application recipient-visibility capability; retain original self/tracked/spectator/nail rules in adapters | Open; collision/PVS algorithms are already shared |
| Held equipment eligibility | Native `src/app/frontend/equipment_native.c:84` and Original QVM equipment hooks accept player-shaped corpse entities as equipment clients | One shared selected-equipment eligibility check against actual client membership, reused by both adapters | Shared roster eligibility merged in 27ee0b9e; long-match native/QVM proof pending, MIKE-35 |
| Presentation feature failure | `src/app/frontend/frame.c:875` promotes presentation failure into run-loop failure; `src/app/application/presentation.c:344` faults the whole application | Shared presentation feature boundary: release entered work, log the failed feature and continue game-frame completion | Source tracing in progress, MIKE-35/36 runtime rule |

Q2 entity visibility already converges on `src/app/application/visual_visibility.c:71`, `application_q2_visibility_test`. Q3 visibility already converges on `src/network/q3/visibility.c:131`. Those adapters do not need a second replacement algorithm.

## Native pickup and rotation qualification

THE-395 / MIKE-31 has shipped native Q1 accepted weapon and ammo traces in classic and rerelease, on CPU and GL. The super shotgun becomes owned and shells increase from 25 to 30. Ammo grants add 20 shells. On the accepted frame, the model clears, solidity becomes `QA_PHYSICS_NOT_SOLID`, frontend appearance becomes invisible with no model, and the item has zero scene draws. These are stock single-player accepted grants. Full-ammo refusal and cooperative weapon retention were not exercised. `QA_PICKUP_SELECT_ORIGINAL` selects the native pickup continuation here, not an Original QuakeC guest. The passing traces justify no new pickup gameplay change.

The existing production fix is `9f119249`. Its shared `frontend_legacy_entity_angles` policy uses the original Q1 MDL rotate flag and Q2 entity rotate effect with their actual Source clocks. Native and received presentation call the same policy. The saved native rotation evidence has the following bounds.

| Q1 edition and renderer | Weapon | Armor | Quad |
|---|---|---|---|
| Classic CPU | Submitted axes at four Source clocks | Submitted axes for one actor at two Source clocks | Submitted axes for one actor at three Source clocks |
| Classic GL | Axes and completed Draw matrices at three Source clocks | Axes and completed Draw matrices at three Source clocks | Axes and completed Draw matrices at three Source clocks |
| Rerelease CPU | No saved weapon rotation trace | Axes and completed Draw matrices at three Source clocks | Submitted axes for one actor at three Source clocks |
| Rerelease GL | Axes and completed Draw matrices at three Source clocks | Axes and completed Draw matrices at three Source clocks | Axes and completed Draw matrices at three Source clocks |

Rerelease armor preserves original MDL flag 8 while drawing the selected MD5 replacement. An ordinary PVS-admitted `dm1` view supplies the three-clock proof. Earlier excluded views did not establish a model-cache defect. Classic CPU armor has two distinct samples, which already show rotation. A third sample is not an implementation requirement. The compact matrix's earlier rerelease CPU weapon claim pointed to four armor samples, not a weapon. An actual rerelease CPU weapon trace remains pending.

Native Q2 classic and rerelease weapon, armor, and Quad rotation has submitted-model proof on CPU and GL. Eight saved `q2dm1` and `q2dm3` map cases record authored `EF_ROTATE`, actual model axes, and three completed client clocks per item representative. Classic uses integer `milliseconds / 10`; rerelease uses `milliseconds * .1f`. The Q2 receipts do not record completed Draw matrices.

Evidence is retained as `the395-native-evidence-matrix.json`, `shipped-967-q1-ammo-proof.json`, `shipped-967-q1-cpu-proof.json`, `shipped-0e7-hud-pickup-proof.json`, `shipped-967-q1-armor-quad-bounds.json`, `shipped-00b-classic-gl-rotation-proof.json`, `shipped-bb7-rr-rotation-proof.json`, and `qualification-967a7349.json`. The latest rerelease armor CPU and GL, weapon GL, and Quad GL cases used installed `bb7f2ec1`; every qualifying run exited through public quit with code 0 and preserved owner settings and the executable. Runs used `--no-audio`. These native receipts do not qualify Original QuakeC or DLL guests, mods, received network presentation, pixel fidelity, audio output, or performance. Mike's retest remains pending.

## Console and cvar ownership

The target has one live console and one cvar table, allocated by the application. Engine, built-in gameplay, QuakeC, QVM, DLL, ELF, cgame and UI register into that table. Only original per-player settings retain seat-specific values. The owner-supplied canonical table defines Q3 names, additional Q1/QW/Q2/rerelease names, and equivalent aliases. Reads, writes and archive output resolve aliases to the same entry.

The following constructor and copy sites were enumerated at 999f41f5. They remain migration work. Source lifetime identity must come from its existing typed provider/role/seat capability rather than reverse lookup by console pointer. Transactional edits remain transactions on the common table.

| Actual Source site | API or operation | Replacement ownership |
|---|---|---|
| `src/app/application/bots_round.c:93` | `qa_cvars_create` | Remove temporary registry; read canonical common records/effective latched values or existing pending transaction at this cold boundary. |
| `src/app/application/character_selection.c:190` | `qa_cvars_create` | Build original per-player userinfo from the existing player/admission owner and common canonical records; remove scratch cvar table. |
| `src/app/application/guest_native_q2.c:328` | `qa_cvars_create` | Replace owned registry allocation with engine-owned common table borrow; remove corresponding destroy/rebind/carry ownership. |
| `src/app/application/guest_native_q2.c:336` | `qa_console_create` | Replace owned console allocation with engine-owned common console borrow; remove corresponding destroy/rebind/carry ownership. |
| `src/app/application/guest_q3_client_console.c:169` | `qa_cvars_create` | Replace owned registry allocation with engine-owned common table borrow; remove corresponding destroy/rebind/carry ownership. |
| `src/app/application/guest_q3_client_console.c:177` | `qa_console_create` | Replace owned console allocation with engine-owned common console borrow; remove corresponding destroy/rebind/carry ownership. |
| `src/app/application/guest_q3_component_services.c:124` | `qa_cvars_create` | Replace owned registry allocation with engine-owned common table borrow; remove corresponding destroy/rebind/carry ownership. |
| `src/app/application/guest_q3_component_services.c:140` | `qa_console_create` | Replace owned console allocation with engine-owned common console borrow; remove corresponding destroy/rebind/carry ownership. |
| `src/app/application/guest_q3_components_scenes.c:122` | `qa_cvars_create` | Replace owned registry allocation with engine-owned common table borrow; remove corresponding destroy/rebind/carry ownership. |
| `src/app/application/guest_q3_components_scenes.c:127` | `qa_console_create` | Replace owned console allocation with engine-owned common console borrow; remove corresponding destroy/rebind/carry ownership. |
| `src/app/application/guest_q3_console.c:282` | `qa_cvars_create` | Replace owned registry allocation with engine-owned common table borrow; remove corresponding destroy/rebind/carry ownership. |
| `src/app/application/guest_q3_console.c:290` | `qa_console_create` | Replace owned console allocation with engine-owned common console borrow; remove corresponding destroy/rebind/carry ownership. |
| `src/app/application/guest_q3_gear_services.c:162` | `qa_cvars_create` | Replace owned registry allocation with engine-owned common table borrow; remove corresponding destroy/rebind/carry ownership. |
| `src/app/application/guest_q3_gear_services.c:168` | `qa_console_create` | Replace owned console allocation with engine-owned common console borrow; remove corresponding destroy/rebind/carry ownership. |
| `src/app/application/guest_q3_save.c:1247` | `qa_cvars_copy_declarations` | Remove Source registry carry/copy; common records and their live values survive Source replacement in the engine owner. |
| `src/app/application/guest_qc.c:293` | `qa_console_create` | Replace owned console allocation with engine-owned common console borrow; remove corresponding destroy/rebind/carry ownership. |
| `src/app/application/guest_qc_factory.c:46` | `qa_cvars_create` | Replace owned registry allocation with engine-owned common table borrow; remove corresponding destroy/rebind/carry ownership. |
| `src/app/application/guest_services.c:101` | `qa_cvars_create` | Retain as sole live application engine cvar table allocation; all other owners borrow it. |
| `src/app/application/guest_services.c:112` | `qa_console_create` | Retain as sole live application engine command console allocation; all other owners borrow it. |
| `src/app/application/native_q1_console.c:771` | `qa_cvars_create` | Replace owned registry allocation with engine-owned common table borrow; remove corresponding destroy/rebind/carry ownership. |
| `src/app/application/native_q1_console.c:778` | `qa_console_create` | Replace owned console allocation with engine-owned common console borrow; remove corresponding destroy/rebind/carry ownership. |
| `src/app/application/native_q1_console.c:847` | `qa_cvars_copy` | Remove Source registry carry/copy; common records and their live values survive Source replacement in the engine owner. |
| `src/app/application/native_q2_console.c:348` | `qa_cvars_create` | Replace owned registry allocation with engine-owned common table borrow; remove corresponding destroy/rebind/carry ownership. |
| `src/app/application/native_q2_console.c:355` | `qa_console_create` | Replace owned console allocation with engine-owned common console borrow; remove corresponding destroy/rebind/carry ownership. |
| `src/app/application/native_q2_console.c:428` | `qa_cvars_copy` | Remove Source registry carry/copy; common records and their live values survive Source replacement in the engine owner. |
| `src/app/application/native_q3_console.c:535` | `qa_cvars_create` | Replace owned registry allocation with engine-owned common table borrow; remove corresponding destroy/rebind/carry ownership. |
| `src/app/application/native_q3_console.c:542` | `qa_console_create` | Replace owned console allocation with engine-owned common console borrow; remove corresponding destroy/rebind/carry ownership. |
| `src/app/application/native_q3_remote_role.c:245` | `qa_cvars_create` | Replace owned registry allocation with engine-owned common table borrow; remove corresponding destroy/rebind/carry ownership. |
| `src/app/application/native_q3_remote_role.c:252` | `qa_console_create` | Replace owned console allocation with engine-owned common console borrow; remove corresponding destroy/rebind/carry ownership. |
| `src/app/application/native_q3_settings.c:617` | `qa_cvars_edit_prepare` | Keep one common pending-edit transaction; remove independent Source table ownership and commit the delta to the canonical table. |
| `src/app/application/q3_campaign_launch.c:124` | `qa_cvars_create` | Keep canonical table across Source Shutdown/Init; remove final_cvars registry copy and declaration carry. |
| `src/app/application/q3_campaign_launch.c:125` | `qa_cvars_copy` | Keep canonical table across Source Shutdown/Init; remove final_cvars registry copy and declaration carry. |
| `src/app/application/q3_campaign_launch.c:159` | `qa_cvars_copy` | Keep canonical table across Source Shutdown/Init; remove final_cvars registry copy and declaration carry. |
| `src/app/application/q3_world_restart.c:391` | `qa_cvars_create` | Remove temporary registry; read canonical common records/effective latched values or existing pending transaction at this cold boundary. |
| `src/app/frontend/client_source.c:337` | `qa_cvars_create` | Replace owned registry allocation with engine-owned common table borrow; remove corresponding destroy/rebind/carry ownership. |
| `src/app/frontend/client_source.c:356` | `qa_console_create` | Replace owned console allocation with engine-owned common console borrow; remove corresponding destroy/rebind/carry ownership. |
| `src/app/frontend/config_store.c:199` | `qa_cvars_create` | Remove settings/mouse/movement/parked/image registry or console allocation; common engine table/console owns declarations and queue, existing player/input owner retains only original per-player values. |
| `src/app/frontend/config_store.c:560` | `qa_console_create` | Remove settings/mouse/movement/parked/image registry or console allocation; common engine table/console owns declarations and queue, existing player/input owner retains only original per-player values. |
| `src/app/frontend/config_store.c:2077` | `qa_cvars_copy` | Remove Source registry carry/copy; common records and their live values survive Source replacement in the engine owner. |
| `src/app/frontend/config_store.c:2520` | `qa_cvars_copy` | Remove Source registry carry/copy; common records and their live values survive Source replacement in the engine owner. |
| `src/app/frontend/config_store.c:2555` | `qa_cvars_create` | Remove settings/mouse/movement/parked/image registry or console allocation; common engine table/console owns declarations and queue, existing player/input owner retains only original per-player values. |
| `src/app/frontend/config_store.c:2559` | `qa_console_create` | Remove settings/mouse/movement/parked/image registry or console allocation; common engine table/console owns declarations and queue, existing player/input owner retains only original per-player values. |
| `src/app/frontend/neutral_config.c:422` | `qa_cvars_create` | Remove settings/mouse/movement/parked/image registry or console allocation; common engine table/console owns declarations and queue, existing player/input owner retains only original per-player values. |
| `src/app/frontend/neutral_config.c:424` | `qa_cvars_create` | Remove settings/mouse/movement/parked/image registry or console allocation; common engine table/console owns declarations and queue, existing player/input owner retains only original per-player values. |
| `src/app/frontend/neutral_config.c:897` | `qa_cvars_create` | Remove settings/mouse/movement/parked/image registry or console allocation; common engine table/console owns declarations and queue, existing player/input owner retains only original per-player values. |
| `src/app/frontend/neutral_config.c:1209` | `qa_cvars_create` | Remove settings/mouse/movement/parked/image registry or console allocation; common engine table/console owns declarations and queue, existing player/input owner retains only original per-player values. |
| `src/app/frontend/remote_config.c:121` | `qa_cvars_create` | Remove settings/mouse/movement/parked/image registry or console allocation; common engine table/console owns declarations and queue, existing player/input owner retains only original per-player values. |
| `src/app/frontend/remote_config.c:600` | `qa_cvars_copy` | Remove Source registry carry/copy; common records and their live values survive Source replacement in the engine owner. |
| `src/app/frontend/remote_q2_source.c:429` | `qa_cvars_create` | Replace owned registry allocation with engine-owned common table borrow; remove corresponding destroy/rebind/carry ownership. |
| `src/app/frontend/remote_q2_source.c:445` | `qa_console_create` | Replace owned console allocation with engine-owned common console borrow; remove corresponding destroy/rebind/carry ownership. |
| `src/app/frontend/remote_unified_components_scene.c:202` | `qa_cvars_create` | Replace owned registry allocation with engine-owned common table borrow; remove corresponding destroy/rebind/carry ownership. |
| `src/app/frontend/remote_unified_components_scene.c:206` | `qa_console_create` | Replace owned console allocation with engine-owned common console borrow; remove corresponding destroy/rebind/carry ownership. |
| `src/app/frontend/shared_values.c:89` | `qa_cvars_edit_prepare` | Keep one common pending-edit transaction; remove independent Source table ownership and commit the delta to the canonical table. |
| `src/app/frontend/shared_values.c:112` | `qa_cvars_edit_prepare` | Keep one common pending-edit transaction; remove independent Source table ownership and commit the delta to the canonical table. |
| `src/app/frontend/shared_values.c:178` | `qa_cvars_edit_prepare` | Keep one common pending-edit transaction; remove independent Source table ownership and commit the delta to the canonical table. |
| `src/app/frontend/startup_arena.c:138` | `qa_cvars_create` | Remove temporary registry; read canonical common records/effective latched values or existing pending transaction at this cold boundary. |
| `src/console/commands.c:467` | `qa_console_create` | Common command console constructor; only application engine lifetime allocates the live console. |
| `src/console/cvars.c:682` | `qa_cvars_create` | Common table constructor; only application engine lifetime allocates the live store. |
| `src/console/cvars.c:1465` | `qa_cvars_edit_prepare` | Existing common transaction boundary; stop treating a prepared full-table copy as an independent Source registry. |
| `src/console/cvars.c:1742` | `qa_cvars_edit_prepare` | Existing common transaction boundary; stop treating a prepared full-table copy as an independent Source registry. |
| `src/console/cvars.c:1798` | `qa_cvars_copy` | Delete full-registry copy API after all owning/carry callers migrate. |
| `src/console/cvars.c:1801` | `qa_cvars_copy_declarations` | Delete per-Source declaration-copy carry after common declarations own registration lifetime. |
| `src/console/cvars_save.c:108` | `qa_cvars_edit_prepare` | Existing common transaction boundary; stop treating a prepared full-table copy as an independent Source registry. |

Factory invocations that also allocate independent heaps (in addition to the factory definitions above):

- `src/app/frontend/config_store.c:943`: `qa_cvars *owned=restored?registry(source,source->command.dialect,error):seat->cvars;`
- `src/app/frontend/config_store.c:2076`: `*out=registry(source,qa_cvars_dialect(previous),error);`
- `src/app/frontend/config_store.c:2700`: `seat->cvars=registry(source,command->dialect,error); seat->mouse=registry(source,command->dialect,error);`
- `src/app/frontend/config_store.c:2872`: `if (ok) source->movement=registry(source,source->movement_dialect,error);`
- `src/app/frontend/config_store.c:2873`: `if (ok) source->fallback=source->movement_dialect==command->dialect?source->movement:registry(source,command->dialect,error);`
- `src/app/frontend/remote_config.c:117`: `static qa_cvars *settings_registry(frontend_remote_config *row,qa_console_dialect dialect,qa_error *error)`
- `src/app/frontend/remote_config.c:687`: `row->q3_mouse=settings_registry(row,QA_CONSOLE_Q3,error);`
- `src/app/frontend/remote_config.c:688`: `row->q3_view=settings_registry(row,QA_CONSOLE_Q3,error);`
- `src/app/frontend/remote_config.c:689`: `row->movement_mouse=row->movement==QA_MOVEMENT_Q3?row->q3_view:settings_registry(row,(qa_console_dialect)row->movement,error);`
- `src/app/frontend/remote_config.c:1167`: `*cvars=settings_registry(row,dialect,io->error); qa_cvars_restore *ticket=NULL;`

There is no qa_cvars_clone or qa_console_clone API. Full registry copies occur through qa_cvars_copy/qa_cvars_copy_declarations above; qa_cvars_edit_prepare copies the current table for a pending edit at src/console/cvars.c:1479 and :1486. Console option/context string copying inside its sole constructor is not another console factory.

The additional typed Source command handoff to delete is src/app/frontend/config_store.c:1172, its app invocation at src/app/application/commands.c:349-356, hook declaration include/qa/application_startup_prepare.h:145, and hook initializer config_store.c:3597.

## Mods and the virtual filesystem

Every executable in a loaded mod folder is inventoried and admitted by its actual format and ABI. QuakeC, QVM, PE DLL and ELF SO modules run together against the existing shared session, actors, world, operations, presentation and cvars. PE and ELF loaders remain available on every host OS. Supported archives share one VFS search order across installed content and loaded mods.

Multiple mod instances and cross-runtime operation hooks already exist. The remaining restrictions are in discovery, artifact selection and borrowed content views.

| Existing restriction or duplicate choice | Current location | Shared target |
|---|---|---|
| Family-specific folder roots and assigned product families | `src/content/catalog/discovery.c:577`, `discovery.c:361` | Discover loaded folders independently of the selected world and inventory their modules/content. |
| Loose native discovery recognizes only names beginning with game | `discovery.c:311` | Admit qualified game, qagame, cgame and UI artifacts by format/ABI, including DLL and SO. |
| Family chooses one executable and returns at the first match | `discovery.c:780`, `discovery.c:795` | Retain every qualified executable as a module instance; no first-match format selection. |
| Supplemental declarations require gameplay-mods.json | `src/content/catalog/metadata.c:74`, `metadata.c:44` | Automatic artifact inventory plus optional authored role/conflict metadata. |
| Product family selects Q2 or Q3 native host | `src/app/application/providers.c:1438`, `providers.c:979` | Select the adapter from qualified module ABI/role, independently of world or folder family. |
| Q2/Q3 guest constructors reject other product families | `guest_native_q2.c:294`, `guest_q3.c:172`, `guest_q3_factory.c:668` | Existing common module host admits actual ABI and lifecycle ownership. |
| Supplemental retained GAME/scene roster accepts QVM only | `guest_q3_components.c:138`, `guest_q3_components_game.c:88` | One admitted module roster covering all formats and roles. Preserve existing QC/native operation registrations. |
| Primary role and UI/cgame recipes choose one native-or-QVM artifact | `guest_q3.c:110`, `guest_q3_source_ui.c:84`, `native_q3_client_modules.c:248` | Inventory all artifacts and compose their contributions through explicit role/replacement rules. |
| Archive discovery uses family-specific filters | `discovery.c:252` | One archive admission path for PAK, PK3, PK4, KPF, ZIP and PKZ, including arbitrary supported filenames. |
| Provider filesystems retain product-specific lookup views | `src/session/configuration/transaction.c:491`, `src/content/catalog/mounts.c:168` | Every local module borrows the same aggregate session VFS and explicit search order. Network pure rules stay at the protocol boundary. |
| Remote content lookup is family-restricted | `src/content/catalog/mounts.c:217` | Keep protocol-specific matching in its adapter; local mod selection uses the common inventory. |
| Hook order defaults to admission chronology | `src/session/configuration/operation.c:131`, `guest_q3_mod_callbacks.c:66` | One explicit mod load order, preserving authored dependencies/conflicts and single-replacement conflict checks. |

The existing common operation host already receives QC hooks at `guest_qc_declared.c:236`, native Q2 hooks at `native_q2_callbacks.c:1912`, and QVM hooks at `guest_q3_mod_callbacks.c:50`. Publication initializes all attached QC additions at `publication.c:1088`. These paths share one world and do not need a second operation system.

Native format inspection at `src/compat/native/image.c:258` already distinguishes PE and ELF by file signatures. Those ABI parsers remain format adapters. The pending work is to admit their modules concurrently and remove product-family selection. Simultaneous all-artifact execution still needs implementation and live proof.
