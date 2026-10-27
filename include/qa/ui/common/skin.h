/*
 * Port of src/ui/common/skin.ts: UiSkin shape, the default skin, font
 * normalization, and nine-slice image expansion.
 * C accommodations: nullable slices are pointers (borrowed);
 * optional fields carry has_ flags. capInk is has_cap_ink + top/height.
 * GPL-2.0-or-later.
 */
#ifndef QA_UI_COMMON_SKIN_H
#define QA_UI_COMMON_SKIN_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/content.h"
#include "qa/contracts/math.h"
#include "qa/contracts/render.h"
#include "qa/contracts/ui.h"
#include "qa/text/atlas.h"

/** TS UiImageSlice. */
typedef struct qa_ui_image_slice {
  qa_resource_id resource;
  double width, height;
  qa_vec2 uv[2];
  double border_left, border_top, border_right, border_bottom;
  bool has_border_scale;
  double border_scale;
} qa_ui_image_slice;

/** TS UiSkin.colors. */
typedef struct qa_ui_skin_colors {
  qa_vec4 text, disabled, accent, panel, control, focused;
} qa_ui_skin_colors;

/** TS UiSkin. */
typedef struct qa_ui_skin {
  qa_resource_id font;
  bool has_title_font;
  qa_resource_id title_font;
  bool has_title_scale;
  double title_scale;
  double font_scale;
  bool has_cap_ink;
  double cap_ink_top, cap_ink_height;
  double line_height;
  const qa_ui_image_slice *panel;  /* nullable, borrowed */
  const qa_ui_image_slice *button; /* nullable, borrowed */
  const qa_ui_image_slice *focus;  /* nullable, borrowed */
  qa_resource_id background;       /* nullable */
  qa_ui_skin_colors colors;
} qa_ui_skin;

/** TS defaultUiSkin(font). */
qa_ui_skin qa_ui_default_skin(qa_resource_id font);
/* TS uiSkinFont(skin, font, lineHeight = 14). Aborts (RangeError)
 * on a non-positive font or requested line height. */
qa_ui_skin qa_ui_skin_font(qa_ui_skin skin, qa_text_font_selection font,
                           double line_height);
/** TS hudSkinFont(skin, font). */
qa_ui_skin qa_ui_hud_skin_font(qa_ui_skin skin, qa_text_font_selection font);
/* TS nineSlice: writes up to 9 image commands into out (capacity
 * out_capacity) and returns the count. Aborts (RangeError) on an
 * invalid slice, exactly as the TS throws. */
size_t qa_ui_nine_slice(const qa_ui_image_slice *slice, qa_rect rect,
                        qa_vec4 color, qa_ui_draw_command *out,
                        size_t out_capacity);

#endif
