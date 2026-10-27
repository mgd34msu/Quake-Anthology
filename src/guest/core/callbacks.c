/* Port of src/guest/core/callbacks.ts. GPL-2.0-or-later. */
#include "qa/guest/core/callbacks.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

struct qa_guest_callback_entry {
  uint64_t handle; /* bindEntry removal handle; 0 for bind() entries */
  qa_callback_id id;
  qa_guest_call_signature signature;
  qa_guest_address address;
  const qa_guest_host_callback *callback; /* NULL = unbound */
  bool (*accepts)(void *ctx);
  void *accepts_ctx;
  bool via_bind; /* present in #byId */
  struct entry_observer { uint64_t handle; void (*before)(void *ctx); void *ctx; bool removed; struct entry_observer *next; } *observers;
  struct qa_guest_callback_entry *next;
};
/* Entry observers keyed by address (TS #entryObservers). */
typedef struct observer_bucket {
  uint64_t byte_offset;
  struct entry_observer *observers;
  struct observer_bucket *next;
} observer_bucket;
static observer_bucket *bucket_of(qa_guest_callback_table *table, uint64_t byte_offset, bool create) {
  observer_bucket **head = (observer_bucket **)&table->observer_buckets;
  for (observer_bucket *b = *head; b != NULL; b = b->next) {
    if (b->byte_offset == byte_offset) return b;
  }
  if (!create) return NULL;
  observer_bucket *bucket = (observer_bucket *)calloc(1, sizeof *bucket);
  if (bucket == NULL) qa_range_error("Guest memory allocation failed");
  bucket->byte_offset = byte_offset;
  bucket->next = *head;
  *head = bucket;
  return bucket;
}
static bool bucket_has_observers(const observer_bucket *bucket) {
  if (bucket == NULL) return false;
  for (const struct entry_observer *o = bucket->observers; o != NULL; o = o->next) {
    if (!o->removed) return true;
  }
  return false;
}

static bool same_string(const char *a, const char *b) {
  if (a == NULL || b == NULL) return a == b;
  return strcmp(a, b) == 0;
}
static bool same_value_layout(const qa_abi_value_layout *left, const qa_abi_value_layout *right) {
  if (!left->is_aggregate) return !right->is_aggregate && left->storage == right->storage;
  if (!right->is_aggregate) return false;
  const qa_abi_layout *a = &left->layout, *b = &right->layout;
  if (!same_string(a->id, b->id) || a->byte_length != b->byte_length || a->alignment != b->alignment ||
      a->pointer_bytes != b->pointer_bytes || a->field_count != b->field_count) return false;
  for (size_t i = 0; i < a->field_count; i++) {
    const qa_abi_field_layout *f = &a->fields[i], *g = &b->fields[i];
    if (!same_string(f->name, g->name) || f->byte_offset != g->byte_offset ||
        f->storage != g->storage || f->count != g->count) return false;
  }
  return true;
}
static bool same_signature(const qa_guest_call_signature *left, const qa_guest_call_signature *right) {
  if (!same_string(left->abi.kind, right->abi.kind) || !same_string(left->abi.call, right->abi.call) ||
      left->variadic != right->variadic || left->parameter_count != right->parameter_count) return false;
  for (size_t i = 0; i < left->parameter_count; i++) {
    if (!same_value_layout(&left->parameters[i], &right->parameters[i])) return false;
  }
  if (left->result_is_void) return right->result_is_void;
  return !right->result_is_void && same_value_layout(&left->result, &right->result);
}

static void changed_entries(qa_guest_callback_table *table) { table->entry_revision += 1u; }
static qa_guest_callback_entry *by_id(qa_guest_callback_table *table, qa_callback_id id) {
  for (qa_guest_callback_entry *e = table->entries; e != NULL; e = e->next) {
    if (e->via_bind && same_string(e->id, id)) return e;
  }
  return NULL;
}
static qa_guest_callback_entry *by_address(qa_guest_callback_table *table, uint64_t byte_offset) {
  for (qa_guest_callback_entry *e = table->entries; e != NULL; e = e->next) {
    if (e->address.byte_offset == byte_offset) return e;
  }
  return NULL;
}
static void append_entry(qa_guest_callback_table *table, qa_guest_callback_entry *entry) {
  entry->next = NULL;
  if (table->entries == NULL) { table->entries = entry; return; }
  qa_guest_callback_entry *tail = table->entries;
  while (tail->next != NULL) tail = tail->next;
  tail->next = entry;
}
static void remove_entry(qa_guest_callback_table *table, qa_guest_callback_entry *entry) {
  qa_guest_callback_entry **link = &table->entries;
  while (*link != NULL && *link != entry) link = &(*link)->next;
  if (*link == entry) *link = entry->next;
  /* observers freed by destroy-by-bucket walk below in destroy; entries
   * removed here carry no observers (bindEntry only). */
  free(entry);
}

void qa_guest_callback_table_init(qa_guest_callback_table *table, qa_sparse_guest_memory *memory) {
  memset(table, 0, sizeof *table);
  table->memory = memory;
}
void qa_guest_callback_table_init_managed(qa_guest_callback_table *table, qa_sparse_guest_memory *memory) {
  qa_guest_callback_table_init(table, memory);
  table->managed = true;
}
bool qa_guest_callback_table_managed(const qa_guest_callback_table *table) { return table->managed; }
uint64_t qa_guest_callback_table_entry_revision(const qa_guest_callback_table *table) { return table->entry_revision; }
bool qa_guest_callback_table_instruction_unhooked(const qa_guest_callback_table *table, uint64_t byte_offset) {
  qa_guest_callback_table *mutable_table = (qa_guest_callback_table *)table;
  return by_address(mutable_table, byte_offset) == NULL &&
         !bucket_has_observers(bucket_of(mutable_table, byte_offset, false));
}
void qa_guest_callback_table_destroy(qa_guest_callback_table *table) {
  qa_guest_callback_entry *entry = table->entries;
  while (entry != NULL) {
    qa_guest_callback_entry *next = entry->next;
    free(entry);
    entry = next;
  }
  observer_bucket *bucket = (observer_bucket *)table->observer_buckets;
  while (bucket != NULL) {
    observer_bucket *next_bucket = bucket->next;
    struct entry_observer *observer = bucket->observers;
    while (observer != NULL) {
      struct entry_observer *next_observer = observer->next;
      free(observer);
      observer = next_observer;
    }
    free(bucket);
    bucket = next_bucket;
  }
  memset(table, 0, sizeof *table);
}

qa_guest_entry_handle qa_guest_callback_table_bind_entry(qa_guest_callback_table *table, qa_guest_address address,
                                                         const qa_guest_host_callback *callback,
                                                         bool (*accepts)(void *ctx), void *accepts_ctx) {
  qa_sparse_check(table->memory, address, 1, QA_ACCESS_EXECUTE);
  if (callback->signature.abi.pointer_bytes != table->memory->base.pointer_bytes ||
      by_address(table, address.byte_offset) != NULL)
    qa_range_error("Invalid or occupied native callback entry");
  qa_guest_callback_entry *entry = (qa_guest_callback_entry *)calloc(1, sizeof *entry);
  if (entry == NULL) qa_range_error("Guest memory allocation failed");
  entry->handle = ++table->next_handle;
  entry->id = callback->id;
  entry->signature = callback->signature;
  entry->address = address;
  entry->callback = callback;
  entry->accepts = accepts;
  entry->accepts_ctx = accepts_ctx;
  entry->via_bind = false;
  append_entry(table, entry);
  changed_entries(table);
  return entry->handle;
}
void qa_guest_callback_table_unbind_entry(qa_guest_callback_table *table, qa_guest_entry_handle handle) {
  for (qa_guest_callback_entry *e = table->entries; e != NULL; e = e->next) {
    if (!e->via_bind && e->handle == handle && handle != 0) {
      remove_entry(table, e);
      changed_entries(table);
      return;
    }
  }
}
qa_guest_entry_handle qa_guest_callback_table_observe_entry(qa_guest_callback_table *table, qa_guest_address address,
                                                            void (*before)(void *ctx), void *ctx) {
  qa_sparse_check(table->memory, address, 1, QA_ACCESS_EXECUTE);
  observer_bucket *bucket = bucket_of(table, address.byte_offset, true);
  struct entry_observer *observer = (struct entry_observer *)calloc(1, sizeof *observer);
  if (observer == NULL) qa_range_error("Guest memory allocation failed");
  observer->handle = ++table->next_handle;
  observer->before = before;
  observer->ctx = ctx;
  observer->next = NULL;
  if (bucket->observers == NULL) bucket->observers = observer;
  else {
    struct entry_observer *tail = bucket->observers;
    while (tail->next != NULL) tail = tail->next;
    tail->next = observer;
  }
  changed_entries(table);
  return observer->handle;
}
void qa_guest_callback_table_unobserve_entry(qa_guest_callback_table *table, qa_guest_entry_handle handle) {
  for (observer_bucket *b = (observer_bucket *)table->observer_buckets; b != NULL; b = b->next) {
    for (struct entry_observer *o = b->observers; o != NULL; o = o->next) {
      if (o->handle == handle && handle != 0 && !o->removed) {
        o->removed = true;
        changed_entries(table);
        return;
      }
    }
  }
}
bool qa_guest_callback_table_enter(qa_guest_callback_table *table, qa_guest_address address) {
  observer_bucket *bucket = bucket_of(table, address.byte_offset, false);
  bool has_observers = bucket_has_observers(bucket);
  /* Ordinary instructions are checked by the CPU fetch, not the table.
   * (The TS foreign-address-space recheck has no C counterpart.) */
  if (!has_observers && by_address(table, address.byte_offset) == NULL) return false;
  qa_sparse_check(table->memory, address, 1, QA_ACCESS_EXECUTE);
  if (bucket != NULL) {
    /* TS iterates a snapshot, skipping observers removed mid-loop. */
    size_t count = 0;
    for (struct entry_observer *o = bucket->observers; o != NULL; o = o->next) count++;
    struct entry_observer **snapshot = (struct entry_observer **)calloc(count == 0 ? 1 : count, sizeof *snapshot);
    if (snapshot == NULL) qa_range_error("Guest memory allocation failed");
    size_t i = 0;
    for (struct entry_observer *o = bucket->observers; o != NULL; o = o->next) snapshot[i++] = o;
    for (size_t j = 0; j < count; j++) {
      if (!snapshot[j]->removed) snapshot[j]->before(snapshot[j]->ctx);
    }
    free(snapshot);
  }
  return qa_guest_callback_table_resolve(table, address) != NULL;
}

qa_guest_address qa_guest_callback_table_bind(qa_guest_callback_table *table, const qa_guest_host_callback *callback) {
  if (callback->signature.abi.pointer_bytes != table->memory->base.pointer_bytes)
    qa_range_error("Callback ABI differs from its guest address space");
  qa_guest_callback_entry *previous = by_id(table, callback->id);
  if (previous != NULL) {
    if (previous->callback != NULL && previous->callback != callback)
      qa_range_error("Callback %s is already bound", callback->id);
    if (!same_signature(&previous->signature, &callback->signature))
      qa_range_error("Callback %s changed ABI or signature", callback->id);
    previous->callback = callback;
    changed_entries(table);
    return previous->address;
  }
  /* INT3 is a trap if the CPU misses the table dispatch. */
  char label[256];
  (void)snprintf(label, sizeof label, "callback %s", callback->id);
  char *label_copy = (char *)malloc(strlen(label) + 1u);
  if (label_copy == NULL) qa_range_error("Guest memory allocation failed");
  strcpy(label_copy, label);
  qa_guest_allocation_options options;
  memset(&options, 0, sizeof options);
  options.byte_length = 16;
  options.alignment = 16;
  options.permissions = QA_PERM_READ_WRITE;
  options.has_permissions = true;
  options.label = label_copy;
  qa_guest_address address = qa_sparse_allocate(table->memory, &options);
  free(label_copy); /* sparse map duplicated the label */
  uint8_t traps[16];
  memset(traps, 0xcc, sizeof traps);
  qa_sparse_write(table->memory, address, traps, 16);
  qa_sparse_protect(table->memory, address, 16, QA_PERM_READ_EXECUTE);
  qa_guest_callback_entry *entry = (qa_guest_callback_entry *)calloc(1, sizeof *entry);
  if (entry == NULL) qa_range_error("Guest memory allocation failed");
  entry->id = callback->id;
  entry->signature = callback->signature;
  entry->address = address;
  entry->callback = callback;
  entry->via_bind = true;
  append_entry(table, entry);
  changed_entries(table);
  return address;
}
void qa_guest_callback_table_unbind(qa_guest_callback_table *table, qa_callback_id id) {
  qa_guest_callback_entry *entry = by_id(table, id);
  if (entry != NULL) {
    entry->callback = NULL;
    changed_entries(table);
  }
}
bool qa_guest_callback_table_address(const qa_guest_callback_table *table, qa_callback_id id, qa_guest_address *out) {
  qa_guest_callback_entry *entry = by_id((qa_guest_callback_table *)table, id);
  if (entry == NULL) return false;
  *out = entry->address;
  return true;
}
bool qa_guest_callback_table_has_bound_trap(const qa_guest_callback_table *table, uint64_t byte_offset) {
  qa_guest_callback_table *mutable_table = (qa_guest_callback_table *)table;
  qa_guest_callback_entry *entry = by_address(mutable_table, byte_offset);
  return entry != NULL && entry->callback != NULL && by_id(mutable_table, entry->id) == entry;
}
const qa_guest_host_callback *qa_guest_callback_table_resolve(qa_guest_callback_table *table, qa_guest_address address) {
  qa_sparse_check(table->memory, address, 1, QA_ACCESS_EXECUTE);
  qa_guest_callback_entry *entry = by_address(table, address.byte_offset);
  if (entry == NULL) return NULL;
  if (entry->accepts != NULL && !entry->accepts(entry->accepts_ctx)) return NULL;
  if (entry->callback == NULL)
    qa_range_error("Guest callback %s at 0x%llx is unbound", entry->id, (unsigned long long)entry->address.byte_offset);
  return entry->callback;
}
qa_call_result qa_guest_callback_table_invoke(qa_guest_callback_table *table, qa_guest_address address,
                                                    const qa_guest_call_context *context,
                                                    const qa_call_value *arguments, size_t argument_count) {
  const qa_guest_host_callback *callback = qa_guest_callback_table_resolve(table, address);
  if (callback == NULL)
    qa_range_error("No host callback at guest address 0x%llx", (unsigned long long)address.byte_offset);
  return callback->invoke(callback->ctx, context, arguments, argument_count);
}
qa_saved_host_callback_address *qa_guest_callback_table_checkpoint(const qa_guest_callback_table *table, size_t *out_count) {
  size_t count = 0;
  for (qa_guest_callback_entry *e = table->entries; e != NULL; e = e->next) {
    if (e->via_bind) count++;
  }
  qa_saved_host_callback_address *saved = (qa_saved_host_callback_address *)calloc(count == 0 ? 1 : count, sizeof *saved);
  if (saved == NULL) qa_range_error("Guest memory allocation failed");
  size_t i = 0;
  for (qa_guest_callback_entry *e = table->entries; e != NULL; e = e->next) {
    if (!e->via_bind) continue;
    saved[i].id = e->id;
    saved[i].byte_offset = e->address.byte_offset;
    saved[i].signature = e->signature;
    saved[i].binding = e->callback == NULL ? QA_CALLBACK_UNBOUND : QA_CALLBACK_BOUND;
    i++;
  }
  *out_count = count;
  return saved;
}
void qa_guest_callback_table_restore(qa_guest_callback_table *table, qa_sparse_guest_memory *memory,
                                     const qa_saved_host_callback_address *saved, size_t count,
                                     const qa_guest_host_callback *(*resolve)(void *ctx, qa_callback_id id), void *resolve_ctx) {
  qa_guest_callback_table_init(table, memory);
  for (size_t i = 0; i < count; i++) {
    const qa_saved_host_callback_address *record = &saved[i];
    if (by_id(table, record->id) != NULL || by_address(table, record->byte_offset) != NULL)
      qa_range_error("Duplicate restored guest callback identity or address");
    qa_guest_address address;
    if (!qa_sparse_pointer(memory, record->byte_offset, &address))
      qa_range_error("Guest callback cannot have a null address");
    qa_sparse_check(memory, address, 16, QA_ACCESS_EXECUTE);
    if (record->signature.abi.pointer_bytes != memory->base.pointer_bytes)
      qa_range_error("Saved callback ABI differs from the restored address space");
    const qa_guest_host_callback *callback = record->binding == QA_CALLBACK_BOUND ? resolve(resolve_ctx, record->id) : NULL;
    if (record->binding == QA_CALLBACK_BOUND && callback == NULL)
      qa_range_error("Missing restored host callback %s", record->id);
    if (callback != NULL && (!same_string(callback->id, record->id) || !same_signature(&callback->signature, &record->signature)))
      qa_range_error("Restored callback %s has a different identity or signature", record->id);
    qa_guest_callback_entry *entry = (qa_guest_callback_entry *)calloc(1, sizeof *entry);
    if (entry == NULL) qa_range_error("Guest memory allocation failed");
    entry->id = record->id;
    entry->signature = record->signature;
    entry->address = address;
    entry->callback = callback;
    entry->via_bind = true;
    append_entry(table, entry);
  }
}

