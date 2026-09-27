# Rogue platforms and elevator buttons

The native map implementation adds `func_new_plat` and `func_elvtr_button` using
the existing Q1 mover, body, target, combat and sound services. Typed continuation
holds floor selection, call delays, return state and button state; the map owns
the shared elevator direction. Floor counts retain the authored float value.
Button geometry can come from a live foreign actor through the canonical body
service. Base and Rogue platforms share trigger construction and bounds logic.

Root read the complete native `rogue_plats.c`, all four changed map files' hunks,
the complete donor `missionpacks/world/rogue-plats.ts` and `common.ts`, plus the
donor mover completion and `calcMove` paths. The source's nonfinite numeric-field
fallback was checked against `foundation/entity.ts:101`. No additional defect
was confirmed in this bounded review.

Shared move completion now retains the destination and completion action across
callbacks, publishes origin and links before zeroing velocity, then clears the
pending move and invokes its action. Each body callback is followed by a full
actor-generation lookup. Move setup cancels its prior think and retains the
requested speed before callbacks. The implementation owner found and corrected
these lifetime/order issues while integrating the new callers.

Private checkpoint coverage and application integration remain unfinished. This
source review does not close B10 or B13. No engine compilation or execution ran.
