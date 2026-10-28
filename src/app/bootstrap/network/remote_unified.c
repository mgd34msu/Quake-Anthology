/* Port of src/app/bootstrap/network/remote-unified.ts.
 * GPL-2.0-or-later. */
#include "qa/app/bootstrap/network/remote_unified.h"
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/network/unified_component_consumer.h"
#include "qa/app/bootstrap/network/unified_event_codec.h"
#include "qa/app/bootstrap/network/unified_native_consumer.h"
#include "qa/app/bootstrap/simulation/players.h"
#include "qa/foundation/support.h"
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
struct qa_app_unified_remote {
  qa_app_unified_remote_options options;
  qa_app_remote_world_content *world;
  id_entry *actors; size_t actor_count, actor_cap;
  id_entry *clients; size_t client_count, client_cap;
  resource_entry *resources; size_t resource_count, resource_cap;
  pending_events *pending; size_t pending_head, pending_count, pending_cap;
  sequence_entry *source_sequences; size_t sequence_count, sequence_cap;
  qa_save_value **media; size_t media_count, media_cap; double media_next;
  qa_save_value **simulation_events; size_t simulation_count, simulation_cap;
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
};
static void assert_current(qa_app_unified_remote *remote, double generation) {
  if (remote->closed || generation != remote->generation)
    qa_range_error("Unified replica load was retired");
}
typedef struct assert_ctx { qa_app_unified_remote *remote; double generation; } assert_ctx;
static void assert_thunk(void *ctx) {
  assert_ctx *a = ctx;
  assert_current(a->remote, a->generation);
}
static qa_actor_id identity_actor(void *ctx, double slot, double generation) {
  qa_app_unified_remote *remote = ctx;
  for (size_t i = 0; i < remote->actor_count; i++)
    if (remote->actors[i].slot == slot && remote->actors[i].generation == generation)
      return remote->actors[i].actor;
  double local = remote->options.next_generation(remote->options.ctx, slot);
  qa_actor_id value = remote->options.identity_actor(remote->options.ctx, slot, local);
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
  for (size_t i = 0; i < remote->client_count; i++)
    if (remote->clients[i].slot == slot && remote->clients[i].generation == generation)
      return remote->clients[i].client;
  double local = remote->options.next_generation(remote->options.ctx, slot);
  qa_client_id value = remote->options.identity_client(remote->options.ctx, slot, local);
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
    if (remote->resources[i].reference.id
        && strcmp(remote->resources[i].reference.id, id) == 0)
      return remote->resources[i].reference.id;
  qa_range_error("Unified resource was not declared: %s", id);
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
static void consumer_admit_owner(void *ctx, qa_presentation_owner owner, qa_content_id content) {
  qa_app_unified_remote *remote = ctx;
  if (remote->options.admit_owner) remote->options.admit_owner(remote->options.ctx, owner, content);
}
static void consumer_retire_owner(void *ctx, qa_presentation_owner owner) {
  qa_app_unified_remote *remote = ctx;
  if (remote->options.retire_owner) remote->options.retire_owner(remote->options.ctx, owner);
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
    qa_range_error("Remote component command channel is unavailable");
  remote->options.send_component_command(remote->options.ctx, owner, generation, args, count);
}
static void retire_op(void *ctx) {
  qa_app_unified_remote *remote = ctx;
  if (remote->closed) qa_range_error("Unified replica is closed");
  if (remote->components) qa_app_unified_component_consumers_close(remote->components);
  remote->components = NULL;
  if (remote->native) qa_app_unified_native_consumers_close(remote->native);
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
  return remote;
}
void qa_app_unified_remote_destroy(qa_app_unified_remote *remote) {
  if (!remote) return;
  qa_app_unified_remote_close(remote);
  if (remote->components) qa_app_unified_component_consumers_destroy(remote->components);
  if (remote->native) qa_app_unified_native_consumers_destroy(remote->native);
  if (remote->has_current) qa_app_unified_presentation_frame_free(&remote->current);
  qa_app_remote_world_content_destroy(remote->world);
  free(remote->actors);
  free(remote->clients);
  for (size_t i = 0; i < remote->resource_count; i++) free(remote->resources[i].id);
  free(remote->resources);
  free(remote->pending);
  free(remote->source_sequences);
  free(remote->media);
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
  if (remote->closed) qa_range_error("Unified replica is closed");
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
  trees_free(remote->simulation_events, remote->simulation_count);
  remote->simulation_events = NULL;
  remote->simulation_count = 0;
  remote->simulation_cap = 0;
  pending_reset(remote);
  remote->last_event_sequence = -1;
  remote->last_simulation_sequence = -1;
  size_t mod_count = 0;
  const qa_prepared_mod *mods = remote->options.prepared_mods
      ? remote->options.prepared_mods(remote->options.ctx, content, &mod_count) : NULL;
  assert_ctx native_assert = {remote, generation};
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
  (void)native_assert;
  if (remote->options.mod_has_presentation
      && remote->options.mod_has_presentation(remote->options.ctx, content)) {
    if (!remote->options.has_mod_files)
      qa_range_error("Remote component presentation requires client-owned writable storage");
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
static void components_publish(void *ctx) {
  components_op *op = ctx;
  qa_app_unified_remote *remote = op->remote;
  if (op->epoch != remote->world_epoch || remote->closed) return;
  qa_app_unified_native_commit *commit = remote->native
      ? qa_app_unified_native_consumers_prepare_update(remote->native, op->update) : NULL;
  if (!remote->components) {
    if (op->update->source_count != 0)
      qa_range_error("Server activated an unqualified remote component");
  } else qa_app_unified_component_consumers_update(remote->components, op->update);
  if (commit) qa_app_unified_native_commit_run(commit);
}
void qa_app_unified_remote_receive_components(qa_app_unified_remote *remote,
    double epoch, const qa_app_unified_component_update *update) {
  components_op op = {remote, epoch, update};
  if (!remote->options.publish_components) components_publish(&op);
  else remote->options.publish_components(remote->options.ctx, components_publish, &op);
}
const void *qa_app_unified_remote_mod_presentation_sources(qa_app_unified_remote *remote,
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
  if (!value) return fallback;
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, value, key)) return fallback;
  double result = qa_save_reader_number(&reader);
  qa_save_reader_destroy(&reader);
  return result;
}
static const char *tree_string(const qa_save_value *tree, const char *key) {
  const qa_save_value *value = qa_save_value_get(tree, key);
  if (!value) return NULL;
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, value, key)) return NULL;
  const char *result = qa_save_reader_string(&reader);
  qa_save_reader_destroy(&reader);
  return result;
}
static double media_receive(qa_app_unified_remote *remote, qa_save_value *event) {
  if (remote->media_count == remote->media_cap) {
    remote->media_cap = remote->media_cap ? remote->media_cap * 2 : 32;
    remote->media = realloc(remote->media, remote->media_cap * sizeof *remote->media);
    if (!remote->media) qa_range_error("out of memory");
  }
  remote->media[remote->media_count++] = event;
  return remote->media_next++;
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
static void release_events(qa_app_unified_remote *remote) {
  if (!remote->has_current) return;
  double frame = remote->current.output.snapshot.frame.frame;
  while (remote->pending_count > 0) {
    pending_events *next = &remote->pending[remote->pending_head];
    if (next->frame > frame) break;
    for (size_t i = 0; i < next->event_count; i++) {
      qa_save_value *event = next->events[i];
      next->events[i] = NULL;
      double sequence = tree_number(event, "sequence", -1);
      if (sequence > remote->last_event_sequence) {
        const char *kind = tree_string(event, "kind");
        const qa_save_value *inner = qa_save_value_get(event, "event");
        if (kind && strcmp(kind, "presentation-owner") == 0 && inner) {
          const char *inner_kind = tree_string(inner, "kind");
          if (inner_kind && strcmp(inner_kind, "retired") == 0 && remote->native) {
            const qa_save_value *owner = qa_save_value_get(inner, "owner");
            if (owner) {
              qa_save_reader reader;
              if (qa_save_reader_init(&reader, owner, "owner")) {
                qa_presentation_owner value = qa_app_unified_read_component_owner(&reader);
                qa_save_reader_destroy(&reader);
                qa_app_unified_native_consumers_retire(remote->native, value);
              }
            }
          }
        }
        sequence_set(remote, sequence, media_receive(remote, event));
        remote->last_event_sequence = sequence;
      } else qa_save_value_free(event);
    }
    for (size_t i = 0; i < next->simulation_count; i++) {
      qa_save_value *event = next->simulation[i];
      next->simulation[i] = NULL;
      double sequence = tree_number(event, "sequence", -1);
      if (sequence > remote->last_simulation_sequence) {
        qa_save_value *payload = (qa_save_value *)qa_save_value_get(event, "payload");
        if (payload) {
          const char *kind = tree_string(payload, "kind");
          const qa_save_value *source = qa_save_value_get(payload, "sourcePresentationSequence");
          if (kind && strcmp(kind, "message") == 0 && source) {
            double source_sequence = tree_number(payload, "sourcePresentationSequence", -1);
            double local = 0;
            if (sequence_get(remote, source_sequence, &local))
              qa_save_value_set(payload, "sourcePresentationSequence",
                  qa_save_value_number(local));
          }
        }
        if (remote->simulation_count == remote->simulation_cap) {
          remote->simulation_cap = remote->simulation_cap ? remote->simulation_cap * 2 : 64;
          remote->simulation_events = realloc(remote->simulation_events,
              remote->simulation_cap * sizeof *remote->simulation_events);
          if (!remote->simulation_events) qa_range_error("out of memory");
        }
        remote->simulation_events[remote->simulation_count++] = event;
        remote->last_simulation_sequence = sequence;
      } else qa_save_value_free(event);
    }
    free(next->events);
    free(next->simulation);
    remote->pending_head = (remote->pending_head + 1) % remote->pending_cap;
    remote->pending_count--;
  }
  if (remote->sequence_count > 4096) {
    size_t kept = 0;
    for (size_t i = 0; i < remote->sequence_count; i++)
      if (remote->source_sequences[i].source >= remote->last_event_sequence - 2048)
        remote->source_sequences[kept++] = remote->source_sequences[i];
    remote->sequence_count = kept;
  }
}
void qa_app_unified_remote_receive_events(qa_app_unified_remote *remote, double epoch,
    double frame, const uint8_t *payload, size_t payload_length,
    const uint8_t *simulation, size_t simulation_length) {
  if (epoch != remote->world_epoch || remote->closed) return;
  qa_app_unified_identity_decoder identity = identity_of(remote);
  size_t event_count = 0;
  qa_save_value **events = qa_app_unified_decode_presentation_events(
      payload, payload_length, &identity, &event_count);
  qa_save_value *decoded = qa_decode_checkpoint_value(simulation, simulation_length);
  if (!decoded) qa_range_error("out of memory");
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, decoded, "unified-events")) qa_range_error("out of memory");
  size_t count = qa_save_reader_list_count(&reader);
  qa_save_value **simulations = xmalloc(count * sizeof *simulations);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader item;
    if (!qa_save_reader_item(&reader, i, &item)) qa_range_error("out of memory");
    const qa_save_value *value = reader.value;
    (void)value;
    qa_save_reader value_reader;
    const qa_save_value *item_value = item.value;
    (void)value_reader;
    simulations[i] = qa_app_unified_read_simulation_event(item_value, &identity);
    qa_save_reader_destroy(&item);
  }
  qa_save_reader_destroy(&reader);
  qa_save_value_free(decoded);
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
  pending_events *entry = &remote->pending[
      (remote->pending_head + remote->pending_count) % remote->pending_cap];
  entry->frame = frame;
  entry->events = events;
  entry->event_count = event_count;
  entry->simulation = simulations;
  entry->simulation_count = count;
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
static qa_decoded_model decoder_model(void *ctx, const qa_resolved_resource_reference *reference) {
  decoder_ctx *decoder = ctx;
  qa_app_unified_resource_key key =
      qa_app_unified_resource_key_from_reference(reference);
  /* The frame decoder's model seam carries no brush index in C. */
  return decoder->remote->options.model(decoder->remote->options.ctx, &key, false, 0);
}
static const qa_scene_snapshot_world *decoder_world(void *ctx,
    const qa_resolved_resource_reference *reference) {
  (void)reference;
  decoder_ctx *decoder = ctx;
  return decoder->remote->options.world_geometry(decoder->remote->options.ctx, decoder->loaded);
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
    qa_range_error("Unified prediction does not match admitted snapshot");
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
bool qa_app_unified_remote_receive_frame(qa_app_unified_remote *remote,
    const uint8_t *bytes, size_t length, double *out_acknowledged) {
  if (remote->closed || !remote->has_binding) return false;
  void *loaded = qa_app_remote_world_loaded(remote->world);
  decoder_ctx context = {remote, loaded, remote->generation};
  qa_app_unified_frame_decoder decoder = {0};
  decoder.ctx = &context;
  decoder.identity = identity_of(remote);
  decoder.resource = decoder_resource;
  decoder.model = decoder_model;
  decoder.world = decoder_world;
  qa_app_unified_presentation_frame_t frame =
      qa_app_unified_decode_frame(bytes, length, &decoder);
  if (frame.epoch != remote->world_epoch) {
    qa_app_unified_presentation_frame_free(&frame);
    return false;
  }
  assert_current(remote, context.generation);
  qa_actor_id expected = identity_actor(remote,
      remote->binding_actor.slot, remote->binding_actor.generation);
  if (!qa_same_actor(frame.player_actor, expected)) {
    qa_app_unified_presentation_frame_free(&frame);
    qa_range_error("Unified frame changed the admitted player");
  }
  if (remote->has_current
      && frame.output.snapshot.frame.frame <= remote->current.output.snapshot.frame.frame) {
    *out_acknowledged = frame.acknowledged_input;
    qa_app_unified_presentation_frame_free(&frame);
    return true;
  }
  qa_app_unified_component_frames empty = {0};
  const qa_app_unified_component_frames *frames =
      frame.has_components ? &frame.components : &empty;
  qa_app_unified_native_camera legacy = {0};
  const qa_app_unified_native_camera *legacy_ptr = NULL;
  if (frame.has_native_camera) {
    legacy.owner = NULL;
    legacy.identity = NULL;
    legacy.generation = frame.native_camera.generation;
    legacy.view = NULL;
    legacy_ptr = &legacy;
  }
  qa_app_unified_native_commit *commit = remote->native
      ? qa_app_unified_native_consumers_prepare(remote->native, frames, expected, legacy_ptr)
      : NULL;
  if (remote->native && !commit) {
    *out_acknowledged = frame.acknowledged_input;
    qa_app_unified_presentation_frame_free(&frame);
    return true;
  }
  if (remote->components
      && !qa_app_unified_component_consumers_accept(remote->components, frames, expected)) {
    if (commit) qa_app_unified_native_commit_free(commit);
    *out_acknowledged = frame.acknowledged_input;
    qa_app_unified_presentation_frame_free(&frame);
    return true;
  }
  if (!remote->components && frames->source_count != 0) {
    if (commit) qa_app_unified_native_commit_free(commit);
    qa_app_unified_presentation_frame_free(&frame);
    qa_range_error("Remote frame contains unadmitted components");
  }
  if (remote->has_current) qa_app_unified_presentation_frame_free(&remote->current);
  remote->current = frame;
  remote->has_current = true;
  remote->current.output.events = NULL;
  remote->current.output.event_count = 0;
  if (commit) qa_app_unified_native_commit_run(commit);
  correct_prediction(remote, &remote->current);
  release_events(remote);
  remote->options.publish(remote->options.ctx, &remote->current.output);
  *out_acknowledged = frame.acknowledged_input;
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
    qa_range_error("Unified player UI belongs to another actor");
  return &remote->current.player_ui;
}
qa_app_sim_player_view qa_app_unified_remote_player_view(qa_app_unified_remote *remote,
    qa_actor_id actor) {
  if (!remote->has_current || !qa_same_actor(remote->current.player_actor, actor))
    qa_range_error("Unified player view belongs to another actor");
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
    if (qa_same_actor(remote->actors[i].actor, actor)) {
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
    qa_range_error("Unified command belongs to another player");
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
qa_save_value **qa_app_unified_remote_drain_simulation_events(qa_app_unified_remote *remote,
    size_t *out_count) {
  qa_save_value **events = remote->simulation_events;
  *out_count = remote->simulation_count;
  remote->simulation_events = NULL;
  remote->simulation_count = 0;
  remote->simulation_cap = 0;
  return events;
}
qa_save_value **qa_app_unified_remote_drain_presentation_events(qa_app_unified_remote *remote,
    size_t *out_count) {
  qa_save_value **events = remote->media;
  *out_count = remote->media_count;
  remote->media = NULL;
  remote->media_count = 0;
  remote->media_cap = 0;
  return events;
}
void qa_app_unified_remote_disconnected(qa_app_unified_remote *remote, const char *reason) {
  remote->options.disconnected(remote->options.ctx, reason);
}
void qa_app_unified_remote_print(qa_app_unified_remote *remote, const char *text) {
  remote->options.print(remote->options.ctx, text);
}
void qa_app_unified_remote_close(qa_app_unified_remote *remote) {
  if (remote->components) qa_app_unified_component_consumers_close(remote->components);
  remote->components = NULL;
  if (remote->native) qa_app_unified_native_consumers_close(remote->native);
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
  trees_free(remote->media, remote->media_count);
  remote->media = NULL;
  remote->media_count = 0;
  remote->media_cap = 0;
  trees_free(remote->simulation_events, remote->simulation_count);
  remote->simulation_events = NULL;
  remote->simulation_count = 0;
  remote->simulation_cap = 0;
  for (size_t i = 0; i < remote->resource_count; i++) free(remote->resources[i].id);
  remote->resource_count = 0;
  remote->actor_count = 0;
  remote->client_count = 0;
}
