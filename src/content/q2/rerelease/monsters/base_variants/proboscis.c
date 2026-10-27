/* Port of src/content/q2/rerelease/monsters/base-variants/proboscis.ts.
 * Rerelease m_parasite.cpp. ZeniMax Media, GPL-2.0. */
#include "qa/content/q2/rerelease/monsters/base_variants/proboscis.h"

#include "qa/content/q2/foundation/fields.h"
#include "qa/content/q2/foundation/monsters/ai.h"
#include "qa/content/q2/rerelease/monsters/common.h"
#include "qa/content/q2/rerelease/monsters/tables/parasite.h"
#include "../rrm_internal.h"

static const qa_vec3 break_offsets[20] = {
  {7, 0, 7}, {6.3, 14.5, 4}, {8.5, 0, 5.6}, {5, -15.25, 4},
  {9.5, -1.8, 5.9}, {6.2, 14, 4}, {12.25, 7.5, 1.4}, {13.8, 0, -2.4},
  {13.8, 0, -4}, {0.1, 0, -0.7}, {5, 0, 3.7}, {11, 0, 4},
  {13.5, 0, -4}, {13.5, 0, -4}, {0.2, 0, -0.7}, {3.9, 0, 3.6},
  {8.5, 0, 5}, {14, 0, -4}, {14, 0, -4}, {0.1, 0, -0.5},
};
static const qa_vec3 drain_offsets[13] = {
  {-1.7, 0, 1.2}, {-2.2, 0, -0.6}, {7.7, 0, 7.2}, {7.2, 0, 5.7},
  {6.2, 0, 7.8}, {4.7, 0, 6.7}, {5, 0, 9}, {5, 0, 7},
  {5, 0, 10.5}, {4.5, 0, 9.7}, {1.5, 0, 12}, {2.9, 0, 11}, {2.1, 0, 7.6},
};
static qa_vec3 start_point(qa_q2m_context *context) {
  double frame = context->entity->frame;
  double break_index = frame - qa_q2rm_parasite_frames_value.break01;
  double drain_index = frame - qa_q2rm_parasite_frames_value.drain01;
  qa_vec3 offset;
  if (break_index >= 0 && break_index < 20 && break_index == trunc(break_index))
    offset = break_offsets[(size_t)break_index];
  else if (drain_index >= 0 && drain_index < 13 && drain_index == trunc(drain_index))
    offset = drain_offsets[(size_t)drain_index];
  else offset = q2rm_v3(8, 0, 6);
  return qa_q2m_project_flash_default(context, offset);
}
static void reset_fn(void *ctx, qa_q2f_entity *tip, qa_q2f_game *game) {
  (void)ctx;
  qa_q2f_entity *owner = game->entity(game->ctx, tip->has_owner ? &tip->owner : NULL);
  qa_q2f_entity *segment = game->entity(game->ctx, tip->has_proboscus ? &tip->proboscus : NULL);
  if (owner) owner->has_proboscus = false;
  if (segment) game->remove(game->ctx, segment);
  game->remove(game->ctx, tip);
}
static void die_fn(void *ctx, qa_q2f_entity *tip, qa_q2f_game *game, qa_death_reaction reaction) {
  (void)ctx; (void)reaction;
  const qa_attack_provenance *attack = tip->last_attack;
  if (attack && attack->cause.kind == QA_ATTACK_CAUSE_Q2 && attack->cause.means_of_death == 20)
    reset_fn(NULL, tip, game);
}
static void retract_with(qa_q2rm_proboscis *proboscis, qa_q2f_entity *tip, qa_q2f_game *game) {
  qa_q2m_context *owner = tip->has_owner ? qa_q2m_monsters_context(proboscis->monsters, tip->owner) : NULL;
  if (owner && strcmp(owner->state->move->name, "parasite_move_fire_proboscis") == 0)
    owner->state->next_frame = qa_q2rm_parasite_frames_value.drain12;
  if (tip->style != 2) tip->speed *= 2;
  tip->style = 2;
  game->motion(game->ctx, tip, QA_Q2F_MOTION_STATIONARY);
  game->solid(game->ctx, tip, QA_Q2F_SOLID_NONE);
  game->link(game->ctx, tip);
}
void qa_q2rm_proboscis_retract(qa_q2rm_proboscis *proboscis, qa_q2f_entity *tip, qa_q2f_game *game) {
  retract_with(proboscis, tip, game);
}
static void think_fn(void *ctx, qa_q2f_entity *tip, qa_q2f_game *game);
static void hit(qa_q2rm_proboscis *proboscis, qa_q2f_entity *tip, qa_q2f_game *game, qa_actor_id other,
                qa_vec3 point, qa_vec3 normal, bool start_solid) {
  qa_q2m_context *owner = tip->has_owner ? qa_q2m_monsters_context(proboscis->monsters, tip->owner) : NULL;
  if (!owner || strcmp(owner->state->move->name, "parasite_move_fire_proboscis") != 0) return;
  qa_body_state body = q2rm_body(game, tip);
  qa_body_state_result target = game->host->bodies.read(game->host->bodies.ctx, other);
  qa_vec3 position;
  bool attach = target.has_value && (game->host->is_player(game->host->ctx, other)
      || (owner->entity->has_enemy && qa_same_actor(owner->entity->enemy, other)));
  if (attach) {
    position = start_solid ? point
        : qa_sub3_double(point, qa_scale3_double(qa_normalize3_double_hypot(qa_sub3_double(body.origin, point)), 12));
    owner->state->next_frame = qa_q2rm_parasite_frames_value.drain06;
    tip->style = 1;
    tip->pos1 = qa_sub3_double(position, target.value.origin);
    tip->enemy = other; tip->has_enemy = true;
    tip->render_flags = q2rm_or(tip->render_flags, 32);
    game->motion(game->ctx, tip, QA_Q2F_MOTION_STATIONARY);
    game->solid(game->ctx, tip, QA_Q2F_SOLID_NONE);
    q2rm_sound(game, tip, "parasite/paratck3.wav", true, 1, false, 0, false, 0);
  } else {
    position = qa_add3_double(point, normal);
    qa_q2f_entity *other_entity = game->entity(game->ctx, &other);
    double server_flags = other_entity ? other_entity->server_flags : 0;
    if (game->host->is_monster(game->host->ctx, other) || q2rm_has(server_flags, 2)) retract_with(proboscis, tip, game);
    else {
      q2rm_set_move(owner, "parasite_move_break");
      tip->style = 1;
      game->motion(game->ctx, tip, QA_Q2F_MOTION_STATIONARY);
      game->solid(game->ctx, tip, QA_Q2F_SOLID_NONE);
      qa_vec3 angles = q2rm_body(game, owner->entity).angles;
      angles.y = body.angles.y;
      qa_q2f_body_changes change;
      memset(&change, 0, sizeof change);
      change.has_angles = true; change.angles = angles;
      q2rm_move_body(game, owner->entity, change, false, false);
    }
  }
  qa_combat_state_result combat = game->host->combat.read(game->host->combat.ctx, other);
  if (combat.has_value && combat.value.can_take_damage)
    (void)q2rm_damage(game, other, tip, tip->has_owner ? &tip->owner : NULL, 5, 0, normal, point, normal, 0, false, 0);
  qa_q2f_presentation_event event;
  memset(&event, 0, sizeof event);
  event.kind = QA_Q2F_EVENT_SOUND;
  event.actor = &owner->entity->actor.id;
  event.origin = point;
  event.path = "parasite/paratck2.wav";
  event.channel = 0; event.volume = 1; event.attenuation = 1; event.reliable = false; event.loop = "once";
  game->host->emit(game->host->ctx, event);
  qa_q2f_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_origin = true; change.origin = position;
  q2rm_move_body(game, tip, change, false, false);
  game->schedule(game->ctx, tip, game->host->frame_seconds(game->host->ctx), proboscis->think);
}
static void touch_fn(void *ctx, qa_q2f_entity *tip, qa_q2f_game *game, qa_touch_contact contact) {
  qa_vec3 normal = contact.plane ? contact.plane->normal : q2rm_zero();
  hit(ctx, tip, game, contact.other, q2rm_body(game, tip).origin, normal, false);
}
static void think_fn(void *ctx, qa_q2f_entity *tip, qa_q2f_game *game) {
  qa_q2rm_proboscis *proboscis = ctx;
  qa_q2m_context *owner = tip->has_owner ? qa_q2m_monsters_context(proboscis->monsters, tip->owner) : NULL;
  if (!owner) { reset_fn(NULL, tip, game); return; }
  game->schedule(game->ctx, tip, game->host->frame_seconds(game->host->ctx), proboscis->think);
  qa_body_state body = q2rm_body(game, tip);
  double frame_seconds = game->host->frame_seconds(game->host->ctx);
  if (tip->style == 2) {
    qa_vec3 origin = start_point(owner);
    qa_vec3 direction = qa_sub3_double(body.origin, origin);
    double distance = qa_hypot3(direction);
    qa_q2f_body_changes change;
    memset(&change, 0, sizeof change);
    change.has_origin = true;
    if (distance <= tip->speed * 2 * frame_seconds) {
      tip->style = 3;
      tip->think = proboscis->reset;
      change.origin = origin;
    } else change.origin = qa_sub3_double(body.origin, qa_scale3_double(qa_normalize3_double_hypot(direction), tip->speed * frame_seconds));
    q2rm_move_body(game, tip, change, false, false);
    return;
  }
  if (tip->style == 1 && tip->has_enemy) {
    qa_body_state_result target = game->host->bodies.read(game->host->bodies.ctx, tip->enemy);
    qa_combat_state_result combat = game->host->combat.read(game->host->combat.ctx, tip->enemy);
    if (!target.has_value || !combat.has_value || combat.value.health <= 0 || !combat.value.can_take_damage) {
      retract_with(proboscis, tip, game);
      return;
    }
    qa_vec3 origin = qa_add3_double(target.value.origin, tip->pos1);
    qa_vec3 from = start_point(owner);
    qa_trace_result trace = q2rm_trace(game, from, origin, NULL, NULL, 3);
    qa_q2f_body_changes change;
    memset(&change, 0, sizeof change);
    change.has_origin = true; change.origin = origin;
    change.has_angles = true;
    change.angles = qa_vector_to_angles_with_rule(qa_normalize3_double_hypot(qa_sub3_double(origin, from)),
                                                  QA_VECTOR_TO_ANGLES_NATIVE_AI);
    q2rm_move_body(game, tip, change, false, false);
    if (trace.fraction != 1) {
      retract_with(proboscis, tip, game);
      memset(&change, 0, sizeof change);
      change.has_origin = true; change.origin = body.origin;
      q2rm_move_body(game, tip, change, false, false);
      return;
    }
    if (tip->timestamp <= q2rm_now(game)) {
      qa_vec3 normal = trace.contact.has_plane ? trace.contact.plane.normal : q2rm_zero();
      (void)q2rm_damage(game, tip->enemy, tip, tip->has_owner ? &tip->owner : NULL, 2, 0, normal, trace.end, normal, 0, false, 0);
      double hp = qa_q2m_health(game, &owner->entity->actor.id) + 2;
      if (hp > owner->entity->max_health) hp = owner->entity->max_health;
      game->host->combat.set_health(game->host->combat.ctx, owner->entity->actor, hp);
      owner->entity->skin = hp < owner->entity->max_health / 2 ? 1 : 0;
      tip->timestamp = q2rm_now(game) + 0.1;
    }
    game->link(game->ctx, tip);
    return;
  }
  if (tip->style == 0) {
    const qa_actor_id *enemy_id = owner->entity->has_enemy ? &owner->entity->enemy : NULL;
    qa_body_state_result target;
    memset(&target, 0, sizeof target);
    if (enemy_id) target = game->host->bodies.read(game->host->bodies.ctx, *enemy_id);
    if (!target.has_value || qa_q2m_health(game, enemy_id) <= 0) { retract_with(proboscis, tip, game); return; }
    qa_vec3 delta = qa_sub3_double(body.origin, target.value.origin);
    qa_vec3 outward = qa_sub3_double(body.origin, q2rm_body(game, owner->entity).origin);
    if (qa_hypot3(delta) > tip->speed * 2 / 15
        && qa_dot3_double(qa_normalize3_double_hypot(delta), qa_normalize3_double_hypot(outward)) > 0) {
      retract_with(proboscis, tip, game);
      return;
    }
  }
}
static void draw_fn(void *ctx, qa_q2f_entity *segment, qa_q2f_game *game) {
  qa_q2rm_proboscis *proboscis = ctx;
  qa_q2f_entity *tip = game->entity(game->ctx, segment->has_owner ? &segment->owner : NULL);
  qa_q2m_context *owner = tip && tip->has_owner ? qa_q2m_monsters_context(proboscis->monsters, tip->owner) : NULL;
  if (!tip || !owner) return;
  qa_vec3 from = start_point(owner);
  qa_vec3 tip_origin = q2rm_body(game, tip).origin;
  qa_vec3 to = qa_sub3_double(tip_origin,
      qa_scale3_double(qa_normalize3_double_hypot(qa_sub3_double(tip_origin, from)), 8));
  segment->pos2 = to;
  qa_q2f_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_origin = true; change.origin = from;
  q2rm_move_body(game, segment, change, false, false);
}
void qa_q2rm_proboscis_fire(qa_q2rm_proboscis *proboscis, qa_q2m_context *context) {
  qa_q2f_entity *entity = context->entity;
  qa_q2f_game *game = context->game;
  qa_q2f_entity *previous = game->entity(game->ctx, entity->has_proboscus ? &entity->proboscus : NULL);
  if (previous && previous->style != 2) reset_fn(NULL, previous, game);
  qa_vec3 from = start_point(context);
  double offset = (float)(qa_q2_rerelease_random_float_range(qa_q2rm_rerelease_random(context), -0.9999999403953552, 1)
      * (float)0.1f);
  qa_vec3 direction;
  if (!qa_q2rm_predicted_direction(context, from, 1250, false, offset, &direction)) return;
  qa_q2f_entity *tip = game->create(game->ctx, "parasite_proboscis", NULL);
  qa_q2f_entity *segment = game->create(game->ctx, "parasite_proboscis_segment", NULL);
  tip->model = "models/monsters/parasite/tip/tris.md2";
  tip->owner = entity->actor.id; tip->has_owner = true;
  entity->proboscus = tip->actor.id; entity->has_proboscus = true;
  tip->clip_mask = (double)(3 | 0x2000000 | 0x40000000);
  tip->speed = 1250;
  tip->projectile = true;
  tip->die = proboscis->die;
  tip->touch = proboscis->touch;
  tip->flags = q2rm_or(tip->flags, 8);
  qa_combat_state combat;
  memset(&combat, 0, sizeof combat);
  combat.health = 0;
  combat.armor.regular.kind = QA_REGULAR_ARMOR_NONE;
  combat.armor.powered.kind = "none";
  combat.mass = 0;
  combat.can_take_damage = true;
  combat.invulnerable = false;
  combat.team = NULL;
  game->host->combat.create(game->host->combat.ctx, tip->actor, combat);
  qa_q2f_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_origin = true; change.origin = from;
  change.has_angles = true;
  change.angles = qa_vector_to_angles_with_rule(direction, QA_VECTOR_TO_ANGLES_NATIVE_AI);
  change.has_velocity = true; change.velocity = qa_scale3_double(direction, tip->speed);
  q2rm_move_body(game, tip, change, false, false);
  game->motion(game->ctx, tip, QA_Q2F_MOTION_FLY_MISSILE);
  game->solid(game->ctx, tip, QA_Q2F_SOLID_BOX);
  game->schedule(game->ctx, tip, game->host->frame_seconds(game->host->ctx), proboscis->think);
  segment->model = "models/monsters/parasite/segment/tris.md2";
  segment->render_flags = 128;
  segment->postthink = proboscis->draw;
  tip->proboscus = segment->actor.id; tip->has_proboscus = true;
  segment->owner = tip->actor.id; segment->has_owner = true;
  qa_vec3 trace_end = qa_add3_double(from, qa_scale3_double(direction,
      tip->speed * game->host->frame_seconds(game->host->ctx)));
  qa_trace_result trace = q2rm_trace(game, from, trace_end, NULL, &entity->actor.id, tip->clip_mask);
  if (trace.start_solid || trace.fraction < 1) {
    qa_actor_id other = trace.hit.kind == QA_TRACE_HIT_ACTOR ? trace.hit.actor
        : game->host->world_actor(game->host->ctx);
    qa_vec3 normal = trace.start_solid ? qa_scale3_double(direction, -1)
        : trace.contact.has_plane ? trace.contact.plane.normal : q2rm_zero();
    hit(proboscis, tip, game, other, trace.start_solid ? from : trace.end, normal, trace.start_solid);
  }
  qa_vec3 tip_origin = q2rm_body(game, tip).origin;
  segment->pos2 = qa_add3_double(tip_origin,
      qa_scale3_double(qa_normalize3_double_hypot(qa_sub3_double(tip_origin, from)), 8));
  memset(&change, 0, sizeof change);
  change.has_origin = true; change.origin = from;
  q2rm_move_body(game, segment, change, false, false);
  game->show(game->ctx, tip);
  game->show(game->ctx, segment);
}
void qa_q2rm_create_proboscis(qa_q2rm_proboscis *out, qa_q2m_monsters *monsters) {
  memset(out, 0, sizeof *out);
  out->monsters = monsters;
  out->reset = (qa_q2f_think){out, reset_fn};
  out->think = (qa_q2f_think){out, think_fn};
  out->draw = (qa_q2f_think){out, draw_fn};
  out->touch = (qa_q2f_touch){out, touch_fn};
  out->die = (qa_q2f_die){out, die_fn};
  out->think_defs[0].name = "rerelease.parasite.proboscis_reset";
  out->think_defs[0].cb = out->reset;
  out->think_defs[1].name = "rerelease.parasite.proboscis_think";
  out->think_defs[1].cb = out->think;
  out->think_defs[2].name = "rerelease.parasite.proboscis_segment_draw";
  out->think_defs[2].cb = out->draw;
  out->touch_defs[0].name = "rerelease.parasite.proboscis_touch";
  out->touch_defs[0].cb = out->touch;
  out->die_defs[0].name = "rerelease.parasite.proboscis_die";
  out->die_defs[0].cb = out->die;
}
qa_q2f_callback_definitions qa_q2rm_proboscis_callbacks(qa_q2rm_proboscis *proboscis) {
  qa_q2f_callback_definitions definitions;
  memset(&definitions, 0, sizeof definitions);
  definitions.think = proboscis->think_defs; definitions.think_count = 3;
  definitions.touch = proboscis->touch_defs; definitions.touch_count = 1;
  definitions.die = proboscis->die_defs; definitions.die_count = 1;
  return definitions;
}
