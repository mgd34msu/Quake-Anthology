/* Port of src/content/q2/base/player/commands.ts. GPL-2.0-or-later. */
#include "qa/content/q2/base/player/commands.h"
#include "qa/network/common/js_number.h"

#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q2/base/player/index.h"
#include "qa/foundation/support.h"
#include "qa/foundation/js.h"
#include "qa/foundation/js_case.h"

static char *join_args(const char *const *args, size_t count) {
  size_t length = 1;
  for (size_t i = 0; i < count; i++) length += strlen(args[i]) + 1;
  char *out = malloc(length);
  if (!out) qa_range_error("Out of memory");
  out[0] = '\0';
  for (size_t i = 0; i < count; i++) {
    if (i) strcat(out, " ");
    strcat(out, args[i]);
  }
  return out;
}
static char *dup_text(const char *text) {
  char *out = malloc(strlen(text) + 1);
  if (!out) qa_range_error("Out of memory");
  strcpy(out, text);
  return out;
}
/* JS Number.parseInt(value, 10) || 0. */
/* Number.parseInt(value ?? "0", 10) || 0 */
static double js_parse_int(const char *value) {
  double parsed = qa_js_parse_int(value != NULL ? value : "0", 10);
  return isnan(parsed) || parsed == 0 ? 0 : parsed;
}
static void js_number(char *out, size_t size, double value) {
  qa_net_js_number(value, out, size);
}
static void pad_start(char *out, size_t size, const char *text, size_t width, char fill) {
  size_t length = strlen(text);
  if (length >= width) { (void)snprintf(out, size, "%s", text); return; }
  size_t pad = width - length;
  if (pad + length + 1 > size) pad = size > length + 1 ? size - length - 1 : 0;
  memset(out, fill, pad);
  (void)snprintf(out + pad, size - pad, "%s", text);
}
/* TS a.toLowerCase() === b (b already lowered): Unicode case folding of client text. */
static bool equals_lower_text(const char *a, const char *b) { return qa_js_equals_ignore_case(a, b); }

/* TS team(): compares the model or skin half of two skins. */
static bool same_team(qa_q2_player_context *context, const char *left, const char *right) {
  const char *left_slash = strchr(left, '/');
  const char *right_slash = strchr(right, '/');
  bool model_side = (qa_js_to_int32(qa_q2_options_deathmatch_flags(&context->game->options)) & 128) != 0;
  const char *left_part = left_slash ? (model_side ? left : left_slash + 1) : left;
  const char *right_part = right_slash ? (model_side ? right : right_slash + 1) : right;
  size_t left_length = left_slash && model_side ? (size_t)(left_slash - left) : strlen(left_part);
  size_t right_length = right_slash && model_side ? (size_t)(right_slash - right) : strlen(right_part);
  return left_length == right_length && strncmp(left_part, right_part, left_length) == 0;
}

/* TS weapons.registeredDefinitions().find(candidate => candidate.item === item). */
static const qa_q2w_definition *registered_weapon(qa_q2_player_context *context, qa_item_id item) {
  size_t count = 0;
  const qa_q2w_definition *const *definitions = qa_q2w_weapons_registered_definitions(context->weapons, &count);
  for (size_t i = 0; i < count; i++)
    if (definitions[i]->item && strcmp(definitions[i]->item, item) == 0) return definitions[i];
  return NULL;
}
/* The player entity as TS Q2WeaponOwner ({ actor, viewHeight }). */
static qa_q2w_owner weapon_owner(qa_q2_player_context *context) {
  return (qa_q2w_owner){context->entity->actor, context->entity->view_height};
}
static void print_to(qa_q2_player_context *context, const char *text) {
  qa_q2_player_event event = {0};
  event.kind = "print";
  event.has_target = true; event.target = context->entity->actor.id;
  event.level = "high"; event.text = text;
  context->hooks->emit(context->hooks->ctx, event);
}

static void publish_inventory(qa_q2_player_context *context) {
  size_t entry_count = 0;
  const qa_inventory_entry *entries = qa_shared_inventory_table_entries(context->game->host->inventory, context->entity->actor.id, &entry_count);
  size_t item_count = 0;
  const qa_q2_inventory_item *list = qa_q2_item_module_list(context->items, &item_count);
  qa_q2_player_item_label *labels = malloc((item_count ? item_count : 1) * sizeof *labels);
  if (!labels) qa_range_error("Out of memory");
  for (size_t i = 0; i < item_count; i++) labels[i] = (qa_q2_player_item_label){list[i].id, list[i].name};
  qa_q2_player_event event = {0};
  event.kind = "inventory";
  event.actor = context->entity->actor.id;
  event.entries = entries; event.entry_count = entry_count;
  event.has_visible = true; event.visible = context->state->show_inventory;
  event.has_selected = true; event.selected = context->state->selected_item;
  event.labels = labels; event.label_count = item_count;
  context->hooks->emit(context->hooks->ctx, event);
  free(labels);
}

/* itemlist order in g_items.c, restricted to what a player can hold; other items keep catalog order after it. */
static const char *const INVENTORY_ORDER[] = {
  "item_power_screen", "item_power_shield", "weapon_blaster", "weapon_shotgun", "weapon_supershotgun",
  "weapon_machinegun", "weapon_chaingun", "ammo_grenades", "weapon_grenadelauncher", "weapon_rocketlauncher",
  "weapon_hyperblaster", "weapon_railgun", "weapon_bfg", "ammo_shells", "ammo_bullets", "ammo_cells",
  "ammo_rockets", "ammo_slugs", "item_quad", "item_invulnerability", "item_silencer", "item_breather",
  "item_enviro", "item_ancient_head", "item_adrenaline", "item_bandolier", "item_pack", "key_data_cd",
  "key_power_cube", "key_pyramid", "key_data_spinner", "key_pass", "key_blue_key", "key_red_key",
  "key_commander_head", "key_airstrike_target",
};
static double item_rank(const char *classname) {
  size_t count = sizeof INVENTORY_ORDER / sizeof INVENTORY_ORDER[0];
  for (size_t i = 0; i < count; i++)
    if (strcmp(INVENTORY_ORDER[i], classname) == 0) return (double)i;
  return (double)count;
}
static int compare_item_rank(const void *left, const void *right, void *ctx) {
  (void)ctx;
  double difference = item_rank(((const qa_q2_inventory_item *)left)->classname) - item_rank(((const qa_q2_inventory_item *)right)->classname);
  return difference < 0 ? -1 : difference > 0 ? 1 : 0;
}

/* filter: "all" | "weapon" | "power". */
static void select_item(qa_q2_player_context *context, double direction, const char *filter) {
  qa_q2_entity *entity = context->entity;
  qa_q2_game_services *game = context->game;
  qa_q2_base_player_state *state = context->state;
  size_t count = 0;
  const qa_q2_inventory_item *catalog = qa_q2_item_module_list(context->items, &count);
  /* [...items.list()].sort((left, right) => itemRank(left.classname) - itemRank(right.classname)) */
  qa_q2_inventory_item *list = malloc((count ? count : 1) * sizeof *list);
  if (!list) qa_range_error("Out of memory");
  if (count) memcpy(list, catalog, count * sizeof *list);
  qa_js_stable_sort(list, count, sizeof *list, compare_item_rank, NULL);
  int64_t first = -1;
  for (size_t i = 0; i < count; i++)
    if (state->selected_item && list[i].id && strcmp(list[i].id, state->selected_item) == 0) { first = (int64_t)i; break; }
  for (size_t index = 1; index <= count; index++) {
    int64_t at = (first + (int64_t)direction * (int64_t)index + (int64_t)count * 2) % (int64_t)count;
    const qa_q2_inventory_item *item = &list[at];
    if (item->usable && qa_shared_inventory_table_count(game->host->inventory, entity->actor.id, item->id) > 0 &&
        (strcmp(filter, "all") == 0 || (strcmp(filter, "weapon") == 0 && item->weapon) ||
         (strcmp(filter, "power") == 0 && strcmp(item->kind, "power") == 0))) {
      state->selected_item = item->id;
      free(list);
      return;
    }
  }
  state->selected_item = NULL;
  free(list);
}
/* TS validateSelection: a selected item the player ran out of moves to the next one. */
static void validate_selection(qa_q2_player_context *context) {
  qa_q2_base_player_state *state = context->state;
  if (state->selected_item != NULL &&
      qa_shared_inventory_table_count(context->game->host->inventory, context->entity->actor.id, state->selected_item) == 0)
    select_item(context, 1, "all");
}

static void use_item_value(qa_q2_player_context *context, const char *value) {
  qa_q2_entity *entity = context->entity;
  qa_q2_game_services *game = context->game;
  qa_q2_inventory_item item;
  if (!qa_q2_item_module_lookup(context->items, value, &item)) {
    char message[512];
    (void)snprintf(message, sizeof message, "unknown item: %s\n", value);
    print_to(context, message);
    return;
  }
  if (!item.usable) { print_to(context, "Item is not usable.\n"); return; }
  if (qa_shared_inventory_table_count(game->host->inventory, entity->actor.id, item.id) == 0) {
    char message[512];
    (void)snprintf(message, sizeof message, "Out of item: %s\n", item.name);
    print_to(context, message);
    return;
  }
  const qa_q2w_definition *weapon = registered_weapon(context, item.id);
  if (weapon) {
    const bool select_empty = context->hooks->select_empty != NULL && context->hooks->select_empty(context->hooks->ctx);
    qa_q2w_selection result = qa_q2w_weapons_request_weapon(context->weapons, weapon_owner(context), game, weapon->name, select_empty);
    if (result == QA_Q2W_SEL_NO_AMMO || result == QA_Q2W_SEL_NOT_ENOUGH_AMMO) {
      qa_q2_inventory_item ammo_item;
      const char *ammo = weapon->ammo != NULL && qa_q2_item_module_lookup(context->items, weapon->ammo, &ammo_item) ? ammo_item.name : NULL;
      char message[512];
      (void)snprintf(message, sizeof message, "%s %s for %s.\n", result == QA_Q2W_SEL_NO_AMMO ? "No" : "Not enough",
                     ammo != NULL ? ammo : "ammo", item.name);
      print_to(context, message);
      return;
    }
  } else (void)qa_q2_item_module_use(context->items, entity->actor, item.id, game, 30);
  validate_selection(context);
}

static void drop_item_value(qa_q2_players *players, qa_q2_player_context *context, const char *value) {
  qa_q2_entity *entity = context->entity;
  qa_q2_game_services *game = context->game;
  qa_q2_inventory_item item;
  if (!qa_q2_item_module_lookup(context->items, value, &item)) {
    char message[512];
    (void)snprintf(message, sizeof message, "unknown item: %s\n", value);
    print_to(context, message);
    return;
  }
  if (!item.droppable || (game->options.mode == QA_Q2_MODE_COOP && item.stay_coop && !qa_q2_players_can_drop_coop_stay_items(players, game))) {
    print_to(context, "Item is not dropable.\n");
    return;
  }
  double count = qa_shared_inventory_table_count(game->host->inventory, entity->actor.id, item.id);
  if (count == 0) {
    char message[512];
    (void)snprintf(message, sizeof message, "Out of item: %s\n", item.name);
    print_to(context, message);
    return;
  }
  const qa_q2w_definition *weapon = registered_weapon(context, item.id);
  if (weapon && !qa_q2w_weapons_can_drop(context->weapons, weapon_owner(context), game, weapon->name)) {
    print_to(context, "Can't drop current weapon\n");
    return;
  }
  qa_q2w_state *current_state = qa_q2_players_weapon_state(context->players, entity->actor.id);
  const char *current = current_state ? current_state->weapon : NULL;
  bool grenade = current && strcmp(current, "grenades") == 0 && strcmp(item.id, "q2:ammo_grenades") == 0;
  double quantity = strcmp(item.kind, "ammo") == 0 ? qa_js_min(item.quantity, count) : 1;
  if (grenade && count - quantity <= 0) { print_to(context, "Can't drop current weapon\n"); return; }
  qa_q2_drop_options options = {0};
  options.player_death = false;
  qa_q2_entity *dropped = qa_q2_item_module_drop(context->items, entity, game, item.id, options);
  if (dropped) {
    dropped->count = quantity;
    (void)qa_shared_inventory_table_consume(game->host->inventory, entity->actor, item.id, quantity);
  }
  if (strcmp(item.kind, "power-armor") == 0) qa_q2_item_module_sync_power_armor(context->items, entity->actor, game, NULL);
  validate_selection(context);
}

static void weapon_cycle(qa_q2_player_context *context, double direction) {
  qa_q2w_state *state = qa_q2_players_weapon_state(context->players, context->entity->actor.id);
  const char *current = state ? state->weapon : NULL;
  size_t count = 0;
  const qa_q2w_definition *const *definitions = qa_q2w_weapons_registered_definitions(context->weapons, &count);
  int64_t index = -1;
  for (size_t i = 0; i < count; i++)
    if (current && definitions[i]->name && strcmp(definitions[i]->name, current) == 0) { index = (int64_t)i; break; }
  for (size_t step = 1; step <= count; step++) {
    int64_t at = (index + (int64_t)direction * (int64_t)step + (int64_t)count * 2) % (int64_t)count;
    const qa_q2w_definition *weapon = definitions[at];
    if (qa_q2w_weapons_request_weapon(context->weapons, weapon_owner(context), context->game, weapon->name, false) == QA_Q2W_SEL_SELECTED) break;
  }
}

bool qa_q2_chat_allowed(qa_q2_player_context *context) {
  qa_q2_base_player_state *state = context->state;
  qa_q2_base_player_rules *rules = context->rules;
  double now = context->game->host->now(context->game->host->ctx);
  if (rules->flood_messages != 0) {
    if (now < state->flood_lock_until) {
      char message[160];
      (void)snprintf(message, sizeof message, "You can't talk for %s more seconds\n", QA_JS_NUMBER(trunc(state->flood_lock_until - now)));
      print_to(context, message);
      return false;
    }
    /* floodTimes[Math.max(0, length - Math.min(10, Math.trunc(floodMessages)))]: a negative
     * or NaN cvar indexes past the end (undefined), never a size_t cast of it. */
    double at = qa_js_max(0, (double)state->flood_time_count - qa_js_min(10, trunc(rules->flood_messages)));
    bool has_previous = at >= 0 && at < (double)state->flood_time_count;
    double previous = has_previous ? state->flood_times[(size_t)at] : 0;
    if ((double)state->flood_time_count >= rules->flood_messages && has_previous && now - previous < rules->flood_seconds) {
      state->flood_lock_until = now + rules->flood_wait_seconds;
      char message[160];
      (void)snprintf(message, sizeof message, "Flood protection:  You can't talk for %s seconds.\n", QA_JS_NUMBER(trunc(rules->flood_wait_seconds)));
      print_to(context, message);
      return false;
    }
    if (state->flood_time_count == state->flood_time_cap) {
      size_t cap = state->flood_time_cap ? state->flood_time_cap * 2 : 8;
      double *grown = realloc(state->flood_times, cap * sizeof *grown);
      if (!grown) qa_range_error("Out of memory");
      state->flood_times = grown; state->flood_time_cap = cap;
    }
    state->flood_times[state->flood_time_count++] = now;
    if (state->flood_time_count > 10) {
      void * destination = state->flood_times;
      const void * source_value = state->flood_times + 1;
      unsigned long byte_count = --state->flood_time_count * sizeof *state->flood_times;
      memmove(destination, source_value, byte_count);
    }
  }
  return true;
}

static void say(qa_q2_players *players, qa_q2_player_context *context, const char *const *args, size_t arg_count, bool team_only) {
  qa_q2_base_player_state *state = context->state;
  qa_q2_game_services *game = context->game;
  qa_q2_entity *entity = context->entity;
  if (arg_count == 0 || !qa_q2_chat_allowed(context)) return;
  bool is_team = team_only && (qa_js_to_int32(qa_q2_options_deathmatch_flags(&game->options)) & (64 | 128)) != 0;
  char *words = join_args(args, arg_count);
  if (words[0] == '"') {
    size_t length = strlen(words);
    if (length >= 2 && words[length - 1] == '"') { memmove(words, words + 1, length - 2); words[length - 2] = '\0'; }
    else if (length == 1) words[0] = '\0';
    else memmove(words, words + 1, length);
  }
  char message[512];
  if (is_team) (void)snprintf(message, sizeof message, "(%s): %s", state->name, words);
  else (void)snprintf(message, sizeof message, "%s: %s", state->name, words);
  free(words);
  /* TS slice(0, 150) + "\n" (byte-wise for ASCII chat). */
  if (strlen(message) > 150) message[150] = '\0';
  size_t length = strlen(message);
  message[length] = '\n'; message[length + 1] = '\0';
  size_t count = qa_q2_players_state_count(players);
  for (size_t i = 0; i < count; i++) {
    qa_actor_id actor;
    qa_q2_base_player_state *other = NULL;
    if (!qa_q2_players_state_at(players, i, &actor, &other)) continue;
    if (!other->connected || (is_team && !same_team(context, other->skin, state->skin))) continue;
    qa_q2_player_event event = {0};
    event.kind = "print";
    event.has_target = true; event.target = actor;
    event.level = "chat"; event.text = message;
    context->hooks->emit(context->hooks->ctx, event);
  }
  if (!qa_q2_players_get_state(players, entity->actor.id)) qa_range_error("Chat actor disconnected during synchronous command");
}

static void give_items(qa_q2_player_context *context, const char *const *args, size_t arg_count);
static void check_power_armor_after_give(qa_q2_player_context *context);

static void chat_handler(qa_q2_players *players, qa_q2_player_context *context, const char *const *args, size_t arg_count, const char *command) {
  say(players, context, args, arg_count, strcmp(command, "say_team") == 0);
}

static void list_players_handler(qa_q2_players *players, qa_q2_player_context *context, const char *const *args, size_t arg_count, const char *command) {
  (void)args; (void)arg_count;
  qa_q2_game_services *game = context->game;
  size_t total = qa_q2_players_state_count(players);
  qa_q2_base_player_state **list = malloc((total ? total : 1) * sizeof *list);
  if (!list) qa_range_error("Out of memory");
  size_t count = 0;
  for (size_t i = 0; i < total; i++) {
    qa_actor_id actor;
    qa_q2_base_player_state *state = NULL;
    if (qa_q2_players_state_at(players, i, &actor, &state) && state->connected) list[count++] = state;
  }
  bool by_score = strcmp(command, "players") == 0;
  for (size_t i = 1; i < count; i++) {
    qa_q2_base_player_state *key = list[i];
    double key_value = by_score ? key->score : key->slot;
    size_t j = i;
    while (j > 0 && (by_score ? list[j - 1]->score : list[j - 1]->slot) > key_value) { list[j] = list[j - 1]; j--; }
    list[j] = key;
  }
  char *message = dup_text("");
  for (size_t i = 0; i < count; i++) {
    qa_q2_base_player_state *row = list[i];
    double seconds = trunc(game->host->now(game->host->ctx) - row->entered_at);
    char line[512];
    if (by_score) {
      char raw[64], padded[64];
      js_number(raw, sizeof raw, row->score);
      pad_start(padded, sizeof padded, raw, 3, ' ');
      (void)snprintf(line, sizeof line, "%s %s\n", padded, row->name);
    } else {
      char raw[64], minutes[64], secs[64], ping[64], score[64];
      js_number(raw, sizeof raw, trunc(seconds / 60)); pad_start(minutes, sizeof minutes, raw, 2, '0');
      js_number(raw, sizeof raw, fmod(seconds, 60)); pad_start(secs, sizeof secs, raw, 2, '0');
      js_number(raw, sizeof raw, row->ping); pad_start(ping, sizeof ping, raw, 4, ' ');
      js_number(raw, sizeof raw, row->score); pad_start(score, sizeof score, raw, 3, ' ');
      (void)snprintf(line, sizeof line, "%s:%s %s %s %s%s\n", minutes, secs, ping, score, row->name, row->spectator ? " (spectator)" : "");
    }
    if (strlen(message) + strlen(line) > 1280) {
      char *grown = realloc(message, strlen(message) + 5);
      if (!grown) qa_range_error("Out of memory");
      message = grown;
      strcat(message, "...\n");
      break;
    }
    char *grown = realloc(message, strlen(message) + strlen(line) + 1);
    if (!grown) qa_range_error("Out of memory");
    message = grown;
    strcat(message, line);
  }
  if (by_score) {
    char suffix[64];
    (void)snprintf(suffix, sizeof suffix, "\n%zu players\n", count);
    char *grown = realloc(message, strlen(message) + strlen(suffix) + 1);
    if (!grown) qa_range_error("Out of memory");
    message = grown;
    strcat(message, suffix);
  }
  print_to(context, message);
  free(message);
  free(list);
}

static void score_handler(qa_q2_players *players, qa_q2_player_context *context, const char *const *args, size_t arg_count, const char *command) {
  (void)args; (void)arg_count; (void)command;
  qa_q2_entity *entity = context->entity;
  qa_q2_game_services *game = context->game;
  qa_q2_base_player_state *state = context->state;
  state->show_inventory = false; state->show_help = false;
  if (game->options.mode == QA_Q2_MODE_SINGLEPLAYER) {
    publish_inventory(context);
    return;
  }
  state->show_scores = !state->show_scores;
  publish_inventory(context);
  if (state->show_scores) qa_q2_players_scoreboard(players, entity, game, true);
}

static void show_help_handler(qa_q2_players *players, qa_q2_player_context *context, const char *const *args, size_t arg_count, const char *command) {
  (void)args; (void)arg_count; (void)command;
  qa_q2_entity *entity = context->entity;
  qa_q2_game_services *game = context->game;
  qa_q2_base_player_state *state = context->state;
  if (game->options.mode == QA_Q2_MODE_DEATHMATCH) {
    score_handler(players, context, NULL, 0, "score");
    return;
  }
  state->show_inventory = false; state->show_scores = false;
  publish_inventory(context);
  if (game->options.edition == QA_Q2_EDITION_CLASSIC) {
    /* Cmd_Help_f: an open computer closes only when no objective changed since it was read. */
    if (state->show_help && state->game_help_changed == game->help_changed) state->show_help = false;
    else {
      state->show_help = true;
      state->help_changed = 0;
    }
  } else state->show_help = !state->show_help;
  qa_q2_player_event event = {0};
  event.kind = "help"; event.actor = entity->actor.id;
  event.has_visible = true; event.visible = state->show_help;
  context->hooks->emit(context->hooks->ctx, event);
}

static void use_handler(qa_q2_players *players, qa_q2_player_context *context, const char *const *args, size_t arg_count, const char *command) {
  (void)players; (void)command;
  char *value = join_args(args, arg_count);
  use_item_value(context, value);
  free(value);
}
static void drop_handler(qa_q2_players *players, qa_q2_player_context *context, const char *const *args, size_t arg_count, const char *command) {
  (void)command;
  char *value = join_args(args, arg_count);
  drop_item_value(players, context, value);
  free(value);
}
static void inventory_handler(qa_q2_players *players, qa_q2_player_context *context, const char *const *args, size_t arg_count, const char *command) {
  (void)players; (void)args; (void)arg_count; (void)command;
  qa_q2_base_player_state *state = context->state;
  state->show_scores = false; state->show_help = false; state->show_inventory = !state->show_inventory;
  publish_inventory(context);
}
static void select_handler(qa_q2_players *players, qa_q2_player_context *context, const char *const *args, size_t arg_count, const char *command) {
  (void)args; (void)arg_count;
  qa_q2_entity *entity = context->entity;
  qa_q2_game_services *game = context->game;
  qa_q2_base_player_state *state = context->state;
  bool next = strncmp(command, "invnext", 7) == 0;
  if (state->has_chase_target) qa_q2_players_chase(players, entity, game, next ? 1 : -1, false);
  else {
    size_t length = strlen(command);
    const char *filter = length && command[length - 1] == 'w' ? "weapon" : length && command[length - 1] == 'p' ? "power" : "all";
    select_item(context, next ? 1 : -1, filter);
    if (state->show_inventory) publish_inventory(context);
  }
}
static void selected_handler(qa_q2_players *players, qa_q2_player_context *context, const char *const *args, size_t arg_count, const char *command) {
  (void)args; (void)arg_count;
  qa_q2_entity *entity = context->entity;
  qa_q2_game_services *game = context->game;
  qa_q2_base_player_state *state = context->state;
  if (state->selected_item == NULL || qa_shared_inventory_table_count(game->host->inventory, entity->actor.id, state->selected_item) == 0)
    select_item(context, 1, "all");
  if (state->selected_item == NULL) print_to(context, strcmp(command, "invuse") == 0 ? "No item to use.\n" : "No item to drop.\n");
  else if (strcmp(command, "invuse") == 0) use_item_value(context, state->selected_item);
  else drop_item_value(players, context, state->selected_item);
}
static void previous_weapon_handler(qa_q2_players *players, qa_q2_player_context *context, const char *const *args, size_t arg_count, const char *command) {
  (void)players; (void)args; (void)arg_count; (void)command;
  weapon_cycle(context, -1);
}
static void next_weapon_handler(qa_q2_players *players, qa_q2_player_context *context, const char *const *args, size_t arg_count, const char *command) {
  (void)players; (void)args; (void)arg_count; (void)command;
  weapon_cycle(context, 1);
}
static void last_weapon_handler(qa_q2_players *players, qa_q2_player_context *context, const char *const *args, size_t arg_count, const char *command) {
  (void)players; (void)args; (void)arg_count; (void)command;
  qa_q2w_state *state = qa_q2_players_weapon_state(context->players, context->entity->actor.id);
  const char *weapon = state ? state->last_weapon : NULL;
  if (weapon) (void)qa_q2w_weapons_request_weapon(context->weapons, weapon_owner(context), context->game, weapon, false);
}
static void kill_handler(qa_q2_players *players, qa_q2_player_context *context, const char *const *args, size_t arg_count, const char *command) {
  (void)players; (void)args; (void)arg_count; (void)command;
  qa_q2_entity *entity = context->entity;
  qa_q2_game_services *game = context->game;
  qa_q2_base_player_state *state = context->state;
  if ((game->options.edition == QA_Q2_EDITION_RERELEASE && state->spectator) ||
      game->host->now(game->host->ctx) - state->respawn_time < 5) return;
  state->god = false;
  entity->flags = (double)(qa_js_to_int32(entity->flags) & ~16);
  qa_combat_trait_changes tc = {0};
  tc.has_invulnerable = true; tc.invulnerable = false;
  qa_gameplay_authority_set_traits(game->host->combat, entity->actor, tc);
  qa_combat_state_result combat = qa_gameplay_authority_read(game->host->combat, entity->actor.id);
  double health = combat.has_value ? combat.value.health : 0;
  if (health <= 0) return;
  qa_damage_request request = {0};
  request.attack = game->attack(game->ctx, entity, entity->actor.id, &entity->actor.id, 23, 32, NULL);
  request.target = entity->actor.id;
  request.amount = health;
  request.knockback = 0;
  request.direction = QA_Q2_ZERO;
  request.point = qa_q2_game_body(game, entity).origin;
  request.normal = QA_Q2_ZERO;
  request.delivery = "direct";
  (void)qa_gameplay_authority_apply(game->host->combat, request, NULL, NULL);
}
static void putaway_handler(qa_q2_players *players, qa_q2_player_context *context, const char *const *args, size_t arg_count, const char *command) {
  (void)players; (void)args; (void)arg_count; (void)command;
  context->state->show_scores = false;
  context->state->show_help = false;
  context->state->show_inventory = false;
}
static void gesture_handler(qa_q2_players *players, qa_q2_player_context *context, const char *const *args, size_t arg_count, const char *command) {
  (void)players; (void)command;
  qa_q2_entity *entity = context->entity;
  qa_q2_base_player_state *state = context->state;
  if (context->movement.ducked || state->animation_priority > 1) return;
  static const struct { const char *name; double first, last; } animations[] = {
      {"flipoff", 72, 83}, {"salute", 84, 94}, {"taunt", 95, 111}, {"wave", 112, 122}, {"point", 123, 134}};
  const char *text = arg_count > 0 ? args[0] : "0";
  /* JS parseInt: NaN (no digits) indexes nothing and falls back. */
  const char *scan = text;
  while (isspace((unsigned char)*scan)) scan++;
  if (*scan == '+' || *scan == '-') scan++;
  double wave = js_parse_int(text);
  size_t at = isdigit((unsigned char)*scan) && wave >= 0 && wave < 5 ? (size_t)wave : 4;
  state->animation_priority = 1;
  entity->frame = animations[at].first - 1;
  state->animation_end = animations[at].last;
  char message[64];
  (void)snprintf(message, sizeof message, "%s\n", animations[at].name);
  print_to(context, message);
}

bool qa_q2_cheats_allowed(qa_q2_player_context *context) {
  qa_q2_game_services *game = context->game;
  bool gated = game->options.edition == QA_Q2_EDITION_RERELEASE ? game->options.max_clients > 1
                                                                : game->options.mode == QA_Q2_MODE_DEATHMATCH;
  if (gated && !context->rules->cheats) {
    print_to(context, "You must run the server with '+set cheats 1' to enable this command.\n");
    return false;
  }
  return true;
}

static void cheat_handler(qa_q2_players *players, qa_q2_player_context *context, const char *const *args, size_t arg_count, const char *command) {
  (void)players;
  qa_q2_entity *entity = context->entity;
  qa_q2_game_services *game = context->game;
  qa_q2_base_player_state *state = context->state;
  if (!qa_q2_cheats_allowed(context)) return;
  if (strcmp(command, "god") == 0) {
    state->god = !state->god;
    entity->flags = (double)(qa_js_to_int32(entity->flags) ^ 16);
    qa_combat_trait_changes tc = {0};
    tc.has_invulnerable = true; tc.invulnerable = state->god;
    qa_gameplay_authority_set_traits(game->host->combat, entity->actor, tc);
    print_to(context, state->god ? "godmode ON\n" : "godmode OFF\n");
  } else if (strcmp(command, "notarget") == 0) {
    state->notarget = !state->notarget;
    entity->flags = (double)(qa_js_to_int32(entity->flags) ^ 32);
    print_to(context, state->notarget ? "notarget ON\n" : "notarget OFF\n");
  } else if (strcmp(command, "noclip") == 0) {
    state->noclip = !state->noclip;
    qa_q2_player_movement_change change = {0};
    change.kind = "noclip"; change.enabled = state->noclip;
    context->hooks->set_movement(context->hooks->ctx, entity->actor.id, change);
    print_to(context, state->noclip ? "noclip ON\n" : "noclip OFF\n");
  } else if (strcmp(command, "target") == 0) {
    char *name = join_args(args, arg_count);
    size_t count = 0;
    qa_q2_entity *const *targets = game->targets(game->ctx, name, &count);
    for (size_t i = 0; i < count; i++) {
      qa_actor_id self_id = entity->actor.id;
      (void)qa_actor_callback_table_use(game->host->callbacks, targets[i]->actor, &self_id, &self_id);
    }
    free(name);
  } else give_items(context, args, arg_count);
}

#define DOC(summary_text, usage_text) {.summary = (summary_text), .usage = (usage_text), .examples = NULL, .example_count = 0, .allowed_values = NULL, .allowed_value_count = 0}
const qa_q2_client_command_definition qa_q2_client_commands[] = {
    {"say", DOC("Send a chat message.", "say <message>"), true, chat_handler},
    {"say_team", DOC("Send chat to your team when model or skin teams are enabled.", "say_team <message>"), true, chat_handler},
    {"players", DOC("List connected players sorted by score.", "players"), true, list_players_handler},
    {"playerlist", DOC("List connected players with time, ping, score, and spectator status.", "playerlist"), true, list_players_handler},
    {"score", DOC("Toggle the scoreboard.", "score"), true, score_handler},
    {"help", DOC("Toggle mission help, or show the deathmatch scoreboard.", "help"), true, show_help_handler},
    {"use", DOC("Use an inventory item or select a weapon by name.", "use <item name>"), false, use_handler},
    {"drop", DOC("Drop an inventory item by name.", "drop <item name>"), false, drop_handler},
    {"inven", DOC("Toggle the inventory display.", "inven"), false, inventory_handler},
    {"invnext", DOC("Select the next usable item, or cycle chase targets.", "invnext"), false, select_handler},
    {"invprev", DOC("Select the previous usable item, or cycle chase targets.", "invprev"), false, select_handler},
    {"invnextw", DOC("Select the next weapon, or cycle chase targets.", "invnextw"), false, select_handler},
    {"invprevw", DOC("Select the previous weapon, or cycle chase targets.", "invprevw"), false, select_handler},
    {"invnextp", DOC("Select the next powerup, or cycle chase targets.", "invnextp"), false, select_handler},
    {"invprevp", DOC("Select the previous powerup, or cycle chase targets.", "invprevp"), false, select_handler},
    {"invuse", DOC("Use the selected inventory item.", "invuse"), false, selected_handler},
    {"invdrop", DOC("Drop the selected inventory item.", "invdrop"), false, selected_handler},
    {"weapprev", DOC("Select the previous available weapon.", "weapprev"), false, previous_weapon_handler},
    {"weapnext", DOC("Select the next available weapon.", "weapnext"), false, next_weapon_handler},
    {"weaplast", DOC("Select the last weapon used.", "weaplast"), false, last_weapon_handler},
    {"kill", DOC("Kill yourself after the five-second respawn delay.", "kill"), false, kill_handler},
    {"putaway", DOC("Close score, help, and inventory displays.", "putaway"), false, putaway_handler},
    {"wave", DOC("Play a gesture: 0 flipoff, 1 salute, 2 taunt, 3 wave, 4 point.", "wave [0-4]"), false, gesture_handler},
    {"god", DOC("Toggle invulnerability; deathmatch requires cheats.", "god"), false, cheat_handler},
    {"notarget", DOC("Toggle monster targeting immunity; deathmatch requires cheats.", "notarget"), false, cheat_handler},
    {"noclip", DOC("Toggle movement through walls; deathmatch requires cheats.", "noclip"), false, cheat_handler},
    {"give", DOC("Give items, health, weapons, ammo, or armor; deathmatch requires cheats.", "give <all|health [amount]|weapons|ammo|armor|item name [amount]>"), false, cheat_handler},
    {"target", DOC("Activate entities with the supplied target name; deathmatch requires cheats.", "target <targetname>"), false, cheat_handler},
};
const size_t qa_q2_client_command_count = sizeof qa_q2_client_commands / sizeof qa_q2_client_commands[0];

static bool run_lowered(qa_q2_players *players, qa_q2_player_context *context, const char *source_command,
                        const char *command, const char *const *args, size_t arg_count);
bool qa_q2_run_client_command(qa_q2_players *players, qa_q2_player_context *context,
                              const char *source_command, const char *const *args, size_t arg_count) {
  /* TS toLowerCase over the whole client-supplied name (no length cap). */
  char *command = qa_js_to_lower_case(source_command); /* Unicode, as String.toLowerCase */
  if (!command) qa_range_error("Out of memory");
  bool handled = run_lowered(players, context, source_command, command, args, arg_count);
  free(command);
  return handled;
}
static bool run_lowered(qa_q2_players *players, qa_q2_player_context *context, const char *source_command,
                        const char *command, const char *const *args, size_t arg_count) {
  if (context->hooks->command && context->hooks->command(context->hooks->ctx, context->entity, context->game, command, args, arg_count) == true)
    return true;
  const qa_q2_client_command_definition *definition = NULL;
  for (size_t i = 0; i < qa_q2_client_command_count; i++)
    if (strcmp(qa_q2_client_commands[i].name, command) == 0) { definition = &qa_q2_client_commands[i]; break; }
  if (strcmp(qa_q2_players_intermission(players).kind, "playing") != 0 && !(definition && definition->intermission)) return true;
  if (!definition) {
    const char **say_args = malloc((arg_count + 1) * sizeof *say_args);
    if (!say_args) qa_range_error("Out of memory");
    say_args[0] = source_command;
    for (size_t i = 0; i < arg_count; i++) say_args[i + 1] = args[i];
    say(players, context, say_args, arg_count + 1, false);
    free(say_args);
  } else definition->run(players, context, args, arg_count, command);
  return true;
}

static const qa_inventory_entry *find_entry(qa_q2_player_context *context, qa_item_id item) {
  size_t count = 0;
  const qa_inventory_entry *entries = qa_shared_inventory_table_entries(context->game->host->inventory, context->entity->actor.id, &count);
  for (size_t i = 0; i < count; i++) if (entries[i].item && strcmp(entries[i].item, item) == 0) return &entries[i];
  return NULL;
}
static void write_item(qa_q2_player_context *context, const qa_q2_inventory_item *item, double count) {
  const qa_inventory_entry *entry = find_entry(context, item->id);
  qa_inventory_entry next = {0};
  next.item = item->id;
  next.count = count;
  next.capacity = entry ? entry->capacity : item->capacity;
  next.has_count_policy = true;
  next.count_policy.kind = "source-counter";
  next.count_policy.arithmetic = "int32";
  qa_shared_inventory_table_configure(context->game->host->inventory, context->entity->actor, next, NULL, NULL);
}
static void pickup_classname(qa_q2_player_context *context, const char *classname) {
  qa_q2_entity *temporary = context->game->create(context->game->ctx, classname, NULL, 0);
  if (!qa_q2_item_module_spawn(context->items, temporary, context->game) ||
      !qa_session_actor_registry_is_live(context->game->host->actors, temporary->actor.id)) return;
  context->game->cancel(context->game->ctx, temporary);
  qa_q2_item_module_touch(context->items, temporary, context->game, context->entity->actor.id);
  if (qa_session_actor_registry_is_live(context->game->host->actors, temporary->actor.id))
    context->game->remove(context->game->ctx, temporary);
}

static void give_items(qa_q2_player_context *context, const char *const *args, size_t arg_count) {
  qa_q2_entity *entity = context->entity;
  qa_q2_game_services *game = context->game;
  qa_q2_player_hooks *hooks = context->hooks;
  char *requested = join_args(args, arg_count);
  { char *lowered = qa_js_to_lower_case(requested); if (!lowered) qa_range_error("Out of memory"); free(requested); requested = lowered; }
  bool all = strcmp(requested, "all") == 0;
  bool rerelease = game->options.edition == QA_Q2_EDITION_RERELEASE;
  size_t catalog_count = 0;
  const qa_q2_inventory_item *catalog = qa_q2_item_module_list(context->items, &catalog_count);
  const char *first = arg_count > 0 ? args[0] : "";
  if (all || (arg_count > 0 && equals_lower_text(first, "health"))) {
    qa_gameplay_authority_set_health(game->host->combat, entity->actor,
        arg_count == 2 ? js_parse_int(args[1]) : entity->max_health);
    if (!all) { free(requested); return; }
  }
  if (all || strcmp(requested, "weapons") == 0) {
    bool granted = hooks->grant_selected_arsenal && hooks->grant_selected_arsenal(hooks->ctx, entity->actor.id, "weapons");
    if (!granted)
      for (size_t i = 0; i < catalog_count; i++)
        if (catalog[i].weapon && !(catalog[i].console_give && strcmp(catalog[i].console_give, "inventory-only") == 0))
          write_item(context, &catalog[i], qa_shared_inventory_table_count(game->host->inventory, entity->actor.id, catalog[i].id) + 1);
    if (!all) { free(requested); return; }
  }
  if (all || strcmp(requested, "ammo") == 0) {
    bool granted = hooks->grant_selected_arsenal && hooks->grant_selected_arsenal(hooks->ctx, entity->actor.id, "ammo");
    if (!granted) {
      if (all && rerelease) pickup_classname(context, "item_pack");
      for (size_t i = 0; i < catalog_count; i++) {
        if (strcmp(catalog[i].kind, "ammo") != 0) continue;
        const qa_inventory_entry *entry = find_entry(context, catalog[i].id);
        write_item(context, &catalog[i], qa_js_min((entry ? entry->count : 0) + 1000, entry ? entry->capacity : catalog[i].capacity));
      }
    }
    if (!all) { free(requested); return; }
  }
  if (all || strcmp(requested, "armor") == 0) {
    qa_regular_armor_state regular = {0};
    regular.kind = QA_REGULAR_ARMOR_Q2;
    regular.item = "q2:item_armor_body";
    regular.points = 200;
    regular.normal_protection = 0.8;
    regular.energy_protection = 0.6;
    qa_gameplay_authority_set_regular_armor(game->host->combat, entity->actor, regular);
    if (!all) { free(requested); return; }
  }
  if (all || (!rerelease && strcmp(requested, "power shield") == 0)) {
    pickup_classname(context, "item_power_shield");
    if (!all) { free(requested); return; }
  }
  if (all) {
    for (size_t i = 0; i < catalog_count; i++) {
      const qa_q2_inventory_item *item = &catalog[i];
      if (item->weapon || strcmp(item->kind, "ammo") == 0 || strcmp(item->kind, "armor") == 0 ||
          strcmp(item->kind, "shard") == 0 || (item->console_give && strcmp(item->console_give, "inventory-only") == 0)) continue;
      if (rerelease && item->console_give && (strcmp(item->console_give, "forbidden") == 0 || strcmp(item->console_give, "individual-only") == 0)) continue;
      if (rerelease && strcmp(item->kind, "health") == 0) continue;
      if (rerelease && strcmp(item->kind, "maximum-health") == 0 && strcmp(item->classname, "item_adrenaline") != 0) continue;
      write_item(context, item, rerelease && strcmp(item->kind, "key") == 0 ? 8 : 1);
    }
    if (rerelease) { entity->power_cubes = 0xff; check_power_armor_after_give(context); }
    free(requested);
    return;
  }
  /* TS requested/first are the joined and first args lowercased. */
  char *first_lower = qa_js_to_lower_case(first);
  if (!first_lower) qa_range_error("Out of memory");
  const qa_q2_inventory_item *item = NULL;
  for (size_t i = 0; i < catalog_count; i++) if (equals_lower_text(catalog[i].name, requested)) { item = &catalog[i]; break; }
  if (!item) for (size_t i = 0; i < catalog_count; i++) if (equals_lower_text(catalog[i].name, first_lower)) { item = &catalog[i]; break; }
  if (!item && rerelease)
    for (size_t i = 0; i < catalog_count; i++)
      if (equals_lower_text(catalog[i].classname, first_lower) || equals_lower_text(catalog[i].id, first_lower)) { item = &catalog[i]; break; }
  if (!item) {
    bool given = hooks->give_selected_item && hooks->give_selected_item(hooks->ctx, entity->actor.id, args, arg_count);
    free(first_lower); free(requested);
    if (!given) print_to(context, "unknown item\n");
    return;
  }
  if (rerelease && item->console_give && strcmp(item->console_give, "forbidden") == 0) {
    print_to(context, "Item is not giveable.\n");
    free(first_lower); free(requested);
    return;
  }
  if ((item->weapon || strcmp(item->kind, "ammo") == 0) && !qa_q2_item_module_maps_supply(context->items, item->id)) {
    const char *selected_args[2] = {item->id, arg_count == 2 ? args[1] : NULL};
    size_t selected_count = strcmp(item->kind, "ammo") == 0 && arg_count == 2 ? 2 : 1;
    if (hooks->give_selected_item && hooks->give_selected_item(hooks->ctx, entity->actor.id, selected_args, selected_count)) {
      free(first_lower); free(requested);
      return;
    }
  }
  if (item->console_give && strcmp(item->console_give, "inventory-only") == 0) {
    if (rerelease) write_item(context, item, 1);
    else print_to(context, "non-pickup item\n");
  } else if (strcmp(item->kind, "ammo") == 0) {
    if (arg_count == 2) {
      double amount = js_parse_int(args[1]);
      qa_q2_item_module_give_ammo_count(context->items, entity->actor, game, item->id, &amount);
    } else qa_q2_item_module_give_ammo_count(context->items, entity->actor, game, item->id, NULL);
  } else pickup_classname(context, item->classname);
  free(first_lower);
  free(requested);
}

static void check_power_armor_after_give(qa_q2_player_context *context) {
  qa_q2_entity *entity = context->entity;
  qa_q2_game_services *game = context->game;
  /* TS splits userinfo on "\\" and scans odd fields for autoshield. */
  double automatic = -1;
  const char *cursor = context->state->userinfo;
  size_t field = 0;
  char token[512];
  const char *pending_value = NULL;
  for (;;) {
    const char *sep = strchr(cursor, '\\');
    size_t length = sep ? (size_t)(sep - cursor) : strlen(cursor);
    if (length >= sizeof token) length = sizeof token - 1;
    memcpy(token, cursor, length);
    token[length] = '\0';
    if (field % 2 == 1 && strcmp(token, "autoshield") == 0) pending_value = sep ? sep + 1 : NULL;
    else if (pending_value && field % 2 == 0) { automatic = js_parse_int(token); pending_value = NULL; }
    if (!sep) break;
    cursor = sep + 1;
    field++;
  }
  double cells = qa_shared_inventory_table_count(game->host->inventory, entity->actor.id, "q2:ammo_cells");
  bool enough = cells != 0 && (automatic < 0 || ((qa_js_to_int32(entity->flags) & 0x40000000) != 0 && cells > automatic));
  qa_combat_state_result combat = qa_gameplay_authority_read(game->host->combat, entity->actor.id);
  bool active = combat.has_value && strcmp(combat.value.armor.powered.kind, "none") != 0;
  const char *shield = qa_shared_inventory_table_count(game->host->inventory, entity->actor.id, "q2:item_power_shield") != 0
      ? "q2:item_power_shield" : "q2:item_power_screen";
  if ((active && !enough) || (!active && automatic != -1 && enough))
    (void)qa_q2_item_module_use(context->items, entity->actor, shield, game, 30);
}

