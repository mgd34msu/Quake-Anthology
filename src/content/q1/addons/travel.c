/* Port of src/content/q1/addons/travel.ts. GPL-2.0-or-later. */
#include "qa/content/q1/addons/travel.h"

#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/addons/campaign.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"

static char *dup_text(const char *text) {
  size_t length = strlen(text) + 1;
  char *copy = malloc(length);
  if (copy == NULL) qa_range_error("out of memory");
  memcpy(copy, text, length);
  return copy;
}

/* Deep copy of a travel extensions list (TS shares the array). */
static void copy_extensions(qa_q1_travel_state *target, const qa_q1_travel_state *source) {
  target->extensions = NULL;
  target->extension_count = 0;
  if (source->extension_count == 0) return;
  target->extensions = calloc(source->extension_count, sizeof *target->extensions);
  if (target->extensions == NULL) qa_range_error("out of memory");
  for (size_t i = 0; i < source->extension_count; i++) {
    target->extensions[i].id = dup_text(source->extensions[i].id);
    target->extensions[i].length = source->extensions[i].length;
    target->extensions[i].bytes = malloc(source->extensions[i].length != 0 ? source->extensions[i].length : 1);
    if (target->extensions[i].bytes == NULL) qa_range_error("out of memory");
    if (source->extensions[i].length != 0)
      memcpy(target->extensions[i].bytes, source->extensions[i].bytes, source->extensions[i].length);
    target->extension_count = i + 1;
  }
}

/* Deep copy of a whole state (TS returns the same immutable value). */
static qa_q1_travel_state copy_state(const qa_q1_travel_state *source) {
  qa_q1_travel_state copy = *source;
  size_t count = source->inventory_count;
  copy.inventory = malloc((count != 0 ? count : 1) * sizeof *copy.inventory);
  copy.inventory_items = malloc((count != 0 ? count : 1) * sizeof *copy.inventory_items);
  if (copy.inventory == NULL || copy.inventory_items == NULL) qa_range_error("out of memory");
  for (size_t i = 0; i < count; i++) {
    copy.inventory[i] = source->inventory[i];
    copy.inventory_items[i] = (qa_item_id)dup_text(source->inventory[i].item);
    copy.inventory[i].item = copy.inventory_items[i];
  }
  copy_extensions(&copy, source);
  return copy;
}

/* { ...newQ1AddonTravel(context), extensions: state.extensions } */
static qa_q1_travel_state fresh_with_extensions(qa_q1ad_context *context, const qa_q1_travel_state *state) {
  qa_q1_travel_state fresh = qa_q1ad_new_travel(context);
  for (size_t i = 0; i < fresh.extension_count; i++) { free(fresh.extensions[i].id); free(fresh.extensions[i].bytes); }
  free(fresh.extensions);
  copy_extensions(&fresh, state);
  return fresh;
}

qa_q1_travel_state qa_q1ad_new_travel(qa_q1ad_context *context) {
  qa_q1_travel_state state = qa_q1_new_travel(&context->game->options);
  if (context->program != QA_Q1AD_MG3 || context->game->options.deathmatch != 0) return state;
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

qa_q1_travel_state qa_q1ad_capture_travel(qa_q1ad_context *context, qa_owned_actor actor) {
  qa_q1_services *game = context->game;
  qa_q1_travel_state state = qa_q1_capture_travel(game, actor, NULL, NULL, (qa_q1_travel_policy){0});
  /* boss_end changes next-level parameters without changing the departing player's health. */
  if (context->program == QA_Q1AD_MG3 && game->world != NULL &&
      qa_q1_actor_number(game->world, "mg3.finalNewGameTravel") == 1) {
    state.health = 50;
    state.max_health = 50;
    return state;
  }
  if (qa_q1_health(game, actor.id) <= 0 || game->options.deathmatch != 0 || game->world_type == 3) {
    qa_q1_travel_state fresh = fresh_with_extensions(context, &state);
    qa_q1_travel_state_destroy(&state);
    return fresh;
  }
  return state;
}

qa_q1_travel_state qa_q1ad_decode_travel(qa_q1ad_context *context, const qa_q1_travel_state *state) {
  qa_q1_services *game = context->game;
  const char *map = game->map_name != NULL ? game->map_name : "";
  if (context->program == QA_Q1AD_MG3 && strcmp(map, "boss2") == 0 && qa_q1_option_skill(game) == 3) {
    qa_q1_campaign_binding campaign = qa_q1_base_campaign(context->base);
    campaign.write_flags(campaign.ctx, (double)(qa_js_to_int32(campaign.read_flags(campaign.ctx)) |
                                                QA_Q1AD_BLOODY_NIGHTMARE_ACTIVE | QA_Q1AD_BLOODY_NIGHTMARE_DISCOVERED));
  }
  if (strcmp(map, "start") == 0 || game->world_type == 3 ||
      (context->program != QA_Q1AD_MG3 && context->services.cvar(context->services.ctx, "horde") != 0))
    return fresh_with_extensions(context, state);
  return copy_state(state);
}

void qa_q1ad_admit_travel(qa_q1ad_context *context, qa_owned_actor actor, const qa_q1_travel_state *state) {
  qa_q1_travel_state decoded = qa_q1ad_decode_travel(context, state);
  qa_q1_admit_travel(context->game, actor, &decoded);
  qa_q1_travel_state_destroy(&decoded);
}
