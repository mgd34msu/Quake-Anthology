/* Port of src/content/q1/addons/base-triggers.ts (quakec_mg1/triggers.qc and
 * quakec_mg3/triggers.qc). GPL-2.0-or-later. */
#include "qa/content/q1/addons/base_triggers.h"

#include <math.h>
#include <string.h>

#include "qa/content/q1/base/map_entities.h"
#include "qa/content/q1/foundation/spawns.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"

/* `${program}:trigger:` names: static literals, since slots borrow the name. */
#define NAMED(context, name) \
  ((const char *const[]){"dopa:trigger:" name, "mg1:trigger:" name, "mg3:trigger:" name, \
                         "ctf:trigger:" name}[(context)->program])

static int32_t spawnflags(const qa_q1_actor *entity) { return qa_js_to_int32(entity->spawnflags); }
static bool falsy(double value) { return value == 0 || isnan(value); }

static bool grounded(qa_q1ad_context *context, qa_q1_actor *entity, const qa_actor_id *other) {
  if (context->program != QA_Q1AD_MG3 || (spawnflags(entity) & 64) == 0) return true;
  if (other == NULL) return false;
  qa_body_state_result body = qa_shared_body_table_read(context->game->host.bodies, *other);
  return body.has_value && body.value.ground != NULL;
}

static void multi_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other,
                        const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)surface;
  if (grounded(ctx, entity, &other))
    qa_q1_call_touch(game, entity, qa_q1_callback_registry_touch(game->named, entity, "multi_touch"), other, normal, NULL);
}
static void multi_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other,
                      const qa_actor_id *activator) {
  if (grounded(ctx, entity, other))
    qa_q1_call_use(game, entity, qa_q1_callback_registry_use(game->named, entity, "multi_use"), other, activator);
}
static void multi_killed(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *attacker) {
  if (grounded(ctx, entity, attacker))
    qa_q1_call_die(game, entity, qa_q1_callback_registry_die(game->named, entity, "multi_killed"), attacker);
}

static void multiple(qa_q1ad_context *context, qa_q1_actor *entity) {
  qa_q1_services *game = context->game;
  if (qa_q1ad_removed_outside_coop(context, entity, true) || qa_q1ad_removed_for_runes(context, entity)) return;
  (void)qa_q1_actor_field_set(entity, "netname", "trigger_multiple");
  if ((spawnflags(entity) & 2) != 0) {
    entity->spawnflags = (double)(spawnflags(entity) & ~2);
    entity->use = qa_q1_callback_registry_use(game->named, entity, NAMED(context, "activate_multi"));
    return;
  }
  if (falsy(entity->wait)) entity->wait = 0.2;
  qa_q1ad_init_trigger(context, entity);
  if (!qa_q1_live(game, entity)) return;
  entity->use = qa_q1_callback_registry_use(game->named, entity, NAMED(context, "multi_use"));
  if (entity->max_health != 0) {
    if ((spawnflags(entity) & 1) != 0) qa_range_error("health and notouch don't make sense");
    qa_q1_actor_set_damageable(entity, true);
    entity->solid = "bbox";
    entity->die = qa_q1_callback_registry_die(game->named, entity, NAMED(context, "multi_killed"));
  } else if ((spawnflags(entity) & 1) == 0) {
    entity->touch = qa_q1_callback_registry_touch(game->named, entity, NAMED(context, "multi_touch"));
  }
}

static void activate_multi(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other,
                           const qa_actor_id *activator) {
  (void)game; (void)other; (void)activator;
  multiple(ctx, entity);
}

static void spawn_multiple_kind(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_context *context = ctx;
  /* The classname the handler was registered under; spawn dispatch matches it. */
  bool once = strcmp(entity->classname, "trigger_once") == 0;
  bool secret = strcmp(entity->classname, "trigger_secret") == 0;
  bool multiple_kind = strcmp(entity->classname, "trigger_multiple") == 0;
  if (qa_q1ad_removed_outside_coop(context, entity, true) || qa_q1ad_removed_for_runes(context, entity)) return;
  if (!multiple_kind) entity->wait = -1;
  if (secret) {
    game->total_secrets++;
    if ((context->program != QA_Q1AD_MG3 || (spawnflags(entity) & 128) == 0) && entity->message[0] == '\0')
      qa_q1ad_set_text(&entity->message, "$qc_found_secret");
    if (falsy(entity->sounds)) entity->sounds = 1;
  }
  multiple(context, entity);
  if (once) (void)qa_q1_actor_field_set(entity, "netname", "trigger_once");
}

static void spawn_teleport_like(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_context *context = ctx;
  bool teleport = strcmp(entity->classname, "trigger_teleport") == 0;
  if (qa_q1ad_removed_outside_coop(context, entity, !teleport) || qa_q1ad_removed_for_runes(context, entity)) return;
  qa_q1_spawn_map_actor(game, entity);
  if (!teleport || !qa_q1_live(game, entity)) return;
  /* IGNORE_TARGETNAME (4) keeps the teleporter live without a firing; flag 8 (MG3) starts it dormant until first used. */
  if ((spawnflags(entity) & 4) != 0) (void)qa_q1_actor_field_set(entity, "teleport.ignoreTargetname", "1");
  if (context->program == QA_Q1AD_MG3 && (spawnflags(entity) & 8) != 0 && entity->targetname[0] != '\0') {
    entity->use = qa_q1_callback_registry_use(game->named, entity, "mg3:teleport_activate");
    entity->touch = (qa_q1_bound_touch){0};
  }
}

static void teleport_activate(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other,
                              const qa_actor_id *activator) {
  (void)ctx; (void)other; (void)activator;
  entity->use = qa_q1_callback_registry_use(game->named, entity, "teleport_use");
  entity->touch = qa_q1_callback_registry_touch(game->named, entity, "teleport_touch");
}

static void spawn_remaining(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)game;
  qa_q1ad_context *context = ctx;
  if (qa_q1ad_removed_outside_coop(context, entity, false) || qa_q1ad_removed_for_runes(context, entity)) return;
  qa_q1_spawn_remaining_map_actor(context->base, entity);
}

void qa_q1ad_register_base_triggers(qa_q1ad_context *context) {
  qa_q1_services *game = context->game;
  qa_q1_callback_registry *named = game->named;
  qa_q1_callback_registry_register(named, NAMED(context, "multi_touch"), (qa_q1_callback_handlers){.ctx = context, .touch = multi_touch});
  qa_q1_callback_registry_register(named, NAMED(context, "multi_use"), (qa_q1_callback_handlers){.ctx = context, .use = multi_use});
  qa_q1_callback_registry_register(named, NAMED(context, "multi_killed"), (qa_q1_callback_handlers){.ctx = context, .die = multi_killed});
  qa_q1_callback_registry_register(named, NAMED(context, "activate_multi"), (qa_q1_callback_handlers){.ctx = context, .use = activate_multi});
  static const char *const MULTIPLE[] = {"trigger_multiple", "trigger_once", "trigger_secret"};
  for (size_t i = 0; i < 3; i++) qa_q1_register_spawn(game, MULTIPLE[i], spawn_multiple_kind, context);
  qa_q1_register_spawn(game, "trigger_teleport", spawn_teleport_like, context);
  qa_q1_register_spawn(game, "trigger_relay", spawn_teleport_like, context);
  qa_q1_callback_registry_register(named, "mg3:teleport_activate", (qa_q1_callback_handlers){.ctx = context, .use = teleport_activate});
  static const char *const REMAINING[] = {"trigger_onlyregistered", "trigger_monsterjump", "trigger_setskill"};
  for (size_t i = 0; i < 3; i++) qa_q1_replace_spawn(game, REMAINING[i], spawn_remaining, context);
}
