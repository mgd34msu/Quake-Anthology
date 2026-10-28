/* Port of src/app/bootstrap/frontend-preferences.ts. Partial<> maps
 * to has_* flags; seat/input/audio application goes through host
 * get/set rows over the frontend value set. The 100 ms change timer
 * becomes an explicit pump timestamp. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_FRONTEND_PREFERENCES_H
#define QA_APP_BOOTSTRAP_FRONTEND_PREFERENCES_H
#include <stdbool.h>
#include "qa/contracts/common.h"
/* TS FrontendPreferenceValues (all fields) / Overrides (has_* gates). */
typedef struct qa_app_frontend_preference_values {
  bool has_effects_volume; double effects_volume;
  bool has_music_volume; double music_volume;
  bool has_controller_vibration; bool controller_vibration;
  bool has_controller_vibration_strength; double controller_vibration_strength;
  bool has_sensitivity; double sensitivity;
  bool has_pitch; double pitch;
  bool has_yaw; double yaw;
  bool has_invert_mouse; bool invert_mouse;
  bool has_acceleration; bool acceleration;
  bool has_filter; bool filter;
  bool has_look_spring; bool look_spring;
  bool has_look_strafe; bool look_strafe;
  bool has_free_look; bool free_look;
  bool has_always_run; bool always_run;
} qa_app_frontend_preference_values;
typedef qa_app_frontend_preference_values qa_app_frontend_preference_overrides;
/* TS changedFrontendPreferences(before, after, selected). */
qa_app_frontend_preference_overrides qa_app_changed_frontend_preferences(qa_app_frontend_preference_values before,
    qa_app_frontend_preference_values after, qa_app_frontend_preference_overrides selected);
/* TS frontendMouseTuning merge onto a mouse row (frontend fields only). */
typedef struct qa_app_frontend_mouse {
  double sensitivity, pitch, yaw;
  bool invert_pitch, acceleration, filter;
  bool has_look_spring; bool look_spring;
  bool has_look_strafe; bool look_strafe;
  bool free_look;
} qa_app_frontend_mouse;
qa_app_frontend_mouse qa_app_frontend_mouse_tuning(qa_app_frontend_preference_overrides values, qa_app_frontend_mouse mouse);
/* TS FrontendPreferences class state (bindings/persistence via host). */
typedef struct qa_app_frontend_preferences qa_app_frontend_preferences;
typedef struct qa_app_frontend_preferences_host {
  void *ctx;
  qa_command_dialect (*dialect)(void *ctx);
  bool has_shared_settings;
  /* loadBaseline/saveGlobal/saveAudioBaseline storage steps. */
  const char *(*load_baseline)(void *ctx, qa_app_frontend_preferences *preferences);
  const char *(*save_global)(void *ctx, qa_app_frontend_preferences *preferences);
  const char *(*save_audio_baseline)(void *ctx, qa_app_frontend_preferences *preferences);
  void (*on_change)(void *ctx);
} qa_app_frontend_preferences_host;
qa_app_frontend_preferences *qa_app_frontend_preferences_create(qa_app_frontend_preferences_host host);
void qa_app_frontend_preferences_destroy(qa_app_frontend_preferences *preferences);
qa_app_frontend_preference_overrides *qa_app_frontend_preferences_values(qa_app_frontend_preferences *preferences);
/* TS audioValues getter. */
void qa_app_frontend_preferences_audio_values(const qa_app_frontend_preferences *preferences, double *effects, double *music);
void qa_app_frontend_preferences_set_audio_baseline(qa_app_frontend_preferences *preferences, bool has_effects, double effects, bool has_music, double music);
void qa_app_frontend_preferences_set_baselines(qa_app_frontend_preferences *preferences, qa_app_frontend_mouse mouse,
                                               bool has_always_run, bool always_run, bool vibration, double vibration_strength);
qa_app_frontend_preference_overrides qa_app_frontend_preferences_effective_values(const qa_app_frontend_preferences *preferences);
/* Notify coalescing: mark changed; pump fires on_change at +100 ms. */
void qa_app_frontend_preferences_changed(qa_app_frontend_preferences *preferences, double now_milliseconds);
void qa_app_frontend_preferences_pump(qa_app_frontend_preferences *preferences, double now_milliseconds);
const char *qa_app_frontend_preferences_load_baseline(qa_app_frontend_preferences *preferences);
const char *qa_app_frontend_preferences_save_global(qa_app_frontend_preferences *preferences);
const char *qa_app_frontend_preferences_save_audio_baseline(qa_app_frontend_preferences *preferences);
#endif
