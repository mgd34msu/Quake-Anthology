/* Port of src/content/composition/q1/commands.ts. GPL-2.0-or-later. */
#include "qa/content/composition/q1/commands.h"

#include <string.h>

#include "qa/content/q1/foundation/types.h"

#define WEAPON_COUNT 8

static bool ammunition(qa_q1_services *game, qa_q1_player_state *player, qa_q1_base_weapon weapon) {
  const qa_item_id ammo = qa_q1_services_weapon_ammo(game, weapon);
  const double minimum = strcmp(weapon, "supershotgun") == 0 || strcmp(weapon, "supernailgun") == 0 ? 2 : 1;
  return ammo == NULL || qa_shared_inventory_table_count(game->host.inventory, player->actor.id, ammo) >= minimum;
}
static void set_count(qa_q1_services *game, qa_q1_player_state *player, qa_item_id item, double count,
                      double capacity) {
  size_t entry_count = 0;
  const qa_inventory_entry *entries = qa_shared_inventory_table_entries(game->host.inventory, player->actor.id, &entry_count);
  qa_inventory_entry entry;
  memset(&entry, 0, sizeof entry);
  entry.item = item;
  entry.count = count;
  entry.capacity = capacity;
  for (size_t i = 0; i < entry_count; i++)
    if (strcmp(entries[i].item, item) == 0) { entry = entries[i]; entry.count = count; break; }
  qa_shared_inventory_table_configure(game->host.inventory, player->actor, entry, NULL, NULL);
}
static void message(qa_q1_services *game, qa_q1_player_state *player, const char *text) {
  qa_q1_message(game, &player->actor.id, text, false, NULL, 0);
}

bool qa_comp_q1_weapon_impulse(qa_q1_services *game, qa_q1_player_state *player, double impulse) {
  const bool classic = strcmp(game->options.edition, "classic") == 0;
  if (impulse >= 1 && impulse <= 8) {
    /* TS WEAPONS[impulse - 1]: a fractional impulse misses. */
    const long index = (long)(impulse - 1);
    if ((double)index != impulse - 1 || index < 0 || index >= WEAPON_COUNT) return false;
    const qa_q1_base_weapon weapon = QA_Q1_WEAPONS[index];
    if (qa_shared_inventory_table_count(game->host.inventory, player->actor.id, qa_q1_services_weapon_item(game, weapon)) == 0)
      message(game, player, classic ? "no weapon.\n" : "$qc_no_weapon");
    else if (!ammunition(game, player, weapon))
      message(game, player, classic ? "not enough ammo.\n" : "$qc_not_enough_ammo");
    else
      (void)qa_q1_select_weapon(game, player->actor, weapon);
    return true;
  }
  if (impulse == 10 || impulse == 12) {
    long index = -1;
    for (long i = 0; i < WEAPON_COUNT; i++)
      if (player->weapon != NULL && strcmp(QA_Q1_WEAPONS[i], player->weapon) == 0) { index = i; break; }
    const long direction = impulse == 10 ? 1 : -1;
    for (long offset = 1; offset <= WEAPON_COUNT; offset++) {
      const qa_q1_base_weapon weapon = QA_Q1_WEAPONS[(index + direction * offset + WEAPON_COUNT * 2) % WEAPON_COUNT];
      if (ammunition(game, player, weapon) && qa_q1_select_weapon(game, player->actor, weapon)) break;
    }
    return true;
  }
  return false;
}

bool qa_comp_q1_ctf_weapon_cycle(qa_q1_services *game, qa_q1_player_state *player, qa_item_id selected,
                                 bool reverse, qa_item_id grapple, qa_comp_q1_cycle_select_fn select,
                                 void *select_ctx) {
  qa_item_id items[WEAPON_COUNT + 1];
  qa_q1_base_weapon weapons[WEAPON_COUNT + 1];
  size_t count = 0;
  items[count] = qa_q1_services_weapon_item(game, "axe"); weapons[count++] = "axe";
  items[count] = grapple; weapons[count++] = NULL;
  for (size_t i = 0; i < WEAPON_COUNT; i++) {
    if (strcmp(QA_Q1_WEAPONS[i], "axe") == 0) continue;
    items[count] = qa_q1_services_weapon_item(game, QA_Q1_WEAPONS[i]);
    weapons[count++] = QA_Q1_WEAPONS[i];
  }
  long current = 0; /* Math.max(0, findIndex) */
  for (size_t i = 0; i < count; i++)
    if (selected != NULL && strcmp(items[i], selected) == 0) { current = (long)i; break; }
  for (long step = 1; step <= (long)count; step++) {
    const size_t at = (size_t)((current + (reverse ? -step : step) + (long)count * 2) % (long)count);
    if (qa_shared_inventory_table_count(game->host.inventory, player->actor.id, items[at]) == 0 ||
        (weapons[at] != NULL && !ammunition(game, player, weapons[at])))
      continue;
    if (select(select_ctx, items[at], weapons[at])) return true;
  }
  return true;
}

bool qa_comp_base_q1_impulse(const qa_comp_q1_impulse_source *source, qa_q1_player_state *player,
                             double impulse) {
  qa_q1_services *game = source->game;
  const qa_comp_q1_services *services = source->services;
  const bool classic = strcmp(game->options.edition, "classic") == 0;
  if (impulse == 11) {
    const double flags = source->campaign.read_flags(source->campaign.ctx);
    source->campaign.write_flags(source->campaign.ctx, (double)(float)(flags * 2 + 1)); /* Math.fround */
    return true;
  }
  if (impulse == 9) {
    if ((game->options.deathmatch != 0 || game->options.coop) &&
        (classic || services->cvar(services->ctx, "sv_cheats") == 0))
      return true;
    const bool foreign_arsenal = services->cheat_arsenal != NULL &&
                                 services->cheat_arsenal(services->ctx, player->actor.id, NULL);
    if (!foreign_arsenal) {
      for (size_t i = 0; i < WEAPON_COUNT; i++)
        set_count(game, player, qa_q1_services_weapon_item(game, QA_Q1_WEAPONS[i]), 1, 1);
      set_count(game, player, "q1:ammo/shells", 100, 100);
      set_count(game, player, "q1:ammo/nails", 200, 200);
      set_count(game, player, "q1:ammo/rockets", 100, 100);
      set_count(game, player, "q1:ammo/cells", 200, 100);
    }
    set_count(game, player, "q1:key/silver", 1, 1);
    set_count(game, player, "q1:key/gold", 1, 1);
    if (source->program == QA_COMP_Q1_CTF) set_count(game, player, "q1:ctf/weapon/grapple", 1, 1);
    if (!classic && (source->program == QA_COMP_Q1_ID1 || source->program == QA_COMP_Q1_CTF)) {
      qa_regular_armor_state armor;
      memset(&armor, 0, sizeof armor);
      armor.kind = QA_REGULAR_ARMOR_Q1;
      armor.points = 200;
      armor.absorption = 0.8;
      armor.item = "q1:item_armorInv";
      qa_gameplay_authority_set_regular_armor(game->host.combat, player->actor, armor);
    }
    if (!foreign_arsenal)
      (void)services->select_weapon(services->ctx, player->actor.id, qa_q1_services_weapon_item(game, "rocketlauncher"));
    return true;
  }
  if (impulse == 255) {
    const bool source_cheats = !classic && (source->program == QA_COMP_Q1_ID1 || source->program == QA_COMP_Q1_CTF);
    if (source_cheats ? services->cvar(services->ctx, "sv_cheats") == 0
                      : game->options.deathmatch != 0 || game->options.coop)
      return true;
    qa_q1_give_powerup(game, player, "quad", 30);
    qa_comp_q1_event event;
    memset(&event, 0, sizeof event);
    event.kind = QA_COMP_Q1_EVENT_DEVELOPER_MESSAGE;
    event.text = "quad cheat\n";
    services->emit(services->ctx, &event);
    return true;
  }
  return false;
}
