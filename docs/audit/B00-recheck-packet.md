# B00 corrected evidence for a new judgment

Judge the final corrected B00 allocation and publication work. The historical review below found six direct target-to-owner omissions and missing nonterminal recovery tracking. The subsequent independent correction review below confirms the fixes. The task is graph publication and scope allocation, not completion of the engine implementations. All implementation tasks keep their existing goals, criteria and unfinished work. Plan revision 6 was acknowledged through the keyed installed plugin; the response at /tmp/qa-jev-audit-ack-result.json returned revision 6 and acknowledged_plan_revision 6. The original Jev rejection is preserved separately in B00-acceptance.json and B00-report-check.txt.

The root corrected the stale contribution count after the correction review. The remaining original checker limits are explicit; no checker was executed. Source review establishes the metadata changes, while whole-project behavior is not claimed. This packet contains the coordinator evidence, independent review of all T01-T23 mappings, and independent re-review of the identified corrections in chronological order.
# B00 source evidence

Goal: Publish the complete dependency graph and current user rules to vibecheck-jev; keep a reviewable local copy.

Acceptance criterion: Ledger and local graph cover all 23 donor functional targets; baseline completion precedes gameplay evaluation and deep enhancement.

Source evidence reviewed by the coordinator:

- `../quake-typescript/docs/functional-targets/README.md:30` through line 52 enumerate T01-T23. `docs/source-map.json` explicitly maps every corresponding target identity to B00-B34 implementation owners and P01/P02/P03/RELEASE qualification owners. This is scope allocation, not a claim that those features are implemented.
- The donor source contains 27 top-level directories and `src/main.ts`. The source-map records each of these exact paths with C task owners. The reviewed directory names are app, audio, bots, camera, capture, compat, console, content, contracts, core, debug, formats, guest, input, llm, materials, media, movement, network, persistence, platform, render, settings, text, types, ui, world.
- `docs/dependencies.json` contains B00-B34, BASELINE, P01, P02, P03, RELEASE and the user-requested AUDIT. BASELINE directly depends on every B00-B34 task and AUDIT. P01 depends on BASELINE; P02 on P01; P03 on P02; RELEASE on P03. `docs/dependency-graph.md` contains the same direct prerequisite table and phase diagram.
- Jev ledger plan_edit request `audit-recovery-plan-20260927` returned plan revision 6 with the AUDIT addition and BASELINE dependency. It returned a passed plan-coverage judgment and a flagged brief-scope judgment. The latter is retained, not represented as a pass. The user expressly required immediate retrospective judgment; the audit does not move existing implementation into a later release or alter the source-only phase boundary.
- `docs/plan.md` states the unified-engine product contract, independently selectable behavior, full game/expansion/rerelease/mod scope, shared services, native built-in execution, ordinary C float permission, no donor source-file copying, local commits, six workers, root-only licensing, and the explicit source-before-build sequence. These are consistent with the task's current user instructions.

Defect found and corrected: `docs/source-map.json` still carried ledger revision 4 while the previous dependency graph carried revision 5. Both now name revision 6, matching the current ledger. The source of `tools/check_plan.py` already checks equal revisions, but was not executed under the user's source-only restriction. It now also requires AUDIT and its direct dependency from BASELINE. The source checker checks task IDs, dependencies and cycles, source assignments, and phase ordering; it does not qualify engine implementation.

B00 remains unaccepted pending a new combined judgment. The initial report overstated the evidence: enumerating 23 target IDs and assigning directories proves scope allocation, but does not establish that each target's substantive requirements have appropriate graph owners. The subsequent independent review in `B00-review.md` found six incomplete direct allocations. Root added the missing existing owners; `B00-corrections.md` independently confirms those changes and the nonterminal recovery register. The checker now also enforces the full P01/P02/P03/RELEASE ancestry chain. It compares local revision numbers, not the live ledger, and does not validate donor path completeness or semantic target ownership. Its source has not been executed.

The initial Jev results are preserved: report overclaiming was flagged at 0.65; the direct comparison selected incomplete (0.38), followed by supported (0.35) and insufficient evidence (0.27). This corrected report does not retroactively change those judgments. No other task is declared complete by this report. The nine historical completed-task reports were not Jev-verified. Their terminal ledger records cannot be reopened or claimed again through the API; their original scopes remain open in the recovery audit before BASELINE. Full project source audit is ongoing, and no game build, test, generator, executable or benchmark was run.
# Independent B00 graph and source-map review

Status: unaccepted pending corrections to semantic target ownership and independent re-judgment. This is a source/metadata review of B00, not an implementation-completion or runtime qualification report.

Reviewer: `/root/design_judge`, session `s_b5a15ba5395f4807bbde4b547d075b58`, AUDIT contribution `w_876a77f4d6e94755b6e8c57d9a26679d`. The inspected repository HEAD was `08195f5cdfffd741ef2732e7771b9517446cc233`; donor HEAD was `5d6c6e311db6749c0b8f8ffca53ebe2bae69b24e`.

Inspected evidence:

| File | SHA-256 |
| --- | --- |
| `../quake-typescript/docs/functional-targets/README.md` | `750edec7643efe77c0511918cebf22e6c983a050835d1bb9b6326b742cfb8033` |
| `docs/source-map.json` | `d551d4d584a20c9a30c138786d73bb21da93d37bfe0f2bac475cc17a2d2569cd` |
| `docs/dependencies.json` | `915c4c50da8fcb60ed206f09f2d325eb7755f4c6f3f48b1c3d97d83d2eb98bae` |
| `docs/dependency-graph.md` | `a77cfe8289303c6ae6b29af2826653dd62fb755c2eaa5c6e9917fcd9992fcfbf` |
| `docs/plan.md` | `0dcb2ddaad8222bc129388abfae7f356a63a59f23a134e5feec89d8d277305ad` |
| `tools/check_plan.py` | `ad4b745067f269bb32c93f5eabfe97f176a1ae103afe7f132ce5b4bd3bfe789c` |
| `docs/audit/B00-evidence.md` | `f41f55562d2356addc8c131737499aace8cb2f40bf002ff5964cdd725e2fab56` |
| `docs/audit/B00-acceptance.json` | `69260f8eca88754a2d23030ffb2863c1ba1428ce3e40fa5a26fd15fbd5d3769e` |
| `docs/audit/B00-report-check.txt` | `03dbceca4fc23927284fdf699c3d6aef9717dacffe6102283631e667a44d44c3` |

No project configuration, build, compiler, test, executable, generator, sanitizer, benchmark, or gameplay run occurred. `tools/check_plan.py` was read and was not executed.

## Criterion disposition

B00's goal is to publish the complete dependency graph and current user rules to the ledger and retain a reviewable local copy. Its acceptance criterion requires the ledger and local graph to cover all 23 donor functional targets and requires baseline completion before gameplay evaluation and deep enhancement.

The phase order is represented correctly in the current local graph: `BASELINE` directly depends on B00-B34 and AUDIT, then P01 depends on BASELINE, P02 on P01, P03 on P02, and RELEASE on P03 (`docs/dependencies.json:540-633`). Adding AUDIT changes the BASELINE acceptance gate. It does not delay or serialize source implementation because AUDIT and B00-B34 have no new dependency edges between them, and the policy explicitly permits source work to overlap once interfaces are published.

The donor target IDs and top-level source inventory are also present. `README.md:30-52` defines T01-T23. At the recorded donor revision, `src` has exactly the 27 directories listed in `docs/source-map.json:399-674` plus `src/main.ts`; the tree read found no omitted or invented top-level entry.

That establishes identifier and directory allocation, but the target-to-task map is not yet semantically complete. Several donor requirements have dedicated graph owners that are absent from the corresponding target's `implementation_tasks`. The omissions do not erase those B tasks from the overall graph, but they make the source map an incomplete ownership account and weaken B00's claim of complete target coverage.

## T01-T23 semantic ownership check

“Aligned” means the listed task goals collectively name the donor target's implementation obligations. It does not mean their code is finished or correct. “Incomplete direct allocation” means a requirement has an identifiable task owner elsewhere in the same graph but that owner is absent from the target mapping.

| Target | Current implementation tasks | Semantic result |
| --- | --- | --- |
| T01 Session and resource lifetime | B03, B07, B20, B21, B28, B30, B34 | **Incomplete direct allocation.** The donor target explicitly includes renderer, audio-device, menu, and candidate-publication lifetime. Their dedicated owners B16/B18, B19, B32, and B15 are reached only indirectly through application integration and are not recorded here. |
| T02 Content and asset loading | B02, B03, B04, B05, B15, B29 | **Incomplete direct allocation.** Archives, mounts, map/model/image formats, catalog and remounting are covered. Shared material/media asset consumption and audio/movie codecs belong to B16/B19 but are not traced despite the target's asset-codec, streaming, logical-dimension, and both-renderer requirements. |
| T03 Rendering and visual effects | B05, B16, B17, B18, B32 | Aligned: formats/resources, shared scene/materials, CPU and GL backends, and presentation consumers have named owners. |
| T04 Audio and music | B19 | Aligned: B19's goal covers decode, music, voice policy, mixing, per-seat audio, synchronization, and devices. |
| T05 Input and local players | B08, B20, B21, B28, B32 | Aligned: commands, devices/seats, bindings/settings, remote-seat integration, and UI are represented. |
| T06 Console, cvars, and profiles | B21, B32 | Aligned at the shared-service level: B21 owns command/cvar/profile semantics and B32 owns public menu/HUD consumers. Source-family command coverage must still be established by the implementation audits. |
| T07 Network connections and prediction | B08, B27, B28 | Aligned: movement/prediction, dialect transport/codecs, and session integration are named. |
| T08 Downloads and content acquisition | B03, B29 | Aligned: retained mounts and the download/remount/browser owner cover the target; B29 depends on transport and settings owners. |
| T09 Server discovery and administration | B21, B27, B29, B32 | Aligned: settings/commands, transport, discovery/admin/rotation, and UI are named. |
| T10 Gamecode and mod execution | B09, B10, B11, B12, B15, B22, B23, B24, B25, B30, B32 | Aligned: shared gameplay, native families, configuration, QC/QVM/native adapters, composition, persistence, and guest presentation are represented. |
| T11 Collision, movement, and scale | B06, B07, B08, B16, B28 | **Incomplete direct allocation.** Collision, schedules, movement, scene transforms, and prediction are mapped. The target explicitly requires model scale and attack transforms; their dedicated owners B05 and B09 are absent. |
| T12 Combat, rosters, pickups, and equipment | B09, B10, B11, B12, B14 | Aligned: shared combat/inventory, all native gameplay families, modes, grapples, and offhand equipment are named. |
| T13 Bots, AI, and navigation | B10, B11, B12, B26 | Aligned: source monster AI and the shared player-bot/navigation owner are named. |
| T14 Map entities and campaigns | B04, B07, B10, B11, B12, B13 | **Incomplete direct allocation.** Formats, scheduling, authored family behavior, and campaigns are mapped. The donor target also requires cinematics and durable revisited-world state; B19 and B30 are their dedicated owners but are absent. |
| T15 Match modes and objectives | B14, B28, B31 | **Incomplete direct allocation.** Rules, remote integration, and records are mapped. The target also requires rule-setting surfaces, hosting configuration, and pre-launch map-eligibility feedback, owned by B15/B29/B32. |
| T16 Saves, autosaves, and recovery | B07, B13, B25, B30, B32 | **Incomplete direct allocation.** Scheduling, campaign/mod ownership, persistence, and UI are mapped. The target explicitly includes QC/QVM/native guest state, while the checkpoint/saved-state owners B22/B23/B24 are only present through B25's dependency closure. |
| T17 Menus and HUD | B15, B16, B20, B21, B25, B32 | Aligned: catalog, scene resources, input, settings/text, mod controls, and UI/HUD have named owners. |
| T18 Localization and accessibility | B19, B21, B32 | Aligned: timed media/captions, text/localization/settings, and presentation are named. |
| T19 Progression and player services | B13, B14, B31, B32 | Aligned: campaign/match event sources, records/services, and user flows are represented, including the approved ranking-provider boundary in B31. |
| T20 Demos, recording, and replay | B27, B28, B30, B32 | Aligned: recorded protocols, session playback, persistence/codecs, and controls are named. |
| T21 Cinematics and animated media | B16, B19, B32 | Aligned: texture/material consumers, decode/synchronization, and menu/world flows are named. |
| T22 Cameras, diagnostics, and tools | B16, B18, B21, B33 | Aligned: scene resources, display/readback, console integration, cameras/debug/capture/tools are named. CPU presentation readback remains an implementation-review obligation under B18/B33. |
| T23 LLM assistance | B21, B33 | Aligned: the real command catalog/permissions and provider/model/cancellation integration are named. |

The six incomplete direct allocations are metadata defects, not permission to narrow any target. The minimum correction is to add the omitted existing owners to the appropriate target entries and keep their original goals and criteria intact. B34 remains the final application integration owner rather than a substitute for component ownership.

## Checker findings

`tools/check_plan.py:9-81` verifies duplicate task IDs, the presence of required IDs, prerequisite references and cycles, a BASELINE ancestor for phase tasks, direct B00-B34/AUDIT prerequisites on BASELINE, exact T01-T23 identifiers, nonempty valid owner arrays, `src/main.ts`, duplicate listed paths, and equality of the two local revision integers.

It does not establish:

- equality with the live ledger revision; two stale local files with the same number pass;
- donor filesystem completeness or whether a listed path exists;
- semantic agreement between a target's donor requirements and its assigned tasks;
- exhaustive task IDs, because the required IDs are checked as a subset;
- the exact P01 to P02 to P03 to RELEASE chain, because any BASELINE ancestor satisfies the phase test; or
- implementation or integration completeness.

The historical metadata defect is confirmed. Immediately before commit `fd23b0bce3808dd9e66554ee86be7d2dc49eb772`, `docs/dependencies.json` and `docs/dependency-graph.md` named plan revision 5 while `docs/source-map.json` named revision 4. Current local metadata names revision 6, and a live ledger read also returned plan revision 6. That live comparison came from the ledger read; the checker cannot make it by itself.

## AUDIT and terminal-history semantics

AUDIT is a real prerequisite of BASELINE, so unreviewed historical work cannot satisfy the source-completion gate. Its addition leaves the source-only phase boundary unchanged. P01 still begins after BASELINE and owns compilation/runtime evaluation; P02 remains the later performance pass.

AUDIT criterion 4 also requires missing functionality and unsupported completion claims to remain open in their original tasks. Nine historical ledger work records are terminal and cannot be reopened or claimed again. A single AUDIT gate records the aggregate recovery but does not, by itself, provide nonterminal status keyed to each original B00-B34 scope. The coordinator has stated that it will add that explicit keyed tracking. Until that exists, the local wording should describe the limitation rather than claim criterion 4 is satisfied.

## Preserved Jev results

The initial evidence packet did not pass. `docs/audit/B00-report-check.txt` records `claims-done overclaims=0.65 FLAGGED`. `docs/audit/B00-acceptance.json` selected `incomplete` with probabilities 0.38 incomplete, 0.35 supported, and 0.27 insufficient evidence; criterion 1 received `noul=0.55`. These outputs remain the applicable B00 judgments for that packet. Editing the evidence cannot retroactively turn them into acceptance. A new comparison is warranted only after the semantic allocation and status-accounting defects are substantively corrected.

Unreviewed B00 scope: none of the assigned graph/source-map/checker packet. Production implementation under B01-B34 was outside this B00 review and is covered by the separate source-audit lanes.
# Independent B00 correction review

Status: the bounded semantic-allocation, phase-chain, and nonterminal-status corrections are supported by source review. The initial B00 evidence and Jev rejection remain historical snapshots; this review does not retroactively accept them or replace the required new combined judgment.

Reviewer: `/root/design_judge`, session `s_b5a15ba5395f4807bbde4b547d075b58`, AUDIT contribution `w_4392699854054b8d8658c2ba7565d976`. The inspected repository HEAD was `4aadab1c7c12d25c1af7c39003db689abdd9a578`; the files were dirty coordinator changes at the hashes below.

| File | SHA-256 | Review result |
| --- | --- | --- |
| `docs/source-map.json` | `d4f4a35437ac4642561e19641f78b6a759caa7937144a656ad47650a142cde07` | All six independently identified direct-owner omissions are corrected without removing an existing owner. |
| `tools/check_plan.py` | `cb9594ba7d000c62d78e68fd2f292684eb262baabe0114c7c796eda21ccdff6b` | Adds the missing ordered phase ancestry checks; remaining documented checker limits are unchanged. |
| `docs/audit/status.md` | `c9da8fea53f835b6bb5a6a967f0def838ec7a676c6a4e9de469fb91a2912ab12` | Records an explicit nonterminal recovery state for every original B00-B34 scope under AUDIT. |
| `docs/dependencies.json` | `915c4c50da8fcb60ed206f09f2d325eb7755f4c6f3f48b1c3d97d83d2eb98bae` | Unchanged plan revision 6 task definitions and phase edges remain the authority for the corrected metadata. |

No project configuration, build, compiler, test, executable, generator, sanitizer, benchmark, or gameplay run occurred. `tools/check_plan.py` was inspected and was not executed.

## Semantic target ownership

The source-map additions match the requirements and dedicated task owners identified in `B00-review.md`:

| Target | Added implementation owners | Requirement now attributed |
| --- | --- | --- |
| T01 | B15, B16, B18, B19, B32 | Candidate publication, shared scene/renderer/display, audio-device, and menu lifetime. |
| T02 | B16, B19 | Shared material/media consumers and audio/movie codec/streaming ownership. |
| T11 | B05, B09 | Model scale and attack/combat transform ownership. |
| T14 | B19, B30 | Authored cinematics and durable revisited-world state. |
| T15 | B15, B29, B32 | Rule-setting/hosting configuration and pre-launch map-eligibility presentation. |
| T16 | B22, B23, B24 | QC, QVM, and native guest checkpoint/saved-state ownership. |

No existing implementation or qualification owner was removed. Every added owner is one of B00-B34, and its unchanged graph goal names the attributed behavior. The corrected map therefore closes the six direct-allocation defects found in the prior semantic review without weakening a target or using B34 as a substitute for the component owners.

The donor T01-T23 identities, the 27 top-level donor source directories, `src/main.ts`, and the remaining target mappings are unchanged from the independently reviewed packet. This correction review is bounded to the changed allocations; it does not claim the mapped implementations are complete.

## Phase-chain checker

The new loop in `tools/check_plan.py:53-58` requires these ancestry relations:

- BASELINE precedes P01;
- P01 precedes P02;
- P02 precedes P03; and
- P03 precedes RELEASE.

Combined with the existing cycle/reference validation, this rejects bypassed or reordered phase chains even when every later phase still happens to have BASELINE somewhere in its ancestry. The current graph already has the exact direct chain, so the change strengthens source validation without changing plan semantics.

The checker still does not compare against the live ledger, inspect the donor filesystem, judge semantic target ownership, require an exact task inventory, or qualify implementation. Those limits remain accurately documented. The source was not run under the source-only restriction.

## Nonterminal recovery status

`docs/audit/status.md` has exactly one row for each original task B00 through B34. Every row uses an open recovery state such as unaccepted, under review, correction required/in progress, incomplete, or insufficient evidence. It explicitly states that historical terminal ledger reports are not accepted completion, ties the register to active AUDIT work `w_357ddd74810a4aa8a3bb84fdd7095fb8`, keeps AUDIT as a BASELINE prerequisite, and retains P01 as the later runtime-qualification phase.

This supplies the missing task-keyed nonterminal account without pretending the immutable ledger history was reopened. The row-specific implementation assertions remain subject to their assigned source packets and Jev judgments; this bounded review confirms the register's coverage and state semantics, not every cited production fact.

One nonblocking maintenance concern remains: the introduction says “the six independent review contributions” are recorded under AUDIT. That count described the original six lanes, but it is already not a complete count of ledger contribution records after this correction review. Removing the hard-coded number would keep the status register accurate as re-reviews are added.

## Disposition

The changed source-map allocations, ordered phase checks, and B00-B34 recovery register satisfy the specific defects identified in the independent B00 review. `docs/audit/B00-evidence.md` still correctly says B00 is unaccepted and semantic review is in progress at its own snapshot. Root should build a new combined evidence packet from the original evidence, `B00-review.md`, and this correction review, preserve the initial overclaim/direct-comparison outputs, and obtain a new Jev comparison. A favorable new judgment would assess the corrected packet; it would not rewrite the initial rejection.

Unreviewed correction scope: none of the bounded source-map/checker/status changes. Production source and row-specific task implementations remain with their assigned audit lanes.
