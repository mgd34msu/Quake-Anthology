/* Application.openGuestBrowser (src/app/bootstrap/application.ts): the local Q3
 * guest's StartupServerBrowser and the Q3BrowserView that reads it. TS passes the
 * browser itself as the Q3BrowserHost; C goes through
 * qa_app_startup_server_browser_q3_host_create. GPL-2.0-or-later. */
#include "application_internal.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "qa/network/common/transport.h"

/* now: () => performance.now() */
static double guest_browser_now(void *ctx) {
  (void)ctx;
  return qa_net_default_now(NULL);
}
/* this.localGuest?.seats.values().next().value?.cvars.variableValue(name) ?? fallback */
static double first_guest_seat_value(qa_application *app, const char *name, double fallback) {
  qa_application_local_q3_guest_world *world = app->local_guest;
  if (world == NULL || world->seat_count == 0 || world->seats[0].cvars == NULL) return fallback;
  return qa_cvar_registry_variable_value(world->seats[0].cvars, name);
}
static double guest_browser_max_ping(void *ctx) { return first_guest_seat_value(ctx, "cl_maxPing", 800); }
static double guest_browser_status_resend(void *ctx) { return first_guest_seat_value(ctx, "cl_serverStatusResendTime", 750); }
/* print: text => this.host.print(text) */
static void guest_browser_print(void *ctx, const char *text) {
  qa_application_printer printer = qa_application_host_printer(ctx);
  printer.print(printer.ctx, text);
}
void qa_application_release_guest_browser(qa_application_local_q3_guest_browser *browser) {
  if (browser == NULL) return;
  if (browser->view != NULL) qa_q3_browser_view_destroy(browser->view);
  qa_app_startup_server_browser_q3_host_destroy(browser->adapter);
  qa_app_startup_server_browser_destroy(browser->host);
  free(browser);
}
typedef struct guest_browser_scope { qa_application_local_q3_guest_browser *record; } guest_browser_scope;
/* catch (error) { await host.close(); throw error } */
static void guest_browser_scope_release(void *arg) {
  guest_browser_scope *scope = arg;
  qa_application_release_guest_browser(scope->record);
  scope->record = NULL;
}
qa_application_local_q3_guest_browser *qa_application_open_guest_browser(qa_application *app) {
  qa_app_startup_server_browser *host = qa_app_startup_server_browser_open(&app->input_config);
  if (host == NULL) return NULL;
  guest_browser_scope scope = {calloc(1, sizeof *scope.record)};
  if (scope.record == NULL) { qa_app_startup_server_browser_destroy(host); return NULL; }
  scope.record->host = host;
  qa_cleanup guard;
  qa_cleanup_push(&guard, guest_browser_scope_release, &scope);
  scope.record->adapter = qa_app_startup_server_browser_q3_host_create(host);
  qa_q3_browser_view_options options;
  memset(&options, 0, sizeof options);
  options.browser = qa_app_startup_q3_browser_host_view(scope.record->adapter);
  options.now = guest_browser_now;
  options.max_ping = guest_browser_max_ping;
  options.status_resend_time = guest_browser_status_resend;
  options.print = guest_browser_print;
  options.print_ctx = app;
  options.ctx = app;
  if (!qa_q3_browser_view_init(&scope.record->view, &options)) qa_range_error("Out of memory");
  qa_cleanup_pop(&guard);
  return scope.record;
}
