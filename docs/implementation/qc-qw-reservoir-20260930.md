# Classic QC QuakeWorld physical clients

The classic QC QW constructor now reserves 32 physical source client rows even
when the launch has zero or one local seat. The loaded program must have the
QuakeWorld system ABI and match the selected source profile before that extent
is chosen. A clock selection alone cannot give a NetQuake program QW rows.

The existing `qa_qc_game_create` producer passes this actual extent into
`first_dynamic_slot`: the initial VM contains world row zero and reserved rows
one through 32, with authored allocations starting at row 33. The subsequent
[reserved lifecycle unit](qc-qw-lifecycle-20260930.md) supplies actual source
actors for disconnected rows and their nonfree metadata. Client callbacks
still require genuine admission. The engine client owner and its checkpoint
identity use the same physical extent.

Declared QC client profiles keep their qualified `clients.maximum`. Classic
NetQuake keeps its configured launch seat capacity, including the existing
one-row minimum. A selected classic QW roster above 32 is rejected before
client storage is allocated.

Source references read: the TypeScript `quakec-source.ts` constructor separates
`options.maxClients` from its fixed QW `reservedClientSlots`; the native QW
factory in `network-qw.ts` uses rows one through 32 for player baselines. The
new C network source observer independently requires an actual completed
classic QW source with 32 rows and reads its real source pool. This constructor
does not produce wire baselines or complete the separate remote admission
policy.

The capacity-only checkpoint initially retained free disconnected rows, which
made `nextent(world)` skip them. The subsequent lifecycle producer now binds
actual OWNED source actors and sets their real free prefix to zero. This
closes that source-level difference; it does not certify the complete native
QW connection or gameplay pipeline.

Validation is limited to source review, exact file hashes and whitespace.
No compiler, test, game or executable validation was performed under the
current source-only gate. Whole native QW wire and gameplay parity remain
unverified.
