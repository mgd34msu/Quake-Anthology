/* Port of src/app/bootstrap/demo-recording.ts. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "qa/app/bootstrap/demo_recording.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include "qa/content/mounts/paths.h"
#include "qa/network/q2/demo.h"
#include "qa/network/q2/mvd_recording.h"
#include "qa/network/q3/demo.h"
struct qa_app_demo_recording {
  char *path;
  qa_app_demo_recording_identity identity;
  FILE *file;
  bool accepting, finishing;
  const char *failure;
  qa_vec3 angles;
  double seconds;
};
static bool ends_ci(const char *text, const char *suffix) {
  size_t t = strlen(text), s = strlen(suffix);
  if (t < s) return false;
  for (size_t i = 0; i < s; i++) {
    char a = text[t - s + i], b = suffix[i];
    if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
    if (a != b) return false;
  }
  return true;
}
char *qa_app_recording_path(const char *root, const char *name, qa_app_demo_recording_identity identity) {
  char *normalized = qa_normalize_resource_path(name);
  const char *extension = identity.kind == QA_APP_RECORDING_MVD ? ".mvd" : identity.kind == QA_APP_RECORDING_Q1 ? ".dem"
      : identity.kind == QA_APP_RECORDING_QW ? ".qwd"
      : identity.kind == QA_APP_RECORDING_Q2 || identity.kind == QA_APP_RECORDING_Q2_SERVER ? ".dm2" : ".dm_68";
  bool has = ends_ci(normalized, extension);
  bool prefix = (identity.kind == QA_APP_RECORDING_MVD || identity.kind == QA_APP_RECORDING_Q2_SERVER ||
      identity.kind == QA_APP_RECORDING_Q2 || identity.kind == QA_APP_RECORDING_Q3) &&
      !(strlen(normalized) >= 6 && strncmp(normalized, "demos/", 6) == 0);
  size_t cap = strlen(root) + strlen(normalized) + 16;
  char *path = malloc(cap);
  if (path) snprintf(path, cap, "%s/%s%s%s", root, prefix ? "demos/" : "", normalized, has ? "" : extension);
  free(normalized);
  return path;
}
static void mkdir_parents(const char *path) {
  char buf[4096];
  snprintf(buf, sizeof buf, "%s", path);
  for (char *p = buf + 1; *p; p++) if (*p == '/') { *p = '\0'; mkdir(buf, 0777); *p = '/'; }
}
static const char *write_bytes(qa_app_demo_recording *r, const uint8_t *bytes, size_t length) {
  if (r->failure) return r->failure;
  size_t offset = 0;
  while (offset < length) {
    size_t written = fwrite(bytes + offset, 1, length - offset, r->file);
    if (written == 0) { r->failure = "Recording write made no progress"; return r->failure; }
    offset += written;
  }
  return NULL;
}
qa_app_demo_recording *qa_app_demo_recording_open(const char *root, const char *name, qa_app_demo_recording_seed seed, bool replace,
                                                  const char **out_error) {
  *out_error = NULL;
  if (seed.packet_count == 0) { *out_error = "Recording requires matching initial source state"; return NULL; }
  for (size_t i = 0; i < seed.packet_count; i++)
    if (seed.packets[i].kind != seed.identity.kind) { *out_error = "Recording requires matching initial source state"; return NULL; }
  qa_app_demo_recording *r = calloc(1, sizeof *r);
  if (!r) { *out_error = "out of memory"; return NULL; }
  r->path = qa_app_recording_path(root, name, seed.identity);
  r->identity = seed.identity;
  r->accepting = true;
  mkdir_parents(r->path);
  /* A name the user typed replaces an existing demo like the original clients; generated names must not collide. */
  r->file = fopen(r->path, replace ? "wb" : "wxb");
  if (!r->file) {
    *out_error = errno == EEXIST ? "EEXIST: recording file already exists" : "could not open recording file";
    free(r->path); free(r);
    return NULL;
  }
  const char *error = NULL;
  if (seed.identity.kind == QA_APP_RECORDING_MVD) {
    size_t len = 0; uint8_t *magic = qa_q2_mvd_magic(&len);
    error = write_bytes(r, magic, len); free(magic);
  }
  if (!error && seed.identity.kind == QA_APP_RECORDING_Q1) {
    size_t len = 0; uint8_t *header = qa_write_netquake_demo_header(seed.identity.track, &len);
    error = write_bytes(r, header, len); free(header);
  }
  for (size_t i = 0; !error && i < seed.packet_count; i++) error = qa_app_demo_recording_append(r, seed.packets[i]);
  if (error) {
    qa_app_demo_recording_abort(r);
    /* A recording that never got its seed is a truncated file, not a demo; the write failure is the error reported. */
    (void)unlink(r->path);
    *out_error = error;
    qa_app_demo_recording_destroy(r);
    return NULL;
  }
  return r;
}
const char *qa_app_demo_recording_path(const qa_app_demo_recording *r) { return r->path; }
qa_app_demo_recording_identity qa_app_demo_recording_identity_of(const qa_app_demo_recording *r) { return r->identity; }
static const char *sink_append(void *ctx, qa_app_demo_recording_packet packet) {
  return qa_app_demo_recording_append(ctx, packet);
}
qa_app_demo_recording_sink qa_app_demo_recording_sink_of(qa_app_demo_recording *r) {
  return (qa_app_demo_recording_sink){r, sink_append};
}
const char *qa_app_demo_recording_append(qa_app_demo_recording *r, qa_app_demo_recording_packet packet) {
  if (!r->accepting) return "Recording is stopped";
  if (packet.kind != r->identity.kind) return "Recording source protocol changed";
  uint8_t *bytes = NULL;
  size_t length = 0;
  switch (packet.kind) {
    case QA_APP_RECORDING_MVD: bytes = qa_q2_frame_mvd_message(packet.message, packet.message_length, &length); break;
    case QA_APP_RECORDING_Q1: {
      r->angles = packet.view_angles;
      qa_netquake_demo_record record = {packet.view_angles, (uint8_t *)packet.message, packet.message_length};
      bytes = qa_write_netquake_demo_record(&record, &length);
      break;
    }
    case QA_APP_RECORDING_QW:
      r->seconds = packet.record.seconds;
      bytes = qa_write_quakeworld_demo_record(&packet.record, &length);
      break;
    case QA_APP_RECORDING_Q2_SERVER:
    case QA_APP_RECORDING_Q2: bytes = qa_q2_write_demo_record(packet.message, packet.message_length, &length); break;
    case QA_APP_RECORDING_Q3: {
      qa_q3_demo_message message = {(int64_t)packet.sequence, packet.message, packet.message_length};
      if (!qa_q3_encode_demo_message(&message, &bytes, &length)) return "out of memory";
      break;
    }
  }
  const char *error = write_bytes(r, bytes, length);
  free(bytes);
  return error;
}
const char *qa_app_demo_recording_stop(qa_app_demo_recording *r) {
  if (r->finishing) return r->failure;
  r->finishing = true;
  r->accepting = false;
  uint8_t *footer = NULL;
  size_t length = 0;
  static const uint8_t mvd_footer[2] = {0, 0};
  const uint8_t *bytes = NULL;
  switch (r->identity.kind) {
    case QA_APP_RECORDING_Q2_SERVER: length = 0; break;
    case QA_APP_RECORDING_MVD: bytes = mvd_footer; length = 2; break;
    case QA_APP_RECORDING_Q1: {
      uint8_t end = 2;
      qa_netquake_demo_record record = {r->angles, &end, 1};
      footer = qa_write_netquake_demo_record(&record, &length);
      bytes = footer;
      break;
    }
    case QA_APP_RECORDING_QW: {
      static const uint8_t tail[] = {255, 255, 255, 255, 2, 'E', 'n', 'd', 'O', 'f', 'D', 'e', 'm', 'o', 0};
      qa_quakeworld_demo_record record = {0};
      record.kind = "packet"; record.seconds = r->seconds;
      record.message = (uint8_t *)tail; record.length = sizeof tail;
      footer = qa_write_quakeworld_demo_record(&record, &length);
      bytes = footer;
      break;
    }
    case QA_APP_RECORDING_Q2: footer = qa_q2_finish_demo(&length); bytes = footer; break;
    case QA_APP_RECORDING_Q3: qa_q3_finish_demo(&footer, &length); bytes = footer; break;
  }
  const char *error = write_bytes(r, bytes, length);
  free(footer);
  if (r->file) { fflush(r->file); fclose(r->file); r->file = NULL; }
  return error;
}
const char *qa_app_demo_recording_abort(qa_app_demo_recording *r) {
  if (r->finishing) return r->failure;
  r->finishing = true;
  r->accepting = false;
  if (r->file) { fclose(r->file); r->file = NULL; }
  return r->failure;
}
void qa_app_demo_recording_destroy(qa_app_demo_recording *r) {
  if (!r) return;
  if (r->file) fclose(r->file);
  free(r->path);
  free(r);
}
