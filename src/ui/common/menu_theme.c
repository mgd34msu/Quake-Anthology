/* Port of src/ui/common/menu-theme.ts. GPL-2.0-or-later. */
#include "qa/ui/common/menu_theme.h"

#include <string.h>

#include "qa/ui/common/art_manifest.h"
#include "qa/ui/common/layout.h"

const qa_resource_id qa_ui_menu_title_font = "resource:engine-menu:title";

/* Body matches the controller skin so custom-drawn text and control
 * text render at the same size; dense label/value pages use field. */
const qa_ui_menu_text_scale qa_ui_menu_text = {
  5.4, 3.6, 1.55, 1.65, 2.3, 1.9, 1.35
};

const qa_rect qa_ui_menu_panel_rect = { 32, 24, 576, 432 };

static qa_vec4 v4(double x, double y, double z, double w) {
  qa_vec4 out = { x, y, z, w };
  return out;
}

qa_ui_skin qa_ui_menu_skin(qa_resource_id font) {
  qa_ui_skin skin;
  memset(&skin, 0, sizeof skin);
  skin.font = font;
  skin.has_title_font = true;
  skin.title_font = qa_ui_menu_title_font;
  skin.has_title_scale = true;
  skin.title_scale = qa_ui_menu_text.title;
  skin.font_scale = qa_ui_menu_text.body;
  skin.line_height = 24;
  skin.colors.text = v4(0.9, 0.92, 0.94, 1);
  skin.colors.disabled = v4(0.48, 0.5, 0.53, 1);
  skin.colors.accent = v4(1, 0.73, 0.35, 1);
  skin.colors.panel = v4(0, 0, 0, 0);
  skin.colors.control = v4(0.06, 0.08, 0.1, 0.83);
  skin.colors.focused = v4(0.23, 0.16, 0.09, 0.97);
  return skin;
}

qa_ui_draw_command qa_ui_menu_backdrop(qa_ui_draw_context context) {
  qa_rect viewport = context.binding.viewport;
  double ratio = viewport.width / viewport.height, art_ratio = qa_ui_main_menu_background.width / qa_ui_main_menu_background.height;
  double crop_x = ratio < art_ratio ? (1 - ratio / art_ratio) / 2 : 0;
  double crop_y = ratio > art_ratio ? (1 - art_ratio / ratio) / 2 : 0;
  qa_ui_draw_command cmd;
  memset(&cmd, 0, sizeof cmd);
  cmd.kind = QA_UI_DRAW_IMAGE;
  cmd.resource = "resource:engine-menu:main-background";
  cmd.rect = viewport;
  cmd.tex_coords[0].x = crop_x; cmd.tex_coords[0].y = crop_y;
  cmd.tex_coords[1].x = 1 - crop_x; cmd.tex_coords[1].y = 1 - crop_y;
  cmd.color = v4(1, 1, 1, 1);
  return cmd;
}

qa_ui_draw_command qa_ui_menu_panel_scaled(qa_ui_draw_context context,
                                           bool narrow, double menu_scale) {
  qa_rect area = context.binding.safe_area;
  qa_ui_draw_command cmd;
  memset(&cmd, 0, sizeof cmd);
  cmd.kind = QA_UI_DRAW_FILL;
  if (area.width <= 0 || area.height <= 0) {
    cmd.rect.x = area.x; cmd.rect.y = area.y; /* zero size, transparent */
    return cmd;
  }
  cmd.rect.x = 32; cmd.rect.y = 24;
  cmd.rect.width = narrow ? 272 : 576;
  cmd.rect.height = 432;
  cmd.color = v4(0.025, 0.035, 0.045, narrow ? 0.86 : 0.96);
  return qa_ui_transform_command(cmd, qa_ui_fit(area, menu_scale));
}

qa_ui_draw_command qa_ui_menu_panel_command(qa_ui_draw_context context,
                                            bool narrow) {
  return qa_ui_menu_panel_scaled(context, narrow, 1);
}
