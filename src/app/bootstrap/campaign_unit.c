/* Port of src/app/bootstrap/campaign-unit.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/campaign_unit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
#include "qa/persistence/provider_ownership.h"
#include "qa/persistence/value.h"
typedef struct { char *key; uint8_t *bytes; size_t length; } world_row;
typedef struct { world_row *rows; size_t count, cap; } worlds_t;
struct qa_app_campaign_unit {
  qa_app_campaign_codec codec;
  bool has_current;
  char *current_content, *current_path;
  worlds_t worlds;
  double revision;
};
struct qa_app_campaign_unit_visit {
  qa_app_campaign_unit *unit;
  double revision;
  char *destination_content, *destination_path;
  worlds_t worlds;
  const qa_save_image *restore;
};
static char *dup(const char *s) { if (!s) return NULL; size_t n = strlen(s) + 1; char *p = malloc(n); if (p) memcpy(p, s, n); return p; }
static qa_app_resource_request location_of(const qa_save_image *image) {
  return (qa_app_resource_request){image->recipe.map.geometry_content, image->recipe.map.geometry.requested_path};
}
static char *map_key(const char *content, const char *path) {
  const char *stem = path;
  if (strncmp(stem, "maps/", 5) == 0) stem += 5;
  size_t len = strlen(stem);
  if (len >= 4 && strcmp(stem + len - 4, ".bsp") == 0) len -= 4;
  size_t cap = strlen(content) + len + 2;
  char *key = malloc(cap);
  snprintf(key, cap, "%s/%.*s", content, (int)len, stem);
  return key;
}
static bool keys_equal(const char *c1, const char *p1, const char *c2, const char *p2) {
  char *a = map_key(c1, p1), *b = map_key(c2, p2);
  bool equal = strcmp(a, b) == 0;
  free(a); free(b);
  return equal;
}
static size_t campaign_entries(const qa_save_image *image, const qa_provider_checkpoint **out_first) {
  size_t count = 0;
  *out_first = NULL;
  for (size_t i = 0; i < image->provider_count; i++) {
    const qa_provider_checkpoint *entry = &image->providers[i];
    bool match = entry->provider && qa_campaign_unit_checkpoint.provider &&
                     strcmp(entry->provider, qa_campaign_unit_checkpoint.provider) == 0;
    if (!match && entry->schema && qa_campaign_unit_checkpoint.schema && strcmp(entry->schema, qa_campaign_unit_checkpoint.schema) == 0) match = true;
    if (match) { if (!*out_first) *out_first = entry; count++; }
  }
  if (count > 1) qa_range_error("Duplicate campaign unit checkpoint");
  if (*out_first) qa_validate_save_provider_owner(**out_first, image->recipe.map.entities.provider);
  return count;
}
static void worlds_free(worlds_t *worlds) {
  for (size_t i = 0; i < worlds->count; i++) { free(worlds->rows[i].key); free(worlds->rows[i].bytes); }
  free(worlds->rows);
  *worlds = (worlds_t){0};
}
static void worlds_set(worlds_t *worlds, char *key, uint8_t *bytes, size_t length) {
  for (size_t i = 0; i < worlds->count; i++)
    if (strcmp(worlds->rows[i].key, key) == 0) { free(worlds->rows[i].bytes); worlds->rows[i].bytes = bytes; worlds->rows[i].length = length; free(key); return; }
  if (worlds->count == worlds->cap) { worlds->cap = worlds->cap ? worlds->cap * 2 : 8; worlds->rows = realloc(worlds->rows, worlds->cap * sizeof *worlds->rows); }
  worlds->rows[worlds->count++] = (world_row){key, bytes, length};
}
static world_row *worlds_take(worlds_t *worlds, const char *key, world_row *out) {
  for (size_t i = 0; i < worlds->count; i++)
    if (strcmp(worlds->rows[i].key, key) == 0) {
      *out = worlds->rows[i];
      memmove(&worlds->rows[i], &worlds->rows[i + 1], (worlds->count - i - 1) * sizeof *worlds->rows);
      worlds->count--;
      return out;
    }
  return NULL;
}
static worlds_t worlds_copy(const worlds_t *source) {
  worlds_t copy = {0};
  for (size_t i = 0; i < source->count; i++) {
    uint8_t *bytes = malloc(source->rows[i].length ? source->rows[i].length : 1);
    memcpy(bytes, source->rows[i].bytes, source->rows[i].length);
    worlds_set(&copy, dup(source->rows[i].key), bytes, source->rows[i].length);
  }
  return copy;
}
/* worldOnly: validate entries; encode the image as given (provider
 * filtering lives in the host codec envelope, as in landed save code). */
static void validate_world_only(const qa_save_image *image) {
  const qa_provider_checkpoint *first;
  campaign_entries(image, &first);
}
qa_app_campaign_unit *qa_app_campaign_unit_create(qa_app_campaign_codec codec) {
  qa_app_campaign_unit *unit = calloc(1, sizeof *unit);
  if (!unit) return NULL;
  unit->codec = codec;
  return unit;
}
void qa_app_campaign_unit_destroy(qa_app_campaign_unit *unit) {
  if (!unit) return;
  free(unit->current_content);
  free(unit->current_path);
  worlds_free(&unit->worlds);
  free(unit);
}
qa_app_campaign_unit_visit *qa_app_campaign_unit_stage(qa_app_campaign_unit *unit, qa_app_resource_request destination,
                                                       bool new_unit, const qa_save_image *departure) {
  if (departure && unit->has_current) {
    qa_app_resource_request from = location_of(departure);
    if (!keys_equal(from.content, from.path, unit->current_content, unit->current_path))
      qa_range_error("Campaign departure does not match the active map");
  }
  bool reset = new_unit || (unit->has_current && strcmp(unit->current_content, destination.content) != 0);
  worlds_t worlds = reset ? (worlds_t){0} : worlds_copy(&unit->worlds);
  if (!reset && departure) {
    validate_world_only(departure);
    qa_app_resource_request from = location_of(departure);
    uint8_t *bytes = NULL; size_t length = 0;
    unit->codec.encode(unit->codec.ctx, departure, &bytes, &length);
    worlds_set(&worlds, map_key(from.content, from.path), bytes, length);
  }
  char *dest_key = map_key(destination.content, destination.path);
  world_row saved;
  const qa_save_image *restore = NULL;
  if (worlds_take(&worlds, dest_key, &saved)) {
    restore = unit->codec.decode(unit->codec.ctx, saved.bytes, saved.length);
    free(saved.key);
    free(saved.bytes);
  }
  free(dest_key);
  qa_app_campaign_unit_visit *visit = calloc(1, sizeof *visit);
  visit->unit = unit;
  visit->revision = unit->revision;
  visit->destination_content = dup(destination.content);
  visit->destination_path = dup(destination.path);
  visit->worlds = worlds;
  visit->restore = restore;
  return visit;
}
const qa_save_image *qa_app_campaign_unit_visit_restore(const qa_app_campaign_unit_visit *visit) { return visit->restore; }
void qa_app_campaign_unit_visit_destroy(qa_app_campaign_unit_visit *visit) {
  if (!visit) return;
  free(visit->destination_content);
  free(visit->destination_path);
  worlds_free(&visit->worlds);
  free(visit);
}
void qa_app_campaign_unit_visit_commit(qa_app_campaign_unit_visit *visit) {
  qa_app_campaign_unit *unit = visit->unit;
  if (visit->revision != unit->revision) qa_range_error("Campaign visit was superseded");
  free(unit->current_content);
  free(unit->current_path);
  unit->current_content = dup(visit->destination_content);
  unit->current_path = dup(visit->destination_path);
  unit->has_current = true;
  worlds_free(&unit->worlds);
  unit->worlds = visit->worlds;
  visit->worlds = (worlds_t){0};
  unit->revision++;
  qa_app_campaign_unit_visit_destroy(visit);
}
qa_provider_checkpoint qa_app_campaign_unit_checkpoint(qa_app_campaign_unit *unit, bool has_current,
                                                       qa_app_resource_request current, uint8_t **out_bytes, size_t *out_length) {
  qa_save_value *root = qa_save_value_object();
  if (!has_current && !unit->has_current) qa_save_value_set(root, "current", qa_save_value_null());
  else {
    const char *content = has_current ? current.content : unit->current_content;
    const char *path = has_current ? current.path : unit->current_path;
    qa_save_value *entry = qa_save_value_object();
    qa_save_value_set(entry, "content", qa_save_value_string(content));
    qa_save_value_set(entry, "path", qa_save_value_string(path));
    qa_save_value_set(root, "current", entry);
  }
  qa_save_value *list = qa_save_value_array();
  for (size_t i = 0; i < unit->worlds.count; i++) {
    qa_save_value *entry = qa_save_value_object();
    qa_save_value_set(entry, "map", qa_save_value_string(unit->worlds.rows[i].key));
    qa_save_value_set(entry, "bytes", qa_save_value_bytes(unit->worlds.rows[i].bytes, unit->worlds.rows[i].length));
    qa_save_value_push(list, entry);
  }
  qa_save_value_set(root, "worlds", list);
  *out_bytes = qa_encode_checkpoint_value(root, out_length);
  qa_save_value_free(root);
  qa_provider_checkpoint checkpoint;
  checkpoint.provider = qa_campaign_unit_checkpoint.provider;
  checkpoint.schema = qa_campaign_unit_checkpoint.schema;
  checkpoint.version = qa_campaign_unit_checkpoint.version;
  checkpoint.bytes.bytes = *out_bytes;
  checkpoint.bytes.length = *out_length;
  return checkpoint;
}
qa_provider_checkpoint qa_app_campaign_unit_attach(qa_app_campaign_unit *unit, const qa_save_image *image,
                                                   uint8_t **out_bytes, size_t *out_length) {
  qa_app_resource_request current = location_of(image);
  validate_world_only(image);
  if (unit->has_current && !keys_equal(unit->current_content, unit->current_path, current.content, current.path))
    qa_range_error("Campaign save does not match the active map");
  return qa_app_campaign_unit_checkpoint(unit, true, current, out_bytes, out_length);
}
void qa_app_campaign_unit_restore(qa_app_campaign_unit *unit, const qa_save_image *image) {
  const qa_provider_checkpoint *checkpoint;
  campaign_entries(image, &checkpoint);
  qa_app_resource_request here = location_of(image);
  char *current_content = dup(here.content), *current_path = dup(here.path);
  worlds_t worlds = {0};
  if (checkpoint) {
    qa_save_value *value = qa_decode_checkpoint_value(checkpoint->bytes.bytes, checkpoint->bytes.length);
    qa_save_reader reader, current_reader;
    qa_save_reader_init(&reader, value, "campaign-unit");
    qa_save_reader_field(&reader, "current", &current_reader);
    bool is_null = qa_save_reader_is_null(&current_reader);
    char *content = NULL, *path = NULL;
    if (!is_null) {
      qa_save_reader content_reader, path_reader;
      qa_save_reader_field(&current_reader, "content", &content_reader);
      content = dup(qa_save_reader_string(&content_reader));
      qa_save_reader_destroy(&content_reader);
      if (!qa_is_content_id(content)) qa_range_error("Invalid campaign content identity");
      qa_save_reader_field(&current_reader, "path", &path_reader);
      path = dup(qa_save_reader_string(&path_reader));
      qa_save_reader_destroy(&path_reader);
    }
    qa_save_reader_destroy(&current_reader);
    if (!content || !keys_equal(content, path, here.content, here.path))
      qa_range_error("Campaign checkpoint active map does not match its world");
    free(current_content); free(current_path);
    current_content = content; current_path = path;
    qa_save_reader list_reader;
    qa_save_reader_field(&reader, "worlds", &list_reader);
    size_t count = qa_save_reader_list_count(&list_reader);
    for (size_t i = 0; i < count; i++) {
      qa_save_reader item, map_reader, bytes_reader;
      qa_save_reader_item(&list_reader, i, &item);
      qa_save_reader_field(&item, "map", &map_reader);
      char *map = dup(qa_save_reader_string(&map_reader));
      qa_save_reader_destroy(&map_reader);
      qa_save_reader_field(&item, "bytes", &bytes_reader);
      size_t length = 0;
      const uint8_t *data = qa_save_reader_bytes(&bytes_reader, &length);
      uint8_t *bytes = malloc(length ? length : 1);
      memcpy(bytes, data, length);
      qa_save_reader_destroy(&bytes_reader);
      qa_save_reader_destroy(&item);
      const qa_save_image *saved = unit->codec.decode(unit->codec.ctx, bytes, length);
      qa_app_resource_request saved_location = location_of(saved);
      char *saved_key = map_key(saved_location.content, saved_location.path);
      char *current_key = map_key(current_content, current_path);
      bool duplicate = false;
      for (size_t j = 0; j < worlds.count; j++) if (strcmp(worlds.rows[j].key, map) == 0) duplicate = true;
      const qa_provider_checkpoint *nested;
      size_t nested_count = campaign_entries(saved, &nested);
      bool invalid = strcmp(map, saved_key) != 0 || strcmp(saved_location.content, current_content) != 0 ||
          strcmp(map, current_key) == 0 || duplicate || nested_count != 0;
      free(saved_key); free(current_key);
      if (invalid) qa_range_error("Invalid campaign unit world checkpoint");
      worlds_set(&worlds, map, bytes, length);
    }
    qa_save_reader_destroy(&list_reader);
    qa_save_reader_destroy(&reader);
    qa_save_value_free(value);
  }
  free(unit->current_content);
  free(unit->current_path);
  unit->current_content = current_content;
  unit->current_path = current_path;
  unit->has_current = true;
  worlds_free(&unit->worlds);
  unit->worlds = worlds;
  unit->revision++;
}
