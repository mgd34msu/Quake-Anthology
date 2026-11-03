/* Port of src/app/bootstrap/remote-application.ts: prepareRemoteConfiguration
 * (TS 2392-2506) and the RemoteConfiguration record's closures (prepareTransport,
 * validateSource, publishSource, routing). Async -> synchronous; TS throws are
 * qa_raise("Error", ...), the TS catch blocks are qa_try + cleanup + rethrow.
 * Objects the TS keeps alive through the record's closures are arena-owned
 * (released when the application is dropped). GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "remote_application_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/cvar_archives.h"
#include "qa/app/bootstrap/frame_time.h"
#include "qa/app/bootstrap/q3_client/userinfo.h"
#include "qa/content/catalog/index.h"
#include "qa/content/mounts.h"
#include "qa/content/roots.h"
#include "qa/content/user_data.h"
#include "qa/foundation/support.h"

static void *xcalloc(size_t count, size_t size) {
  void *memory = calloc(count == 0 ? 1 : count, size == 0 ? 1 : size);
  if (memory == NULL) qa_range_error("Out of memory");
  return memory;
}
static bool seat_id_equal(qa_seat_id a, qa_seat_id b) { return a.token == b.token && a.index == b.index; }

/* ---- arena ------------------------------------------------------------ */
void qa_remote_arena_add(qa_remote_arena *arena, void (*release)(void *object), void *object) {
  if (object == NULL) return;
  if (arena->count == arena->capacity) {
    size_t capacity = arena->capacity == 0 ? 32 : arena->capacity * 2;
    qa_remote_arena_entry *entries = realloc(arena->entries, capacity * sizeof *entries);
    if (entries == NULL) { release(object); qa_range_error("Out of memory"); }
    arena->entries = entries; arena->capacity = capacity;
  }
  arena->entries[arena->count].release = release;
  arena->entries[arena->count].object = object;
  arena->count++;
}
void qa_remote_arena_release(qa_remote_arena *arena) {
  while (arena->count > 0) {
    arena->count--;
    arena->entries[arena->count].release(arena->entries[arena->count].object);
  }
  free(arena->entries);
  arena->entries = NULL; arena->capacity = 0;
}
static void release_memory(void *object) { free(object); }
static void release_registry(void *object) { qa_cvar_registry_destroy(object); }
void qa_remote_release_transfer(void *object) {
  qa_cvar_transfer *transfer = object;
  transfer->destroy(transfer->ctx);
  free(transfer);
}
static void release_store(void *object) { qa_config_store_destroy(object); free(object); }
/* TS new ConfigStore(product.userContent?.root ?? userProductDirectory(options.userContentRoot ?? defaultUserContentRoot(),
 * product.expectation.contentDirectory)), arena-owned. */
qa_config_store *qa_remote_product_settings(qa_remote_arena *arena, const qa_catalog_discovered_product *product,
    const char *user_content_root) {
  qa_config_store *settings = xcalloc(1, sizeof *settings);
  char *fallback = user_content_root != NULL ? NULL : qa_default_user_content_root();
  char *directory = product->user_content != NULL && product->user_content->root != NULL ? NULL
      : qa_user_product_directory(user_content_root != NULL ? user_content_root : fallback, product->expectation.content_directory);
  bool ok = qa_config_store_init(settings, directory != NULL ? directory : product->user_content->root);
  free(directory); free(fallback);
  if (!ok) { free(settings); qa_range_error("Out of memory"); }
  qa_remote_arena_add(arena, release_store, settings);
  return settings;
}

static void release_scripts(void *object) { qa_app_console_script_files_destroy(object); }
static void release_prepared(void *object) { qa_app_prepared_startup_close(object); }
void qa_remote_release_profile(void *object) { qa_app_prepared_profile_destroy(object); }
static void release_input(void *object) { qa_seat_input_destroy(object); }
static void release_permission(void *object) { qa_app_client_download_permission_destroy(object); }
static void release_socks(void *object) { qa_app_client_socks_settings_destroy(object); }
static void release_images(void *object) { qa_app_image_settings_destroy(object); }
static void release_identities(void *object) { qa_app_remote_seat_identities_destroy(object); }
static void release_content(void *object) { qa_app_configuration_content_destroy(object); }
static void release_reader(void *object) {
  qa_app_source_script_reader *reader = object;
  qa_app_source_script_reader_destroy(*reader);
  free(reader);
}

/* ---- the profile's ConsoleScriptFiles ---------------------------------- */
/* TS prepareProfileConfiguration builds its ConsoleScriptFiles over the
 * selected content (configuration.ts 115-120); the C profile borrows one
 * built by its caller, so this is that construction. */
typedef struct profile_scripts_ctx {
  qa_app_configuration_content *content;
  qa_content_mounted *mounts;
  const char **game_roots; char **owned_roots; size_t owned_count;
  char *console_root;
} profile_scripts_ctx;
static void release_profile_scripts_ctx(void *object) {
  profile_scripts_ctx *ctx = object;
  for (size_t i = 0; i < ctx->owned_count; i++) free(ctx->owned_roots[i]);
  free(ctx->owned_roots); free(ctx->game_roots); free(ctx->console_root);
  free(ctx);
}
static bool mounted_open(void *raw, const char *name, uint8_t **out_bytes, size_t *out_count) {
  profile_scripts_ctx *ctx = raw;
  qa_content_opened_resource opened;
  if (!qa_content_mounted_open(ctx->mounts, name, NULL, NULL, &opened)) return false;
  *out_bytes = opened.bytes; *out_count = opened.byte_length;
  return true;
}
static char **mounted_files(void *raw, const char *directory, const char *extension, size_t *out_count) {
  profile_scripts_ctx *ctx = raw;
  char **names = qa_content_mounted_list_files(ctx->mounts, directory, extension, out_count);
  if (names == NULL) qa_range_error("Out of memory");
  return names;
}
static const char *retire_mounted(void *raw) {
  profile_scripts_ctx *ctx = raw;
  qa_app_configuration_content_close(ctx->content);
  return NULL;
}
qa_app_console_script_files *qa_remote_profile_scripts(const qa_app_options *options, qa_app_configuration_content *content,
    const qa_config_store *settings, qa_remote_arena *arena) {
  const qa_content_catalog *catalog = qa_app_configuration_content_catalog(content);
  qa_app_application_configuration_selection selection = qa_app_configuration_content_selection(content);
  profile_scripts_ctx *ctx = xcalloc(1, sizeof *ctx);
  qa_remote_arena_add(arena, release_profile_scripts_ctx, ctx);
  ctx->content = content;
  ctx->mounts = (qa_content_mounted *)qa_app_configuration_content_mounts(content);
  ctx->console_root = qa_app_console_config_root(options->user_content_root);
  if (ctx->console_root == NULL) qa_range_error("Out of memory");
  qa_app_console_script_files_options files;
  memset(&files, 0, sizeof files);
  size_t capacity = catalog->product_count + 1;
  ctx->game_roots = xcalloc(capacity, sizeof *ctx->game_roots);
  ctx->owned_roots = xcalloc(capacity + 1, sizeof *ctx->owned_roots);
  ctx->owned_count = capacity + 1;
  files.has_legacy_config = qa_app_legacy_configuration_options_for_catalog(catalog, options,
      selection.engine_behavior.content, &files.legacy_config, ctx->game_roots, capacity, ctx->owned_roots);
  files.console_root = ctx->console_root;
  files.settings = settings;
  qa_app_source_script_reader *reader = xcalloc(1, sizeof *reader);
  *reader = qa_app_source_script_reader_create(catalog, ctx->mounts, selection.engine_behavior.content);
  qa_remote_arena_add(arena, release_reader, reader);
  files.mounted_script = *reader; files.has_mounted_script = true;
  files.mounted_ctx = ctx;
  files.mounted_resource = mounted_open;
  files.mounted_files = mounted_files;
  files.mounted.ctx = ctx; files.mounted.open = mounted_open; files.has_mounted = true;
  files.retire_mounted = retire_mounted;
  qa_app_console_script_files *scripts = qa_app_console_script_files_create(files);
  if (scripts == NULL) qa_range_error("Out of memory");
  qa_remote_arena_add(arena, release_scripts, scripts);
  return scripts;
}

/* ---- routing closures --------------------------------------------------- */
/* TS stagedRouting(base) result: socks.route(base) with the socks owner
 * swapped for its staged candidate. */
typedef struct staged_route {
  qa_app_client_socks_settings *socks;
  qa_cvar_transfer *staged; /* NULL = TS null */
  qa_command_cvar_routing routing;
  qa_cvar_registry **visible; size_t visible_capacity;
} staged_route;
static void release_staged_route(void *object) { staged_route *route = object; free(route->visible); free(route); }
static qa_cvar_registry *stage(const staged_route *route, qa_cvar_registry *registry) {
  return registry == qa_app_client_socks_settings_cvars(route->socks) && route->staged != NULL ? route->staged->cvars : registry;
}
static qa_cvar_registry *staged_owner(void *raw, const char *name, qa_command_context source) {
  staged_route *route = raw;
  return stage(route, route->routing.owner(route->routing.ctx, name, source));
}
static qa_cvar_registry *const *staged_visible(void *raw, qa_command_context source, size_t *out_count) {
  staged_route *route = raw;
  size_t count = 0;
  qa_cvar_registry *const *visible = route->routing.visible(route->routing.ctx, source, &count);
  if (count > route->visible_capacity) {
    qa_cvar_registry **grown = realloc(route->visible, count * sizeof *grown);
    if (grown == NULL) qa_range_error("Out of memory");
    route->visible = grown; route->visible_capacity = count;
  }
  for (size_t i = 0; i < count; i++) route->visible[i] = stage(route, visible[i]);
  *out_count = count;
  return route->visible;
}
typedef struct staged_routing_ctx {
  qa_app_client_socks_settings *socks;
  qa_cvar_transfer *staged;
  qa_remote_arena *arena;
} staged_routing_ctx;
static qa_command_cvar_routing staged_routing(void *raw, qa_command_cvar_routing base) {
  staged_routing_ctx *ctx = raw;
  staged_route *route = xcalloc(1, sizeof *route);
  qa_remote_arena_add(ctx->arena, release_staged_route, route);
  route->socks = ctx->socks; route->staged = ctx->staged;
  route->routing = qa_app_client_socks_settings_route(ctx->socks, base);
  qa_command_cvar_routing out = { route, staged_owner, staged_visible };
  return out;
}
/* TS remap: transfers.get(registry)?.owner ?? registry. */
static qa_cvar_registry *remap(const qa_remote_configuration *configuration, qa_cvar_registry *registry) {
  for (size_t i = 0; i < configuration->transfer_count; i++)
    if (configuration->transfers[i].transfer->cvars == registry) return configuration->transfers[i].owner;
  return registry;
}
static qa_cvar_registry *configuration_owner(void *raw, const char *name, qa_command_context source) {
  qa_remote_configuration *configuration = raw;
  return remap(configuration, configuration->profile_routing.owner(configuration->profile_routing.ctx, name, source));
}
static qa_cvar_registry *const *configuration_visible(void *raw, qa_command_context source, size_t *out_count) {
  qa_remote_configuration *configuration = raw;
  size_t count = 0;
  qa_cvar_registry *const *visible = configuration->profile_routing.visible(configuration->profile_routing.ctx, source, &count);
  if (count > configuration->visible_capacity) {
    qa_cvar_registry **grown = realloc(configuration->visible, count * sizeof *grown);
    if (grown == NULL) qa_range_error("Out of memory");
    configuration->visible = grown; configuration->visible_capacity = count;
  }
  for (size_t i = 0; i < count; i++) configuration->visible[i] = remap(configuration, visible[i]);
  *out_count = count;
  return configuration->visible;
}

/* ---- RemoteConfiguration members -------------------------------------- */
void qa_remote_configuration_prepare_transport(qa_remote_configuration *configuration, qa_socks_connector transport) {
  if (configuration->peer != NULL) {
    /* TS publishPeerConfiguration: socks.connect(transport, published ? socks.cvars : staged.cvars) */
    qa_app_client_socks_settings_connect(configuration->socks, transport, qa_remote_peer_configuration_transport_cvars(configuration->peer));
    return;
  }
  qa_app_client_socks_settings_connect(configuration->socks, transport,
      qa_app_remote_seat_identities_published(configuration->identities) ? qa_app_client_socks_settings_cvars(configuration->socks)
      : configuration->staged_socks != NULL ? configuration->staged_socks->cvars : qa_app_client_socks_settings_cvars(configuration->socks));
}
void qa_remote_configuration_validate_source(qa_remote_configuration *configuration) {
  if (configuration->peer != NULL) { qa_remote_peer_configuration_validate(configuration->peer); return; }
  if (!qa_app_remote_seat_identities_published(configuration->identities)) qa_app_remote_seat_identities_validate(configuration->identities);
  for (size_t i = 0; i < configuration->transfer_count; i++)
    configuration->transfers[i].transfer->validate_publication(configuration->transfers[i].transfer->ctx);
}
void qa_remote_configuration_publish_source(qa_remote_configuration *configuration) {
  if (configuration->peer != NULL) { qa_remote_peer_configuration_publish(configuration->peer); return; }
  if (!qa_app_remote_seat_identities_published(configuration->identities)) qa_app_remote_seat_identities_publish(configuration->identities);
  for (size_t i = 0; i < configuration->transfer_count; i++)
    configuration->transfers[i].transfer->publish(configuration->transfers[i].transfer->ctx);
}
qa_cvar_registry *qa_remote_configuration_seat_source(const qa_remote_configuration *configuration, qa_seat_id id) {
  for (size_t i = 0; i < configuration->seat_source_count; i++)
    if (seat_id_equal(configuration->seat_sources[i].id, id)) return configuration->seat_sources[i].cvars;
  return NULL;
}
void qa_remote_release_configuration_record(void *object) {
  qa_remote_configuration *configuration = object;
  free(configuration->seats); free(configuration->seat_sources); free(configuration->download_permissions);
  free(configuration->transfers); free(configuration->visible);
  free(configuration);
}

/* ---- prepareRemoteConfiguration --------------------------------------- */
typedef struct retained_seat {
  qa_session_client *client; qa_session_seat *seat;
  qa_cvar_registry *prepared_cvars; /* retained.prepared.cvars */
  qa_seat_input *prepared_input;
} retained_seat;
typedef struct prepare_state {
  /* arguments */
  const qa_app_options *options;
  qa_app_configuration_content *content;
  qa_engine_session *session;
  qa_app_image_settings *image_settings;
  qa_remote_ownership ownership;
  const qa_application_host *host;
  qa_remote_cheats_allowed_fn cheats_allowed; void *cheats_ctx;
  qa_remote_configuration *current;
  qa_app_configuration_content *selected;
  qa_remote_arena *arena;
  /* locals that the catch blocks read */
  qa_app_remote_seat_identities *identities;
  qa_app_console_script_files *seed_scripts; /* NULL = TS null */
  qa_remote_client_images images; bool has_images;
  retained_seat *retained; size_t retained_count;
  qa_remote_configuration *result;
} prepare_state;

qa_command_dialect qa_remote_dialect_for_clock(qa_clock_profile_kind kind) {
  switch (kind) {
    case QA_CLOCK_Q1_NETQUAKE: return QA_COMMAND_DIALECT_Q1_NETQUAKE;
    case QA_CLOCK_Q1_QUAKEWORLD: return QA_COMMAND_DIALECT_Q1_QUAKEWORLD;
    case QA_CLOCK_Q2_CLASSIC: return QA_COMMAND_DIALECT_Q2_CLASSIC;
    case QA_CLOCK_Q2_RERELEASE: return QA_COMMAND_DIALECT_Q2_RERELEASE;
    case QA_CLOCK_Q3: return QA_COMMAND_DIALECT_Q3;
  }
  return QA_COMMAND_DIALECT_Q3;
}
bool qa_remote_network_is(const qa_app_options *options, const char *kind) {
  return options->network_kind != NULL && strcmp(options->network_kind, kind) == 0;
}
void qa_remote_host_print(void *raw, const char *text) {
  const qa_application_host *host = raw;
  if (host->print != NULL) host->print(host->ctx, text);
}
static void host_print_source(void *raw, const char *text, const qa_command_context *source) {
  (void)source; /* TS print: host.print, which takes the text only */
  qa_remote_host_print(raw, text);
}
static void forward_nothing(void *ctx, const char *name, const char *const *args, size_t arg_count, qa_command_context source) {
  (void)ctx; (void)name; (void)args; (void)arg_count; (void)source; /* TS forward: () => undefined */
}
static bool ui_event_none(void *ctx, const qa_seat_input_event *event, qa_seat_input_focus focus) {
  (void)ctx; (void)event; (void)focus; /* TS uiEvent: () => false */
  return false;
}
void qa_remote_next_frame(void *ctx) { (void)ctx; /* TS await setImmediate(): synchronous here */ }
static qa_command_context local_seat_context(qa_engine_session *session, qa_session_seat *seat) {
  qa_command_context context;
  memset(&context, 0, sizeof context);
  context.session = qa_engine_session_id(session);
  context.origin.kind = QA_COMMAND_ORIGIN_LOCAL_SEAT;
  context.origin.seat = qa_session_seat_id(seat);
  context.origin.client = qa_session_client_id(qa_session_seat_client(seat));
  return context;
}
static qa_cvar_registry *new_registry(prepare_state *state, qa_command_dialect dialect, qa_command_context context, bool cheats) {
  qa_cvar_registry_options options;
  memset(&options, 0, sizeof options);
  options.dialect = dialect; options.context = context;
  options.print = qa_remote_host_print; options.print_ctx = (void *)state->host;
  if (cheats) { options.cheats_allowed = state->cheats_allowed; options.cheats_allowed_ctx = state->cheats_ctx; }
  qa_cvar_registry *registry = qa_cvar_registry_create(&options);
  if (registry == NULL) qa_range_error("Out of memory");
  qa_remote_arena_add(state->arena, release_registry, registry);
  return registry;
}
static void reg(qa_cvar_registry *cvars, const char *name, const char *value, int flags) {
  qa_cvar_snapshot registered;
  qa_cvar_registry_register(cvars, name, value, flags, &registered);
  qa_cvar_read_free(&registered);
}
static const qa_app_prepared_seat *prepared_seat_by_id(qa_app_prepared_startup *prepared, qa_seat_id id, size_t *out_index) {
  size_t count = 0;
  const qa_app_prepared_seat *seats = qa_app_prepared_startup_seats(prepared, &count);
  for (size_t i = 0; i < count; i++) if (seat_id_equal(seats[i].id, id)) { if (out_index) *out_index = i; return &seats[i]; }
  return NULL;
}
static const retained_seat *retained_for(const prepare_state *state, const qa_session_seat *seat) {
  for (size_t i = 0; i < state->retained_count; i++) if (state->retained[i].seat == seat) return &state->retained[i];
  return NULL;
}
/* TS order(id) of the borrowed seat sort. */
static double borrowed_order(qa_app_prepared_startup *prepared, qa_seat_id id) {
  size_t index = 0;
  return prepared_seat_by_id(prepared, id, &index) != NULL ? (double)index : 4.0 + (double)id.index;
}

static void prepare_retained(prepare_state *state) {
  qa_remote_configuration *current = state->current;
  if (current == NULL) {
    if (!state->ownership.borrowed) return;
    qa_app_client_bootstrap *client = state->ownership.client;
    state->retained = xcalloc(client->local_count, sizeof *state->retained);
    qa_remote_arena_add(state->arena, release_memory, state->retained);
    for (size_t i = 0; i < client->local_count; i++) {
      retained_seat entry = { client->locals[i].client, client->locals[i].seat, NULL, NULL };
      const qa_app_prepared_seat *prepared = prepared_seat_by_id(client->prepared, client->locals[i].prepared, NULL);
      if (prepared != NULL) { entry.prepared_cvars = prepared->cvars; entry.prepared_input = prepared->input; }
      /* stable insertion sort by order(seat.id): TS Array.prototype.sort is stable */
      double key = borrowed_order(client->prepared, qa_session_seat_id(entry.seat));
      size_t at = state->retained_count;
      while (at > 0 && borrowed_order(client->prepared, qa_session_seat_id(state->retained[at - 1].seat)) > key) {
        state->retained[at] = state->retained[at - 1]; at--;
      }
      state->retained[at] = entry;
      state->retained_count++;
    }
    return;
  }
  size_t selected_count = 0;
  const qa_app_remote_seat_identity *selected = qa_app_remote_seat_identities_selected(current->identities, &selected_count);
  state->retained = xcalloc(current->seat_count + selected_count, sizeof *state->retained);
  qa_remote_arena_add(state->arena, release_memory, state->retained);
  for (size_t pass = 0; pass < 2; pass++) {
    size_t count = pass == 0 ? current->seat_count : selected_count;
    for (size_t i = 0; i < count; i++) {
      qa_session_seat *seat = pass == 0 ? current->seats[i] : selected[i].seat;
      if (pass == 1) {
        bool included = false;
        for (size_t j = 0; j < current->seat_count; j++) if (current->seats[j] == seat) included = true;
        if (included) continue;
      }
      const qa_app_prepared_seat *prepared = prepared_seat_by_id(current->prepared, qa_session_seat_id(seat), NULL);
      if (prepared == NULL) qa_raise("Error", 0, 0, "Active remote seat lost its profile");
      retained_seat entry = { qa_session_seat_client(seat), seat, prepared->cvars, prepared->input };
      state->retained[state->retained_count++] = entry;
    }
  }
}

static void register_dialect_cvars(prepare_state *state, qa_cvar_registry *cvars, qa_command_dialect dialect, qa_session_seat *seat) {
  const qa_app_options *options = state->options;
  bool unified = qa_remote_network_is(options, "unified-client");
  int64_t index = qa_session_seat_id(seat).index;
  char player[32];
  if (index == 0) snprintf(player, sizeof player, "Player");
  else snprintf(player, sizeof player, "Player %lld", (long long)(index + 1));
  qa_app_register_frame_time_cvars(cvars);
  if (dialect == QA_COMMAND_DIALECT_Q1_NETQUAKE) {
    reg(cvars, "name", player, QA_CVAR_FLAG_ARCHIVE | (unified ? QA_CVAR_FLAG_USER_INFO : 0));
    reg(cvars, "color", "0", QA_CVAR_FLAG_ARCHIVE);
  }
  if (dialect == QA_COMMAND_DIALECT_Q1_NETQUAKE && unified) reg(cvars, "password", "", QA_CVAR_FLAG_USER_INFO);
  if (dialect == QA_COMMAND_DIALECT_Q2_CLASSIC || dialect == QA_COMMAND_DIALECT_Q2_RERELEASE) {
    const int flags = QA_CVAR_FLAG_ARCHIVE | QA_CVAR_FLAG_USER_INFO;
    const char *model = options->character_model != NULL ? options->character_model : "";
    char skin[256];
    snprintf(skin, sizeof skin, "%s/%s", model,
        strcmp(model, "female") == 0 ? "athena" : strcmp(model, "cyborg") == 0 ? "oni911" : "grunt");
    reg(cvars, "name", player, flags);
    reg(cvars, "skin", skin, flags);
    reg(cvars, "rate", "15000", flags);
    reg(cvars, "msg", "1", flags);
    reg(cvars, "hand", "0", flags);
    reg(cvars, "fov", "90", flags);
    reg(cvars, "gender", strcmp(model, "female") == 0 ? "female" : "male", flags);
    reg(cvars, "password", "", QA_CVAR_FLAG_USER_INFO);
    reg(cvars, "spectator", "0", QA_CVAR_FLAG_USER_INFO);
  }
  if (dialect == QA_COMMAND_DIALECT_Q3) {
    qa_app_q3_client_identity identity = { player,
        qa_app_q3_client_model(qa_game_family_name(options->character), options->character_model) };
    qa_app_initialize_q3_client_cvars(cvars, identity);
  }
  if (dialect == QA_COMMAND_DIALECT_Q1_QUAKEWORLD) {
    reg(cvars, "cl_hightrack", "0", 0); reg(cvars, "cl_chasecam", "0", 0);
    reg(cvars, "rate", "25000", QA_CVAR_FLAG_ARCHIVE | QA_CVAR_FLAG_USER_INFO);
    reg(cvars, "noskins", "0", QA_CVAR_FLAG_ARCHIVE); reg(cvars, "baseskin", "base", QA_CVAR_FLAG_ARCHIVE);
    static const char *const variables[][2] = { { "name", "unnamed" }, { "team", "" }, { "skin", "" },
      { "topcolor", "0" }, { "bottomcolor", "0" }, { "noaim", "0" }, { "msg", "1" } };
    for (size_t i = 0; i < sizeof variables / sizeof variables[0]; i++)
      reg(cvars, variables[i][0], variables[i][1], QA_CVAR_FLAG_ARCHIVE | QA_CVAR_FLAG_USER_INFO);
    reg(cvars, "password", "", QA_CVAR_FLAG_USER_INFO);
  }
}

static void push_transfer(qa_remote_configuration *configuration, size_t *capacity, qa_cvar_registry *owner, qa_cvar_transfer *transfer) {
  if (configuration->transfer_count == *capacity) {
    *capacity = *capacity == 0 ? 8 : *capacity * 2;
    qa_remote_staged_transfer *grown = realloc(configuration->transfers, *capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("Out of memory");
    configuration->transfers = grown;
  }
  configuration->transfers[configuration->transfer_count].owner = owner;
  configuration->transfers[configuration->transfer_count].transfer = transfer;
  configuration->transfer_count++;
}
static qa_cvar_archive_entry *archive_entries(qa_cvar_registry *registry, size_t *out_count) {
  qa_cvar_archive_entry *entries = qa_cvar_registry_archive_entries(registry, NULL, NULL, out_count);
  if (entries == NULL && *out_count != 0) qa_range_error("Out of memory");
  return entries;
}
typedef struct archive_rows { qa_cvar_archive_entry *entries; size_t count; } archive_rows;
static void release_archive_rows(void *object) {
  archive_rows *rows = object;
  qa_app_cvar_archive_entries_free(rows->entries, rows->count);
  free(rows);
}

static void prepare_body(void *raw) {
  prepare_state *state = raw;
  const qa_app_options *options = state->options;
  qa_remote_configuration *current = state->current;
  qa_remote_arena *arena = state->arena;
  /* const seats = identities.selected.map(entry => entry.seat), seat = current?.seat ?? seats[0] */
  size_t selected_count = 0;
  const qa_app_remote_seat_identity *selected_identities = qa_app_remote_seat_identities_selected(state->identities, &selected_count);
  qa_session_seat *seat = current != NULL ? current->seat : selected_count > 0 ? selected_identities[0].seat : NULL;
  if (seat == NULL) qa_raise("Error", 0, 0, "Remote configuration has no primary seat");
  const retained_seat *retained = retained_for(state, seat);
  qa_command_context source_context;
  memset(&source_context, 0, sizeof source_context);
  source_context.session = qa_engine_session_id(state->session);
  source_context.origin.kind = QA_COMMAND_ORIGIN_LOCAL_CONSOLE;
  const qa_content_catalog *catalog = qa_app_configuration_content_catalog(state->content);
  const qa_catalog_discovered_product *product =
      qa_content_catalog_require(catalog, qa_remote_network_is(options, "qw-client") ? "q1-quakeworld" : options->product);
  qa_config_store *settings = xcalloc(1, sizeof *settings);
  {
    char *fallback = options->user_content_root != NULL ? NULL : qa_default_user_content_root();
    char *directory = product->user_content != NULL && product->user_content->root != NULL ? NULL
        : qa_user_product_directory(options->user_content_root != NULL ? options->user_content_root : fallback,
            product->expectation.content_directory);
    bool ok = qa_config_store_init(settings, directory != NULL ? directory : product->user_content->root);
    free(directory); free(fallback);
    if (!ok) { free(settings); qa_range_error("Out of memory"); }
    qa_remote_arena_add(arena, release_store, settings);
  }
  qa_command_dialect dialect = QA_COMMAND_DIALECT_Q3;
  {
    size_t timing_count = 0;
    const qa_provider_timing *timing = qa_app_configuration_content_timing(state->selected, &timing_count);
    qa_app_application_configuration_selection selection = qa_app_configuration_content_selection(state->selected);
    bool found = false;
    for (size_t i = 0; i < timing_count && !found; i++)
      if (timing[i].provider != NULL && selection.engine_behavior.provider != NULL
          && strcmp(timing[i].provider, selection.engine_behavior.provider) == 0) { dialect = qa_remote_dialect_for_clock(timing[i].clock.kind); found = true; }
    if (!found) qa_raise("Error", 0, 0, "Remote configuration has no source timing");
  }
  qa_remote_configuration *configuration = xcalloc(1, sizeof *configuration);
  qa_remote_arena_add(arena, qa_remote_release_configuration_record, configuration);
  size_t transfer_capacity = 0;
  configuration->seats = xcalloc(selected_count, sizeof *configuration->seats);
  configuration->seat_count = selected_count;
  qa_remote_seat_source_entry *seat_sources = xcalloc(selected_count, sizeof *seat_sources);
  qa_remote_arena_add(arena, release_memory, seat_sources);
  for (size_t i = 0; i < selected_count; i++) {
    qa_session_seat *entry_seat = selected_identities[i].seat;
    configuration->seats[i] = entry_seat;
    qa_command_context context = local_seat_context(state->session, entry_seat);
    const retained_seat *entry_retained = retained_for(state, entry_seat);
    qa_cvar_registry *original = current != NULL ? qa_remote_configuration_seat_source(current, qa_session_seat_id(entry_seat)) : NULL;
    qa_cvar_transfer *transfer = NULL;
    if (original != NULL && qa_cvar_registry_dialect(original) == dialect) {
      transfer = qa_cvar_registry_prepare_candidate(original, qa_remote_host_print, (void *)state->host, true);
      if (transfer == NULL) qa_range_error("Out of memory");
      qa_remote_arena_add(arena, qa_remote_release_transfer, transfer);
    }
    qa_cvar_registry *cvars = transfer != NULL ? transfer->cvars : new_registry(state, dialect, context, true);
    if (original != NULL && transfer != NULL) push_transfer(configuration, &transfer_capacity, original, transfer);
    if (transfer == NULL && entry_retained != NULL && entry_retained->prepared_cvars != NULL
        && qa_cvar_registry_dialect(entry_retained->prepared_cvars) == dialect) {
      qa_save_value *saved = qa_cvar_registry_capture_world_transfer_state(entry_retained->prepared_cvars);
      if (saved == NULL) qa_range_error("Out of memory");
      qa_cvar_registry_restore_save_state(cvars, saved);
      qa_save_value_free(saved);
    }
    register_dialect_cvars(state, cvars, dialect, entry_seat);
    seat_sources[i].id = qa_session_seat_id(entry_seat);
    seat_sources[i].cvars = cvars;
  }
  qa_cvar_registry *cvars = NULL;
  for (size_t i = 0; i < selected_count; i++) if (seat_id_equal(seat_sources[i].id, qa_session_seat_id(seat))) cvars = seat_sources[i].cvars;
  if (cvars == NULL) qa_raise("Error", 0, 0, "Remote source registry is missing");
  /* downloadPermissions = new Map(current?.source.dialect === dialect ? current.downloadPermissions : undefined) */
  bool inherit = current != NULL && qa_cvar_registry_dialect(current->source) == dialect;
  size_t inherited = inherit ? current->download_permission_count : 0;
  configuration->download_permissions = xcalloc(inherited + selected_count, sizeof *configuration->download_permissions);
  for (size_t i = 0; i < inherited; i++) configuration->download_permissions[i] = current->download_permissions[i];
  configuration->download_permission_count = inherited;
  for (size_t i = 0; i < selected_count; i++) {
    bool has = false;
    for (size_t j = 0; j < configuration->download_permission_count; j++)
      if (seat_id_equal(configuration->download_permissions[j].id, seat_sources[i].id)) has = true;
    if (has) continue;
    qa_app_client_download_permission *permission = NULL;
    if (dialect == QA_COMMAND_DIALECT_Q3 || dialect == QA_COMMAND_DIALECT_Q2_CLASSIC || dialect == QA_COMMAND_DIALECT_Q2_RERELEASE) {
      permission = qa_app_create_client_download_permission(seat_sources[i].cvars, dialect == QA_COMMAND_DIALECT_Q3);
      if (permission == NULL) qa_range_error("Out of memory");
      qa_remote_arena_add(arena, release_permission, permission);
    }
    configuration->download_permissions[configuration->download_permission_count].id = seat_sources[i].id;
    configuration->download_permissions[configuration->download_permission_count].permission = permission;
    configuration->download_permission_count++;
  }
  configuration->download_permission = NULL;
  for (size_t j = 0; j < configuration->download_permission_count; j++)
    if (seat_id_equal(configuration->download_permissions[j].id, qa_session_seat_id(seat)))
      configuration->download_permission = configuration->download_permissions[j].permission;
  qa_app_client_socks_settings *socks = current != NULL ? current->socks : NULL;
  if (socks == NULL) {
    socks = qa_app_client_socks_settings_create(source_context, qa_remote_host_print, (void *)state->host);
    if (socks == NULL) qa_range_error("Out of memory");
    qa_remote_arena_add(arena, release_socks, socks);
  }
  qa_cvar_transfer *staged_socks = NULL;
  if (current != NULL) {
    staged_socks = qa_cvar_registry_prepare_candidate(qa_app_client_socks_settings_cvars(socks), qa_remote_host_print, (void *)state->host, true);
    if (staged_socks == NULL) qa_range_error("Out of memory");
    qa_remote_arena_add(arena, qa_remote_release_transfer, staged_socks);
    push_transfer(configuration, &transfer_capacity, qa_app_client_socks_settings_cvars(socks), staged_socks);
  }
  staged_routing_ctx *routing_ctx = xcalloc(1, sizeof *routing_ctx);
  qa_remote_arena_add(arena, release_memory, routing_ctx);
  routing_ctx->socks = socks; routing_ctx->staged = staged_socks; routing_ctx->arena = arena;
  if (current == NULL && !state->ownership.borrowed) {
    char *root = qa_app_console_config_root(options->user_content_root);
    if (root == NULL) qa_range_error("Out of memory");
    qa_remote_arena_add(arena, release_memory, root);
    qa_app_console_script_files_options files;
    memset(&files, 0, sizeof files);
    files.console_root = root; files.settings = settings;
    state->seed_scripts = qa_app_console_script_files_create(files);
    if (state->seed_scripts == NULL) qa_range_error("Out of memory");
    qa_remote_arena_add(arena, release_scripts, state->seed_scripts);
  }
  qa_cvar_registry *seed = new_registry(state, dialect, source_context, false);
  qa_app_prepared_startup *prepared = current != NULL ? current->prepared
      : state->ownership.borrowed ? state->ownership.client->prepared : NULL;
  if (prepared == NULL) {
    if (state->seed_scripts == NULL) qa_raise("Error", 0, 0, "Owned remote configuration requires its script owner");
    qa_app_prepared_seat_configuration *rows = xcalloc(selected_count, sizeof *rows);
    qa_remote_arena_add(arena, release_memory, rows);
    for (size_t i = 0; i < selected_count; i++) {
      qa_command_context context = local_seat_context(state->session, configuration->seats[i]);
      rows[i].context = context;
      rows[i].id = qa_session_seat_id(configuration->seats[i]);
      rows[i].cvars = new_registry(state, dialect, context, false);
      qa_mouse_settings_init(&rows[i].mouse, new_registry(state, dialect, context, false));
      rows[i].profile = NULL;
    }
    qa_app_prepared_startup_options startup;
    memset(&startup, 0, sizeof startup);
    startup.startup_commands = (const char *const *)options->startup_commands;
    startup.startup_command_count = options->startup_command_count;
    startup.dialect = dialect; startup.movement_dialect = dialect;
    startup.seats = rows; startup.seat_count = selected_count;
    startup.shared = qa_app_image_settings_cvars(state->image_settings);
    startup.print = host_print_source; startup.print_ctx = (void *)state->host;
    startup.forward = forward_nothing;
    prepared = qa_app_prepared_startup_create_concrete(seed, seed, state->seed_scripts, &startup);
    if (prepared == NULL) qa_range_error("Out of memory");
    qa_remote_arena_add(arena, release_prepared, prepared);
  }
  qa_app_profile_seat *inputs = xcalloc(selected_count, sizeof *inputs);
  qa_remote_arena_add(arena, release_memory, inputs);
  for (size_t i = 0; i < selected_count; i++) {
    inputs[i].seat = configuration->seats[i];
    const qa_app_prepared_seat *existing = prepared_seat_by_id(prepared, qa_session_seat_id(configuration->seats[i]), NULL);
    if (existing != NULL && existing->input != NULL) { inputs[i].input = existing->input; continue; }
    qa_seat_input_options input;
    memset(&input, 0, sizeof input);
    input.seat = qa_session_seat_id(configuration->seats[i]);
    input.dialect = dialect;
    input.context = local_seat_context(state->session, configuration->seats[i]);
    input.commands = qa_app_prepared_startup_commands(prepared);
    input.ui_event = ui_event_none;
    inputs[i].input = qa_seat_input_create(&input);
    if (inputs[i].input == NULL) qa_range_error("Out of memory");
    qa_remote_arena_add(arena, release_input, inputs[i].input);
  }
  /* const images = imageSettings.prepareClientSettings() */
  qa_app_image_client_settings images = qa_app_image_settings_prepare_client_settings(state->image_settings);
  state->images.settings = images.settings;
  state->has_images = true;
  qa_remote_arena_add(arena, release_images, state->images.settings);
  state->images.transfer = images.transfer;
  qa_remote_arena_add(arena, qa_remote_release_transfer, state->images.transfer);
  /* inner try: images.settings.close() on failure (run by the caller's catch) */
  bool same_profile = (current != NULL && strcmp(qa_app_prepared_profile_options(current->profile)->product, options->product) == 0
          && qa_cvar_registry_dialect(current->source) == dialect)
      || (state->ownership.borrowed && state->ownership.client->configuration_current.options != NULL
          && strcmp(state->ownership.client->configuration_current.options->product, options->product) == 0
          && retained != NULL && retained->prepared_cvars != NULL && qa_cvar_registry_dialect(retained->prepared_cvars) == dialect);
  char *archive = NULL;
  if (!same_profile) {
    archive = qa_config_store_load_text(settings, "settings/client.cfg", NULL);
    qa_remote_arena_add(arena, release_memory, archive);
  }
  bool fresh = current == NULL && !state->ownership.borrowed;
  qa_cvar_registry **seat_cvars = xcalloc(selected_count, sizeof *seat_cvars);
  const qa_cvar_archive_entry **seat_archives = xcalloc(selected_count, sizeof *seat_archives);
  size_t *seat_archive_counts = xcalloc(selected_count, sizeof *seat_archive_counts);
  qa_remote_arena_add(arena, release_memory, seat_cvars);
  qa_remote_arena_add(arena, release_memory, (void *)seat_archives);
  qa_remote_arena_add(arena, release_memory, seat_archive_counts);
  for (size_t i = 0; i < selected_count; i++) {
    seat_cvars[i] = seat_sources[i].cvars;
    bool retained_id = false;
    for (size_t j = 0; j < state->retained_count; j++)
      if (seat_id_equal(qa_session_seat_id(state->retained[j].seat), seat_sources[i].id)) retained_id = true;
    if (same_profile && retained_id) {
      archive_rows *rows = xcalloc(1, sizeof *rows);
      rows->entries = archive_entries(seat_sources[i].cvars, &rows->count);
      qa_remote_arena_add(arena, release_archive_rows, rows);
      seat_archives[i] = rows->entries; seat_archive_counts[i] = rows->count;
    }
  }
  qa_app_profile_client_source client;
  memset(&client, 0, sizeof client);
  client.fresh_input = fresh;
  client.has_startup_commands = fresh;
  client.startup_commands = (const char *const *)options->startup_commands;
  client.startup_command_count = options->startup_command_count;
  client.cvars = cvars;
  client.archive_text = archive;
  client.seat_cvars = seat_cvars;
  client.seat_archives = seat_archives; client.seat_archive_counts = seat_archive_counts; client.has_seat_archives = true;
  client.route = staged_routing; client.route_ctx = routing_ctx;
  archive_rows *source_archive = xcalloc(1, sizeof *source_archive);
  qa_remote_arena_add(arena, release_archive_rows, source_archive);
  if (same_profile) source_archive->entries = archive_entries(cvars, &source_archive->count);
  qa_app_prepare_profile_args args;
  memset(&args, 0, sizeof args);
  args.prepared = prepared;
  args.client_source = &client;
  args.seats = inputs; args.seat_count = selected_count;
  args.content = state->selected;
  args.catalog = catalog;
  args.settings = settings;
  args.shared = qa_app_image_settings_cvars(state->images.settings);
  args.scripts = qa_remote_profile_scripts(options, state->selected, settings, arena);
  args.print = qa_remote_host_print; args.print_ctx = (void *)state->host;
  args.source_archive = source_archive->entries; args.source_archive_count = source_archive->count;
  args.default_capacity = 1;
  args.next_frame = qa_remote_next_frame;
  qa_app_options *profile_options = xcalloc(1, sizeof *profile_options);
  qa_remote_arena_add(arena, release_memory, profile_options);
  *profile_options = *options;
  qa_app_prepare_profile_args *kept_args = xcalloc(1, sizeof *kept_args); /* the profile keeps `args` */
  qa_remote_arena_add(arena, release_memory, kept_args);
  qa_app_profile_client_source *kept_client = xcalloc(1, sizeof *kept_client);
  qa_remote_arena_add(arena, release_memory, kept_client);
  *kept_client = client; *kept_args = args; kept_args->client_source = kept_client;
  qa_app_prepared_profile *profile = qa_app_prepare_profile_configuration(profile_options, kept_args);
  if (profile == NULL) qa_range_error("Out of memory");
  qa_remote_arena_add(arena, qa_remote_release_profile, profile);
  if (qa_remote_network_is(options, "unified-client") && current != NULL && qa_cvar_registry_dialect(current->source) != dialect) {
    for (size_t i = 0; i < selected_count; i++) {
      static const char *const names[] = { "name", "password" };
      for (size_t n = 0; n < 2; n++) {
        qa_cvar_registry *previous = qa_remote_configuration_seat_source(current, seat_sources[i].id);
        qa_cvar_snapshot identity, destination;
        bool has_identity = previous != NULL && qa_cvar_registry_get(previous, names[n], &identity);
        bool has_destination = qa_cvar_registry_get(seat_sources[i].cvars, names[n], &destination);
        if (has_identity && has_destination && (identity.flags & QA_CVAR_FLAG_USER_INFO) != 0
            && (destination.flags & QA_CVAR_FLAG_USER_INFO) != 0)
        {
          qa_cvar_snapshot written;
          qa_cvar_registry_set(seat_sources[i].cvars, destination.name, identity.value, false, &written);
          qa_cvar_read_free(&written);
        }
        if (has_identity) qa_cvar_read_free(&identity);
        if (has_destination) qa_cvar_read_free(&destination);
      }
    }
  }
  /* seatSources: new Map([...(current?.seatSources ?? []), ...remapped]) */
  size_t prior = current != NULL ? current->seat_source_count : 0;
  configuration->seat_sources = xcalloc(prior + selected_count, sizeof *configuration->seat_sources);
  for (size_t i = 0; i < prior; i++) configuration->seat_sources[i] = current->seat_sources[i];
  configuration->seat_source_count = prior;
  for (size_t i = 0; i < selected_count; i++) {
    qa_cvar_registry *value = remap(configuration, seat_sources[i].cvars);
    bool replaced = false;
    for (size_t j = 0; j < configuration->seat_source_count; j++)
      if (seat_id_equal(configuration->seat_sources[j].id, seat_sources[i].id)) { configuration->seat_sources[j].cvars = value; replaced = true; }
    if (!replaced) {
      configuration->seat_sources[configuration->seat_source_count].id = seat_sources[i].id;
      configuration->seat_sources[configuration->seat_source_count].cvars = value;
      configuration->seat_source_count++;
    }
  }
  configuration->prepared = prepared;
  configuration->profile = profile;
  configuration->seat = seat;
  configuration->identities = state->identities;
  configuration->settings = settings;
  configuration->socks = socks;
  configuration->images = state->images;
  configuration->source = remap(configuration, qa_app_prepared_profile_source(profile));
  configuration->staged_socks = staged_socks;
  configuration->profile_routing = qa_app_client_socks_settings_route(socks, qa_app_console_routing_view(qa_app_prepared_profile_routing(profile)));
  configuration->routing.ctx = configuration;
  configuration->routing.owner = configuration_owner;
  configuration->routing.visible = configuration_visible;
  state->result = configuration;
}

static void close_images(void *raw) {
  prepare_state *state = raw;
  qa_app_image_settings_close(state->images.settings);
}
static void discard_identities(void *raw) { qa_app_remote_seat_identities_discard(((prepare_state *)raw)->identities); }
static void close_seed(void *raw) {
  prepare_state *state = raw;
  qa_app_console_script_files *seed = state->seed_scripts;
  state->seed_scripts = NULL;
  if (seed != NULL) qa_app_console_script_files_close(seed);
}

qa_remote_configuration *qa_remote_prepare_configuration(const qa_app_options *options,
    qa_app_configuration_content *content, qa_engine_session *session, qa_app_image_settings *image_settings,
    qa_remote_ownership ownership, const qa_application_host *host,
    qa_remote_cheats_allowed_fn cheats_allowed, void *cheats_ctx,
    qa_remote_configuration *current, qa_app_configuration_content *selected_content, qa_remote_arena *arena) {
  prepare_state state;
  memset(&state, 0, sizeof state);
  state.options = options; state.content = content; state.session = session; state.image_settings = image_settings;
  state.ownership = ownership; state.host = host; state.cheats_allowed = cheats_allowed; state.cheats_ctx = cheats_ctx;
  state.current = current; state.arena = arena;
  state.selected = selected_content;
  if (state.selected == NULL) {
    state.selected = qa_app_remote_configuration_content_native(options, content);
    if (state.selected == NULL) qa_range_error("Out of memory");
    qa_remote_arena_add(arena, release_content, state.selected);
  }
  prepare_retained(&state);
  qa_app_remote_seat_identity *retained = xcalloc(state.retained_count, sizeof *retained);
  for (size_t i = 0; i < state.retained_count; i++) { retained[i].client = state.retained[i].client; retained[i].seat = state.retained[i].seat; }
  state.identities = qa_app_prepare_remote_seat_identities(session, retained, state.retained_count, options->seats);
  free(retained);
  if (state.identities == NULL) qa_range_error("Out of memory");
  qa_remote_arena_add(arena, release_identities, state.identities);
  qa_caught_error caught;
  if (!qa_try(prepare_body, &state, &caught)) {
    /* inner catch: images.settings.close(), wrapping a cleanup failure */
    qa_caught_error primary = caught;
    if (state.has_images) {
      qa_caught_error cleanup;
      if (!qa_try(close_images, &state, &cleanup)) {
        primary.name = "AggregateError";
        snprintf(primary.message, sizeof primary.message, "Remote configuration preparation failed: %.900s; %.900s",
            caught.message, cleanup.message);
      }
    }
    /* outer catch: identities.discard(), seedScripts.close() */
    qa_caught_error failure; bool failed = false;
    if (!qa_try(discard_identities, &state, &failure)) failed = true;
    qa_caught_error seed_failure;
    if (!qa_try(close_seed, &state, &seed_failure) && !failed) { failure = seed_failure; failed = true; }
    if (failed)
      qa_raise("AggregateError", 0, 0, "Remote configuration preparation cleanup failed: %s; %s", primary.message, failure.message);
    qa_rethrow(&primary);
  }
  /* finally { await seedScripts?.close(); } */
  close_seed(&state);
  return state.result;
}
