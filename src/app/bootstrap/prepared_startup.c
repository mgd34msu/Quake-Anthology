/* Port of src/app/bootstrap/prepared-startup.ts. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "qa/app/bootstrap/prepared_startup.h"
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/audio/commands.h"
#include "qa/app/bootstrap/gtv_commands.h"
#include "qa/app/bootstrap/player_userinfo.h"
#include "qa/app/bootstrap/q1_client_settings.h"
#include "qa/app/bootstrap/server_administration.h"
#include "qa/app/bootstrap/shared_setting_cvars.h"
#include "qa/app/bootstrap/startup_commands.h"
#include "qa/foundation/support.h"
#include "qa/input/support.h"
#include "qa/foundation/js.h"

bool qa_app_allow_seat_configuration_command(qa_command_context source) {
  const qa_command_origin *origin = &source.origin;
  while (origin != NULL && origin->kind == QA_COMMAND_ORIGIN_SCRIPT) origin = origin->caller;
  if (origin == NULL) return true;
  return origin->kind == QA_COMMAND_ORIGIN_LOCAL_SEAT
      || origin->kind == QA_COMMAND_ORIGIN_SERVER_CONSOLE;
}

/* Per-seat choice state parallel to the public seat array. */
typedef struct seat_state {
  char **overridden_keys; size_t overridden_count, overridden_capacity;
  bool has_selected; qa_input_binding *selected; size_t selected_count;
  bool has_authored; qa_input_binding *authored; size_t authored_count;
  bool owns_input;
} seat_state;

typedef struct output_binding {
  int64_t handle;
  qa_command_print_source_fn print; void *ctx;
} output_binding;

typedef struct registry_output {
  qa_cvar_registry *registry;
  int64_t binding;
  struct registry_output_ctx *ctx;
} registry_output;
struct registry_output_ctx {
  qa_app_prepared_startup *startup;
  qa_cvar_registry *registry;
};

typedef struct named_forward {
  qa_app_prepared_startup *startup;
  char *name;
  int kind; /* 0 deferred, 1 operator, 2 plain */
} named_forward;

typedef enum run_stage { RUN_NONE, RUN_SEATS, RUN_LATE, RUN_DONE } run_stage;
typedef struct run_state {
  run_stage stage;
  bool started;
  qa_app_prepared_execute_options options;
  qa_app_startup_phases phases;
  qa_command_context command_context;
  size_t index;
  bool seat_started;
  bool late_appended;
} run_state;

typedef struct apply_ctx {
  qa_app_prepared_startup *startup;
  size_t index; /* configuration index; seat undefined when no seats */
} apply_ctx;

struct qa_app_prepared_startup {
  qa_command_buffer *commands;
  qa_app_prepared_startup_host host;
  qa_app_prepared_seat *seats; size_t seat_count;
  bool closed;
  /* Concrete state (legacy create leaves this zeroed). */
  bool concrete;
  bool owns_commands;
  qa_cvar_registry *source, *movement, *fallback;
  bool owns_fallback;
  qa_app_console_script_files *scripts;
  qa_app_prepared_startup_options options;
  qa_app_prepared_seat_configuration *seat_configs; size_t seat_config_count;
  seat_state *states;
  qa_seat_id *active_seat_ids; size_t active_seat_count;
  qa_binding_store *console_bindings;
  qa_binding_command_seat console_seat;
  qa_app_console_routing *routing_owner;
  qa_command_cvar_routing routing;
  qa_q3_product_policy q3_policy;
  char **deferred; size_t deferred_count;
  named_forward **forwards; size_t forward_count, forward_capacity;
  qa_binding_commands_registration *binding_registration;
  qa_app_q1_view_commands *release_view;
  output_binding *outputs; size_t output_count, output_capacity;
  int64_t next_output_handle;
  registry_output *registry_outputs; size_t registry_output_count, registry_output_capacity;
  qa_app_startup_config *active;
  qa_app_prepared_configuration_continuation *continuations;
  size_t continuation_count, continuation_capacity;
  qa_app_prepared_scoped_read_fn scoped_read; void *scoped_read_ctx;
  bool world_action;
  run_state run;
  apply_ctx apply;
};

static void *xmalloc(size_t size) {
  void *p = malloc(size == 0 ? 1 : size);
  if (p == NULL) qa_range_error("Out of memory");
  return p;
}
static void *xcalloc(size_t count, size_t size) {
  void *p = calloc(count == 0 ? 1 : count, size);
  if (p == NULL) qa_range_error("Out of memory");
  return p;
}
static char *xstrdup(const char *text) {
  char *copy = strdup(text);
  if (copy == NULL) qa_range_error("Out of memory");
  return copy;
}
static const qa_command_origin *root_origin(const qa_command_context *source) {
  const qa_command_origin *origin = &source->origin;
  while (origin->kind == QA_COMMAND_ORIGIN_SCRIPT && origin->caller != NULL) origin = origin->caller;
  return origin;
}
static bool seat_is_active(const qa_app_prepared_startup *startup, qa_seat_id id) {
  for (size_t i = 0; i < startup->active_seat_count; i++)
    if (qa_same_seat(startup->active_seat_ids[i], id)) return true;
  return false;
}
static qa_app_prepared_seat *find_seat(qa_app_prepared_startup *startup, qa_seat_id id, size_t *out_index) {
  for (size_t i = 0; i < startup->seat_count; i++)
    if (qa_same_seat(startup->seats[i].id, id)) {
      if (out_index != NULL) *out_index = i;
      return &startup->seats[i];
    }
  return NULL;
}
static bool current_context(const qa_app_prepared_startup *startup, qa_command_context context) {
  const qa_command_origin *origin = root_origin(&context);
  if (origin->kind != QA_COMMAND_ORIGIN_LOCAL_SEAT) return true;
  if (!seat_is_active(startup, origin->seat)) return false;
  for (size_t i = 0; i < startup->seat_count; i++) {
    const qa_command_origin *seat_origin = &startup->seats[i].context.origin;
    if (qa_same_seat(startup->seats[i].id, origin->seat)
        && seat_origin->kind == QA_COMMAND_ORIGIN_LOCAL_SEAT
        && qa_same_client(seat_origin->client, origin->client)) return true;
  }
  return false;
}
static bool startup_pending(const qa_app_prepared_startup *startup) {
  return startup->continuation_count != 0 || startup->run.stage != RUN_NONE;
}
static qa_app_startup_config *current_startup(qa_app_prepared_startup *startup) {
  if (startup->continuation_count == 0) return startup->active;
  qa_app_prepared_configuration_continuation *profile = &startup->continuations[startup->continuation_count - 1];
  return profile->active != NULL ? profile->active(profile->ctx) : NULL;
}
static void prepared_print(qa_app_prepared_startup *startup, const char *text, const qa_command_context *source) {
  const qa_command_context *context = source;
  if (context == NULL && startup->commands != NULL) context = qa_command_buffer_execution_context(startup->commands);
  if (context != NULL && !current_context(startup, *context)) return;
  qa_command_print_source_fn output = startup->options.print;
  void *output_ctx = startup->options.print_ctx;
  for (size_t i = 0; i < startup->output_count; i++) {
    output = startup->outputs[i].print;
    output_ctx = startup->outputs[i].ctx;
  }
  if (output != NULL) output(output_ctx, text, context);
}
static void print_adapter(void *ctx, const char *text, const qa_command_context *source) {
  prepared_print(ctx, text, source);
}
static void print_text_adapter(void *ctx, const char *text) {
  prepared_print(ctx, text, NULL);
}
static void registry_print_adapter(void *ctx, const char *text) {
  struct registry_output_ctx *binding = ctx;
  const qa_command_context *context = qa_command_buffer_execution_context(binding->startup->commands);
  qa_command_context own = {0};
  if (context == NULL) {
    own = qa_cvar_registry_context(binding->registry);
    context = &own;
  }
  prepared_print(binding->startup, text, context);
}
static void set_cvar(qa_cvar_registry *registry, const char *name, const char *value, bool force) {
  qa_cvar_snapshot snapshot = {0};
  if (qa_cvar_registry_set(registry, name, value, force, &snapshot)) qa_cvar_read_free(&snapshot);
}
static void forward_call(qa_app_prepared_startup *startup, const char *name,
    const char *const *args, size_t arg_count, qa_command_context source) {
  if (startup->options.forward != NULL) startup->options.forward(startup->options.forward_ctx, name, args, arg_count, source);
}
/* --- routing callbacks (constructor closure over options.seats) --- */
static qa_command_dialect routing_source_dialect(void *ctx) {
  return ((qa_app_prepared_startup *)ctx)->options.dialect;
}
static bool routing_server(void *ctx, qa_app_console_server *out) {
  qa_app_prepared_startup *startup = ctx;
  out->cvars = startup->source;
  out->shared_names = startup->options.shared_names;
  out->shared_name_count = startup->options.shared_name_count;
  return true;
}
static qa_cvar_registry *routing_seat(void *ctx, qa_seat_id id) {
  qa_app_prepared_startup *startup = ctx;
  for (size_t i = 0; i < startup->seat_config_count; i++)
    if (qa_same_seat(startup->seat_configs[i].id, id)) return startup->seat_configs[i].cvars;
  return NULL;
}
static qa_cvar_registry *routing_input(void *ctx, bool has_seat, qa_seat_id id) {
  qa_app_prepared_startup *startup = ctx;
  for (size_t i = 0; i < startup->seat_config_count; i++)
    if (!has_seat || qa_same_seat(startup->seat_configs[i].id, id)) return startup->seat_configs[i].mouse.cvars;
  return NULL;
}
static qa_cvar_registry *routing_movement(void *ctx) {
  return ((qa_app_prepared_startup *)ctx)->movement;
}
static qa_cvar_registry *routing_shared(void *ctx) {
  return ((qa_app_prepared_startup *)ctx)->options.shared;
}
/* --- command buffer callbacks --- */
static void refresh_registry_output(qa_app_prepared_startup *startup);
static qa_script_read_result read_script_adapter(void *ctx, const char *name, qa_command_context source) {
  qa_script_read_result result = {0};
  result.kind = QA_SCRIPT_READ_READY;
  result.text = qa_app_prepared_startup_read_script(ctx, name, source);
  result.has_text = result.text != NULL;
  return result;
}
static void script_complete_adapter(void *ctx, const qa_script_completion *event) {
  qa_app_startup_config *active = current_startup(ctx);
  if (active != NULL) qa_app_startup_config_on_script_complete(active, event);
}
static void forward_to_server_adapter(void *ctx, qa_command_invocation *command) {
  qa_app_prepared_startup *startup = ctx;
  startup->world_action = true;
  const char *name = command->argc > 0 ? command->argv[0] : "";
  forward_call(startup, name, command->argc > 1 ? (const char *const *)command->argv + 1 : NULL,
               command->argc > 1 ? command->argc - 1 : 0, command->source);
}
static bool allow_command_adapter(void *ctx, qa_command_invocation *command) {
  qa_app_prepared_startup *startup = ctx;
  const char *raw = command->argc > 0 ? command->argv[0] : "";
  char *name = qa_ascii_fold(raw);
  bool blocked = false;
  if (startup->options.dialect == QA_COMMAND_DIALECT_Q3
      && (strcmp(name, "devmap") == 0 || strcmp(name, "spmap") == 0 || strcmp(name, "spdevmap") == 0)) {
    size_t count = 0;
    const char *const *maps = qa_q3_product_map_commands(startup->q3_policy, &count);
    bool listed = false;
    for (size_t i = 0; i < count; i++) if (strcmp(maps[i], name) == 0) listed = true;
    if (!listed) {
      char message[512];
      (void)snprintf(message, sizeof message, "Unknown command \"%s\"\n", name);
      prepared_print(startup, message, &command->source);
      blocked = true;
    }
  }
  if (!blocked && !current_context(startup, command->source)) {
    if (startup->options.print != NULL)
      startup->options.print(startup->options.print_ctx,
          "Command ignored because its local client is inactive or has retired.\n", NULL);
    blocked = true;
  }
  if (!blocked) {
    if (startup_pending(startup)) {
      for (size_t i = 0; i < startup->deferred_count; i++)
        if (strcmp(startup->deferred[i], name) == 0) startup->world_action = true;
    }
    qa_app_startup_config *active = current_startup(startup);
    if (active != NULL && qa_app_startup_config_restrict_shared_configuration(active))
      blocked = !qa_app_allow_seat_configuration_command_for(command, startup->routing,
          startup->seats, startup->seat_count, print_adapter, startup);
  }
  free(name);
  return !blocked;
}
static bool ui_event_never(void *ctx, const qa_seat_input_event *event, qa_seat_input_focus focus) {
  (void)ctx; (void)event; (void)focus;
  return false;
}
static bool binding_lookup(void *ctx, qa_seat_id id, qa_binding_command_seat *out) {
  qa_app_prepared_startup *startup = ctx;
  qa_app_prepared_seat *seat = find_seat(startup, id, NULL);
  if (seat == NULL || seat->input == NULL) return false;
  *out = qa_seat_input_command_seat(seat->input);
  return true;
}
static void named_forward_handler(void *ctx, qa_command_invocation *command) {
  named_forward *named = ctx;
  qa_app_prepared_startup *startup = named->startup;
  const char *name = named->name;
  const char *const *args = command->argc > 1 ? (const char *const *)command->argv + 1 : NULL;
  size_t arg_count = command->argc > 1 ? command->argc - 1 : 0;
  if (named->kind == 0) startup->world_action = true;
  if (named->kind == 1) {
    qa_command_dialect dialect = startup->options.dialect;
    if (strcmp(name, "setmaster") == 0 && startup->options.seat_count == 0
        && (dialect == QA_COMMAND_DIALECT_Q2_CLASSIC || dialect == QA_COMMAND_DIALECT_Q2_RERELEASE))
      set_cvar(startup->source, "public", "1", false);
    if (strcmp(name, "addlrconcmd") == 0 || strcmp(name, "dellrconcmd") == 0) {
      const char *text_args[] = {command->args_text != NULL ? command->args_text : ""};
      forward_call(startup, name, text_args, 1, command->source);
      return;
    }
  }
  forward_call(startup, name, args, arg_count, command->source);
}
static void register_named(qa_app_prepared_startup *startup, const char *name, int kind,
    const qa_command_documentation *documentation) {
  if (startup->forward_count == startup->forward_capacity) {
    size_t capacity = startup->forward_capacity == 0 ? 16 : startup->forward_capacity * 2;
    named_forward **grown = realloc(startup->forwards, capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("Out of memory");
    startup->forwards = grown;
    startup->forward_capacity = capacity;
  }
  named_forward *named = xmalloc(sizeof *named);
  startup->forwards[startup->forward_count++] = named;
  named->startup = startup;
  named->name = xstrdup(name);
  named->kind = kind;
  (void)qa_command_buffer_register(startup->commands, name, named_forward_handler, named,
      documentation, QA_COMMAND_WORLD);
}
/* --- writeconfig (dedicated console) --- */
typedef struct write_config_op {
  qa_config_store *store;
  const char *path;
  const char *contents;
} write_config_op;
static const char *write_config_run(void *ctx) {
  write_config_op *op = ctx;
  return qa_config_store_dump(op->store, op->path, op->contents) ? NULL : "Unable to write configuration";
}
static void writeconfig_handler(void *ctx, qa_command_invocation *command) {
  qa_app_prepared_startup *startup = ctx;
  const qa_command_origin *origin = root_origin(&command->source);
  if (origin->kind != QA_COMMAND_ORIGIN_LOCAL_CONSOLE && origin->kind != QA_COMMAND_ORIGIN_SERVER_CONSOLE) {
    prepared_print(startup, "writeconfig requires a local console.\n", &command->source);
    return;
  }
  if (command->argc > 2) {
    prepared_print(startup, "writeconfig [filename]\n", &command->source);
    return;
  }
  const char *name = command->argc > 1 ? command->argv[1] : "config.cfg";
  size_t name_length = strlen(name);
  bool has_extension = name_length >= 4 && strcmp(name + name_length - 4, ".cfg") == 0;
  char *path = xmalloc(name_length + 5);
  (void)snprintf(path, name_length + 5, "%s%s", name, has_extension ? "" : ".cfg");
  qa_binding_store *bindings = startup->console_bindings;
  if (bindings == NULL || startup->scripts == NULL) { free(path); return; }
  size_t binding_count = 0;
  const qa_input_binding *owned = qa_binding_store_bindings(bindings, &binding_count);
  size_t archived_count = 0;
  char **archived = qa_archived_bindings(owned, binding_count, true, &archived_count);
  size_t command_count = 0;
  char **commands = qa_command_buffer_archive_commands(startup->commands, &command->source, &command_count);
  const char *header = "// Generated by quake-typescript\n";
  size_t total = strlen(header) + 2;
  for (size_t i = 0; i < archived_count; i++) total += strlen(archived[i]) + 1;
  for (size_t i = 0; i < command_count; i++) total += strlen(commands[i]) + 1;
  char *contents = xmalloc(total);
  char *cursor = contents;
  cursor += sprintf(cursor, "%s", header);
  bool first = true;
  for (size_t i = 0; i < archived_count + command_count; i++) {
    const char *line = i < archived_count ? archived[i] : commands[i - archived_count];
    cursor += sprintf(cursor, "%s%s", first ? "" : "\n", line);
    first = false;
  }
  cursor += sprintf(cursor, "\n");
  qa_config_store store = {NULL};
  const char *error = NULL;
  bool store_open = qa_config_store_init(&store, qa_app_console_script_files_root(startup->scripts));
  if (!store_open) {
    error = "Unable to open configuration store";
  } else {
    write_config_op op = {&store, path, contents};
    error = qa_app_console_script_files_write(startup->scripts, write_config_run, &op);
  }
  char message[1024];
  if (error == NULL) (void)snprintf(message, sizeof message, "Wrote %s\n", path);
  else (void)snprintf(message, sizeof message, "Console output failed: %s\n", error);
  prepared_print(startup, message, &command->source);
  if (store_open) qa_config_store_destroy(&store);
  for (size_t i = 0; i < archived_count; i++) free(archived[i]);
  free(archived);
  for (size_t i = 0; i < command_count; i++) free(commands[i]);
  free(commands);
  free(contents);
  free(path);
}
/* --- registry output refresh --- */
static void refresh_registry_output(qa_app_prepared_startup *startup) {
  qa_cvar_registry *wanted[64];
  size_t wanted_count = 0;
  if (startup->output_count != 0 && startup->commands != NULL) {
    qa_cvar_registry *base[] = {startup->source, startup->movement, startup->fallback};
    for (size_t i = 0; i < 3; i++) {
      bool seen = false;
      for (size_t j = 0; j < wanted_count; j++) if (wanted[j] == base[i]) seen = true;
      if (!seen && base[i] != NULL && wanted_count < 64) wanted[wanted_count++] = base[i];
    }
    size_t context_count = startup->active_seat_count + 1;
    for (size_t c = 0; c < context_count; c++) {
      qa_command_context context = c == 0 ? qa_cvar_registry_context(startup->source) : (qa_command_context){0};
      if (c > 0) {
        qa_app_prepared_seat *seat = find_seat(startup, startup->active_seat_ids[c - 1], NULL);
        if (seat == NULL) continue;
        context = seat->context;
      }
      if (!current_context(startup, context) || startup->routing.visible == NULL) continue;
      size_t visible_count = 0;
      qa_cvar_registry *const *visible = startup->routing.visible(startup->routing.ctx, context, &visible_count);
      for (size_t i = 0; i < visible_count; i++) {
        bool seen = false;
        for (size_t j = 0; j < wanted_count; j++) if (wanted[j] == visible[i]) seen = true;
        if (!seen && wanted_count < 64) wanted[wanted_count++] = visible[i];
      }
    }
  }
  for (size_t i = 0; i < startup->registry_output_count;) {
    bool keep = false;
    for (size_t j = 0; j < wanted_count; j++) if (wanted[j] == startup->registry_outputs[i].registry) keep = true;
    if (keep) { i++; continue; }
    qa_cvar_registry_unbind_output(startup->registry_outputs[i].registry, startup->registry_outputs[i].binding);
    free(startup->registry_outputs[i].ctx);
    startup->registry_outputs[i] = startup->registry_outputs[--startup->registry_output_count];
  }
  for (size_t i = 0; i < wanted_count; i++) {
    bool bound = false;
    for (size_t j = 0; j < startup->registry_output_count; j++)
      if (startup->registry_outputs[j].registry == wanted[i]) bound = true;
    if (bound) continue;
    if (startup->registry_output_count == startup->registry_output_capacity) {
      size_t capacity = startup->registry_output_capacity == 0 ? 8 : startup->registry_output_capacity * 2;
      registry_output *grown = realloc(startup->registry_outputs, capacity * sizeof *grown);
      if (grown == NULL) qa_range_error("Out of memory");
      startup->registry_outputs = grown;
      startup->registry_output_capacity = capacity;
    }
    struct registry_output_ctx *binding_ctx = xmalloc(sizeof *binding_ctx);
    binding_ctx->startup = startup;
    binding_ctx->registry = wanted[i];
    registry_output *entry = &startup->registry_outputs[startup->registry_output_count++];
    entry->registry = wanted[i];
    entry->ctx = binding_ctx;
    entry->binding = qa_cvar_registry_bind_output(wanted[i], registry_print_adapter, binding_ctx);
  }
}
/* --- q3 policy registrar over a landed registry --- */
static bool q3_register_cvar(void *ctx, const char *name, const char *default_value,
    int64_t flags, int64_t *out_integer_value) {
  qa_cvar_snapshot snapshot = {0};
  if (!qa_cvar_registry_register(ctx, name, default_value, (double)flags, &snapshot)) return false;
  *out_integer_value = (int64_t)snapshot.integer_value;
  qa_cvar_read_free(&snapshot);
  return true;
}
static void push_deferred(qa_app_prepared_startup *startup, const char *name) {
  char **grown = realloc(startup->deferred, (startup->deferred_count + 1) * sizeof *grown);
  if (grown == NULL) qa_range_error("Out of memory");
  startup->deferred = grown;
  startup->deferred[startup->deferred_count++] = xstrdup(name);
}
/* --- legacy host constructor (kept) --- */
qa_app_prepared_startup *qa_app_prepared_startup_create(qa_command_buffer *commands,
    qa_app_prepared_startup_host host) {
  qa_app_prepared_startup *startup = xcalloc(1, sizeof *startup);
  startup->commands = commands;
  startup->host = host;
  startup->run.stage = RUN_NONE;
  return startup;
}
qa_command_buffer *qa_app_prepared_startup_commands(const qa_app_prepared_startup *startup) {
  return startup->commands;
}
const qa_app_prepared_seat *qa_app_prepared_startup_seats(const qa_app_prepared_startup *startup, size_t *out_count) {
  *out_count = startup->seat_count;
  return startup->seats;
}
static void seat_state_clear(seat_state *state) {
  for (size_t i = 0; i < state->overridden_count; i++) free(state->overridden_keys[i]);
  free(state->overridden_keys);
  free(state->selected);
  free(state->authored);
  memset(state, 0, sizeof *state);
}
void qa_app_prepared_startup_close(qa_app_prepared_startup *startup) {
  if (startup == NULL || startup->closed) return;
  startup->closed = true;
  /* Unbind registry outputs before borrowed registries can go away. */
  startup->output_count = 0;
  for (size_t i = 0; i < startup->registry_output_count; i++) {
    qa_cvar_registry_unbind_output(startup->registry_outputs[i].registry, startup->registry_outputs[i].binding);
    free(startup->registry_outputs[i].ctx);
  }
  free(startup->registry_outputs);
  if (startup->binding_registration != NULL) qa_binding_commands_registration_destroy(startup->binding_registration);
  if (startup->release_view != NULL) qa_app_q1_view_commands_release(startup->release_view);
  for (size_t i = 0; i < startup->seat_count; i++) {
    if (startup->states != NULL && startup->states[i].owns_input && startup->seats[i].input != NULL)
      qa_seat_input_destroy(startup->seats[i].input);
    if (startup->states != NULL) seat_state_clear(&startup->states[i]);
  }
  free(startup->states);
  free(startup->seats);
  free(startup->seat_configs);
  free(startup->active_seat_ids);
  if (startup->console_bindings != NULL) qa_binding_store_destroy(startup->console_bindings);
  if (startup->active != NULL) qa_app_startup_config_destroy(startup->active);
  if (startup->run.stage != RUN_NONE && startup->run.stage != RUN_DONE) qa_app_startup_phases_free(&startup->run.phases);
  free(startup->continuations);
  free(startup->outputs);
  for (size_t i = 0; i < startup->forward_count; i++) { free(startup->forwards[i]->name); free(startup->forwards[i]); }
  free(startup->forwards);
  for (size_t i = 0; i < startup->deferred_count; i++) free(startup->deferred[i]);
  free(startup->deferred);
  if (startup->routing_owner != NULL) qa_app_console_routing_destroy(startup->routing_owner);
  if (startup->owns_commands && startup->commands != NULL) qa_command_buffer_destroy(startup->commands);
  if (startup->owns_fallback && startup->fallback != NULL) qa_cvar_registry_destroy(startup->fallback);
  free(startup);
}
/* --- concrete constructor --- */
static qa_cvar_registry *current_routing_owner(void *ctx, const char *name, qa_command_context source);
static qa_cvar_registry *const *current_routing_visible(void *ctx, qa_command_context source, size_t *out_count);
qa_app_prepared_startup *qa_app_prepared_startup_create_concrete(
    qa_cvar_registry *source, qa_cvar_registry *movement,
    qa_app_console_script_files *scripts,
    const qa_app_prepared_startup_options *options) {
  qa_app_prepared_startup *startup = xcalloc(1, sizeof *startup);
  startup->concrete = true;
  startup->source = source;
  startup->movement = movement;
  startup->scripts = scripts;
  startup->options = *options;
  startup->run.stage = RUN_NONE;
  startup->console_bindings = options->seat_count == 0 ? qa_binding_store_create(NULL, 0) : NULL;
  if (options->has_q3_policy) startup->q3_policy = options->q3_policy;
  else if (options->dialect == QA_COMMAND_DIALECT_Q3) {
    qa_q3_cvar_registrar registrar = {source, q3_register_cvar};
    startup->q3_policy = qa_register_q3_product_policy(&registrar);
  } else startup->q3_policy = (qa_q3_product_policy){QA_Q3_PRODUCT_RETAIL, QA_Q3_TEAM_ARENA_UI_RETAIL};
  static const char *const deferred[] = {"map", "save", "load", "weapnext", "weapprev", "use",
      "weapon", "say", "say_team", "quit", "connect", "disconnect"};
  for (size_t i = 0; i < sizeof deferred / sizeof deferred[0]; i++) push_deferred(startup, deferred[i]);
  if (options->dialect == QA_COMMAND_DIALECT_Q1_NETQUAKE || options->dialect == QA_COMMAND_DIALECT_Q1_QUAKEWORLD)
    push_deferred(startup, "pause");
  if (options->dialect == QA_COMMAND_DIALECT_Q3) {
    size_t count = 0;
    const char *const *maps = qa_q3_product_map_commands(startup->q3_policy, &count);
    for (size_t i = 0; i < count; i++) if (strcmp(maps[i], "map") != 0) push_deferred(startup, maps[i]);
  }
  startup->seat_config_count = options->seat_count;
  if (options->seat_count > 0) {
    startup->seat_configs = xmalloc(options->seat_count * sizeof *startup->seat_configs);
    memcpy(startup->seat_configs, options->seats, options->seat_count * sizeof *startup->seat_configs);
  }
  for (size_t i = 0; i < options->seat_count; i++) {
    qa_app_register_run_cvar(options->seats[i].mouse.cvars, options->movement_dialect);
    qa_app_register_player_userinfo(options->seats[i].cvars, (double)options->seats[i].id.index, NULL);
  }
  qa_app_register_gtv_cvars(source);
  if (qa_cvar_registry_dialect(movement) == options->dialect) {
    startup->fallback = movement;
  } else {
    qa_cvar_registry_options fallback_options = {0};
    fallback_options.dialect = options->dialect;
    fallback_options.context = qa_cvar_registry_context(source);
    fallback_options.print = print_text_adapter;
    fallback_options.print_ctx = startup;
    startup->fallback = qa_cvar_registry_create(&fallback_options);
    if (startup->fallback == NULL) qa_range_error("Out of memory");
    startup->owns_fallback = true;
  }
  qa_app_console_routing_options routing_options = {0};
  routing_options.ctx = startup;
  routing_options.fallback = startup->fallback;
  routing_options.source_dialect = routing_source_dialect;
  routing_options.server = routing_server;
  routing_options.seat = routing_seat;
  routing_options.input = routing_input;
  routing_options.movement = routing_movement;
  routing_options.shared = routing_shared;
  startup->routing_owner = qa_app_console_routing_create(routing_options);
  if (startup->routing_owner == NULL) qa_range_error("Out of memory");
  startup->routing = qa_app_console_routing_view(startup->routing_owner);
  qa_app_startup_phases phases = qa_app_startup_command_phases(
      options->startup_commands, options->startup_command_count, options->dialect);
  qa_command_buffer_options buffer_options = {0};
  buffer_options.dialect = options->dialect;
  buffer_options.context = qa_cvar_registry_context(source);
  buffer_options.cvars = startup->fallback;
  /* cvarRouting: { owner: (name, context) => this.routing.owner(name, context), visible: context => this.routing.visible(context) }:
   * the buffer reads the current routing, which adopt replaces (and frees the previous owner of). */
  buffer_options.cvar_routing.ctx = startup;
  buffer_options.cvar_routing.owner = current_routing_owner;
  buffer_options.cvar_routing.visible = current_routing_visible;
  buffer_options.read_script = read_script_adapter;
  buffer_options.read_script_ctx = startup;
  buffer_options.on_script_complete = script_complete_adapter;
  buffer_options.on_script_complete_ctx = startup;
  buffer_options.print = print_adapter;
  buffer_options.print_ctx = startup;
  buffer_options.startup_command_text = phases.stuffed;
  buffer_options.allow_command = allow_command_adapter;
  buffer_options.allow_command_ctx = startup;
  buffer_options.forward_to_server = forward_to_server_adapter;
  buffer_options.forward_to_server_ctx = startup;
  startup->commands = qa_command_buffer_create(&buffer_options);
  qa_app_startup_phases_free(&phases);
  if (startup->commands == NULL) qa_range_error("Out of memory");
  startup->owns_commands = true;
  startup->seat_count = options->seat_count;
  if (options->seat_count > 0) {
    startup->seats = xcalloc(options->seat_count, sizeof *startup->seats);
    startup->states = xcalloc(options->seat_count, sizeof *startup->states);
    startup->active_seat_ids = xmalloc(options->seat_count * sizeof *startup->active_seat_ids);
  }
  startup->active_seat_count = options->seat_count;
  for (size_t i = 0; i < options->seat_count; i++) {
    const qa_app_prepared_seat_configuration *config = &options->seats[i];
    qa_seat_input_options input_options = {0};
    input_options.seat = config->id;
    input_options.dialect = options->movement_dialect;
    input_options.context = config->context;
    input_options.commands = startup->commands;
    input_options.ui_event = ui_event_never;
    startup->seats[i].context = config->context;
    startup->seats[i].id = config->id;
    startup->seats[i].cvars = config->cvars;
    startup->seats[i].mouse = config->mouse;
    startup->seats[i].input = qa_seat_input_create(&input_options);
    if (startup->seats[i].input == NULL) qa_range_error("Out of memory");
    startup->states[i].owns_input = true;
    startup->active_seat_ids[i] = config->id;
  }
  for (size_t i = 0; i < startup->deferred_count; i++) register_named(startup, startup->deferred[i], 0, NULL);
  size_t operator_count = 0;
  const char *const *operators = qa_app_source_administration_command_names(options->dialect, &operator_count);
  for (size_t i = 0; i < operator_count; i++) register_named(startup, operators[i], 1, NULL);
  if (options->dialect == QA_COMMAND_DIALECT_Q1_NETQUAKE || options->dialect == QA_COMMAND_DIALECT_Q1_QUAKEWORLD) {
    register_named(startup, "status", 1, NULL);
    register_named(startup, "ping", 1, NULL);
  }
  static const char *const plain[] = {"mvdconnect", "mvdisconnect", "in_restart", "midiinfo",
      "local_join", "local_drop", "downloadstatus", "stopdownload", "retrydownload", "demopause", "snd_restart"};
  for (size_t i = 0; i < sizeof plain / sizeof plain[0]; i++) register_named(startup, plain[i], 2, NULL);
  register_named(startup, "cd", 2, &qa_app_cd_command_documentation);
  register_named(startup, "music", 2, &qa_app_music_command_documentation);
  const qa_binding_command_seat *console_seat = NULL;
  if (startup->console_bindings != NULL) {
    startup->console_seat = qa_binding_store_command_seat(startup->console_bindings);
    console_seat = &startup->console_seat;
  }
  startup->binding_registration = qa_register_binding_commands(startup->commands,
      binding_lookup, startup, print_text_adapter, startup, console_seat);
  if (startup->binding_registration == NULL) qa_range_error("Out of memory");
  if (startup->console_bindings != NULL) {
    static const char *const writeconfig_examples[] = {"writeconfig server.cfg"};
    static const qa_command_documentation writeconfig_documentation = {
        "Save the dedicated console configuration and bindings.", "writeconfig [filename]",
        writeconfig_examples, 1, NULL, 0};
    (void)qa_command_buffer_register(startup->commands, "writeconfig", writeconfig_handler, startup,
        &writeconfig_documentation, QA_COMMAND_WORLD);
  }
  startup->release_view = qa_app_register_q1_view_commands(startup->commands);
  return startup;
}
/* --- accessors and output binding --- */
qa_cvar_registry *qa_app_prepared_startup_source(const qa_app_prepared_startup *startup) { return startup->source; }
qa_cvar_registry *qa_app_prepared_startup_movement(const qa_app_prepared_startup *startup) { return startup->movement; }
qa_cvar_registry *qa_app_prepared_startup_fallback(const qa_app_prepared_startup *startup) { return startup->fallback; }
qa_app_console_script_files *qa_app_prepared_startup_scripts(const qa_app_prepared_startup *startup) { return startup->scripts; }
qa_binding_store *qa_app_prepared_startup_console_bindings(const qa_app_prepared_startup *startup) { return startup->console_bindings; }
qa_cvar_registry *qa_app_prepared_startup_shared_cvars(const qa_app_prepared_startup *startup) { return startup->options.shared; }
bool qa_app_prepared_startup_pending(const qa_app_prepared_startup *startup) { return startup_pending(startup); }
bool qa_app_prepared_startup_configuration_can_continue(const qa_app_prepared_startup *startup) { return !startup->world_action; }
int64_t qa_app_prepared_startup_bind_output(qa_app_prepared_startup *startup,
    qa_command_print_source_fn print, void *ctx) {
  if (startup->output_count == startup->output_capacity) {
    size_t capacity = startup->output_capacity == 0 ? 4 : startup->output_capacity * 2;
    output_binding *grown = realloc(startup->outputs, capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("Out of memory");
    startup->outputs = grown;
    startup->output_capacity = capacity;
  }
  int64_t handle = ++startup->next_output_handle;
  startup->outputs[startup->output_count++] = (output_binding){handle, print, ctx};
  refresh_registry_output(startup);
  return handle;
}
void qa_app_prepared_startup_unbind_output(qa_app_prepared_startup *startup, int64_t binding) {
  for (size_t i = 0; i < startup->output_count; i++) {
    if (startup->outputs[i].handle != binding) continue;
    memmove(&startup->outputs[i], &startup->outputs[i + 1],
            (startup->output_count - i - 1) * sizeof *startup->outputs);
    startup->output_count--;
    refresh_registry_output(startup);
    return;
  }
}
bool qa_app_prepared_startup_is_configuration_source(qa_app_prepared_startup *startup, qa_command_context source) {
  qa_app_startup_config *active = current_startup(startup);
  return active != NULL && qa_app_startup_config_owns_source(active, source);
}
void qa_app_prepared_startup_note_world_action(qa_app_prepared_startup *startup) {
  if (startup_pending(startup)) startup->world_action = true;
}
void qa_app_prepared_startup_forward_commands(qa_app_prepared_startup *startup,
    qa_app_prepared_forward_fn forward, void *forward_ctx) {
  startup->options.forward = forward;
  startup->options.forward_ctx = forward_ctx;
}
char *qa_app_prepared_startup_read_configuration(qa_app_prepared_startup *startup, const char *name,
    qa_command_context source, qa_app_startup_script_scope scope) {
  if (startup->scoped_read == NULL) qa_range_error("Startup script reader is missing");
  return startup->scoped_read(startup->scoped_read_ctx, name, source, scope);
}
char *qa_app_prepared_startup_read_script(qa_app_prepared_startup *startup, const char *name, qa_command_context source) {
  if (!current_context(startup, source)) return NULL;
  qa_app_startup_config *active = current_startup(startup);
  if (active != NULL) return qa_app_startup_config_read_script(active, name, source);
  if (startup->scripts == NULL) return NULL;
  return qa_app_console_script_files_read(startup->scripts, name, source);
}
void qa_app_prepared_startup_on_script_complete(qa_app_prepared_startup *startup, const qa_script_completion *event) {
  script_complete_adapter(startup, event);
}
void qa_app_prepared_startup_adopt_readers(qa_app_prepared_startup *startup,
    qa_app_console_script_files *scripts, qa_app_prepared_scoped_read_fn read, void *read_ctx) {
  startup->scripts = scripts;
  startup->scoped_read = read;
  startup->scoped_read_ctx = read_ctx;
}
void qa_app_prepared_startup_adopt_configuration_continuation(qa_app_prepared_startup *startup,
    qa_app_prepared_configuration_continuation continuation) {
  if (!qa_command_buffer_has_preparation_prefix(startup->commands))
    qa_range_error("Configuration continuation requires its published command prefix");
  if (startup->continuation_count == startup->continuation_capacity) {
    size_t capacity = startup->continuation_capacity == 0 ? 2 : startup->continuation_capacity * 2;
    qa_app_prepared_configuration_continuation *grown = realloc(startup->continuations, capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("Out of memory");
    startup->continuations = grown;
    startup->continuation_capacity = capacity;
  }
  startup->continuations[startup->continuation_count++] = continuation;
}
static bool same_session(qa_session_id left, qa_session_id right) { return left.token == right.token; }
void qa_app_prepared_startup_validate_owners(qa_app_prepared_startup *startup, const qa_app_prepared_owners *owners) {
  qa_session_id session = qa_command_buffer_context(startup->commands).session;
  qa_cvar_registry *registries[] = {owners->source, owners->movement, owners->fallback};
  bool foreign = qa_cvar_registry_dialect(owners->fallback) != qa_cvar_registry_dialect(owners->source);
  for (size_t i = 0; i < 3; i++)
    if (!same_session(qa_cvar_registry_context(registries[i]).session, session)) foreign = true;
  if (foreign) qa_range_error("Prepared profile registry belongs to another session or dialect");
  qa_command_buffer_validate_profile(startup->commands, qa_cvar_registry_dialect(owners->source), owners->fallback);
}
static qa_cvar_registry *current_routing_owner(void *ctx, const char *name, qa_command_context source) {
  qa_app_prepared_startup *startup = ctx;
  return startup->routing.owner(startup->routing.ctx, name, source);
}
static qa_cvar_registry *const *current_routing_visible(void *ctx, qa_command_context source, size_t *out_count) {
  qa_app_prepared_startup *startup = ctx;
  return startup->routing.visible(startup->routing.ctx, source, out_count);
}
static bool seat_index_is_active(const qa_app_prepared_startup *startup, size_t index) {
  return seat_is_active(startup, startup->seats[index].id);
}
static void adopt_impl(qa_app_prepared_startup *startup, qa_command_cvar_routing routing,
    qa_app_prepared_forward_fn forward, void *forward_ctx, const qa_app_prepared_owners *owners,
    qa_app_console_routing *routing_owner) {
  bool profile_changed = false;
  if (owners != NULL) {
    profile_changed = qa_command_buffer_dialect(startup->commands) != qa_cvar_registry_dialect(owners->source);
    for (size_t i = 0; i < startup->seat_count; i++)
      if (seat_index_is_active(startup, i) && startup->seats[i].input != NULL
          && qa_seat_input_dialect(startup->seats[i].input) != qa_cvar_registry_dialect(owners->movement))
        profile_changed = true;
  }
  if (profile_changed) {
    for (size_t i = 0; i < startup->seat_count; i++)
      if (seat_index_is_active(startup, i) && startup->seats[i].input != NULL
          && qa_seat_input_has_held_input(startup->seats[i].input))
        qa_range_error("Prepared profile requires released input");
  }
  if (owners != NULL) qa_app_prepared_startup_validate_owners(startup, owners);
  if (profile_changed && owners != NULL)
    qa_command_buffer_set_profile(startup->commands, qa_cvar_registry_dialect(owners->source), owners->fallback);
  startup->routing = routing;
  qa_app_console_routing *previous_owner = startup->routing_owner;
  startup->routing_owner = routing_owner;
  if (previous_owner != NULL && previous_owner != routing_owner) qa_app_console_routing_destroy(previous_owner);
  qa_app_prepared_startup_forward_commands(startup, forward, forward_ctx);
  if (owners != NULL) {
    if (startup->owns_fallback && startup->fallback != owners->fallback) {
      qa_cvar_registry_destroy(startup->fallback);
      startup->owns_fallback = false;
    }
    startup->source = owners->source;
    startup->movement = owners->movement;
    startup->fallback = owners->fallback;
    startup->options.dialect = qa_cvar_registry_dialect(owners->source);
    startup->options.movement_dialect = qa_cvar_registry_dialect(owners->movement);
    qa_app_prepared_startup_adopt_readers(startup, owners->scripts, owners->read, owners->read_ctx);
    if (profile_changed) {
      if (startup->release_view != NULL) qa_app_q1_view_commands_release(startup->release_view);
      for (size_t i = 0; i < startup->seat_count; i++)
        if (seat_index_is_active(startup, i) && startup->seats[i].input != NULL)
          qa_seat_input_set_profile(startup->seats[i].input, qa_cvar_registry_dialect(owners->movement));
      startup->release_view = qa_app_register_q1_view_commands(startup->commands);
    }
  }
  refresh_registry_output(startup);
}
void qa_app_prepared_startup_adopt(qa_app_prepared_startup *startup, qa_command_cvar_routing routing,
    qa_app_prepared_forward_fn forward, void *forward_ctx, const qa_app_prepared_owners *owners) {
  adopt_impl(startup, routing, forward, forward_ctx, owners, NULL);
}
void qa_app_prepared_startup_adopt_console_routing(qa_app_prepared_startup *startup,
    qa_app_console_routing *routing, qa_app_prepared_forward_fn forward, void *forward_ctx,
    const qa_app_prepared_owners *owners) {
  adopt_impl(startup, qa_app_console_routing_view(routing), forward, forward_ctx, owners, routing);
}
void qa_app_prepared_startup_set_active_seats(qa_app_prepared_startup *startup,
    const qa_seat_id *ids, size_t count) {
  for (size_t i = 0; i < count; i++)
    if (find_seat(startup, ids[i], NULL) == NULL)
      qa_range_error("Active seat is not retained by this client");
  qa_seat_id *copy = NULL;
  if (count > 0) {
    copy = xmalloc(count * sizeof *copy);
    memcpy(copy, ids, count * sizeof *copy);
  }
  free(startup->active_seat_ids);
  startup->active_seat_ids = copy;
  startup->active_seat_count = count;
  refresh_registry_output(startup);
}
void qa_app_prepared_startup_publish_seats(qa_app_prepared_startup *startup,
    const qa_app_prepared_seat *seats, size_t seat_count,
    const qa_seat_id *active, size_t active_count, bool has_active) {
  qa_seat_id *active_ids = NULL;
  size_t wanted = has_active ? active_count : seat_count;
  if (wanted > 0) {
    active_ids = xmalloc(wanted * sizeof *active_ids);
    for (size_t i = 0; i < wanted; i++) active_ids[i] = has_active ? active[i] : seats[i].id;
  }
  for (size_t i = 0; i < wanted; i++) {
    bool retained = false;
    for (size_t j = 0; j < seat_count; j++) if (qa_same_seat(seats[j].id, active_ids[i])) retained = true;
    if (!retained) { free(active_ids); qa_range_error("Active seat is not retained by this client"); }
  }
  qa_app_prepared_seat *published = seat_count > 0 ? xcalloc(seat_count, sizeof *published) : NULL;
  seat_state *states = seat_count > 0 ? xcalloc(seat_count, sizeof *states) : NULL;
  bool *claimed = startup->seat_count > 0 ? xcalloc(startup->seat_count, sizeof *claimed) : NULL;
  for (size_t i = 0; i < seat_count; i++) {
    published[i] = seats[i];
    size_t previous = 0;
    bool found = false;
    for (size_t j = 0; j < startup->seat_count; j++) {
      if (!claimed[j] && qa_same_seat(startup->seats[j].id, seats[i].id) && startup->seats[j].input == seats[i].input) {
        previous = j; found = true; break;
      }
    }
    if (found) {
      claimed[previous] = true;
      published[i].all_bindings_chosen = startup->seats[previous].all_bindings_chosen;
      published[i].collecting_bindings = startup->seats[previous].collecting_bindings;
      states[i] = startup->states[previous];
      memset(&startup->states[previous], 0, sizeof startup->states[previous]);
    } else {
      published[i].all_bindings_chosen = true;
      if (startup->seat_count > 0 && startup->states[0].has_authored) {
        states[i].has_authored = true;
        states[i].authored_count = startup->states[0].authored_count;
        states[i].authored = xmalloc((states[i].authored_count + 1) * sizeof *states[i].authored);
        memcpy(states[i].authored, startup->states[0].authored, states[i].authored_count * sizeof *states[i].authored);
      }
    }
  }
  for (size_t i = 0; i < startup->seat_count; i++) {
    if (claimed != NULL && claimed[i]) continue;
    if (startup->states[i].owns_input && startup->seats[i].input != NULL) qa_seat_input_destroy(startup->seats[i].input);
    seat_state_clear(&startup->states[i]);
  }
  free(claimed);
  free(startup->states);
  free(startup->seats);
  startup->seats = published;
  startup->states = states;
  startup->seat_count = seat_count;
  qa_app_prepared_startup_set_active_seats(startup, active_ids, wanted);
  free(active_ids);
}
void qa_app_prepared_startup_adopt_seat(qa_app_prepared_startup *startup, qa_seat_id id,
    qa_cvar_registry *cvars, const qa_mouse_settings *mouse) {
  qa_app_prepared_seat *seat = find_seat(startup, id, NULL);
  if (seat != NULL) {
    if (cvars != NULL) seat->cvars = cvars;
    if (mouse != NULL) seat->mouse = *mouse;
  }
  refresh_registry_output(startup);
}
/* --- binding choices (TS Map keyed by physicalInputKey) --- */
typedef struct binding_entry { char *key; qa_input_binding binding; } binding_entry;
typedef struct binding_map { binding_entry *entries; size_t count, capacity; } binding_map;
static bool map_has(const binding_map *map, const char *key) {
  for (size_t i = 0; i < map->count; i++) if (strcmp(map->entries[i].key, key) == 0) return true;
  return false;
}
static void map_set(binding_map *map, qa_input_binding binding) {
  char *key = qa_physical_input_key(&binding.input);
  if (key == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < map->count; i++) {
    if (strcmp(map->entries[i].key, key) == 0) {
      map->entries[i].binding = binding;
      free(key);
      return;
    }
  }
  if (map->count == map->capacity) {
    size_t capacity = map->capacity == 0 ? 8 : map->capacity * 2;
    binding_entry *grown = realloc(map->entries, capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("Out of memory");
    map->entries = grown;
    map->capacity = capacity;
  }
  map->entries[map->count++] = (binding_entry){key, binding};
}
static void map_delete(binding_map *map, const char *key) {
  for (size_t i = 0; i < map->count; i++) {
    if (strcmp(map->entries[i].key, key) != 0) continue;
    free(map->entries[i].key);
    memmove(&map->entries[i], &map->entries[i + 1], (map->count - i - 1) * sizeof *map->entries);
    map->count--;
    return;
  }
}
static qa_input_binding *map_values(binding_map *map, size_t *out_count) {
  *out_count = map->count;
  qa_input_binding *values = xmalloc((map->count + 1) * sizeof *values);
  for (size_t i = 0; i < map->count; i++) values[i] = map->entries[i].binding;
  return values;
}
static void map_free(binding_map *map) {
  for (size_t i = 0; i < map->count; i++) free(map->entries[i].key);
  free(map->entries);
  memset(map, 0, sizeof *map);
}
static void map_add_all(binding_map *map, const qa_input_binding *bindings, size_t count) {
  for (size_t i = 0; i < count; i++) map_set(map, bindings[i]);
}
static bool overridden_has(const seat_state *state, const char *key) {
  for (size_t i = 0; i < state->overridden_count; i++)
    if (strcmp(state->overridden_keys[i], key) == 0) return true;
  return false;
}
static void overridden_add(seat_state *state, const char *key) {
  if (overridden_has(state, key)) return;
  if (state->overridden_count == state->overridden_capacity) {
    size_t capacity = state->overridden_capacity == 0 ? 8 : state->overridden_capacity * 2;
    char **grown = realloc(state->overridden_keys, capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("Out of memory");
    state->overridden_keys = grown;
    state->overridden_capacity = capacity;
  }
  state->overridden_keys[state->overridden_count++] = xstrdup(key);
}
/* TS /^(?:weapon|impulse|use)\s/i on a command target text. */
static bool weapon_command_text(const qa_input_binding *binding) {
  if (binding->target.is_action || binding->target.text == NULL) return false;
  static const char *const words[] = {"weapon", "impulse", "use"};
  for (size_t i = 0; i < 3; i++) {
    size_t length = strlen(words[i]);
    bool match = true;
    for (size_t j = 0; j < length && match; j++) /* stops at the terminator of a shorter text */
      if (tolower((unsigned char)binding->target.text[j]) != words[i][j]) match = false;
    /* TS /^(?:weapon|impulse|use)\s/i: \s is JS white space. */
    if (match && qa_js_white_space_length(binding->target.text + length) != 0) return true;
  }
  return false;
}
static qa_input_binding *copy_bindings(const qa_input_binding *bindings, size_t count) {
  qa_input_binding *copy = xmalloc((count + 1) * sizeof *copy);
  if (count > 0) memcpy(copy, bindings, count * sizeof *copy);
  return copy;
}
qa_input_binding *qa_app_prepared_startup_preview_bindings(qa_app_prepared_startup *startup,
    qa_seat_id id, const qa_input_binding *defaults, size_t default_count, size_t *out_count) {
  size_t index = 0;
  qa_app_prepared_seat *seat = find_seat(startup, id, &index);
  if (seat == NULL) {
    *out_count = default_count;
    return copy_bindings(defaults, default_count);
  }
  binding_map map = {0};
  size_t current_count = 0;
  const qa_input_binding *current = qa_seat_input_bindings(seat->input, &current_count);
  map_add_all(&map, current, current_count);
  if (!seat->all_bindings_chosen) {
    for (size_t i = 0; i < default_count; i++) {
      char *key = qa_physical_input_key(&defaults[i].input);
      if (key == NULL) qa_range_error("Out of memory");
      if (!overridden_has(&startup->states[index], key)) map_set(&map, defaults[i]);
      free(key);
    }
  }
  qa_input_binding *values = map_values(&map, out_count);
  map_free(&map);
  return values;
}
void qa_app_prepared_startup_adopt_binding_defaults(qa_app_prepared_startup *startup,
    qa_seat_id id, const qa_input_binding *defaults, size_t default_count) {
  size_t index = 0;
  if (find_seat(startup, id, &index) == NULL) return;
  free(startup->states[index].selected);
  startup->states[index].selected = copy_bindings(defaults, default_count);
  startup->states[index].selected_count = default_count;
  startup->states[index].has_selected = true;
}
qa_input_binding *qa_app_prepared_startup_bindings(qa_app_prepared_startup *startup,
    qa_seat_id id, const qa_input_binding *defaults, size_t default_count, size_t *out_count) {
  qa_input_binding *preview = qa_app_prepared_startup_preview_bindings(startup, id, defaults, default_count, out_count);
  /* The preview borrows strings from the seat's stored bindings, which binding the seat replaces and frees: the
   * result is a deep copy (TS returns binding objects that stay alive by reference). */
  qa_input_binding *bindings = xmalloc((*out_count + 1) * sizeof *bindings);
  for (size_t i = 0; i < *out_count; i++)
    if (!qa_input_binding_copy(&bindings[i], &preview[i])) qa_range_error("Out of memory");
  free(preview);
  qa_app_prepared_startup_adopt_binding_defaults(startup, id, defaults, default_count);
  qa_app_prepared_seat *seat = find_seat(startup, id, NULL);
  if (seat != NULL) {
    for (size_t i = 0; i < *out_count; i++)
      if (!qa_seat_input_bind(seat->input, &bindings[i])) qa_range_error("Out of memory");
  }
  return bindings;
}
void qa_app_prepared_startup_reset_bindings(qa_app_prepared_startup *startup, qa_seat_id id) {
  size_t index = 0;
  qa_app_prepared_seat *seat = find_seat(startup, id, &index);
  if (seat == NULL || !startup->states[index].has_authored)
    qa_range_error("Authored binding defaults are not ready");
  seat_state *state = &startup->states[index];
  binding_map defaults = {0};
  map_add_all(&defaults, state->authored, state->authored_count);
  if (state->has_selected) {
    for (size_t i = 0; i < defaults.count;) {
      if (weapon_command_text(&defaults.entries[i].binding)) map_delete(&defaults, defaults.entries[i].key);
      else i++;
    }
    for (size_t i = 0; i < state->selected_count; i++) {
      char *key = qa_physical_input_key(&state->selected[i].input);
      if (key == NULL) qa_range_error("Out of memory");
      if (!map_has(&defaults, key)) map_set(&defaults, state->selected[i]);
      free(key);
    }
  }
  qa_seat_input_unbind_all(seat->input);
  for (size_t i = 0; i < defaults.count; i++)
    if (!qa_seat_input_bind(seat->input, &defaults.entries[i].binding)) qa_range_error("Out of memory");
  map_free(&defaults);
  seat->all_bindings_chosen = true;
  for (size_t i = 0; i < state->overridden_count; i++) free(state->overridden_keys[i]);
  state->overridden_count = 0;
}
bool qa_app_prepared_startup_seat_choices(const qa_app_prepared_startup *startup, qa_seat_id id,
    bool *all_bindings_chosen, char *const **overridden_keys, size_t *overridden_count,
    bool *has_selected, const qa_input_binding **selected, size_t *selected_count,
    const qa_input_binding **authored, size_t *authored_count) {
  for (size_t i = 0; i < startup->seat_count; i++) {
    if (!qa_same_seat(startup->seats[i].id, id)) continue;
    const seat_state *state = &startup->states[i];
    if (all_bindings_chosen != NULL) *all_bindings_chosen = startup->seats[i].all_bindings_chosen;
    if (overridden_keys != NULL) *overridden_keys = state->overridden_keys;
    if (overridden_count != NULL) *overridden_count = state->overridden_count;
    if (has_selected != NULL) *has_selected = state->has_selected;
    if (selected != NULL) *selected = state->selected;
    if (selected_count != NULL) *selected_count = state->selected_count;
    if (authored != NULL) *authored = state->has_authored ? state->authored : NULL;
    if (authored_count != NULL) *authored_count = state->has_authored ? state->authored_count : 0;
    return true;
  }
  return false;
}
bool qa_app_prepared_startup_clear_overridden_keys(qa_app_prepared_startup *startup, qa_seat_id id) {
  size_t index = 0;
  if (find_seat(startup, id, &index) == NULL) return false;
  seat_state *state = &startup->states[index];
  for (size_t i = 0; i < state->overridden_count; i++) free(state->overridden_keys[i]);
  state->overridden_count = 0;
  return true;
}
bool qa_app_prepared_startup_publish_seat_choices(qa_app_prepared_startup *startup, qa_seat_id id,
    bool all_bindings_chosen, bool has_selected, const qa_input_binding *selected, size_t selected_count,
    bool has_authored, const qa_input_binding *authored, size_t authored_count,
    const char *const *overridden_keys, size_t overridden_count) {
  size_t index = 0;
  qa_app_prepared_seat *seat = find_seat(startup, id, &index);
  if (seat == NULL) return false;
  seat_state *state = &startup->states[index];
  seat->all_bindings_chosen = all_bindings_chosen;
  free(state->selected);
  state->has_selected = has_selected;
  state->selected = has_selected ? copy_bindings(selected, selected_count) : NULL;
  state->selected_count = has_selected ? selected_count : 0;
  free(state->authored);
  state->has_authored = has_authored;
  state->authored = has_authored ? copy_bindings(authored, authored_count) : NULL;
  state->authored_count = has_authored ? authored_count : 0;
  for (size_t i = 0; i < overridden_count; i++) overridden_add(state, overridden_keys[i]);
  return true;
}
/* --- run()/execute()/advanceFrame() --- */
static void apply_startup_variables(qa_app_prepared_startup *startup) {
  for (size_t i = 0; i < startup->run.phases.variable_count; i++) {
    qa_app_startup_variable *variable = &startup->run.phases.variables[i];
    qa_cvar_registry *owner = startup->routing.owner(startup->routing.ctx, variable->name,
        startup->run.command_context);
    set_cvar(owner, variable->name, variable->value, true);
    qa_cvar_snapshot snapshot = {0};
    if (!qa_cvar_registry_register(owner, variable->name, "", 0, &snapshot))
      qa_range_error("Q3 startup variable registration rejected: %s", variable->name);
    qa_cvar_registry_add_flags(owner, snapshot.name, QA_CVAR_FLAG_USER_CREATED);
    qa_cvar_read_free(&snapshot);
  }
}
static char *scoped_read_adapter(void *ctx, const char *name, qa_command_context source,
    qa_app_startup_script_scope scope) {
  return qa_app_prepared_startup_read_configuration(ctx, name, source, scope);
}
static qa_input_binding *selected_or_defaults(qa_app_prepared_startup *startup, size_t seat_index,
    size_t *out_count, qa_input_binding **owned_defaults) {
  *owned_defaults = NULL;
  seat_state *state = &startup->states[seat_index];
  if (state->has_selected) {
    *out_count = state->selected_count;
    return state->selected;
  }
  /* TS defaultBindings(0, movementDialect); strings stay alive with
   * authored copies, as the TS shares binding objects. */
  *owned_defaults = qa_default_bindings(0, startup->options.movement_dialect, NULL, 0, out_count);
  if (*owned_defaults == NULL) qa_range_error("Out of memory");
  return *owned_defaults;
}
static void apply_selected_defaults(void *ctx) {
  apply_ctx *apply = ctx;
  qa_app_prepared_startup *startup = apply->startup;
  if (startup->seat_count == 0) return;
  qa_app_prepared_seat *seat = &startup->seats[apply->index];
  seat_state *state = &startup->states[apply->index];
  size_t current_count = 0;
  const qa_input_binding *current = qa_seat_input_bindings(seat->input, &current_count);
  qa_input_binding *snapshot = copy_bindings(current, current_count);
  for (size_t i = 0; i < current_count; i++)
    if (weapon_command_text(&snapshot[i])) qa_seat_input_unbind(seat->input, &snapshot[i].input);
  free(snapshot);
  binding_map authored = {0};
  current = qa_seat_input_bindings(seat->input, &current_count);
  map_add_all(&authored, current, current_count);
  size_t default_count = 0;
  qa_input_binding *owned = NULL;
  qa_input_binding *defaults = selected_or_defaults(startup, apply->index, &default_count, &owned);
  for (size_t i = 0; i < default_count; i++) {
    if (!qa_seat_input_bind(seat->input, &defaults[i])) qa_range_error("Out of memory");
    char *key = qa_physical_input_key(&defaults[i].input);
    if (key == NULL) qa_range_error("Out of memory");
    if (!map_has(&authored, key)) map_set(&authored, defaults[i]);
    free(key);
  }
  free(state->authored);
  state->authored = map_values(&authored, &state->authored_count);
  state->has_authored = true;
  map_free(&authored);
  free(owned);
  seat->collecting_bindings = true;
}
static void apply_archive(void *ctx) {
  apply_ctx *apply = ctx;
  qa_app_prepared_startup *startup = apply->startup;
  qa_app_prepared_execute_options *options = &startup->run.options;
  size_t index = apply->index;
  if (index == 0) {
    qa_cvar_registry_apply_archive(startup->source, options->source_archive, options->source_archive_count);
    qa_cvar_registry_apply_archive(startup->movement, options->movement_archive, options->movement_archive_count);
    qa_cvar_registry_apply_archive(startup->fallback, options->fallback_archive, options->fallback_archive_count);
    if (startup->options.shared != NULL)
      qa_cvar_registry_apply_archive(startup->options.shared, options->shared_archive, options->shared_archive_count);
  }
  if (startup->seat_count == 0) return;
  qa_app_prepared_seat *seat = &startup->seats[index];
  const qa_app_prepared_seat_configuration *saved = &startup->seat_configs[index];
  qa_cvar_registry_apply_archive(seat->cvars, saved->archive, saved->archive_count);
  qa_cvar_registry_apply_archive(seat->mouse.cvars, saved->mouse_archive, saved->mouse_archive_count);
  if (saved->profile != NULL) {
    qa_seat_input_unbind_all(seat->input);
    for (size_t i = 0; i < saved->profile->binding_count; i++)
      if (!qa_seat_input_bind(seat->input, &saved->profile->bindings[i])) qa_range_error("Out of memory");
    seat->all_bindings_chosen = true;
    qa_mouse_settings_write(&seat->mouse, saved->profile->mouse);
    if (saved->profile->has_always_run)
      qa_cvar_registry_set_command_flags(seat->mouse.cvars, "cl_run",
          saved->profile->always_run ? "1" : "0", QA_CVAR_COMMAND_FLAG_ARCHIVE);
  }
  seat->collecting_bindings = true;
}
static void apply_launch_noop(void *ctx) { (void)ctx; }
static void replay_startup_variables(void *ctx) {
  apply_ctx *apply = ctx;
  if (apply->index != 0) return;
  qa_app_prepared_startup *startup = apply->startup;
  apply_startup_variables(startup);
  qa_command_buffer_insert(startup->commands, startup->run.phases.early, &startup->run.command_context, -1);
}
static void collect_bindings(void *ctx) {
  apply_ctx *apply = ctx;
  qa_app_prepared_startup *startup = apply->startup;
  if (startup->seat_count == 0) return;
  qa_app_prepared_seat *seat = &startup->seats[apply->index];
  if (!seat->collecting_bindings) return;
  size_t count = 0;
  char *const *tokens = qa_command_buffer_tokenized_arguments(startup->commands, &count);
  /* TS noteBindingCommand: the command name is ASCII-folded. */
  char *name = qa_ascii_fold(count > 0 ? tokens[0] : "");
  if (name == NULL) qa_range_error("Out of memory");
  const char *key = count > 1 ? tokens[1] : NULL;
  bool unbind_all = strcmp(name, "unbindall") == 0;
  bool binding = strcmp(name, "bind") == 0 || strcmp(name, "unbind") == 0;
  free(name);
  if (unbind_all) seat->all_bindings_chosen = true;
  if (key != NULL && binding) {
    qa_physical_input input = {0};
    if (qa_named_physical_input(key, 0, &input)) {
      char *physical_key = qa_physical_input_key(&input);
      if (physical_key == NULL) qa_range_error("Out of memory");
      overridden_add(&startup->states[apply->index], physical_key);
      free(physical_key);
    }
  }
}
static bool run_should_continue(void *ctx) {
  return !((qa_app_prepared_startup *)ctx)->world_action;
}
static bool run_step(qa_app_prepared_startup *startup) {
  run_state *run = &startup->run;
  if (!run->started) {
    run->started = true;
    run->phases = qa_app_startup_command_phases(startup->options.startup_commands,
        startup->options.startup_command_count, startup->options.dialect);
    run->command_context = startup->seat_count > 0 ? startup->seats[0].context
        : qa_cvar_registry_context(startup->source);
    apply_startup_variables(startup);
    qa_command_buffer_append(startup->commands, run->phases.early, &run->command_context, -1);
    (void)qa_command_buffer_execute_scripts_async(startup->commands, NULL, NULL, NULL, NULL);
    if (startup->options.dialect == QA_COMMAND_DIALECT_Q1_QUAKEWORLD && startup->seat_count == 0)
      qa_command_buffer_append(startup->commands, run->phases.stuffed, &run->command_context, -1);
    run->stage = RUN_SEATS;
  }
  if (run->stage == RUN_SEATS) {
    size_t configurations = startup->seat_count == 0 ? 1 : startup->seat_count;
    if (run->index >= configurations) {
      run->stage = RUN_LATE;
    } else {
      size_t index = run->index;
      if (!run->seat_started) {
        run->seat_started = true;
        if (startup->seat_count > 0 && index >= startup->seat_config_count)
          qa_range_error("Startup seat configuration is missing");
        if (index != 0 && startup->seat_count > 0) {
          binding_map defaults = {0};
          if (startup->states[0].has_authored)
            map_add_all(&defaults, startup->states[0].authored, startup->states[0].authored_count);
          size_t default_count = 0;
          qa_input_binding *owned = NULL;
          qa_input_binding *selected = selected_or_defaults(startup, index, &default_count, &owned);
          for (size_t i = 0; i < default_count; i++) {
            char *key = qa_physical_input_key(&selected[i].input);
            if (key == NULL) qa_range_error("Out of memory");
            if (!map_has(&defaults, key)) map_set(&defaults, selected[i]);
            free(key);
          }
          free(startup->states[index].authored);
          startup->states[index].authored = map_values(&defaults, &startup->states[index].authored_count);
          startup->states[index].has_authored = true;
          map_free(&defaults);
          free(owned);
        }
        startup->apply = (apply_ctx){startup, index};
        qa_app_startup_config_options config = {0};
        config.dialect = startup->options.dialect;
        config.context = startup->seat_count > 0 ? startup->seats[index].context
            : qa_cvar_registry_context(startup->source);
        config.has_mod = run->options.has_mod;
        config.scope = index == 0 ? "source" : "seat";
        config.safe_mode = run->phases.safe;
        config.read = scoped_read_adapter;
        config.read_ctx = startup;
        config.apply_ctx = &startup->apply;
        config.apply_selected_defaults = apply_selected_defaults;
        config.apply_archive = apply_archive;
        config.apply_launch_options = apply_launch_noop;
        config.replay_startup_variables = replay_startup_variables;
        startup->active = qa_app_startup_config_create(&config);
        if (startup->active == NULL) qa_range_error("Out of memory");
        if (startup->seat_count > 0) startup->seats[index].collecting_bindings = index != 0;
      }
      qa_app_startup_config_append plain = { NULL, NULL };
      bool done = qa_app_startup_config_execute_frame(startup->active, startup->commands, plain,
          collect_bindings, &startup->apply, run_should_continue, startup);
      if (!done) return false;
      qa_app_startup_config_destroy(startup->active);
      startup->active = NULL;
      run->seat_started = false;
      run->index++;
      return false;
    }
  }
  if (run->stage == RUN_LATE) {
    qa_command_dialect dialect = startup->options.dialect;
    if (dialect != QA_COMMAND_DIALECT_Q1_NETQUAKE && dialect != QA_COMMAND_DIALECT_Q1_QUAKEWORLD) {
      if (!run->late_appended) {
        run->late_appended = true;
        qa_command_buffer_append(startup->commands, run->phases.late, &run->command_context, -1);
      }
      (void)qa_command_buffer_execute_scripts_async(startup->commands, NULL, NULL,
          run_should_continue, startup);
      if (startup->world_action || qa_command_buffer_has_pending_commands(startup->commands))
        return false; /* TS yields; caller resumes via advanceFrame */
    }
    if (run->options.apply_launch_options != NULL)
      run->options.apply_launch_options(run->options.apply_ctx);
    qa_app_startup_phases_free(&run->phases);
    memset(run, 0, sizeof *run);
    run->stage = RUN_NONE;
    return true;
  }
  return true;
}
bool qa_app_prepared_startup_advance_frame(qa_app_prepared_startup *startup) {
  if (startup->continuation_count > 0) {
    qa_app_prepared_configuration_continuation profile = startup->continuations[startup->continuation_count - 1];
    startup->world_action = false;
    if (profile.advance(profile.ctx, startup)) {
      qa_command_buffer_finish_preparation(startup->commands);
      startup->continuation_count--;
    }
    return true;
  }
  if (startup->run.stage == RUN_NONE) return false;
  startup->world_action = false;
  (void)run_step(startup);
  return true;
}
void qa_app_prepared_startup_execute(qa_app_prepared_startup *startup,
    const qa_app_prepared_execute_options *options) {
  startup->scoped_read = options->read;
  startup->scoped_read_ctx = options->read_ctx;
  memset(&startup->run, 0, sizeof startup->run);
  startup->run.stage = RUN_SEATS;
  startup->run.options = *options;
  (void)qa_app_prepared_startup_advance_frame(startup);
  while (startup_pending(startup) && !startup->world_action) {
    if (options->next_frame != NULL) options->next_frame(options->next_frame_ctx);
    (void)qa_app_prepared_startup_advance_frame(startup);
  }
  if (startup_pending(startup) && options->apply_launch_options != NULL)
    options->apply_launch_options(options->apply_ctx);
}
/* --- allowSeatConfigurationCommand (full TS form) --- */
static bool is_set_name(const char *name) {
  static const char *const names[] = {"set", "seta", "sets", "setu", "toggle", "reset"};
  for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
    if (strcmp(name, names[i]) == 0) return true;
  return false;
}
bool qa_app_allow_seat_configuration_command_for(qa_command_invocation *command,
    qa_command_cvar_routing routing, const qa_app_prepared_seat *seats, size_t seat_count,
    qa_command_print_source_fn print, void *print_ctx) {
  const qa_command_origin *script = &command->source.origin;
  bool saved_configuration = false;
  while (script->kind == QA_COMMAND_ORIGIN_SCRIPT) {
    if (script->name != NULL && (strcmp(script->name, "config.cfg") == 0 || strcmp(script->name, "q3config.cfg") == 0))
      saved_configuration = true;
    if (script->caller == NULL) break;
    script = script->caller;
  }
  if (!saved_configuration) return true;
  if (command->argc == 0) return true;
  char *name = qa_ascii_fold(command->argv[0]);
  if (strcmp(name, "cvar_restart") == 0) {
    if (print != NULL) print(print_ctx, "Ignoring shared cvar restart in saved secondary-seat configuration.\n", &command->source);
    free(name);
    return false;
  }
  bool setter = is_set_name(name);
  const char *target = setter ? (command->argc > 1 ? command->argv[1] : NULL) : name;
  if (target == NULL) { free(name); return true; }
  qa_cvar_registry *owner = routing.owner(routing.ctx, target, command->source);
  if (!setter) {
    qa_cvar_read found = {0};
    bool exists = qa_cvar_registry_find(owner, target, &found);
    if (exists) qa_cvar_read_free(&found);
    if (!exists) { free(name); return true; }
  }
  const qa_command_origin *origin = root_origin(&command->source);
  bool allowed = false;
  if (origin->kind == QA_COMMAND_ORIGIN_LOCAL_SEAT) {
    for (size_t i = 0; i < seat_count; i++)
      if (qa_same_seat(seats[i].id, origin->seat) && (owner == seats[i].cvars || owner == seats[i].mouse.cvars))
        allowed = true;
  }
  if (allowed) { free(name); return true; }
  if (print != NULL) {
    char message[1024];
    (void)snprintf(message, sizeof message,
        "Ignoring shared cvar %s in saved secondary-seat configuration; use autoexec.cfg for intentional shared overrides.\n", target);
    print(print_ctx, message, &command->source);
  }
  free(name);
  return false;
}
/* --- PreparedClientCommands --- */
typedef struct staged_seat {
  qa_seat_id id;
  qa_seat_input *input; /* borrowed original */
  qa_binding_store *staged;
  qa_binding_command_seat seat_view;
  qa_input_binding *original; size_t original_count;
  bool cleared;
} staged_seat;
struct qa_app_prepared_client_commands {
  qa_prepared_program *program;
  qa_command_dialect release_dialect;
  qa_binding_store *console_bindings; /* borrowed original */
  qa_binding_store *staged_console;
  qa_input_binding *original_console; size_t original_console_count;
  bool has_original_console;
  staged_seat *seats; size_t seat_count;
  qa_binding_commands_registration *binding_registration;
  void *print_state; /* the registration's print ctx (TS closure), freed with the client */
  qa_app_q1_view_commands *view_registration;
};
static staged_seat *staged_find(qa_app_prepared_client_commands *client, qa_seat_id id) {
  for (size_t i = 0; i < client->seat_count; i++)
    if (qa_same_seat(client->seats[i].id, id)) return &client->seats[i];
  return NULL;
}
static bool staged_lookup(void *ctx, qa_seat_id id, qa_binding_command_seat *out) {
  staged_seat *seat = staged_find(ctx, id);
  if (seat == NULL) return false;
  *out = seat->seat_view;
  return true;
}
typedef struct staged_print {
  qa_app_prepared_client_commands *client;
  qa_command_print_source_fn print; void *print_ctx;
} staged_print;
static void staged_print_text(void *ctx, const char *text) {
  staged_print *staged = ctx;
  if (staged->print != NULL)
    staged->print(staged->print_ctx, text, qa_command_buffer_execution_context(staged->client->program->commands));
}
static bool bindings_equal_identity(const qa_input_binding *left, size_t left_count,
    const qa_input_binding *right, size_t right_count) {
  /* TS compares binding object identity; C bindings are values in
   * seat-owned arrays, so pointer identity of the array slots is the
   * stored snapshot copy compared field-wise by address of target
   * strings (deep copies share no storage after a rebind). */
  if (left_count != right_count) return false;
  for (size_t i = 0; i < left_count; i++) {
    if (left[i].target.is_action != right[i].target.is_action) return false;
    if (left[i].target.action != right[i].target.action) return false;
    if (left[i].target.text != right[i].target.text) return false;
    char *left_key = qa_physical_input_key(&left[i].input);
    char *right_key = qa_physical_input_key(&right[i].input);
    bool same = left_key != NULL && right_key != NULL && strcmp(left_key, right_key) == 0;
    free(left_key);
    free(right_key);
    if (!same) return false;
  }
  return true;
}
qa_app_prepared_client_commands *qa_app_prepare_client_commands(qa_command_buffer *commands,
    const qa_app_prepared_seat *inputs, size_t input_count,
    qa_cvar_registry *const *live_registries, size_t live_registry_count,
    const qa_command_buffer_options *options, qa_binding_store *console_bindings) {
  /* candidate registries: options.cvars + routing.visible of contexts */
  qa_cvar_registry *candidates[64];
  size_t candidate_count = 0;
  if (options->cvars != NULL) candidates[candidate_count++] = options->cvars;
  if (options->cvar_routing.visible != NULL) {
    for (size_t c = 0; c < input_count + 1; c++) {
      qa_command_context context = c == 0 ? options->context : inputs[c - 1].context;
      size_t visible_count = 0;
      qa_cvar_registry *const *visible = options->cvar_routing.visible(options->cvar_routing.ctx, context, &visible_count);
      for (size_t i = 0; i < visible_count && candidate_count < 64; i++) candidates[candidate_count++] = visible[i];
    }
  }
  for (size_t i = 0; i < candidate_count; i++)
    for (size_t j = 0; j < live_registry_count; j++)
      if (candidates[i] == live_registries[j])
        qa_range_error("Candidate client commands require isolated cvar owners");
  qa_app_prepared_client_commands *client = xcalloc(1, sizeof *client);
  client->program = qa_command_buffer_prepare_program(commands, options);
  if (client->program == NULL) qa_range_error("Out of memory");
  client->release_dialect = qa_command_buffer_dialect(commands);
  client->console_bindings = console_bindings;
  if (console_bindings != NULL) {
    size_t count = 0;
    const qa_input_binding *original = qa_binding_store_bindings(console_bindings, &count);
    client->has_original_console = true;
    client->original_console_count = count;
    client->original_console = copy_bindings(original, count);
    client->staged_console = qa_binding_store_create(original, count);
    if (client->staged_console == NULL) qa_range_error("Out of memory");
  }
  client->seat_count = input_count;
  if (input_count > 0) client->seats = xcalloc(input_count, sizeof *client->seats);
  for (size_t i = 0; i < input_count; i++) {
    staged_seat *seat = &client->seats[i];
    seat->id = inputs[i].id;
    seat->input = inputs[i].input;
    size_t count = 0;
    const qa_input_binding *original = qa_seat_input_bindings(seat->input, &count);
    seat->original_count = count;
    seat->original = copy_bindings(original, count);
    seat->staged = qa_binding_store_create(original, count);
    if (seat->staged == NULL) qa_range_error("Out of memory");
    seat->seat_view = qa_binding_store_command_seat(seat->staged);
  }
  staged_print *print_state = xmalloc(sizeof *print_state);
  print_state->client = client;
  print_state->print = options->print;
  print_state->print_ctx = options->print_ctx;
  qa_binding_command_seat staged_console_view = {0};
  const qa_binding_command_seat *console_view = NULL;
  if (client->staged_console != NULL) {
    staged_console_view = qa_binding_store_command_seat(client->staged_console);
    console_view = &staged_console_view;
  }
  client->binding_registration = qa_register_binding_commands(client->program->commands,
      staged_lookup, client, staged_print_text, print_state, console_view);
  client->print_state = print_state;
  if (client->binding_registration == NULL) qa_range_error("Out of memory");
  client->view_registration = qa_app_register_q1_view_commands(client->program->commands);
  return client;
}
qa_command_buffer *qa_app_prepared_client_commands_buffer(qa_app_prepared_client_commands *client) {
  return client->program->commands;
}
bool qa_app_prepared_client_commands_prepare_prefix(qa_app_prepared_client_commands *client,
    qa_prepare_prefix_run_fn run, void *run_ctx) {
  return client->program->prepare_prefix(client->program->ctx, run, run_ctx);
}
void qa_app_prepared_client_commands_release_append(qa_app_prepared_client_commands *client,
    const char *text, const qa_command_context *source) {
  qa_command_buffer_append(client->program->commands, text, source, (int)client->release_dialect);
}
bool qa_app_prepared_client_commands_input(qa_app_prepared_client_commands *client, qa_seat_id seat,
    qa_binding_command_seat *out) {
  return staged_lookup(client, seat, out);
}
bool qa_app_prepared_client_commands_input_is_down(qa_app_prepared_client_commands *client,
    qa_seat_id seat, const qa_physical_input *input) {
  staged_seat *staged = staged_find(client, seat);
  if (staged == NULL || staged->cleared) return false;
  return qa_seat_input_is_down(staged->input, input);
}
void qa_app_prepared_client_commands_input_clear_states(qa_app_prepared_client_commands *client, qa_seat_id seat) {
  staged_seat *staged = staged_find(client, seat);
  if (staged != NULL) staged->cleared = true;
}
void qa_app_prepared_client_commands_validate_publication(qa_app_prepared_client_commands *client) {
  client->program->validate_publication(client->program->ctx);
  if (client->console_bindings != NULL && client->has_original_console) {
    size_t count = 0;
    const qa_input_binding *current = qa_binding_store_bindings(client->console_bindings, &count);
    if (!bindings_equal_identity(current, count, client->original_console, client->original_console_count))
      qa_range_error("Console bindings changed during preparation");
  }
  for (size_t i = 0; i < client->seat_count; i++) {
    size_t count = 0;
    const qa_input_binding *current = qa_seat_input_bindings(client->seats[i].input, &count);
    if (!bindings_equal_identity(current, count, client->seats[i].original, client->seats[i].original_count))
      qa_range_error("Client bindings changed during preparation");
  }
}
void qa_app_prepared_client_commands_release_inputs(qa_app_prepared_client_commands *client, double time) {
  qa_app_prepared_client_commands_validate_publication(client);
  for (size_t i = 0; i < client->seat_count; i++) {
    staged_seat *seat = &client->seats[i];
    if (seat->cleared) qa_seat_input_release(seat->input, time, client->program->commands);
  }
}
void qa_app_prepared_client_commands_publish(qa_app_prepared_client_commands *client) {
  qa_app_prepared_client_commands_validate_publication(client);
  client->program->publish(client->program->ctx);
  if (client->console_bindings != NULL && client->staged_console != NULL) {
    qa_binding_store_unbind_all(client->console_bindings);
    size_t count = 0;
    const qa_input_binding *staged = qa_binding_store_bindings(client->staged_console, &count);
    for (size_t i = 0; i < count; i++)
      if (!qa_binding_store_bind(client->console_bindings, &staged[i])) qa_range_error("Out of memory");
  }
  for (size_t i = 0; i < client->seat_count; i++) {
    staged_seat *seat = &client->seats[i];
    qa_seat_input_unbind_all(seat->input);
    size_t count = 0;
    const qa_input_binding *staged = qa_binding_store_bindings(seat->staged, &count);
    for (size_t j = 0; j < count; j++)
      if (!qa_seat_input_bind(seat->input, &staged[j])) qa_range_error("Out of memory");
  }
}
void qa_app_prepared_client_commands_destroy(qa_app_prepared_client_commands *client) {
  if (client == NULL) return;
  if (client->binding_registration != NULL) qa_binding_commands_registration_destroy(client->binding_registration);
  free(client->print_state);
  if (client->view_registration != NULL) qa_app_q1_view_commands_release(client->view_registration);
  if (client->staged_console != NULL) qa_binding_store_destroy(client->staged_console);
  free(client->original_console);
  for (size_t i = 0; i < client->seat_count; i++) {
    if (client->seats[i].staged != NULL) qa_binding_store_destroy(client->seats[i].staged);
    free(client->seats[i].original);
  }
  free(client->seats);
  if (client->program != NULL && client->program->destroy != NULL) client->program->destroy(client->program->ctx);
  free(client);
}
qa_app_prepared_client_commands *qa_app_prepared_startup_prepare_client_commands(
    qa_app_prepared_startup *startup, const qa_command_buffer_options *options,
    const qa_app_prepared_seat *inputs, size_t input_count, bool has_inputs) {
  if (!has_inputs) {
    inputs = startup->seats;
    input_count = startup->seat_count;
  }
  qa_cvar_registry *live[64];
  size_t live_count = 0;
  qa_cvar_registry *base[] = {startup->source, startup->movement, startup->fallback};
  for (size_t i = 0; i < 3; i++) {
    bool seen = false;
    for (size_t j = 0; j < live_count; j++) if (live[j] == base[i]) seen = true;
    if (!seen && base[i] != NULL) live[live_count++] = base[i];
  }
  for (size_t i = 0; i < startup->seat_count; i++) {
    qa_cvar_registry *pair[] = {startup->seats[i].cvars, startup->seats[i].mouse.cvars};
    for (size_t k = 0; k < 2; k++) {
      bool seen = false;
      for (size_t j = 0; j < live_count; j++) if (live[j] == pair[k]) seen = true;
      if (!seen && live_count < 64) live[live_count++] = pair[k];
    }
  }
  if (startup->routing.visible != NULL) {
    for (size_t c = 0; c < startup->active_seat_count + 2; c++) {
      qa_command_context context;
      if (c == 0) context = qa_cvar_registry_context(startup->source);
      else if (c == 1) context = qa_command_buffer_context(startup->commands);
      else {
        qa_app_prepared_seat *seat = find_seat(startup, startup->active_seat_ids[c - 2], NULL);
        if (seat == NULL) continue;
        context = seat->context;
      }
      if (!current_context(startup, context)) continue;
      size_t visible_count = 0;
      qa_cvar_registry *const *visible = startup->routing.visible(startup->routing.ctx, context, &visible_count);
      for (size_t i = 0; i < visible_count; i++) {
        bool seen = false;
        for (size_t j = 0; j < live_count; j++) if (live[j] == visible[i]) seen = true;
        if (!seen && live_count < 64) live[live_count++] = visible[i];
      }
    }
  }
  return qa_app_prepare_client_commands(startup->commands, inputs, input_count,
      live, live_count, options, startup->console_bindings);
}
