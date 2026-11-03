/* Port of src/app/bootstrap/remote-seat-view.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/remote_seat_view.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/content.h"
#include "qa/app/bootstrap/q3_client/cvars.h"
#include "qa/app/bootstrap/q3_client/qvm.h"
#include "qa/app/bootstrap/q3_seat_client.h"
#include "qa/app/bootstrap/remote_components.h"
#include "qa/app/bootstrap/render_settings.h"
#include "qa/compat/qvm/client_state.h"
#include "qa/foundation/support.h"
#include "qa/input/user_command.h"
#include "qa/network/common/js_number.h"
#include "qa/network/q3/game_state.h"
#include "qa/ui/settings/services.h"

/* Q3ClientConnection as the client-state view the cgame host reads (TS passes the
 * connection object itself). Rows are owned until the next read of the same kind. */
typedef struct q3_connection_view {
  qa_q3_client_connection *connection;
  char *text;
  char **tokens; size_t token_count;
  qa_q3_source_game_state_record record; bool has_record;
} q3_connection_view;

struct qa_app_remote_seat_view {
  qa_app_remote_seat_view_options options;
  qa_app_world_seat_presentation *presentation;
  qa_app_seat_ui *ui; /* the presentation's seat UI; freed after it (TS GC) */
  qa_app_application_effects *effects;
  qa_app_application_assets *assets;
  bool owns_assets;
  bool closed;
  char **reported; size_t reported_count;
  qa_app_application_q3_client *q3;
  qa_app_q3_client_cvars *guest_cvars;
  q3_connection_view connection;
  qa_app_sim_presentation_access access;
  qa_command_context context;
  qa_scene_queries queries;
  qa_app_source_effect_sound *sounds; size_t sound_count;
  qa_app_recipient_effect_sounds *recipients; size_t recipient_count;
  qa_app_application_effect_sound *effect_sounds;
  /* TS components: RemoteComponentView over the unified replica (NULL otherwise) and the options it was given. */
  qa_app_remote_component_view *components;
  qa_app_qvm_display_renderer display;
  qa_app_mod_presentations_options component_options;
};

static void *xcalloc(size_t count, size_t size) {
  void *out = calloc(count ? count : 1, size);
  if (out == NULL) qa_range_error("Out of memory");
  return out;
}
static const qa_app_remote_seat_presentation *remote_of(const qa_app_remote_seat_view *view) {
  return qa_app_remote_seat_source_remote(view->options.source);
}

/* ---- the remote as the presentation access WorldSeatPresentation and the seat UI read ---- */
static const qa_world_text *access_world_text(void *ctx, size_t *out_count) {
  return qa_app_remote_seat_presentation_world_text(remote_of(ctx), out_count);
}
static qa_app_sim_player_view access_player_view(void *ctx, qa_actor_id actor) {
  return qa_app_remote_seat_presentation_player_view(remote_of(ctx), actor);
}
static qa_app_sim_player_ui access_player_ui(void *ctx, qa_actor_id actor) {
  const qa_app_remote_seat_presentation *remote = remote_of(ctx);
  switch (remote->family) {
    case QA_APP_REMOTE_FAMILY_UNIFIED: return *qa_app_unified_remote_player_ui(remote->unified, actor);
    case QA_APP_REMOTE_FAMILY_Q1: return qa_app_q1_remote_player_ui(remote->q1, actor);
    case QA_APP_REMOTE_FAMILY_QW: return qa_app_qw_remote_player_ui(remote->qw, actor);
    case QA_APP_REMOTE_FAMILY_Q2: return qa_app_q2_remote_player_ui(remote->q2, actor);
    case QA_APP_REMOTE_FAMILY_Q3: return qa_app_q3_remote_player_ui(remote->q3, actor);
  }
  qa_range_error("Unknown remote family");
}
static const qa_active_mod_client_presentation *access_mod_sources(void *ctx, size_t *out_count) {
  return qa_app_unified_remote_mod_client_sources(remote_of(ctx)->unified, out_count);
}
static const qa_q3_character_view *access_character_views(void *ctx, size_t *out_count) {
  return qa_app_remote_seat_presentation_character_views(remote_of(ctx), out_count);
}
static const qa_app_simulation_presentation *access_presentations(void *ctx, size_t *out_count) {
  return qa_app_remote_seat_presentation_presentations(remote_of(ctx), out_count);
}
static bool effects_is_player(void *ctx, qa_actor_id actor) {
  return qa_app_remote_seat_presentation_is_player(remote_of(ctx), actor);
}

/* ---- ApplicationSeatUi closures ---- */
typedef struct window_operation { const qa_ui_window_mutation *mutation; } window_operation;
static void apply_window(void *ctx) {
  window_operation *operation = ctx;
  qa_ui_apply_window_mutation(operation->mutation);
}
static void ui_mutate_window(void *ctx, const qa_ui_window_mutation *mutation) {
  qa_app_remote_seat_view *view = ctx;
  window_operation operation = {mutation};
  qa_app_native_renderer_mutate_window(view->options.renderer, apply_window, &operation);
}
static void ui_quit(void *ctx) {
  qa_app_remote_seat_view *view = ctx;
  view->options.quit(view->options.ctx);
}
static void ui_command(void *ctx, const char *name, const char *const *args, size_t arg_count) {
  qa_app_remote_seat_view *view = ctx;
  view->options.execute(view->options.ctx, name, args, arg_count);
}

/* ---- the Q3 connection view ---- */
static q3_connection_view *cv(void *ctx) { return &((qa_app_remote_seat_view *)ctx)->connection; }
static double cv_generation(void *ctx) { return (double)qa_q3_client_connection_generation(cv(ctx)->connection); }
static double cv_message(void *ctx) { return (double)qa_q3_client_connection_server_message_sequence(cv(ctx)->connection); }
static double cv_executed(void *ctx) {
  return (double)qa_q3_client_connection_last_executed_server_command(cv(ctx)->connection);
}
static double cv_client_number(void *ctx) { return (double)qa_q3_client_connection_client_number(cv(ctx)->connection); }
static bool cv_ping(void *ctx, double number, double *out) {
  int64_t ping = 0;
  if (!qa_q3_client_connection_snapshot_ping(cv(ctx)->connection, (int64_t)number, &ping)) return false;
  *out = (double)ping;
  return true;
}
static void cv_free_tokens(q3_connection_view *view) {
  for (size_t i = 0; i < view->token_count; i++) free(view->tokens[i]);
  free(view->tokens);
  view->tokens = NULL;
  view->token_count = 0;
}
static bool cv_server_command(void *ctx, double sequence, const char *const **out_tokens, size_t *out_count) {
  q3_connection_view *view = cv(ctx);
  cv_free_tokens(view);
  if (!qa_q3_client_connection_get_server_command(view->connection, (int64_t)sequence, &view->tokens, &view->token_count))
    return false;
  *out_tokens = (const char *const *)view->tokens;
  *out_count = view->token_count;
  return true;
}
static const char *cv_get(void *ctx, double index) {
  q3_connection_view *view = cv(ctx);
  free(view->text);
  view->text = NULL;
  (void)qa_q3_client_game_state_storage_get(qa_q3_client_connection_game_state(view->connection), (int64_t)index,
      &view->text);
  return view->text;
}
static void cv_free_record(q3_connection_view *view) {
  if (!view->has_record) return;
  free(view->record.string_offsets);
  free(view->record.string_data);
  view->has_record = false;
}
static qa_qvmx_source_game_state_record cv_record(void *ctx) {
  q3_connection_view *view = cv(ctx);
  cv_free_record(view);
  if (!qa_q3_client_game_state_storage_copy_source_record(qa_q3_client_connection_game_state(view->connection),
          &view->record))
    qa_range_error("Out of memory");
  view->has_record = true;
  qa_qvmx_source_game_state_record out;
  out.string_offsets = view->record.string_offsets;
  out.string_offset_count = 1024;
  out.string_data = view->record.string_data;
  out.string_data_length = 16000;
  out.data_count = (double)view->record.data_count;
  return out;
}
static double cv_command_number(void *ctx) {
  return (double)qa_q3_wire_command_history_current_number(qa_q3_client_connection_commands(cv(ctx)->connection));
}
static bool cv_command_read(void *ctx, double number, qa_qvmx_wire_user_command *out) {
  qa_q3_wire_user_command command;
  if (!qa_q3_wire_command_history_read(qa_q3_client_connection_commands(cv(ctx)->connection), (int64_t)number, &command))
    return false;
  out->server_time = command.server_time;
  for (int i = 0; i < 3; i++) out->angles[i] = command.angles[i];
  out->forwardmove = command.forwardmove;
  out->rightmove = command.rightmove;
  out->upmove = command.upmove;
  out->buttons = command.buttons;
  out->weapon = command.weapon;
  return true;
}

/* ---- ApplicationQ3Client closures ---- */
static bool q3_split_screen(void *ctx) {
  qa_app_remote_seat_view *view = ctx;
  return view->options.count(view->options.ctx) > 1;
}
static bool q3_save_font_data(void *ctx) {
  qa_app_remote_seat_view *view = ctx;
  qa_cvar_registry *shared = qa_app_application_input_shared_cvars(view->options.controls);
  return (shared != NULL ? qa_cvar_registry_variable_value(shared, "r_saveFontData") : 0) != 0;
}
static qa_app_q3_renderer_settings q3_render_settings(void *ctx) {
  qa_app_remote_seat_view *view = ctx;
  return qa_app_q3_renderer_settings_from(qa_app_application_input_shared_cvars(view->options.controls));
}
static void q3_assert_current(void *ctx) {
  qa_app_remote_seat_view *view = ctx;
  view->options.assert_current(view->options.ctx);
}
static qa_rect q3_viewport(void *ctx) {
  qa_app_remote_seat_view *view = ctx;
  return view->options.viewport(view->options.ctx);
}
static double q3_now(void *ctx) {
  qa_app_remote_seat_view *view = ctx;
  return view->options.now(view->options.ctx);
}
static void q3_dispatch_reliable(void *ctx, const char *text, const qa_command_context *source) {
  (void)source; /* TS command => source.network.command(command) */
  qa_app_remote_seat_view *view = ctx;
  qa_app_remote_seat_network_command(qa_app_remote_seat_source_network(view->options.source), text);
}
static void q3_reliable(void *ctx, const char *text) {
  qa_app_remote_seat_view *view = ctx;
  qa_app_application_input_enqueue_client_reliable(view->options.controls, text, &view->context, q3_dispatch_reliable, view);
}
static void q3_console(void *ctx, const char *text) {
  qa_app_remote_seat_view *view = ctx;
  qa_command_context source;
  memset(&source, 0, sizeof source);
  source.session = view->context.session;
  source.origin.kind = QA_COMMAND_ORIGIN_SCRIPT;
  source.origin.name = "q3-cgame";
  source.origin.caller = &view->context.origin;
  qa_app_application_input_enqueue_client_command(view->options.controls, text, &source);
}
static void q3_print(void *ctx, const char *text) {
  qa_app_remote_seat_view *view = ctx;
  qa_app_application_input_print(view->options.controls, text, &view->context);
}
static double guest_execute_now(void *ctx, const char *text, const qa_command_context *source) {
  qa_app_remote_seat_view *view = ctx;
  return qa_app_application_input_guest_execute_now(view->options.controls, text, source);
}
static void guest_insert(void *ctx, const char *text, const qa_command_context *source) {
  qa_app_remote_seat_view *view = ctx;
  qa_app_application_input_guest_insert(view->options.controls, text, source);
}
static void guest_append(void *ctx, const char *text, const qa_command_context *source) {
  qa_app_remote_seat_view *view = ctx;
  qa_app_application_input_guest_append(view->options.controls, text, source, -1);
}
static qa_app_qvm_client_state_info q3_client_state(void *ctx) {
  qa_app_remote_seat_view *view = ctx;
  const qa_app_remote_seat_network *network = qa_app_remote_seat_source_network(view->options.source);
  qa_app_network_phase phase = qa_app_remote_seat_network_phase(network);
  qa_app_qvm_client_state_info info;
  info.phase = phase == QA_APP_PHASE_ACTIVE ? 8 : phase == QA_APP_PHASE_LOADING ? 6 : 5;
  info.connect_packet_count = network->family == QA_APP_REMOTE_FAMILY_Q3
      ? qa_app_q3_client_network_connect_packet_count(network->q3) : 0;
  info.client_number = (double)qa_q3_client_connection_client_number(view->options.q3->connection);
  const qa_app_options *options = view->options.options;
  info.server_name = options->network_kind != NULL && strcmp(options->network_kind, "q3-client") == 0
      && options->network_remote != NULL ? options->network_remote : "";
  info.message = "";
  return info;
}

/* ---- WorldSeatPresentation closures ---- */
static double world_text_cull_factor(void *ctx) {
  qa_app_remote_seat_view *view = ctx;
  return qa_cvar_registry_variable_value(qa_app_image_settings_cvars(view->options.images), "gl_debug_distfrac");
}
static double field_of_view(void *ctx) {
  qa_app_remote_seat_view *view = ctx;
  return qa_app_view_settings_field_of_view(view->options.view);
}
static double console_scale(void *ctx) {
  qa_app_remote_seat_view *view = ctx;
  return qa_cvar_registry_variable_value(qa_app_image_settings_cvars(view->options.images), "con_scale");
}

static void warn_print(void *ctx, const char *text) {
  qa_app_remote_seat_view *view = ctx;
  view->options.print(view->options.ctx, text);
}

typedef struct prepare_state {
  qa_app_remote_seat_view *view;
  qa_app_seat_ui *ui;
  qa_app_seat_ui_options ui_options;
  qa_ui_setting_binding view_binding;
  qa_app_world_seat_presentation_options presentation_options;
  qa_app_seat_q3_client q3_seat;
} prepare_state;
static void prepare_body(void *ctx) {
  prepare_state *state = ctx;
  qa_app_remote_seat_view *view = state->view;
  const qa_app_remote_seat_view_options *options = &view->options;
  const qa_app_remote_seat_presentation *remote = remote_of(view);
  qa_session_seat *seat = options->local->player.seat;
  options->assert_current(options->ctx);
  if (view->owns_assets) (void)qa_app_application_assets_load_world(view->assets);
  qa_text_font_selection font = view->owns_assets ? qa_app_application_assets_load_console_font(view->assets) : options->font;
  const qa_app_menu_typography *typography = qa_app_application_assets_load_menu_typography(view->assets);
  options->assert_current(options->ctx);
  size_t failure_count = 0;
  qa_app_effect_preload_failure *failures = qa_app_application_effects_preload_transient_resources(view->effects,
      &failure_count);
  for (size_t i = 0; i < failure_count; i++) {
    size_t length = strlen(failures[i].content) + strlen(failures[i].path) + strlen(failures[i].error) + 48;
    char *line = xcalloc(length, 1);
    snprintf(line, length, "Optional effect preload skipped: %s/%s: %s\n", failures[i].content, failures[i].path,
        failures[i].error);
    options->print(options->ctx, line);
    free(line);
  }
  qa_app_effect_preload_failures_free(failures, failure_count);
  options->assert_current(options->ctx);
  qa_app_seat_ui_options *ui = &state->ui_options;
  memset(ui, 0, sizeof *ui);
  ui->local = options->local;
  ui->art = options->art;
  ui->input = options->controls;
  ui->mutate_window = ui_mutate_window;
  ui->mutate_ctx = view;
  ui->simulation = &view->access;
  ui->font = font;
  ui->audio = options->audio;
  ui->quit = ui_quit;
  ui->quit_ctx = view;
  ui->command = ui_command;
  ui->command_ctx = view;
  ui->typography = typography;
  state->view_binding = qa_app_view_settings_binding(options->view);
  ui->view_setting = &state->view_binding;
  state->ui = qa_app_seat_ui_create(ui);
  if (state->ui == NULL) qa_range_error("Out of memory");
  if (remote->family == QA_APP_REMOTE_FAMILY_Q3) {
    const qa_app_remote_seat_view_q3 *guest = options->q3;
    if (guest == NULL) qa_raise("Error", 0, 0, "Q3 remote seat requires its admitted guest owners");
    qa_q3_wire_command_history *history = qa_q3_client_connection_commands(guest->connection);
    qa_q3_wire_user_command command;
    bool has_command = qa_q3_wire_command_history_read(history, qa_q3_wire_command_history_current_number(history),
        &command);
    qa_vec3 angles = {0, 0, 0};
    if (has_command) {
      /* TS ((angle << 16) >> 16) * (360 / 65536) */
      angles.x = (double)(int16_t)(uint16_t)(uint32_t)command.angles[0] * (360.0 / 65536.0);
      angles.y = (double)(int16_t)(uint16_t)(uint32_t)command.angles[1] * (360.0 / 65536.0);
      angles.z = (double)(int16_t)(uint16_t)(uint32_t)command.angles[2] * (360.0 / 65536.0);
    }
    qa_input_command_builder_set_view_angles(options->local->builder, angles);
    view->connection.connection = guest->connection;
    qa_seat_id seat_id = qa_session_seat_id(seat);
    view->guest_cvars = qa_app_application_input_guest_cvars(options->controls, seat_id);
    qa_app_application_q3_client_options q3;
    memset(&q3, 0, sizeof q3);
    q3.kind = QA_APP_Q3_CLIENT_QVM;
    q3.ctx = view;
    q3.source = qa_app_q3_remote_cgame_source(remote->q3);
    q3.connection.ctx = view;
    q3.connection.get_generation = cv_generation;
    q3.connection.get_server_message_sequence = cv_message;
    q3.connection.get_last_executed_server_command = cv_executed;
    q3.connection.get_client_number = cv_client_number;
    q3.connection.snapshot_ping = cv_ping;
    q3.connection.get_server_command = cv_server_command;
    q3.connection.game_state = (qa_q3_client_game_state_view){view, cv_get, cv_record};
    q3.connection.commands = (qa_q3_client_commands_view){view, cv_command_number, cv_command_read};
    q3.keys = guest->keys;
    q3.cvars = options->cvars;
    q3.time_cvars = options->cvars;
    q3.split_screen = q3_split_screen;
    q3.save_font_data = q3_save_font_data;
    q3.render_settings = q3_render_settings;
    q3.assert_current = q3_assert_current;
    q3.command_buffer = (qa_app_qvm_command_target){view, guest_execute_now, guest_insert, guest_append};
    q3.guest_cvars = qa_app_q3_client_cvars_services(view->guest_cvars);
    q3.guest_input = qa_app_application_input_guest_input(options->controls, seat_id);
    q3.command_bindings = qa_app_application_input_client_commands(options->controls);
    q3.command_registration = qa_app_application_input_client_command_registration(options->controls, seat_id, NULL);
    /* Pick<NativeRenderer, "backend" | "driver" | "glConfig"> read now */
    q3.renderer.backend_stencil_bits = qa_app_native_renderer_backend(options->renderer).stencil_bits;
    q3.renderer.driver = qa_app_native_renderer_driver(options->renderer);
    q3.renderer.gl_config = qa_app_native_renderer_gl_config(options->renderer);
    q3.browser = guest->browser;
    q3.assets = view->assets;
    q3.queries = qa_app_remote_seat_presentation_scene(remote);
    q3.local = options->local;
    q3.audio = options->audio;
    q3.viewport = q3_viewport;
    q3.now = q3_now;
    q3.client_state = q3_client_state;
    q3.reliable = q3_reliable;
    q3.console = q3_console;
    q3.print = q3_print;
    view->q3 = qa_app_application_q3_client_create(&q3, NULL);
    if (view->q3 == NULL) qa_range_error("Out of memory");
    options->assert_current(options->ctx);
  }
  options->assert_current(options->ctx);
  qa_app_world_seat_presentation_options *p = &state->presentation_options;
  memset(p, 0, sizeof *p);
  p->local = options->local;
  p->assets = view->assets;
  p->renderer = options->renderer;
  p->simulation = &view->access;
  p->seat_count = options->count(options->ctx);
  p->font = font;
  p->characters = NULL; /* TS: Q3 characters only for the unified replica */
  p->ui = state->ui;
  p->effects = view->effects;
  if (view->q3 != NULL) {
    state->q3_seat = qa_app_application_q3_client_seat(view->q3);
    p->q3_client = &state->q3_seat;
  }
  p->world_text_cull_factor = world_text_cull_factor;
  p->world_text_cull_factor_ctx = view;
  p->field_of_view = field_of_view;
  p->field_of_view_ctx = view;
  p->console_scale = console_scale;
  p->console_scale_ctx = view;
  view->presentation = qa_app_world_seat_presentation_create(p);
  if (view->presentation == NULL) qa_range_error("Out of memory");
  state->ui = NULL; /* closed by the presentation from here */
  /* TS publishLayout(options.index(), options.count()): index first */
  double index = options->index(options->ctx);
  double count = options->count(options->ctx);
  qa_app_world_seat_presentation_publish_layout(view->presentation, index, count);
  qa_session_seat_validate_presentation(seat, qa_app_world_seat_presentation_seat(view->presentation));
}
static void cleanup_q3(void *ctx) {
  qa_app_remote_seat_view *view = ctx;
  if (view->q3 != NULL) qa_app_application_q3_client_close(view->q3);
}
static void cleanup_ui(void *ctx) {
  prepare_state *state = ctx;
  if (state->ui != NULL) qa_app_seat_ui_close(state->ui);
}
static void cleanup_presentation(void *ctx) {
  qa_app_remote_seat_view *view = ctx;
  if (view->presentation != NULL) qa_app_world_seat_presentation_close(view->presentation);
}
static void cleanup_effects(void *ctx) {
  qa_app_remote_seat_view *view = ctx;
  qa_app_application_effects_close(view->effects);
}
static void cleanup_assets(void *ctx) {
  qa_app_remote_seat_view *view = ctx;
  if (view->owns_assets) qa_app_application_assets_close(view->assets);
}
static void connection_view_release(q3_connection_view *view) {
  free(view->text);
  view->text = NULL;
  cv_free_tokens(view);
  cv_free_record(view);
}
/* renderer.c (declared in application_internal.h): the QVM display the component presentations draw through. */
qa_app_qvm_display_renderer qa_app_native_renderer_qvm_display(qa_app_native_renderer *renderer);
/* The RemoteComponentView options of the constructor (TS `new RemoteComponentView(remote, { assets, audio, input:
 * options.controls, queries: remote.scene, renderer, systemCinematics, print, nextFrame: setImmediate, clock: { now:
 * options.now, frameNumber: () => remote.output?.snapshot.frame.frame ?? 0 } })`). */
static void component_print(void *ctx, const char *text) {
  qa_app_remote_seat_view *view = ctx;
  view->options.print(view->options.ctx, text);
}
static void component_next_frame(void *ctx) { (void)ctx; /* TS setImmediate turn: synchronous here */ }
static const qa_app_system_cinematic_host *component_cinematics(void *ctx, const qa_active_mod_presentation *source,
    const qa_app_viewing_seat *presentation, const qa_app_component_cinematic_scope *scope) {
  qa_app_remote_seat_view *view = ctx;
  if (view->options.system_cinematics == NULL) qa_raise("Error", 0, 0, "Remote seat view has no system cinematics");
  return view->options.system_cinematics(view->options.system_cinematics_ctx, source, presentation, scope);
}
static double component_now(void *ctx) {
  qa_app_remote_seat_view *view = ctx;
  return view->options.now(view->options.ctx);
}
static double component_frame(void *ctx) {
  qa_app_remote_seat_view *view = ctx;
  const qa_simulation_output *output = qa_app_remote_seat_presentation_output(remote_of(view));
  return output != NULL ? output->snapshot.frame.frame : 0;
}
static void create_components(qa_app_remote_seat_view *view, const qa_app_remote_seat_presentation *remote) {
  if (remote->family != QA_APP_REMOTE_FAMILY_UNIFIED) return;
  view->display = qa_app_native_renderer_qvm_display(view->options.renderer);
  qa_app_mod_presentations_options *o = &view->component_options;
  memset(o, 0, sizeof *o);
  o->assets = view->assets;
  o->audio = view->options.audio;
  o->input = view->options.controls;
  o->queries = qa_app_remote_seat_presentation_scene(remote);
  o->renderer = &view->display;
  o->ctx = view;
  o->print = component_print;
  o->next_frame = component_next_frame;
  o->system_cinematics = component_cinematics;
  o->clock = (qa_app_q3_service_clock){view, component_now, component_frame};
  o->hosts = qa_app_application_assets_q3_presentation_hosts(view->assets, view->options.audio);
  view->components = qa_app_remote_component_view_create(remote->unified, o);
  if (view->components == NULL) qa_range_error("Out of memory");
}
static void view_free(qa_app_remote_seat_view *view) {
  qa_app_remote_component_view_destroy(view->components);
  if (view->presentation != NULL) qa_app_world_seat_presentation_destroy(view->presentation);
  if (view->ui != NULL) qa_app_seat_ui_destroy(view->ui);
  if (view->q3 != NULL) qa_app_application_q3_client_destroy(view->q3);
  if (view->guest_cvars != NULL) qa_app_q3_client_cvars_destroy(view->guest_cvars);
  if (view->effects != NULL) qa_app_application_effects_destroy(view->effects);
  if (view->owns_assets && view->assets != NULL) qa_app_application_assets_destroy(view->assets);
  connection_view_release(&view->connection);
  for (size_t i = 0; i < view->reported_count; i++) free(view->reported[i]);
  free(view->reported);
  free(view->sounds);
  for (size_t i = 0; i < view->recipient_count; i++) free(view->recipients[i].sounds);
  free(view->recipients);
  free(view->effect_sounds);
  free(view);
}
static void create_components_body(void *ctx) {
  qa_app_remote_seat_view *view = ctx;
  create_components(view, remote_of(view));
}
qa_app_remote_seat_view *qa_app_remote_seat_view_prepare(const qa_app_remote_seat_view_options *options) {
  qa_app_remote_seat_view *view = calloc(1, sizeof *view);
  if (view == NULL) return NULL;
  view->options = *options;
  const qa_app_remote_seat_presentation *remote = remote_of(view);
  view->owns_assets = options->content != qa_app_application_assets_content(options->assets);
  if (view->owns_assets) {
    qa_scene_image_policy_owned policy;
    qa_app_image_settings_policy(options->images, &policy);
    qa_app_world_asset_images images;
    memset(&images, 0, sizeof images);
    images.policy = policy.policy;
    images.model_policy = qa_app_image_settings_model_policy(options->images);
    images.cvars = qa_app_image_settings_cvars(options->images);
    view->assets = qa_app_create_world_assets(options->content, qa_app_native_renderer_owner(options->renderer),
        qa_app_native_renderer_images(options->renderer), warn_print, view, &images);
    qa_scene_image_policy_owned_destroy(&policy);
    if (view->assets == NULL) { free(view); return NULL; }
  } else view->assets = options->assets;
  qa_session_seat *seat = options->local->player.seat;
  qa_seat_id seat_id = qa_session_seat_id(seat);
  view->context.session = seat_id.session;
  view->context.origin.kind = QA_COMMAND_ORIGIN_LOCAL_SEAT;
  view->context.origin.seat = seat_id;
  view->context.origin.client = qa_session_client_id(qa_session_seat_client(seat));
  memset(&view->access, 0, sizeof view->access);
  view->access.ctx = view;
  view->access.world_text = access_world_text;
  view->access.player_view = access_player_view;
  view->access.player_ui = access_player_ui;
  view->access.character_views = access_character_views;
  view->access.presentations = access_presentations;
  if (remote->family == QA_APP_REMOTE_FAMILY_UNIFIED) view->access.mod_client_presentation_sources = access_mod_sources;
  view->queries = qa_shared_as_scene_queries(qa_app_remote_seat_presentation_scene(remote));
  view->effects = qa_app_application_effects_create(view->assets, view->queries, effects_is_player, view,
      options->options->seed);
  if (view->effects == NULL) { view_free(view); return NULL; }
  prepare_state state;
  memset(&state, 0, sizeof state);
  state.view = view;
  qa_caught_error caught;
  if (!qa_try(prepare_body, &state, &caught)) {
    /* TS: close q3, ui, effects and owned assets; an extra failure makes an AggregateError */
    bool failed = false;
    qa_caught_error cleanup;
    if (!qa_try(cleanup_q3, view, &cleanup)) failed = true;
    if (!qa_try(cleanup_ui, &state, &cleanup)) failed = true;
    if (!qa_try(cleanup_presentation, view, &cleanup)) failed = true;
    if (!qa_try(cleanup_effects, view, &cleanup)) failed = true;
    if (!qa_try(cleanup_assets, view, &cleanup)) failed = true;
    if (state.ui != NULL) qa_app_seat_ui_destroy(state.ui);
    view_free(view);
    if (failed) qa_raise("AggregateError", 0, 0, "Remote seat view preparation failed");
    qa_rethrow(&caught);
  }
  view->ui = state.ui;
  /* new RemoteSeatView(...): the constructor builds the unified replica's component view. */
  qa_caught_error created;
  if (!qa_try(create_components_body, view, &created)) { view_free(view); qa_rethrow(&created); }
  return view;
}
static void close_view(void *ctx) { qa_app_remote_seat_view_close(ctx); }
void qa_app_remote_seat_view_destroy(qa_app_remote_seat_view *view) {
  if (view == NULL) return;
  if (!view->closed) {
    qa_caught_error caught;
    (void)qa_try(close_view, view, &caught);
  }
  view_free(view);
}
qa_app_world_seat_presentation *qa_app_remote_seat_view_presentation(const qa_app_remote_seat_view *view) {
  return view->presentation;
}
qa_app_application_effects *qa_app_remote_seat_view_effects(const qa_app_remote_seat_view *view) { return view->effects; }
qa_app_application_assets *qa_app_remote_seat_view_assets(const qa_app_remote_seat_view *view) { return view->assets; }

static void close_presentation_cleanup(void *ctx) {
  qa_app_world_seat_presentation_close(ctx);
}
void qa_app_remote_seat_view_publish(qa_app_remote_seat_view *view, double index, double count) {
  view->options.assert_current(view->options.ctx);
  qa_app_world_seat_presentation_publish_layout(view->presentation, index, count);
  qa_session_seat *seat = view->options.local->player.seat;
  (void)qa_session_seat_attach_presentation(seat, qa_app_world_seat_presentation_seat(view->presentation),
      close_presentation_cleanup, view->presentation);
}
static bool reported(qa_app_remote_seat_view *view, const char *key) {
  for (size_t i = 0; i < view->reported_count; i++)
    if (strcmp(view->reported[i], key) == 0) return true;
  return false;
}
void qa_app_remote_seat_view_prepare_frame(qa_app_remote_seat_view *view, const qa_simulation_output *output, bool music,
    qa_app_audio_seat_events *out) {
  if (view->closed) qa_raise("Error", 0, 0, "Remote seat view is closed");
  const qa_app_remote_seat_presentation *remote = remote_of(view);
  size_t event_count = 0, model_count = 0, character_count = 0;
  const qa_app_sim_presentation_event *events = qa_app_remote_seat_presentation_drain_events(remote, &event_count);
  if (remote->family == QA_APP_REMOTE_FAMILY_UNIFIED) {
    size_t simulation_count = 0;
    const qa_simulation_event *simulation = qa_app_unified_remote_drain_simulation_events(remote->unified, &simulation_count);
    qa_session_seat_receive(view->options.local->player.seat, simulation, simulation_count);
  }
  const qa_app_simulation_presentation *models = qa_app_remote_seat_presentation_presentations(remote, &model_count);
  const qa_q3_character_view *characters = qa_app_remote_seat_presentation_character_views(remote, &character_count);
  qa_app_application_effects_receive(view->effects, events, event_count);
  if (remote->family != QA_APP_REMOTE_FAMILY_Q3)
    qa_app_application_effects_prepare(view->effects, &output->snapshot, models, model_count, characters, character_count);
  size_t unhandled_count = 0;
  qa_app_unhandled_effect *unhandled = qa_app_application_effects_drain_unhandled(view->effects, &unhandled_count);
  qa_seat_id seat_id = qa_session_seat_id(view->options.local->player.seat);
  for (size_t i = 0; i < unhandled_count; i++) {
    const char *content = unhandled[i].source.content;
    size_t key_length = strlen(content) + strlen(unhandled[i].reason) + 2;
    char *key = xcalloc(key_length, 1);
    snprintf(key, key_length, "%s:%s", content, unhandled[i].reason);
    if (reported(view, key)) { free(key); continue; }
    char **grown = realloc(view->reported, (view->reported_count + 1) * sizeof *grown);
    if (grown == NULL) { free(key); qa_range_error("Out of memory"); }
    view->reported = grown;
    view->reported[view->reported_count++] = key;
    char player[40];
    qa_net_js_number((double)seat_id.index + 1, player, sizeof player);
    size_t length = strlen(player) + strlen(unhandled[i].source.kind) + strlen(unhandled[i].reason) + 48;
    char *line = xcalloc(length, 1);
    snprintf(line, length, "Player %s: unresolved %s effect: %s\n", player, unhandled[i].source.kind,
        unhandled[i].reason);
    view->options.print(view->options.ctx, line);
    free(line);
  }
  qa_app_unhandled_effects_free(unhandled, unhandled_count);
  if (view->components != NULL) qa_app_remote_component_view_prepare_media(view->components, events, event_count, music);
  qa_app_world_seat_presentation_source_events(view->presentation, events, event_count);
  qa_app_world_seat_presentation_prepare(view->presentation, &output->snapshot, models, model_count, characters,
      character_count);
  if (view->components != NULL)
    qa_app_remote_component_view_prepare(view->components, view->presentation, events, event_count, output->snapshot.frame.frame);
  free(view->sounds);
  for (size_t i = 0; i < view->recipient_count; i++) free(view->recipients[i].sounds);
  free(view->recipients);
  free(view->effect_sounds);
  view->sounds = qa_app_application_effects_drain_sounds(view->effects, &view->sound_count);
  view->recipients = qa_app_application_effects_drain_recipient_sounds(view->effects, &view->recipient_count);
  size_t total = view->sound_count;
  qa_actor_id actor = view->options.local->player.actor;
  for (size_t i = 0; i < view->recipient_count; i++)
    if (qa_same_actor(view->recipients[i].recipient, actor)) total += view->recipients[i].sound_count;
  view->effect_sounds = xcalloc(total, sizeof *view->effect_sounds);
  size_t at = 0;
  for (size_t i = 0; i < view->sound_count; i++) view->effect_sounds[at++] = view->sounds[i];
  for (size_t i = 0; i < view->recipient_count; i++)
    if (qa_same_actor(view->recipients[i].recipient, actor))
      for (size_t j = 0; j < view->recipients[i].sound_count; j++) view->effect_sounds[at++] = view->recipients[i].sounds[j];
  memset(out, 0, sizeof *out);
  out->seat = seat_id;
  out->snapshot = &output->snapshot;
  out->events = events;
  out->event_count = event_count;
  out->scene = &view->queries;
  out->music = false;
  out->effect_sounds = view->effect_sounds;
  out->effect_sound_count = total;
}
static void close_components(void *ctx) {
  qa_app_remote_seat_view *view = ctx;
  if (view->components != NULL) qa_app_remote_component_view_close(view->components);
}
static void close_q3(void *ctx) {
  qa_app_remote_seat_view *view = ctx;
  if (view->q3 != NULL) qa_app_application_q3_client_shutdown(view->q3);
}
static void close_seat(void *ctx) {
  qa_app_remote_seat_view *view = ctx;
  qa_session_seat *seat = view->options.local->player.seat;
  qa_seat_presentation current;
  qa_seat_presentation mine = qa_app_world_seat_presentation_seat(view->presentation);
  if (qa_session_seat_presentation(seat, &current) && current.ctx == mine.ctx) qa_session_seat_clear_presentation(seat);
  else qa_app_world_seat_presentation_close(view->presentation);
}
void qa_app_remote_seat_view_close(qa_app_remote_seat_view *view) {
  if (view->closed) return;
  view->closed = true;
  bool failed = false;
  qa_caught_error caught;
  if (!qa_try(close_components, view, &caught)) failed = true;
  if (!qa_try(close_q3, view, &caught)) failed = true;
  if (!qa_try(close_seat, view, &caught)) failed = true;
  if (!qa_try(cleanup_effects, view, &caught)) failed = true;
  if (!qa_try(cleanup_assets, view, &caught)) failed = true;
  if (failed) qa_raise("AggregateError", 0, 0, "Remote seat view retirement failed");
}
