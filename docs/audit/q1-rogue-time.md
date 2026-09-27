# Rogue time machine source integration

The native Rogue time machine and core use the shared Q1 actor, combat, body,
target, sound and scheduler services. Typed map state holds pain/crash behavior,
cooldown, the world machine reference and cutscene flag. Retained explosion,
chunk and stop-shake actors preserve damage thresholds, random draw order,
falling velocity and lava/effect timing from the donor.

Sprite conversion is shared with projectile and Hipnotic explosions. The helper
sets presentation and physics state; its callers retain their event, link and
schedule order. Those callers reacquire actor generations after callbacks.

Root read the complete native `rogue_time.c` and donor
`src/content/q1/missionpacks/world/rogue-time.ts`, all changed dispatch, field,
state and sprite-helper hunks, and the existing resource, model, scheduler and
touch paths. No additional defect was confirmed in that bounded source review.

The Rogue camera/actor ending sequence, after-physics consumer, private save
continuation and application bindings remain open. This source integration
does not close B10 or B13. No engine compilation or execution ran.
