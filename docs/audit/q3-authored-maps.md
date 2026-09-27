# Native Q3 authored map integration

The Q3 map owner adds native spawn, target, trigger, shooter, portal, item-team
and mover behavior using the shared actor, body, collision, combat, resource and
scheduler services. Typed private state and map continuation retain authored
callbacks and gravity; the application still owns restoring shared gravity
once across all selected providers.

An independent implementation worker read the complete new map sources and
headers, native mover code and changed item/game integration against their donor
paths. Review found and the author corrected brush-trigger bounds and angle
admission, forced hidden-item grants, synchronous laser activation, callback
generation checks, nonpositive think deadlines, shooter RNG use, zero/reverse
gravity continuation, ordered vector parsing and an undeclared state type.

Reciprocal rereview caught three more defects: the non-player branch of
`trigger_multiple` used state after an actor-traits callback; persistent-item
pickup retained a player pointer across provider callbacks; and a valid
respawn default-plus-random value could overflow signed multiplication.
Corrections were independently reread, including pickup denial callbacks and
speaker cadence fields. The final bounded review reported no additional
demonstrated defect. The initially suspected direct oversized-wait example
was refuted by existing input validation.

Shootable mover admission uses the separately reviewed shared local combat
handler. A handled mover hit does not emit a false player death. The map restore
path retains the corresponding private admission state.

This review covers the revised authored-map packet, not an exhaustive review
of unchanged Q3 weapons, player movement or physics. Application consumers,
composition and full baseline qualification remain open; B12/B13 are not closed.
No engine configuration, compilation or execution ran.
