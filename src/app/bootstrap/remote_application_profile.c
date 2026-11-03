/* Port of src/app/bootstrap/remote-application.ts, the profile region (TS
 * 1090-1635 in the 2026-11-03 tree). Status per member:
 * docs/remote_application_layout.md. GPL-2.0-or-later. */
#include <stdlib.h>
#include <string.h>
#include "remote_application_internal.h"
#include "qa/foundation/support.h"

/* TS this.remote.client: the session client the remote presentation was built for
 * (configuration.seat.client for the initial remote, the peer's seat client once promoted). */
static qa_session_client *remote_client(const qa_remote_application *app) {
  return app->primary_peer != NULL ? qa_session_seat_client(app->primary_peer->seat)
                                   : qa_session_seat_client(app->configuration->seat);
}
/* primaryPeer !== null ? !primaryPeer.closed : sourceConnection !== null && remote.client.connection === sourceConnection */
static bool source_connected(const qa_remote_application *app) {
  if (app->primary_peer != NULL) return !app->primary_peer->closed;
  return app->source_connection != NULL && qa_session_client_connection(remote_client(app)) == app->source_connection;
}
bool qa_remote_application_owns_published_source(qa_remote_application *app) {
  return !app->closed && !app->closing
      && (!app->ownership.borrowed || app->ownership.client->source_current == &app->source_lifetime)
      && source_connected(app);
}
void qa_remote_application_publish_remote(qa_remote_application *app, const qa_simulation_output *output) {
  if (!qa_remote_application_owns_published_source(app) || qa_remote_application_presentation(app) == NULL) return;
  qa_seat_id seat = qa_remote_application_seat_id(app);
  qa_client_id client = qa_session_client_id(qa_session_seat_client(app->configuration->seat));
  if (output->event_count > app->published_event_capacity) {
    qa_simulation_event *grown = realloc(app->published_events, output->event_count * sizeof *grown);
    if (grown == NULL) qa_range_error("Out of memory");
    app->published_events = grown;
    app->published_event_capacity = output->event_count;
  }
  size_t count = 0;
  for (size_t i = 0; i < output->event_count; i++) {
    if (!qa_event_targets_seat(output->events[i], seat, client)) continue;
    qa_simulation_event event = output->events[i];
    event.audience.kind = "seat";
    event.audience.seat = seat;
    app->published_events[count++] = event;
  }
  qa_simulation_output published = *output;
  published.events = app->published_events;
  published.event_count = count;
  qa_engine_session_publish(app->session, published);
}
void qa_remote_application_disconnect_source(qa_remote_application *app) {
  if (source_connected(app)) qa_session_client_disconnect(remote_client(app));
}
static void release_presentation(void *object) { qa_app_world_seat_presentation_destroy(object); }
void qa_remote_application_clear_source_presentation(qa_remote_application *app) {
  qa_app_world_seat_presentation *presentation = qa_remote_application_presentation(app);
  /* A promoted peer's presentation stays with its seat view (set_presentation(NULL) keeps it there). */
  bool owned = app->primary_peer == NULL;
  qa_remote_application_set_presentation(app, NULL);
  if (presentation == NULL) return;
  /* TS drops the reference (GC); the application frees it at destroy, before the seat UI it was built over. */
  if (owned) qa_remote_arena_add(&app->arena, release_presentation, presentation);
  qa_session_seat *seat = qa_app_world_seat_presentation_local(presentation)->player.seat;
  qa_seat_presentation current;
  if (qa_session_seat_presentation(seat, &current)
      && current.ctx == qa_app_world_seat_presentation_seat(presentation).ctx)
    qa_session_seat_clear_presentation(seat);
}

/* ---- flushClientCommands (TS 1090-1093) -------------------------------- */
void qa_remote_application_flush_client_commands(qa_remote_application *app) {
  if (app->peer_admission != NULL && !app->peer_admission->published)
    qa_remote_application_publish_peer_configuration(app, app->peer_admission);
  qa_remote_application_dispatch_commands(app);
}

/* ---- activateSourceCommands (TS 1119-1121) ----------------------------- */
void qa_remote_application_activate_source_commands(qa_remote_application *app) {
  if (app->client_command_owner == NULL) return;
  for (size_t i = 0; i < app->source_handler_count; i++) {
    const qa_remote_source_handler *entry = &app->source_handlers[i];
    (void)qa_command_buffer_register_engine(app->client_command_owner->commands, entry->name, entry->handler,
        entry->handler_ctx, entry->has_documentation ? &entry->documentation : NULL);
  }
}

/* ---- startupReader (TS 1146-1159) -------------------------------------- */
typedef struct startup_reader_ctx { qa_app_console_script_files *scripts; char **roots; size_t root_count; char *seat_root; } startup_reader_ctx;
static void release_startup_reader(void *object) {
  startup_reader_ctx *ctx = object;
  for (size_t i = 0; i < ctx->root_count; i++) free(ctx->roots[i]);
  free(ctx->roots);
  free(ctx->seat_root);
  free(ctx);
}
static uint8_t *reader_mounted(void *ctx, const char *name, size_t *out_length) {
  startup_reader_ctx *reader = ctx;
  uint8_t *bytes = NULL;
  if (!qa_app_console_script_files_read_mounted_script(reader->scripts, name, &bytes, out_length)) return NULL;
  return bytes;
}
static char *reader_user(void *ctx, const char *name, qa_command_context source) {
  startup_reader_ctx *reader = ctx;
  return qa_app_console_script_files_read(reader->scripts, name, source);
}
static char *keep_root(startup_reader_ctx *ctx, const char *root) {
  char *copy = strdup(root);
  if (copy == NULL) qa_range_error("Out of memory");
  ctx->roots[ctx->root_count++] = copy;
  return copy;
}
void qa_remote_application_startup_reader(qa_remote_application *app, qa_app_console_script_files *scripts,
    qa_app_prepared_scoped_read_fn *out_read, void **out_ctx) {
  const qa_content_catalog *catalog = app->remote_content != NULL ? app->remote_content->catalog
      : app->loaded_content != NULL ? qa_app_loaded_content_catalog(app->loaded_content)
      : qa_app_configuration_content_catalog(app->mounted_content);
  const qa_catalog_discovered_product *product = qa_content_catalog_product(catalog, app->launch_options.product);
  const qa_catalog_discovered_product *base = product->expectation.base_product == NULL ? product
      : qa_content_catalog_product(catalog, product->expectation.base_product);
  startup_reader_ctx *ctx = calloc(1, sizeof *ctx);
  if (ctx == NULL) qa_range_error("Out of memory");
  qa_remote_arena_add(&app->arena, release_startup_reader, ctx);
  ctx->scripts = scripts;
  ctx->roots = calloc(4, sizeof *ctx->roots);
  if (ctx->roots == NULL) qa_range_error("Out of memory");
  const char *base_roots[2], *game_roots[2];
  size_t base_count = 0, game_count = 0;
  /* roots(selected) = [selected.userContent?.root, selected.looseRoot].filter(root => root != null) */
  if (base->user_content != NULL && base->user_content->root != NULL) base_roots[base_count++] = keep_root(ctx, base->user_content->root);
  if (base->loose_root != NULL) base_roots[base_count++] = keep_root(ctx, base->loose_root);
  if (product->user_content != NULL && product->user_content->root != NULL)
    game_roots[game_count++] = keep_root(ctx, product->user_content->root);
  if (product->loose_root != NULL) game_roots[game_count++] = keep_root(ctx, product->loose_root);
  ctx->seat_root = qa_app_console_config_root(app->launch_options.user_content_root);
  if (ctx->seat_root == NULL) qa_range_error("Out of memory");
  qa_app_startup_script_reader_options options;
  memset(&options, 0, sizeof options);
  options.ctx = ctx;
  options.mounted = reader_mounted;
  options.user = reader_user;
  options.base_loose_roots = base_roots; options.base_loose_root_count = base_count;
  options.game_loose_roots = game_roots; options.game_loose_root_count = game_count;
  options.seat_root = ctx->seat_root;
  qa_app_create_startup_script_reader(&options, out_read, out_ctx);
  qa_remote_arena_add(&app->arena, free, *out_ctx); /* the reader's options copy (TS GC) */
}

/* ---- the release targets of a prepared program ------------------------- */
static void program_release_append(void *ctx, const char *text, const qa_command_context *source) {
  qa_app_prepared_client_commands_release_append(ctx, text, source);
}
qa_command_append_target qa_remote_release_commands(qa_app_prepared_profile *profile) {
  qa_command_append_target target = {qa_app_prepared_profile_program(profile), program_release_append};
  return target;
}
/* TS (this.controls !== null) controls.releaseForProfileChange(releaseCommands)
 * else for (const seat of prepared.seats) seat.input.release(performance.now(), releaseCommands) */
static void release_for_profile_change(qa_remote_application *app, qa_app_prepared_startup *prepared,
    const qa_command_append_target *release) {
  if (app->controls != NULL) {
    qa_app_application_input_release_for_profile_change(app->controls, release);
    return;
  }
  size_t count = 0;
  const qa_app_prepared_seat *seats = qa_app_prepared_startup_seats(prepared, &count);
  double now = qa_app_remote_access_now_milliseconds(NULL);
  for (size_t i = 0; i < count; i++) qa_seat_input_release_to(seats[i].input, now, release);
}
/* TS client.activateFrontend({ releaseCommands, publish }). */
static void activate_frontend(qa_app_client_bootstrap *client, const qa_command_append_target *release,
    void (*publish)(void *ctx), void *ctx) {
  qa_app_client_frontend_configuration configuration;
  memset(&configuration, 0, sizeof configuration);
  configuration.release_commands = qa_app_prepared_client_commands_buffer(release->ctx);
  configuration.release_target = release;
  configuration.ctx = ctx;
  configuration.publish = publish;
  client->activate_frontend(client->bootstrap_ctx, &configuration);
}

/* ---- adoptUnifiedProfile (TS 1161-1292) -------------------------------- */
typedef struct unified_profile_state {
  qa_remote_application *app;
  qa_app_loaded_content *content;
  const char *mode;
  qa_app_assert_current_fn assert_source; void *assert_ctx;
  qa_app_options options;              /* applicationOptionsForRecipe result (owned) */
  qa_app_demo_family family;
  qa_remote_configuration *previous;
  qa_app_main_mount_lease release; bool released;
  qa_app_configuration_content *mounted;
  qa_remote_configuration *candidate;
  qa_app_key_profile *key_profile;
  bool published;
  qa_app_console_script_files *old_scripts;
  qa_command_append_target release_target;
} unified_profile_state;
static void unified_assert_current(unified_profile_state *state) {
  const char *error = state->assert_source(state->assert_ctx);
  if (error != NULL) qa_raise("Error", 0, 0, "%s", error);
  if (state->app->closed || state->app->closing) qa_raise("Error", 0, 0, "Unified profile loading was cancelled");
}
static void unified_commit(void *ctx) {
  unified_profile_state *state = ctx;
  qa_remote_application *app = state->app;
  state->published = true;
  qa_remote_application_release_settings(app);
  app->key_profile = state->key_profile;
  app->key_profile_published = false;
  qa_remote_application_publish_configuration(app, state->candidate);
  qa_app_options_for_recipe_free(&app->launch_options);
  app->launch_options = state->options;
  memset(&state->options, 0, sizeof state->options);
  app->family = state->family;
  qa_remote_command_owner *owner = app->client_command_owner;
  if (owner == NULL) qa_raise("Error", 0, 0, "Unified profile lost its command owner");
  if (app->controls != NULL
      && qa_app_application_input_dialect(app->controls) == qa_cvar_registry_dialect(owner->movement != NULL ? owner->movement : owner->cvars)
      && qa_cvar_registry_dialect(state->previous->source) == qa_cvar_registry_dialect(owner->cvars)) {
    qa_app_input_command_owner adopted = qa_remote_input_owner(owner);
    qa_app_application_input_adopt_client_source(app->controls, &adopted);
  }
  if (app->ownership.borrowed) {
    app->ownership.client->configuration_current.scripts = qa_app_prepared_profile_scripts(state->candidate->profile);
    app->ownership.client->configuration_current.options = &app->launch_options;
  }
  app->reset_frame_elapsed = true;
}
static void unified_body(void *ctx) {
  unified_profile_state *state = ctx;
  qa_remote_application *app = state->app;
  qa_remote_application_prepare_retirement(app);
  unified_assert_current(state);
  state->release = qa_app_loaded_content_retain_main_mounts(state->content);
  /* mounted / selected: the content's catalog and mounts with the recipe's selection */
  state->mounted = qa_remote_unified_configuration_content(app, state->content, state->release);
  state->old_scripts = app->client_command_owner != NULL ? app->client_command_owner->scripts : NULL;
  state->candidate = qa_remote_prepare_configuration(&state->options, state->mounted, app->session, app->image_settings,
      app->ownership, &app->host.base, qa_remote_cheats_never, NULL, state->previous, state->mounted, &app->arena);
  state->key_profile = qa_app_keys_prepare(app->keys, &state->options, qa_app_loaded_content_catalog(state->content),
      state->candidate->source);
  unified_assert_current(state);
  qa_app_prepared_client_commands_validate_publication(qa_app_prepared_profile_program(state->candidate->profile));
  qa_app_image_client_settings images = {state->candidate->images.settings, state->candidate->images.transfer};
  qa_app_image_client_settings_validate_publication(&images);
  qa_remote_configuration_validate_source(state->candidate);
  state->release_target = qa_remote_release_commands(state->candidate->profile);
  if (app->ownership.borrowed) activate_frontend(app->ownership.client, &state->release_target, unified_commit, state);
  else {
    release_for_profile_change(app, state->previous->prepared, &state->release_target);
    unified_commit(state);
  }
}
static void unified_retire_scripts(void *ctx) {
  unified_profile_state *state = ctx;
  if (state->old_scripts != NULL) qa_app_console_script_files_close(state->old_scripts);
}
static void unified_retire_images(void *ctx) {
  unified_profile_state *state = ctx;
  qa_app_image_settings_close(state->previous->images.settings);
}
static bool unified_retire(unified_profile_state *state) {
  qa_caught_error caught;
  bool failed = false;
  if (!qa_try(unified_retire_scripts, state, &caught)) failed = true;
  if (!qa_try(unified_retire_images, state, &caught)) failed = true;
  return !failed;
}
static void unified_discard_identities(void *ctx) {
  unified_profile_state *state = ctx;
  if (state->candidate != NULL) qa_app_remote_seat_identities_discard(state->candidate->identities);
}
static void unified_close_mounted(void *ctx) {
  unified_profile_state *state = ctx;
  if (state->candidate == NULL) {
    /* TS `await close()`: the mounted content's close, or the lease itself before the content existed. */
    if (state->mounted != NULL) qa_app_configuration_content_close(state->mounted);
    else if (!state->released && state->release.release != NULL) {
      state->released = true;
      state->release.release(state->release.ctx);
    }
  } else qa_app_console_script_files_close(qa_app_prepared_profile_scripts(state->candidate->profile));
}
static void unified_close_images(void *ctx) {
  unified_profile_state *state = ctx;
  if (state->candidate != NULL) qa_app_image_settings_close(state->candidate->images.settings);
}
void qa_remote_application_adopt_unified_profile(qa_remote_application *app, qa_app_loaded_content *content,
    const char *mode, qa_app_assert_current_fn assert_current, void *assert_ctx) {
  unified_profile_state state;
  memset(&state, 0, sizeof state);
  state.app = app;
  state.content = content;
  state.mode = mode;
  state.assert_source = assert_current;
  state.assert_ctx = assert_ctx;
  unified_assert_current(&state);
  /* const { remoteContent, q3Product, ...retained } = this.options;
   * applicationOptionsForRecipe({ ...retained, mode, ...q3Product }, content) */
  qa_app_options retained = app->launch_options;
  retained.has_remote_content = false;
  memset(&retained.remote_content, 0, sizeof retained.remote_content);
  retained.has_q3_product = qa_app_loaded_content_q3_product(content, &retained.q3_product);
  retained.mode = mode;
  const qa_executable_recipe *recipe = qa_app_loaded_content_recipe(content);
  state.options = qa_app_application_options_for_recipe(&retained, qa_app_loaded_content_catalog(content), recipe);
  qa_cleanup options_guard;
  qa_cleanup_push(&options_guard, (void (*)(void *))qa_app_options_for_recipe_free, &state.options);
  bool has_dialect = false;
  qa_clock_profile_kind source_dialect = QA_CLOCK_Q3;
  for (size_t i = 0; i < recipe->timing_count; i++)
    if (recipe->timing[i].provider != NULL && recipe->engine_behavior.provider != NULL
        && strcmp(recipe->timing[i].provider, recipe->engine_behavior.provider) == 0) {
      source_dialect = recipe->timing[i].clock.kind;
      has_dialect = true;
      break;
    }
  qa_command_dialect movement = qa_app_movement_dialect(&state.options, recipe);
  state.previous = app->configuration;
  qa_app_application_world world = qa_app_loaded_content_world(content);
  state.family = has_dialect && source_dialect == QA_CLOCK_Q1_QUAKEWORLD ? QA_APP_DEMO_QW
      : strcmp(world.kind, "q1-bsp") == 0 ? QA_APP_DEMO_Q1 : strcmp(world.kind, "q2-bsp") == 0 ? QA_APP_DEMO_Q2 : QA_APP_DEMO_Q3;
  const qa_app_options *previous_options = qa_app_prepared_profile_options(state.previous->profile);
  if (strcmp(previous_options->product, state.options.product) == 0
      && has_dialect && qa_cvar_registry_dialect(state.previous->source) == qa_remote_dialect_for_clock(source_dialect)
      && qa_cvar_registry_dialect(qa_app_prepared_profile_movement(state.previous->profile)) == movement
      && previous_options->character == state.options.character
      && strcmp(previous_options->character_model, state.options.character_model) == 0) {
    qa_cleanup_pop(&options_guard);
    qa_app_options_for_recipe_free(&app->launch_options);
    app->launch_options = state.options;
    app->family = state.family;
    return;
  }
  qa_caught_error caught;
  if (!qa_try(unified_body, &state, &caught)) {
    bool aggregate = false;
    qa_caught_error cleanup;
    if (!state.published) {
      if (!qa_try(unified_discard_identities, &state, &cleanup)) aggregate = true;
      if (!qa_try(unified_close_mounted, &state, &cleanup)) aggregate = true;
      if (!qa_try(unified_close_images, &state, &cleanup)) aggregate = true;
      if (state.key_profile != NULL) qa_app_key_profile_destroy(state.key_profile);
    } else if (!unified_retire(&state)) aggregate = true;
    if (aggregate) qa_raise("AggregateError", 0, 0, "Unified profile publication failed");
    qa_rethrow(&caught);
  }
  qa_cleanup_pop(&options_guard);
  qa_app_options_for_recipe_free(&state.options);
  if (!unified_retire(&state)) qa_raise("AggregateError", 0, 0, "Unified profile retirement failed");
}

/* ---- publishPeerConfiguration (TS 1294-1445) --------------------------- */
typedef struct peer_source { qa_seat_id id; qa_cvar_registry *registry; qa_cvar_transfer *transfer; } peer_source;
typedef struct peer_state {
  qa_remote_application *app;
  qa_remote_peer_admission *request;
  qa_app_configuration_content *mounted;
  qa_app_image_client_settings images; bool has_images;
  qa_app_prepared_profile *profile;
  bool published;
  peer_source *sources; size_t source_count;
  qa_cvar_transfer *socks;
  qa_remote_configuration *configuration;
  qa_remote_configuration *previous;
  qa_app_key_profile *key_profile;
  qa_app_remote_content_mounts *old_content;
  qa_app_console_script_files *old_scripts;
  qa_command_append_target release_target;
  /* the route closures' storage */
  qa_cvar_registry **visible; size_t visible_capacity;
} peer_state;
static void peer_assert(peer_state *state) {
  const char *error = state->request->assert_current(state->request->assert_ctx);
  if (error != NULL) qa_raise("Error", 0, 0, "%s", error);
}
/* TS remap(registry) */
static qa_cvar_registry *peer_remap(const peer_state *state, qa_cvar_registry *registry) {
  qa_cvar_registry *socks = qa_app_client_socks_settings_cvars(state->app->socks_settings);
  if (registry == socks) return state->published ? registry : state->socks->cvars;
  if (state->published)
    for (size_t i = 0; i < state->source_count; i++)
      if (registry == state->sources[i].transfer->cvars) return state->sources[i].registry;
  return registry;
}
typedef struct peer_route { peer_state *state; qa_command_cvar_routing routed; qa_cvar_registry **visible; size_t capacity; } peer_route;
static void release_peer_route(void *object) { peer_route *route = object; free(route->visible); free(route); }
static qa_cvar_registry *peer_route_owner(void *ctx, const char *name, qa_command_context source) {
  peer_route *route = ctx;
  return peer_remap(route->state, route->routed.owner(route->routed.ctx, name, source));
}
static qa_cvar_registry *const *peer_route_visible(void *ctx, qa_command_context source, size_t *out_count) {
  peer_route *route = ctx;
  size_t count = 0;
  qa_cvar_registry *const *visible = route->routed.visible(route->routed.ctx, source, &count);
  if (count > route->capacity) {
    qa_cvar_registry **grown = realloc(route->visible, count * sizeof *grown);
    if (grown == NULL) qa_range_error("Out of memory");
    route->visible = grown;
    route->capacity = count;
  }
  for (size_t i = 0; i < count; i++) route->visible[i] = peer_remap(route->state, visible[i]);
  *out_count = count;
  return route->visible;
}
/* TS route(base): socks.route(base) remapped. The peer state lives as long as the routing (arena). */
static qa_command_cvar_routing peer_route_of(void *ctx, qa_command_cvar_routing base) {
  peer_state *state = ctx;
  peer_route *route = calloc(1, sizeof *route);
  if (route == NULL) qa_range_error("Out of memory");
  qa_remote_arena_add(&state->app->arena, release_peer_route, route);
  route->state = state;
  route->routed = qa_app_client_socks_settings_route(state->app->socks_settings, base);
  qa_command_cvar_routing routing = {route, peer_route_owner, peer_route_visible};
  return routing;
}
static void peer_validate_source(void *ctx) {
  peer_state *state = ctx;
  for (size_t i = 0; i < state->source_count; i++) state->sources[i].transfer->validate_publication(state->sources[i].transfer->ctx);
  state->socks->validate_publication(state->socks->ctx);
}
static void peer_commit(void *ctx) {
  peer_state *state = ctx;
  qa_remote_application *app = state->app;
  app->key_profile = state->key_profile;
  app->key_profile_published = false;
  qa_remote_application_publish_configuration(app, state->configuration);
  app->remote_content = state->request->content;
  qa_app_options_destroy(&app->launch_options);
  qa_remote_options_copy(&state->request->options, &app->launch_options);
  if (app->ownership.borrowed) {
    state->old_scripts = app->ownership.client->configuration_current.scripts;
    app->ownership.client->configuration_current.scripts = qa_app_prepared_profile_scripts(state->configuration->profile);
    app->ownership.client->configuration_current.options = &app->launch_options;
  }
  state->request->published = true;
  app->reset_frame_elapsed = true;
}
static void release_peer_state(void *object) {
  peer_state *state = object;
  free(state->sources);
  free(state->visible);
  free(state);
}
static void peer_body(void *ctx) {
  peer_state *state = ctx;
  qa_remote_application *app = state->app;
  qa_remote_peer_admission *request = state->request;
  peer_assert(state);
  qa_remote_application_prepare_retirement(app);
  peer_assert(state);
  state->previous = app->configuration;
  qa_remote_command_owner *owner = app->client_command_owner;
  if (owner == NULL) qa_raise("Error", 0, 0, "Peer configuration has no retained command owner");
  state->mounted = qa_app_open_remote_application_content_native(&request->options);
  if (state->mounted == NULL) qa_range_error("Out of memory");
  peer_assert(state);
  state->images = qa_app_image_settings_prepare_client_settings(app->image_settings);
  state->has_images = true;
  const qa_remote_configuration *previous = state->previous;
  state->sources = calloc(previous->seat_source_count + 1, sizeof *state->sources);
  if (state->sources == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < previous->seat_source_count; i++) {
    peer_source *source = &state->sources[state->source_count++];
    source->id = previous->seat_sources[i].id;
    source->registry = previous->seat_sources[i].cvars;
    source->transfer = qa_cvar_registry_prepare_candidate(source->registry, qa_remote_host_print, &app->host.base, true);
    if (source->transfer == NULL) qa_range_error("Out of memory");
    qa_remote_arena_add(&app->arena, qa_remote_release_transfer, source->transfer);
  }
  peer_source *primary = NULL;
  for (size_t i = 0; i < state->source_count; i++)
    if (qa_remote_seat_id_equal(state->sources[i].id, qa_remote_application_seat_id(app))) primary = &state->sources[i];
  if (primary == NULL) qa_raise("Error", 0, 0, "Peer configuration lost its primary source registry");
  state->socks = qa_cvar_registry_prepare_candidate(qa_app_client_socks_settings_cvars(app->socks_settings),
      qa_remote_host_print, &app->host.base, true);
  if (state->socks == NULL) qa_range_error("Out of memory");
  qa_remote_arena_add(&app->arena, qa_remote_release_transfer, state->socks);
  qa_app_options key_options = request->options;
  qa_q3_application_product q3_product;
  if (qa_app_configuration_content_q3_value(state->mounted, &q3_product)) {
    key_options.has_q3_product = true;
    key_options.q3_product = q3_product;
  }
  state->key_profile = qa_app_keys_prepare(app->keys, &key_options, qa_app_configuration_content_catalog(state->mounted), owner->cvars);
  peer_assert(state);
  const qa_catalog_discovered_product *product = qa_content_catalog_require(qa_app_configuration_content_catalog(state->mounted),
      request->options.product);
  qa_config_store *settings = qa_remote_product_settings(&app->arena, product, request->options.user_content_root);
  const qa_app_prepared_seat *seat = qa_remote_prepared_seat(previous->prepared, qa_remote_application_seat_id(app));
  if (seat == NULL) qa_raise("Error", 0, 0, "Peer configuration lost its retained seat");
  /* the profile's arguments (TS prepareProfileConfiguration({ ... })) */
  qa_remote_arena *arena = &app->arena;
  qa_app_profile_seat *inputs = calloc(previous->seat_count + 1, sizeof *inputs);
  if (inputs == NULL) qa_range_error("Out of memory");
  qa_remote_arena_add(arena, free, inputs);
  for (size_t i = 0; i < previous->seat_count; i++) {
    const qa_app_prepared_seat *prepared = qa_remote_prepared_seat(previous->prepared, qa_session_seat_id(previous->seats[i]));
    if (prepared == NULL) qa_raise("Error", 0, 0, "Peer configuration lost a retained seat input");
    inputs[i].seat = previous->seats[i];
    inputs[i].input = prepared->input;
  }
  qa_cvar_registry **seat_cvars = calloc(state->source_count + 1, sizeof *seat_cvars);
  if (seat_cvars == NULL) qa_range_error("Out of memory");
  qa_remote_arena_add(arena, free, seat_cvars);
  /* seatCvars: Map<SeatId, CvarRegistry> over the staged sources, read per retained seat */
  for (size_t i = 0; i < previous->seat_count; i++)
    for (size_t k = 0; k < state->source_count; k++)
      if (qa_remote_seat_id_equal(state->sources[k].id, qa_session_seat_id(previous->seats[i])))
        seat_cvars[i] = state->sources[k].transfer->cvars;
  qa_app_profile_client_source *client = calloc(1, sizeof *client);
  if (client == NULL) qa_range_error("Out of memory");
  qa_remote_arena_add(arena, free, client);
  client->fresh_input = false; /* inputState: "retained" */
  client->cvars = primary->transfer->cvars;
  char *archive = qa_config_store_load_text(settings, "settings/client.cfg", NULL);
  qa_remote_arena_add(arena, free, archive);
  client->archive_text = archive;
  client->seat_cvars = seat_cvars;
  client->route = peer_route_of;
  client->route_ctx = state;
  qa_app_configuration_content *content = qa_app_remote_configuration_content_native(&request->options, state->mounted);
  if (content == NULL) qa_range_error("Out of memory");
  qa_app_prepare_profile_args *args = calloc(1, sizeof *args);
  if (args == NULL) qa_range_error("Out of memory");
  qa_remote_arena_add(arena, free, args);
  args->prepared = previous->prepared;
  args->client_source = client;
  args->seats = inputs; args->seat_count = previous->seat_count;
  args->content = content;
  args->catalog = qa_app_configuration_content_catalog(state->mounted);
  args->settings = settings;
  args->shared = qa_app_image_settings_cvars(state->images.settings);
  args->scripts = qa_remote_profile_scripts(&request->options, content, settings, arena);
  args->print = qa_remote_host_print; args->print_ctx = &app->host.base;
  args->default_capacity = 1;
  args->next_frame = qa_remote_next_frame;
  qa_app_options *profile_options = calloc(1, sizeof *profile_options);
  if (profile_options == NULL) qa_range_error("Out of memory");
  qa_remote_arena_add(arena, free, profile_options);
  *profile_options = request->options;
  state->profile = qa_app_prepare_profile_configuration(profile_options, args);
  if (state->profile == NULL) qa_range_error("Out of memory");
  qa_remote_arena_add(arena, qa_remote_release_profile, state->profile);
  peer_assert(state);
  qa_remote_configuration *configuration = calloc(1, sizeof *configuration);
  if (configuration == NULL) qa_range_error("Out of memory");
  qa_remote_arena_add(arena, qa_remote_release_configuration_record, configuration);
  *configuration = *previous;
  configuration->seats = qa_remote_copy_seats(previous->seats, previous->seat_count);
  configuration->seat_sources = qa_remote_copy_seat_sources(previous->seat_sources, previous->seat_source_count);
  configuration->download_permissions = qa_remote_copy_permissions(previous->download_permissions,
      previous->download_permission_count);
  configuration->transfers = NULL; configuration->transfer_count = 0;
  configuration->visible = NULL; configuration->visible_capacity = 0;
  configuration->profile = state->profile;
  configuration->settings = settings;
  configuration->socks = app->socks_settings;
  configuration->images.settings = state->images.settings;
  configuration->images.transfer = state->images.transfer;
  configuration->source = owner->cvars;
  configuration->routing = peer_route_of(state, qa_app_console_routing_view(qa_app_prepared_profile_routing(state->profile)));
  configuration->staged_socks = state->socks;
  configuration->peer = state;
  state->configuration = configuration;
  qa_app_prepared_client_commands_validate_publication(qa_app_prepared_profile_program(state->profile));
  qa_app_image_client_settings_validate_publication(&state->images);
  qa_remote_configuration_validate_source(configuration);
  qa_remote_application_release_settings(app);
  state->old_content = app->remote_content;
  state->old_scripts = owner->scripts;
  state->release_target = qa_remote_release_commands(state->profile);
  if (app->ownership.borrowed) activate_frontend(app->ownership.client, &state->release_target, peer_commit, state);
  else {
    release_for_profile_change(app, previous->prepared, &state->release_target);
    peer_commit(state);
  }
}
/* RemoteConfiguration.validateSource / publishSource of a peer configuration (TS 1393-1401). */
void qa_remote_peer_configuration_validate(void *peer) { peer_validate_source(peer); }
void qa_remote_peer_configuration_publish(void *ctx) {
  peer_state *state = ctx;
  for (size_t i = 0; i < state->source_count; i++) state->sources[i].transfer->publish(state->sources[i].transfer->ctx);
  state->socks->publish(state->socks->ctx);
  state->published = true;
}
qa_cvar_registry *qa_remote_peer_configuration_transport_cvars(void *ctx) {
  peer_state *state = ctx;
  return state->published ? qa_app_client_socks_settings_cvars(state->app->socks_settings) : state->socks->cvars;
}
static void peer_retire_view(void *ctx) {
  qa_remote_application *app = ((peer_state *)ctx)->app;
  qa_cvar_registry *cvars = qa_app_image_settings_cvars(app->image_settings);
  qa_app_view_settings_set_field_of_view(app->view_settings, qa_cvar_registry_variable_value(cvars, "fov"));
  qa_app_view_settings_bind_cvars(app->view_settings, cvars);
  app->release_view_cvars = true;
}
static void peer_retire_content(void *ctx) {
  peer_state *state = ctx;
  if (state->old_content != NULL) qa_app_remote_content_mounts_close(state->old_content);
}
static void peer_retire_scripts(void *ctx) {
  peer_state *state = ctx;
  qa_app_console_script_files_close(state->old_scripts);
}
static void peer_retire_images(void *ctx) {
  peer_state *state = ctx;
  qa_app_image_settings_close(state->previous->images.settings);
}
static void peer_close_profile(void *ctx) {
  peer_state *state = ctx;
  if (state->profile == NULL) { if (state->mounted != NULL) qa_app_configuration_content_close(state->mounted); }
  else qa_app_console_script_files_close(qa_app_prepared_profile_scripts(state->profile));
}
static void peer_close_images(void *ctx) {
  peer_state *state = ctx;
  if (state->has_images) qa_app_image_settings_close(state->images.settings);
}
void qa_remote_application_publish_peer_configuration(qa_remote_application *app, qa_remote_peer_admission *request) {
  peer_state *state = calloc(1, sizeof *state);
  if (state == NULL) qa_range_error("Out of memory");
  qa_remote_arena_add(&app->arena, release_peer_state, state);
  state->app = app;
  state->request = request;
  qa_caught_error caught;
  if (!qa_try(peer_body, state, &caught)) {
    /* request.suspension?.cancel(error): the synchronous admission has no suspended operation */
    bool aggregate = false;
    qa_caught_error cleanup;
    if (!state->published) {
      if (!qa_try(peer_close_profile, state, &cleanup)) aggregate = true;
      if (!qa_try(peer_close_images, state, &cleanup)) aggregate = true;
      if (state->key_profile != NULL && app->key_profile != state->key_profile) qa_app_key_profile_destroy(state->key_profile);
    }
    if (aggregate) qa_raise("AggregateError", 0, 0, "Peer configuration preparation failed");
    qa_rethrow(&caught);
  }
  void (*retire[])(void *) = {peer_retire_view, peer_retire_content, peer_retire_scripts, peer_retire_images};
  bool failed = false;
  for (size_t i = 0; i < sizeof retire / sizeof retire[0]; i++)
    if (!qa_try(retire[i], state, &caught)) failed = true;
  if (failed) qa_raise("AggregateError", 0, 0, "Peer configuration retirement failed");
}

/* ---- advanceSource (TS 1447-1477) --------------------------------------- */
/* TS races the operation's completion against a frame boundary that a peer
 * directory admission raises. The C operation runs synchronously to its end:
 * a peer admission requested while it runs publishes at once (selectRemoteContent),
 * so no boundary is left pending across frames. */
typedef struct advance_call { void (*operation)(void *ctx); void *ctx; } advance_call;
static void advance_body(void *ctx) {
  advance_call *call = ctx;
  call->operation(call->ctx);
}
bool qa_remote_application_advance_source(qa_remote_application *app, void (*operation)(void *ctx), void *ctx) {
  qa_remote_pending_advance pending;
  memset(&pending, 0, sizeof pending);
  if (app->pending_advance == NULL) app->pending_advance = &pending;
  else if (app->peer_admission != NULL) {
    if (!app->peer_admission->published) return false;
    app->peer_admission = NULL;
  }
  qa_remote_pending_advance *current = app->pending_advance;
  advance_call call = {operation, ctx};
  current->failed = !qa_try(advance_body, &call, &current->error);
  current->complete = !current->failed;
  if (current->boundary) {
    current->boundary = false;
    return false;
  }
  if (app->pending_advance == &pending) app->pending_advance = NULL;
  if (current->failed) qa_rethrow(&current->error);
  return true;
}

/* ---- presentLoadingFrame (TS 1479-1487) -------------------------------- */
void qa_remote_application_present_loading_frame(qa_remote_application *app) {
  if (app->ownership.borrowed) return;
  qa_render_command commands[2];
  memset(commands, 0, sizeof commands);
  commands[0].kind = QA_RENDER_COMMAND_DRAW_BUFFER;
  commands[0].buffer = "back";
  commands[0].clear = true;
  commands[1].kind = QA_RENDER_COMMAND_SWAP_BUFFERS;
  qa_remote_renderer_execute(app, commands, 2);
  if (app->capture != NULL) qa_app_capture_drain(app->capture);
}

/* ---- publishConfiguration (TS 1489-1565) ------------------------------- */
typedef struct published_seat { qa_app_prepared_seat seat; double order; } published_seat;
static void queue_forward(void *ctx, const char *name, const char *const *args, size_t arg_count, qa_command_context source) {
  qa_remote_application *app = ctx;
  const qa_command_origin *origin = &source.origin;
  while (origin->kind == QA_COMMAND_ORIGIN_SCRIPT && origin->caller != NULL) origin = origin->caller;
  qa_seat_id seat = origin->seat;
  qa_remote_application_queue_command(app, name, args, arg_count, origin->kind == QA_COMMAND_ORIGIN_LOCAL_SEAT ? &seat : NULL,
      &source);
}
static void request_forward(void *ctx, const qa_app_configuration_command_request *request) {
  qa_remote_application_queue_command(ctx, request->name, request->arguments, request->argument_count,
      request->has_seat ? &request->seat : NULL, &request->source);
}
void qa_remote_application_publish_configuration(qa_remote_application *app, qa_remote_configuration *configuration) {
  qa_remote_command_owner *owner = app->client_command_owner;
  if (owner == NULL) qa_raise("Error", 0, 0, "Remote configuration has no command owner");
  qa_app_prepared_startup *prepared = configuration->prepared;
  qa_app_prepared_profile *profile = configuration->profile;
  qa_app_image_client_settings images = {configuration->images.settings, configuration->images.transfer};
  qa_app_prepared_client_commands_validate_publication(qa_app_prepared_profile_program(profile));
  qa_app_image_client_settings_validate_publication(&images);
  qa_remote_configuration_validate_source(configuration);
  qa_remote_configuration_publish_source(configuration);
  qa_app_image_client_settings_publish(&images);
  qa_app_prepared_client_commands_publish(qa_app_prepared_profile_program(profile));
  size_t prepared_count = 0, profile_count = 0;
  const qa_app_prepared_seat *prepared_seats = qa_app_prepared_startup_seats(prepared, &prepared_count);
  const qa_app_prepared_seat_configuration *profile_seats = qa_app_prepared_profile_seats(profile, &profile_count);
  published_seat *seats = calloc(prepared_count + profile_count + 1, sizeof *seats);
  if (seats == NULL) qa_range_error("Out of memory");
  qa_cleanup guard;
  qa_cleanup_push(&guard, free, seats);
  size_t count = 0;
  for (size_t i = 0; i < prepared_count; i++) {
    bool replaced = false;
    for (size_t j = 0; j < profile_count; j++)
      if (qa_remote_seat_id_equal(profile_seats[j].id, prepared_seats[i].id)) replaced = true;
    if (!replaced) seats[count++].seat = prepared_seats[i];
  }
  for (size_t j = 0; j < profile_count; j++) {
    qa_cvar_registry *cvars = qa_remote_configuration_seat_source(configuration, profile_seats[j].id);
    if (cvars == NULL) qa_raise("Error", 0, 0, "Published remote configuration lost its source registry");
    qa_app_prepared_seat *seat = &seats[count++].seat;
    memset(seat, 0, sizeof *seat);
    seat->context = profile_seats[j].context;
    seat->id = profile_seats[j].id;
    seat->cvars = cvars;
    seat->mouse = profile_seats[j].mouse;
    seat->input = qa_app_prepared_profile_seat_input(profile, j);
  }
  for (size_t i = 0; i < count; i++) {
    double order = 4 + (double)seats[i].seat.id.index;
    for (size_t k = 0; k < configuration->seat_count; k++)
      if (qa_remote_seat_id_equal(qa_session_seat_id(configuration->seats[k]), seats[i].seat.id)) { order = (double)k; break; }
    seats[i].order = order;
  }
  /* seats.sort((a, b) => order(a.id) - order(b.id)): stable insertion sort */
  for (size_t i = 1; i < count; i++) {
    published_seat moving = seats[i];
    size_t at = i;
    while (at > 0 && seats[at - 1].order > moving.order) { seats[at] = seats[at - 1]; at--; }
    seats[at] = moving;
  }
  qa_app_prepared_seat *rows = calloc(count + 1, sizeof *rows);
  qa_seat_id *active = calloc(configuration->seat_count + 1, sizeof *active);
  if (rows == NULL || active == NULL) { free(rows); free(active); qa_range_error("Out of memory"); }
  for (size_t i = 0; i < count; i++) rows[i] = seats[i].seat;
  for (size_t i = 0; i < configuration->seat_count; i++) active[i] = qa_session_seat_id(configuration->seats[i]);
  qa_app_prepared_startup_publish_seats(prepared, rows, count, active, configuration->seat_count, true);
  free(rows);
  free(active);
  qa_cleanup_pop(&guard);
  free(seats);
  size_t added_count = 0;
  const qa_app_remote_seat_identity *added = qa_app_remote_seat_identities_added(configuration->identities, &added_count);
  for (size_t j = 0; j < profile_count; j++) {
    if (profile_seats[j].profile == NULL) continue;
    for (size_t k = 0; k < added_count; k++)
      if (qa_remote_seat_id_equal(qa_session_seat_id(added[k].seat), profile_seats[j].id))
        qa_seat_input_set_gamepad_tuning(qa_app_prepared_profile_seat_input(profile, j), &profile_seats[j].profile->gamepad);
  }
  size_t choice_count = qa_app_prepared_profile_binding_choices(profile, NULL, 0);
  qa_app_profile_binding_choice *choices = calloc(choice_count + 1, sizeof *choices);
  if (choices == NULL) qa_range_error("Out of memory");
  choice_count = qa_app_prepared_profile_binding_choices(profile, choices, choice_count);
  for (size_t i = 0; i < choice_count; i++) {
    if (qa_remote_prepared_seat(prepared, choices[i].id) == NULL) {
      free(choices);
      qa_raise("Error", 0, 0, "Published remote configuration lost its seat");
    }
    (void)qa_app_prepared_startup_clear_overridden_keys(prepared, choices[i].id);
    bool has_authored = false;
    const qa_input_binding *authored = NULL;
    size_t authored_count = 0;
    {
      /* keep the seat's authored bindings: TS only writes selectedBindings and allBindingsChosen */
      bool all = false, has_selected = false;
      char *const *keys = NULL;
      size_t key_count = 0, selected_count = 0;
      const qa_input_binding *selected = NULL;
      (void)qa_app_prepared_startup_seat_choices(prepared, choices[i].id, &all, &keys, &key_count, &has_selected, &selected,
          &selected_count, &authored, &authored_count);
      has_authored = authored != NULL;
    }
    (void)qa_app_prepared_startup_publish_seat_choices(prepared, choices[i].id, choices[i].all_bindings_chosen,
        choices[i].selected_bindings != NULL, choices[i].selected_bindings, choices[i].selected_count, has_authored, authored,
        authored_count, (const char *const *)choices[i].overridden_keys, choices[i].overridden_count);
  }
  free(choices);
  qa_app_prepared_owners owners;
  memset(&owners, 0, sizeof owners);
  owners.source = configuration->source;
  owners.movement = qa_app_prepared_profile_movement(profile);
  owners.fallback = configuration->source;
  owners.scripts = qa_app_prepared_profile_scripts(profile);
  owners.read = qa_app_prepared_profile_read(profile, &owners.read_ctx);
  qa_app_prepared_startup_adopt(prepared, configuration->routing, queue_forward, app, &owners);
  qa_app_prepared_profile_forward_commands(profile, request_forward, app);
  size_t request_count = 0;
  const qa_app_configuration_command_request *requests = qa_app_prepared_profile_requests(profile, &request_count);
  for (size_t i = 0; i < request_count; i++) request_forward(app, &requests[i]);
  qa_app_prepared_profile_publish_continuation(profile, prepared);
  if (app->ownership.borrowed) {
    qa_app_client_bootstrap *client = app->ownership.client;
    size_t selected_count = 0;
    const qa_app_remote_seat_identity *selected = qa_app_remote_seat_identities_selected(configuration->identities, &selected_count);
    for (size_t i = 0; i < selected_count; i++) {
      if (qa_remote_prepared_seat(prepared, qa_session_seat_id(selected[i].seat)) == NULL)
        qa_raise("Error", 0, 0, "Remote identity publication lost its prepared input");
      qa_app_client_bootstrap_seat entry = {selected[i].client, selected[i].seat, qa_session_seat_id(selected[i].seat)};
      size_t index = client->local_count;
      for (size_t k = 0; k < client->local_count; k++) if (client->locals[k].seat == selected[i].seat) { index = k; break; }
      if (index < client->local_count) client->locals[index] = entry;
      else {
        if (client->local_count == client->local_capacity) {
          size_t capacity = client->local_capacity == 0 ? 4 : client->local_capacity * 2;
          qa_app_client_bootstrap_seat *grown = realloc(client->locals, capacity * sizeof *grown);
          if (grown == NULL) qa_range_error("Out of memory");
          client->locals = grown;
          client->local_capacity = capacity;
        }
        client->locals[client->local_count++] = entry;
      }
    }
  }
  app->configuration = configuration;
  for (size_t i = 0; i < app->remote_seat_count; i++) {
    qa_remote_seat_peer *peer = app->remote_seats[i];
    qa_cvar_registry *cvars = qa_remote_configuration_seat_source(configuration, qa_session_seat_id(peer->seat));
    if (cvars != NULL) qa_app_remote_seat_channel_adopt_cvars(peer->channel, cvars);
  }
  qa_app_keys_publish(app->keys, app->key_profile, configuration->source);
  app->key_profile_published = true;
  const qa_app_prepared_seat *mouse_seat = qa_remote_prepared_seat(prepared, qa_remote_application_seat_id(app));
  if (mouse_seat == NULL) qa_raise("Error", 0, 0, "Remote publication lost its mouse owner");
  owner->cvars = configuration->source;
  owner->movement = qa_remote_network_is(qa_app_prepared_profile_options(profile), "unified-client")
      ? qa_app_prepared_profile_movement(profile) : configuration->source;
  owner->fallback = configuration->source;
  owner->routing = configuration->routing;
  owner->scripts = qa_app_prepared_profile_scripts(profile);
  owner->input_settings = (qa_mouse_settings *)&mouse_seat->mouse;
  app->input_config = configuration->settings;
  app->client_config = configuration->settings;
  app->configuration_published = true;
}

/* ---- publishConnecting (TS 1567-1634) ---------------------------------- */
typedef struct connecting_state {
  qa_remote_application *app;
  qa_app_client_bootstrap *client;
  const qa_app_client_source_lifetime *previous;
  bool has_retired; qa_session_resource retired;
  qa_session_connection *old_connection;
  qa_session_connection **old_seats; size_t old_seat_count;
  qa_app_console_script_files *retired_configuration;
  qa_command_append_target release_target;
} connecting_state;
static void connecting_publish(void *ctx) {
  connecting_state *state = ctx;
  qa_remote_application_publish_configuration(state->app, state->app->configuration);
}
static void connecting_body(void *ctx) {
  connecting_state *state = ctx;
  qa_remote_application *app = state->app;
  qa_app_client_bootstrap *client = state->client;
  qa_app_client_source_release_settings(state->previous);
  state->release_target = qa_remote_release_commands(app->configuration->profile);
  activate_frontend(client, &state->release_target, connecting_publish, state);
  state->retired = qa_engine_session_detach_world(client->session);
  state->has_retired = true;
  qa_session_client *remote_client_owner = remote_client(app);
  const qa_remote_source *source = qa_remote_application_source(app);
  app->source_connection = qa_session_client_replace_connection(remote_client_owner,
      source->kind == QA_REMOTE_SOURCE_RECORDED ? "demo" : "remote", &state->old_connection);
  state->old_seats = calloc(app->remote_seat_count + 1, sizeof *state->old_seats);
  if (state->old_seats == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < app->remote_seat_count; i++) {
    qa_session_connection *previous = qa_app_remote_seat_channel_publish_connection(app->remote_seats[i]->channel);
    if (previous != NULL) state->old_seats[state->old_seat_count++] = previous;
  }
  client->source_current = &app->source_lifetime;
  client->has_source_profile = false;
  state->retired_configuration = client->configuration_current.scripts;
  client->configuration_current.scripts = app->client_command_owner->scripts;
  client->configuration_current.options = &app->launch_options;
  qa_cvar_registry *cvars = qa_app_image_settings_cvars(app->image_settings);
  qa_app_view_settings_set_field_of_view(app->view_settings, qa_cvar_registry_variable_value(cvars, "fov"));
  qa_app_view_settings_bind_cvars(app->view_settings, cvars);
  app->release_view_cvars = true;
}
static void connecting_retire_previous(void *ctx) {
  connecting_state *state = ctx;
  const char *error = qa_app_client_source_retire(state->previous);
  if (error != NULL) qa_raise("Error", 0, 0, "%s", error);
}
static void connecting_close_retired(void *ctx) {
  connecting_state *state = ctx;
  if (state->has_retired && state->retired.close != NULL) state->retired.close(state->retired.ctx);
}
static void connecting_close_old(void *ctx) {
  connecting_state *state = ctx;
  if (state->old_connection != NULL) qa_session_connection_close(state->old_connection);
}
typedef struct connecting_seat { qa_session_connection *connection; } connecting_seat;
static void connecting_close_seat(void *ctx) { qa_session_connection_close(((connecting_seat *)ctx)->connection); }
static void connecting_close_scripts(void *ctx) {
  connecting_state *state = ctx;
  if (state->retired_configuration != NULL) qa_app_console_script_files_close(state->retired_configuration);
}
void qa_remote_application_publish_connecting(qa_remote_application *app, qa_app_client_bootstrap *client, const bool *aborted) {
  if (aborted != NULL && *aborted) qa_raise("AbortError", 0, 0, "The operation was aborted.");
  const qa_app_client_source_lifetime *previous = client->source_current;
  qa_remote_command_owner *owner = app->client_command_owner;
  if (owner == NULL) qa_raise("Error", 0, 0, "Remote source has no command owner");
  qa_app_capture_before_world_change(client->capture);
  const char *retirement = qa_app_client_source_prepare_retirement(previous);
  if (retirement != NULL) qa_raise("Error", 0, 0, "%s", retirement);
  if (aborted != NULL && *aborted) qa_raise("AbortError", 0, 0, "The operation was aborted.");
  qa_resource_scope_assert_open(qa_engine_session_resources(client->session));
  qa_app_prepared_owners owners;
  memset(&owners, 0, sizeof owners);
  qa_app_prepared_profile *profile = app->configuration->profile;
  owners.source = qa_app_prepared_profile_source(profile);
  owners.movement = qa_app_prepared_profile_movement(profile);
  owners.fallback = qa_app_prepared_profile_fallback(profile);
  owners.scripts = qa_app_prepared_profile_scripts(profile);
  owners.read = qa_app_prepared_profile_read(profile, &owners.read_ctx);
  qa_app_prepared_startup_validate_owners(client->prepared, &owners);
  qa_app_prepared_client_commands_validate_publication(qa_app_prepared_profile_program(profile));
  qa_app_image_client_settings images = {app->configuration->images.settings, app->configuration->images.transfer};
  qa_app_image_client_settings_validate_publication(&images);
  connecting_state state;
  memset(&state, 0, sizeof state);
  state.app = app;
  state.client = client;
  state.previous = previous;
  app->publication_started = true;
  qa_caught_error caught;
  bool failed = !qa_try(connecting_body, &state, &caught);
  /* finally */
  void (*closes[])(void *) = {connecting_retire_previous, connecting_close_retired, connecting_close_old};
  qa_caught_error cleanup;
  for (size_t i = 0; i < sizeof closes / sizeof closes[0]; i++)
    if (!qa_try(closes[i], &state, &cleanup)) failed = true;
  for (size_t i = 0; i < state.old_seat_count; i++) {
    connecting_seat seat = {state.old_seats[i]};
    if (!qa_try(connecting_close_seat, &seat, &cleanup)) failed = true;
  }
  free(state.old_seats);
  if (!qa_try(connecting_close_scripts, &state, &cleanup)) failed = true;
  if (failed) qa_raise("ClientSourcePublicationError", 0, 0, "%s", qa_app_client_source_publication_error);
  qa_remote_application_activate_source_commands(app);
}
