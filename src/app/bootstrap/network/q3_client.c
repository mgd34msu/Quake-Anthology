/* Port of src/app/bootstrap/network/q3-client.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/network/q3_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
#include "qa/network/q3/recording.h"
#include "qa/network/q3/transport.h"
static void b_assert(void *ctx) {
  qa_app_q3_client_bindings_storage *s = ctx;
  s->lifecycle.assert_current(s->lifecycle.ctx);
}
static void b_print(void *ctx, const char *text) {
  qa_app_q3_client_bindings_storage *s = ctx;
  s->host.print(s->host.ctx, text);
}
static void lifecycle_assert_thunk(void *ctx) {
  qa_app_q3_client_lifecycle *lifecycle = ctx;
  lifecycle->assert_current(lifecycle->ctx);
}
static void b_clear(void *ctx) {
  qa_app_q3_client_bindings_storage *s = ctx;
  s->host.clear_active(s->host.ctx, lifecycle_assert_thunk, &s->lifecycle);
  s->lifecycle.assert_current(s->lifecycle.ctx);
  s->lifecycle.cleared(s->lifecycle.ctx);
}
static void b_system_info(void *ctx, const char *info) {
  qa_app_q3_client_bindings_storage *s = ctx;
  s->host.system_info(s->host.ctx, info);
}
static void b_gamestate(void *ctx, const qa_q3_gamestate *state, int64_t generation) {
  qa_app_q3_client_bindings_storage *s = ctx;
  s->host.gamestate(s->host.ctx, state, (double)generation,
      lifecycle_assert_thunk, &s->lifecycle);
  s->lifecycle.assert_current(s->lifecycle.ctx);
  s->lifecycle.primed(s->lifecycle.ctx);
}
static void b_snapshot(void *ctx, const qa_q3_wire_snapshot *snapshot, int64_t ping) {
  qa_app_q3_client_bindings_storage *s = ctx;
  s->host.snapshot(s->host.ctx, snapshot, (double)ping);
  s->lifecycle.snapshot(s->lifecycle.ctx, snapshot, (double)ping);
}
static int64_t b_download_size(void *ctx, int64_t size) {
  qa_app_q3_client_bindings_storage *s = ctx;
  return (int64_t)s->host.download_size(s->host.ctx, (double)size);
}
static void b_download(void *ctx, const qa_q3_download_block *block) {
  qa_app_q3_client_bindings_storage *s = ctx;
  s->host.download(s->host.ctx, block);
}
static void b_map_restart(void *ctx) {
  qa_app_q3_client_bindings_storage *s = ctx;
  s->host.map_restart(s->host.ctx);
}
static void b_level_shot(void *ctx) {
  (void)ctx;
  qa_range_error("Remote server cannot request a local levelshot");
}
static bool b_local_server(void *ctx) { (void)ctx; return false; }
void qa_app_q3_application_client_bindings(qa_app_q3_client_bindings_storage *storage,
    qa_app_q3_client_host host, qa_app_q3_client_lifecycle lifecycle) {
  storage->host = host;
  storage->lifecycle = lifecycle;
  storage->bindings.assert_current = b_assert;
  storage->bindings.print = b_print;
  storage->bindings.print_ctx = storage;
  storage->bindings.clear_active = b_clear;
  storage->bindings.system_info = b_system_info;
  storage->bindings.gamestate = b_gamestate;
  storage->bindings.snapshot = b_snapshot;
  storage->bindings.download_size = b_download_size;
  storage->bindings.download = b_download;
  storage->bindings.map_restart = b_map_restart;
  storage->bindings.level_shot = b_level_shot;
  storage->bindings.local_server_running = b_local_server;
  storage->bindings.ctx = storage;
}
struct qa_app_q3_client_network {
  qa_app_q3_client_network_options options;
  qa_q3_client_admission *admission;
  qa_q3_client_connection *connection;
  qa_app_q3_client_bindings_storage bindings;
  qa_q3_address peer; bool peer_owned;
  qa_app_network_phase state;
  bool primed, entered;
  double last_received;
  char *last_userinfo;
  bool has_sink; qa_app_q3_recording_sink sink; void *sink_token;
};
static void host_print_thunk(void *ctx, const char *text) {
  qa_app_q3_client_network *n = ctx;
  n->options.host.print(n->options.host.ctx, text);
}
static bool net_to_q3(const qa_net_address *from, qa_q3_address *out) {
  memset(out, 0, sizeof *out);
  switch (from->kind) {
    case QA_NET_IPV4:
      out->kind = QA_Q3_ADDR_IPV4;
      memcpy(out->host, from->ipv4, 4);
      out->port = from->port;
      return true;
    case QA_NET_IPX:
      out->kind = QA_Q3_ADDR_IPX;
      out->network = (int64_t)from->network;
      memcpy(out->node, from->node, 6);
      out->port = from->port;
      return true;
    case QA_NET_LOOPBACK: {
      out->kind = QA_Q3_ADDR_LOOPBACK;
      size_t length = strlen(from->host) + 1;
      out->loopback_id = malloc(length);
      if (!out->loopback_id) return false;
      memcpy(out->loopback_id, from->host, length);
      return true;
    }
    default: return false;
  }
}
static bool q3_to_net(const qa_q3_address *from, qa_net_address *out) {
  memset(out, 0, sizeof *out);
  switch (from->kind) {
    case QA_Q3_ADDR_IPV4: {
      int64_t host[4] = {from->host[0], from->host[1], from->host[2], from->host[3]};
      return qa_net_make_ipv4(out, host, from->port, false) != 0;
    }
    case QA_Q3_ADDR_IPX: {
      int64_t node[6];
      for (int i = 0; i < 6; i++) node[i] = from->node[i];
      return qa_net_make_ipx(out, from->network, node, from->port) != 0;
    }
    case QA_Q3_ADDR_LOOPBACK:
      return qa_net_make_loopback(out, from->loopback_id) != 0;
    default: return false;
  }
}
static void transport_send_q3(qa_app_q3_client_network *n, const qa_q3_address *to,
    const uint8_t *payload, size_t length) {
  qa_net_address address;
  if (!q3_to_net(to, &address)) qa_range_error("out of memory");
  n->options.transport.send(n->options.transport.ctx, &address, payload, length);
  qa_net_address_destroy(&address);
}
static void delivery_send(void *ctx, const qa_q3_address *to, const uint8_t *packet, size_t length) {
  transport_send_q3(ctx, to, packet, length);
}
static void delivery_remote(void *ctx, qa_q3_address *out) {
  qa_app_q3_client_network *n = ctx;
  if (!qa_q3_mirror_address_copy(out, &n->peer)) qa_range_error("out of memory");
}
static const qa_q3_channel_delivery *make_delivery(qa_app_q3_client_network *n,
    qa_q3_client_connection *connection, qa_q3_channel_delivery_storage *storage) {
  return qa_q3_channel_delivery_create(storage, delivery_send, n, delivery_remote, n,
      qa_q3_client_connection_source_state(connection), host_print_thunk, n);
}
static double cvar_integer(qa_cvar_registry *cvars, const char *name, double fallback) {
  if (!cvars) return fallback;
  qa_cvar_snapshot snap;
  if (!qa_cvar_registry_get(cvars, name, &snap)) return fallback;
  double value = snap.integer_value;
  qa_cvar_read_free(&snap);
  return value;
}
static void send_now(qa_app_q3_client_network *n, double now) {
  qa_q3_client_connection *connection = n->connection;
  if (!connection) return;
  qa_q3_client_send_options options = {now,
      cvar_integer(n->options.cvars, "cl_packetdup", 1), false};
  qa_q3_channel_delivery_storage storage;
  qa_q3_client_connection_transmit(connection, &options, make_delivery(n, connection, &storage));
}
typedef struct lifecycle_ctx { qa_app_q3_client_network *n; qa_q3_client_connection *connection; } lifecycle_ctx;
static void lc_assert(void *ctx) {
  lifecycle_ctx *lc = ctx;
  if (lc->n->connection != lc->connection || lc->n->state == QA_APP_PHASE_CLOSED
      || lc->n->state == QA_APP_PHASE_REJECTED)
    qa_range_error("Q3 callback belongs to a retired connection");
}
static void lc_cleared(void *ctx) {
  lifecycle_ctx *lc = ctx;
  lc->n->state = QA_APP_PHASE_LOADING; lc->n->primed = false; lc->n->entered = false;
}
static void lc_primed(void *ctx) {
  lifecycle_ctx *lc = ctx;
  lc->n->primed = !lc->n->options.host.downloading(lc->n->options.host.ctx);
}
static void lc_snapshot(void *ctx, const qa_q3_wire_snapshot *snapshot, double ping) {
  lifecycle_ctx *lc = ctx;
  (void)ping;
  if ((snapshot->flags & 2) == 0 && lc->n->primed) lc->n->state = QA_APP_PHASE_ACTIVE;
}
qa_app_q3_client_network *qa_app_q3_client_network_create(qa_app_q3_client_network_options options) {
  qa_app_q3_client_network *n = calloc(1, sizeof *n);
  if (!n) return NULL;
  n->options = options;
  n->state = QA_APP_PHASE_CONNECTING;
  n->last_userinfo = calloc(1, 1);
  if (!qa_q3_mirror_address_copy(&n->peer, &options.remote)) { free(n->last_userinfo); free(n); return NULL; }
  n->peer_owned = true;
  if (!qa_q3_client_admission_init(&n->admission, (int64_t)options.qport, host_print_thunk, n)) {
    qa_q3_mirror_address_destroy(&n->peer); free(n->last_userinfo); free(n); return NULL;
  }
  qa_q3_client_admission_begin(n->admission, &options.remote);
  return n;
}
void qa_app_q3_client_network_destroy(qa_app_q3_client_network *network) {
  if (!network) return;
  qa_q3_client_connection_destroy(network->connection);
  qa_q3_client_admission_destroy(network->admission);
  if (network->peer_owned) qa_q3_mirror_address_destroy(&network->peer);
  free(network->last_userinfo);
  free(network->sink_token);
  free(network);
}
const qa_app_q3_client_network_options *qa_app_q3_client_network_options_of(
    const qa_app_q3_client_network *network) { return &network->options; }
qa_app_network_phase qa_app_q3_client_network_phase(const qa_app_q3_client_network *network) {
  return network->state;
}
qa_q3_client_connection *qa_app_q3_client_network_native(qa_app_q3_client_network *network) {
  return network->connection;
}
double qa_app_q3_client_network_connect_packet_count(const qa_app_q3_client_network *network) {
  return (double)qa_q3_client_admission_connect_packet_count(network->admission);
}
void qa_app_q3_client_network_command(qa_app_q3_client_network *network, const char *text) {
  if (!network->connection) qa_range_error("Q3 client has no connection");
  qa_q3_client_reliable_commands_add(
      qa_q3_client_connection_reliable(network->connection), text, NULL);
}
void qa_app_q3_client_network_send_packet(qa_app_q3_client_network *network) {
  send_now(network, network->options.now(network->options.now_ctx));
}
typedef struct auth_ctx { qa_app_q3_client_network *n; qa_q3_address peer; } auth_ctx;
static void auth_assert(void *ctx) {
  auth_ctx *auth = ctx;
  qa_q3_address address;
  bool has = qa_q3_client_admission_get_address(auth->n->admission, &address);
  bool same = has && qa_q3_mirror_same_address(&address, &auth->peer, true);
  if (has) qa_q3_mirror_address_destroy(&address);
  if (auth->n->state == QA_APP_PHASE_CLOSED || auth->n->state == QA_APP_PHASE_REJECTED
      || qa_q3_client_admission_get_phase(auth->n->admission) != QA_Q3_PHASE_CONNECTING || !same)
    qa_range_error("Q3 authorization belongs to a retired connection");
}
static void auth_send(void *ctx, const qa_q3_address *address,
    const uint8_t *payload, size_t length) {
  auth_ctx *auth = ctx;
  transport_send_q3(auth->n, address, payload, length);
}
void qa_app_q3_client_network_poll(qa_app_q3_client_network *n, double now) {
  if (n->state == QA_APP_PHASE_CLOSED || n->state == QA_APP_PHASE_REJECTED) return;
  qa_q3_outgoing_datagram request;
  if (qa_q3_client_admission_resend(n->admission, now,
          n->options.host.userinfo(n->options.host.ctx), &request)) {
    if (request.to.kind != QA_Q3_ADDR_LOOPBACK) {
      qa_q3_address peer;
      qa_q3_mirror_address_copy(&peer, &request.to);
      bool lan = qa_q3_is_lan_address(&peer);
      if (qa_q3_client_admission_get_phase(n->admission) == QA_Q3_PHASE_CONNECTING && !lan) {
        if (!n->options.has_authorization) {
          qa_q3_mirror_address_destroy(&peer);
          qa_q3_outgoing_datagram_destroy(&request);
          qa_range_error("Q3 WAN admission requires the client key authorization owner");
        }
        auth_ctx auth = {n, peer};
        n->options.authorization.request(n->options.authorization.ctx,
            auth_assert, &auth, auth_send, &auth);
        auth_assert(&auth);
      }
      transport_send_q3(n, &peer, request.payload, request.payload_len);
      qa_q3_mirror_address_destroy(&peer);
    }
    qa_q3_outgoing_datagram_destroy(&request);
  }
  for (;;) {
    qa_receive_event packet;
    if (!n->options.transport.poll(n->options.transport.ctx, &packet)) break;
    if (packet.kind != QA_RECV_PACKET
        || (packet.from.kind != QA_NET_IPV4 && packet.from.kind != QA_NET_IPX)) {
      qa_receive_event_destroy(&packet);
      continue;
    }
    qa_q3_address from;
    if (!net_to_q3(&packet.from, &from)) { qa_receive_event_destroy(&packet); continue; }
    qa_q3_client_admission_result result;
    qa_q3_client_admission_receive(n->admission, &from, packet.payload, packet.length, now, &result);
    if (result.kind == QA_Q3_ADMIT_ADMITTED) {
      if (result.address.kind == QA_Q3_ADDR_LOOPBACK) {
        qa_q3_mirror_address_destroy(&from);
        qa_q3_client_admission_result_destroy(&result);
        qa_receive_event_destroy(&packet);
        qa_range_error("Q3 remote application requires IPv4 or IPX");
      }
      if (n->peer_owned) qa_q3_mirror_address_destroy(&n->peer);
      qa_q3_mirror_address_copy(&n->peer, &result.address);
      n->peer_owned = true;
      n->state = QA_APP_PHASE_LOADING;
      n->last_received = now;
      lifecycle_ctx *lc = malloc(sizeof *lc);
      if (!lc) qa_range_error("out of memory");
      lc->n = n; lc->connection = NULL;
      qa_app_q3_client_lifecycle lifecycle = {lc, lc_assert, lc_cleared, lc_primed, lc_snapshot};
      qa_app_q3_application_client_bindings(&n->bindings, n->options.host, lifecycle);
      qa_q3_client_mode mode = {0};
      mode.kind = QA_Q3_CLIENT_MODE_NETWORK;
      mode.challenge = result.challenge; mode.qport = result.qport;
      qa_q3_client_connection *connection = NULL;
      if (!qa_q3_client_connection_init(&connection, n->options.host.identity,
              QA_Q3_PRODUCT_BASEQ3, &mode, &n->bindings.bindings)) {
        free(lc);
        qa_range_error("out of memory");
      }
      lc->connection = connection;
      n->connection = connection;
      n->options.host.attach(n->options.host.ctx, connection);
      /* lc intentionally retained for the connection lifetime. */
    } else if (result.kind == QA_Q3_ADMIT_SEQUENCED && n->connection) {
      /* TS catch: state rejected + disconnected + rethrow. Landed
       * receive errors abort inside the call. */
      qa_q3_client_packet_result packet_result;
      qa_q3_client_connection_receive_datagram(n->connection, result.bytes,
          result.byte_len, now, &packet_result);
      /* Accepted packets are identified by plaintext presence in the
       * landed result. */
      if (packet_result.plaintext && !qa_q3_client_connection_demo_waiting(n->connection)
          && n->has_sink) {
        n->sink.append(n->sink.ctx,
            (double)qa_q3_client_connection_server_message_sequence(n->connection),
            packet_result.plaintext, packet_result.plaintext_len);
      }
      qa_q3_client_packet_result_destroy(&packet_result);
      n->last_received = now;
    } else if (result.kind == QA_Q3_ADMIT_CONNECTIONLESS) {
      if (strcmp(result.packet.command, "print") == 0) {
        size_t length = result.packet.payload_len;
        uint8_t *text = malloc(length + 1);
        if (!text) qa_range_error("out of memory");
        memcpy(text, result.packet.payload, length);
        text[length] = 0;
        qa_q3_message_reader *reader = NULL;
        if (!qa_q3_message_reader_init(&reader, text, length + 1, QA_Q3_MESSAGE_OOB, "oob"))
          qa_range_error("out of memory");
        char *value = NULL;
        qa_q3_message_reader_read_string(reader, &value);
        n->options.host.print(n->options.host.ctx, value ? value : "");
        free(value);
        qa_q3_message_reader_destroy(reader);
        free(text);
      } else if (strcmp(result.packet.command, "disconnect") == 0 && n->connection
          && qa_q3_mirror_same_address(&from, &n->peer, true)
          && now - n->last_received >= 3000) {
        n->state = QA_APP_PHASE_CLOSED;
        n->options.host.disconnected(n->options.host.ctx, "Server disconnected");
        qa_q3_mirror_address_destroy(&from);
        qa_q3_client_admission_result_destroy(&result);
        qa_receive_event_destroy(&packet);
        return;
      }
    }
    qa_q3_mirror_address_destroy(&from);
    qa_q3_client_admission_result_destroy(&result);
    qa_receive_event_destroy(&packet);
  }
  qa_q3_client_connection *connection = n->connection;
  if (connection) {
    const char *userinfo = n->options.host.userinfo(n->options.host.ctx);
    if (strcmp(userinfo, n->last_userinfo) != 0) {
      size_t length = strlen(userinfo) + 12;
      char *command = malloc(length);
      if (!command) qa_range_error("out of memory");
      snprintf(command, length, "userinfo \"%s\"", userinfo);
      qa_q3_client_reliable_commands_add(qa_q3_client_connection_reliable(connection), command, NULL);
      free(command);
      free(n->last_userinfo);
      n->last_userinfo = malloc(strlen(userinfo) + 1);
      if (!n->last_userinfo) qa_range_error("out of memory");
      strcpy(n->last_userinfo, userinfo);
    }
    double timeout = n->options.has_timeout_milliseconds ? n->options.timeout_milliseconds : 120000;
    if (now - n->last_received > timeout) {
      n->state = QA_APP_PHASE_REJECTED;
      n->options.host.disconnected(n->options.host.ctx, "Q3 connection timed out");
      return;
    }
    if (n->primed && !n->entered) {
      qa_q3_wire_user_command entered = {0};
      qa_q3_wire_command_history_append(qa_q3_client_connection_commands(connection), &entered);
      n->entered = true;
    }
    qa_q3_client_send_readiness readiness = {now, n->state == QA_APP_PHASE_ACTIVE,
        n->primed, false, n->options.host.downloading(n->options.host.ctx), false,
        qa_q3_is_lan_address(&n->peer), cvar_integer(n->options.cvars, "cl_maxpackets", 30)};
    if (qa_q3_client_connection_ready_to_send(connection, &readiness)) send_now(n, now);
  }
}
void qa_app_q3_client_network_submit(qa_app_q3_client_network *network,
    const void *const *commands, size_t count, double now) {
  (void)now;
  if (network->state != QA_APP_PHASE_ACTIVE || !network->connection) return;
  if (count > 1) qa_range_error("A native Q3 connection carries one player");
  for (size_t i = 0; i < count; i++) {
    qa_q3_wire_user_command command =
        network->options.host.command(network->options.host.ctx, commands[i]);
    qa_q3_wire_command_history_append(
        qa_q3_client_connection_commands(network->connection), &command);
  }
}
void qa_app_q3_client_network_publish(qa_app_q3_client_network *network,
    const void *output, const void *const *events, size_t count, double now) {
  (void)network; (void)output; (void)events; (void)count; (void)now;
  qa_range_error("Remote client cannot publish authoritative state");
}
void qa_app_q3_client_network_close(qa_app_q3_client_network *network) {
  if (network->options.transport.is_closed(network->options.transport.ctx)) return;
  if (network->connection && network->state != QA_APP_PHASE_CLOSED
      && network->state != QA_APP_PHASE_REJECTED) {
    qa_q3_client_send_options options = {
        network->options.now(network->options.now_ctx),
        cvar_integer(network->options.cvars, "cl_packetdup", 1), false};
    qa_q3_channel_delivery_storage storage;
    qa_q3_client_connection_disconnect_packets(network->connection, &options,
        make_delivery(network, network->connection, &storage));
  }
  network->state = QA_APP_PHASE_CLOSED;
  qa_q3_client_admission_disconnect(network->admission);
  network->options.transport.close(network->options.transport.ctx);
}
void qa_app_q3_client_network_recording_seed(qa_app_q3_client_network *network,
    qa_app_q3_recording_seed *out) {
  if (!network->connection || network->state != QA_APP_PHASE_ACTIVE)
    qa_range_error("Recording requires an active Q3 connection");
  int64_t sequence = 0;
  if (!qa_q3_demo_gamestate(network->connection, &sequence, &out->message, &out->length))
    qa_range_error("out of memory");
  out->sequence = (double)sequence;
}
void *qa_app_q3_client_network_recording_attach(qa_app_q3_client_network *network,
    qa_app_q3_recording_sink sink) {
  if (network->has_sink) qa_range_error("Q3 recording is already attached");
  if (!network->connection || network->state != QA_APP_PHASE_ACTIVE)
    qa_range_error("Recording requires an active Q3 connection");
  qa_q3_client_connection_set_demo_waiting(network->connection, true);
  network->sink = sink; network->has_sink = true;
  network->sink_token = malloc(1);
  return network->sink_token;
}
void qa_app_q3_client_network_recording_detach(qa_app_q3_client_network *network, void *token) {
  if (network->has_sink && network->sink_token == token) {
    network->has_sink = false;
    free(network->sink_token); network->sink_token = NULL;
  }
}
static qa_app_network_phase net_phase(void *ctx) {
  return qa_app_q3_client_network_phase(ctx);
}
static const void *net_wire(void *ctx) { (void)ctx; return "q3:68"; }
static const void *const *net_poll(void *ctx, double now, size_t *out_count) {
  qa_app_q3_client_network_poll(ctx, now);
  *out_count = 0;
  return NULL;
}
static void net_submit(void *ctx, const void *const *commands, size_t count, double now) {
  qa_app_q3_client_network_submit(ctx, commands, count, now);
}
static void net_publish(void *ctx, const void *output, const void *const *events,
    size_t count, double now) {
  qa_app_q3_client_network_publish(ctx, output, events, count, now);
}
static void net_close(void *ctx) { qa_app_q3_client_network_close(ctx); }
qa_app_application_network qa_app_q3_client_network_as_network(qa_app_q3_client_network *network) {
  qa_app_application_network result = {0};
  result.ctx = network; result.role = QA_APP_ROLE_CLIENT;
  result.phase = net_phase; result.wire = net_wire;
  result.poll = net_poll; result.submit = net_submit;
  result.publish = net_publish; result.close = net_close;
  return result;
}
