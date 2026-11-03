/* Port of src/compat/qvm/item-catalog.ts. GPL-2.0-or-later. */
#include "qa/compat/qvm/item_catalog.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

static bool is_safe_integer(double v) {
  return isfinite(v) && v == trunc(v) && v >= -9007199254740991.0 && v <= 9007199254740991.0;
}

static void reader_field(const qa_save_reader *reader, const char *name, qa_save_reader *out) {
  if (!qa_save_reader_field(reader, name, out)) qa_range_error("out of memory");
}

static double layout_offset(const qa_save_reader *fields, const char *name, double stride) {
  qa_save_reader field;
  reader_field(fields, name, &field);
  double value = qa_save_reader_integer_min(&field, 0);
  if ((int64_t)value % 4 != 0 || value + 4 > stride)
    qa_save_reader_fail(&field, "item field exceeds its record or is unaligned");
  qa_save_reader_destroy(&field);
  return value;
}

qa_qvm_item_layout qa_qvm_parse_item_layout(const qa_save_reader *reader) {
  qa_save_reader fields, stride_reader, pointer, count, source;
  reader_field(reader, "fields", &fields);
  reader_field(reader, "stride", &stride_reader);
  double stride = qa_save_reader_integer_min(&stride_reader, 4);
  qa_save_reader_destroy(&stride_reader);
  reader_field(reader, "address", &pointer);
  reader_field(reader, "count", &count);
  qa_qvm_item_layout layout;
  memset(&layout, 0, sizeof layout);
  layout.stride = stride;
  if (pointer.value != NULL && pointer.value->kind == QA_SAVE_NUMBER) {
    layout.address_is_number = true;
    layout.address = qa_save_reader_integer_min(&pointer, 4);
  } else {
    qa_save_reader global;
    reader_field(&pointer, "global", &global);
    layout.address_global = qa_save_reader_integer_min(&global, 0);
    qa_save_reader_destroy(&global);
  }
  if (count.value != NULL && count.value->kind == QA_SAVE_NUMBER) {
    layout.count_is_number = true;
    layout.count = qa_save_reader_integer_min(&count, 1);
  } else {
    qa_save_reader global, maximum;
    reader_field(&count, "global", &global);
    layout.count_global = qa_save_reader_integer_min(&global, 0);
    qa_save_reader_destroy(&global);
    reader_field(&count, "maximum", &maximum);
    layout.count_maximum = qa_save_reader_integer_min(&maximum, 1);
    qa_save_reader_destroy(&maximum);
  }
  qa_save_reader_destroy(&pointer);
  qa_save_reader_destroy(&count);
  reader_field(reader, "source", &source);
  if (!qa_save_reader_is_undefined(&source)) {
    layout.has_source = true;
    layout.source = qa_save_reader_literal_string(&source, "live");
  }
  qa_save_reader_destroy(&source);
  layout.field_class_name = layout_offset(&fields, "className", stride);
  layout.field_pickup_name = layout_offset(&fields, "pickupName", stride);
  layout.field_type = layout_offset(&fields, "type", stride);
  layout.field_tag = layout_offset(&fields, "tag", stride);
  qa_save_reader_destroy(&fields);
  qa_save_reader weapon, ammo;
  reader_field(reader, "weaponType", &weapon);
  layout.weapon_type = qa_save_reader_integer_min(&weapon, 0);
  qa_save_reader_destroy(&weapon);
  reader_field(reader, "ammoType", &ammo);
  layout.ammo_type = qa_save_reader_integer_min(&ammo, 0);
  qa_save_reader_destroy(&ammo);
  double address = layout.address_is_number ? layout.address : layout.address_global;
  if ((int64_t)address % 4 != 0 || (!layout.count_is_number && (int64_t)layout.count_global % 4 != 0) ||
      (int64_t)stride % 4 != 0 || layout.weapon_type == layout.ammo_type)
    qa_save_reader_fail(reader, "invalid item table layout");
  bool live = layout.has_source && strcmp(layout.source, "live") == 0;
  if (!live && (!layout.address_is_number || !layout.count_is_number))
    qa_save_reader_fail(reader, "runtime item table locations require live source storage");
  return layout;
}

static int32_t data_i32(const uint8_t *data, size_t length, int64_t offset) {
  qa_qvmx_view view = {(uint8_t *)data, length};
  return qa_qvmx_view_i32(&view, offset);
}

bool qa_qvm_read_item_records(const uint8_t *data, size_t length, qa_qvm_item_layout layout,
                              const double *types, size_t type_count,
                              const qa_qvm_item_range_fn *range, qa_qvm_catalog_records *out) {
  /* global(address) helper inlined at both call sites. */
  double table_value;
  if (layout.address_is_number) {
    table_value = layout.address;
  } else {
    double address = layout.address_global;
    if (!is_safe_integer(address) || address < 0 || (int64_t)address % 4 != 0 ||
        address + 4 > (double)length)
      qa_range_error("QVM item table locator exceeds module data");
    if (range != NULL) range->call(range->ctx, address, 4);
    table_value = data_i32(data, length, (int64_t)address);
  }
  double count_value;
  if (layout.count_is_number) {
    count_value = layout.count;
  } else {
    double address = layout.count_global;
    if (!is_safe_integer(address) || address < 0 || (int64_t)address % 4 != 0 ||
        address + 4 > (double)length)
      qa_range_error("QVM item table locator exceeds module data");
    if (range != NULL) range->call(range->ctx, address, 4);
    count_value = data_i32(data, length, (int64_t)address);
  }
  if (!is_safe_integer(count_value) || count_value < 0 ||
      (!layout.count_is_number && count_value > layout.count_maximum) ||
      !is_safe_integer(table_value) || table_value < 0 || (int64_t)table_value % 4 != 0 ||
      (count_value > 0 && table_value == 0) ||
      !is_safe_integer(table_value + count_value * layout.stride) ||
      table_value + count_value * layout.stride > (double)length)
    qa_range_error("QVM item table exceeds initialized or live module data");
  if (count_value > 0 && range != NULL) range->call(range->ctx, table_value, count_value * layout.stride);
  out->entries = NULL;
  out->count = 0;
  size_t capacity = 0;
  for (int64_t index = 0; index < (int64_t)count_value; index++) {
    int64_t address = (int64_t)table_value + index * (int64_t)layout.stride;
    int32_t class_pointer = data_i32(data, length, address + (int64_t)layout.field_class_name);
    if (class_pointer == 0) continue;
    int32_t type = data_i32(data, length, address + (int64_t)layout.field_type);
    if (types != NULL) {
      bool found = false;
      for (size_t i = 0; i < type_count; i++) found = found || types[i] == (double)type;
      if (!found) continue;
    }
    int32_t pointers[2] = {class_pointer,
                           data_i32(data, length, address + (int64_t)layout.field_pickup_name)};
    char *strings[2] = {NULL, NULL};
    for (int s = 0; s < 2; s++) {
      int32_t pointer = pointers[s];
      if (pointer <= 0 || pointer >= (int64_t)length) {
        free(strings[0]);
        qa_qvm_catalog_records_destroy(out);
        qa_range_error("QVM item string is outside initialized module data");
      }
      size_t end = (size_t)pointer;
      while (end < length && data[end] != 0) end++;
      if (end >= length) {
        free(strings[0]);
        qa_qvm_catalog_records_destroy(out);
        qa_range_error("QVM item string is unterminated");
      }
      if (range != NULL) range->call(range->ctx, pointer, (double)(end - (size_t)pointer + 1));
      strings[s] = malloc(end - (size_t)pointer + 1);
      if (strings[s] == NULL) {
        free(strings[0]);
        qa_qvm_catalog_records_destroy(out);
        return false;
      }
      memcpy(strings[s], data + pointer, end - (size_t)pointer);
      strings[s][end - (size_t)pointer] = '\0';
    }
    if (strings[0][0] == '\0' || strings[1][0] == '\0') {
      free(strings[0]);
      free(strings[1]);
      qa_qvm_catalog_records_destroy(out);
      qa_range_error("QVM item has no source name");
    }
    if (out->count == capacity) {
      capacity = capacity == 0 ? 8 : capacity * 2;
      qa_qvm_catalog_record *grown = realloc(out->entries, capacity * sizeof *grown);
      if (grown == NULL) {
        free(strings[0]);
        free(strings[1]);
        qa_qvm_catalog_records_destroy(out);
        return false;
      }
      out->entries = grown;
    }
    qa_qvm_catalog_record *record = &out->entries[out->count++];
    record->class_name = strings[0];
    record->pickup_name = strings[1];
    record->type = type;
    record->tag = data_i32(data, length, address + (int64_t)layout.field_tag);
    record->index = (double)index;
    record->address = (double)address;
  }
  return true;
}

bool qa_qvm_read_item_catalog(const uint8_t *data, size_t length, qa_qvm_item_layout layout,
                              bool private_inventory, qa_qvm_catalog_items *out) {
  double types[2] = {layout.weapon_type, layout.ammo_type};
  qa_qvm_catalog_records records;
  if (!qa_qvm_read_item_records(data, length, layout, types, 2, NULL, &records)) return false;
  out->entries = NULL;
  out->count = 0;
  if (records.count > 0) {
    out->entries = calloc(records.count, sizeof *out->entries);
    if (out->entries == NULL) {
      qa_qvm_catalog_records_destroy(&records);
      return false;
    }
  }
  for (size_t i = 0; i < records.count; i++) {
    qa_qvm_catalog_record *item = &records.entries[i];
    if (item->tag < 1 || (!private_inventory && item->tag > 15)) {
      qa_qvm_catalog_records_destroy(&records);
      qa_qvm_catalog_items_destroy(out);
      qa_range_error("QVM item cannot be represented by its public weapon/ammo state");
    }
    out->entries[i].class_name = item->class_name;
    out->entries[i].pickup_name = item->pickup_name;
    out->entries[i].type = item->type;
    out->entries[i].tag = item->tag;
    item->class_name = NULL;
    item->pickup_name = NULL;
    out->count++;
  }
  qa_qvm_catalog_records_destroy(&records);
  return true;
}

void qa_qvm_catalog_records_destroy(qa_qvm_catalog_records *records) {
  if (records == NULL) return;
  for (size_t i = 0; i < records->count; i++) {
    free(records->entries[i].class_name);
    free(records->entries[i].pickup_name);
  }
  free(records->entries);
  records->entries = NULL;
  records->count = 0;
}

void qa_qvm_catalog_items_destroy(qa_qvm_catalog_items *items) {
  if (items == NULL) return;
  for (size_t i = 0; i < items->count; i++) {
    free(items->entries[i].class_name);
    free(items->entries[i].pickup_name);
  }
  free(items->entries);
  items->entries = NULL;
  items->count = 0;
}

void qa_qvm_source_item_catalog_init(qa_qvm_source_item_catalog *catalog, qa_qvmx_memory *memory,
                                     qa_qvm_item_layout layout, const uint8_t *initialized,
                                     size_t initialized_length) {
  memset(catalog, 0, sizeof *catalog);
  catalog->memory = memory;
  catalog->layout = layout;
  catalog->initialized = initialized;
  catalog->initialized_length = initialized_length;
}

void qa_qvm_source_item_catalog_init_observed(qa_qvm_source_item_catalog *catalog, qa_qvm_memory *memory,
                                              qa_qvm_item_layout layout, const uint8_t *initialized,
                                              size_t initialized_length) {
  memset(catalog, 0, sizeof *catalog);
  catalog->memory_view.bytes = memory->bytes;
  catalog->memory_view.length = memory->length;
  catalog->memory = &catalog->memory_view;
  catalog->writes = &memory->writes;
  catalog->layout = layout;
  catalog->initialized = initialized;
  catalog->initialized_length = initialized_length;
}

static qa_qvm_memory_writes *catalog_writes(const qa_qvm_source_item_catalog *catalog) {
  return catalog->writes != NULL ? catalog->writes : qa_qvmx_writes_for(catalog->memory->bytes);
}

/* TS remove(): drop the watch over the previous records. */
static void catalog_unwatch(qa_qvm_source_item_catalog *catalog) {
  if (!catalog->watching) return;
  qa_qvm_memory_writes *writes = catalog_writes(catalog);
  if (writes != NULL) qa_qvm_memory_writes_unobserve(writes, catalog->watch_id);
  catalog->watching = false;
}

void qa_qvm_source_item_catalog_destroy(qa_qvm_source_item_catalog *catalog) {
  if (catalog == NULL) return;
  catalog_unwatch(catalog);
  qa_qvm_catalog_records_destroy(&catalog->records);
  catalog->has_records = false;
}

void qa_qvm_source_item_catalog_reset(qa_qvm_source_item_catalog *catalog) {
  catalog_unwatch(catalog);
  qa_qvm_catalog_records_destroy(&catalog->records);
  catalog->has_records = false;
}

typedef struct catalog_ranges {
  qa_qvm_write_range *ranges;
  size_t count, capacity;
} catalog_ranges;

static void collect_range(void *ctx, double offset, double length) {
  catalog_ranges *ranges = ctx;
  if (ranges->count == ranges->capacity) {
    size_t capacity = ranges->capacity == 0 ? 8 : ranges->capacity * 2;
    qa_qvm_write_range *grown = realloc(ranges->ranges, capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("QVM memory allocation failed");
    ranges->ranges = grown;
    ranges->capacity = capacity;
  }
  ranges->ranges[ranges->count++] = (qa_qvm_write_range){(int64_t)offset, (int64_t)length};
}

static void invalidate_records(void *ctx, const qa_qvm_committed_write *event) {
  (void)event;
  qa_qvm_source_item_catalog *catalog = ctx;
  catalog->has_records = false;
}

typedef struct catalog_read {
  qa_qvm_source_item_catalog *catalog;
  bool live;
  catalog_ranges *ranges;
  bool ok;
} catalog_read;

static void catalog_read_body(void *ctx) {
  catalog_read *job = ctx;
  qa_qvm_source_item_catalog *catalog = job->catalog;
  qa_qvm_item_range_fn range = {job->ranges, collect_range};
  const uint8_t *data = job->live ? catalog->memory->bytes : catalog->initialized;
  size_t length = job->live ? catalog->memory->length : catalog->initialized_length;
  job->ok = qa_qvm_read_item_records(data, length, catalog->layout, NULL, 0, job->live ? &range : NULL,
                                     &catalog->records);
  if (!job->ok) return;
  catalog->has_records = true;
  qa_qvm_memory_writes *writes = catalog_writes(catalog);
  if (job->ranges->count != 0 && writes != NULL) {
    catalog->watch_id = qa_qvm_memory_writes_observe(writes, job->ranges->ranges, job->ranges->count,
                                                     invalidate_records, catalog, NULL, NULL);
    catalog->watching = true;
  }
}

bool qa_qvm_source_item_catalog_records(qa_qvm_source_item_catalog *catalog,
                                        const qa_qvm_catalog_records **out) {
  qa_qvm_memory_writes *writes = catalog_writes(catalog);
  if (writes != NULL) qa_qvm_memory_writes_assert_live(writes);
  if (catalog->closed) qa_range_error("QVM item catalog owner is retired");
  if (catalog->has_records) {
    *out = &catalog->records;
    return true;
  }
  catalog_unwatch(catalog);
  qa_qvm_catalog_records_destroy(&catalog->records);
  bool live = catalog->layout.has_source && strcmp(catalog->layout.source, "live") == 0;
  catalog_ranges ranges = {NULL, 0, 0};
  catalog_read job = {catalog, live, &ranges, false};
  char message[512];
  bool ok = qa_range_error_catch(catalog_read_body, &job, message, sizeof message);
  free(ranges.ranges);
  if (!ok) qa_range_error("%s", message);
  if (!job.ok) return false;
  *out = &catalog->records;
  return true;
}

void qa_qvm_source_item_catalog_close(qa_qvm_source_item_catalog *catalog) {
  qa_qvm_source_item_catalog_reset(catalog);
  catalog->closed = true;
}
