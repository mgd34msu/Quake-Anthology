# THE-344 Q2 prediction item IDs

Q2 inventory prediction compares the existing admitted weapon/ammunition IDs.
The one weapon accessor exposes the existing ammunition ID too; pending-weapon
publication keeps using the weapon ID. The previous accessor signature and
the prediction caller's string conversion/comparison loop are deleted.
Zero IDs remain excluded. No new table, namespace conversion, interning,
identity hash or current-state cache was introduced.

The actual definition loader and inventory projection component pass 56
classic/rerelease/product/hook profiles, 3,317 selection comparisons and
1,407 retained-row comparisons. Signed/fractional quantities, all count
policies, unavailable weapons, zero/out-of-range IDs, item-equals-ammo and
mixed names are covered. Actual string/arena/error implementations run;
session binding, provider services and preallocated frame backing are
controlled component boundaries. GCC/Clang plain and ASan/UBSan pairs and
strict production translation-unit checks pass.

The old path performs 3,317 string reads and 82,507 comparisons. The new path
performs zero string reads/finds/interns/comparisons. This is a call-count
proof, not a frame-time result or full module admission/play test.

Evidence is in `/tmp/qa-interned-q2-prediction-20261009`. Production,
ASan/UBSan and allocation-gate builds and seven core checks each pass for
isolated source tree `f08a225666d2dcd5664cbeff9feb99c19e4d0aa1`, based on
`563ea50f`; build logs are in `/tmp/qa-core-q2-names-build-20261009`.
Other hot identity callers remain listed in `docs/core-adoption.md`.
No partial-slice installation was made.
