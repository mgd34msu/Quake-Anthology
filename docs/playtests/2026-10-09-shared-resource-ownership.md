# THE-870 / THE-3172 shared resource ownership

Two existing shared ownership bugs surfaced during outgoing-event custody integration.

`remote_unified_events.c:200` copied the receiver's entire 82-byte ID array from a decoder-owned string. A normal short resource identity occupies nineteen bytes including NUL; ASan reported the 82-byte read past it. The existing control boundary already bounds the name to thirty-seven characters and parses its uint64 serial. The receiver now copies only the admitted string and NUL. No extra guard, schema or format is introduced. The offending copy exists at main 80163ff1.

`equipment_icon.c` obtains an owning image reference and registers it in a material, which retains its own reference. The loader omitted its original release on success and failure. One shared cleanup release now balances all image/WAD/shader branches and all callers. Before that release, private Q1 e1m1 shutdown leaked 1,490,457 bytes in 1,820 allocations; Q1 start with Q3 movement/character leaked 865,657 bytes in 1,270 allocations. The image references also retained their asset and name owners.

Actual decoder/consumer fixtures compiled with GCC/Clang, plain and ASan+UBSan, reproduce the old overread in both sanitizers and pass with the corrected copy in every variant. Short and maximum uint64 IDs, retained/final document destruction, re-declaration and post-document receiver lookup are exercised. All eight control transcripts match 444 bytes. Seven other audited fixed-capacity resource-ID copies use genuine same-capacity source arrays; this is a bounded identity-copy audit, not a whole-program memory-safety claim.

Root production, ASan/UBSan and allocation-gate builds each pass the seven core suites. The complete frozen staged input tree is fd5727ce020118e0faefd803864b9f00d0ebf0c7, based on 80163ff1, and includes the outgoing custody slice. These two corrections have no dependency on that slice's fields or API.

Private X11 CPU Unified loopback runs use fresh copies of the owner's forty-two settings files, masked desktop/audio/device access, native framebuffer delivery, no debugger, ASan leak detection and UBSan halt enabled. Four 200-frame executions quit normally without sanitizer reports. Later captures for the three slower-loading cases confirm gameplay, not just an early loading image:

| Session | Gameplay image |
|---|---|
| Q1 classic e1m1 | `/tmp/qa-private-wayland-hx24v0l0/user/world.png` |
| Q2 rerelease base1 | `/tmp/qa-private-wayland-n1oquf_1/user/world.png` |
| Q3 q3dm1 | `/tmp/qa-private-wayland-at3qyavl/user/world.png` |
| Q1 start, Q3 movement and character | `/tmp/qa-private-wayland-vsensvzo/user/world.png` |

The three later runs use 240 frames and also quit normally with no sanitizer report. Images were viewed. Every original profile byte stayed unchanged and every recorded owned process token was gone after cleanup. Dummy audio provides no sound proof. This verifies startup, received event/resource delivery, bounded gameplay and normal shutdown of native compiled games, not retail guest modules, control feel, campaign completeness or performance. No install was performed.

The first Wayland runs separately exposed a 368-byte SDL/Mesa shutdown leak: one direct 256-byte allocation and two indirect 56-byte allocations. A standalone SDL-only window-surface program reproduces it without engine code. Loader base/size records map all otherwise-unknown allocation frames to libgallium/EGL Mesa. Detection and normal teardown remain unchanged; no leak is suppressed or module pinned. Disabling framebuffer acceleration is unsupported on that Wayland path. The evidence is on THE-900, and the failed logs remain available. The clean X11 runs use the engine's existing native-framebuffer path; they are not a claim that this external Wayland leak is fixed.

Evidence packets:

- `/tmp/qa-the870-resource-identity-20261009/{report.md,checks.json,wire-proof.json}`
- `/tmp/qa-the870-resource-name-build-20261009/asan-x11-no-glx-live` (before image-release correction)
- `/tmp/qa-the870-shared-resource-build-20261009/{source/source-attestation.json,production-tests.log,asan-tests.log,gate-tests.log,asan-x11-live/results.json,asan-late-live/results.json}`
- `/tmp/qa-the870-lsan-source-20261009/{sdl-surface-repro.c,sdl-results.json,sdl-default.log}`
- `/tmp/qa-the870-outgoing-custody-build-20261009/leak-mapping/loader-attribution.json`
