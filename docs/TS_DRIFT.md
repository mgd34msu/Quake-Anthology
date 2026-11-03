# TS drift since the C port began

The C port started from TS revision `53ad56d` (2026-10-25). TS has had 44 commits since,
including the 2026-11-01 audit remediation (`2a2cb7bd`, 1537 files) and the SourceStrategy
refactor of SharedSimulation. Every TS change since the baseline must be reflected in C
before the confirmation gate can pass.

## Tools

- `bun tools/ts_semantic_drift.ts ../quake-anthology-ts 53ad56d > docs/ts_semantic_drift.tsv`
  classifies every TS file: `code` (behaviour changed), `types-only`, `format-only`,
  `added`, `removed`, `same`. Only `code`, `types-only`, `added` and `removed` need work.
  The 2026-11-03 Biome reformat (`6420d047`) is semantics-free except for 5 files
  (application.ts, network/remote-unified.ts, simulation/runtime.ts,
  compat/q2/native-mod-provider.ts, content/q1/foundation/spawns.ts), which the diff tool covers.
- `tools/ts_semantic_diff.sh FILE...` or `tools/ts_semantic_diff.sh --module content/q1`
  prints a diff with the baseline run through TS's own Biome config first, so the reformat
  drops out and only real changes remain. New files print a note to read them in full.
- `python3 tools/ts_drift_files.py` (mtime-based, coarser) writes `docs/ts_drift.tsv`.

## Counts (2026-11-03)

src: 694 code, 393 types-only, 88 added, 68 removed (10 of them moves, listed below).
tests: 415 code, 140 added.

Moves: platform/files/{contained,writable} -> core/files; persistence/q1-source ->
compat/qc/q1-source-save; persistence/q1-quakec -> compat/qc/q1-save;
ui/common/legacy/script/* -> content/legacy-script/*.

## Procedure for a drift packet

1. For each file in the packet, read the semantic diff. Many C files were written after the
   TS change landed, so first check whether C already has it; do not apply a change twice.
2. Apply each real change to the C counterpart (manifest row, or search by symbol).
   Added TS files get ported (or mapped to an existing C file) and a manifest row.
   Removed TS files: delete the C counterpart only if nothing in C still needs it and TS
   moved or dropped the behaviour; otherwise note it in the open items.
3. Port the changed and added TS tests for the packet, with bun-oracle expected values.
4. Build with tools/agent_build.sh (private BUILD dir, -j1) and run every affected test
   binary plus tests/app/test_sim_verify (must stay at 0 differing lines; run it from the repository root: the user content root, whose path is recorded in checkpoint images, is fixed at build/agents/sim-verify2/tmp regardless of TMPDIR).
5. Record each file as `drift-done` in docs/port_review.tsv with a one-line note.

## Packets

| Packet | Scope (src/…) | Diff lines |
|---|---|---|
| D1 | app/bootstrap/simulation | 14.8k |
| D2 | app/bootstrap (rest) incl. network, effects | 12.4k |
| D3 | content/q1 | 9.2k |
| D4 | content/q2 | 6.3k |
| D5 | content/q3, bots | 5.3k |
| D6 | compat, guest | 8.1k |
| D7 | network, render, world, movement | 9.5k |
| D8 | ui, core, formats, persistence, contracts, audio, materials, text, console, settings, input, llm, platform, media, camera, capture, debug, foundation | 6.5k |
