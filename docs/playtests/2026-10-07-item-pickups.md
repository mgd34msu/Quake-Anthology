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

The classic CPU armor attempt failed during its first public noclip U raise.
It missed the target height and reached `[480, -352, 1996.03125]` before the
25-second host bound stopped it. Noclip remained on, health was 100, armor was
zero, and no pickup touch or armor capture occurred. Cleanup released the keys
and quit normally; all 78 pins and the original profile passed, with no owned
tokens remaining. This is a retained route failure, not armor pickup proof or
an attributed engine defect.

THE-395 remains In Progress. Still missing are Q1 armor rotation/removal in both
editions and renderers, the GL quad cases, natural weapon/ammo contacts in
classic GL and rerelease CPU, and Q2 quad/powerup spin with two inspected
rotation images per edition/backend. Existing numerical Q2 ammo/weapon matrices
do not supply the quad visual evidence. Mike's owner retest remains outstanding.

Retained local evidence bundles:

- `qa-private-av-sn22y1pa/user/q1/bounded-quad-proof.json`, classic CPU.
- `qa-private-av-4ex9w9wu/user/q1/bounded-quad-proof.json`, rerelease CPU.
- Each quad bundle contains `q1-pickup-quad-spin-a.png`,
  `q1-pickup-quad-spin-b.png`, `q1-pickup-quad-after.png`, matching capture
  receipts, input/observer records, controller results and cleanup records.
- `qa-private-av-60s3cq75/user/q1/bounded-armor-route-failure.json`, failed armor
  transit and its actual final state.
- `qa-the395-quad-cpu-v4-20261007`, preserved quad helper copy;
  `installed-m0-shared-cvar-notification-20261007.json`, fixed installation
  receipt.

Game assets and captured images remain in private evidence storage.
