/* Application members that build a graphical seat's clients (application.ts prepareNativeQ2Seat, createQ3SeatClient).
 * GPL-2.0-or-later. */
#include "application_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/render_settings.h"
#include "qa/app/bootstrap/simulation/rerelease_guest_services.h"
#include "qa/app/bootstrap/simulation/rerelease_guest_world.h"
#include "qa/content/q3/equipment/grapple_profiles.h"
#include "qa/core/commands/text.h"

static void *checked(void *pointer) {
  if (pointer == NULL) qa_range_error("Out of memory");
  return pointer;
}
/* C-only: a record the application owns until destroy (TS GC), as the other application files keep theirs. */
static void keep_record(qa_application *app, void *record, void (*release)(void *record)) {
  if (app->seat_record_count == app->seat_record_capacity) {
    size_t capacity = app->seat_record_capacity == 0 ? 4 : app->seat_record_capacity * 2;
    app->seat_records = checked(realloc(app->seat_records, capacity * sizeof *app->seat_records));
    app->seat_record_capacity = capacity;
  }
  app->seat_records[app->seat_record_count].record = record;
  app->seat_records[app->seat_record_count++].release = release;
}

/* ---- prepareNativeQ2Seat(local, assets, simulation, cvars) ---- */
/* descriptor.frame's closure state: the client presentation, its world and simulation, and the frame's view storage. */
typedef struct native_seat_descriptor {
  qa_app_native_q2_client *client;
  qa_app_sim_q2_native_world world;
  qa_app_shared_simulation *simulation;
  double *stats; size_t stat_capacity;
  qa_native_q2_config_string *configs; size_t config_capacity;
} native_seat_descriptor;
static void native_seat_descriptor_release(void *record) {
  native_seat_descriptor *descriptor = record;
  qa_app_native_q2_client_destroy(descriptor->client);
  free(descriptor->stats);
  free(descriptor->configs);
}
/* frame: () => ({ protocol, stats, configstrings, layout, inventory, playerNumber, serverFrame, timeMilliseconds,
 * frameTimeMilliseconds }): the views stay valid until the next call. */
static qa_native_q2_hud_frame native_seat_frame(void *ctx) {
  native_seat_descriptor *d = ctx;
  qa_native_q2_hud_frame frame;
  memset(&frame, 0, sizeof frame);
  bool rerelease = d->world.rerelease;
  frame.protocol.kind = rerelease ? "q2-rerelease" : "q2-classic";
  frame.protocol.version = rerelease ? 1038 : 34;
  qa_app_native_q2_player_state state = qa_app_native_q2_client_player_state(d->client);
  qa_double_view stats = rerelease ? state.rerelease_state.view.stats : state.classic.view.stats;
  if (stats.count > d->stat_capacity) {
    free(d->stats);
    d->stats = checked(calloc(stats.count, sizeof *d->stats));
    d->stat_capacity = stats.count;
  }
  if (stats.count != 0) memcpy(d->stats, stats.values, stats.count * sizeof *d->stats);
  frame.stats = d->stats; frame.stat_count = stats.count;
  size_t config_count = 0;
  const qa_app_sim_configstring *configs = qa_app_native_q2_client_configstrings(d->client, &config_count);
  if (config_count > d->config_capacity) {
    free(d->configs);
    d->configs = checked(calloc(config_count, sizeof *d->configs));
    d->config_capacity = config_count;
  }
  for (size_t i = 0; i < config_count; i++) d->configs[i] = (qa_native_q2_config_string){configs[i].index, configs[i].value};
  frame.configstrings = d->configs; frame.configstring_count = config_count;
  frame.layout = qa_app_native_q2_client_layout(d->client);
  frame.inventory = qa_app_native_q2_client_inventory(d->client, &frame.inventory_count);
  frame.player_number = qa_app_native_q2_client_source_slot(d->client) - 1;
  qa_app_sim_rerelease_guest_services *services =
      rerelease ? qa_app_sim_rerelease_guest_world_services(d->world.rerelease_world) : NULL;
  frame.server_frame = rerelease ? qa_app_sim_rerelease_guest_services_server_frame(services)
                                 : qa_app_shared_sim_current_output(d->simulation).snapshot.frame.frame;
  frame.time_milliseconds = qa_app_shared_sim_time_seconds(d->simulation) * 1000;
  frame.has_frame_time_milliseconds = true;
  frame.frame_time_milliseconds = rerelease ? qa_app_sim_rerelease_guest_services_options_of(services)->frame_milliseconds : 100;
  return frame;
}
typedef struct native_preload { qa_application *app; qa_app_application_effects *effects; } native_preload;
static void native_preload_run(void *ctx) {
  native_preload *preload = ctx;
  qa_application_preload_world_effects(preload->app, preload->effects);
}
static void native_effects_close(void *ctx) { qa_app_application_effects_close(ctx); }
bool qa_application_prepare_native_q2_seat(qa_application *app, qa_app_local_input *local, qa_app_application_assets *assets,
    qa_app_shared_simulation *simulation, qa_cvar_registry *cvars, qa_application_native_q2_seat_client *out) {
  qa_app_sim_classic_guest_world *classic = NULL;
  qa_app_sim_rerelease_guest_world *rerelease = NULL;
  if (!qa_app_shared_sim_q2_native(simulation, &classic, &rerelease)) return false;
  if (cvars == NULL) qa_raise("Error", 0, 0, "Native local player has no userinfo registry");
  qa_app_sim_q2_native_world world = {rerelease != NULL, classic, rerelease};
  double slot = (double)qa_session_client_id(qa_session_seat_client(local->player.seat)).slot + 1;
  qa_app_native_q2_client *client = qa_app_native_q2_client_create(world, slot, local->player.actor,
      qa_app_shared_sim_recipe(simulation)->map.entities.content);
  if (client == NULL) qa_range_error("Out of memory");
  native_seat_descriptor *descriptor = calloc(1, sizeof *descriptor);
  if (descriptor == NULL) { qa_app_native_q2_client_destroy(client); qa_range_error("Out of memory"); }
  descriptor->client = client;
  descriptor->world = world;
  descriptor->simulation = simulation;
  keep_record(app, descriptor, native_seat_descriptor_release);
  qa_app_application_effects *effects = qa_application_create_world_effects(app, assets, simulation, simulation->options.seed);
  native_preload preload = {app, effects};
  qa_caught_error error;
  if (!qa_try(native_preload_run, &preload, &error)) {
    qa_caught_error cleanup;
    if (!qa_try(native_effects_close, effects, &cleanup))
      qa_raise("AggregateError", 0, 0, "Native seat effects preparation failed");
    qa_rethrow(&error);
  }
  memset(out, 0, sizeof *out);
  out->client = client;
  out->source_slot = slot;
  out->effects = effects;
  out->cvars = cvars;
  out->userinfo = checked(qa_cvar_registry_info_string(cvars, QA_CVAR_FLAG_USER_INFO,
                                                       qa_cvar_registry_dialect(cvars) == QA_COMMAND_DIALECT_Q3 ? 1024 : 512));
  out->descriptor_frame = native_seat_frame;
  out->descriptor_ctx = descriptor;
  return true;
}

/* ---- createQ3SeatClient(local, assets, audio, input, renderer, simulation, settings?, localGuest = this.localGuest,
 * browser = this.guestBrowser, cvars = this.clientCvars.get(local.player.seat.id)) ---- */
/* The closures of one created client (the C client shares one ctx among its option members). binding comes first: the
 * native client's local-round members (linkBounds, sourceActor, predictionCommand, serverSettings) take the record as a
 * qa_application_q3_round_binding. */
typedef struct q3_seat_record {
  qa_application_q3_round_binding binding;           /* native: source and the prediction adapter */
  qa_application *app;
  qa_app_local_input *local;
  qa_app_application_assets *assets;
  qa_app_application_input *input;
  qa_app_native_renderer *renderer;
  qa_app_shared_simulation *simulation;
  qa_application_local_q3_guest_world *local_guest;  /* qvm: the captured localGuest */
  qa_app_q3_qvm_server_game *guest;                  /* qvm: the captured guest */
  qa_app_q3_source_runtime *source;                  /* native: the captured source */
  qa_app_local_q3_client_state *state;               /* qvm */
  bool has_equipment_profile;                        /* qvm: equipmentProfile !== null */
  qa_app_component_media_host media;                 /* primaryShaderControl(simulation.events, assets, ...) */
  qa_command_context seat_context;                   /* { session, origin: { kind: "local-seat", seat, client } } */
  qa_command_origin script_origin;                   /* { kind: "script", name: "q3-cgame", caller: seat origin } */
  qa_app_q3_client_cvars *guest_cvars;               /* qvm: input.guestCvars(seat) */
  qa_app_q3_source_presentation_state *initial;      /* native: source.sourceState() */
} q3_seat_record;
static void q3_seat_record_release(void *record) {
  q3_seat_record *r = record;
  qa_application_round_binding_release(&r->binding);
  if (r->guest_cvars != NULL) qa_app_q3_client_cvars_destroy(r->guest_cvars);
  if (r->initial != NULL) qa_app_q3_source_presentation_state_destroy(r->initial);
}
static qa_seat_id record_seat(const q3_seat_record *r) { return qa_session_seat_id(r->local->player.seat); }
static qa_session_client *record_client(const q3_seat_record *r) { return qa_session_seat_client(r->local->player.seat); }
/* saveFontData: () => (input.sharedCvars?.variableValue("r_saveFontData") ?? 0) !== 0 */
static bool q3_save_font_data(void *ctx) {
  qa_cvar_registry *shared = qa_app_application_input_shared_cvars(((q3_seat_record *)ctx)->input);
  return (shared == NULL ? 0 : qa_cvar_registry_variable_value(shared, "r_saveFontData")) != 0;
}
/* renderSettings: () => q3RendererSettings(input.sharedCvars) */
static qa_app_q3_renderer_settings q3_render_settings(void *ctx) {
  return qa_app_q3_renderer_settings_from(qa_app_application_input_shared_cvars(((q3_seat_record *)ctx)->input));
}
/* get splitScreen() { return input.locals.length > 1; } */
static bool q3_split_screen(void *ctx) { return qa_app_application_input_local_count(((q3_seat_record *)ctx)->input) > 1; }
/* viewport: () => { const size = renderer.window.drawableSize; return this.localViewport(seat, size.width, size.height); } */
static qa_rect q3_viewport(void *ctx) {
  q3_seat_record *r = ctx;
  qa_sdl_size size = qa_sdl_window_drawable_size(qa_app_native_renderer_window(r->renderer));
  return qa_application_local_viewport(r->app, record_seat(r), size.width, size.height);
}
static double q3_now(void *ctx) {
  (void)ctx; /* TS performance.now() captures nothing */
  return qa_application_performance_now();
}
/* console: text => input.enqueueClientCommand(text, { session, origin: { kind: "script", name: "q3-cgame", caller } }) */
static void q3_console(void *ctx, const char *text) {
  q3_seat_record *r = ctx;
  qa_command_context source;
  memset(&source, 0, sizeof source);
  source.session = r->seat_context.session;
  source.origin = r->script_origin;
  qa_app_application_input_enqueue_client_command(r->input, text, &source);
}
/* print: text => { this.host.print(text); local.console.print(text); } */
static void q3_print(void *ctx, const char *text) {
  q3_seat_record *r = ctx;
  qa_application_print(r->app, text);
  qa_seat_console_print(r->local->console, text);
}
/* tokenizeCommand(text, "q3").argv as an owned { target: "client", name, arguments_, seat } request; false when empty. */
static bool client_request(const q3_seat_record *r, const char *text, qa_application_command_request *out) {
  qa_command_tokens tokens;
  if (!qa_tokenize_command(text, QA_COMMAND_DIALECT_Q3, QA_COMMAND_TEXT_SOURCE, &tokens)) qa_range_error("Out of memory");
  if (tokens.argc == 0) { qa_command_tokens_destroy(&tokens); return false; }
  memset(out, 0, sizeof *out);
  out->target = QA_APPLICATION_TARGET_CLIENT;
  out->name = checked(strdup(tokens.argv[0]));
  out->argument_count = tokens.argc - 1;
  out->arguments = out->argument_count == 0 ? NULL : checked(calloc(out->argument_count, sizeof *out->arguments));
  for (size_t i = 1; i < tokens.argc; i++) out->arguments[i - 1] = checked(strdup(tokens.argv[i]));
  out->has_seat = true;
  out->seat = record_seat(r);
  qa_command_tokens_destroy(&tokens);
  return true;
}
/* remapShader: primaryShaderControl(simulation.events, assets, sourceCurrent): the TS promise rejects with the shader
 * preparation's error. */
static void q3_remap_shader(q3_seat_record *r, bool (*source_current)(void *ctx), const char *original, const char *replacement,
    const char *offset, bool initializing, bool (*current)(void *current_ctx), void *current_ctx) {
  const char *error = qa_app_primary_shader_control(r->media,
      qa_app_loaded_content_recipe(qa_app_application_assets_content(r->assets))->engine_behavior.content, source_current, r,
      original, replacement, offset, initializing, current, current_ctx);
  if (error != NULL) qa_raise("Error", 0, 0, "%s", error);
}
static bool actor_live(const q3_seat_record *r) {
  return qa_session_actor_registry_is_live(qa_app_shared_sim_actors(r->simulation), r->local->player.actor);
}

/* -- the QVM branch -- */
/* () => simulation.q3Guest() === guest && !guest.isRetired && !local.player.seat.client.isClosed && simulation.actors.isLive(actor) */
static bool guest_source_current(void *ctx) {
  q3_seat_record *r = ctx;
  return qa_app_shared_sim_q3_guest(r->simulation) == r->guest && !qa_app_q3_qvm_server_game_is_retired(r->guest)
      && !qa_session_client_is_closed(record_client(r)) && actor_live(r);
}
static void guest_remap_shader(void *ctx, const char *original, const char *replacement, const char *offset, bool initializing,
    bool (*current)(void *current_ctx), void *current_ctx) {
  q3_remap_shader(ctx, guest_source_current, original, replacement, offset, initializing, current, current_ctx);
}
/* player === null ? null : { primaryWeapon: guest.records.player(player.sourceEntity).weapon, warning: playerUi.arsenalWarning } */
static bool guest_equipment_player(q3_seat_record *r, qa_app_q3_equipment_presentation *out) {
  qa_app_q3_player player;
  if (!qa_app_q3_qvm_server_game_player(r->guest, qa_session_client_id(record_client(r)), &player)) return false;
  qa_q3_player_state state;
  double stats[16], persistent[16], powerups[16], ammo[16];
  qa_app_q3_guest_records_player(qa_app_q3_qvm_server_game_records(r->guest), player.source_entity, &state, stats, persistent,
      powerups, ammo);
  out->primary_weapon = state.weapon;
  out->warning = qa_app_shared_sim_player_ui(r->simulation, r->local->player.actor).arsenal_warning;
  return true;
}
static bool guest_equipment_weapon(void *ctx, qa_app_q3_equipment_presentation *out) {
  q3_seat_record *r = ctx;
  const qa_grapple_selection *grapple = &qa_app_shared_sim_recipe(r->simulation)->equipment.grapple;
  qa_arsenal_state arsenal;
  bool owned = false;
  if (qa_app_shared_sim_arsenal_state(r->simulation, r->local->player.actor, &arsenal, &owned)) {
    if (owned) free((void *)arsenal.ammo);
    return guest_equipment_player(r, out);
  }
  if (!r->has_equipment_profile || !grapple->is_enabled || strcmp(grapple->binding, "slot") != 0) return false;
  const qa_weapon_reference *selected = qa_app_shared_sim_weapon_slot(r->simulation, r->local->player.actor).active;
  if (selected == NULL || strcmp(selected->provider, grapple->source.provider) != 0
      || strcmp(selected->item, "q3:weapon_grapplinghook") != 0)
    return false;
  return guest_equipment_player(r, out);
}
/* assertCurrent: () => { if (simulation.q3Guest()?.isRetired !== false || local.player.seat.client.isClosed) throw ... } */
static void guest_assert_current(void *ctx) {
  q3_seat_record *r = ctx;
  qa_app_q3_qvm_server_game *guest = qa_app_shared_sim_q3_guest(r->simulation);
  if (guest == NULL || qa_app_q3_qvm_server_game_is_retired(guest) || qa_session_client_is_closed(record_client(r)))
    qa_raise("Error", 0, 0, "Guest presentation world is retired");
}
/* clientState: () => ({ phase: 8, connectPacketCount: 0, clientNumber: state.clientNumber, serverName: "localhost", message: "" }) */
static qa_app_qvm_client_state_info guest_client_state(void *ctx) {
  q3_seat_record *r = ctx;
  qa_app_qvm_client_state_info info;
  memset(&info, 0, sizeof info);
  info.phase = 8;
  info.connect_packet_count = 0;
  info.client_number = qa_app_local_q3_client_state_client_number(r->state);
  info.server_name = "localhost";
  info.message = "";
  return info;
}
/* reliable: text => { const [name, ...args] = tokenizeCommand(text, "q3").argv; if (name !== undefined)
 * this.queueLocalGuestCommand(localGuest, { target: "client", name, arguments_: args, seat }); } */
static void guest_reliable(void *ctx, const char *text) {
  q3_seat_record *r = ctx;
  qa_application_command_request request;
  if (client_request(r, text, &request)) qa_application_queue_local_guest_command(r->app, r->local_guest, &request);
}
/* commandBuffer: input.guestCommands. */
static double guest_execute_now(void *ctx, const char *text, const qa_command_context *source) {
  return qa_app_application_input_guest_execute_now(((q3_seat_record *)ctx)->input, text, source);
}
static void guest_insert(void *ctx, const char *text, const qa_command_context *source) {
  qa_app_application_input_guest_insert(((q3_seat_record *)ctx)->input, text, source);
}
static void guest_append(void *ctx, const char *text, const qa_command_context *source) {
  qa_app_application_input_guest_append(((q3_seat_record *)ctx)->input, text, source, -1);
}

/* -- the native branch -- */
/* weaponHud: () => { const ui = simulation.playerUi(actor); return { status: ui.weaponStatus, warning: ui.arsenalWarning }; } */
static qa_q3_pres_weapon_hud_reading native_weapon_hud(void *ctx) {
  q3_seat_record *r = ctx;
  qa_app_sim_player_ui ui = qa_app_shared_sim_player_ui(r->simulation, r->local->player.actor);
  qa_q3_pres_weapon_hud_reading reading;
  memset(&reading, 0, sizeof reading);
  reading.has_status = ui.weapon_status != NULL;
  if (ui.weapon_status != NULL) reading.status = *ui.weapon_status;
  reading.warning = ui.arsenal_warning;
  return reading;
}
/* () => simulation.q3Source() === source && !local.player.seat.client.isClosed && simulation.actors.isLive(actor) */
static bool native_source_current(void *ctx) {
  q3_seat_record *r = ctx;
  return qa_app_shared_sim_q3_source(r->simulation) == r->source && !qa_session_client_is_closed(record_client(r)) && actor_live(r);
}
static void native_remap_shader(void *ctx, const char *original, const char *replacement, const char *offset, bool initializing,
    bool (*current)(void *current_ctx), void *current_ctx) {
  q3_remap_shader(ctx, native_source_current, original, replacement, offset, initializing, current, current_ctx);
}
/* reliable: text => input.enqueueClientReliable(text, seatContext, (text, source) => { ...;
 * if (name !== undefined) this.requestedCommands.admit({ target: "client", name, arguments_: args, seat, source }); }) */
static void native_dispatch_reliable(void *ctx, const char *text, const qa_command_context *source) {
  q3_seat_record *r = ctx;
  qa_application_command_request request;
  if (!client_request(r, text, &request)) return;
  request.has_source = true;
  request.source = qa_command_context_copy(source);
  (void)qa_event_ring_admit(&r->app->requested_commands, &request);
}
static void native_reliable(void *ctx, const char *text) {
  q3_seat_record *r = ctx;
  qa_app_application_input_enqueue_client_reliable(r->input, text, &r->seat_context, native_dispatch_reliable, r);
}

static void prediction_destroy(void *ctx) { qa_app_simulation_prediction_host_destroy(ctx); }
bool qa_application_create_q3_seat_client(qa_application *app, qa_app_local_input *local, qa_app_application_assets *assets,
    qa_app_application_audio *audio, qa_app_application_input *input, qa_app_native_renderer *renderer, qa_app_shared_simulation *simulation,
    const qa_cvar_snapshot *settings, size_t setting_count, qa_application_local_q3_guest_world *local_guest,
    qa_application_local_q3_guest_browser *browser, qa_cvar_registry *cvars, qa_application_q3_seat_client *out) {
  qa_seat_id seat = qa_session_seat_id(local->player.seat);
  qa_client_id client = qa_session_client_id(qa_session_seat_client(local->player.seat));
  qa_app_q3_qvm_server_game *guest = qa_app_shared_sim_q3_guest(simulation);
  qa_app_q3_source_runtime *source = guest == NULL ? qa_app_shared_sim_q3_source(simulation) : NULL;
  if (guest == NULL && source == NULL) return false;
  q3_seat_record *r = checked(calloc(1, sizeof *r));
  keep_record(app, r, q3_seat_record_release);
  r->app = app;
  r->local = local;
  r->assets = assets;
  r->input = input;
  r->renderer = renderer;
  r->simulation = simulation;
  r->seat_context.session = qa_engine_session_id(app->session);
  r->seat_context.origin.kind = QA_COMMAND_ORIGIN_LOCAL_SEAT;
  r->seat_context.origin.seat = seat;
  r->seat_context.origin.client = client;
  r->script_origin.kind = QA_COMMAND_ORIGIN_SCRIPT;
  r->script_origin.name = "q3-cgame";
  r->script_origin.caller = &r->seat_context.origin;
  qa_app_application_q3_client_options options;
  memset(&options, 0, sizeof options);
  options.ctx = r;
  options.save_font_data = q3_save_font_data;
  options.render_settings = q3_render_settings;
  if (guest != NULL) {
    qa_application_local_q3_guest_seat *guest_seat = NULL;
    for (size_t i = 0; local_guest != NULL && i < local_guest->seat_count && guest_seat == NULL; i++)
      if (qa_same_seat(local_guest->seat_ids[i], seat)) guest_seat = &local_guest->seats[i];
    if (local_guest == NULL || guest_seat == NULL || browser == NULL) qa_raise("Error", 0, 0, "Local guest client services are not prepared");
    r->local_guest = local_guest;
    r->guest = guest;
    r->state = guest_seat->state; /* const { state, cvars } = seat */
    qa_app_key_profile *keys = qa_application_prepare_keys(app, qa_app_application_assets_content(assets), simulation);
    const qa_app_prepared_q3_game *prepared = qa_app_loaded_content_prepared_q3_game(qa_app_application_assets_content(assets));
    if (prepared != NULL && qa_app_prepared_q3_equipment_artifact(prepared) != NULL) { /* prepared.kind === "qvm" */
      qa_qvm_grapple_definition definition;
      r->has_equipment_profile = qa_q3_grapple_profile(qa_app_prepared_q3_equipment_artifact(prepared), &definition);
    }
    options.kind = QA_APP_Q3_CLIENT_QVM;
    options.keys = keys;
    options.local_server = true;
    options.source = qa_app_local_q3_client_state_source(r->state);
    options.connection = qa_app_local_q3_client_state_connection(r->state);
    options.cvars = guest_seat->cvars;
    options.equipment_weapon = guest_equipment_weapon;
    options.system_cinematics = qa_application_system_cinematics(app, simulation, qa_app_application_assets_content(assets), assets,
        audio, renderer, seat, input, NULL);
    r->media = qa_app_component_media_host_of(simulation, audio, assets);
    qa_app_primary_shader_control_enable(r->media);
    options.remap_shader = guest_remap_shader;
    options.assets = assets;
    options.queries = simulation->scene;
    options.local = local;
    options.audio = audio;
    options.renderer = qa_app_native_renderer_qvm_display(renderer);
    options.browser = browser->view;
    options.command_buffer = (qa_app_qvm_command_target){r, guest_execute_now, guest_insert, guest_append};
    r->guest_cvars = qa_app_application_input_guest_cvars(input, seat);
    options.guest_cvars = qa_app_q3_client_cvars_services(r->guest_cvars);
    options.guest_input = qa_app_application_input_guest_input(input, seat);
    options.command_bindings = qa_app_application_input_client_commands(input);
    options.command_registration = qa_app_application_input_client_command_registration(input, seat, NULL);
    options.split_screen = q3_split_screen;
    options.time_cvars = qa_app_q3_server_state_cvars(qa_app_q3_qvm_server_game_state(guest));
    options.assert_current = guest_assert_current;
    options.client_state = guest_client_state;
    options.viewport = q3_viewport;
    options.now = q3_now;
    options.reliable = guest_reliable;
    options.console = q3_console;
    options.print = q3_print;
    qa_app_application_q3_client *created = qa_app_application_q3_client_create(&options, NULL);
    if (created == NULL) qa_range_error("Out of memory");
    guest_seat->client = created; /* seat.client = client */
    memset(out, 0, sizeof *out);
    out->qvm = true;
    out->client = created;
    out->actor = local->player.actor;
    out->state = r->state;
    return true;
  }
  r->source = source;
  r->binding.source = source;
  qa_app_simulation_prediction_host *prediction = qa_app_create_simulation_prediction_host(qa_app_shared_sim_prediction_source(simulation),
      local->player.actor, seat);
  if (prediction == NULL) qa_range_error("Out of memory");
  qa_cleanup prediction_cleanup; /* the prediction is the seat client's only once the client exists (TS GC) */
  qa_cleanup_push(&prediction_cleanup, prediction_destroy, prediction);
  r->initial = qa_app_q3_source_runtime_source_state(source);
  if (r->initial == NULL) qa_range_error("Out of memory");
  qa_app_simulation_prediction_host_capture_source(prediction, r->initial);
  r->binding.adapter = qa_app_simulation_prediction_host_adapter(prediction);
  options.kind = QA_APP_Q3_CLIENT_LOCAL;
  options.weapon_hud = native_weapon_hud;
  options.assets = assets;
  options.renderer = qa_app_native_renderer_qvm_display(renderer);
  options.queries = simulation->scene;
  options.initial = r->initial;
  options.local = local;
  options.audio = audio;
  options.movement = qa_app_presentation_prediction_adapter_host(r->binding.adapter);
  options.system_cinematics = qa_application_system_cinematics(app, simulation, qa_app_application_assets_content(assets), assets,
      audio, renderer, seat, input, NULL);
  r->media = qa_app_component_media_host_of(simulation, audio, assets);
  qa_app_primary_shader_control_enable(r->media);
  options.remap_shader = native_remap_shader;
  options.command_bindings = qa_app_application_input_client_commands(input);
  options.command_registration = qa_app_application_input_client_command_registration(input, seat, NULL);
  options.split_screen = q3_split_screen;
  options.time_cvars = qa_app_q3_source_runtime_host(source)->cvars;
  options.cvars = cvars; /* ...(cvars === undefined ? {} : { cvars }) */
  if (settings != NULL) { options.settings = settings; options.setting_count = setting_count; options.has_settings = true; }
  options.prediction_command = qa_application_round_prediction_command;
  options.link_bounds = qa_application_round_link_bounds;
  options.source_actor = qa_application_round_source_actor;
  options.server_settings = qa_application_round_server_settings;
  options.viewport = q3_viewport;
  options.now = q3_now;
  options.reliable = native_reliable;
  options.console = q3_console;
  options.print = q3_print;
  qa_app_application_q3_client *created = qa_app_application_q3_client_create(&options, NULL);
  if (created == NULL) qa_range_error("Out of memory");
  qa_cleanup_pop(&prediction_cleanup);
  memset(out, 0, sizeof *out);
  out->qvm = false;
  out->client = created;
  out->actor = local->player.actor;
  out->prediction = prediction;
  return true;
}
