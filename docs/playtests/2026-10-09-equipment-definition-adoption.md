# THE-344 / THE-2874 selected-equipment definition

Equipment publication now reads its selected item with the existing
`qa_inventory_item_definition_find`. The caller's full-catalog allocation,
copy, scan and ambiguity check are deleted. Both lookup forms select through
the same inventory definition owner; within-group duplicates are rejected at
admission. Missing items, non-weapon error codes, borrowed labels, ammunition
and start-requirement status retain their behavior.

The actual `inventory.c` component compares thirteen admitted group scenarios,
including mixed-source overlap in either order, inactive groups, owner zero,
missing/retired definitions and non-weapons. Actor liveness and load-created
groups are controlled boundaries. GCC and Clang, each plain and ASan/UBSan,
pass both implementations with identical numeric and label outputs. For 600
unchanged publications, caller allocations/frees fall from 600 pairs to zero.
The comparison does not assert exact error text.

Evidence: `/tmp/qa-the344-equipment-definition-20261009` contains the script,
compiler/run logs and outputs. The isolated candidate source tree is
`05c744775ded447df7ecbfd6c5674753f62c99f1`, based on `f8b59820`.
Production, ASan/UBSan and allocation-gate builds plus the seven core checks in
each configuration pass; logs are in
`/tmp/qa-the344-equipment-build-20261009`.

This is a caller adoption and allocation measurement. It does not establish
whole-frame zero allocations, a frame-time speedup or complete THE-344
adoption. No partial-slice installation was made.
