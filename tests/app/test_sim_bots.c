/* Checks for the app/bootstrap/simulation bot ports (bots.ts, bot-world.ts,
 * bot-rerelease*.ts, bot-q1/q2/q3-knowledge.ts, bot-selected-knowledge.ts,
 * bot-prediction.ts, bot-assets.ts, bot-objectives.ts,
 * bot-mover-connections.ts).
 *
 * Reproduced TS tests: tests/bots/behavior/rerelease/selected-knowledge.test.ts
 * (retail Q3 baseq3/pak0.pk3; skipped when absent). The other TS tests that
 * import these modules run a full SharedSimulation over mounted content
 * (shared-world, world-checkpoint, arena, round-restart, native-save,
 * shared-save, bot-moving-geometry, q3-routing, fast-restart); the C
 * SharedSimulation does not expose the sources those need yet, so the
 * module-level behavior they exercise is checked here through the
 * simulation view with expected values from the TS modules under bun.
 * Every callback checks a sentinel ctx. GPL-2.0-or-later. */
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "qa/app/bootstrap/simulation/bot_assets.h"
#include "qa/app/bootstrap/simulation/bot_mover_connections.h"
#include "qa/app/bootstrap/simulation/bot_objectives.h"
#include "qa/app/bootstrap/simulation/bot_q3_knowledge.h"
#include "qa/app/bootstrap/simulation/bot_rerelease_world.h"
#include "qa/app/bootstrap/simulation/bot_selected_knowledge.h"
#include "qa/app/bootstrap/simulation/bots.h"
#include "qa/bots/behavior/q3/ai_state.h"
#include "qa/content/archive.h"
#include "qa/persistence/save_image.h"
#include "qa/persistence/value.h"

static int failures = 0;
static int checks = 0;
#define CHECK(cond)                                                   \
  do {                                                                \
    checks++;                                                         \
    if (!(cond)) {                                                    \
      failures++;                                                     \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    }                                                                 \
  } while (0)
#define CHECK_STR(a, b)                                                                   \
  do {                                                                                    \
    checks++;                                                                             \
    const char *check_a = (a), *check_b = (b);                                            \
    if (check_a == NULL || check_b == NULL || strcmp(check_a, check_b) != 0) {            \
      failures++;                                                                         \
      fprintf(stderr, "FAIL %s:%d: [%s] != [%s]\n", __FILE__, __LINE__, check_a ? check_a : "(null)", check_b ? check_b : "(null)"); \
    }                                                                                     \
  } while (0)
#define CHECK_ABORTS(...)                                                \
  do {                                                                   \
    checks++;                                                            \
    fflush(stderr);                                                      \
    pid_t pid = fork();                                                  \
    if (pid == 0) {                                                      \
      (void)prctl(PR_SET_DUMPABLE, 0);                                   \
      if (freopen("/dev/null", "w", stderr) == NULL) _exit(2);           \
      __VA_ARGS__;                                                       \
      _exit(0);                                                          \
    }                                                                    \
    int status = 0;                                                      \
    waitpid(pid, &status, 0);                                            \
    if (!(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT)) {         \
      failures++;                                                        \
      fprintf(stderr, "FAIL %s:%d: expected abort\n", __FILE__, __LINE__); \
    }                                                                    \
  } while (0)

/* ---- sentinel simulation view ---- */
typedef struct sim_fixture {
  const void *sentinel;
  qa_identity_owner identity;
  qa_executable_recipe recipe;
  qa_provider_reference weapons[1];
  qa_provider_timing timing[1];
  qa_actor_id players[4]; size_t player_count;
  qa_client_id clients[4]; size_t client_count;
  bool has_q1; qa_app_sim_bot_q1_source q1; qa_comp_q1_source_composition composition;
  bool q1_weapon; bool q2_weapon; bool q3_weapon;
  qa_app_sim_source_objective objectives[2]; size_t objective_count;
  qa_app_sim_player_ui ui; qa_app_sim_player_ui_item items[4];
  int calls;
} sim_fixture;
static sim_fixture *fx(void *ctx) { sim_fixture *f = ctx; CHECK(f->sentinel == f); f->calls++; return f; }
static double sim_time(void *ctx) { fx(ctx); return 1.25; }
static const char *sim_entities(void *ctx) { fx(ctx); return ""; }
static const qa_actor_id *sim_players(void *ctx, size_t *n) { sim_fixture *f = fx(ctx); *n = f->player_count; return f->players; }
static const qa_client_id *sim_clients(void *ctx, size_t *n) { sim_fixture *f = fx(ctx); *n = f->client_count; return f->clients; }
static qa_app_sim_movement_player *sim_movement(void *ctx, qa_actor_id actor) { fx(ctx); (void)actor; return NULL; }
static qa_app_sim_player_ui sim_ui(void *ctx, qa_actor_id actor) { (void)actor; return fx(ctx)->ui; }
static bool sim_bot_entity(void *ctx, qa_actor_id actor, qa_app_sim_bot_entity *out) { fx(ctx); (void)actor; (void)out; return false; }
static void sim_command(void *ctx, qa_actor_id actor, const char *name, const char *const *args, size_t n) { fx(ctx); (void)actor; (void)name; (void)args; (void)n; }
static qa_owned_actor sim_prepare(void *ctx, qa_client_id client) { fx(ctx); (void)client; qa_owned_actor a; memset(&a, 0, sizeof a); return a; }
static void sim_disconnect(void *ctx, qa_actor_id actor) { fx(ctx); (void)actor; }
static void sim_identity(void *ctx, qa_actor_id actor, const qa_app_sim_player_identity *identity) { fx(ctx); (void)actor; (void)identity; }
static const qa_app_sim_source_objective *sim_objectives(void *ctx, size_t *n) { sim_fixture *f = fx(ctx); *n = f->objective_count; return f->objectives; }
static qa_cvar_registry *sim_q2_cvars(void *ctx) { fx(ctx); return NULL; }
static const qa_app_sim_bot_q1_source *sim_q1(void *ctx) { sim_fixture *f = fx(ctx); return f->has_q1 ? &f->q1 : NULL; }
static qa_q2_product_runtime *sim_q2(void *ctx) { fx(ctx); return NULL; }
static const qa_app_sim_bot_q3_source *sim_q3(void *ctx) { fx(ctx); return NULL; }
static bool sim_q3_guest(void *ctx) { fx(ctx); return false; }
static qa_q1_services *sim_q1_weapon(void *ctx) { sim_fixture *f = fx(ctx); return f->q1_weapon ? (qa_q1_services *)f : NULL; } /* identity only */
static bool sim_q2_weapon(void *ctx, qa_q2_game_services **g, qa_q2w_weapons **w) { sim_fixture *f = fx(ctx); *g = NULL; *w = NULL; return f->q2_weapon; }
static qa_app_q3_selected_arsenal *sim_q3_weapon(void *ctx) { sim_fixture *f = fx(ctx); return f->q3_weapon ? (qa_app_q3_selected_arsenal *)f : NULL; }
static qa_app_sim_bot_simulation make_sim(sim_fixture *f) {
  qa_app_sim_bot_simulation s;
  memset(&s, 0, sizeof s);
  s.ctx = f; s.recipe = &f->recipe; s.mode = "deathmatch"; s.max_clients = 4;
  s.time_seconds = sim_time; s.source_entity_text = sim_entities; s.players = sim_players; s.client_identities = sim_clients;
  s.movement_player = sim_movement; s.player_ui = sim_ui; s.bot_entity = sim_bot_entity; s.player_command = sim_command;
  s.prepare_bot_client = sim_prepare; s.disconnect_player = sim_disconnect; s.set_source_player_identity = sim_identity;
  s.source_objectives = sim_objectives; s.q2_server_cvars = sim_q2_cvars; s.q1_source = sim_q1; s.q2_source = sim_q2;
  s.q3_source = sim_q3; s.q3_guest = sim_q3_guest; s.q1_weapon_source = sim_q1_weapon; s.q2_weapon_source = sim_q2_weapon;
  s.selected_q3_weapon_source = sim_q3_weapon;
  return s;
}
static void fixture_init(sim_fixture *f) {
  memset(f, 0, sizeof *f);
  f->sentinel = f;
  CHECK(qa_identity_owner_create(&f->identity, "sim-bots"));
  f->weapons[0].provider = "q1:official"; f->weapons[0].content = "q1:id1";
  f->recipe.weapons.values = f->weapons; f->recipe.weapons.count = 1;
  f->recipe.engine_behavior.provider = "q1:engine";
  f->timing[0].provider = "q1:engine";
  f->recipe.timing = f->timing; f->recipe.timing_count = 1;
}

/* ---- selected-knowledge.test.ts ---- */
static bool test_selected_knowledge(void) {
  const char *home = getenv("HOME");
  char path[1024];
  snprintf(path, sizeof path, "%s/qfiles/Quake 3 Arena/baseq3/pak0.pk3", home != NULL ? home : "");
  if (access(path, R_OK) != 0) return false;
  qa_bot_asset_files files;
  qa_bot_asset_files_init(&files);
  qa_archive_handle *archive = qa_open_archive(path, false, QA_ARCHIVE_FORMAT_PK3);
  CHECK(archive != NULL);
  for (size_t e = 0; archive != NULL && e < archive->entry_count; e++) {
    const qa_archive_entry *entry = &archive->entries[e];
    if (entry->is_directory || strncmp(entry->path, "botfiles/", 9) != 0) continue;
    size_t length = 0;
    uint8_t *bytes = qa_archive_read_entry_entry(archive, entry, &length);
    CHECK(qa_bot_asset_files_add(&files, entry->path, bytes, length));
    free(bytes);
  }
  if (archive != NULL) qa_archive_destroy(archive);
  qa_app_sim_bot_asset_view view;
  memset(&view, 0, sizeof view);
  view.files = &files;
  qa_source_bot_files source = qa_app_sim_bot_asset_source_files(&view);
  qa_app_sim_q3_weapon_knowledge weapons = qa_app_sim_native_q3_weapon_knowledge(&source.files);
  /* Every row from nativeQ3WeaponKnowledge under bun over the same pak0. */
  static const struct { const char *name; double number, damage, speed, min_range, max_range, priority; const char *ammo; const char *flags; const char *aim; bool melee, needs_ammo; } expected[] = {
      {"q3:weapon/gauntlet", 2, 50, 0, 0, 60, 1000, "", "melee", "center", true, false},
      {"q3:weapon/machinegun", 4, 8, 0, 0, 4096, 160, "q3:ammo/machinegun", "hitscan", "center", false, true},
      {"q3:weapon/shotgun", 8, 110, 0, 0, 4096, 2200, "q3:ammo/shotgun", "hitscan", "center", false, true},
      {"q3:weapon/grenadelauncher", 16, 120, 700, 160, 4096, 2400, "q3:ammo/grenadelauncher", "projectile+explosive", "feet", false, true},
      {"q3:weapon/rocketlauncher", 32, 100, 900, 120, 4096, 2000, "q3:ammo/rocketlauncher", "projectile+explosive", "feet", false, true},
      {"q3:weapon/lightning", 64, 24, 0, 0, 768, 480, "q3:ammo/lightning", "hitscan", "center", false, true},
      {"q3:weapon/railgun", 128, 100, 0, 0, 4096, 2000, "q3:ammo/railgun", "hitscan", "center", false, true},
      {"q3:weapon/plasmagun", 256, 20, 2000, 20, 4096, 400, "q3:ammo/plasmagun", "projectile+explosive", "feet", false, true},
      {"q3:weapon/bfg", 512, 40, 0, 100, 4096, 800, "q3:ammo/bfg", "hitscan+explosive", "feet", false, true}};
  CHECK(weapons.count == sizeof expected / sizeof expected[0]);
  for (size_t i = 0; i < weapons.count && i < sizeof expected / sizeof expected[0]; i++) {
    const qa_weapon_entry *w = weapons.weapons[i].entry;
    char flags[64] = "";
    for (size_t f = 0; f < w->flag_count; f++) { if (f != 0) strcat(flags, "+"); strcat(flags, w->flags[f]); }
    CHECK_STR(w->name, expected[i].name);
    CHECK(w->number == expected[i].number && w->identity.bit == expected[i].number && !w->identity.is_q2_classname);
    CHECK(w->damage == expected[i].damage && w->speed == expected[i].speed);
    CHECK(w->min_range == expected[i].min_range && w->max_range == expected[i].max_range);
    CHECK(w->priority == expected[i].priority);
    CHECK_STR(w->ammo_name, expected[i].ammo); CHECK_STR(w->ammo, expected[i].ammo);
    CHECK(w->min_ammo == 0 && w->max_ammo == 200);
    CHECK_STR(flags, expected[i].flags); CHECK_STR(w->aim_point, expected[i].aim);
    CHECK(weapons.weapons[i].is_melee == expected[i].melee && weapons.weapons[i].needs_ammo == expected[i].needs_ammo);
    CHECK(!weapons.weapons[i].is_electric);
  }
  /* TS: rocket damage 100, speed 900, number 1 << 5, explosive; no grapple. */
  bool grapple = false;
  for (size_t i = 0; i < weapons.count; i++) grapple = grapple || strcmp(weapons.weapons[i].entry->name, "q3:weapon/grapple") == 0;
  CHECK(!grapple);
  qa_app_sim_q3_weapon_knowledge_free(&weapons);
  qa_app_sim_bot_asset_view_destroy(&view);
  qa_bot_asset_files_destroy(&files);
  return true;
}

/* ---- BotAssetFiles.provenance (bun oracle) ---- */
static void test_provenance(void) {
  qa_bot_asset_files files;
  qa_bot_asset_files_init(&files);
  CHECK(qa_bot_asset_files_add(&files, "bots/a.txt", (const uint8_t *)"alpha", 5));
  CHECK(qa_bot_asset_files_add(&files, "Bots\\B.TXT", (const uint8_t *)"beta!", 5));
  CHECK(qa_bot_asset_files_add(&files, "bots/a.txt", (const uint8_t *)"gamma", 5));
  char out[65];
  qa_app_sim_bot_asset_provenance(&files, out);
  CHECK_STR(out, "e36b7833effc21dae95444062f22df794133ff8f483317ff62acd8dc45de4dd1");
  qa_app_sim_bot_asset_view view;
  memset(&view, 0, sizeof view);
  view.files = &files;
  qa_source_bot_files source = qa_app_sim_bot_asset_source_files(&view);
  CHECK_STR(source.provenance(source.files.ctx), out);
  size_t length = 0;
  const uint8_t *bytes = source.files.read(source.files.ctx, "BOTS/A.TXT", &length);
  CHECK(bytes != NULL && length == 5 && memcmp(bytes, "gamma", 5) == 0);
  CHECK(source.files.read(source.files.ctx, "bots/none.txt", &length) == NULL);
  qa_app_sim_bot_asset_view_destroy(&view);
  qa_bot_asset_files_destroy(&files);
}

/* ---- transport checkpoint codec and RestoredBotReliableCommands ---- */
static qa_save_value *transport_value(qa_save_value *connection) {
  qa_save_value *root = qa_save_value_object(), *connections = qa_save_value_array(), *snapshots = qa_save_value_array();
  qa_save_value *snapshot = qa_save_value_object(), *entities = qa_save_value_array();
  qa_save_value_set(root, "version", qa_save_value_number(1));
  qa_save_value_set(root, "elapsedMilliseconds", qa_save_value_number(48));
  qa_save_value_push(connections, connection);
  qa_save_value_set(root, "connections", connections);
  qa_save_value_set(snapshot, "client", qa_save_value_number(2));
  qa_save_value_push(entities, qa_save_value_number(0)); qa_save_value_push(entities, qa_save_value_number(65));
  qa_save_value_set(snapshot, "entities", entities);
  qa_save_value_push(snapshots, snapshot);
  qa_save_value_set(root, "snapshots", snapshots);
  return root;
}
static void decode_invalid_ring(qa_save_value *value) {
  qa_app_sim_bot_transport_checkpoint image;
  qa_app_sim_decode_bot_transport(value, &image);
  image.connections[0].slot_count = 3;
  qa_app_sim_restored_bot_reliable_commands(&image.connections[0]);
}
static void test_transport_codec(void) {
  qa_identity_owner identity;
  CHECK(qa_identity_owner_create(&identity, "transport"));
  qa_session_client *client = qa_session_client_create(qa_identity_owner_client(&identity, 2, 7));
  qa_q3_server_reliable_commands *ring = NULL;
  CHECK(qa_q3_server_reliable_commands_init(&ring));
  qa_q3_server_reliable_commands_add(ring, "print \"one\"\n");
  qa_q3_server_reliable_commands_add(ring, "cs 5 \"two\"\n");
  qa_q3_reliable_ring_assign_acknowledgement(ring, 1);
  qa_actor_id actor = qa_identity_owner_actor(&identity, 9, 3);
  qa_save_value *value = transport_value(qa_app_sim_encode_bot_transport_connection(client, actor, ring));
  size_t length = 0;
  uint8_t *bytes = qa_encode_checkpoint_value(value, &length);
  qa_save_value *decoded = qa_decode_checkpoint_value(bytes, length);
  qa_app_sim_bot_transport_checkpoint image;
  qa_app_sim_decode_bot_transport(decoded, &image);
  CHECK(image.elapsed_milliseconds == 48 && image.connection_count == 1 && image.snapshot_count == 1);
  const qa_app_sim_bot_transport_connection *c = &image.connections[0];
  CHECK(c->client_slot == 2 && c->client_generation == 7 && c->actor.slot == 9 && c->actor.generation == 3);
  CHECK(c->sequence == 2 && c->acknowledge == 1 && c->slot_count == QA_Q3_MAX_RELIABLE_COMMANDS);
  CHECK_STR(c->slots[1], "print \"one\"\n"); CHECK_STR(c->slots[2], "cs 5 \"two\"\n"); CHECK_STR(c->slots[3], "");
  CHECK(image.snapshots[0].client == 2 && image.snapshots[0].entity_count == 2 && image.snapshots[0].entities[1] == 65);
  qa_q3_server_reliable_commands *restored = qa_app_sim_restored_bot_reliable_commands(c);
  CHECK(restored != NULL);
  CHECK(qa_q3_reliable_ring_sequence(restored) == 2 && qa_q3_reliable_ring_acknowledge(restored) == 1);
  for (int64_t i = 0; i < QA_Q3_MAX_RELIABLE_COMMANDS; i++)
    CHECK(strcmp(qa_q3_reliable_ring_lookup_masked(restored, i), qa_q3_reliable_ring_lookup_masked(ring, i)) == 0);
  CHECK_ABORTS(decode_invalid_ring(decoded));
  qa_q3_reliable_ring_destroy(restored);
  qa_app_sim_bot_transport_checkpoint_free(&image);
  free(bytes);
  qa_save_value_free(decoded); qa_save_value_free(value);
  qa_q3_reliable_ring_destroy(ring);
  qa_session_client_destroy(client);
  qa_identity_owner_destroy(&identity);
}

/* ---- SimulationBotServices over a sentinel transport ---- */
typedef struct transport_fixture {
  const void *sentinel;
  qa_app_sim_bots_options options;
  qa_app_sim_bot_client clients[2];
  qa_actor_id actors[2];
  qa_client_id ids[2];
  int64_t disconnected;
  int frames;
} transport_fixture;
static transport_fixture *tf(void *ctx) { transport_fixture *t = ctx; CHECK(t->sentinel == t); return t; }
static bool t_is_bot(void *ctx, qa_actor_id actor) { transport_fixture *t = tf(ctx); return qa_same_actor(actor, t->actors[1]); }
static int32_t t_move(void *ctx, qa_actor_id actor, qa_vec3 point, double tolerance) { tf(ctx); (void)actor; (void)point; return tolerance > 10 ? 3 : 1; }
static int32_t t_follow(void *ctx, qa_actor_id actor, qa_actor_id target) { tf(ctx); (void)actor; (void)target; return 2; }
static const qa_app_sim_bot_client *t_clients(void *ctx, size_t *n) { transport_fixture *t = tf(ctx); *n = 2; return t->clients; }
static bool t_actor(void *ctx, qa_client_id client, qa_actor_id *out) {
  transport_fixture *t = tf(ctx);
  for (int i = 0; i < 2; i++) if (qa_same_client(client, t->ids[i])) { *out = t->actors[i]; return true; }
  return false;
}
static bool t_disconnect(void *ctx, int64_t client) { transport_fixture *t = tf(ctx); t->disconnected = client; return true; }
static const qa_actor_command *t_frame(void *ctx, double time, double elapsed, size_t *n) { transport_fixture *t = tf(ctx); (void)time; (void)elapsed; t->frames++; *n = 0; return NULL; }
static void test_services(void) {
  qa_identity_owner identity;
  CHECK(qa_identity_owner_create(&identity, "services"));
  transport_fixture t;
  memset(&t, 0, sizeof t);
  t.sentinel = &t; t.disconnected = -1;
  for (int i = 0; i < 2; i++) {
    t.ids[i] = qa_identity_owner_client(&identity, i + 1, 1);
    t.actors[i] = qa_identity_owner_actor(&identity, 10 + i, 1);
    t.clients[i].client = qa_session_client_create(t.ids[i]);
  }
  qa_app_sim_bot_service service;
  memset(&service, 0, sizeof service);
  service.ctx = &t; service.options = &t.options;
  service.is_bot = t_is_bot; service.move_to_point = t_move; service.follow_actor = t_follow;
  service.clients = t_clients; service.actor = t_actor; service.disconnect = t_disconnect; service.frame = t_frame;
  qa_app_sim_bot_services services;
  qa_app_sim_bot_services_init(&services);
  qa_vec3 point = {1, 2, 3};
  CHECK(qa_app_sim_bot_services_move_to_point(&services, t.actors[0], point, 64) == 0);
  CHECK(qa_app_sim_bot_services_follow_actor(&services, t.actors[0], t.actors[1]) == 0);
  CHECK(!qa_app_sim_bot_services_is_bot(&services, t.actors[1]));
  CHECK(!qa_app_sim_bot_services_try_add_bot(&services));
  CHECK(!qa_app_sim_bot_services_try_disconnect_bot(&services, t.actors[1]));
  CHECK(qa_app_sim_bot_services_configuration(&services) == NULL && qa_app_sim_bot_services_checkpoint(&services) == NULL);
  qa_app_sim_bot_route route = qa_app_sim_bot_services_navigation(&services, t.actors[0], point, point);
  CHECK(route.kind == QA_APP_SIM_BOT_ROUTE_NO_NAVIGATION);
  CHECK_ABORTS(qa_app_sim_bot_services_source_connect(&services, 1, false));
  qa_app_sim_bot_services_attach_transport(&services, &service);
  CHECK_ABORTS(qa_app_sim_bot_services_attach_transport(&services, &service));
  CHECK(qa_app_sim_bot_services_move_to_point(&services, t.actors[0], point, 64) == 3);
  CHECK(qa_app_sim_bot_services_follow_actor(&services, t.actors[0], t.actors[1]) == 2);
  CHECK(qa_app_sim_bot_services_is_bot(&services, t.actors[1]) && !qa_app_sim_bot_services_is_bot(&services, t.actors[0]));
  size_t count = 99;
  t.options.automatic_frame = false;
  CHECK(qa_app_sim_bot_services_frame(&services, 0, 16, &count) == NULL && count == 0 && t.frames == 0);
  t.options.automatic_frame = true;
  qa_app_sim_bot_services_frame(&services, 0, 16, &count);
  CHECK(t.frames == 1);
  CHECK(qa_app_sim_bot_services_try_disconnect_bot(&services, t.actors[1]) && t.disconnected == 2);
  CHECK_ABORTS(qa_app_sim_bot_services_detach_transport(&services, NULL));
  qa_app_sim_bot_services_detach_transport(&services, &service);
  CHECK(!qa_app_sim_bot_services_is_bot(&services, t.actors[1]));
  CHECK_ABORTS(qa_app_sim_bot_services_detach(&services, (qa_source_bot_director *)&t));
  CHECK(services.source.ctx == &services && strcmp(services.source.kind, "available") == 0);
  for (int i = 0; i < 2; i++) qa_session_client_destroy(t.clients[i].client);
  qa_identity_owner_destroy(&identity);
}

/* ---- botAdmissionError ---- */
static void test_admission(void) {
  sim_fixture f;
  fixture_init(&f);
  qa_app_sim_bot_simulation s = make_sim(&f);
  CHECK_STR(qa_app_sim_bot_admission_error(&s), "Shared bot arsenal observation is unavailable");
  f.q1_weapon = true;
  CHECK_STR(qa_app_sim_bot_admission_error(&s), "Bot world observation is unavailable");
  f.has_q1 = true; f.q1.composition = &f.composition;
  CHECK(qa_app_sim_bot_admission_error(&s) == NULL);
  f.q1_weapon = false; f.q3_weapon = true;
  CHECK(qa_app_sim_bot_admission_error(&s) == NULL);
  CHECK(f.calls > 0);
  qa_identity_owner_destroy(&f.identity);
}

/* ---- BotArsenalBinding plumbing and botKnowledgePersistence ---- */
typedef struct arsenal_fixture { const void *sentinel; qa_identity_owner identity; int updates; } arsenal_fixture;
static bool af_actor(void *ctx, int64_t client, qa_actor_id *out) {
  arsenal_fixture *a = ctx; CHECK(a->sentinel == a);
  if (client > 3) return false;
  *out = qa_identity_owner_actor(&a->identity, client + 20, 1); return true;
}
static void af_update(void *ctx, qa_bot_state_bots_a *state) {
  qa_app_sim_bot_arsenal *arsenal = ctx;
  arsenal_fixture *a = arsenal->actor_ctx; CHECK(a->sentinel == a);
  qa_actor_id actor;
  bool has = qa_app_sim_bot_arsenal_actor(arsenal, state->src->client, &actor);
  qa_app_sim_bot_arsenal_track(arsenal, state->src->ws, has ? &actor : NULL);
  a->updates++;
}
static qa_bot_weapon_knowledge_bots_a *af_candidates(void *ctx, qa_bot_library_bots_a *library, int64_t handle, size_t *n) {
  (void)library; (void)handle;
  qa_app_sim_bot_arsenal *arsenal = ctx;
  arsenal_fixture *a = arsenal->actor_ctx; CHECK(a->sentinel == a);
  *n = 0; return calloc(1, sizeof(qa_bot_weapon_knowledge_bots_a));
}
static qa_actor_id ref_actor(void *ctx, qa_saved_actor_id saved) {
  arsenal_fixture *a = ctx; CHECK(a->sentinel == a);
  return qa_identity_owner_actor(&a->identity, (int64_t)saved.slot, (int64_t)saved.generation + 1);
}
static int64_t ref_handle(void *ctx, int64_t saved) { arsenal_fixture *a = ctx; CHECK(a->sentinel == a); return saved + 1; }
static int64_t ref_handle_same(void *ctx, int64_t saved) { arsenal_fixture *a = ctx; CHECK(a->sentinel == a); (void)saved; return 5; }
static void restore_duplicate(qa_app_sim_bot_arsenal *arsenal, const qa_save_value *value, arsenal_fixture *a) {
  qa_app_sim_bot_arsenal_restore_checkpoint(arsenal, value, ref_actor, a, ref_handle_same, a);
}
static void test_arsenal_plumbing(void) {
  arsenal_fixture a;
  memset(&a, 0, sizeof a);
  a.sentinel = &a;
  CHECK(qa_identity_owner_create(&a.identity, "arsenal"));
  sim_fixture f;
  fixture_init(&f);
  qa_app_sim_bot_simulation s = make_sim(&f);
  qa_app_sim_bot_arsenal *arsenal = qa_app_sim_bot_arsenal_alloc("q1", &s, af_actor, &a);
  qa_app_sim_bot_arsenal_bind(arsenal, af_update, af_candidates);
  qa_bot_state_bots_a state;
  CHECK(qa_bot_state_bots_a_init(&state, QA_Q3_BASE_PRODUCT_BASEQ3, NULL));
  state.src->client = 1; state.src->ws = 4;
  arsenal->knowledge.knowledge.update_inventory(arsenal->knowledge.knowledge.ctx, &state);
  state.src->client = 2; state.src->ws = 2;
  arsenal->knowledge.knowledge.update_inventory(arsenal->knowledge.knowledge.ctx, &state);
  state.src->client = 1; state.src->ws = 4; /* Map.set keeps position */
  arsenal->knowledge.knowledge.update_inventory(arsenal->knowledge.knowledge.ctx, &state);
  CHECK(a.updates == 3 && arsenal->actor_count == 2 && arsenal->actors[0].handle == 4 && arsenal->actors[1].handle == 2);
  qa_save_value *value = qa_app_sim_bot_arsenal_checkpoint(arsenal);
  CHECK_STR(qa_save_value_get(value, "source")->text, "q1");
  const qa_save_value *actors = qa_save_value_get(value, "actors");
  CHECK(actors->item_count == 2 && qa_save_value_get(actors->items[0], "handle")->number == 4);
  CHECK(qa_save_value_get(qa_save_value_get(actors->items[0], "actor"), "slot")->number == 21);
  qa_app_sim_bot_arsenal_restore_checkpoint(arsenal, value, ref_actor, &a, ref_handle, &a);
  qa_actor_id restored;
  CHECK(qa_app_sim_bot_arsenal_handle_actor(arsenal, 5, &restored) && restored.slot == 21 && restored.generation == 2);
  CHECK(!qa_app_sim_bot_arsenal_handle_actor(arsenal, 4, &restored));
  CHECK_ABORTS(restore_duplicate(arsenal, value, &a));
  state.src->client = 9; state.src->ws = 5; /* actor null deletes */
  arsenal->knowledge.knowledge.update_inventory(arsenal->knowledge.knowledge.ctx, &state);
  CHECK(!qa_app_sim_bot_arsenal_handle_actor(arsenal, 5, &restored) && arsenal->actor_count == 1);
  qa_save_value_free(value);
  qa_bot_state_bots_a_destroy(&state);
  qa_app_sim_bot_arsenal_destroy(arsenal);
  qa_identity_owner_destroy(&f.identity);
  qa_identity_owner_destroy(&a.identity);
}

/* ---- openApplicationBotLog / resumeApplicationBotLog ---- */
static void test_log(void) {
  char directory[256];
  const char *tmp = getenv("TMPDIR");
  snprintf(directory, sizeof directory, "%s/sim-bots-log-%d/nested", tmp != NULL ? tmp : "/tmp", (int)getpid());
  qa_bot_log_open_result opened = qa_app_sim_open_application_bot_log(directory, "../escape/bot.log");
  CHECK(opened.opened);
  if (!opened.opened) return;
  CHECK(opened.stream.write(opened.stream.ctx, (const uint8_t *)"hello ", 6).kind == QA_LOG_IO_OK);
  CHECK(opened.stream.write(opened.stream.ctx, (const uint8_t *)"world", 5).kind == QA_LOG_IO_OK);
  CHECK(opened.stream.has_checkpoint && opened.stream.checkpoint_position(opened.stream.ctx) == 11);
  CHECK(opened.stream.flush(opened.stream.ctx).kind == QA_LOG_IO_OK);
  CHECK(opened.stream.close(opened.stream.ctx).kind == QA_LOG_IO_OK);
  qa_bot_log_open_result resumed = qa_app_sim_resume_application_bot_log(directory, "bot.log", 6);
  CHECK(resumed.opened);
  if (resumed.opened) {
    CHECK(resumed.stream.write(resumed.stream.ctx, (const uint8_t *)"there", 5).kind == QA_LOG_IO_OK);
    CHECK(resumed.stream.checkpoint_position(resumed.stream.ctx) == 11);
    resumed.stream.close(resumed.stream.ctx);
  }
  char path[512];
  snprintf(path, sizeof path, "%s/bot.log", directory);
  FILE *file = fopen(path, "rb");
  char text[32] = {0};
  if (file != NULL) { size_t n = fread(text, 1, sizeof text - 1, file); (void)n; fclose(file); }
  CHECK_STR(text, "hello there");
  CHECK(!qa_app_sim_resume_application_bot_log(directory, "bot.log", 12).opened);
  CHECK(!qa_app_sim_resume_application_bot_log(directory, "bot.log", -1).opened);
  CHECK(!qa_app_sim_resume_application_bot_log(directory, "bot.log", 1.5).opened);
  CHECK(!qa_app_sim_resume_application_bot_log(directory, "missing.log", 0).opened);
  remove(path);
  rmdir(directory);
  char parent[256];
  snprintf(parent, sizeof parent, "%s/sim-bots-log-%d", tmp != NULL ? tmp : "/tmp", (int)getpid());
  rmdir(parent);
}

/* ---- bot-mover-connections, bot-objectives, bot-rerelease-world ---- */
static void test_world_helpers(void) {
  qa_vec3 ride = qa_app_sim_train_riding_point((qa_vec3){10, 20, 30}, (qa_bounds){{-8, -8, 0}, {8, 8, 32}}, -24);
  CHECK(ride.x == 10 && ride.y == 20 && ride.z == 30 + 32 + 24 + 0.125);
  CHECK(qa_app_sim_train_travel_seconds(1, (qa_vec3){0, 0, 0}, (qa_vec3){3, 4, 12}, 13) == 2);
  sim_fixture f;
  fixture_init(&f);
  qa_app_sim_bot_simulation s = make_sim(&f);
  qa_navigation_profile profile;
  memset(&profile, 0, sizeof profile);
  qa_app_sim_train_connections none = qa_app_sim_application_train_connections(&s, &profile);
  CHECK(none.count == 0 && none.connections == NULL);
  qa_app_sim_train_connections_free(&none);
  CHECK(qa_app_sim_rerelease_bot_team_of("team:red") == 1 && qa_app_sim_rerelease_bot_team_of("blue") == 2);
  CHECK(qa_app_sim_rerelease_bot_team_of(NULL) == 0 && qa_app_sim_rerelease_bot_team_of("green") == 0);
  /* objectives over a Q1 source with no CTF in coop */
  qa_cvar_registry_options options;
  memset(&options, 0, sizeof options);
  options.dialect = QA_COMMAND_DIALECT_Q3;
  qa_cvar_registry *cvars = qa_cvar_registry_create(&options);
  f.has_q1 = true; f.q1.cvars = cvars; f.q1.composition = &f.composition;
  qa_app_sim_rerelease_bot_objectives_state *objectives = qa_app_sim_rerelease_bot_objectives_create(&s, NULL);
  qa_actor_id actor = qa_identity_owner_actor(&f.identity, 3, 1), carrier = qa_identity_owner_actor(&f.identity, 4, 1);
  f.objectives[0].has_carrier = true; f.objectives[0].carrier = carrier; f.objective_count = 1;
  CHECK(objectives->objectives.carrying(objectives->objectives.ctx, carrier));
  CHECK(!objectives->objectives.carrying(objectives->objectives.ctx, actor));
  f.objective_count = 0;
  qa_vec3 goal;
  CHECK(!objectives->objectives.goal(objectives->objectives.ctx, actor, &goal)); /* deathmatch, no tag/ball */
  qa_app_sim_rerelease_bot_objectives_destroy(objectives);
  f.has_q1 = false;
  CHECK_ABORTS(qa_app_sim_rerelease_bot_objectives_create(&s, NULL));
  /* nativeWeaponItem: knowledge weapon names normalize against selected arsenal item ids */
  qa_bot_knowledge knowledge;
  memset(&knowledge, 0, sizeof knowledge);
  qa_weapon_entry entries[2];
  memset(entries, 0, sizeof entries);
  entries[0].name = "weapon_rocketlauncher"; entries[0].number = 16;
  entries[1].name = "q1:weapon/super_shotgun"; entries[1].number = 4;
  qa_bot_weapon_rec weapons[2] = {{&entries[0], false, false, true}, {&entries[1], false, false, true}};
  qa_bot_knowledge_replace_weapons(&knowledge, weapons, 2);
  f.items[0] = (qa_app_sim_player_ui_item){"q2:weapon_rocketlauncher", "Rockets", "weapon", 0, true, false, false, 0, 0};
  f.items[1] = (qa_app_sim_player_ui_item){"q1:weapon/supershotgun", "SSG", "weapon", 0, true, false, false, 0, 0};
  f.items[2] = (qa_app_sim_player_ui_item){"q2:item_quad", "Quad", "powerup", 0, true, false, false, 0, 0};
  f.ui.items = f.items; f.ui.item_count = 3;
  CHECK_STR(qa_app_sim_native_weapon_item(&s, actor, &knowledge, 16), "q2:weapon_rocketlauncher");
  CHECK_STR(qa_app_sim_native_weapon_item(&s, actor, &knowledge, 4), "q1:weapon/supershotgun");
  CHECK(qa_app_sim_native_weapon_item(&s, actor, &knowledge, 8) == NULL);
  qa_cvar_registry_destroy(cvars);
  qa_identity_owner_destroy(&f.identity);
  /* RereleaseFrameSnapshot: each begin opens a new frame; end drops the list. */
  qa_app_sim_rerelease_frame_snapshot snapshot;
  memset(&snapshot, 0, sizeof snapshot);
  qa_app_sim_rerelease_frame_snapshot_begin(&snapshot);
  CHECK(snapshot.open && snapshot.serial == 1 && !snapshot.has_list);
  snapshot.has_list = true;
  qa_app_sim_rerelease_frame_snapshot_end(&snapshot);
  CHECK(!snapshot.open && !snapshot.has_list && snapshot.serial == 1);
  qa_app_sim_rerelease_frame_snapshot_begin(&snapshot);
  CHECK(snapshot.serial == 2);
}

int main(void) {
  bool retail = test_selected_knowledge();
  test_provenance();
  test_transport_codec();
  test_services();
  test_admission();
  test_arsenal_plumbing();
  test_log();
  test_world_helpers();
  printf("test_sim_bots: %d checks, %d failures%s\n", checks, failures, retail ? "" : " (retail Q3 corpus absent: selected knowledge skipped)");
  return failures == 0 ? 0 : 1;
}
