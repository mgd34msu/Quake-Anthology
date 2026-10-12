# Whole-codebase unification audit — 2026-10-11

The audit ledger records source review of all **3,137 tracked code and build
inputs** in its frozen snapshot: 921,937 lines across 38 disjoint assignments.
A fresh comparison against the snapshot tree confirms zero unassigned paths
and no duplicate coverage entries. Primitive and event-system adoption is
still incomplete. The earlier supplied-list closure did not establish
whole-codebase closure.

The [complete ledger](unification-2026-10-11.json) records every reviewed path,
reading method, file conclusion, finding, caller chain, replacement and semantics
to preserve. It also records exclusions, follow-up fixes and independent
refutations. The coverage sets match exactly; no file is counted twice.

## Scope and limits

The source snapshot is Git tree `b01b311cab565aebb3b8745e777ceeabf0fba2c5`.
Follow-up source changes were reviewed through main `157d1f55`.
Included inputs cover application, platform, gameplay, renderer, audio, input,
UI, networking, module hosts, public headers, tools, tests, build files and
generator policy inputs. Large literal/generated tables were read completely
using structured row/schema inspection and their actual producers/consumers.
The ledger identifies those methods separately from ordinary complete reads.

Excluded paths are non-executable documentation/history, licenses, reports and
binary assets. This is an audit of this repository, not a claim that every line
of sibling reference repositories was reviewed. Findings include actual caller
checks; source references remain at the snapshot's line numbers.

There are **374 overlapping source-review observations**: 21 have source fixes
with matching normal builds and core checks; three claims were refuted by caller
review. The remaining 350 records are not 350 unique bugs: producer/consumer observations overlap, and some concern cold cleanup or
missing capabilities. Their reported severities describe source-review priority,
not measured frame cost or demonstrated owner-visible impact. Independent
refutation covered selected high-risk findings, not every observation twice.

No live game, audio, installed-artifact, performance or whole-frame allocation
claim follows from this audit. Required protocol histories, historical snapshot
versions, guest ABI records and distinct original game rules must survive every
migration. Several instances of one shared implementation are valid ownership.

## Coverage

| Area | Files read | Observations before fixes/refutations |
| --- | ---: | ---: |
| Application | 612 | 108 |
| Audio, input and UI | 121 | 18 |
| Bots and navigation | 239 | 15 |
| Core, content, formats, tools and tests | 256 | 29 |
| Frontend | 517 | 71 |
| Q1 gameplay | 128 | 18 |
| Q2 gameplay | 141 | 19 |
| Q3 gameplay | 44 | 11 |
| Shared gameplay | 43 | 10 |
| Module hosts | 197 | 26 |
| Networking | 177 | 14 |
| Public interfaces | 467 | 11 |
| Render and presentation | 190 | 24 |
| Build and generator policy inputs | 5 | 0 |
| **Total** | **3,137** | **374** |

## Remaining foundation work

The ledger is the complete site list. These groups identify the consequential
shared-layer work; each migration must move callers and delete the replaced
implementation in the same slice.

| Capability | Confirmed examples | Required shared adoption |
| --- | --- | --- |
| Event delivery | `16-011`: legacy frame entity-event and unrecognized stufftext delivery still occurs before the client batch ring consumer. | Move the remaining producers/consumers onto retained records and ordered cursors. Preserve decode order, recipients and source timing. |
| Mixed module lifecycle | `02-12/13/20/21`: provider/map, client, respawn and selected-service gates lack composition adapters. | Implement shared role dispatch and the missing original guest lifecycle/service adapters. Removing a gate alone cannot supply a missing adapter. |
| Trace policy and queries | The named Q2 role-policy and Q3 trigger-overflow observations below are fixed. Other private spatial and visibility walks remain in the ledger. | Migrate each remaining query to shared caller-owned storage and retained role policy, preserving ordering and original collision rules. |
| Numeric and identity domains | `01-combat-source-width`, `03-16`: valid original int32 values lose admissibility through binary32 exactness checks. `J24-07`: numeric Q3 teams and interned team IDs are conflated. | Preserve original widths at module/protocol boundaries and explicit canonical team conversion. Never substitute a string ID for a protocol number. |
| Entity/name/resource indexing | Private actor/source-number lists, model/picture caches, roster/reverse maps and runtime name conversions remain across application, frontend, bots and hosts. | Existing common indexes and retained IDs, keyed to the correct historical generation and ownership. Keep necessary namespace translation and previous snapshots. |
| Stamps, spatial and math | Private visit arrays, visibility/mark/light walks, angle/vector/matrix operations and parsers remain. | Common caller-owned stamp/query scratch and existing math/text primitives. Retain exact rounding, trace rules and original draw semantics. |
| Runtime storage | First-use/nested snapshots, reaction registration, guest invocation staging, packet/decode scratch, UI metadata and renderer buffers still grow or allocate during play. | Load-owned common pool/arena storage or frame leases according to actual lifetime. Preserve nested calls, retransmission, capacity and failure ownership; no arbitrary new cap. |
| Content identity and saves | `J13-01`: full-BSP FNV identity persists into saves and cache keys. Binding-menu fingerprints remain. Native Q3 saves embed installed PE/ELF bytes. | Names in saves, generation/revision counters in-process and existing RAM-state restoration. Preserve mandatory original protocol checksums. |

`04-022` is now fixed: one shared game-type conversion covers campaign and
cooperative values as well as the original modes. The former eight-entry-table
out-of-bounds read was source-confirmed; no live crash was reproduced.

`27-02` also identifies a duplicate QW encoder that can omit a solid-to-nonsolid
update when no other field changes. Moving to the common codec must retain the
original packet bits, widths, baselines and histories.

## Fixed during the audit

| Observation | Commit | Change and bounded verification |
| --- | --- | --- |
| `01-gear-restore-capacity` | `3bcbc944` | QVM gear restore accepts its already-prepared empty actor storage. Serialized-row bounds remain. Normal production build and seven core suites passed; no live QVM round trip. |
| `J13-02` | `f18a8869` | Removed seven SHA-256 fingerprint fields from four native-module inventory tools. Compiler facts and PDB GUID/age remain. No repository consumer of the removed fields was found. Scripts parse; normal build and seven core suites passed. |
| `J23-01` | `a2c842af` | Q2 restore zero-initializes retained model identity before the common binder reads it. Saved gameplay fields remain imported. Normal build and seven core suites passed; no live retail round trip. |

## Follow-up fixes

Each code commit below passed a normal production build and the seven existing
core suites. Its build record was checked against the exact staged Git tree
that became the commit. These are source/build/core results, not live or timing
results.

| Observations | Commit | Shared replacement |
| --- | --- | --- |
| `J21-03`, `J23-02` | `f4f3c235` | Q2 trace and exact clip callers retain their role rules, including rerelease behavior. |
| Supplemental snapshot stamps | `437e1ecf` | Q3 snapshot visibility uses caller-owned `qa_stamp_set` across native, guest and Unified readers. |
| `16-019` | `aa7514e2` | Component player events use received-ring cursors and original leases. Private deferred copies are deleted. |
| `04-001` | `14eddb38` | Shared world snapshots replace the 1,024-candidate trigger limit. |
| Ring capacity support | `5efaf657` | Cold capacity reservation preserves existing record IDs and leases; append uses reserved storage. |
| `04-023` | `6a08052e` | Deferred rankings use the common ring, preserving exact text, batch order and existing overload behavior. |
| Native debug/world-text admission | `691d2582` | Native Q2 GAME imports enter shared records; private world-text storage is deleted. |
| `04-022`, `17-07`, `17-17` | `90cb4b67` | One mode conversion, common selected-sound delivery and cvar revision-based policy refresh. |
| `36-events-native-q2-debug` | `6f837512` | Typed Unified DEBUG delivers native geometry remotely. Later clock/duration fixes apply to the same record and store. |
| `J00-09`, `J00-03` | `690d5482` | Already-admitted native movement stays admitted; QVM live-in reads use executor scratch instead of a temporary host vector. |
| `16-018`, `03-03` | `c09a1af0` | Module actor/source slots use shared indexes; native release reads the host inverse binding. DEBUG uses the actual Source clock. |
| `J00-02`, `03-01` | `27712313` | Weapon preparation uses dispatch custody; Q2 composition, prediction and unicast use the existing actor index. |
| `J24-02` | `f6e429e7` | Q3 once-per-Source-frame actor visits use the common stamp set, including slot reuse. |
| `16-005` | `684e6060` | QC shapes enter common DEBUG; private growth/pruning/shape arenas are deleted. One store preserves width1, provider-clock pause, nanosecond lifetime and instant multi-seat visibility. |
| `04-002` | `157d1f55` | Disconnect scopes retain exact reason text in the existing control lease; cross-frame wire ownership stays intact. |

The seven suites are platform_services, core, archive, VFS, BSP, image and model.
All listed commits are on remote main. No new qualified `qfiles/qa-c`
installation was made. The common debug lifetime checks cover paused clocks,
sub-millisecond and long durations, instant multi-seat frames and binary payload
round trips. Debug geometry is transient presentation and is excluded from new
tool captures; existing checkpoint fields remain readable.

These fixes close the named observations only. They do not establish that all
callers of every capability have migrated, that all module combinations work,
or that the whole engine has zero steady-state allocation.

## Refuted and narrowed claims

`16-020` and `J18-06` described required Q3 module command histories as redundant
player-event queues. Caller checks refuted that claim: frozen-frame visibility,
synthetic config/map-restart commands and rollback require those histories.
A common history implementation may replace copies, but an existing player-event
cursor cannot replace their contract.

`03-12` claimed another provider's first canonical cvar registration caused
shipped Q1/Q2 Source reads to fail. Source declaration views retain local owners,
so that specific failure mechanism was refuted. Lookup simplification remains
a design candidate, with legitimate declaration/write ownership preserved.

Other independent checks narrowed findings in the ledger: historical identity
maps are necessary; resource construction occurs on misses; active-menu rebuilds
are conditional; reached and authoritative config dictionaries are distinct;
and missing module adapters are capability work, not permission to delete
original lifecycle checks blindly.
