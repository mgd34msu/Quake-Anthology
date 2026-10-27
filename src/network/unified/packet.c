/* Port of src/network/unified/packet.ts. GPL-2.0-or-later. */
#include "qa/network/unified/packet.h"
#include "qa/foundation/support.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
static int hexval(int c) { if (c >= '0' && c <= '9') return c - '0'; if (c >= 'a' && c <= 'f') return c - 'a' + 10; if (c >= 'A' && c <= 'F') return c - 'A' + 10; return -1; }
void qa_unified_token(const char *token, char out[33]) {
  if (strlen(token) != 32) qa_range_error("Unified channel token must contain 128 bits of hexadecimal");
  for (int i = 0; i < 32; i++) { if (hexval((unsigned char)token[i]) < 0) qa_range_error("Unified channel token must contain 128 bits of hexadecimal"); out[i] = (char)tolower((unsigned char)token[i]); }
  out[32] = '\0';
}
static bool is_uint(int64_t v, int64_t max) { return v >= 0 && v <= max; }
static void put_u32le(uint8_t *b, size_t o, uint32_t v) { b[o]=(uint8_t)v; b[o+1]=(uint8_t)(v>>8); b[o+2]=(uint8_t)(v>>16); b[o+3]=(uint8_t)(v>>24); }
static void put_u16le(uint8_t *b, size_t o, uint16_t v) { b[o]=(uint8_t)v; b[o+1]=(uint8_t)(v>>8); }
static uint32_t get_u32le(const uint8_t *b, size_t o) { return (uint32_t)b[o] | ((uint32_t)b[o+1]<<8) | ((uint32_t)b[o+2]<<16) | ((uint32_t)b[o+3]<<24); }
static uint16_t get_u16le(const uint8_t *b, size_t o) { return (uint16_t)((uint16_t)b[o] | ((uint16_t)b[o+1]<<8)); }
uint8_t *qa_encode_unified_packet(const qa_unified_packet *packet, size_t *out_len) {
  char token[33]; qa_unified_token(packet->token, token);
  if (!is_uint(packet->sequence, QA_UNIFIED_SEQUENCE_MAX) || packet->sequence == 0
      || !is_uint(packet->acknowledged_reliable, QA_UNIFIED_SEQUENCE_MAX) || !is_uint(packet->fragment, 65535))
    qa_range_error("Invalid unified packet sequence");
  if (packet->kind != QA_UNIFIED_ACK) {
    int64_t frags = packet->total_bytes == 0 ? 1 : (packet->fragment_bytes > 0 ? (packet->total_bytes + packet->fragment_bytes - 1) / packet->fragment_bytes : -1);
    if (frags < 1) frags = 1;
    int64_t expect_len = packet->total_bytes - packet->fragment * packet->fragment_bytes;
    if (expect_len < 0) expect_len = 0;
    if (expect_len > packet->fragment_bytes) expect_len = packet->fragment_bytes;
    if (!is_uint(packet->required_reliable_sequence, QA_UNIFIED_SEQUENCE_MAX) || !is_uint(packet->total_bytes, QA_UNIFIED_SEQUENCE_MAX)
        || !is_uint(packet->fragment_bytes, 65507 - QA_UNIFIED_PACKET_HEADER_BYTES) || packet->fragment_bytes == 0
        || packet->fragments != frags || packet->fragments > 65535 || packet->fragment >= packet->fragments
        || (int64_t)packet->length != expect_len
        || (packet->kind == QA_UNIFIED_RELIABLE && packet->required_reliable_sequence != 0))
      qa_range_error("Invalid unified packet fragment");
  }
  size_t n = QA_UNIFIED_PACKET_HEADER_BYTES + (packet->kind == QA_UNIFIED_ACK ? 0 : packet->length);
  uint8_t *b = calloc(n, 1); if (!b) return NULL;
  b[0]=81; b[1]=84; b[2]=85; b[3]=67; b[4]=1;
  b[5] = packet->kind == QA_UNIFIED_ACK ? 0 : packet->kind == QA_UNIFIED_RELIABLE ? 1 : 2;
  for (int i = 0; i < 16; i++) b[8+(size_t)i] = (uint8_t)(hexval(token[i*2]) * 16 + hexval(token[i*2+1]));
  put_u32le(b, 24, (uint32_t)packet->sequence);
  put_u32le(b, 28, (uint32_t)packet->acknowledged_reliable);
  put_u16le(b, 44, (uint16_t)packet->fragment);
  if (packet->kind != QA_UNIFIED_ACK) {
    put_u32le(b, 32, (uint32_t)packet->required_reliable_sequence);
    put_u32le(b, 36, (uint32_t)packet->total_bytes);
    put_u32le(b, 40, (uint32_t)packet->fragment_bytes);
    put_u16le(b, 46, (uint16_t)packet->fragments);
    memcpy(b + QA_UNIFIED_PACKET_HEADER_BYTES, packet->payload, packet->length);
  }
  *out_len = n; return b;
}
int qa_decode_unified_packet(const uint8_t *bytes, size_t length, qa_unified_packet *out) {
  if (length < QA_UNIFIED_PACKET_HEADER_BYTES || length > 65507) return 0;
  uint32_t magic = ((uint32_t)bytes[0]<<24)|((uint32_t)bytes[1]<<16)|((uint32_t)bytes[2]<<8)|bytes[3];
  if (magic != 0x51545543u || bytes[4] != 1 || get_u16le(bytes, 6) != 0) return 0;
  int kind = bytes[5];
  int64_t sequence = get_u32le(bytes, 24), ack = get_u32le(bytes, 28), fragment = get_u16le(bytes, 44);
  if (kind > 2 || sequence == 0) return 0;
  memset(out, 0, sizeof *out);
  static const char hexd[] = "0123456789abcdef";
  for (int i = 0; i < 16; i++) { out->token[i*2] = hexd[bytes[8+(size_t)i] >> 4]; out->token[i*2+1] = hexd[bytes[8+(size_t)i] & 15]; }
  out->token[32] = '\0';
  int64_t required = get_u32le(bytes, 32), total = get_u32le(bytes, 36), fbytes = get_u32le(bytes, 40), frags = get_u16le(bytes, 46);
  out->sequence = sequence; out->acknowledged_reliable = ack; out->fragment = fragment;
  if (kind == 0) {
    if (length != QA_UNIFIED_PACKET_HEADER_BYTES || required || total || fbytes || frags) return 0;
    out->kind = QA_UNIFIED_ACK; return 1;
  }
  int64_t want_frags = fbytes > 0 ? (total + fbytes - 1) / fbytes : -1;
  if (want_frags < 1) want_frags = 1;
  int64_t expect = total - fragment * fbytes; if (expect < 0) expect = 0; if (expect > fbytes) expect = fbytes;
  if (fbytes == 0 || fbytes > 65507 - QA_UNIFIED_PACKET_HEADER_BYTES || frags != want_frags || fragment >= frags
      || (int64_t)(length - QA_UNIFIED_PACKET_HEADER_BYTES) != expect || (kind == 1 && required != 0)) return 0;
  out->kind = kind == 1 ? QA_UNIFIED_RELIABLE : QA_UNIFIED_FRAME;
  out->required_reliable_sequence = required; out->total_bytes = total;
  out->fragment_bytes = fbytes; out->fragments = frags;
  out->payload = (uint8_t *)bytes + QA_UNIFIED_PACKET_HEADER_BYTES;
  out->length = length - QA_UNIFIED_PACKET_HEADER_BYTES;
  return 1;
}
