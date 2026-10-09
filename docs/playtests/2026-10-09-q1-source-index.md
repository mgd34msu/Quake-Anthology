# THE-344 Q1 frontend source index

Q1's frontend no longer keeps another entity-number-to-actor array. Admitted
actors resolve through `qa_actors_at_source`; only a missing actor enters the
existing CLIENT admission boundary. Metadata is a read-only index lookup, and
brush classification reads the live actor's source slot. The duplicate type,
linear scans, growth, reset and free are deleted. Received snapshots and CLIENT
ownership/checkpoint references retain their distinct roles.

Loaded SETVIEW performs the existing map-generation admission once, before
indexed presentation reads. Stock NetQuake sends serverinfo then setview
(`qsrc/quake/WinQuake/sv_main.c:198,225`); the QuakeWorld gamestate adapter
produces that sequence too. No receiver-busy check was loosened.

Eight GCC/Clang plain and ASan/UBSan component executions pass. They use the
actual actor registry, CLIENT admission/refresh/capture, Q1 entity-current
callback and frontend lookup/SETVIEW code. Physical ownership/network access
are controlled component boundaries. Cases cover large source numbers,
foreign/unbound actors, busy SETVIEW, map retirement, saved references and
released/reused generations. Strict compilation of the actual frontend
translation units passes. Evidence is in
`/tmp/qa-the344-q1-actor-cache-20261009`.

The isolated source tree `10c4ac7c8a37076f4395fce9bafa18f78e974435`, based
on `912ffdf7`, passes production, ASan/UBSan and allocation-gate builds and
seven core checks each. Logs and frozen candidate are in
`/tmp/qa-the344-q1-actor-build-20261009`.

Four private copied-owner-profile CPU runs reached classic start, rerelease
start, retail `demo1` playback and the explicit menu. The coordinator viewed
all final PNGs: worlds and HUDs were visible, and the demo log records pickup
events and completion. Gameplay runs quit through the private compositor's
normal window-close event; the menu exits at its public frame limit. Each
exits zero, leaves owner files unchanged and cleans all recorded process IDs.
`live.json` and `visual-review.json` retain the bounded proof.

This is private software-compositor/dummy-audio evidence, not sound proof,
real keyboard play, full protocol parity or a speedup measurement. THE-344
still has other current-state and body callers to migrate. No partial-slice
installation was made.
