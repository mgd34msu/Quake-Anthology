# Presentation audit corrections

This appendix follows the frozen `presentation.md` packet, SHA-256 `79b4f6ae8f7a8aed3af6a776f1bcbf919984e90cd84498269d61db1b47a00be9`. The original packet and its judgments describe the source before the corrections below. No project build, configuration, compiler, test, executable, generator, or benchmark was run.

## Actual Jev judgments

The coordinator's supplemental `judge-task.mjs` submitted the original graph task and criterion with the complete frozen packet to `jev-1.13.0`. Each result preserves the graph/evidence hashes, task, time, questions, model, and answer in `Bxx-presentation-acceptance.json`. Every call returned exit 2.

| Task | Implementation disposition | Criterion score |
|---|---|---|
| B15 | incomplete | 0.16 |
| B16 | incomplete | 0.20 |
| B17 | insufficient_evidence | 0.43 |
| B18 | incomplete | 0.04 |
| B19 | incomplete | 0.18 |
| B20 | incomplete | 0.14 |
| B21 | incomplete | 0.38 |
| B34 | incomplete | 0.02 |

The plugin's separate `check report --project quake-anthology --task Bxx -` calls read task-specific progress paragraphs. Their exact output is preserved in `Bxx-presentation-report.txt`. All returned exit 2: B15–B21 had `EXCUSE` stop-reason flags; B34 had a `REVIEW blocked-external` flag. Overclaim scores ranged from 0.05 to 0.17. These checks judge the reports and do not establish implementation acceptance. Work is continuing on the authorized defect; the flags are not treated as permission to stop or as project passes.

## P-02 mesh lifetime correction

Status: implemented and reviewed by its author in source; awaiting independent coordinator review. B18 as a whole remains open.

The correction adds one opaque `qa_scene_geometry` owner for each immutable vertex/index allocation. `qa_scene_geometry_adopt` in `src/render/scene/resources.c` takes malloc-compatible arrays only after its record allocation succeeds. It does not copy geometry. World surface admission and model topology creation attach that owner to the existing `qa_scene_mesh`; their teardown releases it. Incomplete producer construction still frees arrays that were never adopted. Q3 patch installation adopts the replacement first, then releases the previous version, and preserves the mesh identity with its incremented revision.

Frames retain geometry through `qa_scene_frame_geometry` in `src/render/scene/frame.c`, including identity-zero draws that borrow retained indices. The pin array is separate from command storage because material/world rollback and model shadow extraction rewind commands. Pins remain until reset, like frame arena allocations; reset releases them even if the commands were removed. Adjacent multipass pins coalesce in constant time, and other pins append to reused capacity rather than scanning the entire frame. World shadow-caster construction also pins retained geometry before returning its intermediate frame data. Model shadow extraction already pins it through emitted commands. Scene command sorting copies values without duplicating or dropping pins.

GL cache entries hold separate residency references. These retain the retirement record without keeping CPU geometry active. When the last producer/frame reference releases a version, the arrays are freed; each renderer's next `qa_gl_execute` deletes that version's VBO/IBO pair through `gl_meshes_prune`. This prevents two contexts from keeping each other's abandoned geometry alive. Normal GL destruction/restart releases residency records alongside buffers; failed make-current destruction releases records without attempting invalid-context GL calls. Context destruction remains responsible for its native objects in that failure case.

The identity-zero streaming buffer path is unchanged. Retained entries remain keyed by identity/revision and now also check geometry-owner agreement when validating reused storage. New retained uploads acquire their residency reference only after GL allocation/upload succeeds, so their existing failure cleanup owns no extra reference. Old patch versions can coexist while earlier frames retain them. No map-global purge is needed, so shared live resources remain reusable across maps and contexts.

Source review covered producer failure cleanup, queued frame copies, command rollback, shadow extraction, patch replacement, GL upload failure, restart, make-current failure, and multiple-context retirement. Geometry reference counters are atomic. The public contract still requires synchronized publication and prohibits reset while any backend consumes a frame; this change does not claim the entire mutable scene/image system is thread-safe. Retain requires an existing active reference and cannot revive a cache-only retired record. Owning frame structs must not be shallow-copied.

Files changed for P-02: `include/qa/scene.h`; `src/render/scene/{resources,frame,world}.c`; `src/render/scene/models/topology.c`; `src/render/scene/world/q3/patch.c`; and `src/render/gl/{internal.h,resources.c,renderer.c}`. No new source registration is required. `git diff --check` over these paths reported no whitespace errors. That is a source check, not compilation or behavior qualification.

## Other corrections and remaining work

The coordinator added `tests/vfs_test.c` to CMake as `vfs_test`, linked it with `qa_content` and `ZLIB::ZLIB`, and registered the `vfs` test. Independent source review checked those definitions against the test's VFS/binary/Zlib dependencies. The frozen packet's missing-registration observation is resolved in source; no test result is implied.

P-01 is unchanged: the executable still lacks the complete application composition, and configuration integration remains baseline work. The other presentation tasks have no new acceptance judgment from the P-02 change. B26 bot implementation remains preserved and unfinished while this audit/fix assignment is handled.
