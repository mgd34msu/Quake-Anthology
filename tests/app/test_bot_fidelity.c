/* Fidelity checks for the bot simulation shells against the TypeScript under
 * bun (quake-anthology-ts, 2026-11-02):
 *  1. bot-world.ts SourceBotGame.random draws from the source host stream
 *     (source.game.host.random(), the simulation SourceRandom).
 *  2. bot-q1/q2-knowledge.ts WeaponInfo keeps JS numbers (109.5 rocket damage,
 *     unrounded atan spreads, 0.1 reloads).
 *  3. q3BotNavigation(game, library, selected, bspEntities) carries the
 *     director's bspEntities and the modelBounds query.
 *  4. BotBrain checkpoint checkSixYaw (type, encoder, reader) and the TS JSON
 *     property order of the brain memory, byte for byte.
 *  6. bestReachableFromJumpPadArea predicts the pad flight with hitBox set.
 *  7. GameAiContext.nodeSwitches is unbounded.
 * Every expected value was printed by the TS modules under bun; every callback
 * checks a sentinel ctx. GPL-2.0-or-later. */
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../content/q1_world_fixture.h"
#include "qa/app/bootstrap/simulation/bot_q2_knowledge.h"
#include "qa/app/bootstrap/simulation/bot_rerelease.h"
#include "qa/app/bootstrap/simulation/bot_world.h"
#include "qa/app/bootstrap/simulation/bots.h"
#include "qa/app/bootstrap/simulation/random.h"
#include "qa/bots/behavior/q3/ai_context.h"
#include "qa/bots/behavior/q3/ai_decision.h"
#include "qa/bots/behavior/q3/library.h"
#include "qa/bots/behavior/q3/navigation_services.h"
#include "qa/bots/behavior/rerelease/checkpoint.h"
#include "qa/bots/behavior/rerelease/profile.h"
#include "qa/bots/navigation/aas.h"
#include "qa/content/q2/foundation/weapons/player.h"
#include "qa/network/common/js_number.h"
#include "qa/persistence/value.h"

static int failures = 0;
static int checks = 0;
#define CHECK(cond)                                                    \
  do {                                                                 \
    checks++;                                                          \
    if (!(cond)) {                                                     \
      failures++;                                                      \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
    }                                                                  \
  } while (0)

/* ---- transcript lines (TS lines.push) ---- */
static char **lines;
static size_t line_count, line_capacity;
static void L(const char *format, ...) __attribute__((format(printf, 1, 2)));
static void L(const char *format, ...) {
  char buffer[1024];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof buffer, format, args);
  va_end(args);
  if (line_count == line_capacity) {
    line_capacity = line_capacity != 0 ? line_capacity * 2 : 32;
    lines = realloc(lines, line_capacity * sizeof *lines);
    if (lines == NULL) abort();
  }
  lines[line_count] = strdup(buffer);
  if (lines[line_count] == NULL) abort();
  line_count++;
}
static void compare_lines(const char *tag, const char *const *want, size_t want_count) {
  for (size_t i = 0; i < want_count || i < line_count; i++) {
    const char *have = i < line_count ? lines[i] : "<missing>", *need = i < want_count ? want[i] : "<extra>";
    checks++;
    if (strcmp(have, need) != 0) {
      failures++;
      fprintf(stderr, "%s line %zu\n  have: %s\n  want: %s\n", tag, i + 1, have, need);
    }
  }
  for (size_t i = 0; i < line_count; i++) free(lines[i]);
  free(lines);
  lines = NULL;
  line_count = line_capacity = 0;
}
/* TS `${x} ${y} ${z}` with String(number). */
#define V(v) QA_JS_NUMBER((v).x), QA_JS_NUMBER((v).y), QA_JS_NUMBER((v).z)

/* ---- sentinel simulation view over a Q1 foundation (q1_world_fixture) ---- */
typedef struct sim_fixture {
  q1_world_fixture q1; /* first: the Q1 host ctx is this fixture */
  const void *sentinel;
  qa_app_sim_source_random *random;
  int host_draws;
  qa_executable_recipe recipe;
  qa_provider_timing timing[1];
  qa_app_sim_bot_q1_source source;
  qa_comp_q1_source_composition composition;
  bool q2_weapon;
  qa_q2_game_services q2_game;
  qa_q2w_weapons q2_weapons;
} sim_fixture;
static sim_fixture *fx(void *ctx) { sim_fixture *f = ctx; CHECK(f != NULL && f->sentinel == f); return f; }
/* TS runtime: random: () => runtime.random.nextUnit() (the simulation SourceRandom). */
static double host_random(void *ctx) {
  sim_fixture *f = fx(ctx);
  f->host_draws++;
  return qa_app_sim_source_random_next_unit(f->random);
}
static const qa_app_sim_bot_q1_source *sim_q1(void *ctx) { return &fx(ctx)->source; }
static qa_q2_product_runtime *sim_q2(void *ctx) { fx(ctx); return NULL; }
static qa_q1_services *sim_q1_weapon(void *ctx) { return fx(ctx)->q1.game; }
static bool sim_q2_weapon(void *ctx, qa_q2_game_services **game, qa_q2w_weapons **weapons) {
  sim_fixture *f = fx(ctx);
  *game = f->q2_weapon ? &f->q2_game : NULL;
  *weapons = f->q2_weapon ? &f->q2_weapons : NULL;
  return f->q2_weapon;
}
static qa_app_q3_selected_arsenal *sim_q3_weapon(void *ctx) { fx(ctx); return NULL; }
static qa_app_sim_bot_simulation make_sim(sim_fixture *f) {
  qa_app_sim_bot_simulation s;
  memset(&s, 0, sizeof s);
  s.ctx = f; s.recipe = &f->recipe; s.mode = "deathmatch"; s.max_clients = 4;
  s.actors = f->q1.actors; s.bodies = f->q1.bodies; s.combat = f->q1.combat; s.inventory = f->q1.inventory;
  s.random = f->random;
  s.q1_source = sim_q1; s.q2_source = sim_q2; s.q1_weapon_source = sim_q1_weapon; s.q2_weapon_source = sim_q2_weapon;
  s.selected_q3_weapon_source = sim_q3_weapon;
  return s;
}
static void sim_fixture_init(sim_fixture *f) {
  memset(f, 0, sizeof *f);
  CHECK(q1_world_fixture_create(&f->q1, q1_world_fixture_options()));
  f->sentinel = f;
  f->random = qa_app_sim_source_random_create(7, "classic");
  f->q1.game->host.random = host_random; /* host.ctx is &f->q1 == f */
  CHECK(f->q1.game->host.ctx == (void *)f);
  f->recipe.engine_behavior.provider = "q1:engine";
  f->timing[0].provider = "q1:engine";
  f->recipe.timing = f->timing; f->recipe.timing_count = 1;
  f->source.game = f->q1.game; f->source.composition = &f->composition;
}
static void sim_fixture_destroy(sim_fixture *f) {
  if (f->q2_weapon) qa_q2w_weapons_destroy(&f->q2_weapons);
  qa_app_sim_source_random_destroy(f->random);
  q1_world_fixture_destroy(&f->q1);
}

/* ---- world options leaves ---- */
static sim_fixture *wo(void *ctx) { return fx(ctx); }
static bool wo_actor(void *ctx, int64_t client, qa_actor_id *out) { wo(ctx); (void)client; (void)out; return false; }
static bool wo_connect(void *ctx, int64_t client, bool restart) { wo(ctx); (void)client; (void)restart; return true; }
static void wo_client(void *ctx, int64_t client) { wo(ctx); (void)client; }
static void wo_text(void *ctx, const char *text) { wo(ctx); (void)text; }
static void wo_message(void *ctx, int64_t client, const char *text) { wo(ctx); (void)client; (void)text; }

/* 1. bot-world.ts: random: { random: () => source.game.host.random(),
 *    crandom: () => source.game.host.random() * 2 - 1 } over SourceRandom(7, "classic"). */
static void test_shared_world_random(void) {
  sim_fixture f;
  sim_fixture_init(&f);
  qa_app_sim_bot_simulation sim = make_sim(&f);
  qa_cvar_registry_options cvar_options;
  memset(&cvar_options, 0, sizeof cvar_options);
  cvar_options.dialect = QA_COMMAND_DIALECT_Q3;
  qa_cvar_registry *cvars = qa_cvar_registry_create(&cvar_options);
  qa_app_sim_shared_bot_world_options options;
  memset(&options, 0, sizeof options);
  options.restoring = true; options.simulation = &sim; options.cvars = cvars; options.ctx = &f;
  options.actor = wo_actor; options.connect = wo_connect; options.drop = wo_client; options.begin = wo_client;
  options.print = wo_text; options.console = wo_text; options.message = wo_message;
  qa_app_sim_shared_bot_world *world = qa_app_sim_shared_bot_world_create(&options);
  CHECK(world != NULL);
  qa_source_bot_game_bots_a *game = qa_app_sim_shared_bot_world_game(world);
  qa_q1_host *host = &f.q1.game->host;
  double draws[6];
  draws[0] = host->random(host->ctx);
  draws[1] = game->random.random(game->random.ctx);
  draws[2] = game->random.crandom(game->random.ctx);
  draws[3] = host->random(host->ctx);
  draws[4] = game->random.crandom(game->random.ctx);
  draws[5] = game->random.random(game->random.ctx);
  /* bun: [host(), game.random(), game.crandom(), host(), game.crandom(), game.random()] */
  static const double expected[6] = {0.7496871948242188, 0.7676931023597717, -0.8870204761624336,
                                     0.22409741580486298, -0.58708456158638, 0.14926603436470032};
  for (int i = 0; i < 6; i++) {
    checks++;
    if (draws[i] != expected[i]) {
      failures++;
      fprintf(stderr, "random draw %d: have %.17g want %.17g\n", i, draws[i], expected[i]);
    }
  }
  CHECK(f.host_draws == 6); /* every bot draw advanced the one source stream */
  qa_app_sim_source_random_state state = qa_app_sim_source_random_checkpoint(f.random);
  CHECK(state.draws == 6);

  /* 2. Q1 WeaponInfo through the world's arsenal binding (no actor: player null). */
  qa_app_sim_bot_arsenal *arsenal = qa_app_sim_shared_bot_world_knowledge(world);
  CHECK(arsenal != NULL && strcmp(arsenal->source, "q1") == 0);
  /* bun createQ1BotKnowledge(...).knowledge.weaponInfo(library, 5, slot): number, projectileCount,
   * horizontalSpread, verticalSpread, speed, offset x y z, extraZVelocity, ammoAmount, activate,
   * reload, spinUp, gravity, damage, radius, damageType, detonation, bounce */
  static const double q1_expected[][19] = {
      {1, 1, 0, 0, 0, 0, 0, -6, 0, 0, 0, 0.5, 0, 0, 20, 0, 1, 0, 0},
      {2, 6, 0.38176834043975494, 0.38176834043975494, 0, 10, 0, -6.800000000000001, 0, 1, 0, 0.5, 0, 0, 4, 0, 1, 0, 0},
      {3, 14, 1.32826839905356, 0.7623202099834768, 0, 10, 0, -6.800000000000001, 0, 2, 0, 0.7, 0, 0, 4, 0, 1, 0, 0},
      {4, 1, 0, 0, 1000, 0, 4, -6, 0, 1, 0, 0.1, 0, 0, 9, 0, 1, 0, 0},
      {5, 1, 0, 0, 1000, 0, 0, -6, 0, 2, 0, 0.1, 0, 0, 18, 0, 1, 0, 0},
      {7, 1, 0, 0, 1000, 8, 0, -6, 0, 1, 0, 0.8, 0, 0, 110, 160, 3, 0, 0},
      {8, 1, 0, 0, 0, 0, 0, -6, 0, 1, 0, 0.1, 0, 0, 30, 0, 1, 0, 0}};
  qa_bot_arsenal_interface_bots_a *knowledge = &arsenal->knowledge.knowledge;
  size_t row = 0;
  for (int64_t slot = 0; slot <= 9; slot++) {
    qa_weapon_info_bots_a info;
    if (!knowledge->weapon_info(knowledge->ctx, NULL, 5, slot, &info)) continue;
    CHECK(row < sizeof q1_expected / sizeof q1_expected[0]);
    if (row >= sizeof q1_expected / sizeof q1_expected[0]) break;
    const double have[19] = {info.number, info.projectile_count, info.horizontal_spread, info.vertical_spread, info.speed,
                             info.offset.x, info.offset.y, info.offset.z, info.extra_z_velocity, info.ammo_amount,
                             info.activate, info.reload, info.spin_up, info.projectile_info.gravity,
                             info.projectile_info.damage, info.projectile_info.radius, info.projectile_info.damage_type,
                             info.projectile_info.detonation, info.projectile_info.bounce};
    for (int k = 0; k < 19; k++) {
      checks++;
      if (have[k] != q1_expected[row][k]) {
        failures++;
        fprintf(stderr, "q1 slot %lld field %d: have %.17g want %.17g\n", (long long)slot, k, have[k], q1_expected[row][k]);
      }
    }
    row++;
  }
  CHECK(row == sizeof q1_expected / sizeof q1_expected[0]);
  qa_app_sim_shared_bot_world_destroy(world);
  qa_cvar_registry_destroy(cvars);
  sim_fixture_destroy(&f);
}

/* 2. bot-q2-knowledge.ts WeaponInfo over the base Q2 arsenal. */
static bool no_actor(void *ctx, int64_t client, qa_actor_id *out) { fx(ctx); (void)client; (void)out; return false; }
static void q2_weapon_info_case(qa_q2_edition edition, qa_q2_mode mode, const double (*expected)[19], size_t expected_count,
                                const char *uncovered) {
  sim_fixture f;
  sim_fixture_init(&f);
  f.q2_weapon = true;
  qa_q2w_hooks hooks;
  memset(&hooks, 0, sizeof hooks);
  CHECK(qa_q2w_weapons_init(&f.q2_weapons, hooks));
  f.q2_game.options.edition = edition; f.q2_game.options.mode = mode;
  qa_app_sim_bot_simulation sim = make_sim(&f);
  qa_app_sim_bot_arsenal *arsenal = qa_app_sim_create_q2_bot_knowledge(&sim, no_actor, &f);
  qa_bot_arsenal_interface_bots_a *knowledge = &arsenal->knowledge.knowledge;
  size_t row = 0;
  for (int64_t slot = 0; slot <= 12; slot++) {
    qa_weapon_info_bots_a info;
    if (!knowledge->weapon_info(knowledge->ctx, NULL, 5, slot, &info)) continue;
    CHECK(row < expected_count);
    if (row >= expected_count) break;
    const double have[19] = {info.number, info.projectile_count, info.horizontal_spread, info.vertical_spread, info.speed,
                             info.offset.x, info.offset.y, info.offset.z, info.extra_z_velocity, info.ammo_amount,
                             info.activate, info.reload, info.spin_up, info.projectile_info.gravity,
                             info.projectile_info.damage, info.projectile_info.radius, info.projectile_info.damage_type,
                             info.projectile_info.detonation, info.projectile_info.bounce};
    for (int k = 0; k < 19; k++) {
      checks++;
      if (have[k] != expected[row][k]) {
        failures++;
        fprintf(stderr, "q2 slot %lld field %d: have %.17g want %.17g\n", (long long)slot, k, have[k], expected[row][k]);
      }
    }
    row++;
  }
  CHECK(row == expected_count);
  char joined[256] = "";
  for (size_t i = 0; i < arsenal->uncovered_count; i++) {
    if (i != 0) strcat(joined, ",");
    strcat(joined, arsenal->uncovered_weapons[i]);
  }
  CHECK(strcmp(joined, uncovered) == 0);
  qa_app_sim_bot_arsenal_destroy(arsenal);
  sim_fixture_destroy(&f);
}
static void test_q2_weapon_info(void) {
  /* bun createQ2BotKnowledge over Q2_BASE_WEAPONS, classic deathmatch */
  static const double classic[][19] = {
      {1, 1, 0, 0, 1000, 24, 8, -8, 0, 0, 0.4, 0.5, 0, 0, 15, 0, 1, 0, 0},
      {2, 12, 0.5821206697084973, 0.5821206697084973, 0, 0, 8, -8, 0, 1, 0.7000000000000001, 1.2, 0, 0, 4, 0, 1, 0, 0},
      {3, 20, 1.1599468174081877, 0.5821206697084973, 0, 0, 8, -8, 0, 2, 0.6000000000000001, 1.2, 0, 0, 6, 0, 1, 0, 0},
      {4, 1, 0.34954948055399604, 0.5821206697084973, 0, 0, 8, -8, 0, 1, 0.30000000000000004, 0.1, 0, 0, 8, 0, 1, 0, 0},
      {5, 3, 0.34954948055399604, 0.5821206697084973, 0, 0, 8, -8, 0, 1, 0.4, 0.1, 1, 0, 6, 0, 1, 0, 0},
      {8, 1, 0, 0, 650, 8, 8, -8, 0, 1, 0.4, 0.9, 0, 0, 109.5, 120, 3, 0, 0},
      {9, 1, 0, 0, 1000, 24, 8, -8, 0, 1, 0.5, 0.1, 0, 0, 15, 0, 1, 0, 0},
      {10, 1, 0, 0, 0, 0, 7, -8, 0, 1, 0.30000000000000004, 1.6, 0, 0, 100, 0, 1, 0, 0}};
  q2_weapon_info_case(QA_Q2_EDITION_CLASSIC, QA_Q2_MODE_DEATHMATCH, classic, sizeof classic / sizeof classic[0],
                      "grenades,grenadelauncher,bfg");
  /* rerelease coop */
  static const double rerelease[][19] = {
      {1, 1, 0, 0, 1500, 24, 8, -8, 0, 0, 0.4, 0.5, 0, 0, 15, 0, 1, 0, 0},
      {2, 12, 0.5821206697084973, 0.5821206697084973, 0, 0, 0, -8, 0, 1, 0.7000000000000001, 1.2, 0, 0, 4, 0, 1, 0, 0},
      {3, 20, 1.1599468174081877, 0.5821206697084973, 0, 0, 0, -8, 0, 2, 0.6000000000000001, 1.2, 0, 0, 6, 0, 1, 0, 0},
      {4, 1, 0.34954948055399604, 0.5821206697084973, 0, 0, 0, -8, 0, 1, 0.30000000000000004, 0.1, 0, 0, 8, 0, 1, 0, 0},
      {5, 3, 0.34954948055399604, 0.5821206697084973, 0, 0, 0, -8, 0, 1, 0.4, 0.1, 1, 0, 8, 0, 1, 0, 0},
      {8, 1, 0, 0, 650, 8, 8, -8, 0, 1, 0.4, 0.9, 0, 0, 109.5, 120, 3, 0, 0},
      {9, 1, 0, 0, 1000, 24, 8, -8, 0, 1, 0.5, 0.1, 0, 0, 20, 0, 1, 0, 0},
      {10, 1, 0, 0, 0, 0, 7, -8, 0, 1, 0.30000000000000004, 1.6, 0, 0, 125, 0, 1, 0, 0}};
  q2_weapon_info_case(QA_Q2_EDITION_RERELEASE, QA_Q2_MODE_COOP, rerelease, sizeof rerelease / sizeof rerelease[0],
                      "grenades,grenadelauncher,bfg");
}

/* ---- 3 and 6. q3BotNavigation(game, library, selected, bspEntities).bestReachableFromJumpPadArea ---- */
typedef struct pad_fixture {
  const void *sentinel;
  qa_bot_observed_entity_bots_a entities[4];
  double stop;
  qa_navigation_runtime runtime;
} pad_fixture;
static pad_fixture *pf(void *ctx) { pad_fixture *p = ctx; CHECK(p != NULL && p->sentinel == p); return p; }
static int64_t pad_entity_count(void *ctx) { pf(ctx); return 4; }
static qa_bot_observed_entity_bots_a pad_entity(void *ctx, int64_t number) {
  pad_fixture *p = pf(ctx);
  L("entity %lld", (long long)number);
  return p->entities[number];
}
static double pad_clock(void *ctx) { pf(ctx); return 0; }
static int64_t pad_contents(void *ctx, qa_vec3 point, int64_t pass) { pf(ctx); (void)point; (void)pass; return 0; }
static qa_q3_base_server_trace_result pad_trace(void *ctx, qa_q3_base_server_trace_query q) {
  pf(ctx);
  if (q.shape.kind == QA_Q3_BASE_SHAPE_POINT)
    L("trace %s %s %s > %s %s %s point %s %s", V(q.start), V(q.end), QA_JS_NUMBER(q.pass_entity_num), QA_JS_NUMBER(q.mask));
  else
    L("trace %s %s %s > %s %s %s %s %s %s | %s %s %s %s %s", V(q.start), V(q.end), V(q.shape.mins), V(q.shape.maxs),
      QA_JS_NUMBER(q.pass_entity_num), QA_JS_NUMBER(q.mask));
  qa_q3_base_server_trace_result r;
  memset(&r, 0, sizeof r);
  r.fraction = 0.5;
  r.end = (qa_vec3){q.end.x, q.end.y, q.end.z + 2};
  r.entity_num = 1022;
  r.solidity = QA_Q3_BASE_SOLID_CLEAR;
  r.contents = 1;
  return r;
}
static qa_navigation_runtime *pad_for_client(void *ctx, int64_t client) { (void)client; return &pf(ctx)->runtime; }
static qa_bot_travel_prediction_result pad_predict(void *ctx, qa_bot_movement_prediction q) {
  pad_fixture *p = pf(ctx);
  char hit[256] = "none";
  if (q.has_hit_box) {
    snprintf(hit, sizeof hit, "%s %s %s | %s %s %s", V(q.hit_box.min), V(q.hit_box.max));
  }
  L("predict %lld %s %s %s %lld %s %s %s %s %s %s %s %lld %lld %s %lld %lld %s %s", (long long)q.entity_num, V(q.origin),
    (long long)q.presence, q.on_ground ? "true" : "false", V(q.velocity), V(q.command_move), (long long)q.command_frames,
    (long long)q.max_frames, QA_JS_NUMBER(q.frame_time), (long long)q.stop_events, (long long)q.stop_area, hit,
    q.visualize ? "true" : "false");
  qa_bot_travel_prediction_result r;
  memset(&r, 0, sizeof r);
  r.frames = 1;
  r.stop_event = p->stop;
  return r;
}
static bool pad_travel_weapon(void *ctx, int64_t client, const char *mode, int64_t *out) {
  pf(ctx); (void)client; (void)mode; (void)out; return false;
}
static void pad_print(void *ctx, int severity, const char *text) { pf(ctx); (void)severity; (void)text; }
static qa_bot_observed_entity_bots_a pad_observed(bool present, bool inline_model, int64_t model, qa_vec3 origin, qa_vec3 min, qa_vec3 max) {
  qa_bot_observed_entity_bots_a e;
  memset(&e, 0, sizeof e);
  e.present = present; e.has_inline_model = inline_model; e.inline_model = model;
  e.origin = origin; e.bounds = (qa_bounds){min, max};
  return e;
}
static void test_jump_pad(void) {
  static pad_fixture p;
  memset(&p, 0, sizeof p);
  p.sentinel = &p;
  p.stop = 2048;
  const qa_vec3 zero = {0, 0, 0};
  p.entities[0] = pad_observed(true, false, 0, zero, zero, zero);
  p.entities[1] = pad_observed(false, true, 3, zero, (qa_vec3){-9, -9, -9}, (qa_vec3){9, 9, 9});
  p.entities[2] = pad_observed(true, true, 3, (qa_vec3){10, 20, 30}, (qa_vec3){-42, -52, -30}, (qa_vec3){22, 12, -14});
  p.entities[3] = pad_observed(true, true, 4, (qa_vec3){100, 100, 0}, zero, (qa_vec3){20, 20, 8});
  /* the director's AasBspEntities over the same entity text */
  qa_aas_bsp_entities bsp;
  qa_aas_bsp_entities_init(&bsp);
  qa_aas_bsp_entities_configure(&bsp, pad_print, &p, NULL);
  (void)qa_aas_bsp_entities_load(&bsp,
                                 "{\n\"classname\" \"worldspawn\"\n}\n"
                                 "{\n\"classname\" \"trigger_push\"\n\"model\" \"*9\"\n\"target\" \"pad1\"\n}\n"
                                 "{\n\"classname\" \"trigger_push\"\n\"model\" \"*4\"\n\"target\" \"missing\"\n}\n"
                                 "{\n\"classname\" \"info_null\"\n\"model\" \"*3\"\n}\n"
                                 "{\n\"classname\" \"trigger_push\"\n\"model\" \"*3\"\n\"target\" \"pad1\"\n}\n"
                                 "{\n\"classname\" \"target_position\"\n\"targetname\" \"pad1\"\n\"origin\" \"300 -40 420\"\n}\n");
  /* a two-area AAS: x >= 0 is area 1, else area 2; both jump-pad areas */
  static qa_aas_plane_rec planes[1] = {{{1, 0, 0}, 0, 0}};
  static qa_aas_node_rec aas_nodes[2] = {{0, {0, 0}}, {0, {-1, -2}}};
  static qa_aas_area_rec areas[3];
  static qa_aas_setting_rec settings[3] = {{0, 0, 2, 1, 0, 0, 0}, {128, 0, 2, 1, 0, 0, 0}, {128 | 1, 0, 2, 1, 0, 0, 0}};
  for (int64_t i = 0; i < 3; i++) areas[i].number = i;
  static qa_aas_asset asset;
  memset(&asset, 0, sizeof asset);
  asset.planes = planes; asset.plane_count = 1;
  asset.nodes = aas_nodes; asset.node_count = 2;
  asset.areas = areas; asset.area_count = 3;
  asset.settings = settings; asset.setting_count = 3;
  static qa_navigation_node nodes[2];
  memset(nodes, 0, sizeof nodes);
  nodes[0].id = 1; nodes[0].bounds = (qa_bounds){{0, -64, 0}, {64, 64, 32}};
  nodes[1].id = 2; nodes[1].bounds = (qa_bounds){{-64, -32, 0}, {0, 32, 32}};
  for (int i = 0; i < 2; i++) {
    nodes[i].radius = 1; nodes[i].presence = 2;
    nodes[i].source.kind = QA_NS_AAS; nodes[i].source.primary = nodes[i].id;
  }
  static qa_navigation_graph graph;
  memset(&graph, 0, sizeof graph);
  graph.map.name = "pad";
  graph.profile.shape.kind = "box";
  graph.profile.shape.bounds = (qa_bounds){{-15, -15, -24}, {15, 15, 32}};
  graph.profile.team = -1;
  graph.aas_asset = &asset;
  graph.nodes = nodes; graph.node_count = 2;
  static qa_navigation_world world;
  memset(&world, 0, sizeof world);
  world.ctx = &p;
  CHECK(qa_navigation_runtime_init(&p.runtime, &graph, &world));
  qa_source_bot_game_bots_a game;
  memset(&game, 0, sizeof game);
  game.ctx = &p; game.entity_count = pad_entity_count; game.entity = pad_entity; game.clock_time = pad_clock;
  game.trace = pad_trace; game.point_contents = pad_contents;
  qa_bot_library_bots_a *library = calloc(1, sizeof *library);
  CHECK(library != NULL);
  if (library == NULL) return;
  qa_bot_move_state_store_init(&library->move_states); /* svGravity null: 800 */
  qa_bot_action_buffer_init(&library->actions);
  qa_selected_bot_navigation_bots_a selected;
  memset(&selected, 0, sizeof selected);
  selected.ctx = &p; selected.runtime = &p.runtime; selected.for_client = pad_for_client;
  selected.crouched_bounds = (qa_bounds){{-15, -15, -24}, {15, 15, 16}};
  selected.predict_client_movement = pad_predict; selected.travel_weapon = pad_travel_weapon;
  qa_q3_bot_navigation_bots_a *navigation = calloc(1, sizeof *navigation);
  CHECK(navigation != NULL);
  if (navigation == NULL) return;
  qa_q3_bot_navigation_bots_a_init(navigation, &game, library, &selected, &bsp);
  CHECK(navigation->navigation.host.bsp_entities == &bsp);
  const qa_bounds bounds = {{-15, -15, -15}, {15, 15, 15}};
  L("result %lld", (long long)qa_source_bot_navigation_best_reachable_from_jump_pad_area(&navigation->navigation, (qa_vec3){300, -40, 380}, bounds));
  p.stop = 0;
  L("result %lld", (long long)qa_source_bot_navigation_best_reachable_from_jump_pad_area(&navigation->navigation, (qa_vec3){1, 2, 3}, bounds));
  /* bun jumppad2.ts: q3BotNavigation(game, library, selected, bsp).bestReachableFromJumpPadArea */
  static const char *const expected[] = {
      "entity 0", "entity 1", "entity 2", "entity 3",
      "entity 0", "entity 1", "entity 2", "entity 3",
      "trace 110 110 68 > 110 110 4 -15 -15 -24 | 15 15 16 -1 65537",
      "entity 0", "entity 1", "entity 2",
      "trace 0 0 72 > 0 0 8 -15 -15 -24 | 15 15 16 -1 65537",
      "predict -1 0 0 10.125 2 false 325.1587219238281 -43.354496002197266 811.9113159179688 325.1587219238281 "
      "-43.354496002197266 0 0 30 0.10000000149011612 2048 0 285 -55 365 | 315 -25 395 false",
      "result 1",
      "entity 0", "entity 1", "entity 2", "entity 3",
      "entity 0", "entity 1", "entity 2", "entity 3",
      "trace 110 110 68 > 110 110 4 -15 -15 -24 | 15 15 16 -1 65537",
      "entity 0", "entity 1", "entity 2",
      "trace 0 0 72 > 0 0 8 -15 -15 -24 | 15 15 16 -1 65537",
      "predict -1 0 0 10.125 2 false 325.1587219238281 -43.354496002197266 811.9113159179688 325.1587219238281 "
      "-43.354496002197266 0 0 30 0.10000000149011612 2048 0 -14 -13 -12 | 16 17 18 false",
      "result 0"};
  compare_lines("jump pad", expected, sizeof expected / sizeof expected[0]);
  free(navigation);
  qa_bot_action_buffer_destroy(&library->actions);
  qa_bot_move_state_store_destroy(&library->move_states);
  free(library);
  qa_navigation_runtime_destroy(&p.runtime);
  qa_aas_bsp_entities_destroy(&bsp);
}

/* ---- 4. BotBrain checkSixYaw and the TS JSON memory order ---- */
static const char brain_fresh_json[] =
    "{\"version\":1,\"source\":\"q1-rerelease\",\"definition\":\"fixture\",\"rng\":794540887,\"brain\":{\"version\":1,\"skill\":\"easy"
    "\",\"gameMode\":{\"gameType\":\"coop\",\"weaponStay\":false,\"hasTeams\":true},\"memory\":{\"triggerWeapon\":0,\"triggerHeldSi"
    "nce\":-1,\"triggerReadyAt\":0,\"aim\":{\"pitch\":0,\"yaw\":0,\"pitchVelocity\":0,\"yawVelocity\":0,\"modifierUntil\":0},\"path"
    "State\":{\"path\":null,\"index\":0,\"stuckOrigin\":{\"x\":0,\"y\":0,\"z\":0},\"stuckSince\":0,\"stuckCount\":0,\"jumpReadyAt\":0,"
    "\"plannedAt\":0,\"liftWaitSince\":-1,\"liftWaitZ\":0},\"targetId\":-1,\"goalPoint\":null,\"goalEntityId\":-1,\"stuckTrips\":"
    "0,\"goalIsLive\":false,\"unstickUntil\":0,\"pressUntil\":0,\"unstickSide\":0,\"explicitGoal\":{\"owner\":\"external\",\"kind\""
    ":\"point\",\"point\":{\"x\":32,\"y\":64,\"z\":16},\"entityId\":-1},\"explicitGoalDone\":false,\"explicitGoalFailed\":false,\"we"
    "dgeOrigin\":null,\"wedgeSince\":-1,\"lastSafeOrigin\":null,\"restPoint\":null,\"restUntil\":0,\"guardRefusals\":0,\"lastGu"
    "ardRefused\":false,\"gapJumps\":0,\"hazardFrames\":0,\"ownObjectiveHome\":null,\"enemyObjectiveHome\":null,\"objectiveRo"
    "le\":\"\",\"touchGoal\":false,\"holdPosition\":false,\"gateShootAt\":null,\"gateFiredAt\":-1,\"coopRegrouping\":false,\"coop"
    "RegroupAt\":0,\"coopRegroupUntil\":0,\"levelStarted\":false,\"checkSixUntil\":0,\"checkSixYaw\":0,\"checkSixNextAt\":0,\"r"
    "oamPoint\":null,\"roamUntil\":0,\"lastCmd\":{\"forwardmove\":0,\"sidemove\":0,\"upmove\":0,\"buttons\":0,\"impulse\":0,\"viewA"
    "ngles\":{\"x\":0,\"y\":0,\"z\":0}},\"spawnedOnce\":false,\"lastWeaponNumber\":0,\"deadSince\":-1,\"respawnWait\":0,\"respawnPr"
    "ess\":false,\"awareness\":[],\"unreachableUntil\":[],\"objectiveHome\":[],\"saidThisLevel\":[]}},\"pendingChats\":[]}"
    ;
static const char brain_saved_json[] =
    "{\"version\":1,\"source\":\"q1-rerelease\",\"definition\":\"fixture\",\"rng\":794540887,\"brain\":{\"version\":1,\"skill\":\"easy"
    "\",\"gameMode\":{\"gameType\":\"coop\",\"weaponStay\":false,\"hasTeams\":true},\"memory\":{\"triggerWeapon\":0,\"triggerHeldSi"
    "nce\":-1,\"triggerReadyAt\":0,\"aim\":{\"pitch\":0,\"yaw\":0,\"pitchVelocity\":0,\"yawVelocity\":0,\"modifierUntil\":0},\"path"
    "State\":{\"path\":null,\"index\":0,\"stuckOrigin\":{\"x\":0,\"y\":0,\"z\":0},\"stuckSince\":0,\"stuckCount\":0,\"jumpReadyAt\":0,"
    "\"plannedAt\":0,\"liftWaitSince\":-1,\"liftWaitZ\":0},\"targetId\":-1,\"goalPoint\":null,\"goalEntityId\":-1,\"stuckTrips\":"
    "0,\"goalIsLive\":false,\"unstickUntil\":0,\"pressUntil\":0,\"unstickSide\":0,\"explicitGoal\":{\"owner\":\"external\",\"kind\""
    ":\"point\",\"point\":{\"x\":32,\"y\":64,\"z\":16},\"entityId\":-1},\"explicitGoalDone\":false,\"explicitGoalFailed\":false,\"we"
    "dgeOrigin\":null,\"wedgeSince\":-1,\"lastSafeOrigin\":null,\"restPoint\":null,\"restUntil\":0,\"guardRefusals\":0,\"lastGu"
    "ardRefused\":false,\"gapJumps\":0,\"hazardFrames\":0,\"ownObjectiveHome\":null,\"enemyObjectiveHome\":null,\"objectiveRo"
    "le\":\"\",\"touchGoal\":false,\"holdPosition\":false,\"gateShootAt\":null,\"gateFiredAt\":-1,\"coopRegrouping\":false,\"coop"
    "RegroupAt\":0,\"coopRegroupUntil\":0,\"levelStarted\":false,\"checkSixUntil\":0,\"checkSixYaw\":123.5,\"checkSixNextAt\":"
    "0,\"roamPoint\":null,\"roamUntil\":0,\"lastCmd\":{\"forwardmove\":0,\"sidemove\":0,\"upmove\":0,\"buttons\":0,\"impulse\":0,\"v"
    "iewAngles\":{\"x\":0,\"y\":0,\"z\":0}},\"spawnedOnce\":false,\"lastWeaponNumber\":0,\"deadSince\":-1,\"respawnWait\":0,\"respa"
    "wnPress\":false,\"awareness\":[],\"unreachableUntil\":[],\"objectiveHome\":[],\"saidThisLevel\":[]}},\"pendingChats\":[]}"
    ;
static const char brain_read_json[] =
    "{\"version\":1,\"source\":\"q1-rerelease\",\"definition\":\"fixture\",\"rng\":794540887,\"brain\":{\"version\":1,\"skill\":\"easy"
    "\",\"gameMode\":{\"gameType\":\"coop\",\"weaponStay\":false,\"hasTeams\":true},\"memory\":{\"triggerWeapon\":0,\"triggerHeldSi"
    "nce\":-1,\"triggerReadyAt\":0,\"targetId\":-1,\"goalPoint\":null,\"goalEntityId\":-1,\"stuckTrips\":0,\"goalIsLive\":false,"
    "\"unstickUntil\":0,\"pressUntil\":0,\"unstickSide\":0,\"explicitGoalDone\":false,\"explicitGoalFailed\":false,\"wedgeOrig"
    "in\":null,\"wedgeSince\":-1,\"lastSafeOrigin\":null,\"restPoint\":null,\"restUntil\":0,\"guardRefusals\":0,\"lastGuardRefu"
    "sed\":false,\"gapJumps\":0,\"hazardFrames\":0,\"ownObjectiveHome\":null,\"enemyObjectiveHome\":null,\"objectiveRole\":\"\","
    "\"touchGoal\":false,\"holdPosition\":false,\"gateShootAt\":null,\"gateFiredAt\":-1,\"coopRegrouping\":false,\"coopRegroup"
    "At\":0,\"coopRegroupUntil\":0,\"levelStarted\":false,\"checkSixUntil\":0,\"checkSixYaw\":123.5,\"checkSixNextAt\":0,\"roam"
    "Point\":null,\"roamUntil\":0,\"spawnedOnce\":false,\"lastWeaponNumber\":0,\"deadSince\":-1,\"respawnWait\":0,\"respawnPres"
    "s\":false,\"aim\":{\"pitch\":0,\"yaw\":0,\"pitchVelocity\":0,\"yawVelocity\":0,\"modifierUntil\":0},\"pathState\":{\"path\":nul"
    "l,\"index\":0,\"stuckOrigin\":{\"x\":0,\"y\":0,\"z\":0},\"stuckSince\":0,\"stuckCount\":0,\"liftWaitSince\":-1,\"liftWaitZ\":0,\""
    "jumpReadyAt\":0,\"plannedAt\":0},\"awareness\":[],\"unreachableUntil\":[],\"objectiveHome\":[],\"saidThisLevel\":[],\"expl"
    "icitGoal\":{\"owner\":\"external\",\"kind\":\"point\",\"point\":{\"x\":32,\"y\":64,\"z\":16},\"entityId\":-1},\"lastCmd\":{\"forward"
    "move\":0,\"sidemove\":0,\"upmove\":0,\"buttons\":0,\"impulse\":0,\"viewAngles\":{\"x\":0,\"y\":0,\"z\":0}}}},\"pendingChats\":[]}"
    ;
static const char brain_restored_json[] =
    "{\"version\":1,\"source\":\"q1-rerelease\",\"definition\":\"fixture\",\"rng\":794540887,\"brain\":{\"version\":1,\"skill\":\"easy"
    "\",\"gameMode\":{\"gameType\":\"coop\",\"weaponStay\":false,\"hasTeams\":true},\"memory\":{\"triggerWeapon\":0,\"triggerHeldSi"
    "nce\":-1,\"triggerReadyAt\":0,\"targetId\":-1,\"goalPoint\":null,\"goalEntityId\":-1,\"stuckTrips\":0,\"goalIsLive\":false,"
    "\"unstickUntil\":0,\"pressUntil\":0,\"unstickSide\":0,\"explicitGoalDone\":false,\"explicitGoalFailed\":false,\"wedgeOrig"
    "in\":null,\"wedgeSince\":-1,\"lastSafeOrigin\":null,\"restPoint\":null,\"restUntil\":0,\"guardRefusals\":0,\"lastGuardRefu"
    "sed\":false,\"gapJumps\":0,\"hazardFrames\":0,\"ownObjectiveHome\":null,\"enemyObjectiveHome\":null,\"objectiveRole\":\"\","
    "\"touchGoal\":false,\"holdPosition\":false,\"gateShootAt\":null,\"gateFiredAt\":-1,\"coopRegrouping\":false,\"coopRegroup"
    "At\":0,\"coopRegroupUntil\":0,\"levelStarted\":false,\"checkSixUntil\":0,\"checkSixYaw\":123.5,\"checkSixNextAt\":0,\"roam"
    "Point\":null,\"roamUntil\":0,\"spawnedOnce\":false,\"lastWeaponNumber\":0,\"deadSince\":-1,\"respawnWait\":0,\"respawnPres"
    "s\":false,\"aim\":{\"pitch\":0,\"yaw\":0,\"pitchVelocity\":0,\"yawVelocity\":0,\"modifierUntil\":0},\"pathState\":{\"path\":nul"
    "l,\"index\":0,\"stuckOrigin\":{\"x\":0,\"y\":0,\"z\":0},\"stuckSince\":0,\"stuckCount\":0,\"liftWaitSince\":-1,\"liftWaitZ\":0,\""
    "jumpReadyAt\":0,\"plannedAt\":0},\"explicitGoal\":{\"owner\":\"external\",\"kind\":\"point\",\"point\":{\"x\":32,\"y\":64,\"z\":16}"
    ",\"entityId\":-1},\"lastCmd\":{\"forwardmove\":0,\"sidemove\":0,\"upmove\":0,\"buttons\":0,\"impulse\":0,\"viewAngles\":{\"x\":0"
    ",\"y\":0,\"z\":0}},\"awareness\":[],\"unreachableUntil\":[],\"objectiveHome\":[],\"saidThisLevel\":[]}},\"pendingChats\":[]}"
    ;
static const char brain_legacy_json[] =
    "{\"version\":1,\"source\":\"q1-rerelease\",\"definition\":\"fixture\",\"rng\":794540887,\"brain\":{\"version\":1,\"skill\":\"easy"
    "\",\"gameMode\":{\"gameType\":\"coop\",\"weaponStay\":false,\"hasTeams\":true},\"memory\":{\"triggerWeapon\":0,\"triggerHeldSi"
    "nce\":-1,\"triggerReadyAt\":0,\"aim\":{\"pitch\":0,\"yaw\":0,\"pitchVelocity\":0,\"yawVelocity\":0,\"modifierUntil\":0},\"path"
    "State\":{\"path\":null,\"index\":0,\"stuckOrigin\":{\"x\":0,\"y\":0,\"z\":0},\"stuckSince\":0,\"stuckCount\":0,\"jumpReadyAt\":0,"
    "\"plannedAt\":0,\"liftWaitSince\":-1,\"liftWaitZ\":0},\"targetId\":-1,\"goalPoint\":null,\"goalEntityId\":-1,\"stuckTrips\":"
    "0,\"goalIsLive\":false,\"unstickUntil\":0,\"pressUntil\":0,\"unstickSide\":0,\"explicitGoal\":{\"owner\":\"external\",\"kind\""
    ":\"point\",\"point\":{\"x\":32,\"y\":64,\"z\":16},\"entityId\":-1},\"explicitGoalDone\":false,\"explicitGoalFailed\":false,\"we"
    "dgeOrigin\":null,\"wedgeSince\":-1,\"lastSafeOrigin\":null,\"restPoint\":null,\"restUntil\":0,\"guardRefusals\":0,\"lastGu"
    "ardRefused\":false,\"gapJumps\":0,\"hazardFrames\":0,\"ownObjectiveHome\":null,\"enemyObjectiveHome\":null,\"objectiveRo"
    "le\":\"\",\"touchGoal\":false,\"holdPosition\":false,\"gateShootAt\":null,\"gateFiredAt\":-1,\"coopRegrouping\":false,\"coop"
    "RegroupAt\":0,\"coopRegroupUntil\":0,\"levelStarted\":false,\"checkSixUntil\":0,\"checkSixNextAt\":0,\"roamPoint\":null,\""
    "roamUntil\":0,\"lastCmd\":{\"forwardmove\":0,\"sidemove\":0,\"upmove\":0,\"buttons\":0,\"impulse\":0,\"viewAngles\":{\"x\":0,\"y"
    "\":0,\"z\":0}},\"spawnedOnce\":false,\"lastWeaponNumber\":0,\"deadSince\":-1,\"respawnWait\":0,\"respawnPress\":false,\"awar"
    "eness\":[],\"unreachableUntil\":[],\"objectiveHome\":[],\"saidThisLevel\":[]}},\"pendingChats\":[]}"
    ;
/* legacy restore checkSixYaw: 0 */
typedef struct brain_fixture { const void *sentinel; } brain_fixture;
static brain_fixture *bf(void *ctx) { brain_fixture *b = ctx; CHECK(b != NULL && b->sentinel == b); return b; }
static double cb_time(void *ctx) { bf(ctx); return 0; }
static void cb_pre(void *ctx, const char *name, qa_rerelease_orders orders) { bf(ctx); (void)name; (void)orders; }
static void cb_post(void *ctx, const char *name, qa_bot_usercmd cmd) { bf(ctx); (void)name; (void)cmd; }
static void cb_chat(void *ctx, qa_bot_chat_event event) { bf(ctx); (void)event; }
static void cb_select(void *ctx, double weapon) { bf(ctx); (void)weapon; }
static double cb_impulse(void *ctx, double weapon) { bf(ctx); (void)weapon; return 0; }
static bool cb_near(void *ctx) { bf(ctx); return false; }
static void check_bytes(const char *tag, const qa_save_value *value, const char *expected) {
  size_t length = 0;
  uint8_t *bytes = qa_encode_checkpoint_value(value, &length);
  CHECK(bytes != NULL);
  checks++;
  if (bytes == NULL || length != strlen(expected) || memcmp(bytes, expected, length) != 0) {
    failures++;
    fprintf(stderr, "%s bytes differ\n  have: %.*s\n  want: %s\n", tag, (int)length, bytes != NULL ? (const char *)bytes : "", expected);
  }
  free(bytes);
}
static qa_save_value *encode_behavior(const qa_rerelease_behavior_checkpoint *checkpoint) {
  qa_save_value *value = qa_app_sim_encode_rerelease_behavior(checkpoint);
  CHECK(value != NULL);
  return value;
}
static void read_json(const char *json, qa_rerelease_behavior_checkpoint *out) {
  qa_save_value *value = qa_decode_checkpoint_value((const uint8_t *)json, strlen(json));
  CHECK(value != NULL);
  CHECK(qa_read_rerelease_behavior(out, value));
  qa_save_value_free(value);
}
static void test_brain_checkpoint(void) {
  static brain_fixture b;
  b.sentinel = &b;
  static qa_bot_skill_settings skill;
  qa_bot_skill_settings_init(&skill); /* TS settings 'skill easy\n{\n}\n' */
  skill.skill = "easy";
  qa_bot_knowledge knowledge;
  memset(&knowledge, 0, sizeof knowledge);
  knowledge.settings_r.skills = &skill;
  knowledge.settings_r.skill_count = 1;
  qa_rerelease_profile_options options;
  memset(&options, 0, sizeof options);
  options.definition = "fixture"; options.source = QA_RERELEASE_Q1; options.knowledge = &knowledge; options.skill = "easy";
  options.seed = 7;
  options.game_mode.game_type = "coop"; options.game_mode.weapon_stay = false;
  options.game_mode.has_has_teams = true; options.game_mode.has_teams = true;
  options.max_health = 100; options.run_speed = 320; options.walk_speed = 160;
  options.callbacks = (qa_rerelease_callbacks){&b, cb_time, cb_pre, cb_post, cb_chat, cb_select, cb_impulse, cb_near};
  qa_rerelease_bot_behavior behavior;
  CHECK(qa_rerelease_bot_behavior_init(&behavior, options));
  qa_rerelease_bot_behavior_request_move_to_point(&behavior, (qa_bot_vec3){32, 64, 16});
  /* never restored: newBrainMemory property order */
  qa_rerelease_behavior_checkpoint fresh;
  CHECK(qa_rerelease_bot_behavior_checkpoint_make(&behavior, &fresh));
  CHECK(fresh.brain.memory.check_six_yaw == 0);
  qa_save_value *value = encode_behavior(&fresh);
  check_bytes("fresh", value, brain_fresh_json);
  qa_save_value_free(value);
  /* { ...memory, checkSixYaw: 123.5 } keeps the key in place */
  fresh.brain.memory.check_six_yaw = 123.5;
  value = encode_behavior(&fresh);
  check_bytes("saved", value, brain_saved_json);
  size_t length = 0;
  uint8_t *saved = qa_encode_checkpoint_value(value, &length);
  qa_save_value_free(value);
  qa_save_value *decoded = qa_decode_checkpoint_value(saved, length);
  free(saved);
  qa_rerelease_behavior_checkpoint read;
  CHECK(qa_read_rerelease_behavior(&read, decoded));
  qa_save_value_free(decoded);
  CHECK(read.brain.memory.check_six_yaw == 123.5);
  /* the read object: memory() order */
  value = encode_behavior(&read);
  check_bytes("read", value, brain_read_json);
  qa_save_value_free(value);
  CHECK(qa_rerelease_bot_behavior_restore(&behavior, &read));
  CHECK(behavior.brain.state.check_six_yaw == 123.5);
  /* restored: read scalars, then awareness, unreachableUntil, objectiveHome, saidThisLevel */
  qa_rerelease_behavior_checkpoint restored;
  CHECK(qa_rerelease_bot_behavior_checkpoint_make(&behavior, &restored));
  CHECK(restored.brain.memory.check_six_yaw == 123.5);
  value = encode_behavior(&restored);
  check_bytes("restored", value, brain_restored_json);
  qa_save_value_free(value);
  /* a save without checkSixYaw restores 0 (bun: 0) */
  qa_rerelease_behavior_checkpoint legacy;
  read_json(brain_legacy_json, &legacy);
  CHECK(legacy.brain.memory.check_six_yaw == 0);
  CHECK(qa_rerelease_bot_behavior_restore(&behavior, &legacy));
  CHECK(behavior.brain.state.check_six_yaw == 0);
  qa_rerelease_behavior_checkpoint_destroy(&legacy);
  qa_rerelease_behavior_checkpoint_destroy(&restored);
  qa_rerelease_behavior_checkpoint_destroy(&read);
  qa_rerelease_behavior_checkpoint_destroy(&fresh);
  qa_rerelease_bot_behavior_destroy(&behavior);
}

/* ---- 7. GameAiContext.nodeSwitches: an unbounded string[] ---- */
typedef struct node_fixture { const void *sentinel; char *prints[96]; size_t print_count; } node_fixture;
static node_fixture *nf(void *ctx) { node_fixture *n = ctx; CHECK(n != NULL && n->sentinel == n); return n; }
static const char *node_configstring(void *ctx, double index) { nf(ctx); (void)index; return "\\n\\Sarge\\t\\0"; }
static void node_print(void *ctx, const char *text) {
  node_fixture *n = nf(ctx);
  if (n->print_count < 96) n->prints[n->print_count] = strdup(text);
  n->print_count++;
}
static void test_node_switches(void) {
  static node_fixture n;
  memset(&n, 0, sizeof n);
  n.sentinel = &n;
  qa_q3_config_string_store store = {&n, node_configstring, NULL};
  qa_source_bot_game_bots_a game;
  memset(&game, 0, sizeof game);
  game.ctx = &n;
  game.options.configstrings = &store;
  game.options.engine.ctx = &n; game.options.engine.insert_ctx = &n;
  game.options.engine.print = node_print;
  qa_game_ai_context_bots_a *context = calloc(1, sizeof *context);
  qa_bot_state_source *source = calloc(1, sizeof *source);
  CHECK(context != NULL && source != NULL);
  if (context == NULL || source == NULL) return;
  context->game = &game;
  context->time = 1.5;
  qa_bot_state_bots_a state;
  memset(&state, 0, sizeof state);
  state.src = source;
  qa_bot_reset_node_switches_bots_a(context);
  for (int i = 0; i < 75; i++) {
    char detail[32];
    snprintf(detail, sizeof detail, "goal %d", i);
    qa_bot_record_node_switch_bots_a(context, &state, "seek ltg", detail, i % 2 == 0 ? "BotDeathmatchAI" : "chat");
  }
  CHECK(context->node_switch_count == 75); /* bun: 75 */
  qa_bot_dump_node_switches_bots_a(context, &state);
  CHECK(n.print_count == 77); /* bun: 77 */
  if (n.print_count == 77) {
    CHECK(strcmp(n.prints[0], "Sarge at 1.5 switched more than 50 AI nodes\n") == 0);
    CHECK(strcmp(n.prints[1], "Sarge at  1.5 entered seek ltg: goal 0 from BotDeathmatchAI\n") == 0);
    CHECK(strcmp(n.prints[75], "Sarge at  1.5 entered seek ltg: goal 74 from BotDeathmatchAI\n") == 0);
    CHECK(strcmp(n.prints[76], "^1Fatal: ") == 0);
  }
  for (size_t i = 0; i < n.print_count && i < 96; i++) free(n.prints[i]);
  qa_bot_reset_node_switches_bots_a(context);
  CHECK(context->node_switch_count == 0);
  free(context->node_switches); /* the context's other members were never initialized */
  free(source);
  free(context);
}

int main(void) {
  test_shared_world_random();
  test_q2_weapon_info();
  test_jump_pad();
  test_brain_checkpoint();
  test_node_switches();
  printf("test_bot_fidelity: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
