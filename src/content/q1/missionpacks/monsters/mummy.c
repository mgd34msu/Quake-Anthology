/* Port of src/content/q1/missionpacks/monsters/mummy.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/monsters/mummy.h"

#include "qa/content/q1/missionpacks/monsters/helpers.h"
#include "qa/content/q1/missionpacks/monsters/tables/mummy.h"
#include "monsters_internal.h"

static void fire_at(qa_q1mpm_mission_monster *monster, qa_vec3 offset) {
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  qa_q1_base_monster_face(&monster->base);
  qa_vec3 target;
  if (!qa_q1_base_monster_target(&monster->base, &target)) return;
  qa_q1_sound(game, entity->actor.id, "zombie/z_shot1.wav", "weapon", 1, 1);
  qa_q1_basis basis = game->basis;
  qa_vec3 origin0 = qa_q1_base_monster_origin(&monster->base);
  qa_vec3 origin = qa_q1_vadd(qa_q1_vadd(qa_q1_vadd(origin0, qa_q1_vscale(basis.forward, offset.x)),
                                              qa_q1_vscale(basis.right, offset.y)),
                                 qa_q1_vscale(basis.up, offset.z - 24));
  qa_q1_actor *grenade = qa_q1_create(game, "mummy_grenade", NULL, NULL);
  grenade->has_owner = true; grenade->owner = entity->actor.id;
  grenade->movement = "bounce";
  grenade->solid = "bbox";
  q1mpm_set_text(&grenade->model, "progs/zom_gib.mdl");
  (void)qa_q1_make_vectors(game, qa_q1_get_body(game, entity).angles);
  qa_vec3 velocity = qa_q1_vscale(qa_q1_normalize(qa_q1_vsub(target, origin)), 600);
  velocity.z = 200;
  qa_q1_body_patch patch = {0};
  patch.has_origin = true; patch.origin = origin;
  patch.has_velocity = true; patch.velocity = velocity;
  patch.has_bounds = true; patch.bounds = QA_Q1_POINT;
  qa_q1_set_body(game, grenade, &patch);
  grenade->angular_velocity = (qa_vec3){3000, 1000, 2000};
  grenade->touch = qa_q1_callback_registry_touch(game->named, grenade, "rogue:mummyGrenadeTouch");
  qa_q1_schedule(game, grenade, 2.5, qa_q1_callback_registry_action(game->named, grenade, "SUB_Remove"));
  qa_q1_link(game, grenade);
}
static void wake(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1mpm_number(monster, "mummy:asleep", 0);
  qa_q1_base_monster_play(&monster->base, "mummy_paine12");
}
static void grenade_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other,
                const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)ctx; (void)normal; (void)surface;
  if ((entity->has_owner && qa_same_actor(other, entity->owner))) return;
  if (q1mpm_can_take_damage(game, other)) {
    (void)qa_q1_damage(game, other, entity->actor.id, (entity->has_owner ? &entity->owner : NULL), 15 + qa_q1_host_random(game) * 15, NULL, NULL, NULL, NULL);
    qa_q1_sound(game, entity->actor.id, "zombie/z_hit.wav", "weapon", 1, 1);
    qa_q1_remove(game, entity);
    return;
  }
  qa_q1_sound(game, entity->actor.id, "zombie/z_miss.wav", "weapon", 1, 1);
  qa_q1_body_patch patch = {0};
  patch.has_velocity = true;
  patch.velocity = QA_Q1_ZERO;
  qa_q1_set_body(game, entity, &patch);
  entity->angular_velocity = QA_Q1_ZERO;
  entity->touch = qa_q1_callback_registry_touch(game->named, entity, "rogue:mummyGrenadeRemove");
}
static void grenade_remove(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other,
                const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)ctx; (void)other; (void)normal; (void)surface;
  qa_q1_remove(game, entity);
}
static void run1(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1_base_monster_ai(&monster->base, QA_Q1_AI_RUN, 2);
  monster->base.in_pain = 0;
}
static void atta13(void *ctx, qa_q1mpm_mission_monster *monster) { (void)ctx; fire_at(monster, (qa_vec3){-10, -22, 30}); }
static void attb14(void *ctx, qa_q1mpm_mission_monster *monster) { (void)ctx; fire_at(monster, (qa_vec3){-10, -24, 29}); }
static void attc12(void *ctx, qa_q1mpm_mission_monster *monster) { (void)ctx; fire_at(monster, (qa_vec3){-12, -19, 29}); }
static void paine11(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1_base_monster_delay(&monster->base, monster->base.entity->next_think - monster->base.game->time + 5);
}
static void paine12(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1_sound(monster->base.game, monster->base.entity->actor.id, "zombie/z_idle.wav", "voice", 2, 1);
  qa_q1_set_bounds(monster->base.game, monster->base.entity, qa_q1mpm_hull_bounds);
  monster->base.entity->solid = "slidebox";
  if (!monster->base.game->host.walk_move(monster->base.game->host.ctx, monster->base.entity->actor, 0, 0)) {
    q1mpm_set_next_frame(&monster->base, "mummy_paine11");
    monster->base.entity->solid = "none";
  }
  qa_q1_link(monster->base.game, monster->base.entity);
}
static void mummy_missile(void *ctx, qa_q1mpm_mission_monster *monster) {
  if (qa_q1_actor_number(monster->base.entity, "mummy:asleep") != 0) { wake(ctx, monster); return; }
  double r = qa_q1_host_random(monster->base.game);
  qa_q1_base_monster_play(&monster->base, r < 0.3 ? "mummy_atta1" : r < 0.6 ? "mummy_attb1" : "mummy_attc1");
}
static void spawn(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1mpm_monster_spawn_default(monster);
  if (qa_js_bit_and(monster->base.entity->spawnflags, 4) != 0) {
    monster->base.entity->max_health = 1000;
    qa_gameplay_authority_set_health(monster->base.game->host.combat, monster->base.entity->actor, 1000);
  }
  if (qa_js_bit_and(monster->base.entity->spawnflags, 2) != 0) {
    qa_q1mpm_number(monster, "mummy:asleep", 1);
    qa_bounds bounds = {qa_q1mpm_hull_bounds.min, {16, 16, -16}};
    qa_q1_set_bounds(monster->base.game, monster->base.entity, bounds);
    monster->base.entity->solid = "none";
  }
}
static void start(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1mpm_monster_start_default(monster);
  if (qa_q1_actor_number(monster->base.entity, "mummy:asleep") != 0)
    q1mpm_set_next_frame(&monster->base, q1mpm_str_eq(monster->base.state->path, "") ? "mummy_sleep" : "mummy_wake");
}
static void found(void *ctx, qa_q1mpm_mission_monster *monster, qa_actor_id target) {
  (void)ctx;
  qa_q1mpm_monster_found_default(monster, target);
  if (qa_q1_actor_number(monster->base.entity, "mummy:asleep") != 0)
    q1mpm_set_next_frame(&monster->base, "mummy_wake");
}
static void pain(void *ctx, qa_q1mpm_mission_monster *monster, const qa_actor_id *attacker, double damage) {
  (void)attacker; (void)damage;
  if (qa_q1_actor_number(monster->base.entity, "mummy:asleep") != 0) { wake(ctx, monster); return; }
  if (monster->base.state->pain_finished > monster->base.game->time) return;
  double r = qa_q1_host_random(monster->base.game);
  if (r > 0.24) return;
  monster->base.state->pain_finished = monster->base.game->time + 2.5;
  qa_q1_base_monster_play(&monster->base, r < 0.06 ? "mummy_paina1" : r < 0.12 ? "mummy_painb1"
                                 : r < 0.18 ? "mummy_painc1" : "mummy_paind1");
}
static void die(void *ctx, qa_q1mpm_mission_monster *monster, const qa_actor_id *attacker) {
  (void)ctx; (void)attacker;
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  qa_gameplay_authority_set_health(game->host.combat, entity->actor, -35);
  qa_q1_sound_defaults(game, entity->actor.id, "zombie/z_gib.wav");
  qa_q1_throw_head(game, entity, "h_zombie");
  static const char *const models[] = {"gib1", "gib2", "gib3"};
  for (size_t i = 0; i < 3; i++)
    (void)qa_q1_throw_gib(game, qa_q1_base_monster_origin(&monster->base), models[i], -35);
}
static const char *const classnames[] = {"monster_mummy"};
static const char *const spec_gibs[] = {"gib1", "gib2", "gib3"};
static const qa_q1mpm_named_action actions[] = {
    {"mummy:mummy_run1", {run1, NULL}},
    {"mummy:mummy_atta13", {atta13, NULL}},
    {"mummy:mummy_attb14", {attb14, NULL}},
    {"mummy:mummy_attc12", {attc12, NULL}},
    {"mummy:mummy_paine11", {paine11, NULL}},
    {"mummy:mummy_paine12", {paine12, NULL}},
    {"mummy_wake", {wake, NULL}},
    {"mummy_missile", {mummy_missile, NULL}},
};
static const qa_q1mpm_named_handlers callbacks[] = {
    {"mummyGrenadeTouch", {.touch = grenade_touch}},
    {"mummyGrenadeRemove", {.touch = grenade_remove}},
};
const qa_q1mpm_pack_monster_definition qa_q1mpm_mummy_definition = {
    .spec = {"mummy", NULL, classnames, 1, "mummy", "h_zombie", 500, 0, spec_gibs, 3,
             {{-16, -16, -24}, {16, 16, 32}}, "mummy_stand1", "mummy_walk1", "mummy_run1",
             "zombie/z_idle.wav", "mummy_missile", false, "walk"},
    .frames = {qa_q1mpm_tables_mummy_entries, QA_Q1MPM_TABLES_MUMMY_COUNT},
    .actions = actions,
    .action_count = sizeof(actions) / sizeof(actions[0]),
    .callbacks = callbacks,
    .callback_count = sizeof(callbacks) / sizeof(callbacks[0]),
    .spawn = {spawn, NULL},
    .start = {start, NULL},
    .pain = pain,
    .die = die,
    .found = found,
};
