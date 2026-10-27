/* Port of src/ui/common/accessibility.ts. GPL-2.0-or-later. */
#include "qa/ui/common/accessibility.h"

#include <string.h>

static qa_vec4 v4(double x, double y, double z, double w) {
  qa_vec4 out = { x, y, z, w };
  return out;
}

qa_ui_skin_colors qa_ui_accessible_colors(qa_ui_skin_colors colors,
                                          bool high_contrast,
                                          const char *color_mode) {
  qa_vec4 white = v4(1, 1, 1, 1), black = v4(0, 0, 0, 1);
  qa_ui_skin_colors base = colors;
  if (high_contrast) {
    base.text = white;
    base.disabled = v4(0.65, 0.65, 0.65, 1);
    base.accent = v4(1, 1, 0, 1);
    base.panel = black;
    base.control = black;
    base.focused = v4(0.2, 0.2, 0.2, 1);
  }
  if (color_mode != NULL && strcmp(color_mode, "monochrome") == 0) {
    base.text = white;
    base.accent = white;
    base.focused = v4(0.25, 0.25, 0.25, 1);
    return base;
  }
  if (color_mode != NULL && strcmp(color_mode, "blue-yellow") == 0) {
    base.accent = v4(1, 0.9, 0.2, 1);
    base.focused = v4(0.06, 0.2, 0.42, 1);
    return base;
  }
  return base;
}
