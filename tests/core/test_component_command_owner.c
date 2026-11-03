/*
 * Port of tests/core/component-command-owner.test.ts, first case ("component
 * console scripts use their original cvars and commands and cannot outlive
 * the consumer"). The TS promise script read is a PENDING read whose poll
 * settles once the test resolves it. The second case (SeatInput, binding and
 * discovery commands over a component config) is not ported here.
 * GPL-2.0-or-later.
 */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/contracts/identity.h"
#include "qa/core/commands/index.h"
#include "qa/core/cvars/index.h"

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

typedef struct pending_script {
  bool resolved;
  const char *text;
  int polls;
  int cancels;
} pending_script;

static qa_script_read_result poll_script(void *ctx) {
  pending_script *script = ctx;
  script->polls++;
  qa_script_read_result result;
  memset(&result, 0, sizeof result);
  if (!script->resolved) {
    result.kind = QA_SCRIPT_READ_PENDING;
    return result;
  }
  result.kind = QA_SCRIPT_READ_READY;
  result.has_text = true;
  result.text = strdup(script->text);
  return result;
}
static void cancel_script(void *ctx) { ((pending_script *)ctx)->cancels++; }

typedef struct owner_state {
  qa_cvar_registry *local;
  pending_script script;
  char commands[4][64];
  int command_count;
  int escaped;
} owner_state;

static qa_cvar_registry *producer_cvars(void *ctx, qa_command_context source) {
  (void)source;
  return ((owner_state *)ctx)->local;
}
static qa_script_read_result producer_read(void *ctx, const char *name, qa_command_context source) {
  (void)name;
  (void)source;
  owner_state *state = ctx;
  qa_script_read_result result;
  memset(&result, 0, sizeof result);
  result.kind = QA_SCRIPT_READ_PENDING;
  result.poll_ctx = &state->script;
  result.poll = poll_script;
  result.cancel = cancel_script;
  return result;
}
static void producer_command(void *ctx, qa_command_invocation *command) {
  owner_state *state = ctx;
  if (state->command_count < 4) snprintf(state->commands[state->command_count], 64, "%s", command->raw);
  state->command_count++;
}
static bool not_engine(void *ctx, const char *name) {
  (void)ctx;
  (void)name;
  return false;
}
static qa_script_read_result escaped_read(void *ctx, const char *name, qa_command_context source) {
  (void)name;
  (void)source;
  ((owner_state *)ctx)->escaped++;
  qa_script_read_result result;
  memset(&result, 0, sizeof result);
  result.kind = QA_SCRIPT_READ_FAILED;
  result.error_message = strdup("Component script escaped its mounted files");
  return result;
}
static void escaped_forward(void *ctx, qa_command_invocation *command) {
  (void)command;
  ((owner_state *)ctx)->escaped++;
}

static bool value_is(qa_cvar_registry *cvars, const char *name, const char *want) {
  char *value = qa_cvar_registry_variable_string(cvars, name);
  bool same = value != NULL && strcmp(value, want) == 0;
  free(value);
  return same;
}
static void reg(qa_cvar_registry *cvars, const char *name, const char *value) {
  qa_cvar_snapshot snapshot;
  if (qa_cvar_registry_register(cvars, name, value, 0, &snapshot)) qa_cvar_read_free(&snapshot);
}

int main(void) {
  qa_identity_owner ids;
  if (!qa_identity_owner_create(&ids, "component-command-owner")) return 1;
  qa_command_context context;
  memset(&context, 0, sizeof context);
  context.session = ids.session;
  context.origin.kind = QA_COMMAND_ORIGIN_LOCAL_CONSOLE;
  static const int instance = 0;
  qa_command_producer producer;
  memset(&producer, 0, sizeof producer);
  producer.kind = "client-module";
  producer.instance = &instance;
  producer.module.id = "mod:client";
  producer.module.artifact_path = "vm/cgame.qvm";
  producer.module.digest = "sha256:0000000000000000000000000000000000000000000000000000000000000000";
  producer.module.revision = "1";
  qa_command_context source = context;
  source.producer = &producer;

  qa_cvar_registry_options world_options;
  memset(&world_options, 0, sizeof world_options);
  world_options.dialect = QA_COMMAND_DIALECT_Q1_NETQUAKE;
  world_options.context = context;
  qa_cvar_registry *world = qa_cvar_registry_create(&world_options);
  qa_cvar_registry_options local_options = world_options;
  local_options.dialect = QA_COMMAND_DIALECT_Q3;
  local_options.context = source;
  owner_state state;
  memset(&state, 0, sizeof state);
  state.local = qa_cvar_registry_create(&local_options);
  reg(world, "cg_custom", "world");
  reg(state.local, "cg_custom", "original");

  qa_command_buffer_options options;
  memset(&options, 0, sizeof options);
  options.dialect = QA_COMMAND_DIALECT_Q1_NETQUAKE;
  options.context = context;
  options.cvars = world;
  options.read_script = escaped_read;
  options.read_script_ctx = &state;
  options.forward_to_server = escaped_forward;
  options.forward_to_server_ctx = &state;
  qa_command_buffer *buffer = qa_command_buffer_create(&options);
  qa_command_producer_services services;
  memset(&services, 0, sizeof services);
  services.ctx = &state;
  services.cvars = producer_cvars;
  services.read_script = producer_read;
  services.read_script_ctx = &state;
  services.command = producer_command;
  services.engine_command = not_engine;
  int64_t release = qa_command_buffer_bind_producer(buffer, &instance, services);

  qa_command_buffer_append(buffer, "set cg_custom local; original_command; exec client.cfg\n", &source,
                           QA_COMMAND_DIALECT_Q3);
  (void)qa_command_buffer_execute(buffer);
  CHECK(value_is(state.local, "cg_custom", "local"));
  CHECK(value_is(world, "cg_custom", "world"));
  CHECK(state.command_count == 1 && strcmp(state.commands[0], " original_command") == 0);
  CHECK(qa_command_buffer_producer_pending(buffer, &instance));
  qa_command_buffer_unbind_producer(buffer, release);
  state.script.resolved = true;
  state.script.text = "set cg_custom stale; original_command\n";
  (void)qa_command_buffer_execute(buffer);
  CHECK(!qa_command_buffer_producer_pending(buffer, &instance));
  CHECK(value_is(state.local, "cg_custom", "local"));
  CHECK(state.command_count == 1);
  CHECK(state.escaped == 0);

  qa_command_buffer_destroy(buffer);
  qa_cvar_registry_destroy(state.local);
  qa_cvar_registry_destroy(world);
  qa_identity_owner_destroy(&ids);
  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
