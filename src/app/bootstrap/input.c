/* Port of src/app/bootstrap/input.ts: movementDialect, the ApplicationInput
 * factories and constructor, readSettings, createJoinedLocal, ownership and
 * the plain accessors. Commands live in input_commands.c; platform,
 * publication and seat changes in input_platform.c. GPL-2.0-or-later. */
#include "input_internal.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/cvar_archives.h"
#include "qa/contracts/execution.h"
#include "qa/contracts/time.h"
#include "qa/foundation/support.h"
#include "qa/input/support.h"
#include "qa/network/common/js_number.h"
#include "qa/persistence/value.h"

/* ---- allocation ---- */
void *qa_app_input_internal_grow(void *items, size_t *capacity, size_t count, size_t size) {
  if (count < *capacity) return items;
  size_t next = *capacity == 0 ? 4 : *capacity * 2;
  void *grown = realloc(items, next * size);
  if (grown == NULL) qa_range_error("Out of memory");
  *capacity = next;
  return grown;
}

char *qa_app_input_internal_dup(const char *text) {
  if (text == NULL) return NULL;
  char *copy = qa_input_dup(text);
  if (copy == NULL) qa_range_error("Out of memory");
  return copy;
}

static void *xcalloc(size_t count, size_t size) {
  void *value = calloc(count == 0 ? 1 : count, size);
  if (value == NULL) qa_range_error("Out of memory");
  return value;
}

/* ---- TS movementDialect ---- */
static qa_command_dialect dialect_of_clock(qa_clock_profile_kind kind) {
  switch (kind) {
    case QA_CLOCK_Q1_NETQUAKE: return QA_COMMAND_DIALECT_Q1_NETQUAKE;
    case QA_CLOCK_Q1_QUAKEWORLD: return QA_COMMAND_DIALECT_Q1_QUAKEWORLD;
    case QA_CLOCK_Q2_CLASSIC: return QA_COMMAND_DIALECT_Q2_CLASSIC;
    case QA_CLOCK_Q2_RERELEASE: return QA_COMMAND_DIALECT_Q2_RERELEASE;
    default: return QA_COMMAND_DIALECT_Q3;
  }
}

qa_command_dialect qa_app_movement_dialect(const qa_app_options *options, const qa_executable_recipe *recipe) {
  if (recipe != NULL) {
    for (size_t i = 0; i < recipe->execution_count; i++) {
      const qa_execution_module *module = &recipe->execution[i];
      if (module->kind != QA_EXECUTION_MODULE_NATIVE || strcmp(module->role, "server-game") != 0) continue;
      if (module->api.kind == QA_GAME_API_Q2_GAME) {
        if (module->api.as.q2_game.kind == QA_Q2_GAME_API_RERELEASE) return QA_COMMAND_DIALECT_Q2_RERELEASE;
        return QA_COMMAND_DIALECT_Q2_CLASSIC;
      }
    }
    const qa_provider_timing *timing = NULL;
    for (size_t i = 0; i < recipe->timing_count && timing == NULL; i++)
      if (strcmp(recipe->timing[i].provider, recipe->movement.provider) == 0) timing = &recipe->timing[i];
    if (timing == NULL) qa_input_error("Recipe has no timing for %s", recipe->movement.provider);
    return dialect_of_clock(timing->clock.kind);
  }
  if (options->network_kind != NULL && strcmp(options->network_kind, "qw-client") == 0)
    return QA_COMMAND_DIALECT_Q1_QUAKEWORLD;
  return options->movement == QA_GAME_FAMILY_Q1 ? QA_COMMAND_DIALECT_Q1_NETQUAKE
      : options->movement == QA_GAME_FAMILY_Q2 ? QA_COMMAND_DIALECT_Q2_CLASSIC : QA_COMMAND_DIALECT_Q3;
}

static uint32_t sdl_ticks(void *ctx) { return qa_sdl_window_ticks(ctx); }
qa_app_input_window qa_app_input_window_from_sdl(qa_sdl_window *window) {
  qa_app_input_window view = {qa_input_window_from_sdl(window), sdl_ticks, window};
  return view;
}

/* ---- ownership (see input.h) ---- */
typedef struct object_entry { const void *object; qa_app_application_input *creator; } object_entry;
typedef struct holder_entry { const void *owner; qa_app_application_input *creator; } holder_entry;
static object_entry *objects; static size_t object_count, object_capacity;
static holder_entry *holders; static size_t holder_count, holder_capacity;

void qa_app_input_internal_own(qa_app_application_input *input, void *object, void (*destroy)(void *object)) {
  input_owned *entry = INPUT_PUSH(input->owned, input->owned_count, input->owned_capacity);
  entry->object = object; entry->destroy = destroy;
}

void qa_app_input_internal_register_object(qa_app_application_input *creator, const void *object) {
  if (object == NULL) return;
  object_entry *entry = INPUT_PUSH(objects, object_count, object_capacity);
  entry->object = object; entry->creator = creator;
}

static qa_app_application_input *creator_of(const void *object) {
  if (object == NULL) return NULL;
  for (size_t i = 0; i < object_count; i++) if (objects[i].object == object) return objects[i].creator;
  return NULL;
}

static void release(qa_app_application_input *input);

void qa_app_input_internal_retain_object(qa_app_application_input *input, const void *object) {
  qa_app_application_input *creator = creator_of(object);
  if (creator == NULL || creator == input) return;
  for (size_t i = 0; i < input->held_count; i++) if (input->held[i] == creator) return;
  *INPUT_PUSH(input->held, input->held_count, input->held_capacity) = creator;
  creator->refs++;
}

void qa_app_input_internal_hold_for(const void *owner, const void *object) {
  qa_app_application_input *creator = creator_of(object);
  if (creator == NULL) return;
  for (size_t i = 0; i < holder_count; i++)
    if (holders[i].owner == owner && holders[i].creator == creator) return;
  holder_entry *entry = INPUT_PUSH(holders, holder_count, holder_capacity);
  entry->owner = owner; entry->creator = creator;
  creator->refs++;
}

void qa_app_input_release_holder(const void *owner) {
  for (;;) {
    size_t index = holder_count;
    for (size_t i = 0; i < holder_count; i++) if (holders[i].owner == owner) { index = i; break; }
    if (index == holder_count) break;
    qa_app_application_input *creator = holders[index].creator;
    memmove(&holders[index], &holders[index + 1], (holder_count - index - 1) * sizeof *holders);
    holder_count--;
    release(creator);
  }
  if (holder_count == 0) { free(holders); holders = NULL; holder_capacity = 0; }
}

static void staged_free(void *record) {
  input_staged_command *command = record;
  free(command->text);
  qa_app_input_internal_free_context(command->source);
}

static void free_ring(qa_event_ring *ring, qa_event_cursor *cursor, void (*dispose)(void *record)) {
  if (ring == NULL) return;
  if (dispose != NULL) {
    size_t count = 0;
    void *records = qa_event_ring_drain_all(ring, &count);
    for (size_t i = 0; i < count; i++) dispose((char *)records + i * ring->elem_size);
    free(records);
  }
  if (cursor != NULL) qa_event_ring_close_cursor(ring, cursor);
  qa_event_ring_destroy(ring);
  free(ring); free(cursor);
}

static void controller_event_dispose(void *record) {
  qa_controller_event *event = record;
  if (event->kind == QA_CONTROLLER_EVENT_CONNECTED || event->kind == QA_CONTROLLER_EVENT_REMAPPED)
    qa_controller_device_dispose(&event->device);
}

static void final_release(qa_app_application_input *input) {
  if (!input->closed) qa_app_application_input_close(input);
  if (input->owns_candidate_program && input->candidate_program != NULL)
    qa_app_prepared_client_commands_destroy(input->candidate_program);
  qa_client_command_bindings_destroy(input->client_commands);
  qa_app_controller_settings_destroy(input->controller_settings);
  qa_input_router_destroy(input->router);
  bool created_devices = creator_of(input->input_devices) == input;
  bool created_controllers = creator_of(input->controllers) == input;
  if (created_devices) qa_app_input_devices_destroy(input->input_devices);
  if (input->has_device_store) qa_config_store_destroy(&input->device_store);
  if (created_controllers) { qa_sdl_controllers_close(input->controllers); free(input->controllers); }
  for (size_t i = 0; i < input->all_local_count; i++) {
    input_local *local = input->all_locals[i];
    if (local->pub.haptics != NULL) qa_seat_haptics_destroy(local->pub.haptics);
    if (local->pub.builder != NULL) qa_input_command_builder_destroy(local->pub.builder);
    free(local->mouse_input);
    if (local->created_console != NULL) { qa_seat_console_destroy(local->created_console); free(local->created_console); }
    free(local);
  }
  free(input->all_locals); free(input->locals);
  for (size_t i = 0; i < input->ui_callback_count; i++) free(input->ui_callbacks[i]);
  free(input->ui_callbacks); free(input->release_ui); free(input->unregister);
  free(input->seat_uis); free(input->mice); free(input->q3);
  for (size_t i = 0; i < input->arsenal_count; i++) { free(input->arsenal[i].provider); free(input->arsenal[i].weapon); }
  free(input->arsenal);
  for (size_t i = 0; i < input->offhand_count; i++) {
    qa_input_button_destroy(&input->offhand[i]->grapple); qa_input_button_destroy(&input->offhand[i]->grenade);
    free(input->offhand[i]);
  }
  free(input->offhand);
  for (size_t i = 0; i < input->candidate_default_count; i++) {
    for (size_t b = 0; b < input->candidate_defaults[i].count; b++)
      qa_input_binding_dispose(&input->candidate_defaults[i].bindings[b]);
    free(input->candidate_defaults[i].bindings);
  }
  free(input->candidate_defaults); free(input->candidate_gamepads);
  free_ring(input->staged, input->staged_cursor, staged_free);
  free_ring(input->window_events, input->window_cursor, NULL);
  free_ring(input->controller_events, input->controller_cursor, controller_event_dispose);
  for (size_t i = input->owned_count; i-- > 0;) input->owned[i].destroy(input->owned[i].object);
  free(input->owned);
  free(input->published); free(input->arsenal_out); free(input->clipboard_text);
  for (size_t i = object_count; i-- > 0;)
    if (objects[i].creator == input) {
      memmove(&objects[i], &objects[i + 1], (object_count - i - 1) * sizeof *objects);
      object_count--;
    }
  if (object_count == 0) { free(objects); objects = NULL; object_capacity = 0; }
  qa_app_application_input **held = input->held;
  size_t held_count = input->held_count;
  free(input);
  for (size_t i = 0; i < held_count; i++) release(held[i]);
  free(held);
}

static void release(qa_app_application_input *input) {
  if (--input->refs == 0) final_release(input);
}

void qa_app_application_input_destroy(qa_app_application_input *input) {
  if (input != NULL) release(input);
}

/* ---- destroy adapters for the owned list ---- */
static void destroy_registry(void *object) { qa_cvar_registry_destroy(object); }
static void destroy_seat_input(void *object) { qa_seat_input_destroy(object); }
static void destroy_buffer(void *object) { qa_command_buffer_destroy(object); }
static void destroy_scripts(void *object) { qa_app_console_script_files_destroy(object); }
static void destroy_routing(void *object) { qa_app_console_routing_destroy(object); }
static void destroy_memory(void *object) { free(object); }

/* ---- shared lookups ---- */
double qa_app_input_internal_now(qa_app_application_input *input) { return input->now(input->now_ctx); }

input_local *qa_app_input_internal_find_local(const qa_app_application_input *input, qa_seat_id seat) {
  for (size_t i = 0; i < input->local_count; i++)
    if (qa_same_seat(qa_session_seat_id(input->locals[i]->pub.player.seat), seat)) return input->locals[i];
  return NULL;
}

input_seat_ui *qa_app_input_internal_seat_ui(const qa_app_application_input *input, qa_seat_id seat) {
  for (size_t i = 0; i < input->seat_ui_count; i++)
    if (qa_same_seat(input->seat_uis[i].seat, seat)) return &input->seat_uis[i];
  return NULL;
}

qa_mouse_settings *qa_app_input_internal_mouse(const qa_app_application_input *input, qa_seat_id seat) {
  for (size_t i = 0; i < input->mouse_count; i++)
    if (qa_same_seat(input->mice[i].seat, seat)) return &input->mice[i].settings;
  return NULL;
}

void qa_app_input_internal_set_mouse(qa_app_application_input *input, qa_seat_id seat, qa_mouse_settings settings) {
  qa_mouse_settings *existing = qa_app_input_internal_mouse(input, seat);
  if (existing != NULL) { *existing = settings; return; }
  input_mouse *entry = INPUT_PUSH(input->mice, input->mouse_count, input->mouse_capacity);
  entry->seat = seat; entry->settings = settings;
}

/* The per-seat Map deletes of retireLocalSeats / prepareLocalSeats. */
void qa_app_input_internal_forget_seat(qa_app_application_input *input, input_local *local) {
  qa_seat_id id = qa_session_seat_id(local->pub.player.seat);
  qa_app_input_internal_remove_ui_callback(input, local->pub.input);
  for (size_t i = input->seat_ui_count; i-- > 0;)
    if (qa_same_seat(input->seat_uis[i].seat, id)) {
      memmove(&input->seat_uis[i], &input->seat_uis[i + 1], (input->seat_ui_count - i - 1) * sizeof *input->seat_uis);
      input->seat_ui_count--;
    }
  for (size_t i = input->mouse_count; i-- > 0;)
    if (qa_same_seat(input->mice[i].seat, id)) {
      memmove(&input->mice[i], &input->mice[i + 1], (input->mouse_count - i - 1) * sizeof *input->mice);
      input->mouse_count--;
    }
  for (size_t i = input->q3_count; i-- > 0;)
    if (qa_same_seat(input->q3[i].seat, id)) {
      memmove(&input->q3[i], &input->q3[i + 1], (input->q3_count - i - 1) * sizeof *input->q3);
      input->q3_count--;
    }
  for (size_t i = input->arsenal_count; i-- > 0;)
    if (qa_same_seat(input->arsenal[i].seat, id)) {
      free(input->arsenal[i].provider); free(input->arsenal[i].weapon);
      memmove(&input->arsenal[i], &input->arsenal[i + 1], (input->arsenal_count - i - 1) * sizeof *input->arsenal);
      input->arsenal_count--;
    }
  for (size_t i = input->offhand_count; i-- > 0;)
    if (qa_same_seat(input->offhand[i]->seat, id)) {
      qa_input_button_destroy(&input->offhand[i]->grapple); qa_input_button_destroy(&input->offhand[i]->grenade);
      free(input->offhand[i]);
      memmove(&input->offhand[i], &input->offhand[i + 1], (input->offhand_count - i - 1) * sizeof *input->offhand);
      input->offhand_count--;
    }
}

void qa_app_input_internal_set_published(qa_app_application_input *input, qa_cvar_registry *from, qa_cvar_registry *to) {
  for (size_t i = 0; i < input->published_count; i++)
    if (input->published[i].from == from) { input->published[i].to = to; return; }
  input_registry_pair *pair = INPUT_PUSH(input->published, input->published_count, input->published_capacity);
  pair->from = from; pair->to = to;
}

qa_cvar_registry *qa_app_input_internal_console_seat(qa_app_application_input *input, qa_seat_id seat) {
  return input->actions.console != NULL ? input->actions.console->seat(input->actions.console->ctx, seat) : NULL;
}

/* consoleRouting ?? externalRouting; false when neither exists. */
bool qa_app_input_internal_routing(qa_app_application_input *input, qa_command_cvar_routing *out) {
  if (input->console_routing != NULL) { *out = qa_app_console_routing_view(input->console_routing); return true; }
  if (input->has_external_routing) { *out = input->external_routing; return true; }
  return false;
}

/* actions.console?.dialect() ?? consoleCvars.dialect */
qa_command_dialect qa_app_input_internal_source_dialect(qa_app_application_input *input) {
  return input->actions.console != NULL ? input->actions.console->dialect(input->actions.console->ctx)
      : qa_cvar_registry_dialect(input->console_cvars);
}

qa_command_context qa_app_input_internal_seat_context(qa_session_id session, const qa_app_local_player *player) {
  qa_command_context context;
  memset(&context, 0, sizeof context);
  context.session = session;
  context.origin.kind = QA_COMMAND_ORIGIN_LOCAL_SEAT;
  context.origin.seat = qa_session_seat_id(player->seat);
  context.origin.client = qa_session_client_id(qa_session_seat_client(player->seat));
  return context;
}

/* while (origin.kind === "script") origin = origin.caller */
const qa_command_origin *qa_app_input_internal_unwind(const qa_command_origin *origin) {
  while (origin->kind == QA_COMMAND_ORIGIN_SCRIPT) origin = origin->caller;
  return origin;
}

void qa_app_input_internal_execute(qa_app_application_input *input, const char *name, const char *const *args,
    size_t count, const qa_seat_id *seat, const qa_command_context *source) {
  input->actions.execute(input->actions.ctx, name, args, count, seat, source);
}


/* Registries ApplicationInput creates print through (text, source?) =>
 * this.print(text, source) (a registry passes no source), or, for
 * createJoinedLocal's mouse registry, text => this.print(text, context). */
typedef struct registry_print { qa_app_application_input *input; qa_command_context context; } registry_print;
static void registry_print_fn(void *ctx, const char *text) {
  qa_app_input_internal_print_source(ctx, text, NULL);
}
static void registry_print_context(void *ctx, const char *text) {
  registry_print *print = ctx;
  qa_app_input_internal_print_source(print->input, text, &print->context);
}

qa_cvar_registry *qa_app_input_internal_create_registry(qa_app_application_input *input, qa_command_dialect dialect,
    qa_command_context context, bool print_in_context) {
  qa_cvar_registry_options options;
  memset(&options, 0, sizeof options);
  options.dialect = dialect; options.context = context;
  options.print = registry_print_fn; options.print_ctx = input;
  if (print_in_context) {
    registry_print *print = xcalloc(1, sizeof *print);
    print->input = input; print->context = context;
    qa_app_input_internal_own(input, print, destroy_memory);
    options.print = registry_print_context; options.print_ctx = print;
  }
  qa_cvar_registry *registry = qa_cvar_registry_create(&options);
  if (registry == NULL) qa_range_error("Out of memory");
  qa_app_input_internal_own(input, registry, destroy_registry);
  qa_app_input_internal_register_object(input, registry);
  return registry;
}

/* ---- console, haptics and UI callback construction ---- */
typedef struct console_ctx {
  qa_app_application_input *input;
  qa_seat_id seat;
  qa_seat_input *seat_input;
  input_local *local; /* joined: the captured record (haptics) */
  bool joined;
} console_ctx;

static double console_now(void *ctx) { console_ctx *console = ctx; return qa_app_input_internal_now(console->input); }
static bool console_connected(void *ctx) { (void)ctx; return true; }
static const char *console_clipboard(void *ctx) {
  console_ctx *console = ctx;
  size_t length = 0;
  uint8_t *bytes = qa_read_sdl_clipboard(&length);
  if (bytes == NULL) return NULL;
  char *text = malloc(length + 1);
  if (text == NULL) qa_range_error("Out of memory");
  memcpy(text, bytes, length); text[length] = '\0';
  free(bytes);
  free(console->input->clipboard_text);
  console->input->clipboard_text = text;
  return text;
}
static void console_focus(void *ctx, qa_seat_input_focus focus) {
  console_ctx *console = ctx;
  qa_seat_input_set_focus(console->seat_input, focus, qa_app_input_internal_now(console->input));
  bool active = qa_seat_input_focused(console->seat_input) && strcmp(focus.kind, "game") == 0;
  /* TS createSeatConsole: the seat's own haptics, captured at construction. */
  if (console->local->pub.haptics != NULL) qa_seat_haptics_set_active(console->local->pub.haptics, active);
}
static void console_chat(void *ctx, const char *text, bool team, bool has_target, double target) {
  console_ctx *console = ctx;
  const char *args[2] = {text, NULL};
  char *number = NULL;
  if (has_target) { number = qa_app_input_internal_dup(QA_JS_NUMBER(target)); args[1] = number; }
  qa_app_input_internal_execute(console->input, team ? "say_team" : "say", args, has_target ? 2 : 1, &console->seat, NULL);
  free(number);
}

qa_seat_console *qa_app_input_internal_create_console(qa_app_application_input *input, qa_seat_id seat,
    qa_command_dialect dialect, qa_command_context context, qa_seat_input *seat_input, input_local *local, bool joined) {
  console_ctx *ctx = xcalloc(1, sizeof *ctx);
  ctx->input = input; ctx->seat = seat; ctx->seat_input = seat_input; ctx->local = local; ctx->joined = joined;
  qa_app_input_internal_own(input, ctx, destroy_memory);
  qa_seat_console_options options;
  memset(&options, 0, sizeof options);
  options.staged = true; options.seat = seat; options.dialect = dialect; options.context = context;
  options.commands = input->commands; options.cvars = input->console_cvars;
  options.now = console_now; options.now_ctx = ctx;
  options.connected = console_connected; options.connected_ctx = ctx;
  options.clipboard = console_clipboard; options.clipboard_ctx = ctx;
  options.focus = console_focus; options.focus_ctx = ctx;
  options.chat = console_chat; options.chat_ctx = ctx;
  qa_seat_console *console = xcalloc(1, sizeof *console);
  if (!qa_seat_console_init(console, &options)) qa_range_error("Out of memory");
  qa_app_input_internal_register_object(input, console);
  qa_app_input_internal_retain_object(input, input->commands);
  qa_app_input_internal_retain_object(input, input->console_cvars);
  return console;
}

static bool haptics_controller(void *ctx, qa_seat_id seat, double *out) {
  qa_app_application_input *input = ctx;
  return qa_input_router_controller_for(input->router, seat, out);
}
static bool haptics_load(void *ctx, qa_resource_request request, uint8_t **out_bytes, size_t *out_length) {
  qa_app_application_input *input = ctx;
  if (input->haptic_load == NULL) return false; /* TS default: async () => null */
  return input->haptic_load(input->haptic_load_ctx, request, out_bytes, out_length);
}
static double haptics_now(void *ctx) { return qa_app_input_internal_now(ctx); }

qa_seat_haptics *qa_app_input_internal_create_haptics(qa_app_application_input *input, qa_seat_id seat) {
  qa_seat_haptics_options options;
  memset(&options, 0, sizeof options);
  options.seat = seat;
  /* TS rumble closure reads this.controllers, assigned after the locals. */
  options.controllers = input->controllers;
  options.controller = haptics_controller; options.controller_ctx = input;
  options.load = haptics_load; options.load_ctx = input;
  options.now = haptics_now; options.now_ctx = input;
  qa_seat_haptics *haptics = qa_seat_haptics_create(options);
  if (haptics == NULL) qa_range_error("Out of memory");
  return haptics;
}

input_ui_callback *qa_app_input_internal_add_ui_callback(qa_app_application_input *input, qa_seat_input *seat_input,
    qa_seat_id seat, input_local *local, qa_seat_console *captured, bool joined) {
  for (size_t i = 0; i < input->ui_callback_count; i++)
    if (input->ui_callbacks[i]->input == seat_input) {
      input_ui_callback *existing = input->ui_callbacks[i];
      existing->seat = seat; existing->local = local; existing->captured_console = captured; existing->joined = joined;
      return existing;
    }
  input_ui_callback *callback = xcalloc(1, sizeof *callback);
  callback->owner = input; callback->input = seat_input; callback->seat = seat;
  callback->local = local; callback->captured_console = captured; callback->joined = joined;
  *INPUT_PUSH(input->ui_callbacks, input->ui_callback_count, input->ui_callback_capacity) = callback;
  return callback;
}

void qa_app_input_internal_remove_ui_callback(qa_app_application_input *input, qa_seat_input *seat_input) {
  for (size_t i = 0; i < input->ui_callback_count; i++)
    if (input->ui_callbacks[i]->input == seat_input) {
      free(input->ui_callbacks[i]);
      memmove(&input->ui_callbacks[i], &input->ui_callbacks[i + 1],
              (input->ui_callback_count - i - 1) * sizeof *input->ui_callbacks);
      input->ui_callback_count--;
      return;
    }
}

/* TS SeatInput option uiEvent: () => false (bindUiEvent replaces it). */
static bool no_ui_event(void *ctx, const qa_seat_input_event *event, qa_seat_input_focus focus) {
  (void)ctx; (void)event; (void)focus;
  return false;
}

static qa_seat_input *create_seat_input(qa_app_application_input *input, qa_seat_id seat, qa_command_context context) {
  qa_seat_input_options options;
  memset(&options, 0, sizeof options);
  options.seat = seat; options.dialect = input->dialect; options.context = context;
  options.commands = input->commands;
  options.ui_event = no_ui_event; options.ui_event_ctx = NULL;
  qa_seat_input *created = qa_seat_input_create(&options);
  if (created == NULL) qa_range_error("Out of memory");
  qa_app_input_internal_own(input, created, destroy_seat_input);
  qa_app_input_internal_register_object(input, created);
  qa_app_input_internal_retain_object(input, input->commands);
  return created;
}

/* A stable qa_mouse_settings (the tuning store's ctx). */
static qa_mouse_tuning_store stable_store(qa_app_application_input *input, qa_mouse_settings settings) {
  qa_mouse_settings *stable = xcalloc(1, sizeof *stable);
  *stable = settings;
  qa_app_input_internal_own(input, stable, destroy_memory);
  return qa_mouse_settings_store(stable);
}

static qa_input_command_builder *create_builder(qa_app_application_input *input, input_local *local,
    qa_mouse_settings settings, qa_actor_id actor) {
  local->mouse_input = xcalloc(1, sizeof *local->mouse_input);
  qa_mouse_input_init_store(local->mouse_input, stable_store(input, settings));
  qa_input_command_builder *builder = qa_input_command_builder_create(input->dialect, local->mouse_input, NULL);
  if (builder == NULL) qa_range_error("Out of memory");
  qa_app_sim_player_view view = input->simulation->player_view(input->simulation->ctx, actor);
  qa_input_command_builder_set_view_angles(builder, view.angles);
  return builder;
}

static input_local *new_local(qa_app_application_input *input) {
  input_local *local = xcalloc(1, sizeof *local);
  *INPUT_PUSH(input->all_locals, input->all_local_count, input->all_local_capacity) = local;
  return local;
}

/* defaultBindings(0, dialect, actions.bindingItems?.(seat) ?? []). */
static qa_input_binding *defaults_for(qa_app_application_input *input, qa_seat_id seat, size_t *out_count) {
  size_t item_count = 0;
  const qa_weapon_binding_item *items = input->actions.binding_items != NULL
      ? input->actions.binding_items(input->actions.ctx, seat, &item_count) : NULL;
  qa_input_binding *defaults = qa_default_bindings(0, input->dialect, items, item_count, out_count);
  if (defaults == NULL) qa_range_error("Out of memory");
  return defaults;
}

static void free_bindings(qa_input_binding *bindings, size_t count) {
  for (size_t i = 0; i < count; i++) qa_input_binding_dispose(&bindings[i]);
  free(bindings);
}

static void bind_all(qa_seat_input *seat_input, const qa_input_binding *bindings, size_t count) {
  for (size_t i = 0; i < count; i++) if (!qa_seat_input_bind(seat_input, &bindings[i])) qa_range_error("Out of memory");
}

/* ---- readSettings ---- */
typedef struct loaded_settings {
  qa_seat_settings *saved; bool *has_saved; size_t count;
  bool has_routing; bool routing_has_seat; double routing_seat;
  qa_cvar_archive_entry *movement; size_t movement_count;
  qa_cvar_archive_entry *fallback; size_t fallback_count;
  qa_cvar_archive_entry **input; size_t *input_counts;
} loaded_settings;

static void loaded_free(loaded_settings *loaded) {
  for (size_t i = 0; i < loaded->count; i++) {
    if (loaded->has_saved[i]) qa_seat_settings_free(&loaded->saved[i]);
    qa_app_cvar_archive_entries_free(loaded->input[i], loaded->input_counts[i]);
  }
  free(loaded->saved); free(loaded->has_saved); free(loaded->input); free(loaded->input_counts);
  qa_app_cvar_archive_entries_free(loaded->movement, loaded->movement_count);
  qa_app_cvar_archive_entries_free(loaded->fallback, loaded->fallback_count);
  memset(loaded, 0, sizeof *loaded);
}

static bool previous_has_seat(const qa_app_application_input *previous, qa_seat_id seat) {
  return previous != NULL && qa_app_input_internal_find_local(previous, seat) != NULL;
}

static const qa_app_prepared_seat *prepared_seat(const qa_app_prepared_startup *startup, qa_seat_id seat) {
  if (startup == NULL) return NULL;
  size_t count = 0;
  const qa_app_prepared_seat *seats = qa_app_prepared_startup_seats(startup, &count);
  for (size_t i = 0; i < count; i++) if (qa_same_seat(seats[i].id, seat)) return &seats[i];
  return NULL;
}

static void read_settings(loaded_settings *out, qa_session_seat *const *seats, size_t seat_count,
    qa_command_dialect dialect, const qa_app_input_commands *actions, const qa_config_store *settings,
    const qa_app_input_command_owner *owner, const qa_app_application_input *previous,
    const qa_app_prepared_startup *prepared) {
  memset(out, 0, sizeof *out);
  out->count = seat_count;
  out->saved = xcalloc(seat_count, sizeof *out->saved);
  out->has_saved = xcalloc(seat_count, sizeof *out->has_saved);
  out->input = xcalloc(seat_count, sizeof *out->input);
  out->input_counts = xcalloc(seat_count, sizeof *out->input_counts);
  for (size_t i = 0; i < seat_count; i++) {
    char name[64];
    snprintf(name, sizeof name, "input/seat-%lld.json", (long long)(qa_session_seat_id(seats[i]).index + 1));
    out->has_saved[i] = qa_config_store_load_seat(settings, name, &out->saved[i]);
  }
  double keyboard = 0; bool has_keyboard = false;
  out->has_routing = qa_config_store_load_input_routing(settings, "input/routing.json", &keyboard, &has_keyboard);
  out->routing_has_seat = has_keyboard; out->routing_seat = keyboard;
  qa_command_dialect source = actions->console != NULL ? actions->console->dialect(actions->console->ctx)
      : owner != NULL ? qa_cvar_registry_dialect(owner->cvars)
      : previous != NULL ? qa_cvar_registry_dialect(previous->console_cvars) : dialect;
  bool has_owners = owner != NULL || previous != NULL || prepared != NULL;
  if (!has_owners) {
    const char *movement[] = {"movement", qa_app_configuration_dialect_name(dialect)};
    out->movement = qa_app_load_cvar_archive(settings, movement, 2, dialect, &out->movement_count);
    const char *fallback[] = {"fallback", qa_app_configuration_dialect_name(source)};
    out->fallback = qa_app_load_cvar_archive(settings, fallback, 2, source, &out->fallback_count);
  }
  for (size_t i = 0; i < seat_count; i++) {
    qa_seat_id id = qa_session_seat_id(seats[i]);
    if ((owner != NULL && owner->input_settings != NULL) || previous_has_seat(previous, id) || prepared_seat(prepared, id) != NULL)
      continue;
    char index[32];
    snprintf(index, sizeof index, "%s", QA_JS_NUMBER((double)id.index));
    const char *path[] = {"input", qa_app_configuration_dialect_name(source), index};
    out->input[i] = qa_app_load_cvar_archive(settings, path, 3, source, &out->input_counts[i]);
  }
}

/* ---- constructor helpers ---- */
static bool context_matches_player(const qa_command_context *context, const qa_app_local_player *player) {
  return context->origin.kind == QA_COMMAND_ORIGIN_LOCAL_SEAT
      && qa_same_client(context->origin.client, qa_session_client_id(qa_session_seat_client(player->seat)));
}

static void restore_from(qa_cvar_registry *target, qa_cvar_registry *source) {
  qa_save_value *state = qa_cvar_registry_capture_save_state(source);
  if (state == NULL) qa_range_error("Out of memory");
  qa_cvar_registry_restore_save_state(target, state);
  qa_save_value_free(state);
}

/* ---- console routing host ---- */
static qa_command_dialect routing_source_dialect(void *ctx) {
  qa_app_application_input *input = ctx;
  return input->actions.console != NULL ? input->actions.console->dialect(input->actions.console->ctx)
      : input->open_source_dialect;
}
static bool routing_server(void *ctx, qa_app_console_server *out) {
  qa_app_application_input *input = ctx;
  return input->actions.console != NULL && input->actions.console->server(input->actions.console->ctx, out);
}
static qa_cvar_registry *routing_seat(void *ctx, qa_seat_id id) {
  return qa_app_input_internal_console_seat(ctx, id);
}
static qa_cvar_registry *routing_input(void *ctx, bool has_seat, qa_seat_id id) {
  return qa_app_application_input_input_cvars(ctx, has_seat, id);
}
static qa_cvar_registry *routing_movement(void *ctx) { return ((qa_app_application_input *)ctx)->cvar_owner; }
static qa_cvar_registry *routing_shared(void *ctx) { return ((qa_app_application_input *)ctx)->shared_owner; }

/* ---- command buffer host ---- */
static qa_script_read_result buffer_read_script(void *ctx, const char *name, qa_command_context source) {
  qa_app_application_input *input = ctx;
  qa_script_read_result result;
  memset(&result, 0, sizeof result);
  result.kind = QA_SCRIPT_READ_READY;
  /* this.startup?.readScript(...) ?? this.scripts.read(...): no startup owns this buffer. */
  result.text = qa_app_console_script_files_read(input->script_owner, name, source);
  result.has_text = result.text != NULL;
  return result;
}

static void forward_with_note(void *ctx, qa_command_invocation *invocation) {
  qa_app_application_input *input = ctx;
  if (invocation->argc == 0) return;
  if (input->startup != NULL) qa_app_prepared_startup_note_world_action(input->startup);
  qa_app_input_internal_forward_to_server(ctx, invocation);
}

static qa_command_buffer *create_buffer(qa_app_application_input *input, qa_command_dialect source_dialect,
    qa_command_context context) {
  qa_command_buffer_options options;
  memset(&options, 0, sizeof options);
  options.dialect = source_dialect; options.context = context; options.cvars = input->console_cvars;
  options.read_script = buffer_read_script; options.read_script_ctx = input;
  if (input->console_routing != NULL) options.cvar_routing = qa_app_console_routing_view(input->console_routing);
  options.print = qa_app_input_internal_print_source; options.print_ctx = input;
  options.forward_to_server = forward_with_note; options.forward_to_server_ctx = input;
  options.builtins = true; options.builtins_set = true;
  qa_command_buffer *buffer = qa_command_buffer_create(&options);
  if (buffer == NULL) qa_range_error("Out of memory");
  qa_app_input_internal_own(input, buffer, destroy_buffer);
  qa_app_input_internal_register_object(input, buffer);
  return buffer;
}

/* ---- router host ---- */
static double router_now(void *ctx) { return qa_app_input_internal_now(ctx); }
static double router_ticks(void *ctx) {
  qa_app_application_input *input = ctx;
  return (double)input->window.ticks(input->window.input.ctx);
}
static void router_unhandled(void *ctx, const qa_input_unhandled_event *event) {
  qa_app_application_input *input = ctx;
  if (event->is_controller && event->controller->kind == QA_CONTROLLER_EVENT_ASSIGNMENT) {
    int64_t slot = event->controller->assignment_slot;
    if (slot >= 0 && (size_t)slot < input->local_count) (void)qa_seat_haptics_cancel(input->locals[slot]->pub.haptics);
  }
  if (!event->is_controller && (event->sdl->kind == QA_SDL_EVENT_QUIT
      || (event->sdl->kind == QA_SDL_EVENT_WINDOW && event->sdl->as.window.event == 14)))
    input->actions.quit(input->actions.ctx);
}

static void client_command_execute(void *ctx, qa_command_invocation *command, qa_seat_id seat) {
  qa_app_application_input *input = ctx;
  const char *name = command->argc > 0 ? command->argv[0] : "";
  qa_app_input_internal_execute(input, name, command->argc > 1 ? (const char *const *)command->argv + 1 : NULL,
                                command->argc > 1 ? command->argc - 1 : 0, &seat, &command->source);
}

static qa_controller_device *settings_devices(void *ctx, size_t *out_count) {
  qa_app_application_input *input = ctx;
  qa_controller_device *devices = qa_sdl_controllers_devices(input->controllers, out_count);
  if (devices == NULL) *out_count = 0;
  return devices;
}

static void report_print(void *ctx, const char *text) {
  qa_app_application_input *input = ctx;
  input->actions.print(input->actions.ctx, text);
}
static void device_print(void *ctx, const char *text) { report_print(ctx, text); }

static qa_controller_selection selection_from_settings(const qa_settings_controller_selection *saved) {
  qa_controller_selection selection;
  memset(&selection, 0, sizeof selection);
  selection.kind = saved->kind == QA_CONTROLLER_NONE ? QA_CONTROLLER_SELECTION_NONE
      : saved->kind == QA_CONTROLLER_DEVICE ? QA_CONTROLLER_SELECTION_DEVICE
      : saved->kind == QA_CONTROLLER_SERIAL ? QA_CONTROLLER_SELECTION_SERIAL : QA_CONTROLLER_SELECTION_AUTOMATIC;
  memcpy(selection.guid, saved->guid, sizeof selection.guid);
  selection.ordinal = saved->ordinal;
  selection.serial = saved->serial;
  return selection;
}

static qa_event_ring *new_ring(size_t capacity, size_t size, qa_event_cursor **cursor) {
  qa_event_ring *ring = xcalloc(1, sizeof *ring);
  if (!qa_event_ring_init(ring, capacity, size)) qa_range_error("Out of memory");
  *cursor = xcalloc(1, sizeof **cursor);
  if (!qa_event_ring_open_cursor(ring, *cursor, false)) qa_range_error("Out of memory");
  return ring;
}

/* configuration?.forwardCommands(request => actions.execute(...)). */
static void configuration_forward(void *ctx, const qa_app_configuration_command_request *request) {
  qa_app_application_input *input = ctx;
  qa_app_input_internal_execute(input, request->name, request->arguments, request->argument_count,
                                request->has_seat ? &request->seat : NULL, &request->source);
}

/* ---- constructor ---- */
static qa_app_application_input *construct(qa_cvar_registry *const *staging, size_t staging_count, bool has_staging,
    const qa_app_input_open_args *args, const loaded_settings *loaded, qa_app_application_input *previous,
    qa_app_prepared_startup *prepared, qa_app_client_bootstrap *client) {
  /* The TS constructor's argument checks, before anything is owned. */
  if (args->player_count == 0) qa_input_error("Native input requires at least one local player");
  if (args->owner != NULL) {
    const qa_app_input_command_owner *check = args->owner;
    qa_cvar_registry *movement = check->movement != NULL ? check->movement : check->cvars;
    qa_cvar_registry *fallback = check->fallback != NULL ? check->fallback : check->cvars;
    if (qa_cvar_registry_dialect(movement) != args->dialect
        || qa_cvar_registry_dialect(fallback) != qa_cvar_registry_dialect(check->cvars) || args->actions.console != NULL)
      qa_input_error("Input command owner does not match its movement and console dialects");
  }
  qa_app_application_input *input = xcalloc(1, sizeof *input);
  input->refs = 1;
  qa_app_input_internal_register_object(input, input); /* holds on the input itself */
  input->window = args->window;
  input->options = args->options; input->dialect = args->dialect;
  input->simulation = args->simulation; input->actions = args->actions;
  input->now = args->now; input->now_ctx = args->now_ctx;
  input->settings = args->settings;
  const qa_app_input_commands *actions = &input->actions;
  const qa_app_input_command_owner *owner = args->owner;
  qa_command_dialect dialect = args->dialect;
  input->startup = prepared != NULL ? prepared : previous != NULL ? previous->startup : NULL;
  qa_app_prepared_profile *configuration = actions->configuration;
  if (owner != NULL) { input->has_external_routing = true; input->external_routing = owner->routing; }
  input->shared_owner = actions->shared_cvars;
  input->owns_controllers = client == NULL && previous == NULL;
  input->staged = new_ring(16384, sizeof(input_staged_command), &input->staged_cursor);
  input->window_events = new_ring(32768, sizeof(qa_sdl_event), &input->window_cursor);
  input->controller_events = new_ring(8192, sizeof(qa_controller_event), &input->controller_cursor);
  const qa_app_local_player *first = &args->players[0];
  qa_command_context context;
  memset(&context, 0, sizeof context);
  context.session = first->actor.session;
  context.origin.kind = QA_COMMAND_ORIGIN_LOCAL_CONSOLE;
  qa_command_dialect source_dialect = actions->console != NULL ? actions->console->dialect(actions->console->ctx)
      : owner != NULL ? qa_cvar_registry_dialect(owner->cvars) : dialect;
  input->open_source_dialect = source_dialect;
  bool fresh_open = !has_staging && previous == NULL;
  qa_cvar_registry *startup_movement = input->startup != NULL ? qa_app_prepared_startup_movement(input->startup) : NULL;
  qa_cvar_registry *startup_fallback = input->startup != NULL ? qa_app_prepared_startup_fallback(input->startup) : NULL;
  input->cvar_owner = configuration != NULL ? qa_app_prepared_profile_movement(configuration)
      : owner != NULL && owner->movement != NULL ? owner->movement
      : fresh_open && startup_movement != NULL && qa_cvar_registry_dialect(startup_movement) == dialect ? startup_movement
      : owner != NULL ? owner->cvars : NULL;
  if (input->cvar_owner == NULL) input->cvar_owner = qa_app_input_internal_create_registry(input, dialect, context, false);
  qa_cvar_registry *console_cvars = configuration != NULL ? qa_app_prepared_profile_fallback(configuration)
      : owner != NULL && owner->fallback != NULL ? owner->fallback
      : owner != NULL ? owner->cvars
      : fresh_open && startup_fallback != NULL && qa_cvar_registry_dialect(startup_fallback) == source_dialect ? startup_fallback
      : NULL;
  if (console_cvars == NULL)
    console_cvars = source_dialect == dialect ? input->cvar_owner : qa_app_input_internal_create_registry(input, source_dialect, context, false);
  input->console_cvars = console_cvars;
  qa_app_input_internal_retain_object(input, input->cvar_owner);
  qa_app_input_internal_retain_object(input, console_cvars);
  if (owner == NULL && configuration == NULL) {
    if (startup_movement != input->cvar_owner) {
      if (previous != NULL && qa_cvar_registry_dialect(previous->cvar_owner) == dialect) restore_from(input->cvar_owner, previous->cvar_owner);
      else if (startup_movement != NULL && qa_cvar_registry_dialect(startup_movement) == dialect) restore_from(input->cvar_owner, startup_movement);
      else qa_cvar_registry_apply_archive(input->cvar_owner, loaded->movement, loaded->movement_count);
    }
    if (startup_fallback != console_cvars) {
      if (console_cvars != input->cvar_owner) {
        if (previous != NULL && qa_cvar_registry_dialect(previous->console_cvars) == source_dialect)
          restore_from(console_cvars, previous->console_cvars);
        else if (startup_fallback != NULL && qa_cvar_registry_dialect(startup_fallback) == source_dialect)
          restore_from(console_cvars, startup_fallback);
        else qa_cvar_registry_apply_archive(console_cvars, loaded->fallback, loaded->fallback_count);
      } else qa_cvar_registry_apply_archive(console_cvars, loaded->fallback, loaded->fallback_count);
    }
  }
  if (owner == NULL) {
    qa_app_console_routing_options routing;
    memset(&routing, 0, sizeof routing);
    routing.ctx = input; routing.fallback = console_cvars;
    routing.source_dialect = routing_source_dialect; routing.server = routing_server; routing.seat = routing_seat;
    routing.input = routing_input; routing.movement = routing_movement; routing.shared = routing_shared;
    input->console_routing = qa_app_console_routing_create(routing);
    if (input->console_routing == NULL) qa_range_error("Out of memory");
    qa_app_input_internal_own(input, input->console_routing, destroy_routing);
  }
  input->script_owner = configuration != NULL ? qa_app_prepared_profile_scripts(configuration)
      : actions->scripts != NULL ? actions->scripts
      : prepared != NULL ? qa_app_prepared_startup_scripts(prepared)
      : owner != NULL ? owner->scripts : NULL;
  if (input->script_owner == NULL) {
    char *root = qa_app_console_config_root(args->options->user_content_root);
    if (root == NULL) qa_range_error("Out of memory");
    qa_app_input_internal_own(input, root, destroy_memory);
    qa_app_console_script_files_options files;
    memset(&files, 0, sizeof files);
    files.console_root = root; files.settings = args->settings;
    files.has_mounted = actions->has_read_script; files.mounted = actions->read_script;
    files.has_mounted_script = actions->has_read_mounted_script; files.mounted_script = actions->read_mounted_script;
    input->script_owner = qa_app_console_script_files_create(files);
    if (input->script_owner == NULL) qa_range_error("Out of memory");
    qa_app_input_internal_own(input, input->script_owner, destroy_scripts);
    qa_app_input_internal_register_object(input, input->script_owner);
  }
  input->commands = input->startup != NULL ? qa_app_prepared_startup_commands(input->startup)
      : owner != NULL ? owner->commands : NULL;
  if (input->commands == NULL) input->commands = create_buffer(input, source_dialect, context);
  qa_app_input_internal_retain_object(input, input->commands);

  /* locals */
  size_t player_count = args->player_count;
  qa_seat_input **fresh = xcalloc(player_count, sizeof *fresh);
  size_t fresh_count = 0;
  for (size_t p = 0; p < player_count; p++) {
    const qa_app_local_player *player = &args->players[p];
    qa_seat_id seat = qa_session_seat_id(player->seat);
    qa_command_context seat_context = qa_app_input_internal_seat_context(context.session, player);
    input_local *local = new_local(input);
    local->pub.player = *player;
    const qa_app_prepared_seat *prepared_row = NULL;
    if (input->startup != NULL) {
      size_t count = 0;
      const qa_app_prepared_seat *seats = qa_app_prepared_startup_seats(input->startup, &count);
      for (size_t i = 0; i < count && prepared_row == NULL; i++)
        if (qa_same_seat(seats[i].id, seat) && context_matches_player(&seats[i].context, player)) prepared_row = &seats[i];
    }
    const qa_app_prepared_seat_configuration *configured = NULL;
    qa_seat_input *configured_input = NULL;
    if (configuration != NULL) {
      size_t count = 0;
      const qa_app_prepared_seat_configuration *seats = qa_app_prepared_profile_seats(configuration, &count);
      for (size_t i = 0; i < count && configured == NULL; i++)
        if (qa_same_seat(seats[i].id, seat) && context_matches_player(&seats[i].context, player)) {
          configured = &seats[i];
          configured_input = qa_app_prepared_profile_seat_input(configuration, i);
        }
    }
    qa_seat_input *retained = configured_input != NULL ? configured_input
        : prepared_row != NULL ? prepared_row->input : NULL;
    if (retained == NULL && previous != NULL) {
      for (size_t i = 0; i < previous->local_count && retained == NULL; i++) {
        const qa_app_local_player *prior = &previous->locals[i]->pub.player;
        if (qa_same_seat(qa_session_seat_id(prior->seat), seat)
            && qa_same_client(qa_session_client_id(qa_session_seat_client(prior->seat)),
                              qa_session_client_id(qa_session_seat_client(player->seat))))
          retained = previous->locals[i]->pub.input;
      }
    }
    qa_seat_input *seat_input = retained != NULL ? retained : create_seat_input(input, seat, seat_context);
    qa_app_input_internal_retain_object(input, seat_input);
    if (retained == NULL || (configured != NULL && prepared_row == NULL)) fresh[fresh_count++] = seat_input;
    local->pub.input = seat_input;
    (void)qa_app_input_internal_add_ui_callback(input, seat_input, seat, local, NULL, false);
    local->created_console = qa_app_input_internal_create_console(input, seat, source_dialect, seat_context, seat_input, local, false);
    local->pub.console = local->created_console;
    qa_mouse_settings mouse;
    bool has_prepared_mouse = false;
    if (configured != NULL) { mouse = configured->mouse; has_prepared_mouse = true; }
    else if (fresh_open && prepared_row != NULL && prepared_row->mouse.cvars != NULL
             && qa_cvar_registry_dialect(prepared_row->mouse.cvars) == source_dialect) { mouse = prepared_row->mouse; has_prepared_mouse = true; }
    if (!has_prepared_mouse) {
      if (owner != NULL && owner->input_settings != NULL) mouse = *owner->input_settings;
      else qa_mouse_settings_init(&mouse, qa_app_input_internal_create_registry(input, source_dialect, seat_context, false));
    }
    qa_app_input_internal_retain_object(input, mouse.cvars);
    qa_command_context mouse_context = qa_cvar_registry_context(mouse.cvars);
    if (qa_cvar_registry_dialect(mouse.cvars) != source_dialect || mouse_context.session.token != context.session.token
        || mouse_context.origin.kind != QA_COMMAND_ORIGIN_LOCAL_SEAT || !qa_same_seat(mouse_context.origin.seat, seat)
        || !qa_same_client(mouse_context.origin.client, qa_session_client_id(qa_session_seat_client(player->seat)))) {
      free(fresh);
      input->closed = true; /* nothing was activated; TS drops the object */
      qa_app_application_input_destroy(input);
      qa_input_error("Mouse settings belong to another seat or source dialect");
    }
    qa_app_input_internal_set_mouse(input, seat, mouse);
    if ((owner == NULL || owner->input_settings == NULL) && !has_prepared_mouse) {
      qa_cvar_registry *previous_mouse = previous != NULL ? qa_app_application_input_input_cvars(previous, true, seat) : NULL;
      if (previous_mouse != NULL && qa_cvar_registry_dialect(previous_mouse) == source_dialect) restore_from(mouse.cvars, previous_mouse);
      else if (prepared_row != NULL && prepared_row->mouse.cvars != NULL
               && qa_cvar_registry_dialect(prepared_row->mouse.cvars) == source_dialect) restore_from(mouse.cvars, prepared_row->mouse.cvars);
      else qa_cvar_registry_apply_archive(mouse.cvars, loaded->input[input->local_count], loaded->input_counts[input->local_count]);
    }
    local->pub.builder = create_builder(input, local, mouse, player->actor);
    size_t default_count = 0;
    qa_input_binding *defaults = defaults_for(input, seat, &default_count);
    if (configuration != NULL) qa_app_prepared_profile_apply_binding_defaults(configuration, seat, defaults, default_count);
    bool keep_defaults = false;
    if (configuration == NULL && (previous == NULL || retained == NULL)) {
      if (retained == NULL) bind_all(seat_input, defaults, default_count);
      else if (has_staging && prepared_row != NULL) {
        input_candidate_defaults *entry = NULL;
        for (size_t i = 0; i < input->candidate_default_count; i++)
          if (qa_same_seat(input->candidate_defaults[i].seat, seat)) entry = &input->candidate_defaults[i];
        if (entry == NULL) entry = INPUT_PUSH(input->candidate_defaults, input->candidate_default_count, input->candidate_default_capacity);
        else free_bindings(entry->bindings, entry->count);
        entry->seat = seat; entry->bindings = defaults; entry->count = default_count;
        keep_defaults = true;
      } else if (input->startup != NULL) {
        size_t count = 0;
        qa_input_binding *chosen = qa_app_prepared_startup_bindings(input->startup, seat, defaults, default_count, &count);
        bind_all(seat_input, chosen, count);
        free_bindings(chosen, count); /* deep copies (prepared_startup.h) */
      } else bind_all(seat_input, defaults, default_count);
    }
    if (!keep_defaults) free_bindings(defaults, default_count);
    /* SeatHaptics is built once this.controllers exists (below). */
    *INPUT_PUSH(input->locals, input->local_count, input->local_capacity) = local;
  }
  for (size_t index = 0; index < input->local_count; index++) {
    input_local *local = input->locals[index];
    if (index >= loaded->count || !loaded->has_saved[index]) continue;
    const qa_seat_settings *profile = &loaded->saved[index];
    bool is_fresh = false;
    for (size_t i = 0; i < fresh_count; i++) if (fresh[i] == local->pub.input) is_fresh = true;
    if (configuration == NULL && ((input->startup == NULL && previous == NULL) || is_fresh)) {
      qa_seat_input_unbind_all(local->pub.input);
      bind_all(local->pub.input, profile->bindings, profile->binding_count);
    }
    if (previous == NULL || is_fresh) {
      if (has_staging && input->startup != NULL && !is_fresh) {
        if (client == NULL) {
          input_candidate_gamepad *entry = NULL;
          for (size_t i = 0; i < input->candidate_gamepad_count; i++)
            if (input->candidate_gamepads[i].input == local->pub.input) entry = &input->candidate_gamepads[i];
          if (entry == NULL) entry = INPUT_PUSH(input->candidate_gamepads, input->candidate_gamepad_count, input->candidate_gamepad_capacity);
          entry->input = local->pub.input; entry->tuning = profile->gamepad;
        }
      } else qa_seat_input_gamepad(local->pub.input)->tuning = profile->gamepad;
    }
    if (input->startup == NULL && profile->has_always_run) {
      qa_view_input_tuning tuning = qa_input_command_builder_tuning(local->pub.builder);
      tuning.always_run = profile->always_run;
      qa_input_command_builder_set_tuning(local->pub.builder, tuning);
    }
    if (configuration == NULL && (owner == NULL || owner->input_settings == NULL) && (input->startup == NULL || is_fresh))
      qa_mouse_input_set_tuning(qa_input_command_builder_mouse(local->pub.builder), profile->mouse);
    qa_console_history_replace(&local->pub.console->history, (const char *const *)profile->history, profile->history_count);
    local->has_profile_rumble = true;
    local->profile_rumble = profile->rumble;
    local->profile_rumble_strength = profile->rumble_strength;
  }
  if (previous != NULL && previous->startup != NULL && qa_app_prepared_startup_pending(previous->startup)) {
    for (size_t index = 0; index < input->local_count; index++) {
      input_local *local = input->locals[index];
      input_local *prior = qa_app_input_internal_find_local(previous, qa_session_seat_id(local->pub.player.seat));
      if (prior == NULL) continue;
      if (local->pub.input != prior->pub.input) {
        qa_seat_input_unbind_all(local->pub.input);
        size_t count = 0;
        const qa_input_binding *bindings = qa_seat_input_bindings(prior->pub.input, &count);
        bind_all(local->pub.input, bindings, count);
        qa_mouse_input_set_tuning(qa_input_command_builder_mouse(local->pub.builder),
                                  qa_mouse_input_tuning(qa_input_command_builder_mouse(prior->pub.builder)));
      }
      qa_view_input_tuning tuning = qa_input_command_builder_tuning(local->pub.builder);
      tuning.always_run = qa_input_command_builder_tuning(prior->pub.builder).always_run;
      qa_input_command_builder_set_tuning(local->pub.builder, tuning);
    }
  }
  free(fresh);
  if (previous != NULL && input->commands != previous->commands) qa_command_buffer_copy_pending_from(input->commands, previous->commands);
  qa_command_cvar_routing candidate_routing;
  bool has_candidate_routing = qa_app_input_internal_routing(input, &candidate_routing);
  if ((previous != NULL || has_staging) && has_candidate_routing) {
    qa_command_buffer_options command_options;
    memset(&command_options, 0, sizeof command_options);
    command_options.dialect = source_dialect; command_options.context = qa_command_buffer_context(input->commands);
    command_options.cvars = console_cvars; command_options.cvar_routing = candidate_routing;
    command_options.print = qa_app_input_internal_print_source; command_options.print_ctx = input;
    command_options.read_script = buffer_read_script; command_options.read_script_ctx = input;
    command_options.forward_to_server = qa_app_input_internal_forward_to_server; command_options.forward_to_server_ctx = input;
    command_options.builtins = true; command_options.builtins_set = true;
    qa_app_prepared_seat *contexts = xcalloc(input->local_count, sizeof *contexts);
    for (size_t i = 0; i < input->local_count; i++) {
      contexts[i].id = qa_session_seat_id(input->locals[i]->pub.player.seat);
      contexts[i].input = input->locals[i]->pub.input;
      contexts[i].context = qa_app_input_internal_seat_context(context.session, &input->locals[i]->pub.player);
    }
    if (configuration != NULL) {
      input->candidate_program = qa_app_prepared_profile_program(configuration);
      input->owns_candidate_program = false;
    } else if (input->startup != NULL) {
      input->candidate_program = qa_app_prepared_startup_prepare_client_commands(input->startup, &command_options,
          contexts, input->local_count, true);
      input->owns_candidate_program = true;
    } else {
      /* live = staging ?? the registries the old routing exposes to the seats. */
      qa_cvar_registry **live = NULL; size_t live_count = 0, live_capacity = 0;
      if (has_staging) {
        for (size_t i = 0; i < staging_count; i++) *INPUT_PUSH(live, live_count, live_capacity) = staging[i];
      } else {
        qa_command_cvar_routing old;
        bool has_old = previous != NULL && qa_app_input_internal_routing(previous, &old);
        for (size_t i = 0; has_old && i < input->local_count; i++) {
          size_t count = 0;
          qa_cvar_registry *const *visible = old.visible(old.ctx, contexts[i].context, &count);
          for (size_t v = 0; v < count; v++) {
            bool seen = false;
            for (size_t k = 0; k < live_count; k++) if (live[k] == visible[v]) seen = true;
            if (!seen) *INPUT_PUSH(live, live_count, live_capacity) = visible[v];
          }
        }
      }
      input->candidate_program = qa_app_prepare_client_commands(input->commands, contexts, input->local_count,
          live, live_count, &command_options, NULL);
      input->owns_candidate_program = true;
      free(live);
    }
    free(contexts);
    if (input->candidate_program == NULL) qa_range_error("Out of memory");
    if (configuration != NULL) qa_app_prepared_profile_forward_commands(configuration, configuration_forward, input);
    for (size_t i = 0; i < input->candidate_default_count; i++) {
      input_candidate_defaults *entry = &input->candidate_defaults[i];
      qa_binding_command_seat staged;
      if (!qa_app_prepared_client_commands_input(input->candidate_program, entry->seat, &staged))
        qa_input_error("Candidate defaults lost their retained seat");
      size_t count = entry->count;
      qa_input_binding *chosen = input->startup != NULL
          ? qa_app_prepared_startup_preview_bindings(input->startup, entry->seat, entry->bindings, entry->count, &count) : NULL;
      const qa_input_binding *list = chosen != NULL ? chosen : entry->bindings;
      for (size_t b = 0; b < count; b++) if (!staged.bind(staged.ctx, &list[b])) qa_range_error("Out of memory");
      free(chosen);
    }
  }
  /* controllers / devices / router */
  input->controllers = client != NULL ? client->controllers : previous != NULL ? previous->controllers : NULL;
  if (input->controllers == NULL) {
    input->controllers = qa_sdl_controllers_open();
    if (input->controllers == NULL) qa_range_error("Out of memory");
    qa_app_input_internal_register_object(input, input->controllers);
  }
  qa_app_input_internal_retain_object(input, input->controllers);
  for (size_t i = 0; i < input->local_count; i++) {
    /* The TS rumble closure reads this.controllers lazily; the C SeatHaptics
     * takes the owner at creation, so the seats' haptics are built now and
     * receive the profile's setEnabled/setStrength in the TS order. */
    input_local *local = input->locals[i];
    local->pub.haptics = qa_app_input_internal_create_haptics(input, qa_session_seat_id(local->pub.player.seat));
    if (local->has_profile_rumble) {
      (void)qa_seat_haptics_set_enabled(local->pub.haptics, local->profile_rumble);
      (void)qa_seat_haptics_set_strength(local->pub.haptics, local->profile_rumble_strength);
    }
  }
  input->input_devices = client != NULL ? client->input_devices : previous != NULL ? previous->input_devices : NULL;
  if (input->input_devices == NULL) {
    if (!qa_app_input_device_store(args->options->user_content_root, &input->device_store)) qa_range_error("Out of memory");
    input->has_device_store = true;
    input->input_devices = qa_app_input_devices_create(input->shared_owner != NULL ? input->shared_owner : input->cvar_owner,
        &input->device_store, device_print, input, NULL);
    if (input->input_devices == NULL) qa_range_error("Out of memory");
    qa_app_input_internal_register_object(input, input->input_devices);
  }
  qa_app_input_internal_retain_object(input, input->input_devices);
  input->owns_input_devices = client == NULL && previous == NULL;
  if (!input->owns_input_devices) qa_app_input_devices_bind_print(input->input_devices, device_print, input);
  bool has_previous_keyboard = false;
  qa_seat_id previous_keyboard;
  memset(&previous_keyboard, 0, sizeof previous_keyboard);
  if (previous != NULL) has_previous_keyboard = qa_input_router_keyboard_seat(previous->router, &previous_keyboard);
  qa_input_seat_route *routes = xcalloc(input->local_count, sizeof *routes);
  for (size_t index = 0; index < input->local_count; index++) {
    input_local *local = input->locals[index];
    qa_seat_id seat = qa_session_seat_id(local->pub.player.seat);
    routes[index].input = local->pub.input;
    if (previous != NULL && qa_input_router_seat(previous->router, seat) != NULL)
      routes[index].controller = qa_input_router_controller_selection(previous->router, seat);
    else if (index < loaded->count && loaded->has_saved[index])
      routes[index].controller = selection_from_settings(&loaded->saved[index].controller);
    else {
      memset(&routes[index].controller, 0, sizeof routes[index].controller);
      routes[index].controller.kind = input->local_count > 1 && index == 0 ? QA_CONTROLLER_SELECTION_NONE : QA_CONTROLLER_SELECTION_AUTOMATIC;
    }
  }
  qa_input_router_options router;
  memset(&router, 0, sizeof router);
  router.seats = routes; router.seat_count = input->local_count;
  router.defer_platform = previous != NULL || has_staging;
  qa_seat_id first_seat = qa_session_seat_id(first->seat);
  if (previous == NULL) {
    if (!loaded->has_routing) { router.has_keyboard_seat = true; router.keyboard_seat = first_seat; }
    else if (!loaded->routing_has_seat) router.has_keyboard_seat = false;
    else {
      router.has_keyboard_seat = true; router.keyboard_seat = first_seat;
      for (size_t i = 0; i < input->local_count; i++) {
        qa_seat_id seat = qa_session_seat_id(input->locals[i]->pub.player.seat);
        if ((double)seat.index == loaded->routing_seat) { router.keyboard_seat = seat; break; }
      }
    }
  } else if (!has_previous_keyboard) router.has_keyboard_seat = false;
  else {
    router.has_keyboard_seat = true; router.keyboard_seat = first_seat;
    input_local *kept = qa_app_input_internal_find_local(input, previous_keyboard);
    if (kept != NULL) router.keyboard_seat = qa_session_seat_id(kept->pub.player.seat);
  }
  qa_input_controllers controllers = qa_input_controllers_from_sdl(input->controllers);
  router.controllers = &controllers;
  router.now = router_now; router.now_ctx = input;
  router.ticks = router_ticks; router.ticks_ctx = input;
  router.subframe = true;
  router.unhandled = router_unhandled; router.unhandled_ctx = input;
  input->router = qa_input_router_create(&router);
  free(routes);
  if (input->router == NULL) qa_range_error("Out of memory");
  qa_seat_id *seat_ids = xcalloc(input->local_count, sizeof *seat_ids);
  for (size_t i = 0; i < input->local_count; i++) seat_ids[i] = qa_seat_input_seat(input->locals[i]->pub.input);
  input->client_commands = qa_client_command_bindings_create(input->commands, seat_ids, input->local_count,
      client_command_execute, input);
  input->controller_settings = qa_app_controller_settings_create(input->router, seat_ids, input->local_count,
      settings_devices, input, args->settings, report_print, input);
  free(seat_ids);
  if (input->client_commands == NULL || input->controller_settings == NULL) qa_range_error("Out of memory");
  /* TS try { attach; restart; profiles } catch { if (ownsControllers) controllers.close(); throw }. */
  if (previous == NULL && !has_staging) qa_input_router_attach_window(input->router, input->window.input);
  qa_input_router_restart(input->router);
  qa_app_controller_settings *previous_settings = previous != NULL ? previous->controller_settings : NULL;
  if (previous_settings == NULL && client != NULL) {
    if (client->platform.kind == QA_APP_CLIENT_INPUT_MENU) previous_settings = client->platform.controller_settings;
    else if (client->platform.kind == QA_APP_CLIENT_INPUT_WORLD) previous_settings = client->platform.input->controller_settings;
  }
  if (previous_settings == NULL) qa_app_controller_settings_update(input->controller_settings);
  else qa_app_controller_settings_copy_settled_profiles_from(input->controller_settings, previous_settings);
  return input;
}

typedef struct create_ctx {
  qa_cvar_registry *const *staging; size_t staging_count; bool has_staging;
  const qa_app_input_open_args *args;
  qa_app_application_input *previous;
  qa_app_prepared_startup *prepared;
  qa_app_client_bootstrap *client;
  qa_app_application_input *input;
} create_ctx;

static void create_settle(void *ctx) {
  create_ctx *create = ctx;
  qa_app_application_input *input = create->input;
  qa_app_controller_settings_settle(input->controller_settings);
  if (create->previous == NULL && !create->has_staging) {
    qa_app_input_internal_activate_commands(input, false);
    qa_app_application_input_adopt_startup(input);
  }
}

/* TS ApplicationInput.create. */
static qa_app_application_input *create(qa_cvar_registry *const *staging, size_t staging_count, bool has_staging,
    const qa_app_input_open_args *args, qa_app_application_input *previous, qa_app_prepared_startup *prepared,
    qa_app_client_bootstrap *client) {
  qa_session_seat **seats = xcalloc(args->player_count, sizeof *seats);
  for (size_t i = 0; i < args->player_count; i++) seats[i] = args->players[i].seat;
  loaded_settings loaded;
  read_settings(&loaded, seats, args->player_count, args->dialect, &args->actions, args->settings, args->owner,
                previous, prepared);
  free(seats);
  qa_app_application_input *input = construct(staging, staging_count, has_staging, args, &loaded, previous, prepared, client);
  loaded_free(&loaded);
  create_ctx ctx = {staging, staging_count, has_staging, args, previous, prepared, client, input};
  qa_caught_error caught;
  if (!qa_try(create_settle, &ctx, &caught)) {
    qa_app_application_input_close(input);
    qa_app_application_input_destroy(input);
    qa_rethrow(&caught);
  }
  return input;
}

qa_app_application_input *qa_app_application_input_open(const qa_app_input_open_args *args) {
  return create(NULL, 0, false, args, args->previous, args->prepared, NULL);
}

qa_app_application_input *qa_app_application_input_prepare(qa_cvar_registry *const *live_registries,
    size_t live_registry_count, const qa_app_input_open_args *args) {
  return create(live_registries, live_registry_count, true, args, args->previous, args->prepared, NULL);
}

qa_app_application_input *qa_app_application_input_prepare_for_client(qa_app_client_bootstrap *client,
    qa_cvar_registry *const *live_registries, size_t live_registry_count, const qa_app_input_open_args *args) {
  if (args->window.input.ctx != client->window || (args->prepared != NULL && args->prepared != client->prepared))
    qa_input_error("Client input preparation changed retained owners");
  qa_app_application_input *previous = args->previous != NULL ? args->previous
      : client->platform.kind == QA_APP_CLIENT_INPUT_WORLD ? client->platform.input : NULL;
  return create(live_registries, live_registry_count, true, args, previous, client->prepared, client);
}

/* ---- accessors ---- */
const qa_app_options *qa_app_application_input_options(const qa_app_application_input *input) { return input->options; }
qa_command_dialect qa_app_application_input_dialect(const qa_app_application_input *input) { return input->dialect; }
double qa_app_application_input_now(qa_app_application_input *input) { return qa_app_input_internal_now(input); }
qa_app_console_script_files *qa_app_application_input_scripts(const qa_app_application_input *input) { return input->script_owner; }

qa_ui_binding_capabilities qa_app_application_input_binding_capabilities(qa_app_application_input *input) {
  qa_ui_binding_capabilities capabilities;
  if (input->actions.binding_capabilities != NULL) capabilities = input->actions.binding_capabilities(input->actions.ctx);
  else {
    const char *kind = input->options->network_kind != NULL ? input->options->network_kind : "";
    size_t length = strlen(kind);
    memset(&capabilities, 0, sizeof capabilities);
    capabilities.chat = length >= 7 && strcmp(kind + length - 7, "-client") == 0;
    capabilities.score_command = strcmp(kind, "q2-client") == 0 ? "score" : "+scores";
    capabilities.offhand_grapple = false; capabilities.offhand_grenades = false;
  }
  if (capabilities.score_command == NULL) capabilities.score_command = "+scores";
  return capabilities;
}

qa_cvar_registry *qa_app_application_input_shared_cvars(const qa_app_application_input *input) { return input->shared_owner; }
qa_cvar_registry *qa_app_application_input_shared_settings(const qa_app_application_input *input) { return input->shared_owner; }

/* TS guestCommands: candidateProgram?.commands ?? commands. */
static qa_command_buffer *guest_buffer(qa_app_application_input *input) {
  return input->candidate_program != NULL ? qa_app_prepared_client_commands_buffer(input->candidate_program) : input->commands;
}
double qa_app_application_input_guest_execute_now(qa_app_application_input *input, const char *text,
    const qa_command_context *source) {
  return qa_command_buffer_execute_now(guest_buffer(input), text, source, -1);
}
void qa_app_application_input_guest_insert(qa_app_application_input *input, const char *text,
    const qa_command_context *source) {
  qa_command_buffer_insert(guest_buffer(input), text, source, -1);
}
void qa_app_application_input_guest_append(qa_app_application_input *input, const char *text,
    const qa_command_context *source, int dialect) {
  qa_command_buffer_append(guest_buffer(input), text, source, dialect);
}

/* TS guestCvars(seat). */
typedef struct guest_cvars_ctx {
  qa_app_application_input *input;
  qa_command_context context;
  qa_command_cvar_routing routing; /* the routing at creation */
  qa_cvar_registry **visible; size_t visible_count;
} guest_cvars_ctx;
static qa_command_cvar_routing guest_routing(guest_cvars_ctx *guest) {
  qa_command_cvar_routing routing;
  return qa_app_input_internal_routing(guest->input, &routing) ? routing : guest->routing;
}
static qa_cvar_registry *guest_owner(void *ctx, const char *name) {
  guest_cvars_ctx *guest = ctx;
  qa_command_cvar_routing routing = guest_routing(guest);
  return routing.owner(routing.ctx, name, guest->context);
}
static size_t guest_visible(void *ctx, qa_cvar_registry ***out) {
  guest_cvars_ctx *guest = ctx;
  qa_command_cvar_routing routing = guest_routing(guest);
  size_t count = 0;
  qa_cvar_registry *const *visible = routing.visible(routing.ctx, guest->context, &count);
  free(guest->visible);
  guest->visible = count == 0 ? NULL : malloc(count * sizeof *guest->visible);
  if (count != 0 && guest->visible == NULL) qa_range_error("Out of memory");
  if (count != 0) memcpy(guest->visible, visible, count * sizeof *guest->visible);
  guest->visible_count = count;
  *out = guest->visible;
  return count;
}
static qa_cvar_registry *guest_current(void *ctx, qa_cvar_registry *owner) {
  guest_cvars_ctx *guest = ctx;
  for (size_t i = 0; i < guest->input->published_count; i++)
    if (guest->input->published[i].from == owner) return guest->input->published[i].to;
  return owner;
}
static void guest_print(void *ctx, const char *text) {
  guest_cvars_ctx *guest = ctx;
  qa_app_application_input_print(guest->input, text, &guest->context);
}
static void destroy_guest_cvars(void *object) {
  guest_cvars_ctx *guest = object;
  free(guest->visible); free(guest);
}

qa_app_q3_client_cvars *qa_app_application_input_guest_cvars(qa_app_application_input *input, qa_seat_id seat) {
  input_local *local = qa_app_input_internal_find_local(input, seat);
  qa_command_cvar_routing routing;
  if (local == NULL || !qa_app_input_internal_routing(input, &routing)) qa_input_error("Guest cvars require a routed local seat");
  guest_cvars_ctx *guest = xcalloc(1, sizeof *guest);
  guest->input = input; guest->routing = routing;
  memset(&guest->context, 0, sizeof guest->context);
  guest->context.session = seat.session;
  guest->context.origin.kind = QA_COMMAND_ORIGIN_LOCAL_SEAT;
  guest->context.origin.seat = seat;
  guest->context.origin.client = qa_session_client_id(qa_session_seat_client(local->pub.player.seat));
  qa_app_input_internal_own(input, guest, destroy_guest_cvars);
  qa_app_q3_client_cvars_options options = {guest, guest_owner, guest_visible, guest_current, guest_print};
  qa_app_q3_client_cvars *cvars = qa_app_q3_client_cvars_create(options);
  if (cvars == NULL) qa_range_error("Out of memory");
  return cvars;
}

void qa_app_application_input_publish_shared_cvars(qa_app_application_input *input, qa_cvar_registry *registry) {
  if (input->shared_owner != NULL) qa_app_input_internal_set_published(input, input->shared_owner, registry);
  input->shared_owner = registry;
}

void qa_app_application_input_adopt_cvar_owner(qa_app_application_input *input, qa_cvar_registry *candidate,
    qa_cvar_registry *retained) {
  qa_app_input_internal_set_published(input, candidate, retained);
  if (input->cvar_owner == candidate) input->cvar_owner = retained;
  if (input->console_cvars == candidate) input->console_cvars = retained;
}

void qa_app_application_input_adopt_client_source(qa_app_application_input *input, const qa_app_input_command_owner *owner) {
  qa_cvar_registry *movement = owner->movement != NULL ? owner->movement : owner->cvars;
  qa_cvar_registry *fallback = owner->fallback != NULL ? owner->fallback : owner->cvars;
  if (input->console_routing != NULL || qa_cvar_registry_dialect(movement) != input->dialect
      || qa_cvar_registry_dialect(fallback) != qa_cvar_registry_dialect(owner->cvars)
      || qa_cvar_registry_dialect(owner->cvars) != qa_cvar_registry_dialect(input->console_cvars))
    qa_input_error("Client input requires matching movement and source registries");
  input->cvar_owner = movement;
  input->console_cvars = fallback;
  input->has_external_routing = true; input->external_routing = owner->routing;
  input->script_owner = owner->scripts;
  for (size_t i = 0; i < input->local_count; i++) {
    qa_seat_id seat = qa_session_seat_id(input->locals[i]->pub.player.seat);
    const qa_app_prepared_seat *prepared = prepared_seat(input->startup, seat);
    if (prepared != NULL) qa_app_application_input_adopt_mouse_owner(input, seat, prepared->mouse);
  }
}

void qa_app_application_input_adopt_mouse_owner(qa_app_application_input *input, qa_seat_id seat, qa_mouse_settings retained) {
  qa_mouse_settings *candidate = qa_app_input_internal_mouse(input, seat);
  input_local *local = qa_app_input_internal_find_local(input, seat);
  if (candidate == NULL || local == NULL) qa_input_error("Mouse publication lost its seat");
  qa_app_input_internal_set_published(input, candidate->cvars, retained.cvars);
  qa_app_input_internal_set_mouse(input, seat, retained);
  qa_app_input_internal_retain_object(input, retained.cvars);
  qa_mouse_input_bind_settings(qa_input_command_builder_mouse(local->pub.builder), stable_store(input, retained));
}

/* TS guestInput(seat). */
typedef struct guest_input_ctx { qa_app_application_input *input; qa_seat_id seat; input_local *local; } guest_input_ctx;
static bool guest_owner_seat(guest_input_ctx *guest, qa_binding_command_seat *out) {
  if (guest->input->candidate_program != NULL && qa_app_prepared_client_commands_input(guest->input->candidate_program, guest->seat, out))
    return true;
  *out = qa_seat_input_command_seat(guest->local->pub.input);
  return true;
}
static const qa_input_binding *guest_bindings(void *ctx, size_t *out_count) {
  qa_binding_command_seat seat; guest_owner_seat(ctx, &seat);
  return seat.bindings(seat.ctx, out_count);
}
static const qa_input_binding_target *guest_binding(void *ctx, const qa_physical_input *physical) {
  qa_binding_command_seat seat; guest_owner_seat(ctx, &seat);
  return seat.binding(seat.ctx, physical);
}
static bool guest_bind(void *ctx, const qa_input_binding *binding) {
  qa_binding_command_seat seat; guest_owner_seat(ctx, &seat);
  return seat.bind(seat.ctx, binding);
}
static void guest_unbind(void *ctx, const qa_physical_input *physical) {
  qa_binding_command_seat seat; guest_owner_seat(ctx, &seat);
  seat.unbind(seat.ctx, physical);
}
static void guest_unbind_all(void *ctx) {
  qa_binding_command_seat seat; guest_owner_seat(ctx, &seat);
  seat.unbind_all(seat.ctx);
}
static bool guest_is_down(void *ctx, const qa_physical_input *physical) {
  guest_input_ctx *guest = ctx;
  qa_binding_command_seat staged;
  if (guest->input->candidate_program != NULL
      && qa_app_prepared_client_commands_input(guest->input->candidate_program, guest->seat, &staged))
    return qa_app_prepared_client_commands_input_is_down(guest->input->candidate_program, guest->seat, physical);
  return qa_seat_input_is_down(guest->local->pub.input, physical);
}
static void guest_clear_states(void *ctx) {
  guest_input_ctx *guest = ctx;
  if (guest->input->candidate_program == NULL)
    qa_seat_input_release(guest->local->pub.input, qa_app_input_internal_now(guest->input), NULL);
  else qa_app_prepared_client_commands_input_clear_states(guest->input->candidate_program, guest->seat);
}

qa_app_qvm_client_input qa_app_application_input_guest_input(qa_app_application_input *input, qa_seat_id seat) {
  input_local *local = qa_app_input_internal_find_local(input, seat);
  if (local == NULL) qa_input_error("Guest input requires a local seat");
  guest_input_ctx *guest = xcalloc(1, sizeof *guest);
  guest->input = input; guest->seat = seat; guest->local = local;
  qa_app_input_internal_own(input, guest, destroy_memory);
  qa_app_qvm_client_input view;
  memset(&view, 0, sizeof view);
  view.seat.ctx = guest;
  view.seat.bindings = guest_bindings; view.seat.binding = guest_binding; view.seat.bind = guest_bind;
  view.seat.unbind = guest_unbind; view.seat.unbind_all = guest_unbind_all;
  view.is_down = guest_is_down; view.clear_states = guest_clear_states; view.state_ctx = guest;
  return view;
}

void qa_app_application_input_validate_candidate_commands(qa_app_application_input *input) {
  if (input->candidate_program != NULL) qa_app_prepared_client_commands_validate_publication(input->candidate_program);
}

static void candidate_release_append(void *ctx, const char *text, const qa_command_context *source) {
  qa_app_prepared_client_commands_release_append(ctx, text, source);
}

void qa_app_application_input_release_into_candidate(qa_app_application_input *input,
    qa_app_application_input *previous, bool world_changed) {
  if (world_changed || qa_app_application_input_profile_changed(input)) {
    qa_command_append_target target = {input->candidate_program, candidate_release_append};
    qa_app_application_input_release_for_profile_change(previous, input->candidate_program != NULL ? &target : NULL);
  } else if (input->candidate_program != NULL)
    qa_app_prepared_client_commands_release_inputs(input->candidate_program, qa_app_input_internal_now(input));
}

void qa_app_application_input_publish_candidate_commands(qa_app_application_input *input) {
  if (input->candidate_program != NULL) {
    qa_app_prepared_client_commands_publish(input->candidate_program);
    if (input->owns_candidate_program) qa_app_prepared_client_commands_destroy(input->candidate_program);
  }
  input->candidate_program = NULL; input->owns_candidate_program = false;
  for (size_t i = 0; i < input->candidate_default_count; i++) {
    input_candidate_defaults *entry = &input->candidate_defaults[i];
    if (input->startup != NULL)
      qa_app_prepared_startup_adopt_binding_defaults(input->startup, entry->seat, entry->bindings, entry->count);
  }
  for (size_t i = 0; i < input->candidate_gamepad_count; i++)
    qa_seat_input_gamepad(input->candidate_gamepads[i].input)->tuning = input->candidate_gamepads[i].tuning;
  for (size_t i = 0; i < input->candidate_default_count; i++)
    free_bindings(input->candidate_defaults[i].bindings, input->candidate_defaults[i].count);
  input->candidate_default_count = 0; input->candidate_gamepad_count = 0;
}

qa_command_buffer *qa_app_application_input_commands(const qa_app_application_input *input) { return input->commands; }
qa_cvar_registry *qa_app_application_input_cvars(const qa_app_application_input *input) { return input->cvar_owner; }

double qa_app_application_input_local_player_capacity(qa_app_application_input *input) {
  return input->actions.local_player_capacity != NULL ? input->actions.local_player_capacity(input->actions.ctx) : 4;
}

bool qa_app_application_input_can_remove_local_player(qa_app_application_input *input, qa_seat_id seat) {
  return input->local_count > 1
      && (input->actions.can_remove_local_player == NULL || input->actions.can_remove_local_player(input->actions.ctx, seat));
}

size_t qa_app_application_input_local_count(const qa_app_application_input *input) { return input->local_count; }
qa_app_local_input *qa_app_application_input_local(const qa_app_application_input *input, size_t index) {
  if (index >= input->local_count) return NULL;
  return &input->locals[index]->pub;
}

qa_sdl_controllers *qa_app_application_input_controllers(const qa_app_application_input *input) { return input->controllers; }
qa_input_router *qa_app_application_input_router(const qa_app_application_input *input) { return input->router; }
qa_app_controller_settings *qa_app_application_input_controller_settings(const qa_app_application_input *input) {
  return input->controller_settings;
}
qa_app_input_devices *qa_app_application_input_input_devices(const qa_app_application_input *input) { return input->input_devices; }
void qa_app_application_input_enable_archive_persistence(qa_app_application_input *input) { input->archive_persistence = true; }

qa_cvar_registry *qa_app_application_input_input_cvars(const qa_app_application_input *input, bool has_id, qa_seat_id id) {
  for (size_t i = 0; i < input->mouse_count; i++)
    if (!has_id || qa_same_seat(input->mice[i].seat, id)) return input->mice[i].settings.cvars;
  return NULL;
}

bool qa_app_application_input_can_reset_bindings(qa_app_application_input *input, qa_seat_id id) {
  if (input->startup == NULL) return false;
  bool all = false, has_selected = false;
  char *const *keys = NULL; size_t key_count = 0, selected_count = 0, authored_count = 0;
  const qa_input_binding *selected = NULL, *authored = NULL;
  if (!qa_app_prepared_startup_seat_choices(input->startup, id, &all, &keys, &key_count, &has_selected,
                                            &selected, &selected_count, &authored, &authored_count))
    return false;
  return authored != NULL; /* authoredBindings !== null */
}

void qa_app_application_input_reset_bindings(qa_app_application_input *input, qa_seat_id id) {
  if (input->startup == NULL) qa_input_error("Authored binding defaults are unavailable");
  qa_app_prepared_startup_reset_bindings(input->startup, id);
}

bool qa_app_application_input_advance_startup(qa_app_application_input *input) {
  return input->startup != NULL && qa_app_prepared_startup_advance_frame(input->startup);
}

/* The shared TS print closure for created registries. */
void qa_app_application_input_print(qa_app_application_input *input, const char *text, const qa_command_context *source) {
  qa_app_input_internal_print_source(input, text, source);
}

/* ---- createJoinedLocal ---- */
static input_local *create_joined_local(qa_app_application_input *input, const qa_app_local_player *player,
    const qa_seat_settings *saved, const qa_cvar_archive_entry *archive, size_t archive_count) {
  qa_seat_id seat = qa_session_seat_id(player->seat);
  qa_command_dialect source_dialect = qa_app_input_internal_source_dialect(input);
  qa_command_context context = qa_app_input_internal_seat_context(seat.session, player);
  const qa_app_prepared_seat *prepared = prepared_seat(input->startup, seat);
  input_local *local = new_local(input);
  local->pub.player = *player;
  qa_seat_input *seat_input = prepared != NULL ? prepared->input : create_seat_input(input, seat, context);
  qa_app_input_internal_retain_object(input, seat_input);
  qa_mouse_settings mouse;
  if (prepared != NULL) mouse = prepared->mouse;
  else qa_mouse_settings_init(&mouse, qa_app_input_internal_create_registry(input, source_dialect, context, true));
  qa_app_input_internal_retain_object(input, mouse.cvars);
  if (prepared == NULL) qa_cvar_registry_apply_archive(mouse.cvars, archive, archive_count);
  local->pub.input = seat_input;
  local->pub.builder = create_builder(input, local, mouse, player->actor);
  size_t default_count = 0;
  qa_input_binding *defaults = defaults_for(input, seat, &default_count);
  if (prepared == NULL) {
    if (saved != NULL) bind_all(seat_input, saved->bindings, saved->binding_count);
    else bind_all(seat_input, defaults, default_count);
  }
  free_bindings(defaults, default_count);
  if (saved != NULL && prepared == NULL) {
    qa_seat_input_gamepad(seat_input)->tuning = saved->gamepad;
    qa_mouse_input_set_tuning(qa_input_command_builder_mouse(local->pub.builder), saved->mouse);
    if (saved->has_always_run) {
      qa_view_input_tuning tuning = qa_input_command_builder_tuning(local->pub.builder);
      tuning.always_run = saved->always_run;
      qa_input_command_builder_set_tuning(local->pub.builder, tuning);
    }
  }
  local->pub.haptics = qa_app_input_internal_create_haptics(input, seat);
  if (saved != NULL) {
    (void)qa_seat_haptics_set_enabled(local->pub.haptics, saved->rumble);
    (void)qa_seat_haptics_set_strength(local->pub.haptics, saved->rumble_strength);
  }
  local->created_console = qa_app_input_internal_create_console(input, seat, source_dialect, context, seat_input, local, true);
  local->pub.console = local->created_console;
  if (saved != NULL)
    qa_console_history_replace(&local->pub.console->history, (const char *const *)saved->history, saved->history_count);
  qa_app_input_internal_set_mouse(input, seat, mouse);
  (void)qa_app_input_internal_add_ui_callback(input, seat_input, seat, local, local->created_console, true);
  return local;
}

/* ---- prepareLocalSeats ---- */
typedef enum preparation_phase { PHASE_PREPARED, PHASE_PUBLISHED, PHASE_DISCARDED } preparation_phase;
struct qa_app_local_seat_preparation {
  qa_app_application_input *input;
  qa_session_seat **seats; size_t seat_count;
  input_local **before; size_t before_count;
  loaded_settings loaded;
  preparation_phase phase;
};

qa_app_local_seat_preparation *qa_app_application_input_prepare_local_seats(qa_app_application_input *input,
    qa_session_seat *const *seats, size_t seat_count) {
  if (seat_count < 1 || seat_count > 4) qa_input_error("A graphical world needs one to four local players");
  qa_app_controller_settings_settle(input->controller_settings);
  qa_app_local_seat_preparation *preparation = xcalloc(1, sizeof *preparation);
  preparation->input = input;
  input->refs++;
  preparation->seats = xcalloc(seat_count, sizeof *preparation->seats);
  memcpy(preparation->seats, seats, seat_count * sizeof *seats);
  preparation->seat_count = seat_count;
  preparation->before = xcalloc(input->local_count, sizeof *preparation->before);
  if (input->local_count != 0) memcpy(preparation->before, input->locals, input->local_count * sizeof *input->locals);
  preparation->before_count = input->local_count;
  read_settings(&preparation->loaded, seats, seat_count, input->dialect, &input->actions, input->settings, NULL,
                input, input->startup);
  preparation->phase = PHASE_PREPARED;
  return preparation;
}

static qa_controller_selection automatic_selection(void) {
  qa_controller_selection selection;
  memset(&selection, 0, sizeof selection);
  selection.kind = QA_CONTROLLER_SELECTION_AUTOMATIC;
  return selection;
}

void qa_app_local_seat_preparation_publish(qa_app_local_seat_preparation *preparation,
    const qa_app_local_player *players, size_t player_count, qa_app_client_bootstrap *client,
    const qa_app_sim_presentation_access *source, bool retain) {
  qa_app_application_input *input = preparation->input;
  bool stale = preparation->phase != PHASE_PREPARED || input->local_count != preparation->before_count;
  for (size_t i = 0; !stale && i < input->local_count; i++) stale = input->locals[i] != preparation->before[i];
  if (stale) qa_input_error("Local input preparation is stale");
  bool changed = player_count != preparation->seat_count;
  for (size_t i = 0; !changed && i < player_count; i++) changed = players[i].seat != preparation->seats[i];
  if (changed) qa_input_error("Local input admission changed prepared identities");
  input_local **retained = xcalloc(player_count, sizeof *retained);
  for (size_t i = 0; i < player_count; i++)
    for (size_t b = 0; b < preparation->before_count && retained[i] == NULL; b++)
      if (preparation->before[b]->pub.player.seat == players[i].seat) retained[i] = preparation->before[b];
  if (source != NULL) input->simulation = source;
  qa_seat_id keyboard;
  bool has_keyboard = qa_input_router_keyboard_seat(input->router, &keyboard);
  qa_controller_selection *selections = xcalloc(player_count, sizeof *selections);
  for (size_t i = 0; i < player_count; i++) {
    if (retained[i] != NULL) selections[i] = qa_input_router_controller_selection(input->router, qa_session_seat_id(players[i].seat));
    else if (i < preparation->loaded.count && preparation->loaded.has_saved[i])
      selections[i] = selection_from_settings(&preparation->loaded.saved[i].controller);
    else selections[i] = automatic_selection();
  }
  qa_app_application_input_release_for_profile_change(input, NULL);
  qa_app_input_internal_retire_commands(input);
  input_local **next = xcalloc(player_count, sizeof *next);
  for (size_t i = 0; i < player_count; i++) {
    input_local *local = retained[i];
    if (local == NULL)
      local = create_joined_local(input, &players[i],
          i < preparation->loaded.count && preparation->loaded.has_saved[i] ? &preparation->loaded.saved[i] : NULL,
          preparation->loaded.input[i], preparation->loaded.input_counts[i]);
    if (!qa_same_actor(local->pub.player.actor, players[i].actor)) {
      local->pub.player.actor = players[i].actor;
      if (qa_input_command_builder_dialect(local->pub.builder) != QA_COMMAND_DIALECT_Q3) {
        qa_app_sim_player_view view = input->simulation->player_view(input->simulation->ctx, players[i].actor);
        qa_input_command_builder_set_view_angles(local->pub.builder, view.angles);
      }
    }
    next[i] = local;
  }
  for (size_t b = 0; b < preparation->before_count; b++) {
    input_local *local = preparation->before[b];
    bool kept = false;
    for (size_t i = 0; i < player_count && !kept; i++) kept = next[i] == local;
    if (kept) continue;
    qa_app_input_internal_forget_seat(input, local);
    (void)qa_seat_haptics_close(local->pub.haptics);
    local->retired = true;
  }
  input->local_count = 0;
  for (size_t i = 0; i < player_count; i++) *INPUT_PUSH(input->locals, input->local_count, input->local_capacity) = next[i];
  qa_seat_id *ids = xcalloc(player_count, sizeof *ids);
  for (size_t i = 0; i < player_count; i++) ids[i] = qa_session_seat_id(preparation->seats[i]);
  qa_client_command_bindings_publish_seats(input->client_commands, ids, player_count);
  qa_input_seat_route *routes = xcalloc(player_count, sizeof *routes);
  for (size_t i = 0; i < player_count; i++) { routes[i].input = next[i]->pub.input; routes[i].controller = selections[i]; }
  bool publish_keyboard = false;
  qa_seat_id published_keyboard;
  memset(&published_keyboard, 0, sizeof published_keyboard);
  if (has_keyboard) {
    if (qa_app_input_internal_find_local(input, keyboard) != NULL) { publish_keyboard = true; published_keyboard = keyboard; }
    else if (player_count > 0) { publish_keyboard = true; published_keyboard = qa_session_seat_id(next[0]->pub.player.seat); }
  }
  qa_input_router_publish_seats(input->router, routes, player_count, publish_keyboard, published_keyboard);
  qa_app_controller_settings_publish_seats(input->controller_settings, ids, player_count);
  free(routes); free(selections); free(retained); free(next); free(ids);
  if (client != NULL) qa_app_application_input_publish_client_seats(input, client, retain);
  else if (input->startup != NULL) qa_app_input_internal_publish_seats_to_startup(input, retain);
  qa_app_application_input_adopt_startup(input);
  qa_app_input_internal_activate_commands(input, true);
  preparation->phase = PHASE_PUBLISHED;
}

void qa_app_local_seat_preparation_discard(qa_app_local_seat_preparation *preparation) {
  if (preparation->phase == PHASE_PREPARED) preparation->phase = PHASE_DISCARDED;
}

void qa_app_local_seat_preparation_destroy(qa_app_local_seat_preparation *preparation) {
  if (preparation == NULL) return;
  loaded_free(&preparation->loaded);
  free(preparation->seats); free(preparation->before);
  qa_app_application_input *input = preparation->input;
  free(preparation);
  release(input);
}
