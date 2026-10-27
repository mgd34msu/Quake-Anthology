/* Port of src/content/q2/base/monsters/actor.ts. GPL-2.0-or-later. */
#include "qa/content/q2/base/monsters/actor.h"

#include <math.h>
#include <stdio.h>

#include "base_internal.h"
#include "qa/content/q2/base/monsters/tables/actor.h"
#include "qa/content/q2/foundation/monsters/ai.h"
#include "qa/content/q2/foundation/monsters/gibs.h"
#include "qa/content/q2/foundation/monsters/muzzle.h"
#include "qa/core/numeric.h"

static qa_q2m_monsters *actor_use_monsters;
static const char *const actor_names[8] = {"Hellrot", "Tokay", "Killme", "Disruptor",
                                           "Adrianator", "Rambear", "Titus", "Bitterman"};
const char *qa_q2bm_actor_name(qa_q2f_entity *entity, qa_q2f_game *game) {
  qa_q2f_saved_result source = game->host->actors.source_of(game->host->actors.ctx, entity->actor.id);
  double slot = source.has_value ? source.value.slot : (double)entity->actor.id.slot;
  return actor_names[qa_q2m_record_at(8, fmod(slot, 8))];
}
/* fields.ts integerField over the qa_q2f spawn mirror (nativeAtoi). */
static double integer_field(const qa_q2f_spawn_fields *fields, const char *key) {
  const char *value = qa_q2f_spawn_value(fields, key);
  return value ? (double)qa_native_atoi(value) : 0;
}
static void json_origin(char *buf, size_t size, qa_vec3 origin) {
  snprintf(buf, size, "{\"x\":%g,\"y\":%g,\"z\":%g}", origin.x, origin.y, origin.z);
}
static void stand_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2bm_set_move(c, "actor_move_stand");
  if (q2bm_now(c->game) < 1) {
    double lo = qa_q2bm_actor_frames_value.stand101, hi = qa_q2bm_actor_frames_value.stand140;
    c->entity->frame = lo + floor(q2bm_rnd(c->game) * (hi - lo + 1));
  }
}
static void run_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (q2bm_now(c->game) < c->state->pain_time && !c->entity->has_enemy) {
    if (c->state->has_move_target) q2bm_set_move(c, "actor_move_walk");
    else stand_fn(NULL, c);
    return;
  }
  if (c->state->stand_ground) stand_fn(NULL, c);
  else q2bm_set_move(c, "actor_move_run");
}
static void actor_use_fn(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game,
                         const qa_actor_id *other, const qa_actor_id *activator);
static qa_q2f_use resolve_actor_use(void) {
  if (!actor_use_monsters) qa_q2f_error("Q2 restore cannot resolve source callback actor_use");
  return (qa_q2f_use){actor_use_monsters, actor_use_fn};
}
static void initialize_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2f_entity *entity = c->entity;
  qa_q2f_game *game = c->game;
  if (strcmp(entity->targetname ? entity->targetname : "", "") == 0
      || strcmp(entity->target ? entity->target : "", "") == 0) {
    char origin[96], message[256];
    json_origin(origin, sizeof origin, q2bm_body(game, entity).origin);
    snprintf(message, sizeof message, "misc_actor requires target and targetname at %s", origin);
    game->host->diagnostic(game->host->ctx, message);
    game->remove(game->ctx, entity);
    return;
  }
  c->state->good_guy = true;
  double health = integer_field(&entity->spawn, "health");
  entity->max_health = health != 0 ? health : 100;
  game->host->combat.set_health(game->host->combat.ctx, entity->actor, entity->max_health);
  entity->use = resolve_actor_use();
}
static void attack_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2bm_set_move(c, "actor_move_attack");
  c->state->pause_time = q2bm_now(c->game) + (floor(q2bm_rnd(c->game) * 16) + 10) * 0.1;
}
static void pain_fn(void *ctx, qa_q2m_context *c, qa_pain_reaction reaction) {
  (void)ctx;
  qa_q2bm_damaged_skin(c);
  if (q2bm_now(c->game) < c->state->pain_time) return;
  c->state->pain_time = q2bm_now(c->game) + 3;
  if (reaction.attacker != NULL && c->game->host->is_player(c->game->host->ctx, *reaction.attacker)
      && q2bm_rnd(c->game) < 0.4) {
    qa_body_state_result other = c->game->host->bodies.read(c->game->host->bodies.ctx, *reaction.attacker);
    if (other.has_value)
      c->state->ideal_yaw = qa_vector_to_angles_with_rule(
          qa_sub3_double(other.value.origin, q2bm_body(c->game, c->entity).origin),
          QA_VECTOR_TO_ANGLES_NATIVE_AI).y;
    q2bm_set_move(c, q2bm_rnd(c->game) < 0.5 ? "actor_move_flipoff" : "actor_move_taunt");
    static const char *const messages[4] = {"Watch it", "#$@*&", "Idiot", "Check your targets"};
    char text[256];
    snprintf(text, sizeof text, "%s: %s!\n", qa_q2bm_actor_name(c->entity, c->game),
             messages[qa_q2m_record_at(4, floor(q2bm_rnd(c->game) * 3))]);
    qa_q2bm_extended_event event;
    memset(&event, 0, sizeof event);
    event.kind = "print"; event.actor = *reaction.attacker; event.level = "chat"; event.text = text;
    qa_q2bm_emit_extended(event);
    return;
  }
  double n = floor(q2bm_rnd(c->game) * 3);
  q2bm_set_move(c, n == 0 ? "actor_move_pain1" : n == 1 ? "actor_move_pain2" : "actor_move_pain3");
}
static void die_fn(void *ctx, qa_q2m_context *c, qa_death_reaction reaction) {
  (void)ctx;
  qa_q2f_entity *entity = c->entity;
  qa_q2f_game *game = c->game;
  qa_q2m_state *state = c->state;
  double damage = reaction.pain.damage;
  if (qa_q2m_health(game, &entity->actor.id) <= -80) {
    qa_q2m_gib_options plain = {.head = false, .metallic = false, .skinned = false, .upright = false};
    for (int i = 0; i < 2; i++) (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/bone/tris.md2", damage, plain);
    for (int i = 0; i < 4; i++) (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/sm_meat/tris.md2", damage, plain);
    (void)qa_q2m_throw_head(entity, game, "models/objects/gibs/head2/tris.md2", damage);
    state->dead = true; state->gibbed = true;
    return;
  }
  if (state->dead) return;
  state->dead = true; state->can_take_damage = true;
  q2bm_set_traits_damage(game, entity, true);
  q2bm_set_move(c, q2bm_rnd(game) < 0.5 ? "actor_move_death1" : "actor_move_death2");
}
static void fire_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2f_entity *entity = c->entity;
  qa_q2f_game *game = c->game;
  qa_body_state enemy;
  bool has_enemy_body = qa_q2m_enemy_body(c, &enemy);
  qa_vec3 start = qa_q2m_project_flash_default(c, qa_q2m_muzzle_offset(game->options.edition, 63));
  qa_vec3 direction = qa_math_angle_vectors_rule(q2bm_body(game, entity).angles, QA_ANGLE_RULE_NATIVE).forward;
  if (has_enemy_body) {
    qa_vec3 target;
    if (qa_q2m_health(game, entity->has_enemy ? &entity->enemy : NULL) > 0) {
      qa_q2f_entity *enemy_entity = entity->has_enemy ? game->entity(game->ctx, &entity->enemy) : NULL;
      target = qa_add3_double(qa_add3_double(enemy.origin, qa_scale3_double(enemy.velocity, -0.2)),
                              q2bm_v3(0, 0, enemy_entity ? enemy_entity->view_height : 22));
    } else {
      target = q2bm_v3(enemy.origin.x + enemy.bounds.min.x, enemy.origin.y + enemy.bounds.min.y,
                       enemy.origin.z + (enemy.bounds.min.z + enemy.bounds.max.z) / 2);
    }
    direction = qa_normalize3_double_hypot(qa_sub3_double(target, start));
  }
  c->weapons.fire_bullet(c->weapons.ctx, entity, game, start, direction, 3, 4, 300, 500, 0);
  qa_q2bm_muzzle(c, 63, direction, start);
  c->state->hold_frame = q2bm_now(game) < c->state->pause_time;
}
#define CB(name, fn) {name, {NULL, fn}}
static const qa_q2m_named_handler actor_callbacks[] = {
  CB("actor_run", run_fn), CB("actor_dead", qa_q2bm_finish_corpse_handler), CB("actor_fire", fire_fn),
};
const qa_q2m_definition qa_q2bm_actor_definition = {
  .classname = "misc_actor", .kind = "actor", .model = "players/male/tris.md2",
  .health = 100, .gib_health = -80, .mass = 200, .bounds = {{-16, -16, -24}, {16, 16, 32}}, .scale = 1,
  .initial_move = "actor_move_stand",
  .moves = qa_q2bm_actor_moves, .move_count = 11,
  .callbacks = actor_callbacks, .callback_count = sizeof actor_callbacks / sizeof actor_callbacks[0],
  .stand = {NULL, stand_fn},
  .walk = {(void *)"actor_move_walk", qa_q2bm_move_handler_fn},
  .run = {NULL, run_fn}, .attack = {NULL, attack_fn},
  .initialize = {NULL, initialize_fn},
  .reactions = {NULL, pain_fn, die_fn},
};

static void actor_use_fn(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game,
                         const qa_actor_id *other, const qa_actor_id *activator) {
  (void)other; (void)activator;
  qa_q2m_context *context = qa_q2m_monsters_context(ctx, entity->actor.id);
  if (context == NULL) qa_q2f_error("Actor use without restored monster state");
  qa_q2m_state *state = context->state;
  qa_q2f_entity *target = game->pick_target(game->ctx, entity->target ? entity->target : "");
  if (target) {
    entity->has_goal = true; entity->goal = target->actor.id;
    state->has_move_target = true; state->move_target = target->actor.id;
  } else {
    entity->has_goal = false; state->has_move_target = false;
  }
  if (target == NULL || strcmp(target->classname ? target->classname : "", "target_actor") != 0) {
    char message[256];
    snprintf(message, sizeof message, "misc_actor has bad target %s", entity->target ? entity->target : "");
    game->host->diagnostic(game->host->ctx, message);
    entity->target = "";
    state->pause_time = 100000000;
    stand_fn(NULL, context);
    return;
  }
  state->ideal_yaw = qa_vector_to_angles_with_rule(
      qa_sub3_double(q2bm_body(game, target).origin, q2bm_body(game, entity).origin),
      QA_VECTOR_TO_ANGLES_NATIVE_AI).y;
  qa_vec3 angles = q2bm_body(game, entity).angles;
  angles.y = state->ideal_yaw;
  qa_q2f_body_changes changes;
  memset(&changes, 0, sizeof changes);
  changes.has_angles = true; changes.angles = angles;
  game->move(game->ctx, entity, changes, false, false);
  context->walk(context->impl_ctx, context);
  entity->target = "";
}
static void target_actor_touch_fn(void *ctx, qa_q2f_entity *self, qa_q2f_game *services,
                                  qa_touch_contact contact) {
  qa_q2f_entity *actor = services->entity(services->ctx, &contact.other);
  qa_q2m_context *context = qa_q2m_monsters_context(ctx, contact.other);
  if (actor == NULL || context == NULL || !context->state->has_move_target
      || !qa_same_actor(context->state->move_target, self->actor.id) || actor->has_enemy)
    return;
  actor->has_goal = false;
  context->state->has_move_target = false;
  if (strcmp(self->message ? self->message : "", "") != 0) {
    size_t count = 0;
    const qa_actor_id *players = services->host->players(services->host->ctx, &count);
    for (size_t i = 0; i < count; i++) {
      char text[512];
      snprintf(text, sizeof text, "%s: %s\n", qa_q2bm_actor_name(actor, services), self->message);
      qa_q2bm_extended_event event;
      memset(&event, 0, sizeof event);
      event.kind = "print"; event.actor = players[i]; event.level = "chat"; event.text = text;
      qa_q2bm_emit_extended(event);
    }
  }
  uint32_t flags = (uint32_t)self->spawnflags;
  if ((flags & 1u) != 0) {
    qa_body_state body = services->body(services->ctx, actor->actor);
    qa_vec3 velocity = {self->movedir.x * self->speed, self->movedir.y * self->speed,
                        body.ground != NULL ? self->movedir.z : body.velocity.z};
    qa_q2f_body_changes changes;
    memset(&changes, 0, sizeof changes);
    changes.has_velocity = true; changes.velocity = velocity;
    changes.has_ground = true; changes.ground = NULL;
    services->move(services->ctx, actor, changes, false, false);
    if (body.ground != NULL)
      services->sound(services->ctx, actor->actor, "player/male/jump1.wav", true, 2, false, 0, false, 0);
  }
  const char *path_target = qa_q2f_spawn_value(&self->spawn, "pathtarget");
  if (!path_target) path_target = "";
  if ((flags & 2u) == 0 && (flags & 4u) != 0) {
    qa_q2f_entity *picked = services->pick_target(services->ctx, path_target);
    if (picked) { actor->has_enemy = true; actor->enemy = picked->actor.id; }
    else actor->has_enemy = false;
    if (actor->has_enemy) {
      actor->has_goal = true; actor->goal = actor->enemy;
      if ((flags & 32u) != 0) context->state->brutal = true;
      if ((flags & 16u) != 0) { context->state->stand_ground = true; stand_fn(NULL, context); }
      else run_fn(NULL, context);
    }
  }
  if ((flags & 6u) == 0 && strcmp(path_target, "") != 0) {
    const char *original = self->target;
    self->target = path_target;
    services->use_targets(services->ctx, self, &actor->actor.id, false, false);
    self->target = original;
  }
  qa_q2f_entity *next = services->pick_target(services->ctx, self->target ? self->target : "");
  if (next) { context->state->has_move_target = true; context->state->move_target = next->actor.id; }
  else context->state->has_move_target = false;
  if (!actor->has_goal && context->state->has_move_target) {
    actor->has_goal = true; actor->goal = context->state->move_target;
  }
  if (!context->state->has_move_target && !actor->has_enemy) {
    context->state->pause_time = q2bm_now(services) + 100000000;
    stand_fn(NULL, context);
    return;
  }
  qa_q2f_entity *goal = context->state->has_move_target
      ? services->entity(services->ctx, &context->state->move_target) : NULL;
  if (goal != NULL && actor->has_goal && qa_same_actor(actor->goal, goal->actor.id))
    context->state->ideal_yaw = qa_vector_to_angles_with_rule(
        qa_sub3_double(services->body(services->ctx, goal->actor).origin,
                       services->body(services->ctx, actor->actor).origin),
        QA_VECTOR_TO_ANGLES_NATIVE_AI).y;
}
static qa_vec3 movedir_of(qa_vec3 angles) {
  if (angles.x == 0 && angles.y == -1 && angles.z == 0) return q2bm_v3(0, 0, 1);
  if (angles.x == 0 && angles.y == -2 && angles.z == 0) return q2bm_v3(0, 0, -1);
  return qa_math_angle_vectors_rule(q2bm_v3(angles.x, angles.y, 0), QA_ANGLE_RULE_NATIVE).forward;
}
static bool actor_target_spawn_fn(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game) {
  if (strcmp(entity->classname ? entity->classname : "", "target_actor") != 0) return false;
  if (strcmp(entity->targetname ? entity->targetname : "", "") == 0) {
    char origin[96], message[256];
    json_origin(origin, sizeof origin, q2bm_body(game, entity).origin);
    snprintf(message, sizeof message, "target_actor has no targetname at %s", origin);
    game->host->diagnostic(game->host->ctx, message);
  }
  entity->server_flags = 1;
  entity->visible = false;
  qa_q2f_body_changes bounds_change;
  memset(&bounds_change, 0, sizeof bounds_change);
  bounds_change.has_bounds = true;
  bounds_change.bounds = (qa_bounds){{-8, -8, -8}, {8, 8, 8}};
  game->move(game->ctx, entity, bounds_change, false, false);
  if (((uint32_t)entity->spawnflags & 1u) != 0) {
    if (entity->speed == 0) entity->speed = 200;
    qa_vec3 angles = q2bm_body(game, entity).angles;
    angles.y = angles.y != 0 ? angles.y : 360;
    double height = qa_q2f_number_field(&entity->spawn, "height", 0);
    entity->movedir = movedir_of(angles);
    entity->movedir.z = height != 0 ? height : 200;
    qa_q2f_body_changes zero_angles;
    memset(&zero_angles, 0, sizeof zero_angles);
    zero_angles.has_angles = true; zero_angles.angles = q2bm_zero();
    game->move(game->ctx, entity, zero_angles, false, false);
  }
  game->solid(game->ctx, entity, QA_Q2F_SOLID_TRIGGER);
  entity->touch = (qa_q2f_touch){((qa_q2bm_actor_target_module *)ctx)->monsters, target_actor_touch_fn};
  return true;
}
static qa_q2f_callback_definitions actor_target_callbacks_fn(void *ctx) {
  qa_q2bm_actor_target_module *module = ctx;
  qa_q2f_callback_definitions defs;
  memset(&defs, 0, sizeof defs);
  defs.use = module->use_defs; defs.use_count = 1;
  defs.touch = module->touch_defs; defs.touch_count = 1;
  return defs;
}
void qa_q2bm_create_actor_target_module(qa_q2bm_actor_target_module *out, qa_q2m_monsters *monsters) {
  memset(out, 0, sizeof *out);
  out->monsters = monsters;
  out->use_defs[0].name = "actor_use";
  out->use_defs[0].cb = (qa_q2f_use){monsters, actor_use_fn};
  out->touch_defs[0].name = "target_actor_touch";
  out->touch_defs[0].cb = (qa_q2f_touch){monsters, target_actor_touch_fn};
  actor_use_monsters = monsters;
}
qa_q2bm_spawn_module qa_q2bm_actor_target_as_spawn(qa_q2bm_actor_target_module *module) {
  qa_q2bm_spawn_module spawn = {module->monsters, NULL, actor_target_spawn_fn};
  spawn.callbacks = actor_target_callbacks_fn;
  spawn.ctx = module;
  return spawn;
}
