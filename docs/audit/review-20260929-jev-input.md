# AUDIT criterion evidence from the 2026-09-29 bounded review

This submission asks Jev to judge the existing AUDIT goal and all its original criteria. It does not request changing those criteria. The current user-requested review/fix pass is complete, while broader AUDIT and BASELINE remain open. This is explicitly selected criterion evidence, not a full copy of all coverage inventories. Full reports and per-file inspection limits remain in docs/audit. No omitted coverage is claimed reviewed.

Criterion 1: Eight source ownership lanes and source/hash metadata exist. The final manifest records52 affected source/header/build paths; an earlier source snapshot recorded830 source/header files after workers started. No initial path was removed. This does not establish all existing reports/interfaces were reviewed.

Criterion 2: These packets are bounded subsystem bug reviews, not a fresh goal-by-goal B00-B34 acceptance audit. Original incomplete tasks remain in docs/audit/status.md. Full per-criterion evidence across all35 feature tasks remains open.

Criterion 3: This is an actual request for an aggregate Jev judgment. Earlier task judgments are retained. A report or progress/stalled check is not a task acceptance result. This submission cannot prove every open task criterion accepted.

Criterion 4: All32 reported repair groups in the bounded pass were fixed by exclusive source owners and independently reread. Peer findings led to revised console/native/Q3 repairs. Source manifests match reviewed packet hashes. Missing features and unreviewed paths remain open. Full implementation acceptance is not claimed.

Criterion 5: No engine configure/build/compiler/parser/tests/program/gameplay/sanitizer/benchmark executed. Reads, diffs, metadata Python, hash comparisons and whitespace checks only. git add failed because .git is read-only; no commit exists for this pass.


---
Source report: docs/audit/review-20260929.md
Selection: complete opening findings, repair/verification and scope sections; any later coverage inventory/peer appendix remains in referenced file.

# Existing-code review and fixes, 2026-09-29

The requested first review/fix pass is complete. Seven GPT-6.1 Sol workers used high effort, with no descendants, plus the coordinator. Existing dirty and untracked implementation was preserved. Every repair packet received independent source review; reviewers' counterexamples led to corrections before acceptance.

This is a bounded review of existing implementation. It does not clear every source file, complete the older whole-project AUDIT, or complete BASELINE. Each lane records complete, targeted and uncovered inspection explicitly. Runtime correctness, full interoperability and performance remain unverified.

| Area | Repairs accepted by source review | Independent confirmation | Evidence |
|---|---|---|---|
| Foundations and console | Locale-independent numeric parsing/formatting with direct binary32 conversion; the initial double-rounding regression was corrected. | Q1 reviewer | [Foundations](review-20260929-foundations.md), [console peer](review-20260929-q1-console-peer.md) |
| Q1 and campaign | Hostile deadline saturation, bounded animation frame conversion, checkpoint animation-base overflow rejection. | Foundations reviewer | [Q1](review-20260929-q1.md) |
| Q2 | Safe provider/action lifetime, checkpoint activity and retired storage, saved Widow timers, entity/controller identity guards, child admission cleanup, unsigned timer rounding, dormant Widow response correction. | Q3/shared gameplay reviewer | [Q2](review-20260929-q2.md) |
| Q3 and shared gameplay | Inventory teardown preflight, armor callback ownership, restore/reservation conflicts, actor generation checks, fire-event retirement, world-callback restore/destruction admission. | Q2 and foundations reviewers | [Q3/shared](review-20260929-q3-gameplay.md) |
| Bots and compatibility | Borrowed bot owner retention, Q3 observed-write identity checks, native synonym string length, complete native-host destroy admission and checkpoint callback retention. | Coordinator; initial incomplete native guard rejected and corrected | [Bots/compatibility](review-20260929-bots-compat.md), [coordinator](review-20260929-root.md) |
| Application and presentation | Consumed-session retry, cutscene mode/height restoration, command sequencing, movement callback ownership, listener identity, LMCTF view direction, world-idle lifecycle gates, cinematic clock callback ownership. | World/network, Q3/shared and foundations reviewers | [Application/presentation](review-20260929-presentation-app.md) |
| Shared world/session interfaces | Destruction readiness and world idleness use the same predicates as the existing owning teardown functions. | Application reviewer | [World/session/network](review-20260929-world-network.md) |
| Build definitions | Registered four existing bot/material source files required by current callers. | Foundations reviewer | [Coordinator](review-20260929-root.md) |

The lane reports identify 32 repair groups; related paths are deliberately grouped rather than counted as one bug per changed line. Shared world/session predicates support those repairs and are not counted again. The final source manifest records the affected source/header/build files and their SHA-256 values. Some of these files already contained unfinished work; a final-file hash does not attribute all its content to this pass.

Final metadata and repository whitespace checks are source-only. No engine configuration, build, compiler, parser, tests, executable, gameplay, sanitizers or benchmarks were run. No game source file was copied or sibling checkout changed. No per-file copyright/license notice was added.

Local commits could not be created: `git add -- CMakeLists.txt` exited 128 because `.git/index.lock` could not be created on the session's read-only `.git` filesystem. The index remained empty. The restriction was not bypassed; source changes and review evidence remain in the working tree.

Original AUDIT and B00–B34 acceptance criteria remain unchanged. Jev plan revision 7 records this session's model/concurrency rules. The aggregate judgment and ledger outcome are recorded below after the final evidence submission. Broader uncovered-source review, unfinished application/game/mod integration and subsequent runtime qualification remain open.


---
Source report: docs/audit/review-20260929-root.md
Selection: complete opening findings, repair/verification and scope sections; any later coverage inventory/peer appendix remains in referenced file.

# Coordinator source review, 2026-09-29

This packet belongs to the user's first task on resumption: review existing C code and repair errors found. It does not complete the older whole-source AUDIT, any B00–B34 feature task, or BASELINE. Seven GPT-6.1 Sol workers used high effort, with no descendants; the coordinator and workers stayed within eight concurrent agents.

## Build definitions

One confirmed registration defect affected four existing source files. `qa_bots` omitted `src/bots/source.c`, `src/bots/movement/source.c`, and `src/bots/navigation/source.c`, despite their definitions being required by existing movement and navigation callers. `qa_scene` omitted `src/render/material/order.c`, whose functions are called by the material library and scene sorting. These files are now registered once in their existing targets. The foundation reviewer independently traced definitions, callers, declarations and transitive dependencies and accepted this four-line repair.

No unfinished application, bot-runtime or Q3-host target was manufactured during this repair. CMake was read and edited, never configured or executed.

## Independent bots and compatibility review

Read the complete repair-only patch in `review-20260929-bots-compat.patch` and the changed owner/dispatch paths in `src/bots/runtime/core.c`, `setup.c`, `include/qa/bot_runtime.h`, and `src/compat/q3_host/{host,bot_library,bot_chat,game_records}.c`. Read native-host destruction and the complete checkpoint implementation, native instance destruction/activity, the native string-length contract, runner checkpoint/region dispatch, and Q3 ABI word writers.

The runtime lease retains borrowed resource owners across observed guest input/output without duplicating their storage. Lifecycle operations remain outside their own lease, and their shared mutation guard rejects an outer lease. The observed map-name read ends its short lease on either result before attempting map mutation. Every leased dispatch exit releases the lease. The Q3 body writer validates the original actor and table descriptors before and after each observed word, including ABI playerState words. The native synonym snapshot now respects the native string reader's content length.

The first native-host repair used `qa_native_active`, which only checks execution depth. Independent review rejected it: runner checkpoint region callbacks can have checkpointing and callback activity while execution depth is zero. The final `qa_native_can_destroy` query shares all four initial destruction rejection conditions with `qa_native_destroy`, preserving the existing activity query. Native-host checkpoint and restore retain their callback owner across all helper returns. The revised twelve-file packet is accepted by bounded independent source review. This establishes the changed failure paths, not all compatibility behavior or runtime correctness.

## Other inspected source

Read all seven existing `src/player_services` source/internal-header files and `include/qa/{player_progress,rankings,local_lobby,arena_progress}.h`, including progress serialization/storage, lobby snapshots, ranking activity and arena progression. No confirmed defect was established in those reviewed paths. Read `src/main.c`, `CMakeLists.txt` and `cmake/Native.cmake`. The current diagnostic entry point and missing application consumers remain baseline work; this review did not represent them as completed functionality.

Selected Q2/Q3 correction diffs and public lifetime contracts were also read for cross-owner integration. The lane reports identify their independent reviewers and exact scope. Reading a file is not a correctness proof; areas outside the recorded inspection remain open review debt.

## Evidence and limits

- Source basis: HEAD `fda00b110e790bf826c0ba9b602ebe1466751bab` plus the existing dirty/untracked implementation and this review's repairs.
- An initial metadata snapshot was captured after the workers started. It is not a clean pre-review tree or proof that every earlier edit was observed. The bots packet's before text for untracked files is explicitly reconstructed, not an independently captured initial snapshot.
- Repository whitespace checks succeeded. No engine configure/build/compiler/parser/test/executable/gameplay/sanitizer/benchmark was run.
- Jev plan revision 7 records the user's current model and concurrency rules. Root acknowledged that revision; the original feature goals and acceptance criteria were preserved. AUDIT remains in progress.
- A local staging attempt, `git add -- CMakeLists.txt`, failed with exit 128: Git could not create `.git/index.lock` because `.git` is a read-only filesystem in this session. The staged diff remained empty. No commit or remote operation occurred, and the restriction was not bypassed.
- Source files, assets and donor checkouts were not copied or modified. No per-file licensing or copyright notices were added.


---
Source report: docs/audit/review-20260929-foundations.md
Selection: complete opening findings, repair/verification and scope sections; any later coverage inventory/peer appendix remains in referenced file.

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


---
Source report: docs/audit/review-20260929-q1.md
Selection: complete opening findings, repair/verification and scope sections; any later coverage inventory/peer appendix remains in referenced file.

# Q1 and campaign source review, 2026-09-29

Existing-code review packet; not baseline acceptance. Reviewer: GPT-6.1 Sol, high effort. Base checkout commit: `fda00b110e790bf826c0ba9b602ebe1466751bab`. Jev contribution: `w_13253db187534697b26a095f7255db67`, session `s_1cf870dcacd54b22a5f5d72c0bfde508`, parent AUDIT work `w_357ddd74810a4aa8a3bb84fdd7095fb8`, original claim against plan revision 6. Current plan revision 7 was read against revision 6 and acknowledged before final handoff; its updated agent/session rules govern this packet.

## Confirmed defect and fix

**Q1-01, high: out-of-range double-to-uint64 conversion in actor traits.** `src/gameplay/q1/runtime.c` formerly cast `hostile_until * 1000000000.0` directly for both attached players and native monsters. The checkpoint paths in `checkpoint/player.c:79` and `checkpoint/monster.c:46` accept the hostile deadline through `q1_save_double`; `checkpoint/io.c:101` rejects nonfinite doubles but allows finite values beyond the representable nanosecond range. A finite deadline such as `1e30` therefore reaches a conversion whose truncated integral result is outside `uint64_t`, which is undefined behavior in C. This is a source-level counterexample; no malformed checkpoint was executed.

Fix: `runtime.c:994` adds `hostile_deadline_ns`, returning zero for nonpositive input and saturating positive unrepresentable nanoseconds to `UINT64_MAX`. Both actor-trait deadline conversions use it at lines 1032 and 1034. The floating threshold is deliberately conservative at the binary64 representation of `UINT64_MAX`; this prevents casting 2^64. Normal finite representable positive values retain the previous truncation. No game timing, simulation arithmetic, checkpoint representation, or normal hostility behavior was otherwise changed.

Only this helper and the two conversion calls are authored by this review. The preexisting dirty include of `qa/game_q1_checkpoint.h` and `qa_q1_game_think_binding` implementation remain intact and are not claimed as review fixes. Frozen runtime file SHA-256: `4c197f205d5db7c08aa9ffff203a75da670749013dcb83de13592ce7d148b26f`.

**Q1-02, high: weapon animation completion narrows an unbounded frame before checking completion.** `src/gameplay/q1/weapons.c:285` formerly converted `floor((g->time - player->animation_at) / 0.1)` to `int32_t` before comparing the result with a 4- or 6-frame animation length. The checkpoint accepts finite runtime and player clocks; `time = 1e9` and `animation_at = 0` yields `1e10`, outside `int32_t`, even though the animation has finished. A large future animation start also yields an out-of-range negative conversion. Fix: retain the computed frame as a double, clamp negative elapsed frames to zero, compare with the short animation length, and narrow only in the active-frame branch, where the value is an integer from 0 through 5. Ordinary started animations retain their source frame behavior. Frozen `weapons.c` SHA-256: `caddde0a8a1e28399421d78bc3d7eb3c3b00b27d848c7340b998515845e61311`.

**Q1-03, high: malformed animation base can overflow signed frame addition.** The same active-frame expression adds a checkpoint-provided `animation_base`, formerly read as unrestricted `int32_t`. A base of `INT32_MAX` and frame 1 overflows signed addition. `checkpoint/player.c:72` now rejects bases greater than `INT32_MAX - 5` at the persistence boundary. Five is the maximum added offset for the inspected 6-frame path; all authored bases found in existing Q1 sources (1, 5, 32, 38) remain valid. Frozen `checkpoint/player.c` SHA-256: `eb06d4740eef00913de99d358483223265c182d36ec4f2e29a1a67d22ad20d48`. Only these two guard lines in the preexisting checkpoint implementation belong to this review. Neither malformed payload was executed.

## Checks and limits

`git diff --check -- src/gameplay/q1/runtime.c src/gameplay/q1/weapons.c src/gameplay/q1/checkpoint/player.c` completed with exit 0 and no output. Source inspection checked the old casts, both checkpoint field codecs, finite-double validation, and the saturating conversion. No configure, build, compiler, parser, test, executable, sanitizer, benchmark, or gameplay run was performed, as required by the current source-only baseline policy. Runtime behavior remains unverified. The foundations reviewer independently accepted the frozen hostile deadline repair by source inspection and SHA-256 match. The same independent reviewer accepted both later animation repairs by source inspection and matching frozen hashes. Runtime verification remains unperformed.

Full-read coverage below means every line was read, with focused reasoning on callbacks, retirement, checkpoint reconstruction, timer/union ownership, campaign transitions and donor behavior. It does not mean every behavior was proved or every TypeScript counterpart compared. Search-only and unvisited files are explicitly distinguished. The uncovered files prevent treating this packet as a complete Q1 subsystem audit.

## Refuted suspicions

- Suicide intentionally retains an infinite character animation deadline; the checkpoint uses `deadline`, whose infinity exception matches that state. No missing infinity exception was established for charm state.
- PUSH continuations use their local physics clock and pusher dispatcher. Rejecting their restored scheduler binding is consistent with that ownership.
- The target read API explicitly requires nonmutating reads. A speculative mutation race in campaign target lookups was not established.
- Classic and rerelease spawn selection ordering/rounding and campaign level state-before-callback ordering matched the inspected donor paths. Callback failure transaction semantics were not redesigned.
- Infected hellknight variant identity is distinguished explicitly in checkpoint species reconstruction. No lost variant was established.
- The unusual Q1 lightning side-vector expression also appears in the donor. No geometry rewrite was made under the guise of a bug fix.


---
Source report: docs/audit/review-20260929-q1-console-peer.md
Selection: complete opening findings, repair/verification and scope sections; any later coverage inventory/peer appendix remains in referenced file.

# Independent console numeric repair review, 2026-09-29

Reviewer: Q1/campaign review agent, GPT-6.1 Sol high effort. Source-only review of the foundations agent's locale repair; no runtime or platform verification.

## Initial patch finding

The initial `src/console/text.c` repair replaced direct `strtof` with locale-independent `qa_parse_atof` followed by a double-to-float cast. Under round-to-nearest and binary32/binary64 arithmetic, the decimal token `1.0000000596046447753906251` is above the binary32 midpoint between 1 and the next float. Direct binary32 parsing rounds up. Binary64 parsing first rounds to the exact midpoint `1.000000059604644775390625`, then conversion to binary32 rounds to even at 1. The intermediate conversion therefore introduced a precision regression while addressing locale dependence. This was established by source and arithmetic reasoning, not by executing a parser.

The foundations agent accepted the finding and replaced this implementation before integration. The initial version is superseded.

## Revised patch acceptance

The revised `qa_parse_atof_float` uses `strtof_l` or `_strtof_l` directly with the existing process-lifetime numeric locale and returns a float. The header declaration and `qac_number` call agree. Direct float parsing removes the intermediate binary64 rounding, retains prefix/nonfinite/overflow/underflow semantics of the old float parser, and selects the C locale independently of UI locale. The Q1-specific parser remains unchanged.

The two formatter callers use the existing `qa_format_fixed` helper. Both use 64-byte local buffers; the longest binary32 fixed output with six fractional digits fits that buffer. The existing 31-byte truncation or Q1 rejection policy remains in place. Integer formatting paths and nonfinite spelling are preserved. The helper borrows no caller-owned storage after return; its shared numeric locale deliberately survives for the process lifetime. The POSIX formatter restores thread locale and rounding state on the inspected branches. No remaining source-level defect was established in this bounded repair.

## Actual coverage and checks

Full source reads: `include/qa/text.h`, `src/core/number.c`. Targeted reads: `src/console/text.c` numeric parser, `src/console/cvars.c` number refresh and numeric setter, `src/console/commands.c` decimal validation and inc/dec handler. Both initial and revised diffs were inspected. This is not a review of the complete console subsystem.

`git diff --check -- include/qa/text.h src/core/number.c src/console/text.c src/console/cvars.c src/console/commands.c` exited 0 with no output. No configure, compiler, parser execution, tests, sanitizer, executable, or gameplay run was performed.

Reviewed SHA-256 hashes:

| File | SHA-256 |
| --- | --- |
| `include/qa/text.h` | `43271b0fae95066fc7ddac44c3105f1afb9360c6767cdffd04a74d2d6af685cf` |
| `src/core/number.c` | `091725950c2ec44fe53adf917ad63939cb2f625d1d8c3ed1f6015ee59f09a119` |
| `src/console/text.c` | `a8a9e8953bd19c9d72e51fa02e8f973678eec661c7029be2e9583e418acecb49` |
| `src/console/cvars.c` | `07a7a0de54a80124be60b389d02f7c453652c2beb1543e14f47a106e180c2bd7` |
| `src/console/commands.c` | `139945b9376345be56fb8a7313ea9ac393fa74c884b44da7530c76b938bbde83` |

Platform availability and live numeric behavior remain unverified under the source-only policy. Peer acceptance applies only to this frozen patch and inspected contracts, not to full AUDIT or BASELINE acceptance.


---
Source report: docs/audit/review-20260929-q2.md
Selection: complete opening findings, repair/verification and scope sections; any later coverage inventory/peer appendix remains in referenced file.

# Q2 source review, 2026-09-29

This packet reviews existing code at base commit `fda00b110e790bf826c0ba9b602ebe1466751bab`, including preexisting uncommitted Widow work. Findings are source-traced, not runtime reproductions. No engine build, compiler, parser, tests, executable, sanitizers, benchmark or gameplay run was performed. This report does not complete AUDIT, B11 or BASELINE. Original source files and all preexisting edits remain intact.

## Confirmed corrections

| ID | Severity | Source location after correction | Concrete failure and correction |
|---|---|---|---|
| Q2R01 | high | `game.c:304`, `monsters/core.c:801` | At a safe session point, `qa_q2_run_actor` could call a callback that destroyed the Q2 game, then write `g->current_actor` after free. Direct monster actions could also run a mission callback that destroyed the game before `q2m_alive` resumed. Destruction now requires session, world, combat and Q2 actor/hand/query idleness. Public monster actions use the existing typed actor invocation. World callback idleness depends on the world reviewer's new `qa_world_idle` API. The direct mission trace was independently confirmed by the Q3 gameplay reviewer before its wrapper was added. |
| Q2R02 | high | `monsters/checkpoint.c:16`, `:449`, `:675` | Per-monster checkpoint checks ignored an ongoing actor turn or hand action, allowing capture or replacement midway through gameplay despite sibling checkpoint APIs rejecting it. Replacement freed an old monster while direct observers could hold its context, and allocator reuse could make a stale pointer appear current. Capture/restore now reuse `q2_checkpoint_idle`; successful replacement retires old state through the existing next-frame reclamation owner. |
| Q2R03 | medium | `monsters/checkpoint.c:196`, `:545` | The preexisting `widow_powers` state and public checkpoint field were not captured or restored, discarding copied quad/double/invulnerability deadlines. Capture and restore now retain all three timers; the changed private checkpoint schema is version 8. Runtime multiplier capture was already implemented in the preexisting runtime checkpoint version 2 and was preserved. |
| Q2R04 | high | `entities/state.c:98`, `entities/lifecycle.c:102`, `:120` | A combat/body observer could replace the authored entity through its checkpoint API while keeping the actor and observed combat/body valid. Health fallback and spawn then dereferenced a freed cached entity. They now revalidate the original actor extension and entity pointer after the observations. |
| Q2R05 | high | `monsters/beam.c:9`, `:63`, `:105`, `:340` | Beam/controller guards accepted any replacement controller of the same kind, so cached old owner, damage and direction could continue after callback replacement. Guards now require the exact observed controller. Body and combat observations stop retired/replaced work before subsequent state use; healing and boss explosion paths also recheck the affected actor. |
| Q2R06 | medium | `monsters/beam.c:183`, `:200`, `:248`, `:294` | Target observations or child admission callbacks could retire the parent monster, yet the old call still created a beam/exploder/Makron controller. Parent identity is rechecked before beam admission and after each controller admission; an already-created child is released when its parent state retired. |
| Q2R07 | medium | `monsters/core.c:13` | `q2m_after(0, 10000000000.0)` has a 10^19 ns result below UINT64_MAX but above LLONG_MAX. `llroundl` therefore exceeded its signed result domain. The helper now rounds with `floorl` and clamps against the remaining unsigned timer range before conversion. |
| Q2R08 | medium, dormant WIP | `monsters/widow_power.c:134` | The donor does not call `respond` when the non-coop Widow has no enemy, retaining its source-wide multiplier. The written C helper called `respond` with an empty actor and reset it to 1. It now skips that response when no enemy exists. The helper still has no gameplay callsites; this correction does not finish the Widow feature. |

## Verification and tracking limits

Reviewed the complete correction diff and ran `git diff --check -- src/gameplay/q2`, which produced no whitespace findings. Examined the shared world body's callback depth/identity guard, combat read and idle guard, target binding callbacks and Q2 retired monster reclamation. No source files were copied from `qsrc` or the TypeScript donor. The donor default attack, boss attack and Widow power routines were consulted for behavior.

Jev tools are exposed, but this worker has no trusted external runtime session ID. No identity was invented. The coordinator should register this source review under existing AUDIT work `w_357ddd74810a4aa8a3bb84fdd7095fb8`, session `s_68a7c43280594f24b1cc6f98bd5aab9d`, plan revision 7 as reported by the coordinator. The Q3 gameplay reviewer independently accepted the frozen seven-file packet after matching its hashes and reading the correction diff and affected lifetime, publication, timer and donor paths. This is bounded source acceptance; broader Q2 behavior and direct public provider teardown remain unqualified.

## Independent Q3 and shared gameplay review

Reviewed every changed hunk in the Q3/shared gameplay packet and the affected operation destruction, inventory preflight, armor ownership, player reservation, death reward, missile/controller, mover fallback, invulnerability expansion and weapon-event paths. This peer review did not read every unchanged body in those files.

Found an additional confirmed high-severity lifetime defect: `set_origin` retained a Q3 actor pointer across an external world body read. Its callback could restore the Q3 actor array while keeping the canonical actor and observed body valid, leaving the helper with freed storage. Session and combat idleness did not cover that world callback. The Q3 author corrected it as QG06: checkpoint restore and provider destruction now require `qa_world_idle`, and `set_origin` reacquires the original full missile identity after observation. Re-read those corrections and matched all ten frozen source hashes against `review-20260929-q3-gameplay.md`; `git diff --check` passed for the packet. No further confirmed defect was found in the bounded correction packet. Whole-provider lifetime and all callback paths remain outside this acceptance; no runtime verification was performed.

## Open concerns and unfinished scope

- Other direct public gameplay entrypoints and native service callbacks have not all been qualified for whole-provider teardown. The corrected actor action and observed world/combat paths do not establish a universal native operation lease.
- Widow `q2m_widow_powerups`, `q2m_widow_power_think` and `q2m_widow_clear_powerups` have declarations and definitions but no gameplay callers. Source-wide multiplier consumers are also absent. These are preexisting incomplete baseline implementation, not completed by this review.
- Corpse and Medic code received the bounded reads listed below; this is not exhaustive edition/species, gameplay, expansion or campaign parity evidence.
- Application composition, native module ownership and aggregate checkpoint publication remain original open baseline obligations.

## Frozen correction hashes

| Path | SHA-256 |
|---|---|
| `src/gameplay/q2/game.c` | `c43eb142a11f4982ef29099aff7d3f4064744dc806e67c20684f483a5a79ba92` |
| `src/gameplay/q2/monsters/core.c` | `1f8f03f75affb6d3751245fa5b7cabb22f7340a937213af2f69ff2342bf3f62d` |
| `src/gameplay/q2/monsters/checkpoint.c` | `24fd56dbad480cbb91adb78cfdd0847af53e620f4275497d29ca2d706cdaeb0b` |
| `src/gameplay/q2/monsters/beam.c` | `bdf904685cda9a4b3643ac1ebcc38886e4c6c6fca2d25303d858343c78004c7c` |
| `src/gameplay/q2/monsters/widow_power.c` | `0bed5bad85ac463dd8d3151ee245163823f944af1708adaea3b58f5f7fa2427d` |
| `src/gameplay/q2/entities/state.c` | `937d2a2b08ea57e7f273d6672296eca36be8c4323e17e48087a6dd7bc1a99455` |
| `src/gameplay/q2/entities/lifecycle.c` | `425056d01468bfc05aac9870a1713d7dd7c696cff17ec2df4cdd80489cb343b5` |


---
Source report: docs/audit/review-20260929-q3-gameplay.md
Selection: complete opening findings, repair/verification and scope sections; any later coverage inventory/peer appendix remains in referenced file.

# Q3 and shared gameplay source review, 2026-09-29

This is a bounded source review of existing code. No engine build, compiler, test, parser, executable, sanitizer, benchmark or gameplay run was performed. Findings below are confirmed by source control flow, not runtime reproduction. This packet does not complete AUDIT, any game-family task, or BASELINE.

The worker has no exposed external session UUID. No Jev session identity was invented. The coordinator owns the existing AUDIT ledger contribution and must record the handoff.

## Confirmed fixes

| ID | Severity | Source | Failure scenario and change |
|---|---|---|---|
| QG01 | high | `src/gameplay/inventory.c:425`, `src/gameplay/operation.c:56`, `include/qa/operation.h:37` | A hook admission prepared on an exposed inventory operation makes operation destruction reject teardown. Inventory destruction formerly ignored that rejection, freed the inventory and returned success. All four operations now receive the same nonmutating destruction preflight before any operation is freed. Pending tokens remain usable after rejection. |
| QG02 | high | `src/gameplay/combat.c:459` | Final external armor validation and write were chained without checking actor/binding/protection ownership between callbacks. A successful validation callback could retire or replace the binding; the subsequent old writer would still run. The exact existing ownership check now separates validation from write. |
| QG03 | medium | `src/gameplay/q3/checkpoint.c:151` | A player admission reservation could remain open while checkpoint restore replaced the player array. This violates the documented begin/commit contract. Restore now rejects any pending player reservation before allocation or publication. |
| QG04 | high | `src/gameplay/q3/death.c:68`, `src/gameplay/q3/missiles.c`, `src/gameplay/q3/movers.c`, `src/gameplay/q3/player.c:1064` | Several cross-actor observations retained pointers to the current Q3 slot. A foreign combat/body/mover callback could retire the current actor and reuse its slot while the observed actor remained valid. Death rewards could credit the replacement killer; missile launch/impact/grapple/mine attachment and triggering could update replacement state; mover fallback could update a replacement player; invulnerability expansion could update a replacement moving player. These changed paths reacquire the original full actor ID after observations and stop when it no longer has the required Q3 kind. Expansion also reacquires state in its caller and returns movement removal. |
| QG06 | high | `src/gameplay/q3/checkpoint.c:145`, `src/gameplay/q3/game.c:189`, `src/gameplay/q3/missiles.c:227` | Independent Q2 review found that an external world body callback could restore the complete Q3 actor array or destroy the provider while a missile helper retained its old actor pointer. Session and combat idleness did not reject a world callback. Restore and destruction now require world idleness, and the origin helper reacquires the original missile after body observation. This correction uses the world reviewer's new `qa_world_idle` API. |
| QG05 | medium | `src/gameplay/q3/player.c:642` | The fire event sink can retire the firing actor. Arsenal execution then called `qa_q3_fire_weapon`, which rejected the missing actor and turned an allowed retirement into a failed command. Arsenal execution now reacquires the original player after the event and ends the retired command successfully. |

QG04 covers the particular changed paths, not every possible Q3 callback path. The code uses retained arrays indexed by full actor identity; checking the observed actor alone does not validate the other actor whose cached pointer will be used.

## Scope and confirmation

Read the full current bodies for the files marked below. Shared world body-read and combat binding guards were also traced so a callback failure for the observed actor was not confused with retirement of a different actor. The Q3 donor `game/weapon.ts` and `game/hitscan.ts` were read for accuracy ordering. Lightning and shotgun intentionally evaluate accuracy after damage in that donor, so this review did not change that behavior. Q3 item spawn, item completion, projectile, corpse and holdable control flow received source review; this is not an exhaustive game-parity claim.

`git diff --check` passed for the changed source paths before freeze. The complete current patch and the hashes below identify the reviewed packet. The foundations reviewer independently accepted QG01 and QG02 after reading complete lifecycle and ownership contracts. The Q2 reviewer found QG06 and accepted its correction after checking all ten updated source hashes, all changed hunks and affected paths. No further confirmed defect was found in that bounded packet. Unchanged source bodies were not exhaustively reread by that peer. Builds and runtime verification remain deferred by the baseline phase rule.

## Frozen source hashes

| Path | SHA-256 |
|---|---|
| `include/qa/operation.h` | `85ff155e3975110127a9fb19c710036cad12e6289318751edd3e54ae5e83389e` |
| `src/gameplay/operation.c` | `c69338daad24b6ddd89680a2497bfa0e8a4374aff8b86ce613177ad8145d9af7` |
| `src/gameplay/inventory.c` | `fe6f355a2c9dca9e6c30ad1859452672e44adf9dbe77baf36a9347c5538573d8` |
| `src/gameplay/combat.c` | `63a57835dd2c44d091482985d172f51381328148e45401434f60565b2050422f` |
| `src/gameplay/q3/checkpoint.c` | `a75796b2112e371d74ad2895bf500f80067c36e2a9199e9915885e18f8bb60be` |
| `src/gameplay/q3/game.c` | `84e0ef89ac21e1b9013ea2eb5ce87fe8399926aaffa4429031d276e1b1797fe7` |
| `src/gameplay/q3/death.c` | `b9daffebdc9c4fb1fefb80840ef812f2fc819b15f13a1e489022b96c45dcaf9c` |
| `src/gameplay/q3/movers.c` | `f91dbfa2831c05ebf9a9849cc99722c55110d8352b5cad16870f1d96b687155a` |
| `src/gameplay/q3/player.c` | `9e8dc792fc7bce2c055b443ab313f3a7a485b1e17b3b92ab3b2f6f941ff945ef` |
| `src/gameplay/q3/missiles.c` | `af177f4321fc28bb2ae4ce937d6dfe70def962bbc7b3b83833125844e0353350` |


---
Source report: docs/audit/review-20260929-bots-compat.md
Selection: complete opening findings, repair/verification and scope sections; any later coverage inventory/peer appendix remains in referenced file.

# Bots and compatibility review, 2026-09-29

This review addresses existing code only. It preserves the unfinished implementation already in the worktree. Four defects were traced and repaired. The native destroy admission repair was tightened after independent coordinator review. All findings below are CONFIRMED by source control flow, without execution. The baseline policy prohibits builds, tests, parsers, executables and benchmarks in this phase. This document does not accept AUDIT, B26, B23, B24, B25 or BASELINE as complete.

## Findings and repairs

| Severity | Location in repaired source | Failure trace | Repair |
| --- | --- | --- | --- |
| High | `src/compat/q3_host/host.c:48`; `bot_movement.c:95` | MoveToGoal takes a borrowed movement owner, then clears observed guest result fields before the movement operation marks that owner active. An observed write can invoke BOTLIB_SHUTDOWN, freeing the movement owner. The outer call then checks the freed state table. Other bot adapters also read observed source arguments while holding borrowed owners. | A runtime owner lease spans ordinary game bot dispatch. Shutdown, destruction, setup, map attachment/loading and level-item reinitialization reject leases or active child owners. Lifecycle dispatch is outside its own lease. Map loading retains its owner while reading the observed name, ends that lease, then performs the guarded mutation. Nested ordinary calls can retain another lease. |
| High | `src/compat/q3_host/game_records.c:259` | A body write checked actor identity once per three-component vector. A write observer can retire actor A and rebind that source slot to B after the x write, after which the old call writes y and z into B's source record. The playerState ABI writer similarly emitted many observed words without checking actor/table identity after each word. | A body-write scope captures the full actor ID and entity/client table descriptors. It checks them before and after every source write, including each ABI playerState word. It also checks after observed record reads and the player-velocity callback. |
| Medium | `src/compat/q3_host/bot_chat.c:93` | Native `qa_native_read_string` reports content length excluding the terminator. Native synonym snapshots subtracted one again. A nonempty string lost its final character; an empty string produced SIZE_MAX and failed the bot text boundary check. | The native snapshot uses the full content length. The separate QVM branch still finds and excludes its terminator explicitly. |
| High | `src/compat/native_host/host.c:237`; `checkpoint.c:211` | For the runner backend, host destruction freed the host after `qa_native_destroy` returned false even when that false result meant an active instance was retained. A native region callback can ask to destroy its host while the instance is executing. Checkpoint/restore callbacks could also destroy the host while the outer operation continued reading it. | Use the shared `qa_native_can_destroy` predicate, which checks active execution, callbacks, checkpointing and destruction. `qa_native_destroy` uses the same predicate. Public native host checkpoint and restore retain the callback owner across all internal return paths, so nested destruction is rejected. Ordinary late runner shutdown failures retain the prior cleanup behavior. |

The runtime lease also guards BSP entities used across observed output writes and key comparisons. It retains ownership rather than copying every bot resource. Ownership operations 200, 201, 206 and 541 use their own mutation guards. The 206 source-name read has a short lease with one release on either read result. Every leased dispatch handler result and the unhandled dispatch exit release their lease.

## Refuted candidates

- Two MoveToGoal result clears are intentional donor behavior. The TypeScript syscall clears six fields, then the travel controller clears those same fields again for admitted nonnull goals.
- An oversized Q3 maximum client count cannot reach the fixed source-slot array through normal creation. `qa_q3_host_create` already rejects values above 64.
- Q3 console-message output copies its 256-byte text into a local value before observed writes. It does not retain a dangling console-cell string.
- The runner checkpoint capture assignment was checked against the current source and HEAD. Both assign `runner_request` to `received`; no assignment repair is part of this packet.
- A native string empty-input failure does not by itself prove an out-of-bounds read. `replace_synonyms` rejects SIZE_MAX before allocation/copy. The repaired defect is lost input and false failure.


---
Source report: docs/audit/review-20260929-presentation-app.md
Selection: complete opening findings, repair/verification and scope sections; any later coverage inventory/peer appendix remains in referenced file.

# Presentation and application review, 2026-09-29

This contributor reviewed existing code and fixed the confirmed defects below.
The work preserves the existing uncommitted application, material-order and Q3
presentation implementation. It does not claim completion of AUDIT, BASELINE,
application integration, or runtime qualification.

Model: GPT-6.1 Sol, high effort. No descendants were spawned.
The runtime did not expose a trusted external session UUID to this contributor,
so no Jev session identity or claim was invented. The coordinator tracks this
contribution under AUDIT work `w_357ddd74810a4aa8a3bb84fdd7095fb8`.

## Confirmed defects and repairs

| ID | Severity | Current location | Failure trace | Repair |
|---|---|---|---|---|
| PA01 | High | `src/app/application/lifetime.c:155`, `owner.c:381` | A safe faulted session frees itself and returns false from `qa_session_destroy`. The application formerly retained that freed pointer after the error, so retrying application destruction accessed the freed session. | Use the session owner's new `qa_session_destroy_ready` predicate before cleanup and again immediately before destruction. Once ready destruction is called, clear the consumed session and world pointers before handling the error. A later application-destroy retry finishes remaining resources with a NULL session. Admission or active-call rejection leaves the session live. |
| PA02 | High | `src/app/application/control.c:1120`, `services.c:852` | The first cutscene update saved the original movement mode. The second update sent RESET through the generic control discontinuity callback, which cleared `saved_mode_valid`; the update froze movement again, so cutscene end could not restore the original mode. | Split motion-record publication from ordinary control-reset synchronization. Cutscene publication uses the shared record helper and performs its own existing pose update. Ordinary builtin discontinuities still call both publication and control synchronization. |
| PA03 | Medium | `src/app/application/control.c:1175`, `1205` | Ending a cutscene restored `view_offset` but left `view_height` at the cinematic height. The control camera view exposed both fields, so its height remained inconsistent after release/reset. | Restore `view_height` from the saved view offset alongside restoring that offset. |
| PA04 | Medium | `src/app/application/control.c:821` | Frozen commands bypassed dialect and increasing-sequence checks. A stale command could lower `command_sequence` during a cutscene, allowing a previously processed sequence after release. | Apply the existing dialect and sequence admission before the frozen-command branch. |
| PA05 | High | `src/app/application/control.c:864` | A direct movement call outside session advancement left the application IDLE while retaining mutable control, provider and result pointers. Synchronous movement callbacks could enter application lifecycle operations while that outer call was live. | Hold APPLICATION_ADVANCING over the movement mutation interval, reject calls during other lifecycle operations, and restore the exact prior IDLE/ADVANCING operation on the single exit after acquisition. |
| PA06 | Medium | `src/presentation/q3/audio.c:135` | Listener contribution discarded the supplied entity and always used QA_AUDIO_NO_ACTOR. The shared Q3 mixer therefore missed its personal-sound full-volume branch and listener-specific voice limit for sounds emitted by that player. | Resolve the listener through the same canonical `audio_actor` callback as positional and looping sounds before contributing the listener. |
| PA07 | Medium | `src/app/application/match.c:83` | The mode player-view hook returned shared body angles. Q2/Q3 movement commits body pitch as zero while retaining actual view pitch in the canonical control record. LMCTF flag/rune drops therefore used a flat forward vector when the player looked up/down. | Prefer the generation-matched control view angles and keep body angles as the fallback for actors without admitted controls. |
| PA08 | High | `owner.c:386`, `lifetime.c:117`, `composition.c:11`, `publication.c:374` | A direct world body-binding callback can run while the session itself is safe. Application teardown/configuration could otherwise retire the session/actor owners while the enclosing world callback still retained them. | Use the world owner's new `qa_world_idle` callback/visit predicate in application destruction, both finalizer prechecks, configuration safety and publication preparation. This preserves the existing application serialized-operation contract. |
| PA09 | High | `src/media/cinematic.c:436`, `467`, `531` | Pause, time query and checkpoint capture called the caller-owned clock while `movie->busy` was false. A `clock.sample` callback could destroy that same movie, then each resumed method read or wrote freed movie state. | Hold the existing movie busy guard through each operation and every clock callback. A private capture body lets all allocation/decoder failures return through the guarded public wrapper. Tick/end already used this guard. |

PA01 and PA08 use narrow world/session predicates implemented by the independent
world/network reviewer. That reviewer confirmed the application lifetime,
cutscene, move-lease and audio repairs by source inspection. Their inspection of
control.c was limited to the changed paths. Their later review also accepted
the generation-matched mode-player-view fallback. This contributor independently read
both world/session predicate diffs and their destruction bodies. The predicates
match the pre-existing destruction rejection conditions; faulted idle sessions
remain eligible, and NULL handling is explicit in each API.

The foundations reviewer independently read the full cinematic owner and its
clock/public/fullscreen contracts and accepted PA09 at SHA-256
`def3c1f6e574fb3f2a611997aa6536eabf6d2ac9d230d759eccd8825b5a5e1fc`.
The three methods acquire busy before the external clock and release it on
success and failure. Existing callback-free metadata getters remain available.
The helper retains capture's previous allocation cleanup and output publication.

Donor references read for the sound and mode defects:

- `../quake-typescript/src/compat/qvm/client-audio-syscalls.ts`, the complete
  CG_S_RESPATIALIZE dispatch that passes entity identity to `setListener`.
- `../quake-typescript/src/content/q3/presentation/client.ts`, listener contract
  and forwarding calls.
- `../quake-typescript/src/content/q2/multiplayer/lmctf/flags.ts:105` and
  `runes.ts:108`, the drop direction selecting retained player view angles before
  body angles.

The C consumers were traced at `src/audio/mixer.c:332`, `:739` and `:757`,
`src/gameplay/modes/objects.c:644`, and the body-angle commitment in
`src/app/application/control.c`. CIN's initially absent pending image was also
compared with `../quake-typescript/src/media/cin-playback.ts:101-140`; that
behavior matches the donor and was not changed.

## Coverage and verification limits

The companion `review-20260929-presentation-app-coverage.tsv` records every file
in this contributor's assigned directories and selected public headers, with
its inspection extent and SHA-256 at review time. It records 36 complete source
reads, seven bounded source reads, and 77 explicitly uncovered files. Complete
source read means the file was read; it does not assert every behavior was
qualified. In particular, much of the committed audio, media codecs, input and
renderer remains uncovered by this contributor pass. App map/services reads
were bounded, while the other application C files were read completely.

Shared material-order changes were read together with registration destruction,
failure rollback, generated replacement, frame sort preparation and library
lifetime paths. No confirmed additional defect was found in those paths. The
pixel-space picture helper now supplies +Z normals and its legacy image wrapper
restores zero normals before projection. GPU resource inspection was limited to
texture and mesh admission, caching, references and pruning in resources.c.

Only source reads, repository metadata, coverage metadata generation, SHA-256
and `git diff --check` were used. The whitespace check passed. No engine build,
configure step, compiler, parser, test, executable, gameplay, sanitizer or
benchmark was run. No original source file was copied, no sibling repository
was edited, and no per-file license or copyright notice was added.

Known missing production definitions of Q3 presentation render/picture/remap
and external application map/movement adapters are unfinished baseline scope.
They were recorded as coverage limits and were not filled in during this bug
review.


---
Source report: docs/audit/review-20260929-world-network.md
Selection: complete opening findings, repair/verification and scope sections; any later coverage inventory/peer appendix remains in referenced file.

# World, session, movement, navigation and network source review

Reviewer: `/root/review_world_network`, GPT-6.1 Sol, high effort. Date: 2026-09-29.

This is a bounded source review and repair contribution to the coordinator's AUDIT work. It does not establish complete subsystem coverage, runtime correctness, BASELINE completion or full game parity. No compiler, executable, test runner, parser, sanitizer or benchmark was run. Existing unrelated edits were preserved. Only the coordinator stages and commits.

## Confirmed defects and repairs

| Severity | Finding | Source failure trace | Repair ownership |
| --- | --- | --- | --- |
| High, CONFIRMED | Application retains a consumed session after cleanup failure. | `qa_session_destroy` rejects unsafe calls/admissions before mutation, but an admitted call clears actors/components, closes the world and frees the session even if the recorded fault makes its result false. The former application finalizer treated every false result as an unconsumed owner, allowing retry against freed storage. | This contribution exposes `qa_session_destroy_ready` in `src/session/session.c:746` and documents consumption in `include/qa/session.h:78`. The query shares the destroy admission predicate. The application reviewer owns pointer clearing and retry handling in `src/app/application/lifetime.c` and `owner.c`. |
| High, CONFIRMED | A direct world callback can destroy a gameplay provider while its caller still uses it. | Q2 reviewer supplied `qa_q2_monster_action` -> `q2m_refresh` -> `qa_world_body_read` -> external binding `read` -> `qa_q2_destroy`. Outside a session actor turn, the session and Q2 local actor guards are idle. Returning from the callback reaches `q2m_alive` with a freed game. Independently read that call path and the world callback-depth increment. | This contribution exposes `qa_world_idle` in `src/world/body.c:64` and shares it with world destruction. Q2 reviewer owns provider destroy guards; application reviewer owns application destroy/finalize/configuration/publication guards. The query excludes callbacks and spatial visits; geometry admissions retain their separate ownership contract. |

The session query accepts NULL, rejects session/scheduler activity and either admission set, and allows destruction of a faulted idle session. After it returns true, the immediately following admitted destroy consumes the owner even if a release callback records a new fault. There are no callbacks between the application's final query and its destroy call. The world query returns false for NULL, matching the existing combat idle convention; nullable application callers skip the query for NULL worlds.

## Independent repair review

Read the application's complete `lifetime.c`, `owner.c` and `composition.c`, plus actor-release/motion-recording sections of `services.c`, the publication readiness boundary, selected control movement/cutscene functions, and complete `src/presentation/q3/audio.c`.

The application session repair clears session/world pointers after an admitted destroy, retains the outer owner when cleanup reports an error, and skips session-dependent provider draining on retry. `application_fault` does not dereference the session. World-idle guards reject direct body-callback teardown before destructive composition work and again at finalization. The application reviewer independently reviewed both shared queries and reported no defect in the narrow interface changes.

The targeted control review found no confirmed defect in the movement operation lease, first-entry cinematic saved-state preservation, or view-height restoration. A suspected repeated-cinematic reset was refuted: `application_record_motion_change` records a continuation and does not call the cinematic reset helper. The audio listener now obtains the same canonical actor identity service used by positional and looping sounds before contributing the listener. This is a targeted peer review, not clearance of every application/presentation file.

Peer source anchors: `owner.c:381` public destroy readiness; `lifetime.c:109` and `:155` finalizer readiness and consumption; `composition.c:5` configuration readiness; `publication.c:371` publication readiness; `control.c:862` prior-operation save and `:935` restoration; `control.c:1097` first-entry cinematic save; `control.c:1175` restored view height; `services.c:852` motion recording; `src/presentation/q3/audio.c:135` canonical listener mapping. `match.c:83` now prefers generation-matched admitted control view angles and otherwise reads the authoritative body; this narrow function was also reviewed.

Peer file SHA-256 snapshot after the final world-idle guards were read:

```text
47d2e312c18a8a21dccb08964d34bd16d4b4036a07382bd497341905cc8c7030  src/app/application/lifetime.c
a094e83a432af0f14c8b71f5408970ebb7d3a8ec7597aab527c9681e36f3cf59  src/app/application/owner.c
447ddb4d7e5dcfb3dfdf1af37be6ef8453c8aa832335e928a205d2f2048cccbb  src/app/application/composition.c
affb4184a689016f8c52db4f95739ab316651c14c77382bfabf46192d5653cb1  src/app/application/publication.c
8219912caa65972e40c02006759732732b702849c506d61e67fcbfdb2cdf5655  src/app/application/control.c
51c9ed85345a358d5024941c01df2bc48b99082e83cd93c10ddd2f710d2c8549  src/app/application/services.c
7af28936aad25b913570a2be20e014b2e07d9b371c118eb81c73f8619c77cf4f  src/presentation/q3/audio.c
```

## Other refuted findings

- Q3 mover rollback saves a yaw word through a float. The donor explicitly uses `f32(check.client.ps.deltaAngles.y)` for that saved field, so the conversion alone is not a demonstrated port regression.
- Q1 toss/step water processing returns early on unobstructed movement. The donor physics path also returns when the trace fraction is one before its water-transition code. No behavior change was made on the basis of that suspicion.
- Q3 portal visibility retains an area-mask accumulator. Its callback contract explicitly ORs into the supplied accumulator; clearing it between portal recursion would lose accumulated visibility.
- Q2 frame baseline lookup assumes sorted unique baseline entities. The public frame contract requires that ordering; absence of repeated per-lookup validation is not a defect.
