/* Port of src/content/q1/addons/monsters/heavy/rune-knight.ts.
 * GPL-2.0-or-later. */
#include "qa/content/q1/addons/monsters/heavy/rune_knight.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "qa/content/q1/addons/monsters/heavy/projectiles.h"
#include "qa/content/q1/addons/monsters/heavy/tables/mg3_rknight.h"

static void shot(qa_q1am_heavy_monster *monster, double offset, int variant) {
  qa_q1am_monster *m = &monster->base;
  qa_q1am_game *game = m->game;
  qa_q1am_actor *entity = m->entity;
  qa_q1am_body body = game->body(game->ctx, entity);
  qa_vec3 target;
  qa_vec3 delta = qa_sub3(qa_q1am_monster_target(m, &target) ? target : QA_Q1AM_ZERO,
                          qa_q1am_monster_origin(m));
  qa_vec3 origin, direction;
  if (variant == 0) {
    qa_vec3 angles = qa_q1am_velocity_angles(delta);
    angles.y += offset * 6;
    qa_vec3 forward = qa_q1am_game_make_vectors(game, angles).forward;
    origin = qa_add3(qa_add3(body.origin, qa_scale3(qa_add3(body.bounds.min, body.bounds.max), 0.5)),
                     qa_scale3(forward, 20));
    direction = qa_q1am_normalize(forward);
    direction.z = -direction.z + (game->random(game->ctx) - 0.5) * 0.1;
  } else {
    qa_vec3 forward = qa_q1am_game_make_vectors(game, body.angles).forward;
    origin = qa_add3(qa_add3(body.origin, qa_scale3(qa_add3(body.bounds.min, body.bounds.max), 0.5)),
                     qa_scale3(forward, 10));
    direction = qa_q1am_normalize(delta);
    double nudge = 0.017 * offset * 6;
    if (variant == 1) {
      origin = qa_add3(origin, (qa_vec3){0, 0, 10});
      direction.z += nudge;
      direction.y += (game->random(game->ctx) - 0.5) * 0.2;
    } else {
      origin = qa_add3(origin, (qa_vec3){0, 0, 14});
      direction.y += (game->random(game->ctx) - 0.5) * 0.1 * (offset + 2);
      direction.z += (game->random(game->ctx) - 0.5) * 0.1 * (offset + 2);
    }
    direction = qa_q1am_normalize(direction);
  }
  qa_q1am_actor *missile = qa_q1am_heavy_spike(game, entity->actor.id, origin, qa_scale3(direction, 1000));
  (void)snprintf(missile->model, sizeof missile->model, "%s", "progs/diamond_trail.mdl");
  qa_q1am_body_patch patch = {0};
  patch.has_velocity = true;
  double flags = monster->runtime->context->base->campaign_read_flags(monster->runtime->context->base->ctx);
  patch.velocity = qa_scale3(direction, ((int64_t)flags & QA_Q1AM_BLOODY_NIGHTMARE_ACTIVE) != 0 ? 600 : 400);
  game->set_body(game->ctx, missile, &patch);
  missile->effects = 64;
  game->link(game->ctx, missile);
  game->sound(game->ctx, entity, "hknight/attack1.wav", "weapon", 1);
}
static void idle(qa_q1am_heavy_monster *monster) {
  qa_q1am_monster *m = &monster->base;
  if (m->game->random(m->game->ctx) >= 0.2) return;
  double r = m->game->random(m->game->ctx);
  m->game->sound(m->game->ctx, m->entity,
                 r < 0.3 ? "rknight/idle_02.wav" : r < 0.6 ? "rknight/idle_03.wav" : "rknight/idle_05.wav",
                 "voice", 1);
}
static void pain_sound(qa_q1am_heavy_monster *monster) {
  qa_q1am_monster *m = &monster->base;
  double r = m->game->random(m->game->ctx);
  char path[64];
  (void)snprintf(path, sizeof path, "rknight/pain_0%d.wav", r < 0.3 ? 1 : r < 0.6 ? 2 : 3);
  m->game->sound(m->game->ctx, m->entity, path, "voice", 1);
}
static void magic(qa_q1am_heavy_monster *monster) {
  qa_q1am_monster *m = &monster->base;
  qa_vec3 target;
  qa_vec3 delta = qa_sub3(qa_q1am_monster_target(m, &target) ? target : QA_Q1AM_ZERO,
                          qa_q1am_monster_origin(m));
  double r = qa_length3(delta) > 300 ? 0.6 : 0.2;
  if (m->game->random(m->game->ctx) < r) {
    m->ops.play(m, "rknight_magicb1");
    return;
  }
  m->ops.play(m, m->game->random(m->game->ctx) > (delta.z > 100 ? 0.7 : 0.5)
                     ? "rknight_magica1" : "rknight_magicc1");
}
static void rk_run(qa_q1am_heavy_monster *monster) {
  qa_q1am_monster *m = &monster->base;
  m->ops.play(m, ((int32_t)m->entity->spawnflags & 2) != 0 ? "rknight_runb1" : "rknight_run1");
}
static void rk_melee(qa_q1am_heavy_monster *monster) {
  qa_q1am_monster *m = &monster->base;
  monster->runtime->rune_knight_melee_cycle++;
  m->game->sound(m->game->ctx, m->entity, "hknight/slash1.wav", "weapon", 1);
  if (monster->runtime->rune_knight_melee_cycle == 1) m->ops.play(m, "rknight_slice1");
  else if (monster->runtime->rune_knight_melee_cycle == 2) m->ops.play(m, "rknight_smash1");
  else if (monster->runtime->rune_knight_melee_cycle == 3) {
    m->ops.play(m, "rknight_watk1");
    monster->runtime->rune_knight_melee_cycle = 0;
  }
}
static void magicb6(qa_q1am_heavy_monster *monster) {
  qa_q1am_monster_face(&monster->base);
  qa_q1am_heavy_monster_set_number(monster, "ammo_nails",
                                   floor(monster->base.game->random(monster->base.game->ctx) * 8 + 0.5));
}
static void magicb12(qa_q1am_heavy_monster *monster) {
  qa_q1am_monster_face(&monster->base);
  shot(monster, 3, 2);
  qa_q1am_heavy_monster_set_number(monster, "ammo_nails",
                                   qa_q1am_actor_number(monster->base.entity, "ammo_nails") - 1);
  if (qa_q1am_actor_number(monster->base.entity, "ammo_nails") > 0)
    qa_q1am_monster_set_frame(monster->base.next_frame, sizeof monster->base.next_frame, "rknight_magicb12");
}
/* TS builds these entries in loops; the C table spells them out. */
#define SHOT_A(name, off) \
  static void name(qa_q1am_heavy_monster *monster) { qa_q1am_monster_face(&monster->base); shot(monster, off, 1); }
#define SHOT_B(name, off) \
  static void name(qa_q1am_heavy_monster *monster) { qa_q1am_monster_face(&monster->base); shot(monster, off, 2); }
#define SHOT_C(name, off) \
  static void name(qa_q1am_heavy_monster *monster) { shot(monster, off, 0); }
SHOT_A(magica8, 1.5) SHOT_A(magica9, 0.5) SHOT_A(magica10, -0.5) SHOT_A(magica11, -1.5) SHOT_A(magica12, -2.5)
SHOT_B(magicb7, -2) SHOT_B(magicb8, -1) SHOT_B(magicb9, 0) SHOT_B(magicb10, 1) SHOT_B(magicb11, 2)
SHOT_C(magicc6, -2) SHOT_C(magicc7, -1) SHOT_C(magicc8, 0) SHOT_C(magicc9, 1) SHOT_C(magicc10, 2) SHOT_C(magicc11, 3)
static const qa_q1am_heavy_action_entry rk_actions[] = {
  {"rk_idle_sound", idle},
  {"rknight_pain_sound", pain_sound},
  {"rknight_magic", magic},
  {"rknight_run", rk_run},
  {"rknight_melee", rk_melee},
  {"mg3_rknight:rknight_magicb6", magicb6},
  {"mg3_rknight:rknight_magicb12", magicb12},
  {"mg3_rknight:rknight_magica8", magica8},
  {"mg3_rknight:rknight_magica9", magica9},
  {"mg3_rknight:rknight_magica10", magica10},
  {"mg3_rknight:rknight_magica11", magica11},
  {"mg3_rknight:rknight_magica12", magica12},
  {"mg3_rknight:rknight_magicb7", magicb7},
  {"mg3_rknight:rknight_magicb8", magicb8},
  {"mg3_rknight:rknight_magicb9", magicb9},
  {"mg3_rknight:rknight_magicb10", magicb10},
  {"mg3_rknight:rknight_magicb11", magicb11},
  {"mg3_rknight:rknight_magicc6", magicc6},
  {"mg3_rknight:rknight_magicc7", magicc7},
  {"mg3_rknight:rknight_magicc8", magicc8},
  {"mg3_rknight:rknight_magicc9", magicc9},
  {"mg3_rknight:rknight_magicc10", magicc10},
  {"mg3_rknight:rknight_magicc11", magicc11},
};
static void rk_spawn(qa_q1am_heavy_monster *monster) {
  qa_q1am_monster *m = &monster->base;
  m->game->combat_set_health(m->game->ctx, m->entity->actor, 250);
  qa_q1am_heavy_monster_set_number(monster, "allowPathFind", 1);
  qa_q1am_heavy_monster_set_number(monster, "combat_style", 1);
  qa_q1am_heavy_monster_initialize(monster, 1);
}
static void rk_sight(qa_q1am_heavy_monster *monster) {
  qa_q1am_monster *m = &monster->base;
  m->game->sound(m->game->ctx, m->entity,
                 m->game->random(m->game->ctx) < 0.5 ? "rknight/sight_01.wav" : "rknight/sight_03.wav",
                 "voice", 1);
}
static void rk_pain(qa_q1am_heavy_monster *monster, bool has, qa_actor_id attacker, double damage) {
  (void)has; (void)attacker;
  qa_q1am_monster *m = &monster->base;
  qa_q1am_game *game = m->game;
  qa_q1am_state *state = m->state;
  if (state->pain_finished > game->time) return;
  if (game->time - state->pain_finished > 5) {
    m->ops.play(m, "rknight_pain1");
    state->pain_finished = game->time + 1;
    return;
  }
  if (game->random(game->ctx) * 30 > damage) return;
  state->pain_finished = game->time + 1;
  m->ops.play(m, "rknight_pain1");
}
static void rk_die(qa_q1am_heavy_monster *monster) {
  qa_q1am_monster *m = &monster->base;
  qa_q1am_game *game = m->game;
  qa_q1am_actor *entity = m->entity;
  double health = game->health(game->ctx, entity->actor.id);
  if (health < -40) {
    game->sound(game->ctx, entity, "player/udeath.wav", "voice", 1);
    game->throw_head(game->ctx, game, entity, "h_hellkn", true, health);
    static const char *const gibs[] = {"gib1", "gib2", "gib3"};
    for (int i = 0; i < 3; i++)
      game->throw_gib(game->ctx, game, qa_q1am_monster_origin(m), gibs[i], health);
    return;
  }
  game->sound(game->ctx, entity,
              game->random(game->ctx) < 0.5 ? "rknight/death_01.wav" : "rknight/death_02.wav", "voice", 1);
  m->ops.play(m, game->random(game->ctx) > 0.5 ? "rknight_die1" : "rknight_dieb1");
}
static const char *const rk_classnames[] = {"monster_ranged_knight"};
static const char *const rk_gibs[] = {"gib1", "gib2", "gib3"};
const qa_q1am_heavy_definition qa_q1am_rune_knight_definition = {
  {"hellknight", NULL, "rknight", "h_hellkn", "rknight_stand1", "rknight_walk1", "rknight_run",
   "", "rknight_magic", "walk", rk_classnames, 1, rk_gibs, 3,
   250, -40, {{-16, -16, -24}, {16, 16, 40}}, false},
  &qa_q1am_mg3_rknight_frames, rk_actions, sizeof rk_actions / sizeof rk_actions[0],
  rk_spawn, rk_pain, rk_die, NULL, NULL, rk_sight, NULL, 0,
};
