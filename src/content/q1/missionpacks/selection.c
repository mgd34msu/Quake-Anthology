/* Port of src/content/q1/missionpacks/selection.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/selection.h"

#include "missionpacks_internal.h"
#include "qa/content/q1/missionpacks/messages.h"

static const char *const HIPNOTIC_CYCLE[] = {"axe", "shotgun", "supershotgun", "nailgun", "supernailgun", "grenadelauncher",
                                             "hipnotic:proximity", "rocketlauncher", "lightning", "hipnotic:laser", "hipnotic:mjolnir"};
static const char *const ROGUE_CYCLE[] = {"axe", "rogue:grapple", "shotgun", "supershotgun", "nailgun", "rogue:lava-nailgun",
                                          "supernailgun", "rogue:lava-supernailgun", "grenadelauncher", "rogue:multi-grenade",
                                          "rocketlauncher", "rogue:multi-rocket", "lightning", "rogue:plasma"};

static double count_required(qa_q1_weapon weapon) {
  return qa_q1mp_streq(weapon, "supershotgun") || qa_q1mp_streq(weapon, "supernailgun") ||
         qa_q1mp_streq(weapon, "rogue:lava-supernailgun") ? 2 : 1;
}
static bool owns(qa_q1_services *game, qa_q1_player_state *player, qa_q1_weapon weapon) {
  return qa_shared_inventory_table_count(game->host.inventory, player->actor.id, qa_q1_weapon_item(weapon)) > 0;
}
static bool has_ammo(qa_q1_services *game, qa_q1_player_state *player, qa_q1_weapon weapon, double required) {
  const qa_item_id ammo = qa_q1_services_weapon_ammo(game, weapon);
  return ammo == NULL || qa_shared_inventory_table_count(game->host.inventory, player->actor.id, ammo) >= required;
}
static bool ctf_grapple(const qa_q1_services *game) {
  return game->options.deathmatch != 0 && qa_q1mp_teamplay(game) >= 4;
}
static bool cycle(qa_q1_services *game, qa_q1_player_state *player, qa_q1mp_pack pack, bool reverse) {
  const bool hipnotic = qa_q1mp_streq(pack, "hipnotic");
  const char *const *order = hipnotic ? HIPNOTIC_CYCLE : ROGUE_CYCLE;
  const int length = hipnotic ? (int)(sizeof HIPNOTIC_CYCLE / sizeof *HIPNOTIC_CYCLE) : (int)(sizeof ROGUE_CYCLE / sizeof *ROGUE_CYCLE);
  int current = -1;
  for (int i = 0; i < length; i++) if (qa_q1mp_streq(order[i], player->weapon)) { current = i; break; }
  for (int step = 1; step <= length; step++) {
    const char *next = order[(current + (reverse ? -step : step) + length * 2) % length];
    if (qa_q1mp_streq(next, "rogue:grapple") && !ctf_grapple(game)) continue;
    if (owns(game, player, next) &&
        has_ammo(game, player, next, reverse && qa_q1mp_streq(next, "rogue:lava-nailgun") ? 2 : count_required(next)))
      return qa_q1_select_weapon(game, player->actor, next);
  }
  return false;
}
static qa_q1_weapon paired(qa_q1_services *game, qa_q1_player_state *player, qa_q1_weapon base, qa_q1_weapon powered) {
  return owns(game, player, powered) &&
         (qa_q1mp_streq(player->weapon, base) || !has_ammo(game, player, base, count_required(base))) ? powered : base;
}
static qa_q1_weapon rogue_selected(qa_q1_services *game, qa_q1_player_state *player, double impulse) {
  const bool ctf = ctf_grapple(game), rerelease = qa_q1mp_streq(game->options.edition, "rerelease");
  if (impulse == 1) return ctf && qa_q1mp_streq(player->weapon, "axe") ? "rogue:grapple" : "axe";
  if (impulse == 2) return "shotgun";
  if (impulse == 3) return "supershotgun";
  if (impulse == 4) return paired(game, player, "nailgun", "rogue:lava-nailgun");
  if (impulse == 5) return paired(game, player, "supernailgun", "rogue:lava-supernailgun");
  if (impulse == 6) return paired(game, player, "grenadelauncher", "rogue:multi-grenade");
  if (impulse == 7) return paired(game, player, "rocketlauncher", "rogue:multi-rocket");
  if (impulse == 8) return paired(game, player, "lightning", "rogue:plasma");
  if (impulse == 22) return ctf ? "rogue:grapple" : NULL;
  if (impulse == 60) return "rogue:lava-nailgun";
  if (impulse == 61) return "rogue:lava-supernailgun";
  if (impulse == 62) return "rogue:multi-grenade";
  if (impulse == 63) return "rogue:multi-rocket";
  if (impulse == 64) return "rogue:plasma";
  if (impulse == 65) return rerelease ? "nailgun" : NULL;
  if (impulse == 66) return rerelease ? "supernailgun" : NULL;
  if (impulse == 67) return rerelease ? "grenadelauncher" : NULL;
  if (impulse == 68) return rerelease ? "rocketlauncher" : NULL;
  return NULL;
}
static qa_q1_weapon hipnotic_selected(qa_q1_services *game, qa_q1_player_state *player, double impulse) {
  const bool rerelease = qa_q1mp_streq(game->options.edition, "rerelease");
  if (impulse == 1) return "axe";
  if (impulse == 2) return "shotgun";
  if (impulse == 3) return "supershotgun";
  if (impulse == 4) return "nailgun";
  if (impulse == 5) return "supernailgun";
  if (impulse == 6) return qa_q1mp_streq(player->weapon, "grenadelauncher") ? "hipnotic:proximity" : "grenadelauncher";
  if (impulse == 7) return "rocketlauncher";
  if (impulse == 8) return "lightning";
  if (impulse == 225) return "hipnotic:laser";
  if (impulse == 226) return "hipnotic:mjolnir";
  if (impulse == 227) return rerelease ? "hipnotic:proximity" : NULL;
  if (impulse == 228) return rerelease ? "grenadelauncher" : NULL;
  return NULL;
}
static const char *switch_message(qa_q1_weapon old, qa_q1_weapon selected) {
  if (qa_q1mp_streq(old, "rogue:lava-nailgun") || qa_q1mp_streq(old, "rogue:lava-supernailgun"))
    return qa_q1mp_streq(selected, "nailgun") || qa_q1mp_streq(selected, "supernailgun") ? "$qc_normal_nails" : "";
  if (qa_q1mp_streq(old, "rogue:multi-grenade")) return qa_q1mp_streq(selected, "grenadelauncher") ? "$qc_normal_grenades" : "";
  if (qa_q1mp_streq(old, "rogue:multi-rocket")) return qa_q1mp_streq(selected, "rocketlauncher") ? "$qc_normal_rockets" : "";
  if (qa_q1mp_streq(old, "rogue:plasma")) return qa_q1mp_streq(selected, "lightning") ? "$qc_lightning_gun" : "";
  if (qa_q1mp_streq(selected, "rogue:lava-nailgun") || qa_q1mp_streq(selected, "rogue:lava-supernailgun")) return "$qc_lava_nails";
  if (qa_q1mp_streq(selected, "rogue:multi-grenade")) return "$qc_multi_gl";
  if (qa_q1mp_streq(selected, "rogue:multi-rocket")) return "$qc_multi_rl";
  if (qa_q1mp_streq(selected, "rogue:plasma")) return "$qc_plasma_gun";
  return "";
}
bool qa_q1mp_mission_weapon_impulse(qa_q1_services *game, qa_q1_player_state *player, qa_q1mp_pack pack, double impulse) {
  if (impulse == 10 || impulse == 12) { (void)cycle(game, player, pack, impulse == 12); return true; }
  const bool hipnotic = qa_q1mp_streq(pack, "hipnotic"), rogue = qa_q1mp_streq(pack, "rogue");
  qa_q1_weapon selected = hipnotic ? hipnotic_selected(game, player, impulse) : rogue_selected(game, player, impulse);
  if (selected == NULL) return false;
  if (hipnotic && qa_q1mp_streq(selected, "grenadelauncher") && !owns(game, player, selected)) selected = "hipnotic:proximity";
  if (!owns(game, player, selected)) { qa_q1mp_mission_message(game, &player->actor.id, "$qc_no_weapon"); return true; }
  const double required = rogue && impulse == 61 ? 1 : count_required(selected);
  const bool alias = rogue && impulse >= 65 && impulse <= 68 && qa_q1mp_starts(player->weapon, "rogue:") &&
                     !qa_q1mp_streq(player->weapon, "rogue:grapple");
  const double alias_ammo = alias ? qa_shared_inventory_table_count(game->host.inventory, player->actor.id,
                                                                    impulse <= 66 ? "rogue:ammo/lava-nails" : "rogue:ammo/multi-rockets") : 0;
  if (!alias ? !has_ammo(game, player, selected, required) : alias_ammo < required) {
    qa_q1mp_mission_message(game, &player->actor.id, "$qc_not_enough_ammo");
    return true;
  }
  if (rogue && !qa_q1mp_streq(selected, player->weapon)) {
    const char *key = switch_message(player->weapon, selected);
    if (key[0] != '\0') qa_q1mp_mission_message(game, &player->actor.id, key);
  }
  (void)qa_q1_select_weapon(game, player->actor, selected);
  return true;
}
