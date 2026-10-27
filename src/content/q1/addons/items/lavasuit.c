/* Port of src/content/q1/addons/items/lavasuit.ts. GPL-2.0-or-later. */
#include "qa/content/q1/addons/items/lavasuit.h"

#include "qa/content/q1/addons/items/common.h"

static void lavasuit_touch(qa_q1ad_context *context, qa_q1ad_actor *entity,
                           const qa_actor_id *other, const qa_vec3 *normal) {
  (void)normal;
  if (other == NULL) return;
  qa_q1ad_player *player = qa_q1ad_player_state(context, *other);
  if (player == NULL || qa_q1ad_health(context, *other) <= 0) return;
  if (context->game.give_powerup != NULL)
    context->game.give_powerup(context->game.ctx, player, "mg3:lavasuit", 30);
  qa_q1ad_set_player_number(context, *other, "lavasuit_time", 1);
  qa_q1ad_msg_arg args[] = {{.text = "$mg3_qc_lavasuit"}};
  qa_q1ad_message(context, *other, "$qc_got_item", true, args, 1);
  qa_q1ad_sound_actor(context, player->actor.id, "items/suit.wav", "item");
  qa_q1ad_effect(context, "pickup", qa_q1ad_body_of(context, entity).origin, other, 0);
  entity->model = "";
  entity->solid = "none";
  qa_q1ad_link(context, entity);
  entity->activator = *other;
  entity->has_activator = true;
  qa_q1ad_use_targets(context, entity, other);
  if (!qa_q1ad_live(context, entity)) return;
  if (context->game.coop) entity->target = "";
  double respawn = context->game.coop ? 2.5 : context->game.deathmatch != 0 ? 60 : entity->wait;
  if (respawn > 0) qa_q1ad_schedule(context, entity, respawn, "SUB_regen");
  else qa_q1ad_cancel(context, entity);
}
static void spawn_lavasuit(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  if (context->game.precache_model != NULL) (void)context->game.precache_model(context->game.ctx, "progs/lavasuit.mdl");
  if (context->game.precache_sound != NULL) {
    (void)context->game.precache_sound(context->game.ctx, "items/suit.wav");
    (void)context->game.precache_sound(context->game.ctx, "items/suit2.wav");
  }
  entity->model = "progs/lavasuit.mdl";
  qa_q1ad_actor_set(entity, "netname", "$mg3_qc_lavasuit");
  qa_q1ad_bind(entity->touch, QA_Q1AD_MG3_ITEM_PREFIX "lavasuit_touch");
  qa_q1ad_set_bounds(context, entity, (qa_bounds){{-16, -16, -24}, {16, 16, 32}});
  qa_q1ad_start_mg3_item(context, entity);
}
void qa_q1ad_register_mg3_lava_suit(qa_q1ad_context *context) {
  qa_q1ad_register_named(context, QA_Q1AD_MG3_ITEM_PREFIX "lavasuit_touch",
      (qa_q1ad_handlers){.touch = lavasuit_touch});
  qa_q1ad_register_spawn(context, "item_artifact_lavasuit", false, spawn_lavasuit);
}
void qa_q1ad_mg3_lava_suit_frame(qa_q1ad_context *context, qa_q1ad_player *player) {
  double expires = qa_q1ad_player_powerup(player, "mg3:lavasuit");
  if (expires == 0) return;
  qa_actor_id actor = player->actor.id;
  double warning = qa_q1ad_player_number(context, actor, "lavasuit_time");
  qa_q1ad_body body;
  bool has_body = qa_q1ad_body_by_id(context, actor, &body);
  if (expires < context->game.time + 3 && warning == 1) {
    qa_q1ad_message(context, actor, "$mg3_qc_lavasuit_wearing_out", true, NULL, 0);
    qa_q1ad_sound_actor(context, player->actor.id, "items/suit2.wav", "item");
    qa_q1ad_set_player_number(context, actor, "lavasuit_time", context->game.time + 1);
    if (has_body) qa_q1ad_effect(context, "pickup", body.origin, &actor, 0);
  } else if (expires < context->game.time + 3 && warning < context->game.time) {
    qa_q1ad_set_player_number(context, actor, "lavasuit_time", context->game.time + 1);
    if (has_body) qa_q1ad_effect(context, "pickup", body.origin, &actor, 0);
  }
  if (expires <= context->game.time) qa_q1ad_set_player_number(context, actor, "lavasuit_time", 0);
}
