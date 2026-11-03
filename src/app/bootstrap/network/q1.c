/* Port of src/app/bootstrap/network/q1.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/network/q1.h"
#include "qa/network/common/js_number.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/bounded_queue.h"
#include "qa/foundation/support.h"
#include "qa/network/q1/channels.h"
#include "qa/network/q1/handshake.h"
#include "qa/network/q1/message.h"
#include "qa/network/common/session.h"
#include "qa/network/q1/profile.h"
#include "qa/network/services/admin.h"
#define MAX_PEERS_PER_ADDRESS 4
/* NetQuake connect has no challenge, so a peer that never answers is dropped quickly
 * instead of retransmitting for the full timeout. */
#define UNANSWERED_TIMEOUT_MILLISECONDS 10000
typedef struct byte_item { uint8_t *bytes; size_t length; } byte_item;
typedef struct peer {
  qa_net_address remote; char *key;
  qa_app_q1_player player;
  qa_netquake_channel channel;
  int stage;
  qa_app_q1_game_state state;
  byte_item *reliable; size_t reliable_count, reliable_cap, reliable_head;
  double last_received, sequence;
  bool heard;
  qa_bounded_queue pings;
} peer;
struct qa_app_q1_server_network {
  qa_app_q1_server_network_options options;
  qa_app_q1_server_host host;
  qa_protocol_codec codec; bool codec_live;
  peer **peers; size_t peer_count, peer_cap;
  const void **pending; size_t pending_count, pending_cap, pending_head;
  double source_seconds;
  struct { double slot; char *name; } *names; size_t name_count, name_cap;
  bool ended;
  const void **poll_result; size_t poll_result_cap;
  qa_connectionless_gate gate;
  /* Removed peers stay allocated until no frame still holds them (TS objects outlive removal). */
  struct peer **retired; size_t retired_count, retired_cap;
};
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
static qa_datagram_transport *transport_of(qa_app_q1_server_network *n) {
  return n->options.transport;
}
static void validate_host(qa_app_q1_server_network *n, const qa_app_q1_server_host *host) {
  (void)n;
  const void *support = host->supports_source_wire ? host->supports_source_wire(host->ctx) : NULL;
  /* WireAdmission rides opaquely: {kind,reasons} with kind text in
   * the first field per network/common/session landing. */
  if (support) {
    const qa_wire_admission *admission = support;
    if (!admission->supported) {
      /* Join reasons with "; ". */
      size_t total = 1;
      for (size_t i = 0; i < admission->reason_count; i++)
        total += strlen(admission->reasons[i]) + 2;
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
}
static void make_codec(qa_app_q1_server_network *n) {
  qa_message_reader reader;
  qa_message_reader_init(&reader, NULL, 0);
  qa_create_netquake_codec(n->host.protocol, &reader, &n->codec);
  n->codec_live = true;
}
static void name_set(qa_app_q1_server_network *n, double slot, const char *value) {
  for (size_t i = 0; i < n->name_count; i++)
    if (n->names[i].slot == slot) {
      free(n->names[i].name);
      n->names[i].name = copy_text(value);
      return;
    }
  if (n->name_count == n->name_cap) {
    n->name_cap = n->name_cap ? n->name_cap * 2 : 8;
    n->names = realloc(n->names, n->name_cap * sizeof *n->names);
    if (!n->names) qa_range_error("out of memory");
  }
  n->names[n->name_count].slot = slot;
  n->names[n->name_count].name = copy_text(value);
  n->name_count++;
}
static const char *name_get(qa_app_q1_server_network *n, double slot) {
  for (size_t i = 0; i < n->name_count; i++)
    if (n->names[i].slot == slot) return n->names[i].name;
  return NULL;
}
static void destroy_buf_slot(void *arg) { qa_size_buf_destroy(arg); }
static uint8_t *encode_messages(qa_app_q1_server_network *n,
    const qa_netquake_message *messages, size_t count, size_t *out_length) {
  for (size_t i = 0; i < count; i++)
    if (strcmp(messages[i].kind, "name") == 0) name_set(n, messages[i].slot, messages[i].str_value ? messages[i].str_value : "");
  qa_size_buf buffer;
  if (!qa_size_buf_init(&buffer, (long long)n->codec.max_msglen, false))
    qa_range_error("out of memory");
  qa_cleanup guard;
  qa_cleanup_push(&guard, destroy_buf_slot, &buffer);
  for (size_t i = 0; i < count; i++)
    qa_write_netquake_message(&buffer, n->host.protocol, &messages[i], "known-retail", true);
  qa_cleanup_pop(&guard);
  uint8_t *bytes = qa_size_buf_bytes(&buffer, out_length);
  qa_size_buf_destroy(&buffer);
  if (!bytes) qa_range_error("out of memory");
  return bytes;
}
static void reliable_admit(peer *p, uint8_t *bytes, size_t length) {
  if (p->reliable_count - p->reliable_head >= 4096) {
    free(bytes);
    qa_range_error("Event ring capacity exhausted (4096 undrained records); drain before admitting more");
  }
  if (p->reliable_count == p->reliable_cap) {
    p->reliable_cap = p->reliable_cap ? p->reliable_cap * 2 : 16;
    p->reliable = realloc(p->reliable, p->reliable_cap * sizeof *p->reliable);
    if (!p->reliable) qa_range_error("out of memory");
  }
  p->reliable[p->reliable_count].bytes = bytes;
  p->reliable[p->reliable_count].length = length;
  p->reliable_count++;
}
static bool reliable_shift(peer *p, byte_item *out) {
  if (p->reliable_head >= p->reliable_count) return false;
  *out = p->reliable[p->reliable_head++];
  if (p->reliable_head == p->reliable_count) p->reliable_head = p->reliable_count = 0;
  return true;
}
static void reliable_reset(peer *p) {
  for (size_t i = p->reliable_head; i < p->reliable_count; i++) free(p->reliable[i].bytes);
  p->reliable_head = p->reliable_count = 0;
}
static void peer_start(qa_app_q1_server_network *n, peer *p) {
  p->stage = 1;
  qa_netquake_message set_view = {0};
  set_view.kind = "set-view";
  set_view.entity = p->player.source_entity;
  qa_netquake_message signon = {0};
  signon.kind = "signon";
  signon.stage = 1;
  qa_netquake_message group[3] = {*(const qa_netquake_message *)p->state.info, set_view, signon};
  size_t length = 0;
  {
    uint8_t *encoded = encode_messages(n, group, 3, &length);
    reliable_admit(p, encoded, length);
  }
}
static peer *find_peer(qa_app_q1_server_network *n, const char *key) {
  for (size_t i = 0; i < n->peer_count; i++)
    if (strcmp(n->peers[i]->key, key) == 0) return n->peers[i];
  return NULL;
}
static void peer_remove(qa_app_q1_server_network *n, peer *p) {
  for (size_t i = 0; i < n->peer_count; i++)
    if (n->peers[i] == p) {
      memmove(n->peers + i, n->peers + i + 1, (n->peer_count - i - 1) * sizeof *n->peers);
      n->peer_count--;
      return;
    }
}
static void peer_destroy(peer *p) {
  qa_net_address_destroy(&p->remote);
  free(p->key);
  qa_netquake_channel_destroy(&p->channel);
  reliable_reset(p);
  free(p->reliable);
  qa_bounded_queue_destroy(&p->pings);
  free(p);
}
static void pending_admit(qa_app_q1_server_network *n, const void *command) {
  if (n->pending_count - n->pending_head >= 8192)
    qa_range_error("Event ring capacity exhausted (8192 undrained records); drain before admitting more");
  if (n->pending_count == n->pending_cap) {
    n->pending_cap = n->pending_cap ? n->pending_cap * 2 : 32;
    n->pending = realloc(n->pending, n->pending_cap * sizeof *n->pending);
    if (!n->pending) qa_range_error("out of memory");
  }
  n->pending[n->pending_count++] = command;
}
static void send_bytes(qa_app_q1_server_network *n, const qa_net_address *to,
    const uint8_t *bytes, size_t length) {
  transport_of(n)->send(transport_of(n)->ctx, to, bytes, length);
}
qa_app_q1_server_network *qa_app_q1_server_network_create(qa_app_q1_server_network_options options) {
  qa_app_q1_server_network *n = calloc(1, sizeof *n);
  if (!n) return NULL;
  n->options = options;
  validate_host(n, &options.host);
  n->host = options.host;
  make_codec(n);
  qa_connectionless_gate_init(&n->gate);
  return n;
}
void qa_app_q1_server_network_destroy(qa_app_q1_server_network *network) {
  if (!network) return;
  for (size_t i = 0; i < network->peer_count; i++) peer_destroy(network->peers[i]);
  free(network->peers);
  free(network->pending);
  free(network->poll_result);
  for (size_t i = 0; i < network->name_count; i++) free(network->names[i].name);
  free(network->names);
  for (size_t i = 0; i < network->retired_count; i++) peer_destroy(network->retired[i]);
  free(network->retired);
  qa_connectionless_gate_destroy(&network->gate);
  free(network);
}
const qa_app_q1_server_network_options *qa_app_q1_server_network_options_of(
    const qa_app_q1_server_network *network) { return &network->options; }
static double peer_ping(peer *p) {
  size_t count = p->pings.count;
  if (count == 0) return 0;
  double sum = 0;
  qa_bounded_queue copy;
  if (!qa_bounded_queue_init(&copy, 1024, sizeof(double))) return 0;
  double value;
  while (qa_bounded_queue_shift(&p->pings, &value)) {
    sum += value;
    qa_bounded_queue_push(&copy, &value);
  }
  while (qa_bounded_queue_shift(&copy, &value)) qa_bounded_queue_push(&p->pings, &value);
  qa_bounded_queue_destroy(&copy);
  return trunc(sum / (double)count);
}
size_t qa_app_q1_server_network_client_pings(qa_app_q1_server_network *network,
    qa_app_q1_client_ping *out, size_t capacity) {
  for (size_t i = 0; i < network->peer_count && i < capacity; i++) {
    out[i].client = network->peers[i]->player.client;
    out[i].ping = peer_ping(network->peers[i]);
  }
  return network->peer_count;
}
bool qa_app_q1_server_network_address(qa_app_q1_server_network *network, qa_net_address *out) {
  return transport_of(network)->get_address(transport_of(network)->ctx, out) != 0;
}
qa_app_network_phase qa_app_q1_server_network_phase(const qa_app_q1_server_network *network) {
  return network->ended ? QA_APP_PHASE_CLOSED : QA_APP_PHASE_ACTIVE;
}
size_t qa_app_q1_server_network_clients(qa_app_q1_server_network *network,
    qa_app_q1_player *out, size_t capacity) {
  for (size_t i = 0; i < network->peer_count && i < capacity; i++)
    out[i] = network->peers[i]->player;
  return network->peer_count;
}
void qa_app_q1_server_network_change_world(qa_app_q1_server_network *network,
    qa_app_q1_server_host host) {
  validate_host(network, &host);
  if (network->peer_count != 0
      && (host.protocol.version != network->host.protocol.version
          || qa_q1_protocol_flags(host.protocol)
              != qa_q1_protocol_flags(network->host.protocol)))
    qa_raise("Error", 0, 0, "Connected NetQuake peers require the same protocol across travel");
  size_t count = network->peer_count;
  qa_app_q1_player *carried = xmalloc(count * sizeof *carried);
  for (size_t i = 0; i < count; i++)
    carried[i] = host.carried_player(host.ctx, network->peers[i]->player.client);
  network->host = host;
  make_codec(network);
  network->pending_head = network->pending_count = 0;
  for (size_t i = 0; i < count; i++) {
    peer *p = network->peers[i];
    p->player = carried[i];
    p->state = host.game_state(host.ctx, carried[i]);
    reliable_reset(p);
    peer_start(network, p);
  }
  free(carried);
}
bool qa_app_q1_server_network_disconnect_client(qa_app_q1_server_network *network,
    qa_client_id client, const char *reason) {
  peer *p = NULL;
  for (size_t i = 0; i < network->peer_count; i++)
    if (network->peers[i]->player.client.slot == client.slot
        && network->peers[i]->player.client.generation == client.generation
        && network->peers[i]->player.client.token == client.token) p = network->peers[i];
  if (!p) return false;
  qa_netquake_message disconnect = {0};
  disconnect.kind = "disconnect";
  size_t length = 0;
  uint8_t *payload = encode_messages(network, &disconnect, 1, &length);
  size_t packet_length = 0;
  uint8_t *packet = qa_netquake_channel_unreliable(&p->channel, payload, length, &packet_length);
  free(payload);
  if (packet) {
    send_bytes(network, &p->remote, packet, packet_length);
    free(packet);
  }
  qa_app_q1_player player = p->player;
  peer_remove(network, p);
  /* pending live-filter by actor: commands are opaque; filter via
   * the qa_actor_command layout (actor is the first field). */
  size_t kept = network->pending_head;
  for (size_t i = network->pending_head; i < network->pending_count; i++) {
    const qa_actor_command *command = network->pending[i];
    bool drop = command->actor.slot == player.actor.slot
        && command->actor.generation == player.actor.generation
        && command->actor.token == player.actor.token;
    if (!drop) network->pending[kept++] = network->pending[i];
  }
  network->pending_count = kept;
  if (network->retired_count == network->retired_cap) {
    network->retired_cap = network->retired_cap ? network->retired_cap * 2 : 8;
    network->retired = realloc(network->retired, network->retired_cap * sizeof *network->retired);
    if (!network->retired) qa_range_error("out of memory");
  }
  network->retired[network->retired_count++] = p;
  network->host.disconnect(network->host.ctx, player, reason);
  return true;
}
static void reap_retired(qa_app_q1_server_network *n) {
  for (size_t i = 0; i < n->retired_count; i++) peer_destroy(n->retired[i]);
  n->retired_count = 0;
}
static bool peer_live(qa_app_q1_server_network *n, const peer *p) {
  for (size_t i = 0; i < n->peer_count; i++) if (n->peers[i] == p) return true;
  return false;
}
typedef struct text_builder { char *text; size_t length, capacity; } text_builder;
static void text_append(text_builder *b, const char *piece) {
  size_t length = strlen(piece);
  if (b->length + length + 1 > b->capacity) {
    size_t capacity = b->capacity ? b->capacity : 128;
    while (capacity < b->length + length + 1) capacity *= 2;
    char *grown = realloc(b->text, capacity);
    if (!grown) qa_range_error("out of memory");
    b->text = grown; b->capacity = capacity;
  }
  memcpy(b->text + b->length, piece, length + 1);
  b->length += length;
}
typedef struct command_scope { char **args; size_t count; text_builder text; qa_netquake_message *group; } command_scope;
static void command_scope_free(void *arg) {
  command_scope *c = arg;
  for (size_t i = 0; i < c->count; i++) free(c->args[i]);
  free(c->args); free(c->text.text); free(c->group);
  memset(c, 0, sizeof *c);
}
static void command_text_body(qa_app_q1_server_network *n, peer *p, const char *text, command_scope *c) {
  c->args = qa_quakeworld_command_arguments(text, &c->count);
  if (c->count == 0) return;
  const char *name = c->args[0];
  if (strcmp(name, "ping") == 0 || strcmp(name, "status") == 0) {
    bool is_ping = strcmp(name, "ping") == 0;
    char number[40];
    if (is_ping) text_append(&c->text, "Client ping times:\n");
    else {
      text_append(&c->text, "map: "); text_append(&c->text, n->host.map_name);
      text_append(&c->text, "\nplayers: "); text_append(&c->text, qa_net_js_number_text((double)n->peer_count, number));
      text_append(&c->text, " active ("); text_append(&c->text, qa_net_js_number_text(n->host.max_clients, number));
      text_append(&c->text, " max)\n");
    }
    for (size_t i = 0; i < n->peer_count; i++) {
      peer *other = n->peers[i];
      const char *row = name_get(n, (double)other->player.client.slot);
      if (!row || row[0] == '\0') row = "unconnected";
      if (is_ping) text_append(&c->text, qa_net_js_number_text(peer_ping(other), number));
      else {
        text_append(&c->text, "#");
        text_append(&c->text, qa_net_js_number_text((double)other->player.client.slot + 1, number));
      }
      text_append(&c->text, " ");
      text_append(&c->text, row);
      if (!is_ping) { text_append(&c->text, " "); text_append(&c->text, other->key); }
      text_append(&c->text, "\n");
    }
    qa_netquake_message print = {0};
    print.kind = "print";
    print.text = c->text.text;
    size_t length = 0;
    {
      uint8_t *encoded = encode_messages(n, &print, 1, &length);
      reliable_admit(p, encoded, length);
    }
  } else if (strcmp(name, "disconnect") == 0) {
    qa_app_q1_server_network_disconnect_client(n, p->player.client, "Client disconnected");
  } else if (strcmp(name, "prespawn") == 0 && p->stage == 1) {
    const qa_app_q1_baselines *baselines = p->state.baselines;
    size_t total = p->state.signon_count + baselines->count + 1;
    c->group = xmalloc(total * sizeof *c->group);
    size_t at = 0;
    for (size_t i = 0; i < p->state.signon_count; i++)
      c->group[at++] = ((const qa_netquake_message *)p->state.signon)[i];
    for (size_t i = 0; i < baselines->count; i++) {
      qa_netquake_message baseline = {0};
      baseline.kind = "baseline";
      baseline.state = baselines->entries[i].state;
      c->group[at++] = baseline;
    }
    qa_netquake_message signon = {0};
    signon.kind = "signon";
    signon.stage = 2;
    c->group[at++] = signon;
    size_t length = 0;
    {
      uint8_t *encoded = encode_messages(n, c->group, at, &length);
      reliable_admit(p, encoded, length);
    }
    p->stage = 2;
  } else if (strcmp(name, "spawn") == 0 && p->stage == 2) {
    size_t spawn_count = 0;
    const qa_netquake_message *spawn =
        (const qa_netquake_message *)n->host.spawn(n->host.ctx, p->player, &spawn_count);
    c->group = xmalloc((spawn_count + 1) * sizeof *c->group);
    for (size_t i = 0; i < spawn_count; i++) c->group[i] = spawn[i];
    qa_netquake_message signon = {0};
    signon.kind = "signon";
    signon.stage = 3;
    c->group[spawn_count] = signon;
    size_t length = 0;
    {
      uint8_t *encoded = encode_messages(n, c->group, spawn_count + 1, &length);
      reliable_admit(p, encoded, length);
    }
    p->stage = 3;
  } else if (strcmp(name, "begin") == 0 && p->stage == 3) {
    p->stage = 4;
  } else if (strcmp(name, "prespawn") != 0 && strcmp(name, "spawn") != 0
      && strcmp(name, "begin") != 0) {
    n->host.command(n->host.ctx, p->player, name, (const char *const *)c->args + 1, c->count - 1);
  }
}
static void command_text(qa_app_q1_server_network *n, peer *p, const char *text) {
  command_scope c;
  memset(&c, 0, sizeof c);
  qa_cleanup guard;
  qa_cleanup_push(&guard, command_scope_free, &c);
  command_text_body(n, p, text, &c);
  qa_cleanup_pop(&guard);
  command_scope_free(&c);
}
typedef struct control_ctx {
  qa_app_q1_server_network *n; const qa_net_address *from; double now;
} control_ctx;
static void control_server_info(void *ctx, qa_netquake_control *out) {
  control_ctx *c = ctx;
  memset(out, 0, sizeof *out);
  out->kind = QA_NQ_SERVER_INFO;
  qa_net_address address;
  char *key = qa_app_q1_server_network_address(c->n, &address)
      ? qa_net_address_key(&address, true) : NULL;
  if (key) qa_net_address_destroy(&address);
  out->address = key;
  out->name = copy_text("Quake");
  out->map = copy_text(c->n->host.map_name);
  out->players = (double)c->n->peer_count;
  out->max_players = c->n->host.max_clients;
  out->version = 3;
}
static int control_player_info(void *ctx, double index, qa_netquake_control *out) {
  (void)ctx; (void)index; (void)out;
  return 0;
}
static int control_next_rule(void *ctx, const char *previous, char **name, char **value) {
  (void)ctx; (void)previous; (void)name; (void)value;
  return 0;
}
typedef struct connect_scope {
  qa_app_q1_server_network *n; const qa_net_address *from; char *key; qa_app_q1_player player; double now;
} connect_scope;
static void free_partial_peer(void *arg) {
  peer *p = arg;
  qa_net_address_destroy(&p->remote);
  free(p->key);
  qa_netquake_channel_destroy(&p->channel);
  reliable_reset(p);
  free(p->reliable);
  qa_bounded_queue_destroy(&p->pings);
  free(p);
}
static void connect_body(void *ctx) {
  connect_scope *scope = ctx;
  qa_app_q1_server_network *n = scope->n;
  peer *p = calloc(1, sizeof *p);
  if (!p) { free(scope->key); qa_range_error("out of memory"); }
  p->key = scope->key;
  qa_cleanup guard;
  qa_cleanup_push(&guard, free_partial_peer, p);
  if (!qa_net_address_clone(scope->from, &p->remote)) qa_range_error("out of memory");
  p->player = scope->player;
  if (!qa_netquake_channel_init(&p->channel, n->codec.max_msglen, 1024))
    qa_range_error("out of memory");
  p->stage = 1;
  if (!qa_bounded_queue_init(&p->pings, 16, sizeof(double))) qa_range_error("out of memory");
  p->state = n->host.game_state(n->host.ctx, scope->player);
  p->last_received = scope->now;
  p->heard = false;
  peer_start(n, p);
  if (n->peer_count == n->peer_cap) {
    n->peer_cap = n->peer_cap ? n->peer_cap * 2 : 8;
    n->peers = realloc(n->peers, n->peer_cap * sizeof *n->peers);
    if (!n->peers) qa_range_error("out of memory");
  }
  qa_cleanup_pop(&guard);
  n->peers[n->peer_count++] = p;
}
static qa_nq_connect_result control_connect(void *ctx, const qa_net_address *from, double now) {
  control_ctx *c = ctx;
  (void)now;
  qa_app_q1_server_network *n = c->n;
  qa_net_address address;
  bool has_address = qa_app_q1_server_network_address(n, &address);
  qa_nq_connect_result result = {1, 0, "NetQuake control requires an IP port"};
  if (!has_address) return result;
  bool loopback = address.kind == QA_NET_LOOPBACK;
  double port = (double)address.port;
  qa_net_address_destroy(&address);
  if (loopback) return result;
  char *key = qa_net_address_key(from, true);
  if (!key) qa_range_error("out of memory");
  if (find_peer(n, key)) {
    free(key);
    result.kind = 0; result.port = port; result.reason = NULL;
    return result;
  }
  size_t same = 0;
  for (size_t i = 0; i < n->peer_count; i++)
    if (qa_net_same_address(&n->peers[i]->remote, from, false)) same++;
  if (same >= MAX_PEERS_PER_ADDRESS) {
    free(key);
    result.reason = "Too many connections from this address";
    return result;
  }
  qa_app_q1_admission admitted = n->host.admit(n->host.ctx, from);
  if (!admitted.accepted) {
    free(key);
    result.reason = admitted.reason;
    return result;
  }
  connect_scope scope = { n, from, key, admitted.player, c->now };
  qa_caught_error caught;
  if (!qa_try(connect_body, &scope, &caught)) {
    n->host.disconnect(n->host.ctx, admitted.player, "Signon failed");
    qa_rethrow(&caught);
  }
  result.kind = 0; result.port = port; result.reason = NULL;
  return result;
}
typedef struct packet_scope {
  qa_app_q1_server_network *n; qa_receive_event *packet; double now;
  uint8_t *response; uint8_t *reply;
  qa_channel_delivery delivery;
  qa_netquake_client_message *messages; size_t count;
} packet_scope;
static void packet_scope_release(packet_scope *scope) {
  free(scope->response); free(scope->reply); free(scope->delivery.payload);
  for (size_t i = 0; i < scope->count; i++) qa_netquake_client_message_destroy(&scope->messages[i]);
  free(scope->messages);
  memset(scope, 0, sizeof *scope);
}
static void packet_body(void *ctx) {
  packet_scope *scope = ctx;
  qa_app_q1_server_network *n = scope->n;
  qa_receive_event *packet = scope->packet;
  double now = scope->now;
  /* DataView.getUint32(0) is big-endian: NETFLAG_CTL is the first byte. */
  bool control = packet->length >= 4
      && ((((uint32_t)packet->payload[0] << 24) | ((uint32_t)packet->payload[1] << 16)
          | ((uint32_t)packet->payload[2] << 8) | (uint32_t)packet->payload[3]) >> 16) == 0x8000;
  if (control) {
    if (!qa_connectionless_gate_allow(&n->gate, &packet->from, now)) return;
    control_ctx ctx2 = {n, &packet->from, now};
    qa_netquake_host host = {&ctx2, control_server_info, control_player_info,
        control_next_rule, control_connect};
    size_t response_length = 0;
    scope->response = qa_answer_netquake_control(packet->payload, packet->length,
        &packet->from, now, &host, &response_length);
    if (scope->response) send_bytes(n, &packet->from, scope->response, response_length);
    return;
  }
  char *key = qa_net_address_key(&packet->from, true);
  if (!key) qa_range_error("out of memory");
  peer *p = find_peer(n, key);
  free(key);
  if (!p) return;
  size_t reply_length = 0;
  qa_netquake_channel_receive(&p->channel, packet->payload, packet->length, now,
      &scope->delivery, &scope->reply, &reply_length);
  p->last_received = now;
  p->heard = true;
  if (scope->reply) send_bytes(n, &p->remote, scope->reply, reply_length);
  if (!scope->delivery.payload) return;
  scope->messages = qa_decode_netquake_client(scope->delivery.payload, scope->delivery.length,
      n->host.protocol, &scope->count);
  for (size_t i = 0; i < scope->count; i++) {
    if (!peer_live(n, p)) break;
    qa_netquake_client_message *message = &scope->messages[i];
    if (strcmp(message->kind, "string-command") == 0) command_text(n, p, message->text);
    else if (strcmp(message->kind, "disconnect") == 0)
      qa_app_q1_server_network_disconnect_client(n, p->player.client, "Client disconnected");
    else if (strcmp(message->kind, "move") == 0 && p->stage == 4) {
      double ping = (n->source_seconds - message->command.acknowledged_server_time_seconds) * 1000;
      if (!(ping > 0)) ping = ping < 0 ? 0 : ping;
      qa_bounded_queue_push(&p->pings, &ping);
      pending_admit(n, n->host.input(n->host.ctx, p->player, &message->command, p->sequence++));
    }
  }
}
const void *const *qa_app_q1_server_network_poll(qa_app_q1_server_network *n,
    double now, size_t *out_count) {
  *out_count = 0;
  if (n->ended) return NULL;
  for (;;) {
    qa_receive_event packet;
    if (!transport_of(n)->poll(transport_of(n)->ctx, &packet)) break;
    if (packet.kind != QA_RECV_PACKET) {
      if (packet.kind == QA_RECV_ERROR && packet.error)
        n->host.print(n->host.ctx, packet.error);
      qa_receive_event_destroy(&packet);
      continue;
    }
    if (n->options.trusted_networks_only && !qa_net_is_private_network_address(&packet.from)) {
      qa_receive_event_destroy(&packet);
      continue;
    }
    if (n->host.rejects && n->host.rejects(n->host.ctx, &packet.from)) {
      qa_receive_event_destroy(&packet);
      continue;
    }
    packet_scope scope;
    memset(&scope, 0, sizeof scope);
    scope.n = n; scope.packet = &packet; scope.now = now;
    qa_caught_error caught;
    bool ok = qa_try(packet_body, &scope, &caught);
    packet_scope_release(&scope);
    if (!ok) {
      char *key = qa_net_address_key(&packet.from, true);
      peer *p = key ? find_peer(n, key) : NULL;
      free(key);
      if (p) qa_app_q1_server_network_disconnect_client(n, p->player.client, caught.message);
      n->host.print(n->host.ctx, caught.message);
    }
    qa_receive_event_destroy(&packet);
    reap_retired(n);
  }
  for (size_t i = 0; i < n->peer_count;) {
    peer *p = n->peers[i];
    double timeout = n->options.has_timeout_milliseconds
        ? n->options.timeout_milliseconds : 65000;
    if (!p->heard && now - p->last_received > UNANSWERED_TIMEOUT_MILLISECONDS) {
      qa_app_q1_server_network_disconnect_client(n, p->player.client, "Connection was not answered");
      continue;
    }
    if (now - p->last_received > timeout) {
      qa_app_q1_server_network_disconnect_client(n, p->player.client, "Connection timed out");
      continue;
    }
    if (qa_netquake_channel_can_send_reliable(&p->channel)) {
      byte_item item;
      if (reliable_shift(p, &item)) {
        qa_netquake_channel_queue_reliable(&p->channel, item.bytes, item.length);
        free(item.bytes);
      }
    }
    size_t length = 0;
    uint8_t *bytes = qa_netquake_channel_next(&p->channel, now, &length);
    if (bytes) {
      send_bytes(n, &p->remote, bytes, length);
      free(bytes);
    }
    i++;
  }
  size_t count = n->pending_count - n->pending_head;
  if (n->poll_result_cap < count) {
    n->poll_result = realloc(n->poll_result, count * sizeof *n->poll_result);
    if (!n->poll_result) qa_range_error("out of memory");
    n->poll_result_cap = count;
  }
  for (size_t i = 0; i < count; i++) n->poll_result[i] = n->pending[n->pending_head + i];
  n->pending_head = n->pending_count = 0;
  reap_retired(n);
  *out_count = count;
  return n->poll_result;
}
void qa_app_q1_server_network_submit(qa_app_q1_server_network *network,
    const void *const *commands, size_t count, double now) {
  (void)network; (void)commands; (void)count; (void)now;
  qa_raise("Error", 0, 0, "Server input must enter the application input batch");
}
void qa_app_q1_server_network_publish(qa_app_q1_server_network *n,
    const void *output, const void *const *events, size_t event_count,
    double frame_time_seconds, double now) {
  if (n->ended) return;
  n->host.observe(n->host.ctx, output, events, event_count);
  n->source_seconds = frame_time_seconds;
  for (size_t i = 0; i < n->peer_count; i++) {
    peer *p = n->peers[i];
    qa_app_q1_frame frame = n->host.frame(n->host.ctx, p->player, output);
    if (frame.reliable_count != 0) {
      size_t length = 0;
      {
        uint8_t *encoded = encode_messages(n, (const qa_netquake_message *)frame.reliable,
          frame.reliable_count, &length);
        reliable_admit(p, encoded, length);
      }
    }
    if (qa_netquake_channel_can_send_reliable(&p->channel)) {
      byte_item item;
      if (reliable_shift(p, &item)) {
        qa_netquake_channel_queue_reliable(&p->channel, item.bytes, item.length);
        free(item.bytes);
      }
    }
    size_t reliable_length = 0;
    uint8_t *reliable = qa_netquake_channel_next(&p->channel, now, &reliable_length);
    if (reliable) {
      send_bytes(n, &p->remote, reliable, reliable_length);
      free(reliable);
    }
    if (p->stage != 4) continue;
    int64_t transport_bytes = transport_of(n)->max_datagram_bytes
        ? transport_of(n)->max_datagram_bytes(transport_of(n)->ctx) : -1;
    double transport_max = (double)(transport_bytes < 0 ? 65507 : transport_bytes) - 8;
    double max_datagram = n->codec.max_datagram < transport_max
        ? n->codec.max_datagram : transport_max;
    qa_size_buf buffer;
    if (!qa_size_buf_init(&buffer, (long long)max_datagram, false))
      qa_range_error("out of memory");
    qa_write_netquake_time(&buffer, frame.seconds);
    for (size_t j = 0; j < frame.message_count; j++)
      qa_write_netquake_message(&buffer, n->host.protocol,
          &((const qa_netquake_message *)frame.messages)[j], "known-retail", true);
    const qa_app_q1_baselines *baselines = p->state.baselines;
    for (size_t j = 0; j < frame.entity_count; j++) {
      const qa_q1_extended_entity_state *state =
          &((const qa_q1_extended_entity_state *)frame.entities)[j];
      const qa_q1_extended_entity_state *baseline = NULL;
      for (size_t k = 0; k < baselines->count; k++)
        if (baselines->entries[k].number == state->base.number) baseline = &baselines->entries[k].state;
      qa_q1_wire_entity_state wire;
      qa_q1_wire_entity_state_init(&wire);
      qa_q1_extended_entity_state fallback = qa_nq_entity_state_from_wire(state->base.number, &wire);
      qa_size_buf encoded;
      if (!qa_size_buf_init(&encoded, 128, false)) qa_range_error("out of memory");
      qa_write_netquake_entity(&encoded, n->host.protocol, state,
          baseline ? baseline : &fallback, frame.seconds);
      if (buffer.cursize + encoded.cursize > (long long)max_datagram) {
        qa_size_buf_destroy(&encoded);
        break;
      }
      size_t encoded_length = 0;
      uint8_t *encoded_bytes = qa_size_buf_bytes(&encoded, &encoded_length);
      qa_sz_write(&buffer, encoded_bytes, (long long)encoded_length, (long long)encoded_length);
      free(encoded_bytes);
      qa_size_buf_destroy(&encoded);
    }
    qa_size_buf datagram;
    if (!qa_size_buf_init(&datagram, (long long)max_datagram, false))
      qa_range_error("out of memory");
    for (size_t j = 0; j < frame.datagram_count; j++) {
      size_t encoded_length = 0;
      uint8_t *encoded = encode_messages(n,
          &((const qa_netquake_message *)frame.datagram)[j], 1, &encoded_length);
      bool fits = datagram.cursize + (long long)encoded_length <= (long long)max_datagram;
      if (fits) qa_sz_write(&datagram, encoded, (long long)encoded_length, (long long)encoded_length);
      free(encoded);
      if (!fits) break;
    }
    if (buffer.cursize + datagram.cursize <= (long long)max_datagram) {
      size_t datagram_length = 0;
      uint8_t *datagram_bytes = qa_size_buf_bytes(&datagram, &datagram_length);
      qa_sz_write(&buffer, datagram_bytes, (long long)datagram_length, (long long)datagram_length);
      free(datagram_bytes);
    }
    qa_size_buf_destroy(&datagram);
    size_t length = 0;
    uint8_t *payload = qa_size_buf_bytes(&buffer, &length);
    qa_size_buf_destroy(&buffer);
    size_t packet_length = 0;
    uint8_t *packet = qa_netquake_channel_unreliable(&p->channel, payload, length, &packet_length);
    free(payload);
    if (packet) {
      send_bytes(n, &p->remote, packet, packet_length);
      free(packet);
    }
  }
}
static void close_body(void *ctx) {
  qa_app_q1_server_network *network = ctx;
  while (network->peer_count > 0)
    qa_app_q1_server_network_disconnect_client(network,
        network->peers[0]->player.client, "Server shutdown");
}
void qa_app_q1_server_network_close(qa_app_q1_server_network *network) {
  if (network->ended) return;
  /* TS try { ... } finally { ended = true; transport.close(); } */
  qa_caught_error caught;
  bool ok = qa_try(close_body, network, &caught);
  network->ended = true;
  transport_of(network)->close(transport_of(network)->ctx);
  reap_retired(network);
  if (!ok) qa_rethrow(&caught);
}
static qa_app_network_role net_role(void) { return QA_APP_ROLE_SERVER; }
static qa_app_network_phase net_phase(void *ctx) {
  return qa_app_q1_server_network_phase(ctx);
}
static qa_wire_selection source_wire(qa_protocol_identity protocol) {
  qa_wire_selection wire;
  memset(&wire, 0, sizeof wire);
  wire.protocol = protocol;
  return wire;
}
/* TS get wire(): { kind: "source", protocol: this.host.protocol } */
static qa_wire_selection net_wire(void *ctx) {
  qa_app_q1_server_network *n = ctx;
  return source_wire(n->host.protocol);
}
static const void *const *net_poll(void *ctx, double now, size_t *out_count) {
  return qa_app_q1_server_network_poll(ctx, now, out_count);
}
static void net_submit(void *ctx, const void *const *commands, size_t count, double now) {
  qa_app_q1_server_network_submit(ctx, commands, count, now);
}
static void net_publish(void *ctx, const void *output, const void *const *events,
    size_t count, double now) {
  const qa_simulation_output *simulation = output;
  double seconds = simulation->snapshot.frame.time.kind == QA_SOURCE_TIME_SECONDS
      ? simulation->snapshot.frame.time.value : simulation->snapshot.frame.time.value / 1000;
  qa_app_q1_server_network_publish(ctx, output, events, count, seconds, now);
}
static void net_close(void *ctx) { qa_app_q1_server_network_close(ctx); }
qa_app_application_network qa_app_q1_server_network_as_network(qa_app_q1_server_network *network) {
  qa_app_application_network result = {0};
  result.ctx = network;
  result.role = net_role();
  result.phase = net_phase;
  result.wire = net_wire;
  result.poll = net_poll;
  result.submit = net_submit;
  result.publish = net_publish;
  result.close = net_close;
  return result;
}
