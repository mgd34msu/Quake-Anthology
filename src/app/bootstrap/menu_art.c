/* Port of src/app/bootstrap/menu-art.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/menu_art.h"
#include <stdlib.h>
#include <string.h>
#include "qa/formats/images/png.h"
#include "qa/foundation/support.h"
typedef struct cached_image {
  const char *asset;
  qa_image_level level;
  uint8_t *pixels;
} cached_image;
static qa_app_menu_art_files host_files;
static cached_image cache[4];
static size_t cache_count;
static const char *const assets[] = {
    "assets/ui/main-menu-background.png", "assets/ui/menu-background.png",
    "assets/ui/menu-panel.png", "assets/ui/menu-focus.png"};
void qa_app_menu_art_use_files(qa_app_menu_art_files files) { host_files = files; }
void qa_app_load_menu_art_image(const char *path, qa_image_level *out) {
  const char *asset = NULL;
  for (size_t i = 0; i < 4; i++) if (strcmp(path, assets[i]) == 0) asset = assets[i];
  if (asset == NULL) qa_range_error("Unknown menu artwork: %s", path);
  cached_image *entry = NULL;
  for (size_t i = 0; i < cache_count; i++) if (cache[i].asset == asset) entry = &cache[i];
  if (entry == NULL) {
    size_t length = 0;
    const uint8_t *bytes = host_files.bytes(host_files.ctx, asset, &length);
    qa_png_image decoded = {0};
    if (bytes == NULL || qa_decode_png(bytes, length, path, &decoded) != 0)
      qa_range_error("Menu artwork could not be decoded: %s", path);
    entry = &cache[cache_count++];
    entry->asset = asset;
    entry->level = decoded.level;
    entry->pixels = malloc(decoded.level.pixels.length == 0 ? 1 : decoded.level.pixels.length);
    if (entry->pixels == NULL) qa_range_error("Out of memory");
    memcpy(entry->pixels, decoded.level.pixels.bytes, decoded.level.pixels.length);
    entry->level.pixels.bytes = entry->pixels;
    entry->level.pixels.length = decoded.level.pixels.length;
    qa_png_image_destroy(&decoded);
  }
  out->width = entry->level.width;
  out->height = entry->level.height;
  out->pixels.length = entry->level.pixels.length;
  out->pixels.bytes = malloc(entry->level.pixels.length == 0 ? 1 : entry->level.pixels.length);
  if (out->pixels.bytes == NULL) qa_range_error("Out of memory");
  memcpy((void *)out->pixels.bytes, entry->level.pixels.bytes, entry->level.pixels.length);
}
