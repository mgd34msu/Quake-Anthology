/* Port of src/ui/hud/q1-wheel.ts. GPL-2.0-or-later. */
#include "qa/ui/hud/q1_wheel.h"
#include "qa/network/common/js_number.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/common_parse.h"
#include "qa/foundation/js.h"

static char *dup_str(const char *text) {
  size_t length = strlen(text);
  char *out = malloc(length + 1);
  if (out != NULL) memcpy(out, text, length + 1);
  return out;
}
/* JS Number(string): whole-string parse, NaN on junk. */
static double js_number_of(const char *text) {
  return text == NULL ? NAN : qa_js_string_to_number(text); /* Number(undefined) is NaN */
}
static bool js_is_integer(double value) {
  return isfinite(value) && value == trunc(value);
}
/* JS String(number) for error messages and ids. */
static void js_string(char *buf, size_t size, double value) {
  qa_net_js_number(value, buf, size);
}

typedef struct field {
  char *key;
  char **values;
  size_t value_count;
  bool taken;
} field;

static void free_fields(field *fields, size_t count) {
  for (size_t i = 0; i < count; i++) {
    free(fields[i].key);
    for (size_t j = 0; j < fields[i].value_count; j++)
      free(fields[i].values[j]);
    free(fields[i].values);
  }
  free(fields);
}

static bool push_error(qa_ui_q1_wheel_parse *out, char *owned,
                       size_t *capacity) {
  if (owned == NULL) return false;
  if (out->error_count == *capacity) {
    size_t cap = *capacity == 0 ? 4 : *capacity * 2;
    char **grown = realloc(out->errors, cap * sizeof *grown);
    if (grown == NULL) { free(owned); return false; }
    out->errors = grown;
    *capacity = cap;
  }
  out->errors[out->error_count++] = owned;
  return true;
}
static bool errorf(qa_ui_q1_wheel_parse *out, size_t *capacity,
                   const char *format, const char *a, const char *b) {
  int need = snprintf(NULL, 0, format, a, b);
  if (need < 0) return false;
  char *buf = malloc((size_t)need + 1);
  if (buf == NULL) return false;
  snprintf(buf, (size_t)need + 1, format, a, b);
  return push_error(out, buf, capacity);
}

/* TS string(key): removes and validates a one-value field. */
static bool take_string(field *fields, size_t count, const char *key,
                        double slot, qa_ui_q1_wheel_parse *out,
                        size_t *error_cap, char **result) {
  *result = NULL;
  for (size_t i = 0; i < count; i++) {
    if (fields[i].taken || strcmp(fields[i].key, key) != 0) continue;
    fields[i].taken = true;
    if (fields[i].value_count != 1) {
      char slotbuf[64];
      js_string(slotbuf, sizeof slotbuf, slot);
      char fmt[128];
      snprintf(fmt, sizeof fmt, "slot %%s: %s expects one value", key);
      return errorf(out, error_cap, fmt, slotbuf, "");
    }
    *result = dup_str(fields[i].values[0]);
    return *result != NULL;
  }
  return true;
}
static bool take_integer(field *fields, size_t count, const char *key,
                         double slot, qa_ui_q1_wheel_parse *out,
                         size_t *error_cap, bool *has, double *result) {
  *has = false;
  *result = 0;
  char *value = NULL;
  if (!take_string(fields, count, key, slot, out, error_cap, &value))
    return false;
  if (value == NULL) return true;
  double numeric = js_number_of(value);
  free(value);
  if (!js_is_integer(numeric)) {
    char slotbuf[64];
    js_string(slotbuf, sizeof slotbuf, slot);
    char fmt[128];
    snprintf(fmt, sizeof fmt, "slot %%s: %s expects an integer", key);
    return errorf(out, error_cap, fmt, slotbuf, "");
  }
  *has = true;
  *result = numeric;
  return true;
}

void qa_ui_q1_wheel_parse_free(qa_ui_q1_wheel_parse *result) {
  if (result == NULL) return;
  for (size_t i = 0; i < result->slot_count; i++) {
    qa_ui_q1_wheel_slot *slot = &result->slots[i];
    free(slot->icon);
    free(slot->selected_icon);
    free(slot->ammo_icon);
    for (size_t j = 0; j < slot->unknown_count; j++) {
      free(slot->unknown[j].key);
      for (size_t k = 0; k < slot->unknown[j].value_count; k++)
        free(slot->unknown[j].values[k]);
      free(slot->unknown[j].values);
    }
    free(slot->unknown);
  }
  free(result->slots);
  for (size_t i = 0; i < result->error_count; i++) free(result->errors[i]);
  free(result->errors);
  memset(result, 0, sizeof *result);
}

bool qa_ui_parse_q1_weapon_wheel(const char *text,
                                 qa_ui_q1_wheel_parse *out) {
  memset(out, 0, sizeof *out);
  size_t slot_cap = 0, error_cap = 0;
  qa_tokenizer parser;
  memset(&parser, 0, sizeof parser);
  if (!qa_tokenizer_init(&parser, text, strlen(text), "wwheel.txt"))
    return false;
  bool ok = true;
  qa_text_token header;
  while (ok && qa_tokenizer_next(&parser, true, &header)) {
    qa_text_token ordinal, opening;
    bool have_ordinal = qa_tokenizer_next(&parser, true, &ordinal);
    if (!have_ordinal) memset(&ordinal, 0, sizeof ordinal);
    bool have_opening = have_ordinal
        && qa_tokenizer_next(&parser, true, &opening);
    if (!have_opening) memset(&opening, 0, sizeof opening);
    double slot = have_ordinal ? js_number_of(ordinal.value) : NAN;
    if (strcmp(header.value, "slot") != 0 || !have_ordinal
        || !js_is_integer(slot) || !have_opening
        || strcmp(opening.value, "{") != 0) {
      char linebuf[32];
      snprintf(linebuf, sizeof linebuf, "%lld", (long long)header.line);
      ok = errorf(out, &error_cap, "line %s: expected slot N {%s",
                  linebuf, "");
      free(header.value);
      free(ordinal.value);
      free(opening.value);
      break;
    }
    long long header_line = (long long)header.line;
    free(header.value);
    free(ordinal.value);
    free(opening.value);
    field *fields = NULL;
    size_t field_count = 0, field_cap = 0;
    bool closed = false;
    while (ok && !closed) {
      qa_text_token key;
      if (!qa_tokenizer_next(&parser, true, &key)) {
        char linebuf[32], slotbuf[64];
        snprintf(linebuf, sizeof linebuf, "%lld", header_line);
        js_string(slotbuf, sizeof slotbuf, slot);
        ok = errorf(out, &error_cap, "line %s: unclosed slot %s",
                    linebuf, slotbuf);
        break;
      }
      if (strcmp(key.value, "}") == 0) {
        free(key.value);
        closed = true;
        break;
      }
      char **values = NULL;
      size_t value_count = 0, value_cap = 0;
      qa_text_token value;
      while (ok && qa_tokenizer_next(&parser, false, &value)) {
        if (strcmp(value.value, "}") == 0) {
          free(value.value);
          closed = true;
          break;
        }
        if (value_count == value_cap) {
          size_t cap = value_cap == 0 ? 4 : value_cap * 2;
          char **grown = realloc(values, cap * sizeof *grown);
          if (grown == NULL) { free(value.value); ok = false; break; }
          values = grown;
          value_cap = cap;
        }
        values[value_count++] = value.value;
      }
      if (!ok) {
        free(key.value);
        for (size_t i = 0; i < value_count; i++) free(values[i]);
        free(values);
        break;
      }
      /* Map.set replaces an earlier entry with the same key. */
      size_t at = field_count;
      for (size_t i = 0; i < field_count; i++)
        if (strcmp(fields[i].key, key.value) == 0) { at = i; break; }
      if (at == field_count) {
        if (field_count == field_cap) {
          size_t cap = field_cap == 0 ? 4 : field_cap * 2;
          field *grown = realloc(fields, cap * sizeof *grown);
          if (grown == NULL) {
            free(key.value);
            for (size_t i = 0; i < value_count; i++) free(values[i]);
            free(values);
            ok = false;
            break;
          }
          fields = grown;
          field_cap = cap;
        }
        field_count++;
        fields[at].key = key.value;
        fields[at].values = NULL;
        fields[at].value_count = 0;
        fields[at].taken = false;
      } else {
        free(key.value);
        for (size_t i = 0; i < fields[at].value_count; i++)
          free(fields[at].values[i]);
        free(fields[at].values);
      }
      fields[at].values = values;
      fields[at].value_count = value_count;
    }
    if (!ok) {
      free_fields(fields, field_count);
      break;
    }
    qa_ui_q1_wheel_slot parsed;
    memset(&parsed, 0, sizeof parsed);
    parsed.slot = slot;
    ok = take_integer(fields, field_count, "impulse", slot, out, &error_cap,
                      &parsed.has_impulse, &parsed.impulse)
        && take_string(fields, field_count, "icon", slot, out, &error_cap,
                       &parsed.icon)
        && take_string(fields, field_count, "icon_sel", slot, out,
                       &error_cap, &parsed.selected_icon)
        && take_string(fields, field_count, "ammoicon", slot, out,
                       &error_cap, &parsed.ammo_icon)
        && take_integer(fields, field_count, "entvaroffs", slot, out,
                        &error_cap,
                        &parsed.has_entity_variable_byte_offset,
                        &parsed.entity_variable_byte_offset)
        && take_integer(fields, field_count, "weaponnum", slot, out,
                        &error_cap, &parsed.has_weapon_bits,
                        &parsed.weapon_bits);
    if (ok) {
      size_t remaining = 0;
      for (size_t i = 0; i < field_count; i++)
        if (!fields[i].taken) remaining++;
      if (remaining > 0) {
        parsed.unknown = calloc(remaining, sizeof *parsed.unknown);
        if (parsed.unknown == NULL) ok = false;
      }
      for (size_t i = 0; ok && i < field_count; i++) {
        if (fields[i].taken) continue;
        qa_ui_q1_wheel_unknown *entry =
            &parsed.unknown[parsed.unknown_count];
        entry->key = dup_str(fields[i].key);
        if (entry->key == NULL) { ok = false; break; }
        parsed.unknown_count++;
        if (fields[i].value_count > 0) {
          entry->values =
              calloc(fields[i].value_count, sizeof *entry->values);
          if (entry->values == NULL) { ok = false; break; }
        }
        for (size_t j = 0; j < fields[i].value_count; j++) {
          entry->values[j] = dup_str(fields[i].values[j]);
          entry->value_count++;
          if (entry->values[j] == NULL) { ok = false; break; }
        }
      }
    }
    free_fields(fields, field_count);
    if (!ok) {
      qa_ui_q1_wheel_slot *one = &parsed;
      free(one->icon);
      free(one->selected_icon);
      free(one->ammo_icon);
      for (size_t j = 0; j < one->unknown_count; j++) {
        free(one->unknown[j].key);
        for (size_t k = 0; k < one->unknown[j].value_count; k++)
          free(one->unknown[j].values[k]);
        free(one->unknown[j].values);
      }
      free(one->unknown);
      break;
    }
    if (out->slot_count == slot_cap) {
      size_t cap = slot_cap == 0 ? 4 : slot_cap * 2;
      qa_ui_q1_wheel_slot *grown = realloc(out->slots, cap * sizeof *grown);
      if (grown == NULL) {
        qa_ui_q1_wheel_slot *one = &parsed;
        free(one->icon);
        free(one->selected_icon);
        free(one->ammo_icon);
        for (size_t j = 0; j < one->unknown_count; j++) {
          free(one->unknown[j].key);
          for (size_t k = 0; k < one->unknown[j].value_count; k++)
            free(one->unknown[j].values[k]);
          free(one->unknown[j].values);
        }
        free(one->unknown);
        ok = false;
        break;
      }
      out->slots = grown;
      slot_cap = cap;
    }
    out->slots[out->slot_count++] = parsed;
    if (!closed) break;
  }
  qa_tokenizer_destroy(&parser);
  if (!ok) {
    qa_ui_q1_wheel_parse_free(out);
    return false;
  }
  return true;
}

void qa_ui_q1_wheel_items_free(qa_ui_q1_wheel_items *result) {
  if (result == NULL) return;
  for (size_t i = 0; i < result->count; i++) free(result->owned_ids[i]);
  free(result->owned_ids);
  free(result->items);
  memset(result, 0, sizeof *result);
}

bool qa_ui_q1_wheel_items_for(const qa_ui_q1_wheel_slot *slots,
                              size_t slot_count,
                              const qa_ui_q1_wheel_state *state,
                              qa_ui_q1_wheel_items *out) {
  memset(out, 0, sizeof *out);
  if (slot_count == 0) return true;
  out->items = calloc(slot_count, sizeof *out->items);
  out->owned_ids = calloc(slot_count, sizeof *out->owned_ids);
  if (out->items == NULL || out->owned_ids == NULL) {
    qa_ui_q1_wheel_items_free(out);
    return false;
  }
  /* Count first so _free can clean a partial build. */
  out->count = slot_count;
  for (size_t i = 0; i < slot_count; i++) {
    const qa_ui_q1_wheel_slot *slot = &slots[i];
    qa_ui_wheel_item *item = &out->items[i];
    char slotbuf[64];
    js_string(slotbuf, sizeof slotbuf, slot->slot);
    size_t need = strlen("q1-wheel:") + strlen(slotbuf) + 1;
    char *id = malloc(need);
    if (id == NULL) {
      qa_ui_q1_wheel_items_free(out);
      return false;
    }
    snprintf(id, need, "q1-wheel:%s", slotbuf);
    out->owned_ids[i] = id;
    bool has_count = slot->has_entity_variable_byte_offset;
    double count = has_count
        ? state->entity_float(state->ctx, slot->entity_variable_byte_offset)
        : 0;
    memset(item, 0, sizeof *item);
    item->id = id;
    item->source_ordinal = slot->slot;
    item->sort_order = slot->slot;
    item->label = state->label(state->ctx, slot);
    /* TS (state.items & slot.weaponBits) !== 0: JS bitwise is int32. */
    item->owned = slot->has_weapon_bits
        && ((qa_js_to_int32(state->items) & qa_js_to_int32(slot->weapon_bits)) != 0);
    item->has_ammo = !has_count || count > 0;
    item->has_count = has_count;
    item->count = count;
    item->warning_count = 0;
    item->icon = slot->icon == NULL ? NULL
                                    : state->image(state->ctx, slot->icon);
    item->selected_icon = slot->selected_icon == NULL ? NULL
        : state->image(state->ctx, slot->selected_icon);
  }
  return true;
}
