# Common current PlayerState and rule-set family

THE-344 moves 21 generic current fields into the existing actor-indexed
control owner, `qa_player_state`. The former public subset type and all 54
type uses migrate together; 725 typed member references follow the same
owner. No second player table, cache or validation context is introduced.
The control record stays 976 bytes. Its temporary public snapshot grows
from 440 to 464 bytes by using the common owner. BODY publication and
completed command history remain separate data.

The common game-family enum moves unchanged into the rule-set header. One
descriptor lookup replaces three equivalent switches and seven family
projections, preserving numeric values, caller defaults and role choices.

## Verification

The exact staged 56-path source was frozen as Git tree
`c9a5cdae41c5fb861f81bac6be8a9329153ccdde` on `9b5aff0e`. Production,
ASan/UBSan and allocation-gate builds pass. Platform services, core, archive,
VFS, BSP, image and model checks pass in all three builds (7/7 each).
Build receipts: `/tmp/qa-the344-common-player-build-20261009/`.

Eight actual-source GCC/Clang plain and sanitizer component runs compare
69,384 bytes each across five movement families, four rounding modes and
three player modes, including completed/in-flight snapshots and checkpoint
restore into a fresh actor namespace. The component substitutes the active
state lookup, two unchanged numeric admission callbacks and an error helper;
linked dependency libraries were private copies of prior built libraries.
Ninety complete translation-unit syntax checks pass. The family component
also passes 16 strict TU checks and four plain/sanitizer executions, covering
3,300 profiles and 13,200 encoded configurations per execution.

Pinned read/copy timing uses cores `0-7,12-19`, no debugger, 100 warm-up
iterations and 600 samples, each averaging 1,000 batches of 48 reads:

| Phase | Median ns | p99 ns |
| --- | ---: | ---: |
| Old A1 | 618 | 665 |
| Common B1 | 412 | 478 |
| Common B2 | 413 | 458 |
| Old A2 | 526 | 550 |

These are bounded read/copy measurements with a controlled active lookup,
not frame-time or original-module gameplay evidence. Component receipts are
in `/tmp/qa-the344-common-player-owner-20261009/` and
`/tmp/qa-the344-ruleset-family-20261009/`.

## Remaining work

THE-344 remains open. Q3 still retains generic selected-player mirrors,
source-to-control bridges and phase copies; their full caller migration is
next. Generic roster/life/connection custody and other type duplicates remain
listed in `docs/core-adoption.md`. Whole-frame allocation and live combined
mode are not proved by this ownership slice. This build is not installed.
