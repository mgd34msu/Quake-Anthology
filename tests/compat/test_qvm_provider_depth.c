/*
 * Depth checks for src/compat/qvm/mod-provider.ts engine syscalls: the
 * provider's G_SEND_SERVER_COMMAND, G_TRACE, G_POINT_CONTENTS,
 * G_AREAS_CONNECTED and G_ADJUST_AREA_PORTAL_STATE paths, mapname
 * binding, and source-event emission into the engine services. A fake
 * interpreter runtime issues the guest system calls during the
 * declared initialize entry. Every engine leaf checks its sentinel ctx.
 * GPL-2.0-or-later.
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

#include "qa/app/bootstrap/simulation/q3/host.h"
#include "qa/compat/qvm/abi.h"
#include "qa/compat/qvm/image.h"
#include "qa/compat/qvm/mod_provider.h"
#include "qa/compat/qvm/trace_record.h"
#include "qa/world/actors/callbacks.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                    \
  do {                                                                 \
    checks++;                                                          \
    if (!(cond)) {                                                     \
      failures++;                                                      \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
    }                                                                  \
  } while (0)

#define CHECK_ABORTS(...)                                              \
  do {                                                                 \
    checks++;                                                          \
    pid_t pid = fork();                                                \
    if (pid == 0) {                                                    \
      (void)prctl(PR_SET_DUMPABLE, 0);                                 \
      freopen("/dev/null", "w", stderr);                               \
      __VA_ARGS__;                                                     \
      _exit(0);                                                        \
    }                                                                  \
    int status = 0;                                                    \
    waitpid(pid, &status, 0);                                          \
    if (!(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT)) {       \
      failures++;                                                      \
      fprintf(stderr, "FAIL %s:%d: expected abort: %s\n", __FILE__,    \
              __LINE__, #__VA_ARGS__);                                 \
    }                                                                  \
  } while (0)

#define DIGEST "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"

static int sentinel_value;
#define SENTINEL ((void *)&sentinel_value)
#define LEAF() CHECK(ctx == SENTINEL)

static uint8_t guest_bytes[16384];
static qa_qvm_memory guest;
static int32_t get_i32(size_t offset) {
  int32_t value;
  memcpy(&value, guest_bytes + offset, 4);
  return value;
}
static float get_f32(size_t offset) {
  float value;
  memcpy(&value, guest_bytes + offset, 4);
  return value;
}
static void put_f32(size_t offset, float value) { memcpy(guest_bytes + offset, &value, 4); }
static void put_text(size_t offset, const char *text) { memcpy(guest_bytes + offset, text, strlen(text) + 1); }

/* ---- fake interpreter runtime ---- */
typedef struct Script {
  qa_qvm_module *module;
  int32_t results[8];
  size_t result_count;
  bool portal_call;
} Script;
static Script script;
static int32_t syscall_words(const int32_t *words, size_t count) {
  static uint8_t bytes[64];
  memset(bytes, 0, sizeof bytes);
  for (size_t i = 0; i < count; i++) memcpy(bytes + i * 4, &words[i], 4);
  qa_qvm_module_syscall call;
  memset(&call, 0, sizeof call);
  call.words = bytes;
  call.words_length = sizeof bytes;
  call.memory = guest_bytes;
  call.memory_length = sizeof guest_bytes;
  call.guest = &guest;
  return (int32_t)qa_qvm_system_call_invoke(&script.module->source_system_call, &call);
}
static int32_t run_script(void) {
  if (script.portal_call) {
    const int32_t adjust[] = {QA_QVM_GAME_IMPORT_G_ADJUST_AREA_PORTAL_STATE, 256, 1};
    return syscall_words(adjust, 3);
  }
  put_text(4096 + 0, "cp hello");
  const int32_t command[] = {QA_QVM_GAME_IMPORT_G_SEND_SERVER_COMMAND, -1, 4096};
  script.results[script.result_count++] = syscall_words(command, 3);
  /* trap_Trace(&results, start, NULL, NULL, end, 1023, mask 1). */
  put_f32(4200, 1); put_f32(4204, 2); put_f32(4208, 3);
  put_f32(4220, 10); put_f32(4224, 20); put_f32(4228, 30);
  const int32_t trace[] = {QA_QVM_GAME_IMPORT_G_TRACE, 4300, 4200, 0, 0, 4220, 1023, 1};
  script.results[script.result_count++] = syscall_words(trace, 8);
  const int32_t contents[] = {QA_QVM_GAME_IMPORT_G_POINT_CONTENTS, 4200, -1};
  script.results[script.result_count++] = syscall_words(contents, 3);
  const int32_t connected[] = {QA_QVM_GAME_IMPORT_G_AREAS_CONNECTED, 3, 4};
  script.results[script.result_count++] = syscall_words(connected, 3);
  return 0;
}
static bool fake_active(void *ctx) { (void)ctx; return false; }
static int64_t fake_stack(void *ctx) { (void)ctx; return 16000; }
static int32_t fake_invoke(void *ctx, const int32_t *args, size_t count, int64_t index,
                           const qa_qvm_read_only_evaluation *evaluation) {
  (void)args; (void)count; (void)index; (void)evaluation;
  LEAF();
  return run_script();
}
static int32_t fake_invoke_async(void *ctx, const int32_t *args, size_t count, int64_t index,
                                 qa_qvm_validate_fn validate, void *validate_ctx) {
  (void)args; (void)count; (void)index;
  LEAF();
  if (validate != NULL) validate(validate_ctx);
  int32_t result = run_script();
  if (validate != NULL) validate(validate_ctx);
  return result;
}
static size_t fake_bind(void *ctx, int64_t index, qa_qvm_function_hook_fn hook, void *hook_ctx) {
  (void)index; (void)hook; (void)hook_ctx;
  LEAF();
  return 1;
}
static size_t fake_resolver(void *ctx, qa_qvm_function_resolver_fn resolve, void *resolve_ctx) {
  (void)resolve; (void)resolve_ctx;
  LEAF();
  return 1;
}
static size_t fake_observe(void *ctx, int64_t index, qa_qvm_function_observer_fn observe, void *observe_ctx) {
  (void)index; (void)observe; (void)observe_ctx;
  LEAF();
  return 1;
}
static void fake_unbind(void *ctx, size_t id) { (void)id; LEAF(); }

/* ---- engine services ---- */
typedef struct EngineLog {
  int emits, server_commands, traces, contents, connected, prints;
  char command_text[64];
  double last_mask;
  const char *last_shape;
  bool pass_actor_null;
  double area_first, area_second;
} EngineLog;
static EngineLog log_state;
static qa_trace_result scene_trace(void *ctx, qa_trace_query query) {
  LEAF();
  log_state.traces++;
  log_state.last_mask = query.policy.contents_mask;
  log_state.last_shape = query.shape.kind;
  log_state.pass_actor_null = query.pass_actor == NULL;
  CHECK(strcmp(query.policy.kind, "q3") == 0 && query.policy.curves && query.policy.player_curve_clip);
  CHECK(query.start.x == 1 && query.end.z == 30 && !query.target.is_model);
  qa_trace_result result;
  memset(&result, 0, sizeof result);
  result.kind = "q3";
  result.fraction = 0.5;
  result.end = (qa_vec3){5.5, 11, 16.5};
  result.source_plane.plane.normal = (qa_vec3){0, 0, 1};
  result.source_plane.plane.distance = 8;
  result.source_plane.type = 2;
  result.contents = 1;
  result.surface_flags = 4;
  result.hit.kind = QA_TRACE_HIT_WORLD;
  return result;
}
static qa_point_contents_result scene_contents(void *ctx, qa_point_contents_query query) {
  LEAF();
  log_state.contents++;
  CHECK(strcmp(query.policy.kind, "q3") == 0 && query.policy.contents_mask == -1 && query.point.y == 2);
  return (qa_point_contents_result){"q3", 33, 33, 33};
}
static bool scene_connected(void *ctx, double first, double second) {
  LEAF();
  log_state.connected++;
  log_state.area_first = first;
  log_state.area_second = second;
  return true;
}
static void engine_emit(void *ctx, const qa_session_mod_emit *event) {
  LEAF();
  const qa_session_mod_emit *emit = event;
  log_state.emits++;
  CHECK(strcmp(emit->event.kind, "q3-source") == 0);
  CHECK(strcmp(emit->event.content, "q3:test:content") == 0);
  const qa_app_q3_source_event *source = emit->event.q3_source;
  if (source->kind == QA_APP_Q3_EVENT_SERVER_COMMAND) {
    log_state.server_commands++;
    CHECK(source->client == -1);
    snprintf(log_state.command_text, sizeof log_state.command_text, "%s", source->text);
  }
}
static void engine_register(void *ctx, const qa_session_mod_resource_registration *resource) { (void)resource; LEAF(); }
static void engine_print(void *ctx, const char *text) { (void)text; LEAF(); log_state.prints++; }
static bool engine_world(void *ctx, qa_actor_id *out) { (void)out; LEAF(); return false; }

static qa_source_time world_time(void *ctx) {
  LEAF();
  return (qa_source_time){QA_SOURCE_TIME_SECONDS, 2};
}
static void assert_current(void *ctx) { LEAF(); }
static qa_bounds abs_bounds(void *ctx, qa_owned_actor a, qa_body_state s) {
  (void)ctx;
  return qa_translated_body_bounds(a, s);
}
static void on_link(void *ctx, qa_linked_body b) { (void)ctx; (void)b; }
static void on_unlink(void *ctx, qa_actor_id a) { (void)ctx; (void)a; }

static qa_qvm_instruction instructions[] = {
    {.opcode = QA_QVM_OP_ENTER, .operand_width = 4, .operand = 0},
    {.opcode = QA_QVM_OP_LEAVE, .operand_width = 4, .operand = 0},
    {.opcode = QA_QVM_OP_ENTER, .operand_width = 4, .operand = 0},
};
static qa_qvm_mod_actor_field fields[] = {
    {.offset = 0, .binding = "health", .encoding = "int32"},
    {.offset = 16, .binding = "origin"},
};
static qa_qvm_mod_actor_record records[] = {{"ents", 256, 64, 4, fields, 2}};
static qa_qvm_mod_source_call initialize_calls[] = {{0, NULL, 0, NULL, 0, "void"}};

typedef struct Fixture {
  qa_identity_owner identities;
  qa_session_actor_registry *actors;
  qa_actor_callback_table *callbacks;
  qa_shared_body_table *bodies;
  qa_shared_inventory_table *inventory;
  qa_gameplay_authority *combat;
  qa_mod_host_services services;
  qa_session_mod_engine_scene scene;
  qa_session_mod_engine_services engine;
  qa_resolved_qvm_artifact artifact;
  qa_qvm_mod_callback_declaration declaration;
  qa_qvm_interpreter_runtime runtime;
  qa_qvm_mod_provider provider;
} Fixture;
static void fixture_init(Fixture *f) {
  memset(f, 0, sizeof *f);
  memset(guest_bytes, 0, sizeof guest_bytes);
  memset(&log_state, 0, sizeof log_state);
  memset(&script, 0, sizeof script);
  qa_qvm_memory_init(&guest, guest_bytes, sizeof guest_bytes, NULL, NULL);
  CHECK(qa_identity_owner_create(&f->identities, "qvm-provider-depth"));
  f->actors = qa_session_actor_registry_create(&f->identities, 1024);
  f->callbacks = qa_actor_callback_table_create(f->actors);
  f->bodies = qa_shared_body_table_create(f->actors, (qa_body_link_hooks){NULL, abs_bounds, on_link, on_unlink});
  f->inventory = qa_shared_inventory_table_create(f->actors);
  qa_gameplay_hooks hooks;
  memset(&hooks, 0, sizeof hooks);
  f->combat = qa_gameplay_authority_create(f->actors, f->callbacks, hooks);
  f->scene.queries.ctx = SENTINEL;
  f->scene.queries.trace = scene_trace;
  f->scene.queries.point_contents = scene_contents;
  f->scene.queries.areas_connected = scene_connected;
  f->engine.ctx = SENTINEL;
  f->engine.print = engine_print;
  f->engine.events = (qa_session_mod_engine_events){SENTINEL, engine_emit, engine_register};
  f->engine.scene = &f->scene;
  f->engine.world = engine_world;
  f->engine.presentation_map = "maps/q3dm1.bsp";
  f->services.actors = f->actors;
  f->services.callbacks = f->callbacks;
  f->services.bodies = f->bodies;
  f->services.combat = f->combat;
  f->services.inventory = f->inventory;
  f->services.seed = 3;
  f->services.ctx = SENTINEL;
  f->services.time = world_time;
  f->services.engine = &f->engine;
  f->artifact.module.id = "q3:test:game";
  f->artifact.module.artifact_path = "vm/qagame.qvm";
  f->artifact.module.digest = DIGEST;
  f->artifact.module.revision = "1";
  f->artifact.role = "qagame";
  f->artifact.has_abi_profile = true;
  f->artifact.abi_profile = "q3-modern";
  f->artifact.image.instructions = instructions;
  f->artifact.image.instruction_count = 3;
  f->artifact.image.data_length = 4096;
  f->artifact.image.bss_length = 4096;
  f->artifact.image.allocated_data_length = 8192;
  f->declaration.version = 1;
  f->declaration.runtime = "qvm";
  f->declaration.program.path = "vm/qagame.qvm";
  f->declaration.program.digest = DIGEST;
  f->declaration.abi_profile = "q3-modern";
  f->declaration.has_spawn_entities = true;
  f->declaration.spawn_entities = "";
  f->declaration.actor_records = records;
  f->declaration.actor_record_count = 1;
  f->declaration.initialize = initialize_calls;
  f->declaration.initialize_count = 1;
  f->runtime.ctx = SENTINEL;
  f->runtime.address_space = &guest;
  f->runtime.is_active = fake_active;
  f->runtime.stack_pointer = fake_stack;
  f->runtime.invoke = fake_invoke;
  f->runtime.invoke_async = fake_invoke_async;
  f->runtime.bind_function = fake_bind;
  f->runtime.bind_invocation = fake_bind;
  f->runtime.bind_function_resolver = fake_resolver;
  f->runtime.observe_function = fake_observe;
  f->runtime.unbind = fake_unbind;
}
static bool fixture_provider(Fixture *f) {
  bool ok = qa_qvm_mod_provider_init(&f->provider, &f->artifact, &f->declaration, &f->services, assert_current,
                                     SENTINEL, "q3:test:content", NULL, NULL, &f->runtime, NULL);
  script.module = &f->provider.module;
  return ok;
}
static void fixture_destroy(Fixture *f) {
  qa_qvm_mod_provider_close(&f->provider);
  qa_qvm_mod_provider_destroy(&f->provider);
  qa_gameplay_authority_destroy(f->combat);
  qa_shared_inventory_table_destroy(f->inventory);
  qa_shared_body_table_destroy(f->bodies);
  qa_actor_callback_table_destroy(f->callbacks);
  qa_session_actor_registry_destroy(f->actors);
  qa_identity_owner_destroy(&f->identities);
}

static void test_engine_syscalls(void) {
  Fixture f;
  fixture_init(&f);
  CHECK(fixture_provider(&f));
  /* TS bindServerVariables: maps/<name>.bsp becomes the read-only mapname. */
  CHECK(strcmp(qa_cvar_registry_variable_string(f.provider.cvars, "mapname"), "q3dm1") == 0);
  qa_qvm_mod_provider_initialize(&f.provider);
  CHECK(script.result_count == 4);
  CHECK(script.results[0] == 0);
  CHECK(log_state.server_commands == 1 && strcmp(log_state.command_text, "cp hello") == 0);
  CHECK(log_state.traces == 1 && log_state.last_mask == 1 && strcmp(log_state.last_shape, "box") == 0);
  CHECK(log_state.pass_actor_null);
  /* writeQvmTrace: fraction at 8, endpos at 12, plane normal at 24,
   * surfaceFlags 44, contents 48, entityNum 52 (world miss -> 1022). */
  CHECK(get_f32(4300 + 8) == 0.5f);
  CHECK(get_f32(4300 + 12) == 5.5f && get_f32(4300 + 20) == 16.5f);
  CHECK(get_f32(4300 + 32) == 1.0f);
  CHECK(get_f32(4300 + 36) == 8.0f);
  CHECK(get_i32(4300 + 44) == 4 && get_i32(4300 + 48) == 1);
  CHECK(get_i32(4300 + 52) == 1022);
  CHECK(script.results[2] == 33 && log_state.contents == 1);
  CHECK(script.results[3] == 1 && log_state.area_first == 3 && log_state.area_second == 4);
  fixture_destroy(&f);
}

static void portal_without_topology(void) {
  Fixture f;
  fixture_init(&f);
  script.portal_call = true;
  if (!fixture_provider(&f)) return;
  qa_qvm_mod_provider_initialize(&f.provider);
}

int main(void) {
  test_engine_syscalls();
  /* No SharedSceneQueries topology: TS "QVM portal state requires
   * destination topology services". */
  CHECK_ABORTS(portal_without_topology());
  printf("test_qvm_provider_depth: %d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
