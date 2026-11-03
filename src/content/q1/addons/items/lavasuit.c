/* Port of src/content/q1/addons/items/lavasuit.ts (quakec_mg3/items.qc powerup_touch and client.qc CheckPowerups).
 * GPL-2.0-or-later. */
#include "qa/content/q1/addons/items/lavasuit.h"

#include "qa/content/q1/addons/items/common.h"

static void lavasuit_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other,
                           const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)normal; (void)surface; /* TS handler ignores them */
  qa_q1ad_context *context = ctx;
  qa_q1_player_state *player = qa_q1_player(game, other);
  if (player == NULL || qa_q1_health(game, other) <= 0) return;
  qa_q1_give_powerup(game, player, "mg3:lavasuit", 30);
  qa_q1ad_set_player_number(context, other, "lavasuit_time", 1);
  qa_q1_msg_arg args[] = {{.text = "$mg3_qc_lavasuit"}};
  qa_q1_message(game, &other, "$qc_got_item", true, args, 1);
  qa_q1_sound(game, player->actor.id, "items/suit.wav", "item", 1, 1);
  qa_q1_effect(game, "pickup", qa_q1_get_body(game, entity).origin, &other, 1);
  qa_q1ad_set_text(&entity->model, "");
  entity->solid = "none";
  qa_q1_link(game, entity);
  entity->has_activator = true;
  entity->activator = other;
  qa_q1_use_targets(game, entity, &other);
  if (!qa_q1_live(game, entity)) return;
  if (game->options.coop) qa_q1ad_set_text(&entity->target, "");
  double respawn = game->options.coop ? 2.5 : game->options.deathmatch != 0 ? 60 : entity->wait;
  if (respawn > 0) qa_q1_schedule(game, entity, respawn, qa_q1_callback_registry_action(game->named, entity, "SUB_regen"));
  else qa_q1_cancel(game, entity);
}

static void spawn_lavasuit(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)qa_q1_services_precache_model(game, "progs/lavasuit.mdl");
  (void)qa_q1_services_precache_sound(game, "items/suit.wav");
  (void)qa_q1_services_precache_sound(game, "items/suit2.wav");
  qa_q1ad_set_text(&entity->model, "progs/lavasuit.mdl");
  (void)qa_q1_actor_field_set(entity, "netname", "$mg3_qc_lavasuit");
  entity->touch = qa_q1_callback_registry_touch(game->named, entity, QA_Q1AD_MG3_ITEM_PREFIX "lavasuit_touch");
  qa_q1_set_bounds(game, entity, (qa_bounds){{-16, -16, -24}, {16, 16, 32}});
  qa_q1ad_start_mg3_item(ctx, entity);
}

void qa_q1ad_register_mg3_lava_suit(qa_q1ad_context *context) {
  qa_q1_services *game = context->game;
  qa_q1_callback_registry_register(game->named, QA_Q1AD_MG3_ITEM_PREFIX "lavasuit_touch",
                                   (qa_q1_callback_handlers){.ctx = context, .touch = lavasuit_touch});
  qa_q1_register_spawn(game, "item_artifact_lavasuit", spawn_lavasuit, context);
}

void qa_q1ad_mg3_lava_suit_frame(qa_q1ad_context *context, qa_q1_player_state *player) {
  qa_q1_services *game = context->game;
  double expires = qa_q1_player_powerup_get(player, "mg3:lavasuit");
  if (expires == 0) return;
  qa_actor_id actor = player->actor.id;
  double warning = qa_q1ad_player_number(context, actor, "lavasuit_time");
  qa_body_state_result body = qa_shared_body_table_read(game->host.bodies, actor);
  if (expires < game->time + 3 && warning == 1) {
    qa_q1_message(game, &actor, "$mg3_qc_lavasuit_wearing_out", true, NULL, 0);
    qa_q1_sound(game, player->actor.id, "items/suit2.wav", "item", 1, 1);
    qa_q1ad_set_player_number(context, actor, "lavasuit_time", game->time + 1);
    if (body.has_value) qa_q1_effect(game, "pickup", body.value.origin, &actor, 1);
  } else if (expires < game->time + 3 && warning < game->time) {
    qa_q1ad_set_player_number(context, actor, "lavasuit_time", game->time + 1);
    if (body.has_value) qa_q1_effect(game, "pickup", body.value.origin, &actor, 1);
  }
  if (expires <= game->time) qa_q1ad_set_player_number(context, actor, "lavasuit_time", 0);
}
