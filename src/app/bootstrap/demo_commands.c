/* Port of src/app/bootstrap/demo-commands.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/demo_commands.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
typedef struct binding {
  qa_command_buffer *buffer;
  qa_command_handler_fn handlers[7];
  const char *names[7];
  size_t count;
  bool has_timedemo;
  struct binding *next;
} binding;
typedef struct token_row { const void *token; bool completed; qa_command_context source; } token_row;
struct qa_app_client_demo_commands {
  qa_app_client_demo_command_host host;
  char *names[8]; size_t name_count;
  long next;
  bool has_cycle_source; qa_command_context cycle_source;
  const void *latest_request;
  char *latest_name;
  token_row *tokens; size_t token_count, token_cap;
  binding *bindings;
  /* localWorldDemoCommands storage */
  qa_app_local_world_demo_host local_host;
  bool local_world;
};
static char *dup(const char *s) { size_t n = strlen(s) + 1; char *p = malloc(n); if (p) memcpy(p, s, n); return p; }
static bool local_source(qa_command_context source) {
  const qa_command_origin *origin = &source.origin;
  while (origin->kind == QA_COMMAND_ORIGIN_SCRIPT && origin->caller) origin = origin->caller;
  return origin->kind != QA_COMMAND_ORIGIN_REMOTE_CLIENT;
}
static token_row *token_for(qa_app_client_demo_commands *commands, const void *token, bool create) {
  for (size_t i = 0; i < commands->token_count; i++)
    if (commands->tokens[i].token == token) return &commands->tokens[i];
  if (!create) return NULL;
  if (commands->token_count == commands->token_cap) {
    commands->token_cap = commands->token_cap ? commands->token_cap * 2 : 8;
    commands->tokens = realloc(commands->tokens, commands->token_cap * sizeof *commands->tokens);
  }
  token_row *row = &commands->tokens[commands->token_count++];
  memset(row, 0, sizeof *row);
  row->token = token;
  return row;
}
static void start_request(qa_app_client_demo_commands *commands, qa_app_demo_family family, const char *name, bool timedemo, qa_command_context source) {
  free(commands->latest_name);
  commands->latest_name = dup(name);
  /* Request identity: the owned name cell stands in for the TS object. */
  commands->latest_request = commands->latest_name;
  token_row *row = token_for(commands, commands->latest_request, true);
  row->source = source;
  qa_app_client_demo_intent intent = {"start", {family, commands->latest_name, timedemo}, commands->latest_request, source};
  commands->host.stage(commands->host.ctx, intent);
}
static void next_demo(qa_app_client_demo_commands *commands) {
  if (commands->next < 0 || !commands->has_cycle_source) return;
  if ((size_t)commands->next >= commands->name_count || commands->names[commands->next][0] == '\0') commands->next = 0;
  const char *name = commands->name_count ? commands->names[commands->next] : NULL;
  if (!name || name[0] == '\0') {
    commands->next = -1;
    commands->host.print(commands->host.ctx, "No demos listed with startdemos\n");
    return;
  }
  commands->next++;
  start_request(commands, qa_app_demo_family_of(name, QA_APP_DEMO_Q1), name, false, commands->cycle_source);
}
static void play(qa_app_client_demo_commands *commands, const char *command_name, const char *const *args, size_t arg_count,
                 qa_command_context source, bool has_selected, qa_app_demo_family selected, bool timedemo) {
  if (commands->host.dedicated && !(has_selected && selected == QA_APP_DEMO_Q2)) return;
  if (arg_count != 1 || !args[0]) {
    char text[256];
    snprintf(text, sizeof text, "Usage: %s <name>\n", command_name);
    commands->host.print(commands->host.ctx, text);
    return;
  }
  commands->next = -1;
  qa_app_demo_family family = has_selected ? selected : qa_app_demo_family_of(args[0], commands->host.current(commands->host.ctx, false, source).family);
  start_request(commands, family, args[0], timedemo, source);
}
bool qa_app_client_demo_commands_handle(qa_app_client_demo_commands *commands, const char *name_input,
                                        const char *const *args, size_t arg_count, qa_command_context source) {
  char name[64];
  size_t len = strlen(name_input);
  if (len >= sizeof name) return false;
  for (size_t i = 0; i <= len; i++) name[i] = (char)tolower((unsigned char)name_input[i]); /* asciiFold */
  bool known = strcmp(name, "playdemo") == 0 || strcmp(name, "demo") == 0 || strcmp(name, "demomap") == 0 ||
      strcmp(name, "startdemos") == 0 || strcmp(name, "demos") == 0 || strcmp(name, "stopdemo") == 0;
  if (strcmp(name, "timedemo") == 0) {
    qa_app_demo_family family = commands->host.current(commands->host.ctx, false, source).family;
    if (family != QA_APP_DEMO_Q1 && family != QA_APP_DEMO_QW) return false;
    known = true;
  }
  if (!known) return false;
  if (!local_source(source)) {
    char text[256];
    snprintf(text, sizeof text, "%s is a local client command.\n", name);
    commands->host.print(commands->host.ctx, text);
    return true;
  }
  if (strcmp(name, "playdemo") == 0) play(commands, name, args, arg_count, source, false, QA_APP_DEMO_Q1, false);
  else if (strcmp(name, "demo") == 0) play(commands, name, args, arg_count, source, true, QA_APP_DEMO_Q3, false);
  else if (strcmp(name, "demomap") == 0) play(commands, name, args, arg_count, source, true, QA_APP_DEMO_Q2, false);
  else if (strcmp(name, "timedemo") == 0) play(commands, name, args, arg_count, source, false, QA_APP_DEMO_Q1, true);
  else if (strcmp(name, "startdemos") == 0) {
    qa_app_demo_client_state current = commands->host.current(commands->host.ctx, true, source);
    if (commands->host.dedicated) {
      if (strcmp(current.kind, "idle") == 0 && !current.explicit_startup) commands->host.append(commands->host.ctx, "map start\n", source);
      return true;
    }
    if (arg_count > 8) commands->host.print(commands->host.ctx, "Max 8 demos in demoloop\n");
    for (size_t i = 0; i < commands->name_count; i++) free(commands->names[i]);
    commands->name_count = arg_count < 8 ? arg_count : 8;
    for (size_t i = 0; i < commands->name_count; i++) {
      char truncated[16];
      snprintf(truncated, sizeof truncated, "%s", args[i]); /* slice(0,15) */
      commands->names[i] = dup(truncated);
    }
    char text[64];
    snprintf(text, sizeof text, "%zu demo(s) in loop\n", commands->name_count);
    commands->host.print(commands->host.ctx, text);
    commands->cycle_source = source;
    commands->has_cycle_source = true;
    if (strcmp(current.kind, "idle") == 0 && !current.explicit_startup && commands->next != -1) {
      commands->next = 0;
      next_demo(commands);
    } else commands->next = -1;
  } else if (strcmp(name, "demos") == 0) {
    if (!commands->host.dedicated) {
      if (commands->next < 0) commands->next = 1;
      commands->cycle_source = source;
      commands->has_cycle_source = true;
      next_demo(commands);
    }
  } else if (strcmp(name, "stopdemo") == 0) {
    if (!commands->host.dedicated && strcmp(commands->host.current(commands->host.ctx, false, source).kind, "demo") == 0) {
      commands->next = -1;
      commands->latest_request = NULL;
      qa_app_client_demo_intent intent = {"stop", {QA_APP_DEMO_Q1, NULL, false}, NULL, source};
      commands->host.stage(commands->host.ctx, intent);
    }
  }
  return true;
}
typedef struct { qa_app_client_demo_commands *commands; const char *name; } handler_ctx;
static void demo_handler(void *ctx, qa_command_invocation *command) {
  handler_ctx *handler = ctx;
  size_t count = command->argc > 0 ? command->argc - 1 : 0;
  qa_app_client_demo_commands_handle(handler->commands, handler->name,
      count ? (const char *const *)(command->argv + 1) : NULL, count, command->source);
}
static void binding_refresh(qa_app_client_demo_commands *commands, binding *bind) {
  qa_app_demo_family family = commands->host.current(commands->host.ctx, false, (qa_command_context){0}).family;
  bool want = family == QA_APP_DEMO_Q1 || family == QA_APP_DEMO_QW;
  if (want && !bind->has_timedemo) {
    handler_ctx *ctx = malloc(sizeof *ctx);
    *ctx = (handler_ctx){commands, "timedemo"};
    qa_command_documentation doc = {"Benchmark a Quake recording.", "timedemo <name>", NULL, 0, NULL, 0};
    static const char *const examples[] = {"timedemo demo1"};
    doc.examples = examples; doc.example_count = 1;
    if (qa_command_buffer_register(bind->buffer, "timedemo", demo_handler, ctx, &doc, QA_COMMAND_WORLD)) bind->has_timedemo = true;
    else { commands->host.print(commands->host.ctx, "Demo command timedemo is already owned by another command.\n"); free(ctx); }
  } else if (!want && bind->has_timedemo) {
    qa_command_buffer_unregister(bind->buffer, "timedemo", NULL, NULL, false);
    bind->has_timedemo = false;
  }
}
void *qa_app_client_demo_commands_attach(qa_app_client_demo_commands *commands, qa_command_buffer *buffer) {
  binding *bind = calloc(1, sizeof *bind);
  bind->buffer = buffer;
  static const struct { const char *name, *summary, *usage; } rows[] = {
      {"playdemo", "Play a recording; its file extension selects the game.", "playdemo <name>"},
      {"demo", "Play a Quake III recording.", "demo <name>"},
      {"demomap", "Play a Quake II recording.", "demomap <name>"},
      {"startdemos", "Set the Quake attract-mode recording list.", "startdemos [name ...]"},
      {"demos", "Resume the saved Quake attract list.", "demos"},
      {"stopdemo", "Stop the active recording and return to the menu.", "stopdemo"}};
  for (size_t i = 0; i < 6; i++) {
    handler_ctx *ctx = malloc(sizeof *ctx);
    *ctx = (handler_ctx){commands, rows[i].name};
    char example_buf[64];
    snprintf(example_buf, sizeof example_buf, "%s", rows[i].usage);
    char *marker = strstr(example_buf, "<name>");
    if (marker) memmove(marker + 5, marker + 6, strlen(marker + 6) + 1), memcpy(marker, "demo1", 5);
    const char *examples_storage[1];
    char *example = dup(rows[i].usage);
    char *found = strstr(example, "<name>");
    if (found) { memmove(found + 5, found + 6, strlen(found + 6) + 1); memcpy(found, "demo1", 5); }
    examples_storage[0] = example;
    qa_command_documentation doc = {rows[i].summary, rows[i].usage, examples_storage, 1, NULL, 0};
    if (qa_command_buffer_register(buffer, rows[i].name, demo_handler, ctx, &doc, QA_COMMAND_WORLD)) {
      bind->handlers[bind->count] = demo_handler;
      bind->names[bind->count++] = rows[i].name;
    } else {
      char text[256];
      snprintf(text, sizeof text, "Demo command %s is already owned by another command.\n", rows[i].name);
      commands->host.print(commands->host.ctx, text);
      free(ctx);
    }
    free(example);
  }
  bind->next = commands->bindings;
  commands->bindings = bind;
  binding_refresh(commands, bind);
  return bind;
}
void qa_app_client_demo_commands_detach(qa_app_client_demo_commands *commands, void *handle) {
  binding *bind = handle;
  for (size_t i = 0; i < bind->count; i++) qa_command_buffer_unregister(bind->buffer, bind->names[i], bind->handlers[i], NULL, false);
  if (bind->has_timedemo) qa_command_buffer_unregister(bind->buffer, "timedemo", NULL, NULL, false);
  binding **cursor = &commands->bindings;
  while (*cursor && *cursor != bind) cursor = &(*cursor)->next;
  if (*cursor) *cursor = bind->next;
  free(bind);
}
void qa_app_client_demo_commands_refresh(qa_app_client_demo_commands *commands) {
  for (binding *bind = commands->bindings; bind; bind = bind->next) binding_refresh(commands, bind);
}
void qa_app_client_demo_commands_complete(qa_app_client_demo_commands *commands, const void *request_token, const char *reason) {
  qa_app_demo_client_state current = commands->host.current(commands->host.ctx, false, (qa_command_context){0});
  token_row *row = token_for(commands, request_token, false);
  if (strcmp(current.kind, "demo") != 0 || current.request != request_token || commands->latest_request != request_token || (row && row->completed)) return;
  row = token_for(commands, request_token, true);
  row->completed = true;
  if (strcmp(reason, "closed") == 0 || strcmp(reason, "truncated") == 0) { commands->next = -1; return; }
  qa_app_demo_family family = current.request_value.family;
  if (family == QA_APP_DEMO_Q1 || family == QA_APP_DEMO_QW) { next_demo(commands); return; }
  if (strcmp(reason, "disconnected") == 0) return;
  char *text = commands->host.take_completion_command(commands->host.ctx, family == QA_APP_DEMO_Q2 ? "q2" : "q3");
  if (text && text[0] && row) {
    char *line = malloc(strlen(text) + 2);
    sprintf(line, "%s\n", text);
    commands->host.append(commands->host.ctx, line, row->source);
    free(line);
  }
  free(text);
}
void qa_app_client_demo_commands_failed(qa_app_client_demo_commands *commands, const void *request_token) {
  token_for(commands, request_token, true)->completed = true;
  if (commands->latest_request == request_token) commands->next = -1;
}
void qa_app_client_demo_commands_manual_game(qa_app_client_demo_commands *commands) {
  commands->next = -1;
  commands->latest_request = NULL;
}
qa_app_client_demo_commands *qa_app_client_demo_commands_create(qa_app_client_demo_command_host host) {
  qa_app_client_demo_commands *commands = calloc(1, sizeof *commands);
  if (!commands) return NULL;
  commands->host = host;
  return commands;
}
static qa_app_demo_client_state local_current(void *ctx, bool has_source, qa_command_context source) {
  (void)has_source; (void)source;
  qa_app_client_demo_commands *commands = ctx;
  qa_command_dialect dialect = commands->local_host.dialect(commands->local_host.ctx);
  qa_app_demo_family family = dialect == QA_COMMAND_DIALECT_Q1_NETQUAKE ? QA_APP_DEMO_Q1
      : dialect == QA_COMMAND_DIALECT_Q1_QUAKEWORLD ? QA_APP_DEMO_QW : dialect == QA_COMMAND_DIALECT_Q3 ? QA_APP_DEMO_Q3 : QA_APP_DEMO_Q2;
  return (qa_app_demo_client_state){"local", family, false, NULL, {family, NULL, false}};
}
static void local_stage(void *ctx, qa_app_client_demo_intent intent) {
  (void)ctx;
  if (strcmp(intent.kind, "start") == 0)
    qa_range_error("Demo playback (%s) requires the graphical launcher; this application owns a local game.", intent.request.name);
  qa_range_error("Stopping demo playback requires its retained client owner.");
}
static void local_print(void *ctx, const char *text) {
  qa_app_client_demo_commands *commands = ctx;
  commands->local_host.print(commands->local_host.ctx, text);
}
static void local_append(void *ctx, const char *text, qa_command_context source) {
  qa_app_client_demo_commands *commands = ctx;
  qa_command_buffer_append(commands->local_host.commands, text, &source, -1);
}
static char *local_take(void *ctx, const char *family) {
  qa_app_client_demo_commands *commands = ctx;
  return commands->local_host.take_next(commands->local_host.ctx, strcmp(family, "q2") == 0 ? "nextserver" : "nextdemo");
}
qa_app_client_demo_commands *qa_app_local_world_demo_commands(qa_app_local_world_demo_host host) {
  qa_app_client_demo_commands *commands = calloc(1, sizeof *commands);
  if (!commands) return NULL;
  commands->local_host = host;
  commands->local_world = true;
  commands->host = (qa_app_client_demo_command_host){commands, host.dedicated, local_current, local_stage, local_print, local_append, local_take};
  return commands;
}
void qa_app_client_demo_commands_destroy(qa_app_client_demo_commands *commands) {
  if (!commands) return;
  while (commands->bindings) qa_app_client_demo_commands_detach(commands, commands->bindings);
  for (size_t i = 0; i < commands->name_count; i++) free(commands->names[i]);
  free(commands->latest_name);
  free(commands->tokens);
  free(commands);
}
