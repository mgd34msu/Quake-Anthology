/* Application members that build and republish seat presentations (application.ts createWorldSeatPresentation,
 * prepareVideoRestart). GPL-2.0-or-later. */
#include "application_internal.h"
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/q1_client_settings.h"
#include "qa/app/bootstrap/q3_seat_client.h"
#include "qa/app/bootstrap/video_restart.h"
#include "qa/app/bootstrap/view_settings.h"

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

/* ---- createWorldSeatPresentation(local, assets, renderer, simulation, seats, font, characters, ui, effects, sourceClient,
 * rerelease, nativeSeat, content = this.content) ---- */
typedef struct seat_presentation_record {
  qa_application *app;
  qa_app_shared_simulation *simulation;
  qa_app_loaded_content *content;
  qa_app_sim_presentation_access access;
  qa_app_sim_debug_shape_access debug_shapes;
  qa_app_seat_q3_client q3_client;
  qa_application_graph_overlay *overlay;
} seat_presentation_record;
static void seat_presentation_record_release(void *record) { free(((seat_presentation_record *)record)->overlay); }
static qa_cvar_registry *image_cvars(qa_application *app) {
  return app->image_settings == NULL ? NULL : qa_app_image_settings_cvars(app->image_settings);
}
/* worldTextCullFactor: () => this.imageSettings?.cvars.variableValue("gl_debug_distfrac") ?? 0.004 */
static double world_text_cull_factor(void *ctx) {
  qa_cvar_registry *cvars = image_cvars(((seat_presentation_record *)ctx)->app);
  return cvars == NULL ? 0.004 : qa_cvar_registry_variable_value(cvars, "gl_debug_distfrac");
}
static double field_of_view(void *ctx) { return qa_app_view_settings_field_of_view(((seat_presentation_record *)ctx)->app->view_settings); }
static const qa_debug_line *debug_lines(void *ctx, size_t *out_count) {
  return qa_app_shared_sim_debug_lines(((seat_presentation_record *)ctx)->simulation, out_count);
}
/* lineWidth: () => this.imageSettings?.debugLineWidth ?? 2 */
static double debug_line_width(void *ctx) {
  qa_application *app = ((seat_presentation_record *)ctx)->app;
  return app->image_settings == NULL ? 2 : qa_app_image_settings_debug_line_width(app->image_settings);
}
static double console_scale(void *ctx) {
  qa_cvar_registry *cvars = image_cvars(((seat_presentation_record *)ctx)->app);
  return cvars == NULL ? 0 : qa_cvar_registry_variable_value(cvars, "con_scale");
}
/* viewSize: () => readQ1ViewSettings(this.imageSettings?.cvars ?? null, this.sourceDialect(content)) */
static bool view_size(void *ctx, qa_app_q1_view_settings *out) {
  seat_presentation_record *record = ctx;
  return qa_app_read_q1_view_settings(image_cvars(record->app), qa_application_source_dialect(record->app, record->content), true, out);
}
static bool planar_shadows(void *ctx) {
  qa_cvar_registry *cvars = image_cvars(((seat_presentation_record *)ctx)->app);
  return (cvars == NULL ? 0 : qa_cvar_registry_variable_value(cvars, "r_shadows")) != 0;
}
/* cameraOverride: camera => this.tools?.applyCamera(camera) ?? camera */
static qa_scene_camera camera_override(void *ctx, qa_scene_camera camera) {
  qa_application *app = ((seat_presentation_record *)ctx)->app;
  return app->tools == NULL ? camera : qa_app_application_tools_apply_camera(app->tools, camera);
}
qa_app_world_seat_presentation *qa_application_create_world_seat_presentation(qa_application *app, qa_app_local_input *local,
    qa_app_application_assets *assets, qa_app_native_renderer *renderer, qa_app_shared_simulation *simulation, double seats,
    qa_text_font_selection font, const qa_q3_character_assets *characters, qa_app_seat_ui *ui, qa_app_application_effects *effects,
    qa_application_q3_seat_client *source_client, qa_app_rerelease_presentation *rerelease,
    qa_application_native_q2_seat_client *native_seat, qa_app_loaded_content *content) {
  seat_presentation_record *record = checked(calloc(1, sizeof *record));
  keep_record(app, record, seat_presentation_record_release);
  record->app = app;
  record->simulation = simulation;
  record->content = content != NULL ? content : app->loaded_content;
  record->access = qa_app_shared_sim_presentation_access(simulation);
  record->debug_shapes = (qa_app_sim_debug_shape_access){record, debug_lines, debug_line_width};
  qa_app_world_seat_presentation_options options;
  memset(&options, 0, sizeof options);
  options.local = local;
  options.assets = assets;
  options.renderer = renderer;
  options.simulation = &record->access;
  options.seat_count = seats;
  options.font = font;
  options.characters = characters;
  options.ui = ui;
  options.effects = native_seat != NULL ? native_seat->effects : effects; /* nativeSeat?.effects ?? effects */
  if (source_client != NULL) { /* sourceClient?.client ?? null */
    record->q3_client = qa_app_application_q3_client_seat(source_client->client);
    options.q3_client = &record->q3_client;
  }
  options.rerelease = rerelease;
  options.world_text_cull_factor = world_text_cull_factor; options.world_text_cull_factor_ctx = record;
  options.field_of_view = field_of_view; options.field_of_view_ctx = record;
  options.debug_shapes = &record->debug_shapes;
  options.console_scale = console_scale; options.console_scale_ctx = record;
  options.view_size = view_size; options.view_size_ctx = record;
  options.planar_shadows = planar_shadows; options.planar_shadows_ctx = record;
  options.camera_override = camera_override; options.camera_override_ctx = record;
  record->overlay = qa_application_create_debug_graph_overlay(app, assets); /* graphOverlay: await createDebugGraphOverlay(assets) */
  options.graph_overlay = record->overlay->draw; options.graph_overlay_ctx = record->overlay->ctx;
  if (native_seat != NULL) { /* nativeQ2: nativeSeat?.descriptor */
    if (native_seat->descriptor_frame == NULL) qa_raise("Error", 0, 0, "Native Q2 seat has no HUD frame descriptor");
    options.has_native_q2 = true;
    options.native_q2_frame = native_seat->descriptor_frame; options.native_q2_ctx = native_seat->descriptor_ctx;
    options.native_q2_owns_effects = true;
  }
  qa_app_world_seat_presentation *presentation = qa_app_world_seat_presentation_create(&options);
  if (presentation == NULL) qa_range_error("Out of memory");
  return presentation;
}

/* ---- prepareVideoRestart() ---- */
typedef struct video_seat_record {
  qa_application *app;
  qa_application_graphical *graphical;
  qa_seat_id seat;
  qa_application_local_q3_guest_seat *guest;
  qa_app_world_seat_presentation *presentation;
} video_seat_record;
typedef struct video_restart_record {
  qa_application *app;
  qa_application_graphical *graphical;
  qa_app_application_q3_client **clients; size_t client_count; /* seats[].client */
} video_restart_record;
static void video_restart_record_release(void *record) { free(((video_restart_record *)record)->clients); }
static qa_application_q3_seat_client *graphical_q3(qa_application_graphical *graphical, qa_seat_id seat) {
  for (size_t i = 0; i < graphical->q3_count; i++)
    if (qa_same_seat(graphical->q3_seats[i], seat)) return &graphical->q3[i];
  return NULL;
}
/* viewport: window => this.localViewport(id, size.width, size.height) over window.drawableSize */
static qa_rect video_seat_viewport(void *ctx, qa_sdl_window *window) {
  video_seat_record *record = ctx;
  qa_sdl_size size = qa_sdl_window_drawable_size(window);
  return qa_application_local_viewport(record->app, record->seat, size.width, size.height);
}
static void video_seat_publish(void *ctx, qa_app_application_q3_client *client) {
  video_seat_record *record = ctx;
  qa_application_q3_seat_client *current = graphical_q3(record->graphical, record->seat);
  qa_app_application_q3_client *previous = current->client;
  current->client = client; /* graphical.q3.set(id, { ...current, client }) */
  record->guest->client = client;
  qa_app_seat_q3_client view = qa_app_application_q3_client_seat(client), replaced;
  (void)qa_app_world_seat_presentation_replace_q3_client(record->presentation, &view, &replaced);
  /* this.clientInputs = this.clientInputs.filter(event => !event.seat.equals(id)) */
  size_t kept = 0;
  for (size_t i = 0; i < record->app->client_input_count; i++)
    if (!qa_same_seat(record->app->client_inputs[i].seat, record->seat)) record->app->client_inputs[kept++] = record->app->client_inputs[i];
  record->app->client_input_count = kept;
  qa_app_application_q3_client_destroy(previous); /* prepareVideoGuests closed it; the publisher frees it */
}
static void video_assert_current(void *ctx) {
  video_restart_record *record = ctx;
  qa_application *app = record->app;
  bool changed = app->closed || app->graphical != record->graphical;
  for (size_t i = 0; !changed && i < record->client_count; i++) {
    bool present = false;
    for (size_t j = 0; j < record->graphical->q3_count; j++)
      if (record->graphical->q3[j].client == record->clients[i]) present = true;
    if (!present) changed = true;
  }
  if (changed) qa_raise("Error", 0, 0, "Guest video source changed during preparation");
}
bool qa_application_prepare_video_restart(qa_application *app, qa_app_prepared_video_presentation *out) {
  qa_application_graphical *graphical = app->graphical;
  if (graphical == NULL || app->closed) qa_raise("Error", 0, 0, "Video source has retired");
  video_restart_record *restart = checked(calloc(1, sizeof *restart));
  keep_record(app, restart, video_restart_record_release);
  restart->app = app;
  restart->graphical = graphical;
  qa_app_video_guest_seat *seats = checked(calloc(graphical->q3_count + 1, sizeof *seats));
  qa_cleanup seats_cleanup;
  qa_cleanup_push(&seats_cleanup, free, seats);
  restart->clients = checked(calloc(graphical->q3_count + 1, sizeof *restart->clients));
  size_t count = 0;
  for (size_t i = 0; i < graphical->q3_count; i++) {
    qa_application_q3_seat_client *current = &graphical->q3[i];
    if (!current->qvm) continue;
    qa_seat_id id = graphical->q3_seats[i];
    qa_app_world_seat_presentation *presentation = NULL;
    for (size_t j = 0; j < graphical->presentation_count && presentation == NULL; j++)
      if (qa_same_seat(qa_session_seat_id(graphical->presentations[j].local->player.seat), id))
        presentation = graphical->presentations[j].presentation;
    qa_application_local_q3_guest_seat *guest = NULL;
    if (app->local_guest != NULL)
      for (size_t j = 0; j < app->local_guest->seat_count && guest == NULL; j++)
        if (qa_same_seat(app->local_guest->seat_ids[j], id)) guest = &app->local_guest->seats[j];
    if (presentation == NULL || guest == NULL || guest->client != current->client)
      qa_raise("Error", 0, 0, "Guest video seat has no current presentation");
    video_seat_record *record = checked(calloc(1, sizeof *record));
    keep_record(app, record, NULL);
    *record = (video_seat_record){app, graphical, id, guest, presentation};
    seats[count] = (qa_app_video_guest_seat){current->client, record, video_seat_viewport, video_seat_publish};
    restart->clients[count++] = current->client;
  }
  restart->client_count = count;
  bool prepared = qa_app_prepare_video_guests(graphical->input, graphical->renderer, seats, count, video_assert_current, restart, out);
  qa_cleanup_pop(&seats_cleanup);
  free(seats);
  return prepared;
}
