/* Port of src/content/q1/addons/items/common.ts (quakec_mg3/items.qc and subs.qc). GPL-2.0-or-later. */
#include "qa/content/q1/addons/items/common.h"

#include <string.h>

#include "qa/foundation/js.h"

/** items.qc draught_fx: the ring hum and teleport particles, once per ring. */
void qa_q1ad_ring_effect(qa_q1ad_context *context, qa_q1_actor *entity, double offset_z) {
  qa_q1_services *game = context->game;
  if (entity->count != 0) return;
  entity->count = 1;
  qa_vec3 origin = qa_q1_vadd(qa_q1_get_body(game, entity).origin, (qa_vec3){0, 0, offset_z});
  qa_q1_emit(game, (qa_q1_event){.kind = "ambient", .has_origin = true, .origin = origin,
                                 .path = "ambience/hum1.wav", .volume = 0.7, .attenuation = 3});
  qa_q1_actor *particles = qa_q1_create(game, "particle_tele", NULL, NULL);
  qa_q1ad_set_number(context, particles, "distance", 48);
  qa_q1_set_origin(game, particles, origin);
  qa_q1_spawn_entity(game, particles);
}

void qa_q1ad_start_mg3_item(qa_q1ad_context *context, qa_q1_actor *entity) {
  qa_q1_services *game = context->game;
  if (qa_q1ad_removed_outside_coop(context, entity, true) || qa_q1ad_removed_for_runes(context, entity)) return;
  qa_q1ad_set_text(&entity->original_model, entity->model);
  if ((qa_js_to_int32(entity->spawnflags) & QA_Q1AD_MG3_SPAWNED_ITEM) != 0)
    entity->use = qa_q1_callback_registry_use(game->named, entity, QA_Q1AD_MG3_ITEM_PREFIX "regenerate");
  qa_q1_schedule(game, entity, 0.2, qa_q1_callback_registry_action(game->named, entity, QA_Q1AD_MG3_ITEM_PREFIX "place"));
}

static bool live(qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other) {
  return qa_q1_live(game, entity) && qa_session_actor_registry_is_live(game->host.actors, other);
}

void qa_q1ad_finish_mg3_pickup(qa_q1ad_context *context, qa_q1_actor *entity, qa_actor_id other,
                               const char *message, const char *sound, const qa_q1_msg_arg *args, size_t arg_count) {
  qa_q1_services *game = context->game;
  qa_owned_actor player;
  if (!qa_session_actor_registry_resolve_owned(game->host.actors, other, &player)) return;
  if (message[0] != '\0') qa_q1_message(game, &other, message, true, args, arg_count);
  if (!live(game, entity, other)) return;
  qa_q1_sound(game, player.id, sound, "item", 1, 1);
  if (!live(game, entity, other)) return;
  qa_q1_effect(game, "pickup", qa_q1_get_body(game, entity).origin, &other, 1);
  if (!live(game, entity, other)) return;
  entity->has_activator = true;
  entity->activator = other;
  qa_q1_use_targets(game, entity, &other);
  if (qa_q1_live(game, entity)) qa_q1_remove(game, entity);
}

static void place_action(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx; /* TS handler reads no context state */
  qa_q1_call_action(game, entity, qa_q1_callback_registry_action(game->named, entity, "PlaceItem"));
  if (qa_q1_live(game, entity) && (qa_js_to_int32(entity->spawnflags) & QA_Q1AD_MG3_SPAWNED_ITEM) != 0) {
    entity->solid = "none";
    qa_q1ad_set_text(&entity->model, "");
    qa_q1_link(game, entity);
  }
}

static void regenerate_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other,
                           const qa_actor_id *activator) {
  (void)other; (void)activator; /* TS handler ignores them */
  qa_q1_call_action(game, entity, qa_q1_callback_registry_action(game->named, entity, "SUB_regen"));
  if (strcmp(entity->classname, "item_draught_stupor") == 0 || strcmp(entity->classname, "item_draught_insight") == 0)
    qa_q1ad_ring_effect(ctx, entity, 0);
}

void qa_q1ad_register_mg3_item_callbacks(qa_q1ad_context *context) {
  qa_q1_services *game = context->game;
  qa_q1_callback_registry_register(game->named, QA_Q1AD_MG3_ITEM_PREFIX "place",
                                   (qa_q1_callback_handlers){.ctx = context, .action = place_action});
  qa_q1_callback_registry_register(game->named, QA_Q1AD_MG3_ITEM_PREFIX "regenerate",
                                   (qa_q1_callback_handlers){.ctx = context, .use = regenerate_use});
}
