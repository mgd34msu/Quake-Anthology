/* Port of src/persistence/q2-typescript.ts. GPL-2.0-or-later. */
#include "qa/persistence/q2_typescript.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/persistence/value.h"

/* Shallow object copy (entries point at the same child nodes); the
 * TS spread {...root, key: value}. Frees shallowly. */
static qa_save_json *shallow_copy(const qa_save_json *object) {
  qa_save_json *copy = qa_save_json_object();
  if (copy == NULL) return NULL;
  for (size_t i = 0; i < object->entry_count; i++) {
    /* Borrow key and child: duplicate the entry shell only. */
    qa_save_json_entry *entries = (qa_save_json_entry *)realloc(
        copy->entries, (copy->entry_count + 1) * sizeof *entries);
    if (entries == NULL) { free(copy->entries); free(copy); return NULL; }
    copy->entries = entries;
    copy->entries[copy->entry_count].key = object->entries[i].key;
    copy->entries[copy->entry_count].value = object->entries[i].value;
    copy->entry_count++;
  }
  return copy;
}
static void shallow_free(qa_save_json *object) {
  if (object == NULL) return;
  free(object->entries);
  free(object->items);
  free(object->text);
  free(object);
}
static bool shallow_set(qa_save_json *object, const char *key, qa_save_json *child) {
  for (size_t i = 0; i < object->entry_count; i++) {
    if (strcmp(object->entries[i].key, key) == 0) {
      object->entries[i].value = child;
      return true;
    }
  }
  qa_save_json_entry *entries = (qa_save_json_entry *)realloc(
      object->entries, (object->entry_count + 1) * sizeof *entries);
  if (entries == NULL) return false;
  object->entries = entries;
  object->entries[object->entry_count].key = (char *)key;
  object->entries[object->entry_count].value = child;
  object->entry_count++;
  return true;
}
static qa_save_json *array_of(qa_save_json **nodes, size_t count) {
  qa_save_json *array = qa_save_json_array();
  if (array == NULL) return NULL;
  if (count > 0) {
    array->items = (qa_save_json **)malloc(count * sizeof *array->items);
    if (array->items == NULL) { free(array); return NULL; }
    memcpy(array->items, nodes, count * sizeof *array->items);
    array->item_count = count;
  }
  return array;
}

bool qa_decode_q2_typescript_game(const char *text, qa_q2_typescript_game_save *out) {
  qa_save_json *root = qa_parse_source_json(text);
  if (root == NULL) return false;
  (void)qa_source_object(root, "q2-typescript");
  const qa_save_json *stamp = qa_save_json_get_const(root, "stamp");
  if (stamp == NULL || stamp->kind != QA_SAVE_JSON_STRING ||
      strcmp(stamp->text, "quake-2-ts:g_save:v1") != 0)
    qa_save_format_error("stamp", "unsupported TypeScript donor save version");
  qa_save_json *clients = qa_save_json_get(root, "clients");
  if (clients == NULL || clients->kind != QA_SAVE_JSON_ARRAY)
    qa_save_format_error("clients", "expected source client array");
  qa_save_json **values = clients->item_count > 0
      ? (qa_save_json **)malloc(clients->item_count * sizeof *values) : NULL;
  if (clients->item_count > 0 && values == NULL) { qa_save_json_free(root); return false; }
  for (size_t i = 0; i < clients->item_count; i++) {
    char path[64];
    (void)snprintf(path, sizeof path, "clients[%zu]", i);
    values[i] = qa_source_object(clients->items[i], path);
  }
  out->root = root;
  out->clients = values;
  out->client_count = clients->item_count;
  return true;
}
char *qa_encode_q2_typescript_game(const qa_q2_typescript_game_save *save) {
  qa_save_json *merged = shallow_copy(save->root);
  if (merged == NULL) return NULL;
  qa_save_json *clients = array_of(save->clients, save->client_count);
  if (clients == NULL || !shallow_set(merged, "clients", clients)) {
    shallow_free(clients);
    shallow_free(merged);
    return NULL;
  }
  char *text = qa_write_source_json(merged);
  shallow_free(clients);
  shallow_free(merged);
  return text;
}
bool qa_decode_q2_typescript_level(const char *text, qa_q2_typescript_level_save *out) {
  qa_save_json *root = qa_parse_source_json(text);
  if (root == NULL) return false;
  (void)qa_source_object(root, "q2-typescript");
  qa_save_json *level = qa_source_object(qa_save_json_get(root, "level"), "level");
  qa_save_json *edicts = qa_save_json_get(root, "edicts");
  if (edicts == NULL || edicts->kind != QA_SAVE_JSON_ARRAY)
    qa_save_format_error("edicts", "expected source entity array");
  qa_q2_typescript_level_edict *values = edicts->item_count > 0
      ? (qa_q2_typescript_level_edict *)malloc(edicts->item_count * sizeof *values) : NULL;
  if (edicts->item_count > 0 && values == NULL) { qa_save_json_free(root); return false; }
  for (size_t i = 0; i < edicts->item_count; i++) {
    char path[64];
    (void)snprintf(path, sizeof path, "edicts[%zu]", i);
    qa_save_json *record = qa_source_object(edicts->items[i], path);
    const qa_save_json *index_node = qa_save_json_get_const(record, "index");
    if (index_node == NULL)
      qa_save_format_error("edicts.index", "missing source entity slot");
    char index_path[80];
    (void)snprintf(index_path, sizeof index_path, "edicts[%zu].index", i);
    double index = qa_source_number(index_node, index_path);
    if (!(index == trunc(index) && fabs(index) <= 9007199254740991.0) || index < 0)
      qa_save_format_error("edicts.index", "invalid source entity slot");
    char data_path[80];
    (void)snprintf(data_path, sizeof data_path, "edicts[%zu].data", i);
    values[i].index = index;
    values[i].data = qa_source_object(qa_save_json_get(record, "data"), data_path);
  }
  out->root = root;
  out->level = level;
  out->edicts = values;
  out->edict_count = edicts->item_count;
  return true;
}
char *qa_encode_q2_typescript_level(const qa_q2_typescript_level_save *save) {
  qa_save_json *merged = shallow_copy(save->root);
  if (merged == NULL) return NULL;
  qa_save_json *array = qa_save_json_array();
  if (array == NULL) { shallow_free(merged); return NULL; }
  /* Records are fresh nodes owning a fresh SaveNumber; data borrowed. */
  qa_save_json **records = save->edict_count > 0
      ? (qa_save_json **)calloc(save->edict_count, sizeof *records) : NULL;
  if (save->edict_count > 0 && records == NULL) {
    qa_save_json_free(array);
    shallow_free(merged);
    return NULL;
  }
  bool ok = true;
  for (size_t i = 0; i < save->edict_count && ok; i++) {
    char number[64];
    double index = save->edicts[i].index;
    if (index == trunc(index) && fabs(index) <= 9007199254740991.0)
      (void)snprintf(number, sizeof number, "%lld", (long long)index);
    else
      (void)snprintf(number, sizeof number, "%.17g", index);
    qa_save_json *record = qa_save_json_object();
    qa_save_json *number_node = record != NULL ? qa_save_number_create(number) : NULL;
    if (record == NULL || number_node == NULL ||
        !qa_save_json_set(record, "index", number_node)) {
      qa_save_json_free(record);
      qa_save_json_free(number_node);
      ok = false;
      break;
    }
    /* Borrow data node without taking ownership. */
    qa_save_json_entry *entries = (qa_save_json_entry *)realloc(
        record->entries, (record->entry_count + 1) * sizeof *entries);
    if (entries == NULL) { qa_save_json_free(record); ok = false; break; }
    record->entries = entries;
    char *key = (char *)malloc(sizeof "data");
    if (key == NULL) { qa_save_json_free(record); ok = false; break; }
    memcpy(key, "data", sizeof "data");
    record->entries[record->entry_count].key = key;
    record->entries[record->entry_count].value = save->edicts[i].data;
    record->entry_count++;
    records[i] = record;
    if (!qa_save_json_push(array, record)) ok = false;
  }
  char *text = NULL;
  if (ok && shallow_set(merged, "level", save->level) &&
      shallow_set(merged, "edicts", array))
    text = qa_write_source_json(merged);
  /* Free record shells without their borrowed data nodes. */
  if (records != NULL) {
    for (size_t i = 0; i < save->edict_count; i++) {
      qa_save_json *record = records[i];
      if (record == NULL) continue;
      for (size_t e = 0; e < record->entry_count; e++) {
        bool owned = strcmp(record->entries[e].key, "index") == 0;
        free(record->entries[e].key);
        if (owned) qa_save_json_free(record->entries[e].value);
      }
      free(record->entries);
      free(record);
    }
    free(records);
  }
  free(array->items);
  free(array);
  shallow_free(merged);
  return text;
}
