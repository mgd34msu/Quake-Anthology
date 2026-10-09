# THE-344 one per-seat centerprint record

`qa_hud_center_state` in `include/qa/hud.h` is the current fixed-size record.
Q1/Q2/Q3 local, legacy and Unified producers publish into their seat's HUD.
The separate Q3 text/time/position store, mutation API and context validator
are deleted. Layout, reveal cadence, fade, source font and Team Arena drawing
remain presentation rules. Source-owned output suppresses a second common
centerprint while retaining supplemental notices/status.

`/tmp/qa-the344-centerprint-20261009` contains eight actual record/draw component
executions under GCC/Clang plain and ASan/UBSan. Cases include UTF-8, bounded
replacement, reveal without shifting line centers, source placement/width,
duration edits, fade, original status routing and color reset. Fonts/graphics
and some external admission adapters are controlled. All twenty changed C
units pass both strict compiler checks. This is algorithm/routing evidence,
not a screenshot of every native or guest centerprint.

Full integration caught a remote Q3 seat-member error missed by the initial
two-unit syntax check. Root corrected it to the existing
`services.resources.physical_seat`; the refreshed whole-unit checks cover that
adapter. Root also preserved the original Q3 status/head helpers and terminal
color reset after reviewing the original worker diff.

Production, ASan/UBSan and allocation-gate builds and seven core checks each
pass for frozen tree `5ac029daf3f68877f8b34880c0172687631e5154`, recorded in
`/tmp/qa-the344-centerprint-build-20261009`. Private copied-owner-profile CPU
classic Q1, rerelease Q1, Q3 and Q1 with Q3 movement/character reach visible
world, weapon and original status output and quit normally. Root opened all
four final PNGs. Dummy audio is containment only. These stationary captures
contain no active centerprint. A virtual-W attempt also produced no observed
hall traversal/centerprint and is not counted as that proof. Component drawing
evidence remains bounded accordingly.

The first Q3 launch used an incorrect product id in the root harness and exited
before gameplay; corrected `q3-baseq3` passes. `live.json`, `center-walk.json`
and `visual-review.json` retain both excluded and passing evidence. Original
profile files are unchanged and all recorded owned processes were cleaned up.
No installation, captured sound or speedup is claimed. THE-344 still has the
remaining common PlayerState, Q3 custody and other caller adoption work.
