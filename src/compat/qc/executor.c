/* Port of src/compat/qc/executor.ts. GPL-2.0-or-later. */
#include "qa/compat/qc/executor.h"
#include <stdlib.h>
#include <string.h>
#include "qa/core/binary/index.h"
static bool same_module(qa_module_identity a, qa_module_identity b) {
  return strcmp(a.id ? a.id : "", b.id ? b.id : "") == 0 && strcmp(a.artifact_path ? a.artifact_path : "", b.artifact_path ? b.artifact_path : "") == 0
      && strcmp(a.digest ? a.digest : "", b.digest ? b.digest : "") == 0 && strcmp(a.revision ? a.revision : "", b.revision ? b.revision : "") == 0;
}
void qa_compat_qc_quakec_executor_init(qa_compat_qc_quakec_executor *e, qa_execution_profile profile, qa_compat_qc_machine *m, qa_compat_qc_executor_host host) {
  memset(e, 0, sizeof *e); e->profile = profile; e->machine = m; e->host = host;
  if (profile.host.api.kind != m->program->api.kind || strcmp(profile.numeric.id ? profile.numeric.id : "", m->numeric->profile.id ? m->numeric->profile.id : "") != 0
      || strcmp(profile.module.digest ? profile.module.digest : "", m->program->digest) != 0)
    qa_compat_qc_machine_fail(m, "execution profile disagrees with loaded program");
}
void qa_compat_qc_quakec_executor_destroy(qa_compat_qc_quakec_executor *e) { free(e->host_state_bytes); qa_compat_qc_machine_snapshot_destroy(&e->snapshot); memset(e, 0, sizeof *e); }
qa_guest_call_result qa_compat_qc_quakec_executor_invoke(qa_compat_qc_quakec_executor *e, qa_guest_call_context context, const qa_guest_call_value *args, size_t argc) {
  qa_compat_qc_machine *m = e->machine;
  qa_guest_callback_reference callback = context.callback;
  if (callback.kind != QA_GUEST_CALLBACK_QUAKEC || !same_module(context.module, e->profile.module) || !same_module(callback.module, e->profile.module))
    qa_compat_qc_machine_fail(m, "callback belongs to another guest module");
  if (argc > 8) qa_compat_qc_machine_fail(m, "too many QuakeC arguments");
  if (context.self_view) {
    if (!same_module(context.self_view->module, e->profile.module)) qa_compat_qc_machine_fail(m, "self belongs to another guest module");
    qa_compat_qc_words_set_int(&m->globals, qa_compat_qc_machine_global_offset(m, "self"), (double)qa_compat_qc_entity_memory_reference(m->entities, (int64_t)context.self_view->slot));
  }
  if (context.other) {
    if (!same_module(context.other->module, e->profile.module)) qa_compat_qc_machine_fail(m, "other belongs to another guest module");
    qa_compat_qc_words_set_int(&m->globals, qa_compat_qc_machine_global_offset(m, "other"), (double)qa_compat_qc_entity_memory_reference(m->entities, (int64_t)context.other->slot));
  }
  for (size_t i = 0; i < argc; i++) {
    qa_guest_call_value v = args[i]; int64_t offset = 4 + (int64_t)i * 3;
    switch (v.kind) {
      case QA_GUEST_VALUE_INT32: case QA_GUEST_VALUE_UINT32: qa_compat_qc_words_set_int(&m->globals, offset, v.number_value); break;
      case QA_GUEST_VALUE_FLOAT32: qa_compat_qc_words_set_float(&m->globals, offset, v.number_value); break;
      case QA_GUEST_VALUE_AGGREGATE:
        if (v.bytes.length != 12 || v.layout.byte_length != 12) qa_compat_qc_machine_fail(m, "QuakeC aggregate argument must be three words");
        memcpy(m->globals.bytes + offset * 4, v.bytes.bytes, 12); break;
      case QA_GUEST_VALUE_POINTER: qa_compat_qc_machine_fail(m, "QuakeC does not accept pointer arguments; encode a source word or vector");
      case QA_GUEST_VALUE_INT64: qa_compat_qc_machine_fail(m, "QuakeC does not accept int64 arguments; encode a source word or vector");
      case QA_GUEST_VALUE_UINT64: qa_compat_qc_machine_fail(m, "QuakeC does not accept uint64 arguments; encode a source word or vector");
      case QA_GUEST_VALUE_FLOAT64: qa_compat_qc_machine_fail(m, "QuakeC does not accept float64 arguments; encode a source word or vector");
      default: qa_compat_qc_machine_fail(m, "QuakeC does not accept void arguments; encode a source word or vector");
    }
  }
  qa_compat_qc_machine_execute(m, (int64_t)callback.function_index, (int64_t)argc);
  memcpy(e->return_bytes, m->globals.bytes + 4, 12);
  qa_guest_call_result r; memset(&r, 0, sizeof r);
  r.kind = QA_GUEST_VALUE_AGGREGATE;
  r.layout = (qa_guest_layout){"quakec:return-words", 12, 4, 4, NULL, 0};
  r.bytes.bytes = e->return_bytes; r.bytes.length = 12;
  return r;
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
  for (size_t i = 0; i < snap->profiling_count; i++) qa_binary_writer_u32(&w, (int64_t)(uint32_t)snap->profiling[i]);
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
bool qa_compat_qc_restore_checkpoint(qa_compat_qc_machine *m, qa_module_identity module, qa_compat_qc_executor_host host_adapter, const qa_guest_checkpoint *cp) {
  if (cp->kind != QA_GUEST_CHECKPOINT_QUAKEC || !same_module(cp->module, module) || cp->quakec_api.kind != m->program->api.kind
      || cp->quakec_api.program_version != m->program->api.program_version || cp->quakec_api.system_crc != m->program->api.system_crc
      || !same_module(cp->host_state.module, module)) qa_compat_qc_machine_fail(m, "incompatible QuakeC checkpoint");
  if (cp->call_stack_count != 0 || cp->locals.length != 0 || cp->function_index != 0) qa_compat_qc_machine_fail(m, "checkpoint is not at an idle callback boundary");
  if (cp->entity_stride_bytes != (double)m->entities->layout.stride_bytes || strcmp(cp->host_state.format, "quakec:host-v1") != 0) qa_compat_qc_machine_fail(m, "checkpoint layout mismatch");
  qa_binary_reader r; if (!qa_binary_reader_init(&r, (uint8_t *)cp->host_state.bytes.bytes, cp->host_state.bytes.length, "QuakeC host checkpoint")) return false;
  if (qa_binary_reader_u32(&r) != 0x31484351u) qa_compat_qc_machine_fail(m, "unknown QuakeC host checkpoint");
  bool trace = qa_binary_reader_u32(&r) != 0; uint32_t count = qa_binary_reader_u32(&r);
  double *profiling = calloc(count ? count : 1, sizeof(double));
  if (!profiling) { qa_binary_reader_destroy(&r); return false; }
  for (uint32_t i = 0; i < count; i++) profiling[i] = (double)qa_binary_reader_u32(&r);
  char *format = qa_binary_reader_fixed_string(&r, qa_binary_reader_u32(&r));
  uint32_t state_len = qa_binary_reader_u32(&r);
  uint8_t *state_bytes = qa_binary_reader_bytes(&r, state_len);
  uint32_t var_off = qa_binary_reader_u32(&r);
  if (!format || (!state_bytes && state_len)) { free(format); free(state_bytes); free(profiling); qa_binary_reader_destroy(&r); return false; }
  if (var_off != (uint32_t)m->entities->layout.variables_offset_bytes || qa_binary_reader_remaining(&r) != 0) qa_compat_qc_machine_fail(m, "checkpoint entity variable offset mismatch");
  qa_binary_reader_destroy(&r);
  const char *sep = strchr(format, ':');
  if (!sep || sep == format || sep[1] == 0) qa_compat_qc_machine_fail(m, "invalid host state format");
  qa_compat_qc_machine_snapshot snap; memset(&snap, 0, sizeof snap);
  snap.globals = (uint8_t *)cp->globals.bytes; snap.globals_length = cp->globals.length;
  snap.entities = (uint8_t *)cp->entities.bytes; snap.entities_length = cp->entities.length;
  snap.entity_count = (int64_t)cp->entity_count;
  snap.strings = (uint8_t *)cp->strings.bytes; snap.strings_length = cp->strings.length;
  snap.statement = (int64_t)cp->statement; snap.function_index = (int64_t)cp->function_index; snap.argument_count = (int64_t)cp->argument_count;
  snap.profiling = profiling; snap.profiling_count = count; snap.trace_enabled = trace;
  bool ok = qa_compat_qc_machine_restore(m, &snap);
  if (ok) {
    qa_compat_qc_host_saved_state saved; memset(&saved, 0, sizeof saved);
    saved.random = cp->random; saved.random_count = cp->random_count;
    saved.callbacks = cp->callbacks; saved.callback_count = cp->callback_count;
    saved.state.module = module; saved.state.format = format;
    saved.state.bytes.bytes = state_bytes; saved.state.bytes.length = state_len;
    host_adapter.restore(host_adapter.ctx, saved);
  }
  free(format); free(state_bytes); free(profiling);
  return ok;
}
const qa_guest_checkpoint *qa_compat_qc_quakec_executor_checkpoint(qa_compat_qc_quakec_executor *e) {
  free(e->host_state_bytes); e->host_state_bytes = NULL;
  qa_compat_qc_machine_snapshot_destroy(&e->snapshot);
  if (!qa_compat_qc_capture_checkpoint(e->machine, e->profile.module, e->host, &e->checkpoint_storage, &e->snapshot, &e->host_state_bytes)) qa_compat_qc_machine_fail(e->machine, "checkpoint allocation failed");
  return &e->checkpoint_storage;
}
void qa_compat_qc_quakec_executor_restore(qa_compat_qc_quakec_executor *e, const qa_guest_checkpoint *cp) {
  if (!qa_compat_qc_restore_checkpoint(e->machine, e->profile.module, e->host, cp)) qa_compat_qc_machine_fail(e->machine, "checkpoint allocation failed");
}
static qa_actor_id_result view_actor(void *ctx) {
  qa_compat_qc_raw_table_state *s = ((void **)ctx)[0]; int64_t slot = *(int64_t *)((void **)ctx)[1];
  qa_actor_id_result r; memset(&r, 0, sizeof r);
  r.has_value = s->current_actor(s->ctx, slot, &r.value); return r;
}
static qa_raw_entity_view at_slot(qa_compat_qc_raw_table_state *s, int64_t slot) {
  int64_t offset = qa_compat_qc_entity_memory_reference(s->machine->entities, slot);
  /* view ctx packs [state, slot-storage]; slot storage lives in view_storage via a static per-state cell */
  s->view_storage.module = s->module; s->view_storage.slot = (double)slot;
  s->view_storage.address = (qa_guest_address){&s->address_space_token, offset};
  s->view_storage.stride_bytes = (double)s->machine->entities->layout.stride_bytes;
  s->view_storage.public_layout = s->layout;
  s->view_storage.bytes.bytes = s->machine->entities->bytes + offset;
  s->view_storage.bytes.length = (size_t)s->machine->entities->layout.stride_bytes;
  static int64_t slot_cell; slot_cell = slot;
  static void *pair[2]; pair[0] = s; pair[1] = &slot_cell;
  s->view_storage.ctx = pair; s->view_storage.current_actor = view_actor;
  return s->view_storage;
}
static qa_raw_entity_view t_at_slot(void *ctx, double slot) { return at_slot(ctx, (int64_t)slot); }
static qa_raw_entity_view t_from_pointer(void *ctx, qa_guest_address address) {
  qa_compat_qc_raw_table_state *s = ctx;
  if (address.address_space != &s->address_space_token || address.byte_offset < 0 || address.byte_offset > 0x7fffffff) qa_compat_qc_machine_fail(s->machine, "entity address belongs to another address space");
  return at_slot(s, qa_compat_qc_entity_memory_slot(s->machine->entities, address.byte_offset));
}
qa_raw_entity_table qa_compat_qc_create_raw_entity_table(qa_compat_qc_machine *m, qa_module_identity module, qa_guest_layout layout, void *ctx, bool (*current_actor)(void *, int64_t, qa_actor_id *), qa_compat_qc_raw_table_state *st) {
  st->machine = m; st->module = module; st->layout = layout; st->ctx = ctx; st->current_actor = current_actor;
  if (layout.byte_length > (double)m->entities->layout.stride_bytes) qa_compat_qc_machine_fail(m, "public layout exceeds QuakeC entity record");
  qa_raw_entity_table t; memset(&t, 0, sizeof t);
  t.ctx = st; t.module = module;
  t.base = (qa_guest_address){&st->address_space_token, 0};
  t.stride_bytes = (double)m->entities->layout.stride_bytes;
  t.count = (double)m->entities->count; t.capacity = (double)m->entities->capacity;
  t.layout = layout; t.at_slot = t_at_slot; t.from_pointer = t_from_pointer;
  return t;
}
