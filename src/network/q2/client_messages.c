/* Port of src/network/q2/client-messages.ts. GPL-2.0-or-later. */
#include "qa/network/q2/client_messages.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/network/q2/checksum.h"
#include "qa/network/q2/constants.h"
#include "qa/network/q2/handshake.h"
#include "qa/network/q2/message.h"

static bool kind_is(const qa_q2_protocol_identity *p, const char *kind) {
  return p->kind != NULL && strcmp(p->kind, kind) == 0;
}

static void event_clear(qa_q2_client_event *event) {
  free(event->text);
  event->text = NULL;
  qa_q2_clc_batch_move_free(&event->batch);
}

void qa_q2_client_records_free(qa_q2_client_record *records, size_t count) {
  if (records != NULL) {
    for (size_t i = 0; i < count; i++) {
      event_clear(&records[i].event);
      free(records[i].raw);
    }
    free(records);
  }
}

/* TS readKexControlString: NUL-terminated bytes, UTF-8 validated. */
static char *read_kex_control_string(qa_q2_sizebuf *m) {
  size_t capacity = 64;
  size_t length = 0;
  uint8_t *bytes = (uint8_t *)malloc(capacity);
  if (bytes == NULL) return NULL;
  for (;;) {
    int32_t byte = qa_q2_msg_read_byte(m);
    if (byte < 0) {
      free(bytes);
      qa_q2_throw_error("Truncated KEX control string");
    }
    if (byte == 0) break;
    if (length == capacity) {
      capacity *= 2;
      uint8_t *grown = (uint8_t *)realloc(bytes, capacity);
      if (grown == NULL) {
        free(bytes);
        return NULL;
      }
      bytes = grown;
    }
    bytes[length++] = (uint8_t)byte;
  }
  /* UTF-8 validity (fatal decoder); reuse the handshake check by
   * validating code points here. */
  size_t i = 0;
  bool valid = true;
  while (i < length && valid) {
    uint8_t c = bytes[i];
    if (c < 0x80) { i++; continue; }
    int need;
    uint32_t cp;
    if ((c & 0xe0) == 0xc0) { need = 1; cp = c & 0x1f; if (cp == 0) valid = false; }
    else if ((c & 0xf0) == 0xe0) { need = 2; cp = c & 0x0f; }
    else if ((c & 0xf8) == 0xf0) { need = 3; cp = c & 0x07; }
    else { valid = false; break; }
    if (i + (size_t)need >= length) { valid = false; break; }
    for (int k = 0; k < need; k++) {
      uint8_t d = bytes[i + 1 + (size_t)k];
      if ((d & 0xc0) != 0x80) { valid = false; break; }
      cp = (cp << 6) | (uint32_t)(d & 0x3f);
    }
    if ((need == 1 && cp < 0x80) || (need == 2 && cp < 0x800) ||
        (need == 3 && cp < 0x10000)) valid = false;
    if (cp >= 0xd800 && cp <= 0xdfff) valid = false;
    if (cp > 0x10ffff) valid = false;
    i += (size_t)need + 1;
  }
  if (!valid) {
    free(bytes);
    qa_q2_throw_utf8_error();
  }
  char *out = (char *)realloc(bytes, length + 1);
  if (out == NULL) {
    free(bytes);
    return NULL;
  }
  out[length] = '\0';
  return out;
}

typedef struct record_list {
  qa_q2_client_record *records;
  size_t count, capacity;
} record_list;

static bool record_push(record_list *list, qa_q2_client_record record) {
  if (list->count == list->capacity) {
    size_t next = list->capacity > 0 ? list->capacity * 2 : 8;
    qa_q2_client_record *grown =
        (qa_q2_client_record *)realloc(list->records, next * sizeof *grown);
    if (grown == NULL) return false;
    list->records = grown;
    list->capacity = next;
  }
  list->records[list->count++] = record;
  return true;
}

static uint8_t *slice_raw(qa_q2_sizebuf *m, int64_t start, size_t *out_length) {
  int64_t span = m->readcount - start;
  size_t n = span > 0 ? (size_t)span : 0;
  uint8_t *raw = (uint8_t *)malloc(n > 0 ? n : 1);
  if (raw == NULL) return NULL;
  for (size_t i = 0; i < n; i++) {
    int64_t at = start + (int64_t)i;
    raw[i] = at >= 0 && (uint64_t)at < (uint64_t)m->data_length ? m->data[at] : 0;
  }
  *out_length = n;
  return raw;
}

bool qa_q2_read_client_messages(qa_q2_wire_codec *wire, const uint8_t *bytes,
                                size_t length, double sequence, double seats,
                                qa_q2_client_record **out, size_t *out_count) {
  if (!qa_q2_wire_codec_begin(wire, bytes, length)) return false;
  qa_q2_sizebuf *m = &wire->message;
  qa_q2_protocol_codec *codec = wire->selected_codec;
  record_list list = {NULL, 0, 0};
  bool moved = false;
  while (m->readcount < m->cursize) {
    int64_t start = m->readcount;
    int32_t raw = qa_q2_msg_read_byte(m);
    int32_t op = kind_is(&wire->selected_protocol, "q2-q2pro") ? raw & 31 : raw;
    qa_q2_client_event event;
    memset(&event, 0, sizeof event);
    double selected_seat = 0;
    bool handled_move = false;
    switch (op) {
      case 1:
        event.kind = QA_Q2_CLIENT_NOP;
        break;
      case 3: {
        event.kind = QA_Q2_CLIENT_USERINFO;
        if (kind_is(&wire->selected_protocol, "q2-kex")) {
          event.text = read_kex_control_string(m);
          if (event.text == NULL) goto oom;
        } else {
          char buffer[2048];
          (void)qa_q2_msg_read_string(m, buffer, sizeof buffer);
          event.text = (char *)malloc(strlen(buffer) + 1);
          if (event.text == NULL) goto oom;
          (void)strcpy(event.text, buffer);
        }
        break;
      }
      case 4: {
        if (kind_is(&wire->selected_protocol, "q2-kex")) {
          selected_seat = (double)qa_q2_msg_read_byte(m) - 1;
          if (selected_seat < 0 || selected_seat >= seats) {
            qa_q2_throw_error("Q2 connection does not own the selected split player");
          }
        }
        event.kind = QA_Q2_CLIENT_COMMAND;
        if (kind_is(&wire->selected_protocol, "q2-kex")) {
          event.text = read_kex_control_string(m);
          if (event.text == NULL) goto oom;
        } else {
          char buffer[2048];
          (void)qa_q2_msg_read_string(m, buffer, sizeof buffer);
          event.text = (char *)malloc(strlen(buffer) + 1);
          if (event.text == NULL) goto oom;
          (void)strcpy(event.text, buffer);
        }
        break;
      }
      case 5: {
        if (codec->read_client_setting == NULL) {
          qa_q2_throw_error("Client setting is not supported by selected Q2 wire");
        }
        event.kind = QA_Q2_CLIENT_SETTING;
        codec->read_client_setting(codec, m, &event.setting);
        break;
      }
      case 12: {
        if (codec->read_userinfo_delta == NULL) {
          qa_q2_throw_error("Userinfo delta is not supported by selected Q2 wire");
        }
        event.kind = QA_Q2_CLIENT_USERINFO_DELTA;
        codec->read_userinfo_delta(codec, m, &event.delta);
        break;
      }
      case 2: {
        if (moved) qa_q2_throw_error("Multiple Q2 move commands in one packet");
        moved = true;
        handled_move = true;
        bool has_checksum = codec->has_clc_move_checksum && codec->clc_move_has_checksum;
        int32_t checksum = has_checksum ? qa_q2_msg_read_byte(m) : -1;
        int64_t checksum_start = m->readcount;
        if (codec->read_delta_usercmd == NULL) {
          qa_q2_throw_error("Selected Q2 wire has no client move decoder");
        }
        double last_frame = (double)qa_q2_msg_read_long(m);
        int64_t seat_total = kind_is(&wire->selected_protocol, "q2-kex") ? (int64_t)seats : 1;
        for (int64_t seat = 0; seat < seat_total; seat++) {
          qa_q2_wire_usercmd oldest;
          qa_q2_wire_usercmd old_cmd;
          qa_q2_wire_usercmd current;
          qa_q2_wire_usercmd empty;
          qa_q2_wire_usercmd_init(&oldest);
          qa_q2_wire_usercmd_init(&old_cmd);
          qa_q2_wire_usercmd_init(&current);
          qa_q2_wire_usercmd_init(&empty);
          bool has_light = kind_is(&wire->selected_protocol, "q2-kex");
          int32_t lightlevel = has_light ? qa_q2_msg_read_byte(m) : 0;
          codec->read_delta_usercmd(codec, m, &empty, &oldest);
          codec->read_delta_usercmd(codec, m, &oldest, &old_cmd);
          codec->read_delta_usercmd(codec, m, &old_cmd, &current);
          if (has_light) {
            oldest.lightlevel = (double)lightlevel;
            old_cmd.lightlevel = (double)lightlevel;
            current.lightlevel = (double)lightlevel;
          }
          qa_q2_message_check_read(m);
          if (has_checksum) {
            int64_t span = m->readcount - checksum_start;
            const uint8_t *span_data = m->data + (checksum_start > 0 ? (size_t)checksum_start : 0);
            size_t span_len = span > 0 ? (size_t)span : 0;
            if (qa_q2_block_sequence_checksum(span_data, span_len, (int64_t)sequence) != checksum) {
              qa_q2_throw_error("Q2 command sequence checksum mismatch");
            }
          }
          qa_q2_client_record record;
          memset(&record, 0, sizeof record);
          record.seat = (double)seat;
          record.event.kind = QA_Q2_CLIENT_MOVE;
          record.event.last_frame = last_frame;
          record.event.commands[0] = oldest;
          record.event.commands[1] = old_cmd;
          record.event.commands[2] = current;
          record.raw = slice_raw(m, start, &record.raw_length);
          if (record.raw == NULL) goto oom;
          if (!record_push(&list, record)) {
            event_clear(&record.event);
            free(record.raw);
            goto oom;
          }
        }
        break;
      }
      case 10:
      case 11: {
        if (moved) qa_q2_throw_error("Multiple Q2 move commands in one packet");
        moved = true;
        if (codec->read_batch_move == NULL) {
          qa_q2_throw_error("Batched movement is not supported by selected Q2 wire");
        }
        event.kind = QA_Q2_CLIENT_BATCH_MOVE;
        if (!codec->read_batch_move(codec, m, op == 10,
                                    (double)((uint32_t)raw >> 5), &event.batch)) {
          goto oom;
        }
        break;
      }
      default:
        qa_q2_throw_error("Unknown Q2 client opcode %d", (int)op);
    }
    if (handled_move) continue;
    qa_q2_message_check_read(m);
    qa_q2_client_record record;
    memset(&record, 0, sizeof record);
    record.seat = selected_seat;
    record.event = event;
    record.raw = slice_raw(m, start, &record.raw_length);
    if (record.raw == NULL) {
      event_clear(&event);
      goto oom;
    }
    if (!record_push(&list, record)) {
      event_clear(&record.event);
      free(record.raw);
      goto oom;
    }
  }
  *out = list.records;
  *out_count = list.count;
  return true;
oom:
  qa_q2_client_records_free(list.records, list.count);
  return false;
}

uint8_t *qa_q2_encode_move(qa_q2_wire_codec *wire, double sequence, double last_frame,
                           const qa_q2_wire_usercmd commands[3], size_t *out_length) {
  qa_q2_protocol_codec *codec = wire->selected_codec;
  if (codec->write_delta_usercmd == NULL) {
    qa_q2_throw_error("Selected Q2 wire has no client move encoder");
  }
  qa_q2_sizebuf m;
  if (!qa_q2_message_create(&m, 32768)) return NULL;
  qa_q2_msg_write_byte(&m, 2);
  bool checksum = codec->has_clc_move_checksum && codec->clc_move_has_checksum;
  if (checksum) qa_q2_msg_write_byte(&m, 0);
  int64_t checksum_start = m.cursize;
  qa_q2_msg_write_long(&m, last_frame);
  if (kind_is(&wire->selected_protocol, "q2-kex")) {
    qa_q2_msg_write_byte(&m, commands[2].lightlevel);
  }
  qa_q2_wire_usercmd empty;
  qa_q2_wire_usercmd_init(&empty);
  codec->write_delta_usercmd(codec, &m, &empty, &commands[0]);
  codec->write_delta_usercmd(codec, &m, &commands[0], &commands[1]);
  codec->write_delta_usercmd(codec, &m, &commands[1], &commands[2]);
  if (checksum) {
    int64_t span = m.cursize - checksum_start;
    m.data[1] = (uint8_t)qa_q2_block_sequence_checksum(
        m.data + (size_t)checksum_start, span > 0 ? (size_t)span : 0, (int64_t)sequence);
  }
  uint8_t *out = qa_q2_message_bytes(&m, out_length);
  qa_q2_message_destroy(&m);
  return out;
}

uint8_t *qa_q2_encode_batch_move(qa_q2_wire_codec *wire, bool has_last_frame,
                                 double last_frame,
                                 const qa_q2_clc_batch_move_frame *frames,
                                 size_t frame_count, size_t *out_length) {
  qa_q2_protocol_codec *codec = wire->selected_codec;
  if (codec->write_batch_move == NULL) {
    qa_q2_throw_error("Selected Q2 codec cannot write batched moves");
  }
  qa_q2_sizebuf m;
  if (!qa_q2_message_create(&m, 32768)) return NULL;
  /* TS: (frames.length - 1) << 5, negative when there are no frames. */
  int32_t extra = kind_is(&wire->selected_protocol, "q2-q2pro")
                      ? (int32_t)(((int64_t)frame_count - 1) << 5)
                      : 0;
  qa_q2_msg_write_byte(&m, (double)((!has_last_frame ? 10 : 11) | extra));
  codec->write_batch_move(codec, &m, has_last_frame, last_frame, frames, frame_count);
  uint8_t *out = qa_q2_message_bytes(&m, out_length);
  qa_q2_message_destroy(&m);
  return out;
}

static void write_kex_text(qa_q2_sizebuf *m, const char *text) {
  for (const unsigned char *p = (const unsigned char *)text; *p != 0; p++) {
    qa_q2_msg_write_byte(m, (double)*p);
  }
  qa_q2_msg_write_byte(m, 0);
}

uint8_t *qa_q2_encode_client_control(const qa_q2_client_event *event, bool kex,
                                     size_t *out_length) {
  qa_q2_sizebuf m;
  if (!qa_q2_message_create(&m, 32768)) return NULL;
  switch (event->kind) {
    case QA_Q2_CLIENT_NOP:
      qa_q2_msg_write_byte(&m, 1);
      break;
    case QA_Q2_CLIENT_USERINFO:
      qa_q2_msg_write_byte(&m, 3);
      if (kex) write_kex_text(&m, event->text != NULL ? event->text : "");
      else qa_q2_msg_write_string(&m, event->text);
      break;
    case QA_Q2_CLIENT_COMMAND:
      qa_q2_msg_write_byte(&m, 4);
      if (kex) qa_q2_msg_write_byte(&m, 1);
      if (kex) write_kex_text(&m, event->text != NULL ? event->text : "");
      else qa_q2_msg_write_string(&m, event->text);
      break;
    case QA_Q2_CLIENT_SETTING:
      qa_q2_msg_write_byte(&m, 5);
      qa_q2_msg_write_short(&m, event->setting.index);
      qa_q2_msg_write_short(&m, event->setting.value);
      break;
    case QA_Q2_CLIENT_USERINFO_DELTA:
      qa_q2_msg_write_byte(&m, 12);
      qa_q2_msg_write_string(&m, event->delta.name);
      qa_q2_msg_write_string(&m, event->delta.value);
      break;
    case QA_Q2_CLIENT_MOVE:
    case QA_Q2_CLIENT_BATCH_MOVE:
      break;
  }
  uint8_t *out = qa_q2_message_bytes(&m, out_length);
  qa_q2_message_destroy(&m);
  return out;
}

void qa_q2_command_replay_init(qa_q2_command_replay *replay) {
  qa_q2_wire_usercmd_init(&replay->previous);
  replay->last_frame = -1;
}

static bool is_integer_number(double v) {
  return isfinite(v) && v == trunc(v);
}

void qa_q2_command_replay_execute(qa_q2_command_replay *replay,
                                  const qa_q2_client_event *event, double dropped,
                                  qa_q2_command_think_fn think, void *ctx) {
  if (!is_integer_number(dropped) || dropped < 0) {
    qa_range_error("Invalid Q2 drop count");
  }
  if (event->kind == QA_Q2_CLIENT_MOVE) {
    replay->last_frame = event->last_frame;
    if (dropped < 20) {
      while (dropped > 2) {
        think(ctx, &replay->previous);
        dropped--;
      }
      if (dropped > 1) think(ctx, &event->commands[0]);
      if (dropped > 0) think(ctx, &event->commands[1]);
    }
    think(ctx, &event->commands[2]);
    replay->previous = event->commands[2];
    return;
  }
  const qa_q2_clc_batch_move *batch = &event->batch;
  replay->last_frame = batch->lastframe;
  const qa_q2_wire_usercmd *last = NULL;
  for (size_t i = 0; i < batch->frame_count; i++) {
    for (size_t j = 0; j < batch->frames[i].cmd_count; j++) {
      last = &batch->frames[i].cmds[j];
    }
  }
  if (last == NULL) return;
  if (dropped < 20) {
    while (dropped > batch->num_dups) {
      think(ctx, &replay->previous);
      dropped--;
    }
    while (dropped > 0) {
      int64_t index = (int64_t)batch->num_dups - (int64_t)dropped;
      if (index < 0 || (uint64_t)index >= (uint64_t)batch->frame_count) {
        qa_q2_throw_error("Missing Q2 batch backup");
      }
      const qa_q2_clc_batch_move_frame *frame = &batch->frames[index];
      for (size_t j = 0; j < frame->cmd_count; j++) think(ctx, &frame->cmds[j]);
      dropped--;
    }
  }
  int64_t newest = (int64_t)batch->num_dups;
  if (newest < 0 || (uint64_t)newest >= (uint64_t)batch->frame_count) {
    qa_q2_throw_error("Missing Q2 newest command batch");
  }
  const qa_q2_clc_batch_move_frame *frame = &batch->frames[newest];
  for (size_t j = 0; j < frame->cmd_count; j++) think(ctx, &frame->cmds[j]);
  replay->previous = *last;
}

void qa_q2_rate_window_init(qa_q2_rate_window *window) {
  memset(window, 0, sizeof *window);
}
/* TS sizes[serverFrame % 10]: a negative or fractional index is not
 * an element index, so the typed-array store is silently dropped. */
static bool frame_slot(double server_frame, size_t *out) {
  if (!isfinite(server_frame) || server_frame != trunc(server_frame) ||
      server_frame < 0) return false;
  *out = (size_t)((int64_t)server_frame % 10);
  return true;
}
bool qa_q2_rate_window_drop(qa_q2_rate_window *window, double server_frame,
                            double bytes_per_second, bool loopback) {
  if (loopback) return false;
  double total = 0;
  for (int i = 0; i < 10; i++) total += (double)window->sizes[i];
  if (total <= bytes_per_second) return false;
  window->suppressed++;
  size_t slot;
  if (frame_slot(server_frame, &slot)) window->sizes[slot] = 0;
  return true;
}
void qa_q2_rate_window_sent(qa_q2_rate_window *window, double server_frame,
                            double bytes) {
  size_t slot;
  if (frame_slot(server_frame, &slot)) window->sizes[slot] = (uint32_t)bytes;
}
double qa_q2_rate_window_take_suppressed(qa_q2_rate_window *window) {
  double value = window->suppressed;
  window->suppressed = 0;
  return value;
}
