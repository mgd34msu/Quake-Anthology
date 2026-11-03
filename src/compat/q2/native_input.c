/* Port of src/compat/q2/native-input.ts: public ClientThink/Pmove boundaries,
 * including calls made inside the original game DLL. GPL-2.0-or-later. */
#include "qa/compat/q2/native_input.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/compat/q2/classic/records.h"
#include "qa/compat/q2/native_mod_client_stages.h"
#include "qa/compat/q2/native_mod_entries.h"
#include "qa/compat/q2/native_throw.h"
#include "qa/compat/q2/rerelease/layouts.h"
#include "qa/compat/q2/rerelease/player_state.h"
#include "qa/core/numeric.h"
#include "qa/movement/client_outputs.h"
#include "qa/movement/q2/view.h"
#include "qa/foundation/js.h"

static int16_t get16(const uint8_t *v, size_t o) { return (int16_t)(uint16_t)(v[o] | (v[o + 1] << 8)); }
qa_q2_user_command qa_compat_q2_read_classic_user_command(const uint8_t *view) {
  qa_q2_user_command c;
  c.milliseconds = view[0]; c.buttons = view[1];
  c.angle_shorts.values[0] = get16(view, 2); c.angle_shorts.values[1] = get16(view, 4); c.angle_shorts.values[2] = get16(view, 6);
  c.forward_move = get16(view, 8); c.side_move = get16(view, 10); c.up_move = get16(view, 12);
  c.impulse = view[14]; c.light_level = view[15];
  return c;
}
typedef struct { qa_mod_client_identity identity; double slot; } command_scope;
struct qa_compat_q2_native_input_binding {
  qa_compat_q2_native_input_source source;
  qa_compat_q2_native_input_services services;
  qa_compat_q2_native_entry *command;
  uint64_t release, movement;
  command_scope **commands; size_t command_count, command_capacity;
  qa_mod_client_application *current;
  bool closed;
};
typedef qa_compat_q2_native_input_binding binding_t;
#define REMOVED_MESSAGE "Native input actor was removed"

static double layout_offset(const qa_guest_layout *layout, const char *name) { return qa_compat_q2_rerelease_field_offset(layout, name); }
static void assert_live(const binding_t *s, const command_scope *scope) {
  if (!s->services.live(s->services.ctx, scope->identity)) qa_compat_q2_native_throw(scope, REMOVED_MESSAGE);
}
static qa_user_command read_command(const binding_t *s, const uint8_t *view) {
  qa_user_command command;
  memset(&command, 0, sizeof command);
  if (s->source.classic) { command.kind = QA_USER_COMMAND_Q2; command.as.q2 = qa_compat_q2_read_classic_user_command(view); }
  else { command.kind = QA_USER_COMMAND_Q2_RERELEASE; command.as.q2_rerelease = qa_compat_q2_read_rerelease_user_command(view); }
  return command;
}
static qa_vec3 aim_of(const binding_t *s, const qa_user_command *command, const uint8_t *state) {
  qa_q2_vec3 result = {0, 0, 0};
  if (command->kind == QA_USER_COMMAND_Q2) {
    qa_q2_vec3 angles = {command->as.q2.angle_shorts.values[0], command->as.q2.angle_shorts.values[1], command->as.q2.angle_shorts.values[2]};
    qa_q2_vec3 delta = {qa_compat_q2_view_i16(state, 20), qa_compat_q2_view_i16(state, 22), qa_compat_q2_view_i16(state, 24)};
    qa_q2_classic_view_angles(result, angles, delta, state[16], s->services.numeric);
  } else {
    qa_q2_vec3 angles = {command->as.q2_rerelease.angles.x, command->as.q2_rerelease.angles.y, command->as.q2_rerelease.angles.z};
    qa_q2_vec3 delta = {qa_compat_q2_view_f32(state, 36), qa_compat_q2_view_f32(state, 40), qa_compat_q2_view_f32(state, 44)};
    qa_q2_rerelease_view_angles(result, angles, delta, qa_compat_q2_view_u16(state, 28), s->services.numeric);
  }
  qa_vec3 out = {result[0], result[1], result[2]};
  return out;
}
static qa_user_command encode_aim(void *v, qa_vec3 aim, qa_user_command value) {
  const uint8_t *state = v;
  if (value.kind == QA_USER_COMMAND_Q2) {
    value.as.q2.angle_shorts.values[0] = trunc(aim.x * 65536 / 360) - qa_compat_q2_view_i16(state, 20);
    value.as.q2.angle_shorts.values[1] = trunc(aim.y * 65536 / 360) - qa_compat_q2_view_i16(state, 22);
    value.as.q2.angle_shorts.values[2] = trunc(aim.z * 65536 / 360) - qa_compat_q2_view_i16(state, 24);
    return value;
  }
  if (value.kind == QA_USER_COMMAND_Q2_RERELEASE) {
    value.as.q2_rerelease.angles.x = aim.x - qa_compat_q2_view_f32(state, 36);
    value.as.q2_rerelease.angles.y = aim.y - qa_compat_q2_view_f32(state, 40);
    value.as.q2_rerelease.angles.z = aim.z - qa_compat_q2_view_f32(state, 44);
    return value;
  }
  qa_range_error("Native input output changed command dialect");
}
static void write_rerelease_command(void *ctx, uint8_t *view, size_t length, qa_q2_rerelease_user_command command) { (void)ctx; (void)length; qa_compat_q2_write_rerelease_user_command(view, command); }
static size_t command_bytes(const binding_t *s) { return s->source.classic ? 16 : (size_t)qa_compat_q2_rerelease_usercmd_layout.byte_length; }
typedef qa_guest_call_result (*apply_run_fn)(void *ctx);
typedef struct {
  binding_t *s; const command_scope *scope; bool movement_slice; const qa_user_command *command; uint8_t *state; uint8_t *command_view;
  void *run_ctx; apply_run_fn run;
  qa_mod_client_application *application; bool failed;
  bool has_projected; int32_t projected_before, projected_value;
  qa_guest_call_result result;
} apply_call;
static void apply_body(void *v) {
  apply_call *a = v; binding_t *s = a->s; const command_scope *scope = a->scope;
  const qa_compat_q2_native_input_services *services = &s->services;
  qa_mod_client_application *previous = s->current;
  const qa_mod_client_application *parent = previous != NULL && qa_same_actor(previous->identity.actor, scope->identity.actor) ? previous : NULL;
  qa_frame_context frame = services->frame(services->ctx);
  const qa_user_command *command = a->command;
  double milliseconds = command->kind == QA_USER_COMMAND_Q2 ? command->as.q2.milliseconds : command->as.q2_rerelease.milliseconds;
  qa_mod_client_application_input input;
  memset(&input, 0, sizeof input);
  input.identity = scope->identity; input.scope = a->movement_slice ? "movement-slice" : "client-command"; input.command = *command;
  input.angle_space = "source-relative"; input.absolute_aim = aim_of(s, command, a->state);
  input.accepted = services->accepted(services->ctx, scope->identity.actor);
  input.has_arsenal = true;
  input.arsenal = a->movement_slice && parent != NULL && parent->has_arsenal ? &parent->arsenal : NULL;
  input.has_controls = true;
  input.controls_impulse = command->kind == QA_USER_COMMAND_Q2 ? command->as.q2.impulse : a->movement_slice && parent != NULL ? parent->controls_impulse : 0;
  input.has_parent_invocation = previous != NULL;
  input.parent_invocation = previous != NULL ? previous->invocation : 0;
  input.frame = frame; input.frame.phase = QA_FRAME_PHASE_CLIENT_COMMAND;
  input.frame.elapsed.kind = QA_SOURCE_TIME_MILLISECONDS; input.frame.elapsed.value = milliseconds;
  a->application = qa_mod_client_applications_begin(services->applications, input, encode_aim, a->state);
  s->current = a->application;
  assert_live(s, scope);
  const qa_mod_client_movement_outputs *slice_outputs = a->movement_slice && services->client_outputs != NULL ? services->client_outputs(services->ctx, scope->identity.actor) : NULL;
  if (a->application != NULL) {
    qa_user_command effective = qa_client_stance_command(a->application->command, slice_outputs != NULL && slice_outputs->has_stance, slice_outputs != NULL && slice_outputs->stance);
    if (effective.kind != QA_USER_COMMAND_Q2 && effective.kind != QA_USER_COMMAND_Q2_RERELEASE) qa_range_error("Native input output changed command dialect");
    if (!a->movement_slice && services->original_command != NULL) effective = services->original_command(services->ctx, scope->identity, effective);
    qa_compat_q2_write_native_user_command(a->command_view, command_bytes(s), effective, NULL, write_rerelease_command);
  }
  const qa_mod_client_movement_outputs *output = a->movement_slice && services->client_outputs != NULL ? services->client_outputs(services->ctx, scope->identity.actor) : NULL;
  if (output != NULL && output->has_mode) {
    int32_t before = qa_compat_q2_view_i32(a->state, 0);
    int32_t value = (int32_t)qa_client_movement_type(command->kind == QA_USER_COMMAND_Q2 ? QA_MOVEMENT_STATE_Q2 : QA_MOVEMENT_STATE_Q2_RERELEASE, output->mode);
    a->has_projected = true; a->projected_before = before; a->projected_value = value;
    qa_compat_q2_view_set_i32(a->state, 0, value);
  }
  a->result = a->run(a->run_ctx);
  a->failed = false;
  qa_mod_client_applications_finish(services->applications, a->application, false);
  qa_mod_client_application *finished = a->application;
  a->application = NULL;
  qa_mod_client_application_dispose(finished);
  assert_live(s, scope);
}
static qa_guest_call_result apply(binding_t *s, const command_scope *scope, bool movement_slice, const qa_user_command *command, uint8_t *state, uint8_t *command_view, void *run_ctx, apply_run_fn run) {
  qa_mod_client_application *previous = s->current;
  apply_call call;
  memset(&call, 0, sizeof call);
  call.s = s; call.scope = scope; call.movement_slice = movement_slice; call.command = command; call.state = state; call.command_view = command_view;
  call.run_ctx = run_ctx; call.run = run; call.failed = true;
  qa_compat_q2_native_thrown thrown;
  bool completed = qa_compat_q2_native_try(apply_body, &call, &thrown);
  qa_mod_client_applications_finish(s->services.applications, call.application, call.failed);
  if (call.application != NULL) qa_mod_client_application_dispose(call.application);
  if (call.has_projected && s->services.live(s->services.ctx, scope->identity) && qa_compat_q2_view_i32(state, 0) == call.projected_value)
    qa_compat_q2_view_set_i32(state, 0, call.projected_before);
  s->current = previous;
  if (!completed) qa_compat_q2_native_rethrow(&thrown);
  return call.result;
}
/* --- ClientThink --- */
typedef struct { binding_t *s; const command_scope *scope; const qa_guest_call_value *values; size_t count; qa_compat_q2_native_entry *binding; qa_guest_call_result result; const qa_raw_entity_view *record; } command_call;
static qa_guest_call_result command_original(void *v) { command_call *c = v; return qa_compat_q2_native_entry_call_original(c->binding, c->values, c->count); }
static void command_body(void *v) {
  command_call *c = v; binding_t *s = c->s; const command_scope *scope = c->scope;
  if (!qa_mod_client_applications_active(s->services.applications)) {
    assert_live(s, scope);
    c->result = command_original(c);
    assert_live(s, scope);
    return;
  }
  qa_compat_q2_memory *memory = s->source.memory;
  qa_guest_address address = qa_compat_q2_guest_required_pointer(c->values, c->count, 1);
  uint8_t *command_view = memory->borrow(memory->ctx, address, (double)command_bytes(s));
  qa_user_command command = read_command(s, command_view);
  uint8_t *state = NULL;
  if (s->source.classic) state = s->source.client_prefix(s->source.host, scope->slot);
  else {
    qa_guest_address client;
    if (qa_compat_q2_mem_read_pointer(memory, qa_compat_q2_mem_offset(memory, c->record->address, (int64_t)layout_offset(&qa_compat_q2_rerelease_edict_layout, "client")), &client))
      state = memory->borrow(memory->ctx, client, qa_compat_q2_rerelease_pmove_state_layout.byte_length);
  }
  if (state == NULL) qa_range_error("Native input has no public client state");
  c->result = apply(s, scope, false, &command, state, command_view, c, command_original);
}
static qa_guest_call_result command_execute(void *v, const qa_guest_call_value *values, size_t count, qa_compat_q2_native_entry *binding) {
  binding_t *s = v; qa_compat_q2_runner *runner = s->source.runner;
  qa_compat_q2_rerelease_layouts_ensure();
  qa_guest_address pointer = qa_compat_q2_guest_required_pointer(values, count, 0);
  qa_raw_entity_view record = s->source.from_pointer(s->source.host, pointer);
  command_scope scope;
  if (!s->services.identity(s->services.ctx, record.slot, &scope.identity)) return qa_compat_q2_native_entry_call_original(binding, values, count);
  scope.slot = record.slot;
  void *saved = runner->capture_processor(runner->ctx);
  double depth = qa_compat_q2_native_depth(runner);
  if (s->command_count == s->command_capacity) {
    size_t capacity = s->command_capacity ? s->command_capacity * 2 : 4;
    command_scope **grown = realloc(s->commands, capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("Native input out of memory");
    s->commands = grown; s->command_capacity = capacity;
  }
  size_t stack_depth = s->command_count;
  s->commands[s->command_count++] = &scope;
  command_call call = {s, &scope, values, count, binding, {0}, &record};
  qa_compat_q2_native_thrown thrown;
  bool completed = qa_compat_q2_native_try(command_body, &call, &thrown), removed = !completed && thrown.token == &scope;
  if (removed) {
    /* Unlike guest faults, this controlled exit resumes this exact original caller. */
    qa_compat_q2_native_unwind(runner, depth);
    runner->restore_processor(runner->ctx, saved);
    call.result = qa_compat_q2_call_void();
  }
  s->command_count = stack_depth;
  if (runner->free_processor != NULL) runner->free_processor(runner->ctx, saved);
  if (!completed && !removed) qa_compat_q2_native_rethrow(&thrown);
  return call.result;
}
static bool command_accepts(void *v) {
  binding_t *s = v;
  return qa_mod_client_applications_active(s->services.applications) || s->services.body_bounds != NULL;
}
static void on_release(void *v, qa_actor_id actor) {
  binding_t *s = v;
  for (size_t i = 0; i < s->command_count; i++) if (qa_same_actor(s->commands[i]->identity.actor, actor)) s->source.retire(s->source.retire_ctx, s->commands[i]->slot, s->commands[i]->identity);
}
/* --- Pmove --- */
typedef struct {
  binding_t *s; const command_scope *scope; qa_guest_address address;
  uint8_t *view; uint8_t *cmd; qa_user_command command;
  void *run_ctx; void (*run)(void *, const qa_movement_body_shape *);
  bool classic, moved;
} movement_call;
static qa_vec3 motion_vector(const movement_call *m, size_t offset) {
  qa_vec3 out;
  if (m->classic) { out.x = qa_compat_q2_view_i16(m->view, offset) * 0.125; out.y = qa_compat_q2_view_i16(m->view, offset + 2) * 0.125; out.z = qa_compat_q2_view_i16(m->view, offset + 4) * 0.125; }
  else { out.x = qa_compat_q2_view_f32(m->view, offset); out.y = qa_compat_q2_view_f32(m->view, offset + 4); out.z = qa_compat_q2_view_f32(m->view, offset + 8); }
  return out;
}
static void motion_store(const movement_call *m, size_t offset, qa_vec3 value) {
  const double values[3] = {value.x, value.y, value.z};
  const qa_numeric_operations *numeric = m->s->services.numeric;
  for (size_t axis = 0; axis < 3; axis++) {
    if (m->classic) qa_compat_q2_view_set_i16(m->view, offset + axis * 2, qa_js_to_int32(numeric->to_int32(numeric->ctx, numeric->multiply(numeric->ctx, values[axis], 8))));
    else qa_compat_q2_view_set_f32(m->view, offset + axis * 4, values[axis]);
  }
}
static qa_compat_q2_native_input_motion_state motion_read(void *v) {
  const movement_call *m = v; qa_compat_q2_native_input_motion_state out;
  out.origin = motion_vector(m, 4); out.velocity = motion_vector(m, m->classic ? 10 : 16);
  return out;
}
static size_t pm_flags_offset(void) { return (size_t)layout_offset(&qa_compat_q2_rerelease_pmove_state_layout, "pm_flags"); }
static bool motion_grounded(void *v) {
  const movement_call *m = v;
  return ((m->classic ? m->view[16] : qa_compat_q2_view_u16(m->view, pm_flags_offset())) & 4) != 0;
}
static qa_compat_q2_native_input_motion_view motion_view(void *v) {
  const movement_call *m = v; const binding_t *s = m->s; qa_compat_q2_native_input_motion_view out;
  if (s->source.classic) {
    const uint8_t *client = s->source.client_prefix(s->source.host, m->scope->slot);
    if (client == NULL) qa_range_error("Native input has no public client view");
    out.view_offset.x = qa_compat_q2_view_f32(client, 40); out.view_offset.y = qa_compat_q2_view_f32(client, 44);
    out.view_offset.z = m->moved ? qa_compat_q2_view_f32(m->view, 192) : qa_compat_q2_view_f32(client, 48);
    out.crouched = ((m->moved ? m->view[16] : client[16]) & 1) != 0;
    return out;
  }
  qa_vec3 offset = motion_vector(m, (size_t)layout_offset(&qa_compat_q2_rerelease_pmove_layout, "viewoffset"));
  out.view_offset = offset;
  out.view_offset.z = offset.z + (int8_t)m->view[(size_t)layout_offset(&qa_compat_q2_rerelease_pmove_state_layout, "viewheight")];
  out.crouched = (qa_compat_q2_view_u16(m->view, pm_flags_offset()) & 1) != 0;
  return out;
}
static void motion_write(void *v, qa_compat_q2_native_input_motion_state value) {
  const movement_call *m = v;
  motion_store(m, 4, value.origin);
  motion_store(m, m->classic ? 10 : 16, value.velocity);
}
static void set_vec3(uint8_t *view, size_t offset, qa_vec3 value) {
  qa_compat_q2_view_set_f32(view, offset, value.x); qa_compat_q2_view_set_f32(view, offset + 4, value.y); qa_compat_q2_view_set_f32(view, offset + 8, value.z);
}
/* TS planeType(normal). */
static uint8_t plane_type(qa_vec3 normal) {
  if (normal.x == 1 || normal.x == -1) return 0;
  if (normal.y == 1 || normal.y == -1) return 1;
  if (normal.z == 1 || normal.z == -1) return 2;
  return 3;
}
/* TS writePlane(base, plane): plane NULL is TS null. */
static void write_plane(uint8_t *view, const char *base, const qa_plane *plane) {
  const qa_guest_layout *pmove = &qa_compat_q2_rerelease_pmove_layout;
  qa_vec3 normal = {0, 0, 0};
  if (plane != NULL) normal = plane->normal;
  char name[80];
  snprintf(name, sizeof name, "%s.normal", base);
  set_vec3(view, (size_t)layout_offset(pmove, name), normal);
  snprintf(name, sizeof name, "%s.dist", base);
  qa_compat_q2_view_set_f32(view, (size_t)layout_offset(pmove, name), plane != NULL ? plane->distance : 0);
  snprintf(name, sizeof name, "%s.type", base);
  view[(size_t)layout_offset(pmove, name)] = plane == NULL ? 0 : plane_type(normal);
  snprintf(name, sizeof name, "%s.signbits", base);
  view[(size_t)layout_offset(pmove, name)] = plane == NULL ? 0 : (uint8_t)((normal.x < 0 ? 1 : 0) | (normal.y < 0 ? 2 : 0) | (normal.z < 0 ? 4 : 0));
}
static void motion_write_outputs(void *v, const qa_compat_q2_native_input_motion_outputs *outputs) {
  const movement_call *m = v; const binding_t *s = m->s; qa_compat_q2_memory *memory = s->source.memory;
  /* Preserve guest-owned flag bits (jump-held, timed states); the selected
   * movement owns grounded/ducked, water and eye height. */
  uint32_t flags = (outputs->ducked ? 1u : 0u) | (outputs->grounded ? 4u : 0u);
  size_t count = outputs->touch_count < 32 ? outputs->touch_count : 32;
  qa_guest_address ground = {0};
  bool has_ground = outputs->has_ground_slot;
  if (has_ground) ground = s->source.at_slot(s->source.host, outputs->ground_slot).address;
  if (m->classic) {
    qa_guest_address a2;
    qa_guest_address v3;
    m->view[16] = (uint8_t)((m->view[16] & ~5u) | flags);
    qa_compat_q2_view_set_i32(m->view, 224, qa_int32(outputs->water_type));
    qa_compat_q2_view_set_i32(m->view, 228, qa_int32(outputs->water_level));
    qa_compat_q2_view_set_f32(m->view, 192, outputs->view_height);
    set_vec3(m->view, 180, outputs->view_angles);
    set_vec3(m->view, 196, outputs->bounds.min);
    set_vec3(m->view, 208, outputs->bounds.max);
    qa_compat_q2_mem_write_pointer(memory, qa_compat_q2_mem_offset(memory, m->address, 220), has_ground, ground);
    qa_compat_q2_view_set_i32(m->view, 48, qa_js_to_int32(count));
    for (size_t index = 0; index < count; index++)
      (a2 = qa_compat_q2_mem_offset(memory, m->address, (int64_t)(52 + index * 4)), v3 = s->source.at_slot(s->source.host, outputs->touches[index].slot).address, qa_compat_q2_mem_write_pointer(memory, a2, true, v3));
    return;
  }
  const qa_guest_layout *pmove = &qa_compat_q2_rerelease_pmove_layout;
  size_t flags_offset = pm_flags_offset();
  qa_compat_q2_view_set_u16(m->view, flags_offset, (qa_compat_q2_view_u16(m->view, flags_offset) & ~5u) | flags);
  qa_compat_q2_view_set_i32(m->view, (size_t)layout_offset(pmove, "watertype"), qa_int32(outputs->water_type));
  m->view[(size_t)layout_offset(pmove, "waterlevel")] = (uint8_t)(qa_int32(outputs->water_level) & 0xff);
  m->view[(size_t)layout_offset(&qa_compat_q2_rerelease_pmove_state_layout, "viewheight")] = (uint8_t)(qa_int32(outputs->view_height) & 0xff);
  set_vec3(m->view, (size_t)layout_offset(pmove, "viewangles"), outputs->view_angles);
  set_vec3(m->view, (size_t)layout_offset(pmove, "mins"), outputs->bounds.min);
  set_vec3(m->view, (size_t)layout_offset(pmove, "maxs"), outputs->bounds.max);
  write_plane(m->view, "groundplane", outputs->has_ground_plane ? &outputs->ground_plane : NULL);
  qa_vec3 zero = {0, 0, 0};
  set_vec3(m->view, (size_t)layout_offset(pmove, "viewoffset"), zero);
  qa_compat_q2_mem_write_pointer(memory, qa_compat_q2_mem_offset(memory, m->address, (int64_t)layout_offset(pmove, "groundentity")), has_ground, ground);
  qa_compat_q2_view_set_i32(m->view, (size_t)layout_offset(pmove, "touch.num"), qa_js_to_int32(count));
  for (size_t index = 0; index < count; index++) {
    char name[64];
    snprintf(name, sizeof name, "touch.touches[%zu].plane", index);
    write_plane(m->view, name, &outputs->touches[index].plane);
    snprintf(name, sizeof name, "touch.touches[%zu].ent", index);
    qa_guest_address a = qa_compat_q2_mem_offset(memory, m->address, (int64_t)layout_offset(pmove, name));
    qa_guest_address v2 = s->source.at_slot(s->source.host, outputs->touches[index].slot).address;
    qa_compat_q2_mem_write_pointer(memory, a, true, v2);
  }
}
static void body_current_actor(void *v) { movement_call *m = v; assert_live(m->s, m->scope); }
static qa_guest_call_result run_body(void *v) {
  movement_call *m = v; binding_t *s = m->s; const qa_compat_q2_native_input_services *services = &s->services;
  assert_live(s, m->scope);
  const qa_mod_client_movement_outputs *outputs = services->client_outputs != NULL ? services->client_outputs(services->ctx, m->scope->identity.actor) : NULL;
  const qa_bounds *requested = outputs != NULL ? outputs->body_bounds : NULL;
  qa_bounds current;
  bool has_current = services->body_bounds != NULL && services->body_bounds(services->ctx, m->scope->identity.actor, &current);
  if (requested != NULL && !has_current) qa_range_error("Native body shape lost its current actor bounds");
  qa_movement_body_shape body;
  memset(&body, 0, sizeof body);
  if (has_current) {
    body.ctx = m; body.current = current; body.current_actor = body_current_actor;
    if (requested != NULL) { body.has_requested = true; body.requested = *requested; }
  }
  m->run(m->run_ctx, has_current ? &body : NULL);
  assert_live(s, m->scope);
  return qa_compat_q2_call_void();
}
static qa_guest_call_result run_slice(void *v) {
  movement_call *m = v;
  qa_guest_call_result result = run_body(m);
  m->moved = true;
  return result;
}
static qa_guest_call_result movement_run(void *v) {
  movement_call *m = v; binding_t *s = m->s;
  if (qa_mod_client_applications_active(s->services.applications))
    return apply(s, m->scope, true, &m->command, m->view, m->cmd, m, run_slice);
  return run_body(m);
}
static void movement_boundary(void *v, qa_guest_address address, void *run_ctx, void (*run)(void *, const qa_movement_body_shape *)) {
  binding_t *s = v;
  const command_scope *scope = s->command_count == 0 ? NULL : s->commands[s->command_count - 1];
  if (scope == NULL) { run(run_ctx, NULL); return; }
  assert_live(s, scope);
  qa_compat_q2_rerelease_layouts_ensure();
  qa_compat_q2_memory *memory = s->source.memory; bool classic = s->source.classic;
  if (!classic) {
    qa_guest_address player;
    bool has = qa_compat_q2_mem_read_pointer(memory, qa_compat_q2_mem_offset(memory, address, (int64_t)layout_offset(&qa_compat_q2_rerelease_pmove_layout, "player")), &player);
    if (!has || player.byte_offset != s->source.at_slot(s->source.host, scope->slot).address.byte_offset) { run(run_ctx, NULL); return; }
  }
  uint8_t *view = memory->borrow(memory->ctx, address, classic ? QA_COMPAT_Q2_CLASSIC_PMOVE_BYTES : qa_compat_q2_rerelease_pmove_layout.byte_length);
  size_t command_offset = classic ? 28 : (size_t)layout_offset(&qa_compat_q2_rerelease_pmove_layout, "cmd.msec");
  movement_call call;
  memset(&call, 0, sizeof call);
  call.s = s; call.scope = scope; call.address = address; call.view = view; call.cmd = view + command_offset;
  call.command = read_command(s, call.cmd); call.run_ctx = run_ctx; call.run = run; call.classic = classic;
  qa_compat_q2_native_input_motion projection = {&call, motion_read, motion_grounded, motion_view, motion_write, motion_write_outputs};
  s->services.movement(s->services.ctx, scope->identity, &projection, call.command, aim_of(s, &call.command, view), &call, movement_run);
}
static bool movement_active(void *v) {
  binding_t *s = v;
  return (qa_mod_client_applications_active(s->services.applications) || s->services.body_bounds != NULL) && s->command_count != 0;
}
qa_compat_q2_native_input_binding *qa_compat_q2_native_input_binding_create(qa_compat_q2_native_input_source source, qa_compat_q2_native_input_services services) {
  binding_t *s = calloc(1, sizeof *s);
  if (s == NULL) qa_range_error("Native input out of memory");
  s->source = source; s->services = services;
  qa_guest_address address; qa_compat_q2_guest_call_signature signature;
  source.entry(source.host, "ClientThink", &address, &signature);
  char id[320];
  snprintf(id, sizeof id, "%s:input.ClientThink", source.memory->module.id);
  s->command = qa_compat_q2_native_entry_bind(source.memory, source.runner, source.host, source.invoke, address, id, signature, s, command_execute, s, command_accepts);
  s->release = services.on_release(services.ctx, on_release, s);
  s->movement = source.bind_input_movement(source.host, s, movement_boundary, s, movement_active);
  return s;
}
void qa_compat_q2_native_input_binding_close(binding_t *s) {
  if (s->closed) return;
  s->closed = true;
  s->source.unbind_input_movement(s->source.host, s->movement);
  s->services.remove_release(s->services.ctx, s->release);
  qa_compat_q2_native_entry_close(s->command);
}
void qa_compat_q2_native_input_binding_destroy(binding_t *s) {
  if (s == NULL) return;
  qa_compat_q2_native_input_binding_close(s);
  free(s->commands); free(s);
}
/* --- Sources over the real guest hosts. --- */
static void classic_entry(void *host, const char *name, qa_guest_address *out_address, qa_compat_q2_guest_call_signature *out_signature) {
  qa_compat_q2_classic_guest_host *h = host;
  const qa_compat_q2_classic_export *definition = qa_compat_q2_classic_find_export(name);
  if (definition == NULL) qa_range_error("API3 lacks %s", name);
  qa_compat_q2_memory *memory = qa_compat_q2_classic_guest_host_get_memory(h);
  if (!qa_compat_q2_mem_read_pointer(memory, qa_compat_q2_mem_offset(memory, qa_compat_q2_classic_guest_host_get_exports(h), (int64_t)definition->offset), out_address)) qa_range_error("Null API3 %s", name);
  *out_signature = definition->signature;
}
static qa_guest_call_result classic_invoke(void *host, qa_guest_address address, qa_compat_q2_guest_call_signature signature, const qa_guest_call_value *values, size_t count) {
  qa_compat_q2_classic_guest_host *h = host;
  return qa_compat_q2_classic_guest_host_invoke(h, address, signature, values, count, NULL, qa_compat_q2_classic_guest_host_get_options(h)->instruction_budget);
}
static qa_raw_entity_view classic_from_pointer(void *host, qa_guest_address address) { return qa_compat_q2_classic_edicts_from_pointer(qa_compat_q2_classic_guest_host_get_edicts(host), address); }
static qa_raw_entity_view classic_at_slot(void *host, double slot) { return qa_compat_q2_classic_edicts_at(qa_compat_q2_classic_guest_host_get_edicts(host), slot); }
static uint8_t *classic_client_prefix(void *host, double slot) { return qa_compat_q2_classic_edicts_client_prefix(qa_compat_q2_classic_guest_host_get_edicts(host), slot); }
static uint64_t classic_bind_movement(void *host, void *ctx, void (*boundary)(void *, qa_guest_address, void *, void (*)(void *, const qa_movement_body_shape *)), void *active_ctx, bool (*active)(void *)) {
  (void)active_ctx; (void)active; /* TS classic bindInputMovement takes no activity predicate. */
  return qa_compat_q2_classic_guest_host_bind_input_movement(host, ctx, boundary);
}
static void classic_unbind_movement(void *host, uint64_t handle) { qa_compat_q2_classic_guest_host_unbind_input_movement(host, handle); }
qa_compat_q2_native_input_source qa_compat_q2_native_input_source_classic(qa_compat_q2_classic_guest_host *host, void *retire_ctx, void (*retire)(void *, double, qa_mod_client_identity)) {
  qa_compat_q2_native_input_source source;
  memset(&source, 0, sizeof source);
  source.classic = true; source.memory = qa_compat_q2_classic_guest_host_get_memory(host); source.runner = qa_compat_q2_classic_guest_host_get_options(host)->runner;
  source.host = host; source.retire_ctx = retire_ctx; source.retire = retire;
  source.entry = classic_entry; source.invoke = classic_invoke; source.from_pointer = classic_from_pointer; source.at_slot = classic_at_slot;
  source.client_prefix = classic_client_prefix; source.bind_input_movement = classic_bind_movement; source.unbind_input_movement = classic_unbind_movement;
  return source;
}
static void rerelease_entry(void *host, const char *name, qa_guest_address *out_address, qa_compat_q2_guest_call_signature *out_signature) {
  qa_compat_q2_rerelease_guest_module *m = host;
  size_t count = 0; const qa_compat_q2_rerelease_api_entry *entries = qa_compat_q2_rerelease_get_game_exports(&count), *definition = NULL;
  for (size_t i = 0; i < count && definition == NULL; i++) if (strcmp(entries[i].name, name) == 0) definition = &entries[i];
  if (definition == NULL) qa_range_error("API2023 lacks %s", name);
  qa_compat_q2_memory *memory = qa_compat_q2_rerelease_guest_module_get_memory(m);
  qa_guest_address game = qa_compat_q2_rerelease_guest_module_bind_game(m);
  if (!qa_compat_q2_mem_read_pointer(memory, qa_compat_q2_mem_offset(memory, game, (int64_t)qa_compat_q2_rerelease_field_offset(qa_compat_q2_rerelease_get_game_export_layout(), name)), out_address)) qa_range_error("Null API2023 %s", name);
  *out_signature = definition->signature;
}
static qa_guest_call_result rerelease_invoke(void *host, qa_guest_address address, qa_compat_q2_guest_call_signature signature, const qa_guest_call_value *values, size_t count) { return qa_compat_q2_rerelease_guest_module_invoke(host, address, signature, values, count, NULL, NULL); }
static qa_raw_entity_view rerelease_from_pointer(void *host, qa_guest_address address) { qa_raw_entity_table table = qa_compat_q2_rerelease_guest_module_entities(host); return table.from_pointer(table.ctx, address); }
static qa_raw_entity_view rerelease_at_slot(void *host, double slot) { qa_raw_entity_table table = qa_compat_q2_rerelease_guest_module_entities(host); return table.at_slot(table.ctx, slot); }
static uint64_t rerelease_bind_movement(void *host, void *ctx, void (*boundary)(void *, qa_guest_address, void *, void (*)(void *, const qa_movement_body_shape *)), void *active_ctx, bool (*active)(void *)) {
  return qa_compat_q2_rerelease_guest_module_bind_input_movement(host, ctx, boundary, active_ctx, active);
}
static void rerelease_unbind_movement(void *host, uint64_t handle) { qa_compat_q2_rerelease_guest_module_unbind_input_movement(host, handle); }
qa_compat_q2_native_input_source qa_compat_q2_native_input_source_rerelease(qa_compat_q2_rerelease_guest_module *host, void *retire_ctx, void (*retire)(void *, double, qa_mod_client_identity)) {
  qa_compat_q2_native_input_source source;
  memset(&source, 0, sizeof source);
  source.classic = false; source.memory = qa_compat_q2_rerelease_guest_module_get_memory(host); source.runner = qa_compat_q2_rerelease_guest_module_get_options(host)->runner;
  source.host = host; source.retire_ctx = retire_ctx; source.retire = retire;
  source.entry = rerelease_entry; source.invoke = rerelease_invoke; source.from_pointer = rerelease_from_pointer; source.at_slot = rerelease_at_slot;
  source.bind_input_movement = rerelease_bind_movement; source.unbind_input_movement = rerelease_unbind_movement;
  return source;
}
