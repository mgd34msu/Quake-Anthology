/* Port of src/app/bootstrap/native-held-weapon.ts. Grip sampling
 * and model-replacement selection are landed render/scene services
 * behind the host below; this file owns grip caching and the held
 * entity transform composition inputs. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_NATIVE_HELD_WEAPON_H
#define QA_APP_BOOTSTRAP_NATIVE_HELD_WEAPON_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/content.h"
#include "qa/contracts/math.h"
#include "qa/contracts/scene.h"
/* TS Grip: sampled model transform for an entity, NULL transform
 * (has_transform false) is the TS null. */
typedef struct qa_app_held_grip_sample {
  bool has_transform;
  qa_model_transform transform;
} qa_app_held_grip_sample;
typedef struct qa_app_native_held_assets {
  void *ctx;
  /* mounts.open for a content provider: malloc'd bytes or NULL. */
  uint8_t *(*open)(void *ctx, qa_content_id content, const char *path, size_t *out_length);
  /* createModelGrip(entity, definition) as an opaque grip handle. */
  void *(*create_grip)(void *ctx, const qa_scene_entity *entity, const uint8_t *definition, size_t length);
  qa_app_held_grip_sample (*sample_grip)(void *ctx, void *grip, const qa_scene_entity *entity);
  void (*destroy_grip)(void *ctx, void *grip);
  /* replacementEntity / selectModelEntity with the model policy. */
  const qa_scene_entity *(*replacement_entity)(void *ctx, const qa_scene_entity *entity);
  qa_scene_entity (*select_model_entity)(void *ctx, const qa_scene_entity *carrier,
      qa_vec3 camera, const char *purpose);
  /* alignModelAttachment(Q3_WEAPON_HAND_GRIP, hand). */
  qa_model_transform (*align_hand_grip)(void *ctx, qa_model_transform hand);
} qa_app_native_held_assets;
typedef struct qa_app_native_held_weapons qa_app_native_held_weapons;
qa_app_native_held_weapons *qa_app_native_held_weapons_create(qa_app_native_held_assets assets);
void qa_app_native_held_weapons_destroy(qa_app_native_held_weapons *weapons);
/* TS attachment(): prepares original/replacement grips; the result
 * resolves a held child per camera. Aborts when no attachment
 * definition exists (TS Error). */
typedef struct qa_app_held_attachment qa_app_held_attachment;
qa_app_held_attachment *qa_app_native_held_weapons_attachment(qa_app_native_held_weapons *weapons,
    qa_content_id content, const qa_scene_entity *carrier, const qa_scene_entity *child);
void qa_app_held_attachment_destroy(qa_app_held_attachment *attachment);
/* TS ResolvedHeldEntity: false = TS null. purpose "view"|"shadow". */
bool qa_app_held_attachment_resolve(qa_app_held_attachment *attachment, qa_vec3 camera,
    const char *purpose, qa_scene_entity *out, qa_scene_entity *scratch, size_t scratch_count);
#endif
