/* See include/qa/compat/qvm/mod_presentation_checkpoint.h. GPL-2.0-or-later. */
#include "qa/compat/qvm/mod_presentation_checkpoint.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/compat/qvm/memory.h"
#include "qa/compat/qvm/player_record.h"
#include "qa/core/binary/index.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/persistence/save_image.h"
#include "qa/persistence/shared.h"

static void *xmalloc(size_t size) {
  void *result = malloc(size == 0 ? 1 : size);
  if (result == NULL) qa_range_error("QVM memory allocation failed");
  return result;
}

/* Source entity read/write are the landed entity_record.h
 * functions; adapt this module's writable views to qa_qvmx_view. */
static qa_qvmx_view raw_view(const qa_qvm_writable_view *view) {
  qa_qvmx_view raw = {view->bytes + view->start, (size_t)view->length};
  return raw;
}

static void read_entity_fields(const qa_qvm_writable_view *view, qa_qvm_abi_profile profile,
                               qa_qvm_entity_state_fields *out) {
  qa_qvm_entity_state state = qa_qvm_read_source_entity_state(raw_view(view), profile);
  qa_qvm_get_entity_state_fields(&state, out);
  qa_qvm_entity_state_destroy(&state);
}

static void write_entity_fields(qa_qvm_writable_view *view, const qa_qvm_entity_state_fields *state,
                                qa_qvm_abi_profile profile) {
  qa_qvm_write_source_entity_state(raw_view(view), state, profile);
}

static qa_save_reader field(const qa_save_reader *reader, const char *name) {
  qa_save_reader out;
  if (!qa_save_reader_field(reader, name, &out)) qa_range_error("QVM memory allocation failed");
  return out;
}

static qa_save_reader item_at(const qa_save_reader *reader, size_t index) {
  qa_save_reader out;
  if (!qa_save_reader_item(reader, index, &out)) qa_range_error("QVM memory allocation failed");
  return out;
}

void qa_qvm_capture_presentation_game_state(const qa_q3_source_game_state_record *state,
                                            qa_qvm_presentation_game_state *out) {
  out->string_offsets = xmalloc(1024 * sizeof *out->string_offsets);
  memcpy(out->string_offsets, state->string_offsets, 1024 * sizeof *out->string_offsets);
  out->string_offset_count = 1024;
  out->string_data = xmalloc(16000);
  memcpy(out->string_data, state->string_data, 16000);
  out->string_data_length = 16000;
  out->data_count = state->data_count;
}

void qa_qvm_presentation_game_state_free(qa_qvm_presentation_game_state *state) {
  free(state->string_offsets);
  free(state->string_data);
  memset(state, 0, sizeof *state);
}

void qa_qvm_read_presentation_game_state(const qa_save_reader *reader, qa_q3_source_game_state_record *out) {
  memset(out, 0, sizeof *out);
  qa_save_reader offsets = field(reader, "stringOffsets");
  size_t offset_count = qa_save_reader_list_count(&offsets);
  int32_t *values = xmalloc(offset_count * sizeof *values);
  for (size_t i = 0; i < offset_count; i++) {
    qa_save_reader item = item_at(&offsets, i);
    double value = qa_save_reader_integer_min(&item, 0);
    /* TS Int32Array.from applies a ToInt32 wrap, no range failure. */
    values[i] = qa_int32(value);
    qa_save_reader_destroy(&item);
  }
  qa_save_reader_destroy(&offsets);
  qa_save_reader data_reader = field(reader, "stringData");
  size_t data_length = 0;
  const uint8_t *borrowed = qa_save_reader_bytes(&data_reader, &data_length);
  uint8_t *data = xmalloc(data_length);
  memcpy(data, borrowed, data_length);
  qa_save_reader_destroy(&data_reader);
  qa_save_reader count_reader = field(reader, "dataCount");
  int64_t count = (int64_t)qa_save_reader_integer_min(&count_reader, 0);
  qa_save_reader_destroy(&count_reader);
  int64_t limit = count > 1 ? count : 1;
  bool invalid = offset_count != 1024 || data_length != 16000 || count > 16000;
  for (size_t i = 0; i < offset_count && !invalid; i++) if (values[i] >= limit) invalid = true;
  if (invalid) qa_save_reader_fail(reader, "invalid source gameState extent");
  out->string_offsets = values;
  out->string_data = data;
  out->data_count = count;
}

/* Mirrored writeSourceQvmSnapshot (TS client-state-record.ts). */
static void write_source_snapshot(qa_qvm_memory *memory, qa_qvm_writable_view *view,
                                  const qa_qvm_source_snapshot *snapshot, qa_qvm_abi_profile profile) {
  int size = (int)qa_qvm_snapshot_bytes_for(profile);
  if (view->length < size) qa_range_error("QVM client record requires %d bytes", size);
  if (snapshot->area_mask_length != 32 || snapshot->entity_count > 256)
    qa_range_error("Invalid source snapshot_t extent");
  int ps_bytes = qa_qvm_player_state_bytes(profile);
  int entity_bytes = (int)qa_qvm_entity_state_bytes_for(profile);
  int64_t offset = view->start;
  qa_qvm_writable_view_set_int32(view, 0, qa_int32(snapshot->flags), true);
  qa_qvm_writable_view_set_int32(view, 4, qa_int32(snapshot->player_state.ping_milliseconds), true); /* setInt32 */
  qa_qvm_writable_view_set_int32(view, 8, qa_int32(snapshot->server_time), true);
  qa_qvm_memory_write_bytes(memory, offset + 12, snapshot->area_mask, 32);
  qa_qvm_writable_view player_view;
  qa_qvm_memory_data_view(memory, offset + 44, ps_bytes, &player_view);
  qa_qvm_write_source_player_state(&player_view, &snapshot->player_state, profile);
  qa_qvm_writable_view_set_int32(view, 44 + ps_bytes, (int32_t)snapshot->entity_count, true);
  for (size_t i = 0; i < snapshot->entity_count; i++) {
    qa_qvm_writable_view entity_view;
    qa_qvm_memory_data_view(memory, offset + 48 + ps_bytes + (int64_t)i * entity_bytes, entity_bytes, &entity_view);
    write_entity_fields(&entity_view, &snapshot->entities[i], profile);
  }
  qa_qvm_writable_view_set_int32(view, size - 4, qa_int32(snapshot->server_command_sequence), true);
}

void qa_qvm_capture_presentation_snapshot(const qa_qvm_source_snapshot *snapshot, qa_qvm_abi_profile profile,
                                          qa_qvm_presentation_snapshot *out) {
  int size = (int)qa_qvm_snapshot_bytes_for(profile);
  size_t allocation = 1;
  while (allocation < (size_t)size) allocation <<= 1;
  uint8_t *bytes = xmalloc(allocation);
  memset(bytes, 0, allocation);
  qa_qvm_memory memory;
  qa_qvm_memory_init(&memory, bytes, allocation, NULL, NULL);
  qa_qvm_writable_view view;
  qa_qvm_memory_data_view(&memory, 0, size, &view);
  write_source_snapshot(&memory, &view, snapshot, profile);
  qa_qvm_memory_destroy(&memory);
  out->number = (int64_t)snapshot->number;
  out->bytes = xmalloc((size_t)size);
  memcpy(out->bytes, bytes, (size_t)size);
  out->byte_length = (size_t)size;
  free(bytes);
}

void qa_qvm_presentation_snapshot_free(qa_qvm_presentation_snapshot *snapshot) {
  free(snapshot->bytes);
  memset(snapshot, 0, sizeof *snapshot);
}

static int32_t bytes_i32(const uint8_t *bytes, size_t offset) {
  uint32_t bits = (uint32_t)bytes[offset] | ((uint32_t)bytes[offset + 1] << 8) |
                  ((uint32_t)bytes[offset + 2] << 16) | ((uint32_t)bytes[offset + 3] << 24);
  return (int32_t)bits;
}

static void prepare_player_slots(qa_q3_player_state *state, double slots[64]) {
  state->stats.values = slots;
  state->persistent.values = slots + 16;
  state->powerups.values = slots + 32;
  state->ammo.values = slots + 48;
}

void qa_qvm_read_presentation_snapshot(const qa_save_reader *reader, qa_qvm_abi_profile profile,
                                       qa_qvm_source_snapshot *out) {
  memset(out, 0, sizeof *out);
  qa_save_reader number = field(reader, "number");
  out->number = qa_save_reader_integer_min(&number, 0);
  qa_save_reader_destroy(&number);
  qa_save_reader bytes_reader = field(reader, "bytes");
  size_t length = 0;
  const uint8_t *borrowed = qa_save_reader_bytes(&bytes_reader, &length);
  uint8_t *bytes = xmalloc(length);
  memcpy(bytes, borrowed, length);
  qa_save_reader_destroy(&bytes_reader);
  if ((int64_t)length != (int64_t)qa_qvm_snapshot_bytes_for(profile))
    qa_save_reader_fail(reader, "invalid source snapshot extent");
  int player_size = qa_qvm_player_state_bytes(profile);
  int entity_size = (int)qa_qvm_entity_state_bytes_for(profile);
  int32_t count = bytes_i32(bytes, (size_t)(44 + player_size));
  if (count < 0 || count > 256) qa_save_reader_fail(reader, "invalid source snapshot entity count");
  out->server_time = bytes_i32(bytes, 8);
  out->flags = bytes_i32(bytes, 0);
  uint8_t *area_mask = xmalloc(32);
  memcpy(area_mask, bytes + 12, 32);
  out->area_mask = area_mask;
  out->area_mask_length = 32;
  qa_qvm_memory_writes writes;
  qa_qvm_memory_writes_init(&writes, bytes, length, NULL, NULL);
  double *player_slots = xmalloc(64 * sizeof *player_slots);
  memset(player_slots, 0, 64 * sizeof *player_slots);
  prepare_player_slots(&out->player_state, player_slots);
  qa_qvm_writable_view player_view;
  qa_qvm_writable_view_init(&player_view, bytes, length, 44, player_size, &writes);
  qa_qvm_read_source_player_state(&player_view, profile, &out->player_state);
  qa_qvm_entity_state_fields *entities = xmalloc((size_t)count * sizeof *entities);
  out->entities = entities;
  out->entity_count = (size_t)count;
  for (int32_t i = 0; i < count; i++) {
    qa_qvm_writable_view entity_view;
    qa_qvm_writable_view_init(&entity_view, bytes, length, 48 + player_size + i * entity_size, entity_size, &writes);
    read_entity_fields(&entity_view, profile, &entities[i]);
  }
  out->server_command_sequence = bytes_i32(bytes, length - 4);
  qa_qvm_memory_writes_destroy(&writes);
  free(bytes);
}

void qa_qvm_source_snapshot_free(qa_qvm_source_snapshot *snapshot) {
  free((void *)snapshot->entities);
  free((void *)snapshot->area_mask);
  free((void *)snapshot->player_state.stats.values);
  memset(snapshot, 0, sizeof *snapshot);
}

static qa_save_value *make_object(void) {
  qa_save_value *value = qa_save_value_object();
  if (value == NULL) qa_range_error("QVM memory allocation failed");
  return value;
}

static qa_save_value *make_array(void) {
  qa_save_value *value = qa_save_value_array();
  if (value == NULL) qa_range_error("QVM memory allocation failed");
  return value;
}

static void set_child(qa_save_value *object, const char *key, qa_save_value *child) {
  if (child == NULL || !qa_save_value_set(object, key, child)) qa_range_error("QVM memory allocation failed");
}

static void set_number(qa_save_value *object, const char *key, double value) {
  set_child(object, key, qa_save_value_number(value));
}

static void set_string(qa_save_value *object, const char *key, const char *value) {
  set_child(object, key, qa_save_value_string(value));
}

static void push_child(qa_save_value *array, qa_save_value *child) {
  if (child == NULL || !qa_save_value_push(array, child)) qa_range_error("QVM memory allocation failed");
}

static qa_save_value *capture_game_state_value(const qa_q3_source_game_state_record *state) {
  qa_save_value *result = make_object();
  qa_save_value *offsets = make_array();
  for (int i = 0; i < 1024; i++) push_child(offsets, qa_save_value_number(state->string_offsets[i]));
  set_child(result, "stringOffsets", offsets);
  set_child(result, "stringData", qa_save_value_bytes(state->string_data, 16000));
  set_number(result, "dataCount", (double)state->data_count);
  return result;
}

qa_save_value *qa_qvm_capture_scene_context(const qa_qvm_scene_context *scene, qa_qvm_abi_profile profile) {
  qa_save_value *result = make_object();
  set_number(result, "revision", (double)scene->revision);
  set_child(result, "gameState", capture_game_state_value(&scene->game_state));
  set_number(result, "gameStateRevision", (double)scene->game_state_revision);
  qa_qvm_source_snapshot numbered = scene->snapshot;
  numbered.number = 0;
  qa_qvm_presentation_snapshot captured;
  qa_qvm_capture_presentation_snapshot(&numbered, profile, &captured);
  qa_save_value *snapshot = make_object();
  set_number(snapshot, "number", (double)captured.number);
  set_child(snapshot, "bytes", qa_save_value_bytes(captured.bytes, captured.byte_length));
  qa_qvm_presentation_snapshot_free(&captured);
  set_child(result, "snapshot", snapshot);
  qa_save_value *actors = make_array();
  for (size_t i = 0; i < scene->actor_count; i++) {
    qa_save_value *row = make_object();
    set_number(row, "slot", (double)scene->actors[i].slot);
    set_child(row, "owned", qa_save_value_boolean(scene->actors[i].owned));
    qa_save_value *actor = make_object();
    qa_saved_actor_id saved = qa_saved_actor_id_of(scene->actors[i].actor);
    set_number(actor, "slot", saved.slot);
    set_number(actor, "generation", saved.generation);
    set_child(row, "actor", actor);
    push_child(actors, row);
  }
  set_child(result, "actors", actors);
  qa_save_value *commands = make_array();
  for (size_t i = 0; i < scene->command_count; i++) {
    qa_save_value *row = make_object();
    set_number(row, "sequence", (double)scene->commands[i].sequence);
    qa_save_value *arguments = make_array();
    for (size_t a = 0; a < scene->commands[i].argument_count; a++)
      push_child(arguments, qa_save_value_string(scene->commands[i].arguments[a]));
    set_child(row, "arguments", arguments);
    push_child(commands, row);
  }
  set_child(result, "commands", commands);
  if (scene->baseline == NULL) set_child(result, "baseline", qa_save_value_null());
  else set_child(result, "baseline", qa_qvm_capture_scene_context(scene->baseline, profile));
  return result;
}

static void read_scene_context_depth(const qa_save_reader *reader, qa_qvm_abi_profile profile,
                                     qa_qvm_resolve_saved_actor_fn resolve, void *resolve_ctx,
                                     int depth, qa_qvm_scene_context *out) {
  memset(out, 0, sizeof *out);
  if (depth > 1) qa_save_reader_fail(reader, "nested scene baseline");
  qa_save_reader baseline_reader = field(reader, "baseline");
  if (!qa_save_reader_is_null(&baseline_reader)) {
    out->baseline = xmalloc(sizeof *out->baseline);
    read_scene_context_depth(&baseline_reader, profile, resolve, resolve_ctx, depth + 1, out->baseline);
  }
  qa_save_reader_destroy(&baseline_reader);
  qa_save_reader revision = field(reader, "revision");
  out->revision = (int64_t)qa_save_reader_integer_min(&revision, 0);
  qa_save_reader_destroy(&revision);
  qa_save_reader game_state = field(reader, "gameState");
  qa_qvm_read_presentation_game_state(&game_state, &out->game_state);
  qa_save_reader_destroy(&game_state);
  qa_save_reader game_state_revision = field(reader, "gameStateRevision");
  out->game_state_revision = (int64_t)qa_save_reader_integer_min(&game_state_revision, 0);
  qa_save_reader_destroy(&game_state_revision);
  qa_save_reader snapshot = field(reader, "snapshot");
  qa_qvm_read_presentation_snapshot(&snapshot, profile, &out->snapshot);
  qa_save_reader_destroy(&snapshot);
  qa_save_reader actors = field(reader, "actors");
  size_t actor_count = qa_save_reader_list_count(&actors);
  out->actors = xmalloc(actor_count * sizeof *out->actors);
  out->actor_count = actor_count;
  for (size_t i = 0; i < actor_count; i++) {
    qa_save_reader row = item_at(&actors, i);
    qa_save_reader slot = field(&row, "slot");
    out->actors[i].slot = (int64_t)qa_save_reader_integer_min(&slot, 0);
    qa_save_reader_destroy(&slot);
    qa_save_reader owned = field(&row, "owned");
    out->actors[i].owned = qa_save_reader_boolean(&owned);
    qa_save_reader_destroy(&owned);
    qa_save_reader actor = field(&row, "actor");
    qa_saved_actor_id saved;
    qa_save_reader saved_slot = field(&actor, "slot");
    saved.slot = qa_save_reader_integer_min(&saved_slot, 0);
    qa_save_reader_destroy(&saved_slot);
    qa_save_reader saved_generation = field(&actor, "generation");
    saved.generation = qa_save_reader_integer_min(&saved_generation, 0);
    qa_save_reader_destroy(&saved_generation);
    qa_save_reader_destroy(&actor);
    out->actors[i].actor = resolve(resolve_ctx, saved);
    qa_save_reader_destroy(&row);
  }
  qa_save_reader_destroy(&actors);
  qa_save_reader commands = field(reader, "commands");
  size_t command_count = qa_save_reader_list_count(&commands);
  out->commands = xmalloc(command_count * sizeof *out->commands);
  out->command_count = command_count;
  for (size_t i = 0; i < command_count; i++) {
    qa_save_reader row = item_at(&commands, i);
    qa_save_reader sequence = field(&row, "sequence");
    out->commands[i].sequence = (int64_t)qa_save_reader_integer_min(&sequence, 0);
    qa_save_reader_destroy(&sequence);
    qa_save_reader arguments = field(&row, "arguments");
    size_t argument_count = qa_save_reader_list_count(&arguments);
    const char **values = xmalloc(argument_count * sizeof *values);
    for (size_t a = 0; a < argument_count; a++) {
      qa_save_reader value = item_at(&arguments, a);
      values[a] = qa_save_reader_string(&value);
      qa_save_reader_destroy(&value);
    }
    qa_save_reader_destroy(&arguments);
    out->commands[i].arguments = values;
    out->commands[i].argument_count = argument_count;
    qa_save_reader_destroy(&row);
  }
  qa_save_reader_destroy(&commands);
}

void qa_qvm_read_scene_context(const qa_save_reader *reader, qa_qvm_abi_profile profile,
                               qa_qvm_resolve_saved_actor_fn resolve, void *resolve_ctx,
                               qa_qvm_scene_context *out) {
  read_scene_context_depth(reader, profile, resolve, resolve_ctx, 0, out);
}

void qa_qvm_scene_context_free(qa_qvm_scene_context *scene) {
  if (scene->baseline != NULL) {
    qa_qvm_scene_context_free(scene->baseline);
    free(scene->baseline);
  }
  qa_q3_source_game_state_record_destroy(&scene->game_state);
  qa_qvm_source_snapshot_free(&scene->snapshot);
  free(scene->actors);
  for (size_t i = 0; i < scene->command_count; i++) free((void *)scene->commands[i].arguments);
  free(scene->commands);
  memset(scene, 0, sizeof *scene);
}

static qa_save_value *saved_actor_value(qa_actor_id actor) {
  qa_saved_actor_id saved = qa_saved_actor_id_of(actor);
  qa_save_value *value = make_object();
  set_number(value, "slot", saved.slot);
  set_number(value, "generation", saved.generation);
  return value;
}

qa_save_value *qa_qvm_capture_mod_scene_publication(const qa_qvm_cp_mod_scene_publication *scene,
                                                    qa_qvm_abi_profile profile) {
  qa_save_value *result = make_object();
  set_number(result, "revision", (double)scene->revision);
  set_number(result, "serverTime", (double)scene->server_time);
  set_number(result, "gameStateRevision", (double)scene->game_state_revision);
  set_child(result, "gameState", capture_game_state_value(&scene->game_state));
  qa_save_value *clients = make_array();
  int player_bytes = qa_qvm_player_state_bytes(profile);
  int entity_bytes = (int)qa_qvm_entity_state_bytes_for(profile);
  for (size_t i = 0; i < scene->client_count; i++) {
    uint8_t *state = xmalloc((size_t)player_bytes);
    memset(state, 0, (size_t)player_bytes);
    qa_qvm_memory_writes writes;
    qa_qvm_memory_writes_init(&writes, state, (size_t)player_bytes, NULL, NULL);
    qa_qvm_writable_view view;
    qa_qvm_writable_view_init(&view, state, (size_t)player_bytes, 0, player_bytes, &writes);
    qa_qvm_write_source_player_state(&view, &scene->clients[i].state, profile);
    qa_qvm_memory_writes_destroy(&writes);
    qa_save_value *row = make_object();
    set_child(row, "actor", saved_actor_value(scene->clients[i].actor));
    set_number(row, "slot", (double)scene->clients[i].slot);
    set_child(row, "state", qa_save_value_bytes(state, (size_t)player_bytes));
    free(state);
    push_child(clients, row);
  }
  set_child(result, "clients", clients);
  qa_save_value *entities = make_array();
  for (size_t i = 0; i < scene->entity_count; i++) {
    uint8_t *state = xmalloc((size_t)entity_bytes);
    memset(state, 0, (size_t)entity_bytes);
    qa_qvm_memory_writes writes;
    qa_qvm_memory_writes_init(&writes, state, (size_t)entity_bytes, NULL, NULL);
    qa_qvm_writable_view view;
    qa_qvm_writable_view_init(&view, state, (size_t)entity_bytes, 0, entity_bytes, &writes);
    write_entity_fields(&view, &scene->entities[i].state, profile);
    qa_qvm_memory_writes_destroy(&writes);
    qa_save_value *row = make_object();
    set_child(row, "actor", saved_actor_value(scene->entities[i].actor));
    set_child(row, "owned", qa_save_value_boolean(scene->entities[i].owned));
    set_child(row, "linked", qa_save_value_boolean(scene->entities[i].linked));
    set_number(row, "serverFlags", (double)scene->entities[i].server_flags);
    set_number(row, "singleClient", (double)scene->entities[i].single_client);
    qa_save_value *bounds = make_object();
    qa_save_value *min = make_object();
    set_number(min, "x", scene->entities[i].bounds.min.x);
    set_number(min, "y", scene->entities[i].bounds.min.y);
    set_number(min, "z", scene->entities[i].bounds.min.z);
    qa_save_value *max = make_object();
    set_number(max, "x", scene->entities[i].bounds.max.x);
    set_number(max, "y", scene->entities[i].bounds.max.y);
    set_number(max, "z", scene->entities[i].bounds.max.z);
    set_child(bounds, "min", min);
    set_child(bounds, "max", max);
    set_child(row, "bounds", bounds);
    set_child(row, "state", qa_save_value_bytes(state, (size_t)entity_bytes));
    free(state);
    push_child(entities, row);
  }
  set_child(result, "entities", entities);
  qa_save_value *commands = make_array();
  for (size_t i = 0; i < scene->command_count; i++) {
    qa_save_value *row = make_object();
    set_number(row, "sequence", (double)scene->commands[i].sequence);
    if (!scene->commands[i].has_recipient) set_child(row, "recipient", qa_save_value_null());
    else set_child(row, "recipient", saved_actor_value(scene->commands[i].recipient_storage));
    set_string(row, "text", scene->commands[i].text);
    push_child(commands, row);
  }
  set_child(result, "commands", commands);
  return result;
}

void qa_qvm_read_mod_scene_publication(const qa_save_reader *reader, qa_qvm_abi_profile profile,
                                       qa_qvm_resolve_saved_actor_fn resolve, void *resolve_ctx,
                                       qa_qvm_cp_mod_scene_publication *out) {
  memset(out, 0, sizeof *out);
  struct { const char *name; int64_t *slot; } scalars[] = {
      {"revision", &out->revision}, {"serverTime", &out->server_time},
      {"gameStateRevision", &out->game_state_revision}};
  for (int i = 0; i < 3; i++) {
    qa_save_reader at = field(reader, scalars[i].name);
    *scalars[i].slot = (int64_t)qa_save_reader_integer_min(&at, 0);
    qa_save_reader_destroy(&at);
  }
  qa_save_reader game_state = field(reader, "gameState");
  qa_qvm_read_presentation_game_state(&game_state, &out->game_state);
  qa_save_reader_destroy(&game_state);
  int player_bytes = qa_qvm_player_state_bytes(profile);
  int entity_bytes = (int)qa_qvm_entity_state_bytes_for(profile);
  qa_save_reader clients = field(reader, "clients");
  size_t client_count = qa_save_reader_list_count(&clients);
  out->clients = xmalloc(client_count * sizeof *out->clients);
  out->client_count = client_count;
  for (size_t i = 0; i < client_count; i++) {
    qa_save_reader row = item_at(&clients, i);
    memset(&out->clients[i], 0, sizeof out->clients[i]);
    qa_save_reader state_reader = field(&row, "state");
    size_t length = 0;
    const uint8_t *borrowed = qa_save_reader_bytes(&state_reader, &length);
    if ((int64_t)length != player_bytes) qa_save_reader_fail(&row, "invalid source player bytes");
    uint8_t *bytes = xmalloc(length);
    memcpy(bytes, borrowed, length);
    qa_save_reader_destroy(&state_reader);
    qa_save_reader actor_reader = field(&row, "actor");
    out->clients[i].actor = resolve(resolve_ctx, qa_read_saved_actor(&actor_reader));
    qa_save_reader_destroy(&actor_reader);
    qa_save_reader slot_reader = field(&row, "slot");
    out->clients[i].slot = (int64_t)qa_save_reader_integer_min(&slot_reader, 0);
    qa_save_reader_destroy(&slot_reader);
    prepare_player_slots(&out->clients[i].state, out->clients[i].state_slots);
    qa_qvm_memory_writes writes;
    qa_qvm_memory_writes_init(&writes, bytes, length, NULL, NULL);
    qa_qvm_writable_view view;
    qa_qvm_writable_view_init(&view, bytes, length, 0, (int64_t)length, &writes);
    qa_qvm_read_source_player_state(&view, profile, &out->clients[i].state);
    qa_qvm_memory_writes_destroy(&writes);
    free(bytes);
    qa_save_reader_destroy(&row);
  }
  qa_save_reader_destroy(&clients);
  qa_save_reader entities = field(reader, "entities");
  size_t entity_count = qa_save_reader_list_count(&entities);
  out->entities = xmalloc(entity_count * sizeof *out->entities);
  out->entity_count = entity_count;
  for (size_t i = 0; i < entity_count; i++) {
    qa_save_reader row = item_at(&entities, i);
    qa_save_reader state_reader = field(&row, "state");
    size_t length = 0;
    const uint8_t *borrowed = qa_save_reader_bytes(&state_reader, &length);
    if ((int64_t)length != entity_bytes) qa_save_reader_fail(&row, "invalid source entity bytes");
    uint8_t *bytes = xmalloc(length);
    memcpy(bytes, borrowed, length);
    qa_save_reader_destroy(&state_reader);
    qa_save_reader actor_reader = field(&row, "actor");
    out->entities[i].actor = resolve(resolve_ctx, qa_read_saved_actor(&actor_reader));
    qa_save_reader_destroy(&actor_reader);
    qa_save_reader owned_reader = field(&row, "owned");
    out->entities[i].owned = qa_save_reader_boolean(&owned_reader);
    qa_save_reader_destroy(&owned_reader);
    qa_save_reader linked_reader = field(&row, "linked");
    out->entities[i].linked = qa_save_reader_boolean(&linked_reader);
    qa_save_reader_destroy(&linked_reader);
    qa_save_reader flags_reader = field(&row, "serverFlags");
    out->entities[i].server_flags = (int64_t)qa_save_reader_integer(&flags_reader);
    qa_save_reader_destroy(&flags_reader);
    qa_save_reader single_reader = field(&row, "singleClient");
    out->entities[i].single_client = (int64_t)qa_save_reader_integer(&single_reader);
    qa_save_reader_destroy(&single_reader);
    qa_save_reader bounds_reader = field(&row, "bounds");
    out->entities[i].bounds = qa_read_bounds(&bounds_reader);
    qa_save_reader_destroy(&bounds_reader);
    qa_qvm_memory_writes writes;
    qa_qvm_memory_writes_init(&writes, bytes, length, NULL, NULL);
    qa_qvm_writable_view view;
    qa_qvm_writable_view_init(&view, bytes, length, 0, (int64_t)length, &writes);
    read_entity_fields(&view, profile, &out->entities[i].state);
    qa_qvm_memory_writes_destroy(&writes);
    free(bytes);
    qa_save_reader_destroy(&row);
  }
  qa_save_reader_destroy(&entities);
  qa_save_reader commands = field(reader, "commands");
  size_t command_count = qa_save_reader_list_count(&commands);
  out->commands = xmalloc(command_count * sizeof *out->commands);
  out->command_count = command_count;
  for (size_t i = 0; i < command_count; i++) {
    qa_save_reader row = item_at(&commands, i);
    memset(&out->commands[i], 0, sizeof out->commands[i]);
    qa_save_reader sequence = field(&row, "sequence");
    out->commands[i].sequence = (int64_t)qa_save_reader_integer_min(&sequence, 0);
    qa_save_reader_destroy(&sequence);
    qa_save_reader text = field(&row, "text");
    out->commands[i].text = qa_save_reader_string(&text);
    qa_save_reader_destroy(&text);
    qa_save_reader recipient = field(&row, "recipient");
    if (!qa_save_reader_is_null(&recipient)) {
      out->commands[i].has_recipient = true;
      out->commands[i].recipient_storage = resolve(resolve_ctx, qa_read_saved_actor(&recipient));
      out->commands[i].recipient = &out->commands[i].recipient_storage;
    }
    qa_save_reader_destroy(&recipient);
    qa_save_reader_destroy(&row);
  }
  qa_save_reader_destroy(&commands);
}

void qa_qvm_mod_scene_publication_free(qa_qvm_cp_mod_scene_publication *scene) {
  qa_q3_source_game_state_record_destroy(&scene->game_state);
  free(scene->clients);
  free(scene->entities);
  free(scene->commands);
  memset(scene, 0, sizeof *scene);
}
