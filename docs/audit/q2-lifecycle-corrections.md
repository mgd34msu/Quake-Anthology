# Q2 monster integration follow-up

This source review extends the frozen B11/B13 audit in `q2.md`. The original
task goals and acceptance criteria remain unchanged and incomplete.

The reviewed monster implementation and immutable animation tables are now
registered in `qa_game_q2`. Native Q2 lifecycle calls initialize and close its
per-instance state, dispatch monster use, retire actor references, and flush
pending monster damage at the source frame boundary. The shared mission
interface retains authored campaign ownership independently of monster choice.

The new lifecycle body implements delayed startup, floor placement, authored
routes, dormant and triggered spawning, first-death accounting, item drops,
health/death targets, and commander accounting. Rendering exposes the source
old frame, render flags, and visibility. Typed checkpoints retain the new
continuations. The audit reader's complete earlier source inventory remains
in `q2.md`; root additionally inspected these new lifecycle paths and their
shared-service call sites before integration.

Root review found cached entity/body state being used after provider callbacks.
The owner corrected health-target restoration, foreign route reads, source
refresh, trait mutation, world-link completion, and use callbacks to recheck
the original actor and native state before access. Root reread those corrections.
Retired actor nodes remain allocated through the source frame, while monster
state can be released sooner; a surviving actor pointer alone is insufficient.

The connection projection in `builtin.h` is read-only and score-free. It shares
connection and selected-character information; scores remain with the explicitly
selected mode. Its Q2 consumers are a separate active correction.

This integration does not close manual turret activation, revival reset,
remaining AI/summoning behavior, cross-family player consumers, or application
mission/counter bindings. Compilation and runtime qualification remain after
the complete baseline. No engine code was executed for this review.
