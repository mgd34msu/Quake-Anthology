/* Port of src/content/mods/pickups.ts. GPL-2.0-or-later. */
#include "qa/content/mods/pickups.h"

#include <stdlib.h>
#include <string.h>

static qa_save_reader field(const qa_save_reader *reader, const char *name) {
  qa_save_reader out;
  if (!qa_save_reader_field(reader, name, &out))
    qa_save_reader_fail(reader, "out of memory");
  return out;
}
static qa_save_reader item(const qa_save_reader *reader, size_t index) {
  qa_save_reader out;
  if (!qa_save_reader_item(reader, index, &out))
    qa_save_reader_fail(reader, "out of memory");
  return out;
}
static const char *field_choice(const qa_save_reader *reader, const char *name,
                                const char *const *choices, size_t count) {
  qa_save_reader f = field(reader, name);
  const char *value = qa_save_reader_choice_string(&f, choices, count);
  qa_save_reader_destroy(&f);
  return value;
}
static bool field_present(const qa_save_reader *reader, const char *name) {
  qa_save_reader f = field(reader, name);
  bool present = !qa_save_reader_is_undefined(&f);
  qa_save_reader_destroy(&f);
  return present;
}

static qa_pickup_write read_write(const qa_save_reader *reader, bool legacy) {
  static const char *const kinds[] = {"protection", "inventory"};
  static const char *const channels[] = {"regular", "powered"};
  static const char *const fields[] = {"count", "capacity", "count-and-capacity"};
  qa_pickup_write write;
  memset(&write, 0, sizeof write);
  if (strcmp(field_choice(reader, "kind", kinds, 2), "protection") == 0) {
    write.kind = QA_PICKUP_RESOURCE_PROTECTION;
    write.channel = strcmp(field_choice(reader, "channel", channels, 2), "regular") == 0
                        ? QA_PROTECTION_REGULAR
                        : QA_PROTECTION_POWERED;
  } else {
    write.kind = QA_PICKUP_RESOURCE_INVENTORY;
    qa_save_reader f = field(reader, "item");
    write.item = qa_namespaced(&f);
    qa_save_reader_destroy(&f);
    write.fields = legacy ? "count" : field_choice(reader, "fields", fields, 3);
  }
  return write;
}

static void *read_call_at(const qa_save_reader *reader, const char *name,
                          void (*read)(void *, const qa_save_reader *, void *),
                          void *ctx, size_t size) {
  qa_save_reader f = field(reader, name);
  void *storage = malloc(size == 0 ? 1 : size);
  if (storage == NULL) qa_save_reader_fail(&f, "out of memory");
  read(ctx, &f, storage);
  qa_save_reader_destroy(&f);
  return storage;
}

qa_mod_pickup_rule qa_read_mod_pickup_rule(
    const qa_save_reader *reader,
    void (*read_call)(void *ctx, const qa_save_reader *reader, void *out),
    void *read_ctx, size_t call_elem_size) {
  static const char *const op_kinds[] = {"boolean-grant", "gate-then-grant"};
  static const char *const accepts[] = {"nonzero", "always"};
  qa_mod_pickup_rule rule;
  memset(&rule, 0, sizeof rule);
  {
    qa_save_reader f = field(reader, "id");
    rule.id = qa_save_reader_string(&f);
    qa_save_reader_destroy(&f);
  }
  {
    qa_save_reader list = field(reader, "offered");
    size_t count = qa_save_reader_list_count(&list);
    qa_item_id *offered =
        count > 0 ? (qa_item_id *)malloc(count * sizeof *offered) : NULL;
    if (count > 0 && offered == NULL) qa_save_reader_fail(&list, "out of memory");
    for (size_t i = 0; i < count; i++) {
      qa_save_reader entry = item(&list, i);
      offered[i] = qa_namespaced(&entry);
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&list);
    rule.offered = offered;
    rule.offered_count = count;
  }
  bool distinct = true;
  for (size_t i = 0; i < rule.offered_count && distinct; i++)
    for (size_t j = i + 1; j < rule.offered_count; j++)
      if (strcmp(rule.offered[j], rule.offered[i]) == 0) distinct = false;
  if (rule.id[0] == '\0' || rule.offered_count == 0 || !distinct)
    qa_save_reader_fail(reader, "Pickup rule requires an id and distinct offered items");
  bool has_resource = field_present(reader, "resource");
  bool has_writes = field_present(reader, "writes");
  if (has_resource && has_writes)
    qa_save_reader_fail(reader, "Pickup rule has both resource and writes");
  if (!has_writes) {
    qa_save_reader resource = field(reader, "resource");
    qa_pickup_write *writes = (qa_pickup_write *)malloc(sizeof *writes);
    if (writes == NULL) qa_save_reader_fail(&resource, "out of memory");
    writes[0] = read_write(&resource, true);
    qa_save_reader_destroy(&resource);
    rule.writes.values = writes;
    rule.writes.count = 1;
  } else {
    qa_save_reader list = field(reader, "writes");
    size_t count = qa_save_reader_list_count(&list);
    qa_pickup_write *writes =
        count > 0 ? (qa_pickup_write *)malloc(count * sizeof *writes) : NULL;
    if (count > 0 && writes == NULL) qa_save_reader_fail(&list, "out of memory");
    for (size_t i = 0; i < count; i++) {
      qa_save_reader entry = item(&list, i);
      writes[i] = read_write(&entry, false);
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&list);
    rule.writes.values = writes;
    rule.writes.count = count;
  }
  if (rule.writes.count == 0)
    qa_save_reader_fail(reader, "Pickup rule requires a nonempty write set");
  for (size_t i = 0; i < rule.writes.count; i++)
    for (size_t j = i + 1; j < rule.writes.count; j++) {
      const qa_pickup_write *a = &rule.writes.values[i];
      const qa_pickup_write *b = &rule.writes.values[j];
      bool same = a->kind == b->kind &&
                  (a->kind == QA_PICKUP_RESOURCE_PROTECTION
                       ? a->channel == b->channel
                       : strcmp(a->item, b->item) == 0);
      if (same)
        qa_save_reader_fail(reader, "Pickup rule has duplicate resource writes");
    }
  qa_save_reader operation = field(reader, "operation");
  const void *grant = read_call_at(&operation, "grant", read_call, read_ctx, call_elem_size);
  rule.operation.call_elem_size = call_elem_size;
  if (strcmp(field_choice(&operation, "kind", op_kinds, 2), "boolean-grant") == 0) {
    rule.operation.is_gate_then_grant = false;
    rule.operation.grant = grant;
  } else {
    rule.operation.is_gate_then_grant = true;
    rule.operation.gate = read_call_at(&operation, "gate", read_call, read_ctx, call_elem_size);
    rule.operation.grant = grant;
    rule.operation.grant_accepts = field_choice(&operation, "grantAccepts", accepts, 2);
  }
  qa_save_reader_destroy(&operation);
  return rule;
}
