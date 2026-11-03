/* Port of src/network/q3/discovery.ts. GPL-2.0-or-later. */
#include "qa/network/q3/discovery.h"
#include "qa/core/commands/text.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/network/q3/adapters.h"
#include "qa/network/q3/connectionless.h"
#include "qa/network/common/js_number.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static char *xstrdup(const char *s) { size_t n = strlen(s); char *r = malloc(n + 1); if (r) memcpy(r, s, n + 1); return r; }
/* encodeConnectionlessText can raise (RangeError over the packet limit); the
 * owned text is released on that path so a catching caller leaks nothing. */
static bool encode_owned_text(char *text, uint8_t **out, size_t *out_len) {
  qa_cleanup guard;
  qa_cleanup_push(&guard, free, text);
  bool ok = qa_q3_encode_connectionless_text(text, out, out_len);
  qa_cleanup_pop(&guard);
  free(text);
  return ok;
}
void qa_q3_discovery_wire_state_destroy(qa_q3_discovery_wire_state *state) {
  for (size_t i = 0; i < state->keyword_count; i++) free(state->keywords[i]);
  free(state->keywords);
  memset(state, 0, sizeof *state);
}
/* query(kind, challenge) => encodeConnectionlessText(`${kind === "info" ? "getinfo" : "getstatus"} ${challenge}`) */
static uint8_t *wire_query(void *ctx, const char *kind, const char *challenge, size_t *out_len) {
  (void)ctx; /* the TS closure does not read keywords or protocol here */
  const char *word = strcmp(kind, "info") == 0 ? "getinfo" : "getstatus";
  size_t n = strlen(word) + 1 + strlen(challenge) + 1;
  char *buf = malloc(n);
  if (!buf) return NULL;
  snprintf(buf, n, "%s %s", word, challenge);
  uint8_t *out = NULL;
  return encode_owned_text(buf, &out, out_len) ? out : NULL;
}
/* masterQuery() => encodeConnectionlessText(`getservers ${protocol}${keywords...}`) */
static uint8_t *wire_master_query(void *ctx, size_t *out_len) {
  const qa_q3_discovery_wire_state *w = ctx;
  char protocol[40];
  snprintf(protocol, sizeof protocol, "%s", QA_JS_NUMBER(w->protocol)); /* `${protocol}` */
  size_t n = strlen("getservers ") + strlen(protocol) + 1;
  for (size_t i = 0; i < w->keyword_count; i++) n += strlen(w->keywords[i]) + 1;
  char *buf = malloc(n);
  if (!buf) return NULL;
  size_t o = (size_t)snprintf(buf, n, "getservers %s", protocol);
  for (size_t i = 0; i < w->keyword_count; i++) o += (size_t)snprintf(buf + o, n - o, " %s", w->keywords[i]);
  uint8_t *out = NULL;
  return encode_owned_text(buf, &out, out_len) ? out : NULL;
}
/* heartbeat() => encodeConnectionlessText("heartbeat QuakeArena-1\n"); `active` is unused in TS too. */
static uint8_t *wire_heartbeat(void *ctx, bool active, size_t *out_len) {
  (void)ctx; (void)active;
  uint8_t *out = NULL;
  return qa_q3_encode_connectionless_text("heartbeat QuakeArena-1\n", &out, out_len) ? out : NULL;
}
bool qa_q3_discovery_wire_init(qa_discovery_wire *wire, qa_q3_discovery_wire_state *state,
                               const char *const *keywords, size_t keyword_count, double protocol) {
  memset(state, 0, sizeof *state);
  if (!(protocol == trunc(protocol) && fabs(protocol) <= 9007199254740991.0) || protocol <= 0 || protocol > 0x7fffffff)
    qa_raise("RangeError", 0, 0, "Invalid Q3 master protocol");
  /* /\s|\0/: C strings cannot hold NUL; whitespace is tested bytewise (ASCII). */
  for (size_t i = 0; i < keyword_count; i++)
    for (const char *p = keywords[i]; *p; p++)
      if (isspace((unsigned char)*p)) qa_raise("RangeError", 0, 0, "Q3 master keywords must be source words");
  state->keywords = calloc(keyword_count ? keyword_count : 1, sizeof *state->keywords);
  if (!state->keywords) return false;
  for (size_t i = 0; i < keyword_count; i++) {
    state->keywords[i] = xstrdup(keywords[i]);
    if (!state->keywords[i]) { qa_q3_discovery_wire_state_destroy(state); return false; }
    state->keyword_count++;
  }
  state->protocol = protocol;
  wire->ctx = state;
  wire->query = wire_query;
  wire->master_query = wire_master_query;
  wire->heartbeat = wire_heartbeat;
  return true;
}
void qa_q3_master_addresses_free(qa_net_address *addresses, size_t count) {
  for (size_t i = 0; i < count; i++) qa_net_address_destroy(&addresses[i]);
  free(addresses);
}
bool qa_q3_decode_master_packet(const uint8_t *bytes, size_t len, qa_net_address **out, size_t *count, bool *complete) {
  static const char prefix[] = "getserversResponse";
  size_t plen = sizeof prefix - 1;
  *out = NULL; *count = 0; *complete = false;
  if (len < plen + 4) qa_raise("RangeError", 0, 0, "Not a Q3 master response");
  for (int i = 0; i < 4; i++) if (bytes[i] != 255) qa_raise("RangeError", 0, 0, "Not a Q3 master response");
  if (memcmp(bytes + 4, prefix, plen) != 0) qa_raise("RangeError", 0, 0, "Not a Q3 master response");
  qa_net_address *result = NULL; size_t n = 0, cap = 0;
  size_t cursor = 4 + plen;
  while (cursor < len && bytes[cursor] != 92) cursor++;
  while (cursor < len && bytes[cursor] == 92) {
    cursor++;
    /* bytes[cursor + k] past the end is undefined in TS and matches nothing. */
    if (cursor + 2 < len && bytes[cursor] == 69 && bytes[cursor + 1] == 79 && bytes[cursor + 2] == 84) { *complete = true; break; }
    if (n >= 256) break;
    if (cursor + 5 >= len) break;
    uint8_t a = bytes[cursor], b = bytes[cursor + 1], c = bytes[cursor + 2], d = bytes[cursor + 3];
    uint8_t high = bytes[cursor + 4], low = bytes[cursor + 5];
    cursor += 6;
    if (cursor >= len || bytes[cursor] != 92) break;
    if (n == cap) {
      size_t nc = cap ? cap * 2 : 16;
      qa_net_address *nr = realloc(result, nc * sizeof *nr);
      if (!nr) { free(result); return false; }
      result = nr; cap = nc;
    }
    /* TS builds the record literally ({kind:"ipv4", host, port}); port 0 is kept here and
     * refused by the callers that need an endpoint. */
    memset(&result[n], 0, sizeof result[n]);
    result[n].kind = QA_NET_IPV4;
    result[n].ipv4[0] = a; result[n].ipv4[1] = b; result[n].ipv4[2] = c; result[n].ipv4[3] = d;
    result[n].port = high * 256 + low; /* (high << 8) | low over bytes */
    n++;
  }
  *out = result; *count = n;
  return true;
}
bool qa_q3_decode_master_response(const uint8_t *bytes, size_t len, qa_net_address **out, size_t *count) {
  bool complete;
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
static bool ci_eq(const char *a, const char *b) {
  while (*a && *b) { if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false; a++; b++; }
  return *a == *b;
}
/* Owned scratch of one status decode. q3InfoValue (CommonError on an oversize
 * infostring) and tokenizeCommand (RangeError on an overlong token) raise on
 * hostile responses; the cleanup releases everything a catching caller would
 * otherwise lose, including the partial status. */
typedef struct {
  char *text, *info, *copy;
  qa_command_tokens tokens; bool has_tokens;
  qa_server_status *status;
  char **challenge;
} status_scratch;
static void status_scratch_release(void *arg) {
  status_scratch *sc = arg;
  free(sc->text); free(sc->info); free(sc->copy);
  sc->text = sc->info = sc->copy = NULL;
  if (sc->has_tokens) { qa_command_tokens_destroy(&sc->tokens); sc->has_tokens = false; }
  qa_server_status_destroy(sc->status);
  free(*sc->challenge); *sc->challenge = NULL;
}
static bool info_int(status_scratch *sc, const char *key, double *out) {
  char *value = qa_q3_info_value(sc->info, key);
  if (!value) return false;
  *out = qa_native_atoi(value);
  free(value);
  return true;
}
static bool decode_status_body(status_scratch *sc, bool is_info, bool is_status) {
  qa_server_status *status = sc->status;
  const char *text = sc->text;
  /* lines = text.split("\n"); info = lines[0] */
  const char *nl = strchr(text, '\n');
  size_t info_len = nl ? (size_t)(nl - text) : strlen(text);
  sc->info = malloc(info_len + 1);
  if (!sc->info) return false;
  memcpy(sc->info, text, info_len); sc->info[info_len] = 0;
  const char *info = sc->info;
  /* rules from info.split("\\") pairs, later keys overwrite in place (Map.set) */
  size_t start_index = (info_len > 0 && info[0] == '\\') ? 1 : 0;
  size_t field = 0, pos = 0;
  const char *ks = NULL; size_t kn = 0;
  for (;;) {
    size_t end = pos; while (end < info_len && info[end] != '\\') end++;
    if (field >= start_index && ((field - start_index) % 2) == 0) { ks = info + pos; kn = end - pos; }
    else if (field >= start_index && ks) {
      /* rules.set(key, value): a repeated key keeps its first position. */
      char *key = malloc(kn + 1), *value = malloc(end - pos + 1);
      if (!key || !value) { free(key); free(value); return false; }
      memcpy(key, ks, kn); key[kn] = 0;
      memcpy(value, info + pos, end - pos); value[end - pos] = 0;
      qa_qw_info_set(&status->rules, key, value);
      free(key); free(value);
      ks = NULL;
    }
    if (end >= info_len) break;
    pos = end + 1; field++;
  }
  if (is_info) {
    double protocol;
    if (!info_int(sc, "protocol", &protocol)) return false;
    if (protocol != 68) qa_raise("RangeError", 0, 0, "Server uses another Q3 wire version");
  }
  if (is_status && nl) {
    const char *line = nl + 1;
    for (;;) {
      const char *eol = strchr(line, '\n');
      size_t ln = eol ? (size_t)(eol - line) : strlen(line);
      if (ln > 0) {
        sc->copy = malloc(ln + 1);
        if (!sc->copy) return false;
        memcpy(sc->copy, line, ln); sc->copy[ln] = 0;
        if (!qa_tokenize_command(sc->copy, QA_COMMAND_DIALECT_Q3, QA_COMMAND_TEXT_SOURCE, &sc->tokens)) return false;
        sc->has_tokens = true;
        if (sc->tokens.argc >= 3) {
          qa_player_detail *nd = realloc(status->player_details, (status->player_count + 1) * sizeof *nd);
          if (!nd) return false;
          status->player_details = nd;
          char *name = xstrdup(sc->tokens.argv[2]);
          if (!name) return false;
          qa_player_detail *d = &status->player_details[status->player_count++];
          d->name = name; d->score = qa_native_atoi(sc->tokens.argv[0]); d->ping = qa_native_atoi(sc->tokens.argv[1]);
        }
        qa_command_tokens_destroy(&sc->tokens); sc->has_tokens = false;
        free(sc->copy); sc->copy = NULL;
      }
      if (!eol) break;
      line = eol + 1;
    }
  }
  *sc->challenge = qa_q3_info_value(info, "challenge");
  if (!*sc->challenge) return false;
  char *host = qa_q3_info_value(info, "hostname");
  if (!host) return false;
  if (host[0]) status->name = host;
  else {
    free(host);
    status->name = qa_q3_info_value(info, "sv_hostname");
    if (!status->name) return false;
  }
  status->map = qa_q3_info_value(info, "mapname");
  if (!status->map) return false;
  if (is_status) status->players = (double)status->player_count;
  else if (!info_int(sc, "clients", &status->players)) return false;
  if (!info_int(sc, "sv_maxclients", &status->max_players)) return false;
  status->wire.is_unified = false;
  status->wire.protocol = qa_q3_protocol;
  return true;
}
bool qa_q3_decode_server_status(const uint8_t *bytes, size_t len, char **challenge_out, qa_server_status *status_out) {
  memset(status_out, 0, sizeof *status_out);
  qa_qw_info_init(&status_out->rules);
  *challenge_out = NULL;
  qa_q3_connectionless_packet packet;
  if (!qa_q3_decode_connectionless(bytes, len, QA_Q3_CONNLESS_CLIENT, NULL, &packet)) return false;
  bool is_info = ci_eq(packet.command, "infoResponse");
  bool is_status = ci_eq(packet.command, "statusResponse");
  if (!is_info && !is_status) { qa_q3_connectionless_packet_destroy(&packet); qa_raise("RangeError", 0, 0, "Not a Q3 server browser response"); }
  char *text = payload_text(packet.payload, packet.payload_len);
  qa_q3_connectionless_packet_destroy(&packet);
  if (!text) return false;
  status_scratch sc = { .text = text, .status = status_out, .challenge = challenge_out };
  qa_cleanup guard;
  qa_cleanup_push(&guard, status_scratch_release, &sc);
  bool ok = decode_status_body(&sc, is_info, is_status);
  qa_cleanup_pop(&guard);
  if (!ok) { status_scratch_release(&sc); return false; }
  free(sc.text); free(sc.info);
  return true;
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
  free(rows);
  return encode_owned_text(text, out, out_len);
}
bool qa_q3_encode_info(const char *info, uint8_t **out, size_t *out_len) {
  size_t n = strlen(info) + 24;
  char *text = malloc(n);
  if (!text) return false;
  snprintf(text, n, "infoResponse\n%s", info);
  return encode_owned_text(text, out, out_len);
}
