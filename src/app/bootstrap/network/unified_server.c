/* Port of src/app/bootstrap/network/unified-server.ts.
 * GPL-2.0-or-later. */
#include "qa/app/bootstrap/network/unified_server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/network/unified_event_codec.h"
#include <math.h>
#include <openssl/hmac.h>
#include <sys/random.h>
#include "qa/network/common/diagnostics.h"
#include "qa/network/common/movement_budget.h"
#include "qa/foundation/support.h"
#include "qa/network/unified/channel.h"
#include "qa/network/unified/packet.h"
#include "qa/network/common/js_number.h"
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
#define COOKIE_WINDOW_MILLISECONDS 10000.0
#define MAX_PEERS_PER_ADDRESS 8
#define MAX_INBOUND_MESSAGE_BYTES (256 * 1024)
#define UNREADY_TIMEOUT_MILLISECONDS 60000.0
/* TS passes inboundMessageBytes: MAX_INBOUND_MESSAGE_BYTES to the channel. */
#define UNIFIED_INBOUND_LIMIT(limits) ((limits).inbound_message_bytes = MAX_INBOUND_MESSAGE_BYTES)
typedef struct peer {
  qa_net_address address; char token[33]; char nonce[33];
  qa_unified_channel channel;
  char **resources; size_t resource_count, resource_cap;
  bool has_player; qa_app_unified_player player;
  bool ready;
  double last_received, last_queued, last_consumed, last_submitted;
  double required_reliable;
  qa_app_unified_component_publisher *components;
  bool closing; double closing_since;
  double created;
  qa_movement_budget budget;
  bool in_host_call; /* TS UnifiedHostFailure: the raise came from a host callback */
  char fault[QA_CAUGHT_MESSAGE_CAPACITY];
} peer;
/* Command arsenals are stored by value with their strings owned here, because the batch
 * tree they decode from dies with the message and the array can move; `command.arsenal`
 * is set only on the copies poll returns. */
typedef struct queued_command {
  qa_actor_command command; bool has_arsenal; qa_arsenal_intent arsenal;
  char *provider, *weapon;
} queued_command;
struct qa_app_unified_server_network {
  qa_app_unified_server_options options;
  qa_app_unified_server_host host;
  qa_app_unified_composition *composition;
  bool ended;
  double epoch, now;
  unsigned char secret[32];
  peer **peers; size_t peer_count, peer_cap;
  queued_command *commands; size_t command_count, command_cap;
  qa_actor_command *poll_result; qa_arsenal_intent *poll_arsenals; size_t poll_cap;
  char **poll_strings; size_t poll_string_count, poll_string_cap;
  const void **poll_ptrs; size_t poll_ptr_cap;
};
/* TS: new EventRing<ActorCommand>(16384). */
#define COMMAND_RING_CAPACITY 16384
static void queued_command_clear(queued_command *entry) {
  free(entry->provider);
  free(entry->weapon);
}
static double queue_control(peer *p, const qa_app_unified_control *control) {
  size_t length = 0;
  uint8_t *bytes = qa_app_unified_encode_control(control, &length);
  if (!bytes) qa_range_error("out of memory");
  qa_cleanup guard;
  qa_cleanup_push(&guard, free, bytes);
  int64_t sequence = qa_unified_channel_queue_reliable(&p->channel, bytes, length);
  qa_cleanup_pop(&guard);
  free(bytes);
  p->required_reliable = (double)sequence;
  return p->required_reliable;
}
static void offer(qa_app_unified_server_network *n, peer *p) {
  if (p->components) qa_app_unified_component_publisher_destroy(p->components);
  p->components = qa_app_unified_component_publisher_create();
  if (!p->components) qa_range_error("out of memory");
  p->ready = false;
  for (size_t i = 0; i < p->resource_count; i++) free(p->resources[i]);
  p->resource_count = 0;
  p->last_queued = -1;
  p->last_consumed = -1;
  p->last_submitted = -1;
  qa_app_unified_control control = {0};
  control.kind = QA_APP_UNIFIED_OFFER;
  control.epoch = n->epoch;
  control.composition = n->composition;
  control.mode = n->host.mode;
  control.max_clients = n->host.max_clients;
  queue_control(p, &control);
}
typedef struct flush_scope {
  qa_app_unified_server_network *n; peer *p; double now;
  uint8_t **packets; size_t *lengths; size_t count;
} flush_scope;
static void flush_body(void *ctx) {
  flush_scope *scope = ctx;
  scope->packets = qa_unified_channel_flush(&scope->p->channel, scope->now, &scope->lengths, &scope->count);
  if (scope->p->channel.failure) qa_range_error("%s", scope->p->channel.failure);
  qa_datagram_transport *transport = scope->n->options.transport;
  for (size_t i = 0; i < scope->count; i++)
    if (!transport->send(transport->ctx, &scope->p->address, scope->packets[i], scope->lengths[i]))
      qa_range_error("Unified transport rejected an outgoing datagram");
}
/* NULL, or the message of the error TS flush() throws and its callers catch. */
static const char *flush_peer(qa_app_unified_server_network *n, peer *p, double now) {
  flush_scope scope = {n, p, now, NULL, NULL, 0};
  qa_caught_error caught;
  bool ok = qa_try(flush_body, &scope, &caught);
  for (size_t i = 0; i < scope.count; i++) free(scope.packets[i]);
  free(scope.packets);
  free(scope.lengths);
  if (ok) return NULL;
  snprintf(p->fault, sizeof p->fault, "%s", caught.message);
  return p->fault;
}
static void peer_destroy(peer *p) {
  qa_net_address_destroy(&p->address);
  qa_unified_channel_destroy(&p->channel);
  for (size_t i = 0; i < p->resource_count; i++) free(p->resources[i]);
  free(p->resources);
  if (p->components) qa_app_unified_component_publisher_destroy(p->components);
  free(p);
}
static void remove_peer(qa_app_unified_server_network *n, peer *p) {
  for (size_t i = 0; i < n->peer_count; i++)
    if (n->peers[i] == p) {
      memmove(n->peers + i, n->peers + i + 1, (n->peer_count - i - 1) * sizeof *n->peers);
      n->peer_count--;
      break;
    }
  peer_destroy(p);
}
typedef struct farewell_scope {
  qa_app_unified_server_network *n; peer *p; const qa_app_unified_control *control; double now;
} farewell_scope;
static void farewell_body(void *ctx) {
  farewell_scope *scope = ctx;
  queue_control(scope->p, scope->control);
  const char *failure = flush_peer(scope->n, scope->p, scope->now);
  if (failure) qa_range_error("%s", failure);
}
static void drop_peer(qa_app_unified_server_network *n, peer *p, const char *reason, double now) {
  if (p->closing) return;
  p->closing = true;
  p->closing_since = now;
  p->ready = false;
  if (p->has_player) {
    qa_app_unified_player player = p->player;
    p->has_player = false;
    size_t kept = 0;
    for (size_t i = 0; i < n->command_count; i++) {
      if (!qa_same_actor(n->commands[i].command.actor, player.actor))
        n->commands[kept++] = n->commands[i];
      else queued_command_clear(&n->commands[i]);
    }
    n->command_count = kept;
    n->host.disconnect(n->host.ctx, player);
  }
  char *key = qa_net_address_key(&p->address, true);
  if (!key) qa_range_error("out of memory");
  size_t total = strlen(key) + strlen(reason) + 40;
  char *text = xmalloc(total);
  char *printable = qa_printable_peer_text(reason, 256);
  if (!printable) qa_range_error("out of memory");
  snprintf(text, total, "Unified peer %s disconnected: %s\n", key, printable);
  free(printable);
  free(key);
  n->options.print(n->options.print_ctx, text);
  free(text);
  char trimmed[1025];
  size_t length = strlen(reason);
  if (length > 1024) length = 1024;
  memcpy(trimmed, reason, length);
  trimmed[length] = '\0';
  qa_app_unified_control control = {0};
  control.kind = QA_APP_UNIFIED_DISCONNECT;
  control.reason = trimmed;
  /* TS catches a queue or flush failure, closes the channel, and deletes the peer. A
   * closing time past the grace period has the poll loop delete it. */
  farewell_scope scope = {n, p, &control, now};
  qa_caught_error caught;
  if (!qa_try(farewell_body, &scope, &caught)) {
    qa_unified_channel_close(&p->channel);
    p->closing_since = now - 1001;
  }
}
static void cleanup_control(void *arg) { qa_app_unified_control_free(arg); }
static void report_dropped(void *ctx, size_t count) {
  qa_app_unified_server_network *n = ctx;
  char text[128];
  snprintf(text, sizeof text, "Unified server dropped %zu presentation event(s) with non-finite numbers\n", count);
  n->options.print(n->options.print_ctx, text);
}
static void cleanup_batch(void *arg) { qa_app_unified_input_batch_free(arg); }
static void cleanup_value(void *arg) { qa_save_value_free(arg); }
static void accept_decoded(qa_app_unified_server_network *n, peer *p,
    const qa_app_unified_control *control, double now) {
  if (control->kind == QA_APP_UNIFIED_DISCONNECT) {
    drop_peer(n, p, control->reason ? control->reason : "", now);
    return;
  }
  if (control->epoch != n->epoch) return;
  if (control->kind == QA_APP_UNIFIED_READY) {
    if (!control->composition_digest
        || strcmp(control->composition_digest, n->composition->digest) != 0)
      qa_range_error("Client content composition differs from the server");
    if (p->ready) return;
    if (!p->has_player) {
      qa_app_unified_admission admission =
          n->host.admit(n->host.ctx, &p->address, control->userinfo);
      if (!admission.accepted) qa_range_error("%s", admission.reason ? admission.reason : "rejected");
      p->player = admission.player;
      p->has_player = true;
    } else n->host.userinfo(n->host.ctx, p->player, control->userinfo);
    qa_app_unified_control admitted = {0};
    admitted.kind = QA_APP_UNIFIED_ADMITTED;
    admitted.epoch = n->epoch;
    admitted.client.slot = p->player.client.slot;
    admitted.client.generation = p->player.client.generation;
    admitted.actor.slot = p->player.actor.slot;
    admitted.actor.generation = p->player.actor.generation;
    admitted.source_entity = p->player.source_entity;
    queue_control(p, &admitted);
    size_t initial_count = 0;
    qa_save_value *const *initial = n->host.initial_presentation
        ? n->host.initial_presentation(n->host.ctx, p->player, &initial_count) : NULL;
    if (initial_count > 0) {
      size_t payload_length = 0;
      uint8_t *payload = qa_app_unified_encode_presentation_events_reporting(
          (qa_save_value *const *)initial, initial_count, report_dropped, n, &payload_length);
      if (!payload) qa_range_error("out of memory");
      qa_cleanup payload_guard;
      qa_cleanup_push(&payload_guard, free, payload);
      qa_save_value *empty = qa_save_value_array();
      if (!empty) qa_range_error("out of memory");
      size_t simulation_length = 0;
      uint8_t *simulation = qa_encode_checkpoint_value(empty, &simulation_length);
      qa_save_value_free(empty);
      if (!simulation) qa_range_error("out of memory");
      qa_cleanup simulation_guard;
      qa_cleanup_push(&simulation_guard, free, simulation);
      qa_app_unified_control events = {0};
      events.kind = QA_APP_UNIFIED_EVENTS;
      events.epoch = n->epoch;
      events.frame = 0;
      events.payload = payload;
      events.payload_length = payload_length;
      events.simulation = simulation;
      events.simulation_length = simulation_length;
      queue_control(p, &events);
      qa_cleanup_pop(&simulation_guard);
      qa_cleanup_pop(&payload_guard);
      free(payload);
      free(simulation);
    }
    p->ready = true;
    return;
  }
  if (!p->has_player || !p->ready) qa_range_error("Client command arrived before world admission");
  if (control->kind == QA_APP_UNIFIED_COMPONENT_COMMAND) {
    if (!n->host.component_command) qa_range_error("No component command receiver");
    if (!n->host.component_command(n->host.ctx, p->player, control->owner,
            control->generation, control->args, control->arg_count))
      qa_range_error("Component command source or recipient is not admitted");
  } else if (control->kind == QA_APP_UNIFIED_COMMAND)
    n->host.command(n->host.ctx, p->player, control->name, control->args, control->arg_count);
  else if (control->kind == QA_APP_UNIFIED_USERINFO)
    n->host.userinfo(n->host.ctx, p->player, control->value);
  else qa_range_error("Client sent a server-only control message");
}
static void accept_control(qa_app_unified_server_network *n, peer *p,
    const uint8_t *bytes, size_t length, double now) {
  qa_app_unified_control control;
  memset(&control, 0, sizeof control);
  qa_cleanup guard;
  qa_cleanup_push(&guard, cleanup_control, &control);
  qa_app_unified_decode_control_into(&control, bytes, length);
  accept_decoded(n, p, &control, now);
  qa_cleanup_pop(&guard);
  qa_app_unified_control_free(&control);
}
static void accept_inputs(qa_app_unified_server_network *n, peer *p,
    const uint8_t *bytes, size_t length) {
  if (!p->has_player || !p->ready) return;
  qa_app_unified_input_batch batch;
  memset(&batch, 0, sizeof batch);
  qa_cleanup guard;
  qa_cleanup_push(&guard, cleanup_batch, &batch);
  qa_app_unified_decode_inputs_into(&batch, bytes, length);
  if (batch.epoch != n->epoch) {
    qa_cleanup_pop(&guard);
    qa_app_unified_input_batch_free(&batch);
    return;
  }
  for (size_t i = 0; i < batch.command_count; i++) {
    qa_app_unified_input *input = &batch.commands[i];
    if (input->sequence <= p->last_queued) continue;
    double milliseconds = input->command.kind == QA_USER_COMMAND_QW ? input->command.as.qw.milliseconds
        : input->command.kind == QA_USER_COMMAND_Q2 ? input->command.as.q2.milliseconds
        : input->command.kind == QA_USER_COMMAND_Q2_RERELEASE ? input->command.as.q2_rerelease.milliseconds : 0;
    if (!qa_movement_budget_admit(&p->budget, milliseconds, n->now)) { p->last_queued = input->sequence; continue; }
    qa_actor_command command = n->host.input(n->host.ctx, p->player, input->sequence,
        &input->command, input->has_arsenal ? &input->arsenal : NULL);
    /* TS EventRing.admit throws when the ring is full. */
    if (n->command_count >= COMMAND_RING_CAPACITY) qa_range_error("Event ring capacity exhausted (%d undrained records); drain before admitting more", COMMAND_RING_CAPACITY);
    if (n->command_count == n->command_cap) {
      n->command_cap = n->command_cap ? n->command_cap * 2 : 64;
      n->commands = realloc(n->commands, n->command_cap * sizeof *n->commands);
      if (!n->commands) qa_range_error("out of memory");
    }
    queued_command *queued = &n->commands[n->command_count];
    memset(queued, 0, sizeof *queued);
    queued->command = command;
    queued->command.arsenal = NULL;
    if (command.arsenal) {
      queued->has_arsenal = true;
      queued->arsenal = *command.arsenal;
      queued->provider = copy_text(command.arsenal->provider);
      queued->arsenal.provider = queued->provider;
      if (command.arsenal->weapon) {
        queued->weapon = copy_text(command.arsenal->weapon);
        queued->arsenal.weapon = queued->weapon;
      }
    }
    n->command_count++;
    p->last_queued = input->sequence;
  }
  qa_cleanup_pop(&guard);
  qa_app_unified_input_batch_free(&batch);
}
typedef struct receive_scope {
  qa_app_unified_server_network *n; peer *p; qa_receive_event *event; double now;
  qa_unified_delivery *deliveries; size_t count;
} receive_scope;
static void receive_body(void *ctx) {
  receive_scope *scope = ctx;
  peer *p = scope->p;
  scope->deliveries = qa_unified_channel_receive(&p->channel, scope->event->payload,
      scope->event->length, scope->now, &scope->count);
  if (p->channel.failure) qa_range_error("%s", p->channel.failure);
  for (size_t i = 0; i < scope->count; i++) {
    if (p->closing) break;
    qa_unified_delivery *message = &scope->deliveries[i];
    if (strcmp(message->kind, "reliable") == 0)
      accept_control(scope->n, p, message->payload, message->length, scope->now);
    else accept_inputs(scope->n, p, message->payload, message->length);
  }
}
qa_app_unified_server_network *qa_app_unified_server_network_create(
    qa_app_unified_server_options options) {
  qa_app_unified_server_network *n = calloc(1, sizeof *n);
  if (!n) return NULL;
  n->options = options;
  n->host = options.host;
  n->composition = options.composition;
  n->epoch = 1;
  /* TS randomBytes(32): the OS random source (getrandom, as the other ports' crypto draws). */
  if (getrandom(n->secret, sizeof n->secret, 0) != (ssize_t)sizeof n->secret) { free(n); return NULL; }
  return n;
}
void qa_app_unified_server_network_destroy(qa_app_unified_server_network *network) {
  if (!network) return;
  for (size_t i = 0; i < network->peer_count; i++) peer_destroy(network->peers[i]);
  free(network->peers);
  for (size_t i = 0; i < network->command_count; i++) queued_command_clear(&network->commands[i]);
  free(network->commands);
  for (size_t i = 0; i < network->poll_string_count; i++) free(network->poll_strings[i]);
  free(network->poll_strings);
  free(network->poll_arsenals);
  free(network->poll_result);
  free(network->poll_ptrs);
  free(network);
}
qa_app_network_phase qa_app_unified_server_network_phase(
    const qa_app_unified_server_network *network) {
  return network->ended ? QA_APP_PHASE_CLOSED : QA_APP_PHASE_ACTIVE;
}
void qa_app_unified_server_network_wire(const qa_app_unified_server_network *network,
    const char **composition_digest, const qa_save_value **snapshot_schema) {
  *composition_digest = network->composition->digest;
  *snapshot_schema = qa_save_value_get(network->composition->recipe, "snapshotSchema");
}
size_t qa_app_unified_server_network_clients(qa_app_unified_server_network *network,
    qa_app_unified_player *out, size_t capacity) {
  size_t count = 0;
  for (size_t i = 0; i < network->peer_count; i++)
    if (network->peers[i]->has_player) {
      if (count < capacity) out[count] = network->peers[i]->player;
      count++;
    }
  return count;
}
bool qa_app_unified_server_network_disconnect_client(qa_app_unified_server_network *network,
    qa_client_id client, const char *reason) {
  for (size_t i = 0; i < network->peer_count; i++)
    if (network->peers[i]->has_player
        && client_equal(network->peers[i]->player.client, client)) {
      drop_peer(network, network->peers[i], reason, network->now);
      return true;
    }
  return false;
}
/* TS cookie(): HMAC-SHA256(secret, `${addressKey}|${nonce}|${window}`) as hex, first 32 digits. */
static void cookie(const qa_app_unified_server_network *n, const qa_net_address *address, const char *nonce,
    double now, int windows_back, char out[33]) {
  char *key = qa_net_address_key(address, true);
  if (!key) qa_range_error("out of memory");
  size_t length = strlen(key) + strlen(nonce) + 40;
  char *text = xmalloc(length);
  snprintf(text, length, "%s|%s|%s", key, nonce, QA_JS_NUMBER(floor(now / COOKIE_WINDOW_MILLISECONDS) - windows_back));
  free(key);
  unsigned char digest[EVP_MAX_MD_SIZE];
  unsigned int digest_length = 0;
  const EVP_MD * evp_md = EVP_sha256();
  const void * key2 = n->secret;
  size_t data_len = strlen(text);
  HMAC(evp_md, key2, sizeof n->secret, (const unsigned char *)text, data_len, digest, &digest_length);
  free(text);
  static const char hex[] = "0123456789abcdef";
  for (size_t i = 0; i < 16; i++) {
    out[i * 2] = hex[digest[i] >> 4];
    out[i * 2 + 1] = hex[digest[i] & 15];
  }
  out[32] = '\0';
}
static peer *find_peer(qa_app_unified_server_network *n, const char *token) {
  for (size_t i = 0; i < n->peer_count; i++)
    if (strcmp(n->peers[i]->token, token) == 0) return n->peers[i];
  return NULL;
}
const qa_actor_command *qa_app_unified_server_network_poll(
    qa_app_unified_server_network *n, double now, size_t *out_count) {
  *out_count = 0;
  if (n->ended) return NULL;
  n->now = now;
  for (size_t i = 0; i < n->poll_string_count; i++) free(n->poll_strings[i]);
  n->poll_string_count = 0;
  for (size_t count = 0; count < 4096; count++) {
    qa_receive_event event;
    if (!n->options.transport->poll(n->options.transport->ctx, &event)) break;
    if (event.kind == QA_RECV_ERROR) {
      size_t total = strlen(event.error ? event.error : "") + 24;
      char *text = xmalloc(total);
      snprintf(text, total, "Unified transport: %s\n", event.error ? event.error : "");
      n->options.print(n->options.print_ctx, text);
      free(text);
      qa_receive_event_destroy(&event);
      continue;
    }
    if (event.kind != QA_RECV_PACKET) {
      qa_receive_event_destroy(&event);
      continue;
    }
    qa_unified_packet packet;
    if (qa_decode_unified_packet(event.payload, event.length, &packet)) {
      peer *p = find_peer(n, packet.token);
      if (!p || !qa_net_same_address(&p->address, &event.from, true)) {
        qa_receive_event_destroy(&event);
        continue;
      }
      p->last_received = now;
      /* A fault raised by one peer's message costs that peer its connection. If the game itself
       * is broken, the disconnect callback raises too and the application's boundary takes over. */
      receive_scope scope = {n, p, &event, now, NULL, 0};
      qa_caught_error caught;
      bool received = qa_try(receive_body, &scope, &caught);
      qa_unified_deliveries_destroy(scope.deliveries, scope.count);
      if (!received) drop_peer(n, p, caught.message, now);
      qa_receive_event_destroy(&event);
      continue;
    }
    qa_app_unified_handshake handshake;
    if (!qa_app_unified_decode_handshake(event.payload, event.length, &handshake)) {
      qa_receive_event_destroy(&event);
      continue;
    }
    if (handshake.kind == QA_APP_UNIFIED_CHALLENGE) {
      qa_app_unified_handshake_free(&handshake);
      qa_receive_event_destroy(&event);
      continue;
    }
    if (handshake.kind == QA_APP_UNIFIED_HELLO) {
      /* The challenge is a keyed hash of the source and a time window, so spoofed hellos allocate nothing. */
      char token[33];
      cookie(n, &event.from, handshake.nonce, now, 0, token);
      qa_app_unified_handshake challenge = {0};
      challenge.kind = QA_APP_UNIFIED_CHALLENGE;
      challenge.nonce = handshake.nonce;
      challenge.token = token;
      size_t length = 0;
      uint8_t *bytes = qa_app_unified_encode_handshake(&challenge, &length);
      if (!bytes) qa_range_error("out of memory");
      n->options.transport->send(n->options.transport->ctx, &event.from, bytes, length);
      free(bytes);
    } else {
      peer *existing = find_peer(n, handshake.token);
      if (existing && strcmp(existing->nonce, handshake.nonce) == 0
          && qa_net_same_address(&existing->address, &event.from, true)) {
        const char *failure = flush_peer(n, existing, now);
        if (failure) drop_peer(n, existing, failure, now);
        qa_app_unified_handshake_free(&handshake);
        qa_receive_event_destroy(&event);
        continue;
      }
      size_t same_source = 0;
      for (size_t i = 0; i < n->peer_count; i++)
        if (qa_net_same_address(&n->peers[i]->address, &event.from, false)) same_source++;
      char current[33], previous[33];
      cookie(n, &event.from, handshake.nonce, now, 0, current);
      cookie(n, &event.from, handshake.nonce, now, 1, previous);
      if ((double)n->peer_count >= n->host.max_clients + 8 || same_source >= MAX_PEERS_PER_ADDRESS
          || (strcmp(handshake.token, current) != 0 && strcmp(handshake.token, previous) != 0)) {
        qa_app_unified_handshake_free(&handshake);
        qa_receive_event_destroy(&event);
        continue;
      }
      peer *p = calloc(1, sizeof *p);
      if (!p) qa_range_error("out of memory");
      if (!qa_net_address_clone(&event.from, &p->address)) qa_range_error("out of memory");
      snprintf(p->token, sizeof p->token, "%s", handshake.token);
      snprintf(p->nonce, sizeof p->nonce, "%s", handshake.nonce);
      int64_t max_datagram = n->options.transport->max_datagram_bytes(n->options.transport->ctx);
      int64_t datagram = max_datagram < 0 ? 1200 : max_datagram;
      if (datagram > 1200) datagram = 1200;
      qa_unified_channel_limits limits = qa_unified_channel_default_limits();
      limits.datagram_bytes = datagram;
      UNIFIED_INBOUND_LIMIT(limits);
      qa_unified_channel_init(&p->channel, p->token, &limits);
      p->last_received = now;
      p->created = now;
      qa_movement_budget_init(&p->budget, 1800, 1.8);
      p->last_queued = -1;
      p->last_consumed = -1;
      p->last_submitted = -1;
      if (n->peer_count == n->peer_cap) {
        n->peer_cap = n->peer_cap ? n->peer_cap * 2 : 16;
        n->peers = realloc(n->peers, n->peer_cap * sizeof *n->peers);
        if (!n->peers) qa_range_error("out of memory");
      }
      n->peers[n->peer_count++] = p;
      offer(n, p);
    }
    qa_app_unified_handshake_free(&handshake);
    qa_receive_event_destroy(&event);
  }
  for (size_t i = 0; i < n->peer_count;) {
    peer *p = n->peers[i];
    if (p->closing && (now - p->closing_since > 1000
        || (double)qa_unified_channel_acknowledged_reliable_sequence(&p->channel) >= p->required_reliable)) {
      remove_peer(n, p);
      continue;
    }
    if (!p->closing && !p->ready && now - p->created > UNREADY_TIMEOUT_MILLISECONDS) {
      drop_peer(n, p, "Signon timed out", now);
      i++;
      continue;
    }
    if (!p->closing && now - p->last_received > 30000) {
      drop_peer(n, p, "Connection timed out", now);
      i++;
      continue;
    }
    const char *failure = flush_peer(n, p, now);
    if (failure) drop_peer(n, p, failure, now);
    i++;
  }
  /* NetQuake applies one command for the server frame; replaying a burst repeats that frame's
   * elapsed time. `latest` is the TS Map<slot, ActorCommand>, as indices into `merged`. */
  if (n->poll_cap < n->command_count) {
    n->poll_result = realloc(n->poll_result, n->command_count * sizeof *n->poll_result);
    n->poll_arsenals = realloc(n->poll_arsenals, n->command_count * sizeof *n->poll_arsenals);
    if (!n->poll_result || !n->poll_arsenals) qa_range_error("out of memory");
    n->poll_cap = n->command_count;
  }
  queued_command *merged = xmalloc(n->command_count * sizeof *merged);
  size_t *latest = xmalloc(n->command_count * sizeof *latest);
  size_t latest_count = 0;
  for (size_t i = 0; i < n->command_count; i++) {
    queued_command entry = n->commands[i];
    size_t slot_index = latest_count;
    for (size_t j = 0; j < latest_count; j++)
      if (merged[latest[j]].command.actor.slot == entry.command.actor.slot) slot_index = j;
    if (slot_index == latest_count) {
      latest[latest_count++] = i;
    } else {
      const queued_command *previous = &merged[latest[slot_index]];
      if (entry.command.command.kind == QA_USER_COMMAND_Q1
          && previous->command.command.kind == QA_USER_COMMAND_Q1) {
        if (previous->has_arsenal && (!entry.has_arsenal
            || strcmp(entry.arsenal.provider, previous->arsenal.provider) == 0)) {
          qa_arsenal_intent current = entry.has_arsenal ? entry.arsenal : previous->arsenal;
          const qa_arsenal_intent *before = &previous->arsenal;
          if (!current.weapon) current.weapon = before->weapon;
          current.use_holdable = current.use_holdable || before->use_holdable;
          /* current.impulse || previous.impulse: a falsy current yields the previous, even undefined,
           * and an undefined result leaves the spread's value alone. */
          if (!(current.has_impulse && current.impulse != 0) && before->has_impulse) {
            current.has_impulse = true;
            current.impulse = before->impulse;
          }
          entry.arsenal = current;
          entry.has_arsenal = true;
        }
        if (entry.command.command.as.q1.impulse == 0)
          entry.command.command.as.q1.impulse = previous->command.command.as.q1.impulse;
      }
      latest[slot_index] = i;
    }
    merged[i] = entry;
  }
  size_t result_count = 0;
  for (size_t i = 0; i < n->command_count; i++) {
    const qa_actor_command *original = &n->commands[i].command;
    const queued_command *chosen = NULL;
    if (original->command.kind != QA_USER_COMMAND_Q1) chosen = &n->commands[i];
    else
      for (size_t j = 0; j < latest_count; j++)
        if (merged[latest[j]].command.actor.slot == original->actor.slot
            && merged[latest[j]].command.sequence == original->sequence) chosen = &merged[latest[j]];
    if (!chosen) continue;
    n->poll_result[result_count] = chosen->command;
    if (chosen->has_arsenal) {
      n->poll_arsenals[result_count] = chosen->arsenal;
      n->poll_result[result_count].arsenal = &n->poll_arsenals[result_count];
    }
    result_count++;
  }
  for (size_t i = 0; i < n->peer_count; i++) {
    peer *p = n->peers[i];
    if (!p->has_player) continue;
    for (size_t j = 0; j < latest_count; j++) {
      const qa_actor_command *command = &merged[latest[j]].command;
      if (command->actor.slot == p->player.actor.slot && qa_same_actor(command->actor, p->player.actor)
          && command->sequence > p->last_submitted)
        p->last_submitted = command->sequence;
    }
  }
  free(merged);
  free(latest);
  /* The returned arsenals borrow these until the next poll. */
  size_t needed = n->poll_string_count + n->command_count * 2;
  if (needed > n->poll_string_cap) {
    n->poll_strings = realloc(n->poll_strings, needed * sizeof *n->poll_strings);
    if (!n->poll_strings) qa_range_error("out of memory");
    n->poll_string_cap = needed;
  }
  for (size_t i = 0; i < n->command_count; i++) {
    if (n->commands[i].provider) n->poll_strings[n->poll_string_count++] = n->commands[i].provider;
    if (n->commands[i].weapon) n->poll_strings[n->poll_string_count++] = n->commands[i].weapon;
  }
  n->command_count = 0;
  *out_count = result_count;
  return n->poll_result;
}
void qa_app_unified_server_network_submit(qa_app_unified_server_network *network,
    const void *const *commands, size_t count, double now) {
  (void)network; (void)commands; (void)count; (void)now;
  qa_range_error("Server input belongs to its authoritative simulation");
}
typedef struct publish_scope {
  qa_app_unified_server_network *n; peer *p;
  const void *output; const void *const *events; size_t event_count; double now;
} publish_scope;
static void cleanup_frame(void *arg) { qa_app_unified_presentation_frame_free(arg); }
static void cleanup_projection(void *arg) { qa_app_unified_component_projection_free(arg); }
static void publish_body(void *ctx) {
  publish_scope *scope = ctx;
  qa_app_unified_server_network *n = scope->n;
  peer *p = scope->p;
  const void *output = scope->output;
  qa_app_unified_player player = p->player;
  size_t resource_count = 0;
  p->in_host_call = true;
  const qa_app_unified_resource_key *resources =
      n->host.resources(n->host.ctx, player, output, &resource_count);
  p->in_host_call = false;
  qa_app_unified_resource_key *fresh = xmalloc(resource_count * sizeof *fresh);
  qa_cleanup fresh_guard;
  qa_cleanup_push(&fresh_guard, free, fresh);
  size_t fresh_count = 0;
  for (size_t i = 0; i < resource_count; i++) {
    char *id = qa_app_unified_resource_id_from_key(&resources[i]);
    if (!id) qa_range_error("out of memory");
    bool known = false;
    for (size_t j = 0; j < p->resource_count; j++)
      if (strcmp(p->resources[j], id) == 0) known = true;
    free(id);
    if (!known) fresh[fresh_count++] = resources[i];
  }
  if (fresh_count > 0) {
    qa_app_unified_control control = {0};
    control.kind = QA_APP_UNIFIED_RESOURCES;
    control.epoch = n->epoch;
    control.resources = fresh;
    control.resource_count = fresh_count;
    queue_control(p, &control);
    for (size_t i = 0; i < fresh_count; i++) {
      char *id = qa_app_unified_resource_id_from_key(&fresh[i]);
      if (!id) qa_range_error("out of memory");
      if (p->resource_count == p->resource_cap) {
        p->resource_cap = p->resource_cap ? p->resource_cap * 2 : 16;
        p->resources = realloc(p->resources, p->resource_cap * sizeof *p->resources);
        if (!p->resources) qa_range_error("out of memory");
      }
      p->resources[p->resource_count++] = id;
    }
  }
  qa_cleanup_pop(&fresh_guard);
  free(fresh);
  size_t presentation_count = 0;
  p->in_host_call = true;
  qa_save_value *const *presentation = n->host.presentation_events
      ? n->host.presentation_events(n->host.ctx, player, scope->events, scope->event_count, &presentation_count)
      : NULL;
  qa_app_unified_presentation_frame_t frame =
      n->host.frame(n->host.ctx, player, output, n->epoch, p->last_consumed);
  p->in_host_call = false;
  qa_cleanup frame_guard;
  qa_cleanup_push(&frame_guard, cleanup_frame, &frame);
  if (presentation_count > 0 || frame.event_count > 0) {
    size_t payload_length = 0;
    uint8_t *payload = qa_app_unified_encode_presentation_events_reporting(
        (qa_save_value *const *)presentation, presentation_count, report_dropped, n, &payload_length);
    if (!payload) qa_range_error("out of memory");
    qa_cleanup payload_guard;
    qa_cleanup_push(&payload_guard, free, payload);
    qa_save_value *list = qa_save_value_array();
    if (!list) qa_range_error("out of memory");
    qa_cleanup list_guard;
    qa_cleanup_push(&list_guard, cleanup_value, list);
    for (size_t i = 0; i < frame.event_count; i++)
      if (!qa_save_value_push(list, qa_app_unified_write_simulation_event(frame.events[i])))
        qa_range_error("out of memory");
    size_t simulation_length = 0;
    uint8_t *simulation = qa_encode_checkpoint_value(list, &simulation_length);
    if (!simulation) qa_range_error("out of memory");
    qa_cleanup simulation_guard;
    qa_cleanup_push(&simulation_guard, free, simulation);
    const qa_simulation_output *simulation_output = output;
    qa_app_unified_control control = {0};
    control.kind = QA_APP_UNIFIED_EVENTS;
    control.epoch = n->epoch;
    control.frame = simulation_output->snapshot.frame.frame;
    control.payload = payload;
    control.payload_length = payload_length;
    control.simulation = simulation;
    control.simulation_length = simulation_length;
    queue_control(p, &control);
    qa_cleanup_pop(&simulation_guard);
    qa_cleanup_pop(&list_guard);
    qa_cleanup_pop(&payload_guard);
    free(simulation);
    qa_save_value_free(list);
    free(payload);
  }
  size_t source_count = 0, native_count = 0;
  p->in_host_call = true;
  const qa_app_unified_component_publication *sources = n->host.components
      ? n->host.components(n->host.ctx, player, &source_count) : NULL;
  const qa_app_unified_native_publication *native = n->host.native_components
      ? n->host.native_components(n->host.ctx, player, &native_count) : NULL;
  p->in_host_call = false;
  qa_app_unified_component_projection projection =
      qa_app_unified_component_publisher_project(p->components, sources, source_count,
          native, native_count);
  qa_cleanup projection_guard;
  qa_cleanup_push(&projection_guard, cleanup_projection, &projection);
  if (projection.has_update) {
    qa_app_unified_control control = {0};
    control.kind = QA_APP_UNIFIED_COMPONENTS;
    control.epoch = n->epoch;
    control.update = projection.update;
    control.has_update = true;
    queue_control(p, &control);
  }
  frame.components = projection.frame;
  frame.has_components = true;
  frame.output.events = NULL;
  frame.output.event_count = 0;
  qa_app_unified_identity_encoder encoder = {0};
  encoder.ctx = n->host.ctx;
  encoder.session = n->host.session;
  encoder.session_matches = n->host.session_matches;
  encoder.actor = n->host.actor_reference;
  size_t length = 0;
  /* TS encodeUnifiedFrame(zeroNonFinite({...frame, components, output: {snapshot, events: []}})). */
  uint8_t *encoded = qa_app_unified_encode_frame_finite(&frame, &encoder, &length);
  if (!encoded) qa_range_error("out of memory");
  qa_cleanup encoded_guard;
  qa_cleanup_push(&encoded_guard, free, encoded);
  qa_unified_channel_queue_frame(&p->channel, encoded, length, p->required_reliable);
  qa_cleanup_pop(&encoded_guard);
  free(encoded);
  memset(&frame.components, 0, sizeof frame.components);
  frame.has_components = false;
  qa_cleanup_pop(&projection_guard);
  qa_app_unified_component_projection_free(&projection);
  qa_cleanup_pop(&frame_guard);
  qa_app_unified_presentation_frame_free(&frame);
  const char *failure = flush_peer(n, p, scope->now);
  if (failure) qa_range_error("%s", failure);
}
void qa_app_unified_server_network_publish(qa_app_unified_server_network *n,
    const void *output, const void *const *events, size_t count, double now) {
  if (n->ended) return;
  for (size_t i = 0; i < n->peer_count; i++) {
    peer *p = n->peers[i];
    if (!p->has_player || !p->ready || p->closing) continue;
    p->last_consumed = p->last_submitted;
    publish_scope scope = {n, p, output, events, count, now};
    qa_caught_error caught;
    p->in_host_call = false;
    if (qa_try(publish_body, &scope, &caught)) continue;
    /* TS rethrows a host failure's cause to the frame boundary; any other fault drops this peer. */
    if (p->in_host_call) { p->in_host_call = false; qa_rethrow(&caught); }
    drop_peer(n, p, caught.message, now);
  }
}
void qa_app_unified_server_network_change_world(qa_app_unified_server_network *n,
    qa_app_unified_server_host host, qa_app_unified_composition *composition) {
  if (n->ended) qa_range_error("Unified server is closed");
  if (n->epoch == 4294967295.0) qa_range_error("Unified world sequence exhausted");
  n->host = host;
  n->composition = composition;
  n->epoch++;
  for (size_t i = 0; i < n->command_count; i++) queued_command_clear(&n->commands[i]);
  n->command_count = 0;
  for (size_t i = 0; i < n->peer_count; i++) {
    peer *p = n->peers[i];
    if (p->closing) continue;
    if (p->has_player) p->player = host.carried_player(host.ctx, p->player.client);
    offer(n, p);
  }
}
typedef struct close_step { qa_app_unified_server_network *n; peer *p; } close_step;
static void close_disconnect_body(void *ctx) {
  close_step *step = ctx;
  step->n->host.disconnect(step->n->host.ctx, step->p->player);
}
static void close_transport_body(void *ctx) {
  qa_datagram_transport *transport = ((close_step *)ctx)->n->options.transport;
  transport->close(transport->ctx);
}
void qa_app_unified_server_network_close(qa_app_unified_server_network *n) {
  if (n->ended) return;
  n->ended = true;
  size_t failures = 0;
  qa_caught_error caught;
  for (size_t i = 0; i < n->peer_count; i++) {
    peer *p = n->peers[i];
    close_step step = {n, p};
    if (p->has_player && !qa_try(close_disconnect_body, &step, &caught)) failures++;
    peer_destroy(p);
  }
  n->peer_count = 0;
  for (size_t i = 0; i < n->command_count; i++) queued_command_clear(&n->commands[i]);
  n->command_count = 0;
  close_step step = {n, NULL};
  if (!qa_try(close_transport_body, &step, &caught)) failures++;
  if (failures > 0) qa_range_error("Unified server shutdown failed");
}
static qa_app_network_phase net_phase(void *ctx) {
  return qa_app_unified_server_network_phase(ctx);
}
static const void *const *net_poll(void *ctx, double now, size_t *out_count) {
  qa_app_unified_server_network *n = ctx;
  size_t count = 0;
  const qa_actor_command *commands = qa_app_unified_server_network_poll(n, now, &count);
  if (n->poll_ptr_cap < count) {
    n->poll_ptrs = realloc(n->poll_ptrs, count * sizeof *n->poll_ptrs);
    if (!n->poll_ptrs) qa_range_error("out of memory");
    n->poll_ptr_cap = count;
  }
  for (size_t i = 0; i < count; i++) n->poll_ptrs[i] = &commands[i];
  *out_count = count;
  return n->poll_ptrs;
}
static void net_submit(void *ctx, const void *const *commands, size_t count, double now) {
  qa_app_unified_server_network_submit(ctx, commands, count, now);
}
static void net_publish(void *ctx, const void *output, const void *const *events,
    size_t count, double now) {
  qa_app_unified_server_network_publish(ctx, output, events, count, now);
}
static void net_close(void *ctx) { qa_app_unified_server_network_close(ctx); }
qa_app_application_network qa_app_unified_server_network_as_network(
    qa_app_unified_server_network *network) {
  qa_app_application_network result = {0};
  result.ctx = network;
  result.role = QA_APP_ROLE_SERVER;
  result.phase = net_phase;
  result.poll = net_poll;
  result.submit = net_submit;
  result.publish = net_publish;
  result.close = net_close;
  return result;
}
