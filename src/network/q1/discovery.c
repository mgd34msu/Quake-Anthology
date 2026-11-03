/* Port of src/network/q1/discovery.ts. GPL-2.0-or-later. */
#include "qa/network/q1/discovery.h"
#include "qa/foundation/support.h"
#include "qa/network/q1/handshake.h"
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/js.h"
static char *dupstr(const char *s) { size_t n = strlen(s) + 1; char *p = malloc(n); memcpy(p, s, n); return p; }
static uint8_t *nq_query(void *ctx, const char *kind, const char *challenge, size_t *out_len) {
  (void)ctx; (void)kind; (void)challenge;
  qa_netquake_control m; memset(&m, 0, sizeof m);
  m.kind = QA_NQ_SERVER_INFO_REQUEST; m.game = (char *)"QUAKE"; m.version = 3;
  return qa_encode_netquake_control(&m, out_len);
}
static uint8_t *nq_master(void *ctx, size_t *out_len) {
  (void)ctx; (void)out_len;
  qa_raise("Error", 0, 0, "NetQuake master discovery is not configured");
}
static uint8_t *nq_heartbeat(void *ctx, bool active, size_t *out_len) {
  (void)ctx; (void)active; (void)out_len;
  qa_raise("Error", 0, 0, "NetQuake master registration is not configured");
}
qa_discovery_wire qa_netquake_discovery_wire(void) {
  qa_discovery_wire w; w.ctx = NULL; w.query = nq_query; w.master_query = nq_master; w.heartbeat = nq_heartbeat;
  return w;
}
void qa_read_netquake_discovery(const uint8_t *bytes, size_t length, qa_server_status *out) {
  qa_netquake_control m; qa_decode_netquake_control(bytes, length, &m);
  if (m.kind != QA_NQ_SERVER_INFO || m.version != 3) {
    qa_netquake_control_destroy(&m);
    qa_raise("Error", 0, 0, "Not a compatible NetQuake discovery response");
  }
  memset(out, 0, sizeof *out);
  out->name = dupstr(m.name); out->map = dupstr(m.map);
  out->players = m.players; out->max_players = m.max_players;
  qa_qw_info_init(&out->rules);
  out->wire.is_unified = false;
  out->wire.protocol.kind = "q1-netquake"; out->wire.protocol.version = 15;
  out->wire.protocol.flags = 0; out->wire.protocol.revision = 0;
  out->wire.composition = NULL; out->wire.snapshot_schema = NULL;
  qa_netquake_control_destroy(&m);
}
static uint8_t *qw_query(void *ctx, const char *kind, const char *challenge, size_t *out_len) {
  (void)ctx; (void)kind; (void)challenge;
  return qa_quakeworld_out_of_band("status\n", false, out_len);
}
static uint8_t *qw_master(void *ctx, size_t *out_len) {
  (void)ctx; (void)out_len;
  qa_raise("Error", 0, 0, "Use an HTTP QuakeWorld master list");
}
static uint8_t *qw_heartbeat(void *ctx, bool active, size_t *out_len) {
  (void)ctx; (void)active; (void)out_len;
  qa_raise("Error", 0, 0, "QuakeWorld heartbeat requires its source sequence and active client count");
}
qa_discovery_wire qa_quakeworld_discovery_wire(void) {
  qa_discovery_wire w; w.ctx = NULL; w.query = qw_query; w.master_query = qw_master; w.heartbeat = qw_heartbeat;
  return w;
}
/* (-?\d+) then Number(): strtol also took "+5", " 5" and clamped long digit runs. */
static bool signed_digits(const char **cursor, double *out) {
  const char *p = *cursor, *start = p;
  if (*p == '-') p++;
  if (!(*p >= '0' && *p <= '9')) return false;
  while (*p >= '0' && *p <= '9') p++;
  *out = qa_js_parse_int(start, 10);
  *cursor = p;
  return true;
}
static int parse_player_line(const char *line, double *score, double *ping, char **name) {
  /* /^\d+ (-?\d+) \d+ (-?\d+) "([^"]*)" "[^"]*" \d+ \d+\s*$/ */
  const char *p = line;
  if (!(*p >= '0' && *p <= '9')) return 0;
  while (*p >= '0' && *p <= '9') p++;
  if (*p++ != ' ') return 0;
  double s, pg;
  if (!signed_digits(&p, &s)) return 0;
  if (*p++ != ' ') return 0;
  if (!(*p >= '0' && *p <= '9')) return 0;
  while (*p >= '0' && *p <= '9') p++;
  if (*p++ != ' ') return 0;
  if (!signed_digits(&p, &pg)) return 0;
  if (*p++ != ' ' || *p++ != '"') return 0;
  const char *nstart = p;
  while (*p && *p != '"') p++;
  if (*p != '"') return 0;
  size_t nlen = (size_t)(p - nstart);
  p++;
  if (*p++ != ' ' || *p++ != '"') return 0;
  while (*p && *p != '"') p++;
  if (*p++ != '"') return 0;
  if (*p++ != ' ') return 0;
  if (!(*p >= '0' && *p <= '9')) return 0;
  while (*p >= '0' && *p <= '9') p++;
  if (*p++ != ' ') return 0;
  if (!(*p >= '0' && *p <= '9')) return 0;
  while (*p >= '0' && *p <= '9') p++;
  while (*p == ' ' || (*p >= '\t' && *p <= '\r')) p++; /* JS \s also matches \v and \f */
  if (*p != '\0') return 0;
  *score = s; *ping = pg;
  *name = malloc(nlen + 1); memcpy(*name, nstart, nlen); (*name)[nlen] = '\0';
  return 1;
}
void qa_read_quakeworld_discovery(const uint8_t *bytes, size_t length, qa_server_status *out) {
  char *text = qa_read_quakeworld_out_of_band(bytes, length);
  if (strncmp(text, "n\\", 2) != 0) { free(text); qa_raise("Error", 0, 0, "Not a QuakeWorld status reply"); }
  char *body = text + 1;
  size_t blen = strlen(body);
  if (blen && body[blen - 1] == '\0') blen--;
  /* Split lines on \n. */
  memset(out, 0, sizeof *out);
  qa_qw_info_init(&out->rules);
  char *copy = dupstr(body);
  char *line = copy;
  bool first = true;
  size_t cap = 0;
  for (;;) {
    char *nl = strchr(line, '\n');
    if (nl) *nl = '\0';
    if (first) {
      first = false;
      const char *info = line[0] == '\\' ? line + 1 : line;
      /* fields split on '\\' */
      char *icopy = dupstr(info);
      size_t fcap = 8, fcount = 0; char **fields = malloc(fcap * sizeof *fields);
      char *start = icopy;
      for (char *p = icopy;; p++) {
        if (*p == '\\' || *p == '\0') {
          bool last = *p == '\0';
          *p = '\0';
          if (fcount == fcap) { fcap *= 2; fields = realloc(fields, fcap * sizeof *fields); }
          fields[fcount++] = start;
          if (last) break;
          start = p + 1;
        }
      }
      for (size_t i = 0; i + 1 < fcount; i += 2) qa_qw_info_set(&out->rules, fields[i], fields[i+1]);
      free(fields); free(icopy);
    } else {
      double score = 0, ping = 0; char *name = NULL;
      if (parse_player_line(line, &score, &ping, &name)) {
        if (out->player_count == cap) { size_t nc = cap ? cap * 2 : 8; out->player_details = realloc(out->player_details, nc * sizeof *out->player_details); cap = nc; }
        out->player_details[out->player_count].name = name;
        out->player_details[out->player_count].score = score;
        out->player_details[out->player_count].ping = ping;
        out->player_count++;
      }
    }
    if (!nl) break;
    line = nl + 1;
  }
  free(copy);
  const char *host = qa_qw_info_get(&out->rules, "hostname");
  const char *map = qa_qw_info_get(&out->rules, "map");
  const char *maxc = qa_qw_info_get(&out->rules, "maxclients");
  out->name = dupstr(host ? host : "");
  out->map = dupstr(map ? map : "");
  out->players = (double)out->player_count;
  out->max_players = qa_js_string_to_number(maxc ? maxc : "32"); /* Number(get("maxclients") ?? "32") */
  out->wire.is_unified = false;
  out->wire.protocol.kind = "q1-quakeworld"; out->wire.protocol.version = 28;
  out->wire.protocol.flags = 0; out->wire.protocol.revision = 0;
  out->wire.composition = NULL; out->wire.snapshot_schema = NULL;
  free(text);
}
