/* Port of src/app/bootstrap/q3-selected-weapon.ts. Async model
 * loads become synchronous host calls. SimulationPresentation
 * (simulation/types.ts) rides the view below. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_Q3_SELECTED_WEAPON_H
#define QA_APP_BOOTSTRAP_Q3_SELECTED_WEAPON_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/content/q3/foundation/animation.h"
#include "qa/content/q3/foundation/presentation.h"
#include "qa/contracts/content.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/scene.h"
/* TS ModelAsset (assets.ts). */
typedef struct qa_app_model_asset {
  qa_resolved_resource_reference resource;
  qa_decoded_model model;
} qa_app_model_asset;
/* TS SimulationPresentation fields read here. */
typedef struct qa_app_q3_weapon_view {
  double time_milliseconds;
  bool firing;
  double weapon;
  bool has_last_fire;
  double last_fire_milliseconds;
  double horizontal_speed;
  double bob_cycle;
  double torso_animation;
} qa_app_q3_weapon_view;
typedef struct qa_app_q3_model_anchor {
  const char *path; /* borrowed */
  const char *tag;  /* NULL = TS undefined */
  qa_vec3 offset;
  bool has_fov_offset;
  double fov_scale, fov_above;
} qa_app_q3_model_anchor;
typedef struct qa_app_q3_model_attachment { const char *tag; const char *path; } qa_app_q3_model_attachment;
typedef struct qa_app_q3_weapon_source {
  const qa_actor_id *actor;      /* nullable */
  qa_content_id content;
  const char *path;              /* borrowed */
  bool visible;
  bool has_weapon;
  qa_app_q3_weapon_view weapon;
  qa_vec3 origin;
  qa_vec3 angles;
  double frame, old_frame;
  bool has_back_lerp; double back_lerp;
  const qa_app_q3_model_anchor *model_anchor; /* NULL = TS undefined */
  const qa_app_q3_model_attachment *model_attachments; size_t model_attachment_count;
} qa_app_q3_weapon_source;
typedef struct qa_app_q3_weapon_character { qa_vec3 origin; double powerups; } qa_app_q3_weapon_character;
typedef struct qa_app_q3_weapon_assets {
  void *ctx;
  /* assets.model: aborts/fails via *error_out (NULL return = error). */
  const qa_app_model_asset *(*model)(void *ctx, qa_content_id content, const char *path, const char **error_out);
  /* mounts.open: malloc'd bytes or NULL. */
  uint8_t *(*open)(void *ctx, qa_content_id content, const char *path, size_t *out_length);
  void (*print)(void *ctx, const char *message);
} qa_app_q3_weapon_assets;
typedef struct qa_app_selected_q3_weapon_presenter qa_app_selected_q3_weapon_presenter;
/* character_animation NULL = TS null. */
qa_app_selected_q3_weapon_presenter *qa_app_selected_q3_weapon_presenter_create(
    qa_app_q3_weapon_assets assets, const qa_q3_player_animation_config *character_animation);
void qa_app_selected_q3_weapon_presenter_destroy(qa_app_selected_q3_weapon_presenter *presenter);
/* World passes: malloc'd array of caller-provided pass structs and
 * backing entities (free both arrays; entity scratch count = pass
 * count + attachments). Aborts with the TS Error text on load
 * failure. */
typedef struct qa_app_q3_weapon_passes {
  qa_q3_character_pass *items; size_t count;
  qa_scene_entity *entities; size_t entity_count;
  qa_scene_entity_attachment *attachments; size_t attachment_count;
} qa_app_q3_weapon_passes;
void qa_app_q3_weapon_passes_destroy(qa_app_q3_weapon_passes *passes);
qa_app_q3_weapon_passes qa_app_selected_q3_weapon_presenter_world(qa_app_selected_q3_weapon_presenter *presenter,
    qa_app_q3_weapon_source source, qa_app_q3_weapon_character character, bool personal_model);
/* First-person frame entity. field_of_view default 90 when NAN.
 * Attachments are written into caller scratch (entities + links). */
qa_scene_entity qa_app_selected_q3_weapon_presenter_frame(qa_app_selected_q3_weapon_presenter *presenter,
    qa_app_q3_weapon_source source, double field_of_view,
    qa_scene_entity *entity_scratch, size_t entity_capacity,
    qa_scene_entity_attachment *attachment_scratch, size_t attachment_capacity);
#endif
