/* Port of src/app/bootstrap/network/qw-downloads.ts.
 * GPL-2.0-or-later. */
#include "qa/app/bootstrap/network/qw_downloads.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
#include "qa/network/services/downloads.h"
typedef struct transfer {
  qa_app_qw_download_category category; char *path; const char *root;
  bool has_sink; qa_download_sink sink; double percent;
} transfer;
struct qa_app_qw_download_receiver {
  qa_app_qw_download_options options;
  transfer *transfer; double generation; bool waiting_block;
  bool has_paused; char *paused_path; qa_app_qw_download_category paused_category;
  bool resume_requested;
};
static char *copy_text(const char *s) {
  size_t n = strlen(s) + 1; char *out = malloc(n);
  if (out) memcpy(out, s, n);
  return out;
}
qa_app_qw_download_receiver *qa_app_qw_download_receiver_create(qa_app_qw_download_options options) {
  qa_app_qw_download_receiver *r = calloc(1, sizeof *r);
  if (!r) return NULL;
  r->options = options;
  return r;
}
void qa_app_qw_download_receiver_destroy(qa_app_qw_download_receiver *receiver) {
  if (!receiver) return;
  qa_app_qw_download_receiver_close(receiver);
  free(receiver);
}
void qa_app_qw_download_receiver_close(qa_app_qw_download_receiver *receiver) {
  receiver->has_paused = false; free(receiver->paused_path); receiver->paused_path = NULL;
  receiver->resume_requested = false; receiver->waiting_block = false;
  receiver->generation++;
  if (receiver->transfer) {
    if (receiver->transfer->has_sink) {
      qa_download_sink_close(&receiver->transfer->sink);
      qa_download_sink_destroy(&receiver->transfer->sink);
    }
    free(receiver->transfer->path); free(receiver->transfer); receiver->transfer = NULL;
  }
}
void qa_app_qw_download_receiver_cancel(qa_app_qw_download_receiver *receiver) {
  if (receiver->has_paused) return;
  transfer *t = receiver->transfer; bool waiting = receiver->waiting_block;
  char *path = t ? copy_text(t->path) : NULL;
  qa_app_qw_download_category category = t ? t->category : QA_APP_QW_DL_SOUND;
  qa_app_qw_download_receiver_close(receiver);
  if (t) {
    receiver->has_paused = true; receiver->paused_path = path;
    receiver->paused_category = category; receiver->waiting_block = waiting;
  } else free(path);
}
qa_app_qw_download_request_result qa_app_qw_download_receiver_retry(qa_app_qw_download_receiver *receiver) {
  if (!receiver->has_paused) return QA_APP_QW_DL_SKIPPED;
  if (receiver->waiting_block) { receiver->resume_requested = true; return QA_APP_QW_DL_WAITING; }
  char *path = receiver->paused_path;
  qa_app_qw_download_category category = receiver->paused_category;
  receiver->has_paused = false; receiver->paused_path = NULL; receiver->resume_requested = false;
  qa_cleanup guard;
  qa_cleanup_push(&guard, free, path);
  qa_app_qw_download_request_result result =
      qa_app_qw_download_receiver_request(receiver, path, category);
  qa_cleanup_pop(&guard);
  free(path);
  return result;
}
size_t qa_app_qw_download_receiver_progress(qa_app_qw_download_receiver *receiver,
    qa_app_client_download_progress *out) {
  transfer *t = receiver->transfer;
  if (!t) return 0;
  out->path = t->path; out->transport = QA_APP_DL_NATIVE;
  out->received = t->has_sink ? qa_download_sink_byte_length(&t->sink) : 0;
  out->has_total = false; out->total = 0;
  out->has_percent = true; out->percent = t->percent;
  out->phase = QA_APP_DL_RUNNING;
  return 1;
}
static bool path_charset_ok(const char *path) {
  if (!*path) return false;
  for (const char *p = path; *p; p++) {
    char c = *p;
    bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
        || c == '_' || c == '+' || c == '.' || c == '/' || c == '-';
    if (!ok) return false;
  }
  return true;
}
static void print_message(qa_app_qw_download_receiver *r, const char *prefix,
    const char *path, const char *suffix) {
  size_t n = strlen(prefix) + strlen(path) + strlen(suffix) + 1;
  char *text = malloc(n);
  if (!text) qa_range_error("out of memory");
  snprintf(text, n, "%s%s%s", prefix, path, suffix);
  r->options.print(r->options.ctx, text);
  free(text);
}
qa_app_qw_download_request_result qa_app_qw_download_receiver_request(
    qa_app_qw_download_receiver *receiver, const char *path, qa_app_qw_download_category category) {
  if (receiver->transfer) qa_raise("Error", 0, 0, "QuakeWorld download already active");
  if (category == QA_APP_QW_DL_MODEL && path[0] == '*') return QA_APP_QW_DL_SKIPPED;
  size_t part_count = 0;
  char **parts = qa_download_path(path, &part_count);
  qa_contained_file_parts_free(parts, part_count);
  bool skin = category == QA_APP_QW_DL_SKIN, sound = category == QA_APP_QW_DL_SOUND,
      model = category == QA_APP_QW_DL_MODEL;
  if (!path_charset_ok(path) || !strchr(path, '/') || strstr(path, "..") != NULL
      || (skin && strncmp(path, "skins/", 6) != 0)
      || (sound && strncmp(path, "sound/", 6) != 0)
      || (model && (strncmp(path, "skins/", 6) == 0 || strncmp(path, "sound/", 6) == 0)))
    qa_raise("Error", 0, 0, "Invalid QuakeWorld download path or category");
  double generation = receiver->generation;
  if (receiver->options.exists(receiver->options.ctx, path, category))
    return generation == receiver->generation ? QA_APP_QW_DL_AVAILABLE : QA_APP_QW_DL_SKIPPED;
  if (generation != receiver->generation) return QA_APP_QW_DL_SKIPPED;
  if (receiver->options.permission) {
    qa_app_client_download_category permission_category = skin ? QA_APP_DL_PLAYER
        : strncmp(path, "maps/", 5) == 0 ? QA_APP_DL_MAP
        : sound ? QA_APP_DL_SOUND : QA_APP_DL_MODEL;
    if (!qa_app_client_download_permitted(receiver->options.permission,
            QA_APP_DL_NATIVE, permission_category)) {
      print_message(receiver, "Skipping QuakeWorld download ", path, ": local permission\n");
      return QA_APP_QW_DL_SKIPPED;
    }
  }
  if (receiver->options.demo_recording(receiver->options.ctx)
      || receiver->options.demo_playback(receiver->options.ctx)
      || (skin && receiver->options.noskins(receiver->options.ctx) != 0)) {
    print_message(receiver, "Skipping QuakeWorld download ", path, ": demo or skin policy\n");
    return QA_APP_QW_DL_SKIPPED;
  }
  transfer *t = calloc(1, sizeof *t);
  if (!t) qa_range_error("out of memory");
  t->category = category; t->path = copy_text(path);
  t->root = skin ? receiver->options.skin_root : receiver->options.game_root;
  receiver->transfer = t; receiver->waiting_block = true;
  size_t n = strlen(path) + 10;
  char *command = malloc(n);
  if (!command) qa_range_error("out of memory");
  snprintf(command, n, "download %s", path);
  receiver->options.send_command(receiver->options.ctx, command);
  free(command);
  return QA_APP_QW_DL_WAITING;
}
typedef struct sink_scope {
  qa_app_qw_download_receiver *receiver; transfer *t; const uint8_t *bytes; size_t length; double percent;
  qa_app_qw_download_receive_result outcome;
} sink_scope;
static void sink_body(void *ctx) {
  sink_scope *scope = ctx;
  qa_app_qw_download_receiver *receiver = scope->receiver;
  transfer *t = scope->t;
  if (!t->has_sink) {
    qa_download_expectation expected = {true, NULL, 0,
        receiver->options.has_maximum_bytes ? receiver->options.maximum_bytes : 64 * 1024 * 1024};
    if (!qa_download_sink_create(&t->sink, t->root, t->path, expected)) qa_raise("Error", 0, 0, "out of memory");
    t->has_sink = true;
  }
  qa_download_sink_append(&t->sink, scope->bytes, scope->length);
  t->percent = scope->percent;
  if (scope->percent != 100) {
    receiver->waiting_block = true;
    receiver->options.send_command(receiver->options.ctx, "nextdl");
    scope->outcome = QA_APP_QW_DL_RX_WAITING;
    return;
  }
  char *digest = qa_download_sink_finish(&t->sink);
  free(digest);
  qa_download_sink_destroy(&t->sink); t->has_sink = false;
  free(t->path); free(t); receiver->transfer = NULL;
  scope->outcome = QA_APP_QW_DL_RX_COMPLETE;
}
qa_app_qw_download_receive_result qa_app_qw_download_receiver_receive(
    qa_app_qw_download_receiver *receiver, qa_app_qw_download_result result) {
  if (receiver->options.demo_playback(receiver->options.ctx)) {
    qa_app_qw_download_receiver_close(receiver);
    return QA_APP_QW_DL_RX_WAITING;
  }
  receiver->waiting_block = false;
  if (receiver->has_paused) {
    if (receiver->resume_requested) {
      qa_app_qw_download_request_result resumed = qa_app_qw_download_receiver_retry(receiver);
      return resumed == QA_APP_QW_DL_WAITING ? QA_APP_QW_DL_RX_WAITING
          : resumed == QA_APP_QW_DL_AVAILABLE ? QA_APP_QW_DL_RX_COMPLETE : QA_APP_QW_DL_RX_MISSING;
    }
    return QA_APP_QW_DL_RX_WAITING;
  }
  transfer *t = receiver->transfer;
  if (!t) qa_raise("Error", 0, 0, "Unsolicited QuakeWorld download");
  if (result.missing) {
    print_message(receiver, "QuakeWorld file not found: ", t->path, "\n");
    qa_app_qw_download_receiver_close(receiver);
    return QA_APP_QW_DL_RX_MISSING;
  }
  if (result.percent != trunc(result.percent) || result.percent < t->percent
      || result.percent > 100 || result.length > 768) {
    qa_app_qw_download_receiver_close(receiver);
    qa_raise("Error", 0, 0, "Invalid QuakeWorld download block");
  }
  if (receiver->options.permission) {
    qa_app_client_download_category permission_category =
        strncmp(t->path, "skins/", 6) == 0 ? QA_APP_DL_PLAYER
        : strncmp(t->path, "sound/", 6) == 0 ? QA_APP_DL_SOUND
        : strncmp(t->path, "maps/", 5) == 0 ? QA_APP_DL_MAP : QA_APP_DL_MODEL;
    if (!qa_app_client_download_permitted(receiver->options.permission,
            QA_APP_DL_NATIVE, permission_category)) {
      qa_app_qw_download_receiver_close(receiver);
      return QA_APP_QW_DL_RX_MISSING;
    }
  }
  /* TS: try { sink ??= create; append; ...; finish } catch { close(); print; return 'missing' } */
  sink_scope scope = { receiver, t, result.bytes, result.length, result.percent, QA_APP_QW_DL_RX_COMPLETE };
  qa_caught_error caught;
  if (!qa_try(sink_body, &scope, &caught)) {
    char *path = copy_text(t->path);
    qa_app_qw_download_receiver_close(receiver);
    size_t total = strlen(path) + strlen(caught.message) + 4;
    char *detail = malloc(total);
    if (detail) snprintf(detail, total, "%s: %s", path, caught.message);
    print_message(receiver, "QuakeWorld download failed for ", detail ? detail : path, "\n");
    free(detail); free(path);
    return QA_APP_QW_DL_RX_MISSING;
  }
  return scope.outcome;
}

