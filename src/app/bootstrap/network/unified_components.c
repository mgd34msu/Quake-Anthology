/* Port of src/app/bootstrap/network/unified-components.ts.
 * GPL-2.0-or-later. */
#include "qa/app/bootstrap/network/unified_components.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/network/unified_frame_values.h"
#include "qa/app/bootstrap/network/unified_native_components.h"
#include "qa/compat/qvm/player_record.h"
#include "qa/foundation/support.h"
#include "qa/persistence/mods.h"

/* Source entity read/write are the landed entity_record.h
 * functions; adapt writable views to qa_qvmx_view. */
static void read_entity_fields(const qa_qvm_writable_view *view, qa_qvm_abi_profile profile,
                               qa_qvm_entity_state_fields *out) {
  qa_qvmx_view raw = {view->bytes + view->start, (size_t)view->length};
  qa_qvm_entity_state state = qa_qvm_read_source_entity_state(raw, profile);
  qa_qvm_get_entity_state_fields(&state, out);
  qa_qvm_entity_state_destroy(&state);
}
static void write_entity_fields(qa_qvm_writable_view *view, const qa_qvm_entity_state_fields *state,
                                qa_qvm_abi_profile profile) {
  qa_qvmx_view raw = {view->bytes + view->start, (size_t)view->length};
  qa_qvm_write_source_entity_state(raw, state, profile);
}
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
static qa_save_reader field_of(const qa_save_reader *reader, const char *name) {
  qa_save_reader out;
  if (!qa_save_reader_field(reader, name, &out)) qa_range_error("out of memory");
  return out;
}
static double bounded_int(const qa_save_reader *reader, double minimum, double maximum) {
  double value = qa_save_reader_integer_min(reader, minimum);
  if (value != trunc(value) || fabs(value) > 9007199254740991.0 || value > maximum)
    qa_save_reader_fail(reader, "component integer exceeds its range");
  return value;
}
qa_presentation_owner qa_app_unified_read_component_owner(const qa_save_reader *reader) {
  qa_presentation_owner owner;
  qa_save_reader provider = field_of(reader, "provider");
  owner.provider = qa_namespaced(&provider);
  qa_save_reader_destroy(&provider);
  qa_save_reader generation = field_of(reader, "generation");
  owner.generation = bounded_int(&generation, 1, 9007199254740991.0);
  qa_save_reader_destroy(&generation);
  return owner;
}
static qa_save_value *object(void) {
  qa_save_value *value = qa_save_value_object();
  if (!value) qa_range_error("out of memory");
  return value;
}
static void set(qa_save_value *target, const char *key, qa_save_value *child) {
  if (!child || !qa_save_value_set(target, key, child)) qa_range_error("out of memory");
}
static void push(qa_save_value *target, qa_save_value *child) {
  if (!child || !qa_save_value_push(target, child)) qa_range_error("out of memory");
}
static qa_save_value *clone_value(const qa_save_value *value) {
  if (!value) return qa_save_value_undefined();
  qa_save_value *out = NULL;
  switch (value->kind) {
    case QA_SAVE_UNDEFINED: out = qa_save_value_undefined(); break;
    case QA_SAVE_NULL: out = qa_save_value_null(); break;
    case QA_SAVE_BOOLEAN: out = qa_save_value_boolean(value->boolean); break;
    case QA_SAVE_NUMBER: out = qa_save_value_number(value->number); break;
    case QA_SAVE_BIGINT: out = qa_save_value_bigint_text(value->text); break;
    case QA_SAVE_STRING: out = qa_save_value_string(value->text); break;
    case QA_SAVE_BYTES: out = qa_save_value_bytes(value->bytes, value->byte_length); break;
    case QA_SAVE_ARRAY:
      out = qa_save_value_array();
      if (out) for (size_t i = 0; i < value->item_count; i++) push(out, clone_value(value->items[i]));
      break;
    case QA_SAVE_OBJECT:
      out = qa_save_value_object();
      if (out) for (size_t i = 0; i < value->entry_count; i++)
        set(out, value->entries[i].key, clone_value(value->entries[i].value));
      break;
  }
  if (!out) qa_range_error("out of memory");
  return out;
}
static qa_save_value *write_owner(qa_presentation_owner owner) {
  qa_save_value *out = object();
  set(out, "provider", qa_save_value_string(owner.provider));
  set(out, "generation", qa_save_value_number(owner.generation));
  return out;
}
static const uint8_t *exact_bytes(const qa_save_reader *reader, size_t length) {
  size_t actual = 0;
  const uint8_t *bytes = qa_save_reader_bytes(reader, &actual);
  if (actual != length) qa_save_reader_fail(reader, "invalid original source record extent");
  return bytes;
}
static qa_save_value *write_state(const qa_q3_source_game_state_record *value) {
  qa_save_value *out = object();
  qa_save_value *offsets = qa_save_value_array();
  if (!offsets) qa_range_error("out of memory");
  for (size_t i = 0; i < 1024; i++)
    push(offsets, qa_save_value_number(value->string_offsets[i]));
  set(out, "stringOffsets", offsets);
  set(out, "stringData", qa_save_value_bytes(value->string_data, 16000));
  set(out, "dataCount", qa_save_value_number((double)value->data_count));
  return out;
}
static qa_q3_source_game_state_record read_state(const qa_save_reader *reader) {
  qa_save_reader count_reader = field_of(reader, "dataCount");
  double count = bounded_int(&count_reader, 0, 16000);
  qa_save_reader_destroy(&count_reader);
  qa_save_reader data_reader = field_of(reader, "stringData");
  const uint8_t *data = exact_bytes(&data_reader, 16000);
  qa_save_reader offsets_reader = field_of(reader, "stringOffsets");
  size_t offset_count = qa_save_reader_list_count(&offsets_reader);
  double maximum = count > 0 ? count - 1 : 0;
  int32_t *offsets = xmalloc(offset_count * sizeof *offsets);
  for (size_t i = 0; i < offset_count; i++) {
    qa_save_reader item;
    if (!qa_save_reader_item(&offsets_reader, i, &item)) qa_range_error("out of memory");
    offsets[i] = (int32_t)bounded_int(&item, 0, maximum);
    qa_save_reader_destroy(&item);
  }
  qa_save_reader_destroy(&offsets_reader);
  if (offset_count != 1024 || data[0] != 0)
    qa_save_reader_fail(reader, "invalid original gameState_t");
  for (size_t i = 0; i < offset_count; i++) {
    if (offsets[i] == 0) continue;
    size_t at = (size_t)offsets[i];
    bool terminated = false;
    for (size_t j = at; j < (size_t)count; j++)
      if (data[j] == 0) { terminated = true; break; }
    if (!terminated) qa_save_reader_fail(reader, "unterminated original configstring");
  }
  qa_q3_source_game_state_record out;
  out.string_offsets = offsets;
  out.string_data = xmalloc(16000);
  memcpy(out.string_data, data, 16000);
  out.data_count = (int64_t)count;
  qa_save_reader_destroy(&data_reader);
  return out;
}
static qa_save_value *write_commands(const qa_qvm_cp_scene_command *commands, size_t count) {
  qa_save_value *list = qa_save_value_array();
  if (!list) qa_range_error("out of memory");
  for (size_t i = 0; i < count; i++) {
    qa_save_value *entry = object();
    set(entry, "sequence", qa_save_value_number((double)commands[i].sequence));
    qa_save_value *args = qa_save_value_array();
    if (!args) qa_range_error("out of memory");
    for (size_t j = 0; j < commands[i].argument_count; j++)
      push(args, qa_save_value_string(commands[i].arguments[j]));
    set(entry, "arguments", args);
    push(list, entry);
  }
  return list;
}
static size_t read_commands(const qa_save_reader *reader, qa_qvm_cp_scene_command **out) {
  size_t count = qa_save_reader_list_count(reader);
  qa_qvm_cp_scene_command *commands = xmalloc(count * sizeof *commands);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader row;
    if (!qa_save_reader_item(reader, i, &row)) qa_range_error("out of memory");
    qa_save_reader sequence = field_of(&row, "sequence");
    commands[i].sequence = (int64_t)bounded_int(&sequence, 1, 2147483647);
    qa_save_reader_destroy(&sequence);
    qa_save_reader args = field_of(&row, "arguments");
    size_t arg_count = qa_save_reader_list_count(&args);
    const char **arguments = xmalloc(arg_count * sizeof *arguments);
    for (size_t j = 0; j < arg_count; j++) {
      qa_save_reader item;
      if (!qa_save_reader_item(&args, j, &item)) qa_range_error("out of memory");
      const char *text = qa_save_reader_string(&item);
      if (strlen(text) > 8192) qa_save_reader_fail(&item, "invalid component command argument");
      arguments[j] = copy_text(text);
      qa_save_reader_destroy(&item);
    }
    commands[i].arguments = (const char *const *)arguments;
    commands[i].argument_count = arg_count;
    qa_save_reader_destroy(&args);
    qa_save_reader_destroy(&row);
  }
  bool invalid = count > 64;
  for (size_t i = 0; i < count && !invalid; i++) {
    if (commands[i].argument_count > 128) invalid = true;
    if (i > 0 && commands[i].sequence != commands[i - 1].sequence + 1) invalid = true;
  }
  if (invalid) qa_save_reader_fail(reader, "invalid original reliable command window");
  *out = commands;
  return count;
}
static void commands_free(qa_qvm_cp_scene_command *commands, size_t count) {
  for (size_t i = 0; i < count; i++) {
    for (size_t j = 0; j < commands[i].argument_count; j++)
      free((void *)commands[i].arguments[j]);
    free((void *)commands[i].arguments);
  }
  free(commands);
}
static void check_owners(const qa_save_reader *reader,
    const char *const *providers, size_t count, const char *message) {
  if (count > 256) qa_save_reader_fail(reader, "%s", message);
  for (size_t i = 0; i < count; i++)
    for (size_t j = i + 1; j < count; j++)
      if (strcmp(providers[i], providers[j]) == 0)
        qa_save_reader_fail(reader, "%s", message);
}
static qa_save_value *write_identity(const qa_app_unified_component_identity *identity,
    bool with_abi_runtime) {
  qa_save_value *out = object();
  set(out, "owner", write_owner(identity->owner));
  set(out, "identity", clone_value(identity->identity_wire));
  set(out, "generation", qa_save_value_number(identity->generation));
  if (with_abi_runtime) {
    set(out, "abi", qa_save_value_string(identity->abi));
    set(out, "runtime", qa_save_value_string(identity->runtime));
  }
  return out;
}
static qa_app_unified_component_identity read_identity(const qa_save_reader *reader,
    bool with_abi_runtime) {
  qa_app_unified_component_identity identity;
  memset(&identity, 0, sizeof identity);
  qa_save_reader owner = field_of(reader, "owner");
  identity.owner = qa_app_unified_read_component_owner(&owner);
  qa_save_reader_destroy(&owner);
  qa_save_reader wire = field_of(reader, "identity");
  identity.identity = qa_read_mod_identity(&wire);
  identity.identity_wire = clone_value(wire.value);
  qa_save_reader_destroy(&wire);
  qa_save_reader generation = field_of(reader, "generation");
  identity.generation = bounded_int(&generation, 0, 9007199254740991.0);
  qa_save_reader_destroy(&generation);
  if (with_abi_runtime) {
    static const char *abis[] = {"q3-modern", "q3-1.16n-base"};
    static const char *runtimes[] = {"qvm-scene", "qvm-player-events"};
    qa_save_reader abi = field_of(reader, "abi");
    identity.abi = qa_save_reader_choice_string(&abi, abis, 2);
    qa_save_reader_destroy(&abi);
    qa_save_reader runtime = field_of(reader, "runtime");
    identity.runtime = qa_save_reader_choice_string(&runtime, runtimes, 2);
    qa_save_reader_destroy(&runtime);
  }
  return identity;
}
qa_save_value *qa_app_unified_write_component_update(const qa_app_unified_component_update *update) {
  qa_save_value *out = object();
  set(out, "revision", qa_save_value_number(update->revision));
  set(out, "native", qa_app_unified_write_native_states(update->native, update->native_count));
  qa_save_value *sources = qa_save_value_array();
  if (!sources) qa_range_error("out of memory");
  for (size_t i = 0; i < update->source_count; i++) {
    const qa_app_unified_component_state *source = &update->sources[i];
    qa_save_value *entry = write_identity(&source->identity, true);
    set(entry, "gameStateRevision", qa_save_value_number(source->game_state_revision));
    if (source->has_game_state) set(entry, "gameState", write_state(&source->game_state));
    else set(entry, "gameState", qa_save_value_null());
    set(entry, "commandBase", qa_save_value_number(source->command_base));
    set(entry, "commands", write_commands(source->commands, source->command_count));
    push(sources, entry);
  }
  set(out, "sources", sources);
  return out;
}
qa_app_unified_component_update qa_app_unified_read_component_update(const qa_save_reader *reader) {
  qa_app_unified_component_update update;
  memset(&update, 0, sizeof update);
  qa_save_reader sources_reader = field_of(reader, "sources");
  size_t count = qa_save_reader_list_count(&sources_reader);
  qa_app_unified_component_state *sources = xmalloc(count * sizeof *sources);
  const char **providers = xmalloc((count + 256) * sizeof *providers);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader source;
    if (!qa_save_reader_item(&sources_reader, i, &source)) qa_range_error("out of memory");
    memset(&sources[i], 0, sizeof sources[i]);
    sources[i].identity = read_identity(&source, true);
    providers[i] = sources[i].identity.owner.provider;
    qa_save_reader revision = field_of(&source, "gameStateRevision");
    sources[i].game_state_revision = bounded_int(&revision, 0, 9007199254740991.0);
    qa_save_reader_destroy(&revision);
    qa_save_reader state = field_of(&source, "gameState");
    if (!qa_save_reader_is_null(&state)) {
      sources[i].has_game_state = true;
      sources[i].game_state = read_state(&state);
    }
    qa_save_reader_destroy(&state);
    qa_save_reader base = field_of(&source, "commandBase");
    sources[i].command_base = bounded_int(&base, 0, 2147483647);
    qa_save_reader_destroy(&base);
    qa_save_reader commands = field_of(&source, "commands");
    sources[i].command_count = read_commands(&commands, &sources[i].commands);
    qa_save_reader_destroy(&commands);
    qa_save_reader_destroy(&source);
  }
  if (count > 256) qa_save_reader_fail(reader, "invalid component owner set");
  check_owners(reader, providers, count, "invalid component owner set");
  qa_save_reader native_reader = field_of(reader, "native");
  update.native_count = qa_app_unified_read_native_states(&native_reader, &update.native);
  qa_save_reader_destroy(&native_reader);
  for (size_t i = 0; i < update.native_count; i++)
    providers[count + i] = update.native[i].identity.owner.provider;
  check_owners(reader, providers, count + update.native_count, "invalid component owner set");
  free(providers);
  qa_save_reader revision = field_of(reader, "revision");
  update.revision = bounded_int(&revision, 1, 9007199254740991.0);
  qa_save_reader_destroy(&revision);
  update.sources = sources;
  update.source_count = count;
  qa_save_reader_destroy(&sources_reader);
  return update;
}
void qa_app_unified_component_update_free(qa_app_unified_component_update *update) {
  for (size_t i = 0; i < update->source_count; i++) {
    qa_save_value_free((qa_save_value *)update->sources[i].identity.identity_wire);
    if (update->sources[i].has_game_state)
      qa_q3_source_game_state_record_destroy(&update->sources[i].game_state);
    commands_free(update->sources[i].commands, update->sources[i].command_count);
  }
  free(update->sources);
  qa_app_unified_native_states_free(update->native, update->native_count);
  memset(update, 0, sizeof *update);
}
static qa_qvm_writable_view record_view(uint8_t *bytes, size_t length) {
  qa_qvm_writable_view view = {bytes, length, 0, (int64_t)length, NULL};
  return view;
}
static void bind_slots(qa_q3_player_state *state, double slots[64]) {
  state->stats.values = slots;
  state->stats.count = 16;
  state->persistent.values = slots + 16;
  state->persistent.count = 16;
  state->powerups.values = slots + 32;
  state->powerups.count = 16;
  state->ammo.values = slots + 48;
  state->ammo.count = 16;
}
static uint8_t *encode_player_state(qa_app_unified_snapshot *snapshot, qa_qvm_abi_profile abi,
    size_t *out_length) {
  int length = qa_qvm_player_state_bytes(abi);
  uint8_t *bytes = xmalloc((size_t)length);
  memset(bytes, 0, (size_t)length);
  qa_qvm_writable_view view = record_view(bytes, (size_t)length);
  qa_qvm_write_source_player_state(&view, &snapshot->player_state, abi);
  *out_length = (size_t)length;
  return bytes;
}
static qa_app_unified_snapshot decode_player_state(const qa_save_reader *snapshot_reader,
    qa_qvm_abi_profile abi) {
  qa_app_unified_snapshot out;
  memset(&out, 0, sizeof out);
  qa_save_reader state_reader = field_of(snapshot_reader, "playerState");
  int length = qa_qvm_player_state_bytes(abi);
  const uint8_t *bytes = exact_bytes(&state_reader, (size_t)length);
  uint8_t *copy = xmalloc((size_t)length);
  memcpy(copy, bytes, (size_t)length);
  qa_save_reader_destroy(&state_reader);
  bind_slots(&out.player_state, out.player_slots);
  qa_qvm_writable_view view = record_view(copy, (size_t)length);
  qa_qvm_read_source_player_state(&view, abi, &out.player_state);
  bind_slots(&out.player_state, out.player_slots);
  free(copy);
  qa_save_reader time_reader = field_of(snapshot_reader, "serverTime");
  out.server_time = bounded_int(&time_reader, 0, 2147483647);
  qa_save_reader_destroy(&time_reader);
  return out;
}
qa_save_value *qa_app_unified_write_component_frames(const qa_app_unified_component_frames *frames) {
  qa_save_value *out = object();
  set(out, "revision", qa_save_value_number(frames->revision));
  set(out, "native", qa_app_unified_write_native_frames(frames->native, frames->native_count));
  qa_save_value *sources = qa_save_value_array();
  if (!sources) qa_range_error("out of memory");
  for (size_t i = 0; i < frames->source_count; i++) {
    const qa_app_unified_component_frame *source = &frames->sources[i];
    size_t state_length = 0;
    qa_app_unified_snapshot snapshot_copy = source->snapshot;
    bind_slots(&snapshot_copy.player_state, snapshot_copy.player_slots);
    uint8_t *state_bytes = encode_player_state(&snapshot_copy, source->abi, &state_length);
    qa_save_value *entry = object();
    set(entry, "owner", write_owner(source->owner));
    set(entry, "generation", qa_save_value_number(source->generation));
    set(entry, "abi", qa_save_value_string(source->abi));
    set(entry, "viewer", qa_app_unified_wire_actor(source->viewer));
    set(entry, "clientNumber", qa_save_value_number(source->client_number));
    set(entry, "gameStateRevision", qa_save_value_number(source->game_state_revision));
    qa_save_value *snapshot = object();
    set(snapshot, "serverTime", qa_save_value_number(source->snapshot.server_time));
    set(snapshot, "playerState", qa_save_value_bytes(state_bytes, state_length));
    set(entry, "snapshot", snapshot);
    set(entry, "weaponPresented", qa_save_value_boolean(source->weapon_presented));
    qa_save_value *bindings = qa_save_value_array();
    if (!bindings) qa_range_error("out of memory");
    for (size_t j = 0; j < source->binding_count; j++) {
      qa_save_value *binding = object();
      set(binding, "actor", qa_app_unified_wire_actor(source->bindings[j].actor));
      set(binding, "slot", qa_save_value_number((double)source->bindings[j].slot));
      set(binding, "owned", qa_save_value_boolean(source->bindings[j].owned));
      push(bindings, binding);
    }
    set(entry, "bindings", bindings);
    if (!source->has_scene) {
      set(entry, "scene", qa_save_value_null());
    } else {
      qa_save_value *scene = object();
      set(scene, "revision", qa_save_value_number(source->scene.revision));
      qa_save_value *scene_snapshot = object();
      const qa_qvm_source_snapshot *snap = &source->scene.snapshot;
      set(scene_snapshot, "serverTime", qa_save_value_number(snap->server_time));
      set(scene_snapshot, "flags", qa_save_value_number(snap->flags));
      static const uint8_t zero_mask[32] = {0};
      set(scene_snapshot, "areaMask",
          qa_save_value_bytes(snap->area_mask != NULL ? snap->area_mask : zero_mask, 32));
      set(scene_snapshot, "playerState", qa_save_value_bytes(state_bytes, state_length));
      qa_save_value *entities = qa_save_value_array();
      if (!entities) qa_range_error("out of memory");
      int entity_length = qa_qvm_entity_state_bytes_for(source->abi);
      for (size_t j = 0; j < snap->entity_count; j++) {
        uint8_t *record = xmalloc((size_t)entity_length);
        memset(record, 0, (size_t)entity_length);
        qa_qvm_writable_view view = record_view(record, (size_t)entity_length);
        write_entity_fields(&view, &snap->entities[j], source->abi);
        push(entities, qa_save_value_bytes(record, (size_t)entity_length));
        free(record);
      }
      set(scene_snapshot, "entities", entities);
      set(scene_snapshot, "serverCommandSequence",
          qa_save_value_number(snap->server_command_sequence));
      set(scene, "snapshot", scene_snapshot);
      set(entry, "scene", scene);
    }
    free(state_bytes);
    push(sources, entry);
  }
  set(out, "sources", sources);
  return out;
}
qa_app_unified_component_frames qa_app_unified_read_component_frames(
    const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  qa_app_unified_component_frames frames;
  memset(&frames, 0, sizeof frames);
  qa_save_reader sources_reader = field_of(reader, "sources");
  size_t count = qa_save_reader_list_count(&sources_reader);
  qa_app_unified_component_frame *sources = xmalloc(count * sizeof *sources);
  const char **providers = xmalloc((count + 256) * sizeof *providers);
  static const char *abis[] = {"q3-modern", "q3-1.16n-base"};
  for (size_t i = 0; i < count; i++) {
    qa_save_reader source;
    if (!qa_save_reader_item(&sources_reader, i, &source)) qa_range_error("out of memory");
    memset(&sources[i], 0, sizeof sources[i]);
    qa_save_reader abi_reader = field_of(&source, "abi");
    sources[i].abi = qa_save_reader_choice_string(&abi_reader, abis, 2);
    qa_save_reader_destroy(&abi_reader);
    qa_save_reader snapshot_reader = field_of(&source, "snapshot");
    sources[i].snapshot = decode_player_state(&snapshot_reader, sources[i].abi);
    qa_save_reader bindings_reader = field_of(&source, "bindings");
    size_t binding_count = qa_save_reader_list_count(&bindings_reader);
    qa_qvm_scene_actor *bindings = xmalloc(binding_count * sizeof *bindings);
    for (size_t j = 0; j < binding_count; j++) {
      qa_save_reader binding;
      if (!qa_save_reader_item(&bindings_reader, j, &binding)) qa_range_error("out of memory");
      qa_save_reader actor_reader = field_of(&binding, "actor");
      bindings[j].actor = qa_app_unified_read_actor(&actor_reader, identity);
      qa_save_reader_destroy(&actor_reader);
      qa_save_reader slot = field_of(&binding, "slot");
      bindings[j].slot = (int64_t)bounded_int(&slot, 0, 1023);
      qa_save_reader_destroy(&slot);
      qa_save_reader owned = field_of(&binding, "owned");
      bindings[j].owned = qa_save_reader_boolean(&owned);
      qa_save_reader_destroy(&owned);
      qa_save_reader_destroy(&binding);
    }
    if (binding_count > 1024) qa_save_reader_fail(&source, "duplicate source actor slots");
    for (size_t a = 0; a < binding_count; a++)
      for (size_t b = a + 1; b < binding_count; b++)
        if (bindings[a].slot == bindings[b].slot)
          qa_save_reader_fail(&source, "duplicate source actor slots");
    sources[i].bindings = bindings;
    sources[i].binding_count = binding_count;
    qa_save_reader_destroy(&bindings_reader);
    qa_save_reader scene_reader = field_of(&source, "scene");
    if (!qa_save_reader_is_null(&scene_reader)) {
      sources[i].has_scene = true;
      qa_save_reader state = field_of(&scene_reader, "snapshot");
      qa_save_reader entities_reader = field_of(&state, "entities");
      size_t entity_count = qa_save_reader_list_count(&entities_reader);
      qa_qvm_entity_state_fields *entities = xmalloc(entity_count * sizeof *entities);
      int entity_length = qa_qvm_entity_state_bytes_for(sources[i].abi);
      for (size_t j = 0; j < entity_count; j++) {
        qa_save_reader item;
        if (!qa_save_reader_item(&entities_reader, j, &item)) qa_range_error("out of memory");
        const uint8_t *bytes = exact_bytes(&item, (size_t)entity_length);
        uint8_t *copy = xmalloc((size_t)entity_length);
        memcpy(copy, bytes, (size_t)entity_length);
        qa_qvm_writable_view view = record_view(copy, (size_t)entity_length);
        read_entity_fields(&view, sources[i].abi, &entities[j]);
        free(copy);
        qa_save_reader_destroy(&item);
      }
      if (entity_count > 256) qa_save_reader_fail(&scene_reader, "invalid visible source entity set");
      for (size_t a = 0; a < entity_count; a++)
        for (size_t b = a + 1; b < entity_count; b++)
          if (entities[a].number == entities[b].number)
            qa_save_reader_fail(&scene_reader, "invalid visible source entity set");
      qa_save_reader_destroy(&entities_reader);
      qa_qvm_source_snapshot *snap = &sources[i].scene.snapshot;
      memset(snap, 0, sizeof *snap);
      qa_save_reader v = field_of(&scene_reader, "revision");
      sources[i].scene.revision = bounded_int(&v, 0, 9007199254740991.0);
      qa_save_reader_destroy(&v);
      v = field_of(&state, "serverTime");
      snap->server_time = (int32_t)bounded_int(&v, 0, 2147483647);
      qa_save_reader_destroy(&v);
      v = field_of(&state, "flags");
      snap->flags = (int32_t)bounded_int(&v, 0, 255);
      qa_save_reader_destroy(&v);
      v = field_of(&state, "areaMask");
      uint8_t *area_mask = xmalloc(32);
      memcpy(area_mask, exact_bytes(&v, 32), 32);
      snap->area_mask = area_mask;
      snap->area_mask_length = 32;
      qa_save_reader_destroy(&v);
      snap->player_state = sources[i].snapshot.player_state;
      double *snap_slots = xmalloc(64 * sizeof *snap_slots);
      memcpy(snap_slots, sources[i].snapshot.player_slots, 64 * sizeof *snap_slots);
      bind_slots(&snap->player_state, snap_slots);
      snap->entities = entities;
      snap->entity_count = entity_count;
      v = field_of(&state, "serverCommandSequence");
      snap->server_command_sequence = (int32_t)bounded_int(&v, 0, 2147483647);
      qa_save_reader_destroy(&v);
      qa_save_reader_destroy(&state);
    }
    qa_save_reader_destroy(&scene_reader);
    qa_save_reader owner = field_of(&source, "owner");
    sources[i].owner = qa_app_unified_read_component_owner(&owner);
    qa_save_reader_destroy(&owner);
    providers[i] = sources[i].owner.provider;
    qa_save_reader v = field_of(&source, "generation");
    sources[i].generation = bounded_int(&v, 0, 9007199254740991.0);
    qa_save_reader_destroy(&v);
    v = field_of(&source, "viewer");
    sources[i].viewer = qa_app_unified_read_actor(&v, identity);
    qa_save_reader_destroy(&v);
    v = field_of(&source, "clientNumber");
    sources[i].client_number = bounded_int(&v, 0, 1023);
    qa_save_reader_destroy(&v);
    v = field_of(&source, "gameStateRevision");
    sources[i].game_state_revision = bounded_int(&v, 0, 9007199254740991.0);
    qa_save_reader_destroy(&v);
    v = field_of(&source, "weaponPresented");
    sources[i].weapon_presented = qa_save_reader_boolean(&v);
    qa_save_reader_destroy(&v);
    qa_save_reader_destroy(&snapshot_reader);
    qa_save_reader_destroy(&source);
  }
  if (count > 256) qa_save_reader_fail(reader, "invalid component frame owner set");
  check_owners(reader, providers, count, "invalid component frame owner set");
  qa_save_reader native_reader = field_of(reader, "native");
  frames.has_native = !qa_save_reader_is_undefined(&native_reader);
  frames.native_count = qa_app_unified_read_native_frames(&native_reader, identity, &frames.native);
  qa_save_reader_destroy(&native_reader);
  for (size_t i = 0; i < frames.native_count; i++)
    providers[count + i] = frames.native[i].owner.provider;
  check_owners(reader, providers, count + frames.native_count, "invalid component owner set");
  free(providers);
  qa_save_reader revision = field_of(reader, "revision");
  frames.revision = bounded_int(&revision, 0, 9007199254740991.0);
  qa_save_reader_destroy(&revision);
  frames.sources = sources;
  frames.source_count = count;
  qa_save_reader_destroy(&sources_reader);
  return frames;
}
void qa_app_unified_component_frames_free(qa_app_unified_component_frames *frames) {
  for (size_t i = 0; i < frames->source_count; i++) {
    free(frames->sources[i].bindings);
    qa_qvm_source_snapshot_free(&frames->sources[i].scene.snapshot);
  }
  free(frames->sources);
  qa_app_unified_native_frames_free(frames->native, frames->native_count);
  memset(frames, 0, sizeof *frames);
}
