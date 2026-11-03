/*
 * Port of src/ui/common/menu-theme.ts: the startup-menu type scale,
 * skin, backdrop, and panel commands.
 * GPL-2.0-or-later.
 */
#ifndef QA_UI_COMMON_MENU_THEME_H
#define QA_UI_COMMON_MENU_THEME_H

#include <stdbool.h>

#include "qa/contracts/content.h"
#include "qa/contracts/render.h"
#include "qa/contracts/ui.h"
#include "qa/ui/common/skin.h"

/** TS menuTitleFont. */
extern const qa_resource_id qa_ui_menu_title_font;

/** TS menuText (the frontend type scale). */
typedef struct qa_ui_menu_text_scale {
  double main_title, title, section, label, body, field, small;
} qa_ui_menu_text_scale;
extern const qa_ui_menu_text_scale qa_ui_menu_text;

/** TS menuSkin(font). */
qa_ui_skin qa_ui_menu_skin(qa_resource_id font);
/** TS menuBackdrop(context). */
qa_ui_draw_command qa_ui_menu_backdrop(qa_ui_draw_context context);
/** TS menuPanelRect: the one menu box in 640x480 UI space. */
extern const qa_rect qa_ui_menu_panel_rect;
/* TS menuPanel(context, narrow = false). Named with a _command
 * suffix: the art-manifest frame owns the qa_ui_menu_panel_art name. */
qa_ui_draw_command qa_ui_menu_panel_command(qa_ui_draw_context context,
                                            bool narrow);
/* TS menuPanel(context, narrow, menuScale). */
qa_ui_draw_command qa_ui_menu_panel_scaled(qa_ui_draw_context context,
                                           bool narrow, double menu_scale);

#endif
