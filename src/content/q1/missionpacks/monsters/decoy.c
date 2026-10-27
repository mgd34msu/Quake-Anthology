/* Port of src/content/q1/missionpacks/monsters/decoy.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/monsters/decoy.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/missionpacks/monsters/helpers.h"
#include "qa/content/q1/missionpacks/monsters/tables/hipdecoy.h"
#include "monsters_internal.h"

static void setup(qa_q1mpm_mission_monster *monster) {
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  snprintf(entity->model, sizeof(entity->model), "%s", "progs/player.mdl");
  game->set_bounds(game, entity, qa_q1mpm_hull_bounds);
  qa_q1mpm_fields_set(entity, "view_ofs", "0 0 22");
  entity->solid = "slidebox";
  entity->movement = "step";
  entity->max_health = 3000000;
  game->host->combat_set_health(game->host->ctx, entity->actor, 3000000);
  qa_actor_id players[1];
  size_t count = game->host->players(game->host->ctx, players, 1);
  qa_q1mpm_actor *player = count > 0 ? game->entity(game, QA_Q1MPM_SOME(players[0])) : NULL;
  char buf[32];
  qa_q1mpm_format_number(buf, sizeof(buf), player == NULL ? 0 : qa_q1mpm_actor_number(player, "colormap"));
  qa_q1mpm_fields_set(entity, "colormap", buf);
}
static void decoy_stand1(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  qa_q1mpm_monster_change_yaw(monster);
  double walk = qa_q1mpm_actor_number(entity, "walkframe");
  if (walk >= 5) walk = 0;
  entity->frame = 12 + walk;
  qa_q1mpm_number(monster, "walkframe", walk + 1);
  if (game->time > monster->state.pause_until) qa_q1mpm_monster_play(monster, "decoy_walk1");
}
static void decoy_walk1(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  qa_q1mpm_actor *goal = game->entity(game, qa_q1mpm_references_get(entity, "goalentity"));
  if (goal == NULL) {
    qa_q1mpm_actor *found[1];
    if (game->find(game, monster->state.path, found, 1) > 0) goal = found[0];
  }
  if (goal != NULL) game->host->move_to_goal(game->host->ctx, entity->actor, goal->actor.id, 12, NULL);
  qa_q1mpm_number(monster, "weaponframe", 0);
  double walk = qa_q1mpm_actor_number(entity, "walkframe");
  if (walk == 6) walk = 0;
  if (walk == 2 || walk == 5) {
    double r = game->host->random(game->host->ctx);
    int step = r < 0.14 ? 1 : r < 0.29 ? 2 : r < 0.43 ? 3 : r < 0.58 ? 4 : r < 0.72 ? 5 : r < 0.86 ? 6 : 7;
    char path[32];
    snprintf(path, sizeof(path), "misc/foot%d.wav", step);
    qa_q1mpm_event event = {0};
    event.kind = "sound";
    event.actor = QA_Q1MPM_SOME(entity->actor.id);
    event.path = path;
    event.channel = QA_Q1MPM_CH("voice");
    event.volume = 0.5;
    event.attenuation = 1;
    game->host->emit(game->host->ctx, &event);
  }
  entity->frame += walk;
  qa_q1mpm_number(monster, "walkframe", walk + 1);
}
static void spawn(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  setup(monster);
  qa_q1mpm_monster_spawn_default(monster);
  monster->game->total_monsters--;
}
static void pain_or_die_frame(qa_q1mpm_mission_monster *monster) {
  qa_q1mpm_monster_play(monster, "decoy_stand1");
}
static void pain(void *ctx, qa_q1mpm_mission_monster *monster, qa_q1mpm_opt_actor attacker, double damage) {
  (void)ctx; (void)attacker; (void)damage;
  pain_or_die_frame(monster);
}
static void die(void *ctx, qa_q1mpm_mission_monster *monster, qa_q1mpm_opt_actor attacker) {
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

bool qa_q1mpm_become_decoy(qa_q1mpm_runtime *runtime, const char *target, qa_vec3 origin,
                           qa_q1mpm_actor **out) {
  qa_q1mpm_game *game = runtime->game;
  qa_q1mpm_actor *entity = game->create(game, "monster_decoy");
  qa_q1mpm_mission_monster *monster = calloc(1, sizeof(*monster));
  if (monster == NULL) return false;
  qa_q1mpm_mission_monster_init(monster, game, entity, &qa_q1mpm_decoy_definition, runtime);
  if (!qa_q1mpm_runtime_set_monster(runtime, entity->actor, monster)) {
    qa_q1mpm_mission_monster_destroy(monster);
    free(monster);
    return false;
  }
  if (game->world != NULL)
    qa_q1mpm_references_set(game->world, "hipdecoy", QA_Q1MPM_SOME(entity->actor.id));
  setup(monster);
  game->set_origin(game, entity, origin);
  snprintf(entity->target, sizeof(entity->target), "%s", target);
  snprintf(monster->state.path, sizeof(monster->state.path), "%s", target);
  qa_q1mpm_actor_set_damageable(game, entity, true);
  entity->aimed_damage = true;
  entity->ideal_yaw = game->body(game, entity).angles.y;
  if (entity->yaw_speed == 0) entity->yaw_speed = 20;
  entity->use = game->named.use(game->named.ctx, entity, "hipnotic:monster_use");
  entity->movement_flags |= 32;
  qa_q1mpm_actor *found[1];
  size_t found_count = game->find(game, target, found, 1);
  qa_q1mpm_actor *destination = found_count > 0 ? found[0] : NULL;
  if (target[0] != '\0') {
    qa_q1mpm_opt_actor destination_id =
        destination == NULL ? QA_Q1MPM_NONE : QA_Q1MPM_SOME(destination->actor.id);
    qa_q1mpm_references_set(entity, "goalentity", destination_id);
    qa_q1mpm_references_set(entity, "movetarget", destination_id);
    qa_vec3 destination_origin = destination == NULL
                                     ? (game->world == NULL ? QA_Q1MPM_ZERO
                                                            : game->body(game, game->world).origin)
                                     : game->body(game, destination).origin;
    entity->ideal_yaw = qa_q1mpm_yaw_for(qa_q1mpm_vsub(destination_origin, origin));
    if (destination != NULL && strcmp(destination->classname, "path_corner") == 0)
      qa_q1mpm_monster_play(monster, "decoy_walk1");
    else
      monster->state.pause_until = 99999999;
    qa_q1mpm_monster_play(monster, "decoy_stand1");
  } else {
    monster->state.pause_until = 99999999;
    qa_q1mpm_monster_play(monster, "decoy_stand1");
  }
  qa_q1mpm_monster_delay(monster, entity->next_think - game->time + game->host->random(game->host->ctx) * 0.5);
  *out = entity;
  return true;
}
