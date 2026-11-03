/* Port of src/content/q3/presentation/entities.ts. GPL-2.0-or-later. */
#include "qa/content/q3/presentation/entities.h"
#include "qa/network/common/js_number.h"
#include "qa/foundation/js.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "qa/content/q3/base/shared/direction_byte.h"
#include "qa/content/q3/base/shared/items.h"
#include "qa/content/q3/base/shared/snapshot_state.h"
#include "qa/content/q3/base/shared/trajectory.h"
#include "qa/content/q3/presentation/model_access.h"
#include "qa/core/common_error.h"
#include "qa/core/math.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"

enum { SOLID_BMODEL = 0xffffff, CHAN_ITEM = 4, CHAN_BODY = 5 };

static double f_(double value) { return (double)(float)value; } /* Math.fround */
/* TS `value | 0` (ToInt32). */
static int32_t to_int32(double value) {
  if (!isfinite(value)) return 0;
  double truncated = trunc(fmod(fabs(value), 4294967296.0));
  uint32_t bits = qa_js_to_uint32(truncated);
  if (value < 0) bits = ~bits + 1u;
  int32_t result;
  memcpy(&result, &bits, sizeof result);
  return result;
}
static int32_t imul(double a, double b) {
  uint32_t product = (uint32_t)to_int32(a) * (uint32_t)to_int32(b);
  int32_t result;
  memcpy(&result, &product, sizeof result);
  return result;
}
static qa_q3_base_trajectory base_trajectory(const qa_q3_wire_trajectory *wire) {
  qa_q3_base_trajectory result = {wire->type, wire->time, wire->duration, wire->base, wire->delta};
  return result;
}
static qa_vec3 eval_pos(const qa_q3_wire_trajectory *wire, double at_time) {
  return qa_q3_base_evaluate_trajectory(base_trajectory(wire), at_time);
}

const char *qa_q3_pres_missile_trail_name(qa_q3_pres_missile_trail trail) {
  switch (trail) {
    case QA_Q3_PRES_TRAIL_ROCKET: return "rocket";
    case QA_Q3_PRES_TRAIL_GRENADE: return "grenade";
    case QA_Q3_PRES_TRAIL_GRAPPLE: return "grapple";
    case QA_Q3_PRES_TRAIL_NAIL: return "nail";
    case QA_Q3_PRES_TRAIL_PLASMA: return "plasma";
  }
  return "rocket";
}

/* TS values[index] is undefined off the table, non-integers included. */
static bool table_index(double index, size_t count) {
  return index == trunc(index) && index >= 0 && index < (double)count;
}
static const qa_q3_pres_scene_model *indexed_model(const qa_q3_pres_scene_model *values, size_t count,
    double index, const char *name) {
  if (!table_index(index, count)) qa_range_error("%s index %s is not registered", name, QA_JS_NUMBER(index));
  return &values[(size_t)index];
}
static qa_pcm_sound *indexed_sound(qa_pcm_sound *const *values, size_t count, double index, const char *name) {
  if (!table_index(index, count)) qa_range_error("%s index %s is not registered", name, QA_JS_NUMBER(index));
  return values[(size_t)index];
}
static const qa_q3_pres_inline_model_entry *indexed_inline(const qa_q3_pres_packet_entity_media *media,
    double index) {
  if (!table_index(index, media->inline_model_count)) {
    qa_range_error("inline model index %s is not registered", QA_JS_NUMBER(index));
  }
  return &media->inline_models[(size_t)index];
}
static const qa_q3_pres_packet_item_visual *indexed_visual(const qa_q3_pres_packet_entity_media *media,
    double index) {
  if (!table_index(index, media->item_count)) {
    qa_range_error("item visual index %s is not registered", QA_JS_NUMBER(index));
  }
  return &media->items[(size_t)index];
}
static const qa_q3_pres_packet_weapon_info *indexed_weapon(const qa_q3_pres_packet_entity_media *media,
    double index) {
  if (index != trunc(index) || index < 0 || index >= (double)media->weapon_count) {
    qa_range_error("weapon index %s is not registered", QA_JS_NUMBER(index));
  }
  return &media->weapons[(size_t)index];
}

static qa_axis identity_axis(void) {
  qa_axis axis = {{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
  return axis;
}
static qa_axis scale_axis(qa_axis axis, double scale) {
  qa_axis result = {{qa_scale3(axis.vectors[0], scale), qa_scale3(axis.vectors[1], scale),
                     qa_scale3(axis.vectors[2], scale)}};
  return result;
}
static qa_axis multiply_axis(qa_axis left, qa_axis right) {
  qa_vec3 x = {right.vectors[0].x, right.vectors[1].x, right.vectors[2].x};
  qa_vec3 y = {right.vectors[0].y, right.vectors[1].y, right.vectors[2].y};
  qa_vec3 z = {right.vectors[0].z, right.vectors[1].z, right.vectors[2].z};
  qa_axis result;
  for (int i = 0; i < 3; i++) {
    result.vectors[i] = qa_math_vec3(qa_dot3(left.vectors[i], x), qa_dot3(left.vectors[i], y),
                                     qa_dot3(left.vectors[i], z));
  }
  return result;
}
typedef struct tag_orientation { qa_vec3 origin; qa_axis axis; } tag_orientation;
static tag_orientation tag_orientation_of(const qa_q3_pres_ref_model_entity *parent,
    const qa_q3_pres_scene_model *model, const char *name) {
  qa_q3_pres_model_tag tag;
  if (!qa_q3_pres_lerp_model_tag(model, name, parent->old_frame, parent->frame, f_(1 - parent->back_lerp), &tag)) {
    tag_orientation cleared = {{0, 0, 0}, identity_axis()};
    return cleared;
  }
  tag_orientation result = {tag.origin, tag.axes};
  return result;
}
static qa_vec3 tag_origin(const qa_q3_pres_ref_model_entity *parent, qa_vec3 tag) {
  qa_vec3 origin = qa_add3(parent->origin, qa_scale3(parent->axis.vectors[0], tag.x));
  origin = qa_add3(origin, qa_scale3(parent->axis.vectors[1], tag.y));
  return qa_add3(origin, qa_scale3(parent->axis.vectors[2], tag.z));
}

void qa_q3_pres_position_entity_on_tag(qa_q3_pres_ref_model_entity *entity,
    const qa_q3_pres_ref_model_entity *parent, const qa_q3_pres_scene_model *parent_model, const char *tag_name) {
  tag_orientation tag = tag_orientation_of(parent, parent_model, tag_name);
  entity->origin = tag_origin(parent, tag.origin);
  entity->axis = multiply_axis(tag.axis, parent->axis);
  entity->back_lerp = parent->back_lerp;
}

void qa_q3_pres_position_rotated_entity_on_tag(qa_q3_pres_ref_model_entity *entity,
    const qa_q3_pres_ref_model_entity *parent, const qa_q3_pres_scene_model *parent_model, const char *tag_name) {
  tag_orientation tag = tag_orientation_of(parent, parent_model, tag_name);
  entity->origin = tag_origin(parent, tag.origin);
  entity->axis = multiply_axis(multiply_axis(entity->axis, tag.axis), parent->axis);
}

qa_vec3 qa_q3_pres_adjust_position_for_mover(qa_q3_pres_client_game_state *state, qa_vec3 input,
    double mover_num, double from_time, double to_time) {
  if (mover_num <= 0 || mover_num >= 1022) return input;
  const qa_q3_base_entity_state *mover =
      &qa_q3_pres_client_game_state_entity_at(state, mover_num)->current_state;
  if (mover->e_type != QA_Q3_BASE_ET_MOVER) return input;
  qa_vec3 old_origin = eval_pos(&mover->pos, from_time);
  (void)eval_pos(&mover->apos, from_time);
  qa_vec3 origin = eval_pos(&mover->pos, to_time);
  (void)eval_pos(&mover->apos, to_time);
  /* The baseline computes angular deltas but only applies translation. */
  return qa_add3(input, qa_sub3(origin, old_origin));
}

static qa_vec3 interpolate(qa_vec3 from, qa_vec3 to, double fraction) {
  return qa_add3(from, qa_scale3(qa_sub3(to, from), fraction));
}
static qa_axis direction_axis(qa_vec3 direction, double yaw) {
  qa_vec3 side = qa_perpendicular_vector(direction);
  if (yaw != 0) side = qa_qvm_rotate_point_around_vector(direction, side, f_(yaw));
  qa_axis axis = {{direction, side, qa_cross3(direction, side)}};
  return axis;
}
static qa_vec3 missile_direction(qa_vec3 delta) {
  double length = qa_length3(delta);
  qa_vec3 direction = qa_normalize3_or_zero(delta);
  return length == 0 ? qa_math_vec3(0, 0, 1) : direction;
}

typedef qa_q3_pres_packet_entity_presenter presenter_t;
static bool body_hidden(presenter_t *p, double number) {
  return p->imports.body_hidden != NULL && p->imports.body_hidden(p->imports.ctx, number) == true;
}
static void body(presenter_t *p, qa_q3_pres_client_entity *entity, qa_q3_pres_ref_entity reference) {
  if (!body_hidden(p, entity->current_state.number)) p->imports.add_ref_entity(p->imports.ctx, &reference);
}
static void body_model(presenter_t *p, qa_q3_pres_client_entity *entity, qa_q3_pres_ref_model_entity model) {
  body(p, entity, qa_q3_pres_ref_entity_from_model(model));
}

void qa_q3_pres_packet_entity_presenter_init(presenter_t *presenter, qa_q3_pres_client_game_state *state,
    const qa_q3_pres_packet_entity_media *media, qa_q3_pres_packet_entity_imports imports) {
  if (state->product != media->product) {
    qa_range_error("packet entity media product differs from cgame state");
  }
  presenter->state = state;
  presenter->media = media;
  presenter->imports = imports;
}

void qa_q3_pres_packet_entity_presenter_set_entity_sound_position(presenter_t *presenter,
    qa_q3_pres_client_entity *entity) {
  const qa_q3_base_entity_state *current = &entity->current_state;
  qa_vec3 origin = current->solid == SOLID_BMODEL
      ? qa_add3(entity->lerp_origin, indexed_inline(presenter->media, current->modelindex)->midpoint)
      : entity->lerp_origin;
  presenter->imports.update_sound_position(presenter->imports.ctx, current->number, origin);
}

void qa_q3_pres_packet_entity_presenter_calculate_lerp_positions(presenter_t *presenter,
    qa_q3_pres_client_entity *entity, bool smooth_clients) {
  qa_q3_pres_client_game_state *state = presenter->state;
  qa_q3_pres_retail_snapshot *snap = state->snap;
  if (snap == NULL) qa_range_error("CG_CalcEntityLerpPositions: cg.snap == NULL");
  qa_q3_base_entity_state *current = &entity->current_state;
  if (!smooth_clients && current->number < 64) {
    current->pos.type = QA_Q3_BASE_TR_INTERPOLATE;
    entity->next_state.pos.type = QA_Q3_BASE_TR_INTERPOLATE;
  }
  if (entity->interpolate && (current->pos.type == QA_Q3_BASE_TR_INTERPOLATE ||
      (current->pos.type == QA_Q3_BASE_TR_LINEAR_STOP && current->number < 64))) {
    qa_q3_pres_retail_snapshot *next = state->next_snap;
    if (next == NULL) qa_common_error(QA_COMMON_ERROR_DROP, "CG_InterpoateEntityPosition: cg.nextSnap == NULL");
    entity->lerp_origin = interpolate(eval_pos(&current->pos, (double)snap->server_time),
        eval_pos(&entity->next_state.pos, (double)next->server_time), state->frame_interpolation);
    qa_vec3 a = eval_pos(&current->apos, (double)snap->server_time);
    qa_vec3 b = eval_pos(&entity->next_state.apos, (double)next->server_time);
    entity->lerp_angles = qa_math_vec3(
        qa_lerp_angle_with_rule(a.x, b.x, state->frame_interpolation, QA_LERP_ANGLE_FLOAT),
        qa_lerp_angle_with_rule(a.y, b.y, state->frame_interpolation, QA_LERP_ANGLE_FLOAT),
        qa_lerp_angle_with_rule(a.z, b.z, state->frame_interpolation, QA_LERP_ANGLE_FLOAT));
    return;
  }
  entity->lerp_origin = eval_pos(&current->pos, state->time);
  entity->lerp_angles = eval_pos(&current->apos, state->time);
  if (entity != &state->predicted_player_entity) {
    entity->lerp_origin = qa_q3_pres_adjust_position_for_mover(state, entity->lerp_origin,
        current->ground_entity_num, (double)snap->server_time, state->time);
  }
}

static void effects(presenter_t *p, qa_q3_pres_client_entity *entity) {
  qa_q3_pres_packet_entity_presenter_set_entity_sound_position(p, entity);
  const qa_q3_base_entity_state *current = &entity->current_state;
  if (current->loop_sound != 0) {
    p->imports.add_loop_sound(p->imports.ctx, current->number, entity->lerp_origin, qa_math_vec3(0, 0, 0),
        indexed_sound(p->media->game_sounds, p->media->game_sound_count, current->loop_sound, "sound"),
        current->e_type == QA_Q3_BASE_ET_SPEAKER);
  }
  if (current->constant_light != 0) {
    int32_t light = qa_js_to_int32(current->constant_light);
    qa_q3_pres_dynamic_light dynamic = {entity->lerp_origin, ((light >> 24) & 255) * 4,
        {light & 255, (light >> 8) & 255, (light >> 16) & 255}, false};
    p->imports.add_light(p->imports.ctx, dynamic);
  }
}

static void general(presenter_t *p, qa_q3_pres_client_entity *entity) {
  const qa_q3_base_entity_state *current = &entity->current_state;
  if (current->modelindex == 0) return;
  qa_q3_pres_ref_model_entity ref = qa_q3_pres_create_model_entity(
      *indexed_model(p->media->game_models, p->media->game_model_count, current->modelindex, "game model"));
  ref.frame = current->frame;
  ref.old_frame = current->frame;
  ref.origin = entity->lerp_origin;
  ref.old_origin = entity->lerp_origin;
  if (p->state->snap != NULL && current->number == p->state->snap->player_state.client_num) {
    ref.shading.render_flags = to_int32(ref.shading.render_flags) | QA_Q3_PRES_RF_THIRD_PERSON;
  }
  ref.axis = qa_qvm_angles_to_axis(entity->lerp_angles);
  body_model(p, entity, ref);
}

static void speaker(presenter_t *p, qa_q3_pres_client_entity *entity) {
  const qa_q3_base_entity_state *current = &entity->current_state;
  if (current->client_num == 0 || p->state->time < entity->misc_time) return;
  p->imports.start_sound(p->imports.ctx, false, qa_math_vec3(0, 0, 0), current->number, CHAN_ITEM,
      indexed_sound(p->media->game_sounds, p->media->game_sound_count, current->event_parm, "sound"));
  double random = f_((to_int32(p->imports.random_integer(p->imports.ctx)) & 0x7fff) / (double)0x7fff);
  double crandom = f_(2 * f_(random - 0.5));
  entity->misc_time = qa_qvm_float_to_int(f_(
      f_((double)to_int32(p->state->time + imul(current->frame, 100))) +
      f_(f_((double)imul(current->client_num, 100)) * crandom)));
}

static void item(presenter_t *p, qa_q3_pres_client_entity *entity, qa_q3_pres_packet_entity_options options) {
  qa_q3_pres_client_game_state *state = p->state;
  const qa_q3_base_entity_state *current = &entity->current_state;
  size_t item_count = 0;
  (void)qa_q3_base_item_list(state->product, &item_count);
  if (current->modelindex >= (int32_t)item_count) {
    char message[96];
    snprintf(message, sizeof message, "Bad item index %s on entity", QA_JS_NUMBER(current->modelindex));
    qa_common_error(QA_COMMON_ERROR_DROP, message);
  }
  if (current->modelindex == 0 || (qa_js_to_int32(current->e_flags) & 0x80) != 0) return;
  const qa_q3_base_item_definition *definition = qa_q3_base_item_at(state->product, current->modelindex);
  const qa_q3_pres_packet_item_visual *visual = indexed_visual(p->media, current->modelindex);
  if (options.simple_items && definition->type != QA_Q3_BASE_IT_TEAM) {
    qa_q3_pres_ref_sprite_entity ref = qa_q3_pres_create_sprite_entity();
    ref.origin = entity->lerp_origin;
    ref.radius = 14;
    ref.shading.custom_shader = visual->icon;
    ref.shading.shader_rgba = qa_math_vec4(255, 255, 255, 255);
    body(p, entity, qa_q3_pres_ref_entity_from_sprite(ref));
    return;
  }
  double scale = f_(f_(0.005) + f_(f_((double)current->number) * f_(0.00001)));
  double bob = f_(4 + f_(f_(cos(f_(f_((double)to_int32(state->time + 1000)) * scale))) * 4));
  entity->lerp_origin = qa_add3(entity->lerp_origin, qa_math_vec3(0, 0, bob));
  qa_q3_pres_ref_model_entity ref = qa_q3_pres_create_model_entity(visual->models[0]);
  bool fast = definition->type == QA_Q3_BASE_IT_HEALTH;
  entity->lerp_angles = fast ? state->auto_angles_fast : state->auto_angles;
  ref.axis = fast ? state->auto_axis_fast : state->auto_axis;
  const qa_q3_pres_packet_weapon_info *weapon =
      definition->type == QA_Q3_BASE_IT_WEAPON ? indexed_weapon(p->media, definition->tag) : NULL;
  if (weapon != NULL) {
    qa_vec3 midpoint = weapon->weapon_midpoint;
    qa_vec3 offset = qa_add3(qa_add3(qa_scale3(ref.axis.vectors[0], midpoint.x),
                                     qa_scale3(ref.axis.vectors[1], midpoint.y)),
                             qa_scale3(ref.axis.vectors[2], midpoint.z));
    entity->lerp_origin = qa_add3(qa_sub3(entity->lerp_origin, offset), qa_math_vec3(0, 0, 8));
  }
  ref.origin = entity->lerp_origin;
  ref.old_origin = entity->lerp_origin;
  int32_t msec = to_int32(state->time - entity->misc_time);
  double fraction = 1;
  if (msec >= 0 && msec < 1000) {
    fraction = f_(f_((double)msec) / 1000);
    ref.axis = scale_axis(ref.axis, fraction);
    ref.non_normalized_axes = true;
  }
  if (definition->type == QA_Q3_BASE_IT_WEAPON || definition->type == QA_Q3_BASE_IT_ARMOR) {
    ref.shading.render_flags = to_int32(ref.shading.render_flags) | QA_Q3_PRES_RF_MINLIGHT;
  }
  if (definition->type == QA_Q3_BASE_IT_WEAPON) {
    ref.axis = scale_axis(ref.axis, 1.5);
    ref.non_normalized_axes = true;
    if (p->media->product == QA_Q3_BASE_PRODUCT_MISSIONPACK) {
      p->imports.add_loop_sound(p->imports.ctx, current->number, entity->lerp_origin, qa_math_vec3(0, 0, 0),
          p->media->mission.weapon_hover_sound, false);
    }
  }
  if (p->media->product == QA_Q3_BASE_PRODUCT_MISSIONPACK && definition->type == QA_Q3_BASE_IT_HOLDABLE &&
      definition->tag == QA_Q3_HI_KAMIKAZE) {
    ref.axis = scale_axis(ref.axis, 2);
    ref.non_normalized_axes = true;
  }
  body_model(p, entity, ref);
  if (p->media->product == QA_Q3_BASE_PRODUCT_MISSIONPACK && weapon != NULL && weapon->has_barrel_model &&
      weapon->barrel_model.kind != QA_Q3_PRES_SCENE_MODEL_DEFAULT) {
    qa_q3_pres_ref_model_entity barrel = qa_q3_pres_create_model_entity(weapon->barrel_model);
    barrel.lighting_origin = ref.lighting_origin;
    barrel.shadow_plane = ref.shadow_plane;
    barrel.shading.render_flags = ref.shading.render_flags;
    qa_q3_pres_position_rotated_entity_on_tag(&barrel, &ref, &weapon->weapon_model, "tag_barrel");
    barrel.axis = ref.axis;
    barrel.non_normalized_axes = ref.non_normalized_axes;
    body_model(p, entity, barrel);
  }
  if (!options.simple_items && (definition->type == QA_Q3_BASE_IT_HEALTH || definition->type == QA_Q3_BASE_IT_POWERUP) &&
      visual->has_second_model && visual->models[1].kind != QA_Q3_PRES_SCENE_MODEL_DEFAULT) {
    ref.model = visual->models[1];
    double yaw = 0;
    if (definition->type == QA_Q3_BASE_IT_POWERUP) {
      ref.origin = qa_add3(ref.origin, qa_math_vec3(0, 0, 12));
      yaw = f_(((to_int32(state->time) & 1023) * 360) / (double)-1024);
    }
    ref.axis = qa_qvm_angles_to_axis(qa_math_vec3(0, yaw, 0));
    if (fraction != 1) {
      ref.axis = scale_axis(ref.axis, fraction);
      ref.non_normalized_axes = true;
    }
    body_model(p, entity, ref);
  }
}

static const qa_q3_pres_packet_weapon_info *presenter_weapon(presenter_t *p, qa_q3_pres_client_entity *entity) {
  /* Source intentionally uses > rather than >= WP_NUM_WEAPONS. */
  double count = p->state->product == QA_Q3_BASE_PRODUCT_MISSIONPACK ? 14 : 11;
  if (entity->current_state.weapon > count) entity->current_state.weapon = QA_Q3_WP_NONE;
  return indexed_weapon(p->media, entity->current_state.weapon);
}

static void missile(presenter_t *p, qa_q3_pres_client_entity *entity) {
  qa_q3_base_entity_state *current = &entity->current_state;
  const qa_q3_pres_packet_weapon_info *weapon = presenter_weapon(p, entity);
  entity->lerp_angles = current->angles;
  if (weapon->has_missile_trail) {
    p->imports.missile_trail(p->imports.ctx, weapon->missile_trail, entity, weapon);
  }
  if (weapon->missile_dlight != 0) {
    qa_q3_pres_dynamic_light light = {entity->lerp_origin, weapon->missile_dlight, weapon->missile_dlight_color, false};
    p->imports.add_light(p->imports.ctx, light);
  }
  if (weapon->missile_sound != NULL) {
    p->imports.add_loop_sound(p->imports.ctx, current->number, entity->lerp_origin,
        qa_q3_base_evaluate_trajectory_delta(base_trajectory(&current->pos), p->state->time),
        weapon->missile_sound, false);
  }
  if (current->weapon == QA_Q3_WP_PLASMAGUN) {
    qa_q3_pres_ref_sprite_entity ref = qa_q3_pres_create_sprite_entity();
    ref.origin = entity->lerp_origin;
    ref.radius = 16;
    ref.shading.custom_shader = p->media->plasma_ball_shader;
    body(p, entity, qa_q3_pres_ref_entity_from_sprite(ref));
    return;
  }
  qa_q3_pres_ref_model_entity ref = qa_q3_pres_create_model_entity(weapon->missile_model);
  ref.origin = entity->lerp_origin;
  ref.old_origin = entity->lerp_origin;
  ref.skin_num = to_int32(p->state->client_frame) & 1;
  ref.shading.render_flags = to_int32(weapon->missile_renderfx) | QA_Q3_PRES_RF_NOSHADOW;
  if (p->media->product == QA_Q3_BASE_PRODUCT_MISSIONPACK && current->weapon == QA_Q3_WP_PROX_LAUNCHER &&
      current->generic1 == QA_Q3_BASE_TEAM_BLUE) {
    ref.model = p->media->mission.blue_prox_mine;
  }
  qa_vec3 direction = missile_direction(current->pos.delta);
  if (current->pos.type != QA_Q3_BASE_TR_STATIONARY) {
    ref.axis = direction_axis(direction, to_int32(p->state->time / 4));
  } else if (p->state->product == QA_Q3_BASE_PRODUCT_MISSIONPACK && current->weapon == QA_Q3_WP_PROX_LAUNCHER) {
    ref.axis = qa_qvm_angles_to_axis(entity->lerp_angles);
  } else {
    ref.axis = direction_axis(direction, current->time);
  }
  if (!body_hidden(p, current->number)) {
    p->imports.add_entity_with_powerups(p->imports.ctx, &ref, current, QA_Q3_BASE_TEAM_FREE);
  }
}

static void grapple(presenter_t *p, qa_q3_pres_client_entity *entity) {
  qa_q3_base_entity_state *current = &entity->current_state;
  const qa_q3_pres_packet_weapon_info *weapon = presenter_weapon(p, entity);
  entity->lerp_angles = current->angles;
  p->imports.grapple_trail(p->imports.ctx, entity, weapon);
  qa_q3_pres_ref_model_entity ref = qa_q3_pres_create_model_entity(weapon->missile_model);
  ref.origin = entity->lerp_origin;
  ref.old_origin = entity->lerp_origin;
  ref.skin_num = to_int32(p->state->client_frame) & 1;
  ref.shading.render_flags = to_int32(weapon->missile_renderfx) | QA_Q3_PRES_RF_NOSHADOW;
  /* CG_Grapple only fills axis[0]; the two cleared axes remain zero. */
  ref.axis.vectors[0] = missile_direction(current->pos.delta);
  body_model(p, entity, ref);
}

static void mover(presenter_t *p, qa_q3_pres_client_entity *entity) {
  const qa_q3_base_entity_state *current = &entity->current_state;
  qa_q3_pres_scene_model model = current->solid == SOLID_BMODEL
      ? indexed_inline(p->media, current->modelindex)->model
      : *indexed_model(p->media->game_models, p->media->game_model_count, current->modelindex, "game model");
  qa_q3_pres_ref_model_entity ref = qa_q3_pres_create_model_entity(model);
  ref.origin = entity->lerp_origin;
  ref.old_origin = entity->lerp_origin;
  ref.axis = qa_qvm_angles_to_axis(entity->lerp_angles);
  ref.shading.render_flags = QA_Q3_PRES_RF_NOSHADOW;
  ref.skin_num = (to_int32(p->state->time) >> 6) & 1;
  body_model(p, entity, ref);
  if (current->modelindex2 != 0) {
    ref.skin_num = 0;
    ref.model = *indexed_model(p->media->game_models, p->media->game_model_count, current->modelindex2, "game model");
    body_model(p, entity, ref);
  }
}

void qa_q3_pres_packet_entity_presenter_beam(presenter_t *presenter, qa_q3_pres_client_entity *entity) {
  qa_q3_pres_ref_beam_entity ref = qa_q3_pres_create_beam_entity();
  ref.origin = entity->current_state.pos.base;
  ref.old_origin = entity->current_state.origin2;
  ref.shading.render_flags = QA_Q3_PRES_RF_NOSHADOW;
  ref.axis = identity_axis();
  body(presenter, entity, qa_q3_pres_ref_entity_from_beam(ref));
}

static void portal(presenter_t *p, qa_q3_pres_client_entity *entity) {
  const qa_q3_base_entity_state *current = &entity->current_state;
  qa_q3_pres_ref_portal_entity ref = qa_q3_pres_create_portal_entity();
  ref.origin = entity->lerp_origin;
  ref.old_origin = current->origin2;
  qa_vec3 forward = qa_q3_base_byte_to_direction(current->event_parm);
  qa_vec3 side = qa_sub3(qa_math_vec3(0, 0, 0), qa_perpendicular_vector(forward));
  ref.axis.vectors[0] = forward;
  ref.axis.vectors[1] = side;
  ref.axis.vectors[2] = qa_cross3(forward, side);
  ref.old_frame = current->powerups;
  ref.frame = current->frame;
  ref.skin_num = qa_qvm_float_to_int(f_(f_(f_((double)current->client_num) / 256) * 360));
  body(p, entity, qa_q3_pres_ref_entity_from_portal(ref));
}

static void team(presenter_t *p, qa_q3_pres_client_entity *entity, qa_q3_pres_packet_entity_options options) {
  const qa_q3_base_entity_state *current = &entity->current_state;
  qa_q3_pres_ref_model_entity ref = qa_q3_pres_create_model_entity_default();
  ref.origin = entity->lerp_origin;
  ref.lighting_origin = entity->lerp_origin;
  ref.axis = qa_qvm_angles_to_axis(current->angles);
  if (options.game_type == QA_Q3_BASE_GT_CTF ||
      (p->state->product == QA_Q3_BASE_PRODUCT_MISSIONPACK && options.game_type == QA_Q3_BASE_GT_1FCTF)) {
    ref.model = current->modelindex == QA_Q3_BASE_TEAM_RED ? p->media->red_flag_base_model
        : current->modelindex == QA_Q3_BASE_TEAM_BLUE ? p->media->blue_flag_base_model
        : p->media->neutral_flag_base_model;
    body_model(p, entity, ref);
    return;
  }
  if (p->media->product != QA_Q3_BASE_PRODUCT_MISSIONPACK) return;
  const qa_q3_pres_packet_mission_media *media = &p->media->mission;
  if (options.game_type == QA_Q3_BASE_GT_HARVESTER) {
    ref.model = current->modelindex == QA_Q3_BASE_TEAM_RED || current->modelindex == QA_Q3_BASE_TEAM_BLUE
        ? media->harvester_model : media->harvester_neutral_model;
    ref.custom_skin = current->modelindex == QA_Q3_BASE_TEAM_RED ? media->harvester_red_skin
        : current->modelindex == QA_Q3_BASE_TEAM_BLUE ? media->harvester_blue_skin : NULL;
    body_model(p, entity, ref);
    return;
  }
  if (options.game_type != QA_Q3_BASE_GT_OBELISK) return;
  ref.model = media->overload_base_model;
  body_model(p, entity, ref);
  double health = qa_qvm_float_to_int(f_((double)current->modelindex2)) & 255;
  if (current->frame == 1) {
    ref.shading.shader_rgba = qa_math_vec4(255, health, health, 255);
    ref.model = media->overload_energy_model;
    body_model(p, entity, ref);
  }
  if (current->frame != 2) {
    entity->misc_time = 0;
    entity->muzzle_flash_time = 0;
    ref.shading.shader_rgba = qa_math_vec4(255, health, health, 255);
    ref.model = media->overload_lights_model;
    body_model(p, entity, ref);
    ref.origin = qa_add3(ref.origin, qa_math_vec3(0, 0, 56));
    ref.model = media->overload_target_model;
    body_model(p, entity, ref);
    return;
  }
  if (entity->misc_time == 0) entity->misc_time = p->state->time;
  int32_t elapsed = to_int32(p->state->time - entity->misc_time);
  int32_t threshold = imul((double)to_int32(options.obelisk_respawn_delay - 5), 1000);
  double scale = 0;
  if (elapsed > threshold) {
    double grown = f_(f_((double)to_int32((double)elapsed - (double)threshold)) / f_((double)threshold));
    scale = grown < 1 ? grown : 1;
  }
  double color = qa_qvm_float_to_int(f_(scale * 255)) & 255;
  ref.shading.shader_rgba = qa_math_vec4(color, color, color, color);
  ref.model = media->overload_lights_model;
  body_model(p, entity, ref);
  if (elapsed > threshold) {
    if (entity->muzzle_flash_time == 0) {
      p->imports.start_sound(p->imports.ctx, true, entity->lerp_origin, 1023, CHAN_BODY,
          media->obelisk_respawn_sound);
      entity->muzzle_flash_time = 1;
    }
    double spin = f_(f_(f_(16 * f_(acos(f_(1 - scale)))) * 180) / f_(acos(-1.0)));
    ref.axis = scale_axis(qa_qvm_angles_to_axis(
        qa_math_vec3(current->angles.x, f_(current->angles.y + spin), current->angles.z)), scale);
    /* Source leaves nonNormalizedAxes false even while scaling this target. */
    ref.shading.shader_rgba = qa_math_vec4(255, 255, 255, 255);
    ref.origin = qa_add3(ref.origin, qa_math_vec3(0, 0, 56));
    ref.model = media->overload_target_model;
    body_model(p, entity, ref);
  }
}

void qa_q3_pres_packet_entity_presenter_add_entity(presenter_t *presenter, qa_q3_pres_client_entity *entity,
    qa_q3_pres_packet_entity_options options) {
  int32_t type = qa_js_to_int32(entity->current_state.e_type);
  if (type >= QA_Q3_BASE_ET_EVENTS) return;
  qa_q3_pres_packet_entity_presenter_calculate_lerp_positions(presenter, entity, options.smooth_clients);
  if (presenter->imports.pose != NULL) presenter->imports.pose(presenter->imports.ctx, entity);
  effects(presenter, entity);
  switch (type) {
    case QA_Q3_BASE_ET_INVISIBLE:
    case QA_Q3_BASE_ET_PUSH_TRIGGER:
    case QA_Q3_BASE_ET_TELEPORT_TRIGGER: return;
    case QA_Q3_BASE_ET_GENERAL: general(presenter, entity); return;
    case QA_Q3_BASE_ET_PLAYER: presenter->imports.player(presenter->imports.ctx, entity); return;
    case QA_Q3_BASE_ET_ITEM: item(presenter, entity, options); return;
    case QA_Q3_BASE_ET_MISSILE: missile(presenter, entity); return;
    case QA_Q3_BASE_ET_MOVER: mover(presenter, entity); return;
    case QA_Q3_BASE_ET_BEAM: qa_q3_pres_packet_entity_presenter_beam(presenter, entity); return;
    case QA_Q3_BASE_ET_PORTAL: portal(presenter, entity); return;
    case QA_Q3_BASE_ET_SPEAKER: speaker(presenter, entity); return;
    case QA_Q3_BASE_ET_GRAPPLE: grapple(presenter, entity); return;
    case QA_Q3_BASE_ET_TEAM: team(presenter, entity, options); return;
    default: {
      char message[64];
      snprintf(message, sizeof message, "Bad entity type: %lld\n", (long long)type);
      qa_common_error(QA_COMMON_ERROR_DROP, message);
    }
  }
}

void qa_q3_pres_packet_entity_presenter_add_packet_entities(presenter_t *presenter,
    qa_q3_pres_packet_entity_options options) {
  qa_q3_pres_client_game_state *state = presenter->state;
  qa_q3_pres_retail_snapshot *snap = state->snap;
  if (snap == NULL) qa_range_error("CG_AddPacketEntities: cg.snap == NULL");
  int32_t delta = state->next_snap == NULL ? 0
      : to_int32((double)state->next_snap->server_time - (double)snap->server_time);
  state->frame_interpolation = delta == 0 ? 0
      : f_(f_((double)to_int32(state->time - (double)snap->server_time)) / f_((double)delta));
  state->auto_angles = qa_math_vec3(0, f_(((to_int32(state->time) & 2047) * 360) / (double)2048), 0);
  state->auto_angles_fast = qa_math_vec3(0, f_(((to_int32(state->time) & 1023) * 360) / (double)1024), 0);
  state->auto_axis = qa_qvm_angles_to_axis(state->auto_angles);
  state->auto_axis_fast = qa_qvm_angles_to_axis(state->auto_angles_fast);
  qa_q3_base_player_state_to_entity_state(&state->predicted_player_state,
      &state->predicted_player_entity.current_state, false);
  qa_q3_pres_packet_entity_presenter_add_entity(presenter, &state->predicted_player_entity, options);
  qa_q3_pres_packet_entity_presenter_calculate_lerp_positions(presenter,
      qa_q3_pres_client_game_state_entity_at(state, snap->player_state.client_num), options.smooth_clients);
  for (size_t index = 0; index < snap->entity_count; index++) {
    qa_q3_pres_packet_entity_presenter_add_entity(presenter,
        qa_q3_pres_client_game_state_entity_at(state, snap->entities[index].number), options);
  }
}

