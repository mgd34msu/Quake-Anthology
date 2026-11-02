/* Port of src/content/q3/team-arena/commands.ts. GPL-2.0-or-later. */
#include "qa/content/q3/team_arena/commands.h"

#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q3/base/game/format.h"
#include "qa/content/q3/base/game/numeric.h"
#include "qa/content/q3/base/shared/entity_shared.h"
#include "qa/content/q3/team_arena/session.h"
#include "qa/core/cvars/info.h"
#include "qa/core/math.h"
#include "qa/foundation/support.h"

enum { EF_VOTED = 0x4000, EF_TEAMVOTED = 0x80000, MAX_VOTE_COUNT = 3,
  MAX_STRING_CHARS = 1024, MAX_SAY_TEXT = 150, MOD_GAUNTLET = 2, MOD_SUICIDE = 20 };
static const char *const orders[] = {"hold your position", "hold this position", "come here", "cover me",
    "guard location", "search and destroy", "report"};
static const char *const game_names[] = {"Free For All", "Tournament", "Single Player", "Team Deathmatch",
    "Capture the Flag", "One Flag CTF", "Overload", "Harvester"};
static double s32(double v) { return (int32_t)v; }
static double imul(double a, double b) { return (int32_t)((int32_t)a * (int32_t)b); }
static double slot_get(qa_q3_game_player_slots_mirror *s, double i) { return qa_q3_game_slots_get_mirror(s, (size_t)i); }
static void slot_set(qa_q3_game_player_slots_mirror *s, double i, double v) { qa_q3_game_slots_set_mirror(s, (size_t)i, v); }

/* TS bounded(): C strings already end at first NUL and hold bytes. */
static char *bounded_dup(const char *text, size_t capacity) {
  size_t n = strlen(text ? text : "");
  if (n > capacity - 1) n = capacity - 1;
  char *out = malloc(n + 1);
  if (!out) qa_range_error("out of memory");
  memcpy(out, text ? text : "", n);
  out[n] = '\0';
  return out;
}
static void lower_inplace(char *text) {
  for (char *p = text; *p; p++) if (*p >= 'A' && *p <= 'Z') *p = (char)(*p + 32);
}
static char *lower_dup(const char *text) { char *out = bounded_dup(text, strlen(text) + 1); lower_inplace(out); return out; }

/* TS Arguments: bounded copies of argv. */
typedef struct arguments { char **values; size_t length; } arguments;
static void arguments_init(arguments *args, const char *const *argv, size_t argc) {
  if (argc > 1024) qa_range_error("Command exceeds MAX_STRING_TOKENS");
  args->values = calloc(argc ? argc : 1, sizeof(char *));
  if (!args->values) qa_range_error("out of memory");
  args->length = argc;
  for (size_t i = 0; i < argc; i++) args->values[i] = bounded_dup(argv[i], MAX_STRING_CHARS);
}
static void arguments_destroy(arguments *args) {
  for (size_t i = 0; i < args->length; i++) free(args->values[i]);
  free(args->values);
}
/* Malloc'd bounded value ("" when absent). */
static char *arguments_at(const arguments *args, size_t index, size_t capacity) {
  return index >= args->length ? bounded_dup("", 1) : bounded_dup(args->values[index], capacity);
}
static char *arguments_concat(const arguments *args, size_t start) {
  char *result = bounded_dup("", 1);
  size_t length = 0;
  for (size_t index = start; index < args->length; index++) {
    char *value = arguments_at(args, index, MAX_STRING_CHARS);
    size_t value_length = strlen(value);
    if (length + value_length >= MAX_STRING_CHARS - 1) { free(value); break; }
    size_t extra = index != args->length - 1 ? 1 : 0;
    char *grown = realloc(result, length + value_length + extra + 1);
    if (!grown) { free(value); free(result); qa_range_error("out of memory"); }
    result = grown;
    memcpy(result + length, value, value_length);
    length += value_length;
    if (extra) result[length++] = ' ';
    result[length] = '\0';
    free(value);
  }
  return result;
}
char *qa_q3_ta_concat_command_args(const char *const *argv, size_t argc, double start) {
  if (start != trunc(start) || start < 0)
    qa_range_error("ConcatArgs start must be a nonnegative integer");
  arguments args;
  arguments_init(&args, argv, argc);
  char *out = arguments_concat(&args, (size_t)start);
  arguments_destroy(&args);
  return out;
}
static char *sanitize(const char *text) {
  char *input = bounded_dup(text, MAX_STRING_CHARS);
  size_t length = strlen(input);
  char *result = malloc(length + 1);
  if (!result) qa_range_error("out of memory");
  size_t out = 0;
  for (size_t i = 0; i < length; i++) {
    unsigned code = (unsigned char)input[i];
    if (code == 27) {
      if (i + 1 == length) { free(input); free(result); qa_range_error("SanitizeString ESC pair crosses the terminating NUL"); }
      i++;
    } else if (code >= 32 && code < 128) result[out++] = (char)tolower((int)code);
  }
  result[out] = '\0';
  free(input);
  return result;
}
static char *clean_name(const char *text) {
  char *input = bounded_dup(text, 36);
  size_t length = strlen(input);
  char *result = malloc(length + 1);
  if (!result) qa_range_error("out of memory");
  size_t out = 0;
  for (size_t i = 0; i < length; i++) {
    unsigned code = (unsigned char)input[i];
    if (input[i] == '^' && i + 1 < length && input[i + 1] != '^') i++;
    else if (code >= 32 && code <= 126) result[out++] = input[i];
  }
  result[out] = '\0';
  lower_inplace(result);
  free(input);
  return result;
}
static qa_q3_game_client *client_of(qa_q3_game_entity *entity) {
  if (entity->client == NULL) qa_range_error("Player command requires a client entity");
  return entity->client;
}
static void print_to(qa_q3_ta_game_command_runtime *r, qa_q3_game_entity *entity, const char *text) {
  qa_q3_game_format_arg a = qa_q3_game_format_string(text);
  char *t = qa_q3_game_format("print \"%s\"", &a, 1, 32000);
  r->host.imports.send_server_command(r->host.imports.ctx, entity->slot, t);
  free(t);
}
static void print_fmt_to(qa_q3_ta_game_command_runtime *r, qa_q3_game_entity *entity, const char *f,
    const qa_q3_game_format_arg *a, size_t n) {
  char *t = qa_q3_game_format(f, a, n, 32000);
  print_to(r, entity, t);
  free(t);
}
static qa_q3_base_product base_product(qa_q3_game_product p) {
  return p == QA_Q3_GAME_PRODUCT_MISSIONPACK ? QA_Q3_BASE_PRODUCT_MISSIONPACK : QA_Q3_BASE_PRODUCT_BASEQ3;
}

void qa_q3_ta_game_command_runtime_init(qa_q3_ta_game_command_runtime *r, qa_q3_ta_game_command_host host) {
  if (host.team_scores->length != 4)
    qa_range_error("Commands require the shared four-slot team score table");
  r->host = host;
}

void qa_q3_ta_game_command_runtime_scoreboard(qa_q3_ta_game_command_runtime *r, qa_q3_game_entity *entity) {
  qa_q3_ta_match_state *state = r->host.state;
  qa_q3_entity_pool *pool = r->host.pool;
  char text[1025] = "";
  size_t length = 0;
  double count;
  for (count = 0; count < state->num_connected_clients; count++) {
    if (count < 0 || count >= QA_Q3_MAX_CLIENTS)
      qa_range_error("Scoreboard sorted-client prefix exceeds its backing storage");
    double slot = state->sorted_clients[(size_t)count];
    qa_q3_game_client *client = qa_q3_entity_pool_client_at(pool, slot);
    qa_q3_game_player_slots_mirror *persistent = &client->ps.persistant;
    double ping = client->pers.connected == QA_Q3_CONN_CONNECTING ? -1 : fmin(0, 999); /* TS ps.ping */
    double accuracy = client->accuracy_shots == 0 ? 0
        : s32(trunc(imul(client->accuracy_hits, 100) / client->accuracy_shots));
    double perfect = (slot_get(persistent, QA_Q3_BASE_PERS_RANK) == 0 &&
        slot_get(persistent, QA_Q3_BASE_PERS_KILLED) == 0) ? 1 : 0;
    qa_q3_game_format_arg a[] = {
      qa_q3_game_format_number(slot), qa_q3_game_format_number(slot_get(persistent, QA_Q3_BASE_PERS_SCORE)),
      qa_q3_game_format_number(ping),
      qa_q3_game_format_number(trunc(s32(state->time - client->pers.enter_time) / 60000)),
      qa_q3_game_format_number(0), qa_q3_game_format_number(qa_q3_entity_pool_at(pool, slot)->s.powerups),
      qa_q3_game_format_number(accuracy),
      qa_q3_game_format_number(slot_get(persistent, QA_Q3_BASE_PERS_IMPRESSIVE_COUNT)),
      qa_q3_game_format_number(slot_get(persistent, QA_Q3_BASE_PERS_EXCELLENT_COUNT)),
      qa_q3_game_format_number(slot_get(persistent, QA_Q3_BASE_PERS_GAUNTLET_FRAG_COUNT)),
      qa_q3_game_format_number(slot_get(persistent, QA_Q3_BASE_PERS_DEFEND_COUNT)),
      qa_q3_game_format_number(slot_get(persistent, QA_Q3_BASE_PERS_ASSIST_COUNT)),
      qa_q3_game_format_number(perfect),
      qa_q3_game_format_number(slot_get(persistent, QA_Q3_BASE_PERS_CAPTURES))};
    char *entry = qa_q3_game_format(" %i %i %i %i %i %i %i %i %i %i %i %i %i %i", a, 14, 1024);
    size_t entry_length = strlen(entry);
    if (length + entry_length > 1024) { free(entry); break; }
    memcpy(text + length, entry, entry_length + 1);
    length += entry_length;
    free(entry);
  }
  qa_q3_game_format_arg a[] = {qa_q3_game_format_number(count),
      qa_q3_game_format_number(slot_get(r->host.team_scores, QA_Q3_BASE_TEAM_RED)),
      qa_q3_game_format_number(slot_get(r->host.team_scores, QA_Q3_BASE_TEAM_BLUE)),
      qa_q3_game_format_string(text)};
  char *t = qa_q3_game_format("scores %i %i %i%s", a, 4, 32000);
  r->host.imports.send_server_command(r->host.imports.ctx, entity->slot, t);
  free(t);
}

bool qa_q3_ta_game_command_runtime_cheats_ok(qa_q3_ta_game_command_runtime *r, qa_q3_game_entity *entity) {
  if (!r->host.settings->cheats) { print_to(r, entity, "Cheats are not enabled on this server.\n"); return false; }
  if (qa_q3_game_entity_health(entity) <= 0) { print_to(r, entity, "You must be alive to use this command.\n"); return false; }
  return true;
}

bool qa_q3_ta_game_command_runtime_client_number_from_string(qa_q3_ta_game_command_runtime *r,
    qa_q3_game_entity *entity, const char *text, double *out) {
  char *input = bounded_dup(text, MAX_STRING_CHARS);
  unsigned first = (unsigned char)input[0];
  if (first >= '0' && first <= '9') {
    double slot = qa_game_atoi(input);
    free(input);
    if (slot < 0 || slot >= r->host.pool->max_clients) {
      qa_q3_game_format_arg a = qa_q3_game_format_number(slot);
      print_fmt_to(r, entity, "Bad client slot: %i\n", &a, 1);
      return false;
    }
    if (qa_q3_entity_pool_client_at(r->host.pool, slot)->pers.connected != QA_Q3_CONN_CONNECTED) {
      qa_q3_game_format_arg a = qa_q3_game_format_number(slot);
      print_fmt_to(r, entity, "Client %i is not active\n", &a, 1);
      return false;
    }
    *out = slot;
    return true;
  }
  char *name = sanitize(input);
  for (double slot = 0; slot < r->host.pool->max_clients; slot++) {
    qa_q3_game_client *client = qa_q3_entity_pool_client_at(r->host.pool, slot);
    if (client->pers.connected == QA_Q3_CONN_CONNECTED) {
      char *candidate = sanitize(client->pers.netname ? client->pers.netname : "");
      bool match = strcmp(candidate, name) == 0;
      free(candidate);
      if (match) { free(name); free(input); *out = slot; return true; }
    }
  }
  qa_q3_game_format_arg a = qa_q3_game_format_string(input);
  print_fmt_to(r, entity, "User %s is not on the server\n", &a, 1);
  free(name);
  free(input);
  return false;
}

/* TS give's isDisabled closure over the disable_<classname> cvar. */
typedef struct give_disabled { qa_q3_ta_game_command_runtime *runtime; const char *cvar_name; } give_disabled;
static bool give_is_disabled(void *ctx) {
  give_disabled *check = ctx;
  return qa_game_atoi(check->runtime->host.imports.get_cvar(
             check->runtime->host.imports.ctx, check->cvar_name)) != 0;
}

static void give(qa_q3_ta_game_command_runtime *r, qa_q3_game_entity *entity, arguments *args) {
  if (!qa_q3_ta_game_command_runtime_cheats_ok(r, entity)) return;
  char *name = arguments_concat(args, 1);
  char *key = lower_dup(name);
  bool all = strcmp(key, "all") == 0;
  qa_q3_game_client *client = client_of(entity);
  const qa_q3_base_stat_schema *schema = qa_q3_base_stat_schema_for(base_product(client->ps.product));
  qa_actor_id actor = qa_q3_game_entity_actor(entity).id;
  bool done = false;
  if (all || strcmp(key, "health") == 0) {
    qa_q3_game_entity_set_health(entity, slot_get(&client->ps.stats, schema->max_health));
    if (!all) done = true;
  }
  if (!done && (all || strcmp(key, "weapons") == 0)) {
    if (!(r->host.grant_selected_arsenal && r->host.grant_selected_arsenal(r->host.ctx, actor, "weapons")))
      slot_set(&client->ps.stats, schema->weapons,
          (double)(int32_t)((1 << (int)qa_q3_base_weapon_count(base_product(client->ps.product))) - 1 -
              (1 << QA_Q3_WP_GRAPPLING_HOOK) - (1 << QA_Q3_WP_NONE)));
    if (!all) done = true;
  }
  if (!done && (all || strcmp(key, "ammo") == 0)) {
    if (!(r->host.grant_selected_arsenal && r->host.grant_selected_arsenal(r->host.ctx, actor, "ammo")))
      for (int index = 0; index < 16; index++) slot_set(&client->ps.ammo, index, 999);
    if (!all) done = true;
  }
  if (!done && (all || strcmp(key, "armor") == 0)) {
    slot_set(&client->ps.stats, schema->armor, 200);
    if (!all) done = true;
  }
  if (!done) {
    bool has_award = true;
    double award = 0;
    if (strcmp(key, "excellent") == 0) award = QA_Q3_BASE_PERS_EXCELLENT_COUNT;
    else if (strcmp(key, "impressive") == 0) award = QA_Q3_BASE_PERS_IMPRESSIVE_COUNT;
    else if (strcmp(key, "gauntletaward") == 0) award = QA_Q3_BASE_PERS_GAUNTLET_FRAG_COUNT;
    else if (strcmp(key, "defend") == 0) award = QA_Q3_BASE_PERS_DEFEND_COUNT;
    else if (strcmp(key, "assist") == 0) award = QA_Q3_BASE_PERS_ASSIST_COUNT;
    else has_award = false;
    if (has_award) {
      slot_set(&client->ps.persistant, award, slot_get(&client->ps.persistant, award) + 1);
      done = true;
    }
  }
  if (!done && !all) {
    const qa_q3_base_item_definition *item =
        qa_q3_base_find_item(base_product(client->ps.product), name);
    if (item == NULL) {
      if (r->host.give_selected_item) {
        size_t count = args->length > 1 ? args->length - 1 : 0;
        (void)r->host.give_selected_item(r->host.ctx, actor,
            (const char *const *)(args->values + (args->length ? 1 : 0)), count);
      }
    } else {
      qa_q3_game_entity *temporary = qa_q3_entity_pool_spawn(r->host.pool);
      temporary->s.origin = entity->r.current_origin;
      qa_q3_game_entity_set_classname(temporary, item->class_name);
      char cvar_name[300];
      (void)snprintf(cvar_name, sizeof cvar_name, "disable_%s", item->class_name ? item->class_name : "");
      /* TS calls spawnItem directly with host.items; the mirror row is
       * index-aligned with the shared item list findItem searched. */
      size_t base_count = 0;
      const qa_q3_base_item_definition *base_list =
          qa_q3_base_item_list(base_product(client->ps.product), &base_count);
      size_t item_index = (size_t)(item - base_list);
      const qa_q3_game_item_definition_mirror *mirror_item =
          r->host.items.registry.items != NULL && item_index < r->host.items.registry.count
              ? &r->host.items.registry.items[item_index] : NULL;
      qa_q3_spawn_variables give_vars;
      qa_q3_spawn_variables_init(&give_vars, NULL, 0);
      give_disabled give_check = {r, cvar_name};
      qa_q3_spawn_item(temporary, mirror_item, &give_vars, give_is_disabled, &give_check,
                       r->host.items);
      qa_q3_spawn_variables_destroy(&give_vars);
      qa_q3_finish_spawning_item(temporary, r->host.items);
      qa_touch_contact contact;
      memset(&contact, 0, sizeof contact);
      contact.self = qa_q3_game_entity_actor(temporary);
      contact.other = actor;
      qa_q3_damage_participant other = qa_q3_participant_entity(entity);
      qa_q3_touch_item(temporary, other, contact, r->host.items);
      if (qa_q3_game_entity_inuse(temporary)) qa_q3_entity_pool_free(r->host.pool, temporary);
    }
  }
  free(name);
  free(key);
}

static void toggle(qa_q3_ta_game_command_runtime *r, qa_q3_game_entity *entity, const char *command) {
  if (!qa_q3_ta_game_command_runtime_cheats_ok(r, entity)) return;
  qa_q3_game_client *client = client_of(entity);
  bool enabled;
  if (strcmp(command, "noclip") == 0) { client->noclip = !client->noclip; enabled = client->noclip; }
  else {
    double flag = strcmp(command, "god") == 0 ? QA_Q3_GAME_FLAG_GODMODE : QA_Q3_GAME_FLAG_NOTARGET;
    entity->flags = (double)((int32_t)entity->flags ^ (int32_t)flag);
    enabled = ((int32_t)entity->flags & (int32_t)flag) != 0;
  }
  char buffer[64];
  (void)snprintf(buffer, sizeof buffer, "%s %s\n",
      strcmp(command, "god") == 0 ? "godmode" : command, enabled ? "ON" : "OFF");
  print_to(r, entity, buffer);
}

static void player_die_call(qa_q3_ta_game_command_runtime *r, qa_q3_game_entity *entity) {
  qa_q3_damage_participant self = qa_q3_participant_entity(entity);
  r->host.player_die(r->host.ctx, entity, self, self, 100000, MOD_SUICIDE);
}
static void kill_self(qa_q3_ta_game_command_runtime *r, qa_q3_game_entity *entity) {
  qa_q3_game_client *client = client_of(entity);
  if (client->sess.session_team == QA_Q3_BASE_TEAM_SPECTATOR || qa_q3_game_entity_health(entity) <= 0) return;
  entity->flags = (double)((int32_t)entity->flags & ~QA_Q3_GAME_FLAG_GODMODE);
  qa_q3_game_entity_set_health(entity, -999);
  slot_set(&client->ps.stats, qa_q3_base_stat_schema_for(base_product(client->ps.product))->health, -999);
  player_die_call(r, entity);
}
static void level_shot(qa_q3_ta_game_command_runtime *r, qa_q3_game_entity *entity) {
  if (!qa_q3_ta_game_command_runtime_cheats_ok(r, entity)) return;
  if (r->host.settings->game_type != QA_Q3_BASE_GT_FFA) {
    print_to(r, entity, "Must be in g_gametype 0 for levelshot\n"); return; }
  qa_q3_ta_match_runtime_begin_intermission(r->host.match);
  r->host.imports.send_server_command(r->host.imports.ctx, entity->slot, "clientLevelShot");
}
static void imports_print(void *ctx, const char *text) {
  qa_q3_ta_game_command_runtime *r = ctx;
  r->host.imports.print(r->host.imports.ctx, text);
}
static void team_task(qa_q3_ta_game_command_runtime *r, qa_q3_game_entity *entity, arguments *args) {
  if (args->length != 2) return;
  char *userinfo = bounded_dup(r->host.imports.get_userinfo(r->host.imports.ctx, entity->slot), MAX_STRING_CHARS);
  char *arg1 = arguments_at(args, 1, MAX_STRING_CHARS);
  qa_q3_game_format_arg a = qa_q3_game_format_number(qa_game_atoi(arg1));
  free(arg1);
  char *value = qa_q3_game_format("%d", &a, 1, 32000);
  qa_info_options options = {QA_COMMAND_DIALECT_Q3, 1024, QA_CVAR_INFO_CLIENT_USERINFO, false, imports_print, r};
  char *updated = qa_set_info_value(userinfo, "teamtask", value, &options);
  r->host.imports.set_userinfo(r->host.imports.ctx, entity->slot, updated);
  free(updated);
  free(value);
  free(userinfo);
  qa_q3_ta_client_admission_runtime_userinfo_changed(r->host.admission, entity->slot);
}

void qa_q3_ta_game_command_runtime_broadcast_team_change(qa_q3_ta_game_command_runtime *r,
    double client_num, double old_team) {
  qa_q3_game_client *client = qa_q3_entity_pool_client_at(r->host.pool, client_num);
  double team = client->sess.session_team;
  const char *announcement = team == QA_Q3_BASE_TEAM_RED ? "joined the red team."
      : team == QA_Q3_BASE_TEAM_BLUE ? "joined the blue team."
      : (team == QA_Q3_BASE_TEAM_SPECTATOR && old_team != QA_Q3_BASE_TEAM_SPECTATOR) ? "joined the spectators."
      : team == QA_Q3_BASE_TEAM_FREE ? "joined the battle." : NULL;
  if (announcement != NULL) {
    qa_q3_game_format_arg a[] = {qa_q3_game_format_string(client->pers.netname),
        qa_q3_game_format_string(announcement)};
    char *t = qa_q3_game_format("cp \"%s^7 %s\n\"", a, 2, 32000);
    r->host.imports.send_server_command(r->host.imports.ctx, -1, t);
    free(t);
  }
}

void qa_q3_ta_game_command_runtime_set_team(qa_q3_ta_game_command_runtime *r,
    qa_q3_game_entity *entity, const char *request) {
  qa_q3_game_client *client = client_of(entity);
  char *bounded = bounded_dup(request, MAX_STRING_CHARS);
  char *key = lower_dup(bounded);
  free(bounded);
  const qa_q3_ta_command_settings *settings = r->host.settings;
  qa_q3_ta_match_state *state = r->host.state;
  double team, spectator_state = QA_Q3_SPEC_NOT, spectator_client = 0;
  if (strcmp(key, "scoreboard") == 0 || strcmp(key, "score") == 0) {
    team = QA_Q3_BASE_TEAM_SPECTATOR; spectator_state = QA_Q3_SPEC_SCOREBOARD;
  } else if (strcmp(key, "follow1") == 0 || strcmp(key, "follow2") == 0) {
    team = QA_Q3_BASE_TEAM_SPECTATOR; spectator_state = QA_Q3_SPEC_FOLLOW;
    spectator_client = strcmp(key, "follow1") == 0 ? -1 : -2;
  } else if (strcmp(key, "spectator") == 0 || strcmp(key, "s") == 0) {
    team = QA_Q3_BASE_TEAM_SPECTATOR; spectator_state = QA_Q3_SPEC_FREE;
  } else if (settings->game_type >= QA_Q3_BASE_GT_TEAM) {
    qa_q3_ta_session_world_state counts;
    memset(&counts, 0, sizeof counts);
    counts.clients = r->host.pool->clients;
    counts.client_count = QA_Q3_MAX_CLIENTS;
    counts.max_clients = r->host.pool->max_clients;
    counts.team_scores = r->host.team_scores;
    team = (strcmp(key, "red") == 0 || strcmp(key, "r") == 0) ? QA_Q3_BASE_TEAM_RED
        : (strcmp(key, "blue") == 0 || strcmp(key, "b") == 0) ? QA_Q3_BASE_TEAM_BLUE
        : qa_q3_ta_pick_team(&counts, entity->slot);
    if (settings->team_force_balance) {
      double red = qa_q3_ta_team_count(&counts, client->ps.client_num, QA_Q3_BASE_TEAM_RED);
      double blue = qa_q3_ta_team_count(&counts, client->ps.client_num, QA_Q3_BASE_TEAM_BLUE);
      if (team == QA_Q3_BASE_TEAM_RED && red - blue > 1) {
        r->host.imports.send_server_command(r->host.imports.ctx, client->ps.client_num,
            "cp \"Red team has too many players.\n\"");
        free(key); return;
      }
      if (team == QA_Q3_BASE_TEAM_BLUE && blue - red > 1) {
        r->host.imports.send_server_command(r->host.imports.ctx, client->ps.client_num,
            "cp \"Blue team has too many players.\n\"");
        free(key); return;
      }
    }
  } else team = QA_Q3_BASE_TEAM_FREE;
  free(key);
  if (settings->game_type == QA_Q3_BASE_GT_TOURNAMENT && state->num_non_spectator_clients >= 2)
    team = QA_Q3_BASE_TEAM_SPECTATOR;
  else if (settings->max_game_clients > 0 && state->num_non_spectator_clients >= settings->max_game_clients)
    team = QA_Q3_BASE_TEAM_SPECTATOR;
  double old_team = client->sess.session_team;
  if (team == old_team && team != QA_Q3_BASE_TEAM_SPECTATOR) return;
  const qa_q3_base_stat_schema *schema = qa_q3_base_stat_schema_for(base_product(client->ps.product));
  if (slot_get(&client->ps.stats, schema->health) <= 0)
    (void)qa_q3_ta_client_spawn_runtime_copy_to_body_queue(r->host.spawn, entity);
  client->pers.team_state.state = QA_Q3_TEAM_STATE_BEGIN;
  if (old_team != QA_Q3_BASE_TEAM_SPECTATOR) {
    entity->flags = (double)((int32_t)entity->flags & ~QA_Q3_GAME_FLAG_GODMODE);
    qa_q3_game_entity_set_health(entity, 0);
    slot_set(&client->ps.stats, schema->health, 0);
    player_die_call(r, entity);
  }
  if (team == QA_Q3_BASE_TEAM_SPECTATOR) client->sess.spectator_time = state->time;
  client->sess.session_team = team;
  client->sess.spectator_state = spectator_state;
  client->sess.spectator_client = spectator_client;
  client->sess.team_leader = 0;
  if (team == QA_Q3_BASE_TEAM_RED || team == QA_Q3_BASE_TEAM_BLUE) {
    qa_q3_game_entity *leader = NULL;
    for (double slot = 0; slot < r->host.pool->max_clients; slot++) {
      qa_q3_game_client *candidate = qa_q3_entity_pool_client_at(r->host.pool, slot);
      if (candidate->pers.connected != QA_Q3_CONN_DISCONNECTED &&
          candidate->sess.session_team == team && candidate->sess.team_leader != 0) {
        leader = qa_q3_entity_pool_at(r->host.pool, slot); break; }
    }
    if (leader == NULL || (((int32_t)entity->r.sv_flags & QA_Q3_BASE_SVF_BOT) == 0 &&
        ((int32_t)leader->r.sv_flags & QA_Q3_BASE_SVF_BOT) != 0))
      qa_q3_ta_match_runtime_set_leader(r->host.match, (qa_q3_base_team)team, entity->slot);
  }
  if (old_team == QA_Q3_BASE_TEAM_RED || old_team == QA_Q3_BASE_TEAM_BLUE)
    qa_q3_ta_match_runtime_check_team_leader(r->host.match, (qa_q3_base_team)old_team);
  qa_q3_ta_game_command_runtime_broadcast_team_change(r, entity->slot, old_team);
  qa_q3_ta_client_admission_runtime_userinfo_changed(r->host.admission, entity->slot);
  qa_q3_ta_client_admission_runtime_begin(r->host.admission, entity->slot);
}

void qa_q3_ta_game_command_runtime_stop_following(qa_q3_ta_game_command_runtime *r, qa_q3_game_entity *entity) {
  (void)r;
  qa_q3_game_client *client = client_of(entity);
  slot_set(&client->ps.persistant, QA_Q3_BASE_PERS_TEAM, QA_Q3_BASE_TEAM_SPECTATOR);
  client->sess.session_team = QA_Q3_BASE_TEAM_SPECTATOR;
  client->sess.spectator_state = QA_Q3_SPEC_FREE;
  client->ps.pm_flags = (double)((int32_t)client->ps.pm_flags & ~QA_Q3_FLAG_FOLLOW);
  entity->r.sv_flags = (double)((int32_t)entity->r.sv_flags & ~QA_Q3_BASE_SVF_BOT);
  client->ps.client_num = entity->slot;
}

static void team_command(qa_q3_ta_game_command_runtime *r, qa_q3_game_entity *entity, arguments *args) {
  qa_q3_game_client *client = client_of(entity);
  if (args->length != 2) {
    double team = client->sess.session_team;
    char buffer[32];
    (void)snprintf(buffer, sizeof buffer, "%s team\n",
        team == QA_Q3_BASE_TEAM_RED ? "Red" : team == QA_Q3_BASE_TEAM_BLUE ? "Blue"
        : team == QA_Q3_BASE_TEAM_FREE ? "Free" : "Spectator");
    print_to(r, entity, buffer);
    return;
  }
  if (client->switch_team_time > r->host.state->time) {
    print_to(r, entity, "May not switch teams more than once per 5 seconds.\n"); return; }
  if (r->host.settings->game_type == QA_Q3_BASE_GT_TOURNAMENT && client->sess.session_team == QA_Q3_BASE_TEAM_FREE)
    client->sess.losses = s32(client->sess.losses + 1);
  char *arg1 = arguments_at(args, 1, MAX_STRING_CHARS);
  qa_q3_ta_game_command_runtime_set_team(r, entity, arg1);
  free(arg1);
  client->switch_team_time = s32(r->host.state->time + 5000);
}
static void follow(qa_q3_ta_game_command_runtime *r, qa_q3_game_entity *entity, arguments *args) {
  qa_q3_game_client *client = client_of(entity);
  if (args->length != 2) {
    if (client->sess.spectator_state == QA_Q3_SPEC_FOLLOW)
      qa_q3_ta_game_command_runtime_stop_following(r, entity);
    return;
  }
  char *arg1 = arguments_at(args, 1, MAX_STRING_CHARS);
  double target;
  bool found = qa_q3_ta_game_command_runtime_client_number_from_string(r, entity, arg1, &target);
  free(arg1);
  if (!found || target == entity->slot ||
      qa_q3_entity_pool_client_at(r->host.pool, target)->sess.session_team == QA_Q3_BASE_TEAM_SPECTATOR) return;
  if (r->host.settings->game_type == QA_Q3_BASE_GT_TOURNAMENT && client->sess.session_team == QA_Q3_BASE_TEAM_FREE)
    client->sess.losses = s32(client->sess.losses + 1);
  if (client->sess.session_team != QA_Q3_BASE_TEAM_SPECTATOR)
    qa_q3_ta_game_command_runtime_set_team(r, entity, "spectator");
  client->sess.spectator_state = QA_Q3_SPEC_FOLLOW;
  client->sess.spectator_client = target;
}
void qa_q3_ta_game_command_runtime_follow_cycle(qa_q3_ta_game_command_runtime *r,
    qa_q3_game_entity *entity, double direction) {
  qa_q3_game_client *client = client_of(entity);
  if (r->host.settings->game_type == QA_Q3_BASE_GT_TOURNAMENT && client->sess.session_team == QA_Q3_BASE_TEAM_FREE)
    client->sess.losses = s32(client->sess.losses + 1);
  if (client->sess.spectator_state == QA_Q3_SPEC_NOT)
    qa_q3_ta_game_command_runtime_set_team(r, entity, "spectator");
  double original = client->sess.spectator_client;
  double slot = original;
  for (double visited = 0; visited < r->host.pool->max_clients; visited++) {
    slot += direction;
    if (slot >= r->host.pool->max_clients) slot = 0;
    if (slot < 0) slot = r->host.pool->max_clients - 1;
    qa_q3_game_client *target = qa_q3_entity_pool_client_at(r->host.pool, slot);
    if (target->pers.connected == QA_Q3_CONN_CONNECTED && target->sess.session_team != QA_Q3_BASE_TEAM_SPECTATOR) {
      client->sess.spectator_client = slot;
      client->sess.spectator_state = QA_Q3_SPEC_FOLLOW;
      return;
    }
    if (slot == original) return;
  }
  qa_range_error("Cmd_FollowCycle_f cannot terminate from an automatic-follow sentinel without an active player");
}
static void say_to(qa_q3_ta_game_command_runtime *r, qa_q3_game_entity *entity, qa_q3_game_entity *target,
    qa_q3_ta_say_mode mode, double color, const char *name, const char *text) {
  if (!qa_q3_game_entity_inuse(target) || target->client == NULL ||
      target->client->pers.connected != QA_Q3_CONN_CONNECTED) return;
  if (mode == QA_Q3_TA_SAY_TEAM && !qa_q3_ta_on_same_team(r->host.settings->game_type, entity, target)) return;
  if (r->host.settings->game_type == QA_Q3_BASE_GT_TOURNAMENT &&
      target->client->sess.session_team == QA_Q3_BASE_TEAM_FREE &&
      client_of(entity)->sess.session_team != QA_Q3_BASE_TEAM_FREE) return;
  qa_q3_game_format_arg a[] = {qa_q3_game_format_string(mode == QA_Q3_TA_SAY_TEAM ? "tchat" : "chat"),
      qa_q3_game_format_string(name), qa_q3_game_format_number(94), qa_q3_game_format_number(color),
      qa_q3_game_format_string(text)};
  char *t = qa_q3_game_format("%s \"%s%c%c%s\"", a, 5, 32000);
  r->host.imports.send_server_command(r->host.imports.ctx, target->slot, t);
  free(t);
}
void qa_q3_ta_game_command_runtime_say(qa_q3_ta_game_command_runtime *r, qa_q3_game_entity *entity,
    qa_q3_game_entity *target, qa_q3_ta_say_mode mode, const char *chat_text) {
  qa_q3_game_client *client = client_of(entity);
  double game_type = r->host.settings->game_type;
  if (game_type < QA_Q3_BASE_GT_TEAM && mode == QA_Q3_TA_SAY_TEAM) mode = QA_Q3_TA_SAY_ALL;
  char *name;
  double color;
  if (mode == QA_Q3_TA_SAY_ALL) {
    qa_q3_game_format_arg a[] = {qa_q3_game_format_string(client->pers.netname),
        qa_q3_game_format_string(chat_text)};
    char *t = qa_q3_game_format("say: %s: %s\n", a, 2, 32000);
    r->host.imports.log(r->host.imports.ctx, t);
    free(t);
    qa_q3_game_format_arg b = qa_q3_game_format_string(client->pers.netname);
    name = qa_q3_game_format("%s^7\x19: ", &b, 1, 64);
    color = 50;
  } else if (mode == QA_Q3_TA_SAY_TEAM) {
    qa_q3_game_format_arg a[] = {qa_q3_game_format_string(client->pers.netname),
        qa_q3_game_format_string(chat_text)};
    char *t = qa_q3_game_format("sayteam: %s: %s\n", a, 2, 32000);
    r->host.imports.log(r->host.imports.ctx, t);
    free(t);
    bool is_null;
    char *location = qa_q3_ta_team_runtime_get_location_message(r->host.team, entity, 64, &is_null);
    if (is_null) {
      qa_q3_game_format_arg b = qa_q3_game_format_string(client->pers.netname);
      name = qa_q3_game_format("\x19(%s^7\x19)\x19: ", &b, 1, 64);
    } else {
      qa_q3_game_format_arg b[] = {qa_q3_game_format_string(client->pers.netname),
          qa_q3_game_format_string(location)};
      name = qa_q3_game_format("\x19(%s^7\x19) (%s)\x19: ", b, 2, 64);
    }
    free(location);
    color = 53;
  } else {
    char *location = NULL;
    bool is_null = true;
    if (target != NULL && game_type >= QA_Q3_BASE_GT_TEAM &&
        client_of(target)->sess.session_team == client->sess.session_team)
      location = qa_q3_ta_team_runtime_get_location_message(r->host.team, entity, 64, &is_null);
    if (is_null) {
      qa_q3_game_format_arg b = qa_q3_game_format_string(client->pers.netname);
      name = qa_q3_game_format("\x19[%s^7\x19]\x19: ", &b, 1, 64);
    } else {
      qa_q3_game_format_arg b[] = {qa_q3_game_format_string(client->pers.netname),
          qa_q3_game_format_string(location)};
      name = qa_q3_game_format("\x19[%s^7\x19] (%s)\x19: ", b, 2, 64);
    }
    free(location);
    color = 54;
  }
  char *text = bounded_dup(chat_text, MAX_SAY_TEXT);
  if (target != NULL) {
    say_to(r, entity, target, mode, color, name, text);
  } else {
    if (r->host.settings->dedicated) {
      qa_q3_game_format_arg a[] = {qa_q3_game_format_string(name), qa_q3_game_format_string(text)};
      char *t = qa_q3_game_format("%s%s\n", a, 2, 32000);
      r->host.imports.print(r->host.imports.ctx, t);
      free(t);
    }
    for (double slot = 0; slot < r->host.pool->max_clients; slot++)
      say_to(r, entity, qa_q3_entity_pool_at(r->host.pool, slot), mode, color, name, text);
  }
  free(text);
  free(name);
}
static void say_command(qa_q3_ta_game_command_runtime *r, qa_q3_game_entity *entity,
    arguments *args, qa_q3_ta_say_mode mode, bool arg0) {
  if (args->length < 2 && !arg0) return;
  char *text = arguments_concat(args, arg0 ? 0 : 1);
  qa_q3_ta_game_command_runtime_say(r, entity, NULL, mode, text);
  free(text);
}
static void tell(qa_q3_ta_game_command_runtime *r, qa_q3_game_entity *entity, arguments *args) {
  if (args->length < 2) return;
  char *arg1 = arguments_at(args, 1, MAX_STRING_CHARS);
  double slot = qa_game_atoi(arg1);
  free(arg1);
  if (slot < 0 || slot >= r->host.pool->max_clients) return;
  qa_q3_game_entity *target = qa_q3_entity_pool_at(r->host.pool, slot);
  if (!qa_q3_game_entity_inuse(target) || target->client == NULL) return;
  char *text = arguments_concat(args, 2);
  qa_q3_game_format_arg a[] = {qa_q3_game_format_string(client_of(entity)->pers.netname),
      qa_q3_game_format_string(target->client->pers.netname), qa_q3_game_format_string(text)};
  char *t = qa_q3_game_format("tell: %s to %s: %s\n", a, 3, 32000);
  r->host.imports.log(r->host.imports.ctx, t);
  free(t);
  qa_q3_ta_game_command_runtime_say(r, entity, target, QA_Q3_TA_SAY_TELL, text);
  if (entity != target && ((int32_t)entity->r.sv_flags & QA_Q3_BASE_SVF_BOT) == 0)
    qa_q3_ta_game_command_runtime_say(r, entity, entity, QA_Q3_TA_SAY_TELL, text);
  free(text);
}
static void voice_to(qa_q3_ta_game_command_runtime *r, qa_q3_game_entity *entity, qa_q3_game_entity *target,
    qa_q3_ta_say_mode mode, const char *id, bool voice_only) {
  if (!qa_q3_game_entity_inuse(target) || target->client == NULL) return;
  if (mode == QA_Q3_TA_SAY_TEAM && !qa_q3_ta_on_same_team(r->host.settings->game_type, entity, target)) return;
  if (r->host.settings->game_type == QA_Q3_BASE_GT_TOURNAMENT) return;
  double color = mode == QA_Q3_TA_SAY_TEAM ? 53 : mode == QA_Q3_TA_SAY_TELL ? 54 : 50;
  const char *command = mode == QA_Q3_TA_SAY_TEAM ? "vtchat" : mode == QA_Q3_TA_SAY_TELL ? "vtell" : "vchat";
  qa_q3_game_format_arg a[] = {qa_q3_game_format_string(command), qa_q3_game_format_number(voice_only ? 1 : 0),
      qa_q3_game_format_number(entity->s.number), qa_q3_game_format_number(color), qa_q3_game_format_string(id)};
  char *t = qa_q3_game_format("%s %d %d %d %s", a, 5, 32000);
  r->host.imports.send_server_command(r->host.imports.ctx, target->slot, t);
  free(t);
}
void qa_q3_ta_game_command_runtime_voice(qa_q3_ta_game_command_runtime *r, qa_q3_game_entity *entity,
    qa_q3_game_entity *target, qa_q3_ta_say_mode mode, const char *id, bool voice_only) {
  if (r->host.settings->game_type < QA_Q3_BASE_GT_TEAM && mode == QA_Q3_TA_SAY_TEAM) mode = QA_Q3_TA_SAY_ALL;
  if (target != NULL) { voice_to(r, entity, target, mode, id, voice_only); return; }
  if (r->host.settings->dedicated) {
    qa_q3_game_format_arg a[] = {qa_q3_game_format_string(client_of(entity)->pers.netname),
        qa_q3_game_format_string(id)};
    char *t = qa_q3_game_format("voice: %s %s\n", a, 2, 32000);
    r->host.imports.print(r->host.imports.ctx, t);
    free(t);
  }
  for (double slot = 0; slot < r->host.pool->max_clients; slot++)
    voice_to(r, entity, qa_q3_entity_pool_at(r->host.pool, slot), mode, id, voice_only);
}
static void voice_command(qa_q3_ta_game_command_runtime *r, qa_q3_game_entity *entity,
    arguments *args, qa_q3_ta_say_mode mode, bool voice_only) {
  if (args->length < 2) return;
  char *id = arguments_concat(args, 1);
  qa_q3_ta_game_command_runtime_voice(r, entity, NULL, mode, id, voice_only);
  free(id);
}
static void voice_tell(qa_q3_ta_game_command_runtime *r, qa_q3_game_entity *entity,
    arguments *args, bool voice_only) {
  if (args->length < 2) return;
  char *arg1 = arguments_at(args, 1, MAX_STRING_CHARS);
  double slot = qa_game_atoi(arg1);
  free(arg1);
  if (slot < 0 || slot >= r->host.pool->max_clients) return;
  qa_q3_game_entity *target = qa_q3_entity_pool_at(r->host.pool, slot);
  if (!qa_q3_game_entity_inuse(target) || target->client == NULL) return;
  char *id = arguments_concat(args, 2);
  qa_q3_game_format_arg a[] = {qa_q3_game_format_string(client_of(entity)->pers.netname),
      qa_q3_game_format_string(target->client->pers.netname), qa_q3_game_format_string(id)};
  char *t = qa_q3_game_format("vtell: %s to %s: %s\n", a, 3, 32000);
  r->host.imports.log(r->host.imports.ctx, t);
  free(t);
  qa_q3_ta_game_command_runtime_voice(r, entity, target, QA_Q3_TA_SAY_TELL, id, voice_only);
  if (entity != target && ((int32_t)entity->r.sv_flags & QA_Q3_BASE_SVF_BOT) == 0)
    qa_q3_ta_game_command_runtime_voice(r, entity, entity, QA_Q3_TA_SAY_TELL, id, voice_only);
  free(id);
}
static void voice_pair(qa_q3_ta_game_command_runtime *r, qa_q3_game_entity *entity,
    qa_q3_game_entity *target, const char *id) {
  if (((int32_t)target->r.sv_flags & QA_Q3_BASE_SVF_BOT) == 0)
    qa_q3_ta_game_command_runtime_voice(r, entity, target, QA_Q3_TA_SAY_TELL, id, false);
  if (((int32_t)entity->r.sv_flags & QA_Q3_BASE_SVF_BOT) == 0)
    qa_q3_ta_game_command_runtime_voice(r, entity, entity, QA_Q3_TA_SAY_TELL, id, false);
}
static void voice_taunt(qa_q3_ta_game_command_runtime *r, qa_q3_game_entity *entity) {
  qa_q3_game_client *client = client_of(entity);
  if (entity->enemy != NULL && entity->enemy->client != NULL &&
      entity->enemy->client->last_killed_client == entity->s.number) {
    voice_pair(r, entity, entity->enemy, "death_insult");
    entity->enemy = NULL;
    return;
  }
  if (client->last_killed_client >= 0 && client->last_killed_client != entity->s.number) {
    qa_q3_game_entity *target = qa_q3_entity_pool_at(r->host.pool, client->last_killed_client);
    if (target->client != NULL) {
      voice_pair(r, entity, target, target->client->last_hurt_mod == MOD_GAUNTLET ? "kill_gauntlet" : "kill_insult");
      client->last_killed_client = -1;
      return;
    }
  }
  if (r->host.settings->game_type >= QA_Q3_BASE_GT_TEAM) {
    for (int slot = 0; slot < QA_Q3_MAX_CLIENTS; slot++) {
      qa_q3_game_entity *target = qa_q3_entity_pool_at(r->host.pool, slot);
      if (target->client != NULL && target != entity &&
          target->client->sess.session_team == client->sess.session_team &&
          target->client->reward_time > r->host.state->time) {
        voice_pair(r, entity, target, "praise");
        return;
      }
    }
  }
  qa_q3_ta_game_command_runtime_voice(r, entity, NULL, QA_Q3_TA_SAY_ALL, "taunt", false);
}
static void game_command(qa_q3_ta_game_command_runtime *r, qa_q3_game_entity *entity, arguments *args) {
  char *arg1 = arguments_at(args, 1, MAX_STRING_CHARS);
  char *arg2 = arguments_at(args, 2, MAX_STRING_CHARS);
  double slot = qa_game_atoi(arg1), order = qa_game_atoi(arg2);
  free(arg1); free(arg2);
  if (slot < 0 || slot >= QA_Q3_MAX_CLIENTS || order < 0 || order > 7) return;
  if (order >= 7) qa_range_error("Cmd_GameCommand_f order 7 reads beyond the source order table");
  const char *text = orders[(size_t)order];
  qa_q3_ta_game_command_runtime_say(r, entity, qa_q3_entity_pool_at(r->host.pool, slot), QA_Q3_TA_SAY_TELL, text);
  qa_q3_ta_game_command_runtime_say(r, entity, entity, QA_Q3_TA_SAY_TELL, text);
}

static void set_config_number(qa_q3_ta_game_command_runtime *r, double index, double value) {
  qa_q3_game_format_arg a = qa_q3_game_format_number(value);
  char *t = qa_q3_game_format("%i", &a, 1, 32000);
  r->host.imports.set_configstring(r->host.imports.ctx, index, t);
  free(t);
}
static void set_owned(char **slot, char *value) { free(*slot); *slot = value; }

static void call_vote(qa_q3_ta_game_command_runtime *r, qa_q3_game_entity *entity, arguments *args) {
  qa_q3_game_client *client = client_of(entity);
  qa_q3_ta_match_state *state = r->host.state;
  qa_q3_ta_vote_state *vote = &state->vote;
  if (!r->host.settings->allow_vote) { print_to(r, entity, "Voting not allowed here.\n"); return; }
  if (vote->time != 0) { print_to(r, entity, "A vote is already in progress.\n"); return; }
  if (client->pers.vote_count >= MAX_VOTE_COUNT) { print_to(r, entity, "You have called the maximum number of votes.\n"); return; }
  if (client->sess.session_team == QA_Q3_BASE_TEAM_SPECTATOR) { print_to(r, entity, "Not allowed to call a vote as spectator.\n"); return; }
  char *command = arguments_at(args, 1, MAX_STRING_CHARS);
  char *parameter = arguments_at(args, 2, MAX_STRING_CHARS);
  char *key = lower_dup(command);
  if (strchr(command, ';') || strchr(parameter, ';')) {
    print_to(r, entity, "Invalid vote string.\n"); free(command); free(parameter); free(key); return; }
  static const char *const valid[] = {"map_restart", "nextmap", "map", "g_gametype", "kick",
      "clientkick", "g_dowarmup", "timelimit", "fraglimit"};
  bool is_valid = false;
  for (size_t i = 0; i < sizeof valid / sizeof valid[0]; i++) if (strcmp(key, valid[i]) == 0) is_valid = true;
  if (!is_valid) {
    print_to(r, entity, "Invalid vote string.\n");
    print_to(r, entity, "Vote commands are: map_restart, nextmap, map <mapname>, g_gametype <n>, kick <player>, clientkick <clientnum>, g_doWarmup, timelimit <time>, fraglimit <frags>.\n");
    free(command); free(parameter); free(key); return;
  }
  if (vote->execute_time != 0) {
    vote->execute_time = 0;
    qa_q3_game_format_arg a = qa_q3_game_format_string(vote->string);
    char *t = qa_q3_game_format("%s\n", &a, 1, 32000);
    r->host.imports.append_console_command(r->host.imports.ctx, t);
    free(t);
  }
  if (strcmp(key, "g_gametype") == 0) {
    double type = qa_game_atoi(parameter);
    if (type == QA_Q3_BASE_GT_SINGLE_PLAYER || type < QA_Q3_BASE_GT_FFA || type >= QA_Q3_BASE_GT_MAX_GAME_TYPE) {
      print_to(r, entity, "Invalid gametype.\n"); free(command); free(parameter); free(key); return; }
    if (type < 0 || type >= 8) { free(command); free(parameter); free(key); qa_range_error("Vote gametype has no source display name"); }
    qa_q3_game_format_arg a[] = {qa_q3_game_format_string(command), qa_q3_game_format_number(type)};
    set_owned(&vote->string, qa_q3_game_format("%s %d", a, 2, MAX_STRING_CHARS));
    qa_q3_game_format_arg b[] = {qa_q3_game_format_string(command), qa_q3_game_format_string(game_names[(size_t)type])};
    set_owned(&vote->display_string, qa_q3_game_format("%s %s", b, 2, MAX_STRING_CHARS));
  } else if (strcmp(key, "map") == 0) {
    char *nextmap = bounded_dup(r->host.imports.get_cvar(r->host.imports.ctx, "nextmap"), MAX_STRING_CHARS);
    if (nextmap[0] != '\0') {
      qa_q3_game_format_arg a[] = {qa_q3_game_format_string(command), qa_q3_game_format_string(parameter),
          qa_q3_game_format_string(nextmap)};
      set_owned(&vote->string, qa_q3_game_format("%s %s; set nextmap \"%s\"", a, 3, MAX_STRING_CHARS));
    } else {
      qa_q3_game_format_arg a[] = {qa_q3_game_format_string(command), qa_q3_game_format_string(parameter)};
      set_owned(&vote->string, qa_q3_game_format("%s %s", a, 2, MAX_STRING_CHARS));
    }
    free(nextmap);
    set_owned(&vote->display_string, bounded_dup(vote->string, strlen(vote->string) + 1));
  } else if (strcmp(key, "nextmap") == 0) {
    char *nextmap = bounded_dup(r->host.imports.get_cvar(r->host.imports.ctx, "nextmap"), MAX_STRING_CHARS);
    bool empty = nextmap[0] == '\0';
    free(nextmap);
    if (empty) { print_to(r, entity, "nextmap not set.\n"); free(command); free(parameter); free(key); return; }
    set_owned(&vote->string, bounded_dup("vstr nextmap", 13));
    set_owned(&vote->display_string, bounded_dup(vote->string, 13));
  } else {
    qa_q3_game_format_arg a[] = {qa_q3_game_format_string(command), qa_q3_game_format_string(parameter)};
    set_owned(&vote->string, qa_q3_game_format("%s \"%s\"", a, 2, MAX_STRING_CHARS));
    set_owned(&vote->display_string, bounded_dup(vote->string, strlen(vote->string) + 1));
  }
  free(command); free(parameter); free(key);
  qa_q3_game_format_arg a = qa_q3_game_format_string(client->pers.netname);
  char *t = qa_q3_game_format("print \"%s called a vote.\n\"", &a, 1, 32000);
  r->host.imports.send_server_command(r->host.imports.ctx, -1, t);
  free(t);
  vote->time = state->time; vote->yes = 1; vote->no = 0;
  for (double slot = 0; slot < r->host.pool->max_clients; slot++) {
    qa_q3_game_client *c = qa_q3_entity_pool_client_at(r->host.pool, slot);
    c->ps.e_flags = (double)((int32_t)c->ps.e_flags & ~EF_VOTED);
  }
  client->ps.e_flags = (double)((int32_t)client->ps.e_flags | EF_VOTED);
  set_config_number(r, 8, vote->time);
  r->host.imports.set_configstring(r->host.imports.ctx, 9, vote->display_string);
  set_config_number(r, 10, vote->yes);
  set_config_number(r, 11, vote->no);
}
static void cast_vote(qa_q3_ta_game_command_runtime *r, qa_q3_game_entity *entity, arguments *args) {
  qa_q3_game_client *client = client_of(entity);
  qa_q3_ta_vote_state *vote = &r->host.state->vote;
  if (vote->time == 0) { print_to(r, entity, "No vote in progress.\n"); return; }
  if (((int32_t)client->ps.e_flags & EF_VOTED) != 0) { print_to(r, entity, "Vote already cast.\n"); return; }
  if (client->sess.session_team == QA_Q3_BASE_TEAM_SPECTATOR) { print_to(r, entity, "Not allowed to vote as spectator.\n"); return; }
  print_to(r, entity, "Vote cast.\n");
  client->ps.e_flags = (double)((int32_t)client->ps.e_flags | EF_VOTED);
  char *text = arguments_at(args, 1, 64);
  if (text[0] == 'y' || text[1] == 'Y' || text[1] == '1') {
    vote->yes = s32(vote->yes + 1);
    set_config_number(r, 10, vote->yes);
  } else {
    vote->no = s32(vote->no + 1);
    set_config_number(r, 11, vote->no);
  }
  free(text);
}
static void call_team_vote(qa_q3_ta_game_command_runtime *r, qa_q3_game_entity *entity, arguments *args) {
  qa_q3_game_client *client = client_of(entity);
  double team = client->sess.session_team;
  if (team != QA_Q3_BASE_TEAM_RED && team != QA_Q3_BASE_TEAM_BLUE) return;
  int offset = team == QA_Q3_BASE_TEAM_RED ? 0 : 1;
  qa_q3_ta_team_vote_state *vote = &r->host.state->team_votes[offset];
  if (!r->host.settings->allow_vote) { print_to(r, entity, "Voting not allowed here.\n"); return; }
  if (vote->time != 0) { print_to(r, entity, "A team vote is already in progress.\n"); return; }
  if (client->pers.team_vote_count >= MAX_VOTE_COUNT) {
    print_to(r, entity, "You have called the maximum number of team votes.\n"); return; }
  char *command = arguments_at(args, 1, MAX_STRING_CHARS);
  char *parameter = bounded_dup("", 1);
  size_t parameter_length = 0;
  for (size_t index = 2; index < args->length; index++) {
    if (parameter_length >= MAX_STRING_CHARS) {
      free(command); free(parameter);
      qa_range_error("Team vote arguments overflow the source string buffer"); }
    char *piece = arguments_at(args, index, MAX_STRING_CHARS - parameter_length);
    size_t piece_length = strlen(piece);
    size_t extra = index > 2 ? 1 : 0;
    char *grown = realloc(parameter, parameter_length + extra + piece_length + 1);
    if (!grown) { free(piece); free(command); free(parameter); qa_range_error("out of memory"); }
    parameter = grown;
    if (extra) parameter[parameter_length++] = ' ';
    memcpy(parameter + parameter_length, piece, piece_length + 1);
    parameter_length += piece_length;
    free(piece);
  }
  if (strchr(command, ';') || strchr(parameter, ';')) {
    print_to(r, entity, "Invalid vote string.\n"); free(command); free(parameter); return; }
  char *key = lower_dup(command);
  bool is_leader = strcmp(key, "leader") == 0;
  free(key);
  if (!is_leader) {
    print_to(r, entity, "Invalid vote string.\n");
    print_to(r, entity, "Team vote commands are: leader <player>.\n");
    free(command); free(parameter); return;
  }
  double target = client->ps.client_num;
  if (parameter_length > 0) {
    size_t digits = 0;
    while (digits < 3 && parameter[digits] >= '0' && parameter[digits] <= '9') digits++;
    if (digits >= 3 || parameter[digits] == '\0') {
      target = qa_game_atoi(parameter);
      if (target < 0 || target >= r->host.pool->max_clients) {
        qa_q3_game_format_arg a = qa_q3_game_format_number(target);
        print_fmt_to(r, entity, "Bad client slot: %i\n", &a, 1);
        free(command); free(parameter); return; }
      if (!qa_q3_game_entity_inuse(qa_q3_entity_pool_at(r->host.pool, target))) {
        qa_q3_game_format_arg a = qa_q3_game_format_number(target);
        print_fmt_to(r, entity, "Client %i is not active\n", &a, 1);
        free(command); free(parameter); return; }
    } else {
      char *name = clean_name(parameter);
      for (target = 0; target < r->host.pool->max_clients; target++) {
        qa_q3_game_client *candidate = qa_q3_entity_pool_client_at(r->host.pool, target);
        if (candidate->pers.connected != QA_Q3_CONN_DISCONNECTED && candidate->sess.session_team == team) {
          char *candidate_name = clean_name(candidate->pers.netname ? candidate->pers.netname : "");
          bool match = strcmp(candidate_name, name) == 0;
          free(candidate_name);
          if (match) break;
        }
      }
      free(name);
      if (target >= r->host.pool->max_clients) {
        qa_q3_game_format_arg a = qa_q3_game_format_string(parameter);
        print_fmt_to(r, entity, "%s is not a valid player on your team.\n", &a, 1);
        free(command); free(parameter); return; }
    }
  }
  qa_q3_game_format_arg a[] = {qa_q3_game_format_string(command), qa_q3_game_format_number(target)};
  set_owned(&vote->string, qa_q3_game_format("%s %d", a, 2, MAX_STRING_CHARS));
  free(command);
  free(parameter);
  for (double slot = 0; slot < r->host.pool->max_clients; slot++) {
    qa_q3_game_client *candidate = qa_q3_entity_pool_client_at(r->host.pool, slot);
    if (candidate->pers.connected != QA_Q3_CONN_DISCONNECTED && candidate->sess.session_team == team) {
      qa_q3_game_format_arg b = qa_q3_game_format_string(client->pers.netname);
      char *t = qa_q3_game_format("print \"%s called a team vote.\n\"", &b, 1, 32000);
      r->host.imports.send_server_command(r->host.imports.ctx, slot, t);
      free(t);
    }
  }
  vote->time = r->host.state->time; vote->yes = 1; vote->no = 0;
  for (double slot = 0; slot < r->host.pool->max_clients; slot++) {
    qa_q3_game_client *candidate = qa_q3_entity_pool_client_at(r->host.pool, slot);
    if (candidate->sess.session_team == team)
      candidate->ps.e_flags = (double)((int32_t)candidate->ps.e_flags & ~EF_TEAMVOTED);
  }
  client->ps.e_flags = (double)((int32_t)client->ps.e_flags | EF_TEAMVOTED);
  set_config_number(r, 12 + offset, vote->time);
  r->host.imports.set_configstring(r->host.imports.ctx, 14 + offset, vote->string);
  set_config_number(r, 16 + offset, vote->yes);
  set_config_number(r, 18 + offset, vote->no);
}
static void cast_team_vote(qa_q3_ta_game_command_runtime *r, qa_q3_game_entity *entity, arguments *args) {
  qa_q3_game_client *client = client_of(entity);
  double team = client->sess.session_team;
  if (team != QA_Q3_BASE_TEAM_RED && team != QA_Q3_BASE_TEAM_BLUE) return;
  int offset = team == QA_Q3_BASE_TEAM_RED ? 0 : 1;
  qa_q3_ta_team_vote_state *vote = &r->host.state->team_votes[offset];
  if (vote->time == 0) { print_to(r, entity, "No team vote in progress.\n"); return; }
  if (((int32_t)client->ps.e_flags & EF_TEAMVOTED) != 0) { print_to(r, entity, "Team vote already cast.\n"); return; }
  print_to(r, entity, "Team vote cast.\n");
  client->ps.e_flags = (double)((int32_t)client->ps.e_flags | EF_TEAMVOTED);
  char *text = arguments_at(args, 1, 64);
  if (text[0] == 'y' || text[1] == 'Y' || text[1] == '1') {
    vote->yes = s32(vote->yes + 1);
    set_config_number(r, 16 + offset, vote->yes);
  } else {
    vote->no = s32(vote->no + 1);
    set_config_number(r, 18 + offset, vote->no);
  }
  free(text);
}
static void set_view_position(qa_q3_ta_game_command_runtime *r, qa_q3_game_entity *entity, arguments *args) {
  if (!r->host.settings->cheats) { print_to(r, entity, "Cheats are not enabled on this server.\n"); return; }
  if (args->length != 5) { print_to(r, entity, "usage: setviewpos x y z yaw\n"); return; }
  char *a1 = arguments_at(args, 1, MAX_STRING_CHARS);
  char *a2 = arguments_at(args, 2, MAX_STRING_CHARS);
  char *a3 = arguments_at(args, 3, MAX_STRING_CHARS);
  char *a4 = arguments_at(args, 4, MAX_STRING_CHARS);
  qa_vec3 origin = qa_math_vec3(qa_game_atof(a1), qa_game_atof(a2), qa_game_atof(a3));
  qa_vec3 angles = qa_math_vec3(0, qa_game_atof(a4), 0);
  free(a1); free(a2); free(a3); free(a4);
  qa_q3_teleport_player(r->host.teleport, entity, origin, angles);
}

void qa_q3_ta_game_command_runtime_dispatch(qa_q3_ta_game_command_runtime *r,
    double client_num, const char *const *argv, size_t argc) {
  qa_q3_game_entity *entity = qa_q3_entity_pool_at(r->host.pool, client_num);
  if (entity->client == NULL) return;
  arguments args;
  arguments_init(&args, argv, argc);
  char *command = arguments_at(&args, 0, MAX_STRING_CHARS);
  char *key = lower_dup(command);
  if (strcmp(key, "say") == 0) say_command(r, entity, &args, QA_Q3_TA_SAY_ALL, false);
  else if (strcmp(key, "say_team") == 0) say_command(r, entity, &args, QA_Q3_TA_SAY_TEAM, false);
  else if (strcmp(key, "tell") == 0) tell(r, entity, &args);
  else if (strcmp(key, "vsay") == 0) voice_command(r, entity, &args, QA_Q3_TA_SAY_ALL, false);
  else if (strcmp(key, "vsay_team") == 0) voice_command(r, entity, &args, QA_Q3_TA_SAY_TEAM, false);
  else if (strcmp(key, "vtell") == 0) voice_tell(r, entity, &args, false);
  else if (strcmp(key, "vosay") == 0) voice_command(r, entity, &args, QA_Q3_TA_SAY_ALL, true);
  else if (strcmp(key, "vosay_team") == 0) voice_command(r, entity, &args, QA_Q3_TA_SAY_TEAM, true);
  else if (strcmp(key, "votell") == 0) voice_tell(r, entity, &args, true);
  else if (strcmp(key, "vtaunt") == 0) voice_taunt(r, entity);
  else if (strcmp(key, "score") == 0) qa_q3_ta_game_command_runtime_scoreboard(r, entity);
  else if (r->host.state->intermission_time != 0) say_command(r, entity, &args, QA_Q3_TA_SAY_ALL, true);
  else if (strcmp(key, "give") == 0) give(r, entity, &args);
  else if (strcmp(key, "god") == 0 || strcmp(key, "notarget") == 0 || strcmp(key, "noclip") == 0) toggle(r, entity, key);
  else if (strcmp(key, "kill") == 0) kill_self(r, entity);
  else if (strcmp(key, "teamtask") == 0) team_task(r, entity, &args);
  else if (strcmp(key, "levelshot") == 0) level_shot(r, entity);
  else if (strcmp(key, "follow") == 0) follow(r, entity, &args);
  else if (strcmp(key, "follownext") == 0) qa_q3_ta_game_command_runtime_follow_cycle(r, entity, 1);
  else if (strcmp(key, "followprev") == 0) qa_q3_ta_game_command_runtime_follow_cycle(r, entity, -1);
  else if (strcmp(key, "team") == 0) team_command(r, entity, &args);
  else if (strcmp(key, "where") == 0) {
    const char *vtos = (const char *)qa_q3_game_utility_scratch_vtos(r->host.pool->utilities, entity->s.origin);
    qa_q3_game_format_arg a = qa_q3_game_format_string(vtos);
    print_fmt_to(r, entity, "%s\n", &a, 1);
  }
  else if (strcmp(key, "callvote") == 0) call_vote(r, entity, &args);
  else if (strcmp(key, "vote") == 0) cast_vote(r, entity, &args);
  else if (strcmp(key, "callteamvote") == 0) call_team_vote(r, entity, &args);
  else if (strcmp(key, "teamvote") == 0) cast_team_vote(r, entity, &args);
  else if (strcmp(key, "gc") == 0) game_command(r, entity, &args);
  else if (strcmp(key, "setviewpos") == 0) set_view_position(r, entity, &args);
  else if (strcmp(key, "stats") == 0) { /* empty source body */ }
  else {
    qa_q3_game_format_arg a = qa_q3_game_format_string(command);
    print_fmt_to(r, entity, "unknown cmd %s\n", &a, 1);
  }
  free(key);
  free(command);
  arguments_destroy(&args);
}
