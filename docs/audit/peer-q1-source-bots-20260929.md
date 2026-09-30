# Q1 source metadata and authored behavior review

This is a bounded source review by the bot implementation owner, independent of
the Q1 implementation owner. No compiler, parser, build, executable, test,
benchmark, game or sanitizer was run.

## Weapon observation and borrowed source lifetime

The reviewer read the new detached weapon observation, its shared shape and
timing table, the affected ordinary fire branches, and the corresponding source
helpers in horde, Hipnotic, Rogue and projectile code. The review checked native
ownership, availability, fallback weapons, ammo cost, infinite ammo admission,
bloody shotgun, discharge, launch angles, paired muzzle origins, actual gravity,
and the distinction between fire continuation and attack deadline. The observer
does not invoke RNG, consume ammo or advance animation itself. Native policy
callbacks can still have their own effects, and the source operation borrow
covers those callbacks.

The observer intentionally returns nominal source facts. Target-dependent hammer
combos, random replacement projectiles, homing after launch and recipient damage
policies remain outside those detached estimates. This review does not accept
every Q1 weapon outcome or all cross-provider weapon adapters.

One confirmed high lifetime defect was found and corrected by the Q1 owner.
During a standalone source observation, `qa_q1_game_operation_begin` incremented
`observation_depth`, but provider frame entry did not check it. A traits or weapon
parameter callback could retire the borrowed actor and invoke
`qa_session_advance` while the session was otherwise idle. Frame entry recycled
retired actor and player storage; a subsequent source spawn could reuse storage
still borrowed by the outer observation. The corrected frame entry rejects
`destroy_pending` or nonzero `observation_depth` before map frame processing,
reclamation and clock mutation. Permanent application retention remains allowed.
The reviewer reread the admission and operation end/live paths. This closes the
traced reuse path; it is source acceptance, without runtime reproduction.

Verified identities:

```text
4ccf939ae99d2f55d1c598bb16bcbbab30fe80591e5a3754e66304d25cd7b0bc  src/gameplay/q1/weapons.c
40b93d7cfe02b6b1625f6cdaff382cc89c927413da340ea570caaca2cecccf39  include/qa/game_q1.h
37dbccf0d9bce4c2ddb07019a21e0243553057f46c956187fb1201ccc99d9dca  src/gameplay/q1/runtime.c
```

## Addon fields and Hipnotic authored continuations

The reviewer read the complete C `addon_fields.c`, `hipnotic_hazards.c` and
`hipnotic_rotation.c`, comparing them with complete donor `field-triggers.ts`,
`hipnotic-hazards.ts`, `hipnotic-rotate.ts` and `rotate-targets.ts`.

The comparison covered addon hurt toggle/cooldown, push timing and shelter
state, MG3 direction/jump policy, sound cooldowns, mine acquisition/explosion,
lightning pulse and private bolt lifetime, Tesla ownership and claims, wrath
cycles, gravity pull, continuous rotation acceleration, grouped rotating door
reversal, wall collision damage, clock ticks, train path event/damage flags,
instant path jumps, stop/wait transitions and normal interpolation. It also
read the affected map continuation codec branches, retained rotation/contact
tables, map reset/cleanup and map cloning. Those codec reads do not constitute
acceptance of the complete Q1 save format or all external reconstruction.

Addon hurt now rejects a touch while actual source solidity is not trigger
before invoking damageability observation. This matches the native cooldown
admission. Ordinary Q1 physics already filtered non-trigger contacts; the
suggested cross-provider stale-contact production path was plausible rather
than demonstrated. The reviewer accepts this narrow guard, without upgrading
that unproven path to a confirmed production failure.

One confirmed rotation integration defect was corrected by the source owner.
At the initial inspected identity, `publish` treated every positioned
body as `QA_BUILTIN_MOTION_TELEPORT`. Ordinary non-wall target rotation and train
interpolation therefore published teleport revisions every tick. Application
bot projection exposes those revisions as `teleport_sequence`; AI frame entry
resets `teleport_time` on each new revision, and combat suppresses attacks during
the reaction interval. A player targeted by continuous rotation can consequently
remain in repeated teleport reaction suppression. Ordinary donor target rotation
is a body origin update; explicit instant train path jumps must remain distinct.
The owner changed ordinary target rotation and train interpolation/completion
to publish `QA_BUILTIN_MOTION_LAUNCH`. Initial train placement and explicit
instant path jumps, including their target placement, retain teleport admission.
The reviewer reread the helper, target transformation, interpolation and every
remaining true teleport callsite. The narrow correction closes the repeated
teleport-reaction trace while preserving body reconciliation and linking.

Current inspected identities:

```text
69030d0708598483d8907e358b51477e61b039cb669e0bf991b055f87db2a1d2  src/gameplay/q1/maps/addon_fields.c
519b2ef6c04df0e321fe78bfc59e68e923f70b6ec62fb922f8b86d4beb9152a4  src/gameplay/q1/maps/hipnotic_hazards.c
41a5020fcbfaf701970b8c32593629735a4343de85e0001bfda1bec0f28ed18a  src/gameplay/q1/maps/hipnotic_rotation.c
```

No other confirmed defect survived this bounded comparison. Full family
behavior, source maps, mixed-provider movement and real gameplay remain open.
