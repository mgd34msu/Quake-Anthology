/* Port of src/network/q3/discovery.ts. GPL-2.0-or-later. */
#include "qa/network/q3/discovery.h"
#include "qa/core/commands/text.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/network/q3/adapters.h"
#include "qa/network/q3/connectionless.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static char *xstrdup(const char *s) { size_t n = strlen(s); char *r = malloc(n + 1); if (r) memcpy(r, s, n + 1); return r; }
struct qa_q3_mirror_discovery_wire { char **keywords; size_t keyword_count; int64_t protocol; };
bool qa_q3_discovery_wire_create(const char *const *keywords, size_t keyword_count, int64_t protocol, qa_q3_discovery_wire **out) {
  if (protocol <= 0 || protocol > 0x7fffffff) qa_range_error("Invalid Q3 master protocol");
  for (size_t i = 0; i < keyword_count; i++)
    for (const char *p = keywords[i]; *p; p++)
      if (isspace((unsigned char)*p)) qa_range_error("Q3 master keywords must be source words");
  qa_q3_discovery_wire *w = calloc(1, sizeof *w);
  if (!w) return false;
  w->keywords = calloc(keyword_count ? keyword_count : 1, sizeof(char *));
  if (!w->keywords) { free(w); return false; }
  for (size_t i = 0; i < keyword_count; i++) {
    w->keywords[i] = xstrdup(keywords[i]);
    if (!w->keywords[i]) { qa_q3_discovery_wire_destroy(w); return false; }
    w->keyword_count++;
  }
  w->protocol = protocol;
  *out = w; return true;
}
bool qa_q3_discovery_wire_create_default(qa_q3_discovery_wire **out) { return qa_q3_discovery_wire_create(NULL, 0, 68, out); }
void qa_q3_discovery_wire_destroy(qa_q3_discovery_wire *w) {
  if (!w) return;
  for (size_t i = 0; i < w->keyword_count; i++) free(w->keywords[i]);
  free(w->keywords); free(w);
}
bool qa_q3_discovery_wire_query(const qa_q3_discovery_wire *w, qa_q3_discovery_request_kind kind, const char *challenge, uint8_t **out, size_t *out_len) {
  (void)w;
  const char *word = kind == QA_Q3_DISCOVERY_INFO ? "getinfo" : "getstatus";
  size_t n = strlen(word) + 1 + strlen(challenge) + 1;
  char *buf = malloc(n);
  if (!buf) return false;
  snprintf(buf, n, "%s %s", word, challenge);
  bool ok = qa_q3_encode_connectionless_text(buf, out, out_len);
  free(buf); return ok;
}
bool qa_q3_discovery_wire_master_query(const qa_q3_discovery_wire *w, uint8_t **out, size_t *out_len) {
  size_t n = 32;
  for (size_t i = 0; i < w->keyword_count; i++) n += strlen(w->keywords[i]) + 1;
  char *buf = malloc(n);
  if (!buf) return false;
  int o = snprintf(buf, n, "getservers %lld", (long long)w->protocol);
  for (size_t i = 0; i < w->keyword_count; i++) o += snprintf(buf + o, n - (size_t)o, " %s", w->keywords[i]);
  bool ok = qa_q3_encode_connectionless_text(buf, out, out_len);
  free(buf); return ok;
}
bool qa_q3_discovery_wire_heartbeat(const qa_q3_discovery_wire *w, bool active, uint8_t **out, size_t *out_len) {
  (void)w; (void)active;
  return qa_q3_encode_connectionless_text("heartbeat QuakeArena-1\n", out, out_len);
}
bool qa_q3_decode_master_packet(const uint8_t *bytes, size_t len, qa_q3_mirror_ipv4_address **out, size_t *count, bool *complete) {
  static const char prefix[] = "getserversResponse";
  size_t plen = sizeof prefix - 1;
  if (len < plen + 4) qa_range_error("Not a Q3 master response");
  for (int i = 0; i < 4; i++) if (bytes[i] != 255) qa_range_error("Not a Q3 master response");
  if (memcmp(bytes + 4, prefix, plen) != 0) qa_range_error("Not a Q3 master response");
  qa_q3_mirror_ipv4_address *result = NULL; size_t n = 0, cap = 0;
  size_t cursor = 4 + plen;
  while (cursor < len && bytes[cursor] != 92) cursor++;
  *complete = false;
  while (cursor < len && bytes[cursor] == 92) {
    cursor++;
    if (cursor + 2 < len && bytes[cursor] == 69 && bytes[cursor+1] == 79 && bytes[cursor+2] == 84) { *complete = true; break; }
    if (n >= 256) break;
    if (cursor + 5 >= len) break;
    uint8_t a = bytes[cursor], b = bytes[cursor+1], c = bytes[cursor+2], d = bytes[cursor+3];
    uint8_t high = bytes[cursor+4], low = bytes[cursor+5];
    cursor += 6;
    if (cursor >= len || bytes[cursor] != 92) break;
    if (n == cap) {
      size_t nc = cap ? cap * 2 : 16;
      qa_q3_mirror_ipv4_address *nr = realloc(result, nc * sizeof *nr);
      if (!nr) { free(result); return false; }
      result = nr; cap = nc;
    }
    memset(&result[n], 0, sizeof result[n]);
    result[n].kind = QA_Q3_ADDR_IPV4;
    result[n].host[0] = a; result[n].host[1] = b; result[n].host[2] = c; result[n].host[3] = d;
    result[n].port = (high << 8) | low;
    n++;
  }
  *out = result; *count = n;
  return true;
}
bool qa_q3_decode_master_response(const uint8_t *bytes, size_t len, qa_q3_mirror_ipv4_address **out, size_t *count) {
  bool complete; (void)complete;
  return qa_q3_decode_master_packet(bytes, len, out, count, &complete);
}
static char *payload_text(const uint8_t *bytes, size_t len) {
  char *text = malloc(len + 1);
  if (!text) return NULL;
  size_t n = 0;
  for (size_t i = 0; i < len; i++) {
    if (bytes[i] == 0) break;
    text[n++] = (char)(bytes[i] == 37 || bytes[i] > 127 ? 46 : bytes[i]);
  }
  text[n] = 0; return text;
}
void qa_q3_mirror_server_status_destroy(qa_q3_mirror_server_status *s) {
  free(s->name); free(s->map);
  for (size_t i = 0; i < s->rule_count; i++) { free(s->rules[i].key); free(s->rules[i].value); }
  free(s->rules);
  for (size_t i = 0; i < s->player_detail_count; i++) free(s->player_details[i].name);
  free(s->player_details);
  memset(s, 0, sizeof *s);
}
static bool ci_eq(const char *a, const char *b) {
  while (*a && *b) { if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false; a++; b++; }
  return *a == *b;
}
static bool rule_set(qa_q3_mirror_server_status *s, const char *key, size_t kn, const char *value, size_t vn) {
  for (size_t i = 0; i < s->rule_count; i++) {
    if (strlen(s->rules[i].key) == kn && memcmp(s->rules[i].key, key, kn) == 0) {
      char *nv = malloc(vn + 1); if (!nv) return false;
      memcpy(nv, value, vn); nv[vn] = 0; free(s->rules[i].value); s->rules[i].value = nv; return true;
    }
  }
  qa_q3_mirror_rule *nr = realloc(s->rules, (s->rule_count + 1) * sizeof *nr);
  if (!nr) return false;
  s->rules = nr;
  char *k = malloc(kn + 1), *v = malloc(vn + 1);
  if (!k || !v) { free(k); free(v); return false; }
  memcpy(k, key, kn); k[kn] = 0; memcpy(v, value, vn); v[vn] = 0;
  s->rules[s->rule_count].key = k; s->rules[s->rule_count].value = v; s->rule_count++;
  return true;
}
bool qa_q3_decode_server_status(const uint8_t *bytes, size_t len, char **challenge_out, qa_q3_mirror_server_status *status_out) {
  memset(status_out, 0, sizeof *status_out);
  *challenge_out = NULL;
  qa_q3_connectionless_packet packet;
  if (!qa_q3_decode_connectionless(bytes, len, QA_Q3_CONNLESS_CLIENT, NULL, &packet)) return false;
  bool is_info = ci_eq(packet.command, "infoResponse");
  bool is_status = ci_eq(packet.command, "statusResponse");
  if (!is_info && !is_status) { qa_q3_connectionless_packet_destroy(&packet); qa_range_error("Not a Q3 server browser response"); }
  char *text = payload_text(packet.payload, packet.payload_len);
  qa_q3_connectionless_packet_destroy(&packet);
  if (!text) return false;
  /* lines = text.split("\n"); info = lines[0] */
  char *nl = strchr(text, '\n');
  size_t info_len = nl ? (size_t)(nl - text) : strlen(text);
  char *info = malloc(info_len + 1);
  if (!info) { free(text); return false; }
  memcpy(info, text, info_len); info[info_len] = 0;
  /* rules from split('\\') pairs */
  {
    size_t start_index = (info_len > 0 && info[0] == '\\') ? 1 : 0;
    size_t field = 0, pos = 0;
    const char *ks = NULL; size_t kn = 0;
    for (;;) {
      size_t end = pos; while (end < info_len && info[end] != '\\') end++;
      if (field >= start_index && ((field - start_index) % 2) == 0) { ks = info + pos; kn = end - pos; }
      else if (field >= start_index && ks) {
        if (!rule_set(status_out, ks, kn, info + pos, end - pos)) { free(info); free(text); return false; }
        ks = NULL;
      }
      if (end >= info_len) break;
      pos = end + 1; field++;
    }
  }
  if (is_info) {
    char *p = qa_q3_info_value(info, "protocol");
    int ok = p && qa_native_atoi(p) == 68; free(p);
    if (!ok) { free(info); free(text); qa_q3_mirror_server_status_destroy(status_out); qa_range_error("Server uses another Q3 wire version"); }
  }
  if (is_status && nl) {
    const char *line = nl + 1;
    while (*line) {
      const char *eol = strchr(line, '\n');
      size_t ln = eol ? (size_t)(eol - line) : strlen(line);
      if (ln > 0) {
        char *copy = malloc(ln + 1);
        if (!copy) { free(info); free(text); return false; }
        memcpy(copy, line, ln); copy[ln] = 0;
        qa_command_tokens tokens;
        if (qa_tokenize_command(copy, QA_COMMAND_DIALECT_Q3, QA_COMMAND_TEXT_SOURCE, &tokens)) {
          if (tokens.argc >= 3) {
            qa_q3_mirror_player_detail *nd = realloc(status_out->player_details, (status_out->player_detail_count + 1) * sizeof *nd);
            if (!nd) { qa_command_tokens_destroy(&tokens); free(copy); free(info); free(text); return false; }
            status_out->player_details = nd;
            qa_q3_mirror_player_detail *d = &status_out->player_details[status_out->player_detail_count++];
            d->name = xstrdup(tokens.argv[2]); d->score = qa_native_atoi(tokens.argv[0]); d->ping = qa_native_atoi(tokens.argv[1]);
          }
          qa_command_tokens_destroy(&tokens);
        }
        free(copy);
      }
      if (!eol) break;
      line = eol + 1;
    }
  }
  *challenge_out = qa_q3_info_value(info, "challenge");
  char *host = qa_q3_info_value(info, "hostname");
  if (host && host[0]) status_out->name = host;
  else { free(host); status_out->name = qa_q3_info_value(info, "sv_hostname"); }
  status_out->map = qa_q3_info_value(info, "mapname");
  if (is_status) status_out->players = (int64_t)status_out->player_detail_count;
  else { char *c = qa_q3_info_value(info, "clients"); status_out->players = qa_native_atoi(c ? c : ""); free(c); }
  { char *c = qa_q3_info_value(info, "sv_maxclients"); status_out->max_players = qa_native_atoi(c ? c : ""); free(c); }
  status_out->wire_kind = "source";
  status_out->wire_protocol = qa_q3_protocol;
  free(info); free(text);
  return *challenge_out && status_out->name && status_out->map;
}
bool qa_q3_encode_status(const char *info, const qa_q3_status_player *players, size_t count, uint8_t **out, size_t *out_len) {
  size_t cap = 16384 + strlen(info) + 32;
  char *rows = calloc(cap, 1);
  if (!rows) return false;
  size_t used = 0;
  for (size_t i = 0; i < count; i++) {
    char row[1100];
    int n = snprintf(row, sizeof row, "%lld %lld \"%s\"\n", (long long)players[i].score, (long long)players[i].ping, players[i].name);
    size_t rn = n > 1023 ? 1023 : (size_t)(n < 0 ? 0 : n); /* TS slice(0,1023) */
    if (rn > sizeof row - 1) rn = sizeof row - 1;
    if (used + rn >= 16384) break;
    memcpy(rows + used, row, rn); used += rn; rows[used] = 0;
  }
  size_t tn = strlen(info) + used + 24;
  char *text = malloc(tn);
  if (!text) { free(rows); return false; }
  snprintf(text, tn, "statusResponse\n%s\n%s", info, rows);
  bool ok = qa_q3_encode_connectionless_text(text, out, out_len);
  free(rows); free(text);
  return ok;
}
bool qa_q3_encode_info(const char *info, uint8_t **out, size_t *out_len) {
  size_t n = strlen(info) + 24;
  char *text = malloc(n);
  if (!text) return false;
  snprintf(text, n, "infoResponse\n%s", info);
  bool ok = qa_q3_encode_connectionless_text(text, out, out_len);
  free(text); return ok;
}
