/* Port of src/app/bootstrap/startup-input-profile.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/startup_input_profile.h"
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
#include "qa/input/bindings.h"
#include "qa/input/gamepad.h"
#include "qa/input/mouse.h"
#include "qa/input/support.h"
/** The front end edits the same first-seat profile that gameplay loads. */
struct qa_app_startup_input_profile {
  qa_config_store settings;
  qa_seat_input *input;
  char *baseline;
  char *gamepad_baseline;
};
static char *checked(char *text) {
  if (text == NULL) qa_range_error("Out of memory");
  return text;
}
/* JSON.stringify(this.input.bindings) / JSON.stringify(this.input.gamepad.tuning) */
static char *bindings_text(qa_seat_input *input) {
  size_t count = 0;
  const qa_input_binding *bindings = qa_seat_input_bindings(input, &count);
  return checked(qa_input_bindings_json(bindings, count));
}
static char *gamepad_text(qa_seat_input *input) { return checked(qa_gamepad_tuning_json(&qa_seat_input_gamepad(input)->tuning)); }
static qa_app_startup_input_profile *make_profile(const qa_config_store *settings, qa_seat_input *input) {
  qa_app_startup_input_profile *p = calloc(1, sizeof *p);
  if (p == NULL) qa_range_error("Out of memory");
  if (!qa_config_store_init(&p->settings, settings->root)) qa_range_error("Out of memory");
  p->input = input;
  return p;
}
qa_app_startup_input_profile *qa_app_startup_input_profile_retained(const qa_config_store *settings, qa_seat_input *input) {
  qa_app_startup_input_profile *p = make_profile(settings, input);
  p->baseline = bindings_text(input);
  p->gamepad_baseline = gamepad_text(input);
  return p;
}
static void seat_settings_cleanup(void *arg) { qa_seat_settings_free(arg); }
static void profile_cleanup(void *arg) { qa_app_startup_input_profile_destroy(arg); }
qa_app_startup_input_profile *qa_app_startup_input_profile_open(const qa_config_store *settings, qa_seat_input *input, qa_command_dialect dialect,
                                                                const qa_weapon_binding_item *items, size_t item_count) {
  qa_app_startup_input_profile *p = make_profile(settings, input);
  qa_cleanup profile_guard;
  qa_cleanup_push(&profile_guard, profile_cleanup, p);
  qa_seat_settings saved;
  memset(&saved, 0, sizeof saved);
  bool has_saved = qa_config_store_load_seat(settings, "input/seat-1.json", &saved);
  qa_cleanup saved_guard;
  qa_cleanup_push(&saved_guard, seat_settings_cleanup, &saved);
  qa_seat_input_unbind_all(input);
  if (has_saved) {
    for (size_t i = 0; i < saved.binding_count; i++) qa_seat_input_bind(input, &saved.bindings[i]);
    qa_seat_input_gamepad(input)->tuning = saved.gamepad;
  } else {
    size_t count = 0;
    qa_input_binding *defaults = qa_default_bindings(0, dialect, items, item_count, &count);
    for (size_t i = 0; i < count; i++) qa_seat_input_bind(input, &defaults[i]);
    for (size_t i = 0; i < count; i++) qa_input_binding_dispose(&defaults[i]);
    free(defaults);
  }
  qa_cleanup_pop(&saved_guard);
  qa_seat_settings_free(&saved);
  p->baseline = bindings_text(input);
  p->gamepad_baseline = gamepad_text(input);
  qa_cleanup_pop(&profile_guard);
  return p;
}
void qa_app_startup_input_profile_destroy(qa_app_startup_input_profile *p) {
  if (p == NULL) return;
  qa_config_store_destroy(&p->settings);
  free(p->baseline);
  free(p->gamepad_baseline);
  free(p);
}
qa_seat_input *qa_app_startup_input_profile_input(const qa_app_startup_input_profile *p) { return p->input; }
/* Object.keys(values).length */
static size_t override_count(const qa_app_frontend_preference_overrides *v) {
  if (v == NULL) return 0;
  const bool present[] = {v->has_effects_volume, v->has_music_volume, v->has_controller_vibration, v->has_controller_vibration_strength,
                          v->has_sensitivity, v->has_pitch, v->has_yaw, v->has_invert_mouse, v->has_acceleration, v->has_filter,
                          v->has_look_spring, v->has_look_strafe, v->has_free_look, v->has_always_run};
  size_t count = 0;
  for (size_t i = 0; i < sizeof present / sizeof present[0]; i++) count += present[i];
  return count;
}
typedef struct save_scope {
  qa_input_binding *bindings; size_t binding_count;
  char *current, *gamepad_current, *selected_text, *baseline_text;
  qa_seat_settings saved; bool has_saved;
} save_scope;
static void save_scope_free(void *arg) {
  save_scope *scope = arg;
  for (size_t i = 0; i < scope->binding_count; i++) qa_input_binding_dispose(&scope->bindings[i]);
  free(scope->bindings);
  scope->bindings = NULL;
  scope->binding_count = 0;
  free(scope->current); free(scope->gamepad_current); free(scope->selected_text); free(scope->baseline_text);
  scope->current = scope->gamepad_current = scope->selected_text = scope->baseline_text = NULL;
  if (scope->has_saved) qa_seat_settings_free(&scope->saved);
  scope->has_saved = false;
}
void qa_app_startup_input_profile_save(qa_app_startup_input_profile *p, const qa_app_frontend_preference_overrides *values,
                                       const char *const *history, size_t history_count, bool has_history) {
  save_scope scope;
  memset(&scope, 0, sizeof scope);
  qa_cleanup guard;
  qa_cleanup_push(&guard, save_scope_free, &scope);
  /* Controller bindings normalize to device 0 in the saved copy. */
  size_t count = 0;
  const qa_input_binding *current_bindings = qa_seat_input_bindings(p->input, &count);
  scope.bindings = calloc(count != 0 ? count : 1, sizeof *scope.bindings);
  if (scope.bindings == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < count; i++) {
    if (!qa_input_binding_copy(&scope.bindings[i], &current_bindings[i])) qa_range_error("Out of memory");
    scope.binding_count = i + 1;
    if (scope.bindings[i].input.kind == QA_PHYSICAL_INPUT_CONTROLLER_BUTTON || scope.bindings[i].input.kind == QA_PHYSICAL_INPUT_CONTROLLER_AXIS)
      scope.bindings[i].input.device = 0;
  }
  scope.current = bindings_text(p->input);
  bool bindings_changed = strcmp(scope.current, p->baseline) != 0;
  scope.gamepad_current = gamepad_text(p->input);
  bool gamepad_changed = strcmp(scope.gamepad_current, p->gamepad_baseline) != 0;
  if (!bindings_changed && !gamepad_changed && override_count(values) == 0 && !has_history) {
    qa_cleanup_pop(&guard);
    save_scope_free(&scope);
    return;
  }
  scope.has_saved = qa_config_store_load_seat(&p->settings, "input/seat-1.json", &scope.saved);
  /* baseline = saved ?? { version: 1, gamepad, mouse: { ...defaultMouseTuning }, history: [], rumble: true,
   * controller: { kind: "automatic" }, bindings } (fields borrowed from saved or the scope). */
  qa_seat_settings baseline;
  if (scope.has_saved) baseline = scope.saved;
  else {
    memset(&baseline, 0, sizeof baseline);
    baseline.version = 1;
    baseline.gamepad = qa_seat_input_gamepad(p->input)->tuning;
    baseline.mouse = qa_default_mouse_tuning;
    baseline.rumble = true;
    baseline.rumble_strength = 1; /* TS absent; the parser and writer resolve it to 1 */
    baseline.controller.kind = QA_CONTROLLER_AUTOMATIC;
    baseline.bindings = scope.bindings;
    baseline.binding_count = scope.binding_count;
  }
  qa_seat_settings merged = baseline;
  if (bindings_changed) { merged.bindings = scope.bindings; merged.binding_count = scope.binding_count; }
  if (gamepad_changed) merged.gamepad = qa_seat_input_gamepad(p->input)->tuning;
  qa_app_frontend_preference_overrides none;
  memset(&none, 0, sizeof none);
  qa_seat_settings selected = qa_app_frontend_seat_settings(values != NULL ? *values : none, &merged);
  if (has_history) {
    selected.history = (char **)history;
    selected.history_count = history_count;
  }
  bool differs = false;
  if (!bindings_changed && !gamepad_changed) {
    scope.selected_text = checked(qa_seat_settings_json(&selected));
    scope.baseline_text = checked(qa_seat_settings_json(&baseline));
    differs = strcmp(scope.selected_text, scope.baseline_text) != 0;
  }
  if ((bindings_changed || gamepad_changed || differs) && !qa_config_store_save_seat(&p->settings, "input/seat-1.json", &selected))
    qa_raise("Error", 0, 0, "Could not write input/seat-1.json");
  free(p->baseline);
  p->baseline = scope.current;
  scope.current = NULL;
  free(p->gamepad_baseline);
  p->gamepad_baseline = scope.gamepad_current;
  scope.gamepad_current = NULL;
  qa_cleanup_pop(&guard);
  save_scope_free(&scope);
}
