/* Port of src/app/bootstrap/held-weapon.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/held_weapon.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/content/held_weapon.h"
#include "qa/content/q1/foundation/held_weapons.h"
#include "qa/content/q2/foundation/held_weapons.h"
#include "qa/content/q2/foundation/weapon_attachments.h"
#include "qa/content/q3/foundation/held_weapons.h"
#include "qa/foundation/support.h"
#include "qa/network/common/js_number.h"
#include "qa/render/scene/models/attachment.h"

typedef struct declaration_row {
  char *key;
  bool present; /* false is the TS undefined result */
  qa_held_weapon_declaration declaration;
} declaration_row;
/* TS Promise<HeldAsset | null>: present false is the TS null. */
typedef struct model_row {
  char *key;
  bool present;
  qa_resolved_resource_reference resource;
  qa_decoded_model model;
  qa_q1_alias_model *part_model;           /* owned q1 subset copy */
  qa_q1_alias_triangle *part_triangles;    /* owned */
} model_row;
struct qa_app_foreign_held_weapons {
  qa_app_held_assets assets;
  declaration_row **declarations; size_t declaration_count, declaration_capacity;
  model_row **models; size_t model_count, model_capacity;
};

static void *xcalloc(size_t count, size_t size) {
  void *value = calloc(count == 0 ? 1 : count, size);
  if (value == NULL) qa_range_error("Out of memory");
  return value;
}
static char *text_join(const char *a, const char *b, const char *c) {
  size_t length = strlen(a) + strlen(b) + strlen(c) + 1;
  char *out = xcalloc(length, 1);
  snprintf(out, length, "%s%s%s", a, b, c);
  return out;
}
/* Growable text for cache keys. */
typedef struct key_text { char *data; size_t length, capacity; } key_text;
static void key_append(key_text *key, const char *text) {
  size_t add = strlen(text);
  if (key->length + add + 1 > key->capacity) {
    size_t capacity = key->capacity == 0 ? 128 : key->capacity;
    while (capacity < key->length + add + 1) capacity *= 2;
    char *next = realloc(key->data, capacity);
    if (next == NULL) qa_range_error("Out of memory");
    key->data = next; key->capacity = capacity;
  }
  memcpy(key->data + key->length, text, add + 1);
  key->length += add;
}

static qa_content_mounted *application_mounts(void *ctx, qa_content_id content) {
  return qa_app_application_assets_provider(ctx, content).mounts;
}
static qa_app_model_asset application_model(void *ctx, qa_content_id content, const char *path) {
  return qa_app_application_assets_model(ctx, content, path);
}
qa_app_held_assets qa_app_held_assets_application(qa_app_application_assets *assets) {
  qa_app_held_assets out = { assets, application_mounts, application_model };
  return out;
}

qa_app_foreign_held_weapons *qa_app_foreign_held_weapons_create(qa_app_held_assets assets) {
  qa_app_foreign_held_weapons *weapons = calloc(1, sizeof *weapons);
  if (weapons == NULL) return NULL;
  weapons->assets = assets;
  return weapons;
}
void qa_app_foreign_held_weapons_destroy(qa_app_foreign_held_weapons *weapons) {
  if (weapons == NULL) return;
  for (size_t i = 0; i < weapons->declaration_count; i++) {
    declaration_row *row = weapons->declarations[i];
    if (row->present) qa_content_held_weapon_free(&row->declaration);
    free(row->key); free(row);
  }
  free(weapons->declarations);
  for (size_t i = 0; i < weapons->model_count; i++) {
    model_row *row = weapons->models[i];
    free(row->part_triangles); free(row->part_model); free(row->key); free(row);
  }
  free(weapons->models);
  free(weapons);
}

/* TS mounts.open(path) !== null. */
static bool resource_present(qa_content_mounted *mounts, const char *path) {
  qa_content_opened_resource opened;
  if (!qa_content_mounted_open(mounts, path, NULL, NULL, &opened)) return false;
  free(opened.bytes);
  return true;
}

const qa_held_weapon_declaration *qa_app_foreign_held_weapons_declaration(qa_app_foreign_held_weapons *weapons,
    const qa_app_simulation_presentation *source) {
  if (source->held_weapon != NULL || source->path[0] == '\0') return source->held_weapon;
  char *key = text_join(source->content, "/", source->path);
  for (size_t i = 0; i < weapons->declaration_count; i++)
    if (strcmp(weapons->declarations[i]->key, key) == 0) {
      free(key);
      return weapons->declarations[i]->present ? &weapons->declarations[i]->declaration : NULL;
    }
  /* The pending body: a raise leaves no cache entry (cacheEvictingRejection). */
  qa_cleanup guard;
  qa_cleanup_push(&guard, free, key);
  qa_content_mounted *mounts = weapons->assets.mounts(weapons->assets.ctx, source->content);
  char *path = text_join(source->path, ".held.json", "");
  qa_content_opened_resource file;
  bool opened = qa_content_mounted_open(mounts, path, NULL, NULL, &file);
  free(path);
  declaration_row *row = xcalloc(1, sizeof *row);
  if (opened) {
    qa_cleanup bytes_guard, row_guard;
    qa_cleanup_push(&bytes_guard, free, file.bytes);
    qa_cleanup_push(&row_guard, free, row);
    if (!qa_read_held_weapon_file(file.bytes, file.byte_length, &row->declaration)) qa_range_error("Out of memory");
    qa_cleanup_pop(&row_guard);
    qa_cleanup_pop(&bytes_guard);
    free(file.bytes);
    row->present = true;
  }
  qa_cleanup_pop(&guard);
  row->key = key;
  if (weapons->declaration_count == weapons->declaration_capacity) {
    size_t capacity = weapons->declaration_capacity == 0 ? 8 : weapons->declaration_capacity * 2;
    declaration_row **next = realloc(weapons->declarations, capacity * sizeof *next);
    if (next == NULL) qa_range_error("Out of memory");
    weapons->declarations = next; weapons->declaration_capacity = capacity;
  }
  weapons->declarations[weapons->declaration_count++] = row;
  return row->present ? &row->declaration : NULL;
}

static bool part_contains(const qa_held_weapon_part *part, double index) {
  for (size_t i = 0; i < part->vertices.count; i++) if (part->vertices.values[i] == index) return true;
  return false;
}

/* TS private model(source, held). */
static const model_row *held_model(qa_app_foreign_held_weapons *weapons, const qa_app_simulation_presentation *source,
                                   const qa_held_weapon_model *held) {
  key_text key = {0};
  key_append(&key, source->content); key_append(&key, "/"); key_append(&key, held->path); key_append(&key, "/");
  key_append(&key, held->fallback != NULL ? held->fallback : ""); key_append(&key, "/");
  if (held->part != NULL)
    for (size_t i = 0; i < held->part->digests.count; i++) {
      if (i != 0) key_append(&key, ",");
      key_append(&key, held->part->digests.values[i]);
    }
  key_append(&key, "/");
  if (held->part != NULL)
    for (size_t i = 0; i < held->part->vertices.count; i++) {
      if (i != 0) key_append(&key, ",");
      key_append(&key, QA_JS_NUMBER(held->part->vertices.values[i]));
    }
  for (size_t i = 0; i < weapons->model_count; i++)
    if (strcmp(weapons->models[i]->key, key.data) == 0) { free(key.data); return weapons->models[i]; }
  qa_cleanup key_guard;
  qa_cleanup_push(&key_guard, free, key.data);
  model_row *row = xcalloc(1, sizeof *row);
  qa_cleanup row_guard;
  qa_cleanup_push(&row_guard, free, row);
  qa_content_mounted *mounts = weapons->assets.mounts(weapons->assets.ctx, source->content);
  const char *path = held->path;
  bool present = true;
  if (!resource_present(mounts, path)) {
    if (held->fallback == NULL || !resource_present(mounts, held->fallback)) present = false;
    else path = held->fallback;
  }
  if (present) {
    qa_app_model_asset asset = weapons->assets.model(weapons->assets.ctx, source->content, path);
    row->resource = asset.resource;
    row->model = asset.model;
    if (held->part != NULL) {
      bool qualified = false;
      for (size_t i = 0; i < held->part->digests.count; i++)
        if (strcmp(held->part->digests.values[i], asset.resource.digest) == 0) { qualified = true; break; }
      if (!qualified || asset.model.kind != QA_DECODED_MODEL_Q1_ALIAS)
        qa_raise("Error", 0, 0, "Held model subset is not qualified for %s/%s", source->content, path);
      const qa_q1_alias_model *model = asset.model.as.q1_alias;
      qa_q1_alias_triangle *triangles = xcalloc(model->triangle_count, sizeof *triangles);
      size_t count = 0;
      for (size_t t = 0; t < model->triangle_count; t++) {
        const qa_q1_alias_triangle *triangle = &model->triangles[t];
        if (part_contains(held->part, triangle->vertices[0]) && part_contains(held->part, triangle->vertices[1])
            && part_contains(held->part, triangle->vertices[2])) triangles[count++] = *triangle;
      }
      if (count == 0) { free(triangles); qa_raise("Error", 0, 0, "Held model part has no source triangles"); }
      qa_q1_alias_model *copy = xcalloc(1, sizeof *copy);
      *copy = *model;
      copy->triangles = triangles; copy->triangle_count = count;
      row->part_model = copy; row->part_triangles = triangles;
      row->model.as.q1_alias = copy;
      row->model.replacement = NULL;
    }
  }
  row->present = present;
  qa_cleanup_pop(&row_guard);
  qa_cleanup_pop(&key_guard);
  row->key = key.data;
  if (weapons->model_count == weapons->model_capacity) {
    size_t capacity = weapons->model_capacity == 0 ? 8 : weapons->model_capacity * 2;
    model_row **next = realloc(weapons->models, capacity * sizeof *next);
    if (next == NULL) qa_range_error("Out of memory");
    weapons->models = next; weapons->model_capacity = capacity;
  }
  weapons->models[weapons->model_count++] = row;
  return row;
}

/* asset.model.frames.length for every non-brush DecodedModel. */
static size_t model_frame_count(const qa_decoded_model *model) {
  switch (model->kind) {
    case QA_DECODED_MODEL_Q1_ALIAS: return model->as.q1_alias->frame_count;
    case QA_DECODED_MODEL_Q2_ALIAS: return model->as.q2_alias->frame_count;
    case QA_DECODED_MODEL_Q1_SPRITE: return model->as.q1_sprite->frame_count;
    case QA_DECODED_MODEL_Q2_SPRITE: return model->as.q2_sprite->frame_count;
    case QA_DECODED_MODEL_Q3_MESH: return model->as.q3_mesh->frame_count;
    case QA_DECODED_MODEL_MD4: return model->as.md4->frame_count;
    case QA_DECODED_MODEL_MD5: return model->as.md5->frame_count;
    case QA_DECODED_MODEL_BRUSH_MODEL: return 0;
  }
  return 0;
}

/* TS options: () => ({}). */
static qa_model_source_options empty_options(void *ctx, const qa_scene_entity *entity) {
  (void)ctx; (void)entity;
  qa_model_source_options options;
  memset(&options, 0, sizeof options);
  return options;
}

size_t qa_app_foreign_held_weapons_frame(qa_app_foreign_held_weapons *weapons,
    const qa_app_simulation_presentation *source, qa_app_held_character character, qa_q3_character_pass *out) {
  const qa_held_weapon_declaration *declaration = qa_app_foreign_held_weapons_declaration(weapons, source);
  if (declaration != NULL && !declaration->is_model) return 0;
  qa_held_weapon_model fallback;
  char fallback_path[256];
  const qa_held_weapon_model *held = NULL;
  if (declaration != NULL) held = &declaration->model;
  else if (source->family == QA_GAME_FAMILY_Q2) {
    if (qa_q2_held_weapon(source->path, source->weapon_item, &fallback, fallback_path, sizeof fallback_path)) held = &fallback;
  } else if (source->family == QA_GAME_FAMILY_Q1) held = qa_q1_held_weapon(source->path);
  if (held == NULL)
    qa_raise("Error", 0, 0, "Selected weapon %s/%s has no authored held model declaration", source->content, source->path);
  const model_row *asset = held_model(weapons, source, held);
  if (!asset->present) qa_raise("Error", 0, 0, "Source held model is absent: %s/%s", source->content, held->path);
  if (held->has_digest && strcmp(held->digest, asset->resource.digest) != 0)
    qa_raise("Error", 0, 0, "Held model digest differs from its source declaration");
  if (asset->model.kind == QA_DECODED_MODEL_BRUSH_MODEL || held->reference_frame >= (double)model_frame_count(&asset->model))
    qa_raise("Error", 0, 0, "Held model reference frame is outside its source animation");
  const qa_model_attachment_definition *source_grip = declaration == NULL && source->family == QA_GAME_FAMILY_Q2
      ? qa_q2_weapon_attachment(asset->resource.digest) : NULL;
  qa_model_transform transform = qa_align_model_attachment(
      source_grip != NULL && !source_grip->is_joint ? source_grip->grip : held->grip, qa_q3_weapon_hand_grip);
  qa_q3_character_pass pass;
  memset(&pass, 0, sizeof pass);
  pass.content = source->content;
  pass.shader = NULL;
  pass.options = empty_options;
  pass.options_ctx = NULL;
  qa_scene_entity *entity = &pass.entity;
  entity->actor = &source->actor;
  entity->resource = asset->resource;
  entity->model = asset->model;
  entity->transform = transform;
  entity->previous_origin = transform.origin;
  entity->pose.is_frame = true;
  entity->pose.frame = held->reference_frame;
  entity->pose.previous_frame = held->reference_frame;
  entity->pose.back_lerp = 0;
  entity->skin = 0;
  entity->color = character.color;
  entity->has_opacity = true;
  entity->opacity = character.has_opacity ? character.opacity : 1;
  entity->shader_time.kind = QA_SOURCE_TIME_SECONDS;
  entity->shader_time.value = 0;
  entity->flags_kind = qa_game_family_name(source->family);
  entity->flags_bits = 0;
  entity->lighting_origin = character.origin;
  entity->shadow_plane = 0;
  entity->attachments = NULL; entity->attachment_count = 0;
  *out = pass;
  return 1;
}
