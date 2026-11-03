/*
 * C checks for src/bots/behavior/q3/ai-decision.ts, ai-combat.ts and
 * ai-navigation.ts.
 *
 *  - test_arena_items / test_arena_attack_move reproduce the ai-combat and
 *    ai-navigation cases of tests/bots/behavior/arena.test.ts (Q3 item inventory,
 *    battle item use, weapon choice while raising, attack-move direction per
 *    arsenal) over a synthetic host instead of the retail arena. The Q2 arsenal
 *    case needs Q2 retail content and is not reproduced.
 *  - test_differential runs botDeathmatchAI for 400 thinks per scenario over a
 *    deterministic host whose every answer comes from a call-ordered LCG and
 *    whose every call, argument and resulting state is folded into an FNV hash.
 *    The expected hashes come from the TS modules run under bun on the identical
 *    host (scratch harness oracle.ts); any reordered random draw, missed
 *    Math.fround or changed call sequence changes every later hash.
 * Every host callback checks its sentinel context.
 * GPL-2.0-or-later.
 */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/bots/behavior/q3/ai_combat.h"
#include "qa/bots/behavior/q3/ai_context.h"
#include "qa/bots/behavior/q3/ai_decision.h"
#include "qa/bots/behavior/q3/ai_navigation.h"
#include "qa/bots/behavior/q3/library.h"
#include "qa/content/q3/base/game/memory.h"
#include "qa/contracts/identity.h"
#include "qa/core/cvars/index.h"
#include "qa/core/game_numeric.h"
#include "qa/core/math.h"

static int failures = 0;
static int checks = 0;
#define CHECK(cond)                                                                                \
  do {                                                                                             \
    checks++;                                                                                      \
    if (!(cond)) {                                                                                 \
      failures++;                                                                                  \
      fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);                     \
    }                                                                                              \
  } while (0)

/* ---- deterministic host ---- */

enum { ENTITY_COUNT = 32, MAX_SNAP = 4 };

typedef struct scenario {
  int32_t seed;
  qa_q3_base_product product;
  int gametype, frames;
  const char *skill, *acc, *aim;
  int team;
  const char *map;
} scenario;

typedef struct fixture {
  const void *sentinel;
  uint32_t hash, lcg;
  int64_t seq;
  bool dump;
  qa_game_random random;
  const scenario *sc;
  qa_identity_owner identity;
  qa_cvar_registry *cvars;
  char *configstrings[1024];
  qa_bot_observed_entity_bots_a ents[ENTITY_COUNT];
  qa_q3_base_player_state player_states[8];
  char names[8][16];
  char userinfo[64];
  qa_q3_game_memory memory;
  qa_bot_arsenal_interface_bots_a knowledge;
  qa_source_bot_game_bots_a game;
  qa_bot_navigation_bots_a nav;
  qa_aas_bsp_entities bsp;
  qa_game_ai_host_bots_a host;
  qa_bot_library_bots_a library;
  qa_goal_world world;
  qa_game_ai_context_bots_a ctx;
  char file_text[2048];
  int64_t snap[MAX_SNAP];
  size_t snap_count;
  /* arena attack-move override */
  bool record_directions;
  double directions[16];
  size_t direction_count;
  int knowledge_mode; /* 0 differential, 1 gauntlet, 2 machinegun, 3 remapped melee */
} fixture;

static bool dump_enabled = false;

static fixture *of(void *ctx) {
  fixture *f = ctx;
  CHECK(f != NULL && f->sentinel == f);
  return f;
}

static void mix(fixture *f, int32_t w) { f->hash = (f->hash ^ (uint32_t)w) * 16777619u; }
static void mixd(fixture *f, double d) {
  uint64_t bits;
  memcpy(&bits, &d, sizeof bits);
  mix(f, (int32_t)(uint32_t)(bits & 0xffffffffu));
  mix(f, (int32_t)(uint32_t)(bits >> 32));
}
static void mixv(fixture *f, qa_vec3 v) {
  mixd(f, v.x);
  mixd(f, v.y);
  mixd(f, v.z);
}
static void mixs(fixture *f, const char *s) {
  for (; *s; s++)
    mix(f, (unsigned char)*s);
  mix(f, -1);
}
static int32_t hr(fixture *f) {
  f->lcg = (f->lcg * 1103515245u + 12345u) & 0x7fffffffu;
  f->seq++;
  return (int32_t)((f->lcg >> 16) & 0x7fff);
}
static void trace_log(fixture *f, const char *tag) {
  if (f->dump)
    fprintf(stderr, "%lld %s H=%x\n", (long long)f->seq, tag, f->hash);
}
static qa_vec3 vec(double x, double y, double z) { return qa_math_vec3(x, y, z); }

/* JS remainder of a non-negative LCG draw: plain C %. */
static const char *const FILE_NAMES[] = {"weapons.c", "items.c", "iw.c", "syn.c", "rnd.c",
                                         "rchat.c",   "match.c", "chat.c", "bots/t_c.c"};
static const char *const FILE_TEXTS[] = {
    "projectileinfo { name \"p\" damage 50 radius 120 } weaponinfo { name \"Gun\" number 2 projectile \"p\" speed 900 }",
    "iteminfo \"item_health\" { name \"Health\" modelindex 5 respawntime 35 mins {-15,-15,-15} maxs {15,15,15} }\n"
    "iteminfo \"item_quad\" { name \"Quad Damage\" modelindex 6 respawntime 120 mins {-15,-15,-15} maxs {15,15,15} }\n"
    "iteminfo \"team_CTF_redflag\" { name \"Red Flag\" modelindex 7 mins {-15,-15,-15} maxs {15,15,15} }\n"
    "iteminfo \"team_CTF_blueflag\" { name \"Blue Flag\" modelindex 8 mins {-15,-15,-15} maxs {15,15,15} }\n"
    "iteminfo \"team_CTF_neutralflag\" { name \"Neutral Flag\" modelindex 9 mins {-15,-15,-15} maxs {15,15,15} }\n"
    "iteminfo \"team_redobelisk\" { name \"Red Obelisk\" modelindex 10 mins {-15,-15,-15} maxs {15,15,15} }\n"
    "iteminfo \"team_blueobelisk\" { name \"Blue Obelisk\" modelindex 11 mins {-15,-15,-15} maxs {15,15,15} }\n"
    "iteminfo \"team_neutralobelisk\" { name \"Neutral Obelisk\" modelindex 12 mins {-15,-15,-15} maxs {15,15,15} }\n",
    "weight \"Health\" { return 100; } weight \"Quad Damage\" { return 80; } ",
    "",
    "",
    "",
    "128 { 0, \": \", 2 = (200, 0); }",
    "chat \"bot\" { type \"game_enter\" { \"hello\"; } type \"death_suicide\" { \"oops\"; } type \"kill_insult\" { "
    "\"gotcha\"; } type \"hit_talking\" { \"ow\"; } type \"random_misc\" { \"hmm\"; } type \"level_start\" { \"go\"; } "
    "type \"level_end\" { \"done\"; } }",
    "skill 1 { 0 \"T\" 1 \"male\" 2 SK2 3 \"ww.c\" 4 0.5 5 360 6 0.2 7 ACC 16 AIM 21 \"chat.c\" 22 \"bot\" 23 400 24 0.4 "
    "25 0.4 26 0.4 27 0.4 28 0.4 29 0.4 30 0.4 31 0.4 32 0.4 33 0.4 34 0.4 35 0.4 36 0.3 37 0.6 38 0.8 39 0.5 40 "
    "\"iw.c\" 41 0.6 42 0.5 43 0.5 44 0.7 45 0.2 46 0.8 47 0.6 48 0 }",
};
static const char *const BSP_TEXT =
    "{\n\"classname\" \"worldspawn\"\n}\n{\n\"classname\" \"item_health\"\n\"origin\" \"100 100 0\"\n}\n{\n\"classname\" "
    "\"item_quad\"\n\"origin\" \"-200 50 0\"\n}\n{\n\"classname\" \"team_CTF_redflag\"\n\"origin\" \"500 0 0\"\n}\n{\n"
    "\"classname\" \"team_CTF_blueflag\"\n\"origin\" \"-500 0 0\"\n}\n{\n\"classname\" \"team_CTF_neutralflag\"\n\"origin\" "
    "\"0 500 0\"\n}\n{\n\"classname\" \"team_redobelisk\"\n\"origin\" \"600 0 0\"\n}\n{\n\"classname\" "
    "\"team_blueobelisk\"\n\"origin\" \"-600 0 0\"\n}\n{\n\"classname\" \"team_neutralobelisk\"\n\"origin\" \"0 600 "
    "0\"\n}\n{\n\"classname\" \"func_door\"\n\"model\" \"*1\"\n\"targetname\" \"door1\"\n\"origin\" \"0 0 0\"\n}\n{\n"
    "\"classname\" \"func_button\"\n\"model\" \"*2\"\n\"target\" \"door1\"\n\"angle\" \"90\"\n\"health\" \"HL\"\n}\n{\n"
    "\"classname\" \"func_door\"\n\"model\" \"*3\"\n\"targetname\" \"door3\"\n\"health\" \"5\"\n}\n{\n\"classname\" "
    "\"trigger_multiple\"\n\"model\" \"*4\"\n\"target\" \"door3\"\n}\n{\n\"classname\" \"target_relay\"\n\"target\" "
    "\"door4\"\n\"targetname\" \"door1\"\n}\n{\n\"classname\" \"item_botroam\"\n\"origin\" \"9 9 9\"\n\"model\" "
    "\"*5\"\n}\n";

/* TS String.replace: the first occurrence only. */
static void replace_first(char *text, size_t size, const char *from, const char *to) {
  char *at = strstr(text, from);
  if (at == NULL)
    return;
  char tail[2048];
  snprintf(tail, sizeof tail, "%s", at + strlen(from));
  snprintf(at, size - (size_t)(at - text), "%s%s", to, tail);
}

static const uint8_t *read_file(void *ctx, const char *path, size_t *out_len) {
  fixture *f = of(ctx);
  const char *key = strncmp(path, "botfiles/", 9) == 0 ? path + 9 : path;
  for (size_t i = 0; i < sizeof FILE_NAMES / sizeof FILE_NAMES[0]; i++) {
    if (strcmp(FILE_NAMES[i], key) != 0)
      continue;
    snprintf(f->file_text, sizeof f->file_text, "%s", FILE_TEXTS[i]);
    replace_first(f->file_text, sizeof f->file_text, "SK2", f->sc->skill);
    replace_first(f->file_text, sizeof f->file_text, "ACC", f->sc->acc);
    replace_first(f->file_text, sizeof f->file_text, "AIM", f->sc->aim);
    *out_len = strlen(f->file_text);
    return (const uint8_t *)f->file_text;
  }
  return NULL;
}

static int64_t random_next_int(void *ctx) { return qa_game_random_rand(&of(ctx)->random); }
static double milliseconds(void *ctx) {
  (void)of(ctx);
  return 0;
}
static void library_print(void *ctx, int severity, const char *text) {
  fixture *f = of(ctx);
  mix(f, severity);
  mixs(f, text);
  trace_log(f, "libprint");
}
static void client_command(void *ctx, int64_t client, const char *text) {
  fixture *f = of(ctx);
  mix(f, 0x43);
  mix(f, (int32_t)client);
  mixs(f, text);
  trace_log(f, "cmd");
}
static qa_bot_log_open_result open_log(void *ctx, const char *name) {
  (void)of(ctx);
  (void)name;
  qa_bot_log_open_result result;
  memset(&result, 0, sizeof result);
  return result;
}

static void engine_print(void *ctx, const char *text) {
  fixture *f = of(ctx);
  mix(f, 0x50);
  mixs(f, text);
  trace_log(f, "print");
}
static const char *engine_get_userinfo(void *ctx, int64_t client) {
  fixture *f = of(ctx);
  snprintf(f->userinfo, sizeof f->userinfo, "name\\Bot%lld", (long long)client);
  return f->userinfo;
}
static void engine_set_userinfo(void *ctx, int64_t client, const char *userinfo) {
  fixture *f = of(ctx);
  mix(f, 0x55);
  mix(f, (int32_t)client);
  mixs(f, userinfo);
  trace_log(f, "setuserinfo");
}
static void engine_send(void *ctx, int64_t client, const char *text) {
  fixture *f = of(ctx);
  mix(f, 0x53);
  mix(f, (int32_t)client);
  mixs(f, text);
}
static void engine_drop(void *ctx, int64_t client, const char *reason) {
  (void)of(ctx);
  (void)client;
  (void)reason;
}
static void engine_insert(void *ctx, const char *text) {
  fixture *f = of(ctx);
  mix(f, 0x49);
  mixs(f, text);
}
static void engine_append(void *ctx, const char *text) {
  fixture *f = of(ctx);
  mix(f, 0x4a);
  mixs(f, text);
}

static const char *configstring_get(void *ctx, double index) {
  fixture *f = of(ctx);
  int i = (int)index;
  return i >= 0 && i < 1024 && f->configstrings[i] != NULL ? f->configstrings[i] : "";
}
static void configstring_set(void *ctx, double index, const char *value) {
  fixture *f = of(ctx);
  int i = (int)index;
  free(f->configstrings[i]);
  f->configstrings[i] = strdup(value);
}

static qa_q3_base_server_trace_result world_trace(void *ctx, qa_q3_base_server_trace_query q) {
  fixture *f = of(ctx);
  int32_t r = hr(f);
  mix(f, 0x54);
  mixv(f, q.start);
  mixv(f, q.end);
  mix(f, q.shape.kind == QA_Q3_BASE_SHAPE_BOX ? 1 : 0);
  mixv(f, q.shape.kind == QA_Q3_BASE_SHAPE_BOX ? q.shape.mins : vec(0, 0, 0));
  mixv(f, q.shape.kind == QA_Q3_BASE_SHAPE_BOX ? q.shape.maxs : vec(0, 0, 0));
  mix(f, (int32_t)q.pass_entity_num);
  mix(f, (int32_t)q.mask);
  trace_log(f, "trace");
  if (f->dump)
    fprintf(stderr, "  T %a %a %a -> %a %a %a pass=%d mask=%d kind=%d\n", q.start.x, q.start.y, q.start.z,
            q.end.x, q.end.y, q.end.z, (int)q.pass_entity_num, (int)q.mask, (int)q.shape.kind);
  double fraction = r % 4 == 0 ? 0.5 : r % 16 == 1 ? 0 : 1;
  qa_q3_base_server_trace_result result;
  memset(&result, 0, sizeof result);
  result.fraction = fraction;
  result.end = vec(q.start.x + (q.end.x - q.start.x) * fraction, q.start.y + (q.end.y - q.start.y) * fraction,
                   q.start.z + (q.end.z - q.start.z) * fraction);
  result.entity_num = fraction < 1 ? (r >> 4) % 10 : 1023;
  result.solidity = r % 32 == 3 ? QA_Q3_BASE_SOLID_START_SOLID : QA_Q3_BASE_SOLID_CLEAR;
  result.contents = 8;
  return result;
}
static int64_t contents_for(fixture *f, int32_t tag, qa_vec3 p, int64_t pass) {
  int32_t r = hr(f);
  mix(f, tag);
  mixv(f, p);
  mix(f, (int32_t)pass);
  trace_log(f, "pc");
  return r % 8 == 0 ? 32 : r % 8 == 1 ? 64 : r % 64 == 2 ? 8 : 0;
}
static int64_t world_point_contents(void *ctx, qa_vec3 point, int64_t pass) {
  return contents_for(of(ctx), 0x70, point, pass);
}
static int64_t host_point_contents(void *ctx, qa_vec3 point) { return contents_for(of(ctx), 0x68, point, -9); }

static int64_t game_type(void *ctx) { return of(ctx)->sc->gametype; }
static int64_t game_max_clients(void *ctx) {
  (void)of(ctx);
  return 8;
}
static int64_t game_entity_count(void *ctx) {
  (void)of(ctx);
  return ENTITY_COUNT;
}
static double clock_zero(void *ctx) {
  (void)of(ctx);
  return 0;
}
static bool game_observable(void *ctx, int64_t number) {
  fixture *f = of(ctx);
  return f->ents[number].present && f->ents[number].linked && !f->ents[number].hidden;
}
static qa_bot_observed_entity_bots_a game_entity(void *ctx, int64_t number) {
  fixture *f = of(ctx);
  CHECK(number >= 0 && number < ENTITY_COUNT);
  return f->ents[number];
}
static int64_t game_model_index(void *ctx, const char *name) {
  (void)of(ctx);
  return (int64_t)strlen(name);
}
static int64_t game_choose_team(void *ctx, int64_t client) {
  (void)of(ctx);
  (void)client;
  return 1;
}
static void game_client(void *ctx, int64_t client) {
  (void)of(ctx);
  (void)client;
}
static void game_void(void *ctx) { (void)of(ctx); }
static void game_userinfo_changed(void *ctx, int64_t client) {
  fixture *f = of(ctx);
  mix(f, 0x75);
  mix(f, (int32_t)client);
}
static const char *game_connect(void *ctx, int64_t client, bool first, bool is_bot) {
  (void)of(ctx);
  (void)client;
  (void)first;
  (void)is_bot;
  return NULL;
}

/* Arsenal knowledge. */
static int64_t knowledge_choose(void *ctx, struct qa_bot_library_bots_a *library, struct qa_bot_state_bots_a *state) {
  fixture *f = of(ctx);
  (void)library;
  (void)state;
  int32_t r = hr(f);
  mix(f, 0x57);
  trace_log(f, "choose");
  return 1 + r % 9;
}
static int64_t knowledge_activation(void *ctx, struct qa_bot_library_bots_a *library,
                                    struct qa_bot_state_bots_a *state) {
  fixture *f = of(ctx);
  (void)library;
  (void)state;
  int32_t r = hr(f);
  mix(f, 0x41);
  trace_log(f, "activation");
  return r % 4 == 0 ? -1 : 1 + r % 9;
}
static qa_bot_weapon_tactics_bots_a knowledge_tactics(void *ctx, int64_t w) {
  fixture *f = of(ctx);
  qa_bot_weapon_tactics_bots_a t;
  memset(&t, 0, sizeof t);
  if (f->knowledge_mode != 0) {
    t.melee = f->knowledge_mode == 1 ? w == 1 : f->knowledge_mode == 3 ? w == 7 : false;
    return t;
  }
  t.melee = w == 1;
  t.has_max_range = w == 3;
  t.max_range = 500;
  t.has_aim_accuracy = w == 4;
  t.aim_accuracy = 8;
  t.has_aim_skill = w == 5;
  t.aim_skill = 17;
  t.weakness = w == 2 ? 60 : 0;
  t.predict_occluded_splash = w == 5;
  t.close_range_aim_penalty = w == 2 || w == 7;
  return t;
}
static double knowledge_aggression(void *ctx, struct qa_bot_state_bots_a *state) {
  (void)of(ctx);
  return (double)((state->src->client * 7 + state->src->inventory[29] * 3 + state->src->weapon_num * 11) % 100);
}
static void knowledge_update(void *ctx, struct qa_bot_state_bots_a *state) {
  (void)of(ctx);
  qa_update_q3_bot_inventory_bots_a(state);
}
static bool knowledge_weapon_info(void *ctx, struct qa_bot_library_bots_a *library, int64_t handle, int64_t w,
                                  qa_weapon_info_bots_a *out) {
  fixture *f = of(ctx);
  (void)library;
  (void)handle;
  memset(out, 0, sizeof *out);
  if (f->knowledge_mode != 0) {
    /* Arena: only the active weapon of each case has loaded information. */
    int64_t active = f->knowledge_mode == 1 ? 1 : f->knowledge_mode == 2 ? 2 : 7;
    if (w != active)
      return false;
    out->valid = true;
    out->number = (int32_t)w;
    return true;
  }
  if (w < 0 || w > 13 || w == 12)
    return false;
  out->valid = true;
  out->number = (int32_t)w;
  out->offset = vec(10, (double)w, 4);
  out->speed = w == 5 ? 900 : w == 8 ? 2000 : 0;
  out->flags = w == 8 ? 1 : 0;
  out->horizontal_spread = (float)((double)w * 0.5);
  out->vertical_spread = (float)((double)w * 0.25);
  out->projectile_info.damage_type = w == 5 ? 2 : 1;
  out->projectile_info.radius = 120;
  out->projectile_info.damage = 100;
  return true;
}

/* Navigation. */
static bool nav_ready(void *ctx) {
  (void)of(ctx);
  return true;
}
static void nav_with_client(void *ctx, int64_t client, void (*think)(void *), void *think_ctx) {
  (void)of(ctx);
  (void)client;
  think(think_ctx);
}
static int64_t nav_point_area(void *ctx, qa_vec3 o) {
  fixture *f = of(ctx);
  int32_t r = hr(f);
  mix(f, 0x61);
  mixv(f, o);
  trace_log(f, "pointArea");
  return r % 5 == 0 ? 0 : 1 + (r >> 3) % 8;
}
static int64_t nav_one(void *ctx, qa_vec3 o, int64_t client) {
  (void)of(ctx);
  (void)o;
  (void)client;
  return 1;
}
static int64_t nav_fuzzy(void *ctx, qa_vec3 o) {
  (void)of(ctx);
  (void)o;
  return 1;
}
static qa_bot_navigation_area nav_area(void *ctx, int64_t n) {
  (void)of(ctx);
  qa_bot_navigation_area a = {n == 3 ? 1024 | (1 << 24) : 0, 0, 2, 0, n % 3};
  return a;
}
static qa_bot_area_crossing *nav_trace_areas(void *ctx, qa_vec3 a, qa_vec3 b, int64_t m, size_t *out_count) {
  fixture *f = of(ctx);
  int32_t r = hr(f);
  mix(f, 0x74);
  mixv(f, a);
  mixv(f, b);
  mix(f, (int32_t)m);
  trace_log(f, "traceAreas");
  size_t count = (size_t)(r % 3);
  qa_bot_area_crossing *out = malloc(sizeof *out * 3);
  for (size_t i = 0; i < count; i++) {
    out[i].area = 1 + (int64_t)i;
    out[i].point = vec(a.x, a.y, a.z - (double)i);
  }
  *out_count = count;
  return out;
}
static int64_t *nav_bbox_areas(void *ctx, qa_bounds b, size_t *out_count) {
  fixture *f = of(ctx);
  mix(f, 0x62);
  mixv(f, b.min);
  mixv(f, b.max);
  trace_log(f, "bbox");
  int64_t *out = malloc(sizeof *out * 4);
  for (int i = 0; i < 4; i++)
    out[i] = i + 1;
  *out_count = 4;
  return out;
}
static void nav_set_area(void *ctx, int64_t area, bool enabled) {
  fixture *f = of(ctx);
  mix(f, 0x65);
  mix(f, (int32_t)area);
  mix(f, enabled ? 1 : 0);
  trace_log(f, "setarea");
}
static double nav_travel_time(void *ctx, qa_area_travel_time_query q) {
  fixture *f = of(ctx);
  int32_t r = hr(f);
  mix(f, 0x72);
  mix(f, (int32_t)q.area);
  mixv(f, q.origin);
  mix(f, (int32_t)q.goal_area);
  mix(f, (int32_t)q.travel_flags);
  trace_log(f, "tt");
  return r % 4 == 0 ? 0 : r % 400;
}
static qa_route_result nav_route(void *ctx, qa_route_query q) {
  (void)of(ctx);
  (void)q;
  qa_route_result r = {QA_ROUTE_UNREACHABLE, 0, 0};
  return r;
}
static qa_predicted_route nav_predict_route(void *ctx, qa_predict_route_query q) {
  fixture *f = of(ctx);
  int32_t r = hr(f);
  mix(f, 0x52);
  mix(f, (int32_t)q.route.area);
  mixv(f, q.route.origin);
  mix(f, (int32_t)q.route.goal_area);
  mix(f, (int32_t)q.route.travel_flags);
  mix(f, (int32_t)q.maximum_areas);
  mix(f, (int32_t)q.maximum_time);
  mix(f, (int32_t)q.stop_event);
  mix(f, (int32_t)q.stop_contents);
  mix(f, (int32_t)q.stop_travel_flags);
  mix(f, (int32_t)q.stop_area);
  trace_log(f, "route");
  bool hit = r % 3 == 0;
  qa_predicted_route out;
  memset(&out, 0, sizeof out);
  out.succeeded = true;
  out.stop_event = hit ? 4 : 0;
  out.end_area = 1;
  out.end_contents = hit ? 1024 | ((1 + (r >> 3) % 4) << 24) : 0;
  out.time = 1;
  return out;
}
static qa_alternative_goal *nav_alternatives(void *ctx, qa_alternative_route_query q, size_t *out_count) {
  fixture *f = of(ctx);
  mix(f, 0x6c);
  mixv(f, q.start);
  mix(f, (int32_t)q.start_area);
  mixv(f, q.goal);
  mix(f, (int32_t)q.goal_area);
  mix(f, (int32_t)q.travel_flags);
  mix(f, (int32_t)q.maximum_goals);
  mix(f, (int32_t)q.type);
  trace_log(f, "alt");
  qa_alternative_goal *out = malloc(sizeof *out * 2);
  out[0] = (qa_alternative_goal){vec(11, 22, 33), 2, 1, 2, 3};
  out[1] = (qa_alternative_goal){vec(44, 55, 66), 5, 1, 2, 3};
  *out_count = 2;
  return out;
}
static void mix_goal(fixture *f, const qa_bot_goal *g) {
  mixv(f, g->origin);
  mix(f, (int32_t)g->area);
  mixv(f, g->mins);
  mixv(f, g->maxs);
  mix(f, (int32_t)g->entity);
  mix(f, (int32_t)g->number);
  mix(f, (int32_t)g->flags);
}
static void nav_move_to_goal(void *ctx, qa_bot_move_result *result, int64_t ms, qa_bot_goal goal, int64_t flags) {
  fixture *f = of(ctx);
  int32_t r = hr(f);
  mix(f, 0x4d);
  mix(f, (int32_t)ms);
  mix_goal(f, &goal);
  mix(f, (int32_t)flags);
  trace_log(f, "moveToGoal");
  static const int64_t table[] = {0, 0, 0, 1, 2, 4, 8, 16, 32, 256, 17};
  result->failure = r % 16 == 0;
  result->blocked = (r >> 4) % 6 == 0;
  result->block_entity = (r >> 7) % 14;
  result->type = (r >> 4) % 32 == 0 ? 8 : 0;
  result->flags = table[(r >> 9) % 11];
  result->weapon = 1 + r % 13;
  result->move_direction = r % 7 == 0 ? vec(0, 0, 0) : vec(1, 0.5, 0);
  result->ideal_view_angles = vec(5, r % 360, 2);
}
static bool nav_move_in_direction(void *ctx, int64_t ms, qa_vec3 d, double speed, int64_t type) {
  fixture *f = of(ctx);
  if (f->record_directions) {
    if (f->direction_count < 16)
      f->directions[f->direction_count++] = d.x;
    return true;
  }
  int32_t r = hr(f);
  mix(f, 0x6d);
  mix(f, (int32_t)ms);
  mixv(f, d);
  mixd(f, speed);
  mix(f, (int32_t)type);
  trace_log(f, "moveDir");
  return r % 4 != 0;
}
static bool nav_view_target(void *ctx, int64_t ms, qa_bot_goal goal, int64_t flags, double look, qa_vec3 *out) {
  fixture *f = of(ctx);
  int32_t r = hr(f);
  mix(f, 0x76);
  mix(f, (int32_t)ms);
  mixv(f, goal.origin);
  mix(f, (int32_t)flags);
  mixd(f, look);
  trace_log(f, "viewTarget");
  if (r % 2 == 0) {
    *out = vec(goal.origin.x, goal.origin.y + 10, goal.origin.z);
    return true;
  }
  return false;
}
static bool nav_predict_visible(void *ctx, qa_vec3 o, int64_t area, qa_bot_goal goal, int64_t flags, qa_vec3 *out) {
  fixture *f = of(ctx);
  int32_t r = hr(f);
  mix(f, 0x69);
  mixv(f, o);
  mix(f, (int32_t)area);
  mixv(f, goal.origin);
  mix(f, (int32_t)goal.area);
  mix(f, (int32_t)goal.entity);
  mix(f, (int32_t)flags);
  trace_log(f, "predictVis");
  if (r % 2 == 0) {
    *out = vec(o.x + 100, o.y, o.z);
    return true;
  }
  return false;
}
static bool nav_swimming(void *ctx, qa_vec3 o) {
  fixture *f = of(ctx);
  int32_t r = hr(f);
  mix(f, 0x77);
  mixv(f, o);
  trace_log(f, "swim");
  return r % 16 == 0;
}
static qa_vec3 nav_predict(void *ctx, qa_bot_movement_prediction q) {
  fixture *f = of(ctx);
  mix(f, 0x71);
  mix(f, (int32_t)q.entity_num);
  mixv(f, q.origin);
  mix(f, (int32_t)q.presence);
  mixv(f, q.velocity);
  mix(f, (int32_t)q.max_frames);
  mixd(f, q.frame_time);
  trace_log(f, "predict");
  return vec(q.origin.x + q.velocity.x * 0.1, q.origin.y + q.velocity.y * 0.1, q.origin.z);
}
static qa_bounds nav_presence(void *ctx, int64_t presence) {
  (void)of(ctx);
  (void)presence;
  qa_bounds b = {{-15, -15, -24}, {15, 15, 32}};
  return b;
}

static int64_t host_snapshot(void *ctx, int64_t client, int64_t sequence) {
  fixture *f = of(ctx);
  (void)client;
  return sequence < (int64_t)f->snap_count ? f->snap[sequence] : -1;
}
static const char *host_console(void *ctx, int64_t client) {
  (void)of(ctx);
  (void)client;
  return NULL;
}
static void host_user_command(void *ctx, int64_t client, const qa_q3_base_user_command *command) {
  (void)of(ctx);
  (void)client;
  (void)command;
}
static void host_check_spawn(void *ctx) { (void)of(ctx); }
static int64_t host_load_map(void *ctx, const char *name) {
  (void)of(ctx);
  (void)name;
  return 0;
}

/* Goal world. */
static qa_goal_area_info goal_area(void *ctx, int64_t n) {
  (void)of(ctx);
  qa_goal_area_info a = {0, n % 3};
  return a;
}
static int64_t goal_jump_pad(void *ctx, qa_vec3 o, qa_bounds b) {
  (void)of(ctx);
  (void)o;
  (void)b;
  return 0;
}
static qa_goal_drop_result goal_drop(void *ctx, qa_vec3 o, qa_bounds b) {
  (void)of(ctx);
  (void)b;
  qa_goal_drop_result r = {o, true};
  return r;
}
static qa_goal_reachable_position goal_best(void *ctx, qa_vec3 o, qa_bounds b) {
  (void)of(ctx);
  (void)b;
  qa_goal_reachable_position r = {1, o};
  return r;
}
static int64_t goal_reach_area(void *ctx, qa_vec3 o, int64_t client) {
  (void)of(ctx);
  (void)o;
  (void)client;
  return 1;
}
static qa_goal_route_result goal_route(void *ctx, const qa_goal_route_request *q) {
  fixture *f = of(ctx);
  int32_t r = hr(f);
  mix(f, 0x47);
  mix(f, (int32_t)q->area);
  mix(f, (int32_t)q->goal_area);
  mix(f, (int32_t)q->travel_flags);
  trace_log(f, "groute");
  qa_goal_route_result out = {r % 5 != 0, r % 5 == 0 ? 0 : 1 + r % 200};
  return out;
}
static double goal_trace(void *ctx, qa_vec3 s, qa_vec3 e, const qa_bounds *b, int64_t pass, int64_t mask) {
  (void)of(ctx);
  (void)s;
  (void)e;
  (void)b;
  (void)pass;
  (void)mask;
  return 1;
}
static int64_t goal_contents(void *ctx, qa_vec3 p) {
  (void)of(ctx);
  (void)p;
  return 0;
}
static int64_t goal_next(void *ctx, int64_t after) {
  (void)of(ctx);
  (void)after;
  return 0;
}
static qa_goal_entity_info goal_info(void *ctx, int64_t entity) {
  (void)of(ctx);
  (void)entity;
  qa_goal_entity_info i;
  memset(&i, 0, sizeof i);
  return i;
}
static int64_t goal_point_area(void *ctx, qa_vec3 o) {
  (void)of(ctx);
  (void)o;
  return 2;
}
static double memory_debug(void *ctx) {
  (void)of(ctx);
  return 0;
}
static void memory_print(void *ctx, const char *text) {
  (void)of(ctx);
  (void)text;
}
static void bsp_print(void *ctx, int severity, const char *text) {
  (void)of(ctx);
  (void)severity;
  (void)text;
}

static void register_cvar(fixture *f, const char *name, const char *value, double flags) {
  qa_cvar_snapshot snapshot;
  CHECK(qa_cvar_registry_register(f->cvars, name, value, flags, &snapshot));
  qa_cvar_read_free(&snapshot);
}

static void fixture_open(fixture *f, const scenario *sc) {
  memset(f, 0, sizeof *f);
  f->sentinel = f;
  f->sc = sc;
  f->hash = 0x811c9dc5u;
  f->lcg = (uint32_t)sc->seed;
  f->dump = dump_enabled && getenv("QA_Q3_AI_DUMP") != NULL;
  qa_game_random_init(&f->random, sc->seed);
  CHECK(qa_identity_owner_create(&f->identity, "oracle"));
  qa_cvar_registry_options options;
  memset(&options, 0, sizeof options);
  options.dialect = QA_COMMAND_DIALECT_Q3;
  options.context.session = f->identity.session;
  options.context.origin.kind = QA_COMMAND_ORIGIN_SERVER_CONSOLE;
  f->cvars = qa_cvar_registry_create(&options);
  char number[16];
  snprintf(number, sizeof number, "%d", sc->gametype);
  register_cvar(f, "g_gametype", number, 4);
  register_cvar(f, "sv_maxclients", "8", 4);
  register_cvar(f, "mapname", sc->map, 4);
  register_cvar(f, "bot_developer", "0", 0);
  register_cvar(f, "bot_challenge", sc->seed % 2 == 0 ? "1" : "0", 0);
  for (int c = 0; c < 8; c++) {
    char info[64];
    snprintf(info, sizeof info, "n\\Bot%d\\t\\%d\\model\\sarge", c, sc->team == 0 ? 0 : 1 + c % 2);
    f->configstrings[544 + c] = strdup(info);
  }
  f->configstrings[288 + 3] = strdup("sound/items/poweruprespawn.wav");
  f->configstrings[288 + 4] = strdup("*falling1.wav");
  f->configstrings[288 + 5] = strdup("sound/items/kamikazerespawn.wav");
  for (int n = 0; n < ENTITY_COUNT; n++) {
    qa_bot_observed_entity_bots_a *e = &f->ents[n];
    qa_q3_entity_state_init(&e->state);
    e->state.number = n;
    e->generation = 1;
    e->present = n < 28;
    e->linked = true;
    e->origin = vec(n * 40, n * -25, 0);
    e->angles = vec(0, n * 10, 0);
    e->bounds = (qa_bounds){vec(-15, -15, -24), vec(15, 15, 32)};
    e->contents = n == 12 ? 0x40000000 : 0;
    e->classname = n == 9 ? "team_redobelisk" : n == 10 ? "team_blueobelisk" : "misc";
    e->has_activator_frame = n == 9;
    e->activator_frame = 2;
    if (n >= 9 && n <= 13) {
      e->state.e_type = n == 12 ? 0 : 4;
      e->state.modelindex = n - 8;
    }
    if (n < 8) {
      CHECK(qa_q3_base_player_state_init(&f->player_states[n], QA_Q3_BASE_PRODUCT_BASEQ3, NULL));
      snprintf(f->names[n], sizeof f->names[n], "Bot%d", n);
      e->has_player = true;
      e->player_state = &f->player_states[n];
      e->player_connected = true;
      e->player_team = 1 + n % 2;
      e->player_name = f->names[n];
      e->state.e_type = 1;
    }
  }
  qa_q3_game_memory_init(&f->memory, f, memory_debug, memory_print);
  f->knowledge = (qa_bot_arsenal_interface_bots_a){.ctx = f,
                                                   .choose_weapon = knowledge_choose,
                                                   .activation_weapon = knowledge_activation,
                                                   .tactics = knowledge_tactics,
                                                   .aggression = knowledge_aggression,
                                                   .update_inventory = knowledge_update,
                                                   .weapon_info = knowledge_weapon_info};
  static qa_q3_config_string_store store;
  store = (qa_q3_config_string_store){.ctx = f, .get = configstring_get, .set = configstring_set};
  qa_source_bot_game_bots_a *game = &f->game;
  game->ctx = f;
  game->options.product = sc->product;
  game->options.cvars = f->cvars;
  game->options.configstrings = &store;
  game->options.engine = (qa_source_bot_engine_bots_a){.ctx = f,
                                                       .insert_ctx = f,
                                                       .print = engine_print,
                                                       .get_userinfo = engine_get_userinfo,
                                                       .set_userinfo = engine_set_userinfo,
                                                       .send_server_command = engine_send,
                                                       .drop_client = engine_drop,
                                                       .insert_console_command = engine_insert,
                                                       .append_console_command = engine_append};
  game->game_type = game_type;
  game->max_clients = game_max_clients;
  game->entity_count = game_entity_count;
  game->trace = world_trace;
  game->point_contents = world_point_contents;
  game->random = qa_source_bot_game_random_bots_a(&f->random);
  game->clock_time = clock_zero;
  game->clock_start_time = clock_zero;
  game->clock_intermission_time = clock_zero;
  game->memory = &f->memory;
  game->knowledge = &f->knowledge;
  game->observable = game_observable;
  game->entity = game_entity;
  game->model_index = game_model_index;
  game->choose_team = game_choose_team;
  game->activate_bot = game_client;
  game->exit_level = game_void;
  game->reset_podium_players = game_void;
  game->client_userinfo_changed = game_userinfo_changed;
  game->client_connect = game_connect;
  game->client_begin = game_client;
  f->nav = (qa_bot_navigation_bots_a){.ctx = f,
                                      .ready = nav_ready,
                                      .with_client = nav_with_client,
                                      .point_area = nav_point_area,
                                      .reachability_area = nav_one,
                                      .fuzzy_point_reachability_area = nav_fuzzy,
                                      .get_area = nav_area,
                                      .trace_areas = nav_trace_areas,
                                      .bbox_areas = nav_bbox_areas,
                                      .set_area_enabled = nav_set_area,
                                      .area_travel_time_to_goal = nav_travel_time,
                                      .route = nav_route,
                                      .predict_route = nav_predict_route,
                                      .alternative_route_goals = nav_alternatives,
                                      .move_to_goal = nav_move_to_goal,
                                      .move_in_direction = nav_move_in_direction,
                                      .movement_view_target = nav_view_target,
                                      .predict_visible_position = nav_predict_visible,
                                      .swimming = nav_swimming,
                                      .predict_client_movement_end = nav_predict,
                                      .presence_bounds = nav_presence};
  qa_aas_bsp_entities_init(&f->bsp);
  qa_aas_bsp_entities_configure(&f->bsp, bsp_print, f, NULL);
  char bsp_text[4096];
  snprintf(bsp_text, sizeof bsp_text, "%s", BSP_TEXT);
  replace_first(bsp_text, sizeof bsp_text, "HL", sc->seed % 3 == 0 ? "0" : "5");
  qa_aas_bsp_entities_load(&f->bsp, bsp_text);
  f->host = (qa_game_ai_host_bots_a){.ctx = f,
                                     .navigation = &f->nav,
                                     .bsp_entities = &f->bsp,
                                     .point_contents = host_point_contents,
                                     .get_snapshot_entity = host_snapshot,
                                     .get_console_message = host_console,
                                     .user_command = host_user_command,
                                     .insert_console_command = engine_insert,
                                     .check_bot_spawn = host_check_spawn,
                                     .load_map = host_load_map};
  qa_source_bot_library_options_bots_a library_options;
  memset(&library_options, 0, sizeof library_options);
  library_options.ctx = f;
  library_options.read_file = read_file;
  library_options.random = (qa_bot_random){.next_int = random_next_int, .ctx = f};
  library_options.milliseconds = milliseconds;
  library_options.print = library_print;
  library_options.client_command = client_command;
  library_options.open_log = open_log;
  qa_bot_library_bots_a_init(&f->library, &library_options);
  CHECK(qa_bot_library_setup_bots_a(&f->library) == 0);
  qa_bot_lib_vars_set(&f->library.variables, "g_gametype", number);
  memset(&f->world, 0, sizeof f->world);
  f->world.bsp_entities = &f->bsp;
  f->world.has_navigation = true;
  f->world.navigation = (qa_goal_navigation){.ctx = f,
                                            .area = goal_area,
                                            .best_reachable_from_jump_pad_area = goal_jump_pad,
                                            .drop_to_floor = goal_drop,
                                            .best_reachable_area = goal_best,
                                            .reachability_area = goal_reach_area,
                                            .route = goal_route};
  f->world.host = (qa_goal_world_host){
      .ctx = f, .trace = goal_trace, .point_contents = goal_contents, .next_entity = goal_next, .entity_info = goal_info};
  f->world.point_area = goal_point_area;
  f->world.point_area_ctx = f;
  CHECK(qa_bot_library_load_map_bots_a(&f->library, &f->world) == 0);
  CHECK(qa_game_ai_context_bots_a_init(&f->ctx, &f->game, &f->library, &f->host));
}

static void fixture_close(fixture *f) {
  qa_game_ai_context_bots_a_destroy(&f->ctx);
  qa_bot_library_shutdown_bots_a(&f->library);
  qa_bot_library_bots_a_destroy(&f->library);
  qa_aas_bsp_entities_destroy(&f->bsp);
  for (int n = 0; n < 8; n++)
    qa_q3_base_player_state_record_destroy(&f->player_states[n]);
  for (int i = 0; i < 1024; i++)
    free(f->configstrings[i]);
  qa_cvar_registry_destroy(f->cvars);
  qa_identity_owner_destroy(&f->identity);
}

static qa_bot_state_bots_a *fixture_bot(fixture *f, bool chat) {
  qa_bot_state_bots_a *st = qa_bot_state_store_acquire(&f->ctx.states, 0, &f->memory);
  st->src->client = 0;
  st->src->entity_num = 0;
  st->src->inuse = 1;
  st->src->character = (int32_t)qa_bot_character_load(&f->library.characters, "bots/t_c.c", 1);
  st->src->gs = (int32_t)qa_bot_goal_alloc_goal_state(&f->library.goals, 0);
  CHECK(qa_bot_goal_load_item_weights(&f->library.goals, st->src->gs, "iw.c") == 0);
  st->src->ws = (int32_t)qa_weapon_ai_allocate_state(&f->library.weapons);
  st->src->cs = (int32_t)qa_bot_chat_library_allocate(&f->library.chat);
  if (chat)
    CHECK(qa_bot_chat_library_load_chat_file(&f->library.chat, st->src->cs, qa_chat_text("chat.c"), qa_chat_text("bot")));
  st->src->ms = (int32_t)qa_bot_move_state_store_allocate(&f->library.move_states);
  qa_bot_state_set_text(st->src->team, sizeof st->src->team, "red");
  st->src->setup_count = 2;
  st->src->enemy = -1;
  st->src->last_killed_player = -1;
  st->src->enter_game_time = 0;
  return st;
}

/* ---- differential run ---- */

static void update_observation(fixture *f, int n, double time) {
  qa_bot_observed_entity_bots_a *e = &f->ents[n];
  double dx = hr(f) % 40, dy = hr(f) % 40;
  qa_vec3 o = vec(e->origin.x + dx - 20, e->origin.y + dy - 20, e->origin.z);
  e->origin = o;
  e->state.origin = o;
  e->state.pos.base = o;
  qa_bot_entity_update u;
  memset(&u, 0, sizeof u);
  u.type = e->state.e_type;
  u.flags = n % 3 == 0 ? (hr(f) % 4 == 0 ? 0x100 : 0x1000) : 0;
  u.origin = o;
  u.angles = e->angles;
  u.old_origin = o;
  u.mins = e->bounds.min;
  u.maxs = e->bounds.max;
  u.model_index = n >= 9 && n <= 13 ? n - 8 : 0;
  u.powerups = hr(f) % 8 == 0 ? (1 << 4) : hr(f) % 8 == 0 ? (1 << 7) : 0;
  qa_bot_entity_observations_update(&f->ctx.observations, n, &u, time);
}

static void vset(qa_bot_fvec3 *cell, qa_vec3 v) { *cell = qa_bot_fv3(v); }

static void force_patrol(fixture *f, qa_bot_state_bots_a *st) {
  qa_bot_waypoint_bots_a *p1 = qa_bot_create_way_point_bots_a(&f->ctx, "pa", vec(10, 10, 0), 2);
  qa_bot_waypoint_bots_a *p2 = qa_bot_create_way_point_bots_a(&f->ctx, "pb", vec(50, 50, 0), 3);
  if (p1 != NULL && p2 != NULL) {
    p1->next = p2;
    p2->prev = p1;
    qa_bot_free_waypoints_bots_a(&f->ctx, st->patrol_points);
    st->patrol_points = p1;
    st->current_patrol_point = p1;
  }
  st->src->patrol_flags = hr(f) % 2 == 0 ? 4 : 0;
}

static void mix_activation(fixture *f, const qa_bot_activate_goal_bots_a *a) {
  const qa_bot_activate_goal_source *s = a->src;
  mix(f, s->inuse ? 1 : 0);
  mixv(f, qa_bot_v3(s->goal.origin));
  mix(f, s->goal.area);
  mixv(f, qa_bot_v3(s->goal.mins));
  mixv(f, qa_bot_v3(s->goal.maxs));
  mix(f, s->goal.entity);
  mixd(f, s->time);
  mix(f, s->shoot ? 1 : 0);
  mixv(f, qa_bot_v3(s->target));
  mix(f, s->num_areas);
  for (int32_t i = 0; i < s->num_areas; i++)
    mix(f, s->areas[i]);
}

/* Goal cells as TS BotGoalState: float32/int32 stores. */
static void mix_goal_cell(fixture *f, const qa_bot_goal *g) {
  mixv(f, g->origin);
  mix(f, (int32_t)g->area);
  mixv(f, g->mins);
  mixv(f, g->maxs);
  mix(f, (int32_t)g->entity);
  mix(f, (int32_t)g->number);
  mix(f, (int32_t)g->flags);
}

/* Direct calls of functions the think loop reaches rarely, in the oracle's order. */
static void direct_calls(fixture *f, qa_bot_state_bots_a *st, float time) {
  qa_game_ai_context_bots_a *ctx = &f->ctx;
  qa_bot_state_source *s = st->src;
  int32_t u = hr(f) % 20;
  mix(f, 0x7700 + u);
  qa_bot_activate_goal_bots_a a;
  qa_bot_activate_goal_init(&a);
  qa_bot_goal g;
  memset(&g, 0, sizeof g);
  switch (u) {
  case 0: {
    int64_t entity = 9 + hr(f) % 5;
    mix(f, (int32_t)qa_bot_get_activate_goal_bots_a(ctx, st, entity, &a));
    mix_activation(f, &a);
    break;
  }
  case 1: {
    int64_t entity = hr(f) % 16;
    mix(f, qa_bot_func_button_activate_goal_bots_a(ctx, st, entity, &a) ? 1 : 0);
    mix_activation(f, &a);
    break;
  }
  case 2: {
    int64_t entity = hr(f) % 16;
    mix(f, qa_bot_trigger_multiple_activate_goal_bots_a(ctx, st, entity, &a) ? 1 : 0);
    mix_activation(f, &a);
    break;
  }
  case 3: {
    int64_t entity = hr(f) % 16;
    mix(f, qa_bot_func_door_activate_goal_bots_a(ctx, st, entity, &a) ? 1 : 0);
    mix_activation(f, &a);
    break;
  }
  case 4:
    s->last_air_time = (float)((double)time - 10);
    mix(f, qa_bot_go_for_air_bots_a(ctx, st, s->tfl, NULL, 400) ? 1 : 0);
    break;
  case 5:
    s->inventory[32] = 1;
    s->kamikaze_time = 0;
    qa_bot_use_kamikaze_bots_a(ctx, st);
    break;
  case 6:
    s->inventory[34] = 1;
    s->invulnerability_time = 0;
    qa_bot_use_invulnerability_bots_a(ctx, st);
    break;
  case 7:
    mix(f, qa_bot_has_persistant_powerup_and_weapon_bots_a(ctx, st) ? 1 : 0);
    break;
  case 8:
    g.origin = vec(hr(f) % 100, 0, 0);
    g.area = 3;
    qa_bot_go_camp_bots_a(ctx, st, &g);
    break;
  case 9:
    a.src->shoot = hr(f) % 2 == 0;
    a.src->goal.origin = qa_bot_fv3(vec(1, 2, 3));
    a.src->goal.area = 4;
    qa_bot_print_activate_goal_info_bots_a(ctx, st, &a, hr(f) % 16);
    break;
  case 10: {
    static const char *const names[] = {"team_CTF_redflag", "item_quad", "func_door", "nothing"};
    mix(f, qa_bot_goal_for_bsp_entity_bots_a(ctx, names[hr(f) % 4], &g) ? 1 : 0);
    mix_goal_cell(f, &g);
    break;
  }
  case 11: {
    static const char *const models[] = {"", "a", "ab", "abc"};
    g.origin = vec(hr(f) % 400, 0, 0);
    g.entity = -1;
    int64_t type = hr(f) % 2 == 0 ? 4 : 0;
    qa_bot_set_entity_num_for_goal_with_model_bots_a(ctx, &g, type, models[hr(f) % 4]);
    mix(f, (int32_t)g.entity);
    break;
  }
  case 12:
    qa_bot_dump_node_switches_bots_a(ctx, st);
    break;
  case 13:
    mix(f, qa_bot_find_way_point_bots_a(st->patrol_points, hr(f) % 2 == 0 ? "PB" : "pc") == NULL ? 0 : 1);
    break;
  case 14:
    mix(f, qa_bot_wants_to_camp_bots_a(ctx, st) ? 1 : 0);
    break;
  case 15:
    qa_bot_check_for_prox_mines_bots_a(ctx, st, &f->ents[15].state);
    mix(f, s->num_prox_mines);
    break;
  case 16:
    mix(f, qa_bot_get_air_goal_bots_a(ctx, st, &g) ? 1 : 0);
    mix_goal_cell(f, &g);
    break;
  case 17: {
    int64_t entity = 9 + hr(f) % 5;
    if (qa_bot_get_activate_goal_bots_a(ctx, st, entity, &a) != 0)
      mix(f, qa_bot_go_for_activate_goal_bots_a(ctx, st, &a) ? 1 : 0);
    mix_activation(f, &a);
    break;
  }
  case 18:
    g.area = 1 + hr(f) % 8;
    mix(f, qa_bot_ai_predict_obstacles_bots_a(ctx, st, &g) ? 1 : 0);
    break;
  case 19: {
    static const int64_t flags[] = {1, 128, 0, 5};
    g.flags = flags[hr(f) % 4];
    double dx = hr(f) % 20 - 10;
    g.origin = vec((double)s->origin.x + dx, s->origin.y, s->origin.z);
    g.mins = vec(-16, -16, -16);
    g.maxs = vec(16, 16, 16);
    g.area = s->area_num;
    g.number = 1 + hr(f) % 3;
    mix(f, qa_bot_reached_goal_bots_a(ctx, st, &g) ? 1 : 0);
    break;
  }
  default:
    break;
  }
}

static void hash_frame(fixture *f, qa_bot_state_bots_a *st, float time) {
  qa_bot_input input = qa_bot_action_get_input(&f->library.actions, 0, 0.1);
  mix(f, input.action_flags);
  mixv(f, input.direction);
  mixd(f, input.speed);
  mixv(f, input.view_angles);
  mix(f, input.weapon);
  qa_bot_action_reset_input(&f->library.actions, 0);
  direct_calls(f, st, time);
  qa_bot_state_source *s = st->src;
  int32_t ints[] = {s->tfl,          s->flags,          s->enemy,          s->ltg_type,
                    s->weapon_num,   s->last_health,    s->last_killed_player, s->num_deaths,
                    s->num_kills,    s->kamikaze_body,  s->num_prox_mines, s->last_enemy_area_num,
                    s->teammate,     s->patrol_flags,   s->red_flag_status, s->blue_flag_status,
                    s->neutral_flag_status, s->team_task_preference, s->num_prox_mines,
                    s->predict_obstacles_goal_area_num};
  for (size_t i = 0; i < sizeof ints / sizeof ints[0]; i++)
    mix(f, ints[i]);
  float floats[] = {s->ltg_time,           s->nbg_time,           s->stand_time,         s->chase_time,
                    s->enemy_visible_time, s->check_time,         s->attack_strafe_time, s->attack_crouch_time,
                    s->attack_jump_time,   s->enemy_sight_time,   s->enemy_death_time,   s->enemy_position_time,
                    s->defend_away_time,   s->defend_away_range,  s->rush_base_away_time, s->attack_away_time,
                    s->harvest_away_time,  s->arrive_time,        s->camp_time,          s->teleport_time,
                    s->weapon_change_time, s->fire_throttle_wait_time, s->fire_throttle_shoot_time,
                    s->not_blocked_time,   s->blocked_by_avoid_spot_time, s->respawn_time,
                    s->respawn_chat_time,  s->reached_alt_route_goal_time, s->team_goal_time,
                    s->lead_backup_time,   s->lead_message_time,  s->kamikaze_time,      s->invulnerability_time};
  for (size_t i = 0; i < sizeof floats / sizeof floats[0]; i++)
    mixd(f, floats[i]);
  qa_bot_fvec3 vectors[] = {s->ideal_viewangles, s->aim_target, s->enemy_velocity, s->enemy_origin,
                            s->last_enemy_origin};
  for (size_t i = 0; i < sizeof vectors / sizeof vectors[0]; i++)
    mixv(f, qa_bot_v3(vectors[i]));
  const qa_bot_goal_state_bots_a *goals[] = {&s->team_goal, &s->lead_team_goal};
  for (size_t i = 0; i < 2; i++) {
    mixv(f, qa_bot_v3(goals[i]->origin));
    mix(f, goals[i]->area);
    mix(f, goals[i]->entity);
  }
  mix(f, (int32_t)st->ai_node);
  mix(f, st->activate_stack == NULL ? -1 : (int32_t)(st->activate_stack - st->activate_goal_heap));
  mix(f, !st->has_scripted_order ? -1 : (int32_t)st->scripted_order.progress);
  for (size_t i = 0; i < QA_MAX_ACTIVATESTACK; i++) {
    const qa_bot_activate_goal_source *a = st->activate_goal_heap[i].src;
    mix(f, a->inuse ? 1 : 0);
    mixv(f, qa_bot_v3(a->goal.origin));
    mix(f, a->goal.area);
    mix(f, a->goal.entity);
    mixd(f, a->time);
    mix(f, a->shoot ? 1 : 0);
    mix(f, a->num_areas);
    mix(f, a->areas_disabled ? 1 : 0);
  }
  mix(f, (int32_t)f->ctx.node_switch_count);
  for (size_t i = 0; i < f->ctx.node_switch_count; i++)
    mixs(f, f->ctx.node_switches[i]);
  mix(f, f->random.seed);
  mix(f, (int32_t)f->lcg);
}

static const qa_ai_node_bots_a NODE_ORDER[] = {
    QA_AI_NODE_INTERMISSION, QA_AI_NODE_OBSERVER,     QA_AI_NODE_RESPAWN,       QA_AI_NODE_STAND,
    QA_AI_NODE_SEEK_ACTIVATE, QA_AI_NODE_SEEK_NBG,    QA_AI_NODE_SEEK_LTG,      QA_AI_NODE_BATTLE_FIGHT,
    QA_AI_NODE_BATTLE_CHASE, QA_AI_NODE_BATTLE_RETREAT, QA_AI_NODE_BATTLE_NBG};

static void run_scenario(const scenario *sc, uint32_t *out) {
  fixture *f = malloc(sizeof *f);
  dump_enabled = true;
  fixture_open(f, sc);
  qa_bot_setup_deathmatch_ai_bots_a(&f->ctx);
  qa_bot_state_bots_a *st = fixture_bot(f, true);
  qa_bot_player_state_source *ps = &st->src->cur_ps;
  bool mp = sc->product == QA_Q3_BASE_PRODUCT_MISSIONPACK;
  int health = 0, holdable = 1, weapons = mp ? 3 : 2, armor = mp ? 4 : 3, persist = 2;
  ps->stats[health] = 100;
  ps->stats[weapons] = 0xffff;
  ps->viewheight = 26;
  ps->ground_entity_num = 0;
  for (int c = 0; c < 14; c++)
    ps->ammo[c] = 50;
  float time = 0;
  for (int frame = 0; frame < sc->frames; frame++) {
    time = (float)(time + 0.1);
    f->ctx.time = time;
    qa_bot_library_start_frame_bots_a(&f->library, time);
    st->src->think_time = (float)0.1;
    for (int n = 1; n < 14; n++) {
      if (n >= 8) {
        if (hr(f) % 4 == 0)
          update_observation(f, n, time);
      } else {
        update_observation(f, n, time);
      }
    }
    update_observation(f, 0, time);
    int32_t pick = hr(f);
    ps->pm_type = pick % 37 == 0 ? 3 : pick % 41 == 0 ? 2 : pick % 53 == 0 ? 5 : 0;
    ps->weapon = 1 + hr(f) % 9;
    ps->weapon_state = hr(f) % 8 == 0 ? 1 : 0;
    double ox = hr(f) % 800 - 400, oy = hr(f) % 800 - 400, oz = hr(f) % 64;
    vset(&ps->origin, vec(ox, oy, oz));
    double vx = hr(f) % 100;
    vset(&ps->velocity, vec(vx, 0, 0));
    st->src->origin = ps->origin;
    vset(&st->src->eye, vec(ps->origin.x, ps->origin.y, (double)ps->origin.z + 26));
    st->src->velocity = ps->velocity;
    double pitch = hr(f) % 30, yaw = hr(f) % 360;
    vset(&st->src->viewangles, vec(pitch, yaw, 0));
    ps->viewangles = st->src->viewangles;
    st->src->area_num = 1 + hr(f) % 8;
    ps->pm_flags = hr(f) % 16 == 0 ? 1 : hr(f) % 16 == 0 ? 64 : 0;
    ps->pm_time = 10;
    ps->stats[health] = 10 + hr(f) % 120;
    ps->stats[armor] = hr(f) % 100;
    ps->stats[holdable] = hr(f) % 8 == 0 ? 26 + hr(f) % 3 : 0;
    if (mp)
      ps->stats[persist] = hr(f) % 8 == 0 ? 42 : 0;
    ps->persistant[1] = hr(f) % 4;
    ps->persistant[3] = 1;
    ps->e_flags = hr(f) % 16 == 0 ? 4 : 0;
    ps->powerups[1] = hr(f) % 8 == 0 ? 1 : 0;
    ps->powerups[7] = hr(f) % 16 == 0 ? 1 : 0;
    ps->powerups[9] = hr(f) % 16 == 0 ? 1 : 0;
    ps->generic1 = hr(f) % 16 == 0 ? 2 : 0;
    ps->external_event = 0;
    ps->external_event_parm = 0;
    int32_t forced = hr(f);
    qa_bot_state_source *s = st->src;
    if (forced % 7 == 0)
      s->enemy = (hr(f) % 10) - 1;
    if (forced % 11 == 0) {
      s->ltg_type = hr(f) % 14;
      double gx = hr(f) % 400, gy = hr(f) % 400;
      vset(&s->team_goal.origin, vec(gx, gy, 0));
      s->team_goal.area = 1 + hr(f) % 8;
      vset(&s->team_goal.mins, vec(-8, -8, -8));
      vset(&s->team_goal.maxs, vec(8, 8, 8));
      s->team_goal.entity = hr(f) % 8;
      s->team_goal_time = (float)((double)time + hr(f) % 30 - 5);
      s->team_message_time = hr(f) % 2 == 0 ? 0 : (float)((double)time - 0.5);
      s->teammate = 1 + hr(f) % 7;
      s->decisionmaker = hr(f) % 8;
      s->formation_dist = 300;
      s->teammate_visible_time = (float)((double)time - hr(f) % 70);
    }
    if (forced % 13 == 0) {
      s->lead_time = hr(f) % 2 == 0 ? 0 : (float)((double)time + hr(f) % 10 - 2);
      s->lead_teammate = 1 + hr(f) % 7;
      s->lead_visible_time = (float)((double)time - hr(f) % 3);
      s->lead_message_time = hr(f) % 2 == 0 ? -time : 0;
      s->lead_backup_time = 0;
    }
    if (forced % 17 == 0)
      s->last_air_time = (float)((double)time - hr(f) % 12);
    if (forced % 19 == 0) {
      s->kamikaze_body = hr(f) % 2 == 0 ? 0 : 14;
      s->blocked_by_avoid_spot_time = (float)((double)time + hr(f) % 4 - 1);
      s->num_prox_mines = hr(f) % 3;
      s->prox_mines[0] = 15;
      s->prox_mines[1] = 16;
    }
    if (forced % 23 == 0)
      st->ai_node = NODE_ORDER[hr(f) % 11];
    if (forced % 29 == 0)
      force_patrol(f, st);
    if (forced % 31 == 0) {
      int32_t o = hr(f) % 3;
      if (o == 0) {
        st->has_scripted_order = false;
      } else if (o == 1) {
        double px = hr(f) % 200, py = hr(f) % 200;
        st->has_scripted_order = true;
        st->scripted_order.order.kind = QA_BOT_ORDER_POINT;
        st->scripted_order.order.point = vec(px, py, 0);
        st->scripted_order.progress = QA_BOT_ORDER_IN_PROGRESS;
      } else {
        int64_t number = 1 + hr(f) % 7, generation = hr(f) % 8 == 0 ? 9 : 1;
        st->has_scripted_order = true;
        st->scripted_order.order.kind = QA_BOT_ORDER_FOLLOW;
        st->scripted_order.order.entity.number = number;
        st->scripted_order.order.entity.generation = generation;
        st->scripted_order.progress = QA_BOT_ORDER_IN_PROGRESS;
      }
    }
    if (forced % 43 == 0) {
      s->enemy_death_time = (float)((double)time - hr(f) % 3);
      s->enemy_suicide = hr(f) % 2 == 0;
      s->last_killed_player = s->enemy;
      s->killed_enemy_time = time;
    }
    if (forced % 47 == 0)
      qa_bot_chat_library_queue_console_message(&f->library.chat, s->cs, 1, qa_chat_text("Bot3: hello there friend"));
    if (forced % 59 == 0) {
      s->attack_chase_time = (float)((double)time + 1);
      s->last_enemy_area_num = 1 + hr(f) % 8;
      vset(&s->last_enemy_origin, vec(hr(f) % 100, 0, 0));
    }
    f->snap_count = 0;
    int32_t ev = hr(f);
    if (ev % 3 == 0) {
      static const int events[] = {60, 46, 47, 42, 45};
      int n = 20 + ev % 4;
      qa_bot_observed_entity_bots_a *e = &f->ents[n];
      e->event_time = frame + 1;
      int kind = (ev >> 3) % 5;
      e->state.e_type = 13 + events[kind];
      e->state.other_entity_num = hr(f) % 3;
      e->state.other_entity_num2 = hr(f) % 3;
      e->state.event_parm = kind == 1 ? 3 + hr(f) % 3 : kind == 2 ? hr(f) % 6 : hr(f) % 260;
      e->state.origin = vec(1, 2, 3);
      f->snap[f->snap_count++] = n;
    }
    if (ev % 5 == 0) {
      qa_bot_observed_entity_bots_a *e = &f->ents[15];
      e->state.e_type = 3;
      e->state.weapon = 4 + (ev >> 4) % 2 * 8;
      e->state.generic1 = 2;
      e->state.e_flags = 0;
      f->snap[f->snap_count++] = 15;
    }
    if (ev % 7 == 0) {
      f->ents[14].state.e_flags = 0x201;
      f->snap[f->snap_count++] = 14;
    }
    qa_bot_deathmatch_ai_bots_a(&f->ctx, st, 100);
    hash_frame(f, st, time);
    if (f->dump)
      fprintf(stderr, "%lld frame %d node=%d seed=%d lcg=%u H=%x\n", (long long)f->seq, frame, (int)st->ai_node,
              f->random.seed, f->lcg, f->hash);
    out[frame] = f->hash;
  }
  fixture_close(f);
  free(f);
}

/* Expected hashes after every 20th think, from the TS modules under bun. */
static const scenario SCENARIOS[] = {
    {7, QA_Q3_BASE_PRODUCT_BASEQ3, 0, 400, "0.5", "0.5", "0.5", 0, "q3dm1"},
    {8, QA_Q3_BASE_PRODUCT_BASEQ3, 0, 400, "0.9", "0.95", "0.99", 0, "q3dm1"},
    {9, QA_Q3_BASE_PRODUCT_BASEQ3, 0, 400, "0.3", "0.1", "0.2", 0, "q3tourney6"},
    {10, QA_Q3_BASE_PRODUCT_BASEQ3, 4, 400, "0.85", "0.5", "0.85", 1, "q3dm1"},
    {11, QA_Q3_BASE_PRODUCT_MISSIONPACK, 5, 400, "0.1", "0.5", "0.5", 1, "q3dm1"},
    {12, QA_Q3_BASE_PRODUCT_MISSIONPACK, 6, 400, "0.7", "0.5", "0.7", 1, "q3dm1"},
    {13, QA_Q3_BASE_PRODUCT_MISSIONPACK, 7, 400, "0.5", "0.92", "0.92", 1, "q3dm1"},
    {14, QA_Q3_BASE_PRODUCT_MISSIONPACK, 4, 400, "0.5", "0.5", "0.5", 1, "mpq3tourney6"},
};
static const uint32_t EXPECTED[8][20] = {
    {0xcad4aad4, 0x55dff85b, 0x222d4f7, 0xb13689f2, 0x584a3d, 0x14213ab1, 0x5939724a, 0x7b922ad, 0x846204b9, 0xa1d01631, 0xfaac0262, 0x2a78132d, 0x1d22b88, 0xbd08d107, 0x7fe375e, 0xcacfef1e, 0x1541017c, 0xf7b36179, 0xaf925ed1, 0x5d713baa},
    {0x305a4d13, 0x73cab312, 0x4d33f5c2, 0xd5bbfd9a, 0xeeed4a04, 0xf0bd471d, 0x56290ec6, 0x5b6106d8, 0x401fbc95, 0x4d87c62b, 0x887f4058, 0x8ed2aad8, 0x8be0c4fe, 0x900bd81d, 0x2fc66402, 0x2f2c730c, 0x1a5b9390, 0x5752a703, 0x84bf18bd, 0xa3c2c85},
    {0x3dced43d, 0xc4031ee2, 0x52dfe748, 0x24d2b0c9, 0x65ff99c9, 0x7b0c1b29, 0x15e040fe, 0xb8ddae24, 0x34b8c002, 0x25fa83e5, 0x660e558b, 0xcc182e98, 0xe4ae7a0, 0x35e3f44d, 0xf50211a2, 0xf56e16c2, 0x8d581414, 0x2eccb574, 0xa6d187a8, 0xbe138a8b},
    {0x33196d17, 0x9ca89099, 0x85c1f3d9, 0x4efcfd83, 0xc56e8e21, 0x776218c1, 0xcc033496, 0xf70b5f05, 0xdaf58769, 0x916bf35f, 0xc4485603, 0x5d8a3e21, 0xdf3e091e, 0x94ef3418, 0xca6f201d, 0xf421effc, 0x5b1a3051, 0x48343cb3, 0x6096fa32, 0xfa07f698},
    {0x6922e1f6, 0xaf602afa, 0x2dfd9684, 0x371ce6e9, 0x3810ecd6, 0xcfa6a964, 0xf33c4341, 0xcf470b1b, 0x667c8127, 0x47a2a4ef, 0x537ecc79, 0xfd0090ee, 0xadf75674, 0x2e51e913, 0x3ebb77ca, 0x9eca8403, 0x8ced6dc3, 0xf3c628c3, 0xc1104c09, 0x28884169},
    {0x98f77d5f, 0xa7b14b0a, 0x34259525, 0x3d04f36b, 0xc88218a2, 0x21a8868c, 0xb4ef8be5, 0xef0ac70e, 0x8db182e2, 0xe07fcc0d, 0xa7791024, 0xe78ee450, 0xe08788f3, 0xdebffb4a, 0xd774c858, 0x310eed2e, 0x4190c74e, 0xd9b96cc6, 0xf091733, 0xff8f343f},
    {0x7d75deb2, 0x66dbc438, 0x43d6b1e7, 0x90817100, 0xff92b810, 0x791f3f93, 0x250cfb9d, 0x4db280fb, 0x8bea9e8e, 0xd4bb714b, 0xe1ef0a61, 0x398cef20, 0xcb99466e, 0x6b9caf0, 0x553409cd, 0x768b5e79, 0xf8fa5b46, 0x29a5c8ec, 0xdfa6d499, 0x7f35a36},
    {0x3aa01672, 0x7e6a435f, 0xb0df7a31, 0x3909e948, 0xa9a37156, 0x8b9b58b1, 0x7f2d22d4, 0x6d1c7858, 0xbde2c883, 0x6d5d0c98, 0x3450c463, 0xf790acb6, 0xf8ef41a4, 0x3dc2b431, 0xc562464c, 0x57e83d38, 0x3ac5362, 0xb1a13140, 0xc516ed45, 0x8cf3b88c},
};

static void test_differential(void) {
  const char *only = getenv("QA_Q3_AI_ONLY");
  for (size_t i = 0; i < sizeof SCENARIOS / sizeof SCENARIOS[0]; i++) {
    if (only != NULL && (size_t)atoi(only) != i)
      continue;
    uint32_t hashes[400];
    run_scenario(&SCENARIOS[i], hashes);
    int first_bad = -1;
    for (int j = 0; j < 20; j++)
      if (hashes[j * 20 + 19] != EXPECTED[i][j] && first_bad < 0)
        first_bad = j * 20 + 19;
    CHECK(first_bad < 0);
    if (first_bad >= 0)
      fprintf(stderr, "scenario %zu diverges by think %d\n", i, first_bad);
  }
}

/* ---- arena.test.ts reproductions ---- */

static const scenario ARENA = {7, QA_Q3_BASE_PRODUCT_BASEQ3, 0, 0, "0.5", "0.5", "0.5", 0, "q3dm1"};

static void test_arena_items(void) {
  fixture *f = malloc(sizeof *f);
  fixture_open(f, &ARENA);
  qa_bot_state_bots_a items;
  CHECK(qa_bot_state_bots_a_init(&items, QA_Q3_BASE_PRODUCT_BASEQ3, NULL));
  items.src->client = 0;
  items.src->inventory[65] = 1;
  items.src->inventory[97] = 13;
  const qa_q3_base_stat_schema *schema = qa_q3_base_stat_schema_for(QA_Q3_BASE_PRODUCT_BASEQ3);
  items.src->cur_ps.stats[(int)schema->health] = 35;
  items.src->cur_ps.powerups[QA_Q3_PW_QUAD] = 1;
  const int models[] = {QA_MI_MEDKIT, QA_MI_TELEPORTER, 0};
  for (int i = 0; i < 3; i++) {
    int model = models[i];
    items.src->cur_ps.stats[(int)schema->holdable_item] = model;
    qa_update_q3_bot_item_inventory_bots_a(&items);
    CHECK(items.src->inventory[QA_INV_HEALTH] == 35);
    CHECK(items.src->inventory[QA_INV_MEDKIT] == (model == QA_MI_MEDKIT));
    CHECK(items.src->inventory[QA_INV_TELEPORTER] == (model == QA_MI_TELEPORTER));
    CHECK(items.src->inventory[QA_INV_QUAD] == 1);
    CHECK(items.src->inventory[65] == 1 && items.src->inventory[97] == 13);
    qa_bot_action_reset_input(&f->library.actions, 0);
    qa_bot_battle_use_items_bots_a(&f->ctx, &items);
    qa_bot_input input = qa_bot_action_get_input(&f->library.actions, 0, 0);
    CHECK((input.action_flags & QA_BOT_ACTION_USE) == (model == 0 ? 0 : QA_BOT_ACTION_USE));
  }
  items.src->cur_ps.powerups[QA_Q3_PW_QUAD] = 0;
  qa_update_q3_bot_item_inventory_bots_a(&items);
  CHECK(items.src->inventory[QA_INV_QUAD] == 0);
  qa_bot_state_bots_a_destroy(&items);
  fixture_close(f);
  free(f);
}

/* Expected direction.x of the first moveInDirection per (case, distance), from bun;
 * the TS test asserts only their signs: melee or > 180 forward, < 100 back, else 0. */
static void test_arena_attack_move(void) {
  static const struct {
    int mode, active;
    bool melee;
  } cases[] = {{1, 1, true}, {2, 2, false}, {3, 7, true}};
  static const double distances[] = {80, 100, 200};
  for (size_t c = 0; c < 3; c++) {
    fixture *f = malloc(sizeof *f);
    fixture_open(f, &ARENA);
    f->knowledge_mode = cases[c].mode;
    f->record_directions = true;
    qa_bot_state_bots_a *state = fixture_bot(f, false);
    state->src->enemy = 0;
    state->src->cur_ps.weapon = cases[c].active;
    state->src->cur_ps.weapon_state = QA_Q3_WEAPON_RAISING;
    state->src->weapon_num = 9;
    f->ctx.time = 10;
    qa_bot_choose_weapon_bots_a(&f->ctx, state);
    CHECK(state->src->weapon_num == 9);
    for (size_t d = 0; d < 3; d++) {
      double distance = distances[d];
      qa_bot_entity_update u;
      memset(&u, 0, sizeof u);
      u.origin = vec(distance, 0, 0);
      qa_bot_entity_observations_update(&f->ctx.observations, 0, &u, 10);
      f->direction_count = 0;
      vset(&state->src->origin, vec(0, 0, 0));
      qa_bot_attack_move_bots_a(&f->ctx, state, 0);
      double expected = cases[c].melee || distance > 180 ? 1 : distance < 100 ? -1 : 0;
      double x = f->direction_count == 0 ? 0 : f->directions[0];
      double sign = x > 0 ? 1 : x < 0 ? -1 : 0;
      CHECK(sign == expected);
      if (sign != expected)
        fprintf(stderr, "attack move case %zu distance %g: x=%g\n", c, distance, x);
      CHECK(state->src->cur_ps.weapon == cases[c].active);
    }
    fixture_close(f);
    free(f);
  }
}

/* inFieldOfVision writes the normalized angles back, stopping at the first failing axis. */
static void test_field_of_vision(void) {
  qa_vec3 angles = vec(-10, 370, 5);
  CHECK(qa_in_field_of_vision_bots_a(vec(0, 0, 0), 90, &angles));
  CHECK(angles.x == qa_qvm_angle_mod(-10) && angles.y == qa_qvm_angle_mod(370) && angles.z == 5);
  qa_vec3 outside = vec(100, 200, 0);
  CHECK(!qa_in_field_of_vision_bots_a(vec(0, 0, 0), 90, &outside));
  CHECK(outside.x == qa_qvm_angle_mod(100) && outside.y == 200);
  qa_vec3 wrap = vec(0, 350, 0);
  CHECK(qa_in_field_of_vision_bots_a(vec(0, 10, 0), 50, &wrap));
}

int main(void) {
  test_field_of_vision();
  test_arena_items();
  test_arena_attack_move();
  test_differential();
  printf("test_q3_decision_combat: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
