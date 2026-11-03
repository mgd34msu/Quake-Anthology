/*
 * Confirmation-gate full-read checks for src/world (gate-world2 pass):
 * session.ts closeAll sites and repeated closes, mod-files path.resolve,
 * observers kept past their call (pickup protection, client outputs, mod
 * initialization), combat rebind identity, match objective identity and
 * original-pickup scope release. Expected values come from
 * tests/world/oracles/gate_world2.ts run under bun (output in gate_world2.txt).
 * GPL-2.0-or-later.
 */
#include <malloc.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "qa/contracts/identity.h"
#include "qa/foundation/support.h"
#include "qa/world/actors/callbacks.h"
#include "qa/world/actors/registry.h"
#include "qa/world/gameplay/authority.h"
#include "qa/world/gameplay/original_pickups.h"
#include "qa/world/handles.h"
#include "qa/world/session/mod_client_outputs.h"
#include "qa/world/session/mod_files.h"
#include "qa/world/session/mod_match.h"
#include "qa/world/session/mods.h"
#include "qa/world/session/session.h"

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

/* "none" or "<name>|<message>", as the oracle prints. */
static char outcome[2200];
static const char *attempt(void (*fn)(void *), void *ctx) {
  qa_caught_error caught;
  if (qa_try(fn, ctx, &caught)) return "none";
  snprintf(outcome, sizeof outcome, "%s|%s", caught.name, caught.message);
  return outcome;
}

/* ---------- 1. session.ts closeAll ---------- */

static char close_log[512];
static void log_name(const char *name) {
  if (close_log[0] != '\0') strcat(close_log, ",");
  strcat(close_log, name);
}
static void thrower(void *ctx) {
  log_name(ctx);
  qa_raise("Error", 0, 0, "%s", (const char *)ctx);
}
static void sim_close_throws(void *ctx) {
  log_name(ctx);
  qa_raise("Error", 0, 0, "%s close", (const char *)ctx);
}
static qa_simulation_output sim_step(void *ctx, qa_input_batch input) {
  (void)ctx;
  (void)input;
  qa_simulation_output output;
  memset(&output, 0, sizeof output);
  return output;
}
static qa_simulation make_sim(qa_session_id session, const char *name) {
  qa_simulation simulation;
  memset(&simulation, 0, sizeof simulation);
  simulation.ctx = (void *)name;
  simulation.session = session;
  simulation.step = sim_step;
  simulation.close = sim_close_throws;
  return simulation;
}
static void present_receive(void *ctx, const qa_simulation_event *events, size_t count) {
  (void)ctx;
  (void)events;
  (void)count;
}
static qa_render_frame present_frame(void *ctx, qa_world_snapshot snapshot) {
  (void)ctx;
  (void)snapshot;
  qa_render_frame frame;
  memset(&frame, 0, sizeof frame);
  return frame;
}
static void present_render(void *ctx, qa_render_frame frame, qa_renderer_backend backend) {
  (void)ctx;
  (void)frame;
  (void)backend;
}
static void resource_close(void *ctx) {
  qa_session_resource *resource = ctx;
  resource->close(resource->ctx);
}
struct client_call {
  qa_engine_session *session;
  qa_session_client *client;
};
static void call_disconnect(void *ctx) { qa_session_client_disconnect(((struct client_call *)ctx)->client); }
static void call_clear_world(void *ctx) { qa_session_client_clear_world(((struct client_call *)ctx)->client); }
static void call_close_client(void *ctx) {
  struct client_call *call = ctx;
  qa_engine_session_close_client(call->session, qa_session_client_id(call->client));
}
static void call_session_close(void *ctx) { qa_engine_session_close(ctx); }

static void check_close(const char *label, void (*fn)(void *), void *ctx, const char *expected,
                        const char *expected_log) {
  close_log[0] = '\0';
  const char *got = attempt(fn, ctx);
  bool ok = strcmp(got, expected) == 0 && strcmp(close_log, expected_log) == 0;
  if (!ok) fprintf(stderr, "%s: got %s [%s], want %s [%s]\n", label, got, close_log, expected, expected_log);
  CHECK(ok);
}

static void test_session_close_all(void) {
  qa_identity_owner owner;
  CHECK(qa_identity_owner_create(&owner, "gate-world2"));
  qa_session_mode mode = {QA_SESSION_MODE_LOCAL};
  qa_engine_session *session = qa_engine_session_create(&owner, mode);
  qa_session_client *client = qa_engine_session_create_client(session, 0);
  qa_session_seat *seat = qa_engine_session_create_seat(session, 0, client);
  (void)qa_engine_session_attach_world(session, make_sim(owner.session, "sim1"));
  qa_resource_scope_defer(qa_session_client_world_resources(client), thrower, "client-world");
  qa_seat_presentation presentation;
  memset(&presentation, 0, sizeof presentation);
  presentation.state.seat = qa_session_seat_id(seat);
  presentation.state.client = qa_session_client_id(client);
  presentation.receive = present_receive;
  presentation.frame = present_frame;
  presentation.render = present_render;
  qa_session_seat_attach_presentation(seat, presentation, thrower, "presentation");
  qa_world_lifetime *world = NULL;
  qa_session_client_change no_clients = {NULL, 0, NULL, 0};
  qa_session_seat_change no_seats = {NULL, 0, NULL, 0};
  qa_session_resource retired = qa_engine_session_replace_world(
      session, make_sim(owner.session, "sim2"), NULL, 0, no_clients, no_seats, &world);
  check_close("retired_close", resource_close, &retired,
              "AggregateError|Retired world shutdown failed", "sim1,presentation,client-world");
  /* The bundle freed its items: a second close must find nothing (TS no-op). */
  check_close("retired_close_again", resource_close, &retired, "none", "");

  struct client_call call = {session, client};
  qa_session_connection *connection = qa_session_client_connect(client, "loopback");
  qa_resource_scope_defer(qa_session_connection_scope(connection), thrower, "connection");
  qa_resource_scope_defer(qa_session_client_world_resources(client), thrower, "client-world2");
  check_close("disconnect", call_disconnect, &call, "AggregateError|Client disconnect failed",
              "client-world2,connection");
  qa_resource_scope_defer(qa_session_client_world_resources(client), thrower, "client-world3");
  check_close("clear_world", call_clear_world, &call, "AggregateError|Client world shutdown failed",
              "client-world3");
  qa_session_resource detached = qa_engine_session_detach_world(session);
  check_close("detach_close", resource_close, &detached, "AggregateError|World replacement failed", "sim2");
  check_close("detach_close_again", resource_close, &detached, "none", "");

  qa_session_client *client2 = qa_engine_session_create_client(session, 1);
  qa_resource_scope_defer(qa_session_client_resources(client2), thrower, "client2");
  struct client_call call2 = {session, client2};
  check_close("close_client", call_close_client, &call2, "AggregateError|Client shutdown failed", "client2");

  qa_session_client *client3 = qa_engine_session_create_client(session, 2);
  qa_resource_scope_defer(qa_session_client_resources(client3), thrower, "client3");
  qa_session_client_change removed = {NULL, 0, &client3, 1};
  qa_session_resource local = qa_engine_session_publish_local_seats(session, NULL, 0, removed, no_seats);
  check_close("local_close", resource_close, &local, "AggregateError|Retired local seats shutdown failed",
              "client3");
  check_close("local_close_again", resource_close, &local, "none", "");

  qa_session_client *client4 = qa_engine_session_create_client(session, 3);
  qa_resource_scope_defer(qa_session_client_resources(client4), thrower, "client4");
  check_close("session_close", call_session_close, session,
              "AggregateError|Failed to close Session resources", "client4");
  check_close("session_close_again", call_session_close, session, "none", "");
  /* Closed clients stay allocated until destroy (TS references keep them). */
  CHECK(qa_session_client_is_closed(client) && qa_session_client_is_closed(client4));
  qa_engine_session_destroy(session);
  qa_identity_owner_destroy(&owner);
  CHECK(qa_world_handle_open_count() == 0);
}

/* ---------- 2. mod-files ---------- */

static void test_mod_files(void) {
  static const char *const roots[4] = {"/tmp/qa/../root/./x//", "/", "rel/../dir/.", ""};
  static const char *const expected[4] = {"/tmp/root/x/.mods/pkg/a%3Ab%2Bc%2Fd", "/.mods/pkg/a%3Ab%2Bc%2Fd",
                                          "dir/.mods/pkg/a%3Ab%2Bc%2Fd", ".mods/pkg/a%3Ab%2Bc%2Fd"};
  char *cwd = getcwd(NULL, 0);
  qa_mod_selection selection = {"pkg", "a:b+c/d"};
  for (int i = 0; i < 4; i++) {
    qa_mod_user_files *files = qa_mod_user_files_create(roots[i]);
    qa_user_file_store *store = qa_mod_user_files_for(files, selection);
    const char *path = store->root;
    /* the oracle prints relative roots relative to the working directory */
    if (roots[i][0] != '/' && strncmp(path, cwd, strlen(cwd)) == 0 && path[strlen(cwd)] == '/')
      path += strlen(cwd) + 1;
    bool ok = strcmp(path, expected[i]) == 0;
    if (!ok) fprintf(stderr, "mod_files %s: got %s\n", roots[i], path);
    CHECK(ok);
    /* A second selection reallocates the store table: the first pointer stays valid. */
    qa_mod_selection other = {"pkg2", "id"};
    (void)qa_mod_user_files_for(files, other);
    CHECK(qa_mod_user_files_for(files, selection) == store);
    qa_mod_user_files_destroy(files);
  }
  free(cwd);
}

/* ---------- 3-4. authority observers and rebind ---------- */

static qa_protection_observer escaped_observer;
static void *keep_observer(void *ctx, qa_protection_observer observer) {
  (void)ctx;
  escaped_observer = observer;
  return NULL;
}
static void call_escaped_observer(void *ctx) {
  (void)ctx;
  qa_protection_store change;
  memset(&change, 0, sizeof change);
  escaped_observer.stored(escaped_observer.ctx, change);
}
static qa_combat_state rebind_state;
static qa_combat_state rebind_read(void *ctx) {
  (void)ctx;
  return rebind_state;
}
static void rebind_write_health(void *ctx, double health) {
  (void)ctx;
  (void)health;
}
static void rebind_write_armor(void *ctx, qa_armor_state armor) {
  (void)ctx;
  (void)armor;
}

/* ---------- 5. client output leases ---------- */

static bool always_live(void *ctx, qa_actor_id actor) {
  (void)ctx;
  (void)actor;
  return true;
}
struct lease_call {
  qa_mod_client_output_lease lease;
  qa_actor_id actor;
};
static void publish_stance(void *ctx) {
  struct lease_call *call = ctx;
  qa_mod_client_output output;
  memset(&output, 0, sizeof output);
  output.kind = QA_MOD_CLIENT_OUTPUT_STANCE;
  output.stance = true;
  call->lease.publish(call->lease.ctx, call->actor, &output, 1);
}

/* ---------- 6. match objectives ---------- */

static qa_source_match_unbind unbind_x;
static qa_source_match_player_result no_player(void *ctx, qa_actor_id actor) {
  (void)ctx;
  (void)actor;
  qa_source_match_player_result none;
  memset(&none, 0, sizeof none);
  return none;
}
static qa_source_objective_state objective_state(void) {
  qa_source_objective_state state;
  memset(&state, 0, sizeof state);
  state.stage = "s";
  return state;
}
static qa_source_objective_state read_x(void *ctx) {
  (void)ctx;
  return objective_state();
}
static qa_source_objective_state read_y_unbinds_x(void *ctx) {
  (void)ctx;
  unbind_x.invoke(unbind_x.ctx);
  return objective_state();
}
static void objective_change(void *ctx, qa_source_objective_change request) {
  (void)ctx;
  (void)request;
}
static void join_objectives(qa_mod_match_state *match, char *out) {
  size_t count = 0;
  const qa_source_objective_listing *listing = qa_mod_match_state_objectives(match, &count);
  out[0] = '\0';
  for (size_t i = 0; i < count; i++) {
    if (i != 0) strcat(out, ",");
    strcat(out, listing[i].id);
  }
}
static void call_unbind_x(void *ctx) {
  (void)ctx;
  unbind_x.invoke(unbind_x.ctx);
}

/* ---------- 7. mods ---------- */

static qa_mod_initialization kept_context;
static bool kept;
static void mod_validate(void *ctx, qa_mod_private_checkpoint state) {
  (void)ctx;
  (void)state;
}
static void mod_next_frame(void *ctx) { (void)ctx; }
static void runtime_close(void *ctx) { (void)ctx; }
static void runtime_register(void *ctx, qa_mod_registrations *registrations) {
  (void)ctx;
  (void)registrations;
}
static qa_mod_runtime mod_initialize(void *ctx, qa_mod_initialization *context) {
  kept_context = *context;
  kept = true;
  if (ctx != NULL) qa_raise("Error", 0, 0, "init failed");
  qa_mod_runtime runtime;
  memset(&runtime, 0, sizeof runtime);
  runtime.close = runtime_close;
  runtime.register_ = runtime_register;
  return runtime;
}
static void call_kept_assert(void *ctx) {
  (void)ctx;
  kept_context.assert_current(kept_context.ctx);
}
static qa_prepared_mod prepared_mod(qa_mod_selection selection, bool fails) {
  qa_prepared_mod prepared;
  memset(&prepared, 0, sizeof prepared);
  static int failing_marker;
  prepared.ctx = fails ? &failing_marker : NULL;
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
  prepared.initialize = mod_initialize;
  return prepared;
}
struct open_call {
  qa_session_mods_options *options;
  qa_session_mods *mods;
};
static void call_open(void *ctx) {
  struct open_call *call = ctx;
  call->mods = qa_session_mods_open(call->options, NULL, NULL);
}

static void test_mods(void) {
  qa_mod_selection good = {"q3-one", "amount"};
  qa_prepared_mod prepared = prepared_mod(good, false);
  qa_session_mods_options options;
  memset(&options, 0, sizeof options);
  options.prepared = &prepared;
  options.prepared_count = 1;
  options.enabled = &good;
  options.enabled_count = 1;
  options.next_frame = mod_next_frame;
  qa_session_mods *mods = qa_session_mods_open(&options, NULL, NULL);
  CHECK(mods != NULL && kept);
  CHECK(strcmp(attempt(call_kept_assert, NULL), "none") == 0);
  qa_session_mods_close(mods);
  CHECK(strcmp(attempt(call_kept_assert, NULL), "Error|Mod session is closed") == 0);
  qa_session_mods_destroy(mods);
  /* After destroy the closure's record is gone; TS still says closed. */
  CHECK(strcmp(attempt(call_kept_assert, NULL), "Error|Mod session is closed") == 0);

  qa_mod_selection broken = {"q3-one", "broken"};
  qa_prepared_mod failing = prepared_mod(broken, true);
  options.prepared = &failing;
  options.enabled = &broken;
  kept = false;
  struct open_call call = {&options, NULL};
  CHECK(strcmp(attempt(call_open, &call), "Error|init failed") == 0);
  CHECK(kept);
  CHECK(strcmp(attempt(call_kept_assert, NULL), "Error|Mod session is closed") == 0);
  CHECK(qa_world_handle_open_count() == 0);
}

/* ---------- 8. original pickups ---------- */

static bool take_original(void *ctx) {
  (void)ctx;
  return true;
}
static void complete_pickup(void *ctx, bool taken) {
  (void)ctx;
  (void)taken;
}

static void test_world_tables(void) {
  qa_identity_owner identities;
  CHECK(qa_identity_owner_create(&identities, "gate-world2 actors"));
  qa_session_actor_registry *actors = qa_session_actor_registry_create(&identities, 4096);
  qa_owned_actor player = qa_session_actor_registry_allocate(actors, "q1:player", "q1:player");
  qa_owned_actor item = qa_session_actor_registry_allocate(actors, "q1:items", "q1:item");
  qa_actor_callback_table *callbacks = qa_actor_callback_table_create(actors);
  qa_gameplay_hooks hooks;
  memset(&hooks, 0, sizeof hooks);
  qa_gameplay_authority *authority = qa_gameplay_authority_create(actors, callbacks, hooks);

  /* 3. pickup_observer_late */
  (void)qa_gameplay_authority_with_pickup_protection(authority, player, "mod:x", keep_observer, NULL);
  CHECK(strcmp(attempt(call_escaped_observer, NULL), "Error|Pickup protection observer is closed") == 0);

  /* 4. rebind_copied true false */
  memset(&rebind_state, 0, sizeof rebind_state);
  rebind_state.health = 50;
  rebind_state.mass = 200;
  rebind_state.can_take_damage = true;
  rebind_state.armor.regular.kind = QA_REGULAR_ARMOR_NONE;
  rebind_state.armor.powered.kind = "none";
  qa_gameplay_authority_create_state(authority, item, rebind_state, NULL, NULL);
  qa_armor_state copied;
  CHECK(qa_gameplay_authority_copied_primary_armor(authority, item, &copied));
  qa_combat_state_binding binding;
  memset(&binding, 0, sizeof binding);
  binding.read = rebind_read;
  binding.write_health = rebind_write_health;
  binding.write_armor = rebind_write_armor;
  qa_gameplay_authority_rebind(authority, item, binding);
  CHECK(!qa_gameplay_authority_copied_primary_armor(authority, item, &copied));

  /* 5. lease_retired_publish / lease_new_owner {"stance":true} */
  qa_mod_client_outputs *outputs = qa_mod_client_outputs_create(always_live, NULL);
  qa_mod_client_output_channel stance = QA_MOD_CLIENT_OUTPUT_STANCE;
  qa_mod_client_output_lease first = qa_mod_client_outputs_claim(outputs, "mod:a", &stance, 1);
  qa_mod_client_outputs_close(outputs);
  qa_mod_client_output_lease second = qa_mod_client_outputs_claim(outputs, "mod:b", &stance, 1);
  struct lease_call first_call = {first, player.id};
  CHECK(strcmp(attempt(publish_stance, &first_call), "Error|Component client output owner is retired") == 0);
  first.close(first.ctx);
  first.close(first.ctx);
  struct lease_call second_call = {second, player.id};
  CHECK(strcmp(attempt(publish_stance, &second_call), "none") == 0);
  qa_mod_client_movement_outputs read;
  CHECK(qa_mod_client_outputs_read(outputs, player.id, &read));
  CHECK(read.has_stance && read.stance && !read.has_mode && read.view_offset == NULL && read.body_bounds == NULL);
  qa_mod_client_outputs_destroy(outputs);

  /* 6. match_objectives x,y / match_unbind_again none y */
  qa_mod_match_state *match = qa_mod_match_state_create(actors, no_player, NULL);
  qa_source_objective_binding x;
  memset(&x, 0, sizeof x);
  x.owner = "o";
  x.id = "x";
  x.campaign_gate = true;
  x.read = read_x;
  x.change = objective_change;
  unbind_x = qa_mod_match_state_bind_objective(match, x);
  qa_source_objective_binding y = x;
  y.id = "y";
  y.read = read_y_unbinds_x;
  (void)qa_mod_match_state_bind_objective(match, y);
  char ids[64];
  join_objectives(match, ids);
  CHECK(strcmp(ids, "x,y") == 0);
  CHECK(strcmp(attempt(call_unbind_x, NULL), "none") == 0);
  join_objectives(match, ids);
  CHECK(strcmp(ids, "y") == 0);
  qa_mod_match_state_destroy(match);

  /* 8. pickup_touch accepted,accepted,accepted; scopes release at close. */
  qa_shared_original_pickup_admission *admission =
      qa_shared_original_pickup_admission_create(actors, authority, NULL, NULL, NULL);
  qa_original_pickup_offer offer;
  memset(&offer, 0, sizeof offer);
  offer.recipient = player.id;
  offer.pickup = item.id;
  offer.source = "q1:items";
  offer.item = "q1:shells";
  offer.time.kind = QA_SOURCE_TIME_SECONDS;
  offer.time.value = 1;
  offer.grant = "source-effect";
  qa_original_pickup_continuation continuation = {NULL, NULL, take_original, complete_pickup};
  for (int i = 0; i < 3; i++)
    CHECK(qa_shared_original_pickup_admission_touch(admission, offer, continuation) ==
          QA_ORIGINAL_PICKUP_OUTCOME_ACCEPTED);
#if !defined(__SANITIZE_ADDRESS__)
  /* Before the fix every touch kept its scope until destroy. */
  size_t before = mallinfo2().uordblks;
  for (int i = 0; i < 20000; i++)
    (void)qa_shared_original_pickup_admission_touch(admission, offer, continuation);
  size_t after = mallinfo2().uordblks;
  CHECK(after <= before + 4096);
#endif
  qa_shared_original_pickup_admission_destroy(admission);
  CHECK(qa_world_handle_open_count() == 0);

  qa_gameplay_authority_destroy(authority);
  qa_actor_callback_table_destroy(callbacks);
  qa_session_actor_registry_destroy(actors);
  qa_identity_owner_destroy(&identities);
}

int main(void) {
  test_session_close_all();
  test_mod_files();
  test_mods();
  test_world_tables();
  printf("test_gate_world2: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
