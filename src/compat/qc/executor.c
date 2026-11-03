/* Port of src/compat/qc/executor.ts. GPL-2.0-or-later. */
#include "qa/compat/qc/executor.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/core/binary/index.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/network/common/js_number.h"
static bool same_module(qa_module_identity a, qa_module_identity b) {
  return strcmp(a.id ? a.id : "", b.id ? b.id : "") == 0 && strcmp(a.artifact_path ? a.artifact_path : "", b.artifact_path ? b.artifact_path : "") == 0
      && strcmp(a.digest ? a.digest : "", b.digest ? b.digest : "") == 0 && strcmp(a.revision ? a.revision : "", b.revision ? b.revision : "") == 0;
}
bool qa_compat_qc_capture_checkpoint(qa_compat_qc_machine *m, qa_module_identity module, qa_compat_qc_executor_host host_adapter, qa_guest_checkpoint *out, qa_compat_qc_machine_snapshot *snap, uint8_t **out_host_bytes) {
  if (!qa_compat_qc_machine_get_snapshot(m, snap)) return false;
  qa_compat_qc_host_saved_state host = host_adapter.checkpoint(host_adapter.ctx);
  if (!same_module(host.state.module, module)) qa_compat_qc_machine_fail(m, "host checkpoint belongs to another module");
  size_t format_len = strlen(host.state.format);
  qa_binary_writer w;
  if (!qa_binary_writer_init(&w, (int64_t)(24 + snap->profiling_count * 4 + format_len + host.state.bytes.length))) return false;
  qa_binary_writer_u32(&w, (int64_t)0x31484351); qa_binary_writer_u32(&w, snap->trace_enabled ? 1 : 0);
  qa_binary_writer_u32(&w, (int64_t)snap->profiling_count);
  for (size_t i = 0; i < snap->profiling_count; i++) qa_binary_writer_u32(&w, (int64_t)(uint32_t)qa_int32(snap->profiling[i])); /* count >>> 0 */
  qa_binary_writer_u32(&w, (int64_t)format_len); qa_binary_writer_bytes(&w, (const uint8_t *)host.state.format, format_len);
  qa_binary_writer_u32(&w, (int64_t)host.state.bytes.length); qa_binary_writer_bytes(&w, host.state.bytes.bytes, host.state.bytes.length);
  qa_binary_writer_u32(&w, m->entities->layout.variables_offset_bytes);
  size_t host_len = 0; *out_host_bytes = qa_binary_writer_finish(&w, &host_len); qa_binary_writer_destroy(&w);
  if (!*out_host_bytes) return false;
  memset(out, 0, sizeof *out);
  out->kind = QA_GUEST_CHECKPOINT_QUAKEC; out->module = module; out->quakec_api = m->program->api;
  out->random = host.random; out->random_count = host.random_count;
  out->callbacks = host.callbacks; out->callback_count = host.callback_count;
  out->globals.bytes = snap->globals; out->globals.length = snap->globals_length;
  out->entities.bytes = snap->entities; out->entities.length = snap->entities_length;
  out->strings.bytes = snap->strings; out->strings.length = snap->strings_length;
  out->entity_stride_bytes = (double)m->entities->layout.stride_bytes; out->entity_count = (double)snap->entity_count;
  out->statement = (double)snap->statement; out->function_index = (double)snap->function_index; out->argument_count = (double)snap->argument_count;
  out->host_state.module = module; out->host_state.format = "quakec:host-v1";
  out->host_state.bytes.bytes = *out_host_bytes; out->host_state.bytes.length = host_len;
  return true;
}
typedef struct { qa_binary_reader reader; double *profiling; char *format; uint8_t *state_bytes; } RestoreScope;
static void restore_scope_free(void *arg) {
  RestoreScope *r = arg;
  free(r->format); free(r->state_bytes); free(r->profiling); qa_binary_reader_destroy(&r->reader);
}
/* TS preflightQcCheckpoint (apply false) and restoreQcCheckpoint (apply true): the decode and every validation run
 * before the machine or host changes, so a corrupt save fails before any state is released. */
static bool restore_impl(qa_compat_qc_machine *m, qa_module_identity module, const qa_compat_qc_executor_host *host_adapter, const qa_guest_checkpoint *cp, bool apply) {
  if (cp->kind != QA_GUEST_CHECKPOINT_QUAKEC || !same_module(cp->module, module) || cp->quakec_api.kind != m->program->api.kind
      || cp->quakec_api.program_version != m->program->api.program_version || cp->quakec_api.system_crc != m->program->api.system_crc
      || !same_module(cp->host_state.module, module)) qa_compat_qc_machine_fail(m, "incompatible QuakeC checkpoint");
  if (cp->call_stack_count != 0 || cp->locals.length != 0 || cp->function_index != 0) qa_compat_qc_machine_fail(m, "checkpoint is not at an idle callback boundary");
  if (cp->entity_stride_bytes != (double)m->entities->layout.stride_bytes || strcmp(cp->host_state.format, "quakec:host-v1") != 0) qa_compat_qc_machine_fail(m, "checkpoint layout mismatch");
  RestoreScope r; memset(&r, 0, sizeof r);
  if (!qa_binary_reader_init(&r.reader, (uint8_t *)cp->host_state.bytes.bytes, cp->host_state.bytes.length, "QuakeC host checkpoint")) return false;
  qa_cleanup scope; qa_cleanup_push(&scope, restore_scope_free, &r);
  if (qa_binary_reader_u32(&r.reader) != 0x31484351u) qa_compat_qc_machine_fail(m, "unknown QuakeC host checkpoint");
  bool trace = qa_binary_reader_u32(&r.reader) != 0; uint32_t count = qa_binary_reader_u32(&r.reader);
  /* A count past the data raises from the read below, as in TS, before any overflow of this array. */
  size_t available = qa_binary_reader_remaining(&r.reader) / 4 + 1;
  r.profiling = calloc(count < available ? count + 1 : available, sizeof(double));
  bool ok = r.profiling != NULL;
  if (ok) {
    for (uint32_t i = 0; i < count; i++) r.profiling[i] = (double)qa_binary_reader_u32(&r.reader);
    uint32_t format_length = qa_binary_reader_u32(&r.reader);
    r.format = qa_binary_reader_fixed_string(&r.reader, format_length);
    uint32_t state_len = qa_binary_reader_u32(&r.reader);
    r.state_bytes = qa_binary_reader_bytes(&r.reader, state_len);
    uint32_t var_off = qa_binary_reader_u32(&r.reader);
    ok = r.format && (r.state_bytes || !state_len);
    if (ok) {
      if (var_off != (uint32_t)m->entities->layout.variables_offset_bytes || qa_binary_reader_remaining(&r.reader) != 0) qa_compat_qc_machine_fail(m, "checkpoint entity variable offset mismatch");
      const char *sep = strchr(r.format, ':');
      if (!sep || sep == r.format || sep[1] == 0) qa_compat_qc_machine_fail(m, "invalid host state format");
      qa_compat_qc_machine_snapshot snap; memset(&snap, 0, sizeof snap);
      snap.globals = (uint8_t *)cp->globals.bytes; snap.globals_length = cp->globals.length;
      snap.entities = (uint8_t *)cp->entities.bytes; snap.entities_length = cp->entities.length;
      snap.entity_count = (int64_t)cp->entity_count;
      snap.strings = (uint8_t *)cp->strings.bytes; snap.strings_length = cp->strings.length;
      snap.statement = (int64_t)cp->statement; snap.function_index = (int64_t)cp->function_index; snap.argument_count = (int64_t)cp->argument_count;
      snap.profiling = r.profiling; snap.profiling_count = count; snap.trace_enabled = trace;
      qa_compat_qc_machine_validate_snapshot(m, &snap);
      if (apply) ok = qa_compat_qc_machine_restore(m, &snap);
      if (ok && apply) {
        qa_compat_qc_host_saved_state saved; memset(&saved, 0, sizeof saved);
        saved.random = cp->random; saved.random_count = cp->random_count;
        saved.callbacks = cp->callbacks; saved.callback_count = cp->callback_count;
        saved.state.module = module; saved.state.format = r.format;
        saved.state.bytes.bytes = r.state_bytes; saved.state.bytes.length = state_len;
        host_adapter->restore(host_adapter->ctx, saved);
      }
    }
  }
  qa_cleanup_pop(&scope);
  restore_scope_free(&r);
  return ok;
}
void qa_compat_qc_preflight_checkpoint(qa_compat_qc_machine *m, qa_module_identity module, const qa_guest_checkpoint *cp) {
  if (!restore_impl(m, module, NULL, cp, false)) qa_compat_qc_machine_fail(m, "checkpoint allocation failed");
}
bool qa_compat_qc_restore_checkpoint(qa_compat_qc_machine *m, qa_module_identity module, qa_compat_qc_executor_host host_adapter, const qa_guest_checkpoint *cp) {
  return restore_impl(m, module, &host_adapter, cp, true);
}
struct qa_compat_qc_raw_view_context { qa_compat_qc_raw_table_state *state; int64_t slot; };
static qa_actor_id_result view_actor(void *ctx) {
  struct qa_compat_qc_raw_view_context *view = ctx;
  qa_actor_id_result r; memset(&r, 0, sizeof r);
  r.has_value = view->state->current_actor(view->state->ctx, view->slot, &r.value); return r;
}
static qa_raw_entity_view at_slot(qa_compat_qc_raw_table_state *s, int64_t slot) {
  int64_t offset = qa_compat_qc_entity_memory_reference(s->machine->entities, slot);
  qa_raw_entity_view view; memset(&view, 0, sizeof view);
  view.module = s->module; view.slot = (double)slot;
  view.address = (qa_guest_address){&s->address_space_token, offset};
  view.stride_bytes = (double)s->machine->entities->layout.stride_bytes;
  view.public_layout = s->layout;
  view.bytes.bytes = s->machine->entities->bytes + offset;
  view.bytes.length = (size_t)s->machine->entities->layout.stride_bytes;
  /* Each view resolves its own slot on every access, however many views are live. */
  view.ctx = &s->views[slot]; view.current_actor = view_actor;
  return view;
}
static qa_raw_entity_view t_at_slot(void *ctx, double slot) {
  qa_compat_qc_raw_table_state *s = ctx;
  if (!(fabs(slot) < 9007199254740992.0) || slot != trunc(slot)) { char text[40]; qa_net_js_number(slot, text, sizeof text); qa_compat_qc_program_error("progs.dat", "invalid entity slot %s", text); }
  return at_slot(s, (int64_t)slot);
}
static qa_raw_entity_view t_from_pointer(void *ctx, qa_guest_address address) {
  qa_compat_qc_raw_table_state *s = ctx;
  if (address.address_space != &s->address_space_token || address.byte_offset < 0 || address.byte_offset > 0x7fffffff) qa_compat_qc_machine_fail(s->machine, "entity address belongs to another address space");
  return at_slot(s, qa_compat_qc_entity_memory_slot(s->machine->entities, address.byte_offset));
}
static double t_live_count(void *ctx) { return (double)((qa_compat_qc_raw_table_state *)ctx)->machine->entities->count; }
qa_raw_entity_table qa_compat_qc_create_raw_entity_table(qa_compat_qc_machine *m, qa_module_identity module, qa_guest_layout layout, void *ctx, bool (*current_actor)(void *, int64_t, qa_actor_id *), qa_compat_qc_raw_table_state *st) {
  memset(st, 0, sizeof *st);
  st->machine = m; st->module = module; st->layout = layout; st->ctx = ctx; st->current_actor = current_actor;
  if (layout.byte_length > (double)m->entities->layout.stride_bytes) qa_compat_qc_machine_fail(m, "public layout exceeds QuakeC entity record");
  size_t capacity = (size_t)m->entities->capacity;
  st->views = calloc(capacity, sizeof *st->views);
  if (!st->views) qa_range_error("raw entity table allocation failed");
  for (size_t i = 0; i < capacity; i++) st->views[i] = (struct qa_compat_qc_raw_view_context){st, (int64_t)i};
  qa_raw_entity_table t; memset(&t, 0, sizeof t);
  t.ctx = st; t.module = module;
  t.base = (qa_guest_address){&st->address_space_token, 0};
  t.stride_bytes = (double)m->entities->layout.stride_bytes;
  t.count = (double)m->entities->count; t.capacity = (double)m->entities->capacity;
  t.layout = layout; t.at_slot = t_at_slot; t.from_pointer = t_from_pointer; t.live_count = t_live_count;
  return t;
}
void qa_compat_qc_raw_table_state_destroy(qa_compat_qc_raw_table_state *st) { free(st->views); memset(st, 0, sizeof *st); }
