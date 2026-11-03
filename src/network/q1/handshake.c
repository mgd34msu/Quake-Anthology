/* Port of src/network/q1/handshake.ts. GPL-2.0-or-later. */
#include "qa/network/q1/handshake.h"
#include "qa/core/common_parse.h"
#include "qa/foundation/support.h"
#include "qa/network/q1/channels.h"
#include "qa/network/q1/message.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/js.h"
#include "qa/network/common/js_number.h"
static char *dupstr(const char *s) { size_t n = strlen(s) + 1; char *p = malloc(n); if (p) memcpy(p, s, n); return p; }
static char *dupn(const char *s, size_t n) { char *p = malloc(n + 1); if (p) { memcpy(p, s, n); p[n] = '\0'; } return p; }
void qa_netquake_control_destroy(qa_netquake_control *m) {
  free(m->game); free(m->previous); free(m->reason); free(m->address);
  free(m->name); free(m->map); free(m->rule_name); free(m->rule_value);
  memset(m, 0, sizeof *m);
}
uint8_t *qa_encode_netquake_control(const qa_netquake_control *m, size_t *out_len) {
  qa_size_buf s; qa_size_buf_init(&s, 65535, false);
  qa_msg_write_long(&s, 0);
  switch (m->kind) {
    case QA_NQ_CONNECT_REQUEST: case QA_NQ_SERVER_INFO_REQUEST:
      qa_msg_write_byte(&s, m->kind == QA_NQ_CONNECT_REQUEST ? 1 : 2);
      qa_msg_write_string(&s, m->game); qa_msg_write_byte(&s, m->version); break;
    case QA_NQ_PLAYER_INFO_REQUEST:
      qa_msg_write_byte(&s, 3); qa_msg_write_byte(&s, m->player); break;
    case QA_NQ_RULE_INFO_REQUEST:
      qa_msg_write_byte(&s, 4); qa_msg_write_string(&s, m->previous); break;
    case QA_NQ_ACCEPT:
      qa_msg_write_byte(&s, 0x81); qa_msg_write_long(&s, m->port); break;
    case QA_NQ_REJECT:
      qa_msg_write_byte(&s, 0x82); qa_msg_write_string(&s, m->reason); break;
    case QA_NQ_SERVER_INFO:
      qa_msg_write_byte(&s, 0x83); qa_msg_write_string(&s, m->address);
      qa_msg_write_string(&s, m->name); qa_msg_write_string(&s, m->map);
      qa_msg_write_byte(&s, m->players); qa_msg_write_byte(&s, m->max_players);
      qa_msg_write_byte(&s, m->version); break;
    case QA_NQ_PLAYER_INFO:
      qa_msg_write_byte(&s, 0x84); qa_msg_write_byte(&s, m->player);
      qa_msg_write_string(&s, m->name); qa_msg_write_long(&s, m->colors);
      qa_msg_write_long(&s, m->frags); qa_msg_write_long(&s, m->seconds);
      qa_msg_write_string(&s, m->address); break;
    case QA_NQ_RULE_INFO:
      qa_msg_write_byte(&s, 0x85);
      if (m->has_rule) { qa_msg_write_string(&s, m->rule_name); qa_msg_write_string(&s, m->rule_value); }
      break;
  }
  uint32_t w = QA_NETFLAG_CTL | (uint32_t)s.cursize;
  s.data[0] = (uint8_t)(w >> 24); s.data[1] = (uint8_t)(w >> 16); s.data[2] = (uint8_t)(w >> 8); s.data[3] = (uint8_t)w;
  uint8_t *out = qa_size_buf_bytes(&s, out_len);
  qa_size_buf_destroy(&s); return out;
}
/* A raise after fields were read releases them, as the abandoned TS object is collected. */
static void control_slot_destroy(void *arg) { qa_netquake_control_destroy(arg); }
void qa_decode_netquake_control(const uint8_t *bytes, size_t length, qa_netquake_control *out) {
  memset(out, 0, sizeof *out);
  if (length < 5) qa_q1_packet_error("Short control header", 0);
  uint32_t header = ((uint32_t)bytes[0]<<24)|((uint32_t)bytes[1]<<16)|((uint32_t)bytes[2]<<8)|bytes[3];
  if ((header & 65535u) != (uint32_t)length || (header >> 16) != 0x8000u) qa_q1_packet_error("Invalid control header", 0);
  qa_message_reader r; qa_message_reader_init(&r, bytes + 4, length - 4);
  double opd = qa_message_reader_byte(&r); int op = (int)opd;
  size_t dummy;
  switch (op) {
    case 1: case 2:
      out->kind = op == 1 ? QA_NQ_CONNECT_REQUEST : QA_NQ_SERVER_INFO_REQUEST;
      out->game = qa_message_reader_string(&r, 2047, false, &dummy);
      out->version = qa_message_reader_byte(&r); break;
    case 3: out->kind = QA_NQ_PLAYER_INFO_REQUEST; out->player = qa_message_reader_byte(&r); break;
    case 4: out->kind = QA_NQ_RULE_INFO_REQUEST; out->previous = qa_message_reader_string(&r, 2047, false, &dummy); break;
    case 0x81: out->kind = QA_NQ_ACCEPT; out->port = qa_message_reader_long(&r); break;
    case 0x82: out->kind = QA_NQ_REJECT; out->reason = qa_message_reader_string(&r, 2047, false, &dummy); break;
    case 0x83:
      out->kind = QA_NQ_SERVER_INFO;
      out->address = qa_message_reader_string(&r, 2047, false, &dummy);
      out->name = qa_message_reader_string(&r, 2047, false, &dummy);
      out->map = qa_message_reader_string(&r, 2047, false, &dummy);
      out->players = qa_message_reader_byte(&r); out->max_players = qa_message_reader_byte(&r);
      out->version = qa_message_reader_byte(&r); break;
    case 0x84:
      out->kind = QA_NQ_PLAYER_INFO; out->player = qa_message_reader_byte(&r);
      out->name = qa_message_reader_string(&r, 2047, false, &dummy);
      out->colors = qa_message_reader_long(&r); out->frags = qa_message_reader_long(&r);
      out->seconds = qa_message_reader_long(&r);
      out->address = qa_message_reader_string(&r, 2047, false, &dummy); break;
    case 0x85:
      out->kind = QA_NQ_RULE_INFO;
      if (qa_message_reader_remaining(&r) > 0) {
        out->has_rule = true;
        out->rule_name = qa_message_reader_string(&r, 2047, false, &dummy);
        out->rule_value = qa_message_reader_string(&r, 2047, false, &dummy);
      }
      break;
    default: {
      char msg[64]; snprintf(msg, sizeof msg, "Unknown control command %d", op);
      qa_q1_packet_error(msg, 4);
    }
  }
  qa_cleanup guard;
  qa_cleanup_push(&guard, control_slot_destroy, out);
  qa_message_reader_finish(&r);
  qa_cleanup_pop(&guard);
}
uint8_t *qa_answer_netquake_control(const uint8_t *bytes, size_t length, const qa_net_address *from,
                                    double now_ms, const qa_netquake_host *host, size_t *out_len) {
  *out_len = 0;
  qa_netquake_control m; qa_decode_netquake_control(bytes, length, &m);
  qa_cleanup guard;
  qa_cleanup_push(&guard, control_slot_destroy, &m);
  uint8_t *result = NULL;
  if (m.kind == QA_NQ_SERVER_INFO_REQUEST) {
    if (m.game && strcmp(m.game, "QUAKE") == 0) {
      qa_netquake_control info; memset(&info, 0, sizeof info);
      host->server_info(host->ctx, &info);
      result = qa_encode_netquake_control(&info, out_len);
      qa_netquake_control_destroy(&info);
    }
  } else if (m.kind == QA_NQ_PLAYER_INFO_REQUEST) {
    qa_netquake_control info; memset(&info, 0, sizeof info);
    if (host->player_info(host->ctx, m.player, &info)) result = qa_encode_netquake_control(&info, out_len);
    qa_netquake_control_destroy(&info);
  } else if (m.kind == QA_NQ_RULE_INFO_REQUEST) {
    qa_netquake_control info; memset(&info, 0, sizeof info); info.kind = QA_NQ_RULE_INFO;
    char *name = NULL, *value = NULL;
    if (host->next_rule(host->ctx, m.previous, &name, &value)) {
      info.has_rule = true; info.rule_name = name; info.rule_value = value;
    }
    result = qa_encode_netquake_control(&info, out_len);
    qa_netquake_control_destroy(&info);
  } else if (m.kind == QA_NQ_CONNECT_REQUEST) {
    if (m.game && strcmp(m.game, "QUAKE") == 0) {
      if (m.version != 3) {
        qa_netquake_control rej; memset(&rej, 0, sizeof rej);
        rej.kind = QA_NQ_REJECT; rej.reason = (char *)"Incompatible version.\n";
        result = qa_encode_netquake_control(&rej, out_len);
      } else {
        qa_nq_connect_result res = host->connect(host->ctx, from, now_ms);
        if (res.kind == 0) {
          qa_netquake_control acc; memset(&acc, 0, sizeof acc);
          acc.kind = QA_NQ_ACCEPT; acc.port = res.port;
          result = qa_encode_netquake_control(&acc, out_len);
        } else if (res.kind == 1) {
          qa_netquake_control rej; memset(&rej, 0, sizeof rej);
          rej.kind = QA_NQ_REJECT; rej.reason = (char *)res.reason;
          result = qa_encode_netquake_control(&rej, out_len);
        }
      }
    }
  }
  qa_cleanup_pop(&guard);
  qa_netquake_control_destroy(&m);
  return result;
}
uint8_t *qa_quakeworld_out_of_band(const char *text, bool nul, size_t *out_len) {
  size_t tl = strlen(text);
  size_t n = tl + 4 + (nul ? 1 : 0);
  uint8_t *b = malloc(n); if (!b) return NULL;
  memset(b, 255, 4);
  for (size_t i = 0; i < tl; i++) b[i + 4] = (uint8_t)text[i];
  if (nul) b[n - 1] = 0;
  *out_len = n; return b;
}
char *qa_read_quakeworld_out_of_band(const uint8_t *bytes, size_t length) {
  if (length < 4) qa_q1_packet_error("Not an out-of-band packet", 0);
  int32_t v = (int32_t)((uint32_t)bytes[0]|((uint32_t)bytes[1]<<8)|((uint32_t)bytes[2]<<16)|((uint32_t)bytes[3]<<24));
  if (v != -1) qa_q1_packet_error("Not an out-of-band packet", 0);
  size_t n = 0; while (4 + n < length && bytes[4 + n] != 0) n++;
  return dupn((const char *)bytes + 4, n);
}
char **qa_quakeworld_command_arguments(const char *text, size_t *out_count) {
  qa_legacy_parse_state state;
  memset(&state, 0, sizeof state);
  /* Landed parse state borrows text; fields per common_parse.h. */
  state.data = text; state.length = strlen(text); state.index = 0;
  size_t cap = 4, count = 0; char **args = malloc(cap * sizeof *args);
  for (;;) {
    char *token = qa_parse_q1_token(&state, true);
    if (!token) break;
    if (count == cap) { cap *= 2; args = realloc(args, cap * sizeof *args); }
    args[count++] = token;
  }
  *out_count = count; return args;
}
void qa_qw_info_init(qa_qw_info *info) { memset(info, 0, sizeof *info); }
void qa_qw_info_destroy(qa_qw_info *info) {
  for (size_t i = 0; i < info->count; i++) { free(info->entries[i].key); free(info->entries[i].value); }
  free(info->entries); memset(info, 0, sizeof *info);
}
const char *qa_qw_info_get(const qa_qw_info *info, const char *key) {
  for (size_t i = 0; i < info->count; i++) if (strcmp(info->entries[i].key, key) == 0) return info->entries[i].value;
  return NULL;
}
void qa_qw_info_set(qa_qw_info *info, const char *key, const char *value) {
  for (size_t i = 0; i < info->count; i++) if (strcmp(info->entries[i].key, key) == 0) {
    free(info->entries[i].value); info->entries[i].value = dupstr(value); return;
  }
  if (info->count == info->capacity) { size_t nc = info->capacity ? info->capacity * 2 : 8; info->entries = realloc(info->entries, nc * sizeof *info->entries); info->capacity = nc; }
  info->entries[info->count].key = dupstr(key); info->entries[info->count].value = dupstr(value); info->count++;
}
void qa_qw_info_delete(qa_qw_info *info, const char *key) {
  for (size_t i = 0; i < info->count; i++) if (strcmp(info->entries[i].key, key) == 0) {
    free(info->entries[i].key); free(info->entries[i].value);
    memmove(&info->entries[i], &info->entries[i+1], (info->count - i - 1) * sizeof *info->entries);
    info->count--; return;
  }
}
char *qa_qw_info_string(const qa_qw_info *info) {
  size_t need = 1;
  for (size_t i = 0; i < info->count; i++) need += strlen(info->entries[i].key) + strlen(info->entries[i].value) + 2;
  char *s = malloc(need); size_t at = 0; s[0] = '\0';
  for (size_t i = 0; i < info->count; i++) at += (size_t)snprintf(s + at, need - at, "\\%s\\%s", info->entries[i].key, info->entries[i].value);
  return s;
}
void qa_quakeworld_info_parse(const char *text, qa_qw_info *out) {
  qa_qw_info_init(out);
  /* split on '\\' */
  size_t cap = 8, count = 0; char **parts = malloc(cap * sizeof *parts);
  const char *start = text;
  for (const char *p = text;; p++) {
    if (*p == '\\' || *p == '\0') {
      if (count == cap) { cap *= 2; parts = realloc(parts, cap * sizeof *parts); }
      parts[count++] = dupn(start, (size_t)(p - start));
      if (*p == '\0') break;
      start = p + 1;
    }
  }
  size_t offset = (count > 0 && parts[0][0] == '\0') ? 1 : 0;
  for (size_t i = offset; i + 1 < count; i += 2) qa_qw_info_set(out, parts[i], parts[i+1]);
  for (size_t i = 0; i < count; i++) free(parts[i]);
  free(parts);
}
void qa_qw_challenges_init(qa_qw_challenges *c, qa_random_fn random, void *ctx, double capacity) {
  memset(c, 0, sizeof *c); c->random = random; c->random_ctx = ctx; c->capacity = capacity;
}
void qa_qw_challenges_destroy(qa_qw_challenges *c) {
  for (size_t i = 0; i < c->count; i++) free(c->records[i].key);
  free(c->records); memset(c, 0, sizeof *c);
}
/* ToInt32 (NaN and infinities are 0; the fmod form cast NaN to uint32_t). */
static int32_t ji(double n) { return qa_js_to_int32(n); }
double qa_qw_challenges_issue(qa_qw_challenges *c, const qa_net_address *address, double now_ms) {
  char *key = qa_net_address_key(address, false);
  for (size_t i = 0; i < c->count; i++) {
    if (strcmp(c->records[i].key, key) != 0) continue;
    if (now_ms / 1000 - c->records[i].issued <= 30) { double v = c->records[i].challenge; free(key); return v; }
    free(c->records[i].key);
    memmove(&c->records[i], &c->records[i + 1], (c->count - i - 1) * sizeof *c->records);
    c->count--;
    break;
  }
  if ((double)c->count >= c->capacity) {
    /* TS deletes the oldest key, or the empty key (absent) when nothing is older than Infinity. */
    size_t oldest = c->count; double time = INFINITY;
    for (size_t i = 0; i < c->count; i++) if (c->records[i].issued < time) { oldest = i; time = c->records[i].issued; }
    if (oldest < c->count) {
      free(c->records[oldest].key);
      memmove(&c->records[oldest], &c->records[oldest + 1], (c->count - oldest - 1) * sizeof *c->records);
      c->count--;
    }
  }
  int32_t challenge = ji(c->random(c->random_ctx)) & 0x7fffffff;
  if (challenge == 0) challenge = 1;
  if (c->count == c->cap) { size_t nc = c->cap ? c->cap * 2 : 8; c->records = realloc(c->records, nc * sizeof *c->records); c->cap = nc; }
  c->records[c->count].key = key; c->records[c->count].challenge = challenge;
  c->records[c->count].issued = trunc(now_ms / 1000); c->count++;
  return challenge;
}
bool qa_qw_challenges_validate(qa_qw_challenges *c, const qa_net_address *address, double challenge) {
  return qa_qw_challenges_validate_at(c, address, challenge, false, 0);
}
bool qa_qw_challenges_validate_at(qa_qw_challenges *c, const qa_net_address *address, double challenge,
                                  bool has_now, double now_ms) {
  char *key = qa_net_address_key(address, false);
  bool ok = false;
  for (size_t i = 0; i < c->count; i++)
    if (strcmp(c->records[i].key, key) == 0)
      ok = c->records[i].challenge == challenge && (!has_now || now_ms / 1000 - c->records[i].issued <= 30);
  free(key); return ok;
}
static double js_parse_int(const char *s) { return s == NULL ? NAN : qa_js_parse_int(s, 10); }
static void push_reply(uint8_t ***arr, size_t **lens, size_t *count, size_t *cap, uint8_t *bytes, size_t len) {
  if (*count == *cap) { size_t nc = *cap ? *cap * 2 : 4; *arr = realloc(*arr, nc * sizeof **arr); *lens = realloc(*lens, nc * sizeof **lens); *cap = nc; }
  (*arr)[*count] = bytes; (*lens)[*count] = len; (*count)++;
}
struct oob_acc { char *buf; size_t len, cap; uint8_t **replies; size_t *lens; size_t count, rcap; };
static void oob_flush(struct oob_acc *a) {
  if (a->len == 0) return;
  char *text = malloc(a->len + 2); text[0] = 'n'; memcpy(text + 1, a->buf, a->len); text[a->len + 1] = '\0';
  size_t n = 0; uint8_t *b = qa_quakeworld_out_of_band(text, false, &n); free(text);
  push_reply(&a->replies, &a->lens, &a->count, &a->rcap, b, n);
  a->len = 0;
}
static void oob_write(void *ctx, const char *text, size_t length) {
  struct oob_acc *a = ctx;
  for (size_t i = 0; i < length; i++) {
    if (a->len == a->cap) { size_t nc = a->cap ? a->cap * 2 : 256; a->buf = realloc(a->buf, nc); a->cap = nc; }
    a->buf[a->len++] = text[i];
    if (a->len >= 7995) oob_flush(a);
  }
}
static char *lower_dup(const char *s) { char *p = dupstr(s); for (char *q = p; *q; q++) *q = (char)tolower((unsigned char)*q); return p; }
void qa_qw_connectionless_server_destroy(qa_qw_connectionless_server *s) { qa_rcon_guard_destroy(&s->rcon_guard); }
/* Everything one receive() owns, released on return and by a caught raise from a host
 * callback (connect, executeAdmin), as TS leaves it to the collector. */
typedef struct oob_scope {
  struct oob_acc acc; /* replies accumulate here */
  char *text, *line, **args; size_t argc;
  qa_qw_info info; bool has_info;
  char *spec_dup, *pw_lower, *userinfo, *cmd;
  qa_net_address from; bool has_from;
} oob_scope;
static void oob_scope_release(oob_scope *o, bool replies_too) {
  if (replies_too) {
    for (size_t i = 0; i < o->acc.count; i++) free(o->acc.replies[i]);
    free(o->acc.replies); free(o->acc.lens);
    o->acc.replies = NULL; o->acc.lens = NULL; o->acc.count = 0;
  }
  free(o->acc.buf); o->acc.buf = NULL;
  for (size_t i = 0; i < o->argc; i++) free(o->args[i]);
  free(o->args); o->args = NULL; o->argc = 0;
  free(o->text); free(o->line); o->text = o->line = NULL;
  if (o->has_info) qa_qw_info_destroy(&o->info);
  o->has_info = false;
  free(o->spec_dup); free(o->pw_lower); free(o->userinfo); free(o->cmd);
  o->spec_dup = o->pw_lower = o->userinfo = o->cmd = NULL;
  if (o->has_from) qa_net_address_destroy(&o->from);
  o->has_from = false;
}
static void oob_scope_abandon(void *arg) { oob_scope_release(arg, true); }
static void reply_text(oob_scope *o, const char *text) {
  size_t n = 0;
  uint8_t *b = qa_quakeworld_out_of_band(text, false, &n);
  push_reply(&o->acc.replies, &o->acc.lens, &o->acc.count, &o->acc.rcap, b, n);
}
static void reply_n(oob_scope *o, const char *text) {
  size_t length = strlen(text);
  char *msg = malloc(length + 2);
  msg[0] = 'n'; memcpy(msg + 1, text, length + 1);
  reply_text(o, msg);
  free(msg);
}
static void connectionless_body(qa_qw_connectionless_server *s, const uint8_t *bytes, size_t length,
                                const qa_net_address *from, double now_ms, oob_scope *o) {
  if (s->host.blocked(s->host.ctx, from)) { reply_text(o, "n\nbanned.\n"); return; }
  o->text = qa_read_quakeworld_out_of_band(bytes, length);
  o->line = dupstr(o->text);
  char *nl = strchr(o->line, '\n'); if (nl) *nl = '\0';
  o->args = qa_quakeworld_command_arguments(o->line, &o->argc);
  char **args = o->args; size_t argc = o->argc;
  const char *command = argc > 0 ? args[0] : NULL;
  if ((command && strcmp(command, "ping") == 0) || strcmp(o->text, "k") == 0) { reply_text(o, "l"); return; }
  if (command && strcmp(command, "getchallenge") == 0) {
    char buf[64]; snprintf(buf, sizeof buf, "c%s", QA_JS_NUMBER(qa_qw_challenges_issue(s->challenges, from, now_ms)));
    reply_text(o, buf);
    return;
  }
  if (command && strcmp(command, "status") == 0) {
    char *st = s->host.status(s->host.ctx);
    o->cmd = st;
    reply_n(o, st);
    return;
  }
  if (command && strcmp(command, "log") == 0) {
    double seq = js_parse_int(argc > 1 ? args[1] : "-1");
    o->cmd = s->host.log(s->host.ctx, seq);
    reply_text(o, o->cmd ? o->cmd : "m");
    return;
  }
  if (command && strcmp(command, "rcon") == 0) {
    if (s->rcon_guard.max_sources == 0) qa_rcon_guard_init(&s->rcon_guard, 3, 1024);
    if (!qa_rcon_guard_permitted(&s->rcon_guard, from, now_ms)) return;
    if (!qa_password_matches(s->host.rcon_password, argc > 1 ? args[1] : "")) {
      qa_rcon_guard_failed(&s->rcon_guard, from, now_ms);
      reply_text(o, "nBad rcon_password.\n");
      return;
    }
    qa_rcon_guard_succeeded(&s->rcon_guard, from);
    size_t need = 1; for (size_t i = 2; i < argc; i++) need += strlen(args[i]) + 1;
    o->cmd = malloc(need); size_t at = 0; o->cmd[0] = '\0';
    for (size_t i = 2; i < argc; i++) at += (size_t)snprintf(o->cmd + at, need - at, "%s ", args[i]);
    s->host.execute_admin(s->host.ctx, o->cmd, oob_write, &o->acc);
    oob_flush(&o->acc);
    return;
  }
  if (!command || strcmp(command, "connect") != 0) return;
  if (argc < 2 || strcmp(args[1], "28") != 0) { reply_text(o, "n\nServer uses QuakeWorld protocol 28.\n"); return; }
  double qport = js_parse_int(argc > 2 ? args[2] : ""), challenge = js_parse_int(argc > 3 ? args[3] : "");
  if (!isfinite(qport) || qport != trunc(qport) || qport < 0 || qport > 65535
      || !qa_qw_challenges_validate_at(s->challenges, from, challenge, true, now_ms)) {
    reply_text(o, "n\nBad challenge.\n");
    return;
  }
  const char *raw = argc > 4 ? args[4] : "";
  char *sliced = dupn(raw, strlen(raw) < 1022 ? strlen(raw) : 1022);
  qa_quakeworld_info_parse(sliced, &o->info); free(sliced);
  o->has_info = true;
  const char *spec_key = qa_qw_info_get(&o->info, "spectator");
  o->spec_dup = dupstr(spec_key ? spec_key : "");
  bool spectator = o->spec_dup[0] && strcmp(o->spec_dup, "0") != 0;
  const char *password = spectator ? s->host.spectator_password : s->host.password;
  const char *supplied_password = qa_qw_info_get(&o->info, "password");
  const char *supplied = spectator ? o->spec_dup : (supplied_password ? supplied_password : "");
  o->pw_lower = lower_dup(password);
  if (password[0] && strcmp(o->pw_lower, "none") != 0 && strcmp(password, supplied) != 0) {
    reply_text(o, spectator ? "n\nrequires a spectator password\n\n" : "n\nserver requires a password\n\n");
    return;
  }
  qa_qw_info_delete(&o->info, spectator ? "spectator" : "password");
  if (spectator) qa_qw_info_set(&o->info, "*spectator", "1");
  const char *wide = qa_qw_info_get(&o->info, "*wide");
  bool donor = wide && strcmp(wide, "1") == 0;
  o->userinfo = qa_qw_info_string(&o->info);
  if (!s->host.high_characters) {
    size_t w = 0;
    for (size_t i = 0; o->userinfo[i]; i++) if ((uint8_t)o->userinfo[i] > 31 && (uint8_t)o->userinfo[i] <= 127) o->userinfo[w++] = o->userinfo[i];
    o->userinfo[w] = '\0';
  }
  if (strlen(o->userinfo) > 195) o->userinfo[195] = '\0';
  qa_qw_connect_request req; memset(&req, 0, sizeof req);
  if (!qa_net_address_clone(from, &o->from)) qa_range_error("out of memory");
  o->has_from = true;
  req.from = o->from;
  req.qport = qport; req.challenge = challenge; req.userinfo = o->userinfo;
  req.spectator = spectator; req.donor_wide = donor;
  qa_qw_host_result res = s->host.connect(s->host.ctx, &req, now_ms);
  if (res.kind == 0) reply_text(o, "j");
  else if (res.kind == 1) reply_n(o, res.reason);
}
uint8_t **qa_qw_connectionless_receive(qa_qw_connectionless_server *s, const uint8_t *bytes, size_t length,
                                       const qa_net_address *from, double now_ms,
                                       size_t **reply_lens, size_t *reply_count) {
  oob_scope o;
  memset(&o, 0, sizeof o);
  qa_cleanup guard;
  qa_cleanup_push(&guard, oob_scope_abandon, &o);
  connectionless_body(s, bytes, length, from, now_ms, &o);
  qa_cleanup_pop(&guard);
  uint8_t **replies = o.acc.replies;
  *reply_lens = o.acc.lens; *reply_count = o.acc.count;
  oob_scope_release(&o, false);
  return replies;
}
int qa_qw_connect_client_init(qa_qw_connect_client *c, double qport, const char *userinfo) {
  memset(c, 0, sizeof *c); c->kind = 0; c->sent_at = -INFINITY; c->qport = qport;
  c->userinfo = dupstr(userinfo); return c->userinfo != NULL;
}
void qa_qw_connect_client_destroy(qa_qw_connect_client *c) { free(c->reason); free(c->userinfo); memset(c, 0, sizeof *c); }
uint8_t *qa_qw_connect_client_next(qa_qw_connect_client *c, double now_ms, size_t *out_len) {
  if (c->kind == 2 || c->kind == 3 || now_ms - c->sent_at < 5000) return NULL;
  c->sent_at = now_ms;
  if (c->kind == 0) return qa_quakeworld_out_of_band("getchallenge\n", false, out_len);
  char *msg = malloc(strlen(c->userinfo) + 128);
  snprintf(msg, strlen(c->userinfo) + 128, "connect 28 %s %s \"%s\"\n", QA_JS_NUMBER(c->qport), QA_JS_NUMBER(c->challenge), c->userinfo);
  uint8_t *b = qa_quakeworld_out_of_band(msg, false, out_len); free(msg); return b;
}
void qa_qw_connect_client_receive(qa_qw_connect_client *c, const uint8_t *bytes, size_t length) {
  char *text = qa_read_quakeworld_out_of_band(bytes, length);
  if (text[0] == 'c') {
    double challenge = js_parse_int(text + 1);
    if (isfinite(challenge) && challenge == trunc(challenge)) { c->kind = 1; c->challenge = challenge; c->sent_at = -INFINITY; }
  } else if (strcmp(text, "j") == 0) c->kind = 2;
  else if (text[0] == 'n') { c->kind = 3; free(c->reason); c->reason = dupstr(text + 1); }
  free(text);
}
uint8_t *qa_quakeworld_heartbeat(double sequence, double active_clients, size_t *out_len) {
  char buf[96]; snprintf(buf, sizeof buf, "a\n%s\n%s\n", QA_JS_NUMBER(sequence), QA_JS_NUMBER(active_clients));
  return qa_quakeworld_out_of_band(buf, false, out_len);
}
uint8_t *qa_quakeworld_shutdown(size_t *out_len) { return qa_quakeworld_out_of_band("C\n", false, out_len); }
void qa_nq_connect_client_init(qa_nq_connect_client *c) {
  memset(c, 0, sizeof *c); c->kind = 0; c->attempts = 0; c->sent_at = -INFINITY;
}
void qa_nq_connect_client_destroy(qa_nq_connect_client *c) { free(c->reason); memset(c, 0, sizeof *c); }
uint8_t *qa_nq_connect_client_next(qa_nq_connect_client *c, double now_ms, size_t *out_len) {
  if (c->kind != 0 || now_ms - c->sent_at < 2500) return NULL;
  if (c->attempts == 3) { c->kind = 2; free(c->reason); c->reason = dupstr("No response"); return NULL; }
  c->attempts += 1; c->sent_at = now_ms;
  qa_netquake_control m; memset(&m, 0, sizeof m);
  m.kind = QA_NQ_CONNECT_REQUEST; m.game = (char *)"QUAKE"; m.version = 3;
  return qa_encode_netquake_control(&m, out_len);
}
void qa_nq_connect_client_receive(qa_nq_connect_client *c, const uint8_t *bytes, size_t length) {
  qa_netquake_control m; qa_decode_netquake_control(bytes, length, &m);
  if (m.kind == QA_NQ_REJECT) { c->kind = 2; free(c->reason); c->reason = dupstr(m.reason); }
  else if (m.kind == QA_NQ_ACCEPT) {
    if (m.port < 1 || m.port > 65535) { qa_netquake_control_destroy(&m); qa_q1_packet_error("Invalid accepted port", 5); }
    c->kind = 1; c->port = m.port;
  }
  qa_netquake_control_destroy(&m);
}
void qa_qw_master_heartbeat_init(qa_qw_master_heartbeat *h) { h->previous_ms = -INFINITY; h->sequence = 0; }
uint8_t *qa_qw_master_heartbeat_next(qa_qw_master_heartbeat *h, double now_ms, double active_clients, bool force, size_t *out_len) {
  if (!force && now_ms - h->previous_ms < 300000) return NULL;
  h->previous_ms = now_ms;
  return qa_quakeworld_heartbeat(++h->sequence, active_clients, out_len);
}
