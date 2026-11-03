/* Port of src/app/bootstrap/startup.ts: static open(), openGraphics() with the
 * StartupGraphics it builds (graphics.draw, graphics.captionCommands through the
 * client record, close()), graphics.menu.bindInput(...) as openGraphics and
 * activateFrontend call it, and the C destroy (TS GC). Async openGraphics is
 * synchronous; the TS catch blocks are qa_try. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "startup_application_internal.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/audio/output_settings.h"
#include "qa/app/bootstrap/audio/playlist_settings.h"
#include "qa/app/bootstrap/menu_art.h"
#include "qa/console/draw.h"
#include "qa/console/metrics.h"
#include "qa/foundation/js.h"
#include "qa/render/commands/frame.h"
#include "qa/text/draw2d.h"
#include "qa/text/layout.h"
#include "qa/ui/settings/console.h"
#include "qa/ui/settings/index.h"
#include "qa/ui/settings/input_routing.h"
#include "qa/ui/settings/services.h"
#include "qa/app/bootstrap/character_turntable.h"
#include "qa/app/bootstrap/input.h"
#include "qa/app/bootstrap/menu_font.h"
#include "qa/app/bootstrap/startup_timing.h"
#include "qa/content/mounts.h"
#include "qa/content/user_data.h"
#include "qa/foundation/support.h"
#include "qa/render/scene/resources.h"
#include "qa/ui/common/assets.h"

typedef struct graphics_state graphics_state;
static void graphics_state_free(graphics_state *state);
static void graphics_state_free_mounts(graphics_state *state);

/* ---- graphics.menu.bindInput(input, actions, { available, reset }) ------------- */
typedef struct menu_input_record {
  qa_startup_application *app;
  qa_seat_id seat;
  bool (*available)(void *ctx, qa_seat_id seat);
  void (*reset)(void *ctx, qa_seat_id seat);
  qa_ui_binding_reset binding_reset;
} menu_input_record;
/* () => sharedBindingActions(movementDialect(model.options), model.bindingItems(), model.bindingCapabilities()) */
static const qa_ui_binding_action *menu_actions(void *ctx, size_t *out_count) {
  menu_input_record *record = ctx;
  qa_startup_application *app = record->app;
  qa_app_options options;
  qa_app_startup_selection_options(app->model, &options);
  qa_command_dialect dialect = qa_app_movement_dialect(&options, NULL);
  qa_app_options_destroy(&options);
  size_t item_count = 0;
  qa_weapon_binding_item *items = qa_app_startup_selection_binding_items(app->model, &item_count);
  qa_ui_binding_actions actions = qa_ui_shared_binding_actions(dialect, items, item_count,
      qa_app_startup_selection_binding_capabilities(app->model));
  qa_weapon_binding_items_free(items, item_count);
  if (app->has_binding_actions) qa_ui_binding_actions_free(&app->binding_actions);
  app->binding_actions = actions;
  app->has_binding_actions = true;
  *out_count = actions.count;
  return actions.items;
}
static bool menu_reset_available(void *ctx) {
  menu_input_record *record = ctx;
  return record->available(record->app, record->seat);
}
static void menu_reset(void *ctx) {
  menu_input_record *record = ctx;
  record->reset(record->app, record->seat);
}
void qa_startup_graphics_bind_menu_input(qa_startup_application *app, qa_seat_input *input, qa_seat_id seat,
    bool (*available)(void *ctx, qa_seat_id seat), void (*reset)(void *ctx, qa_seat_id seat)) {
  menu_input_record *record = calloc(1, sizeof *record);
  if (record == NULL) qa_range_error("Out of memory");
  record->app = app;
  record->seat = seat;
  record->available = available;
  record->reset = reset;
  record->binding_reset.ctx = record;
  record->binding_reset.available = menu_reset_available;
  record->binding_reset.reset = menu_reset;
  qa_app_startup_menu_bind_input(app->graphics->menu, input, NULL, 0, menu_actions, record, &record->binding_reset);
  free(app->menu_input_binding_record);
  app->menu_input_binding_record = record;
}

/* ---- captureNextFrame() ------------------------------------------------------------ */
void qa_startup_application_capture_next_frame(qa_startup_application *app, qa_app_frame_capture_request request) {
  if (app->graphics == NULL) {
    request.reject(request.ctx, "Startup menu is not visible");
    return;
  }
  qa_app_native_renderer_capture_next_frame(app->graphics->renderer, request);
}

/* ---- graphics.close() ---------------------------------------------------------- */
void qa_startup_graphics_close(qa_startup_application *app) {
  qa_startup_graphics *graphics = app->graphics;
  if (graphics == NULL || graphics->closed) return;
  graphics->closed = true;
  if (graphics->menu != NULL) qa_app_startup_menu_close(graphics->menu);
  if (graphics->turntable != NULL) qa_app_character_turntable_destroy(graphics->turntable);
  graphics->turntable = NULL;
  if (graphics->controller_settings != NULL) qa_app_controller_settings_close(graphics->controller_settings);
  if (graphics->devices != NULL) qa_app_input_devices_close(graphics->devices);
  if (graphics->audio != NULL) qa_app_startup_audio_close(graphics->audio);
  if (graphics->theme_mounts != NULL) qa_content_mounted_close(graphics->theme_mounts);
  if (graphics->router != NULL) qa_input_router_close(graphics->router);
  if (graphics->controllers != NULL) qa_sdl_controllers_close(graphics->controllers);
  if (graphics->art != NULL) qa_native_ui_art_close(graphics->art);
  if (graphics->typography != NULL) qa_app_menu_typography_close(graphics->typography);
  graphics->typography = NULL;
  if (graphics->font != NULL) qa_app_menu_font_close(graphics->font);
  graphics->font = NULL;
  if (graphics->images != NULL) qa_scene_image_registry_close(graphics->images);
  if (graphics->renderer != NULL) qa_app_native_renderer_destroy(graphics->renderer);
  graphics->renderer = NULL;
  if (graphics->mounted != NULL) qa_content_mounted_close(graphics->mounted);
}
static void graphics_free(qa_startup_graphics *graphics) {
  if (graphics == NULL) return;
  if (graphics->menu != NULL) qa_app_startup_menu_destroy(graphics->menu);
  if (graphics->controller_settings != NULL) qa_app_controller_settings_destroy(graphics->controller_settings);
  for (size_t i = 0; i < graphics->retired_controller_setting_count; i++)
    qa_app_controller_settings_destroy(graphics->retired_controller_settings[i]);
  free(graphics->retired_controller_settings);
  if (graphics->router != NULL) qa_input_router_destroy(graphics->router);
  for (size_t i = 0; i < graphics->retired_router_count; i++) qa_input_router_destroy(graphics->retired_routers[i]);
  free(graphics->retired_routers);
  if (graphics->input_profile != NULL) qa_app_startup_input_profile_destroy(graphics->input_profile);
  if (graphics->devices != NULL) qa_app_input_devices_destroy(graphics->devices);
  if (graphics->audio != NULL) qa_app_startup_audio_destroy(graphics->audio);
  if (graphics->theme_mounts != NULL) qa_content_mounted_destroy(graphics->theme_mounts);
  if (graphics->controllers != NULL) free(graphics->controllers);
  free(graphics->art);
  if (graphics->images != NULL) { qa_scene_image_registry_destroy(graphics->images); free(graphics->images); }
  if (graphics->mounted != NULL) qa_content_mounted_destroy(graphics->mounted);
  graphics_state_free_mounts(graphics->draw_state);
  graphics_state_free(graphics->draw_state);
  free(graphics->display_renderer);
  free(graphics);
}

/* ---- static open(options, host, saveDirectory?, entry?) --------------------------- */
typedef struct browser_call { qa_startup_application *app; qa_app_startup_server_browser *browser; } browser_call;
static void browser_body(void *ctx) {
  browser_call *call = ctx;
  call->browser = qa_app_startup_server_browser_open(&call->app->browser_config);
  if (call->browser == NULL) qa_range_error("Out of memory");
}
static void open_graphics_body(void *ctx) {
  qa_startup_application *app = ctx;
  /* The browser is optional: a denied UDP bind must not stop single-player. */
  char *root = qa_startup_settings_directory(app->save_directory);
  bool stored = qa_config_store_init(&app->browser_config, root);
  free(root);
  if (!stored) qa_range_error("Out of memory");
  app->has_browser_config = true;
  browser_call call = {app, NULL};
  qa_caught_error caught;
  if (qa_try(browser_body, &call, &caught)) app->browser = call.browser;
  else qa_startup_printf(app, "Server browser unavailable: %s\n", caught.message);
  qa_app_startup_stamp("server-browser");
  qa_startup_open_graphics(app);
  qa_app_startup_stamp("openGraphics");
}
static void close_body(void *ctx) { qa_startup_application_close(ctx); }
qa_startup_application *qa_startup_application_open(const qa_app_options *options,
    qa_startup_application_host host, const char *save_directory, const char *entry) {
  qa_app_startup_selection_model *model = qa_app_create_startup_selection(options);
  if (model == NULL) qa_range_error("Out of memory");
  char *directory = save_directory != NULL ? qa_startup_dup(save_directory) : qa_user_data_directory("saves", options->user_content_root);
  if (directory == NULL) qa_range_error("Out of memory");
  qa_startup_application *app = qa_startup_application_create(model, host, directory, entry);
  free(directory);
  app->owns_model = true;
  qa_app_startup_stamp("selection+catalog");
  qa_caught_error caught;
  if (qa_try(open_graphics_body, app, &caught)) return app;
  qa_caught_error ignored;
  (void)qa_try(close_body, app, &ignored);
  qa_startup_application_destroy(app);
  qa_rethrow(&caught);
}

/* ---- destroy (TS GC) ------------------------------------------------------------- */
static void destroy_close_body(void *ctx) { qa_startup_application_close(ctx); }
void qa_startup_application_destroy(qa_startup_application *app) {
  if (app == NULL) return;
  if (!app->closed) {
    qa_caught_error caught;
    (void)qa_try(destroy_close_body, app, &caught);
  }
  for (size_t i = 0; i < app->game_count; i++) qa_application_destroy(app->games[i]);
  free(app->games);
  for (size_t i = 0; i < app->remote_count; i++) qa_remote_application_destroy(app->remotes[i]);
  free(app->remotes);
  qa_startup_launches_free(app);
  qa_startup_libraries_free(app);
  if (app->recording != NULL) qa_app_client_demo_recording_destroy(app->recording);
  if (app->demos != NULL) qa_app_client_demo_commands_destroy(app->demos);
  free(app->registered_commands);
  for (size_t i = 0; i < app->demo_completion_count; i++) free(app->demo_completions[i]);
  free(app->demo_completions);
  for (size_t i = 0; i < app->retired_routing_count; i++) qa_startup_frontend_routing_free(app->retired_routings[i]);
  free(app->retired_routings);
  for (size_t i = 0; i < app->routing_owner_count; i++) qa_app_console_routing_destroy(app->routing_owners[i]);
  free(app->routing_owners);
  for (size_t i = 0; i < app->owned_console_count; i++) { qa_seat_console_destroy(app->owned_consoles[i]); free(app->owned_consoles[i]); }
  free(app->owned_consoles);
  for (size_t i = 0; i < app->owned_registry_count; i++) qa_cvar_registry_destroy(app->owned_registries[i]);
  free(app->owned_registries);
  free(app->release_source_commands);
  if (app->client != NULL || app->client_storage.locals != NULL || app->client_storage.consoles != NULL) {
    free(app->client_storage.locals);
    free(app->client_storage.consoles);
  }
  if (app->client_storage.capture != NULL) qa_app_capture_destroy(app->client_storage.capture);
  if (app->client_storage.video_restart != NULL) qa_app_video_restart_destroy(app->client_storage.video_restart);
  graphics_free(app->graphics);
  app->graphics = NULL;
  app->graphics_local = NULL;
  /* openGraphics' prepared configuration: the initial owners, its scripts and content (TS GC). */
  if (app->initial != NULL) qa_app_prepared_initial_destroy(app->initial);
  if (app->scripts != NULL) qa_app_console_script_files_destroy(app->scripts);
  if (app->configuration != NULL) qa_app_configuration_content_destroy(app->configuration);
  qa_app_cvar_archive_entries_free(app->source_archive, app->source_archive_count);
  if (app->has_graphics_options) qa_app_options_destroy(&app->graphics_options);
  if (app->browser != NULL) qa_app_startup_server_browser_destroy(app->browser);
  if (app->has_browser_config) qa_config_store_destroy(&app->browser_config);
  if (app->has_binding_actions) qa_ui_binding_actions_free(&app->binding_actions);
  free(app->menu_input_binding_record);
  if (app->preference_store != NULL) qa_config_store_destroy(&app->preference_store_storage);
  if (app->has_settings) qa_config_store_destroy(&app->settings_storage);
  qa_startup_clear_pending_all(app);
  if (app->lobby_selection != NULL) { qa_app_startup_launch_free(app->lobby_selection); free(app->lobby_selection); }
  qa_app_application_local_lobby_destroy(&app->lobby);
  if (app->owned_lobby_service != NULL) qa_local_lobby_service_free(app->owned_lobby_service);
  if (app->accounts != NULL) qa_app_local_accounts_destroy(app->accounts);
  if (app->owns_match_rankings) qa_app_match_rankings_destroy(app->match_rankings);
  qa_app_addon_library_destroy(app->addons);
  qa_app_frontend_preferences_destroy(app->preferences);
  qa_app_startup_saves_destroy(app->saves);
  for (size_t i = 0; i < app->external_directory_count; i++) free(app->external_directories[i]);
  free(app->external_directories);
  qa_app_keys_destroy(app->keys);
  if (app->session != NULL) qa_engine_session_destroy(app->session);
  if (app->has_identity) qa_identity_owner_destroy(&app->identity);
  free(app->status); free(app->baseline_product); free(app->baseline_input); free(app->frontend_routing_baseline);
  free(app->recording_root); free(app->gtv_label); free(app->active_demo_name); free(app->clipboard_text);
  free(app->save_directory);
  if (app->owns_model) qa_app_startup_selection_model_destroy(app->model);
  free(app);
}

/* ---- openGraphics() state ---------------------------------------------------------
 * Everything openGraphics creates that TS keeps in closures (the SceneFrameBuilder,
 * the console SeatTextPresentation, the presentation selection, SeatUiPreferences,
 * the setting binding owners, the stores the device and controller owners borrow,
 * the startup font/theme mount plans and the menu art pixels). */
typedef struct graphics_state {
  qa_startup_application *app;
  qa_scene_frame_builder builder; bool has_builder;
  qa_image_resource_operation *drained;  /* the last drained operations (the frame borrows them) */
  qa_text_font_selection font;           /* activeFont.font */
  qa_seat_text_presentation console_text;
  char *provider_name;                   /* `${family}:official` */
  qa_presentation_selection presentation;
  qa_ui_seat_preferences accessibility;
  qa_ui_application_local_lobby lobby_view;
  qa_ui_audio_output_settings output;
  qa_ui_renderer_settings_options renderer_settings;
  qa_ui_setting_bindings *owners; size_t owner_count;
  qa_ui_setting_binding *settings; size_t setting_count;
  char **output_names; size_t output_name_count;
  qa_controller_device *devices; size_t device_count;
  qa_controller_device *router_devices; size_t router_device_count;
  qa_config_store devices_store; bool has_devices_store;
  qa_config_store controller_store; bool has_controller_store;
  qa_scene_image_policy_owned policy; bool has_policy;
  qa_catalog_mount_list mounts; bool has_mounts; qa_mount_id *order; char *plan_id;
  qa_catalog_mount_list theme_list; bool has_theme_list; qa_mount_id *theme_order; char *theme_plan_id;
  void *art_pixels[4]; size_t art_pixel_count;
  char *capture_root;
  char message[sizeof ((qa_caught_error *)0)->message];
} graphics_state;

static void *xcalloc(size_t count, size_t size) {
  void *p = calloc(count == 0 ? 1 : count, size);
  if (p == NULL) qa_range_error("Out of memory");
  return p;
}
static bool same(const char *a, const char *b) { return a != NULL && b != NULL && strcmp(a, b) == 0; }
static void print_host(qa_startup_application *app, const char *text) {
  if (app->host.base.print != NULL) app->host.base.print(app->host.base.ctx, text);
}
static void host_print_fn(void *ctx, const char *text) { print_host(ctx, text); }
static void set_menu_status(qa_startup_application *app, const char *text) {
  if (app->graphics != NULL && app->graphics->menu != NULL) qa_app_startup_menu_set_status(app->graphics->menu, text, false);
}
static const char *raise_message(graphics_state *state, const qa_caught_error *caught) {
  snprintf(state->message, sizeof state->message, "%s", caught->message);
  return state->message;
}
static qa_app_client_bootstrap_seat *client_primary(qa_startup_application *app) {
  return app->client != NULL && app->client->local_count > 0 ? &app->client->locals[0] : NULL;
}
static qa_seat_console *client_console_of(qa_app_client_bootstrap *client, const qa_session_seat *seat) {
  for (size_t i = 0; i < client->console_count; i++) if (client->consoles[i].seat == seat) return client->consoles[i].console;
  return NULL;
}
static qa_seat_input *prepared_input(qa_app_prepared_startup *prepared, qa_seat_id id) {
  size_t count = 0;
  const qa_app_prepared_seat *seats = qa_app_prepared_startup_seats(prepared, &count);
  for (size_t i = 0; i < count; i++) if (qa_same_seat(seats[i].id, id)) return seats[i].input;
  return NULL;
}
static void own_bindings(graphics_state *state, qa_ui_setting_bindings bindings) {
  qa_ui_setting_bindings *grown = realloc(state->owners, (state->owner_count + 1) * sizeof *grown);
  if (grown == NULL) qa_range_error("Out of memory");
  state->owners = grown;
  state->owners[state->owner_count++] = bindings;
  qa_ui_setting_binding *items = realloc(state->settings, (state->setting_count + bindings.count + 1) * sizeof *items);
  if (items == NULL) qa_range_error("Out of memory");
  state->settings = items;
  if (bindings.count != 0) memcpy(state->settings + state->setting_count, bindings.items, bindings.count * sizeof *items);
  state->setting_count += bindings.count;
}
static void graphics_state_free(graphics_state *state) {
  if (state == NULL) return;
  if (state->has_builder) qa_scene_frame_builder_destroy(&state->builder);
  free(state->drained);
  free(state->provider_name);
  for (size_t i = 0; i < state->owner_count; i++) qa_ui_setting_bindings_free(&state->owners[i]);
  free(state->owners);
  free(state->settings);
  for (size_t i = 0; i < state->output_name_count; i++) free(state->output_names[i]);
  free(state->output_names);
  qa_controller_devices_free(state->devices, state->device_count);
  qa_controller_devices_free(state->router_devices, state->router_device_count);
  if (state->has_devices_store) qa_config_store_destroy(&state->devices_store);
  if (state->has_controller_store) qa_config_store_destroy(&state->controller_store);
  if (state->has_policy) qa_scene_image_policy_owned_destroy(&state->policy);
  for (size_t i = 0; i < state->art_pixel_count; i++) free(state->art_pixels[i]);
  /* SeatUiPreferences binding contexts (as ui.c releases its seat preferences). */
  for (size_t i = 0; i < QA_UI_PREFERENCE_BINDING_COUNT; i++) free(state->accessibility.binding_ctx[i]);
  free(state->capture_root);
  free(state);
}
/* The mount lists and plan ids outlive their mounted plans (graphics_free order). */
static void graphics_state_free_mounts(graphics_state *state) {
  if (state == NULL) return;
  if (state->has_mounts) qa_content_catalog_mount_list_free(&state->mounts);
  state->has_mounts = false;
  free(state->order); free(state->plan_id);
  state->order = NULL; state->plan_id = NULL;
  if (state->has_theme_list) qa_content_catalog_mount_list_free(&state->theme_list);
  state->has_theme_list = false;
  free(state->theme_order); free(state->theme_plan_id);
  state->theme_order = NULL; state->theme_plan_id = NULL;
}

/* openMountPlan({ id: createMountPlanId("startup", revision), mounts, defaultOrder, prefixOrders: [] }). */
static qa_content_mounted *open_startup_mounts(qa_content_catalog *catalog, const char *product, const char *revision,
    qa_catalog_mount_list *list, bool *has_list, qa_mount_id **order, char **plan_id) {
  if (!qa_content_catalog_mounts_for(catalog, product, list)) qa_range_error("Out of memory");
  *has_list = true;
  *order = xcalloc(list->count + 1, sizeof **order);
  for (size_t i = 0; i < list->count; i++)
    (*order)[i] = list->mounts[i].kind == QA_CONTENT_MOUNT_ARCHIVE ? list->mounts[i].as.archive.identity.id
                                                                 : list->mounts[i].as.loose.identity.id;
  *plan_id = qa_create_mount_plan_id("startup", revision);
  if (*plan_id == NULL) qa_range_error("Out of memory");
  qa_resolved_mount_plan plan;
  memset(&plan, 0, sizeof plan);
  plan.id = *plan_id;
  plan.mounts = list->mounts; plan.mount_count = list->count;
  plan.default_order = *order; plan.default_order_count = list->count;
  qa_content_mounted *mounted = qa_open_mount_plan(&plan, NULL);
  if (mounted == NULL) qa_range_error("Out of memory");
  return mounted;
}

/* ---- StartupMenu option closures (ctx is the application) ------------------------ */
static qa_startup_graphics *local_graphics(qa_startup_application *app) { return app->graphics_local; }
static graphics_state *state_of(qa_startup_application *app) { return app->graphics_local != NULL ? app->graphics_local->draw_state : NULL; }
static double menu_now(void *ctx) { (void)ctx; return qa_startup_now(); }
/* lobby: { current: () => this.lobby, seats: () => this.model.options.seats, selection } */
static const qa_lobby *lobby_view_current(void *ctx) { return qa_app_application_local_lobby_current(&((qa_startup_application *)ctx)->lobby); }
static const qa_lobby *const *lobby_view_list(void *ctx, size_t *out_count) {
  const qa_lobby **rows = NULL;
  *out_count = qa_app_application_local_lobby_list(&((qa_startup_application *)ctx)->lobby, &rows);
  return rows;
}
static const char *lobby_view_host(void *ctx, const char *name, double capacity, qa_ui_local_lobby_selection selection, double seats) {
  qa_app_local_lobby_selection chosen = {selection.composition};
  return qa_app_application_local_lobby_host(&((qa_startup_application *)ctx)->lobby, name, capacity, chosen, seats);
}
static const char *lobby_view_join(void *ctx, const char *id, double seats) {
  return qa_app_application_local_lobby_join(&((qa_startup_application *)ctx)->lobby, id, seats);
}
static const char *lobby_view_ready(void *ctx, bool value) { return qa_app_application_local_lobby_ready(&((qa_startup_application *)ctx)->lobby, value); }
static const char *lobby_view_start(void *ctx) { return qa_app_application_local_lobby_start(&((qa_startup_application *)ctx)->lobby); }
static const char *lobby_view_leave(void *ctx) { return qa_app_application_local_lobby_leave(&((qa_startup_application *)ctx)->lobby); }
static const qa_ui_application_local_lobby *menu_lobby_current(void *ctx) {
  graphics_state *state = state_of(ctx);
  return state != NULL ? &state->lobby_view : NULL;
}
static double menu_lobby_seats(void *ctx) {
  qa_app_options options;
  qa_app_startup_selection_options(((qa_startup_application *)ctx)->model, &options);
  double seats = options.seats;
  qa_app_options_destroy(&options);
  return seats;
}
/* selection: async () => { selected = await model.resolve(); hosting connection check; content =
 * await loadApplicationContent(...); composition = await buildUnifiedComposition(content); ... }. */
typedef struct lobby_selection_call { qa_startup_application *app; qa_ui_local_lobby_selection *out; } lobby_selection_call;
static void lobby_selection_body(void *ctx) {
  lobby_selection_call *call = ctx;
  qa_app_startup_launch selected;
  memset(&selected, 0, sizeof selected);
  qa_app_startup_selection_resolve(call->app->model, &selected);
  qa_cleanup guard;
  qa_cleanup_push(&guard, (void (*)(void *))qa_app_startup_launch_free, &selected);
  const char *kind = selected.options.network_kind;
  if (!same(kind, "native-server") && !same(kind, "q2-server") && !same(kind, "unified-server"))
    qa_raise("Error", 0, 0, "Choose a hosting connection in Network settings before creating a lobby");
  /* buildUnifiedComposition(content) has no C producer of a qa_composition_identity (the session
   * composition the lobby offers); see docs/PORT_COMPLETION.md ("Startup top layer"). */
  qa_raise("Error", 0, 0, "Lobby composition requires buildUnifiedComposition, which has no C producer");
  qa_cleanup_pop(&guard);
  qa_app_startup_launch_free(&selected);
}
static const char *menu_lobby_selection(void *ctx, qa_ui_local_lobby_selection *out) {
  qa_startup_application *app = ctx;
  lobby_selection_call call = {app, out};
  qa_caught_error caught;
  if (qa_try(lobby_selection_body, &call, &caught)) return NULL;
  graphics_state *state = state_of(app);
  return state != NULL ? raise_message(state, &caught) : "Lobby selection failed";
}
static qa_ui_preference_values menu_appearance(void *ctx) {
  graphics_state *state = state_of(ctx);
  return qa_ui_seat_preferences_values(&state->accessibility);
}
/* sound: sound => { setVolumes(preferences.audioValues); activeAudio.sound(sound) } */
static void menu_sound(void *ctx, qa_ui_sound sound) {
  qa_startup_application *app = ctx;
  double effects = 0, music = 0;
  qa_app_frontend_preferences_audio_values(app->preferences, &effects, &music);
  qa_app_startup_audio_set_volumes(local_graphics(app)->audio, effects, music);
  qa_app_startup_audio_sound(local_graphics(app)->audio, sound);
}
static const char *menu_clipboard(void *ctx) { return qa_startup_clipboard(ctx); }
static void menu_connect(void *ctx, qa_app_browser_connection connection) {
  qa_app_startup_action *action = qa_startup_stage(ctx, QA_APP_STARTUP_ACTION_CONNECT);
  action->connection = connection;
  action->connection.remote = qa_startup_dup(connection.remote);
}
static void menu_play_preset(void *ctx, const char *id, double skill, const char *arena_map) {
  qa_app_startup_action *action = qa_startup_stage(ctx, QA_APP_STARTUP_ACTION_PRESET);
  action->preset_id = qa_startup_dup(id);
  action->skill = skill;
  action->arena_map = arena_map != NULL ? qa_startup_dup(arena_map) : NULL;
}
static void menu_play(void *ctx) { (void)qa_startup_stage(ctx, QA_APP_STARTUP_ACTION_PLAY); }
typedef struct load_call { qa_startup_application *app; const char *id; const char *path; } load_call;
static void load_body(void *ctx) {
  load_call *call = ctx;
  call->path = qa_app_startup_saves_path(call->app->saves, call->id);
}
static void menu_load(void *ctx, const char *id) {
  qa_startup_application *app = ctx;
  load_call call = {app, id, NULL};
  qa_caught_error caught;
  if (qa_try(load_body, &call, &caught)) {
    qa_app_startup_action *action = qa_startup_stage(app, QA_APP_STARTUP_ACTION_LOAD);
    action->path = qa_startup_dup(call.path);
  } else {
    qa_startup_set_text(&app->status, caught.message);
    set_menu_status(app, app->status);
  }
}
static void menu_quit(void *ctx) { qa_startup_application_request_quit(ctx); }
static const qa_app_startup_save_list *menu_saves(void *ctx) { return qa_app_startup_saves_list(((qa_startup_application *)ctx)->saves); }
static void menu_refresh_saves(void *ctx) { ((qa_startup_application *)ctx)->refresh_saves = true; }

/* ---- settings closures ------------------------------------------------------------- */
/* currentAudio = () => this.client?.output.current ?? activeAudio.engine */
static qa_unified_audio *current_audio(qa_startup_application *app) {
  if (app->client != NULL && app->client->output_current != NULL) return app->client->output_current;
  return qa_app_startup_audio_engine(local_graphics(app)->audio);
}
static const char *output_selected(void *ctx) { return qa_unified_audio_selected_output(current_audio(ctx)); }
static const char *const *output_devices(void *ctx, size_t *out_count) {
  qa_startup_application *app = ctx;
  graphics_state *state = state_of(app);
  for (size_t i = 0; i < state->output_name_count; i++) free(state->output_names[i]);
  free(state->output_names);
  state->output_names = qa_unified_audio_output_device_names(current_audio(app), &state->output_name_count);
  if (state->output_names == NULL) state->output_name_count = 0;
  *out_count = state->output_name_count;
  return (const char *const *)state->output_names;
}
typedef struct output_call { qa_startup_application *app; const char *name; bool has_format; qa_audio_output_format format; } output_call;
static void output_select_body(void *ctx) {
  output_call *call = ctx;
  qa_unified_audio *output = current_audio(call->app);
  if (!call->has_format) (void)qa_unified_audio_select_output(output, call->name, NULL, false);
  else {
    (void)qa_unified_audio_select_output(output, qa_unified_audio_selected_output(output), &call->format, false);
    qa_app_write_audio_output_cvars(qa_app_image_settings_cvars(local_graphics(call->app)->image_settings), qa_unified_audio_output_format(output));
  }
  set_menu_status(call->app, "");
}
/* select: name => { currentAudio().selectOutput(name); this.graphics?.menu.setStatus("") } */
static const char *output_select(void *ctx, const char *name) {
  output_call call = {ctx, name, false, {0}};
  qa_caught_error caught;
  return qa_try(output_select_body, &call, &caught) ? NULL : raise_message(state_of(ctx), &caught);
}
static qa_audio_output_format output_format_read(void *ctx) { return qa_unified_audio_output_format(current_audio(ctx)); }
static const char *output_format_select(void *ctx, qa_audio_output_format format) {
  output_call call = {ctx, NULL, true, format};
  qa_caught_error caught;
  return qa_try(output_select_body, &call, &caught) ? NULL : raise_message(state_of(ctx), &caught);
}
static void report_status(void *ctx, const char *message) { set_menu_status(ctx, message); }
/* () => readMusicSettings(imageSettings.cvars) */
static qa_app_audio_music_preferences music_settings(void *ctx) {
  qa_startup_application *app = ctx;
  return qa_app_read_music_settings(qa_app_image_settings_cvars(local_graphics(app)->image_settings));
}
/* bindInputRoutingSettings(() => platform router, () => devices) */
static qa_input_router *routing_router(void *ctx) {
  qa_startup_application *app = ctx;
  qa_app_client_bootstrap *client = app->client;
  if (client != NULL && client->platform.kind == QA_APP_CLIENT_INPUT_WORLD) return qa_app_application_input_router(client->platform.input);
  if (client != NULL && client->platform.kind == QA_APP_CLIENT_INPUT_MENU && client->platform.router != NULL) return client->platform.router;
  if (local_graphics(app)->router == NULL) qa_raise("Error", 0, 0, "Input routing is not published");
  return local_graphics(app)->router;
}
static const qa_controller_device *routing_devices(void *ctx, size_t *out_count) {
  qa_startup_application *app = ctx;
  graphics_state *state = state_of(app);
  qa_controller_devices_free(state->router_devices, state->router_device_count);
  state->router_devices = NULL; state->router_device_count = 0;
  qa_sdl_controllers *controllers = app->client != NULL ? app->client->controllers : local_graphics(app)->controllers;
  if (controllers != NULL) state->router_devices = qa_sdl_controllers_devices(controllers, &state->router_device_count);
  if (state->router_devices == NULL) state->router_device_count = 0;
  *out_count = state->router_device_count;
  return state->router_devices;
}
/* bindGamepadSettings(() => this.client?.prepared.seats[0]?.input ?? primary.input) */
static qa_seat_input *gamepad_input(void *ctx) {
  qa_startup_application *app = ctx;
  qa_app_prepared_startup *prepared = app->client != NULL ? app->client->prepared : qa_app_prepared_initial_startup(app->initial);
  size_t count = 0;
  const qa_app_prepared_seat *seats = qa_app_prepared_startup_seats(prepared, &count);
  if (count > 0) return seats[0].input;
  const qa_app_prepared_seat *initial = qa_app_prepared_startup_seats(qa_app_prepared_initial_startup(app->initial), &count);
  return count > 0 ? initial[0].input : NULL;
}
static const char *const *music_tracks(void *ctx, size_t *out_count) {
  return qa_app_startup_audio_music_tracks(local_graphics(ctx)->audio, out_count);
}
static qa_sdl_window *native_window(void *ctx) { return qa_app_native_renderer_window(local_graphics(ctx)->renderer); }
static void apply_mutation(void *ctx) { qa_ui_apply_window_mutation(ctx); }
static void mutate_window(void *ctx, const qa_ui_window_mutation *mutation) {
  qa_app_native_renderer_mutate_window(local_graphics(ctx)->renderer, apply_mutation, (void *)mutation);
}
static const char *renderer_current(void *ctx) {
  return qa_sdl_window_backend(native_window(ctx)) == QA_SDL_BACKEND_GL ? "gl" : "cpu";
}
static bool renderer_worker_read(void *ctx) {
  return qa_cvar_registry_variable_value(qa_app_image_settings_cvars(local_graphics(ctx)->image_settings), "r_smp") != 0;
}
static const char *renderer_worker_write(void *ctx, bool value) {
  qa_cvar_snapshot snapshot;
  if (qa_cvar_registry_set(qa_app_image_settings_cvars(local_graphics(ctx)->image_settings), "r_smp",
          value ? "1" : "0", false, &snapshot))
    qa_cvar_read_free(&snapshot);
  return NULL;
}
static bool renderer_enabled(void *ctx) {
  qa_startup_application *app = ctx;
  return app->graphics != NULL && !qa_app_startup_menu_is_busy(app->graphics->menu);
}
typedef struct apply_call { qa_startup_application *app; const char *backend; } apply_call;
static void renderer_apply_body(void *ctx) {
  apply_call *call = ctx;
  qa_app_client_bootstrap *client = call->app->client;
  if (client == NULL) qa_raise("Error", 0, 0, "Client console output has no published owner");
  if (client->local_count == 0) qa_raise("Error", 0, 0, "Renderer selection requires the retained primary seat");
  qa_app_client_bootstrap_seat *primary = &client->locals[0];
  char text[64];
  snprintf(text, sizeof text, "vid_restart %s\n", call->backend);
  qa_command_context context;
  memset(&context, 0, sizeof context);
  context.session = qa_engine_session_id(client->session);
  context.origin.kind = QA_COMMAND_ORIGIN_LOCAL_SEAT;
  context.origin.seat = qa_session_seat_id(primary->seat);
  context.origin.client = qa_session_client_id(primary->client);
  qa_command_buffer_append(qa_app_prepared_startup_commands(client->prepared), text, &context, -1);
}
static const char *renderer_apply(void *ctx, const char *backend) {
  apply_call call = {ctx, backend};
  qa_caught_error caught;
  return qa_try(renderer_apply_body, &call, &caught) ? NULL : raise_message(state_of(ctx), &caught);
}

/* ---- InputRouter / ControllerSettings closures ---------------------------------- */
static double router_now(void *ctx) { (void)ctx; return qa_startup_now(); }
static double router_ticks(void *ctx) { return qa_sdl_window_ticks(native_window(ctx)); }
static void router_unhandled(void *ctx, const qa_input_unhandled_event *event) {
  if (event->is_controller || event->sdl == NULL) return;
  if (event->sdl->kind == QA_SDL_EVENT_QUIT || (event->sdl->kind == QA_SDL_EVENT_WINDOW && event->sdl->as.window.event == 14))
    qa_startup_application_request_quit(ctx);
}
/* () => controllers?.devices ?? [] */
static qa_controller_device *controller_devices(void *ctx, size_t *out_count) {
  qa_startup_application *app = ctx;
  *out_count = 0;
  if (local_graphics(app)->controllers == NULL) return NULL;
  return qa_sdl_controllers_devices(local_graphics(app)->controllers, out_count);
}
static qa_controller_selection controller_selection(const qa_settings_controller_selection *saved) {
  qa_controller_selection selection;
  memset(&selection, 0, sizeof selection);
  selection.kind = saved->kind == QA_CONTROLLER_NONE ? QA_CONTROLLER_SELECTION_NONE
      : saved->kind == QA_CONTROLLER_DEVICE ? QA_CONTROLLER_SELECTION_DEVICE
      : saved->kind == QA_CONTROLLER_SERIAL ? QA_CONTROLLER_SELECTION_SERIAL : QA_CONTROLLER_SELECTION_AUTOMATIC;
  memcpy(selection.guid, saved->guid, sizeof selection.guid);
  selection.ordinal = saved->ordinal;
  selection.serial = saved->serial;
  return selection;
}

/* ---- ApplicationCapture services ------------------------------------------------- */
static qa_app_client_bootstrap *capture_client(qa_startup_application *app) {
  if (app->client == NULL) qa_raise("Error", 0, 0, "Client console output has no published owner");
  return app->client;
}
/* captureSeats(): the world's locals, else the retained primary (seat, prepared input). */
static bool capture_seat(qa_startup_application *app, size_t index, qa_seat_id *out_seat, qa_seat_input **out_input,
    qa_seat_console **out_console) {
  qa_app_client_bootstrap *client = capture_client(app);
  if (client->platform.kind == QA_APP_CLIENT_INPUT_WORLD) {
    if (index >= qa_app_application_input_local_count(client->platform.input)) return false;
    qa_app_local_input *local = qa_app_application_input_local(client->platform.input, index);
    *out_seat = qa_session_seat_id(local->player.seat);
    *out_input = local->input;
    *out_console = client_console_of(client, local->player.seat);
    return true;
  }
  if (index != 0 || client->local_count == 0) return false;
  *out_seat = qa_session_seat_id(client->locals[0].seat);
  *out_input = prepared_input(client->prepared, client->locals[0].prepared);
  *out_console = client_console_of(client, client->locals[0].seat);
  return true;
}
static qa_seat_console *capture_console(void *ctx, qa_seat_id seat) {
  qa_seat_id id; qa_seat_input *input; qa_seat_console *console;
  for (size_t i = 0; capture_seat(ctx, i, &id, &input, &console); i++) if (qa_same_seat(id, seat)) return console;
  return NULL;
}
static qa_seat_input *capture_input(void *ctx, qa_seat_id seat) {
  qa_seat_id id; qa_seat_input *input; qa_seat_console *console;
  for (size_t i = 0; capture_seat(ctx, i, &id, &input, &console); i++) if (qa_same_seat(id, seat)) return input;
  return NULL;
}
static bool capture_can_chat(void *ctx) {
  qa_app_client_bootstrap *client = capture_client(ctx);
  return client->platform.kind == QA_APP_CLIENT_INPUT_WORLD
      && qa_app_application_input_binding_capabilities(client->platform.input).chat;
}
static const char *capture_root(void *ctx) {
  qa_startup_application *app = ctx;
  qa_app_client_bootstrap *client = capture_client(app);
  graphics_state *state = state_of(app);
  free(state->capture_root);
  state->capture_root = qa_app_application_capture_root(client->configuration_current.options != NULL
      ? client->configuration_current.options->user_content_root : NULL);
  if (state->capture_root == NULL) qa_range_error("Out of memory");
  return state->capture_root;
}
static const char *capture_map_name(void *ctx) { return qa_app_client_source_capture_map(capture_client(ctx)->source_current); }
static const char *capture_write(void *ctx, const char *(*operation)(void *op_ctx), void *op_ctx) {
  return qa_app_console_script_files_write(capture_client(ctx)->configuration_current.scripts, operation, op_ctx);
}
static void capture_print(void *ctx, const char *text) {
  qa_startup_application *app = ctx;
  (void)capture_client(app);
  print_host(app, text);
  qa_seat_id id; qa_seat_input *input; qa_seat_console *console;
  if (capture_seat(app, 0, &id, &input, &console) && console != NULL) qa_seat_console_print(console, text);
}
static qa_configuration_write_notify capture_write_started(void *ctx, qa_command_context source, const char *path) {
  return qa_startup_configuration_write_started_notify(ctx, source, path);
}

/* ---- ApplicationVideoRestart host ------------------------------------------------- */
static qa_app_capture *video_capture(void *ctx) { return ((qa_startup_application *)ctx)->client_storage.capture; }
static bool video_render_worker(void *ctx, bool *out) {
  *out = renderer_worker_read(ctx);
  return true;
}
/* prepare: async () => (await this.client?.source.current?.prepareVideoRestart()) ?? null */
static bool video_prepare(void *ctx, qa_app_prepared_video_presentation *out) {
  qa_startup_application *app = ctx;
  if (app->client == NULL) return false;
  const void *prepared = NULL;
  const char *error = qa_app_client_source_prepare_video_restart(app->client->source_current, &prepared);
  if (error != NULL) qa_raise("Error", 0, 0, "%s", error);
  if (prepared == NULL) return false;
  *out = *(const qa_app_prepared_video_presentation *)prepared;
  return true;
}
typedef struct attach_call { qa_startup_application *app; qa_sdl_window *window; } attach_call;
static void attach_body(void *ctx) {
  attach_call *call = ctx;
  qa_input_router_attach_window(call->app->graphics->router, qa_input_window_from_sdl(call->window));
}
static void video_publish_window(void *ctx, qa_sdl_window *window) {
  qa_startup_application *app = ctx;
  qa_app_client_bootstrap *client = capture_client(app);
  if (client->platform.kind == QA_APP_CLIENT_INPUT_WORLD) {
    qa_app_application_input_publish_window(client->platform.input, qa_app_input_window_from_sdl(window));
    return;
  }
  if (app->graphics == NULL) return;
  qa_sdl_window *previous = qa_app_native_renderer_window(app->graphics->renderer);
  attach_call call = {app, window};
  qa_caught_error error;
  if (qa_try(attach_body, &call, &error)) return;
  attach_call rollback = {app, previous};
  qa_caught_error rollback_error;
  if (!qa_try(attach_body, &rollback, &rollback_error))
    qa_raise("AggregateError", 2, 0, "Video input rollback failed: %s; %s", error.message, rollback_error.message);
  qa_rethrow(&error);
}
typedef struct dump_call { qa_startup_application *app; const char *text; } dump_call;
static void dump_body(void *ctx) {
  dump_call *call = ctx;
  if (!qa_config_store_dump(call->app->preference_store, "renderer", call->text)) qa_raise("Error", 0, 0, "Could not write renderer");
}
static void video_published(void *ctx, const char *kind) {
  qa_startup_application *app = ctx;
  qa_app_startup_selection_select(app->model, "renderer", kind);
  if (app->preference_store == NULL) return;
  char text[16];
  snprintf(text, sizeof text, "%s\n", kind);
  dump_call call = {app, text};
  qa_caught_error caught;
  if (!qa_try(dump_body, &call, &caught)) qa_startup_printf(app, "Renderer preference could not be saved: %s\n", caught.message);
}
static void video_settled(void *ctx) { ((qa_startup_application *)ctx)->last_frame = qa_startup_now(); }
static void video_print(void *ctx, const char *text, const qa_command_context *source) {
  qa_startup_application *app = ctx;
  qa_app_client_bootstrap *client = app->client;
  if (client != NULL && client->platform.kind == QA_APP_CLIENT_INPUT_WORLD) {
    qa_app_application_input_print(client->platform.input, text, source);
    return;
  }
  qa_startup_application_print(app, text);
  /* text.trim() */
  size_t start = 0, end = strlen(text);
  while (start < end && isspace((unsigned char)text[start])) start++;
  while (end > start && isspace((unsigned char)text[end - 1])) end--;
  char *trimmed = malloc(end - start + 1);
  if (trimmed == NULL) qa_range_error("Out of memory");
  memcpy(trimmed, text + start, end - start);
  trimmed[end - start] = '\0';
  qa_cleanup guard;
  qa_cleanup_push(&guard, free, trimmed);
  set_menu_status(app, trimmed);
  qa_cleanup_pop(&guard);
  free(trimmed);
}
static void close_application_body(void *ctx) { qa_startup_application_close(ctx); }
static void video_failed(void *ctx, const qa_caught_error *error) {
  qa_startup_application *app = ctx;
  app->stopping = true;
  qa_caught_error cleanup;
  if (!qa_try(close_application_body, app, &cleanup))
    qa_raise("AggregateError", 2, 0, "Video restart and client shutdown failed: %s; %s", error->message, cleanup.message);
  qa_rethrow(error);
}

/* ---- the client record (TS this.client) ------------------------------------------ */
static const char *client_stop_recording(void *ctx, const qa_app_client_source_lifetime *source) {
  qa_startup_application *app = ctx;
  qa_app_client_demo_recording *recording = app->recording;
  const char *errors[2] = {NULL, NULL};
  if (app->recording_owner == source) errors[0] = qa_app_client_demo_recording_stop(recording);
  if (app->server_recording_owner != NULL && qa_startup_source_is_game(source, app->server_recording_owner))
    errors[1] = qa_app_client_demo_recording_stop_server(recording);
  if (app->recording_owner == source) app->recording_owner = NULL;
  if (app->recording_preparation.active && app->recording_preparation.source == source)
    memset(&app->recording_preparation, 0, sizeof app->recording_preparation);
  if (errors[0] == NULL && errors[1] == NULL) return NULL;
  graphics_state *state = state_of(app);
  snprintf(state->message, sizeof state->message, "Recording retirement failed: %s", errors[0] != NULL ? errors[0] : errors[1]);
  return state->message;
}
static bool client_has_pending_source(void *ctx) {
  qa_startup_application *app = ctx;
  return app->has_pending || app->lobby_transition || app->has_pending_demo || app->has_pending_recording || app->has_pending_movie;
}
static bool client_route_command(void *ctx, const char *name, const char *const *args, size_t count, const qa_command_context *source) {
  return qa_startup_application_route_command(ctx, name, args, count, *source, NULL, true);
}
static void client_dispatch(void *ctx, const qa_app_configuration_command_request *request) {
  qa_startup_dispatch_application_request(ctx, request);
}
static void client_activate_frontend(void *ctx, const qa_app_client_frontend_configuration *configuration) {
  qa_startup_activate_frontend(ctx, configuration);
}
/* graphics.captionCommands(captions, viewport, timeMilliseconds) */
static qa_ui_draw_context draw_context(qa_startup_application *app, qa_rect viewport, double time) {
  qa_startup_graphics *graphics = app->graphics;
  graphics_state *state = graphics->draw_state;
  qa_app_client_bootstrap_seat *primary = client_primary(app);
  qa_ui_draw_context context;
  memset(&context, 0, sizeof context);
  context.binding.seat = primary != NULL ? qa_session_seat_id(primary->seat) : graphics->seat;
  context.binding.client = primary != NULL ? qa_session_client_id(primary->client) : graphics->client;
  context.binding.viewport = viewport;
  context.binding.safe_area = viewport;
  context.binding.hud_scale = 1;
  context.binding.presentation = state->presentation;
  context.time_milliseconds = time;
  return context;
}
static const qa_render_command *client_caption_commands(void *ctx, const qa_active_caption *captions, size_t caption_count,
    qa_rect viewport, double time_milliseconds, size_t *out_count) {
  qa_startup_application *app = ctx;
  *out_count = 0;
  if (app->graphics == NULL) return NULL;
  return qa_app_startup_menu_caption_commands(app->graphics->menu, captions, caption_count, draw_context(app, viewport, time_milliseconds),
      out_count);
}
static void forward_frontend(void *ctx, const char *name, const char *const *args, size_t arg_count, qa_command_context source) {
  qa_startup_application_frontend_command(ctx, name, args, arg_count, source);
}
static void on_prepared(void *ctx, qa_app_prepared_startup *prepared) { qa_startup_bind_demo_commands(ctx, prepared); }
static void next_frame(void *ctx) { (void)ctx; /* await Bun.sleep(0) */ }
static bool menu_art_read(void *ctx, const char *path, qa_image_level *out) {
  graphics_state *state = ctx;
  qa_app_load_menu_art_image(path, out);
  /* The queued create operations reference the decoded pixels (TS GC). */
  if (state->art_pixel_count < 4) state->art_pixels[state->art_pixel_count++] = (void *)out->pixels.bytes;
  return true;
}
static qa_content_catalog *turntable_catalog(void *ctx) { return qa_app_startup_selection_catalog(((qa_startup_application *)ctx)->model); }
static bool bindings_available(void *ctx, qa_seat_id seat) {
  qa_startup_application *app = ctx;
  qa_app_prepared_startup *prepared = app->client != NULL ? app->client->prepared : qa_app_prepared_initial_startup(app->initial);
  bool all = false, has_selected = false;
  char *const *keys = NULL;
  size_t key_count = 0, selected_count = 0, authored_count = 0;
  const qa_input_binding *selected = NULL, *authored = NULL;
  return qa_app_prepared_startup_seat_choices(prepared, seat, &all, &keys, &key_count, &has_selected, &selected, &selected_count,
             &authored, &authored_count) && authored != NULL;
}
static void bindings_reset(void *ctx, qa_seat_id seat) {
  qa_startup_application *app = ctx;
  qa_app_prepared_startup_reset_bindings(app->client != NULL ? app->client->prepared : qa_app_prepared_initial_startup(app->initial), seat);
}
static void session_close_body(void *ctx) { qa_engine_session_close(ctx); }
static void configuration_close_body(void *ctx) { qa_app_configuration_content_close(ctx); }
static void scripts_close_body(void *ctx) { qa_app_console_script_files_close(ctx); }
static void image_close_body(void *ctx) { qa_app_image_settings_close(ctx); }

/* The openGraphics catch: every owner opened so far closes in the TS order, then the error rethrows. */
typedef struct open_graphics_frame {
  qa_startup_application *app;
  qa_startup_graphics *g;
  graphics_state *state;
  qa_app_prepared_startup *prepared;
  const qa_catalog_discovered_product *product;
  qa_seat_id seat; qa_client_id client;
  qa_session_seat *primary_seat;
} open_graphics_frame;

static void open_graphics_catch(open_graphics_frame *frame) {
  qa_startup_application *app = frame->app;
  qa_startup_graphics *g = frame->g;
  qa_caught_error ignored;
  if (app->client_storage.video_restart != NULL) qa_app_video_restart_close(app->client_storage.video_restart);
  if (app->client_storage.capture != NULL) qa_app_capture_close(app->client_storage.capture);
  if (g->menu != NULL) qa_app_startup_menu_close(g->menu);
  if (g->turntable != NULL) qa_app_character_turntable_destroy(g->turntable);
  g->turntable = NULL;
  if (g->devices != NULL) qa_app_input_devices_close(g->devices);
  if (g->audio != NULL) qa_app_startup_audio_close(g->audio);
  if (g->theme_mounts != NULL) qa_content_mounted_close(g->theme_mounts);
  if (g->router != NULL) qa_input_router_close(g->router);
  if (g->controllers != NULL) qa_sdl_controllers_close(g->controllers);
  if (g->art != NULL) qa_native_ui_art_close(g->art);
  if (g->typography != NULL) qa_app_menu_typography_close(g->typography);
  g->typography = NULL;
  if (g->font != NULL) qa_app_menu_font_close(g->font);
  g->font = NULL;
  qa_scene_image_registry_close(g->images);
  if (g->renderer != NULL) qa_app_native_renderer_destroy(g->renderer);
  g->renderer = NULL;
  qa_content_mounted_close(g->mounted);
  (void)qa_try(session_close_body, app->session, &ignored);
  (void)qa_try(image_close_body, g->image_settings, &ignored);
  (void)qa_try(scripts_close_body, app->scripts, &ignored);
  g->closed = true;
}

/* The configuration preparation inside openGraphics' first try: the source cvar archive and
 * prepareInitialConfiguration (with bindDemoCommands as onPrepared). */
typedef struct prepare_frame {
  qa_startup_application *app;
  qa_app_options *options;
  qa_content_catalog *catalog;
  qa_app_application_configuration_selection selection;
} prepare_frame;
static void prepare_initial_body(void *ctx) {
  prepare_frame *frame = ctx;
  qa_startup_application *app = frame->app;
  qa_config_store store;
  if (!qa_app_configuration_store_for_catalog(frame->catalog, frame->options, frame->selection.source.content, &store))
    qa_range_error("Out of memory");
  qa_cleanup store_guard;
  qa_cleanup_push(&store_guard, (void (*)(void *))qa_config_store_destroy, &store);
  const char *owner[3] = {"source", frame->selection.source.content, frame->selection.source.provider};
  app->source_archive = qa_app_load_cvar_archive(&store, owner, 3,
      qa_app_configuration_dialect_in_catalog(frame->catalog, app->configuration), &app->source_archive_count);
  qa_cleanup_pop(&store_guard);
  qa_config_store_destroy(&store);
  app->scripts = qa_app_open_configuration_scripts(frame->options, app->configuration, &app->settings_storage);
  qa_app_prepare_initial_args args;
  memset(&args, 0, sizeof args);
  args.catalog = frame->catalog;
  args.content = app->configuration;
  args.session = app->session;
  args.identity = &app->identity;
  args.settings = &app->settings_storage;
  args.scripts = app->scripts;
  args.print = host_print_fn; args.print_ctx = app;
  args.source_archive = app->source_archive; args.source_archive_count = app->source_archive_count;
  /* options.mode === "singleplayer" ? (engine product family q3 ? 8 : options.seats) : 16 */
  if (same(frame->options->mode, "singleplayer")) {
    const qa_catalog_discovered_product *engine = qa_content_catalog_product(frame->catalog, frame->selection.engine_behavior.content);
    args.default_capacity = engine != NULL && engine->expectation.family == QA_GAME_FAMILY_Q3 ? 8 : frame->options->seats;
  } else args.default_capacity = 16;
  args.next_frame = next_frame; args.next_frame_ctx = app;
  args.on_prepared = on_prepared; args.on_prepared_ctx = app;
  app->initial = qa_app_prepare_initial_configuration(frame->options, &args);
  if (app->initial == NULL) qa_range_error("Out of memory");
  qa_app_startup_stamp("prepare-initial-configuration");
}

static const qa_image_resource_operation *registry_drain(void *ctx, size_t *out_count);
static void registry_require(void *ctx, qa_renderer_image image);
/* openGraphics' second try: every graphical owner, the menu, input, capture, video restart and the client record. */
static void open_graphics_owners(void *ctx) {
  open_graphics_frame *frame = ctx;
  qa_startup_application *app = frame->app;
  qa_startup_graphics *g = frame->g;
  graphics_state *state = frame->state;
  const qa_catalog_discovered_product *product = frame->product;
  qa_content_catalog *catalog = qa_app_startup_selection_catalog(app->model);
  const qa_app_options *options = qa_app_prepared_initial_options(app->initial); /* options = initial.options */
  qa_app_image_settings *image_settings = app->image_settings;
  qa_cvar_registry *cvars = qa_app_image_settings_cvars(image_settings);
  qa_seat_id seat = frame->seat;
  /* devices = new InputDevices(imageSettings.cvars, inputDeviceStore(options.userContentRoot), host.print) */
  if (!qa_app_input_device_store(options->user_content_root, &state->devices_store)) qa_range_error("Out of memory");
  state->has_devices_store = true;
  g->devices = qa_app_input_devices_create(cvars, &state->devices_store, host_print_fn, app, NULL);
  if (g->devices == NULL) qa_range_error("Out of memory");
  qa_app_image_settings_policy(image_settings, &state->policy);
  state->has_policy = true;
  qa_app_menu_font_options font_options;
  memset(&font_options, 0, sizeof font_options);
  font_options.catalog = catalog;
  font_options.mounts = g->mounted;
  font_options.family = product->expectation.family;
  font_options.rerelease = same(product->expectation.edition, "rerelease");
  font_options.images = g->images;
  font_options.image_policy = &state->policy.policy;
  g->font = qa_app_load_menu_font(&font_options);
  qa_app_startup_stamp("menu-font");
  state->font = qa_app_menu_font_selection(g->font);
  g->typography = qa_app_load_menu_typography(catalog, g->images, state->font.classic, &state->policy.policy);
  qa_app_startup_stamp("typography");
  const qa_renderer_image *font_source = &state->font.classic->picture.image.image;
  if (!font_source->source_is_resource) qa_raise("Error", 0, 0, "Startup font has no mounted resource identity");
  g->art = xcalloc(1, sizeof *g->art);
  qa_ui_load_native_art_selected(g->art, font_source->source_resource.id, g->images, menu_art_read, state, false, true);
  qa_app_startup_stamp("ui-art");
  qa_app_native_renderer_host renderer_host;
  memset(&renderer_host, 0, sizeof renderer_host);
  renderer_host.open_bundle = qa_app_open_native_backend_bundle;
  g->renderer = qa_app_native_renderer_open(renderer_host, options->renderer, options->width, options->height, options->hidden,
      options->gamma, qa_cvar_registry_variable_value(cvars, "r_smp") != 0, g->images->owner, g->images);
  if (g->renderer == NULL) qa_range_error("Out of memory");
  qa_app_startup_stamp("renderer");
  qa_app_native_renderer *native = g->renderer;
  qa_app_image_settings_refresh_display(image_settings, native);
  g->controllers = qa_sdl_controllers_open();
  if (g->controllers == NULL) qa_range_error("Out of memory");
  const qa_catalog_discovered_product *theme = NULL;
  for (size_t i = 0; i < catalog->product_count && theme == NULL; i++) {
    const qa_catalog_discovered_product *candidate = &catalog->products[i];
    if (candidate->availability.kind == QA_CATALOG_INSTALLED && candidate->expectation.family == QA_GAME_FAMILY_Q2
        && same(candidate->expectation.edition, "rerelease") && same(candidate->expectation.campaign, "baseq2"))
      theme = candidate;
  }
  if (theme != NULL)
    g->theme_mounts = open_startup_mounts(catalog, theme->id, "music", &state->theme_list, &state->has_theme_list, &state->theme_order,
        &state->theme_plan_id);
  qa_app_startup_audio_options audio;
  memset(&audio, 0, sizeof audio);
  audio.music_controls = &app->music_controls;
  if (theme != NULL && g->theme_mounts != NULL) {
    audio.has_theme = true;
    audio.theme_mounts = g->theme_mounts;
    audio.theme_source = (qa_app_music_source){theme->id, theme->expectation.family, theme->expectation.edition, theme->expectation.campaign};
  }
  audio.mounts = g->mounted;
  audio.source = (qa_app_music_source){product->id, product->expectation.family, product->expectation.edition, product->expectation.campaign};
  audio.seat = seat;
  audio.print = host_print_fn; audio.print_ctx = app;
  /* preferences: { ...audioBaseline, ...audioValues, ...readMusicSettings(cvars), outputFormat: readAudioOutputCvars(cvars) } */
  const qa_app_audio_preferences *baseline = qa_app_frontend_preferences_audio_baseline(app->preferences);
  double effects = 0, music = 0;
  qa_app_frontend_preferences_audio_values(app->preferences, &effects, &music);
  qa_app_audio_music_preferences music_preferences = qa_app_read_music_settings(cvars);
  qa_cleanup music_guard;
  qa_cleanup_push(&music_guard, free, (void *)music_preferences.menu_track);
  audio.preferences.has_effects_volume = true; audio.preferences.effects_volume = effects;
  audio.preferences.has_music_volume = true; audio.preferences.music_volume = music;
  audio.preferences.menu_track = music_preferences.menu_track;
  audio.preferences.has_output_format = true;
  audio.preferences.output_format = qa_app_read_audio_output_cvars(cvars);
  g->audio = qa_app_startup_audio_open(&audio);
  qa_cleanup_pop(&music_guard);
  free((void *)music_preferences.menu_track);
  if (g->audio == NULL) qa_range_error("Out of memory");
  qa_app_startup_audio_open_output(g->audio, baseline != NULL ? baseline->device_name : NULL, host_print_fn, app);
  qa_app_startup_audio_bind_output_cvars(g->audio, cvars);
  qa_app_startup_stamp("audio");
  qa_ui_seat_preferences_init(&state->accessibility, seat, cvars);
  qa_app_character_turntable_options turntable;
  memset(&turntable, 0, sizeof turntable);
  turntable.catalog_ctx = app; turntable.catalog = turntable_catalog;
  turntable.images = g->images;
  turntable.print_ctx = app; turntable.print = host_print_fn;
  g->turntable = qa_app_character_turntable_create(turntable);
  if (g->turntable == NULL) qa_range_error("Out of memory");
  /* settings: accessibility, console, preferences (output + music), devices, input routing, gamepad,
   * audio geometry, music playlist, native video, renderer. */
  size_t accessibility_count = 0;
  const qa_ui_setting_binding *accessibility = qa_ui_seat_preferences_bindings(&state->accessibility, &accessibility_count);
  own_bindings(state, (qa_ui_setting_bindings){(qa_ui_setting_binding *)accessibility, accessibility_count, NULL});
  state->owners[state->owner_count - 1].items = NULL; /* the preferences own their bindings */
  state->owners[state->owner_count - 1].count = 0;
  own_bindings(state, qa_ui_bind_console_settings(cvars));
  state->output.ctx = app;
  state->output.has_format = true;
  state->output.format_read = output_format_read;
  state->output.format_select = output_format_select;
  state->output.selected = output_selected;
  state->output.devices = output_devices;
  state->output.select = output_select;
  state->output.report = report_status;
  own_bindings(state, qa_app_frontend_preferences_bindings(app->preferences, &state->output, music_settings, app));
  own_bindings(state, qa_app_input_devices_bindings(g->devices));
  own_bindings(state, qa_ui_bind_input_routing_settings(routing_router, app, routing_devices, app));
  own_bindings(state, qa_ui_bind_gamepad_settings_fn(gamepad_input, app));
  own_bindings(state, qa_ui_bind_audio_geometry_settings(cvars));
  own_bindings(state, qa_ui_bind_music_playlist_settings(cvars, music_tracks, app));
  own_bindings(state, qa_ui_bind_native_video_settings(native_window, app, cvars, report_status, app, mutate_window, app));
  state->renderer_settings.ctx = app;
  state->renderer_settings.current = renderer_current;
  state->renderer_settings.has_worker = true;
  state->renderer_settings.worker_read = renderer_worker_read;
  state->renderer_settings.worker_write = renderer_worker_write;
  state->renderer_settings.apply = renderer_apply;
  state->renderer_settings.report = report_status;
  state->renderer_settings.enabled = renderer_enabled;
  own_bindings(state, qa_ui_bind_renderer_settings(&state->renderer_settings));
  state->lobby_view.ctx = app;
  state->lobby_view.account_id = app->lobby_account.id;
  state->lobby_view.current = lobby_view_current;
  state->lobby_view.list = lobby_view_list;
  state->lobby_view.host = lobby_view_host;
  state->lobby_view.join = lobby_view_join;
  state->lobby_view.ready = lobby_view_ready;
  state->lobby_view.start = lobby_view_start;
  state->lobby_view.leave = lobby_view_leave;
  qa_app_startup_menu_options menu;
  memset(&menu, 0, sizeof menu);
  menu.ctx = app;
  menu.has_lobby = true;
  menu.lobby_current = menu_lobby_current;
  menu.lobby_selection = menu_lobby_selection;
  menu.lobby_seats = menu_lobby_seats;
  menu.appearance = menu_appearance;
  menu.team_arena = true;
  menu.has_libraries = true;
  menu.library_addons = qa_app_addon_library_service(app->addons);
  menu.library_player_progress = qa_startup_player_progress_library(app);
  menu.library_movies = qa_startup_movie_library_menu(app);
  menu.library_demos = qa_startup_demo_library_menu(app);
  menu.library_configurations = qa_startup_configuration_library(app);
  menu.library_server_profiles = qa_startup_server_profile_library(app);
  menu.sound = menu_sound;
  menu.llm = app->host.base.llm_settings;
  menu.clipboard = menu_clipboard;
  menu.seat = seat;
  menu.model = app->model;
  menu.character_turntable = g->turntable;
  menu.art = g->art;
  menu.font = qa_app_menu_typography_body(g->typography);
  menu.title_font = qa_app_menu_typography_title(g->typography);
  menu.now = menu_now;
  if (app->browser != NULL) {
    menu.browser = app->browser;
    menu.connect = menu_connect;
  }
  menu.play_preset = menu_play_preset;
  menu.play = menu_play;
  menu.load = menu_load;
  menu.quit = menu_quit;
  menu.settings = state->settings; menu.setting_count = state->setting_count;
  menu.saves = menu_saves;
  menu.refresh_saves = menu_refresh_saves;
  g->menu = qa_app_startup_menu_new(menu);
  if (g->menu == NULL) qa_range_error("Out of memory");
  qa_app_startup_menu_set_status(g->menu, app->status != NULL ? app->status : "", false);
  qa_app_prepared_startup *prepared = frame->prepared;
  qa_seat_input *input = prepared_input(prepared, seat);
  g->input_profile = qa_app_startup_input_profile_retained(&app->settings_storage, input);
  if (g->input_profile == NULL) qa_range_error("Out of memory");
  /* activeMenu.bindInput(input, () => sharedBindingActions(...), { available, reset }): the helper reads app->graphics. */
  /* activeMenu.bindInput(...): the shared helper reads app->graphics, so the graphics are published here
   * (TS publishes this.graphics after the router; nothing reads it in between). */
  app->graphics = g;
  qa_startup_graphics_bind_menu_input(app, input, seat, bindings_available, bindings_reset);
  qa_ui_menu_id active = qa_native_ui_controller_active_menu(qa_app_startup_menu_controller(g->menu));
  qa_seat_input_focus focus = {"menu", active != NULL ? active : "menu:startup:main", NULL, false, NULL};
  qa_seat_input_set_focus(input, focus, qa_startup_now());
  /* router = new InputRouter({ seats: [{ input, controller: saved ?? automatic }], keyboardSeat, controllers, ... }) */
  qa_input_seat_route route;
  memset(&route, 0, sizeof route);
  route.input = input;
  route.controller.kind = QA_CONTROLLER_SELECTION_AUTOMATIC;
  char *profile_name = qa_app_configuration_seat_profile_name((size_t)seat.index);
  if (profile_name == NULL) qa_range_error("Out of memory");
  qa_seat_settings saved;
  memset(&saved, 0, sizeof saved);
  bool has_saved = qa_config_store_load_seat(&app->settings_storage, profile_name, &saved);
  free(profile_name);
  if (has_saved) route.controller = controller_selection(&saved.controller);
  qa_input_router_options router;
  memset(&router, 0, sizeof router);
  router.seats = &route; router.seat_count = 1;
  router.has_keyboard_seat = true; router.keyboard_seat = seat;
  g->controllers_view = qa_input_controllers_from_sdl(g->controllers);
  router.controllers = &g->controllers_view;
  router.now = router_now;
  router.ticks = router_ticks; router.ticks_ctx = app;
  router.subframe = false;
  router.unhandled = router_unhandled; router.unhandled_ctx = app;
  g->router = qa_input_router_create(&router);
  if (has_saved) qa_seat_settings_free(&saved);
  if (g->router == NULL) qa_range_error("Out of memory");
  qa_input_router_attach_window(g->router, qa_input_window_from_sdl(qa_app_native_renderer_window(native)));
  qa_app_input_devices_activate(g->devices, g->router);
  free(app->frontend_routing_baseline);
  app->frontend_routing_baseline = qa_startup_frontend_routing_state(g->router);
  char *settings_root = qa_startup_settings_directory(qa_app_startup_saves_directory(app->saves));
  bool stored = settings_root != NULL && qa_config_store_init(&state->controller_store, settings_root);
  free(settings_root);
  if (!stored) qa_range_error("Out of memory");
  state->has_controller_store = true;
  g->controller_settings = qa_app_controller_settings_create(g->router, &seat, 1, controller_devices, app, &state->controller_store,
      host_print_fn, app);
  if (g->controller_settings == NULL) qa_range_error("Out of memory");
  qa_app_controller_settings_update(g->controller_settings);
  qa_ui_gyro_settings gyro = qa_app_controller_settings_ui(g->controller_settings, seat);
  qa_app_startup_menu_bind_gyro(g->menu, &gyro);
  qa_render_frame_image_registry registry = {g, g->images->owner, registry_drain, registry_require};
  if (!qa_scene_frame_builder_init(&state->builder, registry)) qa_range_error("Out of memory");
  state->has_builder = true;
  qa_seat_text_presentation_init(&state->console_text, seat, state->font);
  /* provider = { provider: `${family}:official`, content: product.id } */
  const char *family = qa_game_family_name(product->expectation.family);
  size_t length = strlen(family) + sizeof ":official";
  state->provider_name = xcalloc(length, 1);
  snprintf(state->provider_name, length, "%s:official", family);
  qa_provider_reference provider = {state->provider_name, product->id};
  state->presentation.doppler.is_source = true;
  state->presentation.environment.kind = QA_ENVIRONMENT_AUDIO_CONTENT;
  state->presentation.assets = product->id;
  state->presentation.hud = provider;
  state->presentation.effects = provider;
  state->presentation.audio = provider;
  free(g->display_renderer);
  g->display_renderer = qa_startup_dup(options->renderer);
  g->display_gamma = options->gamma;
  g->display_width = options->width;
  g->display_height = options->height;
  g->display_hidden = options->hidden;
  /* locals = initial.prepared.seats.map(prepared => ({ client, seat, prepared })) */
  qa_app_client_bootstrap *client = &app->client_storage;
  size_t seat_count = 0;
  const qa_app_prepared_seat *seats = qa_app_prepared_startup_seats(prepared, &seat_count);
  client->locals = xcalloc(seat_count, sizeof *client->locals);
  client->local_capacity = seat_count;
  for (size_t i = 0; i < seat_count; i++) {
    qa_session_seat *local = seats[i].context.origin.kind == QA_COMMAND_ORIGIN_LOCAL_SEAT
        ? qa_app_prepared_initial_local_seat(app->initial, seats[i].context.origin.client) : NULL;
    if (local == NULL || !qa_same_seat(qa_session_seat_id(local), seats[i].id))
      qa_raise("Error", 0, 0, "Prepared startup seat has no session owner");
    client->locals[i].client = qa_session_seat_client(local);
    client->locals[i].seat = local;
    client->locals[i].prepared = seats[i].id;
  }
  client->local_count = seat_count;
  qa_app_application_capture_services services;
  memset(&services, 0, sizeof services);
  services.ctx = app;
  services.commands = qa_app_prepared_startup_commands(prepared);
  services.configuration_write_started = capture_write_started;
  services.console = capture_console;
  services.input = capture_input;
  services.can_chat = capture_can_chat;
  services.root = capture_root;
  services.map_name = capture_map_name;
  services.write = capture_write;
  services.print = capture_print;
  client->capture = qa_app_capture_create(&services, native);
  if (client->capture == NULL) qa_range_error("Out of memory");
  qa_app_video_restart_host video = {app, video_capture, video_render_worker, video_prepare, video_publish_window, video_published,
      video_settled, video_print, video_failed};
  client->video_restart = qa_app_video_restart_create(native, video);
  if (client->video_restart == NULL) qa_range_error("Out of memory");
  if (app->recording == NULL) qa_raise("Error", 0, 0, "Client recording commands were not prepared");
  qa_cvar_registry *source = qa_app_prepared_startup_source(prepared);
  qa_app_keys_publish(app->keys, qa_app_keys_prepare(app->keys, options, catalog, source), source);
  client->window = qa_app_native_renderer_window(native);
  client->prepared = prepared;
  client->platform.kind = QA_APP_CLIENT_INPUT_MENU;
  client->platform.router = g->router;
  client->platform.controller_settings = g->controller_settings;
  client->platform.ctx = app;
  client->platform.retire_commands = qa_startup_retire_menu_commands;
  client->controllers = g->controllers;
  client->input_devices = g->devices;
  client->configuration_current.scripts = app->scripts;
  client->configuration_current.options = options;
  client->keys = app->keys;
  client->recording = app->recording;
  client->stop_recording_ctx = app;
  client->stop_recording = client_stop_recording;
  client->music_controls = &app->music_controls;
  client->identity = &app->identity;
  client->session = app->session;
  client->image_settings = image_settings;
  client->source_current = NULL;
  client->has_source_profile = true;
  client->source_profile = qa_app_configuration_content_selection(app->configuration).source;
  client->bootstrap_ctx = app;
  client->has_pending_source = client_has_pending_source;
  client->route_command = client_route_command;
  client->dispatch_application_request = client_dispatch;
  client->renderer = native;
  client->output_current = qa_app_startup_audio_engine(g->audio);
  client->local_lobby = &app->lobby;
  client->settings = &app->settings_storage;
  client->caption_commands = client_caption_commands;
  client->activate_frontend = client_activate_frontend;
  app->client = client;
  qa_startup_bind_frontend_console(app);
  qa_app_capture_activate(client->capture);
  qa_app_video_restart_register(client->video_restart, qa_app_prepared_startup_commands(prepared));
  qa_seat_settings saved_input;
  memset(&saved_input, 0, sizeof saved_input);
  if (qa_config_store_load_seat(&app->settings_storage, "input/seat-1.json", &saved_input)) {
    qa_cleanup guard;
    qa_cleanup_push(&guard, (void (*)(void *))qa_seat_settings_free, &saved_input);
    qa_seat_console *console = client_console_of(client, frame->primary_seat);
    if (console != NULL)
      qa_console_history_replace(&console->history, (const char *const *)saved_input.history, saved_input.history_count);
    qa_cleanup_pop(&guard);
    qa_seat_settings_free(&saved_input);
  }
  qa_app_prepared_startup_forward_commands(prepared, forward_frontend, app);
  size_t request_count = 0;
  const qa_app_configuration_command_request *requests = qa_app_prepared_initial_requests(app->initial, &request_count);
  for (size_t i = 0; i < request_count; i++)
    qa_startup_application_frontend_command(app, requests[i].name, requests[i].arguments, requests[i].argument_count, requests[i].source);
  qa_app_startup_audio_flush_commands(g->audio);
  if (!app->has_pending && !app->has_pending_demo && same(app->entry, "run")) {
    qa_app_startup_action *action = qa_startup_stage(app, QA_APP_STARTUP_ACTION_INITIAL);
    qa_app_startup_options_copy(&action->options, qa_app_prepared_initial_options(app->initial));
  }
}

/* ---- graphics.draw(loadingOwner?) -------------------------------------------------- */
static const qa_image_resource_operation *registry_drain(void *ctx, size_t *out_count) {
  qa_startup_graphics *g = ctx;
  graphics_state *state = g->draw_state;
  free(state->drained);
  state->drained = qa_scene_image_registry_drain_operations(g->images, out_count);
  return state->drained;
}
static void registry_require(void *ctx, qa_renderer_image image) {
  qa_scene_image_registry_require(((qa_startup_graphics *)ctx)->images, image);
}
typedef struct draw_sink { qa_scene_frame_builder *builder; } draw_sink;
static void builder_emit(void *ctx, const qa_render_command *command) {
  qa_scene_frame_builder_command(((draw_sink *)ctx)->builder, *command);
}
static void startup_material(void *ctx, const qa_material_text_draw *draw) {
  (void)ctx; (void)draw;
  qa_raise("Error", 0, 0, "Startup charset unexpectedly requested a material draw");
}
static void console_emit(void *ctx, const qa_render_command *command) {
  if (command->kind == QA_RENDER_COMMAND_SWAP_BUFFERS) qa_raise("Error", 0, 0, "Text cannot present a frame");
  qa_scene_frame_builder_command(((draw_sink *)ctx)->builder, *command);
}
static void console_material(void *ctx, const qa_material_text_draw *draw) {
  (void)ctx; (void)draw;
  qa_raise("Error", 0, 0, "Console charset unexpectedly requested a material draw");
}
/* drawConsole's clipped Draw2D (console/draw.h): every stretch is clipped to the console bounds. */
typedef struct console_draw_state { graphics_state *state; const qa_draw2d *draw; } console_draw_state;
typedef struct clip_sink { const qa_text_draw_sink *sink; qa_rect bounds; } clip_sink;
static void clip_set_color(void *ctx, const qa_vec4 *color) {
  const clip_sink *clip = ctx;
  clip->sink->set_color(clip->sink->ctx, color);
}
static void clip_stretch(void *ctx, qa_rect rect, qa_text_texture_rect uv, const qa_picture_asset *picture) {
  const clip_sink *clip = ctx;
  qa_clipped_picture clipped;
  qa_texture_rect source = {uv.s, uv.t, uv.s2, uv.t2};
  if (!qa_clip_picture(rect, source, clip->bounds, &clipped)) return;
  qa_text_texture_rect out = {clipped.uv.s1, clipped.uv.t1, clipped.uv.s2, clipped.uv.t2};
  clip->sink->stretch_pixels(clip->sink->ctx, clipped.rect, out, picture);
}
static qa_draw2d clipped_draw(const qa_draw2d *draw, clip_sink *clip, qa_rect bounds) {
  clip->sink = &draw->commands;
  clip->bounds = bounds;
  qa_draw2d out = *draw;
  out.commands.ctx = clip;
  out.commands.set_color = clip_set_color;
  out.commands.stretch_pixels = clip_stretch;
  return out;
}
static void console_draw_pic(void *ctx, qa_rect rect, const void *picture, qa_rect clip) {
  console_draw_state *draw_state = ctx;
  clip_sink sink;
  qa_draw2d draw = clipped_draw(draw_state->draw, &sink, clip);
  qa_draw2d_draw_pic(&draw, rect, picture);
}
static size_t encode_code_point(char *text, uint32_t character) {
  size_t length = 0;
  if (character < 0x80) text[length++] = (char)character;
  else if (character < 0x800) {
    text[length++] = (char)(0xc0 | (character >> 6));
    text[length++] = (char)(0x80 | (character & 63));
  } else if (character < 0x10000) {
    text[length++] = (char)(0xe0 | (character >> 12));
    text[length++] = (char)(0x80 | ((character >> 6) & 63));
    text[length++] = (char)(0x80 | (character & 63));
  } else {
    text[length++] = (char)(0xf0 | (character >> 18));
    text[length++] = (char)(0x80 | ((character >> 12) & 63));
    text[length++] = (char)(0x80 | ((character >> 6) & 63));
    text[length++] = (char)(0x80 | (character & 63));
  }
  text[length] = '\0';
  return length;
}
static void console_glyph_metrics(void *ctx, const void *font, uint32_t character, double line_height, double cell_width,
    double *out_line_height, double *out_top) {
  (void)ctx;
  char text[8];
  (void)encode_code_point(text, character);
  qa_console_glyph_metrics metrics = qa_console_glyph_metrics_for(font, text, line_height, cell_width);
  *out_line_height = metrics.line_height;
  *out_top = metrics.top;
}
static qa_text_layout_options console_options(const graphics_state *state, char *text, uint32_t character, double scale, qa_vec4 color,
    bool alternate, double line_height) {
  size_t length = encode_code_point(text, character);
  qa_text_layout_options options;
  memset(&options, 0, sizeof options);
  options.text = text; options.text_length = length; options.font = state->console_text.font;
  options.scale = scale; options.color = color; options.alternate = alternate;
  options.has_line_height = true; options.line_height = line_height;
  return options;
}
static double console_layout_width(void *ctx, uint32_t character, double scale, qa_vec4 color, bool alternate, double line_height) {
  console_draw_state *draw_state = ctx;
  char text[8];
  qa_text_layout_options options = console_options(draw_state->state, text, character, scale, color, alternate, line_height);
  qa_text_layout layout;
  if (!qa_seat_text_presentation_layout(&draw_state->state->console_text, &options, &layout)) qa_range_error("Out of memory");
  double width = layout.width;
  qa_text_layout_destroy(&layout);
  return width;
}
static void console_draw_character(void *ctx, uint32_t character, double scale, qa_vec4 color, bool alternate, double line_height,
    double x, double y, qa_rect clip) {
  console_draw_state *draw_state = ctx;
  char text[8];
  qa_text_layout_options options = console_options(draw_state->state, text, character, scale, color, alternate, line_height);
  qa_text_layout layout;
  clip_sink sink;
  qa_draw2d draw = clipped_draw(draw_state->draw, &sink, clip);
  if (!qa_seat_text_presentation_draw(&draw_state->state->console_text, &draw, &options, (qa_vec2){x, y}, 0, &layout))
    qa_range_error("Out of memory");
  qa_text_layout_destroy(&layout);
}
typedef struct console_frame { qa_console_row *rows; size_t row_count; qa_console_discovery_entry *entry; char *field; } console_frame;
static void console_frame_free(void *ctx) {
  console_frame *frame = ctx;
  free(frame->rows);
  if (frame->entry != NULL) qa_console_discovery_entries_free(frame->entry, 1);
  free(frame->field);
}
static void draw_console_overlay(qa_startup_application *app, graphics_state *state, qa_app_client_bootstrap_seat *primary,
    qa_seat_console *console, qa_rect viewport) {
  qa_startup_graphics *g = app->graphics;
  qa_seat_id primary_id = qa_session_seat_id(primary->seat);
  if (!qa_same_seat(state->console_text.seat, primary_id)) qa_seat_text_presentation_init(&state->console_text, primary_id, state->font);
  draw_sink sink = {&state->builder};
  qa_text_command_sink commands;
  qa_text_command_sink_init(&commands, primary_id, viewport, console_emit, &sink, console_material, &sink);
  qa_draw2d draw = {qa_text_command_sink_sink(&commands), QA_COORD_PIXELS};
  qa_sdl_size logical = qa_sdl_window_logical_size(qa_app_native_renderer_window(g->renderer));
  double height = trunc(viewport.height * 0.5);
  qa_console_metrics_options metric_options = {viewport.width, viewport.height,
      qa_js_max(viewport.width / logical.width, viewport.height / logical.height),
      qa_cvar_registry_variable_value(qa_app_image_settings_cvars(g->image_settings), "con_scale"), &state->console_text.font};
  qa_console_metrics metrics = qa_console_metrics_for(&metric_options);
  qa_picture_asset white;
  memset(&white, 0, sizeof white);
  white.kind = QA_PICTURE_IMAGE;
  white.image = g->art->white;
  qa_draw2d_fill_rect(&draw, (qa_rect){0, 0, viewport.width, height}, (qa_vec4){0, 0, 0, 0.85}, &white);
  if ((double)console->buffer.width != metrics.columns) qa_console_buffer_resize(&console->buffer, (int64_t)metrics.columns);
  console_frame frame;
  memset(&frame, 0, sizeof frame);
  qa_cleanup guard;
  qa_cleanup_push(&guard, console_frame_free, &frame);
  frame.rows = qa_console_buffer_visible(&console->buffer, (int64_t)qa_js_max(1, trunc(height / metrics.line_height)), &frame.row_count);
  frame.field = xcalloc(console->field.length + 1, 1);
  memcpy(frame.field, console->field.characters, console->field.length);
  qa_console_field_view field = {frame.field, console->field.cursor, console->field.scroll, console->field.overstrike};
  frame.entry = qa_seat_console_selected_completion_entry(console);
  console_draw_state draw_state = {state, &draw};
  qa_console_draw_target target = {&draw_state, primary_id, qa_draw2d_width(&draw), qa_draw2d_height(&draw), console_draw_pic};
  qa_console_text_presentation text = {&draw_state, primary_id, &state->console_text.font, console_glyph_metrics, console_layout_width,
      console_draw_character};
  qa_console_draw_options options;
  memset(&options, 0, sizeof options);
  options.draw = &target; options.text = &text;
  options.rows = frame.rows; options.row_count = frame.row_count;
  options.field = &field;
  options.height = height;
  options.scale = metrics.scale;
  options.has_cell_width = true; options.cell_width = metrics.cell_width;
  options.now_ms = qa_startup_now();
  options.background = NULL;
  options.selected_entry = frame.entry;
  qa_draw_console(&options);
  qa_cleanup_pop(&guard);
  console_frame_free(&frame);
}
/* graphics.draw(loadingOwner?): has_loading_owner false is TS undefined. */
void qa_startup_graphics_draw(qa_startup_application *app, const qa_app_client_source_lifetime *loading_owner, bool has_loading_owner) {
  qa_startup_graphics *g = app->graphics;
  if (g == NULL) return;
  graphics_state *state = g->draw_state;
  qa_unified_audio *engine = qa_app_startup_audio_engine(g->audio);
  qa_unified_audio *output = app->client != NULL && app->client->output_current != NULL ? app->client->output_current : engine;
  if (output == engine) {
    double effects = 0, music = 0;
    qa_app_frontend_preferences_audio_values(app->preferences, &effects, &music);
    qa_app_startup_audio_set_volumes(g->audio, effects, music);
  }
  qa_unified_audio_update_music(output);
  (void)qa_unified_audio_pump(output, false, 0, 0);
  if (app->client != NULL && app->client->platform.kind == QA_APP_CLIENT_INPUT_WORLD
      && (!has_loading_owner || app->client->source_current != loading_owner))
    return;
  qa_sdl_size drawable = qa_sdl_window_drawable_size(qa_app_native_renderer_window(g->renderer));
  qa_rect viewport = {0, 0, drawable.width, drawable.height};
  qa_scene_frame_builder_begin(&state->builder, "back", true);
  draw_sink sink = {&state->builder};
  qa_app_startup_menu_draw(g->menu, draw_context(app, viewport, qa_startup_now()), builder_emit, startup_material, &sink);
  qa_app_client_bootstrap_seat *primary = client_primary(app);
  qa_seat_console *console = primary != NULL ? client_console_of(app->client, primary->seat) : NULL;
  if (primary != NULL && console != NULL) {
    qa_seat_input *input = prepared_input(app->client->prepared, primary->prepared);
    if (input != NULL && same(qa_seat_input_get_focus(input).kind, "console")) draw_console_overlay(app, state, primary, console, viewport);
  }
  qa_render_frame frame = qa_scene_frame_builder_finish(&state->builder, true);
  if (g->frame_observer != NULL) g->frame_observer(g->frame_observer_ctx, &frame);
  qa_app_native_renderer_execute(g->renderer, frame);
}
/* graphics.captionCommands is the client record's caption_commands (client_caption_commands). */

/* private async openGraphics(): Promise<StartupGraphics> */
void qa_startup_open_graphics(qa_startup_application *app) {
  qa_startup_refresh_preference_baseline(app, true);
  qa_app_startup_stamp("preference-baseline");
  {
    qa_app_options model_options;
    qa_app_startup_selection_options(app->model, &model_options);
    bool selection_default = same(model_options.renderer_selection, "default");
    qa_app_options_destroy(&model_options);
    if (selection_default && app->preference_store != NULL) {
      size_t length = 0;
      char *text = qa_config_store_load_text(app->preference_store, "renderer", &length);
      if (text != NULL) {
        /* ?.trim() */
        size_t start = 0, end = length;
        while (start < end && isspace((unsigned char)text[start])) start++;
        while (end > start && isspace((unsigned char)text[end - 1])) end--;
        text[end] = '\0';
        if (same(text + start, "cpu") || same(text + start, "gl")) qa_app_startup_selection_select(app->model, "renderer", text + start);
        free(text);
      }
    }
  }
  qa_app_startup_selection_options(app->model, &app->graphics_options);
  app->has_graphics_options = true;
  qa_app_options *options = &app->graphics_options;
  qa_content_catalog *catalog = qa_app_startup_selection_catalog(app->model);
  app->configuration = qa_app_open_initial_configuration_content_native(options, NULL, catalog);
  if (app->configuration == NULL) qa_range_error("Out of memory");
  qa_app_startup_stamp("initial-configuration-content");
  qa_app_application_configuration_selection selection = qa_app_configuration_content_selection(app->configuration);
  if (!qa_app_configuration_store_for_catalog(catalog, options, selection.engine_behavior.content, &app->settings_storage))
    qa_range_error("Out of memory");
  app->has_settings = true;
  {
    prepare_frame prepare = {app, options, catalog, selection};
    qa_caught_error caught;
    if (!qa_try(prepare_initial_body, &prepare, &caught)) {
      qa_caught_error ignored;
      (void)qa_try(session_close_body, app->session, &ignored);
      (void)qa_try(configuration_close_body, app->configuration, &ignored);
      qa_rethrow(&caught);
    }
  }
  qa_app_prepared_startup *prepared = qa_app_prepared_initial_startup(app->initial);
  if (!qa_app_prepared_startup_pending(prepared)) app->initial_configuration = false;
  app->scripts = qa_app_prepared_initial_scripts(app->initial);
  app->image_settings = qa_app_prepared_initial_image(app->initial);
  options = (qa_app_options *)qa_app_prepared_initial_options(app->initial); /* options = initial.options */
  size_t seat_count = 0;
  const qa_app_prepared_seat *seats = qa_app_prepared_startup_seats(prepared, &seat_count);
  qa_session_seat *primary_seat = seat_count > 0 && seats[0].context.origin.kind == QA_COMMAND_ORIGIN_LOCAL_SEAT
      ? qa_app_prepared_initial_local_seat(app->initial, seats[0].context.origin.client) : NULL;
  if (seat_count == 0 || primary_seat == NULL || app->image_settings == NULL) {
    qa_caught_error ignored;
    (void)qa_try(session_close_body, app->session, &ignored);
    (void)qa_try(scripts_close_body, app->scripts, &ignored);
    qa_raise("Error", 0, 0, "Graphical startup requires a prepared local client");
  }
  qa_seat_id seat = seats[0].id;
  qa_client_id client = qa_session_client_id(qa_session_seat_client(primary_seat));
  qa_startup_graphics *g = xcalloc(1, sizeof *g);
  graphics_state *state = xcalloc(1, sizeof *state);
  state->app = app;
  g->draw_state = state;
  app->graphics_local = g;
  g->seat = seat;
  g->client = client;
  g->image_settings = app->image_settings;
  /* const owner = { identity: Symbol("startup renderer"), session, generation: 0 }; images = new SceneImageRegistry(owner) */
  g->images = xcalloc(1, sizeof *g->images);
  qa_renderer_resource_owner owner = {g, app->identity.session, 0};
  qa_scene_image_registry_init(g->images, owner, NULL);
  const qa_catalog_discovered_product *product = NULL, *first_installed = NULL;
  for (size_t i = 0; i < catalog->product_count; i++) {
    const qa_catalog_discovered_product *candidate = &catalog->products[i];
    if (candidate->availability.kind != QA_CATALOG_INSTALLED) continue;
    if (first_installed == NULL) first_installed = candidate;
    if (product == NULL && same(candidate->expectation.id, options->product)) product = candidate;
  }
  if (product == NULL) product = first_installed;
  if (product == NULL) {
    app->graphics_local = NULL;
    qa_scene_image_registry_destroy(g->images);
    free(g->images);
    graphics_state_free(state);
    free(g);
    qa_raise("Error", 0, 0, "No installed game data found in %s; a game charset is required for the startup menu",
        options->corpus_root != NULL ? options->corpus_root : "undefined");
  }
  open_graphics_frame frame = {app, g, state, prepared, product, seat, client, primary_seat};
  g->mounted = open_startup_mounts(catalog, product->id, "font", &state->mounts, &state->has_mounts, &state->order, &state->plan_id);
  qa_app_startup_stamp("font-mounts");
  qa_caught_error caught;
  if (qa_try(open_graphics_owners, &frame, &caught)) return;
  open_graphics_catch(&frame);
  /* Graphics that were never published are freed here (TS GC); published ones stay closed for destroy. */
  if (app->graphics != g) {
    graphics_free(g);
    app->graphics_local = NULL;
  }
  qa_rethrow(&caught);
}
