/* Port of src/app/bootstrap/server-master-list.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/server_master_list.h"
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/server_browser_addresses.h"
#include "qa/foundation/support.h"
#include "qa/input/support.h"
char **qa_app_parse_server_master_list(const uint8_t *bytes, size_t length, size_t *out_count) {
  if (length > 1024 * 1024) qa_range_error("Master list exceeds 1 MiB");
  /* TextDecoder fatal UTF-8: reject invalid sequences (overlong/surrogate
   * checks simplified to continuation/lead validation). */
  for (size_t i = 0; i < length;) {
    uint8_t c = bytes[i];
    if (c < 0x80) { i++; continue; }
    size_t need = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : c >= 0xc0 ? 1 : 0;
    if (need == 0 || i + need >= length + 1 || i + need > length - 1) qa_range_error("Invalid master list encoding");
    for (size_t j = 1; j <= need; j++) if ((bytes[i + j] & 0xc0) != 0x80) qa_range_error("Invalid master list encoding");
    i += need + 1;
  }
  char *text = malloc(length + 1);
  if (!text) qa_range_error("Out of memory");
  memcpy(text, bytes, length); text[length] = '\0';
  char **out = NULL; size_t n = 0, cap = 0;
  char *cursor = text;
  for (;;) {
    char *nl = strchr(cursor, '\n');
    size_t line_len = nl ? (size_t)(nl - cursor) : strlen(cursor);
    if (line_len && cursor[line_len - 1] == '\r') line_len--;
    char *line = qa_input_dup_range(cursor, line_len);
    if (!line) qa_range_error("Out of memory");
    char *trimmed = qa_input_js_trim(line);
    free(line);
    if (!trimmed) qa_range_error("Out of memory");
    if (trimmed[0] != '\0' && trimmed[0] != '#') {
      bool dup = false;
      for (size_t i = 0; i < n; i++) if (strcmp(out[i], trimmed) == 0) dup = true;
      if (!dup) {
        if (n == cap) { cap = cap ? cap * 2 : 16; out = realloc(out, cap * sizeof *out); if (!out) qa_range_error("Out of memory"); }
        out[n++] = trimmed;
      } else free(trimmed);
    } else free(trimmed);
    if (!nl) break;
    cursor = nl + 1;
  }
  free(text);
  if (n > 4096) qa_range_error("Master list exceeds 4096 servers");
  for (size_t i = 0; i < n; i++) qa_app_direct_server_text(out[i]);
  *out_count = n;
  return out;
}
char **qa_app_fetch_server_master_list(qa_app_master_list_host host, const char *url, size_t *out_count) {
  if (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0)
    qa_range_error("Master lists require HTTP or HTTPS");
  long status = 0; uint8_t *body = NULL; size_t length = 0; bool has_body = false, byte_stream = true;
  if (!host.fetch(host.ctx, url, &status, &body, &length, &has_body, &byte_stream))
    qa_range_error("Out of memory");
  if (status < 200 || status > 299) { free(body); qa_range_error("Master list returned HTTP %ld", status); }
  if (!has_body) { free(body); qa_range_error("Master list has no response body"); }
  if (!byte_stream) { free(body); qa_range_error("Master list response is not a byte stream"); }
  if (length > 1024 * 1024) { free(body); qa_range_error("Master list exceeds 1 MiB"); }
  char **r = qa_app_parse_server_master_list(body, length, out_count);
  free(body);
  return r;
}
