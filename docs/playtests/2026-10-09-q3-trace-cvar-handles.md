# THE-2859 original Q3 trace handles

The original Q3 host resolves `cm_noCurves` and `cm_playerCurveClip` once at
creation. Trace and contact share one policy that reads those handles from
the common cvar table. Both per-query named reads are deleted. Stock rows
exist before host creation, and the existing host retains its cvar table for
its lifetime. Null-table defaults and original trace masks are unchanged.

Actual policy and host-binding statements run against the real canonical
cvar store. GCC/Clang plain and ASan/UBSan variants pass 2,606 policy
comparisons and ten lifecycle checks: five Source dialect views, values,
contents masks, writes, edit abort/publish, recreation and absent defaults.
Each old query makes two named reads; each new query makes zero find/resolve
calls and two handle reads. Actual host and spatial translation units also
pass both compilers' strict syntax checks. This component does not execute
an entire original GAME trace syscall or host constructor.

Evidence is in `/tmp/qa-q3-trace-cvar-handles-20261009`. Production,
ASan/UBSan and allocation-gate builds plus seven core checks each pass for
source tree `d7ec8010f9632a269551beff3d8f51cfdf3f9c72`. That tree includes
the separately verified Q1 source-index slice and these three host paths;
build logs are in `/tmp/qa-the2859-q3-trace-build-20261009`.

This is a caller/call-count proof, not a frame-time speedup or complete
THE-2859 adoption. Remaining static-name callers are recorded in
`docs/core-adoption.md`. No partial-slice installation was made.
