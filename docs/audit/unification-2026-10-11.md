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
Follow-up source changes were reviewed through main `f39b89c5`.
Included inputs cover application, platform, gameplay, renderer, audio, input,
UI, networking, module hosts, public headers, tools, tests, build files and
generator policy inputs. Large literal/generated tables were read completely
using structured row/schema inspection and their actual producers/consumers.
The ledger identifies those methods separately from ordinary complete reads.

Excluded paths are non-executable documentation/history, licenses, reports and
binary assets. This is an audit of this repository, not a claim that every line
of sibling reference repositories was reviewed. Findings include actual caller
checks; source references remain at the snapshot's line numbers.

There are **374 overlapping source-review observations**: 219 have source fixes
with matching normal builds and core checks; three claims were refuted by caller
review. The remaining 152 records are overlapping observations: producer/consumer observations overlap, and some concern cold cleanup or
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
| Event delivery | The named tool/LLM LOG and deferred connection queues are fixed below. Other event custody observations remain in the ledger. | Move the remaining producers/consumers onto retained records and ordered cursors. Preserve decode order, recipients and source timing. |
| Mixed module lifecycle | `02-12/13/20/21`: provider/map, client, respawn and selected-service gates lack composition adapters. | Implement shared role dispatch and the missing original guest lifecycle/service adapters. Removing a gate alone cannot supply a missing adapter. |
| Trace policy and queries | The named Q2 role-policy and Q3 trigger-overflow observations below are fixed. Other private spatial and visibility walks remain in the ledger. | Migrate each remaining query to shared caller-owned storage and retained role policy, preserving ordering and original collision rules. |
| Numeric and identity domains | `01-combat-source-width`, `03-16`: valid original int32 values lose admissibility through binary32 exactness checks. The named Q3 team-domain observation is fixed below. | Preserve original widths at module/protocol boundaries and explicit canonical team conversion. Never substitute a string ID for a protocol number. |
| Entity/name/resource indexing | Private actor/source-number lists, model/picture caches, roster/reverse maps and runtime name conversions remain across application, frontend, bots and hosts. | Existing common indexes and retained IDs, keyed to the correct historical generation and ownership. Keep necessary namespace translation and previous snapshots. |
| Stamps, spatial and math | Private visit arrays, visibility/mark/light walks, angle/vector/matrix operations and parsers remain. | Common caller-owned stamp/query scratch and existing math/text primitives. Retain exact rounding, trace rules and original draw semantics. |
| Runtime storage | First-use/nested snapshots, reaction registration, guest invocation staging, packet/decode scratch, UI metadata and renderer buffers still grow or allocate during play. | Load-owned common pool/arena storage or frame leases according to actual lifetime. Preserve nested calls, retransmission, capacity and failure ownership; no arbitrary new cap. |
| Content identity and saves | `J25-02`: native Q3 saves embed installed PE/ELF bytes. The named BSP and binding-menu fingerprint observations are fixed. | Names in saves, generation/revision counters in-process and existing RAM-state restoration. Preserve mandatory original protocol checksums. |

`04-022` is now fixed: one shared game-type conversion covers campaign and
cooperative values as well as the original modes. The former eight-entry-table
out-of-bounds read was source-confirmed; no live crash was reproduced.

`27-02` is partial: the shared writer now handles SOLID-only set and clear
updates, preserving raw Source delta decisions and QW29 extensions. Separate
Source and decoder histories remain; their consolidation must retain frame age,
original widths and frozen snapshot ownership.

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
| `J00-01`, `J23-05`, `J24-03` | `cdd63e09` | Shared borrowed info pairs preserve exact/case-folded first-match and merge semantics. |
| `J00-04` | `66a629b5` | Native attack scopes use the existing common control lease. |
| `16-002` | `74f135d5` | One angle interpolation implements explicit single-turn and normalized-delta source policies. |
| `03-14` | `f7a51d88` | Private decimal-prefix readers use common explicit clamp/wrap/whitespace policies. |
| `J14-11`, `J23-08` | `75b69540` | One MT19937 implementation preserves distinct original streams and save semantics. |
| `J00-12` | `5e7cfddf` | Listener snapshots borrow control leases instead of allocating per dispatch. |
| `J00-13` | `9d616e7e` | Native Q2 scalar decision descriptors resolve offsets and encodings once; reads remain live. |
| `26-12` | `683d5bce` | QC radius enumeration uses shared world queries with immediate visitor and original source ordering. |
| `16-011` | `38ed1cb2` | Q2 frame and command delivery uses the ordered received ring, retaining WAIT/cancel custody. |
| `Q3NET-35-01`, `Q3NET-35-04`, `28-04`, `J24-05` | `ffbdb58d` | Boundary binary/math and Unicode comparison readers use the common primitives. |
| `J14-01`, `J00-05`, `J00-06`, `04-013`, `04-014`, `04-015` | `ed9a2152` | Equipment, bindings, drafts and native Q2 resources retain IDs or edit revisions. |
| `17-15`, `04-011` | `77aad804` | Unified legacy light styles use one ordered retained table; physical binding comparison uses the common predicate. |
| `J13-01`, `02-08` | `c37e2735` | BSP fingerprints are removed. Runtime uses load generations; saved adapters preserve old slots and bind names/current generations. |
| `28-02`, `J11-02`, `26-07`, `09-08` | `a78624e2` | Unified payload decode occurs once; JSON strict UTF-8, native info imports and bot integer output use common codecs. |
| `J13-04`, `J13-05`, `J13-06` | `93b2440b` | Numeric vector/bounds equality and spatial sector selection are shared; unused attachment transport is deleted. |
| `J11-03`, `17-16` | `f1436e9f` | Script and HUD compression share caller-owned storage; SystemInfo uses common pair and key primitives. |
| `J11-05`, `J11-09` | `413a776e` | VFS resource identity uses the shared ID index; entity clipping uses the common original-epsilon algorithm. |
| `J13-07` | `bcb95cff` | Q2/Q3 medium clipping and coverage share caller scratch. QW writer portion of 27-02 is fixed; histories remain open. |
| `09-01` | `624740a2` | Named character, fuzzy, goal and initial-chat aliases use the common index; import/history and actual source-byte custody rechecked. Admission/workspace growth remains separate. |
| `J11-08` | `1a5c3dd4` | Model tag/bone names bind once, preserving first exact match per MD3 frame and full MD5 names; native callers carry IDs. |
| `08-06`, `J14-08` | `0109c3d6` | Media/frontend movie image ownership uses the common index, with full image identities, staged publication and original global Q3 sharing. |
| `10-04` | `a04c8473` | Navigation uses the common usercmd builder with retained physical rounding and delta-angle policy; the private constructor is deleted. |
| `J14-09` | `11620e3a` | One platform directory service replaces frontend SDL directory discovery; engine and SDL allocator ownership remain distinct. |

The seven suites are platform_services, core, archive, VFS, BSP, image and model.
All listed commits are on remote main. No new qualified `qfiles/qa-c`
installation was made. The common debug lifetime checks cover paused clocks,
sub-millisecond and long durations, instant multi-seat frames and binary payload
round trips. Debug geometry is transient presentation and is excluded from new
tool captures; existing checkpoint fields remain readable.

| Observations | Commit | Shared replacement |
| --- | --- | --- |
| `J24-07` | `bfc1881c` | Shared combat compares canonical team IDs. Original red/blue numbers are produced only at the Q3 boundary. |
| `J14-03`, `17-03` partial | `da60a3cf` | Full-generation actor and uint64 bus indexes replace lookup scans. Same-round history and pool/index growth remain; round reset reclaims aliases. |
| `J13-08` | `3df0a4c4` | Spatial insertion and trigger delivery retain role rules instead of selecting by family during each operation. |
| `Q3NET-35-03` | `a9c32f98` | Console and Q3 network use one token-span lexer, with their original token limits and partial receipts. DEL remains an ordinary byte; the audit's claimed DEL discrepancy was incorrect. |
| `27-04` | `ac8eec36` | KEX userinfo projects common borrowed info pairs. Pair/key/value limits, defaults and seat overrides remain. Shipped callers use distinct source and destination buffers. |
| `J12-01/02/03/04/11` | `2a7903a1` | QuakeWorld touch de-duplication reads retained movement contacts. Numeric and source-text adapters use common primitives; shared contacts can still grow. |
| `J32-01` | `27bde296` | Render/effect rotation, perpendicular selection, portal remap, transformed bounds and matrix directions use common primitives with explicit original arithmetic policies. |


| Observations | Commit | Shared replacement |
| --- | --- | --- |
| `31-03` | `e1d581d2` | Raw-record kind-mask iteration replaces the named event walks; Q3 effects retire once at the batch boundary and retain successful-prefix/error retry semantics. |
| `J32-12` | `15fd4572` | CPU and GL output use one gamma-table builder with explicit original arithmetic policies. Source-palette/overbright behavior is unchanged; no pixel or timing claim. |
| `J12-06`, `J12-09`, `J18-02` | `f9ee50f0` | Camera samples use frontend frame storage and common arena text. Current debug snapshots already use the same frame arena; no disposable per-view arena remains at the named adapters. |
| `J12-05` | `f3b079bf` | Tools and LLM output enters ordered immutable LOG records. One leased final cursor and shared redirect capture preserve context/text/order; core capture checks exercise direct/ring/direct ordering. |
| `J12-08` | `2a47f117` | One counted-byte comparison implements exact/ASCII-fold equality. Console and tools copies are deleted; embedded NUL/high-byte/length checks passed. |
| `J32-13` | `62d85505` | Dormant model fingerprint/caster/atlas paths are deleted. Original reached planar and stencil shadow paths and reserved identity slot remain. |
| `J12-12`, `Q3NET-35-02`, `26-17` | `a42df03f` | Input identity uses the common context comparator, preserving strict script pointer equality. Q3 decimal adapters preserve locale/ASCII distinctions and checksum casts. QVM perpendicular projection uses the shared reciprocal primitive. |
| `28-01` | `24aad7c8` | Live unified handshake reads the existing bounded layout and writes caller-owned storage, without temporary documents. Existing document decode delegates it; core checks compare wire bytes and failure output behavior. |
| `31-10` | `748fa6a6` | Presentation converts ABI fields and delegates one qsrc-derived trajectory implementation. Core checks cover wrapped stop boundaries, gravity, milliseconds, half-amplitude sine velocity and invalid-output preservation. |
| `09-06` | `c99499c4` | Bot bindings use qa_actor_index with full identities. Checkpoint rebuild and population/requirements save adapters keep the existing field order and client+1 words by source review; no live bot-save round trip. |
| `10-06`, `17-12` | `33e131cc` | Navigation SCC and Unified prediction use caller-owned qa_stamp_set. DFS order/cluster numbering and post-conversion duplicate admission are source-preserved. Cold mark storage grows from byte/bool to uint32; no speed claim. |
| `J32-04` | `28e8d3af` | Legacy numeric style snapshots allocate once from actual face/grid references. Full supplied spans remain validated, including unreachable suffixes; core checks cover extended indices, grid-only styles and shrink/re-expand. Last-submission sampling remains world-owned. |
| `J24-10` recheck | `6a08052e` | The overlapping ranking-queue observation is already resolved by the earlier ring migration; current synchronous delivery, save transfer and teardown callers were rechecked. |

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

| `J15-08` | `cdd9b97a` | Deferred connection actions use common ring records and one network cursor; private nodes are deleted. FIFO and request lease checks pass; no live connection proof. |
| `J21-04` | `ea0eaf27` | Q2 wind timers reserve actual actor capacity at load and use the common actor index, retaining cooldown and save order. |
| `01-identity-remaining-actors` (partial) | `2af9c4c7` | Three guest projection resolvers use the common retained actor index. Detached generations remain; other resolvers and existing context allocations are still open. |
| `06-07` | `c3f6c44a` | Console and font layout share the original Q3 color escape rule. Core checks non-digit escapes, literal/end handling and console cells. |

| Observations | Commit | Shared replacement |
| --- | --- | --- |
| `J32-08` | `efbfdb9d` | One frame-owned common ID index pins immutable images by their full creation serial. Index membership supplies the visit test; a second stamp store would duplicate it. Core checks interleaved frames, reset/re-admission and independent retained lifetimes. No whole-frame allocation or timing claim. |
| `01-identity-remaining-actors` partial | `e0fb8caa` | Guest player-event cursors use the retained full-ID index through track, release, ordered compaction and restored cursor rebuild. Original event sequence, order and save fields remain. Other actor resolver sites remain. |
| `17-02` | `6d503436` | One controller selection equality/device matcher covers platform routing, settings and saves. Core checks GUID case/ordinal, exact serial and serial ordinal independence. No physical hotplug proof. |
| `07-02` | `04212f40` | Common counted-byte ordering/prefix handles console, completion and cvar catalog names. Exact spelling tie-breaks remain. Core covers embedded NUL, high bytes, case, ordering and prefix bounds. Completion storage allocations remain. |
| `26-06` | `894ad0d9` | Removed per-call bootstrap path witness checks and unused held bootstrap descriptor. Cold native-runtime launch validation and actual owned child probe/capture remain. Other standard file capability checks remain; no live SDK claim. |
| `10-07` | `1d7b3118` | Bot weapon scalar records and AAS float fields use common endian primitives; private codecs deleted. Field widths and assembly remain. No retail bot/save round trip. |
| `06-03` | `baa0025a` | Voice and lifecycle notification slots use common pools over the prepared mixer arena. FIFO, paired notifications, voice high-water/reuse order and sample ownership remain. Existing audio core checks now cover zero, partial and full initial voice sets with repeated playback/stop/reuse. |
| `01-identity-remaining-actors` partial | `fe925221` | Accepted guest scene actor slots use one cold-reserved common ID index rebuilt with the accepted ordered publication. Existing duplicate slot/count validation remains; other actor resolvers remain. |
| `06-04` | `50a1d16b` | Audio resource-ID/family entries share the existing bank ID index with name entries in a disjoint lossless key namespace. Core checks full-width IDs, family separation, compaction/rebuild, retirement and held sample lifetime. Cold decoding and resampling are unchanged. |

| Observations | Commit | Shared replacement |
| --- | --- | --- |
| `01-identity-remaining-actors` | `80d65023` | Guest item actor leases use retained full-actor lookup; ordered rows and partial-restore cleanup remain. |
| `10-05` | `8c273b51` | Navigation reads canonical contents directly. Q1 terminal precedence, current-to-water behavior, existing Q2 merged samples and Q2-only ladders remain. |
| `09-05` | `11e8b444` | Route reset retains shared output storage across replanning and slot reuse. Graph metadata and trajectory state reset; lifecycle destruction frees backing. First or longer route capacity growth remains, so cold capacity preparation is still open. |
| `09-02` | `bbd9c124` | Forty bot source-record consumers use common scalar binary primitives; private scalar codecs and aliases are deleted. ABI offsets and widths remain. |
| `01-identity-remaining-actors` | `7c94458b` | Guest Q3 client retirement uses retained full-generation lookup through reserve, connect, bot allocation, round carry and save restore. Pending retired actors remain indexed until actual release. |
| `01-identity-remaining-actors` | `b53b9a68` | Component bindings and each current/baseline/pending publication own common indexes. Ordered arrays and saved fields remain; publication swaps and baseline copying preserve independent index storage. |
| `01-identity-remaining-actors` | `7c1515bd` | Guest pickup/protection resolvers share a retained actor index and common resource-group pool. Binding admission pins the group across callbacks; pickup visibility still starts after bind succeeds. Channels, lease serials and cleanup refusal retain original order and ownership. |
| Signed-word follow-up | `42c2eb26` | Three additional bot scheduler/fuzzy signed-word helpers are deleted; common conversion preserves unsigned-wrap arithmetic. |
| `26-16` | `e299ce8c` | QC direction builtins use common stock integer math: double atan2, float horizontal product, double sqrt rounded to float, positive integer zero and original vertical pitch. Existing float approximation is corrected, not claimed bit-identical to the old port. |

All 13 actor resolver sites named by `01-identity-remaining-actors` now use the common indexes. Existing row/stage allocations remain separate work. `09-05` remains open for cold capacity preparation; retained output reuse alone does not prove zero runtime growth. The QC arithmetic recommendation was corrected against the original source; no old/new bit-equality claim is made for that correction.

| `J19-F03`, `J20-01` | `9b07d1d8` | Q1 clone and MG3 upgrade use the common inventory snapshot lease; empty second reads and the MG3 checkpoint retain their order. Normal build and seven core suites passed; no live result. |

| `02-07` | `06b77db3` | QC weapon records return to the common pool after successful unbind and call draining; cleanup refusal retains ownership. Cold backing and extension for simultaneous records remain. |

| `17-14`, `J18-12` | `c486c081` | Seven local/remote model transforms use one constructor, retaining explicit float math, scales and handedness. Existing model tests check cardinal yaw and scale/translation order; prebuilt beams are unchanged. |

| Radius storage, partial | `8693af1b` | Q1/Q2 radius capture writes directly into the held caller buffer; Hipnotic keeps descending callback order. Extra copies/world leases removed. Private builtin allocation remains open. |

| `06-05` | `5923d872` | Private console text builder and all callers deleted. Scoped command arenas, retained dedicated-input buffers and exact cold owned outputs preserve quoting/lifetimes/saved fields. Existing core checks unread checkpoint and worst-case UTF-8 repair. Large immediate-command and persistent/cold allocations remain separate work. |

| `09-07` | `bfc4ae0a` | Canonical inverse bindings replace three bot combat reverse scans. Source alias ranges, generation equality and ordered targeting remain. Source review plus normal build/seven core suites; no live combat proof. |

| `J25-05` | `732a7833` | One guest-sort body, retaining ABI comparator and memory adapters. Normal build and seven core suites passed. |

| `06-02` partial, `05-D1` final caller | `d89bfbdc` | Music PCM storage reserved before playback; common arena backing and independent wrapped clone/restore checked. Device/Vorbis work remains open. |

| `09-03` partial | `abb1747a` | Shared actor index and retained names for foreign goals; history/import field layout preserved. Historical row growth remains open. |

| `08-07` | `9ef280d4` | One interpolation writer and caller arena scratch, preserving classic/catalog grammar, finale order and clipping behavior. Normal build and seven core suites passed. |

| `31-06` | `c04c46fb` | One source-faithful animation decoder, shared COM/numeric primitives and caller-specific fallback. Normal build and seven core suites passed. |

| `01-identity-component-services`, `01-identity-q2-control` | `53513c6f` | Shared slot/actor indexes replace private module lookup scans while retaining full generations and physical/client predicates. Normal build and seven core suites passed. |

| `01-text-userinfo-pairs` | `3321b45f` | Common info-pair traversal/lookup replace private parsers, preserving Source-specific dedup and edit order. Normal build and seven core suites passed. |

| `01-storage-equipment-status` | `dda89576` | Immutable HUD region spans prepared in one owner arena before hooks bind; per-draw allocation deleted and cleanup refusal retains storage. Normal build and seven core suites passed. |

| `01-math-axis-projection` | `b943a670` | One axis projection primitive; both consumers retain explicit source arithmetic policy. Existing core cases protect distinct roll rules and handed basis recovery; normal build/seven suites passed. |

| `01-storage-lease-grow` | `000d4208` | All five listed capture-buffer growth sites use the common arena, preserving zeroed new elements and held-span lifetime. Normal build and seven core suites passed. |

| `01-storage-protection-continuations` | `e2549f62` | Protection stages/store deliveries use shared control leases; release-before-callback and failed-cleanup retention preserved. Normal build and seven core suites passed. |

| `01-identity-gear-pointer` | `65cf64a8` | One load-sized common pointer index retains interleaved duplicate priority and retired binding lifetime; admission/restore/free paths migrated. Normal build and seven core suites passed. |

| `06-02` partial | `674addf2` | Device PCM/encoding/conversion storage prepared in common arenas; candidate and legacy save ownership preserved. Normal build/seven suites passed; no SDL behavior proof. Vorbis and larger valid-request growth remain open. |

| `09-04` partial | `3aef8687` | Fuzzy evaluation/mutation/release use common arena ownership, retaining live reads and callback order. First and deeper traversal admission remain open. Normal build and seven suites passed. |

| `01-storage-live-catalog` | `90fbdd0f` | Reusable common arena publication/candidate banks and first-tag indexes replace per-read private ownership; live reads and callback copies remain. Larger valid inputs can grow. Normal build and seven suites passed. |

| `06-02` partial | `51a78052` | Vorbis staging uses the common arena and music/OGV prepare actual chunk bounds. Public larger reads and rollback remain; void-start reservation failure preserves ownership. Normal build/seven suites passed; no live audio or zero-growth claim. |

| `J20-06` | `f8151d7a` | Ordered Q1 precache receipt/emission readers use common ID indexes; slot zero, callbacks and checkpoint layout preserved. Normal build and seven suites passed; no live protocol or speed claim. |

| `17-11` | `4ed20da4` | Snapshot-local common actor indexes replace inverse scans; historical generations and WORLD/NONE boundaries preserved. Normal build and seven suites passed; no live prediction or timing proof. |

| `31-08` | `385eee46` | Q3 presentation numeric copies deleted; common float/decimal conversion and defined signed32 wrap used throughout scoped callers. Normal build/seven suites and wrap boundary cases passed; no live or speed claim. |

| `17-18` | `868114bf` | One caller-arena scene graph BFS for namespace, inventory and capture; callback/ordinal/cycle rules and retained replay preserved. Normal build and seven suites passed; no live capture or zero-growth claim. |

| `07-03` partial | `76b76d8f` | All discovery callers and completion custody use common arenas; strided names remove the temporary copy. Existing core behavior cases passed with normal build/seven suites. Load-sized/no-growth work remains open. |

| `31-09` | `7d3f6cc2` | One Q3 tag attachment body; original arithmetic, fields and missing-tag rules are caller policy. Shared item axes and pose normalization. Normal build/seven suites passed; no live visual or timing claim. |

| `J14-10` | `f71e825c` | WORLD music identity retained across unchanged publications; replacement and startup previous-selection rules preserved. Normal build/seven suites passed; no live audio or timing claim. |

| `03-17` | `ef299e52` | Q1 muzzle projection indexes authoritative ring records; abort, reused actor slots and retirement cases pass. Normal build/seven suites passed; no live protocol or speed claim. |

| `03-13` | `c1c02b87` | Native command/name adapters use common ASCII equality; private folds deleted and original authentication/recipient rules retained. Normal build/seven suites passed; no live command or speed claim. |

| `02-23` | `c512bc6b` | Q3 pose and roster readers use common direction/finiteness/equality primitives; original rounding and boundary rejection retained. Normal build/seven suites and cardinal cases passed; no live or speed claim. |

| `17-09` | `468d5c40` | Unused Unified Q2 numeric aliases deleted; full-actor event and legacy number paths retained. Normal build/seven suites passed; no live effects or speed claim. |

| `26-01` | `b1e23546` | Native message references use load-sized common arena storage; WriteEntity and restore growth deleted. Normal build/seven suites passed; no live guest or timing claim. |

| `31-11` | `1c094417` | One Q3 media-row updater uses command-local stable text; duplicate heap copies deleted. Native sequence/slot/callback rules retained. Normal build/seven suites passed; no live claim. |

| `17-10` | `418d314a` | Shared renderer actor/model index and caller-local stamps replace repeated Q2 frame scans. First-model and eligible output order retained. Normal build/seven suites passed; no live, zero-allocation or timing claim. |

| `03-18`, `04-005` | `78168074` | One provider owner index with distinct live/published/routing views; listed scans deleted. Order, ambiguity and lifetime core cases pass with normal build/seven suites. Further owner callers are still being migrated; no global closure claim. |

| `J19-F07` | `067eb17c` | Common finite rounding and QC vector arithmetic replace private Q1 bodies. Original float spread/particle rounds and zero normalization retained; normal build/seven suites passed. No live or timing claim. |

| `03-18`, `04-005` follow-up | `7dd02d56` | 23 additional control, event, bot, equipment and startup owner readers migrated with original selection rules. Normal build/seven suites passed; remaining readers still in flight. |

| `J15-02` | `0ca6ce15` | Browser uses shared info iteration/mutation; strict and warning policies retain boundary behavior. Normal build/seven suites and capacity/duplicate cases passed; no live claim. |

| `03-15`, `04-003` | `57ce4334` | One SystemInfo apply body and shared info iterator replace private parsers. Filters, callback checks, nested text and save bytes retained. Normal build/seven suites passed; candidate allocations remain. |

| `26-02` | `ca94a5f3` | Native HUD prefix uses caller-owned ABI bytes; per-draw malloc/free deleted. Exact guest prefix and synchronous seat lifetime retained. Normal build/seven suites passed; no live claim. |

| `03-18`, `04-005` follow-up | `c01284f8` | 41 more module/restore owner selections use the common index, retaining view/order rules and post-callback identity. Normal build/seven suites passed; historical/name readers remain in flight. |

| `03-04` | `a3ced3e2` | Completed Q2 visibility uses the common full-actor index, preserving source-slot aliases and unchanged saved rows. Normal build/seven suites passed; no live claim. |

| `26-08` | `18f15cae` | Native cvar ABI metadata uses the common ID index and canonical handles. Guest addresses/order/writeback and saved fields retained. Normal build/seven suites passed; registration allocations remain. |

| `26-11` | `9a069401` | Q3 module updates use retained cvar handles and fixed ABI mirror storage. Hidden-status, alias and saved-field rules retained. Normal build/seven suites passed; no live claim. |

| `03-18`, `04-005` follow-up | `cba4fbb4` | Seven equipment/campaign name selections use the common provider index; cross-session IDs resolved in the destination. Normal build/seven suites passed; historical candidate views remain in flight. |

| `02-16` | `10ce2370` | Common full-actor index replaces native CLIENT observer membership scans; original generation, publication and refusal lifetimes retained. Normal build/seven suites passed; no live claim. |

| `J20-09` | `c5993e77` | Q1 info readers share parsing and ordered indexed storage; raw serialization shared, saves unchanged. Storage-only duplicate value corrected. Normal build/seven suites passed; input growth remains. |

- `01977991` / 02-17: shared control leases own both live QC word captures. Normal build and seven core suites passed; larger shared-storage peaks remain open. Records: `/tmp/qa-ta3196-qc-combat-capture-20261012`.

- `79bb8019` / 08-02, 30-02: All world text uses retained output-event pages and one drawing adapter. Private font/QC stores deleted; actual local drawing owns one-frame lifetime, dedicated delivery uses source time, and Q2 save bytes stay unchanged. Normal build/seven core suites passed; live presentation remains untested. Records: `/tmp/qa-ta3196-world-text-owner-20261012`.

- `53103fa2` / 26-18, 26-19: QVM source observers use common pools, arenas and full-width binding indexes; immutable function entries are prepared at admission. Callback ordering, nesting and restore identity mapping retained. Normal build/seven core suites passed; unbounded registration and nesting peaks can still grow common storage. Records: `/tmp/qa-ta3196-qvm-observer-pools-20261012`.

- `27b92d35` / 04-006: Native and external Q3 reached commands share literal token copying and retained alternating arenas; old arguments remain admitted on failed replacement. Joined Args and supplied empty Args both passed actual core cases. Normal build/seven core suites passed; first/larger commands can still grow common storage. Records: `/tmp/qa-ta3196-q3-reached-tokens-20261012`.

- `8934a7e1` / 04-005: Candidate snapshots, published rosters and removed-provider views now share the common provider index. Original first-match ordering, historical descriptor identity, NULL qualification and abort-refusal lifetimes retained; candidate selection callers use the admitted roster view. Normal build/seven core suites passed; no live or allocation claim. Records: `/tmp/qa-ta3196-provider-historical-views-20261012`.

- `fd21bfa3` / 26-09: Q3 AAS crossings lease common navigation captures through the final guest write. Private host capture list deleted; collector arenas retain order, repeated DAG leaves and original zero-capacity behavior. Normal build/seven core suites passed; larger crossing or nesting peaks can still grow common storage. Records: `/tmp/qa-ta3196-aas-crossings-owner-20261012`.

- `03c9415e` / J32-02: Renderer resource identities and CPU skin-owner lookups now use the common ID index; exact owner/revision behavior passes the existing core fixture. This does not claim zero allocation or measured speedup. Records: `/tmp/qa-ta3196-render-resource-index-20261012`.

- `45667a4c` / 26-15: QuakeC host globals and temporary value keys use independently leased common capture arenas, including nested VM execution and restoration. Declared global staging uses the existing control lease; its userinfo projection remains open. Normal build and seven core suites passed; larger storage peaks can still grow. Records: `/tmp/qa-ta3196-qc-host-captures-20261012`.

- `61f53dfc` / 26-14: QuakeC string bytes and ordered engine buffers use one common arena and name index. Guest offsets, QW negative IDs and limits, alias growth and checkpoint replacement pass the existing core suite. New dynamic names and larger string extents can still grow common storage. Records: `/tmp/qa-ta3196-qc-string-storage-20261012`.

- `1e48c0e2` / 02-05: Active and parked QuakeC input scopes retain independent shared control leases through callbacks, restoration and resume/abort. Private scope, field and capture heap storage is deleted. Normal build and seven core suites passed; source close refusal behavior remains unchanged. Records: `/tmp/qa-ta3196-qc-input-leases-20261012`.

- `9cbf0df6` / 04-007: Native Q3 cached cvar snapshots borrow canonical immutable text while retaining the original delayed G_UpdateCvars metadata. Change notifications use exact-sized common control leases and save fields remain unchanged. Normal build and seven core suites passed; new unique values may still allocate canonical strings. Records: `/tmp/qa-ta3196-q3-setting-snapshots-20261012`.

- `e1e3e9f0` / 04-009: Q2 and Q3 source snapshot captures retain independent common control storage, prepared from their admitted entity extents. Prefix growth and partial-construction refusal keep existing captures intact; original wire records and visibility decisions remain unchanged. Normal build and seven core suites passed; no live, allocation-zero or performance claim. Records: `/tmp/qa-ta3196-source-wire-captures-20261012`.

- `2d679812` / 02-11: QuakeC infokey and QW client-name parsing borrow the common info-string parser while callback text/key captures use independent control leases. Last-key selection, limits and string-buffer admission remain unchanged. Normal build and seven core suites passed; dynamic engine-string growth remains separately scoped. Records: `/tmp/qa-ta3196-qc-info-leases-20261012`.

- `138452ac` / 02-02, 02-04: Q3 weapon requests and match callbacks use the full-actor retained index and stable common pool. Ordered requests, mode generations, refused unbind ownership and QAG3WR save bytes remain unchanged. Restore stages candidates in existing control storage. Normal build and seven core suites passed; historical peaks can still extend shared storage. Records: `/tmp/qa-ta3196-q3-weapon-services-20261012`.

- `81459187` / 02-14: Together with45667a4c, declared QC global captures and userinfo/string projections use independent common control leases. Q1 retains its one ordered serializer and exact backslash/first-key semantics; callers copy borrowed values before VM string admission. Normal build and seven core suites passed. Whole-userinfo services/writes and per-read projection work remain; new named guest strings can grow shared storage. Records: `/tmp/qa-ta3196-qc-declared-projections-20261012`.

- `50d1200c` / 02-01: The Q3 weapon namespace is admitted once from the immutable GAME artifact profile. Hot validity checks retain the actual VM, image, path pointer and ABI, with disposal still ordered before role teardown. Normal build and seven core suites passed; no live or performance claim. Records: `/tmp/qa-ta3196-q3-weapon-model-namespace-20261012`.

- `2df8a610` / 02-19: Placement uses one independent control lease with common visit stamps and reusable ordered offsets; retained barriers use the targets-owned common pool. Setter, restore and disposal share that owner, preserving authored retry order, full actor IDs and persisted bytes. Normal build and seven core suites passed; historical pool growth remains explicit, with no live or zero-allocation claim. Records: `/tmp/qa-ta3196-monster-placement-storage-20261012`.

- `134fff3f` / 26-05: Native touch tickets and outer metadata now use existing common pools, and ClientThink/Pmove/touch share retained 96-byte call scratch. Classic/RR encoding, four arguments and observable callback/refusal custody are retained. Scratch addresses now reuse host-owned allocation backing rather than per-contact free/unmap observers. Reaction ABI buffers and actual loan peaks may still allocate; normal build and seven core suites passed, with no live or whole-engine zero-allocation claim. Records: `/tmp/qa-ta3196-native-touch-pool-20261012`.

- `9c2b7e48` / 02-09: QC exports bind once; both command arities use reusable depth/capacity engine slots and indexed calls. Engine rewrites invalidate canonical projections instead of eagerly interning every command, with original guest references and checkpoint bytes retained. Existing slots reuse without consuming guest extent; absent slots choose geometric or exact capacity before admission. Core checks cover NQ/QW reuse, suffix refresh and lazy identity plus existing checkpoint restoration. Normal build and seven core suites passed; new size/depth peaks may grow common storage. Records: `/tmp/qa-ta3196-qc-command-slots-20261012`.

- `216e50cd` / 09-04: Fuzzy goal and weapon selectors prepare one common sealed traversal arena from all admitted separator aliases at create/bind/restore; ordinary evaluation only reads authoritative source records. Child/next and RNG order remain unchanged, with explicit mutation/free traversal prepared separately. Normal production build and seven core suites passed; genuinely new source admission may increase common storage, and no live or whole-frame allocation claim is made. Records: `/tmp/qa-ta3196-fuzzy-workspace-20261012`.

- `a73bb66e` / 02-22, 04-004: Guest player bindings now use common pooled payloads plus full owner/source-slot membership; QVM and native remote Q3 roles use one common role index per owner instead of identity scans. Actual records, actor/launch state and checked teardown remain authoritative. Cold role reindexing retains list selection order and cleanup-refused roles; save field order is unchanged. Corrected a UI adapter name collision before the successful normal production build and seven core suites. No live-module, performance or whole-frame allocation claim. Records: `/tmp/qa-ta3196-q3-membership-corrected-20261012`.

- `be9e1028` / J32-09: World visibility and Q1 mirror traversal now use one read-only BSP tree visitor with caller-owned scratch and caller-defined ordering, pruning and light-mask payload. Private traversal engines/types are deleted. Existing gameplay BSP fixture covers both child orders, between callbacks, payload propagation, pruning and early stop; normal production build and seven core suites passed. Original renderer policy remains caller data; no screenshot or performance claim. Records: `/tmp/qa-ta3196-shared-render-tree-20261012`.

- `8b8b5eeb` / 05-H1: Supply actor/timer payloads now use common per-pair pools backed by one retained arena, prepared to actual actor capacity and exact profile timer extent. Admission/spawn/cheat/capture vectors use independent control leases, and retired rows are returned only after the outer callback scope leaves. Timer activation, int32 arithmetic, save order and candidate restore custody remain unchanged. Normal production build and seven core suites passed. Additional nested admission/cold restore can extend common storage; deeper inventory admission allocations remain outside this slice, so no whole-frame zero-allocation or live/performance claim. Records: `/tmp/qa-ta3196-supply-pools-20261012`.

- `d5510fd4` / 05-I5: Event resource and registration lookup uses the common index; retained row order, resource custody and existing save encoding are preserved. Normal build and seven core suites passed; no live or whole-frame allocation claim. Records: `/tmp/qa-ta3196-event-dictionary-index-20261012`.

- `1909b408` / J37-04: Physical route and binding admission prepare common arena-backed held/button spans; releases reuse admitted storage and repeats remain suppressed. Existing shared input checks passed, including eight overlapping inputs. Normal build and seven core suites passed; new authored source admission remains cold. Records: `/tmp/qa-ta3196-held-input-arena-20261012`.

- `ebe76596` / 04-027: Player-seat readers use the common index prepared from roster capacity and rebuilt at row/seat mutations and restore. Candidate order and actual live/non-retiring/role checks remain unchanged. Normal build and seven core suites passed; no runtime/performance claim. Records: `/tmp/qa-ta3196-player-seat-index-20261012`.

- `f72824b4` / partial adoption: The bot fallback reads the exact canonical actor slot rather than scanning live actors to recover it. Existing roster preference, slot bounds and liveness remain. Normal build and seven core suites passed; no live or performance claim. Records: `/tmp/qa-ta3196-bot-canonical-slot-20261012`.

- `b4ccf402` / partial adoption: Native Q2 combat retains the live numeric team without interning. All shared combat, guest component, player, missile, mode and bot consumers use the common reference and equality. Existing counted-name save encoding and color policies are preserved by core behavior checks. Normal build and seven core suites passed; no live or whole-frame allocation claim. Records: `/tmp/qa-ta3196-live-team-ref-20261012-v4`.

- `d77a17f4` / 05-H2: Component publisher cursors reserve the existing protocol owner extent in the shared arena at create and restore. Frame capture and retained restore delete cursor growth and reuse owned storage. Normal build and seven core suites passed; no live or global allocation claim. Records: `/tmp/qa-ta3196-component-cursor-load-storage-20261012-v2`.

- `9d227a33` / 26-03: Native BoxEdicts now uses a shared spatial snapshot lease; the private candidate collector and per-query calloc/free are deleted. Native traversal/filter/count semantics preserved. Retained metadata uses common arenas with actual-capacity growth; normal build and seven core suites passed, no live allocation or timing claim. Records: `/tmp/qa-ta3196-native-world-common-storage-20261012-v1`.

- `54b9b1d2` / 05-D3, J15-03, J15-07: Startup replay, Q1 command/content comparisons and remote Q3 game-type parsing now use existing shared context/text/integer primitives. Private duplicate bodies removed, original Q3 integer-prefix conversion selected. Normal build and seven core suites passed; no live or performance claim. Records: `/tmp/qa-ta3196-shared-command-text-20261012-v1`.

- `da8d551a` / J23-04, 22-01, 22-03: Compass points are constructor-owned before live use, including admitted restore extents; medic uses retained keys and common actor lookup. Operation private frame/request allocations and spare list replaced by common pool, with new-peak growth still open. Normal build and seven core suites passed, including nested continuation behavior; no live or zero-growth claim. Records: `/tmp/qa-ta3196-gameplay-common-storage-20261012-v1`.

- `74c952c1` / 02-15, 17-01: QC finale polling storage/index now prepared at source construction, reused on reset and reconstructed on restore with inactive owner/save encoding preserved. Restart uses shared dimension projection with live registry or existing edit. Normal build and seven core suites passed; no live/gate claim. Records: `/tmp/qa-ta3196-client-owner-preparation-20261012-v1`.

- `e19d15b2` / J11-04: Recipe and content sidecar admission/restore use one shared map inventory and entity application rule; duplicate inventory/path bodies removed. Optional misses, scoped ownership and existing formats/save fields retained. Normal build and seven core suites passed; cold allocations remain, no timing claim. Records: `/tmp/qa-ta3196-one-sidecar-rule-20261012-v1`.

- `b51370d7` / 16-010: Q2 semantic beam, flashlight and light identities use the common indexed handle store with full actor candidate checks. Unicast claims use common indexed pool backing, preserving domain and publication ordering; new peak growth remains a partial admission gap. Normal build and seven core suites passed; no live or zero-allocation claim. Records: `/tmp/qa-ta3196-indexed-q2-event-storage-20261012-v1`.

- `164b9aec` / 03-08, 04-025, J12-07, J37-01: The Q2 CLIENT metadata constructor and SDK variant are deleted; callers use the existing common profile constructor. Q3 force-model and SystemInfo scratch use protocol-extent storage, diagnostic rows use common leased arena storage, and rankings use the original C integer policy. Normal build and seven core suites passed; no live/gameplay or global completion claim. Records: `/tmp/qa-ta3196-common-client-primitives-20261012-v1`.

- `c2ce87ad` / J20-04: Q1 gameplay and Rogue flag observations use common world leases; the private Q1 frame pool, map reset scan and checkpoint scan are removed while public callback custody remains. Rogue source order and full IDs are preserved. Normal build and seven core suites passed. J33-01 remains open for builtin, Q2, modes and bot callers currently being migrated. Records: `/tmp/qa-ta3196-q1-common-snapshot-leases-20261012-v1`.

- `56e76dfc` / partial adoption: Moved retained presentation, bot and profiler names to common indexed storage. New admission peaks can still grow storage; this does not establish whole-frame zero allocation. Records: `/tmp/qa-ta3196-common-retained-name-storage-20261012-v1`.

- `2d8e7c02` / J33-01, J24-01: Private builtin actor snapshot types and pools deleted from all source callers. Q1/Q2/Q3/QC scoped queries use common world storage; modes and bots retain constructor-sized borrowed arena backing. Original order, callback custody and bot save encoding preserved. Actual common-pool capacity and newly admitted authored candidate growth remain bounded by their owners; no full live or global zero-allocation claim. Records: `/tmp/qa-ta3196-one-world-snapshot-storage-20261012-v1`.

- `4632419f` / J23-06: Both landmark rotation copies deleted; shared ordered float Euler primitive preserves caller axis mapping and sign/conversion order. Normal build and seven core suites passed, with positive/negative noncommuting rotation checks. Records: `/tmp/qa-ta3196-one-ordered-vector-rotation-20261012-v1`.

- `b0e238b0` / J20-10, J21-06: Delete duplicate armor encoding; inventory store/admission and pickup observers use common pooled storage with preserved restore/callback custody. Q2 perception has constructor/restore-sized sealed storage. Reinforcement payloads use existing monster pool; native surfaces use common semantic name/value index. Additional authored/historical demand and native first-use guest allocation remain partial; no global zero-allocation claim. Records: `/tmp/qa-ta3196-common-gameplay-owner-storage-20261012-v1`.

- `4a49aafc` / J19-F08: Q1 addon and weapon lightning use one traversal with explicit preserved caller policies; the addon copy is deleted. Normal production build and seven core suites pass; no live or timing claim. Records: `/tmp/qa-ta3196-one-electric-traversal-20261012-v1`.

- `24b72287` / 07-01: Cvar notifications and nested observer snapshots use reusable common pool/arena leases; immutable declaration metadata is shared by reference. Private per-notification copies are deleted. Normal build and seven core suites pass; cold admission and higher shared-storage demand remain possible. Records: `/tmp/qa-ta3196-common-cvar-notification-leases-20261012-v1`.

- `ecae36eb` / 04-010, J15-05: Native Q3 retained text uses shared control pool pages with callback-safe child arenas; QW/Q3 client userinfo projection uses fixed protocol buffers and published revision gating, preserving prepared edits and reliable retries. Original text encoding remains unchanged. Normal build and seven core suites pass. Records: `/tmp/qa-ta3196-retained-client-userinfo-storage-20261012-v1`.

- `f39b89c5` / 02-03, 30-03: Guest selected weapons use retained canonical IDs; one Q1 admission binder also serves builtin names/models. Existing Q3 Cvar_Update overlap rechecked: typed handles and inline ABI projections already replaced warm name routing/private heap mirrors. Required alias-value comparison remains. Operation hook records now use common pool/arena, with honest new-demand growth. Normal build and seven core suites pass. Records: `/tmp/qa-ta3196-admitted-weapon-ids-operation-storage-20261012-v1`.
