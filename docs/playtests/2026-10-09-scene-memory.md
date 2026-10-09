# THE-2874 scene memory sizing

A temporary private overlay on committed `904dced5` measured actual successful arena allocations, including alignment, and scene counts at finish/reset boundaries. It added no heap or file operations to the observer. The overlay is not production code and is not installed. The SDK sources were restored afterward; the private artifact and exact patch remain in `/tmp/qa-the2874-scene-highwater-20261009`.

Each case used a fresh copy of the same owner's 42 settings files, a private headless Wayland display, dummy audio, CPU output at 640×400, affinity `0-7,12-19`, and a normal 1000-frame exit. All source profiles remained unchanged and all recorded owned process tokens were gone after cleanup. Reviewed screenshots show the retail worlds, weapons and HUDs. No observer records were lost. These instrumented runs measure memory, not frame time or sound.

| World | Arena peak, bytes | Retained array peak, bytes | Commands | Pending groups | Image pins | Geometry pins | Model/pose pins |
|---|---:|---:|---:|---:|---:|---:|---:|
| Q1 classic e1m1 | 1,347,576 | 701,440 | 589 | 299 | 67 | 299 | 0 |
| Q2 rerelease base1 | 13,944,276 | 702,976 | 812 | 510 | 109 | 503 | 9 |
| Q3 q3dm1 | 692,280 | 1,415,560 | 832 | 571 | 117 | 564 | 0 |

Arena peaks are exact successful consumed bytes since initialization. Other counts are boundary-observed peaks; they can miss an intermediate append followed by rollback. Array bytes include all seven retained command, sort and pin arrays. The renderer's repeatedly initialized swap frame had zero observed payload/array use in these cases, so its initialization count alone does not identify an allocation hotspot.

These stationary views establish lower bounds for admission, not complete capacities for movement, every effect, split-screen, portals, combined modules or arbitrary guest draws. Reservation must also use the loaded world's final meshes/material stages and admitted model, view and dynamic-source limits. A warm reused arena is still growable; this measurement does not close the zero-allocation issue.

GCC/Clang and ASan/UBSan checks compare the arena metric with actual block sums through alignment, reuse, growth, reset, sealed failure and destruction. The full private production build passed. Q3 stdout interleaved with the diagnostic summary; the initial parser marked that case failed despite exit zero. `results-corrected.json` extracts the complete summary from the same raw log, verifies all 14 owner rows and zero dropped observations, and retains the original receipt. No case was rerun.

Reproduction and evidence: `overlay.patch`, `private-source-attestation.json`, `arena-proof.json`, `build.log`, `run-live.py`, raw logs, `results-corrected.json`, screenshots at the receipt paths, and `retained-artifact.json` in the packet above. The private synchronization can perturb timings, so none are claimed here.
