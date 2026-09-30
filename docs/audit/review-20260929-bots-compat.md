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

## Inspection coverage

Read and traced complete current bodies in these files:

- `src/bots/runtime/{core,handles,variables,characteristics,observations}.c` and `internal.h`.
- `src/bots/source.c`, `src/bots/navigation/source.c`, `src/bots/movement/state.c`.
- `src/compat/q3_host/{host,memory,bot_library,bot_chat,bot_weapons,bot_movement,bot_goals,files,game_records,game_spatial,game_checkpoint,checkpoint}.c`.
- Public `bot_runtime.h`; native host checkpoint encoder/decoder and lifecycle branches needed by the findings.

Focused reads rather than complete reviews:

- `src/bots/runtime/setup.c` callback adapters and setup entry/exit; the middle asset-loading helpers are not reviewed in full here.
- `src/bots/library/weapons.c` selector allocation, destruction and evaluation.
- `src/bots/chat/state.c` retention, destruction, console records and message delivery; `strings.c` external matching and synonym snapshot/copy paths.
- `src/compat/native/{memory,region,instance,checkpoint,direct}.c` relevant string, teardown, active callback and checkpoint paths. Some combined tool output was truncated, so these are not claimed as full-file coverage.
- `src/compat/native_host/{memory,q3,host}.c` guest string contract and lifecycle/import paths.
- `src/compat/qvm/{memory,records}.c` checked ranges, observed writes and ABI playerState writer.
- `src/world/body.c` shared binding callback and post-callback identity checks, read only.
- TypeScript donor `src/compat/qvm/bot-navigation-syscalls.ts`, `src/bots/behavior/q3/navigation.ts` and `travel/controller.ts` MoveToGoal ordering, read only.

The rest of `src/bots/` and `src/compat/` is outside completed inspection coverage for this packet. In particular this is not a full audit of bot AI, all travel algorithms, QC execution, QVM instructions, foreign native FFI/runner protocol, ABI signatures or all native host imports. No new functionality was added to close those gaps.

## Evidence and handoff

`git diff --check` exited successfully after the repairs. No compilation or runtime test was run. The 12-source-file SHA-256 manifest is `review-20260929-bots-compat-manifest.json`. The repair-only unified diff is `review-20260929-bots-compat.patch`. The before text for existing untracked files was reconstructed by reversing only this review's exact edits; it is not an independently captured initial snapshot. The four previously clean tracked native/native-host files and header use HEAD as their before text.

The coordinator independently accepted the final 12-source repair packet after reading the source and re-reviewing the native destroy predicate. This acceptance covers the bounded repairs and their source traces; it does not establish runtime correctness or completion of the broader workstreams. The first native host destroy guard checked only active execution and was rejected in peer review. Coordinator review identified runner checkpoint region callbacks with checkpointing active and execution depth zero; the final shared predicate covers that case too. All prior uncommitted bot/runtime/Q3-host work remains present. No Git staging or commit was attempted because the coordinator owns Git and the current environment makes `.git` read only.

Jev instructions were read. The worker runtime did not expose an actual external session ID, so this worker did not invent a session or claim an independently registered ledger contribution. The coordinator's AUDIT contribution is `w_357ddd74810a4aa8a3bb84fdd7095fb8`, session `s_68a7c43280594f24b1cc6f98bd5aab9d`, plan revision 7. The coordinator can attach this bounded repair evidence to that contribution. Automatic hook delivery is not verified by this packet.
