/* Port of src/ui/common/legacy/borders.ts. GPL-2.0-or-later. */
#include "qa/ui/common/legacy/borders.h"

/* TS const f = Math.fround: compute in double, cast at f() points. */
static double fr(double value) { return (double)(float)value; }

static const qa_text_texture_rect UV = { 0, 0, 0, 0 };

void qa_ui_draw_cg_top_bottom(const qa_draw2d *draw, qa_rect rect,
                              double size,
                              const qa_picture_asset *picture) {
  qa_rect r = qa_draw2d_adjust(draw, rect);
  double vertical = fr(fr(size) * qa_draw2d_scale_y(draw));
  qa_rect top = r;
  top.height = vertical;
  qa_draw2d_stretch_pixels(draw, top, UV, picture);
  qa_rect bottom = r;
  bottom.y = fr(fr(r.y + r.height) - vertical);
  bottom.height = vertical;
  qa_draw2d_stretch_pixels(draw, bottom, UV, picture);
}

void qa_ui_draw_cg_sides(const qa_draw2d *draw, qa_rect rect, double size,
                         const qa_picture_asset *picture) {
  qa_rect r = qa_draw2d_adjust(draw, rect);
  double horizontal = fr(fr(size) * qa_draw2d_scale_x(draw));
  qa_rect left = r;
  left.width = horizontal;
  qa_draw2d_stretch_pixels(draw, left, UV, picture);
  qa_rect right = r;
  right.x = fr(fr(r.x + r.width) - horizontal);
  right.width = horizontal;
  qa_draw2d_stretch_pixels(draw, right, UV, picture);
}

void qa_ui_draw_cg_rect(const qa_draw2d *draw, qa_rect rect, double size,
                        qa_vec4 color, const qa_picture_asset *picture) {
  qa_draw2d_set_color(draw, &color);
  qa_ui_draw_cg_top_bottom(draw, rect, size, picture);
  qa_ui_draw_cg_sides(draw, rect, size, picture);
  qa_draw2d_set_color(draw, NULL);
}
