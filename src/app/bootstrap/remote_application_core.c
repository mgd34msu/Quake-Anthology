/* Port of src/app/bootstrap/remote-application.ts: fields and getters (TS
 * 216-312, 598-603, 898, 923, 936-954), allocateQport, the static
 * recordedOptions (452-464), queueCommand (1585-1590), requestQuit (2314) and
 * requireQwAddress (2508-2511). Status per member:
 * docs/remote_application_layout.md. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "remote_application_internal.h"
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include "qa/app/bootstrap/network/q2_demo.h"
#include "qa/content/catalog/index.h"
#include "qa/content/catalog/products.h"
#include "qa/content/mounts.h"
#include "qa/content/roots.h"
#include "qa/foundation/support.h"

static void *xcalloc(size_t count, size_t size) {
  void *memory = calloc(count == 0 ? 1 : count, size == 0 ? 1 : size);
  if (memory == NULL) qa_range_error("Out of memory");
  return memory;
}
static char *dup_or_null(const char *text) {
  if (text == NULL) return NULL;
  char *copy = strdup(text);
  if (copy == NULL) qa_range_error("Out of memory");
  return copy;
}

/* ---- TS `{ ...options }` --------------------------------------------- */
void qa_remote_options_copy(const qa_app_options *options, qa_app_options *out) {
  *out = *options;
  out->network_transport_relay = dup_or_null(options->network_transport_relay);
  out->startup_commands = NULL;
  if (options->startup_command_count > 0) {
    out->startup_commands = xcalloc(options->startup_command_count, sizeof *out->startup_commands);
    for (size_t i = 0; i < options->startup_command_count; i++) out->startup_commands[i] = dup_or_null(options->startup_commands[i]);
  }
  out->remote_content.directory = dup_or_null(options->remote_content.directory);
  out->server_profile_path = dup_or_null(options->server_profile_path);
  out->corpus_root = dup_or_null(options->corpus_root);
  out->user_content_root = dup_or_null(options->user_content_root);
  out->addon_content_roots = NULL;
  if (options->addon_content_root_count > 0) {
    out->addon_content_roots = xcalloc(options->addon_content_root_count, sizeof *out->addon_content_roots);
    for (size_t i = 0; i < options->addon_content_root_count; i++) {
      out->addon_content_roots[i].family = options->addon_content_roots[i].family;
      out->addon_content_roots[i].root = dup_or_null(options->addon_content_roots[i].root);
    }
  }
  out->product = dup_or_null(options->product);
  out->map_product = dup_or_null(options->map_product);
  out->map = dup_or_null(options->map);
  out->quakec_program = dup_or_null(options->quakec_program);
  out->q2_game_library = dup_or_null(options->q2_game_library);
  out->weapon_behavior_product = dup_or_null(options->weapon_behavior_product);
  out->weapon_behavior_id = dup_or_null(options->weapon_behavior_id);
  out->mods = NULL;
  if (options->mod_count > 0) {
    out->mods = xcalloc(options->mod_count, sizeof *out->mods);
    for (size_t i = 0; i < options->mod_count; i++) {
      out->mods[i].product = dup_or_null(options->mods[i].product);
      out->mods[i].id = dup_or_null(options->mods[i].id);
    }
  }
  out->movement_product = dup_or_null(options->movement_product);
  out->character_model = dup_or_null(options->character_model);
  out->character_skin = dup_or_null(options->character_skin);
  out->network_host = dup_or_null(options->network_host);
  out->network_remote = dup_or_null(options->network_remote);
}

/* ---- RemoteCommand ---------------------------------------------------- */
static qa_command_origin *copy_origin_chain(const qa_command_origin *origin) {
  qa_command_origin *copy = xcalloc(1, sizeof *copy);
  *copy = *origin;
  copy->name = dup_or_null(origin->name);
  copy->caller = origin->kind == QA_COMMAND_ORIGIN_SCRIPT && origin->caller != NULL ? copy_origin_chain(origin->caller) : NULL;
  return copy;
}
static void free_origin_chain(qa_command_origin *origin) {
  while (origin != NULL) {
    qa_command_origin *caller = (qa_command_origin *)origin->caller;
    free((char *)origin->name);
    free(origin);
    origin = caller;
  }
}
qa_command_context qa_remote_command_context_copy(const qa_command_context *source) {
  qa_command_context copy = *source;
  copy.origin.name = dup_or_null(source->origin.name);
  copy.origin.caller = source->origin.kind == QA_COMMAND_ORIGIN_SCRIPT && source->origin.caller != NULL
      ? copy_origin_chain(source->origin.caller) : NULL;
  if (source->producer != NULL) {
    qa_command_producer *producer = xcalloc(1, sizeof *producer);
    *producer = *source->producer;
    copy.producer = producer;
  }
  return copy;
}
void qa_remote_command_free(qa_remote_command *command) {
  free(command->name);
  for (size_t i = 0; i < command->arg_count; i++) free(command->args[i]);
  free(command->args);
  if (command->has_source) {
    free((char *)command->source.origin.name);
    free_origin_chain((qa_command_origin *)command->source.origin.caller);
    free((qa_command_producer *)command->source.producer);
  }
  memset(command, 0, sizeof *command);
}

/* ---- getters (TS 216-312) --------------------------------------------- */
qa_app_startup_server_browser *qa_remote_application_server_browser(const qa_remote_application *app) {
  return app->browser.browser;
}
qa_remote_command_owner *qa_remote_application_client_commands(const qa_remote_application *app) {
  return app->client_command_owner;
}
qa_app_view_settings *qa_remote_application_view_settings(const qa_remote_application *app) { return app->view_settings; }
const qa_remote_presentation *qa_remote_application_remote(const qa_remote_application *app) {
  return app->primary_peer != NULL ? qa_app_remote_seat_source_remote(app->primary_peer->source) : &app->initial_remote;
}
const qa_remote_source *qa_remote_application_source(qa_remote_application *app) {
  qa_remote_seat_peer *peer = app->primary_peer;
  if (peer == NULL) return &app->initial_source;
  if (app->promoted_peer != peer) {
    /* { kind: "live", network: peer.source.network, transport: peer.source.transport } */
    app->promoted_peer = peer;
    memset(&app->promoted_source, 0, sizeof app->promoted_source);
    app->promoted_source.kind = QA_REMOTE_SOURCE_LIVE;
    app->promoted_source.network = *qa_app_remote_seat_source_network(peer->source);
    app->promoted_source.transport = qa_app_remote_seat_source_transport(peer->source);
  }
  return &app->promoted_source;
}
const qa_remote_live_network *qa_remote_application_network(qa_remote_application *app) {
  const qa_remote_source *source = qa_remote_application_source(app);
  return source->kind == QA_REMOTE_SOURCE_LIVE ? &source->network : NULL;
}
qa_app_world_seat_presentation *qa_remote_application_presentation(const qa_remote_application *app) {
  if (app->primary_peer == NULL) return app->initial_presentation;
  return app->primary_peer->view != NULL ? qa_app_remote_seat_view_presentation(app->primary_peer->view) : NULL;
}
void qa_remote_application_set_presentation(qa_remote_application *app, qa_app_world_seat_presentation *value) {
  if (app->primary_peer == NULL) app->initial_presentation = value;
  else if (value != NULL) qa_raise("Error", 0, 0, "Promoted presentation belongs to its seat view");
}
bool qa_remote_application_primary_view_current(const qa_remote_application *app) {
  return qa_remote_application_presentation(app) != NULL
      && (app->primary_peer != NULL || app->primary_view_generation == app->world_load_generation);
}
void qa_remote_application_send_command(qa_remote_application *app, const char *text) {
  const qa_remote_live_network *network = qa_remote_application_network(app);
  if (network != NULL) qa_app_remote_seat_network_command(network, text);
}
bool qa_remote_application_presented_cinematic(const qa_remote_application *app) { return app->cinematic_presented; }
bool qa_remote_application_has_cinematic(const qa_remote_application *app) { return app->cinematic != NULL; }
qa_seat_id qa_remote_application_seat_id(const qa_remote_application *app) {
  return app->primary_peer != NULL ? qa_session_seat_id(app->primary_peer->seat) : app->initial_seat_id;
}

/* TS allocateQport: crypto.getRandomValues(new Uint16Array(1))[0], then the
 * first port upward (mod 65536) whose low byte is nonzero and unused. */
static uint16_t os_random_u16(void) {
  uint16_t value = 0;
  if (getrandom(&value, sizeof value, 0) != (ssize_t)sizeof value) qa_raise("Error", 0, 0, "Random source unavailable");
  return value;
}
uint16_t qa_remote_application_allocate_qport(qa_remote_application *app) {
  uint16_t port = app->random_u16 != NULL ? app->random_u16(app->random_ctx) : os_random_u16();
  for (;;) {
    bool taken = (port & 255) == 0;
    for (size_t i = 0; i < app->qport_count && !taken; i++) if ((app->qports[i] & 255) == (port & 255)) taken = true;
    if (!taken) break;
    port = (uint16_t)((port + 1) & 65535);
  }
  for (size_t i = 0; i < app->qport_count; i++) if (app->qports[i] == port) return port;
  if (app->qport_count == app->qport_capacity) {
    size_t capacity = app->qport_capacity == 0 ? 8 : app->qport_capacity * 2;
    uint16_t *grown = realloc(app->qports, capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("Out of memory");
    app->qports = grown; app->qport_capacity = capacity;
  }
  app->qports[app->qport_count++] = port;
  return port;
}

/* ---- static recordedOptions (TS 452-464) ------------------------------ */
static bool rerelease_q2(const char *kind) {
  return strcmp(kind, "q2-rerelease") == 0 || strcmp(kind, "q2-kex") == 0 || strcmp(kind, "q2-kex-demo") == 0;
}
void qa_remote_application_recorded_options(const qa_app_options *options, const qa_app_demo_resource *resource, qa_app_options *out) {
  qa_remote_options_copy(options, out);
  /* const { movementProduct, remoteContent: priorSelection, quakeCProgram, q1Protocol, q2Protocol, botSkill, ...retained } */
  free(out->movement_product); out->movement_product = NULL;
  out->has_remote_content = false; free(out->remote_content.directory); memset(&out->remote_content, 0, sizeof out->remote_content);
  free(out->quakec_program); out->quakec_program = NULL;
  out->has_q1_protocol = false; memset(&out->q1_protocol, 0, sizeof out->q1_protocol);
  out->has_q2_protocol = false; memset(&out->q2_protocol, 0, sizeof out->q2_protocol);
  out->has_bot_skill = false; out->bot_skill = 0;
  qa_game_family family = resource->kind == QA_APP_DEMO_Q2 ? QA_GAME_FAMILY_Q2 : resource->kind == QA_APP_DEMO_Q3 ? QA_GAME_FAMILY_Q3 : QA_GAME_FAMILY_Q1;
  const qa_catalog_product_expectation *selected = NULL;
  for (size_t i = 0; i < qa_expected_product_count; i++)
    if (options->product != NULL && strcmp(qa_expected_products[i].id, options->product) == 0) { selected = &qa_expected_products[i]; break; }
  const char *net_quake_product = selected != NULL && selected->family == QA_GAME_FAMILY_Q1 && selected->edition != NULL
      && strcmp(selected->edition, "classic") == 0 ? selected->id : "q1-classic-id1";
  bool q2_rerelease = false;
  if (resource->kind == QA_APP_DEMO_Q2) {
    qa_app_q2_playback_header header;
    memset(&header, 0, sizeof header);
    qa_app_q2_read_playback_header(resource->bytes, resource->byte_count, &header);
    const char *kind = header.is_mvd ? header.protocol.kind : header.dm2.protocol.kind;
    q2_rerelease = kind != NULL && rerelease_q2(kind);
    qa_app_q2_playback_header_free(&header);
  }
  const char *base = resource->kind == QA_APP_DEMO_QW ? "q1-quakeworld"
      : resource->kind == QA_APP_DEMO_Q2 ? (q2_rerelease ? "q2-rerelease-baseq2" : "q2-classic-baseq2") : "q3-baseq3";
  bool has_selection = resource->kind != QA_APP_DEMO_Q1;
  qa_remote_content_selection selection;
  memset(&selection, 0, sizeof selection);
  if (has_selection) {
    if (options->has_remote_content && options->remote_content.base != NULL && strcmp(options->remote_content.base, base) == 0) {
      selection.base = options->remote_content.base;
      selection.directory = dup_or_null(options->remote_content.directory);
    } else if (!qa_remote_content_selection_make(base,
        resource->kind == QA_APP_DEMO_QW ? "qw" : resource->kind == QA_APP_DEMO_Q2 ? "baseq2" : "baseq3", &selection)) {
      qa_range_error("Out of memory");
    }
  }
  /* network: { kind: "offline" } replaces the whole network object */
  out->network_kind = "offline";
  free(out->network_host); out->network_host = NULL;
  free(out->network_remote); out->network_remote = NULL;
  out->network_port = 0;
  out->seats = 1; out->dedicated = false; out->rules = "standard";
  out->movement = family; out->character = family;
  const char *model = options->character == family ? options->character_model
      : family == QA_GAME_FAMILY_Q1 ? "player" : family == QA_GAME_FAMILY_Q2 ? "male" : "sarge";
  char *character_model = dup_or_null(model);
  free(out->character_model); out->character_model = character_model;
  free(out->product);
  if (!has_selection) out->product = dup_or_null(net_quake_product);
  else {
    out->product = qa_remote_content_product(&selection, NULL, 0);
    if (out->product == NULL) qa_range_error("Out of memory");
    out->has_remote_content = true;
    out->remote_content = selection;
  }
}

/* ---- getters (TS 598-603, 898-954) ------------------------------------ */
const qa_app_options *qa_remote_application_options(const qa_remote_application *app) { return &app->launch_options; }
bool qa_remote_application_finished(const qa_remote_application *app) {
  return app->stopping || app->closed || (app->launch_options.has_frame_limit && app->frames >= app->launch_options.frame_limit);
}
void qa_remote_application_pump_client_input(qa_remote_application *app) {
  if (app->controls != NULL) qa_app_application_input_pump(app->controls, false);
}
bool qa_remote_application_advance_client_startup(qa_remote_application *app) {
  return qa_app_prepared_startup_advance_frame(app->configuration->prepared);
}
const char *qa_remote_application_capture_map(const qa_remote_application *app) {
  return app->loaded_content == NULL ? NULL : qa_app_loaded_content_recipe(app->loaded_content)->map.geometry.requested_path;
}
void qa_remote_application_release_settings(qa_remote_application *app) {
  if (app->release_view_cvars) qa_app_view_settings_unbind_cvars(app->view_settings);
  app->release_view_cvars = false;
}
qa_app_loaded_content *qa_remote_application_content(const qa_remote_application *app) {
  if (app->loaded_content == NULL) qa_raise("Error", 0, 0, "Remote server has not supplied a world");
  return app->loaded_content;
}
qa_content_mounted *qa_remote_application_mounts(const qa_remote_application *app) {
  /* remoteContent?.mounts ?? loadedContent?.mounts ?? mountedContent.mounts
   * (the native content forms carry qa_content_mounted) */
  if (app->remote_content != NULL) return app->remote_content->mounts;
  if (app->loaded_content != NULL) return (qa_content_mounted *)qa_app_loaded_content_mounts(app->loaded_content);
  return (qa_content_mounted *)qa_app_configuration_content_mounts(app->mounted_content);
}
double qa_remote_application_frame_count(const qa_remote_application *app) { return app->frames; }
double qa_remote_application_time_milliseconds(const qa_remote_application *app) {
  /* a promoted peer's source is always live */
  if (app->primary_peer == NULL && app->initial_source.kind == QA_REMOTE_SOURCE_GTV) {
    double time = 0;
    if (qa_app_gtv_remote_source_time(app->initial_source.watch, &time)) return time;
  }
  return app->elapsed;
}
qa_sdl_window *qa_remote_application_window(const qa_remote_application *app) {
  return qa_app_native_renderer_window(app->renderer);
}
qa_net_address qa_remote_application_network_address(qa_remote_application *app) {
  const qa_remote_source *source = qa_remote_application_source(app);
  if (source->kind != QA_REMOTE_SOURCE_LIVE) qa_raise("Error", 0, 0, "Recorded source has no network address");
  qa_net_address address;
  memset(&address, 0, sizeof address);
  if (!qa_app_application_transport_address(source->transport, &address)) qa_range_error("Out of memory");
  return address;
}
const qa_app_local_player *qa_remote_application_local_players(qa_remote_application *app, size_t *out_count) {
  size_t count = app->controls != NULL ? qa_app_application_input_local_count(app->controls) : 0;
  if (count > app->local_player_capacity) {
    qa_app_local_player *grown = realloc(app->local_player_rows, count * sizeof *grown);
    if (grown == NULL) qa_range_error("Out of memory");
    app->local_player_rows = grown;
    app->local_player_capacity = count;
  }
  for (size_t i = 0; i < count; i++) app->local_player_rows[i] = qa_app_application_input_local(app->controls, i)->player;
  *out_count = count;
  return app->local_player_rows;
}
const char *qa_remote_application_network_phase(const qa_remote_application *app) {
  if (app->primary_peer != NULL)
    return qa_app_network_phase_name(qa_app_remote_seat_network_phase(qa_app_remote_seat_source_network(app->primary_peer->source)));
  switch (app->initial_source.kind) {
    case QA_REMOTE_SOURCE_LIVE: return qa_app_network_phase_name(qa_app_remote_seat_network_phase(&app->initial_source.network));
    case QA_REMOTE_SOURCE_GTV: return qa_app_gtv_remote_source_phase(app->initial_source.watch);
    case QA_REMOTE_SOURCE_RECORDED: return qa_app_recorded_source_phase(app->initial_source.playback);
  }
  return "closed";
}
bool qa_remote_application_read_client_resource(qa_remote_application *app, const char *path, uint8_t **out_bytes, size_t *out_length) {
  qa_content_opened_resource opened;
  if (!qa_content_mounted_open(qa_remote_application_mounts(app), path, NULL, NULL, &opened)) return false;
  *out_bytes = opened.bytes; *out_length = opened.byte_length;
  return true;
}
const qa_app_sim_presentation_event *qa_remote_application_presentation_events(const qa_remote_application *app, size_t *out_count) {
  *out_count = app->source_event_count;
  return app->source_events;
}
const qa_app_unhandled_effect *qa_remote_application_unhandled_presentation_effects(const qa_remote_application *app,
    size_t *out_count) {
  *out_count = app->unhandled_effect_count;
  return app->unhandled_effects;
}
qa_engine_session *qa_remote_application_session(const qa_remote_application *app) { return app->session; }

/* TS print(text, source?). */
void qa_remote_application_print(qa_remote_application *app, const char *text, const qa_command_context *source) {
  if (app->controls == NULL) {
    if (app->host.base.print != NULL) app->host.base.print(app->host.base.ctx, text);
    return;
  }
  const qa_command_context *context = source;
  if (context == NULL && app->client_command_owner != NULL)
    context = qa_command_buffer_execution_context(app->client_command_owner->commands);
  qa_app_application_input_print(app->controls, text, context);
}

/* ---- queueCommand (TS 1585-1590) -------------------------------------- */
void qa_remote_application_queue_command(qa_remote_application *app, const char *name,
    const char *const *args, size_t arg_count, const qa_seat_id *seat, const qa_command_context *source) {
  /* default argument: source = this.clientCommands?.commands.executionContext */
  const qa_command_context *context = source;
  if (context == NULL && app->client_command_owner != NULL)
    context = qa_command_buffer_execution_context(app->client_command_owner->commands);
  if (app->closed || app->closing) qa_raise("Error", 0, 0, "Remote application is closed");
  qa_remote_command command;
  memset(&command, 0, sizeof command);
  if (seat != NULL) { command.has_seat = true; command.seat = *seat; }
  if (!command.has_seat && context != NULL) {
    const qa_command_origin *origin = &context->origin;
    while (origin->kind == QA_COMMAND_ORIGIN_SCRIPT && origin->caller != NULL) origin = origin->caller;
    if (origin->kind == QA_COMMAND_ORIGIN_LOCAL_SEAT) { command.has_seat = true; command.seat = origin->seat; }
  }
  if (app->command_count == app->command_capacity) {
    size_t capacity = app->command_capacity == 0 ? 8 : app->command_capacity * 2;
    qa_remote_command *grown = realloc(app->commands, capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("Out of memory");
    app->commands = grown; app->command_capacity = capacity;
  }
  command.name = dup_or_null(name);
  command.args = xcalloc(arg_count, sizeof *command.args);
  command.arg_count = arg_count;
  for (size_t i = 0; i < arg_count; i++) command.args[i] = dup_or_null(args[i]);
  if (context != NULL) { command.has_source = true; command.source = qa_remote_command_context_copy(context); }
  app->commands[app->command_count++] = command;
}

void qa_remote_application_request_quit(qa_remote_application *app) { app->stopping = true; }

/* ---- requireQwAddress (TS 2508-2511) ---------------------------------- */
qa_net_address qa_remote_require_qw_address(qa_net_address address) {
  if (address.kind == QA_NET_IPX) qa_raise("Error", 0, 0, "QuakeWorld requires UDP");
  return address;
}
