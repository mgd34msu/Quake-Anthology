/* Port of src/app/bootstrap/startup.ts library services: serverProfileLibrary,
 * movieLibraryMenu, currentDemoLibrary, demoLibraryMenu, configurationLibrary
 * and playerProgressLibrary. The TS promise chains settle synchronously; their
 * generation checks are kept as written. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "startup_application_internal.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/demo_library.h"
#include "qa/app/bootstrap/player_progress_library.h"
#include "qa/app/bootstrap/startup_commands.h"
#include "qa/console/commands.h"
#include "qa/core/game_numeric.h"
#include "qa/core/info_string.h"
#include "qa/foundation/support.h"
#include "qa/settings/server/library.h"

enum { LIBRARY_SERVER_PROFILES, LIBRARY_MOVIES, LIBRARY_DEMOS, LIBRARY_CONFIGURATIONS };

static qa_app_client_bootstrap *capture_client(qa_startup_application *app) {
  if (app->client == NULL) qa_raise("Error", 0, 0, "Client console output has no published owner");
  return app->client;
}
static void set_status(qa_startup_library *library, const char *text) { qa_startup_set_text(&library->status, text); }
static void clear_entries(qa_startup_library *library) {
  for (size_t i = 0; i < library->entry_count; i++) {
    free((char *)library->entries[i].id); free((char *)library->entries[i].label);
    free((char *)library->entries[i].detail); free((char *)library->entries[i].unavailable);
  }
  free(library->entries);
  library->entries = NULL;
  library->entry_count = 0;
}
static void push_entry(qa_startup_library *library, const char *id, const char *label, const char *detail) {
  qa_ui_library_entry *grown = realloc(library->entries, (library->entry_count + 1) * sizeof *grown);
  if (grown == NULL) qa_range_error("Out of memory");
  library->entries = grown;
  qa_ui_library_entry *entry = &library->entries[library->entry_count++];
  memset(entry, 0, sizeof *entry);
  entry->id = qa_startup_dup(id);
  entry->label = qa_startup_dup(label);
  entry->detail = qa_startup_dup(detail);
}
static const qa_ui_library_entry *library_entries(void *ctx, size_t *out_count) {
  qa_startup_library *library = ctx;
  *out_count = library->entry_count;
  return library->entries;
}
static const char *library_status(void *ctx) {
  qa_startup_library *library = ctx;
  return library->status != NULL ? library->status : "";
}
static qa_startup_library *library_new(qa_startup_application *app, int slot) {
  qa_startup_library *library = calloc(1, sizeof *library);
  if (library == NULL) qa_range_error("Out of memory");
  library->app = app;
  library->status = qa_startup_dup("");
  library->service.ctx = library;
  library->service.entries = library_entries;
  library->service.status = library_status;
  app->libraries[slot] = library;
  return library;
}
static bool library_has(const qa_startup_library *library, const char *id) {
  for (size_t i = 0; i < library->entry_count; i++) if (strcmp(library->entries[i].id, id) == 0) return true;
  return false;
}
/* readStartupCommand([`+${name}`, ...args], 0).text */
static char *startup_command_line(const char *name, const char *const *args, size_t arg_count) {
  const char **argv = calloc(arg_count + 2, sizeof *argv);
  if (argv == NULL) qa_range_error("Out of memory");
  size_t length = strlen(name) + 2;
  char *head = malloc(length);
  if (head == NULL) { free(argv); qa_range_error("Out of memory"); }
  snprintf(head, length, "+%s", name);
  argv[0] = head;
  for (size_t i = 0; i < arg_count; i++) argv[i + 1] = args[i];
  qa_app_startup_command command = qa_app_read_startup_command(argv, arg_count + 1, 0);
  free(head);
  free(argv);
  size_t text = strlen(command.text);
  char *line = malloc(text + 2);
  if (line == NULL) { free(command.text); qa_range_error("Out of memory"); }
  memcpy(line, command.text, text);
  line[text] = '\n';
  line[text + 1] = '\0';
  free(command.text);
  return line;
}
static void append_line(qa_command_buffer *commands, char *line, const qa_command_context *context) {
  qa_cleanup guard;
  qa_cleanup_push(&guard, free, line);
  qa_command_buffer_append(commands, line, context, -1);
  qa_cleanup_pop(&guard);
  free(line);
}
static const qa_app_prepared_seat *first_seat(qa_app_client_bootstrap *client) {
  size_t count = 0;
  const qa_app_prepared_seat *seats = qa_app_prepared_startup_seats(client->prepared, &count);
  return count > 0 ? &seats[0] : NULL;
}
static qa_command_context local_context(qa_app_client_bootstrap *client, const qa_app_client_bootstrap_seat *local) {
  qa_command_context context;
  memset(&context, 0, sizeof context);
  context.session = qa_engine_session_id(client->session);
  context.origin.kind = QA_COMMAND_ORIGIN_LOCAL_SEAT;
  context.origin.seat = qa_session_seat_id(local->seat);
  context.origin.client = qa_session_client_id(local->client);
  return context;
}

/* ---- serverProfileLibrary() ----------------------------------------------- */
typedef struct profile_call { qa_startup_library *library; qa_server_profile_entry *profiles; size_t count; } profile_call;
static void profile_list_body(void *ctx) {
  profile_call *call = ctx;
  call->profiles = qa_list_server_profiles(&call->library->store, &call->count);
}
static void server_profile_refresh(void *ctx) {
  qa_startup_library *library = ctx;
  double revision = ++library->generation;
  profile_call call = {library, NULL, 0};
  qa_caught_error caught;
  if (!qa_try(profile_list_body, &call, &caught)) {
    if (revision == library->generation) set_status(library, caught.message);
    return;
  }
  if (revision != library->generation || library->app->closed) {
    qa_server_profile_entries_free(call.profiles, call.count);
    return;
  }
  clear_entries(library);
  for (size_t i = 0; i < call.count; i++) push_entry(library, call.profiles[i].path, call.profiles[i].name, NULL);
  qa_server_profile_entries_free(call.profiles, call.count);
  char text[64];
  snprintf(text, sizeof text, "%zu server profiles", library->entry_count);
  set_status(library, text);
}
static void server_profile_activate(void *ctx, const char *path) {
  qa_startup_library *library = ctx;
  if (!library_has(library, path)) {
    set_status(library, "Server profile is no longer listed");
    return;
  }
  char *selected = qa_settings_path(library->store.root, path);
  if (selected == NULL) qa_range_error("Out of memory");
  qa_cleanup guard;
  qa_cleanup_push(&guard, free, selected);
  qa_app_startup_selection_select_server_profile(library->app->model, selected);
  qa_cleanup_pop(&guard);
  free(selected);
  const char *label = path;
  for (size_t i = 0; i < library->entry_count; i++) if (strcmp(library->entries[i].id, path) == 0) { label = library->entries[i].label; break; }
  size_t length = strlen(label) + 16;
  char *text = malloc(length);
  if (text == NULL) qa_range_error("Out of memory");
  snprintf(text, length, "Selected %s", label);
  free(library->status);
  library->status = text;
}
const qa_ui_library_menu_service *qa_startup_server_profile_library(qa_startup_application *app) {
  qa_startup_library *library = library_new(app, LIBRARY_SERVER_PROFILES);
  char *root = qa_startup_settings_directory(qa_app_startup_saves_directory(app->saves));
  bool stored = qa_config_store_init(&library->store, root);
  free(root);
  if (!stored) qa_range_error("Out of memory");
  library->has_store = true;
  library->service.refresh = server_profile_refresh;
  library->service.activate = server_profile_activate;
  return &library->service;
}

/* ---- movieLibraryMenu() ---------------------------------------------------- */
static void movie_invoke(qa_startup_library *library, const char *name, const char *const *args, size_t arg_count) {
  qa_app_client_bootstrap *client = capture_client(library->app);
  const qa_app_prepared_seat *seat = first_seat(client);
  if (seat == NULL) qa_raise("Error", 0, 0, "Movie command has no local seat");
  append_line(qa_app_prepared_startup_commands(client->prepared), startup_command_line(name, args, arg_count), &seat->context);
}
static void clear_visible(qa_startup_library *library) {
  free(library->visible);
  library->visible = NULL;
  library->visible_count = 0;
}
/* visibleEntries(): Quake III single-player movies stay locked until their tier is complete. */
static const qa_ui_library_entry *movie_visible(void *ctx, size_t *out_count) {
  qa_startup_library *library = ctx;
  qa_app_client_bootstrap *client = library->app->client;
  clear_visible(library);
  if (client == NULL || qa_cvar_registry_dialect(qa_app_prepared_startup_source(client->prepared)) != QA_COMMAND_DIALECT_Q3) {
    *out_count = library->entry_count;
    return library->entries;
  }
  char *videos = qa_cvar_registry_variable_string(qa_app_prepared_startup_source(client->prepared), "g_spVideos");
  qa_cleanup guard;
  qa_cleanup_push(&guard, free, videos);
  library->visible = calloc(library->entry_count + 1, sizeof *library->visible);
  if (library->visible == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < library->entry_count; i++) {
    qa_ui_library_entry entry = library->entries[i];
    const char *slash = strrchr(entry.id, '/');
    const char *base = slash != NULL ? slash + 1 : entry.id;
    char name[256];
    size_t length = strlen(base);
    if (length >= sizeof name) length = sizeof name - 1;
    for (size_t c = 0; c < length; c++) name[c] = (char)tolower((unsigned char)base[c]);
    name[length] = '\0';
    char tier = 0;
    if (strncmp(name, "tier", 4) == 0 && name[4] >= '1' && name[4] <= '7' && strcmp(name + 5, ".roq") == 0) tier = name[4];
    else if (strcmp(name, "end.roq") == 0 || strcmp(name, "demoend.roq") == 0) tier = '8';
    if (tier != 0) {
      char key[8];
      snprintf(key, sizeof key, "tier%c", tier);
      char *value = qa_info_value_for_key(videos, key);
      if (value == NULL) qa_range_error("Out of memory");
      bool locked = qa_game_atoi(value) == 0;
      free(value);
      if (locked) entry.unavailable = "Complete the single-player tier to unlock";
    }
    library->visible[i] = entry;
  }
  library->visible_count = library->entry_count;
  qa_cleanup_pop(&guard);
  free(videos);
  *out_count = library->visible_count;
  return library->visible;
}
static int compare_names(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }
static void free_names(char **names, size_t count) {
  for (size_t i = 0; i < count; i++) free(names[i]);
  free(names);
}
typedef struct movie_list_call {
  qa_app_console_script_files *scripts; qa_command_context context;
  char **names; size_t count;
} movie_list_call;
static void movie_list_body(void *ctx) {
  movie_list_call *call = ctx;
  static const char *const extensions[] = {".roq", ".cin", ".ogv"};
  for (size_t e = 0; e < 3; e++) {
    size_t count = 0;
    char **names = qa_app_console_script_files_list_mounted(call->scripts, "video", extensions[e], call->context, &count);
    char **grown = realloc(call->names, (call->count + count + 1) * sizeof *grown);
    if (grown == NULL) { free_names(names, count); qa_range_error("Out of memory"); }
    call->names = grown;
    for (size_t i = 0; i < count; i++) call->names[call->count++] = names[i];
    free(names);
  }
}
static void movie_refresh(void *ctx) {
  qa_startup_library *library = ctx;
  qa_startup_application *app = library->app;
  qa_app_client_bootstrap *client = capture_client(app);
  const qa_app_prepared_seat *seat = first_seat(client);
  qa_app_console_script_files *scripts = client->configuration_current.scripts;
  double request = ++library->generation;
  if (seat == NULL) qa_raise("Error", 0, 0, "Movie library has no local seat");
  set_status(library, "Reading movies...");
  movie_list_call call;
  memset(&call, 0, sizeof call);
  call.scripts = scripts;
  call.context = seat->context;
  qa_caught_error caught;
  if (!qa_try(movie_list_body, &call, &caught)) {
    free_names(call.names, call.count);
    if (request == library->generation && !app->closed) set_status(library, caught.message);
    return;
  }
  if (request != library->generation || app->closed || client->configuration_current.scripts != scripts) {
    free_names(call.names, call.count);
    return;
  }
  /* [...new Set(lists.flat())].sort() */
  qsort(call.names, call.count, sizeof *call.names, compare_names);
  clear_entries(library);
  clear_visible(library);
  for (size_t i = 0; i < call.count; i++) {
    if (i > 0 && strcmp(call.names[i], call.names[i - 1]) == 0) continue;
    size_t length = strlen(call.names[i]) + 8;
    char *id = malloc(length);
    if (id == NULL) qa_range_error("Out of memory");
    snprintf(id, length, "video/%s", call.names[i]);
    push_entry(library, id, call.names[i], NULL);
    free(id);
  }
  free_names(call.names, call.count);
  char text[64];
  snprintf(text, sizeof text, "%zu movies", library->entry_count);
  set_status(library, text);
}
static void movie_activate(void *ctx, const char *id) {
  qa_startup_library *library = ctx;
  size_t count = 0;
  const qa_ui_library_entry *visible = movie_visible(library, &count);
  for (size_t i = 0; i < count; i++)
    if (strcmp(visible[i].id, id) == 0 && visible[i].unavailable == NULL) {
      const char *args[1] = {id};
      movie_invoke(library, "cinematic", args, 1);
      return;
    }
}
static void movie_stop(void *ctx) { movie_invoke(ctx, "stopcinematic", NULL, 0); }
const qa_ui_library_menu_service *qa_startup_movie_library_menu(qa_startup_application *app) {
  qa_startup_library *library = library_new(app, LIBRARY_MOVIES);
  library->service.entries = movie_visible;
  library->service.refresh = movie_refresh;
  library->service.activate = movie_activate;
  library->service.has_stop = true;
  library->service.stop_label = "Stop movie";
  library->service.stop_activate = movie_stop;
  return &library->service;
}

/* ---- currentDemoLibrary() --------------------------------------------------- */
typedef struct demo_mounts { qa_app_console_script_files *scripts; qa_command_context context; } demo_mounts;
static char **demo_list_files(void *ctx, const char *directory, const char *extension, size_t *out_count) {
  demo_mounts *mounts = ctx;
  return qa_app_console_script_files_list_mounted(mounts->scripts, directory, extension, mounts->context, out_count);
}
static bool demo_read(void *ctx, const char *path, uint8_t **out_bytes, size_t *out_count) {
  return qa_app_console_script_files_read_mounted(((demo_mounts *)ctx)->scripts, path, out_bytes, out_count);
}
typedef struct demo_library_record { qa_app_demo_library *library; demo_mounts mounts; } demo_library_record;
struct qa_app_demo_library *qa_startup_current_demo_library(qa_startup_application *app) {
  qa_app_client_bootstrap *client = capture_client(app);
  if (client->local_count == 0) qa_raise("Error", 0, 0, "Demo library requires a local seat");
  demo_library_record *record = calloc(1, sizeof *record);
  if (record == NULL) qa_range_error("Out of memory");
  record->mounts.scripts = client->configuration_current.scripts;
  record->mounts.context = local_context(client, &client->locals[0]);
  qa_app_demo_library_mounts mounts = {&record->mounts, demo_list_files, demo_read};
  record->library = qa_app_demo_library_create(qa_app_console_script_files_root(record->mounts.scripts), mounts);
  if (record->library == NULL) { free(record); qa_range_error("Out of memory"); }
  void **grown = realloc(app->demo_libraries, (app->demo_library_count + 1) * sizeof *grown);
  if (grown == NULL) qa_range_error("Out of memory");
  app->demo_libraries = grown;
  app->demo_libraries[app->demo_library_count++] = record;
  return record->library;
}
static void demo_libraries_free(qa_startup_application *app) {
  for (size_t i = 0; i < app->demo_library_count; i++) {
    demo_library_record *record = app->demo_libraries[i];
    qa_app_demo_library_destroy(record->library);
    free(record);
  }
  free(app->demo_libraries);
  app->demo_libraries = NULL;
  app->demo_library_count = 0;
}

/* ---- demoLibraryMenu() -------------------------------------------------------- */
static void demo_invoke(qa_startup_library *library, const char *name, const char *const *args, size_t arg_count) {
  qa_app_client_bootstrap *client = capture_client(library->app);
  if (client->local_count == 0) qa_raise("Error", 0, 0, "Demo library requires a local seat");
  qa_command_context context = local_context(client, &client->locals[0]);
  append_line(qa_app_prepared_startup_commands(client->prepared), startup_command_line(name, args, arg_count), &context);
}
/* status: recording?.path === null ? status : (recording?.path ?? status) */
static const char *demo_status(void *ctx) {
  qa_startup_library *library = ctx;
  qa_app_client_demo_recording *recording = library->app->recording;
  const char *path = recording != NULL ? qa_app_client_demo_recording_path(recording) : NULL;
  return path != NULL ? path : library_status(library);
}
static const char *family_label(qa_app_demo_family family) {
  return family == QA_APP_DEMO_Q1 ? "Q1" : family == QA_APP_DEMO_QW ? "QW" : family == QA_APP_DEMO_Q2 ? "Q2" : "Q3";
}
static void demo_refresh(void *ctx) {
  qa_startup_library *library = ctx;
  qa_startup_application *app = library->app;
  double request = ++library->generation;
  qa_app_client_bootstrap *client = capture_client(app);
  qa_app_console_script_files *descriptor_scripts = client->configuration_current.scripts;
  const qa_app_options *descriptor_options = client->configuration_current.options;
  set_status(library, "Reading demos...");
  qa_app_demo_library *demos = qa_startup_current_demo_library(app);
  size_t count = 0;
  const char *error = NULL;
  qa_app_demo_library_entry *list = qa_app_demo_library_list(demos, &count, &error);
  if (error != NULL) {
    qa_app_demo_library_entries_free(list, count);
    if (request == library->generation && !app->closed) set_status(library, error);
    return;
  }
  if (request != library->generation || app->closed || client->configuration_current.scripts != descriptor_scripts
      || client->configuration_current.options != descriptor_options) {
    qa_app_demo_library_entries_free(list, count);
    return;
  }
  clear_entries(library);
  for (size_t i = 0; i < count; i++) push_entry(library, list[i].id, list[i].label, family_label(list[i].family));
  qa_app_demo_library_entries_free(list, count);
  char text[64];
  snprintf(text, sizeof text, "%zu demos", library->entry_count);
  set_status(library, text);
}
static void demo_activate(void *ctx, const char *id) {
  qa_startup_library *library = ctx;
  if (!library_has(library, id)) return;
  const char *args[1] = {id};
  demo_invoke(library, "playdemo", args, 1);
}
static void demo_record(void *ctx, const char *name) {
  const char *args[1] = {name};
  demo_invoke(ctx, "record", args, 1);
}
static void demo_stop(void *ctx) { demo_invoke(ctx, "stop", NULL, 0); }
const qa_ui_library_menu_service *qa_startup_demo_library_menu(qa_startup_application *app) {
  qa_startup_library *library = library_new(app, LIBRARY_DEMOS);
  library->service.status = demo_status;
  library->service.refresh = demo_refresh;
  library->service.activate = demo_activate;
  library->service.has_create = true;
  library->service.create_label = "Record";
  library->service.create_submit = demo_record;
  library->service.has_stop = true;
  library->service.stop_label = "Stop recording";
  library->service.stop_activate = demo_stop;
  return &library->service;
}

/* ---- configurationLibrary() ------------------------------------------------- */
static void release_completion(qa_startup_library *library) {
  if (!library->has_release_completion) return;
  library->has_release_completion = false;
  qa_app_client_bootstrap *client = library->app->client;
  if (client != NULL) qa_command_buffer_unbind_script_completion(qa_app_prepared_startup_commands(client->prepared), library->release_completion);
  free(library->completion_expected);
  library->completion_expected = NULL;
  qa_command_context_release(&library->completion_local);
}
static void configuration_completion(void *ctx, const qa_script_completion *event) {
  qa_startup_library *library = ctx;
  qa_startup_application *app = library->app;
  qa_app_client_bootstrap *client = app->client;
  if (app->closed || client == NULL || (const void *)client->configuration_current.scripts != library->completion_descriptor) {
    release_completion(library);
    return;
  }
  const qa_command_origin *origin = &event->source.origin;
  const qa_command_origin *caller = origin->kind == QA_COMMAND_ORIGIN_SCRIPT ? origin->caller : NULL;
  const qa_command_origin *local = &library->completion_local.origin;
  if (strcmp(event->name, library->completion_expected) != 0 || caller == NULL || caller->kind != QA_COMMAND_ORIGIN_LOCAL_SEAT
      || local->kind != QA_COMMAND_ORIGIN_LOCAL_SEAT || !qa_same_seat(caller->seat, local->seat) || !qa_same_client(caller->client, local->client))
    return;
  release_completion(library);
  library->generation++;
  char *name = qa_startup_dup(event->name);
  size_t length = strlen(name) + (event->error_message != NULL ? strlen(event->error_message) : 0) + 32;
  char *text = malloc(length);
  if (text == NULL) { free(name); qa_range_error("Out of memory"); }
  if (event->result == QA_SCRIPT_COMPLETED) snprintf(text, length, "Finished %s", name);
  else if (event->result == QA_SCRIPT_MISSING) snprintf(text, length, "Not found: %s", name);
  else snprintf(text, length, "Could not read %s: %s", name, event->error_message != NULL ? event->error_message : "");
  free(name);
  free(library->status);
  library->status = text;
}
typedef struct submit_call { qa_startup_library *library; const char *name; const char *path; } submit_call;
static void submit_body(void *ctx) {
  submit_call *call = ctx;
  qa_startup_library *library = call->library;
  qa_app_client_bootstrap *client = capture_client(library->app);
  const qa_app_prepared_seat *seat = first_seat(client);
  if (seat == NULL) qa_raise("Error", 0, 0, "Configuration command has no local seat");
  release_completion(library);
  const char *args[1] = {call->path};
  char *line = startup_command_line(call->name, args, 1);
  qa_cleanup guard;
  qa_cleanup_push(&guard, free, line);
  library->generation++;
  qa_command_buffer *commands = qa_app_prepared_startup_commands(client->prepared);
  if (strcmp(call->name, "exec") == 0) {
    const char *slash = strrchr(call->path, '/');
    const char *base = slash != NULL ? slash + 1 : call->path;
    bool q3 = qa_command_buffer_dialect(commands) == QA_COMMAND_DIALECT_Q3;
    size_t length = strlen(call->path) + 8;
    library->completion_expected = malloc(length);
    if (library->completion_expected == NULL) qa_range_error("Out of memory");
    snprintf(library->completion_expected, length, q3 && strchr(base, '.') == NULL ? "%s.cfg" : "%s", call->path);
    library->completion_descriptor = client->configuration_current.scripts;
    library->completion_local = qa_command_context_copy(&seat->context);
    library->release_completion = qa_command_buffer_bind_script_completion(commands, configuration_completion, library);
    library->has_release_completion = true;
  }
  qa_cleanup_pop(&guard);
  append_line(commands, line, &seat->context);
  size_t length = strlen(call->path) + 16;
  char *text = malloc(length);
  if (text == NULL) qa_range_error("Out of memory");
  snprintf(text, length, "%s %s", strcmp(call->name, "exec") == 0 ? "Queued" : "Saving", call->path);
  free(library->status);
  library->status = text;
}
static void configuration_submit(qa_startup_library *library, const char *name, const char *path) {
  submit_call call = {library, name, path};
  qa_caught_error caught;
  if (!qa_try(submit_body, &call, &caught)) set_status(library, caught.message);
}
typedef struct list_call { qa_app_console_script_files *scripts; qa_command_context context; qa_app_console_script_entry *files; size_t count; } list_call;
static void list_body(void *ctx) {
  list_call *call = ctx;
  call->files = qa_app_console_script_files_list(call->scripts, call->context, &call->count);
}
static void free_files(qa_app_console_script_entry *files, size_t count) {
  for (size_t i = 0; i < count; i++) free(files[i].name);
  free(files);
}
static void configuration_refresh_with(qa_startup_library *library, const char *message) {
  qa_startup_application *app = library->app;
  double revision = ++library->generation;
  const void *descriptor = app->client != NULL ? (const void *)app->client->configuration_current.scripts : NULL;
  list_call call;
  memset(&call, 0, sizeof call);
  qa_caught_error caught;
  bool listed = false;
  {
    qa_app_client_bootstrap *client = app->client;
    const qa_app_prepared_seat *seat = client != NULL ? first_seat(client) : NULL;
    if (client == NULL) snprintf(caught.message, sizeof caught.message, "Client console output has no published owner");
    else if (seat == NULL) snprintf(caught.message, sizeof caught.message, "Configuration library has no local seat");
    else {
      call.scripts = client->configuration_current.scripts;
      call.context = seat->context;
      listed = qa_try(list_body, &call, &caught);
    }
  }
  if (!listed) {
    if (revision == library->generation && !app->closed && app->client != NULL
        && (const void *)app->client->configuration_current.scripts == descriptor)
      set_status(library, caught.message);
    return;
  }
  if (revision != library->generation || app->closed || app->client->configuration_current.scripts != call.scripts) {
    free_files(call.files, call.count);
    return;
  }
  clear_entries(library);
  for (size_t i = 0; i < call.count; i++)
    push_entry(library, call.files[i].name, call.files[i].name, strcmp(call.files[i].kind, "seat") == 0 ? "Current player" : "Selected game");
  free_files(call.files, call.count);
  if (message != NULL) set_status(library, message);
  else {
    char text[64];
    snprintf(text, sizeof text, "%zu configuration files", library->entry_count);
    set_status(library, text);
  }
}
static void configuration_refresh(void *ctx) { configuration_refresh_with(ctx, NULL); }
static void configuration_activate(void *ctx, const char *path) { configuration_submit(ctx, "exec", path); }
static void configuration_create(void *ctx, const char *path) { configuration_submit(ctx, "writeconfig", path); }
/* configurationWriteStarted's result closure: one record per write. */
typedef struct write_notify {
  qa_startup_library *library; double revision; const void *descriptor; char *path;
} write_notify;
static void write_done(void *ctx, qa_configuration_write_result result) {
  write_notify *notify = ctx;
  qa_startup_library *library = notify->library;
  qa_startup_application *app = library->app;
  if (notify->revision != library->generation || app->closed || app->client == NULL
      || (const void *)app->client->configuration_current.scripts != notify->descriptor)
    return;
  size_t length = strlen(notify->path) + (result.error_message != NULL ? strlen(result.error_message) : 0) + 32;
  char *text = malloc(length);
  if (text == NULL) qa_range_error("Out of memory");
  if (result.kind == QA_CONFIGURATION_WRITTEN) {
    snprintf(text, length, "Saved %s", notify->path);
    free(library->status);
    library->status = text;
    char *message = qa_startup_dup(text);
    qa_cleanup guard;
    qa_cleanup_push(&guard, free, message);
    configuration_refresh_with(library, message);
    qa_cleanup_pop(&guard);
    free(message);
  } else {
    library->generation++;
    snprintf(text, length, "Could not save %s: %s", notify->path, result.error_message != NULL ? result.error_message : "");
    free(library->status);
    library->status = text;
  }
}
qa_configuration_write_notify qa_startup_configuration_write_started_notify(qa_startup_application *app, qa_command_context source,
    const char *path) {
  qa_configuration_write_notify none = {NULL, NULL};
  qa_startup_library *library = app->libraries[LIBRARY_CONFIGURATIONS];
  if (!app->has_configuration_write_started || library == NULL) return none;
  qa_app_client_bootstrap *client = capture_client(app);
  const void *descriptor = client->configuration_current.scripts;
  const qa_app_prepared_seat *seat = first_seat(client);
  const qa_command_origin *origin = &source.origin;
  while (origin->kind == QA_COMMAND_ORIGIN_SCRIPT) origin = origin->caller;
  const qa_command_origin *local = seat != NULL ? &seat->context.origin : NULL;
  if (origin->kind != QA_COMMAND_ORIGIN_LOCAL_SEAT || local == NULL || local->kind != QA_COMMAND_ORIGIN_LOCAL_SEAT
      || !qa_same_seat(origin->seat, local->seat) || !qa_same_client(origin->client, local->client))
    return none;
  write_notify *notify = calloc(1, sizeof *notify);
  if (notify == NULL) qa_range_error("Out of memory");
  notify->library = library;
  notify->revision = ++library->generation;
  notify->descriptor = descriptor;
  notify->path = qa_startup_dup(path);
  void **grown = realloc(app->write_notifies, (app->write_notify_count + 1) * sizeof *grown);
  if (grown == NULL) qa_range_error("Out of memory");
  app->write_notifies = grown;
  app->write_notifies[app->write_notify_count++] = notify;
  qa_configuration_write_notify result = {write_done, notify};
  return result;
}
static void release_configuration(qa_startup_application *app, void *ctx) {
  release_completion(ctx);
  app->has_configuration_write_started = false;
}
const qa_ui_library_menu_service *qa_startup_configuration_library(qa_startup_application *app) {
  qa_startup_library *library = library_new(app, LIBRARY_CONFIGURATIONS);
  qa_startup_push_release(app, release_configuration, library);
  library->service.refresh = configuration_refresh;
  library->service.activate = configuration_activate;
  library->service.has_create = true;
  library->service.create_label = "Save config";
  library->service.create_submit = configuration_create;
  app->has_configuration_write_started = true;
  return &library->service;
}

/* ---- playerProgressLibrary() ------------------------------------------------- */
typedef struct progress_state {
  qa_startup_application *app;
  qa_app_player_progress_library *library;
  qa_ui_library_menu_service service;
  const void *cached_descriptor; qa_app_player_progress_store *cached; bool has_cached;
  qa_app_player_progress_store **stores; size_t store_count;
  char participant[64];
} progress_state;
typedef struct progress_open { const char *file; qa_app_player_progress_store *store; } progress_open;
static void progress_open_body(void *ctx) {
  progress_open *open = ctx;
  open->store = qa_app_player_progress_store_open(open->file);
}
static char error_text[sizeof ((qa_caught_error *)0)->message];
static qa_app_player_progress_store *progress_store(void *ctx, const char **error_out) {
  progress_state *state = ctx;
  *error_out = NULL;
  qa_startup_application *app = state->app;
  if (app->client == NULL) { *error_out = "Client console output has no published owner"; return NULL; }
  qa_app_console_script_files *scripts = app->client->configuration_current.scripts;
  const void *descriptor = scripts;
  if (!state->has_cached || state->cached_descriptor != descriptor) {
    const char *root = qa_app_console_script_files_root(scripts);
    size_t length = strlen(root) + 32;
    char *file = malloc(length);
    if (file == NULL) qa_range_error("Out of memory");
    snprintf(file, length, "%s/player-progress.json", root);
    progress_open open = {file, NULL};
    qa_caught_error caught;
    bool opened = qa_try(progress_open_body, &open, &caught);
    free(file);
    if (!opened) {
      state->has_cached = false;
      snprintf(error_text, sizeof error_text, "%s", caught.message);
      *error_out = error_text;
      return NULL;
    }
    qa_app_player_progress_store **grown = realloc(state->stores, (state->store_count + 1) * sizeof *grown);
    if (grown == NULL) qa_range_error("Out of memory");
    state->stores = grown;
    state->stores[state->store_count++] = open.store;
    state->cached = open.store;
    state->cached_descriptor = descriptor;
    state->has_cached = true;
  }
  if (app->client == NULL || (const void *)app->client->configuration_current.scripts != descriptor) {
    state->has_cached = false;
    *error_out = "Player progress profile changed during loading";
    return NULL;
  }
  return state->cached;
}
static const char *progress_participant(void *ctx) {
  progress_state *state = ctx;
  qa_startup_application *app = state->app;
  qa_app_client_bootstrap *client = capture_client(app);
  if (client->local_count == 0) qa_raise("Error", 0, 0, "Player progress requires a local seat");
  snprintf(state->participant, sizeof state->participant, "local-seat:%lld", (long long)qa_session_seat_id(client->locals[0].seat).index);
  return state->participant;
}
const qa_ui_library_menu_service *qa_startup_player_progress_library(qa_startup_application *app) {
  progress_state *state = calloc(1, sizeof *state);
  if (state == NULL) qa_range_error("Out of memory");
  state->app = app;
  state->library = qa_app_player_progress_library_create(progress_store, state, progress_participant, state);
  if (state->library == NULL) { free(state); qa_range_error("Out of memory"); }
  state->service = qa_app_player_progress_library_as_service(state->library);
  app->player_progress = state;
  return &state->service;
}

void qa_startup_libraries_free(qa_startup_application *app) {
  for (size_t slot = 0; slot < 5; slot++) {
    qa_startup_library *library = app->libraries[slot];
    if (library == NULL) continue;
    release_completion(library);
    clear_entries(library);
    clear_visible(library);
    free(library->status);
    if (library->has_store) qa_config_store_destroy(&library->store);
    free(library);
    app->libraries[slot] = NULL;
  }
  progress_state *state = app->player_progress;
  if (state != NULL) {
    qa_app_player_progress_library_destroy(state->library);
    for (size_t i = 0; i < state->store_count; i++) qa_app_player_progress_store_destroy(state->stores[i]);
    free(state->stores);
    free(state);
    app->player_progress = NULL;
  }
  for (size_t i = 0; i < app->write_notify_count; i++) {
    write_notify *notify = app->write_notifies[i];
    free(notify->path);
    free(notify);
  }
  free(app->write_notifies);
  app->write_notifies = NULL;
  app->write_notify_count = 0;
  demo_libraries_free(app);
}
