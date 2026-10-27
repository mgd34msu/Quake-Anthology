/* Port of src/content/composition/q1/give.ts. GPL-2.0-or-later. */
#include "qa/content/composition/q1/give.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/numeric.h"
#include "qa/foundation/support.h"

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
  const qa_comp_q1_give_source *source;
  qa_comp_q1_game *game;
  const qa_comp_q1_services *services;
  qa_comp_q1_player_state *player;
  qa_actor_id actor;
  const char **weapons; /* deduped WEAPONS + registered keys, borrowed */
  size_t weapon_count;
} give_state;

static void configure(give_state *state, qa_item_id item, double count, double capacity) {
  qa_comp_q1_game *game = state->game;
  size_t entry_count = 0;
  const qa_inventory_entry *entries = game->inventory_entries(game->ctx, state->actor, &entry_count);
  double previous_capacity = 0;
  for (size_t i = 0; i < entry_count; i++)
    if (strcmp(entries[i].item, item) == 0) { previous_capacity = entries[i].capacity; break; }
  double resolved = fmax(fmax(capacity, previous_capacity), count);
  game->inventory_configure(game->ctx, state->player->actor,
                            (qa_inventory_entry){item, fmax(0.0, count), resolved, false, {0}});
}

static void ammo_count(give_state *state, qa_item_id item, double count) {
  qa_comp_q1_game *game = state->game;
  const qa_comp_q1_services *services = state->services;
  if (game->pickup_admission == NULL && services->give_selected_item != NULL) {
    char count_text[32];
    /* TS String(count) for an integer-valued count. */
    (void)snprintf(count_text, sizeof count_text, "%lld", (long long)count);
    const char *args[] = {item, count_text};
    if (services->give_selected_item(services->ctx, state->actor, args, 2)) return;
  }
  if (game->pickup_admission == NULL) { configure(state, item, count, count); return; }
  const qa_pickup_admission *admission = game->pickup_admission;
  qa_pickup_supply_offer offer = {0};
  offer.kind = QA_PICKUP_SUPPLY_AMMO;
  offer.grant = (qa_pickup_ammo_grant){item, fmax(1.0, count)};
  qa_pickup_supply_preview mapped = admission->preview(admission->ctx, state->actor, offer);
  for (size_t i = 0; i < mapped.ammo_count; i++) configure(state, mapped.ammo[i].item, count, count);
}

void qa_comp_give_q1(const qa_comp_q1_give_source *source, qa_actor_id actor,
                     const char *const *args, size_t arg_count) {
  qa_comp_q1_game *game = source->game;
  const qa_comp_q1_services *services = source->services;
  void *ctx = game->ctx;
  qa_comp_q1_player_state *player = game->player(ctx, actor);
  if (player == NULL) qa_range_error("Q1 give requires an admitted player");
  if (arg_count == 0 || args[0] == NULL)
    qa_range_error("Usage: give <all|weapons|ammo|health|armor|keys|item> [amount]");
  char *input = lower_dup(args[0]);
  if (input == NULL) qa_range_error("Out of memory");
  bool has_amount = arg_count > 1;
  double amount = has_amount ? (double)qa_native_atoi(args[1]) : 0;
  bool all = strcmp(input, "all") == 0;
  size_t registered_count = 0;
  const char *const *registered = game->registered_weapons(ctx, &registered_count);
  const char **weapons = (const char **)malloc((QA_COMP_Q1_WEAPON_COUNT + registered_count) * sizeof *weapons);
  if (weapons == NULL) qa_range_error("Out of memory");
  size_t weapon_count = 0;
  for (int i = 0; i < QA_COMP_Q1_WEAPON_COUNT; i++) weapons[weapon_count++] = QA_COMP_Q1_WEAPONS[i];
  for (size_t i = 0; i < registered_count; i++) {
    bool seen = false;
    for (size_t j = 0; j < weapon_count; j++) if (strcmp(weapons[j], registered[i]) == 0) { seen = true; break; }
    if (!seen) weapons[weapon_count++] = registered[i];
  }
  give_state state = {source, game, services, player, actor, weapons, weapon_count};

  if (all || strcmp(input, "health") == 0 || strcmp(input, "h") == 0) {
    game->set_health(ctx, player->actor, has_amount ? amount : strcmp(input, "h") == 0 ? 0 : player->max_health);
    if (!all) goto done;
  }
  if (all || strcmp(input, "armor") == 0 || strcmp(input, "a") == 0) {
    double points = has_amount ? amount : strcmp(input, "a") == 0 ? 0 : 200;
    game->set_armor(ctx, player->actor,
                    points <= 0 ? (qa_comp_q1_armor){"none", 0, 0, NULL}
                                : (qa_comp_q1_armor){"q1", points, points > 150 ? 0.8 : points > 100 ? 0.6 : 0.3,
                                                     points > 150 ? "q1:item_armorInv" : points > 100 ? "q1:item_armor2" : "q1:item_armor1"});
    if (!all) goto done;
  }
  if (all || strcmp(input, "weapons") == 0) {
    if (services->cheat_arsenal == NULL || !services->cheat_arsenal(services->ctx, actor, "weapons"))
      for (size_t i = 0; i < weapon_count; i++) configure(&state, game->weapon_item(ctx, weapons[i]), 1, 1);
    if (!all) goto done;
  }
  if (all || strcmp(input, "ammo") == 0) {
    if (services->cheat_arsenal == NULL || !services->cheat_arsenal(services->ctx, actor, "ammo"))
      for (size_t i = 0; i < weapon_count; i++) {
        qa_item_id ammo = game->weapon_ammo(ctx, weapons[i]);
        if (ammo == NULL) continue;
        bool seen = false;
        for (size_t j = 0; j < i; j++) {
          qa_item_id other = game->weapon_ammo(ctx, weapons[j]);
          if (other != NULL && strcmp(other, ammo) == 0) { seen = true; break; }
        }
        if (!seen) configure(&state, ammo, strstr(ammo, "nails") != NULL ? 200 : 100, strstr(ammo, "nails") != NULL ? 200 : 100);
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
        qa_item_id ammo = game->weapon_ammo(ctx, weapons[i]);
        if (ammo != NULL && strcmp(ammo, shorthand) == 0) { available = true; break; }
      }
      if (!available) qa_range_error("Ammo %s is unavailable in this arsenal", input);
      ammo_count(&state, shorthand, has_amount ? amount : 0);
      goto done;
    }
  }
  {
    bool hipnotic = false;
    for (size_t i = 0; i < registered_count; i++)
      if (strcmp(registered[i], "hipnotic:laser") == 0) { hipnotic = true; break; }
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
    else if (strlen(input) == 1 && input[0] >= '2' && input[0] <= '8') numbered = QA_COMP_Q1_WEAPONS[input[0] - '1'];
    char named_norm[512];
    normalize(named, named_norm, sizeof named_norm);
    const char *weapon = NULL;
    for (size_t i = 0; i < weapon_count && weapon == NULL; i++) {
      char buffer[512];
      if (numbered != NULL && strcmp(weapons[i], numbered) == 0) { weapon = weapons[i]; break; }
      normalize(weapons[i], buffer, sizeof buffer);
      if (strcmp(buffer, named_norm) == 0) { weapon = weapons[i]; break; }
      normalize(game->weapon_item(ctx, weapons[i]), buffer, sizeof buffer);
      if (strcmp(buffer, named_norm) == 0) { weapon = weapons[i]; break; }
      char prefixed[512];
      (void)snprintf(prefixed, sizeof prefixed, "weapon_%s", weapons[i]);
      normalize(prefixed, buffer, sizeof buffer);
      if (strcmp(buffer, named_norm) == 0) weapon = weapons[i];
    }
    free(named);
    if (weapon != NULL) {
      qa_item_id item = game->weapon_item(ctx, weapon);
      if (game->pickup_admission == NULL && services->give_selected_item != NULL) {
        const char *give_args[] = {item};
        if (services->give_selected_item(services->ctx, actor, give_args, 1)) goto done;
      }
      if (game->pickup_admission == NULL) { configure(&state, item, 1, 1); goto done; }
      const qa_pickup_admission *admission = game->pickup_admission;
      admission->weapon(admission->ctx, player->actor, (qa_pickup_weapon_offer){item, NULL, 0},
                        QA_PICKUP_SELECTION_NEVER);
      goto done;
    }
    qa_item_id ammo = NULL;
    for (size_t i = 0; i < weapon_count && ammo == NULL; i++) {
      qa_item_id candidate = game->weapon_ammo(ctx, weapons[i]);
      if (candidate == NULL) continue;
      bool seen = false;
      for (size_t j = 0; j < i; j++) {
        qa_item_id other = game->weapon_ammo(ctx, weapons[j]);
        if (other != NULL && strcmp(other, candidate) == 0) { seen = true; break; }
      }
      if (seen) continue;
      const char *tail = strrchr(candidate, '/');
      tail = tail != NULL ? tail + 1 : candidate;
      if (strcmp(candidate, input) == 0 || strcmp(tail, input) == 0) ammo = candidate;
    }
    if (ammo != NULL) {
      ammo_count(&state, ammo, has_amount ? amount : game->inventory_count(ctx, actor, ammo) + 20);
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
      qa_range_error("Unknown Q1 item: %s", joined_args != NULL ? joined_args : "");
    }
    qa_comp_q1_actor *item = game->create(ctx, classname);
    bool finished = false;
    if (source->give_pickup(source->give_ctx, game, item, actor)) finished = true;
    if (!finished) {
      game->spawn_entity(ctx, item);
      if (!game->live(ctx, item) || !game->actor_has_touch(ctx, item)) {
        char classname_copy[256];
        (void)snprintf(classname_copy, sizeof classname_copy, "%s", classname);
        if (game->live(ctx, item)) game->remove(ctx, item); /* TS finally */
        qa_range_error("Item is not giveable: %s", classname_copy);
      }
      game->cancel(ctx, item);
      game->actor_set_solid(ctx, item, "trigger");
      game->callbacks_touch(ctx, game->actor_owned(ctx, item), actor);
    }
    if (game->live(ctx, item)) game->remove(ctx, item); /* TS finally */
  }
done:
  free(weapons);
  free(input);
}
