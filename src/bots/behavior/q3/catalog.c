/* See header. Ported from id Software's code/game/g_bot.c (GPL-2.0-or-later). */
#include "qa/bots/behavior/q3/catalog.h"
#include "qa/bots/behavior/q3/info.h"
#include "qa/content/q3/base/game/format.h"
#include "qa/core/commands/text.h"
#include "qa/core/game_numeric.h"
#include "qa/core/info_string.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/js.h"

enum { MAX_TEXT = 8192, MAX_INFO = 1024, BOT_BEGIN_DELAY_BASE = 2000, BOT_BEGIN_DELAY_INCREMENT = 1500 };
static void *check(void *p) {
  if (p == NULL)
    qa_range_error("Out of memory in the bot catalog");
  return p;
}
static char *dup_n(const char *text, size_t n) {
  size_t length = strnlen(text, n);
  char *copy = check(malloc(length + 1));
  memcpy(copy, text, length);
  copy[length] = '\0';
  return copy;
}
static char *fold(const char *text) {
  char *out = check(qa_source_command_text(text));
  for (char *p = out; *p; p++)
    if (*p >= 'A' && *p <= 'Z')
      *p = (char)(*p + 32);
  return out;
}
static bool equal(const char *left, const char *right) {
  char *a = fold(left), *b = fold(right);
  bool same = strcmp(a, b) == 0;
  free(a);
  free(b);
  return same;
}
/* Q_CleanStr strips color pairs and non-printable bytes, without renaming blanks. */
static char *clean(const char *text) {
  char *input = check(qa_source_command_text(text));
  size_t length = strlen(input), w = 0;
  char *out = check(malloc(length + 1));
  for (size_t i = 0; i < length; i++) {
    unsigned char c = (unsigned char)input[i];
    if (c == 94 && i + 1 < length && input[i + 1] != 94) {
      i++;
      continue;
    }
    if (c >= 32 && c <= 126)
      out[w++] = (char)c;
  }
  out[w] = '\0';
  free(input);
  return out;
}
static char *info_value(const char *info, const char *key) {
  return check(qa_info_value_for_key(info, key));
}
static char *read_info(const qa_q3_game_memory_allocation *allocation) {
  return check(qa_q3_game_memory_allocation_read_string(*allocation));
}

int64_t qa_parse_game_infos(qa_common_parse_cursor *cursor, int64_t maximum,
                            qa_common_parse_state *parser, void (*print)(void *ctx, const char *text),
                            void *print_ctx, qa_q3_game_memory *memory,
                            qa_q3_game_memory_allocation *infos, size_t *infos_length, size_t start) {
  int64_t count = 0;
  for (;;) {
    const char *token = qa_common_parse_state_parse(parser, cursor, true);
    if (token[0] == '\0')
      break;
    if (strcmp(token, "{") != 0) {
      print(print_ctx, "Missing { in info file\n");
      break;
    }
    if (count == maximum) {
      print(print_ctx, "Max infos exceeded\n");
      break;
    }
    char *info = check(calloc(1, 1));
    for (;;) {
      const char *raw_key = qa_common_parse_state_parse(parser, cursor, true);
      if (raw_key[0] == '\0') {
        print(print_ctx, "Unexpected end of info file\n");
        break;
      }
      if (strcmp(raw_key, "}") == 0)
        break;
      char *key = dup_n(raw_key, strlen(raw_key));
      const char *value = qa_common_parse_state_parse(parser, cursor, false);
      char *next = check(qa_bot_info_set_value_for_key(info, key, value[0] == '\0' ? "<NULL>" : value,
                                                        print, print_ctx));
      free(key);
      free(info);
      info = next;
    }
    size_t index = start + (size_t)count;
    if (index >= QA_GAME_MAX_INFOS)
      qa_range_error("Game catalog index has no source pointer");
    infos[index] = qa_q3_game_memory_allocate(memory, (double)(strlen(info) + strlen("\\num\\") + 4 + 1));
    if (index + 1 > *infos_length)
      *infos_length = index + 1;
    qa_q3_game_memory_allocation_write_string(infos[index], info);
    free(info);
    count++;
  }
  return count;
}

void qa_game_bot_catalog_bots_a_init(qa_game_bot_catalog_bots_a *c, qa_source_bot_game_bots_a *game,
                                     qa_bot_catalog_files_bots_a files, qa_common_parse_state *parser,
                                     qa_game_bot_catalog_host_bots_a host) {
  memset(c, 0, sizeof *c);
  c->game = game;
  c->files = files;
  c->parser = parser;
  c->host = host;
}
static const qa_q3_game_memory_allocation *catalog_info(const qa_game_info_catalog_bots_a *catalog,
                                                        int64_t index) {
  if (index < 0 || (size_t)index >= catalog->infos_length)
    qa_range_error("Game catalog index has no source pointer");
  return &catalog->infos[index];
}
int64_t qa_game_bot_catalog_num_bots(const qa_game_bot_catalog_bots_a *c) { return c->bots.count; }
int64_t qa_game_bot_catalog_num_arenas(const qa_game_bot_catalog_bots_a *c) { return c->arenas.count; }
static void print(qa_game_bot_catalog_bots_a *c, const char *text) {
  c->game->options.engine.print(c->game->options.engine.ctx, text);
}
static void print_cb(void *ctx, const char *text) { print(ctx, text); }
/* TS cvarString(name, capacity): value cut to capacity - 1. */
static char *cvar_string(qa_game_bot_catalog_bots_a *c, const char *name, size_t capacity) {
  qa_cvar_read read;
  memset(&read, 0, sizeof read);
  if (!qa_cvar_registry_find(c->game->options.cvars, name, &read))
    return check(calloc(1, 1));
  char *out = dup_n(read.value, capacity - 1);
  qa_cvar_read_free(&read);
  return out;
}
static int64_t cvar_integer(qa_game_bot_catalog_bots_a *c, const char *name) {
  qa_cvar_read read;
  memset(&read, 0, sizeof read);
  if (!qa_cvar_registry_find(c->game->options.cvars, name, &read))
    return 0;
  int64_t value = (int64_t)read.integer_value;
  qa_cvar_read_free(&read);
  return value;
}
static double cvar_value(qa_game_bot_catalog_bots_a *c, const char *name) {
  char *text = cvar_string(c, name, 128);
  double value = qa_game_atof(text);
  free(text);
  return value;
}
static void set_cvar(qa_game_bot_catalog_bots_a *c, const char *name, const char *value) {
  qa_cvar_snapshot snapshot;
  memset(&snapshot, 0, sizeof snapshot);
  if (qa_cvar_registry_set(c->game->options.cvars, name, value, true, &snapshot))
    qa_cvar_read_free(&snapshot);
}
static char *set_info(qa_game_bot_catalog_bots_a *c, char *info, const char *key, const char *value) {
  char *next = check(qa_bot_info_set_value_for_key(info, key, value, print_cb, c));
  free(info);
  return next;
}
static char *format(const char *pattern, const qa_q3_game_format_arg *args, size_t count) {
  return check(qa_q3_game_format(pattern, args, count, 32000));
}
static double clock_time(qa_game_bot_catalog_bots_a *c) { return c->game->clock_time(c->game->ctx); }

static void load_file(qa_game_bot_catalog_bots_a *c, const char *filename,
                      qa_game_info_catalog_bots_a *destination) {
  size_t length = 0;
  const uint8_t *bytes = c->files.read(c->files.ctx, filename, &length);
  char text[256];
  if (bytes == NULL) {
    snprintf(text, sizeof text, "^1file not found: %s\n", filename);
    print(c, text);
    return;
  }
  if (length >= MAX_TEXT) {
    snprintf(text, sizeof text, "^1file too large: %s is %zu, max allowed is %d", filename, length, MAX_TEXT);
    print(c, text);
    return;
  }
  qa_common_parse_cursor cursor;
  if (!qa_common_parse_cursor_init(&cursor, (const char *)bytes, length, false))
    qa_range_error("Out of memory in the bot catalog");
  int64_t parsed = qa_parse_game_infos(&cursor, QA_GAME_MAX_INFOS - destination->count, c->parser,
                                       print_cb, c, c->game->memory, destination->infos,
                                       &destination->infos_length, (size_t)destination->count);
  qa_common_parse_cursor_destroy(&cursor);
  destination->count += parsed;
}
static void load_catalog(qa_game_bot_catalog_bots_a *c, bool bots, qa_game_info_catalog_bots_a *destination) {
  destination->count = 0;
  qa_bot_cvar_bots_a variable;
  qa_bot_cvar_bots_a_init(&variable, c->game->options.cvars);
  qa_bot_cvar_register_bots_a(&variable, bots ? "g_botsFile" : "g_arenasFile", "",
                              QA_CVAR_FLAG_INIT | QA_CVAR_FLAG_READ_ONLY);
  load_file(c, variable.value[0] != '\0' ? variable.value : bots ? "scripts/bots.txt" : "scripts/arenas.txt",
            destination);
  size_t count = 0;
  const char *const *names = c->files.list(c->files.ctx, "scripts", bots ? ".bot" : ".arena", &count);
  char **copies = check(calloc(count == 0 ? 1 : count, sizeof *copies));
  for (size_t i = 0; i < count; i++)
    copies[i] = dup_n(names[i], strlen(names[i]));
  for (size_t i = 0; i < count; i++) {
    size_t length = strlen("scripts/") + strlen(copies[i]);
    if (length >= 128)
      qa_range_error("Game catalog filename exceeds source 128-byte storage");
    char filename[128];
    snprintf(filename, sizeof filename, "scripts/%s", copies[i]);
    load_file(c, filename, destination);
  }
  for (size_t i = 0; i < count; i++)
    free(copies[i]);
  free(copies);
  char text[64];
  snprintf(text, sizeof text, "%lld %s parsed\n", (long long)destination->count, bots ? "bots" : "arenas");
  print(c, text);
}
char *qa_game_bot_catalog_bot_info_by_number(qa_game_bot_catalog_bots_a *c, int64_t number) {
  if (number < 0 || number >= c->bots.count) {
    char text[64];
    snprintf(text, sizeof text, "^1Invalid bot number: %lld\n", (long long)number);
    print(c, text);
    return NULL;
  }
  return read_info(catalog_info(&c->bots, number));
}
static char *find_info(qa_game_info_catalog_bots_a *catalog, const char *key, const char *wanted) {
  for (int64_t i = 0; i < catalog->count; i++) {
    char *info = read_info(catalog_info(catalog, i));
    char *value = info_value(info, key);
    bool match = equal(value, wanted);
    free(value);
    if (match)
      return info;
    free(info);
  }
  return NULL;
}
char *qa_game_bot_catalog_bot_info_by_name(qa_game_bot_catalog_bots_a *c, const char *name) {
  return find_info(&c->bots, "name", name);
}
char *qa_game_bot_catalog_arena_info_by_map(qa_game_bot_catalog_bots_a *c, const char *map) {
  return find_info(&c->arenas, "map", map);
}
static void spawn_bots(qa_game_bot_catalog_bots_a *c, const char *bot_list, int64_t base_delay) {
  c->game->reset_podium_players(c->game->ctx);
  double skill = cvar_value(c, "g_spSkill");
  if (skill < 1) {
    set_cvar(c, "g_spSkill", "1");
    skill = 1;
  } else if (skill > 5) {
    set_cvar(c, "g_spSkill", "5");
    skill = 5;
  }
  char *source = check(qa_source_command_text(bot_list));
  char *list = dup_n(source, 1023);
  free(source);
  size_t position = 0, length = strlen(list);
  int64_t delay = base_delay;
  while (position < length) {
    while (list[position] == ' ')
      position++;
    size_t start = position;
    while (position < length && list[position] != ' ')
      position++;
    char *bot = dup_n(list + start, position - start);
    if (position < length)
      position++;
    qa_q3_game_format_arg args[3] = {qa_q3_game_format_string(bot), qa_q3_game_format_number(skill),
                                     qa_q3_game_format_number((double)delay)};
    char *command = format("addbot %s %f free %i\n", args, 3);
    c->game->options.engine.insert_console_command(c->game->options.engine.insert_ctx, command);
    free(command);
    free(bot);
    delay = (int32_t)(delay + BOT_BEGIN_DELAY_INCREMENT);
  }
  free(list);
}
void qa_game_bot_catalog_initialize_bots(qa_game_bot_catalog_bots_a *c, bool restart) {
  if (cvar_integer(c, "bot_enable") != 0)
    load_catalog(c, true, &c->bots);
  load_catalog(c, false, &c->arenas);
  for (int64_t i = 0; i < c->arenas.count; i++) {
    const qa_q3_game_memory_allocation *allocation = catalog_info(&c->arenas, i);
    char number[32];
    snprintf(number, sizeof number, "%lld", (long long)i);
    char *info = set_info(c, read_info(allocation), "num", number);
    qa_q3_game_memory_allocation_write_string(*allocation, info);
    free(info);
  }
  qa_bot_cvar_bots_a_init(&c->minimum_players, c->game->options.cvars);
  c->has_minimum_players = true;
  qa_bot_cvar_register_bots_a(&c->minimum_players, "bot_minplayers", "0", QA_CVAR_FLAG_SERVER_INFO);
  if (c->game->game_type(c->game->ctx) != QA_Q3_BASE_GT_SINGLE_PLAYER)
    return;
  char *serverinfo = check(qa_cvar_registry_info_string(c->game->options.cvars, QA_CVAR_FLAG_SERVER_INFO, -1));
  char *mapname = info_value(serverinfo, "mapname");
  char *map = dup_n(mapname, 63);
  char *arena = qa_game_bot_catalog_arena_info_by_map(c, map);
  free(serverinfo);
  free(mapname);
  free(map);
  if (arena == NULL)
    return;
  char *frag = info_value(arena, "fraglimit"), *time = info_value(arena, "timelimit");
  int32_t frag_limit = qa_game_atoi(frag), time_limit = qa_game_atoi(time);
  set_cvar(c, "fraglimit", frag_limit != 0 ? frag : "0");
  set_cvar(c, "timelimit", time_limit != 0 ? time : "0");
  if (frag_limit == 0 && time_limit == 0) {
    set_cvar(c, "fraglimit", "10");
    set_cvar(c, "timelimit", "0");
  }
  char *special = info_value(arena, "special");
  int64_t delay = BOT_BEGIN_DELAY_BASE + (equal(special, "training") ? 10000 : 0);
  if (!restart) {
    char *bots = info_value(arena, "bots");
    spawn_bots(c, bots, delay);
    free(bots);
  }
  free(special);
  free(frag);
  free(time);
  free(arena);
}
bool qa_game_bot_catalog_connect(qa_game_bot_catalog_bots_a *c, int64_t client, bool restart) {
  qa_source_bot_engine_bots_a *engine = &c->game->options.engine;
  char *info = dup_n(engine->get_userinfo(engine->ctx, client), 1023);
  qa_bot_settings_bots_a settings;
  memset(&settings, 0, sizeof settings);
  char *value = info_value(info, "characterfile");
  snprintf(settings.characterfile, sizeof settings.characterfile, "%s", value);
  free(value);
  value = info_value(info, "skill");
  settings.skill = qa_game_atof(value);
  free(value);
  value = info_value(info, "team");
  snprintf(settings.team, sizeof settings.team, "%s", value);
  free(value);
  free(info);
  if (!c->host.setup_client(c->host.ctx, client, &settings, restart)) {
    engine->drop_client(engine->ctx, client, "BotAISetupClient failed");
    return false;
  }
  return true;
}
void qa_game_bot_catalog_shutdown_client(qa_game_bot_catalog_bots_a *c, int64_t client, bool restart) {
  c->host.shutdown_client(c->host.ctx, client, restart);
}
static void add_to_spawn_queue(qa_game_bot_catalog_bots_a *c, int64_t client, int64_t delay) {
  for (int i = 0; i < 16; i++)
    if (c->queue[i].spawn_time == 0) {
      c->queue[i].spawn_time = (double)qa_js_to_int32(clock_time(c) + (double)delay); /* (time + delay) | 0 */
      c->queue[i].client_num = client;
      return;
    }
  print(c, "^3Unable to delay spawn\n");
  c->game->client_begin(c->game->ctx, client);
}
/* TS `||` defaulting: an empty string selects the fallback. */
static char *value_or(const char *info, const char *key, const char *fallback) {
  char *value = info_value(info, key);
  if (value[0] != '\0')
    return value;
  free(value);
  return dup_n(fallback, strlen(fallback));
}
static void add_bot(qa_game_bot_catalog_bots_a *c, const char *name, double skill, const char *requested_team,
                    int64_t delay, const char *alternate_name) {
  skill = (float)skill;
  char *info = qa_game_bot_catalog_bot_info_by_name(c, name);
  char text[256];
  if (info == NULL) {
    snprintf(text, sizeof text, "^1Error: Bot '%s' not defined\n", name);
    print(c, text);
    return;
  }
  char *userinfo = check(calloc(1, 1));
  char *funname = value_or(info, "funname", "");
  char *display = alternate_name[0] != '\0' ? dup_n(alternate_name, strlen(alternate_name))
                  : funname[0] != '\0'       ? dup_n(funname, strlen(funname))
                                             : info_value(info, "name");
  userinfo = set_info(c, userinfo, "name", display);
  free(display);
  free(funname);
  userinfo = set_info(c, userinfo, "rate", "25000");
  userinfo = set_info(c, userinfo, "snaps", "20");
  qa_q3_game_format_arg arg = qa_q3_game_format_number(skill);
  char *skill_text = format("%1.2f", &arg, 1);
  userinfo = set_info(c, userinfo, "skill", skill_text);
  free(skill_text);
  if (skill >= 1 && skill < 2)
    userinfo = set_info(c, userinfo, "handicap", "50");
  else if (skill >= 2 && skill < 3)
    userinfo = set_info(c, userinfo, "handicap", "70");
  else if (skill >= 3 && skill < 4)
    userinfo = set_info(c, userinfo, "handicap", "90");
  char *model = value_or(info, "model", "visor/default");
  userinfo = set_info(c, userinfo, "model", model);
  userinfo = set_info(c, userinfo, "team_model", model);
  char *headmodel = value_or(info, "headmodel", model);
  userinfo = set_info(c, userinfo, "headmodel", headmodel);
  userinfo = set_info(c, userinfo, "team_headmodel", headmodel);
  free(headmodel);
  free(model);
  char *value = value_or(info, "gender", "male");
  userinfo = set_info(c, userinfo, "sex", value);
  free(value);
  value = value_or(info, "color1", "4");
  userinfo = set_info(c, userinfo, "color1", value);
  free(value);
  value = value_or(info, "color2", "5");
  userinfo = set_info(c, userinfo, "color2", value);
  free(value);
  char *aifile = info_value(info, "aifile");
  if (aifile[0] == '\0') {
    print(c, "^1Error: bot has no aifile specified\n");
    free(aifile);
    free(userinfo);
    free(info);
    return;
  }
  int64_t client = c->host.allocate_client(c->host.ctx);
  if (client == -1) {
    print(c, "^1Unable to add bot.  All player slots are in use.\n");
    print(c, "^1Start server with more 'open' slots (or check setting of sv_maxclients cvar).\n");
    free(aifile);
    free(userinfo);
    free(info);
    return;
  }
  const char *team = requested_team;
  if (team[0] == '\0')
    team = c->game->game_type(c->game->ctx) >= QA_Q3_BASE_GT_TEAM
               ? (c->game->choose_team(c->game->ctx, client) == QA_Q3_BASE_TEAM_RED ? "red" : "blue")
               : "red";
  userinfo = set_info(c, userinfo, "characterfile", aifile);
  skill_text = format("%5.2f", &arg, 1);
  userinfo = set_info(c, userinfo, "skill", skill_text);
  free(skill_text);
  userinfo = set_info(c, userinfo, "team", team);
  c->game->activate_bot(c->game->ctx, client);
  c->game->options.engine.set_userinfo(c->game->options.engine.ctx, client, userinfo);
  free(aifile);
  free(userinfo);
  free(info);
  if (c->game->client_connect(c->game->ctx, client, true, true) != NULL)
    return;
  if (delay == 0)
    c->game->client_begin(c->game->ctx, client);
  else
    add_to_spawn_queue(c, client, delay);
  /* Q3's G_AddBot tells clients to finish deferred model loads; see the TS note on
   * the level-time condition the original also required. */
  if (cvar_integer(c, "cl_running") != 0)
    c->game->options.engine.send_server_command(c->game->options.engine.ctx, -1, "loaddefered\n");
}
static void list_bots(qa_game_bot_catalog_bots_a *c) {
  print(c, "^1name             model            aifile              funname\n");
  for (int64_t i = 0; i < c->bots.count; i++) {
    char *info = read_info(catalog_info(&c->bots, i));
    char *name = value_or(info, "name", "UnnamedPlayer"), *model = value_or(info, "model", "visor/default");
    char *aifile = value_or(info, "aifile", "bots/default_c.c"), *funname = info_value(info, "funname");
    qa_q3_game_format_arg args[4] = {qa_q3_game_format_string(name), qa_q3_game_format_string(model),
                                     qa_q3_game_format_string(aifile), qa_q3_game_format_string(funname)};
    char *line = format("%-16s %-16s %-20s %-20s\n", args, 4);
    print(c, line);
    free(line);
    free(name);
    free(model);
    free(aifile);
    free(funname);
    free(info);
  }
}
void qa_game_bot_catalog_console_command(qa_game_bot_catalog_bots_a *c, const char *const *argv,
                                         size_t argc) {
  const char *command = argc > 0 ? argv[0] : "";
  if (equal(command, "botlist")) {
    list_bots(c);
    return;
  }
  if (!equal(command, "addbot") || cvar_integer(c, "bot_enable") == 0)
    return;
  char *args[6];
  for (size_t i = 1; i < 6; i++) {
    char *text = check(qa_source_command_text(i < argc ? argv[i] : ""));
    args[i] = dup_n(text, 1023);
    free(text);
  }
  if (args[1][0] == '\0')
    print(c, "Usage: Addbot <botname> [skill 1-5] [team] [msec delay] [altname]\n");
  else
    add_bot(c, args[1], args[2][0] != '\0' ? qa_game_atof(args[2]) : 4, args[3],
            args[4][0] != '\0' ? qa_game_atoi(args[4]) : 0, args[5]);
  for (size_t i = 1; i < 6; i++)
    free(args[i]);
}
void qa_game_bot_catalog_remove_queued_begin(qa_game_bot_catalog_bots_a *c, int64_t client) {
  for (int i = 0; i < 16; i++)
    if (c->queue[i].client_num == client) {
      c->queue[i].spawn_time = 0;
      return;
    }
}
void qa_game_bot_catalog_check_spawn(qa_game_bot_catalog_bots_a *c) {
  qa_game_bot_catalog_check_minimum_players(c);
  for (int i = 0; i < 16; i++) {
    qa_bot_spawn_slot_bots_a *slot = &c->queue[i];
    if (slot->spawn_time == 0 || slot->spawn_time > clock_time(c))
      continue;
    c->game->client_begin(c->game->ctx, slot->client_num);
    slot->spawn_time = 0;
    if (c->game->game_type(c->game->ctx) == QA_Q3_BASE_GT_SINGLE_PLAYER) {
      qa_source_bot_engine_bots_a *engine = &c->game->options.engine;
      char *userinfo = dup_n(engine->get_userinfo(engine->ctx, slot->client_num), 1023);
      char *raw = info_value(userinfo, "model");
      char *model = dup_n(raw, 63);
      const char *slash = strrchr(model, '/');
      char *skin = dup_n(slash == NULL ? model : slash + 1, strlen(model));
      if (equal(skin, "default")) {
        free(skin);
        skin = slash == NULL ? dup_n(model, strlen(model)) : dup_n(model, (size_t)(slash - model));
      }
      qa_q3_game_format_arg arg = qa_q3_game_format_string(skin);
      char *command = format("play sound/player/announce/%s.wav\n", &arg, 1);
      engine->append_console_command(engine->ctx, command);
      free(command);
      free(skin);
      free(model);
      free(raw);
      free(userinfo);
    }
  }
}
static bool eligible(qa_game_bot_catalog_bots_a *c, int64_t number, int64_t team, bool bot,
                     qa_bot_observed_entity_bots_a *out) {
  qa_bot_observed_entity_bots_a entity = c->game->entity(c->game->ctx, number);
  if (out != NULL)
    *out = entity;
  return entity.has_player && entity.player_connected && entity.bot == bot &&
         (team < 0 || entity.player_team == team);
}
int64_t qa_game_bot_catalog_count_human_players(qa_game_bot_catalog_bots_a *c, int64_t team) {
  int64_t count = 0, max = c->game->max_clients(c->game->ctx);
  for (int64_t i = 0; i < max; i++)
    if (eligible(c, i, team, false, NULL))
      count++;
  return count;
}
int64_t qa_game_bot_catalog_count_bot_players(qa_game_bot_catalog_bots_a *c, int64_t team) {
  int64_t count = 0, max = c->game->max_clients(c->game->ctx);
  for (int64_t i = 0; i < max; i++)
    if (eligible(c, i, team, true, NULL))
      count++;
  /* Source counts due queued begins for every team, and excludes future begins. */
  for (int i = 0; i < 16; i++)
    if (c->queue[i].spawn_time != 0 && c->queue[i].spawn_time <= clock_time(c))
      count++;
  return count;
}
static bool name_in_use(qa_game_bot_catalog_bots_a *c, const char *name, int64_t team) {
  int64_t max = c->game->max_clients(c->game->ctx);
  for (int64_t i = 0; i < max; i++) {
    qa_bot_observed_entity_bots_a entity;
    if (eligible(c, i, team, true, &entity) &&
        equal(name, entity.has_player && entity.player_name != NULL ? entity.player_name : ""))
      return true;
  }
  return false;
}
bool qa_game_bot_catalog_add_random_bot(qa_game_bot_catalog_bots_a *c, int64_t team) {
  int64_t count = 0;
  for (int64_t i = 0; i < c->bots.count; i++) {
    char *info = read_info(catalog_info(&c->bots, i)), *name = info_value(info, "name");
    if (!name_in_use(c, name, team))
      count++;
    free(name);
    free(info);
  }
  int64_t selected = qa_qvm_float_to_int((float)((double)c->game->random.random(c->game->random.ctx) * (double)count));
  for (int64_t i = 0; i < c->bots.count; i++) {
    char *info = read_info(catalog_info(&c->bots, i)), *name = info_value(info, "name");
    bool skip = name_in_use(c, name, team);
    if (!skip)
      selected--;
    if (skip || selected > 0) {
      free(name);
      free(info);
      continue;
    }
    double skill = cvar_value(c, "g_spSkill");
    const char *team_name = team == QA_Q3_BASE_TEAM_RED ? "red" : team == QA_Q3_BASE_TEAM_BLUE ? "blue" : "";
    char *short_name = dup_n(name, 35), *cleaned = clean(short_name);
    qa_q3_game_format_arg args[4] = {qa_q3_game_format_string(cleaned), qa_q3_game_format_number(skill),
                                     qa_q3_game_format_string(team_name), qa_q3_game_format_number(0)};
    char *command = format("addbot %s %f %s %i\n", args, 4);
    c->game->options.engine.insert_console_command(c->game->options.engine.insert_ctx, command);
    free(command);
    free(cleaned);
    free(short_name);
    free(name);
    free(info);
    return true;
  }
  return false;
}
bool qa_game_bot_catalog_add_match_bot(qa_game_bot_catalog_bots_a *c) {
  if (cvar_integer(c, "bot_enable") == 0)
    return false;
  if (c->game->game_type(c->game->ctx) >= QA_Q3_BASE_GT_TEAM) {
    int64_t left2 = qa_game_bot_catalog_count_human_players(c, QA_Q3_BASE_TEAM_RED);
    int64_t right2 = qa_game_bot_catalog_count_bot_players(c, QA_Q3_BASE_TEAM_RED);
    int64_t red = left2 +
                  right2;
    int64_t left = qa_game_bot_catalog_count_human_players(c, QA_Q3_BASE_TEAM_BLUE);
    int64_t right = qa_game_bot_catalog_count_bot_players(c, QA_Q3_BASE_TEAM_BLUE);
    int64_t blue = left +
                   right;
    return qa_game_bot_catalog_add_random_bot(c, red <= blue ? QA_Q3_BASE_TEAM_RED : QA_Q3_BASE_TEAM_BLUE);
  }
  return qa_game_bot_catalog_add_random_bot(c, QA_Q3_BASE_TEAM_FREE);
}
bool qa_game_bot_catalog_remove_random_bot(qa_game_bot_catalog_bots_a *c, int64_t team) {
  int64_t max = c->game->max_clients(c->game->ctx);
  for (int64_t i = 0; i < max; i++) {
    qa_bot_observed_entity_bots_a entity;
    if (!eligible(c, i, team, true, &entity))
      continue;
    const char *name = entity.player_name != NULL ? entity.player_name : "";
    if (strlen(name) >= 36)
      qa_range_error("Bot netname exceeds source 36-byte storage");
    char *cleaned = clean(name);
    qa_q3_game_format_arg arg = qa_q3_game_format_string(cleaned);
    char *command = format("kick %s\n", &arg, 1);
    c->game->options.engine.insert_console_command(c->game->options.engine.insert_ctx, command);
    free(command);
    free(cleaned);
    return true;
  }
  return false;
}
static void check_team(qa_game_bot_catalog_bots_a *c, int64_t minimum, int64_t count_team, int64_t add_team,
                       int64_t remove_team, bool tournament) {
  int64_t humans = qa_game_bot_catalog_count_human_players(c, count_team);
  int64_t bots = qa_game_bot_catalog_count_bot_players(c, count_team);
  if (humans + bots < minimum)
    qa_game_bot_catalog_add_random_bot(c, add_team);
  else if (humans + bots > minimum && bots != 0) {
    if (!tournament || !qa_game_bot_catalog_remove_random_bot(c, QA_Q3_BASE_TEAM_SPECTATOR))
      qa_game_bot_catalog_remove_random_bot(c, remove_team);
  }
}
void qa_game_bot_catalog_check_minimum_players(qa_game_bot_catalog_bots_a *c) {
  if (c->game->clock_intermission_time(c->game->ctx) != 0)
    return;
  double time = clock_time(c);
  if (c->check_minimum_time > (double)qa_js_to_int32(time - 10000))
    return;
  c->check_minimum_time = time;
  if (!c->has_minimum_players) {
    fprintf(stderr, "G_CheckMinimumPlayers requires G_InitBots\n");
    abort();
  }
  qa_bot_cvar_update_bots_a(&c->minimum_players);
  int64_t minimum = c->minimum_players.integer_value;
  if (minimum <= 0)
    return;
  int64_t max_clients = c->game->max_clients(c->game->ctx), game_type = c->game->game_type(c->game->ctx);
  if (game_type >= QA_Q3_BASE_GT_TEAM) {
    int64_t half = max_clients / 2;
    if (minimum >= half)
      minimum = half - 1;
    check_team(c, minimum, QA_Q3_BASE_TEAM_RED, QA_Q3_BASE_TEAM_RED, QA_Q3_BASE_TEAM_RED, false);
    check_team(c, minimum, QA_Q3_BASE_TEAM_BLUE, QA_Q3_BASE_TEAM_BLUE, QA_Q3_BASE_TEAM_BLUE, false);
  } else if (game_type == QA_Q3_BASE_GT_TOURNAMENT) {
    if (minimum >= max_clients)
      minimum = max_clients - 1;
    check_team(c, minimum, -1, QA_Q3_BASE_TEAM_FREE, -1, true);
  } else if (game_type == QA_Q3_BASE_GT_FFA) {
    if (minimum >= max_clients)
      minimum = max_clients - 1;
    check_team(c, minimum, QA_Q3_BASE_TEAM_FREE, QA_Q3_BASE_TEAM_FREE, QA_Q3_BASE_TEAM_FREE, false);
  }
}

static qa_save_value *sv(qa_save_value *v) { return check(v); }
static void set(qa_save_value *o, const char *k, qa_save_value *v) {
  if (!qa_save_value_set(o, k, v))
    qa_range_error("Out of memory in the bot catalog");
}
static void push(qa_save_value *a, qa_save_value *v) {
  if (!qa_save_value_push(a, v))
    qa_range_error("Out of memory in the bot catalog");
}
static qa_save_value *catalog_value(qa_game_bot_catalog_bots_a *c, const qa_game_info_catalog_bots_a *catalog) {
  qa_save_value *o = sv(qa_save_value_object()), *infos = sv(qa_save_value_array());
  set(o, "count", sv(qa_save_value_number((double)catalog->count)));
  for (size_t i = 0; i < catalog->infos_length; i++) {
    qa_q3_game_memory_pointer pointer = qa_q3_game_memory_capture_allocation(c->game->memory, catalog->infos[i]);
    qa_save_value *p = sv(qa_save_value_object());
    set(p, "offset", sv(qa_save_value_number(pointer.offset)));
    set(p, "length", sv(qa_save_value_number(pointer.length)));
    push(infos, p);
  }
  set(o, "infos", infos);
  return o;
}
qa_save_value *qa_game_bot_catalog_capture_save_state(qa_game_bot_catalog_bots_a *c) {
  qa_save_value *o = sv(qa_save_value_object()), *queue = sv(qa_save_value_array());
  set(o, "bots", catalog_value(c, &c->bots));
  set(o, "arenas", catalog_value(c, &c->arenas));
  for (int i = 0; i < 16; i++) {
    qa_save_value *slot = sv(qa_save_value_object());
    set(slot, "clientNum", sv(qa_save_value_number((double)c->queue[i].client_num)));
    set(slot, "spawnTime", sv(qa_save_value_number(c->queue[i].spawn_time)));
    push(queue, slot);
  }
  set(o, "queue", queue);
  set(o, "minimumPlayers", c->has_minimum_players ? qa_bot_cvar_capture_save_state(&c->minimum_players)
                                                  : sv(qa_save_value_null()));
  set(o, "checkMinimumTime", sv(qa_save_value_number(c->check_minimum_time)));
  set(o, "parser", check(qa_common_parse_state_capture(c->parser)));
  return o;
}
static void field(const qa_save_reader *r, const char *name, qa_save_reader *out) {
  if (!qa_save_reader_field(r, name, out))
    qa_range_error("Out of memory in the bot catalog");
}
static void restore_catalog(qa_game_bot_catalog_bots_a *c, qa_game_info_catalog_bots_a *target,
                            const qa_save_reader *cell) {
  qa_save_reader infos, count_reader, entry;
  field(cell, "infos", &infos);
  size_t length = qa_save_reader_list_count(&infos);
  field(cell, "count", &count_reader);
  double count = qa_save_reader_integer_min(&count_reader, 0);
  qa_save_reader_destroy(&count_reader);
  qa_q3_game_memory_allocation next[QA_GAME_MAX_INFOS];
  for (size_t i = 0; i < length && i < QA_GAME_MAX_INFOS; i++) {
    if (!qa_save_reader_item(&infos, i, &entry))
      qa_range_error("Out of memory in the bot catalog");
    next[i] = qa_q3_game_memory_restore_allocation(c->game->memory, entry.value);
    qa_save_reader_destroy(&entry);
  }
  if (count > (double)length || length > QA_GAME_MAX_INFOS)
    qa_save_reader_fail(cell, "invalid catalog extent");
  memcpy(target->infos, next, length * sizeof *next);
  target->infos_length = length;
  target->count = (int64_t)count;
  qa_save_reader_destroy(&infos);
}
void qa_game_bot_catalog_restore_save_state(qa_game_bot_catalog_bots_a *c, const qa_save_value *value) {
  qa_save_reader reader, cell, list, leaf;
  if (!qa_save_reader_init(&reader, value, "bot.catalog"))
    qa_range_error("Out of memory in the bot catalog");
  field(&reader, "bots", &cell);
  restore_catalog(c, &c->bots, &cell);
  qa_save_reader_destroy(&cell);
  field(&reader, "arenas", &cell);
  restore_catalog(c, &c->arenas, &cell);
  qa_save_reader_destroy(&cell);
  field(&reader, "queue", &list);
  if (qa_save_reader_list_count(&list) != 16)
    qa_save_reader_fail(&reader, "incorrect spawn queue extent");
  for (size_t i = 0; i < 16; i++) {
    if (!qa_save_reader_item(&list, i, &cell))
      qa_range_error("Out of memory in the bot catalog");
    field(&cell, "clientNum", &leaf);
    c->queue[i].client_num = (int64_t)qa_save_reader_integer(&leaf);
    qa_save_reader_destroy(&leaf);
    field(&cell, "spawnTime", &leaf);
    c->queue[i].spawn_time = qa_save_reader_number(&leaf);
    qa_save_reader_destroy(&leaf);
    qa_save_reader_destroy(&cell);
  }
  qa_save_reader_destroy(&list);
  field(&reader, "minimumPlayers", &cell);
  c->has_minimum_players = !qa_save_reader_is_null(&cell);
  if (c->has_minimum_players) {
    qa_bot_cvar_bots_a_init(&c->minimum_players, c->game->options.cvars);
    qa_bot_cvar_restore_save_state(&c->minimum_players, cell.value);
  }
  qa_save_reader_destroy(&cell);
  field(&reader, "checkMinimumTime", &cell);
  c->check_minimum_time = qa_save_reader_number(&cell);
  qa_save_reader_destroy(&cell);
  field(&reader, "parser", &cell);
  qa_common_parse_state_restore(c->parser, cell.value);
  qa_save_reader_destroy(&cell);
  qa_save_reader_destroy(&reader);
}
