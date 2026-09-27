# Recovery audit

The user required Jev to judge all prior work against what the dependency graph required. This audit starts immediately. It does not replace or narrow any original task, or change the user's source-only phase restriction.

The starting committed source is `c6f7db5c4715416393d63af7e5c994ba59194caf`. Existing tracked edits and untracked production files are preserved and included. Workers freeze their current implementation before reviewing other authors' source. Each lane records its exact inspected paths and identifies unfinished edits explicitly.

## Why the audit is required

The seven running Codex Vibecheck MCP processes lacked `TYPESAFE_API_KEY`, although the key existed in the user's `.bashrc` and was available to the working shell. The plugin creates no ledger verifier when its process cannot resolve a judgment source. At discovery, project history held 249 reports and one judgment recognizing a user pause. That pause judgment explicitly skipped checking the reply. Earlier pre-ledger main-conversation Stop judgments existed; they do not establish implementation audit coverage.

The local plugin launcher now obtains the existing key through interactive Bash when it is absent from the inherited environment. The subprocess disables automatic tmux startup and keeps shell startup output and credential bytes out of the MCP stream. An environment-stripped source probe reached `jev-1.13.0` successfully. Already-running MCP processes retain their original state; checked mutations during this session use `/home/buzzkill/.local/share/vibecheck-jev/call.mjs`, which starts the installed plugin with the repaired launcher. No key was moved into the repository or copied into configuration.

Plan revision 6 and the root AUDIT claim produced recorded Jev verdicts. The plan coverage check passed; brief-scope flagged the audit addition as deferral. The user explicitly ordered this audit now, and the original build/runtime phase boundary is unchanged. That flag is retained for review, not silently treated as a pass. Automatic agent-brief hook coverage has not been established; use explicit checked calls and retain their results.

Nine tasks retain historical `complete` reports: B00-B07 and B09. They are not verified completions. The ledger rejects reopening terminal work records, so AUDIT is an explicit prerequisite of BASELINE and tracks the re-review and corrections without rewriting history. Do not use those nine reports as evidence that their acceptance criteria passed Jev.

## Review assignments

| Reviewer | Production scope | Existing graph tasks |
|---|---|---|
| design_isolated | core, content/catalog, formats, world, session/scheduler, platform file/mapping | B01-B07; catalog portion of B15 |
| native_q1 | Q2 gameplay, items, players, entities, monsters, weapons/projectiles | B11; Q2 authored obligations in B13 |
| input_integration | Q1 gameplay/maps, campaigns, movement, shared gameplay and builtins | B08-B10, B13 |
| design_judge | Q3 gameplay and shared modes/equipment | B12, B14; Q3 authored obligations in B13 |
| native_q2 | rendering, audio/media, input, console/settings/text, device platform, configuration, CMake and executable entry | B15-B21, B34 |
| native_q3 | QC/QVM/native compatibility and host adapters, bots/navigation, network | B22-B31 where source exists |
| root | plan/source-target coverage, documentation, source tools, aggregate findings and missing B25/B28-B34 integration | B00 and whole-graph coverage |

Each scope includes its public/private headers and existing tests as source. No project tests, builds, generators, executables, sanitizers, or benchmarks run. Six existing workers perform the independent reviews; no additional workers are required.

## Evidence and judgment

For every B00-B34 task, retain its existing goal and acceptance criteria verbatim. Record source paths, functions and line references for each criterion; compare relevant donor contracts; list defects, missing behavior, disconnected callers, and files still unread. A file inventory or an author's report alone is not evidence of implementation correctness.

Submit that evidence through the installed Jev `check report --project quake-anthology --task Bxx` command and retain the exact output and exit status alongside the report. This command reads the existing graph task from the ledger. It judges the supplied evidence/report, not repository files it has never received. Independent source readers therefore remain responsible for accurate, sufficient evidence. A clean report check establishes that the report does not overclaim; it does not prove an incomplete implementation meets all criteria. State both judgments separately.

Root also submits each task packet to the same Jev service using `/home/buzzkill/.local/share/vibecheck-jev/judge-task.mjs Bxx REPORT`. This supplemental question explicitly asks whether the work fulfills the graph goal and every criterion, with `supported`, `incomplete`, and `insufficient_evidence` outcomes, and asks separately about each criterion. It does not substitute a report-honesty pass for acceptance. The resulting JSON retains the model, questions, probabilities, graph/evidence hashes, and request identity. This is a direct Jev comparison in addition to the plugin's report check; it is not a built-in plugin ledger verdict.

The initial B00 packet did not pass these checks: the report check flagged overclaiming at 0.65, and direct comparison selected incomplete with probabilities 0.38 incomplete, 0.35 supported, and 0.27 insufficient evidence. The root's graph work therefore remains unaccepted by this audit; an independent worker is reviewing its evidence. These results are retained without lowering thresholds or retrying unchanged evidence for a favorable answer.

Confirmed defects go to the original source owner for correction, followed by independent re-review and another Jev judgment. Missing implementation stays open under its original graph task. Root integrates local commits. Full source audit coverage, resolved defects, and actual judgments are required to close AUDIT; runtime qualification remains P01 after the entire baseline source is complete.
