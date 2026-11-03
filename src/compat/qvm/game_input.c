/* Port of src/compat/qvm/game-input.ts. GPL-2.0-or-later. */
#include "qa/compat/qvm/game_input.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/compat/qvm/client_state_record.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/movement/client_outputs.h"
#include "qa/movement/q3/view.h"

typedef struct client_scope {
  uint64_t sequence;
  double slot;
  qa_mod_client_identity identity;
  qa_qvm_cancellation_scope cancellation;
} client_scope;

typedef struct command_frame {
  uint64_t scope_sequence;
  int64_t address;
} command_frame;

typedef struct removal {
  bool is_unsubscribe;
  size_t id;
  qa_mod_client_unsubscribe unsubscribe;
} removal;

struct qa_qvm_input_binding {
  qa_qvm_input_source source;
  qa_qvm_input_services services;
  removal removals[8];
  size_t removal_count;
  client_scope *clients;
  size_t client_count, client_capacity;
  double *spawning;
  size_t spawning_count, spawning_capacity;
  command_frame *command_frames;
  size_t command_frame_count, command_frame_capacity;
  qa_mod_client_application *current;
  uint64_t next_sequence;
  bool closed;
};

static bool actor_id_equal(qa_actor_id a, qa_actor_id b) {
  return a.slot == b.slot && a.generation == b.generation && a.token == b.token &&
         a.session.token == b.session.token;
}

static int32_t proceed(qa_qvm_module_function_call *call) {
  return strcmp(call->execution, "synchronous") == 0 ? call->proceed(call->call_ctx)
                                                      : call->proceed_async(call->call_ctx);
}

static int32_t call_word(const qa_qvm_module_function_call *call, int64_t byte_offset) {
  if (byte_offset < 0 || byte_offset > (int64_t)call->words_length - 4)
    qa_range_error("Offset is outside the bounds of the DataView");
  const uint8_t *p = call->words + byte_offset;
  uint32_t bits = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
                  ((uint32_t)p[3] << 24);
  return (int32_t)bits;
}

static void *grow(void *array, size_t *capacity, size_t count, size_t size) {
  if (count < *capacity) return array;
  size_t next = *capacity == 0 ? 8 : *capacity * 2;
  void *grown = realloc(array, next * size);
  if (grown == NULL) qa_range_error("out of memory");
  *capacity = next;
  return grown;
}

static void push_removal_bind(qa_qvm_input_binding *b, size_t id) {
  if (b->removal_count >= sizeof b->removals / sizeof b->removals[0])
    qa_range_error("out of memory");
  b->removals[b->removal_count++] = (removal){false, id, {NULL, NULL}};
}

static void push_removal_unsubscribe(qa_qvm_input_binding *b, qa_mod_client_unsubscribe u) {
  if (b->removal_count >= sizeof b->removals / sizeof b->removals[0])
    qa_range_error("out of memory");
  b->removals[b->removal_count++] = (removal){true, 0, u};
}

static double client_pointer(qa_qvm_input_binding *b, double slot) {
  qa_qvm_game *game = b->source.game;
  const qa_qvm_input_definition *definition = &b->source.definition;
  qa_qvm_game_data_state located = qa_qvm_game_data_checkpoint(&game->data);
  if (located.entity_stride != definition->entity_stride ||
      located.client_stride != definition->client_stride)
    qa_range_error("Located QVM input records differ from their artifact declaration");
  qa_qvm_writable_view entity;
  qa_qvm_game_data_entity_bytes(&game->data, slot, &entity);
  int32_t client = qa_qvm_writable_view_get_int32(&entity, (int64_t)definition->client_pointer, true);
  double expected = located.clients_word + slot * located.client_stride;
  qa_qvm_writable_view public_player;
  qa_qvm_game_data_public_player_bytes(&game->data, slot, &public_player);
  if ((double)client != expected)
    qa_range_error("QVM input entity does not own its located public player state");
  return (double)client;
}

static bool spawning_includes(const qa_qvm_input_binding *b, double slot) {
  for (size_t i = 0; i < b->spawning_count; i++)
    if (b->spawning[i] == slot) return true;
  return false;
}

typedef struct spawn_finish_ctx {
  qa_qvm_input_binding *binding;
  double slot;
} spawn_finish_ctx;

static void spawn_finish_perform(void *ctx) {
  spawn_finish_ctx *finish = ctx;
  qa_qvm_input_binding *b = finish->binding;
  qa_mod_client_identity identity;
  if (b->services.identity(b->services.ctx, finish->slot, &identity) &&
      b->services.live(b->services.ctx, identity) && b->services.spawned != NULL)
    b->services.spawned(b->services.ctx, identity);
}

/* TS catch around proceed: spawning.pop(). */
static void spawn_unwind(void *ctx) {
  qa_qvm_input_binding *b = ctx;
  b->spawning_count--;
}

static int32_t spawn_hook(void *ctx, qa_qvm_module_function_call *call) {
  qa_qvm_input_binding *b = ctx;
  if (!qa_mod_client_applications_active(b->services.applications) &&
      b->services.spawned == NULL)
    return proceed(call);
  double slot = qa_qvm_game_data_number_from_pointer(&b->source.game->data, (double)call_word(call, 0));
  b->spawning = grow(b->spawning, &b->spawning_capacity, b->spawning_count, sizeof *b->spawning);
  b->spawning[b->spawning_count++] = slot;
  qa_cleanup guard;
  qa_cleanup_push(&guard, spawn_unwind, b);
  int32_t result = proceed(call);
  qa_cleanup_pop(&guard);
  spawn_finish_ctx finish = {b, slot};
  call->effect(call->call_ctx, spawn_finish_perform, &finish);
  b->spawning_count--;
  return result;
}

/* TS catch around proceed: clients.pop(). */
static void envelope_unwind(void *ctx) {
  qa_qvm_input_binding *b = ctx;
  b->client_count--;
}

static int32_t envelope(qa_qvm_input_binding *b, qa_qvm_module_function_call *call, double slot) {
  if (b->source.retired(b->source.ctx, slot)) return 0;
  if (!qa_mod_client_applications_active(b->services.applications) || spawning_includes(b, slot))
    return proceed(call);
  qa_mod_client_identity identity;
  if (!b->services.identity(b->services.ctx, slot, &identity) ||
      !b->services.live(b->services.ctx, identity))
    return proceed(call);
  (void)client_pointer(b, slot);
  client_scope scope = {++b->next_sequence, slot, identity,
                        call->cancellation_scope(call->call_ctx)};
  b->clients = grow(b->clients, &b->client_capacity, b->client_count, sizeof *b->clients);
  b->clients[b->client_count++] = scope;
  qa_cleanup guard;
  qa_cleanup_push(&guard, envelope_unwind, b);
  int32_t result = proceed(call);
  qa_cleanup_pop(&guard);
  b->client_count--;
  return result;
}

static int32_t think_hook(void *ctx, qa_qvm_module_function_call *call) {
  return envelope(ctx, call, (double)call_word(call, 0));
}

static int32_t run_client_hook(void *ctx, qa_qvm_module_function_call *call) {
  qa_qvm_input_binding *b = ctx;
  return envelope(b, call,
                  qa_qvm_game_data_number_from_pointer(&b->source.game->data,
                                                       (double)call_word(call, 0)));
}

static qa_qvm_cancellation_scope cancellation_of(qa_qvm_input_binding *b,
                                                  const client_scope *scope) {
  for (size_t i = 0; i < b->client_count; i++)
    if (actor_id_equal(b->clients[i].identity.actor, scope->identity.actor))
      return b->clients[i].cancellation;
  return scope->cancellation;
}

static int32_t check_live(qa_qvm_input_binding *b, qa_qvm_module_function_call *call,
                          const client_scope *scope) {
  if (b->services.live(b->services.ctx, scope->identity)) return 0;
  b->source.retiring(b->source.ctx, scope->slot, scope->identity);
  (void)b->source.disconnect(b->source.ctx, scope->slot, scope->identity, call);
  { call->cancel_function(call->call_ctx, cancellation_of(b, scope)); qa_range_error("QVM function scope cancelled"); }
}

typedef struct encode_aim_ctx {
  qa_qvm_input_binding *binding;
  double slot;
} encode_aim_ctx;

static qa_user_command encode_aim(void *ctx, qa_vec3 aim, qa_user_command effective) {
  encode_aim_ctx *encode = ctx;
  if (effective.kind != QA_USER_COMMAND_Q3)
    qa_range_error("QVM aim output changed the source command dialect");
  qa_q3_player_state current;
  double stats[16], persistent[16], powerups[16], ammo[16];
  qa_qvm_game_data_copy_player_state(&encode->binding->source.game->data, encode->slot, &current,
                                     stats, persistent, powerups, ammo);
  double degrees[3] = {aim.x, aim.y, aim.z};
  for (size_t i = 0; i < 3; i++) {
    double word = (double)(qa_int32(trunc(degrees[i] * 65536 / 360)) & 65535) -
                  current.delta_angle_words.values[i];
    effective.as.q3.angle_words.values[i] = word;
  }
  return effective;
}

typedef struct apply_ctx {
  qa_qvm_input_binding *binding;
  qa_qvm_module_function_call *call;
  client_scope scope;
  int64_t movement;
  const char *kind;
} apply_ctx;

typedef struct finish_effect_ctx {
  qa_qvm_input_binding *binding;
  qa_mod_client_application **application;
} finish_effect_ctx;

static void finish_effect_perform(void *ctx) {
  finish_effect_ctx *effect = ctx;
  qa_mod_client_applications_finish(effect->binding->services.applications, *effect->application,
                                    false);
  *effect->application = NULL;
}

typedef struct apply_state {
  qa_qvm_input_binding *binding;
  client_scope scope;
  qa_mod_client_application *previous, *application, *application_owned;
  command_frame frame_entry;
  int64_t address;
  uint8_t *suspended;
  size_t suspended_length;
  bool failed, finished;
  bool has_projected_type;
  int32_t projected_before, projected_value;
} apply_state;

/* TS finish(): runs once, on completion or when the scope is cancelled. */
static void apply_finish(void *ctx) {
  apply_state *state = ctx;
  if (state->finished) return;
  state->finished = true;
  qa_qvm_input_binding *b = state->binding;
  qa_qvm_memory *memory = b->source.game->module.memory;
  qa_mod_client_applications_finish(b->services.applications, state->application, state->failed);
  if (state->has_projected_type && b->services.live(b->services.ctx, state->scope.identity)) {
    qa_qvm_writable_view type_view;
    qa_qvm_memory_view(memory, (int64_t)client_pointer(b, state->scope.slot) + 4, 4, 0, &type_view);
    if (qa_qvm_writable_view_get_int32(&type_view, 0, true) == state->projected_value)
      qa_qvm_writable_view_set_int32(&type_view, 0, state->projected_before, true);
  }
  b->current = state->previous;
  for (size_t i = b->command_frame_count; i-- > 0;) {
    if (b->command_frames[i].scope_sequence == state->frame_entry.scope_sequence &&
        b->command_frames[i].address == state->frame_entry.address) {
      memmove(&b->command_frames[i], &b->command_frames[i + 1],
              (b->command_frame_count - i - 1) * sizeof *b->command_frames);
      b->command_frame_count--;
      break;
    }
  }
  if (state->suspended != NULL) {
    qa_qvm_memory_write_bytes(memory, state->address, state->suspended, state->suspended_length);
    free(state->suspended);
    state->suspended = NULL;
  }
  if (state->application_owned != NULL) qa_mod_client_application_dispose(state->application_owned);
}

static int32_t apply_run(void *ctx) {
  apply_ctx *apply = ctx;
  qa_qvm_input_binding *b = apply->binding;
  qa_qvm_module_function_call *call = apply->call;
  client_scope scope = apply->scope;
  qa_qvm_game *game = b->source.game;
  const qa_qvm_input_definition *definition = &b->source.definition;
  qa_qvm_abi_profile profile = qa_qvm_module_get_abi_profile(&game->module);
  qa_qvm_memory *memory = game->module.memory;

  qa_q3_player_state state;
  double stats[16], persistent[16], powerups[16], ammo[16];
  qa_qvm_game_data_copy_player_state(&game->data, scope.slot, &state, stats, persistent, powerups,
                                     ammo);
  int64_t address = apply->movement + 4;
  qa_qvm_writable_view command_view;
  qa_qvm_memory_view(memory, address, QA_QVM_USER_COMMAND_BYTES, 0, &command_view);
  qa_qvmx_view view = {command_view.bytes + command_view.start, (size_t)command_view.length};
  qa_qvmx_wire_user_command command = qa_qvm_read_user_command(view, profile);
  qa_user_command input;
  memset(&input, 0, sizeof input);
  input.kind = QA_USER_COMMAND_Q3;
  input.as.q3.server_time_milliseconds = command.server_time;
  for (size_t i = 0; i < 3; i++) input.as.q3.angle_words.values[i] = command.angles[i];
  input.as.q3.buttons = command.buttons;
  input.as.q3.weapon = command.weapon;
  input.as.q3.forward_move = command.forwardmove;
  input.as.q3.right_move = command.rightmove;
  input.as.q3.up_move = command.upmove;

  bool overlapping = false;
  for (size_t i = 0; i < b->command_frame_count; i++)
    overlapping = overlapping ||
                  (b->command_frames[i].address == address &&
                   b->command_frames[i].scope_sequence != scope.sequence);
  uint8_t *suspended = NULL;
  size_t suspended_length = 0;
  if (overlapping) {
    suspended_length = QA_QVM_USER_COMMAND_BYTES;
    if (address < 0 || (size_t)address > memory->length) suspended_length = 0;
    else if ((size_t)address + suspended_length > memory->length)
      suspended_length = memory->length - (size_t)address;
    suspended = malloc(suspended_length == 0 ? 1 : suspended_length);
    if (suspended == NULL) qa_range_error("out of memory");
    if (suspended_length > 0) memcpy(suspended, memory->bytes + address, suspended_length);
  }
  command_frame frame_entry = {scope.sequence, address};
  b->command_frames = grow(b->command_frames, &b->command_frame_capacity, b->command_frame_count,
                           sizeof *b->command_frames);
  b->command_frames[b->command_frame_count++] = frame_entry;

  double remaining = command.server_time - state.command_time_milliseconds;
  bool is_client_command = strcmp(apply->kind, "client-command") == 0;
  double milliseconds = remaining;
  if (is_client_command) {
    if (milliseconds > 1000) milliseconds = 1000;
    if (milliseconds < 0) milliseconds = 0;
  } else {
    if (milliseconds > 200) milliseconds = 200;
    if (milliseconds < 1) milliseconds = 1;
  }
  apply_state run;
  memset(&run, 0, sizeof run);
  run.binding = b;
  run.scope = scope;
  run.previous = b->current;
  run.frame_entry = frame_entry;
  run.address = address;
  run.suspended = suspended;
  run.suspended_length = suspended_length;
  run.failed = true;
  qa_mod_client_application *previous = run.previous;
  qa_cleanup guard;
  qa_cleanup_push(&guard, apply_finish, &run);

  qa_vec3 command_angles = {command.angles[0], command.angles[1], command.angles[2]};
  qa_vec3 delta_angles = {state.delta_angle_words.values[0], state.delta_angle_words.values[1],
                          state.delta_angle_words.values[2]};
  qa_q3_view_angles_result aim =
      qa_q3_view_angles(command_angles, delta_angles, state.view_angles, stats[0],
                        state.movement_type, definition->intermission, definition->intermission_count);
  bool same_actor_previous =
      previous != NULL && actor_id_equal(previous->identity.actor, scope.identity.actor);
  qa_frame_context frame_context = b->services.frame(b->services.ctx);
  frame_context.phase = QA_FRAME_PHASE_CLIENT_COMMAND;
  frame_context.elapsed = (qa_source_time){QA_SOURCE_TIME_MILLISECONDS, milliseconds};
  qa_mod_client_application_input begin_input;
  memset(&begin_input, 0, sizeof begin_input);
  begin_input.identity = scope.identity;
  begin_input.scope = apply->kind;
  begin_input.command = input;
  begin_input.has_arsenal = true;
  begin_input.arsenal =
      !is_client_command && same_actor_previous && previous->has_arsenal ? &previous->arsenal : NULL;
  begin_input.has_controls = true;
  begin_input.controls_impulse =
      !is_client_command && same_actor_previous ? previous->controls_impulse : 0;
  begin_input.angle_space = "source-relative";
  begin_input.absolute_aim = aim.angles;
  begin_input.frame = frame_context;
  begin_input.accepted = b->services.accepted(b->services.ctx, scope.identity.actor);
  begin_input.has_parent_invocation = previous != NULL;
  begin_input.parent_invocation = previous != NULL ? previous->invocation : 0;
  encode_aim_ctx encode = {b, scope.slot};
  run.application = qa_mod_client_applications_begin(b->services.applications, begin_input, encode_aim,
                                                     &encode);
  run.application_owned = run.application;
  b->current = run.application;

  (void)check_live(b, call, &scope);
  {
    qa_mod_client_movement_outputs output;
    bool has_output = !is_client_command && b->services.client_outputs != NULL &&
                      b->services.client_outputs(b->services.ctx, scope.identity.actor, &output);
    if (has_output && output.has_mode) {
      if (!definition->has_movement_modes)
        qa_range_error("Original QVM movement modes require their exact source declaration");
      double value = output.mode == QA_MOD_CLIENT_MOVEMENT_NORMAL ? definition->movement_mode_normal
                     : output.mode == QA_MOD_CLIENT_MOVEMENT_NOCLIP ? definition->movement_mode_noclip
                                                                    : definition->movement_mode_freeze;
      qa_qvm_writable_view type_view;
      qa_qvm_memory_view(memory, (int64_t)client_pointer(b, scope.slot) + 4, 4, 0, &type_view);
      run.has_projected_type = true;
      run.projected_before = qa_qvm_writable_view_get_int32(&type_view, 0, true);
      run.projected_value = qa_int32(value);
      qa_qvm_writable_view_set_int32(&type_view, 0, run.projected_value, true);
    }
    qa_user_command base = run.application != NULL ? run.application->command : input;
    bool has_stance = has_output && output.has_stance;
    qa_user_command effective = qa_client_stance_command(base, has_stance, has_stance && output.stance);
    bool unchanged = run.application == NULL && !has_stance;
    if (!unchanged) {
      if (effective.kind != QA_USER_COMMAND_Q3)
        qa_range_error("QVM input output changed the source command dialect");
      qa_qvmx_wire_user_command written;
      memset(&written, 0, sizeof written);
      written.server_time = effective.as.q3.server_time_milliseconds;
      for (size_t i = 0; i < 3; i++) written.angles[i] = effective.as.q3.angle_words.values[i];
      written.buttons = effective.as.q3.buttons;
      written.weapon = effective.as.q3.weapon;
      written.forwardmove = effective.as.q3.forward_move;
      written.rightmove = effective.as.q3.right_move;
      written.upmove = effective.as.q3.up_move;
      qa_qvm_writable_view write_view;
      qa_qvm_memory_view(memory, address, QA_QVM_USER_COMMAND_BYTES, 0, &write_view);
      qa_qvmx_view target = {write_view.bytes + write_view.start, (size_t)write_view.length};
      qa_qvm_write_user_command(target, &written, profile, "update");
    }
  }
  int32_t result = proceed(call);
  run.failed = false;
  {
    finish_effect_ctx effect = {b, &run.application};
    call->effect(call->call_ctx, finish_effect_perform, &effect);
  }
  (void)check_live(b, call, &scope);
  qa_cleanup_pop(&guard);
  apply_finish(&run);
  return result;
}

static int32_t movement_hook(qa_qvm_input_binding *b, qa_qvm_module_function_call *call, const char *kind) {
  if (!qa_mod_client_applications_active(b->services.applications) || b->client_count == 0)
    return proceed(call);
  qa_qvm_game *game = b->source.game;
  int32_t movement_word = call_word(call, 0);
  qa_qvm_writable_view pointer_view;
  qa_qvm_memory_view(game->module.memory, movement_word, 4, 0, &pointer_view);
  int32_t pointer = qa_qvm_writable_view_get_int32(&pointer_view, 0, true);
  const client_scope *scope = NULL;
  for (size_t i = b->client_count; i-- > 0;) {
    if (client_pointer(b, b->clients[i].slot) == (double)pointer) {
      scope = &b->clients[i];
      break;
    }
  }
  if (scope == NULL || spawning_includes(b, scope->slot)) return proceed(call);
  apply_ctx apply = {b, call, *scope, movement_word, kind};
  return b->source.movement(b->source.ctx, scope->slot, apply_run, &apply);
}

typedef struct movement_run_ctx {
  qa_qvm_input_binding *binding;
  qa_qvm_module_function_call *call;
  const char *kind;
} movement_run_ctx;

static int32_t movement_run(void *ctx) {
  movement_run_ctx *run = ctx;
  return movement_hook(run->binding, run->call, run->kind);
}

static int32_t run_movement(qa_qvm_input_binding *b, qa_qvm_module_function_call *call, const char *kind) {
  if (b->services.movement == NULL) return movement_hook(b, call, kind);
  movement_run_ctx run = {b, call, kind};
  return b->services.movement(b->services.ctx, call, kind, movement_run, &run);
}

static int32_t move_hook(void *ctx, qa_qvm_module_function_call *call) {
  return run_movement(ctx, call, "client-command");
}

static int32_t slice_hook(void *ctx, qa_qvm_module_function_call *call) {
  return run_movement(ctx, call, "movement-slice");
}

static void release_listener(void *ctx, qa_actor_id actor) {
  qa_qvm_input_binding *b = ctx;
  for (size_t i = 0; i < b->client_count; i++)
    if (actor_id_equal(b->clients[i].identity.actor, actor))
      b->source.retiring(b->source.ctx, b->clients[i].slot, b->clients[i].identity);
}

static size_t bind_entry(qa_qvm_input_binding *b, double entry, qa_qvm_function_hook_fn hook) {
  qa_guest_callback_reference reference;
  memset(&reference, 0, sizeof reference);
  reference.kind = QA_GUEST_CALLBACK_QVM;
  reference.module = b->source.definition.module;
  reference.instruction_index = entry;
  return qa_qvm_module_bind_function(&b->source.game->module, &reference, hook, b);
}

qa_qvm_input_binding *qa_qvm_input_binding_create(qa_qvm_input_source source,
                                                  qa_qvm_input_services services) {
  qa_qvm_input_binding *b = calloc(1, sizeof *b);
  if (b == NULL) return NULL;
  b->source = source;
  b->services = services;
  const qa_qvm_input_definition *d = &source.definition;
  push_removal_bind(b, bind_entry(b, d->entry_client_spawn, spawn_hook));
  push_removal_bind(b, bind_entry(b, d->entry_client_think, think_hook));
  push_removal_bind(b, bind_entry(b, d->entry_run_client, run_client_hook));
  push_removal_bind(b, bind_entry(b, d->entry_move, move_hook));
  push_removal_bind(b, bind_entry(b, d->entry_slice, slice_hook));
  push_removal_unsubscribe(b, services.on_release(services.ctx, release_listener, b));
  return b;
}

void qa_qvm_input_binding_close(qa_qvm_input_binding *b) {
  if (b == NULL || b->closed) return;
  b->closed = true;
  while (b->removal_count > 0) {
    removal entry = b->removals[--b->removal_count];
    if (entry.is_unsubscribe) {
      if (entry.unsubscribe.invoke != NULL) entry.unsubscribe.invoke(entry.unsubscribe.ctx);
    } else {
      qa_qvm_module_unbind(&b->source.game->module, entry.id);
    }
  }
}

void qa_qvm_input_binding_destroy(qa_qvm_input_binding *b) {
  if (b == NULL) return;
  qa_qvm_input_binding_close(b);
  free(b->clients);
  free(b->spawning);
  free(b->command_frames);
  free(b);
}
