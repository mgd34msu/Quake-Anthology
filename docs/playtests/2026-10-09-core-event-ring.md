# Shared event ring

THE-344 / THE-870 moves the existing load-sized page ring into `src/core/event_ring.c`, exported by `include/qa/event_ring.h` and built in `qa_core`. All eight application paths use that primitive. The private `event_pages.c` and `event_pages.h` are deleted; their source and API names have zero references in `src`, `include` and CMake.

This is a literal symbol/include migration. The storage algorithm, capacities, cursor retirement, lease ownership, backpressure and transaction rollback are unchanged. A retained lease still pins its payload after lookup retirement, slot reuse and owner destruction. No codec, wire field, guard, capacity or failure path is added.

The staged build inputs are tree `a8a3b9576ffedee33127ba619f6501848e67d35e`, based on `1d5d8ac1`. Full production, ASan/UBSan and allocation-instrumented builds passed. All seven configured core suites passed in each build. Final-source GCC and Clang fixtures, plain and ASan/UBSan, compare both implementations across 12 scenarios and 10,000 deterministic stress transactions per run. All eight runs preserve 1,487 encoded typed payload bytes. Warm malloc/calloc/realloc/free counts are zero. Creating 12 owners uses 12 malloc and 36 calloc calls; destruction uses 48 free calls, unchanged before/after. The private strict checks also passed for every changed source translation unit with both compilers.

Evidence is in `/tmp/qa-the870-core-ring-20261009` and `/tmp/qa-the870-core-ring-build-20261009`: explicit source mapping, migration scan, staged-source attestation, build/test logs and final-source `component.json`. This slice makes no timing claim and installs no executable.

THE-870 remains in progress. Outgoing event projection still deep-copies payloads in `unified_events.c`; incoming event decoding and `remote_unified_events.c` still own separate allocated batches. Those callers must move to the common primitive with their full document and consumer lifetimes preserved. The existing frame lease pool also still needs load-time reservation before it can pass the whole-frame allocation gate.
