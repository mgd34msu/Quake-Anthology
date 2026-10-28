/* Port of src/network/q2/kex/lan.ts. GPL-2.0-or-later. */
#include "qa/network/q2/kex/lan.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/network/q2/constants.h"
#include "qa/network/q2/kex/packet.h"

static char *dup_str(const char *s) {
  size_t n = strlen(s);
  char *out = (char *)malloc(n + 1);
  if (out != NULL) memcpy(out, s, n + 1);
  return out;
}
static bool is_integer(double v) { return v == trunc(v) && isfinite(v); }
static bool now_is_negative_infinity(double v) { return isinf(v) && v < 0; }

static bool attr_set(qa_kex_lobby_attr **attrs, size_t *count, size_t *capacity,
                     const char *key, const char *value) {
  (void)capacity;
  for (size_t i = 0; i < *count; i++) {
    if (strcmp((*attrs)[i].key, key) == 0) {
      char *copy = dup_str(value);
      if (copy == NULL) return false;
      free((*attrs)[i].value);
      (*attrs)[i].value = copy;
      return true;
    }
  }
  if (*count == *capacity) {
    size_t next = *capacity > 0 ? *capacity * 2 : 8;
    qa_kex_lobby_attr *grown = (qa_kex_lobby_attr *)realloc(*attrs, next * sizeof *grown);
    if (grown == NULL) return false;
    *attrs = grown;
    *capacity = next;
  }
  char *k = dup_str(key);
  char *v = dup_str(value);
  if (k == NULL || v == NULL) {
    free(k);
    free(v);
    return false;
  }
  (*attrs)[*count].key = k;
  (*attrs)[*count].value = v;
  (*count)++;
  return true;
}
static void attr_delete(qa_kex_lobby_attr *attrs, size_t *count, const char *key) {
  for (size_t i = 0; i < *count; i++) {
    if (strcmp(attrs[i].key, key) == 0) {
      free(attrs[i].key);
      free(attrs[i].value);
      memmove(&attrs[i], &attrs[i + 1], (*count - i - 1) * sizeof *attrs);
      (*count)--;
      return;
    }
  }
}
static const char *attr_get(const qa_kex_lobby_attr *attrs, size_t count, const char *key) {
  for (size_t i = 0; i < count; i++) {
    if (strcmp(attrs[i].key, key) == 0) return attrs[i].value;
  }
  return NULL;
}
static void attrs_free(qa_kex_lobby_attr *attrs, size_t count) {
  for (size_t i = 0; i < count; i++) {
    free(attrs[i].key);
    free(attrs[i].value);
  }
  free(attrs);
}

static void player_free(qa_kex_lobby_player *player) {
  attrs_free(player->attributes, player->attribute_count);
  memset(player, 0, sizeof *player);
}
static void players_clear(qa_kex_lan_transport *lan) {
  for (size_t i = 0; i < lan->player_count; i++) player_free(&lan->players[i]);
  lan->player_count = 0;
}
static bool player_push(qa_kex_lan_transport *lan, uint64_t id) {
  if (lan->player_count == lan->player_capacity) {
    size_t next = lan->player_capacity > 0 ? lan->player_capacity * 2 : 8;
    qa_kex_lobby_player *grown =
        (qa_kex_lobby_player *)realloc(lan->players, next * sizeof *grown);
    if (grown == NULL) return false;
    lan->players = grown;
    lan->player_capacity = next;
  }
  memset(&lan->players[lan->player_count], 0, sizeof(qa_kex_lobby_player));
  lan->players[lan->player_count].id = id;
  lan->player_count++;
  return true;
}
static long player_index_of(const qa_kex_lan_transport *lan, uint64_t id) {
  for (size_t i = 0; i < lan->player_count; i++) {
    if (lan->players[i].id == id) return (long)i;
  }
  return -1;
}

/* Emit binding: peer address cloned for the channel lifetime. */
typedef struct emit_ctx {
  qa_kex_lan_transport *lan;
  qa_net_address address;
} emit_ctx;

static bool peer_emit(void *vctx, const uint8_t *bytes, size_t length) {
  emit_ctx *c = (emit_ctx *)vctx;
  return c->lan->transport->send(c->lan->transport->ctx, &c->address, bytes, length);
}

static void peer_destroy(qa_kex_lan_peer *peer) {
  if (peer->channel != NULL) {
    emit_ctx *ctx = (emit_ctx *)peer->channel->emit_ctx;
    qa_kex_channel_destroy(peer->channel);
    free(peer->channel);
    if (ctx != NULL) {
      qa_net_address_destroy(&ctx->address);
      free(ctx);
    }
  }
  qa_net_address_destroy(&peer->address);
  free(peer->players);
  memset(peer, 0, sizeof *peer);
}

static bool peer_push_player(qa_kex_lan_peer *peer, uint64_t id) {
  if (peer->player_count == peer->player_capacity) {
    size_t next = peer->player_capacity > 0 ? peer->player_capacity * 2 : 4;
    uint64_t *grown = (uint64_t *)realloc(peer->players, next * sizeof *grown);
    if (grown == NULL) return false;
    peer->players = grown;
    peer->player_capacity = next;
  }
  peer->players[peer->player_count++] = id;
  return true;
}

static qa_kex_lan_peer *peer_find(qa_kex_lan_transport *lan, const qa_net_address *address) {
  char *key = qa_net_address_key(address, true);
  if (key == NULL) return NULL;
  qa_kex_lan_peer *found = NULL;
  for (size_t i = 0; i < lan->peer_count; i++) {
    char *other = qa_net_address_key(&lan->peers[i].address, true);
    if (other != NULL && strcmp(other, key) == 0) found = &lan->peers[i];
    free(other);
  }
  free(key);
  return found;
}

static qa_kex_lan_peer *peer_for(qa_kex_lan_transport *lan, const qa_net_address *address);

static bool peer_init(qa_kex_lan_transport *lan, qa_kex_lan_peer *peer,
                      const qa_net_address *address) {
  memset(peer, 0, sizeof *peer);
  if (qa_net_address_clone(address, &peer->address) == 0) return false;
  emit_ctx *ctx = (emit_ctx *)calloc(1, sizeof *ctx);
  qa_kex_channel *channel = (qa_kex_channel *)calloc(1, sizeof *channel);
  if (ctx == NULL || channel == NULL) {
    free(ctx);
    free(channel);
    qa_net_address_destroy(&peer->address);
    return false;
  }
  ctx->lan = lan;
  if (qa_net_address_clone(address, &ctx->address) == 0) {
    free(ctx);
    free(channel);
    qa_net_address_destroy(&peer->address);
    return false;
  }
  if (!qa_kex_channel_init(channel, peer_emit, ctx)) {
    free(channel);
    qa_net_address_destroy(&ctx->address);
    free(ctx);
    qa_net_address_destroy(&peer->address);
    return false;
  }
  peer->channel = channel;
  return true;
}

static qa_kex_lan_peer *peer_for(qa_kex_lan_transport *lan, const qa_net_address *address) {
  qa_kex_lan_peer *present = peer_find(lan, address);
  if (present != NULL) return present;
  if (lan->peer_count == lan->peer_capacity) {
    size_t next = lan->peer_capacity > 0 ? lan->peer_capacity * 2 : 8;
    qa_kex_lan_peer *grown = (qa_kex_lan_peer *)realloc(lan->peers, next * sizeof *grown);
    if (grown == NULL) return NULL;
    lan->peers = grown;
    lan->peer_capacity = next;
  }
  qa_kex_lan_peer *peer = &lan->peers[lan->peer_count];
  if (!peer_init(lan, peer, address)) return NULL;
  lan->peer_count++;
  return peer;
}

static void peer_remove_from_list(qa_kex_lan_transport *lan, qa_kex_lan_peer *peer);
static void lan_remove(qa_kex_lan_transport *lan, qa_kex_lan_peer *peer);

static bool text_split(const char *text, char **key, char **value) {
  const char *separator = strchr(text, '\\');
  if (separator == NULL || separator == text) return false;
  size_t key_len = (size_t)(separator - text);
  *key = (char *)malloc(key_len + 1);
  *value = dup_str(separator + 1);
  if (*key == NULL || *value == NULL) {
    free(*key);
    free(*value);
    return false;
  }
  memcpy(*key, text, key_len);
  (*key)[key_len] = '\0';
  return true;
}

static void player_attribute(qa_kex_lan_transport *lan, double index_d, const char *text);

static void lan_join(qa_kex_lan_transport *lan, qa_kex_lan_peer *peer,
                     const uint8_t *bytes, size_t length) {
  qa_kex_packet_reader reader;
  qa_kex_packet_reader_init(&reader, bytes, length);
  char *first = qa_kex_packet_reader_string(&reader);
  if (first == NULL) return;
  bool is_cran = strcmp(first, "CRANTIME") == 0;
  free(first);
  if (!is_cran) return;
  if (lan->options.role_host) {
    char *second = qa_kex_packet_reader_string(&reader);
    if (second == NULL) return;
    bool is_q2 = strcmp(second, "QuakeII") == 0;
    free(second);
    if (!is_q2) return;
    int32_t count = qa_kex_packet_reader_byte(&reader);
    qa_kex_packet_reader_end(&reader);
    if (count < 1 || count > 8 ||
        (peer->player_count == 0 &&
         (double)(lan->player_count + (size_t)count) > lan->options.max_players)) {
      return;
    }
    if (peer->player_count == 0) {
      qa_kex_packet_writer added;
      qa_kex_packet_writer_init(&added);
      (void)qa_kex_packet_writer_byte(&added, 1);
      (void)qa_kex_packet_writer_integer(&added, (uint64_t)count);
      for (int32_t i = 0; i < count; i++) {
        uint64_t id = lan->next_id++;
        (void)peer_push_player(peer, id);
        (void)player_push(lan, id);
        (void)qa_kex_packet_writer_integer(&added, id);
      }
      size_t added_len = 0;
      uint8_t *added_bytes = qa_kex_packet_writer_finish(&added, &added_len);
      /* Peers may move on realloc during iteration; iterate by index. */
      size_t self_index = 0;
      for (size_t i = 0; i < lan->peer_count; i++) {
        if (&lan->peers[i] == peer) self_index = i;
      }
      for (size_t i = 0; i < lan->peer_count; i++) {
        if (i != self_index && lan->peers[i].player_count != 0) {
          (void)qa_kex_channel_send(lan->peers[i].channel, 253, added_bytes, added_len, 3,
                                    lan->clock);
        }
      }
      free(added_bytes);
      peer = &lan->peers[self_index];
    }
    long first_index = peer->player_count > 0 ? player_index_of(lan, peer->players[0]) : -1;
    qa_kex_packet_writer response;
    qa_kex_packet_writer_init(&response);
    (void)qa_kex_packet_writer_string(&response, "CRANTIME");
    (void)qa_kex_packet_writer_integer(&response,
                                       first_index >= 0 ? (uint64_t)first_index : 0);
    for (size_t i = 0; i < lan->player_count; i++) {
      (void)qa_kex_packet_writer_integer(&response, lan->players[i].id);
    }
    size_t response_len = 0;
    uint8_t *response_bytes = qa_kex_packet_writer_finish(&response, &response_len);
    (void)qa_kex_channel_send(peer->channel, 128, response_bytes, response_len, 0, lan->clock);
    free(response_bytes);
    for (size_t i = 0; i < lan->attribute_count; i++) {
      size_t needed = strlen(lan->attributes[i].key) + strlen(lan->attributes[i].value) + 2;
      char *text = (char *)malloc(needed);
      if (text == NULL) return;
      (void)snprintf(text, needed, "%s\\%s", lan->attributes[i].key,
                     lan->attributes[i].value);
      size_t text_len = 0;
      uint8_t *encoded = qa_kex_text_bytes(text, &text_len);
      free(text);
      (void)qa_kex_channel_send(peer->channel, 255, encoded, text_len, 3, lan->clock);
      free(encoded);
    }
    return;
  }
  if (lan->joined) return;
  uint64_t first_index = qa_kex_packet_reader_integer(&reader);
  if (first_index > 255) qa_q2_throw_error("Invalid KEX local player index");
  size_t total = (size_t)first_index + (size_t)lan->options.local_players;
  qa_kex_lobby_player *fresh =
      (qa_kex_lobby_player *)calloc(total > 0 ? total : 1, sizeof *fresh);
  if (fresh == NULL) return;
  for (size_t i = 0; i < total; i++) {
    fresh[i].id = qa_kex_packet_reader_integer(&reader);
  }
  qa_kex_packet_reader_end(&reader);
  players_clear(lan);
  free(lan->players);
  lan->players = fresh;
  lan->player_count = total;
  lan->player_capacity = total;
  lan->joined = true;
}

static void player_attribute(qa_kex_lan_transport *lan, double index_d, const char *text) {
  size_t index = (size_t)index_d;
  const char *separator = strchr(text, '\\');
  if (index >= lan->player_count || separator == NULL || separator == text ||
      strlen(text) >= 128) {
    qa_q2_throw_error("Invalid KEX player attribute");
  }
  char *key = NULL;
  char *value = NULL;
  if (!text_split(text, &key, &value)) return;
  qa_kex_lobby_player *player = &lan->players[index];
  if (value[0] != '\0') {
    (void)attr_set(&player->attributes, &player->attribute_count,
                   &player->attribute_capacity, key, value);
  } else if (strcmp(key, "name") != 0) {
    attr_delete(player->attributes, &player->attribute_count, key);
  }
  free(key);
  free(value);
}

static void player_message(qa_kex_lan_transport *lan, qa_kex_lan_peer *peer,
                           const uint8_t *bytes, size_t length) {
  qa_kex_packet_reader reader;
  qa_kex_packet_reader_init(&reader, bytes, length);
  int32_t operation = qa_kex_packet_reader_byte(&reader);
  if (lan->options.role_host) {
    if ((size_t)operation >= peer->player_count) return;
    uint64_t id = peer->players[operation];
    long index = player_index_of(lan, id);
    if (index < 0) return;
    char *attribute = qa_kex_packet_reader_string(&reader);
    if (attribute == NULL) return;
    qa_kex_packet_reader_end(&reader);
    player_attribute(lan, (double)index, attribute);
    qa_kex_packet_writer output;
    qa_kex_packet_writer_init(&output);
    (void)qa_kex_packet_writer_byte(&output, 0);
    (void)qa_kex_packet_writer_integer(&output, (uint64_t)index);
    (void)qa_kex_packet_writer_string(&output, attribute);
    free(attribute);
    size_t out_len = 0;
    uint8_t *out_bytes = qa_kex_packet_writer_finish(&output, &out_len);
    for (size_t i = 0; i < lan->peer_count; i++) {
      if (lan->peers[i].player_count != 0) {
        (void)qa_kex_channel_send(lan->peers[i].channel, 253, out_bytes, out_len, 3,
                                  lan->clock);
      }
    }
    free(out_bytes);
    return;
  }
  uint64_t index = qa_kex_packet_reader_integer(&reader);
  if (index > 255) qa_q2_throw_error("Invalid KEX player index");
  if (operation == 0) {
    char *attribute = qa_kex_packet_reader_string(&reader);
    if (attribute == NULL) return;
    player_attribute(lan, (double)index, attribute);
    free(attribute);
  } else if (operation == 1) {
    if (lan->player_count + (size_t)index > 255) {
      qa_q2_throw_error("KEX player roster exceeds capacity");
    }
    for (uint64_t i = 0; i < index; i++) {
      (void)player_push(lan, qa_kex_packet_reader_integer(&reader));
    }
  } else if (operation == 2) {
    if ((size_t)index < lan->player_count) {
      player_free(&lan->players[index]);
      memmove(&lan->players[index], &lan->players[index + 1],
              (lan->player_count - (size_t)index - 1) * sizeof(qa_kex_lobby_player));
      lan->player_count--;
    }
  } else {
    qa_q2_throw_error("Invalid KEX player operation");
  }
  qa_kex_packet_reader_end(&reader);
}

static void peer_remove_from_list(qa_kex_lan_transport *lan, qa_kex_lan_peer *peer) {
  for (size_t i = 0; i < lan->peer_count; i++) {
    if (&lan->peers[i] == peer) {
      peer_destroy(peer);
      memmove(&lan->peers[i], &lan->peers[i + 1],
              (lan->peer_count - i - 1) * sizeof(qa_kex_lan_peer));
      lan->peer_count--;
      return;
    }
  }
}

static void lan_remove(qa_kex_lan_transport *lan, qa_kex_lan_peer *peer) {
  uint64_t *leaving = NULL;
  size_t leaving_count = peer->player_count;
  if (leaving_count > 0) {
    leaving = (uint64_t *)malloc(leaving_count * sizeof *leaving);
    if (leaving != NULL) memcpy(leaving, peer->players, leaving_count * sizeof *leaving);
    else leaving_count = 0;
  }
  peer_remove_from_list(lan, peer);
  if (!lan->options.role_host) {
    lan->joined = false;
    attrs_free(lan->attributes, lan->attribute_count);
    lan->attributes = NULL;
    lan->attribute_count = 0;
    lan->attribute_capacity = 0;
    players_clear(lan);
    free(leaving);
    return;
  }
  for (size_t i = 0; i < leaving_count; i++) {
    long index = player_index_of(lan, leaving[i]);
    if (index < 0) continue;
    player_free(&lan->players[index]);
    memmove(&lan->players[index], &lan->players[index + 1],
            (lan->player_count - (size_t)index - 1) * sizeof(qa_kex_lobby_player));
    lan->player_count--;
    qa_kex_packet_writer writer;
    qa_kex_packet_writer_init(&writer);
    (void)qa_kex_packet_writer_byte(&writer, 2);
    (void)qa_kex_packet_writer_integer(&writer, (uint64_t)index);
    size_t bytes_len = 0;
    uint8_t *bytes = qa_kex_packet_writer_finish(&writer, &bytes_len);
    for (size_t t = 0; t < lan->peer_count; t++) {
      if (lan->peers[t].player_count != 0) {
        (void)qa_kex_channel_send(lan->peers[t].channel, 253, bytes, bytes_len, 3,
                                  lan->clock);
      }
    }
    free(bytes);
  }
  free(leaving);
}

static void lan_message(qa_kex_lan_transport *lan, qa_kex_lan_peer *peer, double kind_d,
                        const uint8_t *bytes, size_t length) {
  int32_t kind = (int32_t)kind_d;
  if (kind < 127) {
    if (qa_kex_lan_admitted(lan, &peer->address)) {
      qa_packet_queue_accept(&lan->queue, &peer->address, bytes, length, false);
    }
    return;
  }
  if (kind == 128) {
    lan_join(lan, peer, bytes, length);
    return;
  }
  if (kind == 129) {
    if (length == 0) return;
    if (!lan->options.role_host) return;
    qa_kex_packet_reader reader;
    qa_kex_packet_reader_init(&reader, bytes, length);
    char *a = qa_kex_packet_reader_string(&reader);
    if (a == NULL) return;
    char *b = NULL;
    bool ok = strcmp(a, "CRANTIME") == 0;
    free(a);
    if (ok) {
      b = qa_kex_packet_reader_string(&reader);
      ok = b != NULL && strcmp(b, "QuakeII") == 0;
      free(b);
    }
    if (!ok) return;
    qa_kex_packet_reader_end(&reader);
    qa_kex_packet_writer response;
    qa_kex_packet_writer_init(&response);
    (void)qa_kex_packet_writer_string(&response, lan->options.name != NULL ? lan->options.name : "");
    (void)qa_kex_packet_writer_integer(&response, (uint64_t)lan->player_count);
    (void)qa_kex_packet_writer_integer(&response, (uint64_t)lan->options.max_players);
    for (size_t i = 0; i < lan->attribute_count; i++) {
      const char *key = lan->attributes[i].key;
      const char *value = lan->attributes[i].value;
      if (value[0] != '\0' && key[0] != '_') {
        (void)qa_kex_packet_writer_string(&response, key);
        (void)qa_kex_packet_writer_string(&response, value);
      }
    }
    size_t response_len = 0;
    uint8_t *response_bytes = qa_kex_packet_writer_finish(&response, &response_len);
    (void)lan->transport->send(lan->transport->ctx, &peer->address, response_bytes,
                               response_len);
    free(response_bytes);
    return;
  }
  if (kind == 254) {
    qa_kex_packet_reader reader;
    qa_kex_packet_reader_init(&reader, bytes, length);
    char *reason = qa_kex_packet_reader_string(&reader);
    if (reason == NULL) return;
    qa_kex_packet_reader_end(&reader);
    lan_remove(lan, peer);
    size_t needed = strlen("KEX LAN disconnected: ") + strlen(reason) + 1;
    char *message = (char *)malloc(needed);
    if (message != NULL) {
      (void)snprintf(message, needed, "KEX LAN disconnected: %s", reason);
      qa_packet_queue_push_error(&lan->queue, message);
      free(message);
    }
    free(reason);
    return;
  }
  if (!qa_kex_lan_admitted(lan, &peer->address)) return;
  if (kind == 255 && !lan->options.role_host) {
    char *text = qa_kex_read_text(bytes, length);
    if (text == NULL) return;
    char *key = NULL;
    char *value = NULL;
    if (!text_split(text, &key, &value)) {
      free(text);
      qa_q2_throw_error("Invalid KEX lobby attribute");
    }
    if (value[0] == '\0') attr_delete(lan->attributes, &lan->attribute_count, key);
    else (void)attr_set(&lan->attributes, &lan->attribute_count, &lan->attribute_capacity, key, value);
    free(key);
    free(value);
    free(text);
    return;
  }
  if (kind == 253) player_message(lan, peer, bytes, length);
}

static double clock_now(void *ctx) { return ((qa_kex_lan_transport *)ctx)->clock; }

static void mdns_failed(void *vctx, const char *message) {
  qa_kex_lan_transport *lan = (qa_kex_lan_transport *)vctx;
  if (!lan->ended) qa_packet_queue_push_error(&lan->queue, message);
}
static void mdns_found_noop(void *ctx, const qa_net_address *address) {
  (void)ctx;
  (void)address;
}
static void lan_drain(qa_kex_lan_transport *lan);
static void on_readable(void *vctx) {
  /* TS subscribeReadable(() => this.drain()): drain only, never pop. */
  lan_drain((qa_kex_lan_transport *)vctx);
}

/* Drain without consuming a poll result; mirrors TS drain(). */
static void lan_drain(qa_kex_lan_transport *lan) {
  if (lan->ended) return;
  for (;;) {
    qa_receive_event event;
    memset(&event, 0, sizeof event);
    int ready = lan->transport->poll(lan->transport->ctx, &event);
    if (ready == 0) break;
    if (event.kind != QA_RECV_PACKET) {
      qa_receive_event moved = event;
      qa_packet_queue_push_event(&lan->queue, &moved);
      continue;
    }
    bool skip = false;
    if (!lan->options.role_host && !qa_net_same_address(&event.from, &lan->server, true)) {
      skip = true;
    }
    if (!skip) {
      qa_kex_lan_peer *peer = peer_find(lan, &event.from);
      if (peer == NULL) {
        if (event.length < 3 || (event.payload[1] % 16) != 0 ||
            (event.payload[2] != 128 && event.payload[2] != 129)) {
          skip = true;
        } else if (lan->peer_count >= 256) {
          skip = true;
        } else {
          peer = peer_for(lan, &event.from);
          if (peer == NULL) skip = true;
        }
      }
      /* Find the peer index so realloc during message handling is safe. */
      if (!skip && peer != NULL) {
        size_t peer_index = (size_t)(peer - lan->peers);
        qa_kex_message message;
        memset(&message, 0, sizeof message);
        /* Malformed packets throw in TS and are caught; probe with a
         * size/shape gate first (inner errors still abort). */
        bool well_formed = event.length >= 3 && event.length <= QA_KEX_DATAGRAM_BYTES;
        if (well_formed) {
          /* header length check mirrors readKexPacket's gate */
          uint16_t header = (uint16_t)(((uint32_t)event.payload[0] << 8) | event.payload[1]);
          if ((size_t)(header >> 4) != event.length) well_formed = false;
        }
        if (well_formed) {
          bool has = qa_kex_channel_receive(lan->peers[peer_index].channel, event.payload,
                                            event.length, lan->clock, &message);
          if (has) {
            lan_message(lan, peer_find(lan, &event.from) != NULL
                                 ? peer_find(lan, &event.from)
                                 : &lan->peers[peer_index],
                        message.kind, message.payload, message.payload_length);
            qa_kex_message_free(&message);
          }
        }
      }
    }
    qa_receive_event_destroy(&event);
  }
}

bool qa_kex_lan_transport_init(qa_kex_lan_transport *lan,
                               qa_datagram_transport *transport,
                               const qa_kex_lan_options *options) {
  memset(lan, 0, sizeof *lan);
  lan->transport = transport;
  lan->options = *options;
  lan->next_id = 1;
  lan->retry_at = -INFINITY;
  lan->unsubscribe_id = -1;
  qa_net_address transport_address;
  memset(&transport_address, 0, sizeof transport_address);
  if (transport->get_address(transport->ctx, &transport_address) == 0) return false;
  bool is_ipx = transport_address.kind == QA_NET_IPX;
  bool is_ip = transport_address.kind == QA_NET_IPV4 || transport_address.kind == QA_NET_IPV6;
  double address_port = (double)transport_address.port;
  qa_net_address_destroy(&transport_address);
  if (is_ipx) qa_q2_throw_error("KEX LAN requires an IP transport");
  if (!is_integer(options->local_players) || options->local_players < 0 ||
      options->local_players > 8 ||
      (!options->role_host && options->local_players == 0)) {
    qa_q2_throw_error("Invalid KEX local player count");
  }
  if (options->role_host && (!is_integer(options->max_players) ||
                             options->max_players < options->local_players ||
                             options->max_players > 255)) {
    qa_q2_throw_error("Invalid KEX player capacity");
  }
  qa_datagram_limits limits = {65535, 256};
  if (qa_packet_queue_init(&lan->queue, limits, clock_now, lan) != 0) {
    memset(lan, 0, sizeof *lan);
    return false;
  }
  if (options->role_host) {
    for (double i = 0; i < options->local_players; i++) {
      (void)player_push(lan, lan->next_id++);
    }
    (void)attr_set(&lan->attributes, &lan->attribute_count, &lan->attribute_capacity,
                   "ingame", "1");
    lan->joined = true;
  } else {
    if (qa_net_address_clone(&options->server, &lan->server) == 0) {
      qa_packet_queue_destroy(&lan->queue);
      memset(lan, 0, sizeof *lan);
      return false;
    }
    lan->options.server = lan->server;
    if (peer_for(lan, &lan->server) == NULL) {
      qa_packet_queue_destroy(&lan->queue);
      return false;
    }
  }
  lan->unsubscribe_id = transport->subscribe(transport->ctx, on_readable, lan);
  if (options->role_host && is_ip) {
    qa_kex_mdns temp;
    if (qa_kex_mdns_open(&temp, true, address_port, mdns_found_noop, lan, mdns_failed, lan)) {
      if (lan->ended) qa_kex_mdns_close(&temp);
      else {
        lan->discovery = temp;
        lan->has_discovery = true;
      }
    } else {
      mdns_failed(lan, "KEX mDNS open failed");
    }
  }
  return true;
}

void qa_kex_lan_transport_destroy(qa_kex_lan_transport *lan) {
  if (!lan->ended) qa_kex_lan_close(lan);
  for (size_t i = 0; i < lan->peer_count; i++) peer_destroy(&lan->peers[i]);
  free(lan->peers);
  players_clear(lan);
  free(lan->players);
  attrs_free(lan->attributes, lan->attribute_count);
  qa_packet_queue_destroy(&lan->queue);
  qa_net_address_destroy(&lan->server);
  memset(lan, 0, sizeof *lan);
}

int qa_kex_lan_get_address(const qa_kex_lan_transport *lan, qa_net_address *out) {
  return lan->transport->get_address(lan->transport->ctx, out);
}
bool qa_kex_lan_get_closed(const qa_kex_lan_transport *lan) { return lan->ended; }
bool qa_kex_lan_get_ready(const qa_kex_lan_transport *lan) {
  const char *ingame = attr_get(lan->attributes, lan->attribute_count, "ingame");
  return lan->joined && ingame != NULL && strcmp(ingame, "1") == 0;
}
const qa_kex_lobby_player *qa_kex_lan_get_lobby_players(const qa_kex_lan_transport *lan,
                                                        size_t *out_count) {
  *out_count = lan->player_count;
  return lan->players;
}
bool qa_kex_lan_admitted(const qa_kex_lan_transport *lan, const qa_net_address *address) {
  if (lan->options.role_host) {
    qa_kex_lan_transport *mutable_lan = (qa_kex_lan_transport *)lan;
    qa_kex_lan_peer *peer = peer_find(mutable_lan, address);
    return peer != NULL && peer->player_count > 0;
  }
  return lan->joined && qa_net_same_address(address, &lan->server, true);
}

void qa_kex_lan_tick(qa_kex_lan_transport *lan, double now) {
  lan->clock = now;
  lan_drain(lan);
  if (lan->has_discovery) (void)qa_kex_mdns_poll(&lan->discovery);
  if (!lan->options.role_host && !lan->joined &&
      (now_is_negative_infinity(lan->retry_at) || now - lan->retry_at >= 500)) {
    lan->retry_at = now;
    qa_kex_packet_writer writer;
    qa_kex_packet_writer_init(&writer);
    (void)qa_kex_packet_writer_string(&writer, "CRANTIME");
    (void)qa_kex_packet_writer_string(&writer, "QuakeII");
    (void)qa_kex_packet_writer_byte(&writer, lan->options.local_players);
    size_t payload_len = 0;
    uint8_t *payload = qa_kex_packet_writer_finish(&writer, &payload_len);
    qa_kex_packet packet;
    memset(&packet, 0, sizeof packet);
    packet.kind = 128;
    packet.payload = payload;
    packet.payload_length = payload_len;
    size_t bytes_len = 0;
    uint8_t *bytes = qa_kex_write_packet(&packet, &bytes_len);
    free(payload);
    if (bytes != NULL) {
      (void)lan->transport->send(lan->transport->ctx, &lan->server, bytes, bytes_len);
      free(bytes);
    }
  }
  /* TS catches tick errors per peer: remove it and queue an error. */
  for (size_t i = 0; i < lan->peer_count;) {
    qa_kex_lan_peer *peer = &lan->peers[i];
    if (!qa_kex_channel_tick(peer->channel, now)) {
      lan_remove(lan, peer);
      qa_packet_queue_push_error(&lan->queue, "KEX LAN peer timed out");
      continue; /* removal shifts the next peer into slot i */
    }
    i++;
  }
}

bool qa_kex_lan_send(qa_kex_lan_transport *lan, const qa_net_address *to,
                     const uint8_t *payload, size_t length) {
  if (lan->ended) qa_q2_throw_error("KEX transport is closed");
  if (!qa_kex_lan_admitted(lan, to)) return false;
  bool reliable = false;
  if (length >= 8) {
    uint32_t first = 0;
    uint32_t second = 0;
    for (int i = 0; i < 4; i++) first |= (uint32_t)payload[i] << (8 * i);
    for (int i = 0; i < 4; i++) second |= (uint32_t)payload[4 + i] << (8 * i);
    reliable = first == 0x80000000u && second == 0x80000000u;
  }
  qa_kex_lan_peer *peer = peer_for(lan, to);
  if (peer == NULL) return false;
  return qa_kex_channel_send(peer->channel, 0, payload, length, reliable ? 3 : 2, lan->clock);
}

int qa_kex_lan_poll(qa_kex_lan_transport *lan, qa_receive_event *out) {
  lan_drain(lan);
  return qa_packet_queue_poll(&lan->queue, out);
}
long long qa_kex_lan_subscribe(qa_kex_lan_transport *lan, qa_net_listener_fn fn, void *ctx) {
  return qa_packet_queue_subscribe(&lan->queue, fn, ctx);
}
void qa_kex_lan_unsubscribe(qa_kex_lan_transport *lan, long long id) {
  qa_packet_queue_unsubscribe(&lan->queue, id);
}

void qa_kex_lan_close(qa_kex_lan_transport *lan) {
  if (lan->ended) return;
  for (size_t i = 0; i < lan->peer_count; i++) {
    qa_kex_packet_writer writer;
    qa_kex_packet_writer_init(&writer);
    (void)qa_kex_packet_writer_string(&writer, "Disconnected");
    size_t payload_len = 0;
    uint8_t *payload = qa_kex_packet_writer_finish(&writer, &payload_len);
    (void)qa_kex_channel_send(lan->peers[i].channel, 254, payload, payload_len, 0, lan->clock);
    free(payload);
  }
  lan->ended = true;
  if (lan->has_discovery) {
    qa_kex_mdns_close(&lan->discovery);
    lan->has_discovery = false;
  }
  if (lan->unsubscribe_id >= 0) {
    lan->transport->unsubscribe(lan->transport->ctx, lan->unsubscribe_id);
    lan->unsubscribe_id = -1;
  }
  for (size_t i = 0; i < lan->peer_count; i++) peer_destroy(&lan->peers[i]);
  lan->peer_count = 0;
  qa_packet_queue_close(&lan->queue);
  lan->transport->close(lan->transport->ctx);
}

void qa_kex_lan_set_attribute(qa_kex_lan_transport *lan, const char *key,
                              const char *value) {
  const char *k = key != NULL ? key : "";
  const char *v = value != NULL ? value : "";
  if (!lan->options.role_host || k[0] == '\0' || strchr(k, '\\') != NULL ||
      strchr(v, '\\') != NULL) {
    qa_q2_throw_error("Invalid KEX host attribute");
  }
  if (v[0] == '\0') attr_delete(lan->attributes, &lan->attribute_count, k);
  else (void)attr_set(&lan->attributes, &lan->attribute_count, &lan->attribute_capacity, k, v);
  size_t needed = strlen(k) + strlen(v) + 2;
  char *text = (char *)malloc(needed);
  if (text == NULL) return;
  (void)snprintf(text, needed, "%s\\%s", k, v);
  size_t encoded_len = 0;
  uint8_t *encoded = qa_kex_text_bytes(text, &encoded_len);
  free(text);
  for (size_t i = 0; i < lan->peer_count; i++) {
    if (lan->peers[i].player_count != 0) {
      (void)qa_kex_channel_send(lan->peers[i].channel, 255, encoded, encoded_len, 3,
                                lan->clock);
    }
  }
  free(encoded);
}

/* Vtable adapters. */
static int vt_get_address(void *ctx, qa_net_address *out) {
  return qa_kex_lan_get_address((qa_kex_lan_transport *)ctx, out);
}
static bool vt_is_closed(void *ctx) {
  return qa_kex_lan_get_closed((qa_kex_lan_transport *)ctx);
}
static int64_t vt_max_bytes(void *ctx) {
  (void)ctx;
  return 65535;
}
static bool vt_send(void *ctx, const qa_net_address *to, const uint8_t *payload,
                    size_t length) {
  return qa_kex_lan_send((qa_kex_lan_transport *)ctx, to, payload, length);
}
static int vt_poll(void *ctx, qa_receive_event *out) {
  return qa_kex_lan_poll((qa_kex_lan_transport *)ctx, out);
}
static long long vt_subscribe(void *ctx, qa_net_listener_fn fn, void *listener_ctx) {
  return qa_kex_lan_subscribe((qa_kex_lan_transport *)ctx, fn, listener_ctx);
}
static void vt_unsubscribe(void *ctx, long long id) {
  qa_kex_lan_unsubscribe((qa_kex_lan_transport *)ctx, id);
}
static void vt_close(void *ctx) { qa_kex_lan_close((qa_kex_lan_transport *)ctx); }

qa_datagram_transport qa_kex_lan_as_transport(qa_kex_lan_transport *lan) {
  qa_datagram_transport transport;
  transport.ctx = lan;
  transport.get_address = vt_get_address;
  transport.is_closed = vt_is_closed;
  transport.max_datagram_bytes = vt_max_bytes;
  transport.send = vt_send;
  transport.poll = vt_poll;
  transport.subscribe = vt_subscribe;
  transport.unsubscribe = vt_unsubscribe;
  transport.close = vt_close;
  return transport;
}
