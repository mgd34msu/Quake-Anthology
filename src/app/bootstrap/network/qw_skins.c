/* Port of src/app/bootstrap/network/qw-skins.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/network/qw_skins.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/content/mounts.h"
#include "qa/formats/images/indexed.h"
typedef struct cache_entry {
  char *key; size_t key_length; bool has_skin; qa_indexed_model_skin skin; char *name; uint8_t *pixels;
} cache_entry;
struct qa_app_qw_player_skins {
  qa_app_qw_skin_options options;
  cache_entry *entries; size_t count, capacity;
};
static bool skin_name_charset_ok(const char *value) {
  if (!*value) return false;
  for (const char *p = value; *p; p++) {
    char c = *p;
    bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
        || c == '_' || c == '+' || c == '.' || c == '-';
    if (!ok) return false;
  }
  return true;
}
/* Malloc'd TS skinName() result. */
static char *skin_name(const char *value) {
  char *base = malloc(5);
  if (!base) return NULL;
  memcpy(base, "base", 5);
  if (strstr(value, "..") || value[0] == '.' || !skin_name_charset_ok(value)) return base;
  const char *dot = strrchr(value, '.');
  size_t length = dot ? (size_t)(dot - value) : strlen(value);
  if (length > 15) length = 15;
  if (length == 0) return base;
  free(base);
  char *out = malloc(length + 1);
  if (!out) return NULL;
  memcpy(out, value, length); out[length] = '\0';
  return out;
}
qa_app_qw_player_skins *qa_app_qw_player_skins_create(qa_app_qw_skin_options options) {
  qa_app_qw_player_skins *s = calloc(1, sizeof *s);
  if (!s) return NULL;
  s->options = options;
  return s;
}
void qa_app_qw_player_skins_clear(qa_app_qw_player_skins *skins) {
  for (size_t i = 0; i < skins->count; i++) {
    free(skins->entries[i].key); free(skins->entries[i].name); free(skins->entries[i].pixels);
  }
  skins->count = 0;
}
void qa_app_qw_player_skins_destroy(qa_app_qw_player_skins *skins) {
  if (!skins) return;
  qa_app_qw_player_skins_clear(skins);
  free(skins->entries); free(skins);
}
char *qa_app_qw_player_skins_name(qa_app_qw_player_skins *skins, const char *userinfo_skin) {
  const char *all = skins->options.allskins(skins->options.ctx);
  const char *chosen = all && all[0] ? all
      : userinfo_skin && userinfo_skin[0] ? userinfo_skin
      : skins->options.baseskin(skins->options.ctx);
  return skin_name(chosen);
}
static bool load_skin(qa_app_qw_player_skins *skins, const char *selected, const char *base,
    cache_entry *entry) {
  char path[256];
  snprintf(path, sizeof path, "skins/%s.pcx", selected);
  uint8_t *bytes = NULL; size_t length = 0;
  if (!skins->options.read(skins->options.ctx, path, &bytes, &length)
      && strcmp(selected, base) != 0) {
    snprintf(path, sizeof path, "skins/%s.pcx", base);
    skins->options.read(skins->options.ctx, path, &bytes, &length);
  }
  if (!bytes) return false;
  bool ok = false;
  if (length >= 128) {
    unsigned xmax = bytes[8] | (bytes[9] << 8);
    unsigned ymax = bytes[10] | (bytes[11] << 8);
    if (xmax < 320 && ymax < 200) {
      qa_pcx_image decoded;
      if (qa_decode_pcx(bytes, length, path, &decoded)) {
        uint8_t *padded = calloc(320 * 200, 1);
        uint8_t *pixels = calloc(296 * 194, 1);
        char *digest = qa_digest_bytes(bytes, length);
        if (padded && pixels && digest) {
          for (int64_t row = 0; row < decoded.height && row < 200; row++) {
            size_t width = decoded.width > 320 ? 320 : (size_t)decoded.width;
            memcpy(padded + row * 320, decoded.indices + row * decoded.width, width);
          }
          for (int row = 0; row < 194; row++)
            memcpy(pixels + row * 296, padded + row * 320, 296);
          const char *hex = strncmp(digest, "sha256:", 7) == 0 ? digest + 7 : digest;
          size_t name_length = strlen(hex) + 48;
          entry->name = malloc(name_length);
          if (entry->name) {
            snprintf(entry->name, name_length, "qw-skin:%s:crop:0,0,296,194:stride320", hex);
            entry->pixels = pixels; pixels = NULL;
            entry->skin.name = entry->name; entry->skin.width = 296; entry->skin.height = 194;
            entry->skin.pixels.bytes = entry->pixels; entry->skin.pixels.length = 296 * 194;
            entry->has_skin = true; ok = true;
          }
        }
        free(padded); free(pixels); free(digest);
        qa_pcx_image_destroy(&decoded);
      }
    }
  }
  free(bytes);
  return ok;
}
const qa_indexed_model_skin *qa_app_qw_player_skins_select(
    qa_app_qw_player_skins *skins, const char *userinfo_skin) {
  if (skins->options.noskins(skins->options.ctx) == 1) return NULL;
  char *base = skin_name(skins->options.baseskin(skins->options.ctx));
  char *selected = qa_app_qw_player_skins_name(skins, userinfo_skin);
  if (!base || !selected) { free(base); free(selected); return NULL; }
  size_t key_length = strlen(selected) + strlen(base) + 2;
  char *key = malloc(key_length);
  if (!key) { free(base); free(selected); return NULL; }
  snprintf(key, key_length, "%s%c%s", selected, '\0', base);
  /* strcmp stops at the embedded NUL exactly like the key build; use
   * full-length compare instead. */
  for (size_t i = 0; i < skins->count; i++)
    if (skins->entries[i].key_length == key_length && memcmp(skins->entries[i].key, key, key_length) == 0) {
      const qa_indexed_model_skin *hit = skins->entries[i].has_skin ? &skins->entries[i].skin : NULL;
      free(base); free(selected); free(key);
      return hit;
    }
  if (skins->count == 128) qa_app_qw_player_skins_clear(skins);
  if (skins->count == skins->capacity) {
    size_t next_cap = skins->capacity ? skins->capacity * 2 : 16;
    cache_entry *next = realloc(skins->entries, next_cap * sizeof *next);
    if (!next) { free(base); free(selected); free(key); return NULL; }
    skins->entries = next; skins->capacity = next_cap;
  }
  cache_entry *entry = &skins->entries[skins->count];
  memset(entry, 0, sizeof *entry);
  entry->key = key; entry->key_length = key_length;
  load_skin(skins, selected, base, entry);
  skins->count++;
  free(base); free(selected);
  return entry->has_skin ? &entry->skin : NULL;
}
