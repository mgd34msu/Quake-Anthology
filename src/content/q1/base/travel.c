/* Port of src/content/q1/base/travel.ts. GPL-2.0-or-later. */
#include "qa/content/q1/base/travel.h"

#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

static char *dup_text(const char *text) {
  size_t length = strlen(text) + 1;
  char *copy = malloc(length);
  if (copy == NULL) qa_range_error("out of memory");
  memcpy(copy, text, length);
  return copy;
}
static void free_inventory(qa_q1_travel_state *state) {
  if (state->inventory_items != NULL)
    for (size_t i = 0; i < state->inventory_count; i++) free((void *)state->inventory_items[i]);
  free(state->inventory_items);
  free(state->inventory);
}
void qa_q1_travel_state_destroy(qa_q1_travel_state *state) {
  free_inventory(state);
  for (size_t i = 0; i < state->extension_count; i++) {
    free(state->extensions[i].id);
    free(state->extensions[i].bytes);
  }
  free(state->extensions);
  memset(state, 0, sizeof *state);
}
static void set_inventory(qa_q1_travel_state *state, const qa_inventory_entry *entries, size_t count) {
  state->inventory = malloc((count != 0 ? count : 1) * sizeof *state->inventory);
  state->inventory_items = malloc((count != 0 ? count : 1) * sizeof *state->inventory_items);
  if (state->inventory == NULL || state->inventory_items == NULL) qa_range_error("out of memory");
  state->inventory_count = count;
  for (size_t i = 0; i < count; i++) {
    state->inventory[i] = entries[i];
    state->inventory_items[i] = dup_text(entries[i].item);
    state->inventory[i].item = state->inventory_items[i];
  }
}

qa_q1_travel_state qa_q1_new_travel(const qa_q1b_options *options) {
  qa_inventory_entry entries[14];
  size_t count = 0;
  static const char *const WEAPON_ITEMS[] = {
    "q1:weapon/axe", "q1:weapon/shotgun", "q1:weapon/supershotgun", "q1:weapon/nailgun",
    "q1:weapon/supernailgun", "q1:weapon/grenadelauncher", "q1:weapon/rocketlauncher",
    "q1:weapon/lightning"
  };
  for (size_t i = 0; i < 8; i++) {
    entries[count] = (qa_inventory_entry){0};
    entries[count].item = WEAPON_ITEMS[i];
    entries[count].count = i == 0 || i == 1 ? 1 : 0;
    entries[count].capacity = 1;
    count++;
  }
  entries[count++] = (qa_inventory_entry){.item = "q1:ammo/shells", .count = 25, .capacity = 100};
  entries[count++] = (qa_inventory_entry){.item = "q1:ammo/nails", .count = 0, .capacity = 200};
  entries[count++] = (qa_inventory_entry){.item = "q1:ammo/rockets", .count = 0, .capacity = 100};
  entries[count++] = (qa_inventory_entry){.item = "q1:ammo/cells", .count = 0, .capacity = 100};
  entries[count++] = (qa_inventory_entry){.item = "q1:key/silver", .count = 0, .capacity = 1};
  entries[count++] = (qa_inventory_entry){.item = "q1:key/gold", .count = 0, .capacity = 1};
  double health = strcmp(options->edition, "rerelease") == 0 && options->skill == 3 &&
                          options->deathmatch == 0
                      ? 50 : 100;
  qa_q1_travel_state state = {0};
  state.health = health;
  state.max_health = health;
  state.armor.regular.kind = QA_REGULAR_ARMOR_NONE;
  state.armor.powered.kind = "none";
  set_inventory(&state, entries, count);
  state.weapon = "shotgun";
  return state;
}

static bool is_temporary(const char *item) {
  static const char *const TEMPORARY[] = {
    "q1:key/silver", "q1:key/gold", "q1:powerup/quad",
    "q1:powerup/invulnerability", "q1:powerup/invisibility", "q1:powerup/suit"
  };
  for (size_t i = 0; i < sizeof TEMPORARY / sizeof *TEMPORARY; i++)
    if (strcmp(item, TEMPORARY[i]) == 0) return true;
  return false;
}
qa_q1_travel_state qa_q1_capture_travel(qa_q1b_game *game, qa_owned_actor actor,
                                        const char *weapon, const double *max_health_arg,
                                        qa_q1_travel_policy policy) {
  qa_q1b_player_state *player_state = game->player(game, actor.id);
  const char *weapon_value = weapon != NULL ? weapon
                             : player_state != NULL ? player_state->weapon : "shotgun";
  double max_health = max_health_arg != NULL ? *max_health_arg
                      : player_state != NULL ? player_state->max_health : 100;
  qa_combat_state combat;
  if (!game->host->combat_read(game->host->ctx, actor.id, &combat))
    qa_range_error("Q1 travel actor has no combat state");
  qa_q1_travel_extension extensions[32];
  size_t extension_count = 0;
  if (player_state != NULL) {
    const qa_q1b_player_extension *list[32];
    size_t count = game->player_extensions(game, list, 32);
    for (size_t i = 0; i < count; i++) {
      const qa_q1b_player_extension *extension = list[i];
      if (extension->capture_travel == NULL) continue;
      size_t length = 0;
      uint8_t *bytes = extension->capture_travel(extension->ctx, game, player_state, &length);
      if (bytes == NULL) continue;
      uint8_t *copy = malloc(length != 0 ? length : 1);
      if (copy == NULL) qa_range_error("out of memory");
      memcpy(copy, bytes, length);
      free(bytes);
      extensions[extension_count].id = dup_text(extension->id);
      extensions[extension_count].bytes = copy;
      extensions[extension_count].length = length;
      extension_count++;
    }
  }
  bool reset = policy.has_reset_in_deathmatch ? policy.reset_in_deathmatch : true;
  qa_q1_travel_state result;
  if (combat.health <= 0 || (game->options.deathmatch != 0 && reset)) {
    result = qa_q1_new_travel(&game->options);
  } else {
    qa_inventory_entry entries[128];
    size_t count = game->host->inventory_entries(game->host->ctx, actor.id, entries, 128);
    for (size_t i = 0; i < count; i++) {
      if (is_temporary(entries[i].item)) entries[i].count = 0;
      else if (strcmp(entries[i].item, "q1:ammo/shells") == 0 && entries[i].count < 25) entries[i].count = 25;
    }
    memset(&result, 0, sizeof result);
    double health = combat.health;
    if (health > max_health) health = max_health;
    if (health < max_health / 2) health = max_health / 2;
    result.health = health;
    result.max_health = max_health;
    result.armor = combat.armor;
    set_inventory(&result, entries, count);
    result.weapon = weapon_value;
  }
  result.extensions = malloc((extension_count != 0 ? extension_count : 1) * sizeof *result.extensions);
  if (result.extensions == NULL) qa_range_error("out of memory");
  memcpy(result.extensions, extensions, extension_count * sizeof *extensions);
  result.extension_count = extension_count;
  return result;
}
qa_q1_travel_state qa_q1_decode_travel(qa_q1b_game *game, const qa_q1_travel_state *state, double server_flags) {
  if (server_flags != 0 && strcmp(game->map_name, "start") == 0) {
    qa_q1_travel_state result = qa_q1_new_travel(&game->options);
    /* TS shares the extensions array; the copy keeps it shared. */
    result.extensions = state->extensions;
    result.extension_count = state->extension_count;
    return result;
  }
  qa_q1_travel_state result = *state;
  result.inventory = NULL;
  result.inventory_items = NULL;
  set_inventory(&result, state->inventory, state->inventory_count);
  result.extensions = state->extensions;
  return result;
}
void qa_q1_admit_travel(qa_q1b_game *game, qa_owned_actor actor, const qa_q1_travel_state *state) {
  game->host->combat_set_health(game->host->ctx, actor, state->health);
  game->host->combat_set_armor(game->host->ctx, actor, state->armor);
  if (!game->host->inventory_has(game->host->ctx, actor.id)) {
    game->host->inventory_create(game->host->ctx, actor, state->inventory, state->inventory_count);
  } else {
    for (size_t i = 0; i < state->inventory_count; i++)
      game->host->inventory_configure(game->host->ctx, actor, &state->inventory[i]);
  }
  qa_q1b_player_state *player = game->player(game, actor.id);
  if (player != NULL) {
    player->max_health = state->max_health;
    /* TS iterates powerup keys, calls host.powerup(actor, kind, 0),
     * then clears the map; copy keys first because host.powerup may
     * mutate the same list in a mirror implementation. */
    const char *kinds[32];
    size_t kind_count = player->powerup_count < 32 ? player->powerup_count : 32;
    for (size_t i = 0; i < kind_count; i++) kinds[i] = player->powerups[i].name;
    for (size_t i = 0; i < kind_count; i++)
      game->host->powerup(game->host->ctx, actor, kinds[i], 0);
    qa_q1b_powerups_clear(player);
    player->mega_rot_at = -1;
    (void)game->select_weapon(game, actor, state->weapon);
    for (size_t i = 0; i < state->extension_count; i++) {
      const qa_q1_travel_extension *extension = &state->extensions[i];
      const qa_q1b_player_extension *registered = game->player_extension_get(game, extension->id);
      if (registered == NULL || registered->restore_travel == NULL)
        qa_range_error("Missing Q1 player travel extension: %s", extension->id);
      registered->restore_travel(registered->ctx, game, player, extension->bytes, extension->length);
    }
  }
}
