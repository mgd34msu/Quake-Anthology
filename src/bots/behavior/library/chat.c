/* See header. GPL-2.0-or-later. */
#include "qa/bots/behavior/library/chat.h"
#include "qa/foundation/support.h"
#include <ctype.h>
#include <math.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/js.h"

#define CONSOLE_MESSAGE_BYTES 276
#define CHAT_STATE_BYTES 316
#define CHAT_CACHE_BYTES 132
#define CHAT_MESSAGE_SIZE QA_CHAT_MESSAGE_SIZE
#define ESCAPE_CHAR '\x01'
#define STATE_NAME 8
#define STATE_MESSAGE 40
#define STATE_HANDLE 296
#define STATE_FIRST 300
#define STATE_LAST 304
#define STATE_COUNT 308
#define STATE_CHAT 312

struct qa_chat_state {
  qa_bot_memory_allocation allocation;
  int64_t revision;
};
struct qa_chat_cached {
  qa_bot_memory_allocation allocation;
};

/* ---- allocation and JS string helpers ---- */
static void *xcalloc(size_t count, size_t size) {
  void *p = calloc(count == 0 ? 1 : count, size == 0 ? 1 : size);
  if (p == NULL)
    qa_range_error("out of memory");
  return p;
}
static void *xrealloc(void *old, size_t size) {
  void *p = realloc(old, size == 0 ? 1 : size);
  if (p == NULL)
    qa_range_error("out of memory");
  return p;
}
static char *xdup_n(const char *text, size_t length) {
  char *copy = xcalloc(length + 1, 1);
  memcpy(copy, text, length);
  return copy;
}
static char *xdup(const char *text) { return xdup_n(text, strlen(text)); }
static char *xformat(const char *format, ...) {
  va_list args, measure;
  va_start(args, format);
  va_copy(measure, args);
  int length = vsnprintf(NULL, 0, format, measure);
  va_end(measure);
  if (length < 0)
    qa_range_error("chat text format failed");
  char *text = xcalloc((size_t)length + 1, 1);
  vsnprintf(text, (size_t)length + 1, format, args);
  va_end(args);
  return text;
}
static qa_save_value *checked(qa_save_value *value) {
  if (value == NULL)
    qa_range_error("out of memory");
  return value;
}
static void set_field(qa_save_value *object, const char *key, qa_save_value *value) {
  if (!qa_save_value_set(object, key, checked(value)))
    qa_range_error("out of memory");
}
static void push_item(qa_save_value *array, qa_save_value *value) {
  if (!qa_save_value_push(array, checked(value)))
    qa_range_error("out of memory");
}
static uint32_t load_u32(const uint8_t *p) {
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void store_u32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
  p[2] = (uint8_t)(v >> 16);
  p[3] = (uint8_t)(v >> 24);
}
static float load_f32(const uint8_t *p) {
  uint32_t bits = load_u32(p);
  float value;
  memcpy(&value, &bits, sizeof value);
  return value;
}
static void store_f32(uint8_t *p, double value) {
  float narrowed = (float)value;
  uint32_t bits;
  memcpy(&bits, &narrowed, sizeof bits);
  store_u32(p, bits);
}
static double fround(double value) { return (double)(float)value; }
/* JS ToInt32 for the `(x) | 0` log formats. */
/* JS String.prototype.slice over a byte string. */
static char *js_slice(const char *text, int64_t start, int64_t end) {
  int64_t length = (int64_t)strlen(text);
  int64_t from = start < 0 ? (start + length < 0 ? 0 : start + length) : (start > length ? length : start);
  int64_t to = end < 0 ? (end + length < 0 ? 0 : end + length) : (end > length ? length : end);
  return to <= from ? xdup("") : xdup_n(text + from, (size_t)(to - from));
}
/* JS Number(string): only used for Number.isInteger and the 0..7 range. */
static bool js_white_space(unsigned char c) {
  return c == ' ' || (c >= 9 && c <= 13) || c == 0xa0;
}
static double js_number(const char *text) {
  size_t start = 0, end = strlen(text);
  while (start < end && js_white_space((unsigned char)text[start]))
    start++;
  while (end > start && js_white_space((unsigned char)text[end - 1]))
    end--;
  if (start == end)
    return 0;
  const char *s = text + start;
  size_t n = end - start;
  if (n > 2 && s[0] == '0' && strchr("xXoObB", s[1]) != NULL) {
    int radix = s[1] == 'x' || s[1] == 'X' ? 16 : s[1] == 'o' || s[1] == 'O' ? 8 : 2;
    double value = 0;
    for (size_t i = 2; i < n; i++) {
      int digit = isdigit((unsigned char)s[i]) ? s[i] - '0'
                  : isalpha((unsigned char)s[i]) ? tolower((unsigned char)s[i]) - 'a' + 10
                                                 : 99;
      if (digit >= radix)
        return NAN;
      value = value * radix + digit;
    }
    return value;
  }
  size_t i = 0;
  if (s[i] == '+' || s[i] == '-')
    i++;
  if (n - i == 8 && strncmp(s + i, "Infinity", 8) == 0)
    return s[0] == '-' ? -INFINITY : INFINITY;
  size_t digits = 0;
  while (i < n && isdigit((unsigned char)s[i])) {
    i++;
    digits++;
  }
  if (i < n && s[i] == '.') {
    i++;
    while (i < n && isdigit((unsigned char)s[i])) {
      i++;
      digits++;
    }
  }
  if (digits == 0)
    return NAN;
  if (i < n && (s[i] == 'e' || s[i] == 'E')) {
    i++;
    if (i < n && (s[i] == '+' || s[i] == '-'))
      i++;
    size_t exponent = 0;
    while (i < n && isdigit((unsigned char)s[i])) {
      i++;
      exponent++;
    }
    if (exponent == 0)
      return NAN;
  }
  if (i != n)
    return NAN;
  char *copy = xdup_n(s, n);
  double value = strtod(copy, NULL);
  free(copy);
  return value;
}

/* TS chatFixed: printf-style ties to even over JS toFixed. */
static char *chat_fixed(double value, int digits) {
  if (!isfinite(value))
    return xdup(isnan(value) ? "nan" : value < 0 ? "-inf" : "inf");
  const char *sign = signbit(value) ? "-" : "";
  double absolute = fabs(value);
  double scale = qa_js_pow(10, digits), scaled = absolute * scale, whole = floor(scaled);
  if (whole <= (double)QA_MAX_SAFE_INTEGER && scaled - whole == 0.5) {
    double rounded = fmod(whole, 2) == 0 ? whole : whole + 1;
    return xformat("%s%.*f", sign, digits, rounded / scale);
  }
  /* Outside exact scaled ties toFixed and %f agree: both print the exact binary
   * value rounded to the nearest digit, including BigInt digits above 1e21. */
  return xformat("%s%.*f", sign, digits, absolute);
}

static char *chat_text(qa_chat_text_source input) { return xdup(qa_chat_text_read(input, -1)); }
static char *bounded_chat_text(qa_chat_text_source input, size_t count) {
  const char *text = qa_chat_text_read(input, (int64_t)count);
  return xdup_n(text, strnlen(text, count));
}
static void check_message_text(const char *text) {
  if (strlen(text) >= CHAT_MESSAGE_SIZE)
    qa_range_error("chat text exceeds the source 256-byte buffer");
}
static char *read_message(const uint8_t *bytes, size_t size) {
  return xdup_n((const char *)bytes, strnlen((const char *)bytes, size));
}
static void write_message(uint8_t *bytes, size_t size, const char *text, size_t offset) {
  size_t length = strlen(text);
  if (offset + length >= size)
    qa_range_error("expanded chat exceeds the source 256-byte buffer");
  memcpy(bytes + offset, text, length + 1);
}
static uint8_t *chat_span(uint8_t *bytes, size_t size, size_t offset, size_t count) {
  if (bytes == NULL)
    qa_range_error("bot chat buffer requires a nonnull pointer");
  if (offset + count > size)
    qa_range_error("bot chat buffer exceeds allocation");
  return bytes + offset;
}
static uint8_t chat_byte(uint8_t *bytes, size_t size, size_t offset) {
  return *chat_span(bytes, size, offset, 1);
}
static const char *chat_c_string(uint8_t *bytes, size_t size) {
  if (bytes == NULL)
    qa_range_error("bot chat string requires a nonnull pointer");
  if (memchr(bytes, 0, size) == NULL)
    qa_range_error("bot chat string has no terminator before allocation end");
  return (const char *)bytes;
}
static void copy_chat_text(uint8_t *bytes, size_t size, size_t offset, const char *text, size_t count,
                           const qa_chat_buffer_writes *writes) {
  uint8_t *destination = chat_span(bytes, size, offset, count);
  size_t copied = strlen(text);
  if (copied > count)
    copied = count;
  if (writes != NULL) {
    for (size_t index = 0; index < copied; index++)
      writes->set_byte(writes->ctx, destination, count, index, (uint8_t)text[index]);
    writes->clear(writes->ctx, destination, count, copied);
    return;
  }
  memcpy(destination, text, copied);
  memset(destination + copied, 0, count - copied);
}
static qa_chat_match_variable *capture(qa_chat_match_variable *variables, int64_t index) {
  if (index < 0 || index >= 8)
    qa_range_error("match variable outside 0..7");
  return &variables[index];
}
/* TS sourceUnitRandom (weights.ts): botlib's random(). */
static double source_random(const qa_bot_random *random) { return qa_bot_source_unit_random(*random); }
static char ascii_upper(char c) { return c >= 'a' && c <= 'z' ? (char)(c - 32) : c; }
static int64_t index_of(const char *input, const char *needle, bool case_sensitive) {
  size_t length = strlen(input), part = strlen(needle);
  for (size_t i = 0; i + part <= length; i++) {
    size_t j = 0;
    while (j < part && (case_sensitive ? input[i + j] == needle[j]
                                       : ascii_upper(input[i + j]) == ascii_upper(needle[j])))
      j++;
    if (j == part)
      return (int64_t)i;
  }
  return -1;
}
int64_t qa_chat_string_contains(qa_chat_text_source text, qa_chat_text_source part,
                                bool case_sensitive) {
  if (qa_chat_text_is_null(text) || qa_chat_text_is_null(part))
    return -1;
  char *input = chat_text(text), *needle = chat_text(part);
  int64_t found = index_of(input, needle, case_sensitive);
  free(input);
  free(needle);
  return found;
}
int64_t qa_string_contains_bots_a(const char *text, const char *part, bool case_sensitive) {
  return qa_chat_string_contains(qa_chat_text(text), qa_chat_text(part), case_sensitive);
}
static bool word_delimiter(char c) { return c == ' ' || c == '.' || c == ',' || c == '!'; }
static int64_t word_index(const char *text, const char *part, int64_t start) {
  int64_t length = (int64_t)strlen(text), needle = (int64_t)strlen(part);
  int64_t end = length - start - needle, pointer = start;
  for (int64_t index = 0; index <= end; index++, pointer++) {
    if (index != 0) {
      while (pointer < length && !word_delimiter(text[pointer]))
        pointer++;
      if (pointer == length)
        break;
      pointer++;
    }
    if (pointer + needle > length)
      continue;
    int64_t j = 0;
    while (j < needle && ascii_upper(text[pointer + j]) == ascii_upper(part[j]))
      j++;
    if (j == needle && (pointer + needle == length || word_delimiter(text[pointer + needle])))
      return pointer;
  }
  return -1;
}
typedef void (*replace_publish_fn)(void *ctx, int64_t offset, const char *tail,
                                   const char *replacement);
/* Consumes text; returns the replaced owned string. */
static char *replace_words(char *text, const char *synonym, const char *replacement,
                           replace_publish_fn publish, void *ctx) {
  int64_t synonym_length = (int64_t)strlen(synonym), replacement_length = (int64_t)strlen(replacement);
  int64_t found = word_index(text, synonym, 0);
  while (found >= 0) {
    int64_t prior = word_index(text, replacement, 0);
    while (prior >= 0 && !(prior <= found && found < prior + replacement_length))
      prior = word_index(text, replacement, prior + 1);
    if (prior < 0) {
      const char *tail = text + found + synonym_length;
      if (publish != NULL)
        publish(ctx, found, tail, replacement);
      char *next = xformat("%.*s%s%s", (int)found, text, replacement, tail);
      free(text);
      text = next;
    }
    found = word_index(text, synonym, found + replacement_length);
  }
  return text;
}
static bool white_space(uint8_t byte) {
  return !((byte >= 65 && byte <= 90) || (byte >= 97 && byte <= 122) || (byte >= 48 && byte <= 57) ||
           (byte != 0 && strchr("()?:'/,.[]-_+=", byte) != NULL));
}
/* Preserves UnifyWhiteSpaces' moving-pointer behavior after each memmove: the
 * scan stays at the old index and can read stale bytes past the new terminator. */
void qa_unify_white_spaces_in_place_bots_a(uint8_t *bytes, size_t size,
                                           const qa_chat_buffer_writes *writes) {
  size_t pointer = 0, old = 0;
  while (chat_byte(bytes, size, pointer) != 0) {
    while (chat_byte(bytes, size, pointer) != 0 && white_space(chat_byte(bytes, size, pointer)))
      pointer++;
    if (pointer > old) {
      if (old > 0 && chat_byte(bytes, size, pointer) != 0) {
        uint8_t *destination = chat_span(bytes, size, old++, 1);
        if (writes == NULL)
          destination[0] = 32;
        else
          writes->set_byte(writes->ctx, destination, 1, 0, 32);
      }
      if (pointer > old) {
        size_t end = pointer;
        while (chat_byte(bytes, size, end) != 0)
          end++;
        size_t count = end + 1 - pointer;
        uint8_t *destination = chat_span(bytes, size, old, count);
        const uint8_t *source = chat_span(bytes, size, pointer, count);
        if (writes == NULL)
          memmove(destination, source, count);
        else
          writes->copy(writes->ctx, destination, source, count);
      }
    }
    while (chat_byte(bytes, size, pointer) != 0 && !white_space(chat_byte(bytes, size, pointer)))
      pointer++;
    old = pointer;
  }
}
char *qa_unify_white_spaces_bots_a(const char *input) {
  size_t size = strlen(input) + 1;
  uint8_t *bytes = xcalloc(size, 1);
  memcpy(bytes, input, size);
  qa_unify_white_spaces_in_place_bots_a(bytes, size, NULL);
  char *result = read_message(bytes, size);
  free(bytes);
  return result;
}
/* TS removeTildes: the index still advances after a removal, so the character
 * following each removed tilde is skipped ("~~" keeps one tilde). */
static void remove_tildes(char *text) {
  size_t length = strlen(text);
  for (size_t index = 0; index < length; index++) {
    if (text[index] == '~') {
      memmove(text + index, text + index + 1, length - index);
      length--;
    }
  }
}
static bool strings_match(const qa_chat_cell *pieces, const char *text,
                          qa_chat_match_variable *variables) {
  int64_t last_variable = -1, pointer = 0, length = (int64_t)strlen(text);
  for (const qa_chat_cell *piece = pieces; piece != NULL; piece = qa_chat_piece_next(piece)) {
    int32_t type = qa_chat_piece_type(piece);
    if (type == 1) {
      int32_t index = qa_chat_piece_variable(piece);
      /* Offsets are the source's signed char. */
      capture(variables, index)->offset = (int8_t)(uint8_t)(uint32_t)pointer;
      last_variable = index;
      continue;
    }
    if (type != 2)
      continue;
    int64_t next_pointer = -1;
    for (const qa_chat_cell *value = qa_chat_piece_first_string(piece); value != NULL;
         value = qa_chat_match_string_next(value)) {
      const char *alternative = qa_chat_match_string_text(value);
      int64_t alternative_length = (int64_t)strlen(alternative);
      if (alternative_length == 0) {
        next_pointer = pointer;
        break;
      }
      int64_t index = index_of(text + (pointer > length ? length : pointer), alternative, false);
      if (index < 0)
        continue;
      if (last_variable >= 0) {
        qa_chat_match_variable *variable = capture(variables, last_variable);
        variable->length = pointer + index - variable->offset;
        last_variable = -1;
        next_pointer = pointer + index + alternative_length;
        break;
      }
      if (index == 0) {
        next_pointer = pointer + alternative_length;
        break;
      }
    }
    if (next_pointer < 0)
      return false;
    pointer = next_pointer;
  }
  if (last_variable < 0)
    return pointer == length;
  qa_chat_match_variable *variable = capture(variables, last_variable);
  /* A negative signed-char offset reads as an absent variable; peers control this text. */
  if (variable->offset < 0)
    return true;
  variable->length = length - variable->offset;
  return true;
}

/* ---- names ---- */
const char *qa_chat_severity_name(qa_chat_severity severity) {
  static const char *const names[] = {"info", "warning", "error", "fatal"};
  return names[severity];
}
static const char *const diagnostic_code_names[] = {
    "loaded",         "empty-replies",  "load-error",    "parse-warning",  "invalid-handle",
    "invalid-variable", "message-heap-full", "cache-full", "expansion-error", "expansion-limit",
    "test-output",    "print-fragment", "missing-random", "integrity-error"};
const char *qa_chat_diagnostic_code_name(qa_chat_diagnostic_code code) {
  return diagnostic_code_names[code];
}

/* ---- library plumbing ---- */
enum { MAXIMUM_CHAT_ISSUES = 256 };
static void report(qa_bot_chat_library *l, qa_chat_severity severity, qa_chat_diagnostic_code code,
                   char *message, const char *source, const qa_ui_script_location *location) {
  qa_chat_diagnostic issue;
  memset(&issue, 0, sizeof issue);
  issue.severity = severity;
  issue.code = code;
  issue.source = xdup(source);
  issue.message = message;
  if (location != NULL) {
    issue.has_location = true;
    issue.location = *location;
    issue.location.path = xdup(location->path);
  }
  /* Diagnostics are a ring: runtime reports would otherwise grow, and be saved, for the whole map. */
  if (l->issue_count >= MAXIMUM_CHAT_ISSUES) {
    free(l->issues[0].source);
    free(l->issues[0].message);
    if (l->issues[0].has_location) qa_ui_script_location_destroy(&l->issues[0].location);
    memmove(&l->issues[0], &l->issues[1], (l->issue_count - 1) * sizeof *l->issues);
    l->issue_count--;
  }
  l->issues = xrealloc(l->issues, (l->issue_count + 1) * sizeof *l->issues);
  l->issues[l->issue_count++] = issue;
  /* The strings outlive any growth of issues during the callback. */
  if (l->host.report != NULL)
    l->host.report(l->host.ctx, &issue);
}
static double lib_time(qa_bot_chat_library *l) {
  double time = fround(l->host.time(l->host.ctx));
  if (!isfinite(time))
    qa_range_error("chat time must be finite float32");
  return time;
}
static qa_chat_state *get_state(qa_bot_chat_library *l, int64_t handle) {
  if (handle < 1 || handle > 64) {
    report(l, QA_CHAT_SEVERITY_FATAL, QA_CHAT_DIAG_INVALID_HANDLE,
           xformat("chat state handle %lld out of range", (long long)handle), "", NULL);
    return NULL;
  }
  qa_chat_state *state = l->states[handle];
  if (state == NULL)
    report(l, QA_CHAT_SEVERITY_FATAL, QA_CHAT_DIAG_INVALID_HANDLE,
           xformat("invalid chat state %lld", (long long)handle), "", NULL);
  return state;
}
static uint8_t *state_bytes(const qa_chat_state *state) {
  return qa_bot_memory_allocation_bytes(&state->allocation);
}
static uint8_t *state_message(const qa_chat_state *state) { return state_bytes(state) + STATE_MESSAGE; }
static char *state_message_text(const qa_chat_state *state) {
  return read_message(state_message(state), CHAT_MESSAGE_SIZE);
}
static void state_set_name(const qa_chat_state *state, const char *value) {
  uint8_t *bytes = state_bytes(state);
  copy_chat_text(bytes, CHAT_STATE_BYTES, STATE_NAME, value, 32, NULL);
  bytes[39] = 0;
}
static qa_chat_initial *initial_chat_at(const qa_bot_chat_library *l, uint32_t pointer) {
  if (pointer == 0)
    return NULL;
  if (pointer <= l->initial_chat_count &&
      qa_chat_initial_pointer(l->initial_chats[pointer - 1]) == pointer)
    return l->initial_chats[pointer - 1];
  for (size_t i = 0; i < l->initial_chat_count; i++)
    if (qa_chat_initial_pointer(l->initial_chats[i]) == pointer)
      return l->initial_chats[i];
  qa_range_error("initial chat pointer is outside the owned chat allocations");
}
static qa_chat_initial *state_chat(const qa_bot_chat_library *l, const qa_chat_state *state) {
  return initial_chat_at(l, load_u32(state_bytes(state) + STATE_CHAT));
}
static void state_set_chat(const qa_chat_state *state, const qa_chat_initial *chat) {
  store_u32(state_bytes(state) + STATE_CHAT, chat != NULL ? qa_chat_initial_pointer(chat) : 0);
}
static qa_chat_console_cell *console_lookup(const qa_bot_chat_library *l, uint32_t pointer) {
  if (pointer <= l->console_cell_count && l->console_cells[pointer - 1].pointer == pointer)
    return &l->console_cells[pointer - 1];
  for (size_t i = 0; i < l->console_cell_count; i++)
    if (l->console_cells[i].pointer == pointer)
      return &l->console_cells[i];
  return NULL;
}
static uint32_t state_console(const qa_bot_chat_library *l, const qa_chat_state *state, size_t offset) {
  uint32_t pointer = load_u32(state_bytes(state) + offset);
  if (pointer != 0 && console_lookup(l, pointer) == NULL)
    qa_range_error("chat state console pointer is outside the owned heaps");
  return pointer;
}
static void state_set_u32(const qa_chat_state *state, size_t offset, uint32_t value) {
  store_u32(state_bytes(state) + offset, value);
}
static int32_t state_i32(const qa_chat_state *state, size_t offset) {
  return (int32_t)load_u32(state_bytes(state) + offset);
}
static uint8_t *cell_bytes(const qa_bot_chat_library *l, uint32_t pointer) {
  const qa_chat_console_cell *cell = console_lookup(l, pointer);
  return qa_bot_memory_allocation_bytes(&cell->allocation) + cell->offset;
}
static uint32_t cell_link(const qa_bot_chat_library *l, uint32_t pointer, size_t offset) {
  uint32_t link = load_u32(cell_bytes(l, pointer) + offset);
  if (link != 0 && console_lookup(l, link) == NULL)
    qa_range_error("console message link is outside the owned heaps");
  return link;
}
#define CELL_PREV 268
#define CELL_NEXT 272
static void cell_set(const qa_bot_chat_library *l, uint32_t pointer, size_t offset, uint32_t value) {
  store_u32(cell_bytes(l, pointer) + offset, value);
}
static void cell_snapshot(const qa_bot_chat_library *l, uint32_t pointer, qa_console_chat_message *out) {
  const uint8_t *bytes = cell_bytes(l, pointer);
  out->handle = (int32_t)load_u32(bytes);
  out->time = load_f32(bytes + 4);
  out->type = (int32_t)load_u32(bytes + 8);
  size_t length = strnlen((const char *)bytes + 12, CHAT_MESSAGE_SIZE);
  memcpy(out->message, bytes + 12, length);
  out->message[length] = '\0';
}
static qa_chat_initial *cached_chat(const qa_bot_chat_library *l, const qa_chat_cached *cached) {
  return initial_chat_at(l, load_u32(qa_bot_memory_allocation_bytes(&cached->allocation)));
}
static void free_copy(qa_bot_chat_library *l, const qa_bot_memory_allocation *allocation) {
  /* The TS handle keeps its identity after free; only a copy is cleared here. */
  qa_bot_memory_allocation copy = *allocation;
  qa_bot_memory_free(l->memory, &copy);
}
static qa_bot_memory_allocation chat_allocate(qa_bot_chat_library *l, int64_t size,
                                              qa_bot_memory_kind kind) {
  qa_bot_memory_allocation allocation;
  if (!qa_bot_memory_allocate(l->memory, size, kind, true, &allocation))
    qa_range_error("out of memory");
  return allocation;
}
static void retire_state(qa_bot_chat_library *l, qa_chat_state *state) {
  l->retired_states =
      xrealloc(l->retired_states, (l->retired_state_count + 1) * sizeof *l->retired_states);
  l->retired_states[l->retired_state_count++] = state;
}
static void cache_ensure(qa_bot_chat_library *l, size_t capacity) {
  if (capacity <= l->cache_capacity)
    return;
  l->cache = xrealloc(l->cache, capacity * sizeof *l->cache);
  memset(l->cache + l->cache_capacity, 0, (capacity - l->cache_capacity) * sizeof *l->cache);
  l->cache_capacity = capacity;
}

/* ---- construction ---- */
static bool sources_resolve_root(void *ctx, const char *path, qa_ui_script_source *out) {
  return qa_bot_script_sources_resolve_root(ctx, path, out);
}
static bool sources_resolve(void *ctx, const qa_ui_script_include_request *request,
                            qa_ui_script_source *out) {
  return qa_bot_script_sources_resolve(ctx, request, out);
}
qa_bot_chat_script_reader qa_bot_chat_script_reader_from_sources(qa_bot_script_sources *sources) {
  qa_bot_chat_script_reader reader;
  memset(&reader, 0, sizeof reader);
  reader.resolve_root = sources_resolve_root;
  reader.resolve = sources_resolve;
  reader.globals = sources->globals;
  reader.ctx = sources;
  reader.debug_eval = sources->debug_eval;
  reader.debug_eval_ctx = sources->debug_eval_ctx;
  return reader;
}
void qa_bot_chat_library_init(qa_bot_chat_library *l) {
  memset(l, 0, sizeof *l);
  qa_bot_memory_init(&l->own_memory);
  l->memory = &l->own_memory;
  l->generation_id = 1;
}
void qa_bot_chat_library_bind(qa_bot_chat_library *l, qa_bot_chat_script_reader reader,
                              qa_bot_chat_host host, const qa_bot_chat_options *options,
                              qa_bot_memory *memory) {
  l->reader = reader;
  l->host = host;
  if (options != NULL)
    l->options = *options;
  if (memory != NULL)
    l->memory = memory;
}
static void release_cell_root(qa_chat_cell **root) {
  if (*root != NULL)
    qa_chat_graph_release((*root)->owner);
  *root = NULL;
}
static void diagnostics_destroy(qa_bot_chat_library *l) {
  for (size_t i = 0; i < l->issue_count; i++) {
    free(l->issues[i].source);
    free(l->issues[i].message);
    if (l->issues[i].has_location)
      qa_ui_script_location_destroy(&l->issues[i].location);
  }
  free(l->issues);
  l->issues = NULL;
  l->issue_count = 0;
}
void qa_bot_chat_library_destroy(qa_bot_chat_library *l) {
  /* Bot memory may already be disposed by its owner: nothing here touches it. */
  for (size_t i = 0; i < l->cache_capacity; i++)
    free(l->cache[i]);
  free(l->cache);
  for (size_t i = 0; i < 65; i++)
    free(l->states[i]);
  for (size_t i = 0; i < l->retired_state_count; i++)
    free(l->retired_states[i]);
  free(l->retired_states);
  diagnostics_destroy(l);
  qa_stored_synonyms_destroy(&l->synonyms);
  qa_stored_randoms_destroy(&l->randoms);
  release_cell_root(&l->matches);
  release_cell_root(&l->replies);
  free(l->console_cells);
  for (size_t i = 0; i < l->initial_chat_count; i++)
    qa_chat_initial_destroy(l->initial_chats[i]);
  free(l->initial_chats);
  qa_bot_memory_destroy(&l->own_memory);
  memset(l, 0, sizeof *l);
}
const qa_chat_diagnostic *qa_bot_chat_library_diagnostics(const qa_bot_chat_library *l,
                                                          size_t *out_count) {
  *out_count = l->issue_count;
  return l->issues;
}
qa_chat_configuration_counts qa_bot_chat_library_configuration_counts(const qa_bot_chat_library *l) {
  qa_chat_configuration_counts counts = {(int64_t)l->synonyms.count, (int64_t)l->randoms.count, 0, 0};
  for (const qa_chat_cell *match = l->matches; match != NULL; match = qa_chat_template_next(match))
    counts.matches++;
  for (const qa_chat_cell *reply = l->replies; reply != NULL; reply = qa_chat_reply_next(reply))
    counts.replies++;
  return counts;
}
bool qa_bot_chat_library_dump_initial_chat(qa_bot_chat_library *l, int64_t handle,
                                           qa_initial_chat *out) {
  qa_chat_state *state = get_state(l, handle);
  qa_chat_initial *chat = state != NULL ? state_chat(l, state) : NULL;
  if (chat == NULL)
    return false;
  qa_chat_initial_snapshot(chat, out);
  return true;
}

/* ---- log dumps ---- */
static bool log_file(const qa_bot_chat_library *l) {
  return l->options.log_has_file != NULL && l->options.log_has_file(l->options.ctx);
}
static void file_write(const qa_bot_chat_library *l, char *text) {
  l->options.log_file_write(l->options.ctx, text);
  free(text);
}
static void log_write(const qa_bot_chat_library *l, char *text) {
  if (l->options.log_write != NULL)
    l->options.log_write(l->options.ctx, text);
  free(text);
}
void qa_bot_chat_library_dump_synonym_list(qa_bot_chat_library *l) {
  if (!log_file(l))
    return;
  for (size_t i = 0; i < l->synonyms.count; i++) {
    file_write(l, xformat("%d : [", (int32_t)qa_stored_synonym_group_context(&l->synonyms, i)));
    size_t entries = l->synonyms.groups[i].entry_count;
    for (size_t j = 0; j < entries; j++) {
      char *weight = chat_fixed(qa_stored_synonym_weight(&l->synonyms, i, j), 2);
      file_write(l, xformat("(\"%s\", %s)", qa_stored_synonym_text(&l->synonyms, i, j), weight));
      free(weight);
      if (j + 1 < entries)
        file_write(l, xdup(", "));
    }
    file_write(l, xdup("]\n"));
  }
}
void qa_bot_chat_library_dump_random_string_list(qa_bot_chat_library *l) {
  if (!log_file(l))
    return;
  for (size_t i = 0; i < l->randoms.count; i++) {
    file_write(l, xformat("%s = {", qa_stored_random_name(&l->randoms, i)));
    size_t messages = l->randoms.lists[i].message_count;
    for (size_t j = 0; j < messages; j++) {
      file_write(l, xformat("\"%s\"", qa_stored_random_message(&l->randoms, i, (int64_t)j)));
      file_write(l, xdup(j + 1 < messages ? ", " : "}\n"));
    }
  }
}
void qa_bot_chat_library_dump_match_templates(qa_bot_chat_library *l) {
  if (!log_file(l))
    return;
  for (const qa_chat_cell *match = l->matches; match != NULL; match = qa_chat_template_next(match)) {
    file_write(l, xdup("{ "));
    for (const qa_chat_cell *piece = qa_chat_template_first(match); piece != NULL;
         piece = qa_chat_piece_next(piece)) {
      int32_t type = qa_chat_piece_type(piece);
      if (type == 2) {
        for (const qa_chat_cell *text = qa_chat_piece_first_string(piece); text != NULL;
             text = qa_chat_match_string_next(text)) {
          file_write(l, xformat("\"%s\"", qa_chat_match_string_text(text)));
          if (qa_chat_match_string_next(text) != NULL)
            file_write(l, xdup("|"));
        }
      } else if (type == 1) {
        file_write(l, xformat("%d", qa_chat_piece_variable(piece)));
      }
      if (qa_chat_piece_next(piece) != NULL)
        file_write(l, xdup(", "));
    }
    file_write(l, xformat(" = (%d, %d);}\n", qa_chat_template_type(match), qa_chat_template_subtype(match)));
  }
}
typedef void (*key_write_fn)(void *ctx, char *text);
static void write_reply_chat_keys(const qa_chat_cell *reply, key_write_fn write, void *ctx) {
  write(ctx, xdup("["));
  for (const qa_chat_cell *key = qa_chat_reply_keys(reply); key != NULL; key = qa_chat_key_next(key)) {
    int32_t flags = qa_chat_key_flags(key);
    if ((flags & 1) != 0)
      write(ctx, xdup("&"));
    else if ((flags & 2) != 0)
      write(ctx, xdup("!"));
    if ((flags & 4) != 0) {
      write(ctx, xdup("name"));
    } else if ((flags & 64) != 0) {
      write(ctx, xdup("female"));
    } else if ((flags & 128) != 0) {
      write(ctx, xdup("male"));
    } else if ((flags & 256) != 0) {
      write(ctx, xdup("it"));
    } else if ((flags & 16) != 0) {
      write(ctx, xdup("("));
      for (const qa_chat_cell *piece = qa_chat_key_match(key); piece != NULL;
           piece = qa_chat_piece_next(piece)) {
        if (qa_chat_piece_type(piece) == 2) {
          const qa_chat_cell *first = qa_chat_piece_first_string(piece);
          if (first == NULL)
            qa_range_error("reply match string has no first alternative");
          write(ctx, xformat("\"%s\"", qa_chat_match_string_text(first)));
        } else {
          write(ctx, xformat("%d", qa_chat_piece_variable(piece)));
        }
        if (qa_chat_piece_next(piece) != NULL)
          write(ctx, xdup(", "));
      }
      write(ctx, xdup(")"));
    } else if ((flags & 8) != 0) {
      write(ctx, xformat("\"%s\"", qa_chat_key_text(key)));
    }
    if (qa_chat_key_next(key) != NULL) {
      write(ctx, xdup(", "));
    } else {
      char *priority = chat_fixed(qa_chat_reply_priority(reply), 0);
      write(ctx, xformat("] = %s\n", priority));
      free(priority);
    }
  }
  write(ctx, xdup("{\n"));
}
static void key_file_write(void *ctx, char *text) { file_write(ctx, text); }
void qa_bot_chat_library_dump_reply_chat(qa_bot_chat_library *l) {
  if (!log_file(l))
    return;
  file_write(l, xdup("BotDumpReplyChat:\n"));
  for (const qa_chat_cell *reply = l->replies; reply != NULL; reply = qa_chat_reply_next(reply)) {
    write_reply_chat_keys(reply, key_file_write, l);
    for (const qa_chat_cell *message = qa_chat_reply_first_message(reply); message != NULL;
         message = qa_chat_reply_message_next(message))
      file_write(l, xformat("\t\"%s\";\n", qa_chat_reply_message_text(message)));
    file_write(l, xdup("}\n"));
  }
}
void qa_bot_chat_library_log_initial_chat(qa_bot_chat_library *l, int64_t handle) {
  qa_chat_state *state = get_state(l, handle);
  qa_chat_initial *chat = state != NULL ? state_chat(l, state) : NULL;
  if (chat == NULL)
    return;
  log_write(l, xdup("{"));
  for (uint32_t type = qa_chat_initial_first_type(chat); type != 0;
       type = qa_chat_initial_type_next(chat, type)) {
    log_write(l, xformat(" type \"%s\"", qa_chat_initial_type_name(chat, type)));
    log_write(l, xdup(" {"));
    log_write(l, xformat("  numchatmessages = %d", qa_chat_initial_type_num_messages(chat, type)));
    for (uint32_t message = qa_chat_initial_type_first_message(chat, type); message != 0;
         message = qa_chat_initial_message_next(chat, message))
      log_write(l, xformat("  \"%s\"", qa_chat_initial_message_text(chat, message)));
    log_write(l, xdup(" }"));
  }
  log_write(l, xdup("}"));
}

/* ---- source callbacks and retirement ---- */
/* TS `current` closure plus the catch that receives ChatSourceCallbackAbort. */
typedef struct chat_scope {
  qa_bot_chat_library *l;
  uint64_t generation_id;
  int64_t revision;
  qa_chat_state *state; /* non-NULL: the load form of current() */
  int64_t handle;
  qa_chat_text_source name;
  jmp_buf jump;
} chat_scope;
static bool scope_current(const chat_scope *scope) {
  const qa_bot_chat_library *l = scope->l;
  if (l->generation_id != scope->generation_id)
    return false;
  if (scope->state != NULL)
    return l->states[scope->handle] == scope->state && scope->state->revision == scope->revision;
  return scope->revision == l->setup_revision;
}
/* TS chatSourceCallback's retirement throw. */
static void scope_after_callback(chat_scope *scope) {
  if (!scope_current(scope))
    longjmp(scope->jump, 1);
}
static void scope_report(void *ctx, const qa_ui_script_diagnostic *issue) {
  chat_scope *scope = ctx;
  report(scope->l, issue->severity == QA_UI_SCRIPT_SEVERITY_ERROR ? QA_CHAT_SEVERITY_ERROR : QA_CHAT_SEVERITY_WARNING,
         issue->severity == QA_UI_SCRIPT_SEVERITY_ERROR ? QA_CHAT_DIAG_LOAD_ERROR : QA_CHAT_DIAG_PARSE_WARNING,
         xdup(issue->message), issue->location.path, &issue->location);
  scope_after_callback(scope);
}
static bool scope_resolve(void *ctx, const qa_ui_script_include_request *request,
                          qa_ui_script_source *out) {
  chat_scope *scope = ctx;
  bool found = scope->l->reader.resolve(scope->l->reader.ctx, request, out);
  scope_after_callback(scope);
  return found;
}
static void scope_debug_eval(void *ctx, const char *text) {
  chat_scope *scope = ctx;
  scope->l->reader.debug_eval(scope->l->reader.debug_eval_ctx, text);
  scope_after_callback(scope);
}
static const char *scope_name(void *ctx, int64_t maximum_bytes) {
  chat_scope *scope = ctx;
  const char *text = qa_chat_text_read(scope->name, maximum_bytes);
  scope_after_callback(scope);
  return text;
}
/* TS parser(): NULL is the undefined result for an unresolved root. */
static qa_chat_data_parser *make_parser(chat_scope *scope, const char *path,
                                        qa_chat_data_memory *storage) {
  qa_bot_chat_library *l = scope->l;
  qa_ui_script_source source;
  memset(&source, 0, sizeof source);
  bool found = l->reader.resolve_root(l->reader.ctx, path, &source);
  scope_after_callback(scope);
  if (!found)
    return NULL;
  qa_ui_script_preprocessor_options options;
  memset(&options, 0, sizeof options);
  options.globals = l->reader.globals;
  options.report = scope_report;
  options.report_ctx = scope;
  if (l->reader.debug_eval != NULL) {
    options.debug_eval = scope_debug_eval;
    options.debug_eval_ctx = scope;
  }
  char *source_path = xdup(source.path != NULL ? source.path : path);
  source.path = source_path;
  qa_ui_script_source_reader *reader = qa_ui_script_source_reader_open(&source, scope_resolve, scope, &options);
  if (reader == NULL)
    qa_range_error("out of memory");
  qa_chat_data_parser *parser =
      qa_chat_data_parser_create(reader, source_path, storage, l->memory, scope_report, scope);
  free(source_path);
  return parser;
}

/* ---- setup ---- */
typedef enum load_kind { LOAD_SYNONYMS, LOAD_RANDOMS, LOAD_MATCHES, LOAD_REPLIES } load_kind;
typedef struct load_value {
  qa_stored_synonyms synonyms;
  qa_stored_randoms randoms;
  qa_chat_cell *cell; /* holds one graph reference */
} load_value;
typedef enum parse_outcome { PARSE_OK, PARSE_FAILED, PARSE_RETIRED } parse_outcome;
static void load_value_destroy(load_value *value) {
  qa_stored_synonyms_destroy(&value->synonyms);
  qa_stored_randoms_destroy(&value->randoms);
  release_cell_root(&value->cell);
}
static parse_outcome load_parse(chat_scope *scope, const char *path, load_kind kind,
                                qa_chat_data_memory *storage, qa_chat_data_parser **parser,
                                load_value *value) {
  if (setjmp(scope->jump) != 0)
    return PARSE_RETIRED;
  *parser = make_parser(scope, path, storage);
  if (*parser == NULL)
    return PARSE_OK;
  bool ok = false;
  switch (kind) {
  case LOAD_SYNONYMS:
    ok = qa_chat_data_parser_synonyms(*parser, &value->synonyms);
    break;
  case LOAD_RANDOMS:
    ok = qa_chat_data_parser_randoms(*parser, &value->randoms);
    break;
  case LOAD_MATCHES:
    ok = qa_chat_data_parser_match_templates(*parser, &value->cell);
    break;
  case LOAD_REPLIES:
    ok = qa_chat_data_parser_reply_chats(*parser, &value->cell);
    break;
  }
  if (ok && value->cell != NULL)
    qa_chat_graph_retain(value->cell->owner);
  return ok ? PARSE_OK : PARSE_FAILED;
}
/* Takes the value; allocation NULL is null. */
static void load_publish(qa_bot_chat_library *l, load_kind kind, load_value *value,
                         const qa_bot_memory_allocation *allocation) {
  switch (kind) {
  case LOAD_SYNONYMS:
    qa_stored_synonyms_destroy(&l->synonyms);
    l->synonyms = value->synonyms;
    memset(&value->synonyms, 0, sizeof value->synonyms);
    l->has_synonyms_allocation = l->synonyms.count != 0 && allocation != NULL;
    if (l->has_synonyms_allocation)
      l->synonyms_allocation = *allocation;
    break;
  case LOAD_RANDOMS:
    qa_stored_randoms_destroy(&l->randoms);
    l->randoms = value->randoms;
    memset(&value->randoms, 0, sizeof value->randoms);
    l->has_randoms_allocation = l->randoms.count != 0 && allocation != NULL;
    if (l->has_randoms_allocation)
      l->randoms_allocation = *allocation;
    break;
  case LOAD_MATCHES:
    release_cell_root(&l->matches);
    l->matches = value->cell;
    value->cell = NULL;
    break;
  case LOAD_REPLIES:
    release_cell_root(&l->replies);
    l->replies = value->cell;
    value->cell = NULL;
    break;
  }
}
static void check_integrity_text(qa_bot_chat_library *l, chat_scope *scope, const char *text,
                                 qa_bot_memory_allocation **strings, size_t *string_count,
                                 uint32_t *head, bool *stop);
/* TS checkIntegrity over either replyMessages or chat.allMessages. */
static void check_integrity_free(qa_bot_chat_library *l, qa_bot_memory_allocation *strings,
                                 uint32_t head) {
  for (uint32_t pointer = head; pointer != 0;) {
    if (pointer > 0x7fffffffu)
      qa_range_error("invalid chat integrity string pointer");
    const qa_bot_memory_allocation *allocation = &strings[pointer - 1];
    pointer = load_u32(qa_bot_memory_allocation_bytes(allocation) + 4);
    free_copy(l, allocation);
  }
  free(strings);
}
static void check_integrity_replies(qa_bot_chat_library *l, chat_scope *scope, const qa_chat_cell *first) {
  qa_bot_memory_allocation *strings = NULL;
  size_t string_count = 0;
  uint32_t head = 0;
  bool stop = false;
  for (const qa_chat_cell *reply = first; reply != NULL && !stop; reply = qa_chat_reply_next(reply))
    for (const qa_chat_cell *message = qa_chat_reply_first_message(reply); message != NULL && !stop;
         message = qa_chat_reply_message_next(message)) {
      char *text = xdup(qa_chat_reply_message_text(message));
      check_integrity_text(l, scope, text, &strings, &string_count, &head, &stop);
      free(text);
    }
  check_integrity_free(l, strings, head);
}
static void check_integrity_chat(qa_bot_chat_library *l, chat_scope *scope, qa_chat_initial *chat) {
  qa_bot_memory_allocation *strings = NULL;
  size_t string_count = 0;
  uint32_t head = 0;
  bool stop = false;
  for (uint32_t type = qa_chat_initial_first_type(chat); type != 0 && !stop;
       type = qa_chat_initial_type_next(chat, type))
    for (uint32_t message = qa_chat_initial_type_first_message(chat, type); message != 0 && !stop;
         message = qa_chat_initial_message_next(chat, message)) {
      char *text = xdup(qa_chat_initial_message_text(chat, message));
      check_integrity_text(l, scope, text, &strings, &string_count, &head, &stop);
      free(text);
    }
  check_integrity_free(l, strings, head);
}
static bool load_finish_replies(qa_bot_chat_library *l, chat_scope *scope, const qa_chat_cell *replies) {
  bool developer = l->options.developer != NULL && l->options.developer(l->options.ctx);
  if (!scope_current(scope))
    return false;
  if (developer)
    check_integrity_replies(l, scope, replies);
  if (!scope_current(scope))
    return false;
  if (replies == NULL)
    report(l, QA_CHAT_SEVERITY_INFO, QA_CHAT_DIAG_EMPTY_REPLIES, xdup("no rchats"), "", NULL);
  return scope_current(scope);
}
/* TS load<T>. */
static bool chat_load(chat_scope *scope, const char *path, load_kind kind, int passes, bool timed) {
  qa_bot_chat_library *l = scope->l;
  bool has_debug = l->options.debug_milliseconds != NULL;
  double load_start = timed && has_debug ? l->options.debug_milliseconds(l->options.ctx) : 0;
  load_value value;
  memset(&value, 0, sizeof value);
  load_value empty;
  memset(&empty, 0, sizeof empty);
  int64_t size = 0;
  bool has_allocation = false;
  qa_bot_memory_allocation allocation;
  memset(&allocation, 0, sizeof allocation);
  for (int pass = 0; pass < passes; pass++) {
    if (!scope_current(scope))
      goto retired;
    has_allocation = pass > 0 && size > 0;
    if (has_allocation)
      allocation = chat_allocate(l, size, QA_BOT_MEMORY_HUNK);
    qa_chat_data_memory storage;
    qa_chat_data_memory_init(&storage, has_allocation ? &allocation : NULL);
    if (!scope_current(scope))
      goto retired;
    qa_chat_data_parser *parser = NULL;
    load_value_destroy(&value);
    parse_outcome outcome = load_parse(scope, path, kind, &storage, &parser, &value);
    if (outcome == PARSE_RETIRED) {
      qa_chat_data_parser_destroy(parser);
      goto retired;
    }
    if (outcome == PARSE_FAILED) {
      if (!scope_current(scope)) {
        qa_chat_data_parser_destroy(parser);
        goto retired;
      }
      qa_chat_data_parser_free_after_error(parser);
      qa_chat_data_parser_destroy(parser);
      load_publish(l, kind, &empty, NULL);
      load_value_destroy(&value);
      return true;
    }
    if (!scope_current(scope)) {
      qa_chat_data_parser_destroy(parser);
      goto retired;
    }
    if (parser == NULL) {
      report(l, QA_CHAT_SEVERITY_ERROR, QA_CHAT_DIAG_LOAD_ERROR, xformat("counldn't load %s", path), path, NULL);
      if (!scope_current(scope))
        goto retired;
      load_publish(l, kind, &empty, NULL);
      load_value_destroy(&value);
      return true;
    }
    qa_chat_data_parser_dispose(parser);
    size = qa_chat_data_memory_byte_length(&storage);
    qa_chat_data_parser_destroy(parser);
  }
  report(l, QA_CHAT_SEVERITY_INFO, QA_CHAT_DIAG_LOADED, xformat("loaded %s", path), path, NULL);
  if (!scope_current(scope))
    goto retired;
  if (timed && has_debug)
    report(l, QA_CHAT_SEVERITY_INFO, QA_CHAT_DIAG_LOADED,
           xformat("random strings %d msec",
                   qa_js_to_int32(l->options.debug_milliseconds(l->options.ctx) - load_start)),
           path, NULL);
  if (!scope_current(scope))
    goto retired;
  if (kind == LOAD_REPLIES && !load_finish_replies(l, scope, value.cell))
    goto retired;
  load_publish(l, kind, &value, has_allocation ? &allocation : NULL);
  load_value_destroy(&value);
  return true;
retired:
  load_value_destroy(&value);
  return false;
}
static const char *option_file(const qa_bot_chat_library *l, const char *option, const char *fixed,
                               const char *fallback) {
  if (l->options.file_name_fn != NULL) {
    const char *name = l->options.file_name_fn(l->options.ctx, option);
    if (name != NULL)
      return name;
  }
  return fixed != NULL ? fixed : fallback;
}
void qa_bot_chat_library_setup(qa_bot_chat_library *l) {
  bool has_debug = l->options.debug_milliseconds != NULL;
  double start_time = has_debug ? l->options.debug_milliseconds(l->options.ctx) : 0;
  chat_scope scope;
  scope.l = l;
  scope.generation_id = l->generation_id;
  scope.revision = ++l->setup_revision;
  scope.state = NULL;
  scope.handle = 0;
  scope.name = qa_chat_text(NULL);
  char *path = xdup(option_file(l, "synonymFile", l->options.synonym_file, "syn.c"));
  bool go_on = chat_load(&scope, path, LOAD_SYNONYMS, 2, false);
  free(path);
  if (!go_on)
    return;
  path = xdup(option_file(l, "randomFile", l->options.random_file, "rnd.c"));
  go_on = chat_load(&scope, path, LOAD_RANDOMS, 2, true);
  free(path);
  if (!go_on)
    return;
  path = xdup(option_file(l, "matchFile", l->options.match_file, "match.c"));
  go_on = chat_load(&scope, path, LOAD_MATCHES, 1, false);
  free(path);
  if (!go_on)
    return;
  bool no_chat = l->options.no_chat != NULL && l->options.no_chat(l->options.ctx);
  if (!scope_current(&scope))
    return;
  if (!no_chat) {
    path = xdup(option_file(l, "replyFile", l->options.reply_file, "rchat.c"));
    go_on = chat_load(&scope, path, LOAD_REPLIES, 1, false);
    free(path);
    if (!go_on)
      return;
  }
  if (!scope_current(&scope))
    return;
  if (l->has_console_heap)
    free_copy(l, &l->console_heap);
  if (!scope_current(&scope))
    return;
  double value = l->options.max_messages_fn != NULL ? l->options.max_messages_fn(l->options.ctx)
                 : l->options.has_max_messages      ? l->options.max_messages
                                                    : 1024;
  if (!scope_current(&scope))
    return;
  double max_messages = trunc(fround(value));
  if (!isfinite(max_messages) || max_messages < 2 || max_messages > 0x7fffffff)
    qa_range_error("InitConsoleMessageHeap: max_messages must truncate to at least 2 within the source int32 range");
  int64_t count = (int64_t)max_messages;
  qa_bot_memory_allocation heap = chat_allocate(l, count * CONSOLE_MESSAGE_BYTES, QA_BOT_MEMORY_HUNK);
  if (!scope_current(&scope))
    return;
  l->console_heap = heap;
  l->has_console_heap = true;
  uint32_t first = 0, previous = 0;
  l->console_cells = xrealloc(l->console_cells, (l->console_cell_count + (size_t)count) * sizeof *l->console_cells);
  for (int64_t index = 0; index < count; index++) {
    uint32_t pointer = (uint32_t)l->console_cell_count + 1;
    qa_chat_console_cell *cell = &l->console_cells[l->console_cell_count++];
    cell->allocation = heap;
    cell->offset = index * CONSOLE_MESSAGE_BYTES;
    cell->pointer = pointer;
    cell_set(l, pointer, CELL_PREV, previous);
    if (previous == 0)
      first = pointer;
    else
      cell_set(l, previous, CELL_NEXT, pointer);
    previous = pointer;
  }
  l->free_console_messages = first;
  if (has_debug)
    report(l, QA_CHAT_SEVERITY_INFO, QA_CHAT_DIAG_LOADED,
           xformat("setup chat AI %d msec", qa_js_to_int32(l->options.debug_milliseconds(l->options.ctx) - start_time)),
           "", NULL);
}

/* ---- integrity ---- */
static bool integrity_has_string(const qa_bot_memory_allocation *strings, size_t count, uint32_t head,
                                 const char *name) {
  for (uint32_t pointer = head; pointer != 0;) {
    if (pointer > count)
      qa_range_error("invalid chat integrity string pointer");
    const uint8_t *bytes = qa_bot_memory_allocation_bytes(&strings[pointer - 1]);
    uint32_t self = load_u32(bytes);
    if (self == 0 || self > count)
      qa_range_error("invalid chat integrity string pointer");
    const qa_bot_memory_allocation *string = &strings[self - 1];
    char *text = read_message(qa_bot_memory_allocation_bytes(string) + 8, string->length - 8);
    bool same = strcmp(text, name) == 0;
    free(text);
    if (same)
      return true;
    pointer = load_u32(bytes + 4);
  }
  return false;
}
static void check_integrity_text(qa_bot_chat_library *l, chat_scope *scope, const char *text,
                                 qa_bot_memory_allocation **strings, size_t *string_count,
                                 uint32_t *head, bool *stop) {
  size_t pointer = 0, length = strlen(text);
  while (pointer < length) {
    if (text[pointer++] != ESCAPE_CHAR)
      continue;
    char kind = text[pointer]; /* the NUL stands for JS charAt("") at the end */
    if (kind != 'v' && kind != 'r') {
      report(l, QA_CHAT_SEVERITY_FATAL, QA_CHAT_DIAG_INTEGRITY_ERROR,
             xformat("BotCheckChatMessageIntegrety: message \"%s\" invalid escape char", text), "", NULL);
      if (!scope_current(scope)) {
        *stop = true;
        return;
      }
      continue;
    }
    size_t start = ++pointer;
    while (pointer < length && text[pointer] != ESCAPE_CHAR)
      pointer++;
    char *name = xdup_n(text + start, pointer - start);
    if (pointer < length)
      pointer++;
    if (kind == 'r') {
      const char *random = qa_bot_chat_library_random_string(l, name);
      if (!scope_current(scope)) {
        *stop = true;
        free(name);
        return;
      }
      if (random == NULL && !integrity_has_string(*strings, *string_count, *head, name)) {
        char *line = xformat("%s = {\"%s\"} //MISSING RANDOM\r\n", name, name);
        log_write(l, line);
        if (!scope_current(scope)) {
          *stop = true;
          free(name);
          return;
        }
        size_t name_length = strlen(name);
        qa_bot_memory_allocation allocation =
            chat_allocate(l, 8 + (int64_t)name_length + 1, QA_BOT_MEMORY_HEAP);
        uint8_t *bytes = qa_bot_memory_allocation_bytes(&allocation);
        *strings = xrealloc(*strings, (*string_count + 1) * sizeof **strings);
        (*strings)[(*string_count)++] = allocation;
        store_u32(bytes, (uint32_t)*string_count);
        write_message(bytes, allocation.length, name, 8);
        store_u32(bytes + 4, *head);
        *head = (uint32_t)*string_count;
        report(l, QA_CHAT_SEVERITY_WARNING, QA_CHAT_DIAG_MISSING_RANDOM,
               xformat("missing random string %s", name), "", NULL);
        if (!scope_current(scope)) {
          *stop = true;
          free(name);
          return;
        }
      }
    }
    free(name);
  }
}

/* ---- lifecycle ---- */
void qa_bot_chat_library_shutdown(qa_bot_chat_library *l) {
  uint64_t generation = ++l->generation_id;
  l->setup_revision = 0;
  /* BotShutdownChatAI intentionally visits 0..63, leaving source handle 64. */
  for (int64_t handle = 0; handle < 64; handle++) {
    if (l->states[handle] != NULL)
      qa_bot_chat_library_free(l, handle);
    if (l->generation_id != generation || l->setup_revision != 0)
      return;
  }
  for (size_t index = 0; index < 64 && index < l->cache_capacity; index++) {
    qa_chat_cached *cached = l->cache[index];
    if (cached == NULL)
      continue;
    qa_chat_initial *chat = cached_chat(l, cached);
    if (chat != NULL)
      free_copy(l, qa_chat_initial_allocation(chat));
    free_copy(l, &cached->allocation);
    free(cached);
    l->cache[index] = NULL;
  }
  if (l->has_console_heap)
    free_copy(l, &l->console_heap);
  l->has_console_heap = false;
  if (l->matches != NULL)
    qa_chat_template_free(l->matches);
  release_cell_root(&l->matches);
  if (l->has_randoms_allocation)
    free_copy(l, &l->randoms_allocation);
  l->has_randoms_allocation = false;
  qa_stored_randoms_destroy(&l->randoms);
  if (l->has_synonyms_allocation)
    free_copy(l, &l->synonyms_allocation);
  l->has_synonyms_allocation = false;
  qa_stored_synonyms_destroy(&l->synonyms);
  if (l->replies != NULL)
    qa_chat_reply_free(l->replies);
  release_cell_root(&l->replies);
}
void qa_bot_chat_library_dispose_resources(qa_bot_chat_library *l) {
  l->generation_id++;
  l->setup_revision = 0;
  for (size_t i = 0; i < l->cache_capacity; i++) {
    free(l->cache[i]);
    l->cache[i] = NULL;
  }
  l->cache_length = 0;
  for (size_t i = 0; i < 65; i++) {
    if (l->states[i] != NULL)
      retire_state(l, l->states[i]);
    l->states[i] = NULL;
  }
  l->free_console_messages = 0;
  l->has_console_heap = false;
  l->console_cell_count = 0;
  for (size_t i = 0; i < l->initial_chat_count; i++)
    qa_chat_initial_destroy(l->initial_chats[i]);
  l->initial_chat_count = 0;
  l->has_synonyms_allocation = l->has_randoms_allocation = false;
  qa_stored_synonyms_destroy(&l->synonyms);
  qa_stored_randoms_destroy(&l->randoms);
  release_cell_root(&l->matches);
  release_cell_root(&l->replies);
}
void qa_bot_chat_library_reset(qa_bot_chat_library *l) {
  for (const qa_chat_cell *reply = l->replies; reply != NULL; reply = qa_chat_reply_next(reply))
    for (const qa_chat_cell *message = qa_chat_reply_first_message(reply); message != NULL;
         message = qa_chat_reply_message_next(message))
      qa_chat_reply_message_set_time(message, 0);
}
int64_t qa_bot_chat_library_allocate(qa_bot_chat_library *l) {
  for (int64_t handle = 1; handle <= 64; handle++) {
    if (l->states[handle] != NULL)
      continue;
    qa_chat_state *state = xcalloc(1, sizeof *state);
    state->allocation = chat_allocate(l, CHAT_STATE_BYTES, QA_BOT_MEMORY_HEAP);
    l->states[handle] = state;
    return handle;
  }
  return 0;
}
void qa_bot_chat_library_free(qa_bot_chat_library *l, int64_t handle) {
  qa_chat_state *state = get_state(l, handle);
  if (state == NULL)
    return;
  uint64_t generation = l->generation_id;
  bool reload = l->options.reload_characters != NULL && l->options.reload_characters(l->options.ctx);
  if (l->generation_id != generation || l->states[handle] != state)
    return;
  if (reload)
    qa_bot_chat_library_free_chat_file(l, handle);
  qa_console_chat_message message;
  while (qa_bot_chat_library_next_console_message(l, handle, &message) && message.handle != 0)
    qa_bot_chat_library_remove_console_message(l, handle, message.handle);
  free_copy(l, &state->allocation);
  state->revision++;
  retire_state(l, state);
  l->states[handle] = NULL;
}
void qa_bot_chat_library_free_chat_file(qa_bot_chat_library *l, int64_t handle) {
  qa_chat_state *state = get_state(l, handle);
  if (state == NULL)
    return;
  qa_chat_initial *chat = state_chat(l, state);
  if (chat != NULL)
    free_copy(l, qa_chat_initial_allocation(chat));
  state_set_chat(state, NULL);
  state->revision++;
}

/* ---- loadChatFile ---- */
static bool failed_load(chat_scope *scope, qa_chat_text_source path, qa_chat_text_source name) {
  qa_bot_chat_library *l = scope->l;
  if (!scope_current(scope))
    return false;
  state_set_chat(scope->state, NULL);
  char *chat_name = chat_text(name), *filename = chat_text(path);
  report(l, QA_CHAT_SEVERITY_FATAL, QA_CHAT_DIAG_LOAD_ERROR,
         xformat("couldn't load chat %s from %s", chat_name, filename), filename, NULL);
  free(chat_name);
  free(filename);
  return false;
}
typedef enum initial_outcome {
  INITIAL_OK,
  INITIAL_FAILED,
  INITIAL_RETIRED,
  INITIAL_NOT_CURRENT
} initial_outcome;
static initial_outcome load_initial_parse(chat_scope *scope, qa_chat_text_source path,
                                          qa_chat_text_source source_name, bool previously_found,
                                          qa_chat_initial *chat, qa_chat_data_memory *storage,
                                          qa_chat_data_parser **parser, char **path_text) {
  if (setjmp(scope->jump) != 0)
    return INITIAL_RETIRED;
  *path_text = chat_text(path);
  *parser = make_parser(scope, *path_text, storage);
  if (!scope_current(scope))
    return INITIAL_NOT_CURRENT;
  if (*parser == NULL)
    return INITIAL_OK;
  bool ok;
  if (chat != NULL) {
    ok = qa_chat_data_parser_initial_into(*parser, source_name, previously_found, chat);
  } else {
    qa_initial_chat snapshot;
    ok = qa_chat_data_parser_initial(*parser, source_name, previously_found, &snapshot);
    if (ok)
      qa_initial_chat_destroy(&snapshot);
  }
  return ok ? INITIAL_OK : INITIAL_FAILED;
}
bool qa_bot_chat_library_load_chat_file(qa_bot_chat_library *l, int64_t handle,
                                        qa_chat_text_source path, qa_chat_text_source name) {
  qa_chat_state *state = get_state(l, handle);
  if (state == NULL)
    return false;
  qa_bot_chat_library_free_chat_file(l, handle);
  chat_scope scope;
  scope.l = l;
  scope.generation_id = l->generation_id;
  scope.revision = state->revision;
  scope.state = state;
  scope.handle = handle;
  scope.name = name;
  bool reload = l->options.reload_characters != NULL && l->options.reload_characters(l->options.ctx);
  if (!scope_current(&scope))
    return false;
  int64_t available = 0;
  if (!reload) {
    available = -1;
    cache_ensure(l, 64);
    for (size_t index = 0; index < 64; index++) {
      const qa_chat_cached *cached = l->cache[index];
      if (cached == NULL) {
        if (available == -1)
          available = (int64_t)index;
        continue;
      }
      const uint8_t *bytes = qa_bot_memory_allocation_bytes(&cached->allocation);
      char *cached_path = read_message(bytes + 4, 64);
      bool same_path = qa_chat_text_equals_source(cached_path, path, true);
      free(cached_path);
      if (!scope_current(&scope))
        return false;
      if (!same_path)
        continue;
      char *cached_name = read_message(qa_bot_memory_allocation_bytes(&l->cache[index]->allocation) + 68, 64);
      bool same_name = qa_chat_text_equals_source(cached_name, name, true);
      free(cached_name);
      if (!scope_current(&scope))
        return false;
      if (same_name) {
        state_set_chat(state, cached_chat(l, l->cache[index]));
        return true;
      }
    }
    if (available == -1) {
      char *chat_name = chat_text(name), *filename = chat_text(path);
      report(l, QA_CHAT_SEVERITY_FATAL, QA_CHAT_DIAG_CACHE_FULL,
             xformat("ichatdata table full; couldn't load chat %s from %s", chat_name, filename), filename, NULL);
      free(chat_name);
      free(filename);
      return false;
    }
  }
  qa_chat_text_source source_name = name.fn != NULL ? qa_chat_text_live(scope_name, &scope) : name;
  bool has_debug = l->options.debug_milliseconds != NULL;
  double start_time = has_debug ? l->options.debug_milliseconds(l->options.ctx) : 0;
  qa_chat_initial *chat = NULL;
  int64_t size = 0;
  for (int pass = 0; pass < 2; pass++) {
    bool has_allocation = pass > 0 && size > 0;
    qa_bot_memory_allocation allocation;
    memset(&allocation, 0, sizeof allocation);
    if (has_allocation)
      allocation = chat_allocate(l, size, QA_BOT_MEMORY_HEAP);
    if (!scope_current(&scope))
      return false;
    qa_chat_data_memory storage;
    qa_chat_data_memory_init(&storage, has_allocation ? &allocation : NULL);
    if (has_allocation) {
      chat = qa_chat_initial_create(&allocation, (uint32_t)l->initial_chat_count + 1);
      l->initial_chats = xrealloc(l->initial_chats, (l->initial_chat_count + 1) * sizeof *l->initial_chats);
      l->initial_chats[l->initial_chat_count++] = chat;
    }
    qa_chat_data_parser *parser = NULL;
    char *path_text = NULL;
    initial_outcome outcome =
        load_initial_parse(&scope, path, source_name, pass != 0, chat, &storage, &parser, &path_text);
    free(path_text);
    if (outcome == INITIAL_RETIRED || outcome == INITIAL_NOT_CURRENT) {
      qa_chat_data_parser_destroy(parser);
      return false;
    }
    if (outcome == INITIAL_FAILED) {
      if (qa_chat_data_parser_failure(parser) == QA_CHAT_PARSER_INITIAL_NOT_FOUND && scope_current(&scope)) {
        char *filename = chat_text(path);
        report(l, QA_CHAT_SEVERITY_ERROR, QA_CHAT_DIAG_LOAD_ERROR,
               xformat("couldn't find chat %s in %s", qa_chat_data_parser_failure_chat_name(parser), filename),
               filename, NULL);
        free(filename);
      }
      if (scope_current(&scope))
        qa_chat_data_parser_dispose(parser);
      qa_chat_data_parser_destroy(parser);
      return failed_load(&scope, path, name);
    }
    if (!scope_current(&scope)) {
      qa_chat_data_parser_destroy(parser);
      return false;
    }
    if (parser == NULL) {
      char *filename = chat_text(path);
      report(l, QA_CHAT_SEVERITY_ERROR, QA_CHAT_DIAG_LOAD_ERROR, xformat("counldn't load %s", filename), filename, NULL);
      free(filename);
      return failed_load(&scope, path, name);
    }
    qa_chat_data_parser_dispose(parser);
    size = qa_chat_data_memory_byte_length(&storage);
    qa_chat_data_parser_destroy(parser);
  }
  if (chat == NULL)
    return failed_load(&scope, path, name);
  char *chat_name = chat_text(name), *filename = chat_text(path);
  report(l, QA_CHAT_SEVERITY_INFO, QA_CHAT_DIAG_LOADED, xformat("loaded %s from %s", chat_name, filename), filename, NULL);
  free(chat_name);
  free(filename);
  if (!scope_current(&scope))
    return false;
  bool developer = l->options.developer != NULL && l->options.developer(l->options.ctx);
  if (developer && scope_current(&scope))
    check_integrity_chat(l, &scope, chat);
  if (!scope_current(&scope))
    return false;
  if (has_debug)
    report(l, QA_CHAT_SEVERITY_INFO, QA_CHAT_DIAG_LOADED,
           xformat("initial chats loaded in %d msec", qa_js_to_int32(l->options.debug_milliseconds(l->options.ctx) - start_time)),
           "", NULL);
  if (!scope_current(&scope))
    return false;
  state_set_chat(state, chat);
  bool cache = !(l->options.reload_characters != NULL && l->options.reload_characters(l->options.ctx));
  if (!scope_current(&scope))
    return false;
  if (cache) {
    qa_bot_memory_allocation allocation = chat_allocate(l, CHAT_CACHE_BYTES, QA_BOT_MEMORY_HEAP);
    if (!scope_current(&scope))
      return false;
    qa_chat_cached *cached = xcalloc(1, sizeof *cached);
    cached->allocation = allocation;
    cache_ensure(l, (size_t)available + 1);
    l->cache[available] = cached;
    if ((size_t)available + 1 > l->cache_length)
      l->cache_length = (size_t)available + 1;
    uint8_t *bytes = qa_bot_memory_allocation_bytes(&allocation);
    store_u32(bytes, qa_chat_initial_pointer(chat));
    char *bounded = bounded_chat_text(name, 63);
    write_message(qa_bot_memory_allocation_bytes(&allocation) + 68, 64, bounded, 0);
    free(bounded);
    if (!scope_current(&scope))
      return false;
    bounded = bounded_chat_text(path, 63);
    write_message(qa_bot_memory_allocation_bytes(&allocation) + 4, 64, bounded, 0);
    free(bounded);
  }
  return scope_current(&scope);
}

/* ---- console messages ---- */
void qa_bot_chat_library_queue_console_message(qa_bot_chat_library *l, int64_t handle, int64_t type,
                                               qa_chat_text_source input) {
  qa_chat_state *state = get_state(l, handle);
  if (state == NULL)
    return;
  uint32_t message = l->free_console_messages;
  if (message == 0) {
    report(l, QA_CHAT_SEVERITY_ERROR, QA_CHAT_DIAG_MESSAGE_HEAP_FULL, xdup("empty console message heap"), "", NULL);
    return;
  }
  l->free_console_messages = cell_link(l, message, CELL_NEXT);
  if (l->free_console_messages != 0)
    cell_set(l, l->free_console_messages, CELL_PREV, 0);
  int32_t next_handle = (int32_t)((uint32_t)state_i32(state, STATE_HANDLE) + 1);
  if (next_handle <= 0 || next_handle > 8192)
    next_handle = 1;
  state_set_u32(state, STATE_HANDLE, (uint32_t)next_handle);
  store_u32(cell_bytes(l, message), (uint32_t)next_handle);
  uint64_t generation = l->generation_id;
  double time = lib_time(l);
  if (l->generation_id != generation || l->states[handle] != state)
    return;
  uint8_t *bytes = cell_bytes(l, message);
  store_f32(bytes + 4, time);
  store_u32(bytes + 8, (uint32_t)type);
  char *text = bounded_chat_text(input, CHAT_MESSAGE_SIZE);
  copy_chat_text(cell_bytes(l, message), CONSOLE_MESSAGE_BYTES, 12, text, CHAT_MESSAGE_SIZE, NULL);
  free(text);
  cell_set(l, message, CELL_NEXT, 0);
  uint32_t previous = state_console(l, state, STATE_LAST);
  if (previous != 0) {
    cell_set(l, previous, CELL_NEXT, message);
    cell_set(l, message, CELL_PREV, previous);
    state_set_u32(state, STATE_LAST, message);
  } else {
    state_set_u32(state, STATE_LAST, message);
    state_set_u32(state, STATE_FIRST, message);
    cell_set(l, message, CELL_PREV, 0);
  }
  state_set_u32(state, STATE_COUNT, (uint32_t)state_i32(state, STATE_COUNT) + 1);
}
void qa_bot_chat_library_remove_console_message(qa_bot_chat_library *l, int64_t handle,
                                                int64_t message_handle) {
  qa_chat_state *state = get_state(l, handle);
  if (state == NULL)
    return;
  for (uint32_t message = state_console(l, state, STATE_FIRST); message != 0;
       message = cell_link(l, message, CELL_NEXT)) {
    if ((int32_t)load_u32(cell_bytes(l, message)) != message_handle)
      continue;
    uint32_t next = cell_link(l, message, CELL_NEXT), previous = cell_link(l, message, CELL_PREV);
    if (next != 0)
      cell_set(l, next, CELL_PREV, previous);
    else
      state_set_u32(state, STATE_LAST, previous);
    if (previous != 0)
      cell_set(l, previous, CELL_NEXT, next);
    else
      state_set_u32(state, STATE_FIRST, next);
    /* TS freeConsoleMessage */
    if (l->free_console_messages != 0)
      cell_set(l, l->free_console_messages, CELL_PREV, message);
    cell_set(l, message, CELL_PREV, 0);
    cell_set(l, message, CELL_NEXT, l->free_console_messages);
    l->free_console_messages = message;
    state_set_u32(state, STATE_COUNT, (uint32_t)state_i32(state, STATE_COUNT) - 1);
    break;
  }
}
bool qa_bot_chat_library_next_console_message(qa_bot_chat_library *l, int64_t handle,
                                              qa_console_chat_message *out) {
  qa_chat_state *state = get_state(l, handle);
  uint32_t first = state != NULL ? state_console(l, state, STATE_FIRST) : 0;
  if (first == 0)
    return false;
  cell_snapshot(l, first, out);
  return true;
}
int64_t qa_bot_chat_library_num_console_messages(qa_bot_chat_library *l, int64_t handle) {
  qa_chat_state *state = get_state(l, handle);
  return state != NULL ? state_i32(state, STATE_COUNT) : 0;
}

/* ---- names, messages ---- */
void qa_bot_chat_library_set_name(qa_bot_chat_library *l, int64_t handle, qa_chat_text_source name,
                                  bool has_client, int64_t client) {
  qa_chat_state *state = get_state(l, handle);
  if (state == NULL)
    return;
  if (has_client)
    state_set_u32(state, 4, (uint32_t)client);
  state_set_name(state, "");
  char *text = bounded_chat_text(name, 32);
  if (strlen(text) > 31)
    text[31] = '\0';
  state_set_name(state, text);
  free(text);
}
void qa_bot_chat_library_set_gender(qa_bot_chat_library *l, int64_t handle, int64_t gender) {
  qa_chat_state *state = get_state(l, handle);
  if (state != NULL)
    state_set_u32(state, 0, gender == 1 || gender == 2 ? (uint32_t)gender : 0);
}
int64_t qa_bot_chat_library_chat_length(qa_bot_chat_library *l, int64_t handle) {
  qa_chat_state *state = get_state(l, handle);
  return state == NULL ? 0 : (int64_t)strnlen((const char *)state_message(state), CHAT_MESSAGE_SIZE);
}
static void clean_state_message(const qa_chat_state *state) {
  char *text = state_message_text(state);
  remove_tildes(text);
  write_message(state_message(state), CHAT_MESSAGE_SIZE, text, 0);
  free(text);
}
void qa_bot_chat_library_write_chat_message(qa_bot_chat_library *l, int64_t handle,
                                            qa_chat_write_fn write, void *ctx) {
  qa_chat_state *state = get_state(l, handle);
  if (state == NULL)
    return;
  clean_state_message(state);
  char *text = state_message_text(state);
  write(ctx, text);
  free(text);
  state_message(state)[0] = 0;
}
typedef struct chat_message_output {
  int64_t size;
  char *result;
} chat_message_output;
static void chat_message_write(void *ctx, const char *text) {
  chat_message_output *output = ctx;
  if (output->size < 1)
    qa_range_error("chat output size must be positive");
  size_t length = strlen(text);
  if ((uint64_t)length > (uint64_t)output->size - 1)
    length = (size_t)output->size - 1;
  free(output->result);
  output->result = xdup_n(text, length);
}
char *qa_bot_chat_library_get_chat_message(qa_bot_chat_library *l, int64_t handle, int64_t size) {
  chat_message_output output = {size, xdup("")};
  qa_bot_chat_library_write_chat_message(l, handle, chat_message_write, &output);
  return output.result;
}
typedef struct enter_chat_steps {
  qa_bot_chat_library *l;
  int64_t handle, client_to, send_to, source_client;
  bool has_source_client;
  bool started;
  qa_chat_state *state;
  char *command;
  qa_call_steps inner;
} enter_chat_steps;
static void enter_chat_release(void *ctx) {
  enter_chat_steps *steps = ctx;
  free(steps->command);
  free(steps);
}
static qa_call_step_result enter_chat_next(void *ctx, const char *thrown) {
  enter_chat_steps *steps = ctx;
  qa_bot_chat_library *l = steps->l;
  qa_call_step_result done = {true, NULL, {NULL, NULL}, NULL};
  if (!steps->started) {
    if (thrown != NULL) {
      done.done = false;
      done.thrown = thrown;
      return done;
    }
    steps->started = true;
    qa_chat_state *state = get_state(l, steps->handle);
    if (state == NULL || state_message(state)[0] == 0)
      return done;
    steps->state = state;
    clean_state_message(state);
    if (l->options.test_initial_chats != NULL && l->options.test_initial_chats(l->options.ctx)) {
      report(l, QA_CHAT_SEVERITY_INFO, QA_CHAT_DIAG_TEST_OUTPUT, state_message_text(state), "", NULL);
      state_message(state)[0] = 0;
      return done;
    }
    char *message = state_message_text(state);
    steps->command = steps->send_to == QA_CHAT_TEAM   ? xformat("say_team %s", message)
                     : steps->send_to == QA_CHAT_TELL ? xformat("tell %lld %s", (long long)steps->client_to, message)
                                                      : xformat("say %s", message);
    free(message);
    int64_t client = steps->has_source_client ? steps->source_client : state_i32(state, 4);
    steps->inner = l->host.client_command(l->host.ctx, client, steps->command);
  }
  qa_call_step_result step = qa_call_steps_forward(&steps->inner, thrown);
  if (step.thrown != NULL || !step.done)
    return step;
  state_message(steps->state)[0] = 0;
  return done;
}
qa_call_steps qa_bot_chat_library_enter_chat_calls(qa_bot_chat_library *l, int64_t handle,
                                                   int64_t client_to, int64_t send_to,
                                                   bool has_source_client, int64_t source_client) {
  enter_chat_steps *steps = xcalloc(1, sizeof *steps);
  steps->l = l;
  steps->handle = handle;
  steps->client_to = client_to;
  steps->send_to = send_to;
  steps->has_source_client = has_source_client;
  steps->source_client = source_client;
  qa_call_steps result = {enter_chat_next, steps, enter_chat_release, NULL};
  return result;
}
void qa_bot_chat_library_enter_chat(qa_bot_chat_library *l, int64_t handle, int64_t client_to,
                                    int64_t send_to, bool has_source_client, int64_t source_client) {
  qa_finish_calls(qa_bot_chat_library_enter_chat_calls(l, handle, client_to, send_to, has_source_client, source_client));
}

/* ---- matching ---- */
bool qa_bot_chat_library_find_match_into(qa_bot_chat_library *l, qa_chat_text_source input,
                                         int64_t context, qa_chat_match_buffer *buffer) {
  /* The source copy leaves a 256-byte input unterminated and reads on past it; the port keeps the terminator in the last byte. */
  char *text = bounded_chat_text(input, CHAT_MESSAGE_SIZE - 1);
  copy_chat_text(buffer->bytes, buffer->length, 0, text, CHAT_MESSAGE_SIZE, buffer->writes);
  free(text);
  size_t length = strlen(chat_c_string(buffer->bytes, buffer->length));
  while (length > 0 && chat_byte(buffer->bytes, buffer->length, length - 1) == 10) {
    uint8_t *destination = chat_span(buffer->bytes, buffer->length, --length, 1);
    if (buffer->writes == NULL)
      destination[0] = 0;
    else
      buffer->writes->set_byte(buffer->writes->ctx, destination, 1, 0, 0);
  }
  const char *match = chat_c_string(buffer->bytes, buffer->length);
  uint32_t mask = (uint32_t)context;
  for (const qa_chat_cell *template_cell = l->matches; template_cell != NULL;
       template_cell = qa_chat_template_next(template_cell)) {
    if ((qa_chat_template_context(template_cell) & mask) == 0)
      continue;
    for (int i = 0; i < 8; i++)
      buffer->variables[i].offset = -1;
    if (strings_match(qa_chat_template_first(template_cell), match, buffer->variables)) {
      buffer->type = qa_chat_template_type(template_cell);
      buffer->subtype = qa_chat_template_subtype(template_cell);
      return true;
    }
  }
  return false;
}
bool qa_bot_chat_library_find_match(qa_bot_chat_library *l, const char *input, int64_t context,
                                    qa_chat_match *out) {
  uint8_t bytes[CHAT_MESSAGE_SIZE];
  qa_chat_match_buffer buffer;
  memset(&buffer, 0, sizeof buffer);
  buffer.bytes = bytes;
  buffer.length = sizeof bytes;
  if (!qa_bot_chat_library_find_match_into(l, qa_chat_text(input), context, &buffer))
    return false;
  memcpy(out->text, chat_c_string(bytes, sizeof bytes), strlen((const char *)bytes) + 1);
  out->type = buffer.type;
  out->subtype = buffer.subtype;
  for (int i = 0; i < 8; i++) {
    out->variables[i].present = buffer.variables[i].offset >= 0;
    out->variables[i].offset = buffer.variables[i].offset;
    out->variables[i].length = buffer.variables[i].length;
  }
  return true;
}
void qa_bot_chat_library_write_match_variable(qa_bot_chat_library *l, qa_chat_variable_at_fn at,
                                              int64_t index, int64_t size,
                                              void (*write)(void *ctx, int64_t offset, int64_t capacity),
                                              void (*clear)(void *ctx), void *ctx) {
  if (index < 0 || index >= 8) {
    report(l, QA_CHAT_SEVERITY_FATAL, QA_CHAT_DIAG_INVALID_VARIABLE,
           xdup("BotMatchVariable: variable out of range"), "", NULL);
    clear(ctx);
    return;
  }
  qa_chat_match_variable variable = at(ctx, index);
  if (variable.offset < 0) {
    clear(ctx);
    return;
  }
  if (variable.length < size)
    size = variable.length + 1;
  write(ctx, variable.offset, size);
}
typedef struct match_variable_output {
  const qa_chat_match *match;
  char *result;
} match_variable_output;
static qa_chat_match_variable match_variable_at(void *ctx, int64_t index) {
  const match_variable_output *output = ctx;
  const qa_chat_capture *variable = &output->match->variables[index];
  qa_chat_match_variable value = {variable->present ? variable->offset : -1, variable->present ? variable->length : 0};
  return value;
}
static void match_variable_write(void *ctx, int64_t offset, int64_t capacity) {
  match_variable_output *output = ctx;
  if (capacity < 1)
    qa_range_error("invalid match variable buffer size");
  free(output->result);
  output->result = js_slice(output->match->text, offset, offset + capacity - 1);
}
static void match_variable_clear(void *ctx) {
  match_variable_output *output = ctx;
  free(output->result);
  output->result = xdup("");
}
char *qa_bot_chat_library_match_variable(qa_bot_chat_library *l, const qa_chat_match *match,
                                         int64_t index, int64_t size) {
  match_variable_output output = {match, xdup("")};
  qa_bot_chat_library_write_match_variable(l, match_variable_at, index, size, match_variable_write,
                                           match_variable_clear, &output);
  return output.result;
}
char *qa_bot_chat_library_replace_synonyms(qa_bot_chat_library *l, const char *input, int64_t context) {
  char *text = xdup(input);
  for (size_t i = 0; i < l->synonyms.count; i++) {
    if ((qa_stored_synonym_group_context(&l->synonyms, i) & (uint32_t)context) == 0)
      continue;
    for (size_t j = 1; j < l->synonyms.groups[i].entry_count; j++)
      text = replace_words(text, qa_stored_synonym_text(&l->synonyms, i, j),
                           qa_stored_synonym_text(&l->synonyms, i, 0), NULL, NULL);
  }
  return text;
}
typedef struct in_place_publish {
  uint8_t *bytes;
  size_t size;
  const qa_chat_buffer_writes *writes;
} in_place_publish;
static void in_place_write(void *ctx, int64_t offset, const char *tail, const char *replacement) {
  const in_place_publish *publish = ctx;
  size_t replacement_length = strlen(replacement), tail_length = strlen(tail);
  copy_chat_text(publish->bytes, publish->size, (size_t)offset + replacement_length, tail, tail_length + 1, publish->writes);
  copy_chat_text(publish->bytes, publish->size, (size_t)offset, replacement, replacement_length, publish->writes);
}
void qa_bot_chat_library_replace_synonyms_in_place(qa_bot_chat_library *l, qa_chat_bytes_fn input,
                                                   void *input_ctx, int64_t context,
                                                   const qa_chat_buffer_writes *writes) {
  for (size_t i = 0; i < l->synonyms.count; i++) {
    if ((qa_stored_synonym_group_context(&l->synonyms, i) & (uint32_t)context) == 0)
      continue;
    for (size_t j = 1; j < l->synonyms.groups[i].entry_count; j++) {
      size_t size = 0;
      uint8_t *bytes = input(input_ctx, &size);
      in_place_publish publish = {bytes, size, writes};
      char *text = xdup(chat_c_string(bytes, size));
      text = replace_words(text, qa_stored_synonym_text(&l->synonyms, i, j),
                           qa_stored_synonym_text(&l->synonyms, i, 0), in_place_write, &publish);
      free(text);
    }
  }
}
char *qa_bot_chat_library_replace_weighted_synonyms(qa_bot_chat_library *l, const char *input,
                                                    int64_t context) {
  char *text = xdup(input);
  for (size_t i = 0; i < l->synonyms.count; i++) {
    if ((qa_stored_synonym_group_context(&l->synonyms, i) & (uint32_t)context) == 0)
      continue;
    double left = source_random(&l->host.random);
    double right = qa_stored_synonym_group_total_weight(&l->synonyms, i);
    double weight = fround(left * right);
    if (weight == 0)
      continue;
    double current = 0;
    size_t entries = l->synonyms.groups[i].entry_count, replacement = entries;
    for (size_t j = 0; j < entries; j++) {
      current = fround(current + qa_stored_synonym_weight(&l->synonyms, i, j));
      if (weight < current) {
        replacement = j;
        break;
      }
    }
    if (replacement == entries)
      continue;
    for (size_t j = 0; j < entries; j++)
      if (j != replacement)
        text = replace_words(text, qa_stored_synonym_text(&l->synonyms, i, j),
                             qa_stored_synonym_text(&l->synonyms, i, replacement), NULL, NULL);
  }
  return text;
}
/* Signed-char source comparison: bytes above 0x7f count as whitespace. */
static bool reply_space(char c) { return (signed char)c <= 32; }
char *qa_bot_chat_library_replace_reply_synonyms(qa_bot_chat_library *l, const char *input,
                                                 int64_t context) {
  char *text = xdup(input);
  size_t pointer = 0;
  while (pointer < strlen(text)) {
    while (text[pointer] != '\0' && reply_space(text[pointer]))
      pointer++;
    if (text[pointer] == '\0')
      break;
    bool replaced = false;
    for (size_t i = 0; i < l->synonyms.count && !replaced; i++) {
      if ((qa_stored_synonym_group_context(&l->synonyms, i) & (uint32_t)context) == 0)
        continue;
      for (size_t j = 1; j < l->synonyms.groups[i].entry_count; j++) {
        const char *synonym = qa_stored_synonym_text(&l->synonyms, i, j);
        if (word_index(text, synonym, (int64_t)pointer) != (int64_t)pointer)
          continue;
        const char *replacement = qa_stored_synonym_text(&l->synonyms, i, 0);
        if (word_index(text, replacement, (int64_t)pointer) == (int64_t)pointer)
          continue;
        char *next = xformat("%.*s%s%s", (int)pointer, text, replacement, text + pointer + strlen(synonym));
        free(text);
        text = next;
        replaced = true;
        break;
      }
    }
    while (text[pointer] != '\0' && !reply_space(text[pointer]))
      pointer++;
  }
  return text;
}
const char *qa_bot_chat_library_random_string(qa_bot_chat_library *l, const char *name) {
  for (size_t i = 0; i < l->randoms.count; i++) {
    if (strcmp(qa_stored_random_name(&l->randoms, i), name) != 0)
      continue;
    double left = source_random(&l->host.random);
    double right = qa_stored_random_num_messages(&l->randoms, i);
    int64_t index = (int64_t)trunc(fround(left * right));
    const char *message = qa_stored_random_message(&l->randoms, i, index);
    if (message != NULL)
      return message;
  }
  return NULL;
}

/* ---- expansion ---- */
typedef struct raw_match {
  char text[CHAT_MESSAGE_SIZE];
  qa_chat_match_variable variables[8];
} raw_match;
static void append_variables(raw_match *match, const qa_chat_text_source *variables) {
  if (variables == NULL)
    return;
  for (int index = 0; index < 8; index++) {
    if (qa_chat_text_is_null(variables[index]))
      continue;
    char *text = chat_text(variables[index]);
    size_t length = strlen(match->text), extra = strlen(text);
    qa_chat_match_variable *variable = capture(match->variables, index);
    variable->offset = (int8_t)(uint8_t)length;
    variable->length = (int64_t)extra;
    if (length + extra >= CHAT_MESSAGE_SIZE)
      qa_range_error("chat text exceeds the source 256-byte buffer");
    memcpy(match->text + length, text, extra + 1);
    free(text);
  }
}
static bool expand(qa_bot_chat_library *l, const qa_chat_state *state, const char *source, int64_t mcontext,
                   raw_match *match, int64_t vcontext, bool reply) {
  size_t pointer = 0, source_length = strlen(source), length = 0;
  bool expanded = false;
  while (pointer < source_length) {
    if (source[pointer] != ESCAPE_CHAR) {
      if (length >= CHAT_MESSAGE_SIZE - 1) {
        report(l, QA_CHAT_SEVERITY_ERROR, QA_CHAT_DIAG_EXPANSION_ERROR, xdup("expanded message exceeds the source buffer"), "", NULL);
        return false;
      }
      state_message(state)[length++] = (uint8_t)source[pointer++];
      continue;
    }
    char kind = source[++pointer]; /* the NUL stands for JS charAt("") at the end */
    if (kind != 'v' && kind != 'r') {
      report(l, QA_CHAT_SEVERITY_FATAL, QA_CHAT_DIAG_EXPANSION_ERROR,
             xformat("BotConstructChat: message \"%s\" invalid escape char", source), "", NULL);
      continue;
    }
    size_t start = ++pointer;
    while (pointer < source_length && source[pointer] != ESCAPE_CHAR)
      pointer++;
    char *key = xdup_n(source + start, pointer - start);
    if (pointer < source_length)
      pointer++;
    char *value;
    if (kind == 'v') {
      double index = js_number(key);
      if (!isfinite(index) || trunc(index) != index || index < 0 || index >= 8) {
        report(l, QA_CHAT_SEVERITY_ERROR, QA_CHAT_DIAG_EXPANSION_ERROR, xformat("message variable %s outside 0..7", key), "", NULL);
        free(key);
        return false;
      }
      qa_chat_match_variable *variable = capture(match->variables, (int64_t)index);
      if (variable->offset < 0) {
        free(key);
        continue;
      }
      char *slice = js_slice(match->text, variable->offset, variable->offset + variable->length);
      value = reply ? qa_bot_chat_library_replace_reply_synonyms(l, slice, vcontext)
                    : qa_bot_chat_library_replace_synonyms(l, slice, vcontext);
      free(slice);
    } else {
      const char *random = qa_bot_chat_library_random_string(l, key);
      if (random == NULL) {
        report(l, QA_CHAT_SEVERITY_ERROR, QA_CHAT_DIAG_EXPANSION_ERROR,
               xformat("BotConstructChat: unknown random string %s", key), "", NULL);
        free(key);
        return false;
      }
      value = xdup(random);
      expanded = true;
    }
    free(key);
    size_t value_length = strlen(value);
    if (length + value_length >= CHAT_MESSAGE_SIZE) {
      report(l, QA_CHAT_SEVERITY_ERROR, QA_CHAT_DIAG_EXPANSION_ERROR,
             kind == 'v' ? xformat("BotConstructChat: message %s too long", source)
                         : xformat("BotConstructChat: message \"%s\" too long", source),
             "", NULL);
      free(value);
      return false;
    }
    write_message(state_message(state), CHAT_MESSAGE_SIZE, value, length);
    length += value_length;
    free(value);
  }
  state_message(state)[length] = 0;
  char *message = state_message_text(state);
  char *weighted = qa_bot_chat_library_replace_weighted_synonyms(l, message, mcontext);
  free(message);
  if (strlen(weighted) >= CHAT_MESSAGE_SIZE) {
    report(l, QA_CHAT_SEVERITY_ERROR, QA_CHAT_DIAG_EXPANSION_ERROR, xdup("weighted message exceeds the source buffer"), "", NULL);
    free(weighted);
    return false;
  }
  write_message(state_message(state), CHAT_MESSAGE_SIZE, weighted, 0);
  free(weighted);
  return expanded;
}
static void construct(qa_bot_chat_library *l, const qa_chat_state *state, const char *text, int64_t mcontext,
                      raw_match *match, int64_t vcontext, bool reply) {
  char *source = xdup(text);
  for (int count = 0; count < 10; count++) {
    if (!expand(l, state, source, mcontext, match, vcontext, reply)) {
      free(source);
      return;
    }
    free(source);
    source = state_message_text(state);
  }
  free(source);
  report(l, QA_CHAT_SEVERITY_WARNING, QA_CHAT_DIAG_EXPANSION_LIMIT, xdup("too many expansions in chat message"), "", NULL);
  report(l, QA_CHAT_SEVERITY_WARNING, QA_CHAT_DIAG_EXPANSION_LIMIT, state_message_text(state), "", NULL);
}
int64_t qa_bot_chat_library_num_initial_chats(qa_bot_chat_library *l, int64_t handle, qa_chat_text_source name) {
  qa_chat_state *state = get_state(l, handle);
  qa_chat_initial *chat = state != NULL ? state_chat(l, state) : NULL;
  if (chat == NULL)
    return 0;
  for (uint32_t type = qa_chat_initial_first_type(chat); type != 0; type = qa_chat_initial_type_next(chat, type)) {
    if (!qa_chat_text_equals_source(qa_chat_initial_type_name(chat, type), name, false) || qa_chat_text_is_null(name))
      continue;
    if (l->options.test_initial_chats != NULL && l->options.test_initial_chats(l->options.ctx)) {
      char *text = chat_text(name);
      report(l, QA_CHAT_SEVERITY_INFO, QA_CHAT_DIAG_TEST_OUTPUT,
             xformat("%s has %d chat lines", text, qa_chat_initial_type_num_messages(chat, type)), "", NULL);
      free(text);
      report(l, QA_CHAT_SEVERITY_INFO, QA_CHAT_DIAG_TEST_OUTPUT, xdup("-------------------"), "", NULL);
    }
    return qa_chat_initial_type_num_messages(chat, type);
  }
  return 0;
}
static void missing_initial_chat(qa_bot_chat_library *l, qa_chat_text_source name) {
  if (l->options.debug_milliseconds == NULL)
    return;
  if (qa_chat_text_is_null(name))
    qa_range_error("BotInitialChat: DEBUG print reads a null source type string");
  char *text = chat_text(name);
  report(l, QA_CHAT_SEVERITY_INFO, QA_CHAT_DIAG_TEST_OUTPUT, xformat("no chat messages of type %s", text), "", NULL);
  free(text);
}
void qa_bot_chat_library_initial_chat(qa_bot_chat_library *l, int64_t handle, qa_chat_text_source name,
                                      int64_t context, const qa_chat_text_source *variables) {
  qa_chat_state *state = get_state(l, handle);
  if (state == NULL)
    return;
  qa_chat_initial *chat = state_chat(l, state);
  if (chat == NULL)
    return;
  uint32_t type = qa_chat_initial_first_type(chat);
  while (type != 0 && !qa_chat_text_equals_source(qa_chat_initial_type_name(chat, type), name, false))
    type = qa_chat_initial_type_next(chat, type);
  if (type == 0) {
    missing_initial_chat(l, name);
    return;
  }
  int64_t eligible = 0;
  for (uint32_t message = qa_chat_initial_type_first_message(chat, type); message != 0;
       message = qa_chat_initial_message_next(chat, message))
    if (qa_chat_initial_message_time(chat, message) <= lib_time(l))
      eligible++;
  uint32_t selected = 0;
  if (eligible == 0) {
    double best_time = 0;
    for (uint32_t message = qa_chat_initial_type_first_message(chat, type); message != 0;
         message = qa_chat_initial_message_next(chat, message)) {
      double time = qa_chat_initial_message_time(chat, message);
      if (best_time == 0 || time < best_time) {
        selected = message;
        best_time = time;
      }
    }
  } else {
    int64_t index = (int64_t)trunc(fround(source_random(&l->host.random) * (double)eligible));
    for (uint32_t message = qa_chat_initial_type_first_message(chat, type); message != 0;
         message = qa_chat_initial_message_next(chat, message)) {
      if (qa_chat_initial_message_time(chat, message) > lib_time(l))
        continue;
      if (--index < 0) {
        selected = message;
        qa_chat_initial_message_set_time(chat, message, fround(lib_time(l) + 20));
        break;
      }
    }
  }
  if (selected == 0) {
    missing_initial_chat(l, name);
    return;
  }
  raw_match match;
  memset(&match, 0, sizeof match);
  append_variables(&match, variables);
  char *text = xdup(qa_chat_initial_message_text(chat, selected));
  construct(l, state, text, context, &match, 0, false);
  free(text);
}
static bool reply_key(const qa_chat_cell *key, const qa_chat_state *state, qa_chat_text_source input,
                      raw_match *match) {
  int32_t flags = qa_chat_key_flags(key);
  if ((flags & 4) != 0 || (flags & 32) != 0) {
    char *name = read_message(state_bytes(state) + STATE_NAME, 32);
    qa_chat_text_source text = (flags & 4) != 0 ? input : qa_chat_text(qa_chat_key_text(key));
    bool found = qa_chat_string_contains(text, qa_chat_text(name), false) >= 0;
    free(name);
    return found;
  }
  if ((flags & 64) != 0)
    return state_i32(state, 0) == 1;
  if ((flags & 128) != 0)
    return state_i32(state, 0) == 2;
  if ((flags & 256) != 0)
    return state_i32(state, 0) == 0;
  if ((flags & 16) != 0)
    return strings_match(qa_chat_key_match(key), match->text, match->variables);
  if ((flags & 8) != 0) {
    char *text = chat_text(input);
    bool found = word_index(text, qa_chat_key_text(key), 0) >= 0;
    free(text);
    return found;
  }
  return false;
}
bool qa_bot_chat_library_reply_chat(qa_bot_chat_library *l, int64_t handle, qa_chat_text_source input,
                                    int64_t message_context, int64_t variable_context,
                                    const qa_chat_text_source *variables) {
  qa_chat_state *state = get_state(l, handle);
  if (state == NULL)
    return false;
  char *message = chat_text(input);
  check_message_text(message);
  raw_match match;
  memset(&match, 0, sizeof match);
  memcpy(match.text, message, strlen(message) + 1);
  free(message);
  const qa_chat_cell *best_message = NULL, *best_reply = NULL;
  raw_match best_match;
  double priority = -1;
  for (const qa_chat_cell *reply = l->replies; reply != NULL; reply = qa_chat_reply_next(reply)) {
    bool found = false;
    for (const qa_chat_cell *key = qa_chat_reply_keys(reply); key != NULL; key = qa_chat_key_next(key)) {
      bool result = reply_key(key, state, input, &match);
      int32_t flags = qa_chat_key_flags(key);
      if ((flags & 1) != 0) {
        if (!result) {
          found = false;
          break;
        }
      } else if ((flags & 2) != 0) {
        if (result) {
          found = false;
          break;
        }
      } else if (result) {
        found = true;
      }
    }
    if (!found || qa_chat_reply_priority(reply) <= priority)
      continue;
    int64_t eligible = 0;
    for (const qa_chat_cell *line = qa_chat_reply_first_message(reply); line != NULL;
         line = qa_chat_reply_message_next(line))
      if (qa_chat_reply_message_time(line) <= lib_time(l))
        eligible++;
    int64_t selected = (int64_t)trunc(fround(source_random(&l->host.random) * (double)eligible));
    for (const qa_chat_cell *line = qa_chat_reply_first_message(reply); line != NULL;
         line = qa_chat_reply_message_next(line)) {
      if (--selected < 0) {
        best_message = line;
        best_reply = reply;
        best_match = match;
        priority = trunc(qa_chat_reply_priority(reply));
        break;
      }
      /* TS re-reads the time here with no effect on the walk. */
      (void)lib_time(l);
    }
  }
  if (best_message == NULL)
    return false;
  append_variables(&best_match, variables);
  if (l->options.test_reply_chats != NULL && l->options.test_reply_chats(l->options.ctx)) {
    for (const qa_chat_cell *line = qa_chat_reply_first_message(best_reply); line != NULL;
         line = qa_chat_reply_message_next(line)) {
      construct(l, state, qa_chat_reply_message_text(line), message_context, &best_match, variable_context, true);
      char *text = state_message_text(state);
      remove_tildes(text);
      write_message(state_message(state), CHAT_MESSAGE_SIZE, text, 0);
      report(l, QA_CHAT_SEVERITY_INFO, QA_CHAT_DIAG_TEST_OUTPUT, text, "", NULL);
    }
  } else {
    qa_chat_reply_message_set_time(best_message, fround(lib_time(l) + 20));
    construct(l, state, qa_chat_reply_message_text(best_message), message_context, &best_match, variable_context, true);
  }
  return true;
}

/* ---- checkpoint ---- */
static qa_save_value *nullable_reference(qa_bot_memory_capture *capture, bool present,
                                         const qa_bot_memory_allocation *allocation) {
  if (!present)
    return qa_save_value_null();
  qa_bot_memory_allocation copy = *allocation;
  return qa_save_value_number((double)qa_bot_memory_capture_reference(capture, &copy));
}
qa_save_value *qa_bot_chat_library_checkpoint(qa_bot_chat_library *l, qa_bot_memory_capture *capture) {
  qa_save_value *image = checked(qa_save_value_object());
  set_field(image, "revision", qa_save_value_number((double)l->setup_revision));
  qa_save_value *issues = checked(qa_save_value_array());
  for (size_t i = 0; i < l->issue_count; i++) {
    const qa_chat_diagnostic *issue = &l->issues[i];
    qa_save_value *entry = checked(qa_save_value_object());
    set_field(entry, "severity", qa_save_value_string(qa_chat_severity_name(issue->severity)));
    set_field(entry, "code", qa_save_value_string(qa_chat_diagnostic_code_name(issue->code)));
    set_field(entry, "source", qa_save_value_string(issue->source));
    set_field(entry, "message", qa_save_value_string(issue->message));
    if (issue->has_location) {
      qa_save_value *location = checked(qa_save_value_object());
      set_field(location, "path", qa_save_value_string(issue->location.path));
      set_field(location, "line", qa_save_value_number((double)issue->location.line));
      set_field(location, "column", qa_save_value_number((double)issue->location.column));
      set_field(entry, "location", location);
    } else {
      set_field(entry, "location", qa_save_value_null());
    }
    push_item(issues, entry);
  }
  set_field(image, "issues", issues);
  qa_save_value *cache = checked(qa_save_value_array());
  for (size_t i = 0; i < l->cache_length; i++)
    push_item(cache, nullable_reference(capture, l->cache[i] != NULL, l->cache[i] != NULL ? &l->cache[i]->allocation : NULL));
  set_field(image, "cache", cache);
  qa_save_value *states = checked(qa_save_value_array());
  for (size_t i = 0; i < 65; i++) {
    if (l->states[i] == NULL) {
      push_item(states, qa_save_value_null());
      continue;
    }
    qa_save_value *entry = checked(qa_save_value_object());
    set_field(entry, "allocation", nullable_reference(capture, true, &l->states[i]->allocation));
    set_field(entry, "revision", qa_save_value_number((double)l->states[i]->revision));
    push_item(states, entry);
  }
  set_field(image, "states", states);
  qa_save_value *synonyms = checked(qa_save_value_array());
  for (size_t i = 0; i < l->synonyms.count; i++) {
    qa_save_value *group = checked(qa_save_value_array());
    for (size_t j = 0; j < l->synonyms.groups[i].entry_count; j++)
      push_item(group, qa_save_value_number((double)strlen(qa_stored_synonym_text(&l->synonyms, i, j))));
    push_item(synonyms, group);
  }
  set_field(image, "synonyms", synonyms);
  qa_save_value *randoms = checked(qa_save_value_array());
  for (size_t i = 0; i < l->randoms.count; i++) {
    qa_save_value *entry = checked(qa_save_value_object());
    set_field(entry, "nameBytes", qa_save_value_number((double)strlen(qa_stored_random_name(&l->randoms, i))));
    qa_save_value *bytes = checked(qa_save_value_array());
    size_t messages = l->randoms.lists[i].message_count;
    for (size_t j = messages; j > 0; j--)
      push_item(bytes, qa_save_value_number((double)strlen(qa_stored_random_message(&l->randoms, i, (int64_t)j - 1))));
    set_field(entry, "messageBytes", bytes);
    push_item(randoms, entry);
  }
  set_field(image, "randoms", randoms);
  set_field(image, "synonymsAllocation", nullable_reference(capture, l->has_synonyms_allocation, &l->synonyms_allocation));
  set_field(image, "randomsAllocation", nullable_reference(capture, l->has_randoms_allocation, &l->randoms_allocation));
  set_field(image, "matches", l->matches != NULL ? qa_chat_cell_checkpoint_graph(l->matches, capture) : qa_save_value_null());
  set_field(image, "replies", l->replies != NULL ? qa_chat_cell_checkpoint_graph(l->replies, capture) : qa_save_value_null());
  set_field(image, "consoleMessageHeap", nullable_reference(capture, l->has_console_heap, &l->console_heap));
  set_field(image, "freeConsoleMessages", qa_save_value_number((double)l->free_console_messages));
  qa_save_value *cells = checked(qa_save_value_array());
  for (size_t i = 0; i < l->console_cell_count; i++) {
    qa_save_value *entry = checked(qa_save_value_object());
    set_field(entry, "pointer", qa_save_value_number((double)l->console_cells[i].pointer));
    set_field(entry, "allocation", nullable_reference(capture, true, &l->console_cells[i].allocation));
    set_field(entry, "offset", qa_save_value_number((double)l->console_cells[i].offset));
    push_item(cells, entry);
  }
  set_field(image, "consoleMessageCells", cells);
  qa_save_value *chats = checked(qa_save_value_array());
  for (size_t i = 0; i < l->initial_chat_count; i++)
    push_item(chats, qa_chat_initial_checkpoint(l->initial_chats[i], capture));
  set_field(image, "initialChats", chats);
  return image;
}

static void field_of(const qa_save_reader *reader, const char *name, qa_save_reader *out) {
  if (!qa_save_reader_field(reader, name, out))
    qa_range_error("out of memory");
}
static void item_of(const qa_save_reader *reader, size_t index, qa_save_reader *out) {
  if (!qa_save_reader_item(reader, index, out))
    qa_range_error("out of memory");
}
static double field_integer(const qa_save_reader *reader, const char *name, double minimum) {
  qa_save_reader field;
  field_of(reader, name, &field);
  double value = qa_save_reader_integer_min(&field, minimum);
  qa_save_reader_destroy(&field);
  return value;
}
static bool field_allocation(const qa_save_reader *reader, const char *name,
                             qa_bot_memory_restored *restored, qa_bot_memory_allocation *out) {
  qa_save_reader field;
  field_of(reader, name, &field);
  bool present = !qa_save_reader_is_null(&field);
  if (present)
    *out = qa_bot_memory_restored_allocation(restored, (int64_t)qa_save_reader_integer_min(&field, 0));
  qa_save_reader_destroy(&field);
  return present;
}
/* TS restore's visit(): a list longer than the cell map must repeat a cell. */
static size_t console_visit(const qa_bot_chat_library *l, uint32_t first) {
  size_t seen = 0;
  for (uint32_t cell = first; cell != 0; cell = cell_link(l, cell, CELL_NEXT)) {
    if (++seen > l->console_cell_count)
      qa_range_error("Saved chat console list cycles");
    (void)cell_link(l, cell, CELL_PREV);
  }
  return seen;
}
static void restore_issues(qa_bot_chat_library *l, const qa_save_reader *reader) {
  static const char *const severities[] = {"info", "warning", "error", "fatal"};
  qa_save_reader list, entry, field, location;
  field_of(reader, "issues", &list);
  for (size_t i = 0, n = qa_save_reader_list_count(&list); i < n; i++) {
    item_of(&list, i, &entry);
    qa_chat_diagnostic issue;
    memset(&issue, 0, sizeof issue);
    field_of(&entry, "severity", &field);
    const char *severity = qa_save_reader_choice_string(&field, severities, 4);
    while (strcmp(qa_chat_severity_name(issue.severity), severity) != 0)
      issue.severity++;
    qa_save_reader_destroy(&field);
    field_of(&entry, "code", &field);
    const char *code = qa_save_reader_choice_string(&field, diagnostic_code_names, 14);
    while (strcmp(diagnostic_code_names[issue.code], code) != 0)
      issue.code++;
    qa_save_reader_destroy(&field);
    field_of(&entry, "source", &field);
    issue.source = xdup(qa_save_reader_string(&field));
    qa_save_reader_destroy(&field);
    field_of(&entry, "message", &field);
    issue.message = xdup(qa_save_reader_string(&field));
    qa_save_reader_destroy(&field);
    field_of(&entry, "location", &location);
    if (!qa_save_reader_is_null(&location)) {
      issue.has_location = true;
      field_of(&location, "path", &field);
      issue.location.path = xdup(qa_save_reader_string(&field));
      qa_save_reader_destroy(&field);
      field_of(&location, "line", &field);
      issue.location.line = (int64_t)qa_save_reader_integer(&field);
      qa_save_reader_destroy(&field);
      field_of(&location, "column", &field);
      issue.location.column = (int64_t)qa_save_reader_integer(&field);
      qa_save_reader_destroy(&field);
    }
    qa_save_reader_destroy(&location);
    qa_save_reader_destroy(&entry);
    l->issues = xrealloc(l->issues, (l->issue_count + 1) * sizeof *l->issues);
    l->issues[l->issue_count++] = issue;
  }
  qa_save_reader_destroy(&list);
}
void qa_bot_chat_library_restore(qa_bot_chat_library *l, const qa_save_value *value,
                                 qa_bot_memory_restored *restored) {
  qa_save_reader reader, list, entry;
  if (!qa_save_reader_init(&reader, value, "bot.chat"))
    qa_range_error("out of memory");
  double revision = field_integer(&reader, "revision", 0);
  bool any_state = false;
  for (size_t i = 0; i < 65; i++)
    any_state = any_state || l->states[i] != NULL;
  field_of(&reader, "states", &list);
  size_t state_count = qa_save_reader_list_count(&list);
  bool first_state_null = false;
  if (state_count > 0) {
    item_of(&list, 0, &entry);
    first_state_null = qa_save_reader_is_null(&entry);
    qa_save_reader_destroy(&entry);
  }
  if (any_state || l->initial_chat_count != 0 || l->console_cell_count != 0 || state_count != 65 ||
      !first_state_null || revision > (double)QA_MAX_SAFE_INTEGER)
    qa_range_error("Invalid bot chat restoration");
  qa_save_reader chats;
  field_of(&reader, "initialChats", &chats);
  for (size_t i = 0, n = qa_save_reader_list_count(&chats); i < n; i++) {
    item_of(&chats, i, &entry);
    qa_chat_initial *chat = qa_chat_initial_restore(entry.value, restored);
    qa_save_reader_destroy(&entry);
    for (size_t j = 0; j < l->initial_chat_count; j++)
      if (qa_chat_initial_pointer(l->initial_chats[j]) == qa_chat_initial_pointer(chat))
        qa_range_error("Duplicate saved initial chat pointer");
    l->initial_chats = xrealloc(l->initial_chats, (l->initial_chat_count + 1) * sizeof *l->initial_chats);
    l->initial_chats[l->initial_chat_count++] = chat;
  }
  qa_save_reader_destroy(&chats);
  qa_save_reader cells;
  field_of(&reader, "consoleMessageCells", &cells);
  bool sequential = true;
  for (size_t i = 0, n = qa_save_reader_list_count(&cells); i < n; i++) {
    item_of(&cells, i, &entry);
    double pointer = field_integer(&entry, "pointer", 1);
    double reference = field_integer(&entry, "allocation", 0);
    double offset = field_integer(&entry, "offset", 0);
    qa_save_reader_destroy(&entry);
    qa_bot_memory_allocation allocation = qa_bot_memory_restored_allocation(restored, (int64_t)reference);
    if (pointer < 1 || pointer > (double)UINT32_MAX || offset < 0 ||
        offset + CONSOLE_MESSAGE_BYTES > (double)allocation.length)
      qa_range_error("Invalid saved chat console cell");
    if ((uint32_t)pointer != l->console_cell_count + 1 || !sequential) {
      sequential = false;
      for (size_t j = 0; j < l->console_cell_count; j++)
        if (l->console_cells[j].pointer == (uint32_t)pointer)
          qa_range_error("Invalid saved chat console cell");
    }
    l->console_cells = xrealloc(l->console_cells, (l->console_cell_count + 1) * sizeof *l->console_cells);
    qa_chat_console_cell *cell = &l->console_cells[l->console_cell_count++];
    cell->allocation = allocation;
    cell->offset = (int64_t)offset;
    cell->pointer = (uint32_t)pointer;
  }
  qa_save_reader_destroy(&cells);
  double free_pointer = field_integer(&reader, "freeConsoleMessages", 0);
  if (free_pointer != 0 && (free_pointer > (double)UINT32_MAX || console_lookup(l, (uint32_t)free_pointer) == NULL))
    qa_range_error("Saved chat console pointer is missing");
  l->free_console_messages = (uint32_t)free_pointer;
  console_visit(l, l->free_console_messages);
  for (size_t index = 0; index < 65; index++) {
    item_of(&list, index, &entry);
    if (qa_save_reader_is_null(&entry)) {
      qa_save_reader_destroy(&entry);
      continue;
    }
    double reference = field_integer(&entry, "allocation", 0);
    double state_revision = field_integer(&entry, "revision", 0);
    qa_save_reader_destroy(&entry);
    qa_bot_memory_allocation allocation = qa_bot_memory_restored_allocation(restored, (int64_t)reference);
    if (allocation.length != CHAT_STATE_BYTES || state_revision > (double)QA_MAX_SAFE_INTEGER)
      qa_range_error("Invalid saved chat state allocation");
    qa_chat_state *state = xcalloc(1, sizeof *state);
    state->allocation = allocation;
    state->revision = (int64_t)state_revision;
    if ((int64_t)console_visit(l, state_console(l, state, STATE_FIRST)) != state_i32(state, STATE_COUNT))
      qa_range_error("Saved chat console count mismatch");
    (void)state_console(l, state, STATE_LAST);
    (void)state_chat(l, state);
    l->states[index] = state;
  }
  qa_save_reader_destroy(&list);
  l->generation_id++;
  l->setup_revision = (int64_t)revision;
  qa_save_reader cache;
  field_of(&reader, "cache", &cache);
  size_t cache_count = qa_save_reader_list_count(&cache);
  cache_ensure(l, cache_count < 64 ? 64 : cache_count);
  l->cache_length = cache_count;
  for (size_t i = 0; i < cache_count; i++) {
    item_of(&cache, i, &entry);
    if (qa_save_reader_is_null(&entry)) {
      qa_save_reader_destroy(&entry);
      continue;
    }
    qa_bot_memory_allocation allocation = qa_bot_memory_restored_allocation(restored, (int64_t)qa_save_reader_integer_min(&entry, 0));
    qa_save_reader_destroy(&entry);
    if (allocation.length != CHAT_CACHE_BYTES)
      qa_range_error("Saved chat cache allocation size mismatch");
    qa_chat_cached *cached = xcalloc(1, sizeof *cached);
    cached->allocation = allocation;
    (void)cached_chat(l, cached);
    l->cache[i] = cached;
  }
  qa_save_reader_destroy(&cache);
  l->has_synonyms_allocation = field_allocation(&reader, "synonymsAllocation", restored, &l->synonyms_allocation);
  l->has_randoms_allocation = field_allocation(&reader, "randomsAllocation", restored, &l->randoms_allocation);
  /* Synonym layout */
  field_of(&reader, "synonyms", &list);
  size_t group_count = qa_save_reader_list_count(&list);
  int64_t **layout = xcalloc(group_count, sizeof *layout);
  size_t *layout_counts = xcalloc(group_count, sizeof *layout_counts);
  for (size_t i = 0; i < group_count; i++) {
    item_of(&list, i, &entry);
    size_t lengths = qa_save_reader_list_count(&entry);
    layout[i] = xcalloc(lengths, sizeof *layout[i]);
    layout_counts[i] = lengths;
    for (size_t j = 0; j < lengths; j++) {
      qa_save_reader length;
      item_of(&entry, j, &length);
      layout[i][j] = (int64_t)qa_save_reader_integer_min(&length, 0);
      qa_save_reader_destroy(&length);
    }
    qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&list);
  qa_stored_synonyms_destroy(&l->synonyms);
  qa_restore_synonyms(&l->synonyms, l->has_synonyms_allocation ? &l->synonyms_allocation : NULL,
                      (const int64_t *const *)layout, layout_counts, group_count);
  for (size_t i = 0; i < group_count; i++)
    free(layout[i]);
  free(layout);
  free(layout_counts);
  /* Random layout */
  field_of(&reader, "randoms", &list);
  size_t random_count = qa_save_reader_list_count(&list);
  qa_saved_random_layout *random_layout = xcalloc(random_count, sizeof *random_layout);
  for (size_t i = 0; i < random_count; i++) {
    item_of(&list, i, &entry);
    random_layout[i].name_bytes = (int64_t)field_integer(&entry, "nameBytes", 0);
    qa_save_reader bytes;
    field_of(&entry, "messageBytes", &bytes);
    size_t lengths = qa_save_reader_list_count(&bytes);
    random_layout[i].message_bytes = xcalloc(lengths, sizeof *random_layout[i].message_bytes);
    random_layout[i].message_count = lengths;
    for (size_t j = 0; j < lengths; j++) {
      qa_save_reader length;
      item_of(&bytes, j, &length);
      random_layout[i].message_bytes[j] = (int64_t)qa_save_reader_integer_min(&length, 0);
      qa_save_reader_destroy(&length);
    }
    qa_save_reader_destroy(&bytes);
    qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&list);
  qa_stored_randoms_destroy(&l->randoms);
  qa_restore_randoms(&l->randoms, l->has_randoms_allocation ? &l->randoms_allocation : NULL, random_layout, random_count);
  for (size_t i = 0; i < random_count; i++)
    free(random_layout[i].message_bytes);
  free(random_layout);
  /* Graphs */
  qa_save_reader graph;
  field_of(&reader, "matches", &graph);
  release_cell_root(&l->matches);
  if (!qa_save_reader_is_null(&graph))
    l->matches = qa_chat_template_restore_graph(graph.value, l->memory, restored);
  qa_save_reader_destroy(&graph);
  field_of(&reader, "replies", &graph);
  release_cell_root(&l->replies);
  if (!qa_save_reader_is_null(&graph))
    l->replies = qa_chat_reply_restore_graph(graph.value, l->memory, restored);
  qa_save_reader_destroy(&graph);
  l->has_console_heap = field_allocation(&reader, "consoleMessageHeap", restored, &l->console_heap);
  restore_issues(l, &reader);
  qa_save_reader_destroy(&reader);
}
