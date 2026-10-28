/* Port of src/app/bootstrap/network/qw-server.ts.
 * GPL-2.0-or-later. */
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
typedef struct ping_frame { double sequence, sent, ping; } ping_frame;
typedef struct peer peer;
struct peer {
  qa_net_address remote;
  qa_app_qw_player player;
  qa_quakeworld_channel channel;
  qa_quakeworld_signon_server *signon;
  void *signon_ctx;
  qa_quakeworld_command_replay replay;
  qa_qw_baseline_map_entry *baselines; size_t baseline_count;
  qa_qw_entity_state *baseline_states;
  uint8_t **reliable; size_t *reliable_lengths; size_t reliable_head, reliable_count;
  double reliable_length;
  frame_store *frames; size_t frame_count, frame_cap;
  bool active;
  bool reply;
  bool has_delta; double delta;
  double choked;
  double last_received;
  double message_level;
  double loss_percent;
  ping_frame ping_frames[64];
  qa_source_chat_flood chat_flood;
};
struct qa_app_qw_server_network {
  qa_app_qw_server_network_options options;
  qa_app_qw_server_host host;
  peer **peers; size_t peer_count, peer_cap;
  qa_qw_connectionless_server connectionless;
  qa_qw_challenges challenges;
  qa_qw_master_heartbeat heartbeat_state;
  bool ended;
};
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
      qa_range_error("%s", message);
    }
  }
  if (host->max_clients != trunc(host->max_clients) || host->max_clients < 1 || host->max_clients > 32)
    qa_range_error("Native QW admits at most 32 players");
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
static uint8_t *messages_bytes(const qa_quakeworld_message *const *messages, size_t count,
    size_t *out_length) {
  qa_size_buf buffer;
  if (!qa_size_buf_init(&buffer, 1450, false)) qa_range_error("out of memory");
  qa_quakeworld_profile profile = qw_profile();
  for (size_t i = 0; i < count; i++) qa_write_quakeworld_message(&buffer, profile, messages[i]);
  uint8_t *out = xmalloc((size_t)buffer.cursize);
  memcpy(out, buffer.data, (size_t)buffer.cursize);
  *out_length = (size_t)buffer.cursize;
  qa_size_buf_destroy(&buffer);
  return out;
}
static void enqueue(peer *p, uint8_t *bytes, size_t length) {
  if (length == 0) { free(bytes); return; }
  if (length > 1450) qa_range_error("QW reliable block exceeds native message size");
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
  if (p->reliable_count >= 5) qa_range_error("QW reliable back buffers overflow");
  size_t index = (p->reliable_head + p->reliable_count) % 8;
  p->reliable[index] = bytes;
  p->reliable_lengths[index] = length;
  p->reliable_count++;
}
static void peer_free(peer *p) {
  qa_net_address_destroy(&p->remote);
  qa_quakeworld_channel_destroy(&p->channel);
  if (p->signon) {
    qa_quakeworld_signon_server_destroy(p->signon);
    free(p->signon);
  }
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
  qa_quakeworld_signon_server_close(p->signon);
  qa_app_qw_player player = p->player;
  peer_free(p);
  n->host.disconnect(n->host.ctx, player, reason);
}
static bool disconnect_client(qa_app_qw_server_network *n, qa_client_id client, const char *reason) {
  peer *p = NULL;
  for (size_t i = 0; i < n->peer_count; i++)
    if (client_equal(n->peers[i]->player.client, client)) p = n->peers[i];
  if (!p) return false;
  qa_quakeworld_message message = {0};
  message.kind = "disconnect";
  const qa_quakeworld_message *messages[] = {&message};
  size_t length = 0;
  uint8_t *bytes = messages_bytes(messages, 1, &length);
  size_t packet_length = 0;
  uint8_t *packet = qa_quakeworld_channel_transmit(&p->channel, bytes, length,
      p->last_received, true, &packet_length);
  free(bytes);
  if (!packet) qa_range_error("out of memory");
  transport_of(n)->send(transport_of(n)->ctx, &p->remote, packet, packet_length);
  free(packet);
  remove_peer(n, p, reason);
  return true;
}
static void deliver(qa_app_qw_server_network *n, peer *p, const qa_quakeworld_message *message) {
  if (!peer_present(n, p)) return;
  if (message->kind && strcmp(message->kind, "print") == 0 && message->level < p->message_level)
    return;
  /* TS catches enqueue failure into a disconnect; C enqueue aborts
   * with the same message. */
  const qa_quakeworld_message *messages[] = {message};
  size_t length = 0;
  uint8_t *bytes = messages_bytes(messages, 1, &length);
  enqueue(p, bytes, length);
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
static bool client_command(qa_app_qw_server_network *n, peer *p, const char *name,
    char **args, size_t arg_count, const char *text, double now) {
  if (strcmp(name, "msg") == 0) {
    if (arg_count == 1) p->message_level = qa_native_atoi(args[0]);
    char line[96];
    snprintf(line, sizeof line, "%s %.0f\n",
        arg_count == 1 ? "Msg level set to" : "Current msg level is", p->message_level);
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
    size_t cap = 256 + n->peer_count * 96;
    char *line = xmalloc(cap);
    size_t used = 0;
    if (strcmp(name, "ping") == 0)
      used = (size_t)snprintf(line, cap, "Client ping times:\n");
    else
      used = (size_t)snprintf(line, cap, "players: %zu active (%.0f max)\n",
          n->peer_count, n->host.max_clients);
    for (size_t i = 0; i < n->peer_count; i++) {
      peer *other = n->peers[i];
      if (strcmp(name, "ping") == 0 && !other->active) continue;
      const char *player_name = info_value(n, other->player, "name");
      if (!player_name) player_name = "unnamed";
      if (strcmp(name, "ping") == 0)
        used += (size_t)snprintf(line + used, cap - used, "%.0f %s\n", peer_ping(other), player_name);
      else
        used += (size_t)snprintf(line + used, cap - used, "#%.0f %s\n",
            other->player.slot + 1, player_name);
    }
    client_print(n, p, 2, line);
    free(line);
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
  const char *flood = qa_source_chat_flood_check(&p->chat_flood, now / 1000, n->host.paused, &seconds);
  if (strcmp(flood, "allowed") != 0) {
    char line[128];
    if (strcmp(flood, "locked") == 0)
      snprintf(line, sizeof line, "You can't talk for %.17g more seconds\n", seconds);
    else
      snprintf(line, sizeof line, "FloodProt: You can't talk for %.17g seconds.\n", seconds);
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
  const char *words = text + parsed.index;
  while (*words == ' ' || *words == '\t') words++;
  char *words_copy = copy_text(words);
  size_t words_length = strlen(words_copy);
  if (words_length >= 2 && words_copy[0] == '"' && words_copy[words_length - 1] == '"') {
    memmove(words_copy, words_copy + 1, words_length - 2);
    words_copy[words_length - 2] = '\0';
  }
  const char *sender_value = info_value(n, p->player, "name");
  char sender[32] = "";
  snprintf(sender, sizeof sender, "%s", sender_value ? sender_value : "");
  size_t total = strlen(sender) + strlen(words_copy) + 8;
  char *message = xmalloc(total);
  if (team) snprintf(message, total, "(%s): %s\n", sender, words_copy);
  else snprintf(message, total, "%s: %s\n", sender, words_copy);
  free(words_copy);
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
    qa_range_error("Source signon does not describe an admitted native QW player");
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
  /* Landed signon server is caller-storage with unpublished size;
   * the landed struct is a host vtable plus three fields. */
  qa_quakeworld_signon_server *signon = xmalloc(512);
  memset(signon, 0, 512);
  qa_quakeworld_signon_server_init(signon, signon_host, false);
  size_t count = 0;
  const void *const *states = host->baselines(host->ctx, player, &count);
  binding_result result = {0};
  result.signon = signon;
  result.ctx = ctx;
  result.baseline_count = count;
  result.baselines = xmalloc(count * sizeof *result.baselines);
  result.baseline_states = xmalloc(count * sizeof *result.baseline_states);
  for (size_t i = 0; i < count; i++) {
    const qa_quakeworld_entity *entity = states[i];
    qa_qw_wire_entity_from_extended(&entity->state,
        ((int64_t)entity->quakeworld_flags & QA_QW_U_SOLID) != 0, &result.baseline_states[i]);
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
  if (!admin) qa_range_error("QW administrator commands require a source command owner");
  admin->execute_admin(admin->ctx, command, write, write_ctx);
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
  qa_app_qw_admission admitted = n->host.admit(n->host.ctx, request);
  if (!admitted.accepted) {
    result.kind = 1;
    result.reason = admitted.reason;
    return result;
  }
  /* TS catches bind failures to disconnect; C bind aborts. */
  binding_result binding = bind_player(n, admitted.player, &n->host);
  qa_qw_info info;
  qa_qw_info_init(&info);
  qa_quakeworld_info_parse(request->userinfo ? request->userinfo : "", &info);
  const char *rate = qa_qw_info_get(&info, "rate");
  const char *msg = qa_qw_info_get(&info, "msg");
  peer *p = calloc(1, sizeof *p);
  if (!p) qa_range_error("out of memory");
  if (!qa_net_address_clone(&request->from, &p->remote)) qa_range_error("out of memory");
  p->player = admitted.player;
  if (!qa_quakeworld_channel_init(&p->channel, false, request->qport, 1450,
          rate ? rate_of(rate) : 2500)) qa_range_error("out of memory");
  p->signon = binding.signon;
  p->signon_ctx = binding.ctx;
  p->baselines = binding.baselines;
  p->baseline_count = binding.baseline_count;
  p->baseline_states = binding.baseline_states;
  qa_quakeworld_command_replay_init(&p->replay);
  p->reliable = xmalloc(8 * sizeof *p->reliable);
  p->reliable_lengths = xmalloc(8 * sizeof *p->reliable_lengths);
  p->last_received = now;
  p->message_level = qa_native_atoi(msg ? msg : "0");
  for (size_t i = 0; i < 64; i++) p->ping_frames[i].ping = -1;
  qa_source_chat_flood_init(&p->chat_flood, 4, 4, 10);
  qa_qw_info_destroy(&info);
  if (n->peer_count == n->peer_cap) {
    n->peer_cap = n->peer_cap ? n->peer_cap * 2 : 16;
    n->peers = realloc(n->peers, n->peer_cap * sizeof *n->peers);
    if (!n->peers) qa_range_error("out of memory");
  }
  n->peers[n->peer_count++] = p;
  return result;
}
static void refresh_connectionless(qa_app_qw_server_network *n) {
  const qa_app_qw_server_authentication *auth = authentication(n);
  const qa_app_qw_server_administration *admin = administration(n);
  n->connectionless.host.password = auth && auth->password ? auth->password : "";
  n->connectionless.host.spectator_password = auth && auth->spectator_password ? auth->spectator_password : "";
  n->connectionless.host.high_characters = auth ? auth->high_characters : false;
  n->connectionless.host.rcon_password = admin && admin->rcon_password ? admin->rcon_password : "";
}
qa_app_qw_server_network *qa_app_qw_server_network_create(qa_app_qw_server_network_options options) {
  qa_app_qw_server_network *n = calloc(1, sizeof *n);
  if (!n) return NULL;
  n->options = options;
  validate_host(&options.host);
  n->host = options.host;
  qa_qw_challenges_init(&n->challenges, options.random, options.random_ctx, 0);
  n->connectionless.challenges = &n->challenges;
  n->connectionless.host.ctx = n;
  n->connectionless.host.blocked = conn_blocked;
  n->connectionless.host.connect = conn_connect;
  n->connectionless.host.status = conn_status;
  n->connectionless.host.log = conn_log;
  n->connectionless.host.execute_admin = conn_execute_admin;
  refresh_connectionless(n);
  qa_qw_master_heartbeat_init(&n->heartbeat_state);
  return n;
}
void qa_app_qw_server_network_destroy(qa_app_qw_server_network *network) {
  if (!network) return;
  for (size_t i = 0; i < network->peer_count; i++) peer_free(network->peers[i]);
  free(network->peers);
  qa_qw_challenges_destroy(&network->challenges);
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
    qa_quakeworld_signon_server_close(p->signon);
    qa_quakeworld_signon_server_destroy(p->signon);
    free(p->signon);
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
  qa_qw_user_command *commands; size_t count, cap;
} apply_ctx;
static void apply_command(void *ctx, const qa_qw_user_command *command) {
  apply_ctx *apply = ctx;
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
static void phase_action(void *ctx) {
  phase_ctx *phase = ctx;
  if (!phase_current(phase)) return;
  qa_app_qw_server_network *n = phase->n;
  peer *p = phase->p;
  if (strcmp(phase->name, "rate") == 0) {
    if (phase->arg_count == 1) p->channel.bytes_per_second = rate_of(phase->args[0]);
    char line[96];
    snprintf(line, sizeof line, "%s %.17g\n",
        phase->arg_count == 1 ? "Net rate set to" : "Current rate is", p->channel.bytes_per_second);
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
static void phase_emit(void *ctx, qa_app_qw_player recipient, const void *message) {
  phase_ctx *phase = ctx;
  if (phase->n->ended || phase->n->host.ctx != phase->host.ctx) return;
  for (size_t i = 0; i < phase->n->peer_count; i++) {
    peer *target = phase->n->peers[i];
    if (client_equal(target->player.client, recipient.client)
        && qa_same_actor(target->player.actor, recipient.actor))
      deliver(phase->n, target, message);
  }
}
static void send_peer(qa_app_qw_server_network *n, peer *p, const qa_app_qw_frame *frame, double now);
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
    /* TS catches per-packet errors into a print/disconnect; landed
     * decoders abort with the same messages. */
    if (packet.length >= 4 && packet.payload[0] == 255 && packet.payload[1] == 255
        && packet.payload[2] == 255 && packet.payload[3] == 255) {
      size_t *lengths = NULL;
      size_t count = 0;
      uint8_t **replies = qa_qw_connectionless_receive(&n->connectionless,
          packet.payload, packet.length, &packet.from, now, &lengths, &count);
      for (size_t i = 0; i < count; i++) {
        transport_of(n)->send(transport_of(n)->ctx, &packet.from, replies[i], lengths[i]);
        free(replies[i]);
      }
      free(replies);
      free(lengths);
      qa_receive_event_destroy(&packet);
      continue;
    }
    if (packet.length < 10) { qa_receive_event_destroy(&packet); continue; }
    unsigned qport = (unsigned)packet.payload[8] | ((unsigned)packet.payload[9] << 8);
    peer *owner = NULL;
    for (size_t i = 0; i < n->peer_count; i++)
      if (n->peers[i]->channel.qport == (double)qport
          && qa_net_same_address(&n->peers[i]->remote, &packet.from, false))
        owner = n->peers[i];
    if (!owner) { qa_receive_event_destroy(&packet); continue; }
    uint32_t first_word = (uint32_t)packet.payload[0] | ((uint32_t)packet.payload[1] << 8)
        | ((uint32_t)packet.payload[2] << 16) | ((uint32_t)packet.payload[3] << 24);
    bool can_reply = (double)(first_word % 0x80000000u)
        >= qa_quakeworld_channel_outgoing_sequence(&owner->channel);
    const qa_app_qw_server_administration *admin = administration(n);
    if (admin && admin->blocked && admin->blocked(admin->ctx, &packet.from)) {
      qa_receive_event_destroy(&packet);
      continue;
    }
    qa_quakeworld_delivery delivery = {0};
    int received = qa_quakeworld_channel_receive(&owner->channel, packet.payload,
        packet.length, now, &delivery);
    if (!received) { qa_receive_event_destroy(&packet); continue; }
    ping_frame *acknowledged = &owner->ping_frames[((int64_t)delivery.acknowledged & 63)];
    if (acknowledged->sequence == delivery.acknowledged && acknowledged->sent != 0)
      acknowledged->ping = now - acknowledged->sent;
    double next_sequence = qa_quakeworld_channel_outgoing_sequence(&owner->channel);
    ping_frame *slot = &owner->ping_frames[((int64_t)next_sequence & 63)];
    slot->sequence = next_sequence;
    slot->sent = now;
    slot->ping = -1;
    qa_net_address_destroy(&owner->remote);
    if (!qa_net_address_clone(&packet.from, &owner->remote)) qa_range_error("out of memory");
    owner->last_received = now;
    owner->reply = can_reply;
    owner->has_delta = false;
    size_t message_count = 0;
    qa_quakeworld_profile profile = qw_profile();
    qa_quakeworld_client_message *messages = qa_decode_quakeworld_client(
        delivery.payload, delivery.length, profile, delivery.sequence, &message_count);
    for (size_t i = 0; i < message_count; i++) {
      if (!peer_present(n, owner)) break;
      qa_quakeworld_client_message *message = &messages[i];
      if (strcmp(message->kind, "delta") == 0) {
        owner->delta = message->sequence;
        owner->has_delta = true;
      } else if (strcmp(message->kind, "move") == 0 && owner->active) {
        owner->loss_percent = message->bundle.loss_percent;
        apply_ctx apply = {0};
        qa_quakeworld_command_replay_run(&owner->replay, &message->bundle,
            delivery.dropped, n->host.paused, apply_command, &apply);
        if (apply.count != 0)
          n->host.command_group(n->host.ctx, owner->player, apply.commands,
              apply.count, delivery.sequence);
        free(apply.commands);
      } else if (strcmp(message->kind, "string-command") == 0) {
        size_t arg_count = 0;
        char **args = qa_quakeworld_command_arguments(message->text, &arg_count);
        if (!args) qa_range_error("out of memory");
        const char *name = arg_count ? args[0] : NULL;
        if (name && (strcmp(name, "drop") == 0 || strcmp(name, "disconnect") == 0)) {
          for (size_t j = 0; j < arg_count; j++) free(args[j]);
          free(args);
          disconnect_client(n, owner->player.client, "Client disconnected");
          continue;
        }
        qa_download_source *prepared = NULL;
        bool has_prepared = false;
        if (name && strcmp(name, "download") == 0 && n->host.prepare_download) {
          qa_app_qw_server_host accepted_host = n->host;
          qa_quakeworld_signon_server *accepted_signon = owner->signon;
          qa_app_qw_player accepted_player = owner->player;
          /* TS catches prepareDownload failures into null + print. */
          prepared = (qa_download_source *)n->host.prepare_download(n->host.ctx,
              owner->player, arg_count > 1 ? args[1] : "");
          has_prepared = true;
          if (n->ended || !peer_present(n, owner) || n->host.ctx != accepted_host.ctx
              || owner->signon != accepted_signon
              || !client_equal(owner->player.client, accepted_player.client)) {
            if (prepared && prepared->close) prepared->close(prepared->ctx);
            for (size_t j = 0; j < arg_count; j++) free(args[j]);
            free(args);
            continue;
          }
        }
        qa_qw_command_result result = qa_quakeworld_signon_server_command(
            owner->signon, message->text, prepared, has_prepared);
        if (result.handled) {
          for (size_t j = 0; j < result.count; j++) enqueue(owner, result.messages[j], result.lens[j]);
          free(result.messages);
          free(result.lens);
          free(result.text);
        } else if (name) {
          qa_qw_command_result_destroy(&result);
          phase_ctx phase = {0};
          phase.n = n;
          phase.p = owner;
          phase.host = n->host;
          phase.player = owner->player;
          phase.name = args[0];
          phase.args = args + 1;
          phase.arg_count = arg_count - 1;
          phase.text = message->text;
          phase.now = now;
          n->host.command_phase(n->host.ctx, owner->player, phase_action, &phase,
              phase_emit, &phase);
        } else qa_qw_command_result_destroy(&result);
        for (size_t j = 0; j < arg_count; j++) free(args[j]);
        free(args);
      }
    }
    for (size_t i = 0; i < message_count; i++) qa_quakeworld_client_message_destroy(&messages[i]);
    free(messages);
    free(delivery.payload);
    qa_receive_event_destroy(&packet);
  }
  double timeout = n->options.has_timeout_milliseconds ? n->options.timeout_milliseconds : 65000;
  for (size_t i = 0; i < n->peer_count;) {
    peer *p = n->peers[i];
    if (now - p->last_received > timeout) {
      disconnect_client(n, p->player.client, "Client timed out");
      continue;
    }
    if (!p->active) send_peer(n, p, NULL, now);
    i++;
  }
  return NULL;
}
static void send_peer(qa_app_qw_server_network *n, peer *p, const qa_app_qw_frame *frame, double now) {
  if (!p->reply) return;
  p->reply = false;
  if (!n->host.paused && !qa_quakeworld_channel_can_packet(&p->channel, now)) {
    p->choked++;
    return;
  }
  if (!qa_quakeworld_channel_has_reliable(&p->channel) && p->reliable_count > 0) {
    size_t index = p->reliable_head;
    qa_quakeworld_channel_queue_reliable(&p->channel, p->reliable[index], p->reliable_lengths[index]);
    p->reliable_length = (double)p->reliable_lengths[index];
    free(p->reliable[index]);
    p->reliable_head = (p->reliable_head + 1) % 8;
    p->reliable_count--;
  }
  qa_size_buf buffer;
  if (!qa_size_buf_init(&buffer, 1450, true)) qa_range_error("out of memory");
  double sequence = qa_quakeworld_channel_outgoing_sequence(&p->channel);
  frame_store sent = {0};
  bool has_sent = false;
  if (frame) {
    /* TS catches frame encode failures into a print + clear;
     * landed writers abort with the same messages. */
    qa_quakeworld_profile profile = qw_profile();
    if (p->choked != 0) {
      qa_quakeworld_message choke = {0};
      choke.kind = "choke-count";
      choke.count = p->choked;
      qa_write_quakeworld_message(&buffer, profile, &choke);
      p->choked = 0;
    }
    for (size_t i = 0; i < frame->message_count; i++) {
      const qa_quakeworld_message *message = frame->messages[i];
      if (message->kind && strcmp(message->kind, "print") == 0 && message->level < p->message_level)
        continue;
      qa_write_quakeworld_message(&buffer, profile, message);
    }
    qa_qw_entity_state *states = xmalloc(frame->entity_count * sizeof *states);
    for (size_t i = 0; i < frame->entity_count; i++) {
      const qa_quakeworld_entity *entity = frame->entities[i];
      qa_qw_wire_entity_from_extended(&entity->state,
          ((int64_t)entity->quakeworld_flags & QA_QW_U_SOLID) != 0, &states[i]);
    }
    qa_qw_previous_frame previous = {0};
    const qa_qw_previous_frame *previous_ptr = NULL;
    if (p->has_delta) {
      for (size_t i = 0; i < p->frame_count; i++)
        if (((int64_t)p->frames[i].sequence & 255) == (int64_t)p->delta
            && sequence - p->frames[i].sequence < 64) {
          previous.sequence = p->frames[i].sequence;
          previous.states = p->frames[i].states;
          previous.count = p->frames[i].count;
          previous_ptr = &previous;
        }
    }
    qa_write_quakeworld_entities(&buffer, profile, states, frame->entity_count,
        p->baselines, p->baseline_count, previous_ptr);
    if (buffer.overflowed) qa_range_error("QW frame datagram overflow");
    sent.sequence = sequence;
    sent.states = states;
    sent.count = frame->entity_count;
    has_sent = true;
  }
  size_t packet_length = 0;
  uint8_t *packet = qa_quakeworld_channel_transmit(&p->channel, buffer.data,
      (size_t)buffer.cursize, now, n->host.paused, &packet_length);
  if (!packet) qa_range_error("out of memory");
  transport_of(n)->send(transport_of(n)->ctx, &p->remote, packet, packet_length);
  double reliable_bytes = packet_length > 3 && (packet[3] & 128) != 0 ? p->reliable_length : 0;
  if (has_sent && packet_length == (size_t)(8 + (int64_t)reliable_bytes + buffer.cursize)) {
    if (p->frame_count == p->frame_cap) {
      p->frame_cap = p->frame_cap ? p->frame_cap * 2 : 16;
      p->frames = realloc(p->frames, p->frame_cap * sizeof *p->frames);
      if (!p->frames) qa_range_error("out of memory");
    }
    p->frames[p->frame_count++] = sent;
    size_t kept = 0;
    for (size_t i = 0; i < p->frame_count; i++) {
      if (sequence - p->frames[i].sequence >= 64) free(p->frames[i].states);
      else p->frames[kept++] = p->frames[i];
    }
    p->frame_count = kept;
  } else free(sent.states);
  free(packet);
  qa_size_buf_destroy(&buffer);
}
void qa_app_qw_server_network_submit(qa_app_qw_server_network *n,
    const void *const *commands, size_t count, double now) {
  (void)n; (void)commands; (void)count; (void)now;
}
void qa_app_qw_server_network_publish(qa_app_qw_server_network *n,
    const void *output, const void *const *events, size_t count, double now) {
  if (n->ended) return;
  n->host.observe(n->host.ctx, output, events, count);
  for (size_t i = 0; i < n->peer_count;) {
    peer *p = n->peers[i];
    if (!p->active) { i++; continue; }
    qa_app_qw_frame frame = n->host.frame(n->host.ctx, p->player, output);
    if (!peer_present(n, p)) continue;
    for (size_t j = 0; j < frame.reliable_count; j++) {
      const qa_quakeworld_message *message = frame.reliable[j];
      if (message->kind && strcmp(message->kind, "print") == 0 && message->level < p->message_level)
        continue;
      const qa_quakeworld_message *messages[] = {message};
      size_t length = 0;
      uint8_t *bytes = messages_bytes(messages, 1, &length);
      enqueue(p, bytes, length);
    }
    send_peer(n, p, &frame, now);
    i++;
  }
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
}
static qa_app_network_phase net_phase(void *ctx) { return qa_app_qw_server_network_phase(ctx); }
static const void *net_wire(void *ctx) {
  (void)ctx;
  static qa_quakeworld_profile profile;
  profile = qw_profile();
  return &profile;
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
