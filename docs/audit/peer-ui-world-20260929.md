# Independent UI source review, 2026-09-29

Reviewer: world/network worker. This review covers the eleven frozen source/header paths and SHA256 identities listed in `docs/implementation/ui-20260929.md`. Each identity matched the checkout during the review. All eight UI C sources, `src/ui/internal.h`, `include/qa/ui.h`, and `include/qa/hud.h` were read completely. Relevant input token retirement and font layout scale implementations were read to check ownership and measurement claims; the donor list-key path was also compared. Other application, renderer, network and source-family UI files were not inspected for this packet.

No configuration, compiler, parser, build, test, runtime, benchmark or gameplay execution occurred. The findings are source traces, not runtime reproduction or B32 acceptance.

## Confirmed findings

1. **P2: End cannot select the last enabled list row when the final row is disabled.** In `src/ui/input.c`, `list_key` sets `selected = count - 1` for End but leaves `direction = 0`. Its disabled-row loop treats every nonnegative direction as forward; at the final disabled row it returns without emitting `QA_UI_SELECT`. For enabled A, enabled B, disabled C with A selected, End keeps A rather than selecting B. Installed-product lists can contain disabled unavailable products. The donor controller filters enabled rows before applying End. Search backward for End, or perform keyboard selection over enabled row indices.

2. **P2: Subunit list row heights have inconsistent pagination and become unreachable.** `src/ui/controller.c` validates any positive `row_height`, and `ui_list_page` divides by that raw value. `src/ui/draw.c::list_draw` and mouse row selection in `src/ui/input.c` instead normalize the height to at least one. A valid height-10 rectangle with `row_height = 0.5` and 100 rows computes a page of 20 and maximum top of 80, but drawing advances one unit per row and clips after ten visible rows. The final ten rows cannot be scrolled into view. Use the same normalized row height in pagination, drawing and input, or reject this geometry consistently at admission.

The root UI owner was notified to implement these corrections; this reviewer did not modify UI ownership paths.

## Refuted concerns and bounded disposition

Input token retirement commits before returning a held-binding release error; the controller close path consumes the corresponding stack entry and does not retain a live borrowed handler to freed menu context. Stable list row keys are copied before an action can rebuild the factory's borrowed rows. Field-width normalization matches the shared font layout's eight-unit line-height convention. HUD draw marks both HUD and UI callback guards and restores the shared UI transform on success or failure. Inventory definition labels are copied before later provider observations. Mod refresh retains catalog ownership while rebasing the complete staged draft, and generation guards prevent stale application changes from silently replacing a newer configuration.

Both corrections were independently re-read in their exact surrounding paths. Pagination now normalizes its divisor with `fmaxf(1, row_height)`, matching drawing/click input. End now sets reverse scan direction with zero step, preserving the final candidate before walking backward over disabled rows. The concrete failure traces above are closed by source inspection. Superseding SHA256 identities: controller.c `fe8ca3b820aa481b80864872b810de4474a9c2041ebdf0cc04c36d6470b00300`; input.c `39be6d767104a7e76cee859d2acc827a0bd2cd15e0003661a3e791c6d9e543eb`.

No additional concrete defect was confirmed in the inspected paths. This disposition does not cover missing source-family HUDs, frontend registration, guest UI execution, save menus, comprehensive controls, or the rest of B32. It is a bounded source acceptance of the reviewed UI packet and corrections, with all runtime behavior unverified.
