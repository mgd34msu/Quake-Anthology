# Local Quake movement and view checks, 2026-10-06

Actual public keyboard and mouse checks passed on retail `e1m1` in the installed
testing executable. Native QuakeWorld used `733077cb`; Original QuakeWorld and
native NetQuake used `dc143246` before the final native-QW angle correction.
Each run returned exit zero and its owned debugger, game and private display
were removed. These diagnostic captures are not performance measurements.

QuakeWorld retains `groundentity` while airborne. Physical body refresh must use
its actual `FL_ONGROUND` flag, as NetQuake already did. After `dc143246`, Original
QW reports no contact while rising and descending, then WORLD contact after
landing. Standing and walking remain grounded, released movement stops, and
jumping raises the player before returning to the ground.

Native QW also exposed a stale view assignment during body refresh. Actual input
and command yaw changed while the rendered view stayed at its authored angle.
`733077cb` removes that overwrite. The shipped native run then followed public
mouse movement from 90 to 180.0769 degrees in input, command and rendered view,
with the movement and contact checks still passing. Original QW consumes authored
`fixangle` once and keeps its Source `v_angle` aligned with the command.

The checks preserve actor identity across movement. Genuine failing captures
remain in the local evidence record; they are not replaced by successful ones.
Native NetQuake's public walk, release, jump and landing checks passed as well.
This record does not qualify complete campaigns or multiplayer interoperability.
