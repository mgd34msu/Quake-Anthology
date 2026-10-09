# THE-2874 native Q3 SystemInfo revision

Native Q3 reads SystemInfo only when the existing reliable configstring
revision changes. Its unchanged-frame path no longer makes two text copies,
parses keys or rewrites cvars. The revision is committed only after successful
application. Existing video reset and retained-round admission invalidate the
observation; service recreation and restore start invalid. No save field,
content fingerprint or new table was added.

Actual-source before/after component executions cover initial revision zero,
unchanged frames, a changed revision with unchanged text, changed values,
manual timescale retention, setter/allocation failures and retries, unreadable
configstrings, reset/admission and the actual 87-byte save codec. GCC/Clang,
each plain and ASan/UBSan, pass both implementations. Across 600 unchanged
frames, 1,200 allocation/free pairs and 1,200 cvar writes become zero.

Evidence is in `/tmp/qa-the2874-native-systeminfo-20261009`. Production,
ASan/UBSan and allocation-gate builds and seven core checks per configuration
pass for isolated source tree `bc04212f2ab5e5ce5ed558f19a0dfc30b68e6c65`,
based on `b3ccc5e5`; build logs are in
`/tmp/qa-the2874-systeminfo-build-20261009`.

Changed revisions still allocate two text copies. This is a measured caller
allocation reduction, not whole-frame zero allocation or a frame-time claim.
No partial-slice installation was made.
