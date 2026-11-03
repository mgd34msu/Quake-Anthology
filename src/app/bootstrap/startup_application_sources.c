/* Port of src/app/bootstrap/startup.ts bindDemoCommands(prepared): the
 * client recorder (ClientDemoRecording) over the published source, the demo
 * playback commands (ClientDemoCommands) and the connect/disconnect/quit names
 * the prepared startup does not own. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "startup_application_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"

static char error_text[sizeof ((qa_caught_error *)0)->message];
static const char *caught_text(const qa_caught_error *caught) {
  snprintf(error_text, sizeof error_text, "%s", caught->message);
  return error_text;
}

/* The recorder and demo-command hosts' ctx: the application and the prepared
 * startup bindDemoCommands was given. */
typedef struct demo_binding { qa_startup_application *app; qa_app_prepared_startup *prepared; } demo_binding;

/* ---- ClientDemoRecording host --------------------------------------------- */
static const char *recording_root(void *ctx) {
  qa_startup_application *app = ((demo_binding *)ctx)->app;
  if (app->recording_root == NULL) qa_raise("Error", 0, 0, "Recording has no selected source");
  return app->recording_root;
}
static void preparation_clear(qa_startup_recording_preparation *preparation) {
  memset(preparation, 0, sizeof *preparation);
}
/* The detach token a feed attach returns: TS () => { detach(); owner/preparation reset }. */
typedef struct feed_token {
  qa_startup_application *app;
  qa_startup_recording_preparation selected;
  const void *inner;
  int kind; /* 0 main (recordingOwner), 1 server (serverRecordingOwner), 2 mvd (recordingOwner) */
} feed_token;

/* seed: TS recording seed(context). */
static const char *main_seed(void *ctx, qa_command_context context, qa_app_demo_recording_seed *out) {
  qa_startup_application *app = ctx;
  const qa_app_client_source_lifetime *source = qa_startup_current_source(app);
  if (source == NULL) return "Recording requires an active source";
  qa_app_client_recording_feed feed;
  memset(&feed, 0, sizeof feed);
  const char *error = qa_app_client_source_prepare_recording(source, context, &feed);
  if (error != NULL) return error;
  if (qa_startup_current_source(app) != source) return "Recording source changed during preparation";
  preparation_clear(&app->recording_preparation);
  app->recording_preparation.source = source;
  app->recording_preparation.client_feed = feed;
  app->recording_preparation.remote_source = qa_startup_source_is_remote(source, app->remote) ? app->remote : NULL;
  app->recording_preparation.application_source = qa_startup_source_is_game(source, app->game) ? app->game : NULL;
  app->recording_preparation.active = true;
  qa_startup_set_text(&app->recording_root, feed.root);
  const void *seed = NULL;
  error = feed.seed(feed.ctx, &seed);
  if (error != NULL) return error;
  *out = *(const qa_app_demo_recording_seed *)seed;
  return NULL;
}
static const void *main_attach(void *ctx, qa_app_demo_recording_sink sink) {
  qa_startup_application *app = ctx;
  const qa_startup_recording_preparation *captured = &app->recording_preparation;
  if (!captured->active || qa_startup_current_source(app) != captured->source)
    qa_raise("Error", 0, 0, "Recording source was retired before attachment");
  feed_token *token = calloc(1, sizeof *token);
  if (token == NULL) qa_range_error("Out of memory");
  token->app = app;
  token->selected = *captured;
  token->kind = 0;
  token->inner = captured->client_feed.attach(captured->client_feed.ctx, &sink);
  app->recording_owner = captured->source;
  return token;
}
static bool same_preparation(const qa_startup_recording_preparation *a, const qa_startup_recording_preparation *b) {
  return a->active && b->active && a->source == b->source && a->feed == b->feed && a->client_feed.ctx == b->client_feed.ctx;
}
static void feed_detach(void *ctx, const void *raw) {
  (void)ctx;
  feed_token *token = (feed_token *)raw;
  qa_startup_application *app = token->app;
  if (token->kind == 0) token->selected.client_feed.detach(token->selected.client_feed.ctx, token->inner);
  else qa_application_recording_feed_detach(token->selected.feed, (void *)token->inner);
  if (token->kind == 1) {
    if (app->server_recording_owner == token->selected.application_source) app->server_recording_owner = NULL;
    if (same_preparation(&app->server_recording_preparation, &token->selected)) preparation_clear(&app->server_recording_preparation);
  } else {
    if (app->recording_owner == token->selected.source) app->recording_owner = NULL;
    if (same_preparation(&app->recording_preparation, &token->selected)) preparation_clear(&app->recording_preparation);
  }
  free(token);
}
/* serverRecording / mvdRecording seed: the published source must be the hosted Application. */
typedef struct server_seed_call {
  qa_startup_application *app; qa_command_context context; bool mvd; qa_application_recording_feed *feed;
} server_seed_call;
static void server_seed_body(void *ctx) {
  server_seed_call *call = ctx;
  call->feed = call->mvd ? qa_application_prepare_mvd_recording(call->app->game, call->context)
      : qa_application_prepare_server_recording(call->app->game, call->context);
}
static const char *application_seed(qa_startup_application *app, qa_command_context context, bool mvd,
    qa_app_demo_recording_seed *out) {
  const qa_app_client_source_lifetime *source = qa_startup_current_source(app);
  if (!qa_startup_source_is_game(source, app->game))
    return mvd ? "mvdrecord requires a hosted Quake II server" : "serverrecord requires a hosted classic Quake II server";
  server_seed_call call = {app, context, mvd, NULL};
  qa_caught_error caught;
  if (!qa_try(server_seed_body, &call, &caught)) return caught_text(&caught);
  qa_startup_recording_preparation *preparation = mvd ? &app->recording_preparation : &app->server_recording_preparation;
  preparation_clear(preparation);
  preparation->source = source;
  preparation->application_source = app->game;
  preparation->feed = call.feed;
  preparation->active = true;
  qa_startup_set_text(&app->recording_root, qa_application_recording_feed_root(call.feed));
  *out = qa_application_recording_feed_seed(call.feed);
  return NULL;
}
static const char *server_seed(void *ctx, qa_command_context context, qa_app_demo_recording_seed *out) {
  return application_seed(ctx, context, false, out);
}
static const char *mvd_seed(void *ctx, qa_command_context context, qa_app_demo_recording_seed *out) {
  return application_seed(ctx, context, true, out);
}
static const void *application_attach(qa_startup_application *app, bool mvd, qa_app_demo_recording_sink sink) {
  const qa_startup_recording_preparation *selected = mvd ? &app->recording_preparation : &app->server_recording_preparation;
  if (!selected->active || selected->feed == NULL || qa_startup_current_source(app) != selected->source)
    qa_raise("Error", 0, 0, "%s", mvd ? "Multiview source was retired before attachment" : "Server demo source was retired before attachment");
  feed_token *token = calloc(1, sizeof *token);
  if (token == NULL) qa_range_error("Out of memory");
  token->app = app;
  token->selected = *selected;
  token->kind = mvd ? 2 : 1;
  token->inner = qa_application_recording_feed_attach(selected->feed, sink);
  if (mvd) app->recording_owner = selected->source;
  else app->server_recording_owner = selected->application_source;
  return token;
}
static const void *server_attach(void *ctx, qa_app_demo_recording_sink sink) { return application_attach(ctx, false, sink); }
static const void *mvd_attach(void *ctx, qa_app_demo_recording_sink sink) { return application_attach(ctx, true, sink); }

/* reconnectRecording(context, sink): reconnect the live QuakeWorld source and
 * record its signon. The connecting recorder takes a QuakeWorld sink; each
 * record reaches the demo sink as a "qw" packet. */
typedef struct reconnect_token {
  qa_startup_application *app;
  qa_remote_application *next;
  const qa_app_client_source_lifetime *next_source;
  void *inner;
  qa_app_demo_recording_sink sink;
} reconnect_token;
static void reconnect_append(void *ctx, const qa_quakeworld_demo_record *record) {
  reconnect_token *token = ctx;
  qa_app_demo_recording_packet packet;
  memset(&packet, 0, sizeof packet);
  packet.kind = QA_APP_RECORDING_QW;
  packet.record = *record;
  /* The demo sink reports and aborts its own recording on a write failure. */
  const char *error = token->sink.append(token->sink.ctx, packet);
  if (error != NULL && token->app->recording_owner == token->next_source) token->app->recording_owner = NULL;
}
typedef struct reconnect_call { qa_startup_application *app; qa_app_options options; reconnect_token *token; } reconnect_call;
static void reconnect_body(void *ctx) {
  reconnect_call *call = ctx;
  qa_startup_application *app = call->app;
  qa_remote_application *previous = app->recording_preparation.remote_source;
  qa_startup_connect_options(app, &call->options);
  qa_remote_application *next = app->remote;
  const qa_app_client_source_lifetime *current = qa_startup_current_source(app);
  if (next == NULL || next == previous || !qa_startup_source_is_remote(current, next))
    qa_raise("Error", 0, 0, "QuakeWorld reconnect did not publish a new connection");
  call->token->next = next;
  call->token->next_source = current;
  qa_app_qw_recording_sink sink = {call->token, reconnect_append};
  call->token->inner = qa_remote_application_attach_connecting_recording(next, sink);
  app->recording_owner = current;
  preparation_clear(&app->recording_preparation);
}
static const void *reconnect_recording(void *ctx, qa_command_context source, qa_app_demo_recording_sink sink, const char **out_error) {
  (void)source;
  qa_startup_application *app = ((demo_binding *)ctx)->app;
  *out_error = NULL;
  qa_remote_application *previous = app->recording_preparation.active ? app->recording_preparation.remote_source : NULL;
  if (previous == NULL || strcmp(qa_remote_application_options(previous)->network_kind, "qw-client") != 0) {
    *out_error = "rerecord requires a live QuakeWorld connection";
    return NULL;
  }
  reconnect_call call;
  memset(&call, 0, sizeof call);
  call.app = app;
  qa_app_startup_options_copy(&call.options, qa_remote_application_options(previous));
  call.token = calloc(1, sizeof *call.token);
  if (call.token == NULL) qa_range_error("Out of memory");
  call.token->app = app;
  call.token->sink = sink;
  qa_caught_error caught;
  bool connected = qa_try(reconnect_body, &call, &caught);
  qa_app_options_destroy(&call.options);
  if (!connected) {
    free(call.token);
    *out_error = caught_text(&caught);
    return NULL;
  }
  return call.token;
}
static void reconnect_detach(void *ctx, const void *raw) {
  (void)ctx;
  reconnect_token *token = (reconnect_token *)raw;
  qa_remote_application_detach_connecting_recording(token->next, token->inner);
  if (token->app->recording_owner == token->next_source) token->app->recording_owner = NULL;
  free(token);
}
static void recording_print(void *ctx, const char *text) { qa_startup_application_print(((demo_binding *)ctx)->app, text); }
static void recording_stage(void *ctx, qa_app_demo_recording_intent intent) {
  demo_binding *binding = ctx;
  qa_startup_application *app = binding->app;
  if (app->has_pending_recording) {
    free(app->pending_recording.name);
    qa_command_context_release(&app->pending_recording.source);
  }
  app->pending_recording.kind = intent.kind;
  app->pending_recording.name = qa_startup_dup(intent.name);
  app->pending_recording.source = qa_command_context_copy(&intent.source);
  app->has_pending_recording = true;
  qa_app_prepared_startup_note_world_action(binding->prepared);
}

/* ---- ClientDemoCommands host ---------------------------------------------- */
static qa_app_demo_family dialect_family(qa_app_prepared_startup *prepared) {
  qa_command_dialect dialect = qa_command_buffer_dialect(qa_app_prepared_startup_commands(prepared));
  return dialect == QA_COMMAND_DIALECT_Q1_NETQUAKE ? QA_APP_DEMO_Q1 : dialect == QA_COMMAND_DIALECT_Q1_QUAKEWORLD ? QA_APP_DEMO_QW
      : dialect == QA_COMMAND_DIALECT_Q3 ? QA_APP_DEMO_Q3 : QA_APP_DEMO_Q2;
}
static qa_app_demo_client_state demo_current(void *ctx, bool has_source, qa_command_context source) {
  demo_binding *binding = ctx;
  qa_startup_application *app = binding->app;
  qa_app_demo_client_state state;
  memset(&state, 0, sizeof state);
  if (app->has_active_demo) {
    state.kind = "demo";
    state.family = app->active_demo.family;
    state.request = app->active_demo_token;
    state.request_value = app->active_demo;
  } else if (app->game != NULL) {
    state.kind = "local";
    state.family = dialect_family(binding->prepared);
  } else if (app->remote != NULL) {
    state.kind = "network";
    state.family = dialect_family(binding->prepared);
  } else {
    state.kind = "idle";
    state.family = dialect_family(binding->prepared);
    state.explicit_startup = app->initial_configuration && has_source
        && qa_app_prepared_startup_is_configuration_source(binding->prepared, source);
  }
  return state;
}
static void demo_stage(void *ctx, qa_app_client_demo_intent intent) {
  demo_binding *binding = ctx;
  qa_startup_application *app = binding->app;
  if (app->has_pending_demo) qa_command_context_release(&app->pending_demo.source);
  app->pending_demo = intent;
  app->pending_demo.source = qa_command_context_copy(&intent.source);
  app->has_pending_demo = true;
  qa_app_prepared_startup_note_world_action(binding->prepared);
}
static void demo_print(void *ctx, const char *text) { qa_startup_application_print(((demo_binding *)ctx)->app, text); }
static void demo_append(void *ctx, const char *text, qa_command_context source) {
  demo_binding *binding = ctx;
  qa_command_buffer_append(qa_app_prepared_startup_commands(binding->prepared), text, &source, -1);
}
static char *demo_take_completion(void *ctx, const char *family) {
  demo_binding *binding = ctx;
  const char *name = strcmp(family, "q2") == 0 ? "nextserver" : "nextdemo";
  qa_cvar_registry *cvars = qa_app_prepared_startup_source(binding->prepared);
  char *command = qa_cvar_registry_variable_string(cvars, name);
  qa_cvar_registry_set(cvars, name, "", true, NULL);
  return command;
}

/* ---- connect / disconnect / quit names --------------------------------------- */
typedef struct source_command {
  qa_startup_application *app; qa_app_prepared_startup *prepared; const char *name;
} source_command;
static void source_command_handler(void *ctx, qa_command_invocation *command) {
  source_command *handler = ctx;
  const char **args = calloc(command->argc > 1 ? command->argc : 1, sizeof *args);
  if (args == NULL) qa_range_error("Out of memory");
  for (size_t i = 1; i < command->argc; i++) args[i - 1] = command->argv[i];
  qa_cleanup guard;
  qa_cleanup_push(&guard, free, args);
  qa_startup_application_route_command(handler->app, handler->name, args, command->argc > 0 ? command->argc - 1 : 0,
      command->source, handler->prepared, false);
  qa_cleanup_pop(&guard);
  free(args);
  qa_app_prepared_startup_note_world_action(handler->prepared);
}
static void release_source_command(qa_startup_application *app, void *ctx) {
  (void)app;
  source_command *handler = ctx;
  qa_command_buffer_unregister(qa_app_prepared_startup_commands(handler->prepared), handler->name, source_command_handler, handler, true);
  free(handler);
}
static void release_recording_commands(qa_startup_application *app, void *ctx) {
  qa_app_client_demo_recording_detach_commands(app->recording, ctx);
}

void qa_startup_bind_demo_commands(qa_startup_application *app, qa_app_prepared_startup *prepared) {
  demo_binding *binding = calloc(1, sizeof *binding);
  if (binding == NULL) qa_range_error("Out of memory");
  binding->app = app;
  binding->prepared = prepared;
  qa_app_demo_recording_host host;
  memset(&host, 0, sizeof host);
  host.ctx = binding;
  host.root = recording_root;
  host.main_feed = (qa_app_recording_feed){app, main_seed, main_attach, feed_detach};
  host.has_server_recording = true;
  host.server_recording = (qa_app_recording_feed){app, server_seed, server_attach, feed_detach};
  host.has_mvd_recording = true;
  host.mvd_recording = (qa_app_recording_feed){app, mvd_seed, mvd_attach, feed_detach};
  host.reconnect_recording = reconnect_recording;
  host.reconnect_detach = reconnect_detach;
  host.print = recording_print;
  host.stage = recording_stage;
  qa_app_client_demo_recording *recording = qa_app_client_demo_recording_create(host);
  if (recording == NULL) qa_range_error("Out of memory");
  app->recording = recording;
  /* The binding is the recorder's and the demo commands' host ctx; destroy frees it after them. */
  app->registered_commands = binding;
  void *attached = qa_app_client_demo_recording_attach(recording, qa_app_prepared_startup_commands(prepared));
  qa_startup_push_release(app, release_recording_commands, attached);

  qa_app_client_demo_command_host demos;
  memset(&demos, 0, sizeof demos);
  demos.ctx = binding;
  demos.dedicated = false;
  demos.current = demo_current;
  demos.stage = demo_stage;
  demos.print = demo_print;
  demos.append = demo_append;
  demos.take_completion_command = demo_take_completion;
  app->demos = qa_app_client_demo_commands_create(demos);
  if (app->demos == NULL) qa_range_error("Out of memory");
  app->release_demos = qa_app_client_demo_commands_attach(app->demos, qa_app_prepared_startup_commands(prepared));
  static const char *const names[] = {"connect", "disconnect", "quit"};
  qa_command_buffer *commands = qa_app_prepared_startup_commands(prepared);
  for (size_t i = 0; i < 3; i++) {
    /* PreparedStartup already owns these names and forwards them to routeCommand through frontendCommand. */
    if (qa_command_buffer_exists(commands, names[i])) continue;
    source_command *handler = calloc(1, sizeof *handler);
    if (handler == NULL) qa_range_error("Out of memory");
    handler->app = app; handler->prepared = prepared; handler->name = names[i];
    if (qa_command_buffer_register(commands, names[i], source_command_handler, handler, NULL, QA_COMMAND_WORLD))
      qa_startup_push_release(app, release_source_command, handler);
    else free(handler);
  }
}
