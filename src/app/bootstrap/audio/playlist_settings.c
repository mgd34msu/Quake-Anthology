/* Port of src/app/bootstrap/audio/playlist-settings.ts.
 * GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "qa/app/bootstrap/audio/playlist_settings.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
static void reg_ignored(qa_cvar_registry *cvars, const char *name, const char *value, double flags) {
  qa_cvar_snapshot ignored;
  if (qa_cvar_registry_register(cvars, name, value, flags, &ignored)) qa_cvar_read_free(&ignored);
}
static bool all_digits(const char *v) {
  if (*v == '\0') return false;
  for (; *v != '\0'; v++) if (!isdigit((unsigned char)*v)) return false;
  return true;
}
static bool ends_with_ci(const char *v, const char *suffix) {
  size_t a = strlen(v), b = strlen(suffix);
  if (a < b) return false;
  for (size_t i = 0; i < b; i++)
    if (tolower((unsigned char)v[a - b + i]) != tolower((unsigned char)suffix[i])) return false;
  return true;
}
/* TS normalizeResourcePath throw conditions (the landed
 * qa_normalize_resource_path aborts, so the catch is inlined). */
static bool resource_path_valid(const char *value) {
  if (value[0] == '\0') return false;
  if (isalpha((unsigned char)value[0]) && value[1] == ':') return false;
  const char *part = value;
  for (;;) {
    const char *slash = strchr(part, '/');
    size_t len = slash != NULL ? (size_t)(slash - part) : strlen(part);
    if (len == 0) return false;
    if (len == 1 && part[0] == '.') return false;
    if (len == 2 && part[0] == '.' && part[1] == '.') return false;
    if (slash == NULL) return true;
    part = slash + 1;
  }
}
bool qa_app_valid_menu_track(const char *value) {
  if (strcmp(value, "auto") == 0 || strcmp(value, "0") == 0) return true;
  if (all_digits(value)) { double n = strtod(value, NULL); return n >= 1 && n <= 255; }
  size_t len = strlen(value);
  if (len > 255) return false;
  /* TS value.trim() !== value (JS trim: unicode space; bytes here). */
  if (len > 0 && (isspace((unsigned char)value[0]) || isspace((unsigned char)value[len - 1]))) return false;
  for (size_t i = 0; i < len; i++) {
    unsigned char c = (unsigned char)value[i];
    if (c == '"' || c <= 0x1f || c == 0x7f || c == '\\') return false;
  }
  if (!resource_path_valid(value)) return false;
  const char *slash = strrchr(value, '/');
  const char *name = slash != NULL ? slash + 1 : value;
  return strchr(name, '.') == NULL || ends_with_ci(name, ".ogg") || ends_with_ci(name, ".wav");
}
static char *validate_shuffle(void *ctx, const char *value) {
  (void)ctx;
  return strcmp(value, "0") == 0 || strcmp(value, "1") == 0 ? NULL : strdup("Use 0 or 1");
}
static char *validate_menu(void *ctx, const char *value) {
  (void)ctx;
  return qa_app_valid_menu_track(value) ? NULL
    : strdup("Use auto, 0, a track number 1..255, or a mounted OGG/WAV music path");
}
static void changed_noop(void *ctx, const char *value) { (void)ctx; (void)value; }
static const char *const shuffle_examples[] = {"music_shuffle 1"};
static const char *const menu_examples[] = {"music_menu_track auto", "music_menu_track 77", "music_menu_track 0"};
void qa_app_register_music_settings(qa_cvar_registry *cvars) {
  reg_ignored(cvars, "music_shuffle", "0", QA_CVAR_FLAG_ARCHIVE);
  reg_ignored(cvars, "music_menu_track", "auto", QA_CVAR_FLAG_ARCHIVE);
  qa_cvar_registry_bind_value(cvars, "music_shuffle",
      (qa_cvar_value_binding){NULL, validate_shuffle, changed_noop});
  qa_cvar_registry_bind_value(cvars, "music_menu_track",
      (qa_cvar_value_binding){NULL, validate_menu, changed_noop});
  qa_command_documentation shuffle = {
    "Shuffle mounted Quake II gameplay music at track end. Menu music remains fixed.",
    "music_shuffle <0|1>", shuffle_examples, 1, NULL, 0};
  qa_command_documentation menu = {
    "Menu music: auto uses Anthology's title/family fallback; 0 disables; numbers and paths select an explicit mounted track. Auto is an Anthology extension.",
    "music_menu_track <auto|0|1..255|path>", menu_examples, 3, NULL, 0};
  qa_cvar_registry_document(cvars, "music_shuffle", shuffle);
  qa_cvar_registry_document(cvars, "music_menu_track", menu);
  qa_cvar_registry_register_alias(cvars, (qa_cvar_alias){"ogg_shuffle", "music_shuffle", shuffle,
      (qa_cvar_alias_conversion){NULL, true, NULL, NULL}});
  qa_cvar_registry_register_alias(cvars, (qa_cvar_alias){"ogg_menu_track", "music_menu_track", menu,
      (qa_cvar_alias_conversion){NULL, true, NULL, NULL}});
}
qa_app_audio_music_preferences qa_app_read_music_settings(qa_cvar_registry *cvars) {
  qa_app_audio_music_preferences out;
  out.music_shuffle = (cvars != NULL ? qa_cvar_registry_variable_value(cvars, "music_shuffle") : 0) != 0;
  out.menu_track = NULL;
  if (cvars != NULL) {
    qa_cvar_read read;
    if (qa_cvar_registry_find(cvars, "music_menu_track", &read)) {
      out.menu_track = strdup(read.value != NULL ? read.value : "");
      qa_cvar_read_free(&read);
    }
  }
  if (out.menu_track == NULL) out.menu_track = strdup("auto");
  if (out.menu_track == NULL) qa_range_error("Out of memory");
  return out;
}
