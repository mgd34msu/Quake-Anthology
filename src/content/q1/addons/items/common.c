/* Port of src/content/q1/addons/items/common.ts. GPL-2.0-or-later. */
#include "qa/content/q1/addons/items/common.h"

void qa_q1ad_start_mg3_item(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  if (qa_q1ad_removed_outside_coop(context, entity, true) ||
      qa_q1ad_removed_for_runes(context, entity)) return;
  entity->original_model = entity->model;
  if (((int32_t)entity->spawnflags & QA_Q1AD_MG3_SPAWNED_ITEM) != 0)
    qa_q1ad_bind(entity->use, QA_Q1AD_MG3_ITEM_PREFIX "regenerate");
  qa_q1ad_schedule(context, entity, 0.2, QA_Q1AD_MG3_ITEM_PREFIX "place");
}
static bool actor_live(qa_q1ad_context *context, qa_actor_id actor) {
  return context->game.is_live == NULL || context->game.is_live(context->game.ctx, actor);
}
void qa_q1ad_finish_mg3_pickup(qa_q1ad_context *context, qa_q1ad_actor *entity, qa_actor_id other,
                               const char *message, const char *sound,
                               const qa_q1ad_msg_arg *args, size_t arg_count) {
  qa_owned_actor player;
  if (!qa_q1ad_resolve_owned(context, other, &player)) return;
  if (message[0] != '\0') qa_q1ad_message(context, other, message, true, args, arg_count);
  if (!qa_q1ad_live(context, entity) || !actor_live(context, other)) return;
  qa_q1ad_sound_actor(context, player.id, sound, "item");
  if (!qa_q1ad_live(context, entity) || !actor_live(context, other)) return;
  qa_q1ad_effect(context, "pickup", qa_q1ad_body_of(context, entity).origin, &other, 0);
  if (!qa_q1ad_live(context, entity) || !actor_live(context, other)) return;
  entity->activator = other;
  entity->has_activator = true;
  qa_q1ad_use_targets(context, entity, &other);
  if (qa_q1ad_live(context, entity)) qa_q1ad_remove(context, entity);
}
static void place_action(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  qa_q1ad_do_action(context, entity, "PlaceItem");
  if (qa_q1ad_live(context, entity) &&
      ((int32_t)entity->spawnflags & QA_Q1AD_MG3_SPAWNED_ITEM) != 0) {
    entity->solid = "none";
    entity->model = "";
    qa_q1ad_link(context, entity);
  }
}
static void regenerate_use(qa_q1ad_context *context, qa_q1ad_actor *entity,
                           const qa_actor_id *other, const qa_actor_id *activator) {
  (void)other; (void)activator;
  qa_q1ad_do_action(context, entity, "SUB_regen");
}
void qa_q1ad_register_mg3_item_callbacks(qa_q1ad_context *context) {
  qa_q1ad_register_named(context, QA_Q1AD_MG3_ITEM_PREFIX "place",
      (qa_q1ad_handlers){.action = place_action});
  qa_q1ad_register_named(context, QA_Q1AD_MG3_ITEM_PREFIX "regenerate",
      (qa_q1ad_handlers){.use = regenerate_use});
}
