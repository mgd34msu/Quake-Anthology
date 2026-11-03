/*
 * Depth checks for the QVM mod subsystems (src/compat/qvm/mod-*.ts), reproducing
 * tests/compat/qvm/mod-player-events.test.ts and mod-clients.test.ts. Every
 * callback checks the sentinel context it was registered with.
 * GPL-2.0-or-later.
 */
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

#include "qa/compat/qvm/memory.h"
#include "qa/compat/qvm/mod_clients.h"
#include "qa/compat/qvm/mod_player_events.h"
#include "qa/contracts/identity.h"
#include "qa/foundation/support.h"
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

#define CHECK_ABORTS(...)                                                    \
  do {                                                                       \
    checks++;                                                                \
    fflush(stdout);                                                          \
    fflush(stderr);                                                          \
    pid_t pid = fork();                                                      \
    if (pid == 0) {                                                          \
      (void)prctl(PR_SET_DUMPABLE, 0); /* no coredump for expected aborts */ \
      if (freopen("/dev/null", "w", stderr) == NULL) _exit(2);               \
      __VA_ARGS__;                                                           \
      _exit(0);                                                              \
    }                                                                        \
    int status = 0;                                                          \
    waitpid(pid, &status, 0);                                                \
    if (!(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT)) {             \
      failures++;                                                            \
      fprintf(stderr, "FAIL %s:%d: expected abort: %s\n", __FILE__,          \
              __LINE__, #__VA_ARGS__);                                       \
    }                                                                        \
  } while (0)

static int sentinel_value;
static void *const SENTINEL = &sentinel_value;
typedef struct events_world events_world;
typedef struct clients_world clients_world;
static events_world *current_events;
static clients_world *current_clients;

/* ---------------- mod-player-events.test.ts ---------------- */

static int32_t get_i32(const uint8_t *bytes, int64_t offset) {
  return (int32_t)((uint32_t)bytes[offset] | ((uint32_t)bytes[offset + 1] << 8) |
                   ((uint32_t)bytes[offset + 2] << 16) | ((uint32_t)bytes[offset + 3] << 24));
}

/* Writes through the observed memory, as the TS DataView over QvmMemory does. */
static void store(qa_qvm_memory *memory, int64_t address, int32_t value) {
  qa_qvm_writable_view view;
  qa_qvm_memory_data_view(memory, address, 4, &view);
  qa_qvm_writable_view_set_int32(&view, 0, value, true);
}

typedef struct recorded_event {
  qa_actor_id actor;
  double event, parameter;
  bool predictable;
  double sequence, time_ms;
  double client_number, event_sequence;
  qa_vec3 origin;
} recorded_event;

struct events_world {
  qa_identity_owner owner;
  qa_qvm_memory memory;
  uint8_t *bytes;
  recorded_event events[32];
  size_t count;
  qa_vec3 origin;
  double time;
  /* restored-cursor test mapping */
  bool map_restored;
  qa_actor_id first, second, restored_first, restored_second;
};

static events_world *events_ctx(void *ctx) {
  CHECK(ctx == SENTINEL);
  return current_events;
}

static qa_actor_id ev_reference_saved(void *ctx, qa_saved_actor_id saved) {
  events_world *world = events_ctx(ctx);
  if (world->map_restored) {
    if (saved.slot == (double)world->first.slot && saved.generation == (double)world->first.generation)
      return world->restored_first;
    if (saved.slot == (double)world->second.slot && saved.generation == (double)world->second.generation)
      return world->restored_second;
    qa_range_error("Missing restored actor");
  }
  return qa_identity_owner_actor(&world->owner, (int64_t)saved.slot, (int64_t)saved.generation);
}
static bool ev_live(void *ctx, qa_actor_id actor) {
  (void)events_ctx(ctx);
  (void)actor;
  return true;
}
static qa_vec3 ev_origin(void *ctx, qa_actor_id actor) {
  (void)actor;
  return events_ctx(ctx)->origin;
}
static double ev_time(void *ctx) { return events_ctx(ctx)->time; }
static void ev_emit(void *ctx, const qa_app_q3_source_player_event *event) {
  events_world *world = events_ctx(ctx);
  if (world->count >= 32) return;
  recorded_event *out = &world->events[world->count++];
  out->actor = event->actor;
  out->event = event->event;
  out->parameter = event->parameter;
  out->predictable = event->sequence_is_predictable;
  out->sequence = event->sequence_number;
  out->time_ms = event->sequence_time;
  out->client_number = event->player_state.client_number;
  out->event_sequence = event->player_state.event_sequence;
  out->origin = event->origin;
}

static const qa_module_identity events_module = {
    "mod:source-events", "vm/qagame.qvm",
    "sha256:9751bad99a2d138f96a9b0436d2ea2d965b86214175dc33e4cea95e059419337", "test"};

static void events_world_init(events_world *world, const char *name, size_t length) {
  memset(world, 0, sizeof *world);
  CHECK(qa_identity_owner_create(&world->owner, name));
  world->bytes = calloc(length, 1);
  qa_qvm_memory_init(&world->memory, world->bytes, length, NULL, NULL);
  current_events = world;
}

static qa_qvm_mod_player_events_operations events_operations(events_world *world) {
  qa_qvm_mod_player_events_operations operations;
  memset(&operations, 0, sizeof operations);
  operations.memory = &world->memory;
  operations.module = events_module;
  operations.abi_profile = "q3-modern";
  operations.ctx = SENTINEL;
  operations.reference_saved = ev_reference_saved;
  operations.live = ev_live;
  operations.origin = ev_origin;
  operations.time = ev_time;
  operations.emit = ev_emit;
  return operations;
}

static void events_world_destroy(events_world *world) {
  qa_qvm_memory_destroy(&world->memory);
  free(world->bytes);
  qa_identity_owner_destroy(&world->owner);
}

static void external(qa_qvm_memory *memory, int64_t base, int32_t event, int32_t time) {
  store(memory, base + 128, event);
  store(memory, base + 132, event + 1);
  store(memory, base + 136, time);
}
static void predictable(qa_qvm_memory *memory, int32_t event) {
  int32_t sequence = get_i32(memory->bytes, 108);
  store(memory, 112 + (sequence & 1) * 4, event);
  store(memory, 120 + (sequence & 1) * 4, event + 1);
  store(memory, 108, sequence + 1);
}

static void test_player_events_order_and_lifetime(void) {
  events_world world;
  events_world_init(&world, "source-events", 2048);
  world.origin = (qa_vec3){10, 20, 30};
  world.time = 1000;
  qa_actor_id first = qa_identity_owner_actor(&world.owner, 4, 2);
  qa_actor_id second = qa_identity_owner_actor(&world.owner, 8, 3);
  qa_qvm_mod_player_events_operations operations = events_operations(&world);
  qa_qvm_mod_player_events publisher;
  qa_qvm_mod_player_events_init(&publisher, &operations);
  store(&world.memory, 140, 11);
  qa_qvm_mod_player_events_track(&publisher, first, 0, NULL);
  qa_qvm_mod_player_events_track(&publisher, second, 512, NULL);
  external(&world.memory, 512, 349, 700);
  predictable(&world.memory, 999);
  external(&world.memory, 0, 325, 800);
  CHECK(world.count == 0);
  qa_qvm_player_events_checkpoint saved;
  qa_qvm_mod_player_events_checkpoint(&publisher, &saved);
  qa_qvm_player_address players[2] = {{first, 0}, {second, 512}};
  qa_qvm_mod_player_events_restore(&publisher, &saved, players, 2);
  qa_qvm_player_events_checkpoint_free(&saved);
  uint8_t *source = malloc(2048);
  memcpy(source, world.bytes, 2048);
  qa_qvm_mod_player_events_publish(&publisher);
  CHECK(world.count == 3);
  CHECK(world.events[0].actor.slot == 8 && world.events[0].event == 349 && world.events[0].parameter == 350 &&
        !world.events[0].predictable && world.events[0].time_ms == 700);
  CHECK(world.events[1].actor.slot == 4 && world.events[1].event == 999 && world.events[1].parameter == 1000 &&
        world.events[1].predictable && world.events[1].sequence == 0);
  CHECK(world.events[2].actor.slot == 4 && world.events[2].event == 325 && world.events[2].parameter == 326 &&
        !world.events[2].predictable && world.events[2].time_ms == 800);
  CHECK(world.events[1].client_number == 11);
  CHECK(world.events[1].origin.x == 10 && world.events[1].origin.y == 20 && world.events[1].origin.z == 30);
  CHECK(memcmp(source, world.bytes, 2048) == 0);
  free(source);
  qa_qvm_mod_player_events_publish(&publisher);
  CHECK(world.count == 3);
  external(&world.memory, 0, 325, 800);
  qa_qvm_mod_player_events_publish(&publisher);
  CHECK(world.count == 3);
  external(&world.memory, 0, 325, 801);
  qa_qvm_mod_player_events_publish(&publisher);
  CHECK(world.count == 4);
  predictable(&world.memory, 101);
  predictable(&world.memory, 102);
  predictable(&world.memory, 103);
  qa_qvm_mod_player_events_publish(&publisher);
  CHECK(world.count == 6 && world.events[4].event == 102 && world.events[5].event == 103);
  CHECK(world.events[1].event_sequence == 1);
  external(&world.memory, 0, 581, 900);
  qa_qvm_mod_player_events_discard(&publisher);
  qa_qvm_mod_player_events_publish(&publisher);
  CHECK(world.count == 6);
  qa_qvm_mod_player_events_release(&publisher, first);
  external(&world.memory, 0, 837, 1000);
  qa_qvm_mod_player_events_publish(&publisher);
  CHECK(world.count == 6);
  qa_actor_id replacement = qa_identity_owner_actor(&world.owner, first.slot, first.generation + 1);
  qa_qvm_mod_player_events_track(&publisher, replacement, 0, NULL);
  qa_qvm_mod_player_events_publish(&publisher);
  CHECK(world.count == 6);
  external(&world.memory, 0, 69, 1100);
  qa_qvm_mod_player_events_publish(&publisher);
  CHECK(world.count == 7 && qa_same_actor(world.events[6].actor, replacement));
  qa_qvm_mod_player_events_close(&publisher);
  CHECK(!qa_qvm_memory_get_observes_writes(&world.memory));
  qa_qvm_mod_player_events_destroy(&publisher);
  events_world_destroy(&world);
}

static qa_save_value *number_value(double value) { return qa_save_value_number(value); }

static void read_bad_external_order(void) {
  /* { nextOrder: 1, clients: [{ ...cursor, externalOrder: 1 }] } */
  qa_save_value *root = qa_save_value_object();
  qa_save_value_set(root, "nextOrder", number_value(1));
  qa_save_value *clients = qa_save_value_array();
  qa_save_value *cursor = qa_save_value_object();
  qa_save_value *actor = qa_save_value_object();
  qa_save_value_set(actor, "slot", number_value(1));
  qa_save_value_set(actor, "generation", number_value(1));
  qa_save_value_set(cursor, "actor", actor);
  qa_save_value_set(cursor, "external", number_value(861));
  qa_save_value_set(cursor, "externalTime", number_value(55));
  qa_save_value_set(cursor, "sequence", number_value(-2147483647.0 - 1));
  qa_save_value_set(cursor, "observedSequence", number_value(-2147483647.0 - 1));
  qa_save_value_set(cursor, "externalOrder", number_value(1));
  qa_save_value_set(cursor, "predictable", qa_save_value_array());
  qa_save_value_push(clients, cursor);
  qa_save_value_set(root, "clients", clients);
  qa_save_reader reader;
  qa_save_reader_init(&reader, root, "player-events");
  qa_qvm_player_events_checkpoint checkpoint;
  (void)qa_qvm_read_player_events(&reader, &checkpoint);
}

static void test_player_events_wrap(void) {
  events_world world;
  events_world_init(&world, "event-wrap", 1024);
  world.time = 1;
  qa_actor_id actor = qa_identity_owner_actor(&world.owner, 1, 1);
  qa_qvm_mod_player_events_operations operations = events_operations(&world);
  qa_qvm_mod_player_events publisher;
  qa_qvm_mod_player_events_init(&publisher, &operations);
  store(&world.memory, 108, 0x7fffffff);
  qa_qvm_mod_player_events_track(&publisher, actor, 0, NULL);
  store(&world.memory, 116, 97);
  store(&world.memory, 124, 3);
  store(&world.memory, 108, -2147483647 - 1);
  qa_qvm_mod_player_events_publish(&publisher);
  CHECK(world.count == 1 && world.events[0].predictable && world.events[0].sequence == 0x7fffffff);
  CHECK(world.events[0].event == 97);
  store(&world.memory, 128, 349);
  store(&world.memory, 136, 55);
  qa_qvm_player_address players[1] = {{actor, 0}};
  qa_qvm_mod_player_events_restore(&publisher, NULL, players, 1);
  qa_qvm_mod_player_events_publish(&publisher);
  CHECK(world.count == 1);
  store(&world.memory, 128, 605);
  store(&world.memory, 128, 861);
  qa_qvm_mod_player_events_publish(&publisher);
  CHECK(world.count == 2 && world.events[1].event == 861);
  CHECK_ABORTS(read_bad_external_order());
  qa_qvm_mod_player_events_close(&publisher);
  qa_qvm_mod_player_events_destroy(&publisher);
  events_world_destroy(&world);
}

static void test_player_events_restored_identities(void) {
  events_world world;
  events_world_init(&world, "restored-source-events", 2048);
  world.time = 1;
  world.first = qa_identity_owner_actor(&world.owner, 4, 2);
  world.second = qa_identity_owner_actor(&world.owner, 8, 3);
  world.restored_first = qa_identity_owner_actor(&world.owner, 12, 5);
  world.restored_second = qa_identity_owner_actor(&world.owner, 13, 6);
  world.map_restored = true;
  qa_qvm_mod_player_events_operations operations = events_operations(&world);
  qa_qvm_mod_player_events publisher;
  qa_qvm_mod_player_events_init(&publisher, &operations);
  qa_qvm_mod_player_events_track(&publisher, world.first, 0, NULL);
  qa_qvm_mod_player_events_track(&publisher, world.second, 512, NULL);
  store(&world.memory, 128, 325);
  store(&world.memory, 136, 100);
  qa_qvm_mod_player_events_publish(&publisher);
  store(&world.memory, 512 + 128, 349);
  store(&world.memory, 512 + 136, 200);
  store(&world.memory, 112, 93);
  store(&world.memory, 108, 1);
  qa_qvm_player_events_checkpoint saved;
  qa_qvm_mod_player_events_checkpoint(&publisher, &saved);
  uint8_t *source = malloc(2048);
  memcpy(source, world.bytes, 2048);
  qa_qvm_player_address players[2] = {{world.restored_first, 0}, {world.restored_second, 512}};
  qa_qvm_mod_player_events_restore(&publisher, &saved, players, 2);
  qa_qvm_player_events_checkpoint_free(&saved);
  qa_qvm_mod_player_events_publish(&publisher);
  CHECK(world.count == 3 && world.events[0].event == 325 && world.events[1].event == 349 &&
        world.events[2].event == 93);
  CHECK(qa_same_actor(world.events[1].actor, world.restored_second) &&
        qa_same_actor(world.events[2].actor, world.restored_first));
  CHECK(memcmp(source, world.bytes, 2048) == 0);
  free(source);
  qa_qvm_mod_player_events_publish(&publisher);
  CHECK(world.count == 3);
  qa_qvm_player_events_checkpoint after;
  qa_qvm_mod_player_events_checkpoint(&publisher, &after);
  CHECK(after.client_count == 2 && after.clients[0].actor.slot == 12 && after.clients[0].actor.generation == 5 &&
        after.clients[1].actor.slot == 13 && after.clients[1].actor.generation == 6);
  qa_qvm_player_events_checkpoint_free(&after);
  qa_qvm_mod_player_events_close(&publisher);
  CHECK(!qa_qvm_memory_get_observes_writes(&world.memory));
  qa_qvm_mod_player_events_destroy(&publisher);
  events_world_destroy(&world);
}

/* ---------------- mod-clients.test.ts (server frame) ---------------- */

typedef struct client_row {
  qa_client_id client;
  qa_actor_id actor;
  bool present;
} client_row;

struct clients_world {
  qa_identity_owner owner;
  client_row rows[3];
  char seen[16][16];
  size_t seen_count;
  qa_qvm_mod_client_bindings *binding;
  qa_actor_id first, second, replacement;
  int mode; /* 0 observe, 1 forget+reuse, 2 drop second, 3 close */
  bool frame_failure;
};

static clients_world *clients_ctx(void *ctx) {
  CHECK(ctx == SENTINEL);
  return current_clients;
}

static bool same_client(qa_client_id a, qa_client_id b) { return a.slot == b.slot && a.generation == b.generation; }

static bool cs_for_actor(void *ctx, qa_actor_id actor, qa_client_id *out) {
  clients_world *world = clients_ctx(ctx);
  for (size_t i = 0; i < 3; i++)
    if (world->rows[i].present && qa_same_actor(world->rows[i].actor, actor)) {
      *out = world->rows[i].client;
      return true;
    }
  return false;
}
static bool cs_actor(void *ctx, qa_client_id client, qa_actor_id *out) {
  clients_world *world = clients_ctx(ctx);
  for (size_t i = 0; i < 3; i++)
    if (world->rows[i].present && same_client(world->rows[i].client, client)) {
      *out = world->rows[i].actor;
      return true;
    }
  return false;
}
static const qa_mod_client_identity *cs_clients(void *ctx, size_t *out_count) {
  (void)clients_ctx(ctx);
  *out_count = 0;
  return NULL;
}
static void cs_unsubscribe(void *ctx) { (void)clients_ctx(ctx); }
static qa_mod_client_unsubscribe cs_subscribe(void *ctx, qa_mod_client_listener_fn listener, void *listener_ctx) {
  (void)clients_ctx(ctx);
  (void)listener;
  (void)listener_ctx;
  qa_mod_client_unsubscribe unsubscribe = {SENTINEL, cs_unsubscribe};
  return unsubscribe;
}
static qa_mod_client_unsubscribe cs_subscribe_application(void *ctx, qa_mod_client_application_listener_fn listener,
                                                          void *listener_ctx) {
  (void)clients_ctx(ctx);
  (void)listener;
  (void)listener_ctx;
  qa_mod_client_unsubscribe unsubscribe = {SENTINEL, cs_unsubscribe};
  return unsubscribe;
}

static void op_project(void *ctx, qa_actor_id actor) {
  (void)clients_ctx(ctx);
  (void)actor;
}
static void op_release(void *ctx, qa_actor_id actor) {
  (void)clients_ctx(ctx);
  (void)actor;
}
static void op_invoke(void *ctx, const qa_qvm_mod_source_call *call, qa_actor_id actor,
                      qa_mod_client_application *application) {
  (void)clients_ctx(ctx);
  (void)call;
  (void)actor;
  (void)application;
  qa_range_error("Frame must use its server context");
}
static void op_send(void *ctx, const char *text, const qa_actor_id *recipient) {
  (void)clients_ctx(ctx);
  (void)text;
  (void)recipient;
}

static void frame_observe(void *ctx, const qa_qvm_mod_source_call *call, qa_actor_id actor) {
  clients_world *world = clients_ctx(ctx);
  if (world->frame_failure) qa_range_error("original frame failed");
  snprintf(world->seen[world->seen_count++], 16, "%lld:%d", (long long)actor.slot, (int)call->entry);
  if (world->mode == 1 && qa_same_actor(actor, world->first)) {
    qa_qvm_mod_client_bindings_forget(world->binding, world->first);
    world->rows[0].actor = world->replacement;
    int64_t slot = -1;
    CHECK(qa_qvm_mod_client_bindings_slot(world->binding, world->replacement, &slot) && slot == 0);
  } else if (world->mode == 2) {
    world->rows[1].present = false;
  } else if (world->mode == 3) {
    qa_qvm_mod_client_bindings_close(world->binding);
  }
}

static bool seen_equals(clients_world *world, const char *const *expected, size_t count) {
  if (world->seen_count != count) return false;
  for (size_t i = 0; i < count; i++)
    if (strcmp(world->seen[i], expected[i]) != 0) return false;
  return true;
}

typedef struct frame_failure_job {
  clients_world *world;
} frame_failure_job;
static void frame_failure_body(void *ctx) {
  frame_failure_job *job = ctx;
  qa_qvm_mod_client_bindings_frame(job->world->binding, frame_observe, SENTINEL);
}

static void test_client_frame_order(void) {
  clients_world world;
  memset(&world, 0, sizeof world);
  current_clients = &world;
  CHECK(qa_identity_owner_create(&world.owner, "component-frame"));
  world.first = qa_identity_owner_actor(&world.owner, 10, 1);
  world.second = qa_identity_owner_actor(&world.owner, 20, 1);
  qa_actor_id pending = qa_identity_owner_actor(&world.owner, 30, 1);
  for (size_t i = 0; i < 3; i++) {
    world.rows[i].client.session = world.owner.session;
    world.rows[i].client.slot = (int64_t)i + 1;
    world.rows[i].client.generation = 1;
    world.rows[i].present = true;
  }
  world.rows[0].actor = world.first;
  world.rows[1].actor = world.second;
  world.rows[2].actor = pending;
  qa_mod_client_services services;
  memset(&services, 0, sizeof services);
  services.ctx = SENTINEL;
  services.maximum = 3;
  services.clients = cs_clients;
  services.for_actor = cs_for_actor;
  services.actor = cs_actor;
  services.subscribe = cs_subscribe;
  services.subscribe_application = cs_subscribe_application;
  qa_qvm_mod_source_call calls[2];
  memset(calls, 0, sizeof calls);
  calls[0].entry = 1;
  calls[0].returns = "void";
  calls[1].entry = 2;
  calls[1].returns = "void";
  const char *records[] = {"client"};
  qa_qvm_mod_clients declaration;
  memset(&declaration, 0, sizeof declaration);
  declaration.maximum = 3;
  declaration.records.values = records;
  declaration.records.count = 1;
  declaration.frame = calls;
  declaration.frame_count = 2;
  qa_qvm_mod_client_operations operations;
  memset(&operations, 0, sizeof operations);
  operations.ctx = SENTINEL;
  operations.content = "q3:classic:component:test";
  operations.services = &services;
  operations.declaration = &declaration;
  operations.project = op_project;
  operations.release = op_release;
  operations.invoke = op_invoke;
  operations.send = op_send;
  qa_qvm_mod_client_bindings binding;
  qa_qvm_mod_client_bindings_init(&binding, &operations);
  world.binding = &binding;
  qa_qvm_mod_client_slot saved[3] = {{world.second, 1, true}, {world.first, 0, true}, {pending, 2, false}};
  qa_qvm_mod_client_bindings_restore(&binding, saved, 3);
  qa_qvm_mod_client_bindings_start(&binding);
  qa_qvm_mod_client_bindings_frame(&binding, frame_observe, SENTINEL);
  const char *expected_all[] = {"10:1", "10:2", "20:1", "20:2"};
  CHECK(seen_equals(&world, expected_all, 4));
  world.replacement = qa_identity_owner_actor(&world.owner, 10, 2);
  world.seen_count = 0;
  world.mode = 1;
  qa_qvm_mod_client_bindings_frame(&binding, frame_observe, SENTINEL);
  const char *expected_reuse[] = {"10:1", "20:1", "20:2"};
  CHECK(seen_equals(&world, expected_reuse, 3));
  qa_qvm_mod_client_slot rows[4];
  size_t row_count = qa_qvm_mod_client_bindings_checkpoint(&binding, rows, 4);
  bool replacement_unadmitted = false;
  for (size_t i = 0; i < row_count && i < 4; i++)
    if (qa_same_actor(rows[i].actor, world.replacement)) replacement_unadmitted = !rows[i].admitted;
  CHECK(replacement_unadmitted);
  world.seen_count = 0;
  world.mode = 2;
  qa_qvm_mod_client_bindings_frame(&binding, frame_observe, SENTINEL);
  const char *expected_drop[] = {"20:1"};
  CHECK(seen_equals(&world, expected_drop, 1));
  world.rows[1].present = true;
  world.rows[0].actor = world.first;
  qa_qvm_mod_client_bindings_restore(&binding, saved, 3);
  world.mode = 0;
  world.frame_failure = true;
  frame_failure_job job = {&world};
  qa_caught_error caught;
  CHECK(!qa_try(frame_failure_body, &job, &caught) && strcmp(caught.message, "original frame failed") == 0);
  world.frame_failure = false;
  world.seen_count = 0;
  world.mode = 3;
  qa_qvm_mod_client_bindings_frame(&binding, frame_observe, SENTINEL);
  const char *expected_close[] = {"10:1"};
  CHECK(seen_equals(&world, expected_close, 1));
  world.mode = 0;
  qa_qvm_mod_client_bindings_frame(&binding, frame_observe, SENTINEL);
  CHECK(seen_equals(&world, expected_close, 1));
  qa_qvm_mod_client_bindings_destroy(&binding);
  qa_identity_owner_destroy(&world.owner);
}

/* TS slot(): an entry whose actor lost its client throws through require(). */
static void slot_without_client(void) {
  clients_world world;
  memset(&world, 0, sizeof world);
  current_clients = &world;
  qa_identity_owner_create(&world.owner, "component-slot");
  qa_actor_id actor = qa_identity_owner_actor(&world.owner, 4, 1);
  qa_mod_client_services services;
  memset(&services, 0, sizeof services);
  services.ctx = SENTINEL;
  services.for_actor = cs_for_actor;
  services.actor = cs_actor;
  qa_qvm_mod_clients declaration;
  memset(&declaration, 0, sizeof declaration);
  declaration.maximum = 3;
  qa_qvm_mod_client_operations operations;
  memset(&operations, 0, sizeof operations);
  operations.ctx = SENTINEL;
  operations.services = &services;
  operations.declaration = &declaration;
  qa_qvm_mod_client_bindings binding;
  qa_qvm_mod_client_bindings_init(&binding, &operations);
  qa_qvm_mod_client_slot saved[1] = {{actor, 0, true}};
  qa_qvm_mod_client_bindings_restore(&binding, saved, 1);
  int64_t slot = -1;
  (void)qa_qvm_mod_client_bindings_slot(&binding, actor, &slot);
}

static void test_client_slot_requires_live_identity(void) {
  CHECK_ABORTS(slot_without_client());
}

int main(void) {
  test_player_events_order_and_lifetime();
  test_player_events_wrap();
  test_player_events_restored_identities();
  test_client_frame_order();
  test_client_slot_requires_live_identity();
  printf("test_qvm_mods_depth: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
