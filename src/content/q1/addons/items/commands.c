/* Port of src/content/q1/addons/items/commands.ts (quakec_mg3/weapons.qc equipment branches of ImpulseCommands).
 * GPL-2.0-or-later. */
#include "qa/content/q1/addons/items/commands.h"

#include <stdio.h>
#include <string.h>

#include "qa/content/q1/addons/items/pickups.h"
#include "qa/content/q1/addons/items/upgrades.h"
#include "qa/foundation/js.h"

static bool cheat_arsenal(qa_q1ad_context *context, qa_actor_id actor, const char *category) {
  /* services.cheatArsenal?.(actor, category) === true */
  return context->services.cheat_arsenal != NULL &&
         context->services.cheat_arsenal(context->services.ctx, actor, category);
}

static void count(qa_q1ad_context *context, qa_q1_player_state *player, qa_item_id item, double amount,
                  double capacity) {
  qa_q1_services *game = context->game;
  size_t entry_count = 0;
  const qa_inventory_entry *entries = qa_shared_inventory_table_entries(game->host.inventory, player->actor.id,
                                                                        &entry_count);
  qa_inventory_entry entry = {.item = item, .count = amount, .capacity = capacity};
  for (size_t i = 0; i < entry_count; i++)
    if (strcmp(entries[i].item, item) == 0) {
      entry = entries[i];
      entry.count = amount;
      break;
    }
  qa_shared_inventory_table_configure(game->host.inventory, player->actor, entry, NULL, NULL);
}

static void restock(qa_q1ad_context *context, qa_q1_player_state *player) {
  if (cheat_arsenal(context, player->actor.id, "ammo")) return;
  count(context, player, "q1:ammo/shells", 100, 100);
  count(context, player, "q1:ammo/nails", 200, 200);
  count(context, player, "q1:ammo/rockets", 100, 100);
  count(context, player, "q1:ammo/cells", 200, 100);
}

static bool enough_ammo(qa_q1ad_context *context, qa_q1_player_state *player, qa_q1_weapon weapon) {
  if (strcmp(weapon, "axe") == 0 || strcmp(weapon, "mg3:mjolnir") == 0) return true;
  qa_q1_services *game = context->game;
  qa_item_id item = qa_q1_services_weapon_ammo(game, weapon);
  double minimum = strcmp(weapon, "supershotgun") == 0 || strcmp(weapon, "supernailgun") == 0 ? 2 : 1;
  return item == NULL || qa_shared_inventory_table_count(game->host.inventory, player->actor.id, item) >= minimum;
}

static qa_q1_weapon melee(qa_q1ad_context *context, qa_q1_player_state *player) {
  qa_q1_services *game = context->game;
  return qa_shared_inventory_table_count(game->host.inventory, player->actor.id,
                                         qa_q1_services_weapon_item(game, "mg3:mjolnir")) != 0
             ? "mg3:mjolnir" : "axe";
}

static void cycle(qa_q1ad_context *context, qa_q1_player_state *player, bool reverse) {
  qa_q1_weapon order[9] = {melee(context, player), "shotgun", "supershotgun", "nailgun", "supernailgun",
                           "grenadelauncher", "rocketlauncher", "lightning", "mg3:laser"};
  int index = -1;
  if (strcmp(player->weapon, "axe") == 0 || strcmp(player->weapon, "mg3:mjolnir") == 0) index = 0;
  else
    for (int i = 0; i < 9 && index < 0; i++)
      if (strcmp(order[i], player->weapon) == 0) index = i;
  /* Native MG3 limits even an unrecognized source weapon to ten attempts. */
  if (index < 0) return;
  for (int remaining = 10; remaining > 0; remaining--) {
    index = (index + (reverse ? -1 : 1) + 9) % 9;
    qa_q1_weapon weapon = order[index];
    if (enough_ammo(context, player, weapon) && qa_q1_select_weapon(context->game, player->actor, weapon)) return;
  }
}

bool qa_q1ad_handle_mg3_item_impulse(qa_q1ad_context *context, qa_actor_id actor, double impulse,
                                     qa_q1ad_developer_message_fn developer_message, void *message_ctx) {
  if (context->program != QA_Q1AD_MG3) return false;
  qa_q1_services *game = context->game;
  qa_q1_player_state *player = qa_q1_player(game, actor);
  if (player == NULL) return false;
  if (impulse == 1 || impulse == 225) {
    qa_q1_weapon weapon = impulse == 1 ? melee(context, player) : "mg3:laser";
    if (qa_shared_inventory_table_count(game->host.inventory, actor, qa_q1_services_weapon_item(game, weapon)) == 0)
      qa_q1_message(game, &actor, "$qc_no_weapon", false, NULL, 0);
    else if (!enough_ammo(context, player, weapon)) qa_q1_message(game, &actor, "$qc_not_enough_ammo", false, NULL, 0);
    else (void)qa_q1_select_weapon(game, player->actor, weapon);
    return true;
  }
  if (impulse == 10 || impulse == 12) { cycle(context, player, impulse == 12); return true; }
  if (impulse == 9 || impulse == 99) {
    if ((game->options.deathmatch != 0 || game->options.coop) &&
        context->services.cvar(context->services.ctx, "sv_cheats") == 0)
      return true;
    restock(context, player);
    bool selected = cheat_arsenal(context, actor, "weapons");
    if (!selected) {
      for (size_t i = 0; i < 8; i++) count(context, player, qa_q1_services_weapon_item(game, QA_Q1_WEAPONS[i]), 1, 1);
      count(context, player, qa_q1_services_weapon_item(game, "mg3:laser"), 1, 1);
    }
    if (impulse != 99) {
      count(context, player, "q1:key/silver", 1, 1);
      count(context, player, "q1:key/gold", 1, 1);
    }
    if (!selected) (void)qa_q1_select_weapon(game, player->actor, "rocketlauncher");
    return true;
  }
  if (impulse == 100) {
    developer_message(message_ctx, "Resetting to defaults\n");
    player->max_health = 100;
    qa_gameplay_authority_set_health(game->host.combat, player->actor, 100);
    static const struct { qa_item_id item; double capacity; } DEFAULTS[] = {
        {"q1:ammo/shells", 100}, {"q1:ammo/nails", 200}, {"q1:ammo/rockets", 100}, {"q1:ammo/cells", 100}};
    for (size_t i = 0; i < sizeof DEFAULTS / sizeof DEFAULTS[0]; i++) {
      qa_q1ad_set_mg3_inventory_capacity(context, player, DEFAULTS[i].item, DEFAULTS[i].capacity);
      qa_inventory_entry entry = {
          .item = DEFAULTS[i].item, .capacity = DEFAULTS[i].capacity,
          .count = qa_shared_inventory_table_count(game->host.inventory, actor, DEFAULTS[i].item)};
      qa_shared_inventory_table_configure(game->host.inventory, player->actor, entry, NULL, NULL);
    }
    return true;
  }
  if (impulse >= 111 && impulse <= 115) {
    static const char *const TYPES[] = {"health", "shells", "nails", "rockets", "cells"};
    (void)qa_q1ad_give_next_mg3_upgrade(context, TYPES[(int)impulse - 111], player);
    return true;
  }
  if (impulse == 118) {
    count(context, player, qa_q1_services_weapon_item(game, "mg3:mjolnir"), 1, 1);
    return true;
  }
  if (impulse == 122) {
    developer_message(message_ctx, "$m_inf_ammo");
    bool enabled = qa_q1ad_player_number(context, actor, "infiniteammo") == 0;
    qa_q1ad_set_player_number(context, actor, "infiniteammo", enabled ? 1 : 0);
    if (enabled) restock(context, player);
    return true;
  }
  if (impulse == 227 || impulse == 228) {
    int32_t flag = impulse == 227 ? QA_Q1AD_MG3_BLOODY_SHOTGUN : QA_Q1AD_MG3_BLOODY_SUPER_SHOTGUN;
    int32_t flags = qa_js_to_int32(qa_q1ad_player_number(context, actor, "parm15"));
    bool enabled = (flags & flag) == 0;
    qa_q1ad_set_player_number(context, actor, "parm15", (double)(flags ^ flag));
    char text[96];
    (void)snprintf(text, sizeof text, "%s 'bloody %s' upgrade\n", enabled ? "activated" : "deactivated",
                   impulse == 227 ? "shotgun" : "Super Shotgun");
    developer_message(message_ctx, text);
    return true;
  }
  return false;
}
