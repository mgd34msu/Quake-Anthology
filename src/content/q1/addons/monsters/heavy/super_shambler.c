/* Port of src/content/q1/addons/monsters/heavy/super-shambler.ts.
 * GPL-2.0-or-later. */
#include "qa/content/q1/addons/monsters/heavy/super_shambler.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "qa/content/q1/addons/monsters/heavy/projectiles.h"
#include "qa/content/q1/addons/monsters/heavy/tables/mg3_super_shambler.h"

static qa_q1am_actor *lightning_child_entity(qa_q1am_heavy_monster *monster) {
  qa_q1am_monster *m = &monster->base;
  qa_actor_id child_id;
  qa_q1am_actor *child = qa_q1am_actor_ref(m->entity, "child", &child_id)
                             ? m->game->entity(m->game->ctx, true, child_id) : NULL;
  if (child && child->has_owner && qa_same_actor(child->owner, m->entity->actor.id) &&
      strcmp(child->classname, "lightning_child") == 0)
    return child;
  return NULL;
}
static void remove_child(qa_q1am_heavy_monster *monster) {
  qa_q1am_monster *m = &monster->base;
  qa_q1am_actor *child = lightning_child_entity(monster);
  if (child) m->game->remove(m->game->ctx, child);
  qa_q1am_actor_set_ref(m->entity, "child", false, (qa_actor_id){0});
}
static void child_frame(qa_q1am_heavy_monster *monster, double frame) {
  monster->base.entity->effects = (double)((int32_t)monster->base.entity->effects | 2);
  qa_q1am_actor *child = lightning_child_entity(monster);
  if (child) child->frame = frame;
}
static void child_frame1(qa_q1am_heavy_monster *monster) { child_frame(monster, 1); }
static void child_frame2(qa_q1am_heavy_monster *monster) { child_frame(monster, 2); }
static void child_frame3(qa_q1am_heavy_monster *monster) { child_frame(monster, 3); }
static void make_lightning_child(qa_q1am_heavy_monster *monster, bool fast) {
  qa_q1am_monster *m = &monster->base;
  qa_q1am_game *game = m->game;
  qa_q1am_actor *entity = m->entity;
  qa_q1am_monster_face(m);
  qa_q1am_monster_delay(m, entity->next_think - game->time + 0.2);
  entity->effects = (double)((int32_t)entity->effects | 2);
  qa_q1am_monster_face(m);
  qa_q1am_actor *child = game->create(game->ctx, fast ? "" : "lightning_child");
  qa_q1am_actor_set_ref(entity, "child", true, child->actor.id);
  if (!fast) {
    child->has_owner = true;
    child->owner = entity->actor.id;
  }
  (void)snprintf(child->model, sizeof child->model, "%s", "progs/s_light.mdl");
  qa_q1am_body_patch patch = {0};
  patch.has_origin = true;
  patch.origin = qa_q1am_monster_origin(m);
  patch.has_angles = true;
  patch.angles = game->body(game->ctx, entity).angles;
  game->set_body(game->ctx, child, &patch);
  game->link(game->ctx, child);
  game->schedule(game->ctx, child, fast ? 0.7 : 1.4, game->named_action(game->ctx, child, "SUB_Remove"));
}
static void lightning_child_slow(qa_q1am_heavy_monster *monster) { make_lightning_child(monster, false); }
static void lightning_child_fast(qa_q1am_heavy_monster *monster) { make_lightning_child(monster, true); }
static void cast_lightning(qa_q1am_heavy_monster *monster) {
  qa_q1am_monster *m = &monster->base;
  qa_q1am_game *game = m->game;
  qa_q1am_actor *entity = m->entity;
  entity->effects = (double)((int32_t)entity->effects | 2);
  qa_q1am_monster_face(m);
  qa_vec3 origin = qa_add3(qa_q1am_monster_origin(m), (qa_vec3){0, 0, 40});
  qa_vec3 target;
  qa_vec3 target_point = qa_q1am_monster_target(m, &target) ? target : QA_Q1AM_ZERO;
  qa_vec3 delta = qa_sub3(qa_add3(target_point, (qa_vec3){0, 0, 16}), origin);
  double range = fmax(600, fmin(1000, qa_length3(delta)));
  qa_vec3 direction = qa_q1am_normalize(delta);
  qa_q1am_trace_request request = {origin, qa_add3(qa_q1am_monster_origin(m), qa_scale3(direction, range)),
                                   QA_Q1AM_POINT, true, entity->actor.id, false};
  qa_q1am_trace trace = game->trace(game->ctx, &request);
  qa_q1am_event event = {0};
  event.kind = "beam";
  event.style_text = "lightning1";
  event.has_actor = true;
  event.actor = entity->actor.id;
  event.start = origin;
  event.end = trace.end;
  game->emit(game->ctx, &event);
  qa_q1am_heavy_monster_set_number(monster, "frags", qa_q1am_actor_number(entity, "frags") + 1);
  qa_q1am_heavy_lightning_damage(game, entity->actor.id, origin, trace.end, 10);
}
static bool attack_check(qa_q1am_heavy_monster *monster) {
  qa_q1am_monster *m = &monster->base;
  double cap = 3 + floor(m->game->random(m->game->ctx) * 2 + 0.5);
  double flags = monster->runtime->context->base->campaign_read_flags(monster->runtime->context->base->ctx);
  if (((int64_t)flags & QA_Q1AM_BLOODY_NIGHTMARE_ACTIVE) != 0) cap--;
  m->entity->count++;
  return m->entity->count > cap;
}
static double z_offset(qa_q1am_heavy_monster *monster) {
  qa_q1am_monster *m = &monster->base;
  qa_vec3 target;
  qa_vec3 delta = qa_sub3(qa_q1am_monster_target(m, &target) ? target : QA_Q1AM_ZERO,
                          qa_q1am_monster_origin(m));
  if (delta.z > 30) return 100 + (m->game->random(m->game->ctx) * 2 - 1) * 50;
  if (delta.z < -30) return -100 + (m->game->random(m->game->ctx) * 2 - 1) * 50;
  double range = qa_length3(delta);
  return range > 300 ? 50 + (m->game->random(m->game->ctx) * 2 - 1) * 25
         : range < 150 ? -50 + (m->game->random(m->game->ctx) * 2 - 1) * 25 : 0;
}
static void shot(qa_q1am_heavy_monster *monster, double offset_y, double offset_z, qa_vec3 offset, bool hands) {
  qa_q1am_monster *m = &monster->base;
  qa_q1am_game *game = m->game;
  qa_q1am_actor *entity = m->entity;
  qa_q1am_body body = game->body(game->ctx, entity);
  qa_vec3 target;
  qa_vec3 angles = qa_q1am_velocity_angles(qa_sub3(
      qa_q1am_monster_target(m, &target) ? target : QA_Q1AM_ZERO, qa_q1am_monster_origin(m)));
  if (hands) {
    angles.y += offset_y * (12 + (game->random(game->ctx) * 2 - 1) * 4);
    angles.x += offset_z * (12 + (game->random(game->ctx) * 2 - 1) * 4);
  } else {
    angles.y += offset_y * 5;
  }
  qa_vec3 forward = qa_q1am_game_make_vectors(game, angles).forward;
  qa_vec3 origin = qa_add3(qa_add3(qa_add3(body.origin, qa_scale3(qa_add3(body.bounds.min, body.bounds.max), 0.5)),
                                   qa_scale3(forward, 20)), offset);
  qa_vec3 direction = qa_q1am_normalize(forward);
  if (!hands) direction.z = -direction.z + (game->random(game->ctx) - 0.5) * 0.1;
  qa_q1am_actor *missile = qa_q1am_heavy_spike(game, entity->actor.id, origin, qa_scale3(direction, 1000));
  (void)snprintf(missile->model, sizeof missile->model, "%s", "progs/rogue/plasma.mdl");
  missile->angular_velocity = (qa_vec3){300 * (game->random(game->ctx) * 2 - 1),
                                        300 * (game->random(game->ctx) * 2 - 1),
                                        300 * (game->random(game->ctx) * 2 - 1)};
  missile->movement = "toss";
  game->set_bounds(game->ctx, missile, QA_Q1AM_POINT);
  qa_vec3 velocity = qa_scale3(direction, (game->random(game->ctx) * 2 - 1) * 100 + (hands ? 500 : 400));
  velocity.z = (hands ? 100 : 125) + (game->random(game->ctx) * 2 - 1) * 50;
  velocity.z += z_offset(monster);
  qa_q1am_body_patch patch = {0};
  patch.has_velocity = true;
  patch.velocity = velocity;
  game->set_body(game->ctx, missile, &patch);
  missile->effects = (double)((int32_t)missile->effects | 64);
  game->link(game->ctx, missile);
}
static void blast(qa_q1am_heavy_monster *monster, qa_vec3 offset, bool hands) {
  if (hands) {
    for (int x = -1; x < 2; x++)
      for (int y = -1; y < 2; y++) shot(monster, x, y, offset, true);
  } else {
    for (int i = -5; i <= 5; i++) shot(monster, i, 0, offset, false);
    monster->base.game->sound(monster->base.game->ctx, monster->base.entity, "zombie/z_shot1.wav", "weapon", 1);
  }
}
static void claw(qa_q1am_heavy_monster *monster, double side) {
  qa_q1am_monster *m = &monster->base;
  if (!m->state->has_enemy) return;
  qa_q1am_game *game = m->game;
  qa_q1am_actor *entity = m->entity;
  m->ops.ai(m, QA_Q1AM_AI_CHARGE, 10);
  if (qa_q1am_monster_distance(m) > 100) return;
  qa_q1am_monster_melee(m, 100, 20, 3, false);
  game->sound(game->ctx, entity, "shambler/smack.wav", "voice", 1);
  if (side != 0) {
    qa_q1am_basis basis = qa_q1am_game_make_vectors(game, game->body(game->ctx, entity).angles);
    game->spawn_meat_spray(game->ctx, game, entity,
                           qa_add3(qa_q1am_monster_origin(m), qa_scale3(basis.forward, 16)),
                           qa_scale3(basis.right, side));
  }
}
static void smash(qa_q1am_heavy_monster *monster) {
  blast(monster, QA_Q1AM_ZERO, false);
  qa_q1am_monster *m = &monster->base;
  if (!m->state->has_enemy) return;
  qa_q1am_game *game = m->game;
  qa_q1am_actor *entity = m->entity;
  m->ops.ai(m, QA_Q1AM_AI_CHARGE, 0);
  if (qa_q1am_monster_distance(m) > 100 || !game->can_damage(game->ctx, m->state->enemy, entity->actor.id)) return;
  qa_q1am_monster_melee(m, 100, 40, 3, false);
  game->sound(game->ctx, entity, "shambler/smack.wav", "voice", 1);
  for (int i = 0; i < 2; i++) {
    qa_q1am_basis basis = game->basis;
    game->spawn_meat_spray(game->ctx, game, entity,
                           qa_add3(qa_q1am_monster_origin(m), qa_scale3(basis.forward, 16)),
                           qa_scale3(basis.right, (game->random(game->ctx) * 2 - 1) * 100));
  }
}
static void melee_action(qa_q1am_heavy_monster *monster) {
  qa_q1am_monster *m = &monster->base;
  qa_q1am_game *game = m->game;
  qa_q1am_actor *entity = m->entity;
  qa_vec3 target;
  qa_vec3 delta = qa_sub3(qa_q1am_monster_origin(m),
                          qa_q1am_monster_target(m, &target) ? target : QA_Q1AM_ZERO);
  double range = qa_length3(delta);
  if (range < 110) {
    m->ops.play(m, game->random(game->ctx) < 0.8 ? "supsham_smash1"
                   : game->random(game->ctx) < 0.5 ? "supsham_swingl1" : "supsham_swingr1");
    return;
  }
  if (range < 200 || entity->wait > game->time) {
    double chance = qa_dot3(qa_q1am_game_make_vectors(game, game->body(game->ctx, entity).angles).right,
                            qa_q1am_normalize(delta));
    m->ops.play(m, chance < 0.45 ? "supsham_swingr1" : chance < 0.9 ? "supsham_swingl1" : "supsham_smash1");
    return;
  }
  entity->count = 0;
  if (game->random(game->ctx) > 0.4) {
    m->ops.play(m, "supsham_magic1");
    entity->wait = game->time + 5;
  } else {
    m->ops.play(m, "supsham_magic_b1");
    entity->wait = game->time + 3;
  }
}
static void missile_action(qa_q1am_heavy_monster *monster) {
  qa_q1am_monster *m = &monster->base;
  double r = m->game->random(m->game->ctx);
  m->ops.play(m, r < 0.36 ? "supsham_magic1" : r < 0.66 ? "supsham_swingr1" : "supsham_swingl1");
}
static void cleanup_orbs(qa_q1am_heavy_monster *monster) {
  qa_q1am_monster *m = &monster->base;
  qa_q1am_game *game = m->game;
  size_t total = game->entity_count(game->ctx);
  for (size_t i = 0; i < total; i++) {
    qa_q1am_actor *entity = game->entity_at(game->ctx, i);
    if (entity && strcmp(entity->classname, "monster_super_shambler") == 0 && entity->has_owner &&
        qa_same_actor(entity->owner, m->entity->actor.id))
      game->schedule(game->ctx, entity, 0.1,
                     game->named_action(game->ctx, entity, QA_Q1AM_HEAVY_PREFIX ":source_die"));
  }
}
static void smash12(qa_q1am_heavy_monster *monster) {
  qa_q1am_monster *m = &monster->base;
  m->ops.ai(m, QA_Q1AM_AI_CHARGE, 4);
  if (attack_check(monster))
    qa_q1am_monster_set_frame(m->next_frame, sizeof m->next_frame, "supsham_magic1");
}
static void swingl7(qa_q1am_heavy_monster *monster) {
  qa_q1am_monster *m = &monster->base;
  blast(monster, qa_add3(qa_scale3(m->game->basis.right, 16), (qa_vec3){0, 0, 32}), true);
  m->ops.ai(m, QA_Q1AM_AI_CHARGE, 5);
  claw(monster, 250);
}
static void swingr7(qa_q1am_heavy_monster *monster) {
  qa_q1am_monster *m = &monster->base;
  blast(monster, qa_add3(qa_scale3(m->game->basis.right, -16), (qa_vec3){0, 0, 32}), true);
  m->ops.ai(m, QA_Q1AM_AI_CHARGE, 6);
  claw(monster, -250);
}
static void swingl9(qa_q1am_heavy_monster *monster) {
  qa_q1am_monster *m = &monster->base;
  m->ops.ai(m, QA_Q1AM_AI_CHARGE, 8);
  qa_q1am_game_make_vectors(m->game, m->game->body(m->game->ctx, m->entity).angles);
  if (attack_check(monster))
    qa_q1am_monster_set_frame(m->next_frame, sizeof m->next_frame, "supsham_magic1");
  else if (m->game->random(m->game->ctx) < 0.5)
    qa_q1am_monster_set_frame(m->next_frame, sizeof m->next_frame, "supsham_swingr1");
}
static void swingr9(qa_q1am_heavy_monster *monster) {
  qa_q1am_monster *m = &monster->base;
  m->ops.ai(m, QA_Q1AM_AI_CHARGE, 1);
  m->ops.ai(m, QA_Q1AM_AI_CHARGE, 10);
  qa_q1am_game_make_vectors(m->game, m->game->body(m->game->ctx, m->entity).angles);
  if (attack_check(monster))
    qa_q1am_monster_set_frame(m->next_frame, sizeof m->next_frame, "supsham_magic1");
  else if (m->game->random(m->game->ctx) < 0.5)
    qa_q1am_monster_set_frame(m->next_frame, sizeof m->next_frame, "supsham_swingl1");
}
static void magic1(qa_q1am_heavy_monster *monster) {
  qa_q1am_monster *m = &monster->base;
  qa_q1am_monster_face(m);
  m->game->sound(m->game->ctx, m->entity, "shambler/sattck1.wav", "weapon", 1);
  m->entity->count = 0;
}
static const qa_q1am_heavy_action_entry ss_actions[] = {
  {"supsham_removechild", remove_child},
  {"SupCastLightning", cast_lightning},
  {"supsham_melee", melee_action},
  {"supsham_missile", missile_action},
  {"cleanup_orbs", cleanup_orbs},
  {"mg3_super_shambler:supsham_smash10", smash},
  {"mg3_super_shambler:supsham_smash12", smash12},
  {"mg3_super_shambler:supsham_swingl7", swingl7},
  {"mg3_super_shambler:supsham_swingr7", swingr7},
  {"mg3_super_shambler:supsham_swingl9", swingl9},
  {"mg3_super_shambler:supsham_swingr9", swingr9},
  {"mg3_super_shambler:supsham_magic1", magic1},
  {"mg3_super_shambler:supsham_magic3", lightning_child_slow},
  {"mg3_super_shambler:supsham_magic_b3", lightning_child_fast},
  {"mg3_super_shambler:supsham_magic4", child_frame1},
  {"mg3_super_shambler:supsham_magic5", child_frame2},
  {"mg3_super_shambler:supsham_magic4b", child_frame3},
};
static void ss_spawn(qa_q1am_heavy_monster *monster) {
  qa_q1am_monster *m = &monster->base;
  m->game->combat_set_health(m->game->ctx, m->entity->actor, 2000);
  qa_q1am_heavy_monster_set_number(monster, "allowPathFind", 1);
  qa_q1am_heavy_monster_set_number(monster, "combat_style", 3);
  qa_q1am_heavy_monster_set_number(monster, "frags", 0);
  qa_q1am_heavy_monster_initialize(monster, 2);
}
static void ss_pain(qa_q1am_heavy_monster *monster, bool has, qa_actor_id attacker, double damage) {
  qa_q1am_monster *m = &monster->base;
  qa_q1am_game *game = m->game;
  qa_q1am_actor *entity = m->entity;
  double health = game->health(game->ctx, entity->actor.id);
  game->sound(game->ctx, entity, "shambler/shurt2.wav", "voice", 1);
  if (damage >= health && has && game->is_player(game->ctx, true, attacker)) {
    qa_q1am_player *player = game->player(game->ctx, attacker);
    if (player && strcmp(player->weapon, "axe") == 0) {
      qa_q1am_event event = {0};
      event.kind = "achievement";
      event.has_player = true;
      event.player = attacker;
      event.id = "ACH_CLOSE_SHAVE";
      game->emit(game->ctx, &event);
    }
    if (qa_q1am_actor_number(entity, "frags") == 0) {
      qa_q1am_event event = {0};
      event.kind = "achievement";
      event.has_player = true;
      event.player = attacker;
      event.id = "ACH_SHAMBLER_DANCE";
      game->emit(game->ctx, &event);
    }
  }
  if (health <= 0 || 25 + game->random(game->ctx) * 400 > damage || m->state->pain_finished > game->time)
    return;
  m->state->pain_finished = game->time + 5;
  remove_child(monster);
  m->ops.play(m, "supsham_pain1");
}
static void ss_die(qa_q1am_heavy_monster *monster) {
  qa_q1am_monster *m = &monster->base;
  qa_q1am_game *game = m->game;
  qa_q1am_actor *entity = m->entity;
  remove_child(monster);
  double health = game->health(game->ctx, entity->actor.id);
  if (health < -60) {
    game->sound(game->ctx, entity, "player/udeath.wav", "voice", 1);
    game->throw_head(game->ctx, game, entity, "h_shams", true, health);
    static const char *const gibs[] = {"gib1", "gib2", "gib3"};
    for (int i = 0; i < 3; i++)
      game->throw_gib(game->ctx, game, qa_q1am_monster_origin(m), gibs[i], health);
    return;
  }
  game->sound(game->ctx, entity, "shambler/sdeath.wav", "voice", 1);
  m->ops.play(m, "supsham_death1");
}
static const char *const ss_classnames[] = {"monster_super_shambler"};
static const char *const ss_gibs[] = {"gib1", "gib2", "gib3"};
const qa_q1am_heavy_definition qa_q1am_super_shambler_definition = {
  {"shambler", NULL, "shambler_blood", "h_shams", "supsham_stand1", "supsham_walk1", "supsham_run1",
   "", "supsham_melee", "walk", ss_classnames, 1, ss_gibs, 3,
   2000, -60, {{-32, -32, -24}, {32, 32, 64}}, true},
  &qa_q1am_mg3_super_shambler_frames, ss_actions, sizeof ss_actions / sizeof ss_actions[0],
  ss_spawn, ss_pain, ss_die, melee_action, NULL, NULL, NULL, 0,
};
