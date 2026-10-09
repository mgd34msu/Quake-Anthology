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

## Retained parser and model storage

Q2 status-layout reference masks now update in the existing changed-configstring setter, together with the text. Player publication reads those masks while continuing to read live player stats and overlays. The same existing parser serves status changes, overlay changes and restore with one owner scratch arena, reserved and sealed at load. Its 196608-byte capacity follows the existing 65536-byte text admission bound. Maximum-input component usage was 131070 requested bytes, with zero overflow. Per-frame status parsing and fresh parser arenas are deleted; changed text still owns a copy.

Q3 weapon records now read their four gun/hands/barrel/flash handles directly from the existing model registry. Cold enumeration uses the same row projection. The hot copied model array, arena and nested row scan are deleted. Lexicographic alias names and independent forked registries retain their meaning. Alias-name selection still scans the existing registry, and unchanged namespace normalization still allocates.

GCC/Clang and sanitizer before/after components preserve exact results. Eight unchanged-layout Q2 player publications per edition drop from eight parses and allocation/free pairs to zero. Q3 direct model metadata reads drop from 4000 pairs to zero; 100 actual weapon-record fixture reads drop from 300 to 200 pairs. The residual work prevents a whole-path zero-allocation claim. Full production and ASan builds and both seven core suites pass for source `03cd5e7c`. Evidence: `/tmp/qa-the2874-q2-layout-references-20261009`, `/tmp/qa-the2874-q3-model-lookup-20261009` and `/tmp/qa-the2874-hud-model-build-20261009`. These are bounded components, not retail play or a frame-time gain. No install was made.

## Latest actual frame census

The recipe-resource fix's instrumented source `84bc7b01` completed seven private copied-profile cases with 878 measured frontend steps each. Owner files were unchanged and recorded owned processes cleaned. These counts are observed work, not an isolated ratio against the older composed candidate: real tick admission and source composition differ.

| Case | malloc requests | calloc | realloc |
| --- | ---: | ---: | ---: |
| Q1 classic e1m1 CPU | 10594 | 29172 | 896 |
| Q1 rerelease e1m1 CPU | 14106 | 29172 | 894 |
| Q2 classic base1 CPU | 37621 | 79791 | 2293 |
| Q2 rerelease base1 CPU | 8223971 | 298013 | 8149 |
| Q3 q3dm1 CPU | 268013 | 33032 | 36594 |
| Q2 rerelease base1 GL | 8116130 | 279048 | 7628 |
| Q3 q3dm1 GL | 240663 | 32022 | 31059 |

All runs exited normally; failed requests and observed size overflows were zero. The Q3 GL captured image says Awaiting snapshot, so that row proves collected frontend-step counters, not rendered gameplay. The other captures show retail worlds and player/HUD presentation. This software-GL/dummy-delivery census supports no GPU timing or sound claim. It does not contain the newer parser/model slice above and does not close the zero-allocation target. Raw records and visual review: `/tmp/qa-the2874-live-gate-after-20261009/results.json` and `visual-review.json`.

## Reserved-capacity exhaustion

The existing diagnostic now observes exhaustion in the shared arena and fixed pool, even when no heap request occurs. Both owners retain their saturating local counters; the gate adds `capacity_exhaustions` to frame totals and peaks. Its measured-window summary reports failure for a heap request or capacity exhaustion. Reporting never changes gameplay success or adds an application-ending check. Normal GCC/Clang objects have no gate reference.

Actual shared-primitive checks exercise three independent owners concurrently: six exhaustion events, then two more with saturated local counters. All eight appear in the gate with zero heap attempts; held payloads, slot reuse and successful application results remain intact. Existing counter checks and the new behavior checks pass under GCC, Clang and both ASan/UBSan builds. The fixture inspects numeric state, not message wording. Evidence: `/tmp/qa-the2874-reserved-exhaustion-20261009/receipt.json`.

Full production, ASan/UBSan and diagnostic builds and all seven core checks in each pass for frozen source `4db03733`. A private copied-owner-profile Q1 classic e1m1 CPU640 run exits normally after 1000 frames, with 878 successful measured steps and zero capacity exhaustion. Its 7953 malloc, 18593 calloc and 894 realloc requests correctly produce a failing gate; zero allocation remains open. The captured world image was reviewed, source settings were unchanged, and all four owned process tokens were gone after cleanup. This diagnostic/dummy-audio run is not timing or sound proof. Build and runtime receipts: `/tmp/qa-the2874-reserved-exhaustion-build-20261009` and `/tmp/qa-the2874-reserved-exhaustion-20261009/results.json`. No install.
