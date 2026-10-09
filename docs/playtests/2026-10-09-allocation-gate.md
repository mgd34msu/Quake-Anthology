# THE-2874 allocation measurement

`QA_ALLOCATION_GATE` is an opt-in ELF diagnostic build. It observes
engine/static-object malloc, calloc, realloc and free calls across threads
inside the live frontend step, including feature-local frame recovery. Normal
builds contain no gate code or calls. The diagnostic skips the first 120
playing frames, retains at most 4096 per-frame records, and reports totals and
peaks once at loop exit. It adds no game-ending check.

The counters are attempted allocation requests, not retained memory. Loaded
DSO/libc internals, other allocation APIs, outer save drain, pacing and shutdown
are excluded. Overflow and failed requests are counted. GCC/Clang and sanitizer
checks cover worker totals, warm-up, bounded records, saturation and failed
allocation. Evidence: `/tmp/qa-the2874-allocation-gate-20261009`.

A composed core candidate, source tree `9eceef6982ff180425c65c81fa26c7ddab077ce4`,
passed full production, ASan/UBSan and diagnostic builds plus the existing seven
core checks. `/tmp/qa-core-pools-jobs-gate-build-20261009` retains logs and source
attestations. This candidate also contains not-yet-committed pool migrations;
the following counts are evidence from that exact candidate, not an isolated
allocation-counter commit or a zero-allocation result.

Private headless Wayland runs used fresh copies of the owner's 42 saved files,
retail maps, working dummy audio delivery, and a normal 1000-frame exit. Each
successful case measured 878 frames after warm-up; captured world images,
per-frame counters, unchanged-profile checks and owned-PID cleanup receipts
are in `/tmp/qa-the2874-live-gate-20261009`.

| Case | malloc requests | calloc | realloc |
| --- | ---: | ---: | ---: |
| Q1 classic e1m1 CPU | 84,469,279 | 18,637 | 898 |
| Q1 rerelease e1m1 CPU | 88,613,451 | 18,660 | 902 |
| Q2 classic base1 CPU | 1,252,256 | 75,903 | 2,427 |
| Q2 rerelease base1 CPU | 450,103,134 | 397,807 | 11,156 |
| Q2 rerelease base1 GL | 450,103,136 | 397,812 | 11,156 |

Q3 CPU/GL and the attempted mixed case exceeded the 100-second diagnostic
budget and were stopped using owned PIDs. They are failed measurements. The
mixed-case capture showed Q3 content, so it does not prove the intended Q1-world
combination. Dummy output and software GL support no sound or GPU-speed claim.

A diagnostic Q1 profile found `qa_executable_recipe_current` at 36.55% inclusive
sampled cycles, chiefly retained-acquisition normalization and origin/history
lookup. `/tmp/qa-the2874-live-gate-profile-20261009` retains raw samples and
reports. Counter overhead affects those samples; they locate work and are not
frame-speedup evidence. Remaining arena/pool sites are inventoried at exact
source coordinates in `/tmp/qa-the2874-arena-audit-20261009/callers.csv` and
`report.md`; scene frame buffers, syscall scratch and typed frame leases remain
migration work.
