/* Port of src/content/q1/addons/items/commands.ts. GPL-2.0-or-later. */
#include "qa/content/q1/addons/items/commands.h"

#include <stdio.h>
#include <string.h>

#include "qa/content/q1/addons/items/pickups.h"
#include "qa/content/q1/addons/items/upgrades.h"

static bool cheat(qa_q1ad_context *context, qa_actor_id actor, const char *category) {
  return context->services.has_cheat_arsenal && context->services.cheat_arsenal != NULL &&
      context->services.cheat_arsenal(context->services.ctx, actor, category);
}
static void set_count(qa_q1ad_context *context, qa_q1ad_player *player, qa_item_id item,
                      double amount, double capacity) {
  qa_q1ad_inventory_entry entries[32];
  size_t count = context->game.inventory_entries != NULL
      ? context->game.inventory_entries(context->game.ctx, player->actor.id, entries, 32) : 0;
  for (size_t i = 0; i < count; i++)
    if (strcmp(entries[i].item, item) == 0) capacity = entries[i].capacity;
  if (context->game.inventory_configure != NULL)
    context->game.inventory_configure(context->game.ctx, player->actor, item, amount, capacity);
}
static void restock(qa_q1ad_context *context, qa_q1ad_player *player) {
  if (cheat(context, player->actor.id, "ammo")) return;
  set_count(context, player, "q1:ammo/shells", 100, 100);
  set_count(context, player, "q1:ammo/nails", 200, 200);
  set_count(context, player, "q1:ammo/rockets", 100, 100);
  set_count(context, player, "q1:ammo/cells", 200, 100);
}
static double inventory_count(qa_q1ad_context *context, qa_actor_id actor, qa_item_id item) {
  return context->game.inventory_count != NULL
      ? context->game.inventory_count(context->game.ctx, actor, item) : 0;
}
static bool enough_ammo(qa_q1ad_context *context, qa_q1ad_player *player, const char *weapon) {
  if (strcmp(weapon, "axe") == 0 || strcmp(weapon, "mg3:mjolnir") == 0) return true;
  const char *item = context->game.weapon_ammo != NULL
      ? context->game.weapon_ammo(context->game.ctx, weapon) : NULL;
  double minimum = strcmp(weapon, "supershotgun") == 0 || strcmp(weapon, "supernailgun") == 0 ? 2 : 1;
  return item == NULL || inventory_count(context, player->actor.id, item) >= minimum;
}
static const char *melee(qa_q1ad_context *context, qa_q1ad_player *player) {
  char item[64];
  return inventory_count(context, player->actor.id, qa_q1ad_weapon_item("mg3:mjolnir", item)) != 0
      ? "mg3:mjolnir" : "axe";
}
static void cycle(qa_q1ad_context *context, qa_q1ad_player *player, bool reverse) {
  const char *order[9];
  order[0] = melee(context, player);
  order[1] = "shotgun"; order[2] = "supershotgun"; order[3] = "nailgun"; order[4] = "supernailgun";
  order[5] = "grenadelauncher"; order[6] = "rocketlauncher"; order[7] = "lightning"; order[8] = "mg3:laser";
  const char *current = player->weapon != NULL ? player->weapon : "";
  int index = -1;
  if (strcmp(current, "axe") == 0 || strcmp(current, "mg3:mjolnir") == 0) index = 0;
  else for (int i = 0; i < 9; i++) if (strcmp(order[i], current) == 0) index = i;
  /* Native MG3 limits even an unrecognized source weapon to ten attempts. */
  if (index < 0) return;
  for (int remaining = 10; remaining > 0; remaining--) {
    index = (index + (reverse ? -1 : 1) + 9) % 9;
    const char *weapon = order[index];
    if (enough_ammo(context, player, weapon) && context->game.select_weapon != NULL &&
        context->game.select_weapon(context->game.ctx, player->actor, weapon)) return;
  }
}

bool qa_q1ad_handle_mg3_item_impulse(qa_q1ad_context *context, qa_actor_id actor, double impulse,
                                     qa_q1ad_developer_message_fn developer_message, void *message_ctx) {
  if (context->program != QA_Q1AD_MG3) return false;
  qa_q1ad_player *player = qa_q1ad_player_state(context, actor);
  if (player == NULL) return false;
  if (impulse == 1 || impulse == 225) {
    const char *weapon = impulse == 1 ? melee(context, player) : "mg3:laser";
    char item[64];
    if (inventory_count(context, actor, qa_q1ad_weapon_item(weapon, item)) == 0)
      qa_q1ad_message(context, actor, "$qc_no_weapon", false, NULL, 0);
    else if (!enough_ammo(context, player, weapon))
      qa_q1ad_message(context, actor, "$qc_not_enough_ammo", false, NULL, 0);
    else if (context->game.select_weapon != NULL)
      (void)context->game.select_weapon(context->game.ctx, player->actor, weapon);
    return true;
  }
  if (impulse == 10 || impulse == 12) { cycle(context, player, impulse == 12); return true; }
  if (impulse == 9 || impulse == 99) {
    if ((context->game.deathmatch != 0 || context->game.coop) &&
        qa_q1ad_cvar(context, "sv_cheats") == 0) return true;
    restock(context, player);
    bool selected = cheat(context, actor, "weapons");
    if (!selected) {
      char item[64];
      for (int i = 0; i < QA_Q1AD_WEAPONS_COUNT; i++)
        set_count(context, player, qa_q1ad_weapon_item(QA_Q1AD_WEAPONS[i], item), 1, 1);
      set_count(context, player, qa_q1ad_weapon_item("mg3:laser", item), 1, 1);
    }
    if (impulse != 99) {
      set_count(context, player, "q1:key/silver", 1, 1);
      set_count(context, player, "q1:key/gold", 1, 1);
    }
    if (!selected && context->game.select_weapon != NULL)
      (void)context->game.select_weapon(context->game.ctx, player->actor, "rocketlauncher");
    return true;
  }
  if (impulse == 100) {
    developer_message(message_ctx, "Resetting to defaults\n");
    player->max_health = 100;
    if (context->game.combat_set_health != NULL)
      context->game.combat_set_health(context->game.ctx, player->actor, 100);
    static const struct { const char *item; double capacity; } DEFAULTS[] = {
      {"q1:ammo/shells", 100}, {"q1:ammo/nails", 200}, {"q1:ammo/rockets", 100}, {"q1:ammo/cells", 100}};
    for (size_t i = 0; i < sizeof DEFAULTS / sizeof DEFAULTS[0]; i++) {
      qa_q1ad_set_mg3_inventory_capacity(context, player, DEFAULTS[i].item, DEFAULTS[i].capacity);
      if (context->game.inventory_configure != NULL)
        context->game.inventory_configure(context->game.ctx, player->actor, DEFAULTS[i].item,
            inventory_count(context, actor, DEFAULTS[i].item), DEFAULTS[i].capacity);
    }
    return true;
  }
  if (impulse >= 111 && impulse <= 115) {
    static const char *const TYPES[] = {"health", "shells", "nails", "rockets", "cells"};
    (void)qa_q1ad_give_next_mg3_upgrade(context, TYPES[(int)impulse - 111], player);
    return true;
  }
  if (impulse == 118) {
    char item[64];
    set_count(context, player, qa_q1ad_weapon_item("mg3:mjolnir", item), 1, 1);
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
    int flag = impulse == 227 ? QA_Q1AD_MG3_BLOODY_SHOTGUN : QA_Q1AD_MG3_BLOODY_SUPER_SHOTGUN;
    int32_t flags = (int32_t)qa_q1ad_player_number(context, actor, "parm15");
    bool enabled = (flags & flag) == 0;
    qa_q1ad_set_player_number(context, actor, "parm15", (double)(flags ^ flag));
    char text[96];
    (void)snprintf(text, sizeof text, "%s 'bloody %s' upgrade\n",
        enabled ? "activated" : "deactivated", impulse == 227 ? "shotgun" : "Super Shotgun");
    developer_message(message_ctx, text);
    return true;
  }
  return false;
}
