/* Port of src/network/unified/packet.ts. GPL-2.0-or-later. */
#ifndef QA_NETWORK_UNIFIED_PACKET_H
#define QA_NETWORK_UNIFIED_PACKET_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define QA_UNIFIED_PACKET_HEADER_BYTES 48
#define QA_UNIFIED_SEQUENCE_MAX 0xffffffffLL
typedef enum qa_unified_packet_kind { QA_UNIFIED_ACK, QA_UNIFIED_RELIABLE, QA_UNIFIED_FRAME } qa_unified_packet_kind;
typedef struct qa_unified_packet {
  qa_unified_packet_kind kind;
  char token[33];
  int64_t sequence, acknowledged_reliable, fragment;
  int64_t required_reliable_sequence, total_bytes, fragment_bytes, fragments;
  uint8_t *payload; size_t length; /* decode: borrowed into input; encode: input */
} qa_unified_packet;
/* Validates + lowercases into out[33]. Aborts on bad token. */
void qa_unified_token(const char *token, char out[33]);
/* Malloc'd wire bytes, caller frees. Invalid packet aborts. */
uint8_t *qa_encode_unified_packet(const qa_unified_packet *packet, size_t *out_len);
/* Returns 1 and fills *out (payload borrowed), else 0. */
int qa_decode_unified_packet(const uint8_t *bytes, size_t length, qa_unified_packet *out);
#endif
