/* Port of src/compat/qvm/item-storage.ts. GPL-2.0-or-later. */
#include "qa/compat/qvm/item_storage.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/numeric.h"
#include "qa/foundation/support.h"

static bool integer(double value) {
  return isfinite(value) && value == trunc(value) && value >= -2147483648.0 && value <= 2147483647.0;
}
static bool is_integer_value(double value) { return isfinite(value) && value == trunc(value); }

static double constant(const qa_qvm_image *image, double instruction) {
  const qa_qvm_instruction *value = NULL;
  if (is_integer_value(instruction) && instruction >= 0 &&
      instruction < (double)image->instruction_count)
    value = &image->instructions[(size_t)instruction];
  if (value == NULL || value->opcode != QA_QVM_OP_CONST || value->operand < 0)
    qa_range_error("QVM item capacity is not its declared original constant");
  return value->operand;
}

static bool item_in(const qa_item_id *items, size_t count, qa_item_id item) {
  for (size_t i = 0; i < count; i++)
    if (strcmp(items[i], item) == 0) return true;
  return false;
}

void qa_qvm_validate_item_storage(const qa_qvm_item_storage *storage_values, size_t storage_count,
                                  const qa_item_id *items, size_t item_count,
                                  const qa_qvm_image *image, qa_qvm_item_field_validator field) {
  qa_item_id *bound = calloc(item_count == 0 ? 1 : item_count, sizeof *bound);
  if (bound == NULL) qa_range_error("out of memory");
  size_t bound_count = 0;
  for (size_t s = 0; s < storage_count; s++) {
    const qa_qvm_item_storage *storage = &storage_values[s];
    field.call(field.ctx, storage->field, NULL);
    if (strcmp(storage->kind, "counter") == 0) {
      if (!item_in(items, item_count, storage->item) || item_in(bound, bound_count, storage->item)) {
        free(bound);
        qa_range_error("QVM item lacks distinct declared storage");
      }
      bound[bound_count++] = storage->item;
      const qa_qvm_item_capacity *capacity = &storage->capacity;
      if (strcmp(capacity->kind, "field") == 0) field.call(field.ctx, capacity->field, "capacity");
      if (strcmp(capacity->kind, "constant") == 0 &&
          (!integer(capacity->value) || capacity->value < 0)) {
        free(bound);
        qa_range_error("QVM item capacity exceeds its source ABI");
      }
      if (strcmp(capacity->kind, "source") == 0) {
        (void)constant(image, capacity->instruction);
        for (size_t i = 0; i < capacity->override_count; i++) {
          const qa_qvm_item_capacity_override *value = &capacity->overrides[i];
          (void)constant(image, value->instruction);
          if (!integer(value->value) || !is_integer_value(value->address) || value->address < 0 ||
              (int64_t)value->address % 4 != 0 ||
              value->address + 4 > (double)image->initialized_length + (double)image->bss_length) {
            free(bound);
            qa_range_error("QVM capacity selector exceeds original source storage");
          }
        }
      }
    } else {
      if (!is_integer_value(storage->private_mask) || storage->private_mask < 0 ||
          storage->private_mask > 4294967295.0 || storage->item_count == 0) {
        free(bound);
        qa_range_error("Invalid QVM private inventory mask");
      }
      uint32_t mask = qa_uint32(storage->private_mask);
      for (size_t i = 0; i < storage->item_count; i++) {
        const qa_qvm_item_mask *value = &storage->items[i];
        if (!item_in(items, item_count, value->item) || item_in(bound, bound_count, value->item)) {
          free(bound);
          qa_range_error("QVM item lacks distinct declared storage");
        }
        bound[bound_count++] = value->item;
        uint32_t bit = qa_uint32(value->mask);
        if (!is_integer_value(value->mask) || value->mask < 1 || value->mask > 2147483648.0 ||
            (bit & (bit - 1)) != 0 || (mask & bit) != 0) {
          free(bound);
          qa_range_error("QVM packed item masks overlap");
        }
        mask |= bit;
      }
    }
  }
  free(bound);
  if (bound_count != item_count) qa_range_error("QVM item definition has no source storage");
}

double qa_qvm_get_item_capacity(const qa_qvm_image *image, qa_qvm_item_capacity capacity,
                                qa_qvm_item_storage_access access) {
  if (strcmp(capacity.kind, "constant") == 0) return capacity.value;
  if (strcmp(capacity.kind, "field") == 0) return access.read(access.ctx, capacity.field);
  for (size_t i = 0; i < capacity.override_count; i++) {
    const qa_qvm_item_capacity_override *value = &capacity.overrides[i];
    bool matches = access.global(access.ctx, value->address) == value->value;
    if (strcmp(value->comparison, "equals") == 0 ? matches : !matches)
      return constant(image, value->instruction);
  }
  return constant(image, capacity.instruction);
}

bool qa_qvm_read_item_storage(const qa_qvm_image *image, qa_qvm_item_storage storage,
                              qa_qvm_item_storage_access access, qa_qvm_inventory_entries *out) {
  double count = access.read(access.ctx, storage.field);
  out->entries = NULL;
  out->count = 0;
  if (strcmp(storage.kind, "counter") == 0) {
    out->entries = malloc(sizeof *out->entries);
    if (out->entries == NULL) return false;
    out->entries[0].item = storage.item;
    out->entries[0].count = count;
    out->entries[0].capacity = qa_qvm_get_item_capacity(image, storage.capacity, access);
    out->entries[0].has_count_policy = true;
    out->entries[0].count_policy.kind = "source-counter";
    out->entries[0].count_policy.arithmetic = "int32";
    out->count = 1;
    return true;
  }
  uint32_t mask = qa_uint32(storage.private_mask);
  for (size_t i = 0; i < storage.item_count; i++) mask |= qa_uint32(storage.items[i].mask);
  if ((qa_uint32(count) & ~mask) != 0) qa_range_error("Original QVM inventory contains undeclared bits");
  if (storage.item_count > 0) {
    out->entries = calloc(storage.item_count, sizeof *out->entries);
    if (out->entries == NULL) return false;
  }
  for (size_t i = 0; i < storage.item_count; i++) {
    out->entries[i].item = storage.items[i].item;
    out->entries[i].count = (qa_uint32(count) & qa_uint32(storage.items[i].mask)) == 0 ? 0 : 1;
    out->entries[i].capacity = 1;
    out->entries[i].has_count_policy = false;
    out->entries[i].count_policy.kind = NULL;
    out->entries[i].count_policy.arithmetic = NULL;
  }
  out->count = storage.item_count;
  return true;
}

void qa_qvm_inventory_entries_destroy(qa_qvm_inventory_entries *entries) {
  if (entries == NULL) return;
  free(entries->entries);
  entries->entries = NULL;
  entries->count = 0;
}

void qa_qvm_write_item_storage(const qa_qvm_image *image, qa_qvm_item_storage storage,
                               qa_inventory_entry entry, qa_qvm_item_storage_access access) {
  if (strcmp(storage.kind, "counter") == 0) {
    if (!integer(entry.count) || !integer(entry.capacity) || entry.capacity < 0)
      qa_range_error("QVM item exceeds its signed source representation");
    if (strcmp(storage.capacity.kind, "field") != 0 &&
        entry.capacity != qa_qvm_get_item_capacity(image, storage.capacity, access))
      qa_range_error("QVM capacity is owned by its original source");
    access.write(access.ctx, storage.field, entry.count);
    if (strcmp(storage.capacity.kind, "field") == 0)
      access.write(access.ctx, storage.capacity.field, entry.capacity);
  } else {
    const qa_qvm_item_mask *bit = NULL;
    for (size_t i = 0; i < storage.item_count; i++)
      if (strcmp(storage.items[i].item, entry.item) == 0) { bit = &storage.items[i]; break; }
    if (bit == NULL || entry.capacity != 1 || (entry.count != 0 && entry.count != 1))
      qa_range_error("QVM packed ownership requires one admitted bit");
    double previous = access.read(access.ctx, storage.field);
    uint32_t mask = qa_uint32(bit->mask);
    int32_t updated = entry.count == 0 ? (int32_t)(qa_uint32(previous) & ~mask)
                                       : (int32_t)(qa_uint32(previous) | mask);
    access.write(access.ctx, storage.field, updated);
  }
}
