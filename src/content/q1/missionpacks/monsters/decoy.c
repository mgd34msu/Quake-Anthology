/* Port of src/content/q1/missionpacks/monsters/decoy.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/monsters/decoy.h"

#include <stdio.h>
#include <string.h>

#include "qa/content/q1/missionpacks/monsters/helpers.h"
#include "qa/content/q1/missionpacks/monsters/tables/hipdecoy.h"
#include "monsters_internal.h"

static void setup(qa_q1mpm_mission_monster *monster) {
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  q1mpm_set_text(&entity->model, "progs/player.mdl");
  qa_q1_set_bounds(game, entity, qa_q1mpm_hull_bounds);
  (void)qa_q1_actor_field_set(entity, "view_ofs", "0 0 22");
  entity->solid = "slidebox";
  entity->movement = "step";
  entity->max_health = 3000000;
  qa_gameplay_authority_set_health(game->host.combat, entity->actor, 3000000);
  qa_q1_actor *player = qa_q1_get_entity(game, q1mpm_first_player(game));
  q1mpm_set_number_text(entity, "colormap", player == NULL ? 0 : qa_q1_actor_number(player, "colormap"));
}
static void decoy_stand1(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  qa_q1_base_monster_change_yaw(&monster->base);
  double walk = qa_q1_actor_number(entity, "walkframe");
  if (walk >= 5) walk = 0;
  entity->frame = 12 + walk;
  qa_q1mpm_number(monster, "walkframe", walk + 1);
  if (game->time > monster->base.state->pause_until) qa_q1_base_monster_play(&monster->base, "decoy_walk1");
}
static void decoy_walk1(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  qa_actor_id goal_id;
  qa_q1_actor *goal = qa_q1_get_entity(game, q1mpm_reference(entity, "goalentity", &goal_id));
  if (goal == NULL) {
    qa_q1_actor *found[1];
    if (qa_q1_find(game, monster->base.state->path, found, 1) > 0) goal = found[0];
  }
  if (goal != NULL) game->host.move_to_goal(game->host.ctx, entity->actor, goal->actor.id, 12, NULL);
  qa_q1mpm_number(monster, "weaponframe", 0);
  double walk = qa_q1_actor_number(entity, "walkframe");
  if (walk == 6) walk = 0;
  if (walk == 2 || walk == 5) {
    double r = qa_q1_host_random(game);
    int step = r < 0.14 ? 1 : r < 0.29 ? 2 : r < 0.43 ? 3 : r < 0.58 ? 4 : r < 0.72 ? 5 : r < 0.86 ? 6 : 7;
    char path[32];
    snprintf(path, sizeof path, "misc/foot%d.wav", step);
    q1mpm_emit_sound(game, entity->actor.id, path, "voice", 0.5, 1);
  }
  entity->frame += walk;
  qa_q1mpm_number(monster, "walkframe", walk + 1);
}
static void spawn(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  setup(monster);
  qa_q1mpm_monster_spawn_default(monster);
  monster->base.game->total_monsters--;
}
static void pain_or_die_frame(qa_q1mpm_mission_monster *monster) {
  qa_q1_base_monster_play(&monster->base, "decoy_stand1");
}
static void pain(void *ctx, qa_q1mpm_mission_monster *monster, const qa_actor_id *attacker, double damage) {
  (void)ctx; (void)attacker; (void)damage;
  pain_or_die_frame(monster);
}
static void die(void *ctx, qa_q1mpm_mission_monster *monster, const qa_actor_id *attacker) {
  (void)ctx; (void)attacker;
  pain_or_die_frame(monster);
}
static const char *const classnames[] = {"monster_decoy"};
static const qa_q1mpm_named_action actions[] = {
    {"hipdecoy:decoy_stand1", {decoy_stand1, NULL}},
    {"hipdecoy:decoy_walk1", {decoy_walk1, NULL}},
};
const qa_q1mpm_pack_monster_definition qa_q1mpm_decoy_definition = {
    .spec = {"decoy", NULL, classnames, 1, "player", NULL, 3000000, -INFINITY, NULL, 0,
             {{-16, -16, -24}, {16, 16, 32}}, "decoy_stand1", "decoy_walk1", "decoy_walk1",
             "", "decoy_stand1", false, "walk"},
    .frames = {qa_q1mpm_tables_hipdecoy_entries, QA_Q1MPM_TABLES_HIPDECOY_COUNT},
    .actions = actions,
    .action_count = sizeof(actions) / sizeof(actions[0]),
    .spawn = {spawn, NULL},
    .pain = pain,
    .die = die,
};

qa_q1_actor *qa_q1mpm_become_decoy(qa_q1mpm_runtime *runtime, const char *target, qa_vec3 origin) {
  qa_q1_services *game = runtime->game;
  qa_q1_actor *entity = qa_q1_create(game, "monster_decoy", NULL, NULL);
  qa_q1mpm_mission_monster *monster = qa_q1mpm_mission_monster_create(game, entity, &qa_q1mpm_decoy_definition, runtime);
  qa_q1mpm_runtime_set_monster(runtime, entity->actor, monster);
  if (game->world != NULL)
    (void)qa_q1_actor_reference_set(game->world, "hipdecoy", &entity->actor.id);
  setup(monster);
  qa_q1_set_origin(game, entity, origin);
  q1mpm_set_text(&entity->target, target);
  q1mpm_set_text(&monster->base.state->path, target);
  qa_q1_actor_set_damageable(entity, true);
  entity->aimed_damage = true;
  entity->ideal_yaw = qa_q1_get_body(game, entity).angles.y;
  if (entity->yaw_speed == 0) entity->yaw_speed = 20;
  entity->use = qa_q1_callback_registry_use(game->named, entity, "hipnotic:monster_use");
  entity->movement_flags = qa_js_bit_or(entity->movement_flags, 32);
  qa_q1_actor *found[1];
  size_t found_count = qa_q1_find(game, target, found, 1);
  qa_q1_actor *destination = found_count > 0 ? found[0] : NULL;
  if (target[0] != '\0') {
    const qa_actor_id *destination_id = destination == NULL ? NULL : &destination->actor.id;
    (void)qa_q1_actor_reference_set(entity, "goalentity", destination_id);
    (void)qa_q1_actor_reference_set(entity, "movetarget", destination_id);
    qa_vec3 destination_origin = destination == NULL
                                     ? (game->world == NULL ? QA_Q1_ZERO
                                                            : qa_q1_get_body(game, game->world).origin)
                                     : qa_q1_get_body(game, destination).origin;
    entity->ideal_yaw = qa_q1_yaw_for(qa_q1_vsub(destination_origin, origin));
    if (destination != NULL && strcmp(destination->classname, "path_corner") == 0)
      qa_q1_base_monster_play(&monster->base, "decoy_walk1");
    else
      monster->base.state->pause_until = 99999999;
    qa_q1_base_monster_play(&monster->base, "decoy_stand1");
  } else {
    monster->base.state->pause_until = 99999999;
    qa_q1_base_monster_play(&monster->base, "decoy_stand1");
  }
  qa_q1_base_monster_delay(&monster->base, entity->next_think - game->time + qa_q1_host_random(game) * 0.5);
  return entity;
}
