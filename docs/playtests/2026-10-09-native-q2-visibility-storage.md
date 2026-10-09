# Native Q2 visibility storage

THE-2874 / THE-873 move the native Q2 rerelease visibility decision workspace to map admission and restore. The same owner and inline rows survive frame invalidation; completion resets and fills them before publishing validity. Retirement frees the owner. The separate per-frame owner allocation and growing row allocation are removed.

Reservation uses the application's existing 65,536-slot admitted source extent, shared with its existing wire namespace checks. It does not narrow the native SDK table limit or require a mod's initial table capacity to remain constant. Actual current capacity remains completed-frame and save metadata. Cold reservation does not read the source table; completion and populated restore already supply that metadata.

The cold allocation requests 4,200,512 bytes per rerelease owner. This is requested storage, not committed resident-page usage. Classic visibility completion remains a no-op. Saved bytes, actor IDs, viewer masks, callback order and native protocol field widths are unchanged.

Root reran the actual old/new visibility component against freshly built supporting production and sanitizer archives. All eight GCC/Clang plain and ASan/UBSan executions pass with byte-identical semantic output and saved bytes. Cases cover first/warm frames, all four viewer-mask words, sparse entities, disconnect/release, nested invalidation, failed export and recovery, populated and empty restore, and accepted table growth from 512 to 768 to 65,536 with slot 65,535. Cold prepare also succeeds while the fixture's native table service is unavailable; no premature table proof is needed.

Old 65-row completion makes five heap calls; 511-row completion makes seven. The new completion makes zero malloc/calloc/realloc/free calls in every first, warm, restored, failed/recovered and growth case. This measures the actual visibility code with controlled source exports. It does not measure arbitrary module allocations or establish a renderer speedup.

Frozen source `b2f28d3e9567e055279d0e124df361f49bff8fd0`, based on 748398bd, passes full production, ASan and allocation-gate builds and the seven configured core checks in each. A private default Q2 rerelease base1 CPU run with a copied owner profile reaches gameplay and quits normally after 400 frames. Its whole-frame gate remains nonzero (122,574 allocations over 278 measured frames), with no null allocation, size overflow, capacity exhaustion or lost record. The source owner profile is unchanged and all recorded owned process tokens are gone. Root inspected the world capture; dummy audio is containment, not sound proof.

The separate retail `--original` attempt exits before gameplay because the selected runtime lacks its source hardware-monitor launcher/client package. It is retained as a failed launch and is not counted as native module gameplay proof. Native export behavior here is proved by the actual component fixture; full retail-original qualification remains open. No owner executable is installed and no Slack retest is posted.

Evidence: `/tmp/qa-the2874-native-q2-visibility-20261009` holds the strict checks and initial private comparison. `/tmp/qa-the2874-native-q2-visibility-build-20261009` holds the final source, full build/check logs, final component commands/results, default private launch and unsuccessful original launch.
