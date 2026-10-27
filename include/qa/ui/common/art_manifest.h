/*
 * Port of src/ui/common/art-manifest.ts: installed menu art manifest.
 * GPL-2.0-or-later.
 */
#ifndef QA_UI_COMMON_ART_MANIFEST_H
#define QA_UI_COMMON_ART_MANIFEST_H

#include <stddef.h>

#include "qa/contracts/math.h"

/** TS MenuArtFile. */
typedef struct qa_menu_art_file {
  const char *file;
  double width, height;
  const char *sha256;
} qa_menu_art_file;

/** TS MenuArtFrame (MenuArtFile plus region, border, borderScale). */
typedef struct qa_menu_art_frame {
  qa_menu_art_file file;
  double region_width, region_height;
  qa_vec2 region_uv[2];
  double border_left, border_top, border_right, border_bottom;
  double border_scale;
} qa_menu_art_frame;

/** TS menuBackground. */
extern const qa_menu_art_file qa_ui_menu_background;
/** TS menuPanel. */
extern const qa_menu_art_frame qa_ui_menu_panel_art;
/** TS menuFocus. */
extern const qa_menu_art_frame qa_ui_menu_focus_art;
/** TS mainMenuBackground. */
extern const qa_menu_art_file qa_ui_main_menu_background;
/** TS menuArtFiles: [menuBackground, menuPanel, menuFocus,
 * mainMenuBackground] projected to their MenuArtFile fields. */
extern const qa_menu_art_file qa_ui_menu_art_files[4];
#define QA_UI_MENU_ART_FILE_COUNT 4

#endif
