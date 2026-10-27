/* Port of src/content/q1/missionpacks/monsters/scourge.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/monsters/scourge.h"

#include <math.h>
#include <stdlib.h>

#include "qa/content/q1/missionpacks/monsters/helpers.h"
#include "qa/content/q1/missionpacks/monsters/tables/hipscrge.h"
#include "monsters_internal.h"

static void think_impl(qa_q1mpm_mission_monster *monster) {
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  if (qa_q1mpm_actor_number(entity, "scourge:state") == 0) {
    qa_q1mpm_actor *trigger = game->create(game, "scourge_trigger");
    trigger->solid = "trigger";
    qa_q1mpm_actor_set_damageable(game, trigger, false);
    qa_bounds bounds = {{-64, -64, -24}, {64, 64, 64}};
    game->set_bounds(game, trigger, bounds);
    qa_q1mpm_references_set(trigger, "lastvictim", QA_Q1MPM_SOME(entity->actor.id));
    qa_q1mpm_references_set(entity, "lastvictim", QA_Q1MPM_SOME(trigger->actor.id));
    trigger->touch = game->named.touch(game->named.ctx, trigger, "hipnotic:ScourgeTriggerTouch");
    game->schedule(game, trigger, 0.1 + game->host->random(game->host->ctx),
                   game->named.action(game->named.ctx, trigger, "hipnotic:ScourgeTriggerThink"));
    game->set_origin(game, trigger, qa_q1mpm_monster_origin(monster));
    qa_q1mpm_number(monster, "scourge:state", 1);
  }
  double silent = qa_q1mpm_actor_number(entity, "spawnsilent");
  double multi = qa_q1mpm_actor_number(entity, "spawnmulti");
  if (silent == 0 && multi == 1) game->sound(game, entity, "misc/null.wav", QA_Q1MPM_CH("body"), 2, 1);
  else if (silent == 1 && multi == 0) game->sound(game, entity, "scourge/walk.wav", QA_Q1MPM_CH("body"), 2, 1);
  qa_q1mpm_number(monster, "spawnmulti", silent);
}
static void think_action(void *ctx, qa_q1mpm_mission_monster *monster) { (void)ctx; think_impl(monster); }
static void side_move(qa_q1mpm_mission_monster *monster, bool right, double distance) {
  monster->game->host->walk_move(monster->game->host->ctx, monster->entity->actor,
                                  monster->game->body(monster->game, monster->entity).angles.y + (right ? 90 : 270),
                                  distance);
}
static void fire_at(qa_q1mpm_mission_monster *monster, double offset) {
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  qa_q1mpm_monster_face(monster);
  qa_vec3 target;
  if (!qa_q1mpm_monster_target(monster, &target)) return;
  qa_q1mpm_basis basis = game->make_vectors(game, game->body(game, entity).angles);
  qa_vec3 origin = qa_q1mpm_vadd(qa_q1mpm_vadd(qa_q1mpm_vadd(qa_q1mpm_monster_origin(monster), QA_Q1MPM_V3(0, 0, -19)),
                                              qa_q1mpm_vscale(basis.right, offset)),
                                 qa_q1mpm_vscale(basis.forward, 14));
  game->sound(game, entity, "weapons/rocket1i.wav", QA_Q1MPM_CH("weapon"), 1, 1);
  qa_vec3 velocity = qa_q1mpm_vscale(
      qa_q1mpm_normalize(qa_q1mpm_vsub(qa_q1mpm_vadd(target, qa_q1mpm_vscale(basis.forward, 200)), origin)), 1000);
  qa_q1mpm_ext_launch_spike(game, QA_Q1MPM_SOME(entity->actor.id), origin, velocity);
  monster->state.attack_finished = game->time + 0.2;
}
static void flash(qa_q1mpm_mission_monster *monster, double offset) {
  monster->entity->effects |= 2;
  fire_at(monster, offset);
}
static void tail(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1mpm_monster_face(monster);
  if (!monster->state.enemy.has || qa_q1mpm_monster_distance(monster) > 100 ||
      !monster->game->can_damage(monster->game, monster->state.enemy.id, monster->entity->actor.id))
    return;
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  const qa_q1mpm_host *host = game->host;
  qa_q1mpm_game_damage(game, monster->state.enemy.id, entity->actor.id, QA_Q1MPM_SOME(entity->actor.id),
                       (host->random(host->ctx) + host->random(host->ctx) + host->random(host->ctx)) * 40);
  game->sound(game, entity, "shambler/smack.wav", QA_Q1MPM_CH("weapon"), 1, 1);
  qa_q1mpm_basis basis = game->basis;
  qa_vec3 spray = qa_q1mpm_vscale(basis.right, (game->host->random(game->host->ctx) * 2 - 1) * 50);
  qa_q1mpm_ext_spawn_meat_spray(game, entity,
                                qa_q1mpm_vadd(qa_q1mpm_monster_origin(monster), qa_q1mpm_vscale(basis.forward, 16)),
                                spray);
}
static void turn_impl(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1mpm_monster_delay(monster, 0.1);
  qa_vec3 target;
  if (!qa_q1mpm_monster_target(monster, &target)) return;
  if (fabs(monster->game->body(monster->game, monster->entity).angles.y -
           qa_q1mpm_yaw_for(qa_q1mpm_vsub(target, qa_q1mpm_monster_origin(monster)))) > 10) {
    qa_q1mpm_monster_face(monster);
    return;
  }
  qa_q1mpm_monster_set_next_frame(monster, qa_q1mpm_monster_spec(monster)->run);
}
static bool check_attack(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  qa_vec3 target;
  bool has_target = monster->state.enemy.has && qa_q1mpm_eye(game, monster->state.enemy.id, &target);
  if (!has_target) return false;
  qa_vec3 start = qa_q1mpm_vadd(qa_q1mpm_monster_origin(monster), QA_Q1MPM_V3(0, 0, 25));
  qa_vec3 delta = qa_q1mpm_vsub(target, start);
  double distance = qa_q1mpm_length(delta);
  if (distance <= 100 && monster->state.enemy.has &&
      game->can_damage(game, monster->state.enemy.id, entity->actor.id)) {
    entity->attack_state = "melee";
    return true;
  }
  if (game->time < monster->state.attack_finished || !qa_q1mpm_monster_visible(monster) ||
      delta.z > 64 || delta.z < -200 || distance > 1000 || distance < 150)
    return false;
  qa_q1mpm_trace_request request = {start, target, QA_Q1MPM_POINT, QA_Q1MPM_SOME(entity->actor.id), true, false};
  qa_q1mpm_trace trace = game->host->trace(game->host->ctx, &request);
  if (!qa_q1mpm_opt_eq(trace.actor, monster->state.enemy) || (trace.in_open && trace.in_water)) return false;
  entity->attack_state = "missile";
  qa_q1mpm_monster_attack_finished(monster, 2 + 2 * game->host->random(game->host->ctx));
  return true;
}
static void trigger_think(void *ctx, qa_q1mpm_game *game, qa_q1mpm_actor *entity) {
  (void)ctx;
  qa_q1mpm_opt_actor owner = qa_q1mpm_references_get(entity, "lastvictim");
  qa_q1mpm_actor *target = game->entity(game, owner);
  if (target == NULL || game->health(game, target->actor.id) <= 0) { game->remove(game, entity); return; }
  qa_q1mpm_body body = game->body(game, target);
  game->set_origin(game, entity,
                   qa_q1mpm_vadd(body.origin, qa_q1mpm_vscale(game->make_vectors(game, body.angles).forward, 300)));
  game->schedule(game, entity, 0.1, game->named.action(game->named.ctx, entity, "hipnotic:ScourgeTriggerThink"));
}
static void trigger_touch(void *ctx, qa_q1mpm_game *game, qa_q1mpm_actor *entity, qa_actor_id other,
                          bool has_normal, qa_vec3 normal, const void *surface) {
  (void)has_normal; (void)normal; (void)surface;
  qa_q1mpm_runtime *runtime = ctx;
  qa_q1mpm_actor *shot = game->entity(game, QA_Q1MPM_SOME(other));
  qa_q1mpm_opt_actor owner = qa_q1mpm_references_get(entity, "lastvictim");
  qa_q1mpm_mission_monster *monster = owner.has ? qa_q1mpm_runtime_context(runtime, owner.id) : NULL;
  if (shot == NULL || (shot->movement_flags & (32 | 8)) != 0 || game->is_player(game, QA_Q1MPM_SOME(other)) ||
      !qa_q1mpm_str_eq(shot->movement, "flymissile"))
    return;
  if (monster == NULL || game->health(game, monster->entity->actor.id) <= 0) {
    game->remove(game, entity);
    return;
  }
  qa_q1mpm_body body = game->body(game, shot);
  if (qa_q1mpm_dot(qa_q1mpm_normalize(qa_q1mpm_vsub(qa_q1mpm_monster_origin(monster), body.origin)),
                   qa_q1mpm_normalize(body.velocity)) < 0.8)
    return;
  if (game->time > qa_q1mpm_actor_number(entity, "duration")) {
    qa_q1mpm_monster_play(monster, game->host->random(game->host->ctx) < 0.5 ? "scourge_strafeleft1"
                                                                              : "scourge_straferight1");
    qa_q1mpm_number(monster, "duration", game->time + 1.5);
  }
}
static void stand1(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1mpm_number(monster, "spawnsilent", 0);
  qa_q1mpm_monster_ai(monster, QA_Q1MPM_AI_STAND, 0);
  think_impl(monster);
}
static void walk1(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  if (monster->game->host->random(monster->game->host->ctx) < 0.1)
    monster->game->sound(monster->game, monster->entity, "scourge/idle.wav", QA_Q1MPM_CH("voice"), 2, 1);
  qa_q1mpm_number(monster, "spawnsilent", 1);
  think_impl(monster);
  qa_q1mpm_monster_ai(monster, QA_Q1MPM_AI_WALK, 8);
}
static void run1(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  if (monster->game->host->random(monster->game->host->ctx) < 0.1)
    monster->game->sound(monster->game, monster->entity, "scourge/idle.wav", QA_Q1MPM_CH("voice"), 2, 1);
  qa_q1mpm_number(monster, "spawnsilent", 1);
  think_impl(monster);
  qa_q1mpm_monster_ai(monster, QA_Q1MPM_AI_RUN, 18);
}
static void strafeleft1(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1mpm_number(monster, "spawnsilent", 1);
  think_impl(monster);
  side_move(monster, false, 20);
}
static void straferight1(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1mpm_number(monster, "spawnsilent", 1);
  think_impl(monster);
  side_move(monster, true, 20);
}
static void turn1(void *ctx, qa_q1mpm_mission_monster *monster) {
  qa_q1mpm_number(monster, "spawnsilent", 1);
  think_impl(monster);
  turn_impl(ctx, monster);
}
static void left20(void *ctx, qa_q1mpm_mission_monster *monster) { (void)ctx; side_move(monster, false, 20); }
static void left14(void *ctx, qa_q1mpm_mission_monster *monster) { (void)ctx; side_move(monster, false, 14); }
static void right20(void *ctx, qa_q1mpm_mission_monster *monster) { (void)ctx; side_move(monster, true, 20); }
static void right14(void *ctx, qa_q1mpm_mission_monster *monster) { (void)ctx; side_move(monster, true, 14); }
static void atk1(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1mpm_number(monster, "spawnsilent", 0);
  think_impl(monster);
  flash(monster, 40);
}
static void atk2(void *ctx, qa_q1mpm_mission_monster *monster) { (void)ctx; flash(monster, -56); }
static void atk3(void *ctx, qa_q1mpm_mission_monster *monster) { (void)ctx; flash(monster, -40); }
static void atk4(void *ctx, qa_q1mpm_mission_monster *monster) { (void)ctx; flash(monster, 56); }
static void atk5(void *ctx, qa_q1mpm_mission_monster *monster) { (void)ctx; flash(monster, 40); }
static void atk8(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  flash(monster, 56);
  qa_q1mpm_monster_attack_finished(monster, 4 * monster->game->host->random(monster->game->host->ctx));
}
static void melee1_action(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1mpm_number(monster, "spawnsilent", 0);
  think_impl(monster);
  qa_q1mpm_monster_ai(monster, QA_Q1MPM_AI_CHARGE, 3);
}
static void melee11(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1mpm_monster_face(monster);
  if (monster->game->options.skill == 3 && !monster->state.refired && qa_q1mpm_monster_visible(monster)) {
    monster->state.refired = true;
    qa_q1mpm_monster_set_next_frame(monster, "scourge_melee1");
  }
}
static void pain1_action(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1mpm_number(monster, "spawnsilent", 0);
  think_impl(monster);
}
static void spawn(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1mpm_fields_set(monster->entity, "yaw_speed", "60");
  qa_q1mpm_number(monster, "scourge:state", 0);
  monster->entity->attack_state = "dodging";
  qa_q1mpm_monster_spawn_default(monster);
}
static void melee_action(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1mpm_monster_play(monster, "scourge_melee1");
  qa_q1mpm_monster_attack_finished(monster, 2 * monster->game->host->random(monster->game->host->ctx));
}
static void pain(void *ctx, qa_q1mpm_mission_monster *monster, qa_q1mpm_opt_actor attacker, double damage) {
  (void)ctx; (void)attacker;
  if (monster->game->host->random(monster->game->host->ctx) * 50 > damage ||
      monster->state.pain_finished > monster->game->time)
    return;
  monster->game->host->random(monster->game->host->ctx);
  qa_q1mpm_game_sound(monster->game, monster->entity, "scourge/pain.wav");
  monster->state.pain_finished = monster->game->time + 2;
  qa_q1mpm_monster_play(monster, "scourge_pain1");
}
static void die(void *ctx, qa_q1mpm_mission_monster *monster, qa_q1mpm_opt_actor attacker) {
  (void)ctx; (void)attacker;
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  qa_q1mpm_actor *trigger = game->entity(game, qa_q1mpm_references_get(entity, "lastvictim"));
  if (trigger != NULL) game->remove(game, trigger);
  qa_q1mpm_number(monster, "spawnsilent", 0);
  think_impl(monster);
  if (game->health(game, entity->actor.id) < -35) {
    qa_q1mpm_game_sound(game, entity, "player/udeath.wav");
    qa_q1mpm_ext_throw_head(game, entity, "h_scourg");
    static const char *const models[] = {"gib1", "gib2", "gib3"};
    for (size_t i = 0; i < 3; i++)
      qa_q1mpm_ext_throw_gib(game, qa_q1mpm_monster_origin(monster), models[i],
                             game->health(game, entity->actor.id));
    return;
  }
  qa_q1mpm_game_sound(game, entity, "scourge/pain2.wav");
  qa_q1mpm_monster_play(monster, "scourge_die1");
}
static const char *const classnames[] = {"monster_scourge"};
static const char *const spec_gibs[] = {"gib1", "gib2", "gib3"};
static const qa_q1mpm_named_action actions[] = {
    {"scourge_think", {think_action, NULL}},
    {"hipscrge:scourge_stand1", {stand1, NULL}},
    {"hipscrge:scourge_walk1", {walk1, NULL}},
    {"hipscrge:scourge_run1", {run1, NULL}},
    {"hipscrge:scourge_strafeleft1", {strafeleft1, NULL}},
    {"hipscrge:scourge_straferight1", {straferight1, NULL}},
    {"hipscrge:scourge_turn1", {turn1, NULL}},
    {"ai_left(20)", {left20, NULL}},
    {"ai_left(14)", {left14, NULL}},
    {"ai_right(20)", {right20, NULL}},
    {"ai_right(14)", {right14, NULL}},
    {"ai_turn_in_place", {turn_impl, NULL}},
    {"hipscrge:scourge_atk1", {atk1, NULL}},
    {"hipscrge:scourge_atk2", {atk2, NULL}},
    {"hipscrge:scourge_atk3", {atk3, NULL}},
    {"hipscrge:scourge_atk4", {atk4, NULL}},
    {"hipscrge:scourge_atk5", {atk5, NULL}},
    {"hipscrge:scourge_atk8", {atk8, NULL}},
    {"hipscrge:scourge_melee1", {melee1_action, NULL}},
    {"hipscrge:scourge_melee11", {melee11, NULL}},
    {"hipscrge:scourge_pain1", {pain1_action, NULL}},
    {"Attack_With_Tail", {tail, NULL}},
};
void qa_q1mpm_scourge_definition(qa_q1mpm_runtime *runtime, qa_q1mpm_pack_monster_definition *out) {
  *out = (qa_q1mpm_pack_monster_definition){0};
  out->spec = (qa_q1mpm_species_spec){
      "scourge", NULL, classnames, 1, "scor", "h_scourg", 300, -35, spec_gibs, 3,
      {{-32, -32, -24}, {32, 32, 64}}, "scourge_stand1", "scourge_walk1", "scourge_run1",
      "scourge/sight.wav", "scourge_atk1", true, "walk"};
  out->frames = (qa_q1mpm_frame_table){qa_q1mpm_tables_hipscrge_entries, QA_Q1MPM_TABLES_HIPSCRGE_COUNT};
  out->actions = actions;
  out->action_count = sizeof(actions) / sizeof(actions[0]);
  qa_q1mpm_named_handlers *callbacks = calloc(2, sizeof(*callbacks));
  if (callbacks != NULL) {
    callbacks[0].name = "ScourgeTriggerThink";
    callbacks[0].handlers = (qa_q1mpm_handlers){NULL, trigger_think, NULL, NULL, NULL, NULL};
    callbacks[1].name = "ScourgeTriggerTouch";
    callbacks[1].handlers = (qa_q1mpm_handlers){runtime, NULL, trigger_touch, NULL, NULL, NULL};
    out->callbacks = callbacks;
    out->callback_count = 2;
  }
  out->spawn = (qa_q1mpm_action){spawn, NULL};
  out->pain = pain;
  out->die = die;
  out->melee = (qa_q1mpm_action){melee_action, NULL};
  out->check_attack = check_attack;
}
