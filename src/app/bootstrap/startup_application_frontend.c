/* Port of src/app/bootstrap/startup.ts: bindFrontendConsole,
 * frontendRoutingState, activateFrontend, failActiveSource, returnToFrontend,
 * publishFrontendRouting, closeFrontendMovie and openFrontendMovie.
 * GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "startup_application_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/audio/output_settings.h"
#include "qa/app/bootstrap/input.h"
#include "qa/app/bootstrap/shared_setting_cvars.h"
#include "qa/console/discovery.h"
#include "qa/foundation/support.h"
#include "qa/input/key_codes.h"
#include "qa/network/common/js_number.h"
#include "qa/platform/sdl.h"
#include "qa/text/media_captions.h"
#include "qa/ui/settings/index.h"
#include "qa/ui/settings/language.h"

static bool eq(const char *a, const char *b) { return a != NULL && b != NULL && strcmp(a, b) == 0; }
static void *grow(void *items, size_t count, size_t size) {
  void *grown = realloc(items, (count + 1) * size);
  if (grown == NULL) qa_range_error("Out of memory");
  return grown;
}
static double now_ms(void *ctx) { (void)ctx; return qa_startup_now(); }
static const qa_app_prepared_seat *prepared_seat(qa_app_prepared_startup *prepared, qa_seat_id id) {
  size_t count = 0;
  const qa_app_prepared_seat *seats = qa_app_prepared_startup_seats(prepared, &count);
  for (size_t i = 0; i < count; i++) if (qa_same_seat(seats[i].id, id)) return &seats[i];
  return NULL;
}
static qa_seat_console *client_console(qa_app_client_bootstrap *client, const qa_session_seat *seat) {
  for (size_t i = 0; i < client->console_count; i++) if (client->consoles[i].seat == seat) return client->consoles[i].console;
  return NULL;
}
static void client_console_set(qa_app_client_bootstrap *client, qa_session_seat *seat, qa_seat_console *console) {
  for (size_t i = 0; i < client->console_count; i++)
    if (client->consoles[i].seat == seat) { client->consoles[i].console = console; return; }
  if (client->console_count == client->console_capacity) {
    size_t capacity = client->console_capacity == 0 ? 4 : client->console_capacity * 2;
    qa_app_client_console_entry *grown = realloc(client->consoles, capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("Out of memory");
    client->consoles = grown;
    client->console_capacity = capacity;
  }
  client->consoles[client->console_count].seat = seat;
  client->consoles[client->console_count].console = console;
  client->console_count++;
}

/* ---- frontendRoutingState(router) ------------------------------------------ */
static void append_text(char **buffer, size_t *length, size_t *capacity, const char *text) {
  size_t n = strlen(text);
  if (*length + n + 1 > *capacity) {
    size_t next = (*capacity == 0 ? 128 : *capacity * 2) + n;
    char *grown = realloc(*buffer, next);
    if (grown == NULL) qa_range_error("Out of memory");
    *buffer = grown;
    *capacity = next;
  }
  memcpy(*buffer + *length, text, n + 1);
  *length += n;
}
/* JSON.stringify({ keyboard, controllers: [{ seat, controller }] }). */
char *qa_startup_frontend_routing_state(qa_input_router *router) {
  char *buffer = NULL;
  size_t length = 0, capacity = 0;
  char text[256];
  qa_seat_id keyboard;
  if (qa_input_router_keyboard_seat(router, &keyboard)) snprintf(text, sizeof text, "{\"keyboard\":%lld,\"controllers\":[", (long long)keyboard.index);
  else snprintf(text, sizeof text, "{\"keyboard\":null,\"controllers\":[");
  append_text(&buffer, &length, &capacity, text);
  size_t count = 0;
  qa_seat_input **inputs = qa_input_router_inputs(router, &count);
  for (size_t i = 0; i < count; i++) {
    qa_seat_id seat = qa_seat_input_seat(inputs[i]);
    qa_controller_selection selection = qa_input_router_controller_selection(router, seat);
    const char *kind = selection.kind == QA_CONTROLLER_SELECTION_AUTOMATIC ? "automatic"
        : selection.kind == QA_CONTROLLER_SELECTION_NONE ? "none" : selection.kind == QA_CONTROLLER_SELECTION_DEVICE ? "device" : "serial";
    snprintf(text, sizeof text, "%s{\"seat\":%lld,\"controller\":{\"kind\":\"%s\"", i == 0 ? "" : ",", (long long)seat.index, kind);
    append_text(&buffer, &length, &capacity, text);
    if (selection.kind == QA_CONTROLLER_SELECTION_DEVICE) {
      snprintf(text, sizeof text, ",\"guid\":\"%s\",\"ordinal\":%.17g", selection.guid, selection.ordinal);
      append_text(&buffer, &length, &capacity, text);
    } else if (selection.kind == QA_CONTROLLER_SELECTION_SERIAL) {
      snprintf(text, sizeof text, ",\"guid\":\"%s\",\"serial\":\"", selection.guid);
      append_text(&buffer, &length, &capacity, text);
      append_text(&buffer, &length, &capacity, selection.serial != NULL ? selection.serial : "");
      append_text(&buffer, &length, &capacity, "\"");
    }
    append_text(&buffer, &length, &capacity, "}}");
  }
  append_text(&buffer, &length, &capacity, "]}");
  return buffer;
}

/* ---- bindFrontendConsole() -------------------------------------------------- */
typedef struct console_binding {
  qa_startup_application *app;
  qa_seat_input *input;
  qa_seat_console *console;
  qa_seat_id seat; qa_client_id client;
  qa_app_prepared_startup *prepared;
  qa_seat_ui_binding *ui;
} console_binding;
static const char *console_clipboard(void *ctx) { return qa_startup_clipboard(((console_binding *)ctx)->app); }
static bool console_connected(void *ctx) { (void)ctx; return false; }
static void console_focus(void *ctx, qa_seat_input_focus focus) {
  console_binding *binding = ctx;
  qa_startup_application *app = binding->app;
  qa_startup_graphics *graphics = app->graphics;
  if (eq(focus.kind, "game")) {
    qa_ui_menu_id active = qa_native_ui_controller_active_menu(qa_app_startup_menu_controller(graphics->menu));
    qa_seat_input_focus menu = {"menu", active != NULL ? active : "menu:startup:main", NULL, false, NULL};
    qa_seat_input_set_focus(binding->input, menu, qa_startup_now());
  } else qa_seat_input_set_focus(binding->input, focus, qa_startup_now());
  qa_app_input_devices_activate(app->client->input_devices, graphics->router);
  qa_input_router_update_capture(graphics->router);
}
static void console_chat(void *ctx, const char *text, bool team, bool has_target, double target) {
  (void)text; (void)team; (void)has_target; (void)target;
  qa_startup_application_print(((console_binding *)ctx)->app, "Chat requires an active connection.\n");
}
static bool console_ui_event(void *ctx, const qa_seat_input_event *event, qa_seat_input_focus focus) {
  console_binding *binding = ctx;
  qa_startup_application *app = binding->app;
  if (eq(event->kind, "key") && (event->code == 96 || event->code == 126)) {
    if (event->down) qa_seat_console_toggle_from_key(binding->console, event->repeat);
    return true;
  }
  if (qa_seat_console_input(binding->console, event, focus) || eq(focus.kind, "console")) return true;
  if (app->frontend_movie != NULL) return qa_app_campaign_cinematic_input(app->frontend_movie, event);
  if (app->remote != NULL && qa_remote_application_has_cinematic(app->remote)) return qa_remote_application_input(app->remote, event);
  return qa_app_startup_menu_input(app->graphics->menu, *event);
}
static void console_output(void *ctx, const char *text, const qa_command_context *source) {
  console_binding *binding = ctx;
  qa_startup_application *app = binding->app;
  if (app->host.base.print != NULL) app->host.base.print(app->host.base.ctx, text);
  const qa_command_origin *origin = source != NULL ? &source->origin : NULL;
  while (origin != NULL && origin->kind == QA_COMMAND_ORIGIN_SCRIPT) origin = origin->caller;
  if (origin == NULL || origin->kind != QA_COMMAND_ORIGIN_LOCAL_SEAT
      || (qa_same_seat(origin->seat, binding->seat) && qa_same_client(origin->client, binding->client)))
    qa_seat_console_print(binding->console, text);
}
static void discovery_print(void *ctx, const char *text) { qa_seat_console_print(((console_binding *)ctx)->console, text); }
static void release_menu_input(qa_startup_application *app) {
  console_binding *binding = app->menu_binding;
  if (binding == NULL) return;
  if (binding->ui != NULL) qa_seat_input_unbind_ui_event(binding->input, binding->ui);
  if (app->menu_output_binding >= 0) qa_app_prepared_startup_unbind_output(binding->prepared, app->menu_output_binding);
  if (app->menu_discovery != NULL) qa_console_discovery_registration_remove(app->menu_discovery);
  app->menu_output_binding = -1;
  app->menu_discovery = NULL;
  app->menu_binding = NULL;
  free(binding);
}
void qa_startup_release_menu_input(qa_startup_application *app) {
  if (app->release_menu_input != NULL) app->release_menu_input(app);
  app->release_menu_input = NULL;
}

void qa_startup_bind_frontend_console(qa_startup_application *app) {
  qa_app_client_bootstrap *client = app->client;
  qa_startup_graphics *graphics = app->graphics;
  if (client == NULL || graphics == NULL || client->local_count == 0) qa_raise("Error", 0, 0, "Frontend console has no retained seat");
  qa_app_client_bootstrap_seat *local = &client->locals[0];
  qa_startup_release_menu_input(app);
  qa_app_prepared_startup *prepared = client->prepared;
  const qa_app_prepared_seat *seat = prepared_seat(prepared, local->prepared);
  if (seat == NULL) qa_raise("Error", 0, 0, "Frontend console has no retained seat");
  console_binding *binding = calloc(1, sizeof *binding);
  if (binding == NULL) qa_range_error("Out of memory");
  binding->app = app;
  binding->input = seat->input;
  binding->prepared = prepared;
  binding->seat = qa_session_seat_id(local->seat);
  binding->client = qa_session_client_id(local->client);
  qa_seat_console_options options;
  memset(&options, 0, sizeof options);
  options.staged = true;
  options.seat = binding->seat;
  /* TS get dialect() { return prepared.commands.dialect }: read when the console is built. */
  options.dialect = qa_command_buffer_dialect(qa_app_prepared_startup_commands(prepared));
  options.context.session = binding->seat.session;
  options.context.origin.kind = QA_COMMAND_ORIGIN_LOCAL_SEAT;
  options.context.origin.seat = binding->seat;
  options.context.origin.client = binding->client;
  options.commands = qa_app_prepared_startup_commands(prepared);
  options.cvars = seat->cvars;
  options.now = now_ms;
  options.connected = console_connected; options.connected_ctx = binding;
  options.clipboard = console_clipboard; options.clipboard_ctx = binding;
  options.focus = console_focus; options.focus_ctx = binding;
  options.chat = console_chat; options.chat_ctx = binding;
  qa_seat_console *candidate = calloc(1, sizeof *candidate);
  if (candidate == NULL || !qa_seat_console_init(candidate, &options)) qa_range_error("Out of memory");
  app->owned_consoles = grow(app->owned_consoles, app->owned_console_count, sizeof *app->owned_consoles);
  app->owned_consoles[app->owned_console_count++] = candidate;
  qa_seat_console *retained = client_console(client, local->seat);
  qa_seat_console *console = retained != NULL ? retained : candidate;
  if (retained == NULL) {
    qa_seat_console_publish(candidate, qa_seat_input_get_focus(seat->input));
    client_console_set(client, local->seat, candidate);
  } else qa_seat_console_adopt(retained, candidate, qa_seat_input_get_focus(seat->input));
  binding->console = console;
  binding->ui = qa_seat_input_bind_ui_event(seat->input, console_ui_event, binding, now_ms, NULL);
  app->menu_output_binding = qa_app_prepared_startup_bind_output(prepared, console_output, binding);
  app->menu_discovery = qa_register_discovery_commands(qa_app_prepared_startup_commands(prepared), discovery_print, binding);
  app->menu_binding = binding;
  app->release_menu_input = release_menu_input;
}

/* ---- activateFrontend(configuration?) -------------------------------------- */
static void router_unhandled(void *ctx, const qa_input_unhandled_event *event) {
  qa_startup_application *app = ctx;
  if (event->is_controller || event->sdl == NULL) return;
  if (event->sdl->kind == QA_SDL_EVENT_QUIT || (event->sdl->kind == QA_SDL_EVENT_WINDOW && event->sdl->as.window.event == 14))
    qa_startup_application_request_quit(app);
}
static double router_ticks(void *ctx) {
  qa_startup_application *app = ctx;
  return qa_sdl_window_ticks(qa_app_native_renderer_window(app->client->renderer));
}
static qa_controller_device *client_devices(void *ctx, size_t *out_count) {
  qa_startup_application *app = ctx;
  return qa_sdl_controllers_devices(app->client->controllers, out_count);
}
static void host_report(void *ctx, const char *text) {
  qa_startup_application *app = ctx;
  if (app->host.base.print != NULL) app->host.base.print(app->host.base.ctx, text);
}
static void menu_reset_bindings(void *ctx, qa_seat_id seat) {
  qa_startup_application *app = ctx;
  qa_app_prepared_startup_reset_bindings(app->client->prepared, seat);
}
static bool menu_bindings_available(void *ctx, qa_seat_id seat) {
  qa_startup_application *app = ctx;
  bool all = false, has_selected = false;
  char *const *keys = NULL;
  size_t key_count = 0, selected_count = 0, authored_count = 0;
  const qa_input_binding *selected = NULL, *authored = NULL;
  /* TS seats.find(seat => seat.id.equals(primary.id))?.authoredBindings != null */
  return qa_app_prepared_startup_seat_choices(app->client->prepared, seat, &all, &keys, &key_count, &has_selected, &selected,
             &selected_count, &authored, &authored_count) && authored != NULL;
}
static qa_audio_output_format audio_output_format(void *ctx) { return qa_app_startup_audio_output_format(ctx); }
static void audio_select_format(void *ctx, qa_audio_output_format format) { qa_app_startup_audio_select_output_format(ctx, format); }

void qa_startup_activate_frontend(qa_startup_application *app, const qa_app_client_frontend_configuration *configuration) {
  qa_app_client_bootstrap *client = app->client;
  qa_startup_graphics *graphics = app->graphics;
  if (client == NULL || graphics == NULL) qa_raise("Error", 0, 0, "Retained frontend is unavailable");
  size_t seat_count = 0;
  const qa_app_prepared_seat *seats = qa_app_prepared_startup_seats(client->prepared, &seat_count);
  if (seat_count == 0) qa_raise("Error", 0, 0, "Frontend has no retained primary seat");
  qa_seat_id primary_id = seats[0].id;
  qa_seat_input *primary_input = seats[0].input;
  if (qa_input_router_seat(graphics->router, primary_id) != primary_input) {
    qa_startup_release_menu_input(app);
    qa_input_router *old_router = graphics->router;
    qa_input_router *previous_router = client->platform.kind == QA_APP_CLIENT_INPUT_WORLD
        ? qa_app_application_input_router(client->platform.input) : old_router;
    qa_input_seat_route route;
    route.input = primary_input;
    if (qa_input_router_seat(previous_router, primary_id) == NULL) {
      memset(&route.controller, 0, sizeof route.controller);
      route.controller.kind = QA_CONTROLLER_SELECTION_AUTOMATIC;
    } else route.controller = qa_input_router_controller_selection(previous_router, primary_id);
    qa_input_router_options options;
    memset(&options, 0, sizeof options);
    options.seats = &route; options.seat_count = 1;
    options.has_keyboard_seat = true; options.keyboard_seat = primary_id;
    graphics->client_controllers = qa_input_controllers_from_sdl(client->controllers);
    options.controllers = &graphics->client_controllers;
    options.defer_platform = true;
    options.now = now_ms;
    options.ticks = router_ticks; options.ticks_ctx = app;
    options.subframe = false;
    options.unhandled = router_unhandled; options.unhandled_ctx = app;
    qa_input_router *router = qa_input_router_create(&options);
    if (router == NULL) qa_range_error("Out of memory");
    if (client->platform.kind == QA_APP_CLIENT_INPUT_MENU) qa_input_router_transfer_window_to(old_router, router);
    qa_app_controller_settings_close(graphics->controller_settings);
    qa_input_router_close(old_router);
    graphics->retired_routers = grow(graphics->retired_routers, graphics->retired_router_count, sizeof *graphics->retired_routers);
    graphics->retired_routers[graphics->retired_router_count++] = old_router;
    graphics->retired_controller_settings = grow(graphics->retired_controller_settings, graphics->retired_controller_setting_count,
        sizeof *graphics->retired_controller_settings);
    graphics->retired_controller_settings[graphics->retired_controller_setting_count++] = graphics->controller_settings;
    graphics->router = router;
    char *state = qa_startup_frontend_routing_state(router);
    free(app->frontend_routing_baseline);
    app->frontend_routing_baseline = state;
    graphics->controller_settings = qa_app_controller_settings_create(router, &primary_id, 1, client_devices, app,
        client->settings, host_report, app);
    if (graphics->controller_settings == NULL) qa_range_error("Out of memory");
    qa_app_startup_input_profile *profile = qa_app_startup_input_profile_retained(client->settings, primary_input);
    if (graphics->input_profile != NULL) qa_app_startup_input_profile_destroy(graphics->input_profile);
    graphics->input_profile = profile;
    qa_startup_graphics_bind_menu_input(app, primary_input, primary_id, menu_bindings_available, menu_reset_bindings);
    qa_ui_gyro_settings gyro = qa_app_controller_settings_ui(graphics->controller_settings, primary_id);
    qa_app_startup_menu_bind_gyro(graphics->menu, &gyro);
  }
  if (client->platform.kind == QA_APP_CLIENT_INPUT_WORLD)
    qa_app_application_input_transfer_platform_to_frontend(client->platform.input, graphics->router,
        configuration != NULL ? configuration->release_target : NULL);
  qa_unified_audio *engine = qa_app_startup_audio_engine(graphics->audio);
  if (client->output_current != engine) {
    qa_unified_audio_transfer_commit(qa_unified_audio_prepare_output_transfer(client->output_current, engine));
    qa_app_client_bootstrap_publish_output(client, engine);
    qa_app_audio_output_target target = {graphics->audio, audio_output_format, audio_select_format};
    qa_app_apply_audio_output_settings(qa_app_image_settings_cvars(client->image_settings), target);
  }
  if (configuration != NULL) {
    if (configuration->release_target != NULL) qa_seat_input_release_to(primary_input, qa_startup_now(), configuration->release_target);
    else qa_seat_input_release(primary_input, qa_startup_now(), configuration->release_commands);
  }
  qa_seat_input_focus focus = {"menu", qa_app_startup_menu_ensure_active_menu(graphics->menu), NULL, false, NULL};
  qa_seat_input_set_focus(primary_input, focus, qa_startup_now());
  qa_app_prepared_startup_set_active_seats(client->prepared, &primary_id, 1);
  memset(&client->platform, 0, sizeof client->platform);
  client->platform.kind = QA_APP_CLIENT_INPUT_MENU;
  client->platform.router = graphics->router;
  client->platform.controller_settings = graphics->controller_settings;
  client->platform.ctx = app;
  client->platform.retire_commands = qa_startup_retire_menu_commands;
  if (configuration == NULL) qa_startup_publish_frontend_routing(app, client);
  else configuration->publish(configuration->ctx);
  qa_startup_bind_frontend_console(app);
  qa_app_input_devices_activate(client->input_devices, graphics->router);
  qa_input_router_update_capture(graphics->router);
}
/* platform.current.retireCommands: () => { releaseMenuInput?.(); releaseMenuInput = null }. */
void qa_startup_retire_menu_commands(void *ctx) { qa_startup_release_menu_input(ctx); }

/* ---- failActiveSource(active, error) --------------------------------------- */
/* A fault in a game or remote session ends that session, not the program. World corruption still ends the process. */
void qa_startup_fail_active_source(qa_startup_application *app, bool active_is_game, const qa_caught_error *error) {
  if (app->closed || app->stopping || (active_is_game && app->game != NULL && qa_application_world_failed(app->game)))
    qa_rethrow(error);
  /* TS error.stack ?? error.message: C errors carry no stack, so the message is printed. */
  qa_startup_printf(app, "Session ended by an error: %s\n", error->message);
  qa_startup_set_text(&app->status, error->message);
  qa_startup_return_to_frontend(app);
}

/* ---- returnToFrontend() ----------------------------------------------------- */
typedef struct retire_call { const qa_app_client_source_lifetime *source; } retire_call;
static void raise_text(const char *error) { if (error != NULL) qa_raise("Error", 0, 0, "%s", error); }
static void retired_close(void *ctx) {
  qa_session_resource *retired = ctx;
  if (retired->close != NULL) retired->close(retired->ctx);
}
void qa_startup_return_to_frontend(qa_startup_application *app) {
  qa_app_client_bootstrap *client = app->client;
  if (client == NULL) qa_raise("Error", 0, 0, "Retained client is unavailable");
  bool has_local_player_count = false;
  double local_player_count = 0;
  if (!app->has_active_demo) {
    const qa_app_options *options = qa_startup_active_options(app);
    if (options != NULL) { has_local_player_count = true; local_player_count = options->seats; }
  }
  const qa_app_client_source_lifetime *source = client->source_current;
  const qa_lobby *room = qa_app_application_local_lobby_current(&app->lobby);
  if (source != NULL && source == app->lobby_source && room != NULL && eq(room->owner, app->lobby_account.id) && eq(room->phase, "playing"))
    raise_text(qa_app_application_local_lobby_complete(&app->lobby));
  raise_text(qa_app_client_source_prepare_retirement(source));
  qa_startup_activate_frontend(app, NULL);
  qa_app_client_source_release_settings(source);
  qa_session_resource retired = qa_engine_session_detach_world(client->session);
  app->game = NULL;
  app->remote = NULL;
  app->gtv_source = NULL;
  free(app->gtv_label);
  app->gtv_label = NULL;
  client->source_current = NULL;
  {
    qa_cleanup guard;
    qa_cleanup_push(&guard, retired_close, &retired);
    raise_text(qa_app_client_source_retire(source));
    qa_cleanup_pop(&guard);
    retired_close(&retired);
  }
  if (has_local_player_count) {
    char seats[32];
    snprintf(seats, sizeof seats, "%s", QA_JS_NUMBER(local_player_count));
    qa_app_startup_selection_select(app->model, "seats", seats);
  }
  app->has_active_demo = false;
  if (app->graphics != NULL) qa_app_startup_menu_set_status(app->graphics->menu, app->status != NULL ? app->status : "", false);
  if (app->demos != NULL) qa_app_client_demo_commands_refresh(app->demos);
  if (qa_app_application_local_lobby_current(&app->lobby) != NULL) {
    if (app->graphics != NULL) qa_app_startup_menu_open_local_lobby(app->graphics->menu);
  } else if (app->graphics != NULL) qa_app_startup_menu_open_main(app->graphics->menu);
}

/* ---- publishFrontendRouting(client) ---------------------------------------- */
typedef struct frontend_routing {
  qa_startup_application *app;
  qa_app_client_bootstrap *client;
  qa_cvar_registry *source, *movement, *fallback;
  qa_app_prepared_seat *seats; size_t seat_count;
  qa_cvar_registry **owners, **copies; size_t copy_count;
  const qa_command_context **contexts; size_t context_count; qa_cvar_registry **context_owners;
  qa_cvar_snapshot *shared; size_t shared_count; const char **shared_names;
} frontend_routing;
static void free_snapshots(qa_cvar_snapshot *snapshots, size_t count) {
  if (snapshots == NULL) return;
  for (size_t i = 0; i < count; i++) qa_cvar_read_free(&snapshots[i]);
  free(snapshots);
}
static qa_cvar_registry *copy_registry(frontend_routing *routing, qa_cvar_registry *owner) {
  for (size_t i = 0; i < routing->copy_count; i++) if (routing->owners[i] == owner) return routing->copies[i];
  qa_startup_application *app = routing->app;
  qa_cvar_registry_options options;
  memset(&options, 0, sizeof options);
  options.dialect = qa_cvar_registry_dialect(owner);
  options.context = qa_cvar_registry_context(owner);
  for (size_t i = 0; i < routing->context_count; i++)
    if (routing->context_owners[i] == owner) options.context = *routing->contexts[i];
  options.print = (qa_command_print_fn)qa_startup_host_print; options.print_ctx = app;
  qa_cvar_registry *cvars = qa_cvar_registry_create(&options);
  if (cvars == NULL) qa_range_error("Out of memory");
  app->owned_registries = grow(app->owned_registries, app->owned_registry_count, sizeof *app->owned_registries);
  app->owned_registries[app->owned_registry_count++] = cvars;
  qa_cvar_transfer *transfer = qa_cvar_registry_prepare_transfer(owner, cvars);
  if (transfer == NULL) qa_range_error("Out of memory");
  if (transfer->destroy != NULL) transfer->destroy(transfer->ctx);
  routing->owners = grow(routing->owners, routing->copy_count, sizeof *routing->owners);
  routing->copies = grow(routing->copies, routing->copy_count, sizeof *routing->copies);
  routing->owners[routing->copy_count] = owner;
  routing->copies[routing->copy_count++] = cvars;
  return cvars;
}
static qa_command_dialect routing_source_dialect(void *ctx) { return qa_cvar_registry_dialect(((frontend_routing *)ctx)->source); }
static bool routing_server(void *ctx, qa_app_console_server *out) {
  frontend_routing *routing = ctx;
  free(routing->shared_names);
  free_snapshots(routing->shared, routing->shared_count);
  routing->shared = qa_cvar_registry_snapshots(routing->source, 0, &routing->shared_count);
  routing->shared_names = calloc(routing->shared_count + 1, sizeof *routing->shared_names);
  if (routing->shared_names == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < routing->shared_count; i++) routing->shared_names[i] = routing->shared[i].name;
  out->cvars = routing->source;
  out->shared_names = routing->shared_names;
  out->shared_name_count = routing->shared_count;
  return true;
}
static qa_cvar_registry *routing_seat(void *ctx, qa_seat_id id) {
  frontend_routing *routing = ctx;
  for (size_t i = 0; i < routing->seat_count; i++)
    if (qa_same_seat(routing->seats[i].id, id) && qa_cvar_registry_dialect(routing->seats[i].cvars) == qa_cvar_registry_dialect(routing->source))
      return routing->seats[i].cvars;
  return NULL;
}
static qa_cvar_registry *routing_input(void *ctx, bool has_seat, qa_seat_id id) {
  frontend_routing *routing = ctx;
  for (size_t i = 0; i < routing->seat_count; i++)
    if ((!has_seat || qa_same_seat(routing->seats[i].id, id))
        && qa_cvar_registry_dialect(routing->seats[i].mouse.cvars) == qa_cvar_registry_dialect(routing->source))
      return routing->seats[i].mouse.cvars;
  return NULL;
}
static qa_cvar_registry *routing_movement(void *ctx) { return ((frontend_routing *)ctx)->movement; }
static qa_cvar_registry *routing_shared(void *ctx) {
  return qa_app_image_settings_cvars(((frontend_routing *)ctx)->client->image_settings);
}
static void routing_forward(void *ctx, const char *name, const char *const *args, size_t arg_count, qa_command_context source) {
  qa_startup_application_frontend_command(((frontend_routing *)ctx)->app, name, args, arg_count, source);
}
/* TS read: (name, context) => scripts.read(name, context): the scope argument is not read. */
static char *routing_read(void *ctx, const char *name, qa_command_context source, qa_app_startup_script_scope scope) {
  (void)scope;
  return qa_app_console_script_files_read(((frontend_routing *)ctx)->client->configuration_current.scripts, name, source);
}
void qa_startup_frontend_routing_free(void *raw) {
  frontend_routing *routing = raw;
  if (routing == NULL) return;
  free(routing->seats); free(routing->owners); free(routing->copies);
  free(routing->contexts); free(routing->context_owners);
  free(routing->shared_names);
  free_snapshots(routing->shared, routing->shared_count);
  free(routing);
}
void qa_startup_publish_frontend_routing(qa_startup_application *app, qa_app_client_bootstrap *client) {
  qa_app_console_script_files *scripts = client->configuration_current.scripts;
  qa_app_prepared_startup *prepared = client->prepared;
  frontend_routing *routing = calloc(1, sizeof *routing);
  if (routing == NULL) qa_range_error("Out of memory");
  routing->app = app;
  routing->client = client;
  qa_cleanup guard;
  qa_cleanup_push(&guard, qa_startup_frontend_routing_free, routing);
  size_t seat_count = 0;
  const qa_app_prepared_seat *seats = qa_app_prepared_startup_seats(prepared, &seat_count);
  routing->contexts = calloc(seat_count * 2 + 1, sizeof *routing->contexts);
  routing->context_owners = calloc(seat_count * 2 + 1, sizeof *routing->context_owners);
  if (routing->contexts == NULL || routing->context_owners == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < seat_count; i++) {
    const qa_command_context *context = &seats[i].context;
    const qa_command_origin *origin = &context->origin;
    if (origin->kind != QA_COMMAND_ORIGIN_LOCAL_SEAT || !qa_same_seat(origin->seat, seats[i].id))
      qa_raise("Error", 0, 0, "Frontend cvar owner has no actual seat context");
    qa_cvar_registry *owners[2] = {seats[i].cvars, seats[i].mouse.cvars};
    for (size_t o = 0; o < 2; o++) {
      size_t at = routing->context_count;
      for (size_t c = 0; c < routing->context_count; c++) if (routing->context_owners[c] == owners[o]) at = c;
      if (at < routing->context_count) {
        const qa_command_origin *previous = &routing->contexts[at]->origin;
        if (previous->kind != QA_COMMAND_ORIGIN_LOCAL_SEAT || !qa_same_seat(previous->seat, origin->seat)
            || !qa_same_client(previous->client, origin->client))
          qa_raise("Error", 0, 0, "Frontend cvar registry is shared by different client seats");
        routing->contexts[at] = context;
      } else {
        routing->context_owners[routing->context_count] = owners[o];
        routing->contexts[routing->context_count++] = context;
      }
    }
  }
  routing->source = copy_registry(routing, qa_app_prepared_startup_source(prepared));
  routing->movement = copy_registry(routing, qa_app_prepared_startup_movement(prepared));
  routing->fallback = copy_registry(routing, qa_app_prepared_startup_fallback(prepared));
  routing->seats = calloc(seat_count + 1, sizeof *routing->seats);
  if (routing->seats == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < seat_count; i++) {
    routing->seats[i] = seats[i];
    routing->seats[i].cvars = copy_registry(routing, seats[i].cvars);
    qa_mouse_settings_init(&routing->seats[i].mouse, copy_registry(routing, seats[i].mouse.cvars));
  }
  routing->seat_count = seat_count;
  qa_app_console_routing_options options;
  memset(&options, 0, sizeof options);
  options.ctx = routing;
  options.fallback = routing->fallback;
  options.source_dialect = routing_source_dialect;
  options.server = routing_server;
  options.seat = routing_seat;
  options.input = routing_input;
  options.movement = routing_movement;
  options.shared = routing_shared;
  qa_app_console_routing *console_routing = qa_app_console_routing_create(options);
  if (console_routing == NULL) qa_range_error("Out of memory");
  qa_app_prepared_owners owners;
  memset(&owners, 0, sizeof owners);
  owners.source = routing->source; owners.movement = routing->movement; owners.fallback = routing->fallback;
  qa_app_prepared_startup_validate_owners(prepared, &owners);
  qa_app_prepared_startup_publish_seats(prepared, routing->seats, routing->seat_count, &routing->seats[0].id, seat_count > 0 ? 1 : 0, true);
  owners.scripts = scripts;
  owners.read = routing_read; owners.read_ctx = routing;
  qa_app_prepared_startup_adopt_console_routing(prepared, console_routing, routing_forward, routing, &owners);
  qa_cleanup_pop(&guard);
  if (app->frontend_routing != NULL) qa_app_console_routing_close(app->frontend_routing);
  app->retired_routings = grow(app->retired_routings, app->retired_routing_count, sizeof *app->retired_routings);
  app->retired_routings[app->retired_routing_count++] = routing;
  app->routing_owners = grow(app->routing_owners, app->routing_owner_count, sizeof *app->routing_owners);
  app->routing_owners[app->routing_owner_count++] = console_routing;
  app->frontend_routing = console_routing;
}

/* ---- closeFrontendMovie() ---------------------------------------------------- */
static void movie_clear(void *ctx) {
  qa_startup_application *app = ctx;
  qa_app_native_renderer *renderer = app->client->renderer;
  qa_render_frame frame;
  memset(&frame, 0, sizeof frame);
  frame.owner = qa_app_native_renderer_owner(renderer);
  frame.sequence = app->frames;
  qa_app_native_renderer_execute(renderer, frame);
}
typedef struct movie_close { qa_startup_application *app; qa_app_campaign_cinematic *movie; struct qa_scene_image_registry *images; } movie_close;
static void movie_close_body(void *ctx) {
  movie_close *close = ctx;
  qa_app_campaign_cinematic_close(close->movie, close->app->frames);
}
static void movie_images_body(void *ctx) {
  movie_close *close = ctx;
  qa_scene_image_registry_close(close->images);
}
void qa_startup_close_frontend_movie(qa_startup_application *app) {
  app->movie_generation++;
  qa_app_campaign_cinematic *current = app->frontend_movie;
  if (current == NULL) return;
  struct qa_scene_image_registry *images = app->frontend_movie_images;
  app->frontend_movie = NULL;
  app->frontend_movie_images = NULL;
  if (app->client == NULL) qa_raise("Error", 0, 0, "Client console output has no published owner");
  movie_close close = {app, current, images};
  void (*steps[4])(void *ctx) = {movie_clear, movie_close_body, movie_images_body, movie_clear};
  void *contexts[4] = {app, &close, &close, app};
  char messages[4][sizeof ((qa_caught_error *)0)->message];
  size_t failures = 0;
  for (size_t i = 0; i < 4; i++) {
    qa_caught_error caught;
    if (!qa_try(steps[i], contexts[i], &caught)) snprintf(messages[failures++], sizeof messages[0], "%s", caught.message);
  }
  qa_app_campaign_cinematic_destroy(current);
  qa_scene_image_registry_destroy(images);
  free(images);
  qa_startup_movie_captions_free(app->frontend_movie_captions);
  app->frontend_movie_captions = NULL;
  if (failures != 0) qa_raise("AggregateError", 0, 0, "Frontend movie retirement failed: %s", messages[0]);
}

/* ---- openFrontendMovie(request) -------------------------------------------- */
/* The movie's caption source and the TS assertCurrent() closure state. */
typedef struct movie_captions {
  qa_startup_application *app;
  qa_app_client_bootstrap *client;
  qa_app_console_script_files *scripts;
  qa_unified_audio *output;
  double generation;
  qa_seat_id seat;
  qa_seat_media_captions captions; bool has_captions;
  qa_ui_seat_preferences preferences;
  char *language;
  qa_app_screen_cinematic_captions view;
} movie_captions;
void qa_startup_movie_captions_free(void *raw) {
  movie_captions *captions = raw;
  if (captions == NULL) return;
  if (captions->has_captions) qa_seat_media_captions_destroy(&captions->captions);
  free(captions->language);
  free(captions);
}
static void assert_current(movie_captions *captions) {
  qa_startup_application *app = captions->app;
  if (app->closed || app->stopping || app->movie_generation != captions->generation || app->client != captions->client
      || captions->client->configuration_current.scripts != captions->scripts || captions->client->output_current != captions->output)
    qa_raise("Error", 0, 0, "Movie preparation belongs to a retired frontend");
}
static bool movie_current(void *ctx) {
  assert_current(ctx);
  return true;
}
static uint8_t *movie_captions_read(void *ctx, const char *path, size_t *out_length) {
  movie_captions *captions = ctx;
  assert_current(captions);
  uint8_t *bytes = NULL;
  size_t count = 0;
  bool found = qa_app_console_script_files_read_mounted(captions->scripts, path, &bytes, &count);
  assert_current(captions);
  *out_length = found ? count : 0;
  return found ? bytes : NULL;
}
static const char *movie_captions_language(void *ctx) {
  movie_captions *captions = ctx;
  char *language = qa_ui_read_seat_language(qa_app_image_settings_cvars(captions->client->image_settings), captions->seat.index);
  if (language == NULL) qa_range_error("Out of memory");
  free(captions->language);
  captions->language = language;
  return language;
}
static void movie_captions_failed(void *ctx, const char *error) {
  qa_startup_printf(((movie_captions *)ctx)->app, "Caption language reload failed: %s\n", error);
}
static void movie_captions_prepare(void *ctx, const char *source) {
  movie_captions *captions = ctx;
  assert_current(captions);
  if (!qa_seat_media_captions_prepare(&captions->captions, source, movie_captions_language(captions))) qa_range_error("Out of memory");
  assert_current(captions);
}
static qa_render_command *movie_captions_commands(void *ctx, const qa_app_cinematic_caption_timeline *timeline, qa_rect viewport,
    size_t *out_count) {
  movie_captions *captions = ctx;
  bool enabled = qa_ui_seat_preferences_values(&captions->preferences).captions;
  qa_caption_preferences selected;
  memset(&selected, 0, sizeof selected);
  selected.subtitles = enabled;
  selected.sound_captions = enabled;
  selected.speakers = true;
  qa_active_caption *active = NULL;
  size_t active_count = 0;
  if (!qa_seat_media_captions_active(&captions->captions, timeline->state, selected, &active, &active_count))
    qa_range_error("Out of memory");
  qa_cleanup guard;
  qa_cleanup_push(&guard, free, active);
  qa_app_client_bootstrap *client = captions->client;
  const qa_render_command *rows = client->caption_commands(client->bootstrap_ctx, active, active_count, viewport,
      timeline->elapsed_milliseconds, out_count);
  qa_render_command *copy = calloc(*out_count + 1, sizeof *copy);
  if (copy == NULL) qa_range_error("Out of memory");
  if (*out_count != 0) memcpy(copy, rows, *out_count * sizeof *copy);
  qa_cleanup_pop(&guard);
  free(active);
  return copy;
}
typedef struct movie_open {
  qa_startup_application *app;
  const qa_app_startup_movie_request *request;
  movie_captions *captions;
  struct qa_scene_image_registry *images;
  qa_app_campaign_cinematic *prepared;
} movie_open;
static void movie_open_body(void *ctx) {
  movie_open *open = ctx;
  qa_startup_application *app = open->app;
  movie_captions *captions = open->captions;
  qa_app_client_bootstrap *client = captions->client;
  /* TS CampaignCinematic.prepare reads through scripts.openMounted(path, context); the C prepare
   * takes the mounted content those scripts read, which is the startup configuration's content. */
  if (captions->scripts != app->scripts || app->configuration == NULL)
    qa_raise("Error", 0, 0, "Movie playback requires the startup configuration mounts");
  const struct qa_content_mounted *mounts = qa_app_configuration_content_mounts(app->configuration);
  qa_live_language live = {captions, movie_captions_language, movie_captions_failed};
  qa_seat_media_captions_init(&captions->captions, captions->seat, movie_captions_read, captions, NULL, false, &live);
  captions->has_captions = true;
  qa_ui_seat_preferences_init(&captions->preferences, captions->seat, qa_app_image_settings_cvars(client->image_settings));
  captions->view.ctx = captions;
  captions->view.prepare = movie_captions_prepare;
  captions->view.commands = movie_captions_commands;
  qa_app_screen_cinematic_request request = {open->request->name, open->request->loop, open->request->hold, open->request->silent};
  open->prepared = qa_app_campaign_cinematic_prepare_engine(request, mounts, open->images, captions->output, client->renderer,
      captions->seat, movie_current, captions, &captions->view);
  if (open->prepared == NULL) qa_range_error("Out of memory");
  assert_current(captions);
  qa_app_campaign_cinematic_activate(open->prepared);
  assert_current(captions);
}
static void movie_failed_close(void *ctx) {
  movie_open *open = ctx;
  if (open->prepared != NULL) qa_app_campaign_cinematic_close(open->prepared, open->app->frames);
}
static void movie_failed_images(void *ctx) { qa_scene_image_registry_close(((movie_open *)ctx)->images); }
static void movie_failed_clear(void *ctx) { movie_clear(((movie_open *)ctx)->app); }
void qa_startup_open_frontend_movie(qa_startup_application *app, const qa_app_startup_movie_request *request) {
  if (app->client == NULL) qa_raise("Error", 0, 0, "Client console output has no published owner");
  qa_app_client_bootstrap *client = app->client;
  if (client->local_count == 0) qa_raise("Error", 0, 0, "Movie playback requires a local seat");
  movie_captions *captions = calloc(1, sizeof *captions);
  if (captions == NULL) qa_range_error("Out of memory");
  captions->app = app;
  captions->client = client;
  captions->scripts = client->configuration_current.scripts;
  captions->output = client->output_current;
  captions->seat = qa_session_seat_id(client->locals[0].seat);
  qa_cleanup guard;
  qa_cleanup_push(&guard, qa_startup_movie_captions_free, captions);
  qa_startup_close_frontend_movie(app);
  captions->generation = app->movie_generation;
  assert_current(captions);
  qa_cleanup_pop(&guard);
  movie_open open = {app, request, captions, NULL, NULL};
  open.images = calloc(1, sizeof *open.images);
  if (open.images == NULL) { qa_startup_movie_captions_free(captions); qa_range_error("Out of memory"); }
  qa_scene_image_registry_fork(qa_app_native_renderer_images(client->renderer), NULL, open.images);
  app->movie_preparations++;
  qa_caught_error caught;
  bool opened = qa_try(movie_open_body, &open, &caught);
  app->movie_preparations--;
  if (!opened) {
    void (*steps[3])(void *ctx) = {movie_failed_close, movie_failed_images, movie_failed_clear};
    size_t failures = 0;
    char cleanup[sizeof caught.message] = "";
    for (size_t i = 0; i < 3; i++) {
      qa_caught_error failed;
      if (!qa_try(steps[i], &open, &failed)) { if (failures++ == 0) snprintf(cleanup, sizeof cleanup, "%s", failed.message); }
    }
    if (open.prepared != NULL) qa_app_campaign_cinematic_destroy(open.prepared);
    qa_scene_image_registry_destroy(open.images);
    free(open.images);
    qa_startup_movie_captions_free(captions);
    if (failures != 0) qa_raise("AggregateError", 0, 0, "Frontend movie preparation and cleanup failed: %s; %s", caught.message, cleanup);
    qa_rethrow(&caught);
  }
  app->frontend_movie = open.prepared;
  app->frontend_movie_images = open.images;
  app->frontend_movie_captions = captions;
  app->last_frame = qa_startup_now();
}
