/* Port of src/content/q1/addons/monsters/bosses/orb.ts. GPL-2.0-or-later. */
#include "qa/content/q1/addons/monsters/bosses/orb.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/addons/monsters/bosses/effects.h"
#include "qa/content/q1/addons/monsters/bosses/frames/orb.h"
#include "qa/content/q1/addons/monsters/bosses/registry.h"
#include "qa/content/q1/addons/monsters/startup.h"
#include "qa/foundation/support.h"

#define FLAGS(x) ((int32_t)(x))

static const char *const orb_classnames[] = {"monster_orb"};
static const qa_q1am_species orb_spec = {
  "wizard", NULL, "teleporter_eye_blink", NULL, "orb_stand1", "orb_walk1", "orb_run1",
  "", "orb_fast1", "fly", orb_classnames, 1, NULL, 0,
  300, -INFINITY, {{-32, -32, -24}, {32, 32, 64}}, false,
};
static void orb_dispatch(qa_q1am_monster *monster, const char *name) {
  qa_q1am_orb_action(monster->owner, name);
}
static const qa_q1am_source *orb_source(void) {
  static const qa_q1am_source *source;
  if (source == NULL)
    source = qa_q1am_boss_build_source("mg3:orb", &qa_q1am_orb_frames, QA_Q1AM_KIND_ORB,
                                       "Orb callback received another controller", orb_dispatch, NULL, 0);
  return source;
}
static qa_q1am_orb *as_orb(qa_q1am_monster *m) { return m->owner; }
static void set_attack_state(qa_q1am_actor *entity, const char *state) {
  (void)snprintf(entity->attack_state, sizeof entity->attack_state, "%s", state);
}
static void orb_spawn(qa_q1am_monster *m) {
  qa_q1am_orb *self = as_orb(m);
  qa_q1am_game *game = m->game;
  qa_q1am_actor *entity = m->entity;
  game->combat_set_health(game->ctx, entity->actor, 300);
  entity->max_health = 300;
  entity->pain = game->named_pain(game->ctx, entity, "mg3:orb:monster_pain");
  entity->die = game->named_die(game->ctx, entity, "mg3:orb:monster_die");
  entity->path_end = game->named_action(game->ctx, entity, "mg3:orb:monster_stand");
  self->context->set_number(self->context->ctx, entity, "combat_style", 1);
  qa_q1am_init_mg3_monster(m, self->context, "progs/teleporter_eye_blink.mdl", 2, 2);
}
static void orb_start(qa_q1am_monster *m) { qa_q1am_start_mg3_monster(m, as_orb(m)->context); }
static void orb_use(qa_q1am_monster *m, bool has, qa_actor_id activator) {
  qa_actor_id resolved;
  bool resolved_has = qa_q1am_mg3_monster_activator(m->game, has, activator, &resolved);
  as_orb(m)->super_ops.use(m, resolved_has, resolved);
}
static void orb_pain(qa_q1am_monster *m, bool has, qa_actor_id attacker, double damage) {
  qa_q1am_monster_retaliate(m, has, attacker);
  qa_q1am_game *game = m->game;
  if (m->state->pain_finished > game->time || game->random(game->ctx) * 200 > damage) return;
  game->sound(game->ctx, m->entity, "orb/orb_pain.wav", "voice", 1);
  m->state->pain_finished = game->time + 8;
  m->ops.play(m, "orb_pain1");
}
static void orb_die(qa_q1am_monster *m, bool has, qa_actor_id attacker) {
  if (m->counted_death) return;
  m->state->has_enemy = has;
  m->state->enemy = attacker;
  qa_q1am_actor_set_damageable(m->game, m->entity, false);
  m->entity->touch.present = false;
  qa_q1am_monster_count_kill(m);
  m->ops.play(m, "orb_death1");
}
static bool orb_try_attack(qa_q1am_monster *m) {
  qa_q1am_game *game = m->game;
  qa_q1am_actor *entity = m->entity;
  double range = game->world ? qa_q1am_actor_number(game->world, "enemy_range") : 0;
  if (game->time < m->state->attack_finished ||
      (game->world ? qa_q1am_actor_number(game->world, "enemy_visible") : 0) == 0)
    return false;
  if (range == 3) {
    if (m->sliding || strcmp(entity->attack_state, "straight") != 0) {
      m->sliding = false;
      set_attack_state(entity, "straight");
      m->ops.play(m, "wiz_run1");
    }
    return false;
  }
  qa_vec3 target, origin;
  bool has_target = m->state->has_enemy && qa_q1am_monster_eye(m, m->state->enemy, &target);
  bool has_origin = qa_q1am_monster_eye(m, entity->actor.id, &origin);
  if (!has_target || !has_origin) return false;
  qa_q1am_trace_request request = {origin, target, QA_Q1AM_POINT, true, entity->actor.id, true};
  qa_q1am_trace trace = game->trace(game->ctx, &request);
  if (!trace.has_actor || !m->state->has_enemy || !qa_same_actor(trace.actor, m->state->enemy)) {
    if (m->sliding || strcmp(entity->attack_state, "straight") != 0) {
      m->sliding = false;
      set_attack_state(entity, "straight");
      m->ops.play(m, "orb_run1");
    }
    return false;
  }
  if (game->random(game->ctx) < (range == 0 ? 0.9 : range == 1 ? 0.6 : range == 2 ? 0.2 : 0)) {
    m->sliding = false;
    set_attack_state(entity, "missile");
    return true;
  }
  if (range == 2) {
    if (m->sliding || strcmp(entity->attack_state, "straight") != 0) {
      m->sliding = false;
      set_attack_state(entity, "straight");
      m->ops.play(m, "orb_run1");
    }
  } else if (!m->sliding) {
    set_attack_state(entity, "straight");
    m->sliding = true;
    m->ops.play(m, "orb_side1");
  }
  return false;
}
static void idle_sound(qa_q1am_orb *orb) {
  qa_q1am_monster *m = &orb->base;
  qa_q1am_game *game = m->game;
  if (qa_q1am_actor_number(m->entity, "waitmin") < game->time) {
    orb->context->set_number(orb->context->ctx, m->entity, "waitmin",
                             game->time + 15 + game->random(game->ctx) * 10);
    game->sound(game->ctx, m->entity, "boss2/sight.wav", "voice", 2);
  }
}
void qa_q1am_orb_action(qa_q1am_orb *orb, const char *name) {
  qa_q1am_monster *m = &orb->base;
  qa_q1am_game *game = m->game;
  qa_q1am_actor *entity = m->entity;
  qa_q1am_context *context = orb->context;
  if (strcmp(name, "orb:orb_stand1") == 0) {
    m->ops.ai(m, QA_Q1AM_AI_STAND, 0);
  } else if (strcmp(name, "orb:orb_walk1") == 0) {
    m->ops.ai(m, QA_Q1AM_AI_WALK, 8);
    idle_sound(orb);
  } else if (strcmp(name, "orb:orb_side1") == 0) {
    qa_q1am_monster_face(m);
    m->ops.ai(m, QA_Q1AM_AI_RUN, 16);
    idle_sound(orb);
  } else if (strcmp(name, "orb:orb_run1") == 0) {
    m->ops.ai(m, QA_Q1AM_AI_RUN, 16);
    idle_sound(orb);
  } else if (strcmp(name, "orb:orb_fast1") == 0) {
    context->set_number(context->ctx, entity, "ammo_nails", 1 + floor(game->random(game->ctx) * 3 + 0.5));
    qa_q1am_monster_face(m);
  } else if (strcmp(name, "orb:orb_fast3") == 0) {
    qa_q1am_monster_delay(m, 0.3);
    qa_q1am_monster_face(m);
  } else if (strcmp(name, "orb:orb_fast4") == 0) {
    qa_q1am_monster_face(m);
    qa_q1am_orb_blast(orb);
    qa_q1am_monster_delay(m, 0.2);
    context->set_number(context->ctx, entity, "ammo_nails", qa_q1am_actor_number(entity, "ammo_nails") - 1);
    if (qa_q1am_actor_number(entity, "ammo_nails") != 0)
      qa_q1am_monster_set_frame(m->next_frame, sizeof m->next_frame, "orb_fast2");
  } else if (strcmp(name, "orb:orb_fast5") == 0) {
    qa_q1am_monster_face(m);
    m->ops.attack_finished(m, 2);
    set_attack_state(entity, "straight");
    m->sliding = (game->world ? qa_q1am_actor_number(game->world, "enemy_range") : 0) < 2 &&
                 (game->world ? qa_q1am_actor_number(game->world, "enemy_visible") : 0) != 0;
    qa_q1am_monster_set_frame(m->next_frame, sizeof m->next_frame, m->sliding ? "orb_side1" : "orb_run1");
  } else if (strcmp(name, "orb:orb_pain1") == 0) {
    context->set_number(context->ctx, entity, "ammo_shells", 3);
    qa_q1am_monster_delay(m, 0.2);
  } else if (strcmp(name, "orb:orb_pain2") == 0) {
    qa_q1am_pain_lightning(context, entity, QA_Q1AM_ZERO);
    qa_q1am_monster_delay(m, 0.3);
    context->set_number(context->ctx, entity, "ammo_shells", qa_q1am_actor_number(entity, "ammo_shells") - 1);
    if (qa_q1am_actor_number(entity, "ammo_shells") != 0)
      qa_q1am_monster_set_frame(m->next_frame, sizeof m->next_frame, "orb_pain2");
  } else if (strcmp(name, "orb:orb_death1") == 0) {
    qa_q1am_body_patch patch = {0};
    patch.has_velocity = true;
    patch.velocity = (qa_vec3){-200 + 400 * game->random(game->ctx),
                               -200 + 400 * game->random(game->ctx),
                               150 + 150 * game->random(game->ctx)};
    patch.has_ground = true;
    patch.ground_null = true;
    game->set_body(game->ctx, entity, &patch);
    entity->movement_flags = (double)(FLAGS(entity->movement_flags) & ~512);
    entity->solid = "bbox";
    game->set_bounds(game->ctx, entity, QA_Q1AM_POINT);
    game->sound(game->ctx, entity, "orb/orb_death.wav", "voice", 1);
  } else if (strcmp(name, "orb:orb_death3") == 0) {
    entity->touch = game->named_touch(game->ctx, entity, "mg3:orb:death_touch");
  } else if (strcmp(name, "orb:orb_death4") == 0) {
    qa_q1am_monster_delay(m, 9999);
  } else {
    qa_range_error("Unknown orb action %s", name);
  }
}
void qa_q1am_orb_blast(qa_q1am_orb *orb) {
  qa_q1am_monster *m = &orb->base;
  qa_q1am_game *game = m->game;
  qa_q1am_actor *entity = m->entity;
  double speed = game->options.skill > 2 ? 500 : game->options.skill > 0 ? 450 : 400;
  double count = 4 + floor(game->random(game->ctx) * 2 + 0.5);
  qa_vec3 target_point;
  qa_vec3 target = qa_q1am_monster_target(m, &target_point) ? target_point : QA_Q1AM_ZERO;
  qa_q1am_basis basis = qa_q1am_game_make_vectors(
      game, qa_q1am_velocity_angles(qa_sub3(target, qa_q1am_monster_origin(m))));
  qa_vec3 origin = qa_add3(qa_q1am_monster_origin(m), qa_scale3(basis.forward, 15));
  double time = qa_length3(qa_sub3(target, origin)) / speed;
  qa_q1am_body enemy_body;
  qa_vec3 target_velocity = m->state->has_enemy && game->body_read(game->ctx, m->state->enemy, &enemy_body)
                                ? enemy_body.velocity : QA_Q1AM_ZERO;
  target_velocity.z = 0;
  qa_vec3 projected = qa_add3(target, qa_scale3(target_velocity, time / 4));
  qa_vec3 direction = qa_q1am_normalize(qa_sub3(projected, origin));
  game->effect(game->ctx, "explosion", origin, false, (qa_actor_id){0}, 1);
  while (count > 0) {
    qa_vec3 aim = qa_q1am_normalize(qa_add3(
        qa_add3(direction, qa_scale3(game->basis.right, (game->random(game->ctx) * 2 - 1) * 0.1)),
        qa_scale3(game->basis.up, (game->random(game->ctx) * 2 - 1) * 0.1)));
    qa_q1am_actor *missile = game->launch_spike(game->ctx, game, true, entity->actor.id,
                                                qa_add3(origin, qa_scale3(aim, 8)), qa_scale3(aim, 1000));
    (void)snprintf(missile->classname, sizeof missile->classname, "%s", "rock");
    (void)snprintf(missile->model, sizeof missile->model, "%s", "progs/rogue/sphere.mdl");
    qa_q1am_body_patch patch = {0};
    patch.has_velocity = true;
    patch.velocity = qa_scale3(aim, speed + (game->random(game->ctx) * 2 - 1) * 100);
    patch.has_bounds = true;
    patch.bounds = QA_Q1AM_POINT;
    game->set_body(game->ctx, missile, &patch);
    missile->angular_velocity = (qa_vec3){300 * (game->random(game->ctx) * 2 - 1),
                                          300 * (game->random(game->ctx) * 2 - 1),
                                          300 * (game->random(game->ctx) * 2 - 1)};
    if ((int)count % 2 == 0) {
      orb->context->set_number(orb->context->ctx, missile, "frags", 1);
      missile->effects = (double)((int32_t)missile->effects | 64);
    }
    missile->touch = game->named_touch(game->ctx, missile, "mg3:orb:missile_touch");
    count--;
  }
}
void qa_q1am_orb_death_touch(qa_q1am_orb *orb, qa_actor_id other) {
  qa_q1am_monster *m = &orb->base;
  qa_q1am_game *game = m->game;
  qa_q1am_actor *entity = m->entity;
  qa_q1am_actor *target = game->entity(game->ctx, true, other);
  if ((target && strcmp(target->solid, "trigger") == 0) ||
      (target && strcmp(target->solid, "bbox") == 0 && game->health(game->ctx, other) == 0))
    return;
  qa_q1am_event event = {0};
  event.kind = "colored-explosion";
  event.origin = qa_q1am_monster_origin(m);
  event.color_start = 244;
  event.color_length = 3;
  orb->context->services_emit(orb->context->services_ctx, &event);
  game->radius_damage(game->ctx, entity->actor.id, true, entity->actor.id, 100,
                      game->world != NULL, game->world ? game->world->actor.id : (qa_actor_id){0}, NULL);
  game->remove(game->ctx, entity);
}
void qa_q1am_orb_init(qa_q1am_orb *orb, qa_q1am_context *context, qa_q1am_actor *entity) {
  memset(orb, 0, sizeof *orb);
  orb->context = context;
  qa_q1am_monster_init(&orb->base, context->game, entity, &orb_spec, context->base, orb_source());
  orb->base.owner = orb;
  qa_q1am_mg3_install(&orb->base);
  orb->base.kind = QA_Q1AM_KIND_ORB;
  orb->super_ops = orb->base.ops;
  orb->base.ops.spawn = orb_spawn;
  orb->base.ops.start = orb_start;
  orb->base.ops.use = orb_use;
  orb->base.ops.pain = orb_pain;
  orb->base.ops.die = orb_die;
  orb->base.ops.try_attack = orb_try_attack;
}
static qa_q1am_monster *create_orb(void *ctx, qa_q1am_actor *entity) {
  qa_q1am_orb *orb = malloc(sizeof *orb);
  if (!orb) qa_range_error("Orb allocation failed");
  qa_q1am_orb_init(orb, ctx, entity);
  return &orb->base;
}
static void destroy_orb(void *ctx, qa_q1am_monster *monster) {
  (void)ctx;
  free(monster->owner);
}
static void death_touch_handler(void *ctx, qa_q1am_game *game, qa_q1am_actor *entity, qa_actor_id other,
                                bool has_normal, qa_vec3 normal) {
  (void)game; (void)has_normal; (void)normal;
  qa_q1am_orb_death_touch(qa_q1am_require_boss(ctx, entity)->owner, other);
}
static void missile_touch_handler(void *ctx, qa_q1am_game *game, qa_q1am_actor *entity, qa_actor_id other,
                                  bool has_normal, qa_vec3 normal) {
  (void)ctx; (void)has_normal; (void)normal;
  if (entity->has_owner && qa_same_actor(entity->owner, other)) return;
  const char *classname = game->classname_of(game->ctx, other);
  if (strcmp(classname, "monster_orb") == 0 || strcmp(classname, "monster_lava_man") == 0 ||
      strcmp(classname, "monster_super_shambler") == 0) {
    game->remove(game->ctx, entity);
    return;
  }
  qa_q1am_actor *target = game->entity(game->ctx, true, other);
  if (strcmp(classname, entity->classname) == 0 || (target && strcmp(target->solid, "trigger") == 0)) return;
  qa_vec3 origin = game->body(game->ctx, entity).origin;
  if (strcmp(game->contents(game->ctx, origin), "sky") == 0) {
    game->remove(game->ctx, entity);
    return;
  }
  if (game->can_take_damage(game->ctx, other)) {
    game->effect(game->ctx, "blood", origin, true, other, 18);
    game->damage(game->ctx, other, entity->actor.id, entity->has_owner, entity->owner, 18);
  } else if (qa_q1am_actor_number(entity, "frags") != 0) {
    game->effect(game->ctx, "knight-spike", origin, false, (qa_actor_id){0}, 1);
  }
  game->remove(game->ctx, entity);
}
void qa_q1am_register_orb(qa_q1am_context *context) {
  qa_q1am_game *game = context->game;
  qa_q1am_boss_create create = {context, create_orb, destroy_orb};
  qa_q1am_boss_registry *monsters =
      qa_q1am_register_boss_controllers(context, "mg3:orb", "monster_orb", create);
  qa_q1am_handlers handlers = {0};
  handlers.ctx = monsters;
  handlers.touch = death_touch_handler;
  game->named_register(game->ctx, "mg3:orb:death_touch", handlers);
  handlers.touch = missile_touch_handler;
  game->named_register(game->ctx, "mg3:orb:missile_touch", handlers);
}
void qa_q1am_launch_orb_missile(qa_q1am_monster *monster, qa_q1am_actor *missile,
                                double speed, double accuracy) {
  qa_q1am_game *game = monster->game;
  qa_q1am_actor *entity = monster->entity;
  qa_q1am_body target;
  bool has_target = monster->state->has_enemy &&
                    game->body_read(game->ctx, monster->state->enemy, &target);
  qa_q1am_basis basis = qa_q1am_game_make_vectors(game, game->body(game->ctx, entity).angles);
  qa_vec3 origin = game->body(game->ctx, missile).origin;
  qa_vec3 target_origin = has_target
      ? qa_add3(qa_add3(target.origin, target.bounds.min),
                qa_scale3(qa_sub3(target.bounds.max, target.bounds.min), 0.7))
      : QA_Q1AM_ZERO;
  qa_vec3 delta = qa_sub3(target_origin, origin);
  double travel = qa_length3(delta) / speed;
  qa_vec3 velocity = has_target ? target.velocity : QA_Q1AM_ZERO;
  velocity.z = 0;
  qa_vec3 direction = qa_add3(
      qa_add3(qa_q1am_normalize(qa_add3(delta, qa_scale3(velocity, travel))),
              qa_scale3(basis.up, accuracy * (game->random(game->ctx) - 0.5))),
      qa_scale3(game->basis.right, accuracy * (game->random(game->ctx) - 0.5)));
  qa_q1am_body_patch patch = {0};
  patch.has_velocity = true;
  patch.velocity = qa_scale3(direction, speed);
  patch.has_angles = true;
  patch.angles = (qa_vec3){0, qa_q1am_yaw_for(direction), 0};
  game->set_body(game->ctx, missile, &patch);
  game->schedule(game->ctx, missile, 5, game->named_action(game->ctx, missile, "SUB_Remove"));
}
