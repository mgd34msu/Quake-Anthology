/* Port of src/app/bootstrap/menu-font.ts. Mounted text fonts
 * (text/mounted.ts) and the scene texture loader are landed; this
 * file owns the family branching and validation over a host that
 * supplies the image registry, mounted-content adapter and texture
 * loads. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_MENU_FONT_H
#define QA_APP_BOOTSTRAP_MENU_FONT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/content/catalog/index.h"
#include "qa/content/mounts.h"
#include "qa/contracts/content.h"
#include "qa/contracts/render.h"
#include "qa/text/atlas.h"
#include "qa/text/mounted.h"
typedef struct qa_app_menu_font_host {
  void *ctx;
  qa_mounted_scene_image_registry images;
  /* Adapter from landed mounts to text/mounted.ts's surface. */
  qa_mounted_content (*mounted_content)(void *ctx, qa_content_mounted *mounts);
  /* SceneTextureLoader.load via mountedImageReader; false = TS null.
   * name_out/image_out borrowed from the loaded texture. */
  bool (*load_texture)(void *ctx, const qa_content_catalog *catalog, qa_content_mounted *mounts,
      const char *path, qa_game_family family, const qa_palette *palette,
      qa_render_image *content_out, const char **name_out, qa_renderer_image *image_out);
  void (*dispose_textures)(void *ctx);
  /* catalog.mountsFor + openMountPlan for a product: owned mounts. */
  qa_content_mounted *(*mounts_for)(void *ctx, const qa_content_catalog *catalog, const char *product_id);
} qa_app_menu_font_host;
/* TS MenuFontOptions (images/imagePolicy ride in the host). */
typedef struct qa_app_menu_font_options {
  const qa_content_catalog *catalog;
  qa_content_mounted *mounts;
  qa_game_family family;
  bool rerelease;
} qa_app_menu_font_options;
typedef struct qa_app_menu_font qa_app_menu_font;
/* Aborts with the TS Error text on missing charset assets. */
qa_app_menu_font *qa_app_load_menu_font(const qa_app_menu_font_options *options,
    const qa_app_menu_font_host *host);
qa_text_font_selection qa_app_menu_font_selection(const qa_app_menu_font *font);
void qa_app_menu_font_close(qa_app_menu_font *font);
/* TS MenuTypography. classic is borrowed. */
typedef struct qa_app_menu_typography qa_app_menu_typography;
qa_app_menu_typography *qa_app_load_menu_typography(const qa_content_catalog *catalog,
    const qa_text_atlas *classic, const qa_app_menu_font_host *host);
qa_text_font_selection qa_app_menu_typography_body(const qa_app_menu_typography *typography);
qa_text_font_selection qa_app_menu_typography_title(const qa_app_menu_typography *typography);
void qa_app_menu_typography_close(qa_app_menu_typography *typography);
#endif
