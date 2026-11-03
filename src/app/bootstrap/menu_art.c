/* Port of src/app/bootstrap/menu-art.ts. GPL-2.0-or-later.
 *
 * Bun embeds the five artwork files in the standalone executable. The C binary
 * reads them from the host's bytes hook when one is installed, otherwise from
 * assets/ui/ beside the executable, under the working directory, or in the
 * sibling TypeScript checkout (the same candidates src/main.c uses for the
 * splash title). TS shares one in-flight decode between concurrent loads and
 * drops it once resolved; C decodes synchronously, so nothing is retained. */
#include "qa/app/bootstrap/menu_art.h"
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "qa/formats/images/png.h"
#include "qa/foundation/support.h"
static qa_app_menu_art_files host_files;
static const char *const assets[] = {
    "assets/ui/main-menu-background.png", "assets/ui/menu-background.png",
    "assets/ui/menu-panel.png", "assets/ui/menu-focus.png", "assets/ui/title.png"};
void qa_app_menu_art_use_files(qa_app_menu_art_files files) { host_files = files; }
static uint8_t *read_file(const char *path, size_t *out_length) {
  FILE *file = fopen(path, "rb");
  if (file == NULL) return NULL;
  uint8_t *bytes = NULL;
  long size = fseek(file, 0, SEEK_END) == 0 ? ftell(file) : -1;
  if (size >= 0 && fseek(file, 0, SEEK_SET) == 0) {
    bytes = malloc(size == 0 ? 1 : (size_t)size);
    if (bytes != NULL && fread(bytes, 1, (size_t)size, file) != (size_t)size) { free(bytes); bytes = NULL; }
    if (bytes != NULL) *out_length = (size_t)size;
  }
  (void)fclose(file);
  return bytes;
}
/* The embedded file for `asset`; NULL when no candidate location has it. Caller frees. */
static uint8_t *default_bytes(const char *asset, size_t *out_length) {
  char path[PATH_MAX + 64];
  char exe[PATH_MAX];
  ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
  if (n > 0) {
    exe[n] = '\0';
    char *slash = strrchr(exe, '/');
    if (slash != NULL) {
      *slash = '\0';
      snprintf(path, sizeof path, "%s/%s", exe, asset);
      uint8_t *bytes = read_file(path, out_length);
      if (bytes != NULL) return bytes;
    }
  }
  uint8_t *bytes = read_file(asset, out_length);
  if (bytes != NULL) return bytes;
  snprintf(path, sizeof path, "../quake-anthology-ts/%s", asset);
  return read_file(path, out_length);
}
void qa_app_load_menu_art_image(const char *path, qa_image_level *out) {
  const char *asset = NULL;
  for (size_t i = 0; i < sizeof assets / sizeof assets[0]; i++) if (strcmp(path, assets[i]) == 0) asset = assets[i];
  if (asset == NULL) qa_raise("Error", 0, 0, "Unknown menu artwork: %s", path);
  size_t length = 0;
  uint8_t *owned = NULL;
  const uint8_t *bytes = host_files.bytes != NULL ? host_files.bytes(host_files.ctx, asset, &length)
                                                  : (owned = default_bytes(asset, &length));
  if (bytes == NULL) qa_raise("Error", 0, 0, "ENOENT: no such file or directory, open '%s'", asset);
  qa_cleanup bytes_guard;
  qa_cleanup_push(&bytes_guard, free, owned);
  qa_png_image decoded = {0};
  bool decoded_ok = qa_decode_png(bytes, length, path, &decoded) != 0; /* decode errors raise; 0 is OOM */
  qa_cleanup_pop(&bytes_guard);
  free(owned);
  if (!decoded_ok) qa_range_error("Out of memory");
  /* TS returns pixels.slice(): the caller owns its copy. */
  out->width = decoded.level.width;
  out->height = decoded.level.height;
  out->pixels.length = decoded.level.pixels.length;
  out->pixels.bytes = malloc(decoded.level.pixels.length == 0 ? 1 : decoded.level.pixels.length);
  if (out->pixels.bytes == NULL) qa_range_error("Out of memory");
  memcpy((void *)out->pixels.bytes, decoded.level.pixels.bytes, decoded.level.pixels.length);
  qa_png_image_destroy(&decoded);
}
