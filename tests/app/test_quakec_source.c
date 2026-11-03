/* C checks for src/app/bootstrap/simulation/quakec-source.ts. TS tests
 * import only prepareQuakeCResources (tests/compat/qc/mod-*.test.ts) and
 * spy on QuakeCSource.admitClient (tests/network/q1/application.test.ts);
 * both are reproduced here over retail id1 data, together with an e1m1
 * run compared against the TS QuakeCSource on the same map under bun
 * (oracle script: the values below were produced by it; see the report).
 * Every callback checks a sentinel ctx. GPL-2.0-or-later. */
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/app/bootstrap/simulation/events.h"
#include "qa/app/bootstrap/simulation/physics.h"
#include "qa/app/bootstrap/simulation/quakec_source.h"
#include "qa/app/bootstrap/simulation/random.h"
#include "qa/compat/qc/machine.h"
#include "qa/content/mounts.h"
#include "qa/contracts/identity.h"
#include "qa/core/numeric.h"
#include "qa/formats/q1_map.h"
#include "qa/foundation/support.h"
#include "qa/world/actors/callbacks.h"
#include "qa/world/actors/registry.h"
#include "qa/world/collision/index.h"
#include "qa/world/gameplay/authority.h"
#include "qa/world/gameplay/inventory.h"
#include "qa/world/scheduler.h"

static int checks, failures;
#define CHECK(...) do { checks++; if (!(__VA_ARGS__)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #__VA_ARGS__); } } while (0)

typedef struct sentinel { int magic; } sentinel;
static sentinel SENTINEL = {0x51C0};
static int bad_ctx;
static void expect_ctx(void *ctx) { if (ctx != &SENTINEL) bad_ctx++; }

/* ---- host leaves ---- */
static double now_seconds = 1;
static size_t admits, emits, prints, changes;
static void h_admit(void *ctx, qa_owned_actor actor, int64_t slot, qa_app_sim_quakec_source *source) {
  expect_ctx(ctx); (void)actor; (void)slot; (void)source; admits++;
}
static qa_damage_request h_damage_request(void *ctx, qa_q1_id1_damage_call call) {
  expect_ctx(ctx); (void)call;
  qa_raise("Error", 0, 0, "no damage expected");
}
static void h_print(void *ctx, const char *text) { expect_ctx(ctx); prints++; fprintf(stderr, "print: %s", text); }
static void h_change_level(void *ctx, const char *map) { expect_ctx(ctx); (void)map; changes++; }
static void *h_run_source(void *ctx, qa_original_pickup_offer offer, void *(*execute)(void *, qa_source_pickup_selection, qa_source_pickup_lifetime), void *execute_ctx) {
  expect_ctx(ctx); (void)offer; (void)execute; (void)execute_ctx;
  qa_raise("Error", 0, 0, "no pickups expected");
}
static qa_original_pickup_outcome h_touch(void *ctx, qa_original_pickup_offer offer, qa_original_pickup_continuation continuation) {
  expect_ctx(ctx); (void)offer; (void)continuation;
  qa_raise("Error", 0, 0, "no pickups expected");
}
static qa_source_time e_now(void *ctx) { expect_ctx(ctx); return (qa_source_time){QA_SOURCE_TIME_SECONDS, now_seconds}; }
static bool has_player_client; static qa_actor_id player_id; static qa_client_id player_client;
static bool e_client_for(void *ctx, qa_actor_id actor, qa_client_id *out) {
  expect_ctx(ctx);
  if (!has_player_client || !qa_same_actor(actor, player_id)) return false;
  *out = player_client;
  return true;
}
static bool e_source_slot(void *ctx, qa_actor_id actor, double *out) { expect_ctx(ctx); (void)actor; (void)out; return false; }
static bool e_resource_for(void *ctx, qa_content_id content, const char *path, qa_resource_id *out) { expect_ctx(ctx); (void)content; (void)path; (void)out; return false; }
/* Hosts copy qa_q1_event.text at emit time: the text can live in a QC string ring. */
static char emitted_text[256];
static void e_emit_super(void *ctx, bool has_owner, qa_presentation_owner owner, qa_content_id content, qa_app_sim_source_event_input source,
                         qa_source_time time, bool has_recipient, qa_actor_id recipient, qa_app_sim_presentation_event *out) {
  expect_ctx(ctx); (void)has_owner; (void)owner; (void)content; (void)time; (void)has_recipient; (void)recipient;
  if (source.q1 && source.q1->text) (void)snprintf(emitted_text, sizeof emitted_text, "%s", source.q1->text);
  out->sequence = (double)emits++;
}
static void e_register(void *ctx, qa_content_id content, const char *path, qa_resolved_resource_reference resource) {
  expect_ctx(ctx); (void)content; (void)path; (void)resource;
}
static const char *e_light_style(void *ctx, double style) { expect_ctx(ctx); (void)style; return ""; }
static double p_order(void *ctx, qa_actor_id a, qa_actor_id b) { expect_ctx(ctx); return (double)(a.slot - b.slot); }
static qa_app_sim_quakec_source *world_source;
static bool p_world(void *ctx, qa_actor_id *out) {
  expect_ctx(ctx);
  if (!world_source) return false;
  *out = qa_app_sim_quakec_source_world_actor(world_source).id;
  return true;
}
static void p_blocked(void *ctx, qa_owned_actor pusher, qa_actor_id other) { expect_ctx(ctx); (void)pusher; (void)other; }

/* ---- retail content ---- */
static uint32_t fnv(const uint8_t *bytes, size_t length, uint32_t h) {
  for (size_t i = 0; i < length; i++) { h ^= bytes[i]; h *= 0x01000193u; }
  return h;
}
static uint32_t fnv_text(const char *text, uint32_t h) { return fnv((const uint8_t *)text, strlen(text), h); }

typedef struct fixture {
  char pak0[512], pak1[512]; char *digest0, *digest1;
  qa_content_mount mounts[2]; qa_mount_id order[2]; qa_resolved_mount_plan plan;
  qa_content_mounted *mounted;
  qa_resolved_resource_reference artifact; const qa_resolved_resource_reference *progs_ref;
  qa_execution_module execution;
  qa_q1_map *map; qa_decoded_world world;
  qa_app_sim_prepared_quakec_source prepared;
  qa_identity_owner identity; qa_session_actor_registry *actors; qa_actor_callback_table *callbacks;
  qa_shared_scene_queries *scene; qa_app_sim_shared_physics *physics; qa_gameplay_authority *combat;
  qa_shared_inventory_table *inventory; qa_app_sim_simulation_events *events; qa_app_sim_source_random *random;
  qa_executable_recipe recipe; qa_app_sim_quakec_source *source;
  char *owned[8];
} fixture;

static bool open_fixture(fixture *f) {
  memset(f, 0, sizeof *f);
  const char *home = getenv("HOME") ? getenv("HOME") : "";
  (void)snprintf(f->pak0, sizeof f->pak0, "%s/qfiles/Quake/Id1/PAK0.PAK", home);
  (void)snprintf(f->pak1, sizeof f->pak1, "%s/qfiles/Quake/Id1/PAK1.PAK", home);
  f->digest0 = qa_digest_file(f->pak0); f->digest1 = qa_digest_file(f->pak1);
  if (!f->digest0 || !f->digest1) { printf("skipped: no id1 PAK0/PAK1 under ~/qfiles/Quake/Id1\n"); return false; }
  const char *ids[2] = {"mount:id1:pak1", "mount:id1:pak0"}, *paths[2] = {f->pak1, f->pak0}; char *digests[2] = {f->digest1, f->digest0};
  for (int i = 0; i < 2; i++) {
    f->mounts[i].kind = QA_CONTENT_MOUNT_ARCHIVE;
    f->mounts[i].as.archive.identity = qa_create_mount_identity(ids[i], "q1:classic:id1:installed", 0);
    f->mounts[i].as.archive.format = QA_ARCHIVE_FORMAT_PAK;
    f->mounts[i].as.archive.archive_path = paths[i];
    f->mounts[i].as.archive.archive_digest = digests[i];
    f->order[i] = ids[i];
  }
  f->plan.id = "mount-plan:id1:test"; f->plan.mounts = f->mounts; f->plan.mount_count = 2;
  f->plan.default_order = f->order; f->plan.default_order_count = 2;
  f->mounted = qa_open_mount_plan(&f->plan, NULL);
  CHECK(f->mounted != NULL);
  CHECK(qa_content_mounted_resolve(f->mounted, "progs.dat", &f->progs_ref));
  /* Own the strings: the mounter replaces its record storage on later reads. */
  f->artifact = *f->progs_ref;
  f->artifact.id = f->owned[0] = strdup(f->progs_ref->id);
  f->artifact.requested_path = f->owned[1] = strdup(f->progs_ref->requested_path);
  f->artifact.digest = f->owned[2] = strdup(f->progs_ref->digest);
  f->artifact.provenance.member_path = f->owned[3] = strdup(f->progs_ref->provenance.member_path);
  f->execution.kind = QA_EXECUTION_MODULE_QUAKEC;
  f->execution.owner = (qa_provider_reference){"q1:official", "q1:classic:id1:installed"};
  f->execution.role = "server-game";
  f->execution.api.kind = QA_GAME_API_QUAKEC;
  f->execution.api.as.quakec = (qa_quakec_api_identity){QA_QUAKEC_API_Q1_NETQUAKE, 6, 5927};
  f->execution.artifact = &f->artifact;
  size_t length = 0;
  uint8_t *bsp = qa_content_mounted_read_path(f->mounted, "maps/e1m1.bsp", &length);
  CHECK(bsp != NULL);
  f->map = calloc(1, sizeof *f->map);
  qa_q1_map_options options; memset(&options, 0, sizeof options); options.source = "maps/e1m1.bsp";
  qa_q1_read_bsp(bsp, length, &options, f->map);
  free(bsp);
  f->world = (qa_decoded_world){QA_DECODED_WORLD_Q1, {.q1 = &f->map->world}};
  return true;
}

static qa_frame_context frame_at(int index) {
  qa_frame_context frame; memset(&frame, 0, sizeof frame);
  frame.frame = index;
  frame.time = (qa_source_time){QA_SOURCE_TIME_SECONDS, 1 + index * 0.1};
  frame.elapsed = (qa_source_time){QA_SOURCE_TIME_SECONDS, 0.1};
  frame.phase = QA_FRAME_PHASE_FRAME_ENTRY;
  return frame;
}
static size_t live_count(fixture *f) {
  size_t n = 0;
  qa_compat_qc_entity_memory *entities = qa_app_sim_quakec_source_entities(f->source);
  for (int64_t slot = 0; slot < qa_compat_qc_entity_memory_get_count(entities); slot++) {
    qa_owned_actor actor;
    if (qa_source_actor_slots_at(qa_app_sim_quakec_source_slots(f->source), slot, &actor)) n++;
  }
  return n;
}
static uint32_t entity_hash(fixture *f) {
  qa_compat_qc_entity_memory *e = qa_app_sim_quakec_source_entities(f->source);
  return fnv(e->bytes, (size_t)(e->layout.stride_bytes * qa_compat_qc_entity_memory_get_count(e)), 0x811c9dc5u);
}
static uint32_t globals_hash(fixture *f) {
  qa_compat_qc_machine *m = qa_app_sim_quakec_source_machine(f->source);
  return fnv(m->globals.bytes, m->globals.byte_length, 0x811c9dc5u);
}

/* qa_try thunks for TS throws */
typedef struct try_ctx { fixture *f; int which; qa_client_id client; } try_ctx;
static void try_body(void *arg) {
  try_ctx *t = arg;
  if (t->which == 0) qa_app_sim_quakec_source_spawn_map(t->f->source);
  else if (t->which == 1) (void)qa_app_sim_quakec_source_checkpoint(t->f->source);
  else if (t->which == 2) (void)qa_app_sim_quakec_source_reserved_client(t->f->source, t->client);
  else if (t->which == 3) {
    qa_frame_context frame = frame_at(1); frame.time.kind = QA_SOURCE_TIME_MILLISECONDS;
    qa_app_sim_quakec_source_begin_frame(t->f->source, frame);
  }
}
static bool raises(fixture *f, int which, qa_client_id client, const char *message) {
  try_ctx t = {f, which, client};
  qa_caught_error caught;
  if (qa_try(try_body, &t, &caught)) return false;
  if (strcmp(caught.message, message) != 0) { fprintf(stderr, "caught: %s\n", caught.message); return false; }
  return true;
}

int main(void) {
  fixture f;
  if (!open_fixture(&f)) return 0;

  /* prepareQuakeCSource / prepareQuakeCResources over the retail mounts. */
  qa_app_sim_prepare_quakec_source_mounted(f.execution, f.mounted, f.map->world.entities, NULL, &f.prepared);
  CHECK(strcmp(f.prepared.kind, "netquake") == 0);
  CHECK(strcmp(f.prepared.message_dialect, "known-retail") == 0);
  CHECK(f.prepared.combat_declaration == NULL && f.prepared.weapon_declaration == NULL && !f.prepared.has_teams);
  CHECK(f.prepared.resources.count == 358); /* TS: prepared.resources.size */
  const qa_app_sim_quakec_resource *player = qa_app_sim_quakec_resources_get(&f.prepared.resources, "progs/player.mdl");
  CHECK(player && player->has_model_bounds && player->model_bounds.min.x == -16 && player->model_bounds.max.z == 16);
  const qa_app_sim_quakec_resource *sound = qa_app_sim_quakec_resources_get(&f.prepared.resources, "misc/h2ohit1.wav");
  CHECK(sound && !sound->has_model_bounds && strcmp(sound->resource.requested_path, "sound/misc/h2ohit1.wav") == 0);
  const qa_app_sim_quakec_resource *box = qa_app_sim_quakec_resources_get(&f.prepared.resources, "maps/b_bh25.bsp");
  CHECK(box && box->has_model_bounds && box->model_bounds.max.x > box->model_bounds.min.x);

  /* The source over shared collaborators. */
  CHECK(qa_identity_owner_create(&f.identity, "qc-e1m1"));
  f.actors = qa_session_actor_registry_create(&f.identity, 4096);
  f.callbacks = qa_actor_callback_table_create(f.actors);
  f.scene = qa_create_scene_queries(f.world);
  qa_app_sim_shared_physics_options po; memset(&po, 0, sizeof po);
  po.ctx = &SENTINEL; po.actors = f.actors; po.callbacks = f.callbacks; po.scene = f.scene; po.numeric = qa_q1_donor_profile;
  po.source_order = p_order; po.world_actor = p_world; po.on_blocked = p_blocked;
  f.physics = qa_app_sim_shared_physics_create(po);
  qa_gameplay_hooks hooks; memset(&hooks, 0, sizeof hooks);
  f.combat = qa_gameplay_authority_create(f.actors, f.callbacks, hooks);
  f.inventory = qa_shared_inventory_table_create(f.actors);
  qa_app_sim_simulation_events_options eo; memset(&eo, 0, sizeof eo);
  eo.ctx = &SENTINEL; eo.bodies = qa_app_sim_shared_physics_bodies(f.physics); eo.now = e_now; eo.client_for = e_client_for;
  eo.source_slot = e_source_slot; eo.resource_for = e_resource_for; eo.emit_super = e_emit_super;
  eo.register_resource_super = e_register; eo.light_style_super = e_light_style;
  f.events = qa_app_sim_simulation_events_create(eo);
  f.random = qa_app_sim_source_random_create(17, "classic");
  memset(&f.recipe, 0, sizeof f.recipe);
  f.recipe.execution = &f.execution; f.recipe.execution_count = 1;
  const qa_resolved_resource_reference *geometry = NULL;
  CHECK(qa_content_mounted_resolve(f.mounted, "maps/e1m1.bsp", &geometry));
  f.recipe.map.geometry = *geometry;
  f.recipe.map.geometry.id = f.owned[4] = strdup(geometry->id);
  f.recipe.map.geometry.requested_path = f.owned[5] = strdup(geometry->requested_path);
  f.recipe.map.geometry.digest = f.owned[6] = strdup(geometry->digest);
  f.recipe.map.geometry.provenance.member_path = f.owned[7] = strdup(geometry->provenance.member_path);
  f.recipe.engine_behavior.content = "q1:classic:id1:installed";
  f.recipe.character.definition.provider = "q1:official";
  qa_app_sim_quakec_source_options so; memset(&so, 0, sizeof so);
  so.ctx = &SENTINEL; so.recipe = &f.recipe; so.world = f.world; so.entity_text = f.map->world.entities;
  so.scene = f.scene; so.actors = f.actors; so.callbacks = f.callbacks; so.physics = f.physics; so.combat = f.combat;
  so.inventory = f.inventory; so.pickups = (qa_original_pickup_admission){&SENTINEL, h_run_source, h_touch};
  so.events = f.events; so.random = f.random; so.skill = 1; so.mode = "singleplayer"; so.max_clients = 1;
  so.initial_source_time_seconds = 1; so.admit = h_admit; so.damage_request = h_damage_request;
  so.print = h_print; so.change_level = h_change_level;
  f.source = qa_app_sim_quakec_source_create(&f.prepared, so);
  world_source = f.source;
  CHECK(qa_app_sim_quakec_source_loading(f.source));
  CHECK(strcmp(qa_app_sim_quakec_source_kind(f.source), "netquake") == 0);
  CHECK(qa_app_sim_quakec_source_reserved_client_slots(f.source) == 1);
  qa_client_id early = qa_identity_owner_client(&f.identity, 0, 1);
  CHECK(raises(&f, 2, early, "QC reserved client is unavailable")); /* still spawning */
  CHECK(raises(&f, 1, early, "QC save requires a completed source frame"));

  qa_app_sim_quakec_source_spawn_map(f.source);
  CHECK(!qa_app_sim_quakec_source_loading(f.source));
  CHECK(raises(&f, 0, early, "QC map was already spawned"));
  CHECK(raises(&f, 3, early, "QC frame requires source seconds"));
  CHECK(admits == 376); /* TS admitsAtSpawn */
  CHECK(prints == 0);

  /* Precache tables, in index order (TS precacheNames). */
  size_t model_count = 0, sound_count = 0;
  const char *const *models = qa_app_sim_quakec_source_precache_names(f.source, "model", &model_count);
  uint32_t model_hash = 0x811c9dc5u;
  for (size_t i = 0; i < model_count; i++) { model_hash = fnv_text(models[i], model_hash); model_hash = fnv_text("\n", model_hash); }
  CHECK(model_count == 100); /* TS modelsAtSpawn */
  if (model_count == 100) {
    CHECK(strcmp(models[0], "maps/e1m1.bsp") == 0 && strcmp(models[57], "*57") == 0 && strcmp(models[58], "progs/player.mdl") == 0);
    CHECK(strcmp(models[99], "maps/b_explob.bsp") == 0);
  }
  const char *const *sounds = qa_app_sim_quakec_source_precache_names(f.source, "sound", &sound_count);
  CHECK(sound_count == 103); /* TS soundsAtSpawn */
  if (sound_count == 103) {
    CHECK(strcmp(sounds[0], "weapons/r_exp3.wav") == 0 && strcmp(sounds[13], "misc/h2ohit1.wav") == 0);
    CHECK(strcmp(sounds[sound_count - 1], "ambience/drone6.wav") == 0);
  }

  /* Spawn order: classname per edict after spawnMap. */
  qa_compat_qc_entity_memory *entities = qa_app_sim_quakec_source_entities(f.source);
  int64_t spawned = qa_compat_qc_entity_memory_get_count(entities);
  CHECK(spawned == 162);
  uint32_t spawn_hash = 0x811c9dc5u;
  static const char *const first[12] = {"worldspawn", "", "bodyque", "bodyque", "bodyque", "bodyque", "info_player_start",
                                        "light_fluorospark", "door", "door", "light_fluoro", "item_armor1"};
  for (int64_t slot = 0; slot < spawned; slot++) {
    qa_owned_actor actor; const char *name = "";
    if (qa_source_actor_slots_at(qa_app_sim_quakec_source_slots(f.source), slot, &actor)) name = qa_app_sim_quakec_source_classname(f.source, actor.id);
    if (slot < 12) CHECK(strcmp(name, first[slot]) == 0);
    if (slot) spawn_hash = fnv_text("\n", spawn_hash);
    spawn_hash = fnv_text(name, spawn_hash);
  }
  CHECK(spawn_hash == 1197432475u);

  /* 100 frames: StartFrame, force_retouch, thinks; per-frame think counts. */
  qa_clock_profile profile; memset(&profile, 0, sizeof profile);
  profile.kind = QA_CLOCK_Q1_NETQUAKE; profile.minimum_frame_seconds = 0.001; profile.maximum_frame_seconds = 0.1;
  size_t total_thinks = 0; uint32_t frame_hash = 0x811c9dc5u;
  int64_t nextthink = qa_compat_qc_machine_field_offset(qa_app_sim_quakec_source_machine(f.source), "nextthink");
  size_t first_thinks[5] = {0};
  for (int i = 1; i <= 100; i++) {
    now_seconds = 1 + i * 0.1;
    qa_frame_context frame = frame_at(i);
    qa_app_sim_quakec_source_begin_frame(f.source, frame);
    size_t thinks = 0;
    for (int64_t slot = 0; slot < qa_compat_qc_entity_memory_get_count(entities); slot++) {
      qa_owned_actor actor;
      if (!qa_source_actor_slots_at(qa_app_sim_quakec_source_slots(f.source), slot, &actor) || !qa_session_actor_registry_is_live(f.actors, actor.id)) continue;
      qa_app_sim_quakec_source_before_actor(f.source, actor);
      double due = qa_compat_qc_words_float(qa_compat_qc_entity_memory_at(entities, slot), nextthink);
      if (qa_think_callback_time(profile, (qa_source_time){QA_SOURCE_TIME_SECONDS, due}, frame).has_value) thinks++;
      qa_app_sim_quakec_source_run_think(f.source, actor, frame);
    }
    qa_app_sim_quakec_source_end_frame(f.source);
    total_thinks += thinks;
    if (i <= 5) first_thinks[i - 1] = thinks;
    char line[64];
    (void)snprintf(line, sizeof line, "%s%zu,%lld,%zu", i == 1 ? "" : ";", thinks, (long long)qa_compat_qc_entity_memory_get_count(entities), live_count(&f));
    frame_hash = fnv_text(line, frame_hash);
  }
  CHECK(first_thinks[0] == 37 && first_thinks[1] == 28 && first_thinks[2] == 7 && first_thinks[3] == 10 && first_thinks[4] == 17);
  CHECK(total_thinks == 2284);
  CHECK(qa_compat_qc_entity_memory_get_count(entities) == 164);
  CHECK(frame_hash == 2019721561u);
  uint32_t eh = entity_hash(&f), gh = globals_hash(&f);
  CHECK(eh == 1466314939u);
  CHECK(gh == 3429457872u);
  CHECK(emits == 46);
  CHECK(admits == 378); /* TS admits after 100 frames */
  printf("e1m1: resources=%zu admits=%zu models=%zu sounds=%zu spawned=%lld thinks=%zu entityHash=%u globalsHash=%u emits=%zu\n",
         f.prepared.resources.count, admits, model_count, sound_count, (long long)spawned, total_thinks, eh, gh, emits);

  /* Checkpoint after a completed frame: deterministic host state. */
  const qa_guest_checkpoint *cp = qa_app_sim_quakec_source_checkpoint(f.source);
  CHECK(cp != NULL && cp->entities.length > 0 && cp->globals.length > 0);
  size_t globals_length = cp->globals.length;
  cp = qa_app_sim_quakec_source_checkpoint(f.source);
  CHECK(cp->globals.length == globals_length);

  /* Client admission (TS application test spies admitClient). */
  qa_client_id client = qa_identity_owner_client(&f.identity, 0, 1);
  qa_app_sim_cvar_name_value name = {"name", "Ranger"};
  qa_app_sim_quakec_source_set_client_info(f.source, client, (qa_app_sim_quakec_info){&name, 1});
  qa_owned_actor player_actor = qa_app_sim_quakec_source_admit_client(f.source, client);
  has_player_client = true; player_id = player_actor.id; player_client = client;
  qa_compat_qc_machine *m = qa_app_sim_quakec_source_machine(f.source);
  qa_compat_qc_words *pw = qa_compat_qc_entity_memory_at(entities, 1);
#define PF(n) qa_compat_qc_words_float(pw, qa_compat_qc_machine_field_offset(m, n))
  CHECK(strcmp(qa_app_sim_quakec_source_classname(f.source, player_actor.id), "player") == 0);
  CHECK(PF("health") == 100 && PF("items") == 4353 && PF("weapon") == 1 && PF("ammo_shells") == 25);
  qa_vec3 origin = qa_compat_qc_words_vector(pw, qa_compat_qc_machine_field_offset(m, "origin"));
  CHECK(origin.x == 480 && origin.y == -352 && origin.z == 89);
  CHECK(qa_app_sim_quakec_source_is_active_client(f.source, player_actor.id));
  CHECK(strcmp(qa_compat_qc_strings_get(&m->strings, qa_compat_qc_words_int(pw, qa_compat_qc_machine_field_offset(m, "netname"))), "Ranger") == 0);
  const char *team = qa_app_sim_quakec_source_match_team(f.source, player_actor.id);
  CHECK(team && strcmp(team, "1") == 0);
  size_t client_count = 0;
  const qa_client_id *connected = qa_app_sim_quakec_source_connected_client_identities(f.source, &client_count);
  CHECK(client_count == 1 && connected[0].slot == 0);
  qa_app_sim_quakec_client_ui ui = qa_app_sim_quakec_source_client_ui(f.source, player_actor.id);
  CHECK(ui.weapon.active_weapon && strcmp(ui.weapon.active_weapon, "q1:weapon/shotgun") == 0);
  free(ui.weapon.items);
  for (int i = 101; i <= 110; i++) {
    now_seconds = 1 + i * 0.1;
    qa_frame_context frame = frame_at(i);
    qa_app_sim_quakec_source_begin_frame(f.source, frame);
    qa_q1_user_command command; memset(&command, 0, sizeof command);
    command.view_angles = (qa_vec3){0, 90, 0}; command.impulse = i == 102 ? 2 : 0;
    qa_app_sim_quakec_source_client_input(f.source, player_actor.id, command);
    qa_app_sim_quakec_source_client_pre_think(f.source, player_actor);
    for (int64_t slot = 0; slot < qa_compat_qc_entity_memory_get_count(entities); slot++) {
      qa_owned_actor actor;
      if (!qa_source_actor_slots_at(qa_app_sim_quakec_source_slots(f.source), slot, &actor) || !qa_session_actor_registry_is_live(f.actors, actor.id)) continue;
      qa_app_sim_quakec_source_before_actor(f.source, actor);
      qa_app_sim_quakec_source_run_think(f.source, actor, frame);
    }
    qa_app_sim_quakec_source_client_post_think(f.source, player_actor);
    qa_app_sim_quakec_source_end_frame(f.source);
  }
  pw = qa_compat_qc_entity_memory_at(entities, 1);
  {
    qa_vec3 o = qa_compat_qc_words_vector(pw, qa_compat_qc_machine_field_offset(m, "origin"));
    qa_vec3 vel = qa_compat_qc_words_vector(pw, qa_compat_qc_machine_field_offset(m, "velocity"));
    qa_vec3 ang = qa_compat_qc_words_vector(pw, qa_compat_qc_machine_field_offset(m, "angles"));
    printf("player: o=%g %g %g vel=%g %g %g ang=%g %g %g fl=%g fr=%g nt=%.17g af=%g mt=%g cnt=%lld\n", o.x, o.y, o.z, vel.x, vel.y, vel.z,
           ang.x, ang.y, ang.z, PF("flags"), PF("frame"), PF("nextthink"), PF("attack_finished"), PF("movetype"),
           (long long)qa_compat_qc_entity_memory_get_count(entities));
  }
  if (getenv("QC_DUMP")) {
    printf("SLOTS ");
    for (int64_t slot = 0; slot < qa_compat_qc_entity_memory_get_count(entities); slot++) {
      qa_compat_qc_words *w = qa_compat_qc_entity_memory_at(entities, slot);
      printf("%s%u", slot ? "," : "", fnv(w->bytes, w->byte_length, 0x811c9dc5u));
    }
    printf("\nGLOB ");
    for (size_t i = 0; i < m->globals.byte_length / 4; i++) printf("%s%d", i ? "," : "", qa_compat_qc_words_int(&m->globals, (int64_t)i));
    printf("\n");
  }
  size_t taken = 0;
  (void)qa_app_sim_simulation_events_take(f.events, &taken);
  const char *give_s[2] = {"s", "42"}, *give_9[1] = {"9"};
  qa_app_sim_quakec_source_host_cheat(f.source, player_actor.id, "god", NULL, 0);
  qa_app_sim_quakec_source_host_cheat(f.source, player_actor.id, "give", give_s, 2);
  qa_app_sim_quakec_source_host_cheat(f.source, player_actor.id, "give", give_9, 1);
  const qa_simulation_event *events = qa_app_sim_simulation_events_take(f.events, &taken);
  for (size_t i = 0; i < taken; i++) printf("message %zu kind=%d text=%s\n", i, (int)events[i].payload.event.kind, events[i].payload.event.text ? events[i].payload.event.text : "(null)");
  CHECK(taken == 2);
  if (taken == 2) {
    CHECK(strcmp(events[0].payload.event.text, "godmode ON\n") == 0);
    CHECK(strcmp(events[1].payload.event.text, "Unknown item: 9\n") == 0);
  }
  CHECK(admits == 379);
  CHECK(PF("health") == 100 && PF("weapon") == 1 && PF("impulse") == 0 && PF("items") == 4353 && PF("ammo_shells") == 42 && PF("flags") == 4168);
  uint32_t eh2 = entity_hash(&f), gh2 = globals_hash(&f); /* TS hashes after the host commands */
  CHECK(eh2 == 4226517570u);
  CHECK(gh2 == 3411727970u);
  printf("client: entityHash=%u globalsHash=%u\n", eh2, gh2);

  /* Disconnect retires the client phase. */
  qa_app_sim_quakec_source_disconnect_client(f.source, player_actor);
  CHECK(!qa_app_sim_quakec_source_has_client(f.source, client));
  CHECK(!qa_app_sim_quakec_source_is_active_client(f.source, player_actor.id));

  CHECK(bad_ctx == 0);
  qa_app_sim_quakec_source_destroy(f.source);
  qa_app_sim_source_random_destroy(f.random);
  qa_app_sim_simulation_events_destroy(f.events);
  qa_shared_inventory_table_destroy(f.inventory);
  qa_gameplay_authority_destroy(f.combat);
  qa_app_sim_shared_physics_destroy(f.physics);
  qa_shared_scene_queries_destroy(f.scene);
  qa_actor_callback_table_destroy(f.callbacks);
  qa_session_actor_registry_destroy(f.actors);
  qa_identity_owner_destroy(&f.identity);
  qa_app_sim_prepared_quakec_source_destroy(&f.prepared);
  qa_q1_map_destroy(f.map); free(f.map);
  qa_content_mounted_destroy(f.mounted);
  free(f.digest0); free(f.digest1);
  for (int i = 0; i < 8; i++) free(f.owned[i]);
  printf("test_quakec_source: %d checks, %d failures\n", checks, failures);
  return failures != 0;
}
