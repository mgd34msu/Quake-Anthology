/* Port of src/content/q1/missionpacks/monsters/overlord.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/monsters/overlord.h"

#include <stdio.h>
#include <string.h>

#include "qa/content/q1/missionpacks/monsters/helpers.h"
#include "qa/content/q1/missionpacks/monsters/tables/s_wrath.h"
#include "qa/content/q1/missionpacks/monsters/wrath.h"
#include "monsters_internal.h"

bool qa_q1mpm_is_spawn_point_empty(qa_q1_services *game, qa_q1_actor *point) {
  size_t count = 0;
  qa_actor_id *actors = qa_q1mpm_radius_actors(game, qa_q1_get_body(game, point).origin, 64, &count);
  bool empty = true;
  for (size_t i = 0; i < count && empty; i++) {
    if (qa_same_actor(actors[i], point->actor.id)) continue;
    qa_q1_actor *other = qa_q1_get_entity(game, &actors[i]);
    /* QuakeC never clears think, so every item that ran PlaceItem keeps blocking the point; the port clears it
     * before each callback. */
    const bool placed_item = other != NULL && (strncmp(other->classname, "item_", 5) == 0 ||
                                               strncmp(other->classname, "weapon_", 7) == 0);
    if (qa_js_bit_and(other == NULL ? 0 : other->movement_flags, 32) != 0 ||
        q1mpm_str_eq(game->host.classname(game->host.ctx, actors[i]), "player") ||
        (other != NULL && (other->think.fn != NULL || other->think.name != NULL)) || placed_item)
      empty = false;
  }
  free(actors);
  return empty;
}
qa_q1_actor *qa_q1mpm_overlord_destination(qa_q1_services *game) {
  const qa_actor_id *player = q1mpm_first_player(game);
  if (player == NULL && game->world != NULL) player = &game->world->actor.id;
  qa_q1_body body;
  const bool has_body = player != NULL && q1mpm_read_body(game, *player, &body);
  const qa_q1_basis basis = qa_q1_make_vectors(game, has_body ? body.angles : QA_Q1_ZERO);
  const qa_vec3 origin = has_body ? body.origin : QA_Q1_ZERO;
  qa_q1_actor *best = NULL, *furthest = NULL;
  double distance = 0;
  for (size_t i = 0; i < game->entity_count; i++) {
    qa_q1_actor *entity = game->entities[i];
    if (strcmp(entity->classname, "info_overlord_destination") != 0 || !qa_q1mpm_is_spawn_point_empty(game, entity))
      continue;
    const qa_vec3 delta = qa_q1_vsub(qa_q1_get_body(game, entity).origin, origin);
    const double current = qa_q1_length(delta);
    if (qa_q1_dot(qa_q1_normalize(delta), basis.forward) > 0.6 && current > 150) best = entity;
    if (current > distance) { furthest = entity; distance = current; }
  }
  return best != NULL ? best : furthest;
}
/* TS `(_game, entity) => ...` over the registering game (ctx). */
static void destination_spawn(void *ctx, qa_q1_services *source, qa_q1_actor *entity) {
  (void)source;
  qa_q1_services *game = ctx;
  const qa_q1_body body = qa_q1_get_body(game, entity);
  entity->mangle = body.angles;
  q1mpm_set_text(&entity->model, "");
  qa_q1_body_patch patch = {0};
  patch.has_angles = true; patch.angles = QA_Q1_ZERO;
  patch.has_origin = true; patch.origin = qa_q1_vadd(body.origin, (qa_vec3){0, 0, 27});
  qa_q1_set_body(game, entity, &patch);
}
void qa_q1mpm_register_overlord_destination(qa_q1_services *game) {
  qa_q1_register_spawn(game, "info_overlord_destination", destination_spawn, game);
}
static void teleport(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  if (qa_js_bit_and(entity->spawnflags, 2) == 0 || qa_q1_host_random(game) > 0.75) return;
  qa_q1_actor *destination = qa_q1mpm_overlord_destination(game);
  if (destination == NULL) return;
  (void)qa_q1_spawn_teleport_fog(game, qa_q1_base_monster_origin(&monster->base));
  qa_q1_basis basis = qa_q1_make_vectors(game, qa_q1_get_body(game, entity).angles);
  qa_vec3 origin = qa_q1_get_body(game, destination).origin;
  (void)qa_q1_spawn_teleport_fog(game, qa_q1_vadd(origin, qa_q1_vscale(basis.forward, 32)));
  (void)qa_q1_spawn_teledeath(game, origin, entity->actor.id);
  qa_q1_set_origin(game, entity, origin);
  entity->movement_flags = qa_js_bit_and(entity->movement_flags, qa_js_bit_not(512));
}
static void toss(qa_q1mpm_mission_monster *monster, const char *model) {
  qa_q1_services *game = monster->base.game;
  qa_q1_basis basis = qa_q1_make_vectors(game, qa_q1_get_body(game, monster->base.entity).angles);
  const qa_vec3 lift = qa_q1_vadd(qa_q1_vadd(qa_q1_vscale(basis.forward, 250), qa_q1_vscale(basis.up, 300)),
                                  qa_q1_vscale(basis.up, qa_q1_host_random(game) * 100 - 50));
  const qa_vec3 velocity = qa_q1_vadd(lift, qa_q1_vscale(basis.right, qa_q1_host_random(game) * 200 - 100));
  qa_q1_actor *gib = qa_q1_create(game, "gib", NULL, NULL);
  char path[96];
  snprintf(path, sizeof path, "progs/%s.mdl", model);
  q1mpm_set_text(&gib->model, path);
  gib->movement = "bounce";
  q1mpm_set_number_text(gib, "ltime", game->time);
  qa_q1_body_patch patch = {0};
  patch.has_origin = true; patch.origin = qa_q1_base_monster_origin(&monster->base);
  patch.has_bounds = true; patch.bounds = QA_Q1_POINT;
  qa_q1_set_body(game, gib, &patch);
  const double delay = 10 + qa_q1_host_random(game) * 10;
  qa_q1_schedule(game, gib, delay, qa_q1_callback_registry_action(game->named, gib, "SUB_Remove"));
  qa_q1_body_patch velocity_patch = {0};
  velocity_patch.has_velocity = true;
  velocity_patch.velocity = velocity;
  qa_q1_set_body(game, gib, &velocity_patch);
  qa_q1_link(game, gib);
}
static void burst(qa_q1mpm_mission_monster *monster, const char *const *models, size_t count) {
  qa_q1_event event;
  memset(&event, 0, sizeof event);
  event.kind = "colored-explosion";
  event.has_origin = true;
  event.origin = qa_q1_base_monster_origin(&monster->base);
  event.color_start = 0;
  event.color_length = 4;
  qa_q1_emit(monster->base.game, event);
  for (size_t i = 0; i < count; i++) toss(monster, models[i]);
}
static void smash(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  if (!monster->base.state->has_enemy || !qa_q1_can_damage(game, monster->base.state->enemy, entity->actor.id))
    return;
  qa_q1_base_monster_ai(&monster->base, QA_Q1_AI_CHARGE, 10);
  qa_vec3 target;
  if (!qa_q1_base_monster_target(&monster->base, &target) || qa_q1_base_monster_distance(&monster->base) > 100) return;
  double damage = 20 + qa_q1_host_random(game) * 10;
  qa_q1_sound(game, entity->actor.id, "s_wrath/smash.wav", "weapon", 1, 1);
  (void)qa_q1_damage(game, monster->base.state->enemy, entity->actor.id, &entity->actor.id, damage, NULL, NULL, NULL, NULL);
  qa_vec3 eye_target;
  qa_vec3 aim = qa_q1mpm_eye(game, monster->base.state->enemy, &eye_target) ? eye_target : target;
  qa_vec3 direction = qa_q1_normalize(qa_q1_vsub(aim, qa_q1_base_monster_origin(&monster->base)));
  qa_q1_event event;
  memset(&event, 0, sizeof event);
  event.kind = "particles";
  event.has_origin = true;
  event.origin = qa_q1_vsub(target, qa_q1_vscale(direction, 30));
  event.direction = qa_q1_vscale(direction, -10);
  event.color = 73;
  event.count = damage * 2;
  qa_q1_emit(game, event);
}
static void melee_impl(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  double r = qa_q1_host_random(monster->base.game);
  qa_q1_base_monster_play(&monster->base, r < 0.33 ? "overlord_at_a01" : r < 0.66 ? "overlord_at_b01" : "overlord_at_c01");
}
static void missile_action(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  (void)qa_q1_host_random(monster->base.game);
  qa_q1_base_monster_play(&monster->base, "overlord_msl_a01");
}
static void missile4(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1mpm_wrath_missile(monster, 4);
}
static void die01(void *ctx, qa_q1mpm_mission_monster *monster) { (void)ctx; qa_q1_base_monster_delay(&monster->base, 0.05); }
static void die02(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  monster->base.entity->movement_flags = qa_js_bit_or(monster->base.entity->movement_flags, 1);
  qa_q1_base_monster_delay(&monster->base, 0.05);
}
static void die17(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  q1mpm_set_text(&monster->base.entity->model, "");
  static const char *const models[] = {"s_wrtgb2", "s_wrtgb3", "wrthgib1", "wrthgib2", "wrthgib3"};
  burst(monster, models, 5);
  qa_q1_base_monster_delay(&monster->base, 0.1);
}
static void die18(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  static const char *const models[] = {"gib1", "gib2", "gib3", "gib1", "gib2", "gib3"};
  burst(monster, models, 6);
  qa_q1_base_monster_delay(&monster->base, 0.1);
}
static void die19(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  static const char *const models[] = {"gib1", "gib2", "gib3", "gib1", "gib2", "gib3"};
  burst(monster, models, 6);
  qa_q1_remove(monster->base.game, monster->base.entity);
}
static void pain(void *ctx, qa_q1mpm_mission_monster *monster, const qa_actor_id *attacker, double damage) {
  (void)ctx; (void)attacker; (void)damage;
  if (monster->base.state->pain_finished > monster->base.game->time) return;
  double r = qa_q1_host_random(monster->base.game);
  if (r > 0.2) return;
  qa_q1_base_monster_play(&monster->base, r < 0.15 ? "overlord_pn_a01" : "overlord_pn_b01");
  monster->base.state->pain_finished = monster->base.game->time + 2;
  qa_q1_sound_defaults(monster->base.game, monster->base.entity->actor.id, "wrath/wpain.wav");
}
static void die(void *ctx, qa_q1mpm_mission_monster *monster, const qa_actor_id *attacker) {
  (void)ctx; (void)attacker;
  qa_q1_base_monster_play(&monster->base, "overlord_die02");
}
static const char *const classnames[] = {"monster_super_wrath"};
static const qa_q1mpm_named_action actions[] = {
    {"overlord_smash", {smash, NULL}},
    {"overlord_melee", {melee_impl, NULL}},
    {"overlord_teleport", {teleport, NULL}},
    {"overlord_missile", {missile_action, NULL}},
    {"WrathMissile(4)", {missile4, NULL}},
    {"s_wrath:overlord_die01", {die01, NULL}},
    {"s_wrath:overlord_die02", {die02, NULL}},
    {"s_wrath:overlord_die17", {die17, NULL}},
    {"s_wrath:overlord_die18", {die18, NULL}},
    {"s_wrath:overlord_die19", {die19, NULL}},
};
const qa_q1mpm_pack_monster_definition qa_q1mpm_overlord_definition = {
    .spec = {"super-wrath", NULL, classnames, 1, "s_wrath", NULL, 1000, -INFINITY, NULL, 0,
             {{-16, -16, -24}, {16, 16, 32}}, "overlord_stand1", "overlord_walk01", "overlord_run01",
             "", "overlord_missile", true, "fly"},
    .frames = {qa_q1mpm_tables_s_wrath_entries, QA_Q1MPM_TABLES_S_WRATH_COUNT},
    .actions = actions,
    .action_count = sizeof(actions) / sizeof(actions[0]),
    .pain = pain,
    .die = die,
    .melee = {melee_impl, NULL},
};
