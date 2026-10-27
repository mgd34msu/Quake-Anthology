/* Port of src/content/q1/addons/items/index.ts. GPL-2.0-or-later. */
#include "qa/content/q1/addons/items/index.h"

#include "qa/content/q1/addons/items/common.h"
#include "qa/content/q1/addons/items/lavasuit.h"
#include "qa/content/q1/addons/items/pickups.h"
#include "qa/content/q1/addons/items/upgrades.h"
#include "qa/content/q1/addons/items/weapons.h"
#include "qa/foundation/support.h"

void qa_q1ad_mg3_items_attach(qa_q1ad_context *context, qa_q1ad_player *player) {
  qa_q1ad_initialize_mg3_capacities(context, player);
}
void qa_q1ad_mg3_items_frame(qa_q1ad_context *context, qa_q1ad_player *player) {
  qa_q1ad_mg3_weapon_frame(context, player);
  qa_q1ad_mg3_lava_suit_frame(context, player);
}

/** Register after the shared base arsenal and before player admission/map spawning. */
void qa_q1ad_register_mg3_items(qa_q1ad_context *context) {
  if (context->program != QA_Q1AD_MG3)
    qa_range_error("MG3 item registration requires its source program");
  qa_q1ad_register_mg3_item_callbacks(context);
  qa_q1ad_register_mg3_upgrades(context);
  qa_q1ad_register_mg3_weapons(context);
  qa_q1ad_register_mg3_pickups(context);
  qa_q1ad_register_mg3_lava_suit(context);
  if (context->game.register_rule != NULL) {
    context->game.register_rule(context->game.ctx, "q1:mg3");
    context->game.register_rule(context->game.ctx, "q1:mg3:items");
  }
}
