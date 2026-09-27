# Presentation and application source audit

Status: assigned source coverage complete; Jev claim checks pending. This report does not accept B15–B21 or B34 as complete. P-01 remains missing baseline work. P-02 is authorized for a focused fix and independent source review.

Snapshot: `c6f7db5c4715416393d63af7e5c994ba59194caf` plus the preserved working tree at the start of the audit. The subsequent `849a3b2159c5888ec965708894386f82150e2fda` header cleanup was inspected; it removes comments from `src/render/scene/sort.c`, `src/audio/codecs.c`, and `src/movement/q2/classic.c`, without changing executable code. Plan revision 6 was read and acknowledged. The local `docs/dependencies.json` still said revision 5 when first inspected; its B15–B21/B34 criteria match the unchanged ledger task criteria. Review lane: `/root/native_q2`, Jev contribution `w_b6e29eff0b574cfe99195d9f6bbe98e1`.

Only source reading, text search, source-control inspection, report editing, and the explicitly requested Jev checks are allowed. No project configuration, compilation, tests, executable runs, generators, sanitizers, benchmarks, or gameplay qualification occurred.

## Required outcomes

| Task | Existing goal | Existing acceptance criterion |
|---|---|---|
| B15 | Installed products, editions, launch recipes, independent selections, preset defaults, and mod catalog. | Configuration resolves the full feature union with atomic changes and explicit source identities. |
| B16 | Shared scene, visibility, materials, models, lights, effects, and frame/resource ownership. | Required family-specific presentation behavior uses one scene/resource system with CPU/GL backend contracts. |
| B17 | Complete software rasterizer and source rendering semantics. | Clipping, textures, palettes, interpolation, depth, stencil, fog, and blending are implemented and source-reviewed; execution checks follow BASELINE. |
| B18 | Native GL rendering, retained buffers, SDL window/context lifecycle, capture, and backend restart. | Required render features are wired to real native APIs with correct resource lifetime; no frame-time tuning gate. |
| B19 | Decoding, music, source voice policy, mixing, per-seat audio, cinematics, synchronization, and devices. | All required audio/media formats and policies have shared C implementations and source-reviewed lifecycle handling. |
| B20 | Keyboard/mouse/controllers, gyro, haptics, text input, device lifetime, and four local seats. | Per-seat routing and state remain independent; focus loss, unplug, and reassignment release owned input correctly. |
| B21 | Commands, cvars, profiles, bindings, settings, text/font resources, localization, and accessibility. | Source command semantics and persistent configuration are exposed through shared services. |
| B34 | Complete baseline executable, dedicated mode, startup/shutdown, delivery layout, and asset discovery. | All required subsystem implementations have actual source callers and build definitions; no placeholders or disconnected feature implementations. Build execution is deferred to P01. |

The donor requirements are `../quake-typescript/docs/functional-targets/README.md`, particularly T01–T06, T17–T18, T21–T22, and `docs/project-plan.md` sections on local seats, presentation, and complete player workflows. Donor declarations of accepted source are context, not evidence that this C rewrite implements those outcomes.

## Confirmed gaps and defects

### P-01: B34 has no native application composition yet

`src/main.c:102` dispatches only help/version, archive listing, and BSP inspection. It does not create a session, local seat, console, renderer, display, audio engine/device, cinematic owner, or dedicated server. `CMakeLists.txt` links the `quake-anthology` executable only to `qa_content`. Standalone library implementations therefore do not establish actual player workflows or B34's source-caller criterion. This is missing baseline implementation, not a runtime failure discovered by this audit.

The configuration transaction implementation also has no build-source registration or actual application caller: searches for `qa_configuration_create`, `qa_configuration_prepare`, and `qa_launch_draft_create` under `src` resolve only to their definitions. B15's atomic configuration machinery exists, but its player workflow remains an integration obligation shared with B34.

### P-02: B18 retained geometry survives every normal map retirement

`src/render/gl/resources.c:283` (`mesh_storage`) allocates a VBO/IBO pair for each retained mesh identity/revision and appends it to the renderer cache. The successful-cache deletion walk is `gl_resources_destroy` at line 446, used by renderer teardown/restart. Allocation-failure cleanup does not retire successful entries. There is no retained-mesh lease or retirement API. `src/render/scene/world.c:60` allocates new identities for new world surfaces, so releasing a world and loading another while retaining the display/renderer leaves the previous world's GPU geometry resident. Repeated map changes accumulate VBO/IBO storage. Texture entries have an independent reference-count pruning path; that path does not cover geometry. This violates B18's resource-lifetime criterion. Retained geometry needs explicit ownership and retirement, preserving identity/revision caching and identity-zero streaming.

### Refuted suspicions

- The eight-light GL draw limit matches the donor's `src/render/gl/programs.ts:276`; it is not a newly introduced feature cut.
- CPU render-target entries are pruned in `qa_cpu_execute` when only the cache reference remains; the GL geometry finding does not apply to that cache.
- Source joystick restart deliberately differs from focus loss in donor `src/input/source-input.ts:97`; restart alone retaining Linux source key state is not reported as a newly introduced regression.
- Ogg stream, total-byte, and packet limits match donor `src/media/ogg.ts`; they are not introduced format limits.
- Q2 CIN palette prefetch and console wrap-at-width behavior match `src/media/cin-playback.ts` and `src/console/buffer.ts` respectively.
- `alias_shadedots.inc` contains 4,082 explicit floats for a 4,096-element table. The omitted final fourteen values are donor padding, but current `scene_model_shade` only indexes it with normal indices below 162. The 255 fallback is computed separately. This is a data discrepancy without a demonstrated reachable rendering defect.

## Criterion evidence

These are source evidence and limits, not execution results or claims of complete product parity.

| Task | Source evidence | Remaining acceptance limit |
|---|---|---|
| B15 | `configuration/transaction.c` implements create at 294, prepare at 326, validation at 364, commit at 382, and abort at 402. Drafts and presets retain independent provider identities; prepared changes validate stale generations before commit. | P-01: configuration sources are absent from build definitions and have no application consumer. Installed-content and complete launch workflows remain unaccepted. |
| B16 | `scene/resources.c` owns image versions and references; `scene/frame.c` retains submitted images and arena geometry. `scene/world.c` implements world creation and submission; legacy world code handles lightstyles, palettes, BSPX/.lit inputs, and Q64 behavior. Q3 patch code handles subdivision/stitching and LOD. Model code implements topology, pose interpolation, skins, lighting, sprites, and shadows. Material code parses source profiles and supplies stages, coordinates, waves, deforms, remaps, and movie inputs to both renderers. | All assigned implementation files were read, but the audit does not establish every required family effect through actual application callers. Mesh lifetime is shared with P-02. |
| B17 | `cpu/renderer.c` implements execute, resize, capture, and depth reads. `raster.c`, `fragment.c`, `texture.c`, and `fog.c` implement clipping, interpolation, coverage, texture sampling, palette-derived image inputs, depth, stencil, blending, and source fog policy. | No runtime image comparison or whole-product parity result exists. Such execution remains deferred until BASELINE under the current graph. |
| B18 | `gl/api.c` resolves native GL entrypoints; `program.c`, `passes.c`, and `renderer.c` implement shader/pass submission, execute, capture, depth reads, and restart. `platform/display.c` owns SDL window/context transitions. | P-02 violates retained geometry lifetime. P-01 leaves native display/render services disconnected from the executable. |
| B19 | `audio/engine.c` supplies listener and voice admission; bank/codec files own decoded PCM, Vorbis, ADPCM, mu-law, and wavelet inputs. Mixer, music, streams, reverb, device, and output code retain audience/seat and source-channel policy. `media` implements Q2 CIN, RoQ, Ogg/Theora/Vorbis, cinematic clocks, audio feeding, scene images, and material movies. | No playback, device, channel-policy, or synchronization execution occurred. Application lifetime/callers remain part of P-01; decoder presence alone does not establish complete workflow parity. |
| B20 | `input/seat.c` implements device release at 345, seat release at 363, and focus loss at 382. `platform/input.c` routes SDL events, restart, and frame polling. Bindings, source device policy, controller tuning, gyro, haptics, commands, and four stable local-seat slots share the input service. | No device runtime qualification. Source implementation is present; actual join/leave and gameplay consumption are pending application composition. |
| B21 | `console/commands.c` executes and drains commands and removes owned registrations. `cvars.c` implements sets, latched apply, restart, and config serialization. Settings files provide persistence and ordered restart handling. Text/font files implement localization, interval captions, sidecar/voice captions, shared glyph/image resources, KFONT, FreeType, Q3/UI/console/world layout, and seat-specific consumers. | No console/profile/accessibility workflow is connected in `main.c`. Existing tests do not exercise these services. |
| B34 | Full `src/main.c`, `CMakeLists.txt`, `cmake/Native.cmake`, and configuration sources were inspected. The executable currently dispatches content inspection utilities only. | P-01 keeps B34 open. No claim that complete baseline application integration exists. |

The existing tests cover core utilities and archive/BSP/image/model/VFS resources. All six test sources were read. CMake registers five of them; `tests/vfs_test.c` is not currently registered. There are no renderer/audio/input/application qualification tests in this manifest. No existing test was executed, and the audit does not infer a pass from reading test source.

Source-only improvement opportunities were also noted: skin flood filling is duplicated in scene resources and model image loading, and shadow signature construction allocates scratch storage per frame. Neither observation includes a measured performance claim. They do not replace the required integration work.

## File coverage

Every file listed below was read in full, including header contracts and existing test sources. Brace notation enumerates the named files; it does not imply review of unnamed neighboring paths. Truncated reads were revisited. There are no unreviewed files in this assigned manifest. Donor comparison was targeted to contracts and disputed behavior, not an assertion that every TypeScript source file was audited. Coverage does not imply correctness or runtime qualification.

Reviewed files:

- Application/configuration: `src/main.c`, `CMakeLists.txt`, `cmake/Native.cmake`, `include/qa/launch.h`, and all five files in `src/session/configuration` (`draft.c`, `validate.c`, `presets.c`, `transaction.c`, `internal.h`).
- CPU renderer: `include/qa/render_cpu.h` and all six files in `src/render/cpu` (`internal.h`, `texture.c`, `renderer.c`, `raster.c`, `fragment.c`, `fog.c`).
- GL renderer: `include/qa/render_gl.h` and all six files in `src/render/gl` (`internal.h`, `api.c`, `resources.c`, `renderer.c`, `program.c`, `passes.c`).
- Audio: `src/audio/{bank,codecs,device,engine,environments,mixer,music,output,reverb,streams,wavelet}.c`, `src/audio/{codec_internal,mixer_internal,reverb_presets}.h`, and `include/qa/audio.h`. Reads split around truncated output were revisited before counting coverage.
- Input: `src/input/{seat,bindings,buttons,mouse,source_devices,keys,haptics,gamepad,commands,client_commands,settings}.c`, `src/input/internal.h`, `src/platform/input.c`, and `include/qa/{input,input_platform}.h`.
- Settings: `include/qa/settings.h`, `src/settings/{internal.h,codec.c,store.c,restart.c}`.
- Console: `src/console/{buffer,text,cvars,documentation,dedicated,source_field,commands,field,seat,discovery}.c`, `src/console/internal.h`, `src/platform/console.c`, and `include/qa/{console,console_io,console_seat,console_discovery,console_buffer,console_draw,field}.h`.
- Media: `src/media/{source,ogg,ogv,cin,cin_playback,roq_stream,roq_decoder,roq_playback,roq_audio,roq_codebook,cinematic,cinematic_scene,library,material_movies}.c`, `src/media/{ogv_internal,roq_internal,cinematic_internal}.h`, and `include/qa/{ogv,media,cinematic}.h`.
- Materials: `src/render/material/{color,coordinates,deform,math,parser,library,submit}.c`, `src/render/material/{internal,library_internal}.h`, and `include/qa/material.h`.
- Scene: `src/render/scene/{resources,frame,sort,math,models,effects,world}.c`; `src/render/scene/models/{topology,images,draw,lighting,shadows,sprites}.c`, `internal.h`, and `alias_shadedots.inc`; `src/render/scene/effects/{particles,primitives,stencil,shadows,sky}.c` and `internal.h`; `src/render/scene/world/{legacy,q3}.c` and `internal.h`; `src/render/scene/world/legacy/{geometry,lighting,textures}.c` and `internal.h`; `src/render/scene/world/q3/{patch.c,patch.h}`; and `include/qa/{scene,scene_effects}.h`.
- Text/fonts: `src/text/{localization,captions,media_captions}.c`, `src/text/font/{library,layout,kfont,freetype,console,ui,q3,world}.c`, `src/text/font/internal.h`, and `include/qa/{font,localization,captions,media_captions}.h`.
- Display: `src/platform/display.c` and `include/qa/display.h`.
- Existing tests: `tests/{core,archive,bsp,image,model,vfs}_test.c`.

Selected donor checks additionally read `src/audio/types.ts`, `src/text/{kfont,captions}.ts`, relevant GL shader limits, source input restart, console wrapping, Ogg limits, CIN palette/prefetch, model shadedot data, and bootstrap configuration/input/audio/console callsites. The user prohibited runtime validation before the complete baseline; this report preserves that boundary.

## Jev judgments

Pending. Ledger claim/acknowledgement and progress records are not judgments. Each task evidence submission and its actual result will be recorded here. No unavailable judgment will be counted as a pass.
