/* Port of src/app/bootstrap/network/q3.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/network/q3.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "qa/app/bootstrap/simulation/q3/host.h"
#include "qa/app/bootstrap/simulation/types.h"
#include "qa/core/commands/text.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/network/q3/configstrings.h"
#include "qa/network/q3/connectionless.h"
#include "qa/network/q3/discovery.h"
#include "qa/network/q3/download.h"
#include "qa/network/q3/pure.h"
#include "qa/network/q3/server.h"
#include "qa/network/q3/transport.h"
#include "qa/network/services/admin.h"
#include "qa/network/services/discovery.h"
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
static void net_to_q3(const qa_net_address *in, qa_q3_address *out) {
  memset(out, 0, sizeof *out);
  out->port = in->port;
  switch (in->kind) {
    case QA_NET_IPV4:
      out->kind = QA_Q3_ADDR_IPV4;
      memcpy(out->host, in->ipv4, 4);
      break;
    case QA_NET_IPV6:
      out->kind = QA_Q3_ADDR_IPV6;
      out->ipv6_host = in->host ? copy_text(in->host) : NULL;
      break;
    case QA_NET_LOOPBACK:
      out->kind = QA_Q3_ADDR_LOOPBACK;
      out->loopback_id = in->host ? copy_text(in->host) : NULL;
      break;
    case QA_NET_IPX:
      out->kind = QA_Q3_ADDR_IPX;
      out->network = (int64_t)in->network;
      memcpy(out->node, in->node, 6);
      break;
  }
}
static void q3_to_net(const qa_q3_address *in, qa_net_address *out) {
  memset(out, 0, sizeof *out);
  out->port = in->port;
  switch (in->kind) {
    case QA_Q3_ADDR_IPV4:
      out->kind = QA_NET_IPV4;
      memcpy(out->ipv4, in->host, 4);
      break;
    case QA_Q3_ADDR_IPV6:
      out->kind = QA_NET_IPV6;
      out->host = in->ipv6_host ? copy_text(in->ipv6_host) : NULL;
      break;
    case QA_Q3_ADDR_LOOPBACK:
      out->kind = QA_NET_LOOPBACK;
      out->host = in->loopback_id ? copy_text(in->loopback_id) : NULL;
      break;
    case QA_Q3_ADDR_IPX:
      out->kind = QA_NET_IPX;
      out->network = (uint32_t)in->network;
      memcpy(out->node, in->node, 6);
      break;
  }
}
typedef struct baseline_entry {
  int64_t number; qa_q3_entity_state_record entity;
} baseline_entry;
typedef struct peer {
  int64_t slot;
  qa_q3_server_download *download;
  qa_q3_server_snapshot_history *snapshots;
  baseline_entry *baselines; size_t baseline_count;
  qa_net_address remote;
  qa_q3_address remote_q3;
  qa_app_q3_player player;
  qa_q3_server_connection *connection;
  double last_received, connected_at, sequence;
  char *userinfo;
  void *binding; /* binding_ctx, owned */
} peer;
struct qa_app_q3_server_network {
  qa_app_q3_server_network_options options;
  qa_app_q3_server_host host;
  bool ended, in_operation, closing;
  double now, server_id, restarted_server_id;
  int server_flags;
  int64_t checksum_feed;
  bool changed_world;
  const void **pending; size_t pending_count, pending_cap;
  peer **overflowed; size_t overflow_count, overflow_cap;
  peer **peers; size_t peer_count, peer_cap;
  qa_q3_snapshot_entities *entities;
  qa_q3_server_admission *admission;
  qa_q3_server_authorization *authorization;
  qa_rcon_service rcon;
  qa_master_heartbeat_service heartbeat_service;
  qa_q3_discovery_wire_state discovery_wire; /* TS q3DiscoveryWire() closure state */
  qa_q3_admission_slot *slot_cache; size_t slot_cache_cap;
  const void **poll_result; size_t poll_cap;
  /* Disconnected peers stay allocated until the operation ends: connection
   * frames still on the stack read them after a drop (TS keeps them by GC). */
  peer **retired; size_t retired_count, retired_cap;
  qa_connectionless_gate gate; /* TS ConnectionlessGate */
  /* TS DiagnosticBudget(20, 2) over performance.now(). */
  double diagnostic_tokens, diagnostic_last;
};
/* TS (random() << 16) ^ random(): both operands go through ToInt32. */
static int64_t checksum_feed_of(double first, double second) {
  return (int32_t)(((uint32_t)qa_int32(first) << 16) ^ (uint32_t)qa_int32(second));
}
static double monotonic_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}
/* TS DiagnosticBudget.allow: bounds per-packet diagnostics so malformed traffic
 * cannot flood the console or log. */
static bool diagnostic_allow(qa_app_q3_server_network *n) {
  double now = monotonic_ms();
  double tokens = n->diagnostic_tokens + (now - n->diagnostic_last) / 1000.0 * 2.0;
  n->diagnostic_tokens = tokens < 20.0 ? tokens : 20.0;
  n->diagnostic_last = now;
  if (n->diagnostic_tokens < 1) return false;
  n->diagnostic_tokens--;
  return true;
}
/* TS printablePeerText(text, 256): control characters other than tab and
 * newline become '?', then the text is bounded (UTF-16 units = bytes here). */
static void printable_peer_text(const char *text, char *out, size_t capacity) {
  size_t used = 0;
  for (const unsigned char *s = (const unsigned char *)text; *s && used < 256 && used + 1 < capacity; s++)
    out[used++] = ((*s <= 0x08) || (*s >= 0x0b && *s <= 0x1f) || *s == 0x7f) ? '?' : (char)*s;
  out[used] = '\0';
}

static void require_supported(qa_app_q3_server_host *host) {
  const void *support = host->supports_source_wire ? host->supports_source_wire(host->ctx) : NULL;
  if (!support) return;
  const qa_wire_admission *admission = support;
  if (admission->supported) return;
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
  char text[QA_CAUGHT_MESSAGE_CAPACITY];
  snprintf(text, sizeof text, "%s", message);
  free(message);
  qa_raise("Error", 0, 0, "%s", text);
}
static peer *peer_at(qa_app_q3_server_network *n, int64_t slot) {
  for (size_t i = 0; i < n->peer_count; i++)
    if (n->peers[i]->slot == slot) return n->peers[i];
  return NULL;
}
static qa_q3_slot_phase slot_phase(qa_q3_server_connection_phase phase) {
  switch (phase) {
    case QA_Q3_SERVER_PHASE_CONNECTED: return QA_Q3_SLOT_CONNECTED;
    case QA_Q3_SERVER_PHASE_PRIMED: return QA_Q3_SLOT_PRIMED;
    case QA_Q3_SERVER_PHASE_ACTIVE: return QA_Q3_SLOT_ACTIVE;
    case QA_Q3_SERVER_PHASE_ZOMBIE: return QA_Q3_SLOT_ZOMBIE;
  }
  return QA_Q3_SLOT_FREE;
}
static const qa_q3_admission_slot *slots_of(void *ctx, size_t *count) {
  qa_app_q3_server_network *n = ctx;
  size_t total = (size_t)n->host.max_clients;
  if (n->slot_cache_cap < total) {
    n->slot_cache = realloc(n->slot_cache, total * sizeof *n->slot_cache);
    if (!n->slot_cache) qa_range_error("out of memory");
    n->slot_cache_cap = total;
  }
  size_t occupied_count = 0;
  const double *occupied = n->host.occupied_slots
      ? n->host.occupied_slots(n->host.ctx, &occupied_count) : NULL;
  for (size_t slot = 0; slot < total; slot++) {
    qa_q3_admission_slot *entry = &n->slot_cache[slot];
    memset(entry, 0, sizeof *entry);
    entry->slot = (int64_t)slot;
    peer *p = peer_at(n, (int64_t)slot);
    if (!p) {
      bool is_occupied = false;
      for (size_t i = 0; i < occupied_count; i++)
        if (occupied[i] == (double)slot) is_occupied = true;
      entry->phase = is_occupied ? QA_Q3_SLOT_ACTIVE : QA_Q3_SLOT_FREE;
    } else {
      entry->phase = slot_phase(qa_q3_server_connection_get_phase(p->connection));
      entry->has_address = true;
      entry->address = p->remote_q3;
      entry->qport = qa_q3_netchannel_get_qport(qa_q3_server_connection_channel(p->connection));
      entry->last_connect_time = p->connected_at;
    }
  }
  *count = total;
  return n->slot_cache;
}
static void send_q3(qa_app_q3_server_network *n, const qa_q3_address *to,
    const uint8_t *bytes, size_t length) {
  qa_net_address address;
  q3_to_net(to, &address);
  n->options.transport->send(n->options.transport->ctx, &address, bytes, length);
  qa_net_address_destroy(&address);
}
static void delivery_send(void *ctx, const qa_q3_address *to, const uint8_t *bytes, size_t length) {
  send_q3(ctx, to, bytes, length);
}
static void remote_of(void *ctx, qa_q3_address *out) {
  peer *p = ctx;
  /* The delivery owns and destroys the address it is given. */
  if (!qa_q3_mirror_address_copy(out, &p->remote_q3)) qa_range_error("out of memory");
}
static void host_print(void *ctx, const char *text) {
  qa_app_q3_server_network *n = ctx;
  n->host.print(n->host.ctx, text);
}
typedef struct delivery_holder { qa_q3_channel_delivery_storage storage; } delivery_holder;
static const qa_q3_channel_delivery *delivery_for(qa_app_q3_server_network *n, peer *p,
    qa_q3_channel_delivery_storage *storage) {
  return qa_q3_channel_delivery_create(storage, delivery_send, n, remote_of, p,
      qa_q3_server_connection_source_state(p->connection), host_print, n);
}
static void gamestate(qa_app_q3_server_network *n, peer *p) {
  const qa_q3_gamestate *state = n->host.game_state(n->host.ctx, p->player, n->server_id);
  free(p->baselines);
  p->baselines = NULL;
  p->baseline_count = 0;
  size_t count = 0;
  for (size_t i = 0; i < state->entry_count; i++)
    if (state->entries[i].kind == QA_Q3_GS_BASELINE) count++;
  p->baselines = xmalloc(count * sizeof *p->baselines);
  for (size_t i = 0; i < state->entry_count; i++)
    if (state->entries[i].kind == QA_Q3_GS_BASELINE) {
      p->baselines[p->baseline_count].number = state->entries[i].number;
      p->baselines[p->baseline_count].entity = state->entries[i].entity;
      p->baseline_count++;
    }
  const qa_q3_server_rate *rate = n->host.rate(n->host.ctx, p->player);
  qa_q3_channel_delivery_storage storage = {0};
  const qa_q3_channel_delivery *delivery = delivery_for(n, p, &storage);
  qa_q3_server_connection_send_gamestate(p->connection, state, rate, delivery);
}
static void assert_world(qa_app_q3_server_network *n, qa_app_q3_server_host *host, double server_id) {
  if (n->ended || n->host.ctx != host->ctx || n->server_id != server_id)
    qa_raise("Error", 0, 0, "Q3 server operation belongs to a retired world");
}
static void assert_peer(qa_app_q3_server_network *n, peer *p,
    qa_app_q3_server_host *host, double server_id) {
  assert_world(n, host, server_id);
  if (peer_at(n, p->slot) != p
      || qa_q3_server_connection_get_phase(p->connection) == QA_Q3_SERVER_PHASE_ZOMBIE)
    qa_raise("Error", 0, 0, "Q3 server callback belongs to a dropped client");
}
static void queue_reliable(qa_app_q3_server_network *n, peer *p, const char *text) {
  qa_q3_server_command_append result =
      qa_q3_server_reliable_commands_add(qa_q3_server_connection_reliable(p->connection), text);
  if (result.kind == QA_Q3_APPEND_OVERFLOW) {
    for (size_t i = 0; i < n->overflow_count; i++)
      if (n->overflowed[i] == p) return;
    if (n->overflow_count == n->overflow_cap) {
      n->overflow_cap = n->overflow_cap ? n->overflow_cap * 2 : 8;
      n->overflowed = realloc(n->overflowed, n->overflow_cap * sizeof *n->overflowed);
      if (!n->overflowed) qa_range_error("out of memory");
    }
    n->overflowed[n->overflow_count++] = p;
  }
}
static void snapshot_peer(qa_app_q3_server_network *n, peer *p);
typedef struct append_ctx { qa_app_q3_server_network *n; peer *p; const qa_q3_server_rate *rate; } append_ctx;
static void append_download(void *ctx, qa_q3_message_writer *writer) {
  append_ctx *append = ctx;
  qa_q3_download_rate rate = {append->rate->rate, append->rate->max_rate, append->rate->snapshot_msec};
  qa_q3_server_download_write(append->p->download, writer, append->n->host.time(append->n->host.ctx), &rate);
}
static void append_nothing(void *ctx, qa_q3_message_writer *writer) {
  (void)ctx; (void)writer;
}
static void snapshot_peer(qa_app_q3_server_network *n, peer *p) {
  qa_q3_netchannel *channel = qa_q3_server_connection_channel(p->connection);
  if (!qa_q3_netchannel_has_unsent_fragments(channel)) {
    qa_app_q3_snapshot snapshot = n->host.snapshot(n->host.ctx, p->player);
    /* q3-types: entities is a list of entity records (one pointer each); the
     * snapshot store copies them from contiguous storage. */
    qa_q3_entity_state_record *entities = xmalloc(snapshot.entity_count * sizeof *entities);
    for (size_t i = 0; i < snapshot.entity_count; i++)
      entities[i] = *(const qa_q3_entity_state_record *)snapshot.entities[i];
    qa_cleanup guard;
    qa_cleanup_push(&guard, free, entities);
    qa_q3_server_snapshot_history_capture(p->snapshots,
        qa_q3_netchannel_outgoing_sequence(channel),
        (const qa_q3_player_state_record *)snapshot.player,
        snapshot.area_mask, snapshot.area_mask_length,
        entities, snapshot.entity_count);
    qa_cleanup_pop(&guard);
    free(entities);
  }
  const qa_q3_server_rate *rate = n->host.rate(n->host.ctx, p->player);
  qa_q3_channel_delivery_storage storage = {0};
  const qa_q3_channel_delivery *delivery = delivery_for(n, p, &storage);
  append_ctx append = {n, p, rate};
  qa_q3_server_connection_send_snapshot(p->connection, n->server_flags, rate, delivery,
      append_download, &append);
}
static bool disconnect_client(qa_app_q3_server_network *n, qa_client_id client, const char *reason);
static void queue_disconnect(qa_app_q3_server_network *n, peer *p, const char *reason) {
  char text[1200];
  size_t used = (size_t)snprintf(text, sizeof text, "disconnect \"");
  for (const char *s = reason; *s && used < sizeof text - 3; s++)
    if (*s != '"' && *s != '\n' && *s != '\r') text[used++] = *s;
  text[used++] = '"';
  text[used] = '\0';
  qa_q3_server_reliable_commands_add(qa_q3_server_connection_reliable(p->connection), text);
  qa_q3_channel_delivery_storage storage = {0};
  const qa_q3_channel_delivery *delivery = delivery_for(n, p, &storage);
  qa_q3_netchannel *channel = qa_q3_server_connection_channel(p->connection);
  while (qa_q3_netchannel_has_unsent_fragments(channel))
    qa_q3_server_connection_transmit_next_fragment(p->connection, delivery);
  const qa_q3_server_rate *rate = n->host.rate(n->host.ctx, p->player);
  qa_q3_server_connection_send_snapshot(p->connection, n->server_flags, rate, delivery,
      append_nothing, NULL);
  while (qa_q3_netchannel_has_unsent_fragments(channel))
    qa_q3_server_connection_transmit_next_fragment(p->connection, delivery);
  qa_q3_server_connection_set_phase(p->connection, QA_Q3_SERVER_PHASE_ZOMBIE);
}
static void peer_free(peer *p) {
  qa_q3_server_download_destroy(p->download);
  qa_q3_server_connection_destroy(p->connection);
  qa_q3_server_snapshot_history_destroy(p->snapshots);
  free(p->baselines);
  qa_net_address_destroy(&p->remote);
  qa_q3_mirror_address_destroy(&p->remote_q3);
  free(p->userinfo);
  free(p->binding);
  free(p);
}
static void retire_peer(qa_app_q3_server_network *n, peer *p) {
  size_t kept = 0;
  for (size_t i = 0; i < n->overflow_count; i++)
    if (n->overflowed[i] != p) n->overflowed[kept++] = n->overflowed[i];
  n->overflow_count = kept;
  if (!n->in_operation) { peer_free(p); return; }
  if (n->retired_count == n->retired_cap) {
    n->retired_cap = n->retired_cap ? n->retired_cap * 2 : 8;
    n->retired = realloc(n->retired, n->retired_cap * sizeof *n->retired);
    if (!n->retired) qa_range_error("out of memory");
  }
  n->retired[n->retired_count++] = p;
}
static void release_retired(qa_app_q3_server_network *n) {
  for (size_t i = 0; i < n->retired_count; i++) peer_free(n->retired[i]);
  n->retired_count = 0;
}
/* TS q3GameCallback(() => host.<call>(...)) thunks. */
typedef struct game_thunk {
  qa_app_q3_server_host *host; qa_app_q3_player player; const char *text;
  const char *const *args; size_t count; const void *command; double sequence;
  const void *request; qa_app_q3_admission admission; const void *accepted; qa_client_id client;
  const qa_app_q3_source_round_binding *round;
} game_thunk;
static void thunk_disconnect(void *ctx) { game_thunk *t = ctx; t->host->disconnect(t->host->ctx, t->player, t->text); }
static void thunk_userinfo(void *ctx) { game_thunk *t = ctx; t->host->userinfo(t->host->ctx, t->player, t->text); }
static void thunk_command(void *ctx) { game_thunk *t = ctx; t->host->command(t->host->ctx, t->player, t->text, t->args, t->count); }
static void thunk_begin(void *ctx) { game_thunk *t = ctx; t->host->begin(t->host->ctx, t->player, t->command); }
static void thunk_input(void *ctx) { game_thunk *t = ctx; t->accepted = t->host->input(t->host->ctx, t->player, t->command, t->sequence); }
static void thunk_admit(void *ctx) { game_thunk *t = ctx; t->admission = t->host->admit(t->host->ctx, t->request); }
static void thunk_reconnect(void *ctx) { game_thunk *t = ctx; t->admission = t->round->reconnect(t->round->ctx, t->client, t->text, t->command); }
static void game_disconnect_body(void *ctx) { qa_app_q3_game_call(thunk_disconnect, ctx); }
static void game_disconnect(qa_app_q3_server_host *host, qa_app_q3_player player, const char *reason) {
  game_thunk t = {0}; t.host = host; t.player = player; t.text = reason;
  qa_app_q3_game_call(thunk_disconnect, &t);
}
static void game_userinfo(qa_app_q3_server_host *host, qa_app_q3_player player, const char *value) {
  game_thunk t = {0}; t.host = host; t.player = player; t.text = value;
  qa_app_q3_game_call(thunk_userinfo, &t);
}
typedef struct queue_disconnect_scope { qa_app_q3_server_network *n; peer *p; const char *reason; } queue_disconnect_scope;
static void queue_disconnect_body(void *ctx) {
  queue_disconnect_scope *scope = ctx;
  qa_q3_server_download_close(scope->p->download);
  queue_disconnect(scope->n, scope->p, scope->reason);
}
static bool disconnect_client(qa_app_q3_server_network *n, qa_client_id client, const char *reason) {
  peer *p = NULL;
  for (size_t i = 0; i < n->peer_count; i++)
    if (client_equal(n->peers[i]->player.client, client)) p = n->peers[i];
  if (!p) return false;
  qa_app_q3_server_host host = n->host;
  for (size_t i = 0; i < n->peer_count; i++)
    if (n->peers[i] == p) {
      memmove(n->peers + i, n->peers + i + 1, (n->peer_count - i - 1) * sizeof *n->peers);
      n->peer_count--;
      break;
    }
  qa_q3_server_admission_disconnect(n->admission, &p->remote_q3);
  size_t kept = 0;
  for (size_t i = 0; i < n->pending_count; i++) {
    const qa_actor_command *command = n->pending[i];
    if (!qa_same_actor(command->actor, p->player.actor)) n->pending[kept++] = n->pending[i];
  }
  n->pending_count = kept;
  /* try { download.close(); queueDisconnect() } finally { q3GameCallback(host.disconnect) };
   * a raise from the finally supersedes the body's. */
  queue_disconnect_scope scope = { n, p, reason };
  qa_caught_error caught, final_caught;
  bool ok = qa_try(queue_disconnect_body, &scope, &caught);
  game_thunk t = {0}; t.host = &host; t.player = p->player; t.text = reason;
  bool final_ok = qa_try(game_disconnect_body, &t, &final_caught);
  retire_peer(n, p);
  if (!final_ok) qa_rethrow(&final_caught);
  if (!ok) qa_rethrow(&caught);
  return true;
}
static void disconnect_overflowed(qa_app_q3_server_network *n) {
  while (n->overflow_count > 0) {
    peer *p = n->overflowed[--n->overflow_count];
    if (!n->ended && peer_at(n, p->slot) == p)
      disconnect_client(n, p->player.client, "Server command overflow");
  }
}
static void input_peer(qa_app_q3_server_network *n, peer *p, const qa_q3_wire_user_command *command) {
  qa_app_q3_server_host host = n->host;
  double server_id = n->server_id;
  game_thunk t = {0}; t.host = &host; t.player = p->player; t.command = command; t.sequence = p->sequence++;
  qa_app_q3_game_call(thunk_input, &t);
  const void *accepted = t.accepted;
  assert_world(n, &host, server_id);
  if (peer_at(n, p->slot) == p
      && qa_q3_server_connection_get_phase(p->connection) != QA_Q3_SERVER_PHASE_ZOMBIE
      && accepted) {
    if (n->pending_count == n->pending_cap) {
      n->pending_cap = n->pending_cap ? n->pending_cap * 2 : 32;
      n->pending = realloc(n->pending, n->pending_cap * sizeof *n->pending);
      if (!n->pending) qa_range_error("out of memory");
    }
    n->pending[n->pending_count++] = accepted;
  }
}
typedef struct binding_ctx { qa_app_q3_server_network *n; peer *p; } binding_ctx;
static void binding_assert(void *ctx) {
  binding_ctx *binding = ctx;
  assert_peer(binding->n, binding->p, &binding->n->host, binding->n->server_id);
}
static int64_t binding_server_id(void *ctx) { return (int64_t)((binding_ctx *)ctx)->n->server_id; }
static int64_t binding_restarted_id(void *ctx) { return (int64_t)((binding_ctx *)ctx)->n->restarted_server_id; }
static int64_t binding_checksum(void *ctx) { return ((binding_ctx *)ctx)->n->checksum_feed; }
static bool binding_pure(void *ctx) {
  qa_app_q3_server_network *n = ((binding_ctx *)ctx)->n;
  const qa_q3_pure_server *pure = n->host.pure(n->host.ctx, n->server_id, &n->restarted_server_id);
  return pure && pure->enabled;
}
static double binding_time(void *ctx) {
  qa_app_q3_server_network *n = ((binding_ctx *)ctx)->n;
  return n->host.time(n->host.ctx);
}
static bool binding_false(void *ctx) { (void)ctx; return false; }
static bool binding_flood(void *ctx) {
  qa_app_q3_server_network *n = ((binding_ctx *)ctx)->n;
  if (!n->host.has_admission || !n->host.admission.flood_protect) return true;
  return n->host.admission.flood_protect(n->host.admission.ctx);
}
static const char *binding_download_name(void *ctx) {
  return qa_q3_server_download_name(((binding_ctx *)ctx)->p->download);
}
static void binding_print(void *ctx, const char *text) { host_print(ctx, text); }
static void binding_think(void *ctx, const qa_q3_wire_user_command *command) {
  binding_ctx *binding = ctx;
  input_peer(binding->n, binding->p, command);
}
static void binding_enter_world(void *ctx, const qa_q3_wire_user_command *command) {
  binding_ctx *binding = ctx;
  qa_app_q3_server_network *n = binding->n;
  qa_app_q3_server_host host = n->host;
  double server_id = n->server_id;
  if (!host.begin) input_peer(n, binding->p, command);
  else {
    game_thunk t = {0}; t.host = &host; t.player = binding->p->player; t.command = command;
    qa_app_q3_game_call(thunk_begin, &t);
    assert_world(n, &host, server_id);
  }
}
static void binding_resend(void *ctx) {
  binding_ctx *binding = ctx;
  gamestate(binding->n, binding->p);
}
static void binding_drop(void *ctx, const char *reason) {
  binding_ctx *binding = ctx;
  disconnect_client(binding->n, binding->p->player.client, reason);
}
static void pure_rejected_snapshot(void *ctx) {
  binding_ctx *binding = ctx;
  snapshot_peer(binding->n, binding->p);
}
static void tokens_cleanup(void *tokens) { qa_command_tokens_destroy(tokens); }
static void check_download_body(void *ctx) { qa_q3_check_download_name(ctx); }
static bool binding_command(void *ctx, const qa_q3_reliable_command *command, bool client_ok) {
  binding_ctx *binding = ctx;
  qa_app_q3_server_network *n = binding->n;
  peer *p = binding->p;
  qa_app_q3_server_host host = n->host;
  double server_id = n->server_id;
  qa_command_tokens tokens;
  if (!qa_tokenize_command(command->text, QA_COMMAND_DIALECT_Q3, QA_COMMAND_TEXT_SOURCE, &tokens))
    qa_range_error("out of memory");
  const char *name = tokens.argc ? tokens.argv[0] : "";
  bool result = true;
  if (strcmp(name, "disconnect") == 0) {
    qa_command_tokens_destroy(&tokens);
    disconnect_client(n, p->player.client, "disconnected");
    return false;
  }
  if (strcmp(name, "cp") == 0) {
    const qa_q3_pure_server *pure = host.pure(host.ctx, server_id, &n->restarted_server_id);
    qa_q3_server_connection_verify_pure(p->connection, pure,
        (const char *const *)tokens.argv, tokens.argc, pure_rejected_snapshot, binding);
    result = qa_q3_server_connection_get_phase(p->connection) != QA_Q3_SERVER_PHASE_ZOMBIE;
    qa_command_tokens_destroy(&tokens);
    return result;
  }
  if (strcmp(name, "vdr") == 0) {
    qa_q3_server_connection_set_pure_authentic(p->connection, false);
    qa_q3_server_connection_set_got_pure_command(p->connection, false);
    qa_command_tokens_destroy(&tokens);
    return true;
  }
  if (strcmp(name, "download") == 0) {
    char requested[64] = "";
    if (tokens.argc > 1) snprintf(requested, sizeof requested, "%s", tokens.argv[1]);
    qa_command_tokens_destroy(&tokens);
    /* try { checkQ3DownloadName(requested) } catch { disconnect 'Invalid download path' } */
    qa_caught_error caught;
    if (requested[0] != '\0' && !qa_try(check_download_body, requested, &caught)) {
      disconnect_client(n, p->player.client, "Invalid download path");
      return false;
    }
    qa_q3_server_download_begin(p->download, requested);
    return true;
  }
  if (strcmp(name, "nextdl") == 0) {
    int64_t block = qa_native_atoi(tokens.argc > 1 ? tokens.argv[1] : "");
    qa_command_tokens_destroy(&tokens);
    qa_q3_server_download_acknowledge(p->download, block, host.time(host.ctx));
    return qa_q3_server_connection_get_phase(p->connection) != QA_Q3_SERVER_PHASE_ZOMBIE;
  }
  if (strcmp(name, "stopdl") == 0) {
    qa_command_tokens_destroy(&tokens);
    qa_q3_server_download_close(p->download);
    return true;
  }
  if (strcmp(name, "donedl") == 0) {
    qa_command_tokens_destroy(&tokens);
    if (qa_q3_server_connection_get_phase(p->connection) != QA_Q3_SERVER_PHASE_ACTIVE)
      gamestate(n, p);
    return true;
  }
  if (strcmp(name, "userinfo") == 0) {
    /* TS q3PeerUserinfo((argv[1] ?? "").slice(0, 1023), peer.remote, print): the server stamps "ip".
       slice counts UTF-16 units; this truncates bytes, which agrees for ASCII userinfo. */
    char *requested = copy_text(tokens.argc > 1 ? tokens.argv[1] : "");
    if (strlen(requested) > 1023) requested[1023] = '\0';
    qa_command_tokens_destroy(&tokens);
    qa_cleanup requested_guard;
    qa_cleanup_push(&requested_guard, free, requested);
    char *stamped = qa_q3_peer_userinfo(requested, &p->remote_q3, host_print, n);
    qa_cleanup_pop(&requested_guard);
    free(requested);
    free(p->userinfo);
    p->userinfo = stamped;
    game_userinfo(&host, p->player, p->userinfo);
  } else if (client_ok) {
    /* The tokens outlive a raising game command only through this node. */
    qa_cleanup guard;
    qa_cleanup_push(&guard, tokens_cleanup, &tokens);
    game_thunk t = {0}; t.host = &host; t.player = p->player; t.text = name;
    t.args = (const char *const *)tokens.argv + 1; t.count = tokens.argc - 1;
    qa_app_q3_game_call(thunk_command, &t);
    qa_cleanup_pop(&guard);
    qa_command_tokens_destroy(&tokens);
  } else qa_command_tokens_destroy(&tokens);
  assert_world(n, &host, server_id);
  return peer_at(n, p->slot) == p
      && qa_q3_server_connection_get_phase(p->connection) != QA_Q3_SERVER_PHASE_ZOMBIE;
}
static void baseline_of(void *ctx, int64_t number, qa_q3_entity_state_record *out) {
  peer *p = ctx;
  for (size_t i = 0; i < p->baseline_count; i++)
    if (p->baselines[i].number == number) {
      *out = p->baselines[i].entity;
      return;
    }
  qa_q3_entity_state_record_init(out, 0);
}
static bool download_open(void *ctx, const char *name, qa_q3_download_read_file *out) {
  binding_ctx *binding = ctx;
  const void *file = binding->n->host.open_download(binding->n->host.ctx, name);
  if (!file) return false;
  *out = *(const qa_q3_download_read_file *)file;
  return true;
}
static bool download_enabled(void *ctx) {
  qa_app_q3_server_network *n = ((binding_ctx *)ctx)->n;
  return n->host.downloads_enabled(n->host.ctx);
}
static bool download_pure(void *ctx) { return binding_pure(ctx); }
static char *admit_request(void *ctx, const qa_q3_accepted_connect *request) {
  qa_app_q3_server_network *n = ctx;
  qa_app_q3_server_host host = n->host;
  double server_id = n->server_id;
  peer *previous = peer_at(n, request->slot);
  if (previous) disconnect_client(n, previous->player.client, "reconnected");
  size_t same_address = 0;
  for (size_t i = 0; i < n->peer_count; i++)
    if (n->peers[i]->slot != request->slot && qa_q3_mirror_same_address(&n->peers[i]->remote_q3, &request->address, false))
      same_address++;
  if (same_address >= 8) return copy_text("Too many connections from this address");
  assert_world(n, &host, server_id);
  game_thunk admit = {0}; admit.host = &host; admit.request = request;
  qa_app_q3_game_call(thunk_admit, &admit);
  qa_app_q3_admission admitted = admit.admission;
  if (n->ended || n->host.ctx != host.ctx || n->server_id != server_id) {
    if (admitted.accepted) game_disconnect(&host, admitted.player, "Server changed during admission");
    return copy_text("Server changed during admission");
  }
  if (!admitted.accepted) return copy_text(admitted.reason ? admitted.reason : "rejected");
  peer *p = calloc(1, sizeof *p);
  if (!p) qa_range_error("out of memory");
  p->slot = request->slot;
  p->player = admitted.player;
  p->last_received = n->now;
  p->connected_at = n->now;
  p->userinfo = copy_text(request->userinfo ? request->userinfo : "");
  if (!qa_q3_mirror_address_copy(&p->remote_q3, &request->address)) qa_range_error("out of memory");
  q3_to_net(&request->address, &p->remote);
  const qa_q3_gamestate *state = host.game_state(host.ctx, admitted.player, n->server_id);
  size_t count = 0;
  for (size_t i = 0; i < state->entry_count; i++)
    if (state->entries[i].kind == QA_Q3_GS_BASELINE) count++;
  p->baselines = xmalloc(count * sizeof *p->baselines);
  for (size_t i = 0; i < state->entry_count; i++)
    if (state->entries[i].kind == QA_Q3_GS_BASELINE) {
      p->baselines[p->baseline_count].number = state->entries[i].number;
      p->baselines[p->baseline_count].entity = state->entries[i].entity;
      p->baseline_count++;
    }
  qa_q3_product product = host.product ? *(const qa_q3_product *)host.product : QA_Q3_PRODUCT_BASEQ3;
  if (!qa_q3_server_snapshot_history_init(&p->snapshots, n->entities, product, baseline_of, p))
    qa_range_error("out of memory");
  binding_ctx *binding = xmalloc(sizeof *binding);
  binding->n = n;
  binding->p = p;
  p->binding = binding;
  qa_q3_download_server_bindings download_bindings = {0};
  download_bindings.open = download_open;
  download_bindings.enabled = download_enabled;
  download_bindings.pure = download_pure;
  download_bindings.drop = binding_drop;
  download_bindings.print = host_print;
  download_bindings.print_ctx = n;
  download_bindings.ctx = binding;
  if (!qa_q3_server_download_init(&p->download, &download_bindings)) qa_range_error("out of memory");
  qa_q3_server_bindings bindings = {0};
  bindings.assert_current = binding_assert;
  bindings.server_id = binding_server_id;
  bindings.restarted_server_id = binding_restarted_id;
  bindings.checksum_feed = binding_checksum;
  bindings.pure = binding_pure;
  bindings.debug_build = false;
  bindings.time = binding_time;
  bindings.client_running = binding_false;
  bindings.flood_protect = binding_flood;
  bindings.download_name = binding_download_name;
  bindings.command = binding_command;
  bindings.enter_world = binding_enter_world;
  bindings.think = binding_think;
  bindings.resend_gamestate = binding_resend;
  bindings.drop = binding_drop;
  bindings.print = binding_print;
  bindings.print_ctx = n;
  bindings.ctx = binding;
  qa_q3_connection_identity identity = {0};
  identity.client = admitted.player.client;
  if (!qa_q3_server_connection_init(&p->connection, identity, request->challenge,
          request->qport, p->snapshots, &bindings))
    qa_range_error("out of memory");
  if (n->peer_count == n->peer_cap) {
    n->peer_cap = n->peer_cap ? n->peer_cap * 2 : 16;
    n->peers = realloc(n->peers, n->peer_cap * sizeof *n->peers);
    if (!n->peers) qa_range_error("out of memory");
  }
  n->peers[n->peer_count++] = p;
  return NULL;
}
static bool adm_enabled(void *ctx) {
  qa_app_q3_server_network *n = ctx;
  if (n->ended) return false;
  if (!n->host.has_admission || !n->host.admission.enabled) return true;
  return n->host.admission.enabled(n->host.admission.ctx);
}
static int64_t adm_private_clients(void *ctx) {
  qa_app_q3_server_network *n = ctx;
  if (!n->host.has_admission || !n->host.admission.private_clients) return 0;
  return n->host.admission.private_clients(n->host.admission.ctx);
}
static const char *adm_private_password(void *ctx) {
  qa_app_q3_server_network *n = ctx;
  if (!n->host.has_admission || !n->host.admission.private_password) return "";
  return n->host.admission.private_password(n->host.admission.ctx);
}
static double adm_reconnect_limit(void *ctx) {
  qa_app_q3_server_network *n = ctx;
  if (!n->host.has_admission || !n->host.admission.reconnect_limit_seconds) return 3;
  return n->host.admission.reconnect_limit_seconds(n->host.admission.ctx);
}
static double adm_minimum_ping(void *ctx) {
  qa_app_q3_server_network *n = ctx;
  if (!n->host.has_admission || !n->host.admission.minimum_ping) return 0;
  return n->host.admission.minimum_ping(n->host.admission.ctx);
}
static double adm_maximum_ping(void *ctx) {
  qa_app_q3_server_network *n = ctx;
  if (!n->host.has_admission || !n->host.admission.maximum_ping) return 0;
  return n->host.admission.maximum_ping(n->host.admission.ctx);
}
static bool adm_authorize_address(void *ctx, qa_q3_address *out) {
  qa_app_q3_server_network *n = ctx;
  return qa_q3_server_authorization_get_address(n->authorization, out);
}
static bool adm_demo_restricted(void *ctx) {
  qa_app_q3_server_network *n = ctx;
  if (!n->host.has_admission || !n->host.admission.demo_restricted) return false;
  return n->host.admission.demo_restricted(n->host.admission.ctx);
}
static bool adm_is_lan(void *ctx, const qa_q3_address *address) {
  (void)ctx;
  return qa_q3_is_lan_address(address);
}
static double adm_random(void *ctx) {
  qa_app_q3_server_network *n = ctx;
  return n->options.random(n->options.random_ctx);
}
static void adm_authorize(void *ctx, const qa_q3_challenge *challenge) {
  qa_app_q3_server_network *n = ctx;
  qa_q3_server_authorization_request(n->authorization, challenge);
}
static void adm_send(void *ctx, const qa_q3_address *address, const uint8_t *packet, size_t len) {
  send_q3(ctx, address, packet, len);
}
static void adm_drop_bot(void *ctx, int64_t slot) {
  (void)ctx;
  (void)slot;
  qa_raise("Error", 0, 0, "Native Q3 admission cannot evict an application bot");
}
static void adm_query(void *ctx, const qa_q3_address *from, qa_q3_connectionless_packet *packet) {
  qa_app_q3_server_network *n = ctx;
  if (strcmp(packet->command, "getinfo") == 0 || strcmp(packet->command, "getstatus") == 0) {
    const char *response = n->host.status(n->host.ctx,
        packet->argument_count ? packet->arguments[0] : "",
        strcmp(packet->command, "getstatus") == 0);
    if (response) {
      uint8_t *bytes = NULL;
      size_t length = 0;
      if (!qa_q3_encode_connectionless_text(response, &bytes, &length)) qa_range_error("out of memory");
      send_q3(n, from, bytes, length);
      free(bytes);
    }
  }
}
static const char *auth_game_directory(void *ctx) {
  qa_app_q3_server_network *n = ctx;
  if (!n->host.has_admission || !n->host.admission.game_directory) return "";
  return n->host.admission.game_directory(n->host.admission.ctx);
}
static const char *auth_strict(void *ctx) {
  qa_app_q3_server_network *n = ctx;
  if (!n->host.has_admission || !n->host.admission.strict_auth) return "1";
  return n->host.admission.strict_auth(n->host.admission.ctx);
}
static bool auth_enabled(void *ctx) { return !((qa_app_q3_server_network *)ctx)->ended; }
static void auth_send(void *ctx, const qa_q3_address *address, const uint8_t *packet, size_t len) {
  send_q3(ctx, address, packet, len);
}
static bool auth_resolve(void *ctx, qa_q3_mirror_ipv4_address *out, char *err, size_t err_len) {
  qa_app_q3_server_network *n = ctx;
  if (!n->options.resolve_authorization) return false;
  return n->options.resolve_authorization(n->options.resolve_ctx, out, err, err_len);
}
static const char *rcon_password(void *ctx) {
  qa_app_q3_server_network *n = ctx;
  const qa_server_administration *admin = n->host.administration;
  return admin && admin->rcon_password ? admin->rcon_password(admin->ctx) : "";
}
static void rcon_execute(void *ctx, const char *command, qa_rcon_output_fn output, void *output_ctx) {
  qa_app_q3_server_network *n = ctx;
  const qa_server_administration *admin = n->host.administration;
  if (!admin) qa_raise("Error", 0, 0, "Server administration is unavailable");
  admin->execute(admin->ctx, command, output, output_ctx);
}
static void rcon_reply(void *ctx, const qa_net_address *to, const char *text) {
  qa_app_q3_server_network *n = ctx;
  size_t total = strlen(text) + 8;
  char *line = xmalloc(total);
  snprintf(line, total, "print\n%s", text);
  uint8_t *bytes = NULL;
  size_t length = 0;
  if (!qa_q3_encode_connectionless_text(line, &bytes, &length)) qa_range_error("out of memory");
  free(line);
  n->options.transport->send(n->options.transport->ctx, to, bytes, length);
  free(bytes);
}
static bool sender_send(void *ctx, const qa_net_address *to, const uint8_t *bytes, size_t length) {
  qa_app_q3_server_network *n = ctx;
  return n->options.transport->send(n->options.transport->ctx, to, bytes, length);
}
qa_app_q3_server_network *qa_app_q3_server_network_create(qa_app_q3_server_network_options options) {
  qa_app_q3_server_network *n = calloc(1, sizeof *n);
  if (!n) return NULL;
  n->options = options;
  n->host = options.host;
  n->server_id = 1;
  n->restarted_server_id = 1;
  qa_connectionless_gate_init(&n->gate);
  n->diagnostic_tokens = 20;
  n->diagnostic_last = monotonic_ms();
  double first = options.random(options.random_ctx);
  double second = options.random(options.random_ctx);
  n->checksum_feed = checksum_feed_of(first, second);
  require_supported(&n->host);
  qa_rcon_host rcon_host = {n, rcon_password, rcon_execute, rcon_reply};
  qa_rcon_service_init(&n->rcon, rcon_host, true, 1008); /* TS RconService default outputBytes */
  qa_discovery_wire wire;
  if (!qa_q3_discovery_wire_init(&wire, &n->discovery_wire, NULL, 0, 68)) { free(n); return NULL; }
  qa_packet_sender sender = {n, sender_send};
  qa_master_heartbeat_service_init(&n->heartbeat_service, wire, sender, 300000); /* TS MasterHeartbeat default interval */
  qa_net_address address;
  bool has_address = options.transport->get_address(options.transport->ctx, &address) != 0;
  if (has_address && address.kind == QA_NET_IPX)
    n->host.print(n->host.ctx, "Q3 IPX transport supports LAN play; IPv4 master advertisement is unavailable.\n");
  if (has_address) qa_net_address_destroy(&address);
  qa_q3_server_authorization_options auth_options = {0};
  auth_options.resolve = options.resolve_authorization ? auth_resolve : NULL;
  auth_options.enabled = auth_enabled;
  auth_options.game_directory = auth_game_directory;
  auth_options.strict_auth = auth_strict;
  auth_options.send = auth_send;
  auth_options.print = host_print;
  auth_options.print_ctx = n;
  auth_options.ctx = n;
  if (!qa_q3_server_authorization_init(&n->authorization, &auth_options)) { free(n); return NULL; }
  if (!qa_q3_snapshot_entities_init(&n->entities, 32768)) { free(n); return NULL; }
  qa_q3_server_admission_bindings bindings = {0};
  bindings.enabled = adm_enabled;
  bindings.slots = slots_of;
  bindings.private_clients = adm_private_clients;
  bindings.private_password = adm_private_password;
  bindings.reconnect_limit_seconds = adm_reconnect_limit;
  bindings.minimum_ping = adm_minimum_ping;
  bindings.maximum_ping = adm_maximum_ping;
  bindings.authorize_address = adm_authorize_address;
  bindings.demo_restricted = adm_demo_restricted;
  bindings.is_lan = adm_is_lan;
  bindings.random = adm_random;
  bindings.authorize = adm_authorize;
  bindings.send = adm_send;
  bindings.admit = admit_request;
  bindings.drop_bot = adm_drop_bot;
  bindings.print = host_print;
  bindings.query = adm_query;
  bindings.ctx = n;
  bindings.print_ctx = n;
  if (!qa_q3_server_admission_init(&n->admission, &bindings)) { free(n); return NULL; }
  return n;
}
void qa_app_q3_server_network_destroy(qa_app_q3_server_network *network) {
  if (!network) return;
  for (size_t i = 0; i < network->peer_count; i++) peer_free(network->peers[i]);
  free(network->peers);
  release_retired(network);
  free(network->retired);
  qa_connectionless_gate_destroy(&network->gate);
  qa_q3_server_admission_destroy(network->admission);
  qa_q3_server_authorization_destroy(network->authorization);
  qa_q3_snapshot_entities_destroy(network->entities);
  qa_q3_discovery_wire_state_destroy(&network->discovery_wire);
  qa_rcon_service_destroy(&network->rcon);
  free(network->pending);
  free(network->overflowed);
  free(network->slot_cache);
  free(network->poll_result);
  free(network);
}
void qa_app_q3_server_network_heartbeat(qa_app_q3_server_network *n, double now) {
  if (n->ended) return;
  qa_net_address address;
  if (!n->options.transport->get_address(n->options.transport->ctx, &address)) return;
  bool ipx = address.kind == QA_NET_IPX;
  qa_net_address_destroy(&address);
  if (ipx) return;
  const qa_server_administration *admin = n->host.administration;
  size_t count = 0;
  const qa_net_address *masters = admin && admin->masters ? admin->masters(admin->ctx, &count) : NULL;
  qa_master_heartbeat_service_send(&n->heartbeat_service, masters, count, trunc(now), true, true);
}
qa_app_q3_recording_source qa_app_q3_server_network_recording_source(qa_app_q3_server_network *n) {
  if (n->ended) qa_raise("Error", 0, 0, "Q3 recording source is retired");
  qa_app_q3_recording_source source = {&n->host, n->server_id, n->server_flags};
  return source;
}
bool qa_app_q3_server_network_address(qa_app_q3_server_network *n, qa_net_address *out) {
  return n->options.transport->get_address(n->options.transport->ctx, out) != 0;
}
qa_app_network_phase qa_app_q3_server_network_phase(const qa_app_q3_server_network *n) {
  return n->ended ? QA_APP_PHASE_CLOSED : QA_APP_PHASE_ACTIVE;
}
size_t qa_app_q3_server_network_clients(qa_app_q3_server_network *n,
    qa_app_q3_player *out, size_t capacity) {
  size_t count = 0;
  for (size_t i = 0; i < n->peer_count; i++) {
    if (count < capacity) out[count] = n->peers[i]->player;
    count++;
  }
  return count;
}
static void configstring_peer(qa_app_q3_server_network *n, peer *p, double index, const char *value) {
  char **commands = NULL;
  size_t count = 0;
  if (!qa_q3_configstring_commands((int64_t)index, value, &commands, &count))
    qa_range_error("out of memory");
  for (size_t i = 0; i < count; i++) {
    if (n->ended || peer_at(n, p->slot) != p) break;
    queue_reliable(n, p, commands[i]);
  }
  for (size_t i = 0; i < count; i++) free(commands[i]);
  free(commands);
}
static void broadcast_configstring(qa_app_q3_server_network *n, double index, const char *value) {
  for (size_t i = 0; i < n->peer_count; i++) {
    peer *p = n->peers[i];
    if (qa_q3_server_connection_get_phase(p->connection) == QA_Q3_SERVER_PHASE_CONNECTED) continue;
    configstring_peer(n, p, index, value);
  }
}
typedef struct prepare_ctx { qa_app_q3_server_network *n; } prepare_ctx;
static void prepare_configstring(void *ctx, double index, const char *value) {
  broadcast_configstring(((prepare_ctx *)ctx)->n, index, value);
}
/* TS operation(): one operation at a time; `finally { activeOperation = null }`.
 * Peers disconnected during the operation are freed once no frame can use them. */
static void run_operation(qa_app_q3_server_network *n, void (*fn)(void *ctx), void *ctx) {
  if (n->in_operation) qa_raise("Error", 0, 0, "Q3 network operation is already in progress");
  n->in_operation = true;
  qa_caught_error caught;
  bool ok = qa_try(fn, ctx, &caught);
  n->in_operation = false;
  release_retired(n);
  if (!ok) qa_rethrow(&caught);
}
static void event_cleanup(void *event) { qa_receive_event_destroy(event); }
static void connectionless_cleanup(void *packet) { qa_q3_connectionless_packet_destroy(packet); }
static void q3_address_cleanup(void *address) { qa_q3_mirror_address_destroy(address); }
static void payload_cleanup(void *result) { free(((qa_q3_channel_result *)result)->payload); ((qa_q3_channel_result *)result)->payload = NULL; }
typedef struct packet_scope {
  qa_app_q3_server_network *n; qa_receive_event *event;
  qa_app_q3_server_host host; double server_id;
  peer *owner;
} packet_scope;
/* One received datagram: the body of TS pollPackets' per-packet try. */
static void packet_body(void *ctx) {
  packet_scope *scope = ctx;
  qa_app_q3_server_network *n = scope->n;
  qa_receive_event *event = scope->event;
  const qa_server_administration *admin = scope->host.administration;
  const uint8_t *bytes = event->payload;
  if (event->length >= 4 && bytes[0] == 255 && bytes[1] == 255 && bytes[2] == 255 && bytes[3] == 255) {
    if (!qa_connectionless_gate_allow(&n->gate, &event->from, n->now)) return;
    qa_q3_connectionless_packet packet;
    if (!qa_q3_decode_connectionless(bytes, event->length, QA_Q3_CONNLESS_SERVER, NULL, &packet))
      qa_range_error("out of memory");
    qa_cleanup packet_guard;
    qa_cleanup_push(&packet_guard, connectionless_cleanup, &packet);
    if (strcmp(packet.command, "rcon") == 0) {
      char *command = qa_q3_rcon_command(packet.line);
      if (!command) qa_range_error("out of memory");
      qa_cleanup command_guard;
      qa_cleanup_push(&command_guard, free, command);
      const char *result = qa_rcon_service_handle(&n->rcon, &event->from,
          packet.argument_count ? packet.arguments[0] : "", command, n->now);
      if (admin && admin->record) admin->record(admin->ctx, &event->from, result);
      qa_cleanup_pop(&command_guard);
      free(command);
    } else {
      qa_q3_address from;
      net_to_q3(&event->from, &from);
      qa_cleanup from_guard;
      qa_cleanup_push(&from_guard, q3_address_cleanup, &from);
      qa_q3_server_admission_receive(n->admission, &from, bytes, event->length, n->now);
      qa_cleanup_pop(&from_guard);
      qa_q3_mirror_address_destroy(&from);
    }
    qa_cleanup_pop(&packet_guard);
    qa_q3_connectionless_packet_destroy(&packet);
    return;
  }
  qa_q3_address from;
  net_to_q3(&event->from, &from);
  qa_cleanup from_guard;
  qa_cleanup_push(&from_guard, q3_address_cleanup, &from);
  size_t slot_count = 0;
  const qa_q3_admission_slot *slots = slots_of(n, &slot_count);
  int64_t slot = qa_q3_route_sequenced_packet(&from, bytes, event->length, slots, slot_count);
  peer *p = slot < 0 ? NULL : peer_at(n, slot);
  scope->owner = p;
  if (p) {
    qa_q3_channel_result result;
    memset(&result, 0, sizeof result);
    qa_cleanup result_guard;
    qa_cleanup_push(&result_guard, payload_cleanup, &result);
    qa_q3_server_connection_receive_datagram(p->connection, bytes, event->length, &result);
    qa_cleanup_pop(&result_guard);
    free(result.payload);
    if (peer_at(n, p->slot) == p) {
      assert_peer(n, p, &scope->host, scope->server_id);
      if (result.kind == QA_Q3_CHANNEL_ACCEPTED) {
        qa_net_address_destroy(&p->remote);
        qa_q3_mirror_address_destroy(&p->remote_q3);
        if (!qa_q3_mirror_address_copy(&p->remote_q3, &from)) qa_range_error("out of memory");
        q3_to_net(&from, &p->remote);
        p->last_received = n->now;
        if (qa_q3_server_connection_get_phase(p->connection) == QA_Q3_SERVER_PHASE_CONNECTED)
          gamestate(n, p);
      }
    }
  }
  qa_cleanup_pop(&from_guard);
  qa_q3_mirror_address_destroy(&from);
}
typedef struct poll_scope { qa_app_q3_server_network *n; double now; size_t count; bool returned; } poll_scope;
static void poll_body(void *ctx) {
  poll_scope *scope = ctx;
  qa_app_q3_server_network *n = scope->n;
  if (n->ended) return;
  n->now = trunc(scope->now);
  qa_app_q3_server_host host = n->host;
  double server_id = n->server_id;
  prepare_ctx prepare = {n};
  host.prepare(host.ctx, (double)n->checksum_feed, server_id, prepare_configstring, &prepare);
  if (n->ended) return;
  assert_world(n, &host, server_id);
  disconnect_overflowed(n);
  if (n->ended) return;
  if (n->changed_world) {
    n->changed_world = false;
    for (size_t i = 0; i < n->peer_count; i++) gamestate(n, n->peers[i]);
  }
  for (;;) {
    qa_receive_event event;
    if (!n->options.transport->poll(n->options.transport->ctx, &event)) break;
    qa_cleanup event_guard;
    qa_cleanup_push(&event_guard, event_cleanup, &event);
    bool stop = n->ended;
    const qa_server_administration *admin = n->host.administration;
    if (!stop && event.kind == QA_RECV_ERROR) {
      if (event.error) n->host.print(n->host.ctx, event.error);
    } else if (!stop && event.kind == QA_RECV_PACKET
        && (event.from.kind == QA_NET_IPV4 || event.from.kind == QA_NET_LOOPBACK || event.from.kind == QA_NET_IPX)
        && !(admin && admin->rejects && admin->rejects(admin->ctx, &event.from))) {
      packet_scope packet = { n, &event, host, server_id, NULL };
      qa_caught_error caught;
      if (!qa_try(packet_body, &packet, &caught)) {
        if (n->ended) stop = true;
        else {
          char text[260];
          printable_peer_text(caught.message, text, sizeof text);
          if (diagnostic_allow(n)) n->host.print(n->host.ctx, text);
          /* A game fault raised by one peer's packet costs that peer its connection. If the game
           * itself is broken, the disconnect callback fails too and the application's frame
           * boundary takes over. */
          if (strcmp(caught.name, QA_APP_Q3_GAME_CALLBACK_ERROR) == 0 && packet.owner)
            disconnect_client(n, packet.owner->player.client, "Server game error");
        }
      }
    }
    qa_cleanup_pop(&event_guard);
    qa_receive_event_destroy(&event);
    if (stop) break;
  }
  if (n->ended) { n->pending_count = 0; return; }
  if (n->poll_cap < n->pending_count) {
    n->poll_result = realloc(n->poll_result, n->pending_count * sizeof *n->poll_result);
    if (!n->poll_result) qa_range_error("out of memory");
    n->poll_cap = n->pending_count;
  }
  for (size_t i = 0; i < n->pending_count; i++) n->poll_result[i] = n->pending[i];
  scope->count = n->pending_count;
  scope->returned = true;
  n->pending_count = 0;
}
const void *const *qa_app_q3_server_network_poll(qa_app_q3_server_network *n,
    double now_milliseconds, size_t *out_count) {
  *out_count = 0;
  poll_scope scope = { n, now_milliseconds, 0, false };
  run_operation(n, poll_body, &scope);
  if (!scope.returned) return NULL;
  *out_count = scope.count;
  return n->poll_result;
}
void qa_app_q3_server_network_submit(qa_app_q3_server_network *n,
    const void *const *commands, size_t count, double now) {
  (void)n; (void)commands; (void)count; (void)now;
}
static void receive_events(qa_app_q3_server_network *n, const void *const *events, size_t count) {
  for (size_t i = 0; i < count; i++) {
    const qa_app_sim_presentation_event *item = events[i];
    if (!item->kind || strcmp(item->kind, "q3-source") != 0) continue;
    const qa_app_q3_source_event *event = item->q3_source;
    for (size_t j = 0; j < n->peer_count;) {
      if (n->ended) return;
      peer *p = n->peers[j];
      if (item->has_recipient && !qa_same_actor(item->recipient, p->player.actor)) { j++; continue; }
      if (event->kind == QA_APP_Q3_EVENT_DROP_CLIENT && event->client == p->player.source_entity) {
        disconnect_client(n, p->player.client, event->reason ? event->reason : "");
        continue;
      }
      if (event->kind == QA_APP_Q3_EVENT_SERVER_COMMAND
          && (event->client == -1 || event->client == p->player.source_entity))
        queue_reliable(n, p, event->text ? event->text : "");
      else if (event->kind == QA_APP_Q3_EVENT_CONFIGSTRING
          && qa_q3_server_connection_get_phase(p->connection) != QA_Q3_SERVER_PHASE_CONNECTED)
        configstring_peer(n, p, event->index, event->value ? event->value : "");
      j++;
    }
  }
}
typedef struct publish_scope { qa_app_q3_server_network *n; const void *const *events; size_t count; double now; } publish_scope;
static void publish_body(void *ctx) {
  publish_scope *scope = ctx;
  qa_app_q3_server_network *n = scope->n;
  if (n->ended) return;
  n->now = trunc(scope->now);
  receive_events(n, scope->events, scope->count);
  disconnect_overflowed(n);
  if (n->ended) return;
  const qa_server_administration *admin = n->host.administration;
  size_t master_count = 0;
  const qa_net_address *masters = admin && admin->masters
      ? admin->masters(admin->ctx, &master_count) : NULL;
  if (master_count > 0) {
    qa_net_address address;
    if (n->options.transport->get_address(n->options.transport->ctx, &address)) {
      bool ipx = address.kind == QA_NET_IPX;
      qa_net_address_destroy(&address);
      if (!ipx) qa_master_heartbeat_service_send(&n->heartbeat_service, masters,
          master_count, n->now, true, false);
    }
  }
  double timeout = n->options.has_timeout_milliseconds ? n->options.timeout_milliseconds : 30000;
  for (size_t i = 0; i < n->peer_count;) {
    peer *p = n->peers[i];
    if (n->now - p->last_received > timeout) {
      disconnect_client(n, p->player.client, "timed out");
      continue;
    }
    i++;
  }
  for (size_t i = 0; i < n->peer_count; i++) {
    double left;
    double right;
    peer *p = n->peers[i];
    if (qa_q3_server_connection_get_phase(p->connection) != QA_Q3_SERVER_PHASE_CONNECTED
        && (left = n->host.time(n->host.ctx), right = (double)qa_q3_server_connection_next_snapshot_time(p->connection), left >= right))
      snapshot_peer(n, p);
  }
}
void qa_app_q3_server_network_publish(qa_app_q3_server_network *n,
    const void *output, const void *const *events, size_t count, double now_milliseconds) {
  (void)output; /* TS publishEvents ignores _output */
  publish_scope scope = { n, events, count, now_milliseconds };
  run_operation(n, publish_body, &scope);
}
static void output_drop_client(void *ctx, double slot, const char *reason) {
  qa_app_q3_server_network *n = ctx;
  for (size_t i = 0; i < n->peer_count; i++)
    if (n->peers[i]->player.source_entity == slot) {
      disconnect_client(n, n->peers[i]->player.client, reason);
      return;
    }
}
static void output_send_command(void *ctx, double slot, const char *text) {
  qa_app_q3_server_network *n = ctx;
  for (size_t i = 0; i < n->peer_count; i++) {
    if (n->ended) return;
    peer *p = n->peers[i];
    if (slot == -1 || p->player.source_entity == slot) queue_reliable(n, p, text);
  }
}
static void output_configstring(void *ctx, double index, const char *value) {
  broadcast_configstring(ctx, index, value);
}
qa_app_q3_guest_output qa_app_q3_server_network_game_output(qa_app_q3_server_network *n) {
  qa_app_q3_guest_output output = {n, output_drop_client, output_send_command, output_configstring};
  return output;
}
bool qa_app_q3_server_network_disconnect_client(qa_app_q3_server_network *n,
    qa_client_id client, const char *reason) {
  return disconnect_client(n, client, reason);
}
typedef struct change_world_scope {
  qa_app_q3_server_network *n; qa_app_q3_server_host host; const qa_app_q3_rejection *rejected; size_t count;
  bool *refused; qa_app_q3_player *carried; peer **carried_peers;
} change_world_scope;
static void change_world_body(void *ctx) {
  change_world_scope *scope = ctx;
  qa_app_q3_server_network *n = scope->n;
  qa_app_q3_server_host host = scope->host;
  require_supported(&host);
  bool *refused = scope->refused = xmalloc(n->peer_count * sizeof *refused);
  for (size_t i = 0; i < n->peer_count; i++) refused[i] = false;
  for (size_t i = 0; i < scope->count; i++) {
    peer *p = NULL;
    size_t index = 0;
    for (size_t j = 0; j < n->peer_count; j++)
      if (n->peers[j]->slot == (int64_t)scope->rejected[i].client.slot) { p = n->peers[j]; index = j; }
    if (!p || !client_equal(p->player.client, scope->rejected[i].client) || refused[index])
      qa_raise("Error", 0, 0, "Q3 map rejection does not identify one carried peer");
    refused[index] = true;
  }
  qa_app_q3_player *carried = scope->carried = xmalloc(n->peer_count * sizeof *carried);
  peer **carried_peers = scope->carried_peers = xmalloc(n->peer_count * sizeof *carried_peers);
  size_t carried_count = 0;
  for (size_t i = 0; i < n->peer_count; i++)
    if (!refused[i]) {
      carried_peers[carried_count] = n->peers[i];
      carried[carried_count] = host.carried_player(host.ctx, n->peers[i]->player.client);
      carried_count++;
    }
  n->host = host;
  n->server_id++;
  n->restarted_server_id = n->server_id;
  n->server_flags = n->server_flags == 0 ? 4 : 0;
  n->pending_count = 0;
  double first = n->options.random(n->options.random_ctx);
  double second = n->options.random(n->options.random_ctx);
  n->checksum_feed = checksum_feed_of(first, second);
  n->changed_world = true;
  for (size_t i = 0; i < scope->count; i++)
    disconnect_client(n, scope->rejected[i].client, scope->rejected[i].reason);
  for (size_t i = 0; i < carried_count; i++) {
    peer *p = carried_peers[i];
    p->player = carried[i];
    game_userinfo(&n->host, p->player, p->userinfo);
    assert_peer(n, p, &n->host, n->server_id);
    qa_q3_server_download_close(p->download);
    qa_q3_server_connection_set_delta_message(p->connection, -1);
    qa_q3_server_connection_set_phase(p->connection, QA_Q3_SERVER_PHASE_CONNECTED);
  }
}
static void change_world_operation(void *ctx) {
  change_world_scope *scope = ctx;
  run_operation(scope->n, change_world_body, scope);
}
void qa_app_q3_server_network_change_world(qa_app_q3_server_network *n,
    qa_app_q3_server_host host, const qa_app_q3_rejection *rejected, size_t count) {
  change_world_scope scope = { n, host, rejected, count, NULL, NULL, NULL };
  qa_caught_error caught;
  bool ok = qa_try(change_world_operation, &scope, &caught);
  free(scope.refused);
  free(scope.carried);
  free(scope.carried_peers);
  if (!ok) qa_rethrow(&caught);
}
typedef struct round_state {
  qa_app_q3_server_network *n;
  qa_app_q3_server_host host;
  double server_id;
  int phase; /* 0 reset, 1 bound, 2 closed */
  peer **peers; size_t peer_count;
  bool *pending; bool *reconnecting;
  qa_client_id *clients;
} round_state;
static void round_current(round_state *round) {
  if (round->phase == 2) qa_raise("Error", 0, 0, "Q3 source restart scope has ended");
  assert_world(round->n, &round->host, round->server_id);
}
static void round_bind(void *ctx) {
  round_state *round = ctx;
  round_current(round);
  if (round->phase != 0) qa_raise("Error", 0, 0, "Q3 network source round was already rebound");
  round->n->host.source_round.rebind(round->n->host.source_round.ctx);
  prepare_ctx prepare = {round->n};
  round->host.prepare(round->host.ctx, (double)round->n->checksum_feed, round->server_id,
      prepare_configstring, &prepare);
  round_current(round);
  round->phase = 1;
}
static void round_receive_events(void *ctx, const void *const *events, size_t count) {
  round_state *round = ctx;
  round_current(round);
  if (round->phase != 1) qa_raise("Error", 0, 0, "Bind the Q3 network source before delivering restart events");
  receive_events(round->n, events, count);
  round_current(round);
}
typedef struct reconnect_scope { round_state *round; size_t index; peer *p; qa_client_id client; } reconnect_scope;
static void round_reconnect_body(void *ctx) {
  reconnect_scope *scope = ctx;
  round_state *round = scope->round;
  peer *p = scope->p;
  qa_client_id client = scope->client;
  if (peer_at(round->n, p->slot) == p) {
    queue_reliable(round->n, p, "map_restart\n");
    round_current(round);
    if (peer_at(round->n, p->slot) == p) {
      qa_q3_wire_user_command last = qa_q3_server_connection_last_user_command(p->connection);
      game_thunk t = {0}; t.round = &round->n->host.source_round; t.client = client; t.text = p->userinfo; t.command = &last;
      qa_app_q3_game_call(thunk_reconnect, &t);
      qa_app_q3_admission admitted = t.admission;
      round_current(round);
      if (!admitted.accepted) disconnect_client(round->n, client, admitted.reason ? admitted.reason : "rejected");
      else {
        if (!client_equal(admitted.player.client, client))
          qa_raise("Error", 0, 0, "Q3 round admission changed client identity");
        p->player = admitted.player;
        qa_q3_server_connection_set_phase(p->connection, QA_Q3_SERVER_PHASE_ACTIVE);
        qa_q3_server_connection_set_delta_message(p->connection, -1);
        qa_q3_server_connection_set_next_snapshot_time(p->connection,
            (int64_t)round->host.time(round->host.ctx));
      }
    }
  }
  round->pending[scope->index] = false;
}
static bool round_reconnect(void *ctx, qa_client_id client) {
  round_state *round = ctx;
  round_current(round);
  if (round->phase != 1) qa_raise("Error", 0, 0, "Bind the Q3 network source before reconnecting");
  size_t index = round->peer_count;
  for (size_t i = 0; i < round->peer_count; i++)
    if (client_equal(round->peers[i]->player.client, client)) { index = i; break; }
  if (index == round->peer_count) return false;
  if (!round->pending[index] || round->reconnecting[index])
    qa_raise("Error", 0, 0, "Q3 network client was already reconnected");
  round->reconnecting[index] = true;
  /* try { ... } finally { reconnecting.delete(peer.slot) } */
  reconnect_scope scope = { round, index, round->peers[index], client };
  qa_caught_error caught;
  bool ok = qa_try(round_reconnect_body, &scope, &caught);
  round->reconnecting[index] = false;
  if (!ok) qa_rethrow(&caught);
  return true;
}
typedef struct restart_scope {
  qa_app_q3_server_network *n;
  void (*run)(void *ctx, qa_app_q3_network_round_restart *round); void *run_ctx;
  void (*on_mutation)(void *ctx); void *mutation_ctx;
  round_state state;
} restart_scope;
static void restart_try_body(void *ctx) {
  restart_scope *scope = ctx;
  qa_app_q3_server_network *n = scope->n;
  round_state *state = &scope->state;
  if (scope->on_mutation) scope->on_mutation(scope->mutation_ctx);
  state->host.prepare(state->host.ctx, (double)n->checksum_feed, state->server_id, NULL, NULL);
  round_current(state);
  qa_app_q3_network_round_restart round = {0};
  round.ctx = state;
  round.clients = state->clients;
  round.client_count = state->peer_count;
  round.snapshot_server_bit = n->server_flags;
  round.bind_source = round_bind;
  round.receive_events = round_receive_events;
  round.reconnect_client = round_reconnect;
  scope->run(scope->run_ctx, &round);
  round_current(state);
  bool all_done = true;
  for (size_t i = 0; i < state->peer_count; i++)
    if (state->pending[i]) all_done = false;
  if (state->phase != 1 || !all_done)
    qa_raise("Error", 0, 0, "Q3 source restart did not bind and reconnect every network client");
}
static void download_close_body(void *ctx) { qa_q3_server_download_close(ctx); }
static void transport_close_body(void *ctx) {
  qa_app_q3_server_network *n = ctx;
  n->options.transport->close(n->options.transport->ctx);
}
static void restart_body(void *ctx) {
  restart_scope *scope = ctx;
  qa_app_q3_server_network *n = scope->n;
  if (n->ended || !n->host.has_source_round)
    qa_raise("Error", 0, 0, "Q3 network host has no native source round restart");
  n->host.source_round.preflight(n->host.source_round.ctx);
  if (n->server_id == 2147483647.0) qa_raise("Error", 0, 0, "Q3 server id exhausted");
  round_state *state = &scope->state;
  state->n = n;
  state->peer_count = n->peer_count;
  state->peers = xmalloc(state->peer_count * sizeof *state->peers);
  state->pending = xmalloc(state->peer_count * sizeof *state->pending);
  state->reconnecting = xmalloc(state->peer_count * sizeof *state->reconnecting);
  state->clients = xmalloc(state->peer_count * sizeof *state->clients);
  for (size_t i = 0; i < state->peer_count; i++) {
    state->peers[i] = n->peers[i];
    state->pending[i] = true;
    state->reconnecting[i] = false;
    state->clients[i] = n->peers[i]->player.client;
  }
  n->server_id++;
  n->server_flags = n->server_flags == 0 ? 4 : 0;
  n->pending_count = 0;
  state->host = n->host;
  state->server_id = n->server_id;
  qa_caught_error caught;
  bool ok = qa_try(restart_try_body, scope, &caught);
  state->phase = 2; /* finally { lifecycle.phase = 'closed' } */
  if (ok) return;
  /* catch: retire the network, closing every download and the transport. */
  n->ended = true;
  n->pending_count = 0;
  size_t failures = 1;
  qa_caught_error failure;
  for (size_t i = 0; i < n->peer_count; i++)
    if (!qa_try(download_close_body, n->peers[i]->download, &failure)) failures++;
  while (n->peer_count > 0) retire_peer(n, n->peers[--n->peer_count]);
  if (!qa_try(transport_close_body, n, &failure)) failures++;
  if (failures > 1) qa_raise("AggregateError", 0, 0, "Q3 source restart and network retirement failed");
  qa_rethrow(&caught);
}
static void restart_operation(void *ctx) {
  restart_scope *scope = ctx;
  run_operation(scope->n, restart_body, scope);
}
void qa_app_q3_server_network_restart_source_round(qa_app_q3_server_network *n,
    void (*run)(void *ctx, qa_app_q3_network_round_restart *round), void *run_ctx,
    void (*on_mutation)(void *ctx), void *mutation_ctx) {
  restart_scope scope;
  memset(&scope, 0, sizeof scope);
  scope.n = n; scope.run = run; scope.run_ctx = run_ctx; scope.on_mutation = on_mutation; scope.mutation_ctx = mutation_ctx;
  qa_caught_error caught;
  bool ok = qa_try(restart_operation, &scope, &caught);
  free(scope.state.peers);
  free(scope.state.pending);
  free(scope.state.reconnecting);
  free(scope.state.clients);
  if (!ok) qa_rethrow(&caught);
}
typedef struct close_scope { qa_app_q3_server_network *n; qa_client_id client; } close_scope;
static void close_disconnect_body(void *ctx) {
  close_scope *scope = ctx;
  disconnect_client(scope->n, scope->client, "Server shutdown");
}
void qa_app_q3_server_network_close(qa_app_q3_server_network *n) {
  if (n->closing) return;
  n->closing = true;
  n->ended = true;
  /* closeOwned: each failure is collected; the transport closes regardless. */
  size_t failures = 0;
  qa_caught_error caught;
  while (n->peer_count > 0) {
    /* A failed disconnect has already removed the peer from the table. */
    close_scope scope = { n, n->peers[0]->player.client };
    if (!qa_try(close_disconnect_body, &scope, &caught)) failures++;
  }
  n->options.transport->close(n->options.transport->ctx);
  if (failures != 0) qa_raise("AggregateError", 0, 0, "Q3 network shutdown failed");
}
static qa_app_network_phase net_phase(void *ctx) { return qa_app_q3_server_network_phase(ctx); }
/* TS readonly wire = { kind: "source", protocol: { kind: "q3", version: 68 } } */
static qa_wire_selection net_wire(void *ctx) {
  (void)ctx;
  qa_wire_selection wire;
  memset(&wire, 0, sizeof wire);
  wire.protocol.kind = "q3";
  wire.protocol.version = 68;
  return wire;
}
static void net_heartbeat(void *ctx, double now) { qa_app_q3_server_network_heartbeat(ctx, now); }
static const void *const *net_poll(void *ctx, double now, size_t *out_count) {
  return qa_app_q3_server_network_poll(ctx, now, out_count);
}
static void net_submit(void *ctx, const void *const *commands, size_t count, double now) {
  qa_app_q3_server_network_submit(ctx, commands, count, now);
}
static void net_publish(void *ctx, const void *output, const void *const *events,
    size_t count, double now) {
  qa_app_q3_server_network_publish(ctx, output, events, count, now);
}
static void net_close(void *ctx) { qa_app_q3_server_network_close(ctx); }
qa_app_application_network qa_app_q3_server_network_as_network(qa_app_q3_server_network *network) {
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
