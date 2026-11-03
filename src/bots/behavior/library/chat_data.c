/* See header. GPL-2.0-or-later. */
#include "qa/bots/behavior/library/chat_data.h"
#include "qa/foundation/support.h"
#include <math.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- small helpers ---- */
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
static char *xdup(const char *text) {
  size_t size = strlen(text) + 1;
  char *copy = xcalloc(size, 1);
  memcpy(copy, text, size);
  return copy;
}
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
static void reader_init(qa_save_reader *reader, const qa_save_value *value, const char *path) {
  if (!qa_save_reader_init(reader, value, path))
    qa_range_error("out of memory");
}
static double field_integer(const qa_save_reader *reader, const char *name, double minimum) {
  qa_save_reader field;
  if (!qa_save_reader_field(reader, name, &field))
    qa_range_error("out of memory");
  double value = qa_save_reader_integer_min(&field, minimum);
  qa_save_reader_destroy(&field);
  return value;
}
static void field_reader(const qa_save_reader *reader, const char *name, qa_save_reader *out) {
  if (!qa_save_reader_field(reader, name, out))
    qa_range_error("out of memory");
}
static void item_reader(const qa_save_reader *reader, size_t index, qa_save_reader *out) {
  if (!qa_save_reader_item(reader, index, out))
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
static float bits_f32(uint32_t bits) {
  float value;
  memcpy(&value, &bits, sizeof value);
  return value;
}
static uint32_t f32_bits(double value) {
  float narrowed = (float)value;
  uint32_t bits;
  memcpy(&bits, &narrowed, sizeof bits);
  return bits;
}
static char ascii_upper(char c) { return c >= 'a' && c <= 'z' ? (char)(c - 32) : c; }
/* TS containsChatText: uppercases a-z only; "".includes("") is true. */
static bool contains_chat_text(const char *text, const char *part) {
  size_t length = strlen(text), needle = strlen(part);
  for (size_t i = 0; i + needle <= length; i++) {
    size_t j = 0;
    while (j < needle && ascii_upper(text[i + j]) == ascii_upper(part[j]))
      j++;
    if (j == needle)
      return true;
  }
  return false;
}

/* ---- ChatTextSource and chatTextEquals ---- */
qa_chat_text_source qa_chat_text(const char *text) {
  qa_chat_text_source source = {text, NULL, NULL};
  return source;
}
qa_chat_text_source qa_chat_text_live(qa_chat_text_fn fn, void *ctx) {
  qa_chat_text_source source = {NULL, fn, ctx};
  return source;
}
bool qa_chat_text_is_null(qa_chat_text_source source) {
  return source.text == NULL && source.fn == NULL;
}
const char *qa_chat_text_read(qa_chat_text_source source, int64_t maximum_bytes) {
  return source.fn != NULL ? source.fn(source.ctx, maximum_bytes) : source.text;
}
bool qa_chat_text_equals_source(const char *expected, qa_chat_text_source input,
                                bool case_sensitive) {
  if (qa_chat_text_is_null(input))
    return false;
  size_t length = strlen(expected);
  for (size_t index = 0; index <= length; index++) {
    /* Q_stricmp and strcmp stop reading at the first different source byte. */
    const char *text = qa_chat_text_read(input, (int64_t)index + 1);
    int first = index == length ? 0 : (uint8_t)expected[index];
    int second = strnlen(text, index + 1) > index ? (uint8_t)text[index] : 0;
    if (!case_sensitive) {
      if (first >= 97 && first <= 122)
        first -= 32;
      if (second >= 97 && second <= 122)
        second -= 32;
    }
    if (first != second)
      return false;
    if (first == 0)
      return true;
  }
  return false;
}
bool qa_chat_text_equals(const char *expected, const char *input, bool case_sensitive) {
  return qa_chat_text_equals_source(expected, qa_chat_text(input), case_sensitive);
}

/* ---- snapshot destructors ---- */
static void messages_destroy(qa_chat_message *messages, size_t count) {
  for (size_t i = 0; i < count; i++)
    free(messages[i].text);
  free(messages);
}
void qa_initial_chat_destroy(qa_initial_chat *chat) {
  for (size_t i = 0; i < chat->type_count; i++) {
    free(chat->types[i].name);
    messages_destroy(chat->types[i].messages, chat->types[i].message_count);
  }
  free(chat->types);
  chat->types = NULL;
  chat->type_count = 0;
}
static void pieces_destroy(qa_match_piece *pieces, size_t count) {
  for (size_t i = 0; i < count; i++) {
    for (size_t j = 0; j < pieces[i].alternative_count; j++)
      free(pieces[i].alternatives[j]);
    free(pieces[i].alternatives);
  }
  free(pieces);
}
void qa_match_template_destroy(qa_match_template *match) {
  pieces_destroy(match->pieces, match->piece_count);
  match->pieces = NULL;
  match->piece_count = 0;
}
void qa_reply_chat_destroy(qa_reply_chat *reply) {
  for (size_t i = 0; i < reply->key_count; i++) {
    free(reply->keys[i].text);
    pieces_destroy(reply->keys[i].pieces, reply->keys[i].piece_count);
  }
  free(reply->keys);
  messages_destroy(reply->messages, reply->message_count);
  reply->keys = NULL;
  reply->messages = NULL;
  reply->key_count = reply->message_count = 0;
}

/* ---- ChatDataMemory ---- */
void qa_chat_data_memory_init(qa_chat_data_memory *m, const qa_bot_memory_allocation *allocation) {
  memset(m, 0, sizeof *m);
  if (allocation != NULL) {
    m->allocation = *allocation;
    m->has_allocation = true;
  }
}
int64_t qa_chat_data_memory_byte_length(const qa_chat_data_memory *m) { return m->cursor; }
int64_t qa_chat_data_memory_reserve(qa_chat_data_memory *m, int64_t size) {
  int64_t offset = m->cursor;
  m->cursor += size;
  if (m->has_allocation && m->cursor > (int64_t)m->allocation.length)
    qa_range_error("chat second pass exceeds first-pass allocation");
  return offset;
}
static void data_integer(qa_chat_data_memory *m, int64_t offset, uint32_t value) {
  if (m->has_allocation)
    store_u32(qa_bot_memory_allocation_bytes(&m->allocation) + offset, value);
}
static void data_float(qa_chat_data_memory *m, int64_t offset, double value) {
  if (m->has_allocation)
    store_u32(qa_bot_memory_allocation_bytes(&m->allocation) + offset, f32_bits(value));
}
static qa_stored_text data_string(qa_chat_data_memory *m, const char *value) {
  size_t length = strlen(value);
  qa_stored_text stored = {NULL, qa_chat_data_memory_reserve(m, (int64_t)length + 1), -1};
  if (m->has_allocation) {
    memcpy(qa_bot_memory_allocation_bytes(&m->allocation) + stored.offset, value, length + 1);
    return stored;
  }
  stored.fixed = xdup(value);
  return stored;
}

/* ---- stored lists ---- */
static const char *stored_text(const qa_bot_memory_allocation *allocation, bool has_allocation,
                               const qa_stored_text *text) {
  if (!has_allocation)
    return text->fixed;
  const uint8_t *bytes = qa_bot_memory_allocation_bytes(allocation);
  if (text->length >= 0) {
    if (text->offset + text->length >= (int64_t)allocation->length ||
        bytes[text->offset + text->length] != 0)
      qa_range_error("Saved chat string is unterminated");
    return (const char *)bytes + text->offset;
  }
  if (memchr(bytes + text->offset, 0, allocation->length - (size_t)text->offset) == NULL)
    qa_range_error("chat string has no terminator before allocation end");
  return (const char *)bytes + text->offset;
}
static uint32_t stored_u32(const qa_bot_memory_allocation *allocation, int64_t offset) {
  if (offset < 0 || offset + 4 > (int64_t)allocation->length)
    qa_range_error("Offset is outside the bounds of the DataView");
  return load_u32(qa_bot_memory_allocation_bytes(allocation) + offset);
}
void qa_stored_synonyms_destroy(qa_stored_synonyms *list) {
  for (size_t i = 0; i < list->count; i++) {
    for (size_t j = 0; j < list->groups[i].entry_count; j++)
      free(list->groups[i].entries[j].text.fixed);
    free(list->groups[i].entries);
  }
  free(list->groups);
  memset(list, 0, sizeof *list);
}
uint32_t qa_stored_synonym_group_context(const qa_stored_synonyms *list, size_t group) {
  const qa_stored_synonym_group *g = &list->groups[group];
  return list->has_allocation ? stored_u32(&list->allocation, g->record) : g->fixed_context;
}
double qa_stored_synonym_group_total_weight(const qa_stored_synonyms *list, size_t group) {
  const qa_stored_synonym_group *g = &list->groups[group];
  return list->has_allocation ? bits_f32(stored_u32(&list->allocation, g->record + 4))
                              : g->fixed_total_weight;
}
const char *qa_stored_synonym_text(const qa_stored_synonyms *list, size_t group, size_t entry) {
  return stored_text(&list->allocation, list->has_allocation,
                     &list->groups[group].entries[entry].text);
}
double qa_stored_synonym_weight(const qa_stored_synonyms *list, size_t group, size_t entry) {
  const qa_stored_synonym *e = &list->groups[group].entries[entry];
  return list->has_allocation ? bits_f32(stored_u32(&list->allocation, e->record + 4))
                              : e->fixed_weight;
}
void qa_stored_randoms_destroy(qa_stored_randoms *list) {
  for (size_t i = 0; i < list->count; i++) {
    free(list->lists[i].name.fixed);
    for (size_t j = 0; j < list->lists[i].message_count; j++)
      free(list->lists[i].messages[j].fixed);
    free(list->lists[i].messages);
  }
  free(list->lists);
  memset(list, 0, sizeof *list);
}
const char *qa_stored_random_name(const qa_stored_randoms *list, size_t index) {
  return stored_text(&list->allocation, list->has_allocation, &list->lists[index].name);
}
uint32_t qa_stored_random_num_messages(const qa_stored_randoms *list, size_t index) {
  const qa_stored_random_list *l = &list->lists[index];
  return list->has_allocation ? stored_u32(&list->allocation, l->record + 4) : l->fixed_count;
}
const char *qa_stored_random_message(const qa_stored_randoms *list, size_t index, int64_t message) {
  const qa_stored_random_list *l = &list->lists[index];
  if (message < 0 || (size_t)message >= l->message_count)
    return NULL;
  return stored_text(&list->allocation, list->has_allocation, &l->messages[message]);
}

/* TS restoredChatData. */
typedef struct restored_data {
  const qa_bot_memory_allocation *allocation;
  int64_t cursor;
} restored_data;
static int64_t restored_reserve(restored_data *data, int64_t bytes) {
  if (bytes < 0 || data->allocation == NULL ||
      data->cursor + bytes > (int64_t)data->allocation->length)
    qa_range_error("Saved chat data layout exceeds allocation");
  int64_t offset = data->cursor;
  data->cursor += bytes;
  return offset;
}
static qa_stored_text restored_string(restored_data *data, int64_t length) {
  qa_stored_text text = {NULL, restored_reserve(data, length + 1), length};
  stored_text(data->allocation, true, &text);
  return text;
}
static void restored_finish(const restored_data *data) {
  if (data->cursor != (data->allocation != NULL ? (int64_t)data->allocation->length : 0))
    qa_range_error("Saved chat layout does not cover its allocation");
}
void qa_restore_synonyms(qa_stored_synonyms *out, const qa_bot_memory_allocation *allocation,
                         const int64_t *const *layout, const size_t *layout_counts, size_t count) {
  memset(out, 0, sizeof *out);
  restored_data data = {allocation, 0};
  out->groups = xcalloc(count, sizeof *out->groups);
  out->count = count;
  for (size_t i = 0; i < count; i++) {
    qa_stored_synonym_group *group = &out->groups[i];
    group->record = restored_reserve(&data, 16);
    group->entries = xcalloc(layout_counts[i], sizeof *group->entries);
    group->entry_count = layout_counts[i];
    for (size_t j = 0; j < layout_counts[i]; j++) {
      group->entries[j].record = restored_reserve(&data, 12);
      group->entries[j].text = restored_string(&data, layout[i][j]);
    }
    if (layout_counts[i] < 2)
      qa_range_error("Saved synonym group requires two entries");
  }
  restored_finish(&data);
  if (allocation != NULL) {
    out->allocation = *allocation;
    out->has_allocation = true;
  }
}
void qa_restore_randoms(qa_stored_randoms *out, const qa_bot_memory_allocation *allocation,
                        const qa_saved_random_layout *layout, size_t count) {
  memset(out, 0, sizeof *out);
  restored_data data = {allocation, 0};
  out->lists = xcalloc(count, sizeof *out->lists);
  out->count = count;
  for (size_t i = 0; i < count; i++) {
    qa_stored_random_list *list = &out->lists[i];
    list->record = restored_reserve(&data, 16);
    list->name = restored_string(&data, layout[i].name_bytes);
    size_t messages = layout[i].message_count;
    list->messages = xcalloc(messages, sizeof *list->messages);
    list->message_count = messages;
    for (size_t j = 0; j < messages; j++) {
      restored_reserve(&data, 8);
      /* Saved in file order; the live list is head-first (TS .reverse()). */
      list->messages[messages - 1 - j] = restored_string(&data, layout[i].message_bytes[j]);
    }
    if (stored_u32(allocation, list->record + 4) != messages)
      qa_range_error("Saved random chat message count mismatch");
  }
  restored_finish(&data);
  if (allocation != NULL) {
    out->allocation = *allocation;
    out->has_allocation = true;
  }
}

/* ---- ChatInitial ---- */
#define INITIAL_TYPE_MARK 1
#define INITIAL_MESSAGE_MARK 2
struct qa_chat_initial {
  qa_bot_memory_allocation allocation;
  uint32_t pointer;
  uint8_t *marks; /* TS types/messages maps, indexed by pointer word */
  size_t mark_count;
};
qa_chat_initial *qa_chat_initial_create(const qa_bot_memory_allocation *allocation,
                                        uint32_t pointer) {
  qa_chat_initial *chat = xcalloc(1, sizeof *chat);
  chat->allocation = *allocation;
  chat->pointer = pointer;
  chat->mark_count = allocation->length + 1;
  chat->marks = xcalloc(chat->mark_count, 1);
  return chat;
}
void qa_chat_initial_destroy(qa_chat_initial *chat) {
  if (chat == NULL)
    return;
  free(chat->marks);
  free(chat);
}
uint32_t qa_chat_initial_pointer(const qa_chat_initial *chat) { return chat->pointer; }
qa_bot_memory_allocation *qa_chat_initial_allocation(qa_chat_initial *chat) {
  return &chat->allocation;
}
static uint8_t *initial_view(qa_chat_initial *chat, int64_t offset, int64_t size) {
  uint8_t *bytes = qa_bot_memory_allocation_bytes(&chat->allocation);
  if (offset < 0 || offset + size > (int64_t)chat->allocation.length)
    qa_range_error("initial chat record exceeds its allocation");
  return bytes + offset;
}
static const char *initial_text(qa_chat_initial *chat, int64_t offset) {
  uint8_t *bytes = qa_bot_memory_allocation_bytes(&chat->allocation);
  if (offset < 0 || offset >= (int64_t)chat->allocation.length)
    qa_range_error("initial chat string pointer is outside its allocation");
  if (memchr(bytes + offset, 0, chat->allocation.length - (size_t)offset) == NULL)
    qa_range_error("initial chat string has no terminator before allocation end");
  return (const char *)bytes + offset;
}
static void initial_copy_text(qa_chat_initial *chat, int64_t offset, const char *text, size_t size) {
  uint8_t *view = initial_view(chat, offset, (int64_t)size);
  size_t copied = strlen(text);
  if (copied > size)
    copied = size;
  memcpy(view, text, copied);
  memset(view + copied, 0, size - copied);
}
static uint32_t type_at(const qa_chat_initial *chat, uint32_t pointer) {
  if (pointer != 0 &&
      (pointer >= chat->mark_count || (chat->marks[pointer] & INITIAL_TYPE_MARK) == 0))
    qa_range_error("initial chat pointer does not identify an allocated type");
  return pointer;
}
static uint32_t message_at(const qa_chat_initial *chat, uint32_t pointer) {
  if (pointer != 0 &&
      (pointer >= chat->mark_count || (chat->marks[pointer] & INITIAL_MESSAGE_MARK) == 0))
    qa_range_error("initial chat pointer does not identify an allocated message");
  return pointer;
}
uint32_t qa_chat_initial_first_type(qa_chat_initial *chat) {
  return type_at(chat, load_u32(initial_view(chat, 0, 4)));
}
uint32_t qa_chat_initial_type_next(qa_chat_initial *chat, uint32_t type) {
  return type_at(chat, load_u32(initial_view(chat, (int64_t)type - 1, 44) + 40));
}
const char *qa_chat_initial_type_name(qa_chat_initial *chat, uint32_t type) {
  return initial_text(chat, (int64_t)type - 1);
}
int32_t qa_chat_initial_type_num_messages(qa_chat_initial *chat, uint32_t type) {
  return (int32_t)load_u32(initial_view(chat, (int64_t)type - 1, 44) + 32);
}
uint32_t qa_chat_initial_type_first_message(qa_chat_initial *chat, uint32_t type) {
  return message_at(chat, load_u32(initial_view(chat, (int64_t)type - 1, 44) + 36));
}
uint32_t qa_chat_initial_message_next(qa_chat_initial *chat, uint32_t message) {
  return message_at(chat, load_u32(initial_view(chat, (int64_t)message - 1, 12) + 8));
}
const char *qa_chat_initial_message_text(qa_chat_initial *chat, uint32_t message) {
  return initial_text(chat, (int64_t)load_u32(initial_view(chat, (int64_t)message - 1, 12)) - 1);
}
float qa_chat_initial_message_time(qa_chat_initial *chat, uint32_t message) {
  return bits_f32(load_u32(initial_view(chat, (int64_t)message - 1, 12) + 4));
}
void qa_chat_initial_message_set_time(qa_chat_initial *chat, uint32_t message, double time) {
  store_u32(initial_view(chat, (int64_t)message - 1, 12) + 4, f32_bits(time));
}
static uint32_t initial_add_type(qa_chat_initial *chat, int64_t offset, const char *name) {
  initial_view(chat, offset, 44);
  uint32_t type = (uint32_t)offset + 1;
  chat->marks[type] |= INITIAL_TYPE_MARK;
  initial_copy_text(chat, offset, name, 32);
  uint8_t *record = initial_view(chat, offset, 44);
  store_u32(record + 36, 0); /* firstMessage = null */
  store_u32(record + 40, qa_chat_initial_first_type(chat));
  store_u32(initial_view(chat, 0, 4), type);
  return type;
}
static void initial_add_message(qa_chat_initial *chat, uint32_t type, int64_t offset) {
  uint8_t *record = initial_view(chat, offset, 12);
  uint32_t message = (uint32_t)offset + 1;
  chat->marks[message] |= INITIAL_MESSAGE_MARK;
  store_u32(record + 4, f32_bits(-40));
  store_u32(record + 8, qa_chat_initial_type_first_message(chat, type));
  store_u32(initial_view(chat, (int64_t)type - 1, 44) + 36, message);
  store_u32(record, (uint32_t)offset + 12 + 1);
}
static void initial_write_message(qa_chat_initial *chat, uint32_t type, int64_t offset,
                                  const char *text) {
  initial_copy_text(chat, offset, text, strlen(text) + 1);
  uint8_t *record = initial_view(chat, (int64_t)type - 1, 44);
  store_u32(record + 32, load_u32(record + 32) + 1);
}
void qa_chat_initial_snapshot(qa_chat_initial *chat, qa_initial_chat *out) {
  memset(out, 0, sizeof *out);
  for (uint32_t type = qa_chat_initial_first_type(chat); type != 0;
       type = qa_chat_initial_type_next(chat, type)) {
    out->types = xrealloc(out->types, (out->type_count + 1) * sizeof *out->types);
    qa_chat_type *entry = &out->types[out->type_count++];
    memset(entry, 0, sizeof *entry);
    entry->name = xdup(qa_chat_initial_type_name(chat, type));
    for (uint32_t message = qa_chat_initial_type_first_message(chat, type); message != 0;
         message = qa_chat_initial_message_next(chat, message)) {
      entry->messages = xrealloc(entry->messages, (entry->message_count + 1) * sizeof *entry->messages);
      entry->messages[entry->message_count].text = xdup(qa_chat_initial_message_text(chat, message));
      entry->messages[entry->message_count].time = qa_chat_initial_message_time(chat, message);
      entry->message_count++;
    }
  }
}
qa_save_value *qa_chat_initial_checkpoint(qa_chat_initial *chat, qa_bot_memory_capture *capture) {
  qa_save_value *image = checked(qa_save_value_object());
  qa_save_value *types = checked(qa_save_value_array());
  qa_save_value *messages = checked(qa_save_value_array());
  /* Offsets are reserved ascending, so pointer order is TS Map insertion order. */
  for (size_t pointer = 1; pointer < chat->mark_count; pointer++) {
    if ((chat->marks[pointer] & INITIAL_TYPE_MARK) != 0)
      push_item(types, qa_save_value_number((double)pointer));
    if ((chat->marks[pointer] & INITIAL_MESSAGE_MARK) != 0)
      push_item(messages, qa_save_value_number((double)pointer));
  }
  set_field(image, "allocation",
            qa_save_value_number((double)qa_bot_memory_capture_reference(capture, &chat->allocation)));
  set_field(image, "pointer", qa_save_value_number((double)chat->pointer));
  set_field(image, "types", types);
  set_field(image, "messages", messages);
  return image;
}
qa_chat_initial *qa_chat_initial_restore(const qa_save_value *value,
                                         qa_bot_memory_restored *restored) {
  qa_save_reader reader, list, entry;
  reader_init(&reader, value, "bot.chat.initial");
  double reference = field_integer(&reader, "allocation", 0);
  double pointer = field_integer(&reader, "pointer", 1);
  qa_bot_memory_allocation allocation =
      qa_bot_memory_restored_allocation(restored, (int64_t)reference);
  qa_chat_initial *chat = qa_chat_initial_create(&allocation, (uint32_t)pointer);
  field_reader(&reader, "types", &list);
  for (size_t i = 0, n = qa_save_reader_list_count(&list); i < n; i++) {
    item_reader(&list, i, &entry);
    double word = qa_save_reader_integer_min(&entry, 1);
    qa_save_reader_destroy(&entry);
    if (word < 1 || word >= (double)chat->mark_count ||
        (chat->marks[(size_t)word] & INITIAL_TYPE_MARK) != 0)
      qa_range_error(word < 1 || word >= (double)chat->mark_count
                         ? "initial chat record exceeds its allocation"
                         : "Invalid saved initial chat type pointer");
    initial_view(chat, (int64_t)word - 1, 44);
    chat->marks[(size_t)word] |= INITIAL_TYPE_MARK;
  }
  qa_save_reader_destroy(&list);
  field_reader(&reader, "messages", &list);
  for (size_t i = 0, n = qa_save_reader_list_count(&list); i < n; i++) {
    item_reader(&list, i, &entry);
    double word = qa_save_reader_integer_min(&entry, 1);
    qa_save_reader_destroy(&entry);
    if (word < 1 || word >= (double)chat->mark_count ||
        (chat->marks[(size_t)word] & INITIAL_MESSAGE_MARK) != 0)
      qa_range_error(word < 1 || word >= (double)chat->mark_count
                         ? "initial chat record exceeds its allocation"
                         : "Invalid saved initial chat message pointer");
    initial_view(chat, (int64_t)word - 1, 12);
    chat->marks[(size_t)word] |= INITIAL_MESSAGE_MARK;
  }
  qa_save_reader_destroy(&list);
  qa_save_reader_destroy(&reader);
  /* Bound by distinct record counts: more steps than records is a cycle. */
  size_t limit = chat->mark_count, seen_types = 0;
  for (uint32_t type = qa_chat_initial_first_type(chat); type != 0;
       type = qa_chat_initial_type_next(chat, type)) {
    if (++seen_types > limit)
      qa_range_error("Saved initial chat type list cycles");
    int64_t messages = 0;
    for (uint32_t message = qa_chat_initial_type_first_message(chat, type); message != 0;
         message = qa_chat_initial_message_next(chat, message)) {
      if ((size_t)++messages > limit)
        qa_range_error("Saved initial chat message list cycles");
      qa_chat_initial_message_text(chat, message);
    }
    if (messages != qa_chat_initial_type_num_messages(chat, type))
      qa_range_error("Saved initial chat message count mismatch");
  }
  return chat;
}

/* ---- ChatGraphMemory ---- */
struct qa_chat_graph {
  size_t refs;
  qa_bot_memory *memory;
  qa_bot_memory own_memory;
  qa_chat_cell **cells; /* insertion order; TS Map pointer -> cell */
  size_t count;
};
static qa_chat_graph *graph_create(qa_bot_memory *memory) {
  qa_chat_graph *graph = xcalloc(1, sizeof *graph);
  graph->refs = 1;
  qa_bot_memory_init(&graph->own_memory);
  graph->memory = memory != NULL ? memory : &graph->own_memory;
  return graph;
}
qa_chat_graph *qa_chat_graph_retain(qa_chat_graph *graph) {
  graph->refs++;
  return graph;
}
void qa_chat_graph_release(qa_chat_graph *graph) {
  if (graph == NULL || --graph->refs > 0)
    return;
  for (size_t i = 0; i < graph->count; i++)
    free(graph->cells[i]);
  free(graph->cells);
  qa_bot_memory_destroy(&graph->own_memory);
  free(graph);
}
static qa_chat_cell *graph_register(qa_chat_graph *graph, qa_chat_cell_kind kind,
                                    const qa_bot_memory_allocation *allocation, uint32_t pointer,
                                    int64_t offset) {
  qa_chat_cell *cell = xcalloc(1, sizeof *cell);
  *cell = (qa_chat_cell){graph, kind, *allocation, pointer, offset};
  graph->cells = xrealloc(graph->cells, (graph->count + 1) * sizeof *graph->cells);
  graph->cells[graph->count++] = cell;
  return cell;
}
static qa_chat_cell *graph_allocate(qa_chat_graph *graph, qa_chat_cell_kind kind, int64_t size) {
  qa_bot_memory_allocation allocation;
  if (!qa_bot_memory_allocate(graph->memory, size, QA_BOT_MEMORY_HUNK, true, &allocation))
    qa_range_error("out of memory");
  return graph_register(graph, kind, &allocation, (uint32_t)graph->count + 1, 0);
}
static qa_chat_cell *graph_target(const qa_chat_graph *graph, uint32_t pointer) {
  if (pointer == 0)
    return NULL;
  if (pointer <= graph->count && graph->cells[pointer - 1]->pointer == pointer)
    return graph->cells[pointer - 1];
  for (size_t i = 0; i < graph->count; i++) /* restored out-of-order pointers */
    if (graph->cells[i]->pointer == pointer)
      return graph->cells[i];
  qa_range_error("chat pointer is outside its allocated graph");
}
static qa_chat_cell *graph_at(const qa_chat_graph *graph, uint32_t pointer, qa_chat_cell_kind kind,
                              const char *wrong) {
  qa_chat_cell *value = graph_target(graph, pointer);
  if (value != NULL && value->kind != kind)
    qa_range_error("chat pointer does not identify %s", wrong);
  return value;
}
static uint8_t *cell_view(const qa_chat_cell *cell, int64_t offset) {
  if (offset + 4 > (int64_t)cell->allocation.length)
    qa_range_error("Offset is outside the bounds of the DataView");
  return qa_bot_memory_allocation_bytes(&cell->allocation) + offset;
}
static uint32_t cell_u32(const qa_chat_cell *cell, int64_t offset) {
  return load_u32(cell_view(cell, offset));
}
static void cell_set_u32(const qa_chat_cell *cell, int64_t offset, uint32_t value) {
  store_u32(cell_view(cell, offset), value);
}
static void cell_set_link(const qa_chat_cell *cell, int64_t offset, const qa_chat_cell *value) {
  cell_set_u32(cell, offset, value != NULL ? value->pointer : 0);
}
static void cell_free_block(const qa_chat_cell *cell) {
  qa_bot_memory_allocation allocation = cell->allocation;
  qa_bot_memory_free(cell->owner->memory, &allocation);
}
static const char *string_cell_text(const qa_chat_cell *cell) {
  const uint8_t *bytes = qa_bot_memory_allocation_bytes(&cell->allocation);
  if (memchr(bytes + cell->offset, 0, cell->allocation.length - (size_t)cell->offset) == NULL)
    qa_range_error("chat string has no terminator before allocation end");
  return (const char *)bytes + cell->offset;
}
static qa_chat_cell *graph_write_string(qa_chat_graph *graph,
                                        const qa_bot_memory_allocation *allocation, int64_t offset,
                                        const char *text) {
  memcpy(qa_bot_memory_allocation_bytes(allocation) + offset, text, strlen(text) + 1);
  return graph_register(graph, QA_CHAT_CELL_STRING, allocation, (uint32_t)graph->count + 1, offset);
}
static qa_chat_cell *graph_new_string(qa_chat_graph *graph, const char *text) {
  qa_bot_memory_allocation allocation;
  if (!qa_bot_memory_allocate(graph->memory, (int64_t)strlen(text) + 1, QA_BOT_MEMORY_HUNK, true,
                              &allocation))
    qa_range_error("out of memory");
  return graph_write_string(graph, &allocation, 0, text);
}
static qa_chat_cell *graph_new_match_string(qa_chat_graph *graph, const char *text) {
  qa_chat_cell *cell = graph_allocate(graph, QA_CHAT_CELL_MATCH_STRING, 8 + (int64_t)strlen(text) + 1);
  cell_set_link(cell, 0, graph_write_string(graph, &cell->allocation, 8, text));
  return cell;
}
static qa_chat_cell *graph_new_message(qa_chat_graph *graph, const char *text) {
  qa_chat_cell *cell = graph_allocate(graph, QA_CHAT_CELL_MESSAGE, 12 + (int64_t)strlen(text) + 1);
  cell_set_link(cell, 0, graph_write_string(graph, &cell->allocation, 12, text));
  return cell;
}
static qa_chat_cell *cell_string_link(const qa_chat_cell *cell, int64_t offset) {
  return graph_at(cell->owner, cell_u32(cell, offset), QA_CHAT_CELL_STRING, "a string");
}

/* ChatMatchString */
const char *qa_chat_match_string_text(const qa_chat_cell *value) {
  const qa_chat_cell *text = cell_string_link(value, 0);
  if (text == NULL)
    qa_range_error("match string has a null string pointer");
  return string_cell_text(text);
}
qa_chat_cell *qa_chat_match_string_next(const qa_chat_cell *value) {
  return graph_at(value->owner, cell_u32(value, 4), QA_CHAT_CELL_MATCH_STRING, "a match string");
}
/* ChatMatchPiece */
int32_t qa_chat_piece_type(const qa_chat_cell *piece) { return (int32_t)cell_u32(piece, 0); }
qa_chat_cell *qa_chat_piece_first_string(const qa_chat_cell *piece) {
  return graph_at(piece->owner, cell_u32(piece, 4), QA_CHAT_CELL_MATCH_STRING, "a match string");
}
int32_t qa_chat_piece_variable(const qa_chat_cell *piece) { return (int32_t)cell_u32(piece, 8); }
qa_chat_cell *qa_chat_piece_next(const qa_chat_cell *piece) {
  return graph_at(piece->owner, cell_u32(piece, 12), QA_CHAT_CELL_PIECE, "a match piece");
}
void qa_chat_piece_snapshot(const qa_chat_cell *piece, qa_match_piece *out) {
  memset(out, 0, sizeof *out);
  int32_t type = qa_chat_piece_type(piece);
  if (type == 1) {
    out->kind = QA_MATCH_PIECE_VARIABLE;
    out->index = qa_chat_piece_variable(piece);
    return;
  }
  if (type != 2)
    qa_range_error("unknown source match piece type");
  out->kind = QA_MATCH_PIECE_STRING;
  for (const qa_chat_cell *value = qa_chat_piece_first_string(piece); value != NULL;
       value = qa_chat_match_string_next(value)) {
    out->alternatives =
        xrealloc(out->alternatives, (out->alternative_count + 1) * sizeof *out->alternatives);
    out->alternatives[out->alternative_count++] = xdup(qa_chat_match_string_text(value));
  }
}
void qa_chat_piece_free(qa_chat_cell *piece) {
  while (piece != NULL) {
    qa_chat_cell *next = qa_chat_piece_next(piece);
    if (qa_chat_piece_type(piece) == 2) {
      for (qa_chat_cell *value = qa_chat_piece_first_string(piece); value != NULL;) {
        qa_chat_cell *next_string = qa_chat_match_string_next(value);
        cell_free_block(value);
        value = next_string;
      }
    }
    cell_free_block(piece);
    piece = next;
  }
}
static void pieces_snapshot(const qa_chat_cell *first, qa_match_piece **out, size_t *out_count) {
  *out = NULL;
  *out_count = 0;
  for (const qa_chat_cell *piece = first; piece != NULL; piece = qa_chat_piece_next(piece)) {
    *out = xrealloc(*out, (*out_count + 1) * sizeof **out);
    qa_chat_piece_snapshot(piece, &(*out)[(*out_count)++]);
  }
}
/* ChatMatchTemplate */
uint32_t qa_chat_template_context(const qa_chat_cell *match) { return cell_u32(match, 0); }
int32_t qa_chat_template_type(const qa_chat_cell *match) { return (int32_t)cell_u32(match, 4); }
int32_t qa_chat_template_subtype(const qa_chat_cell *match) { return (int32_t)cell_u32(match, 8); }
qa_chat_cell *qa_chat_template_first(const qa_chat_cell *match) {
  return graph_at(match->owner, cell_u32(match, 12), QA_CHAT_CELL_PIECE, "a match piece");
}
qa_chat_cell *qa_chat_template_next(const qa_chat_cell *match) {
  return graph_at(match->owner, cell_u32(match, 16), QA_CHAT_CELL_TEMPLATE, "a match template");
}
void qa_chat_template_snapshot(const qa_chat_cell *match, qa_match_template *out) {
  memset(out, 0, sizeof *out);
  pieces_snapshot(qa_chat_template_first(match), &out->pieces, &out->piece_count);
  out->context = qa_chat_template_context(match);
  out->type = qa_chat_template_type(match);
  out->subtype = qa_chat_template_subtype(match);
}
void qa_chat_template_free(qa_chat_cell *match) {
  while (match != NULL) {
    qa_chat_cell *next = qa_chat_template_next(match);
    qa_chat_cell *first = qa_chat_template_first(match);
    if (first != NULL)
      qa_chat_piece_free(first);
    cell_free_block(match);
    match = next;
  }
}
/* ChatReplyKey */
int32_t qa_chat_key_flags(const qa_chat_cell *key) { return (int32_t)cell_u32(key, 0); }
const char *qa_chat_key_text(const qa_chat_cell *key) {
  const qa_chat_cell *text = cell_string_link(key, 4);
  if (text == NULL)
    qa_range_error("reply key has a null string pointer");
  return string_cell_text(text);
}
qa_chat_cell *qa_chat_key_match(const qa_chat_cell *key) {
  return graph_at(key->owner, cell_u32(key, 8), QA_CHAT_CELL_PIECE, "a match piece");
}
qa_chat_cell *qa_chat_key_next(const qa_chat_cell *key) {
  return graph_at(key->owner, cell_u32(key, 12), QA_CHAT_CELL_KEY, "a reply key");
}
void qa_chat_key_snapshot(const qa_chat_cell *key, qa_reply_key *out) {
  memset(out, 0, sizeof *out);
  int32_t flags = qa_chat_key_flags(key);
  out->mode = (flags & 1) != 0 ? QA_REPLY_KEY_AND : (flags & 2) != 0 ? QA_REPLY_KEY_NOT : QA_REPLY_KEY_ANY;
  if ((flags & 4) != 0) {
    out->kind = QA_REPLY_TEST_NAME;
  } else if ((flags & 32) != 0) {
    out->kind = QA_REPLY_TEST_BOTNAMES;
    out->text = xdup(qa_chat_key_text(key));
  } else if ((flags & 64) != 0) {
    out->kind = QA_REPLY_TEST_GENDER;
    out->gender = 1;
  } else if ((flags & 128) != 0) {
    out->kind = QA_REPLY_TEST_GENDER;
    out->gender = 2;
  } else if ((flags & 256) != 0) {
    out->kind = QA_REPLY_TEST_GENDER;
    out->gender = 0;
  } else if ((flags & 16) != 0) {
    out->kind = QA_REPLY_TEST_MATCH;
    pieces_snapshot(qa_chat_key_match(key), &out->pieces, &out->piece_count);
  } else if ((flags & 8) != 0) {
    out->kind = QA_REPLY_TEST_STRING;
    out->text = xdup(qa_chat_key_text(key));
  } else {
    qa_range_error("unknown source reply key flags");
  }
}
/* ChatReplyMessage */
const char *qa_chat_reply_message_text(const qa_chat_cell *message) {
  const qa_chat_cell *text = cell_string_link(message, 0);
  if (text == NULL)
    qa_range_error("reply message has a null string pointer");
  return string_cell_text(text);
}
float qa_chat_reply_message_time(const qa_chat_cell *message) {
  return bits_f32(cell_u32(message, 4));
}
void qa_chat_reply_message_set_time(const qa_chat_cell *message, double time) {
  cell_set_u32(message, 4, f32_bits(time));
}
qa_chat_cell *qa_chat_reply_message_next(const qa_chat_cell *message) {
  return graph_at(message->owner, cell_u32(message, 8), QA_CHAT_CELL_MESSAGE, "a reply message");
}
/* ChatReply */
qa_chat_cell *qa_chat_reply_keys(const qa_chat_cell *reply) {
  return graph_at(reply->owner, cell_u32(reply, 0), QA_CHAT_CELL_KEY, "a reply key");
}
float qa_chat_reply_priority(const qa_chat_cell *reply) { return bits_f32(cell_u32(reply, 4)); }
int32_t qa_chat_reply_num_messages(const qa_chat_cell *reply) { return (int32_t)cell_u32(reply, 8); }
qa_chat_cell *qa_chat_reply_first_message(const qa_chat_cell *reply) {
  return graph_at(reply->owner, cell_u32(reply, 12), QA_CHAT_CELL_MESSAGE, "a reply message");
}
qa_chat_cell *qa_chat_reply_next(const qa_chat_cell *reply) {
  return graph_at(reply->owner, cell_u32(reply, 16), QA_CHAT_CELL_REPLY, "a reply");
}
void qa_chat_reply_snapshot(const qa_chat_cell *reply, qa_reply_chat *out) {
  memset(out, 0, sizeof *out);
  for (const qa_chat_cell *key = qa_chat_reply_keys(reply); key != NULL; key = qa_chat_key_next(key)) {
    out->keys = xrealloc(out->keys, (out->key_count + 1) * sizeof *out->keys);
    qa_chat_key_snapshot(key, &out->keys[out->key_count++]);
  }
  for (const qa_chat_cell *message = qa_chat_reply_first_message(reply); message != NULL;
       message = qa_chat_reply_message_next(message)) {
    out->messages = xrealloc(out->messages, (out->message_count + 1) * sizeof *out->messages);
    out->messages[out->message_count].text = xdup(qa_chat_reply_message_text(message));
    out->messages[out->message_count].time = qa_chat_reply_message_time(message);
    out->message_count++;
  }
  out->priority = qa_chat_reply_priority(reply);
}
void qa_chat_reply_free(qa_chat_cell *reply) {
  while (reply != NULL) {
    qa_chat_cell *next = qa_chat_reply_next(reply);
    for (qa_chat_cell *key = qa_chat_reply_keys(reply); key != NULL;) {
      qa_chat_cell *next_key = qa_chat_key_next(key);
      qa_chat_cell *match = qa_chat_key_match(key);
      if (match != NULL)
        qa_chat_piece_free(match);
      qa_chat_cell *text = cell_string_link(key, 4);
      if (text != NULL)
        cell_free_block(text);
      cell_free_block(key);
      key = next_key;
    }
    for (qa_chat_cell *message = qa_chat_reply_first_message(reply); message != NULL;) {
      qa_chat_cell *next_message = qa_chat_reply_message_next(message);
      cell_free_block(message);
      message = next_message;
    }
    cell_free_block(reply);
    reply = next;
  }
}

/* ---- graph checkpoint ---- */
static const char *const cell_kind_names[] = {"string", "match-string", "piece", "template",
                                              "key",    "message",      "reply"};
qa_save_value *qa_chat_cell_checkpoint_graph(const qa_chat_cell *root,
                                             qa_bot_memory_capture *capture) {
  qa_save_value *image = checked(qa_save_value_object());
  qa_save_value *pointers = checked(qa_save_value_array());
  const qa_chat_graph *graph = root->owner;
  for (size_t i = 0; i < graph->count; i++) {
    qa_chat_cell *cell = graph->cells[i];
    qa_save_value *entry = checked(qa_save_value_object());
    set_field(entry, "pointer", qa_save_value_number((double)cell->pointer));
    set_field(entry, "allocation",
              qa_save_value_number((double)qa_bot_memory_capture_reference(capture, &cell->allocation)));
    set_field(entry, "kind", qa_save_value_string(cell_kind_names[cell->kind]));
    set_field(entry, "offset", qa_save_value_number((double)cell->offset));
    push_item(pointers, entry);
  }
  set_field(image, "root", qa_save_value_number((double)root->pointer));
  set_field(image, "pointers", pointers);
  return image;
}
#define VISIT_PENDING 1
#define VISIT_DONE 2
static size_t graph_cell_index(const qa_chat_graph *graph, const qa_chat_cell *cell) {
  if (cell->pointer <= graph->count && graph->cells[cell->pointer - 1] == cell)
    return cell->pointer - 1;
  size_t index = 0;
  while (graph->cells[index] != cell)
    index++;
  return index;
}
static void graph_visit(const qa_chat_graph *graph, const qa_chat_cell *first, uint8_t *states) {
  /* TS recurses through every link; next chains are walked iteratively here so
   * a long list cannot exhaust the C stack. The cycle check is unchanged. */
  const qa_chat_cell **chain = NULL;
  size_t chain_count = 0;
  for (const qa_chat_cell *cell = first; cell != NULL;) {
    size_t index = graph_cell_index(graph, cell);
    if (states[index] == VISIT_DONE)
      break;
    if (states[index] == VISIT_PENDING)
      qa_range_error("Saved chat graph contains a cycle");
    states[index] = VISIT_PENDING;
    chain = xrealloc(chain, (chain_count + 1) * sizeof *chain);
    chain[chain_count++] = cell;
    const qa_chat_cell *next = NULL;
    switch (cell->kind) {
    case QA_CHAT_CELL_MATCH_STRING:
      graph_visit(graph, cell_string_link(cell, 0), states);
      next = qa_chat_match_string_next(cell);
      break;
    case QA_CHAT_CELL_MESSAGE:
      graph_visit(graph, cell_string_link(cell, 0), states);
      next = qa_chat_reply_message_next(cell);
      break;
    case QA_CHAT_CELL_PIECE:
      graph_visit(graph, qa_chat_piece_first_string(cell), states);
      next = qa_chat_piece_next(cell);
      break;
    case QA_CHAT_CELL_TEMPLATE:
      graph_visit(graph, qa_chat_template_first(cell), states);
      next = qa_chat_template_next(cell);
      break;
    case QA_CHAT_CELL_KEY:
      graph_visit(graph, cell_string_link(cell, 4), states);
      graph_visit(graph, qa_chat_key_match(cell), states);
      next = qa_chat_key_next(cell);
      break;
    case QA_CHAT_CELL_REPLY:
      graph_visit(graph, qa_chat_reply_keys(cell), states);
      graph_visit(graph, qa_chat_reply_first_message(cell), states);
      next = qa_chat_reply_next(cell);
      break;
    case QA_CHAT_CELL_STRING:
      break;
    }
    cell = next;
  }
  for (size_t i = 0; i < chain_count; i++)
    states[graph_cell_index(graph, chain[i])] = VISIT_DONE;
  free(chain);
}
static qa_chat_graph *graph_restore(const qa_save_value *value, qa_bot_memory *memory,
                                    qa_bot_memory_restored *restored) {
  static const char *const kinds[] = {"string", "match-string", "piece", "template",
                                      "key",    "message",      "reply"};
  qa_save_reader reader, entry, field;
  reader_init(&reader, value, "bot.chat.graph");
  qa_chat_graph *graph = graph_create(memory);
  bool sequential = true;
  for (size_t i = 0, n = qa_save_reader_list_count(&reader); i < n; i++) {
    item_reader(&reader, i, &entry);
    double pointer = field_integer(&entry, "pointer", 1);
    double reference = field_integer(&entry, "allocation", 0);
    field_reader(&entry, "kind", &field);
    const char *kind_name = qa_save_reader_choice_string(&field, kinds, 7);
    qa_chat_cell_kind kind = QA_CHAT_CELL_STRING;
    while (strcmp(cell_kind_names[kind], kind_name) != 0)
      kind++;
    qa_save_reader_destroy(&field);
    double offset = field_integer(&entry, "offset", 0);
    qa_save_reader_destroy(&entry);
    /* Pointer words are 32 bits; a larger saved key could never be reached. */
    if (pointer < 1 || pointer > (double)UINT32_MAX)
      qa_range_error("Invalid saved chat graph pointer");
    /* Images saved by this code list pointers 1..n, which cannot repeat. */
    if ((uint32_t)pointer != graph->count + 1 || !sequential) {
      sequential = false;
      for (size_t j = 0; j < graph->count; j++)
        if (graph->cells[j]->pointer == (uint32_t)pointer)
          qa_range_error("Invalid saved chat graph pointer");
    }
    qa_bot_memory_allocation allocation =
        qa_bot_memory_restored_allocation(restored, (int64_t)reference);
    if (kind == QA_CHAT_CELL_STRING) {
      if (offset < 0 || offset >= (double)allocation.length)
        qa_range_error("Invalid saved chat string offset");
      qa_chat_cell probe = {graph, kind, allocation, (uint32_t)pointer, (int64_t)offset};
      string_cell_text(&probe);
    }
    graph_register(graph, kind, &allocation, (uint32_t)pointer,
                   kind == QA_CHAT_CELL_STRING ? (int64_t)offset : 0);
  }
  qa_save_reader_destroy(&reader);
  uint8_t *states = xcalloc(graph->count, 1);
  for (size_t i = 0; i < graph->count; i++)
    graph_visit(graph, graph->cells[i], states);
  free(states);
  return graph;
}
static qa_chat_cell *restore_root(const qa_save_value *value, qa_bot_memory *memory,
                                  qa_bot_memory_restored *restored, const char *path,
                                  qa_chat_cell_kind kind, const char *wrong, const char *no_root) {
  qa_save_reader reader, field;
  reader_init(&reader, value, path);
  field_reader(&reader, "pointers", &field);
  qa_chat_graph *graph = graph_restore(field.value, memory, restored);
  qa_save_reader_destroy(&field);
  double root = field_integer(&reader, "root", 1);
  qa_save_reader_destroy(&reader);
  if (root > (double)UINT32_MAX)
    qa_range_error("chat pointer is outside its allocated graph");
  qa_chat_cell *cell = graph_at(graph, (uint32_t)root, kind, wrong);
  if (cell == NULL)
    qa_range_error("%s", no_root);
  return cell;
}
qa_chat_cell *qa_chat_template_restore_graph(const qa_save_value *value, qa_bot_memory *memory,
                                             qa_bot_memory_restored *restored) {
  return restore_root(value, memory, restored, "bot.chat.matches", QA_CHAT_CELL_TEMPLATE,
                      "a match template", "Saved chat match graph has no root");
}
qa_chat_cell *qa_chat_reply_restore_graph(const qa_save_value *value, qa_bot_memory *memory,
                                          qa_bot_memory_restored *restored) {
  return restore_root(value, memory, restored, "bot.chat.replies", QA_CHAT_CELL_REPLY, "a reply",
                      "Saved chat reply graph has no root");
}

/* ---- ChatDataParser ---- */
typedef enum error_cleanup { CLEANUP_OUTER, CLEANUP_PIECES, CLEANUP_GRAPHS } error_cleanup;
struct qa_chat_data_parser {
  qa_ui_script_source_reader *source;
  bool source_disposed;
  qa_chat_data_memory *memory;
  qa_chat_data_memory own_data_memory;
  qa_chat_graph *graph;
  qa_ui_script_report_fn report;
  void *report_ctx;
  qa_ui_script_diagnostic *issues;
  size_t issue_count;
  char *location_path;
  int64_t location_line, location_column;
  qa_chat_cell *match_head, *reply_head, *unfinished_pieces;
  qa_ui_script_token_record last_record;
  bool has_last_record;
  qa_chat_parser_failure failure;
  char *failure_message;
  char *failure_chat_name;
  error_cleanup cleanup;
  jmp_buf *fail_jump;
  char **scratch; /* TS strings still needed across a parser jump */
  size_t scratch_count;
};

static char *parser_keep(qa_chat_data_parser *p, char *text) {
  p->scratch = xrealloc(p->scratch, (p->scratch_count + 1) * sizeof *p->scratch);
  p->scratch[p->scratch_count++] = text;
  return text;
}
qa_chat_data_parser *qa_chat_data_parser_create(qa_ui_script_source_reader *source,
                                                const char *path, qa_chat_data_memory *memory,
                                                qa_bot_memory *bot_memory,
                                                qa_ui_script_report_fn report, void *report_ctx) {
  qa_chat_data_parser *p = xcalloc(1, sizeof *p);
  p->source = source;
  qa_chat_data_memory_init(&p->own_data_memory, NULL);
  p->memory = memory != NULL ? memory : &p->own_data_memory;
  p->graph = graph_create(bot_memory);
  p->report = report;
  p->report_ctx = report_ctx;
  p->location_path = xdup(path);
  p->location_line = 1;
  p->location_column = 1;
  return p;
}
void qa_chat_data_parser_destroy(qa_chat_data_parser *p) {
  if (p == NULL)
    return;
  if (p->has_last_record)
    qa_ui_script_token_record_release(&p->last_record);
  if (p->source_disposed)
    qa_ui_script_source_reader_destroy(p->source);
  for (size_t i = 0; i < p->issue_count; i++)
    qa_ui_script_diagnostic_destroy(&p->issues[i]);
  free(p->issues);
  free(p->location_path);
  free(p->failure_message);
  free(p->failure_chat_name);
  for (size_t i = 0; i < p->scratch_count; i++)
    free(p->scratch[i]);
  free(p->scratch);
  qa_chat_graph_release(p->graph);
  free(p);
}
void qa_chat_data_parser_dispose(qa_chat_data_parser *p) {
  qa_ui_script_source_reader_dispose(p->source);
  p->source_disposed = true;
}
qa_chat_parser_failure qa_chat_data_parser_failure(const qa_chat_data_parser *p) {
  return p->failure;
}
const char *qa_chat_data_parser_failure_message(const qa_chat_data_parser *p) {
  return p->failure_message;
}
const char *qa_chat_data_parser_failure_chat_name(const qa_chat_data_parser *p) {
  return p->failure_chat_name;
}
size_t qa_chat_data_parser_diagnostic_count(const qa_chat_data_parser *p) {
  size_t source_count = 0;
  qa_ui_script_source_reader_diagnostics(p->source, &source_count);
  return source_count + p->issue_count;
}
const qa_ui_script_diagnostic *qa_chat_data_parser_diagnostic(const qa_chat_data_parser *p,
                                                              size_t index) {
  size_t source_count = 0;
  const qa_ui_script_diagnostic *source =
      qa_ui_script_source_reader_diagnostics(p->source, &source_count);
  return index < source_count ? &source[index] : &p->issues[index - source_count];
}

typedef struct read_next {
  qa_ui_script_source_reader *source;
  qa_ui_script_token_record record;
  bool read;
} read_next;
static void run_read_next(void *ctx) {
  read_next *next = ctx;
  next->read = qa_ui_script_source_reader_next(next->source, &next->record);
}
static qa_ui_script_token *parser_next(qa_chat_data_parser *p) {
  if (p->has_last_record) {
    qa_ui_script_token_record_release(&p->last_record);
    p->has_last_record = false;
  }
  /* PC_ReadToken returns false for a source failure; a later check may read again. */
  read_next next = {p->source, {0}, false};
  if (qa_ui_script_source_reader_catch(p->source, run_read_next, &next) && next.read) {
    p->last_record = next.record;
    p->has_last_record = true;
  }
  qa_ui_script_token *token = p->has_last_record ? p->last_record.token : NULL;
  char filename[1024];
  int64_t line = 0;
  qa_ui_script_source_reader_position(p->source, filename, sizeof filename, &line);
  qa_ui_script_source_reader_current_filename(p->source, filename, sizeof filename);
  char *path = xdup(filename);
  free(p->location_path);
  p->location_path = path;
  p->location_line = line;
  if (token != NULL)
    p->location_column = token->location.column;
  return token;
}
static void parser_unread(qa_chat_data_parser *p, const qa_ui_script_token *token) {
  if (!p->has_last_record || p->last_record.token != token)
    qa_range_error("chat parser can only unread its last source token");
  qa_ui_script_source_reader_unread(p->source, &p->last_record);
}
static qa_ui_script_diagnostic *parser_add_issue(qa_chat_data_parser *p,
                                                 qa_ui_script_severity severity, char *message) {
  p->issues = xrealloc(p->issues, (p->issue_count + 1) * sizeof *p->issues);
  qa_ui_script_diagnostic *issue = &p->issues[p->issue_count++];
  issue->severity = severity;
  issue->message = message;
  issue->location.path = xdup(p->location_path);
  issue->location.line = p->location_line;
  issue->location.column = p->location_column;
  return issue;
}
_Noreturn static void parser_fail_text(qa_chat_data_parser *p, char *message) {
  qa_ui_script_diagnostic *issue = parser_add_issue(p, QA_UI_SCRIPT_SEVERITY_ERROR, message);
  /* Issues may grow during the report; hand out a stable copy. */
  qa_ui_script_diagnostic copy = *issue;
  if (p->report != NULL)
    p->report(p->report_ctx, &copy);
  p->failure = QA_CHAT_PARSER_LANGUAGE_ERROR;
  free(p->failure_message);
  /* TS ScriptLanguageError.message. */
  p->failure_message = xformat("%s:%lld:%lld: %s", copy.location.path, (long long)copy.location.line,
                               (long long)copy.location.column, message);
  longjmp(*p->fail_jump, 1);
}
#define parser_fail(p, ...) parser_fail_text(p, xformat(__VA_ARGS__))
_Noreturn static void parser_empty_pieces(qa_chat_data_parser *p, const char *message) {
  p->cleanup = CLEANUP_GRAPHS;
  p->failure = QA_CHAT_PARSER_EMPTY_PIECES;
  free(p->failure_message);
  p->failure_message = xdup(message);
  longjmp(*p->fail_jump, 1);
}
static void parser_warn(qa_chat_data_parser *p, char *message) {
  qa_ui_script_diagnostic copy = *parser_add_issue(p, QA_UI_SCRIPT_SEVERITY_WARNING, message);
  if (p->report != NULL)
    p->report(p->report_ctx, &copy);
}
static bool token_is(const qa_ui_script_token *token, const char *text) {
  size_t length = strlen(text);
  return token->text_length == length && memcmp(token->text, text, length) == 0;
}
static qa_ui_script_token *parser_required(qa_chat_data_parser *p) {
  qa_ui_script_token *token = parser_next(p);
  if (token == NULL)
    parser_fail(p, "couldn't read expected token");
  return token;
}
static bool parser_check(qa_chat_data_parser *p, const char *text) {
  qa_ui_script_token *token = parser_next(p);
  if (token == NULL)
    return false;
  if (token_is(token, text))
    return true;
  parser_unread(p, token);
  return false;
}
static void parser_expect(qa_chat_data_parser *p, const char *text) {
  qa_ui_script_token *token = parser_next(p);
  if (token == NULL)
    parser_fail(p, "couldn't find expected %s", text);
  if (!token_is(token, text))
    parser_fail(p, "expected %s, found %s", text, token->text);
}
/* TS stringValue cuts at the first NUL; the copy lives as long as the parser. */
static const char *token_string_value(qa_chat_data_parser *p, const qa_ui_script_token *token) {
  return parser_keep(p, xdup(token->value));
}
static const char *parser_string(qa_chat_data_parser *p) {
  qa_ui_script_token *token = parser_required(p);
  if (token->kind != QA_UI_SCRIPT_KIND_STRING)
    parser_fail(p, "expected a string, found %s", token->text);
  return token_string_value(p, token);
}
static double parser_number(qa_chat_data_parser *p, bool integer) {
  qa_ui_script_token *token = parser_required(p);
  if (token->kind != QA_UI_SCRIPT_KIND_NUMBER)
    parser_fail(p, "expected a number, found %s", token->text);
  if (integer && (token->flags & QA_UI_SCRIPT_NUMBER_INTEGER) == 0)
    parser_fail(p, "expected integer, found %s", token->text);
  double value = integer ? (double)token->integer_value : (double)(float)token->float_value;
  if (!isfinite(value))
    qa_range_error("chat number exceeds finite source range");
  return value;
}
/* Returns a parser-lifetime string. */
static const char *parser_message(qa_chat_data_parser *p) {
  char message[QA_CHAT_MESSAGE_SIZE];
  size_t length = 0;
  message[0] = '\0';
  while (true) {
    qa_ui_script_token *token = parser_required(p);
    char *text;
    if (token->kind == QA_UI_SCRIPT_KIND_STRING) {
      text = xdup(token->value);
    } else if (token->kind == QA_UI_SCRIPT_KIND_NUMBER &&
               (token->flags & QA_UI_SCRIPT_NUMBER_INTEGER) != 0) {
      if (length + 7 > QA_CHAT_MESSAGE_SIZE)
        parser_fail(p, "chat message too long\n");
      text = xformat(QA_CHAT_ESCAPE "v%u" QA_CHAT_ESCAPE, (unsigned)token->integer_value);
    } else if (token->kind == QA_UI_SCRIPT_KIND_NAME) {
      if (length + 7 > QA_CHAT_MESSAGE_SIZE)
        parser_fail(p, "chat message too long\n");
      text = xformat(QA_CHAT_ESCAPE "r%s" QA_CHAT_ESCAPE, token->value);
    } else {
      parser_fail(p, "unknown message component %s\n", token->text);
    }
    size_t text_length = strlen(text);
    if (length + text_length >= QA_CHAT_MESSAGE_SIZE) {
      bool is_string = token->kind == QA_UI_SCRIPT_KIND_STRING;
      free(text);
      if (is_string)
        parser_fail(p, "chat message too long\n");
      qa_range_error("encoded chat message exceeds the source 256-byte buffer");
    }
    memcpy(message + length, text, text_length + 1);
    length += text_length;
    free(text);
    if (parser_check(p, ";"))
      return parser_keep(p, xdup(message));
    parser_expect(p, ",");
  }
}

static void synonyms_body(qa_chat_data_parser *p, qa_stored_synonyms *out) {
  uint32_t contexts[32];
  size_t context_count = 0;
  uint32_t context = 0;
  qa_ui_script_token *token;
  qa_chat_data_memory *memory = p->memory;
  while ((token = parser_next(p)) != NULL) {
    if (token->kind == QA_UI_SCRIPT_KIND_NUMBER) {
      context |= token->integer_value;
      contexts[context_count++] = token->integer_value;
      if (context_count >= 32)
        parser_fail(p, "more than 32 context levels");
      parser_expect(p, "{");
    } else if (token->kind == QA_UI_SCRIPT_KIND_PUNCTUATION) {
      if (token_is(token, "}")) {
        if (context_count == 0)
          parser_fail(p, "too many }");
        context &= ~contexts[--context_count];
      } else if (token_is(token, "[")) {
        int64_t record = qa_chat_data_memory_reserve(memory, 16);
        data_integer(memory, record, context);
        data_float(memory, record + 4, 0);
        /* The growing group is published in *out so a jump can reclaim it. */
        out->groups = xrealloc(out->groups, (out->count + 1) * sizeof *out->groups);
        qa_stored_synonym_group *group = &out->groups[out->count++];
        memset(group, 0, sizeof *group);
        group->record = record;
        group->fixed_context = context;
        float total_weight = 0;
        while (true) {
          parser_expect(p, "(");
          const char *text = parser_string(p);
          if (text[0] == '\0')
            parser_fail(p, "empty string");
          int64_t entry_record = qa_chat_data_memory_reserve(memory, 12);
          qa_stored_text stored = data_string(memory, text);
          group->entries = xrealloc(group->entries, (group->entry_count + 1) * sizeof *group->entries);
          qa_stored_synonym *entry = &group->entries[group->entry_count++];
          memset(entry, 0, sizeof *entry);
          entry->text = stored;
          entry->record = entry_record;
          parser_expect(p, ",");
          double weight = parser_number(p, false);
          parser_expect(p, ")");
          total_weight = (float)((double)total_weight + weight);
          entry->fixed_weight = (float)weight;
          group->fixed_total_weight = total_weight;
          data_float(memory, entry_record + 4, weight);
          data_float(memory, record + 4, total_weight);
          if (parser_check(p, "]"))
            break;
          parser_expect(p, ",");
        }
        if (group->entry_count < 2)
          parser_fail(p, "synonym must have at least two entries\n");
      } else {
        parser_fail(p, "unexpected %s", token->text);
      }
    }
  }
  /* The source reports the unclosed context and returns no synonyms, like any other syn.c error. */
  if (context_count != 0)
    parser_fail(p, "unclosed context\n");
  qa_chat_data_parser_dispose(p);
}

static void randoms_body(qa_chat_data_parser *p, qa_stored_randoms *out) {
  qa_ui_script_token *token;
  qa_chat_data_memory *memory = p->memory;
  while ((token = parser_next(p)) != NULL) {
    if (token->kind != QA_UI_SCRIPT_KIND_NAME)
      parser_fail(p, "unknown random %s", token->text);
    int64_t record = qa_chat_data_memory_reserve(memory, 16);
    qa_stored_text name = data_string(memory, token->value);
    data_integer(memory, record + 4, 0);
    out->lists = xrealloc(out->lists, (out->count + 1) * sizeof *out->lists);
    qa_stored_random_list *list = &out->lists[out->count++];
    memset(list, 0, sizeof *list);
    list->name = name;
    list->record = record;
    parser_expect(p, "=");
    parser_expect(p, "{");
    while (!parser_check(p, "}")) {
      const char *message = parser_message(p);
      qa_chat_data_memory_reserve(memory, 8);
      qa_stored_text stored = data_string(memory, message);
      list->messages = xrealloc(list->messages, (list->message_count + 1) * sizeof *list->messages);
      memmove(list->messages + 1, list->messages, list->message_count * sizeof *list->messages);
      list->messages[0] = stored; /* TS messages.unshift */
      list->message_count++;
      list->fixed_count = (uint32_t)list->message_count;
      data_integer(memory, record + 4, (uint32_t)list->message_count);
    }
  }
}

static qa_chat_cell *pieces_body(qa_chat_data_parser *p, const char *end) {
  p->cleanup = CLEANUP_PIECES;
  p->unfinished_pieces = NULL;
  qa_chat_cell *last_piece = NULL;
  bool last_was_variable = false;
  qa_ui_script_token *token;
  while ((token = parser_next(p)) != NULL) {
    if (token->kind == QA_UI_SCRIPT_KIND_NUMBER &&
        (token->flags & QA_UI_SCRIPT_NUMBER_INTEGER) != 0) {
      if (token->integer_value >= 8)
        parser_fail(p, "can't have more than 8 match variables\n");
      if (last_was_variable)
        parser_fail(p, "not allowed to have adjacent variables\n");
      last_was_variable = true;
      qa_chat_cell *piece = graph_allocate(p->graph, QA_CHAT_CELL_PIECE, 16);
      cell_set_u32(piece, 0, 1);
      cell_set_u32(piece, 8, token->integer_value);
      if (last_piece == NULL)
        p->unfinished_pieces = piece;
      else
        cell_set_link(last_piece, 12, piece);
      last_piece = piece;
    } else if (token->kind == QA_UI_SCRIPT_KIND_STRING) {
      qa_chat_cell *piece = graph_allocate(p->graph, QA_CHAT_CELL_PIECE, 16);
      cell_set_u32(piece, 0, 2);
      if (last_piece == NULL)
        p->unfinished_pieces = piece;
      else
        cell_set_link(last_piece, 12, piece);
      last_piece = piece;
      qa_chat_cell *last_string = NULL;
      bool empty = false;
      do {
        const char *text = last_string == NULL ? token_string_value(p, token) : parser_string(p);
        qa_chat_cell *value = graph_new_match_string(p->graph, text);
        if (text[0] == '\0')
          empty = true;
        if (last_string == NULL)
          cell_set_link(piece, 4, value);
        else
          cell_set_link(last_string, 4, value);
        last_string = value;
      } while (parser_check(p, "|"));
      if (!empty)
        last_was_variable = false;
    } else {
      parser_fail(p, "invalid token %s\n", token->text);
    }
    if (parser_check(p, end))
      break;
    parser_expect(p, ",");
  }
  qa_chat_cell *first = p->unfinished_pieces;
  p->unfinished_pieces = NULL;
  p->cleanup = CLEANUP_OUTER;
  return first;
}

static void match_templates_body(qa_chat_data_parser *p) {
  p->match_head = NULL;
  qa_chat_cell *last_match = NULL;
  while (true) {
    qa_ui_script_token *first = parser_next(p);
    if (first == NULL)
      return;
    if (first->kind != QA_UI_SCRIPT_KIND_NUMBER || (first->flags & QA_UI_SCRIPT_NUMBER_INTEGER) == 0)
      parser_fail(p, "expected integer, found %s\n", first->text);
    uint32_t context = first->integer_value;
    parser_expect(p, "{");
    qa_ui_script_token *token;
    while ((token = parser_next(p)) != NULL) {
      if (token_is(token, "}"))
        break;
      qa_ui_script_source_reader_unread_last(p->source);
      qa_chat_cell *match = graph_allocate(p->graph, QA_CHAT_CELL_TEMPLATE, 20);
      cell_set_u32(match, 0, context);
      if (last_match == NULL)
        p->match_head = match;
      else
        cell_set_link(last_match, 16, match);
      last_match = match;
      qa_chat_cell *pieces = pieces_body(p, "=");
      cell_set_link(match, 12, pieces);
      if (pieces == NULL)
        parser_empty_pieces(p, "empty match template");
      parser_expect(p, "(");
      cell_set_u32(match, 4, (uint32_t)(int64_t)parser_number(p, true));
      parser_expect(p, ",");
      cell_set_u32(match, 8, (uint32_t)(int64_t)parser_number(p, true));
      parser_expect(p, ")");
      parser_expect(p, ";");
    }
  }
}

static void reply_warnings(qa_chat_data_parser *p, const qa_chat_cell *keys) {
  bool all_prefixed = true, has_variables = false, has_string = false;
  for (const qa_chat_cell *key = keys; key != NULL; key = qa_chat_key_next(key)) {
    int32_t flags = qa_chat_key_flags(key);
    if ((flags & 3) == 0) {
      all_prefixed = false;
      if ((flags & 16) != 0) {
        for (const qa_chat_cell *piece = qa_chat_key_match(key); piece != NULL;
             piece = qa_chat_piece_next(piece))
          if (qa_chat_piece_type(piece) == 1)
            has_variables = true;
      } else if ((flags & 8) != 0) {
        has_string = true;
      }
    } else if ((flags & 1) != 0 && (flags & 8) != 0) {
      for (const qa_chat_cell *other = keys; other != NULL; other = qa_chat_key_next(other)) {
        int32_t other_flags = qa_chat_key_flags(other);
        if (other == key || (other_flags & 2) != 0 || (other_flags & 16) == 0)
          continue;
        bool space = false;
        for (const qa_chat_cell *piece = qa_chat_key_match(other); piece != NULL;
             piece = qa_chat_piece_next(piece)) {
          int32_t type = qa_chat_piece_type(piece);
          if (type == 2) {
            for (const qa_chat_cell *value = qa_chat_piece_first_string(piece); value != NULL;
                 value = qa_chat_match_string_next(value)) {
              if (contains_chat_text(qa_chat_match_string_text(value), qa_chat_key_text(key))) {
                space = true;
                break;
              }
            }
            if (space)
              break;
          } else if (type == 1) {
            space = true;
            break;
          }
        }
        if (!space)
          parser_warn(p, xformat("one of the match templates does not leave space for the key %s "
                                 "with the & prefix",
                                 qa_chat_key_text(key)));
      }
    }
    if ((flags & 2) != 0 && (flags & 8) != 0) {
      for (const qa_chat_cell *other = keys; other != NULL; other = qa_chat_key_next(other)) {
        int32_t other_flags = qa_chat_key_flags(other);
        if (other == key || (other_flags & 2) != 0)
          continue;
        if ((other_flags & 8) != 0) {
          if (contains_chat_text(qa_chat_key_text(other), qa_chat_key_text(key)))
            parser_warn(p, xformat("the key %s with prefix ! is inside the key %s",
                                   qa_chat_key_text(key), qa_chat_key_text(other)));
        } else if ((other_flags & 16) != 0) {
          for (const qa_chat_cell *piece = qa_chat_key_match(other); piece != NULL;
               piece = qa_chat_piece_next(piece)) {
            if (qa_chat_piece_type(piece) != 2)
              continue;
            for (const qa_chat_cell *value = qa_chat_piece_first_string(piece); value != NULL;
                 value = qa_chat_match_string_next(value)) {
              if (contains_chat_text(qa_chat_match_string_text(value), qa_chat_key_text(key)))
                parser_warn(p, xformat("the key %s with prefix ! is inside the match template string %s",
                                       qa_chat_key_text(key), qa_chat_match_string_text(value)));
            }
          }
        }
      }
    }
  }
  if (all_prefixed)
    parser_warn(p, xdup("all keys have a & or ! prefix"));
  if (has_variables && has_string)
    parser_warn(p, xdup("variables from the match template(s) could be invalid when outputting one "
                        "of the chat messages"));
}

static void reply_chats_body(qa_chat_data_parser *p) {
  p->reply_head = NULL;
  while (true) {
    qa_ui_script_token *first = parser_next(p);
    if (first == NULL)
      return;
    if (!token_is(first, "["))
      parser_fail(p, "expected [, found %s", first->text);
    qa_chat_cell *reply = graph_allocate(p->graph, QA_CHAT_CELL_REPLY, 20);
    cell_set_link(reply, 16, p->reply_head);
    p->reply_head = reply;
    do {
      qa_chat_cell *key = graph_allocate(p->graph, QA_CHAT_CELL_KEY, 16);
      cell_set_link(key, 12, qa_chat_reply_keys(reply));
      cell_set_link(reply, 0, key);
      uint32_t flags = 0;
      if (parser_check(p, "&"))
        flags |= 1;
      else if (parser_check(p, "!"))
        flags |= 2;
      if (parser_check(p, "name")) {
        flags |= 4;
        cell_set_u32(key, 0, flags);
      } else if (parser_check(p, "female")) {
        flags |= 64;
        cell_set_u32(key, 0, flags);
      } else if (parser_check(p, "male")) {
        flags |= 128;
        cell_set_u32(key, 0, flags);
      } else if (parser_check(p, "it")) {
        flags |= 256;
        cell_set_u32(key, 0, flags);
      } else if (parser_check(p, "(")) {
        flags |= 16;
        cell_set_u32(key, 0, flags);
        qa_chat_cell *pieces = pieces_body(p, ")");
        cell_set_link(key, 8, pieces);
        if (pieces == NULL)
          parser_empty_pieces(p, "empty reply match template");
      } else if (parser_check(p, "<")) {
        flags |= 32;
        cell_set_u32(key, 0, flags);
        const char *joined = parser_string(p);
        /* TS reduce: an empty accumulated result restarts from the next name. */
        while (parser_check(p, ",")) {
          const char *name = parser_string(p);
          joined = joined[0] == '\0' ? name : parser_keep(p, xformat("%s\\%s", joined, name));
        }
        parser_expect(p, ">");
        if (strlen(joined) >= QA_CHAT_MESSAGE_SIZE)
          qa_range_error("bot name key exceeds the source 256-byte buffer");
        cell_set_link(key, 4, graph_new_string(p->graph, joined));
      } else {
        flags |= 8;
        cell_set_u32(key, 0, flags);
        cell_set_link(key, 4, graph_new_string(p->graph, parser_string(p)));
      }
      parser_check(p, ",");
    } while (!parser_check(p, "]"));
    reply_warnings(p, qa_chat_reply_keys(reply));
    parser_expect(p, "=");
    cell_set_u32(reply, 4, f32_bits(parser_number(p, false)));
    parser_expect(p, "{");
    cell_set_u32(reply, 8, 0);
    while (!parser_check(p, "}")) {
      qa_chat_cell *message = graph_new_message(p->graph, parser_message(p));
      cell_set_u32(message, 4, f32_bits(-40));
      cell_set_link(message, 8, qa_chat_reply_first_message(reply));
      cell_set_link(reply, 12, message);
      cell_set_u32(reply, 8, cell_u32(reply, 8) + 1);
    }
  }
}

void qa_chat_data_parser_free_graphs(qa_chat_data_parser *p) {
  if (p->unfinished_pieces != NULL)
    qa_chat_piece_free(p->unfinished_pieces);
  p->unfinished_pieces = NULL;
  if (p->match_head != NULL)
    qa_chat_template_free(p->match_head);
  p->match_head = NULL;
  if (p->reply_head != NULL)
    qa_chat_reply_free(p->reply_head);
  p->reply_head = NULL;
}
void qa_chat_data_parser_free_after_error(qa_chat_data_parser *p) {
  /* BotLoadMatchPieces releases the source before its unfinished graph;
   * the outer match/reply loaders free their graphs before FreeSource. */
  if (p->cleanup == CLEANUP_PIECES)
    qa_chat_data_parser_dispose(p);
  qa_chat_data_parser_free_graphs(p);
  if (p->cleanup == CLEANUP_OUTER)
    qa_chat_data_parser_dispose(p);
}

/* TS initial(): stored NULL builds the snapshot overload. */
static void initial_body(qa_chat_data_parser *p, qa_chat_text_source name, bool previously_found,
                         qa_chat_initial *stored, qa_initial_chat *snapshot) {
  qa_chat_data_memory *memory = p->memory;
  qa_chat_data_memory_reserve(memory, 4);
  bool found = previously_found;
  while (true) {
    qa_ui_script_token *token = parser_next(p);
    if (token == NULL)
      break;
    if (!token_is(token, "chat"))
      parser_fail(p, "unknown definition %s\n", token->text);
    const char *chat_name = parser_string(p);
    parser_expect(p, "{");
    if (!qa_chat_text_equals_source(chat_name, name, false)) {
      int depth = 1;
      while (depth > 0) {
        qa_ui_script_token *skipped = parser_required(p);
        if (token_is(skipped, "{"))
          depth++;
        else if (token_is(skipped, "}"))
          depth--;
      }
      continue;
    }
    found = true;
    while (true) {
      qa_ui_script_token *type_token = parser_required(p);
      if (token_is(type_token, "}"))
        break;
      if (!token_is(type_token, "type"))
        parser_fail(p, "expected type found %s\n", type_token->text);
      const char *type_name = parser_string(p);
      parser_expect(p, "{");
      int64_t type_offset = qa_chat_data_memory_reserve(memory, 44);
      uint32_t type = stored != NULL ? initial_add_type(stored, type_offset, type_name) : 0;
      qa_chat_type *entry = NULL;
      if (stored == NULL) { /* TS types.unshift, kept in *snapshot for a jump */
        snapshot->types = xrealloc(snapshot->types, (snapshot->type_count + 1) * sizeof *snapshot->types);
        memmove(snapshot->types + 1, snapshot->types, snapshot->type_count * sizeof *snapshot->types);
        snapshot->type_count++;
        entry = &snapshot->types[0];
        memset(entry, 0, sizeof *entry);
        entry->name = xdup(type_name);
      }
      while (!parser_check(p, "}")) {
        const char *text = parser_message(p);
        int64_t message_offset = qa_chat_data_memory_reserve(memory, 12);
        if (stored != NULL)
          initial_add_message(stored, type, message_offset);
        int64_t text_offset = qa_chat_data_memory_reserve(memory, (int64_t)strlen(text) + 1);
        if (stored != NULL) {
          initial_write_message(stored, type, text_offset, text);
        } else {
          entry->messages = xrealloc(entry->messages, (entry->message_count + 1) * sizeof *entry->messages);
          memmove(entry->messages + 1, entry->messages, entry->message_count * sizeof *entry->messages);
          entry->messages[0].text = xdup(text);
          entry->messages[0].time = -40;
          entry->message_count++;
        }
      }
    }
  }
  qa_chat_data_parser_dispose(p);
  if (!found) {
    const char *chat_name = qa_chat_text_read(name, -1);
    p->failure = QA_CHAT_PARSER_INITIAL_NOT_FOUND;
    free(p->failure_chat_name);
    p->failure_chat_name = xdup(chat_name);
    free(p->failure_message);
    p->failure_message = xformat("couldn't find chat %s", chat_name);
    longjmp(*p->fail_jump, 1);
  }
}

/* Parser failures jump here; a body's partial output is released by the caller. */
#define PARSER_GUARD(p, body)                                                                      \
  do {                                                                                             \
    jmp_buf jump;                                                                                  \
    jmp_buf *previous = (p)->fail_jump;                                                            \
    (p)->fail_jump = &jump;                                                                        \
    if (setjmp(jump) != 0) {                                                                       \
      (p)->fail_jump = previous;                                                                   \
      return false;                                                                                \
    }                                                                                              \
    body;                                                                                          \
    (p)->fail_jump = previous;                                                                     \
  } while (0)

static bool guarded_synonyms(qa_chat_data_parser *p, qa_stored_synonyms *out) {
  PARSER_GUARD(p, synonyms_body(p, out));
  return true;
}
bool qa_chat_data_parser_synonyms(qa_chat_data_parser *p, qa_stored_synonyms *out) {
  memset(out, 0, sizeof *out);
  bool ok = guarded_synonyms(p, out);
  if (p->memory->has_allocation) {
    out->allocation = p->memory->allocation;
    out->has_allocation = true;
  }
  if (!ok)
    qa_stored_synonyms_destroy(out);
  return ok;
}
static bool guarded_randoms(qa_chat_data_parser *p, qa_stored_randoms *out) {
  PARSER_GUARD(p, randoms_body(p, out));
  return true;
}
bool qa_chat_data_parser_randoms(qa_chat_data_parser *p, qa_stored_randoms *out) {
  memset(out, 0, sizeof *out);
  bool ok = guarded_randoms(p, out);
  if (p->memory->has_allocation) {
    out->allocation = p->memory->allocation;
    out->has_allocation = true;
  }
  if (!ok)
    qa_stored_randoms_destroy(out);
  return ok;
}
bool qa_chat_data_parser_match_templates(qa_chat_data_parser *p, qa_chat_cell **out) {
  PARSER_GUARD(p, match_templates_body(p));
  *out = p->match_head;
  return true;
}
bool qa_chat_data_parser_reply_chats(qa_chat_data_parser *p, qa_chat_cell **out) {
  PARSER_GUARD(p, reply_chats_body(p));
  *out = p->reply_head;
  return true;
}
bool qa_chat_data_parser_matches(qa_chat_data_parser *p, qa_match_template **out,
                                 size_t *out_count) {
  *out = NULL;
  *out_count = 0;
  qa_chat_cell *head = NULL;
  if (!qa_chat_data_parser_match_templates(p, &head)) {
    qa_chat_data_parser_free_after_error(p);
    return false;
  }
  for (const qa_chat_cell *match = head; match != NULL; match = qa_chat_template_next(match)) {
    *out = xrealloc(*out, (*out_count + 1) * sizeof **out);
    qa_chat_template_snapshot(match, &(*out)[(*out_count)++]);
  }
  return true;
}
bool qa_chat_data_parser_replies(qa_chat_data_parser *p, qa_reply_chat **out, size_t *out_count) {
  *out = NULL;
  *out_count = 0;
  qa_chat_cell *head = NULL;
  if (!qa_chat_data_parser_reply_chats(p, &head)) {
    qa_chat_data_parser_free_after_error(p);
    return false;
  }
  for (const qa_chat_cell *reply = head; reply != NULL; reply = qa_chat_reply_next(reply)) {
    *out = xrealloc(*out, (*out_count + 1) * sizeof **out);
    qa_chat_reply_snapshot(reply, &(*out)[(*out_count)++]);
  }
  return true;
}
static bool guarded_initial(qa_chat_data_parser *p, qa_chat_text_source name, bool found,
                            qa_chat_initial *stored, qa_initial_chat *snapshot) {
  PARSER_GUARD(p, initial_body(p, name, found, stored, snapshot));
  return true;
}
bool qa_chat_data_parser_initial(qa_chat_data_parser *p, qa_chat_text_source name,
                                 bool previously_found, qa_initial_chat *out) {
  memset(out, 0, sizeof *out);
  bool ok = guarded_initial(p, name, previously_found, NULL, out);
  if (!ok)
    qa_initial_chat_destroy(out);
  return ok;
}
bool qa_chat_data_parser_initial_into(qa_chat_data_parser *p, qa_chat_text_source name,
                                      bool previously_found, qa_chat_initial *stored) {
  return guarded_initial(p, name, previously_found, stored, NULL);
}
