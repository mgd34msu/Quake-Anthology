/* Port of src/content/q1/missionpacks/pickup-rules.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/pickup_rules.h"
#include "missionpacks_internal.h"
#include "qa/content/q1/missionpacks/items.h"
#include "qa/content/q1/missionpacks/player.h"
#include <stdlib.h>

double qa_q1mp_rogue_weapon_rank(const char *weapon) {
  static const char *const order[] = {"rogue:plasma","lightning","rogue:multi-rocket","rocketlauncher","rogue:lava-supernailgun","supernailgun","rogue:multi-grenade","grenadelauncher","rogue:lava-nailgun","supershotgun","nailgun"};
  for (int i = 0; i < 11; i++) if (qa_q1mp_streq(order[i], weapon)) return i + 1;
  return 12;
}
typedef struct rules_ctx { qa_q1mp_pack pack; qa_q1mp_mission_pack_players *players; } rules_ctx;
static bool weapon_leave(void *ctx, qa_q1mp_game *game) {
  (void)ctx; qa_q1b_options *o = &game->base->options;
  return o->coop || o->deathmatch == 2 || (qa_q1mp_streq(o->edition, "rerelease") && (o->deathmatch == 3 || o->deathmatch == 5));
}
static double weapon_rank_hip(void *ctx, const char *w) { (void)ctx; return qa_q1mp_hipnotic_weapon_rank(w); }
static double weapon_rank_rog(void *ctx, const char *w) { (void)ctx; return qa_q1mp_rogue_weapon_rank(w); }
static bool auto_switch(void *ctx, qa_q1mp_game *game, qa_q1mp_player_state *player, bool was_owned) {
  rules_ctx *c = ctx;
  if (qa_q1mp_streq(c->pack, "rogue") && qa_q1mp_streq(player->weapon, "rogue:grapple") && player->attack_held) return false;
  return qa_q1mp_streq(game->base->options.edition, "classic") || qa_q1mp_streq(player->auto_switch, "always") || (qa_q1mp_streq(player->auto_switch, "new") && !was_owned);
}
static const char *weapon_granted(void *ctx, qa_q1mp_game *game, qa_q1mp_player_state *player, const char *weapon) {
  rules_ctx *c = ctx;
  if (!qa_q1mp_streq(c->pack, "rogue")) return weapon;
  qa_q1mp_players_enable_combos(c->players, player);
  qa_actor_id id = player->actor.id;
  if (qa_q1mp_streq(weapon, "lightning")) return qa_q1mp_count(game, id, "rogue:ammo/plasma") > 0 ? "rogue:plasma" : weapon;
  if (qa_q1mp_streq(weapon, "rocketlauncher")) return qa_q1mp_count(game, id, "rogue:ammo/multi-rockets") > 0 ? "rogue:multi-rocket" : weapon;
  if (qa_q1mp_streq(weapon, "grenadelauncher")) return qa_q1mp_count(game, id, "rogue:ammo/multi-rockets") > 0 ? "rogue:multi-grenade" : weapon;
  if (qa_q1mp_streq(weapon, "supernailgun")) return qa_q1mp_count(game, id, "rogue:ammo/lava-nails") > 1 ? "rogue:lava-supernailgun" : weapon;
  if (qa_q1mp_streq(weapon, "nailgun")) return qa_q1mp_count(game, id, "rogue:ammo/lava-nails") > 0 ? "rogue:lava-nailgun" : weapon;
  return weapon;
}
static double respawn_rule(void *ctx, qa_q1mp_game *game, qa_q1mp_actor *entity, double delay) {
  (void)ctx; qa_q1b_options *o = &game->base->options;
  bool listed = qa_q1mp_starts(entity->classname, "item_armor") || qa_q1mp_starts(entity->classname, "weapon_") ||
    qa_q1mp_streq(entity->classname,"item_shells")||qa_q1mp_streq(entity->classname,"item_spikes")||qa_q1mp_streq(entity->classname,"item_rockets")||qa_q1mp_streq(entity->classname,"item_cells")||qa_q1mp_streq(entity->classname,"item_weapon");
  return (qa_q1mp_streq(o->edition, "classic") && o->deathmatch != 1 && listed) ? -1 : delay;
}
void qa_q1mp_register_mission_pack_pickup_rules(qa_q1mp_game *game, qa_q1mp_pack pack, qa_q1mp_mission_pack_players *players) {
  rules_ctx *c = malloc(sizeof *c); /* registration-lived, like the TS closure */
  c->pack = pack; c->players = players;
  static char idbuf[64]; (void)snprintf(idbuf, sizeof idbuf, "q1:%s:pickups", pack);
  qa_q1mp_pickup_rules rules = {idbuf, c, weapon_leave, qa_q1mp_streq(pack,"hipnotic") ? weapon_rank_hip : weapon_rank_rog, auto_switch, weapon_granted, respawn_rule};
  if (game->register_pickup_rules) game->register_pickup_rules(game->ctx, &rules);
}
