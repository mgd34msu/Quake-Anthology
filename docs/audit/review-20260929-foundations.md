# Foundation review, 2026-09-29

This is a bounded source review of existing implementation, not a declaration that the baseline or the entire foundation is complete. No baseline features were added. No compiler, build, parser, test, game executable, sanitizer, or benchmark was run. Source inspections and repository metadata/whitespace checks were the only verification.

## Confirmed defect and repair

**P2: Console numeric parsing and emission depend on the host numeric locale.** Before this repair, `src/console/text.c:qac_number` used plain `strtof` for Q2/Q2 rerelease/Q3 cvars and command amounts. Under a comma decimal locale, an authored value `"1.5"` stops at the dot and becomes `1`. `src/console/cvars.c:qa_cvars_set_number` and `src/console/commands.c:builtin` also used process-locale `%f`: the former emits `"1,500000"`, while Q1's explicit dot parser returns `1`; the latter emits decimal cvar text that its `decimal_text` command check rejects for subsequent increments.

The repair uses existing shared numeric helpers: non-Q1 `qac_number` calls `qa_parse_atof_float`, `qa_cvars_set_number` calls `qa_format_fixed` for six-decimal output, and `inc`/`dec` calls `qa_format_fixed` with the existing zero/six-decimal choice. The helpers use a separate C numeric locale and fixed nearest-even rounding without changing the process UI locale. Existing integer shortcut, nonfinite command spelling, 31-character truncation, finite numeric-set guard, and Q1 parser behavior remain intact. Parsing uses C-locale `strtof_l`/`_strtof_l` directly into a C `float`, preserving direct binary32 rounding.

Evidence: current repair sites are `src/console/text.c:326`, `src/console/cvars.c:414`, and `src/console/commands.c:1199`; helper behavior is in `src/core/number.c:qa_parse_atof_float` and `qa_format_fixed`. Donor `../quake-typescript/src/core/cvars/numbers.ts:cvarValueText` explicitly defines C-locale six-decimal binary32 formatting; `index.ts:setValue` uses it and applies the same integer shortcut/truncation policy. `CMakeLists.txt` already links `qa_console` publicly to `qa_core`, so no dependency change was required. The coordinator approved one public declaration for the float prefix helper in `include/qa/text.h`.

## Rejected suspicions

- Cvar output/binding callbacks causing mutation-related lifetime failures: public console callback contracts prohibit mutation/destruction during those callbacks; no contract-compliant failure established.
- Model reallocation losing existing buffers on failure: `model_grow` returns the original allocation while flagging the reader error.
- JSON node growth invalidating parser-frame storage: the parser frame stack uses separate storage.
- JPEG CMYK-to-RGB conversion dropping K: donor image decoder explicitly retains CMY channel behavior; no change made.
- Q3 one-dimensional weighted mip tail copying without filtering: donor explicitly preserves that source behavior; no change made.
- Script expression division checking the integer operand in float mode: donor deliberately uses that same combined integer/float zero check; no change made.
- Common token failed partial output exceeding the nominal capacity: the public output array reserves the extra byte used by the implementation.

## Verification and limits

- Inspected the complete five-file repair diff and its helper/caller/dependency paths.
- `git diff --check -- src/console/cvars.c src/console/commands.c src/console/text.c` exited 0, with no whitespace diagnostics.
- Only `include/qa/text.h` was modified, with coordinator approval. No donor/source/assets were written, copied, or executed.
- This finding is established by source/control-flow analysis. Locale behavior has not been exercised at runtime under the standing source-only restriction.
- Uncovered and partly inspected files below remain review debt; no correctness claim is made for them.
- Jev registration was unavailable to this worker because no genuine external runtime session ID was exposed. The coordinator has been notified; no ID was fabricated and no global plan/task was marked complete.

## Inspection manifest

“Full source read” means the file was read end-to-end and inspected for concrete lifetime, bounds, state, ownership, or decoding defects. It does not establish formal correctness or runtime compatibility. “Partial” limits the claim to the named regions. “Not covered” records review debt explicitly.

| File | Coverage |
| --- | --- |
| `src/console/buffer.c` | Not covered |
| `src/console/commands.c` | Full source read |
| `src/console/cvars.c` | Full source read |
| `src/console/dedicated.c` | Not covered |
| `src/console/discovery.c` | Not covered |
| `src/console/documentation.c` | Not covered |
| `src/console/field.c` | Full source read |
| `src/console/internal.h` | Not covered |
| `src/console/seat.c` | Not covered |
| `src/console/source_field.c` | Full source read |
| `src/console/text.c` | Full source read |
| `src/content/archive.c` | Full source read |
| `src/content/catalog/behaviors.c` | Full source read |
| `src/content/catalog/catalog.c` | Full source read |
| `src/content/catalog/discovery.c` | Partial: 1-230: file/archive discovery setup; remaining tail not inspected. |
| `src/content/catalog/internal.h` | Full source read |
| `src/content/catalog/metadata.c` | Full source read |
| `src/content/catalog/mounts.c` | Full source read |
| `src/content/catalog/products.c` | Full source read |
| `src/content/vfs.c` | Partial: 1-220: allocation, mount/source setup; remaining tail not inspected. |
| `src/core/arena.c` | Full source read |
| `src/core/binary.c` | Full source read |
| `src/core/common.c` | Full source read |
| `src/core/common_parse.c` | Full source read |
| `src/core/hash.c` | Full source read |
| `src/core/json.c` | Partial: Parser tail, object index, lookup/public operations; initial read truncated; not a full review. |
| `src/core/json_writer.c` | Full source read |
| `src/core/normals.c` | Not covered |
| `src/core/number.c` | Full source read |
| `src/core/q3_key.c` | Full source read |
| `src/core/script/checkpoint.c` | Not covered |
| `src/core/script/codec.c` | Not covered |
| `src/core/script/defines.c` | Not covered |
| `src/core/script/directives.c` | Not covered |
| `src/core/script/expansion.c` | Not covered |
| `src/core/script/expression.c` | Full source read |
| `src/core/script/internal.h` | Not covered |
| `src/core/script/lexer.c` | Not covered |
| `src/core/script/number.c` | Full source read |
| `src/core/script/quoted.c` | Not covered |
| `src/core/script/read.c` | Not covered |
| `src/core/script/source.c` | Not covered |
| `src/core/strings.c` | Full source read |
| `src/core/text.c` | Full source read |
| `src/formats/bsp.c` | Not covered |
| `src/formats/image/gif.c` | Full source read |
| `src/formats/image/image.c` | Full source read |
| `src/formats/image/indexed.c` | Full source read |
| `src/formats/image/internal.h` | Full source read |
| `src/formats/image/jpeg.c` | Full source read |
| `src/formats/image/png.c` | Full source read |
| `src/formats/image/raster.c` | Full source read |
| `src/formats/image/wad.c` | Full source read |
| `src/formats/model/alias.c` | Not covered |
| `src/formats/model/internal.h` | Full source read |
| `src/formats/model/lod.c` | Full source read |
| `src/formats/model/md3.c` | Full source read |
| `src/formats/model/md4.c` | Full source read |
| `src/formats/model/md5.c` | Partial: 1-320: tokenized model decoding; remaining tail not inspected. |
| `src/formats/model/metadata.c` | Full source read |
| `src/formats/model/model.c` | Full source read |
| `src/formats/model/scales.c` | Full source read |
| `src/formats/model/text.c` | Full source read |
| `src/formats/model/transform.c` | Full source read |
| `src/platform/console.c` | Full source read |
| `src/platform/display.c` | Not covered |
| `src/platform/file.c` | Full source read |
| `src/platform/file_windows.c` | Full source read |
| `src/platform/filesystem.c` | Full source read |
| `src/platform/filesystem_internal.h` | Full source read |
| `src/platform/filesystem_posix.c` | Partial: 620-940: writable-parent, replacement, removal, stream snapshot operations. |
| `src/platform/filesystem_windows.c` | Not covered |
| `src/platform/input.c` | Partial: 1-250 and 1180-1392: device setup and public operation tail. |
| `src/platform/mapping.c` | Full source read |
| `src/settings/codec.c` | Full source read |
| `src/settings/internal.h` | Full source read |
| `src/settings/restart.c` | Full source read |
| `src/settings/store.c` | Full source read |
| `src/text/captions.c` | Full source read |
| `src/text/font/console.c` | Not covered |
| `src/text/font/freetype.c` | Not covered |
| `src/text/font/internal.h` | Not covered |
| `src/text/font/kfont.c` | Full source read |
| `src/text/font/layout.c` | Full source read |
| `src/text/font/library.c` | Full source read |
| `src/text/font/q3.c` | Not covered |
| `src/text/font/ui.c` | Full source read |
| `src/text/font/world.c` | Not covered |
| `src/text/localization.c` | Full source read |
| `src/text/media_captions.c` | Full source read |

Public interfaces read in full: `include/qa/console.h`, `include/qa/common_parse.h`, and `include/qa/text.h`. Other corresponding public headers were not reviewed in full and were not edited. Relevant CMake target declarations and the donor numeric formatting helper/setter were inspected in the cited regions only.

## Independent peer review: CMake registration repair

Reviewed the coordinator's four additions to `CMakeLists.txt`, the complete added translation units, their public/internal declarations, caller regions, and transitive target dependencies. Accepted by source inspection; no build or linker invocation performed.

| Existing file newly registered | Definition and caller evidence | Target/dependency result |
| --- | --- | --- |
| `src/bots/source.c` | Defines `qa_bot_vector_component/read/write`; movement direction/view/controller and navigation routes/source call them. `include/qa/bot_source.h` declares matching APIs. | Registered once in `qa_bots`; core error/math services are available through its existing PUBLIC dependencies. |
| `src/bots/movement/source.c` | Defines `bot_goal_origin/area` and `bot_result_*`; controller, routing and view call them. `movement/internal.h` declarations match. | Registered once in `qa_bots`, alongside its callers; no new library dependency needed. |
| `src/bots/navigation/source.c` | Defines `qa_bot_navigation_fuzzy/reachable` and their `_from` variants. Goal choosing, movement controller, and Q3 host service 553 call these APIs. Public navigation headers declare matching APIs. | Registered once in `qa_bots`; `qa_navigation`, `qa_builtin`, and their existing world/session dependency chain supply the called navigation/world APIs. |
| `src/render/material/order.c` | Defines order reservation/publication/lifetime/prepare/rank functions called by `material/library.c` and `scene/sort.c`. `qa/material.h` and `library_internal.h` declarations match. | Registered once in `qa_scene`, alongside both callers; its existing `qa_world` dependency reaches `qa_core`/`qa_data` for error services and math linkage. |

The before-diff target lists omit all four files despite references from registered translation units. The patch supplies their definitions without altering public interfaces. This is source evidence for missing-definition repair; it is not a successful-link claim. `cmake/Native.cmake` supplies `qa_data` (including common/numeric code and UNIX math linkage), reached through `qa_core`. Full application material-order ownership integration was outside this peer request and is not established here.

## Independent peer review: Q1 hostile deadline conversion

Accepted the worker's frozen repair in `src/gameplay/q1/runtime.c` (`4c197f205d5db7c08aa9ffff203a75da670749013dcb83de13592ce7d148b26f`), limited to `hostile_deadline_ns` and its two actor-traits calls. The include and restored think-binding changes in the same working-tree diff predate this repair and were not part of this peer acceptance.

Confirmed source trace: player checkpoint `player.c:77` and monster checkpoint `monster.c:46` serialize `hostile_until` with `q1_save_double`; `checkpoint/io.c:number64` accepts any finite double. Thus a restored finite positive value such as `1e30` reaches `qa_q1_game_actor_traits`; the previous direct `(uint64_t)(seconds * 1e9)` converts an out-of-range double to integer, which is undefined behavior. The helper returns zero for nonpositive/unordered input, saturates nonfinite products and products at/above the unsigned conversion limit to `UINT64_MAX`, and otherwise preserves truncation toward zero. The binary64 conversion of `UINT64_MAX` rounds to `2^64`; the `>=` check rejects exactly that boundary, while the next lower representable positive double is within the unsigned range. Both player and monster branches now use the helper. Their existing positive-value selection policy remains unchanged.

Inspected the helper/diff, both call sites, checkpoint writers and double reader. Source-only SHA-256 matched the worker's frozen artifact. No compiler, sanitizer, save parser, or runtime behavior was executed, so checkpoint execution and ordinary gameplay are not claimed verified.

## Foundation peer correction and revised freeze

The initial console patch used the existing double prefix parser followed by a float cast. Independent Q1 review rejected that approach with the decimal `1.0000000596046447753906251`: direct binary32 conversion rounds above the midpoint, while binary64 first rounds to the exact binary32 midpoint and the later float conversion ties to even, yielding `1.0f`. This was a source/arithmetic finding, not a runtime reproduction. The initial `text.c` hash `491a517fa06506f2fb9c00037ec293d411209eff59af45433f269f22c5e62c7c` is superseded.

The revised helper `qa_parse_atof_float` shares the already initialized C numeric locale but calls the direct float locale parser. It retains existing argument/resource failure reporting, prefix acceptance, overflow/underflow and nonfinite conversion behavior. No double intermediate remains. The root approved the public declaration before it was edited. Revised frozen hashes: `include/qa/text.h` = `43271b0fae95066fc7ddac44c3105f1afb9360c6767cdffd04a74d2d6af685cf`; `src/core/number.c` = `091725950c2ec44fe53adf917ad63939cb2f625d1d8c3ed1f6015ee59f09a119`; `src/console/text.c` = `a8a9e8953bd19c9d72e51fa02e8f973678eec661c7029be2e9583e418acecb49`. Setter/command source hashes remain unchanged. Updated five-file whitespace check exited 0. Independent Q1 reviewer matched all revised hashes and accepted the direct float conversion, matching header/caller, formatter capacity and retained dialect policies by source inspection. Runtime/platform behavior remains unverified.

## Independent peer review: shared inventory operation teardown and armor ownership

Accepted the Q3/shared worker's frozen four-file packet after matching its SHA-256 identities: `include/qa/operation.h` = `85ff155e3975110127a9fb19c710036cad12e6289318751edd3e54ae5e83389e`; `src/gameplay/operation.c` = `c69338daad24b6ddd89680a2497bfa0e8a4374aff8b86ce613177ad8145d9af7`; `src/gameplay/inventory.c` = `fe6f355a2c9dca9e6c30ad1859452672e44adf9dbe77baf36a9347c5538573d8`; `src/gameplay/combat.c` = `63a57835dd2c44d091482985d172f51381328148e45401434f60565b2050422f`.

**Inventory teardown:** read all operation lifecycle/admission/dispatch code, its public contract, inventory creation/destruction, store release, entry admission lifetime, and operation lookup. A caller can obtain an inventory operation and retain a prepared hook token: `qa_operation_prepare` links the token into `operation->admissions`; validation/commit/abort retain access to that operation. Previously inventory destruction ignored `qa_operation_destroy` failures and continued freeing other operations/table, leaving a token linked to leaked operation storage and a freed inventory service, and potentially destroying earlier operations before reaching a later reserved one. The new `qa_operation_destroy_validate` has the same depth/admissions checks as destruction without mutation; inventory checks all four before destroying any. The operation contract has one owning thread, and successful destruction calls no callbacks, so no callback or concurrent mutation can invalidate those checks between preflight and free. Active inventory-entry admissions already increment `table->calls` via `acquire` until commit/abort; the existing initial calls guard remains effective. Null destruction stays successful. No mutation occurs on rejected operation preflight.

**Armor final validator:** read the complete setter, ownership helper, primary/effective readers, binding replacement/serial handling, actor release/destruction, and relevant public binding contracts. The external final `validate_armor` callback can retire the actor or replace its storage/protection ownership while `active_calls` prevents destruction of the combat service. Before the fix, captured `binding.write_armor` followed that callback before checking ownership. The patch balances `active_calls`, rejects validator failure, and checks the original actor record, storage serial and both protection owner serials before calling the captured writer. Stable fixed-capacity record storage and short-circuit ownership lookup protect the checks when an actor retires. The existing post-write ownership check remains. No new callback/lifetime defect was established in this bounded diff.

All conclusions are source-only. The four-file plus foundation diff whitespace check exited 0. No dispatch, callback, admission, teardown, or armor scenario was executed; this peer review does not claim runtime verification or the complete Q3 implementation reviewed.

## Independent peer review: cinematic clock callback lifetime

Accepted `src/media/cinematic.c` frozen at `def3c1f6e574fb3f2a611997aa6536eabf6d2ac9d230d759eccd8825b5a5e1fc`. Read the complete file, cinematic public/internal interfaces, public media clock callback type, and cinematic image/fullscreen caller paths. The repair is restricted to pause/time/capture busy ownership.

Confirmed source trace: while a published movie is playing, `qa_cinematic_pause(movie, true)` calls `pause_clock` → `wall_time` → the caller-supplied `options.clock.sample`. Before this repair, that callback could call `qa_cinematic_destroy(movie)` while `busy` was false, freeing the owner; the resumed pause code then wrote `movie->paused_at`, `movie->paused`, and status. `qa_cinematic_time` and capture similarly sampled and then accessed owner fields. Tick and end-playback already held `busy` during their clock callbacks.

All three repaired public entry points now hold `busy` over clock sampling and subsequent owner accesses; destroy and mutating APIs refuse busy movies. Pause/time unwind the flag for success and sampling failure. Capture uses a private helper so its allocation/decoder/audio failure returns all pass through the wrapper that clears the flag, while the original temporary checkpoint cleanup and final output publication remain unchanged. Create's movie is unpublished during initial clock sampling. Callback-free status/destination/frame/revision queries remain usable. No new concrete lifetime regression was established in the bounded diff. Frozen source hash matched; source-only whitespace checks passed. No callback/lifetime scenario was executed, and no runtime result is claimed.

## Independent peer review: Q1 weapon animation conversion

Accepted the frozen two-file follow-up: `src/gameplay/q1/weapons.c` = `caddde0a8a1e28399421d78bc3d7eb3c3b00b27d848c7340b998515845e61311`; `src/gameplay/q1/checkpoint/player.c` = `eb06d4740eef00913de99d358483223265c182d36ec4f2e29a1a67d22ad20d48`. Read the complete player codec, runtime time serialization and codec numeric validation, the complete prethink animation path and authored animation-base assignment sites.

Confirmed source trace: finite saved time `1e9` seconds and animation start zero are accepted by the existing double checkpoint codec. The old prethink code narrowed `floor((time - start) / 0.1)` = `1e10` to int32 before noticing the animation had finished, causing an out-of-range floating conversion. The repair retains the floored frame in double, clamps negative/future-start differences to zero, and compares against four/six frames before narrowing. Only the incomplete branch narrows, where the frame is an integer from zero through three/five. The other branch publishes zero and closes animation.

The related codec guard rejects `animation_base > INT32_MAX - 5`, which prevents overflow when adding the bounded frame to malformed checkpoint bases such as `INT32_MAX`. Lower int32 bases cannot underflow while adding a nonnegative frame. The authored bases 1, 5, 32 and 38 remain admitted, and ongoing ordinary animation keeps the existing count/event/completion path. The complete preexisting codec is not claimed authored or changed by this worker: only the new base guard is part of the accepted follow-up. Both source hashes matched; whitespace checks exited 0. No animation, checkpoint parser or executable was run, so runtime behavior remains unverified.

## Independent peer review: shared console command contributions

Accepted `src/console/commands.c` at SHA-256 `777f2f4f1e7d3dee624b516e2fcb3b401bfc118061034c5c5e4469d86b17de02`. Read registration, dispatch, ordinary unregister, contribution admission/removal, owner removal and entry cleanup, plus Q3 common AddCommand/RemoveCommand consumers. Header declarations match the implementation. A command entry now distinguishes an ordinary registration from separately owned role contributions; matching name/dispatch owner shares one null-handler entry, repeated identical contribution is idempotent, and conflicting real handlers are rejected. Ordinary unregister preserves contributions; role removal preserves sibling contributions; dispatch-owner removal frees the entire entry and contribution chain. No confirmed defect was found in this bounded source pass. Runtime and platform behavior remain unverified; no compiler, program or test was run.

## Independent peer review: native player bot packet

Source acceptance is bounded to the 20 paths in `docs/implementation/bots-20260929.sha256`, whose current hashes all matched with `sha256sum -c` (exit 0). Read the public bot/runtime/knowledge contracts, complete knowledge and AI checkpoint/combat/console/decision/frame/orders/reset/roster/team sources and private header, and the complete application bot owner/arsenal/navigation/projection/submission sources and private header. Also traced root frame/actor retirement/map publication/destruction consumers in application owner/services/publication/lifetime, navigation train/eligibility/prediction consumers, and donor selected/native Q1/Q2/Q3 knowledge paths. No bot implementation files were edited by this reviewer.

Confirmed and repaired **BPR01**, an out-of-range conversion in the Q2 rerelease player projection. Movement state admission permits finite delta angles without a degree bound. For `delta_angles.x = 1e30f`, the previous direct multiplication and int32 conversion exceeded the integer range. The bot owner replaced projection and submission conversion with one private helper: `fmodf(angle, 360)` bounds the finite reduced value before multiplying and narrowing, then the uint16 conversion preserves angle-word wrap. Reread all three changed files and checked their superseding hashes: private header `9d2a86a01f3ac1f69dbff04d4fe7172c83312e27b8963d0fbdb15f8484e187f1`, submission `f7390069aa13e2a738d889f6aaeca17d6a1d7ef04f761687abec501c05bdf5d1`, projection `4d7be02267503197124de68ce3392c25a687426cdd191294abef8720568a3ab1`. Their tracked diff whitespace check exited 0.

Ownership review: application callbacks retain the bot owner through its call/arsenal/pickup guards; population frame/capture/restore borrow the runtime lease and reject destructive reentry. Actor retirement marks the matching full generation and defers continuation cleanup until frame completion. Publication rejects borrowed bot/guest state and destroys bots before retiring the old map; the new population is constructed after canonical player admission. Close frees population before runtime, and actor navigation before shared graph/world owners. Navigation consumes retained Q1 train stops synchronously and copies selected ride stops into its result. The suspected Q2 rerelease canonical PML mutation was refuted by `navigation/prediction.c`, which copies the supplied origin into its detached prediction workspace and repoints the movement input before execution. The suspected repeated `maps/` path was refuted by the application map-name normalization. Bot restore validates identical actor/character/resource handles, captures prior private state and attempts rollback on a failed apply; allocation failure can also prevent rollback and is reported explicitly rather than claimed atomic.

No additional confirmed defect was established in these bounded paths. This accepts the repaired source packet for further integration, **not B26 completion**. Ordinary command/chat ingress, guest arsenal/player observations, Q2/Q3 source mover observations, detached source prediction lease hooks, recipient pickup replacement registration, full expansion/rerelease weapon coverage and new-session application save reconstruction remain explicit omissions. It does not review every underlying bot library or claim runtime/prediction/save parity. No configure, compiler, parser execution, tests, game or benchmark was run.

## Independent peer review: QC candidate player/map guards

Accepted the bounded guard changes in `src/app/application/map_players.c` at `f3ae2bf441beb86dc17ab7a295ac07c05cf9a30b98ede0df3e990a0069fec1c1`. The candidate map provider's QC qualification is checked before travel allocation/capture and before retirement, so a reused qualified component cannot become an ENTITIES owner that silently omits authored entities. Once-per-provider roster capacity checks use the actual candidate selected seat roles. Per-seat readiness passes the actual selected CHARACTER relationship and actual candidate ENTITIES owner, without trusting the provider's old declaration role mask. Native Q3 preparation availability and the secondary guest guard now use the launch selection clock, which exists before construction; the unpublished component clock cannot falsely classify an unconstructed native guest. Read the predicate producers and existing binding handoff. No further confirmed defect in these bounded changes; no map, client or travel scenario executed. Qualified authored entities and unqualified QC players on foreign maps remain explicit implementation gaps.

## Independent peer review: Q1 weapon metadata bot consumer

Accepted the bounded three-file consumer follow-up after reading its actual metadata contract and source observation owner. Current hashes: `include/qa/bot_knowledge.h` = `5712fa79c89616a811200fa7d4d30fca0a2637457c18e7ea3360255604050776`; `src/app/application/bots_arsenal.c` = `61638a3750e56a2332dee47b26d8d2e56c8ac376459af2222b21900596602b52`; `src/bots/ai/combat.c` = `3002cb8df6fb7bddeec2c4977de42e1162257c3c3bbe69c3ea33bafd2212f9d9`. The complete 20-entry bot manifest matched again, and the bounded tracked whitespace check exited 0.

The bot adapter enumerates actual Q1 weapon observations instead of retaining its duplicate base table. Enum plus one matches existing submission lowering; source availability and ownership remain separate; canonical supply IDs, fractional ammo per shot and travel capability survive detached projection. Its owner lease now starts before source callbacks, is released on failed construction and remains until explicit observation release. Generation and selected provider rechecks clear observations if source reads retire/rebind the actor. Combat copies selected metadata before releasing the lease, uses native world-space muzzle offsets relative to actor origin, retains paired muzzles and incorporates source launch delay in the existing lead estimate. Zero muzzle count preserves Q2/Q3 library eye-relative offsets. No additional confirmed defect found in these bounded changes. Gravity-aware trajectory solving and the broader bot omissions remain required work; source acceptance does not claim gameplay or runtime verification.

## Independent peer review: paired QC native/guest input consumers

Read the consumer scope contract, complete source-selection/deduplication/capture/close helpers and their native whole-command and real movement-slice consumers, guest source move/lowering/publication paths, and movement common/Q1/Q3 END/ABORT dependencies. The current bounded consumer hashes are `control.c` = `7f421e91d6a9e840a9769dae40264a56e68ea9e230272de4e2d8d0e29b1443dd`; `arsenal_guest.c` = `b5b382cba61721ea0b4d786768edda8f27d2ec2b48b079519cc8a2e7c01fea08`; `guest_input_private.h` = `62827855d10a53ccb2540097c52e3ebecf6bf9af52fecf3bc0b582726b95d59f`. Actual hashes matched; bounded tracked whitespace checks exited 0.

The scope records only providers whose before succeeded, across all seven selected player roles with deduplication. After uses that retained list and clears each entry before invoking its already-closing producer. On partial failure, reverse cleanup drains only remaining successful-before entries, avoiding accidental abort of an enclosing same-actor scope. Native slice cleanup runs on END/ABORT, including retirement, and whole-command cleanup precedes control record reclamation; guest source processing/publication failures drain their local matched scopes without a fake after callback. The ordinary native control implementation now exposes the actual applied detached command through a private wrapper; public movement still calls the same implementation.

Found and reread repairs for two output replay defects. The guest source input initializer previously reused the original impulse for each slice despite a prior consume; it now retains the applied impulse. Foreign guest locomotion similarly publishes the consumed native command back to the actual guest usercmd, including attack/axes/aim, and retains independent Q1 jump/up controls while rereading its own lossy Q3 up-byte projection. Source changes to that byte are honored. The QC caller previously inferred Q1 jump from positive upward movement on every callback and used a synthetic RR up shim; both are removed, preserving distinct producer control semantics. Donor review also established normalized scalar units, repaired separately in the producer packet.

No further confirmed defect was found in these bounded caller repairs. This source acceptance excludes complete guest input qualification and the **remaining root-owned dependencies**: shared guest-idle admission must reject retained QC scopes, and QW shared-controls kernel autojump at `quakeworld.c:413` can still restore a consumed jump after whole-command callbacks. That QW alias also exists in the donor shared wrapper; it needs a native source boundary that preserves accepted control outputs. No command, movement, source VM, teardown or error-path scenario was run.

## Independent peer review: Q2 mover application bot consumer

Accepted the bounded three-file consumer follow-up: `src/app/application/bots_navigation.c` = `cffa037427b27f0abcaeb7d9f18695e2a441293f5972b8b6156fd0957766795b`; `bots_private.h` = `63db6542afb93f6da63c73fb429f9314d37878e7d35b9ff37211af2b20324a72`; `bots.c` = `60f43dccb592447f716b81c546bdc4bffd804a81555c894578ac9d60920da54e`. The current 20-entry bot manifest matched (exit 0), and the bounded tracked whitespace check exited 0. Read the complete adapter, private borrow fields, destruction/readiness guards and immediate Q2 producer view/call boundary; checked actual producer/header hashes against `peer-q2-navigation-bots-20260929.md` (`ee35fc4b...`, `1a848e11...`). This is consumer acceptance; the separate bot reviewer owns full Q2 producer acceptance.

`native_mover` resolves the actual canonical map actor's owner in the active routing/provider set, dispatches the typed Q1/Q2 source reader, and maps each concrete door/elevator/train/static kind explicitly. Q1 bobbing remains separate. Both sources write their deterministic route prefix into the same retained application buffer, sized to registry capacity with overflow/allocation failure checks before the source call. `mover_borrowed` rejects callback-driven nested writes/reallocation and guards destruction while the source callback executes; both success and failure clear it, and full actor-generation liveness is checked again before returning an observation. Common model and activation callers copy only detached scalar/actor/bounds facts, not stop pointers.

Traced all navigation stop-pointer consumers through eligibility, train and route code. Train ride reads the borrowed array synchronously without callbacks and copies boarding/arrival stop values into its result. Eligibility derives its awaiting state before the next entity query can overwrite the buffer; route/train traversal consumes/copies its needed stops before prediction/world callbacks. No observed path retains the array for a later callback or uses it after a subsequent mover query. Ambiguous/truncated Q2 routes retain only the source producer's deterministic prefix; the consumer does not invent missing successors. No confirmed defect found in this bounded consumer change.

The actual CMake Q2 source list still omits `src/gameplay/q2/entities/navigation.c`; root was notified and registration is a pending integration dependency. Q3 movers, broader activation/key planning, ordinary commands, detached source prediction and application saves remain unfinished. No build, compiler, parser execution, movement/navigation program or runtime check was run.
