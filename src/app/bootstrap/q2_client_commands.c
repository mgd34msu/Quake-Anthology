/* Port of src/app/bootstrap/q2-client-commands.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/q2_client_commands.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/content/q2/base/player/commands.h"
#include "qa/foundation/support.h"
typedef struct command_ctx {
  qa_app_execute_q2_client_command_fn execute;
  void *execute_ctx;
  const char *definition_name;
} command_ctx;
struct qa_app_q2_client_commands {
  qa_command_buffer *commands;
  char **names; size_t count;
  command_ctx *ctxs;
  char *help_summary; /* owned gamehelp documentation strings */
};
static void handler(void *v, qa_command_invocation *invocation) {
  command_ctx *ctx = v;
  const qa_command_origin *origin = &invocation->source.origin;
  while (origin->kind == QA_COMMAND_ORIGIN_SCRIPT && origin->caller != NULL) origin = origin->caller;
  size_t arg_count = invocation->argc > 0 ? invocation->argc - 1 : 0;
  ctx->execute(ctx->execute_ctx, ctx->definition_name,
      arg_count > 0 ? (const char *const *)(invocation->argv + 1) : NULL, arg_count,
      origin->kind == QA_COMMAND_ORIGIN_LOCAL_SEAT ? &origin->seat : NULL, invocation->source);
}
qa_app_q2_client_commands *qa_app_register_q2_client_commands(qa_command_buffer *commands,
    qa_command_dialect dialect, qa_app_execute_q2_client_command_fn execute, void *execute_ctx) {
  qa_app_q2_client_commands *registration = calloc(1, sizeof *registration);
  if (registration == NULL) qa_range_error("Out of memory");
  registration->commands = commands;
  if (dialect != QA_COMMAND_DIALECT_Q2_CLASSIC && dialect != QA_COMMAND_DIALECT_Q2_RERELEASE) return registration;
  registration->names = calloc(qa_q2_client_command_count, sizeof *registration->names);
  registration->ctxs = calloc(qa_q2_client_command_count, sizeof *registration->ctxs);
  if (registration->names == NULL || registration->ctxs == NULL) qa_range_error("Out of memory");
  static const char *const gamehelp_examples[] = {"gamehelp"};
  for (size_t i = 0; i < qa_q2_client_command_count; i++) {
    const qa_q2_client_command_definition *definition = &qa_q2_client_commands[i];
    const char *name = strcmp(definition->name, "help") == 0 ? "gamehelp" : definition->name;
    if (qa_command_buffer_exists(commands, name)) continue;
    qa_command_documentation documentation = definition->documentation;
    if (strcmp(definition->name, "help") == 0) {
      size_t size = strlen(definition->documentation.summary) + strlen(" For console command help, use help <name>.") + 1;
      registration->help_summary = malloc(size);
      if (registration->help_summary == NULL) qa_range_error("Out of memory");
      snprintf(registration->help_summary, size, "%s For console command help, use help <name>.", definition->documentation.summary);
      documentation.usage = "gamehelp";
      documentation.examples = gamehelp_examples;
      documentation.example_count = 1;
      documentation.summary = registration->help_summary;
    }
    command_ctx *ctx = &registration->ctxs[registration->count];
    ctx->execute = execute; ctx->execute_ctx = execute_ctx; ctx->definition_name = definition->name;
    if (qa_command_buffer_register(commands, name, handler, ctx, &documentation, QA_COMMAND_WORLD)) {
      size_t len = strlen(name);
      char *copy = malloc(len + 1);
      if (copy == NULL) qa_range_error("Out of memory");
      memcpy(copy, name, len + 1);
      registration->names[registration->count++] = copy;
    }
  }
  return registration;
}
void qa_app_q2_client_commands_release(qa_app_q2_client_commands *registration) {
  if (registration == NULL) return;
  for (size_t i = 0; i < registration->count; i++) {
    qa_command_buffer_unregister(registration->commands, registration->names[i], NULL, NULL, false);
    free(registration->names[i]);
  }
  free(registration->names); free(registration->ctxs); free(registration->help_summary); free(registration);
}
