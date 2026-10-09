#ifndef QA_INPUT_H
#define QA_INPUT_H

#include "qa/console.h"
#include "qa/movement.h"

#define QA_INPUT_LOCAL_SEATS 4

typedef struct qa_input_pair {
    float x, y;
} qa_input_pair;
typedef enum qa_button_timing { QA_BUTTON_Q1, QA_BUTTON_Q2, QA_BUTTON_Q3 } qa_button_timing;

/* Zero initializes a button. A source identifies one physical press for this
 * seat. Clear retains storage; destroy releases it. Do not copy live buttons.
 */
typedef struct qa_input_button {
    uint64_t inline_sources[4], *sources;
    size_t count, capacity;
    double down_ms, held_ms;
    bool pressed, released;
} qa_input_button;
bool qa_input_button_down(qa_input_button *, uint64_t source, double time_ms, qa_error *);
void qa_input_button_up(qa_input_button *, uint64_t source, double time_ms, double missing_ms);
void qa_input_button_release(qa_input_button *, double time_ms);
bool qa_input_button_sample(qa_input_button *, qa_button_timing, double now_ms, double frame_ms,
                            float *out, qa_error *);
void qa_input_button_clear(qa_input_button *);
void qa_input_button_destroy(qa_input_button *);

typedef struct qa_mouse_tuning {
    float sensitivity, acceleration, yaw, pitch, side, forward;
    bool filter, free_look, look_spring, look_strafe, invert_pitch;
} qa_mouse_tuning;
typedef struct qa_mouse_input {
    qa_input_pair previous;
} qa_mouse_input;
typedef struct qa_mouse_move {
    float yaw, pitch, side, forward;
} qa_mouse_move;
qa_mouse_tuning qa_mouse_defaults(void);
bool qa_mouse_tuning_valid(const qa_mouse_tuning *);
bool qa_mouse_sample(qa_mouse_input *, const qa_mouse_tuning *, qa_input_pair raw, double frame_ms,
                     bool strafe, bool mouse_look, float zoom, qa_mouse_move *out, qa_error *);

typedef struct qa_pitch_drift {
    bool drifting;
    float velocity, moving_seconds;
} qa_pitch_drift;
typedef struct qa_pitch_drift_input {
    bool grounded, disabled, manual, start;
    float ideal_pitch, forward, threshold, speed, delay;
} qa_pitch_drift_input;
float qa_pitch_drift_sample(qa_pitch_drift *, float pitch, float seconds,
                            const qa_pitch_drift_input *);

typedef enum qa_stick_kind { QA_STICK_RADIAL, QA_STICK_AXIAL } qa_stick_kind;
typedef struct qa_stick_curve {
    qa_stick_kind kind;
    float deadzone, outer_threshold, exponent;
} qa_stick_curve;
typedef enum qa_controller_axis {
    QA_AXIS_LEFT_X,
    QA_AXIS_LEFT_Y,
    QA_AXIS_RIGHT_X,
    QA_AXIS_RIGHT_Y,
    QA_AXIS_LEFT_TRIGGER,
    QA_AXIS_RIGHT_TRIGGER,
    QA_AXIS_COUNT
} qa_controller_axis;
typedef enum qa_gyro_yaw_axis { QA_GYRO_YAW_Y, QA_GYRO_YAW_Z } qa_gyro_yaw_axis;
typedef struct qa_gamepad_tuning {
    qa_stick_curve move, look;
    float yaw_speed, pitch_speed, forward_sensitivity, side_sensitivity, trigger_threshold;
    float gyro_yaw_sensitivity, gyro_pitch_sensitivity;
    qa_gyro_yaw_axis gyro_yaw_axis;
    bool swap_sticks, invert_pitch, gyro_enabled;
} qa_gamepad_tuning;
typedef struct qa_gyro_capture {
    double start_ms, last_ms;
    uint64_t samples;
    qa_vec3 mean, deviation;
} qa_gyro_capture;
typedef struct qa_gamepad_input {
    float axes[QA_AXIS_COUNT], preview_axes[QA_AXIS_COUNT];
    qa_vec3 gyro_sample, gyro_bias;
    bool has_sample, has_bias, calibrating;
    qa_gyro_capture capture;
} qa_gamepad_input;
typedef struct qa_gamepad_sample {
    qa_input_pair move, look_degrees;
} qa_gamepad_sample;
typedef struct qa_gamepad_preview {
    qa_input_pair move_raw, move_curved, look_raw, look_curved;
} qa_gamepad_preview;
typedef enum qa_gyro_state { QA_GYRO_IDLE, QA_GYRO_CALIBRATING, QA_GYRO_READY } qa_gyro_state;
typedef struct qa_gyro_status {
    qa_gyro_state state;
    float progress;
    uint64_t samples;
    qa_vec3 bias;
} qa_gyro_status;
qa_gamepad_tuning qa_gamepad_defaults(void);
bool qa_gamepad_tuning_valid(const qa_gamepad_tuning *);
qa_input_pair qa_stick_apply(qa_input_pair, const qa_stick_curve *);
float qa_controller_axis_normalize(qa_controller_axis, int16_t raw);
bool qa_gamepad_axis(qa_gamepad_input *, qa_controller_axis, float value, bool aiming, qa_error *);
void qa_gamepad_preview_read(const qa_gamepad_input *, const qa_gamepad_tuning *,
                             qa_gamepad_preview *);
void qa_gamepad_calibration_begin(qa_gamepad_input *);
void qa_gamepad_calibration_cancel(qa_gamepad_input *);
void qa_gamepad_calibration_reset(qa_gamepad_input *);
qa_gyro_status qa_gamepad_calibration_status(const qa_gamepad_input *);
bool qa_gamepad_gyro(qa_gamepad_input *, qa_vec3 radians_per_second, double time_ms, bool aiming,
                     qa_error *);
bool qa_gamepad_sample_read(const qa_gamepad_input *, const qa_gamepad_tuning *, double frame_ms,
                            qa_gamepad_sample *out, qa_error *);
void qa_gamepad_clear(qa_gamepad_input *);

typedef enum qa_input_action {
    QA_INPUT_FORWARD,
    QA_INPUT_BACK,
    QA_INPUT_MOVE_LEFT,
    QA_INPUT_MOVE_RIGHT,
    QA_INPUT_MOVE_UP,
    QA_INPUT_MOVE_DOWN,
    QA_INPUT_TURN_LEFT,
    QA_INPUT_TURN_RIGHT,
    QA_INPUT_LOOK_UP,
    QA_INPUT_LOOK_DOWN,
    QA_INPUT_JUMP,
    QA_INPUT_CROUCH,
    QA_INPUT_ATTACK,
    QA_INPUT_USE,
    QA_INPUT_HOLSTER,
    QA_INPUT_WALK,
    QA_INPUT_STRAFE,
    QA_INPUT_KLOOK,
    QA_INPUT_MLOOK,
    QA_INPUT_BUTTON0,
    QA_INPUT_BUTTON1,
    QA_INPUT_BUTTON2,
    QA_INPUT_BUTTON3,
    QA_INPUT_BUTTON4,
    QA_INPUT_BUTTON5,
    QA_INPUT_BUTTON6,
    QA_INPUT_BUTTON7,
    QA_INPUT_BUTTON8,
    QA_INPUT_BUTTON9,
    QA_INPUT_BUTTON10,
    QA_INPUT_BUTTON11,
    QA_INPUT_BUTTON12,
    QA_INPUT_BUTTON13,
    QA_INPUT_BUTTON14,
    QA_INPUT_SCORES,
    QA_INPUT_NEXT_WEAPON,
    QA_INPUT_PREVIOUS_WEAPON,
    QA_INPUT_MENU,
    QA_INPUT_ACTION_COUNT
} qa_input_action;
typedef struct qa_input_action_sample {
    float fraction;
    bool active, pressed;
} qa_input_action_sample;
typedef struct qa_seat_input_sample {
    qa_input_action_sample buttons[QA_INPUT_ACTION_COUNT];
    qa_input_pair mouse;
    qa_gamepad_sample gamepad;
    double frame_ms;
    bool game_focus, any_key_down;
    uint8_t impulse;
} qa_seat_input_sample;
typedef struct qa_view_input_tuning {
    float forward_speed, back_speed, side_speed, up_speed, yaw_speed, pitch_speed;
    float angle_multiplier, move_multiplier;
    bool always_run;
} qa_view_input_tuning;
typedef struct qa_input_command_frame {
    qa_ruleset_id kind;
    uint64_t sequence;
    double acknowledged_server_seconds;
    qa_vec3 delta_angles;
    double server_frame, server_time_ms;
    double light_level, weapon;
    int32_t delta_angle_words[3];
    float sensitivity;
    bool attack_allowed, has_pitch_drift, grounded, drift_disabled;
    float ideal_pitch;
} qa_input_command_frame;
typedef struct qa_input_command_builder {
    qa_ruleset_id kind;
    qa_vec3 angles;
    qa_mouse_input mouse;
    qa_pitch_drift drift;
    bool previous_mouse_look;
    uint8_t pending_impulse;
} qa_input_command_builder;
typedef struct qa_input_command_tuning {
    qa_view_input_tuning view;
    qa_mouse_tuning mouse;
    float drift_speed, drift_delay;
} qa_input_command_tuning;
typedef struct qa_input_tuning_handles {
    qa_cvar_handle sensitivity, acceleration, filter, yaw, pitch, side, forward;
    qa_cvar_handle free_look, look_spring, look_strafe, drift_speed, drift_delay, always_run;
    qa_cvar_handle forward_speed, back_speed, side_speed, up_speed;
    qa_cvar_handle yaw_speed, pitch_speed, angle_multiplier, move_multiplier;
} qa_input_tuning_handles;
/* Physical moves use power-of-two units so normalization loses no source
 * bits. Directional sources supply a local unit direction and source speed. */
typedef struct qa_input_move_intent { double x, y, z; } qa_input_move_intent;
typedef struct qa_input_command_intent {
    qa_vec3 angles, direction;
    qa_input_move_intent move;
    float speed;
    uint64_t actions;
    bool directional, walking, game_focus, any_key_down;
    uint8_t impulse;
} qa_input_command_intent;
typedef enum qa_input_command_encoding {
    QA_INPUT_COMMAND_NATIVE, QA_INPUT_COMMAND_UNIFIED, QA_INPUT_COMMAND_SOURCE_Q3
} qa_input_command_encoding;
typedef struct qa_input_usercmd {
    qa_ruleset_id kind;
    uint64_t sequence;
    qa_vec3 angles, move;
    int32_t angle_words[3];
    uint32_t buttons;
    double milliseconds, server_time_ms, server_frame, acknowledged_server_seconds;
    double light_level, weapon;
    uint8_t impulse;
} qa_input_usercmd;
bool qa_input_command_sample(qa_input_command_builder *, const qa_input_command_tuning *,
    const qa_seat_input_sample *, const qa_input_command_frame *, double source_frame_ms,
    qa_input_command_intent *, qa_error *);
void qa_input_usercmd_build(const qa_input_command_intent *, const qa_input_command_frame *,
    double source_frame_ms, qa_input_command_encoding, qa_input_usercmd *);
void qa_input_usercmd_project(const qa_input_usercmd *, qa_movement_command *);

typedef struct qa_input_command_basis {
    qa_ruleset_id kind;
    double units;
    qa_vec3 delta_angles;
    int32_t delta_words[3];
    bool words, relative, wrap_words, repack_words, wide_delta;
} qa_input_command_basis;
typedef enum qa_input_axis_quantization {
    QA_INPUT_AXIS_EXACT, QA_INPUT_AXIS_TRUNCATE, QA_INPUT_AXIS_NEAREST, QA_INPUT_AXIS_SHORT
} qa_input_axis_quantization;
typedef struct qa_input_axis_rule {
    qa_input_axis_quantization quantization;
    float minimum, maximum;
    bool clamp, ratio_first, float_product;
} qa_input_axis_rule;
float qa_input_command_units(qa_ruleset_id);
void qa_input_command_convert(const qa_movement_command *, const qa_input_move_intent *,
    const qa_input_command_basis *, const qa_input_command_basis *, qa_input_axis_rule, qa_movement_command *);

qa_input_command_tuning qa_input_command_defaults(qa_ruleset_id);
void qa_input_command_clear(qa_input_command_builder *);
bool qa_input_command_angles(qa_input_command_builder *, qa_vec3, qa_error *);
/* Replaces the pending NQ/QW impulse. Only a successful command build consumes it. */
bool qa_input_command_impulse(qa_input_command_builder *, int32_t, qa_error *);
void qa_input_command_center(qa_input_command_builder *, float delta_pitch);
/* Settings are sampled once by the seat owner, including its cvar bindings.
 * A failed build leaves both builder and output unchanged. */
bool qa_input_command_build(qa_input_command_builder *, const qa_input_command_tuning *,
                            const qa_seat_input_sample *, const qa_input_command_frame *,
                            double source_frame_ms, qa_movement_command *out, qa_error *);

enum qa_key_code {
    QA_KEY_TAB = 9,
    QA_KEY_ENTER = 13,
    QA_KEY_ESCAPE = 27,
    QA_KEY_SPACE = 32,
    QA_KEY_BACKSPACE = 127,
    QA_KEY_COMMAND,
    QA_KEY_CAPSLOCK,
    QA_KEY_POWER,
    QA_KEY_PAUSE,
    QA_KEY_UP,
    QA_KEY_DOWN,
    QA_KEY_LEFT,
    QA_KEY_RIGHT,
    QA_KEY_ALT,
    QA_KEY_CONTROL,
    QA_KEY_SHIFT,
    QA_KEY_INSERT,
    QA_KEY_DELETE,
    QA_KEY_PAGEDOWN,
    QA_KEY_PAGEUP,
    QA_KEY_HOME,
    QA_KEY_END,
    QA_KEY_F1,
    QA_KEY_F12 = QA_KEY_F1 + 11,
    QA_KEY_F15 = QA_KEY_F1 + 14,
    QA_KEY_KP_HOME,
    QA_KEY_KP_UP,
    QA_KEY_KP_PAGEUP,
    QA_KEY_KP_LEFT,
    QA_KEY_KP_5,
    QA_KEY_KP_RIGHT,
    QA_KEY_KP_END,
    QA_KEY_KP_DOWN,
    QA_KEY_KP_PAGEDOWN,
    QA_KEY_KP_ENTER,
    QA_KEY_KP_INSERT,
    QA_KEY_KP_DELETE,
    QA_KEY_KP_SLASH,
    QA_KEY_KP_MINUS,
    QA_KEY_KP_PLUS,
    QA_KEY_KP_NUMLOCK,
    QA_KEY_KP_STAR,
    QA_KEY_KP_EQUALS,
    QA_KEY_MOUSE1,
    QA_KEY_MOUSE5 = QA_KEY_MOUSE1 + 4,
    QA_KEY_WHEEL_DOWN,
    QA_KEY_WHEEL_UP,
    QA_KEY_JOY1,
    QA_KEY_JOY32 = QA_KEY_JOY1 + 31,
    QA_KEY_AUX1,
    QA_KEY_AUX16 = QA_KEY_AUX1 + 15,
    QA_KEY_AUX17 = 256,
    QA_KEY_AUX32 = 271,
    QA_KEY_CHAR_FLAG = 1024
};
int qa_input_key_parse(const char *);
/* A key name occupies at most 32 bytes including NUL. */
const char *qa_input_key_name(int key, char out[32]);
int qa_input_source_key(int key, qa_ruleset_id);
int qa_input_sdl_key(int32_t keycode, uint16_t modifiers);
double qa_input_event_time(uint32_t timestamp, uint32_t ticks, double now_ms, bool subframe);
unsigned qa_input_mouse_button(unsigned physical);

typedef enum qa_physical_kind {
    QA_PHYSICAL_KEY,
    QA_PHYSICAL_MOUSE,
    QA_PHYSICAL_BUTTON,
    QA_PHYSICAL_AXIS
} qa_physical_kind;
typedef struct qa_physical_input {
    qa_physical_kind kind;
    int32_t device;
    uint32_t code;
    bool positive;
} qa_physical_input;
bool qa_input_physical_parse(const char *, int32_t device, qa_physical_input *out);
bool qa_input_physical_valid(qa_physical_input);
bool qa_input_physical_name(qa_physical_input, char *out, size_t size);
/* Canonical held command, including +; NULL for an unknown action. */
const char *qa_input_action_command(qa_input_action);
typedef enum qa_binding_kind { QA_BIND_ACTION, QA_BIND_COMMAND } qa_binding_kind;
typedef struct qa_input_binding {
    qa_physical_input input;
    qa_binding_kind kind;
    qa_input_action action;
    const char *command;
} qa_input_binding;
typedef enum qa_input_focus {
    QA_INPUT_GAME,
    QA_INPUT_CONSOLE,
    QA_INPUT_CHAT,
    QA_INPUT_UI
} qa_input_focus;
typedef enum qa_input_event_kind {
    QA_INPUT_EVENT_KEY,
    QA_INPUT_EVENT_TEXT,
    QA_INPUT_EVENT_MOUSE,
    QA_INPUT_EVENT_WHEEL,
    QA_INPUT_EVENT_BUTTON,
    QA_INPUT_EVENT_AXIS,
    QA_INPUT_EVENT_FOCUS,
    QA_INPUT_EVENT_TOUCH
} qa_input_event_kind;
typedef struct qa_input_event {
    qa_input_event_kind kind;
    double time_ms;
    qa_physical_input input;
    bool down, repeat;
    float value;
    qa_input_pair position, delta;
    const char *text;
    int touchpad, finger;
} qa_input_event;
typedef struct qa_input_seat qa_input_seat;
typedef uint64_t qa_input_ui_token;
typedef bool (*qa_input_ui_handler)(void *, qa_input_seat *, qa_input_focus,
                                    const qa_input_event *);
typedef struct qa_input_seat_options {
    /* Physical local route ordinal. context.seat is its authored launch ID. */
    uint32_t seat;
    qa_command_context context;
    qa_console *console;
    qa_cvars *cvars;
    qa_gamepad_tuning gamepad;
    qa_input_ui_handler ui;
    void *ui_user;
    /* Source catchers run before the native UI stack. Consumed events still
     * update physical key state and retire releases; this is one dispatcher. */
    bool (*before_ui)(void *, qa_input_seat *, const qa_input_event *, bool *consumed, qa_error *);
    void *before_ui_user;
    /* Pure owner qualification against the actual preparing/current choices.
     * Required for authored IDs or captured source contexts; callbackless
     * construction admits only an uncaptured ENGINE ordinal template. */
    bool (*context_ready)(void *, uint32_t seat, const qa_command_context *, qa_error *);
    void *context_user;
} qa_input_seat_options;
qa_input_seat *qa_input_seat_create(const qa_input_seat_options *, qa_error *);
/* Release held input before retiring the console. Destruction only frees
 * storage; it does not dispatch callbacks. Event callbacks may change focus or
 * bindings, but the current seat must live until the callback returns. */
void qa_input_seat_destroy(qa_input_seat *);
qa_command_context qa_input_seat_context(const qa_input_seat *);
uint32_t qa_input_seat_ordinal(const qa_input_seat *);
/* Preflight at the returned dispatch boundary. The owner admits this exact
 * upcoming command template; publication copies it without callbacks. An
 * ENGINE template keeps actor/generation uncaptured until real dispatch. */
bool qa_input_seat_context_ready(const qa_input_seat *, const qa_command_context *, qa_error *);
void qa_input_seat_context_publish(qa_input_seat *, const qa_command_context *);
bool qa_input_seat_recipient_read(const qa_input_seat *,qa_console **,qa_cvars **,qa_command_context *);
/* A recipient change follows completed ALL release, or an empty physical
 * seat. The old console remains installed until publication. */
bool qa_input_seat_recipient_ready_is(const qa_input_seat *,qa_console *,qa_cvars *,const qa_command_context *);
bool qa_input_seat_recipient_ready(qa_input_seat *,qa_console *,qa_cvars *,const qa_command_context *,qa_error *);
/* A completed transfer retains the former namespace until its physical Source
 * consumes this exact receipt. Neither operation dispatches release commands. */
bool qa_input_seat_recipient_retired_is(const qa_input_seat *,const qa_console *,const qa_cvars *,const qa_command_context *);
bool qa_input_seat_recipient_retired_consume(qa_input_seat *,const qa_console *,const qa_cvars *,const qa_command_context *,qa_error *);
void qa_input_seat_recipient_publish(qa_input_seat *,qa_console *,qa_cvars *,const qa_command_context *);
qa_input_focus qa_input_seat_focus(const qa_input_seat *);
bool qa_input_seat_focused(const qa_input_seat *);
bool qa_input_seat_action_active(const qa_input_seat *, qa_input_action);
bool qa_input_seat_has_held(const qa_input_seat *);
bool qa_input_seat_key_down(const qa_input_seat *, qa_physical_input);
enum qa_input_catcher_mask {
    QA_INPUT_CATCH_CONSOLE = 1, QA_INPUT_CATCH_UI = 2,
    QA_INPUT_CATCH_GAME = 4, QA_INPUT_CATCH_CHAT = 8
};
typedef struct qa_input_catcher { uint64_t owner; uint32_t mask; } qa_input_catcher;
/* Nonzero owner reads only that provider's source mask. Owner zero reads the
 * composed mask, including current console/chat/UI focus. */
uint32_t qa_input_seat_catcher(const qa_input_seat *, uint64_t owner);
bool qa_input_seat_set_catcher(qa_input_seat *, uint64_t owner, uint32_t mask, qa_error *);
void qa_input_seat_retire_catcher(qa_input_seat *, uint64_t owner);
size_t qa_input_seat_catcher_count(const qa_input_seat *);
bool qa_input_seat_catcher_at(const qa_input_seat *, size_t, qa_input_catcher *);
qa_gamepad_input *qa_input_seat_gamepad(qa_input_seat *);
qa_gamepad_tuning *qa_input_seat_gamepad_tuning(qa_input_seat *);
bool qa_input_seat_profile(qa_input_seat *, qa_ruleset_id, qa_error *);
bool qa_input_seat_bind(qa_input_seat *, const qa_input_binding *, qa_error *);
/* Validates/copies the complete list before publication. Held presses retain
 * their original binding until release, including after a settings reload. */
bool qa_input_seat_replace_bindings(qa_input_seat *, const qa_input_binding *, size_t, qa_error *);
/* Qualify a prepared configuration before its provider commits. The caller
 * keeps both seats alive and unchanged between readiness and publication and
 * holds their input/console dispatch boundary. Publication swaps only live
 * bindings and gamepad tuning; held presses retain their original bindings.
 * The candidate owns the displaced configuration. Destroy it before capture
 * so held binding references belong entirely to the stable physical seat. */
bool qa_input_seat_configuration_ready(const qa_input_seat *active,
    const qa_input_seat *candidate, qa_error *);
/* Pure returned physical configuration state. Context currentness is checked
 * separately by configuration_ready before admission. No qualifier runs. */
bool qa_input_seat_configuration_owned_is(const qa_input_seat *active,
    const qa_input_seat *candidate);
bool qa_input_seat_configuration_clone(const qa_input_seat *,qa_input_seat **,qa_error *);
void qa_input_seat_configuration_publish(qa_input_seat *active, qa_input_seat *candidate);
bool qa_input_seat_unbind(qa_input_seat *, qa_physical_input);
void qa_input_seat_unbind_all(qa_input_seat *);
size_t qa_input_seat_binding_count(const qa_input_seat *);
const qa_input_binding *qa_input_seat_binding_at(const qa_input_seat *, size_t);
const qa_input_binding *qa_input_seat_binding(const qa_input_seat *, qa_physical_input);
bool qa_input_seat_remap_controller(qa_input_seat *, int32_t device, qa_error *);
bool qa_input_seat_event(qa_input_seat *, const qa_input_event *, bool *consumed, qa_error *);
bool qa_input_seat_release(qa_input_seat *, double time_ms, qa_error *);
bool qa_input_seat_release_device(qa_input_seat *, int32_t device, double time_ms, qa_error *);
bool qa_input_seat_set_focus(qa_input_seat *, qa_input_focus, double time_ms, qa_error *);
/* UI overlays form a stack. Retiring a covered overlay never restores it later.
 */
bool qa_input_seat_ui_push(qa_input_seat *, qa_input_ui_handler, void *, double time_ms,
                           qa_input_ui_token *, qa_error *);
bool qa_input_seat_ui_remove(qa_input_seat *, qa_input_ui_token, double time_ms, qa_error *);
bool qa_input_seat_action(qa_input_seat *, qa_input_action, uint64_t physical_source, bool down,
                          double time_ms, qa_error *);
bool qa_input_seat_sample(qa_input_seat *, double now_ms, double frame_ms, qa_seat_input_sample *,
                          qa_error *);
bool qa_input_seat_impulse(qa_input_seat *, const char *, qa_error *);
/* Mouse, pitch drift and cl_run belong to the seat's mouse owner. Movement
 * speeds and angle/move multipliers belong to the selected view owner. These
 * may alias for a composed registry; no other registry is consulted. */
bool qa_input_mouse_settings_register(qa_cvars *, qa_ruleset_id selected, qa_error *);
bool qa_input_movement_settings_register(qa_cvars *, qa_ruleset_id, qa_error *);
bool qa_input_settings_register(qa_cvars *, qa_ruleset_id, qa_error *);
/* Bind once for the actual mouse and movement registry views after registration
 * or replacement. Scalar changes are projected by the common cvar store. */
void qa_input_settings_bind(const qa_cvars *mouse, const qa_cvars *movement,
    qa_input_tuning_handles *);
bool qa_input_settings_read(const qa_cvars *mouse, const qa_cvars *movement,
    const qa_input_tuning_handles *, qa_ruleset_id, qa_input_command_tuning *, qa_error *);
bool qa_input_mouse_settings_write(qa_cvars *, const qa_mouse_tuning *, qa_error *);
bool qa_input_device_settings_register(qa_cvars *, qa_error *);
bool qa_input_bindings_config(const qa_input_seat *, bool controllers, qa_buffer *, qa_error *);
/* Pure rows from the same general defaults used by apply and reset. Command
 * text has static lifetime; false marks the end or an invalid profile/device. */
bool qa_input_default_binding_at(qa_ruleset_id, int32_t device, size_t index,
                                  qa_input_binding *out);
bool qa_input_default_bindings(qa_input_seat *, int32_t device, qa_error *);
/* Copy and validate the default list before replacing current bindings. */
bool qa_input_reset_default_bindings(qa_input_seat *, int32_t device, qa_error *);
typedef struct qa_input_weapon_binding {
    const char *id, *label;
    bool powerup;
    /* Optional original slot and impulse, zero when not applicable. */
    unsigned q1_impulse, q3_weapon;
    const char *default_key;
} qa_input_weapon_binding;
bool qa_input_weapon_defaults(qa_input_seat *, const qa_input_weapon_binding *, size_t, qa_error *);
const qa_input_weapon_binding *qa_input_weapon_resolve(const char *command, const char *argument,
                                                       const qa_input_weapon_binding *, size_t);
typedef struct qa_input_console_options {
    qa_console *console;
    uint64_t owner;
    void *user;
    qa_input_seat *(*seat)(void *, const qa_command_context *);
    void (*print)(void *, const char *);
    void (*wheel)(void *, qa_input_seat *, bool powerups, bool down);
    void (*center)(void *, qa_input_seat *);
    /* Exact Source constructor. An uncaptured ENGINE adapter may use the
     * console's current constructor when cvar_view is zero. */
    qa_command_context context;
} qa_input_console_options;
typedef struct qa_input_console qa_input_console;
qa_input_console *qa_input_console_create(const qa_input_console_options *, qa_error *);
void qa_input_console_destroy(qa_input_console *);

typedef struct qa_input_client_commands qa_input_client_commands;
typedef bool (*qa_input_client_handler)(void *, const qa_command_invocation *, uint32_t seat,
                                        qa_error *);
typedef struct qa_input_client_options {
    qa_console *console;
    uint64_t owner;
    void *user;
    qa_input_client_handler primary;
} qa_input_client_options;
qa_input_client_commands *qa_input_client_commands_create(const qa_input_client_options *,
                                                          qa_error *);
void qa_input_client_commands_destroy(qa_input_client_commands *);
bool qa_input_client_seats(qa_input_client_commands *, uint64_t session, unsigned seat_mask,
                           qa_error *);
/* instance is a nonzero generation-bearing module identity. A NULL handler
 * claims the primary client dispatcher. Name matching follows source ASCII. */
bool qa_input_client_claim(qa_input_client_commands *, uint64_t instance, uint32_t seat,
                           const char *name, qa_input_client_handler, void *, const char *label,
                           qa_error *);
bool qa_input_client_unclaim(qa_input_client_commands *, uint64_t instance, const char *name);
void qa_input_client_retire(qa_input_client_commands *, uint64_t instance);
bool qa_input_client_activate(qa_input_client_commands *, bool active, qa_error *);
qa_command_result qa_input_client_dispatch(qa_input_client_commands *,
                                           const qa_command_invocation *,
                                           uint64_t producer_instance, qa_error *);

typedef struct qa_source_joystick {
    int16_t axes[16];
    uint32_t old_axes;
    uint8_t hat;
    bool buttons[256];
} qa_source_joystick;
typedef bool (*qa_input_key_sink)(void *, int key, bool down, double time_ms, qa_error *);
typedef bool (*qa_input_mouse_sink)(void *, float x, float y, double time_ms, qa_error *);
bool qa_source_joystick_button(qa_source_joystick *, unsigned button, bool down,
                               bool transitions_only, qa_input_key_sink, void *, qa_error *);
bool qa_source_joystick_frame(qa_source_joystick *, bool connected, bool windows, float threshold,
                              unsigned axes, float ball_scale, qa_input_key_sink,
                              qa_input_mouse_sink, void *, qa_error *);
bool qa_source_joystick_release(qa_source_joystick *, double time_ms, qa_input_key_sink, void *,
                                qa_error *);
typedef struct qa_midi_decoder {
    uint8_t status, first;
    bool has_first;
} qa_midi_decoder;
bool qa_midi_feed(qa_midi_decoder *, qa_bytes, int channel, double time_ms, qa_input_key_sink,
                  void *, qa_error *);

typedef struct qa_haptic_pattern qa_haptic_pattern;
typedef struct qa_haptic_cache qa_haptic_cache;
typedef struct qa_vfs qa_vfs;
bool qa_haptic_pattern_parse(qa_bytes, qa_haptic_pattern **, qa_error *);
void qa_haptic_pattern_retain(qa_haptic_pattern *);
void qa_haptic_pattern_release(qa_haptic_pattern *);
qa_haptic_cache *qa_haptic_cache_create(qa_error *);
void qa_haptic_cache_destroy(qa_haptic_cache *);
/* Success with NULL means a sound has no tactile asset. Output owns a
 * reference. */
bool qa_haptic_cache_sound(qa_haptic_cache *, qa_vfs *, const char *sound, qa_haptic_pattern **,
                           qa_error *);
typedef bool (*qa_rumble_sink)(void *, float low, float high, uint32_t duration_ms, qa_error *);
typedef struct qa_haptic_player {
    qa_haptic_pattern *pattern;
    qa_rumble_sink output;
    void *user;
    double start_ms, last_ms;
    int64_t last_index;
    float strength;
    bool enabled, active;
} qa_haptic_player;
void qa_haptic_player_init(qa_haptic_player *, qa_rumble_sink, void *);
bool qa_haptic_stop(qa_haptic_player *, qa_error *);
bool qa_haptic_play(qa_haptic_player *, qa_haptic_pattern *, double now_ms, qa_error *);
bool qa_haptic_update(qa_haptic_player *, double now_ms, qa_error *);
bool qa_haptic_strength(qa_haptic_player *, float strength, double now_ms, qa_error *);
bool qa_haptic_enable(qa_haptic_player *, bool enabled, bool active, qa_error *);

#endif
