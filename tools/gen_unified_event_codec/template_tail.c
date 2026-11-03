qa_save_value *qa_app_unified_write_simulation_event(const qa_save_value *event) {
  return write_event_simulation_event(event);
}
qa_save_value *qa_app_unified_read_simulation_event(const qa_save_value *value,
    const qa_app_unified_identity_decoder *identity) {
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, value, "unified.simulation-event")) qa_range_error("out of memory");
  ev_scope scope;
  ev_scope_open(&scope);
  qa_save_value *out = read_event_simulation_event(&reader, identity);
  qa_save_reader_destroy(&reader);
  return ev_scope_close(&scope, out);
}
qa_save_value *qa_app_unified_read_presentation_event(const qa_save_value *value,
    const qa_app_unified_identity_decoder *identity) {
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, value, "unified.presentation-events")) qa_range_error("out of memory");
  ev_scope scope;
  ev_scope_open(&scope);
  qa_save_value *out = read_event_simulation_presentation_event(&reader, identity);
  qa_save_reader_destroy(&reader);
  return ev_scope_close(&scope, out);
}
qa_save_value *qa_app_unified_write_presentation_events(
    qa_save_value *const *events, size_t count) {
  qa_save_value *list = ev_array();
  for (size_t i = 0; i < count; i++) ev_push(list, write_event_simulation_presentation_event(events[i]));
  return list;
}

/* JSON.stringify ----------------------------------------------------------------- */
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
/* Number::toString: shortest digits that round-trip, then the ECMAScript layout. */
static void json_number(buffer *b, double value) {
  if (value == 0) { buffer_adds(b, "0"); return; }
  char scientific[40];
  int precision = 0;
  for (; precision < 17; precision++) {
    snprintf(scientific, sizeof scientific, "%.*e", precision, value);
    if (strtod(scientific, NULL) == value) break;
  }
  const char *cursor = scientific;
  if (*cursor == '-') { buffer_adds(b, "-"); cursor++; }
  char digits[24];
  int k = 0;
  for (; *cursor && *cursor != 'e'; cursor++)
    if (*cursor != '.') digits[k++] = *cursor;
  int n = atoi(cursor + 1) + 1; /* value = 0.digits x 10^n */
  while (k > 1 && digits[k - 1] == '0') k--;
  digits[k] = '\0';
  char text[64];
  if (k <= n && n <= 21) {
    buffer_add(b, digits, (size_t)k);
    for (int i = k; i < n; i++) buffer_adds(b, "0");
  } else if (0 < n && n <= 21) {
    buffer_add(b, digits, (size_t)n);
    buffer_adds(b, ".");
    buffer_adds(b, digits + n);
  } else if (-6 < n && n <= 0) {
    buffer_adds(b, "0.");
    for (int i = n; i < 0; i++) buffer_adds(b, "0");
    buffer_adds(b, digits);
  } else {
    buffer_add(b, digits, 1);
    if (k > 1) { buffer_adds(b, "."); buffer_adds(b, digits + 1); }
    snprintf(text, sizeof text, "e%c%d", n - 1 < 0 ? '-' : '+', abs(n - 1));
    buffer_adds(b, text);
  }
}
static void json_write(buffer *b, const qa_save_value *value) {
  switch (value->kind) {
    case QA_SAVE_UNDEFINED: break; /* omitted by the object writer below */
    case QA_SAVE_NULL: buffer_adds(b, "null"); break;
    case QA_SAVE_BOOLEAN: buffer_adds(b, value->boolean ? "true" : "false"); break;
    case QA_SAVE_NUMBER:
      if (!isfinite(value->number)) buffer_adds(b, "null");
      else json_number(b, value->number);
      break;
    case QA_SAVE_BIGINT:
      qa_range_error("TypeError: JSON.stringify cannot serialize BigInt.");
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
/* The decoder accepts only finite numbers, so an event whose written form holds NaN or an
 * infinity is dropped here and reported through `dropped` (NULL = TS undefined). */
uint8_t *qa_app_unified_encode_presentation_events_reporting(
    qa_save_value *const *events, size_t count,
    void (*dropped)(void *ctx, size_t count), void *dropped_ctx, size_t *out_length) {
  if (count > 65536) qa_range_error("Too many unified presentation events");
  qa_save_value *list = qa_app_unified_write_presentation_events(events, count);
  size_t kept = 0;
  for (size_t i = 0; i < list->item_count; i++) {
    if (qa_app_unified_has_non_finite(list->items[i], 0)) qa_save_value_free(list->items[i]);
    else list->items[kept++] = list->items[i];
  }
  list->item_count = kept;
  if (kept != count && dropped) dropped(dropped_ctx, count - kept);
  buffer b = {0};
  json_write(&b, list);
  qa_save_value_free(list);
  if (b.length > MAX_EVENT_BYTES) { free(b.data); qa_range_error("Unified presentation events exceed byte limit"); }
  *out_length = b.length;
  return (uint8_t *)b.data;
}
uint8_t *qa_app_unified_encode_presentation_events(
    qa_save_value *const *events, size_t count, size_t *out_length) {
  return qa_app_unified_encode_presentation_events_reporting(events, count, NULL, NULL, out_length);
}

/* new TextDecoder('utf-8', {fatal: true}): WHATWG UTF-8 validation. */
static bool utf8_valid(const uint8_t *s, size_t length) {
  for (size_t i = 0; i < length;) {
    uint8_t c = s[i];
    size_t extra;
    uint8_t lower = 0x80, upper = 0xBF;
    if (c < 0x80) { i++; continue; }
    if (c >= 0xC2 && c <= 0xDF) extra = 1;
    else if (c >= 0xE0 && c <= 0xEF) {
      extra = 2;
      if (c == 0xE0) lower = 0xA0;
      if (c == 0xED) upper = 0x9F;
    } else if (c >= 0xF0 && c <= 0xF4) {
      extra = 3;
      if (c == 0xF0) lower = 0x90;
      if (c == 0xF4) upper = 0x8F;
    } else return false;
    if (length - i <= extra) return false;
    for (size_t k = 1; k <= extra; k++) {
      uint8_t next = s[i + k];
      if (next < lower || next > upper) return false;
      lower = 0x80;
      upper = 0xBF;
    }
    i += extra + 1;
  }
  return true;
}
/* JSON.parse: the last duplicate key wins and keeps the first position. */
static void json_dedupe_keys(qa_save_value *value) {
  if (value->kind == QA_SAVE_ARRAY) {
    for (size_t i = 0; i < value->item_count; i++) json_dedupe_keys(value->items[i]);
    return;
  }
  if (value->kind != QA_SAVE_OBJECT) return;
  size_t kept = 0;
  for (size_t i = 0; i < value->entry_count; i++) {
    qa_save_entry entry = value->entries[i];
    bool replaced = false;
    for (size_t k = 0; k < kept; k++) {
      if (strcmp(value->entries[k].key, entry.key) == 0) {
        qa_save_value_free(value->entries[k].value);
        value->entries[k].value = entry.value;
        free(entry.key);
        replaced = true;
        break;
      }
    }
    if (!replaced) value->entries[kept++] = entry;
  }
  value->entry_count = kept;
  for (size_t i = 0; i < kept; i++) json_dedupe_keys(value->entries[i].value);
}
typedef struct decoded_events { qa_save_value *parsed; qa_save_value **events; size_t count; } decoded_events;
static void decoded_events_free(void *arg) {
  decoded_events *owned = arg;
  for (size_t i = 0; i < owned->count; i++) qa_save_value_free(owned->events[i]);
  free(owned->events);
  qa_save_value_free(owned->parsed);
}
/* A raise mid-list frees the parsed tree and every event already read; the event being read
 * when the raise came is the reader's to release. */
qa_save_value **qa_app_unified_decode_presentation_events(
    const uint8_t *bytes, size_t length,
    const qa_app_unified_identity_decoder *identity, size_t *out_count) {
  if (length > MAX_EVENT_BYTES) qa_range_error("Unified presentation events exceed byte limit");
  if (!utf8_valid(bytes, length)) qa_raise("TypeError", 0, 0, "The encoded data was not valid for encoding utf-8");
  /* TextDecoder drops a leading byte order mark. */
  if (length >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF) { bytes += 3; length -= 3; }
  decoded_events owned = {NULL, NULL, 0};
  qa_cleanup guard;
  qa_cleanup_push(&guard, decoded_events_free, &owned);
  owned.parsed = qa_parse_json_value(bytes, length, false);
  if (!owned.parsed) qa_range_error("out of memory");
  json_dedupe_keys(owned.parsed);
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, owned.parsed, "unified.presentation-events")) qa_range_error("out of memory");
  size_t count = ev_bounded_count(&reader);
  owned.events = malloc((count ? count : 1) * sizeof *owned.events);
  if (!owned.events) qa_range_error("out of memory");
  for (size_t i = 0; i < count; i++) {
    qa_save_reader item;
    ev_item(&reader, i, &item);
    ev_scope scope;
    ev_scope_open(&scope);
    owned.events[i] = ev_scope_close(&scope, read_event_simulation_presentation_event(&item, identity));
    owned.count = i + 1;
    qa_save_reader_destroy(&item);
  }
  qa_save_reader_destroy(&reader);
  qa_cleanup_pop(&guard);
  qa_save_value_free(owned.parsed);
  *out_count = count;
  return owned.events;
}
