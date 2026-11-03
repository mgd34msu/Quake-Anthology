/* Port of src/content/q1/addons/monsters/heavy/rune-knight.ts.
 * quakec_mg3/monsters/mg3_rknight.qc. GPL-2.0-or-later. */
#include "qa/content/q1/addons/monsters/heavy/rune_knight.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "qa/content/q1/addons/campaign.h"
#include "qa/content/q1/addons/monsters/heavy/projectiles.h"
#include "qa/content/q1/addons/monsters/heavy/tables/mg3_rknight.h"
#include "qa/content/q1/base/projectiles.h"
#include "qa/content/q1/base/provider.h"
#include "qa/content/q1/missionpacks/types.h"
#include "../support.h"

static qa_q1_base_monster *base_of(qa_q1am_heavy_monster *monster) { return &monster->base.base; }
static qa_vec3 target_or_zero(qa_q1_base_monster *monster) {
  qa_vec3 target = QA_Q1_ZERO;
  (void)qa_q1_base_monster_target(monster, &target);
  return target;
}
static bool bloody_nightmare(qa_q1am_heavy_monster *monster) {
  const qa_q1_campaign_binding campaign = qa_q1_base_campaign(monster->runtime->context->base);
  return (qa_js_to_int32(campaign.read_flags(campaign.ctx)) & QA_Q1AD_BLOODY_NIGHTMARE_ACTIVE) != 0;
}
static void shot(qa_q1am_heavy_monster *heavy, double offset, int variant) {
  qa_q1_base_monster *monster = base_of(heavy);
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  const qa_q1_body body = qa_q1_get_body(game, entity);
  qa_vec3 a = target_or_zero(monster);
  qa_vec3 b = qa_q1_base_monster_origin(monster);
  const qa_vec3 delta = qa_sub3(a, b);
  const qa_vec3 center = qa_add3(body.origin, qa_scale3(qa_add3(body.bounds.min, body.bounds.max), 0.5));
  qa_vec3 origin, direction;
  if (variant == 0) {
    qa_vec3 angles = qa_q1mp_velocity_angles(delta);
    angles.y = angles.y + offset * 6;
    const qa_vec3 forward = qa_q1_make_vectors(game, angles).forward;
    origin = qa_add3(center, qa_scale3(forward, 20));
    direction = qa_q1_normalize(forward);
    direction.z = -direction.z + (qa_q1am_random(game) - 0.5) * 0.1;
  } else {
    const qa_vec3 forward = qa_q1_make_vectors(game, body.angles).forward;
    origin = qa_add3(center, qa_scale3(forward, 10));
    direction = qa_q1_normalize(delta);
    const double nudge = 0.017 * offset * 6;
    if (variant == 1) {
      origin = qa_add3(origin, (qa_vec3){0, 0, 10});
      direction.z = direction.z + nudge;
      direction.y = direction.y + (qa_q1am_random(game) - 0.5) * 0.2;
    } else {
      origin = qa_add3(origin, (qa_vec3){0, 0, 14});
      direction.y = direction.y + (qa_q1am_random(game) - 0.5) * 0.1 * (offset + 2);
      direction.z = direction.z + (qa_q1am_random(game) - 0.5) * 0.1 * (offset + 2);
    }
    direction = qa_q1_normalize(direction);
  }
  qa_q1_actor *missile = qa_q1am_heavy_spike(game, entity->actor.id, origin, qa_scale3(direction, 1000));
  qa_q1ad_set_text(&missile->model, "progs/diamond_trail.mdl");
  qa_q1am_set_velocity_body(game, missile, qa_scale3(direction, bloody_nightmare(heavy) ? 600 : 400));
  missile->effects = 64;
  qa_q1_link(game, missile);
  qa_q1_sound(game, entity->actor.id, "hknight/attack1.wav", "weapon", 1, 1);
}
static void idle(qa_q1am_heavy_monster *heavy) {
  qa_q1_base_monster *monster = base_of(heavy);
  qa_q1_services *game = monster->game;
  if (qa_q1am_random(game) >= 0.2) return;
  const double r = qa_q1am_random(game);
  qa_q1_sound_defaults(game, monster->entity->actor.id,
                       r < 0.3 ? "rknight/idle_02.wav" : r < 0.6 ? "rknight/idle_03.wav" : "rknight/idle_05.wav");
}
static void pain_sound(qa_q1am_heavy_monster *heavy) {
  qa_q1_base_monster *monster = base_of(heavy);
  const double r = qa_q1am_random(monster->game);
  qa_q1_sound_defaults(monster->game, monster->entity->actor.id,
                       r < 0.3 ? "rknight/pain_01.wav" : r < 0.6 ? "rknight/pain_02.wav" : "rknight/pain_03.wav");
}
static void magic(qa_q1am_heavy_monster *heavy) {
  qa_q1_base_monster *monster = base_of(heavy);
  qa_vec3 a = target_or_zero(monster);
  qa_vec3 b = qa_q1_base_monster_origin(monster);
  const qa_vec3 delta = qa_sub3(a, b);
  const double r = qa_length3(delta) > 300 ? 0.6 : 0.2;
  if (qa_q1am_random(monster->game) < r) { qa_q1_base_monster_play(monster, "rknight_magicb1"); return; }
  qa_q1_base_monster_play(monster, qa_q1am_random(monster->game) > (delta.z > 100 ? 0.7 : 0.5) ? "rknight_magica1" : "rknight_magicc1");
}
static void run(qa_q1am_heavy_monster *heavy) {
  qa_q1_base_monster *monster = base_of(heavy);
  qa_q1_base_monster_play(monster, (QA_Q1AM_FLAGS(monster->entity->spawnflags) & 2) != 0 ? "rknight_runb1" : "rknight_run1");
}
static void melee(qa_q1am_heavy_monster *heavy) {
  qa_q1_base_monster *monster = base_of(heavy);
  heavy->runtime->rune_knight_melee_cycle++;
  qa_q1_sound(monster->game, monster->entity->actor.id, "hknight/slash1.wav", "weapon", 1, 1);
  if (heavy->runtime->rune_knight_melee_cycle == 1) { qa_q1_base_monster_play(monster, "rknight_slice1"); return; }
  if (heavy->runtime->rune_knight_melee_cycle == 2) { qa_q1_base_monster_play(monster, "rknight_smash1"); return; }
  if (heavy->runtime->rune_knight_melee_cycle == 3) {
    qa_q1_base_monster_play(monster, "rknight_watk1");
    heavy->runtime->rune_knight_melee_cycle = 0;
  }
}
static void magicb6(qa_q1am_heavy_monster *heavy) {
  qa_q1_base_monster_face(base_of(heavy));
  qa_q1am_heavy_monster_number(heavy, "ammo_nails", floor(qa_q1am_random(base_of(heavy)->game) * 8 + 0.5));
}
static void magicb12(qa_q1am_heavy_monster *heavy) {
  qa_q1_base_monster *monster = base_of(heavy);
  qa_q1_base_monster_face(monster);
  shot(heavy, 3, 2);
  qa_q1am_heavy_monster_number(heavy, "ammo_nails", qa_q1_actor_number(monster->entity, "ammo_nails") - 1);
  if (qa_q1_actor_number(monster->entity, "ammo_nails") > 0)
    qa_q1am_copy_name(monster->next_frame, sizeof monster->next_frame, "rknight_magicb12");
}
/* The TS loops: magica{8+i} -> face, shot(1.5 - i, 1); magicb{7+i} -> face, shot(i - 2, 2); magicc{6+i} -> shot(i - 2, 0). */
#define MAGICA(I) static void magica##I(qa_q1am_heavy_monster *m) { qa_q1_base_monster_face(base_of(m)); shot(m, 1.5 - (I), 1); }
#define MAGICB(I) static void magicb_##I(qa_q1am_heavy_monster *m) { qa_q1_base_monster_face(base_of(m)); shot(m, (I) - 2, 2); }
#define MAGICC(I) static void magicc##I(qa_q1am_heavy_monster *m) { shot(m, (I) - 2, 0); }
MAGICA(0) MAGICA(1) MAGICA(2) MAGICA(3) MAGICA(4)
MAGICB(0) MAGICB(1) MAGICB(2) MAGICB(3) MAGICB(4)
MAGICC(0) MAGICC(1) MAGICC(2) MAGICC(3) MAGICC(4) MAGICC(5)
#undef MAGICA
#undef MAGICB
#undef MAGICC
static const qa_q1am_heavy_action_entry actions[] = {
  {"rk_idle_sound", idle}, {"rknight_pain_sound", pain_sound}, {"rknight_magic", magic}, {"rknight_run", run},
  {"rknight_melee", melee}, {"mg3_rknight:rknight_magicb6", magicb6}, {"mg3_rknight:rknight_magicb12", magicb12},
  {"mg3_rknight:rknight_magica8", magica0}, {"mg3_rknight:rknight_magicb7", magicb_0},
  {"mg3_rknight:rknight_magica9", magica1}, {"mg3_rknight:rknight_magicb8", magicb_1},
  {"mg3_rknight:rknight_magica10", magica2}, {"mg3_rknight:rknight_magicb9", magicb_2},
  {"mg3_rknight:rknight_magica11", magica3}, {"mg3_rknight:rknight_magicb10", magicb_3},
  {"mg3_rknight:rknight_magica12", magica4}, {"mg3_rknight:rknight_magicb11", magicb_4},
  {"mg3_rknight:rknight_magicc6", magicc0}, {"mg3_rknight:rknight_magicc7", magicc1},
  {"mg3_rknight:rknight_magicc8", magicc2}, {"mg3_rknight:rknight_magicc9", magicc3},
  {"mg3_rknight:rknight_magicc10", magicc4}, {"mg3_rknight:rknight_magicc11", magicc5},
};
static void spawn(qa_q1am_heavy_monster *heavy) {
  qa_q1_base_monster *monster = base_of(heavy);
  qa_gameplay_authority_set_health(monster->game->host.combat, monster->entity->actor, 250);
  qa_q1am_heavy_monster_number(heavy, "allowPathFind", 1);
  qa_q1am_heavy_monster_number(heavy, "combat_style", 1);
  qa_q1am_heavy_monster_initialize(heavy, 1);
}
static void sight(qa_q1am_heavy_monster *heavy) {
  qa_q1_base_monster *monster = base_of(heavy);
  qa_q1_sound_defaults(monster->game, monster->entity->actor.id,
                       qa_q1am_random(monster->game) < 0.5 ? "rknight/sight_01.wav" : "rknight/sight_03.wav");
}
static void pain(qa_q1am_heavy_monster *heavy, const qa_actor_id *attacker, double damage) {
  (void)attacker; /* TS (monster, _attacker, damage) */
  qa_q1_base_monster *monster = base_of(heavy);
  qa_q1_services *game = monster->game;
  qa_q1_monster *state = monster->state;
  if (state->pain_finished > game->time) return;
  if (game->time - state->pain_finished > 5) {
    qa_q1_base_monster_play(monster, "rknight_pain1");
    state->pain_finished = game->time + 1;
    return;
  }
  if (qa_q1am_random(game) * 30 > damage) return;
  state->pain_finished = game->time + 1;
  qa_q1_base_monster_play(monster, "rknight_pain1");
}
static void die(qa_q1am_heavy_monster *heavy) {
  qa_q1_base_monster *monster = base_of(heavy);
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  const double health = qa_q1_health(game, entity->actor.id);
  if (health < -40) {
    qa_q1_sound_defaults(game, entity->actor.id, "player/udeath.wav");
    qa_q1_throw_head_damage(game, entity, "h_hellkn", health);
    static const char *const models[] = {"gib1", "gib2", "gib3"};
    for (size_t i = 0; i < 3; i++) (void)qa_q1_throw_gib(game, qa_q1_base_monster_origin(monster), models[i], health);
    return;
  }
  qa_q1_sound_defaults(game, entity->actor.id, qa_q1am_random(game) < 0.5 ? "rknight/death_01.wav" : "rknight/death_02.wav");
  qa_q1_base_monster_play(monster, qa_q1am_random(game) > 0.5 ? "rknight_die1" : "rknight_dieb1");
}
static const char *const classnames[] = {"monster_ranged_knight"};
static const char *const gibs[] = {"gib1", "gib2", "gib3"};
const qa_q1am_heavy_definition qa_q1am_rune_knight_definition = {
  .spec = {.species = "hellknight", .classnames = classnames, .classname_count = 1, .model = "rknight", .head = "h_hellkn",
           .health = 250, .gib_health = -40, .gibs = gibs, .gib_count = 3, .bounds = {{-16, -16, -24}, {16, 16, 32}},
           .stand = "rknight_stand1", .walk = "rknight_walk1", .run = "rknight_run", .sight = "", .melee = false,
           .missile = "rknight_magic", .movement = "walk"},
  .frames = &qa_q1am_mg3_rknight_frames,
  .actions = actions, .action_count = sizeof actions / sizeof actions[0],
  .spawn = spawn, .sight = sight, .pain = pain, .die = die,
};
