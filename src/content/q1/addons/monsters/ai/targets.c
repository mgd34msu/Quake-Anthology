/* Port of src/content/q1/addons/monsters/ai/targets.ts. GPL-2.0-or-later. */
#include "qa/content/q1/addons/monsters/ai/targets.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

#define FR(x) ((double)(float)(x))
#define FLAGS(x) ((int32_t)(x))

static qa_q1am_actor *destination(qa_q1am_game *game, qa_q1am_actor *mover, const char *name) {
  size_t count = 0;
  qa_q1am_actor **found = game->find(game->ctx, name, &count);
  qa_q1am_actor *target = count > 0 ? found[0] : NULL;
  qa_actor_id id = target ? target->actor.id : (qa_actor_id){0};
  qa_q1am_actor_set_ref(mover, "goalentity", target != NULL, id);
  qa_q1am_actor_set_ref(mover, "movetarget", target != NULL, id);
  if (mover->has_monster)
    (void)snprintf(mover->monster.path, sizeof mover->monster.path, "%s", target ? name : "");
  mover->ideal_yaw = qa_q1am_yaw_for(qa_sub3(target ? game->body(game->ctx, target).origin : QA_Q1AM_ZERO,
                                             game->body(game->ctx, mover).origin));
  free(found);
  return target;
}
static void pause_target(qa_q1am_actor *mover, double until) {
  double value = FR(until);
  char text[64];
  qa_q1am_format_fround(text, sizeof text, value);
  qa_q1am_actor_set_field(mover, "pausetime", text);
  if (mover->has_monster) mover->monster.pause_until = value;
}
static void move_target(void *ctx, qa_q1am_game *game, qa_q1am_actor *corner, qa_actor_id other,
                        bool has_normal, qa_vec3 normal) {
  (void)ctx; (void)has_normal; (void)normal;
  qa_q1am_actor *mover = game->entity(game->ctx, true, other);
  if (mover == NULL) return;
  qa_actor_id movetarget;
  bool has_movetarget = qa_q1am_actor_ref(mover, "movetarget", &movetarget);
  bool has_enemy = (mover->has_monster && mover->monster.has_enemy) ||
                   qa_q1am_actor_ref(mover, "enemy", &(qa_actor_id){0});
  if (!qa_q1am_same_opt(has_movetarget, movetarget, true, corner->actor.id) || has_enemy) return;
  corner->has_owner = true;
  corner->owner = other;
  qa_actor_id previous_id;
  qa_q1am_actor *previous = qa_q1am_actor_ref(mover, "dmg_inflictor", &previous_id)
                                ? game->entity(game->ctx, true, previous_id) : NULL;
  if (previous && strcmp(previous->classname, "path_corner") == 0 &&
      qa_q1am_same_opt(previous->has_owner, previous->owner, true, previous->actor.id))
    previous->has_owner = false;
  qa_q1am_actor_set_ref(mover, "dmg_inflictor", true, corner->actor.id);
  double paused = mover->has_monster ? mover->monster.pause_until : qa_q1am_actor_number(mover, "pausetime");
  if (paused > game->time) return;
  if (strcmp(mover->classname, "monster_ogre") == 0)
    game->sound(game->ctx, mover, "ogre/ogdrag.wav", "voice", 2);
  qa_q1am_actor *target = destination(game, mover, corner->target);
  if (target == NULL || corner->wait != 0) {
    pause_target(mover, game->time + (target == NULL ? 999999 : corner->wait));
    if (mover->path_end.present) game->call_action(game->ctx, mover, mover->path_end);
  }
}
static void cancel_pause(void *ctx, qa_q1am_game *game, qa_q1am_actor *trigger, bool has_other,
                         qa_actor_id other, bool has_activator, qa_actor_id activator) {
  (void)ctx; (void)has_other; (void)other; (void)has_activator; (void)activator;
  size_t count = 0;
  qa_q1am_actor **movers = game->find(game->ctx, trigger->target, &count);
  for (size_t i = 0; i < count; i++) {
    qa_q1am_actor *mover = movers[i];
    if ((FLAGS(mover->movement_flags) & 32) == 0) continue;
    pause_target(mover, 0);
    char name[220];
    (void)snprintf(name, sizeof name, "%s:monster_use", qa_q1am_actor_text(mover, "source.monsterCallbackPrefix"));
    mover->use = game->named_use(game->ctx, mover, name);
  }
  free(movers);
}
static void switch_path(void *ctx, qa_q1am_game *game, qa_q1am_actor *trigger, bool has_other,
                        qa_actor_id other, bool has_activator, qa_actor_id activator) {
  (void)ctx; (void)has_other; (void)other; (void)has_activator; (void)activator;
  size_t count = 0;
  qa_q1am_actor **corners = game->find(game->ctx, trigger->target, &count);
  for (size_t i = 0; i < count; i++) {
    qa_q1am_actor *corner = corners[i];
    if (strcmp(corner->classname, "path_corner") != 0) continue;
    char old_target[128];
    (void)snprintf(old_target, sizeof old_target, "%s", corner->target);
    (void)snprintf(corner->target, sizeof corner->target, "%s", qa_q1am_actor_text(trigger, "netname"));
    qa_q1am_actor *mover = game->entity(game->ctx, corner->has_owner, corner->owner);
    if (mover) {
      size_t old_count = 0;
      qa_q1am_actor **old_found = game->find(game->ctx, old_target, &old_count);
      qa_actor_id movetarget;
      bool has_movetarget = qa_q1am_actor_ref(mover, "movetarget", &movetarget);
      bool matches = qa_q1am_same_opt(has_movetarget, movetarget, old_count > 0,
                                      old_count > 0 ? old_found[0]->actor.id : (qa_actor_id){0});
      free(old_found);
      if (matches) destination(game, mover, corner->target);
    }
  }
  free(corners);
}
typedef struct spawn_ctx { qa_q1am_game *game; char classname[64]; } spawn_ctx;
static void path_corner_spawn(void *ctx, qa_q1am_game *game, qa_q1am_actor *entity) {
  (void)ctx;
  if (entity->targetname[0] == '\0') qa_range_error("path_corner with no targetname.");
  if (entity->wait < 0) entity->wait = 999999;
  entity->solid = "trigger";
  entity->touch = game->named_touch(game->ctx, entity, "mg3:t_movetarget");
  game->set_bounds(game->ctx, entity, (qa_bounds){{-8, -8, -8}, {8, 8, 8}});
}
static void path_target_spawn(void *ctx, qa_q1am_game *game, qa_q1am_actor *entity) {
  spawn_ctx *state = ctx;
  char message[160];
  if (entity->target[0] == '\0') {
    (void)snprintf(message, sizeof message, "%s with no target given.", state->classname);
    qa_range_error("%s", message);
  }
  if (entity->targetname[0] == '\0') {
    (void)snprintf(message, sizeof message, "%s with no targetname given.", state->classname);
    qa_range_error("%s", message);
  }
  if (strcmp(state->classname, "target_switchpath") == 0 && qa_q1am_actor_text(entity, "netname")[0] == '\0')
    qa_range_error("target_switchtarget with no netname given.");
  char name[200];
  (void)snprintf(name, sizeof name, "mg3:%s_use", state->classname);
  entity->use = game->named_use(game->ctx, entity, name);
}
void qa_q1am_register_mg3_path_targets(qa_q1am_context *context) {
  qa_q1am_game *game = context->game;
  qa_q1am_handlers handlers = {0};
  handlers.touch = move_target;
  game->named_register(game->ctx, "mg3:t_movetarget", handlers);
  handlers.touch = NULL;
  handlers.use = cancel_pause;
  game->named_register(game->ctx, "mg3:target_cancelpause_use", handlers);
  handlers.use = switch_path;
  game->named_register(game->ctx, "mg3:target_switchpath_use", handlers);
  game->register_spawn(game->ctx, "path_corner", path_corner_spawn, NULL);
  for (int i = 0; i < 2; i++) {
    spawn_ctx *state = malloc(sizeof *state);
    if (!state) qa_range_error("MG3 path target allocation failed");
    state->game = game;
    (void)snprintf(state->classname, sizeof state->classname, "%s",
                   i == 0 ? "target_cancelpause" : "target_switchpath");
    game->register_spawn(game->ctx, state->classname, path_target_spawn, state);
  }
}
