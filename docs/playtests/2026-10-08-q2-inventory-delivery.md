# THE-909 / THE-593 Q2 inventory delivery checks

Four ordinary `base1` cases passed on installed `b5d8813b965941e37c6466a2dc22143b8afa6b61`,
built at 12:04:30.710913 CDT and installed at 12:35:34.421445 CDT on 2026-10-08.
The exact installation receipt is
`/home/buzzkill/.cache/quake-anthology-recovery/installed-m0-selected-view-weapon-20261008.json`.
Each case used a fresh copy of all 34 owner settings files, private Xvfb/Openbox
and contained Pulse output. The helper read the installed files directly, with
no SDK overlay or debugger. The capture worker inspected all four cases; the
coordinator accepted the group after opening the classic and rerelease GL
inventory and gameplay PNGs.

Source fix `a98f69df` sends the selected Q2 HUD metadata and actual player stats
through the normal CLIENT, reusing the existing Q2 layout interpreter, picture
cache and font loader. Fix `5cb46089` retains the independent packet event
journal until every admitted peer has queued its records reliably. Requested
inventory counts therefore survive host frames without a new Source tick;
ordinary UI/audio events still clear without replay. This is shared delivery
behavior, with no renderer-specific inventory workaround.

| Edition and renderer | Inspected inventory rows | Public quit | Evidence bundle under `/tmp` |
| --- | --- | --- | --- |
| Classic CPU | `1 Blaster` | 0 | `qa-private-av-mfkm2y3k` |
| Rerelease CPU | Compass, Blaster | 0 | `qa-private-av-6k2inquv` |
| Classic software GL | `1 Blaster` | 0 | `qa-private-av-_nxo0nfv` |
| Rerelease software GL | Compass, Blaster | 0 | `qa-private-av-rm7u3t2p` |

The original health `100` display and health/Blaster icons remain visible,
without generic vital tiles. Actual W, Space, crouch and Mouse1 down/up input,
releases and mouse look are recorded. Classic uses its saved C and TAB/inven
bindings; rerelease keeps CTRL/+movedown and TAB/+scores, requesting inventory
with public `inven`. Public `frameinfo` captures show advancing received state.
Every game and controller exited normally with code zero. Full exit-flushed
application and X error scans are empty, and each private monitor capture
contains nonzero PCM.

All five installed stat pins, helper pins and original owner settings bytes
stayed unchanged. Independent final checks found all 152 recorded owned
PID/start tokens absent. The installed-artifact HOLD was released after that
audit. No SDK, build or installation change was made by this proof lane.

Rerelease inventory quantities are not separately legible in the small stock
glyphs. Its GL file named `THE868-final-world.png` still shows the console;
gameplay review uses `THE593-inventory-released-world.png` and
`THE868-fire-released-world.png`. GL is private software rendering only.
Captured PCM establishes output, not sound-cue identity. These ordinary checks
make no jump-velocity, latency or performance claim.

Raw indexes are
`/tmp/qa-the909-the593-installed-b5-prep-20261008/evidence-index.json` and
`/tmp/qa-the909-the593-installed-b5-prep-20261008/evidence-index.md`.
Each evidence bundle contains `ordinary-q2-qualification.json`, original PNGs
and input/log records under `user/evidence`, profile-copy and ownership
receipts, and the private `q3-monitor.raw` capture.
