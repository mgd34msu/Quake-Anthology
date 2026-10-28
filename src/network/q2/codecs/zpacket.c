/* Port of src/network/q2/codecs/zpacket.ts. GPL-2.0-or-later. */
#include "qa/network/q2/codecs/zpacket.h"

#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "qa/network/q2/constants.h"

#define ZPACKET_HEADER_SIZE 5

uint8_t *qa_q2_try_wrap_zpacket(const uint8_t *data, size_t data_length, int32_t len,
                                int32_t max_out, size_t *out_length) {
  if (len < QA_Q2_ZPACKET_MIN_COMPRESS_SIZE) return NULL;
  if (len > 0xffff) return NULL;
  if (len > 0 && data_length > 0 && data[0] == QA_Q2_SVC_SERVERDATA) return NULL;
  uLongf bound = compressBound((uLong)len);
  uint8_t *compressed = (uint8_t *)malloc(bound > 0 ? bound : 1);
  if (compressed == NULL) return NULL;
  z_stream stream;
  memset(&stream, 0, sizeof stream);
  if (deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8,
                   Z_DEFAULT_STRATEGY) != Z_OK) {
    free(compressed);
    return NULL;
  }
  stream.next_in = (Bytef *)data;
  stream.avail_in = (uInt)len;
  stream.next_out = compressed;
  stream.avail_out = (uInt)bound;
  int status = deflate(&stream, Z_FINISH);
  uLongf compressed_length = stream.total_out;
  deflateEnd(&stream);
  if (status != Z_STREAM_END) {
    free(compressed);
    return NULL;
  }
  if (compressed_length > 0xffffu) {
    free(compressed);
    return NULL;
  }
  size_t total = ZPACKET_HEADER_SIZE + (size_t)compressed_length;
  if (total >= (size_t)len) {
    free(compressed);
    return NULL;
  }
  if ((int64_t)total > (int64_t)max_out) {
    free(compressed);
    return NULL;
  }
  uint8_t *out = (uint8_t *)malloc(total);
  if (out == NULL) {
    free(compressed);
    return NULL;
  }
  out[0] = (uint8_t)(QA_Q2_SVC_ZPACKET & 0xff);
  out[1] = (uint8_t)(compressed_length & 0xffu);
  out[2] = (uint8_t)((compressed_length >> 8) & 0xffu);
  out[3] = (uint8_t)(len & 0xff);
  out[4] = (uint8_t)((len >> 8) & 0xff);
  memcpy(out + ZPACKET_HEADER_SIZE, compressed, (size_t)compressed_length);
  free(compressed);
  if (out_length != NULL) *out_length = total;
  return out;
}

uint8_t *qa_q2_read_zpacket_payload(qa_q2_sizebuf *msg, size_t *out_length) {
  int32_t compressed_len = qa_q2_msg_read_word(msg);
  int32_t expected_length = qa_q2_msg_read_word(msg);
  qa_q2_message_check_read(msg);
  uint8_t *compressed =
      (uint8_t *)malloc(compressed_len > 0 ? (size_t)compressed_len : 1);
  if (compressed == NULL) return NULL;
  qa_q2_msg_read_data(msg, compressed,
                      compressed_len > 0 ? (size_t)compressed_len : 0, compressed_len);
  qa_q2_message_check_read(msg);
  size_t capacity = expected_length != 0 ? (size_t)(expected_length > 0 ? expected_length : 1) : 1;
  uint8_t *out = (uint8_t *)malloc(capacity);
  if (out == NULL) {
    free(compressed);
    return NULL;
  }
  z_stream stream;
  memset(&stream, 0, sizeof stream);
  if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
    const char *m = stream.msg != NULL ? stream.msg : "zlib error";
    free(compressed);
    free(out);
    qa_q2_com_error_throw(QA_Q2_ERR_DROP, "zpacket: raw inflate failed: %s", m);
  }
  stream.next_in = compressed;
  stream.avail_in = compressed_len > 0 ? (uInt)compressed_len : 0;
  stream.next_out = out;
  stream.avail_out = (uInt)capacity;
  int status = inflate(&stream, Z_FINISH);
  uLong produced = stream.total_out;
  const char *zmsg = stream.msg;
  inflateEnd(&stream);
  free(compressed);
  /* TS inflateRawSync throws unless the stream ends within the
   * maxOutputLength bound, so only Z_STREAM_END is success. */
  if (status != Z_STREAM_END) {
    const char *m = zmsg != NULL ? zmsg : "zlib error";
    free(out);
    qa_q2_com_error_throw(QA_Q2_ERR_DROP, "zpacket: raw inflate failed: %s", m);
  }
  if ((int64_t)produced != (int64_t)expected_length) {
    free(out);
    qa_q2_com_error_throw(QA_Q2_ERR_DROP,
                          "zpacket: raw inflate failed: inflated length differs from header");
  }
  if (out_length != NULL) *out_length = (size_t)produced;
  return out;
}
