/* Port of src/network/q2/demo.ts. GPL-2.0-or-later. */
#include "qa/network/q2/demo.h"

#include <stdlib.h>
#include <string.h>

#include "qa/network/q2/codec.h"
#include "qa/network/q2/codecs/vanilla.h"
#include "qa/network/q2/constants.h"
#include "qa/network/q2/message.h"

void qa_q2_demo_records_free(qa_q2_demo_record *records, size_t count) {
  if (records != NULL) {
    for (size_t i = 0; i < count; i++) free(records[i].bytes);
    free(records);
  }
}

static int32_t load_i32(const uint8_t *bytes, size_t offset) {
  uint32_t v = (uint32_t)bytes[offset] | ((uint32_t)bytes[offset + 1] << 8) |
               ((uint32_t)bytes[offset + 2] << 16) | ((uint32_t)bytes[offset + 3] << 24);
  int32_t out;
  memcpy(&out, &v, sizeof out);
  return out;
}

bool qa_q2_read_demo(const uint8_t *bytes, size_t length, qa_q2_demo_record **out,
                     size_t *out_count) {
  size_t capacity = 16;
  size_t count = 0;
  qa_q2_demo_record *records = (qa_q2_demo_record *)calloc(capacity, sizeof *records);
  if (records == NULL) return false;
  size_t offset = 0;
  while (offset < length) {
    if (offset + 4 > length) {
      qa_q2_demo_records_free(records, count);
      qa_q2_throw_error("Truncated Q2 demo record header");
    }
    size_t start = offset;
    int32_t record_length = load_i32(bytes, offset);
    offset += 4;
    if (record_length == -1) {
      *out = records;
      *out_count = count;
      return true;
    }
    if (record_length < 0 || offset + (size_t)record_length > length) {
      qa_q2_demo_records_free(records, count);
      qa_q2_throw_error("Truncated Q2 demo packet");
    }
    if (count == capacity) {
      capacity *= 2;
      qa_q2_demo_record *grown =
          (qa_q2_demo_record *)realloc(records, capacity * sizeof *grown);
      if (grown == NULL) {
        qa_q2_demo_records_free(records, count);
        return false;
      }
      records = grown;
    }
    size_t n = (size_t)record_length;
    uint8_t *copy = (uint8_t *)malloc(n > 0 ? n : 1);
    if (copy == NULL) {
      qa_q2_demo_records_free(records, count);
      return false;
    }
    if (n > 0) memcpy(copy, bytes + offset, n);
    records[count].offset = (double)start;
    records[count].bytes = copy;
    records[count].bytes_length = n;
    count++;
    offset += n;
  }
  *out = records;
  *out_count = count;
  return true;
}

uint8_t *qa_q2_write_demo_record(const uint8_t *bytes, size_t length,
                                 size_t *out_length) {
  uint8_t *result = (uint8_t *)malloc(length + 4);
  if (result == NULL) return NULL;
  uint32_t value = (uint32_t)length;
  for (int i = 0; i < 4; i++) result[i] = (uint8_t)((value >> (8 * i)) & 0xffu);
  if (length > 0) memcpy(result + 4, bytes, length);
  if (out_length != NULL) *out_length = length + 4;
  return result;
}

uint8_t *qa_q2_finish_demo(size_t *out_length) {
  uint8_t *result = (uint8_t *)malloc(4);
  if (result == NULL) return NULL;
  memset(result, 255, 4);
  if (out_length != NULL) *out_length = 4;
  return result;
}

void qa_q2_demo_steps_free(qa_q2_demo_step *steps, size_t count) {
  if (steps != NULL) {
    for (size_t i = 0; i < count; i++) qa_q2_server_records_free(steps[i].records, steps[i].record_count);
    free(steps);
  }
}

bool qa_q2_play_demo(const uint8_t *bytes, size_t length,
                     qa_q2_server_message_reader *reader, qa_q2_demo_step **out,
                     size_t *out_count) {
  qa_q2_demo_record *records = NULL;
  size_t record_count = 0;
  if (!qa_q2_read_demo(bytes, length, &records, &record_count)) return false;
  qa_q2_demo_step *steps =
      (qa_q2_demo_step *)calloc(record_count > 0 ? record_count : 1, sizeof *steps);
  if (steps == NULL) {
    qa_q2_demo_records_free(records, record_count);
    return false;
  }
  for (size_t i = 0; i < record_count; i++) {
    if (!qa_q2_server_message_reader_read(reader, records[i].bytes,
                                         records[i].bytes_length, &steps[i].records,
                                         &steps[i].record_count)) {
      qa_q2_demo_steps_free(steps, i);
      qa_q2_demo_records_free(records, record_count);
      return false;
    }
  }
  qa_q2_demo_records_free(records, record_count);
  *out = steps;
  *out_count = record_count;
  return true;
}

static void recorded_protocol(double version, qa_q2_sizebuf *message,
                              qa_q2_protocol_identity *out) {
  memset(out, 0, sizeof *out);
  if (version == 26 || version == 34) {
    out->kind = "q2-classic";
    out->version = 34;
    return;
  }
  if (version == 35 || version == 36) {
    int64_t body = message->readcount;
    qa_q2_vanilla_context vanilla;
    qa_q2_vanilla_context_init(&vanilla, message);
    qa_q2_server_data_result scratch;
    vanilla.codec.read_server_data(&vanilla.codec, &scratch);
    if (version == 35) (void)qa_q2_msg_read_byte(message);
    int32_t revision = qa_q2_msg_read_word(message);
    qa_q2_message_check_read(message);
    message->readcount = body;
    if (version == 35) {
      if (revision == 1903 || revision == 1904 || revision == 1905) {
        out->kind = "q2-r1q2";
        out->version = 35;
        out->revision = revision;
        return;
      }
      qa_q2_throw_error("Unsupported recorded R1Q2 revision %d", (int)revision);
    }
    switch (revision) {
      case 1015: case 1017: case 1018: case 1019: case 1020: case 1021:
      case 1022: case 1023: case 1024: case 1025: case 1026:
        out->kind = "q2-q2pro";
        out->version = 36;
        out->revision = revision;
        return;
      default:
        qa_q2_throw_error("Unsupported recorded Q2PRO revision %d", (int)revision);
    }
  }
  if (version == 1038) {
    out->kind = "q2-rerelease";
    out->version = 1038;
    return;
  }
  if (version == 4038) {
    out->kind = "q2-private-classic";
    out->version = 4038;
    return;
  }
  if (version == 2022) {
    out->kind = "q2-kex-demo";
    out->version = 2022;
    return;
  }
  if (version == 2023) {
    out->kind = "q2-kex";
    out->version = 2023;
    return;
  }
  qa_q2_throw_error("Unsupported recorded Q2 protocol %g", version);
}

bool qa_q2_read_demo_header(const uint8_t *bytes, size_t length,
                            qa_q2_demo_header *out) {
  qa_q2_demo_record *records = NULL;
  size_t record_count = 0;
  if (!qa_q2_read_demo(bytes, length, &records, &record_count)) return false;
  for (size_t r = 0; r < record_count; r++) {
    qa_q2_sizebuf message;
    if (!qa_q2_message_create(&message, (int64_t)records[r].bytes_length)) {
      qa_q2_demo_records_free(records, record_count);
      return false;
    }
    qa_q2_message_load(&message, records[r].bytes, records[r].bytes_length);
    bool free_records = true;
    while (message.readcount < message.cursize) {
      int32_t opcode = qa_q2_msg_read_byte(&message);
      switch (opcode) {
        case 6:
          break;
        case 10: {
          (void)qa_q2_msg_read_byte(&message);
          char scratch[2048];
          (void)qa_q2_msg_read_string(&message, scratch, sizeof scratch);
          break;
        }
        case 11:
        case 15: {
          char scratch[2048];
          (void)qa_q2_msg_read_string(&message, scratch, sizeof scratch);
          break;
        }
        case 12: {
          double recorded_version = (double)qa_q2_msg_read_long(&message);
          qa_q2_message_check_read(&message);
          int64_t body = message.readcount;
          qa_q2_protocol_identity protocol;
          recorded_protocol(recorded_version, &message, &protocol);
          qa_q2_wire_codec wire;
          if (!qa_q2_wire_codec_init(&wire, protocol)) {
            qa_q2_message_destroy(&message);
            qa_q2_demo_records_free(records, record_count);
            return false;
          }
          if (!qa_q2_wire_codec_begin(&wire, records[r].bytes, records[r].bytes_length)) {
            qa_q2_wire_codec_destroy(&wire);
            qa_q2_message_destroy(&message);
            qa_q2_demo_records_free(records, record_count);
            return false;
          }
          wire.message.readcount = body;
          qa_q2_server_data_result data;
          wire.selected_codec->read_server_data(wire.selected_codec, &data);
          bool serverrecord = recorded_version == 34 &&
                              (size_t)(body + 4) < records[r].bytes_length &&
                              records[r].bytes[body + 4] == 2;
          if (serverrecord) {
            qa_q2_wire_codec_destroy(&wire);
            qa_q2_message_destroy(&message);
            qa_q2_demo_records_free(records, record_count);
            qa_q2_throw_error(
                "Native serverrecord footage has no player view; use the Q2 server-demo reader, not ordinary demo playback");
          }
          qa_q2_wire_codec_finish(&wire);
          out->recorded_version = recorded_version;
          out->protocol = protocol;
          out->data = data;
          qa_q2_wire_codec_destroy(&wire);
          qa_q2_message_destroy(&message);
          qa_q2_demo_records_free(records, record_count);
          (void)free_records;
          return true;
        }
        default:
          qa_q2_message_destroy(&message);
          qa_q2_demo_records_free(records, record_count);
          qa_q2_throw_error("Q2 demo preamble opcode %d requires serverdata first",
                            (int)opcode);
      }
      qa_q2_message_check_read(&message);
    }
    qa_q2_message_destroy(&message);
  }
  qa_q2_demo_records_free(records, record_count);
  qa_q2_throw_error("Q2 demo has no serverdata");
}
