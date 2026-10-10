# Common Q1/QC handles and native cvar imports

Implementation commit: `c5aabbc6` (THE-2859 / THE-2874). Compiled Q1 binds
25 handles before spawn; original QC binds world policy and developer output
in its existing owner. Every fixed reader covered by this slice uses the
common table; the named application callback/context is deleted. Original
Q1/QW movement, limits, pause, chat and addon rules are preserved.

Native cvars remain derived ABI objects. Unchanged publication skips writes;
classic modified clears and rerelease temporary integer overrides survive.
One scanner serves owning and borrowed native strings. Repeated existing
Cvar_Get inputs borrow contiguous backing. Setters retain values before
callbacks; new declarations retain both inputs; split backing keeps the
owning fallback. Existing flags OR follows qsrc
`quake-2/qcommon/cvar.c:140-144`; metadata-only additions use the common
updater without widening scalar/lifecycle mutation.

## Verification

The committed tree is exactly the built tree
`2f6589df7d8b3f5a5f0b1fa1936404bbb03896ae`. Production, ASan/UBSan and
allocation-gate builds pass, with seven core checks passing in each. The
first production build required making the existing int32-to-float Q1
teamplay fallback conversion explicit. Build logs/receipts:
`/tmp/qa-the2859-policy-native-build-20261009/`.

Actual-source Q1/QC GCC/Clang plain/sanitizer pairs preserve policy, QW
field ordering, admission, chat, pause, prepared edits and source recreation;
70 complete TU checks pass. Fixed policy reads have zero named lookup calls.
Controlled source/VM boundary adapters and scope are documented in
`/tmp/qa-q1-fixed-policy-handles-extended-20261009/report.md`.

Native publication pairs preserve 76 ordinary snapshots and 125,112 bytes
across four ABIs. Per 600 unchanged polls, guest writes fall from 2,400 to
zero; heap calls were already zero in this component. Supported guest-write
corrections are separate qsrc assertions. Evidence:
`/tmp/qa-the2859-native-cvar-shadow-20261009/`.

All 24 import/flags/reentry compiler-mode executions pass. Ordinary imports
preserve 28 snapshots, 475,624 bytes, addresses and callback order. Per ABI,
600 existing imports remove 1,200 input reallocations and frees, reaching
zero heap calls. Nested flags, aliases, prepared validation/publish, setter
callback custody and readable split mappings pass. Actual common cvars and
import bodies are compiled from source; the external native memory owner is
a component fixture. Evidence:
`/tmp/qa-the2859-native-cvar-import-20261009/`.

## Remaining work

THE-2859 remains open for other fixed Q3/loading/authored HUD callers. New
declarations, setters, split strings and other native text imports retain
allocation paths. No whole-frame, frame-time, retail guest-module or shipped
gameplay completion is claimed. This intermediate slice is not installed.
