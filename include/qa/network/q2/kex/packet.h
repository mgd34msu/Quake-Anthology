/*
 * Port of src/network/q2/kex/packet.ts. Reader/writer are
 * caller-allocated; strings are malloc'd (fatal UTF-8). Kind -1 is
 * TS null. GPL-2.0-or-later.
 */
#ifndef QA_NETWORK_Q2_KEX_PACKET_H
#define QA_NETWORK_Q2_KEX_PACKET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define QA_KEX_DATAGRAM_BYTES 1400
#define QA_KEX_MESSAGE_BYTES (1024 * 1024)

/* TS KexReader (bytes borrowed). */
typedef struct qa_kex_packet_reader {
  const uint8_t *bytes;
  size_t length;
  size_t position;
} qa_kex_packet_reader;

void qa_kex_packet_reader_init(qa_kex_packet_reader *reader, const uint8_t *bytes,
                               size_t length);
size_t qa_kex_packet_reader_remaining(const qa_kex_packet_reader *reader);
int32_t qa_kex_packet_reader_byte(qa_kex_packet_reader *reader);
uint64_t qa_kex_packet_reader_integer(qa_kex_packet_reader *reader);
char *qa_kex_packet_reader_string(qa_kex_packet_reader *reader);
void qa_kex_packet_reader_end(const qa_kex_packet_reader *reader);

/* TS KexWriter (bytes grow dynamically; finish hands ownership). */
typedef struct qa_kex_packet_writer {
  uint8_t *bytes;
  size_t length, capacity;
} qa_kex_packet_writer;

void qa_kex_packet_writer_init(qa_kex_packet_writer *writer);
void qa_kex_packet_writer_destroy(qa_kex_packet_writer *writer);
/* False only on OOM; invalid values abort like TS. */
bool qa_kex_packet_writer_byte(qa_kex_packet_writer *writer, double value);
bool qa_kex_packet_writer_integer(qa_kex_packet_writer *writer, uint64_t value);
bool qa_kex_packet_writer_string(qa_kex_packet_writer *writer, const char *value);
uint8_t *qa_kex_packet_writer_finish(qa_kex_packet_writer *writer, size_t *out_length);

/* TS KexPacket (payload borrowed on write, owned on read). */
typedef struct qa_kex_packet {
  double flags;
  double sequence;
  double reliable;
  int32_t kind; /* -1 is TS null */
  uint8_t *payload;
  size_t payload_length;
} qa_kex_packet;

void qa_kex_packet_free(qa_kex_packet *packet);
/* TS readKexPacket / writeKexPacket (malloc'd; NULL only on OOM). */
bool qa_kex_read_packet(const uint8_t *bytes, size_t length, qa_kex_packet *out);
uint8_t *qa_kex_write_packet(const qa_kex_packet *packet, size_t *out_length);
/* TS kexText / readKexText. */
uint8_t *qa_kex_text_bytes(const char *value, size_t *out_length);
char *qa_kex_read_text(const uint8_t *value, size_t length);

#endif
