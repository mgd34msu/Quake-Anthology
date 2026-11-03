/* Port of src/content/q1/missionpacks/travel.ts over the base travel
 * (src/content/q1/base/travel.ts). GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/travel.h"

#include "missionpacks_internal.h"

static char *copy_text(const char *text) {
  const size_t length = strlen(text) + 1;
  char *copy = malloc(length);
  if (copy == NULL) qa_range_error("Out of memory");
  memcpy(copy, text, length);
  return copy;
}
/* Replaces state's inventory with owned copies of first[] then second[]. */
static void set_inventory(qa_q1_travel_state *state, const qa_inventory_entry *first, size_t first_count,
                          const qa_inventory_entry *second, size_t second_count) {
  const size_t count = first_count + second_count;
  qa_inventory_entry *inventory = malloc((count != 0 ? count : 1) * sizeof *inventory);
  qa_item_id *items = malloc((count != 0 ? count : 1) * sizeof *items);
  if (inventory == NULL || items == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < count; i++) {
    inventory[i] = i < first_count ? first[i] : second[i - first_count];
    items[i] = copy_text(inventory[i].item);
    inventory[i].item = items[i];
  }
  for (size_t i = 0; i < state->inventory_count; i++) free((void *)state->inventory_items[i]);
  free(state->inventory_items); free(state->inventory);
  state->inventory = inventory; state->inventory_items = items; state->inventory_count = count;
}
/* An owned copy of state (where TS returns the state object itself). */
static qa_q1_travel_state copy_state(const qa_q1_travel_state *state) {
  qa_q1_travel_state copy = *state;
  copy.inventory = NULL; copy.inventory_items = NULL; copy.inventory_count = 0;
  set_inventory(&copy, state->inventory, state->inventory_count, NULL, 0);
  copy.extensions = malloc((state->extension_count != 0 ? state->extension_count : 1) * sizeof *copy.extensions);
  if (copy.extensions == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < state->extension_count; i++) {
    copy.extensions[i].id = copy_text(state->extensions[i].id);
    copy.extensions[i].length = state->extensions[i].length;
    copy.extensions[i].bytes = malloc(state->extensions[i].length != 0 ? state->extensions[i].length : 1);
    if (copy.extensions[i].bytes == NULL) qa_range_error("Out of memory");
    if (state->extensions[i].length != 0) memcpy(copy.extensions[i].bytes, state->extensions[i].bytes, state->extensions[i].length);
  }
  return copy;
}
/* TS packInventory; returns the entry count (at most 10). */
static size_t pack_inventory(qa_q1_services *game, qa_q1mp_pack pack, qa_inventory_entry out[16]) {
  size_t count = 0;
  char prefix[32];
  (void)snprintf(prefix, sizeof prefix, "%s:", pack);
  for (size_t i = 0; i < QA_Q1MP_MISSION_WEAPONS_COUNT; i++) {
    if (!qa_q1mp_starts(qa_q1mp_mission_weapons[i].id, prefix)) continue;
    memset(&out[count], 0, sizeof out[count]);
    out[count].item = qa_q1_weapon_item(qa_q1mp_mission_weapons[i].id); out[count].count = 0; out[count].capacity = 1;
    count++;
  }
  if (qa_q1mp_streq(pack, "rogue")) {
    const qa_inventory_entry rogue[5] = {
      {.item = "rogue:ammo/lava-nails", .count = 0, .capacity = 200},
      {.item = "rogue:ammo/multi-rockets", .count = 0, .capacity = 100},
      {.item = "rogue:ammo/plasma", .count = 0, .capacity = 100},
      {.item = "rogue:artifact/vengeance", .count = 0, .capacity = 1},
      {.item = qa_q1_weapon_item("rogue:grapple"), .count = game->options.deathmatch != 0 && qa_q1mp_teamplay(game) >= 4 ? 1 : 0, .capacity = 1},
    };
    for (size_t i = 0; i < 5; i++) out[count++] = rogue[i];
  }
  return count;
}
qa_q1_travel_state qa_q1mp_new_mission_pack_travel(qa_q1_services *game, qa_q1mp_pack pack) {
  qa_q1_travel_state merged = qa_q1_new_travel(&game->options);
  qa_inventory_entry extra[16];
  const size_t extra_count = pack_inventory(game, pack, extra);
  /* set_inventory copies every entry before it frees the lists it replaces. */
  set_inventory(&merged, merged.inventory, merged.inventory_count, extra, extra_count);
  if (merged.extensions == NULL) {
    merged.extensions = malloc(sizeof *merged.extensions);
    if (merged.extensions == NULL) qa_range_error("Out of memory");
    merged.extension_count = 0;
  }
  if (qa_q1mp_streq(pack, "rogue") && game->options.deathmatch != 0 && qa_q1mp_teamplay(game) >= 4) {
    merged.armor.regular.kind = QA_REGULAR_ARMOR_Q1; merged.armor.regular.points = 50;
    merged.armor.regular.absorption = 0.3; merged.armor.regular.item = "q1:armor/green";
  }
  return merged;
}
qa_q1_travel_state qa_q1mp_capture_mission_pack_travel(qa_q1_services *game, qa_owned_actor actor, qa_q1mp_pack pack) {
  const bool classic = qa_q1mp_streq(game->options.edition, "classic"), rerelease = qa_q1mp_streq(game->options.edition, "rerelease");
  if (qa_q1_health(game, actor.id) <= 0 || (rerelease && game->options.deathmatch != 0) ||
      (qa_q1mp_streq(pack, "rogue") && qa_q1mp_teamplay(game) >= 4)) {
    qa_q1_travel_state state = qa_q1mp_new_mission_pack_travel(game, pack);
    qa_q1_travel_extension extensions[32];
    const size_t count = qa_q1_capture_travel_extensions(game, actor, extensions, 32);
    free(state.extensions);
    state.extensions = malloc((count != 0 ? count : 1) * sizeof *state.extensions);
    if (state.extensions == NULL) qa_range_error("Out of memory");
    if (count != 0) memcpy(state.extensions, extensions, count * sizeof *extensions);
    state.extension_count = count;
    return state;
  }
  const qa_q1_player_state *player = qa_q1_player(game, actor.id);
  const double max_health = classic ? 100 : player != NULL ? player->max_health : 100;
  const qa_q1_travel_policy policy = {true, rerelease};
  return qa_q1_capture_travel(game, actor, player != NULL ? player->weapon : "shotgun", &max_health, policy);
}
qa_q1_travel_state qa_q1mp_decode_mission_pack_travel(qa_q1_services *game, const qa_q1_travel_state *state,
                                                      double server_flags, qa_q1mp_pack pack) {
  const char *map = game->map_name != NULL ? game->map_name : "";
  const bool hipnotic_start = qa_q1mp_streq(pack, "hipnotic") &&
                              (qa_q1mp_streq(map, "start") || qa_q1mp_streq(map, "hip1m1") || qa_q1mp_streq(map, "hip2m1") ||
                               qa_q1mp_streq(map, "hip3m1"));
  const bool rogue_start = qa_q1mp_streq(pack, "rogue") &&
                           ((server_flags != 0 && qa_q1mp_streq(map, "start")) || (game->options.deathmatch == 0 && qa_q1mp_streq(map, "r2m1")));
  if (hipnotic_start || rogue_start) return qa_q1mp_new_mission_pack_travel(game, pack);
  qa_q1_travel_state copy = copy_state(state);
  if (qa_q1mp_streq(pack, "rogue") && qa_q1mp_streq(state->weapon, "rogue:grapple") && qa_q1mp_teamplay(game) < 4) copy.weapon = "axe";
  return copy;
}
void qa_q1mp_admit_mission_pack_travel(qa_q1_services *game, qa_owned_actor actor, const qa_q1_travel_state *state, qa_q1mp_pack pack) {
  qa_inventory_entry extra[16], missing[16];
  const size_t extra_count = pack_inventory(game, pack, extra);
  size_t missing_count = 0;
  for (size_t i = 0; i < extra_count; i++) {
    bool supplied = false;
    for (size_t s = 0; s < state->inventory_count && !supplied; s++) supplied = qa_q1mp_streq(state->inventory[s].item, extra[i].item);
    if (!supplied) missing[missing_count++] = extra[i];
  }
  const size_t count = missing_count + state->inventory_count;
  qa_inventory_entry *inventory = malloc((count != 0 ? count : 1) * sizeof *inventory);
  if (inventory == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < missing_count; i++) inventory[i] = missing[i];
  for (size_t i = 0; i < state->inventory_count; i++) inventory[missing_count + i] = state->inventory[i];
  qa_q1_travel_state admitted = *state; /* TS { ...state, inventory } (borrowed lists, not destroyed) */
  admitted.inventory = inventory; admitted.inventory_count = count; admitted.inventory_items = NULL;
  qa_q1_admit_travel(game, actor, &admitted);
  free(inventory);
  qa_q1_set_gravity(game, actor.id, 1);
}
