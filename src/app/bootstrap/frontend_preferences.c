/* Port of src/app/bootstrap/frontend-preferences.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/frontend_preferences.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
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
qa_app_frontend_mouse qa_app_frontend_mouse_tuning(qa_app_frontend_preference_overrides values, qa_app_frontend_mouse mouse) {
  if (values.has_sensitivity) mouse.sensitivity = values.sensitivity;
  if (values.has_pitch) mouse.pitch = values.pitch;
  if (values.has_yaw) mouse.yaw = values.yaw;
  if (values.has_acceleration) mouse.acceleration = values.acceleration;
  if (values.has_filter) mouse.filter = values.filter;
  if (values.has_look_spring) { mouse.has_look_spring = true; mouse.look_spring = values.look_spring; }
  if (values.has_look_strafe) { mouse.has_look_strafe = true; mouse.look_strafe = values.look_strafe; }
  if (values.has_free_look) mouse.free_look = values.free_look;
  if (values.has_invert_mouse) mouse.invert_pitch = values.invert_mouse;
  return mouse;
}
struct qa_app_frontend_preferences {
  qa_app_frontend_preferences_host host;
  qa_app_frontend_preference_overrides values;
  bool has_baseline_effects; double baseline_effects;
  bool has_baseline_music; double baseline_music;
  bool has_always_run_baseline; bool always_run_baseline;
  bool vibration_baseline; double vibration_strength_baseline;
  qa_app_frontend_mouse mouse_baseline;
  bool change_pending; double change_at;
};
qa_app_frontend_preferences *qa_app_frontend_preferences_create(qa_app_frontend_preferences_host host) {
  qa_app_frontend_preferences *preferences = calloc(1, sizeof *preferences);
  if (!preferences) return NULL;
  preferences->host = host;
  preferences->vibration_baseline = true;
  preferences->vibration_strength_baseline = 1;
  /* defaultMouseTuning ownership stays with input/mouse; baseline is
   * replaced by load_baseline before effective values are read. */
  return preferences;
}
void qa_app_frontend_preferences_destroy(qa_app_frontend_preferences *preferences) { free(preferences); }
qa_app_frontend_preference_overrides *qa_app_frontend_preferences_values(qa_app_frontend_preferences *p) { return &p->values; }
void qa_app_frontend_preferences_audio_values(const qa_app_frontend_preferences *p, double *effects, double *music) {
  *effects = p->values.has_effects_volume ? p->values.effects_volume : p->has_baseline_effects ? p->baseline_effects : 0.7;
  *music = p->values.has_music_volume ? p->values.music_volume : p->has_baseline_music ? p->baseline_music : 0.25;
}
void qa_app_frontend_preferences_set_audio_baseline(qa_app_frontend_preferences *p, bool has_effects, double effects, bool has_music, double music) {
  p->has_baseline_effects = has_effects; p->baseline_effects = effects;
  p->has_baseline_music = has_music; p->baseline_music = music;
}
void qa_app_frontend_preferences_set_baselines(qa_app_frontend_preferences *p, qa_app_frontend_mouse mouse,
                                               bool has_always_run, bool always_run, bool vibration, double vibration_strength) {
  p->mouse_baseline = mouse;
  p->has_always_run_baseline = has_always_run;
  p->always_run_baseline = always_run;
  p->vibration_baseline = vibration;
  p->vibration_strength_baseline = vibration_strength;
}
qa_app_frontend_preference_overrides qa_app_frontend_preferences_effective_values(const qa_app_frontend_preferences *p) {
  qa_app_frontend_preference_overrides out = {0};
  const qa_app_frontend_mouse *mouse = &p->mouse_baseline;
  out.has_sensitivity = true; out.sensitivity = mouse->sensitivity;
  out.has_pitch = true; out.pitch = mouse->pitch;
  out.has_yaw = true; out.yaw = mouse->yaw;
  out.has_invert_mouse = true; out.invert_mouse = mouse->invert_pitch;
  out.has_acceleration = true; out.acceleration = mouse->acceleration;
  out.has_filter = true; out.filter = mouse->filter;
  out.has_look_spring = true; out.look_spring = mouse->has_look_spring && mouse->look_spring;
  out.has_look_strafe = true; out.look_strafe = mouse->has_look_strafe && mouse->look_strafe;
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
void qa_app_frontend_preferences_changed(qa_app_frontend_preferences *p, double now_milliseconds) {
  if (!p->host.on_change || p->change_pending) return;
  p->change_pending = true;
  p->change_at = now_milliseconds + 100;
}
void qa_app_frontend_preferences_pump(qa_app_frontend_preferences *p, double now_milliseconds) {
  if (!p->change_pending || now_milliseconds < p->change_at) return;
  p->change_pending = false;
  if (p->host.on_change) p->host.on_change(p->host.ctx);
}
const char *qa_app_frontend_preferences_load_baseline(qa_app_frontend_preferences *p) {
  return p->host.load_baseline ? p->host.load_baseline(p->host.ctx, p) : NULL;
}
const char *qa_app_frontend_preferences_save_global(qa_app_frontend_preferences *p) {
  if (!p->host.has_shared_settings) return NULL;
  return p->host.save_global ? p->host.save_global(p->host.ctx, p) : NULL;
}
const char *qa_app_frontend_preferences_save_audio_baseline(qa_app_frontend_preferences *p) {
  return p->host.save_audio_baseline ? p->host.save_audio_baseline(p->host.ctx, p) : NULL;
}
