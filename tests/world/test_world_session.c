/*
 * Port of key cases from tests/world/session/{mod-client-input,
 * mod-files,mods}.test.ts plus the clocks/resources/command
 * contracts those files exercise. TypeScript toThrow cases run in a
 * child process and must abort. GPL-2.0-or-later.
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

#include "qa/contracts/identity.h"
#include "qa/world/session.h"
#include "qa/world/session/mod_client_applications.h"
#include "qa/world/session/mod_client_command.h"
#include "qa/world/session/mod_client_input.h"
#include "qa/world/session/mod_client_input_values.h"
#include "qa/world/session/mod_client_outputs.h"
#include "qa/world/session/mod_commands.h"
#include "qa/world/session/mod_files.h"
#include "qa/world/session/mods.h"

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
      (void)prctl(PR_SET_DUMPABLE, 0); /* no coredump for expected aborts */ \
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

static qa_source_time seconds(double value) {
  qa_source_time time = {QA_SOURCE_TIME_SECONDS, value};
  return time;
}

static void test_clocks(void) {
  qa_source_clock clock;
  qa_source_clock_init(&clock, seconds(1.5), 0);
  CHECK(qa_source_clock_frame(&clock).frame == 0);
  CHECK(qa_source_clock_frame(&clock).time.value == 1.5);
  CHECK(qa_source_clock_frame(&clock).elapsed.value == 0);
  qa_frame_context frame = qa_source_clock_advance(
      &clock, seconds(0.25), QA_FRAME_PHASE_CLIENT_COMMAND);
  CHECK(frame.frame == 1);
  CHECK(frame.time.value == 1.75);
  CHECK(frame.elapsed.value == 0.25);
  CHECK(frame.phase == QA_FRAME_PHASE_CLIENT_COMMAND);
  frame = qa_source_clock_enter(&clock, QA_FRAME_PHASE_FRAME_EXIT);
  CHECK(frame.frame == 1 && frame.phase == QA_FRAME_PHASE_FRAME_EXIT);
  CHECK_ABORTS(qa_validate_time(seconds(NAN)));
  CHECK_ABORTS(qa_source_clock_advance(&clock, seconds(-1),
                                       QA_FRAME_PHASE_FRAME_ENTRY));
  qa_source_time millis = {QA_SOURCE_TIME_MILLISECONDS, 5};
  CHECK_ABORTS(qa_source_clock_advance(&clock, millis,
                                       QA_FRAME_PHASE_FRAME_ENTRY));
  CHECK_ABORTS(qa_source_clock_init(&clock, seconds(0), -1));
  CHECK_ABORTS(qa_source_clock_init(&clock, seconds(0), 0.5));
}

/* ---------- resources ---------- */

static int close_order[4];
static int close_count;
static void record_close(void *ctx) {
  close_order[close_count++] = *(int *)ctx;
}
static void resource_close(void *ctx) { record_close(ctx); }

static void test_resources(void) {
  qa_resource_scope *scope = qa_resource_scope_create("test scope");
  CHECK(scope != NULL);
  int one = 1, two = 2, three = 3;
  close_count = 0;
  qa_resource_scope_defer(scope, record_close, &one);
  qa_session_resource owned = {&two, resource_close};
  CHECK(qa_resource_scope_own(scope, owned).ctx == &two);
  qa_resource_scope_defer(scope, record_close, &three);
  CHECK(!qa_resource_scope_is_closed(scope));
  qa_resource_scope_close(scope);
  CHECK(close_count == 3);
  CHECK(close_order[0] == 3 && close_order[1] == 2 && close_order[2] == 1);
  qa_resource_scope_close(scope); /* idempotent */
  CHECK(close_count == 3);
  CHECK_ABORTS(qa_resource_scope_assert_open(scope));
  CHECK_ABORTS(qa_resource_scope_defer(scope, record_close, &one));
  qa_resource_scope_destroy(scope);
}

/* ---------- mod client command ---------- */

static qa_user_command q3_command(void) {
  qa_user_command command;
  memset(&command, 0, sizeof command);
  command.kind = QA_USER_COMMAND_Q3;
  command.as.q3.server_time_milliseconds = 120;
  command.as.q3.angle_words.values[0] = 12;
  command.as.q3.angle_words.values[1] = 13;
  command.as.q3.angle_words.values[2] = 14;
  command.as.q3.buttons = 129;
  command.as.q3.weapon = 7;
  command.as.q3.forward_move = 99;
  command.as.q3.right_move = -31;
  command.as.q3.up_move = 127;
  return command;
}

static void test_command(void) {
  qa_user_command q3 = q3_command();
  CHECK(qa_mod_client_move_scale(q3) == 127);
  CHECK(qa_mod_client_command_impulse(q3) == 0);
  qa_user_command q1;
  memset(&q1, 0, sizeof q1);
  q1.kind = QA_USER_COMMAND_Q1;
  q1.as.q1.impulse = 5;
  CHECK(qa_mod_client_move_scale(q1) == 320);
  CHECK(qa_mod_client_command_impulse(q1) == 5);
  /* Consume-style scalars from the TS compose test. */
  qa_user_command out =
      qa_mod_client_command_scalar(q3, "jump", 0);
  CHECK(out.as.q3.up_move == 0);
  out = qa_mod_client_command_scalar(q3, "attack", 0);
  CHECK(out.as.q3.buttons == 128);
  out = qa_mod_client_command_scalar(q3, "forward-move", 0.5);
  CHECK(out.as.q3.forward_move == 63); /* trunc(0.5 * 127) */
  out = qa_mod_client_command_scalar(q1, "impulse", 200);
  CHECK(out.as.q1.impulse == 200);
  CHECK_ABORTS(qa_mod_client_command_scalar(q1, "impulse", 256));
  CHECK_ABORTS(qa_mod_client_command_scalar(q3, "attack", NAN));
  CHECK_ABORTS(qa_mod_client_command_scalar(q3, "forward-move", 2));
  /* Aim: q3 angle words advance by trunc(delta * 65536 / 360). */
  qa_vec3 aim = {0, 90, 0}, zero = {0, 0, 0};
  qa_user_command aimed = qa_mod_client_command_aim(q3, aim, zero);
  CHECK(aimed.as.q3.angle_words.values[1] == 13 + 16384);
  /* Output changes: consume expands to zeroing sets. */
  const qa_mod_client_input inputs[2] = {"jump", "attack"};
  qa_mod_client_input_output consume;
  memset(&consume, 0, sizeof consume);
  consume.kind = QA_MOD_CLIENT_INPUT_OUTPUT_CONSUME;
  consume.inputs = inputs;
  consume.input_count = 2;
  CHECK(qa_mod_client_output_change_count(consume) == 2);
  qa_mod_client_input_output change =
      qa_mod_client_output_change_at(consume, 1);
  CHECK(change.kind == QA_MOD_CLIENT_INPUT_OUTPUT_SET);
  CHECK(strcmp(change.input, "attack") == 0 && change.number_value == 0);
}

/* ---------- applications + input values ---------- */

struct app_fixture {
  qa_identity_owner owner;
  qa_actor_id actor;
  qa_client_id client;
  bool live;
  qa_mod_client_applications *apps;
};
static bool app_live(void *ctx, qa_mod_client_identity identity) {
  (void)identity;
  return ((struct app_fixture *)ctx)->live;
}
static qa_mod_client_application_input app_input(
    struct app_fixture *fixture, qa_user_command command) {
  qa_mod_client_application_input input;
  memset(&input, 0, sizeof input);
  input.identity.client = fixture->client;
  input.identity.actor = fixture->actor;
  input.scope = "client-command";
  input.command = command;
  input.angle_space = "absolute";
  input.frame.frame = 1;
  input.frame.phase = QA_FRAME_PHASE_CLIENT_COMMAND;
  input.frame.time = seconds(1);
  input.frame.elapsed = seconds(0.02);
  return input;
}
static void fixture_init(struct app_fixture *fixture) {
  CHECK(qa_identity_owner_create(&fixture->owner, "component-input"));
  fixture->actor = qa_identity_owner_actor(&fixture->owner, 1, 0);
  fixture->client = qa_identity_owner_client(&fixture->owner, 0, 0);
  fixture->live = true;
  fixture->apps = qa_mod_client_applications_create(app_live, fixture);
  CHECK(fixture->apps != NULL);
}
static void fixture_destroy(struct app_fixture *fixture) {
  qa_mod_client_applications_destroy(fixture->apps);
  qa_identity_owner_destroy(&fixture->owner);
}

struct compose_state {
  qa_mod_client_application *first;
  int stage;
};
static void consume_listener(void *ctx,
                             qa_mod_client_application_event *event) {
  struct compose_state *state = ctx;
  if (!event->is_before) return;
  state->first = event->application;
  const qa_mod_client_input inputs[3] = {"jump", "attack", "impulse"};
  qa_mod_client_input_output output;
  memset(&output, 0, sizeof output);
  output.kind = QA_MOD_CLIENT_INPUT_OUTPUT_CONSUME;
  output.inputs = inputs;
  output.input_count = 3;
  event->output(event->output_ctx, output);
}
static void forward_listener(void *ctx,
                             qa_mod_client_application_event *event) {
  struct compose_state *state = ctx;
  if (!event->is_before) return;
  CHECK(event->application == state->first);
  CHECK(event->application->command.as.q3.up_move == 0);
  CHECK(event->application->command.as.q3.buttons == 128);
  CHECK(event->application->controls_impulse == 0);
  CHECK(event->application->has_arsenal &&
        event->application->arsenal.impulse == 0);
  qa_mod_client_input_output output;
  memset(&output, 0, sizeof output);
  output.kind = QA_MOD_CLIENT_INPUT_OUTPUT_SET;
  output.input = "forward-move";
  output.number_value = 0.5;
  event->output(event->output_ctx, output);
  state->stage = 1;
}

static void test_applications(void) {
  struct app_fixture fixture;
  fixture_init(&fixture);
  struct compose_state state = {NULL, 0};
  qa_mod_client_applications_subscribe(fixture.apps, consume_listener,
                                       &state);
  qa_mod_client_applications_subscribe(fixture.apps, forward_listener,
                                       &state);
  qa_user_command command = q3_command();
  qa_arsenal_intent arsenal;
  memset(&arsenal, 0, sizeof arsenal);
  arsenal.provider = "q1:weapons";
  arsenal.weapon = NULL;
  arsenal.has_impulse = true;
  arsenal.impulse = 9;
  qa_actor_command actor_command;
  memset(&actor_command, 0, sizeof actor_command);
  actor_command.actor = fixture.actor;
  actor_command.sequence = 4;
  actor_command.source.kind = QA_COMMAND_SOURCE_REMOTE_CLIENT;
  actor_command.source.client = fixture.client;
  actor_command.command = command;
  actor_command.arsenal = &arsenal;
  qa_mod_client_command_receipt receipt;
  receipt.input = actor_command;
  receipt.time = seconds(1);
  qa_mod_client_application_input input = app_input(&fixture, command);
  input.has_arsenal = true;
  input.arsenal = &arsenal;
  input.accepted = &receipt;
  qa_mod_client_application *app =
      qa_mod_client_applications_begin(fixture.apps, input, NULL, NULL);
  CHECK(app != NULL);
  CHECK(state.stage == 1);
  CHECK(app->command.as.q3.up_move == 0);
  CHECK(app->command.as.q3.buttons == 128);
  CHECK(app->command.as.q3.forward_move == 63);
  CHECK(app->accepted->input.command.as.q3.up_move == 127);
  CHECK(arsenal.impulse == 9); /* retained receipt unchanged */
  /* Input values read the effective (output-adjusted) command. */
  qa_mod_client_input_values values = qa_mod_client_input_values_of(app);
  CHECK(values.count == 10);
  CHECK(strcmp(values.entries[6].name, "impulse") == 0);
  CHECK(values.entries[6].value.number_value == 0);
  CHECK(values.entries[7].value.number_value == 63.0 / 127.0);
  CHECK(values.entries[1].value.number_value == 1);
  qa_mod_client_applications_finish(fixture.apps, app, false);
  CHECK(qa_mod_client_applications_checkpoint(fixture.apps) == 1);
  qa_mod_client_application_dispose(app);
  fixture_destroy(&fixture);
}

/* ---------- subscribe mod client input ---------- */

struct services_stub {
  struct app_fixture *fixture;
  qa_mod_client_identity identity;
};
static const qa_mod_client_identity *stub_clients(void *ctx,
                                                  size_t *out_count) {
  struct services_stub *stub = ctx;
  *out_count = stub->fixture->live ? 1 : 0;
  return &stub->identity;
}
static bool stub_actor(void *ctx, qa_client_id client, qa_actor_id *out) {
  struct services_stub *stub = ctx;
  if (!stub->fixture->live ||
      !qa_same_client(client, stub->fixture->client))
    return false;
  *out = stub->fixture->actor;
  return true;
}
static bool stub_for_actor(void *ctx, qa_actor_id actor,
                           qa_client_id *out) {
  struct services_stub *stub = ctx;
  if (!stub->fixture->live || !qa_same_actor(actor, stub->fixture->actor))
    return false;
  *out = stub->fixture->client;
  return true;
}
static const char *stub_userinfo(void *ctx, qa_client_id client) {
  (void)ctx;
  (void)client;
  return "";
}
static void stub_set_userinfo(void *ctx, qa_client_id client,
                              const char *value) {
  (void)ctx;
  (void)client;
  (void)value;
}
static const qa_mod_client_command_receipt *stub_command(
    void *ctx, qa_client_id client) {
  (void)ctx;
  (void)client;
  return NULL;
}
static void stub_drop(void *ctx, qa_client_id client, const char *reason,
                      qa_content_id content) {
  (void)ctx;
  (void)client;
  (void)reason;
  (void)content;
}
static void stub_noop_unsubscribe(void *ctx) { (void)ctx; }
static qa_mod_client_unsubscribe stub_subscribe(
    void *ctx, qa_mod_client_listener_fn listener, void *listener_ctx) {
  (void)ctx;
  (void)listener;
  (void)listener_ctx;
  qa_mod_client_unsubscribe result = {NULL, stub_noop_unsubscribe};
  return result;
}
struct stub_unsub {
  qa_mod_client_applications *apps;
  int64_t id;
};
static void stub_unsub_cb(void *ctx) {
  struct stub_unsub *unsub = ctx;
  qa_mod_client_applications_unsubscribe(unsub->apps, unsub->id);
  free(unsub);
}
static qa_mod_client_unsubscribe stub_subscribe_application(
    void *ctx, qa_mod_client_application_listener_fn listener,
    void *listener_ctx) {
  struct services_stub *stub = ctx;
  struct stub_unsub *unsub = malloc(sizeof *unsub);
  unsub->apps = stub->fixture->apps;
  unsub->id = qa_mod_client_applications_subscribe(stub->fixture->apps,
                                                   listener, listener_ctx);
  qa_mod_client_unsubscribe result = {unsub, stub_unsub_cb};
  return result;
}

struct nested_state {
  struct app_fixture *fixture;
  int current;
  char log[256];
};
static void nested_log(struct nested_state *state, const char *text) {
  if (state->log[0] != '\0') strcat(state->log, ",");
  strcat(state->log, text);
}
struct nested_cleanup {
  struct nested_state *state;
  int previous;
};
static void nested_close(void *ctx) {
  struct nested_cleanup *cleanup = ctx;
  char entry[32];
  snprintf(entry, sizeof entry, "close:%d", cleanup->state->current);
  nested_log(cleanup->state, entry);
  cleanup->state->current = cleanup->previous;
  free(cleanup);
}
static void nested_open(void *ctx, qa_mod_client_application *application,
                        void (**cleanup)(void *), void **cleanup_ctx) {
  struct nested_state *state = ctx;
  struct nested_cleanup *node = malloc(sizeof *node);
  node->state = state;
  node->previous = state->current;
  state->current = (int)application->invocation;
  *cleanup = nested_close;
  *cleanup_ctx = node;
}
static void nested_invoke(void *ctx, const void *call,
                          qa_mod_client_application *application) {
  struct nested_state *state = ctx;
  const char *name = *(const char *const *)call;
  CHECK(state->current == (int)application->invocation);
  char entry[32];
  snprintf(entry, sizeof entry, "%s:%d", name, state->current);
  nested_log(state, entry);
  if (strcmp(name, "enter") == 0 && state->current == 1) {
    qa_mod_client_application_input input =
        app_input(state->fixture, q3_command());
    qa_mod_client_application *nested = qa_mod_client_applications_begin(
        state->fixture->apps, input, NULL, NULL);
    qa_mod_client_applications_finish(state->fixture->apps, nested, false);
    qa_mod_client_application_dispose(nested);
    CHECK(state->current == 1);
  }
  if (strcmp(name, "finish") == 0 && state->current == 1) {
    state->fixture->live = false;
    qa_mod_client_applications_release(state->fixture->apps,
                                       state->fixture->actor);
  }
}

static void test_subscribe_input(void) {
  struct app_fixture fixture;
  fixture_init(&fixture);
  struct services_stub stub = {&fixture, {fixture.client, fixture.actor}};
  qa_mod_client_services services;
  memset(&services, 0, sizeof services);
  services.ctx = &stub;
  services.maximum = 1;
  services.clients = stub_clients;
  services.for_actor = stub_for_actor;
  services.actor = stub_actor;
  services.userinfo = stub_userinfo;
  services.set_userinfo = stub_set_userinfo;
  services.command = stub_command;
  services.drop = stub_drop;
  services.subscribe = stub_subscribe;
  services.subscribe_application = stub_subscribe_application;
  const char *enter_calls[1] = {"enter"};
  const char *finish_calls[2] = {"finish", "late"};
  qa_mod_client_input_binding bindings[2];
  memset(bindings, 0, sizeof bindings);
  bindings[0].scope = "client-command";
  bindings[0].calls = enter_calls;
  bindings[0].call_count = 1;
  bindings[0].call_elem_size = sizeof(const char *);
  bindings[0].phase = "before";
  bindings[1].scope = "client-command";
  bindings[1].calls = finish_calls;
  bindings[1].call_count = 2;
  bindings[1].call_elem_size = sizeof(const char *);
  bindings[1].phase = "after";
  struct nested_state state = {&fixture, 0, ""};
  qa_mod_client_input_operations operations;
  memset(&operations, 0, sizeof operations);
  operations.ctx = &state;
  operations.open = nested_open;
  operations.invoke = nested_invoke;
  qa_mod_client_unsubscribe stop = qa_subscribe_mod_client_input(
      &services, bindings, 2, operations);
  qa_mod_client_application_input input =
      app_input(&fixture, q3_command());
  qa_mod_client_application *app =
      qa_mod_client_applications_begin(fixture.apps, input, NULL, NULL);
  qa_mod_client_applications_finish(fixture.apps, app, false);
  qa_mod_client_application_dispose(app);
  CHECK(strcmp(state.log,
               "enter:1,enter:2,finish:2,late:2,close:2,finish:1,close:1") ==
        0);
  CHECK(state.current == 0);
  CHECK(qa_mod_client_applications_checkpoint(fixture.apps) == 2);
  stop.invoke(stop.ctx);
  fixture_destroy(&fixture);
}

/* ---------- client outputs ---------- */

struct output_fixture {
  qa_actor_id current;
};
static bool output_live(void *ctx, qa_actor_id actor) {
  return qa_same_actor(((struct output_fixture *)ctx)->current, actor);
}
static double read_scalar(void *ctx, const void *field) {
  (void)ctx;
  return strcmp((const char *)field, "height") == 0 ? 14 : 10;
}
static qa_vec3 read_vector(void *ctx, const void *field) {
  (void)ctx;
  (void)field;
  qa_vec3 zero = {0, 0, 0};
  return zero;
}
static void check_field(void *ctx, const void *field) {
  (void)ctx;
  (void)field;
}

static void test_outputs(void) {
  qa_identity_owner owner;
  CHECK(qa_identity_owner_create(&owner, "client-output-leases"));
  qa_actor_id actor = qa_identity_owner_actor(&owner, 1, 0);
  qa_actor_id replacement = qa_identity_owner_actor(&owner, 1, 1);
  struct output_fixture fixture = {actor};
  qa_mod_client_outputs *outputs =
      qa_mod_client_outputs_create(output_live, &fixture);
  CHECK(outputs != NULL);
  qa_mod_client_output_channel view_channel[1] = {
      QA_MOD_CLIENT_OUTPUT_VIEW_OFFSET};
  qa_mod_client_output_lease lease =
      qa_mod_client_outputs_claim(outputs, "mod:camera", view_channel, 1);
  CHECK_ABORTS(
      qa_mod_client_outputs_claim(outputs, "mod:other", view_channel, 1));
  qa_mod_client_mode_value mode_values[1] = {
      {2, QA_MOD_CLIENT_MOVEMENT_NOCLIP}};
  qa_mod_client_output_declaration declarations[2];
  memset(declarations, 0, sizeof declarations);
  declarations[0].kind = QA_MOD_CLIENT_OUTPUT_VIEW_OFFSET;
  declarations[0].height = "height";
  declarations[1].kind = QA_MOD_CLIENT_OUTPUT_MOVEMENT_MODE;
  declarations[1].field = "mode";
  declarations[1].has_mask = true;
  declarations[1].mask = 7;
  declarations[1].mode_values = mode_values;
  declarations[1].mode_value_count = 1;
  qa_mod_client_output values[2];
  qa_read_mod_client_outputs(declarations, 2, read_scalar, read_vector,
                             NULL, values);
  CHECK(values[0].view_offset.z == 14);
  CHECK(values[1].mode == QA_MOD_CLIENT_MOVEMENT_NOCLIP);
  lease.publish(lease.ctx, actor, values, 1);
  qa_mod_client_movement_outputs read;
  CHECK(qa_mod_client_outputs_read(outputs, actor, &read));
  CHECK(read.view_offset != NULL && read.view_offset->z == 14);
  qa_mod_client_output_declaration duplicates[2] = {declarations[0],
                                                    declarations[0]};
  CHECK_ABORTS(qa_validate_mod_client_outputs(duplicates, 2, check_field,
                                              check_field, NULL));
  qa_mod_client_outputs_release(outputs, actor);
  fixture.current = replacement;
  CHECK(!qa_mod_client_outputs_read(outputs, replacement, &read));
  CHECK_ABORTS(lease.publish(lease.ctx, actor, values, 1));
  lease.publish(lease.ctx, replacement, values, 1);
  lease.close(lease.ctx);
  CHECK(!qa_mod_client_outputs_read(outputs, replacement, &read));
  qa_mod_client_output_lease next =
      qa_mod_client_outputs_claim(outputs, "mod:other", view_channel, 1);
  next.close(next.ctx);
  qa_mod_client_outputs_destroy(outputs);
  qa_identity_owner_destroy(&owner);
}

/* ---------- mod files + mod command text ---------- */

static void test_files_and_command_text(void) {
  qa_mod_user_files *files = qa_mod_user_files_create("/tmp/component-files");
  CHECK(files != NULL);
  qa_mod_selection first = {"q3-source", "rules/a"};
  qa_mod_selection second = {"q3-source", "rules/b"};
  qa_user_file_store *first_store = qa_mod_user_files_for(files, first);
  qa_user_file_store *second_store = qa_mod_user_files_for(files, second);
  CHECK(strcmp(first_store->root,
               "/tmp/component-files/.mods/q3-source/rules%2Fa") == 0);
  CHECK(first_store != second_store);
  CHECK(qa_mod_user_files_for(files, first) == first_store);
  qa_mod_user_files_destroy(files);

  qa_mod_selection parsed;
  char *text = NULL;
  CHECK(qa_read_mod_command("modcmd q3-source/rules say hi", &parsed,
                            &text));
  CHECK(strcmp(parsed.product, "q3-source") == 0);
  CHECK(strcmp(parsed.id, "rules") == 0);
  CHECK(strcmp(text, "say hi") == 0);
  qa_mod_selection_free(&parsed);
  free(text);
  qa_mod_selection unused_sel;
  char *unused_text = NULL;
  CHECK_ABORTS(qa_read_mod_command("modcmd onlyone", &unused_sel,
                                   &unused_text));
}

/* ---------- session ---------- */

struct sim_state {
  qa_world_snapshot snapshot;
  qa_simulation_event events[2];
  int steps;
  bool closed;
};
static qa_simulation_output sim_step(void *ctx, qa_input_batch input) {
  (void)input;
  struct sim_state *state = ctx;
  state->steps++;
  qa_simulation_output output = {state->snapshot, state->events, 2};
  return output;
}
static qa_save_image sim_checkpoint(void *ctx) {
  (void)ctx;
  qa_save_image image;
  memset(&image, 0, sizeof image);
  return image;
}
static void sim_close(void *ctx) { ((struct sim_state *)ctx)->closed = true; }

struct presentation_state {
  qa_seat_client_state seat_state;
  int received;
  size_t received_count;
};
static void present_receive(void *ctx, const qa_simulation_event *events,
                            size_t count) {
  (void)events;
  struct presentation_state *state = ctx;
  state->received++;
  state->received_count = count;
}
static qa_render_frame present_frame(void *ctx, qa_world_snapshot snapshot) {
  (void)ctx;
  (void)snapshot;
  qa_render_frame frame;
  memset(&frame, 0, sizeof frame);
  return frame;
}
static void present_render(void *ctx, qa_render_frame frame,
                           qa_renderer_backend backend) {
  (void)ctx;
  (void)frame;
  (void)backend;
}
static void present_cleanup(void *ctx) { *(bool *)ctx = true; }

static void test_session(void) {
  qa_identity_owner owner;
  CHECK(qa_identity_owner_create(&owner, "session-test"));
  qa_session_mode mode = {QA_SESSION_MODE_LOCAL};
  qa_engine_session *session = qa_engine_session_create(&owner, mode);
  CHECK(session != NULL);
  qa_session_client *client = qa_engine_session_create_client(session, 0);
  qa_session_seat *seat = qa_engine_session_create_seat(session, 0, client);
  CHECK(qa_engine_session_client_at(session, 0) == client);
  CHECK(qa_engine_session_client_at(session, 9) == NULL);
  CHECK_ABORTS(qa_engine_session_create_client(session, 0));

  struct sim_state sim;
  memset(&sim, 0, sizeof sim);
  sim.snapshot.session = owner.session;
  sim.events[0].audience.kind = "world";
  sim.events[1].audience.kind = "client";
  sim.events[1].audience.client =
      qa_identity_owner_client(&owner, 3, 0); /* another client */
  qa_simulation simulation;
  memset(&simulation, 0, sizeof simulation);
  simulation.ctx = &sim;
  simulation.session = owner.session;
  simulation.step = sim_step;
  simulation.checkpoint = sim_checkpoint;
  simulation.close = sim_close;
  qa_world_lifetime *world =
      qa_engine_session_attach_world(session, simulation);
  CHECK(world == qa_engine_session_world(session));

  struct presentation_state presented;
  memset(&presented, 0, sizeof presented);
  presented.seat_state.seat = qa_session_seat_id(seat);
  presented.seat_state.client = qa_session_client_id(client);
  qa_seat_presentation presentation;
  memset(&presentation, 0, sizeof presentation);
  presentation.ctx = &presented;
  presentation.state = presented.seat_state;
  presentation.receive = present_receive;
  presentation.frame = present_frame;
  presentation.render = present_render;
  bool cleaned = false;
  qa_session_seat_attach_presentation(seat, presentation, present_cleanup,
                                      &cleaned);
  qa_input_batch input = {16, NULL, 0};
  qa_simulation_output output = qa_engine_session_step(session, input);
  CHECK(sim.steps == 1);
  CHECK(output.event_count == 2);
  /* The seat receives only the world-audience event. */
  CHECK(presented.received == 1 && presented.received_count == 1);
  qa_simulation_event seat_event;
  memset(&seat_event, 0, sizeof seat_event);
  seat_event.audience.kind = "seat";
  seat_event.audience.seat = qa_session_seat_id(seat);
  CHECK(qa_event_targets_seat(seat_event, qa_session_seat_id(seat),
                              qa_session_client_id(client)));
  qa_session_client_disconnect(client);
  CHECK(cleaned); /* disconnect clears seat presentations */
  qa_engine_session_destroy(session);
  CHECK(sim.closed);
  qa_identity_owner_destroy(&owner);
}

/* ---------- session mods ---------- */

static void mod_validate(void *ctx, qa_mod_private_checkpoint state) {
  (void)ctx;
  (void)state;
}
static void mod_next_frame(void *ctx) { (void)ctx; }
static qa_prepared_mod prepared_stub(qa_mod_selection selection,
                                     const char *hud, bool view) {
  qa_prepared_mod prepared;
  memset(&prepared, 0, sizeof prepared);
  prepared.has_client_presentation = true;
  prepared.client_presentation.hud = hud;
  prepared.client_presentation.view = view;
  prepared.description.selection = selection;
  prepared.description.source.provider = "q3:official";
  prepared.description.source.content = "q3:classic:baseq3:test";
  prepared.description.title = "stub";
  prepared.description.source_title = "Quake III";
  prepared.description.purpose = "addition";
  prepared.description.is_available = true;
  prepared.identity.selection = selection;
  prepared.identity.source = prepared.description.source;
  prepared.validate_state = mod_validate;
  return prepared;
}

static void test_mods(void) {
  qa_mod_operations operations;
  memset(&operations, 0, sizeof operations);
  qa_session_mods_options empty;
  memset(&empty, 0, sizeof empty);
  empty.operations = operations;
  empty.next_frame = mod_next_frame;
  qa_session_mods *mods = qa_session_mods_open(&empty, NULL, NULL);
  CHECK(mods != NULL);
  size_t enabled = 99;
  qa_session_mods_enabled(mods, &enabled);
  CHECK(enabled == 0);
  CHECK(!qa_session_mods_owns(mods, "mod:nope"));
  qa_session_mods_destroy(mods);

  /* Two HUD replacements conflict before any initialize call. */
  qa_mod_selection first = {"q3-one", "amount"};
  qa_mod_selection second = {"q3-two", "amount"};
  qa_prepared_mod prepared[2] = {prepared_stub(first, "replace", false),
                                 prepared_stub(second, "replace", false)};
  qa_mod_selection both[2] = {first, second};
  qa_session_mods_options options;
  memset(&options, 0, sizeof options);
  options.prepared = prepared;
  options.prepared_count = 2;
  options.enabled = both;
  options.enabled_count = 2;
  options.operations = operations;
  options.next_frame = mod_next_frame;
  CHECK_ABORTS(qa_session_mods_open(&options, NULL, NULL));
  /* Saved + travel together are distinct lifecycle operations. */
  qa_mod_session_checkpoint saved;
  memset(&saved, 0, sizeof saved);
  saved.version = 1;
  qa_mod_travel_checkpoint travel;
  memset(&travel, 0, sizeof travel);
  travel.version = 1;
  CHECK_ABORTS(qa_session_mods_open(&empty, &saved, &travel));
}

int main(void) {
  test_clocks();
  test_resources();
  test_command();
  test_applications();
  test_subscribe_input();
  test_outputs();
  test_files_and_command_text();
  test_session();
  test_mods();
  if (failures != 0) {
    fprintf(stderr, "%d of %d checks failed\n", failures, checks);
    return 1;
  }
  printf("world session: %d checks passed\n", checks);
  return 0;
}
