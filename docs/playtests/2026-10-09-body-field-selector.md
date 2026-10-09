# THE-344 common body-field selector

QVM, native Q2 and declared QuakeC bindings now resolve their five body fields
to `qa_body_vector_kind` at load. One inline accessor beside `qa_body_state`
replaces their separate getter/setter chains. External field names, encodings,
ownership and saved declarations remain at the module boundary.

The actual-source comparison packet is
`/tmp/qa-the344-body-selector-20261009`. GCC and Clang, optimized and
ASan/UBSan, passed 24 executions: all twelve before/after pairs are identical
(944 QVM, 2,594 native Q2 and 444 QC bytes per pair). QVM/native checkpoint
decoders restore into fresh actor namespaces and re-encode identical bytes.
QC checks cover the exact body branches, rather than a complete QC runtime.
All eight changed C units passed both strict compiler checks. Foreign memory
and metadata are controlled boundaries; this is not retail module gameplay.

The production, ASan/UBSan and allocation-gate builds and seven core checks
each passed in `/tmp/qa-common-selectors-handles-build-20261009`. Its frozen
source tree is `e186a88a0643665e38da5e6a2a89e89136a11060`, containing this
eleven-path slice and the seven-path QW handle slice. These are joint candidate
build results, not separate builds of each intermediate commit. No installation
was made.

Pinned ABBA component timings used 120 warm-up refreshes and 600 samples of
48 refreshes, affinity `0-7,12-19`, with no debugger. Median nanoseconds per
five-field refresh:

| Component | A1 | B1 | B2 | A2 |
| --- | ---: | ---: | ---: | ---: |
| QVM | 277.292 | 257.104 | 256.677 | 263.750 |
| Q2 classic | 292.521 | 302.292 | 301.896 | 291.469 |
| Q2 rerelease | 295.021 | 314.583 | 302.615 | 295.646 |

`timings.json` contains the p99s. No speedup is claimed: native medians were
about 3–5% higher, or 10–13 ns per refresh. These component measurements do
not establish frame-time performance. The change removes duplicate field
selection, without adding storage or heap allocation.
