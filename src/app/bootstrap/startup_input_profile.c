/* Port of src/app/bootstrap/startup-input-profile.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/startup_input_profile.h"
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
#include "qa/input/bindings.h"
#include "qa/input/gamepad.h"
#include "qa/input/mouse.h"
#include "qa/input/support.h"
struct qa_app_startup_input_profile {
  const qa_config_store *settings;
  qa_seat_input *input;
  qa_app_startup_input_profile_host host;
  char *baseline;
  char *gamepad_baseline;
};
static char *snapshot(char *(*fn)(void *ctx, qa_seat_input *input), qa_app_startup_input_profile *p) {
  char *r = fn(p->host.ctx, p->input);
  if (!r) qa_range_error("Out of memory");
  return r;
}
static qa_app_startup_input_profile *make_profile(const qa_config_store *settings, qa_seat_input *input, qa_app_startup_input_profile_host host) {
  qa_app_startup_input_profile *p = calloc(1, sizeof *p);
  if (!p) return NULL;
  p->settings = settings; p->input = input; p->host = host;
  p->baseline = qa_input_dup("");
  p->gamepad_baseline = qa_input_dup("");
  return p;
}
qa_app_startup_input_profile *qa_app_startup_input_profile_retained(const qa_config_store *settings, qa_seat_input *input, qa_app_startup_input_profile_host host) {
  qa_app_startup_input_profile *p = make_profile(settings, input, host);
  if (!p) return NULL;
  free(p->baseline); p->baseline = snapshot(host.serialize_bindings, p);
  free(p->gamepad_baseline); p->gamepad_baseline = snapshot(host.serialize_gamepad, p);
  return p;
}
qa_app_startup_input_profile *qa_app_startup_input_profile_open(const qa_config_store *settings, qa_seat_input *input, qa_command_dialect dialect,
                                                                const qa_weapon_binding_item *items, size_t item_count, qa_app_startup_input_profile_host host) {
  qa_app_startup_input_profile *p = make_profile(settings, input, host);
  if (!p) return NULL;
  qa_seat_settings saved; memset(&saved, 0, sizeof saved);
  bool has_saved = qa_config_store_load_seat(settings, "input/seat-1.json", &saved);
  qa_seat_input_unbind_all(input);
  if (has_saved) {
    for (size_t i = 0; i < saved.binding_count; i++) qa_seat_input_bind(input, &saved.bindings[i]);
    qa_seat_input_gamepad(input)->tuning = saved.gamepad;
    qa_seat_settings_free(&saved);
  } else {
    size_t count = 0;
    qa_input_binding *defaults = qa_default_bindings(0, dialect, items, item_count, &count);
    for (size_t i = 0; i < count; i++) qa_seat_input_bind(input, &defaults[i]);
    for (size_t i = 0; i < count; i++) qa_input_binding_dispose(&defaults[i]);
    free(defaults);
  }
  free(p->baseline); p->baseline = snapshot(host.serialize_bindings, p);
  free(p->gamepad_baseline); p->gamepad_baseline = snapshot(host.serialize_gamepad, p);
  return p;
}
void qa_app_startup_input_profile_destroy(qa_app_startup_input_profile *p) {
  if (!p) return;
  free(p->baseline); free(p->gamepad_baseline); free(p);
}
qa_seat_input *qa_app_startup_input_profile_input(const qa_app_startup_input_profile *p) { return p->input; }
void qa_app_startup_input_profile_save(qa_app_startup_input_profile *p, const void *overrides,
                                       const char *const *history, size_t history_count, bool has_history) {
  /* Controller bindings normalize to device 0 in the saved copy. */
  size_t binding_count = 0;
  const qa_input_binding *current_bindings = qa_seat_input_bindings(p->input, &binding_count);
  qa_input_binding *bindings = calloc(binding_count ? binding_count : 1, sizeof *bindings);
  if (!bindings) qa_range_error("Out of memory");
  for (size_t i = 0; i < binding_count; i++) {
    if (!qa_input_binding_copy(&bindings[i], &current_bindings[i])) qa_range_error("Out of memory");
    if (bindings[i].input.kind == QA_PHYSICAL_INPUT_CONTROLLER_BUTTON || bindings[i].input.kind == QA_PHYSICAL_INPUT_CONTROLLER_AXIS)
      bindings[i].input.device = 0;
  }
  char *current = snapshot(p->host.serialize_bindings, p);
  bool bindings_changed = strcmp(current, p->baseline) != 0;
  char *gamepad_current = snapshot(p->host.serialize_gamepad, p);
  bool gamepad_changed = strcmp(gamepad_current, p->gamepad_baseline) != 0;
  size_t override_count = overrides ? p->host.override_count(p->host.ctx, overrides) : 0;
  if (!bindings_changed && !gamepad_changed && override_count == 0 && !has_history) {
    for (size_t i = 0; i < binding_count; i++) qa_input_binding_dispose(&bindings[i]);
    free(bindings); free(current); free(gamepad_current);
    return;
  }
  qa_seat_settings saved; memset(&saved, 0, sizeof saved);
  bool has_saved = qa_config_store_load_seat(p->settings, "input/seat-1.json", &saved);
  qa_seat_settings baseline;
  if (has_saved) baseline = saved;
  else {
    memset(&baseline, 0, sizeof baseline);
    baseline.version = 1;
    baseline.gamepad = qa_seat_input_gamepad(p->input)->tuning;
    baseline.mouse = qa_default_mouse_tuning;
    baseline.rumble = true;
    baseline.rumble_strength = 1;
    baseline.controller.kind = QA_CONTROLLER_AUTOMATIC;
    baseline.bindings = bindings;
    baseline.binding_count = binding_count;
    bindings = NULL;
  }
  if (bindings_changed && has_saved) {
    for (size_t i = 0; i < baseline.binding_count; i++) qa_input_binding_dispose(&baseline.bindings[i]);
    free(baseline.bindings);
    baseline.bindings = bindings;
    baseline.binding_count = binding_count;
    bindings = NULL;
  }
  if (gamepad_changed) baseline.gamepad = qa_seat_input_gamepad(p->input)->tuning;
  qa_seat_settings preferences = p->host.frontend_seat_settings(p->host.ctx, overrides, baseline);
  qa_seat_settings selected = preferences;
  if (has_history) {
    for (size_t i = 0; i < selected.history_count; i++) free(selected.history[i]);
    free(selected.history);
    selected.history = calloc(history_count ? history_count : 1, sizeof *selected.history);
    if (!selected.history) qa_range_error("Out of memory");
    selected.history_count = history_count;
    for (size_t i = 0; i < history_count; i++) {
      selected.history[i] = qa_input_dup(history[i]);
      if (!selected.history[i]) qa_range_error("Out of memory");
    }
  }
  char *selected_text = p->host.serialize_settings(p->host.ctx, &selected);
  char *baseline_text = p->host.serialize_settings(p->host.ctx, &baseline);
  if (!selected_text || !baseline_text) qa_range_error("Out of memory");
  bool differs = strcmp(selected_text, baseline_text) != 0;
  free(selected_text); free(baseline_text);
  if (bindings_changed || gamepad_changed || differs)
    qa_config_store_save_seat(p->settings, "input/seat-1.json", &selected);
  qa_seat_settings_free(&selected);
  if (bindings != NULL) {
    for (size_t i = 0; i < binding_count; i++) qa_input_binding_dispose(&bindings[i]);
    free(bindings);
  }
  free(p->baseline); p->baseline = current;
  free(p->gamepad_baseline); p->gamepad_baseline = gamepad_current;
}
