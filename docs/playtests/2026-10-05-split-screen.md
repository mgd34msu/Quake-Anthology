# Split-screen playtest

MIKE-11 passes in the actual `qfiles/qa-c`. Seven live checks cover Q1 classic, Q1 rerelease, Q2 classic, Q2 rerelease and Q3, with CPU and GL output. Real kernel controllers supplied the input; the debugger observed the actual actors and rendered world views without changing game state.

Each controller changed only its assigned player's look and movement. Each world camera rendered inside that seat's own viewport, and releasing the controls stopped movement. The public Controls menu added a fourth player, assigned keyboard/mouse to that player, and removed it again on both renderers. The original three player/controller identities survived the change.

The shared SDL admission fixes are `6b4d8123` and `2c139d2c`. They retain the opened handle's actual instance, tolerate a genuinely removed device, and rescan after removal shifts enumeration indices. An open failure for a still-present device remains an error. The final live startup check removed the first real controller at the SDL open boundary; both surviving devices opened, controlled separate players, stopped on release, and the game quit normally.

The final controller-removal, Q2 rerelease GL and Q3 CPU seat-change runs used shipped revision `09bf85f7`. Earlier family and GL seat-change checks identify their artifact and private receipt in [the observations](2026-10-05-split-screen.json). Every included run exited normally. Failed private observation assumptions and the actual earlier startup failure remain in the private archive.

These checks cover local viewports, physical input, joining/removal and controller lifetime. They make no network split-screen or performance claim. Original references are Quake/Quake II renderer viewport setup, Q3 viewport/scissor setup, and Quakespasm's SDL instance and hotplug handling. No game assets are included.
