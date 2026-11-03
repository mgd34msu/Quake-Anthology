/* Port of src/settings/server/profile.ts. GPL-2.0-or-later. */
#include "qa/settings/server/profile.h"

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/foundation/js.h"
#include "qa/network/common/js_number.h"

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

/* TS Number(string) / String(number) — see server/cvars.c for the
 * same Number helper (kept per-file, as elsewhere in the port). */
/* TS Number(text). */
static double js_number(const char *text) { return qa_js_string_to_number(text); }
static void js_number_text(double value, char *out, size_t size) {
  qa_net_js_number(value, out, size); /* String(value): NaN and Infinity included */
}
/* TS String(number): see js_number_text. */
static char *js_string(double value) {
  char text[64];
  js_number_text(value, text, sizeof text);
  return dup_text(text);
}
static bool trimmed_empty(const char *text) {
  for (; *text != '\0'; text++)
    if (!isspace((unsigned char)*text)) return false;
  return true;
}
static bool is_safe_integer(double value) {
  return value == trunc(value) && fabs(value) <= 9007199254740991.0;
}

void qa_server_profile_free(qa_server_profile *profile) {
  for (size_t i = 0; i < profile->override_count; i++) {
    free(profile->overrides[i].id);
    free(profile->overrides[i].value);
  }
  free(profile->overrides);
  memset(profile, 0, sizeof *profile);
}

/* Non-fatal parseServerSetting: false is a TS RangeError, whose
 * message is written into `message` for the fatal wrapper. */
static bool try_parse_server_setting(const qa_server_setting_definition *definition,
                                     const char *input, char **out,
                                     char *message, size_t message_size) {
  message[0] = '\0';
  switch (definition->kind) {
    case QA_SERVER_SETTING_TOGGLE:
      if (strcmp(input, "1") == 0 || strcmp(input, "true") == 0) {
        *out = dup_text("1");
        return *out != NULL;
      }
      if (strcmp(input, "0") == 0 || strcmp(input, "false") == 0) {
        *out = dup_text("0");
        return *out != NULL;
      }
      snprintf(message, message_size, "%s requires a boolean", definition->label);
      return false;
    case QA_SERVER_SETTING_SLIDER: {
      double value = js_number(input);
      if (trimmed_empty(input) || !isfinite(value) ||
          (definition->integer && !is_safe_integer(value)) ||
          value < definition->minimum || value > definition->maximum) {
        snprintf(message, message_size, "%s is outside its allowed range",
                 definition->label);
        return false;
      }
      *out = js_string(value);
      return *out != NULL;
    }
    case QA_SERVER_SETTING_CHOICE:
      for (size_t i = 0; i < definition->choice_count; i++) {
        if (strcmp(definition->choices[i].id, input) == 0) {
          *out = dup_text(input);
          return *out != NULL;
        }
      }
      snprintf(message, message_size, "Unknown %s choice", definition->label);
      return false;
    case QA_SERVER_SETTING_TEXT_ENTRY:
      /* Length is a UTF-16 unit count in TS, a byte count here. */
      if ((double)strlen(input) > definition->maximum_length ||
          strchr(input, '\r') != NULL || strchr(input, '\n') != NULL) {
        snprintf(message, message_size, "%s contains invalid text", definition->label);
        return false;
      }
      *out = dup_text(input);
      return *out != NULL;
  }
  return false;
}

char *qa_parse_server_setting(const qa_server_setting_definition *definition,
                              const char *input) {
  char *out = NULL;
  char message[512];
  if (!try_parse_server_setting(definition, input, &out, message, sizeof message)) {
    if (out == NULL && message[0] == '\0') return NULL;
    qa_range_error("%s", message);
  }
  return out;
}

const qa_server_setting_definition **qa_collect_server_settings(
    const qa_server_setting_collection *collections, size_t collection_count,
    size_t *out_count) {
  size_t total = 0;
  for (size_t i = 0; i < collection_count; i++) total += collections[i].definition_count;
  const qa_server_setting_definition **definitions =
      (const qa_server_setting_definition **)calloc(total == 0 ? 1 : total,
                                                    sizeof *definitions);
  if (definitions == NULL) {
    *out_count = 0;
    return NULL;
  }
  size_t count = 0;
  for (size_t i = 0; i < collection_count; i++) {
    for (size_t j = 0; j < collections[i].definition_count; j++) {
      const qa_server_setting_definition *definition =
          &collections[i].definitions[j];
      for (size_t k = 0; k < count; k++)
        if (strcmp(definitions[k]->id, definition->id) == 0)
          qa_error("Two selected components own %s", definition->id);
      free(qa_parse_server_setting(definition, definition->default_value));
      definitions[count++] = definition;
    }
  }
  *out_count = count;
  return definitions;
}

qa_server_setting_status qa_read_server_setting(
    const qa_bound_server_setting *binding) {
  qa_server_setting_values values =
      binding->owner.read(binding->owner.ctx, binding->definition->target);
  qa_server_setting_status status;
  status.desired = values.desired;
  status.effective = values.effective;
  status.pending = strcmp(values.desired, values.effective) != 0;
  status.apply_at = binding->definition->apply_at;
  return status;
}

qa_server_setting_status qa_write_server_setting(
    const qa_bound_server_setting *binding, const char *value) {
  char *parsed = qa_parse_server_setting(binding->definition, value);
  if (parsed == NULL) qa_error("Settings allocation failed");
  binding->owner.write(binding->owner.ctx, binding->definition->target, parsed,
                       binding->definition->apply_at);
  free(parsed);
  return qa_read_server_setting(binding);
}

static bool profile_record(const qa_save_value *value) {
  return value != NULL && value->kind == QA_SAVE_OBJECT;
}

static const qa_server_setting_definition *find_definition(
    const qa_server_setting_definition *const *definitions, size_t count,
    const char *id) {
  for (size_t i = 0; i < count; i++)
    if (strcmp(definitions[i]->id, id) == 0) return definitions[i];
  return NULL;
}

void qa_parse_server_profile(const qa_save_value *value,
                             const qa_server_setting_definition *const *definitions,
                             size_t definition_count, qa_server_profile *out) {
  if (!profile_record(value)) qa_error("Expected a server profile object");
  const qa_save_value *overrides = qa_save_value_get(value, "overrides");
  const qa_save_value *version = qa_save_value_get(value, "version");
  if (version == NULL || version->kind != QA_SAVE_NUMBER || version->number != 1 ||
      overrides == NULL || overrides->kind != QA_SAVE_ARRAY)
    qa_error("Unsupported server profile");
  memset(out, 0, sizeof *out);
  out->version = 1;
  out->override_count = overrides->item_count;
  out->overrides = (qa_server_profile_override *)xalloc(calloc(
      out->override_count == 0 ? 1 : out->override_count, sizeof *out->overrides));
  for (size_t i = 0; i < out->override_count; i++) {
    const qa_save_value *saved = overrides->items[i];
    if (!profile_record(saved)) qa_error("Expected a server profile object");
    const qa_save_value *id_value = qa_save_value_get(saved, "id");
    const qa_save_value *entry_value = qa_save_value_get(saved, "value");
    const qa_server_setting_definition *definition = NULL;
    if (id_value != NULL && id_value->kind == QA_SAVE_STRING && id_value->text != NULL)
      definition = find_definition(definitions, definition_count, id_value->text);
    if (definition == NULL || entry_value == NULL ||
        entry_value->kind != QA_SAVE_STRING || entry_value->text == NULL)
      qa_error("Server profile setting has no selected owner");
    for (size_t j = 0; j < i; j++)
      if (strcmp(out->overrides[j].id, definition->id) == 0)
        qa_error("Duplicate server profile setting %s", definition->id);
    out->overrides[i].id = (char *)xalloc(dup_text(definition->id));
    out->overrides[i].value =
        (char *)xalloc(qa_parse_server_setting(definition, entry_value->text));
  }
}

void qa_capture_server_profile(const qa_bound_server_setting *bindings,
                               size_t binding_count, qa_server_profile *out) {
  /* Live capture is not user input: values outside the menu choices
   * are omitted instead of rejecting the whole profile. */
  memset(out, 0, sizeof *out);
  out->version = 1;
  out->overrides = (qa_server_profile_override *)xalloc(
      calloc(binding_count == 0 ? 1 : binding_count, sizeof *out->overrides));
  size_t count = 0;
  for (size_t i = 0; i < binding_count; i++) {
    qa_server_setting_values values = bindings[i].owner.read(
        bindings[i].owner.ctx, bindings[i].definition->target);
    char *parsed = NULL;
    char message[512] = "";
    bool ok = try_parse_server_setting(bindings[i].definition, values.desired,
                                       &parsed, message, sizeof message);
    if (ok) {
      out->overrides[count].id = (char *)xalloc(dup_text(bindings[i].definition->id));
      out->overrides[count].value = parsed;
      count++;
    } else {
      free(parsed);
    }
    qa_server_setting_values_free(&values);
  }
  out->override_count = count;
}

/* Validates an already-parsed profile exactly as parseServerProfile
 * validates its unknown input, returning normalized values. */
static void validate_profile(const qa_server_profile *profile,
                             const qa_server_setting_definition *const *definitions,
                             size_t definition_count, qa_server_profile *out) {
  if (profile->version != 1) qa_error("Unsupported server profile");
  memset(out, 0, sizeof *out);
  out->version = 1;
  out->override_count = profile->override_count;
  out->overrides = (qa_server_profile_override *)xalloc(calloc(
      out->override_count == 0 ? 1 : out->override_count, sizeof *out->overrides));
  for (size_t i = 0; i < out->override_count; i++) {
    const qa_server_setting_definition *definition = find_definition(
        definitions, definition_count, profile->overrides[i].id);
    if (definition == NULL)
      qa_error("Server profile setting has no selected owner");
    for (size_t j = 0; j < i; j++)
      if (strcmp(out->overrides[j].id, definition->id) == 0)
        qa_error("Duplicate server profile setting %s", definition->id);
    out->overrides[i].id = (char *)xalloc(dup_text(definition->id));
    out->overrides[i].value = (char *)xalloc(
        qa_parse_server_setting(definition, profile->overrides[i].value));
  }
}

void qa_apply_server_profile(const qa_server_profile *profile,
                             const qa_bound_server_setting *bindings,
                             size_t binding_count) {
  const qa_server_setting_definition **definitions =
      (const qa_server_setting_definition **)xalloc(calloc(
          binding_count == 0 ? 1 : binding_count, sizeof *definitions));
  for (size_t i = 0; i < binding_count; i++) definitions[i] = bindings[i].definition;
  qa_server_profile validated;
  validate_profile(profile, definitions, binding_count, &validated);
  free(definitions);
  /* Resolve the complete write list before any source mutation. */
  const qa_bound_server_setting **writes =
      (const qa_bound_server_setting **)xalloc(calloc(
          validated.override_count == 0 ? 1 : validated.override_count,
          sizeof *writes));
  for (size_t i = 0; i < validated.override_count; i++) {
    const qa_bound_server_setting *found = NULL;
    for (size_t j = 0; j < binding_count; j++)
      if (strcmp(bindings[j].definition->id, validated.overrides[i].id) == 0)
        found = &bindings[j];
    if (found == NULL) qa_error("No server owner for %s", validated.overrides[i].id);
    writes[i] = found;
  }
  for (size_t i = 0; i < validated.override_count; i++) {
    qa_server_setting_status status =
        qa_write_server_setting(writes[i], validated.overrides[i].value);
    qa_server_setting_status_free(&status);
  }
  free(writes);
  qa_server_profile_free(&validated);
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

bool qa_save_server_profile(const qa_config_store *store, const char *name,
                            const qa_server_profile *profile,
                            const qa_server_setting_definition *const *definitions,
                            size_t definition_count) {
  qa_server_profile validated;
  validate_profile(profile, definitions, definition_count, &validated);
  text_buffer buffer = {NULL, 0, 0};
  bool built = text_add(&buffer, "{\n  \"version\": 1,\n  \"overrides\": [");
  for (size_t i = 0; built && i < validated.override_count; i++) {
    if (i > 0) built = text_add(&buffer, ",");
    if (built) built = text_add(&buffer, "\n    {\n      \"id\": ");
    if (built) built = text_add_json_string(&buffer, validated.overrides[i].id);
    if (built) built = text_add(&buffer, ",\n      \"value\": ");
    if (built) built = text_add_json_string(&buffer, validated.overrides[i].value);
    if (built) built = text_add(&buffer, "\n    }");
  }
  if (built && validated.override_count > 0) built = text_add(&buffer, "\n  ");
  if (built) built = text_add(&buffer, "]\n}\n");
  bool ok = built && qa_config_store_dump(store, name, buffer.data);
  free(buffer.data);
  qa_server_profile_free(&validated);
  return ok;
}

bool qa_load_server_profile(const qa_config_store *store, const char *name,
                            const qa_server_setting_definition *const *definitions,
                            size_t definition_count, qa_server_profile *out) {
  size_t length = 0;
  char *text = qa_config_store_load_text(store, name, &length);
  if (text == NULL) return false;
  /* Text that is not JSON behaves like the absent profile. */
  qa_save_value *value = qa_parse_json_value((const uint8_t *)text, length, true);
  free(text);
  if (value == NULL) return false;
  qa_parse_server_profile(value, definitions, definition_count, out);
  qa_save_value_free(value);
  return true;
}
