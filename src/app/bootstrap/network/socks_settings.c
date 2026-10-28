/* Port of src/app/bootstrap/network/socks-settings.ts.
 * GPL-2.0-or-later. */
#include "qa/app/bootstrap/network/socks_settings.h"
#include <stdlib.h>
struct qa_app_client_socks_settings {
  qa_cvar_registry *cvars;
  qa_command_cvar_routing base;
  qa_cvar_registry **visible; size_t visible_cap;
};
qa_app_client_socks_settings *qa_app_client_socks_settings_create(
    qa_command_context context, qa_command_print_fn print, void *print_ctx) {
  qa_app_client_socks_settings *s = calloc(1, sizeof *s);
  if (!s) return NULL;
  qa_cvar_registry_options options = {0};
  options.dialect = QA_COMMAND_DIALECT_Q3; options.context = context;
  options.print = print; options.print_ctx = print_ctx;
  s->cvars = qa_cvar_registry_create(&options);
  if (!s->cvars) { free(s); return NULL; }
  double flags = QA_CVAR_FLAG_ARCHIVE | QA_CVAR_FLAG_LATCH;
  qa_cvar_snapshot registered;
  qa_cvar_registry_register(s->cvars, "net_socksEnabled", "0", flags, &registered);
  qa_cvar_registry_register(s->cvars, "net_socksServer", "", flags, &registered);
  qa_cvar_registry_register(s->cvars, "net_socksPort", "1080", flags, &registered);
  qa_cvar_registry_register(s->cvars, "net_socksUsername", "", flags, &registered);
  qa_cvar_registry_register(s->cvars, "net_socksPassword", "", flags, &registered);
  return s;
}
void qa_app_client_socks_settings_destroy(qa_app_client_socks_settings *settings) {
  if (!settings) return;
  qa_cvar_registry_destroy(settings->cvars); free(settings->visible); free(settings);
}
qa_cvar_registry *qa_app_client_socks_settings_cvars(qa_app_client_socks_settings *settings) {
  return settings->cvars;
}
static qa_cvar_registry *route_owner(void *ctx, const char *name, qa_command_context source) {
  qa_app_client_socks_settings *s = ctx;
  qa_cvar_registry *fallback = s->base.owner(s->base.ctx, name, source);
  qa_cvar_read read;
  if (!qa_cvar_registry_find(s->cvars, name, &read)) return fallback;
  qa_cvar_read_free(&read);
  return s->cvars;
}
static qa_cvar_registry *const *route_visible(void *ctx, qa_command_context source, size_t *out_count) {
  qa_app_client_socks_settings *s = ctx;
  size_t count = 0;
  qa_cvar_registry *const *base = s->base.visible(s->base.ctx, source, &count);
  if (s->visible_cap < count + 1) {
    qa_cvar_registry **next = realloc(s->visible, (count + 1) * sizeof *next);
    if (!next) { *out_count = 0; return NULL; }
    s->visible = next; s->visible_cap = count + 1;
  }
  for (size_t i = 0; i < count; i++) s->visible[i] = base[i];
  s->visible[count] = s->cvars;
  *out_count = count + 1;
  return s->visible;
}
qa_command_cvar_routing qa_app_client_socks_settings_route(
    qa_app_client_socks_settings *settings, qa_command_cvar_routing base) {
  settings->base = base;
  qa_command_cvar_routing routing = {settings, route_owner, route_visible};
  return routing;
}
static double cvar_integer(qa_cvar_registry *cvars, const char *name, double fallback) {
  qa_cvar_snapshot snap;
  if (!qa_cvar_registry_get(cvars, name, &snap)) return fallback;
  double value = snap.integer_value; qa_cvar_read_free(&snap);
  return value;
}
void qa_app_client_socks_settings_connect(qa_app_client_socks_settings *settings,
    qa_udp_transport *transport, qa_cvar_registry *cvars) {
  if (!cvars) cvars = settings->cvars;
  size_t count = 0;
  qa_cvar_snapshot *applied = qa_cvar_registry_apply_latched(cvars, NULL, &count);
  for (size_t i = 0; i < count; i++) qa_cvar_read_free(&applied[i]);
  free(applied);
  if (cvar_integer(cvars, "net_socksEnabled", 0) == 0) return;
  char *server = qa_cvar_registry_variable_string(cvars, "net_socksServer");
  char *username = qa_cvar_registry_variable_string(cvars, "net_socksUsername");
  char *password = qa_cvar_registry_variable_string(cvars, "net_socksPassword");
  qa_socks_options options = {server,
    ((int64_t)cvar_integer(cvars, "net_socksPort", 1080)) & 65535, username, password};
  qa_udp_transport_connect_socks(transport, &options);
  free(server); free(username); free(password);
}
