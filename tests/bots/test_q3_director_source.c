/*
 * C checks for src/bots/behavior/director.ts, population.ts, q3/source-game.ts
 * and the bot_challenge branch of q3/ai-input.ts.
 *
 * Director/population: tests/bots has no TS test that constructs the director
 * outside a full SharedSimulation, so the expected transcript below was
 * produced by running the real TS SourceBotDirector and SharedBotPopulation
 * under bun over the same deterministic fake host, game and navigation
 * (scratch harness director_oracle.ts). Every callback checks the ctx it was
 * installed with.
 *
 * q3BotGame: checked over the real Q3 subsystem owner. Expected values are the
 * TS source-game.ts reads of the same owner state (pool, records, world link
 * state, level clock, admission, pickTeam, config strings), plus the
 * tests/app/q3-runtime-warnings.test.ts knowledge check (weapon refresh never
 * asks botlib for WP_NONE).
 * GPL-2.0-or-later.
 */
#include <math.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "qa/app/bootstrap/simulation/q3/runtime.h"
#include "qa/bots/behavior/director.h"
#include "qa/bots/behavior/population.h"
#include "qa/bots/behavior/q3/ai_definitions.h"
#include "qa/bots/behavior/q3/ai_input.h"
#include "qa/bots/behavior/q3/navigation.h"
#include "qa/bots/behavior/q3/source_game.h"
#include "qa/contracts/identity.h"
#include "qa/content/q3/base/game/item_pickup.h"
#include "qa/content/q3/base/shared/entity_shared.h"
#include "qa/core/numeric.h"
#include "qa/core/cvars/index.h"
#include "qa/world/actors/body.h"
#include "qa/world/actors/callbacks.h"
#include "qa/world/actors/registry.h"
#include "qa/world/gameplay/authority.h"
#include "qa/world/gameplay/inventory.h"

static int failures = 0;
static int checks = 0;
#define CHECK(cond)                                                                                \
  do {                                                                                             \
    checks++;                                                                                      \
    if (!(cond)) {                                                                                 \
      failures++;                                                                                  \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                              \
    }                                                                                              \
  } while (0)
#define CHECK_ABORTS(...)                                                                          \
  do {                                                                                             \
    checks++;                                                                                      \
    fflush(NULL);                                                                                  \
    pid_t pid = fork();                                                                            \
    if (pid == 0) {                                                                                \
      (void)prctl(PR_SET_DUMPABLE, 0);                                                             \
      if (freopen("/dev/null", "w", stderr) == NULL)                                               \
        _exit(2);                                                                                  \
      __VA_ARGS__;                                                                                 \
      _exit(0);                                                                                    \
    }                                                                                              \
    int status = 0;                                                                                \
    waitpid(pid, &status, 0);                                                                      \
    if (!(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT)) {                                   \
      failures++;                                                                                  \
      fprintf(stderr, "FAIL %s:%d: expected abort: %s\n", __FILE__, __LINE__, #__VA_ARGS__);       \
    }                                                                                              \
  } while (0)

/* ---- transcript ---- */
static char **lines;
static size_t line_count, line_capacity;
static void emit_line(const char *text) {
  for (;;) {
    const char *end = strchr(text, '\n');
    size_t length = end == NULL ? strlen(text) : (size_t)(end - text);
    if (line_count == line_capacity) {
      line_capacity = line_capacity ? line_capacity * 2 : 256;
      lines = realloc(lines, line_capacity * sizeof *lines);
    }
    lines[line_count++] = strndup(text, length);
    if (end == NULL)
      return;
    text = end + 1;
  }
}
static void L(const char *format, ...) {
  char buffer[8192];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof buffer, format, args);
  va_end(args);
  emit_line(buffer);
}
/* TS N(): integers plain (no -0), otherwise toFixed(6). */
static const char *N(double v) {
  static char ring[64][48];
  static int at;
  char *out = ring[at++ % 64];
  if (v == 0)
    strcpy(out, "0");
  else if (isfinite(v) && v == trunc(v))
    snprintf(out, 48, "%.0f", v);
  else
    snprintf(out, 48, "%.6f", v);
  return out;
}
static const char *B(bool v) { return v ? "true" : "false"; }
static char *json_quote(const char *text, char *out) {
  char *w = out;
  *w++ = '"';
  for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
    if (*p == '"' || *p == '\\') {
      *w++ = '\\';
      *w++ = (char)*p;
    } else if (*p == '\n') {
      *w++ = '\\';
      *w++ = 'n';
    } else if (*p < 0x20)
      w += sprintf(w, "\\u%04x", *p);
    else
      *w++ = (char)*p;
  }
  *w++ = '"';
  *w = '\0';
  return out;
}
/* JSON.stringify of a checkpoint tree (numbers here are integers). */
static void json_value(const qa_save_value *v, char **w) {
  switch (v->kind) {
  case QA_SAVE_NUMBER:
    *w += sprintf(*w, "%s", N(v->number));
    break;
  case QA_SAVE_STRING:
    json_quote(v->text, *w);
    *w += strlen(*w);
    break;
  case QA_SAVE_BOOLEAN:
    *w += sprintf(*w, "%s", B(v->boolean));
    break;
  case QA_SAVE_ARRAY:
    *(*w)++ = '[';
    for (size_t i = 0; i < v->item_count; i++) {
      if (i)
        *(*w)++ = ',';
      json_value(v->items[i], w);
    }
    *(*w)++ = ']';
    break;
  case QA_SAVE_OBJECT:
    *(*w)++ = '{';
    for (size_t i = 0; i < v->entry_count; i++) {
      if (i)
        *(*w)++ = ',';
      json_quote(v->entries[i].key, *w);
      *w += strlen(*w);
      *(*w)++ = ':';
      json_value(v->entries[i].value, w);
    }
    *(*w)++ = '}';
    break;
  default:
    *w += sprintf(*w, "null");
  }
  **w = 0;
}
static char *json(const qa_save_value *v, char *out) {
  char *w = out;
  json_value(v, &w);
  return out;
}

/* ---- files ---- */
static int files_sentinel, library_sentinel;
static const char *const files[][2] = {
    {"botfiles/weapons.c",
     "projectileinfo { name \"p\" damage 50 radius 120 } weaponinfo { name \"Gun\" number 2 projectile \"p\" "
     "speed 900 }"},
    {"botfiles/items.c",
     "iteminfo \"item_health\" { name \"Health\" modelindex 5 respawntime 35 mins {-15,-15,-15} maxs "
     "{15,15,15} }"},
    {"botfiles/syn.c", ""},
    {"botfiles/rnd.c", ""},
    {"botfiles/rchat.c", ""},
    {"botfiles/match.c", ""},
    {"botfiles/bots/t_c.c",
     "skill 1 { 0 \"Tester\" 1 \"male\" 3 \"ww.c\" 21 \"chat.c\" 22 \"t\" 40 \"iw.c\" 4 0.5 5 600 }"},
    {"botfiles/iw.c", "weight \"Health\" { return 100; }"},
    {"botfiles/ww.c", "weight \"Gun\" { return 50; }"},
    {"botfiles/chat.c", "chat \"t\" { type \"game_enter\" { \"hi\"; } }"},
    {"scripts/bots.txt",
     "{\nname Tester\nmodel sarge\naifile bots/t_c.c\n}\n{\nname Other\nfunname \"Mr Other\"\naifile "
     "bots/t_c.c\n}\n"},
    {"scripts/arenas.txt", "{\nmap \"q3dm1\"\nbots \"Tester Ghost\"\n}\n{\nmap \"q3dm2\"\nbots \" Tester  Other \"\n}\n"},
};
static const uint8_t *file_lookup(const char *path, size_t *out_length) {
  for (size_t i = 0; i < sizeof files / sizeof files[0]; i++)
    if (strcmp(files[i][0], path) == 0) {
      *out_length = strlen(files[i][1]);
      return (const uint8_t *)files[i][1];
    }
  return NULL;
}
static const uint8_t *library_read(void *ctx, const char *path, size_t *out_length) {
  CHECK(ctx == &library_sentinel);
  return file_lookup(path, out_length);
}
static const uint8_t *catalog_read(void *ctx, const char *path, size_t *out_length) {
  CHECK(ctx == &files_sentinel);
  return file_lookup(path, out_length);
}
static const char *const no_names[1] = {NULL};
static const char *const *catalog_list(void *ctx, const char *directory, const char *extension, size_t *out_count) {
  CHECK(ctx == &files_sentinel);
  L("list %s %s", directory, extension);
  *out_count = 0;
  return no_names;
}
static const char *provenance_value;
static const char *catalog_provenance(void *ctx) {
  CHECK(ctx == &files_sentinel);
  return provenance_value;
}
static void trim_end(char *text) {
  size_t n = strlen(text);
  while (n > 0 && (text[n - 1] == ' ' || text[n - 1] == '\n' || text[n - 1] == '\t' || text[n - 1] == '\r'))
    text[--n] = 0;
}
static int64_t library_next_int(void *ctx) {
  CHECK(ctx == &library_sentinel);
  return 0;
}
static double library_milliseconds(void *ctx) {
  CHECK(ctx == &library_sentinel);
  return 0;
}
static void library_print(void *ctx, int severity, const char *text) {
  CHECK(ctx == &library_sentinel);
  char copy[8192];
  snprintf(copy, sizeof copy, "%s", text);
  trim_end(copy);
  L("lib %d %s", severity, copy);
}
static void library_command(void *ctx, int64_t client, const char *text) {
  CHECK(ctx == &library_sentinel);
  L("cmd %lld %s", (long long)client, text);
}
static qa_bot_log_open_result library_open_log(void *ctx, const char *name) {
  CHECK(ctx == &library_sentinel);
  (void)name;
  qa_bot_log_open_result result;
  memset(&result, 0, sizeof result);
  return result;
}

/* ---- world: fake game + director host ---- */
typedef struct world {
  qa_identity_owner identity;
  qa_cvar_registry *cvars;
  char *configstrings[1024];
  char userinfo[4][1100];
  bool allocated[4];
  int64_t generation[4];
  qa_q3_base_player_state players[4];
  int64_t entity20;
  qa_q3_config_string_store store;
  qa_q3_game_random random;
  qa_q3_game_memory memory;
  qa_bot_arsenal_interface_bots_a knowledge;
  qa_bot_observed_pickup_bots_a pickups_cells[4];
  qa_bot_pickup_observations_bots_a pickups;
  qa_source_bot_game_bots_a game;
  qa_source_bot_director_host host;
  qa_source_bot_director *director;
} world;
static world *world_a, *world_b;
static int actor_token;
#define WORLD(ctx) world_of(ctx, __LINE__)
static world *world_of(void *ctx, int line) {
  if (ctx != world_a && ctx != world_b) {
    failures++;
    fprintf(stderr, "FAIL %s:%d: callback received a foreign ctx\n", __FILE__, line);
    abort();
  }
  return ctx;
}
static bool actor_for(world *w, int64_t client, qa_owned_actor *out) {
  if (client < 0 || client >= 4 || !w->allocated[client])
    return false;
  memset(out, 0, sizeof *out);
  out->id.slot = 10 + client;
  out->id.generation = w->generation[client];
  out->id.token = &actor_token;
  out->owner = "q3:bot";
  out->token = &actor_token;
  return true;
}
static const char *cs_get(void *ctx, double index) {
  world *w = WORLD(ctx);
  return w->configstrings[(int)index] ? w->configstrings[(int)index] : "";
}
static void cs_set(void *ctx, double index, const char *value) {
  world *w = WORLD(ctx);
  free(w->configstrings[(int)index]);
  w->configstrings[(int)index] = strdup(value);
  L("cs %d %s", (int)index, value);
}
static void engine_print(void *ctx, const char *text) {
  WORLD(ctx);
  char quoted[8192];
  L("print %s", json_quote(text, quoted));
}
static const char *engine_get_userinfo(void *ctx, int64_t client) { return WORLD(ctx)->userinfo[client]; }
static void engine_set_userinfo(void *ctx, int64_t client, const char *value) {
  world *w = WORLD(ctx);
  snprintf(w->userinfo[client], sizeof w->userinfo[client], "%s", value);
  L("userinfo %lld %s", (long long)client, value);
}
static void engine_send(void *ctx, int64_t client, const char *text) {
  WORLD(ctx);
  char quoted[8192];
  L("send %lld %s", (long long)client, json_quote(text, quoted));
}
static void engine_drop(void *ctx, int64_t client, const char *reason) {
  WORLD(ctx);
  L("drop %lld %s", (long long)client, reason);
}
static void engine_insert(void *ctx, const char *text) {
  WORLD(ctx);
  char quoted[8192];
  L("insert %s", json_quote(text, quoted));
}
static void engine_append(void *ctx, const char *text) {
  WORLD(ctx);
  char quoted[8192];
  L("append %s", json_quote(text, quoted));
}
static int64_t game_type(void *ctx) {
  WORLD(ctx);
  return 0;
}
static int64_t game_max_clients(void *ctx) {
  WORLD(ctx);
  return 4;
}
static int64_t game_entity_count(void *ctx) {
  WORLD(ctx);
  return 64;
}
static qa_q3_base_server_trace_result game_trace(void *ctx, qa_q3_base_server_trace_query query) {
  WORLD(ctx);
  qa_q3_base_server_trace_result result;
  memset(&result, 0, sizeof result);
  result.fraction = 1;
  result.end = query.end;
  result.entity_num = 1023;
  return result;
}
static int64_t game_point_contents(void *ctx, qa_vec3 point, int64_t pass) {
  WORLD(ctx);
  (void)point;
  (void)pass;
  return 0;
}
static double clock_time(void *ctx) {
  WORLD(ctx);
  return 0;
}
static double knowledge_pickup_utility(void *ctx, struct qa_bot_library_bots_a *library,
                                       struct qa_bot_state_bots_a *state, const qa_pickup_supply_preview *preview) {
  world *w = WORLD(ctx);
  CHECK(library == &w->director->library);
  L("utility %d %s", state->src->client, B(preview->accepted));
  return preview->accepted ? 7 : 0;
}
static bool game_observable(void *ctx, int64_t n) {
  world *w = WORLD(ctx);
  return n < 4 && (n == 0 || w->allocated[n]);
}
static qa_bot_observed_entity_bots_a game_entity(void *ctx, int64_t n) {
  world *w = WORLD(ctx);
  qa_bot_observed_entity_bots_a e;
  memset(&e, 0, sizeof e);
  qa_q3_entity_state_init(&e.state);
  e.state.number = (int32_t)n;
  bool client = n < 4;
  e.generation = n == 20 ? w->entity20 : 1;
  e.present = n == 20 || (client && (n == 0 || w->allocated[n]));
  e.linked = true;
  e.bot = client && w->allocated[n];
  e.bounds = (qa_bounds){{-15, -15, -24}, {15, 15, 32}};
  if (client) {
    e.has_player = true;
    e.player_state = &w->players[n];
    e.player_connected = n == 0 || w->allocated[n];
    e.player_name = "P";
  }
  return e;
}
static int64_t game_model_index(void *ctx, const char *name) {
  WORLD(ctx);
  return (int64_t)strlen(name);
}
static int64_t game_choose_team(void *ctx, int64_t client) {
  WORLD(ctx);
  (void)client;
  return 1;
}
static void game_activate(void *ctx, int64_t client) {
  WORLD(ctx);
  L("activate %lld", (long long)client);
}
static void game_exit(void *ctx) {
  WORLD(ctx);
  L("exit");
}
static void game_podium(void *ctx) {
  WORLD(ctx);
  L("podium");
}
static void game_changed(void *ctx, int64_t client) {
  WORLD(ctx);
  L("changed %lld", (long long)client);
}
static const char *game_connect(void *ctx, int64_t client, bool first, bool bot) {
  world *w = WORLD(ctx);
  L("connect %lld %s %s", (long long)client, B(first), B(bot));
  bool ok = qa_source_bot_director_connect(w->director, client, false);
  L("setup %s", B(ok));
  return ok ? NULL : "denied";
}
static void game_begin(void *ctx, int64_t client) {
  WORLD(ctx);
  L("begin %lld", (long long)client);
}
static bool pickups_owns(void *ctx, int64_t client, int64_t entity) {
  WORLD(ctx);
  L("owns %lld %lld", (long long)client, (long long)entity);
  return entity == 30;
}
static const qa_bot_observed_pickup_bots_a *pickups_candidates(void *ctx, int64_t client, size_t *out_count) {
  world *w = WORLD(ctx);
  L("candidates %lld", (long long)client);
  *out_count = 4;
  return w->pickups_cells;
}
static bool pickups_inspect(void *ctx, int64_t client, qa_actor_id actor, qa_bot_observed_pickup_bots_a *out) {
  world *w = WORLD(ctx);
  L("inspect %lld %lld", (long long)client, (long long)actor.slot);
  for (int i = 0; i < 4; i++)
    if (w->pickups_cells[i].observation.actor.slot == actor.slot) {
      *out = w->pickups_cells[i];
      return true;
    }
  return false;
}
static int64_t host_allocate(void *ctx) {
  world *w = WORLD(ctx);
  for (int c = 1; c < 4; c++)
    if (!w->allocated[c]) {
      w->allocated[c] = true;
      L("allocate %d", c);
      return c;
    }
  L("allocate -1");
  return -1;
}
static bool host_actor(void *ctx, int64_t client, qa_owned_actor *out) { return actor_for(WORLD(ctx), client, out); }
static qa_arsenal_intent rocket = {"q3:arsenal", "q3:rocketlauncher", false, false, 0};
static void host_encode(void *ctx, int64_t client, const qa_q3_base_user_command *command,
                        qa_source_bot_encoded_command *out) {
  WORLD(ctx);
  L("encode %lld %s %s %s %s", (long long)client, N(command->server_time), N(command->buttons),
    N(command->forwardmove), N(command->weapon));
  memset(out, 0, sizeof *out);
  out->command.kind = QA_USER_COMMAND_Q3;
  out->command.as.q3.server_time_milliseconds = command->server_time;
  if (client % 2 == 1) {
    out->has_arsenal = true;
    out->arsenal = rocket;
  }
}
static int64_t host_snapshot(void *ctx, int64_t client, int64_t sequence) {
  WORLD(ctx);
  (void)client;
  (void)sequence;
  return -1;
}
static const char *host_console(void *ctx, int64_t client) {
  WORLD(ctx);
  (void)client;
  return NULL;
}
static int64_t host_point_contents(void *ctx, qa_vec3 point) {
  WORLD(ctx);
  (void)point;
  return 0;
}
static double memory_debug(void *ctx) {
  WORLD(ctx);
  return 0;
}
static void memory_print(void *ctx, const char *text) {
  WORLD(ctx);
  (void)text;
}

static void cvar_register(qa_cvar_registry *cvars, const char *name, const char *value, double flags) {
  qa_cvar_snapshot snapshot;
  CHECK(qa_cvar_registry_register(cvars, name, value, flags, &snapshot));
  qa_cvar_read_free(&snapshot);
}
static void cvar_set(qa_cvar_registry *cvars, const char *name, const char *value) {
  qa_cvar_snapshot snapshot;
  CHECK(qa_cvar_registry_set(cvars, name, value, true, &snapshot));
  qa_cvar_read_free(&snapshot);
}

static qa_bot_observed_pickup_bots_a pickup(int64_t slot, const char *kind, bool eligible, bool accepted) {
  static char names[64][16];
  qa_bot_observed_pickup_bots_a p;
  memset(&p, 0, sizeof p);
  p.observation.actor.slot = slot;
  p.observation.actor.generation = 2;
  p.observation.availability_kind = kind;
  p.observation.eligible = eligible;
  p.observation.at_seconds = 4;
  p.preview.accepted = accepted;
  p.entity = slot;
  p.origin = (qa_vec3){(float)slot, 1, 2};
  p.bounds = (qa_bounds){{-8, -8, -8}, {8, 8, 8}};
  snprintf(names[slot], sizeof names[slot], "item%lld", (long long)slot);
  p.name = names[slot];
  return p;
}

static world *world_create(const char *tag, const qa_q3_game_memory *shared_memory) {
  world *w = calloc(1, sizeof *w);
  CHECK(qa_identity_owner_create(&w->identity, tag));
  qa_cvar_registry_options options;
  memset(&options, 0, sizeof options);
  options.dialect = QA_COMMAND_DIALECT_Q3;
  options.context.session = w->identity.session;
  options.context.origin.kind = QA_COMMAND_ORIGIN_SERVER_CONSOLE;
  w->cvars = qa_cvar_registry_create(&options);
  cvar_register(w->cvars, "sv_maxclients", "4", 4);
  cvar_register(w->cvars, "mapname", "q3dm1", 4);
  cvar_register(w->cvars, "g_gametype", "0", 4);
  for (int i = 0; i < 4; i++) {
    w->generation[i] = 1;
    CHECK(qa_q3_base_player_state_init(&w->players[i], QA_Q3_BASE_PRODUCT_BASEQ3, NULL));
  }
  w->entity20 = 3;
  w->store = (qa_q3_config_string_store){w, cs_get, cs_set};
  qa_game_random_init(&w->random, 7);
  qa_q3_game_memory_init(&w->memory, w, memory_debug, memory_print);
  if (shared_memory != NULL) { /* TS memoryB.restoreSaveState(memoryA.captureSaveState()) */
    memcpy(w->memory.pool, shared_memory->pool, sizeof w->memory.pool);
    w->memory.alloc_point = shared_memory->alloc_point;
  }
  w->knowledge.ctx = w;
  w->knowledge.pickup_utility = knowledge_pickup_utility;
  w->pickups_cells[0] = pickup(30, "ready", true, true);
  w->pickups_cells[1] = pickup(31, "respawning", true, true);
  w->pickups_cells[2] = pickup(32, "ready", false, true);
  w->pickups_cells[3] = pickup(33, "ready", true, false);
  w->pickups = (qa_bot_pickup_observations_bots_a){w, pickups_owns, pickups_candidates, pickups_inspect};
  qa_source_bot_game_bots_a *g = &w->game;
  g->ctx = w;
  g->pickups = shared_memory == NULL ? &w->pickups : NULL;
  g->options.product = QA_Q3_BASE_PRODUCT_BASEQ3;
  g->options.cvars = w->cvars;
  g->options.configstrings = &w->store;
  g->options.engine = (qa_source_bot_engine_bots_a){w, w, engine_print, engine_get_userinfo, engine_set_userinfo,
                                                    engine_send, engine_drop, engine_insert, engine_append};
  g->game_type = game_type;
  g->max_clients = game_max_clients;
  g->entity_count = game_entity_count;
  g->trace = game_trace;
  g->point_contents = game_point_contents;
  g->random = qa_source_bot_game_random_bots_a(&w->random);
  g->clock_time = clock_time;
  g->clock_start_time = clock_time;
  g->clock_intermission_time = clock_time;
  g->memory = &w->memory;
  g->knowledge = &w->knowledge;
  g->entity = game_entity;
  g->observable = game_observable;
  g->model_index = game_model_index;
  g->choose_team = game_choose_team;
  g->activate_bot = game_activate;
  g->exit_level = game_exit;
  g->reset_podium_players = game_podium;
  g->client_userinfo_changed = game_changed;
  g->client_connect = game_connect;
  g->client_begin = game_begin;
  w->host = (qa_source_bot_director_host){w, &w->game, "q3:bots", host_allocate, host_actor, host_encode,
                                          host_snapshot, host_console, host_point_contents};
  return w;
}
static void world_destroy(world *w) {
  for (int i = 0; i < 4; i++)
    qa_q3_base_player_state_record_destroy(&w->players[i]);
  for (int i = 0; i < 1024; i++)
    free(w->configstrings[i]);
  qa_cvar_registry_destroy(w->cvars);
  qa_identity_owner_destroy(&w->identity);
  free(w);
}

/* ---- navigation ---- */
typedef struct navigation_tag {
  const char *name;
} navigation_tag;
static navigation_tag nav_a = {"a"}, nav_b = {"b"}, nav_restored = {"restored"}, nav_restored2 = {"restored2"};
static navigation_tag *nav_of(void *ctx) {
  CHECK(ctx == &nav_a || ctx == &nav_b || ctx == &nav_restored || ctx == &nav_restored2);
  return ctx;
}
static bool nav_ready(void *ctx) {
  nav_of(ctx);
  return true;
}
static void nav_with_client(void *ctx, int64_t client, void (*think)(void *), void *think_ctx) {
  nav_of(ctx);
  (void)client;
  think(think_ctx);
}
static int64_t nav_one(void *ctx, qa_vec3 origin) {
  nav_of(ctx);
  (void)origin;
  return 1;
}
static int64_t nav_reach(void *ctx, qa_vec3 origin, int64_t client) {
  nav_of(ctx);
  (void)origin;
  (void)client;
  return 1;
}
static qa_bot_navigation_area nav_area(void *ctx, int64_t number) {
  nav_of(ctx);
  (void)number;
  return (qa_bot_navigation_area){0, 0, 2, 0, 1};
}
static qa_bounds nav_presence(void *ctx, int64_t presence) {
  nav_of(ctx);
  (void)presence;
  return (qa_bounds){{-15, -15, -24}, {15, 15, 32}};
}
static qa_goal_area_info goal_area(void *ctx, int64_t number) {
  nav_of(ctx);
  (void)number;
  return (qa_goal_area_info){0, 1};
}
static int64_t goal_jump_pad(void *ctx, qa_vec3 origin, qa_bounds bounds) {
  nav_of(ctx);
  (void)origin;
  (void)bounds;
  return 0;
}
static qa_goal_drop_result goal_drop(void *ctx, qa_vec3 origin, qa_bounds bounds) {
  nav_of(ctx);
  (void)bounds;
  return (qa_goal_drop_result){origin, true};
}
static qa_goal_reachable_position goal_best(void *ctx, qa_vec3 origin, qa_bounds bounds) {
  nav_of(ctx);
  (void)bounds;
  return (qa_goal_reachable_position){1, origin};
}
static qa_goal_route_result goal_route(void *ctx, const qa_goal_route_request *request) {
  nav_of(ctx);
  (void)request;
  return (qa_goal_route_result){false, 0};
}
static void navigation_factory(void *ctx, qa_bot_library_bots_a *library, qa_aas_bsp_entities *bsp_entities,
                               qa_source_bot_navigation *out) {
  navigation_tag *tag = nav_of(ctx);
  (void)library;
  CHECK(bsp_entities != NULL); /* TS navigation(library, bspEntities): the director's entities */
  L("navigation %s", tag->name);
  memset(out, 0, sizeof *out);
  out->bot.ctx = tag;
  out->bot.ready = nav_ready;
  out->bot.with_client = nav_with_client;
  out->bot.point_area = nav_one;
  out->bot.reachability_area = nav_reach;
  out->bot.fuzzy_point_reachability_area = nav_one;
  out->bot.get_area = nav_area;
  out->bot.presence_bounds = nav_presence;
  out->goal.ctx = tag;
  out->goal.area = goal_area;
  out->goal.best_reachable_from_jump_pad_area = goal_jump_pad;
  out->goal.drop_to_floor = goal_drop;
  out->goal.best_reachable_area = goal_best;
  out->goal.reachability_area = nav_reach;
  out->goal.route = goal_route;
}

/* ---- scenario helpers ---- */
static qa_source_bot_director_options director_options(world *w, navigation_tag *navigation, const char *entities,
                                                       bool restoring) {
  qa_source_bot_director_options options;
  memset(&options, 0, sizeof options);
  options.host = &w->host;
  options.library.ctx = &library_sentinel;
  options.library.read_file = library_read;
  options.library.random = (qa_bot_random){library_next_int, &library_sentinel};
  options.library.milliseconds = library_milliseconds;
  options.library.print = library_print;
  options.library.client_command = library_command;
  options.library.open_log = library_open_log;
  options.files.files = (qa_bot_catalog_files_bots_a){&files_sentinel, catalog_read, catalog_list};
  options.files.provenance = catalog_provenance;
  options.entities = entities;
  options.restoring = restoring;
  options.navigation = navigation_factory;
  options.navigation_ctx = navigation;
  return options;
}
static void frame(qa_source_bot_director *d, double time) {
  size_t count = 0;
  const qa_actor_command *commands = qa_source_bot_director_frame(d, time, &count);
  L("frame %s %zu", N(time), count);
  for (size_t i = 0; i < count; i++) {
    const qa_actor_command *c = &commands[i];
    CHECK(c->command.kind == QA_USER_COMMAND_Q3);
    L("command %lld %lld %s %s %s %s %s", (long long)c->actor.slot, (long long)c->actor.generation,
      c->source.kind == QA_COMMAND_SOURCE_BOT ? "bot" : "?", c->source.provider, N(c->sequence),
      N(c->command.as.q3.server_time_milliseconds), c->arsenal == NULL ? "-" : c->arsenal->weapon);
  }
}
static void roster(qa_source_bot_director *d) {
  size_t count = 0;
  const qa_source_bot_roster_entry *entries = qa_source_bot_director_roster(d, &count);
  for (size_t i = 0; i < count; i++) {
    const qa_source_bot_roster_entry *e = &entries[i];
    L("roster %lld %lld %s %s %s %d", (long long)e->source_client, (long long)e->actor.id.slot,
      e->settings.characterfile, N((float)e->settings.skill), e->settings.team, e->state->src->inuse ? 1 : 0);
  }
}
static void orchestration(qa_source_bot_director *d, const char *tag) {
  qa_save_value *value = qa_source_bot_director_checkpoint_orchestration(d);
  char text[4096];
  L("%s %s", tag, json(value, text));
  qa_save_value_free(value);
}
static void attempt(const char *tag, bool ok, qa_source_bot_director *d, const char *value) {
  if (ok)
    L("%s ok%s%s", tag, value == NULL ? "" : " ", value == NULL ? "" : value);
  else
    L("%s error %s", tag, qa_source_bot_director_error(d));
}
static void arena(qa_source_bot_director *d, const char *tag, const char *map) {
  qa_source_bot_arena_entry *entries = NULL;
  size_t count = 0;
  bool ok = qa_source_bot_director_arena_roster(d, map, &entries, &count);
  char text[8192], *w = text, quoted[4096];
  *w++ = '[';
  for (size_t i = 0; i < count; i++) {
    char joined[4096];
    snprintf(joined, sizeof joined, "%s=%s", entries[i].name, entries[i].info);
    w += sprintf(w, "%s%s", i ? "," : "", json_quote(joined, quoted));
  }
  sprintf(w, "]");
  attempt(tag, ok, d, text);
  qa_source_bot_arena_roster_free(entries, count);
}
static qa_save_value *orchestration_with(const qa_save_value *base, int64_t add, const char *owner, bool doubled) {
  qa_save_value *out = qa_save_value_object(), *list = qa_save_value_array();
  qa_save_value_set(out, "version", qa_save_value_number(1));
  const qa_save_value *sequences = qa_save_value_get(base, "sequences");
  for (int pass = 0; pass < (doubled ? 2 : 1); pass++)
    for (size_t i = 0; i < sequences->item_count; i++) {
      const qa_save_value *entry = sequences->items[i], *actor = qa_save_value_get(entry, "actor");
      qa_save_value *copy = qa_save_value_object(), *actor_copy = qa_save_value_object();
      qa_save_value_set(actor_copy, "slot", qa_save_value_number(qa_save_value_get(actor, "slot")->number));
      qa_save_value_set(actor_copy, "generation", qa_save_value_number(qa_save_value_get(actor, "generation")->number));
      qa_save_value_set(copy, "actor", actor_copy);
      qa_save_value_set(copy, "owner",
                        qa_save_value_string(owner != NULL ? owner : qa_save_value_get(entry, "owner")->text));
      qa_save_value_set(copy, "next", qa_save_value_number(qa_save_value_get(entry, "next")->number + (double)add));
      qa_save_value_push(list, copy);
    }
  qa_save_value_set(out, "sequences", list);
  return out;
}
static bool resolve_a(void *ctx, qa_saved_actor_id saved, qa_owned_actor *out) {
  world *w = WORLD(ctx);
  for (int c = 1; c < 4; c++) {
    qa_owned_actor actor;
    if (actor_for(w, c, &actor) && actor.id.slot == (int64_t)saved.slot && actor.id.generation == (int64_t)saved.generation) {
      *out = actor;
      return true;
    }
  }
  return false;
}
static bool resolve_b(void *ctx, qa_saved_actor_id saved, qa_owned_actor *out) {
  return actor_for(WORLD(ctx), (int64_t)saved.slot - 10, out);
}
static qa_actor_id restore_actor(void *ctx, qa_saved_actor_id saved) {
  WORLD(ctx);
  qa_actor_id id;
  memset(&id, 0, sizeof id);
  id.slot = (int64_t)saved.slot;
  id.generation = (int64_t)saved.generation;
  return id;
}
static bool restore_edge(void *ctx, int64_t client, int64_t id) {
  WORLD(ctx);
  (void)client;
  (void)id;
  return false;
}
static void restore_remap(void *ctx, int64_t number, int64_t generation, int64_t *out_number, int64_t *out_generation) {
  WORLD(ctx);
  *out_number = number;
  *out_generation = generation;
}
static int live_sentinel;
static bool population_live(void *ctx, qa_actor_id actor) {
  CHECK(ctx == &live_sentinel);
  L("live %lld", (long long)actor.slot);
  return actor.slot != 13;
}

/* Transcript from director_oracle.ts under bun. */
static const char *const director_expected[] = {
    "navigation a",
    "lib 1 ------- BotLib Initialization -------",
    "lib 3 loaded weapons.c",
    "lib 1 loaded items.c",
    "lib 1 loaded syn.c",
    "lib 1 loaded rnd.c",
    "lib 1 loaded match.c",
    "lib 1 loaded rchat.c",
    "lib 1 no rchats",
    "lib 1 found 0 level items",
    "list scripts .bot",
    "print \"2 bots parsed\\n\"",
    "list scripts .arena",
    "print \"2 arenas parsed\\n\"",
    "load ok",
    "load-again error Bot director map lifetime is already loaded or closed",
    "arena q3dm2 ok [\"Tester=\\\\aifile\\\\bots/t_c.c\\\\model\\\\sarge\\\\name\\\\Tester\",\"Other=\\\\aifile\\\\bots/t_c.c\\\\funname\\\\Mr Other\\\\name\\\\Other\"]",
    "arena q3dm1 error Arena q3dm1 names undefined source bot Ghost",
    "arena none ok []",
    "allocate 1",
    "activate 1",
    "userinfo 1 \\team\\red\\skill\\    1.00\\characterfile\\bots/t_c.c\\color2\\5\\color1\\4\\sex\\male\\team_headmodel\\sarge\\headmodel\\sarge\\team_model\\sarge\\model\\sarge\\handicap\\50\\snaps\\20\\rate\\25000\\name\\Tester",
    "connect 1 true true",
    "lib 3 counldn't load bots/default_c.c",
    "lib 2 couldn't find skill 1 in bots/default_c.c",
    "lib 3 counldn't load bots/default_c.c",
    "lib 3 counldn't load bots/default_c.c",
    "lib 3 counldn't load bots/default_c.c",
    "lib 2 couldn't load any skill from bots/default_c.c",
    "lib 3 loaded skill 1 from bots/t_c.c",
    "lib 1 loaded iw.c",
    "lib 1 loaded ww.c",
    "lib 1 loaded t from chat.c",
    "lib 3 characteristic 48 is not initialized",
    "setup true",
    "begin 1",
    "allocate 2",
    "activate 2",
    "userinfo 2 \\team\\blue\\skill\\    2.00\\characterfile\\bots/t_c.c\\color2\\5\\color1\\4\\sex\\male\\team_headmodel\\visor/default\\headmodel\\visor/default\\team_model\\visor/default\\model\\visor/default\\handicap\\70\\snaps\\20\\rate\\25000\\name\\Mr Other",
    "connect 2 true true",
    "lib 3 counldn't load bots/default_c.c",
    "lib 2 couldn't find skill 1 in bots/default_c.c",
    "lib 3 counldn't load bots/default_c.c",
    "lib 3 counldn't load bots/default_c.c",
    "lib 3 counldn't load bots/default_c.c",
    "lib 2 couldn't load any skill from bots/default_c.c",
    "lib 3 loaded cached skill 1.000000 from bots/t_c.c",
    "lib 3 counldn't load bots/default_c.c",
    "lib 2 couldn't find skill 4 in bots/default_c.c",
    "lib 3 counldn't load bots/default_c.c",
    "lib 3 counldn't load bots/default_c.c",
    "lib 3 counldn't load bots/default_c.c",
    "lib 2 couldn't load any skill from bots/default_c.c",
    "lib 2 couldn't find skill 4 in bots/t_c.c",
    "lib 3 counldn't load bots/default_c.c",
    "lib 3 loaded cached skill 1.000000 from bots/t_c.c",
    "lib 3 characteristic 48 is not initialized",
    "setup true",
    "begin 2",
    "print \"^1Error: Bot 'ghost' not defined\\n\"",
    "roster 1 11 bots/t_c.c 1 red 1",
    "roster 2 12 bots/t_c.c 2 blue 1",
    "challenge 1.494141 305.244141 -160 45 3",
    "challenge -6.553345 310.193481 -160 45 3",
    "challenge -49.998779 352.496338 -160 45 3",
    "challenge -105.001831 18.495483 -160 45 3",
    "challenge-reset 0 0",
    "owns 1 30",
    "owns 2 31",
    "owns-result true false",
    "candidates 1",
    "utility 1 true",
    "utility 1 false",
    "candidates-result 30:30:item30:7:30:8",
    "candidates 3",
    "candidates-none 0",
    "inspect 2 30",
    "utility 2 true",
    "inspect 2 31",
    "inspect 2 40",
    "inspect-result 30:7 null null",
    "encode 1 50 0 0 0",
    "encode 2 50 0 0 0",
    "frame 50 2",
    "command 11 1 bot q3:bots 0 50 q3:rocketlauncher",
    "command 12 1 bot q3:bots 0 50 -",
    "encode 1 100 0 0 0",
    "encode 2 100 0 0 0",
    "frame 100 2",
    "command 11 1 bot q3:bots 1 100 q3:rocketlauncher",
    "command 12 1 bot q3:bots 1 100 -",
    "status 2 2 0 2",
    "follow 2 2 0 0 0 0",
    "follow-stale 0 2 2",
    "cleared 0 2",
    "live 11",
    "live 11",
    "live 12",
    "live 13",
    "population 2 2 2 0",
    "live 11",
    "live 11",
    "population-cleared 0",
    "encode 1 150 0 0 0",
    "encode 2 150 0 0 0",
    "live 11",
    "live 12",
    "population-frame 11:2,12:2",
    "orchestration {\"version\":1,\"sequences\":[{\"actor\":{\"slot\":11,\"generation\":1},\"owner\":\"q3:bot\",\"next\":3},{\"actor\":{\"slot\":12,\"generation\":1},\"owner\":\"q3:bot\",\"next\":3}]}",
    "restore-changed ok",
    "orchestration {\"version\":1,\"sequences\":[{\"actor\":{\"slot\":11,\"generation\":1},\"owner\":\"q3:bot\",\"next\":10},{\"actor\":{\"slot\":12,\"generation\":1},\"owner\":\"q3:bot\",\"next\":10}]}",
    "restore-duplicate error Saved bot sequence has an invalid or duplicate actor",
    "restore-owner error Saved bot sequence has an invalid or duplicate actor",
    "restore-missing error Saved bot sequence has an invalid or duplicate actor",
    "orchestration {\"version\":1,\"sequences\":[{\"actor\":{\"slot\":11,\"generation\":1},\"owner\":\"q3:bot\",\"next\":10},{\"actor\":{\"slot\":12,\"generation\":1},\"owner\":\"q3:bot\",\"next\":10}]}",
    "capture-no-provenance error Exact bot persistence requires complete mounted asset provenance",
    /* TS 2026-11-01 director.ts captureSaveState writes version 2 (navigation files left out of the digest). */
    "capture ok [[\"version\",\"assets\",\"memory\",\"bsp\",\"library\",\"ai\",\"catalog\",\"orchestration\"],\"assets-v1\",2,\"{\\\"version\\\":1,\\\"sequences\\\":[{\\\"actor\\\":{\\\"slot\\\":11,\\\"generation\\\":1},\\\"owner\\\":\\\"q3:bot\\\",\\\"next\\\":10},{\\\"actor\\\":{\\\"slot\\\":12,\\\"generation\\\":1},\\\"owner\\\":\\\"q3:bot\\\",\\\"next\\\":10}]}\"]",
    "encode 1 200 0 0 0",
    "encode 2 200 0 0 0",
    "frame 200 2",
    "command 11 1 bot q3:bots 10 200 q3:rocketlauncher",
    "command 12 1 bot q3:bots 10 200 -",
    "orchestration {\"version\":1,\"sequences\":[{\"actor\":{\"slot\":11,\"generation\":1},\"owner\":\"q3:bot\",\"next\":11}]}",
    "roster 1 11 bots/t_c.c 1 red 1",
    "begin-restart ok",
    "navigation b",
    "list scripts .bot",
    "print \"2 bots parsed\\n\"",
    "list scripts .arena",
    "print \"2 arenas parsed\\n\"",
    "bind ok",
    "resume ok",
    "lib 3 counldn't load bots/default_c.c",
    "lib 2 couldn't find skill 1 in bots/default_c.c",
    "lib 3 counldn't load bots/default_c.c",
    "lib 3 counldn't load bots/default_c.c",
    "lib 3 counldn't load bots/default_c.c",
    "lib 2 couldn't load any skill from bots/default_c.c",
    "lib 3 loaded cached skill 1.000000 from bots/t_c.c",
    "lib 3 characteristic 48 is not initialized",
    "reconnect ok true",
    "roster 1 11 bots/t_c.c 1 red 1",
    "encode 1 250 0 0 0",
    "frame 250 1",
    "command 11 1 bot q3:bots 0 250 q3:rocketlauncher",
    "orchestration {\"version\":1,\"sequences\":[{\"actor\":{\"slot\":11,\"generation\":1},\"owner\":\"q3:bot\",\"next\":1}]}",
    "navigation restored",
    "restore-assets error Saved bot assets differ from mounted bot files",
    "navigation restored2",
    "restore ok",
    "restored-orchestration {\"version\":1,\"sequences\":[{\"actor\":{\"slot\":11,\"generation\":1},\"owner\":\"q3:bot\",\"next\":10},{\"actor\":{\"slot\":12,\"generation\":1},\"owner\":\"q3:bot\",\"next\":10}]}",
    "roster 1 11 bots/t_c.c 1 red 1",
    "roster 2 12 bots/t_c.c 2 blue 1",
    "encode 1 300 0 0 0",
    "encode 2 300 0 0 0",
    "frame 300 2",
    "command 11 1 bot q3:bots 10 300 q3:rocketlauncher",
    "command 12 1 bot q3:bots 10 300 -",
    "close-twice ok",
};

static void scenario_director(void) {
  world *a = world_a = world_create("director-a", NULL);
  qa_source_bot_director_options options = director_options(a, &nav_a, "{\n\"classname\" \"worldspawn\"\n}\n", false);
  qa_source_bot_director *director = qa_source_bot_director_create(&options);
  CHECK(director != NULL);
  a->director = director;
  attempt("load", qa_source_bot_director_load(director, false), director, NULL);
  CHECK_ABORTS(qa_source_bot_director_load(director, false));
  L("load-again error Bot director map lifetime is already loaded or closed");
  arena(director, "arena q3dm2", "q3dm2");
  arena(director, "arena q3dm1", "q3dm1");
  arena(director, "arena none", "none");
  cvar_set(a->cvars, "bot_pause", "1");
  const char *add1[] = {"addbot", "tester", "1"}, *add2[] = {"addbot", "other", "2", "blue"}, *add3[] = {"addbot", "ghost"};
  qa_source_bot_director_console_command(director, add1, 3);
  qa_source_bot_director_console_command(director, add2, 4);
  qa_source_bot_director_console_command(director, add3, 2);
  roster(director);
  {
    qa_game_ai_context_bots_a *ctx = &qa_source_bot_director_ai(director)->context;
    qa_bot_state_bots_a *st = qa_bot_state_store_get(&ctx->states, 1);
    int32_t enemy0 = st->src->enemy;
    cvar_set(a->cvars, "bot_challenge", "1");
    qa_bot_cvar_update_bots_a(qa_game_ai_context_cvar(ctx, "bot_challenge"));
    static const int enemies[] = {-1, 2};
    for (int i = 0; i < 2; i++) {
      st->src->enemy = enemies[i];
      st->src->ideal_viewangles = (qa_bot_fvec3){200, 45, 0};
      st->src->viewangles = (qa_bot_fvec3){10, 300, 0};
      st->src->viewangle_speed = (qa_bot_fvec3){3, -4, 0};
      for (int step = 0; step < 2; step++) {
        qa_bot_change_view_angles(ctx, st, 0.1);
        L("challenge %s %s %s %s %s", N(st->src->viewangles.x), N(st->src->viewangles.y),
          N(st->src->ideal_viewangles.x), N(st->src->ideal_viewangles.y), N(st->src->viewangle_speed.x));
      }
    }
    cvar_set(a->cvars, "bot_challenge", "0");
    qa_bot_cvar_update_bots_a(qa_game_ai_context_cvar(ctx, "bot_challenge"));
    st->src->enemy = enemy0;
    L("challenge-reset %s %d", N((double)qa_game_ai_context_cvar(ctx, "bot_challenge")->integer_value), enemy0);
  }
  {
    CHECK(director->library.goals.has_world && director->library.goals.world.has_source_pickups);
    qa_source_pickup_goals w = director->library.goals.world.source_pickups;
    bool owns30 = w.owns_item_goal(w.ctx, 1, 30);
    bool owns31 = w.owns_item_goal(w.ctx, 2, 31);
    L("owns-result %s %s", B(owns30), B(owns31));
    size_t count = 0;
    const qa_source_pickup_goal *goals = w.candidates(w.ctx, 1, &count);
    char text[2048], *wr = text;
    *wr = 0;
    for (size_t i = 0; i < count; i++)
      wr += sprintf(wr, "%s%lld:%lld:%s:%s:%s:%s", i ? "," : "", (long long)goals[i].actor.slot,
                    (long long)goals[i].entity, goals[i].name, N(goals[i].utility), N(goals[i].origin.x),
                    N(goals[i].bounds.max.z));
    L("candidates-result %s", text);
    w.candidates(w.ctx, 3, &count);
    L("candidates-none %zu", count);
    qa_source_pickup_goal inspected, missing, none;
    qa_actor_id id30 = {{0}, 30, 2, NULL}, id31 = {{0}, 31, 2, NULL}, id40 = {{0}, 40, 2, NULL};
    bool has_inspected = w.inspect(w.ctx, 2, id30, &inspected);
    bool has_missing = w.inspect(w.ctx, 2, id31, &missing);
    bool has_none = w.inspect(w.ctx, 2, id40, &none);
    char first[64];
    snprintf(first, sizeof first, "%lld:%s", (long long)inspected.entity, N(inspected.utility));
    L("inspect-result %s %s %s", has_inspected ? first : "null", has_missing ? "x" : "null", has_none ? "x" : "null");
  }
  frame(director, 50);
  frame(director, 100);
  qa_bot_goal_status s1 = qa_source_bot_director_request_move_to_point(director, 1, (qa_vec3){1, 2, 3});
  qa_bot_goal_status s2 = qa_source_bot_director_request_move_to_point(director, 1, (qa_vec3){1, 2, 3});
  qa_bot_goal_status s3 = qa_source_bot_director_request_move_to_point(director, 1, (qa_vec3){NAN, 0, 0});
  qa_bot_goal_status s4 = qa_source_bot_director_goal_status(director, 1);
  L("status %d %d %d %d", s1, s2, s3, s4);
  qa_bot_goal_status f1 = qa_source_bot_director_request_follow_entity(director, 2, 20);
  qa_bot_goal_status f2 = qa_source_bot_director_goal_status(director, 2);
  qa_bot_goal_status f3 = qa_source_bot_director_request_follow_entity(director, 2, 64);
  qa_bot_goal_status f4 = qa_source_bot_director_request_follow_entity(director, 2, 21);
  qa_bot_goal_status f5 = qa_source_bot_director_request_follow_entity(director, 3, 20);
  qa_bot_goal_status f6 = qa_source_bot_director_goal_status(director, 3);
  L("follow %d %d %d %d %d %d", f1, f2, f3, f4, f5, f6);
  a->entity20 = 4;
  qa_bot_goal_status g1 = qa_source_bot_director_goal_status(director, 2);
  qa_bot_goal_status g2 = qa_source_bot_director_request_follow_entity(director, 2, 20);
  qa_bot_goal_status g3 = qa_source_bot_director_goal_status(director, 2);
  L("follow-stale %d %d %d", g1, g2, g3);
  qa_source_bot_director_clear_goal(director, 1);
  qa_bot_goal_status c1 = qa_source_bot_director_goal_status(director, 1);
  qa_bot_goal_status c2 = qa_source_bot_director_goal_status(director, 2);
  L("cleared %d %d", c1, c2);

  qa_shared_bot_population population;
  qa_shared_bot_population_init(&population, population_live, &live_sentinel, director);
  qa_owned_actor owned1, owned2;
  CHECK(actor_for(a, 1, &owned1) && actor_for(a, 2, &owned2));
  qa_actor_id stranger = owned1.id;
  stranger.slot = 13;
  qa_bot_goal_status p1 = qa_shared_bot_population_request_move_to_point(&population, owned1.id, (qa_vec3){5, 5, 5});
  qa_bot_goal_status p2 = qa_shared_bot_population_goal_status(&population, owned1.id);
  qa_bot_goal_status p3 = qa_shared_bot_population_request_follow_entity(&population, owned2.id, 0);
  qa_bot_goal_status p4 = qa_shared_bot_population_goal_status(&population, stranger);
  L("population %d %d %d %d", p1, p2, p3, p4);
  qa_shared_bot_population_clear_goal(&population, owned1.id);
  L("population-cleared %d", qa_shared_bot_population_goal_status(&population, owned1.id));
  size_t count = 0;
  const qa_actor_command *commands =
      qa_shared_bot_population_frame(&population, (qa_bot_frame){150, 50}, &count);
  char text[16384], *w = text;
  *w = 0;
  for (size_t i = 0; i < count; i++)
    w += sprintf(w, "%s%lld:%s", i ? "," : "", (long long)commands[i].actor.slot, N(commands[i].sequence));
  L("population-frame %s", text);

  qa_save_value *original = qa_source_bot_director_checkpoint_orchestration(director);
  L("orchestration %s", json(original, text));
  qa_save_value *changed = orchestration_with(original, 7, NULL, false);
  attempt("restore-changed", qa_source_bot_director_restore_orchestration(director, changed, resolve_a, a), director, NULL);
  orchestration(director, "orchestration");
  qa_save_value *duplicate = orchestration_with(changed, 0, NULL, true);
  attempt("restore-duplicate", qa_source_bot_director_restore_orchestration(director, duplicate, resolve_a, a), director, NULL);
  qa_save_value *other_owner = orchestration_with(changed, 0, "q3:other", false);
  attempt("restore-owner", qa_source_bot_director_restore_orchestration(director, other_owner, resolve_a, a), director, NULL);
  qa_save_value *missing = qa_save_value_object(), *missing_list = qa_save_value_array(),
                *missing_entry = qa_save_value_object(), *missing_actor = qa_save_value_object();
  qa_save_value_set(missing, "version", qa_save_value_number(1));
  qa_save_value_set(missing_actor, "slot", qa_save_value_number(99));
  qa_save_value_set(missing_actor, "generation", qa_save_value_number(1));
  qa_save_value_set(missing_entry, "actor", missing_actor);
  qa_save_value_set(missing_entry, "owner", qa_save_value_string("q3:bot"));
  qa_save_value_set(missing_entry, "next", qa_save_value_number(1));
  qa_save_value_push(missing_list, missing_entry);
  qa_save_value_set(missing, "sequences", missing_list);
  attempt("restore-missing", qa_source_bot_director_restore_orchestration(director, missing, resolve_a, a), director, NULL);
  orchestration(director, "orchestration");
  qa_save_value_free(original);
  qa_save_value_free(changed);
  qa_save_value_free(duplicate);
  qa_save_value_free(other_owner);
  qa_save_value_free(missing);

  qa_save_value *no_provenance = qa_source_bot_director_capture_save_state(director);
  attempt("capture-no-provenance", no_provenance != NULL, director, NULL);
  provenance_value = "assets-v1";
  qa_save_value *saved = qa_source_bot_director_capture_save_state(director);
  qa_q3_game_memory *memory_image = malloc(sizeof *memory_image);
  memcpy(memory_image, &a->memory, sizeof *memory_image);
  {
    char keys[512], *k = keys, orch[4096], quoted[8192], value[16384];
    *k++ = '[';
    for (size_t i = 0; i < saved->entry_count; i++)
      k += sprintf(k, "%s\"%s\"", i ? "," : "", saved->entries[i].key);
    sprintf(k, "]");
    json(qa_save_value_get(saved, "orchestration"), orch);
    snprintf(value, sizeof value, "[%s,\"%s\",%s,%s]", keys, qa_save_value_get(saved, "assets")->text,
             N(qa_save_value_get(saved, "version")->number), json_quote(orch, quoted));
    attempt("capture", saved != NULL, director, value);
  }
  frame(director, 200);
  qa_source_bot_director_shutdown_client(director, 2, false);
  a->allocated[2] = false;
  orchestration(director, "orchestration");
  roster(director);
  qa_source_bot_director_begin_round_restart(director);
  attempt("begin-restart", true, director, NULL);
  attempt("bind", qa_source_bot_director_bind_restarted_round(director, &a->host, navigation_factory, &nav_b), director, NULL);
  qa_source_bot_director_resume_round_bots(director);
  attempt("resume", true, director, NULL);
  roster(director);
  bool reconnect = qa_source_bot_director_connect(director, 1, true);
  attempt("reconnect", true, director, B(reconnect));
  roster(director);
  frame(director, 250);
  orchestration(director, "orchestration");

  /* Exact restoration into a second, inert director. */
  world *b = world_b = world_create("director-b", memory_image);
  free(memory_image);
  cvar_register(b->cvars, "bot_pause", "1", 0);
  b->allocated[1] = true;
  b->allocated[2] = true;
  qa_source_bot_restore_hooks hooks = {restore_actor, b, resolve_b, b, restore_edge, b, restore_remap, b};
  qa_source_bot_director_options restored_options = director_options(b, &nav_restored, "", true);
  qa_source_bot_director *restored = qa_source_bot_director_create(&restored_options);
  b->director = restored;
  provenance_value = "assets-v2";
  attempt("restore-assets", qa_source_bot_director_restore_save_state(restored, saved, &hooks), restored, NULL);
  provenance_value = "assets-v1";
  qa_source_bot_director_destroy(restored);
  qa_source_bot_director_options restored2_options = director_options(b, &nav_restored2, "", true);
  qa_source_bot_director *restored2 = qa_source_bot_director_create(&restored2_options);
  b->director = restored2;
  attempt("restore", qa_source_bot_director_restore_save_state(restored2, saved, &hooks), restored2, NULL);
  orchestration(restored2, "restored-orchestration");
  roster(restored2);
  frame(restored2, 300);
  qa_source_bot_director_close(restored2, false);
  qa_source_bot_director_close(director, false);
  qa_source_bot_director_close(director, false);
  attempt("close-twice", true, director, NULL);
  /* TS lifecycle errors: frames and checkpoints need a live idle director. */
  CHECK_ABORTS(qa_source_bot_director_frame(director, 350, &count));
  CHECK_ABORTS(qa_source_bot_director_checkpoint_orchestration(director));
  qa_save_value_free(saved);
  qa_source_bot_director_destroy(restored2);
  qa_source_bot_director_destroy(director);
  world_destroy(b);
  world_destroy(a);
  world_a = world_b = NULL;
}

static void compare_transcript(void) {
  size_t want = sizeof director_expected / sizeof director_expected[0];
  size_t mismatches = 0;
  for (size_t i = 0; i < want || i < line_count; i++) {
    const char *have = i < line_count ? lines[i] : "<missing>";
    const char *need = i < want ? director_expected[i] : "<extra>";
    checks++;
    if (strcmp(have, need) != 0) {
      failures++;
      if (mismatches++ < 25)
        fprintf(stderr, "line %zu\n  have: %s\n  want: %s\n", i + 1, have, need);
    }
  }
  for (size_t i = 0; i < line_count; i++)
    free(lines[i]);
  free(lines);
  lines = NULL;
  line_count = line_capacity = 0;
}

/* Transcript from navigation_oracle.ts under bun. */
static const char *const navigation_expected[] = {
    "view-none false 9,9,9",
    "view 30 true -10,0,0",
    "admit 100,0,0 400,0,0 walk",
    "view 150 true 110,0,0",
    "admit 100,0,0 400,0,0 walk",
    "view 300 true 260,0,0",
    "admit 100,0,0 400,0,0 walk",
    "admit 400,0,0 600,0,0 walk",
    "view 450 true 410,0,0",
    "admit 100,0,0 400,0,0 walk",
    "admit 400,0,0 600,0,0 walk",
    "view 2000 false 600,0,0",
    "view-teleport true 250,30,0",
    "view-bad false 9,9,9",
    "view-history false 250,30,0",
    "print 4 invalid move state 9",
    "view-handle false 9,9,9",
    "visible-zero false 9,9,9",
    "visible-same true 250,30,0",
    "admit 0,0,0 100,0,0 walk",
    "trace 100,0,0 600,0,0 null -1 1",
    "admit 100,0,0 400,0,0 walk",
    "trace 400,0,0 600,0,0 null -1 1",
    "visible-walk true 400,0,0",
    "admit 0,0,0 100,0,0 walk",
    "visible-next true 100,0,0",
    "visible-none false 9,9,9",
};

/* ---- SourceBotNavigation look-ahead (BotAddToTarget) over a constructed graph ---- */
static int nav_world_sentinel, nav_host_sentinel, nav_moves_sentinel;
static const char *V(qa_vec3 v) {
  static char ring[16][96];
  static int at;
  char *out = ring[at++ % 16];
  snprintf(out, 96, "%s,%s,%s", N(v.x), N(v.y), N(v.z));
  return out;
}
static const double nav_xs[5] = {0, 100, 250, 400, 600};
static qa_vec3 nav_node(int64_t id) { return (qa_vec3){(float)nav_xs[id - 1], id == 3 ? 30 : 0, 0}; }
static const char *const mode_names[QA_TM_COUNT] = {"walk", "crouch", "jump", "drop", "swim", "water-jump", "ladder",
                                                    "teleport"};
static qa_traversal_admission nav_admit_request(qa_traversal_request request) {
  L("admit %s %s %s", V(request.from), V(request.to), mode_names[request.mode]);
  qa_traversal_admission a;
  memset(&a, 0, sizeof a);
  a.admitted = true;
  a.seconds = 1;
  a.trajectory = malloc(sizeof *a.trajectory);
  a.trajectory[0] = request.to;
  a.trajectory_count = 1;
  return a;
}
static qa_traversal_admission nav_world_admit(void *ctx, qa_traversal_request request, qa_navigation_profile profile) {
  CHECK(ctx == &nav_world_sentinel);
  (void)profile;
  return nav_admit_request(request);
}
static qa_traversal_admission nav_route_admit(void *ctx, qa_traversal_request request) {
  CHECK(ctx == &nav_world_sentinel);
  return nav_admit_request(request);
}
static qa_navigation_route_prediction nav_begin_route(void *ctx, qa_navigation_profile profile) {
  CHECK(ctx == &nav_world_sentinel);
  (void)profile;
  return (qa_navigation_route_prediction){&nav_world_sentinel, nav_route_admit};
}
static bool nav_entity(void *ctx, qa_navigation_entity_binding binding, qa_navigation_entity_state *out) {
  CHECK(ctx == &nav_world_sentinel);
  (void)binding;
  (void)out;
  return false;
}
static bool nav_hazard(void *ctx, qa_bounds bounds) {
  CHECK(ctx == &nav_world_sentinel);
  (void)bounds;
  return false;
}
static qa_trace_result nav_scene_trace(void *ctx, qa_trace_query query) {
  CHECK(ctx == &nav_world_sentinel);
  qa_trace_result r;
  memset(&r, 0, sizeof r);
  r.kind = "q3";
  r.fraction = 1;
  r.end = query.end;
  return r;
}
static qa_point_contents_result nav_scene_contents(void *ctx, qa_point_contents_query query) {
  CHECK(ctx == &nav_world_sentinel);
  (void)query;
  return (qa_point_contents_result){"q3", 0, 0, 0};
}
static double nav_clock = 3;
static double nav_time(void *ctx) {
  CHECK(ctx == &nav_host_sentinel);
  return nav_clock;
}
static qa_navigation_runtime *nav_for_client(void *ctx, int64_t client);
static qa_navigation_runtime nav_runtime;
static qa_navigation_runtime *nav_for_client(void *ctx, int64_t client) {
  CHECK(ctx == &nav_host_sentinel);
  (void)client;
  return &nav_runtime;
}
static int64_t nav_point_contents(void *ctx, qa_vec3 point) {
  CHECK(ctx == &nav_host_sentinel);
  (void)point;
  return 0;
}
static qa_bot_server_trace_result nav_trace(void *ctx, qa_vec3 start, qa_vec3 end, const qa_bounds *bounds,
                                            int64_t pass, int64_t mask) {
  CHECK(ctx == &nav_host_sentinel);
  L("trace %s %s %s %lld %lld", V(start), V(end), bounds == NULL ? "null" : "box", (long long)pass, (long long)mask);
  qa_bot_server_trace_result r;
  memset(&r, 0, sizeof r);
  r.fraction = start.x >= 400 ? 1 : 0.5;
  r.end = end;
  r.entity_num = 1023;
  r.solidity = "clear";
  return r;
}
static double moves_time(void *ctx) {
  CHECK(ctx == &nav_moves_sentinel);
  return 0;
}
static void moves_print(void *ctx, int severity, const char *text) {
  CHECK(ctx == &nav_moves_sentinel);
  char copy[256];
  snprintf(copy, sizeof copy, "%s", text);
  trim_end(copy);
  L("print %d %s", severity, copy);
}
static qa_bot_goal nav_goal(int64_t area) {
  qa_bot_goal g;
  memset(&g, 0, sizeof g);
  g.origin = nav_node(area - 1);
  g.area = area;
  g.mins = (qa_vec3){-8, -8, -8};
  g.maxs = (qa_vec3){8, 8, 8};
  return g;
}
static void nav_view(qa_source_bot_navigation_bots_a *n, const char *tag, int64_t handle, int64_t area, double look) {
  qa_vec3 target = {9, 9, 9};
  bool ok = qa_source_bot_navigation_movement_view_target(n, handle, nav_goal(area), 2, look, &target);
  L("%s %s %s", tag, B(ok), V(target));
}
static void nav_visible(qa_source_bot_navigation_bots_a *n, const char *tag, int64_t area, int64_t goal_area) {
  qa_vec3 target = {9, 9, 9};
  bool ok = qa_source_bot_navigation_predict_visible_position(n, (qa_vec3){0, 0, 0}, area, nav_goal(goal_area), 2,
                                                              &target);
  L("%s %s %s", tag, B(ok), V(target));
}

static void test_navigation_lookahead(void) {
  static qa_navigation_node nodes[5];
  static qa_navigation_edge edges[5];
  static const int64_t links[5][2] = {{1, 2}, {2, 3}, {3, 4}, {4, 5}, {2, 4}};
  for (int64_t id = 1; id <= 5; id++) {
    qa_navigation_node *node = &nodes[id - 1];
    memset(node, 0, sizeof *node);
    node->id = id;
    node->origin = nav_node(id);
    node->bounds = (qa_bounds){nav_node(id), nav_node(id)};
    node->radius = 1;
    node->source.kind = QA_NS_CONSTRUCTED;
  }
  for (int64_t id = 0; id < 5; id++) {
    qa_navigation_edge *edge = &edges[id];
    memset(edge, 0, sizeof *edge);
    edge->id = id;
    edge->from = links[id][0];
    edge->to = links[id][1];
    edge->start = nav_node(edge->from);
    edge->end = nav_node(edge->to);
    edge->mode = id == 2 ? QA_TM_TELEPORT : QA_TM_WALK;
    edge->travel_seconds = id == 4 ? 9 : 1;
    edge->source.kind = QA_NS_CONSTRUCTED;
  }
  static int64_t cluster_ids[5] = {1, 2, 3, 4, 5};
  qa_navigation_cluster_list cluster;
  memset(&cluster, 0, sizeof cluster);
  cluster.nodes = cluster_ids;
  cluster.count = 5;
  qa_navigation_graph graph;
  memset(&graph, 0, sizeof graph);
  graph.map.name = "nav";
  graph.profile.shape.kind = "box";
  graph.profile.shape.bounds = (qa_bounds){{-15, -15, -24}, {15, 15, 32}};
  graph.profile.capabilities = (1u << QA_TM_WALK) | (1u << QA_TM_TELEPORT);
  graph.profile.maximum_step = 18;
  graph.profile.minimum_floor_normal = 0.7;
  graph.profile.maximum_drop = 256;
  graph.profile.team = -1;
  graph.nodes = nodes;
  graph.node_count = 5;
  graph.edges = edges;
  graph.edge_count = 5;
  graph.clusters = &cluster;
  graph.cluster_count = 1;
  qa_navigation_world world;
  memset(&world, 0, sizeof world);
  world.scene.ctx = &nav_world_sentinel;
  world.scene.trace = nav_scene_trace;
  world.scene.point_contents = nav_scene_contents;
  world.ctx = &nav_world_sentinel;
  world.admit = nav_world_admit;
  world.begin_route = nav_begin_route;
  world.get_entity = nav_entity;
  world.hazard = nav_hazard;
  CHECK(qa_navigation_runtime_init(&nav_runtime, &graph, &world));
  qa_bot_move_state_store *moves = calloc(1, sizeof *moves);
  qa_bot_move_state_store_init(moves);
  qa_bot_move_state_store_bind(moves, (qa_bot_move_state_host){&nav_moves_sentinel, moves_time, moves_print, NULL},
                               NULL);
  qa_bot_action_buffer *actions = calloc(1, sizeof *actions);
  qa_bot_action_buffer_init(actions);
  qa_source_bot_navigation_host_bots_a host;
  memset(&host, 0, sizeof host);
  host.ctx = &nav_host_sentinel;
  host.runtime = &nav_runtime;
  host.for_client = nav_for_client;
  host.move_states = moves;
  host.actions = actions;
  host.crouched_bounds = (qa_bounds){{-15, -15, -24}, {15, 15, 16}};
  host.time = nav_time;
  host.point_contents = nav_point_contents;
  host.trace = nav_trace;
  qa_source_bot_navigation_bots_a *n = calloc(1, sizeof *n);
  qa_source_bot_navigation_bots_a_init(n, &host);
  int64_t handle = qa_bot_move_state_store_allocate(moves);
  qa_bot_move_state *state = qa_bot_move_state_store_from_handle(moves, handle);
  qa_bot_move_record *r = state->src;
  r->origin = (qa_bot_fvec3){-40, 0, 0};
  r->area = 2;
  r->last_goal_area = 0;
  r->last_area = 0;
  nav_view(n, "view-none", handle, 6, 150);
  qa_bot_move_state_set_last_reachability(state, 1);
  static const int looks[] = {30, 150, 300, 450, 2000};
  for (int i = 0; i < 5; i++) {
    char tag[32];
    snprintf(tag, sizeof tag, "view %d", looks[i]);
    nav_view(n, tag, handle, 6, looks[i]);
  }
  qa_bot_move_state_set_last_reachability(state, 3);
  r->origin = (qa_bot_fvec3){260, 30, 0};
  nav_view(n, "view-teleport", handle, 6, 2000);
  qa_bot_move_state_set_last_reachability(state, 9);
  nav_view(n, "view-bad", handle, 6, 2000);
  qa_bot_move_state_set_last_reachability(state, 2);
  r->origin = (qa_bot_fvec3){100, 0, 0};
  r->last_goal_area = 6;
  r->last_area = 3;
  nav_view(n, "view-history", handle, 6, 2000);
  nav_view(n, "view-handle", 9, 6, 100);
  nav_visible(n, "visible-zero", 0, 6);
  nav_visible(n, "visible-same", 4, 4);
  nav_visible(n, "visible-walk", 2, 6);
  nav_visible(n, "visible-next", 2, 3);
  nav_visible(n, "visible-none", 6, 2);
  size_t want = sizeof navigation_expected / sizeof navigation_expected[0];
  for (size_t i = 0; i < want || i < line_count; i++) {
    const char *have = i < line_count ? lines[i] : "<missing>";
    const char *need = i < want ? navigation_expected[i] : "<extra>";
    checks++;
    if (strcmp(have, need) != 0) {
      failures++;
      fprintf(stderr, "navigation line %zu\n  have: %s\n  want: %s\n", i + 1, have, need);
    }
  }
  for (size_t i = 0; i < line_count; i++)
    free(lines[i]);
  free(lines);
  lines = NULL;
  line_count = line_capacity = 0;
  free(n);
  qa_bot_action_buffer_destroy(actions);
  free(actions);
  qa_bot_move_state_store_destroy(moves);
  free(moves);
  qa_navigation_runtime_destroy(&nav_runtime);
}

void test_source_game(void);

int main(void) {
  scenario_director();
  compare_transcript();
  test_source_game();
  test_navigation_lookahead();
  printf("test_q3_director_source: %d/%d checks passed\n", checks - failures, checks);
  return failures == 0 ? 0 : 1;
}

/* ---- q3BotGame over the real Q3 subsystem owner ---- */
static int sg_host_sentinel, sg_insert_sentinel, sg_part_sentinel, sg_arsenal_sentinel, sg_knowledge_sentinel;
static char *sg_cs[1024];
static int sg_events;
static char sg_last[256];
#define SG_HOST(ctx) CHECK((ctx) == &sg_host_sentinel)
#define SG_PART(ctx) CHECK((ctx) == &sg_part_sentinel || (ctx) == NULL)
static const char *sg_cs_get(void *ctx, double index) {
  SG_PART(ctx);
  return index >= 0 && index < 1024 && sg_cs[(size_t)index] ? sg_cs[(size_t)index] : "";
}
static void sg_cs_set(void *ctx, double index, const char *value) {
  SG_PART(ctx);
  if (index < 0 || index >= 1024)
    return;
  free(sg_cs[(size_t)index]);
  sg_cs[(size_t)index] = strdup(value ? value : "");
}
static const char *sg_host_cs_get(void *ctx, double index) {
  SG_HOST(ctx);
  snprintf(sg_last, sizeof sg_last, "cs-get %s", N(index));
  return "host-cs";
}
static void sg_host_cs_set(void *ctx, double index, const char *value) {
  SG_HOST(ctx);
  snprintf(sg_last, sizeof sg_last, "cs-set %s %s", N(index), value);
}
static void sg_print(void *ctx, const char *text) {
  SG_HOST(ctx);
  snprintf(sg_last, sizeof sg_last, "print %s", text);
}
static char *sg_get_userinfo(void *ctx, double client) {
  SG_HOST(ctx);
  char text[64];
  snprintf(text, sizeof text, "\\name\\client%s", N(client));
  sg_events++;
  return strdup(text);
}
static void sg_set_userinfo(void *ctx, double client, const char *value) {
  SG_HOST(ctx);
  snprintf(sg_last, sizeof sg_last, "userinfo %s %s", N(client), value);
}
static void sg_send(void *ctx, double client, const char *text) {
  SG_HOST(ctx);
  snprintf(sg_last, sizeof sg_last, "send %s %s", N(client), text);
}
static void sg_drop(void *ctx, double client, const char *reason) {
  SG_HOST(ctx);
  snprintf(sg_last, sizeof sg_last, "drop %s %s", N(client), reason);
}
static void sg_append(void *ctx, const char *text) {
  SG_HOST(ctx);
  snprintf(sg_last, sizeof sg_last, "append %s", text);
}
static void sg_insert(void *ctx, const char *text) {
  CHECK(ctx == &sg_insert_sentinel);
  snprintf(sg_last, sizeof sg_last, "insert %s", text);
}
static void sg_part_print(void *ctx, const char *text) {
  SG_PART(ctx);
  (void)text;
}
static void sg_part_send(void *ctx, double client, const char *text) {
  SG_PART(ctx);
  (void)client;
  (void)text;
}
static void sg_part_cs(void *ctx, double index, const char *text) {
  SG_PART(ctx);
  (void)index;
  (void)text;
}
static void sg_part_append(void *ctx, const char *text) {
  SG_PART(ctx);
  (void)text;
}
static const char *sg_part_userinfo(void *ctx, double client) {
  SG_PART(ctx);
  (void)client;
  return "";
}
static char *sg_part_userinfo_owned(void *ctx, double client) {
  SG_PART(ctx);
  (void)client;
  return strdup("\\handicap\\100");
}
static void sg_part_set_userinfo(void *ctx, double client, const char *text) {
  SG_PART(ctx);
  (void)client;
  (void)text;
}
static const char *sg_part_cvar(void *ctx, const char *name) {
  SG_PART(ctx);
  (void)name;
  return "0";
}
static void sg_part_exec(void *ctx, const char *text) {
  SG_PART(ctx);
  (void)text;
}
static qa_bounds sg_bounds(void *ctx, qa_owned_actor actor, qa_body_state state) {
  (void)ctx;
  (void)actor;
  return (qa_bounds){state.origin, state.origin};
}
static void sg_link_hook(void *ctx, qa_linked_body body) {
  (void)ctx;
  (void)body;
}
static void sg_unlink_hook(void *ctx, qa_actor_id actor) {
  (void)ctx;
  (void)actor;
}
static void sg_schedule(void *ctx, qa_owned_actor a, bool has_due, double due) {
  SG_PART(ctx);
  (void)a;
  (void)has_due;
  (void)due;
}
static void sg_run_think(void *ctx, qa_owned_actor a, double time) {
  SG_PART(ctx);
  (void)a;
  (void)time;
}
static qa_q3_game_entity *sg_foreign(void *ctx, qa_actor_id a) {
  SG_PART(ctx);
  (void)a;
  return NULL;
}
static bool sg_is_player(void *ctx, qa_actor_id a) {
  SG_PART(ctx);
  (void)a;
  return false;
}
static bool sg_is_live(void *ctx, qa_actor_id a) {
  SG_PART(ctx);
  (void)a;
  return true;
}
static void sg_apply_damage(void *ctx, qa_actor_id t, double amt, double kb, qa_vec3 d, qa_vec3 p, bool r) {
  SG_PART(ctx);
  (void)t;
  (void)amt;
  (void)kb;
  (void)d;
  (void)p;
  (void)r;
}
static void sg_collision(void *ctx, qa_owned_actor a, qa_actor_collision c) {
  SG_PART(ctx);
  (void)a;
  (void)c;
}
static bool sg_in_pvs(void *ctx, qa_vec3 a, qa_vec3 b) {
  SG_PART(ctx);
  (void)a;
  (void)b;
  return false;
}
static double sg_debug(void *ctx) {
  SG_PART(ctx);
  return 0;
}
static qa_pickup_ammo_receipt sg_receipts[2] = {{"q3:bullets", 10, 40}, {"q3:shells", 0, 5}};
static bool sg_preview(void *ctx, const void *descriptor, void *out_preview) {
  SG_PART(ctx);
  (void)descriptor;
  qa_q3_source_pickup_preview *p = out_preview;
  memset(p, 0, sizeof *p);
  p->kind = QA_Q3_SOURCE_PREVIEW_SELECTED;
  p->offer.kind = QA_PICKUP_SUPPLY_AMMO;
  p->offer.grant.item = "q3:bullets";
  p->offer.grant.amount = 50;
  p->preview.accepted = true;
  p->preview.ammo = sg_receipts;
  p->preview.ammo_count = 2;
  return true;
}
static double sg_settings_integer(void *ctx, const char *name) {
  SG_PART(ctx);
  if (strcmp(name, "g_weaponrespawn") == 0)
    return 5;
  if (strcmp(name, "g_weaponTeamRespawn") == 0)
    return 30;
  return 0;
}
/* Arsenal binding leaves. */
static int64_t sg_source_weapon(void *ctx, int64_t client) {
  CHECK(ctx == &sg_arsenal_sentinel);
  return 7 + client;
}
static int64_t sg_source_weapon_state(void *ctx, int64_t client) {
  CHECK(ctx == &sg_arsenal_sentinel);
  (void)client;
  return 2;
}
static int sg_arsenal_updates;
static double sg_k_utility(void *ctx, struct qa_bot_library_bots_a *l, struct qa_bot_state_bots_a *s,
                           const qa_pickup_supply_preview *p) {
  CHECK(ctx == &sg_knowledge_sentinel);
  (void)l;
  (void)s;
  return p->accepted ? 11 : 0;
}
static int64_t sg_k_choose(void *ctx, struct qa_bot_library_bots_a *l, struct qa_bot_state_bots_a *s) {
  CHECK(ctx == &sg_knowledge_sentinel);
  (void)l;
  (void)s;
  return 5;
}
static int64_t sg_k_activation(void *ctx, struct qa_bot_library_bots_a *l, struct qa_bot_state_bots_a *s) {
  CHECK(ctx == &sg_knowledge_sentinel);
  (void)l;
  (void)s;
  return 3;
}
static qa_bot_weapon_tactics_bots_a sg_k_tactics(void *ctx, int64_t weapon) {
  CHECK(ctx == &sg_knowledge_sentinel);
  qa_bot_weapon_tactics_bots_a t;
  memset(&t, 0, sizeof t);
  t.weakness = (double)weapon;
  return t;
}
static double sg_k_aggression(void *ctx, struct qa_bot_state_bots_a *s) {
  CHECK(ctx == &sg_knowledge_sentinel);
  (void)s;
  return 42;
}
static void sg_k_update(void *ctx, struct qa_bot_state_bots_a *s) {
  CHECK(ctx == &sg_knowledge_sentinel);
  sg_arsenal_updates++;
  s->src->inventory[QA_INV_ARMOR] = -1;
  s->src->inventory[QA_INV_HEALTH] = -1;
}
static bool sg_k_info(void *ctx, struct qa_bot_library_bots_a *l, int64_t handle, int64_t weapon,
                      qa_weapon_info_bots_a *out) {
  CHECK(ctx == &sg_knowledge_sentinel);
  (void)l;
  (void)handle;
  memset(out, 0, sizeof *out);
  out->number = (int32_t)weapon;
  return weapon == 4;
}
static int sg_library_sentinel, sg_out_of_range;
static const char *sg_weapons =
    "projectileinfo { name \"p\" damage 50 radius 120 }\n"
    "weaponinfo { name \"Gauntlet\" number 1 projectile \"p\" }\n"
    "weaponinfo { name \"Machinegun\" number 2 projectile \"p\" speed 900 }\n"
    "weaponinfo { name \"Rocket Launcher\" number 5 projectile \"p\" speed 900 }\n";
static const uint8_t *sg_library_read(void *ctx, const char *path, size_t *out_length) {
  CHECK(ctx == &sg_library_sentinel);
  if (strcmp(path, "botfiles/weapons.c") != 0)
    return NULL;
  *out_length = strlen(sg_weapons);
  return (const uint8_t *)sg_weapons;
}
static void sg_library_print(void *ctx, int severity, const char *text) {
  CHECK(ctx == &sg_library_sentinel);
  (void)severity;
  if (strstr(text, "weapon number out of range") != NULL)
    sg_out_of_range++;
}
static int64_t sg_library_int(void *ctx) {
  CHECK(ctx == &sg_library_sentinel);
  return 0;
}
static double sg_library_ms(void *ctx) {
  CHECK(ctx == &sg_library_sentinel);
  return 0;
}
static qa_bot_log_open_result sg_library_log(void *ctx, const char *name) {
  CHECK(ctx == &sg_library_sentinel);
  (void)name;
  qa_bot_log_open_result r;
  memset(&r, 0, sizeof r);
  return r;
}

void test_source_game(void) {
  qa_identity_owner identity;
  CHECK(qa_identity_owner_create(&identity, "source-game"));
  qa_session_actor_registry *actors = qa_session_actor_registry_create(&identity, 2048);
  qa_body_link_hooks hooks;
  memset(&hooks, 0, sizeof hooks);
  hooks.absolute_bounds = sg_bounds;
  hooks.on_link = sg_link_hook;
  hooks.on_unlink = sg_unlink_hook;
  qa_shared_body_table *bodies = qa_shared_body_table_create(actors, hooks);
  qa_actor_callback_table *callbacks = qa_actor_callback_table_create(actors);
  qa_gameplay_hooks gameplay_hooks;
  memset(&gameplay_hooks, 0, sizeof gameplay_hooks);
  qa_gameplay_authority *combat = qa_gameplay_authority_create(actors, callbacks, gameplay_hooks);
  qa_shared_inventory_table *inventory = qa_shared_inventory_table_create(actors);
  qa_cvar_registry_options cvar_options;
  memset(&cvar_options, 0, sizeof cvar_options);
  cvar_options.dialect = QA_COMMAND_DIALECT_Q3;
  qa_cvar_registry *cvars = qa_cvar_registry_create(&cvar_options);
  CHECK(actors && bodies && callbacks && combat && inventory && cvars);

  static const qa_q3_ta_command_settings command_settings = {0, true, false, 0, false, false};
  qa_app_q3_source_subsystems_parts parts;
  memset(&parts, 0, sizeof parts);
  parts.product = QA_Q3_BASE_PRODUCT_BASEQ3;
  parts.max_clients = 8;
  parts.map_provider = "q3:test";
  parts.settings_host.cvars = cvars;
  parts.settings_host.send_server_command = sg_part_send;
  parts.records_host.actors = actors;
  parts.records_host.bodies = bodies;
  parts.records_host.combat = combat;
  parts.records_host.inventory = inventory;
  parts.records_host.callbacks = callbacks;
  parts.records_host.schedule = sg_schedule;
  parts.records_host.run_think = sg_run_think;
  parts.records_host.foreign = sg_foreign;
  parts.records_host.is_player = sg_is_player;
  parts.world_host.bodies = bodies;
  parts.world_host.collision = sg_collision;
  parts.pool_options.ctx = &sg_part_sentinel;
  parts.pool_options.print = sg_part_print;
  parts.config_store = (qa_q3_config_string_store){&sg_part_sentinel, sg_cs_get, sg_cs_set};
  parts.memory_debug_integer = sg_debug;
  parts.memory_ctx = &sg_part_sentinel;
  parts.item_services.ctx = &sg_part_sentinel;
  parts.item_services.log = sg_part_print;
  parts.item_services.warn = sg_part_print;
  parts.item_services.get_userinfo = sg_part_userinfo_owned;
  parts.item_services.preview_pickup = sg_preview;
  parts.item_services.settings_integer = sg_settings_integer;
  parts.bridge_host.authority = combat;
  parts.bridge_host.is_live = sg_is_live;
  parts.bridge_host.apply_damage = sg_apply_damage;
  parts.team_host.send_server_command = sg_part_send;
  parts.team_host.set_configstring = sg_part_cs;
  parts.team_host.warn = sg_part_print;
  parts.team_host.in_pvs = sg_in_pvs;
  parts.death_host.log = sg_part_print;
  parts.session_services.print = sg_part_print;
  parts.match_host.append_console_command = sg_part_append;
  parts.match_host.send_server_command = sg_part_send;
  parts.match_host.set_configstring = sg_part_cs;
  parts.match_host.log = sg_part_print;
  parts.match_host.warn = sg_part_print;
  parts.admission_host.bots.kind = QA_Q3_TA_BOTS_UNAVAILABLE;
  parts.admission_host.bots.reason = "no bots";
  parts.admission_host.get_userinfo = sg_part_userinfo;
  parts.admission_host.set_configstring = sg_part_cs;
  parts.admission_host.send_server_command = sg_part_send;
  parts.admission_host.log = sg_part_print;
  parts.command_parts.settings = &command_settings;
  parts.command_parts.imports.send_server_command = sg_part_send;
  parts.command_parts.imports.set_configstring = sg_part_cs;
  parts.command_parts.imports.append_console_command = sg_part_append;
  parts.command_parts.imports.get_cvar = sg_part_cvar;
  parts.command_parts.imports.get_userinfo = sg_part_userinfo;
  parts.command_parts.imports.set_userinfo = sg_part_set_userinfo;
  parts.command_parts.imports.log = sg_part_print;
  parts.command_parts.imports.print = sg_part_print;
  parts.server_command_host.print = sg_part_print;
  parts.server_command_host.send_server_command = sg_part_send;
  parts.server_command_host.execute_console_now = sg_part_exec;
  parts.server_command_host.bots.kind = QA_Q3_TA_CAP_UNAVAILABLE;
  parts.server_command_host.bots.reason = "no bots";
  parts.spawn_text = "{ \"classname\" \"worldspawn\" }\n{ \"classname\" \"item_health\" \"origin\" \"0 0 0\" }\n"
                     "{ \"classname\" \"weapon_machinegun\" \"origin\" \"64 0 0\" }\n";
  parts.worldspawn.set_configstring = sg_part_cs;
  parts.worldspawn.log = sg_part_print;
  parts.worldspawn.motd = "";
  qa_app_q3_source_subsystems_owner *owner = qa_app_q3_source_subsystems_owner_create(parts);
  CHECK(owner != NULL);
  qa_app_q3_source_subsystems table = qa_app_q3_source_subsystems_owner_table(owner);
  void *tc = table.ctx;
  table.settings_register(tc, "Nov  2 2026");
  table.random_reset(tc, 1234);
  table.memory_initialize(tc);
  table.pool_initialize_clients(tc, 8);
  table.records_activate(tc, 1022);
  table.session_initialize_world(tc);
  table.init_body_queue(tc);
  table.registered_items_clear(tc, table.settings_integer(tc, "g_gametype"));
  qa_app_q3_spawn_report report = table.spawn_entities(tc);
  CHECK(report.outcome_count == 2);
  double health_slot = report.outcome_count > 0 ? report.outcomes[0].slot : -1;
  double machinegun_slot = report.outcome_count > 1 ? report.outcomes[1].slot : -1;
  qa_app_q3_spawn_report_destroy(&report);
  table.find_entity_teams(tc);
  table.registered_items_save(tc);

  qa_app_q3_source_host host;
  memset(&host, 0, sizeof host);
  host.ctx = &sg_host_sentinel;
  host.engine = (qa_app_q3_source_engine){&sg_host_sentinel, sg_print, sg_print, sg_send, sg_drop, sg_get_userinfo,
                                          sg_set_userinfo, NULL, sg_append, NULL};
  host.cvars = cvars;
  host.configstrings_ctx = &sg_host_sentinel;
  host.configstring_get = sg_host_cs_get;
  host.configstring_set = sg_host_cs_set;

  qa_q3_bot_game_bots_a *bot_game = malloc(sizeof *bot_game);
  CHECK(qa_q3_bot_game_bots_a_init(bot_game, owner, &host, sg_insert, &sg_insert_sentinel, NULL));
  qa_source_bot_game_bots_a *g = &bot_game->game;
  qa_q3_entity_pool *pool = qa_app_q3_source_subsystems_owner_pool(owner);
  qa_q3_game_level *level = qa_app_q3_source_subsystems_owner_level(owner);
  /* Options and plain reads. */
  CHECK(g->pickups == NULL); /* TS: arsenal === undefined ? null */
  CHECK(g->options.product == QA_Q3_BASE_PRODUCT_BASEQ3);
  CHECK(g->options.cvars == cvars);
  CHECK(g->random.ctx == qa_app_q3_source_subsystems_owner_random(owner)); /* TS random: source.random */
  CHECK(g->memory == qa_app_q3_source_subsystems_owner_memory(owner));
  CHECK(g->max_clients(g->ctx) == 8 && g->entity_count(g->ctx) == (int64_t)pool->num_entities);
  CHECK(g->game_type(g->ctx) == (int64_t)table.settings_integer(tc, "g_gametype"));
  level->base.time = 1234;
  level->base.start_time = 400;
  level->base.intermission_time = 900;
  CHECK(g->clock_time(g->ctx) == 1234 && g->clock_start_time(g->ctx) == 400 && g->clock_intermission_time(g->ctx) == 900);
  /* Config strings and engine forward to the host with the host's ctx;
   * insertConsoleCommand alone is the caller's. */
  CHECK(strcmp(g->options.configstrings->get(g->options.configstrings->ctx, 7), "host-cs") == 0);
  CHECK(strcmp(sg_last, "cs-get 7") == 0);
  g->options.configstrings->set(g->options.configstrings->ctx, 8, "v");
  CHECK(strcmp(sg_last, "cs-set 8 v") == 0);
  const qa_source_bot_engine_bots_a *engine = &g->options.engine;
  engine->print(engine->ctx, "hello");
  CHECK(strcmp(sg_last, "print hello") == 0);
  CHECK(strcmp(engine->get_userinfo(engine->ctx, 3), "\\name\\client3") == 0);
  CHECK(strcmp(engine->get_userinfo(engine->ctx, 4), "\\name\\client4") == 0 && sg_events == 2);
  engine->set_userinfo(engine->ctx, 2, "\\name\\x");
  CHECK(strcmp(sg_last, "userinfo 2 \\name\\x") == 0);
  engine->send_server_command(engine->ctx, -1, "cp hi");
  CHECK(strcmp(sg_last, "send -1 cp hi") == 0);
  engine->drop_client(engine->ctx, 3, "gone");
  CHECK(strcmp(sg_last, "drop 3 gone") == 0);
  engine->append_console_command(engine->ctx, "map q3dm1\n");
  CHECK(strcmp(sg_last, "append map q3dm1\n") == 0);
  engine->insert_console_command(engine->insert_ctx, "addbot x\n");
  CHECK(strcmp(sg_last, "insert addbot x\n") == 0);
  /* modelIndex is config.modelIndex: stable per name. */
  int64_t model = g->model_index(g->ctx, "models/test.md3");
  CHECK(model > 0 && g->model_index(g->ctx, "models/test.md3") == model);
  CHECK(model == (int64_t)qa_q3_config_string_registry_model_index(qa_app_q3_source_subsystems_owner_config(owner),
                                                                   "models/test.md3"));
  /* activateBot: BOT flag, then pool.activateClient. */
  g->activate_bot(g->ctx, 1);
  qa_q3_game_entity *bot = qa_q3_entity_pool_at(pool, 1);
  CHECK(qa_q3_game_entity_inuse(bot) && bot->client != NULL);
  CHECK((qa_int32(bot->r.sv_flags) & QA_Q3_BASE_SVF_BOT) != 0);
  /* observable: inuse && linked && !NOCLIENT. */
  CHECK(!g->observable(g->ctx, 1));
  CHECK(!g->observable(g->ctx, 40));
  qa_q3_base_entity_shared_set_current_origin(&bot->r, (qa_vec3){8, 16, 24});
  qa_q3_base_entity_shared_set_mins(&bot->r, (qa_vec3){-15, -15, -24});
  qa_q3_base_entity_shared_set_maxs(&bot->r, (qa_vec3){15, 15, 32});
  bot->r.model.kind = QA_Q3_BASE_MODEL_BOX;
  qa_q3_base_world_adapter_link(qa_app_q3_source_subsystems_owner_world(owner), bot);
  CHECK(g->observable(g->ctx, 1));
  /* entity(): detached state and player copies. */
  bot->s.weapon = 2;
  bot->s.frame = 9;
  bot->event_time = 77;
  bot->r.contents = 0x2000000;
  qa_q3_base_player_state_record_set_origin(&bot->client->ps, (qa_vec3){8, 16, 24});
  qa_q3_base_player_state_record_set_velocity(&bot->client->ps, (qa_vec3){1, 2, 3});
  qa_q3_base_player_state_record_set_health(&bot->client->ps, 87);
  bot->client->ps.weapon = 2;
  bot->client->ps.command_time = 333;
  bot->client->ps.delta_angles.y = 1024;
  qa_q3_base_player_state_slots_set(&bot->client->ps.stats, 4, 25); /* armor */
  qa_q3_base_player_state_slots_set(&bot->client->ps.ammo, 2, 50);
  bot->client->sess.session_team = 2;
  bot->client->last_hurt_client = 3;
  bot->client->last_hurt_mod = 6;
  qa_q3_game_entity *machinegun = qa_q3_entity_pool_at(pool, machinegun_slot);
  bot->activator = machinegun;
  machinegun->s.frame = 5;
  bot->s.number = 1; /* ClientSpawn's job; activateBot does not set it */
  qa_bot_observed_entity_bots_a e = g->entity(g->ctx, 1);
  CHECK(e.present && e.linked && e.bot && !e.hidden);
  CHECK(e.generation == qa_q3_game_entity_actor(bot).id.generation);
  CHECK(e.state.number == 1 && e.state.weapon == 2 && e.state.frame == 9);
  CHECK(e.origin.x == 8 && e.origin.z == 24 && e.bounds.max.z == 32 && e.contents == 0x2000000);
  CHECK(!e.has_inline_model && e.event_time == 77 && e.has_activator_frame && e.activator_frame == 5);
  CHECK(!e.proximity_trigger);
  CHECK(e.has_player && !e.player_connected && e.player_team == 2 && strcmp(e.player_name, "") == 0);
  CHECK(e.last_hurt_client == 3 && e.last_hurt_mod == 6);
  const qa_q3_base_player_state *ps = e.player_state;
  CHECK(qa_q3_base_player_state_record_origin(ps).y == 16 && qa_q3_base_player_state_record_velocity(ps).z == 3);
  CHECK(qa_q3_base_player_state_record_health(ps) == 87 && qa_q3_base_player_state_slots_get(&ps->stats, 4) == 25);
  CHECK(qa_q3_base_player_state_slots_get(&ps->ammo, 2) == 50 && ps->delta_angles.y == 1024);
  CHECK(ps->weapon == 2 && ps->command_time == 333);
  bot->client->pers.connected = QA_Q3_CONN_CONNECTED;
  bot->r.sv_flags = (double)(qa_int32(bot->r.sv_flags) | QA_Q3_BASE_SVF_NOCLIENT);
  bot->r.model.kind = QA_Q3_BASE_MODEL_INLINE;
  bot->r.model.index = 3;
  e = g->entity(g->ctx, 1);
  CHECK(e.hidden && e.player_connected && e.has_inline_model && e.inline_model == 3);
  CHECK(!g->observable(g->ctx, 1));
  qa_bot_observed_entity_bots_a item = g->entity(g->ctx, (int64_t)health_slot);
  CHECK(item.present && !item.has_player && strcmp(item.classname, "item_health") == 0);
  qa_bot_observed_entity_bots_a empty = g->entity(g->ctx, 900);
  CHECK(!empty.present && empty.generation == 0 && !empty.has_player);
  /* chooseTeam: pickTeam over pool clients and level team scores. */
  qa_q3_game_client **clients = pool->clients;
  clients[2]->pers.connected = QA_Q3_CONN_CONNECTED;
  clients[2]->sess.session_team = QA_Q3_BASE_TEAM_RED;
  clients[3]->pers.connected = QA_Q3_CONN_CONNECTED;
  clients[3]->sess.session_team = QA_Q3_BASE_TEAM_RED;
  CHECK(g->choose_team(g->ctx, 4) == QA_Q3_BASE_TEAM_BLUE);
  bot->client->sess.session_team = QA_Q3_BASE_TEAM_BLUE;
  clients[4]->pers.connected = QA_Q3_CONN_CONNECTED;
  clients[4]->sess.session_team = QA_Q3_BASE_TEAM_BLUE;
  CHECK(g->choose_team(g->ctx, 5) == QA_Q3_BASE_TEAM_BLUE); /* 2 v 2: scores tie, BLUE */
  qa_q3_base_player_state_slots_set(&level->team_scores, QA_Q3_BASE_TEAM_BLUE, 5);
  CHECK(g->choose_team(g->ctx, 5) == QA_Q3_BASE_TEAM_RED);
  CHECK(g->choose_team(g->ctx, 4) == QA_Q3_BASE_TEAM_BLUE); /* ignoring client 4 leaves 1 blue v 2 red */
  clients[2]->pers.connected = QA_Q3_CONN_DISCONNECTED;
  qa_q3_base_player_state_slots_set(&level->team_scores, QA_Q3_BASE_TEAM_BLUE, 0);
  CHECK(g->choose_team(g->ctx, 7) == QA_Q3_BASE_TEAM_RED); /* 2 blue v 1 red */

  /* Knowledge without an arsenal: createBotArsenalKnowledge over botlib weapon
   * info (tests/app/q3-runtime-warnings.test.ts: refresh never asks WP_NONE). */
  qa_source_bot_library_options_bots_a library_options;
  memset(&library_options, 0, sizeof library_options);
  library_options.ctx = &sg_library_sentinel;
  library_options.read_file = sg_library_read;
  library_options.random = (qa_bot_random){sg_library_int, &sg_library_sentinel};
  library_options.milliseconds = sg_library_ms;
  library_options.print = sg_library_print;
  library_options.open_log = sg_library_log;
  qa_bot_library_bots_a *library = calloc(1, sizeof *library);
  qa_bot_library_bots_a_init(library, &library_options);
  CHECK(qa_weapon_ai_setup(&library->weapons, NULL) == QA_WEAPON_LOAD_NO_ERROR);
  int64_t handle = qa_weapon_ai_allocate_state(&library->weapons);
  const qa_bot_arsenal_interface_bots_a *knowledge = g->knowledge;
  CHECK(knowledge == &bot_game->source_knowledge.knowledge);
  sg_out_of_range = 0;
  for (int refresh = 0; refresh < 4; refresh++) {
    qa_weapon_info_bots_a info;
    CHECK(knowledge->weapon_info(knowledge->ctx, library, handle, 1, &info) && strcmp(info.name, "Gauntlet") == 0);
    CHECK(knowledge->weapon_info(knowledge->ctx, library, handle, 5, &info) && strcmp(info.name, "Rocket Launcher") == 0);
  }
  CHECK(sg_out_of_range == 0);
  qa_weapon_info_bots_a none_info;
  CHECK(!qa_weapon_ai_get_weapon_info(&library->weapons, handle, 0, &none_info));
  CHECK(sg_out_of_range == 1);
  /* Candidate shape: gauntlet melee with 60 range; every weapon its own role. */
  size_t candidate_count = 0;
  qa_bot_weapon_knowledge_bots_a *candidates =
      bot_game->source_knowledge.data.candidates(bot_game->source_knowledge.data.ctx, library, handle, &candidate_count);
  CHECK(candidate_count == 3);
  if (candidate_count == 3) {
    CHECK(candidates[0].melee && candidates[0].has_maximum_range && candidates[0].maximum_range == 60);
    CHECK(!candidates[1].melee && !candidates[1].has_maximum_range && candidates[1].personality_role == 2);
    CHECK(candidates[2].personality_role == 5 && !candidates[2].has_supply);
  }
  free(candidates);
  qa_q3_bot_game_bots_a_destroy(bot_game);

  /* With an arsenal binding: pickups, wrapped knowledge, source weapon overrides. */
  qa_bot_arsenal_interface_bots_a arsenal_knowledge = {&sg_knowledge_sentinel, sg_k_utility, sg_k_choose,
                                                       sg_k_activation, sg_k_tactics, sg_k_aggression,
                                                       sg_k_update, sg_k_info};
  qa_q3_bot_arsenal_binding_bots_a binding = {&arsenal_knowledge, &sg_arsenal_sentinel, sg_source_weapon,
                                               sg_source_weapon_state};
  CHECK(qa_q3_bot_game_bots_a_init(bot_game, owner, &host, sg_insert, &sg_insert_sentinel, &binding));
  CHECK(g->pickups != NULL && g->knowledge != &arsenal_knowledge);
  knowledge = g->knowledge;
  e = g->entity(g->ctx, 1);
  CHECK(e.state.weapon == 8 && e.player_state->weapon == 8 && e.player_state->weapon_state == 2);
  qa_bot_observed_entity_bots_a plain = g->entity(g->ctx, (int64_t)health_slot);
  CHECK(!plain.has_player && plain.state.weapon == qa_int32(qa_q3_entity_pool_at(pool, health_slot)->s.weapon));
  qa_pickup_supply_preview accepted = {true, NULL, 0, NULL, 0};
  CHECK(knowledge->pickup_utility(knowledge->ctx, library, NULL, &accepted) == 11);
  CHECK(knowledge->choose_weapon(knowledge->ctx, library, NULL) == 5);
  CHECK(knowledge->activation_weapon(knowledge->ctx, library, NULL) == 3);
  CHECK(knowledge->tactics(knowledge->ctx, 6).weakness == 6);
  CHECK(knowledge->aggression(knowledge->ctx, NULL) == 42);
  qa_weapon_info_bots_a wrapped_info;
  CHECK(knowledge->weapon_info(knowledge->ctx, library, handle, 4, &wrapped_info) && wrapped_info.number == 4);
  CHECK(!knowledge->weapon_info(knowledge->ctx, library, handle, 3, &wrapped_info));
  /* updateInventory: arsenal first, then ARMOR from stats and the item inventory. */
  qa_bot_state_store_bots_a store;
  qa_bot_state_store_bots_a_init(&store, QA_Q3_BASE_PRODUCT_BASEQ3);
  qa_bot_state_bots_a *state = qa_bot_state_store_acquire(&store, 1, qa_app_q3_source_subsystems_owner_memory(owner));
  const qa_q3_base_stat_schema *schema = qa_q3_base_stat_schema_for(QA_Q3_BASE_PRODUCT_BASEQ3);
  state->src->cur_ps.stats[(int)schema->armor] = 33;
  state->src->cur_ps.stats[(int)schema->health] = 71;
  knowledge->update_inventory(knowledge->ctx, state);
  CHECK(sg_arsenal_updates == 1);
  CHECK(state->src->inventory[QA_INV_ARMOR] == 33 && state->src->inventory[QA_INV_HEALTH] == 71);
  qa_bot_state_store_bots_a_destroy(&store);
  /* Pickups: weapon and ammo item goals; candidates are inspect() over every
   * in-use item entity, and inspect is observeSupply plus entity fields. */
  const qa_bot_pickup_observations_bots_a *pickups = g->pickups;
  CHECK(pickups->owns_item_goal(pickups->ctx, 1, (int64_t)machinegun_slot));
  CHECK(!pickups->owns_item_goal(pickups->ctx, 1, (int64_t)health_slot));
  CHECK(!pickups->owns_item_goal(pickups->ctx, 1, 900));
  qa_q3_item_lifecycle_context context =
      qa_app_q3_item_lifecycle_host_context(qa_app_q3_source_subsystems_owner_items(owner));
  /* Items observe supply only once FinishSpawningItem has armed their touch
   * trigger; this fixture has no collision world, so arm them directly. */
  machinegun->touch = context.touch_callback;
  machinegun->r.contents = 0x40000000; /* CONTENTS_TRIGGER */
  machinegun->s.modelindex = 10;      /* itemList index, published by FinishSpawningItem */
  qa_q3_entity_pool_at(pool, health_slot)->touch = context.touch_callback;
  qa_q3_entity_pool_at(pool, health_slot)->s.modelindex = 5;
  size_t expected_count = 0;
  int64_t expected_entity = -1;
  for (double n = 0; n < pool->num_entities; n++) {
    qa_q3_game_entity *entity = qa_q3_entity_pool_at(pool, n);
    if (!qa_q3_game_entity_inuse(entity) || entity->item == NULL)
      continue;
    qa_q3_supply_observation supply;
    if (qa_q3_observe_q3_supply(entity, bot, context, &supply)) {
      expected_count++;
      expected_entity = (int64_t)n;
    }
  }
  size_t pickup_count = 0;
  const qa_bot_observed_pickup_bots_a *observed = pickups->candidates(pickups->ctx, 1, &pickup_count);
  CHECK(pickup_count == expected_count);
  CHECK(expected_count == 1 && expected_entity == (int64_t)machinegun_slot);
  if (pickup_count == 1) {
    CHECK(observed[0].entity == (int64_t)machinegun_slot && strcmp(observed[0].name, "weapon_machinegun") == 0);
    CHECK(observed[0].origin.x == qa_q3_base_entity_shared_current_origin(&machinegun->r).x);
    CHECK(observed[0].preview.accepted && observed[0].preview.ammo_count == 2);
    CHECK(observed[0].preview.ammo != sg_receipts && observed[0].preview.ammo[1].given == 5);
    CHECK(qa_same_actor(observed[0].observation.actor, qa_q3_game_entity_actor(machinegun).id));
  }
  qa_bot_observed_pickup_bots_a one;
  CHECK(pickups->inspect(pickups->ctx, 1, qa_q3_game_entity_actor(machinegun).id, &one) == (expected_count == 1));
  CHECK(!pickups->inspect(pickups->ctx, 1, qa_q3_game_entity_actor(qa_q3_entity_pool_at(pool, health_slot)).id, &one));
  CHECK(!pickups->inspect(pickups->ctx, 5, qa_q3_game_entity_actor(machinegun).id, &one)); /* recipient not in use */
  qa_q3_bot_game_bots_a_destroy(bot_game);
  free(bot_game);
  qa_bot_library_shutdown_bots_a(library);
  qa_bot_library_bots_a_destroy(library);
  free(library);

  qa_app_q3_source_subsystems_owner_destroy(owner);
  qa_cvar_registry_destroy(cvars);
  qa_shared_inventory_table_destroy(inventory);
  qa_gameplay_authority_destroy(combat);
  qa_actor_callback_table_destroy(callbacks);
  qa_shared_body_table_destroy(bodies);
  qa_session_actor_registry_destroy(actors);
  qa_identity_owner_destroy(&identity);
  for (size_t i = 0; i < 1024; i++) {
    free(sg_cs[i]);
    sg_cs[i] = NULL;
  }
}
