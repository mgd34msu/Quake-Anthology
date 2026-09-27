# Shared bot resources and chat

The native resource owner now loads and retains characters, fuzzy weight trees,
weapon/projectile and item definitions, variables and chat assets. References keep
resources alive after their library closes. Fuzzy evaluation uses an explicit
retained stack; clones share immutable topology and own their mutable values.
Chat systems share asset cooldowns and a reusable console pool. Per-bot state and
resource continuation have separate typed capture/restore interfaces.

Root read the frozen library/chat implementation and public contracts, excluding
the detailed BSP implementation delegated to the Q3 host reviewer. Focused donor
comparisons refuted suspected defects in integer fuzzy interpolation, interbreed
child selection, unused genetic draws and moving-pointer whitespace behavior:
those behaviors appear explicitly in the corresponding TypeScript functions.
The review also checked resource references, partial parse cleanup, callback
retirement, topology validation and restore ownership. This bounded review does
not establish complete donor behavior coverage or runtime correctness.

The Q3 host reviewer read the complete BSP query/parser prerequisite and its
donor. That review found incorrect severity and collapsed diagnostic messages.
The author corrected those branches, and the reviewer reread them without a
remaining confirmed defect. Root applied repository formatting and registered all
19 implementation units in `qa_bots`, with shared script/content dependencies.

Native chat captures use wider internal offsets so long messages can retain
variables. Original Q3 `match_t` remains a signed-byte ABI: its host codec must
truncate writes and sign-extend reads. That concrete codec was not yet written at
this handoff and remains part of B23. It is not established by these native APIs.

B26 remains incomplete: native roster/decisions/orders, goal and movement
controllers, complete runtime handles and application/save integration are still
being written. Source logging adapters also remain open. These resources do not
close those requirements. No engine build, parser/test execution or runtime
qualification was performed.
