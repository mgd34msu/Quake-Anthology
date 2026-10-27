/* Port of src/ui/common/skin.ts. GPL-2.0-or-later. */
#include "qa/ui/common/skin.h"

#include <math.h>
#include <string.h>

#include "qa/foundation/support.h"

static qa_vec4 v4(double x, double y, double z, double w) {
  qa_vec4 out = { x, y, z, w };
  return out;
}

qa_ui_skin qa_ui_default_skin(qa_resource_id font) {
  qa_ui_skin skin;
  memset(&skin, 0, sizeof skin);
  skin.font = font;
  skin.font_scale = 1.5;
  skin.line_height = 14;
  skin.colors.text = v4(0.92, 0.88, 0.78, 1);
  skin.colors.disabled = v4(0.4, 0.4, 0.4, 1);
  skin.colors.accent = v4(1, 0.65, 0.22, 1);
  skin.colors.panel = v4(0.055, 0.06, 0.065, 0.94);
  skin.colors.control = v4(0.12, 0.13, 0.14, 0.9);
  skin.colors.focused = v4(0.3, 0.19, 0.09, 0.98);
  return skin;
}

qa_ui_skin qa_ui_skin_font(qa_ui_skin skin, qa_text_font_selection font,
                           double line_height) {
  double height = font.is_atlas ? font.font->line_height : font.classic->line_height;
  if (height <= 0 || line_height <= 0)
    qa_range_error("Menu font line height must be positive");
  /* W21 normalizes every atlas to an eight-unit text line before scale. */
  skin.font_scale = line_height / 8;
  skin.line_height = line_height;
  return skin;
}

qa_ui_skin qa_ui_hud_skin_font(qa_ui_skin skin, qa_text_font_selection font) {
  const qa_text_atlas *atlas = font.is_atlas ? font.font : font.classic;
  skin.has_cap_ink = true;
  if (atlas->has_cap_ink) {
    skin.cap_ink_top = atlas->cap_top;
    skin.cap_ink_height = atlas->cap_height;
  } else {
    skin.cap_ink_top = 0;
    skin.cap_ink_height = 8;
  }
  return skin;
}

size_t qa_ui_nine_slice(const qa_ui_image_slice *slice, qa_rect rect,
                        qa_vec4 color, qa_ui_draw_command *out,
                        size_t out_capacity) {
  double left = slice->border_left, top = slice->border_top;
  double right = slice->border_right, bottom = slice->border_bottom;
  if (slice->width <= 0 || slice->height <= 0 || left < 0 || top < 0
      || right < 0 || bottom < 0 || left + right > slice->width
      || top + bottom > slice->height)
    qa_range_error("Invalid UI nine-slice image");
  double border_scale = slice->has_border_scale ? slice->border_scale : 1;
  double sx = fmin(border_scale, rect.width / fmax(1, left + right));
  double sy = fmin(border_scale, rect.height / fmax(1, top + bottom));
  double xs[4] = { rect.x, rect.x + left * sx,
                   rect.x + rect.width - right * sx, rect.x + rect.width };
  double ys[4] = { rect.y, rect.y + top * sy,
                   rect.y + rect.height - bottom * sy, rect.y + rect.height };
  qa_vec2 uv0 = slice->uv[0], uv1 = slice->uv[1];
  double du = uv1.x - uv0.x, dv = uv1.y - uv0.y;
  double us[4] = { uv0.x, uv0.x + du * left / slice->width,
                   uv1.x - du * right / slice->width, uv1.x };
  double vs[4] = { uv0.y, uv0.y + dv * top / slice->height,
                   uv1.y - dv * bottom / slice->height, uv1.y };
  size_t count = 0;
  for (int y = 0; y < 3; y++) for (int x = 0; x < 3; x++) {
    double x0 = xs[x], x1 = xs[x + 1], y0 = ys[y], y1 = ys[y + 1];
    double u0 = us[x], u1 = us[x + 1], v0 = vs[y], v1 = vs[y + 1];
    if (x1 <= x0 || y1 <= y0) continue;
    if (count >= out_capacity) continue;
    qa_ui_draw_command cmd;
    memset(&cmd, 0, sizeof cmd);
    cmd.kind = QA_UI_DRAW_IMAGE;
    cmd.resource = slice->resource;
    cmd.rect.x = x0; cmd.rect.y = y0;
    cmd.rect.width = x1 - x0; cmd.rect.height = y1 - y0;
    cmd.tex_coords[0].x = u0; cmd.tex_coords[0].y = v0;
    cmd.tex_coords[1].x = u1; cmd.tex_coords[1].y = v1;
    cmd.color = color;
    out[count++] = cmd;
  }
  return count;
}
