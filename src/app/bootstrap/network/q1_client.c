/* Port of src/app/bootstrap/network/q1-client.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/network/q1_client.h"
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
#include "qa/network/q1/channels.h"
#include "qa/network/q1/codecs/wide.h"
#include "qa/network/q1/handshake.h"
#include "qa/network/q1/message.h"
#include "qa/network/q1/recording.h"
typedef struct byte_item { uint8_t *bytes; size_t length; } byte_item;
struct qa_app_q1_client_network {
  qa_app_q1_client_network_options options;
  /* Opaque landed type with caller storage (no published size). */
  _Alignas(16) unsigned char recording_storage[512];
  bool has_sink; qa_app_q1_recording_sink sink; void *sink_token;
  qa_vec3 view_angles;
  qa_nq_connect_client handshake;
  qa_netquake_channel channel;
  qa_netquake_decoder decoder;
  qa_netquake_signon signon;
  qa_app_network_phase state;
  bool has_peer; qa_net_address peer;
  double last_received, movement_messages;
  byte_item *reliable; size_t reliable_count, reliable_cap, reliable_head;
};
static void reliable_admit(qa_app_q1_client_network *n, uint8_t *bytes, size_t length) {
  if (n->reliable_count - n->reliable_head >= 4096)
    qa_range_error("event ring capacity exceeded");
  if (n->reliable_count == n->reliable_cap) {
    n->reliable_cap = n->reliable_cap ? n->reliable_cap * 2 : 16;
    n->reliable = realloc(n->reliable, n->reliable_cap * sizeof *n->reliable);
    if (!n->reliable) qa_range_error("out of memory");
  }
  n->reliable[n->reliable_count].bytes = bytes;
  n->reliable[n->reliable_count].length = length;
  n->reliable_count++;
}
static bool reliable_shift(qa_app_q1_client_network *n, byte_item *out) {
  if (n->reliable_head >= n->reliable_count) return false;
  *out = n->reliable[n->reliable_head++];
  if (n->reliable_head == n->reliable_count) n->reliable_head = n->reliable_count = 0;
  return true;
}
static void send_to(qa_app_q1_client_network *n, const qa_net_address *to,
    const uint8_t *bytes, size_t length) {
  n->options.transport.send(n->options.transport.ctx, to, bytes, length);
}
qa_app_q1_client_network *qa_app_q1_client_network_create(qa_app_q1_client_network_options options) {
  if (options.seat.has_extension_flags)
    qa_range_error("Native NetQuake does not negotiate private extensions");
  qa_app_q1_client_network *n = calloc(1, sizeof *n);
  if (!n) return NULL;
  n->options = options;
  n->state = QA_APP_PHASE_CONNECTING;
  qa_netquake_recording_state_init((qa_netquake_recording_state *)n->recording_storage);
  qa_nq_connect_client_init(&n->handshake);
  if (!qa_netquake_channel_init(&n->channel, QA_WIDE_MAX_MSGLEN, 0)) {
    free(n); return NULL;
  }
  qa_q1_protocol_identity protocol = {"q1-netquake", 15, 0, 0};
  qa_netquake_decoder_init(&n->decoder, protocol, "known-retail", false);
  qa_netquake_signon_init(&n->signon, options.seat);
  return n;
}
void qa_app_q1_client_network_destroy(qa_app_q1_client_network *network) {
  if (!network) return;
  qa_netquake_recording_state_destroy((qa_netquake_recording_state *)network->recording_storage);
  qa_nq_connect_client_destroy(&network->handshake);
  qa_netquake_channel_destroy(&network->channel);
  qa_netquake_decoder_destroy(&network->decoder);
  if (network->has_peer) qa_net_address_destroy(&network->peer);
  for (size_t i = network->reliable_head; i < network->reliable_count; i++)
    free(network->reliable[i].bytes);
  free(network->reliable);
  free(network->sink_token);
  free(network);
}
const qa_app_q1_client_network_options *qa_app_q1_client_network_options_of(
    const qa_app_q1_client_network *network) { return &network->options; }
qa_app_network_phase qa_app_q1_client_network_phase(const qa_app_q1_client_network *network) {
  return network->state;
}
bool qa_app_q1_client_network_server_address(qa_app_q1_client_network *network, qa_net_address *out) {
  if (!network->has_peer) return false;
  return qa_net_address_clone(&network->peer, out) != 0;
}
void qa_app_q1_client_network_command(qa_app_q1_client_network *network, const char *text) {
  if (!network->has_peer) qa_range_error("NetQuake client is not connected");
  qa_size_buf bytes;
  if (!qa_size_buf_init(&bytes, 8000, false)) qa_range_error("out of memory");
  qa_write_client_string_command(&bytes, text);
  size_t length = 0;
  uint8_t *payload = qa_size_buf_bytes(&bytes, &length);
  qa_size_buf_destroy(&bytes);
  if (!payload) qa_range_error("out of memory");
  reliable_admit(network, payload, length);
}
static void reject(qa_app_q1_client_network *n, const char *reason) {
  n->state = QA_APP_PHASE_REJECTED;
  n->options.host.disconnected(n->options.host.ctx, reason);
}
const void *const *qa_app_q1_client_network_poll(qa_app_q1_client_network *n,
    double now, size_t *out_count) {
  *out_count = 0;
  if (n->state == QA_APP_PHASE_CLOSED || n->state == QA_APP_PHASE_REJECTED) return NULL;
  if (!n->has_peer) {
    size_t length = 0;
    uint8_t *request = qa_nq_connect_client_next(&n->handshake, now, &length);
    if (request) {
      send_to(n, &n->options.remote, request, length);
      free(request);
    }
    if (n->handshake.kind == 2) {
      reject(n, n->handshake.reason);
      return NULL;
    }
  }
  for (;;) {
    qa_receive_event packet;
    if (!n->options.transport.poll(n->options.transport.ctx, &packet)) break;
    if (packet.kind != QA_RECV_PACKET) { qa_receive_event_destroy(&packet); continue; }
    const qa_net_address *expected = n->has_peer ? &n->peer : &n->options.remote;
    if (!qa_net_same_address(&packet.from, expected, true)) {
      qa_receive_event_destroy(&packet);
      continue;
    }
    if (!n->has_peer) {
      qa_nq_connect_client_receive(&n->handshake, packet.payload, packet.length);
      if (n->handshake.kind == 2) {
        reject(n, n->handshake.reason);
        qa_receive_event_destroy(&packet);
        break;
      }
      if (n->handshake.kind == 1) {
        if (!qa_net_address_clone(&n->options.remote, &n->peer))
          qa_range_error("out of memory");
        n->peer.port = (int64_t)n->handshake.port;
        n->has_peer = true;
        n->state = QA_APP_PHASE_LOADING;
        n->last_received = now;
      }
      qa_receive_event_destroy(&packet);
      continue;
    }
    qa_channel_delivery delivery = {0};
    uint8_t *reply = NULL;
    size_t reply_length = 0;
    qa_netquake_channel_receive(&n->channel, packet.payload, packet.length, now,
        &delivery, &reply, &reply_length);
    n->last_received = now;
    if (reply) {
      send_to(n, &n->peer, reply, reply_length);
      free(reply);
    }
    qa_receive_event_destroy(&packet);
    if (!delivery.payload) continue;
    size_t count = 0;
    qa_netquake_message *messages = qa_netquake_decoder_decode(
        &n->decoder, delivery.payload, delivery.length, &count);
    qa_netquake_recording_state_observe((qa_netquake_recording_state *)n->recording_storage, messages, count);
    for (size_t i = 0; i < count; i++) {
      if (strcmp(messages[i].kind, "server-info") == 0) {
        if (messages[i].max_clients < 1 || messages[i].max_clients > 16)
          qa_range_error("Native NetQuake requires 1–16 scoreboard slots");
        n->signon.stage = 0;
        n->movement_messages = 0;
        n->state = QA_APP_PHASE_LOADING;
      }
    }
    n->options.host.receive(n->options.host.ctx, messages, count, now);
    for (size_t i = 0; i < count; i++)
      if (strcmp(messages[i].kind, "set-angle") == 0) n->view_angles = messages[i].angles;
    if (n->has_sink)
      n->sink.append(n->sink.ctx, delivery.payload, delivery.length, n->view_angles);
    bool disconnected = false;
    for (size_t i = 0; i < count; i++) {
      if (strcmp(messages[i].kind, "signon") == 0) {
        size_t response_length = 0;
        uint8_t *response = qa_netquake_signon_receive(&n->signon,
            messages[i].stage, &response_length);
        if (response && response_length > 0) reliable_admit(n, response, response_length);
        else free(response);
      }
      if (strcmp(messages[i].kind, "entity") == 0) qa_netquake_signon_first_entity(&n->signon);
      if (strcmp(messages[i].kind, "disconnect") == 0) disconnected = true;
    }
    for (size_t i = 0; i < count; i++) qa_netquake_message_destroy(&messages[i]);
    free(messages);
    free(delivery.payload);
    if (disconnected) {
      n->state = QA_APP_PHASE_CLOSED;
      n->options.host.disconnected(n->options.host.ctx, "Server disconnected");
      return NULL;
    }
    if (qa_netquake_signon_active(&n->signon)) n->state = QA_APP_PHASE_ACTIVE;
  }
  if (n->has_peer) {
    double timeout = n->options.has_timeout_milliseconds
        ? n->options.timeout_milliseconds : 120000;
    if (now - n->last_received > timeout) {
      reject(n, "Connection timed out");
      return NULL;
    }
    if (qa_netquake_channel_can_send_reliable(&n->channel)) {
      byte_item item;
      if (reliable_shift(n, &item)) {
        qa_netquake_channel_queue_reliable(&n->channel, item.bytes, item.length);
        free(item.bytes);
      }
    }
    size_t length = 0;
    uint8_t *packet = qa_netquake_channel_next(&n->channel, now, &length);
    if (packet) {
      send_to(n, &n->peer, packet, length);
      free(packet);
    }
  }
  return NULL;
}
void qa_app_q1_client_network_submit(qa_app_q1_client_network *network,
    const void *const *commands, size_t count, double now) {
  (void)now;
  if (network->state != QA_APP_PHASE_ACTIVE || !network->has_peer) return;
  if (count > 1) qa_range_error("A native NetQuake connection carries one player");
  for (size_t i = 0; i < count; i++) {
    qa_q1_user_command move = network->options.host.command(
        network->options.host.ctx, commands[i]);
    network->view_angles = move.view_angles;
    if (++network->movement_messages <= 2) continue;
    move.acknowledged_server_time_seconds =
        network->decoder.time_seconds;
    qa_size_buf bytes;
    if (!qa_size_buf_init(&bytes, 128, false)) qa_range_error("out of memory");
    qa_write_netquake_move(&bytes, &move, network->decoder.protocol,
        network->decoder.flags_value, true);
    size_t length = 0;
    uint8_t *payload = qa_size_buf_bytes(&bytes, &length);
    qa_size_buf_destroy(&bytes);
    size_t packet_length = 0;
    uint8_t *packet = qa_netquake_channel_unreliable(&network->channel,
        payload, length, &packet_length);
    free(payload);
    if (packet) {
      send_to(network, &network->peer, packet, packet_length);
      free(packet);
    }
  }
}
void qa_app_q1_client_network_publish(qa_app_q1_client_network *network,
    const void *output, const void *const *events, size_t count, double now) {
  (void)network; (void)output; (void)events; (void)count; (void)now;
  qa_range_error("Remote client cannot publish authoritative state");
}
void qa_app_q1_client_network_close(qa_app_q1_client_network *network) {
  if (network->options.transport.is_closed(network->options.transport.ctx)) return;
  if (network->has_peer && network->state != QA_APP_PHASE_CLOSED) {
    uint8_t byte = 2;
    size_t length = 0;
    uint8_t *packet = qa_netquake_channel_unreliable(&network->channel, &byte, 1, &length);
    if (packet) {
      send_to(network, &network->peer, packet, length);
      free(packet);
    }
  }
  network->state = QA_APP_PHASE_CLOSED;
  network->options.transport.close(network->options.transport.ctx);
}
void qa_app_q1_recording_seed_free(qa_app_q1_recording_seed *seed) {
  for (size_t i = 0; i < seed->packet_count; i++) free(seed->packets[i].message);
  free(seed->packets);
  memset(seed, 0, sizeof *seed);
}
void qa_app_q1_client_network_recording_seed(qa_app_q1_client_network *network,
    qa_app_q1_recording_seed *out) {
  if (network->state != QA_APP_PHASE_ACTIVE)
    qa_range_error("Recording requires an active NetQuake connection");
  size_t *lengths = NULL;
  size_t count = 0;
  uint8_t **packets = qa_netquake_recording_state_seed((qa_netquake_recording_state *)network->recording_storage,
      network->decoder.protocol, &lengths, &count);
  if (!packets && count) qa_range_error("out of memory");
  out->protocol_version = (double)network->decoder.protocol.version;
  out->track = -1;
  out->packet_count = count;
  out->packets = malloc(count ? count * sizeof *out->packets : 1);
  if (!out->packets) qa_range_error("out of memory");
  for (size_t i = 0; i < count; i++) {
    out->packets[i].message = packets[i];
    out->packets[i].length = lengths[i];
    out->packets[i].view_angles = network->view_angles;
  }
  free(packets);
  free(lengths);
}
void *qa_app_q1_client_network_recording_attach(qa_app_q1_client_network *network,
    qa_app_q1_recording_sink sink) {
  if (network->has_sink || network->state != QA_APP_PHASE_ACTIVE)
    qa_range_error("NetQuake recording cannot attach");
  network->sink = sink;
  network->has_sink = true;
  network->sink_token = malloc(1);
  return network->sink_token;
}
void qa_app_q1_client_network_recording_detach(qa_app_q1_client_network *network, void *token) {
  if (network->has_sink && network->sink_token == token) {
    network->has_sink = false;
    free(network->sink_token);
    network->sink_token = NULL;
  }
}
static qa_app_network_phase net_phase(void *ctx) {
  return qa_app_q1_client_network_phase(ctx);
}
static const void *net_wire(void *ctx) {
  qa_app_q1_client_network *n = ctx;
  return &n->decoder.protocol;
}
static const void *const *net_poll(void *ctx, double now, size_t *out_count) {
  return qa_app_q1_client_network_poll(ctx, now, out_count);
}
static void net_submit(void *ctx, const void *const *commands, size_t count, double now) {
  qa_app_q1_client_network_submit(ctx, commands, count, now);
}
static void net_publish(void *ctx, const void *output, const void *const *events,
    size_t count, double now) {
  qa_app_q1_client_network_publish(ctx, output, events, count, now);
}
static void net_close(void *ctx) { qa_app_q1_client_network_close(ctx); }
qa_app_application_network qa_app_q1_client_network_as_network(qa_app_q1_client_network *network) {
  qa_app_application_network result = {0};
  result.ctx = network;
  result.role = QA_APP_ROLE_CLIENT;
  result.phase = net_phase;
  result.wire = net_wire;
  result.poll = net_poll;
  result.submit = net_submit;
  result.publish = net_publish;
  result.close = net_close;
  return result;
}
