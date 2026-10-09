# Retained VFS allocation

THE-2874 removes repeated path normalization from `qa_vfs_acquisition_retained`. Normal acquisition and checkpoint admission already normalize the receipt's four paths. Retained validation compares them exactly with that admitted history entry; its opening-prefix check admits only complete initial components of the canonical path. External acquisition and checkpoint validation remain unchanged. No replacement cache, hash, parser or failure path is added.

The actual component fixtures preserve all 119 acceptance outcomes, resource IDs and byte sizes across GCC, Clang and ASan/UBSan before/after runs. They cover links, prefixes, changed lookup policies, clone/checkpoint restore, retired mounts, replaced native files and final reader release. Thirty-four malformed internal receipts now use the existing metadata FORMAT error instead of the redundant parser's diagnostic; their rejection is unchanged.

Each receipt ran 1,024 warm retained checks. Ordinary, archive and overlay receipts previously made 4,096 malloc/free calls each; prefixed, restored and retired receipts made 5,120 each; linked receipts made 6,144 malloc/free calls plus 2,048 reallocs. All now make zero heap calls within retained validation. Origin/history comparisons still run.

The separate allocation-site diagnostic on Q2 rerelease base1 counted 2,666,426 allocations over 278 measured frames. Direct return-address counters attributed 1,271,103 calls to `normalize_path` and 1,271,046 to `copy_string`, together 95.34% of observed calls. This identifies allocation sites, not full parent stacks or CPU-time attribution. The temporary observer was removed and the normal build restored.

The normal whole-frame gate subsequently measured 878 frames after 120 warm frames on the same base1 CPU640 workload and copied owner profile:

| Source | malloc | calloc | realloc | Total |
| --- | ---: | ---: | ---: | ---: |
| Before, b37b23a8 | 8,185,493 | 280,600 | 7,989 | 8,474,082 |
| After, one VFS change | 123,874 | 278,276 | 7,903 | 410,053 |

These are real-time runs, not identical simulation/event transcripts. The allocation gate still fails its zero-allocation condition; remaining allocations are explicitly open. Neither run exhausted capacity, overflowed a size, returned a null allocation or lost an observation record. The gate covers wrapped engine/static-object allocation APIs during the frontend frame, including workers, rather than DSO/libc-internal allocations or outer pacing/save/shutdown.

Production timings use before/after/after/before, affinity `0-7,12-19`, base1 CPU640, caps and swap interval zero, a fixed 640×400 private drawable, 1,798 warm presents and 600 consecutive measured presents. No debugger, perf sampling, allocation instrumentation or other owned heavy workload runs during timing. Both artifacts use the same fixed-memory present observer and helper directory.

| Run | Median ms | p99 ms |
| --- | ---: | ---: |
| Before 1 | 12.729 | 48.918 |
| After 1 | 12.297 | 50.036 |
| After 2 | 11.840 | 51.358 |
| Before 2 | 13.904 | 26.707 |

Medians are lower in this pair, but the tails vary substantially. This does not establish a stable frame-time or p99 improvement. The proven benefit is removing retained-check allocator traffic. Present intervals include private compositor scheduling; ticks, RNG and pixels are not claimed identical.

All private launches quit normally, preserve the source owner profile and leave no recorded owned process tokens running. Root inspected before/after world captures. Dummy audio is containment, not sound proof. No owner installation or Slack retest is made.

The isolated staged source `01b9a974d48bb92dd1d2400cbcf26e7b294b80fe`, based on b37b23a8, passed full production, ASan and allocation-gate builds and all seven configured core checks in each. Evidence:

- `/tmp/qa-the2874-vfs-retained-20261009`: actual fixtures, commands, acceptance and heap comparison.
- `/tmp/qa-the2874-allocation-sites-20261009`: temporary observer source, raw sites, symbol decoding and private launch receipt.
- `/tmp/qa-the2874-vfs-retained-build-20261009`: frozen source, build/check logs, production ABBA and normal live gate.
- `/tmp/qa-the2874-visual-scratch-build-20261009/live`: preceding normal gate baseline.
