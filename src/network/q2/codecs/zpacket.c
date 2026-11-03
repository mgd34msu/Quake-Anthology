/* Port of src/network/q2/codecs/zpacket.ts. GPL-2.0-or-later. */
#include "qa/network/q2/codecs/zpacket.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "qa/foundation/support.h"

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

/* Node inflateSync/inflateRawSync with { maxOutputLength, finishFlush }. Node grows its output
 * chunk by chunk and throws RangeError once the total passes maxOutputLength, so the bound caps
 * the work a hostile stream can cause; the error texts are Node's. */
typedef struct inflate_scope {
  z_stream stream;
  bool open;
  uint8_t *out;
} inflate_scope;
static void inflate_scope_release(void *arg) {
  inflate_scope *scope = (inflate_scope *)arg;
  if (scope->open) inflateEnd(&scope->stream);
  scope->open = false;
  free(scope->out);
  scope->out = NULL;
}
uint8_t *qa_q2_inflate_sync(const uint8_t *bytes, size_t length, bool raw, size_t max_output,
                            int flush, size_t *out_length) {
  inflate_scope scope;
  memset(&scope, 0, sizeof scope);
  if (inflateInit2(&scope.stream, raw ? -MAX_WBITS : MAX_WBITS) != Z_OK) return NULL;
  scope.open = true;
  qa_cleanup node;
  qa_cleanup_push(&node, inflate_scope_release, &scope);
  size_t capacity = 16384, produced = 0;
  scope.out = (uint8_t *)malloc(capacity);
  if (scope.out == NULL) {
    qa_cleanup_pop(&node);
    inflate_scope_release(&scope);
    return NULL;
  }
  scope.stream.next_in = (Bytef *)bytes;
  scope.stream.avail_in = (uInt)length;
  for (;;) {
    if (produced == capacity) {
      size_t next = capacity * 2;
      uint8_t *grown = (uint8_t *)realloc(scope.out, next);
      if (grown == NULL) {
        qa_cleanup_pop(&node);
        inflate_scope_release(&scope);
        return NULL;
      }
      scope.out = grown;
      capacity = next;
    }
    scope.stream.next_out = scope.out + produced;
    scope.stream.avail_out = (uInt)(capacity - produced);
    int status = inflate(&scope.stream, flush);
    produced = (size_t)scope.stream.total_out;
    if (produced > max_output) {
      qa_raise("RangeError", 0, 0, "Cannot create a Buffer larger than %zu bytes", max_output);
    }
    if (status == Z_STREAM_END) break;
    if (status == Z_NEED_DICT) qa_raise("Error", 0, 0, "Missing dictionary");
    if (status == Z_DATA_ERROR || status == Z_STREAM_ERROR || status == Z_MEM_ERROR) {
      qa_raise("Error", 0, 0, "%s",
               scope.stream.msg != NULL ? scope.stream.msg : "zlib error");
    }
    if (scope.stream.avail_out != 0) {
      /* Input exhausted before the stream ended. */
      if (flush == Z_FINISH) qa_raise("Error", 0, 0, "unexpected end of file");
      break;
    }
  }
  qa_cleanup_pop(&node);
  inflateEnd(&scope.stream);
  if (out_length != NULL) *out_length = produced;
  return scope.out;
}

typedef struct zpacket_scope {
  size_t expected;
  const uint8_t *compressed;
  size_t compressed_length;
  uint8_t *bytes;
  size_t length;
} zpacket_scope;
static void zpacket_inflate(void *arg) {
  zpacket_scope *scope = (zpacket_scope *)arg;
  scope->bytes = qa_q2_inflate_sync(scope->compressed, scope->compressed_length, true,
                                    scope->expected != 0 ? scope->expected : 1, Z_FINISH,
                                    &scope->length);
  if (scope->bytes != NULL && scope->length != scope->expected) {
    free(scope->bytes);
    scope->bytes = NULL;
    qa_raise("Error", 0, 0, "inflated length differs from header");
  }
}
static void free_arg(void *arg) { free(arg); }

uint8_t *qa_q2_read_zpacket_payload(qa_q2_sizebuf *msg, size_t *out_length) {
  int32_t compressed_len = qa_q2_msg_read_word(msg);
  int32_t expected_length = qa_q2_msg_read_word(msg);
  qa_q2_message_check_read(msg);
  uint8_t *compressed =
      (uint8_t *)malloc(compressed_len > 0 ? (size_t)compressed_len : 1);
  if (compressed == NULL) return NULL;
  qa_cleanup node;
  qa_cleanup_push(&node, free_arg, compressed);
  qa_q2_msg_read_data(msg, compressed,
                      compressed_len > 0 ? (size_t)compressed_len : 0, compressed_len);
  qa_q2_message_check_read(msg);
  /* TS catches any inflate failure and rethrows it as ComError(ERR_DROP, ...). */
  zpacket_scope scope = {(size_t)expected_length, compressed,
                         compressed_len > 0 ? (size_t)compressed_len : 0, NULL, 0};
  qa_caught_error caught;
  bool ok = qa_try(zpacket_inflate, &scope, &caught);
  qa_cleanup_pop(&node);
  free(compressed);
  if (!ok) qa_q2_com_error_throw(QA_Q2_ERR_DROP, "zpacket: raw inflate failed: %s", caught.message);
  if (scope.bytes == NULL) return NULL;
  if (out_length != NULL) *out_length = scope.length;
  return scope.bytes;
}
