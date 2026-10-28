/* Port of src/app/bootstrap/network/unified-event-codec.ts.
 * GPL-2.0-or-later. */
#include "qa/app/bootstrap/network/unified_event_codec.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/network/unified_frame_values.h"
#include "qa/foundation/support.h"
static void *xmalloc(size_t size) {
  void *p = malloc(size ? size : 1);
  if (!p) qa_range_error("out of memory");
  return p;
}
static qa_save_value *clone_value(const qa_save_value *value) {
  if (!value) return qa_save_value_undefined();
  qa_save_value *out = NULL;
  switch (value->kind) {
    case QA_SAVE_UNDEFINED: out = qa_save_value_undefined(); break;
    case QA_SAVE_NULL: out = qa_save_value_null(); break;
    case QA_SAVE_BOOLEAN: out = qa_save_value_boolean(value->boolean); break;
    case QA_SAVE_NUMBER: out = qa_save_value_number(value->number); break;
    case QA_SAVE_BIGINT: out = qa_save_value_bigint_text(value->text); break;
    case QA_SAVE_STRING: out = qa_save_value_string(value->text); break;
    case QA_SAVE_BYTES: out = qa_save_value_bytes(value->bytes, value->byte_length); break;
    case QA_SAVE_ARRAY:
      out = qa_save_value_array();
      if (out) for (size_t i = 0; i < value->item_count; i++)
        if (!qa_save_value_push(out, clone_value(value->items[i]))) { qa_save_value_free(out); out = NULL; break; }
      break;
    case QA_SAVE_OBJECT:
      out = qa_save_value_object();
      if (out) for (size_t i = 0; i < value->entry_count; i++)
        if (!qa_save_value_set(out, value->entries[i].key, clone_value(value->entries[i].value))) {
          qa_save_value_free(out); out = NULL; break;
        }
      break;
  }
  if (!out) qa_range_error("out of memory");
  return out;
}
static bool unified_resource_ok(const char *value) {
  const char *prefix = "resource:unified:";
  size_t prefix_length = strlen(prefix);
  if (strncmp(value, prefix, prefix_length) != 0) return false;
  const char *hex = value + prefix_length;
  if (strlen(hex) != 64) return false;
  for (size_t i = 0; i < 64; i++) {
    char c = hex[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  }
  return true;
}
static bool content_id_ok(const char *value) {
  /* createContentId: family:edition:package:revision with valid parts. */
  int parts = 0;
  const char *cursor = value;
  bool first = true;
  for (;;) {
    const char *colon = strchr(cursor, ':');
    size_t length = colon ? (size_t)(colon - cursor) : strlen(cursor);
    parts++;
    if (first) {
      if (!((length == 2 && (strncmp(cursor, "q1", 2) == 0 || strncmp(cursor, "q2", 2) == 0
              || strncmp(cursor, "q3", 2) == 0)))) return false;
      first = false;
    } else {
      if (length == 0) return false;
      char c = cursor[0];
      if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')))
        return false;
      for (size_t i = 0; i < length; i++) {
        char ch = cursor[i];
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')
            || ch == '.' || ch == '_' || ch == '+' || ch == '-')) return false;
      }
    }
    if (!colon) break;
    cursor = colon + 1;
  }
  return parts == 4;
}
typedef struct walk {
  bool writing;
  const qa_app_unified_identity_decoder *identity;
} walk;
static bool is_wire_id_object(const qa_save_value *value) {
  if (!value || value->kind != QA_SAVE_OBJECT) return false;
  const qa_save_value *slot = qa_save_value_get(value, "slot");
  const qa_save_value *generation = qa_save_value_get(value, "generation");
  return slot && slot->kind == QA_SAVE_NUMBER && generation && generation->kind == QA_SAVE_NUMBER;
}
static void check_wire_id(qa_save_value *value, const walk *w, int which) {
  qa_save_value *slot = (qa_save_value *)qa_save_value_get(value, "slot");
  qa_save_value *generation = (qa_save_value *)qa_save_value_get(value, "generation");
  double s = slot->number, g = generation->number;
  if (s != trunc(s) || s < 0 || fabs(s) > 9007199254740991.0
      || g != trunc(g) || g < 0 || fabs(g) > 9007199254740991.0)
    qa_range_error("expected a safe nonnegative integer");
  if (!w->writing && w->identity) {
    if (which == 0) {
      qa_actor_id actor = w->identity->actor(w->identity->ctx, s, g);
      slot->number = (double)actor.slot;
      generation->number = (double)actor.generation;
    } else if (which == 1) {
      qa_client_id client = w->identity->client(w->identity->ctx, s, g);
      slot->number = (double)client.slot;
      generation->number = (double)client.generation;
    }
  }
}
static void transform(qa_save_value *value, const walk *w) {
  if (!value) return;
  if (value->kind == QA_SAVE_ARRAY) {
    if (value->item_count > 65536)
      qa_range_error("event list exceeds unified bounds");
    for (size_t i = 0; i < value->item_count; i++) transform(value->items[i], w);
    return;
  }
  if (value->kind != QA_SAVE_OBJECT) return;
  for (size_t i = 0; i < value->entry_count; i++) {
    const char *key = value->entries[i].key;
    qa_save_value *child = value->entries[i].value;
    if (strcmp(key, "resource") == 0 && child->kind == QA_SAVE_STRING) {
      if (w->writing) {
        if (!unified_resource_ok(child->text))
          qa_range_error("Simulation event resources must use negotiated unified identities");
      } else {
        if (!unified_resource_ok(child->text))
          qa_range_error("expected negotiated unified resource identity");
        if (w->identity) {
          qa_resource_id rebound = w->identity->resource_id(w->identity->ctx, child->text);
          qa_save_value *replacement = qa_save_value_string(rebound);
          if (!replacement) qa_range_error("out of memory");
          qa_save_value_free(value->entries[i].value);
          value->entries[i].value = replacement;
          child = replacement;
        }
      }
    } else if ((strcmp(key, "actor") == 0 || strcmp(key, "sourceActor") == 0
        || strcmp(key, "target") == 0) && is_wire_id_object(child)) {
      check_wire_id(child, w, 0);
    } else if (strcmp(key, "client") == 0 && is_wire_id_object(child)) {
      check_wire_id(child, w, 1);
    } else if (strcmp(key, "seat") == 0 && child->kind == QA_SAVE_NUMBER) {
      if (!w->writing && w->identity)
        w->identity->seat(w->identity->ctx, child->number);
    } else if (strcmp(key, "content") == 0 && child->kind == QA_SAVE_STRING) {
      if (!content_id_ok(child->text))
        qa_range_error("invalid content identity");
    }
    transform(child, w);
  }
}
qa_save_value *qa_app_unified_write_simulation_event(const qa_save_value *event) {
  qa_save_value *out = clone_value(event);
  walk w = {true, NULL};
  transform(out, &w);
  return out;
}
qa_save_value *qa_app_unified_read_simulation_event(const qa_save_value *value,
    const qa_app_unified_identity_decoder *identity) {
  qa_save_value *out = clone_value(value);
  walk w = {false, identity};
  transform(out, &w);
  return out;
}
qa_save_value *qa_app_unified_read_presentation_event(const qa_save_value *value,
    const qa_app_unified_identity_decoder *identity) {
  qa_save_value *out = clone_value(value);
  /* Stamp resource ids reached through nested `event` payloads. */
  walk w = {false, identity};
  transform(out, &w);
  return out;
}
qa_save_value *qa_app_unified_write_presentation_events(
    qa_save_value *const *events, size_t count) {
  qa_save_value *list = qa_save_value_array();
  if (!list) qa_range_error("out of memory");
  for (size_t i = 0; i < count; i++) {
    qa_save_value *event = clone_value(events[i]);
    walk w = {true, NULL};
    transform(event, &w);
    if (!qa_save_value_push(list, event)) qa_range_error("out of memory");
  }
  return list;
}
typedef struct buffer { char *data; size_t length, capacity; } buffer;
static void buffer_add(buffer *b, const char *text, size_t n) {
  if (b->length + n + 1 > b->capacity) {
    b->capacity = (b->length + n + 1) * 2;
    b->data = realloc(b->data, b->capacity);
    if (!b->data) qa_range_error("out of memory");
  }
  memcpy(b->data + b->length, text, n);
  b->length += n;
  b->data[b->length] = '\0';
}
static void buffer_adds(buffer *b, const char *text) { buffer_add(b, text, strlen(text)); }
static void json_escape(buffer *b, const char *text) {
  buffer_adds(b, "\"");
  for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
    char escaped[8];
    switch (*p) {
      case '"': buffer_adds(b, "\\\""); break;
      case '\\': buffer_adds(b, "\\\\"); break;
      case '\b': buffer_adds(b, "\\b"); break;
      case '\f': buffer_adds(b, "\\f"); break;
      case '\n': buffer_adds(b, "\\n"); break;
      case '\r': buffer_adds(b, "\\r"); break;
      case '\t': buffer_adds(b, "\\t"); break;
      default:
        if (*p < 0x20) { snprintf(escaped, sizeof escaped, "\\u%04x", *p); buffer_adds(b, escaped); }
        else buffer_add(b, (const char *)p, 1);
        break;
    }
  }
  buffer_adds(b, "\"");
}
static void json_number(buffer *b, double value) {
  char text[40];
  if (value == 0 && signbit(value)) { buffer_adds(b, "0"); return; }
  if (value == trunc(value) && fabs(value) < 1e21) snprintf(text, sizeof text, "%.0f", value);
  else snprintf(text, sizeof text, "%.17g", value);
  buffer_adds(b, text);
}
static void json_write(buffer *b, const qa_save_value *value) {
  switch (value->kind) {
    case QA_SAVE_UNDEFINED: break; /* omitted by object writer below */
    case QA_SAVE_NULL: buffer_adds(b, "null"); break;
    case QA_SAVE_BOOLEAN: buffer_adds(b, value->boolean ? "true" : "false"); break;
    case QA_SAVE_NUMBER:
      if (!isfinite(value->number)) buffer_adds(b, "null");
      else json_number(b, value->number);
      break;
    case QA_SAVE_BIGINT:
      qa_range_error("Do not know how to serialize a BigInt");
      break;
    case QA_SAVE_STRING: json_escape(b, value->text); break;
    case QA_SAVE_BYTES: {
      /* JSON.stringify(Uint8Array) yields an index object. */
      buffer_adds(b, "{");
      for (size_t i = 0; i < value->byte_length; i++) {
        char text[32];
        if (i) buffer_adds(b, ",");
        snprintf(text, sizeof text, "\"%zu\":%u", i, value->bytes[i]);
        buffer_adds(b, text);
      }
      buffer_adds(b, "}");
      break;
    }
    case QA_SAVE_ARRAY:
      buffer_adds(b, "[");
      for (size_t i = 0; i < value->item_count; i++) {
        if (i) buffer_adds(b, ",");
        if (value->items[i]->kind == QA_SAVE_UNDEFINED) buffer_adds(b, "null");
        else json_write(b, value->items[i]);
      }
      buffer_adds(b, "]");
      break;
    case QA_SAVE_OBJECT: {
      buffer_adds(b, "{");
      bool first = true;
      for (size_t i = 0; i < value->entry_count; i++) {
        if (value->entries[i].value->kind == QA_SAVE_UNDEFINED) continue;
        if (!first) buffer_adds(b, ",");
        first = false;
        json_escape(b, value->entries[i].key);
        buffer_adds(b, ":");
        json_write(b, value->entries[i].value);
      }
      buffer_adds(b, "}");
      break;
    }
  }
}
uint8_t *qa_app_unified_encode_presentation_events(
    qa_save_value *const *events, size_t count, size_t *out_length) {
  qa_save_value *list = qa_app_unified_write_presentation_events(events, count);
  buffer b = {0};
  json_write(&b, list);
  qa_save_value_free(list);
  if (b.length > 2 * 1024 * 1024) {
    free(b.data);
    qa_range_error("Presentation event channel exceeds its limit");
  }
  *out_length = b.length;
  if (!b.data) {
    uint8_t *empty = xmalloc(1);
    *out_length = 0;
    return empty;
  }
  return (uint8_t *)b.data;
}
qa_save_value **qa_app_unified_decode_presentation_events(
    const uint8_t *bytes, size_t length,
    const qa_app_unified_identity_decoder *identity, size_t *out_count) {
  if (length > 2 * 1024 * 1024)
    qa_range_error("Presentation event channel exceeds its limit");
  /* Strict UTF-8 (fatal): reject overlongs/surrogates roughly. */
  qa_save_value *parsed = qa_parse_json_value(bytes, length, false);
  if (!parsed) qa_range_error("out of memory");
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, parsed, "unified-events")) qa_range_error("out of memory");
  size_t count = qa_save_reader_list_count(&reader);
  qa_save_value **events = xmalloc(count * sizeof *events);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader item;
    if (!qa_save_reader_item(&reader, i, &item)) qa_range_error("out of memory");
    events[i] = qa_app_unified_read_presentation_event(item.value, identity);
    qa_save_reader_destroy(&item);
  }
  qa_save_reader_destroy(&reader);
  qa_save_value_free(parsed);
  *out_count = count;
  return events;
}
