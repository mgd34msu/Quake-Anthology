/*
 * Depth checks for src/compat/qc/mod-provider.ts, reproducing
 * tests/compat/qc/mod-provider.test.ts against the retail Copper progs
 * (skipped when absent) and the engine-service wiring (physics bind,
 * events, spatial host) with sentinel contexts. GPL-2.0-or-later.
 */
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "qa/app/bootstrap/simulation/random.h"
#include "qa/compat/qc/mod_items.h"
#include "qa/compat/qc/mod_provider.h"
#include "qa/compat/qc/program.h"
#include "qa/world/actors/callbacks.h"

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

#define CHECK_ABORTS(...)                                               \
  do {                                                                  \
    checks++;                                                           \
    pid_t pid = fork();                                                 \
    if (pid == 0) {                                                     \
      (void)prctl(PR_SET_DUMPABLE, 0);                                  \
      freopen("/dev/null", "w", stderr);                                \
      __VA_ARGS__;                                                      \
      _exit(0);                                                         \
    }                                                                   \
    int status = 0;                                                     \
    waitpid(pid, &status, 0);                                           \
    if (!(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT)) {        \
      failures++;                                                       \
      fprintf(stderr, "FAIL %s:%d: expected abort: %s\n", __FILE__,     \
              __LINE__, #__VA_ARGS__);                                  \
    }                                                                   \
  } while (0)

static uint8_t *read_file(const char *path, size_t *out_length) {
  FILE *file = fopen(path, "rb");
  if (!file) return NULL;
  fseek(file, 0, SEEK_END);
  long length = ftell(file);
  fseek(file, 0, SEEK_SET);
  uint8_t *bytes = malloc((size_t)length);
  if (bytes && fread(bytes, 1, (size_t)length, file) != (size_t)length) {
    free(bytes);
    bytes = NULL;
  }
  fclose(file);
  *out_length = (size_t)length;
  return bytes;
}

/* ---- shared world fixtures ---- */
static qa_bounds abs_bounds(void *ctx, qa_owned_actor a, qa_body_state s) {
  (void)ctx;
  return qa_translated_body_bounds(a, s);
}
static void on_link(void *ctx, qa_linked_body b) { (void)ctx; (void)b; }
static void on_unlink(void *ctx, qa_actor_id a) { (void)ctx; (void)a; }

/* Services context: the sentinel every service callback must receive. */
typedef struct World {
  int sentinel;
  qa_session_actor_registry *actors;
  bool restored;
  double now;
  int bind_calls, unbind_calls, emits, prints;
  void *bad_ctx;
} World;
static World *as_world(void *ctx) {
  World *world = ctx;
  if (!world || world->sentinel != 0x51C0) {
    fprintf(stderr, "FAIL: service received a foreign ctx\n");
    abort();
  }
  return world;
}
static qa_source_time world_time(void *ctx) {
  return (qa_source_time){QA_SOURCE_TIME_SECONDS, as_world(ctx)->now};
}
static qa_actor_id world_reference_saved(void *ctx, qa_saved_actor_id saved) {
  World *world = as_world(ctx);
  return qa_session_actor_registry_reference_saved(world->actors, saved,
                                                   world->restored ? "checkpoint" : "current");
}

/* ---- SourceRandom adapter (TS SourceRandom(17)) ---- */
typedef struct RandomCell {
  int sentinel;
  qa_app_sim_source_random *random;
  double words[31];
} RandomCell;
static RandomCell *as_random(void *ctx) {
  RandomCell *cell = ctx;
  if (!cell || cell->sentinel != 0x7A4D) {
    fprintf(stderr, "FAIL: random received a foreign ctx\n");
    abort();
  }
  return cell;
}
static double rnd_integer(void *ctx) { return qa_app_sim_source_random_next_integer(as_random(ctx)->random); }
static double rnd_unit(void *ctx) { return qa_app_sim_source_random_next_unit(as_random(ctx)->random); }
static qa_random_state rnd_checkpoint(void *ctx) {
  RandomCell *cell = as_random(ctx);
  qa_app_sim_source_random_state s = qa_app_sim_source_random_checkpoint(cell->random);
  qa_random_state out;
  memset(&out, 0, sizeof out);
  out.kind = QA_RANDOM_STATE_GLIBC_RANDOM;
  for (int i = 0; i < 31; i++) cell->words[i] = s.words[i];
  out.words = (qa_double_view){cell->words, 31};
  out.front = s.front;
  out.rear = s.rear;
  out.draws = s.draws;
  return out;
}
static void rnd_restore(void *ctx, qa_random_state state) {
  RandomCell *cell = as_random(ctx);
  if (state.kind != QA_RANDOM_STATE_GLIBC_RANDOM) abort();
  qa_app_sim_source_random_state s;
  memset(&s, 0, sizeof s);
  s.kind = "glibc-random";
  for (size_t i = 0; i < state.words.count && i < 31; i++) s.words[i] = (int32_t)state.words.values[i];
  s.front = (int)state.front;
  s.rear = (int)state.rear;
  s.draws = (int64_t)state.draws;
  qa_app_sim_source_random_restore(cell->random, s);
}

typedef struct Fixture {
  World world;
  qa_actor_callback_table *callbacks;
  qa_shared_body_table *bodies;
  qa_shared_inventory_table *inventory;
  qa_gameplay_authority *combat;
  qa_mod_host_services services;
  RandomCell random_cell;
  qa_compat_qc_mod_random random;
} Fixture;
static void fixture_init(Fixture *f, qa_session_actor_registry *actors, bool restored, double now) {
  memset(f, 0, sizeof *f);
  f->world.sentinel = 0x51C0;
  f->world.actors = actors;
  f->world.restored = restored;
  f->world.now = now;
  f->callbacks = qa_actor_callback_table_create(actors);
  f->bodies = qa_shared_body_table_create(actors, (qa_body_link_hooks){NULL, abs_bounds, on_link, on_unlink});
  f->inventory = qa_shared_inventory_table_create(actors);
  qa_gameplay_hooks hooks;
  memset(&hooks, 0, sizeof hooks);
  f->combat = qa_gameplay_authority_create(actors, f->callbacks, hooks);
  f->services.actors = actors;
  f->services.callbacks = f->callbacks;
  f->services.bodies = f->bodies;
  f->services.combat = f->combat;
  f->services.inventory = f->inventory;
  f->services.seed = 17;
  f->services.ctx = &f->world;
  f->services.time = world_time;
  f->services.reference_saved = world_reference_saved;
  f->random_cell.sentinel = 0x7A4D;
  f->random_cell.random = qa_app_sim_source_random_create(17, "classic");
  f->random = (qa_compat_qc_mod_random){{&f->random_cell, rnd_integer, rnd_unit, rnd_checkpoint}, rnd_restore};
}
static void fixture_destroy(Fixture *f) {
  qa_gameplay_authority_destroy(f->combat);
  qa_shared_inventory_table_destroy(f->inventory);
  qa_shared_body_table_destroy(f->bodies);
  qa_actor_callback_table_destroy(f->callbacks);
  qa_app_sim_source_random_destroy(f->random_cell.random);
}

static qa_mod_callback_declaration declaration_for(const qa_compat_qc_program *program) {
  qa_mod_callback_declaration decl;
  memset(&decl, 0, sizeof decl);
  decl.version = 1;
  decl.runtime = "quakec";
  decl.program.path = "progs.dat";
  decl.program.digest = program->digest;
  return decl;
}
static qa_mod_named_value SELF_TIME[] = {
    {"self", {.kind = "input", .name = "self"}},
    {"time", {.kind = "input", .name = "time"}},
};
static double call0(qa_compat_qc_mod_provider *p, const char *function, qa_actor_id actor, double time) {
  qa_compat_qc_mod_input_entry inputs[2];
  memset(inputs, 0, sizeof inputs);
  inputs[0].name = "self";
  inputs[0].value.kind = "actor";
  inputs[0].value.actor_value = &actor;
  inputs[1].name = "time";
  inputs[1].value.kind = "float";
  inputs[1].value.number_value = time;
  return qa_compat_qc_mod_provider_invoke(p, (qa_mod_source_call){function, NULL, 0, SELF_TIME, 2}, inputs, 2, NULL);
}

/* TS "QC source items share capacity fields while count and capacity
 * aliases remain invalid". */
static void test_copper_items(qa_compat_qc_program *program) {
  qa_mod_qc_item_definition definitions[2];
  memset(definitions, 0, sizeof definitions);
  definitions[0].item = "test:shells"; definitions[0].label = "Shells"; definitions[0].admission = "add";
  definitions[1].item = "test:nails"; definitions[1].label = "Nails"; definitions[1].admission = "add";
  qa_mod_qc_item_storage storage[2];
  memset(storage, 0, sizeof storage);
  storage[0].kind = "counter"; storage[0].item = "test:shells"; storage[0].field = "ammo_shells";
  storage[0].capacity.field = "max_health";
  storage[1].kind = "counter"; storage[1].item = "test:nails"; storage[1].field = "ammo_nails";
  storage[1].capacity.field = "max_health";
  qa_mod_qc_items items;
  memset(&items, 0, sizeof items);
  items.definitions = definitions; items.definition_count = 2;
  items.storage = storage; items.storage_count = 2;
  qa_mod_actor_field fields[3] = {{.field = "ammo_shells", .binding = "private"},
                                  {.field = "ammo_nails", .binding = "private"},
                                  {.field = "max_health", .binding = "private"}};
  qa_mod_qc_clients clients;
  memset(&clients, 0, sizeof clients);
  clients.maximum = 1;
  qa_mod_callback_declaration decl = declaration_for(program);
  decl.actor_fields = fields; decl.actor_field_count = 3;
  decl.clients = &clients;
  decl.items = &items;
  qa_compat_qc_validate_items(program, decl);
  CHECK(true);
  for (int i = 0; i < 2; i++) {
    qa_mod_qc_item_storage aliased[2] = {storage[0], storage[1]};
    aliased[1].field = i == 0 ? "ammo_shells" : "max_health";
    qa_mod_qc_items bad_items = items;
    bad_items.storage = aliased;
    qa_mod_callback_declaration bad = decl;
    bad.items = &bad_items;
    CHECK_ABORTS(qa_compat_qc_validate_items(program, bad));
  }
}

/* TS "original Copper healing executes through declared actor callbacks
 * in isolated mod instances": T_Heal through two provider instances,
 * health projected through the canonical combat authority. */
static void test_copper_healing(qa_compat_qc_program *program) {
  qa_identity_owner identities;
  CHECK(qa_identity_owner_create(&identities, "copper-mods"));
  qa_session_actor_registry *actors = qa_session_actor_registry_create(&identities, 1024);
  Fixture f;
  fixture_init(&f, actors, false, 3);
  qa_owned_actor target = qa_session_actor_registry_allocate(actors, "q2:game", "q2:monster_soldier");
  qa_combat_state initial;
  memset(&initial, 0, sizeof initial);
  initial.health = 20;
  initial.mass = 200;
  initial.can_take_damage = true;
  initial.armor.regular.kind = QA_REGULAR_ARMOR_NONE;
  initial.armor.powered.kind = "none";
  qa_gameplay_authority_create_state(f.combat, target, initial, NULL, NULL);
  qa_mod_actor_field fields[3] = {
      {.field = "health", .binding = "health"},
      {.field = "classname", .binding = "constant", .constant_value = {.kind = "string", .string_value = "monster_soldier"}},
      {.field = "max_health", .binding = "constant", .constant_value = {.kind = "float", .number_value = 100}}};
  double amounts[2] = {15.5, 7.2};
  qa_compat_qc_mod_provider providers[2];
  for (int i = 0; i < 2; i++) {
    qa_mod_callback_value arguments[3] = {{.kind = "input", .name = "self"},
                                          {.kind = "float", .number_value = amounts[i]},
                                          {.kind = "float", .number_value = 0}};
    qa_mod_callback callbacks[1] = {{.call = {"T_Heal", arguments, 3, NULL, 0},
                                     .binding = {"copper:heal", "actor.use", "observe", NULL}}};
    qa_mod_callback_declaration decl = declaration_for(program);
    decl.actor_fields = fields; decl.actor_field_count = 3;
    decl.callbacks = callbacks; decl.callback_count = 1;
    qa_module_identity module = {i == 0 ? "mod:healing-0" : "mod:healing-1", "progs.dat", program->digest, "test"};
    CHECK(qa_compat_qc_mod_provider_init(&providers[i], program, module, decl, &f.services, &f.random, NULL, NULL, NULL, 0));
    qa_actor_id self = target.id;
    qa_compat_qc_mod_input_entry inputs[2];
    memset(inputs, 0, sizeof inputs);
    inputs[0].name = "self"; inputs[0].value.kind = "actor"; inputs[0].value.actor_value = &self;
    inputs[1].name = "time"; inputs[1].value.kind = "float"; inputs[1].value.number_value = 3;
    qa_compat_qc_mod_provider_invoke(&providers[i], callbacks[0].call, inputs, 2, NULL);
  }
  /* 20 + ceil(15.5) = 36, + ceil(7.2) = 44 (T_Heal rounds up). */
  CHECK(qa_gameplay_authority_read(f.combat, target.id).value.health == 44);
  for (int i = 0; i < 2; i++) qa_compat_qc_mod_provider_destroy(&providers[i]);
  fixture_destroy(&f);
  qa_session_actor_registry_destroy(actors);
  qa_identity_owner_destroy(&identities);
}

/* TS "original Copper helper spawn and scheduled removal retain shared
 * actor ownership across save". */
static qa_mod_actor_field HELPER_FIELDS[11];
static qa_mod_callback_declaration helper_declaration(qa_compat_qc_program *program) {
  const char *privates[] = {"version", "spawnflags", NULL, NULL, "owner", "model", "solid", "touch", "use"};
  for (int i = 0; i < 9; i++) {
    memset(&HELPER_FIELDS[i], 0, sizeof HELPER_FIELDS[i]);
    HELPER_FIELDS[i].field = privates[i];
    HELPER_FIELDS[i].binding = "private";
  }
  HELPER_FIELDS[2] = (qa_mod_actor_field){.field = "impulse", .binding = "constant",
                                          .constant_value = {.kind = "float", .number_value = 20}};
  HELPER_FIELDS[3] = (qa_mod_actor_field){.field = "classname", .binding = "constant",
                                          .constant_value = {.kind = "string", .string_value = "player"}};
  HELPER_FIELDS[9] = (qa_mod_actor_field){.field = "think", .binding = "think"};
  HELPER_FIELDS[10] = (qa_mod_actor_field){.field = "nextthink", .binding = "nextthink"};
  qa_mod_callback_declaration decl = declaration_for(program);
  decl.actor_fields = HELPER_FIELDS;
  decl.actor_field_count = 11;
  return decl;
}
static qa_frame_context frame_at(qa_source_time time, qa_source_time elapsed) {
  qa_frame_context frame;
  memset(&frame, 0, sizeof frame);
  frame.frame = 1;
  frame.time = time;
  frame.elapsed = elapsed;
  frame.phase = QA_FRAME_PHASE_FRAME_EXIT;
  return frame;
}
static void test_copper_helper(qa_compat_qc_program *program) {
  qa_identity_owner first_identities, second_identities;
  CHECK(qa_identity_owner_create(&first_identities, "copper-helper-original"));
  CHECK(qa_identity_owner_create(&second_identities, "copper-helper-restored"));
  qa_session_actor_registry *original = qa_session_actor_registry_create(&first_identities, 1024);
  qa_owned_actor player = qa_session_actor_registry_allocate(original, "q2:game", "q2:player");
  Fixture f;
  fixture_init(&f, original, false, 5);
  qa_module_identity module = {"mod:copper-helper", "progs.dat", program->digest, "test"};
  qa_mod_callback_declaration decl = helper_declaration(program);
  qa_compat_qc_mod_provider source;
  CHECK(qa_compat_qc_mod_provider_init(&source, program, module, decl, &f.services, &f.random, NULL, NULL, NULL, 0));
  CHECK(call0(&source, "SUB_ShouldSpawn", player.id, 5) == 1);
  size_t owned_count = 0;
  const qa_owned_actor *owned = qa_session_actor_registry_owned_by(original, "mod:copper-helper", &owned_count);
  CHECK(owned_count == 1);
  if (owned_count != 1) return;
  qa_owned_actor helper = owned[0];
  qa_actor_source_result helper_source = qa_session_actor_registry_source_of(original, helper.id);
  CHECK(helper_source.has_value && strcmp(helper_source.provider, "mod:copper-helper") == 0);
  call0(&source, "SUB_RemoveSoon", helper.id, 5);
  const qa_guest_checkpoint *guest = qa_compat_qc_mod_provider_checkpoint(&source);
  size_t slot_count = 0, source_count = 0;
  const qa_actor_slot_checkpoint *slots_view = qa_session_actor_registry_checkpoint(original, &slot_count);
  qa_actor_slot_checkpoint *slots = malloc((slot_count + 1) * sizeof *slots);
  memcpy(slots, slots_view, slot_count * sizeof *slots);
  const qa_source_actor_checkpoint *sources_view = qa_session_actor_registry_source_checkpoint(original, &source_count);
  qa_source_actor_checkpoint *sources = malloc((source_count + 1) * sizeof *sources);
  memcpy(sources, sources_view, source_count * sizeof *sources);
  qa_compat_qc_mod_provider_advance(&source, frame_at((qa_source_time){QA_SOURCE_TIME_SECONDS, 5.05},
                                                     (qa_source_time){QA_SOURCE_TIME_SECONDS, 0.1}));
  CHECK(!qa_session_actor_registry_is_live(original, helper.id));
  CHECK(qa_session_actor_registry_is_live(original, player.id));

  qa_session_actor_registry *restored = qa_session_actor_registry_restore(
      &second_identities, slots, slot_count, sources, source_count, 1024);
  Fixture g;
  fixture_init(&g, restored, true, 5);
  qa_compat_qc_mod_provider resumed;
  CHECK(qa_compat_qc_mod_provider_init(&resumed, program, module, decl, &g.services, &g.random, NULL, NULL, NULL, 0));
  qa_compat_qc_mod_provider_restore(&resumed, guest);
  size_t revived_count = 0;
  const qa_owned_actor *revived = qa_session_actor_registry_owned_by(restored, "mod:copper-helper", &revived_count);
  CHECK(revived_count == 1);
  qa_actor_id revived_id = revived_count ? revived[0].id : helper.id;
  qa_compat_qc_mod_provider_advance(&resumed, frame_at((qa_source_time){QA_SOURCE_TIME_MILLISECONDS, 5050},
                                                      (qa_source_time){QA_SOURCE_TIME_MILLISECONDS, 100}));
  CHECK(!qa_session_actor_registry_is_live(restored, revived_id));
  size_t game_count = 0;
  (void)qa_session_actor_registry_owned_by(restored, "q2:game", &game_count);
  CHECK(game_count == 1);
  qa_compat_qc_mod_provider_destroy(&resumed);
  qa_compat_qc_mod_provider_destroy(&source);
  fixture_destroy(&g);
  fixture_destroy(&f);
  free(slots);
  free(sources);
  qa_session_actor_registry_destroy(restored);
  qa_session_actor_registry_destroy(original);
  qa_identity_owner_destroy(&second_identities);
  qa_identity_owner_destroy(&first_identities);
}

/* Engine services: physics bindSource receives every spawned source
 * actor with the destination's own ctx; release unbinds the handle;
 * events and prints reach the engine with its ctx. */
typedef struct EngineCtx {
  int sentinel;
  int binds, unbinds, collisions, emits, prints, worlds;
  int last_handle;
  qa_app_sim_source_body_physics last_source;
} EngineCtx;
static EngineCtx *as_engine(void *ctx) {
  EngineCtx *e = ctx;
  if (!e || e->sentinel != 0xE161) {
    fprintf(stderr, "FAIL: engine service received a foreign ctx\n");
    abort();
  }
  return e;
}
static int phys_bind(void *ctx, qa_owned_actor actor, qa_app_sim_source_body_physics source) {
  (void)actor;
  EngineCtx *e = as_engine(ctx);
  e->binds++;
  e->last_source = source;
  return ++e->last_handle;
}
static void phys_unbind(void *ctx, int handle) {
  EngineCtx *e = as_engine(ctx);
  if (handle == e->last_handle) e->unbinds++;
}
static void phys_set_collision(void *ctx, qa_owned_actor a, qa_actor_collision c) {
  (void)a; (void)c;
  as_engine(ctx)->collisions++;
}
static const char *phys_step(void *ctx, qa_owned_actor a, double e) { (void)a; (void)e; as_engine(ctx); return NULL; }
static void phys_touch(void *ctx, qa_owned_actor a) { (void)a; as_engine(ctx); }
static qa_q1_pusher_services phys_pusher(void *ctx, qa_app_sim_q1_pusher_env *env) {
  (void)env;
  as_engine(ctx);
  qa_q1_pusher_services services;
  memset(&services, 0, sizeof services);
  return services;
}
static bool phys_read(void *ctx, qa_actor_id a, qa_q1_physics_entity *o) { (void)a; (void)o; as_engine(ctx); return false; }
static void phys_write(void *ctx, qa_q1_physics_entity e) { (void)e; as_engine(ctx); }
static qa_trace_result scene_trace(void *ctx, qa_trace_query q) {
  (void)q;
  as_engine(ctx);
  qa_trace_result r;
  memset(&r, 0, sizeof r);
  r.kind = "q1";
  r.fraction = 1;
  return r;
}
static qa_point_contents_result scene_contents(void *ctx, qa_point_contents_query q) {
  (void)q;
  as_engine(ctx);
  return (qa_point_contents_result){"q1", -1, -1, -1};
}
static void engine_print(void *ctx, const char *text) { (void)text; as_engine(ctx)->prints++; }
static void engine_emit(void *ctx, const qa_session_mod_emit *event) {
  const qa_session_mod_emit *emit = event;
  as_engine(ctx)->emits++;
  CHECK(emit->event.kind != NULL);
}
static void engine_register(void *ctx, const qa_session_mod_resource_registration *resource) { (void)resource; as_engine(ctx); }
static bool engine_world(void *ctx, qa_actor_id *out) { (void)out; as_engine(ctx)->worlds++; return false; }
static void test_engine_wiring(qa_compat_qc_program *program) {
  qa_identity_owner identities;
  CHECK(qa_identity_owner_create(&identities, "copper-engine"));
  qa_session_actor_registry *actors = qa_session_actor_registry_create(&identities, 1024);
  qa_owned_actor player = qa_session_actor_registry_allocate(actors, "q2:game", "q2:player");
  Fixture f;
  fixture_init(&f, actors, false, 5);
  EngineCtx engine_ctx;
  memset(&engine_ctx, 0, sizeof engine_ctx);
  engine_ctx.sentinel = 0xE161;
  qa_session_mod_engine_physics physics = {&engine_ctx, phys_bind, phys_unbind, phys_set_collision, phys_step,
                                           phys_touch, phys_pusher, phys_read, phys_write};
  qa_session_mod_engine_scene scene;
  memset(&scene, 0, sizeof scene);
  scene.queries.ctx = &engine_ctx;
  scene.queries.trace = scene_trace;
  scene.queries.point_contents = scene_contents;
  qa_session_mod_engine_services engine;
  memset(&engine, 0, sizeof engine);
  engine.ctx = &engine_ctx;
  engine.print = engine_print;
  engine.events = (qa_session_mod_engine_events){&engine_ctx, engine_emit, engine_register};
  engine.scene = &scene;
  engine.world = engine_world;
  engine.physics = &physics;
  f.services.engine = &engine;
  qa_module_identity module = {"mod:copper-engine", "progs.dat", program->digest, "test"};
  qa_mod_callback_declaration decl = helper_declaration(program);
  qa_compat_qc_mod_provider provider;
  CHECK(qa_compat_qc_mod_provider_init(&provider, program, module, decl, &f.services, &f.random, NULL, NULL, NULL, 0));
  /* Spawned actors bind their source physics and collision accessors. */
  CHECK(call0(&provider, "SUB_ShouldSpawn", player.id, 5) == 1);
  CHECK(engine_ctx.binds == 1);
  qa_app_sim_shared_solid solid;
  CHECK(engine_ctx.last_source.collision(engine_ctx.last_source.ctx, &solid));
  CHECK(strcmp(solid.solid, "none") == 0);
  /* Release through scheduled SUB_Remove unbinds the same handle. */
  size_t owned_count = 0;
  const qa_owned_actor *owned = qa_session_actor_registry_owned_by(actors, "mod:copper-engine", &owned_count);
  CHECK(owned_count == 1);
  if (owned_count == 1) {
    qa_actor_id helper = owned[0].id;
    call0(&provider, "SUB_RemoveSoon", helper, 5);
    qa_compat_qc_mod_provider_advance(&provider, frame_at((qa_source_time){QA_SOURCE_TIME_SECONDS, 5.05},
                                                         (qa_source_time){QA_SOURCE_TIME_SECONDS, 0.1}));
    CHECK(!qa_session_actor_registry_is_live(actors, helper));
    CHECK(engine_ctx.unbinds == 1);
  }
  /* presentations() reads the owned edict model; none remain. */
  size_t presentation_count = 99;
  (void)qa_compat_qc_mod_provider_presentations(&provider, &presentation_count);
  CHECK(presentation_count == 0);
  /* A moving actor needs the pusher services, which the engine supplies. */
  CHECK(provider.has_pusher && provider.has_movement && provider.has_world_host);
  qa_compat_qc_mod_provider_destroy(&provider);
  fixture_destroy(&f);
  qa_session_actor_registry_destroy(actors);
  qa_identity_owner_destroy(&identities);
}

int main(void) {
  const char *home = getenv("HOME");
  char path[512];
  snprintf(path, sizeof path, "%s/.local/share/quake-typescript/content/q1/rerelease/copper/progs.dat", home ? home : "");
  size_t length = 0;
  uint8_t *bytes = read_file(path, &length);
  if (!bytes) {
    printf("test_qc_provider_depth: Copper progs absent, skipped\n");
    return 0;
  }
  qa_compat_qc_program program;
  CHECK(qa_compat_qc_load_program(bytes, length, NULL, NULL, &program));
  test_copper_items(&program);
  test_copper_healing(&program);
  test_copper_helper(&program);
  test_engine_wiring(&program);
  free(bytes);
  printf("test_qc_provider_depth: %d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
