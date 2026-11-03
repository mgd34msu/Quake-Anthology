/* Port of src/compat/q2/native-mod-items.ts: inventory borrows original native
 * words; observers publish committed stores exactly once. GPL-2.0-or-later. */
#include "qa/compat/q2/native_mod_items.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/compat/q2/native_mod_host_api.h"
#include "qa/content/item_icon.h"
#include "qa/core/numeric.h"
#include "qa/persistence/save_image.h"
#include "qa/foundation/js.h"
#include "qa/network/common/js_number.h"

static int scalar_bytes(qa_native_item_field field) { return qa_compat_q2_native_storage_bytes(field.field.encoding, 4); }
static bool is_integer(double value) { return isfinite(value) && value == trunc(value); }
static bool is_safe_integer(double value) { return is_integer(value) && fabs(value) <= (double)QA_MAX_SAFE_INTEGER; }
static bool is_packed_encoding(const char *encoding) {
  static const char *const names[] = {"int8", "uint8", "int16", "uint16", "int32", "uint32"};
  for (size_t i = 0; i < 6; i++) if (strcmp(encoding, names[i]) == 0) return true;
  return false;
}
static void validate_counter(double value, qa_native_item_field field) {
  if (!isfinite(value)) qa_range_error("Native inventory requires a finite source count");
  const char *encoding = field.field.encoding;
  if (strcmp(encoding, "float32") == 0 || strcmp(encoding, "float64") == 0) return;
  int bits = scalar_bytes(field) * 8; bool is_signed = strncmp(encoding, "int", 3) == 0;
  double bound = qa_js_pow(2, bits - (is_signed ? 1 : 0));
  if (!is_safe_integer(value) || value < (is_signed ? -bound : 0) || value >= bound) qa_range_error("Native inventory count exceeds its original integer storage");
}
/* --- validateNativeModItems --- */
typedef struct { const char *record; double index; const char *shared; } occupied_byte;
typedef struct {
  const qa_native_mod_declaration *d;
  occupied_byte *occupied; size_t occupied_count, occupied_capacity;
  char **shared; size_t shared_count;
} validation;
static const qa_native_mod_actor_record *find_record(const qa_native_mod_declaration *d, const char *id) {
  for (size_t i = 0; i < d->actor_record_count; i++) if (strcmp(d->actor_records[i].id, id) == 0) return &d->actor_records[i];
  return NULL;
}
static double projection_size(const qa_native_mod_declaration *d, const qa_native_mod_actor_field *projection) {
  const char *binding = projection->binding;
  if (strcmp(binding, "private") == 0) return projection->qvm_field.byte_length;
  if (projection->encoding != NULL && (strcmp(binding, "score") == 0 || strcmp(binding, "team") == 0 || strcmp(binding, "health") == 0 || strcmp(binding, "inventory") == 0 || strcmp(binding, "inventory-capacity") == 0 || strcmp(binding, "constant") == 0))
    return qa_compat_q2_native_storage_bytes(projection->encoding, d->target.abi.pointer_bytes);
  if (strcmp(binding, "record") == 0 || strcmp(binding, "address") == 0) return (double)d->target.abi.pointer_bytes;
  return 12;
}
static void validate_field(validation *v, const char *record_id, double offset, double length, bool exclusive, const char *shared_capacity) {
  const qa_native_mod_declaration *d = v->d;
  const qa_native_mod_actor_record *record = find_record(d, record_id);
  bool admitted = false;
  if (d->clients != NULL) for (size_t i = 0; i < d->clients->records.count; i++) if (strcmp(d->clients->records.values[i], record_id) == 0) admitted = true;
  if (record == NULL || !admitted || !is_safe_integer(offset) || offset < 0 || offset + length > record->stride) qa_range_error("Native source item exceeds its client record");
  if (!exclusive) return;
  for (double index = offset; index < offset + length; index++) {
    occupied_byte *found = NULL;
    for (size_t i = 0; i < v->occupied_count && found == NULL; i++) if (v->occupied[i].index == index && strcmp(v->occupied[i].record, record_id) == 0) found = &v->occupied[i];
    if (found != NULL && (shared_capacity == NULL || found->shared == NULL || strcmp(found->shared, shared_capacity) != 0)) qa_range_error("Overlapping native source item storage");
    if (found == NULL) {
      if (v->occupied_count == v->occupied_capacity) {
        size_t capacity = v->occupied_capacity ? v->occupied_capacity * 2 : 64;
        occupied_byte *grown = realloc(v->occupied, capacity * sizeof *grown);
        if (grown == NULL) qa_range_error("Native items out of memory");
        v->occupied = grown; v->occupied_capacity = capacity;
      }
      found = &v->occupied[v->occupied_count++];
      found->record = record_id; found->index = index;
    }
    found->shared = shared_capacity;
  }
  for (size_t i = 0; i < record->field_count; i++) {
    const qa_native_mod_actor_field *projection = &record->fields[i];
    double size = projection_size(d, projection);
    if (projection->offset < offset + length && offset < projection->offset + size && strcmp(projection->binding, "private") != 0 && strcmp(projection->binding, "constant") != 0)
      qa_range_error("Native source item overlaps another canonical projection");
  }
}
static const qa_native_mod_item_definition *find_definition(const qa_native_mod_items *items, qa_item_id item) {
  for (size_t i = 0; i < items->definition_count; i++) if (strcmp(items->definitions[i].item, item) == 0) return &items->definitions[i];
  return NULL;
}
static void validate_test_row(validation *v, const qa_native_item_test_row *rows, size_t row_count) {
  int64_t pointer_bytes = v->d->target.abi.pointer_bytes;
  for (size_t r = 0; r < row_count; r++) for (size_t t = 0; t < rows[r].count; t++) {
    const qa_native_item_test *test = &rows[r].tests[t];
    if (strcmp(test->kind, "pointer") == 0) validate_field(v, test->pointer_field.record, test->pointer_field.offset, (double)pointer_bytes, false, NULL);
    else validate_field(v, test->field.record, test->field.field.offset, scalar_bytes(test->field), false, NULL);
  }
}
static bool any_empty(const qa_native_item_test_row *rows, size_t count) {
  for (size_t i = 0; i < count; i++) if (rows[i].count == 0) return true;
  return false;
}
void qa_compat_q2_validate_native_mod_items(const qa_native_mod_declaration *d) {
  const qa_native_mod_items *items = d->items;
  if (items == NULL) return;
  if (d->clients == NULL || items->definition_count == 0) qa_range_error("Native items require original client admission");
  for (size_t i = 0; i < items->definition_count; i++) for (size_t j = 0; j < i; j++)
    if (strcmp(items->definitions[i].item, items->definitions[j].item) == 0) qa_range_error("Duplicate native source item");
  validation v;
  memset(&v, 0, sizeof v);
  v.d = d;
  size_t capacity_keys = items->storage_count;
  v.shared = calloc(capacity_keys ? capacity_keys : 1, sizeof *v.shared);
  bool *bound = calloc(items->definition_count, sizeof *bound);
  if (v.shared == NULL || bound == NULL) qa_range_error("Native items out of memory");
  size_t bound_count = 0;
  for (size_t s = 0; s < items->storage_count; s++) {
    const qa_native_item_storage *storage = &items->storage[s];
    validate_field(&v, storage->field.record, storage->field.field.offset, scalar_bytes(storage->field), true, NULL);
    const qa_item_id *names = NULL; size_t name_count = 0;
    bool counter = strcmp(storage->kind, "counter") == 0;
    if (counter) {
      if (strcmp(storage->capacity.kind, "field") == 0) {
        qa_native_item_field capacity = storage->capacity.field;
        char key[96];
        snprintf(key, sizeof key, "%s:%s", QA_JS_NUMBER(capacity.field.offset), capacity.field.encoding);
        size_t length = strlen(key) + 1;
        char *owned = malloc(length);
        if (owned == NULL) qa_range_error("Native items out of memory");
        memcpy(owned, key, length);
        v.shared[v.shared_count++] = owned;
        validate_field(&v, capacity.record, capacity.field.offset, scalar_bytes(capacity), true, owned);
      }
      names = &storage->item; name_count = 1;
    } else {
      if (!is_packed_encoding(storage->field.field.encoding) || storage->item_count == 0 || !is_integer(storage->private_mask) || storage->private_mask < 0 || storage->private_mask > 4294967295.0) qa_range_error("Invalid native packed inventory");
    }
    for (size_t n = 0; n < (counter ? name_count : storage->item_count); n++) {
      qa_item_id name = counter ? names[n] : storage->items[n].item;
      const qa_native_mod_item_definition *definition = find_definition(items, name);
      size_t index = definition == NULL ? 0 : (size_t)(definition - items->definitions);
      if (definition == NULL || bound[index]) qa_range_error("Native source item lacks distinct storage");
      bound[index] = true; bound_count++;
    }
    if (counter) {
      if (strcmp(storage->capacity.kind, "constant") == 0 && (!isfinite(storage->capacity.value) || storage->capacity.value < 0)) qa_range_error("Invalid native source capacity");
    } else {
      int32_t mask = qa_int32(storage->private_mask);
      for (size_t n = 0; n < storage->item_count; n++) {
        double value = storage->items[n].mask;
        int32_t bit = qa_int32(value);
        if (!is_integer(value) || value < 1 || value > 2147483648.0 || (bit & qa_int32(value - 1)) != 0 || (mask & bit) != 0) qa_range_error("Overlapping native item bits");
        mask |= bit;
      }
      if (scalar_bytes(storage->field) < 4 && (double)(uint32_t)mask >= qa_js_pow(2, scalar_bytes(storage->field) * 8)) qa_range_error("Native packed item exceeds storage width");
    }
  }
  free(bound);
  if (bound_count != items->definition_count) qa_range_error("Native item definition lacks source storage");
  size_t weapon_count = 0;
  for (size_t i = 0; i < items->definition_count; i++) if (items->definitions[i].is_weapon) weapon_count++;
  if (weapon_count == 0 ? items->weapons != NULL : items->weapons == NULL) qa_range_error("Native weapon items require an original dispatcher");
  const qa_native_weapon_stage *stage = items->weapons;
  if (stage != NULL) {
    const qa_native_weapon_dispatcher *dispatcher = &stage->dispatcher;
    if (find_record(d, dispatcher->record) == NULL || !is_safe_integer(dispatcher->arguments) || dispatcher->arguments < 1 || dispatcher->arguments > 16
        || !is_safe_integer(dispatcher->argument) || dispatcher->argument < 0 || dispatcher->argument >= dispatcher->arguments || stage->decision_count == 0
        || stage->settled_row_count == 0 || any_empty(stage->settled, stage->settled_row_count) || stage->continuation_row_count == 0 || any_empty(stage->continuations, stage->continuation_row_count)
        || (stage->committed_input != NULL && any_empty(stage->committed_input, stage->committed_input_row_count))) qa_range_error("Native weapon stage lacks original decisions or actor ABI");
    for (size_t i = 0; i < stage->decision_count; i++) {
      const qa_native_weapon_decision *region = &stage->decisions[i];
      bool duplicate = false;
      for (size_t j = 0; j < i; j++) if (stage->decisions[j].entry == region->entry) duplicate = true;
      if (!is_safe_integer(region->entry) || !is_safe_integer(region->join) || region->entry < 0 || region->join <= region->entry || duplicate || region->field_count == 0) qa_range_error("Invalid native weapon input-read region");
      for (size_t f = 0; f < region->field_count; f++) {
        const qa_native_weapon_decision_field *value = &region->fields[f];
        validate_field(&v, value->field.record, value->field.field.offset, scalar_bytes(value->field), false, NULL);
        if (!is_packed_encoding(value->field.field.encoding) || !is_integer(value->clear_mask) || value->clear_mask < 1 || value->clear_mask > 2147483647.0) qa_range_error("Invalid native weapon input projection");
      }
    }
    double pointer_bytes = (double)d->target.abi.pointer_bytes;
    validate_field(&v, stage->selection.active.record, stage->selection.active.offset, pointer_bytes, false, NULL);
    if (stage->selection.pending != NULL) validate_field(&v, stage->selection.pending->record, stage->selection.pending->offset, pointer_bytes, false, NULL);
    validate_test_row(&v, stage->settled, stage->settled_row_count);
    validate_test_row(&v, stage->continuations, stage->continuation_row_count);
    if (stage->committed_input != NULL) validate_test_row(&v, stage->committed_input, stage->committed_input_row_count);
    bool differs = stage->selection.value_count != weapon_count;
    for (size_t i = 0; i < stage->selection.value_count && !differs; i++) {
      const qa_native_mod_item_definition *definition = find_definition(items, stage->selection.values[i].item);
      if (definition == NULL || !definition->is_weapon) differs = true;
      for (size_t j = 0; j < i; j++) if (strcmp(stage->selection.values[j].item, stage->selection.values[i].item) == 0) differs = true;
    }
    if (differs) qa_range_error("Native weapon pointers differ from admitted source definitions");
    for (size_t i = 0; i < items->definition_count; i++) {
      const qa_native_mod_item_definition *weapon = &items->definitions[i];
      if (weapon->is_weapon && weapon->weapon.ammo != NULL && find_definition(items, weapon->weapon.ammo) == NULL) qa_range_error("Native weapon ammo lacks original storage");
    }
  }
  for (size_t i = 0; i < v.shared_count; i++) free(v.shared[i]);
  free(v.shared); free(v.occupied);
}
/* --- readNativeItemCheckpoint --- */
void qa_compat_q2_native_item_checkpoint_free(qa_compat_q2_native_item_checkpoint *checkpoint) {
  free((void *)checkpoint->requests);
  checkpoint->requests = NULL; checkpoint->request_count = 0;
}
qa_compat_q2_native_item_checkpoint qa_compat_q2_read_native_item_checkpoint(const qa_save_reader *reader, const qa_native_mod_items *definition) {
  qa_save_reader field;
  if (!qa_save_reader_field(reader, "version", &field)) qa_range_error("Native items out of memory");
  qa_save_reader_literal_number(&field, 1); qa_save_reader_destroy(&field);
  if (!qa_save_reader_field(reader, "nextRequest", &field)) qa_range_error("Native items out of memory");
  double next_request = qa_save_reader_integer_min(&field, 0); qa_save_reader_destroy(&field);
  qa_save_reader list;
  if (!qa_save_reader_field(reader, "requests", &list)) qa_range_error("Native items out of memory");
  size_t count = qa_save_reader_list_count(&list);
  qa_compat_q2_native_item_checkpoint_request *requests = calloc(count ? count : 1, sizeof *requests);
  if (requests == NULL) qa_range_error("Native items out of memory");
  for (size_t i = 0; i < count; i++) {
    qa_save_reader row, value;
    if (!qa_save_reader_item(&list, i, &row)) qa_range_error("Native items out of memory");
    qa_compat_q2_native_item_checkpoint_request *request = &requests[i];
    if (!qa_save_reader_field(&row, "actor", &value)) qa_range_error("Native items out of memory");
    request->actor = qa_read_saved_actor(&value); qa_save_reader_destroy(&value);
    if (!qa_save_reader_field(&row, "id", &value)) qa_range_error("Native items out of memory");
    request->id = qa_save_reader_integer_min(&value, 1); qa_save_reader_destroy(&value);
    if (!qa_save_reader_field(&row, "item", &value)) qa_range_error("Native items out of memory");
    request->item = qa_save_reader_is_null(&value) ? NULL : qa_namespaced(&value); qa_save_reader_destroy(&value);
    static const char *const statuses[] = {"pending", "accepted", "refused"};
    if (!qa_save_reader_field(&row, "status", &value)) qa_range_error("Native items out of memory");
    request->status = qa_save_reader_choice_string(&value, statuses, 3); qa_save_reader_destroy(&value);
    bool duplicate = false, declared = false;
    for (size_t j = 0; j < i; j++)
      if (requests[j].id == request->id || (requests[j].actor.slot == request->actor.slot && requests[j].actor.generation == request->actor.generation)) duplicate = true;
    if (definition->weapons != NULL && request->item != NULL)
      for (size_t j = 0; j < definition->weapons->selection.value_count; j++) if (strcmp(definition->weapons->selection.values[j].item, request->item) == 0) declared = true;
    if (request->id > next_request || duplicate || definition->weapons == NULL || (request->item != NULL && !declared)) qa_range_error("Saved native request differs from its source declaration");
    qa_save_reader_destroy(&row);
  }
  qa_save_reader_destroy(&list);
  return (qa_compat_q2_native_item_checkpoint){next_request, requests, count};
}
/* --- NativeModItems --- */
typedef struct qa_compat_q2_native_mod_items items_t;
typedef struct request_t { double id; qa_item_id item; const char *status; struct request_t *next_owned; } request_t;
typedef struct { const char *record; int64_t address; } record_address;
typedef struct entry_t entry_t;
typedef struct { items_t *s; entry_t *entry; const qa_native_item_storage *storage; } storage_observer;
struct entry_t {
  items_t *s;
  qa_owned_actor actor;
  record_address *addresses; size_t address_count;
  qa_source_item_lease lease;
  uint64_t *removals; size_t removal_count, removal_capacity;
  storage_observer *storage_observers;
  qa_inventory_entry *previous; size_t previous_count;
  qa_inventory_entry *scratch; size_t scratch_capacity;
  bool has_remove_weapon; qa_source_weapon_unbind remove_weapon;
  request_t *request;
  qa_item_id source_selection;
  qa_source_weapon_presentation_model model;
  bool live_entry;
  entry_t *next_owned;
};
typedef struct capability_t { items_t *s; entry_t *entry; request_t *request; struct capability_t *next_owned; } capability_t;
struct qa_compat_q2_native_mod_items {
  const qa_native_mod_items *definition;
  qa_compat_q2_native_mod_item_host host;
  const qa_mod_host_services *services;
  qa_provider_id provider; qa_content_id content;
  qa_compat_q2_native_mod_item_operations operations;
  qa_source_item_admission *admissions; qa_source_item_definition *definitions; qa_weapon_hud_icon *icons; qa_source_item_action *actions;
  qa_compat_q2_native_mod_weapon_stage *stage;
  entry_t **entries; size_t entry_count, entry_capacity; /* live entries, Map order */
  entry_t *owned_entries; request_t *owned_requests; capability_t *owned_capabilities;
  double next_request;
  int writing;
  qa_actor_id *requesting; size_t requesting_count, requesting_capacity;
};
static entry_t *find_entry(const items_t *s, qa_actor_id actor) {
  for (size_t i = 0; i < s->entry_count; i++) if (qa_same_actor(s->entries[i]->actor.id, actor)) return s->entries[i];
  return NULL;
}
static qa_guest_address field_address(const items_t *s, qa_actor_id actor, const char *record, double offset) {
  return qa_compat_q2_mem_offset(s->host.memory, s->operations.pointer(s->operations.ctx, actor, record), (int64_t)offset);
}
static double op_read(const items_t *s, qa_guest_address address, qa_native_mod_scalar encoding) { return s->operations.read(s->operations.ctx, address, encoding); }
static void op_write(const items_t *s, qa_guest_address address, qa_native_mod_scalar encoding, double value) { s->operations.write(s->operations.ctx, address, encoding, value); }
static double read_capacity(const items_t *s, qa_actor_id actor, const qa_native_item_capacity *capacity) {
  if (strcmp(capacity->kind, "constant") == 0) return capacity->value;
  if (strcmp(capacity->kind, "field") == 0) return op_read(s, field_address(s, actor, capacity->field.record, capacity->field.field.offset), capacity->field.field.encoding);
  return op_read(s, s->operations.resolve(s->operations.ctx, capacity->address), capacity->encoding);
}
static size_t storage_rows(const qa_native_item_storage *storage) { return strcmp(storage->kind, "counter") == 0 ? 1 : storage->item_count; }
static size_t total_rows(const items_t *s) {
  size_t total = 0;
  for (size_t i = 0; i < s->definition->storage_count; i++) total += storage_rows(&s->definition->storage[i]);
  return total;
}
/* Writes storage_rows(storage) rows. */
static void read_storage(const items_t *s, qa_actor_id actor, const qa_native_item_storage *storage, qa_inventory_entry *out) {
  double count = op_read(s, field_address(s, actor, storage->field.record, storage->field.field.offset), storage->field.field.encoding);
  if (strcmp(storage->kind, "counter") == 0) {
    const char *encoding = storage->field.field.encoding;
    memset(out, 0, sizeof *out);
    out->item = storage->item; out->count = count; out->capacity = read_capacity(s, actor, &storage->capacity);
    out->has_count_policy = true; out->count_policy.kind = "source-counter";
    out->count_policy.arithmetic = strcmp(encoding, "float32") == 0 ? "binary32" : strcmp(encoding, "int32") == 0 ? "int32" : "binary64";
    return;
  }
  int32_t mask = qa_int32(storage->private_mask);
  for (size_t i = 0; i < storage->item_count; i++) mask |= qa_int32(storage->items[i].mask);
  int bytes = scalar_bytes(storage->field);
  int32_t packed = qa_int32(count) & (bytes == 4 ? -1 : (int32_t)(qa_js_pow(2, bytes * 8) - 1));
  if ((packed & ~mask) != 0) qa_range_error("Native inventory contains undeclared item bits");
  for (size_t i = 0; i < storage->item_count; i++) {
    memset(&out[i], 0, sizeof out[i]);
    out[i].item = storage->items[i].item;
    out[i].count = (qa_int32(count) & qa_int32(storage->items[i].mask)) == 0 ? 0 : 1;
    out[i].capacity = 1;
  }
}
static const qa_native_item_storage *storage_of(const items_t *s, qa_item_id item) {
  for (size_t i = 0; i < s->definition->storage_count; i++) {
    const qa_native_item_storage *storage = &s->definition->storage[i];
    if (strcmp(storage->kind, "counter") == 0) { if (strcmp(storage->item, item) == 0) return storage; }
    else for (size_t n = 0; n < storage->item_count; n++) if (strcmp(storage->items[n].item, item) == 0) return storage;
  }
  return NULL;
}
static bool entry_current(const items_t *s, entry_t *entry) {
  if (find_entry(s, entry->actor.id) != entry) return false;
  qa_owned_actor owner;
  if (!qa_session_actor_registry_resolve_owned(s->services->actors, entry->actor.id, &owner) || !qa_same_owned_actor(owner, entry->actor)) return false;
  if (!s->operations.live(s->operations.ctx, entry->actor.id) || !entry->lease.current(entry->lease.ctx)) return false;
  for (size_t i = 0; i < entry->address_count; i++)
    if (s->operations.pointer(s->operations.ctx, entry->actor.id, entry->addresses[i].record).byte_offset != entry->addresses[i].address) return false;
  return true;
}
static void throw_retired(entry_t *entry) { qa_compat_q2_native_throw(entry, QA_SOURCE_ITEM_RETIRED_MESSAGE); }
static qa_inventory_entry *previous_of(entry_t *entry, qa_item_id item) {
  for (size_t i = 0; i < entry->previous_count; i++) if (strcmp(entry->previous[i].item, item) == 0) return &entry->previous[i];
  return NULL;
}
static bool pickup_declares(const qa_original_pickup_execution *pickup, const qa_inventory_entry *before, const qa_inventory_entry *value) {
  for (size_t i = 0; i < pickup->writes.count; i++) {
    const qa_pickup_write *write = &pickup->writes.values[i];
    if (write->kind == QA_PICKUP_RESOURCE_INVENTORY && strcmp(write->item, value->item) == 0
        && (before->count == value->count || strcmp(write->fields, "capacity") != 0)
        && (before->capacity == value->capacity || strcmp(write->fields, "count") != 0)) return true;
  }
  return false;
}
typedef struct { entry_t *entry; const qa_source_item_store *changes; size_t count; } stored_call;
static void stored_body(void *v) { stored_call *c = v; c->entry->lease.stored(c->entry->lease.ctx, c->changes, c->count); }
/* only NULL: every declared storage. */
static void changed(items_t *s, entry_t *entry, const qa_native_item_storage *only, qa_item_id canonical_write) {
  if (s->writing != 0 || !entry_current(s, entry)) return;
  size_t capacity = only != NULL ? storage_rows(only) : total_rows(s);
  qa_inventory_entry *after = calloc(capacity ? capacity : 1, sizeof *after);
  qa_source_item_store *changes = calloc(capacity ? capacity : 1, sizeof *changes);
  if (after == NULL || changes == NULL) qa_range_error("Native items out of memory");
  size_t used = 0;
  if (only != NULL) { read_storage(s, entry->actor.id, only, after); used = capacity; }
  else for (size_t i = 0; i < s->definition->storage_count; i++) { read_storage(s, entry->actor.id, &s->definition->storage[i], after + used); used += storage_rows(&s->definition->storage[i]); }
  size_t change_count = 0;
  for (size_t i = 0; i < used; i++) {
    const qa_inventory_entry *value = &after[i], *before = previous_of(entry, value->item);
    if ((canonical_write == NULL || strcmp(value->item, canonical_write) != 0) && before != NULL && (before->count != value->count || before->capacity != value->capacity)) {
      const qa_original_pickup_execution *pickup = s->operations.pickup(s->operations.ctx);
      if (pickup != NULL && !pickup_declares(pickup, before, value)) qa_range_error("Original pickup changed an undeclared native source item");
      changes[change_count++] = (qa_source_item_store){*before, *value};
    }
  }
  for (size_t i = 0; i < used; i++) { qa_inventory_entry *row = previous_of(entry, after[i].item); if (row != NULL) *row = after[i]; }
  stored_call call = {entry, changes, change_count};
  qa_compat_q2_native_thrown thrown;
  bool completed = change_count == 0 || qa_compat_q2_native_try(stored_body, &call, &thrown);
  free(after); free(changes);
  if (!completed) qa_compat_q2_native_rethrow(&thrown);
  if (change_count != 0 && !entry_current(s, entry)) throw_retired(entry);
}
typedef struct { items_t *s; qa_actor_id actor; const qa_native_item_storage *storage; qa_inventory_entry value; } write_call;
static void write_body(void *v) {
  write_call *c = v; const items_t *s = c->s; const qa_native_item_storage *storage = c->storage; qa_inventory_entry value = c->value;
  if (strcmp(storage->kind, "counter") == 0) {
    bool field_capacity = strcmp(storage->capacity.kind, "field") == 0;
    if (!field_capacity && value.capacity != read_capacity(s, c->actor, &storage->capacity)) qa_range_error("Native capacity is owned by its original source");
    validate_counter(value.count, storage->field);
    if (field_capacity) validate_counter(value.capacity, storage->capacity.field);
    op_write(s, field_address(s, c->actor, storage->field.record, storage->field.field.offset), storage->field.field.encoding, value.count);
    if (field_capacity) op_write(s, field_address(s, c->actor, storage->capacity.field.record, storage->capacity.field.field.offset), storage->capacity.field.field.encoding, value.capacity);
    return;
  }
  const qa_native_item_mask *bit = NULL;
  for (size_t i = 0; i < storage->item_count && bit == NULL; i++) if (strcmp(storage->items[i].item, value.item) == 0) bit = &storage->items[i];
  if (bit == NULL || value.capacity != 1 || (value.count != 0 && value.count != 1)) qa_range_error("Native item requires its original ownership bit");
  qa_guest_address address = field_address(s, c->actor, storage->field.record, storage->field.field.offset);
  int32_t previous = qa_int32(op_read(s, address, storage->field.field.encoding));
  int32_t next = value.count == 0 ? previous & ~qa_int32(bit->mask) : previous | qa_int32(bit->mask);
  int bits = scalar_bytes(storage->field) * 8;
  double stored;
  if (strncmp(storage->field.field.encoding, "int", 3) == 0) {
    uint32_t shifted = (uint32_t)next << (32 - bits);
    int32_t extended = (int32_t)shifted;
    stored = extended >> (32 - bits);
  } else stored = (double)(uint32_t)next;
  op_write(s, address, storage->field.field.encoding, stored);
}
static void state_write(void *v, qa_inventory_entry value) {
  entry_t *entry = v; items_t *s = entry->s;
  const qa_native_item_storage *storage = storage_of(s, value.item);
  if (find_entry(s, entry->actor.id) == NULL || !entry_current(s, find_entry(s, entry->actor.id)) || storage == NULL) qa_range_error("Native item lease lost its original storage");
  entry_t *owner = find_entry(s, entry->actor.id);
  s->writing++;
  write_call call = {s, entry->actor.id, storage, value};
  qa_compat_q2_native_thrown thrown;
  bool completed = qa_compat_q2_native_try(write_body, &call, &thrown);
  s->writing--;
  changed(s, owner, NULL, value.item);
  if (!completed) qa_compat_q2_native_rethrow(&thrown);
}
static const qa_inventory_entry *state_read(void *v, size_t *out_count) {
  entry_t *entry = v; const items_t *s = entry->s;
  size_t total = total_rows(s), used = 0;
  if (total > entry->scratch_capacity) {
    qa_inventory_entry *grown = realloc(entry->scratch, total * sizeof *grown);
    if (grown == NULL) qa_range_error("Native items out of memory");
    entry->scratch = grown; entry->scratch_capacity = total;
  }
  for (size_t i = 0; i < s->definition->storage_count; i++) { read_storage(s, entry->actor.id, &s->definition->storage[i], entry->scratch + used); used += storage_rows(&s->definition->storage[i]); }
  *out_count = used;
  return entry->scratch;
}
static bool state_entry(void *v, qa_item_id item, qa_inventory_entry *out) {
  entry_t *entry = v; const items_t *s = entry->s;
  const qa_native_item_storage *storage = storage_of(s, item);
  if (storage == NULL) return false;
  size_t rows = storage_rows(storage);
  qa_inventory_entry *values = calloc(rows ? rows : 1, sizeof *values);
  if (values == NULL) qa_range_error("Native items out of memory");
  read_storage(s, entry->actor.id, storage, values);
  bool found = false;
  for (size_t i = 0; i < rows && !found; i++) if (strcmp(values[i].item, item) == 0) { *out = values[i]; found = true; }
  free(values);
  return found;
}
static bool state_mutable_capacity(void *v, qa_item_id item) {
  entry_t *entry = v;
  const qa_native_item_storage *storage = storage_of(entry->s, item);
  return storage != NULL && strcmp(storage->kind, "counter") == 0 && strcmp(storage->capacity.kind, "field") == 0;
}
static void item_invoke(void *v, qa_item_id item, qa_source_item_action action) {
  entry_t *entry = v; items_t *s = entry->s;
  entry_t *current = find_entry(s, entry->actor.id);
  const qa_native_mod_item_definition *definition = find_definition(s->definition, item);
  const qa_native_mod_source_call *call = NULL;
  if (definition != NULL && definition->has_actions) call = action == QA_SOURCE_ITEM_ACTION_USE ? definition->action_use : definition->action_drop;
  if (current == NULL || !entry_current(s, current) || call == NULL) qa_range_error("Source item action is no longer admitted");
  s->operations.invoke(s->operations.ctx, entry->actor.id, call);
}
static void storage_written(void *v) {
  storage_observer *o = v;
  changed(o->s, o->entry, o->storage, NULL);
}
static bool requesting_has(const items_t *s, qa_actor_id actor) {
  for (size_t i = 0; i < s->requesting_count; i++) if (qa_same_actor(s->requesting[i], actor)) return true;
  return false;
}
static void requesting_add(items_t *s, qa_actor_id actor) {
  if (requesting_has(s, actor)) return;
  if (s->requesting_count == s->requesting_capacity) {
    size_t capacity = s->requesting_capacity ? s->requesting_capacity * 2 : 4;
    qa_actor_id *grown = realloc(s->requesting, capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("Native items out of memory");
    s->requesting = grown; s->requesting_capacity = capacity;
  }
  s->requesting[s->requesting_count++] = actor;
}
static void requesting_delete(items_t *s, qa_actor_id actor) {
  for (size_t i = 0; i < s->requesting_count; i++) if (qa_same_actor(s->requesting[i], actor)) { s->requesting[i] = s->requesting[--s->requesting_count]; return; }
}
static qa_item_id stage_selection(const items_t *s, qa_actor_id actor) {
  qa_item_id pending = qa_compat_q2_native_mod_weapon_stage_pending(s->stage, actor);
  return pending != NULL ? pending : qa_compat_q2_native_mod_weapon_stage_active(s->stage, actor);
}
static bool same_item(qa_item_id a, qa_item_id b) { return a == b || (a != NULL && b != NULL && strcmp(a, b) == 0); }
static void selection_written(void *v) {
  entry_t *entry = v; items_t *s = entry->s;
  if (!entry_current(s, entry)) return;
  qa_item_id item = stage_selection(s, entry->actor.id), previous = entry->source_selection;
  entry->source_selection = item;
  if (item != NULL && !same_item(item, previous) && !requesting_has(s, entry->actor.id) && s->services->weapons != NULL) {
    qa_weapon_reference reference = {s->provider, item};
    s->services->weapons->request(s->services->weapons->ctx, entry->actor.id, reference);
  }
}
/* --- Requests --- */
static const char *capability_status(void *v) {
  capability_t *c = v;
  return entry_current(c->s, c->entry) && c->entry->request == c->request ? c->request->status : "refused";
}
static void capability_cancel(void *v) {
  capability_t *c = v;
  if (entry_current(c->s, c->entry) && c->entry->request == c->request && strcmp(c->request->status, "pending") == 0) { c->request->status = "refused"; c->entry->request = NULL; }
}
static qa_source_weapon_request capability(entry_t *entry, request_t *request) {
  items_t *s = entry->s;
  capability_t *c = malloc(sizeof *c);
  if (c == NULL) qa_range_error("Native items out of memory");
  *c = (capability_t){s, entry, request, s->owned_capabilities};
  s->owned_capabilities = c;
  return (qa_source_weapon_request){c, request->id, capability_status, capability_cancel};
}
static request_t *new_request(items_t *s, double id, qa_item_id item, const char *status) {
  request_t *request = malloc(sizeof *request);
  if (request == NULL) qa_range_error("Native items out of memory");
  *request = (request_t){id, item, status, s->owned_requests};
  s->owned_requests = request;
  return request;
}
static bool binding_accepts(void *v, qa_item_id item) {
  entry_t *entry = v; items_t *s = entry->s;
  if (!entry_current(s, entry) || s->definition->weapons == NULL) return false;
  bool declared = false;
  const qa_native_weapon_selection *selection = &s->definition->weapons->selection;
  for (size_t i = 0; i < selection->value_count; i++) if (strcmp(selection->values[i].item, item) == 0) declared = true;
  return declared && qa_shared_inventory_table_count(s->services->inventory, entry->actor.id, item) > 0;
}
typedef struct { entry_t *entry; request_t *value; qa_item_id item; } request_call;
static void request_body(void *v) {
  request_call *c = v; entry_t *entry = c->entry; items_t *s = entry->s; qa_actor_id actor = entry->actor.id;
  if (!binding_accepts(entry, c->item) || !qa_compat_q2_native_mod_weapon_stage_request(s->stage, actor, c->item)) c->value->status = "refused";
  else if (same_item(qa_compat_q2_native_mod_weapon_stage_active(s->stage, actor), c->item)) c->value->status = "accepted";
}
static qa_source_weapon_request binding_resume(void *v, qa_item_id item) {
  entry_t *entry = v; items_t *s = entry->s; qa_actor_id actor = entry->actor.id;
  if (!entry_current(s, entry)) qa_range_error("Native weapon request lost its original actor");
  request_t *value = new_request(s, ++s->next_request, item, "pending");
  entry->request = value;
  if (item == NULL) value->status = qa_compat_q2_native_mod_weapon_stage_active(s->stage, actor) != NULL && qa_compat_q2_native_mod_weapon_stage_settled(s->stage, actor) ? "accepted" : "refused";
  else {
    requesting_add(s, actor);
    request_call call = {entry, value, item};
    qa_compat_q2_native_thrown thrown;
    bool completed = qa_compat_q2_native_try(request_body, &call, &thrown);
    requesting_delete(s, actor);
    if (!completed) qa_compat_q2_native_rethrow(&thrown);
  }
  return capability(entry, value);
}
static bool binding_select(void *v, qa_item_id item) {
  if (!binding_accepts(v, item)) return false;
  qa_source_weapon_request request = binding_resume(v, item);
  return strcmp(request.status(request.ctx), "refused") != 0;
}
static void binding_holster(void *v) { entry_t *entry = v; if (!entry_current(entry->s, entry)) qa_range_error("Native weapon retired while holstering"); }
static bool binding_is_holstered(void *v) { entry_t *entry = v; return entry_current(entry->s, entry) && qa_compat_q2_native_mod_weapon_stage_settled(entry->s->stage, entry->actor.id); }
static qa_source_weapon_request binding_restore_request(void *v, double id, qa_item_id item) {
  entry_t *entry = v; request_t *request = entry->request;
  if (!entry_current(entry->s, entry) || request == NULL || request->id != id || !same_item(request->item, item)) qa_range_error("Saved native weapon request differs from its source owner");
  return capability(entry, request);
}
static bool binding_current(void *v) { entry_t *entry = v; return entry_current(entry->s, entry); }
static qa_source_weapon_presentation binding_read(void *v) {
  entry_t *entry = v; items_t *s = entry->s; qa_actor_id actor = entry->actor.id;
  if (!entry_current(s, entry)) qa_range_error("Native weapon presentation lost its source owner");
  qa_source_weapon_presentation out;
  memset(&out, 0, sizeof out);
  out.source.provider = s->provider; out.source.content = s->content;
  out.active = qa_compat_q2_native_mod_weapon_stage_active(s->stage, actor);
  out.pending = qa_compat_q2_native_mod_weapon_stage_pending(s->stage, actor);
  out.model = s->operations.model(s->operations.ctx, actor);
  out.items = s->definitions; out.item_count = s->definition->definition_count;
  return out;
}
/* --- Stage operations --- */
static bool stage_actor(void *v, const char *record, qa_guest_address address, qa_actor_id *out) {
  items_t *s = v;
  for (size_t i = 0; i < s->entry_count; i++) {
    entry_t *entry = s->entries[i];
    if (s->operations.live(s->operations.ctx, entry->actor.id) && s->operations.pointer(s->operations.ctx, entry->actor.id, record).byte_offset == address.byte_offset) {
      if (!entry_current(s, entry)) throw_retired(entry);
      *out = entry->actor.id;
      return true;
    }
  }
  return false;
}
static bool stage_current(void *v, qa_actor_id actor) { items_t *s = v; entry_t *entry = find_entry(s, actor); return entry != NULL && entry_current(s, entry); }
static bool stage_selected(void *v, qa_actor_id actor) { items_t *s = v; return s->services->weapons != NULL && s->services->weapons->selected(s->services->weapons->ctx, actor, s->provider); }
static qa_guest_address stage_pointer(void *v, qa_actor_id actor, qa_native_item_pointer field) { return field_address(v, actor, field.record, field.offset); }
static qa_guest_address stage_resolve(void *v, qa_native_mod_address address) { items_t *s = v; return s->operations.resolve(s->operations.ctx, address); }
static double stage_read(void *v, qa_actor_id actor, qa_native_item_field field) { items_t *s = v; return op_read(s, field_address(s, actor, field.record, field.field.offset), field.field.encoding); }
static void stage_write(void *v, qa_actor_id actor, qa_native_item_field field, double value) { items_t *s = v; op_write(s, field_address(s, actor, field.record, field.field.offset), field.field.encoding, value); }
static void stage_invoke(void *v, qa_actor_id actor, const qa_native_mod_source_call *call) { items_t *s = v; s->operations.invoke(s->operations.ctx, actor, call); }
static void stage_completed(void *v, qa_actor_id actor, bool reached) {
  items_t *s = v; (void)reached;
  entry_t *entry = find_entry(s, actor);
  if (entry == NULL || entry->request == NULL || strcmp(entry->request->status, "pending") != 0 || s->stage == NULL) return;
  if (same_item(qa_compat_q2_native_mod_weapon_stage_active(s->stage, actor), entry->request->item)) entry->request->status = "accepted";
  else if (!same_item(qa_compat_q2_native_mod_weapon_stage_pending(s->stage, actor), entry->request->item)) entry->request->status = "refused";
}
static bool cancel_accepts(void *v, const qa_compat_q2_native_thrown *error) { return error->token == v; }
static bool cancel_source_current(void *v) {
  entry_t *entry = v; items_t *s = entry->s; qa_owned_actor owner;
  return qa_session_actor_registry_resolve_owned(s->services->actors, entry->actor.id, &owner) && qa_same_owned_actor(owner, entry->actor) && s->operations.live(s->operations.ctx, entry->actor.id);
}
static bool no_accepts(void *v, const qa_compat_q2_native_thrown *error) { (void)v; (void)error; return false; }
static bool no_source_current(void *v) { (void)v; return false; }
static bool stage_cancellation(void *v, qa_actor_id actor, qa_compat_q2_native_weapon_cancellation *out) {
  entry_t *entry = find_entry(v, actor);
  if (entry != NULL) *out = (qa_compat_q2_native_weapon_cancellation){entry, cancel_accepts, cancel_source_current};
  else *out = (qa_compat_q2_native_weapon_cancellation){NULL, no_accepts, no_source_current};
  return true;
}
qa_compat_q2_native_mod_items *qa_compat_q2_native_mod_items_create(const qa_native_mod_items *definition, qa_native_abi abi, qa_compat_q2_native_mod_item_host host, const qa_mod_host_services *services, qa_provider_id provider, qa_content_id content, qa_compat_q2_native_mod_item_operations operations) {
  items_t *s = calloc(1, sizeof *s);
  size_t count = definition->definition_count;
  if (s == NULL) qa_range_error("Native items out of memory");
  s->definition = definition; s->host = host; s->services = services; s->provider = provider; s->content = content; s->operations = operations;
  s->admissions = calloc(count ? count : 1, sizeof *s->admissions);
  s->definitions = calloc(count ? count : 1, sizeof *s->definitions);
  s->icons = calloc(count ? count : 1, sizeof *s->icons);
  s->actions = calloc(count ? count * 2 : 1, sizeof *s->actions);
  if (s->admissions == NULL || s->definitions == NULL || s->icons == NULL || s->actions == NULL) qa_range_error("Native items out of memory");
  for (size_t i = 0; i < count; i++) {
    const qa_native_mod_item_definition *source = &definition->definitions[i];
    qa_source_item_definition *out = &s->definitions[i];
    out->item = source->item; out->label = source->label;
    out->source.provider = provider; out->source.content = content;
    out->has_icon = source->has_icon;
    if (source->has_icon && source->icon != NULL) { s->icons[i] = qa_resolve_item_icon(*source->icon, content); out->icon = &s->icons[i]; }
    if (source->has_actions) {
      qa_source_item_action_calls calls = {source->action_use, source->action_drop};
      out->actions = &s->actions[i * 2];
      out->action_count = qa_source_item_action_names(calls, &s->actions[i * 2], 2);
    }
    out->is_weapon = source->is_weapon; out->weapon = source->weapon;
    s->admissions[i].definition = *out; s->admissions[i].admission = source->admission;
  }
  if (definition->weapons != NULL) {
    qa_compat_q2_native_weapon_operations stage;
    memset(&stage, 0, sizeof stage);
    stage.ctx = s; stage.actor = stage_actor; stage.current = stage_current; stage.selected = stage_selected;
    stage.pointer = stage_pointer; stage.resolve = stage_resolve; stage.read = stage_read; stage.write = stage_write;
    stage.invoke = stage_invoke; stage.completed = stage_completed; stage.cancellation = stage_cancellation;
    qa_compat_q2_native_weapon_host stage_host = {host.memory, host.runner, host.image_base, host.ctx, host.invoke, host.entry};
    s->stage = qa_compat_q2_native_mod_weapon_stage_create(definition->weapons, abi, stage_host, provider, stage);
  }
  return s;
}
static void push_removal(entry_t *entry, uint64_t handle) {
  if (entry->removal_count == entry->removal_capacity) {
    size_t capacity = entry->removal_capacity ? entry->removal_capacity * 2 : 8;
    uint64_t *grown = realloc(entry->removals, capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("Native items out of memory");
    entry->removals = grown; entry->removal_capacity = capacity;
  }
  entry->removals[entry->removal_count++] = handle;
}
static void add_address(entry_t *entry, const items_t *s, qa_actor_id actor, const char *record) {
  entry->addresses[entry->address_count++] = (record_address){record, s->operations.pointer(s->operations.ctx, actor, record).byte_offset};
  for (size_t i = 0; i + 1 < entry->address_count; i++) if (strcmp(entry->addresses[i].record, record) == 0) { entry->addresses[i].address = entry->addresses[entry->address_count - 1].address; entry->address_count--; return; }
}
void qa_compat_q2_native_mod_items_admit(items_t *s, qa_actor_id actor) {
  if (find_entry(s, actor) != NULL) return;
  qa_owned_actor owner;
  if (!qa_session_actor_registry_resolve_owned(s->services->actors, actor, &owner) || !s->operations.live(s->operations.ctx, actor)) qa_range_error("Native items require the current admitted client");
  const qa_native_mod_items *definition = s->definition;
  entry_t *entry = calloc(1, sizeof *entry);
  if (entry == NULL) qa_range_error("Native items out of memory");
  entry->s = s; entry->actor = owner; entry->next_owned = s->owned_entries; s->owned_entries = entry;
  entry->addresses = calloc(definition->storage_count * 2 + 1, sizeof *entry->addresses);
  entry->storage_observers = calloc(definition->storage_count + 1, sizeof *entry->storage_observers);
  size_t rows = total_rows(s);
  entry->previous = calloc(rows ? rows : 1, sizeof *entry->previous);
  if (entry->addresses == NULL || entry->storage_observers == NULL || entry->previous == NULL) qa_range_error("Native items out of memory");
  for (size_t i = 0; i < definition->storage_count; i++) {
    const qa_native_item_storage *storage = &definition->storage[i];
    add_address(entry, s, actor, storage->field.record);
    if (strcmp(storage->kind, "counter") == 0 && strcmp(storage->capacity.kind, "field") == 0) add_address(entry, s, actor, storage->capacity.field.record);
    read_storage(s, actor, storage, entry->previous + entry->previous_count);
    entry->previous_count += storage_rows(storage);
  }
  qa_inventory_item_binding binding;
  memset(&binding, 0, sizeof binding);
  binding.owner = s->provider; binding.items = s->admissions; binding.item_count = definition->definition_count;
  binding.state = (qa_inventory_state_binding){entry, state_read, state_entry, state_write, state_mutable_capacity};
  binding.invoke_ctx = entry; binding.invoke = item_invoke;
  entry->lease = qa_shared_inventory_table_bind_items(s->services->inventory, owner, binding);
  entry->source_selection = s->stage != NULL ? stage_selection(s, actor) : NULL;
  if (s->entry_count == s->entry_capacity) {
    size_t capacity = s->entry_capacity ? s->entry_capacity * 2 : 4;
    entry_t **grown = realloc(s->entries, capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("Native items out of memory");
    s->entries = grown; s->entry_capacity = capacity;
  }
  s->entries[s->entry_count++] = entry;
  qa_compat_q2_memory *memory = s->host.memory;
  for (size_t i = 0; i < definition->storage_count; i++) {
    const qa_native_item_storage *storage = &definition->storage[i];
    entry->storage_observers[i] = (storage_observer){s, entry, storage};
    qa_guest_address address = field_address(s, actor, storage->field.record, storage->field.field.offset);
    push_removal(entry, memory->observe_writes(memory->ctx, address, scalar_bytes(storage->field), &entry->storage_observers[i], storage_written));
    if (strcmp(storage->kind, "counter") == 0 && strcmp(storage->capacity.kind, "field") == 0) {
      address = field_address(s, actor, storage->capacity.field.record, storage->capacity.field.field.offset);
      push_removal(entry, memory->observe_writes(memory->ctx, address, scalar_bytes(storage->capacity.field), &entry->storage_observers[i], storage_written));
    }
    if (strcmp(storage->kind, "counter") == 0 && strcmp(storage->capacity.kind, "source") == 0) {
      address = s->operations.resolve(s->operations.ctx, storage->capacity.address);
      push_removal(entry, memory->observe_writes(memory->ctx, address, qa_compat_q2_native_storage_bytes(storage->capacity.encoding, memory->pointer_bytes), &entry->storage_observers[i], storage_written));
    }
  }
  if (s->stage != NULL) {
    if (s->services->weapons == NULL) qa_range_error("Native source weapon service is unavailable");
    qa_source_weapon_binding weapon;
    memset(&weapon, 0, sizeof weapon);
    weapon.ctx = entry; weapon.current = binding_current; weapon.read = binding_read;
    weapon.handoff.ctx = entry; weapon.handoff.provider = s->provider; weapon.handoff.accepts = binding_accepts; weapon.handoff.select = binding_select;
    weapon.handoff.holster = binding_holster; weapon.handoff.is_holstered = binding_is_holstered; weapon.handoff.is_source_input = true;
    weapon.handoff.resume_request = binding_resume; weapon.handoff.restore_request = binding_restore_request;
    entry->remove_weapon = s->services->weapons->bind(s->services->weapons->ctx, owner, weapon);
    entry->has_remove_weapon = true;
    const qa_native_weapon_selection *selection = &definition->weapons->selection;
    push_removal(entry, memory->observe_writes(memory->ctx, field_address(s, actor, selection->active.record, selection->active.offset), (double)memory->pointer_bytes, entry, selection_written));
    if (selection->pending != NULL) push_removal(entry, memory->observe_writes(memory->ctx, field_address(s, actor, selection->pending->record, selection->pending->offset), (double)memory->pointer_bytes, entry, selection_written));
  }
}
bool qa_compat_q2_native_item_cancellation_accepts(const qa_compat_q2_native_item_cancellation *c, const qa_compat_q2_native_thrown *error) { return c->entry != NULL && error->token == c->entry; }
bool qa_compat_q2_native_item_cancellation_source_current(const qa_compat_q2_native_item_cancellation *c) {
  if (c->entry == NULL) return false;
  const items_t *s = c->items; qa_owned_actor owner;
  return qa_session_actor_registry_resolve_owned(s->services->actors, c->actor, &owner) && qa_same_owned_actor(owner, c->owned) && s->operations.live(s->operations.ctx, c->actor);
}
qa_compat_q2_native_item_cancellation qa_compat_q2_native_mod_items_cancellation(items_t *s, qa_actor_id actor) {
  entry_t *entry = find_entry(s, actor);
  qa_compat_q2_native_item_cancellation out;
  memset(&out, 0, sizeof out);
  out.items = s; out.entry = entry; out.actor = actor;
  if (entry != NULL) out.owned = entry->actor;
  return out;
}
bool qa_compat_q2_native_mod_items_accepts_attack(items_t *s, qa_actor_id actor) {
  return s->stage == NULL || (s->services->weapons != NULL && s->services->weapons->selected(s->services->weapons->ctx, actor, s->provider)) || qa_compat_q2_native_mod_weapon_stage_continuing(s->stage, actor);
}
qa_compat_q2_native_item_checkpoint qa_compat_q2_native_mod_items_checkpoint(const items_t *s) {
  qa_compat_q2_native_item_checkpoint_request *requests = calloc(s->entry_count ? s->entry_count : 1, sizeof *requests);
  if (requests == NULL) qa_range_error("Native items out of memory");
  size_t used = 0;
  for (size_t i = 0; i < s->entry_count; i++) {
    const request_t *request = s->entries[i]->request;
    if (request != NULL) requests[used++] = (qa_compat_q2_native_item_checkpoint_request){qa_saved_actor_id_of(s->entries[i]->actor.id), request->id, request->item, request->status};
  }
  return (qa_compat_q2_native_item_checkpoint){s->next_request, requests, used};
}
void qa_compat_q2_native_mod_items_restore(items_t *s, const qa_compat_q2_native_item_checkpoint *value) {
  s->next_request = value->next_request;
  for (size_t i = 0; i < value->request_count; i++) {
    const qa_compat_q2_native_item_checkpoint_request *row = &value->requests[i];
    qa_actor_id actor = s->services->reference_saved != NULL ? s->services->reference_saved(s->services->ctx, row->actor) : qa_session_actor_registry_reference_saved(s->services->actors, row->actor, "current");
    entry_t *entry = find_entry(s, actor);
    if (entry == NULL || !entry_current(s, entry)) qa_range_error("Saved native request lost its exact source owner");
    entry->request = new_request(s, row->id, row->item, row->status);
  }
}
void qa_compat_q2_native_mod_items_release(items_t *s, qa_actor_id actor) {
  entry_t *entry = find_entry(s, actor);
  if (entry == NULL) return;
  for (size_t i = 0; i < s->entry_count; i++) if (s->entries[i] == entry) { memmove(&s->entries[i], &s->entries[i + 1], (s->entry_count - i - 1) * sizeof *s->entries); s->entry_count--; break; }
  entry->request = NULL;
  for (size_t i = entry->removal_count; i-- > 0;) s->host.memory->unobserve_writes(s->host.memory->ctx, entry->removals[i]);
  entry->removal_count = 0;
  if (entry->has_remove_weapon) entry->remove_weapon.invoke(entry->remove_weapon.ctx);
  entry->has_remove_weapon = false;
  entry->lease.close(entry->lease.ctx);
}
void qa_compat_q2_native_mod_items_clear(items_t *s) { while (s->entry_count != 0) qa_compat_q2_native_mod_items_release(s, s->entries[0]->actor.id); }
void qa_compat_q2_native_mod_items_close(items_t *s) {
  if (s->stage != NULL) qa_compat_q2_native_mod_weapon_stage_close(s->stage);
  qa_compat_q2_native_mod_items_clear(s);
}
void qa_compat_q2_native_mod_items_destroy(items_t *s) {
  if (s == NULL) return;
  qa_compat_q2_native_mod_items_close(s);
  if (s->stage != NULL) qa_compat_q2_native_mod_weapon_stage_destroy(s->stage);
  for (entry_t *entry = s->owned_entries; entry != NULL;) {
    entry_t *next = entry->next_owned;
    free(entry->addresses); free(entry->removals); free(entry->storage_observers); free(entry->previous); free(entry->scratch); free(entry);
    entry = next;
  }
  for (request_t *request = s->owned_requests; request != NULL;) { request_t *next = request->next_owned; free(request); request = next; }
  for (capability_t *c = s->owned_capabilities; c != NULL;) { capability_t *next = c->next_owned; free(c); c = next; }
  free(s->admissions); free(s->definitions); free(s->icons); free(s->actions); free(s->entries); free(s->requesting); free(s);
}
