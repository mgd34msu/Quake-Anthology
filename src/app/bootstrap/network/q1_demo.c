/* Port of src/app/bootstrap/network/q1-demo.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/network/q1_demo.h"
#include "qa/network/common/js_number.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
#include "qa/foundation/js.h"
typedef struct demo_operation {
  bool busy; const char *ended; double frame;
} demo_operation;
static void operation_init(demo_operation *op) {
  op->busy = false; op->ended = NULL; op->frame = -1;
}
/* TS assertCurrent: a RetiredDemoOperation that run() swallows. */
static void operation_assert(void *ctx) {
  demo_operation *op = ctx;
  if (op->ended && strcmp(op->ended, "closed") == 0)
    qa_raise("RetiredDemoOperation", 0, 0, "Demo closed during advancement");
}
static void operation_finish(demo_operation *op, const char *reason) {
  if (!op->ended) op->ended = reason;
}
/* Returns false when the TS run() returns without doing work. */
static bool operation_begin(demo_operation *op, qa_app_q1_demo_frame frame) {
  if (op->busy) qa_raise("Error", 0, 0, "Demo advancement is already in progress");
  if (frame.frame != trunc(frame.frame) || fabs(frame.frame) > 9007199254740991.0
      || frame.frame < 0 || !isfinite(frame.elapsed_seconds) || frame.elapsed_seconds < 0)
    qa_range_error("Invalid demo frame clock");
  if (op->ended || op->frame == frame.frame) return false;
  if (frame.frame < op->frame) qa_range_error("Demo frame number moved backwards");
  op->busy = true; op->frame = frame.frame;
  return true;
}
/* TS run(): try { await work(); } catch (RetiredDemoOperation) {} finally { busy = false; } */
static void operation_run(demo_operation *op, qa_app_q1_demo_frame frame, void (*work)(void *), void *ctx) {
  if (!operation_begin(op, frame)) return;
  qa_caught_error caught;
  bool ok = qa_try(work, ctx, &caught);
  op->busy = false;
  if (!ok && strcmp(caught.name, "RetiredDemoOperation") != 0) qa_rethrow(&caught);
}
static int32_t js_int32(double n) {
  if (!isfinite(n)) return 0;
  double m = fmod(trunc(n), 4294967296.0);
  if (m < 0) m += 4294967296.0;
  return (int32_t)qa_js_to_uint32(m);
}
struct qa_app_netquake_demo_input {
  qa_netquake_demo_reader *reader; qa_app_q1_demo_remote remote;
  qa_netquake_decoder decoder; demo_operation operation;
  double signon, clock; bool primed;
};
qa_app_netquake_demo_input *qa_app_netquake_demo_input_create(
    qa_netquake_demo_reader *reader, qa_app_q1_demo_remote remote) {
  qa_app_netquake_demo_input *in = calloc(1, sizeof *in);
  if (!in) return NULL;
  in->reader = reader; in->remote = remote;
  qa_q1_protocol_identity protocol = {"q1-netquake", 15, 0, 0};
  qa_netquake_decoder_init(&in->decoder, protocol, "known-retail", true);
  operation_init(&in->operation);
  return in;
}
void qa_app_netquake_demo_input_destroy(qa_app_netquake_demo_input *input) {
  if (!input) return;
  qa_netquake_decoder_destroy(&input->decoder);
  free(input);
}
void qa_app_netquake_demo_input_close(qa_app_netquake_demo_input *input) {
  input->operation.ended = "closed";
}
static bool nq_ready(qa_app_netquake_demo_input *in) {
  return in->signon == 4 && in->remote.has_player(in->remote.ctx)
      && in->remote.has_output(in->remote.ctx);
}
typedef struct nq_record_scope { qa_netquake_message *messages; size_t count; uint8_t *bytes; } nq_record_scope;
static void nq_record_scope_free(void *arg) {
  nq_record_scope *scope = arg;
  for (size_t i = 0; i < scope->count; i++) qa_netquake_message_destroy(&scope->messages[i]);
  free(scope->messages); free(scope->bytes);
  memset(scope, 0, sizeof *scope);
}
typedef struct nq_work_scope { qa_app_netquake_demo_input *in; qa_app_q1_demo_frame frame; double records; } nq_work_scope;
static void nq_work(void *ctx) {
  nq_work_scope *work = ctx;
  qa_app_netquake_demo_input *in = work->in;
  qa_app_q1_demo_frame frame = work->frame;
  demo_operation *op = &in->operation;
  if (in->primed) in->clock += frame.elapsed_seconds;
  do {
    if (nq_ready(in) && !frame.timedemo
        && in->clock <= in->remote.recorded_seconds(in->remote.ctx)) break;
    qa_netquake_demo_record record;
    if (!qa_netquake_demo_reader_next(in->reader, &record)) {
      operation_finish(op, "eof");
      break;
    }
    work->records++;
    nq_record_scope scope = { NULL, 0, record.message };
    qa_cleanup guard;
    qa_cleanup_push(&guard, nq_record_scope_free, &scope);
    scope.messages = qa_netquake_decoder_decode(&in->decoder, record.message, record.length, &scope.count);
    qa_netquake_message *messages = scope.messages;
    size_t count = scope.count;
    if (in->reader->forced_track != -1)
      for (size_t i = 0; i < count; i++)
        if (strcmp(messages[i].kind, "cd-track") == 0)
          messages[i].track = (double)(js_int32(in->reader->forced_track) & 255);
    for (size_t i = 0; i < count; i++)
      if (strcmp(messages[i].kind, "server-info") == 0) {
        in->signon = 0; in->primed = false; in->clock = 0;
      }
    in->remote.receive(in->remote.ctx, messages, count, in->clock * 1000,
        operation_assert, op);
    operation_assert(op);
    in->remote.set_demo_view_angles(in->remote.ctx, record.view_angles, true);
    for (size_t i = 0; i < count; i++) {
      const qa_netquake_message *message = &messages[i];
      if (strcmp(message->kind, "signon") == 0) {
        if (message->stage <= in->signon || message->stage > 4)
          qa_raise("Error", 0, 0, "Invalid demo signon stage %s after %s", QA_JS_NUMBER(message->stage), QA_JS_NUMBER(in->signon));
        in->signon = message->stage;
      } else if (strcmp(message->kind, "entity") == 0 && in->signon == 3) in->signon = 4;
      else if (strcmp(message->kind, "disconnect") == 0)
        operation_finish(op, "recorded-disconnect");
    }
    qa_cleanup_pop(&guard);
    nq_record_scope_free(&scope);
    if (!in->primed && nq_ready(in)) {
      in->clock = in->remote.recorded_seconds(in->remote.ctx);
      in->primed = true;
      break;
    }
    if (nq_ready(in) && frame.timedemo) {
      in->clock = in->remote.recorded_seconds(in->remote.ctx);
      break;
    }
  } while (!op->ended);
  operation_assert(op);
  in->remote.sample_demo(in->remote.ctx, in->clock);
}
qa_app_q1_demo_progress qa_app_netquake_demo_input_advance(
    qa_app_netquake_demo_input *in, qa_app_q1_demo_frame frame) {
  nq_work_scope work = { in, frame, 0 };
  operation_run(&in->operation, frame, nq_work, &work);
  demo_operation *op = &in->operation;
  qa_app_q1_demo_progress progress = {in->clock, work.records, NULL, op->ended};
  progress.phase = op->ended ? "ended" : nq_ready(in) ? "active" : "loading";
  return progress;
}
typedef struct command_entry {
  double sequence; qa_quakeworld_demo_record record;
} command_entry;
struct qa_app_quakeworld_demo_input {
  qa_quakeworld_demo_reader *reader; qa_app_qw_demo_remote remote;
  qa_quakeworld_decoder decoder; demo_operation operation;
  bool has_pending; qa_quakeworld_demo_record pending;
  bool has_data; qa_quakeworld_message data;
  char **models; size_t model_count, model_cap;
  char **sounds; size_t sound_count, sound_cap;
  bool model_complete, sound_complete, world_ready;
  double incoming, outgoing, acknowledged, clock;
  command_entry *commands; size_t command_count, command_cap;
};
static char *copy_text(const char *s) {
  size_t n = strlen(s) + 1; char *out = malloc(n);
  if (out) memcpy(out, s, n);
  return out;
}
static void push_name(char ***items, size_t *count, size_t *cap, const char *name) {
  if (*count == *cap) {
    *cap = *cap ? *cap * 2 : 16;
    *items = realloc(*items, *cap * sizeof **items);
    if (!*items) qa_range_error("out of memory");
  }
  (*items)[(*count)++] = copy_text(name);
}
static void clear_names(char ***items, size_t *count) {
  for (size_t i = 0; i < *count; i++) free((*items)[i]);
  *count = 0;
}
qa_app_quakeworld_demo_input *qa_app_quakeworld_demo_input_create(
    qa_quakeworld_demo_reader *reader, qa_app_qw_demo_remote remote) {
  qa_app_quakeworld_demo_input *in = calloc(1, sizeof *in);
  if (!in) return NULL;
  in->reader = reader; in->remote = remote;
  qa_quakeworld_profile profile = {0};
  profile.kind = "q1-quakeworld"; profile.version = 28;
  qa_quakeworld_decoder_init(&in->decoder, profile);
  operation_init(&in->operation);
  in->incoming = -1; in->acknowledged = -1;
  in->has_pending = qa_quakeworld_demo_reader_next(reader, &in->pending) != 0;
  in->clock = in->has_pending ? in->pending.seconds : 0;
  return in;
}
static void clear_commands(qa_app_quakeworld_demo_input *in) {
  for (size_t i = 0; i < in->command_count; i++)
    qa_quakeworld_demo_record_destroy(&in->commands[i].record);
  in->command_count = 0;
}
void qa_app_quakeworld_demo_input_destroy(qa_app_quakeworld_demo_input *input) {
  if (!input) return;
  if (input->has_pending) qa_quakeworld_demo_record_destroy(&input->pending);
  if (input->has_data) qa_quakeworld_message_destroy(&input->data);
  clear_names(&input->models, &input->model_count); free(input->models);
  clear_names(&input->sounds, &input->sound_count); free(input->sounds);
  clear_commands(input); free(input->commands);
  qa_quakeworld_decoder_destroy(&input->decoder);
  free(input);
}
void qa_app_quakeworld_demo_input_close(qa_app_quakeworld_demo_input *input) {
  input->operation.ended = "closed";
}
static bool qw_ready(qa_app_quakeworld_demo_input *in) {
  return in->world_ready && in->remote.has_output(in->remote.ctx)
      && in->remote.has_player(in->remote.ctx);
}
static void replay_commands(qa_app_quakeworld_demo_input *in) {
  if (!qw_ready(in)) return;
  size_t kept = 0;
  for (size_t i = 0; i < in->command_count; i++) {
    command_entry *entry = &in->commands[i];
    if (entry->sequence <= in->acknowledged) {
      qa_quakeworld_demo_record_destroy(&entry->record);
    } else {
      in->remote.prediction_sent(in->remote.ctx, entry->sequence,
          entry->record.command, entry->record.seconds * 1000);
      in->commands[kept++] = *entry;
    }
  }
  in->command_count = kept;
}
static uint32_t read_u32(const uint8_t *bytes) {
  return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8)
      | ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}
typedef struct qw_messages_scope { qa_quakeworld_message *messages; size_t count, moved; } qw_messages_scope;
static void qw_messages_scope_free(void *arg) {
  qw_messages_scope *scope = arg;
  for (size_t i = 0; i < scope->count; i++)
    if (i != scope->moved) qa_quakeworld_message_destroy(&scope->messages[i]);
  free(scope->messages);
  memset(scope, 0, sizeof *scope);
}
static void qw_packet(qa_app_quakeworld_demo_input *in, qa_quakeworld_demo_record *record) {
  demo_operation *op = &in->operation;
  if (record->length < 8) qa_raise("Error", 0, 0, "Truncated QWD netchannel header");
  if ((int32_t)read_u32(record->message) == -1) return;
  double sequence = (double)(read_u32(record->message) & 0x7fffffff);
  double acknowledged = (double)(read_u32(record->message + 4) & 0x7fffffff);
  if (sequence <= in->incoming) return;
  in->incoming = sequence; in->acknowledged = acknowledged;
  in->remote.prediction_acknowledged(in->remote.ctx, acknowledged, record->seconds * 1000);
  qw_messages_scope scope = { NULL, 0, 0 };
  qa_cleanup guard;
  qa_cleanup_push(&guard, qw_messages_scope_free, &scope);
  scope.messages = qa_quakeworld_decoder_decode(
      &in->decoder, record->message + 8, record->length - 8, sequence, &scope.count);
  qa_quakeworld_message *messages = scope.messages;
  size_t count = scope.count;
  scope.moved = count;
  bool disconnect = false;
  for (size_t i = 0; i < count; i++) {
    qa_quakeworld_message *message = &messages[i];
    if (strcmp(message->kind, "server-data") == 0) {
      if (message->player_slot >= 32) qa_raise("Error", 0, 0, "QWD requires a valid native player slot");
      if (in->has_data) qa_quakeworld_message_destroy(&in->data);
      in->data = *message;
      scope.moved = i; /* ownership moved to in->data; the scope skips it */
      in->has_data = true;
      clear_names(&in->models, &in->model_count);
      clear_names(&in->sounds, &in->sound_count);
      in->model_complete = in->sound_complete = in->world_ready = false;
      clear_commands(in);
      in->remote.server_data(in->remote.ctx, &in->data);
      operation_assert(op);
    } else if (strcmp(message->kind, "model-list") == 0
        || strcmp(message->kind, "sound-list") == 0) {
      if (!in->has_data) qa_raise("Error", 0, 0, "QWD precache before serverdata");
      bool models = strcmp(message->kind, "model-list") == 0;
      size_t current = models ? in->model_count : in->sound_count;
      if (message->first != (double)current) qa_raise("Error", 0, 0, "Non-contiguous QWD precache list");
      for (size_t n = 0; n < message->name_count; n++) {
        if (models) push_name(&in->models, &in->model_count, &in->model_cap, message->names[n]);
        else push_name(&in->sounds, &in->sound_count, &in->sound_cap, message->names[n]);
      }
      if (message->next == 0) {
        if (models) in->model_complete = true;
        else in->sound_complete = true;
      }
      if (!in->world_ready && in->model_complete && in->sound_complete) {
        in->remote.game_state(in->remote.ctx, &in->data,
            (const char *const *)in->models, in->model_count,
            (const char *const *)in->sounds, in->sound_count, operation_assert, op);
        operation_assert(op);
        in->world_ready = true;
      }
    }
    if (strcmp(message->kind, "disconnect") == 0) disconnect = true;
  }
  in->remote.receive(in->remote.ctx, messages, count, record->seconds * 1000,
      operation_assert, op);
  operation_assert(op);
  replay_commands(in);
  if (disconnect) operation_finish(op, "recorded-disconnect");
  qa_cleanup_pop(&guard);
  qw_messages_scope_free(&scope);
}
typedef struct qw_work_scope { qa_app_quakeworld_demo_input *in; qa_app_q1_demo_frame frame; double records; } qw_work_scope;
static void free_record_slot(void *arg) { qa_quakeworld_demo_record_destroy(arg); }
static void qw_work(void *ctx) {
  qw_work_scope *work = ctx;
  qa_app_quakeworld_demo_input *in = work->in;
  qa_app_q1_demo_frame frame = work->frame;
  demo_operation *op = &in->operation;
  {
    {
      in->clock += qw_ready(in) && !frame.timedemo ? frame.elapsed_seconds : 0;
      double group = in->has_pending ? in->pending.seconds : in->clock;
      while (in->has_pending && !op->ended) {
        qa_quakeworld_demo_record record = in->pending;
        if (!frame.timedemo && qw_ready(in) && in->clock + 1 < record.seconds)
          in->clock = record.seconds - 1;
        if (frame.timedemo ? record.seconds > group
            : qw_ready(in) && record.seconds > in->clock) {
          in->pending = record;
          break;
        }
        in->has_pending = false;
        if (frame.timedemo || !qw_ready(in)) in->clock = record.seconds;
        work->records++;
        if (strcmp(record.kind, "sequences") == 0) {
          in->outgoing = record.outgoing; in->incoming = record.incoming;
          qa_quakeworld_demo_record_destroy(&record);
        } else if (strcmp(record.kind, "command") == 0) {
          in->remote.set_demo_view_angles(in->remote.ctx, record.view_angles, false);
          /* commands.set(outgoing++, record): an existing key keeps its place. */
          double key = in->outgoing++;
          command_entry *existing = NULL;
          for (size_t c = 0; c < in->command_count; c++)
            if (in->commands[c].sequence == key) existing = &in->commands[c];
          if (existing) {
            qa_quakeworld_demo_record_destroy(&existing->record);
            existing->record = record;
          } else {
            if (in->command_count == in->command_cap) {
              in->command_cap = in->command_cap ? in->command_cap * 2 : 16;
              in->commands = realloc(in->commands, in->command_cap * sizeof *in->commands);
              if (!in->commands) qa_range_error("out of memory");
            }
            in->commands[in->command_count].sequence = key;
            in->commands[in->command_count].record = record;
            in->command_count++;
          }
          while (in->command_count > 64) {
            qa_quakeworld_demo_record_destroy(&in->commands[0].record);
            memmove(in->commands, in->commands + 1,
                (in->command_count - 1) * sizeof *in->commands);
            in->command_count--;
          }
          replay_commands(in);
        } else {
          qa_cleanup record_guard;
          qa_cleanup_push(&record_guard, free_record_slot, &record);
          qw_packet(in, &record);
          qa_cleanup_pop(&record_guard);
          qa_quakeworld_demo_record_destroy(&record);
        }
        operation_assert(op);
        if (!op->ended)
          in->has_pending = qa_quakeworld_demo_reader_next(in->reader, &in->pending) != 0;
      }
      if (!in->has_pending) operation_finish(op, "eof");
      in->remote.sample_demo(in->remote.ctx, in->clock);
      in->remote.sample_presentation(in->remote.ctx, in->clock * 1000);
    }
  }
}
qa_app_q1_demo_progress qa_app_quakeworld_demo_input_advance(
    qa_app_quakeworld_demo_input *in, qa_app_q1_demo_frame frame) {
  qw_work_scope work = { in, frame, 0 };
  operation_run(&in->operation, frame, qw_work, &work);
  demo_operation *op = &in->operation;
  qa_app_q1_demo_progress progress = {in->clock, work.records, NULL, op->ended};
  progress.phase = op->ended ? "ended" : qw_ready(in) ? "active" : "loading";
  return progress;
}
