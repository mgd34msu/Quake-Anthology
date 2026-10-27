/*
 * Port of src/ui/common/layout.ts: Q3 ui_atoms.c's aspect-correct
 * 640x480 space, fitted to each seat's safe area.
 * GPL-2.0-or-later.
 */
#ifndef QA_UI_COMMON_LAYOUT_H
#define QA_UI_COMMON_LAYOUT_H

#include <stdbool.h>

#include "qa/contracts/math.h"
#include "qa/contracts/render.h"
#include "qa/contracts/ui.h"

/** TS UiTransform. */
typedef struct qa_ui_transform { double x, y, scale; } qa_ui_transform;

/* TS fitUi(area, scale = 1). Aborts (RangeError) on a non-finite or
 * non-positive scale or a non-positive safe area. */
qa_ui_transform qa_ui_fit(qa_rect area, double scale);
/** TS uiPoint. */
qa_vec2 qa_ui_point(qa_vec2 point, qa_ui_transform transform);
/** TS contains. */
bool qa_ui_contains(qa_rect rect, qa_vec2 point);
/** TS intersect. */
qa_rect qa_ui_intersect(qa_rect a, qa_rect b);
/* TS transformUi. Strings in the result borrow from command. */
qa_ui_draw_command qa_ui_transform_command(qa_ui_draw_command command,
                                           qa_ui_transform transform);

/** TS menuRow options; a false has_ flag is a TS undefined field. */
typedef struct qa_ui_menu_row_options {
  bool has_x, has_y, has_width, has_height;
  double x, y, width, height;
} qa_ui_menu_row_options;
/** TS menuRow(index, options = {}). */
qa_rect qa_ui_menu_row(double index, qa_ui_menu_row_options options);
/** TS menuRow(index) with no options. */
qa_rect qa_ui_menu_row_default(double index);

#endif
