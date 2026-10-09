# Account-switch pause, 2026-10-08

Resume the active issue **THE-2859**, then **THE-2876 / THE-2861**. The owner explicitly requested this pause. Do not resume agents or launch games until the owner resumes work.

## Git and the installed build

- Verified `main`: `e81f359f0c2adcdf2487b61aecc7c74702de7804`, pushed to origin. Its frozen production and ASan builds succeeded; all seven core CTest checks passed in both. Evidence: [main-build logs](2026-10-08-account-switch/evidence/main-build/ctest.log) and the adjacent ASan log/source attestation.
- Remaining source is preserved on `wip/the-2859-2026-10-08`. The checkpoint commit is explicitly **unverified as a combined tree**. Do not merge it wholesale or install it. Run `git rev-parse HEAD` on that branch for its checkpoint ID; the Linear and Slack pause notes contain the remote ID.
- Installed `qfiles/qa-c`: `a36aa7012486859974de7f827b41e315909d2a0b`, built October 8 at 22:14:03 CDT, installed about 22:27. [Installation receipt](2026-10-08-account-switch/evidence/installed/installed-build-verified.json).
- Commits `85bfdae1`, `c6b9ce13` and `e81f359f` are verified and pushed but deliberately **not installed**, under the owner's changed install cadence.
- The owner profile is `/home/buzzkill/.local/share/quake-anthology/content`, 42 files including the interrupted recovery journal. Never modify it during checks. Every future install requires a fresh complete private copy and `tools/install_qualified_build.py`.

## Exact next step

1. Start from verified main and restore only the pending THE-2859 files from the WIP branch into a new implementation slice. The paths below are independent of the older save/frame/geometry drafts. Root owns SDK changes, builds and installs.
2. Repair the shared Q2 controls component fixture extraction; its current `checks.json` records a compiler failure, not a behavior pass. Verify that the one shared sampler preserves both received legacy Q2 and Unified Q2 behavior. Finish the remaining fixed read at `src/app/application/network_q2_frame.c:664` using the same cold handle policy.
3. Build production and ASan from frozen inputs, run the seven core checks, and commit/push this Q2 slice. No intermediate installation. Component allocation checks prove only their measured functions; the full frame allocation gate **does not yet exist** (THE-2874).
4. Refresh the private diagnostic from the committed source. Count 600 ordinary warm-up frames and 600 sampled frames in Q1, Q2 rerelease and Q3, then inspect all remaining fixed internal name lookups. The old Q3 counter run hit its deadline and produced no count: rerun it with an admitted actual drawable/workload, not an assumed command-line resolution.
5. Measure pinned production workloads before/after, without a debugger or diagnostic counter. Use cores `0-7,12-19`. Admit actual drawable/caps/map/camera through separate public `gfxinfo`, `frameinfo` and cvar queries. [Measurement recommendation](2026-10-08-account-switch/evidence/measurement-plan/qualification.md): identical original-protocol timedemo bytes give a controlled client-frame comparison; native idle-map timings are observational and do not guarantee identical simulation ticks or RNG state.
6. Only when the full THE-2859 slice is finished, run the copied-owner-profile private qualification, install once, post the build note and move proved issues to In Review. Never mark Done. Then implement THE-2876 / THE-2861.

## Pending THE-2859 source

These twelve files are the first slice to recover from the WIP branch:

- `src/app/application/internal.h`, `native_q2_console.c`, `providers.c`, `visuals.c`.
- `src/app/frontend/remote_q2_client.c`, `remote_q2_effects.c`, `remote_q2_effects.h`, `remote_q2_effects_bridge.c`, `remote_q2_private.h`, `remote_unified.c`, `remote_unified_private.h`, `remote_unified_q2.c`.

**Q2 weapon controls:** four handles bound at real construction/restore points. Frozen packet `/tmp/qa-the2859-q2-weapon-tail-20261009`; [committed report](2026-10-08-account-switch/evidence/q2-weapon/report.md). Worker component before/after GCC/Clang and sanitizer streams match; 4,800 warm name reads become zero, with zero sampled heap/resolves. Full-tree build, install, live module and timing proof remain pending.

**Q2 visibility:** cold `sv_novis` bind plus one typed read, with the coordinator's provider field. Frozen packet `/tmp/qa-the2859-visibility-tail-20261009`; [report](2026-10-08-account-switch/evidence/visibility/report.md). Actual retail PVS component culls 26 of 31 actors by default; rerelease override admits all 31, and classic behavior remains unchanged. Before/after traces match across GCC/Clang/sanitizers; 2,400 name reads become zero, sampled heap/resolves zero. No full-tree/live/timing claim. The separate `network_q2_frame.c:664` read is still untouched.

**Shared Q2 effect controls:** one 13-handle sampler replaces duplicated rail/color/control logic in received legacy and Unified paths; constructors bind their actual cvar view. Root glue is included. Packet `/tmp/qa-the2859-q2-controls-20261009`; [partial checks](2026-10-08-account-switch/evidence/q2-controls-incomplete/checks.json). Strict syntax evidence exists, but the generated before-controls fixture currently fails to compile. Preserve this as unfinished. The existing non-reflected `sample_entities` allocation in `remote_unified_q2.c` is still outside the component heap proof.

## What is finished on main

THE-2859 includes typed handles and revision-cached projection, plus input/UI, clocks, renderer/resource, CGAME, legacy view, HUD, audio/pacing, particles, movie/sky and received-view migrations. Latest `e81f359f` cold-binds administration, capture, footsteps and bot controls. Component evidence lives under `/tmp/qa-the2859-{admin-tail,capture-clock-tail,q2-tail,bot-tail}-20261009`.

The diagnostic at `c6b9ce13` counted Q1 classic 7 fixed name reads/frame and Q2 rerelease 51.955/frame. Main e81 and the pending Q2 work target those identified callers, but **there is no final zero-name or pinned speedup proof yet**. Raw cases: `/tmp/qa-private-av-p0j_792b` (Q1), `/tmp/qa-private-av-apy5c6j9` (Q2RR), `/tmp/qa-private-av-vm3nur58` (Q3 deadline, unusable). Resolved callers: `/tmp/qa-the2859-frame-counter-current-20261009`.

THE-2892 (`c6b9ce13`) fixes startup queued commands selecting a GAME snapshot for a live CLIENT owner. Production proof: `/tmp/qa-the2892-startup-live-20261009/results.json`, ten copied-profile controls across five games. It is not installed yet. Its sixteen-case installation matrix was cancelled by the owner's cadence change; `/tmp/qa-the2892-startup-wayland-20261009/qualification-partial.json` is explicitly partial, never an install receipt.

MIKE-39 / THE-2881 tuple retention, startup repeat and pointer fixes are committed and installed, In Review. Packet `/tmp/qa-the2881-install-scope-final-wayland-20261008`, pointer policy `/tmp/qa-the2881-pointer-policy-20261008`, and `docs/playtests/2026-10-08-mike39.md`. The final installed profile checks are startup/render/normal quit evidence; dummy audio is not sound proof. Known autosave warnings remain.

## Older unfinished work preserved separately

[Exact pending path inventory](2026-10-08-account-switch/pending-paths.txt) includes every pre-pause tracked change and the two new primitive-checker tools.

- **THE-865:** `frontend/frame.c` fixed host phases, protected draft. `/tmp/qa-the865-host-phases-20261009` has component GCC/Clang/sanitizer comparisons. Full slice adoption/build/live proof remains pending.
- **THE-873 visibility/load draft:** `world/collision/geometry.c` and `tests/core_test.c`; `/tmp/qa-the873-visibility-load-20261008/results.json`. Keep its bounded component allocation results separate from whole-frame claims; the worst Q2 load-memory tradeoff is still unresolved.
- **Save/recovery drafts:** CMake SDL persistence linkage; demo fixed writer queue; original Q2 rerelease gzip PACK reader and slot metadata/listing; autosave API removal and recovery-test changes. Paths are grouped in the inventory. Do not infer completion from these changes or combine them with cvar commits.
- **Q1 wire/source drafts:** public Q1 network/clientdata optional-presence signature plus native wire and NQ consumer updates.
- **HUD/player-state drafts:** equipment adapter, Q3 factory and Q3 presentation player-state changes.
- **Primitive ownership checker:** `tools/check_primitive_rules.py`, `tools/verify_primitive_rules.py`. This is a structural checker, not the missing runtime allocation gate.
- Entity-store plan only: `/tmp/qa-the2876-entity-store-plan-20261009`. Shared actor registry across accepted/candidate prediction worlds must not alias authoritative rollback body state; remove the old path in the same actual migration.

No new source slice was promoted to main during the pause. No new game/build/test was launched for it. The two-line generated GoodVibes telemetry was preserved outside Git at `/tmp/qa-account-switch-20261008/generated-telemetry.jsonl` and removed from the working tree; it is not engine code.

## Processes, operational paths and remaining work

Workers were told to freeze and stop; the active workers were interrupted. [PID/start-token audit](2026-10-08-account-switch/evidence/process-cleanup/process-audit.json) inspected 13,883 recorded tokens and found no matching live process. Reused PIDs were not signalled. Do not stop the owner's desktop, other agents, MCP/app-server infrastructure or processes by name.

- SDK: `/home/buzzkill/.cache/quake-anthology-recovery/runtime-checkout`, build subdirectory `build`. Last frozen SDK tree matches e81. ASan build `/tmp/qa-core-asan-build-20261008`.
- Retained current production `/tmp/qa-the2859-cold-tail-production-20261009`; installed production `/tmp/qa-the2859-hud-production-20261009`; pre-handle timing baseline `/tmp/qa-the2881-install-scope-production-20261008`.
- Frozen-build/retain operational scripts are copied into `2026-10-08-account-switch/operations/`. Evidence is indexed in `evidence-index.json`; larger original fixtures/screenshots remain at their listed local paths.
- [Live Linear owner-item snapshot](2026-10-08-account-switch/open-owner-items.json) records all current In Progress/In Review M0 rows plus THE-2881. Core architecture remains the owner's active priority; these owner items are not silently closed. In particular THE-420 stock Q3 rebound must not be changed without the owner's decision.

After THE-2859 and THE-2876/2861: THE-2873 read-only geometry/caller scratch, THE-2874 load-only arenas and allocation gate, THE-2871 one jobs dispatcher, one rule-set-id type, interned snapshot names, and THE-2864 physical intake without dropping releases. Continue the architecture issue order afterward. BSP probe/BSP30/IBSP47 bounded format work is already separate; MDC/IQM and music format work remain future slices. Preserve all legacy protocol fields and widths.

Rules: one implementation per shared capability; no SHA/content fingerprints; no new valid-state failure wrappers; private AV only; installs only after a full slice or owner-retest fix; commit/push small verified steps, with issue IDs; Linear is source of truth, never Done; Slack build notes only for qualified installs, except this explicitly requested pause note.
