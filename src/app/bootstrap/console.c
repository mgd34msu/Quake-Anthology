/* Port of src/app/bootstrap/console.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/console.h"
#include <stdlib.h>
#include "qa/app/bootstrap/frame_time.h"
#include "qa/core/commands/text.h"
#include "qa/foundation/support.h"
bool qa_app_q1_console_server(qa_cvar_registry *q1_source, qa_cvar_registry *quakec_source, qa_app_console_server *out) {
  qa_cvar_registry *source = q1_source ? q1_source : quakec_source;
  if (!source) return false;
  size_t count = 0;
  const char *const *names = qa_app_frame_time_cvar_names(qa_cvar_registry_dialect(source), &count);
  *out = (qa_app_console_server){source, names, count};
  return true;
}
struct qa_app_console_routing {
  qa_app_console_routing_options options;
  bool closed;
  qa_cvar_registry *visible_cache[6];
  size_t visible_count;
};
static const qa_command_origin *caller(const qa_command_origin *origin) {
  while (origin->kind == QA_COMMAND_ORIGIN_SCRIPT && origin->caller) origin = origin->caller;
  return origin;
}
static bool same_session(qa_session_id a, qa_session_id b) { return a.token == b.token; }
typedef struct {
  bool has_server; qa_app_console_server server;
  qa_cvar_registry *seat, *input, *movement;
  const qa_command_origin *origin;
} owners_t;
static owners_t owners(qa_app_console_routing *routing, qa_command_context source) {
  if (routing->closed) qa_range_error("Application console owner is closed");
  qa_app_console_routing_options *options = &routing->options;
  qa_command_dialect dialect = options->source_dialect(options->ctx);
  qa_command_context fallback_ctx = qa_cvar_registry_context(options->fallback);
  if (!same_session(source.session, fallback_ctx.session)) qa_range_error("Console command belongs to another session");
  if (dialect != qa_cvar_registry_dialect(options->fallback)) qa_range_error("Changing source command dialect requires a new console owner");
  owners_t out = {0};
  out.has_server = options->server(options->ctx, &out.server);
  out.origin = caller(&source.origin);
  if (out.origin->kind == QA_COMMAND_ORIGIN_REMOTE_CLIENT) qa_range_error("Remote client cvars require an explicit client owner");
  out.seat = out.origin->kind == QA_COMMAND_ORIGIN_LOCAL_SEAT ? options->seat(options->ctx, out.origin->seat) : NULL;
  out.input = out.origin->kind == QA_COMMAND_ORIGIN_SERVER_CONSOLE || !options->input ? NULL
      : options->input(options->ctx, out.origin->kind == QA_COMMAND_ORIGIN_LOCAL_SEAT,
                       out.origin->kind == QA_COMMAND_ORIGIN_LOCAL_SEAT ? out.origin->seat : (qa_seat_id){0});
  out.movement = options->movement ? options->movement(options->ctx) : NULL;
  if (out.movement && !same_session(qa_cvar_registry_context(out.movement).session, source.session))
    qa_range_error("Movement cvars belong to another session");
  qa_cvar_registry *check[3] = {out.has_server ? out.server.cvars : NULL, out.seat, out.input};
  for (size_t i = 0; i < 3; i++) {
    if (!check[i]) continue;
    if (!same_session(qa_cvar_registry_context(check[i]).session, source.session) || qa_cvar_registry_dialect(check[i]) != dialect)
      qa_range_error("Console cvar registry has another session or source dialect");
  }
  if (out.input) {
    qa_command_context input_ctx = qa_cvar_registry_context(out.input);
    if (caller(&input_ctx.origin)->kind != QA_COMMAND_ORIGIN_LOCAL_SEAT)
      qa_range_error("Mouse settings require a local seat owner");
  }
  if (out.origin->kind == QA_COMMAND_ORIGIN_LOCAL_SEAT) {
    if (!same_session(out.origin->seat.session, source.session) || !same_session(out.origin->client.session, source.session))
      qa_range_error("Console seat belongs to another session");
    qa_cvar_registry *seat_check[2] = {out.seat, out.input};
    for (size_t i = 0; i < 2; i++) {
      if (!seat_check[i]) continue;
      qa_command_context registry_ctx = qa_cvar_registry_context(seat_check[i]);
      const qa_command_origin *seat_origin = caller(&registry_ctx.origin);
      if (seat_origin->kind != QA_COMMAND_ORIGIN_LOCAL_SEAT || !qa_same_seat(seat_origin->seat, out.origin->seat) ||
          !(seat_origin->client.token == out.origin->client.token && seat_origin->client.slot == out.origin->client.slot && seat_origin->client.generation == out.origin->client.generation))
        qa_range_error("Console cvar registry belongs to another seat");
    }
  }
  return out;
}
static bool has_cvar(qa_cvar_registry *registry, const char *name) {
  if (!registry) return false;
  qa_cvar_read read;
  bool found = qa_cvar_registry_find(registry, name, &read);
  if (found) qa_cvar_read_free(&read);
  return found;
}
qa_app_console_routing *qa_app_console_routing_create(qa_app_console_routing_options options) {
  if (options.source_dialect(options.ctx) != qa_cvar_registry_dialect(options.fallback))
    qa_range_error("Console dialect must match the source game");
  qa_app_console_routing *routing = calloc(1, sizeof *routing);
  if (!routing) return NULL;
  routing->options = options;
  return routing;
}
void qa_app_console_routing_destroy(qa_app_console_routing *routing) { free(routing); }
qa_cvar_registry *qa_app_console_routing_owner(qa_app_console_routing *routing, const char *name_input, qa_command_context source) {
  char *name = qa_source_command_text(name_input);
  owners_t found = owners(routing, source);
  qa_cvar_registry *shared = routing->options.shared ? routing->options.shared(routing->options.ctx) : NULL;
  qa_cvar_registry *result;
  if (has_cvar(shared, name)) result = shared;
  else if (found.has_server && has_cvar(found.server.cvars, name)) result = found.server.cvars;
  else if (has_cvar(found.input, name)) result = found.input;
  else {
    bool seat_has = has_cvar(found.seat, name), movement_has = has_cvar(found.movement, name);
    if (seat_has && movement_has && found.seat != found.movement) {
      qa_range_error("Console cvar %s has conflicting seat and movement declarations", name);
    }
    if (seat_has) result = found.seat;
    else if (movement_has) result = found.movement;
    else if (has_cvar(routing->options.fallback, name)) result = routing->options.fallback;
    else if (found.origin->kind == QA_COMMAND_ORIGIN_SERVER_CONSOLE && found.has_server) result = found.server.cvars;
    else result = found.seat ? found.seat : routing->options.fallback;
  }
  free(name);
  return result;
}
size_t qa_app_console_routing_visible(qa_app_console_routing *routing, qa_command_context source, qa_cvar_registry **out, size_t capacity) {
  owners_t found = owners(routing, source);
  qa_cvar_registry *shared = routing->options.shared ? routing->options.shared(routing->options.ctx) : NULL;
  qa_cvar_registry *candidates[6] = {shared, found.input, found.has_server ? found.server.cvars : NULL, found.seat, found.movement, routing->options.fallback};
  size_t count = 0;
  for (size_t i = 0; i < 6; i++) {
    if (!candidates[i]) continue;
    bool seen = false;
    for (size_t j = 0; j < count; j++) if (out[j] == candidates[i]) seen = true;
    if (!seen && count < capacity) out[count++] = candidates[i];
  }
  return count;
}
static qa_cvar_registry *view_owner(void *ctx, const char *name, qa_command_context source) {
  return qa_app_console_routing_owner(ctx, name, source);
}
static qa_cvar_registry *const *view_visible(void *ctx, qa_command_context source, size_t *out_count) {
  qa_app_console_routing *routing = ctx;
  routing->visible_count = qa_app_console_routing_visible(routing, source, routing->visible_cache, 6);
  *out_count = routing->visible_count;
  return routing->visible_cache;
}
qa_command_cvar_routing qa_app_console_routing_view(qa_app_console_routing *routing) {
  return (qa_command_cvar_routing){routing, view_owner, view_visible};
}
void qa_app_console_routing_close(qa_app_console_routing *routing) { routing->closed = true; }
