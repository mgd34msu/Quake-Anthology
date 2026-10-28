/* Port of src/app/bootstrap/demo-recording-commands.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/demo_recording_commands.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct {
  qa_app_demo_recording *recording;
  qa_app_recording_feed feed;
  const void *token;
  bool reconnect;
} current_t;
struct qa_app_client_demo_recording {
  qa_app_demo_recording_host host;
  current_t *current, *server;
  bool busy;
};
typedef struct { qa_app_client_demo_recording *owner; qa_app_demo_recording *recording; } sink_ctx;
static const char *sink_append(void *ctx, qa_app_demo_recording_packet packet) {
  sink_ctx *sink = ctx;
  const char *error = qa_app_demo_recording_append(sink->recording, packet);
  if (!error) return NULL;
  qa_app_client_demo_recording *owner = sink->owner;
  if (!owner->current || owner->current->recording != sink->recording) return NULL;
  current_t *current = owner->current;
  owner->current = NULL;
  if (current->reconnect) owner->host.reconnect_detach(owner->host.ctx, current->token);
  else current->feed.detach(current->feed.ctx, current->token);
  qa_app_demo_recording_abort(sink->recording);
  char text[1024];
  snprintf(text, sizeof text, "Demo recording failed: %s\n", error);
  owner->host.print(owner->host.ctx, text);
  free(current);
  return NULL;
}
static qa_app_demo_recording_sink make_sink(qa_app_client_demo_recording *owner, qa_app_demo_recording *recording, sink_ctx *storage) {
  *storage = (sink_ctx){owner, recording};
  return (qa_app_demo_recording_sink){storage, sink_append};
}
static const char *finish_current(qa_app_client_demo_recording *owner) {
  current_t *current = owner->current;
  if (!current) return NULL;
  owner->current = NULL;
  if (current->reconnect) owner->host.reconnect_detach(owner->host.ctx, current->token);
  else current->feed.detach(current->feed.ctx, current->token);
  const char *error = qa_app_demo_recording_stop(current->recording);
  char text[8192];
  snprintf(text, sizeof text, "Completed demo %s\n", qa_app_demo_recording_path(current->recording));
  owner->host.print(owner->host.ctx, text);
  qa_app_demo_recording_destroy(current->recording);
  free(current);
  return error;
}
static qa_app_demo_recording *open_recording(qa_app_client_demo_recording *owner, const char *name,
                                             qa_app_demo_recording_seed seed, const char **out_error) {
  if (name) return qa_app_demo_recording_open(owner->host.root(owner->host.ctx), name, seed, out_error);
  for (int index = 0; index < 10000; index++) {
    char generated[16];
    snprintf(generated, sizeof generated, "demo%04d", index);
    qa_app_demo_recording *recording = qa_app_demo_recording_open(owner->host.root(owner->host.ctx), generated, seed, out_error);
    if (recording) return recording;
    if (!*out_error || strstr(*out_error, "EEXIST") == NULL) return NULL;
  }
  *out_error = "No unused demo0000–demo9999 recording name remains";
  return NULL;
}
const char *qa_app_client_demo_recording_start(qa_app_client_demo_recording *owner, const char *name, qa_command_context source) {
  if (owner->busy) return "Recording operation is already in progress";
  owner->busy = true;
  qa_app_demo_recording_seed seed = {0};
  const char *error = owner->host.main_feed.seed(owner->host.main_feed.ctx, source, &seed);
  if (!error && !name && seed.identity.kind != QA_APP_RECORDING_Q3) error = "Usage: record <name>";
  if (!error) error = finish_current(owner);
  qa_app_demo_recording *recording = NULL;
  if (!error) recording = open_recording(owner, name, seed, &error);
  if (!error) {
    sink_ctx *storage = malloc(sizeof *storage);
    current_t *current = calloc(1, sizeof *current);
    current->recording = recording;
    current->feed = owner->host.main_feed;
    current->token = owner->host.main_feed.attach(owner->host.main_feed.ctx, make_sink(owner, recording, storage));
    owner->current = current;
    char text[8192];
    snprintf(text, sizeof text, "Recording %s\n", qa_app_demo_recording_path(recording));
    owner->host.print(owner->host.ctx, text);
  }
  owner->busy = false;
  return error;
}
const char *qa_app_client_demo_recording_start_mvd(qa_app_client_demo_recording *owner, const char *name, qa_command_context source) {
  if (owner->busy) return "Recording operation is already in progress";
  if (!owner->host.has_mvd_recording) return "mvdrecord requires a hosted Quake II multiview source";
  owner->busy = true;
  qa_app_demo_recording_seed seed = {0};
  const char *error = owner->host.mvd_recording.seed(owner->host.mvd_recording.ctx, source, &seed);
  if (!error && seed.identity.kind != QA_APP_RECORDING_MVD) error = "Multiview source supplied a different recording format";
  if (!error) error = finish_current(owner);
  qa_app_demo_recording *recording = NULL;
  if (!error) recording = open_recording(owner, name, seed, &error);
  if (!error) {
    sink_ctx *storage = malloc(sizeof *storage);
    current_t *current = calloc(1, sizeof *current);
    current->recording = recording;
    current->feed = owner->host.mvd_recording;
    current->token = owner->host.mvd_recording.attach(owner->host.mvd_recording.ctx, make_sink(owner, recording, storage));
    owner->current = current;
    char text[8192];
    snprintf(text, sizeof text, "Recording %s\n", qa_app_demo_recording_path(recording));
    owner->host.print(owner->host.ctx, text);
  }
  owner->busy = false;
  return error;
}
const char *qa_app_client_demo_recording_start_server(qa_app_client_demo_recording *owner, const char *name, qa_command_context source) {
  if (owner->busy) return "Recording operation is already in progress";
  if (owner->server) return "Already doing a serverrecord";
  if (!owner->host.has_server_recording) return "serverrecord requires a hosted classic Quake II source";
  owner->busy = true;
  qa_app_demo_recording_seed seed = {0};
  const char *error = owner->host.server_recording.seed(owner->host.server_recording.ctx, source, &seed);
  if (!error && seed.identity.kind != QA_APP_RECORDING_Q2_SERVER) error = "Server source supplied a different recording format";
  qa_app_demo_recording *recording = NULL;
  if (!error) recording = open_recording(owner, name, seed, &error);
  if (!error) {
    current_t *current = calloc(1, sizeof *current);
    current->recording = recording;
    current->feed = owner->host.server_recording;
    current->token = owner->host.server_recording.attach(owner->host.server_recording.ctx, qa_app_demo_recording_sink_of(recording));
    owner->server = current;
    char text[8192];
    snprintf(text, sizeof text, "Recording server footage %s\n", qa_app_demo_recording_path(recording));
    owner->host.print(owner->host.ctx, text);
  }
  owner->busy = false;
  return error;
}
const char *qa_app_client_demo_recording_stop_server(qa_app_client_demo_recording *owner) {
  if (owner->busy) return "Recording operation is already in progress";
  owner->busy = true;
  current_t *current = owner->server;
  const char *error = NULL;
  if (!current) owner->host.print(owner->host.ctx, "Not doing a serverrecord.\n");
  else {
    owner->server = NULL;
    current->feed.detach(current->feed.ctx, current->token);
    error = qa_app_demo_recording_stop(current->recording);
    char text[8192];
    snprintf(text, sizeof text, "Completed server footage %s\n", qa_app_demo_recording_path(current->recording));
    owner->host.print(owner->host.ctx, text);
    qa_app_demo_recording_destroy(current->recording);
    free(current);
  }
  owner->busy = false;
  return error;
}
const char *qa_app_client_demo_recording_stop(qa_app_client_demo_recording *owner) {
  if (owner->busy) return "Recording operation is already in progress";
  owner->busy = true;
  const char *error = finish_current(owner);
  owner->busy = false;
  return error;
}
const char *qa_app_client_demo_recording_stop_all(qa_app_client_demo_recording *owner) {
  const char *error = qa_app_client_demo_recording_stop(owner);
  if (owner->server) {
    const char *server_error = qa_app_client_demo_recording_stop_server(owner);
    if (!error) error = server_error;
  }
  return error;
}
const char *qa_app_client_demo_recording_rerecord(qa_app_client_demo_recording *owner, const char *name, qa_command_context source) {
  if (owner->busy) return "Recording operation is already in progress";
  owner->busy = true;
  qa_app_demo_recording_seed seed = {0};
  const char *error = owner->host.main_feed.seed(owner->host.main_feed.ctx, source, &seed);
  if (!error && seed.identity.kind != QA_APP_RECORDING_QW) error = "rerecord requires a QuakeWorld server";
  if (!error) error = finish_current(owner);
  qa_app_demo_recording *recording = NULL;
  if (!error) {
    qa_app_demo_recording_packet packet = {0};
    packet.kind = QA_APP_RECORDING_QW;
    packet.record.kind = "sequences";
    qa_app_demo_recording_seed fresh = {seed.identity, &packet, 1};
    recording = qa_app_demo_recording_open(owner->host.root(owner->host.ctx), name, fresh, &error);
  }
  if (!error) {
    sink_ctx *storage = malloc(sizeof *storage);
    const void *token = owner->host.reconnect_recording(owner->host.ctx, source, make_sink(owner, recording, storage), &error);
    if (!error) {
      current_t *current = calloc(1, sizeof *current);
      current->recording = recording;
      current->token = token;
      current->reconnect = true;
      owner->current = current;
      char text[8192];
      snprintf(text, sizeof text, "Recording %s\n", qa_app_demo_recording_path(recording));
      owner->host.print(owner->host.ctx, text);
    } else qa_app_demo_recording_abort(recording);
  }
  owner->busy = false;
  return error;
}
const char *qa_app_client_demo_recording_stop_mvd(qa_app_client_demo_recording *owner) {
  if (!owner->current || qa_app_demo_recording_identity_of(owner->current->recording).kind != QA_APP_RECORDING_MVD) {
    owner->host.print(owner->host.ctx, "Not recording a multiview demo.\n");
    return NULL;
  }
  return qa_app_client_demo_recording_stop(owner);
}
typedef struct { qa_app_client_demo_recording *owner; const char *name; } handler_ctx;
typedef struct { qa_command_buffer *buffer; const char *names[8]; handler_ctx *ctxs[8]; size_t count; } cmd_binding;
static void record_handler(void *ctx, qa_command_invocation *command) {
  handler_ctx *handler = ctx;
  qa_app_client_demo_recording *owner = handler->owner;
  const char *name = handler->name;
  const qa_command_origin *origin = &command->source.origin;
  while (origin->kind == QA_COMMAND_ORIGIN_SCRIPT && origin->caller) origin = origin->caller;
  if (origin->kind == QA_COMMAND_ORIGIN_REMOTE_CLIENT) {
    char text[256];
    snprintf(text, sizeof text, "%s is a local recording command.\n", name);
    owner->host.print(owner->host.ctx, text);
    return;
  }
  size_t count = command->argc > 0 ? command->argc - 1 : 0;
  const char *arg0 = count > 0 ? command->argv[1] : NULL;
  if (strcmp(name, "record") == 0) {
    if (count > 1) { owner->host.print(owner->host.ctx, "Usage: record [name]\n"); return; }
    owner->host.stage(owner->host.ctx, (qa_app_demo_recording_intent){"record", arg0, command->source});
  } else if (strcmp(name, "rerecord") == 0 || strcmp(name, "mvdrecord") == 0 || strcmp(name, "serverrecord") == 0) {
    if (!arg0 || count != 1) {
      char text[128];
      snprintf(text, sizeof text, "Usage: %s <name>\n", name);
      owner->host.print(owner->host.ctx, text);
      return;
    }
    owner->host.stage(owner->host.ctx, (qa_app_demo_recording_intent){name, arg0, command->source});
  } else if (count != 0) {
    char text[128];
    snprintf(text, sizeof text, "Usage: %s\n", name);
    owner->host.print(owner->host.ctx, text);
  } else {
    const char *kind = strcmp(name, "serverstop") == 0 ? "serverstop" : strcmp(name, "mvdstop") == 0 ? "mvdstop" : "stop";
    owner->host.stage(owner->host.ctx, (qa_app_demo_recording_intent){kind, NULL, command->source});
  }
}
void *qa_app_client_demo_recording_attach(qa_app_client_demo_recording *owner, qa_command_buffer *commands) {
  cmd_binding *bind = calloc(1, sizeof *bind);
  bind->buffer = commands;
  static const char *const names[] = {"record", "rerecord", "stop", "stoprecord", "mvdrecord", "mvdstop", "serverrecord", "serverstop"};
  for (size_t i = 0; i < 8; i++) {
    const char *name = names[i];
    bool starts = strcmp(name, "record") == 0 || strcmp(name, "rerecord") == 0 || strcmp(name, "mvdrecord") == 0 || strcmp(name, "serverrecord") == 0;
    const char *summary = strcmp(name, "serverrecord") == 0 ? "Record native Quake II server entity footage."
        : strcmp(name, "mvdrecord") == 0 ? "Record the hosted Quake II world as a native multiview demo."
        : strcmp(name, "rerecord") == 0 ? "Reconnect to the QuakeWorld server and record its signon."
        : strcmp(name, "record") == 0 ? "Record the current session to a demo." : "Finish the current demo recording.";
    char usage[64], example[64];
    snprintf(usage, sizeof usage, "%s", strcmp(name, "record") == 0 ? "record [name]" : starts ? "" : name);
    if (starts && strcmp(name, "record") != 0) snprintf(usage, sizeof usage, "%s <name>", name);
    snprintf(example, sizeof example, "%s", starts ? "" : name);
    if (starts) snprintf(example, sizeof example, "%s session1", name);
    handler_ctx *ctx = malloc(sizeof *ctx);
    *ctx = (handler_ctx){owner, name};
    const char *examples[1] = {example};
    qa_command_documentation doc = {summary, usage, examples, 1, NULL, 0};
    if (qa_command_buffer_register(commands, name, record_handler, ctx, &doc, QA_COMMAND_WORLD)) {
      bind->names[bind->count] = name;
      bind->ctxs[bind->count++] = ctx;
    } else {
      char text[256];
      snprintf(text, sizeof text, "Recording command %s is already owned by another command.\n", name);
      owner->host.print(owner->host.ctx, text);
      free(ctx);
    }
  }
  return bind;
}
void qa_app_client_demo_recording_detach_commands(qa_app_client_demo_recording *owner, void *handle) {
  (void)owner;
  cmd_binding *bind = handle;
  for (size_t i = 0; i < bind->count; i++) {
    qa_command_buffer_unregister(bind->buffer, bind->names[i], record_handler, bind->ctxs[i], true);
    free(bind->ctxs[i]);
  }
  free(bind);
}
qa_app_client_demo_recording *qa_app_client_demo_recording_create(qa_app_demo_recording_host host) {
  qa_app_client_demo_recording *owner = calloc(1, sizeof *owner);
  if (!owner) return NULL;
  owner->host = host;
  return owner;
}
void qa_app_client_demo_recording_destroy(qa_app_client_demo_recording *owner) {
  if (!owner) return;
  if (owner->current) { qa_app_demo_recording_destroy(owner->current->recording); free(owner->current); }
  if (owner->server) { qa_app_demo_recording_destroy(owner->server->recording); free(owner->server); }
  free(owner);
}
const char *qa_app_client_demo_recording_path(const qa_app_client_demo_recording *owner) {
  return owner->current ? qa_app_demo_recording_path(owner->current->recording) : NULL;
}
const char *qa_app_client_demo_recording_server_path(const qa_app_client_demo_recording *owner) {
  return owner->server ? qa_app_demo_recording_path(owner->server->recording) : NULL;
}
