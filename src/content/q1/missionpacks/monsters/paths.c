/* Port of src/content/q1/missionpacks/monsters/paths.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/monsters/paths.h"

#include <stdio.h>
#include <string.h>

#include "monsters_internal.h"

static qa_q1mpm_mission_monster *controller(qa_q1mpm_runtime *runtime, qa_q1mpm_actor *entity) {
  qa_q1mpm_mission_monster *monster = qa_q1mpm_runtime_get(runtime, entity->actor);
  if (monster != NULL) return monster;
  return runtime->base->monsters_get(runtime->base->ctx, entity->actor);
}
static void copy_path(char *dst, size_t size, const char *src) {
  size_t length = strlen(src);
  if (length >= size) length = size - 1;
  memcpy(dst, src, length);
  dst[length] = '\0';
}
static void follow(qa_q1mpm_runtime *runtime, qa_q1mpm_actor *trigger, qa_actor_id actor) {
  qa_q1mpm_game *game = runtime->game;
  qa_q1mpm_actor *entity = game->entity(game, QA_Q1MPM_SOME(actor));
  if (entity == NULL || (entity->movement_flags & 32) == 0 ||
      strcmp(entity->classname, "monster_decoy") == 0 ||
      qa_q1mpm_actor_number(entity, "wetsuit_time") > game->time)
    return;
  qa_q1mpm_mission_monster *monster = controller(runtime, entity);
  if (monster == NULL) return;
  qa_q1mpm_opt_actor enemy = monster->state.enemy;
  qa_vec3 start, end;
  bool has_start = qa_q1mpm_monster_eye(monster, &start);
  bool has_end;
  if (!enemy.has) {
    end = game->world == NULL ? QA_Q1MPM_ZERO : game->body(game, game->world).origin;
    has_end = true;
  } else {
    has_end = qa_q1mpm_monster_eye_actor(monster, enemy.id, &end);
  }
  if (!has_start || !has_end) return;
  qa_q1mpm_trace_request request = {start, end, QA_Q1MPM_POINT, QA_Q1MPM_SOME(actor), true, false};
  if (game->host->trace(game->host->ctx, &request).fraction == 1) return;
  if (enemy.has) {
    monster->state.old_enemy = enemy;
    monster->state.enemy = QA_Q1MPM_NONE;
    qa_q1mpm_monster_set_next_frame(monster, qa_q1mpm_monster_spec(monster)->walk);
    char name[192];
    snprintf(name, sizeof(name), "%s:monster_frame",
             monster->callback_prefix == NULL ? "base" : monster->callback_prefix);
    entity->think = game->named.action(game->named.ctx, entity, name);
  }
  qa_q1mpm_actor *found[1];
  size_t found_count = game->find(game, trigger->target, found, 1);
  qa_q1mpm_actor *target = found_count > 0 ? found[0] : NULL;
  copy_path(monster->state.path, sizeof(monster->state.path), target == NULL ? "" : target->targetname);
  qa_q1mpm_opt_actor target_id = target == NULL ? QA_Q1MPM_NONE : QA_Q1MPM_SOME(target->actor.id);
  qa_q1mpm_references_set(entity, "goalentity", target_id);
  qa_q1mpm_references_set(entity, "movetarget", target_id);
  entity->ideal_yaw = qa_q1mpm_yaw_for(qa_q1mpm_vsub(
      target == NULL ? QA_Q1MPM_ZERO : game->body(game, target).origin, qa_q1mpm_monster_origin(monster)));
  char buf[32];
  qa_q1mpm_format_fround(buf, sizeof(buf), game->time + 2);
  qa_q1mpm_fields_set(entity, "wetsuit_time", buf);
  if (target != NULL) return;
  if (monster->state.old_enemy.has) {
    qa_q1mpm_monster_found(monster, monster->state.old_enemy.id);
    return;
  }
  qa_q1mpm_opt_actor client = game->host->check_client(game->host->ctx, entity->actor);
  /* Original QC enters FoundTarget when checkclient returns world. */
  if (!client.has && game->world != NULL) {
    qa_q1mpm_monster_found(monster, game->world->actor.id);
    return;
  }
  monster->state.pause_until = game->time + 999999;
  qa_q1mpm_monster_play(monster, qa_q1mpm_monster_spec(monster)->stand);
}
static void follow_touch(void *ctx, qa_q1mpm_game *game, qa_q1mpm_actor *entity, qa_actor_id other,
                         bool has_normal, qa_vec3 normal, const void *surface) {
  (void)game; (void)has_normal; (void)normal; (void)surface;
  follow((qa_q1mpm_runtime *)ctx, entity, other);
}
typedef struct path_spawn_ctx { const char *classname; } path_spawn_ctx;
static path_spawn_ctx follow_ctx = {"path_follow"};
static path_spawn_ctx follow2_ctx = {"path_follow2"};
static void path_spawn(void *ctx, qa_q1mpm_game *source, qa_q1mpm_actor *entity) {
  const char *classname = ((path_spawn_ctx *)ctx)->classname;
  entity->solid = "trigger";
  entity->touch = source->named.touch(source->named.ctx, entity, "hipnotic:t_followtarget");
  if (strcmp(classname, "path_follow2") == 0) {
    qa_bounds bounds = {{-8, -8, -8}, {8, 8, 8}};
    source->set_bounds(source, entity, bounds);
    return;
  }
  entity->movement = "none";
  entity->model[0] = '\0';
  source->link(source, entity);
}
static bool move_target(void *ctx, qa_q1mpm_actor *corner, qa_q1mpm_actor *entity) {
  qa_q1mpm_runtime *runtime = ctx;
  qa_q1mpm_game *game = runtime->game;
  qa_q1mpm_mission_monster *monster = controller(runtime, entity);
  if (monster == NULL) return false;
  if (strcmp(monster->state.path, corner->targetname) != 0 || monster->state.enemy.has) return true;
  if (strcmp(entity->classname, "monster_ogre") == 0)
    game->sound(game, entity, "ogre/ogdrag.wav", QA_Q1MPM_CH("voice"), 2, 1);
  if (corner->target[0] != '\0') {
    qa_q1mpm_actor *found[1];
    size_t found_count = game->find(game, corner->target, found, 1);
    qa_q1mpm_actor *target = found_count > 0 ? found[0] : NULL;
    copy_path(monster->state.path, sizeof(monster->state.path), target == NULL ? "" : target->targetname);
    qa_q1mpm_opt_actor target_id = target == NULL ? QA_Q1MPM_NONE : QA_Q1MPM_SOME(target->actor.id);
    qa_q1mpm_references_set(entity, "goalentity", target_id);
    qa_q1mpm_references_set(entity, "movetarget", target_id);
    entity->ideal_yaw = qa_q1mpm_yaw_for(qa_q1mpm_vsub(
        target == NULL ? QA_Q1MPM_ZERO : game->body(game, target).origin, qa_q1mpm_monster_origin(monster)));
    if (target != NULL) {
      if (corner->delay != 0) {
        monster->state.pause_until = game->time + corner->delay;
        qa_q1mpm_monster_play(monster, qa_q1mpm_monster_spec(monster)->stand);
      }
      return true;
    }
  }
  monster->state.pause_until = game->time + 999999;
  qa_q1mpm_monster_play(monster, qa_q1mpm_monster_spec(monster)->stand);
  return true;
}
void qa_q1mpm_register_hipnotic_paths(qa_q1mpm_runtime *runtime) {
  qa_q1mpm_game *game = runtime->game;
  qa_q1mpm_handlers handlers = {runtime, NULL, follow_touch, NULL, NULL, NULL};
  game->named.register_named(game->named.ctx, "hipnotic:t_followtarget", &handlers);
  game->register_spawn(game, "path_follow", path_spawn, &follow_ctx);
  game->register_spawn(game, "path_follow2", path_spawn, &follow2_ctx);
  game->register_path_touch(game, "hipnotic:t_movetarget", move_target, runtime);
}
