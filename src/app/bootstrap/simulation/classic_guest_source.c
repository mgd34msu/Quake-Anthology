/* Port of src/app/bootstrap/simulation/classic-guest-source.ts, plus the
 * guest composition TS builds inline in both native sources (see the
 * header). GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/classic_guest_source.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/simulation/q2_native_world.h"
#include "qa/compat/q2/classic/world_profile.h"
#include "qa/compat/q2/native_combat_call.h"
#include "qa/compat/q2/native_primary_validation.h"
#include "qa/compat/q2/rerelease/world_profile.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/guest/abi/adapter.h"
#include "qa/guest/pe/exports.h"
#include "qa/guest/pe/format.h"
#include "qa/guest/pe/loader.h"

static void *xcalloc(size_t count, size_t size) { void *p = calloc(count == 0 ? 1 : count, size); if (p == NULL) qa_range_error("Out of memory"); return p; }
static char *xstrdup(const char *text) { size_t n = strlen(text != NULL ? text : ""); char *p = malloc(n + 1); if (p == NULL) qa_range_error("Out of memory"); memcpy(p, text != NULL ? text : "", n + 1); return p; }
static bool same(const char *a, const char *b) { return a != NULL && b != NULL && strcmp(a, b) == 0; }

/* ===================== guest value conversions ===================== */
typedef struct field_cache { const qa_guest_field_layout *source; size_t count; qa_abi_field_layout *fields; struct field_cache *next; } field_cache;
typedef struct bound_callback {
  qa_app_sim_native_guest *guest;
  qa_guest_host_callback callback;   /* core table record (kept alive) */
  char *id;
  qa_compat_q2_guest_call_signature signature; /* caller's (borrowed) */
  qa_abi_value_layout *parameters;   /* owned core parameter layouts */
  void *ctx; qa_compat_q2_host_invoke_fn invoke;
  void *accepts_ctx; bool (*accepts)(void *);
  struct bound_callback *next;
} bound_callback;
typedef struct write_observer { qa_app_sim_native_guest *guest; void *ctx; qa_compat_q2_write_observer_fn notify; qa_guest_write_observer_handle handle; struct write_observer *next; } write_observer;
typedef struct region_binding { void *ctx; void (*intercept)(void *, qa_compat_q2_inline_continuation *); void *accepts_ctx; bool (*accepts)(void *); } region_binding;
typedef enum handle_kind { HANDLE_OBSERVE, HANDLE_ENTRY, HANDLE_REGION } handle_kind;
typedef struct handle_row { uint64_t id; handle_kind kind; qa_guest_entry_handle entry; qa_guest_inline_region_handle *region; region_binding *binding; bound_callback *record; } handle_row;

struct qa_app_sim_native_guest {
  qa_module_identity module;
  char *module_strings[4];
  qa_sparse_guest_memory memory;
  qa_mapped_memory_ext ext;
  qa_guest_mapping mapping_buffer[512];
  qa_guest_callback_table callbacks;
  qa_guest_processor_state state;
  qa_i386_cpu x86; qa_x64_cpu x64; bool x64_active;
  qa_guest_cpu cpu;
  qa_guest_call_runner *runner;
  qa_windows_guest_runtime runtime;
  qa_pe_image image;
  qa_guest_core_address stack, returned;
  qa_compat_q2_memory memory_mirror;
  qa_compat_q2_runner runner_mirror;
  void *variadic_ctx;
  size_t (*variadic_layouts)(void *ctx, qa_callback_id id, const qa_guest_call_value *fixed, size_t fixed_count, qa_guest_value_layout *out, size_t capacity);
  field_cache *fields;
  bound_callback *bound;
  write_observer *observers;
  handle_row *handles; size_t handle_count, handle_capacity; uint64_t next_handle;
  qa_guest_call_context current;
  uint8_t *result_bytes;
  uint8_t *argument_bytes[16]; size_t argument_byte_count;
};

static qa_guest_address to_address(const qa_app_sim_native_guest *g, qa_guest_core_address a) { qa_guest_address out = {&g->memory, (int64_t)a.byte_offset}; return out; }
static qa_guest_core_address to_core(qa_guest_address a) { qa_guest_core_address out = {(uint64_t)a.byte_offset}; return out; }
qa_guest_address qa_app_sim_native_guest_address(const qa_app_sim_native_guest *guest, uint64_t byte_offset) { qa_guest_core_address a = {byte_offset}; return to_address(guest, a); }

static qa_abi_storage storage_of(const char *name) {
  qa_abi_storage out; if (name == NULL || !qa_abi_storage_parse(name, &out)) qa_range_error("Unknown guest storage %s", name != NULL ? name : "");
  return out;
}
static qa_guest_native_abi core_abi(qa_native_abi abi) { qa_guest_native_abi out = {abi.kind, abi.image, (int)abi.pointer_bytes, abi.call}; return out; }
static qa_native_abi contract_abi(qa_guest_native_abi abi) { qa_native_abi out = {abi.kind, abi.image, abi.pointer_bytes, abi.call}; return out; }
static const qa_abi_field_layout *core_fields(qa_app_sim_native_guest *g, const qa_guest_field_layout *fields, size_t count) {
  for (field_cache *c = g->fields; c != NULL; c = c->next) {
    if (c->source != fields || c->count != count) continue;
    bool equal = true;
    for (size_t i = 0; i < count && equal; i++)
      equal = c->fields[i].name == fields[i].name && c->fields[i].byte_offset == (int64_t)fields[i].byte_offset
        && c->fields[i].storage == storage_of(fields[i].storage) && c->fields[i].count == (int64_t)fields[i].count;
    if (equal) return c->fields;
  }
  field_cache *c = xcalloc(1, sizeof *c); c->source = fields; c->count = count; c->fields = xcalloc(count, sizeof *c->fields);
  for (size_t i = 0; i < count; i++) {
    c->fields[i].name = fields[i].name; c->fields[i].byte_offset = (int64_t)fields[i].byte_offset;
    c->fields[i].storage = storage_of(fields[i].storage); c->fields[i].count = (int64_t)fields[i].count;
  }
  c->next = g->fields; g->fields = c; return c->fields;
}
static qa_abi_layout core_layout(qa_app_sim_native_guest *g, const qa_guest_layout *layout) {
  qa_abi_layout out; out.id = layout->id; out.byte_length = (int64_t)layout->byte_length; out.alignment = (int64_t)layout->alignment;
  out.pointer_bytes = (int)layout->pointer_bytes; out.fields = core_fields(g, layout->fields, layout->field_count); out.field_count = layout->field_count;
  return out;
}
static qa_abi_value_layout core_value_layout(qa_app_sim_native_guest *g, const qa_guest_value_layout *layout) {
  qa_abi_value_layout out; memset(&out, 0, sizeof out);
  out.is_aggregate = layout->is_aggregate;
  if (layout->is_aggregate) out.layout = core_layout(g, &layout->layout); else out.storage = storage_of(layout->storage);
  return out;
}
/* parameters must hold signature.parameter_count entries. */
static qa_guest_call_signature core_signature(qa_app_sim_native_guest *g, qa_compat_q2_guest_call_signature signature, qa_abi_value_layout *parameters) {
  qa_guest_call_signature out; memset(&out, 0, sizeof out);
  out.abi = core_abi(signature.abi);
  for (size_t i = 0; i < signature.parameter_count; i++) parameters[i] = core_value_layout(g, &signature.parameters[i]);
  out.parameters = parameters; out.parameter_count = signature.parameter_count;
  out.result_is_void = signature.result_is_void;
  if (!signature.result_is_void) out.result = core_value_layout(g, &signature.result);
  out.variadic = signature.variadic;
  return out;
}
static qa_call_value core_value(qa_app_sim_native_guest *g, const qa_guest_call_value *value) {
  switch (value->kind) {
    case QA_GUEST_VALUE_VOID: return qa_call_void();
    case QA_GUEST_VALUE_INT32: return qa_call_int32(qa_js_to_int32(value->number_value));
    case QA_GUEST_VALUE_UINT32: return qa_call_uint32(qa_js_to_uint32(value->number_value));
    case QA_GUEST_VALUE_INT64: return qa_call_int64(value->int64_value);
    case QA_GUEST_VALUE_UINT64: return qa_call_uint64(value->uint64_value);
    case QA_GUEST_VALUE_FLOAT32: return qa_call_float32(value->number_value);
    case QA_GUEST_VALUE_FLOAT64: return qa_call_float64(value->number_value);
    case QA_GUEST_VALUE_POINTER: return qa_call_pointer(value->pointer_value.has_value, to_core(value->pointer_value.value));
    case QA_GUEST_VALUE_AGGREGATE: {
      qa_call_value out = qa_call_aggregate(core_layout(g, &value->layout), value->bytes.bytes, value->bytes.length);
      if (out.kind != QA_CALL_AGGREGATE) qa_range_error("Out of memory");
      return out;
    }
  }
  qa_range_error("Unknown guest call value kind");
  return qa_call_void();
}
/* Aggregate bytes are borrowed from value; layout supplies the contracts shape. */
static qa_guest_call_value contract_value(const qa_app_sim_native_guest *g, const qa_call_value *value, const qa_guest_value_layout *layout) {
  qa_guest_call_value out; memset(&out, 0, sizeof out);
  switch (value->kind) {
    case QA_CALL_VOID: out.kind = QA_GUEST_VALUE_VOID; break;
    case QA_CALL_INT32: out.kind = QA_GUEST_VALUE_INT32; out.number_value = value->number; break;
    case QA_CALL_UINT32: out.kind = QA_GUEST_VALUE_UINT32; out.number_value = value->number; break;
    case QA_CALL_INT64: out.kind = QA_GUEST_VALUE_INT64; out.int64_value = value->int64_value; break;
    case QA_CALL_UINT64: out.kind = QA_GUEST_VALUE_UINT64; out.uint64_value = value->uint64_value; break;
    case QA_CALL_FLOAT32: out.kind = QA_GUEST_VALUE_FLOAT32; out.number_value = value->number; break;
    case QA_CALL_FLOAT64: out.kind = QA_GUEST_VALUE_FLOAT64; out.number_value = value->number; break;
    case QA_CALL_POINTER: out.kind = QA_GUEST_VALUE_POINTER; out.pointer_value.has_value = value->pointer_has; out.pointer_value.value = to_address(g, value->pointer); break;
    case QA_CALL_AGGREGATE:
      out.kind = QA_GUEST_VALUE_AGGREGATE; out.bytes.bytes = value->bytes; out.bytes.length = value->bytes_len;
      if (layout != NULL && layout->is_aggregate) out.layout = layout->layout;
      else { out.layout.id = value->layout.id; out.layout.byte_length = (double)value->layout.byte_length; out.layout.alignment = (double)value->layout.alignment; out.layout.pointer_bytes = value->layout.pointer_bytes; }
      break;
  }
  return out;
}
static qa_guest_call_context contract_context(const qa_app_sim_native_guest *g, const qa_guest_core_call_context *context) {
  qa_guest_call_context out; memset(&out, 0, sizeof out);
  out.module = g->module;
  if (context->callback_kind == QA_CALLBACK_REF_NATIVE_GUEST) {
    out.callback.kind = QA_GUEST_CALLBACK_NATIVE_GUEST; out.callback.module = g->module;
    out.callback.address = to_address(g, context->callback_address); out.callback.abi = contract_abi(context->callback_abi);
  }
  out.self_view = (const qa_raw_entity_view *)context->self_view; out.other = (const qa_raw_entity_view *)context->other;
  return out;
}
static qa_guest_core_call_context core_context(const qa_app_sim_native_guest *g, const qa_guest_call_context *context) {
  qa_guest_core_call_context out; memset(&out, 0, sizeof out);
  out.module.id = context->module.id; out.module.artifact_path = context->module.artifact_path;
  out.module.digest = context->module.digest; out.module.revision = context->module.revision;
  if (context->callback.kind == QA_GUEST_CALLBACK_NATIVE_GUEST) {
    out.callback_kind = QA_CALLBACK_REF_NATIVE_GUEST; out.callback_module = out.module;
    out.callback_address = to_core(context->callback.address); out.callback_abi = core_abi(context->callback.abi);
  }
  out.self_view = context->self_view; out.other = context->other;
  (void)g;
  return out;
}
static qa_guest_call_result keep_result(qa_app_sim_native_guest *g, qa_call_result *core, const qa_compat_q2_guest_call_signature *signature) {
  qa_guest_call_result out = contract_value(g, core, signature->result_is_void ? NULL : &signature->result);
  if (core->kind == QA_CALL_AGGREGATE) {
    free(g->result_bytes); g->result_bytes = malloc(core->bytes_len == 0 ? 1 : core->bytes_len);
    if (g->result_bytes == NULL) qa_range_error("Out of memory");
    if (core->bytes_len != 0) memcpy(g->result_bytes, core->bytes, core->bytes_len);
    out.bytes.bytes = g->result_bytes;
  }
  qa_call_value_free(core);
  return out;
}

/* ===================== MappedGuestMemory table ===================== */
static qa_guest_address_result m_pointer(void *ctx, int64_t raw) {
  qa_app_sim_native_guest *g = ctx; qa_guest_core_address a; qa_guest_address_result out; memset(&out, 0, sizeof out);
  if (qa_sparse_pointer(&g->memory, (uint64_t)raw, &a)) { out.has_value = true; out.value = to_address(g, a); }
  return out;
}
static qa_guest_address m_offset(void *ctx, qa_guest_address a, int64_t d) { qa_app_sim_native_guest *g = ctx; return to_address(g, qa_sparse_offset(&g->memory, to_core(a), d)); }
static size_t byte_count(double length) {
  if (!(length >= 0) || length != floor(length) || length > 9007199254740991.0) qa_range_error("Invalid guest byte length");
  return (size_t)length;
}
static qa_guest_address m_allocate(void *ctx, double byte_length, bool has_alignment, int64_t alignment, const char *label) {
  qa_app_sim_native_guest *g = ctx; qa_guest_allocation_options o; memset(&o, 0, sizeof o);
  o.byte_length = byte_count(byte_length); o.alignment = has_alignment ? (uint64_t)alignment : 0; o.label = label;
  return to_address(g, qa_sparse_allocate(&g->memory, &o));
}
static qa_guest_address m_allocate_pooled(void *ctx, double byte_length, int64_t alignment, const char *label) {
  qa_app_sim_native_guest *g = ctx; qa_guest_allocation_options o; memset(&o, 0, sizeof o);
  o.byte_length = byte_count(byte_length); o.alignment = (uint64_t)alignment; o.label = label; o.pooled = true;
  return to_address(g, qa_sparse_allocate(&g->memory, &o));
}
static void m_unmap(void *ctx, qa_guest_address a, double byte_length) { qa_app_sim_native_guest *g = ctx; qa_sparse_unmap(&g->memory, to_core(a), byte_count(byte_length)); }
static void m_check(void *ctx, qa_guest_address a, double byte_length, const char *access) {
  qa_app_sim_native_guest *g = ctx;
  qa_guest_access kind = same(access, "write") ? QA_ACCESS_WRITE : same(access, "execute") ? QA_ACCESS_EXECUTE : QA_ACCESS_READ;
  qa_sparse_check(&g->memory, to_core(a), byte_count(byte_length), kind);
}
static uint8_t *m_borrow(void *ctx, qa_guest_address a, double byte_length) { qa_app_sim_native_guest *g = ctx; return qa_sparse_borrow(&g->memory, to_core(a), byte_count(byte_length)); }
static void m_copy(void *ctx, qa_guest_address a, uint8_t *dest, double byte_length) { qa_app_sim_native_guest *g = ctx; qa_sparse_copy(&g->memory, to_core(a), dest, byte_count(byte_length)); }
static void m_copy_into(void *ctx, qa_guest_address a, uint8_t *dest, size_t dest_length, double byte_length) {
  qa_app_sim_native_guest *g = ctx; qa_sparse_copy_into(&g->memory, to_core(a), dest, dest_length, 0, byte_count(byte_length));
}
static void m_write(void *ctx, qa_guest_address a, const uint8_t *bytes, size_t length) { qa_app_sim_native_guest *g = ctx; qa_sparse_write(&g->memory, to_core(a), bytes, length); }
static int64_t m_find_zero(void *ctx, qa_guest_address a, double maximum) { qa_app_sim_native_guest *g = ctx; return qa_sparse_find_zero(&g->memory, to_core(a), byte_count(maximum)); }
static void observer_notify(void *ctx, const qa_guest_written_range *ranges, size_t count) {
  write_observer *o = ctx; (void)ranges; (void)count; o->notify(o->ctx);
}
static uint64_t m_observe(void *ctx, qa_guest_address a, double byte_length, void *cb_ctx, qa_compat_q2_write_observer_fn cb) {
  qa_app_sim_native_guest *g = ctx; write_observer *o = xcalloc(1, sizeof *o);
  o->guest = g; o->ctx = cb_ctx; o->notify = cb;
  o->handle = qa_sparse_observe_writes(&g->memory, to_core(a), byte_count(byte_length), observer_notify, o);
  o->next = g->observers; g->observers = o;
  return o->handle;
}
static void m_unobserve(void *ctx, uint64_t handle) {
  qa_app_sim_native_guest *g = ctx;
  for (write_observer **link = &g->observers; *link != NULL; link = &(*link)->next)
    if ((*link)->handle == handle) { write_observer *o = *link; *link = o->next; qa_sparse_unobserve_writes(&g->memory, handle); free(o); return; }
}

/* ===================== GuestCallRunner table ===================== */
static qa_call_result bound_invoke(void *ctx, const qa_guest_core_call_context *context, const qa_call_value *arguments, size_t count) {
  bound_callback *b = ctx; qa_app_sim_native_guest *g = b->guest;
  qa_guest_call_value stack[16] = {{0}}; qa_guest_call_value *values = count <= 16 ? stack : xcalloc(count, sizeof *values);
  for (size_t i = 0; i < count; i++) values[i] = contract_value(g, &arguments[i], i < b->signature.parameter_count ? &b->signature.parameters[i] : NULL);
  qa_guest_call_context converted = contract_context(g, context);
  qa_guest_call_result result = b->invoke(b->ctx, &converted, values, count);
  if (values != stack) free(values);
  return core_value(g, &result);
}
static bound_callback *bind_record(qa_app_sim_native_guest *g, qa_callback_id id, qa_compat_q2_guest_call_signature signature, void *cb_ctx, qa_compat_q2_host_invoke_fn cb) {
  bound_callback *b = xcalloc(1, sizeof *b);
  b->guest = g; b->id = xstrdup(id); b->signature = signature; b->ctx = cb_ctx; b->invoke = cb;
  b->parameters = xcalloc(signature.parameter_count, sizeof *b->parameters);
  b->callback.id = b->id; b->callback.signature = core_signature(g, signature, b->parameters);
  b->callback.ctx = b; b->callback.invoke = bound_invoke;
  b->next = g->bound; g->bound = b;
  return b;
}
static handle_row *add_handle(qa_app_sim_native_guest *g, handle_kind kind) {
  if (g->handle_count == g->handle_capacity) {
    size_t next = g->handle_capacity == 0 ? 16 : g->handle_capacity * 2;
    handle_row *grown = realloc(g->handles, next * sizeof *grown); if (grown == NULL) qa_range_error("Out of memory");
    g->handles = grown; g->handle_capacity = next;
  }
  handle_row *row = &g->handles[g->handle_count++]; memset(row, 0, sizeof *row);
  row->id = ++g->next_handle; row->kind = kind; return row;
}
static qa_guest_call_result r_invoke_common(qa_app_sim_native_guest *g, qa_guest_address target, qa_compat_q2_guest_call_signature signature,
                                            const qa_guest_call_value *arguments, size_t count, qa_guest_call_context context, double budget,
                                            bool loading, void (*next_frame)(void *), void *next_ctx) {
  if (!(budget > 0) || budget != floor(budget) || budget > 9007199254740991.0) qa_range_error("Guest instruction budget must be positive");
  qa_abi_value_layout params_stack[16]; qa_abi_value_layout *params = signature.parameter_count <= 16 ? params_stack : xcalloc(signature.parameter_count, sizeof *params);
  qa_call_value values_stack[16]; qa_call_value *values = count <= 16 ? values_stack : xcalloc(count, sizeof *values);
  for (size_t i = 0; i < count; i++) values[i] = core_value(g, &arguments[i]);
  qa_guest_call_request request; memset(&request, 0, sizeof request);
  request.target = to_core(target); request.signature = core_signature(g, signature, params);
  request.arguments = values; request.argument_count = count;
  request.context = core_context(g, &context); request.instruction_budget = (int64_t)budget;
  qa_call_result result = loading ? qa_guest_call_runner_invoke_loading(g->runner, &request, next_frame, next_ctx)
                                  : qa_guest_call_runner_invoke(g->runner, &request);
  for (size_t i = 0; i < count; i++) qa_call_value_free(&values[i]);
  if (values != values_stack) free(values);
  if (params != params_stack) free(params);
  return keep_result(g, &result, &signature);
}
static qa_guest_call_result r_invoke(void *ctx, qa_guest_address target, qa_compat_q2_guest_call_signature signature, const qa_guest_call_value *arguments,
                                     size_t count, qa_guest_call_context context, double budget) {
  return r_invoke_common(ctx, target, signature, arguments, count, context, budget, false, NULL, NULL);
}
static qa_guest_call_result r_invoke_loading(void *ctx, qa_guest_address target, qa_compat_q2_guest_call_signature signature, const qa_guest_call_value *arguments,
                                             size_t count, qa_guest_call_context context, double budget, void (*next_frame)(void *), void *next_ctx) {
  return r_invoke_common(ctx, target, signature, arguments, count, context, budget, true, next_frame, next_ctx);
}
static const qa_guest_call_context *r_current_context(void *ctx) {
  qa_app_sim_native_guest *g = ctx; const qa_guest_core_call_context *current = qa_guest_call_runner_current_context(g->runner);
  if (current == NULL) return NULL;
  g->current = contract_context(g, current); return &g->current;
}
static double r_depth(void *ctx) { return (double)qa_guest_call_runner_depth(((qa_app_sim_native_guest *)ctx)->runner); }
static int64_t r_instructions(void *ctx) { return (int64_t)qa_guest_call_runner_instructions_executed(((qa_app_sim_native_guest *)ctx)->runner); }
static uint64_t r_read_rsp(void *ctx, int width) { qa_app_sim_native_guest *g = ctx; return qa_integer_register_file_read(&g->state.registers, QA_REG_RSP, width, false); }
static qa_guest_register register_of(const char *name) {
  for (int reg = QA_REG_RAX; reg <= QA_REG_R15; reg++) if (same(qa_guest_register_name((qa_guest_register)reg), name)) return (qa_guest_register)reg;
  qa_range_error("Unknown guest register %s", name != NULL ? name : "");
  return QA_REG_RAX;
}
static uint64_t r_read_register(void *ctx, const char *name, int width) { qa_app_sim_native_guest *g = ctx; return qa_integer_register_file_read(&g->state.registers, register_of(name), width, false); }
static void r_write_register(void *ctx, const char *name, int width, uint64_t value) { qa_app_sim_native_guest *g = ctx; qa_integer_register_file_write(&g->state.registers, register_of(name), width, value, false); }
static void release_argument_bytes(qa_app_sim_native_guest *g) {
  for (size_t i = 0; i < g->argument_byte_count; i++) free(g->argument_bytes[i]);
  g->argument_byte_count = 0;
}
static qa_guest_call_value argument_value(qa_app_sim_native_guest *g, qa_call_value *core, const qa_guest_value_layout *layout) {
  qa_guest_call_value out = contract_value(g, core, layout);
  if (core->kind == QA_CALL_AGGREGATE) {
    uint8_t *copy = malloc(core->bytes_len == 0 ? 1 : core->bytes_len); if (copy == NULL) qa_range_error("Out of memory");
    if (core->bytes_len != 0) memcpy(copy, core->bytes, core->bytes_len);
    out.bytes.bytes = copy;
    if (g->argument_byte_count == 16) release_argument_bytes(g);
    g->argument_bytes[g->argument_byte_count++] = copy;
  }
  qa_call_value_free(core);
  return out;
}
static const char *combat_field_name(qa_compat_q2_native_combat_field field) {
  static const char *names[] = {"target", "inflictor", "attacker", "direction", "point", "normal", "amount", "knockback", "flags", "cause", "sparks", "kick"};
  return names[field];
}
static qa_guest_call_value r_read_combat_field(void *ctx, const void *call_value, qa_compat_q2_guest_call_signature signature, const char *field) {
  qa_app_sim_native_guest *g = ctx; const qa_compat_q2_native_combat_call *call = call_value;
  int64_t index = -1;
  for (size_t i = 0; i < call->argument_count; i++)
    if (call->arguments[i].kind == QA_Q2CA_FIELD && same(combat_field_name(call->arguments[i].field), field)) index = (int64_t)i;
  qa_abi_value_layout params_stack[16]; qa_abi_value_layout *params = signature.parameter_count <= 16 ? params_stack : xcalloc(signature.parameter_count, sizeof *params);
  qa_guest_call_signature core = core_signature(g, signature, params);
  qa_x86_abi_adapter adapter; qa_x86_abi_adapter_init(&adapter, core.abi);
  qa_call_value value = qa_x86_abi_adapter_argument(&adapter, &g->cpu, &core, index);
  if (params != params_stack) free(params);
  return argument_value(g, &value, index >= 0 && (size_t)index < signature.parameter_count ? &signature.parameters[index] : NULL);
}
static size_t r_adapter_arguments(void *ctx, qa_compat_q2_guest_call_signature signature, qa_guest_call_value *out, size_t capacity) {
  qa_app_sim_native_guest *g = ctx;
  qa_abi_value_layout params_stack[16]; qa_abi_value_layout *params = signature.parameter_count <= 16 ? params_stack : xcalloc(signature.parameter_count, sizeof *params);
  qa_guest_call_signature core = core_signature(g, signature, params);
  qa_x86_abi_adapter adapter; qa_x86_abi_adapter_init(&adapter, core.abi);
  qa_call_value *values = NULL; size_t count = 0;
  if (!qa_x86_abi_adapter_arguments(&adapter, &g->cpu, &core, NULL, 0, &values, &count)) qa_range_error("Out of memory");
  for (size_t i = 0; i < count; i++) {
    if (i < capacity) out[i] = argument_value(g, &values[i], i < signature.parameter_count ? &signature.parameters[i] : NULL);
    else qa_call_value_free(&values[i]);
  }
  free(values);
  if (params != params_stack) free(params);
  return count;
}
static void *r_capture(void *ctx) { qa_app_sim_native_guest *g = ctx; qa_abi_processor_snapshot *s = xcalloc(1, sizeof *s); qa_capture_abi_processor_state(s, &g->state); return s; }
static void r_restore(void *ctx, void *snapshot) { qa_app_sim_native_guest *g = ctx; qa_restore_abi_processor_state(&g->state, snapshot); }
static void r_free_processor(void *ctx, void *snapshot) { (void)ctx; free(snapshot); }
static qa_guest_address r_bind(void *ctx, qa_callback_id id, qa_compat_q2_guest_call_signature signature, void *cb_ctx, qa_compat_q2_host_invoke_fn cb) {
  qa_app_sim_native_guest *g = ctx; bound_callback *b = bind_record(g, id, signature, cb_ctx, cb);
  return to_address(g, qa_guest_callback_table_bind(&g->callbacks, &b->callback));
}
static uint64_t r_observe_entry(void *ctx, qa_guest_address address, void *cb_ctx, void (*cb)(void *)) {
  qa_app_sim_native_guest *g = ctx; handle_row *row = add_handle(g, HANDLE_OBSERVE);
  row->entry = qa_guest_callback_table_observe_entry(&g->callbacks, to_core(address), cb, cb_ctx);
  return row->id;
}
static bool entry_accepts(void *ctx) { bound_callback *b = ctx; return b->accepts(b->accepts_ctx); }
static uint64_t r_bind_entry(void *ctx, qa_guest_address address, qa_callback_id id, qa_compat_q2_guest_call_signature signature,
                             void *cb_ctx, qa_compat_q2_host_invoke_fn cb, void *accepts_ctx, bool (*accepts)(void *)) {
  qa_app_sim_native_guest *g = ctx; bound_callback *b = bind_record(g, id, signature, cb_ctx, cb);
  b->accepts_ctx = accepts_ctx; b->accepts = accepts;
  uint64_t entry = qa_guest_callback_table_bind_entry(&g->callbacks, to_core(address), &b->callback, accepts != NULL ? entry_accepts : NULL, accepts != NULL ? b : NULL);
  handle_row *row = add_handle(g, HANDLE_ENTRY); row->entry = entry; row->record = b;
  return row->id;
}
static void region_intercept(void *ctx, qa_guest_inline_continuation *continuation) {
  region_binding *binding = ctx; qa_compat_q2_inline_continuation converted = {continuation->ctx, continuation->execute, continuation->skip};
  binding->intercept(binding->ctx, &converted);
}
static bool region_accepts(void *ctx) { region_binding *binding = ctx; return binding->accepts(binding->accepts_ctx); }
static uint64_t r_bind_region_accepts(void *ctx, qa_guest_address entry, qa_guest_address join, qa_native_call_abi abi,
                                      void *cb_ctx, void (*cb)(void *, qa_compat_q2_inline_continuation *), void *accepts_ctx, bool (*accepts)(void *)) {
  qa_app_sim_native_guest *g = ctx; region_binding *binding = xcalloc(1, sizeof *binding);
  binding->ctx = cb_ctx; binding->intercept = cb; binding->accepts_ctx = accepts_ctx; binding->accepts = accepts;
  qa_guest_inline_region_handle *handle = qa_guest_call_runner_bind_inline_region(g->runner, to_core(entry), to_core(join), core_abi(abi),
      region_intercept, binding, accepts != NULL ? region_accepts : NULL, accepts != NULL ? binding : NULL);
  handle_row *row = add_handle(g, HANDLE_REGION); row->region = handle; row->binding = binding;
  return row->id;
}
static uint64_t r_bind_region(void *ctx, qa_guest_address entry, qa_guest_address join, qa_native_call_abi abi, void *cb_ctx, void (*cb)(void *, qa_compat_q2_inline_continuation *)) {
  return r_bind_region_accepts(ctx, entry, join, abi, cb_ctx, cb, NULL, NULL);
}
static void release_handle(qa_app_sim_native_guest *g, size_t index) {
  handle_row row = g->handles[index];
  memmove(&g->handles[index], &g->handles[index + 1], (g->handle_count - index - 1) * sizeof *g->handles); g->handle_count--;
  if (row.kind == HANDLE_OBSERVE) qa_guest_callback_table_unobserve_entry(&g->callbacks, row.entry);
  else if (row.kind == HANDLE_ENTRY) qa_guest_callback_table_unbind_entry(&g->callbacks, row.entry);
  else { qa_guest_inline_region_unbind(row.region); qa_guest_inline_region_handle_free(row.region); free(row.binding); }
}
static void r_remove(void *ctx, uint64_t handle) {
  qa_app_sim_native_guest *g = ctx;
  for (size_t i = 0; i < g->handle_count; i++) if (g->handles[i].id == handle) { release_handle(g, i); return; }
}
static void r_unwind(void *ctx, double depth) { qa_app_sim_native_guest *g = ctx; qa_guest_call_runner_unwind(g->runner, (int64_t)depth); }
static bool variadic_layouts(void *ctx, const qa_guest_host_callback *callback, const qa_call_value *fixed, size_t fixed_count,
                             const qa_guest_core_call_context *context, qa_abi_value_layout **out, size_t *out_count) {
  qa_app_sim_native_guest *g = ctx; (void)context;
  if (g->variadic_layouts == NULL) qa_range_error("Variadic host callback requires a layout resolver");
  qa_guest_call_value values_stack[16] = {{0}}; qa_guest_call_value *values = fixed_count <= 16 ? values_stack : xcalloc(fixed_count, sizeof *values);
  for (size_t i = 0; i < fixed_count; i++) values[i] = contract_value(g, &fixed[i], NULL);
  qa_guest_value_layout layouts[64];
  size_t count = g->variadic_layouts(g->variadic_ctx, callback->id, values, fixed_count, layouts, 64);
  if (values != values_stack) free(values);
  if (count > 64) qa_range_error("Too many variadic guest arguments");
  *out = xcalloc(count, sizeof **out); *out_count = count;
  for (size_t i = 0; i < count; i++) (*out)[i] = core_value_layout(g, &layouts[i]);
  return true;
}

/* ===================== memory extension over sparse ===================== */
static qa_guest_core_address e_map(void *ctx, uint64_t base, size_t length, qa_guest_permissions permissions, const char *label, const uint8_t *bytes, size_t bytes_len) {
  qa_app_sim_native_guest *g = ctx; qa_guest_map_options o; memset(&o, 0, sizeof o);
  o.base = base; o.byte_length = length; o.permissions = permissions; o.label = label; o.bytes = bytes; o.bytes_length = bytes_len;
  return qa_sparse_map(&g->memory, &o);
}
static qa_guest_core_address e_allocate(void *ctx, size_t length, bool has_alignment, uint64_t alignment, bool has_permissions, qa_guest_permissions permissions, const char *label) {
  qa_app_sim_native_guest *g = ctx; qa_guest_allocation_options o; memset(&o, 0, sizeof o);
  o.byte_length = length; o.alignment = has_alignment ? alignment : 0; o.permissions = permissions; o.has_permissions = has_permissions; o.label = label;
  return qa_sparse_allocate(&g->memory, &o);
}
static void e_unmap(void *ctx, qa_guest_core_address a, size_t length) { qa_sparse_unmap(&((qa_app_sim_native_guest *)ctx)->memory, a, length); }
static void e_protect(void *ctx, qa_guest_core_address a, size_t length, qa_guest_permissions p) { qa_sparse_protect(&((qa_app_sim_native_guest *)ctx)->memory, a, length, p); }
static const qa_guest_mapping *e_mappings(void *ctx, size_t *count) {
  qa_app_sim_native_guest *g = ctx; size_t n = qa_sparse_mappings(&g->memory, g->mapping_buffer, 512);
  if (n > 512) qa_range_error("Guest mapping table exceeds the host listing");
  *count = n; return g->mapping_buffer;
}
static void e_move(void *ctx, qa_guest_core_address d, qa_guest_core_address s, size_t n) { qa_sparse_move(&((qa_app_sim_native_guest *)ctx)->memory, d, s, n); }
static void e_fill(void *ctx, qa_guest_core_address a, size_t n, int v) { qa_sparse_fill(&((qa_app_sim_native_guest *)ctx)->memory, a, n, (uint32_t)v); }
static int64_t e_find_zero(void *ctx, qa_guest_core_address a, size_t m) { return qa_sparse_find_zero(&((qa_app_sim_native_guest *)ctx)->memory, a, m); }
static bool host_call(void *ctx, qa_guest_core_address address) { return qa_guest_callback_table_enter(ctx, address); }
static qa_guest_execution_stop run_x86(void *ctx, int64_t budget, bool has_return, qa_guest_core_address ret) { return qa_i386_cpu_run(ctx, budget, has_return, ret); }
static qa_guest_execution_stop run_x64(void *ctx, int64_t budget, bool has_return, qa_guest_core_address ret) { return qa_x64_cpu_run(ctx, budget, has_return, ret); }
static const qa_pe_image *no_library(void *ctx, const char *library, const qa_pe_image *requesting) { (void)ctx; (void)library; (void)requesting; return NULL; }

qa_app_sim_native_guest *qa_app_sim_native_guest_create(const qa_app_sim_native_guest_options *options) {
  qa_app_sim_native_guest *g = calloc(1, sizeof *g); if (g == NULL) return NULL;
  g->module_strings[0] = xstrdup(options->module.id); g->module_strings[1] = xstrdup(options->module.artifact_path);
  g->module_strings[2] = xstrdup(options->module.digest); g->module_strings[3] = xstrdup(options->module.revision);
  g->module.id = g->module_strings[0]; g->module.artifact_path = g->module_strings[1]; g->module.digest = g->module_strings[2]; g->module.revision = g->module_strings[3];
  qa_guest_module_identity module = {g->module.id, g->module.artifact_path, g->module.digest, g->module.revision};
  qa_sparse_guest_memory_options memory_options; memset(&memory_options, 0, sizeof memory_options);
  memory_options.module = module; memory_options.pointer_bytes = options->pointer_bytes;
  if (options->managed) qa_sparse_guest_memory_init_managed(&g->memory, &memory_options); else qa_sparse_guest_memory_init(&g->memory, &memory_options);
  g->ext.ctx = g; g->ext.map = e_map; g->ext.allocate = e_allocate; g->ext.unmap = e_unmap; g->ext.protect = e_protect;
  g->ext.mappings = e_mappings; g->ext.move = e_move; g->ext.fill = e_fill; g->ext.find_zero = e_find_zero;
  qa_map_pe_image_options map; memset(&map, 0, sizeof map);
  map.bytes = options->bytes; map.bytes_len = options->length; map.memory = &g->memory.base; map.memory_ext = &g->ext;
  map.memory_module = module; map.has_base = options->has_base; map.base = options->base;
  qa_map_pe_image(&g->image, &map);
  qa_guest_allocation_options stack; memset(&stack, 0, sizeof stack);
  stack.byte_length = 1048576; stack.alignment = 16; stack.label = options->stack_label;
  g->stack = qa_sparse_allocate(&g->memory, &stack);
  qa_guest_allocation_options returned; memset(&returned, 0, sizeof returned);
  returned.byte_length = 16; returned.permissions = QA_PERM_READ_EXECUTE; returned.has_permissions = true; returned.label = options->return_label;
  g->returned = qa_sparse_allocate(&g->memory, &returned);
  if (options->managed) qa_guest_callback_table_init_managed(&g->callbacks, &g->memory); else qa_guest_callback_table_init(&g->callbacks, &g->memory);
  qa_guest_processor_initial_state initial = {options->pointer_bytes == 4 ? QA_GUEST_I386 : QA_GUEST_X86_64, 0,
      g->stack.byte_offset + options->stack_top, 2, options->x87_control_word, 0x1f80, 0xffff};
  if (options->managed) qa_create_managed_guest_processor_state(&g->state, &initial); else qa_create_guest_processor_state(&g->state, &initial);
  g->cpu.state = &g->state; g->cpu.memory = &g->memory.base; g->cpu.memory_ext = &g->ext; g->cpu.module = module;
  if (options->pointer_bytes == 8) {
    qa_x64_cpu_init(&g->x64, &g->state, &g->memory, NULL, NULL, &g->callbacks); g->x64_active = true;
    g->cpu.run_ctx = &g->x64; g->cpu.run = run_x64;
  } else {
    qa_i386_cpu_init(&g->x86, &g->state, &g->memory, host_call, &g->callbacks, NULL, NULL);
    g->cpu.run_ctx = &g->x86; g->cpu.run = run_x86;
  }
  g->variadic_ctx = options->variadic_ctx; g->variadic_layouts = options->variadic_layouts;
  g->runner = calloc(1, qa_guest_call_runner_size()); if (g->runner == NULL) qa_range_error("Out of memory");
  qa_guest_call_runner_options runner; memset(&runner, 0, sizeof runner);
  runner.cpu = &g->cpu; runner.callbacks = &g->callbacks; runner.return_address = g->returned;
  if (options->variadic_layouts != NULL) { runner.variadic_ctx = g; runner.variadic_layouts = variadic_layouts; }
  if (!qa_guest_call_runner_init(g->runner, runner)) qa_range_error("Out of memory");
  qa_windows_runtime_options windows; memset(&windows, 0, sizeof windows);
  windows.memory = &g->memory.base; windows.memory_ext = &g->ext; windows.callbacks = &g->callbacks; windows.module = module;
  windows.has_capabilities = true; windows.capabilities = options->capabilities;
  qa_windows_guest_runtime_init(&g->runtime, &windows);
  qa_windows_guest_runtime_attach_runner(&g->runtime, g->runner);
  qa_compat_q2_memory *m = &g->memory_mirror;
  m->ctx = g; m->module = g->module; m->address_space = &g->memory; m->pointer_bytes = options->pointer_bytes; m->managed = options->managed;
  m->pointer = m_pointer; m->offset = m_offset; m->allocate = m_allocate; m->unmap = m_unmap; m->check = m_check; m->borrow = m_borrow;
  m->copy = m_copy; m->copy_into = m_copy_into; m->write = m_write; m->find_zero = m_find_zero; m->observe_writes = m_observe; m->unobserve_writes = m_unobserve; m->allocate_pooled = m_allocate_pooled;
  qa_compat_q2_runner *r = &g->runner_mirror;
  r->ctx = g; r->memory = m; r->cpu_state = &g->state; r->simd_xmm = g->state.simd.xmm; r->simd_xmm_len = g->state.simd.xmm_len;
  r->invoke = r_invoke; r->invoke_loading = r_invoke_loading; r->current_context = r_current_context; r->depth = r_depth;
  r->instructions_executed = r_instructions; r->read_rsp = r_read_rsp; r->read_combat_field = r_read_combat_field; r->adapter_arguments = r_adapter_arguments;
  r->capture_processor = r_capture; r->restore_processor = r_restore; r->free_processor = r_free_processor;
  r->bind = r_bind; r->observe_entry = r_observe_entry; r->bind_entry = r_bind_entry; r->remove_entry = r_remove;
  r->bind_inline_region = r_bind_region; r->remove_inline_region = r_remove; r->bind_inline_region_accepts = r_bind_region_accepts;
  r->read_register = r_read_register; r->write_register = r_write_register; r->unwind = r_unwind;
  return g;
}
void qa_app_sim_native_guest_destroy(qa_app_sim_native_guest *g) {
  if (g == NULL) return;
  while (g->handle_count > 0) release_handle(g, g->handle_count - 1);
  free(g->handles);
  qa_windows_guest_runtime_destroy(&g->runtime);
  qa_guest_call_runner_destroy(g->runner); free(g->runner);
  if (g->x64_active) qa_x64_cpu_destroy(&g->x64);
  qa_guest_callback_table_destroy(&g->callbacks);
  for (write_observer *o = g->observers; o != NULL;) { write_observer *next = o->next; free(o); o = next; }
  qa_sparse_guest_memory_destroy(&g->memory);
  qa_pe_image_free(&g->image);
  for (bound_callback *b = g->bound; b != NULL;) { bound_callback *next = b->next; free(b->id); free(b->parameters); free(b); b = next; }
  for (field_cache *c = g->fields; c != NULL;) { field_cache *next = c->next; free(c->fields); free(c); c = next; }
  free(g->result_bytes); release_argument_bytes(g);
  for (int i = 0; i < 4; i++) free(g->module_strings[i]);
  free(g);
}
qa_compat_q2_memory *qa_app_sim_native_guest_memory(qa_app_sim_native_guest *g) { return &g->memory_mirror; }
qa_compat_q2_runner *qa_app_sim_native_guest_runner(qa_app_sim_native_guest *g) { return &g->runner_mirror; }
qa_sparse_guest_memory *qa_app_sim_native_guest_sparse(qa_app_sim_native_guest *g) { return &g->memory; }
qa_guest_call_runner *qa_app_sim_native_guest_call_runner(qa_app_sim_native_guest *g) { return g->runner; }
qa_guest_callback_table *qa_app_sim_native_guest_callbacks(qa_app_sim_native_guest *g) { return &g->callbacks; }
qa_guest_processor_state *qa_app_sim_native_guest_state(qa_app_sim_native_guest *g) { return &g->state; }
qa_windows_guest_runtime *qa_app_sim_native_guest_runtime(qa_app_sim_native_guest *g) { return &g->runtime; }
qa_pe_image *qa_app_sim_native_guest_image(qa_app_sim_native_guest *g) { return &g->image; }
qa_guest_address qa_app_sim_native_guest_image_base(qa_app_sim_native_guest *g) { return to_address(g, g->image.image.base); }
qa_guest_address qa_app_sim_native_guest_entry(qa_app_sim_native_guest *g, const char *name) {
  qa_guest_symbol_name symbol = {false, name, NULL, 0};
  return to_address(g, qa_resolve_pe_export(&g->image, symbol, no_library, NULL).address);
}
uint64_t qa_app_sim_native_guest_instructions(qa_app_sim_native_guest *g) { return qa_guest_call_runner_instructions_executed(g->runner); }
static qa_windows_initialize_options lifecycle(qa_app_sim_native_guest *g, qa_guest_address target, qa_native_call_abi abi, double budget) {
  qa_windows_initialize_options o; memset(&o, 0, sizeof o);
  o.context.module.id = g->module.id; o.context.module.artifact_path = g->module.artifact_path; o.context.module.digest = g->module.digest; o.context.module.revision = g->module.revision;
  o.context.callback_kind = QA_CALLBACK_REF_NATIVE_GUEST; o.context.callback_module = o.context.module;
  o.context.callback_address = to_core(target); o.context.callback_abi = core_abi(abi);
  o.instruction_budget = (int64_t)budget;
  return o;
}
void qa_app_sim_native_guest_initialize(qa_app_sim_native_guest *g, qa_guest_address target, qa_native_call_abi abi, double budget) {
  qa_windows_initialize_options o = lifecycle(g, target, abi, budget); qa_windows_guest_runtime_initialize(&g->runtime, &g->image, &o);
}
void qa_app_sim_native_guest_detach(qa_app_sim_native_guest *g, qa_guest_address target, qa_native_call_abi abi, double budget) {
  qa_windows_initialize_options o = lifecycle(g, target, abi, budget); qa_windows_guest_runtime_detach(&g->runtime, &g->image, &o);
}
void qa_app_sim_native_guest_release_memory(qa_app_sim_native_guest *g) {
  size_t count = qa_sparse_mappings(&g->memory, NULL, 0);
  qa_guest_mapping *mappings = xcalloc(count, sizeof *mappings);
  count = qa_sparse_mappings(&g->memory, mappings, count);
  for (size_t i = 0; i < count; i++) {
    qa_guest_core_address address;
    if (qa_sparse_pointer(&g->memory, mappings[i].base, &address)) qa_sparse_unmap(&g->memory, address, mappings[i].byte_length);
  }
  free(mappings);
}
size_t qa_app_sim_native_guest_mapping_count(qa_app_sim_native_guest *g) { return qa_sparse_mappings(&g->memory, NULL, 0); }

/* ===================== preparation ===================== */
typedef struct world_storage { qa_compat_q2_classic_world_profile classic; qa_compat_q2_rerelease_primary_world_profile rerelease; bool is_classic, is_rerelease; } world_storage;
typedef struct compatibility_storage { qa_compat_q2_native_compatibility_result result; world_storage world; } compatibility_storage;
static const void *read_classic_world(void *ctx, const qa_save_reader *reader, qa_content_digest digest) {
  world_storage *w = ctx; if (!qa_compat_q2_read_classic_primary_world_profile(reader, digest, &w->classic)) qa_range_error("Out of memory");
  w->is_classic = true; return &w->classic;
}
static const void *read_rerelease_world(void *ctx, const qa_save_reader *reader, qa_content_digest digest) {
  world_storage *w = ctx; if (!qa_compat_q2_read_rerelease_primary_world_profile(reader, digest, &w->rerelease)) qa_range_error("Out of memory");
  w->is_rerelease = true; return &w->rerelease;
}
typedef struct content_filter { qa_content_id content; } content_filter;
static bool same_content(void *ctx, const qa_content_mount *mount) {
  const content_filter *filter = ctx;
  qa_content_id content = mount->kind == QA_CONTENT_MOUNT_ARCHIVE ? mount->as.archive.identity.content : mount->as.loose.identity.content;
  return same(content, filter->content);
}
qa_compat_q2_native_compatibility_result *qa_app_sim_read_native_compatibility(qa_content_mounted *mounts, const qa_resolved_execution_module *execution) {
  content_filter filter = {execution->owner.content};
  qa_content_opened_resource opened;
  if (!qa_content_mounted_open(mounts, "native-compatibility.json", same_content, &filter, &opened)) return NULL;
  qa_save_value *value = qa_parse_json_value(opened.bytes, opened.byte_length, false);
  if (value == NULL) qa_range_error("Invalid native-compatibility.json");
  compatibility_storage *storage = xcalloc(1, sizeof *storage);
  qa_compat_q2_world_readers readers = {&storage->world, read_classic_world, read_rerelease_world};
  if (!qa_compat_q2_parse_native_compatibility(value, execution, readers, &storage->result)) qa_range_error("Out of memory");
  qa_save_value_free(value); free(opened.bytes);
  if (!storage->result.found) { free(storage); return NULL; }
  storage->result.profile.weapons = &storage->result.weapons; storage->result.profile.player = &storage->result.player;
  storage->result.profile.commands = &storage->result.commands; storage->result.profile.inventory = &storage->result.inventory;
  storage->result.profile.drop = &storage->result.drop; storage->result.profile.pickups = &storage->result.pickups;
  /* The declaration reference is the mounted resource itself. */
  return &storage->result;
}
void qa_app_sim_native_compatibility_free(qa_compat_q2_native_compatibility_result *result) {
  if (result == NULL) return;
  compatibility_storage *storage = (compatibility_storage *)result;
  if (storage->world.is_classic) qa_compat_q2_classic_world_profile_free(&storage->world.classic);
  if (storage->world.is_rerelease) qa_compat_q2_rerelease_world_profile_free(&storage->world.rerelease);
  free(storage);
}
static double primary_entity_bytes(const qa_compat_q2_native_primary_profile *profile) {
  if (same(profile->edition, "classic")) return ((const qa_compat_q2_classic_world_profile *)profile->world)->base.entity_bytes;
  return ((const qa_compat_q2_rerelease_primary_world_profile *)profile->world)->edict.byte_length;
}
/* TS validateNativePrimary(profile, parsePe(bytes)). */
static void validate_primary(const qa_compat_q2_native_primary_profile *profile, const qa_pe_file *pe) {
  qa_compat_q2_pe_section *sections = xcalloc(pe->section_count, sizeof *sections);
  for (size_t i = 0; i < pe->section_count; i++) {
    sections[i].rva = pe->sections[i].rva; sections[i].mapped_size = pe->sections[i].mapped_size;
    sections[i].permissions = qa_guest_permissions_name(pe->sections[i].permissions);
  }
  qa_compat_q2_pe_file file = {pe->image_size, pe->abi.pointer_bytes, sections, pe->section_count};
  qa_compat_q2_validate_native_primary(profile, file, primary_entity_bytes(profile));
  free(sections);
}
qa_compat_q2_native_compatibility_result *qa_app_sim_prepare_native_artifact(const qa_resolved_execution_module *execution, qa_content_mounted *mounts,
                                                                           const char *edition, const char *abi_message, uint8_t **bytes, size_t *length,
                                                                           qa_compat_q2_native_primary_declaration *primary_out) {
  const qa_resolved_resource_reference *artifact = execution->artifact;
  *bytes = qa_content_mounted_read_reference(mounts, artifact, length);
  if (*bytes == NULL) qa_range_error("Native artifact could not be read");
  qa_pe_file pe; qa_parse_pe(&pe, *bytes, *length);
  if (!same(pe.abi.kind, execution->profile.kind)) qa_range_error("%s", abi_message);
  qa_compat_q2_native_compatibility_result *primary = qa_app_sim_read_native_compatibility(mounts, execution);
  if (primary != NULL) validate_primary(&primary->profile, &pe);
  qa_pe_file_free(&pe);
  if (primary != NULL && !same(primary->profile.edition, edition)) qa_range_error("Native declaration edition differs from selected API");
  if (primary != NULL) {
    const qa_resolved_resource_reference *reference = NULL;
    if (!qa_content_mounted_resolve(mounts, "native-compatibility.json", &reference) || reference == NULL) qa_range_error("Native compatibility declaration disappeared");
    primary_out->declaration = *reference; primary_out->profile = primary->profile;
  }
  return primary;
}
static bool classic_api(const qa_resolved_execution_module *execution) {
  return same(execution->role, "server-game") && execution->api.kind == QA_GAME_API_Q2_GAME && execution->api.as.q2_game.kind == QA_Q2_GAME_API_CLASSIC
    && execution->api.as.q2_game.version == 3 && same(execution->profile.kind, "windows-i386");
}
void qa_app_sim_prepare_classic_guest(const qa_resolved_execution_module *execution, qa_content_mounted *mounts, qa_app_sim_prepared_classic_guest *out) {
  memset(out, 0, sizeof *out);
  if (!classic_api(execution)) qa_range_error("Classic Q2 guest requires Windows i386 game API 3");
  qa_compat_q2_native_compatibility_result *primary = qa_app_sim_prepare_native_artifact(execution, mounts, "classic", "Classic native artifact ABI differs from the selected profile",
                                                                                         &out->bytes, &out->length, &out->primary);
  out->edition = "classic"; out->execution = *execution;
  out->has_primary = primary != NULL; out->compatibility = primary;
}
void qa_app_sim_prepared_classic_guest_free(qa_app_sim_prepared_classic_guest *prepared) {
  free(prepared->bytes); qa_app_sim_native_compatibility_free(prepared->compatibility);
  memset(prepared, 0, sizeof *prepared);
}
qa_module_identity qa_app_sim_prepared_module_identity(const qa_resolved_execution_module *execution, bool has_primary,
                                                       const qa_compat_q2_native_primary_declaration *primary, char *scratch, size_t scratch_size) {
  qa_app_sim_prepared_q2_native_guest prepared; memset(&prepared, 0, sizeof prepared);
  prepared.execution = *execution; prepared.has_primary = has_primary;
  if (has_primary) { prepared.primary_requested_path = primary->declaration.requested_path; prepared.primary_digest = primary->declaration.digest; }
  return qa_app_sim_native_module_identity(prepared, scratch, scratch_size);
}

/* ===================== ClassicGuestSource ===================== */
typedef enum source_phase { PHASE_CREATED, PHASE_INITIALIZING, PHASE_RUNNING, PHASE_CLOSED } source_phase;
typedef struct owned_file { qa_windows_file iface; qa_windows_file *inner; qa_app_sim_classic_guest_source *source; bool held; struct owned_file *next; } owned_file;
struct qa_app_sim_classic_guest_source {
  qa_app_sim_native_guest *guest;
  qa_compat_q2_classic_guest_host *host;
  qa_compat_q2_classic_engine_services services;
  qa_windows_capabilities capabilities; /* the caller's */
  qa_guest_address game;
  double budget;
  source_phase phase;
  owned_file *files; /* every wrapper, held or not */
  char revision[1024];
};
static size_t f_read(void *ctx, size_t offset, uint8_t *out, size_t length) { owned_file *f = ctx; return f->inner->read(f->inner->ctx, offset, out, length); }
static int64_t f_write(void *ctx, size_t offset, const uint8_t *bytes, size_t length) { owned_file *f = ctx; return f->inner->write(f->inner->ctx, offset, bytes, length); }
static int64_t f_size(void *ctx) { owned_file *f = ctx; return f->inner->size(f->inner->ctx); }
static void f_truncate(void *ctx, size_t length) { owned_file *f = ctx; f->inner->truncate(f->inner->ctx, length); }
static void f_flush(void *ctx) { owned_file *f = ctx; f->inner->flush(f->inner->ctx); }
static void f_close(void *ctx) { owned_file *f = ctx; if (f->held) { f->held = false; f->inner->close(f->inner->ctx); } }
static qa_windows_file *open_owned(void *ctx, const char *path, bool read, bool write, int creation) {
  qa_app_sim_classic_guest_source *s = ctx;
  qa_windows_file *file = s->capabilities.open_file != NULL ? s->capabilities.open_file(s->capabilities.ctx, path, read, write, creation) : NULL;
  if (file == NULL) return NULL;
  owned_file *owned = xcalloc(1, sizeof *owned);
  owned->inner = file; owned->source = s; owned->held = true;
  owned->iface.ctx = owned; owned->iface.read = f_read; owned->iface.write = f_write; owned->iface.size = f_size;
  owned->iface.truncate = f_truncate; owned->iface.flush = f_flush; owned->iface.close = f_close;
  owned->next = s->files; s->files = owned;
  return &owned->iface;
}
/* The caller's capabilities keep their own ctx; only openFile is wrapped. */
static bool c_now(void *ctx, double *out) { qa_app_sim_classic_guest_source *s = ctx; return s->capabilities.now_milliseconds(s->capabilities.ctx, out); }
static bool c_counter(void *ctx, int64_t *out) { qa_app_sim_classic_guest_source *s = ctx; return s->capabilities.performance_counter(s->capabilities.ctx, out); }
static void c_output(void *ctx, qa_windows_stream stream, const uint8_t *bytes, size_t length) { qa_app_sim_classic_guest_source *s = ctx; s->capabilities.standard_output(s->capabilities.ctx, stream, bytes, length); }
static size_t c_input(void *ctx, size_t length, uint8_t *out) { qa_app_sim_classic_guest_source *s = ctx; return s->capabilities.standard_input(s->capabilities.ctx, length, out); }
static size_t source_variadic(void *ctx, qa_callback_id id, const qa_guest_call_value *fixed, size_t count, qa_guest_value_layout *out, size_t capacity) {
  qa_app_sim_classic_guest_source *s = ctx;
  if (s->host == NULL) qa_range_error("Classic API imports have not been bound");
  return qa_compat_q2_classic_guest_host_variadic_layouts(s->host, id, fixed, count, out, capacity);
}
static void close_files(qa_app_sim_classic_guest_source *s) { for (owned_file *f = s->files; f != NULL; f = f->next) f_close(f); }
static const qa_native_call_abi classic_abi = {"windows-i386", "pe32", 4, "cdecl"};

qa_app_sim_classic_guest_source *qa_app_sim_classic_guest_source_create(const qa_app_sim_prepared_classic_guest *prepared,
                                                                        const qa_app_sim_classic_guest_source_options *options) {
  if (!classic_api(&prepared->execution)) qa_range_error("Classic guest source requires selected Windows i386 API 3");
  qa_app_sim_classic_guest_source *s = xcalloc(1, sizeof *s);
  s->capabilities = options->capabilities;
  qa_module_identity module = qa_app_sim_prepared_module_identity(&prepared->execution, prepared->has_primary, &prepared->primary, s->revision, sizeof s->revision);
  qa_app_sim_native_guest_options guest; memset(&guest, 0, sizeof guest);
  guest.module = module; guest.pointer_bytes = 4; guest.bytes = prepared->bytes; guest.length = prepared->length;
  /* sys_win.c runs _controlfp(_PC_24, _MCW_PC) before every frame, so retail game code computes at single precision. */
  guest.x87_control_word = 0x7f; guest.stack_top = 1048560;
  guest.stack_label = "classic game stack"; guest.return_label = "classic game return";
  guest.variadic_ctx = s; guest.variadic_layouts = source_variadic;
  guest.capabilities = options->capabilities; guest.capabilities.ctx = s;
  guest.capabilities.now_milliseconds = options->capabilities.now_milliseconds != NULL ? c_now : NULL;
  guest.capabilities.performance_counter = options->capabilities.performance_counter != NULL ? c_counter : NULL;
  guest.capabilities.standard_output = options->capabilities.standard_output != NULL ? c_output : NULL;
  guest.capabilities.standard_input = options->capabilities.standard_input != NULL ? c_input : NULL;
  guest.capabilities.open_file = open_owned;
  s->guest = qa_app_sim_native_guest_create(&guest);
  if (s->guest == NULL) qa_range_error("Out of memory");
  s->game = qa_app_sim_native_guest_entry(s->guest, "GetGameAPI");
  s->budget = options->has_instruction_budget ? options->instruction_budget : 5000000;
  memset(&s->services, 0, sizeof s->services);
  options->services(options->services_ctx, qa_app_sim_native_guest_memory(s->guest), &s->services);
  qa_compat_q2_classic_guest_host_options host; memset(&host, 0, sizeof host);
  host.runner = qa_app_sim_native_guest_runner(s->guest); host.provider = prepared->execution.owner.provider;
  host.services = s->services; host.instruction_budget = s->budget;
  host.import_boundary_ctx = options->import_boundary_ctx; host.import_boundary = options->import_boundary;
  s->host = qa_compat_q2_classic_guest_host_create(&host);
  if (s->host == NULL) qa_range_error("Out of memory");
  qa_app_sim_native_guest_initialize(s->guest, s->game, classic_abi, s->budget);
  (void)qa_compat_q2_classic_guest_host_get_game_api(s->host, s->game);
  return s;
}
void qa_app_sim_classic_guest_source_destroy(qa_app_sim_classic_guest_source *s) {
  if (s == NULL) return;
  qa_compat_q2_classic_guest_host_destroy(s->host);
  qa_app_sim_native_guest_destroy(s->guest);
  for (owned_file *f = s->files; f != NULL;) { owned_file *next = f->next; free(f); f = next; }
  free(s);
}
qa_compat_q2_classic_guest_host *qa_app_sim_classic_guest_source_host(qa_app_sim_classic_guest_source *s) { return s->host; }
qa_windows_guest_runtime *qa_app_sim_classic_guest_source_runtime(qa_app_sim_classic_guest_source *s) { return qa_app_sim_native_guest_runtime(s->guest); }
qa_compat_q2_memory *qa_app_sim_classic_guest_source_memory(qa_app_sim_classic_guest_source *s) { return qa_app_sim_native_guest_memory(s->guest); }
qa_app_sim_native_guest *qa_app_sim_classic_guest_source_guest(qa_app_sim_classic_guest_source *s) { return s->guest; }
qa_guest_address qa_app_sim_classic_guest_source_image_base(qa_app_sim_classic_guest_source *s) { return qa_app_sim_native_guest_image_base(s->guest); }
qa_guest_address qa_app_sim_classic_guest_source_entry(qa_app_sim_classic_guest_source *s, const char *name) { return qa_app_sim_native_guest_entry(s->guest, name); }
void qa_app_sim_classic_guest_source_init(qa_app_sim_classic_guest_source *s) {
  if (s->phase != PHASE_CREATED) qa_range_error("Classic guest Init requires a fresh source");
  s->phase = PHASE_INITIALIZING;
  /* TS closes the source when Init throws; the C throw aborts. */
  qa_compat_q2_classic_guest_host_init(s->host); s->phase = PHASE_RUNNING;
}
void qa_app_sim_classic_guest_source_init_loading(qa_app_sim_classic_guest_source *s, void (*next_frame)(void *), void *next_ctx) {
  if (s->phase != PHASE_CREATED) qa_range_error("Classic guest Init requires a fresh source");
  s->phase = PHASE_INITIALIZING;
  qa_compat_q2_classic_guest_host_init_loading(s->host, next_frame, next_ctx); s->phase = PHASE_RUNNING;
}
static void release_owned_actors(qa_app_sim_classic_guest_source *s) {
  qa_session_actor_registry *actors = s->services.engine->actors;
  const qa_compat_q2_classic_guest_host_options *options = qa_compat_q2_classic_guest_host_get_options(s->host);
  size_t count = 0; const qa_owned_actor *owned = qa_session_actor_registry_owned_by(actors, options->provider, &count);
  qa_owned_actor *copy = xcalloc(count, sizeof *copy); if (count != 0) memcpy(copy, owned, count * sizeof *copy);
  for (size_t i = 0; i < count; i++) qa_session_actor_registry_release(actors, copy[i]);
  free(copy);
}
void qa_app_sim_classic_guest_source_discard(qa_app_sim_classic_guest_source *s) {
  if (s->phase == PHASE_CLOSED) return;
  if (qa_guest_call_runner_depth(qa_app_sim_native_guest_call_runner(s->guest)) != 0) qa_range_error("Classic guest disposal requires a completed game call");
  s->phase = PHASE_CLOSED;
  if (s->services.dispose != NULL) s->services.dispose(s->services.ctx);
  release_owned_actors(s);
  close_files(s);
  qa_app_sim_native_guest_release_memory(s->guest);
}
void qa_app_sim_classic_guest_source_close(qa_app_sim_classic_guest_source *s) {
  if (s->phase == PHASE_CLOSED) return;
  if (qa_guest_call_runner_depth(qa_app_sim_native_guest_call_runner(s->guest)) != 0) qa_range_error("Classic guest shutdown requires a completed game call");
  bool initialized = s->phase != PHASE_CREATED;
  qa_guest_call_runner_recover(qa_app_sim_native_guest_call_runner(s->guest));
  if (initialized) qa_compat_q2_classic_guest_host_shutdown(s->host);
  qa_app_sim_native_guest_detach(s->guest, s->game, classic_abi, s->budget);
  qa_app_sim_classic_guest_source_discard(s);
}
size_t qa_app_sim_classic_guest_source_open_files(qa_app_sim_classic_guest_source *s) {
  size_t count = 0; for (owned_file *f = s->files; f != NULL; f = f->next) if (f->held) count++;
  return count;
}
