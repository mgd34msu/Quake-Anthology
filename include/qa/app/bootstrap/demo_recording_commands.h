/* Port of src/app/bootstrap/demo-recording-commands.ts. Async host
 * feeds become synchronous fn+ctx returning error text; detach is an
 * identity-checked sink token. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_DEMO_RECORDING_COMMANDS_H
#define QA_APP_BOOTSTRAP_DEMO_RECORDING_COMMANDS_H
#include <stdbool.h>
#include "qa/app/bootstrap/demo_recording.h"
#include "qa/contracts/common.h"
#include "qa/core/commands/index.h"
/* TS DemoRecordingIntent union on kind; name NULL mirrors undefined. */
typedef struct qa_app_demo_recording_intent {
  const char *kind; const char *name; qa_command_context source;
} qa_app_demo_recording_intent;
/* One recording feed (server/mvd/main): seed fills a seed whose packet
 * storage the host keeps valid for the call; attach returns a token. */
typedef struct qa_app_recording_feed {
  void *ctx;
  const char *(*seed)(void *ctx, qa_command_context source, qa_app_demo_recording_seed *out);
  const void *(*attach)(void *ctx, qa_app_demo_recording_sink sink);
  void (*detach)(void *ctx, const void *token);
} qa_app_recording_feed;
typedef struct qa_app_demo_recording_host {
  void *ctx;
  bool has_server_recording; qa_app_recording_feed server_recording;
  bool has_mvd_recording; qa_app_recording_feed mvd_recording;
  const char *(*root)(void *ctx);
  qa_app_recording_feed main_feed; /* seed + attach */
  const void *(*reconnect_recording)(void *ctx, qa_command_context source, qa_app_demo_recording_sink sink, const char **out_error);
  void (*reconnect_detach)(void *ctx, const void *token);
  void (*print)(void *ctx, const char *text);
  void (*stage)(void *ctx, qa_app_demo_recording_intent intent);
} qa_app_demo_recording_host;
typedef struct qa_app_client_demo_recording qa_app_client_demo_recording;
qa_app_client_demo_recording *qa_app_client_demo_recording_create(qa_app_demo_recording_host host);
void qa_app_client_demo_recording_destroy(qa_app_client_demo_recording *recording);
/* TS attach(): returns binding; release with _detach_commands. */
void *qa_app_client_demo_recording_attach(qa_app_client_demo_recording *recording, qa_command_buffer *commands);
void qa_app_client_demo_recording_detach_commands(qa_app_client_demo_recording *recording, void *binding);
const char *qa_app_client_demo_recording_path(const qa_app_client_demo_recording *recording);
const char *qa_app_client_demo_recording_server_path(const qa_app_client_demo_recording *recording);
/* Error-returning (TS async throws). name NULL mirrors undefined. */
const char *qa_app_client_demo_recording_start(qa_app_client_demo_recording *recording, const char *name, qa_command_context source);
const char *qa_app_client_demo_recording_start_mvd(qa_app_client_demo_recording *recording, const char *name, qa_command_context source);
const char *qa_app_client_demo_recording_start_server(qa_app_client_demo_recording *recording, const char *name, qa_command_context source);
const char *qa_app_client_demo_recording_stop_server(qa_app_client_demo_recording *recording);
const char *qa_app_client_demo_recording_stop_all(qa_app_client_demo_recording *recording);
const char *qa_app_client_demo_recording_rerecord(qa_app_client_demo_recording *recording, const char *name, qa_command_context source);
const char *qa_app_client_demo_recording_stop_mvd(qa_app_client_demo_recording *recording);
const char *qa_app_client_demo_recording_stop(qa_app_client_demo_recording *recording);
#endif
