/* Port of src/app/bootstrap/q3-selected-weapon.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/q3_selected_weapon.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/content/q3/foundation/animation_config.h"
#include "qa/content/q3/foundation/weapon_pose.h"
#include "qa/content/q3/presentation/ref_entity.h"
#include "qa/core/game_numeric.h"
#include "qa/core/math.h"
#include "qa/foundation/support.h"
#include "qa/movement/q3/constants.h"
#include "qa/render/scene/models/transform.h"
typedef struct model_entry { char *key; const qa_app_q3_selected_model_asset *asset; bool resolved; } model_entry;
typedef struct animation_entry { char *content; qa_q3_player_animation_config config; } animation_entry;
struct qa_app_selected_q3_weapon_presenter {
  qa_q3_lerp_frame torso;
  qa_q3_weapon_barrel barrel, world_barrel;
  qa_game_random random;
  qa_app_q3_weapon_assets assets;
  const qa_q3_player_animation_config *character_animation;
  model_entry *models; size_t model_count, model_capacity;
  animation_entry *animations; size_t animation_count, animation_capacity;
};
static char *dup_text(const char *text) {
  size_t len = strlen(text);
  char *out = malloc(len + 1);
  if (out == NULL) qa_range_error("Out of memory");
  memcpy(out, text, len + 1);
  return out;
}
static const qa_vec3 zero3 = {0, 0, 0};
static const qa_vec3 unit3 = {1, 1, 1};
qa_app_selected_q3_weapon_presenter *qa_app_selected_q3_weapon_presenter_create(
    qa_app_q3_weapon_assets assets, const qa_q3_player_animation_config *character_animation) {
  qa_app_selected_q3_weapon_presenter *presenter = calloc(1, sizeof *presenter);
  if (presenter == NULL) qa_range_error("Out of memory");
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
  for (size_t i = 0; i < presenter->model_count; i++) free(presenter->models[i].key);
  free(presenter->models);
  for (size_t i = 0; i < presenter->animation_count; i++) {
    free(presenter->animations[i].content);
    qa_q3_player_animation_config_destroy(&presenter->animations[i].config);
  }
  free(presenter->animations);
  free(presenter);
}
static void host_print(void *ctx, const char *message) {
  qa_app_selected_q3_weapon_presenter *presenter = ctx;
  if (presenter->assets.print != NULL) presenter->assets.print(presenter->assets.ctx, message);
}
static const qa_q3_player_animation_config *animation_for(qa_app_selected_q3_weapon_presenter *presenter,
    qa_content_id content) {
  if (presenter->character_animation != NULL) return presenter->character_animation;
  for (size_t i = 0; i < presenter->animation_count; i++)
    if (strcmp(presenter->animations[i].content, content) == 0) return &presenter->animations[i].config;
  const char *path = "models/players/sarge/animation.cfg";
  size_t length = 0;
  uint8_t *bytes = presenter->assets.open(presenter->assets.ctx, content, path, &length);
  if (bytes == NULL) qa_range_error("Selected Q3 weapon animation configuration is missing");
  char *text = malloc(length + 1);
  if (text == NULL) qa_range_error("Out of memory");
  memcpy(text, bytes, length); text[length] = '\0';
  free(bytes);
  if (presenter->animation_count == presenter->animation_capacity) {
    size_t cap = presenter->animation_capacity == 0 ? 4 : presenter->animation_capacity * 2;
    animation_entry *next = realloc(presenter->animations, cap * sizeof *next);
    if (next == NULL) qa_range_error("Out of memory");
    presenter->animations = next; presenter->animation_capacity = cap;
  }
  animation_entry *entry = &presenter->animations[presenter->animation_count];
  memset(entry, 0, sizeof *entry);
  entry->content = dup_text(content);
  qa_q3_parse_player_animation_config(text, path, NULL, &entry->config);
  free(text);
  presenter->animation_count++;
  return &entry->config;
}
static const qa_app_q3_selected_model_asset *model_for(qa_app_selected_q3_weapon_presenter *presenter,
    qa_content_id content, const char *path) {
  size_t key_size = strlen(content) + strlen(path) + 2;
  char *key = malloc(key_size);
  if (key == NULL) qa_range_error("Out of memory");
  snprintf(key, key_size, "%s/%s", content, path);
  for (size_t i = 0; i < presenter->model_count; i++) {
    if (strcmp(presenter->models[i].key, key) == 0) {
      free(key);
      return presenter->models[i].asset;
    }
  }
  size_t length = 0;
  uint8_t *bytes = presenter->assets.open(presenter->assets.ctx, content, path, &length);
  const qa_app_q3_selected_model_asset *asset = NULL;
  if (bytes != NULL) {
    free(bytes);
    const char *error = NULL;
    asset = presenter->assets.model(presenter->assets.ctx, content, path, &error);
    if (asset == NULL) qa_range_error("%s", error != NULL ? error : "Out of memory");
  }
  if (presenter->model_count == presenter->model_capacity) {
    size_t cap = presenter->model_capacity == 0 ? 8 : presenter->model_capacity * 2;
    model_entry *next = realloc(presenter->models, cap * sizeof *next);
    if (next == NULL) qa_range_error("Out of memory");
    presenter->models = next; presenter->model_capacity = cap;
  }
  presenter->models[presenter->model_count].key = key;
  presenter->models[presenter->model_count].asset = asset;
  presenter->models[presenter->model_count].resolved = true;
  presenter->model_count++;
  return asset;
}
static const qa_app_q3_selected_model_asset *required_model(qa_app_selected_q3_weapon_presenter *presenter,
    qa_content_id content, const char *path) {
  const char *error = NULL;
  const qa_app_q3_selected_model_asset *asset = presenter->assets.model(presenter->assets.ctx, content, path, &error);
  if (asset == NULL) qa_range_error("%s", error != NULL ? error : "Out of memory");
  return asset;
}
static char *stem_path(const char *path, const char *suffix) {
  size_t len = strlen(path);
  const char *dot = strrchr(path, '.');
  size_t stem = dot != NULL && dot != path ? (size_t)(dot - path) : len;
  char *out = malloc(stem + strlen(suffix) + 1);
  if (out == NULL) qa_range_error("Out of memory");
  memcpy(out, path, stem);
  memcpy(out + stem, suffix, strlen(suffix) + 1);
  return out;
}
static qa_scene_entity part_entity(qa_app_q3_weapon_source source, const qa_app_q3_selected_model_asset *asset,
    qa_vec3 origin, qa_vec3 angles, double flags_bits, bool world) {
  qa_scene_entity entity = {0};
  entity.actor = source.actor;
  entity.resource = asset->resource;
  entity.model = asset->model;
  entity.transform.origin = origin;
  entity.transform.axis = qa_qvm_angles_to_axis(angles);
  entity.transform.scale = unit3;
  entity.previous_origin = world ? zero3 : origin;
  entity.pose.is_frame = true;
  entity.pose.frame = 0; entity.pose.previous_frame = 0; entity.pose.back_lerp = 0;
  entity.skin = 0;
  entity.color = (qa_vec4){1, 1, 1, 1};
  entity.shader_time.kind = world ? QA_SOURCE_TIME_SECONDS : QA_SOURCE_TIME_MILLISECONDS;
  entity.shader_time.value = 0;
  entity.flags_kind = "q3";
  entity.flags_bits = flags_bits;
  entity.lighting_origin = world ? source.origin : source.origin;
  entity.shadow_plane = 0;
  return entity;
}
static bool continuous_fire(qa_app_q3_weapon_view view) {
  return view.firing && (view.weapon == 1 || view.weapon == 6 || view.weapon == 10);
}
static bool flash_visible(qa_app_q3_weapon_view view) {
  return continuous_fire(view) || (view.has_last_fire && view.time_milliseconds - view.last_fire_milliseconds <= 20);
}
typedef struct pass_state { const char *shader; } pass_state;
static pass_state pass_states[8];
static qa_model_source_options pass_options(void *ctx, const qa_scene_entity *entity) {
  (void)entity;
  pass_state *state = ctx;
  qa_model_source_options options = {0};
  if (state->shader != NULL) {
    options.custom_shader = state->shader;
  }
  return options;
}
void qa_app_q3_weapon_passes_destroy(qa_app_q3_weapon_passes *passes) {
  if (passes == NULL) return;
  free(passes->items); free(passes->entities); free(passes->attachments);
  memset(passes, 0, sizeof *passes);
}
qa_app_q3_weapon_passes qa_app_selected_q3_weapon_presenter_world(qa_app_selected_q3_weapon_presenter *presenter,
    qa_app_q3_weapon_source source, qa_app_q3_weapon_character character, bool personal_model) {
  qa_app_q3_weapon_passes out = {0};
  if (!source.has_weapon || !source.visible || source.path[0] == '\0') return out;
  qa_app_q3_weapon_view view = source.weapon;
  char *barrel_path = stem_path(source.path, "_barrel.md3");
  char *flash_path = stem_path(source.path, "_flash.md3");
  const qa_app_q3_selected_model_asset *gun_asset = required_model(presenter, source.content, source.path);
  const qa_app_q3_selected_model_asset *barrel_asset = model_for(presenter, source.content, barrel_path);
  const qa_app_q3_selected_model_asset *flash_asset = model_for(presenter, source.content, flash_path);
  double flags = QA_Q3_PRES_RF_LIGHTING_ORIGIN | (personal_model ? QA_Q3_PRES_RF_THIRD_PERSON : 0);
  out.entities = calloc(3, sizeof *out.entities);
  out.attachments = calloc(1, sizeof *out.attachments);
  out.items = calloc(4, sizeof *out.items);
  if (out.entities == NULL || out.attachments == NULL || out.items == NULL) qa_range_error("Out of memory");
  qa_q3_weapon_barrel_step_result spin = qa_q3_weapon_barrel_step(&presenter->world_barrel, view.time_milliseconds, view.firing);
  out.entities[0] = part_entity(source, gun_asset, zero3, zero3, flags, true);
  out.entities[0].lighting_origin = character.origin;
  out.entity_count = 1;
  if (barrel_asset != NULL) {
    qa_vec3 angles = {0, 0, spin.angle};
    out.entities[1] = part_entity(source, barrel_asset, zero3, angles, flags, true);
    out.entities[1].lighting_origin = character.origin;
    out.attachments[0].tag = "tag_barrel";
    out.attachments[0].entity = &out.entities[1];
    out.entities[0].attachments = out.attachments;
    out.entities[0].attachment_count = 1;
    out.attachment_count = 1;
    out.entity_count = 2;
  }
  bool invisible = (((int64_t)character.powerups) & (1 << QA_Q3_PW_INVIS)) != 0;
  size_t pass_at = 0;
  const char *shaders[4];
  shaders[pass_at++] = invisible ? "powerups/invisibility" : NULL;
  if (!invisible) {
    if ((((int64_t)character.powerups) & (1 << QA_Q3_PW_BATTLESUIT)) != 0) shaders[pass_at++] = "powerups/battleWeapon";
    if ((((int64_t)character.powerups) & (1 << QA_Q3_PW_QUAD)) != 0) shaders[pass_at++] = "powerups/quadWeapon";
  }
  for (size_t i = 0; i < pass_at; i++) {
    qa_q3_character_pass *pass = &out.items[out.count];
    pass->content = source.content;
    pass->entity = out.entities[0];
    pass->shader = shaders[i];
    pass_states[out.count].shader = shaders[i];
    pass->options_ctx = &pass_states[out.count];
    pass->options = pass_options;
    out.count++;
  }
  if (flash_asset != NULL && flash_visible(view)) {
    qa_model_attachment_tag_result tag;
    if (qa_model_attachment_tag(&out.entities[0], "tag_flash", &tag)) {
      qa_vec3 angles = {0, 0, (float)(qa_game_random_crandom(&presenter->random) * 10)};
      qa_scene_entity flash = part_entity(source, flash_asset, zero3, angles, flags, true);
      flash.lighting_origin = character.origin;
      out.entities[2] = qa_attach_scene_entity(&out.entities[0], &flash, tag);
      out.entity_count = 3;
      qa_q3_character_pass *pass = &out.items[out.count];
      pass->content = source.content;
      pass->entity = out.entities[2];
      pass->shader = NULL;
      pass_states[out.count].shader = NULL;
      pass->options_ctx = &pass_states[out.count];
      pass->options = pass_options;
      out.count++;
    }
  }
  free(barrel_path); free(flash_path);
  return out;
}
static qa_vec3 add3(qa_vec3 a, qa_vec3 b) { qa_vec3 out = {a.x + b.x, a.y + b.y, a.z + b.z}; return out; }
static qa_vec3 scale3(qa_vec3 a, double s) { qa_vec3 out = {a.x * s, a.y * s, a.z * s}; return out; }
qa_scene_entity qa_app_selected_q3_weapon_presenter_frame(qa_app_selected_q3_weapon_presenter *presenter,
    qa_app_q3_weapon_source source, double field_of_view,
    qa_scene_entity *entity_scratch, size_t entity_capacity,
    qa_scene_entity_attachment *attachment_scratch, size_t attachment_capacity) {
  if (!source.has_weapon) qa_range_error("Selected Q3 weapon presentation state is missing");
  qa_app_q3_weapon_view view = source.weapon;
  const char *hand_fallback = "models/weapons2/shotgun/shotgun_hand.md3";
  char *barrel_path = stem_path(source.path, "_barrel.md3");
  char *flash_path = stem_path(source.path, "_flash.md3");
  char *hand_path = stem_path(source.path, "_hand.md3");
  const qa_app_q3_selected_model_asset *gun_asset = required_model(presenter, source.content, source.path);
  const qa_app_q3_selected_model_asset *own_hand = model_for(presenter, source.content,
      source.model_anchor != NULL ? source.model_anchor->path : hand_path);
  const qa_app_q3_selected_model_asset *barrel_asset = source.model_anchor == NULL
      ? model_for(presenter, source.content, barrel_path) : NULL;
  const qa_app_q3_selected_model_asset *flash_asset = source.model_anchor == NULL
      ? model_for(presenter, source.content, flash_path) : NULL;
  const qa_q3_player_animation_config *animation = source.model_anchor == NULL
      ? animation_for(presenter, source.content) : NULL;
  const qa_app_q3_selected_model_asset *hand_asset = own_hand != NULL ? own_hand
      : required_model(presenter, source.content, hand_fallback);
  free(barrel_path); free(flash_path); free(hand_path);
  if (animation != NULL) {
    qa_q3_run_lerp_frame_input input = {view.time_milliseconds, view.torso_animation, 1, false};
    qa_q3_run_lerp_frame(qa_q3_animation_set_from_config(animation), &presenter->torso, input, host_print, presenter);
  }
  double bob = view.bob_cycle;
  qa_q3_weapon_view_motion motion = {source.origin, source.angles, view.time_milliseconds,
      view.horizontal_speed, (double)(((int64_t)bob & 128) >> 7),
      fabs(sin(((double)((int64_t)bob & 127)) / 127 * acos(-1.0))), 0, 0};
  qa_q3_weapon_view_pose_result position = qa_q3_weapon_view_pose(motion);
  double flags = QA_Q3_PRES_RF_MINLIGHT | QA_Q3_PRES_RF_FIRST_PERSON | QA_Q3_PRES_RF_DEPTHHACK;
  qa_axis axis = qa_qvm_angles_to_axis(source.angles);
  qa_vec3 offset = source.model_anchor != NULL ? source.model_anchor->offset : zero3;
  double fov_offset = 0;
  if (source.model_anchor != NULL && source.model_anchor->has_fov_offset) {
    double above = field_of_view - source.model_anchor->fov_above;
    fov_offset = (float)((float)source.model_anchor->fov_scale * (float)(above > 0 ? above : 0));
  }
  qa_vec3 origin = add3(position.origin, add3(add3(scale3(axis.vectors[0], offset.x), scale3(axis.vectors[1], offset.y)),
      scale3(axis.vectors[2], (float)(offset.z + fov_offset))));
  size_t needed = 3 + source.model_attachment_count;
  if (entity_capacity < needed || attachment_capacity < needed) qa_range_error("Out of memory");
  qa_scene_entity hand = part_entity(source, hand_asset, origin, position.angles, flags, false);
  hand.pose.frame = animation == NULL ? source.frame
      : qa_q3_torso_weapon_frame(animation, presenter->torso.frame);
  hand.pose.previous_frame = animation == NULL ? source.old_frame
      : qa_q3_torso_weapon_frame(animation, presenter->torso.old_frame);
  hand.pose.back_lerp = animation == NULL ? (source.has_back_lerp ? source.back_lerp : 0)
      : presenter->torso.back_lerp;
  const char *tag_name = source.model_anchor != NULL && source.model_anchor->tag != NULL
      ? source.model_anchor->tag : "tag_weapon";
  qa_model_attachment_tag_result tag;
  if (!qa_model_attachment_tag(&hand, tag_name, &tag))
    qa_range_error("Selected Q3 hands model has no %s", tag_name);
  size_t entity_at = 0, attachment_at = 0;
  for (size_t i = 0; i < source.model_attachment_count; i++) {
    const qa_app_q3_selected_model_asset *asset = required_model(presenter, source.content, source.model_attachments[i].path);
    entity_scratch[entity_at] = part_entity(source, asset, zero3, zero3, flags, false);
    attachment_scratch[attachment_at].tag = source.model_attachments[i].tag;
    attachment_scratch[attachment_at].entity = &entity_scratch[entity_at];
    entity_at++; attachment_at++;
  }
  qa_q3_weapon_barrel_step_result spin = qa_q3_weapon_barrel_step(&presenter->barrel, view.time_milliseconds, view.firing);
  if (barrel_asset != NULL) {
    qa_vec3 angles = {0, 0, spin.angle};
    entity_scratch[entity_at] = part_entity(source, barrel_asset, zero3, angles, flags, false);
    attachment_scratch[attachment_at].tag = "tag_barrel";
    attachment_scratch[attachment_at].entity = &entity_scratch[entity_at];
    entity_at++; attachment_at++;
  }
  if (flash_asset != NULL && flash_visible(view)) {
    qa_vec3 angles = {0, 0, (float)(qa_game_random_crandom(&presenter->random) * 10)};
    entity_scratch[entity_at] = part_entity(source, flash_asset, zero3, angles, flags, false);
    attachment_scratch[attachment_at].tag = "tag_flash";
    attachment_scratch[attachment_at].entity = &entity_scratch[entity_at];
    entity_at++; attachment_at++;
  }
  qa_scene_entity gun = part_entity(source, gun_asset, zero3, zero3, flags, false);
  gun.attachments = attachment_scratch;
  gun.attachment_count = attachment_at;
  return qa_attach_scene_entity(&hand, &gun, tag);
}
