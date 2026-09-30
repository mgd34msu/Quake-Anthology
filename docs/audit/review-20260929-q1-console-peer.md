# Independent console numeric repair review, 2026-09-29

Reviewer: Q1/campaign review agent, GPT-6.1 Sol high effort. Source-only review of the foundations agent's locale repair; no runtime or platform verification.

## Initial patch finding

The initial `src/console/text.c` repair replaced direct `strtof` with locale-independent `qa_parse_atof` followed by a double-to-float cast. Under round-to-nearest and binary32/binary64 arithmetic, the decimal token `1.0000000596046447753906251` is above the binary32 midpoint between 1 and the next float. Direct binary32 parsing rounds up. Binary64 parsing first rounds to the exact midpoint `1.000000059604644775390625`, then conversion to binary32 rounds to even at 1. The intermediate conversion therefore introduced a precision regression while addressing locale dependence. This was established by source and arithmetic reasoning, not by executing a parser.

The foundations agent accepted the finding and replaced this implementation before integration. The initial version is superseded.

## Revised patch acceptance

The revised `qa_parse_atof_float` uses `strtof_l` or `_strtof_l` directly with the existing process-lifetime numeric locale and returns a float. The header declaration and `qac_number` call agree. Direct float parsing removes the intermediate binary64 rounding, retains prefix/nonfinite/overflow/underflow semantics of the old float parser, and selects the C locale independently of UI locale. The Q1-specific parser remains unchanged.

The two formatter callers use the existing `qa_format_fixed` helper. Both use 64-byte local buffers; the longest binary32 fixed output with six fractional digits fits that buffer. The existing 31-byte truncation or Q1 rejection policy remains in place. Integer formatting paths and nonfinite spelling are preserved. The helper borrows no caller-owned storage after return; its shared numeric locale deliberately survives for the process lifetime. The POSIX formatter restores thread locale and rounding state on the inspected branches. No remaining source-level defect was established in this bounded repair.

## Actual coverage and checks

Full source reads: `include/qa/text.h`, `src/core/number.c`. Targeted reads: `src/console/text.c` numeric parser, `src/console/cvars.c` number refresh and numeric setter, `src/console/commands.c` decimal validation and inc/dec handler. Both initial and revised diffs were inspected. This is not a review of the complete console subsystem.

`git diff --check -- include/qa/text.h src/core/number.c src/console/text.c src/console/cvars.c src/console/commands.c` exited 0 with no output. No configure, compiler, parser execution, tests, sanitizer, executable, or gameplay run was performed.

Reviewed SHA-256 hashes:

| File | SHA-256 |
| --- | --- |
| `include/qa/text.h` | `43271b0fae95066fc7ddac44c3105f1afb9360c6767cdffd04a74d2d6af685cf` |
| `src/core/number.c` | `091725950c2ec44fe53adf917ad63939cb2f625d1d8c3ed1f6015ee59f09a119` |
| `src/console/text.c` | `a8a9e8953bd19c9d72e51fa02e8f973678eec661c7029be2e9583e418acecb49` |
| `src/console/cvars.c` | `07a7a0de54a80124be60b389d02f7c453652c2beb1543e14f47a106e180c2bd7` |
| `src/console/commands.c` | `139945b9376345be56fb8a7313ea9ac393fa74c884b44da7530c76b938bbde83` |

Platform availability and live numeric behavior remain unverified under the source-only policy. Peer acceptance applies only to this frozen patch and inspected contracts, not to full AUDIT or BASELINE acceptance.
