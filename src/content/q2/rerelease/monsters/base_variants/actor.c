/* Port of src/content/q2/rerelease/monsters/base-variants/actor.ts.
 * Rerelease m_actor.cpp. ZeniMax Media, GPL-2.0. */
#include "qa/content/q2/rerelease/monsters/base_variants/actor.h"

#include <stdio.h>

#include "qa/content/q2/base/monsters/actor.h"
#include "qa/content/q2/foundation/fields.h"
#include "qa/content/q2/foundation/monsters/ai.h"
#include "qa/content/q2/foundation/monsters/muzzle.h"
#include "qa/content/q2/rerelease/monsters/common.h"
#include "qa/content/q2/rerelease/monsters/tables/actor.h"
#include "qa/core/numeric.h"
#include "../rrm_internal.h"

static const double hold_forever = 9223372036854775807.0 / 1000.0;

static double integer_field(const qa_q2f_spawn_fields *fields, const char *key) {
  const char *value = qa_q2f_spawn_value(fields, key);
  return value ? (double)qa_native_atoi(value) : 0;
}
static void json_origin(char *buf, size_t size, qa_vec3 origin) {
  (void)snprintf(buf, size, "{\"x\":%g,\"y\":%g,\"z\":%g}", origin.x, origin.y, origin.z);
}
static qa_vec3 movedir_of(qa_vec3 angles) {
  if (angles.x == 0 && angles.y == -1 && angles.z == 0) return q2rm_v3(0, 0, 1);
  if (angles.x == 0 && angles.y == -2 && angles.z == 0) return q2rm_v3(0, 0, -1);
  return qa_math_angle_vectors_rule(q2rm_v3(angles.x, angles.y, 0), QA_ANGLE_RULE_NATIVE).forward;
}
static const char *entity_text(const char *value) { return value ? value : ""; }

static void dead_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  c->state->corpse = true;
  c->entity->server_flags = q2rm_or(c->entity->server_flags, 2);
  qa_q2f_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_bounds = true;
  change.bounds.min = q2rm_v3(-16, -16, -24);
  change.bounds.max = q2rm_v3(16, 16, -8);
  q2rm_move_body(c->game, c->entity, change, false, false);
  c->game->motion(c->game->ctx, c->entity, QA_Q2F_MOTION_TOSS);
  c->game->cancel(c->game->ctx, c->entity);
}
static void use_fn(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game,
                   const qa_actor_id *other, const qa_actor_id *activator) {
  qa_q2m_monsters *monsters = ctx;
  (void)other; (void)activator;
  qa_q2m_context *context = qa_q2m_monsters_context(monsters, entity->actor.id);
  if (!context) return;
  qa_q2f_entity *target = game->pick_target(game->ctx, entity_text(entity->target));
  if (target) {
    entity->goal = target->actor.id; entity->has_goal = true;
    context->state->move_target = target->actor.id; context->state->has_move_target = true;
  } else {
    entity->has_goal = false;
    context->state->has_move_target = false;
  }
  if (!target || strcmp(target->classname, "target_actor") != 0) {
    char message[256];
    (void)snprintf(message, sizeof message, "misc_actor has bad target %s", entity_text(entity->target));
    game->host->diagnostic(game->host->ctx, message);
    entity->target = "";
    context->state->pause_time = hold_forever;
    context->stand(context->impl_ctx, context);
    return;
  }
  context->state->ideal_yaw = qa_vector_to_angles_with_rule(
      qa_sub3_double(q2rm_body(game, target).origin, q2rm_body(game, entity).origin),
      QA_VECTOR_TO_ANGLES_NATIVE_AI).y;
  qa_vec3 angles = q2rm_body(game, entity).angles;
  angles.y = context->state->ideal_yaw;
  qa_q2f_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_angles = true; change.angles = angles;
  q2rm_move_body(game, entity, change, false, false);
  context->walk(context->impl_ctx, context);
  entity->target = "";
}
static void touch_fn(void *ctx, qa_q2f_entity *self, qa_q2f_game *game, qa_touch_contact contact) {
  qa_q2m_monsters *monsters = ctx;
  qa_q2f_entity *actor = game->entity(game->ctx, &contact.other);
  qa_q2m_context *context = qa_q2m_monsters_context(monsters, contact.other);
  if (!actor || !context || !context->state->has_move_target
      || !qa_same_actor(context->state->move_target, self->actor.id) || actor->has_enemy) return;
  actor->has_goal = false;
  context->state->has_move_target = false;
  if (strcmp(entity_text(self->message), "") != 0) {
    size_t count = 0;
    const qa_actor_id *players = game->host->players(game->host->ctx, &count);
    for (size_t i = 0; i < count; i++) {
      char text[512];
      (void)snprintf(text, sizeof text, "%s: %s\n", qa_q2bm_actor_name(actor, game), self->message);
      qa_q2bm_extended_event event;
      memset(&event, 0, sizeof event);
      event.kind = "print";
      event.actor = players[i];
      event.level = "chat";
      event.text = text;
      qa_q2bm_emit_extended(event);
    }
  }
  if (q2rm_has(self->spawnflags, 1)) {
    qa_body_state body = q2rm_body(game, actor);
    qa_vec3 velocity = {self->movedir.x * self->speed, self->movedir.y * self->speed,
                        body.ground ? self->movedir.z : body.velocity.z};
    qa_q2f_body_changes change;
    memset(&change, 0, sizeof change);
    change.has_velocity = true; change.velocity = velocity;
    change.has_ground = true; change.ground = NULL;
    q2rm_move_body(game, actor, change, false, false);
    if (body.ground) q2rm_sound(game, actor, "player/male/jump1.wav", true, 2, false, 0, false, 0);
  }
  const char *path_target = qa_q2f_spawn_value(&self->spawn, "pathtarget");
  if (!path_target) path_target = "";
  if (!q2rm_has(self->spawnflags, 2) && q2rm_has(self->spawnflags, 4)) {
    qa_q2f_entity *enemy = game->pick_target(game->ctx, path_target);
    if (enemy) { actor->enemy = enemy->actor.id; actor->has_enemy = true; }
    else actor->has_enemy = false;
    if (actor->has_enemy) {
      actor->goal = actor->enemy;
      actor->has_goal = true;
      if (q2rm_has(self->spawnflags, 32)) context->state->brutal = true;
      if (q2rm_has(self->spawnflags, 16)) {
        context->state->stand_ground = true;
        context->stand(context->impl_ctx, context);
      } else context->run(context->impl_ctx, context);
    }
  }
  if (((int32_t)self->spawnflags & 6) == 0 && strcmp(path_target, "") != 0) {
    const char *original = self->target;
    self->target = path_target;
    game->use_targets(game->ctx, self, &actor->actor.id, false, false);
    self->target = original;
  }
  qa_q2f_entity *next = game->pick_target(game->ctx, entity_text(self->target));
  if (next) {
    context->state->move_target = next->actor.id;
    context->state->has_move_target = true;
  } else context->state->has_move_target = false;
  if (!actor->has_goal && context->state->has_move_target) {
    actor->goal = context->state->move_target;
    actor->has_goal = true;
  }
  if (!context->state->has_move_target && !actor->has_enemy) {
    context->state->pause_time = hold_forever;
    context->stand(context->impl_ctx, context);
    return;
  }
  qa_q2f_entity *goal = game->entity(game->ctx, context->state->has_move_target ? &context->state->move_target : NULL);
  if (goal && actor->has_goal && qa_same_actor(actor->goal, goal->actor.id))
    context->state->ideal_yaw = qa_vector_to_angles_with_rule(
        qa_sub3_double(q2rm_body(game, goal).origin, q2rm_body(game, actor).origin),
        QA_VECTOR_TO_ANGLES_NATIVE_AI).y;
}
static void init_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2f_entity *entity = c->entity;
  qa_q2f_game *game = c->game;
  if (strcmp(entity_text(entity->targetname), "") == 0 || strcmp(entity_text(entity->target), "") == 0) {
    char origin[96], message[256];
    json_origin(origin, sizeof origin, q2rm_body(game, entity).origin);
    (void)snprintf(message, sizeof message, "misc_actor requires target and targetname at %s", origin);
    game->host->diagnostic(game->host->ctx, message);
    game->remove(game->ctx, entity);
    return;
  }
  c->state->good_guy = true;
  double health = integer_field(&entity->spawn, "health");
  entity->max_health = health != 0 ? health : 100;
  game->host->combat.set_health(game->host->combat.ctx, entity->actor, entity->max_health);
}
static void after_spawn_fn(void *ctx, qa_q2m_context *c) {
  c->entity->use.ctx = ctx;
  c->entity->use.fn = use_fn;
}
static void attack_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2rm_set_move(c, "actor_move_attack");
  c->state->fire_wait = q2rm_now(c->game) + 1 + q2rm_rnd(c->game) * 1.6;
}
static void pain_fn(void *ctx, qa_q2m_context *c, qa_pain_reaction reaction) {
  (void)ctx;
  qa_q2f_entity *entity = c->entity;
  qa_q2f_game *game = c->game;
  qa_q2m_state *state = c->state;
  entity->skin = qa_q2m_health(game, &entity->actor.id) < entity->max_health / 2 ? 1 : 0;
  if (q2rm_now(game) < state->pain_time) return;
  state->pain_time = q2rm_now(game) + 3;
  if (reaction.attacker && game->host->is_player(game->host->ctx, *reaction.attacker) && q2rm_rnd(game) < 0.4) {
    qa_body_state_result other = game->host->bodies.read(game->host->bodies.ctx, *reaction.attacker);
    if (other.has_value)
      state->ideal_yaw = qa_vector_to_angles_with_rule(
          qa_sub3_double(other.value.origin, q2rm_body(game, entity).origin), QA_VECTOR_TO_ANGLES_NATIVE_AI).y;
    q2rm_set_move(c, q2rm_rnd(game) < 0.5 ? "actor_move_flipoff" : "actor_move_taunt");
    static const char *const messages[4] = {"Watch it", "#$@*&", "Idiot", "Check your targets"};
    const char *message = messages[qa_q2m_record_at(4, floor(q2rm_rnd(game) * 4))];
    char text[256];
    (void)snprintf(text, sizeof text, "%s: %s!\n", qa_q2bm_actor_name(entity, game), message);
    qa_q2bm_extended_event event;
    memset(&event, 0, sizeof event);
    event.kind = "print";
    event.actor = *reaction.attacker;
    event.level = "chat";
    event.text = text;
    qa_q2bm_emit_extended(event);
    return;
  }
  char move_name[32];
  (void)snprintf(move_name, sizeof move_name, "actor_move_pain%d", (int)floor(q2rm_rnd(game) * 3) + 1);
  q2rm_set_move(c, move_name);
}
static void fire_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2f_entity *entity = c->entity;
  qa_q2f_game *game = c->game;
  qa_q2m_state *state = c->state;
  qa_body_state body = q2rm_body(game, entity);
  qa_angle_vectors axes = qa_math_angle_vectors_rule(body.angles, QA_ANGLE_RULE_NATIVE);
  qa_vec3 offset = qa_q2m_muzzle_offset(QA_Q2F_RERELEASE, 63);
  qa_body_state enemy;
  bool have_enemy = qa_q2m_enemy_body(c, &enemy);
  qa_vec3 start = qa_add3_double(qa_add3_double(qa_add3_double(body.origin, qa_scale3_double(axes.forward, offset.x)),
                                                qa_scale3_double(axes.right, offset.y)),
                                 q2rm_v3(0, 0, offset.z));
  qa_vec3 direction = axes.forward;
  if (have_enemy) {
    qa_vec3 target;
    if (qa_q2m_health(game, entity->has_enemy ? &entity->enemy : NULL) > 0) {
      qa_q2f_entity *enemy_entity = game->entity(game->ctx, entity->has_enemy ? &entity->enemy : NULL);
      target = qa_add3_double(qa_add3_double(enemy.origin, qa_scale3_double(enemy.velocity, -0.2)),
                              q2rm_v3(0, 0, enemy_entity ? enemy_entity->view_height : 22));
    } else {
      target = q2rm_v3(enemy.origin.x + enemy.bounds.min.x, enemy.origin.y + enemy.bounds.min.y,
                       enemy.origin.z + (enemy.bounds.min.z + enemy.bounds.max.z) / 2 + 1);
    }
    direction = qa_normalize3_double_hypot(qa_sub3_double(target, start));
  }
  c->weapons.fire_bullet(c->weapons.ctx, entity, game, start, direction, 3, 4, 300, 500, 0);
  qa_q2rm_monster_flash(c, 63, start, direction);
  state->hold_frame = q2rm_now(game) < state->fire_wait;
}
static bool targets_spawn_fn(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game) {
  if (strcmp(entity->classname, "target_actor") != 0) return false;
  if (strcmp(entity_text(entity->targetname), "") == 0) {
    char origin[96], message[256];
    json_origin(origin, sizeof origin, q2rm_body(game, entity).origin);
    (void)snprintf(message, sizeof message, "target_actor has no targetname at %s", origin);
    game->host->diagnostic(game->host->ctx, message);
  }
  entity->server_flags = 1;
  entity->visible = false;
  entity->touch.ctx = ((qa_q2rm_actor_module *)ctx)->monsters;
  entity->touch.fn = touch_fn;
  qa_q2f_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_bounds = true;
  change.bounds.min = q2rm_v3(-8, -8, -8);
  change.bounds.max = q2rm_v3(8, 8, 8);
  q2rm_move_body(game, entity, change, false, false);
  if (q2rm_has(entity->spawnflags, 1)) {
    if (entity->speed == 0) entity->speed = 200;
    qa_vec3 angles = q2rm_body(game, entity).angles;
    qa_vec3 movedir_angles = angles;
    movedir_angles.y = angles.y != 0 ? angles.y : 360;
    entity->movedir = movedir_of(movedir_angles);
    double height = qa_q2f_number_field(&entity->spawn, "height", 0);
    entity->movedir.z = height != 0 ? height : 200;
    memset(&change, 0, sizeof change);
    change.has_angles = true; change.angles = q2rm_zero();
    q2rm_move_body(game, entity, change, false, false);
  }
  game->solid(game->ctx, entity, QA_Q2F_SOLID_TRIGGER);
  return true;
}
static qa_q2f_callback_definitions targets_callbacks_fn(void *ctx) {
  qa_q2rm_actor_module *module = ctx;
  qa_q2f_callback_definitions definitions;
  memset(&definitions, 0, sizeof definitions);
  definitions.touch = module->touch_defs;
  definitions.touch_count = 1;
  return definitions;
}

void qa_q2rm_create_rerelease_actor_module(qa_q2rm_actor_module *out, qa_q2m_monsters *monsters) {
  memset(out, 0, sizeof *out);
  out->monsters = monsters;
  qa_q2m_definition definition = qa_q2bm_actor_definition;
  const qa_q2m_named_handler extra[] = {
    {"actor_dead", {NULL, dead_fn}},
    {"actor_fire", {NULL, fire_fn}},
  };
  size_t count = 0;
  for (size_t i = 0; i < definition.callback_count; i++) out->callbacks[count++] = definition.callbacks[i];
  for (size_t i = 0; i < sizeof extra / sizeof extra[0]; i++) {
    size_t j = 0;
    while (j < count && strcmp(out->callbacks[j].name, extra[i].name) != 0) j++;
    if (j < count) out->callbacks[j] = extra[i];
    else {
      if (count >= 16) qa_q2f_error("Rerelease actor callback table overflow");
      out->callbacks[count++] = extra[i];
    }
  }
  definition.moves = qa_q2rm_actor_moves;
  definition.move_count = qa_q2rm_actor_move_count;
  out->use_defs[0].name = "rerelease.actor.actor_use";
  out->use_defs[0].cb = (qa_q2f_use){monsters, use_fn};
  out->touch_defs[0].name = "rerelease.actor.target_actor_touch";
  out->touch_defs[0].cb = (qa_q2f_touch){monsters, touch_fn};
  definition.has_source_callbacks = true;
  memset(&definition.source_callbacks, 0, sizeof definition.source_callbacks);
  definition.source_callbacks.use = out->use_defs;
  definition.source_callbacks.use_count = 1;
  definition.initialize = (qa_q2m_handler){NULL, init_fn};
  definition.after_spawn = (qa_q2m_handler){monsters, after_spawn_fn};
  definition.attack = (qa_q2m_handler){NULL, attack_fn};
  definition.reactions.pain = pain_fn;
  definition.callbacks = out->callbacks;
  definition.callback_count = count;
  out->definition = definition;
}
qa_q2bm_spawn_module qa_q2rm_actor_targets_as_spawn(qa_q2rm_actor_module *module) {
  qa_q2bm_spawn_module spawn = {module, targets_callbacks_fn, targets_spawn_fn};
  return spawn;
}
