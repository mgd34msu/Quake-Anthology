# Compatibility, navigation, networking, and persistence audit

The assigned source-body reading, independent re-review of identified corrections, and direct Jev criterion judgments are complete. The report is ready for coordinator integration. All ten reviewed B22-B31 tasks remain incomplete. This audit does not establish baseline completion or runtime correctness.

The review starts at Git `c6f7db5c4715416393d63af7e5c994ba59194caf`, with the existing dirty working tree frozen by the coordinator. The ledger is plan 6, AUDIT revision 2; contribution `w_850200fe3fed40f2a2a36c59f0cd3dac` belongs to session `s_e46c6f4c48754151b1c3a3ca289c1faf`. Source reads, text searches, and Jev report checks are permitted. No project configuration, compilation, test execution, parser execution, executable run, sanitizer, benchmark, or gameplay evaluation has occurred in this audit.

The original task goals and criteria remain those in `docs/dependencies.json` (reviewed SHA256 `915c4c50da8fcb60ed206f09f2d325eb7755f4c6f3f48b1c3d97d83d2eb98bae`), including B31 through line 474. A library API or a callback declaration is evidence of an implementation boundary; it is not evidence that the required application consumer exists. The executable still links only `qa_content` (`CMakeLists.txt:574`) and `src/main.c` exposes archive/BSP inspection. That makes application integration open, independently of individual library quality. The coordinator subsequently authorized this worker's focused C22-2/3, C24-5/6/7/9 and C27-1/2 corrections. Native-host layout correction C24-10 remains coordinator-owned. The final source inventory was checked with HEAD `2a7d5a1397ea169c963b23593ba84c360a926ab2`; this includes a dirty tree, so finding-specific hashes below identify reviewed corrections more precisely than HEAD alone.

## Existing criteria and current evidence

| Task | Existing criterion | Source finding | Acceptance |
|---|---|---|---|
| B22 | Original QC roles and required extensions connect to shared services; built-in gameplay remains native C. | Interpreter, private instances, several shared-world builtins and checkpoints exist. The complete engine host and composition consumers do not. | Open. |
| B23 | Required QVM semantics and source profiles work through shared engine boundaries without another world. | Interpreter, memory observers, intrinsics, profile classification, records and checkpoints exist. Concrete game/cgame/UI hosts are absent. | Open. |
| B24 | Required binary callbacks, private instances, composition, and saved state are implemented; no default port of TS CPU machinery. | Native executor and host boundaries exist. Source corrections and missing application/composition consumers are recorded below. | Open; no runtime qualification. |
| B25 | Actual module adapters compose through common operations; enable/disable, private state, and rollback are implemented. | Executor continuation APIs and detached configuration ownership are not the missing actual per-module composition adapters. | Open. |
| B26 | Bots use ordinary engine actions and selected gameplay; foreign maps have usable navigation implementation. | Navigation and bot support libraries exist. The player-bot decision/action owner is not yet present. | Open. |
| B27 | Explicit codecs preserve required dialects; one connection authority serves native and mixed sessions. | Transport, connection ownership and dialect codec bodies were read. Two inherited KEX reliability/admission defects were corrected; application consumers remain absent. | Open. |
| B28 | Native and Anthology session state is wired to gameplay with correct command ownership and replay behavior. | Protocol-side session/peer helpers and movement prediction primitives exist; the authoritative gameplay session consumer is absent. | Open. |
| B29 | Required acquisition/discovery/admin flows have production consumers and cleanup behavior. | Discovery and handshake packet primitives exist. No complete downloader/remount/browser/administration application owner is present. | Open. |
| B30 | Persistence retains all owners; legacy exports reject unrepresentable state; save/demo formats have explicit codecs. | Provider checkpoints and some demo/MVD/GTV codecs exist. No shared/original save owner, legacy export admission, autosave/recovery/journal/VCR application service is present. | Open. |
| B31 | Required local/player services have consumers; only the previously approved unavailable ranking backend remains an extension point. | Gameplay achievement events exist elsewhere; no persistent player-services/progression/lobby/ranking-provider owner was found in the current source inventory. | Open. |

## Findings established so far

### C22-1: Required QC host services and application consumers are absent

`src/compat/qc/builtins.c:135` admits eight builtins from the shared session/world: setorigin, setsize, spawn, remove, traceline, findradius, droptofloor and pointcontents. The rest require supplied bindings. `src/compat/qc/instance.c:1391` implements that limited dispatch and returns `QA_ERROR_UNSUPPORTED` for an unbound host builtin. The public requirements in `include/qa/qc.h` include setmodel, sound, precaches, commands/cvars, protocol writes, walkmove/movetogoal, rerelease prompts, bot/path services and more. No concrete engine adapter supplies their complete binding table. Repository-wide call search finds `qa_qc_instance_create` only at its definition. `docs/integration.md:78` separately records the same outstanding host, lifecycle, shared-state and composition consumers.

The donor comparison is its QC engine host and source-provider responsibilities, not a demand to reproduce TypeScript memory machinery. Built-in Q1 gameplay remains native, as required. External QC gameplay still needs actual host services, actor lifecycle/callback admission, declared combat/inventory/pickup projection, protocol destinations, travel and safe restoration.

### C22-2 and C22-3: Shared QC spatial builtins change source admission

Severity: medium. `src/compat/qc/instance.c` maps every nonzero traceline mode except exact 2 to `QA_Q1_MOVE_NO_MONSTERS`. The donor truncates the argument and maps only 1 and 2 specially; every other value uses normal movement (`../quake-typescript/src/compat/qc/spatial-host.ts:55`). Mode 3 and mode 0.5 therefore produce different collision queries.

The same C file's pre-fix `findradius` obtained collision descriptors but never read projected `solid`. The donor explicitly excludes `solid == 0` at `spatial-host.ts:69`. A shared collision descriptor's presence does not establish the source field's value. The authorized correction truncates the traceline mode before matching 1/2 and checks `solid` through `qa_qc_entity_float` both while collecting references and before chaining results. That read refreshes foreign projection and rechecks its binding; the existing actor/body generation checks remain. Corrected `instance.c` SHA256 `d0b905851d4fc4eabc8b5a44d6d523a0099ee32d24f587bded1d21c0a18a197a` was independently reviewed by the coordinator and committed in `d728f87`. No builtin was executed.

### C23-1: QVM roles currently stop at the syscall callback boundary

`src/compat/qvm/intrinsics.c:138` classifies modern and 1.16n profiles, implements common math/memory traps, and delegates remaining traps through `options.syscall`; its final branch rejects an unbound host. `src/compat/qvm/module.c:15` constructs private RAM and a role/API profile, while `compatibility.c` retains declared primary/equipment interfaces. None is a concrete game, cgame, UI, equipment or combat host. `docs/integration.md:87` explicitly lists those missing adapters. Repository-wide constructor search finds no production caller of `qa_qvm_create` outside its implementation.

The donor QVM image reader accepts magic `0x12721444` (`../quake-typescript/src/compat/qvm/image.ts:9`), matching the C reader; the absence of a second magic is therefore not reported as a donor regression. Host integration is the established missing requirement.

### C26-1: Written bot resources are not yet a player-bot core

The current `src/bots` inventory contains chat, fuzzy weights, character/weapon/item support, perception and RNG. It has no session player-bot controller that selects enemies/goals, issues ordinary commands, manages objective/team orders, or owns bot lifecycle/checkpoint state across selected arsenals. Those remain necessary to satisfy B26 even if each support library is correct. The existing source inventory and `CMakeLists.txt` also show that the untracked bot support sources are not yet registered as a target.

### C24-1: Native Q3 UI commands use game lifecycle numbers

Severity: high, confirmed by source. `src/compat/native_host/host.c:123` admits all three QVM roles, including `QA_QVM_UI`, then its `qa_native_host_q3_vm_call` forwards source command words to `qa_native_call`. However `src/compat/native/instance.c:208` (`entry_allowed`) requires command 0 first for every vmMain module; `qa_native_call` changes every module to initialized on command 0 and to shut down on command 1. `qa_native_shutdown` likewise sends command 1. These are game/cgame command numbers. The source UI contract is `UI_GETAPIVERSION=0`, `UI_INIT=1`, `UI_SHUTDOWN=2` (`../quake-typescript/src/compat/qvm/abi.ts:825`, called by `ui.ts:53` and `ui.ts:55`). Consequently a UI API query incorrectly initializes the executor, the subsequent UI_INIT retires it, and later UI calls fail. Destruction can send UI_INIT instead of UI_SHUTDOWN. The executor options need the actual vmMain role and role-specific lifecycle transitions; the host initialization/shutdown callsites and checkpoint identity must preserve that role.

The coordinator's correction was independently re-reviewed through options admission, lifecycle transitions, runner load encoding/decoding, executor checkpoint identity, and host checkpoint identity. A follow-up caught unchanged host convenience calls; the corrected host now has typed cgame and UI initialization, rejects those roles at the game initialization helper, and uses UI shutdown command 2. The described lifecycle path is closed in source. Reviewed hashes: `native/instance.c` `e6d195bbbe1ec8064b15d330c115c824ec083bc2a714960ae1bf5566f624ca14`; `native/checkpoint.c` `5ed6e1ca6fbfe968e9c27c4d0574c7dbe8e659ed39b3ac5aab4bcfae2518bced`; `native_host/host.c` `fc354e164757aafb6b379dcb85aac1629c6bd6b11357f62b9a4caa79e7944a65`. Runtime role qualification remains unperformed.

### C24-2: Nested host checkpoint lengths can wrap before span construction

Severity: high, confirmed from the pre-fix source. `src/compat/native_host/checkpoint.c:314-323` added three untrusted lengths without checking addition. On a 64-bit host, a header-only record with zero slot/retained/cvar counts, `message_size=1`, `engine_size=UINT64_MAX`, and `bridge_size=0` passed the wrapped total-length equality. The subsequent span construction and message copy accessed outside the supplied record. The executor's outer checkpoint decoder checks its own lengths but does not validate this nested host record.

The coordinator corrected the nested reader and record sizing. Independent source re-review of SHA256 `ca9e9ceb04cac12b617905d4589a31c64857ca30a49234a040c0a1345ab9092e` confirmed sequential subtraction of each payload length before constructing spans, division-based record-count bounds, and separate cvar string bounds. The described overflow path is closed in source. This is not an executed malformed-checkpoint test or acceptance of the complete restore lifecycle.

### C24-3: Native host code uses nonexistent shared bounds fields

Severity: high, confirmed from declarations and uses. `include/qa/math.h:9` defines `qa_bounds` with `mins` and `maxs`. The native host uses `bounds.min`/`bounds.max` at `movement.c:195-196`, `q2.c:183-184`, and `world.c:264-300,466-467`. No aliases were found. These accesses do not match the shared C API and require correction before this source can compile. No compiler was run to establish this finding.

The coordinator replaced these accesses with the actual shared members. Independent source re-review confirmed the corrected names in `movement.c` (SHA256 `b6e8e72f242c69d897c25be22665f92a22be41a6a77952b1af9fcabf423ec555`), `q2.c` (`7ce79c9a7fa1c665c947c6e6e4db0fef8987f10044250962dc58295c107fe4d2`) and `world.c` (`650f463abc72a4c07a84bb31ccf9425c9f88f300c2dff621aed92c2b957ad4b9`). The declaration mismatch is closed in source; this is not compiler validation.

### C24-4: Borrowed Q2 projections can overwrite or unlink another provider's actor

Severity: high, confirmed from source. `src/compat/native_host/world.c:116-137` binds an existing projected actor as `QA_NATIVE_SLOT_BORROWED`. `native_host_link` at line 348 subsequently writes its canonical body, changes its collision family to Q2, and links or unlinks it without checking that binding kind. `native_host_unlink` at line 511 likewise has no ownership admission. The rerelease donor host explicitly returns before either operation for a foreign actor (`../quake-typescript/src/compat/q2/rerelease/host.ts:404-431`). An external rerelease module can therefore change another selected provider's authoritative actor merely by linking its foreign projection. The host needs explicit borrowed-projection write admission or the donor no-op rule; a second actor body would not solve the ownership violation.

Independent source re-review of the coordinator's correction (`world.c` SHA256 `24adb862cce1330a897822b161490354625138d22a719a3cd05e6ecc6160d4c3`) confirmed both calls return for borrowed bindings before canonical body or projected link mutation. The described ownership path is closed in source.

### C24-5: Linux native loading leaves the image base at zero

Severity: high. The pre-fix `native/direct.c:327` tested `defined(RTLD_DI_LINKMAP)`. Local glibc `dlfcn.h:135` declares that selector as an enum constant, so the preprocessor selects the zero-base fallback. `qa_native_rva` then returns a bare RVA; runner load admission also rejects the zero image base. The authorized correction uses the Linux platform condition already used for `<link.h>`; `_GNU_SOURCE` is defined before all includes. Reviewed correction SHA256 `5f0a58be5b7821c54fd1fe4afc8d2150528fc1b5d741a9122b660002310502d9`. No preprocessor, compiler or loader was run.

### C24-6: Runner entry admission compares against an unrevised i386 ABI

Severity: high. Pre-fix `native_runner_call` read the static profile entry signature, whose `SPEC` initializer always carries `QA_NATIVE_ABI_CDECL_I386`. `runner_arguments_ready` then required equality with the artifact target ABI, rejecting Linux i386/x86-64 and Windows x86-64 entry calls. `native_profile_prepare_remote` already publishes the correct target-specific signature in `instance->entries`. The authorized correction reads that binding through `native_entry` and removes the redundant static lookup. Reviewed `runner_host.c` SHA256 `3916ec86d5e523c899216769fe30ba1335f8149fa3c52c32e7fd7d7743abcad5`. Source path corrected; no helper execution.

### C24-7: Instrumentation memory opcodes differ from the runner protocol

Severity: high. Pre-fix `instrument_client.c` defined READ=4 and WRITE=5; the runner enum assigns EXPORT=4, READ=5 and WRITE=6. A suspended region therefore decoded a read as a write and rejected actual writes. Its separate version constant also needed the role-patch version change. The authorized correction moves wire magic, version, reply metadata and opcodes into dependency-free `native/wire_constants.h`, included by both `protocol.h` and the standalone DynamoRIO client. Reviewed hashes: new header `8c138dc82e525170fae57ed25ea086d729852aab307a0cde4319de5c475e98fc`; client `ef30920013582dea4687029a14c9a3e9cb68a3a6de8ee26768a92f210a605eac`; protocol header `ca3105c232e25810e9212e7b98869ecfeb6cf836b5b0e71e04681eec9023219f`. No instrumentation execution.

### C24-8: Checkpoint retained-client counting and writing use different bounds

Severity: high. `native_host/checkpoint.c:98-109` counts retained clients only within current `table.capacity`; pre-fix lines 180-183 wrote every retained client within `host->retained_capacity`. The native table can shrink, while host retained storage keeps its larger capacity. A formerly retained high slot therefore writes an uncounted word past the allocated record extent. The coordinator bounded serialization by both capacities. Independent source re-review of SHA256 `7368239771c94bdf85050cdfd942862e4fb408c1dcf76afb4e83c0cf43be7eec` confirms count and write now use the same admitted slot set. Source path closed; no executed checkpoint test.

### C24-9: Runner callbacks reject their own instance during module bootstrap

Severity: high. `qa_native_create_direct` calls `native_profile_bind` before publishing `*out`; that bind calls source GetGameAPI or dllEntry. The runner's previous import and syscall callbacks required their instance to equal `state->instance`, which is still NULL during this interval. Legal bootstrap imports were rejected, and aggregate result-byte lookup likewise had no instance. The authorized correction scopes the active instance over import, syscall-description and syscall dispatch, including nested memory requests, and restores the previous pointer on success and failure. A bootstrap attachment must match both the currently active native instance and the loaded module. Reviewed `runner_child.c` SHA256 `084c825f29a81206c7c16e8591f294b5166ebbe8777d62a125f7a80a9a56d320`; the coordinator independently reviewed and committed it in `4aadab1`. No module was executed.

### C24-10: The API 3 host uses i386 records for admitted x86-64 modules

Severity: high. The coordinator found this additional defect and requested independent layout enumeration. `native/profiles.c` admits ELF64 x86-64 API 3 modules, but the host's fixed public-edict prefix, trace output, Pmove record and cvar shadow encode i386 offsets and pointer widths. This worker confirmed the declarations against `../qsrc/quake-2/game/game.h:84` and `q_shared.h:316,445,520`; the donor classic host only admitted 32-bit modules, so this is a newly admitted ABI failure, not a reason to remove native 64-bit support.

The 64-bit checklist sent to the coordinator is: edict client 88, inuse 96, linkcount 100, area links 104/112, num_clusters 120, clusters 124, headnode 188, areanum 192/196, svflags 200, mins/maxs 204/216, absmin/absmax 228/240, size 252, solid 264, clipmask 268, owner 272, prefix size 280; trace surface 48, contents 56, entity 64, size 72; Pmove touchents 56 with 32 eight-byte pointers, viewangles 312, viewheight 324, mins/maxs 328/340, ground 352, water type/level 360/364, trace/contents callbacks 368/376, size 384; cvar name/string/latched 0/8/16, flags 24, modified 28, value 32, next 40, size 48. Entity-state 84 bytes, usercmd 16 and classic surface 24 have no pointers and retain their widths. BoxEdicts already uses artifact pointer width. The coordinator owns the correction. Independent re-review is recorded below.

Independent re-review confirmed the coordinator's C24-10 correction against that checklist and its callsites. `allocate_host` selects immutable classic32/classic64 descriptors once. `source_inuse`, reconciliation, link metadata, cluster writes and setmodel use the selected edict descriptor; trace FFI fields already use pointer-width-aware `QA_NATIVE_ADDRESS`; `encode_trace` and cvar allocation/writes now match the selected width. Pmove reads and writes the full 240/384-byte record, preserves source callbacks, strides touch pointers correctly, and propagates world-ground address errors. No remaining width mismatch was found in that bounded checklist. The coordinator committed the host integration in `2a7d5a1`; full B24 remains open. Reviewed SHA256 values:

| File under `src/compat/native_host` | SHA256 |
|---|---|
| `internal.h` | `6434bee87c2bb64943936df5bd49c18ffb39abf78411ed39c7a127bf6425b7cf` |
| `host.c` | `69953061cfa72504693fefbffc254200a7afce8a2a1116bc1d7b92265a0edb80` |
| `memory.c` | `55e585c3276f06557211f7dccdcf61d2ac462a55c76109ee4f26bb912f133248` |
| `movement.c` | `37e9cfb38a13f5b705778c5a1fa2a1b7544ecf5534e5e41011fcaef143a99b79` |
| `q2.c` | `1edccc16fd14af1676e218fab0f047877f0664ac46fc062f49eebf1aca2bce4d` |
| `world.c` | `1b76aa382fa2ce1a457485f3adc19779ecb5183eba3f360335d0cc699c926d20` |

### C27-1: KEX reliable acknowledgements lose packets across serial rollover

Severity: high. Pre-fix `network/q2/kex_channel.c` retired queued packets with `head->reliable <= ack`. With queued serials 65535 and 0, ACK 65535 retires both, including unacknowledged 0; ACK 0 cannot retire an outstanding 65535. Donor `network/q2/kex/channel.ts:52` has the same flaw. The authorized correction bounds outstanding reliable serials below half the 16-bit serial space and admits an ACK only within the actual queued window, with successful emission required for every cumulatively retired record. Failed sends remain queued for retry. Reviewed patch SHA256 `f0a69a6c65da471316220bbbb3f9bd4d91707f49eb1979024f548257274d2d94`; the coordinator independently source-reviewed this correction, committed in `657a333`. These cases were traced in source, not executed.

### C27-2: A lost KEX join reply can permanently reject later valid retries

Severity: medium. Host `network/q2/kex_lan.c:join` serializes the complete current roster on every reply. The client previously consumed only `first_local_index + local_player_count` IDs and then required EOF. If A's initial reply is lost and B joins before A retries, B's trailing ID makes A reject every retry. Donor `network/q2/kex/lan.ts:136-149` contains the same mismatch. The correction reads the complete bounded roster, validates unique nonzero IDs, checks that the local seat range fits, and only then publishes it. Reviewed patch SHA256 `548e35a679df79f17e16d68de39dd6d80f6998b8515024a1f8efd0746f9c74ea`; the coordinator independently source-reviewed this correction, committed in `657a333`. No LAN session was executed.

### C28-31-1: Protocol and provider primitives do not close application services

`src/network/q1/session.c`, Q3 client/server peer files and `src/network/unified` supply protocol-side state and envelopes. They do not connect authoritative gameplay commands, selected movement prediction/replay, remote travel, reconnect and mixed-session ownership to an application. `src/network/q1/demos.c`, Q2 MVD/GTV and provider checkpoint files are useful format/state pieces; there is no shared save transaction or complete recovery/demo player. No `src/save`, `src/demo` or `src/progression` directories exist, and the full `src` inventory contains no alternative application owner for the missing B29-B31 workflows. This is unfinished source scope, not the approved unavailable ranking backend.

## Jev judgments obtained

The actual plugin CLI was invoked as `check report --project quake-anthology --task B22` with the C22-1 source evidence, graph/snapshot identifiers, and an explicit statement that B22 and the ongoing audit were incomplete. It returned exit 2:

```text
claims-done overclaims=0.05
EXCUSE 0.59
stop-reason: 1 passages read
```

The flagged passage was the whole report, including its missing-host evidence and ongoing-review limitation. This result is preserved rather than called a pass. The check judged the report; it did not inspect the repository or establish that B22's implementation met its criterion. The later completed evidence and direct comparisons are retained below.

The same CLI checked B23 against its existing criterion using the complete QVM source review, the donor operation/record comparisons, and the missing concrete syscall/composition host evidence. Its output was:

```text
claims-done overclaims=0.12
REVIEW blocked-external 0.23
stop-reason: 1 passages read
```

The flagged passage was the complete source assessment, which explicitly kept B23 open. This is a report judgment, not acceptance of B23. The command's exit code was not retained in that tool output.

The CLI also checked B24 using the native executor/host source assessment and C24-1 through C24-8, keeping the binary-callback/composition/saved-state criterion open. It returned exit 2:

```text
claims-done overclaims=0.11
REVIEW blocked-external 0.59
stop-reason: 1 passages read
```

The whole source assessment was flagged. This is not acceptance of B24 or runtime qualification. C24-9 and C24-10 were discovered after that report and are included in the final evidence packet.

The remaining CLI report checks returned the following actual results. Each command returned exit 2 and `stop-reason: 1 passages read`. Each flagged its complete submitted assessment. These are report judgments, not checks that the implementation fulfills its graph criterion.

| Task | claims-done overclaims | Report disposition |
|---|---:|---|
| B25 | 0.05 | REVIEW allowed-by-rule 0.02 |
| B26 | 0.08 | REVIEW excuse 0.34 |
| B27 | 0.14 | REVIEW blocked-external 0.28 |
| B28 | 0.07 | EXCUSE 0.46 |
| B29 | 0.08 | EXCUSE 0.50 |
| B30 | 0.09 | REVIEW excuse 0.39 |
| B31 | 0.07 | EXCUSE 0.46 |

The B25 report identified missing actual module operation adapters despite private executors and configuration transactions. B26 identified the missing bot decision owner despite navigation/resources. B27 described the reviewed codec/dialect inventory and both KEX corrections. B28-B31 identified the disconnected application consumers in C28-31-1. All kept the original criterion open. The ledger history read after these CLI calls still contained the contribution claim and no task-acceptance verification; no ledger verdict is inferred from those CLI outputs.

## Direct task acceptance judgments

After the source corrections and final coverage inventory, ten task-specific evidence packets were submitted to `/home/buzzkill/.local/share/vibecheck-jev/judge-task.mjs`. The helper sends the unchanged graph goal and every criterion with the source packet to Jev and explicitly asks whether the work fulfills them. It does not ask merely whether this report is honest. The response model was `jev-1.13.0`; all ten responses chose **incomplete**, confidence 1, and all commands exited 2. No task passed. Each JSON preserves the exact question, graph hash, evidence path/hash, timestamp and returned readings; the service returned null request IDs.

| Task | Evidence packet | Direct response | Criterion 1 reading |
|---|---|---|---:|
| B22 | [packet](B22-compatibility-evidence.md) | [incomplete](B22-compatibility-acceptance.json) | 0.08 |
| B23 | [packet](B23-compatibility-evidence.md) | [incomplete](B23-compatibility-acceptance.json) | 0.03 |
| B24 | [packet](B24-compatibility-evidence.md) | [incomplete](B24-compatibility-acceptance.json) | 0.06 |
| B25 | [packet](B25-compatibility-evidence.md) | [incomplete](B25-compatibility-acceptance.json) | 0.03 |
| B26 | [packet](B26-compatibility-evidence.md) | [incomplete](B26-compatibility-acceptance.json) | 0.05 |
| B27 | [packet](B27-compatibility-evidence.md) | [incomplete](B27-compatibility-acceptance.json) | 0.08 |
| B28 | [packet](B28-compatibility-evidence.md) | [incomplete](B28-compatibility-acceptance.json) | 0.03 |
| B29 | [packet](B29-compatibility-evidence.md) | [incomplete](B29-compatibility-acceptance.json) | 0.03 |
| B30 | [packet](B30-compatibility-evidence.md) | [incomplete](B30-compatibility-acceptance.json) | 0.04 |
| B31 | [packet](B31-compatibility-evidence.md) | [incomplete](B31-compatibility-acceptance.json) | 0.03 |

The completed packets were also sent through the installed plugin's `check report --project quake-anthology --task Bxx`. Exact full outputs are retained in `B22-compatibility-report-check.txt` through `B31-compatibility-report-check.txt`; every command exited 2. These checks flagged source-only limitations and incomplete implementation passages as REVIEW/EXCUSE, rather than returning a report pass. Their claims-done readings, in B22-B31 order, are 0.11, 0.07, 0.09, 0.08, 0.06, 0.22, 0.06, 0.05, 0.05, 0.06. This second check used the completed packets, not an unchanged retry of earlier evidence. Earlier outputs above remain part of the history.

These direct responses are supplemental Jev service results, not built-in ledger verification records. The scoped audit contribution is handed off separately; the coordinator retains the original unresolved B22-B31 requirements. The detailed defects identified in this lane were corrected and independently re-reviewed, while the missing complete QC/QVM hosts, module composition adapters, player-bot core, connected connection owner and B28-B31 application services remain implementation work.

## File coverage

Every source and header named in the following table had its body read. Paths in the right column are relative to the directory in the left column. Source reading includes control flow, record ownership and exposed contracts; it does not mean exhaustive donor line-by-line equivalence, executed qualification, or approval of an entire subsystem. Targeted donor comparisons and corrections are recorded above. Fixed tables were visually read, not independently byte-compared. No assigned production source/header remains unread at this snapshot.

| Directory | Files read |
|---|---|
| `src/compat/qc` | `builtins.c`, `checkpoint.c`, `instance.c`, `internal.h`, `machine.c`, `memory.c`, `program.c` |
| `src/compat/qvm` | `compatibility.c`, `execute.c`, `image.c`, `internal.h`, `intrinsics.c`, `memory.c`, `module.c`, `records.c` |
| `src/compat/native` | `checkpoint.c`, `declaration.c`, `direct.c`, `ffi.c`, `image.c`, `instance.c`, `instrument_client.c`, `internal.h`, `main.c`, `memory.c`, `module.c`, `profiles.c`, `protocol.c`, `protocol.h`, `region.c`, `runner_child.c`, `runner_host.c`, `variadic.c`, `wire_constants.h` |
| `src/compat/native_host` | `artifact.c`, `checkpoint.c`, `host.c`, `internal.h`, `memory.c`, `message.c`, `movement.c`, `q2.c`, `q3.c`, `world.c` |
| `src/bots/chat` | `asset.c`, `checkpoint.c`, `construct.c`, `internal.h`, `parse.c`, `reply_parse.c`, `state.c`, `strings.c` |
| `src/bots/library` | `character.c`, `core.c`, `genetic.c`, `internal.h`, `items.c`, `structure.c`, `weapons.c`, `weights.c`, `weights_mutate.c`, `weights_parse.c` |
| `src/bots` | `perception.c`, `random.c` |
| `src/navigation` | `aas_file.c`, `aas_sample.c`, `aas_validate.c`, `asset.c`, `asset_internal.h`, `checkpoint.c`, `construct.c`, `eligibility.c`, `estimates.c`, `graph.c`, `graph_asset.c`, `internal.h`, `nav_file.c`, `prediction.c`, `route.c`, `runtime.c`, `spatial.c`, `train.c`, `travel.c`, `world.c` |
| `src/network` | `connections.c`, `dosbox.c`, `ipx.c`, `message.c`, `reliability.c`, `socket_private.h`, `socks.c`, `transport.c` |
| `src/network/q1` | `channels.c`, `checksum.c`, `commands.c`, `demos.c`, `discovery.c`, `handshake.c`, `history.c`, `netquake.c`, `quakeworld.c`, `scalar.c`, `session.c`, `token.c` |
| `src/network/q2` | `channel.c`, `checksum.c`, `classic_internal.h`, `codec.c`, `connectionless.c`, `fog.c`, `frames.c`, `gtv.c`, `handshake.c`, `internal.h`, `kex_channel.c`, `kex_discovery.c`, `kex_game.c`, `kex_game_internal.h`, `kex_lan.c`, `kex_packet.c`, `messages.c`, `mvd.c`, `q2pro.c`, `q2pro_fields.c`, `q2pro_internal.h`, `r1q2.c`, `rerelease.c`, `temp_entities.c`, `vanilla.c`, `zpacket.c` |
| `src/network/q3` | `admission.c`, `channel.c`, `client.c`, `clock.c`, `codec.c`, `delta.c`, `huffman.c`, `huffman_internal.h`, `messages.c`, `pak_references.c`, `peer_internal.h`, `pure.c`, `server.c`, `visibility.c` |
| `src/network/unified` | `channel.c`, `commands.c`, `composition.c`, `document.c`, `packet.c`, `schema.c`, `value_internal.h` |
| `include/qa` — compatibility | `qc.h`, `qvm.h`, `native.h`, `native_host.h` |
| `include/qa` — bots/navigation | `bot_chat.h`, `bot_library.h`, `bot_perception.h`, `navigation.h`, `navigation_asset.h` |
| `include/qa` — networking | `network.h`, `network_q1.h`, `network_q1_channel.h`, `network_q1_nq.h`, `network_q1_qw.h`, `network_q1_session.h`, `network_q2.h`, `network_q2_batch.h`, `network_q2_kex.h`, `network_q2_kex_game.h`, `network_q2_messages.h`, `network_q2_mvd.h`, `network_q3.h`, `network_unified.h` |

`CMakeLists.txt` and `src/main.c` were read for target registration and actual application consumers. The initial assigned inventory was 49,007 lines; that historical count is an inventory, not a separate validation claim. Repository searches identified six test files: `tests/archive_test.c`, `tests/bsp_test.c`, `tests/core_test.c`, `tests/image_test.c`, `tests/model_test.c`, `tests/vfs_test.c`. These six bodies were not reviewed by this lane; API searches found no corresponding compatibility/network/bot test coverage. No test was run. Other workers own their source/test areas, and this report does not claim review of those bodies.

The navigation review covered AAS/NAV decoding and validation, graph generation, route costs, travel types, spatial association, prediction, runtime invalidation, training and checkpoints. The bot support review covered resource parsers, fuzzy weights, chat construction/replies/checkpoints, perception and deterministic RNG. The network review covered transport/address/IPX/SOCKS/DOSBox boundaries; NQ/QW sessions and demos; Q2 classic/R1Q2/Q2PRO/rerelease/KEX records, MVD/GTV, compression and discovery; Q3 Huffman/delta/snapshot/reliable/pure/visibility paths; and Anthology composition documents, schema, packets and command admission. Existing POSIX socket implementation is a platform limitation; the plan's first baseline is Linux x64, so it is not recorded as a contradiction of that baseline.
