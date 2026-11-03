/* Port of src/app/bootstrap/network/qw-server.ts.
 * GPL-2.0-or-later. */
#include "qa/network/common/js_number.h"
#include "qa/app/bootstrap/network/qw_server.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/core/common_parse.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/network/q1/channels.h"
#include "qa/network/q1/commands.h"
#include "qa/network/q1/handshake.h"
#include "qa/network/q1/message.h"
#include "qa/network/q1/session.h"
#include "qa/network/services/admin.h"
#include "qa/network/common/session.h"
#include "qa/network/common/diagnostics.h"
#include "qa/network/common/movement_budget.h"
#include "qa/foundation/js.h"
#define MAX_PEERS_PER_ADDRESS 8
static void *xmalloc(size_t size) {
  void *p = malloc(size ? size : 1);
  if (!p) qa_range_error("out of memory");
  return p;
}
static char *copy_text(const char *s) {
  size_t n = strlen(s) + 1;
  char *out = xmalloc(n);
  memcpy(out, s, n);
  return out;
}
static bool client_equal(qa_client_id a, qa_client_id b) {
  return a.slot == b.slot && a.generation == b.generation && a.token == b.token;
}
static qa_quakeworld_profile qw_profile(void) {
  return qa_quakeworld_profile_make(28, 0);
}
typedef struct frame_store {
  double sequence; qa_qw_entity_state *states; size_t count;
} frame_store;
typedef struct ping_frame { bool used; double sequence, sent, ping; } ping_frame; /* TS Map entry when used */
typedef struct peer peer;
struct peer {
  qa_net_address remote;
  qa_app_qw_player player;
  qa_quakeworld_channel channel;
  qa_quakeworld_signon_server *signon;
  void *signon_ctx;
  qa_quakeworld_command_replay replay;
  qa_movement_budget budget;
  qa_qw_baseline_map_entry *baselines; size_t baseline_count;
  qa_qw_entity_state *baseline_states;
  uint8_t **reliable; size_t *reliable_lengths; size_t reliable_head, reliable_count;
  double reliable_length;
  frame_store *frames; size_t frame_count, frame_cap;
  bool active;
  bool preparing;
  bool reply;
  bool has_delta; double delta;
  double choked;
  double last_received;
  double message_level;
  double loss_percent;
  ping_frame ping_frames[64];
  qa_source_chat_flood chat_flood;
  struct peer_network *network; /* NULL until networkInfo is published */
};
/* The networkInfo closure state: it outlives its peer, as the TS closure does. */
typedef struct peer_network { peer *p; char *address; double last_ping; } peer_network;
struct qa_app_qw_server_network {
  qa_app_qw_server_network_options options;
  qa_app_qw_server_host host;
  peer **peers; size_t peer_count, peer_cap;
  /* Removed peers stay allocated until no frame can still hold them, as TS
   * objects outlive their removal; reaped at the end of each entry point. */
  peer **retired; size_t retired_count, retired_cap;
  qa_connectionless_gate gate;
  qa_diagnostic_budget diagnostics;
  qa_qw_connectionless_server connectionless;
  qa_qw_challenges challenges;
  qa_qw_master_heartbeat heartbeat_state;
  peer_network **networks; size_t network_count, network_cap;
  bool ended;
};
static bool host_paused(const qa_app_qw_server_network *n) {
  return n->host.paused != NULL && n->host.paused(n->host.ctx);
}
static qa_datagram_transport *transport_of(qa_app_qw_server_network *n) {
  return n->options.transport;
}
static void validate_host(qa_app_qw_server_host *host) {
  const void *support = host->supports_source_wire ? host->supports_source_wire(host->ctx) : NULL;
  if (support) {
    const qa_wire_admission *admission = support;
    if (!admission->supported) {
      size_t total = 1;
      for (size_t i = 0; i < admission->reason_count; i++) total += strlen(admission->reasons[i]) + 2;
      char *message = xmalloc(total);
      size_t used = 0;
      for (size_t i = 0; i < admission->reason_count; i++) {
        if (i) { memcpy(message + used, "; ", 2); used += 2; }
        size_t len = strlen(admission->reasons[i]);
        memcpy(message + used, admission->reasons[i], len);
        used += len;
      }
      message[used] = '\0';
      qa_cleanup guard;
      qa_cleanup_push(&guard, free, message);
      qa_raise("Error", 0, 0, "%s", message);
    }
  }
  if (host->max_clients != trunc(host->max_clients) || host->max_clients < 1 || host->max_clients > 32)
    qa_raise("Error", 0, 0, "Native QW admits at most 32 players");
}
static const qa_app_qw_server_administration *administration(qa_app_qw_server_network *n) {
  return n->host.administration;
}
static const qa_app_qw_server_authentication *authentication(qa_app_qw_server_network *n) {
  return n->host.authentication;
}
static double rate_of(const char *value) {
  double rate = qa_native_atoi(value);
  if (rate < 500) rate = 500;
  if (rate > 10000) rate = 10000;
  return rate;
}
static bool peer_present(qa_app_qw_server_network *n, peer *p) {
  for (size_t i = 0; i < n->peer_count; i++)
    if (n->peers[i] == p) return true;
  return false;
}
static void destroy_buf_slot(void *arg) { qa_size_buf_destroy(arg); }
static uint8_t *messages_bytes(const qa_quakeworld_message *const *messages, size_t count,
    size_t *out_length) {
  qa_size_buf buffer;
  if (!qa_size_buf_init(&buffer, 1450, false)) qa_range_error("out of memory");
  qa_quakeworld_profile profile = qw_profile();
  qa_cleanup guard;
  qa_cleanup_push(&guard, destroy_buf_slot, &buffer);
  for (size_t i = 0; i < count; i++) qa_write_quakeworld_message(&buffer, profile, messages[i]);
  qa_cleanup_pop(&guard);
  uint8_t *out = xmalloc((size_t)buffer.cursize);
  memcpy(out, buffer.data, (size_t)buffer.cursize);
  *out_length = (size_t)buffer.cursize;
  qa_size_buf_destroy(&buffer);
  return out;
}
static void enqueue(peer *p, uint8_t *bytes, size_t length) {
  if (length == 0) { free(bytes); return; }
  if (length > 1450) { free(bytes); qa_raise("Error", 0, 0, "QW reliable block exceeds native message size"); }
  if (p->reliable_count > 0) {
    size_t last_index = (p->reliable_head + p->reliable_count - 1) % 8;
    size_t last_length = p->reliable_lengths[last_index];
    if (last_length + length <= 1450) {
      uint8_t *combined = xmalloc(last_length + length);
      memcpy(combined, p->reliable[last_index], last_length);
      memcpy(combined + last_length, bytes, length);
      free(p->reliable[last_index]);
      free(bytes);
      p->reliable[last_index] = combined;
      p->reliable_lengths[last_index] = last_length + length;
      return;
    }
  }
  if (p->reliable_count >= 5) { free(bytes); qa_raise("Error", 0, 0, "QW reliable back buffers overflow"); }
  size_t index = (p->reliable_head + p->reliable_count) % 8;
  p->reliable[index] = bytes;
  p->reliable_lengths[index] = length;
  p->reliable_count++;
}
static double peer_ping(peer *p);
static void peer_free(peer *p) {
  if (p->network != NULL) { p->network->last_ping = peer_ping(p); p->network->p = NULL; }
  qa_net_address_destroy(&p->remote);
  qa_quakeworld_channel_destroy(&p->channel);
  qa_quakeworld_signon_server_free(p->signon);
  free(p->signon_ctx);
  free(p->baselines);
  free(p->baseline_states);
  for (size_t i = 0; i < p->reliable_count; i++)
    free(p->reliable[(p->reliable_head + i) % 8]);
  free(p->reliable);
  free(p->reliable_lengths);
  for (size_t i = 0; i < p->frame_count; i++) free(p->frames[i].states);
  free(p->frames);
  free(p);
}
static void remove_peer(qa_app_qw_server_network *n, peer *p, const char *reason) {
  size_t index = n->peer_count;
  for (size_t i = 0; i < n->peer_count; i++)
    if (n->peers[i] == p) index = i;
  if (index == n->peer_count) return;
  memmove(n->peers + index, n->peers + index + 1,
      (n->peer_count - index - 1) * sizeof *n->peers);
  n->peer_count--;
  if (n->retired_count == n->retired_cap) {
    n->retired_cap = n->retired_cap ? n->retired_cap * 2 : 8;
    n->retired = realloc(n->retired, n->retired_cap * sizeof *n->retired);
    if (!n->retired) qa_range_error("out of memory");
  }
  n->retired[n->retired_count++] = p;
  qa_quakeworld_signon_server_close(p->signon);
  n->host.disconnect(n->host.ctx, p->player, reason);
}
static void reap_retired(qa_app_qw_server_network *n) {
  for (size_t i = 0; i < n->retired_count; i++) peer_free(n->retired[i]);
  n->retired_count = 0;
}
typedef struct farewell_scope { qa_app_qw_server_network *n; peer *p; } farewell_scope;
static void farewell_body(void *ctx) {
  farewell_scope *scope = ctx;
  qa_quakeworld_message message = {0};
  message.kind = "disconnect";
  const qa_quakeworld_message *messages[] = {&message};
  size_t length = 0;
  uint8_t *bytes = messages_bytes(messages, 1, &length);
  qa_cleanup guard;
  qa_cleanup_push(&guard, free, bytes);
  size_t packet_length = 0;
  uint8_t *packet = qa_quakeworld_channel_transmit(&scope->p->channel, bytes, length,
      scope->p->last_received, true, &packet_length);
  qa_cleanup_pop(&guard);
  free(bytes);
  if (!packet) qa_range_error("out of memory");
  qa_cleanup packet_guard;
  qa_cleanup_push(&packet_guard, free, packet);
  transport_of(scope->n)->send(transport_of(scope->n)->ctx, &scope->p->remote, packet, packet_length);
  qa_cleanup_pop(&packet_guard);
  free(packet);
}
static bool disconnect_client(qa_app_qw_server_network *n, qa_client_id client, const char *reason) {
  peer *p = NULL;
  for (size_t i = 0; i < n->peer_count; i++)
    if (client_equal(n->peers[i]->player.client, client)) p = n->peers[i];
  if (!p) return false;
  /* The farewell is best effort: an exhausted or overflowing channel must not stop the peer from being removed. */
  farewell_scope scope = { n, p };
  qa_caught_error caught;
  if (!qa_try(farewell_body, &scope, &caught)) {
    size_t total = strlen(caught.message) + 32;
    char *line = xmalloc(total);
    snprintf(line, total, "QW farewell failed: %s\n", caught.message);
    n->host.print(n->host.ctx, line);
    free(line);
  }
  remove_peer(n, p, reason);
  return true;
}
typedef struct deliver_scope { peer *p; const qa_quakeworld_message *message; } deliver_scope;
static void deliver_body(void *ctx) {
  deliver_scope *scope = ctx;
  const qa_quakeworld_message *messages[] = {scope->message};
  size_t length = 0;
  uint8_t *bytes = messages_bytes(messages, 1, &length);
  enqueue(scope->p, bytes, length);
}
static void deliver(qa_app_qw_server_network *n, peer *p, const qa_quakeworld_message *message) {
  if (!peer_present(n, p)) return;
  if (message->kind && strcmp(message->kind, "print") == 0 && message->level < p->message_level)
    return;
  deliver_scope scope = { p, message };
  qa_caught_error caught;
  if (!qa_try(deliver_body, &scope, &caught)) disconnect_client(n, p->player.client, caught.message);
}
static void client_print(qa_app_qw_server_network *n, peer *p, double level, const char *text) {
  qa_quakeworld_message message = {0};
  message.kind = "print";
  message.level = level;
  message.text = (char *)text;
  deliver(n, p, &message);
}
static double peer_ping(peer *p) {
  double sum = 0;
  size_t count = 0;
  for (size_t i = 0; i < 64; i++)
    if (p->ping_frames[i].ping > 0) {
      sum += p->ping_frames[i].ping;
      count++;
    }
  return count == 0 ? 9999 : trunc(sum / (double)count);
}
static const char *info_value(qa_app_qw_server_network *n, qa_app_qw_player player,
    const char *key) {
  const qa_qw_info *info = n->host.client_info(n->host.ctx, player);
  return info ? qa_qw_info_get(info, key) : NULL;
}
typedef struct text_builder { char *text; size_t length, capacity; } text_builder;
static void text_builder_free(void *arg) { text_builder *b = arg; free(b->text); b->text = NULL; }
static void text_append(text_builder *b, const char *piece) {
  size_t n = strlen(piece);
  if (b->length + n + 1 > b->capacity) {
    size_t capacity = b->capacity ? b->capacity : 64;
    while (capacity < b->length + n + 1) capacity *= 2;
    char *grown = realloc(b->text, capacity);
    if (!grown) qa_range_error("out of memory");
    b->text = grown; b->capacity = capacity;
  }
  memcpy(b->text + b->length, piece, n + 1);
  b->length += n;
}
/* String.prototype.trimStart over byte text (Latin-1 0xa0 is NBSP). */
static const char *js_trim_start(const char *s) {
  while (*s == ' ' || (*s >= '\t' && *s <= '\r') || (unsigned char)*s == 0xa0) s++;
  return s;
}
static bool client_command(qa_app_qw_server_network *n, peer *p, const char *name,
    char **args, size_t arg_count, const char *text, double now) {
  if (strcmp(name, "msg") == 0) {
    if (arg_count == 1) p->message_level = qa_native_atoi(args[0]);
    char line[96];
    snprintf(line, sizeof line, "%s %s\n",
        arg_count == 1 ? "Msg level set to" : "Current msg level is", QA_JS_NUMBER(p->message_level));
    client_print(n, p, 2, line);
    return true;
  }
  if (strcmp(name, "ping") == 0 || strcmp(name, "status") == 0) {
    char *status = NULL;
    if (strcmp(name, "status") == 0 && administration(n) && administration(n)->status)
      status = administration(n)->status(administration(n)->ctx);
    if (status) {
      client_print(n, p, 2, status);
      free(status);
      return true;
    }
    text_builder line = {0};
    qa_cleanup guard;
    qa_cleanup_push(&guard, text_builder_free, &line);
    char number[40];
    if (strcmp(name, "ping") == 0) text_append(&line, "Client ping times:\n");
    else {
      text_append(&line, "players: ");
      text_append(&line, qa_net_js_number_text((double)n->peer_count, number));
      text_append(&line, " active (");
      text_append(&line, qa_net_js_number_text(n->host.max_clients, number));
      text_append(&line, " max)\n");
    }
    for (size_t i = 0; i < n->peer_count; i++) {
      peer *other = n->peers[i];
      if (strcmp(name, "ping") == 0 && !other->active) continue;
      const char *player_name = info_value(n, other->player, "name");
      if (!player_name) player_name = "unnamed";
      if (strcmp(name, "ping") == 0) text_append(&line, qa_net_js_number_text(peer_ping(other), number));
      else { text_append(&line, "#"); text_append(&line, qa_net_js_number_text(other->player.slot + 1, number)); }
      text_append(&line, " ");
      text_append(&line, player_name);
      text_append(&line, "\n");
    }
    client_print(n, p, 2, line.text ? line.text : "");
    qa_cleanup_pop(&guard);
    text_builder_free(&line);
    return true;
  }
  if (strcmp(name, "pings") == 0) {
    for (size_t i = 0; i < n->peer_count; i++) {
      peer *other = n->peers[i];
      if (!other->active) continue;
      qa_quakeworld_message ping = {0};
      ping.kind = "ping";
      ping.slot = other->player.slot;
      ping.value = peer_ping(other);
      deliver(n, p, &ping);
      qa_quakeworld_message loss = {0};
      loss.kind = "packet-loss";
      loss.slot = other->player.slot;
      loss.value = other->loss_percent;
      deliver(n, p, &loss);
    }
    return true;
  }
  if (strcmp(name, "kill") == 0 && !p->active) {
    client_print(n, p, 2, "Can't suicide -- allready dead!\n");
    return true;
  }
  if (strcmp(name, "say") != 0 && strcmp(name, "say_team") != 0) return false;
  if (arg_count == 0) return true;
  double seconds = 0;
  const char *flood = qa_source_chat_flood_check(&p->chat_flood, now / 1000, host_paused(n), &seconds);
  if (strcmp(flood, "allowed") != 0) {
    char line[128];
    char number[40];
    qa_net_js_number(seconds, number, sizeof number);
    if (strcmp(flood, "locked") == 0)
      snprintf(line, sizeof line, "You can't talk for %s more seconds\n", number);
    else
      snprintf(line, sizeof line, "FloodProt: You can't talk for %s seconds.\n", number);
    client_print(n, p, 3, line);
    return true;
  }
  bool team = strcmp(name, "say_team") == 0;
  const char *team_value = info_value(n, p->player, "team");
  char sender_team[32] = "";
  snprintf(sender_team, sizeof sender_team, "%s", team_value ? team_value : "");
  qa_legacy_parse_state parsed = {0};
  parsed.data = text;
  char *token = qa_parse_q1_token(&parsed, true);
  free(token);
  size_t text_length = strlen(text);
  const char *words = js_trim_start(text + ((size_t)parsed.index < text_length ? (size_t)parsed.index : text_length));
  char *words_copy = copy_text(words);
  size_t words_length = strlen(words_copy);
  if (words_copy[0] == '"') {
    /* words.slice(1, -1): drops the first and the last character whatever the last is. */
    size_t kept = words_length >= 2 ? words_length - 2 : 0;
    memmove(words_copy, words_copy + 1, kept);
    words_copy[kept] = '\0';
  }
  const char *sender_value = info_value(n, p->player, "name");
  char sender[32] = "";
  snprintf(sender, sizeof sender, "%s", sender_value ? sender_value : "");
  size_t total = strlen(sender) + strlen(words_copy) + 8;
  char *message = xmalloc(total);
  if (team) snprintf(message, total, "(%s): %s\n", sender, words_copy);
  else snprintf(message, total, "%s: %s\n", sender, words_copy);
  free(words_copy);
  qa_cleanup message_guard;
  qa_cleanup_push(&message_guard, free, message);
  n->host.print(n->host.ctx, message);
  for (size_t i = 0; i < n->peer_count; i++) {
    peer *recipient = n->peers[i];
    if (!recipient->active) continue;
    if (team) {
      const char *value = info_value(n, recipient->player, "team");
      if (strcmp(value ? value : "", sender_team) != 0) continue;
    }
    client_print(n, recipient, 3, message);
  }
  qa_cleanup_pop(&message_guard);
  free(message);
  return true;
}
typedef struct signon_ctx {
  qa_app_qw_server_network *n;
  const qa_qw_signon_host *source;
  qa_app_qw_player player;
} signon_ctx;
static void signon_server_data(void *ctx, qa_quakeworld_message *out) {
  signon_ctx *signon = ctx;
  signon->source->server_data(signon->source->ctx, out);
}
static const char *const *signon_models(void *ctx, size_t *out_count) {
  signon_ctx *signon = ctx;
  return signon->source->models(signon->source->ctx, out_count);
}
static const char *const *signon_sounds(void *ctx, size_t *out_count) {
  signon_ctx *signon = ctx;
  return signon->source->sounds(signon->source->ctx, out_count);
}
static void signon_buffers(void *ctx, const uint8_t *const **out, const size_t **lens, size_t *out_count) {
  signon_ctx *signon = ctx;
  signon->source->signon_buffers(signon->source->ctx, out, lens, out_count);
}
static bool signon_checksum(void *ctx, uint32_t checksum) {
  signon_ctx *signon = ctx;
  return signon->source->accepts_map_checksum(signon->source->ctx, checksum);
}
static uint8_t **signon_spawn(void *ctx, double start, size_t **lens, size_t *out_count) {
  signon_ctx *signon = ctx;
  return signon->source->spawn(signon->source->ctx, start, lens, out_count);
}
static void signon_begin(void *ctx) {
  signon_ctx *signon = ctx;
  signon->source->begin(signon->source->ctx);
  for (size_t i = 0; i < signon->n->peer_count; i++)
    if (client_equal(signon->n->peers[i]->player.client, signon->player.client))
      signon->n->peers[i]->active = true;
}
static void signon_disconnect(void *ctx, const char *reason) {
  signon_ctx *signon = ctx;
  disconnect_client(signon->n, signon->player.client, reason);
}
static qa_download_source *signon_open_download(void *ctx, const char *path) {
  signon_ctx *signon = ctx;
  return signon->source->open_download(signon->source->ctx, path);
}
static void free_signon_slot(void *arg) { qa_quakeworld_signon_server_free(arg); }
typedef struct binding_result {
  qa_quakeworld_signon_server *signon; void *ctx;
  qa_qw_baseline_map_entry *baselines; size_t baseline_count;
  qa_qw_entity_state *baseline_states;
} binding_result;
static binding_result bind_player(qa_app_qw_server_network *n, qa_app_qw_player player,
    qa_app_qw_server_host *host) {
  const qa_qw_signon_host *source = host->signon(host->ctx, player);
  qa_quakeworld_message data = {0};
  source->server_data(source->ctx, &data);
  qa_quakeworld_profile profile = data.protocol;
  const char *kind = profile.kind;
  if (!kind || strcmp(kind, "q1-quakeworld") != 0 || profile.version != 28
      || data.player_slot != player.slot || player.slot < 0 || player.slot >= host->max_clients) {
    qa_quakeworld_message_destroy(&data);
    qa_raise("Error", 0, 0, "Source signon does not describe an admitted native QW player");
  }
  qa_quakeworld_message_destroy(&data);
  signon_ctx *ctx = xmalloc(sizeof *ctx);
  ctx->n = n;
  ctx->source = source;
  ctx->player = player;
  qa_qw_signon_host signon_host = {0};
  signon_host.ctx = ctx;
  signon_host.server_data = signon_server_data;
  signon_host.models = signon_models;
  signon_host.sounds = signon_sounds;
  signon_host.signon_buffers = signon_buffers;
  signon_host.accepts_map_checksum = signon_checksum;
  signon_host.spawn = signon_spawn;
  signon_host.begin = signon_begin;
  signon_host.disconnect = signon_disconnect;
  signon_host.open_download = signon_open_download;
  qa_quakeworld_signon_server *signon = qa_quakeworld_signon_server_create(signon_host, false);
  if (!signon) qa_range_error("out of memory");
  qa_cleanup ctx_guard, signon_guard;
  qa_cleanup_push(&ctx_guard, free, ctx);
  qa_cleanup_push(&signon_guard, free_signon_slot, signon);
  size_t count = 0;
  const void *const *states = host->baselines(host->ctx, player, &count);
  qa_cleanup_pop(&signon_guard);
  qa_cleanup_pop(&ctx_guard);
  binding_result result = {0};
  result.signon = signon;
  result.ctx = ctx;
  result.baseline_count = count;
  result.baselines = xmalloc(count * sizeof *result.baselines);
  result.baseline_states = xmalloc(count * sizeof *result.baseline_states);
  for (size_t i = 0; i < count; i++) {
    const qa_quakeworld_entity *entity = states[i];
    qa_qw_wire_entity_from_extended(&entity->state,
        (qa_js_to_int32(entity->quakeworld_flags) & QA_QW_U_SOLID) != 0, &result.baseline_states[i]);
    result.baselines[i].number = result.baseline_states[i].number;
    result.baselines[i].state = &result.baseline_states[i];
  }
  return result;
}
static bool conn_blocked(void *ctx, const qa_net_address *from) {
  qa_app_qw_server_network *n = ctx;
  const qa_app_qw_server_administration *admin = administration(n);
  return admin && admin->blocked ? admin->blocked(admin->ctx, from) : false;
}
static char *conn_status(void *ctx) {
  qa_app_qw_server_network *n = ctx;
  const qa_app_qw_server_administration *admin = administration(n);
  if (admin && admin->status) return admin->status(admin->ctx);
  return copy_text("\\hostname\\QuakeWorld\n");
}
static char *conn_log(void *ctx, double sequence) {
  qa_app_qw_server_network *n = ctx;
  const qa_app_qw_server_administration *admin = administration(n);
  return admin && admin->log ? admin->log(admin->ctx, sequence) : NULL;
}
static void conn_execute_admin(void *ctx, const char *command,
    void (*write)(void *, const char *, size_t), void *write_ctx) {
  qa_app_qw_server_network *n = ctx;
  const qa_app_qw_server_administration *admin = administration(n);
  if (!admin) qa_raise("Error", 0, 0, "QW administrator commands require a source command owner");
  admin->execute_admin(admin->ctx, command, write, write_ctx);
}
static void free_partial_peer(void *arg);
static void destroy_info_slot(void *arg) { qa_qw_info_destroy(arg); }
typedef struct admit_scope {
  qa_app_qw_server_network *n; const qa_qw_connect_request *request; qa_app_qw_player player; double now;
} admit_scope;
/* TS { address: () => addressKey(peer.remote, false), ping: () => peerPing(peer) }. */
static const char *network_address(void *ctx) { return ((peer_network *)ctx)->address; }
static double network_ping(void *ctx) {
  peer_network *network = ctx;
  return network->p != NULL ? peer_ping(network->p) : network->last_ping;
}
static void admit_body(void *ctx) {
  admit_scope *scope = ctx;
  qa_app_qw_server_network *n = scope->n;
  const qa_qw_connect_request *request = scope->request;
  binding_result binding = bind_player(n, scope->player, &n->host);
  qa_qw_info info;
  qa_qw_info_init(&info);
  qa_cleanup info_guard;
  qa_cleanup_push(&info_guard, destroy_info_slot, &info);
  qa_quakeworld_info_parse(request->userinfo ? request->userinfo : "", &info);
  const char *rate = qa_qw_info_get(&info, "rate");
  const char *msg = qa_qw_info_get(&info, "msg");
  peer *p = calloc(1, sizeof *p);
  if (!p) qa_range_error("out of memory");
  if (!qa_net_address_clone(&request->from, &p->remote)) qa_range_error("out of memory");
  p->player = scope->player;
  p->signon = binding.signon;
  p->signon_ctx = binding.ctx;
  p->baselines = binding.baselines;
  p->baseline_count = binding.baseline_count;
  p->baseline_states = binding.baseline_states;
  p->reliable = xmalloc(8 * sizeof *p->reliable);
  p->reliable_lengths = xmalloc(8 * sizeof *p->reliable_lengths);
  qa_cleanup peer_guard;
  qa_cleanup_push(&peer_guard, free_partial_peer, p);
  if (!qa_quakeworld_channel_init(&p->channel, false, request->qport, 1450,
          rate ? rate_of(rate) : 2500)) qa_range_error("out of memory");
  qa_cleanup_pop(&peer_guard);
  qa_quakeworld_command_replay_init(&p->replay);
  qa_movement_budget_init(&p->budget, 1800, 1.8);
  p->last_received = scope->now;
  p->message_level = qa_native_atoi(msg ? msg : "0");
  qa_source_chat_flood_init(&p->chat_flood, 4, 4, 10);
  qa_cleanup_pop(&info_guard);
  qa_qw_info_destroy(&info);
  if (n->peer_count == n->peer_cap) {
    n->peer_cap = n->peer_cap ? n->peer_cap * 2 : 16;
    n->peers = realloc(n->peers, n->peer_cap * sizeof *n->peers);
    if (!n->peers) qa_range_error("out of memory");
  }
  n->peers[n->peer_count++] = p;
  if (n->host.network_info != NULL) {
    if (n->network_count == n->network_cap) {
      n->network_cap = n->network_cap ? n->network_cap * 2 : 8;
      n->networks = realloc(n->networks, n->network_cap * sizeof *n->networks);
      if (!n->networks) qa_range_error("out of memory");
    }
    peer_network *network = xmalloc(sizeof *network);
    network->p = p; network->last_ping = 9999;
    network->address = qa_net_address_key(&p->remote, false);
    if (!network->address) { free(network); qa_range_error("out of memory"); }
    n->networks[n->network_count++] = network;
    p->network = network;
    qa_app_qw_network_info info = { network, network_address, network_ping };
    n->host.network_info(n->host.ctx, scope->player, info);
  }
}
/* A raise before the channel exists (invalid qport) releases what the peer holds. */
static void free_partial_peer(void *arg) {
  peer *p = arg;
  qa_net_address_destroy(&p->remote);
  qa_quakeworld_signon_server_free(p->signon);
  free(p->signon_ctx); free(p->baselines); free(p->baseline_states);
  free(p->reliable); free(p->reliable_lengths);
  free(p);
}
static qa_qw_host_result conn_connect(void *ctx, const qa_qw_connect_request *request, double now) {
  qa_app_qw_server_network *n = ctx;
  qa_qw_host_result result = {0, NULL};
  if (request->from.kind != QA_NET_IPV4 && request->from.kind != QA_NET_IPV6) {
    result.kind = 1;
    result.reason = "QW requires an IP endpoint";
    return result;
  }
  peer *existing = NULL;
  for (size_t i = 0; i < n->peer_count; i++)
    if (qa_net_same_address(&n->peers[i]->remote, &request->from, false)
        && n->peers[i]->channel.qport == request->qport) existing = n->peers[i];
  if (existing) {
    if (!existing->active) {
      result.kind = 2;
      return result;
    }
    remove_peer(n, existing, "Reconnecting");
  }
  if ((double)n->peer_count >= n->host.max_clients) {
    result.kind = 1;
    result.reason = "Server is full";
    return result;
  }
  size_t same = 0;
  for (size_t i = 0; i < n->peer_count; i++)
    if (qa_net_same_address(&n->peers[i]->remote, &request->from, false)) same++;
  if (same >= MAX_PEERS_PER_ADDRESS) {
    result.kind = 1;
    result.reason = "Too many connections from this address";
    return result;
  }
  qa_app_qw_admission admitted = n->host.admit(n->host.ctx, request);
  if (!admitted.accepted) {
    result.kind = 1;
    result.reason = admitted.reason;
    return result;
  }
  admit_scope scope = { n, request, admitted.player, now };
  qa_caught_error caught;
  if (!qa_try(admit_body, &scope, &caught)) {
    n->host.disconnect(n->host.ctx, admitted.player, "Signon failed");
    qa_rethrow(&caught);
  }
  return result;
}
static void refresh_connectionless(qa_app_qw_server_network *n) {
  const qa_app_qw_server_authentication *auth = authentication(n);
  const qa_app_qw_server_administration *admin = administration(n);
  n->connectionless.host.password = auth && auth->password ? auth->password : "";
  n->connectionless.host.spectator_password = auth && auth->spectator_password ? auth->spectator_password : "";
  n->connectionless.host.high_characters = auth ? auth->high_characters : false;
  const char *rcon = admin && admin->rcon_password ? admin->rcon_password(admin->ctx) : NULL;
  n->connectionless.host.rcon_password = rcon != NULL ? rcon : "";
}
qa_app_qw_server_network *qa_app_qw_server_network_create(qa_app_qw_server_network_options options) {
  validate_host(&options.host);
  qa_app_qw_server_network *n = calloc(1, sizeof *n);
  if (!n) return NULL;
  n->options = options;
  n->host = options.host;
  qa_qw_challenges_init(&n->challenges, options.random, options.random_ctx, 1024);
  n->connectionless.challenges = &n->challenges;
  n->connectionless.host.ctx = n;
  n->connectionless.host.blocked = conn_blocked;
  n->connectionless.host.connect = conn_connect;
  n->connectionless.host.status = conn_status;
  n->connectionless.host.log = conn_log;
  n->connectionless.host.execute_admin = conn_execute_admin;
  refresh_connectionless(n);
  qa_qw_master_heartbeat_init(&n->heartbeat_state);
  qa_connectionless_gate_init(&n->gate);
  qa_diagnostic_budget_init(&n->diagnostics, 20, 2);
  return n;
}
void qa_app_qw_server_network_destroy(qa_app_qw_server_network *network) {
  if (!network) return;
  for (size_t i = 0; i < network->peer_count; i++) peer_free(network->peers[i]);
  free(network->peers);
  reap_retired(network);
  free(network->retired);
  for (size_t i = 0; i < network->network_count; i++) {
    free(network->networks[i]->address);
    free(network->networks[i]);
  }
  free(network->networks);
  qa_connectionless_gate_destroy(&network->gate);
  qa_qw_challenges_destroy(&network->challenges);
  qa_qw_connectionless_server_destroy(&network->connectionless);
  free(network);
}
size_t qa_app_qw_server_network_client_pings(qa_app_qw_server_network *n,
    qa_client_id *clients, double *values, size_t capacity) {
  size_t count = 0;
  for (size_t i = 0; i < n->peer_count; i++) {
    if (!n->peers[i]->active) continue;
    if (count < capacity) {
      clients[count] = n->peers[i]->player.client;
      values[count] = peer_ping(n->peers[i]);
    }
    count++;
  }
  return count;
}
static void send_masters(qa_app_qw_server_network *n, const uint8_t *packet, size_t length) {
  size_t count = 0;
  const void *const *masters = n->host.masters ? n->host.masters(n->host.ctx, &count) : NULL;
  for (size_t i = 0; i < count; i++)
    transport_of(n)->send(transport_of(n)->ctx,
        (const qa_net_address *)masters[i], packet, length);
}
void qa_app_qw_server_network_heartbeat(qa_app_qw_server_network *n, double now) {
  if (n->ended) return;
  size_t length = 0;
  uint8_t *packet = qa_qw_master_heartbeat_next(&n->heartbeat_state, now,
      (double)n->peer_count, true, &length);
  if (!packet) return;
  send_masters(n, packet, length);
  free(packet);
}
bool qa_app_qw_server_network_address(qa_app_qw_server_network *n, qa_net_address *out) {
  return transport_of(n)->get_address(transport_of(n)->ctx, out) != 0;
}
qa_app_network_phase qa_app_qw_server_network_phase(const qa_app_qw_server_network *n) {
  return n->ended ? QA_APP_PHASE_CLOSED : QA_APP_PHASE_ACTIVE;
}
size_t qa_app_qw_server_network_clients(qa_app_qw_server_network *n,
    qa_app_qw_player *out, size_t capacity) {
  size_t count = 0;
  for (size_t i = 0; i < n->peer_count; i++) {
    if (count < capacity) out[count] = n->peers[i]->player;
    count++;
  }
  return count;
}
bool qa_app_qw_server_network_disconnect_client(qa_app_qw_server_network *n,
    qa_client_id client, const char *reason) {
  return disconnect_client(n, client, reason);
}
void qa_app_qw_server_network_change_world(qa_app_qw_server_network *n,
    qa_app_qw_server_host host) {
  validate_host(&host);
  qa_app_qw_player *players = xmalloc(n->peer_count * sizeof *players);
  binding_result *bindings = xmalloc(n->peer_count * sizeof *bindings);
  for (size_t i = 0; i < n->peer_count; i++) {
    players[i] = host.carried_player(host.ctx, n->peers[i]->player.client);
    bindings[i] = bind_player(n, players[i], &host);
  }
  n->host = host;
  refresh_connectionless(n);
  for (size_t i = 0; i < n->peer_count; i++) {
    peer *p = n->peers[i];
    qa_quakeworld_signon_server_free(p->signon);
    free(p->signon_ctx);
    free(p->baselines);
    free(p->baseline_states);
    p->player = players[i];
    p->signon = bindings[i].signon;
    p->signon_ctx = bindings[i].ctx;
    p->baselines = bindings[i].baselines;
    p->baseline_count = bindings[i].baseline_count;
    p->baseline_states = bindings[i].baseline_states;
    p->active = false;
    p->has_delta = false;
    p->choked = 0;
    for (size_t j = 0; j < p->frame_count; j++) free(p->frames[j].states);
    p->frame_count = 0;
    for (size_t j = 0; j < p->reliable_count; j++)
      free(p->reliable[(p->reliable_head + j) % 8]);
    p->reliable_head = 0;
    p->reliable_count = 0;
    qa_quakeworld_command_replay_init(&p->replay);
    qa_quakeworld_message message = {0};
    message.kind = "stufftext";
    message.text = "changing\nreconnect\n";
    const qa_quakeworld_message *messages[] = {&message};
    size_t length = 0;
    uint8_t *bytes = messages_bytes(messages, 1, &length);
    enqueue(p, bytes, length);
  }
  free(players);
  free(bindings);
}
typedef struct apply_ctx {
  qa_quakeworld_command_replay *replay_owner;
  qa_movement_budget *budget; double now;
  qa_qw_user_command *commands; size_t count, cap;
} apply_ctx;
static void apply_command(void *ctx, const qa_qw_user_command *command) {
  apply_ctx *apply = ctx;
  if (!qa_movement_budget_admit(apply->budget, command->milliseconds, apply->now)) return;
  if (apply->count == apply->cap) {
    apply->cap = apply->cap ? apply->cap * 2 : 8;
    apply->commands = realloc(apply->commands, apply->cap * sizeof *apply->commands);
    if (!apply->commands) qa_range_error("out of memory");
  }
  apply->commands[apply->count++] = *command;
}
typedef struct phase_ctx {
  qa_app_qw_server_network *n; peer *p;
  qa_app_qw_server_host host; qa_app_qw_player player;
  char *name; char **args; size_t arg_count; const char *text; double now;
} phase_ctx;
static bool phase_current(phase_ctx *phase) {
  return !phase->n->ended && phase->n->host.ctx == phase->host.ctx
      && peer_present(phase->n, phase->p)
      && client_equal(phase->p->player.client, phase->player.client);
}
static void phase_body(void *ctx) {
  phase_ctx *phase = ctx;
  qa_app_qw_server_network *n = phase->n;
  peer *p = phase->p;
  if (strcmp(phase->name, "rate") == 0) {
    if (phase->arg_count == 1) p->channel.bytes_per_second = rate_of(phase->args[0]);
    char line[96];
    char number[40];
    qa_net_js_number(p->channel.bytes_per_second, number, sizeof number);
    snprintf(line, sizeof line, "%s %s\n",
        phase->arg_count == 1 ? "Net rate set to" : "Current rate is", number);
    client_print(n, p, 2, line);
    return;
  }
  if (client_command(n, p, phase->name, phase->args, phase->arg_count, phase->text, phase->now))
    return;
  phase->host.command(phase->host.ctx, phase->player, phase->name,
      (const char *const *)phase->args, phase->arg_count);
  if (strcmp(phase->name, "setinfo") == 0) {
    const char *level = info_value(n, phase->player, "msg");
    const char *rate = info_value(n, phase->player, "rate");
    if (level && level[0] != '\0') p->message_level = qa_native_atoi(level);
    if (rate && rate[0] != '\0') p->channel.bytes_per_second = rate_of(rate);
  }
}
static void phase_action(void *ctx) {
  phase_ctx *phase = ctx;
  if (!phase_current(phase)) return;
  qa_caught_error caught;
  if (qa_try(phase_body, phase, &caught)) return;
  /* TS failed(error): print through the accepted host, then drop the peer if still current. */
  phase->host.print(phase->host.ctx, caught.message);
  if (phase_current(phase)) disconnect_client(phase->n, phase->player.client, caught.message);
}
static void phase_emit(void *ctx, qa_app_qw_player recipient, const void *message) {
  phase_ctx *phase = ctx;
  if (phase->n->ended || phase->n->host.ctx != phase->host.ctx) return;
  for (size_t i = 0; i < phase->n->peer_count; i++) {
    peer *target = phase->n->peers[i];
    if (client_equal(target->player.client, recipient.client)
        && qa_same_actor(target->player.actor, recipient.actor)) {
      deliver(phase->n, target, message);
      return;
    }
  }
}
static void send_peer(qa_app_qw_server_network *n, peer *p, const qa_app_qw_frame *frame, double now);
/* Everything one datagram owns; released after qa_try returns on either path. */
typedef struct packet_scope {
  qa_app_qw_server_network *n; qa_receive_event *packet; double now;
  peer *owner;
  qa_quakeworld_delivery delivery; bool has_delivery;
  qa_quakeworld_client_message *messages; size_t message_count;
  char **args; size_t arg_count;
  apply_ctx apply;
  uint8_t **replies; size_t *reply_lengths; size_t reply_count;
  qa_qw_command_result result;
} packet_scope;
static void free_args(packet_scope *scope) {
  for (size_t j = 0; j < scope->arg_count; j++) free(scope->args[j]);
  free(scope->args);
  scope->args = NULL; scope->arg_count = 0;
}
static void packet_scope_release(packet_scope *scope) {
  for (size_t i = 0; i < scope->reply_count; i++) free(scope->replies[i]);
  free(scope->replies); free(scope->reply_lengths);
  scope->replies = NULL; scope->reply_lengths = NULL; scope->reply_count = 0;
  for (size_t i = 0; i < scope->message_count; i++) qa_quakeworld_client_message_destroy(&scope->messages[i]);
  free(scope->messages); scope->messages = NULL; scope->message_count = 0;
  free_args(scope);
  free(scope->apply.commands); scope->apply.commands = NULL; scope->apply.count = scope->apply.cap = 0;
  qa_qw_command_result_destroy(&scope->result);
  if (scope->has_delivery) free(scope->delivery.payload);
  scope->has_delivery = false;
}
typedef struct prepare_scope {
  const qa_app_qw_server_host *host; qa_app_qw_player player; const char *path; qa_download_source *prepared;
} prepare_scope;
static void prepare_body(void *ctx) {
  prepare_scope *prepare = ctx;
  prepare->prepared = (qa_download_source *)prepare->host->prepare_download(prepare->host->ctx, prepare->player, prepare->path);
}
static void string_command(packet_scope *scope, peer *owner, const qa_quakeworld_client_message *message) {
  qa_app_qw_server_network *n = scope->n;
  scope->args = qa_quakeworld_command_arguments(message->text, &scope->arg_count);
  if (!scope->args) qa_range_error("out of memory");
  const char *name = scope->arg_count ? scope->args[0] : NULL;
  if (name && (strcmp(name, "drop") == 0 || strcmp(name, "disconnect") == 0)) {
    disconnect_client(n, owner->player.client, "Client disconnected");
    return;
  }
  if (name && strcmp(name, "download") == 0 && n->host.prepare_download) {
    /* TS runs the open as a detached task, one outstanding per client; C opens synchronously,
     * so the request finishes before the next datagram and `preparing` only brackets it. */
    if (owner->preparing) return;
    owner->preparing = true;
    qa_app_qw_server_host accepted_host = n->host;
    qa_quakeworld_signon_server *accepted_signon = owner->signon;
    qa_app_qw_player accepted_player = owner->player;
    prepare_scope prepare = { &accepted_host, accepted_player, scope->arg_count > 1 ? scope->args[1] : "", NULL };
    qa_caught_error caught;
    if (!qa_try(prepare_body, &prepare, &caught)) {
      prepare.prepared = NULL;
      char *printable = qa_printable_peer_text(caught.message, 256);
      if (printable) accepted_host.print(accepted_host.ctx, printable);
      free(printable);
    }
    owner->preparing = false;
    if (n->ended || !peer_present(n, owner) || n->host.ctx != accepted_host.ctx
        || owner->signon != accepted_signon
        || !client_equal(owner->player.client, accepted_player.client)) {
      if (prepare.prepared && prepare.prepared->close) prepare.prepared->close(prepare.prepared->ctx);
      return;
    }
    /* A throw here is an unhandled rejection of the detached TS task; C lets it reach the
     * per-datagram catch, which drops this peer. */
    scope->result = qa_quakeworld_signon_server_command(owner->signon, message->text, prepare.prepared, true);
    if (scope->result.handled)
      for (size_t j = 0; j < scope->result.count; j++) {
        uint8_t *bytes = scope->result.messages[j];
        scope->result.messages[j] = NULL;
        enqueue(owner, bytes, scope->result.lens[j]);
      }
    return;
  }
  qa_download_source *prepared = NULL;
  bool has_prepared = false;
  scope->result = qa_quakeworld_signon_server_command(owner->signon, message->text, prepared, has_prepared);
  if (scope->result.handled) {
    for (size_t j = 0; j < scope->result.count; j++) {
      uint8_t *bytes = scope->result.messages[j];
      scope->result.messages[j] = NULL; /* enqueue owns it from here, raise or not */
      enqueue(owner, bytes, scope->result.lens[j]);
    }
  } else if (name) {
    phase_ctx phase = {0};
    phase.n = n;
    phase.p = owner;
    phase.host = n->host;
    phase.player = owner->player;
    phase.name = scope->args[0];
    phase.args = scope->args + 1;
    phase.arg_count = scope->arg_count - 1;
    phase.text = message->text;
    phase.now = scope->now;
    n->host.command_phase(n->host.ctx, owner->player, phase_action, &phase, phase_emit, &phase);
  }
}
static void packet_body(void *ctx) {
  packet_scope *scope = ctx;
  qa_app_qw_server_network *n = scope->n;
  qa_receive_event *packet = scope->packet;
  double now = scope->now;
  if (packet->length >= 4 && packet->payload[0] == 255 && packet->payload[1] == 255
      && packet->payload[2] == 255 && packet->payload[3] == 255) {
    if (!qa_connectionless_gate_allow(&n->gate, &packet->from, now)) return;
    scope->replies = qa_qw_connectionless_receive(&n->connectionless,
        packet->payload, packet->length, &packet->from, now, &scope->reply_lengths, &scope->reply_count);
    for (size_t i = 0; i < scope->reply_count; i++)
      transport_of(n)->send(transport_of(n)->ctx, &packet->from, scope->replies[i], scope->reply_lengths[i]);
    return;
  }
  if (packet->length < 10) return;
  unsigned qport = (unsigned)packet->payload[8] | ((unsigned)packet->payload[9] << 8);
  peer *owner = NULL;
  for (size_t i = 0; i < n->peer_count && !owner; i++)
    if (n->peers[i]->channel.qport == (double)qport
        && qa_net_same_address(&n->peers[i]->remote, &packet->from, false))
      owner = n->peers[i];
  if (!owner) return;
  scope->owner = owner;
  uint32_t first_word = (uint32_t)packet->payload[0] | ((uint32_t)packet->payload[1] << 8)
      | ((uint32_t)packet->payload[2] << 16) | ((uint32_t)packet->payload[3] << 24);
  bool can_reply = (double)(first_word % 0x80000000u)
      >= qa_quakeworld_channel_outgoing_sequence(&owner->channel);
  const qa_app_qw_server_administration *admin = administration(n);
  if (admin && admin->blocked && admin->blocked(admin->ctx, &packet->from)) return;
  int received = qa_quakeworld_channel_receive(&owner->channel, packet->payload,
      packet->length, now, &scope->delivery);
  if (!received) return;
  scope->has_delivery = true;
  qa_quakeworld_delivery *delivery = &scope->delivery;
  ping_frame *acknowledged = &owner->ping_frames[(qa_js_to_int32(delivery->acknowledged) & 63)];
  if (acknowledged->used && acknowledged->sequence == delivery->acknowledged)
    acknowledged->ping = now - acknowledged->sent;
  double next_sequence = qa_quakeworld_channel_outgoing_sequence(&owner->channel);
  ping_frame *slot = &owner->ping_frames[(qa_js_to_int32(next_sequence) & 63)];
  slot->used = true;
  slot->sequence = next_sequence;
  slot->sent = now;
  slot->ping = -1;
  qa_net_address remote;
  if (!qa_net_address_clone(&packet->from, &remote)) qa_range_error("out of memory");
  qa_net_address_destroy(&owner->remote);
  owner->remote = remote;
  owner->last_received = now;
  owner->reply = can_reply;
  owner->has_delta = false;
  bool moved = false;
  scope->messages = qa_decode_quakeworld_client(delivery->payload, delivery->length, qw_profile(),
      delivery->sequence, &scope->message_count);
  for (size_t i = 0; i < scope->message_count; i++) {
    if (!peer_present(n, owner)) break;
    const qa_quakeworld_client_message *message = &scope->messages[i];
    if (strcmp(message->kind, "delta") == 0) {
      owner->delta = message->sequence;
      owner->has_delta = true;
    } else if (strcmp(message->kind, "move") == 0 && owner->active) {
      /* sv_user.c ignores a second clc_move in one packet. */
      if (moved) continue;
      moved = true;
      owner->loss_percent = message->bundle.loss_percent;
      scope->apply.budget = &owner->budget;
      scope->apply.now = now;
      scope->apply.count = 0;
      qa_quakeworld_command_replay_run(&owner->replay, &message->bundle,
          delivery->dropped, host_paused(n), apply_command, &scope->apply);
      if (scope->apply.count != 0)
        n->host.command_group(n->host.ctx, owner->player, scope->apply.commands,
            scope->apply.count, delivery->sequence);
    } else if (strcmp(message->kind, "string-command") == 0) {
      string_command(scope, owner, message);
      free_args(scope);
      qa_qw_command_result_destroy(&scope->result);
    }
  }
}
typedef struct send_scope { qa_app_qw_server_network *n; peer *p; const qa_app_qw_frame *frame; double now; } send_scope;
static void send_body(void *ctx) {
  send_scope *scope = ctx;
  send_peer(scope->n, scope->p, scope->frame, scope->now);
}
const void *const *qa_app_qw_server_network_poll(qa_app_qw_server_network *n,
    double now, size_t *out_count) {
  *out_count = 0;
  if (!n->ended) {
    size_t master_count = 0;
    if (n->host.masters) n->host.masters(n->host.ctx, &master_count);
    if (master_count > 0) {
      size_t length = 0;
      uint8_t *packet = qa_qw_master_heartbeat_next(&n->heartbeat_state, now,
          (double)n->peer_count, false, &length);
      if (packet) {
        send_masters(n, packet, length);
        free(packet);
      }
    }
  }
  if (n->ended) return NULL;
  refresh_connectionless(n);
  for (;;) {
    qa_receive_event packet;
    if (!transport_of(n)->poll(transport_of(n)->ctx, &packet)) break;
    if (packet.kind != QA_RECV_PACKET) {
      if (packet.kind == QA_RECV_ERROR && packet.error) n->host.print(n->host.ctx, packet.error);
      qa_receive_event_destroy(&packet);
      continue;
    }
    packet_scope scope;
    memset(&scope, 0, sizeof scope);
    scope.n = n;
    scope.packet = &packet;
    scope.now = now;
    qa_caught_error caught;
    bool ok = qa_try(packet_body, &scope, &caught);
    packet_scope_release(&scope);
    qa_receive_event_destroy(&packet);
    if (!ok) {
      /* A malformed datagram costs its peer, never the server. */
      if (qa_diagnostic_budget_allow(&n->diagnostics)) {
        char *printable = qa_printable_peer_text(caught.message, 256);
        if (printable) n->host.print(n->host.ctx, printable);
        free(printable);
      }
      if (scope.owner) disconnect_client(n, scope.owner->player.client, caught.message);
    }
    reap_retired(n);
  }
  double timeout = n->options.has_timeout_milliseconds ? n->options.timeout_milliseconds : 65000;
  size_t snapshot_count = n->peer_count;
  peer **snapshot = xmalloc(snapshot_count * sizeof *snapshot);
  if (snapshot_count > 0) memcpy(snapshot, n->peers, snapshot_count * sizeof *snapshot);
  for (size_t i = 0; i < snapshot_count; i++) {
    peer *p = snapshot[i];
    if (now - p->last_received > timeout) disconnect_client(n, p->player.client, "Client timed out");
    else if (!p->active) {
      send_scope scope = { n, p, NULL, now };
      qa_caught_error caught;
      if (!qa_try(send_body, &scope, &caught)) disconnect_client(n, p->player.client, caught.message);
    }
  }
  free(snapshot);
  reap_retired(n);
  return NULL;
}
typedef struct frame_scope {
  qa_app_qw_server_network *n; peer *p; const qa_app_qw_frame *frame;
  qa_size_buf *buffer; double sequence;
  qa_qw_entity_state *states; size_t count;
} frame_scope;
static void frame_body(void *ctx) {
  frame_scope *scope = ctx;
  peer *p = scope->p;
  const qa_app_qw_frame *frame = scope->frame;
  qa_size_buf *buffer = scope->buffer;
  qa_quakeworld_profile profile = qw_profile();
  if (p->choked != 0) {
    qa_quakeworld_message choke = {0};
    choke.kind = "choke-count";
    choke.count = p->choked;
    qa_write_quakeworld_message(buffer, profile, &choke);
    p->choked = 0;
  }
  for (size_t i = 0; i < frame->message_count; i++) {
    const qa_quakeworld_message *message = frame->messages[i];
    if (message->kind && strcmp(message->kind, "print") == 0 && message->level < p->message_level)
      continue;
    qa_write_quakeworld_message(buffer, profile, message);
  }
  scope->states = xmalloc(frame->entity_count * sizeof *scope->states);
  scope->count = frame->entity_count;
  for (size_t i = 0; i < frame->entity_count; i++) {
    const qa_quakeworld_entity *entity = frame->entities[i];
    qa_qw_wire_entity_from_extended(&entity->state,
        (qa_js_to_int32(entity->quakeworld_flags) & QA_QW_U_SOLID) != 0, &scope->states[i]);
  }
  qa_qw_previous_frame previous = {0};
  const qa_qw_previous_frame *previous_ptr = NULL;
  if (p->has_delta) {
    /* [...frames.values()].find: the first match in insertion order. */
    for (size_t i = 0; i < p->frame_count && !previous_ptr; i++)
      if ((qa_js_to_int32(p->frames[i].sequence) & 255) == (int64_t)p->delta
          && scope->sequence - p->frames[i].sequence < 64) {
        previous.sequence = p->frames[i].sequence;
        previous.states = p->frames[i].states;
        previous.count = p->frames[i].count;
        previous_ptr = &previous;
      }
  }
  qa_write_quakeworld_entities(buffer, profile, scope->states, frame->entity_count,
      p->baselines, p->baseline_count, previous_ptr);
  if (buffer->overflowed) qa_raise("Error", 0, 0, "QW frame datagram overflow");
}
static void send_peer(qa_app_qw_server_network *n, peer *p, const qa_app_qw_frame *frame, double now) {
  if (!p->reply) return;
  p->reply = false;
  if (!host_paused(n) && !qa_quakeworld_channel_can_packet(&p->channel, now)) {
    p->choked++;
    return;
  }
  if (!qa_quakeworld_channel_has_reliable(&p->channel) && p->reliable_count > 0) {
    size_t index = p->reliable_head;
    uint8_t *bytes = p->reliable[index];
    size_t length = p->reliable_lengths[index];
    p->reliable_head = (p->reliable_head + 1) % 8;
    p->reliable_count--;
    qa_cleanup bytes_guard;
    qa_cleanup_push(&bytes_guard, free, bytes);
    qa_quakeworld_channel_queue_reliable(&p->channel, bytes, length);
    qa_cleanup_pop(&bytes_guard);
    p->reliable_length = (double)length;
    free(bytes);
  }
  qa_size_buf buffer;
  if (!qa_size_buf_init(&buffer, 1450, true)) qa_range_error("out of memory");
  qa_cleanup buffer_guard;
  qa_cleanup_push(&buffer_guard, destroy_buf_slot, &buffer);
  double sequence = qa_quakeworld_channel_outgoing_sequence(&p->channel);
  frame_scope sent = { n, p, frame, &buffer, sequence, NULL, 0 };
  bool has_sent = false;
  if (frame) {
    qa_caught_error caught;
    if (qa_try(frame_body, &sent, &caught)) has_sent = true;
    else {
      n->host.print(n->host.ctx, caught.message);
      qa_size_buf_clear(&buffer);
      free(sent.states);
      sent.states = NULL;
    }
  }
  qa_cleanup states_guard;
  qa_cleanup_push(&states_guard, free, sent.states);
  size_t packet_length = 0;
  uint8_t *packet = qa_quakeworld_channel_transmit(&p->channel, buffer.data,
      (size_t)buffer.cursize, now, host_paused(n), &packet_length);
  if (!packet) qa_range_error("out of memory");
  qa_cleanup packet_guard;
  qa_cleanup_push(&packet_guard, free, packet);
  transport_of(n)->send(transport_of(n)->ctx, &p->remote, packet, packet_length);
  qa_cleanup_pop(&packet_guard);
  qa_cleanup_pop(&states_guard);
  double reliable_bytes = packet_length > 3 && (packet[3] & 128) != 0 ? p->reliable_length : 0;
  if (has_sent && packet_length == (size_t)(8 + (int64_t)reliable_bytes + buffer.cursize)) {
    /* frames.set(sequence, sent): an existing key keeps its position. */
    frame_store *existing = NULL;
    for (size_t i = 0; i < p->frame_count; i++)
      if (p->frames[i].sequence == sequence) existing = &p->frames[i];
    if (existing) {
      free(existing->states);
      existing->states = sent.states;
      existing->count = sent.count;
    } else {
      if (p->frame_count == p->frame_cap) {
        p->frame_cap = p->frame_cap ? p->frame_cap * 2 : 16;
        p->frames = realloc(p->frames, p->frame_cap * sizeof *p->frames);
        if (!p->frames) qa_range_error("out of memory");
      }
      p->frames[p->frame_count].sequence = sequence;
      p->frames[p->frame_count].states = sent.states;
      p->frames[p->frame_count].count = sent.count;
      p->frame_count++;
    }
    size_t kept = 0;
    for (size_t i = 0; i < p->frame_count; i++) {
      if (sequence - p->frames[i].sequence >= 64) free(p->frames[i].states);
      else p->frames[kept++] = p->frames[i];
    }
    p->frame_count = kept;
  } else free(sent.states);
  free(packet);
  qa_cleanup_pop(&buffer_guard);
  qa_size_buf_destroy(&buffer);
}
void qa_app_qw_server_network_submit(qa_app_qw_server_network *n,
    const void *const *commands, size_t count, double now) {
  /* TS submit(_commands, _now) is empty: QW commands arrive through poll. */
  (void)n; (void)commands; (void)count; (void)now;
}
typedef struct publish_scope { qa_app_qw_server_network *n; peer *p; const void *output; double now; } publish_scope;
static void publish_body(void *ctx) {
  publish_scope *scope = ctx;
  qa_app_qw_server_network *n = scope->n;
  peer *p = scope->p;
  qa_app_qw_frame frame = n->host.frame(n->host.ctx, p->player, scope->output);
  for (size_t j = 0; j < frame.reliable_count; j++) {
    const qa_quakeworld_message *message = frame.reliable[j];
    if (message->kind && strcmp(message->kind, "print") == 0 && message->level < p->message_level)
      continue;
    const qa_quakeworld_message *messages[] = {message};
    size_t length = 0;
    uint8_t *bytes = messages_bytes(messages, 1, &length);
    enqueue(p, bytes, length);
  }
  send_peer(n, p, &frame, scope->now);
}
void qa_app_qw_server_network_publish(qa_app_qw_server_network *n,
    const void *output, const void *const *events, size_t count, double now) {
  if (n->ended) return;
  n->host.observe(n->host.ctx, output, events, count);
  size_t snapshot_count = n->peer_count;
  peer **snapshot = xmalloc(snapshot_count * sizeof *snapshot);
  if (snapshot_count > 0) memcpy(snapshot, n->peers, snapshot_count * sizeof *snapshot);
  for (size_t i = 0; i < snapshot_count; i++) {
    peer *p = snapshot[i];
    if (!p->active) continue;
    publish_scope scope = { n, p, output, now };
    qa_caught_error caught;
    if (!qa_try(publish_body, &scope, &caught)) disconnect_client(n, p->player.client, caught.message);
  }
  free(snapshot);
  reap_retired(n);
}
void qa_app_qw_server_network_close(qa_app_qw_server_network *n) {
  if (n->ended) return;
  size_t length = 0;
  uint8_t *packet = qa_quakeworld_shutdown(&length);
  if (!packet) qa_range_error("out of memory");
  send_masters(n, packet, length);
  free(packet);
  while (n->peer_count > 0)
    disconnect_client(n, n->peers[0]->player.client, "Server shutdown");
  n->ended = true;
  transport_of(n)->close(transport_of(n)->ctx);
  reap_retired(n);
}
static qa_app_network_phase net_phase(void *ctx) { return qa_app_qw_server_network_phase(ctx); }
/* TS readonly wire = { kind: "source", protocol: { kind: "q1-quakeworld", version: 28 } } */
static qa_wire_selection net_wire(void *ctx) {
  (void)ctx;
  qa_wire_selection wire;
  memset(&wire, 0, sizeof wire);
  wire.protocol.kind = "q1-quakeworld";
  wire.protocol.version = 28;
  return wire;
}
static void net_heartbeat(void *ctx, double now) { qa_app_qw_server_network_heartbeat(ctx, now); }
static const void *const *net_poll(void *ctx, double now, size_t *out_count) {
  return qa_app_qw_server_network_poll(ctx, now, out_count);
}
static void net_submit(void *ctx, const void *const *commands, size_t count, double now) {
  qa_app_qw_server_network_submit(ctx, commands, count, now);
}
static void net_publish(void *ctx, const void *output, const void *const *events,
    size_t count, double now) {
  qa_app_qw_server_network_publish(ctx, output, events, count, now);
}
static void net_close(void *ctx) { qa_app_qw_server_network_close(ctx); }
qa_app_application_network qa_app_qw_server_network_as_network(qa_app_qw_server_network *network) {
  qa_app_application_network result = {0};
  result.ctx = network;
  result.role = QA_APP_ROLE_SERVER;
  result.phase = net_phase;
  result.wire = net_wire;
  result.heartbeat = net_heartbeat;
  result.poll = net_poll;
  result.submit = net_submit;
  result.publish = net_publish;
  result.close = net_close;
  return result;
}
