# THE-395 / MIKE-31 item pickup checks

Q1 classic and rerelease CPU runs proved rotation and removal of the stock
`e4m6` quad on installed `fac87ea0`, built at 20:07:38 CDT and installed at
20:19:09 CDT on 2026-10-07. Each run used a fresh copy of all 34 owner settings
files and private display/audio servers. The coordinator inspected all six
original window PNGs. Each pair shows different quad orientations from the same
camera; each after-contact image shows the quad tint and item absence. Stock
lighting in this corridor is dark.

The authored quad at `[-192, -1504, 200]` was actor `[1, 99, 0]`, with original
MDL flags `8` and model `447`. In both runs, completed frames 133 and 139 show
yaw 349.997 and 49.999 degrees at Source times 14.3 and 14.9 seconds. CPU rendering
and presentation had finished, and each captured scene sequence matched its
frame number. The classic camera was `[-181.943, -1657.969, 222.063]`; the
rerelease camera was `[-177.945, -1657.969, 222.063]`.

| CPU edition | Actual accepted contact | Quad expiration | Completed after-contact frame | Item DRAW commands before / after |
| --- | --- | --- | --- | --- |
| Classic | Frame 183, Source 19.3 s | Source 49.3 s | 186, Source 19.6 s | 1 / 0 |
| Rerelease | Frame 184, Source 19.4 s | Source 49.4 s | 187, Source 19.7 s | 2 / 0 |

Both physical W contacts selected `QA_PICKUP_SELECT_ORIGINAL`, returned
`QA_PICKUP_ACCEPTED`, and ran the original completion behavior. The same live
Source pickup became model `0`, `hidden=true`, and `QA_PHYSICS_NOT_SOLID`.
Its completed world DRAW commands disappeared. The recorded inventory counts
stayed unchanged; the quad grant produced the active power timer shown above.

Positioning was assisted through public console changes to private
`cl_sidespeed` and `cl_forwardspeed`, both set to `20`, followed by real A/W
input. Before final contact, public queries confirmed restoration to the copied
profile's original values, `350` and `200`. Collision stayed on throughout
these quad runs. This proves the actual stock-item contact after assisted
positioning; it does not claim unchanged owner input settings during transit.

Public quit returned zero in both runs. The original profile, all three installed
files, installation receipt, retail inputs, original helpers and retained failed
attempts stayed unchanged. Each run passed its 69 file stat pins. Independent
PID/start-token checks found no owned processes remaining. These debugger
observations make no timing or sound claim.

Classic and rerelease software GL also passed on installed `37828b66`, built at
21:54:47 CDT and installed at 21:58:48 CDT on 2026-10-07. These runs used private
Xvfb displays, contained Pulse output, and fresh copies of the 34 owner settings
files. The capture worker inspected all six whole window PNGs. The spin pairs
show different quad orientations at the same camera, and the after-contact
images show the quad tint and item absence. Owner gamma stayed unchanged.

| GL edition | Completed spin frames | Source clocks and yaw | Actual accepted contact | Quad expiration | Item DRAW commands before / after |
| --- | --- | --- | --- | --- | --- |
| Classic | 131, 137 | 14.1 s / 329.996 degrees; 14.7 s / 29.998 degrees | Frame 180, Source 19.0 s | Source 49.0 s | 1 / 0 |
| Rerelease | 126, 133 | 13.6 s / 279.998 degrees; 14.3 s / 349.997 degrees | Frame 177, Source 18.7 s | Source 48.7 s | 2 / 0 |

Every GL capture matched its completed scene sequence and presented renderer
sequence. The original quad MDL retained flag `8`; rerelease used its genuine
MD5 replacement. Both final physical W contacts ran the original selection and
completion behavior, returned `QA_PICKUP_ACCEPTED`, and left the same pickup
hidden, model `0`, and not solid. Collision stayed on. The copied profile's
queried speeds were restored before contact, as in the CPU routes above.

Both GL runs quit normally with exit zero. Their 80 file stat pins, installed
files, installation receipt, and original owner profile stayed unchanged.
Independent checks found all 33 recorded PID/start tokens absent in each run.
These debugger observations make no timing or sound claim.

The classic CPU armor attempt failed during its first public noclip U raise.
It missed the target height and reached `[480, -352, 1996.03125]` before the
25-second host bound stopped it. Noclip remained on, health was 100, armor was
zero, and no pickup touch or armor capture occurred. Cleanup released the keys
and quit normally; all 78 pins and the original profile passed, with no owned
tokens remaining. This is a retained route failure, not armor pickup proof or
an attributed engine defect.

Two later classic CPU armor attempts on `37828b66` also remain unqualified.
Both accepted the actual armor during assisted noclip transit, before the
intended ordinary collision approach, and produced no armor rotation PNGs.
The second began with armor zero. Actor `[1, 10, 3]` selected the original pickup
and returned `QA_PICKUP_ACCEPTED` at Source 32.7 seconds. Its model changed
from `298` to `0`, and it became hidden and not solid. The receipt proves
accepted contact during noclip. Ordinary collision contact remains unqualified.

The second route reached player Z156.03125. Source defines this armor's local
box as `[-16, -16, 0]` to `[16, 16, 56]`, at retained origin `[688, 480, 80]`.
The standard player hull and link padding would therefore overlap the trigger
at that height. Neither live hull was retained before contact, so that geometric
explanation remains source derived. The next route needs those actual bounds
before transit. Both failed runs released input and quit normally with exit
zero. The original profile and 89 or 98 file pins stayed unchanged, and all
31 recorded owned tokens were absent in each run.

THE-395 remains In Progress. Still missing are Q1 armor rotation/removal in both
editions and renderers, natural weapon/ammo contacts in
classic GL and rerelease CPU, and Q2 quad/powerup spin with two inspected
rotation images per edition/backend. Existing numerical Q2 ammo/weapon matrices
do not supply the quad visual evidence. Mike's owner retest remains outstanding.

Retained local evidence bundles:

- `qa-private-av-sn22y1pa/user/q1/bounded-quad-proof.json`, classic CPU.
- `qa-private-av-4ex9w9wu/user/q1/bounded-quad-proof.json`, rerelease CPU.
- `qa-private-av-d0zlvfak/user/q1/bounded-quad-proof.json`, classic software GL.
- `qa-private-av-wpppw7yy/user/q1/bounded-quad-proof.json`, rerelease software GL.
- Each quad bundle contains `q1-pickup-quad-spin-a.png`,
  `q1-pickup-quad-spin-b.png`, `q1-pickup-quad-after.png`, matching capture
  receipts, input/observer records, controller results and cleanup records.
- `qa-private-av-60s3cq75/user/q1/bounded-armor-route-failure.json`, failed armor
  transit and its actual final state.
- `qa-private-av-6cncm1z9/user/q1/bounded-armor-route-failure.json` and
  `qa-private-av-6a_j935y/user/q1/bounded-armor-route-failure.json`, later
  unqualified armor transit contacts and cleanup.
- `qa-the395-fixed378-captures-20261007/quad-gl-summary.json` and
  `armor-route-diagnosis.json`, compact GL proof and bounded source diagnosis.
- `qa-the395-quad-cpu-v4-20261007`, preserved quad helper copy;
  `installed-m0-shared-cvar-notification-20261007.json`, fixed installation
  receipt.
- `qa-the395-quad-softwaregl-37828b-20261007`, preserved GL helper copy;
  `installed-m0-cinematic-palette-20261007.json`, fixed GL installation receipt.

Game assets and captured images remain in private evidence storage.
