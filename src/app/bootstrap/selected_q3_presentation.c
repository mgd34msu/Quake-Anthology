/* Port of src/app/bootstrap/selected-q3-presentation.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/selected_q3_presentation.h"
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/q3_client/source.h"
#include "qa/app/bootstrap/q3_client/visibility.h"
#include "qa/content/q3/presentation/client.h"
#include "qa/content/q3/presentation/world_mark_projector.h"
#include "qa/foundation/event_ring.h"
#include "qa/foundation/support.h"
#include "qa/materials/q3_lighting.h"
#include "qa/render/scene/models/light_sampler.h"
#include "qa/world/collision/q3/settings.h"

typedef struct generation_row { double number; qa_actor_id actor; } generation_row;

struct qa_app_selected_q3_presentation {
  qa_app_selected_q3_presentation_options options;
  qa_app_selected_q3_source source;
  qa_app_viewing_seat presentation;
  bool closed, busy;
  qa_actor_id actor;
  double generation;
  qa_app_application_q3_source *transport;
  generation_row *generations; size_t generation_count, generation_capacity;
  qa_event_ring audio; qa_event_cursor audio_cursor;
  qa_model_light_sampler light;
  qa_app_application_q3_assets *media;
  qa_app_application_q3_services *services;
  qa_rect services_viewport; /* TS viewport: presentation.viewport(), read once */
  qa_q3_pres_client_presentation *game;
  qa_app_application_q3_scene_renderer *renderer;
  bool has_scene; qa_q3_pres_scene_content scene;
  double frame;
  int64_t binding;
  /* C storage the TS closures and GC keep alive. */
  qa_command_origin caller, origin;
  qa_cvar_registry *cvars;
  qa_collision_map_settings settings;
  bool has_marks; qa_q3_pres_world_mark_projector marks;
  qa_app_q3_snapshot_selection *selection;
  qa_app_q3_snapshot_source_entity *selection_rows;
};

static void *xcalloc(size_t count, size_t size) {
  void *p = calloc(count == 0 ? 1 : count, size == 0 ? 1 : size);
  if (p == NULL) qa_range_error("Out of memory");
  return p;
}

/* ---- shared failure helpers ---- */
typedef struct cleanup_failures { size_t count; qa_caught_error first; } cleanup_failures;
static void attempt(cleanup_failures *failures, void (*fn)(void *ctx), void *ctx) {
  qa_caught_error caught;
  if (qa_try(fn, ctx, &caught)) return;
  if (failures->count++ == 0) failures->first = caught;
}
static void raise_aggregate(const cleanup_failures *failures, const char *message) {
  qa_raise("AggregateError", 0, (long long)failures->count, "%s (%zu errors; first %s: %s)", message, failures->count,
           failures->first.name, failures->first.message);
}
/* TS catch (error) { try { close() } catch (cleanup) { throw new AggregateError([error, cleanup], message) } throw error }. */
static void close_or_aggregate(void (*close)(void *ctx), void *ctx, const qa_caught_error *error, const char *message) {
  qa_caught_error cleanup;
  if (!qa_try(close, ctx, &cleanup))
    qa_raise("AggregateError", 0, 2, "%s (first %s: %s; cleanup %s: %s)", message, error->name, error->message,
             cleanup.name, cleanup.message);
  qa_rethrow(error);
}

/* ---- ApplicationSelectedQ3Presentation ---- */
static void print_text(void *ctx, const char *text) {
  qa_app_selected_q3_presentation *p = ctx;
  p->options.base.print(p->options.base.ctx, text);
}
bool qa_app_selected_q3_presentation_owns(const qa_app_selected_q3_presentation *p,
    const qa_app_selected_q3_source *source, const qa_app_viewing_seat *seat) {
  return !p->closed && source->source == p->source.source && strcmp(source->content, p->source.content) == 0
      && qa_app_q3_selected_source_generation(source->source) == p->generation && seat->ctx == p->presentation.ctx
      && qa_same_actor(p->actor, seat->actor(seat->ctx)) && qa_app_q3_selected_source_active(source->source)
      && qa_app_q3_selected_source_live(source->source, p->actor);
}
static void assert_current(qa_app_selected_q3_presentation *p) {
  if (!qa_app_selected_q3_presentation_owns(p, &p->source, &p->presentation))
    qa_raise("Error", 0, 0, "Selected Q3 presentation belongs to a retired source or viewer");
}

/* TS bindComponentEffects frame. */
static void effect_frame(void *ctx, qa_scene_camera camera, qa_source_scene_order *order, const qa_scene_fog *q1_fog,
                         qa_app_application_effect_frame *out) {
  qa_app_selected_q3_presentation *p = ctx;
  memset(out, 0, sizeof *out);
  if (p->closed || !qa_app_selected_q3_presentation_owns(p, &p->source, &p->presentation) || !p->has_scene
      || p->renderer == NULL) return;
  qa_app_q3_scene_effect_frame(p->renderer, &p->scene, camera, order, q1_fog, p->presentation.seat(p->presentation.ctx),
      qa_app_application_q3_source_time(p->transport), p->presentation.split_screen(p->presentation.ctx), out);
}

/* TS transport select: selectApplicationQ3Snapshot(player, state, queries,
 * number => source.world.linkState(number)?.absbounds ?? null, leaves, print). */
static bool link_bounds(void *ctx, double number, qa_bounds *out) {
  qa_app_selected_q3_presentation *p = ctx;
  qa_q3_base_link_state state;
  if (!qa_q3_base_world_adapter_link_state(&p->source.source->world, number, &state)) return false;
  *out = state.absbounds;
  return true;
}
static void transport_select(void *ctx, const qa_q3_player_state_record *player,
                             const qa_app_q3_source_presentation_state *state, qa_q3_visible_entities *out) {
  qa_app_selected_q3_presentation *p = ctx;
  qa_app_q3_snapshot_selection_destroy(p->selection); p->selection = NULL;
  free(p->selection_rows);
  p->selection_rows = xcalloc(state->entity_count, sizeof *p->selection_rows);
  for (size_t i = 0; i < state->entity_count; i++) {
    p->selection_rows[i].state = &state->entities[i].state; p->selection_rows[i].linked = state->entities[i].linked;
    p->selection_rows[i].server_flags = state->entities[i].server_flags;
    p->selection_rows[i].single_client = state->entities[i].single_client;
  }
  p->selection = qa_app_select_application_q3_snapshot(player, p->selection_rows, state->entity_count,
      p->options.base.queries, link_bounds, p, qa_app_world_leaf_count(qa_app_application_assets_world(p->options.base.assets)),
      print_text, p);
  if (p->selection == NULL) qa_range_error("Out of memory");
  *out = *qa_app_q3_snapshot_selection_visible(p->selection);
}
static bool transport_actor(void *ctx, double number, qa_actor_id *out) {
  qa_app_selected_q3_presentation *p = ctx;
  return qa_app_q3_selected_source_actor(p->source.source, number, out);
}

/* ---- initialize ---- */
static bool no_font_data(void *ctx) { (void)ctx; return false; }
static void reject_view(void) {
  qa_raise("Error", 0, 0, "Supplemental Q3 scene attempted to take over the primary view or UI");
}
static void reject_scene(void *ctx, qa_q3_pres_presented_scene *scene) { (void)ctx; (void)scene; reject_view(); }
static void reject_command(void *ctx, const qa_render_command *command) { (void)ctx; (void)command; reject_view(); }
static void reject_text(void *ctx, const qa_material_text_draw *draw) { (void)ctx; (void)draw; reject_view(); }
static void reject_listener(void *ctx, qa_vec3 origin, qa_axis axis) { (void)ctx; (void)origin; (void)axis; reject_view(); }
static void output_audio(void *ctx, qa_app_q3_seat_audio_operation operation) {
  qa_app_selected_q3_presentation *p = ctx;
  if (p->closed && operation.kind == QA_APP_Q3_SEAT_AUDIO_CLEAR_LOOPS) return;
  assert_current(p);
  if (qa_event_ring_admit(&p->audio, &operation) < 0) qa_range_error("Out of memory");
}
static bool services_actor_at(void *ctx, double number, qa_actor_id *out) {
  qa_app_selected_q3_presentation *p = ctx;
  assert_current(p);
  return qa_app_q3_selected_source_actor(p->source.source, number, out);
}
static double clock_now(void *ctx) {
  qa_app_selected_q3_presentation *p = ctx;
  return p->options.base.clock.now(p->options.base.clock.ctx);
}
/* TS clock.serverTime: () => transport.time. */
static double clock_server_time(void *ctx) {
  qa_app_selected_q3_presentation *p = ctx;
  return qa_app_application_q3_source_time(p->transport);
}
static double clock_frame(void *ctx) {
  qa_app_selected_q3_presentation *p = ctx;
  return p->options.base.clock.frame_number(p->options.base.clock.ctx);
}
static bool body_hidden(void *ctx, double number) {
  qa_app_selected_q3_presentation *p = ctx;
  return qa_q3_entity_pool_at(&p->source.source->pool, number)->client != NULL;
}
static void center_print(void *ctx, const char *text, double time, double duration) {
  qa_app_selected_q3_presentation *p = ctx;
  p->presentation.center_print(p->presentation.ctx, text, time, duration);
}
static qa_q3_pres_lighting_sample light_for_point(void *ctx, qa_vec3 point) {
  qa_app_selected_q3_presentation *p = ctx;
  qa_lighting_sample light;
  qa_lighting_scales scales = {1, 1};
  if (qa_light_for_point(p->light.has_grid ? &p->light.grid : NULL, point, scales, &light)) return light;
  qa_world_view_input input;
  memset(&input, 0, sizeof input);
  input.camera = p->presentation.camera(p->presentation.ctx);
  input.time.kind = QA_SOURCE_TIME_MILLISECONDS; input.time.value = qa_app_application_q3_source_time(p->transport);
  input.target_is_seat = true; input.target_seat = p->presentation.seat(p->presentation.ctx);
  qa_model_light_sample sample = qa_model_light_sampler_sample(&p->light, point, &input, true);
  qa_lighting_sample out = {{sample.color.x * 255, sample.color.y * 255, sample.color.z * 255}, {0, 0, 0}, {0, 0, 1}};
  return out;
}
static double memory_remaining(void *ctx) {
  (void)ctx;
  double free_memory = qa_app_free_memory();
  return free_memory < 0x7fffffff ? free_memory : 0x7fffffff;
}
/* Q3PresentationSession over the transport (TS session object). */
static qa_q3_pres_snapshot_current snapshot_current(void *ctx) {
  qa_app_selected_q3_presentation *p = ctx;
  qa_q3_pres_snapshot_current current;
  qa_app_application_q3_source_current(p->transport, &current.number, &current.server_time);
  return current;
}
static bool snapshot_read(void *ctx, double number, qa_q3_wire_snapshot *out) {
  qa_app_selected_q3_presentation *p = ctx;
  const qa_q3_wire_snapshot *stored;
  if (!qa_app_application_q3_source_read(p->transport, number, &stored)) return false;
  if (!qa_q3_wire_snapshot_copy(out, stored)) qa_range_error("Out of memory");
  return true;
}
static double server_message_sequence(void *ctx) { (void)ctx; return 0; }
static const char *const *game_state(void *ctx, size_t *out_count) {
  qa_app_selected_q3_presentation *p = ctx;
  return qa_app_application_q3_source_game_state(p->transport, out_count);
}
static bool server_command(void *ctx, double sequence, const char *const **out, size_t *out_count) {
  qa_app_selected_q3_presentation *p = ctx;
  return qa_app_application_q3_source_get_server_command(p->transport, sequence, out, out_count);
}
static bool snapshot_ping(void *ctx, double number, double *out) { (void)ctx; (void)number; *out = 0; return true; }
static void session_assert_current(void *ctx) { assert_current(ctx); }
/* SoundAssetReader over ApplicationQ3Assets (TS assets: media). */
static bool media_read(void *ctx, const char *path, uint8_t **out, size_t *out_length) {
  qa_app_selected_q3_presentation *p = ctx;
  *out = qa_app_application_q3_assets_read(p->media, path, out_length);
  return *out != NULL || *out_length == 0;
}
static bool media_has(void *ctx, const char *path) {
  qa_app_selected_q3_presentation *p = ctx;
  return qa_app_application_q3_assets_has(p->media, path);
}
static bool media_list(void *ctx, const char *prefix, char ***out, size_t *out_count) {
  qa_app_selected_q3_presentation *p = ctx;
  *out = qa_app_application_q3_assets_list(p->media, prefix, out_count);
  return *out != NULL || *out_count == 0;
}
static double media_length(void *ctx, const char *path) {
  qa_app_selected_q3_presentation *p = ctx;
  return qa_app_application_q3_assets_read_file_length(p->media, path);
}
static bool media_read_sync(void *ctx, const char *path, uint8_t **out, size_t *out_length) {
  qa_app_selected_q3_presentation *p = ctx;
  const uint8_t *bytes = qa_app_application_q3_assets_read_sync(p->media, path, out_length);
  *out = malloc(*out_length == 0 ? 1 : *out_length);
  if (*out == NULL) return false;
  if (*out_length > 0) memcpy(*out, bytes, *out_length);
  return true;
}

static void flush_audio(qa_app_selected_q3_presentation *p) {
  assert_current(p);
  if (qa_event_ring_pending(&p->audio, &p->audio_cursor) == 0) return;
  size_t count = 0;
  qa_app_q3_seat_audio_operation *operations = qa_event_ring_take(&p->audio, &p->audio_cursor, &count);
  if (operations == NULL && count != 0) qa_range_error("Out of memory");
  qa_event_ring_retire(&p->audio);
  qa_app_q3_seat_audio_frame frame = {p->source.content, p->presentation.seat(p->presentation.ctx), true,
      p->source.source->host.provider, operations, count};
  qa_cleanup node;
  qa_cleanup_push(&node, free, operations);
  qa_app_application_audio_receive_cgame_frame(p->options.base.audio, &frame);
  qa_cleanup_pop(&node);
  free(operations);
}

static void initialize(void *ctx) {
  qa_app_selected_q3_presentation *p = ctx;
  const qa_app_mod_presentations_options *o = &p->options.base;
  qa_seat_id seat = p->presentation.seat(p->presentation.ctx);
  p->caller.kind = QA_COMMAND_ORIGIN_LOCAL_SEAT; p->caller.seat = seat; p->caller.client = p->presentation.client(p->presentation.ctx);
  p->origin.kind = QA_COMMAND_ORIGIN_SCRIPT; p->origin.name = "selected-q3-cgame"; p->origin.caller = &p->caller;
  qa_cvar_registry_options cvar_options;
  memset(&cvar_options, 0, sizeof cvar_options);
  cvar_options.dialect = QA_COMMAND_DIALECT_Q3;
  cvar_options.context.session = p->actor.session; cvar_options.context.origin = p->origin;
  cvar_options.print = (qa_command_print_fn)print_text; cvar_options.print_ctx = p;
  p->cvars = qa_cvar_registry_create(&cvar_options);
  if (p->cvars == NULL) qa_range_error("Out of memory");
  p->media = qa_app_application_q3_assets_create_from_application(o->assets, p->source.content, print_text, p,
      no_font_data, p, "source-sync", "source");
  if (p->media == NULL) qa_range_error("Out of memory");
  assert_current(p);
  qa_collision_map_settings_init(&p->settings, p->cvars);
  qa_collision_map_settings_register_map(&p->settings);
  qa_app_application_q3_service_options services;
  memset(&services, 0, sizeof services);
  services.collision_settings = &p->settings; services.media = p->media;
  services.world = o->hosts.world; services.audio = o->hosts.audio;
  services.has_owner = true; services.owner = p->source.source->host.provider;
  services.seat = seat;
  p->services_viewport = p->presentation.viewport(p->presentation.ctx);
  services.viewport = &p->services_viewport;
  services.queries = qa_shared_as_scene_queries(o->queries); services.shared_queries = o->queries;
  services.actor_at = services_actor_at; services.actor_ctx = p;
  services.now = clock_now; services.now_ctx = p; services.frame_number = clock_frame; services.frame_ctx = p;
  services.output_ctx = p; services.output_scene = reject_scene; services.output_command = reject_command;
  services.output_text = reject_text; services.output_listener = reject_listener; services.output_audio = output_audio;
  p->services = qa_app_create_application_q3_services(services);
  assert_current(p);
  p->renderer = qa_app_application_q3_scene_renderer_create(p->media, qa_app_application_q3_services_resources(p->services));
  qa_q3_pres_world_mark_projector_init(&p->marks, qa_app_application_assets_world(o->assets));
  p->has_marks = true;
  qa_q3_pres_scene_presentation_options scene;
  memset(&scene, 0, sizeof scene);
  qa_q3_pres_presentation_common *common = &scene.common;
  common->assets.base.ctx = p; common->assets.base.read = media_read; common->assets.base.has = media_has;
  common->assets.base.list = media_list; common->assets.read_file_length = media_length; common->assets.read_sync = media_read_sync;
  common->resources = qa_q3_pres_q3_renderer_resources_as_resources(qa_app_application_q3_services_resources(p->services));
  common->scene = qa_app_application_q3_services_scene(p->services);
  common->mark_geometry = p->marks.projector.geometry;
  common->collision = qa_app_application_q3_services_collision(p->services);
  common->sound = qa_app_application_q3_services_sound(p->services);
  common->draw = *qa_app_application_q3_services_draw(p->services);
  common->font_registry = qa_app_application_q3_assets_font_registry(p->media);
  qa_rect target = p->presentation.viewport(p->presentation.ctx);
  common->target_width = target.width; common->target_height = target.height;
  common->ragepro = p->options.hardware_ragepro(o->ctx);
  common->ctx = p; common->milliseconds = clock_now; common->server_time = clock_server_time; common->frame_number = clock_frame;
  common->body_hidden = body_hidden; common->light_for_point = light_for_point; common->memory_remaining = memory_remaining;
  qa_q3_pres_presentation_session *session = &common->session;
  session->product = p->source.state->product;
  session->client_number = qa_app_application_q3_source_client_number(p->transport);
  session->last_executed_server_command = 0;
  session->mode = QA_Q3_PRES_SESSION_LIVE;
  session->commands = qa_q3_pres_client_command_history_source(qa_app_application_q3_source_commands(p->transport));
  session->snapshots.ctx = p; session->snapshots.current = snapshot_current; session->snapshots.read = snapshot_read;
  session->cvars = p->cvars;
  session->ctx = p; session->server_message_sequence = server_message_sequence; session->get_game_state = game_state;
  session->get_server_command = server_command; session->snapshot_ping = snapshot_ping;
  session->assert_current = session_assert_current; session->print = print_text;
  scene.center_print = center_print;
  p->game = qa_q3_pres_create_scene_presentation(scene);
  if (p->game == NULL) qa_range_error("Out of memory");
  assert_current(p);
  const qa_app_q3_source_presentation_state *baseline = qa_app_q3_selected_source_presentation_baseline(p->source.source);
  if (baseline != NULL) {
    qa_q3_pres_client_game_state *state = qa_q3_pres_client_presentation_state(p->game);
    state->time = baseline->time;
    for (size_t i = 0; i < baseline->entity_count; i++) {
      const qa_app_q3_source_presentation_entity *row = &baseline->entities[i];
      qa_q3_pres_client_entity *entity = qa_q3_pres_client_game_state_entity_at(state, row->state.number);
      entity->previous_event = row->state.e_type > QA_Q3_BASE_ET_EVENTS ? 1 : row->state.event;
      entity->snapshot_time = baseline->time;
      bool found = false;
      for (size_t g = 0; g < p->generation_count; g++)
        if (p->generations[g].number == row->state.number) { p->generations[g].actor = row->actor; found = true; break; }
      if (!found) {
        if (p->generation_count == p->generation_capacity) {
          p->generation_capacity = p->generation_capacity == 0 ? 16 : p->generation_capacity * 2;
          generation_row *next = realloc(p->generations, p->generation_capacity * sizeof *next);
          if (next == NULL) qa_range_error("Out of memory");
          p->generations = next;
        }
        p->generations[p->generation_count].number = row->state.number;
        p->generations[p->generation_count++].actor = row->actor;
      }
    }
    if (!qa_q3_pres_snapshot_runtime_process_snapshots(qa_q3_pres_client_presentation_snapshots(p->game)))
      qa_range_error("Out of memory");
    assert_current(p);
  }
  qa_q3_pres_scene_recorder_clear_scene(qa_app_application_q3_services_scene(p->services));
  flush_audio(p);
}

static void close_run(void *ctx) { qa_app_selected_q3_presentation_close(ctx); }
static void create_run(void *ctx) {
  qa_app_selected_q3_presentation *p = ctx;
  initialize(p);
  assert_current(p);
}

qa_app_selected_q3_presentation *qa_app_selected_q3_presentation_create(
    const qa_app_selected_q3_presentation_options *options, qa_app_selected_q3_source source,
    const qa_app_viewing_seat *presentation) {
  qa_app_selected_q3_presentation *p = xcalloc(1, sizeof *p);
  p->options = *options; p->source = source; p->presentation = *presentation;
  p->actor = presentation->actor(presentation->ctx);
  p->generation = qa_app_q3_selected_source_generation(source.source);
  p->frame = -1;
  if (!qa_event_ring_init(&p->audio, 4096, sizeof(qa_app_q3_seat_audio_operation))
      || !qa_event_ring_open_cursor(&p->audio, &p->audio_cursor, true))
    qa_range_error("Out of memory");
  if (!qa_model_light_sampler_init(&p->light, qa_app_application_assets_world(options->base.assets)))
    qa_range_error("Out of memory");
  const qa_app_q3_source_presentation_state *baseline = qa_app_q3_selected_source_presentation_baseline(source.source);
  qa_app_q3_source_callbacks callbacks = {p, transport_select, transport_actor, NULL, NULL};
  p->transport = qa_app_application_q3_source_create(p->actor, baseline != NULL ? baseline : source.state, callbacks);
  if (p->transport == NULL) qa_range_error("Out of memory");
  qa_app_component_effect_frame frame = {p, effect_frame};
  p->binding = presentation->bind_component_effects(presentation->ctx, frame, NULL);
  qa_caught_error error;
  if (!qa_try(create_run, p, &error)) {
    qa_caught_error cleanup;
    bool closed = qa_try(close_run, p, &cleanup);
    qa_app_selected_q3_presentation_destroy(p);
    if (!closed)
      qa_raise("AggregateError", 0, 2, "Selected Q3 presentation initialization and cleanup failed (first %s: %s; cleanup %s: %s)",
               error.name, error.message, cleanup.name, cleanup.message);
    qa_rethrow(&error);
  }
  return p;
}

typedef struct prepare_job {
  qa_app_selected_q3_presentation *p;
  const qa_app_selected_q3_source *source;
  const qa_app_sim_presentation_event *events; size_t event_count;
  double frame;
  qa_app_sim_presentation_event *filtered;
} prepare_job;
static void prepare_run(void *ctx) {
  prepare_job *job = ctx;
  qa_app_selected_q3_presentation *p = job->p;
  const qa_app_selected_q3_source *source = job->source;
  if (p->game == NULL || p->services == NULL || p->renderer == NULL)
    qa_raise("Error", 0, 0, "Selected Q3 presentation has not initialized");
  /* TS new Map(entities.map(row => [number, actor])): the last row of a number
   * wins and keeps the first row's position. */
  const qa_app_q3_source_presentation_state *state = source->state;
  for (size_t i = 0; i < state->entity_count; i++) {
    double number = state->entities[i].state.number;
    bool first = true;
    for (size_t j = 0; j < i && first; j++) if (state->entities[j].state.number == number) first = false;
    if (!first) continue;
    qa_actor_id actor = state->entities[i].actor;
    for (size_t j = i + 1; j < state->entity_count; j++) if (state->entities[j].state.number == number) actor = state->entities[j].actor;
    const generation_row *previous = NULL;
    for (size_t g = 0; g < p->generation_count; g++) if (p->generations[g].number == number) previous = &p->generations[g];
    if (previous != NULL && !qa_same_actor(previous->actor, actor)) {
      qa_q3_pres_client_entity_init(qa_q3_pres_client_game_state_entity_at(qa_q3_pres_client_presentation_state(p->game), number));
      if (number < source->source->pool.max_clients) {
        const char *info = "";
        for (size_t c = 0; c < state->configstring_count; c++)
          if (state->configstrings[c].index == 544 + number) { info = state->configstrings[c].value; break; }
        if (!qa_q3_pres_client_info_store_new_client_info(qa_q3_pres_client_presentation_clients(p->game), number, info))
          qa_range_error("Out of memory");
        assert_current(p);
      }
    }
  }
  for (size_t i = 0; i < state->entity_count; i++) {
    double number = state->entities[i].state.number;
    bool found = false;
    for (size_t g = 0; g < p->generation_count; g++)
      if (p->generations[g].number == number) { p->generations[g].actor = state->entities[i].actor; found = true; break; }
    if (found) continue;
    if (p->generation_count == p->generation_capacity) {
      p->generation_capacity = p->generation_capacity == 0 ? 16 : p->generation_capacity * 2;
      generation_row *next = realloc(p->generations, p->generation_capacity * sizeof *next);
      if (next == NULL) qa_range_error("Out of memory");
      p->generations = next;
    }
    p->generations[p->generation_count].number = number;
    p->generations[p->generation_count++].actor = state->entities[i].actor;
  }
  job->filtered = xcalloc(job->event_count, sizeof *job->filtered);
  size_t filtered = 0;
  for (size_t i = 0; i < job->event_count; i++)
    if (job->events[i].content != NULL && strcmp(job->events[i].content, source->content) == 0) job->filtered[filtered++] = job->events[i];
  qa_app_application_q3_source_receive(p->transport, state, job->filtered, filtered, NULL, 0);
  qa_q3_pres_presentation_frame frame = {state->time, QA_Q3_PRES_STEREO_CENTER, false, job->frame};
  if (!qa_q3_pres_presentation_frame_runtime_draw_scene_frame(qa_q3_pres_client_presentation_frames(p->game), frame,
      p->presentation.camera(p->presentation.ctx)))
    qa_range_error("Out of memory");
  assert_current(p);
  qa_q3_pres_scene_recorder *recorder = qa_app_application_q3_services_scene(p->services);
  qa_q3_pres_scene_content scene;
  memset(&scene, 0, sizeof scene);
  if (!qa_q3_pres_scene_recorder_capture(recorder, &scene)) qa_range_error("Out of memory");
  qa_cleanup node;
  qa_cleanup_push(&node, (void (*)(void *))qa_q3_pres_scene_content_destroy, &scene);
  qa_q3_pres_scene_recorder_clear_scene(recorder);
  const qa_q3_pres_scene_content *contents[1] = {&scene};
  qa_app_application_q3_scene_renderer_preload(p->renderer, contents, 1);
  assert_current(p);
  flush_audio(p);
  qa_cleanup_pop(&node);
  if (p->has_scene) qa_q3_pres_scene_content_destroy(&p->scene);
  p->scene = scene; p->has_scene = true; p->frame = job->frame;
}
void qa_app_selected_q3_presentation_prepare(qa_app_selected_q3_presentation *p, const qa_app_selected_q3_source *source,
    const qa_app_sim_presentation_event *events, size_t event_count, double frame) {
  assert_current(p);
  if (!qa_app_selected_q3_presentation_owns(p, source, &p->presentation) || p->busy)
    qa_raise("Error", 0, 0, "Selected Q3 scene source changed or is already preparing");
  if (frame <= p->frame) return;
  p->busy = true;
  prepare_job job = {p, source, events, event_count, frame, NULL};
  qa_caught_error error;
  bool ok = qa_try(prepare_run, &job, &error);
  free(job.filtered);
  p->busy = false;
  if (!ok) close_or_aggregate(close_run, p, &error, "Selected Q3 frame and cleanup failed");
}

typedef struct close_step { qa_app_selected_q3_presentation *p; int step; } close_step;
static void close_step_run(void *ctx) {
  close_step *s = ctx;
  qa_app_selected_q3_presentation *p = s->p;
  switch (s->step) {
    case 0: p->presentation.unbind_component_effects(p->presentation.ctx, p->binding); break;
    case 1: if (p->game != NULL) qa_q3_pres_client_presentation_close(p->game); break;
    case 2: if (p->renderer != NULL) qa_app_application_q3_scene_renderer_close(p->renderer); break;
    case 3: if (p->services != NULL) qa_app_application_q3_cinematics_close(qa_app_application_q3_services_cinematics(p->services)); break;
    case 4: if (p->media != NULL) qa_app_application_q3_assets_close(p->media); break;
    default: {
      qa_app_q3_seat_audio_operation release;
      memset(&release, 0, sizeof release);
      release.kind = QA_APP_Q3_SEAT_AUDIO_RELEASE_OWNER;
      qa_app_q3_seat_audio_frame frame = {p->source.content, p->presentation.seat(p->presentation.ctx), true,
          p->source.source->host.provider, &release, 1};
      qa_app_application_audio_receive_cgame_frame(p->options.base.audio, &frame);
    }
  }
}
void qa_app_selected_q3_presentation_close(qa_app_selected_q3_presentation *p) {
  if (p->closed) return;
  p->closed = true;
  qa_event_ring_reset(&p->audio);
  if (p->has_scene) { qa_q3_pres_scene_content_destroy(&p->scene); p->has_scene = false; }
  cleanup_failures failures = {0};
  for (int step = 0; step < 6; step++) { close_step s = {p, step}; attempt(&failures, close_step_run, &s); }
  /* TS drops the references here; C frees what they owned. */
  if (p->game != NULL) { qa_q3_pres_client_presentation_destroy(p->game); p->game = NULL; }
  if (p->renderer != NULL) { qa_app_application_q3_scene_renderer_destroy(p->renderer); p->renderer = NULL; }
  if (p->services != NULL) { qa_app_application_q3_services_destroy(p->services); p->services = NULL; }
  if (p->media != NULL) { qa_app_application_q3_assets_destroy(p->media); p->media = NULL; }
  if (failures.count != 0) raise_aggregate(&failures, "Selected Q3 presentation cleanup failed");
}
void qa_app_selected_q3_presentation_destroy(qa_app_selected_q3_presentation *p) {
  if (p == NULL) return;
  qa_caught_error error;
  bool ok = qa_try(close_run, p, &error);
  if (p->has_marks) qa_q3_pres_world_mark_projector_destroy(&p->marks);
  qa_app_q3_snapshot_selection_destroy(p->selection);
  free(p->selection_rows);
  qa_app_application_q3_source_destroy(p->transport);
  qa_model_light_sampler_destroy(&p->light);
  qa_event_ring_close_cursor(&p->audio, &p->audio_cursor);
  qa_event_ring_destroy(&p->audio);
  if (p->cvars != NULL) qa_cvar_registry_destroy(p->cvars);
  free(p->generations);
  free(p);
  if (!ok) qa_rethrow(&error);
}

/* ---- ApplicationSelectedQ3Presentations ---- */
typedef struct seat_entry { qa_app_viewing_seat seat; qa_app_selected_q3_presentation *consumer; } seat_entry;
struct qa_app_selected_q3_presentations {
  qa_app_selected_q3_presentation_options options;
  seat_entry *seats; size_t seat_count, seat_capacity;
  bool closed, busy;
};
qa_app_selected_q3_presentations *qa_app_selected_q3_presentations_create(const qa_app_selected_q3_presentation_options *options) {
  qa_app_selected_q3_presentations *c = xcalloc(1, sizeof *c);
  c->options = *options;
  return c;
}
static bool seat_listed(const qa_app_viewing_seat *seats, size_t count, const void *ctx) {
  for (size_t i = 0; i < count; i++) if (seats[i].ctx == ctx) return true;
  return false;
}
static qa_app_selected_q3_presentation *remove_seat(qa_app_selected_q3_presentations *c, size_t index) {
  qa_app_selected_q3_presentation *consumer = c->seats[index].consumer;
  memmove(&c->seats[index], &c->seats[index + 1], (c->seat_count - index - 1) * sizeof *c->seats);
  c->seat_count--;
  return consumer;
}
static void destroy_run(void *ctx) { qa_app_selected_q3_presentation_destroy(ctx); }
void qa_app_selected_q3_presentations_retain(qa_app_selected_q3_presentations *c, const qa_app_viewing_seat *seats, size_t count) {
  if (c->closed) qa_raise("Error", 0, 0, "Selected Q3 presentation collection is closed");
  cleanup_failures failures = {0};
  for (size_t i = 0; i < c->seat_count;) {
    if (seat_listed(seats, count, c->seats[i].seat.ctx)) { i++; continue; }
    attempt(&failures, destroy_run, remove_seat(c, i));
  }
  if (failures.count != 0) raise_aggregate(&failures, "Selected Q3 seat retirement failed");
}
typedef struct collection_job {
  qa_app_selected_q3_presentations *c;
  const qa_app_viewing_seat *seats; size_t seat_count;
  const qa_app_selected_q3_source *source;
  const qa_app_sim_presentation_event *events; size_t event_count;
  double frame;
} collection_job;
static void collection_prepare_run(void *ctx) {
  collection_job *job = ctx;
  qa_app_selected_q3_presentations *c = job->c;
  qa_app_selected_q3_presentations_retain(c, job->seats, job->seat_count);
  for (size_t i = 0; i < c->seat_count;) {
    if (job->source != NULL && qa_app_selected_q3_presentation_owns(c->seats[i].consumer, job->source, &c->seats[i].seat)) { i++; continue; }
    qa_app_selected_q3_presentation_destroy(remove_seat(c, i));
  }
  if (job->source == NULL) return;
  for (size_t s = 0; s < job->seat_count; s++) {
    const qa_app_viewing_seat *seat = &job->seats[s];
    /* A structurally broken source (TS state without clients) throws a TypeError. */
    if (job->source->state == NULL) qa_raise("TypeError", 0, 0, "undefined is not an object (evaluating 'source.state.clients')");
    qa_actor_id actor = seat->actor(seat->ctx);
    bool client = false;
    for (size_t r = 0; r < job->source->state->client_count && !client; r++)
      client = qa_same_actor(job->source->state->clients[r].actor, actor);
    if (!client) continue;
    qa_app_selected_q3_presentation *consumer = NULL;
    for (size_t e = 0; e < c->seat_count; e++) if (c->seats[e].seat.ctx == seat->ctx) consumer = c->seats[e].consumer;
    if (consumer == NULL) {
      consumer = qa_app_selected_q3_presentation_create(&c->options, *job->source, seat);
      if (c->closed) {
        qa_app_selected_q3_presentation_destroy(consumer);
        qa_raise("Error", 0, 0, "Selected Q3 presentation collection retired during initialization");
      }
      if (c->seat_count == c->seat_capacity) {
        c->seat_capacity = c->seat_capacity == 0 ? 4 : c->seat_capacity * 2;
        seat_entry *next = realloc(c->seats, c->seat_capacity * sizeof *next);
        if (next == NULL) qa_range_error("Out of memory");
        c->seats = next;
      }
      c->seats[c->seat_count].seat = *seat; c->seats[c->seat_count++].consumer = consumer;
    }
    qa_app_selected_q3_presentation_prepare(consumer, job->source, job->events, job->event_count, job->frame);
  }
}
void qa_app_selected_q3_presentations_prepare(qa_app_selected_q3_presentations *c, const qa_app_viewing_seat *seats, size_t seat_count,
    const qa_app_selected_q3_source *source, const qa_app_sim_presentation_event *events, size_t event_count, double frame) {
  if (c->closed || c->busy) qa_raise("Error", 0, 0, "Selected Q3 presentation collection is closed or already preparing");
  c->busy = true;
  collection_job job = {c, seats, seat_count, source, events, event_count, frame};
  qa_caught_error error;
  bool ok = qa_try(collection_prepare_run, &job, &error);
  c->busy = false;
  if (!ok) qa_rethrow(&error);
}
void qa_app_selected_q3_presentations_close(qa_app_selected_q3_presentations *c) {
  if (c->closed) return;
  c->closed = true;
  cleanup_failures failures = {0};
  for (size_t i = 0; i < c->seat_count; i++) attempt(&failures, destroy_run, c->seats[i].consumer);
  c->seat_count = 0;
  if (failures.count != 0) raise_aggregate(&failures, "Selected Q3 presentation collection cleanup failed");
}
static void collection_close_run(void *ctx) { qa_app_selected_q3_presentations_close(ctx); }
void qa_app_selected_q3_presentations_destroy(qa_app_selected_q3_presentations *c) {
  if (c == NULL) return;
  qa_caught_error error;
  bool ok = qa_try(collection_close_run, c, &error);
  free(c->seats);
  free(c);
  if (!ok) qa_rethrow(&error);
}
