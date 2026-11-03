/*
 * See header. Game bot lifecycle and scheduling translated from id Software's
 * game/ai_main.c. Copyright (C) 1999-2005 Id Software, Inc.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qa/bots/behavior/q3/ai_main.h"
#include "ai_orders_internal.h"
#include "qa/bots/behavior/library/actions.h"
#include "qa/bots/behavior/q3/ai_chat.h"
#include "qa/bots/behavior/q3/ai_combat.h"
#include "qa/bots/behavior/q3/ai_definitions.h"
#include "qa/bots/behavior/q3/ai_input.h"
#include "qa/bots/behavior/q3/ai_navigation.h"
#include "qa/bots/behavior/q3/ai_orders.h"
#include "qa/bots/behavior/q3/ai_voice.h"
#include "qa/content/q3/base/game/format.h"
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/content/q3/team_arena/client_admission.h"
#include "qa/core/game_numeric.h"
#include "qa/foundation/support.h"
#include "qa/persistence/value.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/js.h"



/* asciiEqual over ASCII case only: the shared fold. */
static bool ascii_equal(const char *first, const char *second) {
  return qa_ai_fold_equal_bots_a(first, second);
}

static const char *leader_text(const qa_bot_state_bots_a *state) {
  return qa_bot_state_text(state, state->src->team_leader, "teamLeader");
}

/** Q_IsColorString skips any non-NUL byte after ^ except another ^. */
void qa_remove_color_escape_sequences_bots_a(const char *t, char *out, size_t cap) {
  if (cap == 0)
    return;
  size_t w = 0;
  for (size_t i = 0; t[i] && w + 1 < cap; i++) {
    int ch = (uint8_t)t[i];
    int next = t[i + 1] ? (uint8_t)t[i + 1] : 0;
    if (ch == 94 && next != 0 && next != 94) {
      i++;
      continue;
    }
    /* The VM's signed char values above 127 pass this source comparison. */
    if (ch < 128 && ch > 0x7e)
      continue;
    out[w++] = t[i];
  }
  out[w] = '\0';
}

static char *format_text(const char *format, const qa_q3_game_format_arg *args, size_t count,
                         double max_bytes) {
  char *text = qa_q3_game_format(format, args, count, max_bytes);
  if (text == NULL)
    qa_range_error("Bot format allocation failed");
  return text;
}

/* out holds 32 bytes. */
static const char *carrying(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state, bool colored,
                            char *out) {
  int64_t team = qa_bot_team_bots_a(ctx, state), gt = ctx->deathmatch.gametype;
  const char *prefix = !colored ? "" : team == QA_Q3_BASE_TEAM_RED ? "^1" : "^4";
  if (gt == QA_Q3_BASE_GT_CTF && qa_bot_ctf_carrying_flag_bots_a(ctx, state) != 0) {
    snprintf(out, 32, "%sF ", prefix);
    return out;
  }
  if (qa_ai_missionpack_bots_a(ctx)) {
    if (gt == QA_Q3_BASE_GT_1FCTF && qa_bot_1fctf_carrying_flag_bots_a(ctx, state)) {
      snprintf(out, 32, "%sF ", prefix);
      return out;
    }
    if (gt == QA_Q3_BASE_GT_HARVESTER && qa_bot_harvester_carrying_cubes_bots_a(ctx, state)) {
      int32_t cubes =
          state->src->inventory[team == QA_Q3_BASE_TEAM_RED ? QA_INV_REDCUBE : QA_INV_BLUECUBE];
      qa_q3_game_format_arg args[] = {qa_q3_game_format_string(prefix),
                                      qa_q3_game_format_number(cubes)};
      char *text = format_text("%s%2d", args, 2, 32);
      snprintf(out, 32, "%s", text);
      free(text);
      return out;
    }
  }
  return "  ";
}

/* out holds size bytes. */
static const char *action_description(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                      bool with_goal, char *out, size_t size) {
  char name[256];
  qa_bot_goal_library *goals = &ctx->library->goals;
  switch (state->src->ltg_type) {
  case QA_LTG_TEAMHELP:
    snprintf(out, size, "helping %s",
             qa_easy_client_name_bots_a(ctx, state->src->teammate, 256, name));
    return out;
  case QA_LTG_TEAMACCOMPANY:
    snprintf(out, size, "accompanying %s",
             qa_easy_client_name_bots_a(ctx, state->src->teammate, 256, name));
    return out;
  case QA_LTG_DEFENDKEYAREA:
    snprintf(out, size, "defending %s", qa_bot_goal_goal_name(goals, state->src->team_goal.number));
    return out;
  case QA_LTG_GETITEM:
    snprintf(out, size, "getting item %s",
             qa_bot_goal_goal_name(goals, state->src->team_goal.number));
    return out;
  case QA_LTG_KILL:
    snprintf(out, size, "killing %s",
             qa_client_name_bots_a(ctx, state->src->team_goal.entity, 256, name));
    return out;
  case QA_LTG_CAMP:
  case QA_LTG_CAMPORDER:
    return "camping";
  case QA_LTG_PATROL:
    return "patrolling";
  case QA_LTG_GETFLAG:
    return "capturing flag";
  case QA_LTG_RUSHBASE:
    return "rushing base";
  case QA_LTG_RETURNFLAG:
    return "returning flag";
  case QA_LTG_ATTACKENEMYBASE:
    return "attacking the enemy base";
  case QA_LTG_HARVEST:
    return "harvesting";
  default: {
    if (!with_goal)
      return "roaming";
    qa_bot_goal goal;
    /* The source prints whatever its uninitialized goal names; an empty stack reports an empty name. */
    if (!qa_bot_goal_get_top_goal(goals, state->src->gs, &goal))
      snprintf(out, size, "roaming ");
    else
      snprintf(out, size, "roaming %s", qa_bot_goal_goal_name(goals, goal.number));
    return out;
  }
  }
}

void qa_bot_report_status_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  char name[256], carried[32], action[512];
  qa_client_name_bots_a(ctx, state->src->client, 256, name);
  const char *leader = ascii_equal(name, leader_text(state)) ? "L" : " ";
  const char *carried_text = carrying(ctx, state, true, carried);
  const char *action_text = action_description(ctx, state, false, action, sizeof action);
  qa_q3_game_format_arg args[] = {qa_q3_game_format_string(name), qa_q3_game_format_string(leader),
                                  qa_q3_game_format_string(carried_text),
                                  qa_q3_game_format_string(action_text)};
  char *text = format_text("%-20s%s%s: %s\n", args, 4, 2048);
  qa_ai_print_bots_a(ctx, text);
  free(text);
}

/* clientInfoValue(info, key) into out[QA_AI_INFO_SIZE]. */
static char *client_info_value(const char *info, const char *key, char *out) {
  char *value = qa_q3_ta_client_info_value(info, key);
  if (value == NULL)
    qa_range_error("Bot client info allocation failed");
  snprintf(out, QA_AI_INFO_SIZE, "%s", value);
  free(value);
  return out;
}

void qa_bot_teamplay_report_bots_a(qa_game_ai_context_bots_a *ctx) {
  static const int teams[] = {QA_Q3_BASE_TEAM_RED, QA_Q3_BASE_TEAM_BLUE};
  for (size_t t = 0; t < 2; t++) {
    qa_ai_print_bots_a(ctx, teams[t] == QA_Q3_BASE_TEAM_RED ? "^1RED\n" : "^4BLUE\n");
    for (int64_t index = 0; index < ctx->deathmatch.maxclients && index < QA_AI_MAX_CLIENTS;
         index++) {
      qa_bot_state_bots_a *state = qa_bot_state_store_get(&ctx->states, index);
      if (state == NULL || !state->src->inuse)
        continue;
      char info[QA_AI_INFO_SIZE], name[QA_AI_INFO_SIZE], team[QA_AI_INFO_SIZE];
      qa_ai_configstring_bots_a(ctx, QA_AI_CS_PLAYERS + index, info);
      if (info[0] != '\0' && client_info_value(info, "n", name)[0] != '\0' &&
          qa_game_atoi(client_info_value(info, "t", team)) == teams[t])
        qa_bot_report_status_bots_a(ctx, state);
    }
  }
}

void qa_bot_test_aas_bots_a(qa_game_ai_context_bots_a *ctx, qa_vec3 origin) {
  qa_bot_cvar_bots_a *solid = qa_game_ai_context_cvar(ctx, "bot_testsolid");
  qa_bot_cvar_bots_a *clusters = qa_game_ai_context_cvar(ctx, "bot_testclusters");
  qa_bot_cvar_update_bots_a(solid);
  qa_bot_cvar_update_bots_a(clusters);
  if (solid->integer_value != 0) {
    if (!qa_ai_navigation_ready_bots_a(ctx))
      return;
    qa_ai_print_bots_a(ctx, qa_bot_point_area_num_bots_a(ctx, origin) != 0 ? "\remtpy area"
                                                                           : "\r^1SOLID area");
  } else if (clusters->integer_value != 0) {
    if (!qa_ai_navigation_ready_bots_a(ctx))
      return;
    int64_t area = qa_bot_point_area_num_bots_a(ctx, origin);
    if (area == 0)
      qa_ai_print_bots_a(ctx, "\r^1Solid!                              ");
    else {
      if (!qa_ai_navigation_ready_bots_a(ctx))
        qa_range_error("BotTestAAS requires the initialized AAS world");
      qa_bot_navigation_bots_a *navigation = qa_game_ai_context_navigation(ctx);
      qa_bot_navigation_area settings = navigation->get_area(navigation->ctx, area);
      qa_q3_game_format_arg args[] = {qa_q3_game_format_number((double)area),
                                      qa_q3_game_format_number((double)settings.cluster)};
      char *text = format_text("\rarea %d, cluster %d       ", args, 2, 2048);
      qa_ai_print_bots_a(ctx, text);
      free(text);
    }
  }
}

void qa_bot_update_info_config_strings_bots_a(qa_game_ai_context_bots_a *ctx) {
  for (int64_t index = 0; index < ctx->deathmatch.maxclients && index < QA_AI_MAX_CLIENTS; index++) {
    qa_bot_state_bots_a *state = qa_bot_state_store_get(&ctx->states, index);
    if (state == NULL || !state->src->inuse)
      continue;
    char info[QA_AI_INFO_SIZE], name[QA_AI_INFO_SIZE];
    qa_ai_configstring_bots_a(ctx, QA_AI_CS_PLAYERS + index, info);
    if (info[0] == '\0' || client_info_value(info, "n", name)[0] == '\0')
      continue;
    char own[256], carried[32], action[512], text[1024];
    qa_client_name_bots_a(ctx, state->src->client, 256, own);
    const char *leader = ascii_equal(own, leader_text(state)) ? "L" : " ";
    const char *carried_text = carrying(ctx, state, false, carried);
    const char *action_text = action_description(ctx, state, true, action, sizeof action);
    snprintf(text, sizeof text, "l\\%s\\c\\%s\\a\\%.255s", leader, carried_text, action_text);
    qa_q3_config_string_store *store = ctx->game->options.configstrings;
    store->set(store->ctx, (double)(QA_AI_CS_BOTINFO + state->src->client), text);
  }
}

bool qa_bot_team_leader_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  int64_t client = qa_client_from_name_bots_a(ctx, leader_text(state));
  if (client < 0)
    return false;
  qa_bot_state_bots_a *leader = qa_bot_state_store_get(&ctx->states, client);
  return leader != NULL && leader->src->inuse;
}

static int32_t read_session_integer(const char *text, size_t length, size_t *cursor) {
  size_t index = *cursor;
  if (index > length)
    qa_range_error("Bot session scan reads past its source string");
  while (index < length) {
    int32_t signed_byte = (int8_t)text[index];
    if (signed_byte > 32)
      break;
    index++;
  }
  if (index == length)
    return 0;
  int32_t sign = 1;
  if (text[index] == '-' || text[index] == '+') {
    if (text[index] == '-')
      sign = -1;
    index++;
  }
  uint32_t value = 0;
  for (;;) {
    if (index > length)
      qa_range_error("Bot session integer scan reads past its source string");
    uint8_t byte = index == length ? 0 : (uint8_t)text[index];
    index++;
    if (byte < 48 || byte > 57)
      break;
    value = value * 10u + (uint32_t)(byte - 48);
  }
  *cursor = index;
  return (int32_t)(value * (uint32_t)sign);
}

/* cvars.find(name)?.value ?? "" into out[size]. */
static char *registry_value(qa_game_ai_context_bots_a *ctx, const char *name, char *out, size_t size) {
  qa_cvar_read read;
  out[0] = '\0';
  if (qa_cvar_registry_find(ctx->game->options.cvars, name, &read)) {
    snprintf(out, size, "%s", read.value);
    qa_cvar_read_free(&read);
  }
  return out;
}

static void registry_set(qa_game_ai_context_bots_a *ctx, const char *name, const char *value) {
  qa_cvar_snapshot snapshot;
  if (qa_cvar_registry_set(ctx->game->options.cvars, name, value, true, &snapshot))
    qa_cvar_read_free(&snapshot);
}

static float session_float(const char *text, size_t length, size_t *cursor) {
  /* TS scanGameFloat clamps its offset to the string length. */
  qa_game_float_scan scan = qa_scan_game_float(text, *cursor < length ? *cursor : length);
  *cursor = scan.next_offset;
  return scan.value;
}

static void read_session(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  char name[32], text[QA_AI_INFO_SIZE];
  snprintf(name, sizeof name, "botsession%d", state->src->client);
  registry_value(ctx, name, text, sizeof text);
  size_t length = strlen(text), cursor = 0;
  qa_bot_goal_state_bots_a *goal = &state->src->last_goal_team_goal;
  state->src->last_goal_decisionmaker = read_session_integer(text, length, &cursor);
  state->src->last_goal_ltg_type = read_session_integer(text, length, &cursor);
  state->src->last_goal_teammate = read_session_integer(text, length, &cursor);
  goal->area = read_session_integer(text, length, &cursor);
  goal->entity = read_session_integer(text, length, &cursor);
  goal->flags = read_session_integer(text, length, &cursor);
  goal->item_info = read_session_integer(text, length, &cursor);
  goal->number = read_session_integer(text, length, &cursor);
  qa_bot_fvec3 *vectors[] = {&goal->origin, &goal->mins, &goal->maxs};
  for (size_t i = 0; i < 3; i++) {
    vectors[i]->x = session_float(text, length, &cursor);
    vectors[i]->y = session_float(text, length, &cursor);
    vectors[i]->z = session_float(text, length, &cursor);
  }
  if (qa_is_source_goal_number(goal->number)) {
    qa_bot_goal_state_clear(goal);
    state->src->last_goal_ltg_type = QA_LTG_NONE;
    state->src->last_goal_decisionmaker = 0;
    state->src->last_goal_teammate = 0;
  }
}

static void write_session(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  const qa_bot_goal_state_bots_a *goal = &state->src->last_goal_team_goal;
  qa_q3_game_format_arg args[] = {
      qa_q3_game_format_number(state->src->last_goal_decisionmaker),
      qa_q3_game_format_number(state->src->last_goal_ltg_type),
      qa_q3_game_format_number(state->src->last_goal_teammate),
      qa_q3_game_format_number(goal->area),
      qa_q3_game_format_number(goal->entity),
      qa_q3_game_format_number(goal->flags),
      qa_q3_game_format_number(goal->item_info),
      qa_q3_game_format_number(goal->number),
      qa_q3_game_format_number(goal->origin.x),
      qa_q3_game_format_number(goal->origin.y),
      qa_q3_game_format_number(goal->origin.z),
      qa_q3_game_format_number(goal->mins.x),
      qa_q3_game_format_number(goal->mins.y),
      qa_q3_game_format_number(goal->mins.z),
      qa_q3_game_format_number(goal->maxs.x),
      qa_q3_game_format_number(goal->maxs.y),
      qa_q3_game_format_number(goal->maxs.z),
  };
  char *text = format_text("%i %i %i %i %i %i %i %i %f %f %f %f %f %f %f %f %f", args, 17, 32000);
  char name[32];
  snprintf(name, sizeof name, "botsession%d", state->src->client);
  registry_set(ctx, name, text);
  free(text);
}

bool qa_game_ai_bots_a_init(qa_game_ai_bots_a *g, qa_source_bot_game_bots_a *game,
                            qa_bot_library_bots_a *library, const qa_game_ai_host_bots_a *host) {
  g->local_time = 0;
  g->botlib_residual = 0;
  g->last_bot_think_time = 0;
  return qa_game_ai_context_bots_a_init(&g->context, game, library, host);
}

void qa_game_ai_bots_a_destroy(qa_game_ai_bots_a *g) { qa_game_ai_context_bots_a_destroy(&g->context); }

qa_save_value *qa_game_ai_capture_save_state_bots_a(qa_game_ai_bots_a *g) {
  qa_save_value *object = qa_save_value_object();
  if (object == NULL || !qa_save_value_set(object, "localTime", qa_save_value_number(g->local_time)) ||
      !qa_save_value_set(object, "botlibResidual", qa_save_value_number(g->botlib_residual)) ||
      !qa_save_value_set(object, "lastBotThinkTime", qa_save_value_number(g->last_bot_think_time)) ||
      !qa_save_value_set(object, "context", qa_game_ai_context_capture_save_state(&g->context)))
    qa_range_error("Out of memory capturing bot AI state");
  return object;
}

static double reader_number(const qa_save_reader *reader, const char *name) {
  qa_save_reader cell;
  if (!qa_save_reader_field(reader, name, &cell))
    qa_range_error("Out of memory reading bot AI checkpoint");
  double value = qa_save_reader_number(&cell);
  qa_save_reader_destroy(&cell);
  return value;
}

void qa_game_ai_restore_save_state_bots_a(qa_game_ai_bots_a *g, const qa_save_value *value,
                                          qa_bot_remap_observation_fn remap, void *remap_ctx) {
  qa_save_reader reader, context;
  if (!qa_save_reader_init(&reader, value, "bot.ai"))
    qa_range_error("Out of memory reading bot AI checkpoint");
  /* TS keeps these as numbers. localTime and botlibResidual are only used through
   * `| 0`, so ToInt32 here matches; lastBotThinkTime is compared with the int32
   * bot_thinktime cvar, which differs only for a save holding a non-int32 value.
   * Either way no save can reach an undefined cast. */
  g->local_time = qa_js_to_int32(reader_number(&reader, "localTime"));
  g->botlib_residual = qa_js_to_int32(reader_number(&reader, "botlibResidual"));
  g->last_bot_think_time = qa_js_to_int32(reader_number(&reader, "lastBotThinkTime"));
  if (!qa_save_reader_field(&reader, "context", &context))
    qa_range_error("Out of memory reading bot AI checkpoint");
  qa_game_ai_context_restore_save_state(&g->context, context.value, remap, remap_ctx);
  qa_save_reader_destroy(&context);
  qa_save_reader_destroy(&reader);
}

void qa_game_ai_test_aas_bots_a(qa_game_ai_bots_a *g, qa_vec3 origin) {
  qa_bot_test_aas_bots_a(&g->context, origin);
}

/* read(name): the registry value truncated to 143 bytes. */
static char *read143(qa_game_ai_context_bots_a *ctx, const char *name, char *out) {
  return registry_value(ctx, name, out, 144);
}

static int64_t initialize_library(qa_game_ai_bots_a *g) {
  qa_game_ai_context_bots_a *ctx = &g->context;
  qa_bot_library_bots_a *library = ctx->library;
  qa_bot_lib_vars *variables = &library->variables;
  char value[144], text[32];
  read143(ctx, "sv_maxclients", value);
  qa_bot_lib_vars_set(variables, "maxclients", value[0] != '\0' ? value : "8");
  snprintf(text, sizeof text, "%d", QA_Q3_MAX_GENTITIES);
  qa_bot_lib_vars_set(variables, "maxentities", text);
  static const char *const passthrough[] = {"sv_mapChecksum", "max_aaslinks", "max_levelitems"};
  for (size_t i = 0; i < 3; i++)
    if (read143(ctx, passthrough[i], value)[0] != '\0')
      qa_bot_lib_vars_set(variables, passthrough[i], value);
  char game_type[144];
  read143(ctx, "g_gametype", game_type);
  if (game_type[0] == '\0')
    strcpy(game_type, "0");
  qa_bot_lib_vars_set(variables, "g_gametype", game_type);
  qa_bot_lib_vars_set(variables, "bot_developer", qa_game_ai_context_cvar(ctx, "bot_developer")->value);
  qa_bot_lib_vars_set(variables, "log", game_type);
  if (read143(ctx, "bot_nochat", value)[0] != '\0')
    qa_bot_lib_vars_set(variables, "nochat", "0");
  static const char *const forwarding[][2] = {{"bot_visualizejumppads", "bot_visualizejumppads"},
                                              {"bot_forceclustering", "forceclustering"},
                                              {"bot_forcereachability", "forcereachability"},
                                              {"bot_forcewrite", "forcewrite"},
                                              {"bot_aasoptimize", "aasoptimize"},
                                              {"bot_saveroutingcache", "saveroutingcache"}};
  for (size_t i = 0; i < 6; i++)
    if (read143(ctx, forwarding[i][0], value)[0] != '\0')
      qa_bot_lib_vars_set(variables, forwarding[i][1], value);
  read143(ctx, "bot_reloadcharacters", value);
  qa_bot_lib_vars_set(variables, "bot_reloadcharacters", value[0] != '\0' ? value : "0");
  static const char *const directories[][2] = {
      {"fs_basepath", "basedir"}, {"fs_game", "gamedir"}, {"fs_cdpath", "cddir"}};
  for (size_t i = 0; i < 3; i++)
    if (read143(ctx, directories[i][0], value)[0] != '\0')
      qa_bot_lib_vars_set(variables, directories[i][1], value);
  if (qa_ai_missionpack_bots_a(ctx))
    qa_ui_script_global_defines_add(library->globals, "MISSIONPACK");
  return qa_bot_library_setup_bots_a(library);
}

bool qa_game_ai_setup_bots_a(qa_game_ai_bots_a *g, bool restart) {
  qa_game_ai_context_bots_a *ctx = &g->context;
  static const char *const setup_cvars[][2] = {
      {"bot_thinktime", "100"},   {"bot_memorydump", "0"},   {"bot_saveroutingcache", "0"},
      {"bot_pause", "0"},         {"bot_report", "0"},       {"bot_testsolid", "0"},
      {"bot_testclusters", "0"},  {"bot_developer", "0"}};
  for (size_t i = 0; i < 8; i++)
    qa_game_ai_context_register_cvar(ctx, setup_cvars[i][0], setup_cvars[i][1], QA_CVAR_FLAG_CHEAT);
  qa_game_ai_context_register_cvar(ctx, "bot_interbreedchar", "", 0);
  qa_game_ai_context_register_cvar(ctx, "bot_interbreedbots", "10", 0);
  qa_game_ai_context_register_cvar(ctx, "bot_interbreedcycle", "20", 0);
  qa_game_ai_context_register_cvar(ctx, "bot_interbreedwrite", "", 0);
  if (restart)
    return true;
  qa_bot_state_store_clear(&ctx->states);
  return initialize_library(g) == 0;
}

bool qa_game_ai_load_map_bots_a(qa_game_ai_bots_a *g, bool restart) {
  qa_game_ai_context_bots_a *ctx = &g->context;
  if (!restart) {
    qa_bot_cvar_bots_a map_name;
    qa_bot_cvar_bots_a_init(&map_name, ctx->game->options.cvars);
    qa_bot_cvar_register_bots_a(&map_name, "mapname", "",
                                QA_CVAR_FLAG_SERVER_INFO | QA_CVAR_FLAG_READ_ONLY);
    ctx->host->load_map(ctx->host->ctx, map_name.value);
  }
  for (int64_t index = 0; index < QA_AI_MAX_CLIENTS; index++) {
    qa_bot_state_bots_a *state = qa_bot_state_store_get(&ctx->states, index);
    if (state == NULL || !state->src->inuse)
      continue;
    qa_game_ai_context_reset_state(ctx, state);
    state->src->setup_count = 4;
  }
  qa_bot_setup_deathmatch_ai_bots_a(ctx);
  return true;
}

static void set_progress(qa_bot_state_bots_a *state, qa_bot_setup_kind_bots_a kind,
                         qa_bot_setup_stage_bots_a stage, int64_t error_code) {
  state->setup.kind = kind;
  state->setup.stage = stage;
  state->setup.error_code = error_code;
}

static void setting_up(qa_bot_state_bots_a *state, qa_bot_setup_stage_bots_a stage) {
  set_progress(state, QA_BOT_SETUP_SETTING_UP, stage, 0);
}

/* characters.string(...).slice(0, 143) into out[144]. */
static char *character_string143(qa_bot_library_bots_a *library, int64_t character, int64_t index,
                                 char *out) {
  snprintf(out, 144, "%s", qa_bot_character_string(&library->characters, character, index));
  return out;
}

/* TS private scheduleThink. */
static void schedule_think(qa_game_ai_bots_a *g) {
  qa_game_ai_context_bots_a *ctx = &g->context;
  int32_t bot_number = 0;
  for (int64_t index = 0; index < QA_AI_MAX_CLIENTS; index++) {
    qa_bot_state_bots_a *state = qa_bot_state_store_get(&ctx->states, index);
    if (state == NULL || !state->src->inuse)
      continue;
    if (ctx->num_bots == 0)
      qa_range_error("BotScheduleBotThink divides by zero with an active bot");
    int32_t think = (int32_t)qa_game_ai_context_cvar(ctx, "bot_thinktime")->integer_value;
    int32_t product = (int32_t)((uint32_t)think * (uint32_t)bot_number);
    state->src->bot_think_residual = (int32_t)trunc((double)product / ctx->num_bots);
    bot_number++;
  }
}

bool qa_game_ai_setup_client_bots_a(qa_game_ai_bots_a *g, int64_t client,
                                    const qa_bot_settings_bots_a *settings, bool restart) {
  qa_game_ai_context_bots_a *ctx = &g->context;
  qa_bot_library_bots_a *library = ctx->library;
  qa_bot_state_bots_a *state = qa_bot_state_store_acquire(&ctx->states, client, ctx->game->memory);
  if (state->src->inuse) {
    char text[80];
    snprintf(text, sizeof text, "^1Fatal: BotAISetupClient: client %lld already setup\n",
             (long long)client);
    qa_ai_print_bots_a(ctx, text);
    return false;
  }
  setting_up(state, QA_BOT_SETUP_ALLOCATED);
  if (!qa_ai_navigation_ready_bots_a(ctx)) {
    qa_ai_print_bots_a(ctx, "^1Fatal: AAS not initialized\n");
    set_progress(state, QA_BOT_SETUP_FAILED, QA_BOT_SETUP_AAS, 0);
    return false;
  }
  state->src->character =
      (int32_t)qa_bot_character_load(&library->characters, settings->characterfile, settings->skill);
  setting_up(state, QA_BOT_SETUP_CHARACTER);
  if (state->src->character == 0) {
    qa_q3_game_format_arg args[] = {qa_q3_game_format_number(settings->skill),
                                    qa_q3_game_format_string(settings->characterfile)};
    char *text = format_text("^1Fatal: couldn't load skill %f from %s\n", args, 2, 2048);
    qa_ai_print_bots_a(ctx, text);
    free(text);
    set_progress(state, QA_BOT_SETUP_FAILED, QA_BOT_SETUP_CHARACTER, 0);
    return false;
  }
  qa_bot_state_set_text(state->src->characterfile, sizeof state->src->characterfile,
                        settings->characterfile);
  state->src->skill = (float)settings->skill;
  qa_bot_state_set_text(state->src->team, sizeof state->src->team, settings->team);
  setting_up(state, QA_BOT_SETUP_SETTINGS);
  state->src->gs = (int32_t)qa_bot_goal_alloc_goal_state(&library->goals, client);
  setting_up(state, QA_BOT_SETUP_GOAL_STATE);
  char file[144];
  int64_t item_error = qa_bot_goal_load_item_weights(
      &library->goals, state->src->gs,
      character_string143(library, state->src->character, QA_CHAR_ITEMWEIGHTS, file));
  if (item_error != 0) {
    qa_bot_goal_free_goal_state(&library->goals, state->src->gs);
    set_progress(state, QA_BOT_SETUP_FAILED, QA_BOT_SETUP_ITEM_WEIGHTS, item_error);
    return false;
  }
  setting_up(state, QA_BOT_SETUP_ITEM_WEIGHTS);
  state->src->ws = (int32_t)qa_weapon_ai_allocate_state(&library->weapons);
  setting_up(state, QA_BOT_SETUP_WEAPON_STATE);
  int64_t weapon_error = qa_weapon_ai_load_weights(
      &library->weapons, state->src->ws,
      character_string143(library, state->src->character, QA_CHAR_WEAPONWEIGHTS, file));
  if (weapon_error != 0) {
    qa_bot_goal_free_goal_state(&library->goals, state->src->gs);
    qa_weapon_ai_free_state(&library->weapons, state->src->ws);
    set_progress(state, QA_BOT_SETUP_FAILED, QA_BOT_SETUP_WEAPON_WEIGHTS, weapon_error);
    return false;
  }
  setting_up(state, QA_BOT_SETUP_WEAPON_WEIGHTS);
  state->src->cs = (int32_t)qa_bot_chat_library_allocate(&library->chat);
  setting_up(state, QA_BOT_SETUP_CHAT_STATE);
  char chat_name[144];
  character_string143(library, state->src->character, QA_CHAR_CHAT_FILE, file);
  character_string143(library, state->src->character, QA_CHAR_CHAT_NAME, chat_name);
  if (!qa_bot_chat_library_load_chat_file(&library->chat, state->src->cs, qa_chat_text(file),
                                          qa_chat_text(chat_name))) {
    qa_bot_chat_library_free(&library->chat, state->src->cs);
    qa_bot_goal_free_goal_state(&library->goals, state->src->gs);
    qa_weapon_ai_free_state(&library->weapons, state->src->ws);
    set_progress(state, QA_BOT_SETUP_FAILED, QA_BOT_SETUP_CHAT_FILE, 8);
    return false;
  }
  setting_up(state, QA_BOT_SETUP_CHAT_FILE);
  char gender[144];
  character_string143(library, state->src->character, QA_CHAR_GENDER, gender);
  qa_bot_chat_library_set_gender(&library->chat, state->src->cs,
                                 gender[0] == 'f' || gender[0] == 'F'   ? QA_CHAT_FEMALE
                                 : gender[0] == 'm' || gender[0] == 'M' ? QA_CHAT_MALE
                                                                        : QA_CHAT_GENDERLESS);
  setting_up(state, QA_BOT_SETUP_CHAT_GENDER);
  state->src->inuse = true;
  state->src->client = (int32_t)client;
  state->src->entity_num = (int32_t)client;
  state->src->setup_count = 4;
  state->src->enter_game_time = (float)ctx->time;
  setting_up(state, QA_BOT_SETUP_PUBLISHED);
  state->src->ms = (int32_t)qa_bot_move_state_store_allocate(&library->move_states);
  setting_up(state, QA_BOT_SETUP_MOVE_STATE);
  state->src->walker =
      qa_bot_character_bounded_float(&library->characters, state->src->character, QA_CHAR_WALKER, 0, 1);
  setting_up(state, QA_BOT_SETUP_WALKER);
  ctx->num_bots = (int32_t)((int64_t)ctx->num_bots + 1);
  setting_up(state, QA_BOT_SETUP_COUNTED);
  qa_cvar_read testichat;
  int64_t testichat_value = 0;
  if (qa_cvar_registry_find(ctx->game->options.cvars, "bot_testichat", &testichat)) {
    testichat_value = (int64_t)testichat.integer_value;
    qa_cvar_read_free(&testichat);
  }
  if (testichat_value != 0) {
    qa_bot_lib_vars_set(&library->variables, "bot_testichat", "1");
    qa_bot_chat_test_bots_a(ctx, state);
  }
  schedule_think(g);
  setting_up(state, QA_BOT_SETUP_SCHEDULED);
  if (ctx->interbreed)
    qa_bot_goal_mutate_goal_fuzzy_logic(&library->goals, state->src->gs, 1);
  setting_up(state, QA_BOT_SETUP_INTERBRED);
  if (restart)
    read_session(ctx, state);
  set_progress(state, QA_BOT_SETUP_COMPLETE, QA_BOT_SETUP_ALLOCATED, 0);
  return true;
}

bool qa_game_ai_shutdown_client_bots_a(qa_game_ai_bots_a *g, int64_t client, bool restart) {
  qa_game_ai_context_bots_a *ctx = &g->context;
  qa_bot_library_bots_a *library = ctx->library;
  qa_bot_state_bots_a *state = qa_bot_state_store_get(&ctx->states, client);
  if (state == NULL || !state->src->inuse)
    return false;
  if (restart)
    write_session(ctx, state);
  if (qa_bot_chat_exit_game_bots_a(ctx, state))
    qa_bot_chat_library_enter_chat(&library->chat, state->src->cs, state->src->client, QA_CHAT_ALL,
                                   false, 0);
  qa_bot_move_state_store_free(&library->move_states, state->src->ms);
  qa_bot_goal_free_goal_state(&library->goals, state->src->gs);
  qa_bot_chat_library_free(&library->chat, state->src->cs);
  qa_weapon_ai_free_state(&library->weapons, state->src->ws);
  qa_bot_character_free(&library->characters, state->src->character);
  qa_bot_free_waypoints_bots_a(ctx, state->checkpoints);
  qa_bot_free_waypoints_bots_a(ctx, state->patrol_points);
  qa_bot_clear_activate_goal_stack_bots_a(ctx, state);
  qa_bot_state_clear(state);
  ctx->num_bots = (int32_t)((int64_t)ctx->num_bots - 1);
  return true;
}

bool qa_game_ai_shutdown_bots_a(qa_game_ai_bots_a *g, bool restart) {
  if (restart) {
    for (int64_t index = 0; index < QA_AI_MAX_CLIENTS; index++) {
      qa_bot_state_bots_a *state = qa_bot_state_store_get(&g->context.states, index);
      if (state != NULL && state->src->inuse)
        qa_game_ai_shutdown_client_bots_a(g, state->src->client, restart);
    }
  } else
    qa_bot_library_shutdown_bots_a(g->context.library);
  return true;
}

/* The console-command handling of TS ai(), one message. */
static void bot_console_message(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                const char *command) {
  char buffer[QA_AI_INFO_SIZE];
  snprintf(buffer, sizeof buffer, "%s", command);
  char *split = strchr(buffer, ' ');
  if (split == NULL)
    return;
  *split = '\0';
  const char *verb = buffer;
  char arguments[QA_AI_INFO_SIZE];
  qa_remove_color_escape_sequences_bots_a(split + 1, arguments, sizeof arguments);
  if (ascii_equal(verb, "print") || ascii_equal(verb, "chat") || ascii_equal(verb, "tchat")) {
    size_t length = strlen(arguments);
    if (length < 2)
      return; /* TS `continue`: the next queued command is handled by the caller's loop */
    arguments[length - 1] = '\0';
    qa_bot_chat_library_queue_console_message(&ctx->library->chat, state->src->cs,
                                              ascii_equal(verb, "print") ? 0 : 1,
                                              qa_chat_text(arguments + 1));
  } else if (qa_ai_missionpack_bots_a(ctx)) {
    if (ascii_equal(verb, "vchat"))
      qa_bot_voice_chat_command_bots_a(ctx, state, 0, arguments);
    else if (ascii_equal(verb, "vtchat"))
      qa_bot_voice_chat_command_bots_a(ctx, state, 1, arguments);
    else if (ascii_equal(verb, "vtell"))
      qa_bot_voice_chat_command_bots_a(ctx, state, 2, arguments);
  }
}

/* TS private ai(). */
static bool bot_ai(qa_game_ai_context_bots_a *ctx, int64_t client, double think_time) {
  qa_bot_library_bots_a *library = ctx->library;
  qa_bot_action_reset_input(&library->actions, client);
  qa_bot_state_bots_a *state = qa_bot_state_store_get(&ctx->states, client);
  if (state == NULL || !state->src->inuse) {
    char text[64];
    snprintf(text, sizeof text, "^1Fatal: BotAI: client %lld is not setup\n", (long long)client);
    qa_ai_print_bots_a(ctx, text);
    return false;
  }
  qa_bot_observed_entity_bots_a entity = ctx->game->entity(ctx->game->ctx, client);
  if (entity.present && entity.has_player)
    qa_copy_bot_player_state_bots_a(state, entity.player_state);
  for (;;) {
    const char *command = ctx->host->get_console_message(ctx->host->ctx, client);
    if (command == NULL)
      break;
    bot_console_message(ctx, state, command);
  }
  qa_bot_add_delta_angles(state);
  state->src->ltime = (float)(state->src->ltime + think_time);
  state->src->think_time = (float)think_time;
  state->src->origin = state->src->cur_ps.origin;
  state->src->eye = state->src->cur_ps.origin;
  state->src->eye.z = (float)((double)state->src->eye.z + (double)(float)state->src->cur_ps.viewheight);
  state->src->area_num =
      (int32_t)qa_bot_point_area_num_bots_a(ctx, qa_bot_v3(state->src->origin));
  qa_bot_deathmatch_ai_bots_a(ctx, state, think_time);
  qa_bot_action_select_weapon(&library->actions, state->src->client, state->src->weapon_num);
  qa_bot_subtract_delta_angles(state);
  return true;
}

static bool client_connected(qa_game_ai_context_bots_a *ctx, int64_t client) {
  qa_bot_observed_entity_bots_a entity = ctx->game->entity(ctx->game->ctx, client);
  if (!entity.has_player)
    qa_range_error("BotAIStartFrame reads a null game client");
  return entity.player_connected;
}

static void send_user_command(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  const qa_bot_user_command_source *source = &state->src->last_ucmd;
  qa_q3_base_user_command command = {.server_time = source->server_time,
                                     .angles = {source->angles.x, source->angles.y, source->angles.z},
                                     .buttons = source->buttons,
                                     .weapon = source->weapon,
                                     .forwardmove = source->forwardmove,
                                     .rightmove = source->rightmove,
                                     .upmove = source->upmove};
  ctx->host->user_command(ctx->host->ctx, state->src->client, &command);
}

/* TS entityObservation: false is null. */
static bool entity_observation(qa_game_ai_context_bots_a *ctx, int64_t index,
                               qa_bot_entity_update *out) {
  if (!ctx->game->observable(ctx->game->ctx, index))
    return false;
  qa_bot_observed_entity_bots_a entity = ctx->game->entity(ctx->game->ctx, index);
  const qa_q3_base_entity_state *source = &entity.state;
  if (!entity.present || !entity.linked || entity.hidden)
    return false;
  if (source->e_type == QA_Q3_BASE_ET_MISSILE && source->weapon != QA_Q3_WP_GRAPPLING_HOOK)
    return false;
  if (source->e_type > QA_Q3_BASE_ET_EVENTS)
    return false;
  if (qa_ai_missionpack_bots_a(ctx) && entity.contents == 0x40000000 && entity.proximity_trigger)
    return false;
  memset(out, 0, sizeof *out);
  out->has_generation = true;
  out->generation = entity.generation;
  out->type = source->e_type;
  out->flags = source->e_flags;
  out->origin = entity.origin;
  out->angles = index < QA_AI_MAX_CLIENTS ? source->apos.base : entity.angles;
  out->old_origin = source->origin2;
  out->mins = entity.bounds.min;
  out->maxs = entity.bounds.max;
  out->ground_entity = source->ground_entity_num;
  out->solid = entity.has_inline_model ? 3 : 2;
  out->model_index = source->modelindex;
  out->model_index2 = source->modelindex2;
  out->frame = source->frame;
  out->event = source->event;
  out->event_parameter = source->event_parm;
  out->powerups = source->powerups;
  out->weapon = source->weapon;
  out->legs_animation = source->legs_anim;
  out->torso_animation = source->torso_anim;
  return true;
}

/* TS private interbreeding. */
static void interbreeding(qa_game_ai_bots_a *g) {
  qa_game_ai_context_bots_a *ctx = &g->context;
  qa_bot_cvar_bots_a *variable = qa_game_ai_context_cvar(ctx, "bot_interbreedchar");
  qa_bot_cvar_update_bots_a(variable);
  if (variable->value[0] == '\0')
    return;
  if (ctx->deathmatch.gametype != QA_Q3_BASE_GT_TOURNAMENT) {
    char text[16];
    snprintf(text, sizeof text, "%d", QA_Q3_BASE_GT_TOURNAMENT);
    registry_set(ctx, "g_gametype", text);
    ctx->game->exit_level(ctx->game->ctx);
    return;
  }
  for (int64_t index = 0; index < QA_AI_MAX_CLIENTS; index++) {
    qa_bot_state_bots_a *state = qa_bot_state_store_get(&ctx->states, index);
    if (state != NULL && state->src->inuse)
      qa_game_ai_shutdown_client_bots_a(g, state->src->client, false);
  }
  qa_bot_lib_vars_set(&ctx->library->variables, "bot_reloadcharacters", "1");
  for (int64_t index = 0; index < qa_game_ai_context_cvar(ctx, "bot_interbreedbots")->integer_value;
       index++) {
    int32_t skill_offset = (int32_t)((uint32_t)index * 50u);
    qa_q3_game_format_arg args[] = {qa_q3_game_format_string(variable->value),
                                    qa_q3_game_format_number(skill_offset),
                                    qa_q3_game_format_string(variable->value),
                                    qa_q3_game_format_number((double)index)};
    char *text = format_text("addbot %s 4 free %i %s%d\n", args, 4, 32000);
    ctx->host->insert_console_command(ctx->host->ctx, text);
    free(text);
  }
  registry_set(ctx, "bot_interbreedchar", "");
  ctx->interbreed = true;
}

typedef struct think_call {
  qa_game_ai_context_bots_a *ctx;
  int64_t client;
  double think_time;
} think_call;

static void think_leaf(void *context) {
  think_call *call = context;
  bot_ai(call->ctx, call->client, call->think_time);
}

bool qa_game_ai_start_frame_bots_a(qa_game_ai_bots_a *g, double time) {
  if (time != floor(time) || time < -2147483648.0 || time > 2147483647.0)
    qa_range_error("Bot frame time must be signed 32-bit milliseconds");
  qa_game_ai_context_bots_a *ctx = &g->context;
  qa_bot_library_bots_a *library = ctx->library;
  int32_t now = (int32_t)time;
  ctx->host->check_bot_spawn(ctx->host->ctx);
  static const char *const frame_cvars[] = {"bot_rocketjump",  "bot_grapple",    "bot_fastchat",
                                            "bot_nochat",      "bot_testrchat",  "bot_thinktime",
                                            "bot_memorydump",  "bot_saveroutingcache",
                                            "bot_pause",       "bot_report"};
  for (size_t i = 0; i < sizeof frame_cvars / sizeof frame_cvars[0]; i++)
    qa_bot_cvar_update_bots_a(qa_game_ai_context_cvar(ctx, frame_cvars[i]));
  if (qa_game_ai_context_cvar(ctx, "bot_report")->integer_value != 0)
    qa_bot_update_info_config_strings_bots_a(ctx);
  if (qa_game_ai_context_cvar(ctx, "bot_pause")->integer_value != 0) {
    for (int64_t index = 0; index < QA_AI_MAX_CLIENTS; index++) {
      qa_bot_state_bots_a *state = qa_bot_state_store_get(&ctx->states, index);
      if (state == NULL || !state->src->inuse || !client_connected(ctx, index))
        continue;
      state->src->last_ucmd.forwardmove = 0;
      state->src->last_ucmd.rightmove = 0;
      state->src->last_ucmd.upmove = 0;
      state->src->last_ucmd.buttons = 0;
      state->src->last_ucmd.server_time = now;
      send_user_command(ctx, state);
    }
    return true;
  }
  static const char *const once[] = {"bot_memorydump", "bot_saveroutingcache"};
  for (size_t i = 0; i < 2; i++) {
    if (qa_game_ai_context_cvar(ctx, once[i])->integer_value == 0)
      continue;
    if (i == 1)
      ctx->game->options.engine.print(ctx->game->options.engine.ctx,
                                      "bot_saveroutingcache: routing cache files are not supported by this build\n");
    qa_bot_lib_vars_set(&library->variables, once[i] + 4, "1");
    registry_set(ctx, once[i], "0");
  }
  interbreeding(g);
  int64_t bot_think_time = qa_game_ai_context_cvar(ctx, "bot_thinktime")->integer_value;
  if (bot_think_time > 200)
    registry_set(ctx, "bot_thinktime", "200");
  if (bot_think_time != g->last_bot_think_time) {
    g->last_bot_think_time = (int32_t)bot_think_time;
    schedule_think(g);
  }
  int32_t elapsed = (int32_t)((uint32_t)now - (uint32_t)g->local_time);
  g->local_time = now;
  g->botlib_residual = (int32_t)((uint32_t)g->botlib_residual + (uint32_t)elapsed);
  int64_t think_time = elapsed > bot_think_time ? elapsed : bot_think_time;
  if (g->botlib_residual >= think_time) {
    g->botlib_residual = (int32_t)((int64_t)g->botlib_residual - think_time);
    qa_bot_library_start_frame_bots_a(library, (float)((double)(float)time / 1000));
    if (!qa_ai_navigation_ready_bots_a(ctx))
      return false;
    qa_bot_entity_observations_invalidate(&ctx->observations);
    for (int64_t index = 0; index < QA_Q3_MAX_GENTITIES; index++) {
      qa_bot_entity_update update;
      bool observed = entity_observation(ctx, index, &update);
      qa_bot_entity_observations_update(&ctx->observations, index, observed ? &update : NULL,
                                        qa_bot_library_time_bots_a(library));
    }
    if (ctx->regular_update_time < ctx->time) {
      qa_bot_goal_update_entity_items(&library->goals);
      ctx->regular_update_time = (float)(ctx->time + (double)(float)0.3);
    }
  }
  ctx->time = qa_bot_library_time_bots_a(library);
  for (int64_t index = 0; index < QA_AI_MAX_CLIENTS; index++) {
    qa_bot_state_bots_a *state = qa_bot_state_store_get(&ctx->states, index);
    if (state == NULL || !state->src->inuse)
      continue;
    state->src->bot_think_residual =
        (int32_t)((uint32_t)state->src->bot_think_residual + (uint32_t)elapsed);
    if (state->src->bot_think_residual < think_time)
      continue;
    state->src->bot_think_residual = (int32_t)((int64_t)state->src->bot_think_residual - think_time);
    if (!qa_ai_navigation_ready_bots_a(ctx))
      return false;
    if (client_connected(ctx, index)) {
      qa_bot_navigation_bots_a *navigation = qa_game_ai_context_navigation(ctx);
      think_call call = {ctx, index, (float)((double)(float)think_time / 1000)};
      navigation->with_client(navigation->ctx, index, think_leaf, &call);
    }
  }
  for (int64_t index = 0; index < QA_AI_MAX_CLIENTS; index++) {
    qa_bot_state_bots_a *state = qa_bot_state_store_get(&ctx->states, index);
    if (state == NULL || !state->src->inuse || !client_connected(ctx, index))
      continue;
    qa_bot_update_input_bots_a(ctx, state, now, elapsed);
    send_user_command(ctx, state);
  }
  return true;
}

static double interbreed_rank(const qa_bot_state_bots_a *state) {
  if (state == NULL || !state->src->inuse)
    return -1;
  int32_t doubled = (int32_t)((uint32_t)state->src->num_kills * 2u);
  return (float)(int32_t)((uint32_t)doubled - (uint32_t)state->src->num_deaths);
}

void qa_game_ai_interbreed_end_match_bots_a(qa_game_ai_bots_a *g) {
  qa_game_ai_context_bots_a *ctx = &g->context;
  if (!ctx->interbreed)
    return;
  ctx->interbreed_match_count = (int32_t)((int64_t)ctx->interbreed_match_count + 1);
  double left = ctx->interbreed_match_count;
  double right = (double)qa_game_ai_context_cvar(ctx, "bot_interbreedcycle")->integer_value;
  if (left <
      right)
    return;
  ctx->interbreed_match_count = 0;
  qa_bot_library_bots_a *library = ctx->library;
  qa_bot_cvar_bots_a *output = qa_game_ai_context_cvar(ctx, "bot_interbreedwrite");
  qa_bot_cvar_update_bots_a(output);
  if (output->value[0] != '\0') {
    qa_bot_state_bots_a *best = NULL;
    double best_rank = 0;
    for (int64_t index = 0; index < QA_AI_MAX_CLIENTS; index++) {
      qa_bot_state_bots_a *state = qa_bot_state_store_get(&ctx->states, index);
      double rank = interbreed_rank(state);
      if (rank > best_rank) {
        best_rank = rank;
        best = state;
      }
    }
    if (best != NULL)
      qa_bot_goal_save_goal_fuzzy_logic(&library->goals, best->src->gs, output->value);
    registry_set(ctx, "bot_interbreedwrite", "");
  }
  double ranks[QA_AI_MAX_CLIENTS];
  for (int64_t index = 0; index < QA_AI_MAX_CLIENTS; index++)
    ranks[index] = interbreed_rank(qa_bot_state_store_get(&ctx->states, index));
  qa_genetic_selection_result selection =
      qa_bot_library_genetic_selection_bots_a(library, ranks, QA_AI_MAX_CLIENTS, NULL, NULL);
  if (selection.kind == QA_GENETIC_SELECTED) {
    qa_bot_state_bots_a *first = qa_bot_state_store_get(&ctx->states, selection.parent1);
    qa_bot_state_bots_a *second = qa_bot_state_store_get(&ctx->states, selection.parent2);
    qa_bot_state_bots_a *child = qa_bot_state_store_get(&ctx->states, selection.child);
    if (first == NULL || second == NULL || child == NULL)
      qa_range_error("Bot interbreeding selected an unallocated bot state");
    qa_string_list errors = qa_bot_goal_interbreed_goal_fuzzy_logic(
        &library->goals, first->src->gs, second->src->gs, child->src->gs);
    qa_string_list_destroy(&errors);
    qa_bot_goal_mutate_goal_fuzzy_logic(&library->goals, child->src->gs, 1);
  }
  for (int64_t index = 0; index < QA_AI_MAX_CLIENTS; index++) {
    qa_bot_state_bots_a *state = qa_bot_state_store_get(&ctx->states, index);
    if (state != NULL && state->src->inuse) {
      state->src->num_kills = 0;
      state->src->num_deaths = 0;
    }
  }
}
