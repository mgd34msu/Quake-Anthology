/*
 * C checks for src/content/q3 rows: misc.ts teleportPlayer runs the full
 * playerStateToEntityState conversion; client-events.ts clientEvents walks
 * ps.eventSequence (not a stale mirror field) and useQ3Holdable;
 * entities.ts addPredictableEvent returns the pre-increment sequence and
 * addEvent stamps externalEventTime. Expected values from the TypeScript
 * functions run under bun on the same inputs. GPL-2.0-or-later.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q3/base/game/entities.h"
#include "q3_fake_entity.h"
#include "qa/content/q3/base/game/misc.h"
#include "qa/content/q3/base/game/rankings.h"
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/content/q3/team_arena/client_events.h"
#include "qa/core/math.h"

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

enum { ENTITY_COUNT = 96 };
static qa_q3_game_entity entities[ENTITY_COUNT];
static q3_fake_entity bodies[ENTITY_COUNT];
static qa_q3_game_client clients[64];
static bool clients_live;
static qa_q3_entity_pool pool;
static int sentinel, unlinks, world_links, fires, kamikazes;
static double unlinked_number;
static double ranking_reports[8]; static size_t ranking_count;

static qa_q3_game_entity *rec_get(void *ctx, double slot) { (void)ctx; return slot >= 0 && slot < ENTITY_COUNT ? &entities[(int)slot] : NULL; }
static qa_q3_game_entity *rec_activate(void *ctx, double slot) {
  (void)ctx; int i = (int)slot;
  q3_fake_entity_init(&entities[i], &bodies[i], slot); bodies[i].active = true; return &entities[i];
}
static qa_q3_game_client *rec_client(void *ctx, double slot) { (void)ctx; return &clients[(int)slot]; }
static void rec_release(void *ctx, qa_q3_game_entity *e) { (void)ctx; bodies[(int)e->slot].active = false; }
static void rec_deactivate(void *ctx, double slot) { (void)ctx; (void)slot; }
static double pool_time(void *ctx) { CHECK(ctx == &sentinel); return 4321; }
static void pool_print(void *ctx, const char *m) { (void)m; CHECK(ctx == &sentinel); }

static bool pool_live;
static void pool_setup(qa_q3_base_product product) {
  if (pool_live) qa_q3_entity_pool_destroy(&pool);
  pool_live = true;
  for (int i = 0; i < ENTITY_COUNT; i++) bodies[i].active = false;
  if (clients_live) for (int i = 0; i < 64; i++) qa_q3_game_client_destroy(&clients[i]);
  for (int i = 0; i < 64; i++) qa_q3_game_client_init(&clients[i], product);
  clients_live = true;
  qa_q3_entity_pool_options o; memset(&o, 0, sizeof o);
  o.records.get = rec_get; o.records.activate = rec_activate; o.records.client = rec_client;
  o.records.release = rec_release; o.records.deactivate_client = rec_deactivate;
  o.product = product; o.max_clients = 8; o.ctx = &sentinel; o.time = pool_time; o.print = pool_print;
  qa_q3_entity_pool_init(&pool, o);
  unlinks = world_links = fires = kamikazes = 0; ranking_count = 0;
}

static void world_link(void *ctx, qa_q3_game_entity *e) { (void)e; CHECK(ctx == &sentinel); world_links++; }
static void world_unlink(void *ctx, double n) { CHECK(ctx == &sentinel); unlinks++; unlinked_number = n; }

/* bun: teleportPlayer as a spectator (no temp entities, no kill box). */
static void test_teleport_converts_player_state(void) {
  pool_setup(QA_Q3_BASE_PRODUCT_BASEQ3);
  qa_q3_game_entity *player = rec_activate(NULL, 4); player->client = &clients[4];
  qa_q3_game_client *c = &clients[4];
  c->sess.session_team = 3;
  c->ps.client_num = 4; qa_q3_base_player_state_record_set_health(&c->ps, 75); c->ps.weapon = 3; c->ps.legs_anim = 12; c->ps.torso_anim = 9;
  c->ps.movement_dir = 2; c->ps.e_flags = 0x41; c->ps.ground_entity_num = 1023; c->ps.loop_sound = 5; c->ps.generic1 = 1;
  qa_q3_base_player_state_slots_set(&c->ps.powerups, 2, 9000);
  qa_q3_base_player_state_record_add_event(&c->ps, 23, 4);
  c->pers.cmd.angles = (qa_vec3){10, 20, 0};
  player->s.number = 4; player->s.client_num = 4; player->s.pos.time = 77; player->s.pos.duration = 88;
  player->s.origin = (qa_vec3){-1, -1, -1};
  qa_q3_teleport_context context; memset(&context, 0, sizeof context);
  context.world.ctx = &sentinel; context.world.link = world_link; context.world.unlink = world_unlink;
  qa_q3_teleport_player(context, player, (qa_vec3){100.75, -50.25, 30.5}, (qa_vec3){0, 90.5, 0});
  qa_q3_entity_state_record *s = &player->s;
  CHECK(s->e_type == 1); CHECK(s->number == 4); CHECK(s->pos.type == QA_Q3_BASE_TR_INTERPOLATE);
  CHECK(s->pos.base.x == 100 && s->pos.base.y == -50 && s->pos.base.z == 31);
  CHECK(s->pos.delta.x == -3.4906120300292969); CHECK(s->pos.delta.y == 399.98477172851563); CHECK(s->pos.delta.z == 0);
  CHECK(s->pos.time == 77 && s->pos.duration == 88);
  CHECK(s->apos.type == QA_Q3_BASE_TR_INTERPOLATE);
  CHECK(s->apos.base.x == 0 && s->apos.base.y == 90 && s->apos.base.z == 0);
  CHECK(s->angles2.y == 2); CHECK(s->legs_anim == 12 && s->torso_anim == 9); CHECK(s->client_num == 4);
  CHECK(s->e_flags == 68);
  CHECK(s->event == 23 && s->event_parm == 4); CHECK(c->ps.entity_event_sequence == 1);
  CHECK(s->weapon == 3 && s->ground_entity_num == 1023); CHECK(s->powerups == 4);
  CHECK(s->loop_sound == 5 && s->generic1 == 1);
  /* TS leaves s.origin alone; r.currentOrigin is the unsnapped ps origin. */
  CHECK(s->origin.x == -1 && s->origin.y == -1 && s->origin.z == -1);
  CHECK(qa_q3_base_entity_shared_current_origin(&player->r).x == 100.75 && qa_q3_base_entity_shared_current_origin(&player->r).y == -50.25 && qa_q3_base_entity_shared_current_origin(&player->r).z == 31.5);
  CHECK(c->ps.pm_flags == 64 && c->ps.e_flags == 69);
  CHECK(unlinks == 1 && unlinked_number == 4 && world_links == 0);
  CHECK(c->ps.delta_angles.x == -10);
  CHECK(c->ps.delta_angles.y == 16455);
}

static void ranking_sink(void *ctx, qa_q3_ranking_report report) {
  CHECK(ctx == &sentinel);
  if (ranking_count + 2 <= 8) { ranking_reports[ranking_count++] = report.self; ranking_reports[ranking_count++] = report.key; }
}
static bool no_warmup(void *ctx) { CHECK(ctx == &sentinel); return false; }
static void fire_weapon(void *ctx, qa_q3_game_entity *e) { (void)e; CHECK(ctx == &sentinel); fires++; }
static void start_kamikaze(void *ctx, qa_q3_game_entity *e) { (void)e; CHECK(ctx == &sentinel); kamikazes++; }

static qa_q3_ta_client_events_context events_context(qa_q3_base_product product, qa_q3_game_combat_context *combat) {
  qa_q3_ta_client_events_context context; memset(&context, 0, sizeof context);
  context.product = product; context.combat = combat; context.ctx = &sentinel;
  context.fire_weapon = fire_weapon; context.start_kamikaze = start_kamikaze;
  return context;
}

/* bun: events ITEM2, FIRE_WEAPON, ITEM5 at sequences 0..2; clientEvents(old 0)
 * clamps to sequence 1, so ITEM2 (overwritten) never runs. */
static void test_client_events_use_event_sequence(void) {
  CHECK(QA_Q3_EV_USE_ITEM1 == 25 && QA_Q3_EV_USE_ITEM2 == 26 && QA_Q3_EV_USE_ITEM5 == 29 && QA_Q3_EV_FIRE_WEAPON == 23);
  qa_q3_base_product products[2] = {QA_Q3_BASE_PRODUCT_BASEQ3, QA_Q3_BASE_PRODUCT_MISSIONPACK};
  qa_q3_base_product base_products[2] = {QA_Q3_BASE_PRODUCT_BASEQ3, QA_Q3_BASE_PRODUCT_MISSIONPACK};
  double invulnerability[2] = {0, 12500};
  for (int p = 0; p < 2; p++) {
    pool_setup(products[p]);
    qa_q3_ranking_reports_attach(&pool.rankings, &sentinel, ranking_sink, no_warmup);
    static qa_q3_game_combat_context combat; memset(&combat, 0, sizeof combat);
    combat.entities = &pool; combat.time = 2500;
    qa_q3_game_entity *entity = rec_activate(NULL, 2); entity->client = &clients[2]; entity->s.e_type = 1;
    qa_q3_game_client *c = &clients[2];
    qa_q3_base_player_state_slots_set(&c->ps.stats, qa_q3_base_stat_schema_for(base_products[p])->max_health, 100);
    qa_q3_base_player_state_record_add_event(&c->ps, QA_Q3_EV_USE_ITEM2, 0);
    qa_q3_base_player_state_record_add_event(&c->ps, QA_Q3_EV_FIRE_WEAPON, 0);
    qa_q3_base_player_state_record_add_event(&c->ps, QA_Q3_EV_USE_ITEM5, 0);
    qa_q3_ta_client_events_context context = events_context(base_products[p], &combat);
    qa_q3_ta_client_events(&context, entity, 0);
    CHECK(c->ps.event_sequence == 3); CHECK(qa_q3_game_entity_health(entity) == 0);
    CHECK(ranking_count == 0); CHECK(fires == 1); CHECK(c->invulnerability_time == invulnerability[p]);
    qa_q3_ranking_reports_detach(&pool.rankings);
  }
  /* bun: eventSequence 5, ITEM2 added, clientEvents(old 5) -> health 125, useHoldable(2, 2). */
  pool_setup(QA_Q3_BASE_PRODUCT_BASEQ3);
  qa_q3_ranking_reports_attach(&pool.rankings, &sentinel, ranking_sink, no_warmup);
  static qa_q3_game_combat_context combat; memset(&combat, 0, sizeof combat); combat.entities = &pool;
  qa_q3_game_entity *entity = rec_activate(NULL, 2); entity->client = &clients[2]; entity->s.e_type = 1;
  qa_q3_game_client *c = &clients[2];
  qa_q3_base_player_state_slots_set(&c->ps.stats, 6, 100); c->ps.event_sequence = 5;
  qa_q3_base_player_state_record_add_event(&c->ps, QA_Q3_EV_USE_ITEM2, 0);
  CHECK(qa_q3_base_player_state_slots_get(&c->ps.events, 1) == 26);
  qa_q3_ta_client_events_context context = events_context(QA_Q3_BASE_PRODUCT_BASEQ3, &combat);
  qa_q3_ta_client_events(&context, entity, 5);
  CHECK(qa_q3_game_entity_health(entity) == 125);
  CHECK(ranking_count == 2 && ranking_reports[0] == 2 && ranking_reports[1] == 1111070001);
  qa_q3_ranking_reports_detach(&pool.rankings);
}

/* bun: addEvent at sequence 3 returns {sequence 3}, eventSequence 4, slot 1. */
static void test_entity_pool_events(void) {
  pool_setup(QA_Q3_BASE_PRODUCT_BASEQ3);
  qa_q3_game_entity *entity = rec_activate(NULL, 1); entity->client = &clients[1];
  clients[1].ps.event_sequence = 3;
  qa_q3_base_predictable_event out; memset(&out, 0, sizeof out);
  CHECK(qa_q3_entity_pool_add_predictable_event(&pool, entity, QA_Q3_EV_FIRE_WEAPON, 3, &out));
  CHECK(out.sequence == 3 && out.event == 23 && out.parameter == 3);
  CHECK(clients[1].ps.event_sequence == 4);
  CHECK(qa_q3_base_player_state_slots_get(&clients[1].ps.events, 1) == 23);
  CHECK(qa_q3_base_player_state_slots_get(&clients[1].ps.event_parms, 1) == 3);
  /* entities.ts addEvent: externalEventTime = time(). */
  clients[1].ps.external_event = 0x100 | 7;
  qa_q3_entity_pool_add_event(&pool, entity, 40, 2);
  CHECK(clients[1].ps.external_event == (40 | 0x200)); CHECK(clients[1].ps.external_event_parm == 2);
  CHECK(clients[1].ps.external_event_time == 4321);
}

int main(void) {
  test_teleport_converts_player_state();
  test_client_events_use_event_sequence();
  test_entity_pool_events();
  if (pool_live) qa_q3_entity_pool_destroy(&pool);
  for (int i = 0; i < 64; i++) qa_q3_game_client_destroy(&clients[i]);
  printf("test_q3_rows_2: %d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
