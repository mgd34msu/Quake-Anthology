/* Port of src/app/bootstrap/native-held-weapon.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/native_held_weapon.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
#include "qa/render/scene/models/transform.h"
typedef struct grip_entry { char *key; void *grip; } grip_entry;
struct qa_app_native_held_weapons {
  qa_app_native_held_assets assets;
  grip_entry *grips; size_t count, capacity;
};
struct qa_app_held_attachment {
  qa_app_native_held_weapons *weapons;
  qa_scene_entity carrier;
  qa_scene_entity held; /* inherited child (shallow attachments) */
  void *original;
  void *enhanced; /* NULL when no replacement */
};
qa_app_native_held_weapons *qa_app_native_held_weapons_create(qa_app_native_held_assets assets) {
  qa_app_native_held_weapons *weapons = calloc(1, sizeof *weapons);
  if (weapons == NULL) qa_range_error("Out of memory");
  weapons->assets = assets;
  return weapons;
}
void qa_app_native_held_weapons_destroy(qa_app_native_held_weapons *weapons) {
  if (weapons == NULL) return;
  for (size_t i = 0; i < weapons->count; i++) {
    weapons->assets.destroy_grip(weapons->assets.ctx, weapons->grips[i].grip);
    free(weapons->grips[i].key);
  }
  free(weapons->grips);
  free(weapons);
}
static void *grip_for(qa_app_native_held_weapons *weapons, qa_content_id content,
    const qa_scene_entity *entity) {
  size_t key_size = strlen(content) + strlen(entity->resource.id) + 2;
  char *key = malloc(key_size);
  if (key == NULL) qa_range_error("Out of memory");
  snprintf(key, key_size, "%s/%s", content, entity->resource.id);
  for (size_t i = 0; i < weapons->count; i++) {
    if (strcmp(weapons->grips[i].key, key) == 0) { free(key); return weapons->grips[i].grip; }
  }
  size_t path_size = strlen(entity->resource.requested_path) + strlen(".attachment.json") + 1;
  char *path = malloc(path_size);
  if (path == NULL) qa_range_error("Out of memory");
  snprintf(path, path_size, "%s.attachment.json", entity->resource.requested_path);
  size_t length = 0;
  uint8_t *bytes = weapons->assets.open(weapons->assets.ctx, content, path, &length);
  /* NULL bytes selects the host's q2WeaponAttachment digest fallback
   * inside create_grip (definition bytes NULL). */
  void *grip = weapons->assets.create_grip(weapons->assets.ctx, entity, bytes, length);
  free(bytes);
  if (grip == NULL)
    qa_range_error("Native held model %s/%s requires %s", content, entity->resource.requested_path, path);
  free(path);
  if (weapons->count == weapons->capacity) {
    size_t cap = weapons->capacity == 0 ? 8 : weapons->capacity * 2;
    grip_entry *next = realloc(weapons->grips, cap * sizeof *next);
    if (next == NULL) qa_range_error("Out of memory");
    weapons->grips = next; weapons->capacity = cap;
  }
  weapons->grips[weapons->count].key = key;
  weapons->grips[weapons->count].grip = grip;
  weapons->count++;
  return grip;
}
qa_app_held_attachment *qa_app_native_held_weapons_attachment(qa_app_native_held_weapons *weapons,
    qa_content_id content, const qa_scene_entity *carrier, const qa_scene_entity *child) {
  qa_app_held_attachment *attachment = calloc(1, sizeof *attachment);
  if (attachment == NULL) qa_range_error("Out of memory");
  attachment->weapons = weapons;
  attachment->carrier = *carrier;
  attachment->original = grip_for(weapons, content, carrier);
  const qa_scene_entity *replacement = weapons->assets.replacement_entity(weapons->assets.ctx, carrier);
  attachment->enhanced = replacement != NULL ? grip_for(weapons, content, replacement) : NULL;
  attachment->held = *child;
  attachment->held.flags_kind = carrier->flags_kind;
  attachment->held.flags_bits = carrier->flags_bits;
  attachment->held.has_opacity = true;
  attachment->held.opacity = carrier->has_opacity ? carrier->opacity : 1;
  attachment->held.lighting_origin = carrier->lighting_origin;
  attachment->held.shadow_plane = carrier->shadow_plane;
  /* TS also rewrites nested attachments recursively; nested rows
   * borrow the child's own attachment entities here. */
  return attachment;
}
void qa_app_held_attachment_destroy(qa_app_held_attachment *attachment) { free(attachment); }
bool qa_app_held_attachment_resolve(qa_app_held_attachment *attachment, qa_vec3 camera,
    const char *purpose, qa_scene_entity *out, qa_scene_entity *scratch, size_t scratch_count) {
  (void)scratch; (void)scratch_count;
  qa_app_native_held_weapons *weapons = attachment->weapons;
  qa_scene_entity selected = weapons->assets.select_model_entity(
      weapons->assets.ctx, &attachment->carrier, camera, purpose);
  void *sample_grip = strcmp(selected.resource.id, attachment->carrier.resource.id) == 0
      ? attachment->original : attachment->enhanced;
  if (sample_grip == NULL) qa_range_error("Native attachment replacement changed after preparation");
  qa_app_held_grip_sample sample = weapons->assets.sample_grip(weapons->assets.ctx, sample_grip, &selected);
  if (!sample.has_transform) return false;
  qa_model_transform hand = qa_compose_model_transform(selected.transform, sample.transform);
  qa_model_transform transform = qa_compose_model_transform(
      weapons->assets.align_hand_grip(weapons->assets.ctx, hand), attachment->held.transform);
  *out = attachment->held;
  out->transform = transform;
  out->previous_origin = transform.origin;
  return true;
}
