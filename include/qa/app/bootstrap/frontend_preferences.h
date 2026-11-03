/* Port of src/app/bootstrap/frontend-preferences.ts. Partial<> maps
 * to has_* flags. The 100 ms change timer becomes a deadline that
 * qa_app_frontend_preferences_pump fires. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_FRONTEND_PREFERENCES_H
#define QA_APP_BOOTSTRAP_FRONTEND_PREFERENCES_H
#include <stdbool.h>
#include "qa/contracts/common.h"
#include "qa/app/bootstrap/audio/playlist_settings.h"
#include "qa/app/bootstrap/audio_settings.h"
#include "qa/settings/config.h"
#include "qa/ui/settings/index.h"
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
  bool has_acceleration; double acceleration; /* TS MouseTuning.acceleration: number */
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
/* TS frontendMouseTuning(values, mouse). */
qa_mouse_tuning qa_app_frontend_mouse_tuning(qa_app_frontend_preference_overrides values, qa_mouse_tuning mouse);
/* TS frontendSeatSettings(values, saved): copies saved (strings shared with saved). */
qa_seat_settings qa_app_frontend_seat_settings(qa_app_frontend_preference_overrides values, const qa_seat_settings *saved);
/* TS FrontendPreferences. The ConfigStore methods run synchronously and raise
 * (qa_raise) where the TS promises reject. */
typedef struct qa_app_frontend_preferences qa_app_frontend_preferences;
/* TS new FrontendPreferences(dialect, sharedSettings?). shared_settings NULL is
 * TS undefined; its root is copied. NULL only on OOM. */
qa_app_frontend_preferences *qa_app_frontend_preferences_create(qa_command_dialect (*dialect)(void *ctx), void *dialect_ctx,
    const qa_config_store *shared_settings);
void qa_app_frontend_preferences_destroy(qa_app_frontend_preferences *preferences);
/* TS onChange field (fn NULL is TS null). */
void qa_app_frontend_preferences_set_on_change(qa_app_frontend_preferences *preferences, void (*fn)(void *ctx), void *ctx);
qa_app_frontend_preference_overrides *qa_app_frontend_preferences_values(qa_app_frontend_preferences *preferences);
/* TS audioBaseline: NULL is TS {} (no saved document); the record is owned by
 * the preferences until the next load or save. */
const qa_app_audio_preferences *qa_app_frontend_preferences_audio_baseline(const qa_app_frontend_preferences *preferences);
/* TS audioValues getter. */
void qa_app_frontend_preferences_audio_values(const qa_app_frontend_preferences *preferences, double *effects, double *music);
/* TS effectiveValues(). */
qa_app_frontend_preference_overrides qa_app_frontend_preferences_effective_values(const qa_app_frontend_preferences *preferences);
/* TS onChange coalescing: the 100 ms setTimeout becomes a deadline on
 * qa_app_performance_now() that pump fires once it has passed. */
void qa_app_frontend_preferences_pump(qa_app_frontend_preferences *preferences);
/* TS loadBaseline(settings) / saveGlobal() / saveAudioBaseline(settings). */
void qa_app_frontend_preferences_load_baseline(qa_app_frontend_preferences *preferences, const qa_config_store *settings);
void qa_app_frontend_preferences_save_global(qa_app_frontend_preferences *preferences);
void qa_app_frontend_preferences_save_audio_baseline(qa_app_frontend_preferences *preferences, const qa_config_store *settings);
/* TS bindings(output?, musicSettings?). output NULL is TS undefined (copied);
 * music_settings NULL is TS undefined and returns an owned menu_track. The
 * result is owned by the caller (qa_ui_setting_bindings_free) and reads the
 * preferences, which must outlive it. */
qa_ui_setting_bindings qa_app_frontend_preferences_bindings(qa_app_frontend_preferences *preferences,
    const qa_ui_audio_output_settings *output,
    qa_app_audio_music_preferences (*music_settings)(void *ctx), void *music_ctx);
/* TS readFrontendInput(local) / readFrontendPreferences(input, audio).
 * readFrontendPreferences returns false for TS null (no local seat). */
struct qa_app_local_input;
struct qa_app_application_input;
struct qa_app_application_audio;
qa_app_frontend_preference_values qa_app_read_frontend_input(const struct qa_app_local_input *local);
bool qa_app_read_frontend_preferences(const struct qa_app_application_input *input, const struct qa_app_application_audio *audio,
    qa_app_frontend_preference_values *out);
#endif
