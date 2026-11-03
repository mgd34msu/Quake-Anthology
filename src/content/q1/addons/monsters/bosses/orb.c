/* Port of src/content/q1/addons/monsters/bosses/orb.ts.
 * quakec_mg3/monsters/mg3_orb.qc. GPL-2.0-or-later. */
#include "qa/content/q1/addons/monsters/bosses/orb.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/addons/monsters/bosses/effects.h"
#include "qa/content/q1/addons/monsters/bosses/frames/orb.h"
#include "qa/content/q1/addons/monsters/bosses/registry.h"
#include "qa/content/q1/addons/monsters/startup.h"
#include "qa/content/q1/base/projectiles.h"
#include "qa/content/q1/missionpacks/types.h"
#include "../support.h"

#define PREFIX "mg3:orb"

static const char *const orb_classnames[] = {"monster_orb"};
static const qa_q1_base_monster_species orb_spec = {
  .species = "wizard", .classnames = orb_classnames, .classname_count = 1, .model = "teleporter_eye_blink",
  .head = NULL, .health = 300, .gib_health = -INFINITY, .gibs = NULL, .gib_count = 0,
  .bounds = {{-32, -32, -24}, {32, 32, 64}}, .stand = "orb_stand1", .walk = "orb_walk1", .run = "orb_run1",
  .sight = "", .missile = "orb_fast1", .melee = false, .movement = "fly",
};

static void orb_spawn(qa_q1_base_monster *monster);
static void orb_start(qa_q1_base_monster *monster);
static void orb_use(qa_q1_base_monster *monster, const qa_actor_id *activator);
static void orb_pain(qa_q1_base_monster *monster, const qa_actor_id *attacker, double damage);
static void orb_die(qa_q1_base_monster *monster, const qa_actor_id *attacker);
static bool orb_try_attack(qa_q1_base_monster *monster);
static void orb_destroy(qa_q1_base_monster *monster) { free(monster); }
static const qa_q1_base_monster_ops orb_ops = {
  QA_Q1AM_MG3_MONSTER_OPS,
  .spawn = orb_spawn, .start = orb_start, .use = orb_use, .pain = orb_pain, .die = orb_die,
  .try_attack = orb_try_attack, .destroy = orb_destroy,
};
bool qa_q1am_is_orb(const qa_q1_base_monster *monster) { return monster->ops == &orb_ops; }
static qa_q1am_orb *orb_of(qa_q1_base_monster *monster) { return (qa_q1am_orb *)monster; }

/* TS actions map: every frame action checks instanceof Q1Orb, then monster.action(operation.name). */
static void orb_frame_action(void *ctx, qa_q1_base_monster *monster) {
  if (!qa_q1am_is_orb(monster)) qa_range_error("Orb callback received another controller");
  qa_q1am_orb_action(orb_of(monster), ctx);
}

static double world_number(qa_q1_services *game, const char *name) {
  return game->world == NULL ? 0 : qa_q1_actor_number(game->world, name);
}
static void orb_spawn(qa_q1_base_monster *monster) {
  qa_q1am_orb *orb = orb_of(monster);
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  qa_gameplay_authority_set_health(game->host.combat, entity->actor, 300);
  entity->max_health = 300;
  entity->pain = qa_q1_callback_registry_pain(game->named, entity, PREFIX ":monster_pain");
  entity->die = qa_q1_callback_registry_die(game->named, entity, PREFIX ":monster_die");
  entity->path_end = qa_q1_callback_registry_action(game->named, entity, PREFIX ":monster_stand");
  qa_q1ad_set_number(orb->context, entity, "combat_style", 1);
  qa_q1am_init_mg3_monster(monster, orb->context, "progs/teleporter_eye_blink.mdl", 2, 2);
}
static void orb_start(qa_q1_base_monster *monster) { qa_q1am_start_mg3_monster(monster, orb_of(monster)->context); }
static void orb_use(qa_q1_base_monster *monster, const qa_actor_id *activator) {
  qa_actor_id resolved;
  const bool has = qa_q1am_mg3_monster_activator(monster->game, activator, &resolved);
  qa_q1_base_monster_use_super(monster, has ? &resolved : NULL);
}
static void orb_pain(qa_q1_base_monster *monster, const qa_actor_id *attacker, double damage) {
  qa_q1_base_monster_retaliate(monster, attacker);
  qa_q1_services *game = monster->game;
  if (monster->state->pain_finished > game->time || qa_q1am_random(game) * 200 > damage) return;
  qa_q1_sound_defaults(game, monster->entity->actor.id, "orb/orb_pain.wav");
  monster->state->pain_finished = game->time + 8;
  qa_q1_base_monster_play(monster, "orb_pain1");
}
static void orb_die(qa_q1_base_monster *monster, const qa_actor_id *attacker) {
  if (monster->counted_death) return;
  qa_q1_base_monster_set_enemy(monster, attacker);
  qa_q1_actor_set_damageable(monster->entity, false);
  memset(&monster->entity->touch, 0, sizeof monster->entity->touch);
  qa_q1_base_monster_count_kill(monster);
  qa_q1_base_monster_play(monster, "orb_death1");
}
/* if (this.sliding || entity.attackState !== "straight") { sliding = false; straight; play(frame) } */
static void straighten(qa_q1_base_monster *monster, const char *frame) {
  if (monster->sliding || strcmp(monster->entity->attack_state, "straight") != 0) {
    monster->sliding = false;
    monster->entity->attack_state = "straight";
    qa_q1_base_monster_play(monster, frame);
  }
}
static bool orb_try_attack(qa_q1_base_monster *monster) {
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  const double range = world_number(game, "enemy_range");
  if (game->time < monster->state->attack_finished || world_number(game, "enemy_visible") == 0) return false;
  if (range == 3) { straighten(monster, "wiz_run1"); return false; }
  qa_vec3 target, origin;
  const bool has_target = monster->state->has_enemy && qa_q1_base_monster_eye(monster, monster->state->enemy, &target);
  const bool has_origin = qa_q1_base_monster_eye(monster, entity->actor.id, &origin);
  if (!has_target || !has_origin) return false;
  const qa_q1_trace trace = qa_q1am_trace(game, origin, target, QA_Q1_POINT, &entity->actor.id, true);
  if (!trace.has_actor || !monster->state->has_enemy || !qa_same_actor(trace.actor, monster->state->enemy)) {
    straighten(monster, "orb_run1");
    return false;
  }
  if (qa_q1am_random(game) < (range == 0 ? 0.9 : range == 1 ? 0.6 : range == 2 ? 0.2 : 0)) {
    monster->sliding = false;
    entity->attack_state = "missile";
    return true;
  }
  if (range == 2) {
    straighten(monster, "orb_run1");
  } else if (!monster->sliding) {
    entity->attack_state = "straight";
    monster->sliding = true;
    qa_q1_base_monster_play(monster, "orb_side1");
  }
  return false;
}
static void idle_sound(qa_q1am_orb *orb) {
  qa_q1_base_monster *monster = &orb->base.base;
  qa_q1_services *game = monster->game;
  if (qa_q1_actor_number(monster->entity, "waitmin") < game->time) {
    qa_q1ad_set_number(orb->context, monster->entity, "waitmin", game->time + 15 + qa_q1am_random(game) * 10);
    qa_q1_sound(game, monster->entity->actor.id, "boss2/sight.wav", "voice", 2, 1);
  }
}
static void set_next(qa_q1_base_monster *monster, const char *frame) {
  qa_q1am_copy_name(monster->next_frame, sizeof monster->next_frame, frame);
}
void qa_q1am_orb_action(qa_q1am_orb *orb, const char *name) {
  qa_q1_base_monster *monster = &orb->base.base;
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  qa_q1ad_context *context = orb->context;
  if (strcmp(name, "orb:orb_stand1") == 0) {
    qa_q1_base_monster_ai(monster, QA_Q1_AI_STAND, 0);
  } else if (strcmp(name, "orb:orb_walk1") == 0) {
    qa_q1_base_monster_ai(monster, QA_Q1_AI_WALK, 8);
    idle_sound(orb);
  } else if (strcmp(name, "orb:orb_side1") == 0) {
    qa_q1_base_monster_face(monster);
    qa_q1_base_monster_ai(monster, QA_Q1_AI_RUN, 16);
    idle_sound(orb);
  } else if (strcmp(name, "orb:orb_run1") == 0) {
    qa_q1_base_monster_ai(monster, QA_Q1_AI_RUN, 16);
    idle_sound(orb);
  } else if (strcmp(name, "orb:orb_fast1") == 0) {
    qa_q1ad_set_number(context, entity, "ammo_nails", 1 + floor(qa_q1am_random(game) * 3 + 0.5));
    qa_q1_base_monster_face(monster);
  } else if (strcmp(name, "orb:orb_fast3") == 0) {
    qa_q1_base_monster_delay(monster, 0.3);
    qa_q1_base_monster_face(monster);
  } else if (strcmp(name, "orb:orb_fast4") == 0) {
    qa_q1_base_monster_face(monster);
    qa_q1am_orb_blast(orb);
    qa_q1_base_monster_delay(monster, 0.2);
    qa_q1ad_set_number(context, entity, "ammo_nails", qa_q1_actor_number(entity, "ammo_nails") - 1);
    if (qa_q1_actor_number(entity, "ammo_nails") != 0) set_next(monster, "orb_fast2");
  } else if (strcmp(name, "orb:orb_fast5") == 0) {
    qa_q1_base_monster_face(monster);
    qa_q1_base_monster_attack_finished(monster, 2);
    entity->attack_state = "straight";
    monster->sliding = world_number(game, "enemy_range") < 2 && world_number(game, "enemy_visible") != 0;
    set_next(monster, monster->sliding ? "orb_side1" : "orb_run1");
  } else if (strcmp(name, "orb:orb_pain1") == 0) {
    qa_q1ad_set_number(context, entity, "ammo_shells", 3);
    qa_q1_base_monster_delay(monster, 0.2);
  } else if (strcmp(name, "orb:orb_pain2") == 0) {
    qa_q1am_pain_lightning(context, entity, QA_Q1_ZERO);
    qa_q1_base_monster_delay(monster, 0.3);
    qa_q1ad_set_number(context, entity, "ammo_shells", qa_q1_actor_number(entity, "ammo_shells") - 1);
    if (qa_q1_actor_number(entity, "ammo_shells") != 0) set_next(monster, "orb_pain2");
  } else if (strcmp(name, "orb:orb_death1") == 0) {
    qa_vec3 velocity;
    velocity.x = -200 + 400 * qa_q1am_random(game);
    velocity.y = -200 + 400 * qa_q1am_random(game);
    velocity.z = 150 + 150 * qa_q1am_random(game);
    qa_q1_body_patch patch;
    memset(&patch, 0, sizeof patch);
    patch.has_velocity = true;
    patch.velocity = velocity;
    patch.has_ground = true;
    patch.ground = NULL;
    qa_q1_set_body(game, entity, &patch);
    entity->movement_flags = QA_Q1AM_FLAGS(entity->movement_flags) & ~512;
    entity->solid = "bbox";
    qa_q1_set_bounds(game, entity, QA_Q1_POINT);
    qa_q1_sound_defaults(game, entity->actor.id, "orb/orb_death.wav");
  } else if (strcmp(name, "orb:orb_death3") == 0) {
    entity->touch = qa_q1_callback_registry_touch(game->named, entity, PREFIX ":death_touch");
  } else if (strcmp(name, "orb:orb_death4") == 0) {
    qa_q1_base_monster_delay(monster, 9999);
  } else {
    qa_range_error("Unknown orb action %s", name);
  }
}
static double spread(qa_q1_services *game) { return qa_q1am_random(game) * 2 - 1; } /* TS random() */
void qa_q1am_orb_blast(qa_q1am_orb *orb) {
  qa_q1_base_monster *monster = &orb->base.base;
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  const double speed = qa_q1_option_skill(game) > 2 ? 500 : qa_q1_option_skill(game) > 0 ? 450 : 400;
  double count = 4 + floor(qa_q1am_random(game) * 2 + 0.5);
  qa_vec3 target = QA_Q1_ZERO;
  (void)qa_q1_base_monster_target(monster, &target);
  const qa_q1_basis basis = qa_q1_make_vectors(game, qa_q1mp_velocity_angles(qa_sub3(target, qa_q1_base_monster_origin(monster))));
  const qa_vec3 origin = qa_add3(qa_q1_base_monster_origin(monster), qa_scale3(basis.forward, 15));
  const double time = qa_length3(qa_sub3(target, origin)) / speed;
  qa_vec3 target_velocity = QA_Q1_ZERO;
  qa_q1_body enemy_body;
  if (monster->state->has_enemy && qa_q1am_read_body(game, monster->state->enemy, &enemy_body))
    target_velocity = enemy_body.velocity;
  target_velocity.z = 0;
  const qa_vec3 projected = qa_add3(target, qa_scale3(target_velocity, time / 4));
  const qa_vec3 direction = qa_q1_normalize(qa_sub3(projected, origin));
  qa_q1_effect(game, "explosion", origin, NULL, 1);
  while (count > 0) {
    const double right = spread(game) * 0.1;
    const double up = spread(game) * 0.1;
    const qa_vec3 aim = qa_q1_normalize(qa_add3(qa_add3(direction, qa_scale3(game->basis.right, right)),
                                                qa_scale3(game->basis.up, up)));
    qa_q1_actor *missile = qa_q1_launch_spike(game, &entity->actor.id, qa_add3(origin, qa_scale3(aim, 8)),
                                              qa_scale3(aim, 1000), "spike");
    qa_q1ad_set_text(&missile->classname, "rock");
    qa_q1ad_set_text(&missile->model, "progs/rogue/sphere.mdl");
    qa_q1_body_patch patch;
    memset(&patch, 0, sizeof patch);
    patch.has_velocity = true;
    patch.velocity = qa_scale3(aim, speed + spread(game) * 100);
    patch.has_bounds = true;
    patch.bounds = QA_Q1_POINT;
    qa_q1_set_body(game, missile, &patch);
    qa_vec3 angular;
    angular.x = 300 * spread(game);
    angular.y = 300 * spread(game);
    angular.z = 300 * spread(game);
    missile->angular_velocity = angular;
    if (fmod(count, 2) == 0) {
      qa_q1ad_set_number(orb->context, missile, "frags", 1);
      missile->effects = QA_Q1AM_FLAGS(missile->effects) | 64;
    }
    missile->touch = qa_q1_callback_registry_touch(game->named, missile, PREFIX ":missile_touch");
    count--;
  }
}
void qa_q1am_orb_death_touch(qa_q1am_orb *orb, qa_actor_id other) {
  qa_q1_base_monster *monster = &orb->base.base;
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  const qa_q1_actor *target = qa_q1_get_entity(game, &other);
  if ((target != NULL && strcmp(target->solid, "trigger") == 0) ||
      (target != NULL && strcmp(target->solid, "bbox") == 0 && qa_q1_health(game, other) == 0))
    return;
  qa_q1ad_event event;
  memset(&event, 0, sizeof event);
  event.kind = QA_Q1AD_EVENT_COLORED_EXPLOSION;
  event.origin = qa_q1_base_monster_origin(monster);
  event.color_start = 244;
  event.color_length = 3;
  orb->context->services.emit(orb->context->services.ctx, &event);
  const qa_actor_id self = entity->actor.id;
  qa_q1_radius_damage(game, self, &self, 100, game->world == NULL ? NULL : &game->world->actor.id, NULL, NULL);
  qa_q1_remove(game, entity);
}
bool qa_q1am_orb_init(qa_q1am_orb *orb, qa_q1ad_context *context, qa_q1_actor *entity) {
  memset(orb, 0, sizeof *orb);
  orb->context = context;
  qa_q1_base_monster_source source;
  memset(&source, 0, sizeof source);
  source.callback_prefix = PREFIX;
  source.frames = &qa_q1am_orb_frames;
  source.actions = qa_q1am_frame_actions(&qa_q1am_orb_frames, orb_frame_action, &source.action_count);
  return qa_q1am_mg3_monster_init(&orb->base, context->game, entity, &orb_spec,
                                  qa_q1_base_as_monster_services(context->base), &source, &orb_ops, NULL);
}
static qa_q1_base_monster *create_orb(void *ctx, qa_q1_actor *entity) {
  qa_q1am_orb *orb = malloc(sizeof *orb);
  if (orb == NULL) return NULL;
  if (!qa_q1am_orb_init(orb, ctx, entity)) { free(orb); return NULL; }
  return &orb->base.base;
}
static void death_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other,
                        const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)game; (void)normal; (void)surface; /* TS (_game, entity, other) */
  qa_q1am_orb_death_touch(orb_of(qa_q1am_require_boss(ctx, entity)), other);
}
static void missile_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other,
                          const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)ctx; (void)normal; (void)surface; /* TS (_game, entity, other) over the captured game */
  if (entity->has_owner && qa_same_actor(entity->owner, other)) return;
  const char *classname = game->host.classname(game->host.ctx, other);
  if (strcmp(classname, "monster_orb") == 0 || strcmp(classname, "monster_lava_man") == 0 ||
      strcmp(classname, "monster_super_shambler") == 0) {
    qa_q1_remove(game, entity);
    return;
  }
  const qa_q1_actor *target = qa_q1_get_entity(game, &other);
  if (strcmp(classname, entity->classname) == 0 || (target != NULL && strcmp(target->solid, "trigger") == 0)) return;
  const qa_vec3 origin = qa_q1_get_body(game, entity).origin;
  if (strcmp(game->host.contents(game->host.ctx, origin), "sky") == 0) {
    qa_q1_remove(game, entity);
    return;
  }
  if (qa_q1am_can_take_damage(game, other)) {
    qa_q1_effect(game, "blood", origin, &other, 18);
    (void)qa_q1_damage(game, other, entity->actor.id, qa_q1am_opt(entity->has_owner, &entity->owner), 18,
                       NULL, NULL, NULL, NULL);
  } else if (qa_q1_actor_number(entity, "frags") != 0) {
    qa_q1_effect(game, "knight-spike", origin, NULL, 1);
  }
  qa_q1_remove(game, entity);
}
void qa_q1am_register_orb(qa_q1ad_context *context) {
  qa_q1_services *game = context->game;
  qa_q1am_boss_registry *monsters = qa_q1am_register_boss_controllers(context, PREFIX, "monster_orb", create_orb, context);
  qa_q1_callback_handlers handlers;
  memset(&handlers, 0, sizeof handlers);
  handlers.ctx = monsters;
  handlers.touch = death_touch;
  qa_q1_callback_registry_register(game->named, PREFIX ":death_touch", handlers);
  handlers.ctx = context;
  handlers.touch = missile_touch;
  qa_q1_callback_registry_register(game->named, PREFIX ":missile_touch", handlers);
}
