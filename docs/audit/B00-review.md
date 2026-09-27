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
