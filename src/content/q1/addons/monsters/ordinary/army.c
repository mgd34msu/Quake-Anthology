/* Port of src/content/q1/addons/monsters/ordinary/army.ts. GPL-2.0-or-later. */
#include "qa/content/q1/addons/monsters/ordinary/army.h"

#include <string.h>

static const char *const army_classnames[] = {"monster_army"};
static const char *const army_gibs[] = {"gib1", "gib2", "gib3"};
const qa_q1am_species qa_q1am_army_species = {
  "army", NULL, "soldier", "h_guard", "army_stand1", "army_walk1", "army_run1",
  "soldier/sight1.wav", "army_atk1", "walk", army_classnames, 1, army_gibs, 3,
  30, -35, {{-16, -16, -24}, {16, 16, 40}}, false,
};

void qa_q1am_army_fire_action(qa_q1am_monster *monster) {
  qa_q1am_game *game = monster->game;
  qa_q1am_actor *entity = monster->entity;
  qa_q1am_body target;
  if (!monster->state->has_enemy || !game->body_read(game->ctx, monster->state->enemy, &target)) return;
  qa_q1am_monster_face(monster);
  game->sound(game->ctx, entity, "soldier/sattck1.wav", "weapon", 1);
  game->fire_bullets(game->ctx, game, entity->actor,
                     qa_q1am_normalize(qa_sub3(qa_sub3(target.origin, qa_scale3(target.velocity, 0.2)),
                                               qa_q1am_monster_origin(monster))),
                     game->body(game->ctx, entity).angles, 4, 0.1, 0.1, NULL);
  entity->effects = (double)((int32_t)entity->effects | 2);
}
void qa_q1am_army_refire_action(qa_q1am_monster *monster) {
  if (monster->game->options.skill == 3 && !monster->state->refired &&
      qa_q1am_monster_visible(monster, false, (qa_actor_id){0})) {
    monster->state->refired = true;
    qa_q1am_monster_set_frame(monster->next_frame, sizeof monster->next_frame, "army_atk1");
  }
}
static void army_fire_entry(void *ctx, qa_q1am_monster *monster) {
  (void)ctx;
  qa_q1am_army_fire_action(monster);
}
static void army_refire_entry(void *ctx, qa_q1am_monster *monster) {
  (void)ctx;
  qa_q1am_army_refire_action(monster);
}
const qa_q1am_action_entry qa_q1am_army_actions[] = {
  {"army_fire", NULL, army_fire_entry},
  {"army_refire", NULL, army_refire_entry},
};
const size_t qa_q1am_army_action_count = sizeof qa_q1am_army_actions / sizeof qa_q1am_army_actions[0];

bool qa_q1am_army_attack(qa_q1am_monster *monster) {
  qa_q1am_game *game = monster->game;
  qa_q1am_actor *entity = monster->entity;
  if (!monster->state->has_enemy) return false;
  double range = game->world ? qa_q1am_actor_number(game->world, "enemy_range") : 0;
  qa_vec3 start, end;
  if (!qa_q1am_monster_eye(monster, entity->actor.id, &start) ||
      !qa_q1am_monster_eye(monster, monster->state->enemy, &end))
    return false;
  qa_q1am_trace_request request = {start, end, QA_Q1AM_POINT, true, entity->actor.id, true};
  qa_q1am_trace trace = game->trace(game->ctx, &request);
  if (!trace.has_actor || !qa_same_actor(trace.actor, monster->state->enemy) ||
      (trace.in_open && trace.in_water) || range == 3 || game->time < monster->state->attack_finished ||
      game->random(game->ctx) >= (range == 0 ? 0.9 : range == 1 ? 0.4 : range == 2 ? 0.05 : 0))
    return false;
  monster->ops.play(monster, "army_atk1");
  monster->ops.attack_finished(monster, 1 + game->random(game->ctx));
  if (game->random(game->ctx) < 0.3) {
    monster->lefty = !monster->lefty;
    qa_q1am_actor_set_field(entity, "lefty", monster->lefty ? "1" : "0");
  }
  return true;
}
void qa_q1am_army_pain(qa_q1am_monster *monster, bool has_attacker, qa_actor_id attacker,
                       double damage, bool nightmare_resistance) {
  qa_q1am_monster_retaliate(monster, has_attacker, attacker);
  qa_q1am_game *game = monster->game;
  qa_q1am_actor *entity = monster->entity;
  qa_q1am_state *state = monster->state;
  if (state->pain_finished > game->time ||
      (nightmare_resistance && game->options.skill > 2 && game->random(game->ctx) * 100 > damage))
    return;
  double r = game->random(game->ctx);
  state->pain_finished = game->time + (r < 0.2 ? 0.6 : 1.1);
  monster->ops.play(monster, r < 0.2 ? "army_pain1" : r < 0.6 ? "army_painb1" : "army_painc1");
  game->sound(game->ctx, entity, r < 0.2 ? "soldier/pain1.wav" : "soldier/pain2.wav", "voice", 1);
}
