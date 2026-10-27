/* Port of src/content/q1/addons/monsters/bosses/final.ts. GPL-2.0-or-later. */
#include "qa/content/q1/addons/monsters/bosses/final.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/addons/monsters/bosses/effects.h"
#include "qa/content/q1/addons/monsters/bosses/frames/final.h"
#include "qa/content/q1/addons/monsters/bosses/oldnew.h"
#include "qa/content/q1/addons/monsters/bosses/registry.h"
#include "qa/content/q1/addons/monsters/bosses/sphere_points.h"
#include "qa/foundation/support.h"

#define FR(x) ((double)(float)(x))
#define FLAGS(x) ((int32_t)(x))

static const char *const final_classnames[] = {"monster_boss_final"};
static const qa_q1am_species final_spec = {
  "boss", NULL, "boss", NULL, "boss_final_idle1", "boss_final_idle1", "boss_final_missile1",
  "", "boss_final_missile1", "boss", final_classnames, 1, NULL, 0,
  12000, -INFINITY, {{-128, -128, -24}, {128, 128, 256}}, false,
};
static qa_q1am_final_boss *as_final(qa_q1am_monster *m) { return m->owner; }
static void final_dispatch(qa_q1am_monster *monster, const char *name) {
  qa_q1am_final_boss_action(monster->owner, name);
}
static void decide_action(void *ctx, qa_q1am_monster *monster) {
  (void)ctx;
  if (monster->kind != QA_Q1AM_KIND_FINAL)
    qa_range_error("Final boss decision received another source controller");
  monster->ops.play(monster, "boss_final_decide");
}
static const qa_q1am_source *final_source(void) {
  static const qa_q1am_source *source;
  if (source == NULL) {
    static const qa_q1am_action_entry extra[] = {{"boss_final_decide", NULL, decide_action}};
    source = qa_q1am_boss_build_source("mg3:final", &qa_q1am_final_boss_frames, QA_Q1AM_KIND_FINAL,
                                       "Final boss callback received another source controller",
                                       final_dispatch, extra, 1);
  }
  return source;
}
static void later(qa_q1am_context *context, qa_q1am_actor *entity, const char *name, double delay) {
  char full[200];
  (void)snprintf(full, sizeof full, "mg3:final:%s", name);
  context->game->schedule(context->game->ctx, entity, delay,
                          context->game->named_action(context->game->ctx, entity, full));
}
static double random_signed(qa_q1am_context *context) {
  return FR(2 * context->game->random(context->game->ctx) - 1);
}
static void spin(qa_q1am_context *context, qa_q1am_actor *shot) {
  shot->angular_velocity = (qa_vec3){300 * random_signed(context), 300 * random_signed(context),
                                     300 * random_signed(context)};
}
static qa_q1am_actor *rock(qa_q1am_context *context, bool has_owner, qa_actor_id owner,
                           qa_vec3 origin, qa_vec3 direction, const char *model) {
  qa_q1am_game *game = context->game;
  qa_q1am_actor *shot = game->launch_spike(game->ctx, game, has_owner, owner, origin, direction);
  (void)snprintf(shot->classname, sizeof shot->classname, "%s", "rock");
  (void)snprintf(shot->model, sizeof shot->model, "progs/rogue/%s.mdl", model);
  shot->touch = game->named_touch(game->ctx, shot, "mg3:final:T_RockTouch2");
  game->set_bounds(game->ctx, shot, QA_Q1AM_POINT);
  return shot;
}
static void final_spawn(qa_q1am_monster *m) {
  qa_q1am_final_boss *self = as_final(m);
  qa_q1am_game *game = m->game;
  qa_q1am_actor *entity = m->entity;
  qa_q1am_context *context = self->context;
  if (game->options.deathmatch != 0) {
    game->remove(game->ctx, entity);
    return;
  }
  (void)snprintf(entity->classname, sizeof entity->classname, "%s", "monster_boss");
  context->set_number(context->ctx, entity, "boss_immune", 1);
  entity->movement_flags = 32;
  game->combat_set_team(game->ctx, entity->actor, "q1:monsters");
  game->total_monsters++;
  qa_q1am_event event = {0};
  event.kind = "monster-count";
  event.count = game->total_monsters;
  context->services_emit(context->services_ctx, &event);
  context->set_number(context->ctx, entity, "frags", 0);
  entity->use = game->named_use(game->ctx, entity, "mg3:final:boss_awake");
}
static void final_awake(qa_q1am_monster *m, bool has, qa_actor_id activator) {
  qa_q1am_game *game = m->game;
  qa_q1am_actor *entity = m->entity;
  entity->solid = "slidebox";
  entity->movement = "step";
  qa_q1am_actor_set_damageable(game, entity, true);
  entity->aimed_damage = true;
  (void)snprintf(entity->model, sizeof entity->model, "%s", "progs/boss.mdl");
  m->state->attack_finished = game->time + 3;
  game->set_bounds(game->ctx, entity, final_spec.bounds);
  m->state->has_enemy = has;
  m->state->enemy = activator;
  entity->die = game->named_die(game->ctx, entity, "mg3:final:monster_die");
  entity->pain = game->named_pain(game->ctx, entity, "mg3:final:monster_pain");
  game->combat_set_health(game->ctx, entity->actor, 12000);
  entity->max_health = 12000;
  entity->use = game->named_use(game->ctx, entity, "mg3:final:monster_use");
  entity->yaw_speed = 20;
  game->effect(game->ctx, "lava-splash", qa_q1am_monster_origin(m), false, (qa_actor_id){0}, 1);
  m->ops.play(m, "boss_final_rise1");
}
static void final_play(qa_q1am_monster *m, const char *name) {
  qa_q1am_final_boss *self = as_final(m);
  if (strcmp(name, "boss_final_decide") == 0) {
    self->super_ops.play(m, m->entity->wait > 0 && m->game->random(m->game->ctx) < 0.3
                                ? "boss_final_mg1" : "boss_final_missile1");
    return;
  }
  self->super_ops.play(m, name);
}
static int compare_slots(const void *a, const void *b) {
  int64_t left = ((const qa_actor_id *)a)->slot, right = ((const qa_actor_id *)b)->slot;
  return left < right ? -1 : left > right ? 1 : 0;
}
void qa_q1am_final_boss_face(qa_q1am_final_boss *boss) {
  qa_q1am_monster *m = &boss->base;
  qa_q1am_game *game = m->game;
  if ((m->state->has_enemy ? game->health(game->ctx, m->state->enemy) : 0) <= 0 ||
      game->random(game->ctx) < 0.02) {
    size_t count = 0;
    qa_actor_id *players = game->players(game->ctx, &count);
    qsort(players, count, sizeof *players, compare_slots);
    int64_t after = m->state->has_enemy ? m->state->enemy.slot : 0;
    bool found = false;
    qa_actor_id chosen = {0};
    for (size_t i = 0; i < count && !found; i++)
      if (players[i].slot > after) {
        chosen = players[i];
        found = true;
      }
    if (!found && count > 0) {
      chosen = players[0];
      found = true;
    }
    m->state->has_enemy = found;
    m->state->enemy = chosen;
    free(players);
  }
  if ((m->state->has_enemy ? game->health(game->ctx, m->state->enemy) : 0) > 0)
    qa_q1am_monster_face(m);
}
static void final_pain(qa_q1am_monster *m, bool has, qa_actor_id attacker, double damage) {
  (void)has; (void)attacker; (void)damage;
  qa_q1am_final_boss *self = as_final(m);
  qa_q1am_game *game = m->game;
  qa_q1am_actor *entity = m->entity;
  qa_q1am_context *context = self->context;
  if (m->state->attack_finished > game->time || m->state->pain_finished > game->time ||
      qa_q1am_actor_number(entity, "boss_immune") != 0)
    return;
  const char *event = "";
  double frags = qa_q1am_actor_number(entity, "frags");
  if (frags == 0) {
    event = "boss_final_shocka1";
    frags = 1;
  }
  static const struct { double step, fraction; const char *frame; } steps[] = {
    {1, 0.83, "boss_final_shocka1"}, {2, 0.66, "boss_final_shocka1"}, {3, 0.5, "boss_final_shockb1"},
    {4, 0.33, "boss_final_shocka1"}, {5, 0.16, "boss_final_shockb1"},
  };
  for (size_t i = 0; i < sizeof steps / sizeof steps[0]; i++)
    if (frags == steps[i].step &&
        game->health(game->ctx, entity->actor.id) < FR(entity->max_health * FR(steps[i].fraction))) {
      event = steps[i].frame;
      frags++;
    }
  context->set_number(context->ctx, entity, "frags", frags);
  if (event[0] == '\0') return;
  context->set_number(context->ctx, entity, "boss_immune", 1);
  game->sound(game->ctx, entity, "boss1/pain.wav", "weapon", 1);
  m->state->pain_finished = game->time + 3;
  m->ops.play(m, event);
}
static void final_die(qa_q1am_monster *m, bool has, qa_actor_id attacker) {
  m->state->has_enemy = has;
  m->state->enemy = attacker;
  qa_q1am_actor_set_damageable(m->game, m->entity, false);
  m->entity->touch.present = false;
  m->entity->movement_flags = (double)(FLAGS(m->entity->movement_flags) & ~3);
  m->game->use_targets(m->game->ctx, m->entity, m->state->has_enemy, m->state->enemy);
  m->ops.play(m, "boss_final_death1");
}
static qa_vec3 muzzle(qa_q1am_final_boss *boss, qa_vec3 offset, qa_vec3 angles) {
  qa_q1am_monster *m = &boss->base;
  qa_q1am_basis basis = qa_q1am_game_make_vectors(m->game, angles);
  return qa_add3(qa_add3(qa_add3(qa_q1am_monster_origin(m), qa_scale3(basis.forward, offset.x)),
                         qa_scale3(basis.right, offset.y)),
                 (qa_vec3){0, 0, offset.z});
}
static void boss_radius_damage(qa_q1am_context *context, qa_q1am_actor *source, double damage);
static void final_line(qa_q1am_final_boss *boss, qa_vec3 offset);
static void final_missile(qa_q1am_final_boss *boss, qa_vec3 offset) {
  qa_q1am_monster *m = &boss->base;
  qa_q1am_game *game = m->game;
  qa_q1am_actor *entity = m->entity;
  if ((FLAGS(entity->spawnflags) & 2) != 0) {
    game->random(game->ctx);
    final_line(boss, offset);
    return;
  }
  qa_vec3 target;
  qa_vec3 target_point = qa_q1am_monster_target(m, &target) ? target : QA_Q1AM_ZERO;
  qa_vec3 origin = muzzle(boss, offset, qa_q1am_velocity_angles(qa_sub3(target_point, qa_q1am_monster_origin(m))));
  qa_vec3 direction = qa_q1am_normalize(qa_sub3(target_point, origin));
  qa_q1am_actor *shot = game->launch_spike(game->ctx, game, true, entity->actor.id, origin, direction);
  (void)snprintf(shot->model, sizeof shot->model, "%s", "progs/lavaball.mdl");
  shot->projectile = "rocket";
  shot->touch = game->named_touch(game->ctx, shot, "projectile_touch");
  shot->angular_velocity = (qa_vec3){200, 100, 300};
  qa_q1am_body_patch patch = {0};
  patch.has_bounds = true;
  patch.bounds = QA_Q1AM_POINT;
  patch.has_velocity = true;
  patch.velocity = qa_scale3(direction, 300);
  game->set_body(game->ctx, shot, &patch);
  game->sound(game->ctx, entity, "boss1/throw.wav", "weapon", 1);
  if ((m->state->has_enemy ? game->health(game->ctx, m->state->enemy) : 0) <= 0)
    m->ops.play(m, "boss_final_idle1");
}
void qa_q1am_final_boss_melee_check(qa_q1am_final_boss *boss) {
  qa_q1am_monster *m = &boss->base;
  qa_q1am_game *game = m->game;
  qa_q1am_actor *entity = m->entity;
  qa_q1am_context *context = boss->context;
  if (qa_q1am_monster_distance(m) > 300 || !m->state->has_enemy ||
      !game->can_damage(game->ctx, m->state->enemy, entity->actor.id))
    return;
  game->damage(game->ctx, m->state->enemy, entity->actor.id, true, entity->actor.id,
               (game->random(game->ctx) + game->random(game->ctx) + game->random(game->ctx)) * 40);
  game->sound(game->ctx, entity, "shambler/smack.wav", "voice", 1);
  for (int i = 0; i < 2; i++)
    game->spawn_meat_spray(game->ctx, game, entity,
                           qa_add3(qa_q1am_monster_origin(m), qa_scale3(game->basis.forward, 100)),
                           qa_scale3(game->basis.right, random_signed(context) * 100));
}
static void missile_spawner(qa_q1am_final_boss *boss, qa_vec3 offset) {
  (void)offset;
  qa_q1am_monster *m = &boss->base;
  qa_q1am_game *game = m->game;
  qa_q1am_actor *entity = m->entity;
  qa_vec3 target;
  qa_vec3 target_point = qa_q1am_monster_target(m, &target) ? target : QA_Q1AM_ZERO;
  qa_vec3 origin = muzzle(boss, offset, qa_q1am_velocity_angles(qa_sub3(target_point, qa_q1am_monster_origin(m))));
  qa_q1am_actor *shot = game->launch_spike(game->ctx, game, true, entity->actor.id, origin, QA_Q1AM_ZERO);
  (void)snprintf(shot->model, sizeof shot->model, "%s", "progs/lavaball.mdl");
  shot->projectile = "rocket";
  shot->touch = game->named_touch(game->ctx, shot, "projectile_touch");
  shot->angular_velocity = (qa_vec3){200, 100, 300};
  qa_q1am_body_patch patch = {0};
  patch.has_bounds = true;
  patch.bounds = QA_Q1AM_POINT;
  patch.has_velocity = true;
  patch.velocity = QA_Q1AM_ZERO;
  game->set_body(game->ctx, shot, &patch);
  game->sound(game->ctx, entity, "boss1/throw.wav", "weapon", 1);
  if (!m->state->has_enemy || game->health(game->ctx, m->state->enemy) <= 0)
    m->ops.play(m, "boss_final_idle1");
}
static void blast(qa_q1am_final_boss *boss, qa_vec3 offset, double spread, bool effect) {
  qa_q1am_monster *m = &boss->base;
  qa_q1am_game *game = m->game;
  qa_q1am_actor *entity = m->entity;
  qa_q1am_context *context = boss->context;
  double speed = game->options.skill > 2 ? 500 : game->options.skill > 0 ? 450 : 400;
  double width = FR(0.15 + FR(0.2 * fabs(spread)));
  double count = floor(game->random(game->ctx) * 6 + 0.5);
  qa_vec3 target;
  qa_vec3 target_point = qa_q1am_monster_target(m, &target) ? target : QA_Q1AM_ZERO;
  qa_vec3 angles = qa_q1am_velocity_angles(qa_sub3(target_point, qa_q1am_monster_origin(m)));
  qa_vec3 origin = muzzle(boss, offset, angles);
  qa_q1am_basis basis = qa_q1am_game_make_vectors(game, angles);
  double time = FR(qa_length3(qa_sub3(target_point, origin)) / speed);
  qa_q1am_body enemy_body;
  qa_vec3 velocity = m->state->has_enemy && game->body_read(game->ctx, m->state->enemy, &enemy_body)
                         ? enemy_body.velocity : QA_Q1AM_ZERO;
  velocity.z = 0;
  qa_vec3 direction = qa_q1am_normalize(
      qa_sub3(qa_add3(target_point, qa_scale3(velocity, time / 4)), origin));
  if (effect) game->effect(game->ctx, "explosion", origin, false, (qa_actor_id){0}, 1);
  for (double remaining = count; remaining > 0; remaining--) {
    qa_vec3 aim = qa_q1am_normalize(qa_add3(
        qa_add3(direction, qa_scale3(basis.right, random_signed(context) * width)),
        qa_scale3(basis.up, random_signed(context) * width)));
    qa_q1am_actor *shot = rock(context, true, entity->actor.id, qa_add3(origin, qa_scale3(aim, 8)), aim, "sphere");
    qa_q1am_body_patch patch = {0};
    patch.has_velocity = true;
    patch.velocity = qa_scale3(aim, speed + random_signed(context) * 100);
    game->set_body(game->ctx, shot, &patch);
    spin(context, shot);
    if ((int)remaining % 2 == 0) shot->effects = (double)((int32_t)shot->effects | 64);
  }
}
static void final_line(qa_q1am_final_boss *boss, qa_vec3 offset) {
  qa_q1am_monster *m = &boss->base;
  qa_q1am_game *game = m->game;
  qa_q1am_actor *entity = m->entity;
  qa_q1am_context *context = boss->context;
  double count = game->options.skill == 0 ? 4 : game->options.skill == 1 ? 8
                 : game->options.skill == 3 ? 15 : 11;
  qa_vec3 angles = game->body(game->ctx, entity).angles;
  qa_vec3 origin = muzzle(boss, offset, angles);
  qa_q1am_basis basis = qa_q1am_game_make_vectors(game, angles);
  qa_vec3 target;
  qa_vec3 delta = qa_sub3(qa_q1am_monster_target(m, &target) ? target : QA_Q1AM_ZERO, origin);
  delta.z = 0;
  qa_vec3 direction = qa_q1am_normalize(delta);
  game->effect(game->ctx, "explosion", origin, false, (qa_actor_id){0}, 1);
  double start = 4 / count * (offset.y < 0 ? 1 : -1);
  double increment = 2 / count * (offset.y < 0 ? -1 : 1);
  for (int i = 0; i < (int)count; i++) {
    boss_radius_damage(context, entity, 10 * game->random(game->ctx) + 10);
    qa_vec3 aim = qa_q1am_normalize(qa_add3(qa_add3(direction, qa_scale3(basis.right, start)),
                                            qa_scale3(qa_scale3(basis.right, increment), i)));
    aim.z = 0;
    aim = qa_q1am_normalize(aim);
    qa_q1am_actor *shot = rock(context, true, entity->actor.id, qa_add3(origin, qa_scale3(aim, 8)), aim, "sphere");
    qa_vec3 velocity = qa_scale3(aim, 300 + random_signed(context) * 100);
    if (game->random(game->ctx) > 0.5) velocity.z = (game->random(game->ctx) + 1) * 8;
    qa_q1am_body_patch patch = {0};
    patch.has_velocity = true;
    patch.velocity = velocity;
    game->set_body(game->ctx, shot, &patch);
    spin(context, shot);
    if (i % 2 == 0) {
      context->set_number(context->ctx, shot, "frags", 1);
      shot->effects = (double)((int32_t)shot->effects | 64);
    }
  }
}
void qa_q1am_final_boss_upgrade(qa_q1am_final_boss *boss) {
  qa_q1am_monster *m = &boss->base;
  qa_q1am_game *game = m->game;
  qa_q1am_actor *entity = m->entity;
  qa_q1am_context *context = boss->context;
  entity->wait++;
  context->set_number(context->ctx, entity, "boss_immune", 0);
  const char *target = entity->wait == 2 ? qa_q1am_actor_text(entity, "wave1")
                       : entity->wait == 3 ? qa_q1am_actor_text(entity, "tele_target")
                       : entity->wait == 4 ? qa_q1am_actor_text(entity, "wave2")
                       : entity->wait == 5 ? qa_q1am_actor_text(entity, "wave3") : NULL;
  if (entity->wait == 3) {
    entity->frame = 0;
    game->schedule(game->ctx, entity, 0, game->named_action(game->ctx, entity, "SUB_Null"));
    context->set_number(context->ctx, entity, "boss_immune", 1);
  }
  if (target != NULL) {
    char original[128], wanted[128];
    (void)snprintf(original, sizeof original, "%s", entity->target);
    (void)snprintf(wanted, sizeof wanted, "%s", target);
    (void)snprintf(entity->target, sizeof entity->target, "%s", wanted);
    game->use_targets(game->ctx, entity, entity->has_activator, entity->activator);
    (void)snprintf(entity->target, sizeof entity->target, "%s", original);
  }
  if (entity->wait == 3) qa_q1am_boss_teleport(context, false);
  if (entity->wait == 5) {
    qa_q1am_actor *spiral = game->create(game->ctx, "spiral");
    spiral->classname[0] = '\0';
    spiral->has_owner = true;
    spiral->owner = entity->actor.id;
    qa_vec3 target_point;
    qa_vec3 delta = qa_sub3(qa_q1am_monster_target(m, &target_point) ? target_point : QA_Q1AM_ZERO,
                            qa_q1am_monster_origin(m));
    delta.z = 0;
    qa_q1am_body_patch patch = {0};
    patch.has_origin = true;
    patch.origin = qa_add3(qa_q1am_monster_origin(m), (qa_vec3){0, 0, 60});
    patch.has_angles = true;
    patch.angles = qa_q1am_velocity_angles(qa_q1am_normalize(delta));
    game->set_body(game->ctx, spiral, &patch);
    spiral->delay = game->options.skill <= 2 ? 0.2 : 0.15;
    spiral->count = 50;
    later(context, spiral, "spiral_single", spiral->delay);
  }
}
void qa_q1am_final_boss_action(qa_q1am_final_boss *boss, const char *name) {
  qa_q1am_monster *m = &boss->base;
  qa_q1am_game *game = m->game;
  qa_q1am_actor *entity = m->entity;
  qa_q1am_context *context = boss->context;
  size_t length = strlen(name);
  if (strncmp(name, "boss_final_idle", 15) == 0) {
    qa_q1am_final_boss_face(boss);
    if (strcmp(name, "boss_final_idle31") == 0)
      m->ops.play(m, m->state->has_enemy ? "boss_final_missile1" : "boss_final_idle1");
    return;
  }
  if (strncmp(name, "boss_final_shock", 16) == 0) {
    if (length >= 2 && strcmp(name + length - 2, "10") == 0) qa_q1am_final_boss_upgrade(boss);
    else qa_q1am_pain_lightning(context, entity, (qa_vec3){0, 0, 100});
    return;
  }
  if (strcmp(name, "boss_final_rise1") == 0) {
    game->sound(game->ctx, entity, "boss1/out1.wav", "weapon", 1);
  } else if (strcmp(name, "boss_final_rise2") == 0) {
    game->sound(game->ctx, entity, "boss1/sight1.wav", "voice", 1);
  } else if (strcmp(name, "boss_final_mg7") == 0) {
    double shells = FR(FR((game->options.skill + 3) / 6) * 30) + floor(random_signed(context) * 10 + 0.5);
    context->set_number(context->ctx, entity, "ammo_shells", shells);
    context->set_number(context->ctx, entity, "ammo_nails", shells);
    if (game->options.skill > 0) m->state->attack_finished = game->time + 5;
    qa_q1am_final_boss_face(boss);
  } else if (strcmp(name, "boss_final_mg8") == 0) {
    double nails = qa_q1am_actor_number(entity, "ammo_nails");
    bool wide = fmod(nails, 2) == 0;
    double spread = FR(1 - FR(nails / qa_q1am_actor_number(entity, "ammo_shells")));
    spread = FR(spread * spread);
    blast(boss, (qa_vec3){270, 60, 210}, wide ? spread : spread * 0.5, wide);
    if (nails != 0) qa_q1am_monster_set_frame(m->next_frame, sizeof m->next_frame, "boss_final_mg8");
    context->set_number(context->ctx, entity, "ammo_nails", nails - 1);
    qa_q1am_final_boss_face(boss);
  } else if (strcmp(name, "boss_final_missile1") == 0) {
    context->set_number(context->ctx, entity, "boss_immune", 0);
    qa_q1am_final_boss_face(boss);
  } else if (strcmp(name, "boss_final_missile10") == 0) {
    final_missile(boss, (qa_vec3){200, 100, 60});
  } else if (strcmp(name, "boss_final_missile21") == 0) {
    final_missile(boss, (qa_vec3){200, -100, 60});
  } else if (strcmp(name, "boss_final_death1") == 0) {
    game->sound(game->ctx, entity, "boss1/death.wav", "voice", 1);
    const double delays[] = {1, 3, 5};
    for (int i = 0; i < 3; i++) {
      qa_q1am_actor *timer = game->create(game->ctx, "circle_thinker");
      timer->classname[0] = '\0';
      game->set_origin(game->ctx, timer, qa_add3(qa_q1am_monster_origin(m), (qa_vec3){0, 0, 60}));
      later(context, timer, "circle_think", delays[i]);
    }
  } else if (strcmp(name, "boss_final_death9") == 0) {
    game->sound(game->ctx, entity, "boss1/out1.wav", "body", 1);
    game->effect(game->ctx, "lava-splash", qa_q1am_monster_origin(m), false, (qa_actor_id){0}, 1);
  } else if (strcmp(name, "boss_final_death10") == 0) {
    game->killed_monsters++;
    qa_q1am_event event = {0};
    event.kind = "monster-killed";
    event.has_actor = true;
    event.actor = entity->actor.id;
    event.total = game->total_monsters;
    event.found = game->killed_monsters;
    game->emit(game->ctx, &event);
    context->set_number(context->ctx, entity, "frags", qa_q1am_actor_number(entity, "frags") + 1);
    while (qa_q1am_actor_number(entity, "frags") < 5) {
      game->use_targets(game->ctx, entity, entity->has_activator, entity->activator);
      context->set_number(context->ctx, entity, "frags", qa_q1am_actor_number(entity, "frags") + 1);
    }
    game->sound(game->ctx, entity, "boss2/pop2.wav", "voice", 1);
    for (int i = 0; i < 100; i++) {
      qa_vec3 point = qa_q1am_sphere_point(i);
      if (point.z > 0)
        qa_q1am_throw_gib_vector(context,
                                 qa_add3(qa_add3(qa_q1am_monster_origin(m), qa_scale3(point, 64)), (qa_vec3){0, 0, 48}),
                                 point);
    }
    qa_q1am_actor *timer = game->create(game->ctx, "boss_end_timer");
    timer->classname[0] = '\0';
    game->schedule(game->ctx, timer, 8, game->named_action(game->ctx, timer, "mg3:bosses:boss_end"));
    game->remove(game->ctx, entity);
  } else if (strncmp(name, "boss_final_missile", 18) == 0 || strncmp(name, "boss_final_mg", 13) == 0) {
    qa_q1am_final_boss_face(boss);
  } else {
    qa_range_error("Unknown final boss action %s", name);
  }
  (void)missile_spawner;
}
static void boss_radius_damage(qa_q1am_context *context, qa_q1am_actor *source, double damage) {
  qa_q1am_game *game = context->game;
  qa_vec3 origin = game->body(game->ctx, source).origin;
  size_t count = 0;
  qa_actor_id *observations = game->observations(game->ctx, &count);
  for (size_t i = count; i-- > 0;) {
    qa_actor_id target = observations[i];
    qa_q1am_body body;
    bool has_body = game->body_read(game->ctx, target, &body);
    if (qa_same_actor(target, source->actor.id) ||
        strcmp(game->classname_of(game->ctx, target), "monster_lava_man") == 0 ||
        !game->can_take_damage(game->ctx, target) || !has_body)
      continue;
    double distance = qa_length3(qa_sub3(
        origin, qa_add3(body.origin, qa_scale3(qa_add3(body.bounds.min, body.bounds.max), 0.5))));
    if (distance > damage + 40) continue;
    double points = damage - 0.5 * distance;
    if (points > 0 && game->can_damage(game->ctx, target, source->actor.id))
      game->damage(game->ctx, target, source->actor.id, true, source->actor.id, points);
  }
  free(observations);
}
void qa_q1am_boss_teleport(qa_q1am_context *context, bool variant) {
  qa_q1am_game *game = context->game;
  size_t total = game->entity_count(game->ctx);
  if (variant) {
    qa_q1am_actor *boss = NULL, *point = NULL;
    for (size_t i = 0; i < total; i++) {
      qa_q1am_actor *entity = game->entity_at(game->ctx, i);
      if (!entity) continue;
      if (boss == NULL && strcmp(entity->classname, "monster_boss") == 0) boss = entity;
      if (point == NULL && strcmp(entity->classname, "info_boss_teleport_boss") == 0) point = entity;
    }
    if (boss && point) {
      game->set_origin(game->ctx, boss, game->body(game->ctx, point).origin);
      game->effect(game->ctx, "lava-splash", game->body(game->ctx, boss).origin, false, (qa_actor_id){0}, 1);
      later(context, boss, "boss_final_rise1", 0.1);
    }
  }
  const char *wanted = variant ? "info_boss_teleport_second" : "info_boss_teleport_first";
  for (size_t i = 0; i < total; i++) {
    qa_q1am_actor *entity = game->entity_at(game->ctx, i);
    if (entity && strcmp(entity->classname, wanted) == 0) entity->wait = 0;
  }
  size_t count = 0;
  qa_actor_id *players = game->players(game->ctx, &count);
  for (size_t p = 0; p < count; p++) {
    qa_actor_id player = players[p];
    qa_q1am_actor *point = NULL;
    for (size_t i = 0; i < total && point == NULL; i++) {
      qa_q1am_actor *entity = game->entity_at(game->ctx, i);
      if (entity && strcmp(entity->classname, wanted) == 0 && entity->wait == 0) point = entity;
    }
    if (point == NULL) {
      qa_q1am_event event = {0};
      event.kind = "developer-message";
      event.text = "ERROR: Could not find valid teleport destination!\n";
      context->services_emit(context->services_ctx, &event);
      break;
    }
    point->wait = 1;
    qa_vec3 origin = game->body(game->ctx, point).origin;
    qa_vec3 angles = qa_q1am_actor_vector(point, "mangle");
    qa_q1am_actor *entity = game->entity(game->ctx, true, player);
    qa_q1am_player *state = game->player(game->ctx, player);
    if (entity) entity->movement_flags = (double)(FLAGS(entity->movement_flags) & ~1);
    if (state) state->view_angles = angles;
    qa_owned_actor owned;
    qa_q1am_body body;
    if (game->actors_resolve_owned(game->ctx, player, &owned) &&
        game->body_read(game->ctx, player, &body)) {
      body.origin = origin;
      body.angles = angles;
      body.has_ground = false;
      body.velocity = (qa_vec3){0, 0, 300};
      game->body_write(game->ctx, owned, &body);
      game->body_link(game->ctx, owned);
    }
    qa_q1am_event event = {0};
    event.kind = "camera";
    event.has_player = true;
    event.player = player;
    event.origin = origin;
    event.angles = angles;
    game->emit(game->ctx, &event);
    game->spawn_teleport_fog(game->ctx, game,
                             qa_add3(origin, qa_scale3(qa_q1am_game_make_vectors(game, angles).forward, 32)));
  }
  free(players);
}
static void boss_end(qa_q1am_context *context) {
  qa_q1am_game *game = context->game;
  qa_q1am_base *base = context->base;
  size_t count = 0;
  qa_actor_id *players = game->players(game->ctx, &count);
  bool has_first = count > 0;
  qa_actor_id first = has_first ? players[0] : (qa_actor_id){0};
  if (!game->options.coop && (!has_first || game->health(game->ctx, first) <= 0)) {
    free(players);
    return;
  }
  for (size_t p = 0; p < count; p++) {
    qa_q1am_player *player = game->player(game->ctx, players[p]);
    if (player == NULL) continue;
    size_t entries = game->inventory_count(game->ctx, players[p]);
    for (size_t i = 0; i < entries; i++) {
      const char *item = game->inventory_item(game->ctx, players[p], i);
      if (strcmp(item, "q1:ammo/shells") == 0 || strcmp(item, "q1:ammo/nails") == 0 ||
          strcmp(item, "q1:ammo/cells") == 0 || strcmp(item, "q1:ammo/rockets") == 0)
        game->inventory_set_count(game->ctx, player->actor, i,
                                  strcmp(item, "q1:ammo/shells") == 0 ? 25 : 0);
    }
    game->combat_set_armor_none(game->ctx, player->actor);
    (void)snprintf(player->weapon, sizeof player->weapon, "%s", "shotgun");
  }
  double flags = base->campaign_read_flags(base->ctx);
  const char *map = "start";
  if (((int64_t)flags & QA_Q1AM_BLOODY_NIGHTMARE_ACTIVE) != 0) {
    if (((int64_t)flags & QA_Q1AM_BLOODY_NIGHTMARE_NEWGAME) != 0) {
      map = "boss2";
    } else {
      map = "map1";
      base->campaign_write_flags(base->ctx, QA_Q1AM_BLOODY_NIGHTMARE_ACTIVE |
                                                QA_Q1AM_BLOODY_NIGHTMARE_DISCOVERED |
                                                QA_Q1AM_BLOODY_NIGHTMARE_NEWGAME);
      if (game->world) qa_q1am_actor_set_field(game->world, "mg3.finalNewGameTravel", "1");
      static const char *const parms[] = {"parm10", "parm11", "parm12", "parm13", "parm14"};
      for (size_t p = 0; p < count; p++)
        for (int i = 0; i < 5; i++) context->set_player_number(context->ctx, players[p], parms[i], 0);
    }
  }
  free(players);
  if (game->world) qa_q1am_actor_set_field(game->world, "addon.intermissiontext", "$mg3_qc_boss_finale");
  base->level_begin(base->ctx, map, false, (qa_actor_id){0});
}
void qa_q1am_final_boss_init(qa_q1am_final_boss *boss, qa_q1am_context *context, qa_q1am_actor *entity) {
  memset(boss, 0, sizeof *boss);
  boss->context = context;
  qa_q1am_monster_init(&boss->base, context->game, entity, &final_spec, context->base, final_source());
  boss->base.owner = boss;
  boss->base.kind = QA_Q1AM_KIND_FINAL;
  boss->super_ops = boss->base.ops;
  boss->base.ops.spawn = final_spawn;
  boss->base.ops.awake = final_awake;
  boss->base.ops.play = final_play;
  boss->base.ops.pain = final_pain;
  boss->base.ops.die = final_die;
}
static qa_q1am_monster *create_final(void *ctx, qa_q1am_actor *entity) {
  qa_q1am_final_boss *boss = malloc(sizeof *boss);
  if (!boss) qa_range_error("Final boss allocation failed");
  qa_q1am_final_boss_init(boss, ctx, entity);
  return &boss->base;
}
static void destroy_final(void *ctx, qa_q1am_monster *monster) {
  (void)ctx;
  free(monster->owner);
}
typedef struct final_effects_ctx {
  qa_q1am_context *context;
  qa_q1am_boss_registry *bosses;
} final_effects_ctx;
static bool final_before_health(void *ctx, const qa_q1am_damage_request *request) {
  final_effects_ctx *state = ctx;
  qa_q1am_game *game = state->context->game;
  qa_q1am_actor *entity = game->entity(game->ctx, true, request->target);
  return entity == NULL || !qa_q1am_boss_registry_has(state->bosses, entity->actor) ||
         (FLAGS(entity->spawnflags) & 2) == 0 ||
         (qa_q1am_actor_number(entity, "boss_immune") != 1 &&
          (!request->has_attacker ||
           strcmp(game->classname_of(game->ctx, request->attacker), "monster_lava_man") != 0));
}
static double final_after_armor(void *ctx, const qa_q1am_damage_request *request, double take) {
  final_effects_ctx *state = ctx;
  qa_q1am_game *game = state->context->game;
  qa_q1am_actor *entity = game->entity(game->ctx, true, request->target);
  qa_q1am_player *player = request->has_attacker ? game->player(game->ctx, request->attacker) : NULL;
  const char *weapon = player ? player->weapon : NULL;
  return entity != NULL && qa_q1am_boss_registry_has(state->bosses, entity->actor) &&
                 (FLAGS(entity->spawnflags) & 2) != 0 &&
                 !(weapon && strcmp(weapon, "lightning") == 0) &&
                 !(weapon && strcmp(weapon, "mg3:laser") == 0)
             ? FR(take * FR(0.8)) : take;
}
static void melee_check_handler(void *ctx, qa_q1am_game *game, qa_q1am_actor *entity) {
  (void)game;
  qa_q1am_final_boss_melee_check(qa_q1am_require_boss(ctx, entity)->owner);
}
static void rise_handler(void *ctx, qa_q1am_game *game, qa_q1am_actor *entity) {
  (void)game;
  qa_q1am_monster *boss = qa_q1am_require_boss(ctx, entity);
  boss->ops.play(boss, "boss_final_rise1");
}
typedef struct teleport_spawn_ctx { qa_q1am_context *context; char classname[48]; } teleport_spawn_ctx;
static void teleport_spawn(void *ctx, qa_q1am_game *game, qa_q1am_actor *entity) {
  teleport_spawn_ctx *state = ctx;
  state->context->set_vector(state->context->ctx, entity, "mangle", game->body(game->ctx, entity).angles);
  entity->model[0] = '\0';
  qa_q1am_body_patch patch = {0};
  patch.has_angles = true;
  patch.angles = QA_Q1AM_ZERO;
  patch.has_origin = true;
  patch.origin = qa_add3(game->body(game->ctx, entity).origin,
                         (qa_vec3){0, 0, strcmp(state->classname, "info_boss_teleport_boss") == 0 ? 0 : 27});
  game->set_body(game->ctx, entity, &patch);
}
static void teleport_use(void *ctx, qa_q1am_game *game, qa_q1am_actor *entity, bool has_other,
                         qa_actor_id other, bool has_activator, qa_actor_id activator) {
  (void)game; (void)has_other; (void)other; (void)has_activator; (void)activator;
  qa_q1am_boss_teleport(ctx, (FLAGS(entity->spawnflags) & 1) != 0);
}
static void trigger_teleport_spawn(void *ctx, qa_q1am_game *game, qa_q1am_actor *entity) {
  (void)ctx;
  entity->use = game->named_use(game->ctx, entity, "mg3:final:trigger_boss_teleport_use");
}
static void rock_touch_handler(void *ctx, qa_q1am_game *game, qa_q1am_actor *entity, qa_actor_id other,
                               bool has_normal, qa_vec3 normal) {
  (void)ctx; (void)has_normal;
  if (entity->has_owner && qa_same_actor(entity->owner, other)) return;
  qa_q1am_actor *target = game->entity(game->ctx, true, other);
  const char *classname = game->classname_of(game->ctx, other);
  if (strcmp(classname, "monster_orb") == 0 || strcmp(classname, "monster_lava_man") == 0) {
    game->remove(game->ctx, entity);
    return;
  }
  if (strcmp(classname, entity->classname) == 0 || (target && strcmp(target->solid, "trigger") == 0)) return;
  if (strcmp(game->contents(game->ctx, game->body(game->ctx, entity).origin), "sky") == 0) {
    game->remove(game->ctx, entity);
    return;
  }
  if (game->can_take_damage(game->ctx, other)) {
    qa_vec3 direction = qa_q1am_normalize(qa_add3(
        qa_add3(qa_q1am_normalize(game->body(game->ctx, entity).velocity),
                qa_scale3(game->basis.up, game->random(game->ctx) - 0.5)),
        qa_scale3(game->basis.right, game->random(game->ctx) - 0.5)));
    qa_vec3 spray = qa_scale3(qa_scale3(qa_add3(direction, qa_scale3(has_normal ? normal : QA_Q1AM_ZERO, 2)), 200), 0.2);
    qa_q1am_event event = {0};
    event.kind = "particles";
    event.origin = qa_add3(game->body(game->ctx, entity).origin, qa_scale3(spray, 0.01));
    event.direction = qa_scale3(spray, 0.1);
    event.color = 73;
    event.count = 36;
    game->emit(game->ctx, &event);
    game->damage(game->ctx, other, entity->actor.id, entity->has_owner, entity->owner, 18);
  } else if (qa_q1am_actor_number(entity, "frags") != 0) {
    game->effect(game->ctx, "knight-spike", game->body(game->ctx, entity).origin, false, (qa_actor_id){0}, 1);
  }
  game->remove(game->ctx, entity);
}
static void rock_death_handler(void *ctx, qa_q1am_game *game, qa_q1am_actor *entity) {
  qa_q1am_context *context = ctx;
  size_t total = game->entity_count(game->ctx);
  for (size_t i = 0; i < total; i++) {
    qa_q1am_actor *other = game->entity_at(game->ctx, i);
    if (other && strcmp(other->classname, "rock") == 0 &&
        qa_length3(qa_sub3(game->body(game->ctx, other).origin, game->body(game->ctx, entity).origin)) <= 96)
      later(context, other, "Rock_Death", 0.1);
  }
  game->effect(game->ctx, "explosion", game->body(game->ctx, entity).origin, false, (qa_actor_id){0}, 1);
  game->remove(game->ctx, entity);
}
static void spiral_single_handler(void *ctx, qa_q1am_game *game, qa_q1am_actor *entity) {
  qa_q1am_context *context = ctx;
  qa_q1am_body_patch patch = {0};
  patch.has_angles = true;
  patch.angles = qa_add3(game->body(game->ctx, entity).angles, (qa_vec3){0, 5, 0});
  game->set_body(game->ctx, entity, &patch);
  qa_q1am_game_make_vectors(game, game->body(game->ctx, entity).angles);
  if (game->options.skill < 2 && fmod(entity->count, 10) < 5) {
    later(context, entity, "spiral_single", entity->delay);
    entity->count--;
    return;
  }
  for (int i = 3; i >= 0; i--) {
    qa_vec3 direction = qa_q1am_game_make_vectors(
        game, qa_add3(game->body(game->ctx, entity).angles, (qa_vec3){0, 90.0 * i, 0})).forward;
    qa_q1am_actor *shot = rock(context, entity->has_owner, entity->owner,
                               qa_add3(game->body(game->ctx, entity).origin, qa_scale3(direction, 100)),
                               direction, "plasma");
    memset(&patch, 0, sizeof patch);
    patch.has_velocity = true;
    patch.velocity = qa_scale3(direction, 100);
    game->set_body(game->ctx, shot, &patch);
    spin(context, shot);
    game->schedule(game->ctx, shot, 30, game->named_action(game->ctx, shot, "SUB_Remove"));
  }
  entity->count--;
  if (!entity->has_owner || game->health(game->ctx, entity->owner) <= 0) {
    game->remove(game->ctx, entity);
    return;
  }
  later(context, entity, "spiral_single", entity->delay);
}
static void circle_think_handler(void *ctx, qa_q1am_game *game, qa_q1am_actor *entity) {
  qa_q1am_context *context = ctx;
  for (int remaining = 72; remaining > 0; remaining--) {
    qa_vec3 direction = qa_q1am_game_make_vectors(game, (qa_vec3){0, (72 - remaining) * 5.0, 0}).forward;
    qa_q1am_actor *shot = rock(context, true, entity->actor.id, game->body(game->ctx, entity).origin,
                               direction, "sphere");
    qa_q1am_body_patch patch = {0};
    patch.has_velocity = true;
    patch.velocity = qa_scale3(direction, 250 + random_signed(context) * 100);
    game->set_body(game->ctx, shot, &patch);
    spin(context, shot);
    game->schedule(game->ctx, shot, 20, game->named_action(game->ctx, shot, "SUB_Remove"));
    if (remaining % 4 == 0) shot->effects = (double)((int32_t)shot->effects | 8);
  }
  game->remove(game->ctx, entity);
}
static void boss_end_handler(void *ctx, qa_q1am_game *game, qa_q1am_actor *entity) {
  (void)game; (void)entity;
  boss_end(ctx);
}
void qa_q1am_register_final_boss(qa_q1am_context *context) {
  qa_q1am_game *game = context->game;
  qa_q1am_boss_create create = {context, create_final, destroy_final};
  qa_q1am_boss_registry *bosses =
      qa_q1am_register_boss_controllers(context, "mg3:final", "monster_boss_final", create);
  final_effects_ctx *effects_ctx = malloc(sizeof *effects_ctx);
  if (!effects_ctx) qa_range_error("Final boss allocation failed");
  effects_ctx->context = context;
  effects_ctx->bosses = bosses;
  qa_q1am_damage_effects effects = {effects_ctx, final_before_health, final_after_armor};
  game->register_damage_source_effects(game->ctx, "mg3:final", effects);
  qa_q1am_handlers handlers = {0};
  handlers.ctx = bosses;
  handlers.action = melee_check_handler;
  game->named_register(game->ctx, "mg3:final:boss_final_melee_check", handlers);
  handlers.action = rise_handler;
  game->named_register(game->ctx, "mg3:final:boss_final_rise1", handlers);
  static const char *const teleport_classnames[] = {
    "info_boss_teleport_first", "info_boss_teleport_second", "info_boss_teleport_boss",
  };
  for (int i = 0; i < 3; i++) {
    teleport_spawn_ctx *state = malloc(sizeof *state);
    if (!state) qa_range_error("Final boss allocation failed");
    state->context = context;
    (void)snprintf(state->classname, sizeof state->classname, "%s", teleport_classnames[i]);
    game->register_spawn(game->ctx, teleport_classnames[i], teleport_spawn, state);
  }
  handlers.ctx = context;
  handlers.action = NULL;
  handlers.use = teleport_use;
  game->named_register(game->ctx, "mg3:final:trigger_boss_teleport_use", handlers);
  game->register_spawn(game->ctx, "trigger_boss_teleport", trigger_teleport_spawn, NULL);
  handlers.use = NULL;
  handlers.touch = rock_touch_handler;
  game->named_register(game->ctx, "mg3:final:T_RockTouch2", handlers);
  handlers.touch = NULL;
  handlers.action = rock_death_handler;
  game->named_register(game->ctx, "mg3:final:Rock_Death", handlers);
  handlers.action = spiral_single_handler;
  game->named_register(game->ctx, "mg3:final:spiral_single", handlers);
  handlers.action = circle_think_handler;
  game->named_register(game->ctx, "mg3:final:circle_think", handlers);
  handlers.action = boss_end_handler;
  game->named_register(game->ctx, "mg3:bosses:boss_end", handlers);
}
