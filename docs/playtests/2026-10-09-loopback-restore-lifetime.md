# THE-3166: restored loopback ownership

The ordinary frontend transport exchange moved the server endpoint while leaving its loopback hub with the displaced frontend. Destroying that frontend closed the hub still used by the restored session. Exchange the hub and server address together with their transport; the retained KEX transport branch is unchanged.

Production and ASan/UBSan builds passed, with all seven existing core checks passing in each. The real transport fixture reproduces the closed-endpoint failure before the fix, then passes bidirectional exact-byte exchanges after displaced-owner cleanup for loopback-only and UDP-plus-loopback hosts under GCC/Clang, plain and sanitized builds.

The exact candidate privately loaded two existing owner autosaves: Q2 rerelease base1 after starting base2, and Q3 q3dm1 after starting q3dm0. Both printed Loaded, remained in gameplay for 20 seconds, and quit normally with exit 0. The final captured images were reviewed and show the saved maps, players and HUD. Each run copied the owner profile without changing the original and retired every recorded owned process. Audio delivery was dummy; these runs prove no audio behavior or performance gain.

The Q3 log also records a skipped typed-entity frame and a unified reliable retry warning. This proves restored-map survival and normal shutdown, not every aspect of continued network publication or save fidelity. THE-3166 remains bounded to the recorded cases until broader verification.

Evidence: `/tmp/qa-the3166-loopback-lifetime-20261009/receipt.json`; `/tmp/qa-the3166-loopback-restores-20261009.json`; `/tmp/qa-the3167-verified-build-20261009/{production,asan}-checks.log`.

Reviewed actual images: `/tmp/qa-private-wayland-pmd1h6au/user/q2-rerelease-baseq2-cpu-32.png` and `/tmp/qa-private-wayland-b2h3124v/user/q3-baseq3-cpu-32.png`.
