# Whole-codebase unification audit — 2026-10-11

Every tracked code file and executable build/configuration input in the audit
snapshot has been reviewed: **3,137 files, 921,937 lines, 38 disjoint assignments,
zero unread files**. Primitive and event-system adoption is still incomplete.
The earlier supplied-list closure did not establish whole-codebase closure.

The [complete ledger](unification-2026-10-11.json) records every reviewed path,
reading method, file conclusion, finding, caller chain, replacement and semantics
to preserve. It also records exclusions, follow-up fixes and independent
refutations. The coverage sets match exactly; no file is counted twice.

## Scope and limits

The source snapshot is Git tree `b01b311cab565aebb3b8745e777ceeabf0fba2c5`.
Follow-up source changes were reviewed through main `a2c842af`.
Included inputs cover application, platform, gameplay, renderer, audio, input,
UI, networking, module hosts, public headers, tools, tests, build files and
generator policy inputs. Large literal/generated tables were read completely
using structured row/schema inspection and their actual producers/consumers.
The ledger identifies those methods separately from ordinary complete reads.

Excluded paths are non-executable documentation/history, licenses, reports and
binary assets. This is an audit of this repository, not a claim that every line
of sibling reference repositories was reviewed. Findings include actual caller
checks; source references remain at the snapshot's line numbers.

There are **374 overlapping source-review observations**: three were fixed and
three original claims were refuted. The remaining 368 records are not 368 unique
bugs: producer/consumer observations overlap, and some concern cold cleanup or
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
| Event delivery | `16-019`: Unified component player events are copied into another deferred queue after received-ring delivery. `36-events-native-q2-debug`: native Q2 GAME debug/world-text producers bypass the ring. | Consumer cursors and retained event leases; one admission and delivery order. Preserve component readiness, recipients and transformations. |
| Mixed module lifecycle | `J00-09`: native Q2 selected-movement commands are admitted, then rejected by the common accessor. `02-12/13/20/21`: provider/map, client, respawn and selected-service gates lack composition adapters. | Implement shared role dispatch and the missing original guest lifecycle/service adapters. Removing a gate alone cannot supply a missing adapter. |
| Trace policy and queries | `J21-03`: Q2 trigger/killbox clips omit pose rules. `J23-02`: rerelease callers choose classic policy. `04-001`: Q3 trigger query ignores overflow beyond 1,024 shared-world candidates. | Retained caller rulesets, shared exact clips and query storage sized for the shared world. Preserve spectator and original collision rules. |
| Numeric and identity domains | `01-combat-source-width`, `03-16`: valid original int32 values lose admissibility through binary32 exactness checks. `J24-07`: numeric Q3 teams and interned team IDs are conflated. | Preserve original widths at module/protocol boundaries and explicit canonical team conversion. Never substitute a string ID for a protocol number. |
| Entity/name/resource indexing | Private actor/source-number lists, model/picture caches, roster/reverse maps and runtime name conversions remain across application, frontend, bots and hosts. | Existing common indexes and retained IDs, keyed to the correct historical generation and ownership. Keep necessary namespace translation and previous snapshots. |
| Stamps, spatial and math | Private visit arrays, visibility/mark/light walks, angle/vector/matrix operations and parsers remain. | Common caller-owned stamp/query scratch and existing math/text primitives. Retain exact rounding, trace rules and original draw semantics. |
| Runtime storage | First-use/nested snapshots, reaction registration, guest invocation staging, packet/decode scratch, UI metadata and renderer buffers still grow or allocate during play. | Load-owned common pool/arena storage or frame leases according to actual lifetime. Preserve nested calls, retransmission, capacity and failure ownership; no arbitrary new cap. |
| Content identity and saves | `J13-01`: full-BSP FNV identity persists into saves and cache keys. Binding-menu fingerprints remain. Native Q3 saves embed installed PE/ELF bytes. | Names in saves, generation/revision counters in-process and existing RAM-state restoration. Preserve mandatory original protocol checksums. |

One source-confirmed correctness issue is separate from adoption:
`04-022` — `q3_world_restart.c:286,466` indexes eight-entry tables after accepting
engine game types 8 and 9. The out-of-bounds condition is confirmed; no live
crash was reproduced. Engine campaign/cooperative values and guest stock mode
numbers need an explicit boundary conversion.

`27-02` also identifies a duplicate QW encoder that can omit a solid-to-nonsolid
update when no other field changes. Moving to the common codec must retain the
original packet bits, widths, baselines and histories.

## Fixed during the audit

| Observation | Commit | Change and bounded verification |
| --- | --- | --- |
| `01-gear-restore-capacity` | `3bcbc944` | QVM gear restore accepts its already-prepared empty actor storage. Serialized-row bounds remain. Normal production build and seven core suites passed; no live QVM round trip. |
| `J13-02` | `f18a8869` | Removed seven SHA-256 fingerprint fields from four native-module inventory tools. Compiler facts and PDB GUID/age remain. No repository consumer of the removed fields was found. Scripts parse; normal build and seven core suites passed. |
| `J23-01` | `a2c842af` | Q2 restore zero-initializes retained model identity before the common binder reads it. Saved gameplay fields remain imported. Normal build and seven core suites passed; no live retail round trip. |

The seven existing suites were platform_services, core, archive, VFS, BSP, image
and model. All three commits are on remote main. These changes do not constitute
a new qualified installation into `qfiles/qa-c`.

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
