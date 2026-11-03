/*
 * One representation for Q3 game state (GameEntity.r, GameClient.ps).
 * Each check would fail over the removed *_mirror copies:
 *  - EntityShared.model defaults to {kind:"box"} (the mirror zeroed it to inline 0);
 *  - entity.health and ps.health/stats[health] are one combat value (the mirror kept two);
 *  - ps.deltaAngles is one vector that setClientViewAngle writes and readQ3MovementState
 *    reads (the mirror wrote slots and read a separate vector);
 *  - the PlayerAuthorityBinding keeps origin/velocity in the body table across
 *    EntityPool.initializeClients, and ammoTimes.set notifies ammoTimerStored;
 *  - addEvent prints the bg_misc.c debug line through PredictableEventDebug.
 * Expected values come from running the TypeScript (Q3EntityRecords, EntityPool,
 * setClientViewAngle, readQ3MovementState) under bun with the same inputs:
 *   model {"kind":"box"}; health1 75 75; health2 30 30;
 *   delta {"x":16284,"y":8192,"z":5} [16284,8192,5];
 *   after reset {"x":1,"y":2,"z":3} {"x":4,"y":5,"z":6} 30 0;
 *   stored 0:3=900 0:0=0 ... 0:10=0;
 *   event {"sequence":3,"event":23,"parameter":7};
 *   printed " game event svt    12 ->     3: num =       EV_FIRE_WEAPON parm 7\n".
 * Every host callback checks its sentinel ctx. GPL-2.0-or-later.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/app/bootstrap/simulation/q3/player_state.h"
#include "qa/content/q3/base/game/entities.h"
#include "qa/content/q3/base/records.h"
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/content/q3/team_arena/client_spawn.h"
#include "qa/world/actors/body.h"
#include "qa/world/actors/callbacks.h"
#include "qa/world/actors/registry.h"
#include "qa/world/gameplay/authority.h"
#include "qa/world/gameplay/inventory.h"

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_V(v, a, b, c) CHECK((v).x == (a) && (v).y == (b) && (v).z == (c))

static int host_sentinel, pool_sentinel, debug_sentinel, gameplay_sentinel, body_sentinel;
static int ctx_errors;
#define EXPECT_CTX(ctx, sentinel) do { if ((ctx) != (void *)&(sentinel)) ctx_errors++; } while (0)

static qa_bounds fx_abs(void *ctx, qa_owned_actor a, qa_body_state s) { EXPECT_CTX(ctx, body_sentinel); return qa_translated_body_bounds(a, s); }
static void fx_on_link(void *ctx, qa_linked_body b) { (void)b; EXPECT_CTX(ctx, body_sentinel); }
static void fx_on_unlink(void *ctx, qa_actor_id a) { (void)a; EXPECT_CTX(ctx, body_sentinel); }
static void fx_impulse(void *ctx, qa_owned_actor a, qa_vec3 i, qa_provider_id m) { (void)a; (void)i; (void)m; EXPECT_CTX(ctx, gameplay_sentinel); }
static void fx_before(void *ctx, qa_owned_actor a, qa_damage_decision d) { (void)a; (void)d; EXPECT_CTX(ctx, gameplay_sentinel); }
static void fx_confirmed(void *ctx, qa_damage_outcome o) { (void)o; EXPECT_CTX(ctx, gameplay_sentinel); }

static char stored_log[512];
static void host_ammo_stored(void *ctx, qa_actor_id actor, double weapon, double value) {
  EXPECT_CTX(ctx, host_sentinel);
  char item[32];
  snprintf(item, sizeof item, "%s%lld:%lld=%lld", stored_log[0] ? " " : "", (long long)actor.slot, (long long)weapon, (long long)value);
  strncat(stored_log, item, sizeof stored_log - strlen(stored_log) - 1);
}
static void host_schedule(void *ctx, qa_owned_actor a, bool has, double due) { (void)a; (void)has; (void)due; EXPECT_CTX(ctx, host_sentinel); }
static void host_run_think(void *ctx, qa_owned_actor a, double t) { (void)a; (void)t; EXPECT_CTX(ctx, host_sentinel); }
static bool host_damage_call(void *ctx, qa_q3_damage_call *out) { (void)out; EXPECT_CTX(ctx, host_sentinel); return false; }
static qa_q3_game_entity *host_foreign(void *ctx, qa_actor_id a) { (void)a; EXPECT_CTX(ctx, host_sentinel); return NULL; }
static bool host_is_player(void *ctx, qa_actor_id a) { (void)a; EXPECT_CTX(ctx, host_sentinel); return false; }

static double pool_time(void *ctx) { EXPECT_CTX(ctx, pool_sentinel); return 1000; }
static void pool_print(void *ctx, const char *m) { (void)m; EXPECT_CTX(ctx, pool_sentinel); }
static void pool_link(void *ctx, qa_q3_game_entity *e) { (void)e; EXPECT_CTX(ctx, pool_sentinel); }
static void pool_unlink(void *ctx, qa_q3_game_entity *e) { (void)e; EXPECT_CTX(ctx, pool_sentinel); }

static char printed[256];
static int prints;
static const char *debug_show_events(void *ctx) { EXPECT_CTX(ctx, debug_sentinel); return "1"; }
static void debug_print(void *ctx, const char *message) {
  EXPECT_CTX(ctx, debug_sentinel); prints++;
  snprintf(printed, sizeof printed, "%s", message);
}

int main(void) {
  qa_identity_owner identity;
  if (!qa_identity_owner_create(&identity, "q3-unified")) abort();
  qa_session_actor_registry *actors = qa_session_actor_registry_create(&identity, 2048);
  qa_body_link_hooks hooks; memset(&hooks, 0, sizeof hooks);
  hooks.ctx = &body_sentinel; hooks.absolute_bounds = fx_abs; hooks.on_link = fx_on_link; hooks.on_unlink = fx_on_unlink;
  qa_shared_body_table *bodies = qa_shared_body_table_create(actors, hooks);
  qa_actor_callback_table *callbacks = qa_actor_callback_table_create(actors);
  qa_gameplay_hooks g; memset(&g, 0, sizeof g);
  g.ctx = &gameplay_sentinel; g.impulse = fx_impulse; g.before_reaction = fx_before; g.confirmed = fx_confirmed;
  qa_gameplay_authority *combat = qa_gameplay_authority_create(actors, callbacks, g);
  qa_shared_inventory_table *inventory = qa_shared_inventory_table_create(actors);
  if (!actors || !bodies || !callbacks || !combat || !inventory) abort();

  qa_q3_base_record_host rh; memset(&rh, 0, sizeof rh);
  rh.actors = actors; rh.bodies = bodies; rh.combat = combat; rh.inventory = inventory; rh.callbacks = callbacks;
  rh.ctx = &host_sentinel; rh.ammo_timer_stored = host_ammo_stored; rh.schedule = host_schedule;
  rh.run_think = host_run_think; rh.damage_call = host_damage_call; rh.foreign = host_foreign; rh.is_player = host_is_player;
  qa_q3_base_entity_records *records = qa_q3_base_entity_records_create(rh, "q3:unified", QA_Q3_BASE_PRODUCT_BASEQ3);
  if (!records) abort();

  qa_q3_base_predictable_event_debug debug; memset(&debug, 0, sizeof debug);
  debug.ctx = &debug_sentinel; debug.module_is_game = true; debug.show_events = debug_show_events; debug.print = debug_print;
  qa_q3_entity_pool pool;
  qa_q3_entity_pool_options po; memset(&po, 0, sizeof po);
  po.event_debug = &debug; po.records = qa_q3_base_entity_records_pool_table(records);
  po.product = QA_Q3_BASE_PRODUCT_BASEQ3; po.max_clients = 4; po.map_start_time = 0;
  po.ctx = &pool_sentinel; po.time = pool_time; po.print = pool_print; po.link = pool_link; po.unlink = pool_unlink;
  qa_q3_entity_pool_init(&pool, po);

  /* 1. EntityShared model default. */
  qa_q3_game_entity *e70 = qa_q3_base_entity_records_get(records, 70);
  CHECK(e70->r.model.kind == QA_Q3_BASE_MODEL_BOX);

  /* 2. health has one source: the combat authority. */
  qa_q3_game_entity *player = qa_q3_entity_pool_activate_client(&pool, 0);
  qa_q3_game_client *client = player->client;
  const qa_q3_base_stat_schema *schema = qa_q3_base_stat_schema_for(QA_Q3_BASE_PRODUCT_BASEQ3);
  qa_q3_game_entity_set_health(player, 75);
  CHECK(qa_q3_base_player_state_record_health(&client->ps) == 75);
  CHECK(qa_q3_base_player_state_slots_get(&client->ps.stats, schema->health) == 75);
  qa_q3_base_player_state_record_set_health(&client->ps, 30);
  CHECK(qa_q3_game_entity_health(player) == 30);
  qa_combat_state_result combat_state = qa_gameplay_authority_read(combat, qa_q3_game_entity_actor(player).id);
  CHECK(combat_state.has_value && combat_state.value.health == 30);

  /* 3. deltaAngles has one source for spawn and movement. */
  client->pers.cmd.angles = (qa_vec3){100, 0, -5};
  qa_q3_ta_set_client_view_angle(player, (qa_vec3){90, 45, 0});
  CHECK_V(client->ps.delta_angles, 16284, 8192, 5);
  qa_actor_id jump_pad;
  qa_q3_movement_state movement = qa_app_read_q3_movement_state(player, records, &jump_pad);
  CHECK(movement.delta_angle_words.values[0] == 16284 && movement.delta_angle_words.values[1] == 8192
      && movement.delta_angle_words.values[2] == 5);

  /* 4. Authority keeps origin/velocity in the body table across client reset. */
  qa_q3_base_player_state_record_set_origin(&client->ps, (qa_vec3){1, 2, 3});
  qa_q3_base_player_state_record_set_velocity(&client->ps, (qa_vec3){4, 5, 6});
  qa_body_state_result body = qa_shared_body_table_read(bodies, qa_q3_game_entity_actor(player).id);
  CHECK(body.has_value);
  if (body.has_value) { CHECK_V(body.value.origin, 1, 2, 3); CHECK_V(body.value.velocity, 4, 5, 6); }
  /* ...and r.currentOrigin reads that same body. */
  CHECK_V(qa_q3_base_entity_shared_current_origin(&player->r), 1, 2, 3);
  qa_q3_base_player_state_slots_set(&client->ammo_times, 3, 900);
  qa_q3_entity_pool_initialize_clients(&pool, 4);
  CHECK(pool.clients[0] == client && player->client == client);
  CHECK_V(qa_q3_base_player_state_record_origin(&client->ps), 1, 2, 3);
  CHECK_V(qa_q3_base_player_state_record_velocity(&client->ps), 4, 5, 6);
  CHECK(qa_q3_base_player_state_record_health(&client->ps) == 30);
  CHECK(qa_q3_base_player_state_slots_get(&client->ammo_times, 3) == 0);
  CHECK(strcmp(stored_log, "0:3=900 0:0=0 0:1=0 0:2=0 0:3=0 0:4=0 0:5=0 0:6=0 0:7=0 0:8=0 0:9=0 0:10=0") == 0);
  if (strcmp(stored_log, "0:3=900 0:0=0 0:1=0 0:2=0 0:3=0 0:4=0 0:5=0 0:6=0 0:7=0 0:8=0 0:9=0 0:10=0") != 0)
    fprintf(stderr, "stored: %s\n", stored_log);

  /* 5. addEvent prints the debug line. */
  client->ps.pmove_framecount = 12; client->ps.event_sequence = 3;
  qa_q3_base_predictable_event event;
  CHECK(qa_q3_entity_pool_add_predictable_event(&pool, player, 23, 7, &event));
  CHECK(event.sequence == 3 && event.event == 23 && event.parameter == 7);
  CHECK(prints == 1);
  CHECK(strcmp(printed, " game event svt    12 ->     3: num =       EV_FIRE_WEAPON parm 7\n") == 0);

  CHECK(ctx_errors == 0);

  qa_q3_entity_pool_destroy(&pool);
  qa_q3_base_entity_records_close(records);
  qa_q3_base_entity_records_free(records);
  qa_shared_inventory_table_destroy(inventory);
  qa_gameplay_authority_destroy(combat);
  qa_actor_callback_table_destroy(callbacks);
  qa_shared_body_table_destroy(bodies);
  qa_session_actor_registry_destroy(actors);
  qa_identity_owner_destroy(&identity);

  printf("test_q3_unified: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
