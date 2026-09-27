# Hipnotic trigger integration

Native map callbacks now implement counter/start-stop cycles, oncount and the
source's trigger_command binding, key consumption, remove triggers, gravity,
decoy use, waterfalls, damage thresholds and breakaway walls. They use the shared
target, inventory, body and combat owners. Gravity changes route to the selected
movement provider. Counter state is exposed through the existing typed target
field query.

Root read the complete new `hipnotic_triggers.c` implementation and complete
donor `missionpacks/world/hipnotic-triggers.ts`, the donor common trigger/brush
helpers, and the public/internal/runtime integration diff. The review checked
counter activator retention, native-only victim hiding in remove_touch, source
brush angle clearing, key sound/message order, generation refresh after callbacks
and typed gravity routing. No concrete defect was found in that bounded review.

Native map use dispatch now lets each authored callback own activator assignment;
counter_stop must preserve its original starter. The author reviewed existing
map callbacks for their explicit assignments. A native touch-disabled flag also
preserves remove_touch's effect for non-map actors and native clones.

Source checks only: no compilation, parser execution, tests or gameplay. Complete
Q1 authored hazards/rotation/Rogue behavior, private continuation codecs and
application integration remain open under the original B10/B13 requirements.
