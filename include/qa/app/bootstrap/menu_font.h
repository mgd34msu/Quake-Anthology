/* Port of src/app/bootstrap/menu-font.ts over the scene image registry, the
 * SceneTextureLoader with mountedImageReader, and the mounted text fonts.
 * Async -> synchronous; a TS throw raises (qa_raise). GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_MENU_FONT_H
#define QA_APP_BOOTSTRAP_MENU_FONT_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/content/catalog/index.h"
#include "qa/content/mounts.h"
#include "qa/contracts/content.h"
#include "qa/render/scene/image_policy.h"
#include "qa/render/scene/resources.h"
#include "qa/text/atlas.h"
/* TS MenuFontOptions. Everything is borrowed for the font's life; image_policy
 * NULL is TS undefined. */
typedef struct qa_app_menu_font_options {
  const qa_content_catalog *catalog;
  qa_content_mounted *mounts;
  qa_game_family family;
  bool rerelease;
  qa_scene_image_registry *images;
  const qa_scene_image_policy *image_policy;
} qa_app_menu_font_options;
typedef struct qa_app_menu_font qa_app_menu_font;
/* TS loadMenuFont(options): { font, close() }. */
qa_app_menu_font *qa_app_load_menu_font(const qa_app_menu_font_options *options);
qa_text_font_selection qa_app_menu_font_selection(const qa_app_menu_font *font);
/* TS close(); also frees the C record. */
void qa_app_menu_font_close(qa_app_menu_font *font);
/* TS MenuTypography { body, title, close() }. */
typedef struct qa_app_menu_typography qa_app_menu_typography;
/* TS loadMenuTypography(catalog, images, classic, imagePolicy?). classic is borrowed. */
qa_app_menu_typography *qa_app_load_menu_typography(qa_content_catalog *catalog, qa_scene_image_registry *images,
    const qa_text_atlas *classic, const qa_scene_image_policy *image_policy);
qa_text_font_selection qa_app_menu_typography_body(const qa_app_menu_typography *typography);
qa_text_font_selection qa_app_menu_typography_title(const qa_app_menu_typography *typography);
/* TS close(); also frees the C record. */
void qa_app_menu_typography_close(qa_app_menu_typography *typography);
#endif
