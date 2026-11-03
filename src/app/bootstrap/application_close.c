/* Port of src/app/bootstrap/application.ts, region 5207-5306: close,
 * closeFailedWorld, closeOwned. Each TS `try { x } catch (error) {
 * errors.push(error) }` is one qa_try over one static step below, run in TS
 * order. Layout: docs/application_layout.md. GPL-2.0-or-later. */
#include "application_internal.h"
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/audio_settings.h"
#include "qa/app/bootstrap/console.h"

/* TS errors: unknown[]. */
typedef struct close_errors {
  qa_caught_error *items; size_t count, capacity;
} close_errors;
static void push_error(close_errors *errors, const qa_caught_error *error) {
  if (errors->count == errors->capacity) {
    size_t next = errors->capacity == 0 ? 4 : errors->capacity * 2;
    qa_caught_error *items = realloc(errors->items, next * sizeof *items);
    if (items == NULL) qa_range_error("Out of memory");
    errors->items = items; errors->capacity = next;
  }
  errors->items[errors->count++] = *error;
}
typedef struct close_step { qa_application *app; qa_application_graphical *graphical; size_t index; } close_step;
static void attempt(close_errors *errors, void (*fn)(void *ctx), close_step *step) {
  qa_caught_error caught;
  if (!qa_try(fn, step, &caught)) push_error(errors, &caught);
}

/* TS close(). */
static void close_owned_entry(void *ctx) { qa_application_close_owned(ctx); }
void qa_application_close(qa_application *app) {
  if (app->closing) {
    if (app->has_closing_error) qa_rethrow(&app->closing_error);
    return;
  }
  app->closed = true; app->stopping = true;
  if (app->operator_state != NULL) qa_app_server_operator_state_close(app->operator_state);
  app->closing = true;
  qa_caught_error caught;
  if (!qa_try(close_owned_entry, app, &caught)) {
    app->closing_error = caught; app->has_closing_error = true;
    qa_rethrow(&caught);
  }
}

/* TS closeFailedWorld(error): never returns. Q3GameCallbackError and
 * Q2GameCallbackError carry their cause; the C callback channels raise the
 * cause itself, so error is already the original. */
static void before_world_change(void *ctx) {
  qa_application *app = ctx;
  if (app->capture != NULL) qa_app_capture_before_world_change(app->capture);
}
static void close_entry(void *ctx) { qa_application_close(ctx); }
noreturn void qa_application_close_failed_world(qa_application *app, const qa_caught_error *error) {
  qa_caught_error original = *error;
  size_t failures = 1;
  qa_caught_error cleanup;
  if (!qa_try(before_world_change, app, &cleanup)) failures++;
  if (!qa_try(close_entry, app, &cleanup)) failures++;
  if (failures > 1) qa_raise("AggregateError", 0, (long long)failures, "World publication and shutdown failed");
  qa_rethrow(&original);
}

/* ---- closeOwned steps ---- */
static void step_stop_recordings(void *ctx) {
  close_step *s = ctx;
  if (s->app->owned_recording == NULL) return;
  const char *error = qa_app_client_demo_recording_stop_all(s->app->owned_recording);
  if (error != NULL) qa_raise("Error", 0, 0, "%s", error);
}
static void step_release_demos(void *ctx) {
  close_step *s = ctx;
  if (s->app->has_owned_demos && s->app->owned_demos_release.fn != NULL) s->app->owned_demos_release.fn(s->app->owned_demos_release.ctx);
}
static void step_close_tools(void *ctx) {
  close_step *s = ctx;
  if (s->app->tools == NULL) return;
  const char *error = qa_app_application_tools_close(s->app->tools);
  if (error != NULL) qa_raise("Error", 0, 0, "%s", error);
  qa_app_application_tools_destroy(s->app->tools); /* TS GC */
}
static void step_close_video_restart(void *ctx) {
  close_step *s = ctx;
  if (s->app->owned_video_restart != NULL) qa_app_video_restart_close(s->app->owned_video_restart);
}
static void step_close_movie(void *ctx) {
  close_step *s = ctx;
  qa_app_campaign_cinematic_close(s->app->movies[s->index].playback, s->app->presentation_frames);
}
static void step_close_capture(void *ctx) {
  close_step *s = ctx;
  if (s->app->capture != NULL) qa_app_capture_destroy(s->app->capture); /* closes, then frees (TS GC) */
}
static void step_save_key_profile(void *ctx) {
  close_step *s = ctx;
  qa_application *app = s->app;
  for (size_t i = 0; i < app->key_profile_count; i++)
    if (app->key_profile_simulations[i] == app->world_simulation) { qa_app_key_profile_save(app->key_profiles[i]); return; }
}
static void step_save_source_archive(void *ctx) {
  close_step *s = ctx;
  qa_application *app = s->app;
  qa_application_save_source_archive(app, &app->launch_options, app->loaded_content, app->world_simulation, app->team_arena_overrides);
}
static void step_save_client_archives(void *ctx) {
  close_step *s = ctx;
  qa_application *app = s->app;
  qa_application_save_client_archives(app, &app->launch_options, app->loaded_content, app->client_cvar_seats, app->client_cvars,
      app->client_cvar_count, app->team_arena_overrides);
}
static void step_release_settings(void *ctx) { close_step *s = ctx; qa_application_release_settings(s->app); }
static void step_close_image_settings(void *ctx) {
  close_step *s = ctx;
  if (s->app->image_settings == NULL) return;
  qa_app_image_settings_close(s->app->image_settings);
}
static void step_save_input_settings(void *ctx) {
  close_step *s = ctx;
  if (s->graphical != NULL) qa_app_application_input_save_settings(s->graphical->input);
}
static void step_save_view_settings(void *ctx) {
  close_step *s = ctx;
  if (s->graphical != NULL && !(s->app->prepared_startup != NULL && qa_app_prepared_startup_pending(s->app->prepared_startup)))
    qa_app_view_settings_save(s->app->view_settings, &s->app->input_config);
}
/* TS saveAudioSettings(inputConfig, graphical.audio): the audio object supplies
 * musicPreferences, outputFormat, selectedOutput, effectsVolume, musicVolume. */
static void step_save_audio_settings(void *ctx) {
  close_step *s = ctx;
  if (s->graphical == NULL) return;
  qa_app_application_audio *audio = s->graphical->audio;
  qa_app_audio_music_preferences music = qa_app_application_audio_music_preferences(audio);
  qa_app_audio_settings_input input;
  memset(&input, 0, sizeof input);
  input.has_music_preferences = true;
  input.music_shuffle = music.music_shuffle;
  input.menu_track = music.menu_track;
  input.has_output_format = true;
  input.output_format = qa_app_application_audio_output_format(audio);
  input.selected_output = qa_app_application_audio_selected_output(audio);
  input.effects_volume = qa_app_application_audio_effects_volume(audio);
  input.music_volume = qa_app_application_audio_music_volume(audio);
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, free, music.menu_track);
  qa_app_save_audio_settings(&s->app->input_config, input);
  qa_cleanup_pop(&cleanup);
  free(music.menu_track);
}
static void step_close_mod_presentations(void *ctx) {
  close_step *s = ctx;
  if (s->graphical != NULL) qa_app_application_mod_presentations_close(s->graphical->mod_presentations);
}
static void step_close_selected_q3(void *ctx) {
  close_step *s = ctx;
  if (s->graphical != NULL) qa_app_selected_q3_presentations_close(s->graphical->selected_q3_presentations);
}
static void step_shutdown_q3_client(void *ctx) {
  close_step *s = ctx;
  qa_app_application_q3_client_shutdown(s->graphical->q3[s->index].client);
}
/* TS: seat.presentation === presentation ? seat.replacePresentation(null)?.close() : presentation.close(). */
static void step_close_presentation(void *ctx) {
  close_step *s = ctx;
  qa_application_seat_presentation *presentation = &s->graphical->presentations[s->index];
  qa_session_seat *seat = presentation->local->player.seat;
  qa_seat_presentation current;
  if (qa_session_seat_presentation(seat, &current) && current.ctx == (void *)presentation->presentation) {
    qa_resource_scope *previous = qa_session_seat_replace_presentation(seat, NULL, NULL);
    if (previous == NULL) return;
    /* The scope's cleanup is the publisher's () => presentation.close() (application_open_graphical.c, the world
     * transaction and changeLocalPlayers free the C presentation in it). */
    qa_resource_scope_close(previous);
    qa_resource_scope_destroy(previous);
  } else {
    qa_app_world_seat_presentation_close(presentation->presentation);
    /* C: close no longer frees (presentation.h); TS GC reclaims the closed object. */
    qa_app_world_seat_presentation_destroy(presentation->presentation);
  }
}
static void step_retire_guest_seat(void *ctx) {
  close_step *s = ctx;
  qa_app_local_q3_client_state_retire(s->app->local_guest->seats[s->index].state);
}
static void step_close_browser_view(void *ctx) {
  close_step *s = ctx;
  if (s->app->guest_browser != NULL) qa_q3_browser_view_close(s->app->guest_browser->view);
}
static void step_close_browser_host(void *ctx) {
  close_step *s = ctx;
  if (s->app->guest_browser != NULL) qa_app_startup_server_browser_close(s->app->guest_browser->host);
}
static void step_close_rankings(void *ctx) { close_step *s = ctx; qa_application_close_rankings(s->app, NULL); }
static void step_close_bots(void *ctx) {
  close_step *s = ctx;
  if (s->app->bots != NULL) s->app->bots->close(s->app->bots->ctx, false);
}
static void step_close_network(void *ctx) {
  close_step *s = ctx;
  qa_application_native_server *network = s->app->network;
  if (network == NULL) return;
  switch (network->kind) {
    case QA_APPLICATION_NETWORK_Q1: qa_app_q1_server_network_close(network->q1); break;
    case QA_APPLICATION_NETWORK_QW: qa_app_qw_server_network_close(network->qw); break;
    case QA_APPLICATION_NETWORK_Q2: qa_app_q2_server_network_close(network->q2); break;
    case QA_APPLICATION_NETWORK_Q3: qa_app_q3_server_network_close(network->q3); break;
    case QA_APPLICATION_NETWORK_UNIFIED: qa_app_unified_server_network_close(network->unified); break;
  }
}
static void step_shutdown_q3_guest(void *ctx) { close_step *s = ctx; qa_app_shared_sim_shutdown_q3_guest(s->app->world_simulation); }
static void step_write_shutdown_log(void *ctx) {
  close_step *s = ctx;
  qa_application_write_game_log(s->app, s->app->world_simulation, "ShutdownGame:\n");
  char rule[62];
  memset(rule, '-', 60); rule[60] = '\n'; rule[61] = '\0';
  qa_application_write_game_log(s->app, s->app->world_simulation, rule);
}
/* TS: session.world?.simulation === simulation ? session.closeWorld() : simulation.close(). */
static void step_close_world(void *ctx) {
  close_step *s = ctx;
  qa_world_lifetime *world = qa_engine_session_world(s->app->session);
  /* Every world the application attaches is a qa_application_simulation_view (the initial one is embedded; a
   * replaced world's is the transaction's, application_transition.c). */
  if (world != NULL && ((const qa_application_simulation_view *)qa_world_lifetime_simulation(world).ctx)->simulation == s->app->world_simulation)
    qa_engine_session_close_world(s->app->session);
  else qa_app_shared_sim_close(s->app->world_simulation);
}
static void step_close_game_log(void *ctx) { close_step *s = ctx; qa_application_close_game_log(s->app); }
static void step_close_input(void *ctx) { close_step *s = ctx; if (s->graphical != NULL) qa_app_application_input_close(s->graphical->input); }
static void step_close_audio(void *ctx) { close_step *s = ctx; if (s->graphical != NULL) qa_app_application_audio_close(s->graphical->audio); }
static void step_close_effects(void *ctx) { close_step *s = ctx; if (s->graphical != NULL) qa_app_application_effects_close(s->graphical->effects); }
static void step_close_dedicated_console(void *ctx) {
  close_step *s = ctx;
  if (s->app->dedicated_console != NULL) qa_dedicated_console_close(s->app->dedicated_console);
}
static void step_close_art(void *ctx) { close_step *s = ctx; if (s->graphical != NULL) qa_native_ui_art_close(s->graphical->art); }
static void step_close_assets(void *ctx) { close_step *s = ctx; if (s->graphical != NULL) qa_app_application_assets_close(s->graphical->assets); }
/* TS renderer.execute({ owner: renderer.owner, sequence: presentationFrames, commands: [] }). */
static void step_flush_renderer(void *ctx) {
  close_step *s = ctx;
  if (s->graphical == NULL) return;
  qa_render_frame frame;
  memset(&frame, 0, sizeof frame);
  frame.owner = qa_app_native_renderer_owner(s->graphical->renderer);
  frame.sequence = s->app->presentation_frames;
  qa_app_native_renderer_execute(s->graphical->renderer, frame);
}
static void step_disconnect_source_client(void *ctx) {
  close_step *s = ctx;
  qa_session_client *client = s->app->source_connection_clients[s->index];
  if (qa_session_client_connection(client) == s->app->source_connections[s->index]) qa_session_client_disconnect(client);
}
static void step_close_added_seat(void *ctx) {
  close_step *s = ctx;
  if (!s->app->source_seats_published) qa_session_seat_close(s->app->source_seats_added[s->index]);
}
static void step_close_added_client(void *ctx) {
  close_step *s = ctx;
  qa_session_client *client = s->app->source_clients_added[s->index];
  if (qa_engine_session_client_at(s->app->session, qa_session_client_id(client).slot) != client) qa_session_client_close(client);
}
static void step_close_session(void *ctx) { close_step *s = ctx; qa_engine_session_close(s->app->session); }
static void step_close_renderer(void *ctx) { close_step *s = ctx; if (s->graphical != NULL) qa_app_native_renderer_close(s->graphical->renderer); }
static void step_release_source_commands(void *ctx) {
  close_step *s = ctx;
  if (s->app->release_source_commands.fn != NULL) s->app->release_source_commands.fn(s->app->release_source_commands.ctx);
}
static void step_release_mod_input_commands(void *ctx) {
  close_step *s = ctx;
  if (s->app->release_mod_input_commands.fn != NULL) s->app->release_mod_input_commands.fn(s->app->release_mod_input_commands.ctx);
}
static void step_close_profile_routing(void *ctx) {
  close_step *s = ctx;
  if (s->app->profile_configuration != NULL) qa_app_console_routing_close(qa_app_prepared_profile_routing(s->app->profile_configuration));
}
static void step_close_configuration_scripts(void *ctx) {
  close_step *s = ctx;
  if (s->app->configuration_scripts != NULL) qa_app_console_script_files_close(s->app->configuration_scripts);
}
static void step_close_content(void *ctx) { close_step *s = ctx; qa_app_loaded_content_close(s->app->loaded_content); }

/* TS closeOwned. stepCompletion and worldOperationCompletion are synchronous
 * in C: a close that runs inside a step or world operation runs after that
 * call returns control here, so there is nothing left to await. */
void qa_application_close_owned(qa_application *app) {
  if (app->has_save_operation) {
    qa_application_save_operation operation = app->save_operation;
    app->has_save_operation = false;
    qa_caught_error rejected;
    memset(&rejected, 0, sizeof rejected);
    rejected.name = "Error";
    (void)snprintf(rejected.message, sizeof rejected.message, "%s", "Application closed before the save operation completed.");
    operation.settle(operation.ctx, &rejected);
  }
  app->stopping = true;
  qa_application_graphical *graphical = app->graphical;
  app->graphical = NULL;
  close_errors errors = { NULL, 0, 0 };
  close_step step = { app, graphical, 0 };
  attempt(&errors, step_stop_recordings, &step);
  attempt(&errors, step_release_demos, &step);
  app->has_owned_demos = false; app->owned_demos_service = NULL; app->owned_demos_release.fn = NULL;
  attempt(&errors, step_close_tools, &step);
  app->tools = NULL;
  attempt(&errors, step_close_video_restart, &step);
  app->owned_video_restart = NULL;
  for (step.index = 0; step.index < app->movie_count; step.index++) attempt(&errors, step_close_movie, &step);
  app->movie_count = 0;
  if (app->ownership.borrowed_client == NULL) attempt(&errors, step_close_capture, &step);
  app->capture = NULL;
  if (app->source_published) attempt(&errors, step_save_key_profile, &step);
  if (app->source_published && app->archive_persistence
      && !(app->prepared_startup != NULL && qa_app_prepared_startup_pending(app->prepared_startup))) {
    attempt(&errors, step_save_source_archive, &step);
    attempt(&errors, step_save_client_archives, &step);
  }
  attempt(&errors, step_release_settings, &step);
  if (app->ownership.borrowed_client == NULL) attempt(&errors, step_close_image_settings, &step);
  if (app->source_published) {
    attempt(&errors, step_save_input_settings, &step);
    attempt(&errors, step_save_view_settings, &step);
    attempt(&errors, step_save_audio_settings, &step);
  }
  attempt(&errors, step_close_mod_presentations, &step);
  attempt(&errors, step_close_selected_q3, &step);
  if (graphical != NULL) {
    for (step.index = 0; step.index < graphical->q3_count; step.index++) attempt(&errors, step_shutdown_q3_client, &step);
    for (step.index = 0; step.index < graphical->presentation_count; step.index++) attempt(&errors, step_close_presentation, &step);
  }
  if (app->local_guest != NULL) {
    for (step.index = 0; step.index < app->local_guest->seat_count; step.index++) attempt(&errors, step_retire_guest_seat, &step);
    app->local_guest->seat_count = 0;
  }
  app->local_guest = NULL;
  attempt(&errors, step_close_browser_view, &step);
  attempt(&errors, step_close_browser_host, &step);
  qa_application_release_guest_browser(app->guest_browser);
  app->guest_browser = NULL;
  void (*const sequence[])(void *) = { step_close_rankings, step_close_bots, step_close_network, step_shutdown_q3_guest,
    step_write_shutdown_log, step_close_world, step_close_game_log, step_close_input, step_close_audio, step_close_effects,
    step_close_dedicated_console, step_close_art, step_close_assets, step_flush_renderer };
  for (size_t i = 0; i < sizeof sequence / sizeof sequence[0]; i++) attempt(&errors, sequence[i], &step);
  for (step.index = 0; step.index < app->source_connection_count; step.index++) attempt(&errors, step_disconnect_source_client, &step);
  app->source_connection_count = 0;
  for (step.index = 0; step.index < app->source_seats_added_count; step.index++) attempt(&errors, step_close_added_seat, &step);
  for (step.index = 0; step.index < app->source_clients_added_count; step.index++) attempt(&errors, step_close_added_client, &step);
  if (app->ownership.borrowed_client == NULL) {
    attempt(&errors, step_close_session, &step);
    attempt(&errors, step_close_renderer, &step);
  }
  attempt(&errors, step_release_source_commands, &step);
  attempt(&errors, step_release_mod_input_commands, &step);
  attempt(&errors, step_close_profile_routing, &step);
  app->profile_configuration = NULL;
  attempt(&errors, step_close_configuration_scripts, &step);
  app->configuration_scripts = NULL;
  attempt(&errors, step_close_content, &step);
  app->local_seat_count = 0;
  for (size_t i = 0; i < app->network_player_identity_count; i++) free(app->network_player_identities[i].social_id);
  app->network_player_identity_count = 0;
  size_t failures = errors.count;
  free(errors.items);
  if (failures > 0) qa_raise("AggregateError", 0, (long long)failures, "Application shutdown failed");
}
