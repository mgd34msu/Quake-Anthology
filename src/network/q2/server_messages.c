/* Port of src/network/q2/server-messages.ts. GPL-2.0-or-later. */
#include "qa/network/q2/server_messages.h"

#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "qa/foundation/support.h"
#include "qa/network/q2/codecs/zpacket.h"
#include "qa/network/q2/constants.h"

noreturn void qa_q2_throw_unbound_server_message(double opcode,
                                                 qa_q2_protocol_identity protocol) {
  qa_q2_throw_error("Q2 service opcode %g has no binding for %s", opcode,
                    protocol.kind != NULL ? protocol.kind : "");
}

static bool kind_is(const qa_q2_protocol_identity *p, const char *kind) {
  return p->kind != NULL && strcmp(p->kind, kind) == 0;
}
static char *dup_str(const char *s) {
  size_t n = strlen(s);
  char *out = (char *)malloc(n + 1);
  if (out != NULL) memcpy(out, s, n + 1);
  return out;
}
static char *read_string_owned(qa_q2_sizebuf *m) {
  char *scratch = (char *)malloc(2048);
  if (scratch == NULL) return NULL;
  (void)qa_q2_msg_read_string(m, scratch, 2048);
  size_t n = strlen(scratch);
  char *out = (char *)realloc(scratch, n + 1);
  return out != NULL ? out : scratch;
}

void qa_q2_server_event_clear(qa_q2_server_event *event) {
  free(event->text);
  event->text = NULL;
  qa_q2_wire_frame_free(&event->frame);
  qa_q2_temp_entity_free(&event->temp);
  free(event->counts);
  event->counts = NULL;
  free(event->bytes);
  event->bytes = NULL;
  free(event->indicators);
  event->indicators = NULL;
  free(event->private_name);
  event->private_name = NULL;
}

void qa_q2_server_records_free(qa_q2_server_record *records, size_t count) {
  if (records != NULL) {
    for (size_t i = 0; i < count; i++) {
      free(records[i].raw);
      qa_q2_server_event_clear(&records[i].event);
    }
    free(records);
  }
}

static bool config_set(qa_q2_server_message_reader *reader, double index,
                       const char *value) {
  for (size_t i = 0; i < reader->config_count; i++) {
    if (reader->config_strings[i].index == index) {
      char *copy = dup_str(value);
      if (copy == NULL) return false;
      free(reader->config_strings[i].value);
      reader->config_strings[i].value = copy;
      return true;
    }
  }
  if (reader->config_count == reader->config_capacity) {
    size_t next = reader->config_capacity > 0 ? reader->config_capacity * 2 : 16;
    qa_q2_config_string_entry *grown = (qa_q2_config_string_entry *)realloc(
        reader->config_strings, next * sizeof *grown);
    if (grown == NULL) return false;
    reader->config_strings = grown;
    reader->config_capacity = next;
  }
  char *copy = dup_str(value);
  if (copy == NULL) return false;
  reader->config_strings[reader->config_count].index = index;
  reader->config_strings[reader->config_count].value = copy;
  reader->config_count++;
  return true;
}

const char *qa_q2_server_message_reader_config_string(
    const qa_q2_server_message_reader *reader, double index) {
  for (size_t i = 0; i < reader->config_count; i++) {
    if (reader->config_strings[i].index == index) return reader->config_strings[i].value;
  }
  return NULL;
}

void qa_q2_server_message_reader_destroy(qa_q2_server_message_reader *reader) {
  qa_q2_wire_codec_destroy(&reader->wire);
  if (reader->config_strings != NULL) {
    for (size_t i = 0; i < reader->config_count; i++) free(reader->config_strings[i].value);
    free(reader->config_strings);
  }
  if (reader->histories != NULL) {
    for (size_t i = 0; i < reader->history_count; i++) {
      qa_q2_frame_history_destroy(&reader->histories[i].history);
    }
    free(reader->histories);
  }
  qa_q2_baseline_list_free(&reader->baselines);
  free(reader->compressed_download);
  memset(reader, 0, sizeof *reader);
}

bool qa_q2_server_message_reader_init(qa_q2_server_message_reader *reader,
                                      qa_q2_protocol_identity protocol,
                                      qa_q2_server_message_options options) {
  memset(reader, 0, sizeof *reader);
  reader->options = options;
  if (!qa_q2_wire_codec_init(&reader->wire, protocol)) return false;
  double max_cs = options.max_config_strings;
  double slots = options.inventory_slots;
  if (max_cs != (double)(int64_t)max_cs || max_cs < 1 || max_cs > 65535 ||
      slots != (double)(int64_t)slots || slots < 1 || slots > 32768) {
    qa_range_error("Invalid Q2 message layout limits");
  }
  return true;
}

double qa_q2_server_message_reader_get_seat(const qa_q2_server_message_reader *reader) {
  return reader->selected_seat;
}

qa_q2_frame_history *qa_q2_server_message_reader_get_history(
    qa_q2_server_message_reader *reader, double seat) {
  for (size_t i = 0; i < reader->history_count; i++) {
    if (reader->histories[i].seat == seat) return &reader->histories[i].history;
  }
  if (reader->history_count == reader->history_capacity) {
    size_t next = reader->history_capacity > 0 ? reader->history_capacity * 2 : 4;
    qa_q2_seat_history *grown =
        (qa_q2_seat_history *)realloc(reader->histories, next * sizeof *grown);
    if (grown == NULL) return NULL;
    reader->histories = grown;
    reader->history_capacity = next;
  }
  qa_q2_frame_history *history = &reader->histories[reader->history_count].history;
  if (!qa_q2_frame_history_init(history, 0)) return NULL;
  reader->histories[reader->history_count].seat = seat;
  reader->history_count++;
  for (size_t i = 0; i < reader->baselines.count; i++) {
    (void)qa_q2_baseline_list_set(&history->baselines, reader->baselines.entries[i].number,
                                  &reader->baselines.entries[i].state);
  }
  return history;
}
qa_q2_frame_history *qa_q2_server_message_reader_get_history_default(
    qa_q2_server_message_reader *reader) {
  return qa_q2_server_message_reader_get_history(reader, reader->selected_seat);
}

void qa_q2_server_message_reader_reset(qa_q2_server_message_reader *reader) {
  reader->legacy_demo26 = false;
  for (size_t i = 0; i < reader->history_count; i++) {
    qa_q2_frame_history_destroy(&reader->histories[i].history);
  }
  reader->history_count = 0;
  free(reader->baselines.entries);
  reader->baselines.entries = NULL;
  reader->baselines.count = 0;
  reader->baselines.capacity = 0;
  for (size_t i = 0; i < reader->config_count; i++) free(reader->config_strings[i].value);
  reader->config_count = 0;
  reader->stream = QA_Q2_STREAM_NONE;
  reader->selected_seat = 0;
  free(reader->compressed_download);
  reader->compressed_download = NULL;
  reader->compressed_download_length = 0;
  reader->inflated_download_bytes = 0;
}

void qa_q2_server_message_reader_accept_decoded(qa_q2_server_message_reader *reader,
                                                qa_q2_server_record *records,
                                                size_t count) {
  for (size_t i = 0; i < count; i++) {
    qa_q2_server_record *record = &records[i];
    if (record->event.kind == QA_Q2_SERVER_DATA) qa_q2_server_message_reader_reset(reader);
    reader->selected_seat = record->seat;
    if (record->event.kind == QA_Q2_SERVER_CONFIG_STRING) {
      (void)config_set(reader, record->event.index,
                       record->event.text != NULL ? record->event.text : "");
    } else if (record->event.kind == QA_Q2_SERVER_BASELINE) {
      (void)qa_q2_baseline_list_set(&reader->baselines, record->event.entity.number,
                                    &record->event.entity);
      for (size_t h = 0; h < reader->history_count; h++) {
        (void)qa_q2_baseline_list_set(&reader->histories[h].history.baselines,
                                      record->event.entity.number,
                                      &record->event.entity);
      }
    } else if (record->event.kind == QA_Q2_SERVER_FRAME) {
      qa_q2_frame_history *history =
          qa_q2_server_message_reader_get_history(reader, record->seat);
      if (history != NULL) (void)qa_q2_frame_history_accept(history, &record->event.frame);
    }
  }
}

bool qa_q2_server_message_reader_latest_frames(qa_q2_server_message_reader *reader,
                                               qa_q2_latest_frame **out,
                                               size_t *out_count) {
  qa_q2_latest_frame *rows =
      (qa_q2_latest_frame *)calloc(reader->history_count > 0 ? reader->history_count : 1,
                                   sizeof *rows);
  if (rows == NULL) return false;
  size_t count = 0;
  for (size_t i = 0; i < reader->history_count; i++) {
    qa_q2_wire_frame *frame =
        qa_q2_frame_history_latest(&reader->histories[i].history);
    if (frame != NULL) {
      rows[count].seat = reader->histories[i].seat;
      rows[count].frame = frame;
      count++;
    }
  }
  *out = rows;
  *out_count = count;
  return true;
}

typedef struct record_list {
  qa_q2_server_record *records;
  size_t count, capacity;
} record_list;

static bool record_push(record_list *list, qa_q2_server_record record) {
  if (list->count == list->capacity) {
    size_t next = list->capacity > 0 ? list->capacity * 2 : 16;
    qa_q2_server_record *grown =
        (qa_q2_server_record *)realloc(list->records, next * sizeof *grown);
    if (grown == NULL) return false;
    list->records = grown;
    list->capacity = next;
  }
  list->records[list->count++] = record;
  return true;
}

/* TS private record(): slices raw and checks the read. */
static bool finish_record(qa_q2_server_message_reader *reader, double opcode,
                          int64_t start, qa_q2_server_event *event,
                          qa_q2_server_record *out) {
  qa_q2_message_check_read(&reader->wire.message);
  qa_q2_sizebuf *m = &reader->wire.message;
  int64_t span = m->readcount - start;
  size_t n = span > 0 ? (size_t)span : 0;
  uint8_t *raw = (uint8_t *)malloc(n > 0 ? n : 1);
  if (raw == NULL) return false;
  for (size_t i = 0; i < n; i++) {
    int64_t at = start + (int64_t)i;
    raw[i] = at >= 0 && (uint64_t)at < (uint64_t)m->data_length ? m->data[at] : 0;
  }
  bool kex_frame = event->kind == QA_Q2_SERVER_FRAME &&
                   (kind_is(&reader->wire.selected_protocol, "q2-kex") ||
                    kind_is(&reader->wire.selected_protocol, "q2-kex-demo"));
  memset(out, 0, sizeof *out);
  out->seat = kex_frame ? 0 : reader->selected_seat;
  out->opcode = opcode;
  out->raw = raw;
  out->raw_length = n;
  out->event = *event;
  memset(event, 0, sizeof *event);
  return true;
}

static bool push_record(qa_q2_server_message_reader *reader, record_list *list,
                        double opcode, int64_t start, qa_q2_server_event *event) {
  qa_q2_server_record record;
  if (!finish_record(reader, opcode, start, event, &record)) return false;
  if (!record_push(list, record)) {
    free(record.raw);
    /* event ownership moved into record; clear a shallow copy */
    qa_q2_server_event_clear(&record.event);
    return false;
  }
  return true;
}

/* TS private config(): NULL event is the stream terminator. */
static bool read_config(qa_q2_server_message_reader *reader, qa_q2_server_event *event,
                        bool *is_null) {
  qa_q2_sizebuf *m = &reader->wire.message;
  int32_t index = qa_q2_msg_read_word(m);
  if ((double)index == reader->options.max_config_strings) {
    *is_null = true;
    return true;
  }
  if ((double)index >= reader->options.max_config_strings) {
    qa_range_error("Q2 configstring index exceeds selected layout");
  }
  char *value = read_string_owned(m);
  if (value == NULL) return false;
  if (!config_set(reader, (double)index, value)) {
    free(value);
    return false;
  }
  memset(event, 0, sizeof *event);
  event->kind = QA_Q2_SERVER_CONFIG_STRING;
  event->index = (double)index;
  event->text = value;
  *is_null = false;
  return true;
}

static bool read_baseline(qa_q2_server_message_reader *reader,
                          qa_q2_server_event *event, bool *is_null) {
  qa_q2_protocol_codec *codec = reader->wire.selected_codec;
  qa_q2_entity_bits header;
  codec->read_entity_bits(codec, &header);
  if (header.number == 0) {
    *is_null = true;
    return true;
  }
  qa_q2_wire_entity empty;
  qa_q2_wire_entity_init(&empty);
  qa_q2_wire_entity entity;
  qa_q2_wire_entity_init(&entity);
  codec->read_delta_entity(codec, &empty, &entity, header.number, header.bits_u64);
  if (!qa_q2_baseline_list_set(&reader->baselines, entity.number, &entity)) return false;
  for (size_t h = 0; h < reader->history_count; h++) {
    (void)qa_q2_baseline_list_set(&reader->histories[h].history.baselines, entity.number,
                                  &entity);
  }
  memset(event, 0, sizeof *event);
  event->kind = QA_Q2_SERVER_BASELINE;
  event->entity = entity;
  *is_null = false;
  return true;
}

static void read_sound(qa_q2_server_message_reader *reader, qa_q2_sound_message *out) {
  qa_q2_sizebuf *m = &reader->wire.message;
  memset(out, 0, sizeof *out);
  if (kind_is(&reader->wire.selected_protocol, "q2-kex") ||
      kind_is(&reader->wire.selected_protocol, "q2-kex-demo")) {
    qa_q2_kex_sound s;
    qa_q2_kex_read_sound(&reader->wire.kex, &s);
    out->flags = s.flags;
    out->index = s.index;
    out->entity = s.entity;
    out->channel = s.channel;
    out->has_position = s.has_pos;
    out->position.x = (double)s.pos[0];
    out->position.y = (double)s.pos[1];
    out->position.z = (double)s.pos[2];
    out->volume = s.volume;
    out->attenuation = s.attenuation;
    out->delay_seconds = s.timeofs;
    return;
  }
  int32_t flags = qa_q2_msg_read_byte(m);
  double index = (flags & 32) != 0 ? (double)qa_q2_msg_read_word(m)
                                   : (double)qa_q2_msg_read_byte(m);
  double volume = (flags & 1) != 0 ? (double)qa_q2_msg_read_byte(m) / 255.0 : 1;
  double attenuation = (flags & 2) != 0 ? (double)qa_q2_msg_read_byte(m) / 64.0 : 1;
  double delay = (flags & 16) != 0 ? (double)qa_q2_msg_read_byte(m) / 1000.0 : 0;
  int32_t channel = (flags & 8) != 0 ? qa_q2_msg_read_word(m) : 0;
  out->flags = (double)flags;
  out->index = index;
  out->volume = volume;
  out->attenuation = attenuation;
  out->delay_seconds = delay;
  out->entity = (double)((uint32_t)channel >> 3);
  out->channel = (double)((uint32_t)channel & 7u);
  out->has_position = (flags & 4) != 0;
  if (out->has_position) {
    out->position = qa_q2_read_game_position(
        m, qa_q2_wire_codec_floating_coordinates(&reader->wire),
        qa_q2_wire_codec_q2pro_extended_v2(&reader->wire));
  }
}

/* Raw-inflate helper mirroring inflateRawSync use in downloads. */
static uint8_t *inflate_download(const uint8_t *bytes, size_t length, int flush,
                                 size_t *out_length) {
  size_t capacity = length * 4 + 4096;
  uint8_t *out = (uint8_t *)malloc(capacity);
  if (out == NULL) return NULL;
  z_stream stream;
  memset(&stream, 0, sizeof stream);
  if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
    free(out);
    qa_q2_throw_error("Q2 download inflation failed");
  }
  stream.next_in = (Bytef *)bytes;
  stream.avail_in = (uInt)length;
  stream.next_out = out;
  stream.avail_out = (uInt)capacity;
  int status = inflate(&stream, flush);
  size_t produced = stream.total_out;
  inflateEnd(&stream);
  if (status != Z_OK && status != Z_STREAM_END && status != Z_BUF_ERROR) {
    free(out);
    qa_q2_throw_error("Q2 download inflation failed");
  }
  *out_length = produced;
  return out;
}

typedef enum download_mode { DL_NONE, DL_BLOCK, DL_STREAM } download_mode;

static bool read_download(qa_q2_server_message_reader *reader, download_mode mode,
                          qa_q2_server_event *event) {
  qa_q2_sizebuf *m = &reader->wire.message;
  int32_t length = qa_q2_msg_read_short(m);
  int32_t percent = qa_q2_msg_read_byte(m);
  memset(event, 0, sizeof *event);
  event->kind = QA_Q2_SERVER_DOWNLOAD;
  event->percent = (double)percent;
  if (length < 0) {
    free(reader->compressed_download);
    reader->compressed_download = NULL;
    reader->compressed_download_length = 0;
    reader->inflated_download_bytes = 0;
    event->has_bytes = false;
    return true;
  }
  bool has_expected = mode == DL_BLOCK;
  int32_t expected = has_expected ? qa_q2_msg_read_word(m) : 0;
  if (m->readcount + length > m->cursize) qa_q2_throw_error("Truncated Q2 download block");
  size_t block_len = (size_t)length;
  uint8_t *block = (uint8_t *)malloc(block_len > 0 ? block_len : 1);
  if (block == NULL) return false;
  for (size_t i = 0; i < block_len; i++) {
    int64_t at = m->readcount + (int64_t)i;
    block[i] = at >= 0 && (uint64_t)at < (uint64_t)m->data_length ? m->data[at] : 0;
  }
  m->readcount += length;
  if (mode == DL_NONE) {
    event->has_bytes = true;
    event->bytes = block;
    event->bytes_length = block_len;
    return true;
  }
  if (mode == DL_BLOCK) {
    size_t inflated_len = 0;
    uint8_t *inflated = inflate_download(block, block_len, Z_FINISH, &inflated_len);
    free(block);
    if (inflated == NULL) return false;
    if ((int64_t)inflated_len != (int64_t)expected) {
      free(inflated);
      qa_q2_throw_error("Q2 download length mismatch");
    }
    event->has_bytes = true;
    event->bytes = inflated;
    event->bytes_length = inflated_len;
    return true;
  }
  size_t joined_len = reader->compressed_download_length + block_len;
  uint8_t *joined = (uint8_t *)malloc(joined_len > 0 ? joined_len : 1);
  if (joined == NULL) {
    free(block);
    return false;
  }
  if (reader->compressed_download_length > 0) {
    memcpy(joined, reader->compressed_download, reader->compressed_download_length);
  }
  if (block_len > 0) memcpy(joined + reader->compressed_download_length, block, block_len);
  free(block);
  free(reader->compressed_download);
  reader->compressed_download = joined;
  reader->compressed_download_length = joined_len;
  size_t inflated_len = 0;
  uint8_t *inflated = inflate_download(joined, joined_len,
                                       percent == 100 ? Z_FINISH : Z_SYNC_FLUSH,
                                       &inflated_len);
  if (inflated == NULL) return false;
  size_t prior = reader->inflated_download_bytes;
  size_t delta_len = inflated_len > prior ? inflated_len - prior : 0;
  uint8_t *delta = (uint8_t *)malloc(delta_len > 0 ? delta_len : 1);
  if (delta == NULL) {
    free(inflated);
    return false;
  }
  if (delta_len > 0) memcpy(delta, inflated + prior, delta_len);
  free(inflated);
  reader->inflated_download_bytes = inflated_len;
  if (percent == 100) {
    free(reader->compressed_download);
    reader->compressed_download = NULL;
    reader->compressed_download_length = 0;
    reader->inflated_download_bytes = 0;
  }
  event->has_bytes = true;
  event->bytes = delta;
  event->bytes_length = delta_len;
  return true;
}

typedef struct nested_ctx {
  qa_q2_server_message_reader *reader;
  record_list *list;
  bool ok;
} nested_ctx;

static bool parse_internal(qa_q2_server_message_reader *reader, record_list *list);

static void nested_read(void *vctx, qa_q2_protocol_codec *codec, qa_q2_sizebuf *message) {
  (void)codec;
  (void)message;
  nested_ctx *c = (nested_ctx *)vctx;
  c->ok = parse_internal(c->reader, c->list);
}

static bool has_private_opcode(const qa_q2_server_message_reader *reader, int32_t opcode) {
  if (reader->options.private_opcodes == NULL) return false;
  for (size_t i = 0; i < reader->options.private_opcode_count; i++) {
    if (reader->options.private_opcodes[i] == opcode) return true;
  }
  return false;
}

static bool text_event(qa_q2_server_message_reader *reader, record_list *list,
                       int32_t opcode, int64_t start, qa_q2_server_event_kind kind) {
  qa_q2_server_event event;
  memset(&event, 0, sizeof event);
  event.kind = kind;
  event.text = read_string_owned(&reader->wire.message);
  if (event.text == NULL) return false;
  if (!push_record(reader, list, (double)opcode, start, &event)) {
    qa_q2_server_event_clear(&event);
    return false;
  }
  return true;
}

static bool simple_event(qa_q2_server_message_reader *reader, record_list *list,
                         int32_t opcode, int64_t start, qa_q2_server_event *event) {
  if (!push_record(reader, list, (double)opcode, start, event)) {
    qa_q2_server_event_clear(event);
    return false;
  }
  return true;
}

static bool parse_internal(qa_q2_server_message_reader *reader, record_list *list) {
  qa_q2_sizebuf *m = &reader->wire.message;
  qa_q2_protocol_codec *codec = reader->wire.selected_codec;
  bool kex = kind_is(&reader->wire.selected_protocol, "q2-kex") ||
             kind_is(&reader->wire.selected_protocol, "q2-kex-demo");
  bool rerelease = kind_is(&reader->wire.selected_protocol, "q2-rerelease") ||
                   kind_is(&reader->wire.selected_protocol, "q2-private-classic");
  while (m->readcount < m->cursize) {
    int64_t start = m->readcount;
    if (reader->stream != QA_Q2_STREAM_NONE) {
      qa_q2_server_stream mode = reader->stream;
      qa_q2_server_event event;
      memset(&event, 0, sizeof event);
      bool is_null = false;
      bool ok = mode == QA_Q2_STREAM_BASELINE
                    ? read_baseline(reader, &event, &is_null)
                    : read_config(reader, &event, &is_null);
      if (!ok) return false;
      if (!is_null) {
        if (!simple_event(reader, list, mode == QA_Q2_STREAM_BASELINE ? 39 : 38, start,
                          &event)) return false;
      } else {
        reader->stream = mode == QA_Q2_STREAM_GAMESTATE_CONFIG ? QA_Q2_STREAM_BASELINE
                                                                : QA_Q2_STREAM_NONE;
      }
      qa_q2_message_check_read(m);
      continue;
    }
    int32_t opcode = qa_q2_wire_codec_opcode(&reader->wire, qa_q2_msg_read_byte(m));
    if (has_private_opcode(reader, opcode)) {
      if (reader->options.private_message == NULL) {
        qa_q2_throw_unbound_server_message((double)opcode, reader->wire.selected_protocol);
      }
      qa_q2_server_event event;
      memset(&event, 0, sizeof event);
      reader->options.private_message(reader->options.private_ctx, (double)opcode, m,
                                      reader->wire.selected_protocol, &event);
      if (!simple_event(reader, list, opcode, start, &event)) return false;
      continue;
    }
    if ((opcode == 21 && (kind_is(&reader->wire.selected_protocol, "q2-r1q2") ||
                          kind_is(&reader->wire.selected_protocol, "q2-q2pro"))) ||
        (opcode == 34 && rerelease)) {
      size_t payload_len = 0;
      uint8_t *payload = qa_q2_read_zpacket_payload(m, &payload_len);
      if (payload == NULL) return false;
      nested_ctx nc = {reader, list, true};
      bool ok = qa_q2_wire_codec_nested(&reader->wire, payload, payload_len, nested_read,
                                        &nc);
      free(payload);
      if (!ok || !nc.ok) return false;
      continue;
    }
    qa_q2_server_event event;
    memset(&event, 0, sizeof event);
    bool pushed = false;
    switch (opcode) {
      case 6: event.kind = QA_Q2_SERVER_NOP; break;
      case 7: event.kind = QA_Q2_SERVER_DISCONNECT; break;
      case 8: event.kind = QA_Q2_SERVER_RECONNECT; break;
      case 10:
        event.kind = QA_Q2_SERVER_PRINT;
        event.level = (double)qa_q2_msg_read_byte(m);
        event.text = read_string_owned(m);
        if (event.text == NULL) return false;
        break;
      case 11:
        if (!text_event(reader, list, opcode, start, QA_Q2_SERVER_COMMAND_TEXT)) return false;
        pushed = true;
        break;
      case 15:
        if (!text_event(reader, list, opcode, start, QA_Q2_SERVER_CENTER_PRINT)) return false;
        pushed = true;
        break;
      case 4:
        if (!text_event(reader, list, opcode, start, QA_Q2_SERVER_LAYOUT)) return false;
        pushed = true;
        break;
      case 12: {
        int32_t protocol = qa_q2_msg_read_long(m);
        bool legacy = protocol == 26 && reader->options.read_mode_demo &&
                      kind_is(&reader->wire.selected_protocol, "q2-classic");
        if (protocol != reader->wire.selected_protocol.version && !legacy) {
          qa_q2_throw_error("Q2 serverdata protocol %d differs from negotiated %lld",
                            (int)protocol,
                            (long long)reader->wire.selected_protocol.version);
        }
        qa_q2_server_data_result data;
        codec->read_server_data(codec, &data);
        qa_q2_wire_codec_accept_server_revision(
            &reader->wire, data.has_r1q2_version, data.r1q2_version);
        qa_q2_message_check_read(m);
        qa_q2_server_message_reader_reset(reader);
        reader->legacy_demo26 = legacy;
        event.kind = QA_Q2_SERVER_DATA;
        event.data = data;
        break;
      }
      case 13: {
        bool is_null = false;
        if (!read_config(reader, &event, &is_null)) return false;
        if (is_null) qa_q2_throw_error("Q2 configstring terminator outside stream");
        break;
      }
      case 14: {
        bool is_null = false;
        if (!read_baseline(reader, &event, &is_null)) return false;
        if (is_null) qa_q2_throw_error("Q2 zero spawn baseline");
        break;
      }
      case 20: {
        qa_q2_frame_history *history = qa_q2_server_message_reader_get_history(
            reader, kex ? 0 : reader->selected_seat);
        if (history == NULL) return false;
        qa_q2_wire_frame *stored =
            qa_q2_frame_history_read(history, &reader->wire, !reader->legacy_demo26);
        if (stored == NULL) return false;
        event.kind = QA_Q2_SERVER_FRAME;
        if (!qa_q2_wire_frame_clone(stored, &event.frame)) return false;
        break;
      }
      case 9:
        event.kind = QA_Q2_SERVER_SOUND;
        read_sound(reader, &event.sound);
        break;
      case 3: {
        event.kind = QA_Q2_SERVER_TEMPORARY_ENTITY;
        bool extended = reader->options.has_q2pro_extended_temp
                            ? reader->options.q2pro_extended_temp
                            : qa_q2_wire_codec_q2pro_extended(&reader->wire);
        if (!qa_q2_read_temp_entity(m, qa_q2_wire_codec_floating_coordinates(&reader->wire),
                                    extended,
                                    qa_q2_wire_codec_q2pro_extended_v2(&reader->wire),
                                    &event.temp)) return false;
        break;
      }
      case 1:
      case 2: {
        int32_t entity = qa_q2_msg_read_word(m);
        int32_t flash = qa_q2_msg_read_byte(m);
        bool monster = opcode == 2;
        bool silenced = !monster && (flash & 128) != 0;
        if (!monster) flash &= 127;
        if (monster && (rerelease || qa_q2_wire_codec_q2pro_extended(&reader->wire))) {
          flash |= (int32_t)(((uint32_t)entity & 0xe000u) >> 5);
          entity &= 0x1fff;
        }
        event.kind = QA_Q2_SERVER_MUZZLE_FLASH;
        event.flash_entity = (double)entity;
        event.flash = (double)flash;
        event.monster = monster;
        event.silenced = silenced;
        break;
      }
      case 5: {
        size_t slots = (size_t)reader->options.inventory_slots;
        double *counts = (double *)malloc(slots * sizeof(double));
        if (counts == NULL) return false;
        for (size_t i = 0; i < slots; i++) counts[i] = (double)qa_q2_msg_read_short(m);
        event.kind = QA_Q2_SERVER_INVENTORY;
        event.counts = counts;
        event.count_count = slots;
        break;
      }
      case 16:
        if (!read_download(reader, DL_NONE, &event)) return false;
        break;
      case 21:
        if (!kex) qa_q2_throw_error("Unexpected Q2 splitclient");
        reader->selected_seat = qa_q2_kex_read_splitclient(&reader->wire.kex);
        event.kind = QA_Q2_SERVER_SEAT;
        event.seat = reader->selected_seat;
        break;
      case 22: {
        if (!kex) {
          download_mode mode = kind_is(&reader->wire.selected_protocol, "q2-q2pro") &&
                                       qa_q2_wire_codec_q2pro_revision(&reader->wire) >= 1021
                                   ? DL_STREAM
                                   : DL_BLOCK;
          if (!read_download(reader, mode, &event)) return false;
          break;
        }
        qa_q2_kex_configstring_record *items = NULL;
        size_t item_count = 0;
        if (!qa_q2_kex_read_configblast(&reader->wire.kex, &items, &item_count)) return false;
        pushed = true;
        for (size_t i = 0; i < item_count; i++) {
          if (items[i].index >= reader->options.max_config_strings) {
            qa_q2_kex_configblast_free(items, item_count);
            qa_q2_throw_error("KEX configblast index exceeds selected layout");
          }
          if (!config_set(reader, items[i].index, items[i].value)) {
            qa_q2_kex_configblast_free(items, item_count);
            return false;
          }
          qa_q2_server_event item_event;
          memset(&item_event, 0, sizeof item_event);
          item_event.kind = QA_Q2_SERVER_CONFIG_STRING;
          item_event.index = items[i].index;
          item_event.text = items[i].value; /* steal */
          items[i].value = NULL;
          if (!simple_event(reader, list, opcode, start, &item_event)) {
            qa_q2_kex_configblast_free(items, item_count);
            return false;
          }
        }
        qa_q2_kex_configblast_free(items, item_count);
        break;
      }
      case 23: {
        if (!kex) {
          reader->stream = QA_Q2_STREAM_GAMESTATE_CONFIG;
          pushed = true; /* continue without a record */
          break;
        }
        qa_q2_kex_baseline_entry *items = NULL;
        size_t item_count = 0;
        if (!qa_q2_kex_read_spawnbaselineblast(&reader->wire.kex, &items, &item_count)) return false;
        pushed = true;
        for (size_t i = 0; i < item_count; i++) {
          (void)qa_q2_baseline_list_set(&reader->baselines, items[i].entnum, &items[i].state);
          for (size_t h = 0; h < reader->history_count; h++) {
            (void)qa_q2_baseline_list_set(&reader->histories[h].history.baselines,
                                          items[i].entnum, &items[i].state);
          }
          qa_q2_server_event item_event;
          memset(&item_event, 0, sizeof item_event);
          item_event.kind = QA_Q2_SERVER_BASELINE;
          item_event.entity = items[i].state;
          if (!simple_event(reader, list, opcode, start, &item_event)) {
            free(items);
            return false;
          }
        }
        free(items);
        break;
      }
      case 24:
        if (kex) {
          event.kind = QA_Q2_SERVER_LEVEL_RESTART;
        } else {
          event.kind = QA_Q2_SERVER_SETTING;
          event.index = (double)qa_q2_msg_read_long(m);
          event.value_number = (double)qa_q2_msg_read_long(m);
        }
        break;
      case 25:
        if (!kex && !rerelease) {
          reader->stream = QA_Q2_STREAM_CONFIG;
          pushed = true;
          break;
        }
        event.kind = QA_Q2_SERVER_DAMAGE;
        if (!qa_q2_kex_read_damage(&reader->wire.kex, &event.indicators,
                                   &event.indicator_count)) return false;
        break;
      case 26:
        if (!kex && !rerelease) {
          reader->stream = QA_Q2_STREAM_BASELINE;
          pushed = true;
          break;
        }
        event.kind = QA_Q2_SERVER_LOCALIZED_PRINT;
        qa_q2_kex_read_locprint(&reader->wire.kex, &event.localized);
        break;
      case 27:
        event.kind = QA_Q2_SERVER_FOG;
        qa_q2repro_read_fog(&reader->wire.rerelease, &event.fog);
        break;
      case 30:
        event.kind = QA_Q2_SERVER_POI;
        qa_q2_kex_read_poi(&reader->wire.kex, &event.poi);
        break;
      case 31:
        event.kind = QA_Q2_SERVER_HELP_PATH;
        qa_q2_kex_read_help_path(&reader->wire.kex, &event.help_path);
        break;
      case 32: {
        qa_q2_kex_muzzleflash3 flash3;
        qa_q2_kex_read_muzzleflash3(&reader->wire.kex, &flash3);
        event.kind = QA_Q2_SERVER_MUZZLE_FLASH;
        event.flash_entity = flash3.entity;
        event.flash = flash3.weapon;
        event.monster = true;
        event.silenced = false;
        break;
      }
      case 33: {
        event.kind = QA_Q2_SERVER_ACHIEVEMENT;
        char *text = (char *)malloc(2048);
        if (text == NULL) return false;
        (void)qa_q2_kex_read_achievement(&reader->wire.kex, text, 2048);
        event.text = text;
        break;
      }
      case 35:
        if (!rerelease) qa_q2_throw_unbound_server_message((double)opcode, reader->wire.selected_protocol);
        if (!read_download(reader, DL_STREAM, &event)) return false;
        break;
      case 36:
        if (!rerelease) qa_q2_throw_unbound_server_message((double)opcode, reader->wire.selected_protocol);
        reader->stream = QA_Q2_STREAM_GAMESTATE_CONFIG;
        pushed = true;
        break;
      case 37:
        if (!rerelease) qa_q2_throw_unbound_server_message((double)opcode, reader->wire.selected_protocol);
        event.kind = QA_Q2_SERVER_SETTING;
        event.index = (double)qa_q2_msg_read_long(m);
        event.value_number = (double)qa_q2_msg_read_long(m);
        break;
      case 38:
        if (!rerelease) qa_q2_throw_unbound_server_message((double)opcode, reader->wire.selected_protocol);
        reader->stream = QA_Q2_STREAM_CONFIG;
        pushed = true;
        break;
      case 39:
        if (!rerelease) qa_q2_throw_unbound_server_message((double)opcode, reader->wire.selected_protocol);
        reader->stream = QA_Q2_STREAM_BASELINE;
        pushed = true;
        break;
      default: {
        if (reader->options.private_message == NULL) {
          qa_q2_throw_unbound_server_message((double)opcode, reader->wire.selected_protocol);
        }
        reader->options.private_message(reader->options.private_ctx, (double)opcode, m,
                                        reader->wire.selected_protocol, &event);
        break;
      }
    }
    if (!pushed) {
      if (!simple_event(reader, list, opcode, start, &event)) return false;
    }
  }
  return true;
}

bool qa_q2_server_message_reader_read(qa_q2_server_message_reader *reader,
                                      const uint8_t *bytes, size_t length,
                                      qa_q2_server_record **out, size_t *out_count) {
  if (!qa_q2_wire_codec_begin(&reader->wire, bytes, length)) return false;
  record_list list = {NULL, 0, 0};
  if (!parse_internal(reader, &list)) {
    qa_q2_server_records_free(list.records, list.count);
    return false;
  }
  qa_q2_wire_codec_finish(&reader->wire);
  *out = list.records;
  *out_count = list.count;
  return true;
}
