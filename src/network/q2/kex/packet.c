/* Port of src/network/q2/kex/packet.ts. GPL-2.0-or-later. */
#include "qa/network/q2/kex/packet.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/network/q2/constants.h"
#include "qa/network/q2/handshake.h"

static bool utf8_ok(const uint8_t *bytes, size_t length) {
  size_t i = 0;
  while (i < length) {
    uint8_t c = bytes[i];
    if (c < 0x80) { i++; continue; }
    int need;
    uint32_t cp;
    if ((c & 0xe0) == 0xc0) { need = 1; cp = c & 0x1f; if (cp == 0) return false; }
    else if ((c & 0xf0) == 0xe0) { need = 2; cp = c & 0x0f; }
    else if ((c & 0xf8) == 0xf0) { need = 3; cp = c & 0x07; }
    else return false;
    if (i + (size_t)need >= length) return false;
    for (int k = 0; k < need; k++) {
      uint8_t d = bytes[i + 1 + (size_t)k];
      if ((d & 0xc0) != 0x80) return false;
      cp = (cp << 6) | (uint32_t)(d & 0x3f);
    }
    if ((cp >= 0xd800 && cp <= 0xdfff) || cp > 0x10ffff) return false;
    i += (size_t)need + 1;
  }
  return true;
}

void qa_kex_packet_reader_init(qa_kex_packet_reader *reader, const uint8_t *bytes,
                               size_t length) {
  reader->bytes = bytes;
  reader->length = length;
  reader->position = 0;
}
size_t qa_kex_packet_reader_remaining(const qa_kex_packet_reader *reader) {
  return reader->length - reader->position;
}
int32_t qa_kex_packet_reader_byte(qa_kex_packet_reader *reader) {
  if (reader->position >= reader->length) qa_q2_throw_error("Truncated KEX message");
  return reader->bytes[reader->position++];
}
uint64_t qa_kex_packet_reader_integer(qa_kex_packet_reader *reader) {
  uint64_t value = 0;
  for (int shift = 0; shift < 70; shift += 7) {
    int32_t byte = qa_kex_packet_reader_byte(reader);
    if (shift == 63 && byte > 1) qa_q2_throw_error("Invalid KEX integer");
    value |= (uint64_t)(byte & 127) << shift;
    if ((byte & 128) == 0) return value;
  }
  qa_q2_throw_error("Invalid KEX integer");
}
char *qa_kex_packet_reader_string(qa_kex_packet_reader *reader) {
  uint64_t length = qa_kex_packet_reader_integer(reader);
  if (length > (uint64_t)qa_kex_packet_reader_remaining(reader)) {
    qa_q2_throw_error("Truncated KEX string");
  }
  const uint8_t *start = reader->bytes + reader->position;
  reader->position += (size_t)length;
  if (!utf8_ok(start, (size_t)length)) qa_q2_throw_utf8_error();
  char *out = (char *)malloc((size_t)length + 1);
  if (out != NULL) {
    memcpy(out, start, (size_t)length);
    out[length] = '\0';
  }
  return out;
}
void qa_kex_packet_reader_end(const qa_kex_packet_reader *reader) {
  if (qa_kex_packet_reader_remaining(reader) != 0) qa_q2_throw_error("Trailing KEX data");
}

void qa_kex_packet_writer_init(qa_kex_packet_writer *writer) {
  writer->bytes = NULL;
  writer->length = 0;
  writer->capacity = 0;
}
void qa_kex_packet_writer_destroy(qa_kex_packet_writer *writer) {
  free(writer->bytes);
  writer->bytes = NULL;
  writer->length = 0;
  writer->capacity = 0;
}
static bool push_byte(qa_kex_packet_writer *writer, uint8_t value) {
  if (writer->length == writer->capacity) {
    size_t next = writer->capacity > 0 ? writer->capacity * 2 : 64;
    uint8_t *grown = (uint8_t *)realloc(writer->bytes, next);
    if (grown == NULL) return false;
    writer->bytes = grown;
    writer->capacity = next;
  }
  writer->bytes[writer->length++] = value;
  return true;
}
bool qa_kex_packet_writer_byte(qa_kex_packet_writer *writer, double value) {
  if (value != trunc(value) || value < 0 || value > 255) qa_range_error("Invalid KEX byte");
  return push_byte(writer, (uint8_t)value);
}
bool qa_kex_packet_writer_integer(qa_kex_packet_writer *writer, uint64_t value) {
  do {
    uint8_t byte = (uint8_t)(value & 127u);
    value >>= 7;
    if (!push_byte(writer, (uint8_t)(byte | (value == 0 ? 0 : 128)))) return false;
  } while (value != 0);
  return true;
}
bool qa_kex_packet_writer_string(qa_kex_packet_writer *writer, const char *value) {
  const unsigned char *bytes = (const unsigned char *)(value != NULL ? value : "");
  size_t n = strlen((const char *)bytes);
  if (!qa_kex_packet_writer_integer(writer, (uint64_t)n)) return false;
  for (size_t i = 0; i < n; i++) {
    if (!push_byte(writer, bytes[i])) return false;
  }
  return true;
}
uint8_t *qa_kex_packet_writer_finish(qa_kex_packet_writer *writer, size_t *out_length) {
  uint8_t *out = writer->bytes;
  if (out == NULL) out = (uint8_t *)malloc(1);
  if (out_length != NULL) *out_length = writer->length;
  writer->bytes = NULL;
  writer->length = 0;
  writer->capacity = 0;
  return out;
}

void qa_kex_packet_free(qa_kex_packet *packet) {
  free(packet->payload);
  memset(packet, 0, sizeof *packet);
}

static uint16_t load_u16be(const uint8_t *bytes) {
  /* TS DataView getUint16 defaults to big-endian here. */
  return (uint16_t)(((uint32_t)bytes[0] << 8) | bytes[1]);
}
static void store_u16be(uint8_t *bytes, uint16_t value) {
  bytes[0] = (uint8_t)((value >> 8) & 0xffu);
  bytes[1] = (uint8_t)(value & 0xffu);
}

bool qa_kex_read_packet(const uint8_t *bytes, size_t length, qa_kex_packet *out) {
  memset(out, 0, sizeof *out);
  if (length < 3 || length > QA_KEX_DATAGRAM_BYTES) {
    qa_q2_throw_error("Invalid KEX datagram size");
  }
  uint16_t header = load_u16be(bytes);
  if ((size_t)(header >> 4) != length) qa_q2_throw_error("Invalid KEX datagram length");
  int32_t flags = header & 15;
  bool sequential = (flags & 2) != 0;
  bool continued = (flags & 8) != 0;
  size_t offset = sequential ? 6 : 2;
  if (((flags & 1) != 0 && !sequential) || length < offset + (continued ? 0 : 1)) {
    qa_q2_throw_error("Invalid KEX datagram flags");
  }
  size_t payload_at = offset + (continued ? 0 : 1);
  size_t payload_len = length - payload_at;
  uint8_t *payload = (uint8_t *)malloc(payload_len > 0 ? payload_len : 1);
  if (payload == NULL) return false;
  if (payload_len > 0) memcpy(payload, bytes + payload_at, payload_len);
  out->flags = (double)flags;
  out->sequence = sequential ? (double)load_u16be(bytes + 2) : 0;
  out->reliable = sequential ? (double)load_u16be(bytes + 4) : 0;
  out->kind = continued ? -1 : (offset < length ? (int32_t)bytes[offset] : -1);
  out->payload = payload;
  out->payload_length = payload_len;
  return true;
}

uint8_t *qa_kex_write_packet(const qa_kex_packet *packet, size_t *out_length) {
  int32_t flags = (int32_t)packet->flags;
  bool sequential = (flags & 2) != 0;
  bool continued = (flags & 8) != 0;
  size_t offset = sequential ? 6 : 2;
  size_t total = offset + (continued ? 0 : 1) + packet->payload_length;
  if (total > QA_KEX_DATAGRAM_BYTES || packet->flags < 0 || packet->flags > 15 ||
      continued != (packet->kind < 0)) {
    qa_q2_throw_error("Invalid KEX packet");
  }
  uint8_t *bytes = (uint8_t *)calloc(total > 0 ? total : 1, 1);
  if (bytes == NULL) return NULL;
  store_u16be(bytes, (uint16_t)((total << 4) | (size_t)flags));
  if (sequential) {
    store_u16be(bytes + 2, (uint16_t)(int32_t)packet->sequence);
    store_u16be(bytes + 4, (uint16_t)(int32_t)packet->reliable);
  }
  if (!continued && packet->kind >= 0) bytes[offset] = (uint8_t)packet->kind;
  if (packet->payload_length > 0) {
    memcpy(bytes + offset + (continued ? 0 : 1), packet->payload, packet->payload_length);
  }
  if (out_length != NULL) *out_length = total;
  return bytes;
}

uint8_t *qa_kex_text_bytes(const char *value, size_t *out_length) {
  const char *text = value != NULL ? value : "";
  size_t n = strlen(text);
  uint8_t *out = (uint8_t *)malloc(n > 0 ? n : 1);
  if (out == NULL) return NULL;
  if (n > 0) memcpy(out, text, n);
  if (out_length != NULL) *out_length = n;
  return out;
}
char *qa_kex_read_text(const uint8_t *value, size_t length) {
  if (!utf8_ok(value, length)) qa_q2_throw_utf8_error();
  char *out = (char *)malloc(length + 1);
  if (out != NULL) {
    if (length > 0) memcpy(out, value, length);
    out[length] = '\0';
  }
  return out;
}
