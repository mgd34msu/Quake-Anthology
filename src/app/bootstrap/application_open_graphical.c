/* Application members that open the graphical source and publish a borrowed one (application.ts openGraphical,
 * publishBorrowed) and the application as its client's ClientSourceLifetime. GPL-2.0-or-later. */
#include "application_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/menu_art.h"
#include "qa/app/bootstrap/player_userinfo.h"
#include "qa/app/bootstrap/shared_setting_cvars.h"
#include "qa/foundation/js.h"
#include "qa/network/common/js_number.h"

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
static void stage(qa_application *app, const char *message) {
  if (app->host.loading != NULL) app->host.loading->stage(app->host.loading->ctx, message);
}
static void host_print(void *ctx, const char *text) { qa_application_print(ctx, text); }
/* `${cvars.infoString(CvarFlag.UserInfo)}\\ip\\localhost`. Malloc'd. */
static char *localhost_userinfo(qa_cvar_registry *cvars) {
  char *info = checked(qa_cvar_registry_info_string(cvars, QA_CVAR_FLAG_USER_INFO, -1));
  size_t length = strlen(info) + sizeof "\\ip\\localhost";
  char *text = checked(malloc(length));
  (void)snprintf(text, length, "%s\\ip\\localhost", info);
  free(info);
  return text;
}
/* localSeats.set(client.id, seat). */
static void local_seat_set(qa_application *app, qa_client_id client, qa_session_seat *seat) {
  for (size_t i = 0; i < app->local_seat_count; i++)
    if (qa_same_client(app->local_seat_clients[i], client)) { app->local_seats[i] = seat; return; }
  if (app->local_seat_count == app->local_seat_capacity) {
    app->local_seat_capacity = app->local_seat_capacity == 0 ? 4 : app->local_seat_capacity * 2;
    app->local_seat_clients = checked(realloc(app->local_seat_clients, app->local_seat_capacity * sizeof *app->local_seat_clients));
    app->local_seats = checked(realloc(app->local_seats, app->local_seat_capacity * sizeof *app->local_seats));
  }
  app->local_seat_clients[app->local_seat_count] = client;
  app->local_seats[app->local_seat_count++] = seat;
}
/* sourceConnections.set(client, connection). */
static void source_connection_set(qa_application *app, qa_session_client *client, qa_session_connection *connection) {
  for (size_t i = 0; i < app->source_connection_count; i++)
    if (app->source_connection_clients[i] == client) { app->source_connections[i] = connection; return; }
  if (app->source_connection_count == app->source_connection_capacity) {
    app->source_connection_capacity = app->source_connection_capacity == 0 ? 4 : app->source_connection_capacity * 2;
    app->source_connection_clients = checked(realloc(app->source_connection_clients,
        app->source_connection_capacity * sizeof *app->source_connection_clients));
    app->source_connections = checked(realloc(app->source_connections, app->source_connection_capacity * sizeof *app->source_connections));
  }
  app->source_connection_clients[app->source_connection_count] = client;
  app->source_connections[app->source_connection_count++] = connection;
}
/* clientCvars.set(seat, cvars). */
static void client_cvars_set(qa_application *app, qa_seat_id seat, qa_cvar_registry *cvars) {
  for (size_t i = 0; i < app->client_cvar_count; i++)
    if (qa_same_seat(app->client_cvar_seats[i], seat)) { app->client_cvars[i] = cvars; return; }
  if (app->client_cvar_count == app->client_cvar_capacity) {
    app->client_cvar_capacity = app->client_cvar_capacity == 0 ? 4 : app->client_cvar_capacity * 2;
    app->client_cvar_seats = checked(realloc(app->client_cvar_seats, app->client_cvar_capacity * sizeof *app->client_cvar_seats));
    app->client_cvars = checked(realloc(app->client_cvars, app->client_cvar_capacity * sizeof *app->client_cvars));
  }
  app->client_cvar_seats[app->client_cvar_count] = seat;
  app->client_cvars[app->client_cvar_count++] = cvars;
}
static qa_audio_output_format target_output_format(void *ctx) { return qa_app_application_audio_output_format(ctx); }
static void target_select_output_format(void *ctx, qa_audio_output_format format) { qa_app_application_audio_select_output_format(ctx, format); }
/* applyAudioOutputSettings(cvars, audio): the ApplicationAudio as the output target. */
static void apply_audio_output(qa_cvar_registry *cvars, qa_app_application_audio *audio) {
  qa_app_audio_output_target target = { audio, target_output_format, target_select_output_format };
  qa_app_apply_audio_output_settings(cvars, target);
}

/* ================= openGraphical(save?) ================= */
/* The renderer owner of an owned application ({ identity: Symbol("application renderer"), session, generation: 0 }), its
 * root image registry and the renderer it opens. The application frees them at destroy (TS GC). */
typedef struct graphical_owner {
  char identity;
  qa_scene_image_registry *root_images; bool owns_root_images;
  qa_app_native_renderer *renderer; bool owns_renderer;
  bool adopted;   /* a GraphicalApplication record tears these down after its seats (graphical_release) */
  bool torn_down;
} graphical_owner;
static void graphical_owner_teardown(graphical_owner *owner) {
  if (owner->torn_down) return;
  owner->torn_down = true;
  if (owner->owns_renderer && owner->renderer != NULL) qa_app_native_renderer_destroy(owner->renderer);
  if (owner->owns_root_images && owner->root_images != NULL) {
    qa_scene_image_registry_destroy(owner->root_images);
    free(owner->root_images);
  }
}
/* The kept record is a box: an adopted owner belongs to the GraphicalApplication record, which frees it. */
typedef struct graphical_owner_box { graphical_owner *owner; } graphical_owner_box;
static void graphical_owner_release(void *record) {
  graphical_owner *owner = ((graphical_owner_box *)record)->owner;
  if (owner->adopted) return;
  graphical_owner_teardown(owner);
  free(owner);
}
/* GraphicalApplication storage and the owners openGraphical created (TS GC after close): the seat clients and their
 * prediction hosts, seat UIs, native seat effects, effects, the rerelease presentation, input, audio and assets, then the
 * renderer owner (the assets' image registry forks its root registry). */
typedef struct graphical_record {
  qa_application_graphical graphical; /* first: app->graphical points here */
  graphical_owner *owner;
} graphical_record;
static void graphical_release(void *record) {
  graphical_record *kept = record;
  qa_application_graphical *graphical = &kept->graphical;
  for (size_t i = 0; i < graphical->q3_count; i++) {
    qa_app_application_q3_client_destroy(graphical->q3[i].client);
    if (graphical->q3[i].prediction != NULL) qa_app_simulation_prediction_host_destroy(graphical->q3[i].prediction);
  }
  /* The seat presentations closed their UIs when they closed (and were freed by their cleanup). */
  for (size_t i = 0; i < graphical->presentation_count; i++) qa_app_seat_ui_destroy(graphical->presentations[i].ui);
  for (size_t i = 0; i < graphical->native_q2_count; i++) qa_app_application_effects_destroy(graphical->native_q2[i].effects);
  qa_app_application_effects_destroy(graphical->effects);
  qa_app_rerelease_presentation_destroy(graphical->rerelease);
  qa_app_application_input_destroy(graphical->input);
  qa_app_application_audio_destroy(graphical->audio);
  qa_app_application_assets_destroy(graphical->assets);
  free(graphical->presentations);
  free(graphical->q3_seats); free(graphical->q3);
  free(graphical->native_q2_seats); free(graphical->native_q2);
  if (kept->owner != NULL) { graphical_owner_teardown(kept->owner); free(kept->owner); }
}
/* loadMenuArtImage as loadNativeUiArt's reader. */
static bool menu_art_read(void *ctx, const char *path, qa_image_level *out) {
  (void)ctx; /* the reader closure captures nothing */
  qa_app_load_menu_art_image(path, out);
  return true;
}
static double performance_now(void *ctx) {
  (void)ctx; /* TS performanceNow captures nothing */
  return qa_application_performance_now();
}
/* startupReader: () => configuration.read (the configuration openGraphical captured). */
static bool startup_reader(void *ctx, qa_app_console_script_files *scripts, const qa_app_options *options,
    qa_app_prepared_scoped_read_fn *out_read, void **out_read_ctx) {
  (void)scripts; (void)options; /* the TS closure ignores its arguments */
  qa_application_input_actions *actions = ctx;
  *out_read = qa_app_prepared_profile_read(actions->startup_configuration, out_read_ctx);
  return true;
}
/* presentationClock: () => this.presentationMilliseconds ?? this.elapsed. */
static double presentation_clock(void *ctx) {
  qa_application *app = ctx;
  return app->has_presentation_milliseconds ? app->presentation_milliseconds : app->elapsed;
}
/* () => presentation.close(): the C presentation is freed with it (TS GC). */
static void presentation_cleanup(void *ctx) {
  qa_app_world_seat_presentation_close(ctx);
  qa_app_world_seat_presentation_destroy(ctx);
}

typedef struct open_graphical {
  qa_application *app;
  const qa_save_image *save;
  bool restoring;
  qa_app_client_bootstrap *client;
  graphical_owner *owner;
  qa_renderer_resource_owner renderer_owner;
  qa_scene_image_registry *root_images;
  qa_app_application_assets *assets;
  qa_app_native_renderer *renderer;
  qa_app_application_input *input;
  qa_app_application_audio *audio;
  qa_native_ui_art *art;
  qa_app_application_effects *effects;
  qa_app_application_q3_client **source_clients; size_t source_client_count;
  qa_application_native_q2_seat_client *native_q2; size_t native_q2_count;
  qa_app_local_player *players; size_t player_count, player_capacity;
} open_graphical;
static void push_player(open_graphical *g, qa_session_seat *seat, qa_actor_id actor) {
  if (g->player_count == g->player_capacity) {
    g->player_capacity = g->player_capacity == 0 ? 4 : g->player_capacity * 2;
    g->players = checked(realloc(g->players, g->player_capacity * sizeof *g->players));
  }
  g->players[g->player_count].seat = seat;
  g->players[g->player_count++].actor = actor;
}
/* The restored players: for (const seat of this.localSeats.values()) ... */
typedef struct archive_rows { qa_cvar_archive_entry *entries; size_t count; } archive_rows;
static void archive_rows_free(void *ctx) {
  archive_rows *rows = ctx;
  qa_app_cvar_archive_entries_free(rows->entries, rows->count);
  rows->entries = NULL;
  rows->count = 0;
}
static void restored_players(open_graphical *g) {
  qa_application *app = g->app;
  qa_app_shared_simulation *simulation = app->world_simulation;
  for (size_t index = 0; index < app->local_seat_count; index++) {
    qa_session_seat *seat = app->local_seats[index];
    archive_rows archive = { NULL, 0 };
    archive.entries = qa_application_load_client_archive(app, seat, app->loaded_content, NULL, &archive.count);
    qa_cleanup archive_cleanup;
    qa_cleanup_push(&archive_cleanup, archive_rows_free, &archive);
    if (app->local_guest != NULL) {
      qa_actor_id actor = qa_application_prepare_restored_guest_seat(app, app->local_guest, simulation, seat, app->loaded_content,
          &app->launch_options, NULL, archive.entries, archive.count);
      push_player(g, seat, actor);
      qa_cvar_registry *cvars = NULL;
      for (size_t i = 0; i < app->local_guest->seat_count && cvars == NULL; i++)
        if (qa_same_seat(app->local_guest->seat_ids[i], qa_session_seat_id(seat))) cvars = app->local_guest->seats[i].cvars;
      if (cvars == NULL) qa_raise("Error", 0, 0, "Restored guest seat cvars are missing");
      client_cvars_set(app, qa_session_seat_id(seat), cvars);
    } else {
      char *userinfo = qa_application_restored_userinfo(simulation, seat);
      qa_cleanup userinfo_cleanup;
      qa_cleanup_push(&userinfo_cleanup, free, userinfo);
      qa_cvar_registry *cvars = qa_application_create_client_cvars(app, simulation, app->loaded_content, &app->launch_options, seat,
          userinfo, NULL, archive.entries, archive.count, false);
      qa_cleanup_pop(&userinfo_cleanup);
      free(userinfo);
      client_cvars_set(app, qa_session_seat_id(seat), cvars);
      size_t count = 0;
      const qa_actor_id *players = qa_app_shared_sim_players(simulation, &count);
      const qa_actor_id *found = NULL;
      qa_client_id seat_client = qa_session_client_id(qa_session_seat_client(seat));
      for (size_t i = 0; i < count && found == NULL; i++) {
        qa_client_id owner;
        if (qa_app_shared_sim_player_client(simulation, players[i], &owner) && qa_same_client(owner, seat_client)) found = &players[i];
      }
      if (found == NULL) qa_raise("Error", 0, 0, "Restored world is missing a local player");
      push_player(g, seat, *found);
    }
    qa_cleanup_pop(&archive_cleanup);
    archive_rows_free(&archive);
  }
}
/* The new players: for (let index = 0; index < this.options.seats; index++) ... */
static void new_players(open_graphical *g) {
  qa_application *app = g->app;
  qa_app_shared_simulation *simulation = app->world_simulation;
  bool owned = app->ownership.borrowed_client == NULL;
  for (double index = 0; index < app->launch_options.seats; index++) {
    qa_session_seat *existing = NULL;
    for (size_t i = 0; i < app->local_seat_count && existing == NULL; i++)
      if ((double)qa_session_seat_id(app->local_seats[i]).index == index) existing = app->local_seats[i];
    qa_session_client *client = existing != NULL ? qa_session_seat_client(existing)
        : checked(qa_engine_session_create_client(app->session, (int64_t)index));
    if (owned) source_connection_set(app, client, qa_session_client_connect(client, "loopback"));
    qa_session_seat *seat = existing != NULL ? existing : checked(qa_engine_session_create_seat(app->session, (int64_t)index, client));
    local_seat_set(app, qa_session_client_id(client), seat);
    archive_rows archive = { NULL, 0 };
    archive.entries = qa_application_load_client_archive(app, seat, app->loaded_content, NULL, &archive.count);
    qa_cleanup archive_cleanup;
    qa_cleanup_push(&archive_cleanup, archive_rows_free, &archive);
    qa_cvar_registry *cvars = qa_application_create_client_cvars(app, simulation, app->loaded_content, &app->launch_options, seat, NULL,
        NULL, archive.entries, archive.count, true);
    qa_cleanup_pop(&archive_cleanup);
    archive_rows_free(&archive);
    client_cvars_set(app, qa_session_seat_id(seat), cvars);
    char *userinfo = localhost_userinfo(cvars);
    keep_record(app, userinfo, NULL); /* the guest seat keeps the userinfo it was prepared with */
    if (app->local_guest != NULL) {
      qa_app_sim_q3_server_authority *authority = &app->local_guest->authority;
      qa_app_q3_admission admission = authority->connect(authority->host.ctx, qa_session_client_id(client), userinfo);
      if (!admission.accepted) qa_raise("Error", 0, 0, "%s", admission.reason != NULL ? admission.reason : "");
      qa_application_prepare_local_guest_seat(app, app->local_guest, simulation, seat, cvars, userinfo,
          authority->host.game_state(authority->host.ctx, admission.player, 1));
      if (authority->host.begin != NULL) {
        /* { serverTime: 0, angles: [0, 0, 0], forwardmove: 0, rightmove: 0, upmove: 0, buttons: 0, weapon: 2 } */
        qa_q3_wire_user_command command;
        memset(&command, 0, sizeof command);
        command.weapon = 2;
        authority->host.begin(authority->host.ctx, admission.player, &command);
      }
      push_player(g, seat, admission.player.actor);
    } else {
      qa_app_q3_source_runtime *q3 = qa_app_shared_sim_q3_source(simulation);
      if (q3 != NULL) {
        qa_app_q3_source_host *host = qa_app_q3_source_runtime_host(q3);
        host->engine.set_userinfo(host->engine.ctx, (double)qa_session_client_id(client).slot, userinfo);
      }
      qa_app_sim_player_admission player;
      if (!qa_app_shared_sim_q2_native(simulation, NULL, NULL)) {
        char *info = qa_app_player_userinfo(cvars);
        qa_cleanup cleanup;
        qa_cleanup_push(&cleanup, free, info);
        player = qa_app_shared_sim_admit_player_with(simulation, qa_session_client_id(client), NULL, info);
        qa_cleanup_pop(&cleanup);
        free(info);
      } else player = qa_app_shared_sim_admit_q2_native_player(simulation, qa_session_client_id(client), userinfo, true);
      push_player(g, seat, player.actor);
    }
  }
}
/* applyTeamArenaOverrides(this.teamArenaOverrides, this.overrideSeats()) / captureTeamArenaOverrides(this.overrideSeats()). */
static void team_arena_overrides(open_graphical *g) {
  qa_application *app = g->app;
  if (!g->restoring && app->launch_options.team_arena_skirmish == NULL) return;
  size_t count = qa_application_override_seats(app, NULL, NULL, 0, NULL, 0);
  qa_app_override_registry *seats = checked(calloc(count + 1, sizeof *seats));
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, free, seats);
  (void)qa_application_override_seats(app, NULL, NULL, 0, seats, count);
  if (g->restoring) {
    if (app->team_arena_overrides != NULL) qa_app_apply_team_arena_overrides(app->team_arena_overrides, seats, count);
  } else {
    qa_app_team_arena_overrides *captured = checked(calloc(1, sizeof *captured));
    keep_record(app, captured, (void (*)(void *))qa_app_team_arena_overrides_free);
    *captured = qa_app_capture_team_arena_overrides(seats, count);
    app->team_arena_overrides = captured;
  }
  qa_cleanup_pop(&cleanup);
  free(seats);
}
static void open_body(void *ctx) {
  open_graphical *g = ctx;
  qa_application *app = g->app;
  qa_app_shared_simulation *simulation = app->world_simulation;
  qa_app_loaded_content *content = app->loaded_content;
  const qa_app_options *options = &app->launch_options;
  stage(app, "Loading textures...");
  (void)qa_app_application_assets_load_world(g->assets);
  stage(app, "Loading characters...");
  qa_text_font_selection font = qa_app_application_assets_load_console_font(g->assets);
  const qa_app_menu_typography *typography = qa_app_application_assets_load_menu_typography(g->assets);
  const qa_q3_character_assets *characters = options->character == QA_GAME_FAMILY_Q3
      ? qa_app_load_q3_character_for_content(content, qa_app_loaded_content_recipe(content)->character.appearance.content,
          options->character_model)
      : NULL;
  if (g->client != NULL) g->renderer = g->client->renderer;
  else {
    /* NativeRenderer.open({ ...options, renderWorker: r_smp !== 0, ...(loading?.deferWindowVisibility ? { hidden: true } : {}) },
     * owner, rootImages) */
    bool worker = (app->image_settings == NULL ? 0
        : qa_cvar_registry_variable_value(qa_app_image_settings_cvars(app->image_settings), "r_smp")) != 0;
    bool hidden = app->host.loading != NULL && app->host.loading->defer_window_visibility ? true : options->hidden;
    g->renderer = qa_app_native_renderer_open((qa_app_native_renderer_host){0}, options->renderer, options->width, options->height,
        hidden, options->gamma, worker, g->renderer_owner, g->root_images);
    if (g->renderer == NULL) qa_range_error("Out of memory");
    g->owner->renderer = g->renderer;
    g->owner->owns_renderer = true;
    if (app->image_settings != NULL) qa_app_image_settings_refresh_display(app->image_settings, g->renderer);
  }
  if (g->restoring) restored_players(g);
  else new_players(g);
  team_arena_overrides(g);
  stage(app, "Loading sounds...");
  qa_app_prepared_profile *configuration = app->profile_configuration;
  qa_application_input_actions *actions = qa_application_input_actions_create(app, NULL);
  keep_record(app, actions, qa_application_input_actions_release);
  if (app->configuration_scripts != NULL) actions->commands.scripts = app->configuration_scripts;
  if (configuration != NULL) {
    actions->commands.configuration = configuration;
    actions->startup_configuration = configuration;
    actions->commands.startup_reader = startup_reader;
  }
  qa_app_sim_presentation_access *access = checked(malloc(sizeof *access));
  keep_record(app, access, NULL);
  *access = qa_app_shared_sim_presentation_access(simulation);
  qa_app_input_open_args args;
  memset(&args, 0, sizeof args);
  args.window = qa_app_input_window_from_sdl(qa_app_native_renderer_window(g->renderer));
  args.players = g->players; args.player_count = g->player_count;
  args.options = options;
  args.dialect = qa_app_movement_dialect(options, qa_app_shared_sim_recipe(simulation));
  args.simulation = access;
  args.actions = actions->commands;
  args.now = performance_now; args.now_ctx = NULL;
  args.settings = &app->input_config;
  args.prepared = app->prepared_startup;
  if (g->client == NULL) g->input = qa_app_application_input_open(&args);
  else {
    /* new Set([client.imageSettings.cvars, prepared.source, prepared.movement, prepared.fallback,
     * ...prepared.seats.flatMap(seat => [seat.cvars, seat.mouse.cvars])]) */
    size_t seat_count = 0;
    const qa_app_prepared_seat *seats = qa_app_prepared_startup_seats(g->client->prepared, &seat_count);
    qa_cvar_registry **live = checked(calloc(4 + seat_count * 2, sizeof *live));
    qa_cleanup cleanup;
    qa_cleanup_push(&cleanup, free, live);
    size_t live_count = 0;
    qa_cvar_registry *candidates[4] = { qa_app_image_settings_cvars(g->client->image_settings),
      qa_app_prepared_startup_source(g->client->prepared), qa_app_prepared_startup_movement(g->client->prepared),
      qa_app_prepared_startup_fallback(g->client->prepared) };
    for (size_t i = 0; i < 4 + seat_count * 2; i++) {
      qa_cvar_registry *registry = i < 4 ? candidates[i] : (i - 4) % 2 == 0 ? seats[(i - 4) / 2].cvars : seats[(i - 4) / 2].mouse.cvars;
      bool present = false;
      for (size_t j = 0; j < live_count && !present; j++) present = live[j] == registry;
      if (!present) live[live_count++] = registry;
    }
    g->input = qa_app_application_input_prepare_for_client(g->client, live, live_count, &args);
    qa_cleanup_pop(&cleanup);
    free(live);
  }
  if (g->input == NULL) qa_range_error("Out of memory");
  qa_application_restore_source_input_angles(app, g->input, simulation, NULL);
  if (g->restoring) {
    /* Math.max(0, ...players.map(player => (simulation.movementPlayer(actor)?.lastSequence ?? -1) + 1)) */
    double sequence = 0;
    for (size_t i = 0; i < g->player_count; i++) {
      qa_app_sim_movement_player *movement = qa_app_shared_sim_movement_player(simulation, g->players[i].actor);
      sequence = qa_js_max(sequence, (movement == NULL ? -1 : movement->last_sequence) + 1);
    }
    qa_app_application_input_resume_commands(g->input, sequence);
  }
  if (app->local_guest != NULL) {
    app->guest_browser = qa_application_open_guest_browser(app);
    qa_application_publish_local_guest_snapshots(app);
  }
  qa_application_audio_settings settings;
  memset(&settings, 0, sizeof settings);
  settings.has_values = qa_app_load_audio_settings(&app->input_config, &settings.values);
  if (settings.has_values) {
    keep_record(app, settings.values.device_name, NULL);
    keep_record(app, settings.values.menu_track, NULL);
  }
  g->audio = qa_application_create_world_audio(app, content, options, &settings, g->client != NULL, NULL);
  qa_cvar_registry *image_cvars = app->image_settings == NULL ? NULL : qa_app_image_settings_cvars(app->image_settings);
  qa_cvar_read volume;
  bool has_volume = image_cvars != NULL && qa_cvar_registry_find(image_cvars, "volume", &volume);
  if (has_volume) qa_cvar_read_free(&volume);
  if (has_volume) {
    if (app->prepared_startup == NULL) {
      char text[40];
      qa_net_js_number(qa_app_application_audio_effects_volume(g->audio), text, sizeof text);
      qa_application_cvar_set(image_cvars, "volume", text, false);
      qa_net_js_number(qa_app_application_audio_music_volume(g->audio), text, sizeof text);
      qa_application_cvar_set(image_cvars, "bgmvolume", text, false);
    }
    qa_app_application_audio_bind_volume_cvars(g->audio, image_cvars);
    qa_app_application_audio_bind_output_cvars(g->audio, image_cvars);
    if (g->client == NULL) apply_audio_output(image_cvars, g->audio);
  }
  qa_app_application_audio_bind_haptics(g->audio, qa_app_application_input_audio_haptics(g->input));
  qa_app_application_audio_prepare_environment(g->audio, qa_app_shared_sim_scene_queries(simulation));
  qa_app_apply_frontend_preferences(app->frontend_overrides, g->input, g->audio);
  app->has_frontend_baseline = qa_app_read_frontend_preferences(g->input, g->audio, &app->frontend_baseline);
  const qa_renderer_image *font_source = &font.classic->picture.image.image;
  if (!font_source->source_is_resource) qa_raise("Error", 0, 0, "Native menu font has no mounted resource identity");
  qa_native_ui_art *art = checked(calloc(1, sizeof *art));
  keep_record(app, art, NULL);
  qa_ui_load_native_art_selected(art, font_source->source_resource.id, qa_app_application_assets_images(g->assets), menu_art_read, NULL,
      false, false); /* { frames: false, mainBackground: false } */
  g->art = art;
  g->effects = qa_application_create_world_effects(app, g->assets, simulation, options->seed);
  qa_application_preload_world_effects(app, g->effects);
  qa_app_prepare_world_resources(g->assets, simulation, g->audio, g->effects, app->host.loading, host_print, app);
  qa_app_native_renderer *native = g->renderer;
  qa_app_rerelease_seat *rerelease_seats = checked(calloc(g->player_count + 1, sizeof *rerelease_seats));
  keep_record(app, rerelease_seats, NULL);
  for (size_t i = 0; i < g->player_count; i++) {
    rerelease_seats[i].seat = qa_session_seat_id(g->players[i].seat);
    rerelease_seats[i].actor = g->players[i].actor;
  }
  qa_app_rerelease_presentation *rerelease = qa_app_rerelease_presentation_create(g->assets, rerelease_seats, g->player_count,
      qa_app_application_input_shared_settings(g->input));
  graphical_record *kept = checked(calloc(1, sizeof *kept));
  kept->owner = g->owner;
  g->owner->adopted = true;
  qa_application_graphical *graphical = &kept->graphical;
  graphical->rerelease = rerelease;
  keep_record(app, kept, graphical_release);
  size_t locals = qa_app_application_input_local_count(g->input);
  graphical->presentations = checked(calloc(locals + 1, sizeof *graphical->presentations));
  graphical->q3_seats = checked(calloc(locals + 1, sizeof *graphical->q3_seats));
  graphical->q3 = checked(calloc(locals + 1, sizeof *graphical->q3));
  graphical->native_q2_seats = checked(calloc(locals + 1, sizeof *graphical->native_q2_seats));
  graphical->native_q2 = checked(calloc(locals + 1, sizeof *graphical->native_q2));
  g->source_clients = checked(calloc(locals + 1, sizeof *g->source_clients));
  g->native_q2 = graphical->native_q2;
  for (size_t index = 0; index < locals; index++) {
    qa_app_local_input *local = qa_app_application_input_local(g->input, index);
    qa_seat_id seat = qa_session_seat_id(local->player.seat);
    qa_application_q3_seat_client source_client;
    bool has_source = qa_application_create_q3_seat_client(app, local, g->assets, g->audio, g->input, native, simulation, NULL, 0,
        app->local_guest, app->guest_browser, qa_application_client_cvars_get(app, seat), &source_client);
    qa_application_q3_seat_client *seat_client = NULL;
    if (has_source) {
      g->source_clients[g->source_client_count++] = source_client.client;
      graphical->q3_seats[graphical->q3_count] = seat;
      graphical->q3[graphical->q3_count] = source_client;
      seat_client = &graphical->q3[graphical->q3_count++];
    }
    double override_value = 0;
    if (qa_app_view_settings_override(app->view_settings, &override_value)) {
      double field_of_view = qa_app_view_settings_field_of_view(app->view_settings);
      if (!has_source || !source_client.qvm)
        qa_app_shared_sim_set_player_field_of_view(simulation, local->player.actor, field_of_view, g->restoring ? "restore" : "change");
      if (has_source) {
        char text[40];
        qa_net_js_number(field_of_view, text, sizeof text);
        qa_application_cvar_set(qa_app_application_q3_client_cvars(source_client.client), "cg_fov", text, false);
      }
    }
    if (g->restoring && has_source && source_client.qvm)
      qa_app_application_q3_client_prepare(source_client.client, app->presentation_frames, NULL, NULL, 0, true, NULL, 0, true, false);
    qa_application_native_q2_seat_client native_seat;
    bool has_native = qa_application_prepare_native_q2_seat(app, local, g->assets, simulation, qa_application_client_cvars_get(app, seat),
        &native_seat);
    qa_application_native_q2_seat_client *native_row = NULL;
    if (has_native) {
      graphical->native_q2_seats[graphical->native_q2_count] = seat;
      graphical->native_q2[graphical->native_q2_count] = native_seat;
      native_row = &graphical->native_q2[graphical->native_q2_count++];
      g->native_q2_count = graphical->native_q2_count;
    }
    qa_app_seat_ui *ui = qa_application_create_seat_ui(app, local, g->art, g->input, native, simulation, font, g->audio, typography,
        rerelease, content, options, has_source && source_client.qvm, NULL, NULL, 0);
    qa_app_world_seat_presentation *presentation = qa_application_create_world_seat_presentation(app, local, g->assets, native,
        simulation, options->seats, font, characters, ui, g->effects, seat_client, rerelease, native_row, NULL);
    if (g->client == NULL)
      (void)qa_session_seat_attach_presentation(local->player.seat, qa_app_world_seat_presentation_seat(presentation),
          presentation_cleanup, presentation);
    graphical->presentations[graphical->presentation_count].presentation = presentation;
    graphical->presentations[graphical->presentation_count].local = local;
    graphical->presentations[graphical->presentation_count].ui = ui;
    graphical->presentations[graphical->presentation_count++].q3_client = seat_client;
  }
  graphical->renderer = g->renderer;
  graphical->assets = g->assets;
  graphical->input = g->input;
  graphical->audio = g->audio;
  graphical->effects = g->effects;
  graphical->art = g->art;
  qa_application_clock clock = { app, presentation_clock };
  qa_application_create_presentation_services(app, g->assets, g->audio, g->input, simulation, content, native, clock, graphical);
  app->graphical = graphical;
  if (g->save != NULL) {
    qa_application_restore_component_clients(app, g->save, app->graphical, simulation);
    qa_app_application_mod_presentations_publish_restored(app->graphical->mod_presentations);
  }
  qa_application_refresh_application_tools(app);
  app->capture = g->client != NULL ? g->client->capture : qa_application_create_application_capture(app, g->input, g->renderer);
  if (g->client == NULL) {
    qa_app_capture_activate(app->capture);
    qa_app_video_restart *video = qa_application_create_video_restart(app, g->renderer);
    qa_app_video_restart_register(video, qa_app_application_input_commands(g->input));
    app->owned_video_restart = video;
  }
  if (graphical->q3_count == 0) qa_app_application_audio_start_world_music(g->audio);
}
typedef struct empty_frame { qa_app_native_renderer *renderer; qa_render_frame frame; } empty_frame;
static void execute_empty(void *ctx) {
  empty_frame *flush = ctx;
  qa_app_native_renderer_execute(flush->renderer, flush->frame);
}
/* The TS catch block: every owner the preparation created is closed before the error is rethrown. */
static void open_cleanup(open_graphical *g) {
  qa_application *app = g->app;
  if (app->graphical != NULL) qa_app_application_mod_presentations_close(app->graphical->mod_presentations);
  for (size_t i = 0; i < g->native_q2_count; i++) qa_app_application_effects_close(g->native_q2[i].effects);
  for (size_t i = 0; i < g->source_client_count; i++) qa_app_application_q3_client_close(g->source_clients[i]);
  qa_app_q3_qvm_server_game *guest = qa_app_shared_sim_q3_guest(app->world_simulation);
  if (g->restoring) { if (guest != NULL) qa_app_q3_qvm_server_game_discard(guest); }
  else qa_app_shared_sim_shutdown_q3_guest(app->world_simulation);
  if (app->local_guest != NULL) {
    for (size_t i = 0; i < app->local_guest->seat_count; i++) qa_app_local_q3_client_state_retire(app->local_guest->seats[i].state);
    app->local_guest->seat_count = 0;
  }
  app->local_guest = NULL;
  if (g->audio != NULL) qa_app_application_audio_close(g->audio);
  if (g->effects != NULL) qa_app_application_effects_close(g->effects);
  if (g->input != NULL) qa_app_application_input_close(g->input);
  if (g->art != NULL) qa_native_ui_art_close(g->art);
  qa_app_application_assets_close(g->assets);
  if (g->client == NULL) {
    qa_engine_session_close(app->session);
    if (g->renderer == NULL) qa_scene_image_registry_close(g->root_images);
    else qa_app_native_renderer_close(g->renderer);
  } else {
    /* try { client.renderer.execute({ owner, sequence: this.presentationFrames, commands: [] }); }
     * catch (cleanup) { throw new AggregateError([error, cleanup], "Graphical preparation and image retirement failed"); } */
    empty_frame flush;
    memset(&flush, 0, sizeof flush);
    flush.renderer = g->client->renderer;
    flush.frame.owner = g->renderer_owner;
    flush.frame.sequence = app->presentation_frames;
    qa_caught_error cleanup;
    if (!qa_try(execute_empty, &flush, &cleanup))
      qa_raise("AggregateError", 0, 2, "Graphical preparation and image retirement failed");
  }
  app->graphical = NULL;
}
static void open_graphical_storage_free(void *ctx) {
  open_graphical *g = ctx;
  free(g->players); g->players = NULL;
  free(g->source_clients); g->source_clients = NULL;
}
void qa_application_open_graphical(qa_application *app, const qa_save_image *initial_save) {
  open_graphical g;
  memset(&g, 0, sizeof g);
  g.app = app;
  g.save = initial_save;
  g.restoring = initial_save != NULL;
  g.client = app->ownership.borrowed_client;
  graphical_owner_box *box = checked(calloc(1, sizeof *box));
  keep_record(app, box, graphical_owner_release);
  box->owner = g.owner = checked(calloc(1, sizeof *g.owner));
  if (g.client != NULL) {
    g.renderer_owner = qa_app_native_renderer_owner(g.client->renderer);
    g.root_images = qa_app_native_renderer_images(g.client->renderer);
  } else {
    g.renderer_owner.identity = &g.owner->identity;
    g.renderer_owner.session = qa_engine_session_id(app->session);
    g.renderer_owner.generation = 0;
    g.root_images = checked(calloc(1, sizeof *g.root_images));
    qa_scene_image_registry_init(g.root_images, g.renderer_owner, NULL);
    g.owner->root_images = g.root_images;
    g.owner->owns_root_images = true;
  }
  g.assets = qa_application_create_world_assets(app, app->loaded_content, g.renderer_owner, g.root_images);
  qa_caught_error error;
  bool opened = qa_try(open_body, &g, &error);
  if (!opened) {
    qa_cleanup storage;
    qa_cleanup_push(&storage, open_graphical_storage_free, &g);
    open_cleanup(&g);
    qa_cleanup_pop(&storage);
  }
  open_graphical_storage_free(&g);
  if (!opened) qa_rethrow(&error);
}

/* ================= the application as a ClientSourceLifetime ================= */
/* The C lifetime row returns errors as borrowed text: a raised error is caught and its message kept in
 * app->source_lifetime_error (valid until the next lifetime call). */
static const char *lifetime_error(qa_application *app, const qa_caught_error *error) {
  (void)snprintf(app->source_lifetime_error, sizeof app->source_lifetime_error, "%s", error->message);
  return app->source_lifetime_error;
}
/* prepareRecording(source): the ClientRecordingFeed (root, seed(), attach(sink)). seed's *out_seed is a
 * qa_app_demo_recording_seed valid until the next seed call; attach takes a const qa_app_demo_recording_sink *. */
typedef struct lifetime_feed { qa_application_recording_feed *feed; qa_app_demo_recording_seed seed; } lifetime_feed;
static const char *feed_seed(void *ctx, const void **out_seed) {
  lifetime_feed *feed = ctx;
  feed->seed = qa_application_recording_feed_seed(feed->feed);
  *out_seed = &feed->seed;
  return NULL;
}
static const void *feed_attach(void *ctx, const void *sink) {
  return qa_application_recording_feed_attach(((lifetime_feed *)ctx)->feed, *(const qa_app_demo_recording_sink *)sink);
}
static void feed_detach(void *ctx, const void *token) {
  qa_application_recording_feed_detach(((lifetime_feed *)ctx)->feed, (void *)token);
}
typedef struct recording_call { qa_application *app; qa_command_context source; qa_application_recording_feed *feed; } recording_call;
static void recording_body(void *ctx) {
  recording_call *call = ctx;
  call->feed = qa_application_prepare_recording(call->app, call->source);
}
static const char *lifetime_prepare_recording(void *ctx, qa_command_context source, qa_app_client_recording_feed *out) {
  qa_application *app = ctx;
  recording_call call = { app, source, NULL };
  qa_caught_error error;
  if (!qa_try(recording_body, &call, &error)) return lifetime_error(app, &error);
  lifetime_feed *feed = checked(calloc(1, sizeof *feed));
  keep_record(app, feed, NULL);
  feed->feed = call.feed;
  memset(out, 0, sizeof *out);
  out->ctx = feed;
  out->root = qa_application_recording_feed_root(call.feed);
  out->seed = feed_seed;
  out->attach = feed_attach;
  out->detach = feed_detach;
  return NULL;
}
/* prepareVideoRestart(): *out_prepared is a qa_app_prepared_video_presentation (NULL is TS null), kept until destroy. */
typedef struct video_call { qa_application *app; qa_app_prepared_video_presentation *prepared; bool present; } video_call;
static void video_body(void *ctx) {
  video_call *call = ctx;
  call->present = qa_application_prepare_video_restart(call->app, call->prepared);
}
static const char *lifetime_prepare_video_restart(void *ctx, const void **out_prepared) {
  qa_application *app = ctx;
  qa_app_prepared_video_presentation *prepared = checked(calloc(1, sizeof *prepared));
  keep_record(app, prepared, NULL);
  video_call call = { app, prepared, false };
  qa_caught_error error;
  if (!qa_try(video_body, &call, &error)) return lifetime_error(app, &error);
  *out_prepared = call.present ? prepared : NULL;
  return NULL;
}
static void retirement_body(void *ctx) { qa_application_prepare_retirement(ctx); }
static const char *lifetime_prepare_retirement(void *ctx) {
  qa_caught_error error;
  return qa_try(retirement_body, ctx, &error) ? NULL : lifetime_error(ctx, &error);
}
static void lifetime_release_settings(void *ctx) { qa_application_release_settings(ctx); }
static void retire_body(void *ctx) { qa_application_retire(ctx); }
static const char *lifetime_retire(void *ctx) {
  qa_caught_error error;
  return qa_try(retire_body, ctx, &error) ? NULL : lifetime_error(ctx, &error);
}
static const qa_app_client_source_lifetime *source_lifetime(qa_application *app) {
  qa_app_client_source_lifetime *lifetime = &app->source_lifetime;
  lifetime->ctx = app;
  lifetime->prepare_recording = lifetime_prepare_recording;
  lifetime->prepare_video_restart = lifetime_prepare_video_restart;
  lifetime->capture_map = qa_application_capture_map(app); /* TS get captureMap (refreshed when the content changes) */
  lifetime->prepare_retirement = lifetime_prepare_retirement;
  lifetime->release_settings = lifetime_release_settings;
  lifetime->retire = lifetime_retire;
  return lifetime;
}

/* ================= publishBorrowed(client, images) ================= */
static void unbind_view_cvars(void *ctx) { qa_app_view_settings_unbind_cvars(ctx); }
typedef struct publish_borrowed {
  qa_application *app;
  qa_app_client_bootstrap *client;
  const qa_app_image_client_settings *images;
  qa_application_graphical *graphical;
  const qa_app_client_source_lifetime *previous;
  qa_session_presentation_entry *presentations;
  qa_session_client_change clients;
  qa_session_seat_change seats;
  qa_unified_audio_transfer publish_audio;
  qa_session_resource retired; bool has_retired;
  qa_app_console_script_files *retired_configuration;
  qa_session_connection **retired_connections; size_t retired_connection_count;
} publish_borrowed;
static void publish_body(void *ctx) {
  publish_borrowed *p = ctx;
  qa_application *app = p->app;
  qa_app_client_bootstrap *client = p->client;
  qa_application_graphical *graphical = p->graphical;
  qa_app_client_source_release_settings(p->previous); /* previous?.releaseSettings() */
  size_t locals = qa_app_application_input_local_count(graphical->input);
  for (size_t i = 0; i < locals; i++) {
    qa_session_client *seat_client = qa_session_seat_client(qa_app_application_input_local(graphical->input, i)->player.seat);
    qa_session_connection *retired = NULL;
    qa_session_connection *connection = qa_session_client_replace_connection(seat_client, "loopback", &retired);
    source_connection_set(app, seat_client, connection);
    if (retired != NULL) p->retired_connections[p->retired_connection_count++] = retired;
  }
  qa_world_lifetime *world = NULL;
  p->retired = qa_engine_session_replace_world(client->session, qa_application_simulation_contract(&app->simulation_view),
      p->presentations, graphical->presentation_count, p->clients, p->seats, &world);
  p->has_retired = true;
  app->source_seats_published = true;
  client->has_source_profile = true;
  client->source_profile = qa_app_loaded_content_recipe(app->loaded_content)->map.entities;
  qa_application_release_settings(app);
  qa_app_image_client_settings_publish(p->images);
  qa_application_adopt_image_settings(app, client->image_settings);
  qa_cvar_registry *cvars = qa_app_image_settings_cvars(client->image_settings);
  qa_app_application_input_publish_shared_cvars(graphical->input, cvars);
  qa_app_application_audio_bind_volume_cvars(graphical->audio, cvars);
  qa_app_application_audio_bind_output_cvars(graphical->audio, cvars);
  qa_app_view_settings_set_field_of_view(app->view_settings, qa_cvar_registry_variable_value(cvars, "fov"));
  qa_app_view_settings_bind_cvars(app->view_settings, cvars);
  app->release_view_cvars.fn = unbind_view_cvars; app->release_view_cvars.ctx = app->view_settings;
  qa_app_application_input_publish_client_platform(graphical->input, client, false); /* "replace" */
  qa_application_publish_keys(app);
  qa_application_refresh_application_tools(app);
  if (app->configuration_scripts != NULL && app->configuration_scripts != client->configuration_current.scripts) {
    p->retired_configuration = client->configuration_current.scripts;
    client->configuration_current.scripts = app->configuration_scripts;
    client->configuration_current.options = &app->launch_options;
    app->configuration_scripts = NULL;
  }
  qa_unified_audio_transfer_commit(p->publish_audio); /* publishAudio() */
  qa_app_client_bootstrap_publish_output(client, qa_app_application_audio_engine(graphical->audio));
  apply_audio_output(cvars, graphical->audio);
}
static void retired_close(void *ctx) {
  qa_session_resource *retired = ctx;
  if (retired->close != NULL) retired->close(retired->ctx);
}
static void configuration_close(void *ctx) { if (ctx != NULL) qa_app_console_script_files_close(ctx); }
static void connection_close(void *ctx) { qa_session_connection_close(ctx); }
static void publish_storage_free(void *ctx) {
  publish_borrowed *p = ctx;
  free(p->presentations); p->presentations = NULL;
  free(p->retired_connections); p->retired_connections = NULL;
}
void qa_application_publish_borrowed(qa_application *app, qa_app_client_bootstrap *client, const qa_app_image_client_settings *images) {
  qa_application_graphical *graphical = app->graphical;
  if (graphical == NULL) qa_raise("Error", 0, 0, "Borrowed application has no graphical source");
  publish_borrowed p;
  memset(&p, 0, sizeof p);
  p.app = app;
  p.client = client;
  p.images = images;
  p.graphical = graphical;
  p.previous = client->source_current;
  qa_app_capture_before_world_change(client->capture);
  if (p.previous != NULL) {
    const char *error = qa_app_client_source_prepare_retirement(p.previous);
    if (error != NULL) qa_raise("Error", 0, 0, "%s", error);
  }
  qa_resource_scope_assert_open(qa_engine_session_resources(client->session));
  qa_app_application_input_validate_startup_adoption(graphical->input);
  qa_app_application_input_validate_candidate_commands(graphical->input);
  qa_app_image_client_settings_validate_publication(images);
  qa_cleanup storage;
  qa_cleanup_push(&storage, publish_storage_free, &p);
  p.presentations = checked(calloc(graphical->presentation_count + 1, sizeof *p.presentations));
  for (size_t i = 0; i < graphical->presentation_count; i++) {
    qa_app_world_seat_presentation *presentation = graphical->presentations[i].presentation;
    p.presentations[i].seat = graphical->presentations[i].local->player.seat;
    p.presentations[i].presentation = qa_app_world_seat_presentation_seat(presentation);
    p.presentations[i].cleanup = presentation_cleanup;
    p.presentations[i].cleanup_ctx = presentation;
  }
  p.clients = (qa_session_client_change){ app->source_clients_added, app->source_clients_added_count, app->source_clients_removed,
    app->source_clients_removed_count };
  p.seats = (qa_session_seat_change){ app->source_seats_added, app->source_seats_added_count, app->source_seats_removed,
    app->source_seats_removed_count };
  qa_engine_session_validate_world_replacement(client->session, qa_application_simulation_contract(&app->simulation_view), p.presentations,
      graphical->presentation_count, p.clients, p.seats);
  p.publish_audio = qa_unified_audio_prepare_output_transfer(client->output_current, qa_app_application_audio_engine(graphical->audio));
  p.retired_connections = checked(calloc(qa_app_application_input_local_count(graphical->input) + 1, sizeof *p.retired_connections));
  qa_caught_error *failures = NULL;
  size_t failure_count = 0;
  app->source_published = true;
  client->source_current = source_lifetime(app);
  qa_caught_error caught;
  if (!qa_try(publish_body, &p, &caught)) {
    failures = checked(realloc(failures, (failure_count + 1) * sizeof *failures));
    failures[failure_count++] = caught;
    app->fatal_world_failure = true;
  }
  /* finally: each retirement step's failure joins the list instead of stopping the rest. */
  if (p.previous != NULL) {
    const char *error = qa_app_client_source_retire(p.previous);
    if (error != NULL) {
      memset(&caught, 0, sizeof caught);
      caught.name = "Error";
      (void)snprintf(caught.message, sizeof caught.message, "%s", error);
      failures = checked(realloc(failures, (failure_count + 1) * sizeof *failures));
      failures[failure_count++] = caught;
    }
  }
  if (p.has_retired && !qa_try(retired_close, &p.retired, &caught)) {
    failures = checked(realloc(failures, (failure_count + 1) * sizeof *failures));
    failures[failure_count++] = caught;
  }
  if (p.retired_configuration != NULL && !qa_try(configuration_close, p.retired_configuration, &caught)) {
    failures = checked(realloc(failures, (failure_count + 1) * sizeof *failures));
    failures[failure_count++] = caught;
  }
  for (size_t i = 0; i < p.retired_connection_count; i++)
    if (!qa_try(connection_close, p.retired_connections[i], &caught)) {
      failures = checked(realloc(failures, (failure_count + 1) * sizeof *failures));
      failures[failure_count++] = caught;
    }
  qa_cleanup_pop(&storage);
  publish_storage_free(&p);
  free(failures);
  if (failure_count > 0)
    qa_raise("AggregateError", 0, (long long)failure_count, "Client source publication failed after retirement began");
}
