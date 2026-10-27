/* Port of src/content/q1/addons/base-triggers.ts. GPL-2.0-or-later. */
#include "qa/content/q1/addons/base_triggers.h"

#include <stdio.h>
#include <string.h>

#include "qa/foundation/support.h"

static void prefix_of(qa_q1ad_context *context, char out[QA_Q1AD_NAME]) {
  (void)snprintf(out, QA_Q1AD_NAME, "%s:trigger:", qa_q1ad_program_name(context->program));
}
static bool grounded(qa_q1ad_context *context, qa_q1ad_actor *entity, const qa_actor_id *other) {
  if (context->program != QA_Q1AD_MG3 || ((int32_t)entity->spawnflags & 64) == 0) return true;
  if (other == NULL) return false;
  qa_q1ad_body body;
  return qa_q1ad_body_by_id(context, *other, &body) && body.has_ground;
}
static void multi_touch(qa_q1ad_context *context, qa_q1ad_actor *entity,
                        const qa_actor_id *other, const qa_vec3 *normal) {
  if (grounded(context, entity, other)) qa_q1ad_do_touch(context, entity, "multi_touch", other, normal);
}
static void multi_use(qa_q1ad_context *context, qa_q1ad_actor *entity,
                      const qa_actor_id *other, const qa_actor_id *activator) {
  if (grounded(context, entity, other)) qa_q1ad_do_use(context, entity, "multi_use", other, activator);
}
static void multi_killed(qa_q1ad_context *context, qa_q1ad_actor *entity, const qa_actor_id *attacker) {
  if (grounded(context, entity, attacker)) qa_q1ad_do_die(context, entity, "multi_killed", attacker);
}
static void multiple(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  if (qa_q1ad_removed_outside_coop(context, entity, true) ||
      qa_q1ad_removed_for_runes(context, entity)) return;
  qa_q1ad_actor_set(entity, "netname", "trigger_multiple");
  char prefix[QA_Q1AD_NAME];
  prefix_of(context, prefix);
  if (((int32_t)entity->spawnflags & 2) != 0) {
    entity->spawnflags = (double)((int32_t)entity->spawnflags & ~2);
    qa_q1ad_bind_prefix(entity->use, prefix, "activate_multi");
    return;
  }
  if (entity->wait == 0) entity->wait = 0.2;
  qa_q1ad_init_trigger(context, entity);
  if (!qa_q1ad_live(context, entity)) return;
  qa_q1ad_bind_prefix(entity->use, prefix, "multi_use");
  if (entity->max_health != 0) {
    if (((int32_t)entity->spawnflags & 1) != 0)
      qa_range_error("health and notouch don't make sense");
    entity->damageable = true;
    entity->solid = "bbox";
    qa_q1ad_bind_prefix(entity->die, prefix, "multi_killed");
  } else if (((int32_t)entity->spawnflags & 1) == 0)
    qa_q1ad_bind_prefix(entity->touch, prefix, "multi_touch");
}
static void activate_multi(qa_q1ad_context *context, qa_q1ad_actor *entity,
                           const qa_actor_id *other, const qa_actor_id *activator) {
  (void)other; (void)activator;
  multiple(context, entity);
}
static void spawn_multiple_kind(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  const char *classname = entity->classname != NULL ? entity->classname : "";
  if (qa_q1ad_removed_outside_coop(context, entity, true) ||
      qa_q1ad_removed_for_runes(context, entity)) return;
  if (strcmp(classname, "trigger_multiple") != 0) entity->wait = -1;
  if (strcmp(classname, "trigger_secret") == 0) {
    context->game.total_secrets++;
    if (context->program != QA_Q1AD_MG3 || ((int32_t)entity->spawnflags & 128) == 0) {
      if (entity->message == NULL || entity->message[0] == '\0') entity->message = "$qc_found_secret";
    }
    if (entity->sounds == 0) entity->sounds = 1;
  }
  multiple(context, entity);
  if (strcmp(classname, "trigger_once") == 0) qa_q1ad_actor_set(entity, "netname", "trigger_once");
}
static void spawn_teleport_like(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  bool relay = entity->classname != NULL && strcmp(entity->classname, "trigger_relay") == 0;
  if (qa_q1ad_removed_outside_coop(context, entity, relay) ||
      qa_q1ad_removed_for_runes(context, entity)) return;
  if (context->game.spawn_map_actor != NULL) context->game.spawn_map_actor(context->game.ctx, entity);
}
static void spawn_remaining(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  if (qa_q1ad_removed_outside_coop(context, entity, false) ||
      qa_q1ad_removed_for_runes(context, entity)) return;
  if (context->game.spawn_remaining_map_actor != NULL)
    context->game.spawn_remaining_map_actor(context->game.ctx, entity);
}

void qa_q1ad_register_base_triggers(qa_q1ad_context *context) {
  char prefix[QA_Q1AD_NAME], full[QA_Q1AD_NAME];
  prefix_of(context, prefix);
#define REG(name, handlers) do { qa_q1ad_named_name(full, prefix, name); \
  qa_q1ad_register_named(context, full, handlers); } while (0)
  REG("multi_touch", ((qa_q1ad_handlers){.touch = multi_touch}));
  REG("multi_use", ((qa_q1ad_handlers){.use = multi_use}));
  REG("multi_killed", ((qa_q1ad_handlers){.die = multi_killed}));
  REG("activate_multi", ((qa_q1ad_handlers){.use = activate_multi}));
#undef REG
  qa_q1ad_register_spawn(context, "trigger_multiple", false, spawn_multiple_kind);
  qa_q1ad_register_spawn(context, "trigger_once", false, spawn_multiple_kind);
  qa_q1ad_register_spawn(context, "trigger_secret", false, spawn_multiple_kind);
  qa_q1ad_register_spawn(context, "trigger_teleport", false, spawn_teleport_like);
  qa_q1ad_register_spawn(context, "trigger_relay", false, spawn_teleport_like);
  qa_q1ad_register_spawn(context, "trigger_onlyregistered", true, spawn_remaining);
  qa_q1ad_register_spawn(context, "trigger_monsterjump", true, spawn_remaining);
  qa_q1ad_register_spawn(context, "trigger_setskill", true, spawn_remaining);
}
