/* Port of src/app/bootstrap/simulation/native-mod-host.ts.
 * createNativeModHost reuses the selected DLL's real imports and original
 * save callbacks in an isolated host. TS async members run synchronously
 * with the next-frame callback. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/native_mod_host.h"
#include <math.h>
#include <stdio.h>
#include <stdnoreturn.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/network/q2_layout.h"
#include "qa/app/bootstrap/simulation/classic_guest_services.h"
#include "qa/app/bootstrap/simulation/random.h"
#include "qa/app/bootstrap/simulation/rerelease_guest_services.h"
#include "qa/compat/q2/classic/records.h"
#include "qa/compat/q2/rerelease/api.h"
#include "qa/compat/q2/rerelease/imports.h"
#include "qa/compat/q2/rerelease/layouts.h"
#include "qa/compat/q2/rerelease/public_state.h"
#include "qa/content/catalog/timing.h"
#include "qa/content/mounts/paths.h"
#include "qa/content/q2/rerelease/types.h"
#include "qa/content/q2/rerelease/world_text.h"
#include "qa/core/commands/text.h"
#include "qa/core/numeric.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/guest/core/memory.h"
#include "qa/network/common/js_number.h"
#include "qa/persistence/q2_classic_guest.h"
#include "qa/persistence/value.h"
#include "qa/world/session/mod_engine.h"

/* An import name the guest host routes through the boundary. The cell is the
 * persistent invoke_ctx handed to the projection: inside the boundary call it
 * runs the guest's own closure; afterwards (a provider restore link) it runs
 * the same import again with the saved values, as the retained TS closure. */
typedef struct import_cell { struct host *host; char *name; struct import_cell *next; } import_cell;
typedef struct import_frame {
  import_cell *cell; qa_guest_call_result (*invoke)(void *); void *invoke_ctx; struct import_frame *previous;
} import_frame;
typedef struct write_observer {
  struct host *host; qa_guest_write_observer_handle handle; void *ctx; qa_compat_q2_written_fn cb; struct write_observer *next;
} write_observer;

typedef struct host {
  qa_compat_q2_native_mod_host table; /* first: the table pointer is the host */
  qa_app_sim_native_mod_host_options options;
  const qa_native_mod_declaration *declaration;
  const qa_app_sim_native_mod_host_context *context;
  const qa_mod_host_services *services;
  bool rerelease, closed;
  qa_provider_timing timing;
  qa_cvar_registry *cvars;
  double max_clients;
  qa_mod_command_port *commands;
  qa_command_invocation *current_command;
  bool has_source_time; double source_time, source_frame;
  double frame_seconds;
  qa_app_sim_source_random *random;
  qa_app_sim_actor_host_runtime runtime;
  char *map;
  qa_app_sim_classic_guest_services_options shared;
  qa_compat_q2_classic_actor_projection classic_projection;
  /* classic */
  qa_classic_original_save_files *files;
  qa_app_sim_classic_guest_services *classic;
  qa_app_sim_classic_guest_source *classic_source;
  /* rerelease */
  qa_app_sim_rerelease_guest_services *rr;
  qa_app_sim_rerelease_guest_source *rr_source;
  qa_compat_q2_rerelease_semantic_bindings semantics;
  qa_app_sim_native_mod_presentation *presentation;
  qa_app_sim_native_guest *guest;
  write_observer *observers;
  import_cell *cells; import_frame *frame;
  /* results borrowed until the next call of the same member */
  char *signature, *weapon_path; qa_source_weapon_presentation_model weapon_model;
  /* checkpoint storage borrowed by the provider until the next checkpoint */
  qa_compat_q2_native_mod_source_save save;
  uint8_t *saved_bytes[3];
  qa_compat_q2_native_mod_configstring *saved_configstrings; char **saved_values; size_t saved_value_count;
} host_t;

static noreturn void fail(const char *message) { qa_raise("Error", 0, 0, "%s", message); }
static noreturn void unavailable(const char *operation) { qa_raise("Error", 0, 0, "Native gameplay mod requires a declared %s binding", operation); }
static void *xcalloc(size_t count, size_t size) { void *p = calloc(count == 0 ? 1 : count, size); if (p == NULL) qa_range_error("Out of memory"); return p; }
static char *xstrdup(const char *text) { size_t n = strlen(text) + 1; char *p = malloc(n); if (p == NULL) qa_range_error("Out of memory"); memcpy(p, text, n); return p; }
static host_t *of(void *ctx) { return ctx; }

/* --- clock and runtime --- */
static double time_seconds(qa_source_time time) { return time.kind == QA_SOURCE_TIME_SECONDS ? time.value : time.value / 1000; }
/* TS runtime.now(): services.time() is read before sourceTime decides. */
static double runtime_now(void *ctx) {
  host_t *h = ctx; qa_source_time time = h->services->time(h->services->ctx);
  return h->has_source_time ? h->source_time : time_seconds(time);
}
static double runtime_frame_seconds(void *ctx) { return ((host_t *)ctx)->frame_seconds; }
static void runtime_schedule(void *ctx, qa_owned_actor actor, const double *due) {
  (void)ctx; (void)actor; (void)due; fail("Native mod scheduling requires a declared source lifecycle callback");
}
static qa_app_sim_native_mod_clock presentation_clock(void *ctx) {
  host_t *h = ctx; qa_source_time now = h->services->time(h->services->ctx);
  qa_app_sim_native_mod_clock clock; memset(&clock, 0, sizeof clock);
  clock.server_frame = h->source_frame; clock.has_frame_time_milliseconds = true; clock.frame_time_milliseconds = h->frame_seconds * 1000;
  clock.time_milliseconds = (h->has_source_time ? h->source_time : time_seconds(now)) * 1000;
  return clock;
}

/* --- commands --- */
typedef struct command_scope { host_t *h; qa_command_invocation *parent; void (*run)(void *); void *run_ctx; } command_scope;
static void command_body(void *ctx) { command_scope *c = ctx; c->run(c->run_ctx); }
static void command_restore(void *ctx) { command_scope *c = ctx; c->h->current_command = c->parent; }
static void with_command(void *ctx, qa_command_invocation *command, void (*run)(void *), void *run_ctx) {
  host_t *h = ctx; qa_command_invocation_assert_active(command);
  command_scope scope = {h, h->current_command, run, run_ctx}; h->current_command = command;
  qa_compat_q2_native_mod_finally(command_body, &scope, command_restore, &scope);
}
static bool invoke_command_with(host_t *h, qa_command_invocation *command, void (*run)(void *), void *run_ctx) {
  char *folded = qa_ascii_fold(command->argc > 0 ? command->argv[0] : ""); if (folded == NULL) qa_range_error("Out of memory");
  bool sv = strcmp(folded, "sv") == 0; free(folded);
  if (!sv) return false;
  with_command(h, command, run, run_ctx); return true;
}
static const char *const *service_command(void *ctx, size_t *out_count, const char **out_args) {
  host_t *h = ctx; qa_command_invocation *current = h->current_command;
  if (current != NULL) qa_command_invocation_assert_active(current);
  *out_count = current != NULL ? current->argc : 0; *out_args = current != NULL ? current->args_text : "";
  return current != NULL ? (const char *const *)current->argv : NULL;
}
static void service_add_command(void *ctx, const char *text) {
  host_t *h = ctx; if (h->commands == NULL) unavailable("command buffer");
  qa_mod_command_port_append(h->commands, text, h->current_command != NULL ? &h->current_command->source : NULL);
}
static void service_print(void *ctx, const char *text) { host_t *h = ctx; const qa_session_mod_engine_services *e = h->services->engine; if (e != NULL) e->print(e->ctx, text); }
/* TS output.events.emit(options.source.content, source, {kind:"seconds", value: runtime.now()}). */
static void emit_event(host_t *h, qa_app_sim_presentation_event event) {
  const qa_session_mod_engine_services *engine = h->services->engine;
  qa_session_mod_emit emit; memset(&emit, 0, sizeof emit);
  emit.event = event; emit.event.content = h->options.source.content;
  emit.has_time = true; emit.time.kind = QA_SOURCE_TIME_SECONDS; emit.time.value = runtime_now(h);
  engine->events.emit(engine->events.ctx, &emit);
}
static qa_app_sim_presentation_event source_event(const char *kind) {
  qa_app_sim_presentation_event event; memset(&event, 0, sizeof event); event.kind = kind; return event;
}
static void service_debug_graph(void *ctx, double value, double color) {
  host_t *h = ctx; if (h->services->engine == NULL) unavailable("destination presentation");
  qa_app_sim_presentation_event event = source_event("debug-graph");
  event.debug_graph.kind = "sample"; event.debug_graph.value = value; event.debug_graph.color = color;
  emit_event(h, event);
}
static void service_admit(void *ctx, qa_raw_entity_view record, qa_owned_actor actor) { (void)ctx; (void)record; (void)actor; unavailable("owned actor"); }
static bool service_accepts_client(void *ctx, double slot) { host_t *h = ctx; return h->options.projection.accepts_client(h->options.projection.ctx, slot); }
static void cvar_print(void *ctx, const char *text) { service_print(ctx, text); }

/* --- import boundary --- */
static import_cell *cell_for(host_t *h, const char *name) {
  for (import_cell *cell = h->cells; cell != NULL; cell = cell->next) if (strcmp(cell->name, name) == 0) return cell;
  import_cell *cell = xcalloc(1, sizeof *cell); cell->host = h; cell->name = xstrdup(name); cell->next = h->cells; h->cells = cell; return cell;
}
static qa_guest_call_result replay_import(host_t *h, const char *name, const qa_guest_call_value *values, size_t count) {
  if (!h->rerelease) {
    qa_compat_q2_runner *runner = qa_app_sim_native_guest_runner(h->guest);
    return qa_compat_q2_classic_guest_host_import_call(qa_app_sim_classic_guest_source_host(h->classic_source), name, runner->current_context(runner->ctx), values, count);
  }
  qa_compat_q2_rerelease_import_call call; memset(&call, 0, sizeof call);
  call.api = "game"; call.name = name; call.arguments = values; call.argument_count = count;
  return qa_compat_q2_rerelease_q2_guest_host_dispatch_import(qa_app_sim_rerelease_guest_source_host(h->rr_source), call);
}
static qa_guest_call_result cell_invoke(void *ctx, const qa_guest_call_value *values, size_t count) {
  import_cell *cell = ctx; host_t *h = cell->host;
  if (h->frame != NULL && h->frame->cell == cell) return h->frame->invoke(h->frame->invoke_ctx);
  return replay_import(h, cell->name, values, count);
}
typedef struct boundary_call {
  host_t *h; import_frame frame; const char *name; const qa_guest_call_value *values; size_t count; qa_guest_call_result result;
} boundary_call;
static void boundary_body(void *ctx) {
  boundary_call *b = ctx; const qa_app_sim_native_mod_projection *p = &b->h->options.projection;
  b->result = p->import_boundary(p->ctx, b->name, b->values, b->count, b->frame.cell, cell_invoke);
}
static void boundary_restore(void *ctx) { boundary_call *b = ctx; b->h->frame = b->frame.previous; }
static qa_guest_call_result import_boundary(void *ctx, const char *name, const qa_guest_call_value *values, size_t count,
                                            qa_guest_call_result (*invoke)(void *), void *invoke_ctx) {
  host_t *h = ctx; boundary_call b; memset(&b, 0, sizeof b);
  b.h = h; b.name = name; b.values = values; b.count = count;
  b.frame.cell = cell_for(h, name); b.frame.invoke = invoke; b.frame.invoke_ctx = invoke_ctx; b.frame.previous = h->frame;
  h->frame = &b.frame;
  qa_compat_q2_native_mod_finally(boundary_body, &b, boundary_restore, &b);
  return b.result;
}

/* --- memory observers (MappedGuestMemory.observeWrites with ranges) --- */
static void observer_notify(void *ctx, const qa_guest_written_range *ranges, size_t count) {
  write_observer *o = ctx; qa_compat_q2_written_range stack[16] = {{0, 0}}; qa_compat_q2_written_range *converted = count <= 16 ? stack : xcalloc(count, sizeof *converted);
  for (size_t i = 0; i < count; i++) { converted[i].byte_offset = (double)ranges[i].byte_offset; converted[i].byte_length = (double)ranges[i].byte_length; }
  o->cb(o->ctx, converted, count);
  if (converted != stack) free(converted);
}
static uint64_t observe_writes(void *ctx, qa_guest_address address, double byte_length, void *cb_ctx, qa_compat_q2_written_fn cb) {
  host_t *h = ctx; write_observer *o = xcalloc(1, sizeof *o); o->host = h; o->ctx = cb_ctx; o->cb = cb;
  qa_guest_core_address core = {(uint64_t)address.byte_offset};
  o->handle = qa_sparse_observe_writes(qa_app_sim_native_guest_sparse(h->guest), core, (size_t)byte_length, observer_notify, o);
  o->next = h->observers; h->observers = o; return o->handle;
}
static void unobserve_writes(void *ctx, uint64_t handle) {
  host_t *h = ctx;
  for (write_observer **link = &h->observers; *link != NULL; link = &(*link)->next)
    if ((*link)->handle == handle) { write_observer *o = *link; *link = o->next; qa_sparse_unobserve_writes(qa_app_sim_native_guest_sparse(h->guest), handle); free(o); return; }
}
static void set_instruction_pointer(void *ctx, qa_guest_address address) { qa_app_sim_native_guest_state(of(ctx)->guest)->instruction_pointer = (uint64_t)address.byte_offset; }

/* --- shared helpers --- */
static const char *model_signature(host_t *h, const double indexes[4], double skin) {
  free(h->signature); h->signature = NULL;
  char text[256];
  snprintf(text, sizeof text, "[[%s,%s,%s,%s],%s]", QA_JS_NUMBER(indexes[0]), QA_JS_NUMBER(indexes[1]), QA_JS_NUMBER(indexes[2]), QA_JS_NUMBER(indexes[3]), QA_JS_NUMBER(skin));
  h->signature = xstrdup(text); return h->signature;
}
static const qa_source_weapon_presentation_model *weapon_path_model(host_t *h, const char *path, double frame) {
  if (path == NULL || path[0] == '\0') fail("Original native viewmodel has no model configstring");
  free(h->weapon_path); h->weapon_path = qa_normalize_resource_path(path); if (h->weapon_path == NULL) qa_range_error("Out of memory");
  memset(&h->weapon_model, 0, sizeof h->weapon_model); h->weapon_model.path = h->weapon_path; h->weapon_model.frame = frame;
  return &h->weapon_model;
}
static qa_app_sim_native_mod_appearance appearance_of(qa_app_sim_model_appearance model) {
  qa_app_sim_native_mod_appearance a; memset(&a, 0, sizeof a);
  a.path = model.path; a.skin = model.skin; a.skin_path = model.skin_path; a.attached_model_count = model.attached_model_count;
  for (size_t i = 0; i < model.attached_model_count && i < 3; i++) a.attached_models[i] = model.attached_models[i];
  return a;
}
static void next_frame(host_t *h) { h->options.next_frame(h->options.ctx); }
static void next_frame_fn(void *ctx) { next_frame(ctx); }
static void free_saved(host_t *h) {
  for (size_t i = 0; i < 3; i++) { free(h->saved_bytes[i]); h->saved_bytes[i] = NULL; }
  for (size_t i = 0; i < h->saved_value_count; i++) free(h->saved_values[i]);
  free(h->saved_values); h->saved_values = NULL; h->saved_value_count = 0;
  free(h->saved_configstrings); h->saved_configstrings = NULL;
  memset(&h->save, 0, sizeof h->save);
}
static uint8_t *encode_cvars(host_t *h, size_t *length) {
  qa_save_value *value = qa_cvar_registry_capture_world_transfer_state(h->cvars); if (value == NULL) qa_range_error("Out of memory");
  uint8_t *bytes = qa_encode_checkpoint_value(value, length); qa_save_value_free(value);
  if (bytes == NULL) qa_range_error("Out of memory");
  return bytes;
}
static void restore_cvars(host_t *h, const uint8_t *bytes, size_t length) {
  qa_save_value *value = qa_decode_checkpoint_value(bytes, length); if (value == NULL) fail("Invalid native mod cvar state");
  qa_cvar_registry_restore_save_state(h->cvars, value); qa_save_value_free(value);
}
static void spawn_done(host_t *h) {
  if (h->rerelease) qa_app_sim_rerelease_guest_services_complete_spawn(h->rr); else qa_app_sim_classic_guest_services_complete_spawn(h->classic);
}
/* TS spawn(): SpawnEntities for a declared entity string, then completeSpawn. */
static void spawn(host_t *h) {
  const char *entities = h->declaration->spawn_entities;
  if (entities != NULL) {
    if (h->rerelease) qa_compat_q2_rerelease_q2_guest_host_spawn_entities_loading(qa_app_sim_rerelease_guest_source_host(h->rr_source), h->map, entities, "", next_frame_fn, h);
    else qa_compat_q2_classic_guest_host_spawn_entities_loading(qa_app_sim_classic_guest_source_host(h->classic_source), h->map, entities, "", next_frame_fn, h);
  }
  spawn_done(h);
}

/* --- presentation table over the NativeModPresentation class --- */
static void p_release(void *ctx, qa_actor_id actor) { qa_app_sim_native_mod_presentation_release(of(ctx)->presentation, actor); }
static void p_begin_frame(void *ctx) { qa_app_sim_native_mod_presentation_begin_frame(of(ctx)->presentation); }
static void p_publish(void *ctx, const qa_compat_q2_native_mod_actor_slot *entries, size_t count, bool events) {
  qa_app_sim_native_mod_actor_slot stack[32]; memset(stack, 0, sizeof stack); qa_app_sim_native_mod_actor_slot *rows = count <= 32 ? stack : xcalloc(count, sizeof *rows);
  for (size_t i = 0; i < count; i++) { rows[i].actor = entries[i].actor; rows[i].slot = entries[i].slot; }
  qa_app_sim_native_mod_presentation_publish(of(ctx)->presentation, rows, count, events);
  if (rows != stack) free(rows);
}
static const char *p_signature(void *ctx, double slot) { return qa_app_sim_native_mod_presentation_signature(of(ctx)->presentation, slot); }
static const qa_app_simulation_presentation *p_appearance(void *ctx, qa_actor_id actor, double slot, size_t *out_count) {
  return qa_app_sim_native_mod_presentation_appearance(of(ctx)->presentation, actor, slot, out_count);
}
static double p_generation(void *ctx) { return qa_app_sim_native_mod_presentation_generation(of(ctx)->presentation); }
static bool p_client_frame(void *ctx, qa_actor_id actor, qa_mod_client_presentation_frame *out) { return qa_app_sim_native_mod_presentation_client_frame(of(ctx)->presentation, actor, out); }
static qa_save_value *p_checkpoint(void *ctx) {
  qa_app_sim_native_mod_presentation_checkpoint checkpoint; qa_app_sim_native_mod_presentation_capture(of(ctx)->presentation, &checkpoint);
  qa_save_value *value = qa_app_sim_native_mod_presentation_checkpoint_value(&checkpoint);
  qa_app_sim_native_mod_presentation_checkpoint_free(&checkpoint);
  if (value == NULL) qa_range_error("Out of memory");
  return value;
}
static void p_restore(void *ctx, const qa_save_value *saved) {
  qa_save_reader reader; if (!qa_save_reader_init(&reader, saved, "presentation")) qa_range_error("Out of memory");
  qa_app_sim_native_mod_presentation_checkpoint checkpoint; qa_app_sim_read_native_mod_presentation(&reader, &checkpoint); qa_save_reader_destroy(&reader);
  qa_app_sim_native_mod_presentation_restore(of(ctx)->presentation, &checkpoint);
  qa_app_sim_native_mod_presentation_checkpoint_free(&checkpoint);
}
static void p_close(void *ctx) { qa_app_sim_native_mod_presentation_close(of(ctx)->presentation); }

/* ===================== classic (API 3) ===================== */
static qa_compat_q2_classic_guest_host *classic_host(host_t *h) { return qa_app_sim_classic_guest_source_host(h->classic_source); }
static qa_compat_q2_classic_edicts *classic_edicts(host_t *h) { return qa_compat_q2_classic_guest_host_get_edicts(classic_host(h)); }
static qa_raw_entity_view classic_at(host_t *h, double slot) { return qa_compat_q2_classic_edicts_at(classic_edicts(h), slot); }
static int32_t record_i32(qa_raw_entity_view record, size_t offset) { return qa_compat_q2_view_i32(record.bytes.bytes, offset); }
static bool cp_project(void *ctx, qa_raw_entity_view record, qa_owned_actor *out) { host_t *h = ctx; return h->options.projection.project(h->options.projection.ctx, record, out); }
static qa_guest_address cp_address(void *ctx, qa_actor_id actor) { host_t *h = ctx; return h->options.projection.address(h->options.projection.ctx, actor); }
static void classic_services(void *ctx, qa_compat_q2_memory *memory, qa_compat_q2_classic_engine_services *out) {
  host_t *h = ctx;
  h->classic = qa_app_sim_classic_guest_services_create(memory, &h->shared); if (h->classic == NULL) qa_range_error("Out of memory");
  *out = qa_app_sim_classic_guest_services_engine(h->classic);
  h->classic_projection.ctx = h; h->classic_projection.project = cp_project; h->classic_projection.address = cp_address;
  out->projection = &h->classic_projection;
}
static bool classic_now(void *ctx, double *out) { host_t *h = ctx; return h->context->clock.now_milliseconds(h->context->clock.ctx, out); }
static bool classic_counter(void *ctx, int64_t *out) { host_t *h = ctx; return h->context->clock.performance_counter(h->context->clock.ctx, out); }
static qa_windows_file *classic_open_file(void *ctx, const char *path, bool read, bool write, int creation) {
  return (qa_windows_file *)qa_classic_original_save_files_open_file(of(ctx)->files, path, read, write, creation);
}
/* presentation source */
static const qa_app_sim_configstring *cs_configstrings(void *ctx, size_t *n) { return qa_app_sim_classic_guest_services_configstrings(of(ctx)->classic, n); }
static const qa_app_sim_classic_guest_message *cs_drain(void *ctx, size_t *n) { return qa_app_sim_classic_guest_services_drain_messages(of(ctx)->classic, n); }
static qa_app_sim_native_mod_player_state cs_player(void *ctx, double slot) {
  qa_app_sim_native_mod_player_state state; memset(&state, 0, sizeof state); state.classic = qa_app_sim_classic_guest_services_player_state(of(ctx)->classic, slot); return state;
}
static qa_app_sim_native_mod_entity_state cs_state(void *ctx, double slot) {
  host_t *h = ctx; qa_q2_entity_state state = qa_app_sim_classic_guest_services_entity_state(h->classic, slot); qa_raw_entity_view record = classic_at(h, slot);
  qa_app_sim_native_mod_entity_state out; memset(&out, 0, sizeof out);
  out.active = record_i32(record, 88) != 0; out.visible = (record_i32(record, 184) & 1) == 0;
  out.sound = state.sound; out.event = state.event; out.origin = state.origin; out.volume = 1; out.attenuation = 1;
  return out;
}
static const char *cs_signature(void *ctx, double slot) { host_t *h = ctx; qa_q2_entity_state state = qa_app_sim_classic_guest_services_entity_state(h->classic, slot); return model_signature(h, state.model_indexes, state.skin); }
static qa_app_sim_native_mod_appearance cs_appearance(void *ctx, double slot) {
  host_t *h = ctx; qa_q2_entity_state state = qa_app_sim_classic_guest_services_entity_state(h->classic, slot); qa_raw_entity_view record = classic_at(h, slot);
  qa_app_sim_native_mod_appearance a = appearance_of(qa_app_sim_classic_guest_services_model_appearance(h->classic, slot));
  a.frame = state.frame; a.old_frame = state.frame; a.effects = state.effects; a.render_flags = state.render_effects;
  a.scale = 1; a.alpha = (qa_js_to_int32(state.render_effects) & 32) != 0 ? 0.3 : 1;
  a.visible = record_i32(record, 88) != 0 && (record_i32(record, 184) & 1) == 0; a.origin = state.origin; a.angles = state.angles;
  return a;
}
/* table members */
static void c_synchronize_frame(void *ctx, double seconds, double frame) { host_t *h = ctx; h->has_source_time = true; h->source_time = seconds; h->source_frame = frame; }
static qa_raw_entity_view c_entity(void *ctx, double slot) { return classic_at(of(ctx), slot); }
static bool c_client(void *ctx, double slot, qa_guest_address *out) {
  host_t *h = ctx; qa_compat_q2_memory *memory = h->table.memory;
  return qa_compat_q2_mem_read_pointer(memory, qa_compat_q2_mem_offset(memory, classic_at(h, slot).address, 84), out);
}
static bool c_active(void *ctx, double slot) { return record_i32(classic_at(of(ctx), slot), 88) != 0; }
/* TS writes the borrowed record view directly (no write observers). */
static void c_clear_entity_event(void *ctx, double slot) { qa_compat_q2_view_set_i32((uint8_t *)classic_at(of(ctx), slot).bytes.bytes, 80, 0); }
static const qa_source_weapon_presentation_model *c_weapon_model(void *ctx, double slot) {
  host_t *h = ctx; qa_q2_player_state state = qa_app_sim_classic_guest_services_player_state(h->classic, slot);
  if (state.view.gun_index == 0) return NULL;
  return weapon_path_model(h, qa_app_sim_classic_guest_services_configstring(h->classic, 32 + state.view.gun_index), state.view.gun_frame);
}
static uint8_t *c_encode_trace(void *ctx, const qa_trace_result *trace, size_t *out_length) {
  uint8_t *bytes = xcalloc(56, 1); qa_compat_q2_classic_guest_host_trace_bytes(classic_host(of(ctx)), *trace, bytes); *out_length = 56; return bytes;
}
static void c_server_command(void *ctx) {
  host_t *h = ctx; qa_compat_q2_classic_guest_host *host = classic_host(h);
  (void)qa_compat_q2_classic_guest_host_call(host, "ServerCommand", NULL, 0, NULL, qa_compat_q2_classic_guest_host_get_options(host)->instruction_budget);
}
static bool c_invoke_command(void *ctx, qa_command_invocation *command) { return invoke_command_with(ctx, command, c_server_command, ctx); }
static qa_guest_address c_entry(void *ctx, const char *name) { return qa_app_sim_classic_guest_source_entry(of(ctx)->classic_source, name); }
static qa_compat_q2_native_mod_game_entry c_game_entry(void *ctx, const char *name) {
  host_t *h = ctx; const qa_compat_q2_classic_export *definition = qa_compat_q2_classic_find_export(name);
  if (definition == NULL) qa_raise("Error", 0, 0, "Unknown API3 game export %s", name);
  qa_compat_q2_memory *memory = h->table.memory; qa_compat_q2_native_mod_game_entry entry; memset(&entry, 0, sizeof entry);
  if (!qa_compat_q2_mem_read_pointer(memory, qa_compat_q2_mem_offset(memory, qa_compat_q2_classic_guest_host_get_exports(classic_host(h)), (int64_t)definition->offset), &entry.address))
    qa_raise("Error", 0, 0, "Null API3 game export %s", name);
  entry.signature = definition->signature; return entry;
}
static qa_compat_q2_native_mod_entities c_entities(void *ctx) {
  qa_compat_q2_classic_edict_descriptor d = qa_compat_q2_classic_edicts_descriptor(classic_edicts(of(ctx)));
  qa_compat_q2_native_mod_entities e = {d.base, d.stride, d.count, d.capacity}; return e;
}
static qa_guest_call_result c_invoke(void *ctx, qa_guest_address entry, qa_compat_q2_guest_call_signature signature, const qa_guest_call_value *values, size_t count) {
  qa_compat_q2_classic_guest_host *host = classic_host(of(ctx));
  qa_compat_q2_classic_cvars_refresh(qa_compat_q2_classic_guest_host_get_cvars(host));
  return qa_compat_q2_classic_guest_host_invoke(host, entry, signature, values, count, NULL, qa_compat_q2_classic_guest_host_get_options(host)->instruction_budget);
}
static void c_initialize(void *ctx, bool restoring) {
  host_t *h = ctx; qa_app_sim_classic_guest_source_init_loading(h->classic_source, next_frame_fn, h);
  if (!restoring) spawn(h);
}
static qa_q2_classic_original_identity classic_identity(host_t *h) {
  qa_q2_classic_original_identity identity = {h->table.memory->module, h->context->map_path}; return identity;
}
typedef struct server_state { host_t *h; uint8_t *cvars; size_t cvars_length; qa_q2_classic_configstring_entry *configstrings; } server_state;
static void capture_server(void *ctx, qa_q2_classic_original_server_state *out) {
  server_state *s = ctx; memset(out, 0, sizeof *out);
  s->cvars = encode_cvars(s->h, &s->cvars_length); out->cvars.bytes = s->cvars; out->cvars.length = s->cvars_length;
  size_t count = 0; const qa_app_sim_configstring *table = qa_app_sim_classic_guest_services_configstrings(s->h->classic, &count);
  s->configstrings = xcalloc(count, sizeof *s->configstrings);
  for (size_t i = 0; i < count; i++) { s->configstrings[i].index = table[i].index; s->configstrings[i].value = table[i].value; }
  out->state.configstrings = s->configstrings; out->state.configstring_count = count;
}
static void write_original(void *ctx, const char *game, const char *level, bool autosave) {
  qa_compat_q2_classic_guest_host *host = classic_host(of(ctx));
  qa_compat_q2_classic_guest_host_save(host, "WriteGame", game, autosave); qa_compat_q2_classic_guest_host_save(host, "WriteLevel", level, false);
}
static qa_compat_q2_native_mod_source_save c_checkpoint(void *ctx) {
  host_t *h = ctx; free_saved(h);
  server_state server = {h, NULL, 0, NULL}; qa_q2_classic_original_save save;
  if (!qa_classic_original_save_files_capture(h->files, classic_identity(h), capture_server, &server, write_original, h, false, &save)) qa_range_error("Out of memory");
  qa_provider_checkpoint original; bool encoded = qa_encode_q2_classic_original_save(&save, &original);
  qa_q2_classic_original_save_free(&save); free(server.cvars); free(server.configstrings);
  if (!encoded) qa_range_error("Out of memory");
  h->saved_bytes[0] = (uint8_t *)original.bytes.bytes;
  h->save.rerelease = false; h->save.original = original;
  return h->save;
}
typedef struct classic_read { host_t *h; const qa_q2_classic_original_save *original; } classic_read;
static void read_original(void *ctx, const char *game, const char *level) {
  classic_read *r = ctx; host_t *h = r->h; qa_compat_q2_classic_guest_host *host = classic_host(h);
  qa_compat_q2_classic_guest_host_save_loading(host, "ReadGame", game, next_frame_fn, h); spawn(h);
  for (size_t i = 0; i < r->original->server.state.configstring_count; i++)
    qa_app_sim_classic_guest_services_set_configstring(h->classic, r->original->server.state.configstrings[i].index, r->original->server.state.configstrings[i].value);
  qa_compat_q2_classic_guest_host_save_loading(host, "ReadLevel", level, next_frame_fn, h);
}
static void c_restore(void *ctx, const qa_compat_q2_native_mod_source_save *save) {
  host_t *h = ctx; if (save->rerelease) fail("Native mod save ABI differs");
  qa_q2_classic_original_identity identity = classic_identity(h); qa_q2_classic_original_save original;
  if (!qa_decode_q2_classic_original_save(&save->original, identity, &original)) qa_range_error("Out of memory");
  restore_cvars(h, original.server.cvars.bytes, original.server.cvars.length);
  classic_read read = {h, &original};
  qa_classic_original_save_files_restore_loading(h->files, &original, identity, read_original, &read);
  qa_q2_classic_original_save_free(&original);
}
static void c_close(void *ctx) { host_t *h = ctx; h->closed = true; qa_app_sim_classic_guest_source_close(h->classic_source); }

static void create_classic(host_t *h) {
  h->files = qa_classic_original_save_files_create(NULL, NULL); if (h->files == NULL) qa_range_error("Out of memory");
  qa_app_sim_classic_guest_source_options source; memset(&source, 0, sizeof source);
  source.capabilities = h->context->clock; source.capabilities.ctx = h;
  source.capabilities.now_milliseconds = h->context->clock.now_milliseconds != NULL ? classic_now : NULL;
  source.capabilities.performance_counter = h->context->clock.performance_counter != NULL ? classic_counter : NULL;
  source.capabilities.open_file = classic_open_file;
  source.import_boundary_ctx = h; source.import_boundary = import_boundary;
  source.services_ctx = h; source.services = classic_services;
  h->classic_source = qa_app_sim_classic_guest_source_create(h->options.classic, &source);
  if (h->classic == NULL) { qa_app_sim_classic_guest_source_close(h->classic_source); fail("Classic mod did not initialize its imports"); }
  qa_app_sim_classic_guest_services_bind_host(h->classic, classic_host(h), NULL);
  h->guest = qa_app_sim_classic_guest_source_guest(h->classic_source);
  qa_app_sim_native_mod_presentation_source presentation = {"classic", h, cs_configstrings, cs_drain, cs_appearance, cs_signature, cs_player, presentation_clock, cs_state};
  h->presentation = qa_app_sim_native_mod_presentation_create(presentation, h->options.source.content, &h->options.projection, h->services, h->context->scene,
                                                             h->options.source.provider, h->declaration->client_presentation);
  if (h->presentation == NULL) qa_range_error("Out of memory");
  qa_compat_q2_native_mod_host *t = &h->table;
  t->memory = qa_app_sim_classic_guest_source_memory(h->classic_source); t->image_base = qa_app_sim_classic_guest_source_image_base(h->classic_source);
  t->synchronize_frame = c_synchronize_frame; t->entity = c_entity; t->client = c_client; t->active = c_active; t->clear_entity_event = c_clear_entity_event;
  t->weapon_model = c_weapon_model; t->encode_trace = c_encode_trace; t->invoke_command = c_invoke_command; t->entry = c_entry; t->game_entry = c_game_entry;
  t->entities = c_entities; t->invoke = c_invoke; t->initialize = c_initialize; t->checkpoint = c_checkpoint; t->restore = c_restore; t->close = c_close;
}

/* ===================== rerelease (API 2023) ===================== */
static qa_compat_q2_rerelease_q2_guest_host *rr_host(host_t *h) { return qa_app_sim_rerelease_guest_source_host(h->rr_source); }
static qa_compat_q2_rerelease_guest_module *rr_module(host_t *h) { return qa_compat_q2_rerelease_q2_guest_host_get_module(rr_host(h)); }
static qa_raw_entity_view rr_at(host_t *h, double slot) { qa_raw_entity_table table = qa_compat_q2_rerelease_guest_module_entities(rr_module(h)); return table.at_slot(table.ctx, slot); }
static char *rr_localize(void *ctx, const char *key, const char *const *arguments, size_t count) { host_t *h = ctx; return h->options.localize(h->options.ctx, key, arguments, count); }
static void rr_debug_shapes(void *ctx, qa_compat_q2_rerelease_debug_shapes_event event) {
  qa_q2_rerelease_event rerelease; memset(&rerelease, 0, sizeof rerelease);
  rerelease.kind = "debug-shapes"; rerelease.lines = event.lines; rerelease.line_count = event.line_count;
  rerelease.lifetime_milliseconds = event.lifetime_milliseconds;
  qa_app_sim_presentation_event source = source_event("q2-rerelease"); source.q2_rerelease = &rerelease;
  emit_event(ctx, source);
}
static void rr_world_text(void *ctx, qa_compat_q2_rerelease_world_text_event event) {
  qa_q2_rerelease_event rerelease; memset(&rerelease, 0, sizeof rerelease);
  /* TS RereleaseWorldTextImports emits q2WorldText(...) (the compat mirror keeps its raw arguments). */
  char text[512]; const qa_compat_q2_world_text_input *in = &event.text;
  rerelease.kind = "world-text"; rerelease.lifetime = event.lifetime;
  rerelease.text_value = qa_q2_world_text(in->origin, in->has_angles ? &in->angles : NULL, in->text != NULL ? in->text : "", in->color, in->size, in->depth_test, text, sizeof text);
  qa_app_sim_presentation_event source = source_event("q2-rerelease"); source.q2_rerelease = &rerelease;
  emit_event(ctx, source);
}
static bool sb_project(void *ctx, qa_raw_entity_view view, qa_compat_q2_rerelease_guest_module *module, qa_owned_actor *out) {
  host_t *h = ctx; (void)module; return h->options.projection.project(h->options.projection.ctx, view, out);
}
static qa_guest_address sb_foreign_address(void *ctx, qa_actor_id actor) { host_t *h = ctx; return h->options.projection.address(h->options.projection.ctx, actor); }
static qa_compat_q2_rerelease_actor_bindings sb_bind(void *ctx, qa_raw_entity_view view, qa_owned_actor actor, qa_compat_q2_rerelease_guest_module *module) {
  (void)ctx; (void)view; (void)actor; (void)module; unavailable("owned actor");
}
static qa_compat_q2_rerelease_core_services rr_bind_memory(void *ctx, qa_compat_q2_memory *memory) { return qa_app_sim_rerelease_guest_services_bind_memory(of(ctx)->rr, memory); }
static bool rr_now(void *ctx, double *out) { return classic_now(ctx, out); }
static bool rr_counter(void *ctx, int64_t *out) { return classic_counter(ctx, out); }
/* presentation source */
static const qa_app_sim_configstring *rs_configstrings(void *ctx, size_t *n) { return qa_app_sim_rerelease_guest_services_configstrings(of(ctx)->rr, n); }
static const qa_app_sim_classic_guest_message *rs_drain(void *ctx, size_t *n) { return qa_app_sim_rerelease_guest_services_drain_messages(of(ctx)->rr, n); }
static qa_app_sim_native_mod_player_state rs_player(void *ctx, double slot) {
  qa_app_sim_native_mod_player_state state; memset(&state, 0, sizeof state); state.rerelease = true; state.rerelease_state = qa_app_sim_rerelease_guest_services_player_state(of(ctx)->rr, slot); return state;
}
static qa_app_sim_native_mod_entity_state rs_state(void *ctx, double slot) {
  host_t *h = ctx; qa_q2_rerelease_entity_state state = qa_app_sim_rerelease_guest_services_entity_state(h->rr, slot);
  qa_app_sim_rerelease_entity_info info = qa_app_sim_rerelease_guest_services_entity_info(h->rr, slot);
  qa_app_sim_native_mod_entity_state out; memset(&out, 0, sizeof out);
  out.active = info.active; out.visible = (qa_js_to_int32(info.server_flags) & 1) == 0; out.sound = state.base.sound; out.event = state.base.event;
  out.origin = state.base.origin; out.volume = state.loop_volume; out.attenuation = state.loop_attenuation;
  return out;
}
static const char *rs_signature(void *ctx, double slot) { host_t *h = ctx; qa_q2_rerelease_entity_state state = qa_app_sim_rerelease_guest_services_entity_state(h->rr, slot); return model_signature(h, state.base.model_indexes, state.base.skin); }
static qa_app_sim_native_mod_appearance rs_appearance(void *ctx, double slot) {
  host_t *h = ctx; qa_q2_rerelease_entity_state state = qa_app_sim_rerelease_guest_services_entity_state(h->rr, slot);
  qa_app_sim_rerelease_entity_info info = qa_app_sim_rerelease_guest_services_entity_info(h->rr, slot);
  if (state.effects > (uint64_t)QA_MAX_SAFE_INTEGER) qa_raise("RangeError", 0, 0, "Native mod effects exceed lossless shared presentation");
  qa_app_sim_native_mod_appearance a = appearance_of(qa_app_sim_rerelease_guest_services_model_appearance(h->rr, slot));
  a.frame = state.base.frame; a.old_frame = state.old_frame; a.effects = (double)state.effects; a.render_flags = state.base.render_effects;
  a.scale = state.scale == 0 ? 1 : state.scale;
  a.alpha = state.alpha == 0 ? ((qa_js_to_int32(state.base.render_effects) & 32) != 0 ? 0.3 : 1) : state.alpha;
  a.visible = info.active && (qa_js_to_int32(info.server_flags) & 1) == 0; a.origin = state.base.origin; a.angles = state.base.angles;
  return a;
}
/* table members */
static void r_synchronize_frame(void *ctx, double seconds, double frame) {
  host_t *h = ctx; h->has_source_time = true; h->source_time = seconds; h->source_frame = frame; qa_app_sim_rerelease_guest_services_begin_frame(h->rr, frame);
}
static qa_compat_q2_rerelease_public_edict *public_edict(host_t *h, double slot) {
  qa_compat_q2_rerelease_public_edict *record = qa_compat_q2_rerelease_public_edict_create(h->table.memory, rr_at(h, slot)); if (record == NULL) qa_range_error("Out of memory");
  return record;
}
static void r_project_player_view(void *ctx, double slot, double view_height) {
  host_t *h = ctx; double height = trunc(view_height);
  if (!isfinite(view_height) || height < -128 || height > 127) qa_raise("RangeError", 0, 0, "Selected stance height exceeds API2023 pmove range");
  qa_compat_q2_rerelease_public_edict *record = public_edict(h, slot);
  qa_guest_address client = qa_compat_q2_rerelease_public_edict_client(record); qa_compat_q2_rerelease_public_edict_destroy(record);
  uint8_t byte = (uint8_t)(int8_t)height; h->table.memory->write(h->table.memory->ctx, qa_compat_q2_mem_offset(h->table.memory, client, 48), &byte, 1);
}
static qa_raw_entity_view r_entity(void *ctx, double slot) { return rr_at(of(ctx), slot); }
static bool r_client(void *ctx, double slot, qa_guest_address *out) {
  qa_compat_q2_rerelease_public_edict *record = public_edict(of(ctx), slot);
  bool has = qa_compat_q2_rerelease_public_edict_pointer(record, "client", out); qa_compat_q2_rerelease_public_edict_destroy(record); return has;
}
static bool r_active(void *ctx, double slot) { return qa_app_sim_rerelease_guest_services_entity_info(of(ctx)->rr, slot).active; }
static void r_clear_entity_event(void *ctx, double slot) {
  host_t *h = ctx; qa_compat_q2_rerelease_public_edict *record = public_edict(h, slot);
  qa_guest_address address = qa_compat_q2_rerelease_public_edict_address(record, "s.event"); qa_compat_q2_rerelease_public_edict_destroy(record);
  qa_compat_q2_mem_write_u8(h->table.memory, address, 0);
}
static const qa_source_weapon_presentation_model *r_weapon_model(void *ctx, double slot) {
  host_t *h = ctx; qa_q2_rerelease_player_state state = qa_app_sim_rerelease_guest_services_player_state(h->rr, slot);
  if (state.view.gun_index == 0) return NULL;
  qa_q2_protocol_identity protocol; memset(&protocol, 0, sizeof protocol); protocol.kind = "q2-rerelease"; protocol.version = 1038;
  return weapon_path_model(h, qa_app_sim_rerelease_guest_services_configstring(h->rr, qa_app_q2_application_layout(protocol).models + state.view.gun_index), state.view.gun_frame);
}
static uint8_t *r_encode_trace(void *ctx, const qa_trace_result *trace, size_t *out_length) {
  qa_guest_call_result result = qa_compat_q2_rerelease_q2_guest_host_encode_trace(rr_host(of(ctx)), *trace, NULL);
  if (result.kind != QA_GUEST_VALUE_AGGREGATE) fail("Native trace must be an aggregate");
  uint8_t *bytes = xcalloc(result.bytes.length, 1); memcpy(bytes, result.bytes.bytes, result.bytes.length); *out_length = result.bytes.length; return bytes;
}
static void r_server_command(void *ctx) { (void)qa_compat_q2_rerelease_guest_module_call_game(rr_module(of(ctx)), "ServerCommand", NULL, 0, NULL, NULL); }
static bool r_invoke_command(void *ctx, qa_command_invocation *command) { return invoke_command_with(ctx, command, r_server_command, ctx); }
static qa_guest_address r_entry(void *ctx, const char *name) { return qa_app_sim_rerelease_guest_source_entry(of(ctx)->rr_source, name); }
static qa_compat_q2_native_mod_game_entry r_game_entry(void *ctx, const char *name) {
  host_t *h = ctx; size_t count = 0; const qa_compat_q2_rerelease_api_entry *exports = qa_compat_q2_rerelease_get_game_exports(&count);
  const qa_compat_q2_rerelease_api_entry *definition = qa_compat_q2_rerelease_find_entry(exports, count, name);
  if (definition == NULL) qa_raise("Error", 0, 0, "Unknown API2023 game export %s", name);
  qa_compat_q2_memory *memory = h->table.memory; qa_compat_q2_native_mod_game_entry entry; memset(&entry, 0, sizeof entry);
  qa_guest_address table = qa_compat_q2_rerelease_guest_module_bind_game(rr_module(h));
  double offset = qa_compat_q2_rerelease_field_offset(qa_compat_q2_rerelease_get_game_export_layout(), name);
  if (!qa_compat_q2_mem_read_pointer(memory, qa_compat_q2_mem_offset(memory, table, (int64_t)offset), &entry.address)) qa_raise("Error", 0, 0, "Null API2023 game export %s", name);
  entry.signature = definition->signature; return entry;
}
static qa_compat_q2_native_mod_entities r_entities(void *ctx) {
  qa_raw_entity_table table = qa_compat_q2_rerelease_guest_module_entities(rr_module(of(ctx)));
  qa_compat_q2_native_mod_entities e = {table.base, table.stride_bytes, table.count, table.capacity}; return e;
}
static qa_guest_call_result r_invoke(void *ctx, qa_guest_address entry, qa_compat_q2_guest_call_signature signature, const qa_guest_call_value *values, size_t count) {
  host_t *h = ctx; qa_compat_q2_rerelease_core_imports_refresh_cvars(qa_compat_q2_rerelease_q2_guest_host_get_core(rr_host(h)));
  return qa_compat_q2_rerelease_guest_module_invoke(rr_module(h), entry, signature, values, count, NULL, NULL);
}
static void r_initialize(void *ctx, bool restoring) {
  host_t *h = ctx; qa_app_sim_rerelease_guest_source_init_loading(h->rr_source, next_frame_fn, h);
  if (!restoring) spawn(h);
}
static qa_compat_q2_native_mod_source_save r_checkpoint(void *ctx) {
  host_t *h = ctx; free_saved(h);
  qa_compat_q2_rerelease_source_save game = qa_compat_q2_rerelease_q2_guest_host_write_save_loading(rr_host(h), "game", false, next_frame_fn, h);
  h->saved_bytes[0] = game.native; bool game_extra = game.deferred_damage_count != 0 || game.projection_count != 0;
  qa_compat_q2_rerelease_source_save level = qa_compat_q2_rerelease_q2_guest_host_write_save_loading(rr_host(h), "level", false, next_frame_fn, h);
  h->saved_bytes[1] = level.native;
  if (game_extra || level.deferred_damage_count != 0 || level.projection_count != 0) fail("Unexpected whole-world native mod save state");
  size_t cvars_length = 0; h->saved_bytes[2] = encode_cvars(h, &cvars_length);
  size_t count = 0; const qa_app_sim_configstring *table = qa_app_sim_rerelease_guest_services_configstrings(h->rr, &count);
  h->saved_configstrings = xcalloc(count, sizeof *h->saved_configstrings); h->saved_values = xcalloc(count, sizeof *h->saved_values);
  for (size_t i = 0; i < count; i++) {
    h->saved_values[i] = xstrdup(table[i].value); h->saved_value_count = i + 1;
    h->saved_configstrings[i].index = table[i].index; h->saved_configstrings[i].value = h->saved_values[i];
  }
  h->save.rerelease = true;
  h->save.game.bytes = game.native; h->save.game.length = game.native_length; h->save.level.bytes = level.native; h->save.level.length = level.native_length;
  h->save.cvars.bytes = h->saved_bytes[2]; h->save.cvars.length = cvars_length;
  h->save.configstrings = h->saved_configstrings; h->save.configstring_count = count;
  return h->save;
}
static void r_restore(void *ctx, const qa_compat_q2_native_mod_source_save *save) {
  host_t *h = ctx; if (!save->rerelease) fail("Native mod save ABI differs");
  restore_cvars(h, save->cvars.bytes, save->cvars.length);
  qa_compat_q2_rerelease_core_imports_refresh_cvars(qa_compat_q2_rerelease_q2_guest_host_get_core(rr_host(h)));
  qa_compat_q2_rerelease_source_save game; memset(&game, 0, sizeof game); game.native = (uint8_t *)save->game.bytes; game.native_length = save->game.length;
  qa_compat_q2_rerelease_q2_guest_host_read_save_loading(rr_host(h), "game", game, next_frame_fn, h, "current");
  spawn(h);
  /* TS new Map(entries): the first index position keeps the last value. */
  qa_app_sim_configstring *values = xcalloc(save->configstring_count, sizeof *values); size_t count = 0;
  for (size_t i = 0; i < save->configstring_count; i++) {
    size_t at = 0; while (at < count && values[at].index != save->configstrings[i].index) at++;
    values[at].index = save->configstrings[i].index; values[at].value = save->configstrings[i].value; if (at == count) count++;
  }
  qa_app_sim_rerelease_guest_services_restore_configstrings(h->rr, values, count); free(values);
  qa_compat_q2_rerelease_source_save level; memset(&level, 0, sizeof level); level.native = (uint8_t *)save->level.bytes; level.native_length = save->level.length;
  qa_compat_q2_rerelease_q2_guest_host_read_save_loading(rr_host(h), "level", level, next_frame_fn, h, "current");
}
static void r_close(void *ctx) { host_t *h = ctx; h->closed = true; qa_app_sim_rerelease_guest_source_close(h->rr_source); }

static void create_rerelease(host_t *h) {
  if (h->services->engine == NULL) fail("Native component presentation requires destination engine services");
  qa_app_sim_rerelease_guest_services_options options; memset(&options, 0, sizeof options);
  options.base = h->shared; options.base.engine = h->context->engine(h->context->ctx, h->options.source, &h->runtime);
  options.frame_milliseconds = h->frame_seconds * 1000;
  options.localize.ctx = h; options.localize.localize = rr_localize;
  options.clipboard.client = false;
  options.navigation = h->context->navigation;
  options.debug_shapes.ctx = h; options.debug_shapes.debug_shapes = rr_debug_shapes;
  options.world_text.ctx = h; options.world_text.world_text = rr_world_text;
  h->semantics.ctx = h; h->semantics.project = sb_project; h->semantics.foreign_address = sb_foreign_address; h->semantics.bind = sb_bind;
  options.semantic_bindings = &h->semantics;
  h->rr = qa_app_sim_rerelease_guest_services_create(&options); if (h->rr == NULL) qa_range_error("Out of memory");
  qa_app_sim_rerelease_guest_source_options source; memset(&source, 0, sizeof source);
  qa_app_sim_rerelease_guest_services_host_options(h->rr, &source.host);
  source.pickups = source.host.has_pickups ? source.host.pickup_admission : NULL; source.host.has_pickups = false; source.host.pickup_admission = NULL;
  source.host.import_boundary_ctx = h; source.host.import_boundary = import_boundary;
  source.clock_ctx = h; source.now_milliseconds = rr_now; source.performance_counter = rr_counter; source.performance_frequency = h->context->clock.performance_frequency;
  source.services_ctx = h; source.services = rr_bind_memory;
  h->rr_source = qa_app_sim_rerelease_guest_source_create(h->options.rerelease, &source);
  qa_app_sim_rerelease_guest_services_bind_host(h->rr, rr_host(h));
  h->guest = qa_app_sim_rerelease_guest_source_guest(h->rr_source);
  qa_app_sim_native_mod_presentation_source presentation = {"rerelease", h, rs_configstrings, rs_drain, rs_appearance, rs_signature, rs_player, presentation_clock, rs_state};
  h->presentation = qa_app_sim_native_mod_presentation_create(presentation, h->options.source.content, &h->options.projection, h->services, h->context->scene,
                                                             h->options.source.provider, h->declaration->client_presentation);
  if (h->presentation == NULL) qa_range_error("Out of memory");
  qa_compat_q2_native_mod_host *t = &h->table;
  t->memory = qa_app_sim_rerelease_guest_source_memory(h->rr_source); t->image_base = qa_app_sim_rerelease_guest_source_image_base(h->rr_source);
  t->synchronize_frame = r_synchronize_frame; t->project_player_view = r_project_player_view; t->entity = r_entity; t->client = r_client; t->active = r_active;
  t->clear_entity_event = r_clear_entity_event; t->weapon_model = r_weapon_model; t->encode_trace = r_encode_trace; t->invoke_command = r_invoke_command;
  t->entry = r_entry; t->game_entry = r_game_entry; t->entities = r_entities; t->invoke = r_invoke; t->initialize = r_initialize;
  t->checkpoint = r_checkpoint; t->restore = r_restore; t->close = r_close;
}

/* ===================== createNativeModHost ===================== */
static void register_cvar(host_t *h, const char *name, const char *value) {
  qa_cvar_snapshot out; if (!qa_cvar_registry_register(h->cvars, name, value, 0, &out)) qa_range_error("Out of memory"); qa_cvar_read_free(&out);
}
static void create_cvars(host_t *h) {
  qa_cvar_registry_options options; memset(&options, 0, sizeof options);
  options.dialect = h->rerelease ? QA_COMMAND_DIALECT_Q2_RERELEASE : QA_COMMAND_DIALECT_Q2_CLASSIC;
  options.context.session = qa_session_actor_registry_session(h->services->actors); options.context.origin.kind = QA_COMMAND_ORIGIN_SERVER_CONSOLE;
  options.print = cvar_print; options.print_ctx = h;
  h->cvars = qa_cvar_registry_create(&options); if (h->cvars == NULL) qa_range_error("Out of memory");
  const qa_app_sim_native_mod_host_context *c = h->context;
  register_cvar(h, "maxclients", QA_JS_NUMBER(h->max_clients)); register_cvar(h, "skill", QA_JS_NUMBER(c->skill));
  register_cvar(h, "deathmatch", strcmp(c->mode, "deathmatch") == 0 ? "1" : "0"); register_cvar(h, "coop", strcmp(c->mode, "coop") == 0 ? "1" : "0");
  register_cvar(h, "sv_gravity", QA_JS_NUMBER(c->gravity));
  for (size_t i = 0; i < h->declaration->cvar_count; i++) {
    const qa_mod_named_string *cvar = &h->declaration->cvars[i]; qa_cvar_read found;
    if (!qa_cvar_registry_find(h->cvars, cvar->name, &found)) { register_cvar(h, cvar->name, cvar->value); continue; }
    qa_cvar_read_free(&found);
    qa_cvar_snapshot out; if (!qa_cvar_registry_set(h->cvars, cvar->name, cvar->value, true, &out)) qa_range_error("Out of memory"); qa_cvar_read_free(&out);
  }
  if (h->declaration->clients != NULL) {
    qa_cvar_read found; bool present = qa_cvar_registry_find(h->cvars, "maxclients", &found);
    double value = present ? qa_js_string_to_number(found.value) : NAN; if (present) qa_cvar_read_free(&found);
    if (value != h->max_clients) fail("Native component client capacity differs from maxclients");
  }
}
qa_compat_q2_native_mod_host *qa_app_sim_create_native_mod_host(const qa_app_sim_native_mod_host_options *options) {
  if ((options->classic == NULL) == (options->rerelease == NULL)) qa_range_error("Native mod host requires one prepared guest");
  host_t *h = xcalloc(1, sizeof *h);
  h->options = *options; h->declaration = options->declaration; h->context = options->context; h->services = options->services;
  h->rerelease = options->rerelease != NULL;
  if (!qa_native_provider_timing(options->source, QA_GAME_FAMILY_Q2, h->rerelease, &h->timing)) qa_range_error("Out of memory");
  h->max_clients = h->declaration->clients != NULL ? h->declaration->clients->maximum : h->context->max_clients;
  create_cvars(h);
  h->commands = options->bind_commands != NULL ? options->bind_commands(options->ctx, h->cvars) : NULL;
  h->frame_seconds = h->declaration->source_actors != NULL ? h->declaration->source_actors->frame_seconds : h->rerelease ? h->context->frame_milliseconds / 1000 : 0.1;
  h->random = qa_app_sim_source_random_create(h->services->seed, h->rerelease ? "q2-rerelease" : "classic");
  h->runtime.numeric = h->timing.numeric; h->runtime.random = h->random; h->runtime.ctx = h;
  h->runtime.now = runtime_now; h->runtime.frame_seconds = runtime_frame_seconds; h->runtime.schedule = runtime_schedule;
  qa_app_sim_classic_guest_services_options *shared = &h->shared;
  shared->engine = h->context->engine(h->context->ctx, options->source, &h->runtime); shared->scene = h->context->scene; shared->cvars = h->cvars;
  shared->numeric = *qa_create_numeric_operations(&h->timing.numeric); shared->map_path = h->context->map_path; shared->max_clients = h->max_clients;
  shared->admit.ctx = h; shared->admit.admit = service_admit;
  shared->collision.ctx = h->context->ctx; shared->collision.collision = h->context->collision;
  shared->accepts_client.ctx = h; shared->accepts_client.accepts_client = service_accepts_client;
  shared->print.ctx = h; shared->print.print = service_print;
  shared->command.ctx = h; shared->command.command = service_command;
  shared->add_command.ctx = h; shared->add_command.add_command = service_add_command;
  shared->debug_graph.ctx = h; shared->debug_graph.debug_graph = service_debug_graph;
  /* TS context.mapPath.replace(/^maps\//, "").replace(/\.bsp$/, ""). */
  const char *map = h->context->map_path; if (strncmp(map, "maps/", 5) == 0) map += 5;
  h->map = xstrdup(map); size_t length = strlen(h->map); if (length >= 4 && strcmp(h->map + length - 4, ".bsp") == 0) h->map[length - 4] = '\0';
  qa_compat_q2_native_mod_host *t = &h->table;
  t->ctx = h; t->content = options->source.content; t->abi = h->declaration->target.abi;
  t->presentation.ctx = h; t->presentation.release = p_release; t->presentation.begin_frame = p_begin_frame; t->presentation.publish = p_publish;
  t->presentation.signature = p_signature; t->presentation.appearance = p_appearance; t->presentation.generation = p_generation;
  t->presentation.client_frame = p_client_frame; t->presentation.checkpoint = p_checkpoint; t->presentation.restore = p_restore; t->presentation.close = p_close;
  t->set_instruction_pointer = set_instruction_pointer; t->observe_writes = observe_writes; t->unobserve_writes = unobserve_writes; t->with_command = with_command;
  if (h->rerelease) create_rerelease(h); else create_classic(h);
  t->runner = qa_app_sim_native_guest_runner(h->guest);
  return t;
}
void qa_app_sim_native_mod_host_destroy(qa_compat_q2_native_mod_host *table) {
  if (table == NULL) return;
  host_t *h = table->ctx;
  while (h->observers != NULL) unobserve_writes(h, h->observers->handle);
  while (h->cells != NULL) { import_cell *next = h->cells->next; free(h->cells->name); free(h->cells); h->cells = next; }
  qa_app_sim_native_mod_presentation_destroy(h->presentation);
  if (h->rerelease) { qa_app_sim_rerelease_guest_source_destroy(h->rr_source); qa_app_sim_rerelease_guest_services_destroy(h->rr); }
  else { qa_app_sim_classic_guest_source_destroy(h->classic_source); qa_app_sim_classic_guest_services_destroy(h->classic); qa_classic_original_save_files_destroy(h->files); }
  qa_app_sim_source_random_destroy(h->random); qa_cvar_registry_destroy(h->cvars); qa_native_provider_timing_free(&h->timing);
  free_saved(h); free(h->signature); free(h->weapon_path); free(h->map); free(h);
}
qa_cvar_registry *qa_app_sim_native_mod_host_cvars(const qa_compat_q2_native_mod_host *table) { return ((const host_t *)table->ctx)->cvars; }
qa_app_sim_native_mod_presentation *qa_app_sim_native_mod_host_presentation(const qa_compat_q2_native_mod_host *table) { return ((const host_t *)table->ctx)->presentation; }
