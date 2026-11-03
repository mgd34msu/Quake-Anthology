/*
 * JS bitwise operators on TS numbers convert with ToInt32 and return a signed
 * 32-bit result. Ports that computed them in int64_t or uint32_t kept bit 31
 * positive, kept bits above 2^31, or cast an out-of-range double (undefined).
 * Expected values were printed by the TS under bun:
 *   q2RereleaseColor(...) from src/content/q2/rerelease/lights.ts, and
 *   `let s = 0; s |= 0x80000001` (monsterDabeam's beam.spawnflags) -> -2147483647.
 * GPL-2.0-or-later.
 */
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "qa/content/q2/missionpacks.h"
#include "qa/content/q2/missionpacks/monsters/dabeam.h"
#include "qa/content/q2/rerelease/lights.h"

static int failures = 0;
static int checks = 0;
#define CHECK(cond)                                                     \
  do {                                                                  \
    checks++;                                                           \
    if (!(cond)) {                                                      \
      failures++;                                                       \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
    }                                                                   \
  } while (0)

static void test_rerelease_color(void) {
  /* bun: q2RereleaseColor(text) */
  CHECK(qa_q2_rerelease_color("300 128 64") == 746602497);
  CHECK(qa_q2_rerelease_color("-1 0 0") == 16777471);
  CHECK(qa_q2_rerelease_color("1e10 0 0") == 1);
  CHECK(qa_q2_rerelease_color("1 0.5 0.25") == -8437761);
  CHECK(qa_q2_rerelease_color("255 255 255 255") == -1);
  CHECK(qa_q2_rerelease_color("nan 1 1") == 16777215);
  CHECK(qa_q2_rerelease_color("0.5 0.5 0.5 0.5") == 2139062143);
  CHECK(qa_q2_rerelease_color("4294967296 2 3 4") == 131844);
  CHECK(qa_q2_rerelease_color("1 1 1 -2") == -254);
}

/* Minimal Q2 game: only what monsterDabeam touches with no target. */
#define POOL 4
static qa_q2_entity pool[POOL];
static size_t pool_count;
static int game_sentinel, host_sentinel;
static qa_q2_entity *fake_create(void *ctx, const char *classname, const qa_q2_field_pair *values, size_t count) {
  CHECK(ctx == &game_sentinel); (void)values; (void)count;
  size_t slot = pool_count++;
  qa_q2_spawn_fields fields = {0};
  fields.classname = classname;
  qa_owned_actor owned = {0};
  owned.id.slot = (int64_t)slot + 1;
  owned.id.generation = 1;
  qa_q2_entity_init(&pool[slot], owned, fields);
  return &pool[slot];
}
static void fake_move(void *ctx, qa_q2_entity *e, qa_q2_body_changes c, bool link) {
  CHECK(ctx == &game_sentinel); (void)e; (void)c; (void)link;
}
static void fake_solid(void *ctx, qa_q2_entity *e, const char *s) { CHECK(ctx == &game_sentinel); e->solid = s; }
static void fake_motion(void *ctx, qa_q2_entity *e, const char *k) { CHECK(ctx == &game_sentinel); e->motion = k; }
static void fake_schedule(void *ctx, qa_q2_entity *e, double delay, qa_q2_think think) {
  CHECK(ctx == &game_sentinel); (void)delay; e->think = think;
}
static double fake_now(void *ctx) { CHECK(ctx == &host_sentinel); return 1; }

static void test_dabeam_spawnflags(void) {
  qa_q2_foundation_host host = {0};
  host.ctx = &host_sentinel;
  host.now = fake_now;
  qa_q2_game_services game = {0};
  game.ctx = &game_sentinel;
  game.host = &host;
  game.options.edition = QA_Q2_EDITION_RERELEASE;
  game.create = fake_create;
  game.move = fake_move;
  game.solid = fake_solid;
  game.motion = fake_motion;
  game.schedule = fake_schedule;
  game.source_callbacks = qa_q2_source_callbacks_create();
  qa_q2_entity *owner = fake_create(&game_sentinel, "monster_medic_commander", NULL, 0);
  qa_q2_entity *beam = qa_q2mpm_monster_dabeam(owner, &game, NULL, (qa_vec3){0, 0, 0}, (qa_vec3){0, 90, 0}, 5, true);
  /* bun: `let s = 0; s |= 0x80000001` is -2147483647 (the old uint32 port stored 2147483649). */
  CHECK(beam->spawnflags == -2147483647.0);
  CHECK(beam->skin == 4092850673.0); /* 0xf3f3f1f1 stays a positive number */
  qa_q2_source_callbacks_destroy(game.source_callbacks);
}

int main(void) {
  test_rerelease_color();
  test_dabeam_spawnflags();
  printf("test_js_semantics_bitwise: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
