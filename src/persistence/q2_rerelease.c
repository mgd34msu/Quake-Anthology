/* Port of src/persistence/q2-rerelease.ts. GPL-2.0-or-later. */
#include "qa/persistence/q2_rerelease.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/persistence/value.h"

static qa_save_json *shallow_copy(const qa_save_json *object) {
  qa_save_json *copy = qa_save_json_object();
  if (copy == NULL) return NULL;
  for (size_t i = 0; i < object->entry_count; i++) {
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

static qa_save_json *root_of(const char *text) {
  qa_save_json *root = qa_parse_source_json(text);
  if (root == NULL) return NULL;
  (void)qa_source_object(root, "q2-rerelease");
  if (qa_source_number(qa_save_json_get_const(root, "save_version"), "save_version") != 1) {
    qa_save_json_free(root);
    qa_save_format_error("q2-rerelease", "unsupported game save version");
  }
  return root;
}
bool qa_decode_q2_rerelease_game(const char *text, qa_q2_rerelease_game_save *out) {
  qa_save_json *root = root_of(text);
  if (root == NULL) return false;
  qa_save_json *game = qa_source_object(qa_save_json_get(root, "game"), "game");
  qa_save_json *clients = qa_save_json_get(root, "clients");
  if (clients == NULL || clients->kind != QA_SAVE_JSON_ARRAY)
    qa_save_format_error("clients", "expected client array");
  qa_save_json **values = clients->item_count > 0
      ? (qa_save_json **)malloc(clients->item_count * sizeof *values) : NULL;
  if (clients->item_count > 0 && values == NULL) { qa_save_json_free(root); return false; }
  for (size_t i = 0; i < clients->item_count; i++) {
    char path[64];
    (void)snprintf(path, sizeof path, "clients[%zu]", i);
    values[i] = qa_source_object(clients->items[i], path);
  }
  out->root = root;
  out->game = game;
  out->clients = values;
  out->client_count = clients->item_count;
  return true;
}
char *qa_encode_q2_rerelease_game(const qa_q2_rerelease_game_save *save) {
  qa_save_json *merged = shallow_copy(save->root);
  if (merged == NULL) return NULL;
  qa_save_json *clients = array_of(save->clients, save->client_count);
  if (clients == NULL || !shallow_set(merged, "game", save->game) ||
      !shallow_set(merged, "clients", clients)) {
    shallow_free(clients);
    shallow_free(merged);
    return NULL;
  }
  char *text = qa_write_source_json(merged);
  shallow_free(clients);
  shallow_free(merged);
  return text;
}
bool qa_decode_q2_rerelease_level(const char *text, qa_q2_rerelease_level_save *out) {
  qa_save_json *root = root_of(text);
  if (root == NULL) return false;
  qa_save_json *level = qa_source_object(qa_save_json_get(root, "level"), "level");
  qa_save_json *records = qa_source_object(qa_save_json_get(root, "entities"), "entities");
  qa_q2_rerelease_entity *values = records->entry_count > 0
      ? (qa_q2_rerelease_entity *)malloc(records->entry_count * sizeof *values) : NULL;
  if (records->entry_count > 0 && values == NULL) { qa_save_json_free(root); return false; }
  for (size_t i = 0; i < records->entry_count; i++) {
    const char *key = records->entries[i].key;
    double slot = strtod(key, NULL);
    char canonical[64];
    if (slot == trunc(slot) && fabs(slot) <= 9007199254740991.0)
      (void)snprintf(canonical, sizeof canonical, "%lld", (long long)slot);
    else
      (void)snprintf(canonical, sizeof canonical, "%.17g", slot);
    char path[128];
    (void)snprintf(path, sizeof path, "entities.%s", key);
    if (!(slot == trunc(slot) && fabs(slot) <= 9007199254740991.0) || slot < 0 ||
        strcmp(canonical, key) != 0)
      qa_save_format_error(path, "invalid source entity index");
    values[i].slot = slot;
    values[i].record = qa_source_object(records->entries[i].value, path);
  }
  out->root = root;
  out->level = level;
  out->entities = values;
  out->entity_count = records->entry_count;
  return true;
}
char *qa_encode_q2_rerelease_level(const qa_q2_rerelease_level_save *save) {
  qa_save_json *merged = shallow_copy(save->root);
  if (merged == NULL) return NULL;
  qa_save_json *entities = qa_save_json_object();
  if (entities == NULL) { shallow_free(merged); return NULL; }
  bool ok = true;
  for (size_t i = 0; i < save->entity_count && ok; i++) {
    char key[64];
    double slot = save->entities[i].slot;
    if (slot == trunc(slot) && fabs(slot) <= 9007199254740991.0)
      (void)snprintf(key, sizeof key, "%lld", (long long)slot);
    else
      (void)snprintf(key, sizeof key, "%.17g", slot);
    /* Borrow the record node under a fresh owned key. */
    qa_save_json_entry *entries = (qa_save_json_entry *)realloc(
        entities->entries, (entities->entry_count + 1) * sizeof *entries);
    if (entries == NULL) { ok = false; break; }
    entities->entries = entries;
    char *copy = (char *)malloc(strlen(key) + 1);
    if (copy == NULL) { ok = false; break; }
    strcpy(copy, key);
    entities->entries[entities->entry_count].key = copy;
    entities->entries[entities->entry_count].value = save->entities[i].record;
    entities->entry_count++;
  }
  char *text = NULL;
  if (ok && shallow_set(merged, "level", save->level) &&
      shallow_set(merged, "entities", entities))
    text = qa_write_source_json(merged);
  for (size_t i = 0; i < entities->entry_count; i++) free(entities->entries[i].key);
  free(entities->entries);
  free(entities);
  shallow_free(merged);
  return text;
}

static qa_vec3 vector_of(const qa_save_json *value, const char *path) {
  qa_vec3 vector = {0, 0, 0};
  if (value == NULL || value->kind == QA_SAVE_JSON_UNDEFINED) return vector;
  if (value->kind != QA_SAVE_JSON_ARRAY || value->item_count != 3)
    qa_save_format_error(path, "expected source vec3");
  vector.x = qa_source_number(value->items[0], path);
  vector.y = qa_source_number(value->items[1], path);
  vector.z = qa_source_number(value->items[2], path);
  return vector;
}
static qa_save_json *vector_value(qa_vec3 value) {
  qa_save_json *array = qa_save_json_array();
  if (array == NULL) return NULL;
  qa_save_json *x = qa_save_number_from_double(value.x);
  qa_save_json *y = qa_save_number_from_double(value.y);
  qa_save_json *z = qa_save_number_from_double(value.z);
  if (x == NULL || y == NULL || z == NULL || !qa_save_json_push(array, x) ||
      !qa_save_json_push(array, y) || !qa_save_json_push(array, z)) {
    size_t pushed = array->item_count;
    qa_save_json_free(array);
    if (pushed < 1) qa_save_json_free(x);
    if (pushed < 2) qa_save_json_free(y);
    if (pushed < 3) qa_save_json_free(z);
    return NULL;
  }
  return array;
}
static bool boolean_of(const qa_save_json *value, const char *path) {
  if (value == NULL || value->kind == QA_SAVE_JSON_UNDEFINED) return false;
  if (value->kind != QA_SAVE_JSON_BOOLEAN)
    qa_save_format_error(path, "expected boolean");
  return value->boolean;
}
static double prefixed_number(const qa_save_json *record, const char *prefix,
                              const char *key) {
  char full[128];
  (void)snprintf(full, sizeof full, "%s%s", prefix, key);
  return qa_source_number(qa_save_json_get_const(record, full), full);
}
static bool set_number(qa_save_json *record, const char *key, double value) {
  qa_save_json *node = qa_save_number_from_double(value);
  if (node == NULL || !qa_save_json_set(record, key, node)) {
    qa_save_json_free(node);
    return false;
  }
  return true;
}
static bool set_prefixed_number(qa_save_json *record, const char *prefix,
                                const char *key, double value) {
  char full[128];
  (void)snprintf(full, sizeof full, "%s%s", prefix, key);
  return set_number(record, full, value);
}
static bool set_vector(qa_save_json *record, const char *key, qa_vec3 value) {
  qa_save_json *node = vector_value(value);
  if (node == NULL || !qa_save_json_set(record, key, node)) {
    qa_save_json_free(node);
    return false;
  }
  return true;
}
static bool set_prefixed_vector(qa_save_json *record, const char *prefix,
                                const char *key, qa_vec3 value) {
  char full[128];
  (void)snprintf(full, sizeof full, "%s%s", prefix, key);
  return set_vector(record, full, value);
}
static bool set_boolean(qa_save_json *record, const char *key, bool value) {
  qa_save_json *node = qa_save_json_boolean(value);
  if (node == NULL || !qa_save_json_set(record, key, node)) {
    qa_save_json_free(node);
    return false;
  }
  return true;
}

qa_q2_fog_save qa_read_q2_fog(const qa_save_json *record) {
  qa_q2_fog_save fog;
  fog.color = vector_of(qa_save_json_get_const(record, "fog.color"), "fog.color");
  fog.density = qa_source_number(qa_save_json_get_const(record, "fog.density"), "fog.density");
  fog.color_off = vector_of(qa_save_json_get_const(record, "fog.color_off"), "fog.color_off");
  fog.density_off =
      qa_source_number(qa_save_json_get_const(record, "fog.density_off"), "fog.density_off");
  fog.sky_factor =
      qa_source_number(qa_save_json_get_const(record, "fog.sky_factor"), "fog.sky_factor");
  fog.sky_factor_off = qa_source_number(
      qa_save_json_get_const(record, "fog.sky_factor_off"), "fog.sky_factor_off");
  return fog;
}
bool qa_write_q2_fog(qa_save_json *record, qa_q2_fog_save fog) {
  return set_vector(record, "fog.color", fog.color) &&
         set_number(record, "fog.density", fog.density) &&
         set_vector(record, "fog.color_off", fog.color_off) &&
         set_number(record, "fog.density_off", fog.density_off) &&
         set_number(record, "fog.sky_factor", fog.sky_factor) &&
         set_number(record, "fog.sky_factor_off", fog.sky_factor_off);
}
qa_q2_height_fog_save qa_read_q2_height_fog(const qa_save_json *record) {
  qa_q2_height_fog_save fog;
  fog.falloff = prefixed_number(record, "heightfog.", "falloff");
  fog.density = prefixed_number(record, "heightfog.", "density");
  fog.start_color = vector_of(
      qa_save_json_get_const(record, "heightfog.start_color"), "heightfog.start_color");
  fog.start_distance = prefixed_number(record, "heightfog.", "start_dist");
  fog.end_color = vector_of(
      qa_save_json_get_const(record, "heightfog.end_color"), "heightfog.end_color");
  fog.end_distance = prefixed_number(record, "heightfog.", "end_dist");
  fog.falloff_off = prefixed_number(record, "heightfog.", "falloff_off");
  fog.density_off = prefixed_number(record, "heightfog.", "density_off");
  fog.start_color_off = vector_of(qa_save_json_get_const(record, "heightfog.start_color_off"),
                                  "heightfog.start_color_off");
  fog.start_distance_off = prefixed_number(record, "heightfog.", "start_dist_off");
  fog.end_color_off = vector_of(
      qa_save_json_get_const(record, "heightfog.end_color_off"), "heightfog.end_color_off");
  fog.end_distance_off = prefixed_number(record, "heightfog.", "end_dist_off");
  return fog;
}
bool qa_write_q2_height_fog(qa_save_json *record, qa_q2_height_fog_save fog) {
  return set_prefixed_number(record, "heightfog.", "falloff", fog.falloff) &&
         set_prefixed_number(record, "heightfog.", "density", fog.density) &&
         set_prefixed_number(record, "heightfog.", "start_dist", fog.start_distance) &&
         set_prefixed_number(record, "heightfog.", "end_dist", fog.end_distance) &&
         set_prefixed_number(record, "heightfog.", "falloff_off", fog.falloff_off) &&
         set_prefixed_number(record, "heightfog.", "density_off", fog.density_off) &&
         set_prefixed_number(record, "heightfog.", "start_dist_off", fog.start_distance_off) &&
         set_prefixed_number(record, "heightfog.", "end_dist_off", fog.end_distance_off) &&
         set_prefixed_vector(record, "heightfog.", "start_color", fog.start_color) &&
         set_prefixed_vector(record, "heightfog.", "end_color", fog.end_color) &&
         set_prefixed_vector(record, "heightfog.", "start_color_off", fog.start_color_off) &&
         set_prefixed_vector(record, "heightfog.", "end_color_off", fog.end_color_off);
}
qa_q2_brush_animation_save qa_read_q2_brush_animation(const qa_save_json *record) {
  qa_q2_brush_animation_save animation;
  animation.start = prefixed_number(record, "bmodel_anim.", "start");
  animation.end = prefixed_number(record, "bmodel_anim.", "end");
  animation.style = prefixed_number(record, "bmodel_anim.", "style");
  animation.speed = prefixed_number(record, "bmodel_anim.", "speed");
  animation.no_wrap = boolean_of(
      qa_save_json_get_const(record, "bmodel_anim.nowrap"), "bmodel_anim.nowrap");
  animation.alternate_start = prefixed_number(record, "bmodel_anim.", "alt_start");
  animation.alternate_end = prefixed_number(record, "bmodel_anim.", "alt_end");
  animation.alternate_style = prefixed_number(record, "bmodel_anim.", "alt_style");
  animation.alternate_speed = prefixed_number(record, "bmodel_anim.", "alt_speed");
  animation.alternate_no_wrap = boolean_of(
      qa_save_json_get_const(record, "bmodel_anim.alt_nowrap"), "bmodel_anim.alt_nowrap");
  animation.enabled = boolean_of(
      qa_save_json_get_const(record, "bmodel_anim.enabled"), "bmodel_anim.enabled");
  animation.alternate = boolean_of(
      qa_save_json_get_const(record, "bmodel_anim.alternate"), "bmodel_anim.alternate");
  animation.currently_alternate = boolean_of(
      qa_save_json_get_const(record, "bmodel_anim.currently_alternate"),
      "bmodel_anim.currently_alternate");
  animation.next_tick = prefixed_number(record, "bmodel_anim.", "next_tick");
  return animation;
}
bool qa_write_q2_brush_animation(qa_save_json *record, qa_q2_brush_animation_save animation) {
  bool ok = set_prefixed_number(record, "bmodel_anim.", "start", animation.start) &&
      set_prefixed_number(record, "bmodel_anim.", "end", animation.end) &&
      set_prefixed_number(record, "bmodel_anim.", "style", animation.style) &&
      set_prefixed_number(record, "bmodel_anim.", "speed", animation.speed) &&
      set_prefixed_number(record, "bmodel_anim.", "alt_start", animation.alternate_start) &&
      set_prefixed_number(record, "bmodel_anim.", "alt_end", animation.alternate_end) &&
      set_prefixed_number(record, "bmodel_anim.", "alt_style", animation.alternate_style) &&
      set_prefixed_number(record, "bmodel_anim.", "alt_speed", animation.alternate_speed) &&
      set_prefixed_number(record, "bmodel_anim.", "next_tick", animation.next_tick);
  if (!ok) return false;
  return set_boolean(record, "bmodel_anim.nowrap", animation.no_wrap) &&
         set_boolean(record, "bmodel_anim.alt_nowrap", animation.alternate_no_wrap) &&
         set_boolean(record, "bmodel_anim.enabled", animation.enabled) &&
         set_boolean(record, "bmodel_anim.alternate", animation.alternate) &&
         set_boolean(record, "bmodel_anim.currently_alternate", animation.currently_alternate);
}
qa_q2_poi_save qa_read_q2_poi(const qa_save_json *level) {
  qa_q2_poi_save poi;
  poi.valid = boolean_of(qa_save_json_get_const(level, "valid_poi"), "valid_poi");
  poi.point = vector_of(qa_save_json_get_const(level, "current_poi"), "current_poi");
  poi.stage = qa_source_number(
      qa_save_json_get_const(level, "current_poi_stage"), "current_poi_stage");
  poi.image = qa_source_number(
      qa_save_json_get_const(level, "current_poi_image"), "current_poi_image");
  const qa_save_json *dynamic = qa_save_json_get_const(level, "current_dynamic_poi");
  poi.has_dynamic_entity = dynamic != NULL && dynamic->kind != QA_SAVE_JSON_UNDEFINED &&
                           dynamic->kind != QA_SAVE_JSON_NULL;
  poi.dynamic_entity = poi.has_dynamic_entity
      ? qa_source_number(dynamic, "current_dynamic_poi") : 0;
  return poi;
}
bool qa_write_q2_poi(qa_save_json *level, qa_q2_poi_save poi) {
  if (!set_boolean(level, "valid_poi", poi.valid)) return false;
  if (!set_vector(level, "current_poi", poi.point)) return false;
  if (!set_number(level, "current_poi_stage", poi.stage)) return false;
  if (!set_number(level, "current_poi_image", poi.image)) return false;
  qa_save_json *dynamic = poi.has_dynamic_entity
      ? qa_save_number_from_double(poi.dynamic_entity) : qa_save_json_null();
  if (dynamic == NULL || !qa_save_json_set(level, "current_dynamic_poi", dynamic)) {
    qa_save_json_free(dynamic);
    return false;
  }
  return true;
}
double *qa_read_q2_ammo_capacity(const qa_save_json *persistent, size_t *out_count) {
  const qa_save_json *values = qa_save_json_get_const(persistent, "max_ammo");
  if (values == NULL || values->kind == QA_SAVE_JSON_UNDEFINED) {
    *out_count = 0;
    return NULL;
  }
  if (values->kind != QA_SAVE_JSON_ARRAY)
    qa_save_format_error("max_ammo", "expected ammo capacity array");
  double *out = values->item_count > 0
      ? (double *)malloc(values->item_count * sizeof *out) : NULL;
  if (values->item_count > 0 && out == NULL) { *out_count = (size_t)-1; return NULL; }
  for (size_t i = 0; i < values->item_count; i++) {
    char path[64];
    (void)snprintf(path, sizeof path, "max_ammo[%zu]", i);
    out[i] = qa_source_number(values->items[i], path);
  }
  *out_count = values->item_count;
  return out;
}
bool qa_write_q2_ammo_capacity(qa_save_json *persistent, const double *values, size_t count) {
  qa_save_json *array = qa_save_json_array();
  if (array == NULL) return false;
  for (size_t i = 0; i < count; i++) {
    qa_save_json *node = qa_save_number_from_double(values[i]);
    if (node == NULL || !qa_save_json_push(array, node)) {
      qa_save_json_free(node);
      qa_save_json_free(array);
      return false;
    }
  }
  if (!qa_save_json_set(persistent, "max_ammo", array)) {
    qa_save_json_free(array);
    return false;
  }
  return true;
}
