/* Port of src/content/q1/addons/items/index.ts. GPL-2.0-or-later. */
#include "qa/content/q1/addons/items/index.h"

#include <stdlib.h>

#include "qa/content/q1/addons/items/common.h"
#include "qa/content/q1/addons/items/lavasuit.h"
#include "qa/content/q1/addons/items/pickups.h"
#include "qa/content/q1/addons/items/upgrades.h"
#include "qa/foundation/support.h"

static double weapon_rank(void *ctx, qa_q1_weapon weapon) {
  (void)ctx; /* mg3WeaponRank is a pure table */
  return qa_q1ad_mg3_weapon_rank(weapon);
}

static void attach(void *ctx, qa_q1_services *game, qa_q1_player_state *player) {
  (void)game; /* extension signature */
  qa_q1ad_initialize_mg3_capacities(ctx, player);
}

static bool inventory_capacity(void *ctx, qa_q1_services *game, qa_q1_player_state *player, qa_item_id item,
                               double *out) {
  (void)game; /* extension signature */
  return qa_q1ad_mg3_inventory_capacity(ctx, player, item, out);
}

static void frame(void *ctx, qa_q1_services *game, qa_q1_player_state *player, double seconds) {
  (void)game; (void)seconds; /* TS frame ignores them */
  qa_q1ad_mg3_weapon_frame(ctx, player);
  qa_q1ad_mg3_lava_suit_frame(ctx, player);
}

static bool capture_travel(void *ctx, qa_q1_services *game, qa_q1_player_state *player, uint8_t **out,
                           size_t *out_len) {
  (void)game; /* extension signature */
  *out = qa_q1ad_capture_mg3_upgrade_travel(ctx, player, out_len);
  return *out != NULL;
}

static void restore_travel(void *ctx, qa_q1_services *game, qa_q1_player_state *player, const uint8_t *bytes,
                           size_t len) {
  (void)game; /* extension signature */
  qa_q1ad_restore_mg3_upgrade_travel(ctx, player, bytes, len);
}

qa_q1ad_mg3_items *qa_q1ad_register_mg3_items(qa_q1ad_context *context) {
  if (context->program != QA_Q1AD_MG3) qa_range_error("MG3 item registration requires its source program");
  qa_q1ad_mg3_items *items = calloc(1, sizeof *items);
  if (items == NULL) return NULL;
  items->context = context;
  qa_q1_services *game = context->game;
  qa_q1ad_register_mg3_item_callbacks(context);
  qa_q1ad_register_mg3_upgrades(context);
  qa_q1ad_register_mg3_weapons(context, &items->weapons);
  qa_q1ad_register_mg3_pickups(context);
  qa_q1ad_register_mg3_lava_suit(context);
  items->pickup_rules = (qa_q1_pickup_rules){.ctx = context, .id = "q1:mg3", .weapon_rank = weapon_rank};
  qa_q1_register_pickup_rules(game, &items->pickup_rules);
  items->player_extension = (qa_q1_player_extension){
      .ctx = context, .id = "q1:mg3:items", .attach = attach, .inventory_capacity = inventory_capacity,
      .frame = frame, .capture_travel = capture_travel, .restore_travel = restore_travel};
  qa_q1_register_player_extension(game, &items->player_extension);
  return items;
}

void qa_q1ad_mg3_items_destroy(qa_q1ad_mg3_items *items) { free(items); }
