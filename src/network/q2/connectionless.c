/* Port of src/network/q2/connectionless.ts. GPL-2.0-or-later. */
#include "qa/network/q2/connectionless.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/common_parse.h"
#include "qa/foundation/support.h"
#include "qa/network/q2/constants.h"
#include "qa/network/q2/message.h"

static char *dup_str(const char *s) {
  size_t n = strlen(s);
  char *out = (char *)malloc(n + 1);
  if (out != NULL) memcpy(out, s, n + 1);
  return out;
}
static char *dup_span(const char *s, size_t n) {
  char *out = (char *)malloc(n + 1);
  if (out != NULL) {
    memcpy(out, s, n);
    out[n] = '\0';
  }
  return out;
}
/* JS number-to-string for the values used here (integers plainly). */
static void number_text(double value, char *buffer, size_t capacity) {
  if (value == (double)(int64_t)value) {
    (void)snprintf(buffer, capacity, "%lld", (long long)(int64_t)value);
  } else {
    (void)snprintf(buffer, capacity, "%g", value);
  }
}

char *qa_q2_status_text(const qa_q2_status *status, bool has_maximum, double maximum) {
  double limit = has_maximum ? maximum : 1384;
  const char *info = status->server_info != NULL ? status->server_info : "";
  size_t capacity = strlen(info) + 2 + status->player_count * 64 +
                    (status->player_count > 0 ? 0 : 1);
  for (size_t i = 0; i < status->player_count; i++) {
    capacity += strlen(status->players[i].name != NULL ? status->players[i].name : "");
  }
  char *result = (char *)malloc(capacity);
  if (result == NULL) return NULL;
  int wrote = snprintf(result, capacity, "%s\n", info);
  size_t length = wrote > 0 ? (size_t)wrote : 0;
  if ((double)length >= limit) {
    free(result);
    qa_range_error("Q2 serverinfo exceeds status packet capacity");
  }
  for (size_t i = 0; i < status->player_count; i++) {
    char score[32];
    char ping[32];
    number_text(status->players[i].score, score, sizeof score);
    number_text(status->players[i].ping, ping, sizeof ping);
    const char *name = status->players[i].name != NULL ? status->players[i].name : "";
    size_t line_len = strlen(score) + strlen(ping) + strlen(name) + 5;
    if ((double)(length + line_len) >= limit) break;
    wrote = snprintf(result + length, capacity - length, "%s %s \"%s\"\n", score, ping,
                     name);
    if (wrote > 0) length += (size_t)wrote;
  }
  return result;
}

void qa_q2_mirror_server_status_free(qa_q2_mirror_server_status *status) {
  free(status->name);
  free(status->map);
  if (status->rules != NULL) {
    for (size_t i = 0; i < status->rule_count; i++) {
      free(status->rules[i].key);
      free(status->rules[i].value);
    }
    free(status->rules);
  }
  if (status->player_details != NULL) {
    for (size_t i = 0; i < status->player_detail_count; i++) free(status->player_details[i].name);
    free(status->player_details);
  }
  memset(status, 0, sizeof *status);
}

static bool split_all(const char *text, char separator, char ***out, size_t *out_count) {
  size_t capacity = 8;
  size_t count = 0;
  char **parts = (char **)calloc(capacity, sizeof(char *));
  if (parts == NULL) return false;
  const char *start = text;
  for (;;) {
    const char *at = strchr(start, separator);
    size_t len = at != NULL ? (size_t)(at - start) : strlen(start);
    if (count == capacity) {
      capacity *= 2;
      char **grown = (char **)realloc(parts, capacity * sizeof(char *));
      if (grown == NULL) {
        for (size_t i = 0; i < count; i++) free(parts[i]);
        free(parts);
        return false;
      }
      parts = grown;
    }
    parts[count] = dup_span(start, len);
    if (parts[count] == NULL) {
      for (size_t i = 0; i < count; i++) free(parts[i]);
      free(parts);
      return false;
    }
    count++;
    if (at == NULL) break;
    start = at + 1;
  }
  *out = parts;
  *out_count = count;
  return true;
}
static void free_split(char **parts, size_t count) {
  for (size_t i = 0; i < count; i++) free(parts[i]);
  free(parts);
}

bool qa_q2_read_status(const qa_q2_connectionless_message *message,
                       qa_q2_protocol_identity protocol,
                       qa_q2_mirror_server_status *out) {
  memset(out, 0, sizeof *out);
  if (strcmp(message->command != NULL ? message->command : "", "print") != 0) return false;
  char **lines = NULL;
  size_t line_count = 0;
  if (!split_all(message->body != NULL ? message->body : "", '\n', &lines, &line_count)) return false;
  const char *info = line_count > 0 ? lines[0] : NULL;
  if (info == NULL || info[0] != '\\') {
    free_split(lines, line_count);
    return false;
  }
  char **fields = NULL;
  size_t field_count = 0;
  if (!split_all(info + 1, '\\', &fields, &field_count)) {
    free_split(lines, line_count);
    return false;
  }
  size_t rule_capacity = 8;
  size_t rule_count = 0;
  qa_q2_mirror_rule *rules = (qa_q2_mirror_rule *)calloc(rule_capacity, sizeof *rules);
  if (rules == NULL) {
    free_split(fields, field_count);
    free_split(lines, line_count);
    return false;
  }
  bool oom = false;
  for (size_t i = 0; i + 1 < field_count && !oom; i += 2) {
    long at = -1;
    for (size_t k = 0; k < rule_count; k++) {
      if (strcmp(rules[k].key, fields[i]) == 0) at = (long)k;
    }
    char *value = dup_str(fields[i + 1]);
    if (value == NULL) { oom = true; break; }
    if (at >= 0) {
      free(rules[at].value);
      rules[at].value = value;
    } else {
      if (rule_count == rule_capacity) {
        rule_capacity *= 2;
        qa_q2_mirror_rule *grown =
            (qa_q2_mirror_rule *)realloc(rules, rule_capacity * sizeof *grown);
        if (grown == NULL) {
          free(value);
          oom = true;
          break;
        }
        rules = grown;
      }
      char *owned_key = dup_str(fields[i]);
      if (owned_key == NULL) {
        free(value);
        oom = true;
        break;
      }
      rules[rule_count].key = owned_key;
      rules[rule_count].value = value;
      rule_count++;
    }
  }
  free_split(fields, field_count);
  if (oom) {
    for (size_t i = 0; i < rule_count; i++) {
      free(rules[i].key);
      free(rules[i].value);
    }
    free(rules);
    free_split(lines, line_count);
    return false;
  }
  size_t player_capacity = 8;
  size_t player_count = 0;
  qa_q2_mirror_player_detail *players =
      (qa_q2_mirror_player_detail *)calloc(player_capacity, sizeof *players);
  if (players == NULL) {
    for (size_t i = 0; i < rule_count; i++) {
      free(rules[i].key);
      free(rules[i].value);
    }
    free(rules);
    free_split(lines, line_count);
    return false;
  }
  for (size_t li = 1; li < line_count && !oom; li++) {
    const char *line = lines[li];
    /* /^(-?\d+) (-?\d+) "(.*)"$/ */
    const char *p = line;
    char *end = NULL;
    double score = strtod(p, &end);
    if (end == p || *end != ' ') continue;
    /* Score must be an integer token. */
    bool int_ok = true;
    const char *q = p;
    if (*q == '-') q++;
    if (*q == '\0') continue;
    for (; q < end; q++) {
      if (!isdigit((unsigned char)*q)) int_ok = false;
    }
    if (!int_ok) continue;
    p = end + 1;
    double ping = strtod(p, &end);
    if (end == p) continue;
    q = p;
    if (*q == '-') q++;
    int_ok = *q != '\0';
    for (; q < end; q++) {
      if (!isdigit((unsigned char)*q)) int_ok = false;
    }
    if (!int_ok) continue;
    if (end[0] != ' ' || end[1] != '"') continue;
    const char *name_start = end + 2;
    size_t name_len = strlen(name_start);
    if (name_len < 1 || name_start[name_len - 1] != '"') continue;
    if (player_count == player_capacity) {
      player_capacity *= 2;
      qa_q2_mirror_player_detail *grown = (qa_q2_mirror_player_detail *)realloc(
          players, player_capacity * sizeof *grown);
      if (grown == NULL) { oom = true; break; }
      players = grown;
    }
    players[player_count].name = dup_span(name_start, name_len - 1);
    if (players[player_count].name == NULL) { oom = true; break; }
    players[player_count].score = score;
    players[player_count].ping = ping;
    player_count++;
  }
  free_split(lines, line_count);
  if (oom) {
    for (size_t i = 0; i < rule_count; i++) {
      free(rules[i].key);
      free(rules[i].value);
    }
    free(rules);
    for (size_t i = 0; i < player_count; i++) free(players[i].name);
    free(players);
    return false;
  }
  const char *hostname = "";
  const char *mapname = "";
  double max_clients = 0;
  for (size_t i = 0; i < rule_count; i++) {
    if (strcmp(rules[i].key, "hostname") == 0) hostname = rules[i].value;
    if (strcmp(rules[i].key, "mapname") == 0) mapname = rules[i].value;
    if (strcmp(rules[i].key, "maxclients") == 0) max_clients = strtod(rules[i].value, NULL);
  }
  out->name = dup_str(hostname);
  out->map = dup_str(mapname);
  if (out->name == NULL || out->map == NULL) {
    free(out->name);
    free(out->map);
    for (size_t i = 0; i < rule_count; i++) {
      free(rules[i].key);
      free(rules[i].value);
    }
    free(rules);
    for (size_t i = 0; i < player_count; i++) free(players[i].name);
    free(players);
    memset(out, 0, sizeof *out);
    return false;
  }
  out->players = (double)player_count;
  out->max_players = max_clients;
  out->rules = rules;
  out->rule_count = rule_count;
  out->player_details = players;
  out->player_detail_count = player_count;
  out->protocol = protocol;
  return true;
}

void qa_q2_discovery_wire_init(qa_q2_mirror_discovery_wire *wire,
                               qa_q2_protocol_identity protocol, qa_q2_status_fn status,
                               void *ctx) {
  wire->protocol = protocol;
  wire->status = status;
  wire->status_ctx = ctx;
}

uint8_t *qa_q2_discovery_wire_query(const qa_q2_mirror_discovery_wire *wire,
                                    qa_q2_discovery_query_kind kind, size_t *out_length) {
  if (kind == QA_Q2_DISCOVERY_INFO) {
    char buffer[64];
    (void)snprintf(buffer, sizeof buffer, "info %lld",
                   (long long)wire->protocol.version);
    return qa_q2_out_of_band(buffer, false, out_length);
  }
  return qa_q2_out_of_band("status", false, out_length);
}

uint8_t *qa_q2_discovery_wire_master_query(const qa_q2_mirror_discovery_wire *wire,
                                           size_t *out_length) {
  (void)wire;
  uint8_t *out = (uint8_t *)malloc(5);
  if (out == NULL) return NULL;
  memcpy(out, "query", 5);
  if (out_length != NULL) *out_length = 5;
  return out;
}

uint8_t *qa_q2_discovery_wire_heartbeat(const qa_q2_mirror_discovery_wire *wire,
                                        bool active, size_t *out_length) {
  if (!active) return qa_q2_out_of_band("shutdown", false, out_length);
  qa_q2_status status = wire->status(wire->status_ctx);
  char *text = qa_q2_status_text(&status, false, 0);
  if (text == NULL) return NULL;
  size_t needed = strlen("heartbeat\n") + strlen(text) + 1;
  char *full = (char *)malloc(needed);
  if (full == NULL) {
    free(text);
    return NULL;
  }
  (void)snprintf(full, needed, "heartbeat\n%s", text);
  free(text);
  uint8_t *out = qa_q2_out_of_band(full, false, out_length);
  free(full);
  return out;
}

void qa_q2_master_reply_free(qa_net_address *addresses, size_t count) {
  if (addresses != NULL) {
    for (size_t i = 0; i < count; i++) qa_net_address_destroy(&addresses[i]);
    free(addresses);
  }
}

qa_net_address *qa_q2_read_master_reply(const uint8_t *bytes, size_t length,
                                        size_t *out_count) {
  size_t start = 0;
  if (length >= 4 && bytes[0] == 255 && bytes[1] == 255 && bytes[2] == 255 &&
      bytes[3] == 255) {
    start = 4;
  }
  if (length - start < 8) return NULL;
  const uint8_t *prefix = bytes + start;
  if (memcmp(prefix, "servers", 7) != 0 || (prefix[7] != 32 && prefix[7] != 10)) {
    return NULL;
  }
  start += 8;
  if ((length - start) % 6 != 0) qa_q2_throw_error("Partial Q2 master address");
  size_t capacity = 16;
  size_t count = 0;
  qa_net_address *found = (qa_net_address *)calloc(capacity, sizeof *found);
  if (found == NULL) return NULL;
  for (size_t offset = start; offset < length; offset += 6) {
    int port = ((int)bytes[offset + 4] << 8) | (int)bytes[offset + 5];
    if (port == 0) continue;
    int64_t host[4] = {bytes[offset], bytes[offset + 1], bytes[offset + 2],
                       bytes[offset + 3]};
    qa_net_address address;
    memset(&address, 0, sizeof address);
    if (qa_net_make_ipv4(&address, host, port, false) == 0) {
      qa_q2_master_reply_free(found, count);
      return NULL;
    }
    bool seen = false;
    for (size_t i = 0; i < count; i++) {
      if (qa_net_same_address(&found[i], &address, true)) seen = true;
    }
    if (seen) {
      qa_net_address_destroy(&address);
      continue;
    }
    if (count == capacity) {
      capacity *= 2;
      qa_net_address *grown =
          (qa_net_address *)realloc(found, capacity * sizeof *grown);
      if (grown == NULL) {
        qa_net_address_destroy(&address);
        qa_q2_master_reply_free(found, count);
        return NULL;
      }
      found = grown;
    }
    found[count++] = address;
  }
  *out_count = count;
  return found;
}

void qa_q2_connect_admission_free(qa_q2_connect_admission *admission) {
  free(admission->response_arguments);
  free(admission->reason);
  memset(admission, 0, sizeof *admission);
}

static char *pad_start(const char *text, size_t width) {
  size_t n = strlen(text);
  if (n >= width) return dup_str(text);
  char *out = (char *)malloc(width + 1);
  if (out == NULL) return NULL;
  size_t pad = width - n;
  memset(out, ' ', pad);
  memcpy(out + pad, text, n + 1);
  return out;
}

char *qa_q2_info_text(qa_q2_host_info info, const qa_q2_protocol_identity *protocols,
                      size_t protocol_count, double version) {
  if (info.max_players == 1) return NULL;
  bool known = false;
  for (size_t i = 0; i < protocol_count; i++) {
    if ((double)protocols[i].version == version) known = true;
  }
  const char *name = info.name != NULL ? info.name : "";
  const char *map = info.map != NULL ? info.map : "";
  if (!known) {
    size_t needed = strlen("info\n") + strlen(name) + strlen(": wrong version\n") + 1;
    char *out = (char *)malloc(needed);
    if (out == NULL) return NULL;
    (void)snprintf(out, needed, "info\n%s: wrong version\n", name);
    return out;
  }
  char players[32];
  char maxp[32];
  number_text(info.players, players, sizeof players);
  number_text(info.max_players, maxp, sizeof maxp);
  char *padded_name = pad_start(name, 16);
  char *padded_map = pad_start(map, 8);
  char *padded_players = pad_start(players, 2);
  char *padded_max = pad_start(maxp, 2);
  if (padded_name == NULL || padded_map == NULL || padded_players == NULL ||
      padded_max == NULL) {
    free(padded_name);
    free(padded_map);
    free(padded_players);
    free(padded_max);
    return NULL;
  }
  size_t needed = strlen("info\n") + strlen(padded_name) + strlen(padded_map) +
                  strlen(padded_players) + strlen(padded_max) + 8;
  char *out = (char *)malloc(needed);
  if (out != NULL) {
    (void)snprintf(out, needed, "info\n%s %s %s/%s\n", padded_name, padded_map,
                   padded_players, padded_max);
  }
  free(padded_name);
  free(padded_map);
  free(padded_players);
  free(padded_max);
  return out;
}

static void host_reply_text(const qa_q2_connectionless_host *host,
                            const qa_net_address *to, const char *text) {
  size_t length = 0;
  uint8_t *packet = qa_q2_out_of_band(text, false, &length);
  if (packet != NULL) {
    host->reply(host->ctx, to, packet, length);
    free(packet);
  }
}

bool qa_q2_connectionless_server_receive(qa_q2_connectionless_server *server,
                                         const qa_net_address *from,
                                         const uint8_t *bytes, size_t length,
                                         double now) {
  const qa_q2_connectionless_host *host = server->host;
  qa_q2_connectionless_message message;
  if (!qa_q2_read_out_of_band(bytes, length, false, &message)) return false;
  const char *command = message.command != NULL ? message.command : "";
  bool result = false;
  if (strcmp(command, "ping") == 0) {
    host_reply_text(host, from, "ack");
    result = true;
  } else if (strcmp(command, "status") == 0) {
    qa_q2_status status = host->status(host->ctx);
    char *status_text = qa_q2_status_text(&status, false, 0);
    if (status_text != NULL) {
      size_t needed = strlen("print\n") + strlen(status_text) + 1;
      char *full = (char *)malloc(needed);
      if (full != NULL) {
        (void)snprintf(full, needed, "print\n%s", status_text);
        host_reply_text(host, from, full);
        free(full);
      }
      free(status_text);
    }
    result = true;
  } else if (strcmp(command, "info") == 0) {
    double version = message.argument_count > 0 ? strtod(message.arguments[0], NULL) : 0.0 / 0.0;
    /* TS Number(undefined) is NaN; missing arg compares unequal. */
    if (message.argument_count == 0) version = NAN;
    qa_q2_host_info info = host->info(host->ctx);
    char *text = qa_q2_info_text(info, host->protocols, host->protocol_count, version);
    if (text != NULL) {
      host_reply_text(host, from, text);
      free(text);
    }
    result = true;
  } else if (strcmp(command, "getchallenge") == 0) {
    size_t reply_len = 0;
    uint8_t *reply = qa_q2_challenge_table_reply(server->challenges, from, now,
                                                 host->protocols, host->protocol_count,
                                                 &reply_len);
    if (reply != NULL) {
      host->reply(host->ctx, from, reply, reply_len);
      free(reply);
    }
    result = true;
  } else if (strcmp(command, "connect") == 0) {
    qa_q2_connect_request request;
    if (!qa_q2_read_connect(&message, &request)) {
      qa_q2_connectionless_message_free(&message);
      return false;
    }
    bool known = false;
    for (size_t i = 0; i < host->protocol_count; i++) {
      if (host->protocols[i].version == request.protocol.version) known = true;
    }
    if (!known) {
      host_reply_text(host, from, "print\nUnsupported protocol.\n");
      qa_q2_connect_request_free(&request);
      result = true;
    } else if (!qa_q2_challenge_table_validate(server->challenges, from,
                                               request.challenge)) {
      host_reply_text(host, from, "print\nBad challenge.\n");
      qa_q2_connect_request_free(&request);
      result = true;
    } else {
      qa_q2_connect_admission admission = host->connect(host->ctx, from, &request);
      if (admission.accepted) {
        if (admission.response_arguments != NULL) {
          size_t needed = strlen("client_connect ") + strlen(admission.response_arguments) + 1;
          char *full = (char *)malloc(needed);
          if (full != NULL) {
            (void)snprintf(full, needed, "client_connect %s", admission.response_arguments);
            host_reply_text(host, from, full);
            free(full);
          }
        } else {
          host_reply_text(host, from, "client_connect");
        }
      } else {
        const char *reason = admission.reason != NULL ? admission.reason : "";
        size_t needed = strlen("print\n") + strlen(reason) + 2;
        char *full = (char *)malloc(needed);
        if (full != NULL) {
          (void)snprintf(full, needed, "print\n%s\n", reason);
          host_reply_text(host, from, full);
          free(full);
        }
      }
      qa_q2_connect_admission_free(&admission);
      qa_q2_connect_request_free(&request);
      result = true;
    }
  } else if (strcmp(command, "rcon") == 0) {
    qa_q2_handle_rcon(host, from, &message, now);
    result = true;
  }
  qa_q2_connectionless_message_free(&message);
  return result;
}

typedef struct rcon_buffer {
  const qa_q2_connectionless_host *host;
  const qa_net_address *from;
  char *buffer;
  size_t length;
  size_t capacity;
} rcon_buffer;

static void rcon_flush(rcon_buffer *b) {
  if (b->length > 0) {
    size_t needed = strlen("print\n") + b->length + 1;
    char *full = (char *)malloc(needed);
    if (full != NULL) {
      memcpy(full, "print\n", 6);
      memcpy(full + 6, b->buffer, b->length);
      full[6 + b->length] = '\0';
      host_reply_text(b->host, b->from, full);
      free(full);
    }
    b->length = 0;
    if (b->buffer != NULL) b->buffer[0] = '\0';
  }
}

static void rcon_output(void *vctx, const char *text) {
  rcon_buffer *b = (rcon_buffer *)vctx;
  /* TS iterates Unicode characters; byte strings here iterate bytes. */
  for (const unsigned char *p = (const unsigned char *)text; *p != 0; p++) {
    if (b->length == 1383) rcon_flush(b);
    if (b->length + 2 > b->capacity) {
      size_t next = b->capacity > 0 ? b->capacity * 2 : 1400;
      char *grown = (char *)realloc(b->buffer, next);
      if (grown == NULL) return;
      b->buffer = grown;
      b->capacity = next;
    }
    b->buffer[b->length++] = (char)*p;
    b->buffer[b->length] = '\0';
  }
}

void qa_q2_handle_rcon(const qa_q2_connectionless_host *host,
                       const qa_net_address *from,
                       const qa_q2_connectionless_message *message, double now) {
  if (host->profile_rerelease && !host->rcon_rate_allowed(host->ctx, now)) return;
  const char *password = message->argument_count > 0 ? message->arguments[0] : "";
  const char *full = host->rcon_password(host->ctx);
  const char *limit_password = NULL;
  const char **prefixes = NULL;
  size_t prefix_count = 0;
  bool has_limit = host->limited_rcon(host->ctx, &limit_password, &prefixes, &prefix_count);
  bool full_ok = full != NULL && full[0] != '\0' && strcmp(password, full) == 0;
  bool limited = !full_ok && host->profile_rerelease && has_limit &&
                 limit_password != NULL && limit_password[0] != '\0' &&
                 strcmp(password, limit_password) == 0;
  if (!full_ok && !limited) {
    host_reply_text(host, from, "print\nBad rcon_password.\n");
    return;
  }
  if (host->profile_rerelease) host->recharge_rcon_rate(host->ctx);
  char *command = NULL;
  if (!host->profile_rerelease) {
    size_t needed = 2;
    for (size_t i = 1; i < message->argument_count; i++) needed += strlen(message->arguments[i]) + 1;
    command = (char *)malloc(needed);
    if (command == NULL) return;
    char *w = command;
    for (size_t i = 1; i < message->argument_count; i++) {
      if (i > 1) *w++ = ' ';
      size_t n = strlen(message->arguments[i]);
      memcpy(w, message->arguments[i], n);
      w += n;
    }
    *w++ = ' ';
    *w = '\0';
  } else {
    qa_legacy_parse_state cursor = {message->text, strlen(message->text != NULL ? message->text : ""), 0};
    char *t1 = qa_parse_q2_token(&cursor, 0);
    free(t1);
    char *t2 = qa_parse_q2_token(&cursor, 0);
    free(t2);
    const char *rest = message->text != NULL ? message->text + cursor.index : "";
    while (*rest != '\0' && isspace((unsigned char)*rest)) rest++;
    command = dup_str(rest);
    if (command == NULL) return;
  }
  if (limited && has_limit) {
    bool permitted = false;
    for (size_t i = 0; i < prefix_count; i++) {
      if (strncmp(command, prefixes[i], strlen(prefixes[i])) == 0) permitted = true;
    }
    if (!permitted) {
      host_reply_text(host, from, "print\nThis command is not permitted.\n");
      free(command);
      return;
    }
  }
  rcon_buffer buffer = {host, from, NULL, 0, 0};
  host->execute_rcon(host->ctx, command, limited, rcon_output, &buffer);
  rcon_flush(&buffer);
  free(buffer.buffer);
  free(command);
}
