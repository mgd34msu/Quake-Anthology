/* Port of src/ui/common/layout.ts. GPL-2.0-or-later. */
#include "qa/ui/common/layout.h"

#include <math.h>

#include "qa/foundation/support.h"
#include "qa/foundation/js.h"

qa_ui_transform qa_ui_fit(qa_rect area, double scale) {
  if (!isfinite(scale) || scale <= 0 || area.width <= 0 || area.height <= 0)
    qa_range_error("UI requires a positive scale and safe area");
  double factor = qa_js_min(area.width / 640, area.height / 480) * scale;
  qa_ui_transform out = { area.x + (area.width - 640 * factor) / 2,
                          area.y + (area.height - 480 * factor) / 2, factor };
  return out;
}

qa_vec2 qa_ui_point(qa_vec2 point, qa_ui_transform transform) {
  qa_vec2 out = { (point.x - transform.x) / transform.scale,
                  (point.y - transform.y) / transform.scale };
  return out;
}

bool qa_ui_contains(qa_rect rect, qa_vec2 point) {
  return point.x >= rect.x && point.y >= rect.y
      && point.x < rect.x + rect.width && point.y < rect.y + rect.height;
}

qa_rect qa_ui_intersect(qa_rect a, qa_rect b) {
  double x = qa_js_max(a.x, b.x), y = qa_js_max(a.y, b.y);
  qa_rect out = { x, y,
    qa_js_max(0, qa_js_min(a.x + a.width, b.x + b.width) - x),
    qa_js_max(0, qa_js_min(a.y + a.height, b.y + b.height) - y) };
  return out;
}

static qa_rect map_rect(qa_rect value, qa_ui_transform t) {
  qa_rect out = { t.x + value.x * t.scale, t.y + value.y * t.scale,
                  value.width * t.scale, value.height * t.scale };
  return out;
}

qa_ui_draw_command qa_ui_transform_command(qa_ui_draw_command command,
                                           qa_ui_transform transform) {
  switch (command.kind) {
    case QA_UI_DRAW_FILL: case QA_UI_DRAW_IMAGE:
      command.rect = map_rect(command.rect, transform);
      return command;
    case QA_UI_DRAW_CLIP:
      if (command.has_clip_rect) command.rect = map_rect(command.rect, transform);
      return command;
    case QA_UI_DRAW_TEXT:
      command.origin.x = transform.x + command.origin.x * transform.scale;
      command.origin.y = transform.y + command.origin.y * transform.scale;
      command.scale = command.scale * transform.scale;
      return command;
  }
  return command;
}

qa_rect qa_ui_menu_row(double index, qa_ui_menu_row_options options) {
  double height = options.has_height ? options.height : 28;
  qa_rect out = { options.has_x ? options.x : 64,
    (options.has_y ? options.y : 92) + index * height,
    options.has_width ? options.width : 512, height };
  return out;
}

qa_rect qa_ui_menu_row_default(double index) {
  qa_ui_menu_row_options none = { false, false, false, false, 0, 0, 0, 0 };
  return qa_ui_menu_row(index, none);
}
