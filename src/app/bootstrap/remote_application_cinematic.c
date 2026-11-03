/* Port of src/app/bootstrap/remote-application.ts, the cinematic region (TS
 * 2692-2906 in the 2026-11-03 tree): componentCinematics, systemCinematicHost,
 * startCinematic, activateCinematic, prepareCinematic, stopCinematic,
 * completeCinematic and closeCinematic. A cinematic's closures (current, the
 * captions, the system handle) live as long as the application (TS GC: arena).
 * GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "remote_application_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/audio/output_settings.h"
#include "qa/app/bootstrap/mod_presentations.h"
#include "qa/app/bootstrap/q3_client/cinematics.h"
#include "qa/content/mounts.h"
#include "qa/foundation/support.h"
#include "qa/text/media_captions.h"
#include "qa/ui/settings/language.h"

static void *xcalloc(size_t count, size_t size) {
  void *memory = calloc(count == 0 ? 1 : count, size == 0 ? 1 : size);
  if (memory == NULL) qa_range_error("Out of memory");
  return memory;
}

/* ---- the cinematic's current() closures -------------------------------- */
/* startCinematic: !closed && !closing && !stopping && cinematicRequest === ticket && worldLoadGeneration === generation */
typedef struct start_current { qa_remote_application *app; double ticket, generation; } start_current;
static bool start_is_current(void *ctx) {
  start_current *current = ctx;
  qa_remote_application *app = current->app;
  return !app->closed && !app->closing && !app->stopping && app->cinematic_request == current->ticket
      && app->world_load_generation == current->generation;
}
static bool movie_current(const qa_remote_cinematic *movie) { return movie->current(movie->current_ctx); }

/* ---- closeCinematic (TS 2890-2906) ------------------------------------- */
typedef struct close_call { qa_remote_application *app; qa_remote_cinematic *current; } close_call;
static void close_execute(void *ctx) { qa_remote_renderer_execute(((close_call *)ctx)->app, NULL, 0); }
static void close_movie(void *ctx) {
  close_call *call = ctx;
  qa_app_campaign_cinematic_close(call->current->movie, call->app->frames);
}
static void close_images(void *ctx) {
  close_call *call = ctx;
  if (call->current->images != NULL) {
    qa_scene_image_registry_destroy(call->current->images);
    free(call->current->images);
    call->current->images = NULL;
  }
}
static void close_audio(void *ctx) {
  close_call *call = ctx;
  if (call->current->owned_audio != NULL) {
    qa_unified_audio_close(call->current->owned_audio);
    qa_unified_audio_destroy(call->current->owned_audio);
    call->current->owned_audio = NULL;
  }
}
void qa_remote_application_close_cinematic(qa_remote_application *app, qa_remote_cinematic *current) {
  close_call call = {app, current};
  void (*closes[])(void *) = {close_execute, close_movie, close_images, close_audio, close_execute};
  bool failed = false;
  qa_caught_error caught;
  for (size_t i = 0; i < sizeof closes / sizeof closes[0]; i++)
    if (!qa_try(closes[i], &call, &caught)) failed = true;
  if (failed) qa_raise("AggregateError", 0, 0, "Cinematic retirement failed");
}

/* ---- stopCinematic / completeCinematic (TS 2867-2888) ------------------ */
void qa_remote_application_stop_cinematic(qa_remote_application *app) {
  app->cinematic_request++;
  qa_remote_cinematic *current = app->cinematic;
  if (current == NULL) return;
  app->cinematic = NULL;
  qa_remote_application_close_cinematic(app, current);
}
void qa_remote_application_complete_cinematic(qa_remote_application *app, qa_remote_cinematic *movie) {
  if (app->cinematic != movie || !movie_current(movie)) return;
  app->cinematic = NULL;
  qa_remote_application_close_cinematic(app, movie);
  if (movie_current(movie) && app->cinematic_request == movie->request) {
    app->cinematic_request++;
    movie->ended(movie->ended_ctx);
    for (size_t i = 0; i < app->remote_seat_count; i++) {
      qa_remote_seat_peer *peer = app->remote_seats[i];
      void (*completed)(void *ctx) = peer->cinematic_ended;
      void *completed_ctx = peer->cinematic_ended_ctx;
      peer->cinematic_ended = NULL;
      peer->cinematic_ended_ctx = NULL;
      if (completed != NULL) completed(completed_ctx);
    }
  }
}

/* ---- activateCinematic (TS 2771-2794) ---------------------------------- */
typedef struct activate_call { qa_remote_application *app; qa_remote_cinematic *movie; } activate_call;
static void activate_body(void *ctx) {
  activate_call *call = ctx;
  qa_remote_application *app = call->app;
  qa_remote_cinematic *previous = app->cinematic;
  if (previous != NULL && previous != call->movie) {
    app->cinematic = NULL;
    qa_remote_application_close_cinematic(app, previous);
  }
  if (call->movie->owned_audio != NULL
      && qa_unified_audio_open_device(call->movie->owned_audio, NULL, 0) != QA_SDL_AUDIO_OPEN_OK)
    qa_raise("Error", 0, 0, "Audio output is unavailable");
  qa_app_campaign_cinematic_activate(call->movie->movie);
  app->cinematic = call->movie;
  app->reset_frame_elapsed = true;
}
static void activate_cleanup(void *ctx) {
  activate_call *call = ctx;
  qa_remote_application_close_cinematic(call->app, call->movie);
}
void qa_remote_application_activate_cinematic(qa_remote_application *app, qa_remote_cinematic *movie) {
  if (!movie_current(movie)) {
    qa_remote_application_close_cinematic(app, movie);
    return;
  }
  activate_call call = {app, movie};
  qa_caught_error caught;
  if (!qa_try(activate_body, &call, &caught)) {
    qa_caught_error cleanup;
    if (!qa_try(activate_cleanup, &call, &cleanup))
      qa_raise("AggregateError", 0, 0, "Cinematic activation and cleanup failed");
    qa_rethrow(&caught);
  }
}

/* ---- prepareCinematic (TS 2796-2865) ----------------------------------- */
typedef struct cinematic_captions {
  qa_remote_application *app;
  qa_seat_id seat;
  qa_cvar_registry *cvars;
  qa_content_mounted *mounts;
  qa_seat_media_captions captions;
  qa_ui_seat_preferences preferences;
  char *language;
  qa_app_screen_cinematic_captions view;
} cinematic_captions;
static void release_captions(void *object) {
  cinematic_captions *captions = object;
  qa_seat_media_captions_destroy(&captions->captions);
  free(captions->language);
  free(captions);
}
static uint8_t *captions_read(void *ctx, const char *path, size_t *out_length) {
  cinematic_captions *captions = ctx;
  qa_content_opened_resource opened;
  if (!qa_content_mounted_open(captions->mounts, path, NULL, NULL, &opened)) { *out_length = 0; return NULL; }
  *out_length = opened.byte_length;
  return opened.bytes;
}
static const char *captions_language(void *ctx) {
  cinematic_captions *captions = ctx;
  char *language = qa_ui_read_seat_language(captions->cvars, captions->seat.index);
  if (language == NULL) qa_range_error("Out of memory");
  free(captions->language);
  captions->language = language;
  return language;
}
static void captions_failed(void *ctx, const char *error) {
  cinematic_captions *captions = ctx;
  size_t length = strlen(error) + 40;
  char *text = xcalloc(length, 1);
  snprintf(text, length, "Caption language reload failed: %s\n", error);
  qa_cleanup guard;
  qa_cleanup_push(&guard, free, text);
  qa_remote_application_print(captions->app, text, NULL);
  qa_cleanup_pop(&guard);
  free(text);
}
static void captions_prepare(void *ctx, const char *source) {
  cinematic_captions *captions = ctx;
  if (!qa_seat_media_captions_prepare(&captions->captions, source, captions_language(captions))) qa_range_error("Out of memory");
}
static qa_render_command *captions_commands(void *ctx, const qa_app_cinematic_caption_timeline *timeline, qa_rect viewport,
    size_t *out_count) {
  cinematic_captions *captions = ctx;
  qa_remote_application *app = captions->app;
  *out_count = 0;
  qa_app_world_seat_presentation *presentation = qa_remote_application_presentation(app);
  if (presentation == NULL
      || !qa_same_seat(qa_session_seat_id(qa_app_world_seat_presentation_local(presentation)->player.seat), captions->seat)) {
    presentation = NULL;
    for (size_t i = 0; i < app->remote_seat_count && presentation == NULL; i++) {
      qa_remote_seat_peer *peer = app->remote_seats[i];
      if (qa_same_seat(qa_session_seat_id(peer->seat), captions->seat) && peer->view != NULL)
        presentation = qa_app_remote_seat_view_presentation(peer->view);
    }
  }
  qa_caption_preferences selected;
  memset(&selected, 0, sizeof selected);
  selected.speakers = true;
  qa_active_caption *active = NULL;
  size_t active_count = 0;
  if (presentation == NULL) {
    if (!app->ownership.borrowed) return NULL;
    bool enabled = qa_ui_seat_preferences_values(&captions->preferences).captions;
    selected.subtitles = enabled;
    selected.sound_captions = enabled;
    if (!qa_seat_media_captions_active(&captions->captions, timeline->state, selected, &active, &active_count))
      qa_range_error("Out of memory");
    qa_cleanup guard;
    qa_cleanup_push(&guard, free, active);
    qa_app_client_bootstrap *client = app->ownership.client;
    const qa_render_command *rows = client->caption_commands(client->bootstrap_ctx, active, active_count, viewport,
        timeline->elapsed_milliseconds, out_count);
    qa_render_command *copy = xcalloc(*out_count, sizeof *copy);
    if (*out_count != 0) memcpy(copy, rows, *out_count * sizeof *copy);
    qa_cleanup_pop(&guard);
    free(active);
    return copy;
  }
  qa_app_seat_ui *ui = qa_app_world_seat_presentation_ui(presentation);
  bool enabled = qa_ui_seat_preferences_values(qa_app_seat_ui_preferences(ui)).captions;
  selected.subtitles = enabled;
  selected.sound_captions = enabled;
  if (!qa_seat_media_captions_active(&captions->captions, timeline->state, selected, &active, &active_count))
    qa_range_error("Out of memory");
  qa_cleanup guard;
  qa_cleanup_push(&guard, free, active);
  qa_seat_client_state state = qa_app_world_seat_presentation_state(presentation);
  qa_ui_draw_context binding;
  memset(&binding, 0, sizeof binding);
  binding.binding = state.presentation;
  binding.binding.viewport = viewport;
  binding.binding.safe_area = viewport;
  binding.time_milliseconds = timeline->elapsed_milliseconds;
  qa_render_command *commands = qa_app_seat_ui_caption_commands(ui, active, active_count, binding, out_count);
  qa_cleanup_pop(&guard);
  free(active);
  return commands;
}
static double audio_milliseconds(void *ctx) {
  (void)ctx; /* TS Math.trunc(performance.now()) */
  return (double)(int64_t)qa_app_remote_access_now_milliseconds(NULL);
}
static double audio_random(void *ctx) { (void)ctx; /* TS () => 0 */ return 0; }
static void release_cinematic(void *object) {
  qa_remote_cinematic *movie = object;
  if (movie->movie != NULL) qa_app_campaign_cinematic_destroy(movie->movie);
  if (movie->images != NULL) { qa_scene_image_registry_destroy(movie->images); free(movie->images); }
  if (movie->owned_audio != NULL) qa_unified_audio_destroy(movie->owned_audio);
  free(movie);
}
typedef struct prepare_source { qa_content_mounted *mounts; qa_seat_id seat; qa_cvar_registry *cvars; } prepare_source;
typedef struct prepare_state {
  qa_remote_application *app;
  qa_app_screen_cinematic_request request;
  qa_remote_cinematic *movie;
  qa_unified_audio *candidate_audio;
  const prepare_source *source;
} prepare_state;
static void prepare_body(void *ctx) {
  prepare_state *state = ctx;
  qa_remote_application *app = state->app;
  qa_remote_cinematic *movie = state->movie;
  qa_unified_audio *retained = state->candidate_audio != NULL ? state->candidate_audio
      : app->ownership.borrowed ? app->ownership.client->output_current
      : app->frontend != NULL ? qa_app_application_audio_engine(app->frontend->audio) : NULL;
  if (retained == NULL) {
    qa_unified_audio_options options;
    memset(&options, 0, sizeof options);
    options.milliseconds = audio_milliseconds;
    options.random = audio_random;
    options.has_output_format = true;
    options.output_format = qa_app_read_audio_output_cvars(qa_app_image_settings_cvars(app->image_settings));
    movie->owned_audio = qa_unified_audio_create(&options);
    if (movie->owned_audio == NULL) qa_range_error("Out of memory");
  }
  qa_unified_audio *engine = retained != NULL ? retained : movie->owned_audio;
  if (engine == NULL) qa_raise("Error", 0, 0, "Cinematic has no audio output owner");
  qa_seat_id seat = state->source != NULL ? state->source->seat : qa_remote_application_seat_id(app);
  qa_cvar_registry *cvars = state->source != NULL ? state->source->cvars : qa_app_image_settings_cvars(app->image_settings);
  cinematic_captions *captions = xcalloc(1, sizeof *captions);
  captions->app = app;
  captions->seat = seat;
  captions->cvars = cvars;
  captions->mounts = state->source != NULL ? state->source->mounts : qa_remote_application_mounts(app);
  qa_ui_seat_preferences_init(&captions->preferences, seat, cvars);
  qa_live_language live = {captions, captions_language, captions_failed};
  qa_seat_media_captions_init(&captions->captions, seat, captions_read, captions, NULL, false, &live);
  qa_remote_arena_add(&app->arena, release_captions, captions);
  captions->view.ctx = captions;
  captions->view.prepare = captions_prepare;
  captions->view.commands = captions_commands;
  movie->movie = qa_app_campaign_cinematic_prepare_engine(state->request, captions->mounts, movie->images, engine, app->renderer,
      seat, movie->current, movie->current_ctx, &captions->view);
  if (movie->movie == NULL) qa_range_error("Out of memory");
}
static void prepare_close_images(void *ctx) {
  prepare_state *state = ctx;
  qa_scene_image_registry_destroy(state->movie->images);
  free(state->movie->images);
  state->movie->images = NULL;
}
static void prepare_close_audio(void *ctx) {
  prepare_state *state = ctx;
  if (state->movie->owned_audio != NULL) {
    qa_unified_audio_close(state->movie->owned_audio);
    qa_unified_audio_destroy(state->movie->owned_audio);
    state->movie->owned_audio = NULL;
  }
}
static void prepare_execute(void *ctx) { qa_remote_renderer_execute(((prepare_state *)ctx)->app, NULL, 0); }
static qa_remote_cinematic *prepare_cinematic(qa_remote_application *app, qa_app_screen_cinematic_request request,
    void (*ended)(void *ctx), void *ended_ctx, double ticket, bool (*current)(void *ctx), void *current_ctx,
    qa_unified_audio *candidate_audio, const prepare_source *source) {
  qa_remote_cinematic *movie = xcalloc(1, sizeof *movie);
  movie->request = ticket;
  movie->current = current;
  movie->current_ctx = current_ctx;
  movie->ended = ended;
  movie->ended_ctx = ended_ctx;
  movie->images = xcalloc(1, sizeof *movie->images);
  qa_scene_image_registry_fork(qa_app_native_renderer_images(app->renderer), NULL, movie->images);
  prepare_state state = {app, request, movie, candidate_audio, source};
  qa_caught_error caught;
  if (!qa_try(prepare_body, &state, &caught)) {
    bool aggregate = false;
    qa_caught_error cleanup;
    void (*closes[])(void *) = {prepare_close_images, prepare_close_audio, prepare_execute};
    for (size_t i = 0; i < sizeof closes / sizeof closes[0]; i++)
      if (!qa_try(closes[i], &state, &cleanup)) aggregate = true;
    free(movie->images);
    free(movie);
    if (aggregate) qa_raise("AggregateError", 0, 0, "Cinematic preparation and cleanup failed");
    qa_rethrow(&caught);
  }
  qa_remote_arena_add(&app->arena, release_cinematic, movie);
  return movie;
}

/* ---- startCinematic (TS 2757-2769) ------------------------------------- */
void qa_remote_application_start_cinematic(qa_remote_application *app, const char *name, bool loop, bool hold, bool silent,
    void (*ended)(void *ctx), void *ended_ctx) {
  qa_remote_application_stop_cinematic(app);
  start_current *current = xcalloc(1, sizeof *current);
  qa_remote_arena_add(&app->arena, free, current);
  current->app = app;
  current->ticket = app->cinematic_request;
  current->generation = app->world_load_generation;
  qa_app_screen_cinematic_request request = {name, loop, hold, silent};
  qa_remote_cinematic *movie = prepare_cinematic(app, request, ended, ended_ctx, current->ticket, start_is_current, current,
      NULL, NULL);
  if (!start_is_current(current)) {
    qa_remote_application_close_cinematic(app, movie);
    qa_raise("Error", 0, 0, "Cinematic belongs to a retired remote request");
  }
  qa_remote_application_activate_cinematic(app, movie);
}

/* ---- systemCinematicHost (TS 2710-2755) -------------------------------- */
struct qa_remote_cinematic_owner {
  qa_remote_application *app;
  qa_unified_audio *audio;
  qa_content_mounted *mounts;
  qa_seat_id seat;
  qa_cvar_registry *cvars;
  void *ctx;
  bool (*current)(void *ctx);
  void (*complete)(void *ctx);
  void (*publish)(void *ctx, qa_remote_cinematic *movie);
  void (*unpublish)(void *ctx, qa_remote_cinematic *movie); /* NULL = TS undefined */
  qa_app_system_cinematic_host host;
};
typedef struct system_open {
  qa_remote_cinematic_owner *owner;
  double ticket;
  bool (*consumer_current)(void *ctx); void *consumer_ctx;
  qa_remote_cinematic *movie;
} system_open;
static bool system_current(void *ctx) {
  system_open *open = ctx;
  return open->owner->current(open->owner->ctx) && open->consumer_current(open->consumer_ctx)
      && open->owner->app->cinematic_request == open->ticket;
}
static void system_ended(void *ctx) {
  system_open *open = ctx;
  open->owner->complete(open->owner->ctx);
}
static void handle_refresh(void *ctx, qa_app_system_cinematic_handle *handle) {
  system_open *open = ctx;
  handle->status = system_current(open) ? qa_app_campaign_cinematic_status(open->movie->movie) : "stopped";
}
static void handle_skip(void *ctx) {
  system_open *open = ctx;
  qa_remote_application *app = open->owner->app;
  if (!system_current(open)) return;
  qa_app_campaign_cinematic_skip(open->movie->movie);
  if (app->cinematic == open->movie) qa_remote_application_complete_cinematic(app, open->movie);
  else {
    if (open->owner->unpublish != NULL) open->owner->unpublish(open->owner->ctx, open->movie);
    if (app->cinematic_request == open->ticket) app->cinematic_request++;
    qa_remote_application_close_cinematic(app, open->movie);
  }
}
static void handle_stop(void *ctx) {
  system_open *open = ctx;
  qa_remote_application *app = open->owner->app;
  if (app->cinematic == open->movie) qa_remote_application_stop_cinematic(app);
  else {
    if (open->owner->unpublish != NULL) open->owner->unpublish(open->owner->ctx, open->movie);
    qa_remote_application_close_cinematic(app, open->movie);
  }
}
static qa_app_system_cinematic_handle system_host_open(void *ctx, qa_app_q3_screen_cinematic_request request,
    bool (*consumer_current)(void *cctx), void *consumer_ctx) {
  qa_remote_cinematic_owner *owner = ctx;
  qa_remote_application *app = owner->app;
  qa_remote_application_stop_cinematic(app);
  system_open *open = xcalloc(1, sizeof *open);
  qa_remote_arena_add(&app->arena, free, open);
  open->owner = owner;
  open->ticket = app->cinematic_request;
  open->consumer_current = consumer_current;
  open->consumer_ctx = consumer_ctx;
  prepare_source source = {owner->mounts, owner->seat, owner->cvars};
  qa_app_screen_cinematic_request screen = {request.name, request.loop, request.hold, request.silent};
  open->movie = prepare_cinematic(app, screen, system_ended, open, open->ticket, system_current, open, owner->audio, &source);
  if (!system_current(open)) {
    qa_remote_application_close_cinematic(app, open->movie);
    qa_raise("Error", 0, 0, "System cinematic belongs to a retired remote request");
  }
  owner->publish(owner->ctx, open->movie);
  qa_app_system_cinematic_handle handle;
  memset(&handle, 0, sizeof handle);
  handle.ctx = open;
  handle.status = "playing";
  handle.refresh_status = handle_refresh;
  handle.skip = handle_skip;
  handle.stop = handle_stop;
  handle_refresh(open, &handle);
  return handle;
}
const qa_app_system_cinematic_host *qa_remote_application_system_cinematic_host(qa_remote_application *app,
    const qa_remote_cinematic_owner *owner) {
  qa_remote_cinematic_owner *kept = xcalloc(1, sizeof *kept);
  qa_remote_arena_add(&app->arena, free, kept);
  *kept = *owner;
  kept->app = app;
  kept->host.ctx = kept;
  kept->host.open = system_host_open;
  kept->host.restore = NULL;
  return &kept->host;
}
qa_remote_cinematic_owner *qa_remote_cinematic_owner_create(qa_remote_application *app, qa_unified_audio *audio,
    qa_content_mounted *mounts, qa_seat_id seat, qa_cvar_registry *cvars, void *ctx, bool (*current)(void *ctx),
    void (*complete)(void *ctx), void (*publish)(void *ctx, qa_remote_cinematic *movie),
    void (*unpublish)(void *ctx, qa_remote_cinematic *movie)) {
  qa_remote_cinematic_owner *owner = xcalloc(1, sizeof *owner);
  qa_remote_arena_add(&app->arena, free, owner);
  owner->app = app;
  owner->audio = audio;
  owner->mounts = mounts;
  owner->seat = seat;
  owner->cvars = cvars;
  owner->ctx = ctx;
  owner->current = current;
  owner->complete = complete;
  owner->publish = publish;
  owner->unpublish = unpublish;
  return owner;
}

/* ---- componentCinematics (TS 2692-2708) -------------------------------- */
typedef struct component_cinematics { qa_remote_application *app; qa_app_application_audio *audio; double generation; } component_cinematics;
typedef struct component_owner { component_cinematics *factory; qa_app_component_cinematic_scope scope; } component_owner;
static bool component_current(void *ctx) {
  component_owner *owner = ctx;
  qa_remote_application *app = owner->factory->app;
  return !app->closed && !app->closing && !app->stopping && app->world_load_generation == owner->factory->generation;
}
static void component_complete(void *ctx) {
  component_owner *owner = ctx;
  char *next = qa_cvar_registry_variable_string(owner->scope.cvars, "nextmap");
  if (next == NULL) qa_range_error("Out of memory");
  qa_cleanup guard;
  qa_cleanup_push(&guard, free, next);
  qa_remote_cvar_set(owner->scope.cvars, "nextmap", "", true);
  if (next[0] != '\0') {
    size_t length = strlen(next) + 2;
    char *text = xcalloc(length, 1);
    snprintf(text, length, "%s\n", next);
    qa_cleanup text_guard;
    qa_cleanup_push(&text_guard, free, text);
    owner->scope.append(owner->scope.ctx, text);
    qa_cleanup_pop(&text_guard);
    free(text);
  }
  qa_cleanup_pop(&guard);
  free(next);
}
static void component_publish(void *ctx, qa_remote_cinematic *movie) {
  component_owner *owner = ctx;
  qa_remote_application_activate_cinematic(owner->factory->app, movie);
}
static const qa_app_system_cinematic_host *component_host(void *ctx, const qa_active_mod_presentation *source,
    const qa_app_viewing_seat *presentation, const qa_app_component_cinematic_scope *scope) {
  (void)source; /* TS (_source, presentation, scope) */
  component_cinematics *factory = ctx;
  qa_remote_application *app = factory->app;
  component_owner *owner = xcalloc(1, sizeof *owner);
  qa_remote_arena_add(&app->arena, free, owner);
  owner->factory = factory;
  owner->scope = *scope;
  qa_remote_cinematic_owner *cinematic = qa_remote_cinematic_owner_create(app, qa_app_application_audio_engine(factory->audio),
      scope->mounts, presentation->seat(presentation->ctx), scope->cvars, owner, component_current, component_complete,
      component_publish, NULL);
  return qa_remote_application_system_cinematic_host(app, cinematic);
}
qa_remote_component_cinematics qa_remote_application_component_cinematics(qa_remote_application *app,
    qa_app_application_audio *audio) {
  component_cinematics *factory = xcalloc(1, sizeof *factory);
  qa_remote_arena_add(&app->arena, free, factory);
  factory->app = app;
  factory->audio = audio;
  factory->generation = app->world_load_generation;
  qa_remote_component_cinematics out = {factory, component_host};
  return out;
}
