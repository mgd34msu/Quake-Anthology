/* Port of src/app/bootstrap/q3-selected-weapon.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/q3_selected_weapon.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/content/q3/foundation/animation.h"
#include "qa/content/q3/foundation/weapon_pose.h"
#include "qa/content/q3/presentation/ref_entity.h"
#include "qa/core/game_numeric.h"
#include "qa/core/math.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/movement/q3/constants.h"
#include "qa/render/scene/models/transform.h"

typedef struct animation_entry { char *content; qa_q3_player_animation_config config; } animation_entry;
/* One settled TS Promise<ModelAsset | null>: present false is TS null. */
typedef struct model_entry { char *key; bool present; qa_app_model_asset asset; } model_entry;
struct qa_app_selected_q3_weapon_presenter {
  qa_q3_lerp_frame torso;
  qa_q3_weapon_barrel barrel, world_barrel;
  qa_game_random random;
  qa_app_application_assets *assets;
  const qa_q3_player_animation_config *character_animation;
  animation_entry **animations; size_t animation_count, animation_capacity;
  model_entry **models; size_t model_count, model_capacity;
  /* world() scratch: gun, barrel, flash; passes. */
  qa_scene_entity world_entities[3];
  qa_scene_entity_attachment world_attachments[1];
  qa_q3_character_pass world_passes[4];
  /* frame() scratch: the gun's attachments and their entities. */
  qa_scene_entity *frame_entities; qa_scene_entity_attachment *frame_attachments; size_t frame_capacity;
};

static const qa_vec3 ZERO = {0, 0, 0};
static const qa_vec3 UNIT = {1, 1, 1};

static char *copy_text(const char *text) {
  size_t length = strlen(text) + 1;
  char *out = malloc(length);
  if (out == NULL) qa_range_error("Out of memory");
  memcpy(out, text, length);
  return out;
}
static char *join_text(const char *a, const char *b, const char *c) {
  size_t length = strlen(a) + strlen(b) + strlen(c) + 1;
  char *out = malloc(length);
  if (out == NULL) qa_range_error("Out of memory");
  snprintf(out, length, "%s%s%s", a, b, c);
  return out;
}
/* TS source.path.replace(/\.[^.]+$/, ""). */
static char *path_stem(const char *path) {
  const char *dot = strrchr(path, '.');
  size_t length = dot != NULL && dot[1] != '\0' ? (size_t)(dot - path) : strlen(path);
  char *out = malloc(length + 1);
  if (out == NULL) qa_range_error("Out of memory");
  memcpy(out, path, length);
  out[length] = '\0';
  return out;
}

qa_app_selected_q3_weapon_presenter *qa_app_selected_q3_weapon_presenter_create(
    qa_app_application_assets *assets, const qa_q3_player_animation_config *character_animation) {
  qa_app_selected_q3_weapon_presenter *presenter = calloc(1, sizeof *presenter);
  if (presenter == NULL) return NULL;
  presenter->torso = qa_q3_create_lerp_frame();
  qa_q3_weapon_barrel_init(&presenter->barrel);
  qa_q3_weapon_barrel_init(&presenter->world_barrel);
  qa_game_random_init(&presenter->random, 0);
  presenter->assets = assets;
  presenter->character_animation = character_animation;
  return presenter;
}
void qa_app_selected_q3_weapon_presenter_destroy(qa_app_selected_q3_weapon_presenter *presenter) {
  if (presenter == NULL) return;
  for (size_t i = 0; i < presenter->animation_count; i++) {
    qa_q3_player_animation_config_destroy(&presenter->animations[i]->config);
    free(presenter->animations[i]->content); free(presenter->animations[i]);
  }
  free(presenter->animations);
  for (size_t i = 0; i < presenter->model_count; i++) { free(presenter->models[i]->key); free(presenter->models[i]); }
  free(presenter->models);
  free(presenter->frame_entities); free(presenter->frame_attachments);
  free(presenter);
}

/* TS (await provider(content)).mounts.open(path) !== null. */
static bool resource_present(qa_app_application_assets *assets, qa_content_id content, const char *path) {
  qa_content_mounted *mounts = qa_app_application_assets_provider(assets, content).mounts;
  qa_content_opened_resource opened;
  if (!qa_content_mounted_open(mounts, path, NULL, NULL, &opened)) return false;
  free(opened.bytes);
  return true;
}

/* TS private animation(content). */
static const qa_q3_player_animation_config *animation_for(qa_app_selected_q3_weapon_presenter *presenter, qa_content_id content) {
  if (presenter->character_animation != NULL) return presenter->character_animation;
  for (size_t i = 0; i < presenter->animation_count; i++)
    if (strcmp(presenter->animations[i]->content, content) == 0) return &presenter->animations[i]->config;
  const char *path = "models/players/sarge/animation.cfg";
  qa_content_mounted *mounts = qa_app_application_assets_provider(presenter->assets, content).mounts;
  qa_content_opened_resource resource;
  if (!qa_content_mounted_open(mounts, path, NULL, NULL, &resource))
    qa_raise("Error", 0, 0, "Selected Q3 weapon animation configuration is missing");
  char *text = malloc(resource.byte_length + 1);
  if (text == NULL) qa_range_error("Out of memory");
  memcpy(text, resource.bytes, resource.byte_length);
  text[resource.byte_length] = '\0';
  free(resource.bytes);
  animation_entry *entry = calloc(1, sizeof *entry);
  if (entry == NULL) qa_range_error("Out of memory");
  qa_cleanup text_guard, entry_guard;
  qa_cleanup_push(&text_guard, free, text);
  qa_cleanup_push(&entry_guard, free, entry);
  qa_q3_parse_player_animation_config(text, path, NULL, &entry->config);
  qa_cleanup_pop(&entry_guard);
  qa_cleanup_pop(&text_guard);
  free(text);
  entry->content = copy_text(content);
  if (presenter->animation_count == presenter->animation_capacity) {
    size_t capacity = presenter->animation_capacity == 0 ? 4 : presenter->animation_capacity * 2;
    animation_entry **next = realloc(presenter->animations, capacity * sizeof *next);
    if (next == NULL) qa_range_error("Out of memory");
    presenter->animations = next; presenter->animation_capacity = capacity;
  }
  presenter->animations[presenter->animation_count++] = entry;
  return &entry->config;
}

/* TS private model(content, path): NULL is TS null. */
static const qa_app_model_asset *model_for(qa_app_selected_q3_weapon_presenter *presenter, qa_content_id content, const char *path) {
  char *key = join_text(content, "/", path);
  for (size_t i = 0; i < presenter->model_count; i++)
    if (strcmp(presenter->models[i]->key, key) == 0) {
      free(key);
      return presenter->models[i]->present ? &presenter->models[i]->asset : NULL;
    }
  qa_cleanup key_guard;
  qa_cleanup_push(&key_guard, free, key);
  model_entry *entry = calloc(1, sizeof *entry);
  if (entry == NULL) qa_range_error("Out of memory");
  qa_cleanup entry_guard;
  qa_cleanup_push(&entry_guard, free, entry);
  if (resource_present(presenter->assets, content, path)) {
    entry->asset = qa_app_application_assets_model(presenter->assets, content, path);
    entry->present = true;
  }
  qa_cleanup_pop(&entry_guard);
  qa_cleanup_pop(&key_guard);
  entry->key = key;
  if (presenter->model_count == presenter->model_capacity) {
    size_t capacity = presenter->model_capacity == 0 ? 8 : presenter->model_capacity * 2;
    model_entry **next = realloc(presenter->models, capacity * sizeof *next);
    if (next == NULL) qa_range_error("Out of memory");
    presenter->models = next; presenter->model_capacity = capacity;
  }
  presenter->models[presenter->model_count++] = entry;
  return entry->present ? &entry->asset : NULL;
}

static bool flash_visible(const qa_app_simulation_presentation *source) {
  bool continuous = source->q3_weapon_firing
      && (source->q3_weapon_weapon == 1 || source->q3_weapon_weapon == 6 || source->q3_weapon_weapon == 10);
  return continuous || (source->has_q3_weapon_last_fire && source->q3_weapon_time_ms - source->q3_weapon_last_fire_ms <= 20);
}

/* TS options: () => shader === null ? {} : { customShader: shader }; ctx is the shader text. */
static qa_model_source_options shader_options(void *ctx, const qa_scene_entity *entity) {
  (void)entity;
  qa_model_source_options options;
  memset(&options, 0, sizeof options);
  options.custom_shader = ctx;
  return options;
}

/* TS world part(asset, angles). */
static qa_scene_entity world_part(const qa_app_simulation_presentation *source, const qa_app_model_asset *asset,
                                  qa_vec3 angles, qa_vec3 lighting_origin, bool personal_model) {
  qa_scene_entity entity;
  memset(&entity, 0, sizeof entity);
  entity.actor = &source->actor;
  entity.resource = asset->resource;
  entity.model = asset->model;
  entity.transform.origin = ZERO;
  entity.transform.axis = qa_qvm_angles_to_axis(angles);
  entity.transform.scale = UNIT;
  entity.previous_origin = ZERO;
  entity.pose.is_frame = true;
  entity.color = (qa_vec4){1, 1, 1, 1};
  entity.shader_time.kind = QA_SOURCE_TIME_SECONDS;
  entity.shader_time.value = 0;
  entity.flags_kind = "q3";
  entity.flags_bits = QA_Q3_PRES_RF_LIGHTING_ORIGIN | (personal_model ? QA_Q3_PRES_RF_THIRD_PERSON : 0);
  entity.lighting_origin = lighting_origin;
  entity.shadow_plane = 0;
  return entity;
}

const qa_q3_character_pass *qa_app_selected_q3_weapon_presenter_world(qa_app_selected_q3_weapon_presenter *presenter,
    const qa_app_simulation_presentation *source, qa_vec3 character_origin, double character_powerups,
    bool personal_model, size_t *out_count) {
  *out_count = 0;
  if (!source->has_q3_weapon || !source->visible || source->path[0] == '\0') return presenter->world_passes;
  char *stem = path_stem(source->path);
  char *barrel_path = join_text(stem, "_barrel.md3", ""), *flash_path = join_text(stem, "_flash.md3", "");
  free(stem);
  qa_cleanup barrel_guard, flash_guard;
  qa_cleanup_push(&barrel_guard, free, barrel_path);
  qa_cleanup_push(&flash_guard, free, flash_path);
  qa_app_model_asset gun_asset = qa_app_application_assets_model(presenter->assets, source->content, source->path);
  const qa_app_model_asset *barrel_asset = model_for(presenter, source->content, barrel_path);
  const qa_app_model_asset *flash_asset = model_for(presenter, source->content, flash_path);
  qa_cleanup_pop(&flash_guard);
  qa_cleanup_pop(&barrel_guard);
  free(barrel_path); free(flash_path);
  qa_q3_weapon_barrel_step_result spin = qa_q3_weapon_barrel_step(&presenter->world_barrel, source->q3_weapon_time_ms,
                                                                   source->q3_weapon_firing);
  qa_scene_entity *gun = &presenter->world_entities[0];
  *gun = world_part(source, &gun_asset, ZERO, character_origin, personal_model);
  if (barrel_asset != NULL) {
    presenter->world_entities[1] = world_part(source, barrel_asset, (qa_vec3){0, 0, spin.angle}, character_origin, personal_model);
    presenter->world_attachments[0].tag = "tag_barrel";
    presenter->world_attachments[0].entity = &presenter->world_entities[1];
    gun->attachments = presenter->world_attachments;
    gun->attachment_count = 1;
  }
  size_t count = 0;
  int32_t powerups = qa_js_to_int32(character_powerups);
  bool invisible = (powerups & (1 << QA_Q3_PW_INVIS)) != 0;
  const char *shaders[3]; size_t shader_count = 0;
  shaders[shader_count++] = invisible ? "powerups/invisibility" : NULL;
  if (!invisible) {
    if (powerups & (1 << QA_Q3_PW_BATTLESUIT)) shaders[shader_count++] = "powerups/battleWeapon";
    if (powerups & (1 << QA_Q3_PW_QUAD)) shaders[shader_count++] = "powerups/quadWeapon";
  }
  for (size_t i = 0; i < shader_count; i++) {
    qa_q3_character_pass *pass = &presenter->world_passes[count++];
    pass->content = source->content; pass->entity = *gun; pass->shader = shaders[i];
    pass->options = shader_options; pass->options_ctx = (void *)shaders[i];
  }
  if (flash_asset != NULL && flash_visible(source)) {
    qa_model_attachment_tag_result tag;
    if (qa_model_attachment_tag(gun, "tag_flash", &tag)) {
      double roll = (float)(qa_game_random_crandom(&presenter->random) * 10);
      qa_scene_entity flash = world_part(source, flash_asset, (qa_vec3){0, 0, roll}, character_origin, personal_model);
      presenter->world_entities[2] = qa_attach_scene_entity(gun, &flash, tag);
      qa_q3_character_pass *pass = &presenter->world_passes[count++];
      pass->content = source->content; pass->entity = presenter->world_entities[2]; pass->shader = NULL;
      pass->options = shader_options; pass->options_ctx = NULL;
    }
  }
  *out_count = count;
  return presenter->world_passes;
}

/* TS frame part(asset, origin, angles). */
static qa_scene_entity view_part(const qa_app_simulation_presentation *source, const qa_app_model_asset *asset,
                                 qa_vec3 origin, qa_vec3 angles) {
  qa_scene_entity entity;
  memset(&entity, 0, sizeof entity);
  entity.actor = &source->actor;
  entity.resource = asset->resource;
  entity.model = asset->model;
  entity.transform.origin = origin;
  entity.transform.axis = qa_qvm_angles_to_axis(angles);
  entity.transform.scale = UNIT;
  entity.previous_origin = origin;
  entity.pose.is_frame = true;
  entity.color = (qa_vec4){1, 1, 1, 1};
  entity.shader_time.kind = QA_SOURCE_TIME_MILLISECONDS;
  entity.shader_time.value = 0;
  entity.flags_kind = "q3";
  entity.flags_bits = QA_Q3_PRES_RF_MINLIGHT | QA_Q3_PRES_RF_FIRST_PERSON | QA_Q3_PRES_RF_DEPTHHACK;
  entity.lighting_origin = source->origin;
  entity.shadow_plane = 0;
  return entity;
}

bool qa_app_selected_q3_weapon_presenter_frame(qa_app_selected_q3_weapon_presenter *presenter,
    const qa_app_simulation_presentation *source, double field_of_view, qa_scene_entity *out) {
  if (!source->has_q3_weapon) qa_raise("Error", 0, 0, "Selected Q3 weapon presentation state is missing");
  const qa_qvm_grapple_view_anchor *anchor = source->model_anchor;
  char *stem = path_stem(source->path);
  char *hand_path = join_text(stem, "_hand.md3", ""), *barrel_path = join_text(stem, "_barrel.md3", ""),
       *flash_path = join_text(stem, "_flash.md3", "");
  free(stem);
  qa_cleanup hand_guard, barrel_guard, flash_guard;
  qa_cleanup_push(&hand_guard, free, hand_path);
  qa_cleanup_push(&barrel_guard, free, barrel_path);
  qa_cleanup_push(&flash_guard, free, flash_path);
  qa_app_model_asset gun_asset = qa_app_application_assets_model(presenter->assets, source->content, source->path);
  const qa_app_model_asset *own_hand = model_for(presenter, source->content, anchor != NULL ? anchor->path : hand_path);
  const qa_app_model_asset *barrel_asset = anchor == NULL ? model_for(presenter, source->content, barrel_path) : NULL;
  const qa_app_model_asset *flash_asset = anchor == NULL ? model_for(presenter, source->content, flash_path) : NULL;
  const qa_q3_player_animation_config *animation = anchor == NULL ? animation_for(presenter, source->content) : NULL;
  qa_cleanup_pop(&flash_guard);
  qa_cleanup_pop(&barrel_guard);
  qa_cleanup_pop(&hand_guard);
  free(hand_path); free(barrel_path); free(flash_path);
  qa_app_model_asset hand_fallback;
  const qa_app_model_asset *hand_asset = own_hand;
  if (hand_asset == NULL) {
    hand_fallback = qa_app_application_assets_model(presenter->assets, source->content, "models/weapons2/shotgun/shotgun_hand.md3");
    hand_asset = &hand_fallback;
  }
  if (animation != NULL) {
    qa_q3_run_lerp_frame_input input = { source->q3_weapon_time_ms, source->q3_weapon_torso_animation, 1, false };
    qa_q3_run_lerp_frame(qa_q3_animation_set_from_config(animation), &presenter->torso, input, NULL, NULL);
  }
  int32_t bob = qa_js_to_int32(source->q3_weapon_bob_cycle);
  qa_q3_weapon_view_motion motion = { source->origin, source->angles, source->q3_weapon_time_ms,
      source->q3_weapon_horizontal_speed, (double)((bob & 128) >> 7),
      fabs(sin((double)(bob & 127) / 127 * 3.141592653589793)), 0, 0 };
  qa_q3_weapon_view_pose_result position = qa_q3_weapon_view_pose(motion);
  qa_axis axis = qa_qvm_angles_to_axis(source->angles);
  qa_vec3 offset = anchor != NULL ? anchor->offset : ZERO;
  double fov_offset = anchor == NULL ? 0
      : (float)((double)(float)anchor->fov_offset_scale * (double)(float)qa_js_max(0, field_of_view - anchor->fov_offset_above));
  qa_vec3 origin = qa_add3(position.origin, qa_add3(qa_add3(qa_scale3(axis.vectors[0], offset.x), qa_scale3(axis.vectors[1], offset.y)),
                                                    qa_scale3(axis.vectors[2], (float)(offset.z + fov_offset))));
  qa_scene_entity hand = view_part(source, hand_asset, origin, position.angles);
  hand.pose.is_frame = true;
  hand.pose.frame = animation == NULL ? source->frame : qa_q3_torso_weapon_frame(animation, presenter->torso.frame);
  hand.pose.previous_frame = animation == NULL ? source->old_frame : qa_q3_torso_weapon_frame(animation, presenter->torso.old_frame);
  hand.pose.back_lerp = animation == NULL ? (source->has_back_lerp ? source->back_lerp : 0) : presenter->torso.back_lerp;
  const char *tag_name = anchor != NULL ? anchor->tag : "tag_weapon";
  qa_model_attachment_tag_result tag;
  if (!qa_model_attachment_tag(&hand, tag_name, &tag)) {
    char *key = join_text("q3-hands-tag:", source->content, "/");
    char *full_key = join_text(key, source->path, "");
    free(key);
    char *text = join_text("Selected Q3 hands model has no ", tag_name, "; the weapon is not drawn.\n");
    qa_app_application_assets_warn_once(presenter->assets, full_key, text);
    free(full_key); free(text);
    return false;
  }
  size_t needed = source->model_attachment_count + 2;
  if (needed > presenter->frame_capacity) {
    qa_scene_entity *entities = realloc(presenter->frame_entities, needed * sizeof *entities);
    if (entities == NULL) qa_range_error("Out of memory");
    presenter->frame_entities = entities;
    qa_scene_entity_attachment *attachments = realloc(presenter->frame_attachments, needed * sizeof *attachments);
    if (attachments == NULL) qa_range_error("Out of memory");
    presenter->frame_attachments = attachments;
    presenter->frame_capacity = needed;
  }
  size_t count = 0;
  for (size_t i = 0; i < source->model_attachment_count; i++) {
    qa_app_model_asset asset = qa_app_application_assets_model(presenter->assets, source->content, source->model_attachments[i].path);
    presenter->frame_entities[count] = view_part(source, &asset, ZERO, ZERO);
    presenter->frame_attachments[count].tag = source->model_attachments[i].tag;
    presenter->frame_attachments[count].entity = &presenter->frame_entities[count];
    count++;
  }
  qa_q3_weapon_barrel_step_result spin = qa_q3_weapon_barrel_step(&presenter->barrel, source->q3_weapon_time_ms, source->q3_weapon_firing);
  if (barrel_asset != NULL) {
    presenter->frame_entities[count] = view_part(source, barrel_asset, ZERO, (qa_vec3){0, 0, spin.angle});
    presenter->frame_attachments[count].tag = "tag_barrel";
    presenter->frame_attachments[count].entity = &presenter->frame_entities[count];
    count++;
  }
  if (flash_asset != NULL && flash_visible(source)) {
    double roll = (float)(qa_game_random_crandom(&presenter->random) * 10);
    presenter->frame_entities[count] = view_part(source, flash_asset, ZERO, (qa_vec3){0, 0, roll});
    presenter->frame_attachments[count].tag = "tag_flash";
    presenter->frame_attachments[count].entity = &presenter->frame_entities[count];
    count++;
  }
  qa_scene_entity gun = view_part(source, &gun_asset, ZERO, ZERO);
  gun.attachments = count == 0 ? NULL : presenter->frame_attachments;
  gun.attachment_count = count;
  *out = qa_attach_scene_entity(&hand, &gun, tag);
  return true;
}
