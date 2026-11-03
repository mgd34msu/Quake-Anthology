/* Port of src/content/q1/addons/monsters/heavy/lava-man.ts.
 * quakec_mg3/monsters/mg3_lavaman.qc. GPL-2.0-or-later. */
#include "qa/content/q1/addons/monsters/heavy/lava_man.h"

#include <math.h>
#include <string.h>

#include "qa/content/q1/addons/monsters/heavy/tables/mg3_lavaman.h"
#include "qa/content/q1/base/projectiles.h"
#include "qa/content/q1/missionpacks/types.h"
#include "../support.h"

#define PREFIX QA_Q1AM_HEAVY_PREFIX
#define FLAGS QA_Q1AM_FLAGS

static qa_q1_base_monster *base_of(qa_q1am_heavy_monster *monster) { return &monster->base.base; }
static qa_vec3 target_or_zero(qa_q1_base_monster *monster) {
  qa_vec3 target = QA_Q1_ZERO;
  (void)qa_q1_base_monster_target(monster, &target);
  return target;
}
static bool check_attack(qa_q1am_heavy_monster *heavy) {
  qa_q1_base_monster *monster = base_of(heavy);
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  qa_q1_base_monster_face(monster);
  qa_vec3 start = qa_add3(qa_q1_base_monster_origin(monster), (qa_vec3){0, 0, 64});
  qa_vec3 end = target_or_zero(monster);
  const qa_q1_trace trace = qa_q1am_trace(game, start,
                                          end, QA_Q1_POINT, &entity->actor.id, true);
  if ((trace.in_open && trace.in_water) || !monster->state->has_enemy || !trace.has_actor ||
      !qa_same_actor(trace.actor, monster->state->enemy) || game->time < monster->state->attack_finished)
    return false;
  qa_q1_base_monster_play(monster, "lavaman_fire1");
  qa_q1_base_monster_attack_finished(monster, 1 + qa_q1am_random(game));
  return true;
}
static void hunt(qa_q1am_heavy_monster *heavy) {
  qa_q1_base_monster *monster = base_of(heavy);
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  if (!monster->state->has_enemy || qa_q1_health(game, monster->state->enemy) <= 0) {
    size_t count = 0;
    const qa_actor_observation *actors = qa_session_actor_registry_observations(game->host.actors, &count);
    size_t start = 0; /* current + 1, current = -1 when absent */
    if (monster->state->has_enemy)
      for (size_t i = 0; i < count; i++)
        if (qa_same_actor(actors[i].id, monster->state->enemy)) { start = i + 1; break; }
    qa_actor_id player;
    bool has_player = false;
    for (size_t i = start; i < count && !has_player; i++)
      if (qa_q1_is_player(game, &actors[i].id)) { player = actors[i].id; has_player = true; }
    if (!has_player && game->world != NULL) { player = game->world->actor.id; has_player = true; }
    qa_q1_body body;
    const qa_vec3 target = has_player && qa_q1am_read_body(game, player, &body) ? body.origin : QA_Q1_ZERO;
    const qa_q1_trace trace = qa_q1am_trace(game, qa_add3(qa_q1_base_monster_origin(monster), (qa_vec3){0, 0, 96}), target,
                                            QA_Q1_POINT, game->world == NULL ? NULL : &game->world->actor.id, false);
    if (trace.fraction == 1) qa_q1_base_monster_set_enemy(monster, has_player ? &player : NULL);
  }
  if (monster->state->has_enemy) {
    qa_q1_base_monster_face(monster);
    const qa_actor_id enemy = monster->state->enemy;
    qa_q1am_reference(entity, "movetarget", &enemy);
    qa_q1am_reference(entity, "goalentity", &enemy);
  }
}
static void think(qa_q1am_heavy_monster *heavy, qa_q1_monster_ai mode) {
  if (base_of(heavy)->state->has_enemy) (void)check_attack(heavy);
  else hunt(heavy);
  qa_q1_base_monster_ai(base_of(heavy), mode, mode == QA_Q1_AI_STAND ? 0 : 2);
}
static void launch(qa_q1am_heavy_monster *heavy, double hand) {
  qa_q1_base_monster *monster = base_of(heavy);
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  const qa_q1_basis basis = qa_q1_make_vectors(game, qa_q1_get_body(game, entity).angles);
  const qa_vec3 origin = qa_add3(qa_add3(qa_add3(qa_q1_base_monster_origin(monster), qa_scale3(basis.forward, 40)),
                                         qa_scale3(basis.right, hand == 1 ? 65 : -65)), qa_scale3(basis.up, 90));
  const qa_vec3 delta = qa_sub3(target_or_zero(monster), origin);
  const double duration = qa_js_max(1, qa_js_min(1.75, qa_length3(delta) / 380));
  const qa_vec3 direction = qa_q1_normalize(delta);
  qa_q1_actor *ball = qa_q1_create(game, "lavaman_ball", NULL, NULL);
  ball->has_owner = true;
  ball->owner = entity->actor.id;
  ball->movement = "bounce";
  ball->solid = "bbox";
  qa_q1ad_set_text(&ball->model, "progs/lavaball.mdl");
  ball->touch = qa_q1_callback_registry_touch(game->named, ball, PREFIX ":lavaman_touch");
  ball->angular_velocity = (qa_vec3){200, 100, 300};
  qa_q1_body_patch patch;
  memset(&patch, 0, sizeof patch);
  patch.has_origin = true;
  patch.origin = origin;
  patch.has_angles = true;
  patch.angles = qa_q1mp_velocity_angles(direction);
  patch.has_bounds = true;
  patch.bounds = QA_Q1_POINT;
  patch.has_velocity = true;
  patch.velocity = qa_add3(qa_scale3(direction, 600 * duration), (qa_vec3){0, 0, 200 * duration});
  qa_q1_set_body(game, ball, &patch);
  qa_q1_link(game, ball);
  qa_q1_schedule(game, ball, 6, qa_q1_callback_registry_action(game->named, ball, "SUB_Remove"));
  qa_q1_sound(game, entity->actor.id, "boss1/throw.wav", "weapon", 1, 1);
  if (!monster->state->has_enemy || qa_q1_health(game, monster->state->enemy) <= 0)
    qa_q1_base_monster_play(monster, "lavaman_idle1");
}
static void awake(qa_q1am_heavy_monster *heavy, const qa_actor_id *activator) {
  qa_q1_base_monster *monster = base_of(heavy);
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  qa_q1ad_context *context = heavy->runtime->context;
  entity->solid = "slidebox";
  entity->movement = "fly";
  entity->aimed_damage = true;
  qa_q1_actor_set_damageable(entity, true);
  entity->movement_flags = FLAGS(entity->movement_flags) | 32;
  entity->ideal_yaw = qa_q1_get_body(game, entity).angles.y;
  const double yaw_speed = qa_q1_actor_number(entity, "yaw_speed");
  entity->yaw_speed = yaw_speed != 0 ? yaw_speed : 20; /* TS || */
  qa_q1ad_set_text(&entity->model, "progs/lavaman.mdl");
  qa_q1_set_bounds(game, entity, monster->spec->bounds);
  qa_q1ad_set_vector(context, entity, "view_ofs", (qa_vec3){0, 0, 48});
  qa_gameplay_authority_set_health(game->host.combat, entity->actor, 1250 + 250 * context->services.cvar(context->services.ctx, "skill"));
  qa_q1am_heavy_monster_install_callbacks(heavy);
  entity->use = qa_q1_callback_registry_use(game->named, entity, PREFIX ":lavaman_force_death");
  qa_q1_effect(game, "lava-splash", qa_sub3(qa_q1_base_monster_origin(monster), (qa_vec3){0, 0, 50}), NULL, 1);
  if (activator != NULL && qa_q1_is_player(game, activator)) {
    const qa_q1_player_state *player = qa_q1_player(game, *activator);
    const qa_q1_actor *actor = qa_q1_get_entity(game, activator);
    if ((player != NULL ? qa_q1_player_powerup_get(player, "invisibility") : 0) <= game->time &&
        ((actor != NULL ? FLAGS(actor->movement_flags) : 0) & 128) == 0)
      qa_q1_base_monster_set_enemy(monster, activator);
  }
  qa_q1_base_monster_play(monster, "lavaman_rise1");
}
static void act_stand(qa_q1am_heavy_monster *monster) { think(monster, QA_Q1_AI_STAND); }
static void act_walk(qa_q1am_heavy_monster *monster) { think(monster, QA_Q1_AI_WALK); }
static void act_run(qa_q1am_heavy_monster *monster) { think(monster, QA_Q1_AI_RUN); }
static void act_missile1(qa_q1am_heavy_monster *monster) { launch(monster, 1); }
static void act_missile2(qa_q1am_heavy_monster *monster) { launch(monster, 2); }
static void act_death9(qa_q1am_heavy_monster *heavy) {
  qa_q1_base_monster *monster = base_of(heavy);
  qa_q1_sound(monster->game, monster->entity->actor.id, "boss1/out1.wav", "body", 1, 1);
  qa_q1_effect(monster->game, "lava-splash", qa_sub3(qa_q1_base_monster_origin(monster), (qa_vec3){0, 0, 50}), NULL, 1);
}
static void act_death10(qa_q1am_heavy_monster *heavy) { qa_q1_remove(base_of(heavy)->game, base_of(heavy)->entity); }
static const qa_q1am_heavy_action_entry lava_actions[] = {
  {"lavaman_stand", act_stand}, {"lavaman_walk", act_walk}, {"lavaman_run", act_run},
  {"lavaman_missile(1)", act_missile1}, {"lavaman_missile(2)", act_missile2},
  {"mg3_lavaman:lavaman_death9", act_death9}, {"mg3_lavaman:lavaman_death10", act_death10},
};

static void awake_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other,
                      const qa_actor_id *activator) {
  (void)game; (void)other; /* TS (_game, entity, _other, activator) */
  awake(qa_q1am_mg3_heavy_require(ctx, entity), activator);
}
static void dead_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other,
                     const qa_actor_id *activator) {
  (void)ctx; (void)game; (void)entity; (void)other; (void)activator; /* TS use: () => undefined */
}
static void force_death(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other,
                        const qa_actor_id *activator) {
  (void)other; /* TS (game, entity, _other, activator) */
  qa_q1am_heavy_monster *monster = qa_q1am_mg3_heavy_require(ctx, entity);
  qa_q1_actor_set_damageable(entity, false);
  qa_gameplay_authority_set_health(game->host.combat, entity->actor, 0);
  game->killed_monsters++;
  qa_q1_event event;
  memset(&event, 0, sizeof event);
  event.kind = "monster-killed";
  event.has_actor = true;
  event.actor = entity->actor.id;
  event.total = game->total_monsters;
  event.found = game->killed_monsters;
  qa_q1_emit(game, event);
  qa_q1_use_targets(game, entity, activator);
  base_of(monster)->counted_death = true;
  entity->use = qa_q1_callback_registry_use(game->named, entity, PREFIX ":lavaman_dead_use");
  qa_q1_base_monster_play(base_of(monster), "lavaman_death1");
}
static void lava_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other,
                       const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)ctx; (void)normal; (void)surface; /* TS (game, entity, other) */
  if (entity->has_owner && qa_same_actor(entity->owner, other)) return;
  if (strcmp(game->host.contents(game->host.ctx, qa_q1_get_body(game, entity).origin), "sky") == 0) {
    qa_q1_remove(game, entity);
    return;
  }
  const qa_actor_id *owner = qa_q1am_opt(entity->has_owner, &entity->owner);
  if (qa_q1_health(game, other) != 0)
    (void)qa_q1_damage(game, other, entity->actor.id, owner,
                       strcmp(game->host.classname(game->host.ctx, other), "monster_shambler") == 0 ? 20 : 40,
                       NULL, NULL, NULL, NULL);
  qa_q1_radius_damage(game, entity->actor.id, owner, 40, &other, NULL, NULL);
  qa_vec3 a = qa_q1_get_body(game, entity).origin;
  qa_vec3 b = qa_scale3(qa_q1_normalize(qa_q1_get_body(game, entity).velocity), 8);
  qa_q1_set_origin(game, entity, qa_sub3(a, b));
  qa_q1_sprite_explosion(game, entity);
}
static void lava_spawn(qa_q1am_heavy_monster *heavy) {
  qa_q1_base_monster *monster = base_of(heavy);
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  if (game->options.deathmatch != 0) { qa_q1_remove(game, entity); return; }
  game->total_monsters++;
  entity->spawnflags = FLAGS(entity->spawnflags) | 16384;
  if (entity->targetname[0] != '\0') {
    entity->use = qa_q1_callback_registry_use(game->named, entity, PREFIX ":lavaman_awake");
    return;
  }
  const qa_actor_id activator = entity->activator;
  awake(heavy, entity->has_activator ? &activator : NULL);
}
static void lava_melee(qa_q1am_heavy_monster *heavy) { qa_q1_base_monster_play(base_of(heavy), "lavaman_fire1"); }
static void lava_pain(qa_q1am_heavy_monster *heavy, const qa_actor_id *attacker, double damage) {
  (void)attacker; (void)damage; /* TS pain: monster => ... */
  qa_q1_base_monster *monster = base_of(heavy);
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  if (entity->count == 0) {
    entity->count++;
    monster->state->pain_finished = game->time + 2;
    qa_q1_base_monster_play(monster, "lavaman_shocka1");
    return;
  }
  if (monster->state->pain_finished > game->time || qa_q1am_random(game) >= 0.05) return;
  monster->state->pain_finished = game->time + 2;
  qa_q1_base_monster_play(monster, "lavaman_shocka1");
}
static void lava_die(qa_q1am_heavy_monster *heavy) { qa_q1_base_monster_play(base_of(heavy), "lavaman_death1"); }

static const char *const lava_classnames[] = {"monster_lava_man"};
void qa_q1am_lava_man_definition(qa_q1am_mg3_heavy *runtime, qa_q1am_heavy_definition *out) {
  qa_q1am_heavy_callback_entry *callbacks = runtime->lava_man_callbacks; /* they capture runtime */
  memset(callbacks, 0, 4 * sizeof *callbacks);
  callbacks[0].name = "lavaman_awake";
  callbacks[0].handlers.ctx = runtime;
  callbacks[0].handlers.use = awake_use;
  callbacks[1].name = "lavaman_dead_use";
  callbacks[1].handlers.ctx = runtime;
  callbacks[1].handlers.use = dead_use;
  callbacks[2].name = "lavaman_force_death";
  callbacks[2].handlers.ctx = runtime;
  callbacks[2].handlers.use = force_death;
  callbacks[3].name = "lavaman_touch";
  callbacks[3].handlers.ctx = runtime;
  callbacks[3].handlers.touch = lava_touch;
  memset(out, 0, sizeof *out);
  out->spec = (qa_q1_base_monster_species){
    .species = "lava-man", .classnames = lava_classnames, .classname_count = 1, .model = "lavaman", .head = NULL,
    .health = 1500, .gib_health = -INFINITY, .gibs = NULL, .gib_count = 0, .bounds = {{-32, -32, -24}, {32, 32, 64}},
    .stand = "lavaman_idle1", .walk = "lavaman_walk1", .run = "lavaman_walk1", .sight = "", .melee = true,
    .missile = "lavaman_fire1", .movement = "walk",
  };
  out->frames = &qa_q1am_mg3_lavaman_frames;
  out->actions = lava_actions;
  out->action_count = sizeof lava_actions / sizeof lava_actions[0];
  out->callbacks = callbacks;
  out->callback_count = 4;
  out->spawn = lava_spawn;
  out->melee = lava_melee;
  out->pain = lava_pain;
  out->die = lava_die;
}

static bool lava_before_health(void *ctx, qa_damage_request request, double damage, qa_combat_state target,
                               const qa_combat_state *attacker) {
  (void)damage; (void)target; (void)attacker; /* TS beforeHealth(request) */
  qa_q1_services *game = ((qa_q1ad_context *)ctx)->game;
  return strcmp(game->host.classname(game->host.ctx, request.target), "monster_lava_man") != 0 ||
         request.attack.attacker == NULL ||
         strcmp(game->host.classname(game->host.ctx, *request.attack.attacker), "monster_boss") != 0;
}
static double lava_after_armor(void *ctx, qa_damage_request request, double take, qa_combat_state target,
                               const qa_combat_state *attacker) {
  (void)target; (void)attacker; /* TS afterArmor(request, take) */
  qa_q1_services *game = ((qa_q1ad_context *)ctx)->game;
  if (strcmp(game->host.classname(game->host.ctx, request.target), "monster_lava_man") != 0) return take;
  const qa_q1_player_state *player = request.attack.attacker == NULL ? NULL : qa_q1_player(game, *request.attack.attacker);
  const char *weapon = player == NULL ? NULL : player->weapon;
  return !(weapon != NULL && strcmp(weapon, "lightning") == 0) && !(weapon != NULL && strcmp(weapon, "mg3:laser") == 0)
             ? QA_Q1AM_FR(take * QA_Q1AM_FR(0.8)) : take;
}
void qa_q1am_register_lava_man_damage(qa_q1ad_context *context) {
  qa_q1_damage_source_effects effects;
  memset(&effects, 0, sizeof effects);
  effects.ctx = context;
  effects.before_health = lava_before_health;
  effects.after_armor = lava_after_armor;
  qa_q1_register_damage_source_effects(context->game, "mg3:lava-man", effects);
}
