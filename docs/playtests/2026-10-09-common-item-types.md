# THE-344 common item types

QVM item requests use the existing `qa_weapon_request_status` directly through
request, status, cancellation and checkpoint APIs. The separate internal enum
and its equipment translation are deleted. QC, QVM and native Q2 adapters use
one `qa_item_bit` from the common inventory header; their three copies are
deleted. No module ABI layout or original weapon phase changes.

GCC, Clang and both ASan/UBSan original/candidate pairs compare actual extracted
request/status/cancel and inventory-reader behavior. Each pair produces the
same 349 bytes. Cases include pending/accepted/refused transitions, stale
receipts, cancellation, missing or undeclared weapons, private/high item bits,
undeclared-bit rejection and exact signed inventory counts. Numeric status
values remain 0/1/2; item/mask offsets and widths are unchanged. Guest liveness
and memory are controlled services in this component check, not full VM play.

The checkpoint codec still writes/reads its existing u32 status field. Its byte
layout is unchanged by inspection; this fixture does not exercise a complete
checkpoint round trip.

The isolated ten-path slice passed production, ASan/UBSan and allocation-gate
builds and all seven configured core checks in each. The SDK source attestation
contains 3,102 directly compared inputs and excludes unrelated working-tree
changes. Evidence is retained at
`/tmp/qa-the344-item-types-20261009/receipt.json` and
`/tmp/qa-the344-item-types-build-20261009`.

No frame-time gain, whole-frame zero-allocation result, gameplay qualification
or installation is claimed. Remaining common-type adoption is listed in
`docs/core-adoption.md`; THE-344 stays In Progress.
