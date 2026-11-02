/* See include/qa/compat/qvm/mod_items.h. GPL-2.0-or-later. */
#include "qa/compat/qvm/mod_items.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/compat/qvm/item_storage.h"
#include "qa/compat/qvm/operations.h"
#include "qa/compat/qvm/primary_inventory_profile.h"
#include "qa/content/item_icon.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/persistence/save_image.h"

#define QVM_MAX_PRIVATE_ARGUMENT_WORDS 62

static void *xmalloc(size_t size) {
  void *result = malloc(size == 0 ? 1 : size);
  if (result == NULL) qa_range_error("QVM memory allocation failed");
  return result;
}

static bool integer_word(double value) {
  return isfinite(value) && value == trunc(value) && value >= -2147483648.0 && value <= 2147483647.0;
}

typedef struct validate_state {
  const qa_qvm_mod_callback_declaration *declaration;
  /* Occupied (record, offset) pairs; TS keys are full strings. */
  const char **key_records;
  double *key_offsets;
  const char **usages;
  size_t count;
  size_t capacity;
} validate_state;

static void validate_field(void *ctx, const qa_qvm_item_field *field, const char *usage) {
  validate_state *state = ctx;
  const qa_qvm_mod_callback_declaration *declaration = state->declaration;
  const qa_qvm_mod_actor_record *record = NULL;
  for (size_t i = 0; i < declaration->actor_record_count; i++)
    if (strcmp(declaration->actor_records[i].id, field->record) == 0) { record = &declaration->actor_records[i]; break; } /* TS Array.find: first */
  bool in_clients = false;
  if (record != NULL && declaration->clients != NULL) {
    for (size_t i = 0; i < declaration->clients->records.count; i++)
      if (strcmp(declaration->clients->records.values[i], record->id) == 0) in_clients = true;
  }
  const char *previous = NULL;
  for (size_t i = 0; i < state->count; i++)
    if (strcmp(state->key_records[i], field->record) == 0 && state->key_offsets[i] == field->offset)
      previous = state->usages[i];
  if (record == NULL || !in_clients || !isfinite(field->offset) || field->offset != trunc(field->offset) ||
      field->offset < -9007199254740991.0 || field->offset > 9007199254740991.0 || field->offset < 0 ||
      (int64_t)field->offset % 4 != 0 || field->offset + 4 > record->stride ||
      (strcmp(usage, "view") != 0 && previous != NULL &&
       !(strcmp(usage, "capacity") == 0 && strcmp(previous, "capacity") == 0)))
    qa_range_error("Invalid QVM item source field");
  if (strcmp(usage, "view") != 0) {
    if (state->count == state->capacity) {
      state->capacity = state->capacity == 0 ? 16 : state->capacity * 2;
      state->key_records = realloc(state->key_records, state->capacity * sizeof *state->key_records);
      state->key_offsets = realloc(state->key_offsets, state->capacity * sizeof *state->key_offsets);
      state->usages = realloc(state->usages, state->capacity * sizeof *state->usages);
      if (state->key_records == NULL || state->key_offsets == NULL || state->usages == NULL)
        qa_range_error("QVM memory allocation failed");
    }
    state->key_records[state->count] = field->record;
    state->key_offsets[state->count] = field->offset;
    state->usages[state->count++] = usage;
  }
  for (size_t i = 0; i < record->field_count; i++) {
    const qa_qvm_mod_actor_field *value = &record->fields[i];
    double length;
    if (strcmp(value->binding, "private") == 0) length = value->byte_length;
    else if (strcmp(value->binding, "origin") == 0 || strcmp(value->binding, "velocity") == 0 ||
             strcmp(value->binding, "angles") == 0 || strcmp(value->binding, "bounds-min") == 0 ||
             strcmp(value->binding, "bounds-max") == 0 || strcmp(value->binding, "constant-vector") == 0)
      length = 12;
    else length = 4;
    if (value->offset < field->offset + 4 && field->offset < value->offset + length &&
        strcmp(value->binding, "private") != 0 && strcmp(value->binding, "constant") != 0)
      qa_range_error("QVM item field overlaps another canonical source projection");
  }
}

static const qa_qvm_instruction *insn(const qa_qvm_instruction *instructions, size_t count, int64_t index) {
  if (index < 0 || (uint64_t)index >= count) return NULL;
  return &instructions[index];
}

/* Adapter onto the landed validator shape: usage NULL is the TS
 * undefined, defaulted to "storage" by the TS field callback. */
static void validate_field_validator(void *ctx, qa_qvm_item_field field, const char *usage) {
  validate_field(ctx, &field, usage == NULL ? "storage" : usage);
}

void qa_qvm_validate_mod_items(const qa_qvm_mod_items *items, const qa_qvm_mod_callback_declaration *declaration,
                               const qa_qvm_instruction *instructions, size_t instruction_count,
                               int64_t initialized_data_length, int64_t bss_length) {
  if (declaration->clients == NULL || items->definition_count == 0)
    qa_range_error("QVM items require source client admission");
  for (size_t i = 0; i < items->definition_count; i++)
    for (size_t j = i + 1; j < items->definition_count; j++)
      if (strcmp(items->definitions[i].item, items->definitions[j].item) == 0)
        qa_range_error("Duplicate QVM source item definition");
  validate_state state = {declaration, NULL, NULL, NULL, 0, 0};
  qa_item_id *ids = xmalloc(items->definition_count * sizeof *ids);
  for (size_t i = 0; i < items->definition_count; i++) ids[i] = items->definitions[i].item;
  qa_qvm_image image;
  memset(&image, 0, sizeof image);
  image.instructions = (qa_qvm_instruction *)instructions;
  image.instruction_count = instruction_count;
  image.initialized_length = (size_t)initialized_data_length;
  image.bss_length = bss_length;
  qa_qvm_item_field_validator validator = {&state, validate_field_validator};
  qa_qvm_validate_item_storage(items->storage, items->storage_count, ids, items->definition_count,
                               &image, validator);
  free(ids);
  size_t weapon_count = 0;
  for (size_t i = 0; i < items->definition_count; i++)
    if (items->definitions[i].is_weapon) weapon_count++;
  if ((weapon_count == 0) != (items->weapons == NULL))
    qa_range_error("QVM weapon items require an original source consumer");
  if (items->weapons != NULL) {
    const qa_qvm_weapon_stage *stage = &items->weapons->stage;
    qa_qvm_validate_weapon_stage(stage, instructions, instruction_count);
    const qa_qvm_mod_input_pointer *pointers[] = {&stage->dispatcher.actor.pointer,
                                                  &stage->continuation.actor.pointer,
                                                  &stage->continuation.projection.movement};
    const char *records[] = {stage->dispatcher.actor.record, stage->continuation.actor.record, NULL};
    for (int p = 0; p < 3; p++) {
      if (records[p] != NULL) {
        bool found = false;
        for (size_t i = 0; i < declaration->clients->records.count; i++)
          if (strcmp(declaration->clients->records.values[i], records[p]) == 0) found = true;
        if (!found) qa_range_error("QVM weapon pointer is not an admitted client record");
      }
      const qa_qvm_mod_input_pointer *source = pointers[p];
      if (!source->is_global
              ? source->index != trunc(source->index) || source->index < 0 || source->index >= QVM_MAX_PRIVATE_ARGUMENT_WORDS
              : source->address != trunc(source->address) || source->address < 0 ||
                    (int64_t)source->address % 4 != 0 || source->address + 4 > (double)(initialized_data_length + bss_length))
        qa_range_error("QVM weapon pointer exceeds its original call ABI");
      for (size_t i = 0; i < source->indirections.count; i++) {
        double value = source->indirections.values[i];
        if (!isfinite(value) || value != trunc(value) || value < -9007199254740991.0 ||
            value > 9007199254740991.0 || value < 0 || (int64_t)value % 4 != 0)
          qa_range_error("QVM weapon pointer is not aligned source storage");
      }
      if (!isfinite(source->offset) || source->offset != trunc(source->offset) || source->offset < 0 ||
          (int64_t)source->offset % 4 != 0)
        qa_range_error("QVM weapon pointer is not aligned source storage");
    }
    const qa_qvm_weapon_projection *projection = &stage->continuation.projection;
    if (!isfinite(projection->byte_length) || projection->byte_length != trunc(projection->byte_length) ||
        projection->byte_length < -9007199254740991.0 || projection->byte_length > 9007199254740991.0 ||
        projection->byte_length < 12 || (int64_t)projection->byte_length % 4 != 0)
      qa_range_error("QVM weapon movement projection lacks its caller layout");
    double bounds[] = {projection->minimum, projection->maximum};
    for (int i = 0; i < 2; i++) {
      if (!isfinite(bounds[i]) || bounds[i] != trunc(bounds[i]) || bounds[i] < -9007199254740991.0 ||
          bounds[i] > 9007199254740991.0 || bounds[i] < 0 || (int64_t)bounds[i] % 4 != 0 ||
          bounds[i] + 12 > projection->byte_length)
        qa_range_error("QVM weapon bounds exceed their original caller storage");
    }
    if (fabs(projection->minimum - projection->maximum) < 12)
      qa_range_error("QVM weapon caller bounds overlap");
    double entry = items->weapons->input.entry;
    size_t input_matches = 0;
    const qa_mod_client_input_binding *matched = NULL;
    for (size_t i = 0; i < declaration->clients->input_count; i++) {
      for (size_t c = 0; c < declaration->clients->input[i].call_count; c++) {
        if (((const qa_qvm_mod_source_call *)declaration->clients->input[i].calls)[c].entry == entry) {
          input_matches++;
          matched = &declaration->clients->input[i];
        }
      }
    }
    const qa_qvm_instruction *instruction = insn(instructions, instruction_count, (int64_t)entry);
    if (instruction == NULL || instruction->opcode != QA_QVMY_OP_ENTER || input_matches != 1 ||
        matched == NULL || strcmp(matched->scope, "movement-slice") != 0 || strcmp(matched->phase, "after") != 0)
      qa_range_error("QVM weapon input requires one declared original callback after authoritative movement");
    validate_field(&state, &stage->selection.field, "view");
    validate_field(&state, &items->weapons->input.clock, "view");
    validate_field(&state, &projection->view_height, "view");
    validate_field(&state, &projection->ground, "view");
    const qa_qvm_item_test *tests[3] = {stage->settled, stage->request.accepted, stage->continuation.when};
    size_t test_counts[3] = {stage->settled_count, stage->request.accepted_count, stage->continuation.when_count};
    for (int t = 0; t < 3; t++) {
      for (size_t i = 0; i < test_counts[t]; i++) {
        validate_field(&state, &tests[t][i].field, "view");
        if (!integer_word(tests[t][i].value) ||
            (tests[t][i].has_mask && (!isfinite(tests[t][i].mask) || tests[t][i].mask != trunc(tests[t][i].mask) ||
                                      tests[t][i].mask < 0 || tests[t][i].mask > 2147483647.0)))
          qa_range_error("Invalid QVM weapon source state predicate");
      }
    }
    bool selection_differs = stage->selection.value_count != weapon_count;
    for (size_t i = 0; i < stage->selection.value_count && !selection_differs; i++) {
      for (size_t j = i + 1; j < stage->selection.value_count; j++) {
        if (stage->selection.values[i].value == stage->selection.values[j].value ||
            strcmp(stage->selection.values[i].item, stage->selection.values[j].item) == 0)
          selection_differs = true;
      }
      bool is_weapon = false;
      for (size_t w = 0; w < items->definition_count; w++)
        if (items->definitions[w].is_weapon && strcmp(items->definitions[w].item, stage->selection.values[i].item) == 0)
          is_weapon = true;
      if (!integer_word(stage->selection.values[i].value) || stage->selection.values[i].value < 1 || !is_weapon)
        selection_differs = true;
    }
    if (selection_differs) qa_range_error("QVM weapon selection differs from its admitted definitions");
    for (size_t i = 0; i < items->definition_count; i++) {
      if (!items->definitions[i].is_weapon || items->definitions[i].weapon.ammo == NULL) continue;
      bool found = false;
      for (size_t j = 0; j < items->definition_count; j++)
        if (strcmp(items->definitions[j].item, items->definitions[i].weapon.ammo) == 0) found = true;
      if (!found) qa_range_error("QVM weapon ammo lacks its source storage");
    }
  }
  free(state.key_records);
  free(state.key_offsets);
  free(state.usages);
}

/* Mirrored item-storage access (TS item-storage.ts). */
static int32_t capacity_constant(const qa_qvm_mod_items_runtime *items, double instruction) {
  const qa_qvm_instruction *value = insn(items->image->instructions, items->image->instruction_count,
                                                 (int64_t)instruction);
  if (value == NULL || value->opcode != QA_QVMY_OP_CONST || value->operand < 0)
    qa_range_error("QVM item capacity is not its declared original constant");
  return (int32_t)value->operand;
}

typedef struct request {
  int64_t id;
  qa_item_id item; /* nullable */
  const char *status;
  bool attempted;
} request;

typedef struct item_entry {
  qa_owned_actor actor;
  struct { const char *record; int64_t address; } *addresses; /* owned */
  size_t address_count;
  size_t address_capacity;
  void *observe_state; /* owned */
  void *lease_state;   /* owned */
  qa_source_item_lease lease;
  size_t watch_id;
  bool has_remove_weapon;
  qa_source_weapon_unbind remove_weapon;
  request *current_request;
} item_entry;

static int32_t read_word_at(qa_qvm_memory *memory, int64_t address) {
  qa_qvm_writable_view view;
  qa_qvm_memory_data_view(memory, address, 4, &view);
  return qa_qvm_writable_view_get_int32(&view, 0, true);
}

static int64_t address_of(qa_qvm_mod_items_runtime *items, qa_actor_id actor, const qa_qvm_item_field *field) {
  return items->operations->pointer(items->operations->ctx, actor, field->record) + (int64_t)field->offset;
}

static int32_t scalar_with_previous(qa_qvm_mod_items_runtime *items, int64_t address,
                                    const qa_qvm_committed_write *previous) {
  if (previous == NULL) return read_word_at(items->memory, address);
  /* TS slices 4 bytes and builds a 4-byte DataView, which throws
   * outside the allocation; validate before the raw copy. */
  qa_qvm_writable_view extent;
  qa_qvm_memory_data_view(items->memory, address, 4, &extent);
  uint8_t bytes[4];
  memcpy(bytes, items->memory->bytes + address, 4);
  for (size_t r = 0; r < previous->range_count; r++) {
    for (size_t i = 0; i < previous->ranges[r].length; i++) {
      int64_t target = previous->ranges[r].byte_offset + (int64_t)i - address;
      if (target >= 0 && target < 4) bytes[target] = previous->ranges[r].before[i];
    }
  }
  int32_t value;
  memcpy(&value, bytes, 4);
  return value;
}

static int32_t item_capacity(qa_qvm_mod_items_runtime *items, qa_actor_id actor,
                             const qa_qvm_item_capacity *capacity, const qa_qvm_committed_write *previous) {
  if (strcmp(capacity->kind, "constant") == 0) return (int32_t)capacity->value;
  if (strcmp(capacity->kind, "field") == 0)
    return scalar_with_previous(items, address_of(items, actor, &capacity->field), previous);
  for (size_t i = 0; i < capacity->override_count; i++) {
    const qa_qvm_item_capacity_override *override = &capacity->overrides[i];
    int32_t selected = scalar_with_previous(items, (int64_t)override->address, previous);
    bool matches = strcmp(override->comparison, "equals") == 0 ? selected == (int32_t)override->value
                                                                : selected != (int32_t)override->value;
    if (matches) return capacity_constant(items, override->instruction);
  }
  return capacity_constant(items, capacity->instruction);
}

static size_t read_storage(qa_qvm_mod_items_runtime *items, qa_actor_id actor,
                           const qa_qvm_item_storage *storage, const qa_qvm_committed_write *previous,
                           qa_inventory_entry *out) {
  int32_t count = scalar_with_previous(items, address_of(items, actor, &storage->field), previous);
  if (strcmp(storage->kind, "counter") == 0) {
    out[0].item = storage->item;
    out[0].count = count;
    out[0].capacity = item_capacity(items, actor, &storage->capacity, previous);
    out[0].has_count_policy = true;
    out[0].count_policy.kind = "source-counter";
    out[0].count_policy.arithmetic = "int32";
    return 1;
  }
  uint32_t mask = (uint32_t)storage->private_mask;
  for (size_t i = 0; i < storage->item_count; i++) mask |= (uint32_t)storage->items[i].mask;
  if (((uint32_t)count & ~mask) != 0) qa_range_error("Original QVM inventory contains undeclared bits");
  for (size_t i = 0; i < storage->item_count; i++) {
    out[i].item = storage->items[i].item;
    out[i].count = ((uint32_t)count & (uint32_t)storage->items[i].mask) == 0 ? 0 : 1;
    out[i].capacity = 1;
    out[i].has_count_policy = false;
    out[i].count_policy.kind = NULL;
    out[i].count_policy.arithmetic = NULL;
  }
  return storage->item_count;
}

static const qa_qvm_item_storage *storage_for(qa_qvm_mod_items_runtime *items, qa_item_id item) {
  for (size_t i = 0; i < items->definition->storage_count; i++) {
    const qa_qvm_item_storage *storage = &items->definition->storage[i];
    if (strcmp(storage->kind, "counter") == 0) {
      if (strcmp(storage->item, item) == 0) return storage;
    } else {
      for (size_t j = 0; j < storage->item_count; j++)
        if (strcmp(storage->items[j].item, item) == 0) return storage;
    }
  }
  return NULL;
}

/* Entries are heap objects on a pointer array: weapon binding and
 * request capabilities capture entry pointers (TS Entry objects)
 * across later admits (realloc) and releases (memmove). */
static item_entry **entry_stack(qa_qvm_mod_items_runtime *items) {
  return (item_entry **)items->entries;
}

static item_entry *find_entry(qa_qvm_mod_items_runtime *items, qa_actor_id actor) {
  item_entry **entries = entry_stack(items);
  for (size_t i = 0; i < items->entry_count; i++)
    if (qa_same_actor(entries[i]->actor.id, actor)) return entries[i];
  return NULL;
}

static bool entry_current(qa_qvm_mod_items_runtime *items, item_entry *entry) {
  if (find_entry(items, entry->actor.id) != entry) return false;
  qa_owned_actor resolved;
  if (!items->services->resolve_owned(items->services->ctx, entry->actor.id, &resolved)) return false;
  /* TS resolveOwned(...) !== entry.actor is object identity; the landed
   * qa_owned_actor identity is id + token + owner (qa_q1b_owned_equal). */
  if (!qa_same_actor(resolved.id, entry->actor.id) || resolved.token != entry->actor.token ||
      !((resolved.owner == entry->actor.owner) ||
        (resolved.owner != NULL && entry->actor.owner != NULL &&
         strcmp(resolved.owner, entry->actor.owner) == 0)))
    return false;
  if (!items->operations->live(items->operations->ctx, entry->actor.id) || !entry->lease.current(entry->lease.ctx))
    return false;
  for (size_t i = 0; i < entry->address_count; i++)
    if (items->operations->pointer(items->operations->ctx, entry->actor.id, entry->addresses[i].record) !=
        entry->addresses[i].address)
      return false;
  return true;
}

static void write_storage(qa_qvm_mod_items_runtime *items, qa_actor_id actor, qa_inventory_entry entry) {
  item_entry *current = find_entry(items, actor);
  const qa_qvm_item_storage *storage = storage_for(items, entry.item);
  if (current == NULL || !entry_current(items, current) || storage == NULL)
    qa_range_error("QVM item storage is no longer admitted");
  if (strcmp(storage->kind, "counter") == 0) {
    if (!integer_word(entry.count) || !integer_word(entry.capacity) || entry.capacity < 0)
      qa_range_error("QVM item exceeds its signed source representation");
    if (strcmp(storage->capacity.kind, "field") != 0 &&
        (int32_t)entry.capacity != item_capacity(items, actor, &storage->capacity, NULL))
      qa_range_error("QVM capacity is owned by its original source");
    qa_qvm_writable_view view;
    qa_qvm_memory_data_view(items->memory, address_of(items, actor, &storage->field), 4, &view);
    qa_qvm_writable_view_set_int32(&view, 0, (int32_t)entry.count, true);
    if (strcmp(storage->capacity.kind, "field") == 0) {
      qa_qvm_memory_data_view(items->memory, address_of(items, actor, &storage->capacity.field), 4, &view);
      qa_qvm_writable_view_set_int32(&view, 0, (int32_t)entry.capacity, true);
    }
  } else {
    const qa_qvm_item_mask *bit = NULL;
    for (size_t i = 0; i < storage->item_count; i++)
      if (strcmp(storage->items[i].item, entry.item) == 0) bit = &storage->items[i];
    if (bit == NULL || entry.capacity != 1 || (entry.count != 0 && entry.count != 1))
      qa_range_error("QVM packed ownership requires one admitted bit");
    int32_t previous = read_word_at(items->memory, address_of(items, actor, &storage->field));
    qa_qvm_writable_view view;
    qa_qvm_memory_data_view(items->memory, address_of(items, actor, &storage->field), 4, &view);
    qa_qvm_writable_view_set_int32(&view, 0,
        entry.count == 0 ? (int32_t)((uint32_t)previous & ~(uint32_t)bit->mask)
                         : (int32_t)((uint32_t)previous | (uint32_t)bit->mask), true);
  }
}

typedef struct lease_state {
  qa_qvm_mod_items_runtime *items;
  qa_actor_id actor;
} lease_state;

static void lease_invoke(void *ctx, qa_item_id item, qa_source_item_action action) {
  lease_state *state = ctx;
  item_entry *current = find_entry(state->items, state->actor);
  const qa_qvm_mod_source_call *call = NULL;
  for (size_t i = 0; i < state->items->definition->definition_count; i++) {
    if (strcmp(state->items->definition->definitions[i].item, item) == 0)
      call = action == QA_SOURCE_ITEM_ACTION_USE ? state->items->definition->definitions[i].action_use
                                                  : state->items->definition->definitions[i].action_drop;
  }
  if (current == NULL || !entry_current(state->items, current) || call == NULL)
    qa_range_error("Source item action is no longer admitted");
  (void)state->items->operations->invoke(state->items->operations->ctx, state->actor, call);
}

static size_t lease_read(void *ctx, qa_inventory_entry *out, size_t capacity) {
  lease_state *state = ctx;
  size_t count = 0;
  for (size_t i = 0; i < state->items->definition->storage_count; i++) {
    qa_inventory_entry buffer[64];
    size_t read = read_storage(state->items, state->actor, &state->items->definition->storage[i], NULL, buffer);
    for (size_t j = 0; j < read; j++) {
      if (count < capacity) out[count] = buffer[j];
      count++;
    }
  }
  return count;
}

static bool lease_entry(void *ctx, qa_item_id item, qa_inventory_entry *out) {
  lease_state *state = ctx;
  const qa_qvm_item_storage *storage = storage_for(state->items, item);
  if (storage == NULL) return false;
  qa_inventory_entry buffer[64];
  size_t count = read_storage(state->items, state->actor, storage, NULL, buffer);
  for (size_t i = 0; i < count; i++) {
    if (strcmp(buffer[i].item, item) == 0) {
      *out = buffer[i];
      return true;
    }
  }
  return false;
}

static void lease_write(void *ctx, qa_inventory_entry entry) {
  lease_state *state = ctx;
  write_storage(state->items, state->actor, entry);
}

static bool lease_mutable_capacity(void *ctx, qa_item_id item) {
  lease_state *state = ctx;
  const qa_qvm_item_storage *storage = storage_for(state->items, item);
  return storage != NULL && strcmp(storage->kind, "counter") == 0 && strcmp(storage->capacity.kind, "field") == 0;
}

typedef struct observe_state {
  qa_qvm_mod_items_runtime *items;
  qa_actor_id actor;
  qa_source_item_store *pending; /* owned, grown on demand */
  size_t pending_count;
  size_t pending_capacity;
  bool has_pending;
} observe_state;

static void observe_publish(void *ctx, const qa_qvm_committed_write *event) {
  observe_state *state = ctx;
  qa_qvm_mod_items_runtime *items = state->items;
  state->pending_count = 0;
  state->has_pending = true;
  for (size_t s = 0; s < items->definition->storage_count; s++) {
    const qa_qvm_item_storage *storage = &items->definition->storage[s];
    size_t address_capacity = 2 + (strcmp(storage->kind, "counter") == 0 ? storage->capacity.override_count : 0);
    int64_t *addresses = xmalloc(address_capacity * sizeof *addresses);
    size_t address_count = 0;
    addresses[address_count++] = address_of(items, state->actor, &storage->field);
    if (strcmp(storage->kind, "counter") == 0 && strcmp(storage->capacity.kind, "field") == 0)
      addresses[address_count++] = address_of(items, state->actor, &storage->capacity.field);
    if (strcmp(storage->kind, "counter") == 0 && strcmp(storage->capacity.kind, "source") == 0) {
      for (size_t o = 0; o < storage->capacity.override_count; o++)
        addresses[address_count++] = (int64_t)storage->capacity.overrides[o].address;
    }
    bool touched = false;
    for (size_t a = 0; a < address_count; a++) {
      for (size_t r = 0; r < event->range_count; r++) {
        if (addresses[a] < event->ranges[r].byte_offset + (int64_t)event->ranges[r].length &&
            event->ranges[r].byte_offset < addresses[a] + 4)
          touched = true;
      }
    }
    free(addresses);
    if (!touched) continue;
    size_t entry_capacity = strcmp(storage->kind, "counter") == 0 ? 1 : storage->item_count;
    qa_inventory_entry *before = xmalloc(entry_capacity * sizeof *before);
    qa_inventory_entry *after = xmalloc(entry_capacity * sizeof *after);
    size_t before_count = read_storage(items, state->actor, storage, event, before);
    size_t after_count = read_storage(items, state->actor, storage, NULL, after);
    for (size_t i = 0; i < after_count; i++) {
      const qa_inventory_entry *previous = NULL;
      for (size_t j = 0; j < before_count; j++)
        if (strcmp(before[j].item, after[i].item) == 0) previous = &before[j];
      if (previous != NULL && (previous->count != after[i].count || previous->capacity != after[i].capacity)) {
        qa_original_pickup_execution pickup;
        bool has_pickup = items->operations->pickup != NULL &&
                          items->operations->pickup(items->operations->ctx, &pickup);
        if (has_pickup) {
          bool allowed = false;
          for (size_t w = 0; w < pickup.writes.count; w++) {
            const qa_pickup_write *write = &pickup.writes.values[w];
            if (write->kind == QA_PICKUP_RESOURCE_INVENTORY && strcmp(write->item, after[i].item) == 0 &&
                (previous->count == after[i].count || strcmp(write->fields, "capacity") != 0) &&
                (previous->capacity == after[i].capacity || strcmp(write->fields, "count") != 0))
              allowed = true;
          }
          if (!allowed) qa_range_error("Original pickup changed an undeclared QVM source item");
        }
        if (state->pending_count == state->pending_capacity) {
          state->pending_capacity = state->pending_capacity == 0 ? 8 : state->pending_capacity * 2;
          state->pending = realloc(state->pending, state->pending_capacity * sizeof *state->pending);
          if (state->pending == NULL) qa_range_error("QVM memory allocation failed");
        }
        state->pending[state->pending_count].before = *previous;
        state->pending[state->pending_count].after = after[i];
        state->pending_count++;
      }
    }
    free(before);
    free(after);
  }
}

static void observe_after(void *ctx, const qa_qvm_committed_write *event) {
  (void)event;
  observe_state *state = ctx;
  if (!state->has_pending) return;
  state->has_pending = false;
  item_entry *entry = find_entry(state->items, state->actor);
  if (state->pending_count != 0 && entry != NULL && entry_current(state->items, entry)) {
    entry->lease.stored(entry->lease.ctx, state->pending, state->pending_count);
    if (!entry_current(state->items, entry) && state->items->weapons != NULL)
      qa_qvm_mod_weapon_stage_cancel_retired(state->items->weapons, state->actor);
  }
}

static int64_t selection_value(qa_qvm_mod_items_runtime *items, qa_item_id item, bool *out_found) {
  *out_found = false;
  if (item == NULL || items->definition->weapons == NULL) return 0;
  for (size_t i = 0; i < items->definition->weapons->stage.selection.value_count; i++) {
    if (strcmp(items->definition->weapons->stage.selection.values[i].item, item) == 0) {
      *out_found = true;
      return (int64_t)items->definition->weapons->stage.selection.values[i].value;
    }
  }
  return 0;
}

typedef struct request_state {
  qa_qvm_mod_items_runtime *items;
  item_entry *entry;
  request *request;
} request_state;

static const char *request_status(void *ctx) {
  request_state *state = ctx;
  return entry_current(state->items, state->entry) && state->entry->current_request == state->request
      ? state->request->status : "refused";
}

static void request_cancel(void *ctx) {
  request_state *state = ctx;
  if (entry_current(state->items, state->entry) && state->entry->current_request == state->request &&
      strcmp(state->request->status, "pending") == 0) {
    state->request->status = "refused";
    state->entry->current_request = NULL;
  }
}

static qa_source_weapon_request make_capability(qa_qvm_mod_items_runtime *items, item_entry *entry, request *value) {
  request_state *state = xmalloc(sizeof *state);
  state->items = items;
  state->entry = entry;
  state->request = value;
  qa_source_weapon_request capability = {state, (double)value->id, request_status, request_cancel};
  return capability;
}

static bool binding_accepts(qa_qvm_mod_items_runtime *items, item_entry *entry, qa_item_id item) {
  bool found = false;
  (void)selection_value(items, item, &found);
  return entry_current(items, entry) && found &&
         items->services->inventory_count(items->services->ctx, entry->actor.id, item) > 0;
}

static qa_source_weapon_request binding_request(qa_qvm_mod_items_runtime *items, item_entry *entry, qa_item_id item) {
  if (!entry_current(items, entry)) qa_range_error("QVM source weapon request lost its actor");
  bool accepted = item == NULL ? qa_qvm_mod_weapon_stage_active(items->weapons, entry->actor.id) != NULL &&
                                      qa_qvm_mod_weapon_stage_settled(items->weapons, entry->actor.id)
                                : binding_accepts(items, entry, item);
  request *value = xmalloc(sizeof *value);
  value->id = ++items->next_request;
  value->item = item;
  value->status = accepted ? (item == NULL ? "accepted" : "pending") : "refused";
  value->attempted = false;
  entry->current_request = value;
  return make_capability(items, entry, value);
}

typedef struct binding_state {
  qa_qvm_mod_items_runtime *items;
  item_entry *entry;
} binding_state;

static bool handoff_accepts(void *ctx, qa_item_id item) {
  binding_state *state = ctx;
  return binding_accepts(state->items, state->entry, item);
}
static bool handoff_select(void *ctx, qa_item_id item) {
  binding_state *state = ctx;
  if (!binding_accepts(state->items, state->entry, item)) return false;
  qa_source_weapon_request created = binding_request(state->items, state->entry, item);
  return strcmp(created.status(created.ctx), "refused") != 0;
}
static void handoff_holster(void *ctx) {
  binding_state *state = ctx;
  if (!entry_current(state->items, state->entry)) qa_range_error("QVM source weapon retired while holstering");
}
static bool handoff_is_holstered(void *ctx) {
  binding_state *state = ctx;
  return entry_current(state->items, state->entry) &&
         qa_qvm_mod_weapon_stage_settled(state->items->weapons, state->entry->actor.id);
}
static qa_source_weapon_request handoff_resume(void *ctx, qa_item_id item) {
  binding_state *state = ctx;
  return binding_request(state->items, state->entry, item);
}
static qa_source_weapon_request handoff_restore(void *ctx, double id, qa_item_id item) {
  binding_state *state = ctx;
  request *saved = state->entry->current_request;
  if (!entry_current(state->items, state->entry) || saved == NULL || saved->id != (int64_t)id ||
      !((saved->item == NULL && item == NULL) ||
        (saved->item != NULL && item != NULL && strcmp(saved->item, item) == 0)))
    qa_range_error("Saved QVM weapon request differs from its original source owner");
  return make_capability(state->items, state->entry, saved);
}
static bool binding_current(void *ctx) {
  binding_state *state = ctx;
  return entry_current(state->items, state->entry);
}
static qa_source_weapon_presentation binding_read(void *ctx) {
  binding_state *state = ctx;
  if (!entry_current(state->items, state->entry))
    qa_range_error("QVM weapon presentation lost its original actor");
  qa_actor_id actor = state->entry->actor.id;
  qa_item_id active = qa_qvm_mod_weapon_stage_active(state->items->weapons, actor);
  qa_item_id pending = NULL;
  request *current_request = state->entry->current_request;
  if (current_request != NULL && strcmp(current_request->status, "refused") != 0 &&
      !((current_request->item == NULL && active == NULL) ||
        (current_request->item != NULL && active != NULL && strcmp(current_request->item, active) == 0)))
    pending = current_request->item;
  qa_source_item_definition *definitions = xmalloc(state->items->admission_count * sizeof *definitions);
  for (size_t i = 0; i < state->items->admission_count; i++) definitions[i] = state->items->admissions[i].definition;
  qa_source_weapon_presentation result;
  result.source.provider = state->items->provider;
  result.source.content = state->items->content;
  result.active = active;
  result.pending = pending;
  result.model = NULL;
  result.items = definitions;
  result.item_count = state->items->admission_count;
  return result;
}

static qa_source_weapon_binding make_binding(qa_qvm_mod_items_runtime *items, item_entry *entry) {
  if (items->weapons == NULL) qa_range_error("QVM weapon stage is absent");
  binding_state *state = xmalloc(sizeof *state);
  state->items = items;
  state->entry = entry;
  qa_source_weapon_binding binding;
  memset(&binding, 0, sizeof binding);
  binding.ctx = state;
  binding.current = binding_current;
  binding.read = binding_read;
  binding.handoff.ctx = state;
  binding.handoff.provider = items->provider;
  binding.handoff.accepts = handoff_accepts;
  binding.handoff.select = handoff_select;
  binding.handoff.holster = handoff_holster;
  binding.handoff.is_holstered = handoff_is_holstered;
  binding.handoff.is_source_input = true;
  binding.handoff.resume_request = handoff_resume;
  binding.handoff.restore_request = handoff_restore;
  return binding;
}

typedef struct weapon_operations_state {
  qa_qvm_mod_items_runtime *items;
} weapon_operations_state;

static int64_t weapon_pointer(void *ctx, qa_actor_id actor, const char *record) {
  qa_qvm_mod_items_runtime *items = ((weapon_operations_state *)ctx)->items;
  return items->operations->pointer(items->operations->ctx, actor, record);
}
static bool weapon_live(void *ctx, qa_actor_id actor) {
  qa_qvm_mod_items_runtime *items = ((weapon_operations_state *)ctx)->items;
  item_entry *entry = find_entry(items, actor);
  return entry != NULL && entry_current(items, entry);
}
static bool weapon_selected(void *ctx, qa_actor_id actor) {
  qa_qvm_mod_items_runtime *items = ((weapon_operations_state *)ctx)->items;
  return items->services->weapon_selected(items->services->ctx, actor, items->provider);
}
static int32_t weapon_invoke(void *ctx, qa_actor_id actor, const qa_qvm_mod_source_call *call) {
  qa_qvm_mod_items_runtime *items = ((weapon_operations_state *)ctx)->items;
  return items->operations->invoke(items->operations->ctx, actor, call);
}
static void weapon_attempted(void *ctx, qa_actor_id actor, int32_t value) {
  qa_qvm_mod_items_runtime *items = ((weapon_operations_state *)ctx)->items;
  item_entry *entry = find_entry(items, actor);
  if (entry == NULL || entry->current_request == NULL) return;
  bool found = false;
  int64_t wanted = selection_value(items, entry->current_request->item, &found);
  if (strcmp(entry->current_request->status, "pending") == 0 && found && wanted == value)
    entry->current_request->attempted = true;
}
static void weapon_accepted(void *ctx, qa_actor_id actor, int32_t value) {
  qa_qvm_mod_items_runtime *items = ((weapon_operations_state *)ctx)->items;
  item_entry *entry = find_entry(items, actor);
  if (entry == NULL || entry->current_request == NULL) return;
  bool found = false;
  int64_t wanted = selection_value(items, entry->current_request->item, &found);
  if (strcmp(entry->current_request->status, "pending") == 0 && found && wanted == value)
    entry->current_request->status = "accepted";
}
static void weapon_completed(void *ctx, qa_actor_id actor) {
  qa_qvm_mod_items_runtime *items = ((weapon_operations_state *)ctx)->items;
  item_entry *entry = find_entry(items, actor);
  if (entry == NULL || entry->current_request == NULL) return;
  request *value = entry->current_request;
  if (strcmp(value->status, "pending") != 0) return;
  qa_item_id active = qa_qvm_mod_weapon_stage_active(items->weapons, actor);
  bool matches = (value->item == NULL && active == NULL) ||
                 (value->item != NULL && active != NULL && strcmp(value->item, active) == 0);
  if (matches) value->status = "accepted";
  else if (value->attempted) value->status = "refused";
}
static qa_qvm_posture weapon_posture(void *ctx, qa_actor_id actor) {
  qa_qvm_mod_items_runtime *items = ((weapon_operations_state *)ctx)->items;
  qa_body_snapshot body;
  qa_mod_client_player_view view;
  if (!items->services->body_read(items->services->ctx, actor, &body) ||
      !items->services->player_view(items->services->ctx, actor, &view))
    qa_range_error("QVM weapon input requires the actual authoritative posture");
  qa_qvm_posture result = {body.body.bounds, (int32_t)view.view_offset.z,
                           items->operations->ground(items->operations->ctx, actor)};
  return result;
}

void qa_qvm_mod_items_runtime_init(qa_qvm_mod_items_runtime *items, const qa_qvm_mod_items *definition,
                                   qa_qvm_module *module, const qa_qvmy_artifact *image,
                                   const qa_qvmy_item_services *services, qa_provider_id provider,
                                   qa_content_id content, const qa_qvm_mod_item_operations *operations) {
  memset(items, 0, sizeof *items);
  items->definition = definition;
  items->memory = module->memory;
  items->image = image;
  items->services = services;
  items->provider = provider;
  items->content = content;
  items->operations = operations;
  items->admissions = xmalloc(definition->definition_count * sizeof *items->admissions);
  items->admission_actions = xmalloc(definition->definition_count * 2 * sizeof *items->admission_actions);
  items->admission_icons = xmalloc(definition->definition_count * sizeof *items->admission_icons);
  items->admission_count = definition->definition_count;
  for (size_t i = 0; i < definition->definition_count; i++) {
    const qa_qvm_mod_item_definition *source = &definition->definitions[i];
    qa_source_item_definition *target = &items->admissions[i].definition;
    memset(target, 0, sizeof *target);
    target->item = source->item;
    target->label = source->label;
    target->source.provider = provider;
    target->source.content = content;
    target->has_icon = source->has_icon;
    if (source->has_icon && source->icon != NULL) {
      items->admission_icons[i] = qa_resolve_item_icon(*source->icon, content);
      target->icon = &items->admission_icons[i];
    }
    if (source->has_actions) {
      qa_source_item_action_calls calls = {source->action_use, source->action_drop};
      target->actions = &items->admission_actions[i * 2];
      target->action_count = qa_source_item_action_names(calls, &items->admission_actions[i * 2], 2);
    }
    target->is_weapon = source->is_weapon;
    target->weapon = source->weapon;
    items->admissions[i].admission = source->admission;
  }
  if (definition->weapons != NULL) {
    items->weapons = xmalloc(sizeof *items->weapons);
    weapon_operations_state *state = xmalloc(sizeof *state);
    state->items = items;
    qa_qvm_mod_weapon_stage_operations stage_ops;
    memset(&stage_ops, 0, sizeof stage_ops);
    stage_ops.module = module;
    stage_ops.ctx = state;
    stage_ops.pointer = weapon_pointer;
    stage_ops.live = weapon_live;
    stage_ops.selected = weapon_selected;
    stage_ops.invoke = weapon_invoke;
    stage_ops.attempted = weapon_attempted;
    stage_ops.accepted = weapon_accepted;
    stage_ops.completed = weapon_completed;
    stage_ops.posture = weapon_posture;
    qa_qvm_mod_weapon_stage_init(items->weapons, &definition->weapons->stage,
                                 (int64_t)definition->weapons->input.entry, &stage_ops);
  }
}

void qa_qvm_mod_items_runtime_destroy(qa_qvm_mod_items_runtime *items) {
  if (items->weapons != NULL) {
    free(items->weapons->operations.ctx);
    qa_qvm_mod_weapon_stage_destroy(items->weapons);
    free(items->weapons);
  }
  item_entry **entries = entry_stack(items);
  for (size_t i = 0; i < items->entry_count; i++) {
    free(entries[i]->current_request);
    free(entries[i]->addresses);
    observe_state *observe = entries[i]->observe_state;
    if (observe != NULL) free(observe->pending);
    free(observe);
    free(entries[i]->lease_state);
    free(entries[i]);
  }
  free(items->entries);
  {
    item_entry **retired = items->retired_entries;
    for (size_t i = 0; i < items->retired_entry_count; i++) {
      free(retired[i]->current_request);
      free(retired[i]);
    }
    free(items->retired_entries);
  }
  free(items->applied);
  free(items->admissions);
  free(items->admission_actions);
  free(items->admission_icons);
  memset(items, 0, sizeof *items);
}

void qa_qvm_mod_items_admit(qa_qvm_mod_items_runtime *items, qa_actor_id actor) {
  if (find_entry(items, actor) != NULL) return;
  qa_owned_actor owner;
  if (!items->services->resolve_owned(items->services->ctx, actor, &owner) ||
      !items->operations->live(items->operations->ctx, actor))
    qa_range_error("QVM source items require the current admitted client");
  item_entry *entry = xmalloc(sizeof *entry);
  memset(entry, 0, sizeof *entry);
  entry->actor = owner;
  for (size_t s = 0; s < items->definition->storage_count; s++) {
    const qa_qvm_item_storage *storage = &items->definition->storage[s];
    const qa_qvm_item_field *fields_to_add[2] = {&storage->field, NULL};
    size_t count = 1;
    if (strcmp(storage->kind, "counter") == 0 && strcmp(storage->capacity.kind, "field") == 0) {
      fields_to_add[1] = &storage->capacity.field;
      count = 2;
    }
    for (size_t f = 0; f < count; f++) {
      bool seen = false;
      for (size_t a = 0; a < entry->address_count; a++)
        if (strcmp(entry->addresses[a].record, fields_to_add[f]->record) == 0) seen = true;
      if (!seen) {
        if (entry->address_count == entry->address_capacity) {
          entry->address_capacity = entry->address_capacity == 0 ? 8 : entry->address_capacity * 2;
          entry->addresses = realloc(entry->addresses, entry->address_capacity * sizeof *entry->addresses);
          if (entry->addresses == NULL) qa_range_error("QVM memory allocation failed");
        }
        entry->addresses[entry->address_count].record = fields_to_add[f]->record;
        entry->addresses[entry->address_count].address =
            items->operations->pointer(items->operations->ctx, actor, fields_to_add[f]->record);
        entry->address_count++;
      }
    }
  }
  lease_state *lease = xmalloc(sizeof *lease);
  lease->items = items;
  lease->actor = actor;
  entry->lease_state = lease;
  qa_qvmy_item_lease_binding binding;
  memset(&binding, 0, sizeof binding);
  binding.owner = items->provider;
  binding.items = items->admissions;
  binding.item_count = items->admission_count;
  binding.ctx = lease;
  binding.invoke = lease_invoke;
  binding.read = lease_read;
  binding.entry = lease_entry;
  binding.write = lease_write;
  binding.mutable_capacity = lease_mutable_capacity;
  entry->lease = items->services->bind_items(items->services->ctx, owner, &binding);
  size_t range_capacity = 0;
  for (size_t s = 0; s < items->definition->storage_count; s++) {
    const qa_qvm_item_storage *storage = &items->definition->storage[s];
    range_capacity += 2;
    if (strcmp(storage->kind, "counter") == 0 && strcmp(storage->capacity.kind, "source") == 0)
      range_capacity += storage->capacity.override_count;
  }
  qa_qvm_write_range *ranges = xmalloc(range_capacity * sizeof *ranges);
  size_t range_count = 0;
  for (size_t s = 0; s < items->definition->storage_count; s++) {
    const qa_qvm_item_storage *storage = &items->definition->storage[s];
    ranges[range_count++] = (qa_qvm_write_range){address_of(items, actor, &storage->field), 4};
    if (strcmp(storage->kind, "counter") == 0 && strcmp(storage->capacity.kind, "field") == 0)
      ranges[range_count++] = (qa_qvm_write_range){address_of(items, actor, &storage->capacity.field), 4};
    if (strcmp(storage->kind, "counter") == 0 && strcmp(storage->capacity.kind, "source") == 0) {
      for (size_t o = 0; o < storage->capacity.override_count; o++)
        ranges[range_count++] = (qa_qvm_write_range){(int64_t)storage->capacity.overrides[o].address, 4};
    }
  }
  observe_state *observe = xmalloc(sizeof *observe);
  entry->observe_state = observe;
  observe->items = items;
  observe->actor = actor;
  observe->pending = NULL;
  observe->pending_count = 0;
  observe->pending_capacity = 0;
  observe->has_pending = false;
  entry->watch_id = qa_qvm_memory_observe_writes(items->memory, ranges, range_count,
                                                  observe_publish, observe, observe_after, observe);
  free(ranges);
  entry->current_request = NULL;
  if (items->entry_count == items->entry_capacity) {
    items->entry_capacity = items->entry_capacity == 0 ? 8 : items->entry_capacity * 2;
    items->entries = realloc(items->entries, items->entry_capacity * sizeof(item_entry *));
    if (items->entries == NULL) qa_range_error("QVM memory allocation failed");
  }
  entry_stack(items)[items->entry_count++] = entry;
  if (items->weapons != NULL) {
    qa_source_weapon_unbind remove;
    if (!items->services->has_weapons(items->services->ctx) ||
        !items->services->weapon_bind(items->services->ctx, owner, make_binding(items, entry), &remove)) {
      qa_qvm_mod_items_release(items, actor);
      qa_range_error("QVM source weapon service is unavailable");
    }
    entry->has_remove_weapon = true;
    entry->remove_weapon = remove;
  }
}

bool qa_qvm_mod_items_requested(qa_qvm_mod_items_runtime *items, qa_actor_id actor, int64_t *out) {
  item_entry *entry = find_entry(items, actor);
  if (entry == NULL || !entry_current(items, entry) || entry->current_request == NULL ||
      strcmp(entry->current_request->status, "refused") == 0)
    return false;
  bool found = false;
  int64_t value = selection_value(items, entry->current_request->item, &found);
  if (!found && entry->current_request->item != NULL) return false;
  *out = entry->current_request->item == NULL ? 0 : value;
  return entry->current_request->item != NULL;
}

void qa_qvm_mod_items_open(qa_qvm_mod_items_runtime *items, qa_mod_client_application *application,
                           void (**remove)(void *ctx), void **remove_ctx) {
  if (items->weapons == NULL) {
    *remove = NULL;
    *remove_ctx = NULL;
    return;
  }
  qa_qvm_mod_weapon_stage_open(items->weapons, application, remove, remove_ctx);
}

bool qa_qvm_mod_items_applies(qa_qvm_mod_items_runtime *items, const qa_qvm_mod_source_call *call) {
  return items->definition->weapons != NULL && items->definition->weapons->input.entry == call->entry;
}

int32_t qa_qvm_mod_items_apply(qa_qvm_mod_items_runtime *items, qa_mod_client_application *application,
                               int32_t (*run)(void *ctx), void *run_ctx) {
  if (items->definition->weapons == NULL) return run(run_ctx);
  bool seen = false;
  for (size_t i = 0; i < items->applied_count; i++) if (items->applied[i] == application) seen = true;
  if (strcmp(application->scope, "movement-slice") != 0 || seen)
    qa_range_error("QVM weapon input must consume each actual movement slice exactly once");
  item_entry *entry = find_entry(items, application->identity.actor);
  if (entry == NULL || !entry_current(items, entry))
    qa_range_error("QVM weapon application lost its admitted actor");
  if (items->applied_count == items->applied_capacity) {
    items->applied_capacity = items->applied_capacity == 0 ? 8 : items->applied_capacity * 2;
    items->applied = realloc(items->applied, items->applied_capacity * sizeof *items->applied);
    if (items->applied == NULL) qa_range_error("QVM memory allocation failed");
  }
  items->applied[items->applied_count++] = application;
  double end = application->frame.time.kind == QA_SOURCE_TIME_SECONDS
      ? application->frame.time.value * 1000 : application->frame.time.value;
  double elapsed = application->frame.elapsed.kind == QA_SOURCE_TIME_SECONDS
      ? application->frame.elapsed.value * 1000 : application->frame.elapsed.value;
  if (!isfinite(end) || !isfinite(elapsed) || elapsed < 0) qa_range_error("Invalid applied source weapon clock");
  qa_qvm_writable_view view;
  qa_qvm_memory_data_view(items->memory,
      address_of(items, entry->actor.id, &items->definition->weapons->input.clock), 4, &view);
  qa_qvm_writable_view_set_int32(&view, 0, qa_int32(trunc(end) - trunc(elapsed)), true);
  return qa_qvm_mod_weapon_stage_apply(items->weapons, entry->actor.id, run, run_ctx);
}

static qa_save_value *make_object(void) {
  qa_save_value *value = qa_save_value_object();
  if (value == NULL) qa_range_error("QVM memory allocation failed");
  return value;
}

static qa_save_value *make_array(void) {
  qa_save_value *value = qa_save_value_array();
  if (value == NULL) qa_range_error("QVM memory allocation failed");
  return value;
}

static void set_child(qa_save_value *object, const char *key, qa_save_value *child) {
  if (child == NULL || !qa_save_value_set(object, key, child)) qa_range_error("QVM memory allocation failed");
}

qa_save_value *qa_qvm_mod_items_checkpoint(qa_qvm_mod_items_runtime *items) {
  qa_save_value *result = make_object();
  set_child(result, "version", qa_save_value_number(1));
  set_child(result, "nextRequest", qa_save_value_number((double)items->next_request));
  qa_save_value *requests = make_array();
  item_entry **entries = entry_stack(items);
  for (size_t i = 0; i < items->entry_count; i++) {
    if (entries[i]->current_request == NULL) continue;
    qa_save_value *row = make_object();
    qa_saved_actor_id saved = qa_saved_actor_id_of(entries[i]->actor.id);
    qa_save_value *actor = make_object();
    set_child(actor, "slot", qa_save_value_number(saved.slot));
    set_child(actor, "generation", qa_save_value_number(saved.generation));
    set_child(row, "actor", actor);
    set_child(row, "id", qa_save_value_number((double)entries[i]->current_request->id));
    if (entries[i]->current_request->item == NULL) set_child(row, "item", qa_save_value_null());
    else set_child(row, "item", qa_save_value_string(entries[i]->current_request->item));
    set_child(row, "status", qa_save_value_string(entries[i]->current_request->status));
    if (!qa_save_value_push(requests, row)) qa_range_error("QVM memory allocation failed");
  }
  set_child(result, "requests", requests);
  return result;
}

static qa_save_reader rfield(const qa_save_reader *reader, const char *name) {
  qa_save_reader out;
  if (!qa_save_reader_field(reader, name, &out)) qa_range_error("QVM memory allocation failed");
  return out;
}

static qa_save_reader ritem(const qa_save_reader *reader, size_t index) {
  qa_save_reader out;
  if (!qa_save_reader_item(reader, index, &out)) qa_range_error("QVM memory allocation failed");
  return out;
}

void qa_qvm_mod_items_restore(qa_qvm_mod_items_runtime *items, const qa_save_value *value) {
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, value, "qvm.items")) qa_range_error("QVM memory allocation failed");
  qa_save_reader version = rfield(&reader, "version");
  (void)qa_save_reader_literal_number(&version, 1);
  qa_save_reader_destroy(&version);
  qa_save_reader next = rfield(&reader, "nextRequest");
  items->next_request = (int64_t)qa_save_reader_integer_min(&next, 0);
  qa_save_reader_destroy(&next);
  qa_save_reader requests = rfield(&reader, "requests");
  size_t count = qa_save_reader_list_count(&requests);
  int64_t *ids = xmalloc(count * sizeof *ids);
  size_t id_count = 0;
  qa_actor_id *actors = xmalloc(count * sizeof *actors);
  size_t actor_count = 0;
  for (size_t i = 0; i < count; i++) {
    qa_save_reader row = ritem(&requests, i);
    qa_save_reader actor_reader = rfield(&row, "actor");
    qa_saved_actor_id saved = qa_read_saved_actor(&actor_reader);
    qa_save_reader_destroy(&actor_reader);
    qa_actor_id actor = items->services->reference_saved(items->services->ctx, saved);
    qa_save_reader id_reader = rfield(&row, "id");
    int64_t id = (int64_t)qa_save_reader_integer_min(&id_reader, 1);
    qa_save_reader_destroy(&id_reader);
    qa_save_reader item_reader = rfield(&row, "item");
    qa_item_id item = NULL;
    if (!qa_save_reader_is_null(&item_reader)) item = qa_namespaced(&item_reader);
    qa_save_reader_destroy(&item_reader);
    qa_save_reader status_reader = rfield(&row, "status");
    static const char *const statuses[] = {"pending", "accepted", "refused"};
    const char *status = qa_save_reader_choice_string(&status_reader, statuses, 3);
    qa_save_reader_destroy(&status_reader);
    qa_save_reader_destroy(&row);
    item_entry *entry = find_entry(items, actor);
    bool duplicate = false;
    for (size_t j = 0; j < id_count; j++) if (ids[j] == id) duplicate = true;
    for (size_t j = 0; j < actor_count; j++) if (qa_same_actor(actors[j], actor)) duplicate = true;
    bool found = false;
    if (item != NULL) (void)selection_value(items, item, &found);
    if (entry == NULL || !entry_current(items, entry) || id > items->next_request || duplicate ||
        (item != NULL && !found))
      qa_range_error("Saved QVM weapon request is not its current admitted owner");
    ids[id_count++] = id;
    actors[actor_count++] = actor;
    free(entry->current_request);
    entry->current_request = xmalloc(sizeof(request));
    entry->current_request->id = id;
    entry->current_request->item = item;
    entry->current_request->status = status;
    entry->current_request->attempted = false;
  }
  free(ids);
  free(actors);
  qa_save_reader_destroy(&requests);
  qa_save_reader_destroy(&reader);
}

void qa_qvm_mod_items_clear(qa_qvm_mod_items_runtime *items) {
  while (items->entry_count != 0) {
    item_entry **entries = entry_stack(items);
    qa_actor_id actor = entries[0]->actor.id;
    qa_qvm_mod_items_release(items, actor);
  }
}

void qa_qvm_mod_items_release(qa_qvm_mod_items_runtime *items, qa_actor_id actor) {
  item_entry **entries = entry_stack(items);
  for (size_t i = 0; i < items->entry_count; i++) {
    if (!qa_same_actor(entries[i]->actor.id, actor)) continue;
    item_entry *removed = entries[i];
    memmove(&entries[i], &entries[i + 1], (items->entry_count - i - 1) * sizeof *entries);
    items->entry_count--;
    qa_qvm_memory_writes_unobserve(&items->memory->writes, removed->watch_id);
    if (removed->has_remove_weapon) removed->remove_weapon.invoke(removed->remove_weapon.ctx);
    removed->lease.close(removed->lease.ctx);
    free(removed->addresses);
    removed->addresses = NULL;
    removed->address_count = 0;
    {
      observe_state *observe = removed->observe_state;
      if (observe != NULL) free(observe->pending);
      free(observe);
      removed->observe_state = NULL;
    }
    free(removed->lease_state);
    removed->lease_state = NULL;
    /* Keep the entry and its request allocated until destroy: weapon
     * capabilities captured them and compare identity after release
     * (the TS objects stay alive through the capability closures). */
    if (items->retired_entry_count == items->retired_entry_capacity) {
      items->retired_entry_capacity =
          items->retired_entry_capacity == 0 ? 8 : items->retired_entry_capacity * 2;
      items->retired_entries =
          realloc(items->retired_entries, items->retired_entry_capacity * sizeof(item_entry *));
      if (items->retired_entries == NULL) qa_range_error("QVM memory allocation failed");
    }
    ((item_entry **)items->retired_entries)[items->retired_entry_count++] = removed;
    return;
  }
}

void qa_qvm_mod_items_close(qa_qvm_mod_items_runtime *items) {
  if (items->weapons != NULL) qa_qvm_mod_weapon_stage_close(items->weapons);
  qa_qvm_mod_items_clear(items);
}
