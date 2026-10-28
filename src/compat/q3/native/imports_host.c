/* Landed-host binding for the NativeQ3EngineServices seam. This is
 * the TU that sees qa/app/bootstrap/simulation/q3/types.h; it must
 * not include guest ABI headers (see engine_services.h).
 * GPL-2.0-or-later. */
#include "qa/compat/q3/native/engine_services.h"
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/simulation/q3/types.h"
#include "qa/core/cvars/index.h"
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
