/* See header. GPL-2.0-or-later. */
#include "qa/bots/behavior/library/memory.h"
#include "qa/foundation/support.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct rec {
  struct rec *next;
  uint8_t *backing;
  size_t size;
  qa_bot_memory_kind kind;
  bool live;
} rec;
void qa_bot_memory_init(qa_bot_memory *m) { memset(m, 0, sizeof *m); }
static void release_rec(qa_bot_memory *m, rec *r) {
  if (!r->live)
    return;
  memset(r->backing, 0xaa, r->size + 4);
  free(r->backing);
  r->backing = NULL;
  r->live = false;
  m->live_count--;
  m->allocated_bytes -= r->size;
}
void qa_bot_memory_destroy(qa_bot_memory *m) {
  qa_bot_memory_dispose(m);
  rec *r = m->records;
  while (r) {
    rec *n = r->next;
    free(r);
    r = n;
  }
  m->records = NULL;
}
bool qa_bot_memory_allocate(qa_bot_memory *m, int64_t size, qa_bot_memory_kind kind, bool clear,
                            qa_bot_memory_allocation *out) {
  if (m->disposed)
    qa_range_error("Bot memory owner is disposed");
  if (size < 0 || size > 2147483647LL - 4)
    qa_range_error("Bot memory allocation must fit a signed source size with its four-byte prefix");
  uint8_t *b = malloc((size_t)size + 4);
  if (!b)
    return false;
  rec *r = malloc(sizeof *r);
  if (!r) {
    free(b);
    return false;
  }
  uint32_t id = kind == QA_BOT_MEMORY_HEAP ? 0x12345678u : 0x87654321u;
  memcpy(b, &id, 4);
  if (clear)
    memset(b + 4, 0, (size_t)size);
  *r = (rec){m->records, b, (size_t)size, kind, true};
  m->records = r;
  m->live_count++;
  m->allocated_bytes += (size_t)size;
  out->bytes = b + 4;
  out->length = (size_t)size;
  out->record = r;
  return true;
}
void qa_bot_memory_free(qa_bot_memory *m, qa_bot_memory_allocation *a) {
  rec *r = a ? a->record : NULL;
  if (!r || !r->live) {
    if (m->has_debug && m->debug_print) {
      m->debug_print(m->debug_ctx, "fatal", "FreeMemory: invalid memory block\n");
      return;
    }
    qa_range_error("Bot memory allocation belongs to another owner or has been freed");
  }
  uint32_t id;
  memcpy(&id, r->backing, 4);
  if (id != 0x12345678u)
    return;
  if (r->kind != QA_BOT_MEMORY_HEAP)
    qa_range_error("Bot memory heap ID does not belong to a heap allocation");
  release_rec(m, r);
  a->bytes = NULL;
  a->length = 0;
}
uint8_t *qa_bot_memory_allocation_bytes(const qa_bot_memory_allocation *a) {
  const rec *r = a->record;
  if (r == NULL || !r->live)
    qa_range_error("Bot memory allocation has been freed");
  return r->backing + 4;
}
void qa_bot_memory_reset_hunk(qa_bot_memory *m) {
  for (rec *r = m->records; r; r = r->next)
    if (r->kind == QA_BOT_MEMORY_HUNK)
      release_rec(m, r);
}
void qa_bot_memory_dispose(qa_bot_memory *m) {
  if (m->disposed)
    return;
  for (rec *r = m->records; r; r = r->next)
    release_rec(m, r);
  m->disposed = true;
}
double qa_bot_memory_available_memory(qa_bot_memory *m) {
  if (!m->has_host || !m->available_fn)
    qa_range_error("Bot AvailableMemory requires a host arena with capacity accounting");
  return m->available_fn(m->available_ctx);
}
size_t qa_bot_memory_get_live_allocations(const qa_bot_memory *m) { return m->live_count; }
size_t qa_bot_memory_get_allocated_bytes(const qa_bot_memory *m) { return m->allocated_bytes; }
void qa_bot_memory_print_used_memory_size(qa_bot_memory *m) {
  if (!m->has_debug || !m->debug_print)
    return;
  char buf[128];
  snprintf(buf, sizeof buf, "total allocated memory: %lld KB\n",
           (long long)(m->allocated_bytes >> 10));
  m->debug_print(m->debug_ctx, "message", buf);
  snprintf(buf, sizeof buf, "total botlib memory: %lld KB\n",
           (long long)((m->allocated_bytes + 4 * m->live_count) >> 10));
  m->debug_print(m->debug_ctx, "message", buf);
  snprintf(buf, sizeof buf, "total memory blocks: %zu\n", m->live_count);
  m->debug_print(m->debug_ctx, "message", buf);
}
int64_t qa_bot_memory_memory_byte_size(qa_bot_memory *m, qa_bot_memory_allocation *a) {
  if (!m->has_debug)
    qa_range_error("MemoryByteSize requires the bot memory manager profile");
  rec *r = a ? a->record : NULL;
  return (!r || !r->live) ? 0 : (int64_t)r->size + 4;
}

/* Records are prepended; the TS live Map iterates allocation order. */
bool qa_bot_memory_checkpoint(qa_bot_memory *m, qa_bot_memory_capture *out) {
  memset(out, 0, sizeof *out);
  if (m->disposed) qa_range_error("Cannot checkpoint disposed bot memory");
  out->allocations =
      calloc(m->live_count == 0 ? 1 : m->live_count, sizeof *out->allocations);
  if (out->allocations == NULL) return false;
  out->count = m->live_count;
  size_t slot = m->live_count;
  for (rec *r = m->records; r; r = r->next) {
    if (!r->live) continue;
    qa_bot_memory_allocation *a = &out->allocations[--slot];
    a->bytes = r->backing + 4;
    a->length = r->size;
    a->record = r;
  }
  qa_save_value *image = qa_save_value_object();
  qa_save_value *allocations = qa_save_value_array();
  qa_save_value *version = qa_save_value_number(1);
  if (image == NULL || allocations == NULL || version == NULL ||
      !qa_save_value_set(image, "version", version)) {
    qa_save_value_free(image);
    qa_save_value_free(allocations);
    qa_save_value_free(version);
    qa_bot_memory_capture_destroy(out);
    return false;
  }
  for (size_t i = 0; i < out->count; i++) {
    rec *r = out->allocations[i].record;
    qa_save_value *row = qa_save_value_object();
    qa_save_value *kind =
        qa_save_value_string(r->kind == QA_BOT_MEMORY_HEAP ? "heap" : "hunk");
    qa_save_value *bytes =
        qa_save_value_bytes(out->allocations[i].bytes, out->allocations[i].length);
    /* qa_bot_memory_allocate takes no provenance, so it saves as null. */
    qa_save_value *provenance = qa_save_value_null();
    if (row == NULL || kind == NULL || bytes == NULL || provenance == NULL ||
        !qa_save_value_set(row, "kind", kind) ||
        !qa_save_value_set(row, "bytes", bytes) ||
        !qa_save_value_set(row, "provenance", provenance) ||
        !qa_save_value_push(allocations, row)) {
      qa_save_value_free(row);
      qa_save_value_free(kind);
      qa_save_value_free(bytes);
      qa_save_value_free(provenance);
      qa_save_value_free(allocations);
      qa_save_value_free(image);
      qa_bot_memory_capture_destroy(out);
      return false;
    }
  }
  if (!qa_save_value_set(image, "allocations", allocations)) {
    qa_save_value_free(allocations);
    qa_save_value_free(image);
    qa_bot_memory_capture_destroy(out);
    return false;
  }
  out->image = image;
  return true;
}

void qa_bot_memory_capture_destroy(qa_bot_memory_capture *capture) {
  qa_save_value_free(capture->image);
  free(capture->allocations);
  memset(capture, 0, sizeof *capture);
}

int64_t qa_bot_memory_capture_reference(qa_bot_memory_capture *capture,
                                        qa_bot_memory_allocation *allocation) {
  for (size_t i = 0; i < capture->count; i++) {
    qa_bot_memory_allocation *a = &capture->allocations[i];
    bool same = allocation != NULL && allocation->record != NULL
                    ? allocation->record == a->record
                    : allocation != NULL && allocation->bytes == a->bytes;
    if (same) return (int64_t)i;
  }
  qa_range_error("Bot checkpoint references an unowned or freed allocation");
}

static void field_of(const qa_save_reader *reader, const char *name,
                     qa_save_reader *out) {
  if (!qa_save_reader_field(reader, name, out)) qa_range_error("out of memory");
}

void qa_bot_memory_restore(qa_bot_memory *m, const qa_save_value *value,
                           qa_bot_memory_restored *out) {
  memset(out, 0, sizeof *out);
  qa_save_reader reader, allocations_reader;
  if (!qa_save_reader_init(&reader, value, "bot.memory"))
    qa_range_error("out of memory");
  qa_save_reader version_reader;
  field_of(&reader, "version", &version_reader);
  qa_save_reader_literal_number(&version_reader, 1);
  qa_save_reader_destroy(&version_reader);
  field_of(&reader, "allocations", &allocations_reader);
  size_t count = qa_save_reader_list_count(&allocations_reader);
  const char **kinds = calloc(count == 0 ? 1 : count, sizeof *kinds);
  const uint8_t **bytes = calloc(count == 0 ? 1 : count, sizeof *bytes);
  size_t *lengths = calloc(count == 0 ? 1 : count, sizeof *lengths);
  if (kinds == NULL || bytes == NULL || lengths == NULL)
    qa_range_error("out of memory");
  static const char *const kind_choices[] = {"heap", "hunk"};
  for (size_t i = 0; i < count; i++) {
    qa_save_reader item, field;
    if (!qa_save_reader_item(&allocations_reader, i, &item))
      qa_range_error("out of memory");
    field_of(&item, "kind", &field);
    kinds[i] = qa_save_reader_choice_string(&field, kind_choices, 2);
    qa_save_reader_destroy(&field);
    field_of(&item, "bytes", &field);
    bytes[i] = qa_save_reader_bytes(&field, &lengths[i]);
    qa_save_reader_destroy(&field);
    field_of(&item, "provenance", &field);
    if (!qa_save_reader_is_null(&field)) {
      /* Provenance is validated as in TS; qa_bot_memory_allocate
       * takes no provenance, so the values are then dropped. */
      qa_save_reader sub;
      field_of(&field, "file", &sub);
      qa_save_reader_string(&sub);
      qa_save_reader_destroy(&sub);
      field_of(&field, "line", &sub);
      qa_save_reader_integer_min(&sub, 0);
      qa_save_reader_destroy(&sub);
      field_of(&field, "label", &sub);
      qa_save_reader_string(&sub);
      qa_save_reader_destroy(&sub);
    }
    qa_save_reader_destroy(&field);
    qa_save_reader_destroy(&item);
  }
  qa_save_reader_destroy(&allocations_reader);
  qa_save_reader_destroy(&reader);
  if (m->disposed || m->live_count != 0)
    qa_range_error("Bot memory restore requires a fresh owner and supported image");
  for (size_t i = 0; i < count; i++)
    if (lengths[i] > 2147483647u - 4u) qa_range_error("Invalid saved bot allocation");
  out->allocations = calloc(count == 0 ? 1 : count, sizeof *out->allocations);
  if (out->allocations == NULL) qa_range_error("out of memory");
  out->count = count;
  for (size_t i = 0; i < count; i++) {
    qa_bot_memory_kind kind =
        strcmp(kinds[i], "heap") == 0 ? QA_BOT_MEMORY_HEAP : QA_BOT_MEMORY_HUNK;
    if (!qa_bot_memory_allocate(m, (int64_t)lengths[i], kind, false,
                                &out->allocations[i]))
      qa_range_error("out of memory");
    memcpy(out->allocations[i].bytes, bytes[i], lengths[i]);
  }
  free(kinds);
  free(bytes);
  free(lengths);
}

void qa_bot_memory_restored_destroy(qa_bot_memory_restored *restored) {
  free(restored->allocations);
  memset(restored, 0, sizeof *restored);
}

qa_bot_memory_allocation qa_bot_memory_restored_allocation(
    qa_bot_memory_restored *restored, int64_t reference) {
  if (reference < 0 || (uint64_t)reference >= restored->count)
    qa_range_error("Saved bot allocation reference is invalid");
  return restored->allocations[reference];
}
