# Q2 rerelease parasite continuation

This source chunk implements the retained rerelease parasite tip and segment,
mouth offsets, initial trace, attachment, drain/heal, obstruction, doubled-speed
retraction, crush reset, wait/break frames, and pain/death hooks. It uses the
shared projectile, physics, world, and combat owners. Presentation exposes the
segment endpoint through the existing projectile view. Monster checkpoints
retain the proboscis reference; projectile restore validates the new phase domain.

An independent worker read the full new C body against the full donor proboscis
and parasite bodies, checked 32 break and 18 drain frame/action rows, and traced
the changed dispatch, AI, combat, save and rendering hooks. Three findings were
corrected by the source owner and independently reread:

- Initial beam drawing must retain the firing origin captured before a wall-hit
  callback changes the owner's yaw.
- Restore must reject invalid phases for both new projectile kinds.
- Rerelease parasite pain must use its full source branch: retract, debounce,
  classic random sound choice, source pain admission, and deferred move change.
  Generic monster pain changed flags and difficulty/ducking behavior incorrectly.

Root reviewed the shared dispatch and checkpoint integration and matched the
final source hashes to the independent review. Suspected launch/reel fallthrough
was refuted because the species callback intercepts those names first.

The accompanying Q2 constructor now prepares private state without registering
a live session component. The application admits its borrowed component and
owns explicit destruction after detachment at a safe point.

Common Q2 AI/start-run behavior, summons, assembled saves, application physics,
traits and rendering consumers remain open under the original graph. This
bounded source review does not close B11/B13/B30/B34. No engine code was compiled,
tested or executed.
