/* Port of src/network/q3/connectionless.ts. GPL-2.0-or-later. */
#include "qa/network/q3/connectionless.h"
#include "qa/core/binary/index.h"
#include "qa/core/commands/text.h"
#include "qa/foundation/support.h"
#include "qa/network/q3/huffman.h"
#include "qa/network/q3/message.h"
#include <stdlib.h>
#include <string.h>
#define MAX_PACKET (QA_Q3_MAX_MESSAGE_LENGTH - 1)
#define CONNECT_OFFSET 12
#define MAX_LINE_LENGTH 1023
#define MAX_CONNECT_INFO 1013
static const uint8_t CONNECT_WORD[7] = {99,111,110,110,101,99,116};
static void check_latin1_len(const char *text, size_t max) {
  /* C bytes are already 0..255; length is strlen (cuts at NUL like TS cString). */
  if (strlen(text) > max) qa_range_error("Connectionless text exceeds %zu source bytes", max);
}
bool qa_q3_encode_connectionless_text(const char *text, uint8_t **out, size_t *out_len) {
  check_latin1_len(text, MAX_PACKET - 4);
  size_t n = strlen(text);
  *out = malloc(n + 4);
  if (!*out) return false;
  memset(*out, 255, 4);
  memcpy(*out + 4, text, n);
  *out_len = n + 4; return true;
}
bool qa_q3_encode_connect(const char *userinfo, uint8_t **out, size_t *out_len) {
  if (strchr(userinfo, '"') || strchr(userinfo, '\n'))
    qa_range_error("Connect userinfo cannot contain a quote or newline");
  check_latin1_len(userinfo, MAX_CONNECT_INFO);
  size_t n = strlen(userinfo);
  char *text = malloc(n + 16);
  if (!text) return false;
  memcpy(text, "connect \"", 9); memcpy(text + 9, userinfo, n); text[9+n] = '"'; text[10+n] = 0;
  uint8_t *packet = NULL; size_t plen = 0;
  if (!qa_q3_encode_connectionless_text(text, &packet, &plen)) { free(text); return false; }
  free(text);
  uint8_t *compressed = NULL; size_t clen = 0;
  if (!qa_q3_compress_adaptive(packet + CONNECT_OFFSET, plen - CONNECT_OFFSET, &compressed, &clen)) { free(packet); return false; }
  *out = malloc(CONNECT_OFFSET + clen);
  if (!*out) { free(packet); free(compressed); return false; }
  memcpy(*out, packet, CONNECT_OFFSET);
  memcpy(*out + CONNECT_OFFSET, compressed, clen);
  *out_len = CONNECT_OFFSET + clen;
  free(packet); free(compressed);
  return true;
}
/* Native x86 Cmd_TokenizeString signed-char whitespace normalization. */
static char *command_whitespace(const char *line) {
  size_t n = strlen(line);
  char *norm = malloc(n + 2);
  if (!norm) return NULL;
  size_t o = 0; int mode = 0; /* 0 regular 1 quoted 2 comment */
  for (size_t i = 0; i < n; i++) {
    char ch = line[i];
    if (mode == 2) {
      norm[o++] = ch;
      if (strncmp(line + i, "*/", 2) == 0) { norm[o++] = '/'; i++; mode = 0; }
    } else if (mode == 1) {
      norm[o++] = ch;
      if (ch == '"') mode = 0;
    } else {
      if (strncmp(line + i, "//", 2) == 0) { memcpy(norm + o, line + i, n - i); o += n - i; break; }
      if (strncmp(line + i, "/*", 2) == 0) { norm[o++] = '/'; norm[o++] = '*'; i++; mode = 2; }
      else {
        norm[o++] = (unsigned char)ch > 127 ? ' ' : ch;
        if (ch == '"') mode = 1;
      }
    }
  }
  norm[o] = 0;
  return norm;
}
typedef struct expand_scope { const uint8_t *bytes; size_t len; uint8_t *expanded; size_t length; bool ok; } expand_scope;
static void expand_body(void *ctx) {
  expand_scope *scope = ctx;
  scope->ok = qa_q3_decompress_adaptive(scope->bytes, scope->len, QA_Q3_MAX_MESSAGE_LENGTH - CONNECT_OFFSET, &scope->expanded, &scope->length);
}
bool qa_q3_decode_connectionless(const uint8_t *packet, size_t len, qa_q3_connectionless_receiver receiver,
                                 const char *source, qa_q3_connectionless_packet *out) {
  memset(out, 0, sizeof *out);
  const char *src = source ? source : "<connectionless>";
  if (len < 4) qa_binary_error(src, 0, "Truncated connectionless marker");
  if (len > MAX_PACKET) qa_binary_error(src, 0, "Connectionless datagram exceeds source receive limit");
  for (int i = 0; i < 4; i++) if (packet[i] != 255) qa_binary_error(src, i, "Invalid connectionless marker");
  uint8_t *data = malloc(len ? len : 1);
  if (!data) return false;
  memcpy(data, packet, len);
  size_t dlen = len;
  qa_q3_connectionless_compression compression = QA_Q3_COMPRESSION_NONE;
  bool is_connect = len >= 4 + 7 && memcmp(packet + 4, CONNECT_WORD, 7) == 0;
  if (receiver == QA_Q3_CONNLESS_SERVER && is_connect && len > CONNECT_OFFSET) {
    /* TS rewraps any decompression Error as BinaryError at CONNECT_OFFSET. */
    expand_scope scope = { packet + CONNECT_OFFSET, len - CONNECT_OFFSET, NULL, 0, false };
    qa_caught_error caught;
    if (!qa_try(expand_body, &scope, &caught)) {
      free(data);
      qa_binary_error(src, CONNECT_OFFSET, "Malformed compressed connectionless payload: %s", caught.message);
    }
    if (!scope.ok) { free(data); return false; }
    uint8_t *expanded = scope.expanded; size_t elen = scope.length;
    uint8_t *nd = malloc(CONNECT_OFFSET + elen);
    if (!nd) { free(data); free(expanded); return false; }
    memcpy(nd, packet, CONNECT_OFFSET);
    memcpy(nd + CONNECT_OFFSET, expanded, elen);
    free(expanded); free(data); data = nd; dlen = CONNECT_OFFSET + elen;
    compression = QA_Q3_COMPRESSION_ADAPTIVE;
  }
  char line[MAX_LINE_LENGTH + 1]; size_t llen = 0;
  size_t offset = 4;
  qa_q3_line_ending ending = QA_Q3_LINE_LIMIT;
  while (llen < MAX_LINE_LENGTH) {
    if (offset >= dlen) { offset++; ending = QA_Q3_LINE_END; break; } /* TS reads undefined past end, offset++ already done */
    uint8_t byte = data[offset++];
    if (byte == 0) { ending = QA_Q3_LINE_NUL; break; }
    if (byte == 10) { ending = QA_Q3_LINE_NEWLINE; break; }
    line[llen++] = (char)(byte == 37 ? 46 : byte);
  }
  line[llen] = 0;
  char *normalized = command_whitespace(line);
  if (!normalized) { free(data); return false; }
  qa_command_tokens tokens;
  if (!qa_tokenize_command(normalized, QA_COMMAND_DIALECT_Q3, QA_COMMAND_TEXT_SOURCE, &tokens)) { free(normalized); free(data); return false; }
  free(normalized);
  out->line = malloc(llen + 1);
  if (!out->line) { qa_command_tokens_destroy(&tokens); free(data); return false; }
  memcpy(out->line, line, llen + 1);
  out->argv_storage = tokens.argv; out->argv_count = tokens.argc;
  free(tokens.args_text);
  out->command = tokens.argc ? tokens.argv[0] : "";
  out->arguments = tokens.argc > 1 ? tokens.argv + 1 : tokens.argv;
  out->argument_count = tokens.argc > 0 ? tokens.argc - 1 : 0;
  size_t payload_off = offset <= dlen ? offset : dlen;
  out->payload_len = dlen - payload_off;
  out->payload = malloc(out->payload_len ? out->payload_len : 1);
  if (!out->payload) { qa_q3_connectionless_packet_destroy(out); free(data); return false; }
  memcpy(out->payload, data + payload_off, out->payload_len);
  out->compression = compression;
  out->line_ending = ending;
  out->read_count = (int64_t)offset;
  free(data);
  return true;
}
void qa_q3_connectionless_packet_destroy(qa_q3_connectionless_packet *p) {
  for (size_t i = 0; i < p->argv_count; i++) free(p->argv_storage[i]);
  free(p->argv_storage); free(p->line); free(p->payload);
  memset(p, 0, sizeof *p);
}
