# Rogue HUD and debug cvar handles: THE-2859

Rogue team-face output reads its admitted provider teamplay handle. Tools bind
debug width at ordinary, diagnostic and restored construction and refresh that
handle when rebound. Both read the existing common cvar table; no value cache,
save field, fallback lookup or validation context is added.

Actual-source comparisons using the common cvar store cover 221 HUD decisions,
726 debug decisions and nine lifecycle cases per GCC/Clang plain and sanitizer
run. The changed full translation units pass strict syntax checks. Controlled
observation and drawing adapters bound this proof; it does not demonstrate
rendered gameplay or a frame-time speedup.

Production, ASan/UBSan and allocation-gate builds and seven core checks each
pass for the isolated four-source-path candidate. Evidence is retained at
`/tmp/qa-fixed-cvar-readers-20261009/` and
`/tmp/qa-the2859-fixed-readers-build-20261009/`; frozen source tree is
`72f338a40744ba644734b72c903c68dd23ad5f08`.

THE-2859 remains open: other fixed-name hot callers and native cvar-shadow
refresh still need migration. This is not a whole-frame allocation result.
