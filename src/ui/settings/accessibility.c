/* Port of src/ui/settings/accessibility.ts. GPL-2.0-or-later. */
#include "qa/ui/settings/accessibility.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/input/support.h"
#include "qa/ui/settings/language.h"

const qa_ui_preference_values qa_ui_default_preferences = {
  1, 1, 1, false, false, true, true, 8, "standard", "standard"
};

/* Range keys in TS order; the index is the validate ctx. */
static const char *const RANGE_KEYS[4] = {
  "hudScale", "textScale", "menuScale", "crosshairSize"
};
static const double RANGES[4][2] = {
  { 0.5, 1.5 }, { 0.75, 2 }, { 0.75, 1 }, { 2, 32 }
};
static const char *const TOGGLE_KEYS[4] = {
  "highContrast", "reducedFlashes", "captions", "crosshair"
};

static void seat_name(int64_t seat, const char *key, char *out, size_t size) {
  snprintf(out, size, "ui_seat%lld_%s", (long long)(seat + 1), key);
}
static double default_range_value(size_t index) {
  switch (index) {
    case 0: return qa_ui_default_preferences.hud_scale;
    case 1: return qa_ui_default_preferences.text_scale;
    case 2: return qa_ui_default_preferences.menu_scale;
    default: return qa_ui_default_preferences.crosshair_size;
  }
}
static bool default_toggle_value(size_t index) {
  switch (index) {
    case 0: return qa_ui_default_preferences.high_contrast;
    case 1: return qa_ui_default_preferences.reduced_flashes;
    case 2: return qa_ui_default_preferences.captions;
    default: return qa_ui_default_preferences.crosshair;
  }
}
static void register_one(qa_cvar_registry *cvars, const char *key,
                         const char *value) {
  qa_cvar_snapshot out;
  if (qa_cvar_registry_register(cvars, key, value, QA_CVAR_FLAG_ARCHIVE,
                                &out))
    qa_cvar_read_free(&out);
}
static char *validate_range(void *ctx, const char *value) {
  size_t index = (size_t)(uintptr_t)ctx;
  double low = RANGES[index][0], high = RANGES[index][1];
  double number = qa_input_js_parse_number(value);
  if (isfinite(number) && number >= low && number <= high) return NULL;
  char *low_text = qa_input_js_number(low);
  char *high_text = qa_input_js_number(high);
  char buffer[96];
  snprintf(buffer, sizeof buffer, "Expected %s through %s",
           low_text != NULL ? low_text : "", high_text != NULL ? high_text : "");
  free(low_text);
  free(high_text);
  return qa_input_dup(buffer);
}
static char *validate_toggle(void *ctx, const char *value) {
  (void)ctx;
  return strcmp(value, "0") == 0 || strcmp(value, "1") == 0
             ? NULL
             : qa_input_dup("Expected 0 or 1");
}
static char *validate_choice(void *ctx, const char *value) {
  bool typeface = ctx != NULL;
  if (typeface) {
    if (strcmp(value, "standard") == 0 || strcmp(value, "bold") == 0)
      return NULL;
    return qa_input_dup("Expected standard, bold");
  }
  if (strcmp(value, "standard") == 0 || strcmp(value, "blue-yellow") == 0
      || strcmp(value, "monochrome") == 0)
    return NULL;
  return qa_input_dup("Expected standard, blue-yellow, monochrome");
}

void qa_ui_register_accessibility_settings(qa_cvar_registry *cvars) {
  qa_ui_register_language_settings(cvars);
  for (int64_t seat = 0; seat < 4; seat++) {
    char key[72];
    for (size_t i = 0; i < 4; i++) {
      seat_name(seat, RANGE_KEYS[i], key, sizeof key);
      char *def = qa_input_js_number(default_range_value(i));
      if (def == NULL) qa_range_error("Out of memory");
      register_one(cvars, key, def);
      free(def);
      qa_cvar_value_binding binding = {
        (void *)(uintptr_t)i, validate_range, NULL
      };
      qa_cvar_registry_bind_value(cvars, key, binding);
    }
    for (size_t i = 0; i < 4; i++) {
      seat_name(seat, TOGGLE_KEYS[i], key, sizeof key);
      register_one(cvars, key, default_toggle_value(i) ? "1" : "0");
      qa_cvar_value_binding binding = { NULL, validate_toggle, NULL };
      qa_cvar_registry_bind_value(cvars, key, binding);
    }
    static const char *const choice_keys[2] = { "typeface", "colorMode" };
    for (size_t i = 0; i < 2; i++) {
      seat_name(seat, choice_keys[i], key, sizeof key);
      register_one(cvars, key, i == 0 ? qa_ui_default_preferences.typeface
                                      : qa_ui_default_preferences.color_mode);
      qa_cvar_value_binding binding = {
        i == 0 ? (void *)"typeface" : NULL, validate_choice, NULL
      };
      qa_cvar_registry_bind_value(cvars, key, binding);
    }
  }
  char target[72];
  seat_name(0, "crosshair", target, sizeof target);
  static char target_copy[72];
  snprintf(target_copy, sizeof target_copy, "%s", target);
  static const char *const examples[2] = { "crosshair 1", "crosshair 0" };
  qa_cvar_alias alias;
  memset(&alias, 0, sizeof alias);
  alias.name = "crosshair";
  alias.target = target_copy;
  alias.documentation.summary =
    "Show the first seat's crosshair. Alias of ui_seat1_crosshair and "
    "the Crosshair accessibility preference.";
  alias.documentation.usage = "crosshair <0|1>";
  alias.documentation.examples = examples;
  alias.documentation.example_count = 2;
  alias.conversion.is_identity = true;
  qa_cvar_registry_register_alias(cvars, alias);
}

/* Reads with the TS fallback: the registered value, else the default
 * rendered as "0"/"1" for booleans and String(number) otherwise. */
static void read_value(qa_cvar_registry *cvars, int64_t seat, const char *key,
                       const char *fallback, char *out, size_t size) {
  char name[72];
  seat_name(seat, key, name, sizeof name);
  qa_cvar_read read;
  if (qa_cvar_registry_find(cvars, name, &read)) {
    snprintf(out, size, "%s", read.value);
    qa_cvar_read_free(&read);
  } else {
    snprintf(out, size, "%s", fallback);
  }
}

qa_ui_preference_values qa_ui_read_preferences(qa_cvar_registry *cvars,
                                               int64_t seat) {
  char buffer[256];
  qa_ui_preference_values out;
  char *def;
  def = qa_input_js_number(qa_ui_default_preferences.hud_scale);
  read_value(cvars, seat, "hudScale", def != NULL ? def : "1", buffer,
             sizeof buffer);
  out.hud_scale = qa_input_js_parse_number(buffer);
  free(def);
  def = qa_input_js_number(qa_ui_default_preferences.text_scale);
  read_value(cvars, seat, "textScale", def != NULL ? def : "1", buffer,
             sizeof buffer);
  out.text_scale = qa_input_js_parse_number(buffer);
  free(def);
  def = qa_input_js_number(qa_ui_default_preferences.menu_scale);
  read_value(cvars, seat, "menuScale", def != NULL ? def : "1", buffer,
             sizeof buffer);
  out.menu_scale = qa_input_js_parse_number(buffer);
  free(def);
  def = qa_input_js_number(qa_ui_default_preferences.crosshair_size);
  read_value(cvars, seat, "crosshairSize", def != NULL ? def : "8", buffer,
             sizeof buffer);
  out.crosshair_size = qa_input_js_parse_number(buffer);
  free(def);
  read_value(cvars, seat, "highContrast",
             qa_ui_default_preferences.high_contrast ? "1" : "0", buffer,
             sizeof buffer);
  out.high_contrast = strcmp(buffer, "1") == 0;
  read_value(cvars, seat, "reducedFlashes",
             qa_ui_default_preferences.reduced_flashes ? "1" : "0", buffer,
             sizeof buffer);
  out.reduced_flashes = strcmp(buffer, "1") == 0;
  read_value(cvars, seat, "captions",
             qa_ui_default_preferences.captions ? "1" : "0", buffer,
             sizeof buffer);
  out.captions = strcmp(buffer, "1") == 0;
  read_value(cvars, seat, "crosshair",
             qa_ui_default_preferences.crosshair ? "1" : "0", buffer,
             sizeof buffer);
  out.crosshair = strcmp(buffer, "1") == 0;
  read_value(cvars, seat, "typeface", qa_ui_default_preferences.typeface,
             buffer, sizeof buffer);
  out.typeface = strcmp(buffer, "bold") == 0 ? "bold" : "standard";
  read_value(cvars, seat, "colorMode", qa_ui_default_preferences.color_mode,
             buffer, sizeof buffer);
  out.color_mode = strcmp(buffer, "blue-yellow") == 0 ? "blue-yellow"
                   : strcmp(buffer, "monochrome") == 0 ? "monochrome"
                                                         : "standard";
  return out;
}

void qa_ui_write_preferences(qa_cvar_registry *cvars, int64_t seat,
                             qa_ui_preference_values values) {
  static const char *const keys[10] = {
    "hudScale", "textScale", "menuScale", "crosshairSize", "highContrast",
    "reducedFlashes", "captions", "crosshair", "typeface", "colorMode"
  };
  for (size_t i = 0; i < 10; i++) {
    char name[72], text_buffer[64];
    const char *text = text_buffer;
    char *number = NULL;
    switch (i) {
      case 0: number = qa_input_js_number(values.hud_scale); break;
      case 1: number = qa_input_js_number(values.text_scale); break;
      case 2: number = qa_input_js_number(values.menu_scale); break;
      case 3: number = qa_input_js_number(values.crosshair_size); break;
      case 4: text = values.high_contrast ? "1" : "0"; break;
      case 5: text = values.reduced_flashes ? "1" : "0"; break;
      case 6: text = values.captions ? "1" : "0"; break;
      case 7: text = values.crosshair ? "1" : "0"; break;
      case 8: text = values.typeface; break;
      default: text = values.color_mode; break;
    }
    if (number != NULL) text = number;
    seat_name(seat, keys[i], name, sizeof name);
    qa_cvar_snapshot out;
    if (qa_cvar_registry_set(cvars, name, text, false, &out))
      qa_cvar_read_free(&out);
    free(number);
  }
}
