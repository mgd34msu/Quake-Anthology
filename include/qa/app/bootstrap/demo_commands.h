/* Port of src/app/bootstrap/demo-commands.ts. Command registration
 * uses the landed buffer; request identity is a caller token (the TS
 * WeakSet/WeakMap keyed on request objects). GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_DEMO_COMMANDS_H
#define QA_APP_BOOTSTRAP_DEMO_COMMANDS_H
#include <stdbool.h>
#include "qa/app/bootstrap/demo_playback.h"
#include "qa/contracts/common.h"
#include "qa/core/commands/index.h"
/* TS DemoClientState union on kind; request token set for "demo". */
typedef struct qa_app_demo_client_state {
  const char *kind; /* "idle"|"local"|"network"|"demo" */
  qa_app_demo_family family;
  bool explicit_startup; /* idle */
  const void *request; /* demo: identity token */
  qa_app_demo_request request_value;
} qa_app_demo_client_state;
/* TS ClientDemoIntent. */
typedef struct qa_app_client_demo_intent {
  const char *kind; /* "start"|"stop" */
  qa_app_demo_request request; /* start; name owned by the commands object */
  const void *request_token;
  qa_command_context source;
} qa_app_client_demo_intent;
typedef struct qa_app_client_demo_command_host {
  void *ctx;
  bool dedicated;
  qa_app_demo_client_state (*current)(void *ctx, bool has_source, qa_command_context source);
  void (*stage)(void *ctx, qa_app_client_demo_intent intent);
  void (*print)(void *ctx, const char *text);
  void (*append)(void *ctx, const char *text, qa_command_context source);
  /* Malloc'd nextdemo/nextserver text, already cleared; caller frees. */
  char *(*take_completion_command)(void *ctx, const char *family);
} qa_app_client_demo_command_host;
typedef struct qa_app_client_demo_commands qa_app_client_demo_commands;
qa_app_client_demo_commands *qa_app_client_demo_commands_create(qa_app_client_demo_command_host host);
void qa_app_client_demo_commands_destroy(qa_app_client_demo_commands *commands);
/* TS localWorldDemoCommands: host supplies dialect + nextserver access. */
typedef struct qa_app_local_world_demo_host {
  void *ctx;
  bool dedicated;
  qa_command_buffer *commands;
  qa_command_dialect (*dialect)(void *ctx);
  char *(*take_next)(void *ctx, const char *name); /* read+clear cvar; malloc'd */
  void (*print)(void *ctx, const char *text);
} qa_app_local_world_demo_host;
qa_app_client_demo_commands *qa_app_local_world_demo_commands(qa_app_local_world_demo_host host);
/* Returns a binding handle; destroy via _detach with the same handle. */
void *qa_app_client_demo_commands_attach(qa_app_client_demo_commands *commands, qa_command_buffer *buffer);
void qa_app_client_demo_commands_detach(qa_app_client_demo_commands *commands, void *binding);
bool qa_app_client_demo_commands_handle(qa_app_client_demo_commands *commands, const char *name_input,
                                        const char *const *args, size_t arg_count, qa_command_context source);
void qa_app_client_demo_commands_refresh(qa_app_client_demo_commands *commands);
/* reason: "eof"|"terminator"|"disconnected"|"truncated"|"closed". */
void qa_app_client_demo_commands_complete(qa_app_client_demo_commands *commands, const void *request_token, const char *reason);
void qa_app_client_demo_commands_failed(qa_app_client_demo_commands *commands, const void *request_token);
void qa_app_client_demo_commands_manual_game(qa_app_client_demo_commands *commands);
#endif
