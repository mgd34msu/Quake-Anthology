# Q3 common player custody: THE-344

Q3 now borrows selected current movement from the common actor control owner.
Its ten duplicate delta-angle, ground, command-time, view, view-height,
Pmove-frame and jump-pad fields and their phase-copy callbacks are deleted.
Every production reader/writer of those fields migrates in this slice:
source Think, spawn/follow, items, weapons, view, source wire and checkpoint
capture. The typed census changes from 150 references in 13 files to zero.

The owner is `application_control_record.player`; the actual in-flight
continuation takes precedence over stale input during synchronous callbacks.
`application_control_borrow` in `src/app/application/control.c:2333` lends it,
and `application_control_frames_state_current` in `control_frame.c:1535`
selects it. The Q2 provider uses the same selection. Body position/velocity
remain in the shared world, with the existing motion-change operation updating
physical and selected movement state together.

Q3 policy when a foreign mover is selected remains in the small
`qa_q3_foreign_movement` rule tail. It has no current view, height, contact or
body cache. Detached sources, spectator history, immutable checkpoints and
protocol projections retain their required boundary values. They are not
second live movement owners. Source view input writes the common owner once;
full signed 32-bit Q3 command words survive forced-angle subtraction, and only
the original protocol writer narrows its fields. The serialized wire-client
layout remains 116 bytes, and explicit save field order/widths are unchanged.

Q3 configstring slots now hold IDs from the existing session string table.
The old per-slot text allocations and hook-notice copies are deleted. Writes
and restore intern at their existing boundaries; reads borrow immutable text.
Unset and explicit empty remain distinct where checkpoint presence requires
it. Restore into another session reinterns text rather than carrying runtime
IDs across namespaces. Reentrant change callbacks retain stable text.

Original references are Q3 `game/g_client.c:480-491` and `:1245` for forced
view/spawn clock, `bg_pmove.c:1811-1818`, `:1895-1901`, `:2031-2062` for raw
angles/command clock/substeps, `g_active.c:315-316` and `bg_misc.c:1445` for
jump-pad retirement, and `qcommon/msg.c:1101-1149` for wire PS widths.

The frozen 29-source-path tree is
`e2e39becacc7a579066102badc8b0779039d4da8`. Production, ASan/UBSan and
allocation-gate engine builds each pass all seven core checks. Records are in
`/tmp/qa-the344-q3-custody-build-20261009`, including source attestation,
build/test logs and a candidate byte-comparison receipt. Passing the gate
build's core checks does not establish whole-frame zero allocation.

Actual-source components preserve 2,179,200 bytes per original/candidate run
across 480 Q3/Team Arena, five-mover, four-rounding-mode and native/combined
cases. Eight GCC/Clang plain/sanitized runs pass. Four additional borrower
runs cover 30 completed/in-flight/synchronous cases, callback priority and
outer-call restoration; their provider/body constructor and entered-context
boundary are controlled. Configstring components preserve 505,739 bytes
across 289 outcomes in eight runs, including cold codec/restore into another
namespace and nested writes. Warm getters make no allocation/find/intern
calls; the original getters also made none. Detailed records are in
`/tmp/qa-the344-q3-owner-mirrors-20261009` and
`/tmp/qa-the344-q3-configstrings-20261009`.

This is not a projection speedup. Pinned 48-projection ABBA median/p99
microseconds are 2.4945/2.905, 2.8285/3.443, 2.8315/3.615 and 2.4055/2.833
(old/new/new/old). Direct current reads cost about 0.4 us more per batch in
that component. Removed phase-copy work and complete frame cost are not
measured by it. Changed explicit fail/fault call forms decrease by six;
other valid-state failure paths are not thereby closed.

Root privately launched the exact production candidate with a fresh copy of
the owner's profile in native Q3, Q3 with Q2 movement, and original Q3 on
retail q3dm0. The final CPU/Wayland images show the world, first-person weapon
and HUD. Each exits normally via its private compositor close; the original
profile remains unchanged and no recorded owned process remains. The receipt
and image paths are in the build record's `live.json`. This is stationary
startup/gameplay evidence, not keyboard, captured sound or full save/load
proof. The original-module log also reports a skipped control/move operation
and an autosave console-ownership failure, so those paths remain unproved.

The linked public direct-application fixture is separately blocked by an
existing publication sequence: program preflight is called both before
publication and again by seal; a published cvar edit is left pending while
source initialization registers settings. No fabricated context or extra
validation was added to qualify this slice. Actual dedicated frontend hooks
and full source/follow/detach reconstruction are still being checked.

The candidate is not installed: this verified primitive step precedes the
next complete-slice qualification. Player identity/rule tails, model-resource
lending and other adoption rows in `docs/core-adoption.md` remain open.
