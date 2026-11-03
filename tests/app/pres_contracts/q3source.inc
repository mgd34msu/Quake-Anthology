/* pres_contracts part: Q3SourcePresentationState (simulation/q3/presentation.ts)
 * over a real EntityPool (Q3EntityRecords + body table) and config strings:
 * q3PoolPresentationState copies the source entity, player and configstring
 * state, and q3PoolModels selects the model presentations. Expected values are
 * the TS output of build/agents/pres-contracts/tmp/q3source_oracle.ts under bun
 * (the same functions over the same pool contents):
 *   entities [[0,1,{1,2,3},true,0,0],[64,2,{10,20,30},true,0,0],[65,3,0,true,0,0],
 *             [66,0,0,true,0,0],[67,0,0,true,0,0],[68,0,0,true,1,3],[69,0,0,false,0,0]]
 *   clients [[0,26]]; configstrings 0, 35, 1000
 *   models 64 shard.md3 + shard_sphere.md3 at {10,20,30}; 65 rocket.md3 frame 2 at {10,0,0};
 *          66 configstring 35 model, effects 64, angles {0,90,0}; 67 "*4".
 * Included by tests/app/test_pres_contracts.c. */
#include "qa/app/bootstrap/simulation/q3/presentation.h"
#include "qa/content/q3/base/game/entities.h"
#include "qa/content/q3/base/records.h"
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/content/q3/base/shared/entity_shared.h"
#include "qa/content/q3/base/shared/trajectory.h"
#include "qa/world/actors/body.h"
#include "qa/world/actors/callbacks.h"
#include "qa/world/actors/registry.h"
#include "qa/world/gameplay/authority.h"
#include "qa/world/gameplay/inventory.h"

static bool q3s_vec(qa_vec3 v, double x, double y, double z) { return v.x == x && v.y == y && v.z == z; }
static qa_bounds q3s_abs(void *ctx, qa_owned_actor a, qa_body_state s) { CHECK_SENTINEL(ctx); return qa_translated_body_bounds(a, s); }
static void q3s_on_link(void *ctx, qa_linked_body b) { (void)b; CHECK_SENTINEL(ctx); }
static void q3s_on_unlink(void *ctx, qa_actor_id a) { (void)a; CHECK_SENTINEL(ctx); }
static void q3s_impulse(void *ctx, qa_owned_actor a, qa_vec3 i, qa_provider_id m) { (void)a; (void)i; (void)m; CHECK_SENTINEL(ctx); }
static void q3s_before(void *ctx, qa_owned_actor a, qa_damage_decision d) { (void)a; (void)d; CHECK_SENTINEL(ctx); }
static void q3s_confirmed(void *ctx, qa_damage_outcome o) { (void)o; CHECK_SENTINEL(ctx); }
static void q3s_ammo_stored(void *ctx, qa_actor_id a, double w, double v) { (void)a; (void)w; (void)v; CHECK_SENTINEL(ctx); }
static void q3s_schedule(void *ctx, qa_owned_actor a, bool has, double due) { (void)a; (void)has; (void)due; CHECK_SENTINEL(ctx); }
static void q3s_run_think(void *ctx, qa_owned_actor a, double t) { (void)a; (void)t; CHECK_SENTINEL(ctx); }
static bool q3s_damage_call(void *ctx, qa_q3_damage_call *out) { (void)out; CHECK_SENTINEL(ctx); return false; }
static qa_q3_game_entity *q3s_foreign(void *ctx, qa_actor_id a) { (void)a; CHECK_SENTINEL(ctx); return NULL; }
static bool q3s_is_player(void *ctx, qa_actor_id a) { (void)a; CHECK_SENTINEL(ctx); return false; }
static double q3s_time(void *ctx) { CHECK_SENTINEL(ctx); return 1000; }
static void q3s_print(void *ctx, const char *m) { (void)m; CHECK_SENTINEL(ctx); }
static void q3s_link(void *ctx, qa_q3_game_entity *e) { (void)e; CHECK_SENTINEL(ctx); }
static void q3s_unlink(void *ctx, qa_q3_game_entity *e) { (void)e; CHECK_SENTINEL(ctx); }
static const char *q3s_strings[1024];
static const char *q3s_string_get(void *ctx, double index) {
  CHECK_SENTINEL(ctx);
  const char *value = index >= 0 && index < 1024 ? q3s_strings[(size_t)index] : NULL;
  return value ? value : "";
}
static void q3s_string_set(void *ctx, double index, const char *value) {
  CHECK_SENTINEL(ctx);
  if (index >= 0 && index < 1024) q3s_strings[(size_t)index] = value;
}

static void pres_q3source_tests(void) {
  qa_identity_owner identity;
  if (!qa_identity_owner_create(&identity, "pres-q3source")) abort();
  qa_session_actor_registry *actors = qa_session_actor_registry_create(&identity, 2048);
  qa_body_link_hooks hooks; memset(&hooks, 0, sizeof hooks);
  hooks.ctx = PRES_SENTINEL; hooks.absolute_bounds = q3s_abs; hooks.on_link = q3s_on_link; hooks.on_unlink = q3s_on_unlink;
  qa_shared_body_table *bodies = qa_shared_body_table_create(actors, hooks);
  qa_actor_callback_table *callbacks = qa_actor_callback_table_create(actors);
  qa_gameplay_hooks g; memset(&g, 0, sizeof g);
  g.ctx = PRES_SENTINEL; g.impulse = q3s_impulse; g.before_reaction = q3s_before; g.confirmed = q3s_confirmed;
  qa_gameplay_authority *combat = qa_gameplay_authority_create(actors, callbacks, g);
  qa_shared_inventory_table *inventory = qa_shared_inventory_table_create(actors);
  if (!actors || !bodies || !callbacks || !combat || !inventory) abort();
  qa_q3_base_record_host rh; memset(&rh, 0, sizeof rh);
  rh.actors = actors; rh.bodies = bodies; rh.combat = combat; rh.inventory = inventory; rh.callbacks = callbacks;
  rh.ctx = PRES_SENTINEL; rh.ammo_timer_stored = q3s_ammo_stored; rh.schedule = q3s_schedule;
  rh.run_think = q3s_run_think; rh.damage_call = q3s_damage_call; rh.foreign = q3s_foreign; rh.is_player = q3s_is_player;
  qa_q3_base_entity_records *records = qa_q3_base_entity_records_create(rh, "q3:pres", QA_Q3_BASE_PRODUCT_BASEQ3);
  if (!records) abort();
  qa_q3_entity_pool pool;
  qa_q3_entity_pool_options po; memset(&po, 0, sizeof po);
  po.records = qa_q3_base_entity_records_pool_table(records);
  po.product = QA_Q3_BASE_PRODUCT_BASEQ3; po.max_clients = 4; po.map_start_time = 0;
  po.ctx = PRES_SENTINEL; po.time = q3s_time; po.print = q3s_print; po.link = q3s_link; po.unlink = q3s_unlink;
  qa_q3_entity_pool_init(&pool, po);
  memset(q3s_strings, 0, sizeof q3s_strings);
  qa_q3_config_string_store strings = {PRES_SENTINEL, q3s_string_get, q3s_string_set};
  q3s_string_set(PRES_SENTINEL, 0, "\\sv_hostname\\pres");
  q3s_string_set(PRES_SENTINEL, 35, "models/mapobjects/pres.md3");
  q3s_string_set(PRES_SENTINEL, 1000, "last");

  /* Slot 0: a client; 64..69: item, missile, configstring model, inline model,
   * NOCLIENT entity, unlinked entity. */
  qa_q3_game_entity *player = qa_q3_entity_pool_activate_client(&pool, 0);
  player->s.e_type = QA_Q3_BASE_ET_PLAYER;
  qa_q3_set_origin(player, (qa_vec3){1, 2, 3});
  player->client->ps.viewheight = 26;
  qa_q3_game_entity *spawned[6];
  for (int i = 0; i < 6; i++) { spawned[i] = qa_q3_entity_pool_spawn(&pool); CHECK(spawned[i]->s.number == 64 + i); }
  qa_q3_game_entity *item = spawned[0], *missile = spawned[1], *general = spawned[2], *brush = spawned[3];
  item->s.e_type = QA_Q3_BASE_ET_ITEM; item->s.modelindex = 1; qa_q3_set_origin(item, (qa_vec3){10, 20, 30});
  missile->s.e_type = QA_Q3_BASE_ET_MISSILE; missile->s.weapon = 5; missile->s.frame = 2;
  missile->s.pos.type = QA_Q3_BASE_TR_LINEAR; missile->s.pos.time = 900; missile->s.pos.delta = (qa_vec3){100, 0, 0};
  general->s.modelindex = 3; general->s.e_flags = 0x40; general->s.apos.base = (qa_vec3){0, 90, 0};
  brush->r.model.kind = QA_Q3_BASE_MODEL_INLINE; brush->r.model.index = 4;
  spawned[4]->s.modelindex = 3; spawned[4]->r.sv_flags = QA_Q3_BASE_SVF_NOCLIENT; spawned[4]->r.single_client = 3;
  spawned[5]->s.modelindex = 3;
  qa_shared_body_table_link(bodies, qa_q3_game_entity_actor(player), NULL);
  for (int i = 0; i < 5; i++) qa_shared_body_table_link(bodies, qa_q3_game_entity_actor(spawned[i]), NULL);

  qa_app_q3_source_presentation_state *state = qa_app_q3_pool_presentation_state(&pool, QA_Q3_BASE_PRODUCT_BASEQ3, 1000, strings);
  CHECK(state != NULL);
  if (state != NULL) {
    CHECK(state->product == QA_Q3_BASE_PRODUCT_BASEQ3 && state->time == 1000);
    static const double numbers[7] = {0, 64, 65, 66, 67, 68, 69}, types[7] = {1, 2, 3, 0, 0, 0, 0};
    static const bool linked[7] = {true, true, true, true, true, true, false};
    CHECK(state->entity_count == 7);
    for (size_t i = 0; i < state->entity_count && i < 7; i++) {
      CHECK(state->entities[i].state.number == numbers[i] && state->entities[i].state.e_type == types[i]);
      CHECK(state->entities[i].linked == linked[i]);
      CHECK(state->entities[i].server_flags == (i == 5 ? 1 : 0) && state->entities[i].single_client == (i == 5 ? 3 : 0));
      CHECK(qa_same_actor(state->entities[i].actor, qa_q3_game_entity_actor(qa_q3_entity_pool_at(&pool, numbers[i])).id));
    }
    if (state->entity_count == 7) {
      CHECK(q3s_vec(state->entities[0].origin, 1, 2, 3) && q3s_vec(state->entities[1].origin, 10, 20, 30));
      CHECK(q3s_vec(state->entities[2].origin, 0, 0, 0));
    }
    CHECK(state->client_count == 1 && state->clients[0].slot == 0 && state->clients[0].state.viewheight == 26);
    CHECK(state->configstring_count == 3);
    if (state->configstring_count == 3) {
      CHECK(state->configstrings[0].index == 0 && strcmp(state->configstrings[0].value, "\\sv_hostname\\pres") == 0);
      CHECK(state->configstrings[1].index == 35 && strcmp(state->configstrings[1].value, "models/mapobjects/pres.md3") == 0);
      CHECK(state->configstrings[2].index == 1000 && strcmp(state->configstrings[2].value, "last") == 0);
    }
    /* The state holds copies: later source changes do not reach it. */
    item->s.e_type = QA_Q3_BASE_ET_GENERAL; player->client->ps.viewheight = 8;
    q3s_string_set(PRES_SENTINEL, 35, "models/changed.md3");
    CHECK(state->entities[1].state.e_type == QA_Q3_BASE_ET_ITEM && state->clients[0].state.viewheight == 26);
    CHECK(strcmp(state->configstrings[1].value, "models/mapobjects/pres.md3") == 0);
    item->s.e_type = QA_Q3_BASE_ET_ITEM; player->client->ps.viewheight = 26;
    q3s_string_set(PRES_SENTINEL, 35, "models/mapobjects/pres.md3");
    qa_app_q3_source_presentation_state_destroy(state);
  }

  qa_app_simulation_presentation models[8];
  size_t count = qa_app_q3_pool_models(&pool, QA_Q3_BASE_PRODUCT_BASEQ3, 1000, "q3:pres", strings, models, 8);
  CHECK(count == 5);
  static const double slots[5] = {64, 64, 65, 66, 67};
  static const char *const paths[5] = {"models/powerups/armor/shard.md3", "models/powerups/armor/shard_sphere.md3",
      "models/ammo/rocket/rocket.md3", "models/mapobjects/pres.md3", "*4"};
  for (size_t i = 0; i < count && i < 5; i++) {
    CHECK(qa_same_actor(models[i].actor, qa_q3_game_entity_actor(qa_q3_entity_pool_at(&pool, slots[i])).id));
    CHECK_STR(models[i].path, paths[i]);
    CHECK_STR(models[i].content, "q3:pres");
    CHECK_STR(models[i].render_owner, "source-client");
    CHECK(models[i].family == QA_GAME_FAMILY_Q3 && models[i].visible && !models[i].view_weapon && models[i].scale == 1);
    CHECK(models[i].skin == 0 && models[i].render_flags == 0 && models[i].frame == models[i].old_frame);
  }
  if (count == 5) {
    CHECK(q3s_vec(models[0].origin, 10, 20, 30) && q3s_vec(models[1].origin, 10, 20, 30));
    CHECK(models[2].frame == 2 && q3s_vec(models[2].origin, 10, 0, 0));
    CHECK(models[3].effects == 64 && q3s_vec(models[3].angles, 0, 90, 0));
    CHECK(q3s_vec(models[4].origin, 0, 0, 0));
  }

  qa_q3_entity_pool_destroy(&pool);
  qa_q3_base_entity_records_close(records);
  qa_q3_base_entity_records_free(records);
  qa_shared_inventory_table_destroy(inventory);
  qa_gameplay_authority_destroy(combat);
  qa_actor_callback_table_destroy(callbacks);
  qa_shared_body_table_destroy(bodies);
  qa_session_actor_registry_destroy(actors);
  qa_identity_owner_destroy(&identity);
}
