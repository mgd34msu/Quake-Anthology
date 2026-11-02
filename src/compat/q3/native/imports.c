/* Port of src/compat/q3/native/imports.ts. GPL-2.0-or-later. */
#include "qa/compat/q3/native/imports.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/simulation/q3/types.h"
#include "qa/core/cvars/index.h"
#include "qa/foundation/support.h"
#include "qa/guest/runtime/common/memory.h"

struct qa_compat_q3_native_import_slot {
  char *id, *name;
  qa_abi_value_layout *layouts, result_layout;
  qa_guest_host_callback callback;
  void *service_ctx;
  qa_compat_q3_native_service service;
  bool implemented;
  int64_t reached;
  qa_compat_q3_quakelive_game_imports *imports;
  int64_t slot;
};
typedef qa_compat_q3_quakelive_game_imports I;
typedef struct qa_compat_q3_native_import_slot Slot;

noreturn void qa_compat_q3_native_service_unavailable(int64_t slot, const char *service) {
  qa_range_error("Quake Live game import %lld (%s) is unavailable in the supplied engine host", (long long)slot, service);
}
noreturn void qa_compat_q3_native_game_error(const char *message) { qa_range_error("%s", message); }

typedef struct { I *imports; int64_t slot; char *name; } UnboundCtx;
static qa_call_result unbound_invoke(void *ctx, const qa_guest_core_call_context *context, const qa_call_value *args, size_t count);
static char *dup_text(const char *s) { size_t n = strlen(s) + 1; char *r = malloc(n); if (r) memcpy(r, s, n); return r; }
static qa_call_result slot_invoke(void *ctx, const qa_guest_core_call_context *context, const qa_call_value *args, size_t count) {
  Slot *slot = ctx;
  slot->reached++;
  return slot->service(slot->service_ctx, context, args, count);
}
qa_guest_core_address qa_compat_q3_quakelive_game_imports_bind(I *imports, int64_t slot_index, const char *name,
  const qa_abi_storage *parameters, size_t parameter_count, bool has_result, qa_abi_storage result,
  void *ctx, qa_compat_q3_native_service invoke, bool implemented) {
  if (slot_index < 0 || slot_index >= 206) qa_range_error("Quake Live import slot outside table");
  Slot *slot = &imports->slots[slot_index];
  /* The callback table borrows the previous id/layouts; retire them
   * (and our unbound ctx) to the graveyard instead of freeing. */
  {
    void *graveyard[5] = {slot->id, slot->name, slot->layouts, NULL, NULL};
    if (slot->service == unbound_invoke && slot->service_ctx) {
      graveyard[3] = ((UnboundCtx *)slot->service_ctx)->name;
      graveyard[4] = slot->service_ctx;
    }
    for (int i = 0; i < 5; i++) if (graveyard[i]) {
      if (imports->retired_count == imports->retired_capacity) {
        size_t c = imports->retired_capacity ? imports->retired_capacity * 2 : 16;
        imports->retired = realloc(imports->retired, c * sizeof *imports->retired);
        if (!imports->retired) qa_range_error("Quake Live import allocation failed");
        imports->retired_capacity = c;
      }
      imports->retired[imports->retired_count++] = graveyard[i];
    }
  }
  memset(slot, 0, sizeof *slot);
  slot->imports = imports; slot->slot = slot_index;
  slot->service_ctx = ctx; slot->service = invoke; slot->implemented = implemented;
  slot->name = dup_text(name);
  char id[256];
  snprintf(id, sizeof id, "native-ql:%llu:%lld:%s", (unsigned long long)imports->address.byte_offset, (long long)slot_index, name);
  slot->id = dup_text(id);
  slot->layouts = calloc(parameter_count ? parameter_count : 1, sizeof *slot->layouts);
  if (!slot->name || !slot->id || !slot->layouts) qa_range_error("Quake Live import allocation failed");
  qa_guest_call_signature signature = qa_compat_q3_native_signature(imports->abi, parameters, parameter_count,
    has_result, result, false, slot->layouts, &slot->result_layout);
  slot->callback = (qa_guest_host_callback){slot->id, signature, slot, slot_invoke};
  qa_guest_core_address target = qa_guest_callback_table_bind(imports->callbacks, &slot->callback);
  qa_sparse_write_pointer(imports->memory,
    qa_sparse_offset(imports->memory, imports->address, slot_index * imports->abi.pointer_bytes), true, target);
  return target;
}
size_t qa_compat_q3_quakelive_game_imports_coverage(const I *imports, qa_compat_q3_native_service_coverage *out, size_t capacity) {
  for (size_t i = 0; i < 206 && i < capacity; i++) {
    const Slot *slot = &imports->slots[i];
    if (out) out[i] = (qa_compat_q3_native_service_coverage){(int64_t)i, slot->name, slot->implemented, slot->reached};
  }
  return 206;
}
void qa_compat_q3_quakelive_game_imports_write_string(I *imports, qa_guest_core_address address, const char *value, int64_t capacity) {
  if (capacity < 1) qa_range_error("Native string output requires positive source capacity");
  qa_sparse_check(imports->memory, address, (size_t)capacity, QA_ACCESS_WRITE);
  uint8_t *bytes = calloc((size_t)capacity, 1);
  if (!bytes) qa_range_error("Quake Live import allocation failed");
  size_t length = strlen(value);
  size_t end = length < (size_t)(capacity - 1) ? length : (size_t)(capacity - 1);
  for (size_t i = 0; i < end; i++) bytes[i] = (uint8_t)value[i];
  qa_sparse_write(imports->memory, address, bytes, (size_t)capacity);
  free(bytes);
}
static void update_cvar(I *imports, qa_guest_core_address address) {
  qa_sparse_guest_memory *m = imports->memory;
  double modification_count = 0, numeric_value = 0, integer_value = 0;
  char *value = NULL;
  if (!imports->host.cvar_read_vm(imports->host.ctx, qa_sparse_read_i32(m, address),
        &modification_count, &value, &numeric_value, &integer_value)) return;
  if (qa_sparse_read_i32(m, qa_sparse_offset(m, address, 4)) == (int32_t)modification_count) { free(value); return; }
  qa_sparse_check(m, address, 272, QA_ACCESS_WRITE);
  qa_sparse_write_i32(m, qa_sparse_offset(m, address, 4), (int32_t)modification_count);
  if (strlen(value) >= 256) qa_range_error("Cvar_Update: source value exceeds MAX_CVAR_VALUE_STRING");
  qa_compat_q3_quakelive_game_imports_write_string(imports, qa_sparse_offset(m, address, 16), value, 256);
  qa_sparse_write_f32(m, qa_sparse_offset(m, address, 8), (double)(float)numeric_value);
  qa_sparse_write_i32(m, qa_sparse_offset(m, address, 12), (int32_t)integer_value);
  free(value);
}
static char *arg_text(I *imports, const qa_call_value *args, size_t count, size_t index) {
  qa_guest_core_address address = qa_guest_required_pointer(args, count, index);
  int64_t length = qa_sparse_find_zero(imports->memory, address, 1048576);
  if (length < 0) qa_range_error("Guest string exceeds checked maximum");
  char *text = malloc((size_t)length + 1);
  if (!text) qa_range_error("Quake Live import allocation failed");
  qa_sparse_copy(imports->memory, address, (uint8_t *)text, (size_t)length);
  text[length] = 0;
  return text;
}
static qa_call_result unbound_invoke(void *ctx, const qa_guest_core_call_context *context, const qa_call_value *args, size_t count) {
  (void)context; (void)args; (void)count;
  UnboundCtx *u = ctx;
  qa_compat_q3_native_service_unavailable(u->slot, u->name);
}
#define HOST(slot_name) ((I *)ctx)
static qa_call_result svc_send_console_command(void *ctx, const qa_guest_core_call_context *c, const qa_call_value *args, size_t n) {
  (void)c; I *imports = HOST(0); char *t = arg_text(imports, args, n, 0);
  imports->host.append_console_command(imports->host.ctx, t); free(t); return qa_call_void();
}
static qa_call_result svc_printf(void *ctx, const qa_guest_core_call_context *c, const qa_call_value *args, size_t n) {
  (void)c; I *imports = HOST(0); char *t = arg_text(imports, args, n, 0);
  imports->host.print(imports->host.ctx, t); free(t); return qa_call_void();
}
static qa_call_result svc_cvar_variable_value(void *ctx, const qa_guest_core_call_context *c, const qa_call_value *args, size_t n) {
  (void)c; I *imports = HOST(0); char *t = arg_text(imports, args, n, 0);
  double value = (double)(float)imports->host.cvar_variable_value(imports->host.ctx, t); free(t);
  return qa_call_float32(value);
}
static qa_call_result svc_cvar_update(void *ctx, const qa_guest_core_call_context *c, const qa_call_value *args, size_t n) {
  (void)c; I *imports = HOST(0); update_cvar(imports, qa_guest_required_pointer(args, n, 0)); return qa_call_void();
}
static qa_call_result svc_cvar_variable_string_buffer(void *ctx, const qa_guest_core_call_context *c, const qa_call_value *args, size_t n) {
  (void)c; I *imports = HOST(0); char *t = arg_text(imports, args, n, 0);
  char *value = imports->host.cvar_variable_string(imports->host.ctx, t); free(t);
  qa_compat_q3_quakelive_game_imports_write_string(imports, qa_guest_required_pointer(args, n, 1),
    value ? value : "", (int64_t)qa_guest_integer(args, n, 2));
  free(value); return qa_call_void();
}
static qa_call_result svc_cvar_set(void *ctx, const qa_guest_core_call_context *c, const qa_call_value *args, size_t n) {
  (void)c; I *imports = HOST(0);
  char *name = arg_text(imports, args, n, 0), *value = arg_text(imports, args, n, 1);
  imports->host.cvar_set(imports->host.ctx, name, value, true);
  free(name); free(value); return qa_call_void();
}
static qa_call_result svc_cvar_register(void *ctx, const qa_guest_core_call_context *c, const qa_call_value *args, size_t n) {
  (void)c; I *imports = HOST(0);
  qa_guest_core_address address; bool has = qa_guest_pointer_argument(args, n, 0, &address);
  char *name = arg_text(imports, args, n, 1), *fallback = arg_text(imports, args, n, 2);
  int64_t handle = imports->host.cvar_bind_vm(imports->host.ctx, name, fallback, (double)qa_guest_integer(args, n, 3));
  free(name); free(fallback);
  if (has) {
    qa_sparse_write_i32(imports->memory, address, (int32_t)handle);
    qa_sparse_write_i32(imports->memory, qa_sparse_offset(imports->memory, address, 4), -1);
    update_cvar(imports, address);
  }
  return qa_call_void();
}
static qa_call_result svc_argv(void *ctx, const qa_guest_core_call_context *c, const qa_call_value *args, size_t n) {
  (void)c; I *imports = HOST(0);
  size_t count = 0; const char *const *argv = imports->host.command_arguments(imports->host.ctx, &count);
  int64_t index = qa_guest_integer(args, n, 0);
  const char *value = index >= 0 && (size_t)index < count ? argv[index] : "";
  qa_compat_q3_quakelive_game_imports_write_string(imports, qa_guest_required_pointer(args, n, 1), value, (int64_t)qa_guest_integer(args, n, 2));
  return qa_call_void();
}
static qa_call_result svc_argc(void *ctx, const qa_guest_core_call_context *c, const qa_call_value *args, size_t n) {
  (void)c; (void)args; (void)n; I *imports = HOST(0);
  size_t count = 0; (void)imports->host.command_arguments(imports->host.ctx, &count);
  return qa_call_int32((int32_t)count);
}
static qa_call_result svc_drop_client(void *ctx, const qa_guest_core_call_context *c, const qa_call_value *args, size_t n) {
  (void)c; I *imports = HOST(0); char *t = arg_text(imports, args, n, 1);
  imports->host.drop_client(imports->host.ctx, (double)qa_guest_integer(args, n, 0), t); free(t); return qa_call_void();
}
static qa_call_result svc_send_server_command(void *ctx, const qa_guest_core_call_context *c, const qa_call_value *args, size_t n) {
  (void)c; I *imports = HOST(0); char *t = arg_text(imports, args, n, 1);
  imports->host.send_server_command(imports->host.ctx, (double)qa_guest_integer(args, n, 0), t); free(t); return qa_call_void();
}
static qa_call_result svc_set_configstring(void *ctx, const qa_guest_core_call_context *c, const qa_call_value *args, size_t n) {
  (void)c; I *imports = HOST(0); char *t = arg_text(imports, args, n, 1);
  imports->host.configstring_set(imports->host.ctx, (double)qa_guest_integer(args, n, 0), t); free(t); return qa_call_void();
}
static qa_call_result svc_get_configstring(void *ctx, const qa_guest_core_call_context *c, const qa_call_value *args, size_t n) {
  (void)c; I *imports = HOST(0);
  qa_compat_q3_quakelive_game_imports_write_string(imports, qa_guest_required_pointer(args, n, 1),
    imports->host.configstring_get(imports->host.ctx, (double)qa_guest_integer(args, n, 0)), (int64_t)qa_guest_integer(args, n, 2));
  return qa_call_void();
}
static qa_call_result svc_get_userinfo(void *ctx, const qa_guest_core_call_context *c, const qa_call_value *args, size_t n) {
  (void)c; I *imports = HOST(0);
  qa_compat_q3_quakelive_game_imports_write_string(imports, qa_guest_required_pointer(args, n, 1),
    imports->host.get_userinfo(imports->host.ctx, (double)qa_guest_integer(args, n, 0)), (int64_t)qa_guest_integer(args, n, 2));
  return qa_call_void();
}
static qa_call_result svc_set_userinfo(void *ctx, const qa_guest_core_call_context *c, const qa_call_value *args, size_t n) {
  (void)c; I *imports = HOST(0); char *t = arg_text(imports, args, n, 1);
  imports->host.set_userinfo(imports->host.ctx, (double)qa_guest_integer(args, n, 0), t); free(t); return qa_call_void();
}
#undef HOST
bool qa_compat_q3_quakelive_game_imports_init(I *imports, qa_sparse_guest_memory *memory,
  qa_guest_callback_table *callbacks, qa_guest_native_abi abi, qa_compat_q3_native_engine_services host) {
  memset(imports, 0, sizeof *imports);
  imports->memory = memory; imports->callbacks = callbacks; imports->abi = abi; imports->host = host;
  imports->slots = calloc(206, sizeof *imports->slots);
  if (!imports->slots) return false;
  qa_guest_allocation_options options;
  memset(&options, 0, sizeof options);
  options.byte_length = (size_t)(206 * abi.pointer_bytes); options.alignment = 8;
  options.label = "Quake Live game imports";
  imports->address = qa_sparse_allocate(memory, &options);
  /* Unbound contexts live in a per-slot side array owned by slots'
   * service_ctx allocations below (freed on rebind is not tracked;
   * the initial 206 contexts are replaced only by host-service ctx). */
  for (int64_t slot = 0; slot < 206; slot++) {
    char name[32]; snprintf(name, sizeof name, "unbound-%lld", (long long)slot);
    UnboundCtx *u = malloc(sizeof *u);
    if (!u) return false;
    u->imports = imports; u->slot = slot; u->name = dup_text(name);
    if (!u->name) { free(u); return false; }
    qa_compat_q3_quakelive_game_imports_bind(imports, slot, name, NULL, 0, false, QA_STORAGE_INT32, u, unbound_invoke, false);
  }
  static const qa_abi_storage p1[1] = {QA_STORAGE_POINTER};
  static const qa_abi_storage p2[2] = {QA_STORAGE_POINTER, QA_STORAGE_POINTER};
  static const qa_abi_storage pi[2] = {QA_STORAGE_INT32, QA_STORAGE_POINTER};
  static const qa_abi_storage ppi[3] = {QA_STORAGE_POINTER, QA_STORAGE_POINTER, QA_STORAGE_INT32};
  static const qa_abi_storage ipi[3] = {QA_STORAGE_INT32, QA_STORAGE_POINTER, QA_STORAGE_INT32};
  static const qa_abi_storage pppi[4] = {QA_STORAGE_POINTER, QA_STORAGE_POINTER, QA_STORAGE_POINTER, QA_STORAGE_INT32};
  qa_compat_q3_quakelive_game_imports_bind(imports, 0, "SendConsoleCommand", p1, 1, false, QA_STORAGE_INT32, imports, svc_send_console_command, true);
  qa_compat_q3_quakelive_game_imports_bind(imports, 2, "Printf", p1, 1, false, QA_STORAGE_INT32, imports, svc_printf, true);
  qa_compat_q3_quakelive_game_imports_bind(imports, 11, "Cvar_VariableValue", p1, 1, true, QA_STORAGE_FLOAT32, imports, svc_cvar_variable_value, true);
  qa_compat_q3_quakelive_game_imports_bind(imports, 12, "Cvar_Update", p1, 1, false, QA_STORAGE_INT32, imports, svc_cvar_update, true);
  qa_compat_q3_quakelive_game_imports_bind(imports, 13, "Cvar_VariableStringBuffer", ppi, 3, false, QA_STORAGE_INT32, imports, svc_cvar_variable_string_buffer, true);
  qa_compat_q3_quakelive_game_imports_bind(imports, 15, "Cvar_Set", p2, 2, false, QA_STORAGE_INT32, imports, svc_cvar_set, true);
  qa_compat_q3_quakelive_game_imports_bind(imports, 17, "Cvar_Register", pppi, 4, false, QA_STORAGE_INT32, imports, svc_cvar_register, true);
  qa_compat_q3_quakelive_game_imports_bind(imports, 18, "Argv", ipi, 3, false, QA_STORAGE_INT32, imports, svc_argv, true);
  qa_compat_q3_quakelive_game_imports_bind(imports, 20, "Argc", NULL, 0, true, QA_STORAGE_INT32, imports, svc_argc, true);
  qa_compat_q3_quakelive_game_imports_bind(imports, 23, "DropClient", pi, 2, false, QA_STORAGE_INT32, imports, svc_drop_client, true);
  qa_compat_q3_quakelive_game_imports_bind(imports, 24, "SendServerCommand", pi, 2, false, QA_STORAGE_INT32, imports, svc_send_server_command, true);
  qa_compat_q3_quakelive_game_imports_bind(imports, 25, "SetConfigstring", pi, 2, false, QA_STORAGE_INT32, imports, svc_set_configstring, true);
  qa_compat_q3_quakelive_game_imports_bind(imports, 26, "GetConfigstring", ipi, 3, false, QA_STORAGE_INT32, imports, svc_get_configstring, true);
  qa_compat_q3_quakelive_game_imports_bind(imports, 28, "GetUserinfo", ipi, 3, false, QA_STORAGE_INT32, imports, svc_get_userinfo, true);
  qa_compat_q3_quakelive_game_imports_bind(imports, 29, "SetUserinfo", pi, 2, false, QA_STORAGE_INT32, imports, svc_set_userinfo, true);
  return true;
}
void qa_compat_q3_quakelive_game_imports_destroy(I *imports) {
  if (imports->slots) for (size_t i = 0; i < 206; i++) {
    free(imports->slots[i].id); free(imports->slots[i].name); free(imports->slots[i].layouts);
    if (imports->slots[i].service == unbound_invoke) {
      UnboundCtx *u = imports->slots[i].service_ctx; if (u) { free(u->name); free(u); }
    }
  }
  free(imports->slots);
  for (size_t i = 0; i < imports->retired_count; i++) free(imports->retired[i]);
  free(imports->retired); memset(imports, 0, sizeof *imports);
}

/* Landed-host binding for the NativeQ3EngineServices seam
 * (engine_services.h), folded in from the former imports_host.c. */
typedef qa_compat_q3_native_host_adapter A;
static void f_append(void *ctx, const char *text) {
  A *a = ctx; a->host->engine.append_console_command(a->host->engine.ctx, text);
}
static void f_print(void *ctx, const char *text) {
  A *a = ctx; a->host->engine.print(a->host->engine.ctx, text);
}
static void f_drop(void *ctx, double client, const char *reason) {
  A *a = ctx; a->host->engine.drop_client(a->host->engine.ctx, client, reason);
}
static void f_send(void *ctx, double client, const char *text) {
  A *a = ctx; a->host->engine.send_server_command(a->host->engine.ctx, client, text);
}
static const char *f_get_userinfo(void *ctx, double client) {
  A *a = ctx;
  free(a->userinfo);
  a->userinfo = a->host->engine.get_userinfo(a->host->engine.ctx, client);
  return a->userinfo ? a->userinfo : "";
}
static void f_set_userinfo(void *ctx, double client, const char *value) {
  A *a = ctx; a->host->engine.set_userinfo(a->host->engine.ctx, client, value);
}
static double f_cvar_value(void *ctx, const char *name) {
  return qa_cvar_registry_variable_value(((A *)ctx)->host->cvars, name);
}
static char *f_cvar_string(void *ctx, const char *name) {
  return qa_cvar_registry_variable_string(((A *)ctx)->host->cvars, name);
}
static void f_cvar_set(void *ctx, const char *name, const char *value, bool force) {
  qa_cvar_snapshot snapshot;
  memset(&snapshot, 0, sizeof snapshot);
  qa_cvar_registry_set(((A *)ctx)->host->cvars, name, value, force, &snapshot);
  qa_cvar_read_free(&snapshot);
}
static int64_t f_cvar_bind(void *ctx, const char *name, const char *default_value, double flags) {
  return qa_cvar_registry_bind_vm(((A *)ctx)->host->cvars, name, default_value, flags);
}
static bool f_cvar_read(void *ctx, int64_t handle, double *out_modification_count,
                        char **out_value, double *out_numeric_value, double *out_integer_value) {
  qa_cvar_snapshot snapshot;
  memset(&snapshot, 0, sizeof snapshot);
  if (!qa_cvar_registry_read_vm(((A *)ctx)->host->cvars, (double)handle, &snapshot)) return false;
  *out_modification_count = snapshot.modification_count;
  *out_numeric_value = snapshot.numeric_value;
  *out_integer_value = snapshot.integer_value;
  *out_value = snapshot.value ? strdup(snapshot.value) : NULL;
  qa_cvar_read_free(&snapshot);
  return *out_value != NULL;
}
static void f_cs_set(void *ctx, double index, const char *value) {
  A *a = ctx; a->host->configstring_set(a->host->configstrings_ctx, index, value);
}
static const char *f_cs_get(void *ctx, double index) {
  A *a = ctx; return a->host->configstring_get(a->host->configstrings_ctx, index);
}
static const char *const *f_arguments(void *ctx, size_t *out_count) {
  A *a = ctx; return a->command_arguments(a->arguments_ctx, out_count);
}
void qa_compat_q3_native_host_adapter_init(qa_compat_q3_native_host_adapter *adapter,
    qa_app_q3_source_host *host,
    const char *const *(*command_arguments)(void *ctx, size_t *out_count), void *arguments_ctx) {
  memset(adapter, 0, sizeof *adapter);
  adapter->host = host;
  adapter->command_arguments = command_arguments;
  adapter->arguments_ctx = arguments_ctx;
  qa_compat_q3_native_engine_services *s = &adapter->services;
  s->ctx = adapter;
  s->append_console_command = f_append;
  s->print = f_print;
  s->drop_client = f_drop;
  s->send_server_command = f_send;
  s->get_userinfo = f_get_userinfo;
  s->set_userinfo = f_set_userinfo;
  s->cvar_variable_value = f_cvar_value;
  s->cvar_variable_string = f_cvar_string;
  s->cvar_set = f_cvar_set;
  s->cvar_bind_vm = f_cvar_bind;
  s->cvar_read_vm = f_cvar_read;
  s->configstring_set = f_cs_set;
  s->configstring_get = f_cs_get;
  s->command_arguments = f_arguments;
}
void qa_compat_q3_native_host_adapter_destroy(qa_compat_q3_native_host_adapter *adapter) {
  free(adapter->userinfo);
  adapter->userinfo = NULL;
}

