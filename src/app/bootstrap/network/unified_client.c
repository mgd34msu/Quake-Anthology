/* Port of src/app/bootstrap/network/unified-client.ts.
 * GPL-2.0-or-later. */
#include "qa/app/bootstrap/network/unified_client.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/core/commands/text.h"
#include "qa/foundation/support.h"
#include "qa/network/unified/channel.h"
#include "qa/network/unified/packet.h"
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
struct qa_app_unified_client_network {
  qa_app_unified_client_options options;
  qa_app_network_phase state;
  char nonce[33];
  char *token;
  qa_unified_channel channel; bool channel_live, channel_closed;
  qa_app_unified_composition *composition;
  double epoch, generation;
  double last_handshake;
  bool has_last_received; double last_received;
  bool has_handshake_started; double handshake_started;
  char *last_userinfo;
  qa_app_unified_input *inputs; size_t input_count, input_cap;
  double acknowledged;
  bool has_command_time; double command_time;
  bool has_submitted_at; double submitted_at;
};
static void make_nonce(char out[33]) {
  unsigned char bytes[16] = {0};
  FILE *file = fopen("/dev/urandom", "rb");
  if (file) {
    size_t read_count = fread(bytes, 1, sizeof bytes, file);
    (void)read_count;
    fclose(file);
  }
  static const char hex[] = "0123456789abcdef";
  for (size_t i = 0; i < 16; i++) {
    out[i * 2] = hex[bytes[i] >> 4];
    out[i * 2 + 1] = hex[bytes[i] & 15];
  }
  out[32] = '\0';
}
qa_app_unified_client_network *qa_app_unified_client_network_create(
    qa_app_unified_client_options options) {
  qa_app_unified_client_network *n = calloc(1, sizeof *n);
  if (!n) return NULL;
  n->options = options;
  n->state = QA_APP_PHASE_CHALLENGING;
  make_nonce(n->nonce);
  n->acknowledged = -1;
  n->last_handshake = -1.0 / 0.0;
  n->last_userinfo = copy_text("");
  return n;
}
void qa_app_unified_client_network_destroy(qa_app_unified_client_network *network) {
  if (!network) return;
  free(network->token);
  if (network->channel_live) qa_unified_channel_destroy(&network->channel);
  if (network->composition) qa_app_unified_composition_destroy(network->composition);
  free(network->last_userinfo);
  for (size_t i = 0; i < network->input_count; i++) {
    qa_app_unified_input *input = &network->inputs[i];
    (void)input;
  }
  free(network->inputs);
  free(network);
}
const qa_app_unified_client_options *qa_app_unified_client_network_options_of(
    const qa_app_unified_client_network *network) { return &network->options; }
qa_app_network_phase qa_app_unified_client_network_phase(
    const qa_app_unified_client_network *network) { return network->state; }
void qa_app_unified_client_network_wire(const qa_app_unified_client_network *network,
    const char **composition_digest, const qa_save_value **snapshot_schema) {
  if (!network->composition) qa_range_error("Unified composition has not been negotiated");
  *composition_digest = network->composition->digest;
  *snapshot_schema = qa_save_value_get(network->composition->recipe, "snapshotSchema");
}
static void send_control(qa_app_unified_client_network *n, const qa_app_unified_control *control) {
  if (!n->channel_live) qa_range_error("Unified connection has no channel");
  size_t length = 0;
  uint8_t *bytes = qa_app_unified_encode_control(control, &length);
  if (!bytes) qa_range_error("out of memory");
  qa_unified_channel_queue_reliable(&n->channel, bytes, length);
  free(bytes);
}
static void assert_current(qa_app_unified_client_network *n, double generation) {
  if (n->state == QA_APP_PHASE_CLOSED || n->state == QA_APP_PHASE_REJECTED
      || generation != n->generation)
    qa_range_error("Unified channel operation was retired");
}
static void send_packet(qa_app_unified_client_network *n, const uint8_t *bytes, size_t length) {
  n->options.transport->send(n->options.transport->ctx, &n->options.remote, bytes, length);
}
static void flush_channel(qa_app_unified_client_network *n, double now) {
  size_t *lengths = NULL;
  size_t count = 0;
  uint8_t **packets = qa_unified_channel_transmit_reliable(&n->channel, now, &lengths, &count);
  for (size_t i = 0; i < count; i++) {
    send_packet(n, packets[i], lengths[i]);
    free(packets[i]);
  }
  free(packets);
  free(lengths);
}
static void fail(qa_app_unified_client_network *n, const char *message) {
  if (n->state != QA_APP_PHASE_CLOSED) {
    n->state = QA_APP_PHASE_REJECTED;
    qa_app_unified_remote_disconnected(n->options.host, message);
    qa_app_unified_client_network_close(n);
  }
  qa_range_error("%s", message);
}
const void *const *qa_app_unified_client_network_poll(qa_app_unified_client_network *n,
    double now, size_t *out_count) {
  *out_count = 0;
  if (n->state == QA_APP_PHASE_CLOSED || n->state == QA_APP_PHASE_REJECTED) return NULL;
  double generation = n->generation;
  if (!n->has_handshake_started) {
    n->handshake_started = now;
    n->has_handshake_started = true;
  }
  if (!n->composition && now - n->last_handshake >= 1000) {
    n->last_handshake = now;
    qa_app_unified_handshake handshake = {0};
    handshake.kind = n->token ? QA_APP_UNIFIED_CONNECT : QA_APP_UNIFIED_HELLO;
    handshake.nonce = n->nonce;
    handshake.token = n->token;
    size_t length = 0;
    uint8_t *bytes = qa_app_unified_encode_handshake(&handshake, &length);
    if (!bytes) qa_range_error("out of memory");
    send_packet(n, bytes, length);
    free(bytes);
  }
  for (;;) {
    qa_receive_event packet;
    if (!n->options.transport->poll(n->options.transport->ctx, &packet)) break;
    if (packet.kind == QA_RECV_ERROR) {
      char *message = copy_text(packet.error ? packet.error : "transport error");
      qa_receive_event_destroy(&packet);
      fail(n, message);
    }
    if (packet.kind != QA_RECV_PACKET
        || !qa_net_same_address(&packet.from, &n->options.remote, true)) {
      qa_receive_event_destroy(&packet);
      continue;
    }
    qa_app_unified_handshake handshake;
    if (qa_app_unified_decode_handshake(packet.payload, packet.length, &handshake)) {
      if (handshake.kind == QA_APP_UNIFIED_CHALLENGE
          && strcmp(handshake.nonce, n->nonce) == 0 && !n->composition) {
        if (!n->token || strcmp(n->token, handshake.token) == 0) {
          free(n->token);
          n->token = copy_text(handshake.token);
          if (!n->channel_live) {
            int64_t max_datagram = n->options.transport->max_datagram_bytes(
                n->options.transport->ctx);
            int64_t datagram = max_datagram < 0 ? 1200 : max_datagram;
            if (datagram > 1200) datagram = 1200;
            qa_unified_channel_init(&n->channel, handshake.token, datagram, 0, 0, 0, false);
            n->channel_live = true;
          }
          n->state = QA_APP_PHASE_CONNECTING;
          n->last_handshake = now;
          qa_app_unified_handshake connect = {0};
          connect.kind = QA_APP_UNIFIED_CONNECT;
          connect.nonce = n->nonce;
          connect.token = handshake.token;
          size_t length = 0;
          uint8_t *bytes = qa_app_unified_encode_handshake(&connect, &length);
          if (!bytes) qa_range_error("out of memory");
          send_packet(n, bytes, length);
          free(bytes);
        }
      }
      qa_receive_event_destroy(&packet);
      continue;
    }
    qa_unified_packet decoded;
    if (!n->channel_live || !qa_decode_unified_packet(packet.payload, packet.length, &decoded)
        || strcmp(decoded.token, n->channel.token) != 0) {
      qa_receive_event_destroy(&packet);
      continue;
    }
    n->last_received = now;
    n->has_last_received = true;
    qa_unified_receive_result result;
    qa_unified_channel_receive(&n->channel, packet.payload, packet.length, now, &result);
    for (size_t i = 0; i < result.reply_count; i++) send_packet(n, result.replies[i], result.reply_lens[i]);
    for (size_t i = 0; i < result.delivery_count; i++) {
      qa_unified_delivery *message = &result.deliveries[i];
      if (strcmp(message->kind, "reliable") == 0) {
        qa_app_unified_control control =
            qa_app_unified_decode_control(message->payload, message->length);
        if (control.kind == QA_APP_UNIFIED_DISCONNECT) {
          n->state = QA_APP_PHASE_REJECTED;
          qa_app_unified_remote_disconnected(n->options.host,
              control.reason ? control.reason : "");
          qa_app_unified_control_free(&control);
          qa_unified_receive_result_destroy(&result);
          qa_receive_event_destroy(&packet);
          qa_app_unified_client_network_close(n);
          return NULL;
        }
        if (control.kind == QA_APP_UNIFIED_OFFER) {
          if (control.epoch <= n->epoch) {
            qa_app_unified_control_free(&control);
            continue;
          }
          n->state = QA_APP_PHASE_LOADING;
          n->epoch = control.epoch;
          n->input_count = 0;
          n->acknowledged = -1;
          n->has_command_time = false;
          n->has_submitted_at = false;
          qa_app_unified_remote_offer(n->options.host, &control);
          assert_current(n, generation);
          if (n->composition) qa_app_unified_composition_destroy(n->composition);
          n->composition = control.composition;
          control.composition = NULL;
          free(n->last_userinfo);
          n->last_userinfo = copy_text(n->options.userinfo(n->options.userinfo_ctx));
          qa_app_unified_control ready = {0};
          ready.kind = QA_APP_UNIFIED_READY;
          ready.epoch = n->epoch;
          ready.composition_digest = n->composition->digest;
          ready.userinfo = n->last_userinfo;
          send_control(n, &ready);
        } else if (control.epoch == n->epoch) {
          if (control.kind == QA_APP_UNIFIED_ADMITTED)
            qa_app_unified_remote_admitted(n->options.host, &control);
          else if (control.kind == QA_APP_UNIFIED_RESOURCES) {
            qa_app_unified_remote_declare(n->options.host, control.epoch,
                control.resources, control.resource_count);
            assert_current(n, generation);
          } else if (control.kind == QA_APP_UNIFIED_COMPONENTS) {
            qa_app_unified_remote_receive_components(n->options.host,
                control.epoch, &control.update);
            assert_current(n, generation);
          } else if (control.kind == QA_APP_UNIFIED_EVENTS) {
            qa_app_unified_remote_receive_events(n->options.host, control.epoch,
                control.frame, control.payload, control.payload_length,
                control.simulation, control.simulation_length);
          } else fail(n, "Unexpected unified server control");
        }
        qa_app_unified_control_free(&control);
      } else if (strcmp(message->kind, "frame") == 0) {
        double acknowledged = 0;
        bool accepted = qa_app_unified_remote_receive_frame(n->options.host,
            message->payload, message->length, &acknowledged);
        assert_current(n, generation);
        if (accepted) {
          n->state = QA_APP_PHASE_ACTIVE;
          if (acknowledged > n->acknowledged) n->acknowledged = acknowledged;
          size_t kept = 0;
          for (size_t j = 0; j < n->input_count; j++)
            if (n->inputs[j].sequence > n->acknowledged) n->inputs[kept++] = n->inputs[j];
          n->input_count = kept;
          if (n->input_count == 0) {
            double baseline = 0;
            if (qa_app_unified_remote_command_time(n->options.host, &baseline)) {
              n->command_time = baseline;
              n->has_command_time = true;
            }
          }
        }
      }
    }
    qa_unified_receive_result_destroy(&result);
    qa_receive_event_destroy(&packet);
  }
  double since = n->has_last_received ? n->last_received : n->handshake_started;
  if (now - since > 120000) fail(n, "Unified connection timed out");
  if (n->channel_live) {
    if (n->state == QA_APP_PHASE_ACTIVE) {
      const char *userinfo = n->options.userinfo(n->options.userinfo_ctx);
      if (strcmp(userinfo, n->last_userinfo) != 0) {
        qa_app_unified_control control = {0};
        control.kind = QA_APP_UNIFIED_USERINFO;
        control.epoch = n->epoch;
        control.value = userinfo;
        send_control(n, &control);
        free(n->last_userinfo);
        n->last_userinfo = copy_text(userinfo);
      }
      qa_app_unified_input_batch batch = {0};
      batch.epoch = n->epoch;
      batch.commands = n->inputs;
      batch.command_count = n->input_count;
      size_t length = 0;
      uint8_t *bytes = qa_app_unified_encode_inputs(&batch, &length);
      if (!bytes) qa_range_error("out of memory");
      size_t frame_length = 0;
      uint8_t *frame = qa_unified_channel_transmit_frame(&n->channel, bytes, length,
          true, 0, &frame_length);
      free(bytes);
      if (!frame) qa_range_error("out of memory");
      send_packet(n, frame, frame_length);
      free(frame);
    }
    flush_channel(n, now);
  }
  return NULL;
}
void qa_app_unified_client_network_submit(qa_app_unified_client_network *n,
    const qa_actor_command *commands, size_t count, double now) {
  double elapsed = n->has_submitted_at ? now - n->submitted_at : 0;
  if (elapsed < 0) elapsed = 0;
  qa_app_unified_client_network_submit_timed(n, commands, count, now, elapsed);
}
static double command_duration(const qa_user_command *command, double elapsed) {
  switch (command->kind) {
    case QA_USER_COMMAND_Q1:
    case QA_USER_COMMAND_Q3: return elapsed;
    case QA_USER_COMMAND_QW: return command->as.qw.milliseconds;
    case QA_USER_COMMAND_Q2: return command->as.q2.milliseconds;
    case QA_USER_COMMAND_Q2_RERELEASE: return command->as.q2_rerelease.milliseconds;
  }
  return elapsed;
}
void qa_app_unified_client_network_submit_timed(qa_app_unified_client_network *n,
    const qa_actor_command *commands, size_t count, double now, double elapsed) {
  if (!isfinite(elapsed) || elapsed < 0)
    qa_range_error("Unified input needs finite elapsed milliseconds");
  if (n->state != QA_APP_PHASE_ACTIVE) return;
  qa_actor_id player_actor;
  qa_client_id player_client;
  bool has_player = qa_app_unified_remote_player(n->options.host,
      &player_actor, &player_client, NULL);
  qa_client_id client_id = qa_app_unified_remote_client(n->options.host);
  for (size_t i = 0; i < count; i++) {
    const qa_actor_command *command = &commands[i];
    if (!has_player || !qa_same_actor(command->actor, player_actor)
        || command->source.kind != QA_COMMAND_SOURCE_LOCAL_SEAT
        || !(command->source.client.slot == client_id.slot
            && command->source.client.generation == client_id.generation
            && command->source.client.token == client_id.token))
      qa_range_error("Unified input belongs to another player");
    if (command->sequence <= n->acknowledged) continue;
    bool present = false;
    for (size_t j = 0; j < n->input_count; j++)
      if (n->inputs[j].sequence == command->sequence) present = true;
    if (present) continue;
    double baseline = 0;
    if (!qa_app_unified_remote_command_time(n->options.host, &baseline))
      qa_range_error("Unified input has no authoritative clock");
    qa_user_command timed = command->command;
    double duration = command_duration(&timed, elapsed);
    double base = n->has_command_time ? n->command_time : baseline;
    if (base < baseline) base = baseline;
    n->command_time = base + duration;
    n->has_command_time = true;
    n->submitted_at = now;
    n->has_submitted_at = true;
    if (timed.kind == QA_USER_COMMAND_Q3)
      timed.as.q3.server_time_milliseconds = trunc(n->command_time);
    else if (timed.kind == QA_USER_COMMAND_Q1)
      timed.as.q1.acknowledged_server_time_seconds = baseline / 1000;
    qa_app_prediction_command prediction = {0};
    prediction.sequence = command->sequence;
    prediction.time_milliseconds = n->command_time;
    prediction.command = timed;
    prediction.angle_space = "absolute";
    if (command->arsenal) {
      prediction.has_arsenal = true;
      prediction.arsenal = *command->arsenal;
    }
    qa_app_unified_remote_predict(n->options.host, prediction);
    if (n->input_count == n->input_cap) {
      n->input_cap = n->input_cap ? n->input_cap * 2 : 16;
      n->inputs = realloc(n->inputs, n->input_cap * sizeof *n->inputs);
      if (!n->inputs) qa_range_error("out of memory");
    }
    n->inputs[n->input_count].sequence = command->sequence;
    n->inputs[n->input_count].command = timed;
    n->inputs[n->input_count].has_arsenal = command->arsenal != NULL;
    if (command->arsenal) n->inputs[n->input_count].arsenal = *command->arsenal;
    n->input_count++;
    while (n->input_count > 64) {
      memmove(n->inputs, n->inputs + 1, (n->input_count - 1) * sizeof *n->inputs);
      n->input_count--;
    }
  }
}
void qa_app_unified_client_network_command(qa_app_unified_client_network *n,
    const char *text) {
  qa_command_tokens tokens;
  if (!qa_tokenize_command(text, QA_COMMAND_DIALECT_Q3, QA_COMMAND_TEXT_SOURCE, &tokens))
    qa_range_error("out of memory");
  if (tokens.argc == 0) {
    qa_command_tokens_destroy(&tokens);
    return;
  }
  qa_app_unified_client_network_player_command(n, tokens.argv[0],
      (const char *const *)tokens.argv + 1, tokens.argc - 1);
  qa_command_tokens_destroy(&tokens);
}
void qa_app_unified_client_network_player_command(qa_app_unified_client_network *n,
    const char *name, const char *const *args, size_t count) {
  if (n->state != QA_APP_PHASE_ACTIVE) return;
  qa_app_unified_control control = {0};
  control.kind = QA_APP_UNIFIED_COMMAND;
  control.epoch = n->epoch;
  control.name = name;
  control.args = (const char **)args;
  control.arg_count = count;
  send_control(n, &control);
}
void qa_app_unified_client_network_component_command(qa_app_unified_client_network *n,
    qa_presentation_owner owner, double generation, const char *const *args, size_t count) {
  if (n->state != QA_APP_PHASE_ACTIVE)
    qa_range_error("Component command requires an active client");
  qa_app_unified_control control = {0};
  control.kind = QA_APP_UNIFIED_COMPONENT_COMMAND;
  control.epoch = n->epoch;
  control.owner = owner;
  control.generation = generation;
  control.args = (const char **)args;
  control.arg_count = count;
  send_control(n, &control);
}
void qa_app_unified_client_network_userinfo(qa_app_unified_client_network *n,
    const char *value) {
  if (n->epoch == 0) return;
  qa_app_unified_control control = {0};
  control.kind = QA_APP_UNIFIED_USERINFO;
  control.epoch = n->epoch;
  control.value = value;
  send_control(n, &control);
}
void qa_app_unified_client_network_publish(qa_app_unified_client_network *n,
    const void *output, const void *const *events, size_t count, double now) {
  (void)n; (void)output; (void)events; (void)count; (void)now;
  qa_range_error("Unified client cannot publish authoritative state");
}
void qa_app_unified_client_network_close(qa_app_unified_client_network *n) {
  if (n->state == QA_APP_PHASE_CLOSED) return;
  if (n->channel_live && !n->channel_closed && n->epoch != 0) {
    qa_app_unified_control control = {0};
    control.kind = QA_APP_UNIFIED_DISCONNECT;
    control.reason = "Client disconnected";
    size_t length = 0;
    uint8_t *bytes = qa_app_unified_encode_control(&control, &length);
    if (bytes) {
      qa_unified_channel_queue_reliable(&n->channel, bytes, length);
      free(bytes);
      flush_channel(n, 0);
    }
  }
  n->state = QA_APP_PHASE_CLOSED;
  n->generation++;
  qa_app_unified_remote_close(n->options.host);
  n->channel_closed = true;
  n->input_count = 0;
  n->options.transport->close(n->options.transport->ctx);
}
static qa_app_network_phase net_phase(void *ctx) {
  return qa_app_unified_client_network_phase(ctx);
}
static const void *const *net_poll(void *ctx, double now, size_t *out_count) {
  return qa_app_unified_client_network_poll(ctx, now, out_count);
}
static void net_submit(void *ctx, const void *const *commands, size_t count, double now) {
  qa_app_unified_client_network *n = ctx;
  const qa_actor_command **list = (const qa_actor_command **)commands;
  for (size_t i = 0; i < count; i++)
    qa_app_unified_client_network_submit(n, list[i], 1, now);
}
static void net_publish(void *ctx, const void *output, const void *const *events,
    size_t count, double now) {
  qa_app_unified_client_network_publish(ctx, output, events, count, now);
}
static void net_close(void *ctx) { qa_app_unified_client_network_close(ctx); }
qa_app_application_network qa_app_unified_client_network_as_network(
    qa_app_unified_client_network *network) {
  qa_app_application_network result = {0};
  result.ctx = network;
  result.role = QA_APP_ROLE_CLIENT;
  result.phase = net_phase;
  result.poll = net_poll;
  result.submit = net_submit;
  result.publish = net_publish;
  result.close = net_close;
  return result;
}
