/* See header. Ported from id Software's code/game/ai_dmq3.c. GPL-2.0-or-later. */
#include "qa/bots/behavior/q3/ai_orders.h"
#include "ai_orders_internal.h"
#include "qa/bots/behavior/library/actions.h"
#include "qa/bots/behavior/q3/ai_combat.h"
#include "qa/bots/behavior/q3/ai_command.h"
#include "qa/bots/behavior/q3/ai_context.h"
#include "qa/bots/behavior/q3/ai_definitions.h"
#include "qa/bots/behavior/q3/ai_main.h"
#include "qa/bots/behavior/q3/ai_navigation.h"
#include "qa/bots/behavior/q3/ai_team.h"
#include "qa/bots/behavior/q3/info.h"
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/core/game_numeric.h"
#include "qa/core/info_string.h"
#include "qa/core/math.h"
#include "qa/foundation/support.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { TEAM_FREE = QA_Q3_BASE_TEAM_FREE, TEAM_RED = QA_Q3_BASE_TEAM_RED, TEAM_BLUE = QA_Q3_BASE_TEAM_BLUE };
enum { GT_TEAM = QA_Q3_BASE_GT_TEAM, GT_CTF = QA_Q3_BASE_GT_CTF, GT_1FCTF = QA_Q3_BASE_GT_1FCTF,
       GT_OBELISK = QA_Q3_BASE_GT_OBELISK, GT_HARVESTER = QA_Q3_BASE_GT_HARVESTER };

const char *qa_ai_configstring_bots_a(qa_game_ai_context_bots_a *ctx, int64_t index, char *buffer) {
  qa_q3_config_string_store *store = ctx->game->options.configstrings;
  snprintf(buffer, QA_AI_INFO_SIZE, "%s", store->get(store->ctx, (double)index));
  return buffer;
}

void qa_ai_print_bots_a(qa_game_ai_context_bots_a *ctx, const char *text) {
  ctx->game->options.engine.print(ctx->game->options.engine.ctx, text);
}

int64_t qa_ai_sv_maxclients_bots_a(qa_game_ai_context_bots_a *ctx) {
  qa_cvar_read read;
  if (!qa_cvar_registry_find(ctx->game->options.cvars, "sv_maxclients", &read))
    return 0;
  int64_t value = (int64_t)read.integer_value;
  qa_cvar_read_free(&read);
  return value;
}

bool qa_ai_missionpack_bots_a(qa_game_ai_context_bots_a *ctx) {
  return ctx->game->options.product == QA_Q3_BASE_PRODUCT_MISSIONPACK;
}

double qa_ai_area_travel_time_bots_a(qa_game_ai_context_bots_a *ctx, int64_t area, qa_vec3 origin,
                                     int64_t goal_area) {
  qa_bot_navigation_bots_a *navigation = qa_game_ai_context_navigation(ctx);
  qa_area_travel_time_query query = {area, true, origin, goal_area, QA_TFL_DEFAULT};
  return navigation->area_travel_time_to_goal(navigation->ctx, query);
}

bool qa_ai_navigation_ready_bots_a(qa_game_ai_context_bots_a *ctx) {
  qa_bot_navigation_bots_a *navigation = qa_game_ai_context_navigation(ctx);
  return navigation->ready(navigation->ctx);
}

bool qa_ai_client_origin_bots_a(qa_game_ai_context_bots_a *ctx, int64_t client, qa_vec3 *origin) {
  qa_q3_base_player_state state;
  if (!qa_game_ai_context_get_client_state(ctx, client, &state))
    return false;
  *origin = qa_q3_base_player_state_record_origin(&state);
  qa_q3_base_player_state_record_destroy(&state);
  return true;
}

bool qa_ai_client_score_bots_a(qa_game_ai_context_bots_a *ctx, int64_t client, double *score) {
  qa_q3_base_player_state state;
  if (!qa_game_ai_context_get_client_state(ctx, client, &state))
    return false;
  *score = qa_q3_base_player_state_slots_get(&state.persistant, QA_Q3_BASE_PERS_SCORE);
  qa_q3_base_player_state_record_destroy(&state);
  return true;
}

char *qa_ai_info_value_bots_a(const char *info, const char *key, char *out) {
  char *value = qa_info_value_for_key(info, key);
  if (value == NULL)
    qa_range_error("Bot info value allocation failed");
  snprintf(out, QA_AI_INFO_SIZE, "%s", value);
  free(value);
  return out;
}

bool qa_ai_fold_equal_bots_a(const char *a, const char *b) {
  for (;; a++, b++) {
    int x = (uint8_t)*a, y = (uint8_t)*b;
    if (x >= 'A' && x <= 'Z')
      x += 32;
    if (y >= 'A' && y <= 'Z')
      y += 32;
    if (x != y)
      return false;
    if (x == 0)
      return true;
  }
}

int64_t qa_ai_cached_max_clients_bots_a(qa_game_ai_context_bots_a *ctx,
                                        qa_game_ai_max_clients_cell_bots_a *cells, size_t *count,
                                        size_t capacity, const char *name) {
  qa_game_ai_max_clients_cell_bots_a *cell = NULL;
  for (size_t i = 0; i < *count; i++)
    if (strcmp(cells[i].name, name) == 0)
      cell = &cells[i];
  if (cell == NULL || cell->value == 0) {
    int64_t value = qa_ai_sv_maxclients_bots_a(ctx);
    if (cell == NULL) {
      if (*count >= capacity)
        qa_range_error("Bot maxclients cache has no cell for %s", name);
      cell = &cells[(*count)++];
      cell->name = name;
    }
    cell->value = value;
  }
  return cell->value < QA_AI_MAX_CLIENTS ? cell->value : QA_AI_MAX_CLIENTS;
}

/** Q_CleanStr keeps printable ASCII and removes source color escape pairs. */
void qa_bot_clean_string_bots_a(const char *t, char *out, size_t cap) {
  if (cap == 0)
    return;
  size_t w = 0;
  for (size_t i = 0; t[i] && w + 1 < cap; i++) {
    int code = (uint8_t)t[i];
    if (code == 94 && t[i + 1] && (uint8_t)t[i + 1] != 94) {
      i++;
      continue;
    }
    if (code >= 32 && code <= 126)
      out[w++] = t[i];
  }
  out[w] = '\0';
}

/* rawClientName into buffer[QA_AI_INFO_SIZE]; false is TS null. */
static bool raw_client_name(qa_game_ai_context_bots_a *ctx, int64_t client, char *buffer) {
  if (client < 0 || client >= QA_AI_MAX_CLIENTS) {
    qa_ai_print_bots_a(ctx, "^1Error: ClientName: client out of range\n");
    return false;
  }
  char info[QA_AI_INFO_SIZE];
  qa_ai_info_value_bots_a(qa_ai_configstring_bots_a(ctx, QA_AI_CS_PLAYERS + client, info), "n",
                          buffer);
  return true;
}

char *qa_client_name_bots_a(qa_game_ai_context_bots_a *ctx, int64_t client, int64_t size, char *out) {
  char name[QA_AI_INFO_SIZE];
  if (!raw_client_name(ctx, client, name)) {
    strcpy(out, "[client out of range]");
    return out;
  }
  /* name.slice(0, size - 1), then the clean, which only shrinks it. */
  if (size - 1 < (int64_t)strlen(name))
    name[size - 1] = '\0';
  qa_bot_clean_string_bots_a(name, out, sizeof name);
  return out;
}

void qa_copy_client_name_to_team_leader_bots_a(qa_game_ai_context_bots_a *ctx,
                                               qa_bot_state_bots_a *state, int64_t client) {
  char name[QA_AI_INFO_SIZE];
  if (raw_client_name(ctx, client, name))
    qa_bot_state_copy_team_leader_client_name(state, name);
}

char *qa_client_skin_bots_a(qa_game_ai_context_bots_a *ctx, int64_t client, int64_t size, char *out) {
  if (client < 0 || client >= QA_AI_MAX_CLIENTS) {
    qa_ai_print_bots_a(ctx, "^1Error: ClientSkin: client out of range\n");
    strcpy(out, "[client out of range]");
    return out;
  }
  char info[QA_AI_INFO_SIZE], value[QA_AI_INFO_SIZE];
  qa_ai_info_value_bots_a(qa_ai_configstring_bots_a(ctx, QA_AI_CS_PLAYERS + client, info), "model",
                          value);
  snprintf(out, (size_t)size, "%s", value);
  return out;
}

/* botCleanString(configstring) with its "n" value equal to name, case-insensitively. */
static bool configstring_name_matches(qa_game_ai_context_bots_a *ctx, int64_t client,
                                      const char *name) {
  char info[QA_AI_INFO_SIZE], clean[QA_AI_INFO_SIZE], value[QA_AI_INFO_SIZE];
  qa_bot_clean_string_bots_a(qa_ai_configstring_bots_a(ctx, QA_AI_CS_PLAYERS + client, info), clean,
                             sizeof clean);
  return qa_ai_fold_equal_bots_a(qa_ai_info_value_bots_a(clean, "n", value), name);
}

int64_t qa_client_from_name_bots_a(qa_game_ai_context_bots_a *ctx, const char *name) {
  if (ctx->team.client_from_name_max_clients == 0)
    ctx->team.client_from_name_max_clients = qa_ai_sv_maxclients_bots_a(ctx);
  for (int64_t i = 0; i < ctx->team.client_from_name_max_clients && i < QA_AI_MAX_CLIENTS; i++)
    if (configstring_name_matches(ctx, i, name))
      return i;
  return -1;
}

int64_t qa_client_on_same_team_from_name_bots_a(qa_game_ai_context_bots_a *ctx,
                                                qa_bot_state_bots_a *state, const char *name) {
  if (ctx->team.client_on_same_team_from_name_max_clients == 0)
    ctx->team.client_on_same_team_from_name_max_clients = qa_ai_sv_maxclients_bots_a(ctx);
  for (int64_t i = 0;
       i < ctx->team.client_on_same_team_from_name_max_clients && i < QA_AI_MAX_CLIENTS; i++) {
    if (!qa_bot_same_team_bots_a(ctx, state, i))
      continue;
    if (configstring_name_matches(ctx, i, name))
      return i;
  }
  return -1;
}

static int fold_upper(int c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }

const char *qa_stristr_bots_a(const char *text, const char *part) {
  size_t tl = strlen(text), pl = strlen(part);
  /* TS: an empty pattern matches at offset 0, which an empty input rejects. */
  if (tl == 0)
    return NULL;
  for (size_t i = 0; i + pl <= tl; i++) {
    size_t j = 0;
    for (; j < pl; j++)
      if (fold_upper((uint8_t)text[i + j]) != fold_upper((uint8_t)part[j]))
        break;
    if (j == pl)
      return text + i;
  }
  return NULL;
}

char *qa_easy_client_name_bots_a(qa_game_ai_context_bots_a *ctx, int64_t client, int64_t size,
                                 char *out) {
  char source[QA_AI_INFO_SIZE], name[QA_AI_INFO_SIZE];
  qa_client_name_bots_a(ctx, client, 128, source);
  size_t n = 0;
  for (size_t i = 0; source[i]; i++) {
    char c = (char)((uint8_t)source[i] & 127);
    if (c != ' ' && c != '\0')
      name[n++] = c;
  }
  name[n] = '\0';
  const char *left = strchr(name, 0x5b);
  const char *right = strchr(name, 0x5d);
  if (left != NULL && right != NULL) {
    size_t lo = (size_t)((left < right ? left : right) - name);
    size_t hi = (size_t)((left < right ? right : left) - name);
    memmove(name + lo, name + hi + 1, n - hi);
  }
  const char *cursor = name;
  if ((cursor[0] == 'm' || cursor[0] == 'M') && (cursor[1] == 'r' || cursor[1] == 'R'))
    cursor += 2;
  int64_t w = 0;
  for (; *cursor && w < size - 1; cursor++) {
    int c = (uint8_t)*cursor;
    if (c >= 'A' && c <= 'Z')
      c += 32;
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')
      out[w++] = (char)c;
  }
  out[w] = '\0';
  return out;
}

static void engine_print_leaf(void *context, const char *text) {
  qa_ai_print_bots_a((qa_game_ai_context_bots_a *)context, text);
}

void qa_bot_set_user_info_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                 const char *key, const char *value) {
  qa_source_bot_game_bots_a *game = ctx->game;
  char current[QA_AI_INFO_SIZE];
  snprintf(current, sizeof current, "%s",
           game->options.engine.get_userinfo(game->options.engine.ctx, state->src->client));
  char *info = qa_bot_info_set_value_for_key(current, key, value, engine_print_leaf, ctx);
  if (info == NULL)
    qa_range_error("Bot userinfo allocation failed");
  game->options.engine.set_userinfo(game->options.engine.ctx, state->src->client, info);
  free(info);
  game->client_userinfo_changed(game->ctx, state->src->client);
}

bool qa_team_play_is_on_bots_a(qa_game_ai_context_bots_a *ctx) {
  return ctx->deathmatch.gametype >= GT_TEAM;
}

static int32_t client_team_value(qa_game_ai_context_bots_a *ctx, int64_t client) {
  char info[QA_AI_INFO_SIZE], value[QA_AI_INFO_SIZE];
  return qa_game_atoi(qa_ai_info_value_bots_a(
      qa_ai_configstring_bots_a(ctx, QA_AI_CS_PLAYERS + client, info), "t", value));
}

int64_t qa_bot_team_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  if (state->src->client < 0 || state->src->client >= QA_AI_MAX_CLIENTS)
    return TEAM_FREE;
  int32_t team = client_team_value(ctx, state->src->client);
  return team == TEAM_RED ? TEAM_RED : team == TEAM_BLUE ? TEAM_BLUE : TEAM_FREE;
}

int64_t qa_bot_opposite_team_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  int64_t team = qa_bot_team_bots_a(ctx, state);
  return team == TEAM_RED ? TEAM_BLUE : team == TEAM_BLUE ? TEAM_RED : TEAM_FREE;
}

bool qa_bot_same_team_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                             int64_t entity) {
  if (state->src->client < 0 || state->src->client >= QA_AI_MAX_CLIENTS || entity < 0 ||
      entity >= QA_AI_MAX_CLIENTS || !qa_team_play_is_on_bots_a(ctx))
    return false;
  return client_team_value(ctx, state->src->client) == client_team_value(ctx, entity);
}

const qa_bot_goal *qa_bot_enemy_flag_bots_a(qa_game_ai_context_bots_a *ctx,
                                            qa_bot_state_bots_a *state) {
  return qa_bot_team_bots_a(ctx, state) == TEAM_RED ? &ctx->deathmatch.ctf_blue_flag
                                                    : &ctx->deathmatch.ctf_red_flag;
}

const qa_bot_goal *qa_bot_team_flag_bots_a(qa_game_ai_context_bots_a *ctx,
                                           qa_bot_state_bots_a *state) {
  return qa_bot_team_bots_a(ctx, state) == TEAM_RED ? &ctx->deathmatch.ctf_red_flag
                                                    : &ctx->deathmatch.ctf_blue_flag;
}

int64_t qa_bot_ctf_carrying_flag_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  if (ctx->deathmatch.gametype != GT_CTF)
    return QA_CTFFLAG_NONE;
  if (state->src->inventory[QA_INV_REDFLAG] > 0)
    return QA_CTFFLAG_RED;
  if (state->src->inventory[QA_INV_BLUEFLAG] > 0)
    return QA_CTFFLAG_BLUE;
  return QA_CTFFLAG_NONE;
}

bool qa_bot_1fctf_carrying_flag_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  return ctx->deathmatch.gametype == GT_1FCTF && state->src->inventory[QA_INV_NEUTRALFLAG] > 0;
}

bool qa_bot_harvester_carrying_cubes_bots_a(qa_game_ai_context_bots_a *ctx,
                                            qa_bot_state_bots_a *state) {
  return ctx->deathmatch.gametype == GT_HARVESTER &&
         (state->src->inventory[QA_INV_REDCUBE] > 0 || state->src->inventory[QA_INV_BLUECUBE] > 0);
}

static bool state_missionpack(const qa_bot_state_bots_a *state) {
  return state->product == QA_Q3_BASE_PRODUCT_MISSIONPACK;
}

int64_t qa_bot_synonym_context_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  int64_t result = 1 | 2 | 1024;
  bool red = qa_bot_team_bots_a(ctx, state) == TEAM_RED;
  int64_t gt = ctx->deathmatch.gametype;
  if (gt == GT_CTF || (state_missionpack(state) && gt == GT_1FCTF))
    result |= red ? 4 : 8;
  else if (state_missionpack(state) && gt == GT_OBELISK)
    result |= red ? 32 : 64;
  else if (state_missionpack(state) && gt == GT_HARVESTER)
    result |= red ? 128 : 256;
  return result;
}

static int64_t carrier(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state, bool teammate,
                       bool visible, bool cubes) {
  for (int64_t i = 0; i < ctx->deathmatch.maxclients && i < QA_AI_MAX_CLIENTS; i++) {
    if (i == state->src->client)
      continue;
    qa_aas_entity_info_bots_a info = qa_bot_entity_observations_info(&ctx->observations, i);
    if (!info.valid || !(cubes ? qa_entity_carries_cubes_bots_a(ctx, &info)
                               : qa_entity_carries_flag_bots_a(ctx, &info)))
      continue;
    if (qa_bot_same_team_bots_a(ctx, state, i) != teammate)
      continue;
    if (visible && qa_bot_entity_visible_bots_a(ctx, state->src->entity_num, qa_bot_v3(state->src->eye), qa_bot_v3(state->src->viewangles),
                                                360, i) <= 0)
      continue;
    return i;
  }
  return -1;
}

int64_t qa_bot_team_flag_carrier_visible_bots_a(qa_game_ai_context_bots_a *ctx,
                                                qa_bot_state_bots_a *state) {
  return carrier(ctx, state, true, true, false);
}
int64_t qa_bot_team_flag_carrier_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  return carrier(ctx, state, true, false, false);
}
int64_t qa_bot_enemy_flag_carrier_visible_bots_a(qa_game_ai_context_bots_a *ctx,
                                                 qa_bot_state_bots_a *state) {
  return carrier(ctx, state, false, true, false);
}
int64_t qa_bot_team_cube_carrier_visible_bots_a(qa_game_ai_context_bots_a *ctx,
                                                qa_bot_state_bots_a *state) {
  return carrier(ctx, state, true, true, true);
}
int64_t qa_bot_enemy_cube_carrier_visible_bots_a(qa_game_ai_context_bots_a *ctx,
                                                 qa_bot_state_bots_a *state) {
  return carrier(ctx, state, false, true, true);
}

/* The source deliberately counts only flag carriers in this helper. */
void qa_bot_visible_team_mates_and_enemies_bots_a(qa_game_ai_context_bots_a *ctx,
                                                  qa_bot_state_bots_a *state, double range,
                                                  int64_t *teammates, int64_t *enemies) {
  *teammates = 0;
  *enemies = 0;
  for (int64_t i = 0; i < ctx->deathmatch.maxclients && i < QA_AI_MAX_CLIENTS; i++) {
    if (i == state->src->client)
      continue;
    qa_aas_entity_info_bots_a info = qa_bot_entity_observations_info(&ctx->observations, i);
    if (!info.valid || !qa_entity_carries_flag_bots_a(ctx, &info))
      continue;
    qa_vec3 direction = qa_sub3(info.origin, qa_bot_v3(state->src->origin));
    if (qa_dot3(direction, direction) > (double)(float)(range * range))
      continue;
    if (qa_bot_entity_visible_bots_a(ctx, state->src->entity_num, qa_bot_v3(state->src->eye), qa_bot_v3(state->src->viewangles), 360, i) <=
        0)
      continue;
    if (qa_bot_same_team_bots_a(ctx, state, i))
      (*teammates)++;
    else
      (*enemies)++;
  }
}

void qa_bot_remember_last_ordered_task_bots_a(qa_game_ai_context_bots_a *ctx,
                                              qa_bot_state_bots_a *state) {
  (void)ctx; /* TS _context */
  if (!state->src->ordered)
    return;
  state->src->last_goal_decisionmaker = state->src->decisionmaker;
  state->src->last_goal_ltg_type = state->src->ltg_type;
  state->src->last_goal_team_goal = state->src->team_goal;
  state->src->last_goal_teammate = state->src->teammate;
}

void qa_bot_set_team_status_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  if (!state_missionpack(state))
    return;
  int task = 3;
  int64_t gt = ctx->deathmatch.gametype;
  switch (state->src->ltg_type) {
  case QA_LTG_TEAMACCOMPANY: {
    qa_aas_entity_info_bots_a info =
        qa_bot_entity_observations_info(&ctx->observations, state->src->teammate);
    task = ((gt == GT_CTF || gt == GT_1FCTF) && qa_entity_carries_flag_bots_a(ctx, &info)) ||
                   (gt == GT_HARVESTER && qa_entity_carries_cubes_bots_a(ctx, &info))
               ? 6
               : 4;
    break;
  }
  case QA_LTG_DEFENDKEYAREA:
  case QA_LTG_RUSHBASE:
    task = 2;
    break;
  case QA_LTG_GETFLAG:
  case QA_LTG_HARVEST:
  case QA_LTG_ATTACKENEMYBASE:
    task = 1;
    break;
  case QA_LTG_RETURNFLAG:
    task = 5;
    break;
  case QA_LTG_CAMP:
  case QA_LTG_CAMPORDER:
    task = 7;
    break;
  default:
    break;
  }
  char text[8];
  snprintf(text, sizeof text, "%d", task);
  qa_bot_set_user_info_bots_a(ctx, state, "teamtask", text);
}

bool qa_bot_set_last_ordered_task_bots_a(qa_game_ai_context_bots_a *ctx,
                                         qa_bot_state_bots_a *state) {
  int64_t gt = ctx->deathmatch.gametype;
  if (gt == GT_CTF && state->src->last_goal_ltg_type == QA_LTG_RETURNFLAG &&
      (qa_bot_team_bots_a(ctx, state) == TEAM_RED ? state->src->red_flag_status
                                                  : state->src->blue_flag_status) == 0)
    state->src->last_goal_ltg_type = 0;
  if (state->src->last_goal_ltg_type == 0)
    return false;
  state->src->decisionmaker = state->src->last_goal_decisionmaker;
  state->src->ordered = true;
  state->src->ltg_type = state->src->last_goal_ltg_type;
  state->src->team_goal = state->src->last_goal_team_goal;
  state->src->teammate = state->src->last_goal_teammate;
  state->src->team_goal_time = (float)(ctx->time + 300);
  qa_bot_set_team_status_bots_a(ctx, state);
  if (gt == GT_CTF && state->src->ltg_type == QA_LTG_GETFLAG) {
    if (qa_ai_navigation_ready_bots_a(ctx)) {
      double team = qa_ai_area_travel_time_bots_a(ctx, state->src->area_num, qa_bot_v3(state->src->origin),
                                                  qa_bot_team_flag_bots_a(ctx, state)->area);
      double enemy = qa_ai_area_travel_time_bots_a(ctx, state->src->area_num, qa_bot_v3(state->src->origin),
                                                   qa_bot_enemy_flag_bots_a(ctx, state)->area);
      if (enemy > team)
        qa_bot_get_alternate_route_goal_bots_a(ctx, state, qa_bot_opposite_team_bots_a(ctx, state));
    }
  }
  return true;
}

void qa_bot_refuse_order_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  if (!state->src->ordered)
    return;
  if (state->src->order_time != 0 && state->src->order_time > (double)(float)(ctx->time - 10)) {
    qa_bot_action_action(&ctx->library->actions, state->src->client, QA_BOT_ACTION_NEGATIVE);
    qa_bot_voice_chat_bots_a(ctx, state, state->src->decisionmaker, "no");
    state->src->order_time = 0;
  }
}

static void decide_for_self(qa_bot_state_bots_a *state) {
  state->src->decisionmaker = state->src->client;
  state->src->ordered = false;
}

static int64_t own_decision_deadline(double time) {
  double deadline = (float)(time + 5);
  if (!isfinite(deadline) || deadline < -2147483648.0 || deadline > 2147483647.0)
    qa_range_error("Bot own-decision deadline has undefined source float-to-int conversion");
  return (int32_t)trunc(deadline);
}

static double message_delay_time(qa_game_ai_context_bots_a *ctx) {
  return (float)(ctx->time + (float)(2 * qa_game_ai_context_random(ctx)));
}

static void accompany(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state, int64_t teammate) {
  decide_for_self(state);
  state->src->teammate = teammate;
  state->src->teammate_visible_time = (float)ctx->time;
  state->src->team_message_time = 0;
  state->src->arrive_time = 1;
  qa_bot_voice_chat_bots_a(ctx, state, teammate, "onfollow");
  state->src->team_goal_time = (float)(ctx->time + QA_TEAM_ACCOMPANY_TIME);
  state->src->ltg_type = QA_LTG_TEAMACCOMPANY;
  state->src->formation_dist = 3.5 * 32;
  qa_bot_set_team_status_bots_a(ctx, state);
}

static void rush_base(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  qa_bot_refuse_order_bots_a(ctx, state);
  state->src->ltg_type = QA_LTG_RUSHBASE;
  state->src->team_goal_time = (float)(ctx->time + QA_CTF_RUSHBASE_TIME);
  state->src->rush_base_away_time = 0;
  decide_for_self(state);
}

static bool protect_ordered_goal(int64_t type) {
  return type == QA_LTG_TEAMHELP || type == QA_LTG_TEAMACCOMPANY || type == QA_LTG_CAMPORDER ||
         type == QA_LTG_PATROL || type == QA_LTG_GETITEM;
}

static bool ctf_goal(int64_t type, bool one_flag) {
  return protect_ordered_goal(type) || type == QA_LTG_DEFENDKEYAREA || type == QA_LTG_GETFLAG ||
         type == QA_LTG_RUSHBASE || type == QA_LTG_RETURNFLAG || type == QA_LTG_MAKELOVE_UNDER ||
         type == QA_LTG_MAKELOVE_ONTOP || (one_flag && type == QA_LTG_ATTACKENEMYBASE);
}

static void thresholds(const qa_bot_state_bots_a *state, double *attack, double *defense) {
  if ((state->src->team_task_preference & (QA_TASK_ATTACKER | QA_TASK_DEFENDER)) != 0) {
    *attack = (state->src->team_task_preference & QA_TASK_ATTACKER) != 0 ? (double)(float)0.7
                                                                    : (double)(float)0.2;
    *defense = (float)0.9;
    return;
  }
  *attack = (float)0.4;
  *defense = (float)0.7;
}

static void defend(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                   const qa_bot_goal *goal) {
  decide_for_self(state);
  qa_bot_goal_state_copy_from(&state->src->team_goal, *goal);
  state->src->ltg_type = QA_LTG_DEFENDKEYAREA;
  state->src->team_goal_time = (float)(ctx->time + QA_TEAM_DEFENDKEYAREA_TIME);
  state->src->defend_away_time = 0;
  qa_bot_set_team_status_bots_a(ctx, state);
}

static void roam(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  state->src->ltg_type = 0;
  state->src->ctf_roam_time = (float)(ctx->time + QA_CTF_ROAM_TIME);
  qa_bot_set_team_status_bots_a(ctx, state);
}

static bool teammate_not_carrying_flag(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  qa_aas_entity_info_bots_a info =
      qa_bot_entity_observations_info(&ctx->observations, state->src->teammate);
  return !qa_entity_carries_flag_bots_a(ctx, &info);
}

void qa_bot_ctf_seek_goals_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  const qa_game_ai_deathmatch_state_bots_a *goals = &ctx->deathmatch;
  if (qa_bot_ctf_carrying_flag_bots_a(ctx, state) != QA_CTFFLAG_NONE) {
    if (state->src->ltg_type != QA_LTG_RUSHBASE) {
      rush_base(ctx, state);
      int64_t team = qa_bot_team_bots_a(ctx, state);
      qa_vec3 direction = team == TEAM_RED    ? qa_sub3(qa_bot_v3(state->src->origin), goals->ctf_blue_flag.origin)
                          : team == TEAM_BLUE ? qa_sub3(qa_bot_v3(state->src->origin), goals->ctf_red_flag.origin)
                                              : (qa_vec3){999, 999, 999};
      if (qa_length3(direction) < 128)
        qa_bot_get_alternate_route_goal_bots_a(ctx, state, qa_bot_opposite_team_bots_a(ctx, state));
      else
        state->src->alt_route_goal.area = 0;
      qa_bot_set_user_info_bots_a(ctx, state, "teamtask", "1");
      qa_bot_voice_chat_bots_a(ctx, state, -1, "ihaveflag");
    } else if (state->src->rush_base_away_time > ctx->time &&
               (qa_bot_team_bots_a(ctx, state) == TEAM_RED ? state->src->red_flag_status
                                                           : state->src->blue_flag_status) == 0)
      state->src->rush_base_away_time = 0;
    return;
  }
  if (state->src->ltg_type == QA_LTG_TEAMACCOMPANY && !state->src->ordered &&
      teammate_not_carrying_flag(ctx, state))
    state->src->ltg_type = 0;
  int64_t flag_status = qa_bot_team_bots_a(ctx, state) == TEAM_RED
                            ? state->src->red_flag_status * 2 + state->src->blue_flag_status
                            : state->src->blue_flag_status * 2 + state->src->red_flag_status;
  if (flag_status == 1) {
    if (state->src->own_decision_time < ctx->time &&
        !(state->src->ltg_type == QA_LTG_DEFENDKEYAREA &&
          (state->src->team_goal.number == goals->ctf_red_flag.number ||
           state->src->team_goal.number == goals->ctf_blue_flag.number))) {
      int64_t teammate = qa_bot_team_flag_carrier_visible_bots_a(ctx, state);
      if (teammate >= 0 &&
          (state->src->ltg_type != QA_LTG_TEAMACCOMPANY || state->src->teammate != teammate)) {
        qa_bot_refuse_order_bots_a(ctx, state);
        accompany(ctx, state, teammate);
        state->src->own_decision_time = own_decision_deadline(ctx->time);
      }
    }
    return;
  }
  if (flag_status == 2) {
    if (state->src->own_decision_time < ctx->time) {
      qa_bot_enemy_flag_carrier_visible_bots_a(ctx, state);
      if (state->src->ltg_type != QA_LTG_GETFLAG && state->src->ltg_type != QA_LTG_RETURNFLAG &&
          !protect_ordered_goal(state->src->ltg_type)) {
        qa_bot_refuse_order_bots_a(ctx, state);
        decide_for_self(state);
        state->src->ltg_type =
            qa_game_ai_context_random(ctx) < 0.5 ? QA_LTG_GETFLAG : QA_LTG_RETURNFLAG;
        state->src->team_message_time = 0;
        state->src->team_goal_time = (float)(ctx->time + QA_CTF_GETFLAG_TIME);
        qa_bot_get_alternate_route_goal_bots_a(ctx, state, qa_bot_opposite_team_bots_a(ctx, state));
        qa_bot_set_team_status_bots_a(ctx, state);
        state->src->own_decision_time = own_decision_deadline(ctx->time);
      }
    }
    return;
  }
  if (flag_status == 3) {
    if (state->src->own_decision_time < ctx->time && state->src->ltg_type != QA_LTG_RETURNFLAG &&
        state->src->ltg_type != QA_LTG_TEAMACCOMPANY) {
      int64_t teammate = qa_bot_team_flag_carrier_visible_bots_a(ctx, state);
      qa_bot_refuse_order_bots_a(ctx, state);
      if (teammate >= 0)
        accompany(ctx, state, teammate);
      else {
        decide_for_self(state);
        state->src->team_message_time = message_delay_time(ctx);
        state->src->ltg_type = QA_LTG_RETURNFLAG;
        state->src->team_goal_time = (float)(ctx->time + QA_CTF_RETURNFLAG_TIME);
        qa_bot_get_alternate_route_goal_bots_a(ctx, state, qa_bot_opposite_team_bots_a(ctx, state));
        qa_bot_set_team_status_bots_a(ctx, state);
      }
      state->src->own_decision_time = own_decision_deadline(ctx->time);
    }
    return;
  }
  if (qa_bot_team_leader_bots_a(ctx, state))
    return;
  if (state->src->last_goal_ltg_type != 0)
    state->src->team_goal_time = (float)(state->src->team_goal_time + 60);
  if (!state->src->ordered && state->src->last_goal_ltg_type != 0)
    state->src->ltg_type = 0;
  if (ctf_goal(state->src->ltg_type, false) || qa_bot_set_last_ordered_task_bots_a(ctx, state))
    return;
  if (state->src->own_decision_time > ctx->time || state->src->ctf_roam_time > ctx->time ||
      qa_bot_aggression_bots_a(ctx, state) < 50)
    return;
  state->src->team_message_time = message_delay_time(ctx);
  double attack, defense;
  thresholds(state, &attack, &defense);
  double random = qa_game_ai_context_random(ctx);
  if (random < attack && goals->ctf_red_flag.area != 0 && goals->ctf_blue_flag.area != 0) {
    decide_for_self(state);
    state->src->ltg_type = QA_LTG_GETFLAG;
    state->src->team_goal_time = (float)(ctx->time + QA_CTF_GETFLAG_TIME);
    qa_bot_get_alternate_route_goal_bots_a(ctx, state, qa_bot_opposite_team_bots_a(ctx, state));
    qa_bot_set_team_status_bots_a(ctx, state);
  } else if (random < defense && goals->ctf_red_flag.area != 0 && goals->ctf_blue_flag.area != 0)
    defend(ctx, state, qa_bot_team_flag_bots_a(ctx, state));
  else
    roam(ctx, state);
  state->src->own_decision_time = own_decision_deadline(ctx->time);
  if (qa_bot_library_debug_build(ctx->library))
    qa_bot_print_team_goal_bots_a(ctx, state);
}

void qa_bot_ctf_retreat_goals_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  if (qa_bot_ctf_carrying_flag_bots_a(ctx, state) != QA_CTFFLAG_NONE &&
      state->src->ltg_type != QA_LTG_RUSHBASE) {
    rush_base(ctx, state);
    qa_bot_set_team_status_bots_a(ctx, state);
  }
}

void qa_bot_1fctf_seek_goals_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  const qa_game_ai_deathmatch_state_bots_a *goals = &ctx->deathmatch;
  if (qa_bot_1fctf_carrying_flag_bots_a(ctx, state)) {
    if (state->src->ltg_type != QA_LTG_RUSHBASE) {
      rush_base(ctx, state);
      qa_bot_get_alternate_route_goal_bots_a(ctx, state, qa_bot_opposite_team_bots_a(ctx, state));
      qa_bot_set_team_status_bots_a(ctx, state);
      qa_bot_voice_chat_bots_a(ctx, state, -1, "ihaveflag");
    }
    return;
  }
  if (state->src->ltg_type == QA_LTG_TEAMACCOMPANY && !state->src->ordered &&
      teammate_not_carrying_flag(ctx, state))
    state->src->ltg_type = 0;
  if (state->src->neutral_flag_status == 1) {
    if (state->src->own_decision_time < ctx->time) {
      if (state->src->ltg_type != QA_LTG_TEAMACCOMPANY) {
        int64_t teammate = qa_bot_team_flag_carrier_visible_bots_a(ctx, state);
        if (teammate >= 0) {
          qa_bot_refuse_order_bots_a(ctx, state);
          accompany(ctx, state, teammate);
          state->src->own_decision_time = own_decision_deadline(ctx->time);
          return;
        }
      }
      if (ctf_goal(state->src->ltg_type, true) && state->src->ltg_type != QA_LTG_RETURNFLAG)
        return;
      if (state->src->ltg_type != QA_LTG_ATTACKENEMYBASE) {
        qa_bot_refuse_order_bots_a(ctx, state);
        decide_for_self(state);
        qa_bot_goal_state_copy_from(&state->src->team_goal, *qa_bot_enemy_flag_bots_a(ctx, state));
        state->src->ltg_type = QA_LTG_ATTACKENEMYBASE;
        state->src->team_goal_time = (float)(ctx->time + QA_TEAM_ATTACKENEMYBASE_TIME);
        qa_bot_set_team_status_bots_a(ctx, state);
        state->src->own_decision_time = own_decision_deadline(ctx->time);
      }
    }
    return;
  }
  if (state->src->neutral_flag_status == 2) {
    if (state->src->own_decision_time < ctx->time) {
      qa_bot_enemy_flag_carrier_visible_bots_a(ctx, state);
      if (protect_ordered_goal(state->src->ltg_type))
        return;
      if (state->src->ltg_type != QA_LTG_DEFENDKEYAREA) {
        qa_bot_refuse_order_bots_a(ctx, state);
        defend(ctx, state, qa_bot_team_flag_bots_a(ctx, state));
        state->src->own_decision_time = own_decision_deadline(ctx->time);
      }
    }
    return;
  }
  if (qa_bot_team_leader_bots_a(ctx, state))
    return;
  if (state->src->last_goal_ltg_type != 0)
    state->src->team_goal_time = (float)(state->src->team_goal_time + 60);
  if (!state->src->ordered && state->src->last_goal_ltg_type != 0)
    state->src->ltg_type = 0;
  if (ctf_goal(state->src->ltg_type, true) || qa_bot_set_last_ordered_task_bots_a(ctx, state))
    return;
  if (state->src->own_decision_time > ctx->time || state->src->ctf_roam_time > ctx->time ||
      qa_bot_aggression_bots_a(ctx, state) < 50)
    return;
  state->src->team_message_time = message_delay_time(ctx);
  double attack, defense;
  thresholds(state, &attack, &defense);
  double random = qa_game_ai_context_random(ctx);
  if (random < attack && goals->ctf_neutral_flag.area != 0) {
    decide_for_self(state);
    state->src->ltg_type = QA_LTG_GETFLAG;
    state->src->team_goal_time = (float)(ctx->time + QA_CTF_GETFLAG_TIME);
    qa_bot_set_team_status_bots_a(ctx, state);
  } else if (random < defense && goals->ctf_red_flag.area != 0 && goals->ctf_blue_flag.area != 0)
    defend(ctx, state, qa_bot_team_flag_bots_a(ctx, state));
  else
    roam(ctx, state);
  state->src->own_decision_time = own_decision_deadline(ctx->time);
  if (qa_bot_library_debug_build(ctx->library))
    qa_bot_print_team_goal_bots_a(ctx, state);
}

void qa_bot_1fctf_retreat_goals_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  if (qa_bot_1fctf_carrying_flag_bots_a(ctx, state) && state->src->ltg_type != QA_LTG_RUSHBASE) {
    rush_base(ctx, state);
    qa_bot_get_alternate_route_goal_bots_a(ctx, state, qa_bot_opposite_team_bots_a(ctx, state));
    qa_bot_set_team_status_bots_a(ctx, state);
  }
}

void qa_bot_obelisk_seek_goals_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  if (qa_bot_team_leader_bots_a(ctx, state))
    return;
  if (state->src->last_goal_ltg_type != 0)
    state->src->team_goal_time = (float)(state->src->team_goal_time + 60);
  if (ctf_goal(state->src->ltg_type, true) || qa_bot_set_last_ordered_task_bots_a(ctx, state))
    return;
  if (state->src->ctf_roam_time > ctx->time || qa_bot_aggression_bots_a(ctx, state) < 50)
    return;
  state->src->team_message_time = message_delay_time(ctx);
  double attack, defense;
  thresholds(state, &attack, &defense);
  double random = qa_game_ai_context_random(ctx);
  const qa_game_ai_deathmatch_state_bots_a *goals = &ctx->deathmatch;
  if (random < attack && goals->red_obelisk.area != 0 && goals->blue_obelisk.area != 0) {
    decide_for_self(state);
    qa_bot_goal_state_copy_from(&state->src->team_goal, qa_bot_team_bots_a(ctx, state) == TEAM_RED
                                                            ? goals->blue_obelisk
                                                            : goals->red_obelisk);
    state->src->ltg_type = QA_LTG_ATTACKENEMYBASE;
    state->src->team_goal_time = (float)(ctx->time + QA_TEAM_ATTACKENEMYBASE_TIME);
    qa_bot_get_alternate_route_goal_bots_a(ctx, state, qa_bot_opposite_team_bots_a(ctx, state));
    qa_bot_set_team_status_bots_a(ctx, state);
  } else if (random < defense && goals->red_obelisk.area != 0 && goals->blue_obelisk.area != 0) {
    defend(ctx, state,
           qa_bot_team_bots_a(ctx, state) == TEAM_RED ? &goals->red_obelisk : &goals->blue_obelisk);
  } else
    roam(ctx, state);
}

void qa_bot_go_harvest_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  qa_bot_goal_state_copy_from(&state->src->team_goal, qa_bot_team_bots_a(ctx, state) == TEAM_RED
                                                          ? ctx->deathmatch.blue_obelisk
                                                          : ctx->deathmatch.red_obelisk);
  state->src->ltg_type = QA_LTG_HARVEST;
  state->src->team_goal_time = (float)(ctx->time + QA_TEAM_HARVEST_TIME);
  state->src->harvest_away_time = 0;
  qa_bot_set_team_status_bots_a(ctx, state);
}

/** BotObeliskRetreatGoals has an empty body in the reference. */
void qa_bot_obelisk_retreat_goals_bots_a(qa_game_ai_context_bots_a *ctx,
                                         qa_bot_state_bots_a *state) {
  (void)ctx;
  (void)state;
}

void qa_bot_harvester_seek_goals_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  if (qa_bot_harvester_carrying_cubes_bots_a(ctx, state)) {
    if (state->src->ltg_type != QA_LTG_RUSHBASE) {
      rush_base(ctx, state);
      qa_bot_get_alternate_route_goal_bots_a(ctx, state, qa_bot_opposite_team_bots_a(ctx, state));
      qa_bot_set_team_status_bots_a(ctx, state);
    }
    return;
  }
  if (qa_bot_team_leader_bots_a(ctx, state))
    return;
  if (state->src->ltg_type == QA_LTG_TEAMACCOMPANY && !state->src->ordered) {
    qa_aas_entity_info_bots_a info =
        qa_bot_entity_observations_info(&ctx->observations, state->src->teammate);
    if (!qa_entity_carries_cubes_bots_a(ctx, &info))
      state->src->ltg_type = 0;
  }
  if (state->src->last_goal_ltg_type != 0)
    state->src->team_goal_time = (float)(state->src->team_goal_time + 60);
  if ((ctf_goal(state->src->ltg_type, true) && state->src->ltg_type != QA_LTG_RUSHBASE &&
       state->src->ltg_type != QA_LTG_RETURNFLAG) ||
      state->src->ltg_type == QA_LTG_HARVEST || qa_bot_set_last_ordered_task_bots_a(ctx, state))
    return;
  if (state->src->ctf_roam_time > ctx->time || qa_bot_aggression_bots_a(ctx, state) < 50)
    return;
  state->src->team_message_time = message_delay_time(ctx);
  qa_bot_enemy_cube_carrier_visible_bots_a(ctx, state);
  if (state->src->ltg_type != QA_LTG_TEAMACCOMPANY) {
    int64_t teammate = qa_bot_team_cube_carrier_visible_bots_a(ctx, state);
    if (teammate >= 0) {
      accompany(ctx, state, teammate);
      return;
    }
  }
  double attack, defense;
  thresholds(state, &attack, &defense);
  double random = qa_game_ai_context_random(ctx);
  const qa_game_ai_deathmatch_state_bots_a *goals = &ctx->deathmatch;
  if (random < attack && goals->red_obelisk.area != 0 && goals->blue_obelisk.area != 0) {
    decide_for_self(state);
    qa_bot_go_harvest_bots_a(ctx, state);
  } else if (random < defense && goals->red_obelisk.area != 0 && goals->blue_obelisk.area != 0) {
    defend(ctx, state,
           qa_bot_team_bots_a(ctx, state) == TEAM_RED ? &goals->red_obelisk : &goals->blue_obelisk);
  } else
    roam(ctx, state);
}

void qa_bot_harvester_retreat_goals_bots_a(qa_game_ai_context_bots_a *ctx,
                                           qa_bot_state_bots_a *state) {
  if (qa_bot_harvester_carrying_cubes_bots_a(ctx, state) && state->src->ltg_type != QA_LTG_RUSHBASE) {
    rush_base(ctx, state);
    qa_bot_set_team_status_bots_a(ctx, state);
  }
}

void qa_bot_team_goals_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                              bool retreat) {
  int64_t gt = ctx->deathmatch.gametype;
  if (gt == GT_CTF) {
    if (retreat)
      qa_bot_ctf_retreat_goals_bots_a(ctx, state);
    else
      qa_bot_ctf_seek_goals_bots_a(ctx, state);
  } else if (state_missionpack(state)) {
    switch (gt) {
    case GT_1FCTF:
      if (retreat)
        qa_bot_1fctf_retreat_goals_bots_a(ctx, state);
      else
        qa_bot_1fctf_seek_goals_bots_a(ctx, state);
      break;
    case GT_OBELISK:
      if (retreat)
        qa_bot_obelisk_retreat_goals_bots_a(ctx, state);
      else
        qa_bot_obelisk_seek_goals_bots_a(ctx, state);
      break;
    case GT_HARVESTER:
      if (retreat)
        qa_bot_harvester_retreat_goals_bots_a(ctx, state);
      else
        qa_bot_harvester_seek_goals_bots_a(ctx, state);
      break;
    default:
      break;
    }
  }
  state->src->order_time = 0;
}
