/* Port of src/app/bootstrap/simulation/bots.ts (and the BotArsenalBinding
 * dispatch of bot-arsenal.ts, see bots.h). GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/bots.h"
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "qa/app/bootstrap/simulation/bot_knowledge_checkpoint.h"
#include "qa/app/bootstrap/simulation/bot_q1_knowledge.h"
#include "qa/app/bootstrap/simulation/bot_q2_knowledge.h"
#include "qa/app/bootstrap/simulation/bot_q3_knowledge.h"
#include "qa/app/bootstrap/simulation/bot_world.h"
#include "qa/app/bootstrap/simulation/q3_commands.h"
#include "qa/app/bootstrap/q3_client/visibility.h"
#include "qa/app/bootstrap/simulation/q3/host.h"
#include "qa/app/bootstrap/simulation/q3/runtime.h"
#include "qa/app/bootstrap/q3_client/source.h"
#include "qa/core/commands/text.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/persistence/save_image.h"

static bool is(const char *a, const char *b) { return a != NULL && b != NULL && strcmp(a, b) == 0; }
static bool starts(const char *a, const char *prefix) { return a != NULL && strncmp(a, prefix, strlen(prefix)) == 0; }
static void *xcalloc(size_t n, size_t size) {
  void *p = calloc(n != 0 ? n : 1, size);
  if (p == NULL) qa_range_error("Out of memory");
  return p;
}
static void *xrealloc(void *p, size_t n, size_t size) {
  void *q = realloc(p, (n != 0 ? n : 1) * size);
  if (q == NULL) qa_range_error("Out of memory");
  return q;
}
static char *xstrdup(const char *s) {
  char *p = strdup(s != NULL ? s : "");
  if (p == NULL) qa_range_error("Out of memory");
  return p;
}
static noreturn void fail(const char *message) { qa_raise("Error", 0, 0, "%s", message); }
static qa_save_value *obj(void) { qa_save_value *v = qa_save_value_object(); if (v == NULL) qa_range_error("Out of memory"); return v; }
static qa_save_value *arr(void) { qa_save_value *v = qa_save_value_array(); if (v == NULL) qa_range_error("Out of memory"); return v; }
static void put(qa_save_value *o, const char *key, qa_save_value *v) { if (v == NULL || !qa_save_value_set(o, key, v)) qa_range_error("Out of memory"); }
static void push(qa_save_value *a, qa_save_value *v) { if (v == NULL || !qa_save_value_push(a, v)) qa_range_error("Out of memory"); }
static qa_save_value *num(double n) { return qa_save_value_number(n); }
static qa_save_value *str(const char *s) { return qa_save_value_string(s); }
static qa_save_value *saved_actor_value(qa_actor_id actor) {
  qa_saved_actor_id saved = qa_saved_actor_id_of(actor);
  qa_save_value *v = obj(); put(v, "slot", num(saved.slot)); put(v, "generation", num(saved.generation)); return v;
}
static void rfield(const qa_save_reader *r, const char *name, qa_save_reader *out) { if (!qa_save_reader_field(r, name, out)) qa_range_error("Out of memory"); }
static void ritem(const qa_save_reader *r, size_t i, qa_save_reader *out) { if (!qa_save_reader_item(r, i, out)) qa_range_error("Out of memory"); }

bool qa_app_sim_bot_numeric(const qa_app_sim_bot_simulation *simulation, qa_numeric_profile *out) {
  const qa_executable_recipe *recipe = simulation->recipe;
  for (size_t i = 0; i < recipe->timing_count; i++)
    if (is(recipe->timing[i].provider, recipe->engine_behavior.provider)) { *out = recipe->timing[i].numeric; return true; }
  return false;
}

/* ---- BotArsenalBinding ---- */
qa_app_sim_bot_arsenal *qa_app_sim_create_bot_arsenal(const qa_app_sim_bot_simulation *simulation,
    qa_app_sim_actor_for_client_fn actor_for_client, void *actor_ctx) {
  qa_q2_game_services *game; qa_q2w_weapons *weapons;
  if (simulation->q2_weapon_source(simulation->ctx, &game, &weapons)) return qa_app_sim_create_q2_bot_knowledge(simulation, actor_for_client, actor_ctx);
  if (simulation->q1_weapon_source(simulation->ctx) != NULL) return qa_app_sim_create_q1_bot_knowledge(simulation, actor_for_client, actor_ctx);
  if (simulation->selected_q3_weapon_source(simulation->ctx) != NULL) return qa_app_sim_create_q3_bot_knowledge(simulation, actor_for_client, actor_ctx);
  return NULL;
}
static int64_t binding_source_weapon(void *ctx, int64_t client) { qa_app_sim_bot_arsenal *a = ctx; return a->source_weapon(a, client); }
static int64_t binding_source_weapon_state(void *ctx, int64_t client) { qa_app_sim_bot_arsenal *a = ctx; return a->source_weapon_state(a, client); }
qa_app_sim_bot_arsenal *qa_app_sim_bot_arsenal_alloc(const char *source, const qa_app_sim_bot_simulation *simulation,
    qa_app_sim_actor_for_client_fn actor_for_client, void *actor_ctx) {
  qa_app_sim_bot_arsenal *a = xcalloc(1, sizeof *a);
  a->source = source; a->simulation = simulation; a->actor_for_client = actor_for_client; a->actor_ctx = actor_ctx;
  return a;
}
void qa_app_sim_bot_arsenal_bind(qa_app_sim_bot_arsenal *a,
    void (*update_inventory)(void *ctx, struct qa_bot_state_bots_a *state),
    qa_bot_weapon_knowledge_bots_a *(*candidates)(void *ctx, struct qa_bot_library_bots_a *library, int64_t handle, size_t *out_count)) {
  qa_bot_arsenal_data_bots_a data = {a, update_inventory, candidates};
  qa_create_bot_arsenal_knowledge_bots_a(&a->knowledge, &data);
  a->q3_binding.knowledge = &a->knowledge.knowledge;
  a->q3_binding.ctx = a;
  a->q3_binding.source_weapon = binding_source_weapon;
  a->q3_binding.source_weapon_state = binding_source_weapon_state;
}
void qa_app_sim_bot_arsenal_destroy(qa_app_sim_bot_arsenal *a) {
  if (a == NULL) return;
  qa_bot_arsenal_knowledge_bots_a_destroy(&a->knowledge);
  if (a->destroy_impl != NULL) a->destroy_impl(a->impl);
  free(a->actors); free(a->uncovered_weapons); free(a);
}
bool qa_app_sim_bot_arsenal_actor(const qa_app_sim_bot_arsenal *a, int64_t client, qa_actor_id *out) {
  return a->actor_for_client(a->actor_ctx, client, out);
}
void qa_app_sim_bot_arsenal_track(qa_app_sim_bot_arsenal *a, int64_t handle, const qa_actor_id *actor) {
  for (size_t i = 0; i < a->actor_count; i++) {
    if (a->actors[i].handle != handle) continue;
    if (actor != NULL) { a->actors[i].actor = *actor; return; } /* Map.set keeps position */
    memmove(&a->actors[i], &a->actors[i + 1], (a->actor_count - i - 1) * sizeof *a->actors);
    a->actor_count--; return;
  }
  if (actor == NULL) return;
  if (a->actor_count == a->actor_capacity) {
    a->actor_capacity = a->actor_capacity != 0 ? a->actor_capacity * 2 : 8;
    a->actors = xrealloc(a->actors, a->actor_capacity, sizeof *a->actors);
  }
  a->actors[a->actor_count].handle = handle; a->actors[a->actor_count].actor = *actor; a->actor_count++;
}
bool qa_app_sim_bot_arsenal_handle_actor(const qa_app_sim_bot_arsenal *a, int64_t handle, qa_actor_id *out) {
  for (size_t i = 0; i < a->actor_count; i++) if (a->actors[i].handle == handle) { *out = a->actors[i].actor; return true; }
  return false;
}
qa_save_value *qa_app_sim_bot_arsenal_checkpoint(const qa_app_sim_bot_arsenal *a) {
  qa_save_value *v = obj(), *actors = arr();
  put(v, "version", num(1)); put(v, "source", str(a->source));
  for (size_t i = 0; i < a->actor_count; i++) {
    qa_save_value *entry = obj();
    put(entry, "handle", num((double)a->actors[i].handle)); put(entry, "actor", saved_actor_value(a->actors[i].actor));
    push(actors, entry);
  }
  put(v, "actors", actors);
  return v;
}
typedef struct restore_adapter {
  qa_app_sim_saved_actor_ref_fn actor; void *actor_ctx;
  qa_app_sim_weapon_handle_ref_fn weapon; void *weapon_ctx;
} restore_adapter;
static qa_actor_id restore_actor(void *ctx, qa_saved_actor_id saved) { restore_adapter *r = ctx; return r->actor(r->actor_ctx, saved); }
static double restore_weapon(void *ctx, double saved) { restore_adapter *r = ctx; return (double)r->weapon(r->weapon_ctx, (int64_t)saved); }
void qa_app_sim_bot_arsenal_restore_checkpoint(qa_app_sim_bot_arsenal *a, const qa_save_value *value,
    qa_app_sim_saved_actor_ref_fn actor, void *actor_ctx, qa_app_sim_weapon_handle_ref_fn weapon_handle, void *weapon_ctx) {
  qa_app_sim_bot_knowledge_entry *entries = xcalloc(a->actor_count, sizeof *entries);
  for (size_t i = 0; i < a->actor_count; i++) { entries[i].handle = (double)a->actors[i].handle; entries[i].actor = a->actors[i].actor; }
  qa_app_sim_bot_knowledge_persistence *p = qa_app_sim_bot_knowledge_persistence_create(a->source, entries, a->actor_count);
  restore_adapter adapter = {actor, actor_ctx, weapon_handle, weapon_ctx};
  qa_app_sim_bot_knowledge_restore(p, value, restore_actor, &adapter, restore_weapon, &adapter);
  size_t n = qa_app_sim_bot_knowledge_entry_count(p);
  const qa_app_sim_bot_knowledge_entry *restored = qa_app_sim_bot_knowledge_entries(p);
  a->actor_count = 0;
  for (size_t i = 0; i < n; i++) qa_app_sim_bot_arsenal_track(a, (int64_t)restored[i].handle, &restored[i].actor);
  qa_app_sim_bot_knowledge_persistence_destroy(p);
}

/* ---- transport checkpoint codec ---- */
static void decode_transport_reader(const qa_save_reader *reader, qa_app_sim_bot_transport_checkpoint *out) {
  memset(out, 0, sizeof *out);
  qa_save_reader f, list, entry, client, actor, reliable, slots, item;
  rfield(reader, "version", &f); qa_save_reader_literal_number(&f, 1); qa_save_reader_destroy(&f);
  rfield(reader, "elapsedMilliseconds", &f); out->elapsed_milliseconds = qa_save_reader_finite(&f); qa_save_reader_destroy(&f);
  rfield(reader, "connections", &list);
  out->connection_count = qa_save_reader_list_count(&list);
  out->connections = xcalloc(out->connection_count, sizeof *out->connections);
  for (size_t i = 0; i < out->connection_count; i++) {
    qa_app_sim_bot_transport_connection *c = &out->connections[i];
    ritem(&list, i, &entry);
    rfield(&entry, "client", &client);
    rfield(&client, "slot", &f); c->client_slot = qa_save_reader_integer_min(&f, 0); qa_save_reader_destroy(&f);
    rfield(&client, "generation", &f); c->client_generation = qa_save_reader_integer_min(&f, 0); qa_save_reader_destroy(&f);
    qa_save_reader_destroy(&client);
    rfield(&entry, "actor", &actor);
    rfield(&actor, "slot", &f); c->actor.slot = qa_save_reader_integer_min(&f, 0); qa_save_reader_destroy(&f);
    rfield(&actor, "generation", &f); c->actor.generation = qa_save_reader_integer_min(&f, 0); qa_save_reader_destroy(&f);
    qa_save_reader_destroy(&actor);
    rfield(&entry, "reliable", &reliable);
    rfield(&reliable, "sequence", &f); c->sequence = qa_save_reader_integer_min(&f, 0); qa_save_reader_destroy(&f);
    rfield(&reliable, "acknowledge", &f); c->acknowledge = qa_save_reader_integer(&f); qa_save_reader_destroy(&f);
    rfield(&reliable, "slots", &slots);
    size_t slot_count = qa_save_reader_list_count(&slots);
    c->slot_count = slot_count;
    for (size_t s = 0; s < slot_count; s++) {
      ritem(&slots, s, &item);
      const char *text = qa_save_reader_string(&item);
      if (s < QA_Q3_MAX_RELIABLE_COMMANDS) c->slots[s] = xstrdup(text);
      qa_save_reader_destroy(&item);
    }
    qa_save_reader_destroy(&slots); qa_save_reader_destroy(&reliable); qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&list);
  rfield(reader, "snapshots", &list);
  out->snapshot_count = qa_save_reader_list_count(&list);
  out->snapshots = xcalloc(out->snapshot_count, sizeof *out->snapshots);
  for (size_t i = 0; i < out->snapshot_count; i++) {
    ritem(&list, i, &entry);
    rfield(&entry, "client", &f); out->snapshots[i].client = qa_save_reader_integer_min(&f, 0); qa_save_reader_destroy(&f);
    rfield(&entry, "entities", &slots);
    out->snapshots[i].entity_count = qa_save_reader_list_count(&slots);
    out->snapshots[i].entities = xcalloc(out->snapshots[i].entity_count, sizeof *out->snapshots[i].entities);
    for (size_t e = 0; e < out->snapshots[i].entity_count; e++) {
      ritem(&slots, e, &item); out->snapshots[i].entities[e] = qa_save_reader_integer_min(&item, 0); qa_save_reader_destroy(&item);
    }
    qa_save_reader_destroy(&slots); qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&list);
}
void qa_app_sim_decode_bot_transport(const qa_save_value *value, qa_app_sim_bot_transport_checkpoint *out) {
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, value, "application.bots.transport")) qa_range_error("Out of memory");
  decode_transport_reader(&reader, out);
  qa_save_reader_destroy(&reader);
}
void qa_app_sim_bot_transport_checkpoint_free(qa_app_sim_bot_transport_checkpoint *image) {
  for (size_t i = 0; i < image->connection_count; i++)
    for (size_t s = 0; s < QA_Q3_MAX_RELIABLE_COMMANDS; s++) free(image->connections[i].slots[s]);
  for (size_t i = 0; i < image->snapshot_count; i++) free(image->snapshots[i].entities);
  free(image->connections); free(image->snapshots);
  memset(image, 0, sizeof *image);
}
void qa_app_sim_decode_application_bots_checkpoint(const qa_save_value *value, qa_app_sim_decoded_bots_checkpoint *out) {
  memset(out, 0, sizeof *out);
  qa_save_reader reader, f, list, entry, actor;
  if (!qa_save_reader_init(&reader, value, "application.bots")) qa_range_error("Out of memory");
  rfield(&reader, "version", &f); qa_save_reader_literal_number(&f, 1); qa_save_reader_destroy(&f);
  rfield(&reader, "transport", &f); qa_app_sim_decode_bot_transport(f.value, &out->transport); qa_save_reader_destroy(&f);
  out->director = qa_save_value_get(value, "director"); out->navigation = qa_save_value_get(value, "navigation");
  out->knowledge = qa_save_value_get(value, "knowledge"); out->shared_world = qa_save_value_get(value, "sharedWorld");
  rfield(&reader, "observations", &list);
  out->observation_count = qa_save_reader_list_count(&list);
  out->observations = xcalloc(out->observation_count, sizeof *out->observations);
  for (size_t i = 0; i < out->observation_count; i++) {
    ritem(&list, i, &entry);
    rfield(&entry, "number", &f); out->observations[i].number = qa_save_reader_integer_min(&f, 0); qa_save_reader_destroy(&f);
    rfield(&entry, "actor", &actor);
    rfield(&actor, "slot", &f); out->observations[i].actor.slot = qa_save_reader_integer_min(&f, 0); qa_save_reader_destroy(&f);
    rfield(&actor, "generation", &f); out->observations[i].actor.generation = qa_save_reader_integer_min(&f, 0); qa_save_reader_destroy(&f);
    qa_save_reader_destroy(&actor); qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&list); qa_save_reader_destroy(&reader);
}
void qa_app_sim_decoded_bots_checkpoint_free(qa_app_sim_decoded_bots_checkpoint *image) {
  qa_app_sim_bot_transport_checkpoint_free(&image->transport);
  free(image->observations);
  memset(image, 0, sizeof *image);
}
qa_save_value *qa_app_sim_encode_bot_transport_connection(qa_session_client *client, qa_actor_id actor,
    qa_q3_server_reliable_commands *reliable) {
  qa_client_id id = qa_session_client_id(client);
  qa_save_value *entry = obj(), *c = obj(), *r = obj(), *slots = arr();
  put(c, "slot", num((double)id.slot)); put(c, "generation", num((double)id.generation));
  put(entry, "client", c); put(entry, "actor", saved_actor_value(actor));
  put(r, "sequence", num((double)qa_q3_reliable_ring_sequence(reliable))); put(r, "acknowledge", num((double)qa_q3_reliable_ring_acknowledge(reliable)));
  for (int64_t index = 0; index < QA_Q3_MAX_RELIABLE_COMMANDS; index++) push(slots, str(qa_q3_reliable_ring_lookup_masked(reliable, index)));
  put(r, "slots", slots);
  put(entry, "reliable", r);
  return entry;
}
qa_q3_server_reliable_commands *qa_app_sim_restored_bot_reliable_commands(const qa_app_sim_bot_transport_connection *image) {
  if (image->sequence != trunc(image->sequence) || image->sequence < 0 || image->sequence > 0x7fffffff
      || image->acknowledge != trunc(image->acknowledge) || image->acknowledge < -2147483648.0 || image->acknowledge > 0x7fffffff
      || image->slot_count != QA_Q3_MAX_RELIABLE_COMMANDS) fail("Invalid saved bot reliable command ring");
  qa_q3_server_reliable_commands *ring = NULL;
  if (!qa_q3_server_reliable_commands_init(&ring)) return NULL;
  for (size_t index = 0; index < QA_Q3_MAX_RELIABLE_COMMANDS; index++) {
    const char *text = image->slots[index];
    if (strlen(text) > 1023) { qa_q3_reliable_ring_destroy(ring); fail("Invalid saved bot reliable command text"); }
    qa_q3_reliable_ring_replace(ring, (int64_t)index, text);
  }
  qa_q3_reliable_ring_set_sequences(ring, (int64_t)image->sequence, (int64_t)image->acknowledge);
  return ring;
}

/* ---- SimulationBotServices ---- */
static void services_test_aas(void *ctx, qa_vec3 origin) {
  qa_app_sim_bot_services *s = ctx;
  if (s->director != NULL) qa_source_bot_director_test_aas(s->director, origin);
}
static void services_interbreed(void *ctx) {
  qa_app_sim_bot_services *s = ctx;
  if (s->director != NULL) qa_source_bot_director_interbreed_end_match(s->director);
}
static qa_source_bot_director *services_require(qa_app_sim_bot_services *s) {
  if (s->director == NULL) fail("Source bot transport must be attached before bot commands");
  return s->director;
}
static void services_console(void *ctx, const char *const *argv, size_t argc) {
  qa_source_bot_director_console_command(services_require(ctx), argv, argc);
}
/* source.connect / shutdownClient / removeQueuedBegin (ClientBotServices). */
static bool services_connect(void *ctx, double client, bool restart) {
  return qa_source_bot_director_connect(services_require(ctx), (int64_t)client, restart);
}
static void services_shutdown_client(void *ctx, double client, bool restart) {
  qa_source_bot_director_shutdown_client(services_require(ctx), (int64_t)client, restart);
}
static void services_remove_queued_begin(void *ctx, double client) {
  qa_app_sim_bot_services *s = ctx;
  if (s->director != NULL) qa_source_bot_director_remove_queued_begin(s->director, (int64_t)client);
}
void qa_app_sim_bot_services_init(qa_app_sim_bot_services *s) {
  memset(s, 0, sizeof *s);
  s->source.ctx = s; s->source.kind = "available"; s->source.reason = NULL;
  s->source.test_aas = services_test_aas; s->source.interbreed_end_match = services_interbreed;
  s->source.console_command = services_console;
  s->source.client.kind = QA_Q3_TA_BOTS_AVAILABLE; s->source.client.ctx = s;
  s->source.client.connect = services_connect; s->source.client.shutdown_client = services_shutdown_client;
  s->source.client.remove_queued_begin = services_remove_queued_begin;
}
int32_t qa_app_sim_bot_services_move_to_point(qa_app_sim_bot_services *s, qa_actor_id actor, qa_vec3 point, double tolerance) {
  return s->transport != NULL ? s->transport->move_to_point(s->transport->ctx, actor, point, tolerance) : 0;
}
int32_t qa_app_sim_bot_services_follow_actor(qa_app_sim_bot_services *s, qa_actor_id actor, qa_actor_id target) {
  return s->transport != NULL ? s->transport->follow_actor(s->transport->ctx, actor, target) : 0;
}
bool qa_app_sim_bot_services_is_bot(qa_app_sim_bot_services *s, qa_actor_id actor) {
  return s->transport != NULL && s->transport->is_bot(s->transport->ctx, actor);
}
/* Admits one roster bot for the running match when a director is attached. */
bool qa_app_sim_bot_services_try_add_bot(qa_app_sim_bot_services *s) {
  return s->director != NULL && qa_source_bot_director_add_match_bot(s->director);
}
/* Retires one bot through its transport, freeing the session client slot. */
bool qa_app_sim_bot_services_try_disconnect_bot(qa_app_sim_bot_services *s, qa_actor_id actor) {
  const qa_app_sim_bot_service *transport = s->transport;
  if (transport == NULL) return false;
  size_t count = 0;
  const qa_app_sim_bot_client *clients = transport->clients(transport->ctx, &count);
  qa_client_id *ids = xcalloc(count, sizeof *ids);
  for (size_t i = 0; i < count; i++) ids[i] = qa_session_client_id(clients[i].client);
  for (size_t i = 0; i < count; i++) {
    qa_actor_id owner;
    if (transport->actor(transport->ctx, ids[i], &owner) && qa_same_actor(owner, actor)) {
      int64_t slot = ids[i].slot;
      free(ids);
      return transport->disconnect(transport->ctx, slot);
    }
  }
  free(ids);
  return false;
}
qa_cvar_registry *qa_app_sim_bot_services_configuration(qa_app_sim_bot_services *s) {
  return s->transport != NULL ? s->transport->configuration(s->transport->ctx) : NULL;
}
qa_save_value *qa_app_sim_bot_services_checkpoint(qa_app_sim_bot_services *s) {
  return s->transport != NULL ? s->transport->checkpoint(s->transport->ctx) : NULL;
}
const qa_actor_command *qa_app_sim_bot_services_frame(qa_app_sim_bot_services *s, double time, double elapsed, size_t *out_count) {
  *out_count = 0;
  if (s->transport == NULL || !s->transport->options->automatic_frame) return NULL;
  return s->transport->frame(s->transport->ctx, time, elapsed, out_count);
}
qa_app_sim_bot_route qa_app_sim_bot_services_navigation(qa_app_sim_bot_services *s, qa_actor_id actor, qa_vec3 start, qa_vec3 goal) {
  qa_app_sim_bot_route out = {QA_APP_SIM_BOT_ROUTE_NO_NAVIGATION, 0, NULL, 0};
  const qa_app_sim_bot_service *transport = s->transport;
  if (transport == NULL) return out;
  const qa_app_sim_bot_simulation *sim = transport->options->simulation;
  qa_app_sim_movement_player *player = sim->movement_player(sim->ctx, actor);
  if (player == NULL) return out;
  qa_app_sim_bot_navigation *navigation = transport->options->navigation;
  qa_navigation_runtime *runtime = navigation->selected.for_client(navigation->selected.ctx, player->client.slot);
  qa_navigation_route_query query;
  memset(&query, 0, sizeof query);
  query.start = start; query.goal = goal;
  qa_navigation_route_result result = qa_navigation_runtime_route(runtime, query);
  if (!result.is_route) { out.kind = QA_APP_SIM_BOT_ROUTE_UNREACHABLE; return out; }
  double distance = 0;
  const qa_vec3 *previous = NULL;
  for (size_t i = 0; i < result.route.point_count; i++) {
    const qa_vec3 *current = &result.route.points[i];
    if (previous != NULL) distance += qa_js_hypot3(current->x - previous->x, current->y - previous->y, current->z - previous->z);
    previous = current;
  }
  out.kind = QA_APP_SIM_BOT_ROUTE_PATH; out.distance_squared = distance * distance;
  out.point_count = result.route.point_count;
  out.points = xcalloc(out.point_count, sizeof *out.points);
  memcpy(out.points, result.route.points, out.point_count * sizeof *out.points);
  qa_navigation_route_destroy(&result.route);
  return out;
}
bool qa_app_sim_bot_services_source_connect(qa_app_sim_bot_services *s, int64_t client, bool restart) {
  return qa_source_bot_director_connect(services_require(s), client, restart);
}
void qa_app_sim_bot_services_source_shutdown_client(qa_app_sim_bot_services *s, int64_t client, bool restart) {
  qa_source_bot_director_shutdown_client(services_require(s), client, restart);
}
void qa_app_sim_bot_services_source_remove_queued_begin(qa_app_sim_bot_services *s, int64_t client) {
  if (s->director != NULL) qa_source_bot_director_remove_queued_begin(s->director, client);
}
void qa_app_sim_bot_services_attach(qa_app_sim_bot_services *s, qa_source_bot_director *director, const qa_app_sim_bot_service *transport) {
  if (s->director != NULL || s->transport != NULL) fail("Source bot services already have a director");
  s->director = director; s->transport = transport;
}
void qa_app_sim_bot_services_attach_transport(qa_app_sim_bot_services *s, const qa_app_sim_bot_service *transport) {
  if (s->director != NULL || s->transport != NULL) fail("Bot transport is already attached");
  s->transport = transport;
}
void qa_app_sim_bot_services_detach_transport(qa_app_sim_bot_services *s, const qa_app_sim_bot_service *transport) {
  if (s->director != NULL || s->transport != transport) fail("Bot transport lifetime differs");
  s->transport = NULL;
}
void qa_app_sim_bot_services_detach(qa_app_sim_bot_services *s, qa_source_bot_director *director) {
  if (s->director != director) fail("Cannot detach a different source bot director");
  s->director = NULL; s->transport = NULL;
}

/* ---- bot admission ---- */
const char *qa_app_sim_bot_admission_error(const qa_app_sim_bot_simulation *simulation) {
  const qa_app_sim_bot_q3_source *q3 = simulation->q3_source(simulation->ctx);
  qa_q2_game_services *q2_game; qa_q2w_weapons *q2_weapons;
  if (q3 != NULL) {
    const qa_executable_recipe *recipe = simulation->recipe;
    bool native = recipe->weapons.count != 0 && starts(recipe->weapons.values[0].provider, "q3:");
    const qa_app_q3_source_options *options = qa_app_q3_source_runtime_options(q3->runtime);
    double game_type = qa_app_q3_source_subsystems_owner_game_type(q3->owner);
    bool selected = is(options->product, "baseq3")
        && (simulation->q2_weapon_source(simulation->ctx, &q2_game, &q2_weapons) || simulation->q1_weapon_source(simulation->ctx) != NULL)
        && ((is(simulation->mode, "deathmatch") && game_type == 0) || (is(simulation->mode, "singleplayer") && game_type == 2));
    return native || selected ? NULL : "Q3-map bots support native Q3 weapons or selected Q1/Q2 weapons in base Q3 deathmatch and single-player arenas";
  }
  const qa_app_sim_bot_q1_source *q1 = simulation->q1_source(simulation->ctx);
  qa_q2_product_runtime *q2 = simulation->q2_source(simulation->ctx);
  if (!simulation->q2_weapon_source(simulation->ctx, &q2_game, &q2_weapons) && simulation->q1_weapon_source(simulation->ctx) == NULL
      && simulation->selected_q3_weapon_source(simulation->ctx) == NULL) return "Shared bot arsenal observation is unavailable";
  return q1 != NULL || q2 != NULL ? NULL : "Bot world observation is unavailable";
}
static const char *legacy_bot_admission_error(const qa_app_sim_bot_simulation *simulation) {
  const qa_app_sim_bot_q1_source *q1 = simulation->q1_source(simulation->ctx);
  qa_q2_product_runtime *q2 = simulation->q2_source(simulation->ctx);
  if (!is(simulation->mode, "deathmatch")
      || (q1 != NULL && (q1->composition->selection.program != QA_COMP_Q1_ID1 || qa_cvar_registry_variable_value(q1->cvars, "teamplay") != 0))
      || (q2 != NULL && q2->match.selection.kind != QA_Q2_MATCH_STANDARD)) return "Team and campaign bots require mounted native bot definitions";
  return NULL;
}

/* ---- application bot log files ---- */
typedef struct log_stream { int descriptor; int64_t position; } log_stream;
static qa_bot_log_io_result io_ok(void) { qa_bot_log_io_result r = {QA_LOG_IO_OK}; return r; }
static qa_bot_log_io_result io_failed(void) { qa_bot_log_io_result r = {QA_LOG_IO_FAILED}; return r; }
static qa_bot_log_io_result log_write(void *ctx, const uint8_t *bytes, size_t n) {
  log_stream *s = ctx;
  size_t offset = 0;
  while (offset < n) {
    ssize_t written = pwrite(s->descriptor, bytes + offset, n - offset, (off_t)s->position);
    if (written <= 0) return io_failed(); /* error, or "Bot log write made no progress" */
    offset += (size_t)written; s->position += written;
  }
  return io_ok();
}
/* Writes above are synchronous writeSync calls, already visible to any
 * reader; the source fflush has no durability requirement, and an
 * fsync per botlib log line stalls frames whenever bots are active. */
static qa_bot_log_io_result log_flush(void *ctx) { (void)ctx; return io_ok(); }
static qa_bot_log_io_result log_close(void *ctx) {
  log_stream *s = ctx;
  int result = close(s->descriptor);
  free(s);
  return result == 0 ? io_ok() : io_failed();
}
static int64_t log_position(void *ctx) { log_stream *s = ctx; return s->position; }
static qa_bot_log_open_result application_bot_log(const char *directory_override, const char *filename, bool resume, double resume_position) {
  qa_bot_log_open_result out;
  memset(&out, 0, sizeof out);
  char directory[4096];
  if (directory_override != NULL) snprintf(directory, sizeof directory, "%s", directory_override);
  else {
    const char *home = getenv("HOME");
    snprintf(directory, sizeof directory, "%s/.local/state/quake-typescript/bots", home != NULL ? home : "");
  }
  if (resume && (resume_position != trunc(resume_position) || resume_position < 0 || resume_position > 9007199254740991.0)) return out;
  if (!resume) {
    /* mkdirSync(directory, { recursive: true }) */
    char partial[4096];
    snprintf(partial, sizeof partial, "%s", directory);
    for (char *p = partial + 1; *p != '\0'; p++) {
      if (*p != '/') continue;
      *p = '\0'; if (mkdir(partial, 0777) != 0 && errno != EEXIST) return out; *p = '/';
    }
    if (mkdir(partial, 0777) != 0 && errno != EEXIST) return out;
  }
  const char *base = strrchr(filename, '/');
  base = base != NULL ? base + 1 : filename;
  char path[8192];
  snprintf(path, sizeof path, "%s/%s", directory, base);
  int descriptor = resume ? open(path, O_RDWR) : open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if (descriptor < 0) return out;
  if (resume) {
    struct stat info;
    if (fstat(descriptor, &info) != 0 || (double)info.st_size < resume_position) { close(descriptor); return out; }
  }
  log_stream *s = calloc(1, sizeof *s);
  if (s == NULL) { close(descriptor); return out; }
  s->descriptor = descriptor; s->position = resume ? (int64_t)resume_position : 0;
  out.opened = true;
  out.stream.ctx = s; out.stream.write = log_write; out.stream.flush = log_flush; out.stream.close = log_close;
  out.stream.has_checkpoint = true; out.stream.checkpoint_position = log_position;
  return out;
}
qa_bot_log_open_result qa_app_sim_open_application_bot_log(const char *directory, const char *filename) {
  return application_bot_log(directory, filename, false, 0);
}
qa_bot_log_open_result qa_app_sim_resume_application_bot_log(const char *directory, const char *filename, double position) {
  return application_bot_log(directory, filename, true, position);
}

/* ---- ApplicationBots ---- */
typedef struct bot_connection {
  int64_t slot;
  qa_session_client *client;
  qa_q3_server_reliable_commands *reliable; /* owned */
  qa_owned_actor actor;
} bot_connection;
typedef struct bot_snapshot { int64_t client; int64_t *entities; size_t count; } bot_snapshot;
typedef enum restart_kind { RESTART_ACTIVE, RESTART_DETACHED, RESTART_BOUND } restart_kind;
typedef struct phase_client { qa_session_client *client; qa_q3_server_reliable_commands *reliable; char *userinfo; } phase_client;
struct qa_app_sim_application_bots {
  qa_app_sim_bots_options options;
  qa_source_bot_director *director;
  qa_shared_bot_population population;
  const qa_app_sim_bot_q3_source *source;
  qa_source_bot_game_bots_a *game;
  qa_q3_bot_game_bots_a *q3_game; /* owned (source rounds) */
  qa_q3_bot_game_bots_a *retired_games[4]; size_t retired_game_count;
  restart_kind restart;
  const qa_app_sim_bot_q3_source *detached_source;
  phase_client *phase_clients; size_t phase_client_count; /* owned rings + userinfo */
  int64_t *pending; size_t pending_count;
  qa_app_sim_bot_arsenal *arsenal;
  qa_app_sim_shared_bot_world *shared;
  bot_connection *connections; size_t connection_count, connection_capacity;
  bot_snapshot *snapshots; size_t snapshot_count, snapshot_capacity;
  double elapsed_milliseconds;
  bool closed;
  qa_source_bot_director_host host;
  qa_q3_bot_navigation_bots_a *navigation; /* current round */
  qa_q3_bot_navigation_bots_a *retired_navigation[4]; size_t retired_navigation_count;
  qa_app_sim_bot_service service;
  qa_app_sim_bot_client *client_views; size_t client_view_capacity;
};
typedef struct qa_app_sim_application_bots app_bots;
static const qa_app_sim_bot_simulation *sim(app_bots *b) { return b->options.simulation; }
static bot_connection *connection_at(app_bots *b, int64_t slot) {
  for (size_t i = 0; i < b->connection_count; i++) if (b->connections[i].slot == slot) return &b->connections[i];
  return NULL;
}
static bot_connection *require_connection(app_bots *b, int64_t client) {
  bot_connection *c = connection_at(b, client);
  if (c == NULL) qa_raise("Error", 0, 0, "Source bot %lld has no session connection", (long long)client);
  return c;
}
static void set_connection(app_bots *b, bot_connection connection) {
  bot_connection *existing = connection_at(b, connection.slot);
  if (existing != NULL) {
    if (existing->reliable != connection.reliable) qa_q3_reliable_ring_destroy(existing->reliable);
    *existing = connection; return;
  }
  if (b->connection_count == b->connection_capacity) {
    b->connection_capacity = b->connection_capacity != 0 ? b->connection_capacity * 2 : 8;
    b->connections = xrealloc(b->connections, b->connection_capacity, sizeof *b->connections);
  }
  b->connections[b->connection_count++] = connection;
}
static void delete_connection(app_bots *b, int64_t slot, bool free_ring) {
  for (size_t i = 0; i < b->connection_count; i++) {
    if (b->connections[i].slot != slot) continue;
    if (free_ring) qa_q3_reliable_ring_destroy(b->connections[i].reliable);
    memmove(&b->connections[i], &b->connections[i + 1], (b->connection_count - i - 1) * sizeof *b->connections);
    b->connection_count--; return;
  }
}
static void snapshots_clear(app_bots *b) {
  for (size_t i = 0; i < b->snapshot_count; i++) free(b->snapshots[i].entities);
  b->snapshot_count = 0;
}
static void delete_snapshot(app_bots *b, int64_t client) {
  for (size_t i = 0; i < b->snapshot_count; i++) {
    if (b->snapshots[i].client != client) continue;
    free(b->snapshots[i].entities);
    memmove(&b->snapshots[i], &b->snapshots[i + 1], (b->snapshot_count - i - 1) * sizeof *b->snapshots);
    b->snapshot_count--; return;
  }
}
static bot_snapshot *snapshot_of(app_bots *b, int64_t client) {
  for (size_t i = 0; i < b->snapshot_count; i++) if (b->snapshots[i].client == client) return &b->snapshots[i];
  return NULL;
}
static bot_snapshot *add_snapshot(app_bots *b, int64_t client, int64_t *entities, size_t count) {
  if (b->snapshot_count == b->snapshot_capacity) {
    b->snapshot_capacity = b->snapshot_capacity != 0 ? b->snapshot_capacity * 2 : 8;
    b->snapshots = xrealloc(b->snapshots, b->snapshot_capacity, sizeof *b->snapshots);
  }
  bot_snapshot *s = &b->snapshots[b->snapshot_count++];
  s->client = client; s->entities = entities; s->count = count;
  return s;
}
static bool players_actor_for_client(void *ctx, int64_t client, qa_actor_id *out) {
  app_bots *b = ctx;
  size_t count = 0;
  const qa_actor_id *players = sim(b)->players(sim(b)->ctx, &count);
  for (size_t i = 0; i < count; i++) {
    qa_app_sim_movement_player *player = sim(b)->movement_player(sim(b)->ctx, players[i]);
    if (player != NULL && player->client.slot == client) { *out = players[i]; return true; }
  }
  return false;
}
static bool connection_actor_for_client(void *ctx, int64_t client, qa_actor_id *out) {
  bot_connection *c = connection_at(ctx, client);
  if (c == NULL) return false;
  *out = c->actor.id; return true;
}
static bool prepare_client(app_bots *b, qa_session_client *client, qa_q3_server_reliable_commands *reliable) {
  if (qa_session_client_is_closed(client)) fail("Cannot admit a closed bot client");
  qa_client_id id = qa_session_client_id(client);
  qa_owned_actor actor = sim(b)->prepare_bot_client(sim(b)->ctx, id);
  if (reliable == NULL && !qa_q3_server_reliable_commands_init(&reliable)) qa_range_error("Out of memory");
  bot_connection connection = {id.slot, client, reliable, actor};
  set_connection(b, connection);
  return true;
}
typedef struct allocate_attempt { app_bots *b; qa_session_client *client; } allocate_attempt;
static void allocate_attempt_run(void *ctx) { allocate_attempt *a = ctx; prepare_client(a->b, a->client, NULL); }
static int64_t allocate_client(void *ctx) {
  app_bots *b = ctx;
  size_t count = 0;
  const qa_actor_id *players = sim(b)->players(sim(b)->ctx, &count);
  int64_t *occupied = xcalloc(count, sizeof *occupied);
  size_t occupied_count = 0;
  for (size_t i = 0; i < count; i++) {
    qa_app_sim_movement_player *player = sim(b)->movement_player(sim(b)->ctx, players[i]);
    if (player != NULL) occupied[occupied_count++] = player->client.slot;
  }
  int64_t max = b->game->max_clients(b->game->ctx);
  for (int64_t slot = 0; slot < max; slot++) {
    bool used = false;
    for (size_t i = 0; i < occupied_count && !used; i++) used = occupied[i] == slot;
    if (used) continue;
    free(occupied);
    qa_session_client *client = qa_engine_session_create_client(b->options.session, slot);
    allocate_attempt attempt = {b, client};
    qa_caught_error error;
    if (!qa_try(allocate_attempt_run, &attempt, &error)) {
      qa_engine_session_close_client(b->options.session, qa_session_client_id(client));
      qa_rethrow(&error);
    }
    return slot;
  }
  free(occupied);
  return -1;
}
static bool host_actor(void *ctx, int64_t client, qa_owned_actor *out) {
  bot_connection *c = connection_at(ctx, client);
  if (c == NULL) return false;
  *out = c->actor; return true;
}
static void encode_command(void *ctx, int64_t client, const qa_q3_base_user_command *command, qa_source_bot_encoded_command *out) {
  app_bots *b = ctx;
  bot_connection *connection = require_connection(b, client);
  qa_app_sim_movement_player *player = sim(b)->movement_player(sim(b)->ctx, connection->actor.id);
  if (player == NULL) fail("Admitted bot has no selected movement player");
  memset(out, 0, sizeof *out);
  out->has_arsenal = true;
  if (b->shared != NULL) {
    out->command = qa_app_sim_selected_q3_command(*command, player->profile, b->elapsed_milliseconds);
    qa_app_sim_bot_arsenal *knowledge = qa_app_sim_shared_bot_world_knowledge(b->shared);
    out->arsenal.provider = player->arsenal.provider;
    out->arsenal.weapon = knowledge->resolve_weapon(knowledge, client, (int64_t)command->weapon);
    out->arsenal.use_holdable = false;
    return;
  }
  if (b->source == NULL) fail("Bot source observation is unavailable");
  qa_q3_game_entity *entity = qa_q3_entity_pool_at(qa_app_q3_source_subsystems_owner_pool(b->source->owner), (double)client);
  qa_q3_game_client *source_player = entity->client;
  if (source_player == NULL) fail("Source bot lost its player state");
  qa_q3_base_user_command selected = *command;
  if (!is(player->profile.kind, "q3")) {
    selected.angles.x = (double)(qa_js_to_int32(command->angles.x + source_player->ps.delta_angles.x) & 65535);
    selected.angles.y = (double)(qa_js_to_int32(command->angles.y + source_player->ps.delta_angles.y) & 65535);
    selected.angles.z = (double)(qa_js_to_int32(command->angles.z + source_player->ps.delta_angles.z) & 65535);
  }
  qa_item_id weapon = NULL;
  if (b->arsenal == NULL) {
    for (size_t i = 0; i < QA_Q3_WEAPON_ITEM_COUNT && weapon == NULL; i++)
      if (qa_q3_weapon_items[i].weapon == command->weapon) weapon = qa_q3_weapon_items[i].item;
  } else weapon = b->arsenal->resolve_weapon(b->arsenal, client, (int64_t)command->weapon);
  selected.weapon = b->arsenal == NULL ? command->weapon : source_player->ps.weapon;
  out->command = qa_app_sim_selected_q3_command(selected, player->profile, b->elapsed_milliseconds);
  out->arsenal.provider = player->arsenal.provider;
  out->arsenal.weapon = weapon;
  out->arsenal.use_holdable = (qa_js_to_int32(command->buttons) & 4) != 0;
}
static void player_record_of(qa_q3_player_state_record *out, const qa_q3_base_player_state *in) {
  memset(out, 0, sizeof *out);
  out->product = qa_q3_base_player_state_record_product(in) == QA_Q3_BASE_PRODUCT_MISSIONPACK ? QA_Q3_PRODUCT_MISSIONPACK : QA_Q3_PRODUCT_BASEQ3;
  out->command_time = qa_js_to_int32(in->command_time); out->pm_type = qa_js_to_int32(in->pm_type); out->bob_cycle = qa_js_to_int32(in->bob_cycle);
  out->pm_flags = qa_js_to_int32(in->pm_flags); out->pm_time = qa_js_to_int32(in->pm_time);
  out->origin = qa_q3_base_player_state_record_origin(in); out->velocity = qa_q3_base_player_state_record_velocity(in);
  out->weapon_time = qa_js_to_int32(in->weapon_time); out->gravity = qa_js_to_int32(in->gravity); out->speed = qa_js_to_int32(in->speed);
  out->delta_angles = in->delta_angles;
  out->ground_entity_num = qa_js_to_int32(in->ground_entity_num); out->legs_timer = qa_js_to_int32(in->legs_timer);
  out->legs_anim = qa_js_to_int32(in->legs_anim); out->torso_timer = qa_js_to_int32(in->torso_timer);
  out->torso_anim = qa_js_to_int32(in->torso_anim); out->movement_dir = qa_js_to_int32(in->movement_dir);
  out->grapple_point = in->grapple_point;
  out->e_flags = qa_js_to_int32(in->e_flags); out->event_sequence = qa_js_to_int32(in->event_sequence);
  for (int i = 0; i < 2; i++) {
    out->events[i] = qa_js_to_int32(qa_q3_base_player_state_slots_get(&in->events, i));
    out->event_parms[i] = qa_js_to_int32(qa_q3_base_player_state_slots_get(&in->event_parms, i));
  }
  out->external_event = qa_js_to_int32(in->external_event); out->external_event_parm = qa_js_to_int32(in->external_event_parm);
  out->external_event_time = qa_js_to_int32(in->external_event_time); out->client_num = qa_js_to_int32(in->client_num);
  out->weapon = qa_js_to_int32(in->weapon); out->weapon_state = qa_js_to_int32(in->weapon_state);
  out->viewangles = in->viewangles;
  out->viewheight = qa_js_to_int32(in->viewheight); out->damage_event = qa_js_to_int32(in->damage_event);
  out->damage_yaw = qa_js_to_int32(in->damage_yaw); out->damage_pitch = qa_js_to_int32(in->damage_pitch); out->damage_count = qa_js_to_int32(in->damage_count);
  for (int i = 0; i < 16; i++) {
    out->stats[i] = qa_js_to_int32(qa_q3_base_player_state_slots_get(&in->stats, i));
    out->persistant[i] = qa_js_to_int32(qa_q3_base_player_state_slots_get(&in->persistant, i));
    out->powerups[i] = qa_js_to_int32(qa_q3_base_player_state_slots_get(&in->powerups, i));
    out->ammo[i] = qa_js_to_int32(qa_q3_base_player_state_slots_get(&in->ammo, i));
  }
  out->generic1 = qa_js_to_int32(in->generic1); out->loop_sound = qa_js_to_int32(in->loop_sound);
  out->jumppad_ent = qa_js_to_int32(in->jumppad_ent); out->ping = qa_js_to_int32(in->ping);
  out->pmove_framecount = qa_js_to_int32(in->pmove_framecount); out->jumppad_frame = qa_js_to_int32(in->jumppad_frame);
  out->entity_event_sequence = qa_js_to_int32(in->entity_event_sequence);
}
static bool link_bounds(void *ctx, double number, qa_bounds *out) {
  app_bots *b = ctx;
  qa_q3_base_link_state state;
  if (!qa_q3_base_world_adapter_link_state(qa_app_q3_source_subsystems_owner_world(b->source->owner), number, &state)) return false;
  *out = state.absbounds; return true;
}
static void print_text(void *ctx, const char *text) { app_bots *b = ctx; b->options.print(b->options.ctx, text); }
static int64_t snapshot_entity(void *ctx, int64_t client, int64_t sequence) {
  app_bots *b = ctx;
  if (sequence < -2147483648LL || sequence > 2147483647LL) qa_range_error("Bot snapshot sequence must be a signed source integer");
  if (sequence < 0) return -1;
  bot_snapshot *snapshot = snapshot_of(b, client);
  if (snapshot == NULL && b->source == NULL) {
    int64_t count = b->game->entity_count(b->game->ctx);
    int64_t *entities = xcalloc((size_t)count, sizeof *entities);
    size_t n = 0;
    for (int64_t number = 0; number < count; number++) {
      qa_bot_observed_entity_bots_a entity = b->game->entity(b->game->ctx, number);
      if (entity.present && entity.linked && !entity.hidden) entities[n++] = number;
    }
    snapshot = add_snapshot(b, client, entities, n);
  }
  if (snapshot == NULL && b->source != NULL) {
    qa_app_q3_source_presentation_state *published = qa_app_q3_source_runtime_source_state(b->source->runtime);
    if (published == NULL) qa_range_error("Out of memory");
    const qa_app_q3_source_presentation_client *player = NULL;
    for (size_t i = 0; i < published->client_count && player == NULL; i++) if (published->clients[i].slot == (double)client) player = &published->clients[i];
    int64_t *entities = NULL;
    size_t n = 0;
    bool found = player != NULL;
    if (found) {
      qa_q3_player_state_record player_record;
      player_record_of(&player_record, &player->state);
      qa_app_q3_snapshot_source_entity *rows = xcalloc(published->entity_count, sizeof *rows);
      for (size_t i = 0; i < published->entity_count; i++) {
        rows[i].state = &published->entities[i].state; rows[i].linked = published->entities[i].linked;
        rows[i].server_flags = published->entities[i].server_flags; rows[i].single_client = published->entities[i].single_client;
      }
      qa_app_q3_snapshot_selection *selection = qa_app_select_application_q3_snapshot(&player_record, rows, published->entity_count,
          sim(b)->scene, link_bounds, b, b->options.leaf_count, print_text, b);
      if (selection == NULL) qa_range_error("Out of memory");
      const qa_q3_visible_entities *visible = qa_app_q3_snapshot_selection_visible(selection);
      entities = xcalloc(visible->entity_count, sizeof *entities);
      for (size_t i = 0; i < visible->entity_count; i++) entities[n++] = visible->entities[i]->number;
      qa_app_q3_snapshot_selection_destroy(selection);
      free(rows);
    }
    qa_app_q3_source_presentation_state_destroy(published);
    if (!found) return -1;
    snapshot = add_snapshot(b, client, entities, n);
  }
  return (size_t)sequence < snapshot->count ? snapshot->entities[sequence] : -1;
}
static const char *console_message(void *ctx, int64_t client) {
  app_bots *b = ctx;
  qa_q3_server_reliable_commands *ring = require_connection(b, client)->reliable;
  if (qa_q3_reliable_ring_acknowledge(ring) == qa_q3_reliable_ring_sequence(ring)) return NULL;
  qa_q3_reliable_ring_assign_acknowledgement(ring, qa_q3_reliable_ring_acknowledge(ring) + 1);
  const char *text = qa_q3_reliable_ring_lookup_masked(ring, qa_q3_reliable_ring_acknowledge(ring));
  return text[0] != '\0' ? text : NULL;
}
static int64_t host_point_contents(void *ctx, qa_vec3 point) {
  app_bots *b = ctx;
  return b->game->point_contents(b->game->ctx, point, -1);
}
static void bind_director_host(app_bots *b, qa_source_bot_game_bots_a *game) {
  b->host.ctx = b; b->host.game = game; b->host.provider = "q3:bot";
  b->host.allocate_client = allocate_client; b->host.actor = host_actor; b->host.encode_command = encode_command;
  b->host.snapshot_entity = snapshot_entity; b->host.console_message = console_message; b->host.point_contents = host_point_contents;
}
/* TS navigation: (library, bspEntities) => q3BotNavigation(game, library, navigation, bspEntities) */
static void navigation_factory(void *ctx, qa_bot_library_bots_a *library, qa_aas_bsp_entities *bsp_entities, qa_source_bot_navigation *out) {
  app_bots *b = ctx;
  qa_q3_bot_navigation_bots_a *navigation = xcalloc(1, sizeof *navigation);
  qa_q3_bot_navigation_bots_a_init(navigation, b->game, library, &b->options.navigation->selected, bsp_entities);
  if (b->navigation != NULL) {
    if (b->retired_navigation_count == 4) { free(b->retired_navigation[0]); memmove(b->retired_navigation, b->retired_navigation + 1, 3 * sizeof *b->retired_navigation); b->retired_navigation_count--; }
    b->retired_navigation[b->retired_navigation_count++] = b->navigation;
  }
  b->navigation = navigation;
  out->bot = qa_source_bot_navigation_interface(&navigation->navigation);
  out->goal = qa_source_bot_navigation_goal_interface(&navigation->navigation);
}
static void reset_view(app_bots *b, qa_actor_id actor, qa_vec3 angles) {
  if (!qa_session_actor_registry_is_live(sim(b)->actors, actor)) return;
  size_t count = 0;
  const qa_source_bot_roster_entry *roster = qa_source_bot_director_roster(b->director, &count);
  for (size_t i = 0; i < count; i++) {
    if (!qa_same_actor(roster[i].actor.id, actor)) continue;
    roster[i].state->src->viewangles = qa_bot_fv3(angles); roster[i].state->src->ideal_viewangles = qa_bot_fv3(angles);
    return;
  }
}
static void reset_weapon(app_bots *b, qa_actor_id actor) {
  if (!qa_session_actor_registry_is_live(sim(b)->actors, actor)) return;
  size_t count = 0;
  const qa_source_bot_roster_entry *roster = qa_source_bot_director_roster(b->director, &count);
  for (size_t i = 0; i < count; i++) {
    if (!qa_same_actor(roster[i].actor.id, actor)) continue;
    qa_bot_observed_entity_bots_a observed = b->game->entity(b->game->ctx, roster[i].source_client);
    if (!observed.has_player) fail("Bot spawn has no actual arsenal observation");
    roster[i].state->src->weapon_num = observed.state.weapon;
    return;
  }
}
static bool app_disconnect(void *ctx, int64_t client);
static const qa_app_sim_bot_client *app_clients(void *ctx, size_t *out_count);
static bool append_reliable(app_bots *b, qa_session_client *client, qa_q3_server_reliable_commands *reliable, const char *text) {
  qa_q3_server_command_append result = qa_q3_server_reliable_commands_add(reliable, text);
  if (result.kind != QA_Q3_APPEND_OVERFLOW) return true;
  int64_t slot = qa_session_client_id(client).slot;
  b->game->options.engine.drop_client(b->game->options.engine.ctx, slot, "Server command overflow");
  app_disconnect(b, slot);
  return false;
}
static void app_receive(void *ctx, const qa_app_sim_presentation_event *events, size_t count) {
  app_bots *b = ctx;
  for (size_t i = 0; i < count; i++) {
    const qa_app_sim_presentation_event *event = &events[i];
    if (is(event->kind, "view-reset") && b->shared != NULL) {
      reset_view(b, event->view_reset.actor, event->view_reset.angles);
      if (is(event->view_reset.reason, "spawn")) reset_weapon(b, event->view_reset.actor);
      continue;
    }
    if (!is(event->kind, "q3-source") || event->q3_source == NULL
        || event->q3_source->kind != QA_APP_Q3_EVENT_SERVER_COMMAND) continue;
    const qa_app_q3_source_event *command = event->q3_source;
    size_t client_count = 0;
    const qa_app_sim_bot_client *views = app_clients(b, &client_count);
    qa_app_sim_bot_client *clients = xcalloc(client_count, sizeof *clients);
    memcpy(clients, views, client_count * sizeof *clients);
    for (size_t c = 0; c < client_count; c++) {
      qa_client_id id = qa_session_client_id(clients[c].client);
      if (event->has_recipient) {
        qa_actor_id owner;
        if (!(b->service.actor(b, id, &owner) && qa_same_actor(owner, event->recipient))) continue;
      }
      if (command->client != -1 && command->client != (double)id.slot) continue;
      append_reliable(b, clients[c].client, clients[c].reliable, command->text);
    }
    free(clients);
  }
}
static const qa_actor_command *app_frame(void *ctx, double time_milliseconds, double elapsed_milliseconds, size_t *out_count) {
  app_bots *b = ctx;
  *out_count = 0;
  if (b->closed) fail("Bot transport is closed");
  if (b->restart != RESTART_ACTIVE) return NULL;
  b->elapsed_milliseconds = elapsed_milliseconds;
  snapshots_clear(b);
  if (b->shared != NULL) qa_app_sim_shared_bot_world_refresh(b->shared);
  double source_time = b->options.automatic_frame && b->source != NULL
      && qa_cvar_registry_variable_value(b->source->host->cvars, "dedicated") != 0
      ? qa_app_q3_source_runtime_level_time(b->source->runtime) : time_milliseconds;
  qa_bot_frame frame = {source_time, elapsed_milliseconds};
  return qa_shared_bot_population_frame(&b->population, frame, out_count);
}
static const qa_app_sim_bot_client *app_clients(void *ctx, size_t *out_count) {
  app_bots *b = ctx;
  size_t n = 0;
  if (b->restart != RESTART_ACTIVE) {
    b->client_views = xrealloc(b->client_views, b->phase_client_count, sizeof *b->client_views);
    b->client_view_capacity = b->phase_client_count;
    for (size_t i = 0; i < b->phase_client_count; i++) {
      if (qa_session_client_is_closed(b->phase_clients[i].client)) continue;
      b->client_views[n].client = b->phase_clients[i].client; b->client_views[n].reliable = b->phase_clients[i].reliable;
      b->client_views[n].userinfo = b->phase_clients[i].userinfo; n++;
    }
    *out_count = n;
    return b->client_views;
  }
  b->client_views = xrealloc(b->client_views, b->connection_count, sizeof *b->client_views);
  b->client_view_capacity = b->connection_count;
  for (size_t i = 0; i < b->connection_count; i++) {
    b->client_views[i].client = b->connections[i].client; b->client_views[i].reliable = b->connections[i].reliable;
    b->client_views[i].userinfo = b->game->options.engine.get_userinfo(b->game->options.engine.ctx, b->connections[i].slot);
  }
  *out_count = b->connection_count;
  return b->client_views;
}
qa_save_value *qa_app_sim_application_bots_checkpoint_orchestration(app_bots *b) {
  if (b->closed) fail("Cannot checkpoint a closed bot transport");
  qa_save_value *o = obj(), *connections = arr(), *snapshots = arr();
  put(o, "version", num(1)); put(o, "elapsedMilliseconds", num(b->elapsed_milliseconds));
  for (size_t i = 0; i < b->connection_count; i++)
    push(connections, qa_app_sim_encode_bot_transport_connection(b->connections[i].client, b->connections[i].actor.id, b->connections[i].reliable));
  put(o, "connections", connections);
  for (size_t i = 0; i < b->snapshot_count; i++) {
    qa_save_value *entry = obj(), *entities = arr();
    put(entry, "client", num((double)b->snapshots[i].client));
    for (size_t e = 0; e < b->snapshots[i].count; e++) push(entities, num((double)b->snapshots[i].entities[e]));
    put(entry, "entities", entities);
    push(snapshots, entry);
  }
  put(o, "snapshots", snapshots);
  return o;
}
/* Bind existing restored players without allocating clients, spawning actors or invoking admission. */
static void restore_orchestration_image(app_bots *b, const qa_app_sim_bot_transport_checkpoint *image) {
  const qa_app_sim_bot_simulation *s = sim(b);
  if (b->closed || !isfinite(image->elapsed_milliseconds) || image->elapsed_milliseconds < 0) fail("Invalid bot transport restoration");
  bot_connection *connections = xcalloc(image->connection_count, sizeof *connections);
  size_t connection_count = 0;
  for (size_t i = 0; i < image->connection_count; i++) {
    const qa_app_sim_bot_transport_connection *entry = &image->connections[i];
    qa_session_client *client = b->options.resolve_client(b->options.ctx, entry->client_slot, entry->client_generation);
    qa_owned_actor actor;
    bool has_actor = qa_session_actor_registry_resolve_saved(s->actors, entry->actor, &actor);
    bool invalid = client == NULL || qa_session_client_is_closed(client) || !has_actor;
    if (!invalid) {
      qa_client_id id = qa_session_client_id(client);
      invalid = id.session.token != qa_engine_session_id(b->options.session).token || (double)id.slot != entry->client_slot;
      for (size_t j = 0; j < connection_count && !invalid; j++)
        invalid = qa_same_owned_actor(connections[j].actor, actor) || connections[j].slot == id.slot;
    }
    if (invalid) fail("Invalid saved bot transport binding");
    qa_client_id id = qa_session_client_id(client);
    qa_app_sim_movement_player *player = s->movement_player(s->ctx, actor.id);
    if (player == NULL || !qa_same_client(player->client, id)) fail("Saved bot transport does not match the restored player client");
    qa_q3_server_reliable_commands *reliable = qa_app_sim_restored_bot_reliable_commands(entry);
    if (reliable == NULL) qa_range_error("Out of memory");
    connections[connection_count].slot = id.slot; connections[connection_count].client = client;
    connections[connection_count].reliable = reliable; connections[connection_count].actor = actor; connection_count++;
  }
  int64_t entity_count = b->game->entity_count(b->game->ctx);
  bot_snapshot *snapshots = xcalloc(image->snapshot_count, sizeof *snapshots);
  size_t snapshot_count = 0;
  for (size_t i = 0; i < image->snapshot_count; i++) {
    const qa_app_sim_bot_transport_snapshot *snapshot = &image->snapshots[i];
    bool known = false, duplicate = false, invalid = false;
    for (size_t j = 0; j < connection_count; j++) if ((double)connections[j].slot == snapshot->client) known = true;
    for (size_t j = 0; j < snapshot_count; j++) if ((double)snapshots[j].client == snapshot->client) duplicate = true;
    for (size_t e = 0; e < snapshot->entity_count; e++) {
      double entity = snapshot->entities[e];
      if (entity != trunc(entity) || fabs(entity) > 9007199254740991.0 || entity < 0 || entity >= (double)entity_count) invalid = true;
    }
    if (!known || duplicate || invalid) fail("Invalid saved bot entity snapshot");
    snapshots[snapshot_count].client = (int64_t)snapshot->client;
    snapshots[snapshot_count].count = snapshot->entity_count;
    snapshots[snapshot_count].entities = xcalloc(snapshot->entity_count, sizeof *snapshots[snapshot_count].entities);
    for (size_t e = 0; e < snapshot->entity_count; e++) snapshots[snapshot_count].entities[e] = (int64_t)snapshot->entities[e];
    snapshot_count++;
  }
  for (size_t i = 0; i < b->connection_count; i++) qa_q3_reliable_ring_destroy(b->connections[i].reliable);
  b->connection_count = 0; snapshots_clear(b);
  for (size_t i = 0; i < connection_count; i++) set_connection(b, connections[i]);
  for (size_t i = 0; i < snapshot_count; i++) add_snapshot(b, snapshots[i].client, snapshots[i].entities, snapshots[i].count);
  free(connections); free(snapshots);
  b->elapsed_milliseconds = image->elapsed_milliseconds;
}
void qa_app_sim_application_bots_restore_orchestration(app_bots *b, const qa_save_value *value) {
  qa_app_sim_bot_transport_checkpoint image;
  qa_app_sim_decode_bot_transport(value, &image);
  restore_orchestration_image(b, &image);
  qa_app_sim_bot_transport_checkpoint_free(&image);
}
static qa_app_sim_bot_navigation *persistent_navigation(app_bots *b) {
  qa_app_sim_bot_navigation *navigation = b->options.navigation;
  if (navigation->checkpoint == NULL || navigation->restore_checkpoint == NULL) fail("Exact bot persistence requires navigation continuation support");
  return navigation;
}
static qa_app_sim_bot_arsenal *knowledge_of(app_bots *b) {
  return b->arsenal != NULL ? b->arsenal : b->shared != NULL ? qa_app_sim_shared_bot_world_knowledge(b->shared) : NULL;
}
static qa_save_value *app_checkpoint(void *ctx) {
  app_bots *b = ctx;
  const qa_app_sim_bot_simulation *s = sim(b);
  if (b->closed) fail("Cannot checkpoint a closed bot transport");
  qa_save_value *observations = arr();
  if (b->source != NULL) {
    qa_q3_base_ownership_state *records = qa_q3_base_entity_records_capture_ownership(qa_app_q3_source_subsystems_owner_records(b->source->owner));
    if (records == NULL) qa_range_error("Out of memory");
    for (size_t number = 0; number < QA_Q3_MAX_GENTITIES; number++) {
      if (!records[number].has_actor) continue;
      qa_save_value *entry = obj();
      put(entry, "number", num((double)number)); put(entry, "actor", saved_actor_value(records[number].actor.id));
      push(observations, entry);
    }
    free(records);
  } else {
    size_t count = 0;
    const qa_actor_id *players = s->players(s->ctx, &count);
    for (size_t i = 0; i < count; i++) {
      qa_app_sim_movement_player *player = s->movement_player(s->ctx, players[i]);
      if (player == NULL) continue;
      qa_save_value *entry = obj();
      put(entry, "number", num((double)player->client.slot)); put(entry, "actor", saved_actor_value(players[i]));
      push(observations, entry);
    }
  }
  qa_app_sim_bot_arsenal *knowledge = knowledge_of(b);
  qa_save_value *root = obj();
  put(root, "version", num(1));
  put(root, "transport", qa_app_sim_application_bots_checkpoint_orchestration(b));
  qa_save_value *director = qa_source_bot_director_capture_save_state(b->director);
  if (director == NULL) fail(qa_source_bot_director_error(b->director));
  put(root, "director", director);
  qa_app_sim_bot_navigation *navigation = persistent_navigation(b);
  put(root, "navigation", navigation->checkpoint(navigation->ctx));
  put(root, "knowledge", knowledge != NULL ? qa_app_sim_bot_arsenal_checkpoint(knowledge) : qa_save_value_null());
  put(root, "sharedWorld", b->shared != NULL ? qa_app_sim_shared_bot_world_checkpoint(b->shared) : qa_save_value_null());
  put(root, "observations", observations);
  return root;
}
static bool is_null_value(const qa_save_value *value) { return value != NULL && value->kind == QA_SAVE_NULL; }
typedef struct restore_ctx { app_bots *b; const qa_app_sim_bot_observation *observations; size_t observation_count; } restore_ctx;
static qa_actor_id restore_reference(void *ctx, qa_saved_actor_id saved) {
  restore_ctx *r = ctx;
  return qa_session_actor_registry_reference_saved(sim(r->b)->actors, saved, NULL);
}
static bool restore_resolve_owned(void *ctx, qa_saved_actor_id saved, qa_owned_actor *out) {
  restore_ctx *r = ctx;
  return qa_session_actor_registry_resolve_saved(sim(r->b)->actors, saved, out);
}
static bool restore_edge(void *ctx, int64_t client, int64_t edge) {
  restore_ctx *r = ctx;
  qa_app_sim_bot_navigation *navigation = r->b->options.navigation;
  qa_navigation_runtime *runtime = navigation->selected.for_client(navigation->selected.ctx, client);
  return qa_navigation_runtime_edge(runtime, edge) != NULL;
}
static void restore_remap(void *ctx, int64_t number, int64_t generation, int64_t *out_number, int64_t *out_generation) {
  restore_ctx *r = ctx;
  const qa_app_sim_bot_observation *saved = NULL;
  for (size_t i = 0; i < r->observation_count && saved == NULL; i++) if (r->observations[i].number == (double)number) saved = &r->observations[i];
  *out_number = number;
  if (saved == NULL || generation == 0) { *out_generation = generation; return; }
  qa_saved_actor_id id = {saved->actor.slot, (double)generation};
  *out_generation = restore_reference(r, id).generation;
}
static int64_t restore_weapon_handle(void *ctx, int64_t handle) {
  (void)ctx;
  if (handle < 0 || handle > QA_MAX_WEAPON_STATES) fail("Saved bot knowledge weapon handle exceeds source capacity");
  return handle;
}
static void restore_checkpoint(app_bots *b, const qa_app_sim_decoded_bots_checkpoint *image) {
  const qa_app_sim_bot_simulation *s = sim(b);
  qa_app_sim_bot_navigation *navigation = persistent_navigation(b);
  restore_ctx r = {b, NULL, 0};
  if (is_null_value(image->shared_world) != (b->shared == NULL)) fail("Saved bot world projection differs from selected source");
  navigation->restore_checkpoint(navigation->ctx, image->navigation);
  if (b->shared != NULL) qa_app_sim_shared_bot_world_restore_checkpoint(b->shared, image->shared_world, restore_reference, &r);
  restore_orchestration_image(b, &image->transport);
  qa_app_sim_bot_observation *observations = xcalloc(image->observation_count, sizeof *observations);
  size_t observation_count = 0;
  qa_q3_base_ownership_state *native = b->source != NULL
      ? qa_q3_base_entity_records_capture_ownership(qa_app_q3_source_subsystems_owner_records(b->source->owner)) : NULL;
  if (b->source != NULL && native == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < image->observation_count; i++) {
    const qa_app_sim_bot_observation *entry = &image->observations[i];
    bool duplicate = false;
    for (size_t j = 0; j < observation_count; j++) if (observations[j].number == entry->number) duplicate = true;
    if (duplicate || (b->source == NULL && entry->number >= 64)) fail("Invalid saved bot observation authority");
    bool has_current = false;
    qa_actor_id current = {0};
    if (native != NULL && entry->number < QA_Q3_MAX_GENTITIES && native[(size_t)entry->number].has_actor) {
      current = native[(size_t)entry->number].actor.id; has_current = true;
    } else if (b->shared != NULL) has_current = qa_app_sim_shared_bot_world_actor_for_id(b->shared, (int64_t)entry->number, &current);
    if (!has_current || !qa_same_actor(current, restore_reference(&r, entry->actor))) fail("Saved bot observation authority differs from restored source");
    observations[observation_count++] = *entry;
  }
  size_t expected = 0;
  if (native != NULL) { for (size_t n = 0; n < QA_Q3_MAX_GENTITIES; n++) if (native[n].has_actor) expected++; }
  else s->players(s->ctx, &expected);
  free(native);
  if (observation_count != expected) fail("Saved bot observation authority is incomplete");
  r.observations = observations; r.observation_count = observation_count;
  qa_source_bot_restore_hooks hooks = {restore_reference, &r, restore_resolve_owned, &r, restore_edge, &r, restore_remap, &r};
  if (!qa_source_bot_director_restore_save_state(b->director, image->director, &hooks)) fail(qa_source_bot_director_error(b->director));
  qa_app_sim_bot_arsenal *knowledge = knowledge_of(b);
  if (is_null_value(image->knowledge) != (knowledge == NULL)) fail("Saved bot arsenal knowledge differs from selected source");
  if (knowledge != NULL) qa_app_sim_bot_arsenal_restore_checkpoint(knowledge, image->knowledge, restore_reference, &r, restore_weapon_handle, &r);
  free(observations);
}
static void restore_client(app_bots *b, qa_session_client *client, qa_q3_server_reliable_commands *reliable, const char *userinfo) {
  qa_client_id id = qa_session_client_id(client);
  prepare_client(b, client, reliable);
  qa_source_bot_engine_bots_a *engine = &b->game->options.engine;
  if (b->source == NULL) {
    b->game->activate_bot(b->game->ctx, id.slot);
    engine->set_userinfo(engine->ctx, id.slot, userinfo != NULL ? userinfo : "");
    const char *rejection = b->game->client_connect(b->game->ctx, id.slot, false, true);
    if (rejection != NULL) fail(rejection);
    b->game->client_begin(b->game->ctx, id.slot);
    return;
  }
  if (userinfo != NULL) engine->set_userinfo(engine->ctx, id.slot, userinfo);
  else {
    char *current = xstrdup(engine->get_userinfo(engine->ctx, id.slot));
    engine->set_userinfo(engine->ctx, id.slot, current);
    free(current);
  }
  qa_q3_entity_pool *pool = qa_app_q3_source_subsystems_owner_pool(b->source->owner);
  qa_q3_game_entity *entity = qa_q3_entity_pool_at(pool, (double)id.slot);
  entity->r.sv_flags = (double)(qa_js_to_int32(entity->r.sv_flags) | QA_Q3_BASE_SVF_BOT);
  qa_q3_entity_pool_activate_client(pool, (double)id.slot);
  qa_q3_ta_client_admission_runtime *admission = qa_app_q3_source_subsystems_owner_admission(b->source->owner);
  const char *rejection = qa_q3_ta_client_admission_runtime_connect(admission, (double)id.slot, false, true);
  if (rejection != NULL) qa_raise("Error", 0, 0, "Source bot restoration rejected: %s", rejection);
  qa_q3_ta_client_admission_runtime_begin(admission, (double)id.slot);
}
static bool app_disconnect(void *ctx, int64_t client) {
  app_bots *b = ctx;
  bot_connection *connection = connection_at(b, client);
  qa_session_client *session_client = connection != NULL ? connection->client : NULL;
  if (session_client == NULL && b->restart != RESTART_ACTIVE)
    for (size_t i = 0; i < b->phase_client_count && session_client == NULL; i++)
      if (qa_session_client_id(b->phase_clients[i].client).slot == client) session_client = b->phase_clients[i].client;
  if (session_client == NULL || qa_session_client_is_closed(session_client)) return false;
  qa_client_id id = qa_session_client_id(session_client);
  if (connection != NULL) {
    if (b->source == NULL) qa_source_bot_director_shutdown_client(b->director, client, false);
    sim(b)->disconnect_player(sim(b)->ctx, connection->actor.id);
  }
  delete_connection(b, client, true); delete_snapshot(b, client);
  if (b->restart == RESTART_BOUND)
    for (size_t i = 0; i < b->pending_count; i++)
      if (b->pending[i] == client) { memmove(&b->pending[i], &b->pending[i + 1], (b->pending_count - i - 1) * sizeof *b->pending); b->pending_count--; break; }
  qa_engine_session_close_client(b->options.session, id);
  return true;
}
static void phase_clear(app_bots *b) {
  for (size_t i = 0; i < b->phase_client_count; i++) {
    if (b->phase_clients[i].reliable != NULL) qa_q3_reliable_ring_destroy(b->phase_clients[i].reliable);
    free(b->phase_clients[i].userinfo);
  }
  free(b->phase_clients); b->phase_clients = NULL; b->phase_client_count = 0;
  free(b->pending); b->pending = NULL; b->pending_count = 0;
}
static const qa_app_sim_bot_client *app_begin_round_restart(void *ctx, size_t *out_count) {
  app_bots *b = ctx;
  if (b->closed || b->restart != RESTART_ACTIVE || b->source == NULL) fail("Bot fast restart requires an active native Q3 round");
  if (b->options.navigation->restart_round == NULL) fail("Bot fast restart requires retained application navigation");
  size_t count = 0;
  const qa_app_sim_bot_client *clients = app_clients(b, &count);
  b->phase_clients = xcalloc(count, sizeof *b->phase_clients);
  for (size_t i = 0; i < count; i++) {
    b->phase_clients[i].client = clients[i].client; b->phase_clients[i].reliable = clients[i].reliable;
    b->phase_clients[i].userinfo = xstrdup(clients[i].userinfo);
  }
  b->phase_client_count = count;
  qa_source_bot_director_begin_round_restart(b->director);
  qa_app_sim_bot_services_detach(sim(b)->bot_services, b->director);
  b->connection_count = 0; /* rings now owned by the phase clients */
  snapshots_clear(b); b->elapsed_milliseconds = 0;
  b->detached_source = b->source; b->restart = RESTART_DETACHED;
  return app_clients(b, out_count);
}
static void app_bind_restarted_round(void *ctx) {
  app_bots *b = ctx;
  const qa_app_sim_bot_q3_source *source = sim(b)->q3_source(sim(b)->ctx);
  qa_app_sim_bot_navigation *navigation = b->options.navigation;
  if (b->closed || b->restart != RESTART_DETACHED || source == NULL || source == b->detached_source) fail("Publish a new source round before rebinding bots");
  if (navigation->restart_round == NULL) fail("Bot fast restart lost application navigation");
  navigation->restart_round(navigation->ctx);
  b->source = source;
  qa_app_sim_bot_arsenal_destroy(b->arsenal);
  b->arsenal = qa_app_sim_create_bot_arsenal(sim(b), connection_actor_for_client, b);
  qa_q3_bot_game_bots_a *game = xcalloc(1, sizeof *game);
  if (!qa_q3_bot_game_bots_a_init(game, source->owner, source->host, b->options.insert_console_command, b->options.ctx,
          b->arsenal != NULL ? &b->arsenal->q3_binding : NULL)) qa_range_error("Out of memory");
  if (b->q3_game != NULL) {
    /* The previous round's game stays addressable for the director's retired round. */
    if (b->retired_game_count == 4) {
      qa_q3_bot_game_bots_a_destroy(b->retired_games[0]); free(b->retired_games[0]);
      memmove(b->retired_games, b->retired_games + 1, 3 * sizeof *b->retired_games); b->retired_game_count--;
    }
    b->retired_games[b->retired_game_count++] = b->q3_game;
  }
  b->q3_game = game; b->game = &game->game;
  bind_director_host(b, b->game);
  if (!qa_source_bot_director_bind_restarted_round(b->director, &b->host, navigation_factory, b)) fail(qa_source_bot_director_error(b->director));
  qa_app_sim_bot_services_attach(sim(b)->bot_services, b->director, &b->service);
  b->pending = xcalloc(b->phase_client_count, sizeof *b->pending);
  b->pending_count = 0;
  for (size_t i = 0; i < b->phase_client_count; i++)
    if (!qa_session_client_is_closed(b->phase_clients[i].client)) b->pending[b->pending_count++] = qa_session_client_id(b->phase_clients[i].client).slot;
  b->restart = RESTART_BOUND;
}
/* Call once for each preserved slot after the three source settle frames; humans return false. */
static bool app_reconnect_restarted_client(void *ctx, qa_client_id client) {
  app_bots *b = ctx;
  if (b->closed || b->restart != RESTART_BOUND) fail("Bind the new bot round before reconnecting clients");
  phase_client *saved = NULL;
  for (size_t i = 0; i < b->phase_client_count && saved == NULL; i++)
    if (qa_same_client(qa_session_client_id(b->phase_clients[i].client), client)) saved = &b->phase_clients[i];
  if (saved == NULL) return false;
  size_t pending_index = b->pending_count;
  for (size_t i = 0; i < b->pending_count; i++) if (b->pending[i] == client.slot) pending_index = i;
  if (qa_session_client_is_closed(saved->client)) {
    if (pending_index < b->pending_count) { memmove(&b->pending[pending_index], &b->pending[pending_index + 1], (b->pending_count - pending_index - 1) * sizeof *b->pending); b->pending_count--; }
    return true;
  }
  if (pending_index == b->pending_count) fail("Bot restart client was already reconnected");
  if (!append_reliable(b, saved->client, saved->reliable, "map_restart\n")) return true;
  qa_q3_server_reliable_commands *reliable = saved->reliable;
  saved->reliable = NULL; /* owned by the connection */
  restore_client(b, saved->client, reliable, saved->userinfo);
  for (size_t i = 0; i < b->pending_count; i++)
    if (b->pending[i] == client.slot) { memmove(&b->pending[i], &b->pending[i + 1], (b->pending_count - i - 1) * sizeof *b->pending); b->pending_count--; break; }
  return true;
}
/* The caller owns the final source settle frame and must complete it before resuming. */
static void app_resume_round_bots(void *ctx) {
  app_bots *b = ctx;
  if (b->closed || b->restart != RESTART_BOUND || b->pending_count != 0) fail("Reconnect every preserved bot before resuming");
  qa_source_bot_director_resume_round_bots(b->director);
  phase_clear(b);
  b->restart = RESTART_ACTIVE;
}
static int32_t app_move_to_point(void *ctx, qa_actor_id actor, qa_vec3 point, double tolerance) { (void)ctx; (void)actor; (void)point; (void)tolerance; return 0; }
static int32_t app_follow_actor(void *ctx, qa_actor_id actor, qa_actor_id target) { (void)ctx; (void)actor; (void)target; return 0; }
static bool app_is_bot(void *ctx, qa_actor_id actor) {
  app_bots *b = ctx;
  for (size_t i = 0; i < b->connection_count; i++) if (qa_same_actor(b->connections[i].actor.id, actor)) return true;
  return false;
}
static bool app_actor(void *ctx, qa_client_id client, qa_actor_id *out) {
  bot_connection *c = connection_at(ctx, client.slot);
  if (c == NULL || !qa_same_client(qa_session_client_id(c->client), client)) return false;
  *out = c->actor.id; return true;
}
static void app_console_command(void *ctx, const char *const *argv, size_t argc) {
  app_bots *b = ctx;
  qa_source_bot_director_console_command(b->director, argv, argc);
}
static void app_close(void *ctx, bool restart) {
  app_bots *b = ctx;
  if (b->closed) return;
  qa_source_bot_director_close(b->director, restart);
  if (b->restart != RESTART_DETACHED) qa_app_sim_bot_services_detach(sim(b)->bot_services, b->director);
  snapshots_clear(b);
  for (size_t i = 0; i < b->connection_count; i++) qa_q3_reliable_ring_destroy(b->connections[i].reliable);
  b->connection_count = 0; b->closed = true;
}
static qa_cvar_registry *app_configuration(void *ctx) { app_bots *b = ctx; return b->game->options.cvars; }

/* ---- construction ---- */
static bool shared_actor(void *ctx, int64_t client, qa_actor_id *out) { return connection_actor_for_client(ctx, client, out); }
static bool shared_connect(void *ctx, int64_t client, bool restart) { app_bots *b = ctx; return qa_source_bot_director_connect(b->director, client, restart); }
static void shared_drop(void *ctx, int64_t client) { app_disconnect(ctx, client); }
static void shared_begin(void *ctx, int64_t client) {
  app_bots *b = ctx;
  bot_connection *c = connection_at(b, client);
  if (c == NULL) fail("Bot begin has no actual player");
  qa_actor_id actor = c->actor.id;
  qa_app_sim_movement_player *player = sim(b)->movement_player(sim(b)->ctx, actor);
  if (player == NULL) fail("Bot begin has no actual view");
  reset_view(b, actor, player->view_angles);
  reset_weapon(b, actor);
}
static void shared_print(void *ctx, const char *text) { app_bots *b = ctx; b->options.print(b->options.ctx, text); }
static void shared_console(void *ctx, const char *text) { app_bots *b = ctx; b->options.insert_console_command(b->options.ctx, text); }
static void shared_message(void *ctx, int64_t client, const char *text) {
  app_bots *b = ctx;
  for (size_t i = 0; i < b->connection_count; i++)
    if (client == -1 || client == b->connections[i].slot) qa_q3_server_reliable_commands_add(b->connections[i].reliable, text);
}
static int64_t library_random(void *ctx) { app_bots *b = ctx; return (int64_t)qa_app_sim_source_random_next_integer(sim(b)->random); }
static double library_milliseconds(void *ctx) { app_bots *b = ctx; return sim(b)->time_seconds(sim(b)->ctx) * 1000; }
static void library_print(void *ctx, int severity, const char *text) { (void)severity; app_bots *b = ctx; b->options.print(b->options.ctx, text); }
static const uint8_t *library_read(void *ctx, const char *path, size_t *out_len) {
  app_bots *b = ctx;
  return b->options.files.files.read(b->options.files.files.ctx, path, out_len);
}
static void library_client_command(void *ctx, int64_t client, const char *text) {
  app_bots *b = ctx;
  bot_connection *connection = require_connection(b, client);
  qa_command_tokens tokens;
  if (!qa_tokenize_command(text, QA_COMMAND_DIALECT_Q3, QA_COMMAND_TEXT_SOURCE, &tokens)) qa_range_error("Out of memory");
  sim(b)->player_command(sim(b)->ctx, connection->actor.id, tokens.argc > 0 ? tokens.argv[0] : "",
      (const char *const *)(tokens.argc > 0 ? tokens.argv + 1 : tokens.argv), tokens.argc > 0 ? tokens.argc - 1 : 0);
  qa_command_tokens_destroy(&tokens);
}
static qa_bot_log_open_result library_open_log(void *ctx, const char *filename) { app_bots *b = ctx; return b->options.open_log(b->options.ctx, filename); }
static qa_bot_log_open_result library_resume_log(void *ctx, const char *filename, int64_t position) {
  app_bots *b = ctx; return b->options.resume_log(b->options.ctx, filename, position);
}
static bool population_is_live(void *ctx, qa_actor_id actor) { app_bots *b = ctx; return qa_session_actor_registry_is_live(sim(b)->actors, actor); }

typedef struct construct_attempt { app_bots *b; bool attached; } construct_attempt;
static void construct_run(void *ctx) {
  construct_attempt *a = ctx;
  app_bots *b = a->b;
  if (b->options.restore != NULL) {
    restore_checkpoint(b, b->options.restore);
    qa_app_sim_bot_services_attach(sim(b)->bot_services, b->director, &b->service); a->attached = true;
  } else {
    qa_app_sim_bot_services_attach(sim(b)->bot_services, b->director, &b->service); a->attached = true;
    if (!qa_source_bot_director_load(b->director, b->options.restart)) fail(qa_source_bot_director_error(b->director));
    for (size_t i = 0; i < b->options.client_count; i++) {
      const qa_app_sim_bot_client *client = &b->options.clients[i];
      restore_client(b, client->client, client->reliable, client->userinfo);
    }
  }
}
qa_app_sim_application_bots *qa_app_sim_application_bots_create(const qa_app_sim_bots_options *options) {
  const qa_app_sim_bot_simulation *s = options->simulation;
  const char *error = qa_app_sim_bot_admission_error(s);
  if (error != NULL) fail(error);
  const qa_app_sim_bot_q3_source *source = s->q3_source(s->ctx);
  if (source == NULL && legacy_bot_admission_error(s) != NULL) fail(legacy_bot_admission_error(s));
  if (qa_engine_session_id(options->session).token != s->session.token) fail("Bot clients and simulation belong to different sessions");
  app_bots *b = calloc(1, sizeof *b);
  if (b == NULL) return NULL;
  b->options = *options; b->source = source; b->restart = RESTART_ACTIVE;
  b->service.ctx = b; b->service.options = &b->options;
  b->service.configuration = app_configuration; b->service.move_to_point = app_move_to_point; b->service.follow_actor = app_follow_actor;
  b->service.is_bot = app_is_bot; b->service.actor = app_actor; b->service.frame = app_frame; b->service.receive = app_receive;
  b->service.clients = app_clients; b->service.console_command = app_console_command; b->service.disconnect = app_disconnect;
  b->service.close = app_close; b->service.checkpoint = app_checkpoint; b->service.begin_round_restart = app_begin_round_restart;
  b->service.bind_restarted_round = app_bind_restarted_round; b->service.reconnect_restarted_client = app_reconnect_restarted_client;
  b->service.resume_round_bots = app_resume_round_bots;
  b->arsenal = source == NULL ? NULL : qa_app_sim_create_bot_arsenal(s, players_actor_for_client, b);
  if (source == NULL && options->configuration == NULL) fail("Shared bot configuration requires the application console registry");
  if (source == NULL) {
    qa_app_sim_shared_bot_world_options world;
    world.restoring = options->restore != NULL; world.simulation = s; world.cvars = options->configuration; world.ctx = b;
    world.actor = shared_actor; world.connect = shared_connect; world.drop = shared_drop; world.begin = shared_begin;
    world.print = shared_print; world.console = shared_console; world.message = shared_message;
    b->shared = qa_app_sim_shared_bot_world_create(&world);
    if (b->shared == NULL) qa_range_error("Out of memory");
    b->game = qa_app_sim_shared_bot_world_game(b->shared);
  } else {
    b->q3_game = xcalloc(1, sizeof *b->q3_game);
    if (!qa_q3_bot_game_bots_a_init(b->q3_game, source->owner, source->host, options->insert_console_command, options->ctx,
            b->arsenal != NULL ? &b->arsenal->q3_binding : NULL)) qa_range_error("Out of memory");
    b->game = &b->q3_game->game;
  }
  if (b->game == NULL) fail("Bot world projection is unavailable");
  bind_director_host(b, b->game);
  qa_source_bot_director_options director;
  memset(&director, 0, sizeof director);
  director.host = &b->host; director.files = options->files;
  director.entities = source != NULL ? qa_app_q3_source_runtime_options(source->runtime)->entities : s->source_entity_text(s->ctx);
  director.restoring = options->restore != NULL;
  director.library.ctx = b; director.library.read_file = library_read;
  director.library.random.next_int = library_random; director.library.random.ctx = b;
  director.library.debug = false; director.library.milliseconds = library_milliseconds; director.library.print = library_print;
  director.library.client_command = library_client_command; director.library.open_log = library_open_log;
  director.library.resume_log = options->resume_log != NULL ? library_resume_log : NULL;
  director.navigation = navigation_factory; director.navigation_ctx = b;
  b->director = qa_source_bot_director_create(&director);
  if (b->director == NULL) qa_range_error("Out of memory");
  qa_shared_bot_population_init(&b->population, population_is_live, b, b->director);
  construct_attempt attempt = {b, false};
  qa_caught_error caught;
  if (!qa_try(construct_run, &attempt, &caught)) {
    if (attempt.attached) qa_app_sim_bot_services_detach(s->bot_services, b->director);
    if (options->restore == NULL) qa_source_bot_director_close(b->director, false);
    else qa_bot_log_shutdown(&b->director->library.log);
    qa_bot_memory_dispose(&b->director->library.memory);
    qa_rethrow(&caught);
  }
  return b;
}
void qa_app_sim_application_bots_destroy(qa_app_sim_application_bots *b) {
  if (b == NULL) return;
  qa_source_bot_director_destroy(b->director);
  for (size_t i = 0; i < b->retired_navigation_count; i++) free(b->retired_navigation[i]);
  free(b->navigation);
  for (size_t i = 0; i < b->retired_game_count; i++) { qa_q3_bot_game_bots_a_destroy(b->retired_games[i]); free(b->retired_games[i]); }
  if (b->q3_game != NULL) { qa_q3_bot_game_bots_a_destroy(b->q3_game); free(b->q3_game); }
  qa_app_sim_shared_bot_world_destroy(b->shared);
  qa_app_sim_bot_arsenal_destroy(b->arsenal);
  for (size_t i = 0; i < b->connection_count; i++) qa_q3_reliable_ring_destroy(b->connections[i].reliable);
  snapshots_clear(b);
  phase_clear(b);
  free(b->connections); free(b->snapshots); free(b->client_views); free(b);
}
const qa_app_sim_bot_service *qa_app_sim_application_bots_service(qa_app_sim_application_bots *b) { return &b->service; }
qa_source_bot_director *qa_app_sim_application_bots_director(qa_app_sim_application_bots *b) { return b->director; }
qa_source_bot_game_bots_a *qa_app_sim_application_bots_game(qa_app_sim_application_bots *b) { return b->game; }
