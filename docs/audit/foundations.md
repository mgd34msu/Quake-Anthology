# Foundations retrospective audit

## Scope and snapshot

This is a source-only retrospective against the goals and acceptance criteria for B01-B07 and B15 in `docs/dependencies.json`. It does not replace P01 compilation, sanitizer, fixture, campaign, or interoperability validation. Per the baseline execution gate, no project build, test, compiler, parser, generator, executable, sanitizer, or benchmark was run during this review.

The review began at Git `c6f7db5c4715416393d63af7e5c994ba59194caf` and closed against Git `fd23b0bce3808dd9e66554ee86be7d2dc49eb772`. `git diff --name-status` between those revisions was empty for every tracked path in this audit, so the later revision identifies the reviewed tracked snapshot. Two reviewed B06 files had live working-tree edits, and all B15 implementation files were untracked. Their exact SHA-256 snapshot is recorded below. Concurrent changes outside this lane are not evidence for any verdict here.

| Task | Source-review result | Reason |
|---|---|---|
| B01 | Supported, P01 pending | The C17 targets, checked data/file primitives, ownership APIs, and a native diagnostic entry exist with no Bun dependency. The complete playable executable belongs to B34. |
| B02 | Supported, P01 pending | Ordered PAK and ZIP-family parsing, normalized lookup, shared stored bytes, owned inflate output, bounds checks, and CRC checks are present. |
| B03 | Held | The shared resource model is present, but `src/content/vfs.c` is Linux/POSIX-specific while `qa_content` is unconditional, and the existing VFS test is not registered. |
| B04 | Held | All declared BSP families are read, but full validation omits Q1 model hull 3. |
| B05 | Supported, P01 pending | The required image/model families and retained metadata, animation, replacement, LOD, and attachment facilities are represented. No record-dropping defect was found in this source pass. |
| B06 | Supported by source, P01 pending | One actor registry/world/collision graph owns bodies and spatial publication with explicit reuse and ordering contracts. There is no C test target for this subsystem. |
| B07 | Held | The session never dispatches its scheduler at any think boundary, so scheduled gameplay continuations remain pending. Its final tie rule also diverges from the donor invocation sequence contract. |
| B15 | Held | The catalog/configuration implementation is only an untracked overlay: it is absent from all build targets, tests, and application callers. Discovery is also POSIX-only. |

## Complete reviewed file manifest

Every file below was read in full or by full function/record inventory followed by inspection of every implementation block relevant to its task criterion. There are **no unreviewed files inside the assigned foundations lane**. Files elsewhere in the repository, and the donor/original trees as a whole, were outside this audit and were not sampled as if they had full coverage.

### B01: build, native entry, core, script, files, and memory

```text
CMakeLists.txt
cmake/Native.cmake
src/main.c
include/qa/common.h
include/qa/arena.h
include/qa/binary.h
include/qa/hash.h
include/qa/json.h
include/qa/json_writer.h
include/qa/math.h
include/qa/strings.h
include/qa/text.h
include/qa/script.h
src/core/arena.c
src/core/binary.c
src/core/common.c
src/core/hash.c
src/core/json.c
src/core/json_writer.c
src/core/normals.c
src/core/number.c
src/core/strings.c
src/core/text.c
src/core/script/checkpoint.c
src/core/script/defines.c
src/core/script/directives.c
src/core/script/expansion.c
src/core/script/expression.c
src/core/script/internal.h
src/core/script/lexer.c
src/core/script/number.c
src/core/script/quoted.c
src/core/script/read.c
src/core/script/source.c
src/platform/file.c
src/platform/file_windows.c
src/platform/mapping.c
tests/core_test.c
```

### B02-B05: archives, mounted content, BSP, image, and model formats

```text
include/qa/archive.h
src/content/archive.c
tests/archive_test.c
include/qa/vfs.h
src/content/vfs.c
tests/vfs_test.c
include/qa/bsp.h
src/formats/bsp.c
tests/bsp_test.c
include/qa/image.h
src/formats/image/gif.c
src/formats/image/image.c
src/formats/image/indexed.c
src/formats/image/internal.h
src/formats/image/jpeg.c
src/formats/image/png.c
src/formats/image/raster.c
src/formats/image/wad.c
tests/image_test.c
include/qa/model.h
src/formats/model/alias.c
src/formats/model/internal.h
src/formats/model/lod.c
src/formats/model/md3.c
src/formats/model/md4.c
src/formats/model/md5.c
src/formats/model/metadata.c
src/formats/model/model.c
src/formats/model/scales.c
src/formats/model/text.c
src/formats/model/transform.c
tests/model_test.c
```

### B06-B07: shared world and session authority

```text
include/qa/actors.h
include/qa/collision.h
include/qa/world.h
src/world/actors.c
src/world/body.c
src/world/spatial.c
src/world/collision/contents.c
src/world/collision/geometry.c
src/world/collision/internal.h
src/world/collision/q1.c
src/world/collision/q1/geometry.c
src/world/collision/q1/geometry.h
src/world/collision/q2.c
src/world/collision/q3.c
src/world/collision/q3/model.c
src/world/collision/q3/patch.c
src/world/collision/q3/patch.h
src/world/collision/q3/shared.h
src/world/collision/world.c
src/world/collision/world_internal.h
include/qa/scheduler.h
include/qa/session.h
src/session/scheduler.c
src/session/session.c
```

No B06 or B07 C test file exists in `tests/`.

### B15: product catalog and atomic configuration overlay

```text
include/qa/catalog.h
include/qa/launch.h
src/content/catalog/behaviors.c
src/content/catalog/catalog.c
src/content/catalog/discovery.c
src/content/catalog/internal.h
src/content/catalog/metadata.c
src/content/catalog/mounts.c
src/content/catalog/products.c
src/session/configuration/draft.c
src/session/configuration/internal.h
src/session/configuration/presets.c
src/session/configuration/transaction.c
src/session/configuration/validate.c
```

No B15 C test file exists in `tests/`. The B15 files above were untracked at the snapshot and therefore cannot be named by the Git revision alone.

### Live-overlay hashes

```text
9ee74761f76008bd0a168fba750a169bcb9a5a2ac909d5745386cc53c1fc68a9  include/qa/collision.h
25ccb26cec40188fe9087382ffd8d10b0f0fbdbd299afd8a40cb85b93bd4fb18  src/world/collision/internal.h
a97b889c76b674aa5b2605bcde3fea00e1a8590722a181c89b5fbc5c77b056c7  include/qa/catalog.h
d833a31f16e242a8f95d6eedd9a532f3453f546ee221decb14c7e40dcf79de55  include/qa/launch.h
9c84a98c10663a6d6c82465f2a4c2e14d2e679158d6a7773eeac839566dffe30  src/content/catalog/behaviors.c
2f7195238975212ec1816afdda2fa28c2e7240652701e068ef416a9d3102870d  src/content/catalog/catalog.c
afb19649af9738356952afa18d12b09f5711217c890a920499701ddb07563760  src/content/catalog/discovery.c
cda429a01bd0136098580996a4cc501e9479f944a0ddb403b821bcb90d91712b  src/content/catalog/internal.h
e3069d0d92c8ea9970a8248c8de82dba6a7b333a50ad026798f41d369c2ef1c7  src/content/catalog/metadata.c
4bf14bb57b1c74046fc3ba352c9697d4570189b1a45c69b14e2638459af9504c  src/content/catalog/mounts.c
e750d9e629cda826bd7d9303aa1cc54234fbf027f4bc70e66ba83db1036fac19  src/content/catalog/products.c
684dac43eaccfe603a3119c9900dae39825169ec2334c15da1d335d8c90b61a8  src/session/configuration/draft.c
ac1a2193da70f48bd4fc449b8e37073586d9e8a6a8f1948d20b9b853619a6c6e  src/session/configuration/internal.h
3133822fb4863f83968736585a239180c0e12b6c816d498265036efe21cdbfae  src/session/configuration/presets.c
182dd27e4ff5e0d921a1b1b21b6362d3011818d56a7445f5db62ed1aa6cf4712  src/session/configuration/transaction.c
b9bc8e98a29138b31be4e0d5ace237b7df0afe289082a21eb661a9a65a76e849  src/session/configuration/validate.c
```

The B06 overlay publishes `qa_collision_trace_body` in `include/qa/collision.h:78-82` and removes its duplicate private declaration. This audit found no ownership regression in that bounded declaration move.

## Criterion evidence and findings

### B01

`CMakeLists.txt:1-26` selects strict C17 and common warning/sanitizer flags. `cmake/Native.cmake:6-21` builds the portable `qa_data` layer from checked binary, number, JSON, hash, mapping, and platform-selected file implementations. `CMakeLists.txt:47-68` builds the retained core and script libraries. The public contracts centralize explicit error output and owned/borrowed byte buffers in `include/qa/common.h`, checked reads in `include/qa/binary.h`, bounded arena lifetime in `include/qa/arena.h`, and retained string/script state in `include/qa/strings.h` and `include/qa/script.h`.

`src/main.c:7-14,90-107` is a native C entry that reports version/help and inspects archive/BSP data. `CMakeLists.txt:559-561` links that diagnostic entry to `qa_content`; it is not the complete playable application, which is correctly a B34 requirement. A repository search found no Bun import, command, or runtime call in these C/CMake paths.

`tests/core_test.c:23-224` covers errors/buffers, checked binary reads, spans, arena behavior, and file access. It does not exercise hash, JSON, strings, UTF-8, script preprocessing, or mapping. That is a P01/coverage obligation, not source evidence that the existing implementations are absent.

### B02

`include/qa/archive.h:42-47,65-81` explicitly defines archive backing and extraction ownership: memory opens borrow immutable bytes, file opens own them, stored entries borrow the archive, and deflated entries own their output. `src/content/archive.c:25-76` bounds ranges and normalizes names while rejecting traversal. PAK parsing preserves directory order; ZIP-family parsing validates central/local records, rejects encryption/multidisk/ZIP64 it cannot safely interpret, and accepts stored or raw-deflated entries. `src/content/archive.c:428-455` preserves duplicate search order, `470-510` performs bounded inflate, and `513-541` verifies output size and CRC.

`tests/archive_test.c:183-446` covers PAK order, case profiles, ZIP store/deflate, malformed records, CRC failure, and file-backing ownership. Optional command-line retail archives are outside the source-only run performed here. No criterion-level source defect was found.

### B03

`include/qa/vfs.h:13-25,42-83,96-162` defines one retained resource pool, ordered loose/archive mounts, immutable resource acquisition, writable user overlays, restrictions, links, and resumable file state. `src/content/vfs.c:339-449` creates/clones/releases views over the shared pool; `623-692` mounts archives/directories and exposes content identities; `922-1053` applies default/prefix order, overlays, and links; `1276-1502` resolves and shares retained resources; and `1709-1988` implements bounded writes and continuation state. Distinct paths and mount identities remain distinct rather than being merged across games.

**FND-03, high:** this service is not cross-platform at its current build boundary. `src/content/vfs.c:1-18` unconditionally includes Linux/POSIX headers, while its loose read/write code uses `/proc/self/fd`, `SYS_openat2`, `openat`, `O_NOFOLLOW`, and related APIs at `479-495`, `1083-1138`, and `1668-1988`. `qa_content` includes `src/content/vfs.c` unconditionally at `CMakeLists.txt:70-92`; selecting `src/platform/file_windows.c` for `qa_data` does not make VFS compilable on Windows. Split the OS operations behind a platform implementation, with equivalent root containment, symlink/reparse-point policy, atomic replacement, and resumable file semantics.

**FND-06a, medium:** `tests/vfs_test.c:117-269` covers shared cache identity, PAK-first/ZIP-last duplicate behavior, order/prefix rules, case behavior, path/symlink rejection, immutable versions, write/remove, archive mutation, and resource lifetime, but `CMakeLists.txt:563-584` never creates or registers `vfs_test`. It also lacks cases for listing, restrictions, links, clone isolation, and resumable writes.

### B04

`include/qa/bsp.h:7-11` names BSP29, BSP2, 2PSB, Quake64, IBSP38, QBSP, IBSP44, and IBSP46. `src/formats/bsp.c:201-260` selects those layouts, validates all lump spans/strides, preserves Q2 entity-clamp diagnostics, validates visibility, and discovers BSPX. The typed record readers cover entities, planes, nodes/leaves, models, faces/surfaces, brushes, visibility, textures, lighting, BSPX metadata, IBSP44 derived materials/model membership, and Q3 lightgrid data. `qa_bsp_validate` performs cross-record reference checks rather than only header validation.

**FND-04, high:** Q1 model records read all four headnodes at `src/formats/bsp.c:540-553`, matching the donor record shape in `../quake-typescript/src/formats/q1-map/records.ts:137-149`. Full validation only checks clip hulls 1 and 2 because `src/formats/bsp.c:1613` uses `hull < 3`. A nonnegative, out-of-range `headnodes[3]` therefore survives `qa_bsp_validate`. Validate indices 1 through 3 and add a malformed fourth-headnode case to `tests/bsp_test.c`.

### B05

`include/qa/image.h:6-157` exposes PCX/QPIC, TGA, BMP, PNG, JPEG, GIF, WAD2/WAD3, MIP/WAL, QLIT, colormap/palette translation, mip generation, resampling, gamma, and screenshot encoders. The image source files retain format-specific policies rather than forcing Q3 quirks onto common decoding. `include/qa/model.h:6-280` and `src/formats/model/*` cover MDL, MD2, MD3, MD4, MD5 mesh/animation, SPR/SP2, timed frame/skin groups, tags, skeletal poses, Q3 player metadata, LOD selection, replacement paths/skin selection, and attachment transforms.

The donor comparison included the complete file inventory under `src/formats/images`, `src/formats/q12-model`, and `src/formats/q3-model`, with API/format checks against their exported decoders, encoders, animation, replacement, LOD, and attachment operations. The C surface represents each required format family and retained record class. `tests/image_test.c:396-405` and `tests/model_test.c:333-430` exercise the main synthetic format groups and truncation paths, but do not cover every metadata, replacement, LOD, transform, progressive-image, or retail-file case. No supported-format record drop was established by source review; P01 must supply runtime and retail coverage.

### B06

`include/qa/actors.h:10-110` defines generational IDs, explicit owner/source-slot records, release/reuse rules, checkpoints, saved references, and source rebinding. `src/world/actors.c:102-322` implements allocation, source uniqueness, invalidation-before-callback, generation reuse, and stable iteration; `324-485` implements checkpoint/restore identity history and rebinding.

`include/qa/world.h:50-108` makes the ownership relationship explicit: the session owns the single actor registry and collision geometry, while one world borrows them and owns authoritative bodies, bindings, attachments, link snapshots, collision publication, and spatial traversal. `src/world/body.c:46-133` enforces that lifetime and release cascade. `src/world/spatial.c:8-127` provides the retained four-level broadphase and family-specific insertion order, while `129-240` refreshes authoritative body state and preserves Q1 live trigger traversal versus Q2/Q3 snapshot traversal under nested mutation. `src/world/collision/world.c:68-160` traces the common geometry plus actor bodies and keeps strict earlier-winner tie behavior.

No separate family world, actor registry, body store, or spatial replica appears in this lane. No B06 C test exists, so allocation/reuse, nested touch mutation, ordering, link restoration, portal state, and tie behavior remain unexecuted P01 obligations.

### B07

`include/qa/session.h:66-127` assigns the session one actor registry, scheduler, retained strings, ordered component clocks, nested invocation stack, release hooks, and world publication lifecycle. `src/session/session.c:236-369` handles partial creation cleanup, component registration/removal, and clock restore; `371-489` owns actor allocation, execution-provider binding, scheduling admission, nested invocation, and actor turns; `616-663` clears old actors/scheduling before world publication and destroys components in reverse registration order.

**FND-01, critical:** `qa_session_advance` at `src/session/session.c:521-614` advances provider clocks and invokes `begin_frame`, `actor_frames`, and `end_frame`, but never calls `qa_scheduler_advance` or `qa_scheduler_run` at `QA_THINK_BEFORE_PHYSICS`, `QA_THINK_DURING_PHYSICS`, or `QA_THINK_AFTER_PHYSICS`. Repository-wide references to those functions are only their definitions and the scheduler's internal call. This is live, not hypothetical: `src/gameplay/q1/runtime.c:466-484` schedules a Q1 continuation through `qa_session_schedule`. It remains pending forever under the session's only frame driver. Integrate scheduler dispatch at the source-defined boundaries, using the exact active provider frames for a deadline, preserving nested callback/fault ordering and actor liveness.

**FND-05, medium:** `qa_think.sequence` is public at `include/qa/scheduler.h:36-45`, and the donor uses invocation sequence as the last selection key at `../quake-typescript/src/world/scheduler.ts:25-35,182-186`. The C comparator at `src/session/scheduler.c:55-62` ignores it and substitutes provider registration/host-slot order. Unique live source slots mask many ties, but native ordering across owners sharing a source slot can differ from the declared donor order. Either restore sequence as the final native tie key with explicit validation, or remove the field and document/prove the intentionally changed invariant against all callers and checkpoint formats.

There is no C session/scheduler test. The missing dispatch would have been exposed by a single scheduled-think-through-`qa_session_advance` case; nested reschedule, actor release/reuse, mixed provider clocks, bounded debt, callback failure, and world replacement also need direct coverage.

### B15

`include/qa/catalog.h:11-129` models editions, products, mounts, maps, starts/episodes, mods, and behavior metadata. `src/content/catalog/products.c:16-44` carries Q1 shareware/classic/mission packs/CTF/rerelease/DOPA/MG1/MG3/QW/Q64, Q2 base/xatrix/rogue/CTF/LMCTF/rerelease/MG2/N64, and Q3 base/missionpack/demo rows, matching the donor inventory in `../quake-typescript/src/content/catalog/products.ts:19-45`. `src/content/catalog/discovery.c:181-392,459-567` discovers native package order, mods, QuakeWorld variants, KPF content, external programs, maps, and metadata.

`include/qa/launch.h:12-118` represents independent provider selection across world/player/scoped roles, mods, modes, equipment, seats, loadouts, monsters, and behavior sources. `src/session/configuration/presets.c:17-80` supplies family defaults; `validate.c:84-173` validates product/provider/binding and independent feature choices. `transaction.c:203-291,328-408` prepares or reuses private instances and mount resources, then validates and publishes through a prepare/rollback/infallible-publish contract.

**FND-02, critical:** every B15 implementation/header listed above is untracked. None appears in `CMakeLists.txt`; no compiled target owns it, no test exercises it, and `src/main.c` has no catalog/configuration caller. The implementation therefore cannot satisfy “configuration resolves” yet, despite a substantial API and source body. Integrate it into explicit content/session targets, wire a real application owner through the transaction hooks, and add focused product discovery plus atomic commit/rollback tests before re-review.

**FND-06b, high:** `src/content/catalog/discovery.c:1-9,36-65` directly uses `dirent`, `lstat`, and POSIX `stat` macros. Once added to the build it still needs a platform-neutral directory-discovery boundary, including Windows reparse-point and case behavior. There is no B15 test file for the stock product table, expansion inheritance, mod discovery/dependencies, independent role selections, or prepare/abort/commit lifetime.

## Donor comparison boundary

The donor files directly used to check behavior were:

```text
../quake-typescript/src/world/scheduler.ts
../quake-typescript/src/content/catalog/products.ts
../quake-typescript/src/content/catalog/index.ts
../quake-typescript/src/content/catalog/launch.ts
../quake-typescript/src/content/mounts/index.ts
../quake-typescript/src/content/mounts/paths.ts
../quake-typescript/src/formats/q1-map/index.ts
../quake-typescript/src/formats/q1-map/records.ts
../quake-typescript/src/formats/q2-map/reader.ts
../quake-typescript/src/formats/q3-map/decode.ts
../quake-typescript/src/formats/q3-map/ibsp44.ts
all TypeScript files under ../quake-typescript/src/formats/images/
all TypeScript files under ../quake-typescript/src/formats/q12-model/
all TypeScript files under ../quake-typescript/src/formats/q3-model/
```

Those comparisons establish required format/ordering/product semantics only. TypeScript output is not treated as a golden runtime oracle, and the original source trees were reference-only and not exhaustively audited in this lane.

## Repair and re-review gates

1. Wire the scheduler into the session frame phases and add direct session/scheduler tests (FND-01).
2. Put B15 into build-owned targets with an application consumer and lifecycle tests (FND-02).
3. Split VFS and catalog filesystem operations into complete POSIX/Windows implementations without weakening containment or identity semantics (FND-03/FND-06b).
4. Validate Q1 model hull 3 and add its malformed-input regression (FND-04).
5. Resolve and test the `qa_think.sequence` contract (FND-05).
6. Register `vfs_test` and add focused world/session/catalog tests for the source contracts above (FND-06a).

Each held task needs source re-review after its fix. P01 remains responsible for compilation, sanitizers, actual retail assets, campaigns, mixed-provider execution, saves/travel, networking, and platform runtime evidence.

## Jev judgment record

Each report was submitted with the installed plugin CLI as `check report --project quake-anthology --task Bxx REPORT`. These are the actual readings; an unflagged `claims-done` reading is a source-report judgment, not runtime qualification. Exit status 2 below comes from any stop-reason review/flag as well as a claims-done gate.

| Task/report | `claims-done` | Stop-reason reading(s) | Exit |
|---|---|---|---|
| B01 | `overclaims=0.40` | `REVIEW blocked-external 0.32`; `REVIEW excuse 0.39` | 2 |
| B02 | `overclaims=0.40` | `REVIEW allowed-by-rule 0.28` | 2 |
| B03 | `overclaims=0.10` | `REVIEW allowed-by-rule 0.28`; `REVIEW blocked-external 0.35` | 2 |
| B04 | `overclaims=0.11` | heading `REVIEW blocked-external 0.32`; report `EXCUSE 0.65` | 2 |
| B05, first summary | **`overclaims=0.76 FLAGGED`** | `EXCUSE 0.67` | 2 |
| B05, detailed recheck | `overclaims=0.18` | `REVIEW excuse 0.25` | 2 |
| B06, first summary | **`overclaims=0.71 FLAGGED`** | heading `REVIEW allowed-by-rule 0.25`; report `EXCUSE 0.60` | 2 |
| B06, detailed recheck | `overclaims=0.47` | `REVIEW excuse 0.33` | 2 |
| B07 | `overclaims=0.05` | `EXCUSE 0.74` | 2 |
| B15 | `overclaims=0.06` | heading `REVIEW blocked-external 0.36`; report `EXCUSE 0.50` | 2 |

The first B05/B06 summaries were too coarse for the claims-done check. After the flagged readings, their reports were expanded with exact loader dispatch, retained record classes, actor/world ownership, spatial ordering, and collision paths. The rechecks cleared the claims-done gate. Both initial flags remain recorded above. The stop-reason classifier continued to review or flag passages that described P01's recorded deferral or root-owned repair work; none of those readings is represented as a pass.

At audit time the ledger still reported B01-B07 as `complete`, although this source review establishes unmet B03, B04, and B07 criteria. B15 was already `in_progress`. The original tasks must remain or become open until the held findings are fixed and independently re-reviewed.
