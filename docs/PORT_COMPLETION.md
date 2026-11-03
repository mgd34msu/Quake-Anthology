# Completing the port

`docs/manifest.tsv` says every file is "ported". That is not true. It records
that a C file exists. Measured on 2026-11-02, most game code could not execute
in the binary and many files were empty shells. Use these two tools instead:

- `tools/reachability.py [--list] [--module PREFIX]`: objects reachable from `main`.
- `tools/depth_audit.py [--write]`: per-file token comparison with the TypeScript;
  writes the backlog to `docs/port_gaps.tsv`.
- `tools/admission_audit.sh [path...]`: comments admitting behavior was left out.
  A file can pass the token check with branches falling into a no-op; these find it.
- `tools/symbol_audit.py [--write] [--module PREFIX]`: TS functions, classes, and
  methods with no C name; writes `docs/port_missing_symbols.tsv`.
- `tools/eval_order_audit.py [--list] [path...]`: expressions whose operands'
  effects can run in a different order than TypeScript's left-to-right.

## Paused 2026-11-03 (~14:00)

Four agents were stopped mid-task. Their edits may be partial, so build and run sim_verify
and test_remote_loopback before relying on the tree. To resume, restart these jobs:

- D1d (build/agents/d1d-sim): first fix the test_native_mod_host regression (12/70 fail;
  binary from 00:41 passed). Then the 22 save/restore SourceStrategy members
  (docs/runtime_layout.md), the as_*/ss_source_is sweep, the TS file extractions
  (network-q2-guest-core, qvm-component-state, foreign-* rules/host, monster-admission,
  diagnostics), freeing ports in qa_mod_commands_destroy, and the Q2 admission carry leak
  (content/q2/base/player/types.c:49, 17.6 KB, the largest leak left in the loopback run).
  It was about to run sim_verify and loopback when stopped.
- startup3 (build/agents/startup3): qa_startup_open_graphics / qa_startup_graphics_draw /
  graphics.captionCommands, main.c using them, offscreen graphical test, link
  test_startup_port. It was waiting on an ASan rerun when stopped.
- gate-q1b (build/agents/gate-q1b): full read of content/q1 quakec, equipment, addons,
  missionpacks; remove C caps TS lacks (entity string 1023, backpack 8/16, ~1024 spawn
  and monster lists).
- gate-render3 (build/agents/gate-render3): rest of render read (world, shaders, models
  prepare/renderer, q3 particles, cpu, gl, worker), JPEG error type, sprite orientation,
  fog viewport, then model shadows. It was mid-fix: the legacy shadow mesh must own its
  indices (the prepared surface frees generated indices before the caster uses them).

Not started: full reads of content/q2, content/q3, catalog/composition/mounts, compat,
guest, bots, network, ui, app, audio, materials, formats, rest of core/input/media/platform/
text/persistence/contracts; unported TS tests; full-tree ASan/UBSan; audits; final drift
re-check; then connect main, then optimize.

## What "ported" means

A TypeScript file is ported when every exported symbol and every private
function and branch exists in C with the same behavior, and the matching tests
under `../quake-anthology-ts/tests` are reproduced as C checks and pass.

The TypeScript code is the only source of truth. Read it; do not infer behavior
from an existing C header, comment, or report.

Not acceptable, in any form:

- A struct that stores its options with no logic (the placeholder pattern).
- Delegating a body to a host callback "until X lands" when TypeScript has the
  logic in that file. Host callbacks are correct only where TypeScript takes a
  host parameter too.
- `(void)param` to drop an argument TypeScript uses; returning a fixed value
  where TypeScript computes one; aborting with "not constructed".
- Comments saying something is unresolved, deferred, or left for later, in place
  of doing it. If a dependency truly is missing, say so in your final report.
- Weakening or deleting a test to make it pass. A failing oracle test means the
  port is wrong until shown otherwise; state the evidence if the oracle is wrong.

A C file can legitimately be small: a table the C port generates differently, or
types that belong in the header. Prove that by reading the TypeScript, and name
the file and reason in your report.

## One engine

This is one shared, unified engine. `SharedSimulation` composes content by
recipe: map entities, movement, weapons, and characters from different games
coexist in one world. Never fork per-game runtime paths where TypeScript has one
path with per-source dispatch, and never add a per-game `main` branch.

## Conventions

`docs/PORTING.md` has the TS-to-C conventions (binary32 staging, error handling,
closures as fn+ctx, ownership). Match the surrounding code.

Every callback taking `void *ctx` must receive the context its author expects.
When you overwrite a host's `ctx`, forward the caller's leaf functions with the
caller's original context. Test this with a sentinel `ctx`: a test whose leaves
ignore `ctx` proves nothing.

## Working alongside other porters

The tree may be edited concurrently, and the base state is uncommitted.

- Do not run any git command that changes the repository or its index: no `git add`,
  `git rm` (including `--cached`), `git commit`, `git stash`, `git checkout`,
  `git restore`, or `git reset`. Delete files with `rm`. Also no `make clean`.
- Build only with `tools/agent_build.sh NAME <targets>`, using your own NAME.
  Binaries land in `build/agents/NAME/`. Example:
  `tools/agent_build.sh bots-lib build/agents/bots-lib/test-bots/test_library`.
- Edit only files in your assignment, plus their tests. Other headers may gain
  additive declarations; do not change or remove what exists. If you must change
  an existing signature, update every caller in the same edit and report it.
- When you must link through a private archive, build it with `ar qc` then `ranlib`,
  not `ar rcs`: `rcs` replaces members that share a basename (several `index.o`,
  `services.o`, `content.o`) and leaves undefined references.
- Do not start helper agents or forks of your own. Several porters each starting
  four or five helpers, with builds and bun runs in each, exhausted memory and crashed
  the machine. Do the work yourself, one build at a time at -j1 (the default).
- A compile error in a file you do not own usually means another porter is mid-
  edit. Retry shortly. Do not fix it.
- New tests go in new files, `tests/<module>/test_<name>.c`; the Makefile finds
  them. Do not rely on `make test` to find your failure: it stops at the first
  failing binary. Run your binaries directly.

## Order of work

The user's instruction: finish porting everything, confirm it is all there, and
only then connect it. Do not wire anything into `main` before the gate below.

1. Port leaf subsystems: bots library, Q3 game logic and presentation, Q2 native
   mods, renderer and network remnants. Memory safety: every test binary clean
   under ASan and UBSan.
2. Port the Q3 bot AI (`bots/behavior/q3`, director) over the finished library,
   then the `app/bootstrap/simulation/*` shells: monsters, guests, grapple, native
   mods, network adapters, bot services. The unified event codec.
3. Port the top layer as code, tested at unit level: `simulation/quakec-source.ts`,
   `simulation/runtime.ts`, `application.ts`, `startup.ts`, `startup-selection.ts`,
   `input.ts`, `effects.ts`, `presentation*.ts`, `ui.ts`, `audio.ts`,
   `remote-application.ts`. `SharedSimulation` stays one type. `createSource` stays
   one dispatch over its six source kinds (native Q2, Q3 guest, QuakeC, TS Q3, TS Q1,
   TS Q2), and `step` stays one pipeline. Split the C across `runtime_*.c` by TS
   region; do not split by game.

**Confirmation gate.** Before step 4, all of these hold and are reported.

Full read: every TS module is compared against its C function by function, every
branch, not only rows the audits flag. Reading 112 rows the name audit counted as
present found 15 behavior bugs, so name and token checks cannot confirm a port. Track
it per module in `docs/port_review.tsv` (module, files, functions read, bugs fixed,
reviewer), and run one review pass per module after its porting is finished.

Also:
`tools/depth_audit.py --ratio 0.75` and `tools/symbol_audit.py` show no unexplained
gap, every exception named with a reason; `tools/admission_audit.sh src include`
has no hit that describes missing behavior; `tools/eval_order_audit.py` reports no
unclassified site; `tools/js_semantics_audit.py` reports no unclassified site;
every TS test file maps to C checks; and all test binaries pass,
also under ASan and UBSan.

4. Connect: `main` and the application reach the real `SharedSimulation`. Delete
   the "concrete host" layer. Run real data side by side with TS and fix what that
   finds, then optimize.

## Done means

- `tools/depth_audit.py` lists no unjustified thin file, including above the 0.5
  threshold on a second pass with `--ratio 0.75`.
- `tools/reachability.py` shows every object reachable, or a named reason it is not.
- Every test binary passes, also under ASan and UBSan; C checks cover the TS tests.
- Side-by-side real-data runs match the TS oracle: console output, entity counts,
  and deterministic state hashes after N frames, for each game and for mixed
  recipes (`--preset q2-q1-q3`), dedicated and graphical.
- Only then optimize, with a profile for every change and these checks as the guard.

## Open items found by porters

- D6 (compat + guest TS drift, 2026-11-03) leftovers. Per-file notes are in docs/port_review.tsv (`d6-compat`;
  `drift-partial` rows name what is missing). (1) Perf-only TS changes not mirrored, each claimed
  behaviour-preserving by TS (its differential tests): guest/x86/fast.ts (I386Fast predecoded path) and the
  i386 decode cache, guest/floating-point/fast-arithmetic.ts with the SSE `fastBinary` and x87
  `fastBinaryInstruction` paths, PERF1 page-indexed write observers in guest/core/memory.ts (C full scan delivers
  the same ranges and counts `writeDeliveries`), QvmMemoryWrites watched pages, QVM lazy resolver words, QVM
  mod-provider observe() reuse/ProjectionSlots index, QVM mod player-event skip, Q2 native-mod per-slot field
  watches, native-mod-provider readOnlyImports/projection index, rerelease host direct reconcile skip, classic
  records cache. (2) Error aggregation on close (`AggregateError`) in qvm/mod-provider, qc/mod-provider and
  q2/native-mod-provider is not represented: the C close functions run every step but cannot collect raised errors.
  (3) Async guest calls (GuestCallRunner.invokeAsync, promise host callbacks, Quake3VmModule.callAsync) have no C
  counterpart; the C runner is synchronous. (4) Not wired by D6 (D1 territory): native Q3/Linux qagame hosting in
  simulation/q3/native-guest-source.ts (it needs qa_native_game_memory, qa_native_q3_game_call, the 16-word
  qa_compat_q3_native_syscalls handler, qa_sparse_allocator_image/restore_allocator and the Windows/System V
  heap_state/restore_heap_state added here), and the NativeGuestHeapImage bytes (persistence/execution, D8) — the
  heap image bytes were therefore not compared against bun. `qa_qvm_semantics_from_cvar` exists but the
  q3-client/qvm.ts and guest-runtime.ts callers (D1/D2) do not pass it yet. Classic `standInActor` is a host
  option nobody sets yet (classic-guest-services.ts, D1). (5) TS tests of the drift not reproduced in C: the
  native Q3 synthetic/host tests, runner-inline chargeBytes/recover tests, x64/x86 differential and fast-path
  tests, guest elf mutation, system-v module-check, the windows installed/msvc/services changes, Q2 classic/native
  primary 1998 profile tests (need the 1998 Xatrix DLL fixture), and most compat/qc, compat/qvm and
  compat/q2/rerelease test edits. New checks: tests/guest/test_guest_drift.c, tests/compat/test_compat_drift.c
  (oracle tests/compat/oracles/d6_drift_oracle.ts). (6) tests/compat/test_qvm_b.c line ~1343 expects
  `capture_part(ctx, "graph") == NULL` from the Q3 runtime table, but the uncommitted
  src/app/bootstrap/simulation/q3/runtime.c (04:21, not D6) raises "Unknown Q3 native save part graph", so the
  binary aborts there; app/bootstrap owner to reconcile the expectation.

Each is assigned or waits for the batch named. Remove an item only once it is done
and tested.

- D8 (misc TS drift, 2026-11-03): done files are the `drift-done (d8-misc)`, `drift-done (d8b)` and
  `drift-done (d8c)` rows of docs/port_review.tsv. D8b finished audio, camera, capture, console, content/archive,
  content/catalog, content/composition, content/mounts, content/user-data, core/{common-parse,game-numeric},
  formats, foundation and media (including the removed foundation/{arena,handles,pool,sys-queue,vec},
  media/{roq-presentation,transitions,index}, core/diagnostics, console/{log,index}, formats/q3-model/{animation,index}
  and the types-only rows of those modules). D8c finished input, llm, materials, persistence, platform, settings, text,
  ui/{common,hud,library,mods,saves,settings} (rows `drift-done (d8c)`), including the added platform/signal-hint,
  settings/setting-spec, ui/common/controls and text/localization-key, the removed persistence/q3, platform/runtime and
  the input/materials/platform/settings/text index barrels, the settings-menu leak (setting controls now use
  caller-owned qa_ui_setting_control_storage; test_ui_controller_fixes is LSan-clean), music onFault in
  app/bootstrap/audio/music.c, and bun's JSON.parse wording (value.c's parser follows JavaScriptCore's LiteralParser).
  D8c tests not ported (harness or fixture changes, corpus-gated, or needing a full Application/SharedSimulation):
  tests/input/input.test.ts (haptics fixture rewrite), llm/settings.test.ts "saving an API key after sign-in keeps the
  subscription", llm/request.test.ts, materials/{animated-images,image-resolution,images,sky-clouds}, persistence/
  {native-weapon,provider-ownership,q1-source,q3-native-graph,q3-native-server,qvm-*,reference-save-fixtures,
  source-saves,save}.test.ts and the q2-classic-guest/save-policy message tightenings, platform/{controller,
  window-*}, settings/* (corpus-gating, preferences and server-ui harness edits), text/{accessibility,presentation,
  sound-captions,sound-captions-table,world}, the truetype hostile-font case (needs a fake ParsedFontT; C fonts are
  concrete structs) and ui/hud/q2-native.test.ts. audio/engine.c (D8b) keeps a private SdlAudioDevice.removed copy
  that calls abort() when SDL enumerates no output (names == NULL); qa_sdl_audio_removed is the platform port.
  Not ported from the D8b modules: tests/audio/{bootstrap,precache-retail,rocket-effects,startup,loop-aggregation}
  (app-level or corpus-gated), the corpus-gated q1-map cycle cases (C has synthetic equivalents), and the
  corpus-gating and message-tightening edits in tests/content/catalog/{anchor-discovery,digest-cache,equipment,
  expansion-weapons,monster-roster,monsters,weapons,start-maps,qvm-weapon-behaviors}; tests/audio/undecodable.test.ts PLATFORM-003 (audio/bank, a d8-misc file) has no C case.
  D8b gaps: content/mounts keeps a fresh directory read instead of TS's cached-listing-first lookup (same result
  except when a cached hit has gone stale), and media/playback still hands tick callers a copy of the retained
  picture (TS now shares it). Known: world/actors/source_slots.c still sets the dropped
  ActorLifetimePolicy clear "source-gentity" for Q3 (D7 file); recipe.c's 34 KB "leak" was the save-image parse
  not freeing what it had read before a later field raised (fixed in save_image.c; test_save_mutation is LSan-clean).
sim_verify: C equals the current TS bun output
  in all 10 cases (0 differing lines). The stored oracles are current: D1 regenerated them from TS HEAD and got
  identical bytes. The checkpoint-image mismatches some agents saw came from the user content root following
  TMPDIR (its path is recorded in the images). Fixed 2026-11-03: test_sim_verify and its oracle now always use
  <repo>/build/agents/sim-verify2/tmp/sim-verify-user-<case>; with the full tree of ~10:30 all 10 cases have
  0 mismatches with TMPDIR unset.
- D4 (content/q2 TS drift, 2026-11-03) leftovers: (1) done by D1: `qa_app_sim_movement_player.up_move` is set in
  moveCommand and passed to `qa_q2_player_movement.up_move`.
  (2) TS tests from the drift not reproduced in C: rerelease monster AI "follows the rerelease source" (3 tests),
  q2-base "medic whose patient is alive again ... alarm does not mark the player hostile", weapons "a version 2
  weapon checkpoint ... still loads" (the C persistence reader accepts v2; "ammo dropped mid-fire" is checked at the
  consume step only),
  and the changed expectations inside TS gameplay/q2-* files that have no C mirror (no C fixture runs full Q2Monsters
  with traces, or the Q2 weapon fixture). New C checks: tests/content/test_q2_drift_d4.c. (3) test_sim_verify q3qvm
  crashes in compat/qvm ("invalid QVM instruction index 103974", then `qa_qvm_files_close_all(NULL)` in
  `qa_app_q3_qvm_server_game_discard`) with the tree of 2026-11-03 ~09:00 (compat/qvm mid-edit by another packet);
  the other nine cases have 0 differing lines. [D6, ~09:40: all ten cases 0 mismatches after the compat/qvm edits
  settled; `qa_qvm_files_close_all` now returns on NULL.] (4) tests/contracts/test_header_audit_a aborts on "Q1 official spawn
  not yet implemented: monster_army" (content/q1, D3).

- Q2 native guests (2026-11-02, sim-guests): `qa_app_sim_native_guest`
  (classic_guest_source.c) implements the `qa_compat_q2_memory`/`qa_compat_q2_runner`
  tables over SparseGuestMemory, GuestCallbackTable and GuestCallRunner, including
  register access, inline-region accepts, unwind (`qa_guest_call_runner_unwind`) and
  free processor. The retail CTF/Xatrix API3 and rerelease API2023 DLLs run through
  ClassicGuestSource/World and RereleaseGuestSource/World, and NativePrimaryWeapons
  runs on the API2023 DLL (tests/app/test_sim_guests.c). The native mod classes
  (NativeModProvider over `simulation/native-mod-host.ts` and `native-mod.ts`) run on
  the retail CTF API3 and rerelease API2023 DLLs (tests/app/test_native_mod_host.c);
  checkpoints match the TS classes byte for byte (oracle:
  tests/app/oracles/native_mod_host_oracle.ts). Still open: the C
  GuestCallRunner aborts on GuestCallStopped, so `runner.recover()` has nothing to
  restore. RereleaseWeaponBehaviorSource is ported but unexecuted: the corpus has no
  DLL matching any native weapon declaration (the built-in one is q2eaks).
- `src/bots/behavior/q3/library.c` must bind the library's `_configure` and chat or
  action hosts. Assigned: Q3 bot foundations.
- `preprocessor.c` root-frame leak per script reader. Assigned: Q3 bot foundations.
- `src/render/gl/renderer.c` was rewritten by two porters in turn and is unrun;
  `qa_gl_renderer_present` needs `swap`, which `src/app/bootstrap/renderer.c` does
  not set. Assigned: render top; the swap is for the connection step.
- UnifiedApplicationServer aborts the process on a malformed peer message, where TS
  drops the peer. Remote crash. Assigned: network follow-up.
- Evaluation order: C leaves argument and initializer order unspecified, so ports of
  `f(read(r), read(r))` can run reversed. Assigned: evaluation-order sweep.
- Full-tree ASan and UBSan run over every test binary: at the gate, once edits stop.
- Native mods follow-ups (2026-11-02, native-mod): the TS guest memory gained pooled
  pages mid-session (`SparseGuestMemory` "a pooled page can only be released whole";
  TS ClassicGuestSource/RereleaseGuestSource close now fails in the oracle). [D6: C guest
  memory has pooled pages now (allocate_pooled_page/release_pooled_page, and the
  allocator image); the test_native_mod_host oracle has not been rerun by D6.] `__stdio_common_vsprintf` reads its size_t count as int64, so the
  MSVC `sprintf` count SIZE_MAX aborts on 64-bit guests ("Guest printf buffer count is
  not size_t") where TS accepts it (the scanf count is fixed). classic records
  `resolves` calls the projection's `address`, which aborts where TS catches and
  returns false. System V format service states are still not freed with their
  runtime (the Windows ones are, through `qa_windows_own`). Not yet assigned.
- TS errors that are caught and recovered from: earlier porters aborted instead
  (see docs/PORTING.md). Re-check each module's TS `catch` blocks at the gate.
- Audio: TS prefers an SDL3 backend (`platform/audio.ts` `sdl3Backend`) and falls back
  to SDL2; C has SDL2 only. Port the SDL3 backend and the same selection order.
- Identity-keyed caches not ported: `render/scene/prepare.ts` pose caches,
  `retainedMerge`, GL uniform caches, the guest call-plan cache, the Q1/Q2 alias
  topology cache. They need an invalidation hook (model unload) so a reused
  address cannot return stale data. Port with the connection step.
- Q3 runtime owner (`app/bootstrap/simulation/q3/runtime.c`): with a Q3SourceHost the owner wires weapon fire and
  mover spawns to the owned missile and mover runtimes, the runtime restores the graph through
  `qa_q3_prepare_q3_graph`/`qa_q3_restore_q3_graph`, and close/destroy close the missile runtime. The table's
  `restore_part("graph")` (unit fixtures only) still uses the old reader.
- Gate full read still to do (2026-11-03, after review-c and review-d): core cvars/index
  from TS line 530; text/truetype; persistence except saved-game, source-bytes, value,
  q2-weapons (save-policy and save-image partly); contracts (all 42); materials, audio
  (all); world: read in full (gate-world 225, gate-render 395, gate-world2 183; see
  port_review.tsv), and the gate-render world findings are fixed (gate-world2: session.ts
  closeAll at all 13 sites, retired-bundle double close, mod-files path.resolve, observers
  kept past their call raise "closed" through closure handles, original-pickups releases
  scopes at close). World still open: authority keeps closed damage cursors, decision
  mutation copies, pickup-match states and create() states until destroy (growth); retired
  session worlds/seats/clients are freed when their bundle closes, so a pointer kept past
  that is undefined; a late grant() of an original-pickup selection used before its scope
  closed returns stale where TS throws; native_mod.c/qvm_mod.c destroy a command port
  their mod scope still owns (D1c). Movement is read in full (gate-world, 2026-11-03). Render: 293
  functions read (gate-render, gate-render2; see port_review.tsv). Still unread: scene/world,
  scene/shaders, SceneMaterialRegistrations bind/remap/refresh, models/prepare,
  models/renderer, particles/q3-system, cpu/{rasterizer, triangle-kernel, lighting,
  row-parallel, row-worker}, gl/* (11 files), worker, worker-protocol, worker-transport,
  worker-runtime; model shadows (r_shadows 2/3) still unported. Render leaks under LSan in
  test_remote_loopback are fixed (68.5 MB to 0 in src/render, gate-render2).
  Input except the 31 functions read; media except roq-audio; platform
  except freetype layout; then content, compat, bots, network, ui, app (not started).
  Content/q1 (gate-q1, 2026-11-03): foundation (17 files) and base (16 files) read in full,
  19 divergences fixed, checks in tests/content/test_q1_gate_review.c (bun oracle
  tests/content/oracles/q1_gate_review.ts). Still to read in full: content/q1/quakec (14
  files), addons, missionpacks, equipment (2); 13 of their save-restore paths only had the
  decoded-tree leak on raise fixed. The app-side Q1 primaryWeaponHandoff
  (simulation/arsenal/q1.c, D1c's area) was not read.
  Unported TS tests from review-c: component-command-owner case 2, local-seat-change,
  q3-native-runtime, q3-native-server, qvm-local-runtime, qvm-map-lifecycle,
  qvm-runtime-save, reference-save-fixtures.
- Review-c/d open findings: app callers not freeing `qa_cvar_transfer` after destroy
  (startup_application_frontend.c, maybe remote_application_seat/profile.c); cvar alias
  declaration errors abort; recipe.c readers leak 34 KB on raise; llm JSON depth cap 512;
  `pmove_ladders`: done by D1 (sourceMovement.ladders reaches the Q3 profile's `ladders_disabled` in player_movement.c,
  and the prediction step options carry `ladders`).
- Rows only located, not read: persistence, llm, contracts, text, media, foundation,
  console, most of formats and core, parts of materials and movement. Covered by the
  full read at the gate.
- The TS working tree is edited concurrently (on 2026-11-02 it had 425 uncommitted
  changes over HEAD f855f940). Ports read whatever was on disk.
  Baseline snapshot: build/ts-baseline/src-20261102-0217.tar. Superseded by
  docs/TS_DRIFT.md: the real port baseline is TS 53ad56d (2026-10-25), and every TS change
  since then (694 src files with code changes, 88 added, 140 added tests) is worked in
  packets D1-D8 with tools/ts_semantic_diff.sh. The gate needs every packet done.
- Startup top layer (2026-11-03, app-startup, app-startup2): `startup-selection.ts` is ported
  (startup_selection.c, StartupSelectionModel over the real catalog, archives, entity
  parsers, launch resolver, mod choices, QVM hook styles, Team Arena and base-arena
  selection; Q2 maps use `qa_q2_parse_entities_checked`; tests/app/test_startup_port.c).
  `startup.ts` StartupApplication is one C type, `qa_startup_application`
  (startup_application*.c; private state in src/app/bootstrap/startup_application_internal.h).
  Ported: the constructor (lobby transitions, StartupSaves, AddonLibrary changed/launch,
  FrontendPreferences dialect/onChange), static open, print, requestQuit, routeCommand,
  frontendCommand (snd_restart, music, cd), connect, lobbyJoinOptions, bindDemoCommands,
  bindFrontendConsole, frontendRoutingState, activateFrontend, failActiveSource,
  returnToFrontend, publishFrontendRouting, closeFrontendMovie (with the renderer clears),
  openFrontendMovie, the five library services, saveFrontendInput, persistPreferences,
  persistPreferencesOrWarn, refreshPreferenceBaseline, stageLobbyLaunch, launch, connectGtv,
  connectOptions, publishPendingSource, publishDemoIntent, dispatchApplicationRequest, step,
  run, close, the public getters, captureNextFrame (qa_startup_application_capture_next_frame
  forwards the renderer's capture request; with no menu it rejects "Startup menu is not
  visible"). Ported 2026-11-03 (startup-gfx), each over its real collaborators with no host:
  FrontendPreferences (bindings() over the ui/settings factories, audioBaseline, the 100 ms
  onChange timer pumped by step(), loadBaseline/saveGlobal/saveAudioBaseline over ConfigStore,
  MouseSettings and audio-settings; the written global-tuning.json and audio.json match TS
  byte for byte), StartupInputProfile (retained/open/save; change detection compares the
  compact JSON texts `qa_input_bindings_json`/`qa_gamepad_tuning_json`/`qa_seat_settings_json`
  in settings/config.c), StartupAudio (UnifiedAudio, SoundBank, ApplicationMusic),
  loadMenuFont/loadMenuTypography (scene image registry, SceneTextureLoader over
  `qa_app_scene_image_reader_open`, the TS mountedImageReader, and the localization coverage
  fallback pages), Application.networkWire (`qa_application_network_wire`; every
  ApplicationNetwork `wire` now returns a `qa_wire_selection`). loadAudioSettings no longer
  aborts on a foreign audio.json: it returns TS {} as the TS catch does. Fixed in
  render/scene/textures.c: ADD_CAND evaluated its xdup argument once per use (leaked candidate
  names) and the decoded PNG/TGA/BMP/JPEG pixel buffers were never freed with the loader.
  Checks: tests/app/test_startup_port.c (preferences bytes, input profile, menu font,
  typography and StartupAudio against oracles in build/agents/startup-gfx/tmp/oracle); wire
  checks in test_q2_app_units, test_q3_app, test_q1_hardening.
  Ported 2026-11-03 (startup2): StartupMenu in the TS options shape (the StartupSelectionModel itself,
  NativeUiArt, body/title TextFontSelection, appearance, characterTurntable, teamArena; the model
  fn-table and `skin_font` are gone), with draw, captionCommands, builderLayout/builderFields/
  builderDropdownSpec/builderFooter/builderViewer and row(); factory scratch is retired per
  generation and freed at the next input()/draw() (it grew without bound before).
  tests/app/test_startup_menu.c compares every RenderCommand draw() and captionCommands() emit with
  the bun oracle tests/app/oracles/startup_menu_draw.ts (13919 lines, 17 menu states, 3 caption
  passes; QTS_TEST_CORPUS). AddonLibrary is concrete (addon_library.c: libcurl catalog fetch with the
  20 s timeout and Cancel, skipped-row count from `qa_parse_addon_catalog_reporting`, cached-catalog
  fallback, HttpDownloadQueue installs, file: packages, importLocal, removeInstalled, purgeHidden,
  hide, ensureDirectory) and is the addons LibraryMenuService (`qa_app_addon_library_service`);
  tests/app/test_addon_library.c compares statuses, entries and the resulting file trees with the
  bun oracle tests/app/oracles/addon_library.ts, no network.
  Ported 2026-11-03 (startup3): openGraphics, graphics.draw and graphics.captionCommands
  (startup_application_graphics.c), in TS order over the real collaborators: renderer preference,
  openInitialConfigurationContent over model.catalog, the configuration store, the source cvar archive,
  ConsoleScriptFiles (`qa_app_open_configuration_scripts`, shared from application_open.c and declared in
  configuration.h), prepareInitialConfiguration with bindDemoCommands, the startup font mounts, InputDevices,
  loadMenuFont/loadMenuTypography/loadNativeUiArt({frames:false}), NativeRenderer.open + refreshDisplay,
  SdlControllers, the rerelease baseq2 theme mounts, StartupAudio open/openOutput/bindOutputCvars,
  SeatUiPreferences, CharacterTurntable, StartupMenu with its full settings list and the lobby view over
  ApplicationLocalLobby, StartupInputProfile.retained, bindInput, setFocus, InputRouter + attachWindow +
  devices.activate, ControllerSettings + bindGyro, SceneFrameBuilder, ApplicationCapture,
  ApplicationVideoRestart, keys.prepare/publish, the client record, bindFrontendConsole, capture.activate,
  videoRestart.register, the saved console history, forwardCommands, the initial requests, flushCommands
  and the pending "initial" source; graphics.draw (audio pump, world guard, menu.draw, the console overlay
  with consoleMetrics/drawConsole, execute). The openGraphics catch closes the opened owners in TS order;
  the closed graphics stay published until destroy (graphics->closed guards close()). main.c's graphical
  run/menu now opens StartupApplication (qa_startup_application_open, run, close; SIGINT/SIGTERM ->
  requestQuit; the host carries the LLM settings as both UI and requester), as TS main; its startup phase
  host, menu construction and nested-game helpers are gone. Fixed: refreshPreferenceBaseline used
  catalog.require where TS uses catalog.product, so a missing product raised "Content <product> requires"
  for the wrong product before openInitialConfigurationContent (C and bun now report
  "Content q1-classic-id1 requires: ..." for an empty --content-root). Defined here because
  startup_application_sources.c calls it: `qa_remote_application_detach_connecting_recording`
  (remote_application_commands.c, the TS detach closure). Checks: tests/app/test_startup_graphics.c, a
  hidden 640x480 CPU StartupApplication over Q1 id1, compares 1496 lines with the bun oracle
  tests/app/oracles/startup_graphics.ts (6 RenderFrames through native.execute, active menu and status per
  step, host prints, captionCommands through the client record); test_main_composition checks the
  StartupApplication failure text against bun. Observation seam: qa_startup_graphics.frame_observer is
  called with each frame before execute (the oracle spies on NativeRenderer.execute). Still open in this
  layer: the menu's lobby `selection` has no qa_composition_identity producer (buildUnifiedComposition
  over loaded content), so creating a lobby from the menu reports "Lobby composition requires
  buildUnifiedComposition, which has no C producer" after the TS hosting-connection check. Found while
  running the binary: platform/audio.c opens SDL2 (sdl2-compat on this machine) where TS prefers SDL3;
  at the default 22050 Hz output the SDL2 probe reports "SDL audio driver cannot report exact queued input
  frames" and StartupAudio.openOutput raises "Audio output is unavailable" (TS opens). The hidden runs here
  pass `+set s_outputRate 44100` (both sides) to avoid it. Not reproducible in C: the TS AddonLibrary "cancel during local
  import" ordering (stop() lands between two awaits of the import; the C import runs inside
  submit()). Fixed (lead, 2026-11-03): `qa_open_archive` leaked the header, ZIP tail and
  directory buffers when a parser raised; each is now released through a cleanup node. Found earlier: ConfigStore.saveSeat writes the parser key order where TS
  stringifies the object as built, so the first StartupInputProfile write of a fresh profile
  differs from TS in key order and in the rumbleStrength key (values equal after load); the C
  ConfigStore has no `decoded` quarantine (`.bad` rename) of an undecodable document. Async becomes synchronous:
  the lobby host/join transitions launch inside the lobby call (TS stages them for
  publishPendingSource); openFrontendMovie reads the startup configuration's mounts (C
  CampaignCinematic.prepare takes a MountedContent, TS passes scripts.openMounted), so a movie
  after a world has replaced those scripts raises "Movie playback requires the startup
  configuration mounts".
  Fixed here: startup_menu.c browser query/favorite/connection/discover failures go to
  browser.status (discover writes String(error)) and StartupMenu.input catches a controller
  failure into its status, as TS; Q3BrowserView maxPing/statusResendTime are JS numbers
  (`qa_q3_browser_number_fn`), so a fractional cl_maxPing is no longer truncated.
  `startup.c` (the former concrete-host shell) is no longer called by main.c; its tests still link it. Found while porting:
  `qa_q3_text_parse_error` (formats/q3_map/entities.c) aborts where TS throws a catchable
  TextParseError, so a malformed Q3 map entity lump aborts prepareMapChoices instead of
  marking that map unavailable; the session composition (network/common/session.h) carries
  the recipe only inside its canonical JSON, which lobbyJoinOptions reads back; nothing builds
  a `qa_composition_identity` from a unified composition yet, so the menu's lobby selection
  has no C producer; the gameplay-mod discovery mounts stay open for the model's life (the mod
  rows borrow their references) where TS closes them when discovery ends;
  demo_recording_commands.c leaves `busy` set if a feed attach raises.
- Q1 sync follow-up (2026-11-02): the quakec source composition in app/bootstrap must
  switch to the id1 `*_create_foreign` constructors. Not yet assigned.
- Q3, from the Q1 reader: `saveEntity` saves `sourceSlot` as null for dynamic entities;
  `armor_stage.c` caches by program pointer.
- Mounts: case-insensitive match is ASCII only where JS lowercases Unicode; the disk
  digest cache saves on every digest instead of the TS 250 ms timer.
- Leaks in `entity_services.c`, `entity.c`, `travel.c`. Q2 rerelease monsters:
  `qa_q2rm_bind_armor` allocates a cell context per monster spawn that nothing frees
  (test_q2_rerelease_monsters, 2 x 80 bytes).
- Parallel game and mirror types: Q3 content has no `*_mirror` types left (GameEntity.r is
  the EntityShared port, GameClient.ps the PlayerState port, items the shared itemList).
  TS `EntityState` is one C type, `qa_q3_entity_state_record` with number storage (the
  wire codecs apply `| 0`/`Math.fround` where TS does). app/bootstrap keeps
  `qa_app_*_mirror` types (remote seats, HUD, q3_client presentation, frame/video time).
  Q2 is one family now (`qa_q2_entity`/`qa_q2_game_services`/
  `qa_q2_foundation_host`; the `qa_q2f_*` family is gone). Q2Players has the TS
  virtual dispatch and Q2RereleasePlayers derives from it; CTF/LMCTF, equipment and
  mission-pack items run on the real weapons, player and item types; Q2 composition
  (content/composition/q2) is ported on the real types with test_q2_composition; the
  Q2 persistence codecs encode the content checkpoint types (one C type per TS type).
  Q2 mirrors still left: persistence/q2_hand_grenades.h `qa_hand_action` (a copy of TS
  HandAction); content/composition/q2_expansion_arsenal now runs on the real types
  (runtime-rest). The Q2 TS drift after the baseline (base player, foundation weapons,
  items, monsters, movers, rerelease monsters common.ts, ctf say_team) is ported (D4,
  2026-11-03; docs/port_review.tsv `drift-done` rows).
- Two recoverable-error mechanisms exist: `qa_try`/`qa_raise` (foundation, about 65
  files) and the Q2 `native_throw.h` channel (16 files). Fold `native_throw` into
  `qa_raise`. Also still aborting where TS recovers: `SourceItemRetired` raised fatally
  in `inventory.c` during `lease.stored`; CommonError "drop" in `core/common_error.c`;
  the Q3 native guest runner is unusable ("wrong architecture") after a host callback
  raises, and leaks.
- `quakec_mod.c` registers its callbacks and binds services.commands now (app-mods). No adapter
  exists from `qa_app_sim_shared_physics` to the typed session mod engine
  (`include/qa/world/session/mod_engine.h`): connection step.
- compat/qc, compat/qvm, compat/q3 depth tests were not run under ASan or UBSan.
- JS semantics: wildcard rows in `tools/js_semantics_audit.tsv` admitted without a full
  read (QVM profile-offset rows, Q3 content range-guard rows) must be re-read. The
  detector cannot see integer overflow where TS uses doubles, integer `%`, `t*t*t*t`
  for `t ** 4`, or wrong hand-written ternaries: the full read covers those.
  `localeCompare` is exact for ASCII only.
- Evaluation order: 4,583 sites are unclassifiable today, mostly calls through host
  interfaces with no implementation in `src` yet. After the connection step, re-run
  `tools/eval_order_audit.py` and classify all of them; "unclassifiable" does not pass
  the gate once implementations exist. 171 rewrites inside conditions use the comma
  operator and read awkwardly; tidy them during the full read.
- Six content binaries failed in several agents' builds during the Q1, Q2 and Q3
  refactors: test_content_composition, test_q1_addon_monsters, test_q1_missionpacks,
  test_q1_rows_4, test_q1_unified, test_q3_base_ta. Assigned to the Q1 and Q3
  unification agents to settle against bun on their final trees.
- Simulation shells batch (2026-11-02, sim-monsters): the Q1 foundation save reader
  returns `qa_persist_q1_foundation_checkpoint`, a second C type for TS
  Q1FoundationCheckpoint with no conversion to the content type, so read monster-source,
  foreign-Q1 and threewave-grapple checkpoints cannot be restored into a game yet.
  `SelectedMonsterSource` (monster_runtime.h) embeds `qa_app_sim_selected_q2_monster_modules`
  from q2_monster_sources.h, still an opaque-pointer shell. Not yet assigned.
- Possible TS defect, reported to the user rather than changed: TS `clientSpawn`
  replaces `ammoTimes` with a fresh `GameClient`'s, which has no `ammoTimerStored`
  hook. C reproduces it faithfully.
- Bots/UI script readers: a reader the chat loader or item-config loader abandons
  (retired scope, failed PC_ExpectTokenType) keeps its C objects, because destroying it
  also frees its script allocations in bot memory, which TS leaves live. It needs a
  C-only teardown (about 4 KB per abandoned reader; test_library_chat and
  test_library_core). Unassigned.
- QuakeCSource (`simulation/quakec_source.c`): matches TS on e1m1 (byte-identical
  entity and globals memory after 100 frames). Left: the local-message text pool grows
  for the source's lifetime (C state borrows text); checkpoint encoding of declaration
  subtrees not compared byte for byte with a TS checkpoint; `.slice(0, 31)` name
  truncation counts bytes, not UTF-16 units. The legacy
  `qa_app_sim_prepare_quakec_source` (no mounts) exists only for runtime.c and
  application.c; delete it when they use `_prepare_quakec_source_mounted`.
- `simulation/runtime.c` leaks cvar snapshots: `c_qc_host_cvar_set` (the `set` result)
  and `c_qc_cvar_register` (the `find` read and the `register` result), 16 per concrete
  host in test_sim_concrete. With the runtime port.
- Network adapters: `slice(0, 15)`/`slice(0, 126)` count bytes, not UTF-16 units. Q2
  foundation `localSound` (rerelease lowammo) is ported (D4): weapons/player.c emits the sound
  event with `local` set, and the network routing reads it. network-q3:
  TS caches the `Q3ApplicationPackages.open` promise, so a failed open fails every later
  prepare; C retries the open. `qa_executable_recipe_free` frees the gameplay mod, grapple
  profile, native declaration and QVM layout records shallowly (their readers have no free
  functions).
- The network adapters and bot shells take access structs
  (`qa_app_sim_network_simulation`, `qa_app_sim_network_content`,
  `qa_app_sim_q2_guest_world`, `qa_app_sim_bot_simulation`, `qa_app_sim_bot_navigation`):
  the runtime port must fill them from the real SharedSimulation.
- `test_q3_game_logic` leaks about 21 MB (5,632 allocations) in Q3 base entity records
  (`src/content/q3/base/records.c` and its users). Unassigned.
- Under ASan, `test_q3_presentation_depth` and three Q3 bot tests failed in one run
  where their sources changed mid-build; confirm on the full-tree run at the gate.
- Top presentation batch (2026-11-02, top-pres): `character-turntable.ts` is ported and matches
  bun bit for bit (tests/app/test_top_turntable.c, QTS_TEST_CORPUS rows). The lower-layer
  blockers of `presentation*.ts`, `mod-presentation(s).ts` and `selected-q3-presentation.ts` are
  resolved (pres-contracts, tests/app/test_pres_contracts.c): SimulationPresentationEvent is one
  owned, copyable tagged union (simulation/types.h, `qa_app_sim_presentation_event_copy`/
  `_release`); the model renderer and light sampler take `qa_world_scene`/`qa_world_view_input`
  and a ModelRenderProvider over the real texture loader and shader registry
  (`qa_app_application_assets_render_provider`); SimulationPresentation's held weapon, model
  anchor and grapple cable are typed; `qa_mod_qvm_presentation_source.context` returns
  `qa_qvm_presentation_context` from both producers; client collision takes the shared clip
  models; the Q3 client assets host is built from ApplicationAssets; Q3SelectedSource and
  Q3SourcePresentationState are ported. Still open on the presentation path (void* or mirror
  where TS has a type; found by the pres-contracts sweep): component_scene.h (`state`, `bounds`,
  `game_state`, `player_state`, `visible_entities`, `select_visible`: Q3PlayerState, Bounds,
  SourceGameStateRecord, EntityStateFields[], Q3VisibleEntities); held_weapon.h `held_pass`
  `resource`/`model` (ResolvedResourceReference, DecodedModel); native_q2_client.h `payload`
  (SimulationPresentationEvent) and its host/world `entity`, `fog`, `fog_from_wire`, `empty_fog`,
  `player_state`, `entity_state`; network/remote.h `qa_app_q2_remote_event.effect`;
  remote_unified.h / unified_component_consumer.h `installed_mounts`/`borrow_mounts`
  (MountedContent); network/unified_types.h frame members (PresentationOwner, ModIdentity,
  NativeModCameraView, UnifiedComponentFrames, UnifiedPredictionProjection, SimulationOutput,
  SimulationPresentation[], Q3CharacterView[], WorldText[], PlayerView, PlayerUi); the unified
  network carries SimulationPresentationEvent as qa_save_value trees
  (simulation/network_unified.c, network/unified_frame_values.c, unified_event_codec.c) and
  PlayerUi.nativeInventory as a save tree, where TS has the typed event and
  NativeInventoryReadout; q3_client.h `movement_timing` and `receive_events`/`receive`
  (`const void *events`: SimulationPresentationEvent[]); q3_client/cinematics.h
  `image_allocate` (RendererImage); remote_q1.h/remote_qw.h/remote_q3.h `character_views`/`presentations`
  (`const void *`, always empty; remote_seat_source.c casts them); selected_q3_presentation.h
  presentation/event arguments; world/session/mod_client_presentation.h `configstrings`
  (ReadonlyMap<number,string>); world/session/mod_presentations.h typedefs
  `qa_session_qvm_entity_state_fields`, `qa_session_source_game_state_record` and mods.h
  `qa_session_qvm_artifact`; compat/qvm/mod_provider.h `scene_state`/`scene_baseline`
  (QvmModScenePublication); content/q3/presentation/ref_entity.h `value` (SceneShader |
  SceneSkin); simulation/types.h `qa_app_sim_player_ui.native_inventory`.
  compat/q2/rerelease/world_text.h keeps q2WorldText's arguments
  (`qa_compat_q2_world_text_input`) where TS emits WorldTextInput; the SharedSimulation world-text
  sink applies `qa_q2_world_text` to them (as native_mod_host.c does). compat/qc: TS ModMessages drops
  "monster-killed" when `publishesKills` is false; the C provider has no publishesKills.
  test_top_effects and test_top_assets now run their retail cases by default (content-life,
  2026-11-03: resource references are owned values, see contracts/content.h). Under
  LeakSanitizer they leak the loaded world's textures and material registrations (about 62 MB
  in test_top_effects, 15 MB in test_top_assets: render/scene/textures.c load_uncached/rgba_wrap,
  material_registrations.c new_registration, world.c lightmaps). Unassigned.
- void* sweep, not on a presentation path (2026-11-02, pres-contracts): struct members and
  callback results typed `void *`/`const void *` where TS has a concrete type, to type during
  the full read. app/bootstrap: application_tools.h `bind_buffer`; capture.h `commands`;
  client_bootstrap.h `attach`, `window`; content.h `application_world.value`;
  demo_recording_commands.h `attach`, `reconnect_recording`; network/q1_types.h (`info`,
  `baselines`, `signon`, frame `messages`/`reliable`/`datagram`/`entities`, host
  `supports_source_wire`/`spawn`/`input`, `transport`); network/q3_client_content.h
  `load_content`; network/q3_types.h (`player`, `entities`, `administration`, `product`, `pure`,
  `open_download`, `rate`, `supports_source_wire`, `game_state`, `input`);
  network/qw_server_types.h (`entities`, `messages`, `reliable`, `administration`,
  `authentication`, `masters`, `supports_source_wire`, `client_info`, `signon`,
  `prepare_download`, `baselines`, `transport`); network/remote*.h `content`/`load_content`,
  remote_unified.h `map_entities_provider`; network/remote_world.h `create_scene_queries`;
  network/types.h (`seed`, `wire`, `poll`, q2 game state `data`/`config_strings`/`baselines`,
  `event`, `downloads`, `message_options`, `administration`, `discovery`, `masters`,
  `supports_source_wire`, `mvd_capture`, `frame`, `input`, client `downloads`/
  `message_options`/`command`, `transport`, `remote`); network/unified_content.h
  `load_content`; options.h (`q3_map_launch`, `team_arena_skirmish`, `server_profile`,
  `weapon_behavior_command`, `parse_weapon_behavior_tool`); original_save.h `data`;
  simulation: foreign_q1_world.h `host`, native_q1_pusher.h
  options, native_q2_travel.h `selected_arsenal`, network.h / network_q3.h / network_qw.h
  `administration`/`masters`, network_q2_guest.h `entity_states`, q2_monster_sources.h (all
  module and factory members), q2_native_world.h `primary_profile`/`source`/`builtin_primary`,
  q3/guest_artifact.h (`artifact`, `items`, `primary` and the prepare-host results),
  q3/guest_bots.h `selected`, q3/guest_runtime.h `checkpoint`, q3/types.h (`services`,
  `mover_actors`, `actors`, `bodies`, `callbacks`, `combat`, `inventory`, `scene`,
  `original_pickups`, `death_animations`, `weapon_behavior`), quakec_source.h
  `prepare_resources`, qvm_mod.h `mounts`, types.h (NativeQ2GuestOptions `capabilities`,
  `prepared`, `localize`, `clipboard`, `semantic_bindings`; SimulationTravel `lmctf`,
  `rerelease`, player `hand_grenades`, `selected_arsenal`, `carry`; SimulationOptions
  `prepared_qvm_grapple`, `prepared_mods`, `mod_commands`, `mod_files`, `enabled_mods`,
  `mod_travel`, `prepare_rerelease_navigation`, `weapon_behavior_real_time`,
  `weapon_behavior_clock`, `weapon_behaviors`, `prepared_quakec`, `native_q2_travel`,
  `q3_guest`, `world`, `mounts`, `q3_session`, `server_profile`, `source_registry`,
  `source_archive`, `monster_navigation`); startup.h `captures`; startup_audio.h
  `engine_selected_output`; startup_selection.h `payload` (x2), `binding_capabilities`;
  video_restart.h `client_command_registration`; weapon_behavior_selection.h `payload`,
  `mounts`, `for_content`; weapon_behavior_tool.h `open_product`. world/session: mods.h
  `clients_visibility`, `clients_at`, `scene`, `physics` (typed views in mod_engine.h),
  `native`. compat: q2/classic/layout.h `cpu_state`, `capture_processor`;
  q2/compatibility.h `classic`, `rerelease`; q2/native_input.h `host`;
  q2/native_mod_items.h `items`, `entry`; q2/native_pickups.h `frames`; q2/native_primary.h
  providers and `world`; q2/rerelease/navigation.h `runtime`; qvm/memory_writes.h `watches`;
  private containers typed `void *` in qvm/mod_actors.h, mod_clients.h, mod_input.h,
  mod_items.h, mod_pickups.h, mod_player_events.h, mod_presentation.h, mod_protection.h,
  mod_provider.h (`frames`, `pickup_scopes`, `weapon_inputs`), mod_weapon_stage.h,
  registry.h `slots`, symbols.h `records`. render: scene/models/renderer.h `materials`,
  particles/q3_system.h `particles`/`animations` (private containers). C-only storage,
  closure contexts and TS identity tokens are not listed.
- Top input (2026-11-02, top-input): `input.ts` is ported (input.c, input_commands.c,
  input_platform.c), and `input-devices.ts` / `controller-settings.ts` were re-ported over
  the real MIDI, joystick, router and config owners (their host-shaped C had no callers).
  For the connection step: application.h's function `qa_app_application_input()` collides
  with the `qa_app_application_input` typedef once application.c includes input.h (rename
  the function); `qa_app_client_bootstrap` (client_bootstrap.h) holds only the
  ClientBootstrap members input.ts reads; seats and consoles published into a prepared
  startup or client hold their ApplicationInput until `qa_app_input_release_holder(owner)`,
  which the application must call before closing that startup or client.
  `sharedSettings()` returns the current shared registry, where TS returns a view that
  follows a later `publishSharedCvars` (the C ui/settings take a concrete registry).
- `test_q3_presentation_depth` leaks about 408 MB in presentation audio and media
  registration. Unassigned.
- SharedSimulation B1 remainder (`bindNativeInput` and `captureNativeQ2Travel` are done, runtime-rest):
  `capture/restoreSelectedMonsters` and
  `capture/restoreForeignQ1World` are blocked on converting
  `qa_persist_q1_foundation_checkpoint` (save reader) to `qa_q1_foundation_checkpoint`
  (content). TS moved tag, LMCTF and DeathBall into `foreign-match-rules.ts` during the
  port: re-check at the drift gate. TS drift without C members: `cloakAlpha` in the
  monster target observation, `foreignItems` in Q2 item hooks. Next free slot.
- Q3 sources (2026-11-02, q3-source): `qa_app_q3_source_runtime_new(options, host, mode)` builds the content/q3
  subsystems from the Q3SourceHost (typed now) and runs on them; `qa_app_shared_sim_create_q3_source` and
  `qa_app_shared_sim_create_q3_qvm_source` are ported; Q3QvmServerGame owns its QvmGame, files, parser and syscall
  host; PreparedQ3Game is typed. Construction over retail q3dm1/mpteam1 (TS source and QVM guest) matches the TS
  dumps line for line (tests/app/test_q3_source.c, oracles in tests/app/oracles/q3_*). Still open: the Q3 native
  checkpoint parts locations/spawns/match/remaps/settings/memory/bridge/missiles/team/arenas/personalPortal/
  serverCommands have struct captures but no checkpoint-tree writer, so `captureNativeState` writes null for them
  (graph, registeredItems, server, report and publishedEvents are written); restore reads every part. The guest
  test runs with bot_enable 0 (no bot library attached), so "32 bots parsed, 35 arenas parsed" are not produced.
- Q1 checkpoint drift: TS Q1 content checkpoints are version 6 (monster animation
  moved, `legacyAnimation`); the C content and reader are version 5, so Q1 checkpoints
  cannot match TS until the Q1 content port catches up. Unassigned.
- `runtime.c` sets save schema_version 3; it is deleted at connection, but anything
  still using it writes old images until then.
- Seat presentation (2026-11-03, app-seat-pres): held-weapon.ts, native-held-weapon.ts and
  q3-selected-weapon.ts run on real ApplicationAssets (HeldAssets/NativeHeldAssets are the TS structural
  interfaces, with ApplicationAssets adapters); presentation-scene.ts (ApplicationWorldScene) and
  presentation.ts (WorldSeatPresentation, plus seat-factory's createSeatPresentation as
  `qa_app_world_seat_presentation_create`) are ported. Scene operations and held passes match bun line for
  line on retail e1m1/q3dm1 (tests/app/test_world_seat_presentation.c, oracle
  tests/app/oracles/world_scene_oracle.ts). mod_client_presentation.h carries `qa_native_q2_hud_frame`;
  the Q3 scene presentation has `server_time`. Still open: ApplicationQ3Client (q3-client.ts) is unported,
  so WorldSeatPresentation reads it through `qa_app_seat_q3_client`, which the application must fill;
  application_step.c (movie branch) must call the new prepare(snapshot, presentations, characters) and
  application_close.c must call `qa_app_world_seat_presentation_destroy` after close (close no longer
  frees); finale.c, q1_fog.h,
  q1_localization.h and the console draw target stay host/mirror-shaped (adapters in presentation.c);
  rendering a seat frame on the CPU backend fails "CPU image dimensions disagree" for the console font
  ("conchars") image registered by `qa_app_application_assets_load_console_font` (its content is gone by
  upload); mod-presentation.test.ts still needs an ApplicationAudio test seam.
- runtime-rest follow-ups (2026-11-03): `qa_app_shared_sim_open_mods` (load's `SessionMods.open`) is still not
  ported: it needs adapters from the PresentationState owner binding (`qa_app_sim_presentation_event` vs
  `qa_app_sim_source_presentation_event`) and typed views for the mod engine services (scene, physics, Q1 client
  visibility, mod operations of the authority, callback table and inventory). `prepareApplicationWeaponBehavior`
  (weapon-behavior-selection.ts) is still a host seam (`qa_app_weapon_behavior_host.prepare`) that nothing implements,
  so the `qa_app_prepared_qvm_weapon_behavior` / `qa_app_prepared_native_weapon_behavior` payloads are never filled.
  TS missionpacks/items.ts `registerQ2MissionPackItems` `rereleaseCapacity` and rerelease quantities are in the C
  registration and the bandolier/pack ceilings (`qa_q2mp_mission_ammo_pack_capacity`) are ported (D4). players.c
  moveQ3Client passes a selected `qa_app_shared_sim_arsenal` read to weaponStep without freeing it.
  Not yet assigned.
- From the runtime-rest porter: `prepareApplicationWeaponBehavior` (application layer)
  is unported, so the qvm and native weapon-behavior payload records are never filled;
  Q2 mission ammo item registration has the TS rerelease capacities (D4);
  `shared_simulation_players.c` moveQ3Client leaks its selected input read.
- Q3 native checkpoint (`captureNativeState`) writes null for locations, spawns, match,
  remaps, settings, memory, bridge, missiles, team, arenas, personalPortal,
  serverCommands and level: those modules have struct captures but no writer to the
  checkpoint value format. Unassigned.
- Q3 client (2026-11-03, app-q3client): `q3-client.ts` (ApplicationQ3Client, q3_client.c) and
  `q3-client/qvm.ts` (ApplicationQvmClient, q3_client/qvm.c) are ported on real types: the QVM
  host runs the TS syscall chain (status, common, files, scripts, render, audio, client state,
  collision, marks, cinematics, browser, UI keys, scalar) over the real services, equipment and
  held-weapon observers bind on the cgame module, retained artifacts are refcounted for video
  reopen. QvmCvarServices is now an interface in compat (`qa_qvm_cvar_table`,
  `qa_qvm_cvar_table_syscall`; `qa_qvm_common_services.cvar_table`), so Q3ClientCvars and the
  CgameStatusView mask run through the compat cvar syscall; `legacyClientCommand` returns null
  as TS does; SharedCvarMirror has a CvarRegistry view (`qa_cvar_registry_mirror_view`);
  `applyServerSystemInfo` is ported (userinfo.c); StartupServerBrowser has a Q3BrowserHost
  adapter (`qa_app_startup_server_browser_q3_host_create`). Follow-up (app-q3c2, 2026-11-03): q3_client/scene.c
  (SceneModelRenderer per provider, world/material batches) and cinematics.c (CinematicImage in the ApplicationAssets
  registry, implicit picture material with a video binding) run on the ApplicationAssets behind ApplicationQ3Assets
  (`qa_app_application_q3_assets_application`); the `scene_renderer`/`cinematics_host` options and the presentation
  hosts' `cinematics`/`scene_renderer` are gone. The services, the scene recorder (`viewport_ref`), the text sink
  (`qa_text_command_sink_share_target`, `qa_text_draw_sink.target_ref`) and the presentation (`common.target`) share the
  client's viewport. WorldViewInput has `model_shadows` (world.c clears stencil and appends shadow-finish as TS does) and
  Q3SceneRenderOptions has `rail`. remote_q3.c fills the whole cgame source. ApplicationVideoRestart and
  prepareVideoGuests run over ApplicationQ3Client with raised errors. tests/app/test_top_q3_client.c runs
  ApplicationQ3Client kind "qvm" over retail baseq3 ui.qvm/cgame.qvm on q3dm1 with a fixed game state and snapshot
  session and matches tests/app/oracles/q3_client_oracle.ts line for line (ui/cgame console output, 21 models and 3
  skins, 4 frames of draw commands: 1,295 lines). Still open: SceneModelRenderer has no modelShadowGroups (the TS
  stencil-volume and projection-shadow groups of cg_shadows 2/3: stencil-shadow.ts, shadowVolumeGroup), so
  `model_shadows` reaches the world but no model emits shadow volumes; `prepareVideoRestart` (application.ts, P2) needs
  an ApplicationQ3Client adapter to `qa_app_seat_q3_client` for replaceQ3Client. Fixed on the way (lower layers, found by
  the retail run): qvm_display/qvm_scalars/status wrote guest records through `qa_qvm_writable_view.bytes` (the
  allocation base, not the view start); render/scene/view.c projected with unstaged dots where TS uses `dot3`;
  world.c registered *fog/*dlight before the surfaces, kept one static clear/clip plane for every prepared view and
  leaked merged sky geometry; material2d.c cast a MaterialHandle picture to a CompiledMaterial (pictures now carry
  `compiled_is_handle`); `qa_content_mounted_list_files` returned NULL for an empty listing. test_top_q3_client under
  LeakSanitizer still reports the known texture/material-registration leaks (about 28 MB), none from the client.
- SharedSimulation link (2026-11-03, sim-link): every shared_simulation_*.o links with no unresolved symbol and the
  tests/app/test_shared_simulation_* binaries link through the normal rule. Post-restore key orders are tracked for
  source movement, grapple controls and weapon state, the Q3 selected arsenal and Q2 sourceModels. Still open: the
  player checkpoint's `readArsenal` builds { provider, activeWeapon, state, ammo } for every family and a restored Q3
  selected arsenal's read() is { provider, activeWeapon, ammo, state }; both reach players[].arsenal, whose writer
  (`qa_app_sim_arsenal_state_value`) picks the order by weapon kind. Carrying that needs an order bit on
  `qa_arsenal_state` (contracts/movement.h, about 1,200 dependent objects). Not yet assigned.
- Application core (2026-11-03, app-core): the real `Application` is `qa_application` (application_class.h,
  src/app/bootstrap/application_*.c; layout and parallel assignments P1-P7 in docs/application_layout.md), built beside
  the concrete-host `qa_app_application`. Struct, constructor, step, run, recoverDedicatedFrame, close and closeOwned
  are ported; openSource is blocked on lower layers with no concrete C form (content.c prepared artifacts,
  openInitialConfigurationContent, resolveApplicationMovement, authoredCampaignStart, sourceConsoleScripts,
  prepareConfiguredApplicationRecipe), openBorrowed on the ClientBootstrap members, and step/close read
  WorldSeatPresentation members presentation.c does not expose. Listed in the layout's "Dependency gaps".
- `qa_q3_text_parse_error` aborts on a malformed Q3 entity lump where TS throws a
  catchable error (startup selection marks the map unavailable): convert to `qa_raise`.
  Nine eval-order sites in `startup_selection.c` are judged allocation-order-only but are
  not ledgered in `tools/eval_order_ledger.tsv`. Unassigned, small.
- `src/content/catalog/launch.c` `launch_service_qvm_compat` reads QVM compatibility
  through the whole mount set, where TS uses the scoped reader. Unassigned, small.
- RemoteApplication (`docs/remote_application_layout.md`): the q1, qw, q2, q3 and unified clients connect over loopback
  UDP to the real C Application server and match the TS RemoteApplication against the same server line for line
  (datagrams through signon, first snapshots, console; tests/app/test_remote_loopback.c), including the unified server
  over the mixed `--preset q2-q1-q3` recipe (unified-network packet, 2026-11-03: typed presentation events <->
  event trees in network/unified_event_values.c, the replica's PresentationState, RemoteComponentView, the concrete
  UnifiedContentHost, the server's typed publish and its prediction-projection ownership fix). Clients whose profile
  differs from the served recipe adopt it as TS does (unified2, 2026-11-03): `qa_app_client_socks_settings_route`
  kept one `base` on the shared ClientSocksSettings, so the candidate's staged routing rewrote what the published
  configuration's routing reported and the prepared startup counted the candidate's fresh source registry as live
  ("Candidate client commands require isolated cvar owners"); TS `route(base)` returns a new closure per call and the C
  route is now its own object. unified-cross (Q2 client, Q1 server), unified-cross-q1 (Q1 client, Q2 base1 coop
  server) and unified-cross-q3 (Q3 client, Q1 server) run by default, 0 differing. App-layer leaks (unified2): every
  default family under ASan+UBSan (build/agents/unified-asan, with the TS runs) reports no ASan or UBSan error and
  68.2 MB leaked with no allocation site in src/app (LeakSanitizer, normal -O2 build, C runs only: 128.7 MB before the
  fixes, 66.6 MB after). Fixed: cleared presentation and bound seat UI (seat HUD, event rings, scene) freed at the
  RemoteApplication's destroy; RemoteSeatView frees its seat UI; per-step unhandled effects; profile default bindings and
  configurationScriptReader ctx (configuration.c); prepared client commands' print state; socks register snapshots;
  demo command handler ctxs, the startup ClientDemoCommands and the published-source demo binding (TS releaseDemos);
  server definitions array. Remaining, owned elsewhere: render (gate-render, ~68.06 MB: world/model/shader textures
  through render/scene/textures.c load_uncached, world lightmaps from world.c qa_world_scene_load, world.c q2 sky
  operations, resources.c image registry updates, material registrations/shader names, md3 tags via
  render/scene/models/transform.c); simulation (D1c, ~106 KB: prediction runtime.c's per-command movement
  effects/contacts, Q2 carry set and Q3 spawn loadout at admission, ss_cvar_set/ss_cvar_register snapshots in
  shared_simulation_source.c). [D1c: fixed the prediction replay (runtime.c, sequence.c, presentation.c free each step's
  movement effects/contacts; step.c frees the movement array it replaces), the Q3 spawn loadout (players.c: the player owns it
  as owned_arsenal_ammo), the cvar snapshots, and qa_capture_q2_product's schema strings (checkpoint.c). The Q2 admission
  carry leak did not reproduce under LSan in sim_verify (q2c). Still leaking under LSan in sim_verify, outside D1c's files or
  not traced: qa_q2_cvar_binding_pull's snapshot (settings/server/q2_owner.c:446), qa_q1_character_actor_capture's replaced
  null value (content/q1/base/player.c:198), and the last QC pusher candidate buffer (physics.c q1_services_candidates). The
  native_mod.c / qvm_mod.c double destroy is fixed: the mods no longer destroy the command port their scope owns; the
  port's memory is not freed by qa_mod_commands_destroy (world/session/mod_commands.c, gate reader's area).] Open: (2) not run against TS output: the unified RemoteSeatView
  (local remote seats), component sources, a Q3 character over the replica; (3) C payload domains narrower than the
  TS codec: Q1ClientSnapshot slot/shirt/pants and CtfStatus flags/runeItems are int64_t, Q2 model attachedModels is a
  fixed [3], SimulationPresentationEvent.sourceEntity has no null/undefined distinction (named in
  tests/network/test_unified_event_values.c); (4) a decoded damage or transition simulation event raises in the
  client view (unified frames never carry them: permitted() drops both).
- SharedSimulation side by side (2026-11-03, sim-verify): tests/app/test_sim_verify.c and its oracle
  tests/app/oracles/sim_verify.ts construct the real SharedSimulation through loadSimulation on retail data
  (parseApplicationCommand -> loadApplicationContent -> SimulationOptions, guest options on a deterministic
  clock) for TS Q1, TS Q2 classic/rerelease, TS Q3, QuakeC, native Q2 classic/rerelease, the Q3 QVM guest and
  the mixed `--game q2-classic-baseq2 --map base1` / `--preset q2-q1-q3` recipe, and compare a state dump at
  frames 0/1/10/100 (expected dumps tests/app/oracles/sim_verify_<case>.txt, written from the TS tree of
  2026-11-03 01:12). Through frame 100 the state lines match for TS Q3, QuakeC, native Q2 classic and rerelease
  and the Q3 QVM guest. Still differing, not yet assigned: TS Q1 (TS moved monster_army/monster_dog from
  foundation/monsters to the base species registry with a different start schedule; C still spawns them through
  foundation/monsters.c, and base/species.c has no army/dog rows or frame tables); Q2 rerelease monster AI drift
  in ai.ts/perception.ts/index.ts (thirdEye, reachedHoldCombat, noStep, badMoveTime, bumpTime, slideAlongObstacle,
  refusesWater, alternateFly, nearestNoise, faceWall, M_FlyCheck, power-armor effects) and the
  MonsterState fields behind them; the mixed recipe's Q1-movement player wedged at frame 97: TS NetQuake/QuakeWorld
  sweepBody `samePlane` is JS object identity of the trace's source-plane normal (shared geometry plane objects;
  a Q1 hull back-side hit makes a fresh object), while C `sweep_same_plane` compares plane slots only, so a plane
  hit twice is a crease in C (velocity zeroed) and the same plane in TS; carrying a plane identity on
  qa_trace_result through the q1/q2/q3/body producers and the swept services is the fix; checkpoint key orders of launch-built records (resource
  references, mounts, provenance, timing, execution, Q3 movement state, players) and checkpoint schema drift
  (Q2 weapons formatVersion 3 noises, monster state fields, g_select_empty, bodyque spawn values,
  q3:native null parts). Movement results (Q1 contexts, Q3 contacts) and Q3 character animation steps leak
  their arrays: nothing owns a qa_movement_result after it is consumed.
- Application P3 leftovers:
  Not reached: prepareLocalGuestSeat, createClientCvars, uiSourceOptions,
  prepareRestoredGuestSeat, restoreSourceInputAngles, publishLocalGuestSnapshots,
  prepareSavedClients. `q3_rankings.c` keeps account-action contexts in a static
  64-entry table shared by every owner. The C bot asset loader lacks TS
  `legacyProvenance` and loads navigation eagerly, so restored bot provenance can differ.
- WorldSeatPresentation is connected in application_step.c (movie console branch) and
  destroyed after close in application_close.c; the seat-replace close branch goes
  through the seat resource scope: confirm it frees the presentation (leak run).
  Executing a seat frame on the CPU backend aborts with "CPU image dimensions
  disagree" on the "conchars" console font from
  `qa_app_application_assets_load_console_font`. Leaks: `md5.c` pose sampling via
  `qa_model_grip_transform`. Unassigned.
- From app-deps: the rerelease presentation's `prepareImageRefresh` loads skies through
  its host instead of the prepared images; `ApplicationVideoRestart` (`video_restart.c`)
  still reports errors as strings and nothing creates one; leaks in
  `remote_application_config.c:547` (cvar register snapshot), `qa_default_bindings`, the
  startup script reader context (no destroy), and prepared_startup client commands.
- From app-p6 (2026-11-03): under LeakSanitizer, a Q1 dm1 SharedSimulation load + 12 frames (test_match_results) leaks
  Q1 movement contact/effect arrays (movement/q1/common.c push_contact, qa_q1_context_effect), cvar snapshots in
  shared_simulation_source.c `ss_cvar_set` and `qa_cvar_registry_register` callers, `qa_session_mods_open` (mods.c:477)
  and the Q1 combat policy (policies.c qa_create_q1_combat_policy, qa_native_victim_armor). Unassigned.
- Gameplay mods (2026-11-03, app-mods): the declaration writer (content/mods/declaration_writer.c) matches bun
  JSON.stringify for 202 declarations (test_mod_declarations); preparedMods, mod-selection.ts, save_image.c
  `gameplay_mod_value`, native-q2-client.ts and presentOutput are ported (docs/application_layout.md, "app-mods").
  Open: C strings end at NUL, so a declaration string holding "\u0000" (TS fixtures that test its rejection) reads as the
  text before it, and the C reader/validator sees a different string than TS (a C-wide string-model limit, not the
  writer's). The retail corpus has no gameplay-mods.json. presentOutput's graphical branches are not checked at unit level.
- Guest bots (2026-11-03, app-botsnet): done. compat/qvm bot-library, legacy-bot and bot-navigation syscalls take
  the real BotLibrary, SourceBotNavigation and AasBspEntities (the `qa_qvmx_bot_library` / `qa_qvmx_bot_navigation`
  mirror tables are gone); guest_bots.c is a port of guest-bots.ts; `qa_application_prepare_guest_bots` is ported;
  saved bot connections carry the client generation and openSource decodes the saved bots checkpoint. Retail check:
  tests/app/test_guest_bots_verify.c matches tests/app/oracles/guest_bots_verify.ts (bun) line for line on baseq3
  qagame.qvm q3dm1 through 300 frames ("32 bots parsed", addbot, bot command times, bot and player positions).
  Found on the way: the Q3 guest ran BOTAI_START_FRAME synchronously (TS botFrameAsync) and aborted on the first bot
  user command; guest_spatial.c trace/pointContents aborted on pass entity -1 (TS records.reference); guest_spatial.c
  pointContents iterates every entity with owner checks where TS queries the actors at the point and skips only the
  pass slot (not re-ported here). The navigation syscalls stage goal, move-result and target records (the C
  SourceBotNavigation takes values where TS takes live guest references). test_qvm_a's NaN max-frames case was removed:
  the C query fields are int32 words, so the case cannot occur.
- Server browser (2026-11-03, app-browser): done. server-browser.ts, server-master-list.ts, server-browser-addresses.ts
  and server-browser-cache.ts are re-ported over src/network: `qa_app_startup_server_browser_open(config)` binds a
  `qa_udp_transport` (0.0.0.0:0, broadcast) and builds the four ServerBrowser cores (`qa_server_browser_create`) with the
  Q1/QW/Q2/Q3 wires; resolveAddress is `qa_net_resolve_address`; the master list is a libcurl GET (redirects followed,
  1 MiB and 4096-server bounds, fatal UTF-8, invalid lines skipped and counted, the 4 s AbortSignal as a deadline); KEX
  mDNS is `qa_kex_mdns_*`, pumped by poll(). network/q3/discovery uses the shared types (`qa_q3_discovery_wire_init` fills a
  `qa_discovery_wire`; statuses are `qa_server_status`; master records `qa_net_address`); browser_view.h's host returns
  `qa_server_status`. User-input errors raise catchably (TS throws); poll() catches every remote packet failure as TS
  does (no remote datagram can abort: tests/app/test_server_browser.c mutation pass); open() skips and reports invalid
  saved lists and caches; loadQ3Cache reads the live statuses before clearing the lists (the old C read them after);
  `q3List` returns the browser's snapshot (same pointer while the generation holds). openGuestBrowser is
  `qa_application_open_guest_browser` (application_guest_browser.c). Tests: tests/app/test_server_browser.c ports the
  three TS server-browser test files (menu clicks replaced by the browser calls they make), the startup-selection
  browser case against the C Q1/Q2/Q3 hosts on loopback, the Q3BrowserView over the adapter, and a mutation pass; clean
  under ASan/UBSan with leak detection. Fixed on the way: `qa_kex_read_discovery` leaked its partial status when the
  reader raised on a hostile lobby packet. Open: startup_menu.c calls query/favorite/
  connection/discover/scan without qa_try, where the TS StartupMenu `run()` catches and puts the message in
  `browser.status` (`qa_app_startup_server_browser_set_status` exists for it); the Q3BrowserView takes int64
  maxPing/statusResendTime, so a fractional cl_maxPing truncates; a failed settings write reports
  "<strerror>: could not write <name>" where Node reports its fs error text, and a failed mDNS bind "bind <strerror>
  0.0.0.0:5353" where Node prints the errno code; a TS close() during a pending DNS lookup or settings write
  cannot interleave with the synchronous C calls. Next free slot for the menu catch.
- The presentation access view leaves `drain_presentation_events` NULL. On openSource failure the open record and
  closure state are not freed. Next free slots.
- Application concrete collaborators (2026-11-03, app-concrete): capture.ts, application-tools.ts, precache.ts and
  campaign-cinematic.ts are ported to their TS shape over ApplicationInput, the console commands, FrameCapture, the
  NativeRenderer, the spline camera, the diagnostic registrations, MountedContent, the SharedSimulation sources,
  ApplicationAudio/Effects/Assets and the media layer (docs/application_layout.md, "Found by app-concrete"); tests
  tests/app/test_app_concrete_*.c against bun oracles. Follow-up (app-concrete2, 2026-11-03): original-save.ts, the
  rerelease presentation and the Q3 presentation hosts over ApplicationAssets, the ApplicationQ3Client seat view,
  createWorldSeatPresentation and prepareVideoRestart are done (docs/application_layout.md, "Found by app-concrete2";
  restoreOriginalSave existed but was undeclared). Follow-up (app-concrete3): the seat UI asset owners (weapon HUD, Q1 wheel, sound captions, native Q2 HUD,
  game prompt) run on ApplicationAssets, StartupSaves on the catalog; createSeatUi, saveMenu, uiSourceOptions, windowMutator
  and prepareNativeQ2Seat are done (docs/application_layout.md, "Found by app-concrete3"). Follow-up (app-final):
  createQ3SeatClient, openGraphical and publishBorrowed are done; every application_*.o links with no undefined symbol;
  the real Application opened dedicated on retail Q1 e1m1, Q2 base1 and Q3 q3dm1 matches TS line for line and a graphical
  Q1 application runs on the offscreen GL driver (tests/app/test_application_open_real.c; "Found by app-final"). Still
  open: publishBorrowed and prepareVideoRestart have no application-level test (no borrowed ClientBootstrap fixture).
  Found while porting: console/commands.c frees a queued operation's context only when the operation runs, so an
  operation refused by a closed capture owner or a retired script writer keeps it (TS GC); a failed capture reaches
  ApplicationCapture as "Screenshot capture failed" (FrameCapture I/O errors carry no message), while a rejected frame
  read prints its own message as TS; the `path` diagnostic raises "Missing active mount" before printing any order,
  where TS prints the default order first when only a prefix order is broken; the camera `read` keeps mounted bytes
  as-is where TS TextDecoder replaces invalid UTF-8; `qa_frame_capture_*` abort on "No free screenshot filename" where
  TS rejects. The camera module still aborts where the TS queue prints "Camera: <message>". Under LeakSanitizer a CPU
  NativeRenderer window leaks 128 bytes inside the SDL/GL driver (pthread_once in qa_sdl_window_open; suppressed in
  build/agents/app-concrete-asan/lsan.supp). UBSan found `src/audio/streams.c` shifting a negative u8 PCM sample
  (`(u8 - 128) << 8`); it now multiplies by 256 (same value as the JS int32 shift).
- Render: the C SceneModelRenderer has no `modelShadowGroups` (TS stencil-volume and
  projection-shadow groups), so `cg_shadows` 2/3 emits no shadow volumes. `test_qvm_b`
  aborted with "Unknown Q3 native save part graph" (q3/runtime.c) during the Q3 native
  checkpoint writer work; recheck when the verifier finishes. Unassigned.
- SharedSimulation side by side: all 10 cases (q1, q2c, q2r, q3, qc, q2nc, q2nr, q3qvm, mixed,
  preset) match live TS with 0 diff lines, including checkpoint image and provider hashes
  (tests/app/test_sim_verify.c, oracle tests/app/oracles/sim_verify.ts). Open: the oracle's
  Q3 command literal was reordered to the C canonical key order; TS `input.ts` builds
  commands as {kind, serverTimeMilliseconds, weapon, sensitivity, ...}, so check at the
  connection step that a checkpoint taken after real input matches TS key order. qc under
  ASan (-O1) differs by one float32 ulp in a player origin from frame 41-50; -O2 matches.
  q2nr checkpoints stop on "Guest call stopped: budget" identically in TS and C. Leaks
  remaining: q2nr about 227 KB (text_add buffer, native server capture, finish_save,
  rerelease cvar registry, source_client_inventory), q3qvm about 4.6 KB, qc
  `q1_services_candidates`, cvar snapshot strings, Q2 save copy_text, `qa_q3_spawn_loadout`.
- TS drift D3, content/q1 (2026-11-03, d3-q1): every code/types-only/added/removed file under
  src/content/q1 is recorded `drift-done` in docs/port_review.tsv. Open:
  (1) composition/q1/runtime.ts gates the MG developer impulses behind `sv_gate_dev_impulses`
  with `isQ1AddonDeveloperImpulse`; the predicate is ported (`qa_q1ad_is_developer_impulse`,
  addons/commands.c) but the C composition impulse path does not call it yet (composition
  drift, no packet owns content/composition). (2) TS moved foreignQ3Pickup to
  composition/q1/q3-pickups.ts behind `game.foreignPickup`; C installs the hook from
  composition/q1/q3_pickups.c, but the Q3 definitions and their take kinds stay in
  foundation/pickups.c because the pickup definition is private there; moving them needs a
  public pickup type. (3) Q1EntityServices.find's name index (entityEpoch/targetnameEpoch)
  is a cache only; C keeps the linear scan with the same order. (4) TS tests of the packet
  not yet reproduced as C checks: tests/gameplay/q1-addons/monsters/ordinary/nightmare.test.ts,
  ordinary.test.ts (Shub zombie grenade, monster_slime, MG3 shared base), start-flags.test.ts
  (MG3 base pickups, MG3 cells/coop respawn), and the changed hunks of foundation/q1/
  foundation.test.ts, q1-addons/{common,ctf/ctf,items/items,monsters/heavy/source}.test.ts,
  q1-base/base.test.ts, q1-equipment/threewave-grapple.test.ts and q1-missionpacks/{arsenal,
  monsters/bosses/source,monsters/source,world/world}.test.ts (the 2026-11-02 sync tests
  test_q1_sync*.c cover part of them; not audited hunk by hunk).
- TS drift D5, content/q3 and bots (2026-11-03, d5-q3bots): every code/types-only/added/removed
  file under src/content/q3 and src/bots is recorded `drift-done` in docs/port_review.tsv. Open:
  (1) estimate-store.ts: the cross-runtime sharing of estimate tails and AAS caches (one store per
  edge list, keyed by the runtime's eligibility scope string) is not ported; C keeps one private
  LRU per runtime (estimates.c, estimate_aas.c). Estimates are a pure function of eligibility,
  so results match; only eligibility-callback counts and memory differ, which the first test of
  tests/bots/navigation/estimate-sharing.test.ts observes. Porting it needs refcounted (or
  deferred-free) cache values, because estimate_aas.c holds cache pointers across later
  remembers that may evict. manifest row status `partial`.
  (2) app/bootstrap (D1): prediction/presentation.c must pass
  `ladders: movement.ladders !== false` (qa_q3_pres_movement_options.ladders_disabled) into
  the prediction step options, which have no ladders member yet; simulation/q3/guest_bots.c
  can use the new exported `qa_aas_entity_info_empty_bots_a`; simulation/bot-assets.ts calls
  prepareLegacyProvenance (now `qa_bot_asset_files_prepare_legacy_provenance`) and adds
  "arsenal/provenance" with legacy bytes (`qa_bot_asset_files_add_with_legacy`); the
  version 1 bot-save restore path needs those calls. `qa_app_sim_bot_asset_provenance`
  duplicates `qa_bot_asset_files_provenance` and does not skip a path that is also recorded
  unread (TS `unread.has`).
  (3) D7: movement/q3/grapple.ts now owns Q3_GRAPPLE_SPEED and q3GrappleVelocity; C keeps
  `qa_q3_grapple_velocity` in content/q3/base/game/grapple.c (same behaviour). Map the new
  manifest row there or move it, but do not define the symbol twice.
  (4) D8: mission_hud.c carries a private `compress_script_text` mirroring
  content/legacy-script/memory.ts compressScriptText; switch to the D8 port once it exists.
  TS UiRuntime.create no longer takes commands/commandContext/context; mission_hud.c still
  sets them because qa_ui_runtime_options has them.
  (5) TS tests of the packet ported: tests/bots/behavior/asset-files.test.ts,
  tests/bots/navigation/aas-client-trace.test.ts, tests/bots/behavior/rerelease/replan.test.ts,
  the Q3 tests of tests/gameplay/q3/base/peer-input-regressions.test.ts
  (tests/bots/test_bots_d5_drift.c, tests/content/test_q3_d5_drift.c). Updated existing C
  expectations to current TS: test_bots_b (saturating epair, bun), test_library_core
  (projectile dump model offset 80), test_q3_base_ta (pmove_ladders "1", bun),
  test_q3_presentation_depth (compressed menu buffer, bun), test_q3_presentation (cvar table
  90/102, bun), test_q3_director_source (save version 2), test_q3_owner_draw frame
  registration (four cases re-derived from reading frame.ts/weapons.ts, not re-recorded under
  bun: re-record them). Not reproduced: ai-decision, library/botfile-errors,
  library/memory-view (no C counterpart of the cached DataView), rerelease/{frame-snapshot,
  replan-cost}, navigation/{aas-routing,estimate-sharing,q3-routing},
  gameplay/q3/base/train-leg, and the changed hunks of the code-changed tests under
  tests/bots/** and tests/gameplay/q3/** (mostly Biome formatting; not audited hunk by hunk).
  (6) Rerelease brain hazard memo is keyed by the navigation interface ctx (TS: the nodes
  array identity); equal when one interface owns one graph.

- D7 (network, render, world, movement TS drift, 2026-11-03) leftovers. Per-file notes are in docs/port_review.tsv
  (`d7-net`). (1) Model shadows: render/scene/models/renderer.ts `modelShadowGroups` (r_shadows 2 stencil volumes,
  r_shadows 3 RF_SHADOW_PLANE projection) and submissions.ts `SceneShadowGroup`/`shadowVolumeGroup` are not ported.
  stencil-shadow.ts (qa_make_stencil_shadow_volume) and shadowFinishQuad (qa_shadow_finish_quad) are ported and
  world.c already appends shadow-finish, but the C qa_scene_model_group has no shadow-volume kind, so app
  q3_client/scene.c `push_model_group` (D2) must learn it in the same change. (2) D2 call sites: app/bootstrap/network/q2.c
  passes `admin.rcon_guard = NULL` (a fresh guard per packet); TS q2.ts keeps one `rconGuard` per server.
  app/bootstrap/network/q3.c should set userinfo "ip" with qa_q3_peer_userinfo on userinfo updates (TS q3.ts:261).
  app/bootstrap/assets.c calls qa_scene_shader_registry_add_script per file; TS assets.ts:201 calls addScriptFiles once
  (qa_scene_shader_registry_add_script_files, id ScanAndLoadShaderFiles precedence). app renderer.c should call
  qa_scene_image_registry_invalidate_backend where TS renderer.ts:280/442 does. q2-downloads.c resume should use
  qa_download_sink_create_resumable/qa_download_sink_suspend (TS q2-downloads.ts:514/585). (3) D6: QC and QVM mod
  validation should use qa_mod_callback_inputs (world/session/mod_callback_inputs.c) as TS mod-validation.ts does.
  (4) D8: core/files contained-parent errors print strerror text; TS (Node) messages are "ENOTDIR: not a directory,
  open" (tests/network/test_d7_drift fails that one check); content/mounts aborts on "Ambiguous content path", which
  render textures now skip like TS (qa_try around the read) once mounts raise. materials/fog qa_prepare_fog_volume has
  no identityLight parameter (TS fog.ts:35), so WorldSceneOptions.q3IdentityLight is not ported (TS sets it nowhere).
  (5) Kept although TS removed them, because C tests use them: Q2ConnectionlessServer (tests/app/test_server_browser.c),
  q3EntityLifetime (tests/world/test_js_semantics_world.c), network/common/loopback.c (TS moved LoopbackHub to
  tests/support). (6) Perf-only TS changes not mirrored (same output): render PERF2 retainedMerge, PERF3 ancestor
  marks and merged-PVS cache, PoseLru pose caches, material snapshot cache, actor registry lowestFree, Q1 brush-cell
  and drawing-node caches, JS row-worker level/lighting-base registries (not applicable to the pthread pool).
  (7) movement/q2/classic.ts and rerelease.ts: the 2a2cb7bd closure-to-class restructure was not compared function by
  function; only the dead-mask change (index.ts) and test-name diffs were checked. Left for the gate read.
  (8) Not ported (need corpus maps or the TS harness): tests/world/collision/q2-source-rules.test.ts, the q2 dead-mask
  movement test, tests/movement/q3/ladders.test.ts, tests/render/{commands/world-translucent-order,
  commands/merge-retention,commands/models/model-shadows,visibility,image-journal,cpu/row-parallel-registry}.test.ts,
  the CPU stencil-pixel case of stencil-shadow.test.ts, tests/network/hardening/*, tests/network/remote-fault-containment,
  unified-event-codec-{generator,symmetry}, q2/mvd-mutation, and the code-changed network/render/world/movement tests
  beyond the cases named in tests/{network,render,world}/test_d7_drift.c (mostly Biome reformatting; not audited
  hunk by hunk). (9) test_sim_verify at 09:46: q2nr 0 mismatches; the other 9 cases differ only in the
  `checkpoint-image` payload hash (every per-part checkpoint line and all state lines match). d1-sim's own binary
  (09:48) gives the same 9; shared_simulation_checkpoint.c and save_image.c were being edited at the time.
- TS drift D1, app/bootstrap/simulation (2026-11-03, d1-sim): every code/types-only/added file under
  src/app/bootstrap/simulation has a row in docs/port_review.tsv (`drift-done` or `drift-partial` with what is left).
  Done: SourceStrategy as `qa_app_source_strategy` (src/app/bootstrap/simulation/source_strategy.{h,c}): one table
  per TS class (Loading, Loaded, Q1, QuakeC, Q2, Q2Native, Q3, Q3Qvm), built by inheritance-style constructors and
  replaced with the source (`qa_ss_set_source_strategy`, TS setSource). Ported members: typed access (asQ1 ...
  asQ2Native), userinfo/storeUserinfo/updateUserinfo/pause, the spawn section (admitClient, prepareBot,
  initialArsenalState, attachEntity, configureAdmitted, attachForeignCharacter, admitCarry, enterWorld,
  captureEntryCarry), q2Gibbed, q3Client, suppliesPunch, skipsTriggerTouch, admitsGuestMonsters, the command section
  (forwardsCommands, preCommand, afterCommand; members qa_ss_*_pre_command/after_command live beside their helpers in
  shared_simulation_players.c), hasGuestUi/guestUi (qa_ss_*_guest_ui in shared_simulation_arsenal.c), and the whole step
  section (lmctf ... watchesTransitions, stepRoute, settles, syncGravity, afterStep, finishFrame, concludeFrame,
  q1Powerups, q1CharacterFrame). SharedSimulation.step is split into the TS phases (beginStepBoundary,
  admitBoundaryInputs, applyBoundaryCommands, advanceBoundaryActors, closeStepBoundary, finishFrameState,
  concludeSourceFrame) in shared_simulation_step.c. No `sim->source.kind == QA_SS_SOURCE_*` comparison remains in
  shared_simulation_*.c: every per-source branch goes through `sim->strategy` (the remaining ones through as_*()).
  Open (D1, updated by D1b and D1c): (1) D1c (2026-11-03, d1c-sim) converted 123 more members; 204 of the 226 TS
  SourceStrategy members are now table members with each class's override (full list with status:
  docs/runtime_layout.md "SourceStrategy members"). `sim->strategy` is now a pointer to a per-class table built once
  (qa_app_create_source_strategy), so qa_app_shared_simulation's layout no longer changes when the table grows (a
  build mixing objects compiled before and after a table change read `stepping` at a shifted offset and failed
  "Simulation step is already running"). Still open: the 22 save/restore members (requireLoaded, assertSaveReady,
  reconstructsActor, checkpointProviders, checkpointGuests, checkpointExtras, nativeClients, sourceCvarSnapshots,
  expectedDeclaration, legacyCombat, requireInventoryProjection, inventoryCoverage, assertSavable, captureGuestSave,
  originalSave, restoreLoading, restoreGuest, monsterProxy, restoreContinuation, restoreFoundation, restoreGravity,
  ammoTimer) run as `sim->strategy->as_*()` branches in shared_simulation_checkpoint.c, shared_simulation_core.c and
  shared_simulation_worlds.c. The D1c step-3 sweep was not done: 35 `ss_source_is(` calls remain in
  shared_simulation_arsenal.c and ~170 `strategy->as_*()` calls across shared_simulation_*.c; each must be classified as
  a typed access TS also makes (`strategy.asQ1()` ...) or converted. Item (3) below (the pure TS extractions as their
  own C files) was not started by D1c. (2) Done by D1b: native Q3 / Linux qagame through the server game (q3/engine.h,
  qvm_engine.c, server_traps.c, native_guest_source.c; guest_runtime.c drives the engine; guest_artifact
  prepareQ3NativeGame; save.c native-guest checkpoints; content.c selectedQ3NativeGame on D8b's catalog
  native_programs). persistence/save_image.c (D8c area, forced) writes a native-guest checkpoint in
  Q3NativeGuestSource.checkpoint()'s key order (kind, version first; processorLayout spread order). The heap image,
  allocator, Windows heap/TLS/FLS tables and full checkpoint bytes match bun (tests/app/test_q3_native_engine.c vs
  tests/app/oracles/q3_native_engine.ts: authored PE, heap PE with an FLS slot, synthetic ELF; 0 mismatches). ~/qfiles has
  no native qagame, so there is no corpus side-by-side; the full Application `--vm-game 0` run of
  tests/persistence/q3-native-server.test.ts is not reproduced in C. (3) Extractions TS made without behavior change, kept
  as C copies: network-q2-guest-core.ts, qvm-component-state.ts, foreign-{match-host,match-rules,tag,lmctf,deathball,
  horde}-rules.ts, monster-admission.ts, diagnostics.ts (test-only view). (4) Bot assets (D5 handoff) done:
  qa_app_sim_load_application_bot_assets_with(content, simulation, legacy) prepares the legacy digest and adds
  "arsenal/provenance" with legacy bytes; application_bots.c (D2 file, call-site change only) passes
  `setup->restore != NULL` and `true` for guest bots as application.ts does. (5) TS tests: of the 19
  tests/first-playable/simulation and 2 prediction test files only physics.test.ts's three new cases are ported
  (tests/app/test_sim_c.c); the other diffs are harness changes (corpusTest/writeCapture) or need a full
  Application on corpus (qc-grants give no-throw, source-round-settlement start 400, q3-catchup, cross-family-q3,
  notarget-selected-monsters, monster-projectile-save, q3-round-restart, prediction/arsenal, prediction/q2-network).

- D2 (app/bootstrap except simulation, TS drift, 2026-11-03) leftovers. Per-file notes are in docs/port_review.tsv
  (`d2-app`). (1) application.ts aacb3b45: `step` must add `simulation.takeAdmissionSettleMilliseconds()` to `elapsed`
  before the pause check (application_step.c, `app->elapsed += frame_milliseconds`); SharedSimulation has no C
  `takeAdmissionSettleMilliseconds`/`admissionSettleMilliseconds` yet (D1). Done by D1b (shared_simulation_players.c,
  application_step.c). (2) Done by D1b: content.ts `selectedQ3NativeGame`
  (`--vm-game 0` selects a mod's qagamex86.dll / qagamei386.so as a native server-game module): the C catalog has no
  `expectation.nativePrograms`, and content.c's prepare step still raises "prepareQ3NativeGame has no C port" (D1/D6).
  The option is parsed (`has_vm_game`/`vm_game`) and the native Q3 module validation and assertRecipeInstalled are in.
  (3) Done: audio/music.ts `player.onFault` ("WARNING: music playback stopped: ...") is wired in
  src/app/bootstrap/audio/music.c to src/audio/music.c's on_fault (D8b hook, D8c wiring). (4) unified-event-codec.ts optional fields `local` (q2
  sound), `helpIcon` (Q2 player view) and `damagePowerupOwner` (attack provenance) are read/written on the save-value
  trees; `local` and `helpIcon` now also convert from/to the typed events (unified_event_values.c, both written only
  when true, as every TS producer sets them); `damagePowerupOwner` rides in simulation events (not a presentation
  payload). (5) Model shadows
  (r_shadows/cg_shadows 2|3): q3_client.c already passes `model_shadows`, but `modelShadowGroups` / the shadow-volume
  kind of qa_scene_model_group (render/scene/models/renderer.ts, world.ts stencil clear) are not ported (D7 area; D7
  added qa_make_stencil_shadow_volume). (6) Not checked (out of scope): AddonLibrary drift (catalog abort/20 s
  timeout, skipped rows, cancelQueue, purgeHidden), StartupMenu draw (menuPanel menuScale). (7) tests/app/
  test_startup_port does not link: src/app/bootstrap/startup_application_{graphics,launch}.c call
  qa_startup_open_graphics/qa_startup_graphics_draw, declared in startup_application_internal.h and defined nowhere
  (StartupMenu openGraphics/draw packet). (8) q3.ts `slice(0, 1023)` of client userinfo counts UTF-16 units; C cuts
  bytes (same for ASCII). (9) Done by D8c: value.c now gives bun's text, "checkpoint: JSON Parse error: Unexpected identifier
  \"not\"" (test_persistence_review; tests/app/test_unified_net.c still checks only the error name). (10) TS tests
  not reproduced in C: the tests/first-playable/application, network and remote-application drift files (66; all need
  a full Application or remote session on corpus, or test harness changes) and tests/app restored-recipe-gate,
  load-path-policy, q2-native-hud-prepare, q3-first-admission, q3-tinfo, addon-library-purge, guest-monsters-entity-text,
  native-pmove-contacts, selected-q3-presentation-collection, prepared-startup, demo-recording (unlink path),
  unified-host, q3-reports (sv_rankingsGameID). New C checks: tests/app/test_d2_drift.c (oracle
  tests/app/oracles/d2_drift_oracle.ts); the unified frame corpus was regenerated for version 10.
