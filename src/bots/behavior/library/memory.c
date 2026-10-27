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
void qa_bot_memory_print_memory_labels(qa_bot_memory *m) {
  if (!m->has_debug)
    return;
  qa_bot_memory_print_used_memory_size(m);
  if (m->write_log)
    m->write_log(m->log_ctx, "============= Botlib memory log ==============\r\n\r\n");
}
int64_t qa_bot_memory_memory_byte_size(qa_bot_memory *m, qa_bot_memory_allocation *a) {
  if (!m->has_debug)
    qa_range_error("MemoryByteSize requires the bot memory manager profile");
  rec *r = a ? a->record : NULL;
  return (!r || !r->live) ? 0 : (int64_t)r->size + 4;
}
void qa_bot_memory_dump_memory(qa_bot_memory *m) {
  if (!m->has_debug)
    qa_range_error("DumpMemory requires the bot memory manager profile");
  for (rec *r = m->records; r; r = r->next)
    if (r->live && r->kind == QA_BOT_MEMORY_HEAP)
      release_rec(m, r);
}
