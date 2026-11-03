/* Port of src/content/q1/missionpacks/monsters/scourge.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/monsters/scourge.h"

#include <math.h>

#include "qa/content/q1/missionpacks/monsters/helpers.h"
#include "qa/content/q1/missionpacks/monsters/tables/hipscrge.h"
#include "monsters_internal.h"

static void think_impl(qa_q1mpm_mission_monster *monster) {
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  if (qa_q1_actor_number(entity, "scourge:state") == 0) {
    qa_q1_actor *trigger = qa_q1_create(game, "scourge_trigger", NULL, NULL);
    trigger->solid = "trigger";
    qa_q1_actor_set_damageable(trigger, false);
    qa_bounds bounds = {{-64, -64, -24}, {64, 64, 64}};
    qa_q1_set_bounds(game, trigger, bounds);
    (void)qa_q1_actor_reference_set(trigger, "lastvictim", &entity->actor.id);
    (void)qa_q1_actor_reference_set(entity, "lastvictim", &trigger->actor.id);
    trigger->touch = qa_q1_callback_registry_touch(game->named, trigger, "hipnotic:ScourgeTriggerTouch");
    const double delay = 0.1 + qa_q1_host_random(game);
    qa_q1_schedule(game, trigger, delay, qa_q1_callback_registry_action(game->named, trigger, "hipnotic:ScourgeTriggerThink"));
    qa_q1_set_origin(game, trigger, qa_q1_base_monster_origin(&monster->base));
    qa_q1mpm_number(monster, "scourge:state", 1);
  }
  double silent = qa_q1_actor_number(entity, "spawnsilent");
  double multi = qa_q1_actor_number(entity, "spawnmulti");
  if (silent == 0 && multi == 1) qa_q1_sound(game, entity->actor.id, "misc/null.wav", "body", 2, 1);
  else if (silent == 1 && multi == 0) qa_q1_sound(game, entity->actor.id, "scourge/walk.wav", "body", 2, 1);
  qa_q1mpm_number(monster, "spawnmulti", silent);
}
static void think_action(void *ctx, qa_q1mpm_mission_monster *monster) { (void)ctx; think_impl(monster); }
static void side_move(qa_q1mpm_mission_monster *monster, bool right, double distance) {
  (void)monster->base.game->host.walk_move(monster->base.game->host.ctx, monster->base.entity->actor,
                                           qa_q1_get_body(monster->base.game, monster->base.entity).angles.y + (right ? 90 : 270),
                                           distance);
}
static void fire_at(qa_q1mpm_mission_monster *monster, double offset) {
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  qa_q1_base_monster_face(&monster->base);
  qa_vec3 target;
  if (!qa_q1_base_monster_target(&monster->base, &target)) return;
  qa_q1_basis basis = qa_q1_make_vectors(game, qa_q1_get_body(game, entity).angles);
  qa_vec3 origin = qa_q1_vadd(qa_q1_vadd(qa_q1_vadd(qa_q1_base_monster_origin(&monster->base), (qa_vec3){0, 0, -19}),
                                              qa_q1_vscale(basis.right, offset)),
                                 qa_q1_vscale(basis.forward, 14));
  qa_q1_sound(game, entity->actor.id, "weapons/rocket1i.wav", "weapon", 1, 1);
  qa_vec3 velocity = qa_q1_vscale(
      qa_q1_normalize(qa_q1_vsub(qa_q1_vadd(target, qa_q1_vscale(basis.forward, 200)), origin)), 1000);
  (void)qa_q1_launch_spike(game, &entity->actor.id, origin, velocity, "spike");
  monster->base.state->attack_finished = game->time + 0.2;
}
static void flash(qa_q1mpm_mission_monster *monster, double offset) {
  monster->base.entity->effects = qa_js_bit_or(monster->base.entity->effects, 2);
  fire_at(monster, offset);
}
static void tail(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1_base_monster_face(&monster->base);
  if (!monster->base.state->has_enemy || qa_q1_base_monster_distance(&monster->base) > 100 ||
      !qa_q1_can_damage(monster->base.game, monster->base.state->enemy, monster->base.entity->actor.id))
    return;
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  const double first = qa_q1_host_random(game);
  const double second = qa_q1_host_random(game);
  const double amount = (first + second + qa_q1_host_random(game)) * 40;
  (void)qa_q1_damage(game, monster->base.state->enemy, entity->actor.id, &entity->actor.id, amount, NULL, NULL, NULL, NULL);
  qa_q1_sound(game, entity->actor.id, "shambler/smack.wav", "weapon", 1, 1);
  qa_q1_basis basis = game->basis;
  qa_vec3 spray = qa_q1_vscale(basis.right, (qa_q1_host_random(game) * 2 - 1) * 50);
  (void)qa_q1_spawn_meat_spray(game, entity,
                               qa_q1_vadd(qa_q1_base_monster_origin(&monster->base), qa_q1_vscale(basis.forward, 16)),
                               spray);
}
static void turn_impl(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1_base_monster_delay(&monster->base, 0.1);
  qa_vec3 target;
  if (!qa_q1_base_monster_target(&monster->base, &target)) return;
  double left = qa_q1_get_body(monster->base.game, monster->base.entity).angles.y;
  double right = qa_q1_yaw_for(qa_q1_vsub(target, qa_q1_base_monster_origin(&monster->base)));
  if (fabs(left - right) > 10) {
    qa_q1_base_monster_face(&monster->base);
    return;
  }
  q1mpm_set_next_frame(&monster->base, monster->base.spec->run);
}
static bool check_attack(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  qa_vec3 target;
  bool has_target = monster->base.state->has_enemy && qa_q1mpm_eye(game, monster->base.state->enemy, &target);
  if (!has_target) return false;
  qa_vec3 start = qa_q1_vadd(qa_q1_base_monster_origin(&monster->base), (qa_vec3){0, 0, 25});
  qa_vec3 delta = qa_q1_vsub(target, start);
  double distance = qa_q1_length(delta);
  if (distance <= 100 && monster->base.state->has_enemy &&
      qa_q1_can_damage(game, monster->base.state->enemy, entity->actor.id)) {
    entity->attack_state = "melee";
    return true;
  }
  if (game->time < monster->base.state->attack_finished || !qa_q1_base_monster_visible(&monster->base, qa_q1_base_monster_enemy(&monster->base)) ||
      delta.z > 64 || delta.z < -200 || distance > 1000 || distance < 150)
    return false;
  const qa_q1_trace trace = q1mpm_trace(game, start, target, QA_Q1_POINT, &entity->actor.id, true);
  if (!q1mpm_same_opt(trace.has_actor ? &trace.actor : NULL, qa_q1_base_monster_enemy(&monster->base)) ||
      (trace.in_open && trace.in_water))
    return false;
  entity->attack_state = "missile";
  qa_q1_base_monster_attack_finished(&monster->base, 2 + 2 * qa_q1_host_random(game));
  return true;
}
static void trigger_think(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  qa_actor_id owner_id;
  const qa_actor_id *owner = q1mpm_reference(entity, "lastvictim", &owner_id);
  qa_q1_actor *target = owner == NULL ? NULL : qa_q1_get_entity(game, owner);
  if (target == NULL || qa_q1_health(game, target->actor.id) <= 0) { qa_q1_remove(game, entity); return; }
  qa_q1_body body = qa_q1_get_body(game, target);
  qa_q1_set_origin(game, entity,
                   qa_q1_vadd(body.origin, qa_q1_vscale(qa_q1_make_vectors(game, body.angles).forward, 300)));
  qa_q1_schedule(game, entity, 0.1, qa_q1_callback_registry_action(game->named, entity, "hipnotic:ScourgeTriggerThink"));
}
static void trigger_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other,
                const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)normal; (void)surface;
  qa_q1mpm_runtime *runtime = ctx;
  qa_q1_actor *shot = qa_q1_get_entity(game, &other);
  qa_actor_id owner;
  qa_q1mpm_mission_monster *monster =
      qa_q1_actor_reference_get(entity, "lastvictim", &owner) ? qa_q1mpm_runtime_context(runtime, owner) : NULL;
  if (shot == NULL || qa_js_bit_and(shot->movement_flags, 32 | 8) != 0 || qa_q1_is_player(game, &other) ||
      !q1mpm_str_eq(shot->movement, "flymissile"))
    return;
  if (monster == NULL || qa_q1_health(game, monster->base.entity->actor.id) <= 0) {
    qa_q1_remove(game, entity);
    return;
  }
  qa_q1_body body = qa_q1_get_body(game, shot);
  if (qa_q1_dot(qa_q1_normalize(qa_q1_vsub(qa_q1_base_monster_origin(&monster->base), body.origin)),
                   qa_q1_normalize(body.velocity)) < 0.8)
    return;
  if (game->time > qa_q1_actor_number(entity, "duration")) {
    qa_q1_base_monster_play(&monster->base, qa_q1_host_random(game) < 0.5 ? "scourge_strafeleft1"
                                                                              : "scourge_straferight1");
    qa_q1mpm_number(monster, "duration", game->time + 1.5);
  }
}
static void stand1(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1mpm_number(monster, "spawnsilent", 0);
  qa_q1_base_monster_ai(&monster->base, QA_Q1_AI_STAND, 0);
  think_impl(monster);
}
static void walk1(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  if (qa_q1_host_random(monster->base.game) < 0.1)
    qa_q1_sound(monster->base.game, monster->base.entity->actor.id, "scourge/idle.wav", "voice", 2, 1);
  qa_q1mpm_number(monster, "spawnsilent", 1);
  think_impl(monster);
  qa_q1_base_monster_ai(&monster->base, QA_Q1_AI_WALK, 8);
}
static void run1(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  if (qa_q1_host_random(monster->base.game) < 0.1)
    qa_q1_sound(monster->base.game, monster->base.entity->actor.id, "scourge/idle.wav", "voice", 2, 1);
  qa_q1mpm_number(monster, "spawnsilent", 1);
  think_impl(monster);
  qa_q1_base_monster_ai(&monster->base, QA_Q1_AI_RUN, 18);
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
  qa_q1_base_monster_attack_finished(&monster->base, 4 * qa_q1_host_random(monster->base.game));
}
static void melee1_action(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1mpm_number(monster, "spawnsilent", 0);
  think_impl(monster);
  qa_q1_base_monster_ai(&monster->base, QA_Q1_AI_CHARGE, 3);
}
static void melee11(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1_base_monster_face(&monster->base);
  if (qa_q1_option_skill(monster->base.game) == 3 && !monster->base.state->refired && qa_q1_base_monster_visible(&monster->base, qa_q1_base_monster_enemy(&monster->base))) {
    monster->base.state->refired = true;
    q1mpm_set_next_frame(&monster->base, "scourge_melee1");
  }
}
static void pain1_action(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1mpm_number(monster, "spawnsilent", 0);
  think_impl(monster);
}
static void spawn(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  (void)qa_q1_actor_field_set(monster->base.entity, "yaw_speed", "60");
  qa_q1mpm_number(monster, "scourge:state", 0);
  monster->base.entity->attack_state = "dodging";
  qa_q1mpm_monster_spawn_default(monster);
}
static void melee_action(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1_base_monster_play(&monster->base, "scourge_melee1");
  qa_q1_base_monster_attack_finished(&monster->base, 2 * qa_q1_host_random(monster->base.game));
}
static void pain(void *ctx, qa_q1mpm_mission_monster *monster, const qa_actor_id *attacker, double damage) {
  (void)ctx; (void)attacker;
  if (qa_q1_host_random(monster->base.game) * 50 > damage ||
      monster->base.state->pain_finished > monster->base.game->time)
    return;
  (void)qa_q1_host_random(monster->base.game);
  qa_q1_sound_defaults(monster->base.game, monster->base.entity->actor.id, "scourge/pain.wav");
  monster->base.state->pain_finished = monster->base.game->time + 2;
  qa_q1_base_monster_play(&monster->base, "scourge_pain1");
}
static void die(void *ctx, qa_q1mpm_mission_monster *monster, const qa_actor_id *attacker) {
  (void)ctx; (void)attacker;
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  qa_actor_id trigger_id;
  qa_q1_actor *trigger = qa_q1_get_entity(game, q1mpm_reference(entity, "lastvictim", &trigger_id));
  if (trigger != NULL) qa_q1_remove(game, trigger);
  qa_q1mpm_number(monster, "spawnsilent", 0);
  think_impl(monster);
  if (qa_q1_health(game, entity->actor.id) < -35) {
    qa_vec3 origin;
    const char * model;
    double damage;
    qa_q1_sound_defaults(game, entity->actor.id, "player/udeath.wav");
    qa_q1_throw_head(game, entity, "h_scourg");
    static const char *const models[] = {"gib1", "gib2", "gib3"};
    for (size_t i = 0; i < 3; i++)
      (void)(origin = qa_q1_base_monster_origin(&monster->base), model = models[i], damage = qa_q1_health(game, entity->actor.id), qa_q1_throw_gib(game, origin, model,
                            damage));
    return;
  }
  qa_q1_sound_defaults(game, entity->actor.id, "scourge/pain2.wav");
  qa_q1_base_monster_play(&monster->base, "scourge_die1");
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
  out->spec = (qa_q1_base_monster_species){
      "scourge", NULL, classnames, 1, "scor", "h_scourg", 300, -35, spec_gibs, 3,
      {{-32, -32, -24}, {32, 32, 64}}, "scourge_stand1", "scourge_walk1", "scourge_run1",
      "scourge/sight.wav", "scourge_atk1", true, "walk"};
  out->frames = (qa_q1_monster_frame_table){qa_q1mpm_tables_hipscrge_entries, QA_Q1MPM_TABLES_HIPSCRGE_COUNT};
  out->actions = actions;
  out->action_count = sizeof(actions) / sizeof(actions[0]);
  qa_q1mpm_named_handlers *callbacks = qa_q1mpm_runtime_calloc(runtime, 2, sizeof *callbacks);
  callbacks[0].name = "ScourgeTriggerThink";
  callbacks[0].handlers = (qa_q1_callback_handlers){.action = trigger_think};
  callbacks[1].name = "ScourgeTriggerTouch";
  callbacks[1].handlers = (qa_q1_callback_handlers){.ctx = runtime, .touch = trigger_touch};
  out->callbacks = callbacks;
  out->callback_count = 2;
  out->spawn = (qa_q1mpm_action){spawn, NULL};
  out->pain = pain;
  out->die = die;
  out->melee = (qa_q1mpm_action){melee_action, NULL};
  out->check_attack = check_attack;
}
