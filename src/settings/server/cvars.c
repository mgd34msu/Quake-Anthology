/* Port of src/settings/server/cvars.ts. GPL-2.0-or-later. */
#include "qa/settings/server/cvars.h"

#include "qa/foundation/support.h"
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/js.h"

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

/* TS Number(string): JS whitespace trim (ASCII subset; cvar values
 * are engine text), then empty is 0, 0x/0b/0o integers, Infinity,
 * else a full decimal parse — anything else is NaN. */
/* TS Number(text). */
static double js_number(const char *text) { return qa_js_string_to_number(text); }

/* JS ToInt32 for the bitwise operators in projected/merged. */
static int32_t js_int32(double value) {
  if (!isfinite(value) || value == 0.0) return 0;
  double truncated = trunc(fabs(value));
  double modulo = fmod(truncated, 4294967296.0);
  int64_t bits = (int64_t)modulo;
  if (value < 0) bits = -bits;
  if (bits >= 2147483648LL) bits -= 4294967296LL;
  if (bits < -2147483648LL) bits += 4294967296LL;
  return (int32_t)bits;
}

static char *projected(qa_server_setting_target target, const char *value) {
  if (!target.is_bit) return dup_text(value);
  bool enabled = (js_int32(js_number(value)) & target.mask) != 0;
  return dup_text(enabled != target.inverted ? "1" : "0");
}

static char *merged(qa_server_setting_target target, const char *current,
                    const char *value) {
  if (!target.is_bit) return dup_text(value);
  bool enabled = (strcmp(value, "1") == 0) != target.inverted;
  int32_t base = js_int32(js_number(current));
  uint32_t merged_value = (uint32_t)(enabled ? base | target.mask : base & ~target.mask);
  char text[16];
  snprintf(text, sizeof text, "%u", (unsigned)merged_value);
  return dup_text(text);
}

static void require_value(qa_cvar_registry *cvars, const char *name,
                          qa_cvar_read *out) {
  if (!qa_cvar_registry_find(cvars, name, out))
    qa_error("Server setting has no cvar owner: %s", name);
}

static qa_server_setting_values owner_read(void *ctx,
                                           qa_server_setting_target target) {
  qa_cvar_server_owner *storage = (qa_cvar_server_owner *)ctx;
  qa_cvar_read value;
  require_value(storage->cvars, target.name, &value);
  qa_server_setting_values values;
  values.effective = projected(target, value.value);
  values.desired =
      projected(target, value.latched_value != NULL ? value.latched_value : value.value);
  qa_cvar_read_free(&value);
  return values;
}

static void registry_set(qa_cvar_registry *cvars, const char *name,
                         const char *value, bool force) {
  qa_cvar_snapshot snapshot;
  (void)qa_cvar_registry_set(cvars, name, value, force, &snapshot);
  qa_cvar_read_free(&snapshot);
}
static void registry_stage(qa_cvar_registry *cvars, const char *name,
                           const char *value) {
  qa_cvar_snapshot snapshot;
  (void)qa_cvar_registry_stage(cvars, name, value, &snapshot);
  qa_cvar_read_free(&snapshot);
}

static void owner_write(void *ctx, qa_server_setting_target target,
                        const char *value, qa_server_apply_at apply_at) {
  qa_cvar_server_owner *storage = (qa_cvar_server_owner *)ctx;
  qa_cvar_registry *cvars = storage->cvars;
  qa_cvar_read current;
  require_value(cvars, target.name, &current);
  char *desired = merged(target,
                         current.latched_value != NULL ? current.latched_value
                                                         : current.value,
                         value);
  char *current_value = dup_text(current.value);
  qa_cvar_read_free(&current);
  if (desired == NULL || current_value == NULL) {
    free(desired);
    free(current_value);
    qa_error("Settings allocation failed");
  }
  if (storage->initializing) {
    registry_set(cvars, target.name, desired, true);
    free(desired);
    free(current_value);
    return;
  }
  if (apply_at != QA_SERVER_APPLY_LIVE) {
    registry_stage(cvars, target.name, desired);
    free(desired);
    free(current_value);
    return;
  }
  char *effective = merged(target, current_value, value);
  free(current_value);
  if (effective == NULL) {
    free(desired);
    qa_error("Settings allocation failed");
  }
  registry_set(cvars, target.name, effective, true);
  if (strcmp(desired, effective) != 0) registry_stage(cvars, target.name, desired);
  free(desired);
  free(effective);
}

void qa_cvar_server_settings_owner_init(qa_cvar_server_owner *storage,
                                        qa_cvar_registry *cvars,
                                        bool initializing) {
  storage->cvars = cvars;
  storage->initializing = initializing;
  storage->owner.ctx = storage;
  storage->owner.read = owner_read;
  storage->owner.write = owner_write;
}
