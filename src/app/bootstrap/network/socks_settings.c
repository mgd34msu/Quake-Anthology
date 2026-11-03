/* Port of src/app/bootstrap/network/socks-settings.ts.
 * GPL-2.0-or-later. */
#include "qa/app/bootstrap/network/socks_settings.h"
#include <math.h>
#include <stdlib.h>
#include "qa/foundation/support.h"
/* One TS route() closure: its own `base` and visible storage. TS returns a fresh closure per call, so routes built for a
 * candidate configuration never change what an earlier (published) route reports. Kept until the settings are destroyed
 * (the TS closures stay reachable from the configurations that hold them). */
typedef struct socks_route {
  struct socks_route *next;
  qa_app_client_socks_settings *settings;
  qa_command_cvar_routing base;
  qa_cvar_registry **visible; size_t visible_cap;
} socks_route;
struct qa_app_client_socks_settings {
  qa_cvar_registry *cvars;
  socks_route *routes;
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
  static const char *const names[] = {"net_socksEnabled", "net_socksServer", "net_socksPort", "net_socksUsername",
      "net_socksPassword"};
  static const char *const values[] = {"0", "", "1080", "", ""};
  for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
    qa_cvar_snapshot registered;
    if (qa_cvar_registry_register(s->cvars, names[i], values[i], flags, &registered)) qa_cvar_read_free(&registered);
  }
  return s;
}
void qa_app_client_socks_settings_destroy(qa_app_client_socks_settings *settings) {
  if (!settings) return;
  for (socks_route *route = settings->routes, *next; route != NULL; route = next) {
    next = route->next;
    free(route->visible);
    free(route);
  }
  qa_cvar_registry_destroy(settings->cvars); free(settings);
}
qa_cvar_registry *qa_app_client_socks_settings_cvars(qa_app_client_socks_settings *settings) {
  return settings->cvars;
}
static qa_cvar_registry *route_owner(void *ctx, const char *name, qa_command_context source) {
  socks_route *route = ctx;
  qa_cvar_registry *fallback = route->base.owner(route->base.ctx, name, source);
  qa_cvar_read read;
  if (!qa_cvar_registry_find(route->settings->cvars, name, &read)) return fallback;
  qa_cvar_read_free(&read);
  return route->settings->cvars;
}
static qa_cvar_registry *const *route_visible(void *ctx, qa_command_context source, size_t *out_count) {
  socks_route *route = ctx;
  size_t count = 0;
  qa_cvar_registry *const *base = route->base.visible(route->base.ctx, source, &count);
  if (route->visible_cap < count + 1) {
    qa_cvar_registry **next = realloc(route->visible, (count + 1) * sizeof *next);
    if (!next) qa_range_error("Out of memory");
    route->visible = next; route->visible_cap = count + 1;
  }
  for (size_t i = 0; i < count; i++) route->visible[i] = base[i];
  route->visible[count] = route->settings->cvars;
  *out_count = count + 1;
  return route->visible;
}
qa_command_cvar_routing qa_app_client_socks_settings_route(
    qa_app_client_socks_settings *settings, qa_command_cvar_routing base) {
  socks_route *route = calloc(1, sizeof *route);
  if (route == NULL) qa_range_error("Out of memory");
  route->settings = settings;
  route->base = base;
  route->next = settings->routes;
  settings->routes = route;
  qa_command_cvar_routing routing = {route, route_owner, route_visible};
  return routing;
}
static double cvar_integer(qa_cvar_registry *cvars, const char *name, double fallback) {
  qa_cvar_snapshot snap;
  if (!qa_cvar_registry_get(cvars, name, &snap)) return fallback;
  double value = snap.integer_value; qa_cvar_read_free(&snap);
  return value;
}
/* JS ToInt32 for `integerValue & 65535`. */
static int64_t js_port_bits(double value) {
  if (!isfinite(value)) return 0;
  double t = fmod(trunc(value), 65536.0);
  if (t < 0) t += 65536.0;
  return (int64_t)t;
}
static void free_text(void *text) { free(text); }
void qa_app_client_socks_settings_connect(qa_app_client_socks_settings *settings,
    qa_socks_connector transport, qa_cvar_registry *cvars) {
  if (!cvars) cvars = settings->cvars;
  size_t count = 0;
  qa_cvar_snapshot *applied = qa_cvar_registry_apply_latched(cvars, NULL, &count);
  for (size_t i = 0; i < count; i++) qa_cvar_read_free(&applied[i]);
  free(applied);
  if (cvar_integer(cvars, "net_socksEnabled", 0) == 0) return;
  char *server = qa_cvar_registry_variable_string(cvars, "net_socksServer");
  qa_cleanup server_guard; qa_cleanup_push(&server_guard, free_text, server);
  int64_t port = js_port_bits(cvar_integer(cvars, "net_socksPort", 1080));
  char *username = qa_cvar_registry_variable_string(cvars, "net_socksUsername");
  qa_cleanup username_guard; qa_cleanup_push(&username_guard, free_text, username);
  char *password = qa_cvar_registry_variable_string(cvars, "net_socksPassword");
  qa_cleanup password_guard; qa_cleanup_push(&password_guard, free_text, password);
  qa_socks_options options = {server, port, username, password};
  transport.connect_socks(transport.ctx, &options);
  qa_cleanup_pop(&password_guard); qa_cleanup_pop(&username_guard); qa_cleanup_pop(&server_guard);
  free(server); free(username); free(password);
}
