/* See include/qa/compat/qvm/mod_presentation.h. GPL-2.0-or-later. */
#include "qa/compat/qvm/body_scope.h"
#include "qa/compat/qvm/mod_presentation.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/compat/qvm/abi.h"
#include "qa/compat/qvm/operations.h"
#include "qa/compat/qvm/player_record.h"
#include "qa/core/math.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/persistence/execution.h"
#include "qa/persistence/save_image.h"

/* Source entity read/write are the landed entity_record.h
 * functions; adapt writable views to qa_qvmx_view. */
static void write_entity_fields(qa_qvm_writable_view *view, const qa_qvm_entity_state_fields *state,
                                qa_qvm_abi_profile profile) {
  qa_qvmx_view raw = {view->bytes + view->start, (size_t)view->length};
  qa_qvm_write_source_entity_state(raw, state, profile);
}

#define QVM_GAME_STATE_BYTES 20100
#define QVM_MAX_PRIVATE_ARGUMENT_WORDS 62

static void *xmalloc(size_t size) {
  void *result = malloc(size == 0 ? 1 : size);
  if (result == NULL) qa_range_error("QVM memory allocation failed");
  return result;
}

static bool matches_module(qa_module_identity first, qa_module_identity second) {
  return strcmp(first.id, second.id) == 0 && strcmp(first.artifact_path, second.artifact_path) == 0 &&
         strcmp(first.digest, second.digest) == 0 && strcmp(first.revision, second.revision) == 0;
}

static void check_int32(double value) {
  if (!isfinite(value) || value != trunc(value) || value < -2147483648.0 || value > 2147483647.0)
    qa_range_error("Source presentation value exceeds int32");
}

static bool safe_integer(double value) {
  return isfinite(value) && value == trunc(value) && value >= -9007199254740991.0 && value <= 9007199254740991.0;
}

static const qa_qvm_instruction *insn_at(const qa_qvmy_artifact *artifact, double index) {
  int64_t at = (int64_t)index;
  if (index != trunc(index) || at < 0 || (uint64_t)at >= artifact->instruction_count) return NULL;
  return &artifact->instructions[at];
}

void qa_qvm_validate_mod_presentation(const qa_qvmy_artifact *artifact, qa_module_identity source,
                                      const qa_qvm_mod_presentation_declaration *declaration) {
  const qa_qvm_presentation_base *base = declaration->is_scene ? &declaration->as.scene->base
                                                              : &declaration->as.player_events->base;
  const qa_qvm_presentation_program *gameplay = &base->gameplay;
  const qa_qvm_presentation_program *cgame = &base->cgame;
  const char *artifact_profile = artifact->has_abi_profile ? artifact->abi_profile : "q3-modern";
  if (strcmp(artifact->role, "cgame") != 0 || strcmp(artifact->module.artifact_path, cgame->path) != 0 ||
      strcmp(artifact->module.digest, cgame->digest) != 0 || strcmp(artifact_profile, cgame->abi_profile) != 0 ||
      strcmp(source.artifact_path, gameplay->path) != 0 || strcmp(source.digest, gameplay->digest) != 0)
    qa_range_error("Source presentation differs from its declared gameplay/cgame artifacts");
  if (strcmp(gameplay->abi_profile, cgame->abi_profile) != 0)
    qa_range_error("Source presentation requires matching player-state ABI profiles");
  int64_t end = artifact->data_length + artifact->literal_length + artifact->bss_length;
  struct range_check { double address; double size; };
  /* Common storage checks, branching on the storage variant. */
  double game_state = 0, centity_address = 0, centity_stride = 0, centity_capacity = 0, centity_state = 0;
  const double *time_addresses = NULL, *frame_time = NULL, *view_origin = NULL, *view_angles = NULL, *view_axis = NULL;
  size_t time_count = 0, frame_time_count = 0, origin_count = 0, angle_count = 0, axis_count = 0;
  if (declaration->is_scene) {
    const qa_qvm_scene_storage *storage = &declaration->as.scene->storage;
    game_state = storage->game_state;
    time_addresses = storage->time.values; time_count = storage->time.count;
    frame_time = storage->frame_time.values; frame_time_count = storage->frame_time.count;
    view_origin = storage->view_origin.values; origin_count = storage->view_origin.count;
    view_angles = storage->view_angles.values; angle_count = storage->view_angles.count;
    view_axis = storage->view_axis.values; axis_count = storage->view_axis.count;
    centity_address = storage->centities.address; centity_stride = storage->centities.stride;
    centity_capacity = storage->centities.capacity; centity_state = storage->centities.state;
  } else {
    const qa_qvm_player_event_storage *storage = &declaration->as.player_events->storage;
    game_state = storage->game_state;
    time_addresses = storage->time.values; time_count = storage->time.count;
    frame_time = storage->frame_time.values; frame_time_count = storage->frame_time.count;
    view_origin = storage->view_origin.values; origin_count = storage->view_origin.count;
    view_angles = storage->view_angles.values; angle_count = storage->view_angles.count;
    view_axis = storage->view_axis.values; axis_count = storage->view_axis.count;
    centity_address = storage->centities.address; centity_stride = storage->centities.stride;
    centity_capacity = storage->centities.capacity; centity_state = storage->centities.state;
  }
  struct range_check ranges[512];
  size_t range_count = 0;
  ranges[range_count++] = (struct range_check){game_state, QVM_GAME_STATE_BYTES};
  for (size_t i = 0; i < time_count && range_count < 512; i++) ranges[range_count++] = (struct range_check){time_addresses[i], 4};
  for (size_t i = 0; i < frame_time_count && range_count < 512; i++) ranges[range_count++] = (struct range_check){frame_time[i], 4};
  for (size_t i = 0; i < origin_count && range_count < 512; i++) ranges[range_count++] = (struct range_check){view_origin[i], 12};
  for (size_t i = 0; i < angle_count && range_count < 512; i++) ranges[range_count++] = (struct range_check){view_angles[i], 12};
  for (size_t i = 0; i < axis_count && range_count < 512; i++) ranges[range_count++] = (struct range_check){view_axis[i], 36};
  ranges[range_count++] = (struct range_check){centity_address, centity_stride * centity_capacity};
  for (size_t i = 0; i < range_count; i++) {
    if (!safe_integer(ranges[i].address) || ranges[i].address < 0 || !safe_integer(ranges[i].size) ||
        ranges[i].size < 1 || ranges[i].address + ranges[i].size > (double)end)
      qa_range_error("Source presentation storage exceeds the original data image");
  }
  if (!safe_integer(centity_capacity) || centity_capacity < 1 || !safe_integer(centity_stride) ||
      !safe_integer(centity_state) || centity_state < 0 ||
      centity_state + qa_qvm_entity_state_bytes_for(cgame->abi_profile) > centity_stride)
    qa_range_error("Source presentation centity layout is invalid");
  if (!declaration->is_scene) {
    const qa_qvm_player_event_storage *storage = &declaration->as.player_events->storage;
    int ps_bytes = qa_qvm_player_state_bytes(cgame->abi_profile);
    int snapshot_bytes = qa_qvm_snapshot_bytes_for(cgame->abi_profile);
    struct range_check extra[] = {{storage->player_state, ps_bytes}, {storage->snapshot.address, snapshot_bytes}};
    for (int i = 0; i < 2; i++) {
      if (!safe_integer(extra[i].address) || extra[i].address < 0 || extra[i].address + extra[i].size > (double)end)
        qa_range_error("Source presentation storage exceeds the original data image");
    }
    for (size_t i = 0; i < storage->snapshot.pointers.count; i++) {
      double address = storage->snapshot.pointers.values[i];
      if (!safe_integer(address) || address < 0 || address + 4 > (double)end)
        qa_range_error("Source presentation storage exceeds the original data image");
    }
    if (storage->centities.origin < 0 || storage->centities.origin + 12 > centity_stride)
      qa_range_error("Source centity origin exceeds its record");
    if (storage->player_state < storage->snapshot.address + snapshot_bytes &&
        storage->snapshot.address < storage->player_state + ps_bytes)
      qa_range_error("Event projection state must be separate from the viewing player's snapshot");
  } else {
    const qa_qvm_scene_presentation *scene = declaration->as.scene;
    double sequence_address = scene->storage.server_command_sequence;
    if (!safe_integer(sequence_address) || sequence_address < 0 || sequence_address + 4 > (double)end)
      qa_range_error("Source presentation storage exceeds the original data image");
    double offsets[] = {scene->storage.centities.previous_event, scene->storage.centities.snapshot_time};
    for (int i = 0; i < 2; i++)
      if (!safe_integer(offsets[i]) || offsets[i] < 0 || offsets[i] + 4 > centity_stride)
        qa_range_error("Source centity event cursor exceeds its record");
    double entries[] = {scene->body.player.entry, scene->body.mesh.entry, scene->event_check.entry};
    for (int i = 0; i < 3; i++) {
      const qa_qvm_instruction *instruction = insn_at(artifact, entries[i]);
      if (instruction == NULL || instruction->opcode != QA_QVMY_OP_ENTER)
        qa_range_error("Source mesh scope has no original function entry");
    }
    double arguments[] = {scene->body.player.centity_argument, scene->body.mesh.entity_argument,
                          scene->body.mesh.state_argument, scene->event_check.centity_argument};
    for (int i = 0; i < 4; i++)
      if (arguments[i] != trunc(arguments[i]) || arguments[i] < 0 || arguments[i] >= QVM_MAX_PRIVATE_ARGUMENT_WORDS)
        qa_range_error("Source mesh scope has an invalid argument");
    if (scene->body.mesh.shader_offset != 112)
      qa_range_error("Source mesh shader field differs from the declared refEntity ABI");
    if (scene->snapshot_count == 0) qa_range_error("Source scene requires original snapshot processing");
  }
  size_t check_capacity = base->initialize_count + base->refresh_count + base->frame_count +
      (base->hud != NULL ? base->hud->frame_count : 0) +
      (!declaration->is_scene ? declaration->as.player_events->project_count + 1
                              : declaration->as.scene->snapshot_count);
  const qa_qvm_presentation_call **checks = malloc((check_capacity == 0 ? 1 : check_capacity) * sizeof *checks);
  bool *initializing_flags = malloc((check_capacity == 0 ? 1 : check_capacity) * sizeof *initializing_flags);
  if (checks == NULL || initializing_flags == NULL) qa_range_error("QVM memory allocation failed");
  size_t check_count = 0;
  for (size_t i = 0; i < base->initialize_count; i++) { checks[check_count] = &base->initialize[i]; initializing_flags[check_count++] = true; }
  for (size_t i = 0; i < base->refresh_count; i++) { checks[check_count] = &base->refresh[i]; initializing_flags[check_count++] = true; }
  for (size_t i = 0; i < base->frame_count; i++) { checks[check_count] = &base->frame[i]; initializing_flags[check_count++] = true; }
  if (base->hud != NULL)
    for (size_t i = 0; i < base->hud->frame_count; i++) { checks[check_count] = &base->hud->frame[i]; initializing_flags[check_count++] = true; }
  if (!declaration->is_scene) {
    for (size_t i = 0; i < declaration->as.player_events->project_count; i++) {
      checks[check_count] = &declaration->as.player_events->project[i];
      initializing_flags[check_count++] = false;
    }
    checks[check_count] = &declaration->as.player_events->event;
    initializing_flags[check_count++] = false;
  } else {
    for (size_t i = 0; i < declaration->as.scene->snapshot_count; i++) {
      checks[check_count] = &declaration->as.scene->snapshots[i];
      initializing_flags[check_count++] = true;
    }
  }
  for (size_t c = 0; c < check_count; c++) {
    const qa_qvm_presentation_call *call = checks[c];
    const qa_qvm_instruction *instruction = insn_at(artifact, call->entry);
    if (call->entry != trunc(call->entry) || instruction == NULL || instruction->opcode != QA_QVMY_OP_ENTER ||
        call->argument_count > QVM_MAX_PRIVATE_ARGUMENT_WORDS)
      qa_range_error("Source presentation call has no original function entry");
    for (size_t a = 0; a < call->argument_count; a++) {
      const qa_qvm_presentation_argument *argument = &call->arguments[a];
      if (strcmp(argument->kind, "address") == 0) {
        if (!safe_integer(argument->number_value) || argument->number_value < 0 ||
            argument->number_value + 1 > (double)end)
          qa_range_error("Source presentation storage exceeds the original data image");
      } else if (strcmp(argument->kind, "int32") == 0) {
        check_int32(argument->number_value);
      } else if (strcmp(argument->kind, "float32") == 0) {
        if (!isfinite((double)(float)argument->number_value))
          qa_range_error("Source presentation float exceeds its ABI");
      } else if (declaration->is_scene && strcmp(argument->kind, "source") == 0 &&
                 (strcmp(argument->source_value, "player-state") == 0 ||
                  strcmp(argument->source_value, "snapshot") == 0)) {
        qa_range_error("Scene presentation receives its records through original snapshot traps");
      } else if (initializing_flags[c] && strcmp(argument->kind, "source") == 0 &&
                 (strcmp(argument->source_value, "entity-state") == 0 ||
                  strcmp(argument->source_value, "centity") == 0 ||
                  strcmp(argument->source_value, "origin") == 0 ||
                  strcmp(argument->source_value, "event") == 0 ||
                  strcmp(argument->source_value, "parameter") == 0)) {
        qa_range_error("Source presentation initialization requires an event-independent context");
      }
    }
  }
  free(checks);
  free(initializing_flags);
}

typedef struct slot_actor {
  int64_t slot;
  qa_actor_id actor;
} slot_actor;

typedef struct snapshot_entry {
  int64_t number;
  qa_qvm_source_snapshot snapshot;
} snapshot_entry;

typedef struct command_entry {
  int64_t sequence;
  const char **arguments; /* owned array of borrowed strings */
  size_t argument_count;
} command_entry;

typedef struct scene_actor_entry {
  int64_t slot;
  qa_actor_id actor;
  bool owned;
} scene_actor_entry;

typedef struct body_mesh_ref {
  size_t part_index; /* index, not pointer: parts may realloc */
  int64_t site;
} body_mesh_ref;

typedef struct player_scope {
  qa_actor_id actor;
  int64_t state;
  qa_qvm_body_mesh *parts;
  size_t part_count;
  size_t part_capacity;
  body_mesh_ref *pending;
  size_t pending_count;
  bool active;
  struct player_scope *previous;
} player_scope;

typedef struct mesh_scope {
  player_scope *player;
  int64_t pointer;
  int32_t shader;
  size_t output_index; /* into player->parts; resolved at use */
  bool active;
  struct mesh_scope *previous;
} mesh_scope;

typedef struct scope_state {
  player_scope *player;
  mesh_scope *mesh;
  double call_sites[128];
  const char *call_parts[128];
  size_t call_count;
} scope_state;

static scope_state *scopes(qa_qvm_mod_presentation *presentation) {
  if (presentation->scopes == NULL) {
    scope_state *state = xmalloc(sizeof *state);
    memset(state, 0, sizeof *state);
    presentation->scopes = state;
  }
  return presentation->scopes;
}

static bool is_scene(qa_qvm_mod_presentation *presentation) {
  return presentation->options.declaration->is_scene;
}

static const qa_qvm_presentation_base *base_of(qa_qvm_mod_presentation *presentation) {
  return is_scene(presentation) ? &presentation->options.declaration->as.scene->base
                                : &presentation->options.declaration->as.player_events->base;
}

static const char *cgame_profile(qa_qvm_mod_presentation *presentation) {
  return base_of(presentation)->cgame.abi_profile;
}

static slot_actor *find_player(qa_qvm_mod_presentation *presentation, int64_t slot) {
  slot_actor *players = presentation->players;
  for (size_t i = 0; i < presentation->player_count; i++)
    if (players[i].slot == slot) return &players[i];
  return NULL;
}

static void set_player(qa_qvm_mod_presentation *presentation, int64_t slot, qa_actor_id actor) {
  slot_actor *found = find_player(presentation, slot);
  if (found != NULL) {
    found->actor = actor;
    return;
  }
  if (presentation->player_count == presentation->player_capacity) {
    presentation->player_capacity = presentation->player_capacity == 0 ? 8 : presentation->player_capacity * 2;
    presentation->players = realloc(presentation->players, presentation->player_capacity * sizeof(slot_actor));
    if (presentation->players == NULL) qa_range_error("QVM memory allocation failed");
  }
  slot_actor *players = presentation->players;
  players[presentation->player_count++] = (slot_actor){slot, actor};
}

static snapshot_entry *find_snapshot(qa_qvm_mod_presentation *presentation, int64_t number) {
  snapshot_entry *snapshots = presentation->snapshots;
  for (size_t i = 0; i < presentation->snapshot_count; i++)
    if (snapshots[i].number == number) return &snapshots[i];
  return NULL;
}

static void put_snapshot(qa_qvm_mod_presentation *presentation, qa_qvm_source_snapshot snapshot) {
  snapshot_entry *found = find_snapshot(presentation, snapshot.number);
  if (found != NULL) {
    qa_qvm_source_snapshot_free(&found->snapshot);
    found->snapshot = snapshot;
    return;
  }
  if (presentation->snapshot_count == presentation->snapshot_capacity) {
    presentation->snapshot_capacity = presentation->snapshot_capacity == 0 ? 8 : presentation->snapshot_capacity * 2;
    presentation->snapshots = realloc(presentation->snapshots, presentation->snapshot_capacity * sizeof(snapshot_entry));
    if (presentation->snapshots == NULL) qa_range_error("QVM memory allocation failed");
  }
  snapshot_entry *snapshots = presentation->snapshots;
  snapshots[presentation->snapshot_count].number = snapshot.number;
  snapshots[presentation->snapshot_count].snapshot = snapshot;
  presentation->snapshot_count++;
}

static void drop_snapshot(qa_qvm_mod_presentation *presentation, int64_t number) {
  snapshot_entry *snapshots = presentation->snapshots;
  for (size_t i = 0; i < presentation->snapshot_count; i++) {
    if (snapshots[i].number == number) {
      qa_qvm_source_snapshot_free(&snapshots[i].snapshot);
      memmove(&snapshots[i], &snapshots[i + 1], (presentation->snapshot_count - i - 1) * sizeof *snapshots);
      presentation->snapshot_count--;
      return;
    }
  }
}

static command_entry *find_command(qa_qvm_mod_presentation *presentation, int64_t sequence) {
  command_entry *commands = presentation->commands;
  for (size_t i = 0; i < presentation->command_count; i++)
    if (commands[i].sequence == sequence) return &commands[i];
  return NULL;
}

static void put_command(qa_qvm_mod_presentation *presentation, int64_t sequence,
                        const char *const *arguments, size_t argument_count) {
  command_entry *found = find_command(presentation, sequence);
  const char **copy = xmalloc(argument_count * sizeof *copy);
  memcpy(copy, arguments, argument_count * sizeof *copy);
  if (found != NULL) {
    free(found->arguments);
    found->arguments = copy;
    found->argument_count = argument_count;
    return;
  }
  if (presentation->command_count == presentation->command_capacity) {
    presentation->command_capacity = presentation->command_capacity == 0 ? 8 : presentation->command_capacity * 2;
    presentation->commands = realloc(presentation->commands, presentation->command_capacity * sizeof(command_entry));
    if (presentation->commands == NULL) qa_range_error("QVM memory allocation failed");
  }
  command_entry *commands = presentation->commands;
  commands[presentation->command_count++] = (command_entry){sequence, copy, argument_count};
}

static scene_actor_entry *find_scene_actor(qa_qvm_mod_presentation *presentation, int64_t slot) {
  scene_actor_entry *actors = presentation->scene_actors;
  for (size_t i = 0; i < presentation->scene_actor_count; i++)
    if (actors[i].slot == slot) return &actors[i];
  return NULL;
}

static void set_scene_actor(qa_qvm_mod_presentation *presentation, int64_t slot, qa_actor_id actor, bool owned) {
  scene_actor_entry *found = find_scene_actor(presentation, slot);
  if (found != NULL) {
    found->actor = actor;
    found->owned = owned;
    return;
  }
  if (presentation->scene_actor_count == presentation->scene_actor_capacity) {
    presentation->scene_actor_capacity = presentation->scene_actor_capacity == 0 ? 8 : presentation->scene_actor_capacity * 2;
    presentation->scene_actors = realloc(presentation->scene_actors, presentation->scene_actor_capacity * sizeof(scene_actor_entry));
    if (presentation->scene_actors == NULL) qa_range_error("QVM memory allocation failed");
  }
  scene_actor_entry *actors = presentation->scene_actors;
  actors[presentation->scene_actor_count++] = (scene_actor_entry){slot, actor, owned};
}

static void current(qa_qvm_mod_presentation *presentation, const qa_app_q3_source_player_event *event) {
  presentation->options.assert_current(presentation->options.ctx);
  if (strcmp(presentation->phase, "closed") == 0 || strcmp(presentation->phase, "failed") == 0)
    qa_range_error("Source presentation is %s", presentation->phase);
  if (event != NULL) {
    qa_actor_id resolved;
    bool has = presentation->options.actor(presentation->options.ctx,
                                           (int64_t)event->player_state.client_number, &resolved);
    if (!presentation->options.live(presentation->options.ctx, event->actor) || !has ||
        !qa_same_actor(resolved, event->actor))
      qa_range_error("Source event actor no longer owns its original client slot");
  }
}

static qa_save_reader rfield(const qa_save_reader *reader, const char *name) {
  qa_save_reader out;
  if (!qa_save_reader_field(reader, name, &out)) qa_range_error("QVM memory allocation failed");
  return out;
}

static qa_save_reader ritem(const qa_save_reader *reader, size_t index) {
  qa_save_reader out;
  if (!qa_save_reader_item(reader, index, &out)) qa_range_error("QVM memory allocation failed");
  return out;
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

static void push_child(qa_save_value *array, qa_save_value *child) {
  if (child == NULL || !qa_save_value_push(array, child)) qa_range_error("QVM memory allocation failed");
}

static qa_save_value *saved_actor_value(qa_actor_id actor) {
  qa_saved_actor_id saved = qa_saved_actor_id_of(actor);
  qa_save_value *value = make_object();
  set_number(value, "slot", saved.slot);
  set_number(value, "generation", saved.generation);
  return value;
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

static qa_save_value *capture_host(qa_qvm_mod_presentation *presentation) {
  scope_state *state = presentation->scopes;
  if (strcmp(presentation->phase, "initialized") != 0 || presentation->busy ||
      presentation->has_command_arguments || presentation->active_event != NULL ||
      (state != NULL && (state->player != NULL || state->mesh != NULL)) || presentation->restoring_scene)
    qa_range_error("Cannot checkpoint an active component call");
  qa_save_value *result = make_object();
  set_number(result, "version", 1);
  set_number(result, "sequence", (double)presentation->sequence);
  set_number(result, "revision", (double)presentation->revision);
  set_number(result, "frame", (double)presentation->frame);
  set_number(result, "hudFrame", (double)presentation->hud_frame);
  qa_save_value *players = make_array();
  slot_actor *player_entries = presentation->players;
  for (size_t i = 0; i < presentation->player_count; i++) {
    qa_save_value *row = make_object();
    set_number(row, "slot", (double)player_entries[i].slot);
    set_child(row, "actor", saved_actor_value(player_entries[i].actor));
    push_child(players, row);
  }
  set_child(result, "players", players);
  if (presentation->defaults == NULL) set_child(result, "defaults", qa_save_value_null());
  else set_child(result, "defaults", qa_save_value_bytes(presentation->defaults, presentation->defaults_length));
  qa_save_value *snapshots = make_array();
  snapshot_entry *entries = presentation->snapshots;
  for (size_t i = 0; i < presentation->snapshot_count; i++) {
    qa_qvm_presentation_snapshot captured;
    qa_qvm_capture_presentation_snapshot(&entries[i].snapshot, cgame_profile(presentation), &captured);
    qa_save_value *row = make_object();
    set_number(row, "number", (double)captured.number);
    set_child(row, "bytes", qa_save_value_bytes(captured.bytes, captured.byte_length));
    qa_qvm_presentation_snapshot_free(&captured);
    push_child(snapshots, row);
  }
  set_child(result, "snapshots", snapshots);
  set_number(result, "snapshotNumber", (double)presentation->snapshot_number);
  set_number(result, "sceneRevision", (double)presentation->scene_revision);
  qa_save_value *commands = make_array();
  command_entry *command_entries = presentation->commands;
  for (size_t i = 0; i < presentation->command_count; i++) {
    qa_save_value *row = make_object();
    set_number(row, "sequence", (double)command_entries[i].sequence);
    qa_save_value *arguments = make_array();
    for (size_t a = 0; a < command_entries[i].argument_count; a++)
      push_child(arguments, qa_save_value_string(command_entries[i].arguments[a]));
    set_child(row, "arguments", arguments);
    push_child(commands, row);
  }
  set_child(result, "commands", commands);
  qa_save_value *arguments = make_array();
  for (size_t i = 0; i < presentation->argument_count; i++)
    push_child(arguments, qa_save_value_string(presentation->arguments[i]));
  set_child(result, "arguments", arguments);
  qa_save_value *scene_actors = make_array();
  scene_actor_entry *actor_entries = presentation->scene_actors;
  for (size_t i = 0; i < presentation->scene_actor_count; i++) {
    qa_save_value *row = make_object();
    set_number(row, "slot", (double)actor_entries[i].slot);
    set_child(row, "owned", qa_save_value_boolean(actor_entries[i].owned));
    set_child(row, "actor", saved_actor_value(actor_entries[i].actor));
    push_child(scene_actors, row);
  }
  set_child(result, "sceneActors", scene_actors);
  if (!presentation->has_current_game_state) set_child(result, "currentGameState", qa_save_value_null());
  else set_child(result, "currentGameState", capture_game_state_value(&presentation->current_game_state));
  return result;
}

static double integer_field_min(const qa_save_reader *reader, const char *name, double minimum) {
  qa_save_reader at = rfield(reader, name);
  double value = qa_save_reader_integer_min(&at, minimum);
  qa_save_reader_destroy(&at);
  return value;
}

static void restore_host(qa_qvm_mod_presentation *presentation, qa_save_value *value) {
  if (presentation->restore_actor == NULL)
    qa_range_error("Component host restore has no saved-actor resolver");
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, value, "component-host")) qa_range_error("QVM memory allocation failed");
  qa_save_reader version = rfield(&reader, "version");
  (void)qa_save_reader_literal_number(&version, 1);
  qa_save_reader_destroy(&version);
  presentation->sequence = (int64_t)integer_field_min(&reader, "sequence", -1);
  presentation->revision = (int64_t)integer_field_min(&reader, "revision", -1);
  presentation->frame = (int64_t)integer_field_min(&reader, "frame", -1);
  presentation->hud_frame = (int64_t)integer_field_min(&reader, "hudFrame", -1);
  qa_save_reader defaults_reader = rfield(&reader, "defaults");
  if (!qa_save_reader_is_null(&defaults_reader)) {
    size_t length = 0;
    const uint8_t *borrowed = qa_save_reader_bytes(&defaults_reader, &length);
    free(presentation->defaults);
    presentation->defaults = xmalloc(length);
    memcpy(presentation->defaults, borrowed, length);
    presentation->defaults_length = length;
  } else {
    free(presentation->defaults);
    presentation->defaults = NULL;
    presentation->defaults_length = 0;
  }
  qa_save_reader_destroy(&defaults_reader);
  double stride, capacity;
  if (is_scene(presentation)) {
    stride = presentation->options.declaration->as.scene->storage.centities.stride;
    capacity = presentation->options.declaration->as.scene->storage.centities.capacity;
  } else {
    stride = presentation->options.declaration->as.player_events->storage.centities.stride;
    capacity = presentation->options.declaration->as.player_events->storage.centities.capacity;
  }
  if (presentation->defaults == NULL || (double)presentation->defaults_length != stride * capacity)
    qa_save_reader_fail(&reader, "invalid default centity bytes");
  qa_save_reader players = rfield(&reader, "players");
  size_t player_rows = qa_save_reader_list_count(&players);
  for (size_t i = 0; i < player_rows; i++) {
    qa_save_reader row = ritem(&players, i);
    int64_t slot = (int64_t)integer_field_min(&row, "slot", 0);
    qa_save_reader actor_reader = rfield(&row, "actor");
    qa_saved_actor_id saved = qa_read_saved_actor(&actor_reader);
    qa_save_reader_destroy(&actor_reader);
    if (slot >= (int64_t)capacity || find_player(presentation, slot) != NULL)
      qa_save_reader_fail(&reader, "invalid saved centity owner");
    set_player(presentation, slot, presentation->restore_actor(presentation->restore_actor_ctx, saved));
    qa_save_reader_destroy(&row);
  }
  qa_save_reader_destroy(&players);
  presentation->snapshot_number = (int64_t)integer_field_min(&reader, "snapshotNumber", 0);
  presentation->scene_revision = (int64_t)integer_field_min(&reader, "sceneRevision", -1);
  qa_save_reader snapshots_reader = rfield(&reader, "snapshots");
  size_t snapshot_rows = qa_save_reader_list_count(&snapshots_reader);
  for (size_t i = 0; i < snapshot_rows; i++) {
    qa_qvm_source_snapshot snapshot;
    qa_save_reader row = ritem(&snapshots_reader, i);
    qa_qvm_read_presentation_snapshot(&row, cgame_profile(presentation), &snapshot);
    qa_save_reader_destroy(&row);
    if (find_snapshot(presentation, snapshot.number) != NULL || snapshot.number > presentation->snapshot_number ||
        snapshot.number <= presentation->snapshot_number - 32)
      qa_save_reader_fail(&reader, "invalid snapshot ring");
    put_snapshot(presentation, snapshot);
  }
  qa_save_reader_destroy(&snapshots_reader);
  qa_save_reader commands_reader = rfield(&reader, "commands");
  size_t command_rows = qa_save_reader_list_count(&commands_reader);
  for (size_t i = 0; i < command_rows; i++) {
    qa_save_reader row = ritem(&commands_reader, i);
    int64_t sequence = (int64_t)integer_field_min(&row, "sequence", 0);
    qa_save_reader arguments_reader = rfield(&row, "arguments");
    size_t argument_count = qa_save_reader_list_count(&arguments_reader);
    const char **arguments = xmalloc(argument_count * sizeof *arguments);
    for (size_t a = 0; a < argument_count; a++) {
      qa_save_reader argument = ritem(&arguments_reader, a);
      arguments[a] = qa_save_reader_string(&argument);
      qa_save_reader_destroy(&argument);
    }
    qa_save_reader_destroy(&arguments_reader);
    qa_save_reader_destroy(&row);
    if (find_command(presentation, sequence) != NULL) {
      free(arguments);
      qa_save_reader_fail(&reader, "duplicate saved server command");
    }
    if (presentation->command_count == presentation->command_capacity) {
      presentation->command_capacity = presentation->command_capacity == 0 ? 8 : presentation->command_capacity * 2;
      presentation->commands = realloc(presentation->commands, presentation->command_capacity * sizeof(command_entry));
      if (presentation->commands == NULL) qa_range_error("QVM memory allocation failed");
    }
    command_entry *commands = presentation->commands;
    commands[presentation->command_count++] = (command_entry){sequence, arguments, argument_count};
  }
  qa_save_reader_destroy(&commands_reader);
  qa_save_reader arguments_reader = rfield(&reader, "arguments");
  size_t argument_count = qa_save_reader_list_count(&arguments_reader);
  const char **arguments = xmalloc(argument_count * sizeof *arguments);
  for (size_t i = 0; i < argument_count; i++) {
    qa_save_reader argument = ritem(&arguments_reader, i);
    arguments[i] = qa_save_reader_string(&argument);
    qa_save_reader_destroy(&argument);
  }
  qa_save_reader_destroy(&arguments_reader);
  presentation->arguments = arguments;
  presentation->argument_count = argument_count;
  qa_save_reader scene_actors = rfield(&reader, "sceneActors");
  size_t actor_rows = qa_save_reader_list_count(&scene_actors);
  for (size_t i = 0; i < actor_rows; i++) {
    qa_save_reader row = ritem(&scene_actors, i);
    int64_t slot = (int64_t)integer_field_min(&row, "slot", 0);
    qa_save_reader actor_reader = rfield(&row, "actor");
    qa_saved_actor_id saved = qa_read_saved_actor(&actor_reader);
    qa_save_reader_destroy(&actor_reader);
    qa_save_reader owned = rfield(&row, "owned");
    bool owned_value = qa_save_reader_boolean(&owned);
    qa_save_reader_destroy(&owned);
    qa_save_reader_destroy(&row);
    if (slot >= (int64_t)capacity || find_scene_actor(presentation, slot) != NULL)
      qa_save_reader_fail(&reader, "invalid saved scene owner");
    set_scene_actor(presentation, slot, presentation->restore_actor(presentation->restore_actor_ctx, saved), owned_value);
  }
  qa_save_reader_destroy(&scene_actors);
  qa_save_reader current_state = rfield(&reader, "currentGameState");
  if (qa_save_reader_is_null(&current_state)) {
    presentation->has_current_game_state = false;
  } else {
    qa_qvm_read_presentation_game_state(&current_state, &presentation->current_game_state);
    presentation->has_current_game_state = true;
  }
  qa_save_reader_destroy(&current_state);
  qa_save_reader_destroy(&reader);
}

static qa_qvm_host_checkpoint host_checkpoint(void *ctx) {
  qa_qvm_mod_presentation *presentation = ctx;
  qa_save_value *captured = capture_host(presentation);
  size_t length = 0;
  uint8_t *bytes = qa_encode_checkpoint_value(captured, &length);
  qa_save_value_free(captured);
  if (bytes == NULL) qa_range_error("QVM memory allocation failed");
  qa_qvm_host_checkpoint result;
  memset(&result, 0, sizeof result);
  result.state.module = presentation->options.module_options.artifact->module;
  result.state.format = "qvm:component-client-v1";
  result.state.bytes.bytes = bytes;
  result.state.bytes.length = length;
  return result;
}

static void host_restore(void *ctx, const qa_qvm_host_checkpoint *checkpoint) {
  qa_qvm_mod_presentation *presentation = ctx;
  if (strcmp(checkpoint->state.format, "qvm:component-client-v1") != 0 || checkpoint->random_count != 0 ||
      checkpoint->callback_count != 0)
    qa_range_error("Invalid component client host checkpoint");
  qa_save_value *decoded = qa_decode_checkpoint_value(checkpoint->state.bytes.bytes,
                                                      checkpoint->state.bytes.length);
  if (decoded == NULL) qa_range_error("QVM memory allocation failed");
  restore_host(presentation, decoded);
  qa_save_value_free(decoded);
}

static void write_vector_words(qa_qvm_mod_presentation *presentation, int64_t address, qa_vec3 value) {
  qa_qvm_writable_view view;
  qa_qvm_memory_data_view(presentation->module.memory, address, 12, &view);
  qa_qvm_writable_view_set_float32(&view, 0, value.x, true);
  qa_qvm_writable_view_set_float32(&view, 4, value.y, true);
  qa_qvm_writable_view_set_float32(&view, 8, value.z, true);
}

static void write_view(qa_qvm_mod_presentation *presentation, const qa_qvm_presentation_context *context) {
  const double *origins = NULL, *angles = NULL, *axes = NULL;
  size_t origin_count = 0, angle_count = 0, axis_count = 0;
  if (is_scene(presentation)) {
    const qa_qvm_scene_storage *storage = &presentation->options.declaration->as.scene->storage;
    origins = storage->view_origin.values; origin_count = storage->view_origin.count;
    angles = storage->view_angles.values; angle_count = storage->view_angles.count;
    axes = storage->view_axis.values; axis_count = storage->view_axis.count;
  } else {
    const qa_qvm_player_event_storage *storage = &presentation->options.declaration->as.player_events->storage;
    origins = storage->view_origin.values; origin_count = storage->view_origin.count;
    angles = storage->view_angles.values; angle_count = storage->view_angles.count;
    axes = storage->view_axis.values; axis_count = storage->view_axis.count;
  }
  for (size_t i = 0; i < origin_count; i++) write_vector_words(presentation, (int64_t)origins[i], context->view_origin);
  if (angle_count + axis_count == 0) return;
  if (!context->has_view_axis)
    qa_range_error("Declared original view storage requires the actual viewing camera axis");
  qa_axis axis = context->view_axis;
  for (size_t i = 0; i < axis_count; i++)
    for (int index = 0; index < 3; index++)
      write_vector_words(presentation, (int64_t)axes[i] + index * 12, axis.vectors[index]);
  qa_vec3 view_angles = qa_vector_to_angles(axis.vectors[0]);
  qa_axis basis = qa_qvm_angles_to_axis(view_angles);
  double roll = atan2(qa_dot3(axis.vectors[1], basis.vectors[2]), qa_dot3(axis.vectors[1], basis.vectors[1])) *
                180.0 / 3.141592653589793;
  view_angles.z = roll;
  for (size_t i = 0; i < angle_count; i++) write_vector_words(presentation, (int64_t)angles[i], view_angles);
}

static void write_game_state(qa_qvm_memory *memory, int64_t address, const qa_q3_source_game_state_record *state) {
  /* Mirrored writeSourceQvmGameState (TS client-state-record.ts). */
  qa_qvm_writable_view view;
  qa_qvm_memory_data_view(memory, address, QVM_GAME_STATE_BYTES, &view);
  for (int i = 0; i < 1024; i++)
    qa_qvm_writable_view_set_int32(&view, i * 4, state->string_offsets[i], true);
  qa_qvm_memory_write_bytes(memory, address + 4096, state->string_data, 16000);
  qa_qvm_writable_view_set_int32(&view, 20096, (int32_t)state->data_count, true);
}

static void write_source_snapshot_full(qa_qvm_memory *memory, int64_t address,
                                       const qa_qvm_source_snapshot *snapshot, const char *profile) {
  int size = qa_qvm_snapshot_bytes_for(profile);
  qa_qvm_writable_view view;
  qa_qvm_memory_data_view(memory, address, size, &view);
  if (snapshot->entity_count > 256) qa_range_error("Invalid source snapshot_t extent");
  int ps_bytes = qa_qvm_player_state_bytes(profile);
  int entity_bytes = qa_qvm_entity_state_bytes_for(profile);
  qa_qvm_writable_view_set_int32(&view, 0, qa_int32(snapshot->flags), true);
  qa_qvm_writable_view_set_int32(&view, 4, (int32_t)snapshot->player_state.ping_milliseconds, true);
  qa_qvm_writable_view_set_int32(&view, 8, qa_int32(snapshot->server_time), true);
  qa_qvm_memory_write_bytes(memory, address + 12, snapshot->area_mask, 32);
  qa_qvm_writable_view player_view;
  qa_qvm_memory_data_view(memory, address + 44, ps_bytes, &player_view);
  qa_qvm_write_source_player_state(&player_view, &snapshot->player_state, profile);
  qa_qvm_writable_view_set_int32(&view, 44 + ps_bytes, (int32_t)snapshot->entity_count, true);
  for (size_t i = 0; i < snapshot->entity_count; i++) {
    qa_qvm_writable_view entity_view;
    qa_qvm_memory_data_view(memory, address + 48 + ps_bytes + (int64_t)i * entity_bytes, entity_bytes, &entity_view);
    write_entity_fields(&entity_view, &snapshot->entities[i], profile);
  }
  qa_qvm_writable_view_set_int32(&view, size - 4, qa_int32(snapshot->server_command_sequence), true);
}

static qa_qvm_presentation_context build_context(qa_qvm_mod_presentation *presentation,
                                                 const qa_qvm_scene_context *scene) {
  qa_qvm_presentation_context context;
  memset(&context, 0, sizeof context);
  presentation->options.context(presentation->options.ctx, &context);
  if (scene != NULL) {
    context.scene = scene;
    context.game_state = scene->game_state;
    context.game_state_revision = scene->game_state_revision;
    context.snapshot_server_time = scene->snapshot.server_time;
    context.snapshot_player_state = scene->snapshot.player_state;
    if (scene->snapshot.player_state.stats.values != NULL)
      memcpy(context.player_slots, scene->snapshot.player_state.stats.values, 16 * sizeof(double));
    if (scene->snapshot.player_state.persistent.values != NULL)
      memcpy(context.player_slots + 16, scene->snapshot.player_state.persistent.values, 16 * sizeof(double));
    if (scene->snapshot.player_state.powerups.values != NULL)
      memcpy(context.player_slots + 32, scene->snapshot.player_state.powerups.values, 16 * sizeof(double));
    if (scene->snapshot.player_state.ammo.values != NULL)
      memcpy(context.player_slots + 48, scene->snapshot.player_state.ammo.values, 16 * sizeof(double));
    context.snapshot_player_state.stats.values = context.player_slots;
    context.snapshot_player_state.persistent.values = context.player_slots + 16;
    context.snapshot_player_state.powerups.values = context.player_slots + 32;
    context.snapshot_player_state.ammo.values = context.player_slots + 48;
    context.has_time = true;
    context.time_milliseconds = scene->snapshot.server_time;
    context.frame_time_milliseconds = 0;
  }
  int64_t data_game_state;
  const double *times = NULL, *frame_times = NULL;
  size_t time_count = 0, frame_time_count = 0;
  if (is_scene(presentation)) {
    const qa_qvm_scene_storage *storage = &presentation->options.declaration->as.scene->storage;
    data_game_state = (int64_t)storage->game_state;
    times = storage->time.values; time_count = storage->time.count;
    frame_times = storage->frame_time.values; frame_time_count = storage->frame_time.count;
  } else {
    const qa_qvm_player_event_storage *storage = &presentation->options.declaration->as.player_events->storage;
    data_game_state = (int64_t)storage->game_state;
    times = storage->time.values; time_count = storage->time.count;
    frame_times = storage->frame_time.values; frame_time_count = storage->frame_time.count;
  }
  if (context.game_state_revision < 0 || context.game_state_revision < presentation->revision)
    qa_range_error("Source gameState revision is invalid or stale");
  check_int32(context.frame_time_milliseconds);
  if (context.frame_time_milliseconds < 0 || !isfinite((double)(float)context.view_origin.x) ||
      !isfinite((double)(float)context.view_origin.y) || !isfinite((double)(float)context.view_origin.z))
    qa_range_error("Source presentation frame context is invalid");
  if (presentation->has_current_game_state)
    qa_q3_source_game_state_record_destroy(&presentation->current_game_state);
  presentation->current_game_state.string_offsets = xmalloc(1024 * sizeof(int32_t));
  memcpy(presentation->current_game_state.string_offsets, context.game_state.string_offsets, 1024 * sizeof(int32_t));
  presentation->current_game_state.string_data = xmalloc(16000);
  memcpy(presentation->current_game_state.string_data, context.game_state.string_data, 16000);
  presentation->current_game_state.data_count = context.game_state.data_count;
  presentation->has_current_game_state = true;
  check_int32((double)context.snapshot_server_time);
  check_int32(context.snapshot_player_state.client_number);
  qa_qvm_memory *memory = presentation->module.memory;
  if (context.game_state_revision != presentation->revision)
    write_game_state(memory, data_game_state, &context.game_state);
  if (!is_scene(presentation)) {
    const qa_qvm_player_event_storage *storage = &presentation->options.declaration->as.player_events->storage;
    const char *profile = cgame_profile(presentation);
    int bytes = qa_qvm_snapshot_bytes_for(profile);
    int ps_bytes = qa_qvm_player_state_bytes(profile);
    qa_qvm_writable_view view;
    qa_qvm_memory_data_view(memory, (int64_t)storage->snapshot.address, bytes, &view);
    qa_qvm_writable_view_set_int32(&view, 0, 0, true);
    qa_qvm_writable_view_set_int32(&view, 4, (int32_t)context.snapshot_player_state.ping_milliseconds, true);
    qa_qvm_writable_view_set_int32(&view, 8, (int32_t)context.snapshot_server_time, true);
    qa_qvm_memory_fill_bytes(memory, (int64_t)storage->snapshot.address + 12, 32, 0);
    qa_qvm_writable_view player_view;
    qa_qvm_memory_data_view(memory, (int64_t)storage->snapshot.address + 44, ps_bytes, &player_view);
    qa_qvm_write_source_player_state(&player_view, &context.snapshot_player_state, profile);
    qa_qvm_memory_data_view(memory, (int64_t)storage->player_state, ps_bytes, &player_view);
    qa_qvm_write_source_player_state(&player_view, &context.snapshot_player_state, profile);
    qa_qvm_writable_view_set_int32(&view, 44 + ps_bytes, 0, true);
    qa_qvm_writable_view_set_int32(&view, bytes - 8, 0, true);
    qa_qvm_writable_view_set_int32(&view, bytes - 4, 0, true);
    for (size_t i = 0; i < storage->snapshot.pointers.count; i++) {
      qa_qvm_writable_view pointer_view;
      qa_qvm_memory_data_view(memory, (int64_t)storage->snapshot.pointers.values[i], 4, &pointer_view);
      qa_qvm_writable_view_set_int32(&pointer_view, 0, (int32_t)storage->snapshot.address, true);
    }
  }
  int64_t time_value = context.has_time ? context.time_milliseconds : context.snapshot_server_time;
  check_int32((double)time_value);
  for (size_t i = 0; i < time_count; i++) {
    qa_qvm_writable_view view;
    qa_qvm_memory_data_view(memory, (int64_t)times[i], 4, &view);
    qa_qvm_writable_view_set_int32(&view, 0, (int32_t)time_value, true);
  }
  for (size_t i = 0; i < frame_time_count; i++) {
    qa_qvm_writable_view view;
    qa_qvm_memory_data_view(memory, (int64_t)frame_times[i], 4, &view);
    qa_qvm_writable_view_set_int32(&view, 0, context.frame_time_milliseconds, true);
  }
  write_view(presentation, &context);
  return context;
}

static int32_t word_value(qa_qvm_mod_presentation *presentation, const qa_qvm_presentation_argument *argument,
                          const qa_qvm_presentation_context *context, const qa_app_q3_source_player_event *event) {
  if (strcmp(argument->kind, "float32") == 0) return (int32_t)qa_float32_to_bits(argument->number_value);
  if (strcmp(argument->kind, "source") != 0) return (int32_t)argument->number_value;
  const char *value = argument->source_value;
  if (strcmp(value, "snapshot-number") == 0) {
    int64_t number = presentation->snapshot_number - 1;
    return (int32_t)(number > 0 ? number : 0);
  }
  if (strcmp(value, "server-command-sequence") == 0)
    return context->scene != NULL ? context->scene->snapshot.server_command_sequence : 0;
  if (strcmp(value, "player-state") == 0) {
    if (is_scene(presentation)) qa_range_error("Scene calls cannot borrow event projection state");
    return (int32_t)presentation->options.declaration->as.player_events->storage.player_state;
  }
  if (strcmp(value, "snapshot") == 0) {
    if (is_scene(presentation)) qa_range_error("Scene snapshots are supplied by original engine traps");
    return (int32_t)presentation->options.declaration->as.player_events->storage.snapshot.address;
  }
  if (strcmp(value, "client-number") == 0)
    return (int32_t)(context->has_client_number ? context->client_number
                                                 : (int64_t)context->snapshot_player_state.client_number);
  if (strcmp(value, "time") == 0)
    return (int32_t)(context->has_time ? context->time_milliseconds : context->snapshot_server_time);
  if (event == NULL || is_scene(presentation)) qa_range_error("Original call requires a source player event");
  const qa_qvm_player_event_storage *storage = &presentation->options.declaration->as.player_events->storage;
  int64_t entity = (int64_t)storage->centities.address +
                   (int64_t)event->player_state.client_number * (int64_t)storage->centities.stride;
  if (strcmp(value, "entity-state") == 0) return (int32_t)(entity + (int64_t)storage->centities.state);
  if (strcmp(value, "centity") == 0) return (int32_t)entity;
  if (strcmp(value, "origin") == 0) return (int32_t)(entity + (int64_t)storage->centities.origin);
  if (strcmp(value, "event") == 0) return event->event;
  return event->parameter;
}

typedef struct call_validate {
  qa_qvm_mod_presentation *presentation;
  const qa_app_q3_source_player_event *event;
} call_validate;

static void run_call_validate(void *ctx) {
  call_validate *validate = ctx;
  current(validate->presentation,
          validate->event != NULL ? validate->event : validate->presentation->active_event);
}

static void invoke_call(qa_qvm_mod_presentation *presentation, const qa_qvm_presentation_call *call,
                        qa_qvm_presentation_context *context, const qa_app_q3_source_player_event *event) {
  call_validate validate = {presentation, event};
  run_call_validate(&validate);
  if (call->when != NULL && strcmp(call->when, "weapon-presented") == 0) {
    qa_qvm_presentation_context fresh;
    memset(&fresh, 0, sizeof fresh);
    presentation->options.context(presentation->options.ctx, &fresh);
    if (!fresh.has_weapon_presented)
      qa_range_error("Original weapon presentation requires a current source owner");
    if (!fresh.weapon_presented) return;
  }
  write_view(presentation, context);
  int32_t words[62];
  for (size_t i = 0; i < call->argument_count && i < 62; i++)
    words[i] = word_value(presentation, &call->arguments[i], context, event);
  (void)qa_qvm_module_call_async(&presentation->module, words, call->argument_count, (int64_t)call->entry,
                                 run_call_validate, &validate);
  run_call_validate(&validate);
}

static void refresh_context(qa_qvm_mod_presentation *presentation, qa_qvm_presentation_context *context) {
  if (context->game_state_revision == presentation->revision) return;
  for (size_t i = 0; i < base_of(presentation)->refresh_count; i++)
    invoke_call(presentation, &base_of(presentation)->refresh[i], context, NULL);
  presentation->revision = context->game_state_revision;
}

/* Landed QvmCgameImport / QvmCgameExport values (TS abi.ts). */
enum {
  CG_GETGAMESTATE = QA_QVM_CGAME_IMPORT_CG_GETGAMESTATE,
  CG_GETCURRENTSNAPSHOTNUMBER = QA_QVM_CGAME_IMPORT_CG_GETCURRENTSNAPSHOTNUMBER,
  CG_GETSNAPSHOT = QA_QVM_CGAME_IMPORT_CG_GETSNAPSHOT,
  CG_GETSERVERCOMMAND = QA_QVM_CGAME_IMPORT_CG_GETSERVERCOMMAND,
  CG_R_ADDREFENTITYTOSCENE = QA_QVM_CGAME_IMPORT_CG_R_ADDREFENTITYTOSCENE,
  CG_CONSOLE_COMMAND = QA_QVM_CGAME_EXPORT_CG_CONSOLE_COMMAND,
};

static int32_t word_at(const qa_qvm_host_call *call, size_t index) {
  return qa_qvm_host_call_word(call, index);
}

static bool scene_syscall(qa_qvm_mod_presentation *presentation, const qa_qvm_host_call *call, int32_t *out) {
  if (!is_scene(presentation) || strcmp(call->kind, "engine") != 0 || strcmp(call->role, "cgame") != 0)
    return false;
  qa_qvm_memory *guest = call->guest;
  const char *profile = cgame_profile(presentation);
  if (call->code == CG_GETGAMESTATE) {
    if (!presentation->has_current_game_state)
      qa_range_error("Component cgame has no active source gameState");
    write_game_state(guest, word_at(call, 1), &presentation->current_game_state);
    *out = 0;
    return true;
  }
  if (call->code == CG_R_ADDREFENTITYTOSCENE) {
    scope_state *state = presentation->scopes;
    mesh_scope *mesh = state != NULL ? state->mesh : NULL;
    if (mesh != NULL && mesh->active && mesh->player == (state != NULL ? state->player : NULL) &&
        presentation->options.live(presentation->options.ctx, mesh->player->actor)) {
      qa_qvm_writable_view number_view;
      qa_qvm_memory_view(guest, (int32_t)mesh->player->state, 4, 0, &number_view);
      int32_t number = qa_qvm_writable_view_get_int32(&number_view, 0, true);
      qa_actor_id resolved;
      bool has = presentation->options.actor(presentation->options.ctx, number, &resolved);
      if (has && qa_same_actor(resolved, mesh->player->actor) && word_at(call, 1) == mesh->pointer) {
        qa_qvm_writable_view view;
        qa_qvm_memory_view(guest, (int32_t)mesh->pointer, QA_QVM_REF_ENTITY_BYTES, 0, &view);
        qa_qvm_source_ref_entity_record source;
        qa_qvm_read_ref_entity(&view, &source);
        if (strcmp(source.kind, "model") != 0) return false;
        qa_qvm_body_mesh *output = &mesh->player->parts[mesh->output_index];
        if (source.custom_shader == mesh->shader) output->base = true;
        else {
          if (output->pass_count == output->pass_capacity) {
            output->pass_capacity = output->pass_capacity == 0 ? 4 : output->pass_capacity * 2;
            output->passes = realloc(output->passes, output->pass_capacity * sizeof *output->passes);
            if (output->passes == NULL) qa_range_error("QVM memory allocation failed");
          }
          output->passes[output->pass_count++] = source;
        }
        *out = 0;
        return true;
      }
    }
    return false;
  }
  if (call->code == CG_GETCURRENTSNAPSHOTNUMBER) {
    qa_qvm_writable_view view;
    qa_qvm_memory_view(guest, word_at(call, 1), 4, 0, &view);
    qa_qvm_writable_view_set_int32(&view, 0, (int32_t)presentation->snapshot_number, true);
    snapshot_entry *current_snapshot = find_snapshot(presentation, presentation->snapshot_number);
    qa_qvm_memory_view(guest, word_at(call, 2), 4, 0, &view);
    qa_qvm_writable_view_set_int32(&view, 0, current_snapshot != NULL ? current_snapshot->snapshot.server_time : 0, true);
    *out = 0;
    return true;
  }
  if (call->code == CG_GETSNAPSHOT) {
    int32_t number = word_at(call, 1);
    if (number > presentation->snapshot_number)
      qa_range_error("Component cgame requested a future snapshot");
    snapshot_entry *snapshot = find_snapshot(presentation, number);
    if (snapshot == NULL) {
      *out = 0;
      return true;
    }
    write_source_snapshot_full(guest, word_at(call, 2), &snapshot->snapshot, profile);
    *out = 1;
    return true;
  }
  if (call->code == CG_GETSERVERCOMMAND) {
    command_entry *entry = find_command(presentation, word_at(call, 1));
    if (entry == NULL)
      qa_range_error("Component cgame server command fell outside retained source history");
    presentation->arguments = (const char *const *)entry->arguments;
    presentation->argument_count = entry->argument_count;
    *out = entry->argument_count != 0 ? 1 : 0;
    return true;
  }
  return false;
}

static qa_qvm_host_result host_wrapper(void *ctx, const qa_qvm_host_call *call) {
  qa_qvm_mod_presentation *presentation = ctx;
  current(presentation, presentation->active_event);
  int32_t result = 0;
  if (scene_syscall(presentation, call, &result)) return result;
  qa_qvm_host_fn host = presentation->options.module_options.host;
  if (host == NULL) qa_qvm_reject_syscall(call);
  return host(presentation->options.module_options.host_ctx, call);
}

static bool accept_scene(qa_qvm_mod_presentation *presentation, const qa_qvm_scene_context *scene, bool baseline) {
  if (scene->revision < presentation->scene_revision)
    qa_range_error("Component scene publication moved backward");
  if (scene->revision == presentation->scene_revision) return false;
  if (!is_scene(presentation) || presentation->defaults == NULL)
    qa_range_error("Component scene has no initialized caller storage");
  const qa_qvm_scene_storage *storage = &presentation->options.declaration->as.scene->storage;
  presentation->scene_actor_count = 0;
  for (size_t i = 0; i < scene->actor_count; i++) {
    int64_t slot = scene->actors[i].slot;
    if (slot < 0 || slot >= (int64_t)storage->centities.capacity)
      qa_range_error("Component source actor exceeds cgame centities");
    slot_actor *previous = find_player(presentation, slot);
    if (previous != NULL && !qa_same_actor(previous->actor, scene->actors[i].actor)) {
      qa_qvm_memory_write_bytes(presentation->module.memory,
          (int64_t)storage->centities.address + slot * (int64_t)storage->centities.stride,
          presentation->defaults + (size_t)(slot * (int64_t)storage->centities.stride),
          (size_t)storage->centities.stride);
    }
    set_player(presentation, slot, scene->actors[i].actor);
    set_scene_actor(presentation, slot, scene->actors[i].actor, scene->actors[i].owned);
  }
  if (baseline) {
    for (size_t i = 0; i < scene->snapshot.entity_count; i++) {
      const qa_qvm_entity_state_fields *state = &scene->snapshot.entities[i];
      int64_t address = (int64_t)storage->centities.address + state->number * (int64_t)storage->centities.stride;
      qa_qvm_writable_view view;
      qa_qvm_memory_data_view(presentation->module.memory, address, (int64_t)storage->centities.stride, &view);
      qa_qvm_writable_view_set_int32(&view, (int64_t)storage->centities.previous_event,
          (double)state->e_type > presentation->options.declaration->as.scene->event_entity_type ? 1 : state->event, true);
      qa_qvm_writable_view_set_int32(&view, (int64_t)storage->centities.snapshot_time, scene->snapshot.server_time, true);
    }
  }
  presentation->scene_revision = scene->revision;
  for (size_t i = 0; i < scene->command_count; i++)
    put_command(presentation, scene->commands[i].sequence, scene->commands[i].arguments, scene->commands[i].argument_count);
  command_entry *commands = presentation->commands;
  for (size_t i = 0; i < presentation->command_count;) {
    if (commands[i].sequence <= scene->snapshot.server_command_sequence - 64) {
      free((void *)commands[i].arguments);
      memmove(&commands[i], &commands[i + 1], (presentation->command_count - i - 1) * sizeof *commands);
      presentation->command_count--;
    } else {
      i++;
    }
  }
  int64_t number = ++presentation->snapshot_number;
  qa_qvm_source_snapshot copy = scene->snapshot;
  copy.number = (double)number;
  qa_qvm_entity_state_fields *copy_entities = xmalloc(copy.entity_count * sizeof *copy_entities);
  memcpy(copy_entities, scene->snapshot.entities, copy.entity_count * sizeof *copy_entities);
  copy.entities = copy_entities;
  if (scene->snapshot.area_mask != NULL) {
    uint8_t *copy_mask = xmalloc(scene->snapshot.area_mask_length);
    memcpy(copy_mask, scene->snapshot.area_mask, scene->snapshot.area_mask_length);
    copy.area_mask = copy_mask;
  }
  double *copy_slots = xmalloc(64 * sizeof *copy_slots);
  memset(copy_slots, 0, 64 * sizeof *copy_slots);
  if (scene->snapshot.player_state.stats.values != NULL)
    memcpy(copy_slots, scene->snapshot.player_state.stats.values, 16 * sizeof(double));
  if (scene->snapshot.player_state.persistent.values != NULL)
    memcpy(copy_slots + 16, scene->snapshot.player_state.persistent.values, 16 * sizeof(double));
  if (scene->snapshot.player_state.powerups.values != NULL)
    memcpy(copy_slots + 32, scene->snapshot.player_state.powerups.values, 16 * sizeof(double));
  if (scene->snapshot.player_state.ammo.values != NULL)
    memcpy(copy_slots + 48, scene->snapshot.player_state.ammo.values, 16 * sizeof(double));
  copy.player_state = scene->snapshot.player_state;
  copy.player_state.stats.values = copy_slots;
  copy.player_state.persistent.values = copy_slots + 16;
  copy.player_state.powerups.values = copy_slots + 32;
  copy.player_state.ammo.values = copy_slots + 48;
  put_snapshot(presentation, copy);
  drop_snapshot(presentation, number - 32);
  return true;
}

typedef struct hook_state {
  qa_qvm_mod_presentation *presentation;
  int kind; /* 0 player, 1 mesh, 2 event check */
} hook_state;

static int32_t call_word(qa_qvmy_function_call *call, double index) {
  size_t offset = (size_t)(int64_t)index * 4;
  if (offset > call->words_length || call->words_length - offset < 4)
    qa_range_error("Offset is outside the bounds of the DataView");
  int32_t value;
  memcpy(&value, call->words + offset, 4);
  return value;
}

static int32_t proceed(qa_qvmy_function_call *call) {
  return strcmp(call->execution, "synchronous") == 0 ? call->proceed(call->call_ctx)
                                                     : call->proceed_async(call->call_ctx);
}

static int32_t body_hook(void *ctx, qa_qvmy_function_call *call) {
  hook_state *hook = ctx;
  qa_qvm_mod_presentation *presentation = hook->presentation;
  const qa_qvm_scene_presentation *declaration = presentation->options.declaration->as.scene;
  scope_state *state = scopes(presentation);
  if (hook->kind == 2) {
    if (presentation->restoring_scene) {
      int32_t pointer = call_word(call, declaration->event_check.centity_argument);
      const qa_qvm_scene_centities *fields = &declaration->storage.centities;
      qa_qvm_writable_view state_view;
      qa_qvm_memory_view(call->guest, pointer + (int32_t)fields->state,
                         qa_qvm_entity_state_bytes_for(cgame_profile(presentation)), 0, &state_view);
      int32_t previous = qa_qvm_writable_view_get_int32(&state_view, 4, true) > (int32_t)declaration->event_entity_type
          ? 1 : qa_qvm_writable_view_get_int32(&state_view, 180, true);
      qa_qvm_writable_view cursor;
      qa_qvm_memory_view(call->guest, pointer + (int32_t)fields->previous_event, 4, 0, &cursor);
      qa_qvm_writable_view_set_int32(&cursor, 0, previous, true);
    }
    return proceed(call);
  }
  if (hook->kind == 0) {
    player_scope *previous = state->player;
    int64_t pointer = call_word(call, declaration->body.player.centity_argument) +
                      (int64_t)declaration->storage.centities.state;
    qa_qvm_writable_view number_view;
    qa_qvm_memory_view(call->guest, (int32_t)pointer, 4, 0, &number_view);
    int32_t number = qa_qvm_writable_view_get_int32(&number_view, 0, true);
    scene_actor_entry *row = find_scene_actor(presentation, number);
    player_scope *player = NULL;
    if (row != NULL && !row->owned) {
      qa_actor_id resolved;
      bool has = presentation->options.actor(presentation->options.ctx, number, &resolved);
      if (has && qa_same_actor(resolved, row->actor) && presentation->options.live(presentation->options.ctx, row->actor)) {
        player = xmalloc(sizeof *player);
        memset(player, 0, sizeof *player);
        player->actor = row->actor;
        player->state = pointer;
        player->active = true;
        player->previous = previous;
        player->parts = xmalloc(state->call_count * sizeof *player->parts);
        player->pending = xmalloc(state->call_count * sizeof *player->pending);
        for (size_t i = 0; i < state->call_count; i++) {
          memset(&player->parts[i], 0, sizeof player->parts[i]);
          player->parts[i].part = state->call_parts[i];
          player->pending[i].part_index = i;
          player->pending[i].site = (int64_t)state->call_sites[i];
        }
        player->part_count = state->call_count;
        player->part_capacity = state->call_count;
        player->pending_count = state->call_count;
      }
    }
    state->player = player;
    int32_t result = proceed(call);
    if (player != NULL && presentation->options.live(presentation->options.ctx, player->actor)) {
      if (presentation->body_count == presentation->body_capacity) {
        presentation->body_capacity = presentation->body_capacity == 0 ? 8 : presentation->body_capacity * 2;
        presentation->body_frame = realloc(presentation->body_frame, presentation->body_capacity * sizeof *presentation->body_frame);
        if (presentation->body_frame == NULL) qa_range_error("QVM memory allocation failed");
      }
      presentation->body_frame[presentation->body_count].actor = player->actor;
      presentation->body_frame[presentation->body_count].parts = player->parts;
      presentation->body_frame[presentation->body_count].part_count = player->part_count;
      presentation->body_count++;
    } else if (player != NULL) {
      for (size_t i = 0; i < player->part_count; i++) free(player->parts[i].passes);
      free(player->parts);
    }
    if (player != NULL) free(player->pending);
    free(player);
    state->player = previous;
    return result;
  }
  mesh_scope *previous = state->mesh;
  player_scope *player = state->player;
  int32_t pointer = call_word(call, declaration->body.mesh.entity_argument);
  int32_t entity_state = call_word(call, declaration->body.mesh.state_argument);
  const char *part = NULL;
  bool has_output = false;
  size_t output_index = 0;
  if (player != NULL && entity_state == player->state && call->has_caller_instruction) {
    for (size_t i = 0; i < state->call_count; i++)
      if ((int64_t)state->call_sites[i] == call->caller_instruction) part = state->call_parts[i];
    if (part != NULL) {
      for (size_t i = 0; i < player->pending_count; i++) {
        if (player->pending[i].site == call->caller_instruction) {
          output_index = player->pending[i].part_index;
          has_output = true;
          memmove(&player->pending[i], &player->pending[i + 1],
                  (player->pending_count - i - 1) * sizeof *player->pending);
          player->pending_count--;
          break;
        }
      }
      if (!has_output) {
        if (player->part_count == player->part_capacity) {
          player->part_capacity = player->part_capacity == 0 ? 4 : player->part_capacity * 2;
          player->parts = realloc(player->parts, player->part_capacity * sizeof *player->parts);
          if (player->parts == NULL) qa_range_error("QVM memory allocation failed");
        }
        output_index = player->part_count++;
        has_output = true;
        memset(&player->parts[output_index], 0, sizeof player->parts[output_index]);
        player->parts[output_index].part = part;
      }
    }
  }
  mesh_scope *mesh = NULL;
  if (player != NULL && has_output) {
    mesh = xmalloc(sizeof *mesh);
    mesh->player = player;
    mesh->pointer = pointer;
    qa_qvm_writable_view shader_view;
    qa_qvm_memory_view(call->guest, pointer + (int32_t)declaration->body.mesh.shader_offset, 4, 0, &shader_view);
    mesh->shader = qa_qvm_writable_view_get_int32(&shader_view, 0, true);
    mesh->output_index = output_index;
    mesh->active = true;
    mesh->previous = previous;
  }
  state->mesh = mesh;
  int32_t result = proceed(call);
  free(mesh);
  state->mesh = previous;
  return result;
}


/* qualifyQvmBodyCalls over the artifact's instruction list (the
 * landed qa_qvm_qualify_body_calls reads only instructions/count).
 * Fills up to capacity (site, part) pairs; returns the filled count. */
static size_t qualify_body_calls(const qa_qvmy_artifact *artifact, const qa_qvm_scene_body *body,
                                 double *out_sites, const char **out_parts, size_t capacity) {
  qa_qvm_image image;
  memset(&image, 0, sizeof image);
  image.instructions = (qa_qvm_instruction *)artifact->instructions;
  image.instruction_count = artifact->instruction_count;
  qa_qvm_body_calls calls;
  if (!qa_qvm_qualify_body_calls(&image, *body, &calls)) qa_range_error("QVM memory allocation failed");
  size_t filled = calls.count < capacity ? calls.count : capacity;
  for (size_t i = 0; i < filled; i++) {
    if (out_sites != NULL) out_sites[i] = calls.entries[i].call;
    if (out_parts != NULL) out_parts[i] = calls.entries[i].part;
  }
  qa_qvm_body_calls_destroy(&calls);
  return filled;
}

static void bind_body(qa_qvm_mod_presentation *presentation) {
  scope_state *state = scopes(presentation);
  state->call_count = qualify_body_calls(
      presentation->options.module_options.artifact,
      &presentation->options.declaration->as.scene->body, state->call_sites, state->call_parts, 128);
  double entries[] = {presentation->options.declaration->as.scene->body.player.entry,
                      presentation->options.declaration->as.scene->body.mesh.entry,
                      presentation->options.declaration->as.scene->event_check.entry};
  for (int i = 0; i < 3; i++) {
    hook_state *hook = xmalloc(sizeof *hook);
    hook->presentation = presentation;
    hook->kind = i;
    qa_guest_callback_reference reference;
    memset(&reference, 0, sizeof reference);
    reference.kind = QA_GUEST_CALLBACK_QVM;
    reference.module = qa_qvm_module_get_profile(&presentation->module)->module;
    reference.instruction_index = entries[i];
    presentation->removal_ids[presentation->removal_count++] =
        qa_qvm_module_bind_invocation(&presentation->module, &reference, body_hook, hook);
  }
}

void qa_qvm_mod_presentation_init(qa_qvm_mod_presentation *presentation,
                                  const qa_qvm_mod_presentation_options *options) {
  memset(presentation, 0, sizeof *presentation);
  presentation->options = *options;
  presentation->phase = "created";
  presentation->sequence = -1;
  presentation->revision = -1;
  presentation->frame = -1;
  presentation->hud_frame = -1;
  presentation->scene_revision = -1;
  qa_qvm_validate_mod_presentation(options->module_options.artifact, options->source, options->declaration);
  if (is_scene(presentation))
    (void)qualify_body_calls(options->module_options.artifact,
                             &options->declaration->as.scene->body, NULL, NULL, 0);
  presentation->host_state.ctx = presentation;
  presentation->host_state.checkpoint = host_checkpoint;
  presentation->host_state.restore = host_restore;
  qa_qvm_module_options module_options = options->module_options;
  module_options.host = host_wrapper;
  module_options.host_ctx = presentation;
  module_options.host_state = &presentation->host_state;
  qa_qvm_module_init(&presentation->module, &module_options);
  if (is_scene(presentation)) bind_body(presentation);
}

static void free_bodies(qa_qvm_mod_presentation *presentation) {
  for (size_t i = 0; i < presentation->body_count; i++) {
    for (size_t p = 0; p < presentation->body_frame[i].part_count; p++)
      free(presentation->body_frame[i].parts[p].passes);
    free(presentation->body_frame[i].parts);
  }
  presentation->body_count = 0;
}

void qa_qvm_mod_presentation_destroy(qa_qvm_mod_presentation *presentation) {
  free_bodies(presentation);
  free(presentation->body_frame);
  free(presentation->players);
  free(presentation->defaults);
  snapshot_entry *snapshots = presentation->snapshots;
  for (size_t i = 0; i < presentation->snapshot_count; i++) qa_qvm_source_snapshot_free(&snapshots[i].snapshot);
  free(presentation->snapshots);
  command_entry *commands = presentation->commands;
  for (size_t i = 0; i < presentation->command_count; i++) free((void *)commands[i].arguments);
  free(presentation->commands);
  free(presentation->scene_actors);
  free(presentation->scopes);
  if (presentation->has_current_game_state)
    qa_q3_source_game_state_record_destroy(&presentation->current_game_state);
  qa_qvm_module_destroy(&presentation->module);
  memset(presentation, 0, sizeof *presentation);
}

const qa_qvm_body_presentation *qa_qvm_mod_presentation_bodies(qa_qvm_mod_presentation *presentation,
                                                               size_t *out_count) {
  current(presentation, NULL);
  *out_count = presentation->body_count;
  return presentation->body_frame;
}

void qa_qvm_mod_presentation_capture_checkpoint(qa_qvm_mod_presentation *presentation, qa_guest_checkpoint *out) {
  current(presentation, NULL);
  if (strcmp(presentation->phase, "initialized") != 0 || presentation->busy)
    qa_range_error("Component checkpoint requires an idle initialized cgame");
  qa_qvm_module_checkpoint(&presentation->module, out);
}

void qa_qvm_mod_presentation_restore_checkpoint(qa_qvm_mod_presentation *presentation, const qa_save_value *value,
                                                qa_qvm_resolve_saved_actor_fn resolve, void *resolve_ctx) {
  current(presentation, NULL);
  if (strcmp(presentation->phase, "created") != 0 || presentation->busy)
    qa_range_error("Component restore requires an uninitialized cgame");
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, value, "component-client")) qa_range_error("QVM memory allocation failed");
  qa_guest_checkpoint checkpoint = qa_read_guest(&reader);
  qa_save_reader_destroy(&reader);
  if (checkpoint.kind != QA_GUEST_CHECKPOINT_QVM)
    qa_range_error("Component client checkpoint is not QVM state");
  presentation->restore_actor = resolve;
  presentation->restore_actor_ctx = resolve_ctx;
  qa_qvm_module_restore(&presentation->module, &checkpoint);
  presentation->phase = "initialized";
  presentation->restore_actor = NULL;
}

const char *const *qa_qvm_mod_presentation_arguments(qa_qvm_mod_presentation *presentation, size_t *out_count) {
  *out_count = presentation->argument_count;
  return presentation->arguments;
}

int64_t qa_qvm_mod_presentation_last_sequence(const qa_qvm_mod_presentation *presentation) {
  return presentation->sequence;
}

void qa_qvm_mod_presentation_initialize(qa_qvm_mod_presentation *presentation, int64_t baseline_sequence) {
  current(presentation, NULL);
  if (presentation->busy || strcmp(presentation->phase, "created") != 0 || baseline_sequence < -1)
    qa_range_error("Invalid source presentation initialization");
  presentation->busy = true;
  presentation->sequence = baseline_sequence;
  qa_qvm_presentation_context context = build_context(presentation, NULL);
  const qa_qvm_scene_context *baseline_scene = context.scene != NULL ? context.scene->baseline : NULL;
  if (baseline_scene != NULL) context = build_context(presentation, baseline_scene);
  for (size_t i = 0; i < base_of(presentation)->initialize_count; i++)
    invoke_call(presentation, &base_of(presentation)->initialize[i], &context, NULL);
  presentation->revision = context.game_state_revision;
  double centity_address, centity_stride, centity_capacity;
  if (is_scene(presentation)) {
    centity_address = presentation->options.declaration->as.scene->storage.centities.address;
    centity_stride = presentation->options.declaration->as.scene->storage.centities.stride;
    centity_capacity = presentation->options.declaration->as.scene->storage.centities.capacity;
  } else {
    centity_address = presentation->options.declaration->as.player_events->storage.centities.address;
    centity_stride = presentation->options.declaration->as.player_events->storage.centities.stride;
    centity_capacity = presentation->options.declaration->as.player_events->storage.centities.capacity;
  }
  size_t extent = (size_t)(centity_stride * centity_capacity);
  presentation->defaults = xmalloc(extent);
  memcpy(presentation->defaults, presentation->module.memory->bytes + (size_t)centity_address, extent);
  presentation->defaults_length = extent;
  presentation->phase = "initialized";
  if (is_scene(presentation)) {
    if (context.scene == NULL) qa_range_error("Scene declaration requires real source snapshots");
    qa_qvm_writable_view view;
    qa_qvm_memory_data_view(presentation->module.memory,
        (int64_t)presentation->options.declaration->as.scene->storage.server_command_sequence, 4, &view);
    qa_qvm_writable_view_set_int32(&view, 0, context.scene->snapshot.server_command_sequence, true);
    qa_qvm_presentation_context fresh;
    memset(&fresh, 0, sizeof fresh);
    presentation->options.context(presentation->options.ctx, &fresh);
    if (fresh.scene != NULL && fresh.scene->baseline != NULL) {
      (void)accept_scene(presentation, fresh.scene->baseline, true);
      presentation->restoring_scene = true;
      for (size_t i = 0; i < presentation->options.declaration->as.scene->snapshot_count; i++)
        invoke_call(presentation, &presentation->options.declaration->as.scene->snapshots[i], &context, NULL);
      presentation->restoring_scene = false;
    }
  }
  presentation->busy = false;
}

void qa_qvm_mod_presentation_consume(qa_qvm_mod_presentation *presentation,
                                     const qa_app_q3_source_player_event *event, int64_t sequence) {
  current(presentation, NULL);
  if (presentation->busy || strcmp(presentation->phase, "initialized") != 0)
    qa_range_error("Source presentation event requires an idle initialized owner");
  if (sequence < 0) qa_range_error("Invalid source event delivery sequence");
  if (sequence <= presentation->sequence) return;
  if (!matches_module(event->source_module, presentation->options.source) ||
      strcmp(event->source_abi_profile, base_of(presentation)->gameplay.abi_profile) != 0)
    qa_range_error("Source player event belongs to a different gameplay module");
  if (is_scene(presentation)) qa_range_error("Scene presentation owns events through original snapshots");
  const qa_qvm_player_event_storage *storage = &presentation->options.declaration->as.player_events->storage;
  const char *profile = cgame_profile(presentation);
  int64_t slot = (int64_t)event->player_state.client_number;
  if (slot < 0 || slot >= (int64_t)storage->centities.capacity)
    qa_range_error("Source player event exceeds the declared centity array");
  check_int32(event->event);
  check_int32(event->parameter);
  if (!isfinite((double)(float)event->origin.x) || !isfinite((double)(float)event->origin.y) ||
      !isfinite((double)(float)event->origin.z))
    qa_range_error("Source player event has a nonfinite origin");
  presentation->sequence = sequence;
  presentation->busy = true;
  presentation->active_event = event;
  current(presentation, event);
  qa_qvm_memory *memory = presentation->module.memory;
  int64_t entity = (int64_t)storage->centities.address + slot * (int64_t)storage->centities.stride;
  slot_actor *player = find_player(presentation, slot);
  if (player == NULL || !qa_same_actor(player->actor, event->actor)) {
    if (presentation->defaults == NULL) qa_range_error("Source centity defaults are unavailable");
    qa_qvm_memory_write_bytes(memory, entity,
        presentation->defaults + (size_t)(slot * (int64_t)storage->centities.stride),
        (size_t)storage->centities.stride);
    set_player(presentation, slot, event->actor);
  }
  qa_qvm_presentation_context context = build_context(presentation, NULL);
  refresh_context(presentation, &context);
  qa_qvm_writable_view player_view;
  qa_qvm_memory_data_view(memory, (int64_t)storage->player_state, qa_qvm_player_state_bytes(profile), &player_view);
  qa_qvm_write_source_player_state(&player_view, &event->player_state, profile);
  for (size_t i = 0; i < presentation->options.declaration->as.player_events->project_count; i++)
    invoke_call(presentation, &presentation->options.declaration->as.player_events->project[i], &context, event);
  qa_qvm_writable_view state_view;
  qa_qvm_memory_data_view(memory, entity + (int64_t)storage->centities.state,
                          qa_qvm_entity_state_bytes_for(profile), &state_view);
  qa_qvm_writable_view_set_int32(&state_view, 180, event->event, true);
  qa_qvm_writable_view_set_int32(&state_view, 184, event->parameter, true);
  qa_qvm_writable_view origin_view;
  qa_qvm_memory_data_view(memory, entity + (int64_t)storage->centities.origin, 12, &origin_view);
  qa_qvm_writable_view_set_float32(&origin_view, 0, event->origin.x, true);
  qa_qvm_writable_view_set_float32(&origin_view, 4, event->origin.y, true);
  qa_qvm_writable_view_set_float32(&origin_view, 8, event->origin.z, true);
  invoke_call(presentation, &presentation->options.declaration->as.player_events->event, &context, event);
  presentation->busy = false;
  presentation->active_event = NULL;
}

void qa_qvm_mod_presentation_advance(qa_qvm_mod_presentation *presentation, int64_t frame_sequence) {
  current(presentation, NULL);
  if (presentation->busy || strcmp(presentation->phase, "initialized") != 0)
    qa_range_error("Source presentation frame requires an idle initialized owner");
  if (frame_sequence < 0) qa_range_error("Invalid source presentation frame sequence");
  if (frame_sequence <= presentation->frame) return;
  presentation->frame = frame_sequence;
  presentation->busy = true;
  free_bodies(presentation);
  qa_qvm_presentation_context context = build_context(presentation, NULL);
  refresh_context(presentation, &context);
  if (is_scene(presentation)) {
    if (context.scene == NULL) qa_range_error("Scene declaration requires real source snapshots");
    if (accept_scene(presentation, context.scene, false)) {
      for (size_t i = 0; i < presentation->options.declaration->as.scene->snapshot_count; i++)
        invoke_call(presentation, &presentation->options.declaration->as.scene->snapshots[i], &context, NULL);
    }
  }
  for (size_t i = 0; i < base_of(presentation)->frame_count; i++)
    invoke_call(presentation, &base_of(presentation)->frame[i], &context, NULL);
  presentation->busy = false;
}

typedef struct command_validate {
  qa_qvm_mod_presentation *presentation;
} command_validate;

static void run_command_validate(void *ctx) {
  current(((command_validate *)ctx)->presentation, NULL);
}

bool qa_qvm_mod_presentation_console_command(qa_qvm_mod_presentation *presentation,
                                             const char *const *arguments, size_t argument_count) {
  current(presentation, NULL);
  if (presentation->busy || strcmp(presentation->phase, "initialized") != 0)
    qa_range_error("Source console command requires an idle initialized owner");
  presentation->busy = true;
  presentation->has_command_arguments = true;
  const char *const *previous_arguments = presentation->arguments;
  size_t previous_count = presentation->argument_count;
  presentation->arguments = arguments;
  presentation->argument_count = argument_count;
  command_validate validate = {presentation};
  int32_t words[] = {CG_CONSOLE_COMMAND};
  int32_t result = qa_qvm_module_command_async(&presentation->module, words, 1, arguments, argument_count,
                                               run_command_validate, &validate);
  current(presentation, NULL);
  presentation->has_command_arguments = false;
  presentation->arguments = previous_arguments;
  presentation->argument_count = previous_count;
  presentation->busy = false;
  return result != 0;
}

void qa_qvm_mod_presentation_draw_hud(qa_qvm_mod_presentation *presentation, int64_t frame_sequence) {
  current(presentation, NULL);
  if (presentation->busy || strcmp(presentation->phase, "initialized") != 0 || presentation->frame != frame_sequence)
    qa_range_error("Source HUD requires its completed scene frame");
  if (frame_sequence <= presentation->hud_frame) return;
  const qa_qvm_presentation_hud *hud = base_of(presentation)->hud;
  if (hud == NULL) qa_range_error("Source presentation has no HUD admission");
  presentation->hud_frame = frame_sequence;
  presentation->busy = true;
  qa_qvm_presentation_context context = build_context(presentation, NULL);
  for (size_t i = 0; i < hud->frame_count; i++) invoke_call(presentation, &hud->frame[i], &context, NULL);
  presentation->busy = false;
}

void qa_qvm_mod_presentation_release(qa_qvm_mod_presentation *presentation, qa_actor_id actor) {
  slot_actor *players = presentation->players;
  for (size_t i = 0; i < presentation->player_count;) {
    if (qa_same_actor(players[i].actor, actor)) {
      memmove(&players[i], &players[i + 1], (presentation->player_count - i - 1) * sizeof *players);
      presentation->player_count--;
    } else {
      i++;
    }
  }
}

void qa_qvm_mod_presentation_close(qa_qvm_mod_presentation *presentation) {
  if (strcmp(presentation->phase, "closed") == 0) return;
  presentation->phase = "closed";
  for (size_t i = presentation->removal_count; i-- > 0;)
    qa_qvm_module_unbind(&presentation->module, presentation->removal_ids[i]);
  presentation->removal_count = 0;
  free_bodies(presentation);
  presentation->player_count = 0;
  presentation->scene_actor_count = 0;
  snapshot_entry *snapshots = presentation->snapshots;
  for (size_t i = 0; i < presentation->snapshot_count; i++) qa_qvm_source_snapshot_free(&snapshots[i].snapshot);
  presentation->snapshot_count = 0;
  command_entry *commands = presentation->commands;
  for (size_t i = 0; i < presentation->command_count; i++) free((void *)commands[i].arguments);
  presentation->command_count = 0;
  free(presentation->defaults);
  presentation->defaults = NULL;
  if (presentation->has_current_game_state)
    qa_q3_source_game_state_record_destroy(&presentation->current_game_state);
  presentation->has_current_game_state = false;
  qa_qvm_module_retire(&presentation->module);
}
