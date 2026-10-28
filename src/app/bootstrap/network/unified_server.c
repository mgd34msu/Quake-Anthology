/* Port of src/app/bootstrap/network/unified-server.ts.
 * GPL-2.0-or-later. */
#include "qa/app/bootstrap/network/unified_server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/network/unified_event_codec.h"
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
static bool client_equal(qa_client_id a, qa_client_id b) {
  return a.slot == b.slot && a.generation == b.generation && a.token == b.token;
}
static void random_token(char out[33]) {
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
typedef struct pending_peer {
  qa_net_address address; char *key; char nonce[33]; char token[33]; double created;
} pending_peer;
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
} peer;
typedef struct queued_command {
  qa_actor_command command; qa_arsenal_intent arsenal;
} queued_command;
struct qa_app_unified_server_network {
  qa_app_unified_server_options options;
  qa_app_unified_server_host host;
  qa_app_unified_composition *composition;
  bool ended;
  double epoch, now;
  pending_peer *pending; size_t pending_count, pending_cap;
  peer **peers; size_t peer_count, peer_cap;
  queued_command *commands; size_t command_count, command_cap;
  qa_actor_command *poll_result; size_t poll_cap;
  const void **poll_ptrs; size_t poll_ptr_cap;
};
static double queue_control(peer *p, const qa_app_unified_control *control) {
  size_t length = 0;
  uint8_t *bytes = qa_app_unified_encode_control(control, &length);
  if (!bytes) qa_range_error("out of memory");
  qa_unified_channel_queue_reliable(&p->channel, bytes, length);
  free(bytes);
  p->required_reliable = (double)p->channel.reliable_sequence;
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
static void flush_peer(qa_app_unified_server_network *n, peer *p, double now) {
  size_t *lengths = NULL;
  size_t count = 0;
  uint8_t **packets = qa_unified_channel_transmit_reliable(&p->channel, now, &lengths, &count);
  for (size_t i = 0; i < count; i++) {
    bool sent = n->options.transport->send(n->options.transport->ctx,
        &p->address, packets[i], lengths[i]);
    free(packets[i]);
    if (!sent) {
      for (size_t j = i + 1; j < count; j++) free(packets[j]);
      free(packets);
      free(lengths);
      qa_range_error("Unified transport rejected an outgoing datagram");
    }
  }
  free(packets);
  free(lengths);
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
static void drop_peer(qa_app_unified_server_network *n, peer *p, const char *reason, double now) {
  if (p->closing) return;
  p->closing = true;
  p->closing_since = now;
  p->ready = false;
  if (p->has_player) {
    qa_app_unified_player player = p->player;
    p->has_player = false;
    size_t kept = 0;
    for (size_t i = 0; i < n->command_count; i++)
      if (!qa_same_actor(n->commands[i].command.actor, player.actor))
        n->commands[kept++] = n->commands[i];
    n->command_count = kept;
    n->host.disconnect(n->host.ctx, player);
  }
  char *key = qa_net_address_key(&p->address, true);
  if (!key) qa_range_error("out of memory");
  size_t total = strlen(key) + strlen(reason) + 40;
  char *text = xmalloc(total);
  snprintf(text, total, "Unified peer %s disconnected: %s\n", key, reason);
  free(key);
  n->options.print(n->options.print_ctx, text);
  free(text);
  /* TS catches queue/flush failure and removes the peer; landed
   * queue aborts on an unacknowledged reliable instead. */
  char trimmed[1025];
  size_t length = strlen(reason);
  if (length > 1024) length = 1024;
  memcpy(trimmed, reason, length);
  trimmed[length] = '\0';
  qa_app_unified_control control = {0};
  control.kind = QA_APP_UNIFIED_DISCONNECT;
  control.reason = trimmed;
  queue_control(p, &control);
  flush_peer(n, p, now);
}
static void accept_control(qa_app_unified_server_network *n, peer *p,
    const uint8_t *bytes, size_t length, double now) {
  qa_app_unified_control control = qa_app_unified_decode_control(bytes, length);
  if (control.kind == QA_APP_UNIFIED_DISCONNECT) {
    char *reason = copy_text(control.reason ? control.reason : "");
    qa_app_unified_control_free(&control);
    drop_peer(n, p, reason, now);
    free(reason);
    return;
  }
  if (control.epoch != n->epoch) {
    qa_app_unified_control_free(&control);
    return;
  }
  if (control.kind == QA_APP_UNIFIED_READY) {
    if (!control.composition_digest
        || strcmp(control.composition_digest, n->composition->digest) != 0) {
      qa_app_unified_control_free(&control);
      qa_range_error("Client content composition differs from the server");
    }
    if (p->ready) {
      qa_app_unified_control_free(&control);
      return;
    }
    if (!p->has_player) {
      qa_app_unified_admission admission =
          n->host.admit(n->host.ctx, &p->address, control.userinfo);
      if (!admission.accepted) {
        char *reason = copy_text(admission.reason ? admission.reason : "rejected");
        qa_app_unified_control_free(&control);
        qa_range_error("%s", reason);
      }
      p->player = admission.player;
      p->has_player = true;
    } else n->host.userinfo(n->host.ctx, p->player, control.userinfo);
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
      uint8_t *payload = qa_app_unified_encode_presentation_events(
          (qa_save_value *const *)initial, initial_count, &payload_length);
      if (!payload) qa_range_error("out of memory");
      qa_save_value *empty = qa_save_value_array();
      if (!empty) qa_range_error("out of memory");
      size_t simulation_length = 0;
      uint8_t *simulation = qa_encode_checkpoint_value(empty, &simulation_length);
      qa_save_value_free(empty);
      if (!simulation) qa_range_error("out of memory");
      qa_app_unified_control events = {0};
      events.kind = QA_APP_UNIFIED_EVENTS;
      events.epoch = n->epoch;
      events.frame = 0;
      events.payload = payload;
      events.payload_length = payload_length;
      events.simulation = simulation;
      events.simulation_length = simulation_length;
      queue_control(p, &events);
      free(payload);
      free(simulation);
    }
    p->ready = true;
    qa_app_unified_control_free(&control);
    return;
  }
  if (!p->has_player || !p->ready) {
    qa_app_unified_control_free(&control);
    qa_range_error("Client command arrived before world admission");
  }
  if (control.kind == QA_APP_UNIFIED_COMPONENT_COMMAND) {
    if (!n->host.component_command) {
      qa_app_unified_control_free(&control);
      qa_range_error("No component command receiver");
    }
    bool accepted = n->host.component_command(n->host.ctx, p->player, control.owner,
        control.generation, control.args, control.arg_count);
    qa_app_unified_control_free(&control);
    if (!accepted) qa_range_error("Component command source or recipient is not admitted");
  } else if (control.kind == QA_APP_UNIFIED_COMMAND) {
    n->host.command(n->host.ctx, p->player, control.name, control.args, control.arg_count);
    qa_app_unified_control_free(&control);
  } else if (control.kind == QA_APP_UNIFIED_USERINFO) {
    n->host.userinfo(n->host.ctx, p->player, control.value);
    qa_app_unified_control_free(&control);
  } else {
    qa_app_unified_control_free(&control);
    qa_range_error("Client sent a server-only control message");
  }
}
static void accept_inputs(qa_app_unified_server_network *n, peer *p,
    const uint8_t *bytes, size_t length) {
  if (!p->has_player || !p->ready) return;
  qa_app_unified_input_batch batch = qa_app_unified_decode_inputs(bytes, length);
  if (batch.epoch != n->epoch) {
    qa_app_unified_input_batch_free(&batch);
    return;
  }
  for (size_t i = 0; i < batch.command_count; i++) {
    qa_app_unified_input *input = &batch.commands[i];
    if (input->sequence <= p->last_queued) continue;
    qa_actor_command command = n->host.input(n->host.ctx, p->player, input->sequence,
        &input->command, input->has_arsenal ? &input->arsenal : NULL);
    if (n->command_count == n->command_cap) {
      n->command_cap = n->command_cap ? n->command_cap * 2 : 64;
      n->commands = realloc(n->commands, n->command_cap * sizeof *n->commands);
      if (!n->commands) qa_range_error("out of memory");
    }
    queued_command *queued = &n->commands[n->command_count++];
    queued->command = command;
    if (command.arsenal) {
      queued->arsenal = *command.arsenal;
      queued->command.arsenal = &queued->arsenal;
    }
    p->last_queued = input->sequence;
  }
  qa_app_unified_input_batch_free(&batch);
}
qa_app_unified_server_network *qa_app_unified_server_network_create(
    qa_app_unified_server_options options) {
  qa_app_unified_server_network *n = calloc(1, sizeof *n);
  if (!n) return NULL;
  n->options = options;
  n->host = options.host;
  n->composition = options.composition;
  n->epoch = 1;
  return n;
}
void qa_app_unified_server_network_destroy(qa_app_unified_server_network *network) {
  if (!network) return;
  for (size_t i = 0; i < network->peer_count; i++) peer_destroy(network->peers[i]);
  free(network->peers);
  for (size_t i = 0; i < network->pending_count; i++) {
    qa_net_address_destroy(&network->pending[i].address);
    free(network->pending[i].key);
  }
  free(network->pending);
  free(network->commands);
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
  for (size_t i = 0; i < n->pending_count;) {
    if (now - n->pending[i].created > 10000) {
      qa_net_address_destroy(&n->pending[i].address);
      free(n->pending[i].key);
      memmove(n->pending + i, n->pending + i + 1,
          (n->pending_count - i - 1) * sizeof *n->pending);
      n->pending_count--;
      continue;
    }
    i++;
  }
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
      /* TS catches per-message failures into drop(); landed
       * decoders abort with the same messages. */
      qa_unified_receive_result result;
      qa_unified_channel_receive(&p->channel, event.payload, event.length, now, &result);
      for (size_t i = 0; i < result.reply_count; i++)
        n->options.transport->send(n->options.transport->ctx, &p->address,
            result.replies[i], result.reply_lens[i]);
      for (size_t i = 0; i < result.delivery_count; i++) {
        if (p->closing) break;
        qa_unified_delivery *message = &result.deliveries[i];
        if (strcmp(message->kind, "reliable") == 0)
          accept_control(n, p, message->payload, message->length, now);
        else if (strcmp(message->kind, "frame") == 0)
          accept_inputs(n, p, message->payload, message->length);
      }
      qa_unified_receive_result_destroy(&result);
      qa_receive_event_destroy(&event);
      continue;
    }
    qa_app_unified_handshake handshake;
    if (!qa_app_unified_decode_handshake(event.payload, event.length, &handshake)
        || handshake.kind == QA_APP_UNIFIED_CHALLENGE) {
      qa_receive_event_destroy(&event);
      continue;
    }
    char *key = qa_net_address_key(&event.from, true);
    if (!key) qa_range_error("out of memory");
    if (handshake.kind == QA_APP_UNIFIED_HELLO) {
      pending_peer *existing = NULL;
      for (size_t i = 0; i < n->pending_count; i++)
        if (strcmp(n->pending[i].key, key) == 0) existing = &n->pending[i];
      if (!existing && n->pending_count >= 256) {
        free(key);
        qa_receive_event_destroy(&event);
        continue;
      }
      pending_peer *pending = existing;
      if (!pending || strcmp(pending->nonce, handshake.nonce) != 0) {
        if (pending) {
          qa_net_address_destroy(&pending->address);
        } else {
          if (n->pending_count == n->pending_cap) {
            n->pending_cap = n->pending_cap ? n->pending_cap * 2 : 32;
            n->pending = realloc(n->pending, n->pending_cap * sizeof *n->pending);
            if (!n->pending) qa_range_error("out of memory");
          }
          pending = &n->pending[n->pending_count++];
          memset(pending, 0, sizeof *pending);
        }
        if (!qa_net_address_clone(&event.from, &pending->address)) qa_range_error("out of memory");
        free(pending->key);
        pending->key = copy_text(key);
        snprintf(pending->nonce, sizeof pending->nonce, "%s", handshake.nonce);
        random_token(pending->token);
        pending->created = now;
      }
      qa_app_unified_handshake challenge = {0};
      challenge.kind = QA_APP_UNIFIED_CHALLENGE;
      challenge.nonce = pending->nonce;
      challenge.token = pending->token;
      size_t length = 0;
      uint8_t *bytes = qa_app_unified_encode_handshake(&challenge, &length);
      if (!bytes) qa_range_error("out of memory");
      n->options.transport->send(n->options.transport->ctx, &event.from, bytes, length);
      free(bytes);
    } else {
      peer *existing = find_peer(n, handshake.token);
      if (existing && strcmp(existing->nonce, handshake.nonce) == 0
          && qa_net_same_address(&existing->address, &event.from, true)) {
        flush_peer(n, existing, now);
        free(key);
        qa_receive_event_destroy(&event);
        continue;
      }
      size_t index = n->pending_count;
      for (size_t i = 0; i < n->pending_count; i++)
        if (strcmp(n->pending[i].key, key) == 0) index = i;
      if (index == n->pending_count
          || strcmp(n->pending[index].token, handshake.token) != 0
          || strcmp(n->pending[index].nonce, handshake.nonce) != 0
          || (double)n->peer_count >= n->host.max_clients + 8) {
        free(key);
        qa_receive_event_destroy(&event);
        continue;
      }
      pending_peer pending = n->pending[index];
      memmove(n->pending + index, n->pending + index + 1,
          (n->pending_count - index - 1) * sizeof *n->pending);
      n->pending_count--;
      peer *p = calloc(1, sizeof *p);
      if (!p) qa_range_error("out of memory");
      p->address = pending.address;
      free(pending.key);
      snprintf(p->token, sizeof p->token, "%s", pending.token);
      snprintf(p->nonce, sizeof p->nonce, "%s", pending.nonce);
      int64_t max_datagram = n->options.transport->max_datagram_bytes(n->options.transport->ctx);
      int64_t datagram = max_datagram < 0 ? 1200 : max_datagram;
      if (datagram > 1200) datagram = 1200;
      qa_unified_channel_init(&p->channel, pending.token, datagram, 0, 0, 0, false);
      p->last_received = now;
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
    free(key);
    qa_receive_event_destroy(&event);
  }
  for (size_t i = 0; i < n->peer_count;) {
    peer *p = n->peers[i];
    if (p->closing && (now - p->closing_since > 1000
        || (double)p->channel.acknowledged_reliable >= p->required_reliable)) {
      remove_peer(n, p);
      continue;
    }
    if (!p->closing && now - p->last_received > 30000) {
      drop_peer(n, p, "Connection timed out", now);
      i++;
      continue;
    }
    flush_peer(n, p, now);
    i++;
  }
  /* Coalesce NetQuake bursts per actor slot. */
  if (n->poll_cap < n->command_count) {
    n->poll_result = realloc(n->poll_result, n->command_count * sizeof *n->poll_result);
    if (!n->poll_result) qa_range_error("out of memory");
    n->poll_cap = n->command_count;
  }
  size_t result_count = 0;
  for (size_t i = 0; i < n->command_count; i++) {
    queued_command *entry = &n->commands[i];
    if (entry->command.command.kind != QA_USER_COMMAND_Q1) {
      n->poll_result[result_count++] = entry->command;
      continue;
    }
    size_t latest = i;
    for (size_t j = i + 1; j < n->command_count; j++)
      if (n->commands[j].command.actor.slot == entry->command.actor.slot
          && n->commands[j].command.command.kind == QA_USER_COMMAND_Q1) latest = j;
    if (latest != i) continue;
    qa_actor_command merged = entry->command;
    /* Merge impulse/arsenal from earlier same-slot Q1 commands. */
    for (size_t j = 0; j < i; j++) {
      queued_command *previous = &n->commands[j];
      if (previous->command.actor.slot != entry->command.actor.slot
          || previous->command.command.kind != QA_USER_COMMAND_Q1) continue;
      if (merged.command.as.q1.impulse == 0)
        merged.command.as.q1.impulse = previous->command.command.as.q1.impulse;
      if (previous->command.arsenal
          && (!merged.arsenal || merged.arsenal->provider == previous->command.arsenal->provider)) {
        if (!merged.arsenal) {
          n->commands[i].arsenal = *previous->command.arsenal;
          merged.arsenal = &n->commands[i].arsenal;
        }
        qa_arsenal_intent *arsenal = &n->commands[i].arsenal;
        if (!arsenal->weapon && previous->command.arsenal->weapon)
          arsenal->weapon = previous->command.arsenal->weapon;
        arsenal->use_holdable = arsenal->use_holdable || previous->command.arsenal->use_holdable;
        if (!arsenal->has_impulse && previous->command.arsenal->has_impulse) {
          arsenal->has_impulse = true;
          arsenal->impulse = previous->command.arsenal->impulse;
        } else if (arsenal->has_impulse && arsenal->impulse == 0
            && previous->command.arsenal->has_impulse)
          arsenal->impulse = previous->command.arsenal->impulse;
        n->commands[i].command.arsenal = arsenal;
        merged.arsenal = arsenal;
      }
    }
    n->poll_result[result_count++] = merged;
  }
  for (size_t i = 0; i < n->peer_count; i++) {
    peer *p = n->peers[i];
    if (!p->has_player) continue;
    for (size_t j = 0; j < n->command_count; j++) {
      queued_command *entry = &n->commands[j];
      if (entry->command.actor.slot == p->player.actor.slot
          && qa_same_actor(entry->command.actor, p->player.actor)
          && entry->command.sequence > p->last_submitted)
        p->last_submitted = entry->command.sequence;
    }
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
static void publish_peer(qa_app_unified_server_network *n, peer *p,
    const void *output, const void *const *events, size_t event_count, double now) {
  qa_app_unified_player player = p->player;
  size_t resource_count = 0;
  const qa_app_unified_resource_key *resources =
      n->host.resources(n->host.ctx, player, output, &resource_count);
  const qa_app_unified_resource_key *fresh = xmalloc(resource_count * sizeof *fresh);
  size_t fresh_count = 0;
  for (size_t i = 0; i < resource_count; i++) {
    char *id = qa_app_unified_resource_id_from_key(&resources[i]);
    if (!id) qa_range_error("out of memory");
    bool known = false;
    for (size_t j = 0; j < p->resource_count; j++)
      if (strcmp(p->resources[j], id) == 0) known = true;
    if (known) { free(id); continue; }
    ((qa_app_unified_resource_key *)fresh)[fresh_count++] = resources[i];
    if (p->resource_count == p->resource_cap) {
      p->resource_cap = p->resource_cap ? p->resource_cap * 2 : 16;
      p->resources = realloc(p->resources, p->resource_cap * sizeof *p->resources);
      if (!p->resources) qa_range_error("out of memory");
    }
    p->resources[p->resource_count++] = id;
  }
  if (fresh_count > 0) {
    qa_app_unified_control control = {0};
    control.kind = QA_APP_UNIFIED_RESOURCES;
    control.epoch = n->epoch;
    control.resources = (qa_app_unified_resource_key *)fresh;
    control.resource_count = fresh_count;
    queue_control(p, &control);
  }
  free((void *)fresh);
  size_t presentation_count = 0;
  qa_save_value *const *presentation = n->host.presentation_events
      ? n->host.presentation_events(n->host.ctx, player, events, event_count, &presentation_count)
      : NULL;
  qa_app_unified_presentation_frame_t frame =
      n->host.frame(n->host.ctx, player, output, n->epoch, p->last_consumed);
  if (presentation_count > 0 || frame.event_count > 0) {
    size_t payload_length = 0;
    uint8_t *payload = qa_app_unified_encode_presentation_events(
        (qa_save_value *const *)presentation, presentation_count, &payload_length);
    if (!payload) qa_range_error("out of memory");
    qa_save_value *list = qa_save_value_array();
    if (list)
      for (size_t i = 0; i < frame.event_count; i++)
        if (!qa_save_value_push(list, qa_app_unified_write_simulation_event(frame.events[i])))
          qa_range_error("out of memory");
    if (!list) qa_range_error("out of memory");
    size_t simulation_length = 0;
    uint8_t *simulation = qa_encode_checkpoint_value(list, &simulation_length);
    qa_save_value_free(list);
    if (!simulation) qa_range_error("out of memory");
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
    free(payload);
    free(simulation);
  }
  size_t source_count = 0, native_count = 0;
  const qa_app_unified_component_publication *sources = n->host.components
      ? n->host.components(n->host.ctx, player, &source_count) : NULL;
  const qa_app_unified_native_publication *native = n->host.native_components
      ? n->host.native_components(n->host.ctx, player, &native_count) : NULL;
  qa_app_unified_component_projection projection =
      qa_app_unified_component_publisher_project(p->components, sources, source_count,
          native, native_count);
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
  uint8_t *encoded = qa_app_unified_encode_frame(&frame, &encoder, &length);
  if (!encoded) qa_range_error("out of memory");
  size_t packet_length = 0;
  uint8_t *packet = qa_unified_channel_transmit_frame(&p->channel, encoded, length,
      true, (int64_t)p->required_reliable, &packet_length);
  free(encoded);
  if (!packet) qa_range_error("out of memory");
  bool sent = n->options.transport->send(n->options.transport->ctx, &p->address,
      packet, packet_length);
  free(packet);
  if (!sent) qa_range_error("Unified transport rejected an outgoing datagram");
  qa_app_unified_component_projection_free(&projection);
  memset(&frame.components, 0, sizeof frame.components);
  frame.has_components = false;
  qa_app_unified_presentation_frame_free(&frame);
  flush_peer(n, p, now);
}
void qa_app_unified_server_network_publish(qa_app_unified_server_network *n,
    const void *output, const void *const *events, size_t count, double now) {
  if (n->ended) return;
  for (size_t i = 0; i < n->peer_count; i++) {
    peer *p = n->peers[i];
    if (!p->has_player || !p->ready || p->closing) continue;
    p->last_consumed = p->last_submitted;
    publish_peer(n, p, output, events, count, now);
  }
}
void qa_app_unified_server_network_change_world(qa_app_unified_server_network *n,
    qa_app_unified_server_host host, qa_app_unified_composition *composition) {
  if (n->ended) qa_range_error("Unified server is closed");
  if (n->epoch == 4294967295.0) qa_range_error("Unified world sequence exhausted");
  n->host = host;
  n->composition = composition;
  n->epoch++;
  n->command_count = 0;
  for (size_t i = 0; i < n->peer_count; i++) {
    peer *p = n->peers[i];
    if (p->closing) continue;
    if (p->has_player) p->player = host.carried_player(host.ctx, p->player.client);
    offer(n, p);
  }
}
void qa_app_unified_server_network_close(qa_app_unified_server_network *n) {
  if (n->ended) return;
  n->ended = true;
  for (size_t i = 0; i < n->peer_count; i++) {
    peer *p = n->peers[i];
    if (p->has_player) n->host.disconnect(n->host.ctx, p->player);
    peer_destroy(p);
  }
  n->peer_count = 0;
  for (size_t i = 0; i < n->pending_count; i++) {
    qa_net_address_destroy(&n->pending[i].address);
    free(n->pending[i].key);
  }
  n->pending_count = 0;
  n->command_count = 0;
  n->options.transport->close(n->options.transport->ctx);
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
