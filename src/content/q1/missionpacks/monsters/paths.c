/* Port of src/content/q1/missionpacks/monsters/paths.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/monsters/paths.h"

#include <stdio.h>
#include <string.h>

#include "monsters_internal.h"

/* TS runtime.monsters.get(actor) ?? runtime.base.monsters.get(actor) ?? null. */
static qa_q1_base_monster *controller(qa_q1mpm_runtime *runtime, qa_q1_actor *entity) {
  qa_q1mpm_mission_monster *monster = qa_q1mpm_runtime_get(runtime, entity->actor);
  if (monster != NULL) return &monster->base;
  return qa_q1_creatures_monsters_get(qa_q1_base_creatures(runtime->base), entity->actor);
}
static const char *callback_prefix(const qa_q1_base_monster *monster) {
  return monster->has_source && monster->source.callback_prefix != NULL ? monster->source.callback_prefix : "base";
}
static void follow(qa_q1mpm_runtime *runtime, qa_q1_actor *trigger, qa_actor_id actor) {
  qa_q1_services *game = runtime->game;
  qa_q1_actor *entity = qa_q1_get_entity(game, &actor);
  if (entity == NULL || qa_js_bit_and(entity->movement_flags, 32) == 0 ||
      strcmp(entity->classname, "monster_decoy") == 0 || qa_q1_actor_number(entity, "wetsuit_time") > game->time)
    return;
  qa_q1_base_monster *monster = controller(runtime, entity);
  if (monster == NULL) return;
  const qa_actor_id *enemy_ref = qa_q1_base_monster_enemy(monster);
  const bool has_enemy = enemy_ref != NULL;
  const qa_actor_id enemy = has_enemy ? *enemy_ref : (qa_actor_id){0};
  qa_vec3 start, end;
  const bool has_start = qa_q1_base_monster_eye(monster, entity->actor.id, &start);
  bool has_end = true;
  if (!has_enemy) end = game->world == NULL ? QA_Q1_ZERO : qa_q1_get_body(game, game->world).origin;
  else has_end = qa_q1_base_monster_eye(monster, enemy, &end);
  if (!has_start || !has_end || q1mpm_trace(game, start, end, QA_Q1_POINT, &actor, true).fraction == 1) return;
  if (has_enemy) {
    monster->state->has_old_enemy = true;
    monster->state->old_enemy = enemy;
    qa_q1_base_monster_set_enemy(monster, NULL);
    q1mpm_set_next_frame(monster, monster->spec->walk);
    char name[192];
    snprintf(name, sizeof name, "%s:monster_frame", callback_prefix(monster));
    entity->think = qa_q1_callback_registry_action(game->named, entity, name);
  }
  qa_q1_actor *found[1];
  qa_q1_actor *target = qa_q1_find(game, trigger->target, found, 1) > 0 ? found[0] : NULL;
  q1mpm_set_text(&monster->state->path, target == NULL ? "" : target->targetname);
  const qa_actor_id *target_id = target == NULL ? NULL : &target->actor.id;
  (void)qa_q1_actor_reference_set(entity, "goalentity", target_id);
  (void)qa_q1_actor_reference_set(entity, "movetarget", target_id);
  qa_vec3 a = target == NULL ? QA_Q1_ZERO : qa_q1_get_body(game, target).origin;
  qa_vec3 b = qa_q1_base_monster_origin(monster);
  entity->ideal_yaw = qa_q1_yaw_for(qa_q1_vsub(a, b));
  q1mpm_set_fround_text(entity, "wetsuit_time", game->time + 2);
  if (target != NULL) return;
  if (monster->state->has_old_enemy) { qa_q1_base_monster_found(monster, monster->state->old_enemy); return; }
  qa_actor_id client;
  const bool has_client = q1mpm_check_client(game, entity->actor, &client);
  /* Original QC enters FoundTarget when checkclient returns world. */
  if (!has_client && game->world != NULL) { qa_q1_base_monster_found(monster, game->world->actor.id); return; }
  monster->state->pause_until = game->time + 999999;
  qa_q1_base_monster_play(monster, monster->spec->stand);
}
static void follow_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other,
                         const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)game; (void)normal; (void)surface; /* TS `(_game, entity, other) => follow(runtime, entity, other)` */
  follow(ctx, entity, other);
}
typedef struct path_spawn_ctx { const char *classname; } path_spawn_ctx;
static const path_spawn_ctx FOLLOW_CTX = {"path_follow"};
static const path_spawn_ctx FOLLOW2_CTX = {"path_follow2"};
static void path_spawn(void *ctx, qa_q1_services *source, qa_q1_actor *entity) {
  const char *classname = ((const path_spawn_ctx *)ctx)->classname;
  entity->solid = "trigger";
  entity->touch = qa_q1_callback_registry_touch(source->named, entity, "hipnotic:t_followtarget");
  if (strcmp(classname, "path_follow2") == 0) {
    const qa_bounds bounds = {{-8, -8, -8}, {8, 8, 8}};
    qa_q1_set_bounds(source, entity, bounds);
    return;
  }
  entity->movement = "none";
  q1mpm_set_text(&entity->model, "");
  qa_q1_link(source, entity);
}
static bool move_target(void *ctx, qa_q1_actor *corner, qa_q1_actor *entity) {
  qa_q1mpm_runtime *runtime = ctx;
  qa_q1_services *game = runtime->game;
  qa_q1_base_monster *monster = controller(runtime, entity);
  if (monster == NULL) return false;
  if (strcmp(monster->state->path, corner->targetname) != 0 || qa_q1_base_monster_enemy(monster) != NULL) return true;
  if (strcmp(entity->classname, "monster_ogre") == 0) qa_q1_sound(game, entity->actor.id, "ogre/ogdrag.wav", "voice", 2, 1);
  qa_actor_id goal;
  const bool found = qa_q1_path_goal(game, corner->target, &goal);
  const qa_q1_actor *target = found ? qa_q1_get_entity(game, &goal) : NULL;
  q1mpm_set_text(&monster->state->path, target == NULL || target->targetname == NULL ? "" : target->targetname);
  (void)qa_q1_actor_reference_set(entity, "goalentity", found ? &goal : NULL);
  (void)qa_q1_actor_reference_set(entity, "movetarget", found ? &goal : NULL);
  const qa_body_state_result body = found ? qa_shared_body_table_read(game->host.bodies, goal) : (qa_body_state_result){0};
  const qa_vec3 a = body.has_value ? body.value.origin : QA_Q1_ZERO;
  const qa_vec3 b = qa_q1_base_monster_origin(monster);
  entity->ideal_yaw = qa_q1_yaw_for(qa_q1_vsub(a, b));
  if (found) {
    if (corner->delay != 0) {
      monster->state->pause_until = game->time + corner->delay;
      qa_q1_base_monster_play(monster, monster->spec->stand);
    }
    return true;
  }
  monster->state->pause_until = game->time + 999999;
  qa_q1_base_monster_play(monster, monster->spec->stand);
  return true;
}
void qa_q1mpm_register_hipnotic_paths(qa_q1mpm_runtime *runtime) {
  qa_q1_services *game = runtime->game;
  qa_q1_callback_registry_register(game->named, "hipnotic:t_followtarget",
                                   (qa_q1_callback_handlers){.ctx = runtime, .touch = follow_touch});
  qa_q1_register_spawn(game, "path_follow", path_spawn, (void *)&FOLLOW_CTX);
  qa_q1_register_spawn(game, "path_follow2", path_spawn, (void *)&FOLLOW2_CTX);
  qa_q1_register_path_touch(game, "hipnotic:t_movetarget", move_target, runtime);
}
