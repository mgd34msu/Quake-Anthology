# Common family types: THE-344

Scene, audio and input buttons use the existing `qa_game_family` type in
`include/qa/ruleset.h`. Their three duplicate enums and every production/test
type/token caller are deleted. Each role still selects its own family;
original BSP/collision families and foreign ABI constants remain separate
domains. Numeric family values stay 0/1/2.

The intrinsic product-to-ruleset mapping now has one implementation,
`qa_product_ruleset` in `src/content/catalog/catalog.c:4`. Eight callers use
it, passing their original invalid-family fallback as caller data. Explicit
configured component clocks remain independent. Buttons use the same family
type while retaining Q1 fractions and Q2/Q3 division/rounding rules.

The 129-source-path slice contains only type/token substitutions, the three
enum deletions, includes and the shared intrinsic mapping. The scene/audio
packet passes 364 strict before/after GCC/Clang TU checks and eight actual
component runs of 1,985 cases. Complete outputs agree for invalid families,
channels, Q1/Q2 looping versus Q3 policy, source-rate layout and six public
structure sizes/alignments/offsets. The button packet passes eight strict TU
checks and four plain/sanitized component runs with 3,024 bit-identical
arithmetic/lifecycle comparisons. The product mapping passes 36 strict TU
checks and four component runs with 242 exact comparisons, including invalid
families/editions and the original optional-provider fallback.

Component records are in `/tmp/qa-the344-scene-audio-family-20261009`,
`/tmp/qa-the344-button-family-20261009` and
`/tmp/qa-the344-product-ruleset-20261009`. Their actual functions and public
layout are compared; diagnostics are compared by codes, not text. These
components do not prove live renderer/audio behavior or a frame-time speedup.
The isolated integration record is
`/tmp/qa-the344-family-build-20261009`, frozen code tree
`5c1861a851629c335b22bdd6d6ee4262294eb42c`.

No alternate family TYPE remains for those three capabilities. Original
rule policies, entity identity custody, fixed allocation and remaining
per-role selection/caller adoption are still separate rows in
`docs/core-adoption.md`; this slice does not close the whole issue or install
a new executable.
