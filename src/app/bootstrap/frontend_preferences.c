/* Port of src/app/bootstrap/frontend-preferences.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/frontend_preferences.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/input_devices.h"
#include "qa/contracts/identity.h"
#include "qa/foundation/support.h"
#include "qa/input/mouse_settings.h"
#include "qa/input/user_command.h"
static bool same_number(double a, double b) {
  /* Object.is for pitch/yaw: NaN equals NaN, +0 differs from -0. */
  if (isnan(a) && isnan(b)) return true;
  if (a == 0 && b == 0) return signbit(a) == signbit(b);
  return a == b;
}
qa_app_frontend_preference_overrides qa_app_changed_frontend_preferences(qa_app_frontend_preference_values before,
    qa_app_frontend_preference_values after, qa_app_frontend_preference_overrides selected) {
  qa_app_frontend_preference_overrides out = selected;
  if (after.controller_vibration_strength != before.controller_vibration_strength) { out.has_controller_vibration_strength = true; out.controller_vibration_strength = after.controller_vibration_strength; }
  if (after.controller_vibration != before.controller_vibration) { out.has_controller_vibration = true; out.controller_vibration = after.controller_vibration; }
  if (after.effects_volume != before.effects_volume) { out.has_effects_volume = true; out.effects_volume = after.effects_volume; }
  if (after.music_volume != before.music_volume) { out.has_music_volume = true; out.music_volume = after.music_volume; }
  if (after.sensitivity != before.sensitivity) { out.has_sensitivity = true; out.sensitivity = after.sensitivity; }
  if (!same_number(after.pitch, before.pitch)) { out.has_pitch = true; out.pitch = after.pitch; }
  if (!same_number(after.yaw, before.yaw)) { out.has_yaw = true; out.yaw = after.yaw; }
  if (after.acceleration != before.acceleration) { out.has_acceleration = true; out.acceleration = after.acceleration; }
  if (after.filter != before.filter) { out.has_filter = true; out.filter = after.filter; }
  if (after.look_spring != before.look_spring) { out.has_look_spring = true; out.look_spring = after.look_spring == true; }
  if (after.look_strafe != before.look_strafe) { out.has_look_strafe = true; out.look_strafe = after.look_strafe == true; }
  if (after.free_look != before.free_look) { out.has_free_look = true; out.free_look = after.free_look; }
  if (after.invert_mouse != before.invert_mouse) { out.has_invert_mouse = true; out.invert_mouse = after.invert_mouse; }
  if (after.always_run != before.always_run) { out.has_always_run = true; out.always_run = after.always_run; }
  return out;
}
qa_mouse_tuning qa_app_frontend_mouse_tuning(qa_app_frontend_preference_overrides values, qa_mouse_tuning mouse) {
  if (values.has_sensitivity) mouse.sensitivity = values.sensitivity;
  if (values.has_pitch) mouse.pitch = values.pitch;
  if (values.has_yaw) mouse.yaw = values.yaw;
  if (values.has_acceleration) mouse.acceleration = values.acceleration;
  if (values.has_filter) mouse.filter = values.filter;
  if (values.has_look_spring) mouse.look_spring = values.look_spring;
  if (values.has_look_strafe) mouse.look_strafe = values.look_strafe;
  if (values.has_free_look) mouse.free_look = values.free_look;
  if (values.has_invert_mouse) mouse.invert_pitch = values.invert_mouse;
  return mouse;
}
qa_seat_settings qa_app_frontend_seat_settings(qa_app_frontend_preference_overrides values, const qa_seat_settings *saved) {
  qa_seat_settings out = *saved;
  out.mouse = qa_app_frontend_mouse_tuning(values, saved->mouse);
  if (values.has_always_run) { out.has_always_run = true; out.always_run = values.always_run; }
  if (values.has_controller_vibration) out.rumble = values.controller_vibration;
  if (values.has_controller_vibration_strength) out.rumble_strength = values.controller_vibration_strength;
  return out;
}

/* ---- TS class FrontendPreferences ---------------------------------------------- */
struct qa_app_frontend_preferences {
  qa_command_dialect (*dialect)(void *ctx);
  void *dialect_ctx;
  qa_config_store shared_settings; bool has_shared_settings;
  qa_app_frontend_preference_overrides values;
  bool has_audio_baseline; qa_app_audio_preferences audio_baseline; /* false is TS {} */
  bool has_audio_output; qa_ui_audio_output_settings audio_output;
  qa_app_audio_music_preferences (*music_settings)(void *ctx); void *music_ctx;
  bool has_always_run_baseline; bool always_run_baseline;
  bool vibration_baseline; double vibration_strength_baseline;
  qa_mouse_tuning mouse_baseline;
  void (*on_change)(void *ctx); void *on_change_ctx;
  bool change_timer; double change_at;
  /* Binding services handed to the ui/settings factories (ctx = this). */
  qa_ui_controller_vibration_service vibration_service;
  qa_ui_audio_settings_service audio_service;
  qa_ui_primary_input_service primary_service;
  qa_ui_mouse_motion_service motion_service;
};
static void free_audio_preferences(qa_app_audio_preferences *value) {
  free(value->menu_track);
  free(value->device_name);
  memset(value, 0, sizeof *value);
}
static char *dup_text(const char *text) {
  if (text == NULL) return NULL;
  size_t length = strlen(text) + 1;
  char *copy = malloc(length);
  if (copy == NULL) qa_range_error("Out of memory");
  memcpy(copy, text, length);
  return copy;
}
qa_app_frontend_preferences *qa_app_frontend_preferences_create(qa_command_dialect (*dialect)(void *ctx), void *dialect_ctx,
    const qa_config_store *shared_settings) {
  qa_app_frontend_preferences *p = calloc(1, sizeof *p);
  if (p == NULL) return NULL;
  p->dialect = dialect;
  p->dialect_ctx = dialect_ctx;
  if (shared_settings != NULL) {
    if (!qa_config_store_init(&p->shared_settings, shared_settings->root)) { free(p); return NULL; }
    p->has_shared_settings = true;
  }
  p->vibration_baseline = true;
  p->vibration_strength_baseline = 1;
  p->mouse_baseline = qa_default_mouse_tuning;
  return p;
}
void qa_app_frontend_preferences_destroy(qa_app_frontend_preferences *p) {
  if (p == NULL) return;
  if (p->has_shared_settings) qa_config_store_destroy(&p->shared_settings);
  free_audio_preferences(&p->audio_baseline);
  free(p);
}
void qa_app_frontend_preferences_set_on_change(qa_app_frontend_preferences *p, void (*fn)(void *ctx), void *ctx) {
  p->on_change = fn;
  p->on_change_ctx = ctx;
}
qa_app_frontend_preference_overrides *qa_app_frontend_preferences_values(qa_app_frontend_preferences *p) { return &p->values; }
const qa_app_audio_preferences *qa_app_frontend_preferences_audio_baseline(const qa_app_frontend_preferences *p) {
  return p->has_audio_baseline ? &p->audio_baseline : NULL;
}
void qa_app_frontend_preferences_audio_values(const qa_app_frontend_preferences *p, double *effects, double *music) {
  *effects = p->values.has_effects_volume ? p->values.effects_volume : p->has_audio_baseline ? p->audio_baseline.effects_volume : 0.7;
  *music = p->values.has_music_volume ? p->values.music_volume : p->has_audio_baseline ? p->audio_baseline.music_volume : 0.25;
}
/* TS private changed(): one 100 ms timer coalesces writes. */
static void changed(qa_app_frontend_preferences *p) {
  if (p->on_change == NULL || p->change_timer) return;
  p->change_timer = true;
  p->change_at = qa_app_performance_now() + 100;
}
void qa_app_frontend_preferences_pump(qa_app_frontend_preferences *p) {
  if (!p->change_timer || qa_app_performance_now() < p->change_at) return;
  p->change_timer = false;
  if (p->on_change != NULL) p->on_change(p->on_change_ctx);
}
/* TS private effectiveTuning(). */
static qa_global_input_tuning effective_tuning(const qa_app_frontend_preferences *p) {
  qa_global_input_tuning tuning;
  memset(&tuning, 0, sizeof tuning);
  tuning.version = 1;
  tuning.mouse = qa_app_frontend_mouse_tuning(p->values, p->mouse_baseline);
  if (p->values.has_always_run) { tuning.has_always_run = true; tuning.always_run = p->values.always_run; }
  else if (p->has_always_run_baseline) { tuning.has_always_run = true; tuning.always_run = p->always_run_baseline; }
  tuning.rumble = p->values.has_controller_vibration ? p->values.controller_vibration : p->vibration_baseline;
  tuning.rumble_strength = p->values.has_controller_vibration_strength ? p->values.controller_vibration_strength : p->vibration_strength_baseline;
  return tuning;
}
qa_app_frontend_preference_overrides qa_app_frontend_preferences_effective_values(const qa_app_frontend_preferences *p) {
  qa_app_frontend_preference_overrides out;
  memset(&out, 0, sizeof out);
  const qa_mouse_tuning *mouse = &p->mouse_baseline;
  out.has_sensitivity = true; out.sensitivity = mouse->sensitivity;
  out.has_pitch = true; out.pitch = mouse->pitch;
  out.has_yaw = true; out.yaw = mouse->yaw;
  out.has_invert_mouse = true; out.invert_mouse = mouse->invert_pitch;
  out.has_acceleration = true; out.acceleration = mouse->acceleration;
  out.has_filter = true; out.filter = mouse->filter;
  out.has_look_spring = true; out.look_spring = mouse->look_spring;
  out.has_look_strafe = true; out.look_strafe = mouse->look_strafe;
  out.has_free_look = true; out.free_look = mouse->free_look;
  out.has_controller_vibration = true; out.controller_vibration = p->vibration_baseline;
  out.has_controller_vibration_strength = true; out.controller_vibration_strength = p->vibration_strength_baseline;
  if (p->has_always_run_baseline) { out.has_always_run = true; out.always_run = p->always_run_baseline; }
  /* ...this.values overlays every present override. */
  const qa_app_frontend_preference_overrides *v = &p->values;
  if (v->has_effects_volume) { out.has_effects_volume = true; out.effects_volume = v->effects_volume; }
  if (v->has_music_volume) { out.has_music_volume = true; out.music_volume = v->music_volume; }
  if (v->has_controller_vibration) out.controller_vibration = v->controller_vibration;
  if (v->has_controller_vibration_strength) out.controller_vibration_strength = v->controller_vibration_strength;
  if (v->has_sensitivity) out.sensitivity = v->sensitivity;
  if (v->has_pitch) out.pitch = v->pitch;
  if (v->has_yaw) out.yaw = v->yaw;
  if (v->has_invert_mouse) out.invert_mouse = v->invert_mouse;
  if (v->has_acceleration) out.acceleration = v->acceleration;
  if (v->has_filter) out.filter = v->filter;
  if (v->has_look_spring) out.look_spring = v->look_spring;
  if (v->has_look_strafe) out.look_strafe = v->look_strafe;
  if (v->has_free_look) out.free_look = v->free_look;
  if (v->has_always_run) { out.has_always_run = true; out.always_run = v->always_run; }
  return out;
}
void qa_app_frontend_preferences_save_global(qa_app_frontend_preferences *p) {
  if (!p->has_shared_settings) return;
  qa_global_input_tuning tuning = effective_tuning(p);
  if (!qa_config_store_save_global_tuning(&p->shared_settings, "input/global-tuning.json", &tuning))
    qa_raise("Error", 0, 0, "Could not write input/global-tuning.json");
  p->mouse_baseline = tuning.mouse;
  p->has_always_run_baseline = tuning.has_always_run;
  p->always_run_baseline = tuning.always_run;
  p->vibration_baseline = tuning.rumble;
  p->vibration_strength_baseline = tuning.rumble_strength;
}
/* The frontend-mouse-baseline MouseSettings registry and its identity owner. */
typedef struct mouse_baseline_scope {
  qa_identity_owner identity; bool has_identity;
  qa_cvar_registry *cvars;
  qa_seat_settings saved; bool has_saved;
} mouse_baseline_scope;
static void mouse_baseline_scope_free(void *arg) {
  mouse_baseline_scope *scope = arg;
  if (scope->has_saved) qa_seat_settings_free(&scope->saved);
  scope->has_saved = false;
  if (scope->cvars != NULL) qa_cvar_registry_destroy(scope->cvars);
  scope->cvars = NULL;
  if (scope->has_identity) qa_identity_owner_destroy(&scope->identity);
  scope->has_identity = false;
}
void qa_app_frontend_preferences_load_baseline(qa_app_frontend_preferences *p, const qa_config_store *settings) {
  qa_app_audio_preferences audio;
  memset(&audio, 0, sizeof audio);
  bool has_audio = qa_app_load_audio_settings(settings, &audio);
  free_audio_preferences(&p->audio_baseline);
  p->audio_baseline = audio;
  p->has_audio_baseline = has_audio;
  mouse_baseline_scope scope;
  memset(&scope, 0, sizeof scope);
  qa_cleanup guard;
  qa_cleanup_push(&guard, mouse_baseline_scope_free, &scope);
  scope.has_saved = qa_config_store_load_seat(settings, "input/seat-1.json", &scope.saved);
  if (!qa_identity_owner_create(&scope.identity, "frontend-mouse-baseline")) qa_range_error("Out of memory");
  scope.has_identity = true;
  qa_cvar_registry_options options;
  memset(&options, 0, sizeof options);
  options.dialect = p->dialect(p->dialect_ctx);
  options.context.session = scope.identity.session;
  options.context.origin.kind = QA_COMMAND_ORIGIN_LOCAL_SEAT;
  options.context.origin.seat = qa_identity_owner_seat(&scope.identity, 0);
  options.context.origin.client = qa_identity_owner_client(&scope.identity, 0, 0);
  scope.cvars = qa_cvar_registry_create(&options);
  if (scope.cvars == NULL) qa_range_error("Out of memory");
  qa_mouse_settings mouse;
  qa_mouse_settings_init(&mouse, scope.cvars);
  if (scope.has_saved) qa_mouse_settings_write(&mouse, scope.saved.mouse);
  p->mouse_baseline = qa_mouse_settings_read(&mouse);
  p->has_always_run_baseline = scope.has_saved && scope.saved.has_always_run;
  p->always_run_baseline = p->has_always_run_baseline && scope.saved.always_run;
  p->vibration_baseline = scope.has_saved ? scope.saved.rumble : true;
  p->vibration_strength_baseline = scope.has_saved ? scope.saved.rumble_strength : 1;
  if (p->has_shared_settings) {
    /* A global tuning document that cannot be loaded is ignored (TS catch → null). */
    qa_global_input_tuning global;
    memset(&global, 0, sizeof global);
    if (qa_config_store_load_global_tuning(&p->shared_settings, "input/global-tuning.json", &global)) {
      qa_mouse_settings_write(&mouse, global.mouse);
      p->mouse_baseline = qa_mouse_settings_read(&mouse);
      if (global.has_always_run) { p->has_always_run_baseline = true; p->always_run_baseline = global.always_run; }
      p->vibration_baseline = global.rumble;
      p->vibration_strength_baseline = global.rumble_strength;
    }
  }
  qa_cleanup_pop(&guard);
  mouse_baseline_scope_free(&scope);
}
static bool same_text(const char *a, const char *b) { return a == NULL || b == NULL ? a == b : strcmp(a, b) == 0; }
typedef struct save_audio_scope {
  qa_app_audio_preferences saved;
  qa_app_audio_music_preferences music;
  char *device_name;
} save_audio_scope;
static void save_audio_scope_free(void *arg) {
  save_audio_scope *scope = arg;
  free_audio_preferences(&scope->saved);
  free(scope->music.menu_track);
  scope->music.menu_track = NULL;
  free(scope->device_name);
  scope->device_name = NULL;
}
void qa_app_frontend_preferences_save_audio_baseline(qa_app_frontend_preferences *p, const qa_config_store *settings) {
  if (!p->has_audio_output && p->music_settings == NULL && !p->values.has_effects_volume && !p->values.has_music_volume) return;
  save_audio_scope scope;
  memset(&scope, 0, sizeof scope);
  qa_cleanup guard;
  qa_cleanup_push(&guard, save_audio_scope_free, &scope);
  /* saved is TS {} when false: every field reads undefined. */
  bool has_saved = qa_app_load_audio_settings(settings, &scope.saved);
  double effects = 0, music = 0;
  qa_app_frontend_preferences_audio_values(p, &effects, &music);
  double effects_volume = p->values.has_effects_volume ? p->values.effects_volume : has_saved ? scope.saved.effects_volume : effects;
  double music_volume = p->values.has_music_volume ? p->values.music_volume : has_saved ? scope.saved.music_volume : music;
  /* deviceName: undefined only when saved is {} and there is no output (?? null makes it null). */
  if (p->has_audio_output) scope.device_name = dup_text(p->audio_output.selected(p->audio_output.ctx));
  else if (has_saved) scope.device_name = dup_text(scope.saved.device_name);
  qa_audio_output_format format = p->has_audio_output && p->audio_output.has_format ? p->audio_output.format_read(p->audio_output.ctx)
      : has_saved ? scope.saved.output_format : qa_default_audio_output_format;
  if (p->music_settings != NULL) scope.music = p->music_settings(p->music_ctx);
  else {
    scope.music.music_shuffle = has_saved && scope.saved.has_music_shuffle ? scope.saved.music_shuffle : false;
    scope.music.menu_track = dup_text(has_saved && scope.saved.has_menu_track ? scope.saved.menu_track : "auto");
  }
  /* saved.X !== value, where an absent saved field is undefined (never equal). */
  bool differs = !has_saved || !scope.saved.has_music_shuffle || scope.saved.music_shuffle != scope.music.music_shuffle ||
      !scope.saved.has_menu_track || !same_text(scope.saved.menu_track, scope.music.menu_track) ||
      !same_text(scope.saved.device_name, scope.device_name) || scope.saved.effects_volume != effects_volume ||
      scope.saved.music_volume != music_volume || scope.saved.output_format.sample_rate != format.sample_rate ||
      scope.saved.output_format.sample_bits != format.sample_bits || scope.saved.output_format.channels != format.channels;
  if (differs) {
    qa_app_audio_settings_input input;
    memset(&input, 0, sizeof input);
    input.has_music_preferences = true;
    input.music_shuffle = scope.music.music_shuffle;
    input.menu_track = scope.music.menu_track;
    input.has_output_format = true;
    input.output_format = format;
    input.selected_output = scope.device_name;
    input.effects_volume = effects_volume;
    input.music_volume = music_volume;
    qa_app_save_audio_settings(settings, input);
  }
  /* this.audioBaseline = { deviceName, outputFormat, ...volume, ...musicPreferences } */
  qa_app_audio_preferences values;
  memset(&values, 0, sizeof values);
  values.device_name = scope.device_name;
  scope.device_name = NULL;
  values.output_format = format;
  values.effects_volume = effects_volume;
  values.music_volume = music_volume;
  values.has_music_shuffle = true;
  values.music_shuffle = scope.music.music_shuffle;
  values.has_menu_track = true;
  values.menu_track = scope.music.menu_track;
  scope.music.menu_track = NULL;
  qa_cleanup_pop(&guard);
  save_audio_scope_free(&scope);
  free_audio_preferences(&p->audio_baseline);
  p->audio_baseline = values;
  p->has_audio_baseline = true;
}

/* ---- bindings(output?, musicSettings?) -------------------------------------------- */
/* write = values => { this.values = { ...this.values, ...values }; this.changed(); } */
static qa_ui_controller_vibration_settings vibration_read(void *ctx) {
  qa_app_frontend_preferences *p = ctx;
  qa_ui_controller_vibration_settings out;
  out.controller_vibration = p->values.has_controller_vibration ? p->values.controller_vibration : p->vibration_baseline;
  out.controller_vibration_strength = p->values.has_controller_vibration_strength ? p->values.controller_vibration_strength
                                                                                  : p->vibration_strength_baseline;
  return out;
}
static void vibration_write(void *ctx, qa_ui_controller_vibration_settings values, unsigned mask) {
  qa_app_frontend_preferences *p = ctx;
  if (mask & QA_UI_VIBRATION_ON) { p->values.has_controller_vibration = true; p->values.controller_vibration = values.controller_vibration; }
  if (mask & QA_UI_VIBRATION_STRENGTH) {
    p->values.has_controller_vibration_strength = true;
    p->values.controller_vibration_strength = values.controller_vibration_strength;
  }
  changed(p);
}
static qa_ui_audio_settings audio_read(void *ctx) {
  qa_ui_audio_settings out;
  qa_app_frontend_preferences_audio_values(ctx, &out.effects_volume, &out.music_volume);
  return out;
}
static void audio_write(void *ctx, qa_ui_audio_settings values, unsigned mask) {
  qa_app_frontend_preferences *p = ctx;
  if (mask & QA_UI_AUDIO_EFFECTS) { p->values.has_effects_volume = true; p->values.effects_volume = values.effects_volume; }
  if (mask & QA_UI_AUDIO_MUSIC) { p->values.has_music_volume = true; p->values.music_volume = values.music_volume; }
  changed(p);
}
static qa_ui_primary_input_settings primary_read(void *ctx) {
  qa_app_frontend_preferences *p = ctx;
  qa_ui_primary_input_settings out;
  out.sensitivity = p->values.has_sensitivity ? p->values.sensitivity : p->mouse_baseline.sensitivity;
  out.pitch = p->values.has_pitch ? p->values.pitch : p->mouse_baseline.pitch;
  out.yaw = p->values.has_yaw ? p->values.yaw : p->mouse_baseline.yaw;
  out.invert_mouse = p->values.has_invert_mouse ? p->values.invert_mouse : p->mouse_baseline.invert_pitch;
  out.always_run = p->values.has_always_run ? p->values.always_run
      : p->has_always_run_baseline ? p->always_run_baseline : qa_default_view_input_tuning(p->dialect(p->dialect_ctx)).always_run;
  return out;
}
static void primary_write(void *ctx, qa_ui_primary_input_settings values, unsigned mask) {
  qa_app_frontend_preferences *p = ctx;
  if (mask & QA_UI_PRIMARY_SENSITIVITY) { p->values.has_sensitivity = true; p->values.sensitivity = values.sensitivity; }
  if (mask & QA_UI_PRIMARY_PITCH) { p->values.has_pitch = true; p->values.pitch = values.pitch; }
  if (mask & QA_UI_PRIMARY_YAW) { p->values.has_yaw = true; p->values.yaw = values.yaw; }
  if (mask & QA_UI_PRIMARY_INVERT) { p->values.has_invert_mouse = true; p->values.invert_mouse = values.invert_mouse; }
  if (mask & QA_UI_PRIMARY_ALWAYS_RUN) { p->values.has_always_run = true; p->values.always_run = values.always_run; }
  changed(p);
}
static qa_ui_mouse_motion_settings motion_read(void *ctx) {
  qa_app_frontend_preferences *p = ctx;
  qa_ui_mouse_motion_settings out;
  out.acceleration = p->values.has_acceleration ? p->values.acceleration : p->mouse_baseline.acceleration;
  out.look_spring = p->values.has_look_spring ? p->values.look_spring : p->mouse_baseline.look_spring;
  out.look_strafe = p->values.has_look_strafe ? p->values.look_strafe : p->mouse_baseline.look_strafe;
  out.filter = p->values.has_filter ? p->values.filter : p->mouse_baseline.filter;
  out.free_look = p->values.has_free_look ? p->values.free_look : p->mouse_baseline.free_look;
  return out;
}
static void motion_write(void *ctx, qa_ui_mouse_motion_settings values, unsigned mask) {
  qa_app_frontend_preferences *p = ctx;
  if (mask & QA_UI_MOTION_ACCELERATION) { p->values.has_acceleration = true; p->values.acceleration = values.acceleration; }
  if (mask & QA_UI_MOTION_FILTER) { p->values.has_filter = true; p->values.filter = values.filter; }
  if (mask & QA_UI_MOTION_FREE_LOOK) { p->values.has_free_look = true; p->values.free_look = values.free_look; }
  if (mask & QA_UI_MOTION_LOOK_SPRING) { p->values.has_look_spring = true; p->values.look_spring = values.look_spring; }
  if (mask & QA_UI_MOTION_LOOK_STRAFE) { p->values.has_look_strafe = true; p->values.look_strafe = values.look_strafe; }
  changed(p);
}
/* () => this.dialect() === "q1-netquake" || this.dialect() === "q1-quakeworld" */
static bool spring_available(void *ctx) {
  qa_app_frontend_preferences *p = ctx;
  qa_command_dialect dialect = p->dialect(p->dialect_ctx);
  return dialect == QA_COMMAND_DIALECT_Q1_NETQUAKE || dialect == QA_COMMAND_DIALECT_Q1_QUAKEWORLD;
}
typedef struct bindings_owner {
  qa_ui_setting_bindings parts[4];
  qa_ui_setting_binding *items;
} bindings_owner;
static void bindings_teardown(void *state) {
  bindings_owner *owner = state;
  for (size_t i = 0; i < 4; i++) qa_ui_setting_bindings_free(&owner->parts[i]);
  free(owner->items);
  free(owner);
}
qa_ui_setting_bindings qa_app_frontend_preferences_bindings(qa_app_frontend_preferences *p,
    const qa_ui_audio_output_settings *output,
    qa_app_audio_music_preferences (*music_settings)(void *ctx), void *music_ctx) {
  p->has_audio_output = output != NULL;
  if (output != NULL) p->audio_output = *output;
  else memset(&p->audio_output, 0, sizeof p->audio_output);
  p->music_settings = music_settings;
  p->music_ctx = music_ctx;
  p->vibration_service = (qa_ui_controller_vibration_service){p, vibration_read, vibration_write};
  p->audio_service = (qa_ui_audio_settings_service){p, audio_read, audio_write};
  p->primary_service = (qa_ui_primary_input_service){p, primary_read, primary_write};
  p->motion_service = (qa_ui_mouse_motion_service){p, motion_read, motion_write};
  bindings_owner *owner = calloc(1, sizeof *owner);
  if (owner == NULL) qa_range_error("Out of memory");
  owner->parts[0] = qa_ui_bind_controller_vibration(&p->vibration_service);
  owner->parts[1] = qa_ui_bind_audio_settings(&p->audio_service, output != NULL ? &p->audio_output : NULL);
  owner->parts[2] = qa_ui_bind_primary_input_settings(&p->primary_service);
  owner->parts[3] = qa_ui_bind_mouse_motion_settings(&p->motion_service, spring_available, p);
  size_t total = 0;
  for (size_t i = 0; i < 4; i++) total += owner->parts[i].count;
  owner->items = malloc((total == 0 ? 1 : total) * sizeof *owner->items);
  if (owner->items == NULL) qa_range_error("Out of memory");
  size_t at = 0;
  for (size_t i = 0; i < 4; i++) {
    memcpy(owner->items + at, owner->parts[i].items, owner->parts[i].count * sizeof *owner->items);
    at += owner->parts[i].count;
  }
  return qa_ui_setting_bindings_own(owner->items, total, owner, bindings_teardown);
}

/* ---- TS readFrontendInput / readFrontendPreferences ---- */
#include "qa/app/bootstrap/audio.h"
#include "qa/app/bootstrap/input.h"
#include "qa/input/haptics.h"
#include "qa/input/mouse.h"
#include "qa/input/user_command.h"
qa_app_frontend_preference_values qa_app_read_frontend_input(const qa_app_local_input *local) {
  qa_mouse_tuning mouse = qa_mouse_input_tuning(qa_input_command_builder_mouse(local->builder));
  qa_app_frontend_preference_values out;
  memset(&out, 0, sizeof out);
  out.has_controller_vibration = true; out.controller_vibration = qa_seat_haptics_enabled(local->haptics);
  out.has_controller_vibration_strength = true; out.controller_vibration_strength = qa_seat_haptics_strength(local->haptics);
  out.has_sensitivity = true; out.sensitivity = mouse.sensitivity;
  out.has_pitch = true; out.pitch = mouse.pitch;
  out.has_yaw = true; out.yaw = mouse.yaw;
  out.has_invert_mouse = true; out.invert_mouse = mouse.invert_pitch;
  out.has_acceleration = true; out.acceleration = mouse.acceleration;
  out.has_filter = true; out.filter = mouse.filter;
  out.has_look_spring = true; out.look_spring = mouse.look_spring;
  out.has_look_strafe = true; out.look_strafe = mouse.look_strafe;
  out.has_free_look = true; out.free_look = mouse.free_look;
  out.has_always_run = true; out.always_run = qa_input_command_builder_tuning(local->builder).always_run;
  return out;
}
bool qa_app_read_frontend_preferences(const qa_app_application_input *input, const qa_app_application_audio *audio,
    qa_app_frontend_preference_values *out) {
  if (qa_app_application_input_local_count(input) == 0) return false;
  *out = qa_app_read_frontend_input(qa_app_application_input_local(input, 0));
  out->has_effects_volume = true; out->effects_volume = qa_app_application_audio_effects_volume(audio);
  out->has_music_volume = true; out->music_volume = qa_app_application_audio_music_volume(audio);
  return true;
}
/* ---- TS applyFrontendPreferences / applyFrontendInput ---- */
void qa_app_apply_frontend_input(qa_app_frontend_preference_values values, qa_app_local_input *local) {
  /* TS ignores the haptics results (setStrength / setEnabled report device state only). */
  if (values.has_controller_vibration_strength) (void)qa_seat_haptics_set_strength(local->haptics, values.controller_vibration_strength);
  if (values.has_controller_vibration) (void)qa_seat_haptics_set_enabled(local->haptics, values.controller_vibration);
  qa_mouse_input *mouse = qa_input_command_builder_mouse(local->builder);
  qa_mouse_tuning tuning = qa_mouse_input_tuning(mouse);
  if (values.has_sensitivity) tuning.sensitivity = values.sensitivity;
  if (values.has_pitch) tuning.pitch = values.pitch;
  if (values.has_yaw) tuning.yaw = values.yaw;
  if (values.has_acceleration) tuning.acceleration = values.acceleration;
  if (values.has_filter) tuning.filter = values.filter;
  if (values.has_look_spring) tuning.look_spring = values.look_spring;
  if (values.has_look_strafe) tuning.look_strafe = values.look_strafe;
  if (values.has_free_look) tuning.free_look = values.free_look;
  if (values.has_invert_mouse) tuning.invert_pitch = values.invert_mouse;
  qa_mouse_input_set_tuning(mouse, tuning);
  if (values.has_always_run) {
    qa_view_input_tuning view = qa_input_command_builder_tuning(local->builder);
    view.always_run = values.always_run;
    qa_input_command_builder_set_tuning(local->builder, view);
  }
}
void qa_app_apply_frontend_preferences(qa_app_frontend_preference_overrides values, qa_app_application_input *input,
    qa_app_application_audio *audio) {
  if (values.has_effects_volume) qa_app_application_audio_set_effects_volume(audio, values.effects_volume);
  if (values.has_music_volume) qa_app_application_audio_set_music_volume(audio, values.music_volume);
  /* Frontend values belong to the frontend seat; other seats keep their own profiles. */
  if (qa_app_application_input_local_count(input) > 0) qa_app_apply_frontend_input(values, qa_app_application_input_local(input, 0));
}
