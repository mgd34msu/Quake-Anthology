/* Port of src/content/q1/missionpacks/pickup-rules.ts. The rules object
 * lives in players->pickup_rules (the game borrows it); its ctx is the
 * players struct, which carries the pack the TS closures capture.
 * GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/pickup_rules.h"

#include "missionpacks_internal.h"
#include "qa/content/q1/missionpacks/items.h"

double qa_q1mp_rogue_weapon_rank(qa_q1_weapon weapon) {
  static const char *const order[] = {"rogue:plasma", "lightning", "rogue:multi-rocket", "rocketlauncher",
                                      "rogue:lava-supernailgun", "supernailgun", "rogue:multi-grenade",
                                      "grenadelauncher", "rogue:lava-nailgun", "supershotgun", "nailgun"};
  for (int i = 0; i < (int)(sizeof order / sizeof *order); i++) if (qa_q1mp_streq(order[i], weapon)) return i + 1;
  return 12;
}
static bool stays(qa_q1_services *runtime) {
  const qa_q1_options *options = &runtime->options;
  return options->coop || options->deathmatch == 2 ||
         (qa_q1mp_streq(options->edition, "rerelease") && (options->deathmatch == 3 || options->deathmatch == 5));
}
static bool weapon_leave(void *ctx, qa_q1_services *runtime) { (void)ctx; return stays(runtime); }
static double hipnotic_rank(void *ctx, qa_q1_weapon weapon) { (void)ctx; return qa_q1mp_hipnotic_weapon_rank(weapon); }
static double rogue_rank(void *ctx, qa_q1_weapon weapon) { (void)ctx; return qa_q1mp_rogue_weapon_rank(weapon); }
static bool auto_switch(void *ctx, qa_q1_services *runtime, qa_q1_player_state *player, bool was_owned) {
  const qa_q1mp_mission_pack_players *players = ctx;
  if (qa_q1mp_streq(players->pack, "rogue") && qa_q1mp_streq(player->weapon, "rogue:grapple") && player->attack_held) return false;
  return qa_q1mp_streq(runtime->options.edition, "classic") || qa_q1mp_streq(player->auto_switch, "always") ||
         (qa_q1mp_streq(player->auto_switch, "new") && !was_owned);
}
static qa_q1_weapon weapon_granted(void *ctx, qa_q1_services *runtime, qa_q1_player_state *player, qa_q1_weapon weapon) {
  qa_q1mp_mission_pack_players *players = ctx;
  if (!qa_q1mp_streq(players->pack, "rogue")) return weapon;
  qa_q1mp_players_enable_combos(players, player);
  qa_shared_inventory_table *inventory = runtime->host.inventory;
  /* items.qc weapon_touch: a weapon that stays also hands out the powered-weapon rounds. */
  if (stays(runtime)) {
    if (qa_q1mp_streq(weapon, "lightning")) (void)qa_shared_inventory_table_give(inventory, player->actor, "rogue:ammo/plasma", 5);
    else if (qa_q1mp_streq(weapon, "rocketlauncher") || qa_q1mp_streq(weapon, "grenadelauncher"))
      (void)qa_shared_inventory_table_give(inventory, player->actor, "rogue:ammo/multi-rockets", 3);
    else if (qa_q1mp_streq(weapon, "nailgun") || qa_q1mp_streq(weapon, "supernailgun"))
      (void)qa_shared_inventory_table_give(inventory, player->actor, "rogue:ammo/lava-nails", 20);
  }
  const qa_actor_id id = player->actor.id;
  if (qa_q1mp_streq(weapon, "lightning"))
    return qa_shared_inventory_table_count(inventory, id, "rogue:ammo/plasma") > 0 ? "rogue:plasma" : weapon;
  if (qa_q1mp_streq(weapon, "rocketlauncher"))
    return qa_shared_inventory_table_count(inventory, id, "rogue:ammo/multi-rockets") > 0 ? "rogue:multi-rocket" : weapon;
  if (qa_q1mp_streq(weapon, "grenadelauncher"))
    return qa_shared_inventory_table_count(inventory, id, "rogue:ammo/multi-rockets") > 0 ? "rogue:multi-grenade" : weapon;
  if (qa_q1mp_streq(weapon, "supernailgun"))
    return qa_shared_inventory_table_count(inventory, id, "rogue:ammo/lava-nails") > 1 ? "rogue:lava-supernailgun" : weapon;
  if (qa_q1mp_streq(weapon, "nailgun"))
    return qa_shared_inventory_table_count(inventory, id, "rogue:ammo/lava-nails") > 0 ? "rogue:lava-nailgun" : weapon;
  return weapon;
}
static double respawn(void *ctx, qa_q1_services *runtime, qa_q1_actor *entity, double delay) {
  (void)ctx;
  const char *classname = entity->classname;
  const bool listed = qa_q1mp_starts(classname, "item_armor") || qa_q1mp_starts(classname, "weapon_") ||
                      qa_q1mp_streq(classname, "item_shells") || qa_q1mp_streq(classname, "item_spikes") ||
                      qa_q1mp_streq(classname, "item_rockets") || qa_q1mp_streq(classname, "item_cells") ||
                      qa_q1mp_streq(classname, "item_weapon");
  return qa_q1mp_streq(runtime->options.edition, "classic") && runtime->options.deathmatch != 1 && listed ? -1 : delay;
}
void qa_q1mp_register_mission_pack_pickup_rules(qa_q1_services *game, qa_q1mp_pack pack, qa_q1mp_mission_pack_players *players) {
  qa_q1_pickup_rules *rules = &players->pickup_rules;
  memset(rules, 0, sizeof *rules);
  rules->ctx = players;
  rules->id = qa_q1mp_streq(pack, "hipnotic") ? "q1:hipnotic:pickups" : "q1:rogue:pickups";
  rules->weapon_leave = weapon_leave;
  rules->weapon_rank = qa_q1mp_streq(pack, "hipnotic") ? hipnotic_rank : rogue_rank;
  rules->auto_switch = auto_switch;
  rules->weapon_granted = weapon_granted;
  rules->respawn = respawn;
  qa_q1_register_pickup_rules(game, rules);
}
