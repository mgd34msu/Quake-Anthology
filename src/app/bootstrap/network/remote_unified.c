/* Port of src/app/bootstrap/network/remote-unified.ts.
 * GPL-2.0-or-later. */
#include "qa/app/bootstrap/network/remote_unified.h"
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/network/unified_component_consumer.h"
#include "qa/app/bootstrap/network/unified_event_codec.h"
#include "qa/app/bootstrap/network/unified_event_values.h"
#include "qa/app/bootstrap/presentation_state.h"
#include "qa/app/bootstrap/network/unified_native_consumer.h"
#include "qa/app/bootstrap/simulation/players.h"
#include "qa/foundation/support.h"
#include "qa/world/collision/index.h"
static void *xmalloc(size_t size) {
  void *p = malloc(size ? size : 1);
  if (!p) qa_range_error("out of memory");
  return p;
}
static char *copy_text(const char *s) {
  size_t n = strlen(s) + 1;
  char *out = xmalloc(n);
  memcpy(out, s, n);
  return out;
}
typedef struct id_entry { double slot, generation; qa_actor_id actor; qa_client_id client; } id_entry;
typedef struct resource_entry {
  char *id; qa_resolved_resource_reference reference;
} resource_entry;
typedef struct pending_events {
  double frame;
  qa_save_value **events; size_t event_count;
  qa_save_value **simulation; size_t simulation_count;
} pending_events;
typedef struct sequence_entry { double source, local; } sequence_entry;
/* A released SimulationEvent: its view borrows the decoded tree and the storage beside it. */
typedef struct simulation_record {
  qa_save_value *tree; qa_simulation_event event; qa_app_unified_simulation_event_storage storage;
} simulation_record;
static void simulation_record_free(simulation_record *record) {
  if (record == NULL) return;
  qa_app_unified_simulation_event_storage_release(&record->storage);
  qa_save_value_free(record->tree);
  free(record);
}
static void simulation_records_free(simulation_record **records, size_t count) {
  for (size_t i = 0; i < count; i++) simulation_record_free(records[i]);
  free(records);
}
struct qa_app_unified_remote {
  qa_app_unified_remote_options options;
  qa_app_remote_world_content *world;
  id_entry *actors; size_t actor_count, actor_cap;
  id_entry *clients; size_t client_count, client_cap;
  resource_entry *resources; size_t resource_count, resource_cap;
  pending_events *pending; size_t pending_head, pending_count, pending_cap;
  sequence_entry *source_sequences; size_t sequence_count, sequence_cap;
  qa_app_presentation_state *media; /* TS mediaValue */
  simulation_record **simulation_events; size_t simulation_count, simulation_cap;
  /* The last drainSimulationEvents() result, valid until the next drain. */
  simulation_record **drained; size_t drained_count; qa_simulation_event *drained_events;
  qa_app_unified_component_consumers *components;
  qa_app_unified_native_consumers *native;
  bool has_current; qa_app_unified_presentation_frame_t current;
  bool has_binding; qa_app_unified_actor_reference binding_client, binding_actor;
  double binding_source_entity;
  qa_app_selected_movement_prediction *predictor;
  bool has_predicted; qa_app_movement_prediction_result predicted;
  qa_actor_id *linked; size_t linked_count, linked_cap;
  double generation;
  bool closed;
  double world_epoch, content_epoch;
  char *offered_digest;
  double last_event_sequence, last_simulation_sequence;
  double consumer_generation;
  qa_body_snapshot *sample_bodies;
  qa_scene_entity *sample_entities;
  qa_actor_id sample_ground;
  qa_app_simulation_presentation *shifted_models;
  qa_q3_character_view *shifted_characters;
  qa_active_mod_client_presentation *client_sources; size_t client_source_cap;
  /* Component updates the consumers retain pointers into: an owned re-read of each control's
   * update (its tree and arrays), released when the epoch's consumers are. */
  struct retained_update { qa_save_value *tree; qa_app_unified_component_update update; } *retained;
  size_t retained_count, retained_cap;
};
static void retained_release(qa_app_unified_remote *remote) {
  for (size_t i = 0; i < remote->retained_count; i++) {
    qa_app_unified_component_update_free(&remote->retained[i].update);
    qa_save_value_free(remote->retained[i].tree);
  }
  remote->retained_count = 0;
}
static void assert_current(qa_app_unified_remote *remote, double generation) {
  if (remote->closed || generation != remote->generation)
    qa_raise("Error", 0, 0, "Unified replica load was retired");
}
typedef struct assert_ctx { qa_app_unified_remote *remote; double generation; } assert_ctx;
static void assert_thunk(void *ctx) {
  assert_ctx *a = ctx;
  assert_current(a->remote, a->generation);
}
/* new PresentationState(() => this.current?.output.snapshot.frame.time ?? { kind: "milliseconds", value: 0 },
 *   actor => this.numberOf(actor)). */
static qa_source_time media_now(void *ctx) {
  qa_app_unified_remote *remote = ctx;
  if (remote->has_current) return remote->current.output.snapshot.frame.time;
  return (qa_source_time){QA_SOURCE_TIME_MILLISECONDS, 0};
}
static bool media_slot(void *ctx, qa_actor_id actor, double *out) {
  return qa_app_unified_remote_number_of(ctx, actor, out);
}
static void media_replace(qa_app_unified_remote *remote) {
  qa_app_presentation_state *media = qa_app_presentation_state_create(media_now, media_slot, remote, NULL);
  if (media == NULL) qa_range_error("out of memory");
  qa_app_presentation_state_destroy(remote->media);
  remote->media = media;
}
#define MAX_IDENTITY_SLOTS 65536
#define PENDING_CAPACITY 4096
#define SIMULATION_CAPACITY 16384
/* One entry per slot: a new generation replaces the old, so identity memory is bounded by the
 * slot range. */
static qa_actor_id identity_actor(void *ctx, double slot, double generation) {
  qa_app_unified_remote *remote = ctx;
  if (slot >= MAX_IDENTITY_SLOTS) qa_raise("RangeError", 0, 0, "Unified actor slot is out of range");
  id_entry *old = NULL;
  for (size_t i = 0; i < remote->actor_count; i++)
    if (remote->actors[i].slot == slot) old = &remote->actors[i];
  if (old && old->generation == generation) return old->actor;
  double local = remote->options.next_generation(remote->options.ctx, slot);
  qa_actor_id value = remote->options.identity_actor(remote->options.ctx, slot, local);
  if (old) {
    old->generation = generation;
    old->actor = value;
    return value;
  }
  if (remote->actor_count == remote->actor_cap) {
    remote->actor_cap = remote->actor_cap ? remote->actor_cap * 2 : 16;
    remote->actors = realloc(remote->actors, remote->actor_cap * sizeof *remote->actors);
    if (!remote->actors) qa_range_error("out of memory");
  }
  remote->actors[remote->actor_count].slot = slot;
  remote->actors[remote->actor_count].generation = generation;
  remote->actors[remote->actor_count].actor = value;
  remote->actor_count++;
  return value;
}
static qa_client_id identity_client(void *ctx, double slot, double generation) {
  qa_app_unified_remote *remote = ctx;
  if (remote->has_binding && remote->binding_client.slot == slot
      && remote->binding_client.generation == generation)
    return remote->options.client;
  if (slot >= MAX_IDENTITY_SLOTS) qa_raise("RangeError", 0, 0, "Unified client slot is out of range");
  id_entry *old = NULL;
  for (size_t i = 0; i < remote->client_count; i++)
    if (remote->clients[i].slot == slot) old = &remote->clients[i];
  if (old && old->generation == generation) return old->client;
  /* Remote references are not admitted session clients; their generation is epoch-scoped. */
  double local = remote->options.next_generation(remote->options.ctx, slot);
  qa_client_id value = remote->options.identity_client(remote->options.ctx, slot, local);
  if (old) {
    old->generation = generation;
    old->client = value;
    return value;
  }
  if (remote->client_count == remote->client_cap) {
    remote->client_cap = remote->client_cap ? remote->client_cap * 2 : 16;
    remote->clients = realloc(remote->clients, remote->client_cap * sizeof *remote->clients);
    if (!remote->clients) qa_range_error("out of memory");
  }
  remote->clients[remote->client_count].slot = slot;
  remote->clients[remote->client_count].generation = generation;
  remote->clients[remote->client_count].client = value;
  remote->client_count++;
  return value;
}
static qa_seat_id identity_seat(void *ctx, double index) {
  (void)index;
  qa_app_unified_remote *remote = ctx;
  return remote->options.seat;
}
static qa_resource_id identity_resource(void *ctx, qa_resource_id id) {
  qa_app_unified_remote *remote = ctx;
  for (size_t i = 0; i < remote->resource_count; i++)
    if (strcmp(remote->resources[i].id, id) == 0) return remote->resources[i].reference.id;
  qa_raise("Error", 0, 0, "Unified resource was not declared: %s", id);
}
static qa_app_unified_identity_decoder identity_of(qa_app_unified_remote *remote) {
  qa_app_unified_identity_decoder decoder = {0};
  decoder.ctx = remote;
  decoder.session = remote->options.session;
  decoder.actor = identity_actor;
  decoder.client = identity_client;
  decoder.seat = identity_seat;
  decoder.resource_id = identity_resource;
  return decoder;
}
static void resource_set(qa_app_unified_remote *remote, char *id,
    qa_resolved_resource_reference reference) {
  for (size_t i = 0; i < remote->resource_count; i++)
    if (strcmp(remote->resources[i].id, id) == 0) {
      remote->resources[i].reference = reference;
      free(id);
      return;
    }
  if (remote->resource_count == remote->resource_cap) {
    remote->resource_cap = remote->resource_cap ? remote->resource_cap * 2 : 16;
    remote->resources = realloc(remote->resources, remote->resource_cap * sizeof *remote->resources);
    if (!remote->resources) qa_range_error("out of memory");
  }
  remote->resources[remote->resource_count].id = id;
  remote->resources[remote->resource_count].reference = reference;
  remote->resource_count++;
}
static const qa_resolved_resource_reference *resource_get(qa_app_unified_remote *remote,
    const char *id) {
  for (size_t i = 0; i < remote->resource_count; i++)
    if (strcmp(remote->resources[i].id, id) == 0) return &remote->resources[i].reference;
  return NULL;
}
static void predicted_clear_result(qa_app_unified_remote *remote) {
  if (!remote->has_predicted) return;
  qa_app_movement_prediction_snapshot_destroy(&remote->predicted.player);
  free(remote->predicted.effects);
  remote->has_predicted = false;
}
static void clear_prediction(qa_app_unified_remote *remote) {
  if (remote->has_current && remote->options.scene_unlink) {
    void *scene = qa_app_remote_world_scene(remote->world);
    for (size_t i = 0; i < remote->linked_count; i++)
      remote->options.scene_unlink(remote->options.ctx, scene, remote->linked[i]);
  }
  remote->linked_count = 0;
  if (remote->predictor) {
    qa_app_selected_movement_prediction_destroy(remote->predictor);
    remote->predictor = NULL;
  }
  predicted_clear_result(remote);
}
static bool viewer_of(qa_app_unified_remote *remote, qa_actor_id *out) {
  if (!remote->has_current || !remote->has_binding) return false;
  *out = remote->current.player_actor;
  return true;
}
static bool viewer_thunk(void *ctx, qa_actor_id *out) { return viewer_of(ctx, out); }
static void consumer_assert_current(void *ctx) {
  qa_app_unified_remote *remote = ctx;
  assert_current(remote, remote->consumer_generation);
}
/* TS events: this.mediaValue (the consumers' admitReplicatedOwner / retireReplicatedOwner). */
static void consumer_admit_owner(void *ctx, qa_presentation_owner owner, qa_content_id content) {
  qa_app_unified_remote *remote = ctx;
  qa_app_presentation_state_admit_replicated_owner(remote->media, owner, content);
}
static void consumer_retire_owner(void *ctx, qa_presentation_owner owner) {
  qa_app_unified_remote *remote = ctx;
  qa_app_presentation_state_retire_replicated_owner(remote->media, owner);
}
static const void *consumer_installed_mounts(void *ctx, qa_content_id content) {
  qa_app_unified_remote *remote = ctx;
  return remote->options.installed_mounts
      ? remote->options.installed_mounts(remote->options.ctx, content) : NULL;
}
static const void *consumer_borrow_mounts(void *ctx, const char *selection, qa_content_id content,
    const void *installed, const void **out_writable) {
  qa_app_unified_remote *remote = ctx;
  return remote->options.borrow_mounts
      ? remote->options.borrow_mounts(remote->options.ctx, selection, content, installed, out_writable)
      : NULL;
}
static void consumer_close_mounts(void *ctx, const void *mounts) {
  qa_app_unified_remote *remote = ctx;
  if (remote->options.close_mounts) remote->options.close_mounts(remote->options.ctx, mounts);
}
static void consumer_product_expectation(void *ctx, qa_content_id content,
    const char **family, const char **edition) {
  qa_app_unified_remote *remote = ctx;
  if (remote->options.product_expectation)
    remote->options.product_expectation(remote->options.ctx, content, family, edition);
}
static void consumer_command(void *ctx, qa_presentation_owner owner, double generation,
    const char *const *args, size_t count) {
  qa_app_unified_remote *remote = ctx;
  if (!remote->options.send_component_command)
    qa_raise("Error", 0, 0, "Remote component command channel is unavailable");
  remote->options.send_component_command(remote->options.ctx, owner, generation, args, count);
}
static void retire_op(void *ctx) {
  qa_app_unified_remote *remote = ctx;
  if (remote->closed) qa_raise("Error", 0, 0, "Unified replica is closed");
  if (remote->components) {
    qa_app_unified_component_consumers_close(remote->components);
    qa_app_unified_component_consumers_destroy(remote->components);
  }
  remote->components = NULL;
  if (remote->native) {
    qa_app_unified_native_consumers_close(remote->native);
    qa_app_unified_native_consumers_destroy(remote->native);
  }
  remote->native = NULL;
  remote->generation++;
}
typedef struct offer_ctx {
  qa_app_unified_remote *remote; const qa_app_unified_control *offer;
} offer_ctx;
static void offer_retire(void *ctx) {
  offer_ctx *offer = ctx;
  retire_op(offer->remote);
  offer->remote->world_epoch = offer->offer->epoch;
  free(offer->remote->offered_digest);
  offer->remote->offered_digest = offer->offer->composition
      ? copy_text(offer->offer->composition->digest) : NULL;
  offer->remote->has_binding = false;
}
static void trees_free(qa_save_value **trees, size_t count) {
  for (size_t i = 0; i < count; i++) qa_save_value_free(trees[i]);
  free(trees);
}
static void pending_entry_free(void *arg) {
  pending_events *entry = arg;
  trees_free(entry->events, entry->event_count);
  trees_free(entry->simulation, entry->simulation_count);
  entry->events = NULL; entry->event_count = 0;
  entry->simulation = NULL; entry->simulation_count = 0;
}
static void pending_reset(qa_app_unified_remote *remote) {
  for (size_t i = 0; i < remote->pending_count; i++) {
    pending_events *entry = &remote->pending[(remote->pending_head + i) % remote->pending_cap];
    trees_free(entry->events, entry->event_count);
    trees_free(entry->simulation, entry->simulation_count);
  }
  remote->pending_head = 0;
  remote->pending_count = 0;
}
qa_app_unified_remote *qa_app_unified_remote_create(qa_app_unified_remote_options options) {
  qa_app_unified_remote *remote = calloc(1, sizeof *remote);
  if (!remote) return NULL;
  remote->options = options;
  remote->world = qa_app_remote_world_content_create(options.world_host, NULL);
  if (!remote->world) { free(remote); return NULL; }
  remote->last_event_sequence = -1;
  remote->last_simulation_sequence = -1;
  remote->media = qa_app_presentation_state_create(media_now, media_slot, remote, NULL);
  if (!remote->media) { qa_app_remote_world_content_destroy(remote->world); free(remote); return NULL; }
  return remote;
}
void qa_app_unified_remote_destroy(qa_app_unified_remote *remote) {
  if (!remote) return;
  qa_app_unified_remote_close(remote);
  if (remote->components) qa_app_unified_component_consumers_destroy(remote->components);
  if (remote->native) qa_app_unified_native_consumers_destroy(remote->native);
  if (remote->has_current) qa_app_unified_presentation_frame_free(&remote->current);
  retained_release(remote);
  qa_app_presentation_state_destroy(remote->media);
  remote->media = NULL;
  simulation_records_free(remote->drained, remote->drained_count);
  free(remote->drained_events);
  free(remote->retained);
  qa_app_remote_world_content_destroy(remote->world);
  free(remote->actors);
  free(remote->clients);
  for (size_t i = 0; i < remote->resource_count; i++) free(remote->resources[i].id);
  free(remote->resources);
  free(remote->pending);
  free(remote->source_sequences);
  free(remote->simulation_events);
  free(remote->linked);
  free(remote->offered_digest);
  free(remote->sample_bodies);
  free(remote->sample_entities);
  free(remote->shifted_models);
  free(remote->shifted_characters);
  free(remote->client_sources);
  free(remote);
}
qa_client_id qa_app_unified_remote_client(const qa_app_unified_remote *remote) {
  return remote->options.client;
}
const char *qa_app_unified_remote_composition_digest(const qa_app_unified_remote *remote) {
  return remote->offered_digest;
}
double qa_app_unified_remote_epoch(const qa_app_unified_remote *remote) {
  return remote->world_epoch;
}
double qa_app_unified_remote_loaded_epoch(const qa_app_unified_remote *remote) {
  return remote->content_epoch;
}
void *qa_app_unified_remote_scene(qa_app_unified_remote *remote) {
  return qa_app_remote_world_scene(remote->world);
}
const qa_simulation_output *qa_app_unified_remote_output(const qa_app_unified_remote *remote) {
  return remote->has_current ? &remote->current.output : NULL;
}
bool qa_app_unified_remote_player(const qa_app_unified_remote *remote,
    qa_actor_id *actor, qa_client_id *client, double *source_entity) {
  if (!remote->has_current || !remote->has_binding) return false;
  if (actor) *actor = remote->current.player_actor;
  if (client) *client = remote->options.client;
  if (source_entity) *source_entity = remote->binding_source_entity;
  return true;
}
void qa_app_unified_remote_offer(qa_app_unified_remote *remote,
    const qa_app_unified_control *offer) {
  if (remote->closed) qa_raise("Error", 0, 0, "Unified replica is closed");
  offer_ctx context = {remote, offer};
  if (!remote->options.publish_components) offer_retire(&context);
  else remote->options.publish_components(remote->options.ctx, offer_retire, &context);
  double generation = remote->generation;
  assert_ctx current = {remote, generation};
  void *content = remote->options.load_content(remote->options.ctx, offer, assert_thunk, &current);
  assert_current(remote, generation);
  clear_prediction(remote);
  qa_app_remote_world_set_loaded(remote->world, content);
  remote->content_epoch = offer->epoch;
  if (remote->has_current) {
    qa_app_unified_presentation_frame_free(&remote->current);
    remote->has_current = false;
  }
  remote->actor_count = 0;
  remote->client_count = 0;
  for (size_t i = 0; i < remote->resource_count; i++) free(remote->resources[i].id);
  remote->resource_count = 0;
  remote->sequence_count = 0;
  retained_release(remote);
  media_replace(remote);
  simulation_records_free(remote->simulation_events, remote->simulation_count);
  remote->simulation_events = NULL;
  remote->simulation_count = 0;
  remote->simulation_cap = 0;
  pending_reset(remote);
  remote->last_event_sequence = -1;
  remote->last_simulation_sequence = -1;
  size_t mod_count = 0;
  const qa_prepared_mod *mods = remote->options.prepared_mods
      ? remote->options.prepared_mods(remote->options.ctx, content, &mod_count) : NULL;
  qa_app_unified_native_consumer_options native_options = {0};
  native_options.ctx = remote;
  native_options.prepared_mods = mods;
  native_options.prepared_mod_count = mod_count;
  remote->consumer_generation = generation;
  native_options.assert_current = consumer_assert_current;
  native_options.viewer = viewer_thunk;
  native_options.admit_replicated_owner = consumer_admit_owner;
  native_options.retire_replicated_owner = consumer_retire_owner;
  native_options.product_expectation = consumer_product_expectation;
  remote->native = qa_app_unified_native_consumers_create(native_options);
  if (!remote->native) qa_range_error("out of memory");
  if (remote->options.mod_has_presentation
      && remote->options.mod_has_presentation(remote->options.ctx, content)) {
    if (!remote->options.has_mod_files)
      qa_raise("Error", 0, 0, "Remote component presentation requires client-owned writable storage");
    qa_app_unified_component_consumer_options component_options = {0};
    component_options.ctx = remote;
    component_options.prepared_mods = mods;
    component_options.prepared_mod_count = mod_count;
    component_options.assert_current = consumer_assert_current;
    component_options.viewer = viewer_thunk;
    component_options.command = consumer_command;
    component_options.admit_replicated_owner = consumer_admit_owner;
    component_options.retire_replicated_owner = consumer_retire_owner;
    component_options.installed_mounts = consumer_installed_mounts;
    component_options.borrow_mounts = consumer_borrow_mounts;
    component_options.close_mounts = consumer_close_mounts;
    remote->components = qa_app_unified_component_consumers_create(component_options);
    if (!remote->components) qa_range_error("out of memory");
  }
}
void qa_app_unified_remote_admitted(qa_app_unified_remote *remote,
    const qa_app_unified_control *binding) {
  if (binding->epoch != remote->world_epoch) return;
  remote->has_binding = true;
  remote->binding_client = binding->client;
  remote->binding_actor = binding->actor;
  remote->binding_source_entity = binding->source_entity;
  identity_actor(remote, binding->actor.slot, binding->actor.generation);
}
typedef struct components_op {
  qa_app_unified_remote *remote; double epoch; const qa_app_unified_component_update *update;
} components_op;
static void cleanup_commit(void *arg) {
  qa_app_unified_native_commit **commit = arg;
  if (*commit) qa_app_unified_native_commit_free(*commit);
}
static void components_publish(void *ctx) {
  components_op *op = ctx;
  qa_app_unified_remote *remote = op->remote;
  if (op->epoch != remote->world_epoch || remote->closed) return;
  qa_app_unified_native_commit *commit = remote->native
      ? qa_app_unified_native_consumers_prepare_update(remote->native, op->update) : NULL;
  qa_cleanup commit_guard;
  qa_cleanup_push(&commit_guard, cleanup_commit, &commit);
  if (!remote->components) {
    if (op->update->source_count != 0)
      qa_raise("Error", 0, 0, "Server activated an unqualified remote component");
  } else qa_app_unified_component_consumers_update(remote->components, op->update);
  qa_cleanup_pop(&commit_guard);
  if (commit) {
    qa_app_unified_native_commit_run(commit);
    qa_app_unified_native_commit_free(commit);
  }
}
static void cleanup_value(void *arg) { qa_save_value_free(arg); }
void qa_app_unified_remote_receive_components(qa_app_unified_remote *remote,
    double epoch, const qa_app_unified_component_update *update) {
  /* The decoded control (and the strings its update borrows) is freed after this call, while the
   * consumers keep identities and configstrings: they get an owned copy. */
  if (remote->retained_count == remote->retained_cap) {
    size_t cap = remote->retained_cap ? remote->retained_cap * 2 : 4;
    struct retained_update *grown = realloc(remote->retained, cap * sizeof *grown);
    if (!grown) qa_range_error("out of memory");
    remote->retained = grown;
    remote->retained_cap = cap;
  }
  qa_save_value *tree = qa_app_unified_write_component_update(update);
  if (!tree) qa_range_error("out of memory");
  qa_cleanup tree_guard;
  qa_cleanup_push(&tree_guard, cleanup_value, tree);
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, tree, "unified-components")) qa_range_error("out of memory");
  qa_app_unified_component_update owned = qa_app_unified_read_component_update(&reader);
  qa_cleanup_pop(&tree_guard);
  struct retained_update *slot = &remote->retained[remote->retained_count++];
  slot->tree = tree;
  slot->update = owned;
  components_op op = {remote, epoch, &slot->update};
  if (!remote->options.publish_components) components_publish(&op);
  else remote->options.publish_components(remote->options.ctx, components_publish, &op);
}
const struct qa_active_mod_presentation *qa_app_unified_remote_mod_presentation_sources(qa_app_unified_remote *remote,
    size_t *out_count) {
  if (!remote->components) { *out_count = 0; return NULL; }
  return qa_app_unified_component_consumers_sources(remote->components, out_count);
}
const qa_active_mod_client_presentation *qa_app_unified_remote_mod_client_sources(
    qa_app_unified_remote *remote, size_t *out_count) {
  size_t first_count = 0, second_count = 0;
  const qa_active_mod_client_presentation *first = remote->components
      ? qa_app_unified_component_consumers_client_sources(remote->components, &first_count) : NULL;
  const qa_active_mod_client_presentation *second = remote->native
      ? qa_app_unified_native_consumers_sources(remote->native, &second_count) : NULL;
  if (remote->client_source_cap < first_count + second_count) {
    remote->client_sources = realloc(remote->client_sources,
        (first_count + second_count) * sizeof *remote->client_sources);
    if (!remote->client_sources) qa_range_error("out of memory");
    remote->client_source_cap = first_count + second_count;
  }
  if (first_count) memcpy(remote->client_sources, first, first_count * sizeof *first);
  if (second_count) memcpy(remote->client_sources + first_count, second,
      second_count * sizeof *second);
  *out_count = first_count + second_count;
  return remote->client_sources;
}
void qa_app_unified_remote_declare(qa_app_unified_remote *remote, double epoch,
    const qa_app_unified_resource_key *keys, size_t count) {
  if (epoch != remote->world_epoch || remote->closed) return;
  double generation = remote->generation;
  void *loaded = qa_app_remote_world_loaded(remote->world);
  for (size_t i = 0; i < count; i++) {
    qa_resolved_resource_reference resource =
        remote->options.resolve_resource(remote->options.ctx, loaded, &keys[i]);
    assert_current(remote, generation);
    char *id = qa_app_unified_resource_id_from_key(&keys[i]);
    if (!id) qa_range_error("out of memory");
    resource_set(remote, id, resource);
  }
}
static double tree_number(const qa_save_value *tree, const char *key, double fallback) {
  const qa_save_value *value = qa_save_value_get(tree, key);
  return value != NULL && value->kind == QA_SAVE_NUMBER ? value->number : fallback;
}
static void sequence_set(qa_app_unified_remote *remote, double source, double local) {
  for (size_t i = 0; i < remote->sequence_count; i++)
    if (remote->source_sequences[i].source == source) {
      remote->source_sequences[i].local = local;
      return;
    }
  if (remote->sequence_count == remote->sequence_cap) {
    remote->sequence_cap = remote->sequence_cap ? remote->sequence_cap * 2 : 64;
    remote->source_sequences = realloc(remote->source_sequences,
        remote->sequence_cap * sizeof *remote->source_sequences);
    if (!remote->source_sequences) qa_range_error("out of memory");
  }
  remote->source_sequences[remote->sequence_count].source = source;
  remote->source_sequences[remote->sequence_count].local = local;
  remote->sequence_count++;
}
static bool sequence_get(qa_app_unified_remote *remote, double source, double *out) {
  for (size_t i = 0; i < remote->sequence_count; i++)
    if (remote->source_sequences[i].source == source) {
      *out = remote->source_sequences[i].local;
      return true;
    }
  return false;
}
/* One released presentation event: the decoded TS object as the typed event mediaValue.receivePresentation takes. */
typedef struct released_event { qa_app_sim_presentation_event typed; } released_event;
static void released_event_free(void *arg) { qa_app_sim_presentation_event_release(&((released_event *)arg)->typed); }
static void receive_presentation(qa_app_unified_remote *remote, const qa_save_value *tree) {
  released_event event;
  memset(&event, 0, sizeof event);
  qa_cleanup guard;
  qa_cleanup_push(&guard, released_event_free, &event);
  qa_app_unified_presentation_event_from_tree(tree, &event.typed);
  if (strcmp(event.typed.kind, "presentation-owner") == 0 && strcmp(event.typed.presentation_owner.kind, "retired") == 0
      && remote->native)
    qa_app_unified_native_consumers_retire(remote->native, event.typed.presentation_owner.owner);
  sequence_set(remote, event.typed.sequence, qa_app_presentation_state_receive_presentation(remote->media, &event.typed));
  remote->last_event_sequence = event.typed.sequence;
  qa_cleanup_pop(&guard);
  released_event_free(&event);
}
typedef struct release_batch { simulation_record **records; size_t count, cap; } release_batch;
static void release_batch_free(void *arg) {
  release_batch *batch = arg;
  simulation_records_free(batch->records, batch->count);
  memset(batch, 0, sizeof *batch);
}
static void release_events(qa_app_unified_remote *remote) {
  if (!remote->has_current) return;
  double frame = remote->current.output.snapshot.frame.frame;
  release_batch batch = {NULL, 0, 0};
  qa_cleanup batch_guard;
  qa_cleanup_push(&batch_guard, release_batch_free, &batch);
  while (remote->pending_count > 0) {
    pending_events *next = &remote->pending[remote->pending_head];
    if (next->frame > frame) break;
    /* TS pendingEvents.read(cursor): the entry leaves the ring before its events are handled. */
    pending_events entry = *next;
    remote->pending_head = (remote->pending_head + 1) % remote->pending_cap;
    remote->pending_count--;
    {
      qa_cleanup entry_guard;
      qa_cleanup_push(&entry_guard, pending_entry_free, &entry);
      for (size_t i = 0; i < entry.event_count; i++)
        if (tree_number(entry.events[i], "sequence", -1) > remote->last_event_sequence)
          receive_presentation(remote, entry.events[i]);
      for (size_t i = 0; i < entry.simulation_count; i++) {
        double sequence = tree_number(entry.simulation[i], "sequence", -1);
        if (!(sequence > remote->last_simulation_sequence)) continue;
        simulation_record *record = calloc(1, sizeof *record);
        if (!record) qa_range_error("out of memory");
        record->tree = entry.simulation[i];
        entry.simulation[i] = NULL;
        if (batch.count == batch.cap) {
          size_t cap = batch.cap ? batch.cap * 2 : 64;
          simulation_record **grown = realloc(batch.records, cap * sizeof *grown);
          if (!grown) { simulation_record_free(record); qa_range_error("out of memory"); }
          batch.records = grown;
          batch.cap = cap;
        }
        batch.records[batch.count++] = record;
        qa_app_unified_simulation_event_view(record->tree, &record->event, &record->storage);
        double local = 0;
        if (record->event.payload.kind == QA_SIM_EVENT_MESSAGE && record->event.payload.has_source_presentation_sequence
            && sequence_get(remote, record->event.payload.source_presentation_sequence, &local))
          record->event.payload.source_presentation_sequence = local;
        remote->last_simulation_sequence = sequence;
      }
      qa_cleanup_pop(&entry_guard);
      pending_entry_free(&entry);
    }
  }
  if (remote->sequence_count > 4096) {
    size_t kept = 0;
    for (size_t i = 0; i < remote->sequence_count; i++)
      if (remote->source_sequences[i].source >= remote->last_event_sequence - 2048)
        remote->source_sequences[kept++] = remote->source_sequences[i];
    remote->sequence_count = kept;
  }
  if (batch.count == 0) { qa_cleanup_pop(&batch_guard); release_batch_free(&batch); return; }
  /* TS simulationEvents.admitAll: all records or a capacity error, never a partial batch. */
  if (batch.count > SIMULATION_CAPACITY - remote->simulation_count)
    qa_raise("RangeError", 0, 0,
        "Event ring capacity exhausted (%d undrained records, batch of %zu); drain before admitting more",
        SIMULATION_CAPACITY, batch.count);
  if (remote->simulation_count + batch.count > remote->simulation_cap) {
    size_t cap = remote->simulation_count + batch.count;
    simulation_record **grown = realloc(remote->simulation_events, cap * sizeof *grown);
    if (!grown) qa_range_error("out of memory");
    remote->simulation_events = grown;
    remote->simulation_cap = cap;
  }
  memcpy(remote->simulation_events + remote->simulation_count, batch.records, batch.count * sizeof *batch.records);
  remote->simulation_count += batch.count;
  qa_cleanup_pop(&batch_guard);
  free(batch.records);
}
typedef struct events_scope {
  qa_save_value **events; size_t event_count;
  qa_save_value **simulation; size_t simulation_count;
  qa_save_value *decoded;
} events_scope;
static void events_scope_free(void *arg) {
  events_scope *scope = arg;
  trees_free(scope->events, scope->event_count);
  trees_free(scope->simulation, scope->simulation_count);
  qa_save_value_free(scope->decoded);
  memset(scope, 0, sizeof *scope);
}
void qa_app_unified_remote_receive_events(qa_app_unified_remote *remote, double epoch,
    double frame, const uint8_t *payload, size_t payload_length,
    const uint8_t *simulation, size_t simulation_length) {
  if (epoch != remote->world_epoch || remote->closed) return;
  qa_app_unified_identity_decoder identity = identity_of(remote);
  events_scope scope;
  memset(&scope, 0, sizeof scope);
  qa_cleanup guard;
  qa_cleanup_push(&guard, events_scope_free, &scope);
  scope.events = qa_app_unified_decode_presentation_events(payload, payload_length, &identity,
      &scope.event_count);
  scope.decoded = qa_decode_checkpoint_value(simulation, simulation_length);
  if (!scope.decoded) qa_range_error("out of memory");
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, scope.decoded, "unified-events")) qa_range_error("out of memory");
  size_t count = qa_save_reader_list_count(&reader);
  scope.simulation = calloc(count ? count : 1, sizeof *scope.simulation);
  if (!scope.simulation) qa_range_error("out of memory");
  for (size_t i = 0; i < count; i++) {
    qa_save_reader item;
    if (!qa_save_reader_item(&reader, i, &item)) qa_range_error("out of memory");
    scope.simulation[i] = qa_app_unified_read_simulation_event(item.value, &identity);
    if (!scope.simulation[i]) qa_range_error("out of memory");
    scope.simulation_count = i + 1;
  }
  /* TS EventRing(4096).admit: a full ring throws instead of growing. */
  if (remote->pending_count >= PENDING_CAPACITY)
    qa_raise("RangeError", 0, 0,
        "Event ring capacity exhausted (%d undrained records); drain before admitting more", PENDING_CAPACITY);
  if (remote->pending_count == remote->pending_cap) {
    size_t new_cap = remote->pending_cap ? remote->pending_cap * 2 : 16;
    pending_events *grown = xmalloc(new_cap * sizeof *grown);
    for (size_t i = 0; i < remote->pending_count; i++)
      grown[i] = remote->pending[(remote->pending_head + i) % remote->pending_cap];
    free(remote->pending);
    remote->pending = grown;
    remote->pending_cap = new_cap;
    remote->pending_head = 0;
  }
  qa_cleanup_pop(&guard);
  qa_save_value_free(scope.decoded);
  pending_events *entry = &remote->pending[
      (remote->pending_head + remote->pending_count) % remote->pending_cap];
  entry->frame = frame;
  entry->events = scope.events;
  entry->event_count = scope.event_count;
  entry->simulation = scope.simulation;
  entry->simulation_count = scope.simulation_count;
  remote->pending_count++;
  release_events(remote);
}
typedef struct decoder_ctx {
  qa_app_unified_remote *remote; void *loaded; double generation;
} decoder_ctx;
static qa_resolved_resource_reference decoder_resource(void *ctx,
    const qa_app_unified_resource_key *key) {
  decoder_ctx *decoder = ctx;
  qa_app_unified_remote *remote = decoder->remote;
  char *id = qa_app_unified_resource_id_from_key(key);
  if (!id) qa_range_error("out of memory");
  const qa_resolved_resource_reference *existing = resource_get(remote, id);
  if (existing) {
    qa_resolved_resource_reference result = *existing;
    free(id);
    return result;
  }
  qa_resolved_resource_reference value =
      remote->options.resolve_resource(remote->options.ctx, decoder->loaded, key);
  assert_current(remote, decoder->generation);
  resource_set(remote, id, value);
  return value;
}
static qa_decoded_model decoder_model(void *ctx, const qa_app_unified_resource_key *key,
    bool has_brush_model, double brush_model) {
  decoder_ctx *decoder = ctx;
  return decoder->remote->options.model(decoder->remote->options.ctx, key, has_brush_model, brush_model);
}
static bool is_brush_hit(void *ctx, qa_trace_hit hit) {
  qa_app_unified_remote *remote = ctx;
  if (hit.kind == QA_TRACE_HIT_WORLD) return true;
  if (hit.kind == QA_TRACE_HIT_ACTOR && remote->options.scene_actor_is_model)
    return remote->options.scene_actor_is_model(remote->options.ctx,
        qa_app_remote_world_scene(remote->world), hit.actor);
  return false;
}
static void correct_prediction(qa_app_unified_remote *remote,
    qa_app_unified_presentation_frame_t *frame) {
  qa_app_unified_prediction_projection *projection = &frame->prediction;
  if (!qa_same_actor(projection->actor, frame->player_actor)
      || projection->snapshot.sequence != frame->acknowledged_input)
    qa_raise("Error", 0, 0, "Unified prediction does not match admitted snapshot");
  if (remote->has_current && remote->predictor
      && !qa_same_actor(remote->current.prediction.actor, projection->actor))
    clear_prediction(remote);
  void *scene = remote->options.scene_link ? qa_app_remote_world_scene(remote->world) : NULL;
  size_t kept = 0;
  for (size_t i = 0; i < remote->linked_count; i++) {
    bool present = false;
    for (size_t j = 0; j < projection->collision_count; j++)
      if (qa_same_actor(projection->collisions[j].body.actor, remote->linked[i])) present = true;
    if (!present && remote->options.scene_unlink)
      remote->options.scene_unlink(remote->options.ctx, scene, remote->linked[i]);
    if (present) remote->linked[kept++] = remote->linked[i];
  }
  remote->linked_count = kept;
  for (size_t i = 0; i < projection->collision_count; i++) {
    if (remote->options.scene_link)
      remote->options.scene_link(remote->options.ctx, scene,
          projection->collisions[i].body, projection->collisions[i].collision);
    bool present = false;
    for (size_t j = 0; j < remote->linked_count; j++)
      if (qa_same_actor(remote->linked[j], projection->collisions[i].body.actor)) present = true;
    if (!present) {
      if (remote->linked_count == remote->linked_cap) {
        remote->linked_cap = remote->linked_cap ? remote->linked_cap * 2 : 16;
        remote->linked = realloc(remote->linked, remote->linked_cap * sizeof *remote->linked);
        if (!remote->linked) qa_range_error("out of memory");
      }
      remote->linked[remote->linked_count++] = projection->collisions[i].body.actor;
    }
  }
  qa_app_movement_prediction_snapshot initial = projection->snapshot;
  initial.has_q3_arsenal = false;
  memset(&initial.q3_arsenal, 0, sizeof initial.q3_arsenal);
  if (!remote->predictor) {
    void *loaded = qa_app_remote_world_loaded(remote->world);
    qa_app_movement_prediction_options options = {0};
    options.probe.movement_only = true;
    options.probe.actor = remote->options.owned_actor(remote->options.ctx,
        projection->actor, remote->options.map_entities_provider
            ? remote->options.map_entities_provider(remote->options.ctx, loaded) : NULL);
    options.seat = remote->options.seat;
    options.probe.recipe = remote->options.recipe
        ? remote->options.recipe(remote->options.ctx, loaded) : NULL;
    options.probe.profile = projection->profile;
    options.probe.standing_bounds = projection->standing_bounds;
    options.probe.standing_view_height = projection->standing_view_height;
    /* scene: this.scene (the replica world's queries) */
    options.probe.scene = qa_shared_as_scene_queries((qa_shared_scene_queries *)qa_app_remote_world_scene(remote->world));
    options.probe.host_ctx = remote;
    options.probe.is_brush = is_brush_hit;
    remote->predictor = qa_app_selected_movement_prediction_create(options, initial);
    if (!remote->predictor) qa_range_error("out of memory");
  } else {
    /* TS getters read the live projection; refresh the stored probe
     * fields through receive (options are create-time in C). */
    qa_app_selected_movement_prediction_receive(remote->predictor, initial);
  }
  predicted_clear_result(remote);
  remote->predicted = qa_app_selected_movement_prediction_replay(remote->predictor, NULL, NULL);
  remote->has_predicted = true;
}
static void cleanup_envelope(void *arg) { qa_app_unified_frame_envelope_free(arg); }
static void cleanup_frame(void *arg) { qa_app_unified_presentation_frame_free(arg); }
bool qa_app_unified_remote_receive_frame(qa_app_unified_remote *remote,
    const uint8_t *bytes, size_t length, double *out_acknowledged) {
  qa_app_unified_frame_envelope envelope = qa_app_unified_read_frame(bytes, length);
  if (remote->closed || envelope.epoch != remote->world_epoch || !remote->has_binding) {
    qa_app_unified_frame_envelope_free(&envelope);
    return false;
  }
  qa_cleanup envelope_guard;
  qa_cleanup_push(&envelope_guard, cleanup_envelope, &envelope);
  void *loaded = qa_app_remote_world_loaded(remote->world);
  decoder_ctx context = {remote, loaded, remote->generation};
  qa_app_unified_frame_decoder decoder;
  memset(&decoder, 0, sizeof decoder);
  decoder.ctx = &context;
  decoder.identity = identity_of(remote);
  decoder.world = remote->options.world_geometry(remote->options.ctx, loaded);
  decoder.resource = decoder_resource;
  decoder.model = decoder_model;
  qa_cleanup_pop(&envelope_guard);
  qa_app_unified_presentation_frame_t frame = qa_app_unified_decode_frame_envelope(&envelope, &decoder);
  qa_cleanup frame_guard;
  qa_cleanup_push(&frame_guard, cleanup_frame, &frame);
  assert_current(remote, context.generation);
  qa_actor_id expected = identity_actor(remote,
      remote->binding_actor.slot, remote->binding_actor.generation);
  if (!qa_same_actor(frame.player_actor, expected))
    qa_raise("Error", 0, 0, "Unified frame changed the admitted player");
  *out_acknowledged = frame.acknowledged_input;
  if (remote->has_current
      && frame.output.snapshot.frame.frame <= remote->current.output.snapshot.frame.frame) {
    qa_cleanup_pop(&frame_guard);
    qa_app_unified_presentation_frame_free(&frame);
    return true;
  }
  qa_app_unified_component_frames empty;
  memset(&empty, 0, sizeof empty);
  const qa_app_unified_component_frames *frames = frame.has_components ? &frame.components : &empty;
  qa_app_unified_native_camera camera;
  memset(&camera, 0, sizeof camera);
  if (frame.has_native_camera) {
    camera.owner = &frame.native_camera.owner;
    camera.identity = &frame.native_camera.identity;
    camera.generation = frame.native_camera.generation;
    camera.view = &frame.native_camera.view;
  }
  qa_app_unified_native_commit *commit = NULL;
  qa_cleanup commit_guard;
  qa_cleanup_push(&commit_guard, cleanup_commit, &commit);
  bool rejected = false;
  if (remote->native) {
    commit = qa_app_unified_native_consumers_prepare(remote->native, frames, expected,
        frame.has_native_camera ? &camera : NULL);
    rejected = commit == NULL;
  }
  if (!rejected && remote->components
      && !qa_app_unified_component_consumers_accept(remote->components, frames, expected))
    rejected = true;
  if (!rejected && !remote->components && frames->source_count != 0)
    qa_raise("Error", 0, 0, "Remote frame contains unadmitted components");
  qa_cleanup_pop(&commit_guard);
  qa_cleanup_pop(&frame_guard);
  if (rejected) {
    if (commit) qa_app_unified_native_commit_free(commit);
    qa_app_unified_presentation_frame_free(&frame);
    return true;
  }
  if (remote->has_current) qa_app_unified_presentation_frame_free(&remote->current);
  remote->current = frame;
  remote->has_current = true;
  /* TS current = {...frame, output:{...frame.output, events:[]}}: simulation events arrive on the
   * events channel only. */
  for (size_t i = 0; i < remote->current.event_count; i++) qa_save_value_free(remote->current.events[i]);
  remote->current.event_count = 0;
  remote->current.output.events = NULL;
  remote->current.output.event_count = 0;
  if (commit) {
    qa_app_unified_native_commit_run(commit);
    qa_app_unified_native_commit_free(commit);
  }
  correct_prediction(remote, &remote->current);
  release_events(remote);
  remote->options.publish(remote->options.ctx, &remote->current.output);
  return true;
}
bool qa_app_unified_remote_command_time(const qa_app_unified_remote *remote, double *out) {
  if (!remote->has_current) return false;
  *out = remote->current.prediction.snapshot.command_time_milliseconds;
  return true;
}
void qa_app_unified_remote_predict(qa_app_unified_remote *remote,
    qa_app_prediction_command command) {
  if (!remote->predictor) return;
  command.angle_space = "absolute";
  qa_app_selected_movement_prediction_submit(remote->predictor, command);
  predicted_clear_result(remote);
  remote->predicted = qa_app_selected_movement_prediction_replay(remote->predictor, NULL, NULL);
  remote->has_predicted = true;
}
static const qa_app_movement_prediction_snapshot *predicted_player(qa_app_unified_remote *remote) {
  if (!remote->has_predicted) return NULL;
  const char *status = remote->predicted.status;
  if (status && (strcmp(status, "history-exhausted") == 0 || strcmp(status, "unchanged") == 0))
    return NULL;
  return &remote->predicted.player;
}
static qa_vec3 shift_origin(qa_app_unified_remote *remote, qa_vec3 origin) {
  const qa_app_movement_prediction_snapshot *player = predicted_player(remote);
  if (!player || !remote->has_current) return origin;
  qa_vec3 predicted = qa_app_sim_movement_origin(player->state);
  qa_vec3 original = qa_app_sim_movement_origin(remote->current.prediction.snapshot.state);
  qa_vec3 result = {origin.x + predicted.x - original.x,
      origin.y + predicted.y - original.y, origin.z + predicted.z - original.z};
  return result;
}
const qa_simulation_output *qa_app_unified_remote_sample_presentation(
    qa_app_unified_remote *remote, double now, qa_simulation_output *scratch) {
  (void)now;
  const qa_simulation_output *output = qa_app_unified_remote_output(remote);
  const qa_app_movement_prediction_snapshot *player = predicted_player(remote);
  if (!output || !player || !remote->has_current) return output;
  qa_actor_id actor = remote->current.player_actor;
  *scratch = *output;
  free(remote->sample_bodies);
  remote->sample_bodies = xmalloc(output->snapshot.body_count * sizeof *remote->sample_bodies);
  for (size_t i = 0; i < output->snapshot.body_count; i++) {
    remote->sample_bodies[i] = output->snapshot.bodies[i];
    if (qa_same_actor(output->snapshot.bodies[i].actor, actor)) {
      remote->sample_bodies[i].body.origin = qa_app_sim_movement_origin(player->state);
      remote->sample_bodies[i].body.velocity = qa_app_sim_movement_velocity(player->state);
      remote->sample_bodies[i].body.bounds = player->bounds;
      if (player->has_contact && player->contact_ground.kind == QA_TRACE_HIT_ACTOR) {
        remote->sample_ground = player->contact_ground.actor;
        remote->sample_bodies[i].body.ground = &remote->sample_ground;
      } else remote->sample_bodies[i].body.ground = NULL;
    }
  }
  scratch->snapshot.bodies = remote->sample_bodies;
  free(remote->sample_entities);
  remote->sample_entities = xmalloc(output->snapshot.scene.entity_count * sizeof *remote->sample_entities);
  for (size_t i = 0; i < output->snapshot.scene.entity_count; i++) {
    remote->sample_entities[i] = output->snapshot.scene.entities[i];
    const qa_scene_entity *entity = &output->snapshot.scene.entities[i];
    if (entity->actor && qa_same_actor(*entity->actor, actor)) {
      remote->sample_entities[i].transform.origin = shift_origin(remote, entity->transform.origin);
      remote->sample_entities[i].previous_origin = shift_origin(remote, entity->previous_origin);
      remote->sample_entities[i].lighting_origin = shift_origin(remote, entity->lighting_origin);
    }
  }
  scratch->snapshot.scene.entities = remote->sample_entities;
  return scratch;
}
const qa_app_sim_player_ui *qa_app_unified_remote_player_ui(qa_app_unified_remote *remote,
    qa_actor_id actor) {
  if (!remote->has_current || !qa_same_actor(remote->current.player_actor, actor))
    qa_raise("Error", 0, 0, "Unified player UI belongs to another actor");
  return &remote->current.player_ui;
}
qa_app_sim_player_view qa_app_unified_remote_player_view(qa_app_unified_remote *remote,
    qa_actor_id actor) {
  if (!remote->has_current || !qa_same_actor(remote->current.player_actor, actor))
    qa_raise("Error", 0, 0, "Unified player view belongs to another actor");
  qa_app_sim_player_view view = remote->current.player_view;
  const qa_app_movement_prediction_snapshot *player = predicted_player(remote);
  if (!player) return view;
  view.origin = shift_origin(remote, view.origin);
  view.angles = player->view_angles;
  if (!view.has_client_view_offset_delta) view.view_height = player->view_height;
  return view;
}
const qa_app_simulation_presentation *qa_app_unified_remote_presentations(
    qa_app_unified_remote *remote, size_t *out_count) {
  if (!remote->has_current) { *out_count = 0; return NULL; }
  qa_actor_id actor = remote->current.player_actor;
  free(remote->shifted_models);
  remote->shifted_models = xmalloc(remote->current.model_count * sizeof *remote->shifted_models);
  for (size_t i = 0; i < remote->current.model_count; i++) {
    remote->shifted_models[i] = remote->current.models[i];
    if (qa_same_actor(remote->current.models[i].actor, actor)) {
      remote->shifted_models[i].origin = shift_origin(remote, remote->current.models[i].origin);
      if (remote->current.models[i].has_previous_origin)
        remote->shifted_models[i].previous_origin =
            shift_origin(remote, remote->current.models[i].previous_origin);
    }
  }
  *out_count = remote->current.model_count;
  return remote->shifted_models;
}
const qa_q3_character_view *qa_app_unified_remote_character_views(
    qa_app_unified_remote *remote, size_t *out_count) {
  if (!remote->has_current) { *out_count = 0; return NULL; }
  const qa_app_movement_prediction_snapshot *player = predicted_player(remote);
  qa_actor_id actor = remote->current.player_actor;
  free(remote->shifted_characters);
  remote->shifted_characters = xmalloc(remote->current.character_count * sizeof *remote->shifted_characters);
  for (size_t i = 0; i < remote->current.character_count; i++) {
    remote->shifted_characters[i] = remote->current.characters[i];
    if (player && qa_same_actor(remote->current.characters[i].actor, actor)) {
      remote->shifted_characters[i].origin = shift_origin(remote, remote->current.characters[i].origin);
      remote->shifted_characters[i].velocity = qa_app_sim_movement_velocity(player->state);
    }
  }
  *out_count = remote->current.character_count;
  return remote->shifted_characters;
}
const qa_world_text *qa_app_unified_remote_world_text(qa_app_unified_remote *remote,
    size_t *out_count) {
  if (!remote->has_current) { *out_count = 0; return NULL; }
  *out_count = remote->current.world_text_count;
  return remote->current.world_text;
}
bool qa_app_unified_remote_is_player(qa_app_unified_remote *remote, qa_actor_id actor) {
  if (!remote->has_current) return false;
  for (size_t i = 0; i < remote->current.output.snapshot.configuration_count; i++)
    if (qa_same_actor(remote->current.output.snapshot.configurations[i].actor, actor))
      return true;
  return false;
}
bool qa_app_unified_remote_number_of(qa_app_unified_remote *remote, qa_actor_id actor,
    double *out) {
  for (size_t i = 0; i < remote->actor_count; i++)
    if (remote->actors[i].slot == (double)actor.slot && qa_same_actor(remote->actors[i].actor, actor)) {
      *out = remote->actors[i].slot;
      return true;
    }
  return false;
}
void qa_app_unified_remote_player_command(qa_app_unified_remote *remote, qa_actor_id actor,
    const char *name, const char *const *args, size_t count) {
  qa_actor_id player;
  if (!qa_app_unified_remote_player(remote, &player, NULL, NULL)
      || !qa_same_actor(player, actor))
    qa_raise("Error", 0, 0, "Unified command belongs to another player");
  remote->options.send_command(remote->options.ctx, name, args, count);
}
void qa_app_unified_remote_register_resource(qa_app_unified_remote *remote,
    qa_content_id content, const char *path, qa_resolved_resource_reference resource) {
  (void)content;
  (void)path;
  char *id = qa_app_unified_resource_id_from_reference(&resource);
  if (!id) qa_range_error("out of memory");
  resource_set(remote, id, resource);
}
const qa_simulation_event *qa_app_unified_remote_drain_simulation_events(qa_app_unified_remote *remote,
    size_t *out_count) {
  simulation_records_free(remote->drained, remote->drained_count);
  free(remote->drained_events);
  remote->drained = remote->simulation_events;
  remote->drained_count = remote->simulation_count;
  remote->simulation_events = NULL;
  remote->simulation_count = 0;
  remote->simulation_cap = 0;
  remote->drained_events = calloc(remote->drained_count ? remote->drained_count : 1, sizeof *remote->drained_events);
  if (!remote->drained_events) qa_range_error("out of memory");
  for (size_t i = 0; i < remote->drained_count; i++) remote->drained_events[i] = remote->drained[i]->event;
  *out_count = remote->drained_count;
  return remote->drained_events;
}
const qa_app_sim_presentation_event *qa_app_unified_remote_drain_presentation_events(qa_app_unified_remote *remote,
    size_t *out_count) {
  return qa_app_presentation_state_take(remote->media, out_count);
}
qa_app_presentation_state *qa_app_unified_remote_media(qa_app_unified_remote *remote) { return remote->media; }
void qa_app_unified_remote_disconnected(qa_app_unified_remote *remote, const char *reason) {
  remote->options.disconnected(remote->options.ctx, reason);
}
void qa_app_unified_remote_print(qa_app_unified_remote *remote, const char *text) {
  remote->options.print(remote->options.ctx, text);
}
void qa_app_unified_remote_close(qa_app_unified_remote *remote) {
  if (remote->components) {
    qa_app_unified_component_consumers_close(remote->components);
    qa_app_unified_component_consumers_destroy(remote->components);
  }
  remote->components = NULL;
  if (remote->native) {
    qa_app_unified_native_consumers_close(remote->native);
    qa_app_unified_native_consumers_destroy(remote->native);
  }
  remote->native = NULL;
  clear_prediction(remote);
  remote->content_epoch = 0;
  remote->closed = true;
  remote->generation++;
  if (remote->has_current) {
    qa_app_unified_presentation_frame_free(&remote->current);
    remote->has_current = false;
  }
  pending_reset(remote);
  remote->sequence_count = 0;
  size_t taken = 0;
  (void)qa_app_presentation_state_take(remote->media, &taken);
  simulation_records_free(remote->simulation_events, remote->simulation_count);
  remote->simulation_events = NULL;
  remote->simulation_count = 0;
  remote->simulation_cap = 0;
  for (size_t i = 0; i < remote->resource_count; i++) free(remote->resources[i].id);
  remote->resource_count = 0;
  remote->actor_count = 0;
  remote->client_count = 0;
}
