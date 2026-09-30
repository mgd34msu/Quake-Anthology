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
