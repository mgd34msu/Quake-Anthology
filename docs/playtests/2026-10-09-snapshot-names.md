# THE-344 / THE-873 common snapshot names

Actor owners and definitions, inventory items, movement profile/numeric names, and active/pending prediction weapons now carry `qa_string_id` from the existing session table. Producers copy the IDs already registered by the actor, item and movement systems. The receiving replica shares that table instead of creating another namespace and re-interning each actor name.

Frames, retained world cuts, event documents and network sessions retain the actual table until their last reference is released. The existing record codec compares IDs within one table and exact text across tables. Decoding and unchanged delta fields resolve into the receiving table. Wire and save boundaries continue writing nullable text; process-local IDs are never persisted. Two unused event encode/decode wrappers were deleted. There is no second codec, dictionary, identity fingerprint or format variant.

The focused GCC/Clang checks and ASan/UBSan runs preserve these exact outputs:

| Record | Before and after bytes |
|---|---:|
| Full frame | 184 |
| Delta frame | 64 |
| Inventory event document | 46 |

The fixture uses actual sessions with differently ordered string tables. It exercises colliding IDs in separate namespaces, unchanged delta remapping, NULL versus empty names, session teardown while documents remain held, retirement of a frame pool with a held decoded frame, and truncated-frame cleanup. An inventory-name clone changes from one text allocation to zero. This is a bounded clone result, not a whole-frame allocation or latency claim.

Full production, ASan/UBSan and allocation-gate builds pass, with all seven existing core checks passing in each. The name and session-receipt fixtures also pass against the freshly built production and sanitizer libraries. The receipt fixture uses actual continuation/receipt functions and a channel with a manually assembled network session and a real session string table; it does not prove a complete attached peer lifecycle. The retained NQ 15/666/999, QW 28, Q2/KEX and Q3/TA fixtures pass; the Q2 wire transcript remains byte-identical to all retained before fixtures.

Private copied-owner-profile launches of Q1 classic e1m1, Q2 rerelease base1 and Q3 q3dm1 each reach the retail world and quit normally after 1000 frames. Reviewed CPU captures show the world, weapon and HUD. All 42 original settings files remain unchanged and all recorded owned process tokens are gone. Dummy audio is contained; these are neither sound nor timing runs. No installation was made.

Each run records 878 successful measured playing frames after the diagnostic warm-up. The gate still fails its zero-heap criterion: Q1 records 27,440 allocation attempts, Q2 rerelease 8,458,676 and Q3 326,073. No capacity exhaustion, NULL result or size overflow was reported. This migration closes the selected name copies, not the remaining scene, module and frame allocations.

Remaining work: first admission of novel remote names can still grow the existing string table; unrelated content, model, label and event text fields remain unchanged. The requested authoritative reduce column was not found in the checked architecture and primitive records, so this change does not invent reductions or remove native protocol fields. Full legacy client/server interoperability and complete combined-mode gameplay are not established by the component fixtures.

Evidence packets: `/tmp/qa-common-snapshot-names-proof-20261009` (before/after sources, commands, exact output bytes and current-library receipts) and `/tmp/qa-common-snapshot-names-build-20261009` (frozen source attestation, full builds, core checks and private gameplay).
