/* Port of src/content/q1/missionpacks/travel.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/travel.h"
#include "missionpacks_internal.h"

static void pack_inventory(qa_q1mp_game *game, qa_q1mp_pack pack, qa_inventory_entry *out, size_t *count) {
  size_t n = 0; char prefix[32]; (void)snprintf(prefix, sizeof prefix, "%s:", pack);
  for (size_t i = 0; i < QA_Q1MP_MISSION_WEAPONS_COUNT; i++) {
    if (!qa_q1mp_starts(qa_q1mp_mission_weapons[i].id, prefix)) continue;
    char *buf = malloc(96); qa_q1mp_weapon_item(qa_q1mp_mission_weapons[i].id, buf, 96);
    out[n].item = buf; out[n].count = 0; out[n].capacity = 1; out[n].has_count_policy = false; out[n].has_count_policy = false; out[n].count_policy.kind = NULL; out[n].count_policy.arithmetic = NULL; n++;
  }
  (void)game;
  if (qa_q1mp_streq(pack, "rogue")) {
    out[n++] = (qa_inventory_entry){"rogue:ammo/lava-nails", 0, 200, false, {NULL, NULL}};
    out[n++] = (qa_inventory_entry){"rogue:ammo/multi-rockets", 0, 100, false, {NULL, NULL}};
    out[n++] = (qa_inventory_entry){"rogue:ammo/plasma", 0, 100, false, {NULL, NULL}};
    out[n++] = (qa_inventory_entry){"rogue:artifact/vengeance", 0, 1, false, {NULL, NULL}};
    char *buf = malloc(96); qa_q1mp_weapon_item("rogue:grapple", buf, 96);
    double grapple = (game->base->options.deathmatch != 0 && qa_q1mp_teamplay(game) >= 4) ? 1 : 0;
    out[n].item = buf; out[n].count = grapple; out[n].capacity = 1; out[n].has_count_policy = false; out[n].count_policy.kind = NULL; out[n].count_policy.arithmetic = NULL; n++;
  }
  *count = n;
}
qa_q1mp_travel_state qa_q1mp_new_mission_pack_travel(qa_q1mp_game *game, qa_q1mp_pack pack) {
  qa_q1mp_travel_state base = qa_q1mp_ext_new_q1_travel(&game->base->options);
  qa_inventory_entry extra[16]; size_t extra_count = 0; pack_inventory(game, pack, extra, &extra_count);
  for (size_t i = 0; i < extra_count && base.inventory_count < QA_Q1MP_TRAVEL_ITEMS; i++) base.inventory[base.inventory_count++] = extra[i];
  if (qa_q1mp_streq(pack, "rogue") && game->base->options.deathmatch != 0 && qa_q1mp_teamplay(game) >= 4) {
    base.armor.regular.kind = QA_REGULAR_ARMOR_Q1; base.armor.regular.points = 50; base.armor.regular.absorption = 0.3; base.armor.regular.item = "q1:armor/green";
  }
  return base;
}
qa_q1mp_travel_state qa_q1mp_capture_mission_pack_travel(qa_q1mp_game *game, qa_owned_actor actor, qa_q1mp_pack pack) {
  qa_q1b_options *o = &game->base->options;
  if (game->base->health(game->base, actor.id) <= 0 || (qa_q1mp_streq(o->edition, "rerelease") && o->deathmatch != 0) || (qa_q1mp_streq(pack, "rogue") && qa_q1mp_teamplay(game) >= 4))
    return qa_q1mp_new_mission_pack_travel(game, pack);
  qa_q1mp_player_state *player = game->base->player(game->base, actor.id);
  return qa_q1mp_ext_capture_q1_travel(game, actor, player ? player->weapon : "shotgun",
    qa_q1mp_streq(o->edition, "classic") ? 100 : (player ? player->max_health : 100),
    qa_q1mp_streq(o->edition, "rerelease"));
}
qa_q1mp_travel_state qa_q1mp_decode_mission_pack_travel(qa_q1mp_game *game, qa_q1mp_travel_state state, double server_flags, qa_q1mp_pack pack) {
  const char *map = game->base->map_name;
  bool hip_reset = qa_q1mp_streq(pack, "hipnotic") && (qa_q1mp_streq(map,"start")||qa_q1mp_streq(map,"hip1m1")||qa_q1mp_streq(map,"hip2m1")||qa_q1mp_streq(map,"hip3m1"));
  bool rog_reset = qa_q1mp_streq(pack, "rogue") && ((server_flags != 0 && qa_q1mp_streq(map,"start")) || (game->base->options.deathmatch == 0 && qa_q1mp_streq(map,"r2m1")));
  if (hip_reset || rog_reset) return qa_q1mp_new_mission_pack_travel(game, pack);
  if (qa_q1mp_streq(pack, "rogue") && qa_q1mp_streq(state.weapon, "rogue:grapple") && qa_q1mp_teamplay(game) < 4) state.weapon = "axe";
  return state;
}
void qa_q1mp_admit_mission_pack_travel(qa_q1mp_game *game, qa_owned_actor actor, qa_q1mp_travel_state state, qa_q1mp_pack pack) {
  qa_inventory_entry extra[16]; size_t extra_count = 0; pack_inventory(game, pack, extra, &extra_count);
  qa_q1mp_travel_state merged = state; size_t n = 0;
  for (size_t i = 0; i < extra_count; i++) {
    bool supplied = false;
    for (size_t j = 0; j < state.inventory_count; j++) if (qa_q1mp_streq(state.inventory[j].item, extra[i].item)) supplied = true;
    if (!supplied && n < QA_Q1MP_TRAVEL_ITEMS) merged.inventory[n++] = extra[i];
  }
  for (size_t j = 0; j < state.inventory_count && n < QA_Q1MP_TRAVEL_ITEMS; j++) merged.inventory[n++] = state.inventory[j];
  merged.inventory_count = n;
  qa_q1mp_ext_admit_q1_travel(game, actor, &merged);
  if (game->set_gravity) game->set_gravity(game->ctx, actor.id, 1);
  else if (game->base->host->set_gravity) game->base->host->set_gravity(game->base->host->ctx, actor.id, 1);
}
