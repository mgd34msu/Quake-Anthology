/* Port of src/content/q3/presentation/players.ts. GPL-2.0-or-later. */
#include "qa/content/q3/presentation/players.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q3/base/game/format.h"
#include "qa/content/q3/base/game/numeric.h"
#include "qa/content/q3/base/shared/trajectory.h"
#include "qa/content/q3/foundation/animation.h"
#include "qa/content/q3/foundation/animation_config.h"
#include "qa/content/q3/foundation/player_pose.h"
#include "qa/content/q3/presentation/character_resources.h"
#include "qa/content/q3/presentation/entities.h"
#include "qa/content/q3/presentation/model_access.h"
#include "qa/core/common_error.h"
#include "qa/core/common_parse.h"
#include "qa/core/info_string.h"
#include "qa/core/math.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/movement/q3/constants.h"

const char *const qa_q3_pres_custom_sound_names[] = {
    "*death1.wav", "*death2.wav", "*death3.wav", "*jump1.wav", "*pain25_1.wav", "*pain50_1.wav",
    "*pain75_1.wav", "*pain100_1.wav", "*falling1.wav", "*gasp.wav", "*drown.wav", "*fall1.wav", "*taunt.wav"};
const size_t qa_q3_pres_custom_sound_name_count =
    sizeof qa_q3_pres_custom_sound_names / sizeof qa_q3_pres_custom_sound_names[0];

enum { MASK_PLAYERSOLID = 1 | 0x10000 | 0x2000000, MASK_WATER = 8 | 16 | 32, DEAD = 1, KAMIKAZE = 0x200 };

static double f_(double value) { return (double)(float)value; }
static int32_t to_int32(double value) {
  if (!isfinite(value)) return 0;
  double truncated = trunc(fmod(fabs(value), 4294967296.0));
  uint32_t bits = (uint32_t)truncated;
  if (value < 0) bits = ~bits + 1u;
  int32_t result;
  memcpy(&result, &bits, sizeof result);
  return result;
}
static qa_q3_base_trajectory base_trajectory(const qa_q3_wire_trajectory *wire) {
  qa_q3_base_trajectory result = {wire->type, wire->time, wire->duration, wire->base, wire->delta};
  return result;
}
static bool powered(const qa_q3_base_entity_state *state, qa_q3_powerup powerup) {
  return (state->powerups & (1 << powerup)) != 0;
}
static double sphere_angle(double time, double divisor) {
  return f_(f_(f_((double)(to_int32(trunc(time / divisor)) & 255) * f_(acos(-1.0) * 2))) / 255);
}
static qa_q3_animation_set animation_set_of(qa_q3_pres_client_info *info) {
  qa_q3_animation_set set = {info->animations, NULL, QA_Q3_TOTAL_ANIMATION_COUNT};
  return set;
}
typedef struct print_ctx { void *ctx; void (*print)(void *ctx, const char *message); } print_ctx;
static void print_thunk(void *ctx, const char *message) {
  const print_ctx *print = ctx;
  print->print(print->ctx, message);
}

typedef qa_q3_pres_player_presenter presenter_t;
static double presenter_time(presenter_t *presenter) { return presenter->host.state->time; }
static qa_q3_base_source_player_state *snapshot_ps(presenter_t *presenter) {
  qa_q3_pres_retail_snapshot *snap = presenter->host.state->snap;
  if (snap == NULL) qa_range_error("CG_Player requires an active snapshot");
  return &snap->player_state;
}
static void add_model(presenter_t *presenter, const qa_q3_pres_ref_model_entity *model) {
  qa_q3_pres_ref_entity tagged = qa_q3_pres_ref_entity_from_model(*model);
  presenter->host.add_entity(presenter->host.ctx, &tagged);
}

void qa_q3_pres_player_presenter_init(presenter_t *presenter, qa_q3_pres_player_presentation_host host) {
  if (host.product != host.state->product) {
    qa_range_error("Player media product differs from cgame product");
  }
  presenter->host = host;
}

void qa_q3_pres_player_presenter_reset_player_entity(presenter_t *presenter,
    qa_q3_pres_client_entity *entity) {
  qa_q3_pres_player_presentation_host *host = &presenter->host;
  qa_q3_pres_client_info *ci = host->client_info(host->clients_ctx, entity->current_state.client_num);
  qa_q3_pres_player_presentation_settings settings = host->settings(host->ctx);
  print_ctx debug = {host->ctx, host->print};
  qa_q3_lerp_print_fn print = settings.debug_animation ? print_thunk : NULL;
  void *print_ctx_ptr = settings.debug_animation ? &debug : NULL;
  entity->error_time = -99999;
  entity->extrapolated = false;
  qa_q3_animation_set set = animation_set_of(ci);
  qa_q3_clear_lerp_frame(set, &entity->player.pose.legs.base, entity->current_state.legs_anim,
      presenter_time(presenter), print, print_ctx_ptr);
  qa_q3_clear_lerp_frame(set, &entity->player.pose.torso.base, entity->current_state.torso_anim,
      presenter_time(presenter), print, print_ctx_ptr);
  entity->lerp_origin = qa_q3_base_evaluate_trajectory(base_trajectory(&entity->current_state.pos),
      presenter_time(presenter));
  entity->lerp_angles = qa_q3_base_evaluate_trajectory(base_trajectory(&entity->current_state.apos),
      presenter_time(presenter));
  entity->raw_origin = entity->lerp_origin;
  entity->raw_angles = entity->lerp_angles;
  /* The source memset follows ClearLerpFrame and discards its timing. */
  entity->player.pose.legs.base = qa_q3_create_lerp_frame();
  entity->player.pose.legs.yaw_angle = entity->raw_angles.y;
  entity->player.pose.legs.yawing = false;
  entity->player.pose.legs.pitch_angle = 0;
  entity->player.pose.legs.pitching = false;
  entity->player.pose.torso.base = qa_q3_create_lerp_frame();
  entity->player.pose.torso.yaw_angle = entity->raw_angles.y;
  entity->player.pose.torso.yawing = false;
  entity->player.pose.torso.pitch_angle = entity->raw_angles.x;
  entity->player.pose.torso.pitching = false;
  if (settings.debug_position) {
    qa_q3_game_format_arg args[2] = {
        qa_q3_game_format_number(entity->current_state.number),
        qa_q3_game_format_number((double)(int32_t)qa_float32_to_bits(entity->player.pose.torso.yaw_angle))};
    char *message = qa_q3_game_format("%i ResetPlayerEntity yaw=%i\n", args, 2, 32000);
    if (message != NULL) {
      host->print(host->ctx, message);
      free(message);
    }
  }
}

void qa_q3_pres_player_presenter_add_ref_entity_with_powerups(presenter_t *presenter,
    qa_q3_pres_ref_model_entity *entity, const qa_q3_base_entity_state *state, qa_q3_base_team team,
    qa_qvm_body_part part) {
  qa_q3_pres_player_presentation_host *host = &presenter->host;
  if (host->body_hidden != NULL && host->body_hidden(host->body_ctx, state->number) == true) return;
  const qa_q3_pres_player_media *media = &host->media;
  const qa_q3_pres_scene_shader *initial_shader = entity->shading.custom_shader;
  /* submit(): bodySubmission may consume the part instead of addEntity. */
  #define SUBMIT()                                                                                              \
    do {                                                                                                        \
      if (part == NULL || host->body_submission == NULL ||                                                      \
          host->body_submission(host->body_ctx, state->number, part, entity,                                   \
              entity->shading.custom_shader == initial_shader) != true) {                                       \
        add_model(presenter, entity);                                                                           \
      }                                                                                                         \
    } while (0)
  if (powered(state, QA_Q3_PW_INVIS)) {
    entity->shading.custom_shader = media->invis_shader;
    SUBMIT();
    return;
  }
  SUBMIT();
  if (powered(state, QA_Q3_PW_QUAD)) {
    entity->shading.custom_shader = team == QA_Q3_BASE_TEAM_RED ? media->red_quad_shader : media->quad_shader;
    SUBMIT();
  }
  if (powered(state, QA_Q3_PW_REGEN) && (int64_t)trunc(presenter_time(presenter) / 100) % 10 == 1) {
    entity->shading.custom_shader = media->regen_shader;
    SUBMIT();
  }
  if (powered(state, QA_Q3_PW_BATTLESUIT)) {
    entity->shading.custom_shader = media->battle_suit_shader;
    SUBMIT();
  }
  #undef SUBMIT
}

bool qa_q3_pres_player_presenter_light_verts(presenter_t *presenter, qa_vec3 normal,
    qa_q3_pres_ref_poly_vertex *vertices, size_t vertex_count) {
  qa_q3_pres_lighting_sample light = presenter->host.light_for_point(presenter->host.ctx, vertices[0].position);
  double incoming = qa_dot3(normal, light.light_dir);
  for (size_t index = 0; index < vertex_count; index++) {
    double components[3];
    const double ambient[3] = {light.ambient_light.x, light.ambient_light.y, light.ambient_light.z};
    const double directed[3] = {light.directed_light.x, light.directed_light.y, light.directed_light.z};
    for (int axis = 0; axis < 3; axis++) {
      if (incoming <= 0) {
        components[axis] = (double)((int64_t)trunc(ambient[axis]) & 255);
      } else {
        double value = trunc(f_(ambient[axis] + f_(incoming * directed[axis])));
        components[axis] = (double)((int64_t)(value < 255 ? value : 255) & 255);
      }
    }
    vertices[index].color = qa_math_vec4(components[0], components[1], components[2], 255);
  }
  return true;
}

static void animation(presenter_t *presenter, qa_q3_pres_client_entity *entity, qa_q3_pres_client_info *ci,
    qa_q3_pres_ref_model_entity *legs, qa_q3_pres_ref_model_entity *torso,
    qa_q3_pres_player_presentation_settings options) {
  if (options.no_player_animations) return;
  const qa_q3_base_entity_state *state = &entity->current_state;
  double speed_scale = powered(state, QA_Q3_PW_HASTE) ? 1.5 : 1;
  double legs_animation = entity->player.pose.legs.yawing &&
          (state->legs_anim & ~QA_Q3_ANIMATION_TOGGLE_BIT) == QA_Q3_ANIM_LEGS_IDLE
      ? QA_Q3_ANIM_LEGS_TURN : state->legs_anim;
  print_ctx debug = {presenter->host.ctx, presenter->host.print};
  qa_q3_lerp_print_fn print = options.debug_animation ? print_thunk : NULL;
  void *print_ctx_ptr = options.debug_animation ? &debug : NULL;
  qa_q3_animation_set set = animation_set_of(ci);
  qa_q3_run_lerp_frame_input legs_input = {presenter_time(presenter), legs_animation, speed_scale,
                                           options.animation_speed == 0};
  qa_q3_run_lerp_frame(set, &entity->player.pose.legs.base, legs_input, print, print_ctx_ptr);
  qa_q3_run_lerp_frame_input torso_input = {presenter_time(presenter), state->torso_anim, speed_scale,
                                            options.animation_speed == 0};
  qa_q3_run_lerp_frame(set, &entity->player.pose.torso.base, torso_input, print, print_ctx_ptr);
  legs->old_frame = entity->player.pose.legs.base.old_frame;
  legs->frame = entity->player.pose.legs.base.frame;
  legs->back_lerp = entity->player.pose.legs.base.back_lerp;
  torso->old_frame = entity->player.pose.torso.base.old_frame;
  torso->frame = entity->player.pose.torso.base.frame;
  torso->back_lerp = entity->player.pose.torso.base.back_lerp;
}

static void sprite(presenter_t *presenter, qa_q3_pres_client_entity *entity,
    const qa_q3_pres_scene_shader *shader) {
  qa_q3_pres_ref_sprite_entity sprite = qa_q3_pres_create_sprite_entity();
  sprite.origin = qa_add3(entity->lerp_origin, qa_math_vec3(0, 0, 48));
  sprite.shading.custom_shader = shader;
  sprite.radius = 10;
  sprite.shading.render_flags = entity->current_state.number == snapshot_ps(presenter)->client_num &&
          !presenter->host.state->rendering_third_person
      ? QA_Q3_PRES_RF_THIRD_PERSON : 0;
  sprite.shading.shader_rgba = qa_math_vec4(255, 255, 255, 255);
  qa_q3_pres_ref_entity tagged = qa_q3_pres_ref_entity_from_sprite(sprite);
  presenter->host.add_entity(presenter->host.ctx, &tagged);
}
static void sprites(presenter_t *presenter, qa_q3_pres_client_entity *entity, qa_q3_pres_client_info *ci,
    qa_q3_pres_player_presentation_settings options) {
  const qa_q3_pres_player_media *media = &presenter->host.media;
  const struct { int32_t flag; const qa_q3_pres_scene_shader *shader; } choices[] = {
      {0x2000, media->connection_shader}, {0x1000, media->balloon_shader}, {0x8000, media->medal_impressive},
      {8, media->medal_excellent}, {64, media->medal_gauntlet}, {0x10000, media->medal_defend},
      {0x20000, media->medal_assist}, {0x800, media->medal_capture}};
  for (size_t index = 0; index < sizeof choices / sizeof choices[0]; index++) {
    if (entity->current_state.e_flags & choices[index].flag) {
      sprite(presenter, entity, choices[index].shader);
      return;
    }
  }
  if (!(entity->current_state.e_flags & DEAD) &&
      qa_q3_base_player_state_slots_get(&snapshot_ps(presenter)->persistant, QA_Q3_BASE_PERS_TEAM) == ci->team &&
      options.game_type >= QA_Q3_BASE_GT_TEAM && options.draw_friend) {
    sprite(presenter, entity, media->friend_shader);
  }
}
typedef struct shadow_result { bool visible; double plane; } shadow_result;
static shadow_result shadow(presenter_t *presenter, qa_q3_pres_client_entity *entity,
    qa_q3_pres_player_presentation_settings options) {
  shadow_result hidden = {false, 0};
  if (options.shadows == 0 || powered(&entity->current_state, QA_Q3_PW_INVIS)) return hidden;
  qa_q3_trace_query query;
  memset(&query, 0, sizeof query);
  query.start = entity->lerp_origin;
  query.end = qa_add3(entity->lerp_origin, qa_math_vec3(0, 0, -128));
  query.shape.kind = "box";
  query.shape.mins = qa_math_vec3(-15, -15, 0);
  query.shape.maxs = qa_math_vec3(15, 15, 2);
  query.mask = MASK_PLAYERSOLID;
  qa_q3_trace_result trace = presenter->host.collision.trace(presenter->host.collision.ctx, query);
  if (trace.fraction == 1 || strcmp(trace.solidity, "clear") != 0) return hidden;
  shadow_result result = {true, f_(trace.end.z + 1)};
  if (options.shadows != 1) return result;
  if (!trace.has_plane) qa_range_error("Player shadow hit has no contact plane");
  double alpha = f_(1 - trace.fraction);
  qa_q3_pres_impact_mark_request_mirror request;
  memset(&request, 0, sizeof request);
  request.shader = presenter->host.media.shadow_mark_shader;
  request.origin = trace.end;
  request.direction = trace.plane.normal;
  request.orientation = entity->player.pose.legs.yaw_angle;
  request.color = qa_math_vec4(alpha, alpha, alpha, 1);
  request.alpha_fade = false;
  request.radius = 24;
  request.temporary = true;
  qa_q3_pres_ref_poly *polys = NULL;
  size_t poly_count = 0;
  if (presenter->host.marks.impact_mark(presenter->host.marks.ctx, request, &polys, &poly_count)) {
    for (size_t index = 0; index < poly_count; index++) {
      presenter->host.add_poly(presenter->host.ctx, &polys[index]);
      qa_q3_pres_ref_poly_destroy(&polys[index]);
    }
    free(polys);
  }
  return result;
}
static void splash(presenter_t *presenter, qa_q3_pres_client_entity *entity,
    qa_q3_pres_player_presentation_settings options) {
  if (options.shadows == 0) return;
  qa_q3_pres_collision_world *collision = &presenter->host.collision;
  qa_vec3 end = qa_add3(entity->lerp_origin, qa_math_vec3(0, 0, -24));
  if (!(to_int32(collision->point_contents(collision->ctx, end, 0)) & MASK_WATER)) return;
  qa_vec3 start = qa_add3(entity->lerp_origin, qa_math_vec3(0, 0, 32));
  if (to_int32(collision->point_contents(collision->ctx, start, 0)) & (1 | MASK_WATER)) return;
  qa_q3_trace_query query;
  memset(&query, 0, sizeof query);
  query.start = start;
  query.end = end;
  query.shape.kind = "point";
  query.mask = MASK_WATER;
  qa_q3_trace_result trace = collision->trace(collision->ctx, query);
  if (trace.fraction == 1) return;
  qa_q3_pres_ref_poly_vertex vertices[4] = {
      {qa_add3(trace.end, qa_math_vec3(-32, -32, 0)), {0, 0}, {255, 255, 255, 255}},
      {qa_add3(trace.end, qa_math_vec3(-32, 32, 0)), {0, 1}, {255, 255, 255, 255}},
      {qa_add3(trace.end, qa_math_vec3(32, 32, 0)), {1, 1}, {255, 255, 255, 255}},
      {qa_add3(trace.end, qa_math_vec3(32, -32, 0)), {1, 0}, {255, 255, 255, 255}}};
  qa_q3_pres_ref_poly poly = {presenter->host.media.wake_mark_shader, vertices, 4};
  presenter->host.add_poly(presenter->host.ctx, &poly);
}
static qa_q3_pres_local_entity *smoke_puff(presenter_t *presenter, qa_vec3 origin, qa_vec3 velocity,
    double radius, qa_vec4 color, double duration, double start_time, double fade_in_time, double flags,
    const qa_q3_pres_scene_shader *shader) {
  qa_q3_pres_smoke_puff_options_mirror options;
  memset(&options, 0, sizeof options);
  options.origin = origin;
  options.velocity = velocity;
  options.radius = radius;
  options.color = color;
  options.duration = duration;
  options.start_time = start_time;
  options.fade_in_time = fade_in_time;
  options.flags = flags;
  options.shader = shader;
  return presenter->host.effects.smoke_puff(presenter->host.effects.ctx, options);
}
static void haste_trail(presenter_t *presenter, qa_q3_pres_client_entity *entity) {
  if (entity->trail_time > presenter_time(presenter)) return;
  int32_t animation_number =
      to_int32(entity->player.pose.legs.base.animation_number) & ~QA_Q3_ANIMATION_TOGGLE_BIT;
  if (animation_number != QA_Q3_ANIM_LEGS_RUN && animation_number != QA_Q3_ANIM_LEGS_BACK) return;
  entity->trail_time = to_int32(entity->trail_time + 100);
  if (entity->trail_time < presenter_time(presenter)) entity->trail_time = presenter_time(presenter);
  qa_q3_pres_local_entity *smoke = smoke_puff(presenter,
      qa_add3(entity->lerp_origin, qa_math_vec3(0, 0, -16)), qa_math_vec3(0, 0, 0), 8,
      qa_math_vec4(1, 1, 1, 1), 500, presenter_time(presenter), 0, 0, presenter->host.media.haste_puff_shader);
  smoke->le_type = QA_Q3_PRES_LE_SCALE_FADE;
}
static void breath(presenter_t *presenter, qa_q3_pres_client_entity *entity,
    qa_q3_pres_ref_model_entity *head, qa_q3_pres_player_presentation_settings options,
    const qa_q3_pres_mission_player_media *media) {
  if (!options.enable_breath ||
      (entity->current_state.number == snapshot_ps(presenter)->client_num &&
       !presenter->host.state->rendering_third_person) ||
      (entity->current_state.e_flags & DEAD)) {
    return;
  }
  qa_q3_pres_client_info *ci =
      presenter->host.client_info(presenter->host.clients_ctx, entity->current_state.number);
  if ((to_int32(presenter->host.collision.point_contents(presenter->host.collision.ctx, head->origin, 0)) &
       MASK_WATER) || ci->breath_puff_time > presenter_time(presenter)) {
    return;
  }
  (void)smoke_puff(presenter,
      qa_add3(qa_add3(head->origin, qa_scale3(head->axis.vectors[0], 8)),
              qa_scale3(head->axis.vectors[2], -4)),
      qa_math_vec3(0, 0, 8), 16, qa_math_vec4(1, 1, 1, 0.66), 1500, presenter_time(presenter),
      to_int32(presenter_time(presenter) + 400), 1, media->shotgun_smoke_puff_shader);
  ci->breath_puff_time = to_int32(presenter_time(presenter) + 2000);
}
static void dust(presenter_t *presenter, qa_q3_pres_client_entity *entity,
    qa_q3_pres_player_presentation_settings options, const qa_q3_pres_mission_player_media *media) {
  if (!options.enable_dust || entity->dust_trail_time > presenter_time(presenter)) return;
  int32_t animation_number =
      to_int32(entity->player.pose.legs.base.animation_number) & ~QA_Q3_ANIMATION_TOGGLE_BIT;
  if (animation_number != QA_Q3_ANIM_LEGS_LANDB && animation_number != QA_Q3_ANIM_LEGS_LAND) return;
  entity->dust_trail_time = to_int32(entity->dust_trail_time + 40);
  if (entity->dust_trail_time < presenter_time(presenter)) entity->dust_trail_time = presenter_time(presenter);
  qa_vec3 origin = entity->current_state.pos.base;
  qa_bounds zero = {{0, 0, 0}, {0, 0, 0}};
  qa_q3_trace_result trace = presenter->host.trace(presenter->host.trace_ctx, origin,
      qa_add3(origin, qa_math_vec3(0, 0, -64)), zero, entity->current_state.number, MASK_PLAYERSOLID);
  if (!(to_int32(trace.surface_flags) & 0x40000)) return;
  (void)smoke_puff(presenter, qa_add3(origin, qa_math_vec3(0, 0, -16)), qa_math_vec3(0, 0, -30), 24,
      qa_math_vec4(0.8, 0.8, 0.7, 0.33), 500, presenter_time(presenter), 0, 0, media->dust_puff_shader);
}

static void trail_item(presenter_t *presenter, qa_q3_pres_client_entity *entity, qa_q3_pres_scene_model model) {
  qa_vec3 angles = qa_math_vec3(0, entity->lerp_angles.y, 0);
  qa_axis axis = qa_qvm_angles_to_axis(angles);
  qa_q3_pres_ref_model_entity item = qa_q3_pres_create_model_entity(model);
  item.origin = qa_add3(qa_add3(entity->lerp_origin, qa_scale3(axis.vectors[0], -16)), qa_math_vec3(0, 0, 16));
  item.axis = qa_qvm_angles_to_axis(qa_math_vec3(0, angles.y + 90, 0));
  add_model(presenter, &item);
}
static void flag(presenter_t *presenter, qa_q3_pres_client_entity *entity,
    const qa_q3_pres_scene_skin *skin, qa_q3_pres_ref_model_entity *torso,
    qa_q3_pres_player_presentation_settings options) {
  qa_q3_pres_ref_model_entity pole = qa_q3_pres_create_model_entity(presenter->host.media.flag_pole_model);
  qa_q3_pres_ref_model_entity flap = qa_q3_pres_create_model_entity(presenter->host.media.flag_flap_model);
  pole.lighting_origin = torso->lighting_origin;
  pole.shadow_plane = torso->shadow_plane;
  pole.shading.render_flags = torso->shading.render_flags;
  qa_q3_pres_position_entity_on_tag(&pole, torso, &torso->model, "tag_flag");
  add_model(presenter, &pole);
  flap.custom_skin = skin;
  flap.lighting_origin = torso->lighting_origin;
  flap.shadow_plane = torso->shadow_plane;
  flap.shading.render_flags = torso->shading.render_flags;
  int32_t legs = entity->current_state.legs_anim & ~QA_Q3_ANIMATION_TOGGLE_BIT;
  bool idle = legs == QA_Q3_ANIM_LEGS_IDLE || legs == QA_Q3_ANIM_LEGS_IDLECR;
  bool walk = legs == QA_Q3_ANIM_LEGS_WALK || legs == QA_Q3_ANIM_LEGS_WALKCR;
  if (!idle) {
    qa_vec3 direction = qa_normalize3(qa_add3(entity->current_state.pos.delta, qa_math_vec3(0, 0, 100)));
    if (fabs(qa_dot3(pole.axis.vectors[2], direction)) < f_(0.9)) {
      double dot = qa_dot3(pole.axis.vectors[0], direction);
      double angle = f_(acos(dot < -1 ? -1 : dot > 1 ? 1 : dot));
      double degrees = f_(f_(angle * 180) / f_(acos(-1.0)));
      double yaw = qa_dot3(pole.axis.vectors[1], direction) < 0 ? f_(360 - degrees) : degrees;
      if (yaw < 0) yaw = f_(yaw + 360);
      if (yaw > 360) yaw = f_(yaw - 360);
      qa_q3_swing_angles_input input = {yaw, 25, 90, 0.15, presenter->host.state->frame_time,
                                        entity->player.flag.yaw_angle, entity->player.flag.yawing};
      qa_q3_swing_angles_result swing = qa_q3_swing_angles(input);
      entity->player.flag.yaw_angle = swing.angle;
      entity->player.flag.yawing = swing.swinging;
    }
  }
  qa_q3_pres_client_info *ci =
      presenter->host.client_info(presenter->host.clients_ctx, entity->current_state.client_num);
  print_ctx debug = {presenter->host.ctx, presenter->host.print};
  qa_q3_animation_set set = animation_set_of(ci);
  qa_q3_run_lerp_frame_input input = {presenter_time(presenter),
      idle || walk ? QA_Q3_ANIM_FLAG_STAND : QA_Q3_ANIM_FLAG_RUN, 1, options.animation_speed == 0};
  qa_q3_run_lerp_frame(set, &entity->player.flag.base, input,
      options.debug_animation ? print_thunk : NULL, options.debug_animation ? &debug : NULL);
  flap.old_frame = entity->player.flag.base.old_frame;
  flap.frame = entity->player.flag.base.frame;
  flap.back_lerp = entity->player.flag.base.back_lerp;
  flap.axis = qa_qvm_angles_to_axis(qa_math_vec3(0, entity->player.flag.yaw_angle, 0));
  qa_q3_pres_position_rotated_entity_on_tag(&flap, &pole, &pole.model, "tag_flag");
  add_model(presenter, &flap);
}
static void powerups(presenter_t *presenter, qa_q3_pres_client_entity *entity,
    qa_q3_pres_ref_model_entity *torso, qa_q3_pres_client_info *ci,
    qa_q3_pres_player_presentation_settings options) {
  const qa_q3_base_entity_state *state = &entity->current_state;
  const qa_q3_pres_player_media *media = &presenter->host.media;
  if (!state->powerups) return;
  if (powered(state, QA_Q3_PW_QUAD)) {
    qa_q3_pres_dynamic_light light = {entity->lerp_origin,
        200 + (qa_game_random_rand(presenter->host.random) & 31), {0.2, 0.2, 1}, false};
    presenter->host.add_light(presenter->host.ctx, light);
  }
  if (powered(state, QA_Q3_PW_FLIGHT)) {
    presenter->host.add_looping_sound(presenter->host.ctx, state->number, entity->lerp_origin,
        qa_math_vec3(0, 0, 0), media->flight_sound);
  }
  const struct { qa_q3_powerup powerup; qa_q3_pres_scene_model model; const qa_q3_pres_scene_skin *skin; qa_vec3 color; } flags[] = {
      {QA_Q3_PW_REDFLAG, media->red_flag_model, media->red_flag_flap_skin, {1, 0.2, 0.2}},
      {QA_Q3_PW_BLUEFLAG, media->blue_flag_model, media->blue_flag_flap_skin, {0.2, 0.2, 1}},
      {QA_Q3_PW_NEUTRALFLAG, media->neutral_flag_model, media->neutral_flag_flap_skin, {1, 1, 1}}};
  for (size_t index = 0; index < sizeof flags / sizeof flags[0]; index++) {
    if (!powered(state, flags[index].powerup)) continue;
    if (ci->new_anims) flag(presenter, entity, flags[index].skin, torso, options);
    else trail_item(presenter, entity, flags[index].model);
    qa_q3_pres_dynamic_light light = {entity->lerp_origin,
        200 + (qa_game_random_rand(presenter->host.random) & 31), flags[index].color, false};
    presenter->host.add_light(presenter->host.ctx, light);
  }
  if (powered(state, QA_Q3_PW_HASTE)) haste_trail(presenter, entity);
}
static qa_vec3 trail_position_at(qa_q3_pres_skull_trail *trail, int64_t index) {
  if (index < 0 || index >= QA_Q3_PRES_SKULL_TRAIL_POSITIONS) {
    qa_range_error("Player index %lld outside %d", (long long)index, QA_Q3_PRES_SKULL_TRAIL_POSITIONS);
  }
  return trail->positions[index];
}
static void tokens(presenter_t *presenter, qa_q3_pres_client_entity *entity, qa_q3_pres_client_info *ci,
    double render_flags, const qa_q3_pres_mission_player_media *media) {
  double token_count_value = entity->current_state.generic1 < 10 ? entity->current_state.generic1 : 10;
  int32_t number = entity->current_state.number;
  if (number < 0 || number >= QA_Q3_PRES_MAX_CLIENTS) {
    /* Corpses have non-client entity numbers. */
    if (token_count_value == 0) return;
    qa_range_error("No skull trail for entity %lld", (long long)number);
  }
  qa_q3_pres_skull_trail *trail = &presenter->host.state->skull_trails[number];
  if (token_count_value == 0) {
    trail->num_positions = 0;
    return;
  }
  for (double i = 0; i < token_count_value - trail->num_positions; i++) {
    for (double j = trail->num_positions; j > 0; j--) {
      trail->positions[(size_t)j] = trail_position_at(trail, (int64_t)j - 1);
    }
    trail->positions[0] = entity->lerp_origin;
  }
  trail->num_positions = token_count_value;
  qa_vec3 origin = entity->lerp_origin;
  for (double i = 0; i < trail->num_positions; i++) {
    qa_vec3 delta = qa_sub3(trail_position_at(trail, (int64_t)i), origin);
    if (qa_length3(delta) > 30) {
      trail->positions[(size_t)i] = qa_add3(origin, qa_scale3(qa_normalize3(delta), 30));
    }
    origin = trail_position_at(trail, (int64_t)i);
  }
  qa_q3_pres_ref_model_entity skull = qa_q3_pres_create_model_entity(
      ci->team == QA_Q3_BASE_TEAM_BLUE ? media->red_cube_model : media->blue_cube_model);
  skull.shading.render_flags = render_flags;
  origin = entity->lerp_origin;
  for (double i = 0; i < trail->num_positions; i++) {
    qa_vec3 position = trail_position_at(trail, (int64_t)i);
    qa_vec3 delta = qa_sub3(origin, position);
    qa_vec3 forward = qa_normalize3(qa_math_vec3(delta.x, delta.y, 0));
    qa_vec3 up = qa_math_vec3(0, 0, 1);
    skull.axis.vectors[0] = forward;
    skull.axis.vectors[1] = qa_cross3(forward, up);
    skull.axis.vectors[2] = up;
    double angle = sphere_angle(to_int32(presenter_time(presenter) + 500 * 10 - 500 * i), 16);
    skull.origin = qa_add3(position, qa_math_vec3(0, 0, f_(f_(sin(angle)) * 10)));
    add_model(presenter, &skull);
    origin = position;
  }
}
static void kamikaze_pair(presenter_t *presenter, qa_q3_pres_ref_model_entity *skull,
    const qa_q3_pres_mission_player_media *media, bool flip_trail) {
  skull->model = media->kamikaze_head_model;
  add_model(presenter, skull);
  if (flip_trail) skull->axis.vectors[1] = qa_scale3(skull->axis.vectors[1], -1);
  skull->model = media->kamikaze_head_trail;
  add_model(presenter, skull);
}
static void kamikaze_orbit_axis(qa_q3_pres_ref_model_entity *skull, qa_vec3 direction) {
  qa_vec3 side = qa_normalize3(qa_math_vec3(direction.x, direction.y, 0));
  qa_vec3 up = qa_math_vec3(0, 0, 1);
  skull->axis.vectors[0] = qa_cross3(side, up);
  skull->axis.vectors[1] = side;
  skull->axis.vectors[2] = up;
}
static void kamikaze(presenter_t *presenter, qa_q3_pres_client_entity *entity,
    qa_q3_pres_ref_model_entity *torso, const qa_q3_pres_mission_player_media *media) {
  qa_q3_pres_ref_model_entity skull = qa_q3_pres_create_model_entity_default();
  skull.lighting_origin = entity->lerp_origin;
  skull.shadow_plane = torso->shadow_plane;
  skull.shading.render_flags = torso->shading.render_flags;
  double pi = acos(-1.0);
  if (entity->current_state.e_flags & DEAD) {
    double angle = sphere_angle(presenter_time(presenter), 7);
    if (angle > f_(pi * 2)) angle = f_(angle - f_(f_(pi) * 2));
    double x = f_(f_(sin(angle)) * 20), y = f_(f_(cos(angle)) * 20);
    angle = sphere_angle(presenter_time(presenter), 4);
    qa_vec3 direction = qa_math_vec3(x, y, f_(15 + f_(f_(sin(angle)) * 8)));
    skull.origin = qa_add3(torso->origin, direction);
    kamikaze_orbit_axis(&skull, direction);
    kamikaze_pair(presenter, &skull, media, false);
    return;
  }
  double angle = sphere_angle(presenter_time(presenter), 4);
  qa_vec3 direction = qa_math_vec3(f_(f_(cos(angle)) * 20), f_(f_(sin(angle)) * 20), f_(f_(cos(angle)) * 20));
  skull.origin = qa_add3(torso->origin, direction);
  double yaw = f_(f_(f_(angle * 180) / f_(pi)) + 90);
  if (yaw > 360) yaw = f_(yaw - 360);
  skull.axis = qa_qvm_angles_to_axis(qa_math_vec3(f_(f_(sin(angle)) * 30), yaw, 0));
  kamikaze_pair(presenter, &skull, media, true);
  angle = f_(sphere_angle(presenter_time(presenter), 4) + f_(pi));
  if (angle > f_(pi * 2)) angle = f_(angle - f_(f_(pi) * 2));
  direction = qa_math_vec3(f_(f_(sin(angle)) * 20), f_(f_(cos(angle)) * 20), f_(f_(cos(angle)) * 20));
  skull.origin = qa_add3(torso->origin, direction);
  yaw = f_(360 - f_(f_(angle * 180) / f_(pi)));
  if (yaw > 360) yaw = f_(yaw - 360);
  skull.axis = qa_qvm_angles_to_axis(qa_math_vec3(f_(f_(cos(f_(angle - f_(0.5 * pi)))) * 30), yaw, 0));
  kamikaze_pair(presenter, &skull, media, false);
  angle = f_(sphere_angle(presenter_time(presenter), 3) + f_(0.5 * pi));
  if (angle > f_(pi * 2)) angle = f_(angle - f_(f_(pi) * 2));
  direction = qa_math_vec3(f_(f_(sin(angle)) * 20), f_(f_(cos(angle)) * 20), 0);
  skull.origin = qa_add3(torso->origin, direction);
  kamikaze_orbit_axis(&skull, direction);
  kamikaze_pair(presenter, &skull, media, false);
}
static void mission_powerups(presenter_t *presenter, qa_q3_pres_client_entity *entity,
    qa_q3_pres_client_info *ci, qa_q3_pres_ref_model_entity *torso,
    const qa_q3_pres_mission_player_media *media) {
  if (entity->current_state.e_flags & KAMIKAZE) kamikaze(presenter, entity, torso, media);
  const struct { qa_q3_powerup powerup; qa_q3_pres_scene_model model; } attachments[] = {
      {QA_Q3_PW_GUARD, media->guard_powerup_model}, {QA_Q3_PW_SCOUT, media->scout_powerup_model},
      {QA_Q3_PW_DOUBLER, media->doubler_powerup_model}, {QA_Q3_PW_AMMOREGEN, media->ammo_regen_powerup_model}};
  for (size_t index = 0; index < sizeof attachments / sizeof attachments[0]; index++) {
    if (!powered(&entity->current_state, attachments[index].powerup)) continue;
    qa_q3_pres_ref_model_entity attachment = *torso;
    attachment.model = attachments[index].model;
    attachment.frame = 0;
    attachment.old_frame = 0;
    attachment.custom_skin = NULL;
    add_model(presenter, &attachment);
  }
  bool invulnerable = powered(&entity->current_state, QA_Q3_PW_INVULNERABILITY);
  if (invulnerable) {
    if (!ci->invulnerability_start_time) ci->invulnerability_start_time = presenter_time(presenter);
    ci->invulnerability_stop_time = presenter_time(presenter);
  } else {
    ci->invulnerability_start_time = 0;
  }
  int32_t since_start = to_int32(presenter_time(presenter) - ci->invulnerability_start_time);
  int32_t since_stop = to_int32(presenter_time(presenter) - ci->invulnerability_stop_time);
  if (invulnerable || since_stop < 250) {
    qa_q3_pres_ref_model_entity shell = *torso;
    shell.model = media->invulnerability_powerup_model;
    shell.custom_skin = NULL;
    shell.shading.render_flags = to_int32(shell.shading.render_flags) & ~QA_Q3_PRES_RF_THIRD_PERSON;
    shell.origin = entity->lerp_origin;
    double scale = since_start < 250 ? f_(f_((double)since_start) / 250)
        : since_stop < 250 ? f_(f_((double)to_int32(250 - (double)since_stop)) / 250) : 1;
    shell.axis.vectors[0] = qa_math_vec3(scale, 0, 0);
    shell.axis.vectors[1] = qa_math_vec3(0, scale, 0);
    shell.axis.vectors[2] = qa_math_vec3(0, 0, scale);
    add_model(presenter, &shell);
  }
  int32_t elapsed = to_int32(presenter_time(presenter) - ci->medkit_usage_time);
  if (ci->medkit_usage_time && elapsed < 500) {
    qa_q3_pres_ref_model_entity medkit = *torso;
    medkit.model = media->medkit_usage_model;
    medkit.custom_skin = NULL;
    medkit.shading.render_flags = to_int32(medkit.shading.render_flags) & ~QA_Q3_PRES_RF_THIRD_PERSON;
    medkit.axis = qa_qvm_angles_to_axis(qa_math_vec3(0, 0, 0));
    medkit.origin = qa_add3(entity->lerp_origin,
        qa_math_vec3(0, 0, f_(-24 + f_(f_(f_((double)elapsed) * 80) / 500))));
    double channel = elapsed > 400
        ? (double)(to_int32(f_(255 - f_(f_(f_((double)to_int32((double)elapsed - 1000) * 255) / 100)))) & 255)
        : 255;
    medkit.shading.shader_rgba = qa_math_vec4(channel, channel, channel, channel);
    add_model(presenter, &medkit);
  }
}

void qa_q3_pres_player_presenter_player(presenter_t *presenter, qa_q3_pres_client_entity *entity) {
  qa_q3_pres_player_presentation_host *host = &presenter->host;
  int32_t client_num = entity->current_state.client_num;
  if (client_num < 0 || client_num >= 64) {
    qa_common_error(QA_COMMON_ERROR_DROP, "Bad clientNum on player entity");
  }
  qa_q3_pres_client_info *ci = host->client_info(host->clients_ctx, client_num);
  if (!ci->info_valid) return;
  qa_q3_pres_player_presentation_settings options = host->settings(host->ctx);
  bool body_visible =
      host->body_hidden == NULL || host->body_hidden(host->body_ctx, entity->current_state.number) != true;
  double render_flags = 0;
  if (entity->current_state.number == snapshot_ps(presenter)->client_num) {
    if (!host->state->rendering_third_person) render_flags = QA_Q3_PRES_RF_THIRD_PERSON;
    else if (options.camera_mode) return;
  }
  qa_q3_pres_ref_model_entity legs = qa_q3_pres_create_model_entity_default();
  qa_q3_pres_ref_model_entity torso = qa_q3_pres_create_model_entity_default();
  qa_q3_pres_ref_model_entity head = qa_q3_pres_create_model_entity_default();
  qa_q3_calculate_player_pose_input pose_input;
  memset(&pose_input, 0, sizeof pose_input);
  pose_input.entity.e_flags = entity->current_state.e_flags;
  pose_input.entity.velocity = entity->current_state.pos.delta;
  pose_input.entity.movement_direction = entity->current_state.angles2.y;
  pose_input.entity.legs_anim = entity->current_state.legs_anim;
  pose_input.entity.torso_anim = entity->current_state.torso_anim;
  pose_input.fixed_legs = ci->fixed_legs;
  pose_input.fixed_torso = ci->fixed_torso;
  pose_input.lerp_angles = entity->lerp_angles;
  pose_input.time_ms = presenter_time(presenter);
  pose_input.frame_time_ms = host->state->frame_time;
  pose_input.swing_speed = options.swing_speed;
  qa_q3_player_pose pose = qa_q3_calculate_player_pose(&entity->player.pose, pose_input);
  legs.axis = pose.legs;
  torso.axis = pose.torso;
  head.axis = pose.head;
  animation(presenter, entity, ci, &legs, &torso, options);
  if (body_visible) sprites(presenter, entity, ci, options);
  shadow_result shadow_value = body_visible ? shadow(presenter, entity, options) : (shadow_result){false, 0};
  if (body_visible) splash(presenter, entity, options);
  if (options.shadows == 3 && shadow_value.visible) {
    render_flags = to_int32(render_flags) | QA_Q3_PRES_RF_SHADOW_PLANE;
  }
  render_flags = to_int32(render_flags) | QA_Q3_PRES_RF_LIGHTING_ORIGIN;
  if (host->product == QA_Q3_BASE_PRODUCT_MISSIONPACK && options.game_type == QA_Q3_BASE_GT_HARVESTER) {
    tokens(presenter, entity, ci, render_flags, host->mission_media);
  }
  legs.model = ci->legs_model;
  legs.custom_skin = ci->legs_skin;
  legs.origin = entity->lerp_origin;
  legs.lighting_origin = entity->lerp_origin;
  legs.shadow_plane = shadow_value.plane;
  legs.shading.render_flags = render_flags;
  legs.old_origin = legs.origin;
  qa_q3_pres_player_presenter_add_ref_entity_with_powerups(presenter, &legs, &entity->current_state,
      ci->team, "lower");
  if (legs.model.kind == QA_Q3_PRES_SCENE_MODEL_DEFAULT ||
      ci->torso_model.kind == QA_Q3_PRES_SCENE_MODEL_DEFAULT) {
    return;
  }
  torso.model = ci->torso_model;
  torso.custom_skin = ci->torso_skin;
  torso.lighting_origin = entity->lerp_origin;
  qa_q3_pres_position_rotated_entity_on_tag(&torso, &legs, &ci->legs_model, "tag_torso");
  torso.shadow_plane = shadow_value.plane;
  torso.shading.render_flags = render_flags;
  qa_q3_pres_player_presenter_add_ref_entity_with_powerups(presenter, &torso, &entity->current_state,
      ci->team, "upper");
  if (host->product == QA_Q3_BASE_PRODUCT_MISSIONPACK) {
    mission_powerups(presenter, entity, ci, &torso, host->mission_media);
  }
  if (ci->head_model.kind == QA_Q3_PRES_SCENE_MODEL_DEFAULT) return;
  head.model = ci->head_model;
  head.custom_skin = ci->head_skin;
  head.lighting_origin = entity->lerp_origin;
  qa_q3_pres_position_rotated_entity_on_tag(&head, &torso, &ci->torso_model, "tag_head");
  head.shadow_plane = shadow_value.plane;
  head.shading.render_flags = render_flags;
  qa_q3_pres_player_presenter_add_ref_entity_with_powerups(presenter, &head, &entity->current_state,
      ci->team, "head");
  if (body_visible && host->product == QA_Q3_BASE_PRODUCT_MISSIONPACK) {
    breath(presenter, entity, &head, options, host->mission_media);
    dust(presenter, entity, options, host->mission_media);
  }
  host->add_player_weapon(host->ctx, &torso, NULL, entity, ci->team);
  powerups(presenter, entity, &torso, ci, options);
}

/* ---------------- ClientInfoStore ---------------- */

struct qa_q3_pres_client_info_store {
  qa_q3_pres_client_info_host host;
  qa_q3_pres_client_info *slots[64];
  double requests[64];
  qa_common_parse_state animation_parser;
  double lifecycle;
};
typedef qa_q3_pres_client_info_store store_t;
typedef enum load_status { LOAD_OK, LOAD_SUPERSEDED, LOAD_OOM } load_status;
typedef struct load_check {
  store_t *store;
  double index, request, lifecycle;
  bool has_index; /* loadDeferredPlayers uses its own request slot */
} load_check;
static bool superseded(load_check *check) {
  if (check->lifecycle != check->store->lifecycle) return true;
  return check->store->requests[(size_t)check->index] != check->request;
}
#define CHECKED(check) do { if (superseded(check)) return LOAD_SUPERSEDED; } while (0)
#define STR(text) ((text) != NULL ? (text) : "")

static void fmt_index(char *buffer, size_t size, double value) {
  if (value == trunc(value) && fabs(value) < 1e21) snprintf(buffer, size, "%lld", (long long)value);
  else snprintf(buffer, size, "%g", value);
}
qa_q3_pres_client_info *qa_q3_pres_client_info_store_client_info(store_t *store, double index) {
  if (index != trunc(index) || index < 0 || index >= 64) {
    char wanted[32];
    fmt_index(wanted, sizeof wanted, index);
    qa_range_error("Player index %s outside 64", wanted);
  }
  return store->slots[(size_t)index];
}
qa_q3_pres_client_info *qa_q3_pres_client_info_store_client_info_ctx(void *ctx, double index) {
  return qa_q3_pres_client_info_store_client_info(ctx, index);
}
bool qa_q3_pres_client_info_store_new_client_info_ctx(void *ctx, double client_num, const char *config) {
  return qa_q3_pres_client_info_store_new_client_info(ctx, client_num, config);
}

bool qa_q3_pres_client_info_store_init(store_t **out, qa_q3_pres_client_info_host host,
    qa_q3_pres_client_info *const *slots, size_t slot_count) {
  if (slot_count != 64) {
    qa_range_error("ClientInfoStore requires 64 distinct canonical client slots");
  }
  for (size_t i = 0; i < 64; i++) {
    for (size_t j = i + 1; j < 64; j++) {
      if (slots[i] == slots[j]) {
        qa_range_error("ClientInfoStore requires 64 distinct canonical client slots");
      }
    }
  }
  store_t *store = calloc(1, sizeof *store);
  if (store == NULL) return false;
  store->host = host;
  for (size_t i = 0; i < 64; i++) store->slots[i] = slots[i];
  qa_common_parse_state_init(&store->animation_parser);
  *out = store;
  return true;
}
void qa_q3_pres_client_info_store_destroy(store_t *store) { free(store); }
bool qa_q3_pres_client_info_store_reset(store_t *store) {
  store->lifecycle++;
  qa_q3_pres_client_info fresh;
  qa_q3_pres_client_info_init(&fresh);
  bool ok = true;
  for (size_t i = 0; i < 64 && ok; i++) ok = qa_q3_pres_client_info_copy_from(store->slots[i], &fresh);
  qa_q3_pres_client_info_destroy(&fresh);
  return ok;
}
qa_pcm_sound *qa_q3_pres_client_info_store_custom_sound(store_t *store, double index, const char *name) {
  if (name[0] != '*') return store->host.sound(store->host.ctx, name, false);
  qa_q3_pres_client_info *slot =
      qa_q3_pres_client_info_store_client_info(store, index < 0 || index >= 64 ? 0 : index);
  for (size_t i = 0; i < qa_q3_pres_custom_sound_name_count; i++) {
    if (strcmp(qa_q3_pres_custom_sound_names[i], name) == 0) return slot->sounds[i];
  }
  char message[256];
  snprintf(message, sizeof message, "Unknown custom sound: %.200s", name);
  qa_common_error(QA_COMMON_ERROR_DROP, message);
}

static const char *team_name(qa_q3_pres_client_info *ci, qa_q3_pres_client_info_settings settings) {
  return settings.game_type >= QA_Q3_BASE_GT_TEAM ? (ci->team == QA_Q3_BASE_TEAM_BLUE ? "blue" : "red") : "default";
}
static load_status filename_of(store_t *store, double length, const char *format,
    qa_q3_game_format_arg *args, size_t arg_count, load_check *check, char *out, size_t out_size) {
  char *filename = qa_q3_game_format(format, args, arg_count, 32000);
  if (filename == NULL) return LOAD_OOM;
  size_t filename_length = strlen(filename);
  if ((double)filename_length >= length) {
    qa_q3_game_format_arg print_args[2] = {qa_q3_game_format_number((double)filename_length),
                                           qa_q3_game_format_number(length)};
    char *message = qa_q3_game_format("Com_sprintf: overflow of %i in %i\n", print_args, 2, 32000);
    if (message == NULL) {
      free(filename);
      return LOAD_OOM;
    }
    store->host.print(store->host.ctx, message);
    free(message);
    if (superseded(check)) {
      free(filename);
      return LOAD_SUPERSEDED;
    }
  }
  size_t keep = (size_t)length - 1;
  if (keep >= out_size) keep = out_size - 1;
  size_t take = filename_length < keep ? filename_length : keep;
  memcpy(out, filename, take);
  out[take] = '\0';
  free(filename);
  return LOAD_OK;
}
static load_status exists(store_t *store, const char *path, load_check *check, bool *out_found) {
  *out_found = false;
  if (!store->host.assets.has(store->host.assets.ctx, path)) return LOAD_OK;
  uint8_t *bytes = NULL;
  size_t length = 0;
  if (!store->host.assets.read(store->host.assets.ctx, path, &bytes, &length)) return LOAD_OOM;
  free(bytes);
  CHECKED(check);
  *out_found = length > 0;
  return LOAD_OK;
}
typedef struct client_file { bool found; char path[128]; } client_file;
static load_status find_model(store_t *store, qa_q3_pres_client_info *ci,
    qa_q3_pres_client_info_settings settings, const char *team_name_arg, const char *model,
    const char *skin, const char *base, const char *ext, load_check *check, client_file *out) {
  const char *team = team_name(ci, settings);
  out->found = false;
  out->path[0] = '\0';
  const char *folders[] = {"", "characters/"};
  for (size_t folder_index = 0; folder_index < 2; folder_index++) {
    const char *prefixes[2] = {team_name_arg, ""};
    size_t prefix_count = team_name_arg[0] != '\0' ? 2 : 1;
    if (prefix_count == 1) prefixes[0] = "";
    for (size_t prefix_index = 0; prefix_index < prefix_count; prefix_index++) {
      qa_q3_game_format_arg args[7] = {
          qa_q3_game_format_string(folders[folder_index]), qa_q3_game_format_string(model),
          qa_q3_game_format_string(prefixes[prefix_index]), qa_q3_game_format_string(base),
          qa_q3_game_format_string(skin), qa_q3_game_format_string(team), qa_q3_game_format_string(ext)};
      load_status status = filename_of(store, 64, "models/players/%s%s/%s%s_%s_%s.%s", args, 7, check,
          out->path, sizeof out->path);
      if (status != LOAD_OK) return status;
      bool found = false;
      status = exists(store, out->path, check, &found);
      if (status != LOAD_OK) return status;
      CHECKED(check);
      if (found) { out->found = true; return LOAD_OK; }
      args[4] = qa_q3_game_format_string(settings.game_type >= QA_Q3_BASE_GT_TEAM ? team : skin);
      qa_q3_game_format_arg args2[6] = {
          qa_q3_game_format_string(folders[folder_index]), qa_q3_game_format_string(model),
          qa_q3_game_format_string(prefixes[prefix_index]), qa_q3_game_format_string(base),
          qa_q3_game_format_string(settings.game_type >= QA_Q3_BASE_GT_TEAM ? team : skin),
          qa_q3_game_format_string(ext)};
      status = filename_of(store, 64, "models/players/%s%s/%s%s_%s.%s", args2, 6, check,
          out->path, sizeof out->path);
      if (status != LOAD_OK) return status;
      status = exists(store, out->path, check, &found);
      if (status != LOAD_OK) return status;
      CHECKED(check);
      if (found) { out->found = true; return LOAD_OK; }
    }
  }
  return LOAD_OK;
}
static load_status find_head(store_t *store, qa_q3_pres_client_info *ci,
    qa_q3_pres_client_info_settings settings, const char *team_name_arg, const char *model,
    const char *skin, const char *base, const char *ext, double length, load_check *check, client_file *out) {
  const char *team = team_name(ci, settings);
  bool starred = model[0] == '*';
  const char *name = starred ? model + 1 : model;
  out->found = false;
  out->path[0] = '\0';
  const char *starred_folders[] = {"heads/"};
  const char *plain_folders[] = {"", "heads/"};
  const char *const *folders = starred ? starred_folders : plain_folders;
  size_t folder_count = starred ? 1 : 2;
  for (size_t folder_index = 0; folder_index < folder_count; folder_index++) {
    const char *prefixes[2] = {"", ""};
    size_t prefix_count = 1;
    if (team_name_arg[0] != '\0') { prefixes[0] = team_name_arg; prefixes[1] = ""; prefix_count = 2; }
    for (size_t prefix_index = 0; prefix_index < prefix_count; prefix_index++) {
      qa_q3_game_format_arg args[7] = {
          qa_q3_game_format_string(folders[folder_index]), qa_q3_game_format_string(name),
          qa_q3_game_format_string(skin), qa_q3_game_format_string(prefixes[prefix_index]),
          qa_q3_game_format_string(base), qa_q3_game_format_string(team), qa_q3_game_format_string(ext)};
      load_status status = filename_of(store, length, "models/players/%s%s/%s/%s%s_%s.%s", args, 7,
          check, out->path, sizeof out->path);
      if (status != LOAD_OK) return status;
      bool found = false;
      status = exists(store, out->path, check, &found);
      if (status != LOAD_OK) return status;
      CHECKED(check);
      if (found) { out->found = true; return LOAD_OK; }
      qa_q3_game_format_arg args2[6] = {
          qa_q3_game_format_string(folders[folder_index]), qa_q3_game_format_string(name),
          qa_q3_game_format_string(prefixes[prefix_index]), qa_q3_game_format_string(base),
          qa_q3_game_format_string(settings.game_type >= QA_Q3_BASE_GT_TEAM ? team : skin),
          qa_q3_game_format_string(ext)};
      status = filename_of(store, length, "models/players/%s%s/%s%s_%s.%s", args2, 6, check,
          out->path, sizeof out->path);
      if (status != LOAD_OK) return status;
      status = exists(store, out->path, check, &found);
      if (status != LOAD_OK) return status;
      CHECKED(check);
      if (found) { out->found = true; return LOAD_OK; }
    }
  }
  return LOAD_OK;
}
static load_status load_skin(store_t *store, qa_q3_pres_client_info *ci,
    qa_q3_pres_client_info_settings settings, const char *team, const char *model, const char *skin,
    const char *head, const char *head_skin, load_check *check, bool *out_loaded) {
  client_file file;
  load_status status = find_model(store, ci, settings, team, model, skin, "lower", "skin", check, &file);
  if (status != LOAD_OK) return status;
  CHECKED(check);
  if (file.found) {
    if (!store->host.register_skin(store->host.resources_ctx, file.path, &ci->legs_skin)) return LOAD_OOM;
    CHECKED(check);
  }
  if (ci->legs_skin == NULL) {
    char message[192];
    snprintf(message, sizeof message, "Leg skin load failure: %.150s\n", file.path);
    store->host.print(store->host.ctx, message);
  }
  status = find_model(store, ci, settings, team, model, skin, "upper", "skin", check, &file);
  if (status != LOAD_OK) return status;
  CHECKED(check);
  if (file.found) {
    if (!store->host.register_skin(store->host.resources_ctx, file.path, &ci->torso_skin)) return LOAD_OOM;
    CHECKED(check);
  }
  if (ci->torso_skin == NULL) {
    char message[192];
    snprintf(message, sizeof message, "Torso skin load failure: %.150s\n", file.path);
    store->host.print(store->host.ctx, message);
  }
  status = find_head(store, ci, settings, team, head, head_skin, "head", "skin", 64, check, &file);
  if (status != LOAD_OK) return status;
  CHECKED(check);
  if (file.found) {
    if (!store->host.register_skin(store->host.resources_ctx, file.path, &ci->head_skin)) return LOAD_OOM;
    CHECKED(check);
  }
  if (ci->head_skin == NULL) {
    char message[192];
    snprintf(message, sizeof message, "Head skin load failure: %.150s\n", file.path);
    store->host.print(store->host.ctx, message);
  }
  *out_loaded = ci->legs_skin != NULL && ci->torso_skin != NULL && ci->head_skin != NULL;
  return LOAD_OK;
}
typedef struct anim_print_ctx { store_t *store; load_check *check; bool superseded_seen; } anim_print_ctx;
static void anim_print(void *ctx, const char *message) {
  anim_print_ctx *print = ctx;
  print->store->host.print(print->store->host.ctx, message);
  if (superseded(print->check)) print->superseded_seen = true;
}
static load_status load_animation(store_t *store, qa_q3_pres_client_info *ci, const char *path,
    load_check *check, bool *out_loaded) {
  *out_loaded = false;
  if (!store->host.assets.has(store->host.assets.ctx, path)) return LOAD_OK;
  uint8_t *bytes = NULL;
  size_t length = 0;
  if (!store->host.assets.read(store->host.assets.ctx, path, &bytes, &length)) return LOAD_OOM;
  if (superseded(check)) {
    free(bytes);
    return LOAD_SUPERSEDED;
  }
  if (length == 0) {
    free(bytes);
    return LOAD_OK;
  }
  char *text = malloc(length + 1);
  if (text == NULL) {
    free(bytes);
    return LOAD_OOM;
  }
  memcpy(text, bytes, length);
  text[length] = '\0';
  free(bytes);
  qa_q3_player_animation_target target = qa_q3_pres_client_info_animation_target(ci);
  anim_print_ctx print = {store, check, false};
  qa_q3_player_animation_parse_context context = {&target, &store->animation_parser, &print, anim_print};
  qa_q3_player_animation_config config;
  /* Landed parse errors abort (qa_text_parse_error); TS would catch
   * PlayerAnimationParseError and return false here. */
  qa_q3_parse_player_animation_config(text, path, &context, &config);
  free(text);
  ci->footsteps = target.footsteps;
  ci->head_offset = target.head_offset;
  ci->gender = target.gender;
  ci->fixed_legs = target.fixed_legs;
  ci->fixed_torso = target.fixed_torso;
  qa_q3_player_animation_config_destroy(&config);
  if (print.superseded_seen || superseded(check)) return LOAD_SUPERSEDED;
  *out_loaded = true;
  return LOAD_OK;
}
static load_status register_model_path(store_t *store, const char *format, const char *arg1,
    const char *arg2, load_check *check, char *path_out, qa_q3_pres_scene_model *out) {
  qa_q3_game_format_arg args[2] = {qa_q3_game_format_string(arg1),
                                   arg2 != NULL ? qa_q3_game_format_string(arg2) : qa_q3_game_format_null()};
  load_status status = filename_of(store, 128, format, args, arg2 != NULL ? 2 : 1, check, path_out, 128);
  if (status != LOAD_OK) return status;
  if (!store->host.register_model(store->host.resources_ctx, path_out, out)) return LOAD_OOM;
  CHECKED(check);
  return LOAD_OK;
}
static load_status load_model(store_t *store, qa_q3_pres_client_info *ci,
    qa_q3_pres_client_info_settings settings, const char *model, const char *skin,
    const char *head_model, const char *head_skin, const char *team, load_check *check, bool *out_loaded) {
  *out_loaded = false;
  const char *head = head_model[0] != '\0' ? head_model : model;
  char path[128];
  load_status status = register_model_path(store, "models/players/%s/lower.md3", model, NULL, check,
      path, &ci->legs_model);
  if (status != LOAD_OK) return status;
  if (ci->legs_model.kind == QA_Q3_PRES_SCENE_MODEL_DEFAULT) {
    status = register_model_path(store, "models/players/characters/%s/lower.md3", model, NULL, check,
        path, &ci->legs_model);
    if (status != LOAD_OK) return status;
  }
  if (ci->legs_model.kind == QA_Q3_PRES_SCENE_MODEL_DEFAULT) {
    qa_q3_game_format_arg args[1] = {qa_q3_game_format_string(path)};
    char *message = qa_q3_game_format("Failed to load model file %s\n", args, 1, 32000);
    if (message == NULL) return LOAD_OOM;
    store->host.print(store->host.ctx, message);
    free(message);
    return LOAD_OK;
  }
  status = register_model_path(store, "models/players/%s/upper.md3", model, NULL, check, path,
      &ci->torso_model);
  if (status != LOAD_OK) return status;
  if (ci->torso_model.kind == QA_Q3_PRES_SCENE_MODEL_DEFAULT) {
    status = register_model_path(store, "models/players/characters/%s/upper.md3", model, NULL, check,
        path, &ci->torso_model);
    if (status != LOAD_OK) return status;
  }
  if (ci->torso_model.kind == QA_Q3_PRES_SCENE_MODEL_DEFAULT) {
    qa_q3_game_format_arg args[1] = {qa_q3_game_format_string(path)};
    char *message = qa_q3_game_format("Failed to load model file %s\n", args, 1, 32000);
    if (message == NULL) return LOAD_OOM;
    store->host.print(store->host.ctx, message);
    free(message);
    return LOAD_OK;
  }
  if (head[0] == '*') {
    status = register_model_path(store, "models/players/heads/%s/%s.md3", head_model + 1, head_model + 1,
        check, path, &ci->head_model);
  } else {
    status = register_model_path(store, "models/players/%s/head.md3", head, NULL, check, path,
        &ci->head_model);
  }
  if (status != LOAD_OK) return status;
  if (ci->head_model.kind == QA_Q3_PRES_SCENE_MODEL_DEFAULT && head[0] != '*') {
    status = register_model_path(store, "models/players/heads/%s/%s.md3", head_model, head_model, check,
        path, &ci->head_model);
    if (status != LOAD_OK) return status;
  }
  if (ci->head_model.kind == QA_Q3_PRES_SCENE_MODEL_DEFAULT) {
    qa_q3_game_format_arg args[1] = {qa_q3_game_format_string(path)};
    char *message = qa_q3_game_format("Failed to load model file %s\n", args, 1, 32000);
    if (message == NULL) return LOAD_OOM;
    store->host.print(store->host.ctx, message);
    free(message);
    return LOAD_OK;
  }
  bool loaded_skin = false;
  status = load_skin(store, ci, settings, team, model, skin, head, head_skin, check, &loaded_skin);
  if (status != LOAD_OK) return status;
  CHECKED(check);
  if (!loaded_skin) {
    char message[512];
    if (team[0] == '\0') {
      snprintf(message, sizeof message, "Failed to load skin file: %.80s : %.80s, %.80s : %.80s\n",
          model, skin, head, head_skin);
      store->host.print(store->host.ctx, message);
      return LOAD_OK;
    }
    snprintf(message, sizeof message, "Failed to load skin file: %.80s : %.80s : %.80s, %.80s : %.80s\n",
        team, model, skin, head, head_skin);
    store->host.print(store->host.ctx, message);
    char fallback_team[128];
    qa_q3_game_format_arg team_args[1] = {
        qa_q3_game_format_string(ci->team == QA_Q3_BASE_TEAM_BLUE ? "Pagans" : "Stroggs")};
    status = filename_of(store, 128, "%s/", team_args, 1, check, fallback_team, sizeof fallback_team);
    if (status != LOAD_OK) return status;
    status = load_skin(store, ci, settings, fallback_team, model, skin, head, head_skin, check, &loaded_skin);
    if (status != LOAD_OK) return status;
    CHECKED(check);
    if (!loaded_skin) {
      snprintf(message, sizeof message, "Failed to load skin file: %.80s : %.80s : %.80s, %.80s : %.80s\n",
          fallback_team, model, skin, head, head_skin);
      store->host.print(store->host.ctx, message);
      return LOAD_OK;
    }
  }
  {
    qa_q3_game_format_arg args[1] = {qa_q3_game_format_string(model)};
    status = filename_of(store, 128, "models/players/%s/animation.cfg", args, 1, check, path, sizeof path);
    if (status != LOAD_OK) return status;
  }
  bool animated = false;
  status = load_animation(store, ci, path, check, &animated);
  if (status != LOAD_OK) return status;
  CHECKED(check);
  if (!animated) {
    qa_q3_game_format_arg args[1] = {qa_q3_game_format_string(model)};
    status = filename_of(store, 128, "models/players/characters/%s/animation.cfg", args, 1, check,
        path, sizeof path);
    if (status != LOAD_OK) return status;
    status = load_animation(store, ci, path, check, &animated);
    if (status != LOAD_OK) return status;
    CHECKED(check);
    if (!animated) {
      qa_q3_game_format_arg print_args[1] = {qa_q3_game_format_string(path)};
      char *message = qa_q3_game_format("Failed to load animation file %s\n", print_args, 1, 32000);
      if (message == NULL) return LOAD_OOM;
      store->host.print(store->host.ctx, message);
      free(message);
      return LOAD_OK;
    }
  }
  client_file icon;
  status = find_head(store, ci, settings, team, head, head_skin, "icon", "skin", 128, check, &icon);
  if (status != LOAD_OK) return status;
  CHECKED(check);
  if (!icon.found) {
    status = find_head(store, ci, settings, team, head, head_skin, "icon", "tga", 128, check, &icon);
    if (status != LOAD_OK) return status;
    CHECKED(check);
  }
  if (icon.found) {
    if (!store->host.register_shader_no_mip(store->host.ctx, icon.path, &ci->model_icon)) return LOAD_OOM;
    CHECKED(check);
  }
  *out_loaded = ci->model_icon != NULL;
  return LOAD_OK;
}

static void qpath_copy(char *out, size_t out_size, const char *value) {
  size_t take = strlen(value);
  if (take > 63) take = 63;
  if (take >= out_size) take = out_size - 1;
  memcpy(out, value, take);
  out[take] = '\0';
}
static load_status load_client(store_t *store, qa_q3_pres_client_info *ci,
    qa_q3_pres_client_info_settings settings, load_check *check) {
  bool mission = store->host.state->product == QA_Q3_BASE_PRODUCT_MISSIONPACK;
  const char *team_model = mission ? "james" : "sarge";
  const char *team_head = mission ? "*james" : "sarge";
  char team[80] = "";
  if (mission && settings.game_type >= QA_Q3_BASE_GT_TEAM) {
    qpath_copy(team, sizeof team,
        ci->team == QA_Q3_BASE_TEAM_BLUE ? STR(settings.blue_team_name) : STR(settings.red_team_name));
  }
  if (team[0] != '\0') strcat(team, "/");
  bool loaded = false;
  load_status status = load_model(store, ci, settings, STR(ci->model_name), STR(ci->skin_name),
      STR(ci->head_model_name), STR(ci->head_skin_name), team, check, &loaded);
  if (status != LOAD_OK) return status;
  CHECKED(check);
  if (!loaded) {
    if (settings.build_script) {
      char message[320];
      snprintf(message, sizeof message, "CG_RegisterClientModelname( %.70s, %.70s, %.70s, %.70s %.70s ) failed",
          STR(ci->model_name), STR(ci->skin_name), STR(ci->head_model_name), STR(ci->head_skin_name), team);
      qa_common_error(QA_COMMON_ERROR_DROP, message);
    }
    bool fallback_loaded = false;
    if (settings.game_type >= QA_Q3_BASE_GT_TEAM) {
      const char *fallback_team = ci->team == QA_Q3_BASE_TEAM_BLUE ? "Pagans" : "Stroggs";
      status = load_model(store, ci, settings, team_model, STR(ci->skin_name), team_head,
          STR(ci->skin_name), fallback_team, check, &fallback_loaded);
      if (status != LOAD_OK) return status;
      CHECKED(check);
      if (!fallback_loaded) {
        char message[192];
        snprintf(message, sizeof message, "DEFAULT_TEAM_MODEL / skin (%.80s/%.80s) failed to register",
            team_model, STR(ci->skin_name));
        qa_common_error(QA_COMMON_ERROR_DROP, message);
      }
    } else {
      status = load_model(store, ci, settings, "sarge", "default", "sarge", "default", team, check,
          &fallback_loaded);
      if (status != LOAD_OK) return status;
      CHECKED(check);
      if (!fallback_loaded) {
        qa_common_error(QA_COMMON_ERROR_DROP, "DEFAULT_MODEL (sarge) failed to register");
      }
    }
  }
  qa_q3_pres_model_tag tag;
  ci->new_anims = qa_q3_pres_lerp_model_tag(&ci->torso_model, "tag_flag", 0, 0, 1, &tag);
  const char *fallback = qa_q3_custom_sound_fallback(store->host.state->product,
      settings.game_type >= QA_Q3_BASE_GT_TEAM);
  qa_pcm_sound *sounds[QA_Q3_PRES_CLIENT_INFO_SOUND_COUNT];
  memset(sounds, 0, sizeof sounds);
  for (size_t index = 0; index < qa_q3_pres_custom_sound_name_count; index++) {
    const char *name = qa_q3_pres_custom_sound_names[index] + 1;
    char path[160];
    if (loaded) {
      snprintf(path, sizeof path, "sound/player/%.80s/%.60s", STR(ci->model_name), name);
      if (!store->host.register_sound(store->host.ctx, path, false, &sounds[index])) return LOAD_OOM;
      CHECKED(check);
    }
    if (sounds[index] == NULL) {
      snprintf(path, sizeof path, "sound/player/%.80s/%.60s", fallback, name);
      if (!store->host.register_sound(store->host.ctx, path, false, &sounds[index])) return LOAD_OOM;
      CHECKED(check);
    }
  }
  memcpy(ci->sounds, sounds, sizeof ci->sounds);
  ci->deferred = false;
  return LOAD_OK;
}
static bool same_string(const char *a, const char *b) {
  const unsigned char *left = (const unsigned char *)STR(a);
  const unsigned char *right = (const unsigned char *)STR(b);
  for (;;) {
    unsigned char ca = *left, cb = *right;
    if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca + 32);
    if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb + 32);
    if (ca != cb) return false;
    if (ca == 0) return true;
    left++;
    right++;
  }
}
static bool copy_model(qa_q3_pres_client_info *from, qa_q3_pres_client_info *to) {
  to->head_offset = from->head_offset;
  to->footsteps = from->footsteps;
  to->gender = from->gender;
  to->legs_model = from->legs_model;
  to->legs_skin = from->legs_skin;
  to->torso_model = from->torso_model;
  to->torso_skin = from->torso_skin;
  to->head_model = from->head_model;
  to->head_skin = from->head_skin;
  to->model_icon = from->model_icon;
  to->new_anims = from->new_anims;
  qa_q3_pres_client_info_set_animations(to, from->animations, NULL, QA_Q3_TOTAL_ANIMATION_COUNT);
  memcpy(to->sounds, from->sounds, sizeof to->sounds);
  /* CG_CopyClientInfoModel deliberately does not copy fixed legs/torso. */
  return true;
}
static bool reuse(store_t *store, qa_q3_pres_client_info *ci, qa_q3_pres_client_info_settings settings) {
  for (double i = 0; i < settings.max_clients; i++) {
    qa_q3_pres_client_info *match = qa_q3_pres_client_info_store_client_info(store, i);
    if (match->info_valid && !match->deferred && same_string(ci->model_name, match->model_name) &&
        same_string(ci->skin_name, match->skin_name) && same_string(ci->head_model_name, match->head_model_name) &&
        same_string(ci->head_skin_name, match->head_skin_name) && same_string(ci->blue_team, match->blue_team) &&
        same_string(ci->red_team, match->red_team) &&
        (settings.game_type < QA_Q3_BASE_GT_TEAM || ci->team == match->team)) {
      ci->deferred = false;
      return copy_model(match, ci);
    }
  }
  return false;
}
static load_status defer(store_t *store, qa_q3_pres_client_info *ci,
    qa_q3_pres_client_info_settings settings, load_check *check) {
  size_t candidate_count = 0;
  if (settings.max_clients > 0) {
    candidate_count = settings.max_clients >= 64 ? 64 : (size_t)settings.max_clients;
  }
  for (size_t i = 0; i < candidate_count; i++) {
    qa_q3_pres_client_info *match = store->slots[i];
    if (match->info_valid && !match->deferred && same_string(ci->skin_name, match->skin_name) &&
        same_string(ci->model_name, match->model_name) &&
        (settings.game_type < QA_Q3_BASE_GT_TEAM || ci->team == match->team)) {
      return load_client(store, ci, settings, check);
    }
  }
  for (size_t i = 0; i < candidate_count; i++) {
    qa_q3_pres_client_info *match = store->slots[i];
    if (match->info_valid && (settings.game_type < QA_Q3_BASE_GT_TEAM ||
        (!match->deferred && same_string(ci->skin_name, match->skin_name) && ci->team == match->team))) {
      ci->deferred = true;
      (void)copy_model(match, ci);
      return LOAD_OK;
    }
  }
  if (settings.game_type < QA_Q3_BASE_GT_TEAM) {
    store->host.print(store->host.ctx, "CG_SetDeferredClientInfo: no valid clients!\n");
  }
  return load_client(store, ci, settings, check);
}
static qa_vec3 color_of(const char *value) {
  int32_t bits = qa_game_atoi(value);
  return bits < 1 || bits > 7 ? qa_math_vec3(1, 1, 1)
      : qa_math_vec3((bits & 4) != 0 ? 1 : 0, (bits & 2) != 0 ? 1 : 0, (bits & 1) != 0 ? 1 : 0);
}
static void model_skin(const char *value, char *model, char *skin) {
  char path[64];
  qpath_copy(path, sizeof path, value);
  char *slash = strchr(path, '/');
  if (slash == NULL) {
    strcpy(model, path);
    strcpy(skin, "default");
  } else {
    size_t model_length = (size_t)(slash - path);
    memcpy(model, path, model_length);
    model[model_length] = '\0';
    strcpy(skin, slash + 1);
  }
}
static bool set_qpath_field(char **slot, const char *value) {
  char trimmed[64];
  qpath_copy(trimmed, sizeof trimmed, value);
  return qa_q3_pres_set_string(slot, trimmed);
}
static bool set_sliced_field(char **slot, const char *value, size_t length) {
  char trimmed[64];
  size_t take = strlen(value);
  if (take > length) take = length;
  if (take >= sizeof trimmed) take = sizeof trimmed - 1;
  memcpy(trimmed, value, take);
  trimmed[take] = '\0';
  return qa_q3_pres_set_string(slot, trimmed);
}
static char *info_value(const char *config, const char *key) {
  return qa_info_value_for_key(config, key);
}

bool qa_q3_pres_client_info_store_new_client_info(store_t *store, double index, const char *configstring) {
  qa_q3_pres_client_info *slot = qa_q3_pres_client_info_store_client_info(store, index);
  double request = store->requests[(size_t)index] + 1;
  store->requests[(size_t)index] = request;
  load_check check = {store, index, request, store->lifecycle, true};
  qa_q3_pres_client_info next;
  qa_q3_pres_client_info_init(&next);
  bool ok = true;
  if (configstring == NULL || configstring[0] == '\0') {
    ok = qa_q3_pres_client_info_copy_from(slot, &next);
    qa_q3_pres_client_info_destroy(&next);
    return ok;
  }
  qa_q3_pres_client_info_settings settings = store->host.settings(store->host.ctx);
  char *value = NULL;
  value = info_value(configstring, "n");
  ok = value != NULL && set_qpath_field(&next.name, value);
  free(value);
  value = info_value(configstring, "c1");
  next.color1 = color_of(value != NULL ? value : "");
  free(value);
  value = info_value(configstring, "c2");
  next.color2 = color_of(value != NULL ? value : "");
  free(value);
  value = info_value(configstring, "skill");
  next.bot_skill = qa_game_atoi(value != NULL ? value : "");
  free(value);
  value = info_value(configstring, "hc");
  next.handicap = qa_game_atoi(value != NULL ? value : "");
  free(value);
  value = info_value(configstring, "w");
  next.wins = qa_game_atoi(value != NULL ? value : "");
  free(value);
  value = info_value(configstring, "l");
  next.losses = qa_game_atoi(value != NULL ? value : "");
  free(value);
  value = info_value(configstring, "t");
  int32_t team = qa_game_atoi(value != NULL ? value : "");
  free(value);
  if (team != QA_Q3_BASE_TEAM_FREE && team != QA_Q3_BASE_TEAM_RED && team != QA_Q3_BASE_TEAM_BLUE &&
      team != QA_Q3_BASE_TEAM_SPECTATOR) {
    qa_q3_pres_client_info_destroy(&next);
    qa_range_error("Invalid player team %lld", (long long)team);
  }
  next.team = (qa_q3_base_team)team;
  value = info_value(configstring, "tt");
  next.team_task = qa_game_atoi(value != NULL ? value : "");
  free(value);
  value = info_value(configstring, "tl");
  next.team_leader = qa_game_atoi(value != NULL ? value : "") != 0;
  free(value);
  value = info_value(configstring, "g_redteam");
  ok = ok && value != NULL && set_sliced_field(&next.red_team, value, 31);
  free(value);
  value = info_value(configstring, "g_blueteam");
  ok = ok && value != NULL && set_sliced_field(&next.blue_team, value, 31);
  free(value);
  if (!ok) {
    qa_q3_pres_client_info_destroy(&next);
    return false;
  }
  const char *forced = store->host.state->product == QA_Q3_BASE_PRODUCT_MISSIONPACK ? "james" : "sarge";
  char model_buffer[64], skin_buffer[64], head_buffer[64], head_skin_buffer[64];
  char *model_value = info_value(configstring, "model");
  char *head_value = info_value(configstring, "hmodel");
  const char *model_source = settings.force_model
      ? (settings.game_type >= QA_Q3_BASE_GT_TEAM ? forced : STR(settings.model))
      : STR(model_value);
  const char *head_source = settings.force_model
      ? (settings.game_type >= QA_Q3_BASE_GT_TEAM ? forced : STR(settings.head_model))
      : STR(head_value);
  model_skin(model_source, model_buffer, skin_buffer);
  model_skin(head_source, head_buffer, head_skin_buffer);
  ok = qa_q3_pres_set_string(&next.model_name, model_buffer) &&
       qa_q3_pres_set_string(&next.skin_name, skin_buffer) &&
       qa_q3_pres_set_string(&next.head_model_name, head_buffer) &&
       qa_q3_pres_set_string(&next.head_skin_name, head_skin_buffer);
  if (ok && settings.force_model && settings.game_type >= QA_Q3_BASE_GT_TEAM) {
    const char *slash = model_value != NULL ? strchr(model_value, '/') : NULL;
    if (slash != NULL) ok = set_qpath_field(&next.skin_name, slash + 1);
    slash = head_value != NULL ? strchr(head_value, '/') : NULL;
    if (ok && slash != NULL) ok = set_qpath_field(&next.head_skin_name, slash + 1);
  }
  free(model_value);
  free(head_value);
  if (!ok) {
    qa_q3_pres_client_info_destroy(&next);
    return false;
  }
  load_status status = LOAD_OK;
  if (!reuse(store, &next, settings)) {
    bool force_defer = store->host.memory_remaining(store->host.ctx) < 4000000;
    if (force_defer || (settings.defer_players && !settings.build_script && !settings.loading)) {
      status = defer(store, &next, settings, &check);
      if (status == LOAD_OK && superseded(&check)) status = LOAD_SUPERSEDED;
      if (status == LOAD_OK && force_defer) {
        store->host.print(store->host.ctx, "Memory is low.  Using deferred model.\n");
        next.deferred = false;
      }
    } else {
      status = load_client(store, &next, settings, &check);
    }
  }
  if (status == LOAD_SUPERSEDED) {
    qa_q3_pres_client_info_destroy(&next);
    return true;
  }
  if (status == LOAD_OOM) {
    qa_q3_pres_client_info_destroy(&next);
    return false;
  }
  if (check.lifecycle == store->lifecycle && store->requests[(size_t)index] == request) {
    next.info_valid = true;
    ok = qa_q3_pres_client_info_copy_from(slot, &next);
  }
  qa_q3_pres_client_info_destroy(&next);
  return ok;
}

bool qa_q3_pres_client_info_store_load_deferred_players(store_t *store,
    void (*reset_player_entity)(void *ctx, qa_q3_pres_client_entity *entity), void *reset_ctx) {
  double lifecycle = store->lifecycle;
  qa_q3_pres_client_info_settings settings = store->host.settings(store->host.ctx);
  for (double i = 0; i < settings.max_clients && lifecycle == store->lifecycle; i++) {
    qa_q3_pres_client_info *slot = qa_q3_pres_client_info_store_client_info(store, i);
    if (!slot->info_valid || !slot->deferred) continue;
    if (store->host.memory_remaining(store->host.ctx) < 4000000) {
      store->host.print(store->host.ctx, "Memory is low.  Using deferred model.\n");
      slot->deferred = false;
      continue;
    }
    double request = store->requests[(size_t)i] + 1;
    store->requests[(size_t)i] = request;
    load_check check = {store, i, request, lifecycle, true};
    load_status status = load_client(store, slot, settings, &check);
    if (status == LOAD_OOM) return false;
    if (status == LOAD_SUPERSEDED) continue;
    if (lifecycle != store->lifecycle || store->requests[(size_t)i] != request) continue;
    for (int32_t number = 0; number < 1024; number++) {
      qa_q3_pres_client_entity *entity =
          qa_q3_pres_client_game_state_entity_at(store->host.state, number);
      if (entity->current_state.client_num == i && entity->current_state.e_type == QA_Q3_BASE_ET_PLAYER) {
        reset_player_entity(reset_ctx, entity);
      }
    }
  }
  return true;
}
