/* Port of src/app/bootstrap/native-held-weapon.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/native_held_weapon.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/content/model_attachment.h"
#include "qa/content/q2/foundation/weapon_attachments.h"
#include "qa/content/q3/foundation/held_weapons.h"
#include "qa/foundation/support.h"
#include "qa/render/scene/models/attachment.h"
#include "qa/render/scene/models/transform.h"

/* One settled TS Promise<Grip | null>. */
typedef struct grip_entry {
  char *key;
  bool rejected; qa_caught_error error; /* the cached rejection */
  bool present;                         /* false is TS null */
  qa_model_grip grip;
  bool owns_definition; qa_model_attachment_definition definition;
} grip_entry;
struct qa_app_native_held_weapons {
  qa_app_native_held_assets assets;
  grip_entry **grips; size_t count, capacity;
};

static qa_model_replacement_policy application_policy(void *ctx) {
  return qa_app_application_assets_model_policy(ctx);
}
static qa_content_mounted *application_mounts(void *ctx, qa_content_id content) {
  return qa_app_application_assets_provider(ctx, content).mounts;
}
qa_app_native_held_assets qa_app_native_held_assets_application(qa_app_application_assets *assets) {
  qa_app_native_held_assets out = { assets, application_policy, application_mounts };
  return out;
}

qa_app_native_held_weapons *qa_app_native_held_weapons_create(qa_app_native_held_assets assets) {
  qa_app_native_held_weapons *weapons = calloc(1, sizeof *weapons);
  if (weapons == NULL) return NULL;
  weapons->assets = assets;
  return weapons;
}
void qa_app_native_held_weapons_destroy(qa_app_native_held_weapons *weapons) {
  if (weapons == NULL) return;
  for (size_t i = 0; i < weapons->count; i++) {
    grip_entry *entry = weapons->grips[i];
    if (entry->present) qa_model_grip_destroy(&entry->grip);
    if (entry->owns_definition) qa_content_model_attachment_free(&entry->definition);
    free(entry->key); free(entry);
  }
  free(weapons->grips);
  free(weapons);
}

typedef struct grip_job {
  qa_app_native_held_weapons *weapons;
  qa_content_id content;
  const qa_scene_entity *entity;
  grip_entry *entry;
  uint8_t *bytes;
} grip_job;
static void grip_run(void *ctx) {
  grip_job *job = ctx;
  size_t length = strlen(job->entity->resource.requested_path) + sizeof ".attachment.json";
  char *path = malloc(length);
  if (path == NULL) qa_range_error("Out of memory");
  snprintf(path, length, "%s.attachment.json", job->entity->resource.requested_path);
  qa_content_mounted *mounts = job->weapons->assets.mounts(job->weapons->assets.ctx, job->content);
  qa_content_opened_resource file;
  bool opened = qa_content_mounted_open(mounts, path, NULL, NULL, &file);
  free(path);
  const qa_model_attachment_definition *definition;
  if (!opened) definition = qa_q2_weapon_attachment(job->entity->resource.digest);
  else {
    job->bytes = file.bytes;
    if (!qa_content_read_model_attachment(file.bytes, file.byte_length, &job->entry->definition)) qa_range_error("Out of memory");
    job->entry->owns_definition = true;
    free(job->bytes); job->bytes = NULL;
    definition = &job->entry->definition;
  }
  if (definition == NULL) return;
  if (!qa_create_model_grip(job->entity, *definition, &job->entry->grip)) qa_range_error("Out of memory");
  job->entry->present = true;
}

/* TS private grip(content, entity). */
static const grip_entry *grip_for(qa_app_native_held_weapons *weapons, qa_content_id content, const qa_scene_entity *entity) {
  size_t length = strlen(content) + strlen(entity->resource.id) + 2;
  char *key = malloc(length);
  if (key == NULL) qa_range_error("Out of memory");
  snprintf(key, length, "%s/%s", content, entity->resource.id);
  for (size_t i = 0; i < weapons->count; i++)
    if (strcmp(weapons->grips[i]->key, key) == 0) {
      free(key);
      if (weapons->grips[i]->rejected) qa_rethrow(&weapons->grips[i]->error);
      return weapons->grips[i];
    }
  grip_entry *entry = calloc(1, sizeof *entry);
  if (entry == NULL) qa_range_error("Out of memory");
  entry->key = key;
  if (weapons->count == weapons->capacity) {
    size_t capacity = weapons->capacity == 0 ? 8 : weapons->capacity * 2;
    grip_entry **next = realloc(weapons->grips, capacity * sizeof *next);
    if (next == NULL) qa_range_error("Out of memory");
    weapons->grips = next; weapons->capacity = capacity;
  }
  /* TS this.grips.set(key, pending) before the promise settles. */
  weapons->grips[weapons->count++] = entry;
  grip_job job = { weapons, content, entity, entry, NULL };
  qa_caught_error caught;
  if (!qa_try(grip_run, &job, &caught)) {
    free(job.bytes);
    if (entry->owns_definition) { qa_content_model_attachment_free(&entry->definition); entry->owns_definition = false; }
    entry->rejected = true;
    entry->error = caught;
    qa_rethrow(&caught);
  }
  return entry;
}

bool qa_app_native_held_weapons_attachment(qa_app_native_held_weapons *weapons, qa_content_id content,
    const qa_scene_entity *carrier, qa_app_native_held_attachment *out) {
  const grip_entry *original = grip_for(weapons, content, carrier);
  qa_scene_entity replacement;
  bool has_replacement = qa_replacement_entity(carrier, &replacement);
  if (!original->present) return false;
  const grip_entry *enhanced = has_replacement ? grip_for(weapons, content, &replacement) : NULL;
  out->weapons = weapons;
  out->carrier = *carrier;
  out->original = &original->grip;
  out->enhanced = enhanced != NULL && enhanced->present ? &enhanced->grip : NULL;
  return true;
}

struct qa_app_resolved_held_entity {
  qa_app_native_held_attachment attachment;
  qa_scene_entity held;
  /* The inherited tree: every entity and attachment array is owned here. */
  void **owned; size_t owned_count, owned_capacity;
};
static void *own(qa_app_resolved_held_entity *resolved, size_t size) {
  void *value = calloc(1, size == 0 ? 1 : size);
  if (value == NULL) qa_range_error("Out of memory");
  if (resolved->owned_count == resolved->owned_capacity) {
    size_t capacity = resolved->owned_capacity == 0 ? 8 : resolved->owned_capacity * 2;
    void **next = realloc(resolved->owned, capacity * sizeof *next);
    if (next == NULL) qa_range_error("Out of memory");
    resolved->owned = next; resolved->owned_capacity = capacity;
  }
  resolved->owned[resolved->owned_count++] = value;
  return value;
}
/* TS inherit(entity). */
static qa_scene_entity inherit(qa_app_resolved_held_entity *resolved, const qa_scene_entity *entity) {
  const qa_scene_entity *carrier = &resolved->attachment.carrier;
  qa_scene_entity out = *entity;
  out.flags_kind = carrier->flags_kind;
  out.flags_bits = carrier->flags_bits;
  out.has_opacity = true;
  out.opacity = carrier->has_opacity ? carrier->opacity : 1;
  out.lighting_origin = carrier->lighting_origin;
  out.shadow_plane = carrier->shadow_plane;
  if (entity->attachment_count > 0) {
    qa_scene_entity_attachment *attachments = own(resolved, entity->attachment_count * sizeof *attachments);
    for (size_t i = 0; i < entity->attachment_count; i++) {
      qa_scene_entity *child = own(resolved, sizeof *child);
      *child = inherit(resolved, entity->attachments[i].entity);
      attachments[i].tag = entity->attachments[i].tag;
      attachments[i].entity = child;
    }
    out.attachments = attachments;
  } else out.attachments = NULL;
  return out;
}
qa_app_resolved_held_entity *qa_app_native_held_attachment_bind(const qa_app_native_held_attachment *attachment,
                                                                const qa_scene_entity *child) {
  qa_app_resolved_held_entity *resolved = calloc(1, sizeof *resolved);
  if (resolved == NULL) return NULL;
  resolved->attachment = *attachment;
  resolved->held = inherit(resolved, child);
  return resolved;
}
void qa_app_resolved_held_entity_destroy(qa_app_resolved_held_entity *resolved) {
  if (resolved == NULL) return;
  for (size_t i = 0; i < resolved->owned_count; i++) free(resolved->owned[i]);
  free(resolved->owned);
  free(resolved);
}
bool qa_app_resolved_held_entity_resolve(const qa_app_resolved_held_entity *resolved, qa_vec3 camera,
                                         const char *purpose, qa_scene_entity *out) {
  const qa_app_native_held_attachment *attachment = &resolved->attachment;
  qa_app_native_held_weapons *weapons = attachment->weapons;
  qa_scene_entity selected = qa_select_model_entity(&attachment->carrier, camera,
      weapons->assets.model_policy(weapons->assets.ctx), purpose);
  const qa_model_grip *sample = strcmp(selected.resource.id, attachment->carrier.resource.id) == 0
      ? attachment->original : attachment->enhanced;
  if (sample == NULL) qa_raise("Error", 0, 0, "Native attachment replacement changed after preparation");
  qa_model_transform grip;
  if (!qa_model_grip_transform(sample, &selected, &grip)) return false;
  qa_model_transform hand = qa_compose_model_transform(selected.transform, grip);
  qa_model_transform transform = qa_compose_model_transform(qa_align_model_attachment(qa_q3_weapon_hand_grip, hand),
                                                            resolved->held.transform);
  *out = resolved->held;
  out->transform = transform;
  out->previous_origin = transform.origin;
  return true;
}
