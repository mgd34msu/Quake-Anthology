# THE-2876 entity store

Installed code: `037b0d77`, built October 9 at 02:05:58 CDT and installed at
02:20:52 CDT through `tools/install_qualified_build.py`.

The actor registry owns physical bodies and compact published area links.
Area lists use load-sized slot indices and stack traversal cursors. Native
target indexes change on committed names and slot admission/release. Prediction
retains its world and load-sized receive buffers.

Implementation commits: `b171ff77`, `c6884e77`, `c4e40941`, `037b0d77`.

Production and ASan builds passed. All seven existing core/content checks passed
in both builds. Focused GCC/Clang and sanitizer checks covered nested traversal,
unlink/relink, slot reuse, exclusions and checkpoint restoration. The post-load
five-actor/3,100-relink check changed from six heap calls to zero. This does not
establish zero allocation across a whole frame.

Component measurements used affinity `0-7,12-19`, no debugger, ABBA ordering,
120 warm-up batches and 600 sampled batches per run. Times below are microseconds
per batch, not frame times.

| GCC Q1, 512 actors | Before median / p99 | After median / p99 |
| --- | ---: | ---: |
| 64 area queries | 31.620 / 43.650 | 20.4005 / 26.290 |
| 2,048 relinks | 148.1405 / 188.680 | 130.315 / 136.770 |

All 24 component medians improved. Some tails increased: GCC Q2/512 relinks
p99 changed from 184.710 to 216.080 microseconds. No FPS improvement is claimed.

Private X11 launches reached visible retail `e1m1` and `q3dm1` gameplay and
quit normally through keyboard console input. Sixteen headless Wayland cases
covered CPU/GL menus, every native edition, bare owner display defaults and
empty content. Their final PNGs were reviewed; every exit was zero. All 42
copied owner-profile source files remained byte-identical, and no recorded
owned game/compositor processes remained. Audio was contained and dummy;
these runs do not prove sound.

Evidence packets: `qa-the2876-compact-build-20261009`,
`qa-the2876-retail-proof-20261009`, `qa-the2876-slot-links-20261009`,
`qa-the2876-incremental-targets-20261009`, and
`qa-the2876-entity-storage-20261009`. Linear THE-2876 records their local paths.

Retail server think/touch/clip parity and an actual 600-frame target-refresh
count remain unproved. Conventional client demo playback does not execute
those server callbacks. Focused order tests preserve published fields/order,
except the documented removal of a retired link's historical revisit after
nested unlink/relink. Live module field reads (THE-2861), spatial snapshot
allocation (THE-2874), and the client event ring (THE-870) remain separate work.
