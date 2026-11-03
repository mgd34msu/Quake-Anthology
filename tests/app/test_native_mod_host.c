/* C checks for src/app/bootstrap/simulation/native-mod-host.ts and
 * native-mod.ts. No TS test runs these modules on retail data, so the
 * expected values come from running the TS classes under bun on the same
 * DLLs, map and declaration (the oracle script is reproduced in the
 * comment above run_retail). Every callback checks a sentinel ctx. The
 * retail cases run when ~/qfiles (or $QA_CORPUS laid out as ~/qfiles) has
 * the DLLs and baseq2/pak0.pak, as the TS suite skips without its corpus.
 * GPL-2.0-or-later. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include "qa/app/bootstrap/simulation/native_mod.h"
#include "qa/app/bootstrap/simulation/native_mod_host.h"
#include "qa/app/bootstrap/simulation/physics.h"
#include "qa/compat/q2/native_mod_provider.h"
#include "qa/content/archive.h"
#include "qa/content/mods/declaration.h"
#include "qa/content/mods/native_callbacks.h"
#include "qa/core/cvars/index.h"
#include "qa/formats/q2_map.h"
#include "qa/network/common/session.h"
#include "qa/world/actors/body.h"
#include "qa/world/actors/callbacks.h"
#include "qa/world/actors/registry.h"
#include "qa/world/gameplay/authority.h"
#include "qa/world/gameplay/inventory.h"
#include "qa/world/session/mod_engine.h"
#include "qa/world/session/resources.h"

static int failures, checks;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_ABORTS(expr) do { checks++; fflush(NULL); pid_t pid_ = fork(); \
    if (pid_ == 0) { prctl(PR_SET_DUMPABLE, 0); if (freopen("/dev/null", "w", stderr) == NULL) _exit(3); expr; _exit(0); } \
    int status_ = 0; waitpid(pid_, &status_, 0); \
    if (!(WIFSIGNALED(status_) || (WIFEXITED(status_) && WEXITSTATUS(status_) != 0))) { failures++; fprintf(stderr, "%s:%d: expected abort: %s\n", __FILE__, __LINE__, #expr); } } while (0)
static int sentinel_storage;
#define SENTINEL ((void *)&sentinel_storage)
#define CTX_OK(ctx) do { if ((ctx) != SENTINEL) { fprintf(stderr, "%s:%d: wrong callback ctx\n", __FILE__, __LINE__); abort(); } } while (0)

static char corpus_root[512];
static const char *corpus(const char *relative, char *out, size_t size) { snprintf(out, size, "%s/%s", corpus_root, relative); return out; }
static bool exists(const char *path) { struct stat st; return stat(path, &st) == 0; }
static uint8_t *read_file(const char *path, size_t *length) {
  FILE *f = fopen(path, "rb"); if (f == NULL) return NULL;
  fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
  uint8_t *bytes = malloc((size_t)n + 1); if (bytes == NULL || fread(bytes, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(bytes); return NULL; }
  fclose(f); *length = (size_t)n; return bytes;
}
/* QA_DUMP_DIR=dir writes checkpoints for comparison with the oracle's. */
static void dump(const char *name, bool rerelease, const uint8_t *bytes, size_t length) {
  const char *dir = getenv("QA_DUMP_DIR"); if (dir == NULL) return;
  char path[1024]; snprintf(path, sizeof path, "%s/c-%s-%s.bin", dir, rerelease ? "rerelease" : "classic", name);
  FILE *f = fopen(path, "wb"); if (f != NULL) { fwrite(bytes, 1, length, f); fclose(f); }
}
static void sha_hex(const uint8_t *bytes, size_t length, char out[65]) { qa_net_sha256_hex(bytes, length, out); }

static qa_q2_decoded_map base1; static bool base1_loaded;
static bool load_base1(void) {
  if (base1_loaded) return true;
  char pak[1024]; corpus("Quake 2/baseq2/pak0.pak", pak, sizeof pak);
  if (!exists(pak)) return false;
  qa_archive_handle *archive = qa_open_archive(pak, false, QA_ARCHIVE_FORMAT_PAK);
  const qa_archive_entry *entry = NULL;
  if (qa_archive_find_entries(archive, "maps/base1.bsp", (qa_archive_path_comparison)0, &entry, 1) == 0) { qa_archive_destroy(archive); return false; }
  size_t length = 0; uint8_t *bytes = qa_archive_read_entry_entry(archive, entry, &length);
  qa_decode_q2_map(bytes, length, "base1", NULL, &base1); free(bytes); qa_archive_destroy(archive);
  base1_loaded = true; return true;
}

/* ---------------- world fixture (the oracle's engine and services) ---------------- */
typedef struct fixture {
  qa_identity_owner identity; qa_session_actor_registry *actors; qa_actor_callback_table *callbacks; qa_shared_scene_queries *scene;
  qa_app_sim_shared_physics *physics; qa_gameplay_authority *combat; qa_shared_inventory_table *inventory;
  qa_owned_actor world; double time;
  qa_q2_foundation_host engines[2]; const qa_app_sim_actor_host_runtime *runtimes[2]; size_t engine_count;
  qa_session_mod_engine_scene engine_scene; qa_session_mod_engine_services engine; qa_mod_host_services services;
  qa_app_sim_native_mod_host_context native;
  size_t prints, events, assertions, frames;
} fixture;
static fixture *F;
static size_t engine_index(void *ctx) { CTX_OK(ctx); return 0; }
static const qa_app_sim_actor_host_runtime *runtime_of(void *ctx) { (void)engine_index(ctx); return F->runtimes[F->engine_count - 1]; }
static double e_now(void *ctx) { const qa_app_sim_actor_host_runtime *r = runtime_of(ctx); return r->now(r->ctx); }
static double e_frame_seconds(void *ctx) { const qa_app_sim_actor_host_runtime *r = runtime_of(ctx); return r->frame_seconds(r->ctx); }
static double e_gravity(void *ctx) { CTX_OK(ctx); return 800; }
static double e_random(void *ctx) { return qa_app_sim_source_random_next_unit(runtime_of(ctx)->random); }
static void e_schedule(void *ctx, qa_owned_actor actor, const double *due) { CTX_OK(ctx); (void)actor; (void)due; }
static void e_touch(void *ctx, qa_owned_actor actor) { CTX_OK(ctx); (void)actor; }
static qa_trace_result e_trace(void *ctx, qa_q2_trace_request request) { CTX_OK(ctx); return qa_app_sim_shared_physics_trace(F->physics, request, "q2"); }
static double e_contents(void *ctx, qa_vec3 point) {
  CTX_OK(ctx); qa_point_contents_query q; memset(&q, 0, sizeof q); q.point = point; q.policy.kind = "q2"; q.policy.contents_mask = -1; q.policy.leaf_contents = "merged"; q.numeric = qa_q2_donor_profile;
  qa_point_contents_result r = qa_shared_point_contents(F->scene, q); return strcmp(r.kind, "q2") == 0 ? r.merged : r.contents;
}
static bool visible(qa_vec3 a, qa_vec3 b, const char *kind) { return qa_shared_cluster_visible(F->scene, qa_shared_leaf_cluster(F->scene, qa_shared_point_leaf(F->scene, a)), qa_shared_leaf_cluster(F->scene, qa_shared_point_leaf(F->scene, b)), kind); }
static bool e_pvs(void *ctx, qa_vec3 a, qa_vec3 b) { CTX_OK(ctx); return visible(a, b, "pvs"); }
static bool e_phs(void *ctx, qa_vec3 a, qa_vec3 b) { CTX_OK(ctx); return visible(a, b, "phs"); }
static bool e_areas(void *ctx, qa_vec3 a, qa_vec3 b) { CTX_OK(ctx); (void)a; (void)b; return true; }
static const qa_actor_id *e_nearby(void *ctx, qa_vec3 origin, double radius, size_t *n) { CTX_OK(ctx); (void)origin; (void)radius; *n = 0; return NULL; }
static const qa_actor_id *e_players(void *ctx, size_t *n) { CTX_OK(ctx); *n = 0; return NULL; }
static qa_actor_id e_world(void *ctx) { CTX_OK(ctx); return F->world.id; }
static bool e_no(void *ctx, qa_actor_id actor) { CTX_OK(ctx); (void)actor; return false; }
static void e_portal(void *ctx, double portal, bool open) { CTX_OK(ctx); qa_shared_set_area_portal_state(F->scene, portal, open); }
static void e_solid(void *ctx, qa_owned_actor actor, const char *solid, const double *model) {
  CTX_OK(ctx); qa_app_sim_shared_solid value; memset(&value, 0, sizeof value); value.solid = solid; value.family = "q2";
  if (model != NULL) { value.has_model = true; value.model = *model; }
  qa_app_sim_shared_physics_set_solid(F->physics, actor, value);
}
static qa_bounds e_model_bounds(void *ctx, double model) { CTX_OK(ctx); return qa_shared_model_bounds(F->scene, model); }
static void e_motion(void *ctx, qa_q2_motion motion) { CTX_OK(ctx); (void)motion; }
static void e_emit(void *ctx, qa_q2_presentation_event event) { CTX_OK(ctx); (void)event; }
/* context.engine(source, runtime): a fresh host bound to the runtime, as the oracle's engineFor. */
static qa_q2_foundation_host *c_engine(void *ctx, qa_provider_reference source, const qa_app_sim_actor_host_runtime *runtime) {
  CTX_OK(ctx); (void)source; if (F->engine_count == 2) abort();
  F->runtimes[F->engine_count] = runtime; qa_q2_foundation_host *e = &F->engines[F->engine_count++]; memset(e, 0, sizeof *e);
  e->ctx = SENTINEL; e->actors = F->actors; e->callbacks = F->callbacks; e->bodies = qa_app_sim_shared_physics_bodies(F->physics); e->combat = F->combat; e->inventory = F->inventory;
  e->now = e_now; e->frame_seconds = e_frame_seconds; e->gravity = e_gravity; e->random = e_random; e->schedule = e_schedule; e->touch_triggers = e_touch;
  e->trace = e_trace; e->point_contents = e_contents; e->in_pvs = e_pvs; e->in_phs = e_phs; e->areas_connected = e_areas; e->nearby = e_nearby; e->players = e_players;
  e->world_actor = e_world; e->is_player = e_no; e->is_monster = e_no; e->set_area_portal = e_portal; e->set_solid = e_solid; e->inline_model_bounds = e_model_bounds;
  e->set_motion = e_motion; e->emit = e_emit;
  return e;
}
static void c_collision(void *ctx, qa_owned_actor actor, qa_actor_collision collision) { CTX_OK(ctx); qa_app_sim_shared_physics_set_collision(F->physics, actor, collision); }
static bool clock_now(void *ctx, double *out) { CTX_OK(ctx); *out = 1700000000000.0; return true; }
static bool clock_counter(void *ctx, int64_t *out) { CTX_OK(ctx); *out = 12345678; return true; }
static void *nav_runtime(void *ctx) { CTX_OK(ctx); return NULL; }
static qa_compat_q2_rerelease_goal_status nav_move(void *ctx, qa_actor_id a, qa_vec3 p, double t) { CTX_OK(ctx); (void)a; (void)p; (void)t; return 0; }
static qa_compat_q2_rerelease_goal_status nav_follow(void *ctx, qa_actor_id a, qa_actor_id b) { CTX_OK(ctx); (void)a; (void)b; return 0; }
static void s_print(void *ctx, const char *text) { CTX_OK(ctx); (void)text; F->prints++; }
static void s_emit(void *ctx, const qa_session_mod_emit *event) { CTX_OK(ctx); (void)event; F->events++; }
static void s_register(void *ctx, const qa_session_mod_resource_registration *resource) { CTX_OK(ctx); (void)resource; }
static bool s_world(void *ctx, qa_actor_id *out) { CTX_OK(ctx); *out = F->world.id; return true; }
static qa_source_time s_time(void *ctx) { CTX_OK(ctx); qa_source_time t = {QA_SOURCE_TIME_SECONDS, F->time}; return t; }
static double p_order(void *ctx, qa_actor_id a, qa_actor_id b) { CTX_OK(ctx); return (double)(a.slot - b.slot); }
static bool p_world(void *ctx, qa_actor_id *out) { CTX_OK(ctx); *out = F->world.id; return true; }
static void p_blocked(void *ctx, qa_owned_actor pusher, qa_actor_id other) { CTX_OK(ctx); (void)pusher; (void)other; }
static void assert_current(void *ctx) { CTX_OK(ctx); F->assertions++; }
static void next_frame(void *ctx) { CTX_OK(ctx); F->frames++; }
static char *localize(void *ctx, const char *key, const char *const *arguments, size_t count) { CTX_OK(ctx); (void)arguments; (void)count; return strdup(key); }

static void fixture_init(fixture *f) {
  memset(f, 0, sizeof *f); F = f; f->time = 1;
  CHECK(qa_identity_owner_create(&f->identity, "native-mod-oracle"));
  f->actors = qa_session_actor_registry_create(&f->identity, 4096); f->callbacks = qa_actor_callback_table_create(f->actors);
  qa_decoded_world world; memset(&world, 0, sizeof world); world.kind = QA_DECODED_WORLD_Q2; world.as.q2 = &base1.world;
  f->scene = qa_create_scene_queries(world);
  f->world = qa_session_actor_registry_allocate(f->actors, "q2:world-fixture", "q2:worldspawn");
  qa_app_sim_shared_physics_options physics; memset(&physics, 0, sizeof physics);
  physics.ctx = SENTINEL; physics.actors = f->actors; physics.callbacks = f->callbacks; physics.scene = f->scene; physics.numeric = qa_q2_donor_profile;
  physics.source_order = p_order; physics.world_actor = p_world; physics.on_blocked = p_blocked;
  f->physics = qa_app_sim_shared_physics_create(physics);
  qa_gameplay_hooks hooks; memset(&hooks, 0, sizeof hooks);
  f->combat = qa_gameplay_authority_create(f->actors, f->callbacks, hooks); f->inventory = qa_shared_inventory_table_create(f->actors);
  qa_app_sim_native_mod_host_context *n = &f->native;
  n->scene = f->scene; n->map_path = "maps/base1.bsp"; n->max_clients = 1; n->frame_milliseconds = 25; n->skill = 1; n->mode = "singleplayer"; n->gravity = 800;
  n->clock.ctx = SENTINEL; n->clock.now_milliseconds = clock_now; n->clock.performance_counter = clock_counter; n->clock.has_performance_frequency = true; n->clock.performance_frequency = 1000000;
  n->navigation.ctx = SENTINEL; n->navigation.runtime = nav_runtime; n->navigation.move_to_point = nav_move; n->navigation.follow_actor = nav_follow;
  n->ctx = SENTINEL; n->collision = c_collision; n->engine = c_engine;
  f->engine_scene.shared = f->scene;
  qa_session_mod_engine_services *e = &f->engine; e->ctx = SENTINEL; e->print = s_print; e->events.ctx = SENTINEL; e->events.emit = s_emit; e->events.register_resource = s_register;
  e->scene = &f->engine_scene; e->world = s_world;
  qa_mod_host_services *s = &f->services;
  s->native = n; s->actors = f->actors; s->bodies = qa_app_sim_shared_physics_bodies(f->physics); s->combat = f->combat; s->inventory = f->inventory; s->seed = 7;
  s->callbacks = f->callbacks; s->ctx = SENTINEL; s->engine = e; s->time = s_time;
}
static void fixture_free(fixture *f) {
  qa_app_sim_shared_physics_destroy(f->physics); qa_shared_scene_queries_destroy(f->scene);
  qa_shared_inventory_table_destroy(f->inventory); qa_gameplay_authority_destroy(f->combat);
  qa_actor_callback_table_destroy(f->callbacks); qa_session_actor_registry_destroy(f->actors);
}

/* The oracle's declaration (TS declarationJson). */
static char *declaration_json(bool rerelease, const char *digest) {
  const char *arguments = rerelease ? "[{\"kind\":\"uint8\",\"value\":{\"kind\":\"float\",\"value\":1}}]" : "[]";
  char call[256]; snprintf(call, sizeof call, "\"entry\":{\"kind\":\"game-export\",\"name\":\"RunFrame\"},\"arguments\":%s,\"globals\":[],\"returns\":\"void\"", arguments);
  const char *target = rerelease
    ? "{\"api\":{\"kind\":\"q2-rerelease-game\",\"version\":2023},\"abi\":{\"kind\":\"windows-x86-64\",\"image\":\"pe32+\",\"pointerBytes\":8,\"call\":\"microsoft-x64\"}}"
    : "{\"api\":{\"kind\":\"q2-classic-game\",\"version\":3},\"abi\":{\"kind\":\"windows-i386\",\"image\":\"pe32\",\"pointerBytes\":4,\"call\":\"cdecl\"}}";
  char *json = malloc(4096);
  snprintf(json, 4096, "{\"version\":1,\"runtime\":\"native\",\"program\":{\"path\":\"%s\",\"digest\":\"%s\"},\"target\":%s,"
    "\"cvars\":[{\"name\":\"fixture_flag\",\"value\":\"1\"},{\"name\":\"skill\",\"value\":\"3\"}],"
    "\"spawnEntities\":\"{\\n\\\"classname\\\" \\\"worldspawn\\\"\\n\\\"message\\\" \\\"Native Fixture\\\"\\n}\\n\","
    "\"actorRecords\":[],\"entityRecord\":null,\"initialize\":[{%s}],\"project\":[],\"release\":[],"
    "\"callbacks\":[{\"id\":\"fixture:frame\",\"operation\":\"actor.think\",\"stage\":\"observe\",%s}]}",
    rerelease ? "game_x64.dll" : "gamex86.dll", digest, target, call, call);
  return json;
}

typedef struct expected {
  const char *base; double stride, count, capacity; const char *image, *run_frame; const char *cvars[8]; const char *world, *client1;
  const char *checkpoint; size_t checkpoint_length; const char *restored; size_t prints;
} expected;
static const char *const cvar_names[8] = {"maxclients", "skill", "deathmatch", "coop", "sv_gravity", "fixture_flag", "gamename", "dmflags"};
/* bun oracle (TS native-mod-host.ts + NativeModProvider), regenerated 2026-11-03 from TS HEAD: TS 2a2cb7bd
 * (pooled guest pages, guest/core/memory.ts) moved the guest heap layout, so table and client
 * addresses, the world entity bytes and the checkpoints changed. */
static const expected classic_expected = {"57b000", 892, 11, 1024, "20000000", "20014240", {"1", "3", "1", "0", "800", "1", "baseq2", "0"},
  "32f9b1a4b2799d5254bd9fa456e7d5615abb6329288955b14ab8a179ca318643", "65a000",
  "17d57f4ee43c11bb290b0cbe2915086b8492cb8f54cfa8385ebfd783b11f3fad", 45926, "7a61025330e04daba83b1fb0a9dc6ea01414ec1a968fac80e667a50505d2a349", 6};
static const expected rerelease_expected = {"fa0000", 3688, 10, 8192, "280000000", "28006e1d0", {"1", "3", "0", "0", "800", "1", "baseq2", NULL},
  "3ea5d67810bd25f81b9c02ef1ede30fdb112a3223fddb6cde1a92824df9b6788", "2c70000",
  "1ba213f69f492505bd342f4c8055a4f639d39f29808a5f89945b2528d45f4163", 40424, "1ba213f69f492505bd342f4c8055a4f639d39f29808a5f89945b2528d45f4163", 7};
static bool hex_is(qa_guest_address address, const char *hex) { char text[32]; snprintf(text, sizeof text, "%llx", (unsigned long long)address.byte_offset); return strcmp(text, hex) == 0; }
static void report_hex(const char *label, qa_guest_address address) { fprintf(stderr, "  %s %llx\n", label, (unsigned long long)address.byte_offset); }

typedef struct retail { bool rerelease; uint8_t *program; size_t length; char digest[80]; qa_native_mod_declaration *declaration; qa_resolved_resource_reference artifact; } retail;
static bool load_retail(retail *r, bool rerelease) {
  memset(r, 0, sizeof *r); r->rerelease = rerelease;
  char path[1024]; corpus(rerelease ? "Quake 2/rerelease/baseq2/game_x64.dll" : "Quake 2/ctf/gamex86.dll", path, sizeof path);
  if (!exists(path) || !load_base1()) return false;
  r->program = read_file(path, &r->length); if (r->program == NULL) return false;
  char hex[65]; sha_hex(r->program, r->length, hex); snprintf(r->digest, sizeof r->digest, "sha256:%s", hex);
  char *json = declaration_json(rerelease, r->digest);
  r->declaration = qa_read_native_mod_callbacks((const uint8_t *)json, strlen(json)); free(json);
  r->artifact.id = "resource:fixture"; r->artifact.requested_path = rerelease ? "game_x64.dll" : "gamex86.dll"; r->artifact.digest = r->digest;
  return r->declaration != NULL;
}
static void free_retail(retail *r) { qa_native_mod_declaration_free(r->declaration); free(r->program); }

/* NativeModProjection over the live provider, with the sentinel ctx the host must forward. */
static qa_compat_q2_native_mod_provider *live_provider;
static bool j_project(void *ctx, qa_raw_entity_view record, qa_owned_actor *out) { CTX_OK(ctx); return qa_compat_q2_native_mod_provider_project(live_provider, record, out); }
static bool j_actor_at(void *ctx, double slot, qa_actor_id *out) { CTX_OK(ctx); return qa_compat_q2_native_mod_provider_actor_at(live_provider, slot, out); }
static bool j_slot_of(void *ctx, qa_actor_id actor, double *out) { CTX_OK(ctx); return qa_compat_q2_native_mod_provider_slot_of(live_provider, actor, out); }
static bool j_accepts(void *ctx, double slot) { CTX_OK(ctx); return qa_compat_q2_native_mod_provider_accepts_client(live_provider, slot); }
static qa_guest_address j_address(void *ctx, qa_actor_id actor) { CTX_OK(ctx); return qa_compat_q2_native_mod_provider_address(live_provider, actor); }
static size_t imports_seen;
static qa_guest_call_result j_import(void *ctx, const char *name, const qa_guest_call_value *values, size_t count, void *invoke_ctx,
                                     qa_guest_call_result (*invoke)(void *, const qa_guest_call_value *, size_t)) {
  CTX_OK(ctx); imports_seen++; return qa_compat_q2_native_mod_provider_import_boundary(live_provider, name, values, count, invoke_ctx, invoke);
}

/* oracle "host" mode: createNativeModHost + NativeModProvider by hand. */
static void run_host(retail *r, const expected *want) {
  fixture f; fixture_init(&f);
  qa_compat_q2_native_mod_provider *provider = qa_compat_q2_native_mod_provider_create(r->declaration, &f.services, "mod:fixture-native", f.native.map_path, SENTINEL, assert_current);
  qa_compat_q2_native_mod_provider_reserve_protection(provider);
  qa_provider_reference source = {"mod:fixture-native", "q2:fixture-native"};
  qa_resolved_execution_module execution; memset(&execution, 0, sizeof execution);
  execution.kind = QA_EXECUTION_MODULE_NATIVE; execution.owner = source; execution.role = "server-game"; execution.api.kind = QA_GAME_API_Q2_GAME;
  execution.api.as.q2_game = r->declaration->target.api; execution.profile = r->declaration->target.abi; execution.artifact = &r->artifact;
  qa_app_sim_prepared_classic_guest classic; memset(&classic, 0, sizeof classic);
  qa_app_sim_prepared_rerelease_guest rerelease; memset(&rerelease, 0, sizeof rerelease);
  uint8_t *bytes = malloc(r->length); memcpy(bytes, r->program, r->length);
  qa_app_sim_native_mod_host_options options; memset(&options, 0, sizeof options);
  if (r->rerelease) { rerelease.edition = "rerelease"; rerelease.execution = execution; rerelease.bytes = bytes; rerelease.length = r->length; options.rerelease = &rerelease; }
  else { classic.edition = "classic"; classic.execution = execution; classic.bytes = bytes; classic.length = r->length; options.classic = &classic; }
  options.declaration = r->declaration; options.source = source; options.context = &f.native; options.services = &f.services;
  live_provider = provider;
  options.projection.ctx = SENTINEL; options.projection.project = j_project; options.projection.actor_at = j_actor_at; options.projection.slot_of = j_slot_of;
  options.projection.accepts_client = j_accepts; options.projection.address = j_address; options.projection.import_boundary = j_import;
  options.ctx = SENTINEL; options.localize = localize; options.next_frame = next_frame;
  qa_compat_q2_native_mod_host *host = qa_app_sim_create_native_mod_host(&options);
  qa_compat_q2_native_mod_provider_attach(provider, host);
  qa_compat_q2_native_mod_provider_initialize(provider, false);
  qa_compat_q2_native_mod_entities table = host->entities(host->ctx);
  CHECK(hex_is(table.base, want->base) && table.stride == want->stride && table.count == want->count && table.capacity == want->capacity);
  if (!hex_is(table.base, want->base) || table.count != want->count) { report_hex("entities", table.base); fprintf(stderr, "  stride %g count %g capacity %g\n", table.stride, table.count, table.capacity); }
  CHECK(hex_is(host->image_base, want->image));
  qa_compat_q2_native_mod_game_entry run_frame = host->game_entry(host->ctx, "RunFrame");
  CHECK(hex_is(run_frame.address, want->run_frame)); if (!hex_is(run_frame.address, want->run_frame)) report_hex("RunFrame", run_frame.address);
  qa_cvar_registry *cvars = qa_app_sim_native_mod_host_cvars(host);
  for (size_t i = 0; i < 8; i++) {
    qa_cvar_read read; bool found = qa_cvar_registry_find(cvars, cvar_names[i], &read);
    CHECK(want->cvars[i] == NULL ? !found : found && strcmp(read.value, want->cvars[i]) == 0);
    if (found) { if (want->cvars[i] == NULL || strcmp(read.value, want->cvars[i]) != 0) fprintf(stderr, "  cvar %s=%s\n", cvar_names[i], read.value); qa_cvar_read_free(&read); }
  }
  qa_raw_entity_view world = host->entity(host->ctx, 0); char hex[65]; sha_hex(world.bytes.bytes, world.bytes.length, hex);
  CHECK(strcmp(hex, want->world) == 0); CHECK(host->active(host->ctx, 0));
  qa_guest_address client; CHECK(host->client(host->ctx, 1, &client) && hex_is(client, want->client1));
  CHECK(host->weapon_model(host->ctx, 1) == NULL);
  qa_compat_q2_native_pickup_inputs none = {NULL, 0}; double invoked = -1;
  CHECK(qa_compat_q2_native_mod_provider_invoke(provider, &r->declaration->callbacks[0].call, none, &invoked) && invoked == 0);
  world = host->entity(host->ctx, 0); sha_hex(world.bytes.bytes, world.bytes.length, hex); CHECK(strcmp(hex, want->world) == 0);
  qa_provider_checkpoint state = qa_compat_q2_native_mod_provider_checkpoint(provider);
  dump("host-checkpoint", r->rerelease, state.bytes.bytes, state.bytes.length);
  sha_hex(state.bytes.bytes, state.bytes.length, hex);
  CHECK(strcmp(hex, want->checkpoint) == 0 && state.bytes.length == want->checkpoint_length);
  if (strcmp(hex, want->checkpoint) != 0) fprintf(stderr, "  checkpoint %s length %zu\n", hex, state.bytes.length);
  f.time = 2; host->synchronize_frame(host->ctx, 2, 20);
  qa_compat_q2_native_mod_provider_restore(provider, &state);
  qa_provider_checkpoint again = qa_compat_q2_native_mod_provider_checkpoint(provider);
  sha_hex(again.bytes.bytes, again.bytes.length, hex); CHECK(strcmp(hex, want->restored) == 0);
  if (strcmp(hex, want->restored) != 0) fprintf(stderr, "  restored %s length %zu\n", hex, again.bytes.length);
  CHECK(f.events == 0 && f.prints == want->prints && imports_seen > 0);
  if (f.prints != want->prints) fprintf(stderr, "  prints %zu\n", f.prints);
  free((void *)state.bytes.bytes); free((void *)again.bytes.bytes);
  qa_compat_q2_native_mod_provider_close(provider);
  qa_compat_q2_native_mod_provider_destroy(provider); qa_app_sim_native_mod_host_destroy(host);
  free(bytes); fixture_free(&f);
}

/* oracle "prepared" mode: prepareNativeMod().initialize() and its runtime. */
static void run_prepared(retail *r, const char *checkpoint, size_t length, const char *restored, size_t prints) {
  fixture f; fixture_init(&f);
  qa_mod_description description; memset(&description, 0, sizeof description);
  description.selection.product = "q2-fixture"; description.selection.id = "native";
  description.source.provider = "q2:fixture-source"; description.source.content = "q2:fixture-native";
  description.title = "Fixture"; description.source_title = "Fixture"; description.purpose = "addition"; description.is_available = true;
  qa_app_sim_prepare_native_mod_options options; memset(&options, 0, sizeof options);
  options.description = description; options.declaration = r->declaration; options.declaration_digest = "sha256:cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd";
  options.program = r->program; options.program_length = r->length; options.artifact = &r->artifact; options.ctx = SENTINEL; options.localize = localize;
  qa_prepared_mod prepared = qa_app_sim_prepare_native_mod(&options);
  CHECK(strcmp(prepared.module_checkpoint, "provider") == 0 && !prepared.has_client_presentation);
  CHECK(prepared.identity.module_count == 1 && strcmp(prepared.identity.modules[0].id, "mod:q2-fixture%2Fnative") == 0);
  CHECK(strcmp(prepared.identity.modules[0].artifact_path, r->rerelease ? "game_x64.dll" : "gamex86.dll") == 0);
  CHECK(strcmp(prepared.identity.modules[0].digest, r->digest) == 0 && strcmp(prepared.identity.modules[0].revision, r->digest) == 0);
  CHECK(prepared.identity.provider_count == 1 && strcmp(prepared.identity.providers[0].provider, "mod:q2-fixture%2Fnative") == 0
        && strcmp(prepared.identity.providers[0].schema, "native:mod") == 0 && prepared.identity.providers[0].version == 1);
  CHECK(strcmp(prepared.identity.source.provider, "q2:fixture-source") == 0);
  qa_resource_scope *resources = qa_resource_scope_create("fixture");
  qa_mod_initialization context; memset(&context, 0, sizeof context);
  context.services = &f.services; context.instance = "mod:q2-fixture%2Fnative"; context.resources = resources; context.ctx = SENTINEL;
  context.next_frame = next_frame; context.assert_current = assert_current;
  qa_mod_runtime runtime = prepared.initialize(prepared.ctx, &context);
  qa_mod_private_checkpoint state = runtime.checkpoint(runtime.ctx);
  CHECK(state.guest_count == 0 && state.provider_count == 1);
  char hex[65]; sha_hex(state.providers[0].bytes.bytes, state.providers[0].bytes.length, hex);
  CHECK(strcmp(hex, checkpoint) == 0 && state.providers[0].bytes.length == length);
  if (strcmp(hex, checkpoint) != 0) fprintf(stderr, "  prepared checkpoint %s length %zu\n", hex, state.providers[0].bytes.length);
  prepared.validate_state(prepared.ctx, state);
  /* The runtime's checkpoint storage is replaced by the next checkpoint; keep a copy to restore from. */
  qa_provider_checkpoint copy = state.providers[0]; uint8_t *bytes = malloc(copy.bytes.length); memcpy(bytes, copy.bytes.bytes, copy.bytes.length); copy.bytes.bytes = bytes;
  qa_mod_private_checkpoint saved = {NULL, 0, &copy, 1};
  runtime.restore(runtime.ctx, saved);
  qa_mod_private_checkpoint again = runtime.checkpoint(runtime.ctx);
  sha_hex(again.providers[0].bytes.bytes, again.providers[0].bytes.length, hex); CHECK(strcmp(hex, restored) == 0);
  if (strcmp(hex, restored) != 0) fprintf(stderr, "  prepared restored %s\n", hex);
  qa_mod_private_checkpoint empty = {NULL, 0, NULL, 0};
  CHECK_ABORTS(prepared.validate_state(prepared.ctx, empty)); /* Missing native gameplay mod checkpoint */
  free(bytes);
  runtime.close(runtime.ctx); qa_resource_scope_close(resources); qa_resource_scope_destroy(resources);
  /* The oracle counts prints after close (ShutdownGame prints). */
  CHECK(f.prints == prints); if (f.prints != prints) fprintf(stderr, "  prepared prints %zu\n", f.prints);
  qa_app_sim_prepared_native_mod_free(&prepared);
  fixture_free(&f);
}
static void test_prepare_rejects(retail *r) {
  qa_app_sim_prepare_native_mod_options options; memset(&options, 0, sizeof options);
  options.description.selection.product = "q2-fixture"; options.description.selection.id = "native";
  options.declaration = r->declaration; options.declaration_digest = "sha256:cd"; options.program = r->program; options.program_length = r->length;
  options.artifact = &r->artifact; options.ctx = SENTINEL; options.localize = localize;
  qa_resolved_resource_reference moved = r->artifact; moved.requested_path = "other.dll";
  options.artifact = &moved; CHECK_ABORTS((void)qa_app_sim_prepare_native_mod(&options)); /* Native mod artifact or ABI differs */
  options.artifact = &r->artifact; options.program_length = r->length - 1;
  CHECK_ABORTS((void)qa_app_sim_prepare_native_mod(&options)); /* program digest differs */
}

int main(int argc, char **argv) {
  (void)argc; (void)argv;
  const char *root = getenv("QA_CORPUS"); const char *home = getenv("HOME");
  snprintf(corpus_root, sizeof corpus_root, "%s", root != NULL ? root : home != NULL ? home : ".");
  if (root == NULL) { size_t n = strlen(corpus_root); snprintf(corpus_root + n, sizeof corpus_root - n, "/qfiles"); }
  for (int edition = 0; edition < 2; edition++) {
    retail r; if (!load_retail(&r, edition == 1)) { printf("skip: %s DLL or base1 absent\n", edition == 1 ? "rerelease" : "classic"); continue; }
    run_host(&r, edition == 1 ? &rerelease_expected : &classic_expected);
    if (edition == 0) run_prepared(&r, "3f2ac073bde1ef141673dcc66bd734a9489f9253228e693c74875cb271c55ee1", 45944, "49127d657a21b74b7bd4db6d76aaebb66d123e3abc51f983a60afaa29a31c481", 7);
    else run_prepared(&r, "0b4f6e7916de1c444a935790a53bca4d1bfd3b3fa3aae398191b612b4abb7b5c", 40429, "0b4f6e7916de1c444a935790a53bca4d1bfd3b3fa3aae398191b612b4abb7b5c", 8);
    test_prepare_rejects(&r);
    free_retail(&r);
  }
  if (base1_loaded) qa_q2_decoded_map_destroy(&base1);
  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
