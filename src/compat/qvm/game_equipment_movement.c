/* Port of src/compat/qvm/game-equipment-movement.ts. GPL-2.0-or-later. */
#include "qa/compat/qvm/game_equipment_movement.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/compat/qvm/client_state_record.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"

typedef struct frame {
  bool present;
  uint64_t sequence;
  bool has_accepted_bounds;
  qa_bounds accepted_bounds;
  qa_actor_id actor;
  int32_t player, movement;
  const qa_fixed_movement_pose *pose;
  bool owns_holdable_input;
  const qa_movement_body_shape *body;
} frame;

struct qa_qvm_equipment_movement {
  qa_qvm_game *game;
  qa_qvm_equipment_movement_profile profile;
  qa_qvm_equipment_movement_services services;
  int64_t body_scratch;
  frame *frames;
  size_t frame_count, frame_capacity;
  uint64_t next_sequence;
  bool has_removal;
  size_t removal;
};

static bool is_integer(double v) { return isfinite(v) && v == trunc(v); }

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

static int32_t guest_i32(qa_qvm_module_function_call *call, int64_t address) {
  qa_qvm_writable_view view;
  qa_qvm_memory_data_view(call->guest, address, 4, &view);
  return qa_qvm_writable_view_get_int32(&view, 0, true);
}

static qa_vec3 guest_vector(qa_qvm_module_function_call *call, int64_t address) {
  qa_qvm_writable_view view;
  qa_qvm_memory_data_view(call->guest, address, 12, &view);
  qa_vec3 v = {qa_qvm_writable_view_get_float32(&view, 0, true),
               qa_qvm_writable_view_get_float32(&view, 4, true),
               qa_qvm_writable_view_get_float32(&view, 8, true)};
  return v;
}

static void guest_write_vector(qa_qvm_module_function_call *call, int64_t address, qa_vec3 value) {
  qa_qvm_writable_view view;
  qa_qvm_memory_data_view(call->guest, address, 12, &view);
  qa_qvm_writable_view_set_float32(&view, 0, value.x, true);
  qa_qvm_writable_view_set_float32(&view, 4, value.y, true);
  qa_qvm_writable_view_set_float32(&view, 8, value.z, true);
}

static bool admit(qa_qvm_equipment_movement *m, qa_qvm_module_function_call *call, frame *out) {
  int32_t movement_word = call_word(call, 0);
  int32_t player = guest_i32(call, movement_word);
  qa_qvm_game_data_state located = qa_qvm_game_data_checkpoint(&m->game->data);
  double slot = ((double)player - located.clients_word) / located.client_stride;
  if (!is_integer(slot) || slot < 0 || slot >= qa_qvm_game_data_get_num_clients(&m->game->data))
    return false;
  qa_actor_id actor;
  if (!m->services.actor(m->services.ctx, slot, &actor)) return false;
  if (!m->services.live(m->services.ctx, actor)) return false;
  qa_qvm_equipment_motion equipment;
  if (!m->services.equipment(m->services.ctx, actor, &equipment)) return false;
  if (!isfinite(equipment.speed_multiplier) || equipment.speed_multiplier <= 0)
    qa_range_error("Selected movement speed must be positive and finite");
  qa_qvm_writable_view speed;
  qa_qvm_memory_data_view(call->guest, (int64_t)player + 52, 4, &speed);
  double scaled = trunc((double)qa_float32((double)qa_qvm_writable_view_get_int32(&speed, 0, true) *
                                            equipment.speed_multiplier));
  qa_qvm_writable_view_set_int32(&speed, 0, qa_int32(scaled), true);
  memset(out, 0, sizeof *out);
  out->present = true;
  out->actor = actor;
  out->player = player;
  out->movement = movement_word;
  out->pose = equipment.pose;
  out->owns_holdable_input = equipment.owns_holdable_input;
  out->body = equipment.body;
  return true;
}

static frame *top_frame(qa_qvm_equipment_movement *m) {
  return m->frame_count == 0 ? NULL : &m->frames[m->frame_count - 1];
}

static frame *current(qa_qvm_equipment_movement *m, qa_qvm_module_function_call *call) {
  frame *f = top_frame(m);
  if (f == NULL || !f->present) return NULL;
  if (!m->services.live(m->services.ctx, f->actor)) return NULL;
  if (f->pose == NULL && f->body == NULL) return NULL;
  if (guest_i32(call, (int64_t)m->profile.movement_global) != f->movement) return NULL;
  return f;
}

typedef struct slice_run_ctx {
  qa_qvm_equipment_movement *movement;
  qa_qvm_module_function_call *call;
  uint64_t sequence;
} slice_run_ctx;

static const char *slice_run(void *ctx, void *control_ctx) {
  (void)control_ctx;
  slice_run_ctx *run = ctx;
  qa_qvm_equipment_movement *m = run->movement;
  frame *f = current(m, run->call);
  if (f == NULL || f->sequence != run->sequence) return "execute";
  qa_qvm_writable_view command;
  qa_qvm_memory_data_view(run->call->guest, (int64_t)f->movement + 4, 24, &command);
  qa_qvm_user_command_layout layout =
      qa_qvm_get_user_command_layout(qa_qvm_module_get_abi_profile(&m->game->module));
  qa_qvm_writable_view_set_int8(&command, (int64_t)layout.forward, 0);
  qa_qvm_writable_view_set_int8(&command, (int64_t)layout.right, 0);
  qa_qvm_writable_view_set_int8(&command, (int64_t)layout.up, 0);
  qa_qvm_writable_view velocity;
  qa_qvm_memory_data_view(run->call->guest, (int64_t)f->player + 32, 12, &velocity);
  qa_qvm_writable_view_set_float32(&velocity, 0, 0, true);
  qa_qvm_writable_view_set_float32(&velocity, 4, 0, true);
  qa_qvm_writable_view_set_float32(&velocity, 8, 0, true);
  return "skip";
}

static int32_t slice(qa_qvm_equipment_movement *m, qa_qvm_module_function_call *call,
                     int32_t (*run)(void *ctx), void *run_ctx) {
  frame *f = top_frame(m);
  if (f != NULL && f->present && f->pose != NULL && m->services.live(m->services.ctx, f->actor)) {
    slice_run_ctx run_ctx_local = {m, call, f->sequence};
    qa_qvm_module_region_binding binding = {(int64_t)m->profile.locomotion_entry,
                                      (int64_t)m->profile.locomotion_join, &run_ctx_local,
                                      slice_run, NULL};
    call->regions(call->call_ctx, &binding, 1);
    return run(run_ctx);
  }
  return run(run_ctx);
}

/* TS catch: a cancelled scope pops its frame. */
static void movement_unwind(void *ctx) {
  qa_qvm_equipment_movement *m = ctx;
  m->frame_count--;
}

int32_t qa_qvm_equipment_movement_movement(qa_qvm_equipment_movement *m,
                                           qa_qvm_module_function_call *call, const char *kind,
                                           int32_t (*run)(void *ctx), void *run_ctx) {
  if (strcmp(kind, "movement-slice") == 0) return slice(m, call, run, run_ctx);
  frame f;
  bool present = admit(m, call, &f);
  if (!present) memset(&f, 0, sizeof f);
  f.present = present;
  f.sequence = ++m->next_sequence;
  if (m->frame_count == m->frame_capacity) {
    size_t capacity = m->frame_capacity == 0 ? 8 : m->frame_capacity * 2;
    frame *grown = realloc(m->frames, capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("out of memory");
    m->frames = grown;
    m->frame_capacity = capacity;
  }
  m->frames[m->frame_count++] = f;
  uint64_t sequence = f.sequence;
  qa_cleanup guard;
  qa_cleanup_push(&guard, movement_unwind, m);
  int32_t result = run(run_ctx);
  qa_cleanup_pop(&guard);
  frame *top = top_frame(m);
  if (top == NULL || top->sequence != sequence)
    qa_range_error("Selected movement scope completed out of order");
  m->frame_count--;
  return result;
}

static int32_t weapon_read(qa_qvm_memory *guest, int64_t address, double bytes) {
  qa_qvm_writable_view word;
  qa_qvm_memory_data_view(guest, address, (int64_t)bytes, &word);
  return bytes == 1 ? qa_qvm_writable_view_get_uint8(&word, 0)
                    : qa_qvm_writable_view_get_int32(&word, 0, true);
}

static void weapon_write(qa_qvm_memory *guest, int64_t address, double bytes, int32_t value) {
  qa_qvm_writable_view word;
  qa_qvm_memory_data_view(guest, address, (int64_t)bytes, &word);
  if (bytes == 1)
    qa_qvm_writable_view_set_uint8(&word, 0, (uint32_t)value);
  else
    qa_qvm_writable_view_set_int32(&word, 0, value, true);
}

bool qa_qvm_equipment_movement_prepare_weapon(qa_qvm_equipment_movement *m, qa_actor_id actor,
                                              qa_qvm_module_function_call *call,
                                              qa_qvm_equipment_weapon_restore *out) {
  frame *f = top_frame(m);
  if (f == NULL || !f->present || !actor_id_equal(f->actor, actor) ||
      !m->services.live(m->services.ctx, f->actor) || !f->owns_holdable_input ||
      guest_i32(call, (int64_t)m->profile.movement_global) != f->movement)
    return false;
  qa_qvm_user_command_layout layout =
      qa_qvm_get_user_command_layout(qa_qvm_module_get_abi_profile(&m->game->module));
  int64_t address = (int64_t)f->movement + 4 + (int64_t)layout.buttons_offset;
  int32_t previous = weapon_read(call->guest, address, layout.buttons_bytes);
  int32_t projected = previous & ~4;
  weapon_write(call->guest, address, layout.buttons_bytes, projected);
  out->movement = m;
  out->guest = call->guest;
  out->address = address;
  out->bytes = layout.buttons_bytes;
  out->previous = previous;
  out->projected = projected;
  out->actor = f->actor;
  return true;
}

void qa_qvm_equipment_weapon_restore_call(qa_qvm_equipment_weapon_restore *restore) {
  qa_qvm_equipment_movement *m = restore->movement;
  if (m->services.live(m->services.ctx, restore->actor) &&
      weapon_read(restore->guest, restore->address, restore->bytes) == restore->projected)
    weapon_write(restore->guest, restore->address, restore->bytes, restore->previous);
}

typedef struct apply_ctx {
  qa_qvm_module_function_call *call;
  qa_qvm_equipment_movement *movement;
  frame *frame;
  const qa_movement_body_shape *body;
  const qa_bounds *accepted;
  const qa_bounds *requested;
  int32_t previous_duck, previous_height;
} apply_ctx;

static void apply_perform(void *ctx) {
  apply_ctx *apply = ctx;
  apply->body->current_actor(apply->body->ctx);
  if (apply->accepted != apply->requested) {
    qa_qvm_writable_view flags, height;
    qa_qvm_memory_data_view(apply->call->guest, (int64_t)apply->frame->player + 12, 4, &flags);
    qa_qvm_writable_view_set_int32(
        &flags, 0,
        (qa_qvm_writable_view_get_int32(&flags, 0, true) & ~1) | apply->previous_duck, true);
    qa_qvm_memory_data_view(apply->call->guest, (int64_t)apply->frame->player + 164, 4, &height);
    qa_qvm_writable_view_set_int32(&height, 0, apply->previous_height, true);
  }
  apply->frame->accepted_bounds = *apply->accepted;
  apply->frame->has_accepted_bounds = true;
  guest_write_vector(apply->call, (int64_t)apply->frame->movement +
                                      (int64_t)apply->movement->profile.mins,
                     apply->accepted->min);
  guest_write_vector(apply->call, (int64_t)apply->frame->movement +
                                      (int64_t)apply->movement->profile.maxs,
                     apply->accepted->max);
}

typedef struct restore_ctx {
  qa_qvm_module_function_call *call;
  int64_t at;
  const uint8_t *saved;
  size_t length;
} restore_ctx;

static void restore_perform(void *ctx) {
  restore_ctx *restore = ctx;
  qa_qvm_memory_write_bytes(restore->call->guest, restore->at, restore->saved, restore->length);
}

/* TS catch: restore(); throw. */
static void restore_unwind(void *ctx) {
  restore_ctx *restore = ctx;
  restore_perform(restore);
  free((void *)restore->saved);
}

typedef struct scratch_ctx {
  qa_qvm_module_function_call *call;
  qa_qvm_equipment_movement *movement;
  frame *frame;
  int64_t at;
  const qa_bounds *requested;
} scratch_ctx;

static void scratch_perform(void *ctx) {
  scratch_ctx *scratch = ctx;
  guest_write_vector(scratch->call, scratch->at + 56,
                     guest_vector(scratch->call, (int64_t)scratch->frame->player + 20));
  guest_write_vector(scratch->call, scratch->at + 68, scratch->requested->min);
  guest_write_vector(scratch->call, scratch->at + 80, scratch->requested->max);
}

static int32_t body_shape(qa_qvm_equipment_movement *m, qa_qvm_module_function_call *call, frame *f);

static int32_t duck_hook(void *ctx, qa_qvm_module_function_call *call) {
  qa_qvm_equipment_movement *m = ctx;
  frame *f = current(m, call);
  if (f == NULL) return proceed(call);
  if (f->pose == NULL) return body_shape(m, call, f);
  const qa_fixed_movement_pose *pose = f->pose;
  qa_qvm_writable_view flags;
  qa_qvm_memory_data_view(call->guest, (int64_t)f->player + 12, 4, &flags);
  int32_t value = qa_qvm_writable_view_get_int32(&flags, 0, true);
  qa_qvm_writable_view_set_int32(&flags, 0, pose->crouched ? value | 1 : value & ~1, true);
  qa_qvm_writable_view height;
  qa_qvm_memory_data_view(call->guest, (int64_t)f->player + 164, 4, &height);
  qa_qvm_writable_view_set_int32(&height, 0, qa_int32(pose->view_height), true);
  guest_write_vector(call, (int64_t)f->movement + (int64_t)m->profile.mins, pose->bounds.min);
  guest_write_vector(call, (int64_t)f->movement + (int64_t)m->profile.maxs, pose->bounds.max);
  return 0;
}

static int32_t body_finish(qa_qvm_equipment_movement *m, qa_qvm_module_function_call *call, frame *f,
                           const qa_movement_body_shape *body, int32_t previous_duck,
                           int32_t previous_height, int32_t result) {
  body->current_actor(body->ctx);
  qa_bounds source = {guest_vector(call, (int64_t)f->movement + (int64_t)m->profile.mins),
                      guest_vector(call, (int64_t)f->movement + (int64_t)m->profile.maxs)};
  const qa_bounds *requested = body->has_requested ? &body->requested : &source;
  const qa_bounds *previous = f->has_accepted_bounds ? &f->accepted_bounds : &body->current;
  bool expands = requested->min.x < previous->min.x || requested->min.y < previous->min.y ||
                 requested->min.z < previous->min.z || requested->max.x > previous->max.x ||
                 requested->max.y > previous->max.y || requested->max.z > previous->max.z;
  if (!expands) {
    apply_ctx apply = {call, m, f, body, requested, requested, previous_duck, previous_height};
    call->effect(call->call_ctx, apply_perform, &apply);
    return result;
  }
  int64_t at = m->body_scratch;
  size_t saved_length = 92;
  if (at < 0 || (size_t)at > call->memory_length) saved_length = 0;
  else if ((size_t)at + saved_length > call->memory_length)
    saved_length = call->memory_length - (size_t)at;
  uint8_t *saved = malloc(saved_length == 0 ? 1 : saved_length);
  if (saved == NULL) qa_range_error("out of memory");
  if (saved_length > 0) memcpy(saved, call->memory + at, saved_length);
  restore_ctx restore = {call, at, saved, saved_length};
  qa_cleanup guard;
  qa_cleanup_push(&guard, restore_unwind, &restore);
  {
    scratch_ctx scratch = {call, m, f, at, requested};
    call->effect(call->call_ctx, scratch_perform, &scratch);
  }
  int32_t callback = guest_i32(call, (int64_t)f->movement + (int64_t)m->profile.body_trace_callback);
  int32_t mask = guest_i32(call, (int64_t)f->movement + (int64_t)m->profile.body_trace_mask);
  int32_t args[7] = {(int32_t)at, (int32_t)(at + 56), (int32_t)(at + 68), (int32_t)(at + 80),
                     (int32_t)(at + 56), guest_i32(call, (int64_t)f->player + 140), mask};
  (void)qa_qvm_module_invoke_source_callback(&m->game->module, call, callback, args, 7);
  qa_qvm_writable_view checked_view;
  qa_qvm_memory_data_view(call->guest, at, 56, &checked_view);
  const qa_bounds *accepted =
      qa_qvm_writable_view_get_int32(&checked_view, 0, true) == 0 ? requested : previous;
  apply_ctx apply = {call, m, f, body, accepted, requested, previous_duck, previous_height};
  call->effect(call->call_ctx, apply_perform, &apply);
  qa_cleanup_pop(&guard);
  call->effect(call->call_ctx, restore_perform, &restore);
  free(saved);
  return result;
}

static int32_t body_shape(qa_qvm_equipment_movement *m, qa_qvm_module_function_call *call, frame *f) {
  const qa_movement_body_shape *body = f->body;
  if (body == NULL || !m->profile.has_body_trace) {
    if (body != NULL && body->has_requested)
      qa_range_error("Original QVM body shape requires an admitted trace callback layout");
    return proceed(call);
  }
  int32_t previous_duck = guest_i32(call, (int64_t)f->player + 12) & 1;
  int32_t previous_height = guest_i32(call, (int64_t)f->player + 164);
  size_t index = (size_t)(f - m->frames);
  int32_t result = proceed(call);
  f = &m->frames[index]; /* a nested scope may have grown the stack */
  return body_finish(m, call, f, body, previous_duck, previous_height, result);
}

qa_qvm_equipment_movement *qa_qvm_equipment_movement_create(
    qa_qvm_game *game, const qa_resolved_qvm_artifact *artifact,
    qa_qvm_equipment_movement_profile profile, qa_qvm_equipment_movement_services services) {
  const qa_qvm_image *image = &artifact->image;
  int64_t total = image->data_length + image->literal_length + image->bss_length;
  int64_t body_scratch = (int64_t)ceil((double)total / 16.0) * 16;
  if (profile.has_body_trace && body_scratch + 92 > image->allocated_data_length - 65536)
    qa_range_error("Original body trace requires scratch outside source data and stack");
  double entries[3] = {profile.move, profile.slice, profile.duck};
  for (size_t i = 0; i < 3; i++) {
    double entry = entries[i];
    const qa_qvm_instruction *instruction =
        is_integer(entry) && entry >= 0 && entry < (double)image->instruction_count
            ? &image->instructions[(size_t)entry]
            : NULL;
    if (instruction == NULL || instruction->opcode != QA_QVM_OP_ENTER)
      qa_range_error("Selected equipment movement requires an original function boundary");
  }
  {
    qa_qvm_instruction *list =
        malloc((image->instruction_count == 0 ? 1 : image->instruction_count) * sizeof *list);
    if (list == NULL) return NULL;
    for (size_t i = 0; i < image->instruction_count; i++) {
      list[i].opcode = (int32_t)image->instructions[i].opcode;
      list[i].operand_width = image->instructions[i].operand_width;
      list[i].operand = image->instructions[i].operand;
      list[i].byte_offset = image->instructions[i].byte_offset;
    }
    (void)qa_qvm_qualify_region(list, image->instruction_count, (int64_t)profile.slice,
                                 (int64_t)profile.locomotion_entry,
                                 (int64_t)profile.locomotion_join);
    free(list);
  }
  qa_qvm_equipment_movement *m = calloc(1, sizeof *m);
  if (m == NULL) return NULL;
  m->game = game;
  m->profile = profile;
  m->services = services;
  m->body_scratch = body_scratch;
  qa_guest_callback_reference reference;
  memset(&reference, 0, sizeof reference);
  reference.kind = QA_GUEST_CALLBACK_QVM;
  reference.module = artifact->module;
  reference.instruction_index = profile.duck;
  m->removal = qa_qvm_module_bind_invocation(&game->module, &reference, duck_hook, m);
  m->has_removal = true;
  return m;
}

void qa_qvm_equipment_movement_close(qa_qvm_equipment_movement *m) {
  if (m == NULL) return;
  if (m->has_removal) {
    qa_qvm_module_unbind(&m->game->module, m->removal);
    m->has_removal = false;
  }
}

void qa_qvm_equipment_movement_destroy(qa_qvm_equipment_movement *m) {
  if (m == NULL) return;
  qa_qvm_equipment_movement_close(m);
  free(m->frames);
  free(m);
}
