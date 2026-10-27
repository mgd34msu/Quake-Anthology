/*
 * Port of src/ui/settings/index.ts: SettingBinding, cvar-backed
 * settings, the settings menu tree, and the input/audio/gamepad
 * binding factories. Also the umbrella for ui/settings re-exports.
 * C accommodations:
 *  - SettingCvars (a Pick<CvarRegistry>) is a qa_cvar_registry *.
 *  - SettingBinding closures become fn ptr + one ctx. read_str /
 *    format_value return strings borrowed until the next call on
 *    the same binding. choices returns a borrowed array + count.
 *  - Factories returning binding arrays return a
 *    qa_ui_setting_bindings owner freed with
 *    qa_ui_setting_bindings_free. bindCvarSetting's single binding
 *    is freed with qa_ui_cvar_setting_binding_dispose.
 *  - SettingsValueService<T>.write(Partial<T>) becomes a write of
 *    the full struct plus a field-mask word.
 *  - Plain TS Errors abort with the identical message
 *    (qa_range_error), as in the landed cvar port.
 * GPL-2.0-or-later.
 */
#ifndef QA_UI_SETTINGS_INDEX_H
#define QA_UI_SETTINGS_INDEX_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/audio/output.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/render.h"
#include "qa/contracts/ui.h"
#include "qa/core/cvars/index.h"
#include "qa/input/gamepad.h"
#include "qa/input/mouse.h"
#include "qa/input/seat.h"
#include "qa/input/user_command.h"
#include "qa/settings/restart.h"
#include "qa/ui/common/controller.h"

/** TS SettingCategory (exact strings). */
typedef const char *qa_ui_setting_category;

/** TS SettingBinding. kind: "toggle"|"slider"|"choice"|"text-entry"|"button". */
typedef struct qa_ui_setting_binding {
  qa_ui_control_id id;
  const char *label;
  qa_ui_setting_category category;
  const char *group; /* NULL when absent */
  const char *kind;
  void *ctx;
  bool (*enabled)(void *ctx);
  bool (*read_bool)(void *ctx);              /* toggle */
  void (*write_bool)(void *ctx, bool value);
  double (*read_num)(void *ctx);             /* slider */
  void (*write_num)(void *ctx, double value);
  const char *(*format_value)(void *ctx, double value); /* optional */
  double minimum, maximum, step;
  const char *(*read_str)(void *ctx);        /* choice/text-entry */
  void (*write_str)(void *ctx, const char *value);
  const qa_ui_choice *(*choices)(void *ctx, size_t *out_count);
  double maximum_length;                     /* text-entry */
  bool has_commit;
  void (*commit_submit)(void *ctx, const char *value);
  void (*commit_cancel)(void *ctx);
  void (*activate)(void *ctx);               /* button */
} qa_ui_setting_binding;

/** Owned binding array returned by the bind* factories. */
typedef struct qa_ui_setting_bindings {
  qa_ui_setting_binding *items;
  size_t count;
  void *owner;
} qa_ui_setting_bindings;
void qa_ui_setting_bindings_free(qa_ui_setting_bindings *bindings);
/** Wrap state-owned items; teardown frees state on free. */
qa_ui_setting_bindings qa_ui_setting_bindings_own(
    qa_ui_setting_binding *items, size_t count, void *state,
    void (*teardown)(void *state));

/** TS settingControl(binding, rect, seat). The control borrows the
 * binding (ctx = binding) and valueLabel storage in label_scratch:
 * caller supplies a buffer of at least 64 bytes kept alive with the
 * control; may be NULL when the binding has no format_value. */
qa_ui_control qa_ui_setting_control(const qa_ui_setting_binding *binding,
                                    qa_rect rect, qa_seat_id seat,
                                    char *label_scratch, size_t scratch_size);

/** TS CvarSettingSpec. restart meaningful when has_restart. */
typedef struct qa_ui_cvar_setting_spec {
  const char *name;
  const char *label;
  qa_ui_setting_category category;
  bool has_restart;
  qa_restart_kind restart;
  const char *group; /* NULL when absent */
  const char *kind;  /* toggle|slider|choice|text-entry */
  double minimum, maximum, step;
  const qa_ui_choice *choices;
  size_t choice_count;
  double maximum_length;
  bool submit_only;
} qa_ui_cvar_setting_spec;

/** TS bindCvarSetting. False only on OOM. */
bool qa_ui_bind_cvar_setting(qa_cvar_registry *registry,
                             const qa_ui_cvar_setting_spec *spec,
                             qa_restart_controls *restarts,
                             qa_ui_setting_binding *out);
void qa_ui_cvar_setting_binding_dispose(qa_ui_setting_binding *binding);

/** TS SettingsMenus {root, dispose()}. */
typedef struct qa_ui_settings_menus qa_ui_settings_menus;
struct qa_ui_settings_menus {
  qa_ui_menu_id root;
  void *state;
  void (*dispose_fn)(qa_ui_settings_menus *menus);
};
void qa_ui_settings_menus_dispose(qa_ui_settings_menus *menus);

/** TS registerSettingsMenus reset option. */
typedef struct qa_ui_settings_reset {
  void *ctx;
  const char *label;
  void (*apply)(void *ctx);
} qa_ui_settings_reset;

struct qa_ui_llm_settings_menus; /* ui/settings/llm.h */
/** Bindings are borrowed and must outlive the menus. llm_root NULL
 * omits the LLM entry; llm_dispose (with llm_menus) is run on
 * dispose when non-NULL, mirroring the TS llmMenu.dispose. */
qa_ui_settings_menus qa_ui_register_settings_menus(
    qa_native_ui_controller *controller,
    const qa_ui_setting_binding *bindings, size_t binding_count,
    qa_ui_menu_id llm_root, void *llm_menus,
    void (*llm_dispose)(void *llm_menus),
    const qa_ui_settings_reset *reset);

/** TS PrimaryInputSettings / field mask for Partial writes. */
typedef struct qa_ui_primary_input_settings {
  double sensitivity, pitch, yaw;
  bool invert_mouse, always_run;
} qa_ui_primary_input_settings;
enum {
  QA_UI_PRIMARY_SENSITIVITY = 1u << 0, QA_UI_PRIMARY_PITCH = 1u << 1,
  QA_UI_PRIMARY_YAW = 1u << 2, QA_UI_PRIMARY_INVERT = 1u << 3,
  QA_UI_PRIMARY_ALWAYS_RUN = 1u << 4
};
typedef struct qa_ui_primary_input_service {
  void *ctx;
  qa_ui_primary_input_settings (*read)(void *ctx);
  void (*write)(void *ctx, qa_ui_primary_input_settings values, unsigned mask);
} qa_ui_primary_input_service;

/** TS MouseMotionSettings = Pick<MouseTuning,...>. */
typedef struct qa_ui_mouse_motion_settings {
  double acceleration;
  bool filter, free_look, look_spring, look_strafe;
} qa_ui_mouse_motion_settings;
enum {
  QA_UI_MOTION_ACCELERATION = 1u << 0, QA_UI_MOTION_FILTER = 1u << 1,
  QA_UI_MOTION_FREE_LOOK = 1u << 2, QA_UI_MOTION_LOOK_SPRING = 1u << 3,
  QA_UI_MOTION_LOOK_STRAFE = 1u << 4
};
typedef struct qa_ui_mouse_motion_service {
  void *ctx;
  qa_ui_mouse_motion_settings (*read)(void *ctx);
  void (*write)(void *ctx, qa_ui_mouse_motion_settings values, unsigned mask);
} qa_ui_mouse_motion_service;

/** TS ControllerVibrationSettings. */
typedef struct qa_ui_controller_vibration_settings {
  bool controller_vibration;
  double controller_vibration_strength;
} qa_ui_controller_vibration_settings;
enum { QA_UI_VIBRATION_ON = 1u << 0, QA_UI_VIBRATION_STRENGTH = 1u << 1 };
typedef struct qa_ui_controller_vibration_service {
  void *ctx;
  qa_ui_controller_vibration_settings (*read)(void *ctx);
  void (*write)(void *ctx, qa_ui_controller_vibration_settings values,
                unsigned mask);
} qa_ui_controller_vibration_service;

/** TS AudioSettings. */
typedef struct qa_ui_audio_settings {
  double effects_volume, music_volume;
} qa_ui_audio_settings;
enum { QA_UI_AUDIO_EFFECTS = 1u << 0, QA_UI_AUDIO_MUSIC = 1u << 1 };
typedef struct qa_ui_audio_settings_service {
  void *ctx;
  qa_ui_audio_settings (*read)(void *ctx);
  void (*write)(void *ctx, qa_ui_audio_settings values, unsigned mask);
} qa_ui_audio_settings_service;

/** TS AudioOutputSettings. select/format_select return an error
 * message (borrowed) or NULL, mirroring the TS try/catch report. */
typedef struct qa_ui_audio_output_settings {
  void *ctx;
  bool has_format;
  qa_audio_output_format (*format_read)(void *ctx);
  const char *(*format_select)(void *ctx, qa_audio_output_format format);
  const char *(*selected)(void *ctx); /* NULL is TS null */
  const char *const *(*devices)(void *ctx, size_t *out_count);
  const char *(*select)(void *ctx, const char *name); /* name nullable */
  void (*report)(void *ctx, const char *message);
} qa_ui_audio_output_settings;

qa_ui_setting_bindings qa_ui_bind_controller_vibration(
    const qa_ui_controller_vibration_service *service);
qa_ui_setting_bindings qa_ui_bind_primary_input_settings(
    const qa_ui_primary_input_service *service);
/** spring_available NULL selects the TS default () => true. */
qa_ui_setting_bindings qa_ui_bind_mouse_motion_settings(
    const qa_ui_mouse_motion_service *service,
    bool (*spring_available)(void *ctx), void *spring_ctx);
/** tracks NULL omits the menu-track choice; registry NULL yields none. */
qa_ui_setting_bindings qa_ui_bind_music_playlist_settings(
    qa_cvar_registry *registry,
    const char *const *(*tracks)(void *ctx, size_t *out_count), void *tracks_ctx);
qa_ui_setting_bindings qa_ui_bind_audio_geometry_settings(
    qa_cvar_registry *registry);
/** output NULL omits device/format bindings. */
qa_ui_setting_bindings qa_ui_bind_audio_settings(
    const qa_ui_audio_settings_service *service,
    const qa_ui_audio_output_settings *output);
/** vibration NULL omits the vibration bindings. */
qa_ui_setting_bindings qa_ui_bind_input_settings(
    qa_seat_input *input, qa_input_command_builder *builder,
    const qa_ui_controller_vibration_service *vibration);
/** TS bindGamepadSettings over a seat input (the () => SeatInput
 * form reads through get_input when non-NULL). */
qa_ui_setting_bindings qa_ui_bind_gamepad_settings(qa_seat_input *input);
qa_ui_setting_bindings qa_ui_bind_gamepad_settings_fn(
    qa_seat_input *(*get_input)(void *ctx), void *ctx);

/** TS UiPreferenceValues. typeface: "standard"|"bold"; color_mode:
 * "standard"|"blue-yellow"|"monochrome". */
typedef struct qa_ui_preference_values {
  double hud_scale, text_scale, menu_scale;
  bool high_contrast, reduced_flashes, captions, crosshair;
  double crosshair_size;
  const char *typeface;
  const char *color_mode;
} qa_ui_preference_values;

#define QA_UI_PREFERENCE_BINDING_COUNT 11
/** TS SeatUiPreferences. cvars NULL keeps local values. */
typedef struct qa_ui_seat_preferences {
  qa_seat_id seat;
  qa_cvar_registry *cvars;
  qa_ui_preference_values local;
  qa_ui_setting_binding bindings[QA_UI_PREFERENCE_BINDING_COUNT];
  void *binding_ctx[QA_UI_PREFERENCE_BINDING_COUNT];
} qa_ui_seat_preferences;
void qa_ui_seat_preferences_init(qa_ui_seat_preferences *prefs, qa_seat_id seat,
                                 qa_cvar_registry *cvars);
qa_ui_preference_values qa_ui_seat_preferences_values(
    const qa_ui_seat_preferences *prefs);
void qa_ui_seat_preferences_set_values(qa_ui_seat_preferences *prefs,
                                       qa_ui_preference_values values);
/** TS prefs.bindings(): prefs-owned array of 11. */
const qa_ui_setting_binding *qa_ui_seat_preferences_bindings(
    qa_ui_seat_preferences *prefs, size_t *out_count);

#endif
