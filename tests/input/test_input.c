/*
 * Port of tests/input/input.test.ts, impulse.test.ts,
 * weapon-bindings.test.ts, pitch-drift.test.ts and
 * mouse-settings.test.ts (the cases owned by the input module).
 * TypeScript `toThrow` cases run in a child process and must
 * abort. GPL-2.0-or-later.
 */
#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "qa/contracts/identity.h"
#include "qa/core/commands/index.h"
#include "qa/core/cvars/index.h"
#include "qa/input.h"
#include "qa/input/binding_store.h"
#include "qa/input/client_commands.h"
#include "qa/input/device_settings.h"
#include "qa/input/impulse.h"
#include "qa/input/mouse_buttons.h"
#include "qa/input/mouse_settings.h"
#include "qa/input/pitch_drift.h"
#include "qa/input/weapon_bindings.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                     \
  do {                                                                  \
    checks++;                                                           \
    if (!(cond)) {                                                      \
      failures++;                                                       \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
    }                                                                   \
  } while (0)

#define CHECK_ABORTS(...)                                               \
  do {                                                                  \
    checks++;                                                           \
    pid_t pid = fork();                                                 \
    if (pid == 0) {                                                     \
      (void)prctl(PR_SET_DUMPABLE, 0);                                  \
      (void)prctl(PR_SET_DUMPABLE, 0); /* no coredump for expected aborts */ \
      freopen("/dev/null", "w", stderr);                                \
      __VA_ARGS__;                                                      \
      _exit(0);                                                         \
    }                                                                   \
    int status = 0;                                                     \
    waitpid(pid, &status, 0);                                           \
    if (!(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT)) {        \
      failures++;                                                       \
      fprintf(stderr, "FAIL %s:%d: expected abort: %s\n", __FILE__,     \
              __LINE__, #__VA_ARGS__);                                  \
    }                                                                   \
  } while (0)

static bool close_to(double value, double expected, double epsilon) {
  return fabs(value - expected) <= epsilon;
}

static qa_command_context seat_context(qa_identity_owner *owner, qa_seat_id seat,
                                       qa_client_id client) {
  qa_command_context context;
  memset(&context, 0, sizeof context);
  context.session = owner->session;
  context.origin.kind = QA_COMMAND_ORIGIN_LOCAL_SEAT;
  context.origin.seat = seat;
  context.origin.client = client;
  return context;
}

static qa_cvar_registry *make_cvars(qa_command_dialect dialect,
                                    qa_command_context context) {
  qa_cvar_registry_options options;
  memset(&options, 0, sizeof options);
  options.dialect = dialect;
  options.context = context;
  return qa_cvar_registry_create(&options);
}

static qa_command_buffer *make_commands(qa_command_dialect dialect,
                                        qa_command_context context) {
  qa_command_buffer_options options;
  memset(&options, 0, sizeof options);
  options.dialect = dialect;
  options.context = context;
  return qa_command_buffer_create(&options);
}

static bool no_ui(void *ctx, const qa_seat_input_event *event,
                  qa_seat_input_focus focus) {
  (void)ctx;
  (void)event;
  (void)focus;
  return false;
}

typedef struct seat_fixture {
  qa_identity_owner owner;
  qa_seat_id seat;
  qa_command_context context;
  qa_command_buffer *commands;
  qa_seat_input *input;
  qa_input_commands_registration *registration;
} seat_fixture;

static qa_seat_input *fixture_lookup(void *ctx, qa_seat_id seat) {
  seat_fixture *fixture = (seat_fixture *)ctx;
  return qa_same_seat(seat, fixture->seat) ? fixture->input : NULL;
}

static void fixture_init(seat_fixture *fixture, const char *name,
                         qa_command_dialect dialect) {
  memset(fixture, 0, sizeof *fixture);
  CHECK(qa_identity_owner_create(&fixture->owner, name));
  fixture->seat = qa_identity_owner_seat(&fixture->owner, 0);
  fixture->context =
      seat_context(&fixture->owner, fixture->seat,
                   qa_identity_owner_client(&fixture->owner, 0, 0));
  fixture->commands = make_commands(dialect, fixture->context);
  CHECK(fixture->commands != NULL);
  qa_seat_input_options options;
  memset(&options, 0, sizeof options);
  options.seat = fixture->seat;
  options.dialect = dialect;
  options.context = fixture->context;
  options.commands = fixture->commands;
  options.ui_event = no_ui;
  fixture->input = qa_seat_input_create(&options);
  CHECK(fixture->input != NULL);
  fixture->registration = qa_register_input_commands(
      fixture->commands, fixture_lookup, fixture, NULL, NULL);
  CHECK(fixture->registration != NULL);
}

static void fixture_destroy(seat_fixture *fixture) {
  qa_input_commands_registration_destroy(fixture->registration);
  qa_seat_input_destroy(fixture->input);
  qa_command_buffer_destroy(fixture->commands);
  qa_identity_owner_destroy(&fixture->owner);
}

static qa_physical_input key_input(double code) {
  qa_physical_input input;
  memset(&input, 0, sizeof input);
  input.kind = QA_PHYSICAL_INPUT_KEY;
  input.code = code;
  return input;
}

static void send_key(seat_fixture *fixture, double code, bool down, double time) {
  qa_seat_input_event event;
  memset(&event, 0, sizeof event);
  event.seat = fixture->seat;
  event.time_milliseconds = time;
  event.kind = "key";
  event.code = code;
  event.down = down;
  (void)qa_seat_input_input(fixture->input, &event);
}

static void test_impulse(void) {
  static const struct {
    const char *text;
    double expected;
  } quake_cases[] = {
      {"", 0}, {"9suffix", 9}, {"9.5", 9}, {"-1", 255}, {"255", 255},
      {"256", 0}, {"257", 1}, {"0x109tail", 9}, {"-0X1", 255}, {"'a", 97},
      {"-'a", 159}, {"'", 0}, {"+9", 0}, {" 9", 0}, {"garbage", 0}, {"1e2", 1}};
  for (size_t i = 0; i < sizeof quake_cases / sizeof quake_cases[0]; i++) {
    CHECK(qa_parse_impulse(quake_cases[i].text, QA_COMMAND_DIALECT_Q1_NETQUAKE) ==
          quake_cases[i].expected);
    CHECK(qa_parse_impulse(quake_cases[i].text, QA_COMMAND_DIALECT_Q1_QUAKEWORLD) ==
          quake_cases[i].expected);
  }
  static const struct {
    const char *text;
    double expected;
  } q2_cases[] = {{"+9", 9}, {" 9", 9}, {"9tail", 9}, {"9.5", 9}, {"-1", 255},
                  {"256", 0}, {"257", 1}, {"0x9", 0}, {"'a", 0}, {"bad", 0},
                  {"", 0}};
  for (size_t i = 0; i < sizeof q2_cases / sizeof q2_cases[0]; i++) {
    CHECK(qa_parse_impulse(q2_cases[i].text, QA_COMMAND_DIALECT_Q2_CLASSIC) ==
          q2_cases[i].expected);
  }
  CHECK(qa_parse_impulse("0x9", QA_COMMAND_DIALECT_Q3) == 9);
  CHECK(isnan(qa_parse_impulse("9tail", QA_COMMAND_DIALECT_Q3)));

  /* The impulse command reaches only the caller seat's sample. */
  seat_fixture fixture;
  fixture_init(&fixture, "impulse", QA_COMMAND_DIALECT_Q1_NETQUAKE);
  qa_command_buffer_append(fixture.commands, "impulse 257\n", NULL, -1);
  qa_command_buffer_execute(fixture.commands);
  qa_seat_input_sample sample;
  CHECK(qa_seat_input_take_sample(fixture.input, 20, 20, &sample));
  CHECK(sample.impulse == 1);
  qa_seat_input_sample_dispose(&sample);
  CHECK(qa_seat_input_take_sample(fixture.input, 40, 20, &sample));
  CHECK(sample.impulse == 0);
  qa_seat_input_sample_dispose(&sample);
  CHECK_ABORTS(qa_seat_input_set_impulse(fixture.input, 256));
  fixture_destroy(&fixture);
}

static void test_buttons(void) {
  qa_input_button button;
  qa_input_button_init(&button);
  CHECK(!qa_input_button_active(&button));
  CHECK(qa_input_button_down(&button, "key:119", 100));
  CHECK(qa_input_button_active(&button));
  CHECK(qa_input_button_pressed(&button));
  /* Q1: pressed and still held reads 0.5, held reads 1. */
  CHECK(qa_input_button_sample(&button, QA_BUTTON_TIMING_Q1, 100, 20) == 0.5);
  CHECK(qa_input_button_sample(&button, QA_BUTTON_TIMING_Q1, 120, 20) == 1);
  qa_input_button_up(&button, "key:119", 130, 10);
  CHECK(qa_input_button_sample(&button, QA_BUTTON_TIMING_Q1, 130, 20) == 0);
  /* Q1 tap within one frame reads 0.25. */
  CHECK(qa_input_button_down(&button, "key:119", 200));
  qa_input_button_up(&button, "key:119", 205, 10);
  CHECK(qa_input_button_sample(&button, QA_BUTTON_TIMING_Q1, 210, 20) == 0.25);
  /* Q2 millisecond fraction: 10 ms of a 20 ms frame. */
  CHECK(qa_input_button_down(&button, "key:119", 300));
  qa_input_button_up(&button, "key:119", 310, 10);
  CHECK(qa_input_button_sample(&button, QA_BUTTON_TIMING_Q2, 320, 20) == 0.5);
  /* Missing up-timestamp charges the default 10 ms. */
  CHECK(qa_input_button_down(&button, "key:119", 400));
  qa_input_button_up(&button, "key:119", 0, 10);
  CHECK(qa_input_button_sample(&button, QA_BUTTON_TIMING_Q2, 400, 20) == 0.5);
  qa_input_button_destroy(&button);
}

static void test_keys(void) {
  CHECK(qa_string_to_keynum(NULL) == -1);
  CHECK(qa_string_to_keynum("") == -1);
  CHECK(qa_string_to_keynum("w") == 119);
  CHECK(qa_string_to_keynum("SPACE") == QA_KEY_SPACE);
  CHECK(qa_string_to_keynum("space") == QA_KEY_SPACE);
  CHECK(qa_string_to_keynum("F12") == QA_KEY_F12);
  CHECK(qa_string_to_keynum("MOUSE2") == QA_KEY_MOUSE2);
  CHECK(qa_string_to_keynum("JOY32") == QA_KEY_JOY32);
  CHECK(qa_string_to_keynum("AUX17") == 256);
  CHECK(qa_string_to_keynum("KP_5") == QA_KEY_KEYPAD_5);
  CHECK(qa_string_to_keynum("SEMICOLON") == 59);
  CHECK(qa_string_to_keynum("0x41") == 65);
  CHECK(qa_string_to_keynum("bogus") == -1);
  char *name = qa_keynum_to_string(-1);
  CHECK(name != NULL && strcmp(name, "<KEY NOT FOUND>") == 0);
  free(name);
  name = qa_keynum_to_string(999);
  CHECK(name != NULL && strcmp(name, "<OUT OF RANGE>") == 0);
  free(name);
  name = qa_keynum_to_string(119);
  CHECK(name != NULL && strcmp(name, "w") == 0);
  free(name);
  name = qa_keynum_to_string(QA_KEY_SPACE);
  CHECK(name != NULL && strcmp(name, "SPACE") == 0);
  free(name);
  name = qa_keynum_to_string(59);
  CHECK(name != NULL && strcmp(name, "SEMICOLON") == 0);
  free(name);
  name = qa_keynum_to_string(256);
  CHECK(name != NULL && strcmp(name, "AUX17") == 0);
  free(name);
  name = qa_keynum_to_string(34);
  CHECK(name != NULL && strcmp(name, "0x22") == 0);
  free(name);
  /* SDL translation spots. */
  CHECK(qa_sdl_game_key(97, 0) == 97);
  CHECK(qa_sdl_game_key(65, 0) == 97);
  CHECK(qa_sdl_game_key(8, 0) == QA_KEY_BACKSPACE);
  CHECK(qa_sdl_game_key(27, 0) == QA_KEY_ESCAPE);
  CHECK(qa_sdl_game_key(0x40000000 | 58, 0) == QA_KEY_F1);
  CHECK(qa_sdl_event_time(1000, 1010, 5000, true) == 4990);
  CHECK(qa_sdl_event_time(1000, 1010, 5000, false) == 5000);
  CHECK(qa_sdl_event_time(1000, 1100, 5000, true) == 5000);
  CHECK(qa_quake_mouse_button(2) == 3);
  CHECK(qa_physical_mouse_button(3) == 2);
  CHECK(qa_quake_mouse_button(1) == 1);
}

static void test_pitch_drift(void) {
  qa_pitch_drift drift;
  qa_pitch_drift_init(&drift);
  qa_pitch_drift_state ground = {true, 5, false};
  CHECK(qa_pitch_drift_sample(&drift, 50, 20, ground, false, true, 0, 200, 500,
                              0.15) == 40);
  CHECK(close_to(qa_pitch_drift_sample(&drift, 40, 20, ground, false, false, 0,
                                       200, 500, 0.15),
                 29.8, 1e-9));
  CHECK(close_to(qa_pitch_drift_sample(&drift, 29.8, 20, ground, true, false, 0,
                                       200, 500, 0.15),
                 29.8, 1e-9));
  CHECK(close_to(qa_pitch_drift_sample(&drift, 29.8, 20, ground, false, false, 0,
                                       200, 500, 0.15),
                 29.8, 1e-9));
  CHECK(qa_pitch_drift_sample(&drift, 10, 100, ground, false, true, 0, 200, 500,
                              0.15) == 5);
  qa_pitch_drift_state airborne = {false, 5, false};
  CHECK(qa_pitch_drift_sample(&drift, 40, 20, airborne, false, true, 0, 200, 500,
                              0.15) == 40);
  CHECK(qa_pitch_drift_sample(&drift, 40, 20, ground, false, false, 0, 200, 500,
                              0.15) == 40);
  CHECK(close_to(qa_pitch_drift_sample(&drift, 40, 20, ground, false, false, 0,
                                       200, 500, 0.15),
                 39.8, 1e-9));
  qa_pitch_drift_state disabled = {true, 5, true};
  CHECK(qa_pitch_drift_sample(&drift, 20, 20, disabled, false, true, 0, 200, 500,
                              0.15) == 20);
  qa_pitch_drift_clear(&drift);
  CHECK(qa_pitch_drift_sample(&drift, 20, 200, ground, false, false, 250, 200,
                              500, 0.15) == 20);
  CHECK(qa_pitch_drift_sample(&drift, 20, 20, ground, false, false, 250, 200, 500,
                              0.15) == 10);
}

static void test_mouse_and_settings(void) {
  /* Lookstrafe routing (pitch-drift.test.ts). */
  qa_identity_owner owner;
  CHECK(qa_identity_owner_create(&owner, "mouse"));
  qa_command_context context;
  memset(&context, 0, sizeof context);
  context.session = owner.session;
  context.origin.kind = QA_COMMAND_ORIGIN_LOCAL_CONSOLE;
  qa_cvar_registry *cvars = make_cvars(QA_COMMAND_DIALECT_Q1_NETQUAKE, context);
  CHECK(cvars != NULL);
  qa_mouse_settings settings;
  qa_mouse_settings_init(&settings, cvars);
  qa_mouse_tuning tuning = qa_default_mouse_tuning;
  tuning.free_look = false;
  tuning.look_spring = true;
  tuning.look_strafe = true;
  qa_mouse_settings_write(&settings, tuning);
  qa_mouse_input mouse;
  qa_mouse_input_init_store(&mouse, qa_mouse_settings_store(&settings));
  qa_vec2 raw = {10, 10};
  qa_mouse_move held = qa_mouse_input_sample(&mouse, raw, 20, false, true, 1, false);
  CHECK(held.yaw == 0);
  CHECK(close_to(held.side, 24, 1e-4));
  CHECK(close_to(held.pitch, 0.66, 1e-6));
  CHECK(held.forward == 0);
  qa_mouse_move released =
      qa_mouse_input_sample(&mouse, raw, 20, false, false, 1, false);
  CHECK(close_to(released.yaw, -0.66, 1e-6));
  CHECK(released.side == 0);
  CHECK(released.pitch == 0);
  CHECK(released.forward == -30);
  qa_mouse_tuning read = qa_mouse_settings_read(&settings);
  CHECK(read.look_spring && read.look_strafe);
  /* Negative-zero pitch inversion survives the cvar text. */
  qa_mouse_tuning inverted = qa_mouse_settings_read(&settings);
  inverted.yaw = -0.0;
  inverted.pitch = 0;
  inverted.invert_pitch = true;
  qa_mouse_settings_write(&settings, inverted);
  char *text = qa_cvar_registry_variable_string(cvars, "m_pitch");
  CHECK(text != NULL && strcmp(text, "-0") == 0);
  free(text);
  text = qa_cvar_registry_variable_string(cvars, "m_yaw");
  CHECK(text != NULL && strcmp(text, "-0") == 0);
  free(text);
  CHECK(qa_mouse_settings_read(&settings).invert_pitch);
  /* Q3 m_filter reads the integer value. */
  qa_cvar_registry *q3 = make_cvars(QA_COMMAND_DIALECT_Q3, context);
  qa_mouse_settings q3_settings;
  qa_mouse_settings_init(&q3_settings, q3);
  qa_cvar_snapshot snapshot;
  memset(&snapshot, 0, sizeof snapshot);
  (void)qa_cvar_registry_set(q3, "m_filter", "0.5", false, &snapshot);
  qa_cvar_read_free(&snapshot);
  CHECK(!qa_mouse_settings_read(&q3_settings).filter);
  memset(&snapshot, 0, sizeof snapshot);
  (void)qa_cvar_registry_set(cvars, "m_filter", "0.5", false, &snapshot);
  qa_cvar_read_free(&snapshot);
  CHECK(qa_mouse_settings_read(&settings).filter);
  qa_cvar_registry_destroy(q3);
  qa_cvar_registry_destroy(cvars);
  qa_identity_owner_destroy(&owner);
}

static void test_gamepad(void) {
  qa_stick_curve axial = {QA_STICK_CURVE_AXIAL, 0.25, 0, 1};
  qa_vec2 in = {1, -0.25};
  qa_vec2 curved = qa_apply_stick_curve(in, axial);
  CHECK(close_to(curved.x, 1, 1e-12));
  CHECK(curved.y == 0);
  qa_vec2 radial_in = {0.1, 0};
  qa_vec2 dead = qa_apply_stick_curve(radial_in, qa_default_gamepad_tuning.move);
  CHECK(dead.x == 0 && dead.y == 0);
  CHECK(qa_controller_axis_name(4) != NULL &&
        strcmp(qa_controller_axis_name(4), "left-trigger") == 0);
  CHECK(qa_controller_axis_name(6) == NULL);
  CHECK(close_to(qa_normalized_controller_axis("left-x", -32767), -1, 1e-12));
  CHECK(qa_normalized_controller_axis("left-trigger", -100) == 0);

  /* Gyro calibration (input.test.ts). */
  qa_gamepad_tuning tuning = qa_default_gamepad_tuning;
  tuning.gyro.enabled = true;
  qa_gamepad_input pad;
  qa_gamepad_input_init(&pad, &tuning);
  qa_vec3 still = {0.01, -0.02, 0.005};
  qa_gamepad_input_begin_gyro_calibration(&pad);
  for (int i = 0; i < 100; i++) qa_gamepad_input_gyro(&pad, still, 0, true);
  qa_gyro_calibration_state state = qa_gamepad_input_gyro_calibration(&pad);
  CHECK(state.kind == QA_GYRO_CALIBRATION_CALIBRATING && state.samples == 1 &&
        state.progress == 0);
  qa_vec3 moving = {1, 0, 0};
  qa_gamepad_input_gyro(&pad, moving, 410, true);
  state = qa_gamepad_input_gyro_calibration(&pad);
  CHECK(state.kind == QA_GYRO_CALIBRATION_CALIBRATING && state.samples == 0);
  for (int i = 0; i <= 100; i++) {
    qa_gamepad_input_gyro(&pad, still, 6000 + i * 20, true);
  }
  state = qa_gamepad_input_gyro_calibration(&pad);
  CHECK(state.kind == QA_GYRO_CALIBRATION_READY);
  qa_gamepad_input_gyro(&pad, still, 8020, true);
  qa_gamepad_sample sample = qa_gamepad_input_sample(&pad, 100);
  CHECK(close_to(sample.look_degrees.x, 0, 1e-9));
  CHECK(close_to(sample.look_degrees.y, 0, 1e-9));
  qa_gamepad_input_reset_gyro_calibration(&pad);
  CHECK(qa_gamepad_input_gyro_calibration(&pad).kind == QA_GYRO_CALIBRATION_IDLE);
  qa_gamepad_tuning invalid = qa_default_gamepad_tuning;
  invalid.move.deadzone = 1;
  CHECK_ABORTS(qa_validate_gamepad_tuning(&invalid));
}

typedef struct key_log {
  double keys[16];
  bool downs[16];
  size_t count;
} key_log;

static void log_key(void *ctx, double key, bool down, double time) {
  (void)time;
  key_log *log = (key_log *)ctx;
  if (log->count < 16) {
    log->keys[log->count] = key;
    log->downs[log->count] = down;
    log->count++;
  }
}

static void test_source_devices(void) {
  /* MIDI decoder: note 60 on channel 1 is Aux1; velocity-zero
   * NoteOn queues both the release and the press. */
  qa_source_midi_decoder decoder;
  qa_source_midi_decoder_init(&decoder);
  key_log log;
  memset(&log, 0, sizeof log);
  const uint8_t on[] = {0x90, 60, 100};
  qa_source_midi_decoder_feed(&decoder, on, sizeof on, 1, 7, log_key, &log);
  CHECK(log.count == 1 && log.keys[0] == QA_KEY_AUX1 && log.downs[0]);
  const uint8_t zero[] = {0x90, 60, 0};
  qa_source_midi_decoder_feed(&decoder, zero, sizeof zero, 1, 8, log_key, &log);
  CHECK(log.count == 3 && !log.downs[1] && log.downs[2]);
  const uint8_t off_channel[] = {0x91, 62, 100};
  qa_source_midi_decoder_feed(&decoder, off_channel, sizeof off_channel, 1, 9,
                              log_key, &log);
  CHECK(log.count == 3);

  /* Joystick axes bitmask and button transitions. */
  int16_t axes[2] = {-32767, 32767};
  CHECK(qa_sdl_joystick_axes(axes, 2, 0.15) == (1 | 8));
  qa_source_joystick_state stick;
  qa_source_joystick_state_init(&stick);
  memset(&log, 0, sizeof log);
  qa_source_joystick_state_button(&stick, 0, true, log_key, &log, true);
  qa_source_joystick_state_button(&stick, 0, true, log_key, &log, true);
  CHECK(log.count == 1 && log.keys[0] == QA_KEY_JOY1);
  qa_source_joystick_state_remove_device(&stick, log_key, &log);
  CHECK(log.count == 2 && !log.downs[1]);
  CHECK_ABORTS(qa_source_joystick_state_button(&stick, 300, true, log_key, &log,
                                                false));
  /* Windows debug row shape. */
  qa_sdl_event debug_events[1];
  memset(debug_events, 0, sizeof debug_events);
  debug_events[0].kind = QA_SDL_EVENT_JOYSTICK_BUTTON;
  debug_events[0].as.joystick_button.button = 1;
  debug_events[0].as.joystick_button.down = true;
  char *row = qa_windows_joystick_debug(debug_events, 1);
  CHECK(row != NULL && strncmp(row, "       2", 8) == 0 && strstr(row, "65535") != NULL);
  free(row);
}

static void test_binding_store(void) {
  qa_physical_input pad;
  memset(&pad, 0, sizeof pad);
  pad.kind = QA_PHYSICAL_INPUT_CONTROLLER_AXIS;
  pad.device = 2;
  pad.axis = "left-x";
  pad.direction = "negative";
  char *key = qa_physical_input_key(&pad);
  CHECK(key != NULL && strcmp(key, "pad:2:axis:left-x:negative") == 0);
  free(key);
  qa_binding_store *store = qa_binding_store_create(NULL, 0);
  CHECK(store != NULL);
  qa_input_binding binding;
  memset(&binding, 0, sizeof binding);
  binding.input = key_input(119);
  binding.target.is_action = false;
  binding.target.text = "+forward";
  CHECK(qa_binding_store_bind(store, &binding));
  const qa_input_binding_target *target =
      qa_binding_store_binding(store, &binding.input);
  CHECK(target != NULL && !target->is_action &&
        strcmp(target->text, "+forward") == 0);
  qa_binding_store_unbind(store, &binding.input);
  CHECK(qa_binding_store_binding(store, &binding.input) == NULL);
  qa_binding_store_destroy(store);
}

static void test_seat_flow(void) {
  seat_fixture fixture;
  fixture_init(&fixture, "seat-flow", QA_COMMAND_DIALECT_Q1_NETQUAKE);
  qa_input_binding binding;
  memset(&binding, 0, sizeof binding);
  binding.input = key_input(119);
  binding.target.is_action = false;
  binding.target.text = "+forward";
  CHECK(qa_seat_input_bind(fixture.input, &binding));
  send_key(&fixture, 119, true, 10);
  qa_command_buffer_execute(fixture.commands);
  qa_input_button *forward = qa_seat_input_button(fixture.input, "forward");
  CHECK(forward != NULL && qa_input_button_active(forward));
  CHECK(qa_seat_input_is_down(fixture.input, &binding.input));
  send_key(&fixture, 119, false, 30);
  qa_command_buffer_execute(fixture.commands);
  CHECK(!qa_input_button_active(forward));
  /* Action targets drive buttons directly. */
  memset(&binding, 0, sizeof binding);
  binding.input = key_input(120);
  binding.target.is_action = true;
  binding.target.action = "attack";
  CHECK(qa_seat_input_bind(fixture.input, &binding));
  send_key(&fixture, 120, true, 40);
  CHECK(qa_input_button_active(qa_seat_input_button(fixture.input, "attack")));
  fixture_destroy(&fixture);
}

static void test_user_command(void) {
  seat_fixture fixture;
  fixture_init(&fixture, "user-command", QA_COMMAND_DIALECT_Q1_NETQUAKE);
  qa_input_binding binding;
  memset(&binding, 0, sizeof binding);
  binding.input = key_input(119);
  binding.target.is_action = true;
  binding.target.action = "forward";
  CHECK(qa_seat_input_bind(fixture.input, &binding));
  send_key(&fixture, 119, true, 10);
  qa_seat_input_sample sample;
  CHECK(qa_seat_input_take_sample(fixture.input, 20, 20, &sample));
  qa_input_command_builder *builder = qa_input_command_builder_create(
      QA_COMMAND_DIALECT_Q1_NETQUAKE, NULL, NULL);
  CHECK(builder != NULL);
  qa_user_command_frame frame;
  memset(&frame, 0, sizeof frame);
  frame.kind = QA_COMMAND_DIALECT_Q1_NETQUAKE;
  frame.acknowledged_server_time_seconds = 1;
  qa_user_command command =
      qa_input_command_builder_build(builder, &sample, &frame, 20);
  CHECK(command.kind == QA_USER_COMMAND_Q1);
  /* Q1 kbutton sampling gives a first-frame press fraction of
   * 0.5, so forward is 200 * 0.5. */
  CHECK(command.as.q1.forward_move == 100);
  qa_seat_input_sample_dispose(&sample);
  qa_input_command_builder_destroy(builder);
  fixture_destroy(&fixture);
}

static const qa_weapon_binding_item *find_item(const qa_weapon_binding_item *items,
                                               size_t count, const char *id) {
  for (size_t i = 0; i < count; i++) {
    if (strcmp(items[i].id, id) == 0) return &items[i];
  }
  return NULL;
}

static void test_weapon_bindings(void) {
  size_t count = 0;
  qa_weapon_binding_item *q1 =
      qa_base_weapon_binding_items(QA_GAME_FAMILY_Q1, "", "classic", &count);
  CHECK(q1 != NULL && count == 8);
  const qa_weapon_binding_item *ss = find_item(q1, count, "q1:weapon/supershotgun");
  CHECK(ss != NULL && strcmp(ss->label, "Double-barrelled Shotgun") == 0);
  size_t hip_count = 0;
  qa_weapon_binding_item *hip = qa_base_weapon_binding_items(
      QA_GAME_FAMILY_Q1, "hipnotic", "classic", &hip_count);
  const qa_weapon_binding_item *laser =
      find_item(hip, hip_count, "q1:weapon/hipnotic:laser");
  CHECK(laser != NULL && strcmp(laser->label, "Laser Cannon") == 0);
  size_t q2_count = 0;
  qa_weapon_binding_item *q2 =
      qa_base_weapon_binding_items(QA_GAME_FAMILY_Q2, "xatrix", "classic", &q2_count);
  const qa_weapon_binding_item *boomer = find_item(q2, q2_count, "q2:weapon_boomer");
  CHECK(boomer != NULL && strcmp(boomer->label, "Ionripper") == 0);
  size_t q3_count = 0;
  qa_weapon_binding_item *q3 =
      qa_base_weapon_binding_items(QA_GAME_FAMILY_Q3, "", "classic", &q3_count);
  CHECK(find_item(q3, q3_count, "q3:weapon/proxlauncher") == NULL);
  const qa_weapon_binding_item *lightning =
      find_item(q3, q3_count, "q3:weapon/lightning");
  CHECK(lightning != NULL && strcmp(lightning->label, "Lightning Gun") == 0);

  qa_resolved_weapon_selection resolved;
  const char *args1[] = {"BFG10K"};
  size_t base_q2 = 0;
  qa_weapon_binding_item *q2_base = qa_base_weapon_binding_items(
      QA_GAME_FAMILY_Q2, "", "classic", &base_q2);
  CHECK(qa_resolve_weapon_selection("use", args1, 1, q2_base, base_q2, &resolved) &&
        strcmp(resolved.item, "q2:weapon_bfg") == 0);
  const char *args2[] = {"3"};
  CHECK(qa_resolve_weapon_selection("weapon", args2, 1, q3, q3_count, &resolved) &&
        strcmp(resolved.item, "q3:weapon/shotgun") == 0);
  const char *args3[] = {"2"};
  CHECK(qa_resolve_weapon_selection("impulse", args3, 1, q1, count, &resolved) &&
        strcmp(resolved.item, "q1:weapon/shotgun") == 0);
  const char *args4[] = {"6"};
  CHECK(!qa_resolve_weapon_selection("impulse", args4, 1, hip, hip_count,
                                     &resolved));
  const char *bad[] = {"BFG10K;quit"};
  CHECK(!qa_resolve_weapon_selection("use", bad, 1, q2_base, base_q2, &resolved));
  const char *item = qa_weapon_binding_item_for_text("weapon 3", q3, q3_count);
  CHECK(item != NULL && strcmp(item, "q3:weapon/shotgun") == 0);
  CHECK(qa_weapon_binding_item_for_text("weapon 3 4", q3, q3_count) == NULL);

  /* Default bindings: Q2 arsenal under a Q1 dialect still jumps
   * on SPACE and keeps grenades on g. */
  size_t binding_count = 0;
  qa_input_binding *bindings = qa_default_bindings(
      0, QA_COMMAND_DIALECT_Q1_NETQUAKE, q2_base, base_q2, &binding_count);
  CHECK(bindings != NULL);
  bool blaster = false, grenades = false, jump = false;
  for (size_t i = 0; i < binding_count; i++) {
    if (bindings[i].target.is_action) continue;
    if (strcmp(bindings[i].target.text, "use q2:weapon_blaster") == 0 &&
        bindings[i].input.kind == QA_PHYSICAL_INPUT_KEY &&
        bindings[i].input.code == 49) {
      blaster = true;
    }
    if (strcmp(bindings[i].target.text, "use q2:ammo_grenades") == 0 &&
        bindings[i].input.kind == QA_PHYSICAL_INPUT_KEY &&
        bindings[i].input.code == 103) {
      grenades = true;
    }
    if (strcmp(bindings[i].target.text, "+jump") == 0 &&
        bindings[i].input.kind == QA_PHYSICAL_INPUT_KEY &&
        bindings[i].input.code == QA_KEY_SPACE) {
      jump = true;
    }
  }
  CHECK(blaster && grenades && jump);
  for (size_t i = 0; i < binding_count; i++) qa_input_binding_dispose(&bindings[i]);
  free(bindings);
  qa_weapon_binding_items_free(q1, count);
  qa_weapon_binding_items_free(hip, hip_count);
  qa_weapon_binding_items_free(q2, q2_count);
  qa_weapon_binding_items_free(q3, q3_count);
  qa_weapon_binding_items_free(q2_base, base_q2);
}

static void print_sink(void *ctx, const char *text) {
  char *buffer = (char *)ctx;
  strncat(buffer, text, 4095 - strlen(buffer));
}

static bool store_lookup(void *ctx, qa_seat_id seat, qa_binding_command_seat *out) {
  (void)seat;
  *out = qa_binding_store_command_seat((qa_binding_store *)ctx);
  return true;
}

static void test_bindings(void) {
  qa_physical_input physical;
  CHECK(qa_named_physical_input("MOUSE2", 0, &physical) &&
        physical.kind == QA_PHYSICAL_INPUT_MOUSE_BUTTON && physical.button == 3);
  CHECK(qa_named_physical_input("GAMEPAD_RIGHT_TRIGGER", 1, &physical) &&
        physical.kind == QA_PHYSICAL_INPUT_CONTROLLER_AXIS && physical.device == 1 &&
        strcmp(physical.axis, "right-trigger") == 0);
  CHECK(qa_named_physical_input("a_button", 0, &physical) &&
        physical.kind == QA_PHYSICAL_INPUT_CONTROLLER_BUTTON &&
        physical.button == 0);
  char *name = qa_physical_input_name(&physical);
  CHECK(name != NULL && strcmp(name, "Pad 1 A") == 0);
  free(name);
  qa_physical_input rejected;
  CHECK(!qa_named_physical_input("nonsense", 0, &rejected));
  char *wheel = qa_canonical_wheel_command("+wheel");
  CHECK(wheel != NULL && strcmp(wheel, "+weaponwheel") == 0);
  free(wheel);
  wheel = qa_canonical_wheel_command("  -WHEEL2 ");
  CHECK(wheel != NULL && strcmp(wheel, "-powerupwheel") == 0);
  free(wheel);
  wheel = qa_canonical_wheel_command("+attack");
  CHECK(wheel != NULL && strcmp(wheel, "+attack") == 0);
  free(wheel);
  double number = 0;
  CHECK(qa_source_key_number(QA_KEY_MOUSE_WHEEL_UP, QA_GAME_FAMILY_Q2, &number) &&
        number == 240);
  CHECK(qa_source_key_number(QA_KEY_MOUSE_WHEEL_UP, QA_GAME_FAMILY_Q1, &number) &&
        number == 239);
  CHECK(qa_source_key_number(QA_KEY_PAUSE, QA_GAME_FAMILY_Q1, &number) &&
        number == 255);
  CHECK(!qa_source_key_number(300, QA_GAME_FAMILY_Q3, &number));

  /* bind/unbind over a real command buffer. */
  seat_fixture fixture;
  fixture_init(&fixture, "bindings", QA_COMMAND_DIALECT_Q3);
  qa_binding_store *store = qa_binding_store_create(NULL, 0);
  char output[4096] = "";
  qa_binding_commands_registration *registration = qa_register_binding_commands(
      fixture.commands, store_lookup, store, print_sink, output, NULL);
  CHECK(registration != NULL);
  qa_command_buffer_append(fixture.commands, "bind SPACE \"+jump\"\nbind SPACE\n",
                           NULL, -1);
  qa_command_buffer_execute(fixture.commands);
  CHECK(strstr(output, "SPACE = +jump") != NULL);
  qa_physical_input space = key_input(QA_KEY_SPACE);
  CHECK(qa_binding_store_binding(store, &space) != NULL);
  qa_command_buffer_append(fixture.commands, "unbind SPACE\n", NULL, -1);
  qa_command_buffer_execute(fixture.commands);
  CHECK(qa_binding_store_binding(store, &space) == NULL);
  qa_binding_commands_registration_destroy(registration);
  qa_binding_store_destroy(store);
  fixture_destroy(&fixture);

  /* Archived bindings render bind lines after unbindall. */
  qa_input_binding one;
  memset(&one, 0, sizeof one);
  one.input = key_input(119);
  one.target.text = "+forward";
  size_t lines = 0;
  char **archived = qa_archived_bindings(&one, 1, false, &lines);
  CHECK(archived != NULL && lines == 2 && strcmp(archived[0], "unbindall") == 0 &&
        strcmp(archived[1], "bind \"w\" \"+forward\"") == 0);
  for (size_t i = 0; i < lines; i++) free(archived[i]);
  free(archived);
}

typedef struct rumble_log {
  double low;
  double high;
  double duration;
  int calls;
} rumble_log;

static qa_controller_operation_result log_motors(void *ctx, double low, double high,
                                                 double duration) {
  rumble_log *log = (rumble_log *)ctx;
  log->low = low;
  log->high = high;
  log->duration = duration;
  log->calls++;
  qa_controller_operation_result result;
  memset(&result, 0, sizeof result);
  result.kind = QA_CONTROLLER_OPERATION_ACCEPTED;
  return result;
}

static void test_haptics(void) {
  char *path = qa_tactile_path_for_sound("#weapons/shot.wav");
  CHECK(path != NULL && strcmp(path, "tactile/weapons/shot.bnvib") == 0);
  free(path);
  CHECK(qa_tactile_path_for_sound("x.txt") == NULL);
  CHECK(qa_tactile_path_for_sound(".wav") == NULL);
  /* Minimal BNVIB: size 4, format 3, 100 Hz, one sample. */
  uint8_t bytes[] = {4, 0, 0, 0, 3, 0, 100, 0, 4, 0, 0, 0, 255, 0, 128, 0};
  qa_bnvib_pattern pattern;
  CHECK(qa_parse_bnvib(bytes, sizeof bytes, &pattern));
  CHECK(pattern.sample_rate_hz == 100 && pattern.sample_count == 1 &&
        !pattern.has_loop && pattern.samples[0].amp_low == 255 &&
        pattern.samples[0].amp_high == 128);
  rumble_log log;
  memset(&log, 0, sizeof log);
  qa_rumble_sink sink = {&log, log_motors};
  qa_bnvib_scheduler scheduler;
  qa_bnvib_scheduler_init(&scheduler, sink);
  qa_haptics_result result = qa_bnvib_scheduler_play(&scheduler, &pattern, 1000);
  CHECK(!result.unchanged && log.calls == 1 && log.low == 1 &&
        close_to(log.high, 128.0 / 255, 1e-12));
  result = qa_bnvib_scheduler_update(&scheduler, 2000);
  CHECK(!result.unchanged && !qa_bnvib_scheduler_active(&scheduler) &&
        log.low == 0);
  CHECK_ABORTS(qa_bnvib_scheduler_set_strength(&scheduler, 2, 0));
  qa_bnvib_pattern_dispose(&pattern);
}

typedef struct client_log {
  int executions;
  qa_seat_id seat;
} client_log;

static void log_client_execute(void *ctx, qa_command_invocation *command,
                               qa_seat_id seat) {
  (void)command;
  client_log *log = (client_log *)ctx;
  log->executions++;
  log->seat = seat;
}

static void test_client_commands(void) {
  seat_fixture fixture;
  fixture_init(&fixture, "client-commands", QA_COMMAND_DIALECT_Q3);
  client_log log;
  memset(&log, 0, sizeof log);
  qa_client_command_bindings *bindings = qa_client_command_bindings_create(
      fixture.commands, &fixture.seat, 1, log_client_execute, &log);
  CHECK(bindings != NULL);
  qa_client_command_owner *owner =
      qa_client_command_bindings_create_owner(bindings, fixture.seat, NULL);
  CHECK(owner != NULL);
  qa_client_command_owner_register(bindings, owner, "cgizmo");
  qa_client_command_bindings_activate(bindings);
  qa_command_buffer_append(fixture.commands, "cgizmo\n", NULL, -1);
  qa_command_buffer_execute(fixture.commands);
  CHECK(log.executions == 1 && qa_same_seat(log.seat, fixture.seat));
  qa_client_command_bindings_deactivate(bindings);
  qa_client_command_bindings_destroy(bindings);
  fixture_destroy(&fixture);
}

typedef struct fake_midi_io {
  const char *names[4];
  size_t name_count;
} fake_midi_io;

static bool fake_read_directory(void *ctx, char ***out_names, size_t *out_count,
                                qa_midi_error *error) {
  (void)error;
  fake_midi_io *io = (fake_midi_io *)ctx;
  char **names = (char **)calloc(io->name_count, sizeof *names);
  for (size_t i = 0; i < io->name_count; i++) names[i] = qa_input_dup(io->names[i]);
  *out_names = names;
  *out_count = io->name_count;
  return true;
}
static bool fake_open_file(void *ctx, const char *path, int flags, int *out_fd,
                           qa_midi_error *error) {
  (void)ctx;
  (void)path;
  (void)flags;
  (void)error;
  *out_fd = 42;
  return true;
}
static bool fake_is_character(void *ctx, int fd, bool *out, qa_midi_error *error) {
  (void)ctx;
  (void)fd;
  (void)error;
  *out = true;
  return true;
}
static bool fake_read_file(void *ctx, int fd, uint8_t *bytes, size_t length,
                           int64_t *out_count, qa_midi_error *error) {
  (void)ctx;
  (void)fd;
  (void)bytes;
  (void)length;
  error->code = qa_input_dup("EAGAIN");
  error->message = qa_input_dup("again");
  (void)out_count;
  return false;
}
static void fake_close_file(void *ctx, int fd) {
  (void)ctx;
  (void)fd;
}

static void test_midi_and_source_input(void) {
  fake_midi_io fake = {{"midiC1D0", "other", "midiC0D1", "midiC0D0"}, 4};
  qa_midi_file_io io = {&fake, fake_read_directory, fake_open_file,
                        fake_is_character, fake_read_file, fake_close_file};
  qa_linux_midi_input_boundary *boundary =
      qa_linux_midi_input_boundary_create(&io);
  CHECK(boundary != NULL);
  qa_midi_device *devices = NULL;
  size_t count = 0;
  char *message = NULL;
  CHECK(qa_linux_midi_input_boundary_list(boundary, &devices, &count, &message));
  CHECK(count == 3 && strcmp(devices[0].path, "/dev/snd/midiC0D0") == 0 &&
        strcmp(devices[0].name, "ALSA midiC0D0") == 0 &&
        strcmp(devices[2].path, "/dev/snd/midiC1D0") == 0);
  qa_midi_input_handle handle;
  CHECK(qa_linux_midi_input_boundary_open(boundary, &devices[0], &handle, &message));
  handle.destroy(handle.ctx);
  qa_midi_devices_free(devices, count);
  qa_linux_midi_input_boundary_destroy(boundary);
  free(message);

  /* Device settings + source input with the joystick off. */
  CHECK(strcmp(qa_input_device_cvar_names[0], "in_midi") == 0);
  CHECK(strcmp(qa_input_device_cvar_names[14], "in_joystickSeat") == 0);
  seat_fixture fixture;
  fixture_init(&fixture, "source-input", QA_COMMAND_DIALECT_Q3);
  qa_cvar_registry *cvars = make_cvars(QA_COMMAND_DIALECT_Q3, fixture.context);
  char output[4096] = "";
  qa_source_input_options options = {cvars, print_sink, output};
  qa_source_input_state *state = qa_source_input_create(options);
  CHECK(state != NULL);
  qa_source_input_initialize(state);
  CHECK(strstr(output, "Joystick is not active.") != NULL);
  CHECK(!qa_source_input_instance(state, &(int32_t){0}));
  qa_source_input_destroy(state);
  qa_cvar_registry_destroy(cvars);
  fixture_destroy(&fixture);
}

static double fixed_now(void *ctx) {
  (void)ctx;
  return 20;
}

static void ignore_unhandled(void *ctx, const qa_input_unhandled_event *event) {
  (void)ctx;
  (void)event;
}

static void test_router(void) {
  seat_fixture fixture;
  fixture_init(&fixture, "router", QA_COMMAND_DIALECT_Q2_CLASSIC);
  qa_input_binding binding;
  memset(&binding, 0, sizeof binding);
  binding.input = key_input(119);
  binding.target.is_action = true;
  binding.target.action = "forward";
  CHECK(qa_seat_input_bind(fixture.input, &binding));
  qa_input_seat_route route;
  memset(&route, 0, sizeof route);
  route.input = fixture.input;
  route.controller.kind = QA_CONTROLLER_SELECTION_NONE;
  qa_input_router_options options;
  memset(&options, 0, sizeof options);
  options.seats = &route;
  options.seat_count = 1;
  options.has_keyboard_seat = true;
  options.keyboard_seat = fixture.seat;
  options.now = fixed_now;
  options.ticks = fixed_now;
  options.unhandled = ignore_unhandled;
  options.defer_platform = true;
  qa_input_router *staged = qa_input_router_create(&options);
  CHECK(staged != NULL);
  send_key(&fixture, 119, true, 10);
  /* A staged (deferred) router must not release live seat state. */
  qa_input_router_restart(staged);
  qa_input_router_close(staged);
  CHECK(qa_seat_input_is_down(fixture.input, &binding.input));
  CHECK(qa_input_button_active(qa_seat_input_button(fixture.input, "forward")));
  qa_input_router_destroy(staged);
  options.defer_platform = false;
  qa_input_router *active = qa_input_router_create(&options);
  CHECK(active != NULL);
  qa_sdl_event key_event;
  memset(&key_event, 0, sizeof key_event);
  key_event.kind = QA_SDL_EVENT_KEY;
  key_event.timestamp = 20;
  key_event.as.key.scancode = 26;
  key_event.as.key.keycode = 119;
  key_event.as.key.down = false;
  qa_input_router_handle_platform(active, &key_event);
  CHECK(!qa_seat_input_is_down(fixture.input, &binding.input));
  qa_input_router_destroy(active);
  fixture_destroy(&fixture);
}

int main(void) {
  test_impulse();
  test_buttons();
  test_keys();
  test_pitch_drift();
  test_mouse_and_settings();
  test_gamepad();
  test_source_devices();
  test_binding_store();
  test_seat_flow();
  test_user_command();
  test_weapon_bindings();
  test_bindings();
  test_haptics();
  test_client_commands();
  test_midi_and_source_input();
  test_router();
  printf("input: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
