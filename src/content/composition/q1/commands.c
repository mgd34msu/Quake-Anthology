/* Port of src/content/composition/q1/commands.ts. GPL-2.0-or-later. */
#include "qa/content/composition/q1/commands.h"

#include <string.h>

const char *const QA_COMP_Q1_WEAPONS[8] = {
    "axe", "shotgun", "supershotgun", "nailgun", "supernailgun", "grenadelauncher", "rocketlauncher", "lightning"};

static bool ammunition(qa_comp_q1_game *game, qa_comp_q1_player_state *player, const char *weapon) {
  qa_item_id ammo = game->weapon_ammo(game->ctx, weapon);
  double minimum = strcmp(weapon, "supershotgun") == 0 || strcmp(weapon, "supernailgun") == 0 ? 2 : 1;
  return ammo == NULL || game->inventory_count(game->ctx, player->actor.id, ammo) >= minimum;
}

static void set_count(qa_comp_q1_game *game, qa_comp_q1_player_state *player, qa_item_id item,
                      double count, double capacity) {
  size_t entry_count = 0;
  const qa_inventory_entry *entries = game->inventory_entries(game->ctx, player->actor.id, &entry_count);
  qa_inventory_entry entry = {item, count, capacity, false, {0}};
  for (size_t i = 0; i < entry_count; i++)
    if (strcmp(entries[i].item, item) == 0) { entry = entries[i]; entry.count = count; break; }
  game->inventory_configure(game->ctx, player->actor, entry);
}

bool qa_comp_q1_weapon_impulse(qa_comp_q1_game *game, qa_comp_q1_player_state *player, double impulse) {
  void *ctx = game->ctx;
  if (impulse >= 1 && impulse <= 8) {
    /* TS indexes WEAPONS[impulse - 1]; a fractional impulse misses. */
    long index = (long)(impulse - 1);
    if ((double)index != impulse - 1 || index < 0 || index >= QA_COMP_Q1_WEAPON_COUNT) return false;
    const char *weapon = QA_COMP_Q1_WEAPONS[index];
    bool classic = strcmp(game->edition, "classic") == 0;
    if (game->inventory_count(ctx, player->actor.id, game->weapon_item(ctx, weapon)) == 0)
      game->message(ctx, player->actor.id, classic ? "no weapon.\n" : "$qc_no_weapon", false, NULL, 0);
    else if (!ammunition(game, player, weapon))
      game->message(ctx, player->actor.id, classic ? "not enough ammo.\n" : "$qc_not_enough_ammo", false, NULL, 0);
    else
      game->select_weapon(ctx, player->actor, weapon);
    return true;
  }
  if (impulse == 10 || impulse == 12) {
    long index = -1;
    for (long i = 0; i < QA_COMP_Q1_WEAPON_COUNT; i++)
      if (player->weapon != NULL && strcmp(QA_COMP_Q1_WEAPONS[i], player->weapon) == 0) { index = i; break; }
    long direction = impulse == 10 ? 1 : -1;
    for (long offset = 1; offset <= QA_COMP_Q1_WEAPON_COUNT; offset++) {
      const char *weapon = QA_COMP_Q1_WEAPONS[(index + direction * offset + QA_COMP_Q1_WEAPON_COUNT * 2) % QA_COMP_Q1_WEAPON_COUNT];
      if (ammunition(game, player, weapon) && game->select_weapon(ctx, player->actor, weapon)) break;
    }
    return true;
  }
  return false;
}

bool qa_comp_base_q1_impulse(const qa_comp_q1_impulse_source *source, qa_comp_q1_player_state *player,
                             double impulse) {
  qa_comp_q1_game *game = source->game;
  const qa_comp_q1_services *services = source->services;
  void *ctx = game->ctx;
  if (impulse == 11) {
    double flags = source->campaign.read_flags(source->campaign.ctx);
    source->campaign.write_flags(source->campaign.ctx, (double)(float)(flags * 2 + 1)); /* Math.fround */
    return true;
  }
  if (impulse == 9) {
    if ((game->deathmatch != 0 || game->coop) &&
        (strcmp(game->edition, "classic") == 0 || services->cvar(services->ctx, "sv_cheats") == 0))
      return true;
    bool foreign_arsenal = services->cheat_arsenal != NULL &&
                           services->cheat_arsenal(services->ctx, player->actor.id, NULL);
    if (!foreign_arsenal) {
      for (int i = 0; i < QA_COMP_Q1_WEAPON_COUNT; i++)
        set_count(game, player, game->weapon_item(ctx, QA_COMP_Q1_WEAPONS[i]), 1, 1);
      set_count(game, player, "q1:ammo/shells", 100, 100);
      set_count(game, player, "q1:ammo/nails", 200, 200);
      set_count(game, player, "q1:ammo/rockets", 100, 100);
      set_count(game, player, "q1:ammo/cells", 200, 100);
    }
    set_count(game, player, "q1:key/silver", 1, 1);
    set_count(game, player, "q1:key/gold", 1, 1);
    if (source->program == QA_COMP_Q1_CTF) set_count(game, player, "q1:ctf/weapon/grapple", 1, 1);
    if (strcmp(game->edition, "rerelease") == 0 &&
        (source->program == QA_COMP_Q1_ID1 || source->program == QA_COMP_Q1_CTF))
      game->set_armor(ctx, player->actor, (qa_comp_q1_armor){"q1", 200, 0.8, "q1:item_armorInv"});
    if (!foreign_arsenal)
      services->select_weapon(services->ctx, player->actor.id, game->weapon_item(ctx, "rocketlauncher"));
    return true;
  }
  if (impulse == 255) {
    bool source_cheats = strcmp(game->edition, "rerelease") == 0 &&
                         (source->program == QA_COMP_Q1_ID1 || source->program == QA_COMP_Q1_CTF);
    if (source_cheats ? services->cvar(services->ctx, "sv_cheats") == 0
                      : game->deathmatch != 0 || game->coop)
      return true;
    game->give_powerup(ctx, player, "quad");
    qa_comp_q1_event event = {0};
    event.kind = QA_COMP_Q1_EVENT_DEVELOPER_MESSAGE;
    event.text = "quad cheat\n";
    services->emit(services->ctx, &event);
    return true;
  }
  return false;
}
