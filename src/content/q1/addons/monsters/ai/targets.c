/* Port of src/content/q1/addons/monsters/ai/targets.ts.
 * quakec_mg3/ai.qc path_corner and path control targets. GPL-2.0-or-later. */
#include "qa/content/q1/addons/monsters/ai/targets.h"
#include "qa/foundation/support.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/persistence/value.h"
#include "../support.h"

static bool classname_is(const qa_q1_actor *entity, const char *classname) {
  return entity->classname != NULL && strcmp(entity->classname, classname) == 0;
}
static const qa_actor_id *reference(const qa_q1_actor *entity, const char *name, qa_actor_id *storage) {
  return qa_q1_actor_reference_get(entity, name, storage) ? storage : NULL;
}
static bool destination(qa_q1_services *game, qa_q1_actor *mover, const char *name) {
  qa_actor_id target;
  const bool found = qa_q1_path_goal(game, name, &target);
  qa_q1am_reference(mover, "goalentity", found ? &target : NULL);
  qa_q1am_reference(mover, "movetarget", found ? &target : NULL);
  if (mover->monster != NULL) qa_q1ad_set_text(&mover->monster->path, found ? name : "");
  qa_q1_body body;
  const qa_vec3 a = found && qa_q1am_read_body(game, target, &body) ? body.origin : QA_Q1_ZERO;
  const qa_vec3 b = qa_q1_get_body(game, mover).origin;
  mover->ideal_yaw = qa_q1_yaw_for(qa_sub3(a, b));
  return found;
}
static void pause_until(qa_q1_actor *mover, double until) {
  const double value = QA_Q1AM_FR(until);
  qa_q1am_field_number(mover, "pausetime", value);
  if (mover->monster != NULL) mover->monster->pause_until = value;
}
static void move_target(void *ctx, qa_q1_services *game, qa_q1_actor *corner, qa_actor_id other,
                        const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)ctx; (void)normal; (void)surface; /* TS moveTarget(game, corner, other) */
  qa_q1_actor *mover = qa_q1_get_entity(game, &other);
  if (mover == NULL) return;
  qa_actor_id movetarget, enemy;
  const bool has_enemy = mover->monster != NULL && mover->monster->has_enemy
                             ? true : reference(mover, "enemy", &enemy) != NULL;
  if (!qa_q1am_same(reference(mover, "movetarget", &movetarget), &corner->actor.id) || has_enemy) return;
  corner->has_owner = true;
  corner->owner = other;
  qa_actor_id inflictor;
  qa_q1_actor *previous = qa_q1_get_entity(game, reference(mover, "dmg_inflictor", &inflictor));
  if (previous != NULL && classname_is(previous, "path_corner") &&
      qa_q1am_same(previous->has_owner ? &previous->owner : NULL, &previous->actor.id))
    previous->has_owner = false;
  qa_q1am_reference(mover, "dmg_inflictor", &corner->actor.id);
  if ((mover->monster != NULL ? mover->monster->pause_until : qa_q1_actor_number(mover, "pausetime")) > game->time)
    return;
  if (classname_is(mover, "monster_ogre")) qa_q1_sound(game, mover->actor.id, "ogre/ogdrag.wav", "voice", 2, 1);
  const bool found = destination(game, mover, corner->target);
  if (!found || corner->wait != 0) {
    pause_until(mover, game->time + (!found ? 999999 : corner->wait));
    qa_q1_call_action(game, mover, mover->path_end);
  }
}
static void cancel_pause(void *ctx, qa_q1_services *game, qa_q1_actor *trigger, const qa_actor_id *other,
                         const qa_actor_id *activator) {
  (void)ctx; (void)other; (void)activator; /* TS cancelPause(game, trigger) */
  size_t count = 0;
  qa_q1_actor **movers = qa_q1am_find(game, trigger->target, &count);
  for (size_t i = 0; i < count; i++) {
    qa_q1_actor *mover = movers[i];
    if ((QA_Q1AM_FLAGS(mover->movement_flags) & 32) == 0) continue;
    pause_until(mover, 0);
    char name[256];
    (void)snprintf(name, sizeof name, "%s:monster_use", qa_q1_actor_text(mover, "source.monsterCallbackPrefix"));
    mover->use = qa_q1_callback_registry_use(game->named, mover, name);
  }
  free(movers);
}
static void switch_path(void *ctx, qa_q1_services *game, qa_q1_actor *trigger, const qa_actor_id *other,
                        const qa_actor_id *activator) {
  (void)ctx; (void)other; (void)activator; /* TS switchPath(game, trigger) */
  size_t count = 0;
  qa_q1_actor **corners = qa_q1am_find(game, trigger->target, &count);
  for (size_t i = 0; i < count; i++) {
    qa_q1_actor *corner = corners[i];
    if (!classname_is(corner, "path_corner")) continue;
    char *old_target = corner->target;
    corner->target = NULL;
    qa_q1ad_set_text(&corner->target, qa_q1_actor_text(trigger, "netname"));
    if (corner->target == NULL) qa_range_error("out of memory");
    qa_q1_actor *mover = qa_q1_get_entity(game, corner->has_owner ? &corner->owner : NULL);
    if (mover != NULL) {
      qa_actor_id movetarget;
      qa_q1_actor *old = qa_q1am_find_first(game, old_target);
      if (qa_q1am_same(reference(mover, "movetarget", &movetarget), old == NULL ? NULL : &old->actor.id))
        (void)destination(game, mover, corner->target);
    }
    free(old_target);
  }
  free(corners);
}
static void path_corner_spawn(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx; /* the TS closure reads only the game */
  if (entity->targetname[0] == '\0') qa_range_error("path_corner with no targetname.");
  if (entity->wait < 0) entity->wait = 999999;
  entity->solid = "trigger";
  entity->touch = qa_q1_callback_registry_touch(game->named, entity, "mg3:t_movetarget");
  qa_q1_set_bounds(game, entity, (qa_bounds){{-8, -8, -8}, {8, 8, 8}});
}
/* The TS loop variable each spawn closure captures. */
static const char *const path_target_classnames[2] = {"target_cancelpause", "target_switchpath"};
static void path_target_spawn(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  const char *classname = *(const char *const *)ctx;
  if (entity->target[0] == '\0') qa_range_error("%s with no target given.", classname);
  if (entity->targetname[0] == '\0') qa_range_error("%s with no targetname given.", classname);
  if (strcmp(classname, "target_switchpath") == 0 && qa_q1_actor_text(entity, "netname")[0] == '\0')
    qa_range_error("target_switchtarget with no netname given.");
  char name[64];
  (void)snprintf(name, sizeof name, "mg3:%s_use", classname);
  entity->use = qa_q1_callback_registry_use(game->named, entity, name);
}
/* The "mg3:world-words" extension saves game.sourceWorldWords as [{name, value}]. */
static void must(bool ok) { if (!ok) qa_range_error("out of memory"); }
static bool world_words_capture(void *ctx, uint8_t **out, size_t *out_length) {
  qa_q1_services *game = ((qa_q1ad_context *)ctx)->game;
  qa_save_value *list = qa_save_value_array();
  if (list == NULL) return false;
  for (size_t i = 0; i < game->source_world_word_count; i++) {
    qa_save_value *object = qa_save_value_object();
    qa_save_value *name = qa_save_value_string(game->source_world_words[i].name);
    qa_save_value *value = qa_save_value_number(game->source_world_words[i].value);
    must(object != NULL && name != NULL && value != NULL);
    must(qa_save_value_set(object, "name", name) && qa_save_value_set(object, "value", value));
    must(qa_save_value_push(list, object));
  }
  *out = qa_encode_checkpoint_value(list, out_length);
  qa_save_value_free(list);
  return *out != NULL;
}
static void q1_free_decoded_save(void *value) { qa_save_value_free(value); }
static void world_words_restore(void *ctx, const uint8_t *bytes, size_t length) {
  qa_q1_services *game = ((qa_q1ad_context *)ctx)->game;
  qa_q1_source_world_words_clear(game);
  qa_save_value *decoded = qa_decode_checkpoint_value(bytes, length);
  qa_cleanup decoded_cleanup; /* a malformed save raises from the readers */
  qa_cleanup_push(&decoded_cleanup, q1_free_decoded_save, decoded);
  qa_save_reader root;
  must(qa_save_reader_init(&root, decoded, "mg3:world-words"));
  const size_t count = qa_save_reader_list_count(&root);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader item, name, value;
    must(qa_save_reader_item(&root, i, &item));
    must(qa_save_reader_field(&item, "name", &name));
    char *text = strdup(qa_save_reader_string(&name));
    if (text == NULL) qa_range_error("out of memory");
    qa_save_reader_destroy(&name);
    must(qa_save_reader_field(&item, "value", &value));
    const double number = qa_save_reader_number(&value);
    qa_save_reader_destroy(&value);
    qa_q1_source_world_word_set(game, text, number);
    free(text);
    qa_save_reader_destroy(&item);
  }
  qa_save_reader_destroy(&root);
  qa_cleanup_pop(&decoded_cleanup);
  qa_save_value_free(decoded);
}
static void world_words_restore_absent(void *ctx) { qa_q1_source_world_words_clear(((qa_q1ad_context *)ctx)->game); }

void qa_q1am_register_mg3_path_targets(qa_q1ad_context *context) {
  qa_q1_services *game = context->game;
  context->world_words_extension = (qa_q1_state_extension){
    .ctx = context, .id = "mg3:world-words", .capture = world_words_capture, .restore = world_words_restore,
  };
  qa_q1_register_state_extension(game, &context->world_words_extension);
  qa_q1_set_state_extension_restore_absent(game, &context->world_words_extension, world_words_restore_absent);
  qa_q1_callback_handlers handlers;
  memset(&handlers, 0, sizeof handlers);
  handlers.ctx = context;
  handlers.touch = move_target;
  qa_q1_callback_registry_register(game->named, "mg3:t_movetarget", handlers);
  handlers.touch = NULL;
  handlers.use = cancel_pause;
  qa_q1_callback_registry_register(game->named, "mg3:target_cancelpause_use", handlers);
  handlers.use = switch_path;
  qa_q1_callback_registry_register(game->named, "mg3:target_switchpath_use", handlers);
  qa_q1_register_spawn(game, "path_corner", path_corner_spawn, context);
  for (size_t i = 0; i < 2; i++)
    qa_q1_register_spawn(game, path_target_classnames[i], path_target_spawn, (void *)&path_target_classnames[i]);
}
