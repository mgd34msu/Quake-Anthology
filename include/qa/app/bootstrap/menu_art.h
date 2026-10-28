/* Port of src/app/bootstrap/menu-art.ts. Bun-embedded PNG files
 * become a host byte source keyed by the same four asset paths;
 * decoded images are cached (TS module-level Map). Async becomes
 * synchronous. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_MENU_ART_H
#define QA_APP_BOOTSTRAP_MENU_ART_H
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/render.h"
/* Embedded-file host: returns a borrowed byte view for one of the
 * four TS asset identifiers, or NULL bytes when absent. */
typedef struct qa_app_menu_art_files {
  void *ctx;
  const uint8_t *(*bytes)(void *ctx, const char *asset, size_t *out_length);
} qa_app_menu_art_files;
void qa_app_menu_art_use_files(qa_app_menu_art_files files);
/* Fills out with a copy of the decoded pixels (malloc'd, caller
 * frees pixels.bytes). Aborts on an unknown path (TS Error). */
void qa_app_load_menu_art_image(const char *path, qa_image_level *out);
#endif
