/* Port of src/content/q1/addons/monsters/heavy/lava-man.ts.
 * GPL-2.0-or-later. */
#include "qa/content/q1/addons/monsters/heavy/lava_man.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/addons/monsters/heavy/tables/mg3_lavaman.h"

#define FLAGS(x) ((int32_t)(x))

static bool check_attack(qa_q1am_heavy_monster *monster) {
  qa_q1am_monster *m = &monster->base;
  qa_q1am_game *game = m->game;
  qa_q1am_monster_face(m);
  qa_vec3 target;
  qa_q1am_trace_request request = {
    qa_add3(qa_q1am_monster_origin(m), (qa_vec3){0, 0, 64}),
    qa_q1am_monster_target(m, &target) ? target : QA_Q1AM_ZERO,
    QA_Q1AM_POINT, true, m->entity->actor.id, true};
  qa_q1am_trace trace = game->trace(game->ctx, &request);
  if ((trace.in_open && trace.in_water) || !m->state->has_enemy || !trace.has_actor ||
      !qa_same_actor(trace.actor, m->state->enemy) || game->time < m->state->attack_finished)
    return false;
  m->ops.play(m, "lavaman_fire1");
  m->ops.attack_finished(m, 1 + game->random(game->ctx));
  return true;
}
static void hunt(qa_q1am_heavy_monster *monster) {
  qa_q1am_monster *m = &monster->base;
  qa_q1am_game *game = m->game;
  qa_q1am_actor *entity = m->entity;
  if (!m->state->has_enemy || game->health(game->ctx, m->state->enemy) <= 0) {
    size_t count = 0;
    qa_actor_id *actors = game->observations(game->ctx, &count);
    long current = -1;
    if (m->state->has_enemy)
      for (size_t i = 0; i < count; i++)
        if (qa_same_actor(actors[i], m->state->enemy)) current = (long)i;
    bool has_player = false;
    qa_actor_id player = {0};
    for (size_t i = (size_t)(current + 1); i < count && !has_player; i++)
      if (game->is_player(game->ctx, true, actors[i])) {
        player = actors[i];
        has_player = true;
      }
    if (!has_player && game->world) {
      player = game->world->actor.id;
      has_player = true;
    }
    qa_q1am_body body;
    qa_vec3 target = has_player && game->body_read(game->ctx, player, &body) ? body.origin : QA_Q1AM_ZERO;
    qa_q1am_trace_request request = {0};
    request.start = qa_add3(qa_q1am_monster_origin(m), (qa_vec3){0, 0, 96});
    request.end = target;
    request.bounds = QA_Q1AM_POINT;
    request.has_ignore = game->world != NULL;
    request.ignore = game->world ? game->world->actor.id : (qa_actor_id){0};
    if (game->trace(game->ctx, &request).fraction == 1) {
      m->state->has_enemy = has_player;
      m->state->enemy = player;
    }
    free(actors);
  }
  if (m->state->has_enemy) {
    qa_q1am_monster_face(m);
    qa_q1am_actor_set_ref(entity, "movetarget", true, m->state->enemy);
    qa_q1am_actor_set_ref(entity, "goalentity", true, m->state->enemy);
  }
}
static void think(qa_q1am_heavy_monster *monster, qa_q1am_ai mode) {
  if (monster->base.state->has_enemy) check_attack(monster);
  else hunt(monster);
  monster->base.ops.ai(&monster->base, mode, mode == QA_Q1AM_AI_STAND ? 0 : 2);
}
static void think_stand(qa_q1am_heavy_monster *monster) { think(monster, QA_Q1AM_AI_STAND); }
static void think_walk(qa_q1am_heavy_monster *monster) { think(monster, QA_Q1AM_AI_WALK); }
static void think_run(qa_q1am_heavy_monster *monster) { think(monster, QA_Q1AM_AI_RUN); }
static void launch(qa_q1am_heavy_monster *monster, double hand) {
  qa_q1am_monster *m = &monster->base;
  qa_q1am_game *game = m->game;
  qa_q1am_actor *entity = m->entity;
  qa_q1am_basis basis = qa_q1am_game_make_vectors(game, game->body(game->ctx, entity).angles);
  qa_vec3 origin = qa_add3(qa_add3(qa_add3(qa_q1am_monster_origin(m), qa_scale3(basis.forward, 40)),
                                   qa_scale3(basis.right, hand == 1 ? 65 : -65)),
                           qa_scale3(basis.up, 90));
  qa_vec3 target;
  qa_vec3 delta = qa_sub3(qa_q1am_monster_target(m, &target) ? target : QA_Q1AM_ZERO, origin);
  double duration = fmax(1, fmin(1.75, qa_length3(delta) / 380));
  qa_vec3 direction = qa_q1am_normalize(delta);
  qa_q1am_actor *ball = game->create(game->ctx, "lavaman_ball");
  ball->has_owner = true;
  ball->owner = entity->actor.id;
  ball->movement = "bounce";
  ball->solid = "bbox";
  (void)snprintf(ball->model, sizeof ball->model, "%s", "progs/lavaball.mdl");
  ball->touch = game->named_touch(game->ctx, ball, QA_Q1AM_HEAVY_PREFIX ":lavaman_touch");
  ball->angular_velocity = (qa_vec3){200, 100, 300};
  qa_q1am_body_patch patch = {0};
  patch.has_origin = true;
  patch.origin = origin;
  patch.has_angles = true;
  patch.angles = qa_q1am_velocity_angles(direction);
  patch.has_bounds = true;
  patch.bounds = QA_Q1AM_POINT;
  patch.has_velocity = true;
  patch.velocity = qa_add3(qa_scale3(direction, 600 * duration), (qa_vec3){0, 0, 200 * duration});
  game->set_body(game->ctx, ball, &patch);
  game->link(game->ctx, ball);
  game->schedule(game->ctx, ball, 6, game->named_action(game->ctx, ball, "SUB_Remove"));
  game->sound(game->ctx, entity, "boss1/throw.wav", "weapon", 1);
  if (!m->state->has_enemy || game->health(game->ctx, m->state->enemy) <= 0)
    m->ops.play(m, "lavaman_idle1");
}
static void launch1(qa_q1am_heavy_monster *monster) { launch(monster, 1); }
static void launch2(qa_q1am_heavy_monster *monster) { launch(monster, 2); }
static void death9(qa_q1am_heavy_monster *monster) {
  qa_q1am_monster *m = &monster->base;
  m->game->sound(m->game->ctx, m->entity, "boss1/out1.wav", "body", 1);
  m->game->effect(m->game->ctx, "lava-splash",
                  qa_sub3(qa_q1am_monster_origin(m), (qa_vec3){0, 0, 50}), false, (qa_actor_id){0}, 1);
}
static void death10(qa_q1am_heavy_monster *monster) {
  monster->base.game->remove(monster->base.game->ctx, monster->base.entity);
}
static void awake(qa_q1am_heavy_monster *monster, bool has_activator, qa_actor_id activator) {
  qa_q1am_monster *m = &monster->base;
  qa_q1am_game *game = m->game;
  qa_q1am_actor *entity = m->entity;
  entity->solid = "slidebox";
  entity->movement = "fly";
  entity->aimed_damage = true;
  qa_q1am_actor_set_damageable(game, entity, true);
  entity->movement_flags = (double)(FLAGS(entity->movement_flags) | 32);
  entity->ideal_yaw = game->body(game->ctx, entity).angles.y;
  double yaw_speed = qa_q1am_actor_number(entity, "yaw_speed");
  entity->yaw_speed = yaw_speed != 0 ? yaw_speed : 20;
  (void)snprintf(entity->model, sizeof entity->model, "%s", "progs/lavaman.mdl");
  game->set_bounds(game->ctx, entity, m->spec.bounds);
  monster->runtime->context->set_vector(monster->runtime->context->ctx, entity, "view_ofs", (qa_vec3){0, 0, 48});
  game->combat_set_health(game->ctx, entity->actor,
                           1250 + 250 * monster->runtime->context->services_cvar(
                                            monster->runtime->context->services_ctx, "skill"));
  qa_q1am_heavy_monster_install_callbacks(monster);
  entity->use = game->named_use(game->ctx, entity, QA_Q1AM_HEAVY_PREFIX ":lavaman_force_death");
  game->effect(game->ctx, "lava-splash", qa_sub3(qa_q1am_monster_origin(m), (qa_vec3){0, 0, 50}),
               false, (qa_actor_id){0}, 1);
  if (has_activator && game->is_player(game->ctx, true, activator)) {
    qa_q1am_player *player = game->player(game->ctx, activator);
    qa_q1am_actor *actor = game->entity(game->ctx, true, activator);
    if ((player ? qa_q1am_player_powerup(player, "invisibility") : 0) <= game->time &&
        ((actor ? FLAGS(actor->movement_flags) : 0) & 128) == 0) {
      m->state->has_enemy = true;
      m->state->enemy = activator;
    }
  }
  m->ops.play(m, "lavaman_rise1");
}
static void awake_use(void *ctx, qa_q1am_game *game, qa_q1am_actor *entity, bool has_other,
                      qa_actor_id other, bool has_activator, qa_actor_id activator) {
  (void)game; (void)has_other; (void)other;
  awake(qa_q1am_mg3_heavy_require(ctx, entity), has_activator, activator);
}
static void dead_use(void *ctx, qa_q1am_game *game, qa_q1am_actor *entity, bool has_other,
                     qa_actor_id other, bool has_activator, qa_actor_id activator) {
  (void)ctx; (void)game; (void)entity; (void)has_other; (void)other; (void)has_activator; (void)activator;
}
static void force_death_use(void *ctx, qa_q1am_game *game, qa_q1am_actor *entity, bool has_other,
                            qa_actor_id other, bool has_activator, qa_actor_id activator) {
  (void)has_other; (void)other;
  qa_q1am_heavy_monster *monster = qa_q1am_mg3_heavy_require(ctx, entity);
  qa_q1am_actor_set_damageable(game, entity, false);
  game->combat_set_health(game->ctx, entity->actor, 0);
  game->killed_monsters++;
  qa_q1am_event event = {0};
  event.kind = "monster-killed";
  event.has_actor = true;
  event.actor = entity->actor.id;
  event.total = game->total_monsters;
  event.found = game->killed_monsters;
  game->emit(game->ctx, &event);
  game->use_targets(game->ctx, entity, has_activator, activator);
  monster->base.counted_death = true;
  entity->use = game->named_use(game->ctx, entity, QA_Q1AM_HEAVY_PREFIX ":lavaman_dead_use");
  monster->base.ops.play(&monster->base, "lavaman_death1");
}
static void lavaman_touch(void *ctx, qa_q1am_game *game, qa_q1am_actor *entity, qa_actor_id other,
                          bool has_normal, qa_vec3 normal) {
  (void)ctx; (void)has_normal; (void)normal;
  if (entity->has_owner && qa_same_actor(entity->owner, other)) return;
  if (strcmp(game->contents(game->ctx, game->body(game->ctx, entity).origin), "sky") == 0) {
    game->remove(game->ctx, entity);
    return;
  }
  if (game->health(game->ctx, other) != 0)
    game->damage(game->ctx, other, entity->actor.id, entity->has_owner, entity->owner,
                 strcmp(game->classname_of(game->ctx, other), "monster_shambler") == 0 ? 20 : 40);
  game->radius_damage(game->ctx, entity->actor.id, entity->has_owner, entity->owner, 40, true, other, NULL);
  qa_q1am_body body = game->body(game->ctx, entity);
  game->set_origin(game->ctx, entity, qa_sub3(body.origin, qa_scale3(qa_q1am_normalize(body.velocity), 8)));
  game->sprite_explosion(game->ctx, game, entity);
}
static void lava_spawn(qa_q1am_heavy_monster *monster) {
  qa_q1am_monster *m = &monster->base;
  qa_q1am_game *game = m->game;
  qa_q1am_actor *entity = m->entity;
  if (game->options.deathmatch != 0) {
    game->remove(game->ctx, entity);
    return;
  }
  game->total_monsters++;
  entity->spawnflags = (double)(FLAGS(entity->spawnflags) | 16384);
  if (entity->targetname[0] != '\0') {
    entity->use = game->named_use(game->ctx, entity, QA_Q1AM_HEAVY_PREFIX ":lavaman_awake");
    return;
  }
  awake(monster, entity->has_activator, entity->activator);
}
static void lava_melee(qa_q1am_heavy_monster *monster) { monster->base.ops.play(&monster->base, "lavaman_fire1"); }
static void lava_pain(qa_q1am_heavy_monster *monster, bool has, qa_actor_id attacker, double damage) {
  (void)has; (void)attacker; (void)damage;
  qa_q1am_monster *m = &monster->base;
  if (m->entity->count == 0) {
    m->entity->count++;
    m->state->pain_finished = m->game->time + 2;
    m->ops.play(m, "lavaman_shocka1");
    return;
  }
  if (m->state->pain_finished > m->game->time || m->game->random(m->game->ctx) >= 0.05) return;
  m->state->pain_finished = m->game->time + 2;
  m->ops.play(m, "lavaman_shocka1");
}
static void lava_die(qa_q1am_heavy_monster *monster) { monster->base.ops.play(&monster->base, "lavaman_death1"); }
static const char *const lava_classnames[] = {"monster_lava_man"};
static const qa_q1am_heavy_action_entry lava_actions[] = {
  {"lavaman_stand", think_stand},
  {"lavaman_walk", think_walk},
  {"lavaman_run", think_run},
  {"lavaman_missile(1)", launch1},
  {"lavaman_missile(2)", launch2},
  {"mg3_lavaman:lavaman_death9", death9},
  {"mg3_lavaman:lavaman_death10", death10},
};
void qa_q1am_lava_man_definition(qa_q1am_mg3_heavy *runtime, qa_q1am_heavy_definition *out) {
  memset(out, 0, sizeof *out);
  out->spec = (qa_q1am_species){
    "lava-man", NULL, "lavaman", NULL, "lavaman_idle1", "lavaman_walk1", "lavaman_walk1",
    "", "lavaman_fire1", "walk", lava_classnames, 1, NULL, 0,
    1500, -INFINITY, {{-32, -32, -24}, {32, 32, 64}}, true,
  };
  out->frames = &qa_q1am_mg3_lavaman_frames;
  out->actions = lava_actions;
  out->action_count = sizeof lava_actions / sizeof lava_actions[0];
  out->spawn = lava_spawn;
  out->attack = check_attack;
  out->melee = lava_melee;
  out->pain = lava_pain;
  out->die = lava_die;
  static qa_q1am_heavy_callback_entry callbacks[4];
  callbacks[0].name = "lavaman_awake";
  callbacks[0].handlers = (qa_q1am_handlers){.ctx = runtime, .use = awake_use};
  callbacks[1].name = "lavaman_dead_use";
  callbacks[1].handlers = (qa_q1am_handlers){.ctx = runtime, .use = dead_use};
  callbacks[2].name = "lavaman_force_death";
  callbacks[2].handlers = (qa_q1am_handlers){.ctx = runtime, .use = force_death_use};
  callbacks[3].name = "lavaman_touch";
  callbacks[3].handlers = (qa_q1am_handlers){.ctx = runtime, .touch = lavaman_touch};
  out->callbacks = callbacks;
  out->callback_count = 4;
}
