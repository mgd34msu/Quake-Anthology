/* Port of src/content/q3/foundation/presentation.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q3_FOUNDATION_PRESENTATION_H
#define QA_CONTENT_Q3_FOUNDATION_PRESENTATION_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/content/q3/foundation/assets.h"
#include "qa/content/q3/foundation/player_pose.h"
#include "qa/contracts/content.h"
#include "qa/contracts/math.h"
#include "qa/contracts/movement.h"
#include "qa/render/scene/models/types.h"

typedef struct qa_q3_character_view {
  qa_actor_id actor; qa_vec3 origin, angles, velocity; double movement_direction;
  qa_animation_state animation; /* Extract<...,{kind:"q3"}> */
  double source_flags, powerups; const char *team; /* NULL|"red"|"blue" */
  qa_vec4 color; bool has_scale; double scale; bool has_opacity; double opacity;
} qa_q3_character_view;
typedef struct qa_q3_character_pass qa_q3_character_pass;
struct qa_q3_character_pass {
  qa_content_id content; /* nullable */
  qa_scene_entity entity;
  const char *shader; /* nullable */
  void *options_ctx;
  qa_model_source_options (*options)(void *ctx, const qa_scene_entity *entity);
};
typedef struct qa_q3_character_render_options {
  double time_milliseconds, frame_milliseconds; qa_source_time shader_time;
  double swing_speed; bool no_player_animations, personal_model;
  bool has_shadow_plane; double shadow_plane;
  const qa_q3_character_pass *weapon; size_t weapon_count;
} qa_q3_character_render_options;

/* Presenter scratch: entities/attachments/passes are presenter-owned and
 * valid until the next frame/reset/destroy. */
typedef struct qa_q3_character_presenter qa_q3_character_presenter;
struct qa_q3_character_presenter {
  qa_q3_player_pose_state pose;
  const qa_q3_character_assets *assets;
  qa_actor_id actor_storage;
  qa_scene_entity entities[3]; /* legs, torso, head */
  qa_scene_entity_attachment legs_attachments[1], torso_attachments[1];
  qa_scene_entity *weapon_entities; size_t weapon_entity_cap;
  qa_q3_character_pass *passes; size_t pass_cap;
  qa_model_custom_skin_entry *skin_entries[3];
  struct qa_q3_character_pass_option_state { const qa_q3_character_presenter *presenter; const char *shader; } *option_states;
  size_t option_state_cap;
};
void qa_q3_character_presenter_init(qa_q3_character_presenter *presenter, const qa_q3_character_assets *assets);
void qa_q3_character_presenter_destroy(qa_q3_character_presenter *presenter);
void qa_q3_character_presenter_reset(qa_q3_character_presenter *presenter, qa_q3_character_view view, double time_milliseconds);
/* Returned passes are presenter-owned scratch. */
const qa_q3_character_pass *qa_q3_character_presenter_frame(qa_q3_character_presenter *presenter,
    qa_q3_character_view view, qa_q3_character_render_options options, size_t *out_count);
qa_axis qa_q3_identity_axis(void);
#endif
