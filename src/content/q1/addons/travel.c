/* Port of src/content/q1/addons/travel.ts. GPL-2.0-or-later. */
#include "qa/content/q1/addons/travel.h"

#include <string.h>

#include "qa/content/q1/addons/campaign.h"

qa_q1ad_travel qa_q1ad_new_travel(const qa_q1ad_base_travel *base, qa_q1ad_context *context) {
  qa_q1ad_travel state = base->new_travel(base->ctx, context);
  if (context->program != QA_Q1AD_MG3 || context->game.deathmatch != 0) return state;
  state.health = 50;
  state.max_health = 50;
  for (size_t i = 0; i < state.inventory_count; i++) {
    const char *item = state.inventory[i].item;
    if (strcmp(item, "q1:ammo/shells") == 0) state.inventory[i].capacity = 50;
    else if (strcmp(item, "q1:ammo/nails") == 0) state.inventory[i].capacity = 100;
    else if (strcmp(item, "q1:ammo/rockets") == 0) state.inventory[i].capacity = 20;
  }
  return state;
}

qa_q1ad_travel qa_q1ad_capture_travel(const qa_q1ad_base_travel *base, qa_q1ad_context *context, qa_owned_actor actor) {
  qa_q1ad_travel state = base->capture(base->ctx, context, actor);
  qa_q1ad_actor *world = qa_q1ad_world(context);
  if (context->program == QA_Q1AD_MG3 && world != NULL &&
      qa_q1ad_actor_number(world, "mg3.finalNewGameTravel") == 1) {
    state.health = 50;
    state.max_health = 50;
    return state;
  }
  if (qa_q1ad_health(context, actor.id) <= 0 || context->game.deathmatch != 0 || context->game.world_type == 3) {
    qa_q1ad_travel fresh = qa_q1ad_new_travel(base, context);
    fresh.extension_bytes = state.extension_bytes;
    fresh.extension_count = state.extension_count;
    return fresh;
  }
  return state;
}

qa_q1ad_travel qa_q1ad_decode_travel(const qa_q1ad_base_travel *base, qa_q1ad_context *context, const qa_q1ad_travel *state) {
  const char *map = context->game.map_name != NULL ? context->game.map_name : "";
  if (context->program == QA_Q1AD_MG3 && strcmp(map, "boss2") == 0 && context->game.skill == 3 &&
      context->game.campaign_write_flags != NULL && context->game.campaign_read_flags != NULL)
    context->game.campaign_write_flags(context->game.ctx,
        (double)(int32_t)((int32_t)context->game.campaign_read_flags(context->game.ctx) |
            QA_Q1AD_BLOODY_NIGHTMARE_ACTIVE | QA_Q1AD_BLOODY_NIGHTMARE_DISCOVERED));
  if (strcmp(map, "start") == 0 || context->game.world_type == 3 ||
      (context->program != QA_Q1AD_MG3 && qa_q1ad_cvar(context, "horde") != 0)) {
    qa_q1ad_travel fresh = qa_q1ad_new_travel(base, context);
    fresh.extension_bytes = state->extension_bytes;
    fresh.extension_count = state->extension_count;
    return fresh;
  }
  return *state;
}

void qa_q1ad_admit_travel(const qa_q1ad_base_travel *base, qa_q1ad_context *context, qa_owned_actor actor, const qa_q1ad_travel *state) {
  qa_q1ad_travel decoded = qa_q1ad_decode_travel(base, context, state);
  base->admit(base->ctx, context, actor, &decoded);
}
