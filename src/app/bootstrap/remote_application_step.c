/* Port of src/app/bootstrap/remote-application.ts, the step region (TS
 * 3905-4229 in the 2026-11-03 tree): step, sourceSessionActions, run,
 * readPixels and captureNextFrame. Async members are synchronous; a TS throw
 * is qa_raise, a TS catch/finally is qa_try. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "remote_application_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/component_media.h"
#include "qa/app/bootstrap/diagnostic_tools.h"
#include "qa/app/bootstrap/frame_loop.h"
#include "qa/app/bootstrap/frame_time.h"
#include "qa/app/bootstrap/q1_session_actions.h"
#include "qa/content/q3/foundation/arsenal.h"
#include "qa/foundation/support.h"
#include "qa/input/seat.h"
#include "qa/network/common/js_number.h"

static void *xcalloc(size_t count, size_t size) {
  void *memory = calloc(count == 0 ? 1 : count, size == 0 ? 1 : size);
  if (memory == NULL) qa_range_error("Out of memory");
  return memory;
}
static double now_milliseconds(void) { return qa_app_remote_access_now_milliseconds(NULL); }

/* ---- sourceSessionActions (TS 4182-4210) ------------------------------- */
void qa_remote_application_source_session_actions(qa_remote_application *app, const qa_app_audio_seat_events *batches,
    size_t batch_count) {
  if (qa_remote_application_source(app)->kind != QA_REMOTE_SOURCE_LIVE) return;
  qa_seat_id *leaving = xcalloc(batch_count * 4 + 1, sizeof *leaving);
  qa_cleanup guard;
  qa_cleanup_push(&guard, free, leaving);
  size_t leaving_count = 0;
  for (size_t b = 0; b < batch_count; b++) {
    const qa_app_audio_seat_events *batch = &batches[b];
    qa_app_local_input *local = NULL;
    size_t count = app->controls != NULL ? qa_app_application_input_local_count(app->controls) : 0;
    for (size_t i = 0; i < count && local == NULL; i++) {
      qa_app_local_input *candidate = qa_app_application_input_local(app->controls, i);
      if (qa_same_seat(qa_session_seat_id(candidate->player.seat), batch->seat)) local = candidate;
    }
    if (local == NULL) continue;
    qa_actor_id actor = local->player.actor;
    qa_seat_id seat = qa_session_seat_id(local->player.seat);
    qa_app_seat_actor pair = {actor, seat};
    leaving_count += qa_app_q1_session_departures(batch->events, batch->event_count, &pair, 1, leaving + leaving_count);
    for (size_t i = 0; i < batch->event_count; i++) {
      const qa_app_sim_presentation_event *source = &batch->events[i];
      if (source->has_recipient && !qa_same_actor(source->recipient, actor)) continue;
      const char *completion = qa_app_source_level_completion(source);
      if (completion == NULL) continue;
      if (app->player_progress == NULL) {
        char *file = qa_remote_join3(app->input_config->root, "player-progress.json", ".");
        app->player_progress = qa_app_player_progress_store_open(file);
        free(file);
        if (app->player_progress == NULL) qa_range_error("Out of memory");
      }
      char participant[64], index[40];
      qa_net_js_number((double)seat.index, index, sizeof index);
      snprintf(participant, sizeof participant, "local-seat:%s", index);
      const char *map = app->launch_options.map != NULL ? app->launch_options.map : "";
      size_t length = strlen(source->content) + strlen(map) + 16;
      char *event = xcalloc(length, 1);
      snprintf(event, length, "level:%s:%s", source->content, map);
      qa_app_player_progress_event record;
      memset(&record, 0, sizeof record);
      record.kind = "level-completed";
      record.source = completion;
      record.participant = participant;
      record.event = event;
      record.map = map;
      qa_cleanup event_guard;
      qa_cleanup_push(&event_guard, free, event);
      (void)qa_app_player_progress_store_record(app->player_progress, record);
      qa_cleanup_pop(&event_guard);
      free(event);
    }
  }
  bool everyone = leaving_count > 0;
  for (size_t i = 0; i < app->configuration->seat_count && everyone; i++) {
    bool found = false;
    for (size_t j = 0; j < leaving_count && !found; j++) found = qa_same_seat(leaving[j], qa_session_seat_id(app->configuration->seats[i]));
    everyone = found;
  }
  if (everyone) qa_remote_application_request_quit(app);
  else
    for (size_t j = 0; j < leaving_count; j++) {
      bool queued = false;
      for (size_t k = 0; k < app->command_count && !queued; k++)
        queued = strcmp(app->commands[k].name, "local_drop") == 0 && app->commands[k].has_seat && qa_same_seat(app->commands[k].seat, leaving[j]);
      if (!queued) qa_remote_application_queue_command(app, "local_drop", NULL, 0, &leaving[j], NULL);
    }
  qa_cleanup_pop(&guard);
  free(leaving);
}

/* ---- step (TS 3905-4180) ----------------------------------------------- */
typedef struct step_state {
  qa_remote_application *app;
  double elapsed_milliseconds;
  bool result; qa_simulation_output *out;
  double frame_milliseconds, now;
  qa_app_audio_seat_events *batches; size_t batch_count;
  qa_audio_listener *listeners; size_t listener_count;
  qa_app_application_effect_sound *sounds;
  qa_app_source_effect_sound *drained; size_t drained_count;
  qa_app_recipient_effect_sounds *recipients; size_t recipient_count;
  qa_app_sim_presentation_event *filtered;
  qa_app_audio_seat_events *audio_batches; qa_app_sim_presentation_event **audio_events;
} step_state;
static void step_release(step_state *state) {
  free(state->batches);
  free(state->listeners);
  free(state->sounds);
  free(state->drained);
  for (size_t i = 0; i < state->recipient_count; i++) free(state->recipients[i].sounds);
  free(state->recipients);
  free(state->filtered);
  for (size_t i = 0; state->audio_events != NULL && i < state->batch_count; i++) free(state->audio_events[i]);
  free(state->audio_events);
  free(state->audio_batches);
}
static void advance_live(void *ctx) {
  step_state *state = ctx;
  qa_remote_application *app = state->app;
  const qa_remote_source *source = qa_remote_application_source(app);
  if (source->kind == QA_REMOTE_SOURCE_LIVE) {
    if (app->primary_peer == NULL) qa_app_remote_seat_network_poll(&source->network, state->now);
  } else if (source->kind == QA_REMOTE_SOURCE_GTV) {
    qa_app_gtv_remote_source_poll(source->watch);
    (void)qa_remote_presentation_sample(qa_remote_application_remote(app), state->now, &app->sample_scratch);
  } else
    qa_app_recorded_source_advance(source->playback, state->frame_milliseconds, app->frames,
        (double)(int64_t)qa_app_presentation_time_milliseconds(&app->presentation_time));
}
static void advance_poll(void *ctx) {
  step_state *state = ctx;
  qa_remote_application *app = state->app;
  const qa_remote_source *source = qa_remote_application_source(app);
  if (source->kind == QA_REMOTE_SOURCE_LIVE && app->primary_peer == NULL) qa_app_remote_seat_network_poll(&source->network, state->now);
}
static bool any_console(qa_remote_application *app) {
  if (app->controls != NULL) {
    size_t count = qa_app_application_input_local_count(app->controls);
    for (size_t i = 0; i < count; i++) {
      qa_seat_input_focus focus = qa_seat_input_get_focus(qa_app_application_input_local(app->controls, i)->input);
      if (focus.kind != NULL && strcmp(focus.kind, "console") == 0) return true;
    }
    return false;
  }
  if (!app->ownership.borrowed) return false;
  size_t count = 0;
  const qa_app_prepared_seat *seats = qa_app_prepared_startup_seats(app->ownership.client->prepared, &count);
  for (size_t i = 0; i < count; i++) {
    qa_seat_input_focus focus = qa_seat_input_get_focus(seats[i].input);
    if (focus.kind != NULL && strcmp(focus.kind, "console") == 0) return true;
  }
  return false;
}
static qa_app_capture *step_capture(qa_remote_application *app) {
  return app->ownership.borrowed ? app->ownership.client->capture : app->capture;
}
static void swap_buffers(qa_remote_application *app) {
  qa_render_command command;
  memset(&command, 0, sizeof command);
  command.kind = QA_RENDER_COMMAND_SWAP_BUFFERS;
  qa_remote_renderer_execute(app, &command, 1);
}
static bool dispatch_continue(void *ctx) { return !qa_remote_application_client_commands_blocked(ctx); }
static void dispatch_after(void *ctx) { qa_remote_application_dispatch_commands(ctx); }
static void step_body(void *ctx) {
  step_state *state = ctx;
  qa_remote_application *app = state->app;
  qa_app_startup_server_browser_poll(qa_remote_application_server_browser(app));
  if (app->q3_browser != NULL) qa_q3_browser_view_poll(app->q3_browser);
  app->source_event_count = 0;
  qa_app_unhandled_effects_free(app->unhandled_effects, app->unhandled_effect_count);
  app->unhandled_effects = NULL;
  app->unhandled_effect_count = 0;
  qa_remote_application_drain_seat_publications(app);
  if (!app->ownership.borrowed && app->controls != NULL) qa_app_application_input_pump(app->controls, false);
  else if (!app->ownership.borrowed) {
    size_t count = 0;
    qa_sdl_event *events = qa_sdl_window_poll_events(qa_remote_application_window(app), &count);
    for (size_t i = 0; i < count; i++)
      if (events[i].kind == QA_SDL_EVENT_QUIT || (events[i].kind == QA_SDL_EVENT_WINDOW && events[i].as.window.event == 14))
        qa_remote_application_request_quit(app);
    free(events);
  }
  if (!app->ownership.borrowed) {
    qa_remote_application_flush_client_commands(app);
    if (!qa_remote_application_client_commands_blocked(app)) {
      bool continued = qa_remote_application_advance_client_startup(app);
      qa_remote_application_dispatch_commands(app);
      if (!continued && app->client_command_owner != NULL)
        (void)qa_command_buffer_execute_scripts_async(app->client_command_owner->commands, dispatch_after, app, dispatch_continue, app);
    }
  }
  if (app->stopping) {
    qa_remote_application_present_loading_frame(app);
    return;
  }
  double elapsed = state->elapsed_milliseconds;
  if (app->reset_frame_elapsed) {
    elapsed = 1;
    app->reset_frame_elapsed = false;
  }
  qa_cvar_registry *time_cvars = app->client_command_owner != NULL ? app->client_command_owner->cvars : NULL;
  double source_milliseconds = time_cvars == NULL ? elapsed
      : qa_app_source_frame_milliseconds(qa_cvar_registry_dialect(time_cvars), elapsed, qa_app_read_frame_time_controls(time_cvars),
          (qa_app_frame_host){false, false});
  double timescale = 1;
  if (time_cvars != NULL) {
    qa_cvar_read read;
    if (qa_cvar_registry_find(time_cvars, "timescale", &read)) {
      timescale = read.numeric_value;
      qa_cvar_read_free(&read);
    }
  }
  qa_app_capture_frame_result capture = qa_app_source_capture_frame(source_milliseconds,
      time_cvars != NULL ? qa_cvar_registry_variable_value(time_cvars, "cl_avidemo") : 0, timescale,
      strcmp(qa_remote_application_network_phase(app), "active") == 0,
      time_cvars != NULL && qa_cvar_registry_variable_value(time_cvars, "cl_forceavidemo") != 0);
  state->frame_milliseconds = capture.milliseconds;
  if (capture.capture && step_capture(app) != NULL) qa_app_capture_frame(step_capture(app));
  app->elapsed += state->frame_milliseconds;
  state->now = now_milliseconds();
  qa_app_presentation_time_advance(&app->presentation_time, state->now, elapsed, state->frame_milliseconds);
  const qa_remote_live_network *network = qa_remote_application_network(app);
  const qa_remote_presentation *remote = qa_remote_application_remote(app);
  if (network != NULL && network->family == QA_APP_REMOTE_FAMILY_Q2 && remote->family == QA_APP_REMOTE_FAMILY_Q2
      && qa_app_remote_seat_network_phase(network) == QA_APP_PHASE_ACTIVE) {
    const char *info = qa_app_q2_remote_userinfo(remote->q2);
    char **previous = app->primary_peer == NULL ? &app->initial_userinfo : &app->primary_peer->userinfo;
    if (*previous == NULL || strcmp(info, *previous) != 0) {
      char *copy = strdup(info);
      if (copy == NULL) qa_range_error("Out of memory");
      qa_app_q2_client_network_userinfo(network->q2, copy);
      free(*previous);
      *previous = copy;
    }
  }
  for (size_t i = 0; i < app->remote_seat_count; i++)
    if (!app->remote_seats[i]->closed) qa_app_remote_seat_channel_begin_frame(app->remote_seats[i]->channel, state->now, elapsed);
  if (!qa_remote_application_advance_source(app, advance_live, state)) {
    qa_remote_application_present_loading_frame(app);
    return;
  }
  qa_remote_application_poll_remote_seats(app, state->now);
  qa_remote_application_drain_seat_publications(app);
  remote = qa_remote_application_remote(app);
  if (remote->family == QA_APP_REMOTE_FAMILY_QW) {
    if (app->client_command_owner != NULL) {
      size_t count = 0;
      qa_cvar_effect *effects = qa_cvar_registry_take_effects(app->client_command_owner->cvars, &count);
      for (size_t i = 0; i < count; i++) qa_cvar_effect_free(&effects[i]);
      free(effects);
    }
    qa_app_qw_remote_prepare_skins(remote->qw, NULL, NULL);
  }
  app->frames++;
  qa_remote_client_input *inputs = app->client_inputs;
  size_t input_count = app->client_input_count;
  app->client_inputs = NULL;
  app->client_input_count = 0;
  app->client_input_capacity = 0;
  qa_cleanup inputs_guard;
  qa_cleanup_push(&inputs_guard, free, inputs);
  for (size_t i = 0; i < input_count; i++) {
    if (app->closed || inputs[i].generation != app->world_load_generation) continue;
    bool live = qa_remote_presentation_q3_client(qa_remote_application_presentation(app)) == inputs[i].client;
    for (size_t j = 0; j < app->remote_seat_count && !live; j++)
      live = app->remote_seats[j]->view != NULL
          && qa_remote_presentation_q3_client(qa_app_remote_seat_view_presentation(app->remote_seats[j]->view)) == inputs[i].client;
    if (live) qa_app_application_q3_client_input(inputs[i].client, &inputs[i].event);
  }
  qa_cleanup_pop(&inputs_guard);
  free(inputs);
  const char *phase = qa_remote_application_network_phase(app);
  if (strcmp(phase, "closed") == 0 || strcmp(phase, "rejected") == 0) {
    qa_remote_seat_peer *successor = NULL;
    for (size_t i = 0; i < app->remote_seat_count && successor == NULL; i++) {
      qa_remote_seat_peer *peer = app->remote_seats[i];
      if (!peer->closed && peer != app->primary_peer && qa_app_remote_seat_channel_active(peer->channel) && peer->view != NULL
          && peer->map != NULL && app->launch_options.map != NULL && strcmp(peer->map, app->launch_options.map) == 0)
        successor = peer;
    }
    if (successor == NULL) {
      if (app->controls != NULL) qa_app_application_input_stop_haptics(app->controls);
      qa_remote_application_request_quit(app);
      return;
    }
    qa_remote_application_drop_remote_seat(app, qa_remote_application_seat_id(app).index);
  }
  qa_remote_cinematic *cinematic = app->cinematic;
  if (cinematic != NULL) {
    if (!cinematic->current(cinematic->current_ctx)) {
      if (app->cinematic == cinematic) app->cinematic = NULL;
      qa_remote_application_close_cinematic(app, cinematic);
      return;
    }
    bool console_open = any_console(app);
    bool ended = qa_app_campaign_cinematic_frame(cinematic->movie, state->frame_milliseconds, app->frames, console_open, false);
    app->cinematic_presented = !console_open;
    remote = qa_remote_application_remote(app);
    const qa_simulation_output *output = qa_app_remote_seat_presentation_output(remote);
    qa_app_world_seat_presentation *presentation = qa_remote_application_presentation(app);
    if (console_open && output != NULL && presentation != NULL) {
      size_t model_count = 0, character_count = 0;
      const qa_app_simulation_presentation *models = qa_app_remote_seat_presentation_presentations(remote, &model_count);
      const qa_q3_character_view *characters = qa_app_remote_seat_presentation_character_views(remote, &character_count);
      qa_app_world_seat_presentation_prepare(presentation, &output->snapshot, models, model_count, characters, character_count);
      (void)qa_session_seat_present(qa_app_world_seat_presentation_local(presentation)->player.seat, output->snapshot,
          qa_app_native_renderer_backend(app->renderer));
      swap_buffers(app);
      app->cinematic_presented = true;
    }
    if (app->cinematic_presented) {
      if (step_capture(app) != NULL) qa_app_capture_drain(step_capture(app));
    } else if (!app->ownership.borrowed) qa_remote_application_present_loading_frame(app);
    if (ended) qa_remote_application_complete_cinematic(app, cinematic);
    return;
  }
  remote = qa_remote_application_remote(app);
  if (strcmp(qa_remote_application_network_phase(app), "active") != 0 || qa_app_remote_seat_presentation_output(remote) == NULL) {
    if (app->controls != NULL) qa_app_application_input_stop_haptics(app->controls);
    if (!app->ownership.borrowed) qa_remote_application_dispatch_commands(app);
    qa_remote_application_refresh_images(app);
    qa_remote_application_present_loading_frame(app);
    return;
  }
  const qa_remote_source *source = qa_remote_application_source(app);
  if (source->kind == QA_REMOTE_SOURCE_LIVE) (void)qa_remote_presentation_sample(remote, state->now, &app->sample_scratch);
  qa_remote_application_bind_seat(app, NULL);
  qa_app_application_q3_client *q3 = qa_remote_presentation_q3_client(qa_remote_application_presentation(app));
  if (q3 != NULL && app->controls != NULL) {
    qa_seat_id seat = qa_session_seat_id(qa_app_application_q3_client_options_of(q3)->local->player.seat);
    qa_app_q3_command_selection selection = qa_app_application_q3_client_user_command_selection(q3);
    qa_app_application_input_set_q3_command_selection(app->controls, seat, selection);
    const qa_q3_weapon_item *item = qa_q3_get_weapon_item(selection.weapon);
    qa_app_arsenal_selection arsenal = {qa_app_loaded_content_recipe(qa_remote_application_content(app))->inventory.provider,
        item != NULL ? item->item : NULL};
    qa_app_application_input_set_arsenal_selection(app->controls, seat, &arsenal);
  }
  source = qa_remote_application_source(app);
  remote = qa_remote_application_remote(app);
  if (source->kind == QA_REMOTE_SOURCE_LIVE && app->controls != NULL) {
    double duration = app->primary_peer != NULL ? qa_app_remote_seat_channel_frame_milliseconds(app->primary_peer->channel)
        : state->frame_milliseconds;
    const qa_simulation_output *output = qa_app_remote_seat_presentation_output(remote);
    qa_actor_command command = qa_app_application_input_build_for_seat(app->controls, qa_remote_application_seat_id(app), duration,
        app->primary_peer != NULL ? qa_app_remote_seat_channel_elapsed_milliseconds(app->primary_peer->channel) : app->elapsed,
        output->snapshot.frame.frame, elapsed);
    if (source->network.family == QA_APP_REMOTE_FAMILY_UNIFIED)
      qa_app_unified_client_network_submit_timed(source->network.unified, &command, 1, state->now, duration);
    else qa_app_remote_seat_network_submit(&source->network, &command, 1, state->now);
  }
  qa_remote_application_prepare_remote_seat_frames(app, state->now, elapsed);
  if (!app->ownership.borrowed) qa_remote_application_dispatch_commands(app);
  if (qa_remote_application_source(app)->kind == QA_REMOTE_SOURCE_LIVE && !qa_remote_application_advance_source(app, advance_poll, state)) {
    qa_remote_application_present_loading_frame(app);
    return;
  }
  remote = qa_remote_application_remote(app);
  if (remote->family == QA_APP_REMOTE_FAMILY_QW) qa_app_qw_remote_prepare_skins(remote->qw, NULL, NULL);
  if (strcmp(qa_remote_application_network_phase(app), "active") != 0) {
    if (app->controls != NULL) qa_app_application_input_stop_haptics(app->controls);
    return;
  }
  qa_remote_application_bind_seat(app, NULL);
  qa_remote_application_refresh_images(app);
  remote = qa_remote_application_remote(app);
  source = qa_remote_application_source(app);
  const qa_simulation_output *output = source->kind == QA_REMOTE_SOURCE_LIVE
      ? qa_remote_presentation_sample(remote, now_milliseconds(), &app->sample_scratch) : qa_app_remote_seat_presentation_output(remote);
  qa_remote_world_frontend *frontend = app->frontend;
  qa_app_world_seat_presentation *presentation = qa_remote_application_presentation(app);
  if (output == NULL) return;
  if (frontend == NULL || presentation == NULL) qa_raise("Error", 0, 0, "Active remote player has no frontend");
  if (app->primary_peer == NULL && remote->family == QA_APP_REMOTE_FAMILY_UNIFIED) {
    /* this.publishRemote({ ...output, events: this.remote.drainSimulationEvents() }) */
    qa_simulation_output published = *output;
    published.events = qa_app_unified_remote_drain_simulation_events(remote->unified, &published.event_count);
    qa_remote_application_publish_remote(app, &published);
  }
  if (app->primary_peer == NULL) {
    size_t count = 0;
    const qa_app_sim_presentation_event *events = qa_app_remote_seat_presentation_drain_events(remote, &count);
    free(app->source_events);
    app->source_events = xcalloc(count, sizeof *app->source_events);
    if (count > 0) memcpy(app->source_events, events, count * sizeof *events);
    app->source_event_count = count;
  } else app->source_event_count = 0;
  size_t model_count = 0, character_count = 0;
  const qa_app_simulation_presentation *models = qa_app_remote_seat_presentation_presentations(remote, &model_count);
  const qa_q3_character_view *characters = qa_app_remote_seat_presentation_character_views(remote, &character_count);
  if (app->primary_peer == NULL) {
    qa_app_application_effects_receive(frontend->effects, app->source_events, app->source_event_count);
    if (remote->family != QA_APP_REMOTE_FAMILY_Q3)
      qa_app_application_effects_prepare(frontend->effects, &output->snapshot, models, model_count, characters, character_count);
  }
  qa_app_unhandled_effects_free(app->unhandled_effects, app->unhandled_effect_count);
  app->unhandled_effects = qa_app_application_effects_drain_unhandled(frontend->effects, &app->unhandled_effect_count);
  for (size_t i = 0; i < app->unhandled_effect_count; i++) {
    const qa_app_unhandled_effect *effect = &app->unhandled_effects[i];
    size_t length = strlen(effect->source.content) + strlen(effect->reason) + 2;
    char *key = xcalloc(length, 1);
    snprintf(key, length, "%s:%s", effect->source.content, effect->reason);
    bool reported = false;
    for (size_t j = 0; j < app->reported_effect_gap_count && !reported; j++) reported = strcmp(app->reported_effect_gaps[j], key) == 0;
    if (reported) { free(key); continue; }
    if (app->reported_effect_gap_count == app->reported_effect_gap_capacity) {
      size_t capacity = app->reported_effect_gap_capacity == 0 ? 8 : app->reported_effect_gap_capacity * 2;
      char **grown = realloc(app->reported_effect_gaps, capacity * sizeof *grown);
      if (grown == NULL) { free(key); qa_range_error("Out of memory"); }
      app->reported_effect_gaps = grown;
      app->reported_effect_gap_capacity = capacity;
    }
    app->reported_effect_gaps[app->reported_effect_gap_count++] = key;
    size_t line_length = strlen(effect->source.kind) + strlen(effect->reason) + 32;
    char *line = xcalloc(line_length, 1);
    snprintf(line, line_length, "Unresolved %s effect: %s\n", effect->source.kind, effect->reason);
    qa_cleanup line_guard;
    qa_cleanup_push(&line_guard, free, line);
    qa_remote_application_print(app, line, NULL);
    qa_cleanup_pop(&line_guard);
    free(line);
  }
  bool has_primary_batch = app->primary_peer != NULL && app->primary_peer->view != NULL;
  qa_app_audio_seat_events primary_batch;
  memset(&primary_batch, 0, sizeof primary_batch);
  if (has_primary_batch) qa_app_remote_seat_view_prepare_frame(app->primary_peer->view, output, true, &primary_batch);
  state->batches = xcalloc(app->remote_seat_count + 1, sizeof *state->batches);
  state->listeners = xcalloc(app->remote_seat_count + 1, sizeof *state->listeners);
  qa_scene_queries primary_scene = qa_shared_as_scene_queries(qa_app_remote_seat_presentation_scene(remote));
  if (!has_primary_batch) {
    state->drained = qa_app_application_effects_drain_sounds(frontend->effects, &state->drained_count);
    state->recipients = qa_app_application_effects_drain_recipient_sounds(frontend->effects, &state->recipient_count);
    qa_actor_id actor = qa_app_world_seat_presentation_local(presentation)->player.actor;
    size_t total = state->drained_count;
    for (size_t i = 0; i < state->recipient_count; i++)
      if (qa_same_actor(state->recipients[i].recipient, actor)) total += state->recipients[i].sound_count;
    state->sounds = xcalloc(total, sizeof *state->sounds);
    size_t at = 0;
    for (size_t i = 0; i < state->drained_count; i++) state->sounds[at++] = state->drained[i];
    for (size_t i = 0; i < state->recipient_count; i++)
      if (qa_same_actor(state->recipients[i].recipient, actor))
        for (size_t j = 0; j < state->recipients[i].sound_count; j++) state->sounds[at++] = state->recipients[i].sounds[j];
    qa_app_audio_seat_events *batch = &state->batches[0];
    batch->seat = qa_remote_application_seat_id(app);
    batch->snapshot = &output->snapshot;
    batch->events = app->source_events;
    batch->event_count = app->source_event_count;
    batch->music = true;
    batch->scene = &primary_scene;
    batch->effect_sounds = state->sounds;
    batch->effect_sound_count = total;
  } else {
    state->batches[0] = primary_batch;
    state->batches[0].music = true;
  }
  state->batch_count = 1;
  if (!has_primary_batch) {
    if (app->initial_components != NULL)
      qa_app_remote_component_view_prepare_media(app->initial_components, app->source_events, app->source_event_count, true);
    qa_app_world_seat_presentation_source_events(presentation, app->source_events, app->source_event_count);
    qa_app_world_seat_presentation_prepare(presentation, &output->snapshot, models, model_count, characters, character_count);
    if (app->initial_components != NULL)
      qa_app_remote_component_view_prepare(app->initial_components, presentation, app->source_events, app->source_event_count,
          app->frames);
  } else {
    free(app->source_events);
    app->source_events = xcalloc(primary_batch.event_count, sizeof *app->source_events);
    if (primary_batch.event_count > 0) memcpy(app->source_events, primary_batch.events, primary_batch.event_count * sizeof *app->source_events);
    app->source_event_count = primary_batch.event_count;
  }
  qa_session_seat *seat = qa_app_world_seat_presentation_local(presentation)->player.seat;
  (void)qa_session_seat_present(seat, output->snapshot, qa_app_native_renderer_backend(app->renderer));
  qa_scene_camera camera = qa_app_world_seat_presentation_camera(presentation);
  qa_actor_id actor = qa_app_world_seat_presentation_local(presentation)->player.actor;
  qa_audio_listener *listener = &state->listeners[state->listener_count++];
  listener->seat = qa_session_seat_id(seat);
  listener->has_actor = true;
  listener->actor = actor;
  listener->origin = camera.origin;
  listener->axis = camera.axis;
  listener->gain = 1;
  listener->underwater = qa_remote_application_underwater(app, camera, actor, &frontend->scene);
  qa_scene_queries *peer_scenes = xcalloc(app->remote_seat_count + 1, sizeof *peer_scenes);
  qa_cleanup scene_guard;
  qa_cleanup_push(&scene_guard, free, peer_scenes);
  for (size_t i = 0; i < app->remote_seat_count; i++) {
    qa_remote_seat_peer *peer = app->remote_seats[i];
    if (peer->closed || peer == app->primary_peer || peer->view == NULL || !qa_app_remote_seat_channel_active(peer->channel)
        || !qa_remote_application_peer_world_current(app, peer)) continue;
    const qa_remote_presentation *peer_remote = qa_app_remote_seat_source_remote(peer->source);
    const qa_simulation_output *received = qa_remote_presentation_sample(peer_remote, state->now, &peer->sample_scratch);
    if (received == NULL) continue;
    qa_app_remote_seat_view_prepare_frame(peer->view, received, false, &state->batches[state->batch_count++]);
    qa_app_world_seat_presentation *view = qa_app_remote_seat_view_presentation(peer->view);
    (void)qa_session_seat_present(qa_app_world_seat_presentation_local(view)->player.seat, received->snapshot,
        qa_app_native_renderer_backend(app->renderer));
    qa_scene_camera peer_camera = qa_app_world_seat_presentation_camera(view);
    qa_actor_id peer_actor = qa_app_world_seat_presentation_local(view)->player.actor;
    peer_scenes[i] = qa_shared_as_scene_queries(qa_app_remote_seat_presentation_scene(peer_remote));
    qa_audio_listener *entry = &state->listeners[state->listener_count++];
    entry->seat = qa_session_seat_id(peer->seat);
    entry->has_actor = true;
    entry->actor = peer_actor;
    entry->origin = peer_camera.origin;
    entry->axis = peer_camera.axis;
    entry->gain = 1;
    entry->underwater = qa_remote_application_underwater(app, peer_camera, peer_actor, &peer_scenes[i]);
  }
  swap_buffers(app);
  if (app->capture != NULL) qa_app_capture_drain(app->capture);
  /* audioBatches.map(batch => unified ? { ...batch, events: batch.events.filter(event => !presentationAudioControl(event)) }
   * : batch): a batch of a unified seat (the primary with a unified remote, or a unified peer) drops its audio controls. */
  state->audio_batches = xcalloc(state->batch_count, sizeof *state->audio_batches);
  state->audio_events = xcalloc(state->batch_count, sizeof *state->audio_events);
  for (size_t b = 0; b < state->batch_count; b++) {
    qa_app_audio_seat_events batch = state->batches[b];
    bool unified = qa_same_seat(batch.seat, qa_remote_application_seat_id(app))
        ? remote->family == QA_APP_REMOTE_FAMILY_UNIFIED : false;
    if (!qa_same_seat(batch.seat, qa_remote_application_seat_id(app)))
      for (size_t i = 0; i < app->remote_seat_count && !unified; i++)
        if (qa_same_seat(qa_session_seat_id(app->remote_seats[i]->seat), batch.seat)
            && qa_app_remote_seat_source_remote(app->remote_seats[i]->source)->family == QA_APP_REMOTE_FAMILY_UNIFIED)
          unified = true;
    if (unified) {
      qa_app_sim_presentation_event *kept = xcalloc(batch.event_count, sizeof *kept);
      state->audio_events[b] = kept;
      size_t count = 0;
      for (size_t i = 0; i < batch.event_count; i++)
        if (!qa_app_presentation_audio_control(&batch.events[i])) kept[count++] = batch.events[i];
      batch.events = kept;
      batch.event_count = count;
    }
    state->audio_batches[b] = batch;
  }
  qa_app_application_audio_frame(frontend->audio, &output->snapshot, state->listeners, state->listener_count, NULL, 0, state->now,
      state->audio_batches, state->batch_count);
  qa_remote_application_source_session_actions(app, state->batches, state->batch_count);
  qa_cleanup_pop(&scene_guard);
  free(peer_scenes);
  *state->out = *output;
  state->result = true;
}
static void step_before_world_change(void *ctx) {
  qa_remote_application *app = ctx;
  if (app->capture != NULL) qa_app_capture_before_world_change(app->capture);
}
bool qa_remote_application_step(qa_remote_application *app, double elapsed_milliseconds, qa_simulation_output *out) {
  if (app->closed || app->closing) qa_raise("Error", 0, 0, "Remote application is closed");
  if (app->stepping) qa_raise("Error", 0, 0, "Remote application step is already in progress");
  if (!(elapsed_milliseconds > 0) || elapsed_milliseconds != elapsed_milliseconds || elapsed_milliseconds == 1.0 / 0.0)
    qa_raise("RangeError", 0, 0, "Remote application step requires positive elapsed milliseconds");
  if (!app->ownership.borrowed && app->owned_video_restart != NULL) (void)qa_app_video_restart_drain(app->owned_video_restart);
  app->stepping = true;
  app->cinematic_presented = false;
  step_state state;
  memset(&state, 0, sizeof state);
  state.app = app;
  state.elapsed_milliseconds = elapsed_milliseconds;
  state.out = out;
  qa_caught_error caught;
  bool ok = qa_try(step_body, &state, &caught);
  step_release(&state);
  app->stepping = false;
  if (!ok) {
    /* catch: await this.capture?.beforeWorldChange(); throw error (finally: stepping = false) */
    qa_caught_error cleanup;
    if (!qa_try(step_before_world_change, app, &cleanup)) qa_rethrow(&cleanup);
    qa_rethrow(&caught);
  }
  return state.result;
}

/* ---- run / readPixels / captureNextFrame (TS 4216-4229) ---------------- */
static bool loop_alive(void *ctx) {
  qa_remote_application *app = ctx;
  return !app->stopping && !app->closed && (!app->launch_options.has_frame_limit || app->frames < app->launch_options.frame_limit);
}
static double loop_minimum(void *ctx) { (void)ctx; return 4; }
static void loop_step(void *ctx, double elapsed) {
  qa_simulation_output output;
  (void)qa_remote_application_step(ctx, elapsed, &output);
}
static void loop_clock_state(void *ctx, char *out, size_t capacity) {
  /* TS String(this.videoRestart?.generation) */
  qa_app_video_restart *restart = qa_remote_application_video_restart(ctx);
  if (restart == NULL) snprintf(out, capacity, "undefined");
  else qa_net_js_number(qa_app_video_restart_generation(restart), out, capacity);
}
void qa_remote_application_run(qa_remote_application *app) {
  qa_app_frame_loop_host host;
  memset(&host, 0, sizeof host);
  host.ctx = app;
  host.alive = loop_alive;
  host.minimum_milliseconds = loop_minimum;
  host.step = loop_step;
  host.clock_state = loop_clock_state;
  qa_app_run_frame_loop(host);
}
uint8_t *qa_remote_application_read_pixels(qa_remote_application *app, size_t *out_length) {
  return qa_app_native_renderer_read_pixels(app->renderer, out_length);
}
void qa_remote_application_capture_next_frame(qa_remote_application *app, qa_app_frame_capture_request request) {
  qa_app_native_renderer_capture_next_frame(app->renderer, request);
}
