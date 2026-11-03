/* Port of src/app/bootstrap/remote-application.ts close and closeOwned (TS
 * 4231-4429 in the 2026-11-03 tree), the videoRestart and
 * clientCommandsBlocked getters (TS 1078-1083), the ClientSourceLifetime the
 * application publishes as client.source.current, and the C release of a
 * dropped application (TS GC). GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "remote_application_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"

/* ---- getters ------------------------------------------------------------ */
qa_app_video_restart *qa_remote_application_video_restart(const qa_remote_application *app) {
  return app->ownership.borrowed ? app->ownership.client->video_restart : app->owned_video_restart;
}
bool qa_remote_application_client_commands_blocked(const qa_remote_application *app) {
  qa_app_video_restart *restart = qa_remote_application_video_restart(app);
  return (app->capture != NULL && qa_app_capture_pending_readback(app->capture))
      || (restart != NULL && qa_app_video_restart_pending(restart));
}

/* ---- the ClientSourceLifetime (TS `this` as client.source.current) ----- */
typedef struct lifetime_call { qa_remote_application *app; } lifetime_call;
static void lifetime_prepare_retirement_body(void *ctx) { qa_remote_application_prepare_retirement(((lifetime_call *)ctx)->app); }
static void lifetime_retire_body(void *ctx) { qa_remote_application_retire(((lifetime_call *)ctx)->app); }
static const char *lifetime_error(qa_remote_application *app, const qa_caught_error *caught) {
  snprintf(app->lifetime_error, sizeof app->lifetime_error, "%s", caught->message);
  return app->lifetime_error;
}
static const char *lifetime_prepare_retirement(void *ctx) {
  lifetime_call call = {ctx};
  qa_caught_error caught;
  return qa_try(lifetime_prepare_retirement_body, &call, &caught) ? NULL : lifetime_error(ctx, &caught);
}
static void lifetime_release_settings(void *ctx) { qa_remote_application_release_settings(ctx); }
static const char *lifetime_retire(void *ctx) {
  lifetime_call call = {ctx};
  qa_caught_error caught;
  return qa_try(lifetime_retire_body, &call, &caught) ? NULL : lifetime_error(ctx, &caught);
}
typedef struct video_call { qa_remote_application *app; qa_app_prepared_video_presentation *out; bool prepared; } video_call;
static void lifetime_video_body(void *ctx) {
  video_call *call = ctx;
  call->prepared = qa_remote_application_prepare_video_restart(call->app, call->out);
}
static const char *lifetime_prepare_video_restart(void *ctx, const void **out_prepared) {
  qa_remote_application *app = ctx;
  video_call call = {app, &app->lifetime_video, false};
  qa_caught_error caught;
  if (!qa_try(lifetime_video_body, &call, &caught)) return lifetime_error(app, &caught);
  *out_prepared = call.prepared ? &app->lifetime_video : NULL;
  return NULL;
}
/* TS prepareRecording(context) as a ClientRecordingFeed. */
static const char *lifetime_prepare_recording(void *ctx, qa_command_context source, qa_app_client_recording_feed *out) {
  qa_remote_application *app = ctx;
  return qa_remote_application_prepare_recording_feed(app, source, out);
}
void qa_remote_application_init_lifetime(qa_remote_application *app) {
  qa_app_client_source_lifetime *lifetime = &app->source_lifetime;
  memset(lifetime, 0, sizeof *lifetime);
  lifetime->ctx = app;
  lifetime->prepare_recording = lifetime_prepare_recording;
  lifetime->prepare_video_restart = lifetime_prepare_video_restart;
  lifetime->capture_map = NULL;
  lifetime->prepare_retirement = lifetime_prepare_retirement;
  lifetime->release_settings = lifetime_release_settings;
  lifetime->retire = lifetime_retire;
}

/* ---- close (TS 4231-4236) ----------------------------------------------- */
static void close_owned_body(void *ctx) { qa_remote_application_close_owned(ctx); }
void qa_remote_application_close(qa_remote_application *app) {
  if (app->close_started) {
    if (app->close_failed) qa_rethrow(&app->close_error);
    return;
  }
  app->close_started = true;
  app->closing = true;
  app->stopping = true;
  app->close_failed = !qa_try(close_owned_body, app, &app->close_error);
  if (app->close_failed) qa_rethrow(&app->close_error);
}

/* ---- closeOwned (TS 4238-4429) ------------------------------------------ */
typedef struct close_state { qa_remote_application *app; qa_remote_seat_peer *peer; qa_remote_world_frontend *frontend; } close_state;
static void close_peer(void *ctx) {
  close_state *state = ctx;
  qa_remote_application_retire_remote_seat(state->app, state->peer, false);
}
static void close_stop_borrowed_recording(void *ctx) {
  qa_remote_application *app = ((close_state *)ctx)->app;
  qa_app_client_bootstrap *client = app->ownership.client;
  if (client->stop_recording == NULL) return;
  const char *error = client->stop_recording(client->stop_recording_ctx, &app->source_lifetime);
  if (error != NULL) qa_raise("Error", 0, 0, "%s", error);
}
static void close_stop_owned_recording(void *ctx) {
  qa_remote_application *app = ((close_state *)ctx)->app;
  if (app->owned_recording == NULL) return;
  const char *error = qa_app_client_demo_recording_stop(app->owned_recording);
  if (error != NULL) qa_raise("Error", 0, 0, "%s", error);
}
static void close_stop_cinematic(void *ctx) { qa_remote_application_stop_cinematic(((close_state *)ctx)->app); }
static void close_video(void *ctx) {
  qa_remote_application *app = ((close_state *)ctx)->app;
  if (app->owned_video_restart != NULL) qa_app_video_restart_close(app->owned_video_restart);
}
static void close_save(void *ctx) { qa_remote_application_save_source_settings(((close_state *)ctx)->app); }
static void close_borrowed_capture(void *ctx) { qa_app_capture_before_world_change(((close_state *)ctx)->app->ownership.client->capture); }
static void close_borrowed_frontend(void *ctx) {
  qa_app_client_bootstrap *client = ((close_state *)ctx)->app->ownership.client;
  client->activate_frontend(client->bootstrap_ctx, NULL);
}
static void close_q3_browser(void *ctx) {
  qa_remote_application *app = ((close_state *)ctx)->app;
  if (app->q3_browser != NULL) qa_q3_browser_view_close(app->q3_browser);
}
static void close_capture(void *ctx) {
  qa_remote_application *app = ((close_state *)ctx)->app;
  if (!app->ownership.borrowed && app->capture != NULL) qa_app_capture_close(app->capture);
}
static void close_q3_client(void *ctx) {
  qa_remote_application *app = ((close_state *)ctx)->app;
  qa_app_application_q3_client *client = qa_remote_presentation_q3_client(qa_remote_application_presentation(app));
  if (client != NULL) qa_app_application_q3_client_shutdown(client);
}
static void close_release_settings(void *ctx) { qa_remote_application_release_settings(((close_state *)ctx)->app); }
static void close_clear_presentation(void *ctx) { qa_remote_application_clear_source_presentation(((close_state *)ctx)->app); }
static void close_disconnect(void *ctx) { qa_remote_application_disconnect_source(((close_state *)ctx)->app); }
static void close_remote_content(void *ctx) {
  qa_remote_application *app = ((close_state *)ctx)->app;
  if (app->remote_content != NULL) qa_app_remote_content_mounts_close(app->remote_content);
}
static void close_qw_downloads(void *ctx) {
  qa_remote_application *app = ((close_state *)ctx)->app;
  if (app->qw_downloads != NULL) qa_app_qw_download_receiver_close(app->qw_downloads);
}
static void close_q3_downloads(void *ctx) {
  qa_remote_application *app = ((close_state *)ctx)->app;
  if (app->q3_downloads != NULL) qa_app_q3_client_downloads_close(app->q3_downloads);
}
static void close_source(void *ctx) {
  qa_remote_application *app = ((close_state *)ctx)->app;
  const qa_remote_source *source = qa_remote_application_source(app);
  if (source->kind == QA_REMOTE_SOURCE_LIVE) qa_app_remote_seat_network_close(&source->network);
  else if (source->kind == QA_REMOTE_SOURCE_GTV) qa_app_gtv_remote_source_close(source->watch);
  else qa_app_recorded_source_close(source->playback);
}
static void close_transport(void *ctx) {
  qa_remote_application *app = ((close_state *)ctx)->app;
  const qa_remote_source *source = qa_remote_application_source(app);
  if (source->kind == QA_REMOTE_SOURCE_LIVE) qa_app_application_transport_close(source->transport);
}
static void close_controls(void *ctx) {
  qa_remote_application *app = ((close_state *)ctx)->app;
  if (app->controls != NULL) qa_app_application_input_close(app->controls);
}
static void close_frontend_audio(void *ctx) {
  close_state *state = ctx;
  if (state->frontend != NULL) qa_app_application_audio_close(state->frontend->audio);
}
static void close_frontend_effects(void *ctx) {
  close_state *state = ctx;
  if (state->frontend != NULL) qa_app_application_effects_close(state->frontend->effects);
}
static void close_frontend_art(void *ctx) {
  close_state *state = ctx;
  if (state->frontend != NULL) qa_native_ui_art_close(state->frontend->art);
}
static void close_frontend_assets(void *ctx) {
  close_state *state = ctx;
  if (state->frontend != NULL) qa_app_application_assets_close(state->frontend->assets);
}
static void close_retire_frontends(void *ctx) { qa_remote_application_retire_frontends(((close_state *)ctx)->app); }
static void close_execute(void *ctx) { qa_remote_renderer_execute(((close_state *)ctx)->app, NULL, 0); }
static void close_scripts(void *ctx) {
  qa_remote_application *app = ((close_state *)ctx)->app;
  if (app->client_command_owner != NULL) qa_app_console_script_files_close(app->client_command_owner->scripts);
}
static void close_session(void *ctx) { qa_engine_session_close(((close_state *)ctx)->app->session); }
static void close_renderer(void *ctx) { qa_app_native_renderer_close(((close_state *)ctx)->app->renderer); }
static void close_image_settings(void *ctx) { qa_app_image_settings_close(((close_state *)ctx)->app->image_settings); }
static void close_browser(void *ctx) {
  qa_remote_application *app = ((close_state *)ctx)->app;
  if (app->browser.owned) qa_app_startup_server_browser_close(app->browser.browser);
}
static void close_loaded(void *ctx) {
  qa_remote_application *app = ((close_state *)ctx)->app;
  if (app->loaded_content != NULL) qa_app_loaded_content_close(app->loaded_content);
}
static void close_identities(void *ctx) {
  qa_remote_application *app = ((close_state *)ctx)->app;
  if (!qa_app_remote_seat_identities_published(app->configuration->identities))
    qa_app_remote_seat_identities_discard(app->configuration->identities);
}
static void close_configuration_images(void *ctx) {
  qa_app_image_settings_close(((close_state *)ctx)->app->configuration->images.settings);
}
static void step(void (*fn)(void *ctx), close_state *state, bool *failed) {
  qa_caught_error caught;
  if (!qa_try(fn, state, &caught)) *failed = true;
}
void qa_remote_application_close_owned(qa_remote_application *app) {
  bool failed = false;
  close_state state = {app, NULL, NULL};
  size_t peer_count = app->remote_seat_count;
  qa_remote_seat_peer **peers = calloc(peer_count + 1, sizeof *peers);
  if (peers == NULL) qa_range_error("Out of memory");
  if (peer_count > 0) memcpy(peers, app->remote_seats, peer_count * sizeof *peers);
  for (size_t i = 0; i < peer_count; i++) {
    state.peer = peers[i];
    step(close_peer, &state, &failed);
  }
  free(peers);
  if (app->ownership.borrowed) step(close_stop_borrowed_recording, &state, &failed);
  step(close_stop_owned_recording, &state, &failed);
  if (app->peer_admission != NULL) {
    /* TS cancels the suspended admission and awaits the pending advance; the C advance never stays pending
     * across frames (advanceSource), so only the admission record is dropped. */
    app->peer_admission = NULL;
    app->pending_advance = NULL;
  }
  step(close_stop_cinematic, &state, &failed);
  step(close_video, &state, &failed);
  if (app->owned_video_restart != NULL) {
    qa_app_video_restart_destroy(app->owned_video_restart);
    app->owned_video_restart = NULL;
  }
  bool current = !app->ownership.borrowed || app->ownership.client->source_current == &app->source_lifetime;
  if (app->configuration_published && current) step(close_save, &state, &failed);
  if (app->ownership.borrowed && app->ownership.client->source_current == &app->source_lifetime) {
    step(close_borrowed_capture, &state, &failed);
    step(close_borrowed_frontend, &state, &failed);
    app->ownership.client->source_current = NULL;
  }
  bool closing_during_step = app->stepping;
  if (closing_during_step) step(close_q3_browser, &state, &failed);
  step(close_capture, &state, &failed);
  if (!app->ownership.borrowed && app->capture != NULL) qa_remote_arena_add(&app->arena, (void (*)(void *))qa_app_capture_destroy, app->capture);
  app->capture = NULL;
  if (!closing_during_step) step(close_q3_client, &state, &failed);
  if (!closing_during_step) step(close_q3_browser, &state, &failed);
  step(close_release_settings, &state, &failed);
  step(close_clear_presentation, &state, &failed);
  step(close_disconnect, &state, &failed);
  app->closed = true;
  app->stopping = true;
  app->world_load_generation++;
  app->client_input_count = 0;
  /* TS initialComponents?.close(): only the unified replica builds one (the application frees it at destroy). */
  if (app->initial_components != NULL) qa_app_remote_component_view_close(app->initial_components);
  app->initial_components = NULL;
  state.frontend = app->frontend;
  app->frontend = NULL;
  void (*closes[])(void *) = {close_remote_content, close_qw_downloads, close_q3_downloads, close_source, close_transport,
      close_controls, close_frontend_audio, close_frontend_effects, close_frontend_art, close_frontend_assets};
  for (size_t i = 0; i < sizeof closes / sizeof closes[0]; i++) step(closes[i], &state, &failed);
  app->closed_frontend = state.frontend;
  step(close_retire_frontends, &state, &failed);
  step(close_execute, &state, &failed);
  if (app->client_command_owner != NULL)
    for (size_t i = 0; i < app->source_handler_count; i++)
      (void)qa_command_buffer_unregister(app->client_command_owner->commands, app->source_handlers[i].name,
          app->source_handlers[i].handler, app->source_handlers[i].handler_ctx, true);
  if (!app->ownership.borrowed
      || (qa_app_prepared_startup_scripts(app->ownership.client->prepared) != (app->client_command_owner != NULL ? app->client_command_owner->scripts : NULL)
          && app->ownership.client->configuration_current.scripts != (app->client_command_owner != NULL ? app->client_command_owner->scripts : NULL)))
    step(close_scripts, &state, &failed);
  if (!app->ownership.borrowed) {
    step(close_session, &state, &failed);
    step(close_renderer, &state, &failed);
    step(close_image_settings, &state, &failed);
  }
  step(close_browser, &state, &failed);
  step(close_loaded, &state, &failed);
  step(close_identities, &state, &failed);
  step(close_configuration_images, &state, &failed);
  if (failed) qa_raise("AggregateError", 0, 0, "Remote application shutdown failed");
}

/* ---- destroy (TS GC) ---------------------------------------------------- */
static void close_body(void *ctx) { qa_remote_application_close(ctx); }
static void destroy_frontend(qa_remote_world_frontend *frontend) {
  if (frontend == NULL) return;
  if (frontend->effects != NULL) qa_app_application_effects_destroy(frontend->effects);
  if (frontend->audio != NULL) qa_app_application_audio_destroy(frontend->audio);
  free(frontend->art);
  if (frontend->assets != NULL) qa_app_application_assets_destroy(frontend->assets);
  free(frontend);
}
void qa_remote_application_destroy(qa_remote_application *app) {
  if (app == NULL) return;
  if (!app->close_started) {
    qa_caught_error caught;
    (void)qa_try(close_body, app, &caught);
  }
  destroy_frontend(app->closed_frontend);
  if (app->controls != NULL) qa_app_application_input_destroy(app->controls);
  if (app->initial_presentation != NULL) qa_app_world_seat_presentation_destroy(app->initial_presentation);
  qa_remote_application_close_qw_downloads(app);
  qa_remote_application_close_q3_downloads(app);
  if (app->q3_content != NULL) {
    qa_app_q3_client_content_close(app->q3_content);
    qa_app_q3_client_content_destroy(app->q3_content);
    qa_remote_q3_content_host_release(app->q3_content_host);
  }
  if (app->owned_recording != NULL) qa_app_client_demo_recording_destroy(app->owned_recording);
  if (app->q3_browser != NULL) qa_q3_browser_view_destroy(app->q3_browser);
  if (app->q3_browser_host != NULL) qa_app_startup_server_browser_q3_host_destroy(app->q3_browser_host);
  qa_app_remote_seat_network_destroy(&app->initial_source.network);
  if (app->initial_source.kind == QA_REMOTE_SOURCE_RECORDED && app->initial_source.playback != NULL)
    qa_app_recorded_source_destroy(app->initial_source.playback);
  if (app->initial_source.kind == QA_REMOTE_SOURCE_GTV && app->initial_source.watch != NULL)
    qa_app_gtv_remote_source_destroy(app->initial_source.watch);
  free(app->initial_source.label);
  if (app->initial_source.kind == QA_REMOTE_SOURCE_LIVE && app->initial_source.transport != NULL)
    qa_app_application_transport_destroy(app->initial_source.transport);
  qa_app_remote_seat_presentation_destroy(&app->initial_remote);
  if (app->loaded_content != NULL) qa_app_loaded_content_destroy(app->loaded_content);
  if (app->key_profile != NULL && !app->key_profile_published) qa_app_key_profile_destroy(app->key_profile);
  if (app->player_progress != NULL) qa_app_player_progress_store_destroy(app->player_progress);
  free(app->source_events);
  qa_app_unhandled_effects_free(app->unhandled_effects, app->unhandled_effect_count);
  qa_remote_arena_release(&app->arena);
  if (app->browser.owned && app->browser.browser != NULL) qa_app_startup_server_browser_destroy(app->browser.browser);
  if (app->browser_settings != NULL) { qa_config_store_destroy(app->browser_settings); free(app->browser_settings); }
  if (app->owned_keys != NULL) qa_app_keys_destroy(app->owned_keys);
  if (!app->ownership.borrowed) {
    if (app->renderer != NULL) qa_app_native_renderer_destroy(app->renderer);
    if (app->image_settings != NULL) qa_app_image_settings_destroy(app->image_settings);
    if (app->session != NULL) qa_engine_session_destroy(app->session);
  }
  if (app->owned_mounted != NULL) qa_app_configuration_content_destroy(app->owned_mounted);
  if (app->owned_identity != NULL) { qa_identity_owner_destroy(app->owned_identity); free(app->owned_identity); }
  qa_app_options_destroy(&app->launch_options); /* every assignment hands the application an owned copy */
  qa_remote_application_free_storage(app);
}
