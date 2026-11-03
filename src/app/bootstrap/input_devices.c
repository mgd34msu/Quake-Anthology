/* Port of src/app/bootstrap/input-devices.ts. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "qa/app/bootstrap/input_devices.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "qa/app/bootstrap/config_scripts.h"
#include "qa/foundation/support.h"
#include "qa/input/device_settings.h"
#include "qa/input/source_input.h"
#include "qa/input/support.h"
#include "qa/network/common/js_number.h"

double qa_app_performance_now(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

bool qa_app_input_device_store(const char *user_content_root, qa_config_store *out) {
  char *root = qa_app_console_config_root(user_content_root);
  if (root == NULL) return false;
  bool ok = qa_config_store_init(out, root);
  free(root);
  return ok;
}

qa_cvar_archive_entry *qa_app_load_input_device_settings(const qa_config_store *store, size_t *out_count) {
  const char *owner[] = {"input", "devices"};
  return qa_app_load_cvar_archive(store, owner, 2, QA_COMMAND_DIALECT_Q3, out_count);
}

/* One SettingBinding row of bindings(); ctx of each binding. */
typedef struct binding_row {
  qa_app_input_devices *devices;
  const char *name; /* cvar */
  char *text;       /* last read_str result */
  qa_ui_choice *choices; size_t choice_count;
  char **choice_text; size_t choice_text_count;
} binding_row;
typedef struct binding_state {
  binding_row rows[9];
  qa_ui_setting_binding items[9];
} binding_state;

struct qa_app_input_devices {
  qa_cvar_registry *cvars;
  const qa_config_store *store;
  qa_command_print_fn print; void *print_ctx;
  qa_source_midi_input *midi;
  qa_source_input_state *joystick;
  qa_input_router *router;
  qa_seat_input *midi_seat, *joystick_seat;
  char *signature;
  double retry_at;
  bool closed;
};

static void device_print(void *ctx, const char *text) {
  qa_app_input_devices *devices = ctx;
  devices->print(devices->print_ctx, text);
}

qa_app_input_devices *qa_app_input_devices_create(qa_cvar_registry *cvars, const qa_config_store *store,
    qa_command_print_fn print, void *print_ctx, const qa_midi_input_boundary *midi_boundary) {
  qa_app_input_devices *devices = calloc(1, sizeof *devices);
  if (devices == NULL) return NULL;
  devices->cvars = cvars;
  devices->store = store;
  devices->print = print; devices->print_ctx = print_ctx;
  devices->signature = qa_input_dup("");
  qa_register_input_device_cvars(cvars);
  qa_source_midi_options midi = {cvars, device_print, devices};
  qa_source_input_options joystick = {cvars, device_print, devices};
  devices->midi = qa_source_midi_input_create(midi, midi_boundary);
  devices->joystick = qa_source_input_create(joystick);
  if (devices->signature == NULL || devices->midi == NULL || devices->joystick == NULL) {
    qa_app_input_devices_destroy(devices);
    return NULL;
  }
  return devices;
}

void qa_app_input_devices_destroy(qa_app_input_devices *devices) {
  if (devices == NULL) return;
  if (devices->midi != NULL) qa_source_midi_input_destroy(devices->midi);
  if (devices->joystick != NULL) qa_source_input_destroy(devices->joystick);
  free(devices->signature);
  free(devices);
}

void qa_app_input_devices_bind_print(qa_app_input_devices *devices, qa_command_print_fn print, void *print_ctx) {
  devices->print = print; devices->print_ctx = print_ctx;
}

/* TS selection(): the device cvars joined with "/". */
static char *selection(qa_app_input_devices *devices) {
  static const char *const names[] = {"in_midi", "in_mididevice", "in_midichannel", "in_midiseat",
                                      "in_joystick", "in_joystickProfile", "in_joystickSeat"};
  char *parts[7];
  size_t total = 1;
  for (size_t i = 0; i < 7; i++) {
    parts[i] = qa_cvar_registry_variable_string(devices->cvars, names[i]);
    if (parts[i] == NULL) qa_range_error("Out of memory");
    total += strlen(parts[i]) + 1;
  }
  char *joined = malloc(total);
  if (joined == NULL) qa_range_error("Out of memory");
  size_t length = 0;
  for (size_t i = 0; i < 7; i++) {
    length += (size_t)snprintf(joined + length, total - length, "%s%s", i != 0 ? "/" : "", parts[i]);
    free(parts[i]);
  }
  return joined;
}

/* TS target(name): the router input whose seat index is cvar - 1. */
static qa_seat_input *target(qa_app_input_devices *devices, const char *name) {
  double index = qa_cvar_registry_variable_value(devices->cvars, name) - 1;
  if (devices->router == NULL) return NULL;
  size_t count = 0;
  qa_seat_input **inputs = qa_input_router_inputs(devices->router, &count);
  for (size_t i = 0; i < count; i++)
    if ((double)qa_seat_input_seat(inputs[i]).index == index) return inputs[i];
  return NULL;
}

/* TS key(input, key, down, time). */
static void key_event(qa_seat_input *input, double key, bool down, double time) {
  if (input == NULL) return;
  qa_seat_input_event event;
  memset(&event, 0, sizeof event);
  event.kind = "key";
  event.seat = qa_seat_input_seat(input);
  event.code = key; event.down = down; event.repeat = false;
  event.time_milliseconds = time;
  (void)qa_seat_input_input(input, &event);
}

/* router.setSourceJoystick(this.joystick.instance, this.joystickSeat?.seat ?? null). */
static void publish_joystick(qa_app_input_devices *devices, qa_input_router *router) {
  int32_t instance = 0;
  bool has_instance = qa_source_input_instance(devices->joystick, &instance);
  qa_seat_id seat;
  memset(&seat, 0, sizeof seat);
  bool has_seat = devices->joystick_seat != NULL;
  if (has_seat) seat = qa_seat_input_seat(devices->joystick_seat);
  qa_input_router_set_source_joystick(router, instance, has_instance, seat, has_seat);
}

static void clear_joystick(qa_input_router *router) {
  qa_seat_id none;
  memset(&none, 0, sizeof none);
  qa_input_router_set_source_joystick(router, 0, false, none, false);
}

typedef struct release_ctx { qa_app_input_devices *devices; bool has_commands; qa_seat_input *seat; } release_ctx;
static void release_key(void *ctx, double key, bool down, double timestamp) {
  release_ctx *release = ctx;
  if (!release->has_commands) key_event(release->seat, key, down, timestamp);
}

void qa_app_input_devices_release(qa_app_input_devices *devices, double time,
    const qa_command_append_target *commands) {
  if (commands != NULL) {
    if (devices->midi_seat != NULL) qa_seat_input_release_to(devices->midi_seat, time, commands);
    if (devices->joystick_seat != devices->midi_seat && devices->joystick_seat != NULL)
      qa_seat_input_release_to(devices->joystick_seat, time, commands);
  }
  release_ctx midi = {devices, commands != NULL, devices->midi_seat};
  qa_source_midi_input_release(devices->midi, time, release_key, &midi);
  release_ctx joystick = {devices, commands != NULL, devices->joystick_seat};
  qa_source_input_release_joystick_state(devices->joystick, time, release_key, &joystick);
}

void qa_app_input_devices_activate(qa_app_input_devices *devices, qa_input_router *router) {
  if (devices->closed) qa_input_error("Input device owner is closed");
  qa_app_input_devices_release(devices, qa_app_performance_now(), NULL);
  if (devices->router != NULL) clear_joystick(devices->router);
  devices->router = router;
  devices->midi_seat = target(devices, "in_midiseat");
  devices->joystick_seat = target(devices, "in_joystickSeat");
  if (devices->signature[0] == '\0') qa_app_input_devices_restart(devices);
  publish_joystick(devices, router);
}

void qa_app_input_devices_restart(qa_app_input_devices *devices) {
  if (devices->closed) qa_input_error("Input device owner is closed");
  qa_app_input_devices_release(devices, qa_app_performance_now(), NULL);
  devices->midi_seat = target(devices, "in_midiseat");
  devices->joystick_seat = target(devices, "in_joystickSeat");
  qa_source_midi_input_restart(devices->midi);
  qa_source_input_restart(devices->joystick);
  char *next = selection(devices);
  free(devices->signature);
  devices->signature = next;
  if (devices->router != NULL) publish_joystick(devices, devices->router);
}

static void midi_key(void *ctx, double key, bool down, double time) {
  qa_app_input_devices *devices = ctx;
  key_event(devices->midi_seat, key, down, time);
}
typedef struct joystick_frame_ctx { qa_app_input_devices *devices; double now; } joystick_frame_ctx;
static void joystick_key(void *ctx, double key, bool down, double time) {
  (void)time; /* TS passes the frame's now, not the queued time. */
  joystick_frame_ctx *frame = ctx;
  key_event(frame->devices->joystick_seat, key, down, frame->now);
}
static void joystick_mouse(void *ctx, double x, double y, double time) {
  (void)time; /* TS stamps the motion with the frame's now. */
  joystick_frame_ctx *frame = ctx;
  qa_seat_input *input = frame->devices->joystick_seat;
  if (input == NULL || strcmp(qa_seat_input_get_focus(input).kind, "game") != 0) return;
  qa_seat_input_event event;
  memset(&event, 0, sizeof event);
  event.kind = "mouse-motion";
  event.seat = qa_seat_input_seat(input);
  event.position.x = 0; event.position.y = 0;
  event.delta.x = x; event.delta.y = y;
  event.time_milliseconds = frame->now;
  (void)qa_seat_input_input(input, &event);
}

void qa_app_input_devices_frame(qa_app_input_devices *devices, double now) {
  if (devices->closed || devices->router == NULL) return;
  char *current = selection(devices);
  bool changed = strcmp(current, devices->signature) != 0;
  free(current);
  if (changed) qa_app_input_devices_restart(devices);
  if (now >= devices->retry_at) {
    devices->retry_at = now + 1000;
    int32_t instance;
    if (qa_cvar_registry_variable_value(devices->cvars, "in_midi") != 0 && !qa_source_midi_input_connected(devices->midi))
      qa_source_midi_input_restart(devices->midi);
    if (qa_cvar_registry_variable_value(devices->cvars, "in_joystick") != 0
        && !qa_source_input_instance(devices->joystick, &instance))
      qa_source_input_restart(devices->joystick);
    publish_joystick(devices, devices->router);
  }
  size_t count = 0;
  qa_seat_input **inputs = qa_input_router_inputs(devices->router, &count);
  bool focused = false;
  for (size_t i = 0; i < count && !focused; i++) focused = qa_seat_input_focused(inputs[i]);
  if (!focused) qa_app_input_devices_release(devices, now, NULL);
  qa_source_midi_input_frame(devices->midi, midi_key, devices, now);
  joystick_frame_ctx frame = {devices, now};
  qa_source_input_joystick_frame(devices->joystick, joystick_key, &frame, joystick_mouse, &frame);
  publish_joystick(devices, devices->router);
}

void qa_app_input_devices_info(qa_app_input_devices *devices) { qa_source_midi_input_info(devices->midi); }

/* JSON.stringify string escaping over byte text. */
static void json_string(char **text, size_t *length, size_t *capacity, const char *value) {
  size_t need = strlen(value) * 6 + 3;
  if (*length + need + 1 > *capacity) {
    size_t next = (*length + need + 1) * 2;
    char *grown = realloc(*text, next);
    if (grown == NULL) qa_range_error("Out of memory");
    *text = grown; *capacity = next;
  }
  char *out = *text + *length;
  *out++ = '"';
  for (const unsigned char *at = (const unsigned char *)value; *at != '\0'; at++) {
    unsigned char c = *at;
    if (c == '"' || c == '\\') { *out++ = '\\'; *out++ = (char)c; }
    else if (c == '\b') { *out++ = '\\'; *out++ = 'b'; }
    else if (c == '\f') { *out++ = '\\'; *out++ = 'f'; }
    else if (c == '\n') { *out++ = '\\'; *out++ = 'n'; }
    else if (c == '\r') { *out++ = '\\'; *out++ = 'r'; }
    else if (c == '\t') { *out++ = '\\'; *out++ = 't'; }
    else if (c < 0x20) out += sprintf(out, "\\u%04x", c);
    else *out++ = (char)c;
  }
  *out++ = '"';
  *out = '\0';
  *length = (size_t)(out - *text);
}

static void append_raw(char **text, size_t *length, size_t *capacity, const char *value) {
  size_t need = strlen(value);
  if (*length + need + 1 > *capacity) {
    size_t next = (*length + need + 1) * 2;
    char *grown = realloc(*text, next);
    if (grown == NULL) qa_range_error("Out of memory");
    *text = grown; *capacity = next;
  }
  memcpy(*text + *length, value, need + 1);
  *length += need;
}

void qa_app_input_devices_save(qa_app_input_devices *devices) {
  size_t count = 0;
  qa_cvar_archive_entry *entries = qa_cvar_registry_archive_entries(devices->cvars, NULL, NULL, &count);
  size_t capacity = 256, length = 0;
  char *text = malloc(capacity);
  if (text == NULL) qa_range_error("Out of memory");
  text[0] = '\0';
  append_raw(&text, &length, &capacity, "{\"version\":1,\"dialect\":\"q3\",\"entries\":[");
  bool first = true;
  for (size_t i = 0; i < count; i++) {
    bool device = false;
    for (size_t n = 0; n < QA_INPUT_DEVICE_CVAR_NAME_COUNT && !device; n++)
      device = strcmp(entries[i].name, qa_input_device_cvar_names[n]) == 0;
    if (!device) continue;
    append_raw(&text, &length, &capacity, first ? "{\"name\":" : ",{\"name\":");
    first = false;
    json_string(&text, &length, &capacity, entries[i].name);
    append_raw(&text, &length, &capacity, ",\"value\":");
    json_string(&text, &length, &capacity, entries[i].value);
    append_raw(&text, &length, &capacity, "}");
  }
  append_raw(&text, &length, &capacity, "]}\n");
  qa_app_cvar_archive_entries_free(entries, count);
  bool saved = qa_config_store_dump(devices->store, "cvars/input/devices.json", text);
  free(text);
  if (!saved) qa_input_error("Could not write cvars/input/devices.json");
}

/* --- bindings() --- */
static bool binding_enabled(void *ctx) { (void)ctx; return true; }
static void set_cvar(qa_cvar_registry *cvars, const char *name, const char *value, bool force) {
  qa_cvar_snapshot snapshot;
  if (qa_cvar_registry_set(cvars, name, value, force, &snapshot)) qa_cvar_read_free(&snapshot);
}
static bool toggle_read(void *ctx) {
  binding_row *row = ctx;
  return qa_cvar_registry_variable_value(row->devices->cvars, row->name) != 0;
}
static void toggle_write(void *ctx, bool value) {
  binding_row *row = ctx;
  set_cvar(row->devices->cvars, row->name, value ? "1" : "0", true);
}
static const char *string_read(void *ctx) {
  binding_row *row = ctx;
  free(row->text);
  row->text = qa_cvar_registry_variable_string(row->devices->cvars, row->name);
  return row->text != NULL ? row->text : "";
}
static void string_write(void *ctx, const char *value) {
  binding_row *row = ctx;
  set_cvar(row->devices->cvars, row->name, value, false);
}
static void profile_write(void *ctx, const char *value) {
  binding_row *row = ctx;
  set_cvar(row->devices->cvars, row->name, value, true);
}
static double number_read(void *ctx) {
  binding_row *row = ctx;
  return qa_cvar_registry_variable_value(row->devices->cvars, row->name);
}
static void number_write(void *ctx, double value) {
  binding_row *row = ctx;
  set_cvar(row->devices->cvars, row->name, QA_JS_NUMBER(value), false);
}
static void clear_choices(binding_row *row) {
  for (size_t i = 0; i < row->choice_text_count; i++) free(row->choice_text[i]);
  free(row->choice_text); free(row->choices);
  row->choice_text = NULL; row->choices = NULL;
  row->choice_text_count = 0; row->choice_count = 0;
}
static char *keep_text(binding_row *row, const char *text) {
  char **grown = realloc(row->choice_text, (row->choice_text_count + 1) * sizeof *grown);
  if (grown == NULL) qa_range_error("Out of memory");
  row->choice_text = grown;
  char *copy = qa_input_dup(text);
  if (copy == NULL) qa_range_error("Out of memory");
  row->choice_text[row->choice_text_count++] = copy;
  return copy;
}
static void reserve_choices(binding_row *row, size_t count) {
  row->choices = count == 0 ? NULL : calloc(count, sizeof *row->choices);
  if (count != 0 && row->choices == NULL) qa_range_error("Out of memory");
}
static const qa_ui_choice *seat_choices(void *ctx, size_t *out_count) {
  binding_row *row = ctx;
  clear_choices(row);
  size_t count = 0;
  qa_seat_input **inputs = row->devices->router != NULL ? qa_input_router_inputs(row->devices->router, &count) : NULL;
  reserve_choices(row, count);
  for (size_t i = 0; i < count; i++) {
    char id[32], label[48];
    double index = (double)qa_seat_input_seat(inputs[i]).index + 1;
    snprintf(id, sizeof id, "%s", QA_JS_NUMBER(index));
    snprintf(label, sizeof label, "Player %s", QA_JS_NUMBER(index));
    row->choices[i].id = keep_text(row, id);
    row->choices[i].label = keep_text(row, label);
  }
  row->choice_count = count;
  *out_count = count;
  return row->choices;
}
typedef struct device_list { binding_row *row; qa_midi_device *devices; size_t count; } device_list;
static void list_devices(void *ctx) {
  device_list *list = ctx;
  list->devices = qa_source_midi_input_available_devices(list->row->devices->midi, &list->count);
}
static const qa_ui_choice *midi_choices(void *ctx, size_t *out_count) {
  binding_row *row = ctx;
  clear_choices(row);
  device_list list = {row, NULL, 0};
  qa_caught_error caught;
  if (!qa_try(list_devices, &list, &caught)) {
    /* TS `${String(error)}`: "<name>: <message>". */
    size_t length = strlen(caught.name) + strlen(caught.message) + 40;
    char *line = malloc(length);
    if (line == NULL) qa_range_error("Out of memory");
    snprintf(line, length, "MIDI devices unavailable: %s: %s\n", caught.name, caught.message);
    row->devices->print(row->devices->print_ctx, line);
    free(line);
    *out_count = 0;
    return NULL;
  }
  reserve_choices(row, list.count);
  for (size_t i = 0; i < list.count; i++) {
    char id[32];
    snprintf(id, sizeof id, "%zu", i);
    row->choices[i].id = keep_text(row, id);
    row->choices[i].label = keep_text(row, list.devices[i].name != NULL ? list.devices[i].name : "");
  }
  row->choice_count = list.count;
  qa_midi_devices_free(list.devices, list.count);
  *out_count = row->choice_count;
  return row->choices;
}
static const qa_ui_choice profile_choice_rows[] = {{"linux", "Linux axes"}, {"windows", "Windows POV and ball"}};
static const qa_ui_choice *profile_choices(void *ctx, size_t *out_count) {
  (void)ctx; /* fixed TS list */
  *out_count = 2;
  return profile_choice_rows;
}

static void binding_teardown(void *state) {
  binding_state *bindings = state;
  for (size_t i = 0; i < 9; i++) { free(bindings->rows[i].text); clear_choices(&bindings->rows[i]); }
  free(bindings);
}

qa_ui_setting_bindings qa_app_input_devices_bindings(qa_app_input_devices *devices) {
  binding_state *state = calloc(1, sizeof *state);
  if (state == NULL) qa_range_error("Out of memory");
  static const char *const names[9] = {"in_midi", "in_midiseat", "in_mididevice", "in_midichannel",
                                       "in_joystick", "in_joystickSeat", "in_joystickProfile", "joy_threshold", NULL};
  for (size_t i = 0; i < 8; i++) { state->rows[i].devices = devices; state->rows[i].name = names[i]; }
  qa_ui_setting_binding *items = state->items;
  for (size_t i = 0; i < 8; i++) {
    items[i].ctx = &state->rows[i];
    items[i].category = "input";
    items[i].enabled = binding_enabled;
  }
  /* toggle("in_midi", "MIDI input", "MIDI") */
  items[0].id = "ui:input:in_midi"; items[0].label = "MIDI input"; items[0].group = "MIDI"; items[0].kind = "toggle";
  items[0].read_bool = toggle_read; items[0].write_bool = toggle_write;
  /* seat("in_midiseat", "MIDI player", "MIDI") */
  items[1].id = "ui:input:in_midiseat"; items[1].label = "MIDI player"; items[1].group = "MIDI"; items[1].kind = "choice";
  items[1].read_str = string_read; items[1].write_str = string_write; items[1].choices = seat_choices;
  items[2].id = "ui:input:midi-device"; items[2].label = "MIDI device"; items[2].group = "MIDI"; items[2].kind = "choice";
  items[2].read_str = string_read; items[2].write_str = string_write; items[2].choices = midi_choices;
  items[3].id = "ui:input:midi-channel"; items[3].label = "MIDI channel"; items[3].group = "MIDI"; items[3].kind = "slider";
  items[3].minimum = 1; items[3].maximum = 16; items[3].step = 1;
  items[3].read_num = number_read; items[3].write_num = number_write;
  items[4].id = "ui:input:in_joystick"; items[4].label = "Source joystick input"; items[4].group = "Joystick"; items[4].kind = "toggle";
  items[4].read_bool = toggle_read; items[4].write_bool = toggle_write;
  items[5].id = "ui:input:in_joystickSeat"; items[5].label = "Joystick player"; items[5].group = "Joystick"; items[5].kind = "choice";
  items[5].read_str = string_read; items[5].write_str = string_write; items[5].choices = seat_choices;
  items[6].id = "ui:input:joystick-profile"; items[6].label = "Joystick profile"; items[6].group = "Joystick"; items[6].kind = "choice";
  items[6].read_str = string_read; items[6].write_str = profile_write; items[6].choices = profile_choices;
  items[7].id = "ui:input:joystick-threshold"; items[7].label = "Joystick axis threshold"; items[7].group = "Joystick"; items[7].kind = "slider";
  items[7].minimum = 0.01; items[7].maximum = 1; items[7].step = 0.01;
  items[7].read_num = number_read; items[7].write_num = number_write;
  return qa_ui_setting_bindings_own(items, 8, state, binding_teardown);
}

void qa_app_input_devices_close(qa_app_input_devices *devices) {
  if (devices->closed) return;
  qa_app_input_devices_release(devices, qa_app_performance_now(), NULL);
  if (devices->router != NULL) clear_joystick(devices->router);
  devices->closed = true;
  devices->router = NULL;
  qa_source_midi_input_close(devices->midi);
  qa_source_input_close(devices->joystick);
}
