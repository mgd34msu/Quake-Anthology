/* Checks for src/app/bootstrap/simulation/network_q3.c (TS network-q3.ts
 * createQ3ApplicationServerHost) over a native Q3SourceRuntime built from the
 * subsystems owner. Expected info strings were produced by the TS setInfoValue
 * under bun. Every callback checks its sentinel ctx. GPL-2.0-or-later. */
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/simulation/network_q3.h"
#include "qa/app/bootstrap/simulation/q3/runtime.h"
#include "qa/app/bootstrap/simulation/q3/server_state.h"
#include "qa/content/q3/base/world_adapter.h"
#include "qa/core/cvars/index.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/network/q3/admission.h"
#include "qa/network/q3/server.h"
#include "qa/network/q3/server_message.h"
#include "qa/network/q3/pure.h"
#include "qa/world/session/session.h"

static int checks, failures;
#define CHECK(...) do { checks++; if (!(__VA_ARGS__)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #__VA_ARGS__); } } while (0)

static int sim_sentinel, content_sentinel, host_sentinel, print_sentinel;
static int ctx_errors;
#define EXPECT_CTX(ctx, sentinel) do { if ((ctx) != &(sentinel)) ctx_errors++; } while (0)

/* ---- Q1 empty world for the scene queries (only SVF_BROADCAST entities are visible). ---- */
static qa_q1_leaf leaves[2];
static qa_q1_world_model models[1];
static double headnodes[4] = {-2, -1, -1, -1};
static qa_q1_world_geometry geometry;
static qa_bounds box(double a, double b, double c, double d, double e, double f) { qa_bounds out = {{a, b, c}, {d, e, f}}; return out; }
static void build_empty_world(void) {
  qa_bounds b = box(-256, -256, -256, 256, 256, 256);
  leaves[0] = (qa_q1_leaf){-2, b, {0, 0}, false, 0, {0, 0, 0, 0}};
  leaves[1] = (qa_q1_leaf){-1, b, {0, 0}, false, 0, {0, 0, 0, 0}};
  memset(&models, 0, sizeof models);
  models[0].bounds = b; models[0].headnodes = (qa_double_view){headnodes, 4}; models[0].visible_leaves = 1;
  memset(&geometry, 0, sizeof geometry);
  geometry.format = "bsp29"; geometry.entities = "";
  geometry.leaves = leaves; geometry.leaf_count = 2;
  geometry.models = models; geometry.model_count = 1;
}

/* ---- host leaves (sentinel host ctx). ---- */
#define PART(ctx) do { if ((ctx) != &host_sentinel) ctx_errors++; } while (0)
static qa_bounds p_bounds(void *ctx, qa_owned_actor actor, qa_body_state state) {
  (void)ctx; (void)actor;
  return (qa_bounds){{state.origin.x - 8, state.origin.y - 8, state.origin.z - 8}, {state.origin.x + 8, state.origin.y + 8, state.origin.z + 8}};
}
static void p_link_hook(void *ctx, qa_linked_body body) { (void)ctx; (void)body; }
static void p_unlink_hook(void *ctx, qa_actor_id actor) { (void)ctx; (void)actor; }
static void p_schedule(void *ctx, qa_owned_actor a, bool has_due, double due) { PART(ctx); (void)a; (void)has_due; (void)due; }
static void p_run_think(void *ctx, qa_owned_actor a, double time) { PART(ctx); (void)a; (void)time; }
static qa_q3_game_entity *p_foreign(void *ctx, qa_actor_id a) { PART(ctx); (void)a; return NULL; }
static bool p_is_player(void *ctx, qa_actor_id a) { PART(ctx); (void)a; return false; }
static void p_collision(void *ctx, qa_owned_actor a, qa_actor_collision c) { PART(ctx); (void)a; (void)c; }

/* ---- Q3 source host (sentinel host ctx): configstrings live in the server state. ---- */
static qa_app_q3_server_state *server_state;
static double host_now_value = 4321;
static int userinfo_reads;
static const char *host_cs_get(void *ctx, double index) { EXPECT_CTX(ctx, host_sentinel); return qa_app_q3_server_state_configstring_get(server_state, index); }
static void host_cs_set(void *ctx, double index, const char *value) { EXPECT_CTX(ctx, host_sentinel); qa_app_q3_server_state_configstring_set(server_state, index, value); }
static void host_print(void *ctx, const char *text) { EXPECT_CTX(ctx, host_sentinel); (void)text; }
static void host_send(void *ctx, double client, const char *text) { EXPECT_CTX(ctx, host_sentinel); (void)client; (void)text; }
static void host_drop(void *ctx, double client, const char *reason) { EXPECT_CTX(ctx, host_sentinel); (void)client; (void)reason; }
static char *host_get_userinfo(void *ctx, double client) {
  EXPECT_CTX(ctx, host_sentinel);
  userinfo_reads++;
  const char *info = qa_app_q3_server_state_get_userinfo(server_state, client);
  return strdup(info ? info : "");
}
static void host_set_userinfo(void *ctx, double client, const char *value) { EXPECT_CTX(ctx, host_sentinel); qa_app_q3_server_state_set_userinfo(server_state, client, value); }
static void host_append(void *ctx, const char *text) { EXPECT_CTX(ctx, host_sentinel); (void)text; }
static double host_now(void *ctx) { EXPECT_CTX(ctx, host_sentinel); return host_now_value; }
static void state_print(void *ctx, const char *text) { EXPECT_CTX(ctx, host_sentinel); (void)text; }

/* ---- the SharedSimulation access struct. ---- */
typedef enum admit_mode { ADMIT_OK, ADMIT_DENY, ADMIT_FAIL } admit_mode;
static struct {
  qa_app_q3_source_runtime *runtime, *next;
  qa_app_sim_movement_player movement[4];
  qa_actor_id players[4]; size_t player_count;
  admit_mode admit; bool cleanup_fails;
  int admits, disconnects, notifies, observes;
  qa_actor_id last_disconnect; char last_notify[32];
  double time;
  bool reset_plan;
  qa_shared_scene_queries *scene;
  qa_shared_body_table *bodies;
} sim;
static qa_app_q3_source_runtime *s_q3_source(void *ctx) { EXPECT_CTX(ctx, sim_sentinel); return sim.runtime; }
static qa_app_q3_qvm_server_game *s_q3_guest(void *ctx) { EXPECT_CTX(ctx, sim_sentinel); return NULL; }
static const qa_actor_id *s_players(void *ctx, size_t *out_count) { EXPECT_CTX(ctx, sim_sentinel); *out_count = sim.player_count; return sim.players; }
static qa_app_sim_movement_player *s_movement_player(void *ctx, qa_actor_id actor) {
  EXPECT_CTX(ctx, sim_sentinel);
  for (size_t i = 0; i < 4; i++) if (qa_same_actor(sim.movement[i].actor.id, actor)) return &sim.movement[i];
  return NULL;
}
static qa_app_sim_player_admission s_admit_player(void *ctx, qa_client_id client) {
  EXPECT_CTX(ctx, sim_sentinel);
  sim.admits++;
  /* admitPlayer registers the player before the source can deny it. */
  sim.players[sim.player_count++] = sim.movement[client.slot].actor.id;
  if (sim.admit == ADMIT_DENY) qa_raise(QA_APP_Q3_CLIENT_ADMISSION_DENIED, 0, 0, "Invalid password");
  if (sim.admit == ADMIT_FAIL) qa_raise("Error", 0, 0, "unexpected connect");
  qa_app_sim_player_admission admission = {sim.movement[client.slot].actor.id, 0};
  return admission;
}
static void s_disconnect_player(void *ctx, qa_actor_id actor) {
  EXPECT_CTX(ctx, sim_sentinel);
  sim.disconnects++; sim.last_disconnect = actor;
  if (sim.cleanup_fails) qa_raise("Error", 0, 0, "cleanup failed");
  size_t kept = 0;
  for (size_t i = 0; i < sim.player_count; i++) if (!qa_same_actor(sim.players[i], actor)) sim.players[kept++] = sim.players[i];
  sim.player_count = kept;
}
static void s_notify(void *ctx, const char *kind, qa_actor_id actor) {
  EXPECT_CTX(ctx, sim_sentinel); (void)actor;
  sim.notifies++; snprintf(sim.last_notify, sizeof sim.last_notify, "%s", kind);
}
static void s_observe(void *ctx, const qa_actor_command *input) { EXPECT_CTX(ctx, sim_sentinel); (void)input; sim.observes++; }
static double s_time(void *ctx) { EXPECT_CTX(ctx, sim_sentinel); return sim.time; }
static qa_app_sim_network_restart_plan s_plan(void *ctx) {
  EXPECT_CTX(ctx, sim_sentinel);
  qa_app_sim_network_restart_plan plan = {sim.reset_plan, sim.reset_plan ? NULL : "map changed"};
  return plan;
}
/* ---- content access. ---- */
static int resolves, collects;
static const qa_app_q3_package_mount *c_mounts(void *ctx, size_t *out_count) { EXPECT_CTX(ctx, content_sentinel); *out_count = 0; return NULL; }
static void c_collect(void *ctx, void (*visit)(void *, const qa_resolved_resource_reference *, bool), void *visit_ctx) {
  EXPECT_CTX(ctx, content_sentinel); (void)visit; (void)visit_ctx; collects++;
}
static bool c_resolve(void *ctx, const char *path, qa_resolved_resource_reference *out) {
  EXPECT_CTX(ctx, content_sentinel); (void)out;
  if (strcmp(path, "vm/cgame.qvm") == 0) resolves++;
  return false;
}
static char printed[256];
static void print_sink(void *ctx, const char *text) { EXPECT_CTX(ctx, print_sentinel); snprintf(printed, sizeof printed, "%s", text); }

static void set_cvar(qa_cvar_registry *cvars, const char *name, const char *value) {
  qa_cvar_snapshot snapshot;
  memset(&snapshot, 0, sizeof snapshot);
  qa_cvar_registry_set(cvars, name, value, true, &snapshot);
  qa_cvar_read_free(&snapshot);
}
static char *cvar(qa_cvar_registry *cvars, const char *name) { return qa_cvar_registry_variable_string(cvars, name); }

typedef struct call { qa_app_sim_q3_server_authority *authority; qa_app_sim_q3_server_binding_options options;
  qa_client_id client; const char *userinfo; qa_app_q3_admission result; qa_q3_wire_user_command command; } call;
static void call_create(void *ctx) { call *c = ctx; *c->authority = qa_app_sim_create_q3_application_server_host(c->options); }
static void call_connect(void *ctx) { call *c = ctx; c->result = c->authority->connect(c->authority->host.ctx, c->client, c->userinfo); }
static void call_preflight(void *ctx) { call *c = ctx; c->authority->host.source_round.preflight(c->authority->host.source_round.ctx); }
static void call_rebind(void *ctx) { call *c = ctx; c->authority->host.source_round.rebind(c->authority->host.source_round.ctx); }
static void call_reconnect(void *ctx) {
  call *c = ctx;
  c->result = c->authority->host.source_round.reconnect(c->authority->host.source_round.ctx, c->client, c->userinfo, &c->command);
}
static void call_game_state(void *ctx) { call *c = ctx; (void)c->authority->host.game_state(c->authority->host.ctx, c->result.player, 9); }

static void test_native_host(void) {
  qa_identity_owner identity;
  CHECK(qa_identity_owner_create(&identity, "network-q3"));
  qa_session_actor_registry *actors = qa_session_actor_registry_create(&identity, 2048);
  qa_body_link_hooks hooks;
  memset(&hooks, 0, sizeof hooks);
  hooks.absolute_bounds = p_bounds; hooks.on_link = p_link_hook; hooks.on_unlink = p_unlink_hook;
  qa_shared_body_table *bodies = qa_shared_body_table_create(actors, hooks);
  qa_actor_callback_table *callbacks = qa_actor_callback_table_create(actors);
  qa_gameplay_hooks gameplay_hooks;
  memset(&gameplay_hooks, 0, sizeof gameplay_hooks);
  qa_gameplay_authority *combat = qa_gameplay_authority_create(actors, callbacks, gameplay_hooks);
  qa_shared_inventory_table *inventory = qa_shared_inventory_table_create(actors);
  build_empty_world();
  qa_decoded_world world;
  memset(&world, 0, sizeof world);
  world.kind = QA_DECODED_WORLD_Q1; world.as.q1 = &geometry;
  qa_shared_scene_queries *scene = qa_create_scene_queries(world);
  CHECK(actors && bodies && callbacks && combat && inventory && scene);
  sim.scene = scene; sim.bodies = bodies;

  qa_app_q3_server_state_options state_options;
  memset(&state_options, 0, sizeof state_options);
  state_options.session.name = "test"; state_options.session.token = "test";
  state_options.settings.game_type = 0; state_options.settings.max_clients = 8; state_options.settings.map_name = "q3dm17";
  state_options.ctx = &host_sentinel; state_options.print = state_print; state_options.now = host_now;
  server_state = qa_app_q3_server_state_create(state_options);
  CHECK(server_state != NULL);
  qa_cvar_registry *cvars = qa_app_q3_server_state_cvars(server_state);

  /* new Q3SourceRuntime(options, host): the runtime builds its subsystems from the host. */
  qa_app_q3_source_host host;
  memset(&host, 0, sizeof host);
  host.ctx = &host_sentinel;
  host.server_state = server_state;
  host.engine = (qa_app_q3_source_engine){&host_sentinel, host_print, host_print, host_send, host_drop, host_get_userinfo,
                                          host_set_userinfo, NULL, host_append, NULL};
  host.cvars = cvars;
  host.configstrings_ctx = &host_sentinel; host.configstring_get = host_cs_get; host.configstring_set = host_cs_set;
  host.now = host_now;
  host.actors = actors; host.bodies = bodies; host.combat = combat; host.inventory = inventory; host.callbacks = callbacks;
  host.scene = scene;
  host.schedule = p_schedule; host.run_think = p_run_think; host.foreign = p_foreign; host.is_player = p_is_player;
  host.collision = p_collision;
  host.bots.kind = "unavailable"; host.bots.reason = "no bots";
  qa_app_q3_source_options source_options;
  memset(&source_options, 0, sizeof source_options);
  source_options.recipe.map.entities.provider = "q3:test";
  source_options.product = "baseq3"; source_options.max_clients = 8; source_options.seed = 1; source_options.build_date = "Nov  2 2026";
  source_options.entities = "{ \"classname\" \"worldspawn\" }\n";
  qa_app_q3_source_runtime_mode mode = {false, NULL};
  sim.runtime = qa_app_q3_source_runtime_new(source_options, host, mode);
  qa_app_q3_source_subsystems_owner *owner = qa_app_q3_source_runtime_owner(sim.runtime);
  qa_app_q3_source_subsystems table = qa_app_q3_source_subsystems_owner_table(owner);
  CHECK(table.records != NULL && table.admission != NULL);
  CHECK(sim.runtime != NULL);
  void *tc = table.ctx;
  table.memory_initialize(tc);
  table.pool_initialize_clients(tc, 8);
  table.records_activate(tc, 1022);
  table.session_initialize_world(tc);
  qa_app_q3_spawn_report report = table.spawn_entities(tc);
  qa_app_q3_spawn_report_destroy(&report);

  /* Players 0..2 with movement records; 0 and 1 have source entities. */
  for (int i = 0; i < 3; i++) {
    sim.movement[i].actor = qa_session_actor_registry_allocate(actors, "q3:test", "q3:player");
    sim.movement[i].client = (qa_client_id){0};
    sim.movement[i].client.session = qa_session_actor_registry_session(actors);
    sim.movement[i].client.slot = i; sim.movement[i].client.generation = 1;
    /* SharedSimulation.admitPlayer creates the body and combat state before the source attaches. */
    qa_body_state body;
    memset(&body, 0, sizeof body);
    body.bounds = box(-15, -15, -24, 15, 15, 32);
    qa_shared_body_table_create_body(bodies, sim.movement[i].actor, body);
    qa_combat_state combat_state;
    memset(&combat_state, 0, sizeof combat_state);
    combat_state.health = 100; combat_state.mass = 200;
    combat_state.armor.regular.kind = QA_REGULAR_ARMOR_NONE; combat_state.armor.powered.kind = "none";
    qa_gameplay_authority_create_state(combat, sim.movement[i].actor, combat_state, NULL, NULL);
  }
  qa_q3_entity_pool *pool = qa_app_q3_source_runtime_pool(sim.runtime);
  CHECK(pool == qa_app_q3_source_subsystems_owner_pool(owner));
  table.records_attach(tc, 0, sim.movement[0].actor);
  table.records_attach(tc, 1, sim.movement[1].actor);
  sim.players[0] = sim.movement[0].actor.id; sim.players[1] = sim.movement[1].actor.id; sim.player_count = 2;
  qa_q3_game_entity *e0 = qa_app_q3_source_runtime_record_by_actor(sim.runtime, sim.movement[0].actor.id);
  qa_q3_game_entity *e1 = qa_app_q3_source_runtime_record_by_actor(sim.runtime, sim.movement[1].actor.id);
  CHECK(e0 != NULL && e1 != NULL && e0->slot == 0 && e1->slot == 1 && e0->client && e1->client);
  CHECK(qa_app_q3_source_runtime_record_by_actor(sim.runtime, sim.movement[2].actor.id) == NULL);

  qa_executable_recipe recipe;
  memset(&recipe, 0, sizeof recipe);
  recipe.movement.provider = "q3:movement"; recipe.character.definition.provider = "q1:character";
  qa_app_simulation_options sim_options;
  memset(&sim_options, 0, sizeof sim_options);
  sim_options.max_clients = 8;
  qa_app_sim_network_simulation s;
  memset(&s, 0, sizeof s);
  s.ctx = &sim_sentinel; s.recipe = &recipe; s.options = &sim_options; s.actors = actors; s.bodies = bodies; s.scene = scene;
  s.q3_source = s_q3_source; s.q3_guest = s_q3_guest; s.players = s_players; s.movement_player = s_movement_player;
  s.admit_player = s_admit_player; s.disconnect_player = s_disconnect_player; s.notify_client_event = s_notify;
  s.observe_client_command = s_observe; s.time_seconds = s_time; s.source_restart_plan = s_plan;
  qa_app_sim_network_content content;
  memset(&content, 0, sizeof content);
  content.ctx = &content_sentinel; content.recipe = &recipe;
  content.q3_package_mounts = c_mounts; content.q3_collect_opened = c_collect; content.resolve_path = c_resolve;
  qa_session_mode session_mode = {QA_SESSION_MODE_HEADLESS};
  qa_engine_session *session = qa_engine_session_create(&identity, session_mode);
  CHECK(session != NULL);

  qa_app_sim_q3_server_binding_options options;
  memset(&options, 0, sizeof options);
  options.session = session; options.simulation = &s; options.content = &content;
  options.print = print_sink; options.print_ctx = &print_sentinel;

  /* Restore mode needs a saved QVM source; no provider at all is an Error. */
  qa_app_sim_q3_server_authority authority;
  memset(&authority, 0, sizeof authority);
  call c;
  memset(&c, 0, sizeof c);
  c.authority = &authority; c.options = options; c.options.mode = "restore";
  qa_caught_error caught;
  CHECK(!qa_try(call_create, &c, &caught) && strcmp(caught.name, "Error") == 0
        && strcmp(caught.message, "Restored Q3 client authority requires a saved QVM source") == 0);
  qa_app_q3_source_runtime *saved_runtime = sim.runtime;
  sim.runtime = NULL; c.options.mode = NULL;
  CHECK(!qa_try(call_create, &c, &caught) && strcmp(caught.message, "Q3 network requires a Q3 game provider") == 0);
  sim.runtime = saved_runtime;

  authority = qa_app_sim_create_q3_application_server_host(options);
  qa_app_q3_server_host *h = &authority.host;
  /* cvars.register('fs_game', product === 'missionpack' ? ... : '', SystemInfo). */
  qa_cvar_read read;
  CHECK(qa_cvar_registry_find(cvars, "fs_game", &read) && strcmp(read.value, "") == 0 && (read.flags & QA_CVAR_FLAG_SYSTEM_INFO) != 0);
  qa_cvar_read_free(&read);
  CHECK(*(const qa_q3_product *)h->product == QA_Q3_PRODUCT_BASEQ3 && h->max_clients == 8);
  CHECK(h->has_source_round && h->begin == NULL && h->has_admission && h->administration == NULL);

  /* Admission settings read the server cvars live. */
  set_cvar(cvars, "sv_privateClients", "2");
  set_cvar(cvars, "sv_privatePassword", "secret");
  set_cvar(cvars, "sv_reconnectlimit", "5");
  set_cvar(cvars, "sv_minPing", "20");
  set_cvar(cvars, "sv_maxPing", "300");
  set_cvar(cvars, "sv_strictAuth", "0");
  set_cvar(cvars, "sv_floodProtect", "0");
  const qa_app_q3_admission_settings *a = &h->admission;
  CHECK(a->private_clients(a->ctx) == 2 && strcmp(a->private_password(a->ctx), "secret") == 0);
  CHECK(a->reconnect_limit_seconds(a->ctx) == 5 && a->minimum_ping(a->ctx) == 20 && a->maximum_ping(a->ctx) == 300);
  CHECK(!a->demo_restricted(a->ctx) && a->enabled(a->ctx) && strcmp(a->game_directory(a->ctx), "") == 0);
  CHECK(strcmp(a->strict_auth(a->ctx), "0") == 0 && !a->flood_protect(a->ctx));
  set_cvar(cvars, "g_gametype", "2");
  CHECK(!a->enabled(a->ctx));
  set_cvar(cvars, "g_gametype", "0");
  set_cvar(cvars, "sv_privateClients", "0");

  /* connect: an accepted native player carries the source entity slot. */
  c.client = sim.movement[0].client; c.userinfo = "\\name\\Zero\\rate\\25000\\snaps\\10\\ip\\localhost";
  sim.player_count = 0; sim.admit = ADMIT_OK;
  CHECK(qa_try(call_connect, &c, &caught));
  CHECK(c.result.accepted && c.result.player.source_entity == 0 && qa_same_actor(c.result.player.actor, sim.movement[0].actor.id));
  CHECK(strcmp(qa_app_q3_server_state_get_userinfo(server_state, 0), c.userinfo) == 0);
  /* A source denial is a rejection after the admitted actor is disconnected. */
  sim.player_count = 1; sim.admit = ADMIT_DENY; int disconnects = sim.disconnects;
  c.client = sim.movement[2].client; c.userinfo = "\\name\\Two";
  CHECK(qa_try(call_connect, &c, &caught));
  CHECK(!c.result.accepted && strcmp(c.result.reason, "Invalid password") == 0);
  CHECK(sim.disconnects == disconnects + 1 && qa_same_actor(sim.last_disconnect, sim.movement[2].actor.id) && sim.player_count == 1);
  /* Other failures rethrow; a failing cleanup aggregates. */
  sim.admit = ADMIT_FAIL;
  CHECK(!qa_try(call_connect, &c, &caught) && strcmp(caught.name, "Error") == 0 && strcmp(caught.message, "unexpected connect") == 0);
  sim.admit = ADMIT_DENY; sim.cleanup_fails = true;
  CHECK(!qa_try(call_connect, &c, &caught) && strcmp(caught.name, "AggregateError") == 0
        && strcmp(caught.message, "Q3 client admission and cleanup failed") == 0);
  sim.cleanup_fails = false; sim.admit = ADMIT_OK;
  sim.players[0] = sim.movement[0].actor.id; sim.players[1] = sim.movement[1].actor.id; sim.player_count = 2;
  qa_app_q3_player p0 = h->carried_player(h->ctx, sim.movement[0].client);
  qa_app_q3_player p1 = h->carried_player(h->ctx, sim.movement[1].client);
  CHECK(p0.source_entity == 0 && p1.source_entity == 1);

  /* Packages are required before pure/gameState; prepare publishes the info strings. */
  c.result.player = p0;
  CHECK(!qa_try(call_game_state, &c, &caught) && strcmp(caught.message, "Q3 package metadata has not been prepared") == 0);
  set_cvar(cvars, "sv_pure", "1");
  set_cvar(cvars, "sv_hostname", "Test Host");
  h->prepare(h->ctx, -5, 9, NULL, NULL);
  CHECK(resolves == 1 && collects == 1);
  h->prepare(h->ctx, -5, 9, NULL, NULL);
  CHECK(resolves == 1 && collects == 2); /* the cgame is touched once */
  char *serverid = cvar(cvars, "sv_serverid"), *paks = cvar(cvars, "sv_paks"), *referenced = cvar(cvars, "sv_referencedPaks");
  CHECK(strcmp(serverid, "9") == 0 && strcmp(paks, "") == 0 && strcmp(referenced, "") == 0);
  free(serverid); free(paks); free(referenced);
  char *systeminfo = qa_cvar_registry_info_string(cvars, QA_CVAR_FLAG_SYSTEM_INFO, 8192);
  char *serverinfo = qa_app_q3_server_state_server_info(server_state);
  CHECK(strcmp(qa_app_q3_server_state_configstring_get(server_state, 1), systeminfo) == 0);
  CHECK(strcmp(qa_app_q3_server_state_configstring_get(server_state, 0), serverinfo) == 0);
  CHECK(strstr(systeminfo, "\\sv_serverid\\9") != NULL);
  free(systeminfo); free(serverinfo);
  const qa_q3_pure_server *pure = h->pure(h->ctx, 9, NULL);
  CHECK(pure->enabled && pure->checksum_feed == -5 && pure->checksum_feed_server_id == 9);
  CHECK(!pure->has_cgame_checksum && !pure->has_ui_checksum && pure->loaded_count == 0);
  double restarted = 11;
  CHECK(((const qa_q3_pure_server *)h->pure(h->ctx, 9, &restarted))->checksum_feed_server_id == 11);
  set_cvar(cvars, "sv_pure", "0");
  CHECK(h->open_download(h->ctx, "baseq3/pak0.pk3") == NULL);
  set_cvar(cvars, "sv_allowDownload", "1");
  CHECK(h->downloads_enabled(h->ctx));

  /* A linked broadcast entity becomes a baseline and a snapshot entity. */
  qa_q3_game_entity *beacon = qa_q3_entity_pool_spawn(pool);
  CHECK(beacon != NULL);
  beacon->s.e_type = 7; beacon->s.modelindex = 3; beacon->s.time2 = 1.5; /* number storage */
  beacon->r.sv_flags = QA_Q3_BASE_SVF_BROADCAST;
  qa_q3_base_entity_shared_set_current_origin(&beacon->r, (qa_vec3){32, 0, 0});
  qa_q3_base_entity_shared_set_mins(&beacon->r, (qa_vec3){-4, -4, -4});
  qa_q3_base_entity_shared_set_maxs(&beacon->r, (qa_vec3){4, 4, 4});
  beacon->r.model.kind = QA_Q3_BASE_MODEL_BOX;
  qa_q3_base_world_adapter_link(qa_app_q3_source_subsystems_owner_world(owner), beacon);
  qa_q3_game_entity *hidden = qa_q3_entity_pool_spawn(pool);
  hidden->r.sv_flags = QA_Q3_BASE_SVF_BROADCAST | QA_Q3_BASE_SVF_NOCLIENT;
  qa_q3_base_entity_shared_set_current_origin(&hidden->r, (qa_vec3){0, 32, 0});
  qa_q3_base_entity_shared_set_mins(&hidden->r, (qa_vec3){-4, -4, -4});
  qa_q3_base_entity_shared_set_maxs(&hidden->r, (qa_vec3){4, 4, 4});
  hidden->r.model.kind = QA_Q3_BASE_MODEL_BOX;
  qa_q3_base_world_adapter_link(qa_app_q3_source_subsystems_owner_world(owner), hidden);
  qa_q3_game_entity *unlinked = qa_q3_entity_pool_spawn(pool);
  unlinked->r.sv_flags = QA_Q3_BASE_SVF_BROADCAST;
  qa_app_q3_server_state_configstring_set(server_state, 5, "five");

  const qa_q3_gamestate *state = h->game_state(h->ctx, p0, 9);
  CHECK(state->client_number == 0 && state->checksum_feed == -5 && state->command_sequence == 0);
  CHECK(state->entry_count >= 3 && state->entries[0].kind == QA_Q3_GS_CONFIGSTRING && state->entries[0].index == 0);
  CHECK(state->entries[1].kind == QA_Q3_GS_CONFIGSTRING && state->entries[1].index == 1);
  bool five = false, beacon_baseline = false, hidden_baseline = false, unlinked_baseline = false;
  for (size_t i = 2; i < state->entry_count; i++) {
    const qa_q3_gamestate_entry *entry = &state->entries[i];
    if (entry->kind == QA_Q3_GS_CONFIGSTRING) {
      CHECK(entry->index != 0 && entry->index != 1);
      if (entry->index == 5) five = strcmp(entry->value, "five") == 0;
    } else {
      if (entry->number == beacon->slot) beacon_baseline = entry->entity.e_type == 7 && entry->entity.time2 == 1.5;
      if (entry->number == hidden->slot) hidden_baseline = true;
      if (entry->number == unlinked->slot) unlinked_baseline = true;
    }
  }
  CHECK(five && beacon_baseline && hidden_baseline && !unlinked_baseline);

  /* rate(): parseInt of the userinfo, clamped and against sv_fps. */
  set_cvar(cvars, "sv_fps", "20");
  set_cvar(cvars, "sv_maxRate", "8000");
  int reads = userinfo_reads;
  const qa_q3_server_rate *rate = h->rate(h->ctx, p0);
  CHECK(userinfo_reads == reads + 1);
  CHECK(rate->rate == 25000 && rate->max_rate == 8000 && rate->snapshot_msec == 100 && rate->local && !rate->lan && !rate->force_lan);
  qa_app_q3_server_state_set_userinfo(server_state, 1, "\\rate\\500000\\snaps\\x");
  rate = h->rate(h->ctx, p1);
  CHECK(rate->rate == 90000 && rate->snapshot_msec == 50 && !rate->local);
  qa_app_q3_server_state_set_userinfo(server_state, 1, "\\name\\One");
  rate = h->rate(h->ctx, p1);
  CHECK(rate->rate == 3000 && rate->snapshot_msec == 50);

  /* supportsSourceWire lists the non-Q3 recipe parts. */
  const qa_wire_admission *support = h->supports_source_wire(h->ctx);
  CHECK(!support->supported && support->reason_count == 1 && strcmp(support->reasons[0], "Native Q3 wire requires a Q3 character") == 0);
  recipe.character.definition.provider = "q3:character";
  CHECK(((const qa_wire_admission *)h->supports_source_wire(h->ctx))->supported);
  CHECK(h->time(h->ctx) == 4321);
  size_t occupied_count = 0;
  const double *occupied = h->occupied_slots(h->ctx, &occupied_count);
  CHECK(occupied_count == 2 && occupied[0] == 0 && occupied[1] == 1);

  /* snapshot(): a following spectator sees the followed client's origin and stats. */
  qa_q3_base_player_state_record_set_origin(&e1->client->ps, (qa_vec3){100, 200, 300});
  qa_q3_base_player_state_slots_set(&e1->client->ps.stats, 0, 55);
  qa_q3_base_player_state_record_set_origin(&e0->client->ps, (qa_vec3){1, 2, 3});
  qa_q3_base_player_state_slots_set(&e0->client->ps.stats, 0, 99);
  qa_q3_base_player_state_slots_set(&e0->client->ps.persistant, 0, 12);
  e0->client->ps.pm_flags = QA_Q3_FLAG_FOLLOW; e0->client->ps.client_num = 1; e0->client->ps.ping = 33;
  qa_app_q3_snapshot snapshot = h->snapshot(h->ctx, p0);
  const qa_q3_player_state_record *player = snapshot.player;
  CHECK(player->origin.x == 100 && player->origin.y == 200 && player->origin.z == 300);
  CHECK(player->stats[0] == 55 && player->persistant[0] == 12 && player->client_num == 1 && player->ping == 33);
  CHECK(snapshot.entity_count == 1);
  if (snapshot.entity_count == 1) {
    const qa_q3_entity_state_record *entity = snapshot.entities[0];
    CHECK(entity->number == beacon->slot && entity->e_type == 7 && entity->modelindex == 3 && entity->time2 == 1.5);
  }
  e0->client->ps.pm_flags = 0; e0->client->ps.client_num = 0;
  snapshot = h->snapshot(h->ctx, p0);
  CHECK(((const qa_q3_player_state_record *)snapshot.player)->origin.x == 1 && ((const qa_q3_player_state_record *)snapshot.player)->stats[0] == 99);

  /* input(): the native source returns the actor command to queue. */
  qa_q3_wire_user_command wire;
  memset(&wire, 0, sizeof wire);
  wire.server_time = 77; wire.weapon = 3; wire.forwardmove = 127; wire.angles[1] = 1024;
  const qa_actor_command *input = h->input(h->ctx, p0, &wire, 5);
  CHECK(input != NULL && qa_same_actor(input->actor, p0.actor) && input->sequence == 5);
  CHECK(input->source.kind == QA_COMMAND_SOURCE_REMOTE_CLIENT && qa_same_client(input->source.client, p0.client));
  CHECK(input->command.kind == QA_USER_COMMAND_Q3 && input->command.as.q3.server_time_milliseconds == 77
        && input->command.as.q3.weapon == 3 && input->command.as.q3.forward_move == 127);
  CHECK(sim.observes == 0);

  /* userinfo(): stored, admission refreshed, simulation notified. */
  h->userinfo(h->ctx, p1, "\\name\\Renamed");
  CHECK(strcmp(qa_app_q3_server_state_get_userinfo(server_state, 1), "\\name\\Renamed") == 0);
  CHECK(sim.notifies == 1 && strcmp(sim.last_notify, "userinfo") == 0);

  /* status(): infoResponse and statusResponse strings (expected from TS setInfoValue). */
  set_cvar(cvars, "sv_privateClients", "2");
  set_cvar(cvars, "sv_minPing", "20");
  set_cvar(cvars, "sv_maxPing", "0");
  set_cvar(cvars, "fs_game", "");
  const char *info = h->status(h->ctx, "abc", false);
  CHECK(info != NULL && strcmp(info, "infoResponse\n\\minPing\\20\\pure\\0\\gametype\\0\\sv_maxclients\\6"
                                      "\\clients\\0\\mapname\\q3dm17\\hostname\\Test Host\\protocol\\68\\challenge\\abc") == 0);
  set_cvar(cvars, "ui_singlePlayerActive", "1");
  CHECK(h->status(h->ctx, "abc", false) == NULL);
  /* Rows: persistant[0] ping "netname"; client 1 took its name from the admission userinfo, which the
   * runtime reads through host.engine.getUserinfo (the server state's "\\name\\Renamed"). */
  free(e0->client->pers.netname); e0->client->pers.netname = strdup("Zero");
  const char *status = h->status(h->ctx, "abc", true);
  CHECK(status != NULL && strncmp(status, "statusResponse\n", 15) == 0);
  CHECK(status != NULL && strstr(status, "\\challenge\\abc\\protocol\\68\\") != NULL
        && strstr(status, "\n12 33 \"Zero\"\n0 0 \"Renamed\"\n") != NULL);
  set_cvar(cvars, "g_gametype", "2");
  CHECK(h->status(h->ctx, "abc", true) == NULL);
  set_cvar(cvars, "g_gametype", "0");
  set_cvar(cvars, "ui_singlePlayerActive", "0");

  /* sourceRound: preflight and rebind guard the retained round. */
  sim.reset_plan = false;
  CHECK(!qa_try(call_preflight, &c, &caught) && strcmp(caught.message, "Q3 network fast restart is incompatible: map changed") == 0);
  sim.reset_plan = true;
  CHECK(qa_try(call_preflight, &c, &caught));
  CHECK(!qa_try(call_rebind, &c, &caught)
        && strcmp(caught.message, "Q3 network requires a new round with retained server state and product") == 0);
  /* reconnect stores the last command, then connects. */
  c.client = sim.movement[1].client; c.userinfo = "\\name\\Again";
  memset(&c.command, 0, sizeof c.command); c.command.server_time = 123; c.command.weapon = 2; c.command.angles[0] = 1;
  sim.player_count = 0;
  CHECK(qa_try(call_reconnect, &c, &caught) && c.result.accepted && c.result.player.source_entity == 1);
  qa_q3_base_user_command stored;
  CHECK(qa_app_q3_server_state_get_user_command(server_state, 1, &stored) && stored.server_time == 123 && stored.weapon == 2 && stored.angles.x == 1);

  /* admit(): a rejected admission closes the session client it created. */
  qa_q3_accepted_connect request;
  memset(&request, 0, sizeof request);
  request.slot = 2; request.address.kind = QA_Q3_ADDR_LOOPBACK; request.userinfo = "\\name\\Late";
  sim.admit = ADMIT_DENY;
  qa_app_q3_admission admitted = h->admit(h->ctx, &request);
  CHECK(!admitted.accepted && strcmp(admitted.reason, "Invalid password") == 0);
  sim.admit = ADMIT_OK;
  CHECK(ctx_errors == 0);

  qa_app_sim_q3_application_server_authority_destroy(authority);
  qa_engine_session_destroy(session);
  qa_app_q3_source_runtime_destroy(sim.runtime);
  qa_app_q3_server_state_destroy(server_state);
  qa_shared_scene_queries_destroy(scene);
  qa_shared_inventory_table_destroy(inventory);
  qa_gameplay_authority_destroy(combat);
  qa_actor_callback_table_destroy(callbacks);
  qa_shared_body_table_destroy(bodies);
  qa_session_actor_registry_destroy(actors);
  qa_identity_owner_destroy(&identity);
}

/* Q3SourceRuntime.admitPlayer raises Q3ClientAdmissionDenied, which a catch can match. */
static void raise_denied(void *ctx) { (void)ctx; qa_raise(QA_APP_Q3_CLIENT_ADMISSION_DENIED, 0, 0, "%s", "Server is full."); }
static void test_denied_name(void) {
  qa_caught_error caught;
  CHECK(!qa_try(raise_denied, NULL, &caught));
  CHECK(strcmp(caught.name, "Q3ClientAdmissionDenied") == 0 && strcmp(caught.message, "Server is full.") == 0);
}

int main(void) {
  test_denied_name();
  test_native_host();
  printf("test_sim_network_q3: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
