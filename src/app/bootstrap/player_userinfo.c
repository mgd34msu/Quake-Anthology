/* Port of src/app/bootstrap/player-userinfo.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/player_userinfo.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
static bool is(const char *a, const char *b) { return a != NULL && strcmp(a, b) == 0; }
static bool dialect_is(qa_cvar_registry *cvars, qa_command_dialect dialect) {
  return qa_cvar_registry_dialect(cvars) == dialect;
}
static bool dialect_q1(qa_cvar_registry *cvars) {
  qa_command_dialect d = qa_cvar_registry_dialect(cvars);
  return d == QA_COMMAND_DIALECT_Q1_NETQUAKE || d == QA_COMMAND_DIALECT_Q1_QUAKEWORLD;
}
static bool dialect_q2(qa_cvar_registry *cvars) {
  qa_command_dialect d = qa_cvar_registry_dialect(cvars);
  return d == QA_COMMAND_DIALECT_Q2_CLASSIC || d == QA_COMMAND_DIALECT_Q2_RERELEASE;
}
static bool find_cvar(qa_cvar_registry *cvars, const char *name, qa_cvar_read *out) {
  return qa_cvar_registry_find(cvars, name, out);
}
static void register_cvar(qa_cvar_registry *cvars, const char *name, const char *value, int32_t flags) {
  qa_cvar_read existing;
  bool exists = find_cvar(cvars, name, &existing);
  int32_t existing_flags = exists ? existing.flags : 0;
  if (exists) qa_cvar_read_free(&existing);
  if (!dialect_q1(cvars) || !exists || qa_cvar_registry_is_console_created(cvars, name)) {
    qa_cvar_snapshot snap;
    if (qa_cvar_registry_register(cvars, name, value, flags, &snap)) qa_cvar_read_free(&snap);
  } else if ((existing_flags & flags) != flags) {
    qa_cvar_registry_add_flags(cvars, name, flags);
  }
}
static void set_if_present(qa_cvar_registry *cvars, const char *name, const char *value) {
  qa_cvar_read read;
  if (!find_cvar(cvars, name, &read)) return;
  qa_cvar_read_free(&read);
  qa_cvar_snapshot snap;
  if (qa_cvar_registry_set(cvars, name, value, true, &snap)) qa_cvar_read_free(&snap);
}
void qa_app_register_player_userinfo(qa_cvar_registry *cvars, double index, const char *model) {
  if (model == NULL) model = "male";
  if (dialect_is(cvars, QA_COMMAND_DIALECT_Q3)) return;
  const int32_t flags = QA_CVAR_FLAG_ARCHIVE | QA_CVAR_FLAG_USER_INFO;
  if (dialect_q1(cvars)) register_cvar(cvars, "qts_weapon_autoswitch", "always", flags);
  if (dialect_is(cvars, QA_COMMAND_DIALECT_Q2_RERELEASE)) register_cvar(cvars, "autoswitch", "0", flags);
  if (dialect_is(cvars, QA_COMMAND_DIALECT_Q1_NETQUAKE)) {
    qa_cvar_read read;
    char fallback[64];
    snprintf(fallback, sizeof fallback, "Player %lld", (long long)(index + 1));
    const char *name_value = fallback;
    char *kept_name = NULL, *kept_color = NULL;
    if (find_cvar(cvars, "name", &read)) { kept_name = read.value; read.value = NULL; qa_cvar_read_free(&read); name_value = kept_name; }
    register_cvar(cvars, "_cl_name", name_value, QA_CVAR_FLAG_ARCHIVE);
    const char *color_value = "0";
    if (find_cvar(cvars, "color", &read)) { kept_color = read.value; read.value = NULL; qa_cvar_read_free(&read); color_value = kept_color; }
    register_cvar(cvars, "_cl_color", color_value, QA_CVAR_FLAG_ARCHIVE);
    free(kept_name); free(kept_color);
    return;
  }
  char player[64];
  snprintf(player, sizeof player, "Player %lld", (long long)(index + 1));
  register_cvar(cvars, "name", player, flags);
  if (dialect_is(cvars, QA_COMMAND_DIALECT_Q1_QUAKEWORLD)) {
    register_cvar(cvars, "topcolor", "0", flags);
    register_cvar(cvars, "bottomcolor", "0", flags);
    register_cvar(cvars, "team", "", flags);
    register_cvar(cvars, "skin", "", flags);
    return;
  }
  register_cvar(cvars, "spectator", "0", QA_CVAR_FLAG_USER_INFO);
  register_cvar(cvars, "password", "", QA_CVAR_FLAG_USER_INFO);
  char skin[256];
  snprintf(skin, sizeof skin, "%s/%s", model, is(model, "female") ? "athena" : is(model, "cyborg") ? "oni911" : "grunt");
  register_cvar(cvars, "skin", skin, flags);
  register_cvar(cvars, "rate", "25000", flags);
  register_cvar(cvars, "msg", "1", flags);
  register_cvar(cvars, "hand", "0", flags);
  register_cvar(cvars, "fov", "90", flags);
  register_cvar(cvars, "gender", is(model, "female") ? "female" : "male", flags);
}
char *qa_app_player_userinfo(qa_cvar_registry *cvars) {
  char *info = qa_cvar_registry_info_string(cvars, QA_CVAR_FLAG_USER_INFO, -1);
  if (info == NULL) qa_range_error("Out of memory");
  if (!dialect_is(cvars, QA_COMMAND_DIALECT_Q1_NETQUAKE)) return info;
  qa_cvar_read read;
  bool has_color = find_cvar(cvars, "color", &read);
  if (has_color) qa_cvar_read_free(&read);
  char *color_text = qa_cvar_registry_variable_string(cvars, has_color ? "color" : "_cl_color");
  int32_t color = qa_native_atoi(color_text != NULL ? color_text : "");
  free(color_text);
  bool has_name = find_cvar(cvars, "name", &read);
  if (has_name) qa_cvar_read_free(&read);
  char *name = qa_cvar_registry_variable_string(cvars, has_name ? "name" : "_cl_name");
  if (name == NULL) qa_range_error("Out of memory");
  if (strlen(name) > 15) name[15] = '\0'; /* TS slice(0,15) counts UTF-16 units */
  /* Strip \name\, \topcolor\, \bottomcolor\ pairs from info. */
  size_t len = strlen(info);
  char *other = malloc(len + 1);
  if (other == NULL) qa_range_error("Out of memory");
  size_t at = 0;
  for (size_t i = 0; i < len;) {
    bool strip = false;
    if (info[i] == '\\') {
      static const char *const keys[] = {"name", "topcolor", "bottomcolor"};
      for (size_t k = 0; k < 3; k++) {
        size_t klen = strlen(keys[k]);
        if (strncmp(info + i + 1, keys[k], klen) == 0 && info[i + 1 + klen] == '\\') {
          size_t j = i + 1 + klen + 1;
          while (j < len && info[j] != '\\') j++;
          i = j; strip = true; break;
        }
      }
    }
    if (!strip) other[at++] = info[i++];
  }
  other[at] = '\0';
  int top = (color >> 4) & 15; if (top > 13) top = 13;
  int bottom = color & 15; if (bottom > 13) bottom = 13;
  size_t size = at + strlen(name) + 64;
  char *out = malloc(size);
  if (out == NULL) qa_range_error("Out of memory");
  snprintf(out, size, "%s\\name\\%s\\topcolor\\%d\\bottomcolor\\%d", other, name, top, bottom);
  free(info); free(name); free(other);
  return out;
}
static double clamp_color(bool has, double value) {
  double v = trunc(has ? value : 0);
  return v < 0 ? 0 : v > 13 ? 13 : v;
}
void qa_app_apply_character_appearance(qa_cvar_registry *cvars, qa_app_character_appearance options) {
  double top = clamp_color(options.has_top_color, options.top_color);
  double bottom = clamp_color(options.has_bottom_color, options.bottom_color);
  if (dialect_is(cvars, QA_COMMAND_DIALECT_Q1_NETQUAKE)) {
    char packed[32];
    snprintf(packed, sizeof packed, "%lld", (long long)(((int)top << 4) | (int)bottom));
    set_if_present(cvars, "_cl_color", packed);
    set_if_present(cvars, "color", packed);
  } else if (dialect_is(cvars, QA_COMMAND_DIALECT_Q1_QUAKEWORLD)) {
    char top_text[32], bottom_text[32];
    snprintf(top_text, sizeof top_text, "%lld", (long long)top);
    snprintf(bottom_text, sizeof bottom_text, "%lld", (long long)bottom);
    set_if_present(cvars, "topcolor", top_text);
    set_if_present(cvars, "bottomcolor", bottom_text);
  }
  if (options.character_skin == NULL) return;
  /* TS trim(): JS whitespace; ASCII whitespace is the practical set. */
  const char *start = options.character_skin;
  while (*start != '\0' && isspace((unsigned char)*start)) start++;
  size_t len = strlen(start);
  while (len > 0 && isspace((unsigned char)start[len - 1])) len--;
  if (len == 0) return;
  char *skin = malloc(len + 1);
  if (skin == NULL) qa_range_error("Out of memory");
  memcpy(skin, start, len); skin[len] = '\0';
  char *model_skin = malloc(strlen(options.character_model) + len + 2);
  if (model_skin == NULL) qa_range_error("Out of memory");
  snprintf(model_skin, strlen(options.character_model) + len + 2, "%s/%s", options.character_model, skin);
  if (dialect_q2(cvars) && is(options.character, "q2")) set_if_present(cvars, "skin", model_skin);
  if (dialect_is(cvars, QA_COMMAND_DIALECT_Q3) && is(options.character, "q3")) {
    static const char *const names[] = {"model", "headmodel", "team_model", "team_headmodel"};
    for (size_t i = 0; i < 4; i++) set_if_present(cvars, names[i], model_skin);
  }
  if (dialect_is(cvars, QA_COMMAND_DIALECT_Q1_QUAKEWORLD) && is(options.character, "q1"))
    set_if_present(cvars, "skin", skin);
  free(skin); free(model_skin);
}
