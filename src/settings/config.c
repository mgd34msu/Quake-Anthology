/* Port of src/settings/config.ts. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "qa/settings/config.h"
#include "qa/network/common/js_number.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdarg.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "qa/core/commands/index.h"
#include "qa/foundation/support.h"

/* Plain TS Errors abort with the identical message (see
 * src/core/cvars/index.c for the same local helper). */
static noreturn void qa_error(const char *format, ...) {
  char text[1536];
  va_list args;
  va_start(args, format);
  vsnprintf(text, sizeof text, format, args);
  va_end(args);
  qa_raise("Error", 0, 0, "%s", text);
}

static char *dup_text(const char *text) {
  size_t length = strlen(text);
  char *out = (char *)malloc(length + 1);
  if (out != NULL) memcpy(out, text, length + 1);
  return out;
}
static void *xalloc(void *pointer) {
  if (pointer == NULL) qa_error("Settings allocation failed");
  return pointer;
}

/* TS String(number): plain decimal in [1e-6, 1e21), exponent form
 * outside it. Finds the shortest round-tripping decimal in the
 * matching notation. */
static void js_number_text(double value, char *out, size_t size) {
  qa_net_js_number(value, out, size);
}

typedef struct text_buffer {
  char *data;
  size_t length;
  size_t capacity;
} text_buffer;
static bool text_add(text_buffer *buffer, const char *text) {
  size_t length = strlen(text);
  if (buffer->length + length + 1 > buffer->capacity) {
    size_t cap = buffer->capacity == 0 ? 256 : buffer->capacity;
    while (cap < buffer->length + length + 1) cap *= 2;
    char *data = (char *)realloc(buffer->data, cap);
    if (data == NULL) return false;
    buffer->data = data;
    buffer->capacity = cap;
  }
  memcpy(buffer->data + buffer->length, text, length + 1);
  buffer->length += length;
  return true;
}
static bool text_add_number(text_buffer *buffer, double value) {
  char text[40];
  js_number_text(value, text, sizeof text);
  return text_add(buffer, text);
}
/* JSON string literal. Bytes pass through unescaped except the JSON
 * structural escapes; TS strings are UTF-16, so lone surrogates in a
 * TS document would have been \udXXX escapes, which NUL-terminated
 * byte strings cannot represent. */
static bool text_add_json_string(text_buffer *buffer, const char *text) {
  if (!text_add(buffer, "\"")) return false;
  for (const unsigned char *p = (const unsigned char *)text; *p != '\0'; p++) {
    char scratch[8];
    const char *piece = scratch;
    switch (*p) {
      case '"': piece = "\\\""; break;
      case '\\': piece = "\\\\"; break;
      case '\b': piece = "\\b"; break;
      case '\f': piece = "\\f"; break;
      case '\n': piece = "\\n"; break;
      case '\r': piece = "\\r"; break;
      case '\t': piece = "\\t"; break;
      default:
        if (*p < 0x20) snprintf(scratch, sizeof scratch, "\\u%04x", *p);
        else {
          scratch[0] = (char)*p;
          scratch[1] = '\0';
        }
        break;
    }
    if (!text_add(buffer, piece)) return false;
  }
  return text_add(buffer, "\"");
}

/* --- TS unknown-value readers (record/number/boolean/...) --- */
static bool is_undefined(const qa_save_value *value) {
  return value == NULL || value->kind == QA_SAVE_UNDEFINED;
}
static const qa_save_value *settings_record(const qa_save_value *value) {
  if (value == NULL || value->kind != QA_SAVE_OBJECT)
    qa_error("Expected a settings object");
  return value;
}
static const qa_save_value *field(const qa_save_value *object, const char *key) {
  return qa_save_value_get(object, key);
}
static double settings_number(const qa_save_value *value) {
  if (value == NULL || value->kind != QA_SAVE_NUMBER || !isfinite(value->number))
    qa_error("Expected a finite settings number");
  return value->number;
}
static double settings_rumble_strength(const qa_save_value *value) {
  double result = is_undefined(value) ? 1 : settings_number(value);
  if (result < 0 || result > 1)
    qa_error("Vibration strength must be between zero and one");
  return result;
}
static double settings_natural(const qa_save_value *value) {
  double result = settings_number(value);
  if (!(result == trunc(result) && fabs(result) <= 9007199254740991.0) || result < 0)
    qa_error("Expected a nonnegative settings integer");
  return result;
}
static bool settings_boolean(const qa_save_value *value) {
  if (value == NULL || value->kind != QA_SAVE_BOOLEAN)
    qa_error("Expected a settings boolean");
  return value->boolean;
}
static const char *settings_string(const qa_save_value *value) {
  if (value == NULL || value->kind != QA_SAVE_STRING || value->text == NULL)
    qa_error("Expected settings text");
  return value->text;
}

static qa_stick_curve parse_curve(const qa_save_value *value) {
  const qa_save_value *input = settings_record(value);
  double deadzone = settings_number(field(input, "deadzone"));
  double exponent = settings_number(field(input, "exponent"));
  const qa_save_value *kind = field(input, "kind");
  qa_stick_curve curve;
  memset(&curve, 0, sizeof curve);
  curve.deadzone = deadzone;
  curve.exponent = exponent;
  if (kind != NULL && kind->kind == QA_SAVE_STRING && kind->text != NULL &&
      strcmp(kind->text, "radial") == 0) {
    curve.kind = QA_STICK_CURVE_RADIAL;
    curve.outer_threshold = settings_number(field(input, "outerThreshold"));
    return curve;
  }
  if (kind != NULL && kind->kind == QA_SAVE_STRING && kind->text != NULL &&
      strcmp(kind->text, "axial") == 0) {
    curve.kind = QA_STICK_CURVE_AXIAL;
    return curve;
  }
  qa_error("Unknown stick curve");
}

/* TS yawAxis "y" | "z" -> qa_gyro_tuning yaw_axis_z. */
static bool parse_yaw_axis_z(const qa_save_value *value) {
  if (value != NULL && value->kind == QA_SAVE_STRING && value->text != NULL) {
    if (strcmp(value->text, "y") == 0) return false;
    if (strcmp(value->text, "z") == 0) return true;
  }
  qa_error("Unknown gyro yaw axis");
}

static qa_gamepad_tuning parse_gamepad(const qa_save_value *value) {
  const qa_save_value *input = settings_record(value);
  const qa_save_value *gyro = settings_record(field(input, "gyro"));
  bool yaw_axis_z = parse_yaw_axis_z(field(gyro, "yawAxis"));
  qa_gamepad_tuning tuning;
  memset(&tuning, 0, sizeof tuning);
  tuning.move = parse_curve(field(input, "move"));
  tuning.look = parse_curve(field(input, "look"));
  tuning.swap_sticks = settings_boolean(field(input, "swapSticks"));
  tuning.yaw_degrees_per_second = settings_number(field(input, "yawDegreesPerSecond"));
  tuning.pitch_degrees_per_second =
      settings_number(field(input, "pitchDegreesPerSecond"));
  tuning.invert_pitch = settings_boolean(field(input, "invertPitch"));
  tuning.forward_sensitivity = settings_number(field(input, "forwardSensitivity"));
  tuning.side_sensitivity = settings_number(field(input, "sideSensitivity"));
  tuning.trigger_threshold = settings_number(field(input, "triggerThreshold"));
  tuning.gyro.enabled = settings_boolean(field(gyro, "enabled"));
  tuning.gyro.yaw_axis_z = yaw_axis_z;
  tuning.gyro.yaw_sensitivity = settings_number(field(gyro, "yawSensitivity"));
  tuning.gyro.pitch_sensitivity = settings_number(field(gyro, "pitchSensitivity"));
  qa_validate_gamepad_tuning(&tuning);
  return tuning;
}

static qa_mouse_tuning parse_mouse(const qa_save_value *value) {
  const qa_save_value *input = settings_record(value);
  qa_mouse_tuning mouse;
  memset(&mouse, 0, sizeof mouse);
  mouse.sensitivity = settings_number(field(input, "sensitivity"));
  mouse.acceleration = settings_number(field(input, "acceleration"));
  mouse.filter = settings_boolean(field(input, "filter"));
  mouse.yaw = settings_number(field(input, "yaw"));
  mouse.pitch = settings_number(field(input, "pitch"));
  mouse.side = settings_number(field(input, "side"));
  mouse.forward = settings_number(field(input, "forward"));
  mouse.look_spring =
      is_undefined(field(input, "lookSpring")) ? false : settings_boolean(field(input, "lookSpring"));
  mouse.look_strafe =
      is_undefined(field(input, "lookStrafe")) ? false : settings_boolean(field(input, "lookStrafe"));
  mouse.free_look = settings_boolean(field(input, "freeLook"));
  mouse.invert_pitch = settings_boolean(field(input, "invertPitch"));
  return mouse;
}

static qa_controller_axis parse_axis(const qa_save_value *value) {
  static const char *const names[] = {"left-x", "left-y", "right-x",
                                      "right-y", "left-trigger", "right-trigger"};
  if (value != NULL && value->kind == QA_SAVE_STRING && value->text != NULL)
    for (size_t i = 0; i < sizeof names / sizeof *names; i++)
      if (strcmp(value->text, names[i]) == 0) return names[i];
  qa_error("Unknown controller axis");
}

static qa_physical_input parse_physical(const qa_save_value *value) {
  const qa_save_value *input = settings_record(value);
  const qa_save_value *kind = field(input, "kind");
  qa_physical_input physical;
  memset(&physical, 0, sizeof physical);
  const char *name =
      (kind != NULL && kind->kind == QA_SAVE_STRING) ? kind->text : NULL;
  if (name != NULL && strcmp(name, "key") == 0) {
    physical.kind = QA_PHYSICAL_INPUT_KEY;
    physical.code = settings_natural(field(input, "code"));
    return physical;
  }
  if (name != NULL && strcmp(name, "mouse-button") == 0) {
    physical.kind = QA_PHYSICAL_INPUT_MOUSE_BUTTON;
    physical.button = settings_natural(field(input, "button"));
    return physical;
  }
  if (name != NULL && strcmp(name, "controller-button") == 0) {
    physical.kind = QA_PHYSICAL_INPUT_CONTROLLER_BUTTON;
    physical.device = settings_natural(field(input, "device"));
    physical.button = settings_natural(field(input, "button"));
    return physical;
  }
  if (name != NULL && strcmp(name, "controller-axis") == 0) {
    const qa_save_value *direction = field(input, "direction");
    const char *text = NULL;
    if (direction != NULL && direction->kind == QA_SAVE_STRING && direction->text != NULL) {
      if (strcmp(direction->text, "positive") == 0) text = "positive";
      if (strcmp(direction->text, "negative") == 0) text = "negative";
    }
    if (text == NULL) qa_error("Unknown axis direction");
    physical.kind = QA_PHYSICAL_INPUT_CONTROLLER_AXIS;
    physical.device = settings_natural(field(input, "device"));
    physical.axis = parse_axis(field(input, "axis"));
    physical.direction = text;
    return physical;
  }
  qa_error("Unknown binding input");
}

static qa_input_action parse_action(const qa_save_value *value) {
  static const char *const names[] = {"attack", "jump", "forward", "back",
                                      "move-left", "move-right", "move-up",
                                      "move-down", "use", "crouch", "walk",
                                      "scores", "next-weapon", "previous-weapon",
                                      "menu"};
  if (value != NULL && value->kind == QA_SAVE_STRING && value->text != NULL)
    for (size_t i = 0; i < sizeof names / sizeof *names; i++)
      if (strcmp(value->text, names[i]) == 0) return names[i];
  qa_error("Unknown input action");
}

static qa_input_binding_target parse_target(const qa_save_value *value) {
  const qa_save_value *input = settings_record(value);
  const qa_save_value *kind = field(input, "kind");
  qa_input_binding_target target;
  memset(&target, 0, sizeof target);
  if (kind != NULL && kind->kind == QA_SAVE_STRING && kind->text != NULL &&
      strcmp(kind->text, "action") == 0) {
    target.is_action = true;
    target.action = parse_action(field(input, "action"));
    return target;
  }
  if (kind != NULL && kind->kind == QA_SAVE_STRING && kind->text != NULL &&
      strcmp(kind->text, "command") == 0) {
    target.is_action = false;
    target.text = (const char *)xalloc(dup_text(settings_string(field(input, "text"))));
    return target;
  }
  qa_error("Unknown binding target");
}

static bool is_hex_guid(const char *text, bool case_insensitive) {
  if (strlen(text) != 32) return false;
  for (int i = 0; i < 32; i++) {
    char c = text[i];
    bool digit = c >= '0' && c <= '9';
    bool lower = c >= 'a' && c <= 'f';
    bool upper = c >= 'A' && c <= 'F';
    if (!digit && !lower && !(case_insensitive && upper)) return false;
  }
  return true;
}

static qa_settings_controller_selection parse_controller(const qa_save_value *value) {
  const qa_save_value *input = settings_record(value);
  const qa_save_value *kind = field(input, "kind");
  qa_settings_controller_selection selection;
  memset(&selection, 0, sizeof selection);
  const char *name =
      (kind != NULL && kind->kind == QA_SAVE_STRING) ? kind->text : NULL;
  if (name != NULL && strcmp(name, "automatic") == 0) {
    selection.kind = QA_CONTROLLER_AUTOMATIC;
    return selection;
  }
  if (name != NULL && strcmp(name, "none") == 0) {
    selection.kind = QA_CONTROLLER_NONE;
    return selection;
  }
  const char *guid = settings_string(field(input, "guid"));
  if (!is_hex_guid(guid, true))
    qa_error("Controller GUID requires 32 hexadecimal digits");
  memcpy(selection.guid, guid, 33);
  if (name != NULL && strcmp(name, "device") == 0) {
    selection.kind = QA_CONTROLLER_DEVICE;
    selection.ordinal = settings_natural(field(input, "ordinal"));
    return selection;
  }
  if (name != NULL && strcmp(name, "serial") == 0) {
    selection.kind = QA_CONTROLLER_SERIAL;
    selection.serial = (char *)xalloc(dup_text(settings_string(field(input, "serial"))));
    return selection;
  }
  qa_error("Unknown controller selection");
}

static bool version_is_one(const qa_save_value *value) {
  return value != NULL && value->kind == QA_SAVE_NUMBER && value->number == 1;
}

void qa_seat_settings_free(qa_seat_settings *settings) {
  /* NULL-safe arrays: a parse that raised part way leaves counts ahead of
   * the allocations. */
  if (settings->bindings != NULL)
    for (size_t i = 0; i < settings->binding_count; i++)
      free((void *)settings->bindings[i].target.text);
  free(settings->bindings);
  if (settings->history != NULL)
    for (size_t i = 0; i < settings->history_count; i++) free(settings->history[i]);
  free(settings->history);
  free(settings->controller.serial);
  memset(settings, 0, sizeof *settings);
}

void qa_parse_seat_settings(const qa_save_value *value, qa_seat_settings *out) {
  const qa_save_value *input = settings_record(value);
  const qa_save_value *bindings = field(input, "bindings");
  if (!version_is_one(field(input, "version")) || bindings == NULL ||
      bindings->kind != QA_SAVE_ARRAY)
    qa_error("Unsupported seat settings document");
  memset(out, 0, sizeof *out);
  out->version = 1;
  out->binding_count = bindings->item_count;
  out->bindings = (qa_input_binding *)xalloc(
      calloc(out->binding_count == 0 ? 1 : out->binding_count, sizeof *out->bindings));
  for (size_t i = 0; i < out->binding_count; i++) {
    const qa_save_value *binding = settings_record(bindings->items[i]);
    out->bindings[i].input = parse_physical(field(binding, "input"));
    out->bindings[i].target = parse_target(field(binding, "target"));
  }
  out->has_always_run = !is_undefined(field(input, "alwaysRun"));
  out->always_run =
      out->has_always_run ? settings_boolean(field(input, "alwaysRun")) : false;
  out->gamepad = parse_gamepad(field(input, "gamepad"));
  out->mouse = parse_mouse(field(input, "mouse"));
  const qa_save_value *history = field(input, "history");
  if (history == NULL || history->kind != QA_SAVE_ARRAY)
    qa_error("Expected a settings list");
  out->history_count = history->item_count;
  out->history = (char **)xalloc(
      calloc(out->history_count == 0 ? 1 : out->history_count, sizeof *out->history));
  for (size_t i = 0; i < out->history_count; i++)
    out->history[i] = (char *)xalloc(dup_text(settings_string(history->items[i])));
  out->rumble = settings_boolean(field(input, "rumble"));
  out->rumble_strength = settings_rumble_strength(field(input, "rumbleStrength"));
  out->controller = parse_controller(field(input, "controller"));
}

void qa_parse_global_input_tuning(const qa_save_value *value,
                                  qa_global_input_tuning *out) {
  const qa_save_value *input = settings_record(value);
  if (!version_is_one(field(input, "version")))
    qa_error("Unsupported global input tuning document");
  memset(out, 0, sizeof *out);
  out->version = 1;
  out->mouse = parse_mouse(field(input, "mouse"));
  out->has_always_run = !is_undefined(field(input, "alwaysRun"));
  out->always_run =
      out->has_always_run ? settings_boolean(field(input, "alwaysRun")) : false;
  out->rumble = settings_boolean(field(input, "rumble"));
  out->rumble_strength = settings_rumble_strength(field(input, "rumbleStrength"));
}

void qa_gyro_profile_free(qa_gyro_profile *profile) {
  free(profile->serial);
  memset(profile, 0, sizeof *profile);
}

void qa_parse_gyro_profile(const qa_save_value *value, qa_gyro_profile *out) {
  const qa_save_value *input = settings_record(value);
  const qa_save_value *identity = settings_record(field(input, "identity"));
  const qa_save_value *tuning = settings_record(field(input, "tuning"));
  if (!version_is_one(field(input, "version")))
    qa_error("Unsupported gyro settings version");
  memset(out, 0, sizeof *out);
  out->version = 1;
  const qa_save_value *kind = field(identity, "kind");
  const char *name =
      (kind != NULL && kind->kind == QA_SAVE_STRING) ? kind->text : NULL;
  if (name != NULL && strcmp(name, "seat") == 0) {
    out->identity_is_device = false;
  } else if (name != NULL && strcmp(name, "device") == 0) {
    const char *guid = settings_string(field(identity, "guid"));
    const char *serial = settings_string(field(identity, "serial"));
    /* The TS length/NUL checks count UTF-16 units; byte length here. */
    if (!is_hex_guid(guid, false) || serial[0] == '\0' || strlen(serial) > 256)
      qa_error("Invalid gyro device identity");
    out->identity_is_device = true;
    memcpy(out->guid, guid, 33);
    out->serial = (char *)xalloc(dup_text(serial));
  } else {
    qa_error("Unknown gyro profile identity");
  }
  out->tuning.yaw_axis_z = parse_yaw_axis_z(field(tuning, "yawAxis"));
  out->tuning.enabled = settings_boolean(field(tuning, "enabled"));
  out->tuning.yaw_sensitivity = settings_number(field(tuning, "yawSensitivity"));
  out->tuning.pitch_sensitivity = settings_number(field(tuning, "pitchSensitivity"));
}

/* --- settingsPath (node:path resolve/relative, POSIX lexical form) --- */
static char *normalize_absolute(const char *path) {
  /* Split on '/', dropping "" and ".", resolving ".." lexically. */
  size_t length = strlen(path);
  char *copy = (char *)malloc(length + 1);
  if (copy == NULL) return NULL;
  memcpy(copy, path, length + 1);
  char **parts = (char **)calloc(length / 2 + 2, sizeof *parts);
  char *out = (char *)malloc(length + 2);
  if (parts == NULL || out == NULL) {
    free(copy);
    free(parts);
    free(out);
    return NULL;
  }
  size_t count = 0;
  char *cursor = copy;
  while (*cursor != '\0') {
    while (*cursor == '/') cursor++;
    if (*cursor == '\0') break;
    char *start = cursor;
    while (*cursor != '\0' && *cursor != '/') cursor++;
    if (*cursor == '/') *cursor++ = '\0';
    if (strcmp(start, ".") == 0) continue;
    if (strcmp(start, "..") == 0) {
      if (count > 0) count--;
      continue;
    }
    parts[count++] = start;
  }
  size_t used = 0;
  out[used++] = '/';
  for (size_t i = 0; i < count; i++) {
    size_t part_length = strlen(parts[i]);
    memcpy(out + used, parts[i], part_length);
    used += part_length;
    if (i + 1 < count) out[used++] = '/';
  }
  out[used] = '\0';
  free(copy);
  free(parts);
  return out;
}

static char *absolute_of(const char *path) {
  if (path[0] == '/') return normalize_absolute(path);
  char cwd[4096];
  if (getcwd(cwd, sizeof cwd) == NULL) return NULL;
  size_t length = strlen(cwd) + 1 + strlen(path) + 1;
  char *joined = (char *)malloc(length);
  if (joined == NULL) return NULL;
  snprintf(joined, length, "%s/%s", cwd, path);
  char *normalized = normalize_absolute(joined);
  free(joined);
  return normalized;
}

char *qa_settings_path(const char *root, const char *name) {
  char *resolved_root = absolute_of(root);
  if (resolved_root == NULL) return NULL;
  char *path = NULL;
  if (name[0] == '/') {
    path = normalize_absolute(name);
  } else {
    size_t length = strlen(resolved_root) + 1 + strlen(name) + 1;
    char *joined = (char *)malloc(length);
    if (joined != NULL) {
      snprintf(joined, length, "%s/%s", resolved_root, name);
      path = normalize_absolute(joined);
      free(joined);
    }
  }
  if (path == NULL) {
    free(resolved_root);
    return NULL;
  }
  size_t root_length = strlen(resolved_root);
  bool inside = strncmp(path, resolved_root, root_length) == 0 &&
                (root_length == 1 || path[root_length] == '/') &&
                path[root_length] != '\0';
  free(resolved_root);
  if (!inside) {
    free(path);
    qa_error("Settings path escapes the writable directory");
  }
  return path;
}

/* --- writeAtomic --- */
static bool make_directories(const char *path) {
  /* mkdir -p for every prefix of the directory path. */
  char *copy = dup_text(path);
  if (copy == NULL) return false;
  bool ok = true;
  for (size_t i = 1; copy[i] != '\0'; i++) {
    if (copy[i] != '/') continue;
    copy[i] = '\0';
    if (mkdir(copy, 0777) != 0 && errno != EEXIST) ok = false;
    copy[i] = '/';
    if (!ok) break;
  }
  if (ok && mkdir(copy, 0777) != 0 && errno != EEXIST) ok = false;
  free(copy);
  return ok;
}

bool qa_write_atomic(const char *path, const void *contents, size_t length) {
  const char *slash = strrchr(path, '/');
  if (slash != NULL && slash != path) {
    size_t dir_length = (size_t)(slash - path);
    char *dir = (char *)malloc(dir_length + 1);
    if (dir == NULL) return false;
    memcpy(dir, path, dir_length);
    dir[dir_length] = '\0';
    bool made = make_directories(dir);
    free(dir);
    if (!made) return false;
  }
  size_t template_length = strlen(path) + 16;
  char *temporary = (char *)malloc(template_length);
  if (temporary == NULL) return false;
  snprintf(temporary, template_length, "%s..tmpXXXXXX", path);
  int fd = mkstemp(temporary); /* 0600, matching the TS open mode */
  if (fd < 0) {
    free(temporary);
    return false;
  }
  bool ok = true;
  const uint8_t *bytes = (const uint8_t *)contents;
  size_t written = 0;
  while (written < length) {
    ssize_t chunk = write(fd, bytes + written, length - written);
    if (chunk <= 0) {
      ok = false;
      break;
    }
    written += (size_t)chunk;
  }
  if (ok && fsync(fd) != 0) ok = false; /* TS file.sync() */
  if (close(fd) != 0) ok = false;
  if (ok && rename(temporary, path) != 0) ok = false;
  if (ok) {
    /* TS: open(dirname(path), "r") and directory.sync(), so the rename is durable. */
    char *directory = NULL;
    if (slash == NULL) directory = dup_text(".");
    else if (slash == path) directory = dup_text("/");
    else {
      directory = (char *)malloc((size_t)(slash - path) + 1);
      if (directory != NULL) {
        memcpy(directory, path, (size_t)(slash - path));
        directory[slash - path] = '\0';
      }
    }
    int directory_fd = directory != NULL ? open(directory, O_RDONLY) : -1;
    if (directory_fd < 0 || fsync(directory_fd) != 0) ok = false;
    if (directory_fd >= 0 && close(directory_fd) != 0) ok = false;
    free(directory);
  }
  unlink(temporary); /* the TS rm(force) finally; ENOENT after rename */
  free(temporary);
  return ok;
}

bool qa_write_atomic_text(const char *path, const char *contents) {
  return qa_write_atomic(path, contents, strlen(contents));
}

/* --- JSON writers (JSON.stringify shapes; key order follows the TS
 * object literals in the parsers above) --- */
static bool add_curve(text_buffer *b, const qa_stick_curve *curve, bool pretty,
                      int depth);
static bool add_indent(text_buffer *b, bool pretty, int depth) {
  if (!pretty) return true;
  if (!text_add(b, "\n")) return false;
  for (int i = 0; i < depth * 2; i++)
    if (!text_add(b, " ")) return false;
  return true;
}
static bool add_key(text_buffer *b, const char *key, bool pretty, int depth,
                    bool first) {
  if (!first && !text_add(b, ",")) return false;
  if (!add_indent(b, pretty, depth)) return false;
  if (!text_add_json_string(b, key)) return false;
  return text_add(b, pretty ? ": " : ":");
}
static bool add_close(text_buffer *b, bool pretty, int depth, const char *close) {
  if (pretty && depth > 0 && !add_indent(b, pretty, depth - 1)) return false;
  return text_add(b, close);
}
static bool add_bool(text_buffer *b, bool value) {
  return text_add(b, value ? "true" : "false");
}
static bool add_curve(text_buffer *b, const qa_stick_curve *curve, bool pretty,
                      int depth) {
  if (!text_add(b, "{")) return false;
  if (!add_key(b, "kind", pretty, depth, true)) return false;
  bool radial = curve->kind == QA_STICK_CURVE_RADIAL;
  if (!text_add_json_string(b, radial ? "radial" : "axial")) return false;
  if (!add_key(b, "deadzone", pretty, depth, false)) return false;
  if (!text_add_number(b, curve->deadzone)) return false;
  if (!add_key(b, "exponent", pretty, depth, false)) return false;
  if (!text_add_number(b, curve->exponent)) return false;
  if (radial) {
    if (!add_key(b, "outerThreshold", pretty, depth, false)) return false;
    if (!text_add_number(b, curve->outer_threshold)) return false;
  }
  return add_close(b, pretty, depth, "}");
}
static bool add_gyro(text_buffer *b, const qa_gyro_tuning *gyro, bool pretty,
                     int depth) {
  if (!text_add(b, "{")) return false;
  if (!add_key(b, "enabled", pretty, depth, true)) return false;
  if (!add_bool(b, gyro->enabled)) return false;
  if (!add_key(b, "yawAxis", pretty, depth, false)) return false;
  if (!text_add_json_string(b, gyro->yaw_axis_z ? "z" : "y")) return false;
  if (!add_key(b, "yawSensitivity", pretty, depth, false)) return false;
  if (!text_add_number(b, gyro->yaw_sensitivity)) return false;
  if (!add_key(b, "pitchSensitivity", pretty, depth, false)) return false;
  if (!text_add_number(b, gyro->pitch_sensitivity)) return false;
  return add_close(b, pretty, depth, "}");
}
static bool add_gamepad(text_buffer *b, const qa_gamepad_tuning *pad, bool pretty,
                        int depth) {
  if (!text_add(b, "{")) return false;
  if (!add_key(b, "move", pretty, depth, true)) return false;
  if (!add_curve(b, &pad->move, pretty, depth + 1)) return false;
  if (!add_key(b, "look", pretty, depth, false)) return false;
  if (!add_curve(b, &pad->look, pretty, depth + 1)) return false;
  if (!add_key(b, "swapSticks", pretty, depth, false)) return false;
  if (!add_bool(b, pad->swap_sticks)) return false;
  if (!add_key(b, "yawDegreesPerSecond", pretty, depth, false)) return false;
  if (!text_add_number(b, pad->yaw_degrees_per_second)) return false;
  if (!add_key(b, "pitchDegreesPerSecond", pretty, depth, false)) return false;
  if (!text_add_number(b, pad->pitch_degrees_per_second)) return false;
  if (!add_key(b, "invertPitch", pretty, depth, false)) return false;
  if (!add_bool(b, pad->invert_pitch)) return false;
  if (!add_key(b, "forwardSensitivity", pretty, depth, false)) return false;
  if (!text_add_number(b, pad->forward_sensitivity)) return false;
  if (!add_key(b, "sideSensitivity", pretty, depth, false)) return false;
  if (!text_add_number(b, pad->side_sensitivity)) return false;
  if (!add_key(b, "triggerThreshold", pretty, depth, false)) return false;
  if (!text_add_number(b, pad->trigger_threshold)) return false;
  if (!add_key(b, "gyro", pretty, depth, false)) return false;
  if (!add_gyro(b, &pad->gyro, pretty, depth + 1)) return false;
  return add_close(b, pretty, depth, "}");
}
static bool add_mouse(text_buffer *b, const qa_mouse_tuning *mouse, bool pretty,
                      int depth) {
  if (!text_add(b, "{")) return false;
  if (!add_key(b, "sensitivity", pretty, depth, true)) return false;
  if (!text_add_number(b, mouse->sensitivity)) return false;
  if (!add_key(b, "acceleration", pretty, depth, false)) return false;
  if (!text_add_number(b, mouse->acceleration)) return false;
  if (!add_key(b, "filter", pretty, depth, false)) return false;
  if (!add_bool(b, mouse->filter)) return false;
  if (!add_key(b, "yaw", pretty, depth, false)) return false;
  if (!text_add_number(b, mouse->yaw)) return false;
  if (!add_key(b, "pitch", pretty, depth, false)) return false;
  if (!text_add_number(b, mouse->pitch)) return false;
  if (!add_key(b, "side", pretty, depth, false)) return false;
  if (!text_add_number(b, mouse->side)) return false;
  if (!add_key(b, "forward", pretty, depth, false)) return false;
  if (!text_add_number(b, mouse->forward)) return false;
  if (!add_key(b, "lookSpring", pretty, depth, false)) return false;
  if (!add_bool(b, mouse->look_spring)) return false;
  if (!add_key(b, "lookStrafe", pretty, depth, false)) return false;
  if (!add_bool(b, mouse->look_strafe)) return false;
  if (!add_key(b, "freeLook", pretty, depth, false)) return false;
  if (!add_bool(b, mouse->free_look)) return false;
  if (!add_key(b, "invertPitch", pretty, depth, false)) return false;
  if (!add_bool(b, mouse->invert_pitch)) return false;
  return add_close(b, pretty, depth, "}");
}
static bool add_physical(text_buffer *b, const qa_physical_input *input,
                         bool pretty, int depth) {
  if (!text_add(b, "{")) return false;
  if (!add_key(b, "kind", pretty, depth, true)) return false;
  switch (input->kind) {
    case QA_PHYSICAL_INPUT_KEY:
      if (!text_add_json_string(b, "key")) return false;
      if (!add_key(b, "code", pretty, depth, false)) return false;
      if (!text_add_number(b, input->code)) return false;
      break;
    case QA_PHYSICAL_INPUT_MOUSE_BUTTON:
      if (!text_add_json_string(b, "mouse-button")) return false;
      if (!add_key(b, "button", pretty, depth, false)) return false;
      if (!text_add_number(b, input->button)) return false;
      break;
    case QA_PHYSICAL_INPUT_CONTROLLER_BUTTON:
      if (!text_add_json_string(b, "controller-button")) return false;
      if (!add_key(b, "device", pretty, depth, false)) return false;
      if (!text_add_number(b, input->device)) return false;
      if (!add_key(b, "button", pretty, depth, false)) return false;
      if (!text_add_number(b, input->button)) return false;
      break;
    case QA_PHYSICAL_INPUT_CONTROLLER_AXIS:
      if (!text_add_json_string(b, "controller-axis")) return false;
      if (!add_key(b, "device", pretty, depth, false)) return false;
      if (!text_add_number(b, input->device)) return false;
      if (!add_key(b, "axis", pretty, depth, false)) return false;
      if (!text_add_json_string(b, input->axis)) return false;
      if (!add_key(b, "direction", pretty, depth, false)) return false;
      if (!text_add_json_string(b, input->direction)) return false;
      break;
  }
  return add_close(b, pretty, depth, "}");
}
static bool add_binding_target(text_buffer *b, const qa_input_binding_target *target,
                               bool pretty, int depth) {
  if (!text_add(b, "{")) return false;
  if (!add_key(b, "kind", pretty, depth, true)) return false;
  if (target->is_action) {
    if (!text_add_json_string(b, "action")) return false;
    if (!add_key(b, "action", pretty, depth, false)) return false;
    if (!text_add_json_string(b, target->action)) return false;
  } else {
    if (!text_add_json_string(b, "command")) return false;
    if (!add_key(b, "text", pretty, depth, false)) return false;
    if (!text_add_json_string(b, target->text != NULL ? target->text : "")) return false;
  }
  return add_close(b, pretty, depth, "}");
}
static bool add_controller(text_buffer *b, const qa_settings_controller_selection *selection,
                           bool pretty, int depth) {
  if (!text_add(b, "{")) return false;
  if (!add_key(b, "kind", pretty, depth, true)) return false;
  switch (selection->kind) {
    case QA_CONTROLLER_AUTOMATIC:
      if (!text_add_json_string(b, "automatic")) return false;
      break;
    case QA_CONTROLLER_NONE:
      if (!text_add_json_string(b, "none")) return false;
      break;
    case QA_CONTROLLER_DEVICE:
      if (!text_add_json_string(b, "device")) return false;
      if (!add_key(b, "guid", pretty, depth, false)) return false;
      if (!text_add_json_string(b, selection->guid)) return false;
      if (!add_key(b, "ordinal", pretty, depth, false)) return false;
      if (!text_add_number(b, selection->ordinal)) return false;
      break;
    case QA_CONTROLLER_SERIAL:
      if (!text_add_json_string(b, "serial")) return false;
      if (!add_key(b, "guid", pretty, depth, false)) return false;
      if (!text_add_json_string(b, selection->guid)) return false;
      if (!add_key(b, "serial", pretty, depth, false)) return false;
      if (!text_add_json_string(b, selection->serial != NULL ? selection->serial : ""))
        return false;
      break;
  }
  return add_close(b, pretty, depth, "}");
}
static bool add_bindings_json(text_buffer *b, const qa_input_binding *bindings, size_t count, bool pretty, int depth) {
  if (!text_add(b, "[")) return false;
  for (size_t i = 0; i < count; i++) {
    if (i > 0 && !text_add(b, ",")) return false;
    if (!add_indent(b, pretty, depth)) return false;
    if (!text_add(b, "{")) return false;
    if (!add_key(b, "input", pretty, depth + 1, true)) return false;
    if (!add_physical(b, &bindings[i].input, pretty, depth + 2)) return false;
    if (!add_key(b, "target", pretty, depth + 1, false)) return false;
    if (!add_binding_target(b, &bindings[i].target, pretty, depth + 2)) return false;
    if (!add_close(b, pretty, depth + 1, "}")) return false;
  }
  if (count > 0 && !add_close(b, pretty, depth, "]")) return false;
  if (count == 0 && !text_add(b, "]")) return false;
  return true;
}
static bool add_seat_json_with(text_buffer *b, const qa_seat_settings *settings, bool pretty);
static bool add_seat_json(text_buffer *b, const qa_seat_settings *settings) { return add_seat_json_with(b, settings, true); }
static bool add_seat_json_with(text_buffer *b, const qa_seat_settings *settings, bool pretty) {
  if (!text_add(b, "{")) return false;
  if (!add_key(b, "version", pretty, 1, true)) return false;
  if (!text_add_number(b, settings->version)) return false;
  if (!add_key(b, "bindings", pretty, 1, false)) return false;
  if (!add_bindings_json(b, settings->bindings, settings->binding_count, pretty, 2)) return false;
  if (settings->has_always_run) {
    if (!add_key(b, "alwaysRun", pretty, 1, false)) return false;
    if (!add_bool(b, settings->always_run)) return false;
  }
  if (!add_key(b, "gamepad", pretty, 1, false)) return false;
  if (!add_gamepad(b, &settings->gamepad, pretty, 2)) return false;
  if (!add_key(b, "mouse", pretty, 1, false)) return false;
  if (!add_mouse(b, &settings->mouse, pretty, 2)) return false;
  if (!add_key(b, "history", pretty, 1, false)) return false;
  if (!text_add(b, "[")) return false;
  for (size_t i = 0; i < settings->history_count; i++) {
    if (i > 0 && !text_add(b, ",")) return false;
    if (!add_indent(b, pretty, 2)) return false;
    if (!text_add_json_string(b, settings->history[i])) return false;
  }
  if (settings->history_count > 0 && !add_close(b, pretty, 2, "]")) return false;
  if (settings->history_count == 0 && !text_add(b, "]")) return false;
  if (!add_key(b, "rumble", pretty, 1, false)) return false;
  if (!add_bool(b, settings->rumble)) return false;
  if (!add_key(b, "rumbleStrength", pretty, 1, false)) return false;
  if (!text_add_number(b, settings->rumble_strength)) return false;
  if (!add_key(b, "controller", pretty, 1, false)) return false;
  if (!add_controller(b, &settings->controller, pretty, 2)) return false;
  return add_close(b, pretty, 1, "}");
}
/* Compact JSON.stringify texts (key order follows the parsers). */
char *qa_seat_settings_json(const qa_seat_settings *settings) {
  text_buffer buffer = {NULL, 0, 0};
  if (!add_seat_json_with(&buffer, settings, false)) { free(buffer.data); return NULL; }
  return buffer.data;
}
char *qa_input_bindings_json(const qa_input_binding *bindings, size_t count) {
  text_buffer buffer = {NULL, 0, 0};
  if (!add_bindings_json(&buffer, bindings, count, false, 0)) { free(buffer.data); return NULL; }
  return buffer.data;
}
char *qa_gamepad_tuning_json(const qa_gamepad_tuning *tuning) {
  text_buffer buffer = {NULL, 0, 0};
  if (!add_gamepad(&buffer, tuning, false, 0)) { free(buffer.data); return NULL; }
  return buffer.data;
}
static bool add_global_json(text_buffer *b, const qa_global_input_tuning *tuning) {
  const bool pretty = true;
  if (!text_add(b, "{")) return false;
  if (!add_key(b, "version", pretty, 1, true)) return false;
  if (!text_add_number(b, tuning->version)) return false;
  if (!add_key(b, "mouse", pretty, 1, false)) return false;
  if (!add_mouse(b, &tuning->mouse, pretty, 2)) return false;
  if (tuning->has_always_run) {
    if (!add_key(b, "alwaysRun", pretty, 1, false)) return false;
    if (!add_bool(b, tuning->always_run)) return false;
  }
  if (!add_key(b, "rumble", pretty, 1, false)) return false;
  if (!add_bool(b, tuning->rumble)) return false;
  if (!add_key(b, "rumbleStrength", pretty, 1, false)) return false;
  if (!text_add_number(b, tuning->rumble_strength)) return false;
  return add_close(b, pretty, 1, "}");
}
static bool add_gyro_json(text_buffer *b, const qa_gyro_profile *profile) {
  if (!text_add(b, "{")) return false;
  if (!add_key(b, "version", false, 1, true)) return false;
  if (!text_add_number(b, profile->version)) return false;
  if (!add_key(b, "identity", false, 1, false)) return false;
  if (!text_add(b, "{")) return false;
  if (!add_key(b, "kind", false, 2, true)) return false;
  if (profile->identity_is_device) {
    if (!text_add_json_string(b, "device")) return false;
    if (!add_key(b, "guid", false, 2, false)) return false;
    if (!text_add_json_string(b, profile->guid)) return false;
    if (!add_key(b, "serial", false, 2, false)) return false;
    if (!text_add_json_string(b, profile->serial != NULL ? profile->serial : ""))
      return false;
  } else {
    if (!text_add_json_string(b, "seat")) return false;
  }
  if (!add_close(b, false, 2, "}")) return false;
  if (!add_key(b, "tuning", false, 1, false)) return false;
  if (!add_gyro(b, &profile->tuning, false, 2)) return false;
  return add_close(b, false, 1, "}");
}

/* --- ConfigStore --- */
bool qa_config_store_init(qa_config_store *store, const char *root) {
  store->root = dup_text(root);
  return store->root != NULL;
}
void qa_config_store_destroy(qa_config_store *store) {
  free(store->root);
  memset(store, 0, sizeof *store);
}

/* Bun.file(path).text() when it exists, else the TS null. Bytes are
 * kept as-is (Bun decodes UTF-8 lossily; qa_parse_json_value is
 * equally lenient, and malformed bytes still fail JSON parsing). */
static char *read_file_bytes(const char *path, size_t *out_length) {
  FILE *file = fopen(path, "rb");
  if (file == NULL) return NULL;
  size_t capacity = 4096;
  size_t length = 0;
  char *data = (char *)malloc(capacity);
  if (data == NULL) {
    fclose(file);
    return NULL;
  }
  for (;;) {
    if (length + 2048 + 1 > capacity) {
      capacity *= 2;
      char *grown = (char *)realloc(data, capacity);
      if (grown == NULL) {
        free(data);
        fclose(file);
        return NULL;
      }
      data = grown;
    }
    size_t read = fread(data + length, 1, 2048, file);
    length += read;
    if (read < 2048) break;
  }
  fclose(file);
  data[length] = '\0';
  if (out_length != NULL) *out_length = length;
  return data;
}

char *qa_config_store_load_text(const qa_config_store *store, const char *name,
                                size_t *out_length) {
  char *path = qa_settings_path(store->root, name);
  if (path == NULL) return NULL;
  char *text = read_file_bytes(path, out_length);
  free(path);
  return text;
}

bool qa_config_store_dump(const qa_config_store *store, const char *name,
                          const char *contents) {
  char *path = qa_settings_path(store->root, name);
  if (path == NULL) return false;
  bool ok = qa_write_atomic_text(path, contents);
  free(path);
  return ok;
}

/* ConfigStore.parsed: a document that is not JSON at all behaves
 * like the absent one; parsed values still face validation. Returns
 * NULL for the TS null (undecodable or JSON null). */
static qa_save_value *parsed_document(const char *text, size_t length) {
  qa_save_value *value =
      qa_parse_json_value((const uint8_t *)text, length, true);
  if (value != NULL && value->kind == QA_SAVE_NULL) {
    qa_save_value_free(value);
    return NULL;
  }
  return value;
}

/* TS ConfigStore.decoded: decode and validate one stored document. One that
 * cannot be read is renamed to `.bad` so the next save does not erase it,
 * and loads as absent (false). parse fills *out (zeroed first); free_out
 * releases a partial *out when parse raises. Takes ownership of text. */
typedef struct decode_job {
  const qa_save_value *value;
  void (*parse)(const qa_save_value *value, void *out);
  void *out;
  void (*free_out)(void *out);
} decode_job;
static void decode_run(void *ctx) {
  decode_job *job = (decode_job *)ctx;
  qa_cleanup node;
  if (job->free_out != NULL) qa_cleanup_push(&node, job->free_out, job->out);
  job->parse(job->value, job->out);
  if (job->free_out != NULL) qa_cleanup_pop(&node);
}
static bool config_decoded(const qa_config_store *store, const char *name,
                           char *text, size_t length,
                           void (*parse)(const qa_save_value *, void *),
                           void *out, size_t out_size,
                           void (*free_out)(void *)) {
  qa_save_value *value = parsed_document(text, length);
  free(text);
  if (value != NULL) {
    memset(out, 0, out_size);
    decode_job job = {value, parse, out, free_out};
    qa_caught_error caught;
    const bool parsed = qa_try(decode_run, &job, &caught);
    qa_save_value_free(value);
    if (parsed) return true;
    /* quarantined below like a document that is not JSON */
    memset(out, 0, out_size);
  }
  char *path = qa_settings_path(store->root, name);
  if (path != NULL) {
    size_t path_length = strlen(path);
    char *bad = (char *)malloc(path_length + 5);
    if (bad != NULL) {
      memcpy(bad, path, path_length);
      memcpy(bad + path_length, ".bad", 5);
      /* the document may already be gone or read-only; defaults still apply */
      (void)rename(path, bad);
      free(bad);
    }
    free(path);
  }
  return false;
}
static void parse_gyro_job(const qa_save_value *value, void *out) {
  qa_parse_gyro_profile(value, (qa_gyro_profile *)out);
}
static void free_gyro_job(void *out) { qa_gyro_profile_free((qa_gyro_profile *)out); }
static void parse_seat_job(const qa_save_value *value, void *out) {
  qa_parse_seat_settings(value, (qa_seat_settings *)out);
}
static void free_seat_job(void *out) { qa_seat_settings_free((qa_seat_settings *)out); }
static void parse_tuning_job(const qa_save_value *value, void *out) {
  qa_parse_global_input_tuning(value, (qa_global_input_tuning *)out);
}
typedef struct input_routing_result {
  double seat;
  bool has_seat;
} input_routing_result;
static void parse_input_routing_job(const qa_save_value *value, void *out) {
  input_routing_result *result = (input_routing_result *)out;
  const qa_save_value *data = settings_record(value);
  if (!version_is_one(field(data, "version")))
    qa_error("Unsupported input routing document");
  const qa_save_value *seat = field(data, "keyboardSeat");
  if (seat != NULL && seat->kind == QA_SAVE_NULL) {
    result->has_seat = false;
    result->seat = 0;
  } else {
    result->seat = settings_natural(seat);
    result->has_seat = true;
  }
}

bool qa_config_store_load_gyro(const qa_config_store *store, const char *name,
                               qa_gyro_profile *out) {
  size_t length = 0;
  char *text = qa_config_store_load_text(store, name, &length);
  if (text == NULL || length > 8192) {
    free(text);
    return false;
  }
  return config_decoded(store, name, text, length, parse_gyro_job, out,
                        sizeof *out, free_gyro_job);
}

bool qa_config_store_save_gyro(const qa_config_store *store, const char *name,
                               const qa_gyro_profile *profile) {
  /* TS saveGyro re-validates via parseGyroProfile(profile). */
  if (profile->version != 1) qa_error("Unsupported gyro settings version");
  if (profile->identity_is_device) {
    const char *serial = profile->serial != NULL ? profile->serial : "";
    if (!is_hex_guid(profile->guid, false) || serial[0] == '\0' ||
        strlen(serial) > 256)
      qa_error("Invalid gyro device identity");
  }
  /* yaw_axis_z is a bool: the TS "Unknown gyro yaw axis" case is
   * unrepresentable here. */
  if (!isfinite(profile->tuning.yaw_sensitivity) ||
      !isfinite(profile->tuning.pitch_sensitivity))
    qa_error("Expected a finite settings number");
  text_buffer buffer = {NULL, 0, 0};
  if (!add_gyro_json(&buffer, profile) || !text_add(&buffer, "\n")) {
    free(buffer.data);
    return false;
  }
  bool ok = qa_config_store_dump(store, name, buffer.data);
  free(buffer.data);
  return ok;
}

bool qa_config_store_save_input_routing(const qa_config_store *store,
                                        const char *name, double keyboard_seat,
                                        bool has_keyboard_seat) {
  if (has_keyboard_seat &&
      (!(keyboard_seat == trunc(keyboard_seat) &&
         fabs(keyboard_seat) <= 9007199254740991.0) ||
       keyboard_seat < 0 || !isfinite(keyboard_seat)))
    qa_error("Expected a nonnegative settings integer");
  text_buffer buffer = {NULL, 0, 0};
  bool built = text_add(&buffer, "{\"version\":1,\"keyboardSeat\":");
  if (built && has_keyboard_seat) built = text_add_number(&buffer, keyboard_seat);
  if (built && !has_keyboard_seat) built = text_add(&buffer, "null");
  if (built) built = text_add(&buffer, "}\n");
  if (!built) {
    free(buffer.data);
    return false;
  }
  bool ok = qa_config_store_dump(store, name, buffer.data);
  free(buffer.data);
  return ok;
}

bool qa_config_store_load_input_routing(const qa_config_store *store,
                                        const char *name, double *out_seat,
                                        bool *out_has_seat) {
  size_t length = 0;
  char *text = qa_config_store_load_text(store, name, &length);
  if (text == NULL) return false;
  input_routing_result result;
  if (!config_decoded(store, name, text, length, parse_input_routing_job,
                      &result, sizeof result, NULL))
    return false;
  *out_seat = result.seat;
  *out_has_seat = result.has_seat;
  return true;
}

bool qa_config_store_save_seat(const qa_config_store *store, const char *name,
                               const qa_seat_settings *settings) {
  char *path = qa_settings_path(store->root, name);
  if (path == NULL) return false;
  text_buffer buffer = {NULL, 0, 0};
  bool ok = add_seat_json(&buffer, settings) && text_add(&buffer, "\n") &&
            qa_write_atomic_text(path, buffer.data);
  free(buffer.data);
  free(path);
  return ok;
}

bool qa_config_store_load_seat(const qa_config_store *store, const char *name,
                               qa_seat_settings *out) {
  size_t length = 0;
  char *text = qa_config_store_load_text(store, name, &length);
  if (text == NULL) return false;
  return config_decoded(store, name, text, length, parse_seat_job, out,
                        sizeof *out, free_seat_job);
}

bool qa_config_store_load_global_tuning(const qa_config_store *store,
                                        const char *name,
                                        qa_global_input_tuning *out) {
  size_t length = 0;
  char *text = qa_config_store_load_text(store, name, &length);
  if (text == NULL) return false;
  return config_decoded(store, name, text, length, parse_tuning_job, out,
                        sizeof *out, NULL);
}

bool qa_config_store_save_global_tuning(const qa_config_store *store,
                                        const char *name,
                                        const qa_global_input_tuning *tuning) {
  /* TS saveGlobalTuning re-validates via parseGlobalInputTuning. */
  if (tuning->version != 1) qa_error("Unsupported global input tuning document");
  const qa_mouse_tuning *mouse = &tuning->mouse;
  const double numbers[] = {mouse->sensitivity, mouse->acceleration, mouse->yaw,
                            mouse->pitch, mouse->side, mouse->forward};
  for (size_t i = 0; i < sizeof numbers / sizeof *numbers; i++)
    if (!isfinite(numbers[i])) qa_error("Expected a finite settings number");
  if (!isfinite(tuning->rumble_strength))
    qa_error("Expected a finite settings number");
  if (tuning->rumble_strength < 0 || tuning->rumble_strength > 1)
    qa_error("Vibration strength must be between zero and one");
  text_buffer buffer = {NULL, 0, 0};
  if (!add_global_json(&buffer, tuning) || !text_add(&buffer, "\n")) {
    free(buffer.data);
    return false;
  }
  bool ok = qa_config_store_dump(store, name, buffer.data);
  free(buffer.data);
  return ok;
}

bool qa_config_store_save_cvars(const qa_config_store *store, const char *name,
                                qa_cvar_registry *cvars,
                                const char *const *bindings,
                                size_t binding_count) {
  char *path = qa_settings_path(store->root, name);
  if (path == NULL) return false;
  size_t command_count = 0;
  char **commands =
      qa_cvar_registry_archive_commands(cvars, NULL, NULL, &command_count);
  text_buffer buffer = {NULL, 0, 0};
  bool ok = text_add(&buffer, "// Generated by quake-typescript\n");
  bool first = true;
  for (size_t i = 0; ok && i < binding_count; i++) {
    if (!first) ok = text_add(&buffer, "\n");
    if (ok) ok = text_add(&buffer, bindings[i]);
    first = false;
  }
  for (size_t i = 0; ok && i < command_count; i++) {
    if (!first) ok = text_add(&buffer, "\n");
    if (ok) ok = text_add(&buffer, commands[i]);
    first = false;
  }
  if (ok) ok = text_add(&buffer, "\n");
  if (ok) ok = qa_write_atomic_text(path, buffer.data);
  free(buffer.data);
  for (size_t i = 0; i < command_count; i++) free(commands[i]);
  free(commands);
  free(path);
  return ok;
}

void qa_config_store_execute(const qa_config_store *store, const char *name,
                             qa_command_buffer *commands,
                             qa_command_context context) {
  char *path = qa_settings_path(store->root, name);
  if (path == NULL) qa_error("Settings script path could not be allocated");
  size_t length = 0;
  char *text = read_file_bytes(path, &length);
  free(path);
  if (text == NULL) qa_error("Settings script could not be read: %s", name);
  text_buffer buffer = {NULL, 0, 0};
  if (!text_add(&buffer, text) || !text_add(&buffer, "\n")) {
    free(text);
    free(buffer.data);
    qa_error("Settings allocation failed");
  }
  free(text);
  qa_command_origin origin;
  memset(&origin, 0, sizeof origin);
  origin.kind = QA_COMMAND_ORIGIN_SCRIPT;
  origin.name = name;
  origin.caller = &context.origin;
  qa_command_context source;
  memset(&source, 0, sizeof source);
  source.session = context.session;
  source.origin = origin;
  qa_command_buffer_append(commands, buffer.data, &source, -1);
  free(buffer.data);
}
