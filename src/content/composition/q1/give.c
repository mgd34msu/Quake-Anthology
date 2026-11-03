/* Port of src/content/composition/q1/give.ts. GPL-2.0-or-later. */
#include "qa/content/composition/q1/give.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/foundation/js.h"
#include "qa/content/q1/foundation/pickups.h"
#include "qa/network/common/js_number.h"

/* ASCII toLowerCase; TS lowercases UTF-16 (non-ASCII give args differ). */
static char *lower_dup(const char *text) {
  size_t length = strlen(text);
  char *out = (char *)malloc(length + 1);
  if (out == NULL) return NULL;
  for (size_t i = 0; i < length; i++) out[i] = text[i] >= 'A' && text[i] <= 'Z' ? (char)(text[i] + 32) : text[i];
  out[length] = '\0';
  return out;
}

static char *join_args(const char *const *args, size_t count) {
  size_t length = 0;
  for (size_t i = 0; i < count; i++) length += strlen(args[i]) + 1;
  char *out = (char *)malloc(length + 1);
  if (out == NULL) return NULL;
  char *cursor = out;
  for (size_t i = 0; i < count; i++) cursor += sprintf(cursor, "%s%s", i > 0 ? " " : "", args[i]);
  return out;
}

/* TS /^-?\d+$/. */
static bool is_integer_text(const char *text) {
  if (*text == '-') text++;
  if (*text == '\0') return false;
  for (; *text != '\0'; text++) if (*text < '0' || *text > '9') return false;
  return true;
}

/* TS normalize: strip spaces and underscores. Writes into out (cap). */
static size_t normalize(const char *name, char *out, size_t cap) {
  size_t have = 0;
  for (size_t i = 0; name[i] != '\0'; i++) {
    if (name[i] == ' ' || name[i] == '_') continue;
    if (have + 1 < cap) out[have++] = name[i];
  }
  out[have] = '\0';
  return have;
}

typedef struct give_state {
  qa_q1_services *game;
  const qa_comp_q1_services *services;
  qa_q1_player_state *player;
  qa_actor_id actor;
  const char **weapons; /* deduped WEAPONS + registered keys, borrowed */
  size_t weapon_count;
} give_state;

static void message(give_state *state, const char *text) {
  qa_q1_message(state->game, &state->actor, text, false, NULL, 0);
}
static void configure(give_state *state, qa_item_id item, double count, double capacity) {
  qa_q1_services *game = state->game;
  size_t entry_count = 0;
  const qa_inventory_entry *entries = qa_shared_inventory_table_entries(game->host.inventory, state->actor, &entry_count);
  double previous_capacity = 0;
  for (size_t i = 0; i < entry_count; i++)
    if (strcmp(entries[i].item, item) == 0) { previous_capacity = entries[i].capacity; break; }
  qa_inventory_entry entry;
  memset(&entry, 0, sizeof entry);
  entry.item = item;
  entry.count = qa_js_max(0.0, count);
  entry.capacity = qa_js_max(qa_js_max(capacity, previous_capacity), count);
  qa_shared_inventory_table_configure(game->host.inventory, state->player->actor, entry, NULL, NULL);
}
static void ammo_count(give_state *state, qa_item_id item, double count) {
  qa_q1_services *game = state->game;
  const qa_comp_q1_services *services = state->services;
  if (game->pickup_admission == NULL && services->give_selected_item != NULL) {
    char count_text[40];
    qa_net_js_number(count, count_text, sizeof count_text); /* TS String(count) */
    const char *args[] = {item, count_text};
    if (services->give_selected_item(services->ctx, state->actor, args, 2)) return;
  }
  if (game->pickup_admission == NULL) { configure(state, item, count, count); return; }
  const qa_pickup_admission *admission = game->pickup_admission;
  qa_pickup_supply_offer offer;
  memset(&offer, 0, sizeof offer);
  offer.kind = QA_PICKUP_SUPPLY_AMMO;
  offer.grant = (qa_pickup_ammo_grant){item, qa_js_max(1.0, count)};
  const qa_pickup_supply_preview mapped = admission->preview(admission->ctx, state->actor, offer);
  for (size_t i = 0; i < mapped.ammo_count; i++) configure(state, mapped.ammo[i].item, count, count);
}
static bool ammo_seen_before(give_state *state, size_t i, qa_item_id ammo) {
  for (size_t j = 0; j < i; j++) {
    const qa_item_id other = qa_q1_services_weapon_ammo(state->game, state->weapons[j]);
    if (other != NULL && strcmp(other, ammo) == 0) return true;
  }
  return false;
}

void qa_comp_give_q1(const qa_comp_q1_give_source *source, qa_actor_id actor,
                     const char *const *args, size_t arg_count) {
  qa_q1_services *game = source->game;
  const qa_comp_q1_services *services = source->services;
  qa_q1_player_state *player = qa_q1_player(game, actor);
  if (player == NULL) qa_range_error("Q1 give requires an admitted player");
  if (arg_count == 0 || args[0] == NULL) {
    qa_q1_message(game, &actor, "Usage: give <all|weapons|ammo|health|armor|keys|item> [amount]\n", false, NULL, 0);
    return;
  }
  char *input = lower_dup(args[0]);
  if (input == NULL) qa_range_error("Out of memory");
  const bool has_amount = arg_count > 1;
  const double amount = has_amount ? (double)qa_native_atoi(args[1]) : 0;
  const bool all = strcmp(input, "all") == 0;
  const char **weapons = (const char **)malloc((8 + game->registered_weapon_count) * sizeof *weapons);
  if (weapons == NULL) qa_range_error("Out of memory");
  size_t weapon_count = 0;
  for (size_t i = 0; i < 8; i++) weapons[weapon_count++] = QA_Q1_WEAPONS[i];
  bool hipnotic = false;
  for (size_t i = 0; i < game->registered_weapon_count; i++) {
    const char *id = game->registered_weapons[i].def->id;
    if (strcmp(id, "hipnotic:laser") == 0) hipnotic = true;
    bool seen = false;
    for (size_t j = 0; j < weapon_count; j++) if (strcmp(weapons[j], id) == 0) { seen = true; break; }
    if (!seen) weapons[weapon_count++] = id;
  }
  give_state state = {game, services, player, actor, weapons, weapon_count};

  if (all || strcmp(input, "health") == 0 || strcmp(input, "h") == 0) {
    qa_gameplay_authority_set_health(game->host.combat, player->actor,
                                     has_amount ? amount : strcmp(input, "h") == 0 ? 0 : player->max_health);
    if (!all) goto done;
  }
  if (all || strcmp(input, "armor") == 0 || strcmp(input, "a") == 0) {
    const double points = has_amount ? amount : strcmp(input, "a") == 0 ? 0 : 200;
    qa_regular_armor_state armor;
    memset(&armor, 0, sizeof armor);
    armor.kind = QA_REGULAR_ARMOR_NONE;
    if (points > 0) {
      armor.kind = QA_REGULAR_ARMOR_Q1;
      armor.points = points;
      armor.absorption = points > 150 ? 0.8 : points > 100 ? 0.6 : 0.3;
      armor.item = points > 150 ? "q1:item_armorInv" : points > 100 ? "q1:item_armor2" : "q1:item_armor1";
    }
    qa_gameplay_authority_set_regular_armor(game->host.combat, player->actor, armor);
    if (!all) goto done;
  }
  if (all || strcmp(input, "weapons") == 0) {
    if (services->cheat_arsenal == NULL || !services->cheat_arsenal(services->ctx, actor, "weapons"))
      for (size_t i = 0; i < weapon_count; i++) configure(&state, qa_q1_services_weapon_item(game, weapons[i]), 1, 1);
    if (!all) goto done;
  }
  if (all || strcmp(input, "ammo") == 0) {
    if (services->cheat_arsenal == NULL || !services->cheat_arsenal(services->ctx, actor, "ammo"))
      for (size_t i = 0; i < weapon_count; i++) {
        const qa_item_id ammo = qa_q1_services_weapon_ammo(game, weapons[i]);
        if (ammo == NULL || ammo_seen_before(&state, i, ammo)) continue;
        const double count = strstr(ammo, "nails") != NULL ? 200 : 100;
        configure(&state, ammo, count, count);
      }
    if (!all) goto done;
  }
  if (all || strcmp(input, "keys") == 0) {
    configure(&state, "q1:key/silver", 1, 1);
    configure(&state, "q1:key/gold", 1, 1);
    goto done;
  }
  if (strcmp(input, "items") == 0) {
    static const char *const ITEMS[] = {"quad", "pent", "ring", "suit"};
    for (int i = 0; i < 4; i++) qa_comp_give_q1(source, actor, &ITEMS[i], 1);
    goto done;
  }
  {
    const char *shorthand = strcmp(input, "s") == 0 ? "q1:ammo/shells" : strcmp(input, "n") == 0 ? "q1:ammo/nails"
                            : strcmp(input, "r") == 0 ? "q1:ammo/rockets" : strcmp(input, "c") == 0 ? "q1:ammo/cells"
                            : strcmp(input, "l") == 0 ? "rogue:ammo/lava-nails" : strcmp(input, "m") == 0 ? "rogue:ammo/multi-rockets"
                            : strcmp(input, "p") == 0 ? "rogue:ammo/plasma" : NULL;
    if (shorthand != NULL) {
      bool available = false;
      for (size_t i = 0; i < weapon_count; i++) {
        const qa_item_id ammo = qa_q1_services_weapon_ammo(game, weapons[i]);
        if (ammo != NULL && strcmp(ammo, shorthand) == 0) { available = true; break; }
      }
      if (!available) {
        char text[96];
        (void)snprintf(text, sizeof text, "Ammo %s is unavailable in this arsenal\n", input);
        message(&state, text);
      } else {
        ammo_count(&state, shorthand, has_amount ? amount : 0);
      }
      goto done;
    }
  }
  {
    size_t named_count = arg_count;
    if (arg_count > 1 && is_integer_text(args[arg_count - 1])) named_count = arg_count - 1;
    char *joined = join_args(args, named_count);
    if (joined == NULL) qa_range_error("Out of memory");
    char *named = lower_dup(joined);
    free(joined);
    if (named == NULL) qa_range_error("Out of memory");
    const char *numbered = NULL;
    if (hipnotic && strcmp(input, "6a") == 0) numbered = "hipnotic:proximity";
    else if (hipnotic && strcmp(input, "9") == 0) numbered = "hipnotic:laser";
    else if (hipnotic && strcmp(input, "0") == 0) numbered = "hipnotic:mjolnir";
    else if (strlen(input) == 1 && input[0] >= '2' && input[0] <= '8') numbered = QA_Q1_WEAPONS[input[0] - '1'];
    char named_norm[512];
    normalize(named, named_norm, sizeof named_norm);
    free(named);
    const char *weapon = NULL;
    for (size_t i = 0; i < weapon_count && weapon == NULL; i++) {
      char buffer[512];
      if (numbered != NULL && strcmp(weapons[i], numbered) == 0) { weapon = weapons[i]; break; }
      normalize(weapons[i], buffer, sizeof buffer);
      if (strcmp(buffer, named_norm) == 0) { weapon = weapons[i]; break; }
      normalize(qa_q1_services_weapon_item(game, weapons[i]), buffer, sizeof buffer);
      if (strcmp(buffer, named_norm) == 0) { weapon = weapons[i]; break; }
      char prefixed[512];
      (void)snprintf(prefixed, sizeof prefixed, "weapon_%s", weapons[i]);
      normalize(prefixed, buffer, sizeof buffer);
      if (strcmp(buffer, named_norm) == 0) weapon = weapons[i];
    }
    if (weapon != NULL) {
      const qa_item_id item = qa_q1_services_weapon_item(game, weapon);
      if (game->pickup_admission == NULL && services->give_selected_item != NULL) {
        const char *give_args[] = {item};
        if (services->give_selected_item(services->ctx, actor, give_args, 1)) goto done;
      }
      if (game->pickup_admission == NULL) { configure(&state, item, 1, 1); goto done; }
      const qa_pickup_admission *admission = game->pickup_admission;
      (void)admission->weapon(admission->ctx, player->actor, (qa_pickup_weapon_offer){item, NULL, 0},
                              QA_PICKUP_SELECTION_NEVER);
      goto done;
    }
    qa_item_id ammo = NULL;
    for (size_t i = 0; i < weapon_count && ammo == NULL; i++) {
      const qa_item_id candidate = qa_q1_services_weapon_ammo(game, weapons[i]);
      if (candidate == NULL || ammo_seen_before(&state, i, candidate)) continue;
      const char *tail = strrchr(candidate, '/');
      tail = tail != NULL ? tail + 1 : candidate;
      if (strcmp(candidate, input) == 0 || strcmp(tail, input) == 0) ammo = candidate;
    }
    if (ammo != NULL) {
      ammo_count(&state, ammo, has_amount ? amount : qa_shared_inventory_table_count(game->host.inventory, actor, ammo) + 20);
      goto done;
    }
    const char *classname = strcmp(input, "quad") == 0 ? "item_artifact_super_damage"
                            : strcmp(input, "pent") == 0 ? "item_artifact_invulnerability"
                            : strcmp(input, "ring") == 0 ? "item_artifact_invisibility"
                            : strcmp(input, "suit") == 0 ? "item_artifact_envirosuit" : input;
    if (services->give_selected_item != NULL && services->give_selected_item(services->ctx, actor, args, arg_count))
      goto done;
    if (strncmp(classname, "item_", 5) != 0 && strncmp(classname, "weapon_", 7) != 0) {
      char *joined_args = join_args(args, arg_count);
      char text[600];
      (void)snprintf(text, sizeof text, "Unknown Q1 item: %s\n", joined_args != NULL ? joined_args : "");
      free(joined_args);
      message(&state, text);
      goto done;
    }
    qa_q1_actor *item = qa_q1_create(game, classname, NULL, NULL);
    if (!qa_q1_give_pickup(game, item, actor)) {
      qa_q1_spawn_entity(game, item);
      if (!qa_q1_live(game, item) || item->touch.fn == NULL) {
        char text[300];
        (void)snprintf(text, sizeof text, "Item is not giveable: %s\n", classname);
        message(&state, text);
      } else {
        qa_q1_cancel(game, item);
        item->solid = "trigger";
        qa_touch_contact contact;
        memset(&contact, 0, sizeof contact);
        contact.self = item->actor;
        contact.other = actor;
        (void)qa_actor_callback_table_touch(game->host.callbacks, contact);
      }
    }
    if (qa_q1_live(game, item)) qa_q1_remove(game, item); /* TS finally */
  }
done:
  free(weapons);
  free(input);
}
