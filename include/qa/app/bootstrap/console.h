/* Port of src/app/bootstrap/console.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_CONSOLE_H
#define QA_APP_BOOTSTRAP_CONSOLE_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/common.h"
#include "qa/contracts/identity.h"
#include "qa/core/commands/index.h"
#include "qa/core/cvars/index.h"
/* TS ApplicationConsoleServer. shared_names borrowed. */
typedef struct qa_app_console_server {
  qa_cvar_registry *cvars;
  const char *const *shared_names; size_t shared_name_count;
} qa_app_console_server;
/* TS q1ConsoleServer source pick: first non-NULL wins. The returned
 * server's shared_names are the static frame-time names. False = null. */
bool qa_app_q1_console_server(qa_cvar_registry *q1_source, qa_cvar_registry *quakec_source, qa_app_console_server *out);
typedef struct qa_app_console_routing qa_app_console_routing;
typedef struct qa_app_console_routing_options {
  void *ctx;
  qa_cvar_registry *fallback;
  qa_command_dialect (*source_dialect)(void *ctx);
  bool (*server)(void *ctx, qa_app_console_server *out); /* false = TS null */
  qa_cvar_registry *(*seat)(void *ctx, qa_seat_id id);
  /* Optional; has_seat false mirrors TS null id (primary seat). */
  qa_cvar_registry *(*input)(void *ctx, bool has_seat, qa_seat_id id);
  qa_cvar_registry *(*movement)(void *ctx); /* optional, may be NULL */
  qa_cvar_registry *(*shared)(void *ctx);   /* optional, may be NULL */
} qa_app_console_routing_options;
qa_app_console_routing *qa_app_console_routing_create(qa_app_console_routing_options options);
void qa_app_console_routing_destroy(qa_app_console_routing *routing);
/* Landed CommandCvarRouting view (borrowed; valid while routing lives). */
qa_command_cvar_routing qa_app_console_routing_view(qa_app_console_routing *routing);
qa_cvar_registry *qa_app_console_routing_owner(qa_app_console_routing *routing, const char *name_input, qa_command_context source);
/* Fills caller array (max 6); returns count. */
size_t qa_app_console_routing_visible(qa_app_console_routing *routing, qa_command_context source, qa_cvar_registry **out, size_t capacity);
void qa_app_console_routing_close(qa_app_console_routing *routing);
#endif
