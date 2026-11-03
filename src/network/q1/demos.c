/* Port of src/network/q1/demos.ts. GPL-2.0-or-later. */
#include "qa/network/q1/demos.h"
#include "qa/network/common/js_number.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
#include "qa/foundation/js.h"
/* ToInt32 (NaN and infinities are 0; the fmod form cast NaN to uint32_t). */
static int32_t ji(double n) { return qa_js_to_int32(n); }
/* Throw-time owner of one malloc'd slot: a raise after the read frees it. */
static void free_slot(void *arg) { free(*(void **)arg); *(void **)arg = NULL; }
void qa_netquake_demo_reader_init(qa_netquake_demo_reader *r, const uint8_t *bytes, size_t length, double max_message_bytes) {
  r->max_message_bytes = max_message_bytes; r->owned_base = bytes;
  long long end = -1;
  for (size_t i = 0; i < length; i++) if (bytes[i] == 10) { end = (long long)i; break; }
  if (end < 0 || end > 31) qa_q1_packet_error("Missing demo track line", 0);
  char track[32]; size_t n = (size_t)end;
  memcpy(track, bytes, n); track[n] = '\0';
  /* /^-?\d+$/ */
  size_t i = (n > 0 && track[0] == '-') ? 1 : 0;
  bool ok = i < n;
  for (size_t j = i; j < n; j++) if (!isdigit((unsigned char)track[j])) ok = false;
  if (!ok) qa_q1_packet_error("Invalid demo track", 0);
  r->forced_track = qa_js_string_to_number(track); /* Number(track) */
  qa_message_reader_init(&r->reader, bytes + end + 1, length - (size_t)end - 1);
}
int qa_netquake_demo_reader_next(qa_netquake_demo_reader *r, qa_netquake_demo_record *out) {
  qa_message_reader *mr = &r->reader;
  if (qa_message_reader_remaining(mr) <= 0) return 0;
  double size = qa_message_reader_long(mr);
  if (size < 0 || size > r->max_message_bytes) qa_q1_packet_error("Invalid demo message size", mr->offset);
  out->view_angles.x = qa_message_reader_float(mr);
  out->view_angles.y = qa_message_reader_float(mr);
  out->view_angles.z = qa_message_reader_float(mr);
  size_t length = 0;
  uint8_t *message = qa_message_reader_bytes(mr, (long long)size, &length);
  qa_cleanup guard;
  qa_cleanup_push(&guard, free_slot, &message);
  qa_message_reader_finish(mr);
  qa_cleanup_pop(&guard);
  out->message = message; out->length = length;
  return 1;
}
uint8_t *qa_write_netquake_demo_header(double track, size_t *out_len) {
  char buf[48]; int n = snprintf(buf, sizeof buf, "%s\n", QA_JS_NUMBER(trunc(track)));
  uint8_t *b = malloc((size_t)n); memcpy(b, buf, (size_t)n); *out_len = (size_t)n; return b;
}
uint8_t *qa_write_netquake_demo_record(const qa_netquake_demo_record *record, size_t *out_len) {
  qa_size_buf s; qa_size_buf_init(&s, (long long)record->length + 16, false);
  qa_msg_write_long(&s, (double)record->length);
  qa_msg_write_float(&s, record->view_angles.x);
  qa_msg_write_float(&s, record->view_angles.y);
  qa_msg_write_float(&s, record->view_angles.z);
  qa_sz_write(&s, record->message, (long long)record->length, (long long)record->length);
  uint8_t *out = qa_size_buf_bytes(&s, out_len); qa_size_buf_destroy(&s); return out;
}
void qa_quakeworld_demo_record_destroy(qa_quakeworld_demo_record *r) { free(r->message); memset(r, 0, sizeof *r); }
void qa_quakeworld_demo_reader_init(qa_quakeworld_demo_reader *r, const uint8_t *bytes, size_t length, double max_message_bytes) {
  qa_message_reader_init(&r->reader, bytes, length); r->max_message_bytes = max_message_bytes;
}
static float rd_f32(const uint8_t *b, size_t o) {
  uint32_t v = (uint32_t)b[o]|((uint32_t)b[o+1]<<8)|((uint32_t)b[o+2]<<16)|((uint32_t)b[o+3]<<24);
  float f; memcpy(&f, &v, 4); return f;
}
static int16_t rd_i16(const uint8_t *b, size_t o) { return (int16_t)((uint16_t)b[o] | ((uint16_t)b[o+1] << 8)); }
int qa_quakeworld_demo_reader_next(qa_quakeworld_demo_reader *r, qa_quakeworld_demo_record *out) {
  qa_message_reader *mr = &r->reader;
  memset(out, 0, sizeof *out);
  if (qa_message_reader_remaining(mr) <= 0) return 0;
  double seconds = qa_message_reader_float(mr), type = qa_message_reader_byte(mr);
  out->seconds = seconds;
  if (type == 0) {
    size_t raw_len = 0; uint8_t *raw = qa_message_reader_bytes(mr, 24, &raw_len);
    qa_qw_user_command *c = &out->command;
    c->milliseconds = raw[0];
    c->angles.x = rd_f32(raw, 4); c->angles.y = rd_f32(raw, 8); c->angles.z = rd_f32(raw, 12);
    c->forward_move = rd_i16(raw, 16); c->side_move = rd_i16(raw, 18); c->up_move = rd_i16(raw, 20);
    c->buttons = raw[22]; c->impulse = raw[23];
    free(raw);
    out->kind = "command";
    out->view_angles.x = qa_message_reader_float(mr);
    out->view_angles.y = qa_message_reader_float(mr);
    out->view_angles.z = qa_message_reader_float(mr);
  } else if (type == 1) {
    double size = qa_message_reader_long(mr);
    if (size < 0 || size > r->max_message_bytes) qa_q1_packet_error("Invalid QWD packet size", mr->offset);
    out->kind = "packet";
    out->message = qa_message_reader_bytes(mr, (long long)size, &out->length);
  } else if (type == 2) {
    out->kind = "sequences";
    out->outgoing = qa_message_reader_long(mr); out->incoming = qa_message_reader_long(mr);
  } else {
    char msg[64]; snprintf(msg, sizeof msg, "Unknown QWD record %s", QA_JS_NUMBER(type));
    qa_q1_packet_error(msg, mr->offset - 1);
  }
  qa_cleanup guard;
  qa_cleanup_push(&guard, free_slot, &out->message);
  qa_message_reader_finish(mr);
  qa_cleanup_pop(&guard);
  return 1;
}
uint8_t *qa_write_quakeworld_demo_record(const qa_quakeworld_demo_record *record, size_t *out_len) {
  bool is_packet = record->kind && strcmp(record->kind, "packet") == 0;
  qa_size_buf s; qa_size_buf_init(&s, is_packet ? (long long)record->length + 9 : 41, false);
  qa_msg_write_float(&s, record->seconds);
  if (record->kind && strcmp(record->kind, "command") == 0) {
    qa_msg_write_byte(&s, 0);
    uint8_t bytes[24]; memset(bytes, 0, sizeof bytes);
    const qa_qw_user_command *c = &record->command;
    bytes[0] = (uint8_t)ji(c->milliseconds);
    float fx = (float)c->angles.x, fy = (float)c->angles.y, fz = (float)c->angles.z;
    memcpy(bytes + 4, &fx, 4); memcpy(bytes + 8, &fy, 4); memcpy(bytes + 12, &fz, 4);
    int16_t f = (int16_t)ji(c->forward_move), sd = (int16_t)ji(c->side_move), u = (int16_t)ji(c->up_move);
    memcpy(bytes + 16, &f, 2); memcpy(bytes + 18, &sd, 2); memcpy(bytes + 20, &u, 2);
    bytes[22] = (uint8_t)ji(c->buttons); bytes[23] = (uint8_t)ji(c->impulse);
    qa_sz_write(&s, bytes, 24, 24);
    qa_msg_write_float(&s, record->view_angles.x);
    qa_msg_write_float(&s, record->view_angles.y);
    qa_msg_write_float(&s, record->view_angles.z);
  } else if (is_packet) {
    qa_msg_write_byte(&s, 1); qa_msg_write_long(&s, (double)record->length);
    qa_sz_write(&s, record->message, (long long)record->length, (long long)record->length);
  } else {
    qa_msg_write_byte(&s, 2); qa_msg_write_long(&s, record->outgoing); qa_msg_write_long(&s, record->incoming);
  }
  uint8_t *out = qa_size_buf_bytes(&s, out_len); qa_size_buf_destroy(&s); return out;
}
void qa_quakeworld_demo_playback_init(qa_quakeworld_demo_playback *p, qa_quakeworld_demo_reader *reader) {
  p->reader = reader;
  p->has_pending = qa_quakeworld_demo_reader_next(reader, &p->pending) != 0;
}
void qa_quakeworld_demo_playback_destroy(qa_quakeworld_demo_playback *p) {
  if (p->has_pending) qa_quakeworld_demo_record_destroy(&p->pending);
  p->has_pending = false;
}
typedef struct drain_scope { qa_quakeworld_demo_record **records; size_t *count; } drain_scope;
/* A raise in reader.next leaves `pending` on the record already pushed, as TS
 * leaves this.pending unassigned; that last copy stays owned by pending. */
static void drain_scope_free(void *arg) {
  drain_scope *scope = arg;
  for (size_t i = 0; i + 1 < *scope->count; i++) free((*scope->records)[i].message);
  free(*scope->records);
}
qa_quakeworld_demo_record *qa_quakeworld_demo_playback_drain(qa_quakeworld_demo_playback *p, double seconds, bool force, size_t *out_count) {
  size_t cap = 4, count = 0;
  qa_quakeworld_demo_record *records = calloc(cap, sizeof *records);
  drain_scope scope = { &records, &count };
  qa_cleanup guard;
  qa_cleanup_push(&guard, drain_scope_free, &scope);
  while (p->has_pending && (force || p->pending.seconds <= seconds)) {
    if (count == cap) { cap *= 2; records = realloc(records, cap * sizeof *records); }
    records[count++] = p->pending;
    qa_quakeworld_demo_record next;
    p->has_pending = qa_quakeworld_demo_reader_next(p->reader, &next) != 0;
    p->pending = next;
  }
  qa_cleanup_pop(&guard);
  *out_count = count; return records;
}
bool qa_quakeworld_demo_playback_ended(const qa_quakeworld_demo_playback *p) { return !p->has_pending; }
