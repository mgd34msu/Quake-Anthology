# Shared bot movement and observed inventory

Native movement controllers now use the selected navigation, collision,
physics capabilities, equipment and elementary-action owner. Route, prediction,
candidate, point and visited storage is retained for reuse. Typed movement
state covers ordinary ground travel, ladders, movers, swimming, jumps, grapples
and travel view selection without TypeScript allocation and coroutine machinery.

Root read every new movement implementation/header and the four navigation
interface changes. Donor comparisons covered movement state, route/controller,
ground and special travel, and navigation adapters. Two review findings were
corrected and reread: restore now guards its navigation callback against nested
state mutation; selected grapple observation no longer guesses a Q3 weapon
index when the actual arsenal has no such weapon. A module-specific observation
adapter can still supply its source profile. The missing bobbing-model report
now retains the source INFO severity, model number and newline. The existing
travel admission already rejects use before variables are configured.

Native and guest inventories share the fuzzy evaluator and weapon selector.
An external inventory callback preserves the two comparison reads and the
interpolation read after both leaves; native arrays stay direct. Genetic
selection uses one bounded native rank workspace, preserves both source input
passes and writes parent/child outputs in source order. Root read all five
changed resource files and compared donor weight evaluation, weapon selection
and the complete genetic selection routine. No additional defect was confirmed.

The goal owner and its four implementation units are now integrated after a
separate independent comparison with the complete donor `goals.ts`. This covers
map items, locations and camps, source goals, goal stacks and avoidance, fuzzy
selection, retained weight mappings and checkpoint state. Corrections preserve
the source missing-weight FATAL report with successful no-op, interbreed
topology diagnostics, stack-overflow report and dump, item expiry before absent
navigation, developer/debug routing and the 127-byte classname boundary. Root
also read the shared topology-report API changes, which retain all three weight
trees across callbacks. No additional defect remained in that bounded review.

Bot runtime, decisions, roster, all-owner
continuation and concrete application consumers remain open. This does not
close B26 or B23. No engine configuration, compilation or execution ran.
