/* Port of src/compat/q2/rerelease/module.ts. GPL-2.0-or-later. */
#include "qa/compat/q2/rerelease/module.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/compat/q2/rerelease/body_shape.h"
#include "qa/compat/q2/rerelease/equipment_movement.h"
#include "qa/compat/q2/rerelease/player_state.h"

void qa_compat_q2_rerelease_missing_import_message(const qa_compat_q2_rerelease_import_call *call, char *out, size_t capacity) {
  const char *address = "unknown"; char hex[32];
  if (call->context.callback.kind == QA_GUEST_CALLBACK_NATIVE_GUEST) {
    snprintf(hex, sizeof hex, "%llx", (unsigned long long)call->context.callback.address.byte_offset); address = hex;
  }
  snprintf(out, capacity, "Missing Q2 rerelease %s import %s at 0x%s", call->api, call->name, address);
}
noreturn void qa_compat_q2_rerelease_throw_missing_import(const qa_compat_q2_rerelease_import_call *call) {
  char message[512]; qa_compat_q2_rerelease_missing_import_message(call, message, sizeof message);
  qa_range_error("%s", message);
}
qa_guest_call_value qa_compat_q2_rerelease_guest_pointer(bool has, qa_guest_address value) { return qa_compat_q2_call_pointer(has, value); }
qa_guest_call_value qa_compat_q2_rerelease_guest_int(double value) { return qa_compat_q2_call_int32(value); }
qa_guest_call_value qa_compat_q2_rerelease_guest_bool(bool value) { return qa_compat_q2_call_uint32(value ? 1 : 0); }
bool qa_compat_q2_rerelease_result_pointer(qa_guest_call_result result, qa_guest_address *out) {
  if (result.kind != QA_GUEST_VALUE_POINTER) qa_range_error("Guest pointer return required");
  if (result.pointer_value.has_value && out) *out = result.pointer_value.value;
  return result.pointer_value.has_value;
}

typedef struct cb_rec { char *id; qa_compat_q2_rerelease_guest_module *module; const char *api; const char *name; } cb_rec_t;
typedef struct equip_rec { qa_raw_entity_view client; qa_equipment_movement value; bool applied; } equip_rec_t;
struct qa_compat_q2_rerelease_guest_module {
  qa_compat_q2_rerelease_module_options options;
  qa_compat_q2_memory *memory;
  const qa_compat_q2_rerelease_primary_world_profile *world_profile;
  qa_guest_address game_import_address, cgame_import_address;
  cb_rec_t *callbacks; size_t callback_count;
  bool has_game, has_cgame; qa_guest_address game, cgame;
  bool has_entity_fields; struct { qa_guest_address base, stride, count, capacity; } entity_fields;
  bool has_entity_table; qa_raw_entity_table entity_table;
  qa_raw_entity_table entity_private; /* ctx-bearing returned copy */
  bool has_movement; qa_guest_address movement_address; uint64_t movement_binding;
  bool has_input; void *input_ctx; qa_compat_q2_rerelease_input_boundary_fn input_boundary;
  void *active_ctx; bool (*active_fn)(void *); uint64_t input_handle;
  equip_rec_t equipment[32]; size_t equipment_count;
};
static char *xstrdup(const char *s) { size_t n = strlen(s) + 1; char *p = malloc(n); if (p) memcpy(p, s, n); return p; }
static qa_guest_call_result import_cb(void *ctx, const qa_guest_call_context *context, const qa_guest_call_value *args, size_t count) {
  cb_rec_t *rec = ctx; qa_compat_q2_rerelease_guest_module *m = rec->module;
  qa_compat_q2_rerelease_import_call call = {rec->api, rec->name, *context, args, count};
  return m->options.invoke_import(m->options.invoke_import_ctx, call);
}
static qa_guest_address build_imports(qa_compat_q2_rerelease_guest_module *m, const char *api, const qa_compat_q2_rerelease_api_entry *entries, size_t count, const qa_guest_layout *layout) {
  qa_compat_q2_memory *memory = m->memory;
  char label[64]; snprintf(label, sizeof label, "Q2 %s imports", api);
  qa_guest_address address = memory->allocate(memory->ctx, layout->byte_length, true, 8, label);
  double milliseconds = m->options.has_frame_milliseconds ? m->options.frame_milliseconds : 25;
  if (milliseconds != trunc(milliseconds) || milliseconds <= 0 || fmod(1000.0, milliseconds) != 0)
    qa_range_error("Q2 source frame duration requires an integral tick rate");
  qa_compat_q2_mem_write_u32(memory, address, (uint32_t)(1000.0 / milliseconds));
  qa_compat_q2_mem_write_f32(memory, qa_compat_q2_mem_offset(memory, address, 4), milliseconds / 1000);
  qa_compat_q2_mem_write_u32(memory, qa_compat_q2_mem_offset(memory, address, 8), (uint32_t)milliseconds);
  size_t base = m->callback_count;
  m->callbacks = realloc(m->callbacks, (base + count) * sizeof(cb_rec_t));
  for (size_t i = 0; i < count; i++) {
    char id[512]; snprintf(id, sizeof id, "%s:q2-%s-%s", memory->module.id ? memory->module.id : "", api, entries[i].name);
    cb_rec_t *rec = &m->callbacks[base + i]; rec->id = xstrdup(id); rec->module = m; rec->api = api; rec->name = entries[i].name;
    qa_guest_address callback = m->options.runner->bind(m->options.runner->ctx, rec->id, entries[i].signature, rec, import_cb);
    qa_compat_q2_mem_write_pointer(memory, qa_compat_q2_mem_offset(memory, address, (int64_t)qa_compat_q2_rerelease_field_offset(layout, entries[i].name)), true, callback);
  }
  m->callback_count = base + count;
  return address;
}
qa_compat_q2_rerelease_guest_module *qa_compat_q2_rerelease_guest_module_create(const qa_compat_q2_rerelease_module_options *options) {
  qa_compat_q2_rerelease_guest_module *m = calloc(1, sizeof *m); if (!m) return NULL;
  m->options = *options;
  m->memory = options->runner->memory;
  m->world_profile = options->world_profile ? options->world_profile : qa_compat_q2_rerelease_get_primary_world_profile(m->memory->module.digest);
  if (m->world_profile) qa_compat_q2_validate_rerelease_primary_world_profile(m->world_profile, m->memory->module.digest);
  if (m->memory->pointer_bytes != 8) qa_range_error("Rerelease Windows ABI requires 64-bit guest memory");
  size_t n_game, n_cgame;
  const qa_compat_q2_rerelease_api_entry *game = qa_compat_q2_rerelease_get_game_imports(&n_game);
  const qa_compat_q2_rerelease_api_entry *cgame = qa_compat_q2_rerelease_get_cgame_imports(&n_cgame);
  m->game_import_address = build_imports(m, "game", game, n_game, qa_compat_q2_rerelease_get_game_import_layout());
  m->cgame_import_address = build_imports(m, "cgame", cgame, n_cgame, qa_compat_q2_rerelease_get_cgame_import_layout());
  return m;
}
void qa_compat_q2_rerelease_guest_module_destroy(qa_compat_q2_rerelease_guest_module *m) {
  if (!m) return;
  for (size_t i = 0; i < m->callback_count; i++) free(m->callbacks[i].id);
  free(m->callbacks); free(m);
}
const qa_compat_q2_rerelease_module_options *qa_compat_q2_rerelease_guest_module_get_options(const qa_compat_q2_rerelease_guest_module *m) { return &m->options; }
qa_compat_q2_memory *qa_compat_q2_rerelease_guest_module_get_memory(const qa_compat_q2_rerelease_guest_module *m) { return m->memory; }
const qa_compat_q2_rerelease_primary_world_profile *qa_compat_q2_rerelease_guest_module_get_world_profile(const qa_compat_q2_rerelease_guest_module *m) { return m->world_profile; }
const qa_compat_q2_rerelease_primary_world_profile *qa_compat_q2_rerelease_guest_module_require_world_profile(qa_compat_q2_rerelease_guest_module *m) {
  if (!m->world_profile) qa_range_error("Original API2023 world requires an artifact-qualified source profile");
  return m->world_profile;
}
qa_guest_address qa_compat_q2_rerelease_guest_module_get_game_import_address(const qa_compat_q2_rerelease_guest_module *m) { return m->game_import_address; }
qa_guest_address qa_compat_q2_rerelease_guest_module_get_cgame_import_address(const qa_compat_q2_rerelease_guest_module *m) { return m->cgame_import_address; }
static qa_guest_call_context make_context(qa_compat_q2_rerelease_guest_module *m, qa_guest_address target, const qa_raw_entity_view *self, const qa_raw_entity_view *other, bool loading) {
  qa_guest_call_context c; memset(&c, 0, sizeof c);
  c.module = m->memory->module; c.callback.kind = QA_GUEST_CALLBACK_NATIVE_GUEST; c.callback.module = m->memory->module;
  c.callback.address = target; c.callback.abi = qa_compat_q2_rerelease_abi;
  c.parent = loading ? NULL : (m->options.runner->current_context ? m->options.runner->current_context(m->options.runner->ctx) : NULL);
  c.self_view = self; c.other = other;
  return c;
}
qa_guest_call_result qa_compat_q2_rerelease_guest_module_invoke(qa_compat_q2_rerelease_guest_module *m, qa_guest_address target,
                                                                qa_compat_q2_guest_call_signature signature, const qa_guest_call_value *args,
                                                                size_t count, const qa_raw_entity_view *self, const qa_raw_entity_view *other) {
  double budget = m->options.has_instruction_budget ? m->options.instruction_budget : 2000000;
  return m->options.runner->invoke(m->options.runner->ctx, target, signature, args, count, make_context(m, target, self, other, false), budget);
}
static qa_guest_address function_at(qa_compat_q2_rerelease_guest_module *m, qa_guest_address table, const qa_guest_layout *layout, const char *name) {
  qa_guest_address address;
  if (!qa_compat_q2_mem_read_pointer(m->memory, qa_compat_q2_mem_offset(m->memory, table, (int64_t)qa_compat_q2_rerelease_field_offset(layout, name)), &address))
    qa_range_error("Q2 rerelease export %s is null", name);
  m->memory->check(m->memory->ctx, address, 1, "execute");
  return address;
}
qa_guest_address qa_compat_q2_rerelease_guest_module_bind_game(qa_compat_q2_rerelease_guest_module *m) {
  if (m->has_game) return m->game;
  qa_guest_call_value arg = qa_compat_q2_call_pointer(true, m->game_import_address);
  qa_guest_call_result result = qa_compat_q2_rerelease_guest_module_invoke(m, m->options.get_game_api, qa_compat_q2_rerelease_get_api_signature(), &arg, 1, NULL, NULL);
  qa_guest_address address;
  if (!qa_compat_q2_rerelease_result_pointer(result, &address)) qa_range_error("GetGameAPI returned null");
  m->memory->check(m->memory->ctx, address, qa_compat_q2_rerelease_get_game_export_layout()->byte_length, "read");
  if (qa_compat_q2_mem_read_i32(m->memory, address) != 2023) qa_range_error("Q2 rerelease game API must be 2023");
  m->has_game = true; m->game = address; return address;
}
qa_guest_address qa_compat_q2_rerelease_guest_module_bind_cgame(qa_compat_q2_rerelease_guest_module *m) {
  if (m->has_cgame) return m->cgame;
  qa_guest_call_value arg = qa_compat_q2_call_pointer(true, m->cgame_import_address);
  qa_guest_call_result result = qa_compat_q2_rerelease_guest_module_invoke(m, m->options.get_cgame_api, qa_compat_q2_rerelease_get_api_signature(), &arg, 1, NULL, NULL);
  qa_guest_address address;
  if (!qa_compat_q2_rerelease_result_pointer(result, &address)) qa_range_error("GetCGameAPI returned null");
  m->memory->check(m->memory->ctx, address, qa_compat_q2_rerelease_get_cgame_export_layout()->byte_length, "read");
  if (qa_compat_q2_mem_read_i32(m->memory, address) != 2022) qa_range_error("Q2 rerelease cgame API must be 2022");
  m->has_cgame = true; m->cgame = address; return address;
}
qa_guest_call_result qa_compat_q2_rerelease_guest_module_call_game(qa_compat_q2_rerelease_guest_module *m, const char *name,
                                                                   const qa_guest_call_value *args, size_t count,
                                                                   const qa_raw_entity_view *self, const qa_raw_entity_view *other) {
  size_t n; const qa_compat_q2_rerelease_api_entry *entries = qa_compat_q2_rerelease_get_game_exports(&n);
  const qa_compat_q2_rerelease_api_entry *entry = qa_compat_q2_rerelease_find_entry(entries, n, name);
  if (!entry) qa_range_error("Unknown game export %s", name);
  return qa_compat_q2_rerelease_guest_module_invoke(m, function_at(m, qa_compat_q2_rerelease_guest_module_bind_game(m),
      qa_compat_q2_rerelease_get_game_export_layout(), name), entry->signature, args, count, self, other);
}
static qa_guest_call_result invoke_loading(qa_compat_q2_rerelease_guest_module *m, qa_guest_address target, qa_compat_q2_guest_call_signature signature,
                                           const qa_guest_call_value *args, size_t count, void (*next_frame)(void *), void *next_ctx, double budget) {
  return m->options.runner->invoke_loading(m->options.runner->ctx, target, signature, args, count, make_context(m, target, NULL, NULL, true), budget, next_frame, next_ctx);
}
qa_guest_call_result qa_compat_q2_rerelease_guest_module_call_game_loading(qa_compat_q2_rerelease_guest_module *m, const char *name,
                                                                           const qa_guest_call_value *args, size_t count,
                                                                           void (*next_frame)(void *), void *next_ctx) {
  double loading_budget = m->options.has_loading_budget ? m->options.loading_instruction_budget
                        : m->options.has_instruction_budget ? m->options.instruction_budget : 50000000;
  if (!m->has_game) {
    qa_guest_call_value arg = qa_compat_q2_call_pointer(true, m->game_import_address);
    qa_guest_call_result result = invoke_loading(m, m->options.get_game_api, qa_compat_q2_rerelease_get_api_signature(), &arg, 1, next_frame, next_ctx, loading_budget);
    qa_guest_address address;
    if (!qa_compat_q2_rerelease_result_pointer(result, &address)) qa_range_error("GetGameAPI returned null");
    m->memory->check(m->memory->ctx, address, qa_compat_q2_rerelease_get_game_export_layout()->byte_length, "read");
    if (qa_compat_q2_mem_read_i32(m->memory, address) != 2023) qa_range_error("Q2 rerelease game API must be 2023");
    m->has_game = true; m->game = address;
  }
  size_t n; const qa_compat_q2_rerelease_api_entry *entries = qa_compat_q2_rerelease_get_game_exports(&n);
  const qa_compat_q2_rerelease_api_entry *entry = qa_compat_q2_rerelease_find_entry(entries, n, name);
  if (!entry) qa_range_error("Unknown game export %s", name);
  bool serialization = strcmp(name, "WriteGameJson") == 0 || strcmp(name, "WriteLevelJson") == 0 || strcmp(name, "ReadGameJson") == 0 || strcmp(name, "ReadLevelJson") == 0;
  double budget = serialization ? (m->options.has_serialization_budget ? m->options.serialization_instruction_budget : 100000000) : loading_budget;
  return invoke_loading(m, function_at(m, m->game, qa_compat_q2_rerelease_get_game_export_layout(), name), entry->signature, args, count, next_frame, next_ctx, budget);
}
qa_guest_call_result qa_compat_q2_rerelease_guest_module_call_cgame(qa_compat_q2_rerelease_guest_module *m, const char *name,
                                                                    const qa_guest_call_value *args, size_t count) {
  size_t n; const qa_compat_q2_rerelease_api_entry *entries = qa_compat_q2_rerelease_get_cgame_exports(&n);
  const qa_compat_q2_rerelease_api_entry *entry = qa_compat_q2_rerelease_find_entry(entries, n, name);
  if (!entry) qa_range_error("Unknown cgame export %s", name);
  return qa_compat_q2_rerelease_guest_module_invoke(m, function_at(m, qa_compat_q2_rerelease_guest_module_bind_cgame(m),
      qa_compat_q2_rerelease_get_cgame_export_layout(), name), entry->signature, args, count, NULL, NULL);
}
void qa_compat_q2_rerelease_guest_module_pre_init(qa_compat_q2_rerelease_guest_module *m) { (void)qa_compat_q2_rerelease_guest_module_call_game(m, "PreInit", NULL, 0, NULL, NULL); }
void qa_compat_q2_rerelease_guest_module_init(qa_compat_q2_rerelease_guest_module *m) { (void)qa_compat_q2_rerelease_guest_module_call_game(m, "Init", NULL, 0, NULL, NULL); }
void qa_compat_q2_rerelease_guest_module_prep_frame(qa_compat_q2_rerelease_guest_module *m) { (void)qa_compat_q2_rerelease_guest_module_call_game(m, "PrepFrame", NULL, 0, NULL, NULL); }
void qa_compat_q2_rerelease_guest_module_run_frame(qa_compat_q2_rerelease_guest_module *m, bool main_loop) {
  qa_guest_call_value v = qa_compat_q2_rerelease_guest_bool(main_loop);
  (void)qa_compat_q2_rerelease_guest_module_call_game(m, "RunFrame", &v, 1, NULL, NULL);
}
qa_guest_address qa_compat_q2_rerelease_guest_module_string(qa_compat_q2_rerelease_guest_module *m, const char *text) {
  qa_guest_address address = qa_compat_q2_allocate_native_memory(m->memory, (double)strlen(text) + 1, "Q2 API string");
  m->memory->write(m->memory->ctx, address, (const uint8_t *)text, strlen(text));
  return address;
}
typedef struct slot_cell { qa_compat_q2_rerelease_guest_module *m; double slot; } slot_cell_t;
static qa_actor_id_result slot_current_actor(void *ctx) {
  slot_cell_t *cell = ctx; qa_actor_id_result r; memset(&r, 0, sizeof r);
  qa_actor_id actor;
  if (cell->m->options.actor_at_slot(cell->m->options.actor_ctx, cell->slot, &actor)) { r.has_value = true; r.value = actor; }
  return r;
}
typedef struct table_ctx { qa_compat_q2_rerelease_guest_module *m; qa_guest_address base; uint64_t stride; double stride_bytes, capacity; } table_ctx_t;
static qa_raw_entity_view table_at_slot(void *ctx, double slot) {
  table_ctx_t *t = ctx; qa_compat_q2_memory *memory = t->m->memory;
  if (slot != trunc(slot) || fabs(slot) > 9007199254740991.0 || slot < 0 || slot >= t->capacity)
    qa_range_error("Q2 edict slot is outside source capacity");
  qa_guest_address address = qa_compat_q2_mem_offset(memory, t->base, (int64_t)(slot * t->stride_bytes));
  slot_cell_t *cell = malloc(sizeof *cell); cell->m = t->m; cell->slot = slot;
  qa_raw_entity_view view; memset(&view, 0, sizeof view);
  view.ctx = cell; view.module = memory->module; view.slot = slot; view.address = address;
  view.stride_bytes = t->stride_bytes; view.public_layout = qa_compat_q2_rerelease_edict_layout;
  view.bytes.bytes = memory->borrow(memory->ctx, address, t->stride_bytes); view.bytes.length = (size_t)t->stride_bytes;
  view.current_actor = slot_current_actor;
  return view;
}
static qa_raw_entity_view table_from_pointer(void *ctx, qa_guest_address address) {
  table_ctx_t *t = ctx; qa_compat_q2_memory *memory = t->m->memory;
  memory->check(memory->ctx, address, qa_compat_q2_rerelease_edict_layout.byte_length, "read");
  int64_t delta = address.byte_offset - t->base.byte_offset;
  if (delta < 0 || delta % (int64_t)t->stride != 0 || delta / (int64_t)t->stride >= (int64_t)t->capacity)
    qa_range_error("Pointer does not identify a Q2 edict");
  return table_at_slot(ctx, (double)(delta / (int64_t)t->stride));
}
qa_raw_entity_table qa_compat_q2_rerelease_guest_module_entities(qa_compat_q2_rerelease_guest_module *m) {
  qa_compat_q2_rerelease_layouts_ensure();
  if (!m->has_entity_fields) {
    qa_guest_address table = qa_compat_q2_rerelease_guest_module_bind_game(m);
    const qa_guest_layout *layout = qa_compat_q2_rerelease_get_game_export_layout();
    m->entity_fields.base = qa_compat_q2_mem_offset(m->memory, table, (int64_t)qa_compat_q2_rerelease_field_offset(layout, "edicts"));
    m->entity_fields.stride = qa_compat_q2_mem_offset(m->memory, table, (int64_t)qa_compat_q2_rerelease_field_offset(layout, "edict_size"));
    m->entity_fields.count = qa_compat_q2_mem_offset(m->memory, table, (int64_t)qa_compat_q2_rerelease_field_offset(layout, "num_edicts"));
    m->entity_fields.capacity = qa_compat_q2_mem_offset(m->memory, table, (int64_t)qa_compat_q2_rerelease_field_offset(layout, "max_edicts"));
    m->has_entity_fields = true;
  }
  qa_guest_address base; bool has_base = qa_compat_q2_mem_read_pointer(m->memory, m->entity_fields.base, &base);
  uint64_t stride = qa_compat_q2_mem_read_u64(m->memory, m->entity_fields.stride);
  double count = qa_compat_q2_mem_read_u32(m->memory, m->entity_fields.count), capacity = qa_compat_q2_mem_read_u32(m->memory, m->entity_fields.capacity);
  if (!has_base) qa_range_error("Q2 game edicts are not initialized");
  if (stride < (uint64_t)qa_compat_q2_rerelease_edict_layout.byte_length || stride > 9007199254740991ULL || count > capacity)
    qa_range_error("Invalid Q2 rerelease edict table");
  double stride_bytes = (double)stride;
  if (m->has_entity_table && m->entity_table.base.byte_offset == base.byte_offset && m->entity_table.stride_bytes == stride_bytes &&
      m->entity_table.count == count && m->entity_table.capacity == capacity) return m->entity_table;
  table_ctx_t *t = malloc(sizeof *t); *t = (table_ctx_t){m, base, stride, stride_bytes, capacity};
  qa_raw_entity_table table; memset(&table, 0, sizeof table);
  table.ctx = t; table.module = m->memory->module; table.base = base; table.stride_bytes = stride_bytes;
  table.count = count; table.capacity = capacity; table.layout = qa_compat_q2_rerelease_edict_layout;
  table.at_slot = table_at_slot; table.from_pointer = table_from_pointer;
  m->entity_table = table; m->has_entity_table = true;
  return table;
}
static void release_movement(qa_compat_q2_rerelease_guest_module *m) {
  if (m->has_input || m->equipment_count != 0) return;
  if (m->has_movement) m->options.runner->remove_entry(m->options.runner->ctx, m->movement_binding);
  m->has_movement = false;
}
typedef struct move_ctx { qa_compat_q2_rerelease_guest_module *m; const qa_guest_call_value *values; size_t count;
  qa_guest_address entry; qa_compat_q2_guest_call_signature signature; } move_ctx_t;
typedef struct exec_ctx { move_ctx_t *move; } exec_ctx_t;
static void move_execute(void *vctx) {
  exec_ctx_t *ex = vctx; move_ctx_t *mv = ex->move; qa_compat_q2_rerelease_guest_module *m = mv->m;
  qa_guest_call_result result = qa_compat_q2_rerelease_guest_module_invoke(m, mv->entry, mv->signature, mv->values, mv->count, NULL, NULL);
  if (result.kind != QA_GUEST_VALUE_VOID) qa_range_error("Pmove returned a non-void result");
}
typedef struct equip_wrap { qa_compat_q2_rerelease_guest_module *m; qa_guest_address movement; const qa_equipment_movement *equipment; const qa_movement_body_shape *body; void *exec; } equip_wrap_t;
static void body_wrap_run(void *vctx) {
  equip_wrap_t *w = vctx;
  typedef struct { move_ctx_t *move; } e_t; e_t *ex = w->exec;
  qa_compat_q2_with_rerelease_body_shape(w->m, w->movement, (w->equipment && w->equipment->pose) ? NULL : w->body, ex, move_execute);
}
static void move_run(void *vctx, const qa_movement_body_shape *body) {
  move_ctx_t *mv = vctx; qa_compat_q2_rerelease_guest_module *m = mv->m;
  qa_guest_address movement = qa_compat_q2_guest_required_pointer(mv->values, mv->count, 0);
  equip_rec_t *scope = m->equipment_count ? &m->equipment[m->equipment_count - 1] : NULL;
  qa_guest_address player; bool has_player = qa_compat_q2_mem_read_pointer(m->memory,
      qa_compat_q2_mem_offset(m->memory, movement, (int64_t)qa_compat_q2_rerelease_field_offset(&qa_compat_q2_rerelease_pmove_layout, "player")), &player);
  equip_rec_t *equipment = scope && has_player && player.byte_offset == scope->client.address.byte_offset ? scope : NULL;
  if (equipment && !equipment->applied) { qa_compat_q2_prepare_rerelease_equipment_movement(m, movement, &equipment->value); equipment->applied = true; }
  exec_ctx_t ex = {mv};
  equip_wrap_t w = {m, movement, equipment ? &equipment->value : NULL, body, &ex};
  qa_compat_q2_with_rerelease_equipment_movement(m, movement, w.equipment, &w, body_wrap_run);
}
static qa_guest_call_result movement_invoke(void *ctx, const qa_guest_call_context *context, const qa_guest_call_value *values, size_t count) {
  (void)context; move_ctx_t *mv_template = ctx; qa_compat_q2_rerelease_guest_module *m = mv_template->m;
  move_ctx_t mv = *mv_template; mv.values = values; mv.count = count;
  qa_guest_address movement = qa_compat_q2_guest_required_pointer(values, count, 0);
  if (m->has_input && m->active_fn && m->active_fn(m->active_ctx)) m->input_boundary(m->input_ctx, movement, &mv, move_run);
  else move_run(&mv, NULL);
  return qa_compat_q2_call_void();
}
static bool movement_accepts(void *ctx) {
  qa_compat_q2_rerelease_guest_module *m = ctx;
  return (m->has_input && m->active_fn && m->active_fn(m->active_ctx)) || m->equipment_count != 0;
}
static void bind_movement(qa_compat_q2_rerelease_guest_module *m) {
  size_t n; const qa_compat_q2_rerelease_api_entry *entries = qa_compat_q2_rerelease_get_game_exports(&n);
  const qa_compat_q2_rerelease_api_entry *entry = qa_compat_q2_rerelease_find_entry(entries, n, "Pmove");
  if (!entry) qa_range_error("API2023 has no Pmove signature");
  qa_guest_address address = function_at(m, qa_compat_q2_rerelease_guest_module_bind_game(m), qa_compat_q2_rerelease_get_game_export_layout(), "Pmove");
  if (m->has_movement && m->movement_address.byte_offset == address.byte_offset) return;
  if (m->has_movement) m->options.runner->remove_entry(m->options.runner->ctx, m->movement_binding);
  m->has_movement = false;
  move_ctx_t *mv = malloc(sizeof *mv); mv->m = m; mv->values = NULL; mv->count = 0; mv->entry = address; mv->signature = entry->signature;
  char id[512]; snprintf(id, sizeof id, "%s:Pmove", m->memory->module.id ? m->memory->module.id : "");
  m->movement_binding = m->options.runner->bind_entry(m->options.runner->ctx, address, xstrdup(id), entry->signature, mv, movement_invoke, m, movement_accepts);
  m->movement_address = address; m->has_movement = true;
}
uint64_t qa_compat_q2_rerelease_guest_module_bind_input_movement(qa_compat_q2_rerelease_guest_module *m, void *ctx,
                                                                 qa_compat_q2_rerelease_input_boundary_fn boundary,
                                                                 void *active_ctx, bool (*active_fn)(void *)) {
  if (m->has_input) qa_range_error("Native Pmove already has an input owner");
  m->has_input = true; m->input_ctx = ctx; m->input_boundary = boundary; m->active_ctx = active_ctx; m->active_fn = active_fn;
  m->input_handle++;
  bind_movement(m);
  return m->input_handle;
}
void qa_compat_q2_rerelease_guest_module_unbind_input_movement(qa_compat_q2_rerelease_guest_module *m, uint64_t handle) {
  if (m->has_input && handle == m->input_handle) m->has_input = false;
  release_movement(m);
}
qa_compat_q2_rerelease_client_connect qa_compat_q2_rerelease_guest_module_client_connect(
    qa_compat_q2_rerelease_guest_module *m, double slot, const char *userinfo, const char *social_id, bool is_bot) {
  if (strlen(userinfo) >= 2048 || strlen(social_id) >= 256)
    qa_range_error("Rerelease client identity exceeds its source string limits");
  qa_guest_address info = m->memory->allocate(m->memory->ctx, 2048, false, 0, "Q2 mutable client userinfo");
  qa_guest_address social = qa_compat_q2_rerelease_guest_module_string(m, social_id);
  qa_raw_entity_table table = qa_compat_q2_rerelease_guest_module_entities(m);
  qa_raw_entity_view client = table.at_slot(table.ctx, slot);
  m->memory->write(m->memory->ctx, info, (const uint8_t *)userinfo, strlen(userinfo));
  qa_guest_call_value args[] = {qa_compat_q2_call_pointer(true, client.address), qa_compat_q2_call_pointer(true, info),
                                qa_compat_q2_call_pointer(true, social), qa_compat_q2_rerelease_guest_bool(is_bot)};
  qa_guest_call_result result = qa_compat_q2_rerelease_guest_module_call_game(m, "ClientConnect", args, 4, &client, NULL);
  if (result.kind != QA_GUEST_VALUE_UINT32) qa_range_error("ClientConnect did not return its native boolean");
  uint8_t output[2048]; m->memory->copy(m->memory->ctx, info, output, 2048);
  size_t end = 0; while (end < 2048 && output[end] != 0) end++;
  if (end >= 2048) qa_range_error("ClientConnect returned unterminated userinfo");
  char *text = malloc(end + 1); memcpy(text, output, end); text[end] = 0;
  m->memory->unmap(m->memory->ctx, info, 2048);
  m->memory->unmap(m->memory->ctx, social, qa_compat_q2_native_allocation_bytes((double)strlen(social_id) + 1));
  return (qa_compat_q2_rerelease_client_connect){result.number_value != 0, text};
}
void qa_compat_q2_rerelease_guest_module_client_begin(qa_compat_q2_rerelease_guest_module *m, double slot) {
  qa_raw_entity_table table = qa_compat_q2_rerelease_guest_module_entities(m);
  qa_raw_entity_view client = table.at_slot(table.ctx, slot);
  qa_guest_call_value v = qa_compat_q2_call_pointer(true, client.address);
  (void)qa_compat_q2_rerelease_guest_module_call_game(m, "ClientBegin", &v, 1, &client, NULL);
}
void qa_compat_q2_rerelease_guest_module_client_think(qa_compat_q2_rerelease_guest_module *m, double slot,
                                                      qa_q2_rerelease_user_command command, const qa_equipment_movement *equipment) {
  qa_compat_q2_rerelease_layouts_ensure();
  qa_raw_entity_table table = qa_compat_q2_rerelease_guest_module_entities(m);
  qa_raw_entity_view client = table.at_slot(table.ctx, slot);
  qa_guest_address address = m->memory->allocate(m->memory->ctx, qa_compat_q2_rerelease_usercmd_layout.byte_length, true, 4, "Q2 client command");
  bool pushed = false;
  if (equipment) {
    if (m->equipment_count >= 32) qa_range_error("Rerelease equipment scope overflow");
    m->equipment[m->equipment_count++] = (equip_rec_t){client, *equipment, false}; pushed = true;
  }
  if (equipment || m->has_input) bind_movement(m);
  qa_compat_q2_write_rerelease_user_command(m->memory->borrow(m->memory->ctx, address, qa_compat_q2_rerelease_usercmd_layout.byte_length), command);
  qa_guest_call_value args[] = {qa_compat_q2_call_pointer(true, client.address), qa_compat_q2_call_pointer(true, address)};
  (void)qa_compat_q2_rerelease_guest_module_call_game(m, "ClientThink", args, 2, &client, NULL);
  if (pushed) m->equipment_count--;
  release_movement(m);
  m->memory->unmap(m->memory->ctx, address, qa_compat_q2_rerelease_usercmd_layout.byte_length);
}
void qa_compat_q2_rerelease_guest_module_client_disconnect(qa_compat_q2_rerelease_guest_module *m, double slot) {
  qa_raw_entity_table table = qa_compat_q2_rerelease_guest_module_entities(m);
  qa_raw_entity_view client = table.at_slot(table.ctx, slot);
  qa_guest_call_value v = qa_compat_q2_call_pointer(true, client.address);
  (void)qa_compat_q2_rerelease_guest_module_call_game(m, "ClientDisconnect", &v, 1, &client, NULL);
}
void qa_compat_q2_rerelease_guest_module_spawn_entities(qa_compat_q2_rerelease_guest_module *m, const char *map, const char *entities, const char *spawnpoint) {
  const char *texts[] = {map, entities, spawnpoint};
  qa_guest_address addrs[3]; qa_guest_call_value args[3];
  for (int i = 0; i < 3; i++) { addrs[i] = qa_compat_q2_rerelease_guest_module_string(m, texts[i]); args[i] = qa_compat_q2_call_pointer(true, addrs[i]); }
  (void)qa_compat_q2_rerelease_guest_module_call_game(m, "SpawnEntities", args, 3, NULL, NULL);
  for (int i = 0; i < 3; i++) m->memory->unmap(m->memory->ctx, addrs[i], qa_compat_q2_native_allocation_bytes((double)strlen(texts[i]) + 1));
}
void qa_compat_q2_rerelease_guest_module_draw_hud(qa_compat_q2_rerelease_guest_module *m, double split, qa_guest_address server_data,
                                                  const double viewport[4], const double safe_area[4], double scale, double player_number, qa_guest_address player_state) {
  qa_compat_q2_rerelease_layouts_ensure();
  uint8_t *rect_bytes[2]; const double *rects[] = {viewport, safe_area};
  qa_guest_call_value rect_values[2];
  for (int r = 0; r < 2; r++) {
    rect_bytes[r] = malloc(16);
    for (int i = 0; i < 4; i++) qa_compat_q2_view_set_i32(rect_bytes[r], (size_t)i * 4, (int32_t)rects[r][i]);
    memset(&rect_values[r], 0, sizeof rect_values[r]);
    rect_values[r].kind = QA_GUEST_VALUE_AGGREGATE; rect_values[r].layout = qa_compat_q2_rerelease_rectangle_layout;
    rect_values[r].bytes.bytes = rect_bytes[r]; rect_values[r].bytes.length = 16;
  }
  qa_guest_call_value args[] = {qa_compat_q2_rerelease_guest_int(split), qa_compat_q2_call_pointer(true, server_data),
                                rect_values[0], rect_values[1], qa_compat_q2_rerelease_guest_int(scale),
                                qa_compat_q2_rerelease_guest_int(player_number), qa_compat_q2_call_pointer(true, player_state)};
  (void)qa_compat_q2_rerelease_guest_module_call_cgame(m, "DrawHUD", args, 7);
}
