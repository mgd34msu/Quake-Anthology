/* Port of src/app/bootstrap/network/q2.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/network/q2.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/network/q2_downloads.h"
#include "qa/core/commands/text.h"
#include "qa/foundation/support.h"
#include "qa/network/q2/channel.h"
#include "qa/network/q2/client_messages.h"
#include "qa/network/q2/connectionless.h"
#include "qa/network/q2/handshake.h"
#include "qa/network/q2/kex/lan.h"
#include "qa/network/q2/mvd_broadcast.h"
#include "qa/network/q2/mvd_encoding.h"
#include "qa/network/q2/server_demo.h"
#include "qa/network/q2/server_write.h"
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
static double signon_integer(const char *text) {
  if (!text) qa_range_error("Invalid Q2 signon number");
  const char *p = text;
  if (*p == '-') p++;
  if (!*p) qa_range_error("Invalid Q2 signon number");
  for (const char *q = p; *q; q++)
    if (*q < '0' || *q > '9') qa_range_error("Invalid Q2 signon number");
  double result = strtod(text, NULL);
  if (result != trunc(result) || fabs(result) > 9007199254740991.0)
    qa_range_error("Q2 signon number outside range");
  return result;
}
static bool client_equal(qa_client_id a, qa_client_id b) {
  return a.slot == b.slot && a.generation == b.generation && a.token == b.token;
}
static bool wire_supported(const qa_app_q2_server_host *host, const char *prefix) {
  const void *support = host->supports_source_wire ? host->supports_source_wire(host->ctx) : NULL;
  if (!support) return true;
  const qa_wire_admission *admission = support;
  if (admission->supported) return true;
  size_t total = strlen(prefix) + 1;
  for (size_t i = 0; i < admission->reason_count; i++)
    total += strlen(admission->reasons[i]) + 2;
  char *message = xmalloc(total);
  size_t used = (size_t)snprintf(message, total, "%s", prefix);
  for (size_t i = 0; i < admission->reason_count; i++) {
    if (i) { memcpy(message + used, "; ", 2); used += 2; }
    size_t len = strlen(admission->reasons[i]);
    memcpy(message + used, admission->reasons[i], len);
    used += len;
  }
  message[used] = '\0';
  qa_range_error("%s", message);
}
typedef struct split {
  qa_app_q2_player player; qa_q2_command_replay replay; double sequence;
} split;
typedef struct frame_store { double number; qa_q2_wire_frame frame; } frame_store;
typedef struct peer {
  qa_app_q2_peer_download *download;
  qa_q2_protocol_identity protocol;
  char *download_failure;
  qa_net_address remote; char *key;
  qa_app_q2_player player;
  split *splits; size_t split_count;
  qa_q2_channel channel;
  qa_q2_wire_codec wire;
  qa_q2_command_replay replay;
  frame_store *frames; size_t frame_count, frame_cap;
  bool has_game_state; qa_app_q2_game_state game_state;
  bool active; double sequence, last_received;
  uint8_t **datagram; size_t *datagram_lengths; size_t datagram_count, datagram_cap;
  char *userinfo;
} peer;
typedef struct mvd_recording_owner {
  qa_app_q2_recording_sink sink; qa_q2_mvd_encoder encoder; void *token;
} mvd_recording_owner;
typedef struct server_recording_owner {
  qa_app_q2_recording_sink sink; double generation;
  const qa_q2_mvd_capture_config *configs; size_t config_count; void *token;
} server_recording_owner;
struct qa_app_q2_server_network {
  qa_app_q2_server_network_options options;
  qa_app_q2_server_host host;
  qa_kex_lan_transport lan; bool lan_live;
  qa_datagram_transport transport; bool transport_is_lan;
  qa_master_heartbeat_service heartbeat_service;
  qa_q2_discovery_wire_state discovery_state;
  qa_discovery_wire discovery_wire;
  bool ended;
  peer **peers; size_t peer_count, peer_cap;
  qa_q2_challenge_table challenges;
  const void **pending; size_t pending_count, pending_cap;
  double server_generation;
  const void *mvd_output; const void *const *mvd_events; size_t mvd_event_count;
  bool has_mvd_frame;
  const qa_q2_mvd_capture *mvd_seed;
  bool has_mvd_tick; double mvd_tick;
  const qa_q2_mvd_emission **mvd_messages; size_t mvd_message_count, mvd_message_cap;
  mvd_recording_owner *mvd_owner;
  qa_q2_mvd_broadcast broadcast; bool broadcast_live, broadcast_init;
  const qa_q2_mvd_capture *server_demo_seed;
  server_recording_owner *server_owner;
  const void **poll_result; size_t poll_result_cap;
};
typedef struct q2_discovery {
  void *ctx;
  qa_q2_status (*status)(void *ctx);
  qa_q2_host_info (*info)(void *ctx);
} q2_discovery;
static qa_datagram_transport *transport_of(qa_app_q2_server_network *n) {
  return &n->transport;
}
static bool is_kex(const qa_q2_protocol_identity *protocol) {
  return protocol->kind && strcmp(protocol->kind, "q2-kex") == 0;
}
static double now_performance(void) { return 0; }
static void datagram_push(peer *p, uint8_t *bytes, size_t length) {
  if (p->datagram_count == p->datagram_cap) {
    p->datagram_cap = p->datagram_cap ? p->datagram_cap * 2 : 8;
    p->datagram = realloc(p->datagram, p->datagram_cap * sizeof *p->datagram);
    p->datagram_lengths = realloc(p->datagram_lengths, p->datagram_cap * sizeof *p->datagram_lengths);
    if (!p->datagram || !p->datagram_lengths) qa_range_error("out of memory");
  }
  p->datagram[p->datagram_count] = bytes;
  p->datagram_lengths[p->datagram_count] = length;
  p->datagram_count++;
}
static void datagram_clear(peer *p) {
  for (size_t i = 0; i < p->datagram_count; i++) free(p->datagram[i]);
  p->datagram_count = 0;
}
static void frames_clear(peer *p) {
  for (size_t i = 0; i < p->frame_count; i++) qa_q2_wire_frame_free(&p->frames[i].frame);
  p->frame_count = 0;
}
static void reliable(peer *p, const qa_q2_server_event *event) {
  size_t length = 0;
  uint8_t *bytes = qa_q2_encode_server_event(&p->wire, event, &length);
  if (!bytes) qa_range_error("out of memory");
  qa_q2_channel_queue_reliable(&p->channel, bytes, length);
  free(bytes);
}
static void stuff(peer *p, const char *text) {
  qa_q2_server_event event = {0};
  event.kind = QA_Q2_SERVER_COMMAND_TEXT;
  event.text = (char *)text;
  reliable(p, &event);
}
static void close_download(peer *p) {
  qa_app_q2_peer_download_close(p->download);
  free(p->download_failure);
  p->download_failure = NULL;
}
static peer *find_peer(qa_app_q2_server_network *n, const char *key) {
  for (size_t i = 0; i < n->peer_count; i++)
    if (strcmp(n->peers[i]->key, key) == 0) return n->peers[i];
  return NULL;
}
static void peer_destroy(qa_app_q2_server_network *n, peer *p) {
  (void)n;
  close_download(p);
  qa_app_q2_peer_download_destroy(p->download);
  qa_net_address_destroy(&p->remote);
  free(p->key);
  free(p->splits);
  qa_q2_channel_destroy(&p->channel);
  frames_clear(p);
  free(p->frames);
  datagram_clear(p);
  free(p->datagram);
  free(p->datagram_lengths);
  free(p->userinfo);
  free(p);
}
static void drop_peer(qa_app_q2_server_network *n, peer *p, const char *reason) {
  close_download(p);
  for (size_t i = 0; i < n->peer_count; i++)
    if (n->peers[i] == p) {
      memmove(n->peers + i, n->peers + i + 1, (n->peer_count - i - 1) * sizeof *n->peers);
      n->peer_count--;
      break;
    }
  /* pending live-filter by remote-client source identity. Commands
   * are qa_actor_command-shaped: source client at offset of .source. */
  size_t kept = 0;
  for (size_t i = 0; i < n->pending_count; i++) {
    const qa_actor_command *command = n->pending[i];
    bool drop = false;
    if (command->source.kind == QA_COMMAND_SOURCE_REMOTE_CLIENT) {
      if (client_equal(command->source.client, p->player.client)) drop = true;
      for (size_t j = 0; j < p->split_count; j++)
        if (client_equal(command->source.client, p->splits[j].player.client)) drop = true;
    }
    if (!drop) n->pending[kept++] = n->pending[i];
  }
  n->pending_count = kept;
  qa_app_q2_player player = p->player;
  size_t split_count = p->split_count;
  qa_app_q2_player *split_players = xmalloc(split_count * sizeof *split_players);
  for (size_t i = 0; i < split_count; i++) split_players[i] = p->splits[i].player;
  peer_destroy(n, p);
  n->host.disconnect(n->host.ctx, player, reason);
  for (size_t i = 0; i < split_count; i++)
    n->host.disconnect(n->host.ctx, split_players[i], reason);
  free(split_players);
}
static void reply_oob(qa_app_q2_server_network *n, const qa_net_address *remote, const char *text) {
  size_t length = 0;
  uint8_t *bytes = qa_q2_out_of_band(text, is_kex(&n->host.protocol), &length);
  if (!bytes) qa_range_error("out of memory");
  transport_of(n)->send(transport_of(n)->ctx, remote, bytes, length);
  free(bytes);
}
static void accept_connection(qa_app_q2_server_network *n, const qa_net_address *remote) {
  const qa_app_q2_downloads *downloads = n->host.downloads;
  char *url = downloads ? qa_app_q2_downloads_http_server((qa_app_q2_downloads *)downloads) : NULL;
  char text[1024];
  snprintf(text, sizeof text, "client_connect%s%s%s", is_kex(&n->host.protocol) ? " 2023" : "",
      url ? " dlserver=" : "", url ? url : "");
  free(url);
  reply_oob(n, remote, text);
}
static const qa_q2_server_data_result *state_data(const qa_app_q2_game_state *state) {
  return state->data;
}
static void new_client(qa_app_q2_server_network *n, peer *p) {
  close_download(p);
  p->active = false;
  frames_clear(p);
  qa_q2_command_replay_init(&p->replay);
  for (size_t i = 0; i < p->split_count; i++) qa_q2_command_replay_init(&p->splits[i].replay);
  datagram_clear(p);
  qa_app_q2_game_state original = n->host.game_state(n->host.ctx, p->player, &p->protocol);
  qa_app_q2_game_state state = original;
  qa_q2_server_data_result data = *(const qa_q2_server_data_result *)original.data;
  data.servercount = n->server_generation;
  if (p->split_count > 0) {
    data.has_clientnums = true;
    data.clientnum_count = p->split_count + 1;
    data.clientnums[0] = p->player.source_entity - 1;
    for (size_t i = 0; i < p->split_count; i++)
      data.clientnums[i + 1] = p->splits[i].player.source_entity - 1;
  }
  qa_q2_server_data_result *owned_data = xmalloc(sizeof *owned_data);
  *owned_data = data;
  state.data = owned_data;
  p->game_state = state;
  p->has_game_state = true;
  qa_q2_server_event event = {0};
  event.kind = QA_Q2_SERVER_DATA;
  event.data = data;
  reliable(p, &event);
  char text[96];
  snprintf(text, sizeof text, "cmd configstrings %.0f 0\n", data.servercount);
  stuff(p, text);
}
static void signon_page(qa_app_q2_server_network *n, peer *p, bool configs, double start) {
  (void)n;
  if (!p->has_game_state) qa_range_error("Q2 signon has no game state");
  if (start < 0) qa_range_error("Q2 signon index is negative");
  const qa_app_q2_game_state *state = &p->game_state;
  const qa_q2_server_data_result *data = state_data(state);
  size_t length = 0;
  bool has_next = false;
  double next = 0;
  if (configs) {
    const qa_app_q2_config_snapshot *snapshot = state->config_strings;
    for (size_t i = 0; i < snapshot->count; i++) {
      if (snapshot->entries[i].index < start) continue;
      qa_q2_server_event event = {0};
      event.kind = QA_Q2_SERVER_CONFIG_STRING;
      event.index = snapshot->entries[i].index;
      event.text = snapshot->entries[i].value;
      size_t entry_length = 0;
      uint8_t *bytes = qa_q2_encode_server_event(&p->wire, &event, &entry_length);
      if (!bytes) qa_range_error("out of memory");
      if ((double)(length + entry_length) > p->channel.capacity - 96) {
        has_next = true;
        next = snapshot->entries[i].index;
        free(bytes);
        break;
      }
      qa_q2_channel_queue_reliable(&p->channel, bytes, entry_length);
      free(bytes);
      length += entry_length;
    }
  } else {
    const qa_q2_baseline_list *baselines = state->baselines;
    for (size_t i = 0; i < baselines->count; i++) {
      if (baselines->entries[i].number < start) continue;
      qa_q2_server_event event = {0};
      event.kind = QA_Q2_SERVER_BASELINE;
      event.entity = baselines->entries[i].state;
      size_t entry_length = 0;
      uint8_t *bytes = qa_q2_encode_server_event(&p->wire, &event, &entry_length);
      if (!bytes) qa_range_error("out of memory");
      if ((double)(length + entry_length) > p->channel.capacity - 96) {
        has_next = true;
        next = baselines->entries[i].number;
        free(bytes);
        break;
      }
      qa_q2_channel_queue_reliable(&p->channel, bytes, entry_length);
      free(bytes);
      length += entry_length;
    }
  }
  if (has_next && length == 0)
    qa_range_error("Q2 signon record exceeds negotiated reliable message capacity");
  char text[96];
  if (has_next) snprintf(text, sizeof text, "cmd %s %.0f %.0f\n",
      configs ? "configstrings" : "baselines", data->servercount, next);
  else if (configs) snprintf(text, sizeof text, "cmd baselines %.0f 0\n", data->servercount);
  else snprintf(text, sizeof text, "precache %.0f\n", data->servercount);
  stuff(p, text);
}
static void begin_download(qa_app_q2_server_network *n, peer *p,
    const char *name, const char *offset) {
  /* TS completes the open asynchronously; errors land in
   * downloadFailure for the next poll. The C begin is synchronous. */
  qa_q2_server_event *event = qa_app_q2_peer_download_begin(p->download,
      (qa_app_q2_downloads *)n->host.downloads, name, offset);
  if (event) {
    reliable(p, event);
    qa_q2_server_event_clear(event);
    free(event);
  }
}
static void client_command(qa_app_q2_server_network *n, peer *p, const char *original,
    qa_app_q2_player player) {
  const char *text = original;
  char *expanded = NULL;
  if (n->host.expand_client_command) {
    const char *result = n->host.expand_client_command(n->host.ctx, text);
    if (!result) return;
    expanded = (char *)result;
    text = result;
  }
  qa_command_tokens tokens;
  if (!qa_tokenize_command(text, QA_COMMAND_DIALECT_Q2_CLASSIC, QA_COMMAND_TEXT_SOURCE, &tokens)) qa_range_error("out of memory");
  free(expanded);
  if (tokens.argc == 0) { qa_command_tokens_destroy(&tokens); return; }
  const char *name = tokens.argv[0];
  if (strcmp(name, "disconnect") == 0) {
    qa_command_tokens_destroy(&tokens);
    drop_peer(n, p, "Client disconnected");
    return;
  }
  if (strcmp(name, "new") == 0) {
    qa_command_tokens_destroy(&tokens);
    new_client(n, p);
    return;
  }
  if (strcmp(name, "download") == 0) {
    char *name_copy = copy_text(tokens.argc > 1 ? tokens.argv[1] : "");
    char *offset_copy = tokens.argc > 2 ? copy_text(tokens.argv[2]) : NULL;
    qa_command_tokens_destroy(&tokens);
    begin_download(n, p, name_copy, offset_copy);
    free(name_copy);
    free(offset_copy);
    return;
  }
  if (strcmp(name, "nextdl") == 0) {
    qa_command_tokens_destroy(&tokens);
    qa_q2_server_event *event = qa_app_q2_peer_download_next(p->download);
    if (event) {
      reliable(p, event);
      qa_q2_server_event_clear(event);
      free(event);
    }
    return;
  }
  if (strcmp(name, "configstrings") == 0 || strcmp(name, "baselines") == 0
      || strcmp(name, "begin") == 0) {
    bool is_begin = strcmp(name, "begin") == 0;
    bool is_configs = strcmp(name, "configstrings") == 0;
    char *arg1 = tokens.argc > 1 ? copy_text(tokens.argv[1]) : NULL;
    char *arg2 = tokens.argc > 2 ? copy_text(tokens.argv[2]) : NULL;
    qa_command_tokens_destroy(&tokens);
    if (!p->has_game_state || signon_integer(arg1) != state_data(&p->game_state)->servercount) {
      free(arg1); free(arg2);
      new_client(n, p);
      return;
    }
    if (is_begin) {
      if (!p->active && n->host.begin) {
        n->host.begin(n->host.ctx, p->player);
        for (size_t i = 0; i < p->split_count; i++) n->host.begin(n->host.ctx, p->splits[i].player);
      }
      p->active = true;
      free(arg1); free(arg2);
      return;
    }
    double start = signon_integer(arg2);
    free(arg1); free(arg2);
    signon_page(n, p, is_configs, start);
    return;
  }
  if (p->active) {
    if (n->host.command_text) n->host.command_text(n->host.ctx, player, text);
    else n->host.command(n->host.ctx, player, name,
        (const char *const *)tokens.argv + 1, tokens.argc - 1);
  }
  qa_command_tokens_destroy(&tokens);
}
typedef struct think_ctx {
  qa_app_q2_server_network *n; qa_app_q2_player player; double *sequence;
} think_ctx;
static void think_command(void *ctx, const qa_q2_wire_usercmd *command) {
  think_ctx *think = ctx;
  const void *input = think->n->host.input(think->n->host.ctx, think->player,
      command, (*think->sequence)++);
  if (input) {
    if (think->n->pending_count == think->n->pending_cap) {
      think->n->pending_cap = think->n->pending_cap ? think->n->pending_cap * 2 : 32;
      think->n->pending = realloc(think->n->pending,
          think->n->pending_cap * sizeof *think->n->pending);
      if (!think->n->pending) qa_range_error("out of memory");
    }
    think->n->pending[think->n->pending_count++] = input;
  }
}
static void process_peer(qa_app_q2_server_network *n, peer *p,
    const qa_q2_channel_receive_result *result) {
  qa_q2_client_record *records = NULL;
  size_t count = 0;
  if (!qa_q2_read_client_messages(&p->wire, result->bytes, result->bytes_length,
          result->sequence, (double)p->split_count + 1, &records, &count))
    qa_range_error("out of memory");
  for (size_t i = 0; i < count; i++) {
    qa_q2_client_record *record = &records[i];
    double seat = record->seat;
    qa_q2_command_replay *replay;
    qa_app_q2_player player;
    double *sequence;
    if (seat == 0) {
      replay = &p->replay; player = p->player; sequence = &p->sequence;
    } else {
      if (seat < 1 || seat > (double)p->split_count)
        qa_range_error("Missing admitted Q2 split owner");
      split *owner = &p->splits[(size_t)seat - 1];
      replay = &owner->replay; player = owner->player; sequence = &owner->sequence;
    }
    switch (record->event.kind) {
      case QA_Q2_CLIENT_MOVE:
      case QA_Q2_CLIENT_BATCH_MOVE:
        if (p->active) {
          think_ctx think = {n, player, sequence};
          qa_q2_command_replay_execute(replay, &record->event, result->dropped,
              think_command, &think);
        }
        break;
      case QA_Q2_CLIENT_COMMAND: {
        char *text = copy_text(record->event.text ? record->event.text : "");
        client_command(n, p, text, player);
        free(text);
        char *key = qa_net_address_key(&p->remote, true);
        bool present = key && find_peer(n, key) == p;
        free(key);
        if (!present) {
          qa_q2_client_records_free(records, count);
          return;
        }
        break;
      }
      case QA_Q2_CLIENT_USERINFO: {
        const char *text = record->event.text ? record->event.text : "";
        if (is_kex(&p->protocol)) {
          char *seat0 = qa_q2_kex_seat_userinfo(text, 0);
          n->host.userinfo(n->host.ctx, p->player, seat0);
          free(seat0);
        } else n->host.userinfo(n->host.ctx, p->player, text);
        for (size_t s = 0; s < p->split_count; s++) {
          char *seat_text = qa_q2_kex_seat_userinfo(text, (double)s + 1);
          n->host.userinfo(n->host.ctx, p->splits[s].player, seat_text);
          free(seat_text);
        }
        free(p->userinfo);
        p->userinfo = copy_text(text);
        break;
      }
      case QA_Q2_CLIENT_USERINFO_DELTA: {
        const char *args[] = {record->event.delta.name, record->event.delta.value};
        n->host.command(n->host.ctx, p->player, "userinfo_delta", args, 2);
        break;
      }
      case QA_Q2_CLIENT_SETTING: {
        char index_text[32];
        char value_text[64];
        snprintf(index_text, sizeof index_text, "%.17g", record->event.setting.index);
        snprintf(value_text, sizeof value_text, "%.17g", record->event.setting.value);
        const char *args[] = {index_text, value_text};
        n->host.command(n->host.ctx, p->player, "set_setting", args, 2);
        break;
      }
      case QA_Q2_CLIENT_NOP: break;
    }
  }
  qa_q2_client_records_free(records, count);
}
typedef struct rcon_ctx { qa_app_q2_server_network *n; const qa_net_address *remote; } rcon_ctx;
static void rcon_reply(void *ctx, const qa_net_address *to, const uint8_t *payload, size_t length) {
  rcon_ctx *rcon = ctx;
  (void)to;
  transport_of(rcon->n)->send(transport_of(rcon->n)->ctx, rcon->remote, payload, length);
}
static bool connectionless(qa_app_q2_server_network *n, const qa_net_address *remote,
    const uint8_t *bytes, size_t length, double now) {
  qa_q2_connectionless_message message;
  if (!qa_q2_read_out_of_band(bytes, length, is_kex(&n->host.protocol), &message))
    return false;
  bool handled = true;
  if (strcmp(message.command, "rcon") == 0) {
    if (n->host.administration) {
      qa_q2_connectionless_host admin =
          *(const qa_q2_connectionless_host *)n->host.administration;
      rcon_ctx ctx = {n, remote};
      admin.ctx = &ctx;
      admin.reply = rcon_reply;
      qa_q2_handle_rcon(&admin, remote, &message, now);
    }
  } else if (strcmp(message.command, "status") == 0) {
    if (n->host.discovery) {
      const q2_discovery *discovery = n->host.discovery;
      qa_q2_status status = discovery->status(discovery->ctx);
      char *text = qa_q2_status_text(&status, false, 0);
      size_t total = strlen(text) + 8;
      char *reply = xmalloc(total);
      snprintf(reply, total, "print\n%s", text);
      free(text);
      reply_oob(n, remote, reply);
      free(reply);
    }
  } else if (strcmp(message.command, "info") == 0) {
    if (n->host.discovery) {
      const q2_discovery *discovery = n->host.discovery;
      double argument = message.argument_count ? strtod(message.arguments[0], NULL) : NAN;
      char *text = qa_q2_info_text(discovery->info(discovery->ctx),
          &n->host.protocol, 1, argument);
      if (text) {
        reply_oob(n, remote, text);
        free(text);
      }
    }
  } else if (strcmp(message.command, "getchallenge") == 0) {
    size_t reply_length = 0;
    uint8_t *reply = qa_q2_challenge_table_reply(&n->challenges, remote, now,
        &n->host.protocol, 1, &reply_length);
    if (!reply) qa_range_error("out of memory");
    transport_of(n)->send(transport_of(n)->ctx, remote, reply, reply_length);
    free(reply);
  } else if (strcmp(message.command, "ping") == 0) {
    reply_oob(n, remote, "ack");
  } else if (strcmp(message.command, "connect") == 0) {
    qa_q2_connect_request request;
    if (!qa_q2_read_connect(&message, &request)) qa_range_error("out of memory");
    if (request.protocol.version != n->host.protocol.version) {
      reply_oob(n, remote, "print\nUnsupported protocol.\n");
    } else if (!(is_kex(&request.protocol)
        ? n->lan_live && qa_kex_lan_admitted(&n->lan, remote)
        : qa_q2_challenge_table_validate(&n->challenges, remote, request.challenge))) {
      reply_oob(n, remote, "print\nBad challenge.\n");
    } else {
      char *key = qa_net_address_key(remote, true);
      if (!key) qa_range_error("out of memory");
      peer *existing = find_peer(n, key);
      if (existing) {
        free(key);
        accept_connection(n, remote);
      } else {
        size_t client_count = 0;
        for (size_t i = 0; i < n->peer_count; i++)
          client_count += 1 + n->peers[i]->split_count;
        size_t seats = request.has_social_ids ? request.social_id_count : 1;
        if ((double)(client_count + seats) > n->host.max_clients) {
          free(key);
          reply_oob(n, remote, "print\nServer is full.\n");
        } else {
          qa_q2_connect_request first = request;
          first.split_seat = 0;
          if (first.has_social_ids && first.social_id_count > 1) first.social_id_count = 1;
          qa_app_q2_admission admitted = n->host.admit(n->host.ctx, remote, &first);
          if (!admitted.accepted) {
            char *reply = xmalloc(strlen(admitted.reason) + 10);
            snprintf(reply, strlen(admitted.reason) + 10, "print\n%s\n", admitted.reason);
            reply_oob(n, remote, reply);
            free(reply);
            free(key);
          } else {
            split *splits = NULL;
            size_t split_count = 0;
            const char *failure = NULL;
            if (seats > 1) splits = xmalloc((seats - 1) * sizeof *splits);
            for (size_t seat = 1; seat < seats; seat++) {
              qa_q2_connect_request additional = request;
              additional.split_seat = (double)seat;
              additional.social_ids = request.social_ids + seat;
              additional.social_id_count = 1;
              qa_app_q2_admission extra = n->host.admit(n->host.ctx, remote, &additional);
              if (!extra.accepted) { failure = extra.reason; break; }
              splits[split_count].player = extra.player;
              qa_q2_command_replay_init(&splits[split_count].replay);
              splits[split_count].sequence = 0;
              split_count++;
            }
            if (failure) {
              n->host.disconnect(n->host.ctx, admitted.player, failure);
              for (size_t i = 0; i < split_count; i++)
                n->host.disconnect(n->host.ctx, splits[i].player, failure);
              char *reply = xmalloc(strlen(failure) + 10);
              snprintf(reply, strlen(failure) + 10, "print\n%s\n", failure);
              reply_oob(n, remote, reply);
              free(reply);
              free(splits);
              free(key);
            } else {
              qa_q2_protocol_identity configured = n->host.protocol;
              qa_q2_protocol_identity offered = request.protocol;
              qa_q2_protocol_identity protocol = offered;
              if (configured.kind && offered.kind
                  && ((strcmp(configured.kind, "q2-r1q2") == 0
                          && strcmp(offered.kind, "q2-r1q2") == 0)
                      || (strcmp(configured.kind, "q2-q2pro") == 0
                          && strcmp(offered.kind, "q2-q2pro") == 0))
                  && configured.revision < offered.revision) protocol = configured;
              peer *p = calloc(1, sizeof *p);
              if (!p) qa_range_error("out of memory");
              p->download = qa_app_q2_peer_download_create();
              if (!p->download) qa_range_error("out of memory");
              if (!qa_net_address_clone(remote, &p->remote)) qa_range_error("out of memory");
              p->key = key;
              p->player = admitted.player;
              p->splits = splits;
              p->split_count = split_count;
              qa_q2_channel_options channel_options = {0};
              channel_options.side_server = true;
              channel_options.protocol = protocol;
              channel_options.channel_new = request.channel_new;
              channel_options.qport = request.qport;
              channel_options.has_payload_bytes = true;
              channel_options.payload_bytes = request.payload_bytes;
              channel_options.compress = request.compression;
              channel_options.has_max_datagram_bytes = true;
              int64_t max_datagram = transport_of(n)->max_datagram_bytes(transport_of(n)->ctx);
              channel_options.max_datagram_bytes = max_datagram < 0 ? 65507 : (double)max_datagram;
              qa_q2_channel_init(&p->channel, &channel_options);
              p->protocol = protocol;
              if (!qa_q2_wire_codec_init(&p->wire, protocol)) qa_range_error("out of memory");
              qa_q2_command_replay_init(&p->replay);
              p->last_received = now;
              p->userinfo = copy_text(request.userinfo ? request.userinfo : "");
              if (n->peer_count == n->peer_cap) {
                n->peer_cap = n->peer_cap ? n->peer_cap * 2 : 8;
                n->peers = realloc(n->peers, n->peer_cap * sizeof *n->peers);
                if (!n->peers) qa_range_error("out of memory");
              }
              n->peers[n->peer_count++] = p;
              accept_connection(n, remote);
            }
          }
        }
      }
    }
    qa_q2_connect_request_free(&request);
  } else {
    handled = true;
  }
  qa_q2_connectionless_message_free(&message);
  return handled;
}
static const qa_q2_mvd_capture *capture_mvd(qa_app_q2_server_network *n) {
  if (n->ended || !n->has_mvd_frame || !n->host.mvd_capture)
    qa_range_error("MVD recording requires an active authoritative Q2 capture source");
  return n->host.mvd_capture(n->host.ctx, n->mvd_output, n->mvd_events,
      n->mvd_event_count, n->server_generation);
}
static bool broadcast_authorize(void *ctx, const qa_q2_gtv_client_hello *hello) {
  qa_app_q2_server_network *n = ctx;
  bool enabled = false;
  const char *password = "";
  if (n->host.mvd_settings) n->host.mvd_settings(n->host.ctx, &enabled, NULL, &password);
  (void)enabled;
  return strcmp(hello->password, password) == 0;
}
static void configure_mvd(qa_app_q2_server_network *n) {
  bool enabled = false;
  double max_viewers = 0;
  if (n->host.mvd_settings)
    n->host.mvd_settings(n->host.ctx, &enabled, &max_viewers, NULL);
  if (!enabled) {
    if (n->broadcast_live) {
      qa_q2_mvd_broadcast_close(&n->broadcast);
      n->broadcast_live = false;
    }
    return;
  }
  if (n->broadcast_live) return;
  qa_net_address address;
  if (!transport_of(n)->get_address(transport_of(n)->ctx, &address))
    qa_range_error("out of memory");
  bool is_ip = address.kind == QA_NET_IPV4 || address.kind == QA_NET_IPV6;
  double port = (double)address.port;
  char host[128] = "";
  if (address.kind == QA_NET_IPV4)
    snprintf(host, sizeof host, "%d.%d.%d.%d",
        address.ipv4[0], address.ipv4[1], address.ipv4[2], address.ipv4[3]);
  else if (address.kind == QA_NET_IPV6) snprintf(host, sizeof host, "%s", address.host);
  qa_net_address_destroy(&address);
  if (!is_ip) qa_range_error("GTV broadcast requires an IP server transport");
  if (!n->host.mvd_capture)
    qa_range_error("GTV broadcast requires an authoritative Q2 capture source");
  if (!n->broadcast_init) {
    qa_q2_mvd_broadcast_options options = {n, broadcast_authorize, NULL, NULL, max_viewers};
    qa_q2_mvd_broadcast_init(&n->broadcast, options);
    n->broadcast_init = true;
  }
  qa_q2_mvd_broadcast_listen(&n->broadcast, host, port);
  if (n->ended) {
    qa_q2_mvd_broadcast_close(&n->broadcast);
    return;
  }
  n->broadcast_live = true;
}
static qa_q2_status discovery_status(void *ctx) {
  qa_app_q2_server_network *n = ctx;
  const q2_discovery *discovery = n->host.discovery;
  if (!discovery) qa_range_error("Q2 master publication requires source status");
  return discovery->status(discovery->ctx);
}
static bool sender_send(void *ctx, const qa_net_address *to, const uint8_t *bytes, size_t length) {
  qa_app_q2_server_network *n = ctx;
  return transport_of(n)->send(transport_of(n)->ctx, to, bytes, length);
}
qa_app_q2_server_network *qa_app_q2_server_network_create(qa_app_q2_server_network_options options) {
  qa_app_q2_server_network *n = calloc(1, sizeof *n);
  if (!n) return NULL;
  n->options = options;
  n->host = options.host;
  n->server_generation = 1;
  qa_datagram_transport *base = options.transport;
  n->transport = *base;
  if (is_kex(&options.host.protocol)) {
    qa_kex_lan_options lan_options = {0};
    lan_options.role_host = true;
    lan_options.local_players = 0;
    lan_options.max_players = options.host.max_clients;
    lan_options.name = "Quake II";
    if (!qa_kex_lan_transport_init(&n->lan, base, &lan_options)) { free(n); return NULL; }
    n->lan_live = true;
    n->transport = qa_kex_lan_as_transport(&n->lan);
    n->transport_is_lan = true;
  }
  qa_q2_discovery_wire_init(&n->discovery_wire, &n->discovery_state,
      options.host.protocol, discovery_status, n);
  qa_packet_sender sender = {n, sender_send};
  qa_master_heartbeat_service_init(&n->heartbeat_service, n->discovery_wire, sender, 0);
  wire_supported(&options.host, "Native Q2 wire is unavailable: ");
  if (options.host.protocol.kind && strcmp(options.host.protocol.kind, "q2-kex-demo") == 0)
    qa_range_error("KEX native live transport is unbound");
  if (!qa_q2_challenge_table_init(&n->challenges, options.random, options.random_ctx, 0)) {
    free(n);
    return NULL;
  }
  return n;
}
void qa_app_q2_server_network_destroy(qa_app_q2_server_network *network) {
  if (!network) return;
  for (size_t i = 0; i < network->peer_count; i++) peer_destroy(network, network->peers[i]);
  free(network->peers);
  qa_q2_challenge_table_destroy(&network->challenges);
  if (network->lan_live) qa_kex_lan_transport_destroy(&network->lan);
  if (network->broadcast_init) qa_q2_mvd_broadcast_destroy(&network->broadcast);
  if (network->mvd_owner) {
    qa_q2_mvd_encoder_destroy(&network->mvd_owner->encoder);
    free(network->mvd_owner->token);
    free(network->mvd_owner);
  }
  if (network->server_owner) {
    free(network->server_owner->token);
    free(network->server_owner);
  }
  free(network->pending);
  free(network->poll_result);
  free(network->mvd_messages);
  free(network);
}
const qa_app_q2_server_network_options *qa_app_q2_server_network_options_of(
    const qa_app_q2_server_network *network) { return &network->options; }
qa_app_network_phase qa_app_q2_server_network_phase(const qa_app_q2_server_network *network) {
  return network->ended ? QA_APP_PHASE_CLOSED : QA_APP_PHASE_ACTIVE;
}
size_t qa_app_q2_server_network_clients(qa_app_q2_server_network *network,
    qa_app_q2_player *out, size_t capacity) {
  size_t count = 0;
  for (size_t i = 0; i < network->peer_count; i++) {
    if (count < capacity) out[count] = network->peers[i]->player;
    count++;
    for (size_t j = 0; j < network->peers[i]->split_count; j++) {
      if (count < capacity) out[count] = network->peers[i]->splits[j].player;
      count++;
    }
  }
  return count;
}
void qa_app_q2_server_network_change_world(qa_app_q2_server_network *n,
    qa_app_q2_server_host host) {
  wire_supported(&host, "Native Q2 map transition is unavailable: ");
  if (!host.protocol.kind || !n->host.protocol.kind
      || strcmp(host.protocol.kind, n->host.protocol.kind) != 0
      || host.protocol.version != n->host.protocol.version)
    qa_range_error("Native Q2 map transition cannot change protocol");
  if (n->server_owner) qa_range_error("Finish serverrecord with serverstop before changing maps");
  n->server_demo_seed = NULL;
  size_t count = n->peer_count;
  qa_app_q2_player *players = xmalloc(count * sizeof *players);
  for (size_t i = 0; i < count; i++)
    players[i] = host.carried_player(host.ctx, n->peers[i]->player.client);
  n->host = host;
  n->has_mvd_frame = false;
  n->mvd_seed = NULL;
  n->has_mvd_tick = false;
  n->mvd_message_count = 0;
  n->server_generation++;
  n->pending_count = 0;
  for (size_t i = 0; i < count; i++) {
    peer *p = n->peers[i];
    close_download(p);
    p->player = players[i];
    for (size_t j = 0; j < p->split_count; j++) {
      p->splits[j].player = host.carried_player(host.ctx, p->splits[j].player.client);
      qa_q2_command_replay_init(&p->splits[j].replay);
    }
    if (is_kex(&p->protocol)) {
      char *seat = qa_q2_kex_seat_userinfo(p->userinfo, 0);
      host.userinfo(host.ctx, p->player, seat);
      free(seat);
    } else host.userinfo(host.ctx, p->player, p->userinfo);
    for (size_t j = 0; j < p->split_count; j++) {
      char *seat = qa_q2_kex_seat_userinfo(p->userinfo, (double)j + 1);
      host.userinfo(host.ctx, p->splits[j].player, seat);
      free(seat);
    }
    p->active = false;
    frames_clear(p);
    p->has_game_state = false;
    qa_q2_command_replay_init(&p->replay);
    datagram_clear(p);
    stuff(p, "changing\n");
    qa_q2_server_event reconnect = {0};
    reconnect.kind = QA_Q2_SERVER_RECONNECT;
    reliable(p, &reconnect);
  }
  free(players);
}
void qa_app_q2_server_network_heartbeat(qa_app_q2_server_network *network, double now) {
  if (network->ended) return;
  size_t count = 0;
  const qa_net_address *masters = network->host.masters
      ? (const qa_net_address *)network->host.masters(network->host.ctx, &count) : NULL;
  qa_master_heartbeat_service_send(&network->heartbeat_service, masters, count, now, true, true);
}
bool qa_app_q2_server_network_disconnect_client(qa_app_q2_server_network *network,
    qa_client_id client, const char *reason) {
  peer *p = NULL;
  for (size_t i = 0; i < network->peer_count; i++) {
    if (client_equal(network->peers[i]->player.client, client)) p = network->peers[i];
    for (size_t j = 0; j < network->peers[i]->split_count; j++)
      if (client_equal(network->peers[i]->splits[j].player.client, client))
        p = network->peers[i];
  }
  if (!p) return false;
  char *text = xmalloc(strlen(reason) + 2);
  snprintf(text, strlen(reason) + 2, "%s\n", reason);
  qa_q2_server_event print = {0};
  print.kind = QA_Q2_SERVER_PRINT;
  print.level = 2;
  print.text = text;
  reliable(p, &print);
  free(text);
  qa_q2_server_event disconnect = {0};
  disconnect.kind = QA_Q2_SERVER_DISCONNECT;
  reliable(p, &disconnect);
  qa_q2_channel_send(&p->channel, transport_of(network), &p->remote, NULL, 0, now_performance());
  drop_peer(network, p, reason);
  return true;
}
const void *const *qa_app_q2_server_network_poll(qa_app_q2_server_network *n,
    double now, size_t *out_count) {
  *out_count = 0;
  if (n->lan_live) qa_kex_lan_tick(&n->lan, now);
  if (n->ended) return NULL;
  configure_mvd(n);
  size_t master_count = 0;
  const qa_net_address *masters = n->host.masters
      ? (const qa_net_address *)n->host.masters(n->host.ctx, &master_count) : NULL;
  if (master_count > 0)
    qa_master_heartbeat_service_send(&n->heartbeat_service, masters, master_count, now, true, false);
  for (size_t i = 0; i < n->peer_count;) {
    if (n->peers[i]->download_failure) {
      char *reason = copy_text(n->peers[i]->download_failure);
      drop_peer(n, n->peers[i], reason);
      free(reason);
      continue;
    }
    i++;
  }
  for (;;) {
    qa_receive_event packet;
    if (!transport_of(n)->poll(transport_of(n)->ctx, &packet)) break;
    if (packet.kind != QA_RECV_PACKET) {
      if (packet.kind == QA_RECV_ERROR && packet.error) {
        char *text = xmalloc(strlen(packet.error) + 2);
        snprintf(text, strlen(packet.error) + 2, "%s\n", packet.error);
        n->host.print(n->host.ctx, text);
        free(text);
      }
      qa_receive_event_destroy(&packet);
      continue;
    }
    if (n->host.rejects && n->host.rejects(n->host.ctx, &packet.from)) {
      qa_receive_event_destroy(&packet);
      continue;
    }
    char *key = qa_net_address_key(&packet.from, true);
    if (!key) qa_range_error("out of memory");
    peer *p = find_peer(n, key);
    free(key);
    /* TS catches per-packet errors (drop peer or print reply);
     * landed decoders abort with the same messages. */
    if (connectionless(n, &packet.from, packet.payload, packet.length, now)) {
      qa_receive_event_destroy(&packet);
      continue;
    }
    if (!p) {
      for (size_t i = 0; i < n->peer_count; i++) {
        peer *candidate = n->peers[i];
        if (!qa_net_same_address(&candidate->remote, &packet.from, false)) continue;
        if (packet.length < 10) continue;
        bool matches = candidate->channel.options.protocol.version == 34
            ? (unsigned)(packet.payload[8] | (packet.payload[9] << 8))
                == (unsigned)(int64_t)candidate->channel.options.qport
            : packet.payload[8] == ((unsigned)(int64_t)candidate->channel.options.qport & 255);
        if (matches) p = candidate;
      }
    }
    if (!p) { qa_receive_event_destroy(&packet); continue; }
    qa_q2_channel_receive_result result;
    if (!qa_q2_channel_receive(&p->channel, packet.payload, packet.length, now, &result))
      qa_range_error("out of memory");
    if (result.kind != QA_Q2_CHANNEL_REJECTED) p->last_received = now;
    if (result.kind == QA_Q2_CHANNEL_MESSAGE) {
      if (!qa_net_same_address(&p->remote, &packet.from, true)) {
        for (size_t i = 0; i < n->peer_count; i++)
          if (n->peers[i] == p) {
            memmove(n->peers + i, n->peers + i + 1,
                (n->peer_count - i - 1) * sizeof *n->peers);
            n->peer_count--;
            break;
          }
        qa_net_address_destroy(&p->remote);
        free(p->key);
        if (!qa_net_address_clone(&packet.from, &p->remote)) qa_range_error("out of memory");
        p->key = qa_net_address_key(&p->remote, true);
        if (!p->key) qa_range_error("out of memory");
        n->peers[n->peer_count++] = p;
      }
      process_peer(n, p, &result);
    }
    qa_q2_channel_receive_result_free(&result);
    qa_receive_event_destroy(&packet);
  }
  for (size_t i = 0; i < n->peer_count;) {
    peer *p = n->peers[i];
    double timeout = n->options.has_timeout_milliseconds
        ? n->options.timeout_milliseconds : 125000;
    if (now - p->last_received > timeout) {
      drop_peer(n, p, "Connection timed out");
      continue;
    }
    if (!p->active && qa_q2_channel_should_update(&p->channel, now))
      qa_q2_channel_send(&p->channel, transport_of(n), &p->remote, NULL, 0, now);
    i++;
  }
  if (n->poll_result_cap < n->pending_count) {
    n->poll_result = realloc(n->poll_result, n->pending_count * sizeof *n->poll_result);
    if (!n->poll_result) qa_range_error("out of memory");
    n->poll_result_cap = n->pending_count;
  }
  for (size_t i = 0; i < n->pending_count; i++) n->poll_result[i] = n->pending[i];
  *out_count = n->pending_count;
  n->pending_count = 0;
  return n->poll_result;
}
void qa_app_q2_server_network_submit(qa_app_q2_server_network *network,
    const void *const *commands, size_t count, double now) {
  (void)network; (void)commands; (void)count; (void)now;
  qa_range_error("Q2 server commands must enter the authoritative application input batch");
}
static uint8_t *join_packets(uint8_t *const *packets, const size_t *lengths,
    size_t count, size_t *out_length) {
  size_t total = 0;
  for (size_t i = 0; i < count; i++) total += lengths[i];
  uint8_t *out = xmalloc(total);
  size_t offset = 0;
  for (size_t i = 0; i < count; i++) {
    memcpy(out + offset, packets[i], lengths[i]);
    offset += lengths[i];
  }
  *out_length = total;
  return out;
}
static bool event_reliable(const qa_app_q2_server_event *event) {
  if (event->has_reliable) return event->reliable;
  const qa_q2_server_event *wire = event->event;
  return wire->kind != QA_Q2_SERVER_SOUND && wire->kind != QA_Q2_SERVER_MUZZLE_FLASH
      && wire->kind != QA_Q2_SERVER_TEMPORARY_ENTITY;
}
static qa_q2_wire_frame clone_frame(const qa_q2_wire_frame *frame) {
  qa_q2_wire_frame out = *frame;
  out.area_bits = xmalloc(frame->area_bits_length);
  memcpy(out.area_bits, frame->area_bits, frame->area_bits_length);
  out.split_players = xmalloc(frame->split_count * sizeof *out.split_players);
  for (size_t i = 0; i < frame->split_count; i++) {
    out.split_players[i] = frame->split_players[i];
    out.split_players[i].area_bits = xmalloc(frame->split_players[i].area_bits_length);
    memcpy(out.split_players[i].area_bits, frame->split_players[i].area_bits,
        frame->split_players[i].area_bits_length);
  }
  out.entities = xmalloc(frame->entity_count * sizeof *out.entities);
  if (frame->entity_count) memcpy(out.entities, frame->entities,
      frame->entity_count * sizeof *out.entities);
  return out;
}
void qa_app_q2_server_network_publish(qa_app_q2_server_network *n,
    const void *output, const void *const *events, size_t event_count,
    bool frame_time_is_seconds, double frame_time_value, double now) {
  if (n->ended) return;
  n->host.observe(n->host.ctx, output, events, event_count);
  n->mvd_output = output;
  n->mvd_events = events;
  n->mvd_event_count = event_count;
  n->has_mvd_frame = true;
  bool need_capture = n->mvd_owner != NULL || n->broadcast_live || n->server_owner != NULL;
  const qa_q2_mvd_capture *current = need_capture ? capture_mvd(n) : NULL;
  if (n->server_owner && current) {
    qa_q2_wire_codec wire;
    qa_q2_protocol_identity classic = {"q2-classic", 34, 0, 0};
    if (!qa_q2_wire_codec_init(&wire, classic)) qa_range_error("out of memory");
    uint8_t **multicasts = NULL;
    size_t *lengths = NULL;
    size_t multicast_count = 0, multicast_cap = 0;
    for (size_t i = 0; i < current->message_count; i++) {
      const qa_q2_mvd_emission *message = &current->messages[i];
      if (message->recipient.kind == QA_Q2_MVD_RECIPIENT_PLAYER) continue;
      uint8_t first = message->length ? message->bytes[0] : 0;
      if (!(first == 1 || first == 2 || first == 3 || first == 9)) continue;
      if (multicast_count == multicast_cap) {
        multicast_cap = multicast_cap ? multicast_cap * 2 : 8;
        multicasts = realloc(multicasts, multicast_cap * sizeof *multicasts);
        lengths = realloc(lengths, multicast_cap * sizeof *lengths);
        if (!multicasts || !lengths) qa_range_error("out of memory");
      }
      multicasts[multicast_count] = (uint8_t *)message->bytes;
      lengths[multicast_count] = message->length;
      multicast_count++;
    }
    uint8_t **owned = xmalloc(multicast_count * sizeof *owned);
    size_t owned_count = 0;
    for (size_t i = 0; i < current->config_string_count; i++) {
      const qa_q2_mvd_capture_config *config = &current->config_strings[i];
      const char *previous = NULL;
      for (size_t j = 0; j < n->server_owner->config_count; j++)
        if (n->server_owner->configs[j].index == config->index)
          previous = n->server_owner->configs[j].value;
      if (!previous || strcmp(previous, config->value) != 0) {
        qa_q2_server_event event = {0};
        event.kind = QA_Q2_SERVER_CONFIG_STRING;
        event.index = config->index;
        event.text = (char *)config->value;
        size_t entry_length = 0;
        uint8_t *bytes = qa_q2_encode_server_event(&wire, &event, &entry_length);
        if (!bytes) qa_range_error("out of memory");
        owned[owned_count++] = bytes;
        if (multicast_count == multicast_cap) {
          multicast_cap = multicast_cap ? multicast_cap * 2 : 8;
          multicasts = realloc(multicasts, multicast_cap * sizeof *multicasts);
          lengths = realloc(lengths, multicast_cap * sizeof *lengths);
          if (!multicasts || !lengths) qa_range_error("out of memory");
        }
        multicasts[multicast_count] = bytes;
        lengths[multicast_count] = entry_length;
        multicast_count++;
      }
    }
    n->server_owner->configs = current->config_strings;
    n->server_owner->config_count = current->config_string_count;
    const qa_simulation_output *simulation = output;
    qa_q2_byte_span *spans = xmalloc(multicast_count * sizeof *spans);
    for (size_t i = 0; i < multicast_count; i++) {
      spans[i].bytes = multicasts[i];
      spans[i].length = lengths[i];
    }
    size_t frame_length = 0;
    uint8_t *frame_bytes = qa_q2_encode_server_demo_frame(
        simulation->snapshot.frame.frame, current->entities, current->entity_count,
        spans, multicast_count, &frame_length);
    free(spans);
    if (!frame_bytes) qa_range_error("out of memory");
    n->server_owner->sink.append(n->server_owner->sink.ctx, "q2-server",
        frame_bytes, frame_length);
    free(frame_bytes);
    for (size_t i = 0; i < owned_count; i++) free(owned[i]);
    free(owned);
    free(multicasts);
    free(lengths);
  }
  if ((n->mvd_owner || n->broadcast_live) && current) {
    double ticks = frame_time_is_seconds ? frame_time_value * 10 : frame_time_value / 100;
    double tick = floor(ticks + 1e-7);
    for (size_t i = 0; i < current->message_count; i++) {
      if (n->mvd_message_count == n->mvd_message_cap) {
        n->mvd_message_cap = n->mvd_message_cap ? n->mvd_message_cap * 2 : 16;
        n->mvd_messages = realloc(n->mvd_messages, n->mvd_message_cap * sizeof *n->mvd_messages);
        if (!n->mvd_messages) qa_range_error("out of memory");
      }
      n->mvd_messages[n->mvd_message_count++] = &current->messages[i];
    }
    if (!n->has_mvd_tick || tick != n->mvd_tick) {
      n->mvd_tick = tick;
      n->has_mvd_tick = true;
      qa_q2_mvd_capture capture = *current;
      /* Borrowed emission pointers flattened into a capture array. */
      qa_q2_mvd_emission *flat = xmalloc(n->mvd_message_count * sizeof *flat);
      for (size_t i = 0; i < n->mvd_message_count; i++) flat[i] = *n->mvd_messages[i];
      capture.messages = flat;
      capture.message_count = n->mvd_message_count;
      n->mvd_message_count = 0;
      if (n->broadcast_live) qa_q2_mvd_broadcast_observe(&n->broadcast, &capture);
      if (n->mvd_owner) {
        qa_q2_mvd_byte_block *blocks = NULL;
        size_t block_count = 0;
        if (!qa_q2_mvd_encoder_capture(&n->mvd_owner->encoder, &capture,
                &blocks, &block_count))
          qa_range_error("out of memory");
        for (size_t i = 0; i < block_count; i++)
          n->mvd_owner->sink.append(n->mvd_owner->sink.ctx, "mvd",
              blocks[i].bytes, blocks[i].length);
        qa_q2_mvd_byte_blocks_free(blocks, block_count);
      }
      free(flat);
    }
  } else {
    n->has_mvd_tick = false;
    n->mvd_message_count = 0;
  }
  for (size_t pi = 0; pi < n->peer_count; pi++) {
    peer *p = n->peers[pi];
    if (n->host.raw_messages) {
      size_t count = 0;
      const qa_app_q2_raw_message *messages = n->host.raw_messages(n->host.ctx, p->player, &count);
      for (size_t i = 0; i < count; i++) {
        if (messages[i].reliable)
          qa_q2_channel_queue_reliable(&p->channel, messages[i].bytes, messages[i].length);
        else if (p->active) {
          uint8_t *copy = xmalloc(messages[i].length);
          memcpy(copy, messages[i].bytes, messages[i].length);
          datagram_push(p, copy, messages[i].length);
        }
      }
    }
    for (size_t si = 0; si < p->split_count; si++) {
      uint8_t prefix[2] = {21, (uint8_t)(si + 2)};
      uint8_t suffix[2] = {21, 1};
      if (n->host.raw_messages) {
        size_t count = 0;
        const qa_app_q2_raw_message *messages =
            n->host.raw_messages(n->host.ctx, p->splits[si].player, &count);
        for (size_t i = 0; i < count; i++) {
          uint8_t *parts[] = {prefix, (uint8_t *)messages[i].bytes, suffix};
          size_t part_lengths[] = {2, messages[i].length, 2};
          size_t length = 0;
          uint8_t *joined = join_packets(parts, part_lengths, 3, &length);
          if (messages[i].reliable) {
            qa_q2_channel_queue_reliable(&p->channel, joined, length);
            free(joined);
          } else if (p->active) datagram_push(p, joined, length);
          else free(joined);
        }
      }
      if (p->active && n->host.events) {
        size_t count = 0;
        const qa_app_q2_server_event *host_events = n->host.events(n->host.ctx,
            p->splits[si].player, output, events, event_count, &count);
        for (size_t i = 0; i < count; i++) {
          size_t entry_length = 0;
          uint8_t *encoded = qa_q2_encode_server_event(&p->wire,
              (const qa_q2_server_event *)host_events[i].event, &entry_length);
          if (!encoded) qa_range_error("out of memory");
          uint8_t *parts[] = {prefix, encoded, suffix};
          size_t part_lengths[] = {2, entry_length, 2};
          size_t length = 0;
          uint8_t *joined = join_packets(parts, part_lengths, 3, &length);
          free(encoded);
          if (event_reliable(&host_events[i])) {
            qa_q2_channel_queue_reliable(&p->channel, joined, length);
            free(joined);
          } else datagram_push(p, joined, length);
        }
      }
    }
    if (!p->active) continue;
    const qa_q2_wire_frame *primary = n->host.frame(n->host.ctx, p->player, output, &p->protocol);
    qa_q2_wire_frame frame = *primary;
    qa_q2_wire_frame *split_frames = NULL;
    if (p->split_count) {
      split_frames = xmalloc(p->split_count * sizeof *split_frames);
      for (size_t si = 0; si < p->split_count; si++)
        split_frames[si] = *(const qa_q2_wire_frame *)n->host.frame(
            n->host.ctx, p->splits[si].player, output, &p->protocol);
      /* Merge entities by number, sorted ascending. */
      size_t total = primary->entity_count;
      for (size_t si = 0; si < p->split_count; si++) total += split_frames[si].entity_count;
      qa_q2_wire_entity *merged = xmalloc(total * sizeof *merged);
      size_t merged_count = 0;
      for (size_t i = 0; i < primary->entity_count; i++) merged[merged_count++] = primary->entities[i];
      for (size_t si = 0; si < p->split_count; si++)
        for (size_t i = 0; i < split_frames[si].entity_count; i++) {
          bool replaced = false;
          for (size_t j = 0; j < merged_count; j++)
            if (merged[j].number == split_frames[si].entities[i].number) {
              merged[j] = split_frames[si].entities[i];
              replaced = true;
            }
          if (!replaced) merged[merged_count++] = split_frames[si].entities[i];
        }
      for (size_t i = 0; i < merged_count; i++)
        for (size_t j = i + 1; j < merged_count; j++)
          if (merged[j].number < merged[i].number) {
            qa_q2_wire_entity swap = merged[i];
            merged[i] = merged[j];
            merged[j] = swap;
          }
      frame.entities = merged;
      frame.entity_count = merged_count;
      frame.has_split = true;
      frame.split_players = xmalloc(p->split_count * sizeof *frame.split_players);
      frame.split_count = p->split_count;
      for (size_t si = 0; si < p->split_count; si++) {
        frame.split_players[si].area_bits = split_frames[si].area_bits;
        frame.split_players[si].area_bits_length = split_frames[si].area_bits_length;
        frame.split_players[si].player = split_frames[si].player;
      }
    }
    if (n->host.events) {
      size_t count = 0;
      const qa_app_q2_server_event *host_events = n->host.events(n->host.ctx,
          p->player, output, events, event_count, &count);
      for (size_t i = 0; i < count; i++) {
        size_t entry_length = 0;
        uint8_t *encoded = qa_q2_encode_server_event(&p->wire,
            (const qa_q2_server_event *)host_events[i].event, &entry_length);
        if (!encoded) qa_range_error("out of memory");
        if (event_reliable(&host_events[i])) {
          qa_q2_channel_queue_reliable(&p->channel, encoded, entry_length);
          free(encoded);
        } else datagram_push(p, encoded, entry_length);
      }
    }
    bool have_frame = false;
    for (size_t i = 0; i < p->frame_count; i++)
      if (p->frames[i].number == frame.server_frame) have_frame = true;
    if (qa_q2_channel_get_fragment_pending(&p->channel)) {
      qa_q2_channel_send(&p->channel, transport_of(n), &p->remote, NULL, 0, now);
      free(split_frames);
      if (p->split_count) { free(frame.entities); free(frame.split_players); }
      continue;
    }
    if (have_frame) {
      if (qa_q2_channel_should_update(&p->channel, now))
        qa_q2_channel_send(&p->channel, transport_of(n), &p->remote, NULL, 0, now);
      free(split_frames);
      if (p->split_count) { free(frame.entities); free(frame.split_players); }
      continue;
    }
    const qa_q2_wire_frame *old = NULL;
    if (p->replay.last_frame >= 0)
      for (size_t i = 0; i < p->frame_count; i++)
        if (p->frames[i].number == p->replay.last_frame) old = &p->frames[i].frame;
    if (!p->has_game_state) qa_range_error("Active Q2 client has no baselines");
    const qa_q2_baseline_list *baselines = p->game_state.baselines;
    size_t frame_length = 0;
    uint8_t *frame_bytes = qa_q2_encode_frame(&p->wire, &frame, old,
        baselines, n->host.max_clients, &frame_length);
    if (!frame_bytes) qa_range_error("out of memory");
    size_t datagram_length = frame_length;
    for (size_t i = 0; i < p->datagram_count; i++) datagram_length += p->datagram_lengths[i];
    uint8_t *datagram = xmalloc(datagram_length);
    memcpy(datagram, frame_bytes, frame_length);
    size_t offset = frame_length;
    for (size_t i = 0; i < p->datagram_count; i++) {
      memcpy(datagram + offset, p->datagram[i], p->datagram_lengths[i]);
      offset += p->datagram_lengths[i];
    }
    free(frame_bytes);
    datagram_clear(p);
    qa_q2_channel_send(&p->channel, transport_of(n), &p->remote, datagram, datagram_length, now);
    free(datagram);
    if (p->frame_count == p->frame_cap) {
      p->frame_cap = p->frame_cap ? p->frame_cap * 2 : 16;
      p->frames = realloc(p->frames, p->frame_cap * sizeof *p->frames);
      if (!p->frames) qa_range_error("out of memory");
    }
    p->frames[p->frame_count].number = frame.server_frame;
    p->frames[p->frame_count].frame = clone_frame(&frame);
    p->frame_count++;
    while (p->frame_count > 16) {
      qa_q2_wire_frame_free(&p->frames[0].frame);
      memmove(p->frames, p->frames + 1, (p->frame_count - 1) * sizeof *p->frames);
      p->frame_count--;
    }
    free(split_frames);
    if (p->split_count) { free(frame.entities); free(frame.split_players); }
  }
}
void qa_app_q2_server_network_close(qa_app_q2_server_network *n) {
  if (n->ended) return;
  n->ended = true;
  n->server_owner = n->server_owner;
  (void)0;
  n->mvd_owner = NULL;
  /* TS aggregates failures; C cleanup steps abort on failure. */
  size_t master_count = 0;
  const qa_net_address *masters = n->host.masters
      ? (const qa_net_address *)n->host.masters(n->host.ctx, &master_count) : NULL;
  qa_master_heartbeat_service_send(&n->heartbeat_service, masters, master_count,
      now_performance(), false, true);
  for (size_t i = 0; i < n->peer_count; i++) {
    peer *p = n->peers[i];
    close_download(p);
    qa_q2_server_event disconnect = {0};
    disconnect.kind = QA_Q2_SERVER_DISCONNECT;
    reliable(p, &disconnect);
    qa_q2_channel_send(&p->channel, transport_of(n), &p->remote, NULL, 0, now_performance());
    n->host.disconnect(n->host.ctx, p->player, "Server shutdown");
    for (size_t j = 0; j < p->split_count; j++)
      n->host.disconnect(n->host.ctx, p->splits[j].player, "Server shutdown");
  }
  for (size_t i = 0; i < n->peer_count; i++) peer_destroy(n, n->peers[i]);
  n->peer_count = 0;
  n->pending_count = 0;
  transport_of(n)->close(transport_of(n)->ctx);
  if (n->broadcast_live) {
    qa_q2_mvd_broadcast_close(&n->broadcast);
    n->broadcast_live = false;
  }
}
void qa_app_q2_server_recording_seed_free(qa_app_q2_server_recording_seed *seed) {
  free(seed->message);
  memset(seed, 0, sizeof *seed);
}
void qa_app_q2_server_network_server_recording_seed(qa_app_q2_server_network *network,
    qa_app_q2_server_recording_seed *out) {
  const qa_q2_mvd_capture *capture = capture_mvd(network);
  if (capture->revision != 2010)
    qa_range_error("serverrecord requires classic Quake II; use mvdrecord for other source revisions");
  if (network->server_owner) qa_range_error("Already doing a serverrecord");
  network->server_demo_seed = capture;
  qa_q2_server_demo_state state = {0};
  state.servercount = capture->servercount;
  state.gamedir = (char *)capture->gamedir;
  qa_q2_server_demo_config *demo_configs =
      xmalloc(capture->config_string_count * sizeof *demo_configs);
  for (size_t i = 0; i < capture->config_string_count; i++) {
    demo_configs[i].index = capture->config_strings[i].index;
    demo_configs[i].value = capture->config_strings[i].value;
  }
  state.config_strings = demo_configs;
  state.config_count = capture->config_string_count;
  size_t length = 0;
  uint8_t *message = qa_q2_encode_server_demo_signon(&state, &length);
  free(demo_configs);
  if (!message) qa_range_error("out of memory");
  out->kind = "q2-server";
  out->protocol = 34;
  out->message = message;
  out->length = length;
}
void *qa_app_q2_server_network_server_recording_attach(qa_app_q2_server_network *network,
    qa_app_q2_recording_sink sink) {
  const qa_q2_mvd_capture *seed = network->server_demo_seed;
  if (network->ended || !seed || network->server_owner
      || seed->servercount != network->server_generation)
    qa_range_error("Server recording requires a current unused seed");
  server_recording_owner *owner = calloc(1, sizeof *owner);
  if (!owner) qa_range_error("out of memory");
  owner->sink = sink;
  owner->generation = seed->servercount;
  owner->configs = seed->config_strings;
  owner->config_count = seed->config_string_count;
  owner->token = xmalloc(1);
  network->server_demo_seed = NULL;
  network->server_owner = owner;
  return owner->token;
}
void qa_app_q2_server_network_server_recording_detach(qa_app_q2_server_network *network,
    void *token) {
  if (network->server_owner && network->server_owner->token == token) {
    free(network->server_owner->token);
    free(network->server_owner);
    network->server_owner = NULL;
  }
}
void qa_app_q2_mvd_recording_seed_free(qa_app_q2_mvd_recording_seed *seed) {
  for (size_t i = 0; i < seed->packet_count; i++) free(seed->packets[i]);
  free(seed->packets);
  free(seed->lengths);
  memset(seed, 0, sizeof *seed);
}
void qa_app_q2_server_network_mvd_recording_seed(qa_app_q2_server_network *network,
    qa_app_q2_mvd_recording_seed *out) {
  const qa_q2_mvd_capture *capture = capture_mvd(network);
  double revision = capture->revision;
  if (revision != 2009 && revision != 2010 && revision != 2011 && revision != 2012
      && revision != 2013 && revision != 3038)
    qa_range_error("Unsupported MVD recording revision");
  qa_q2_mvd_encoder encoder;
  qa_q2_mvd_encoder_init(&encoder);
  qa_q2_mvd_capture empty = *capture;
  empty.messages = NULL;
  empty.message_count = 0;
  memset(out, 0, sizeof *out);
  out->revision = revision;
  qa_q2_mvd_byte_block *blocks = NULL;
  if (!qa_q2_mvd_encoder_capture(&encoder, &empty, &blocks, &out->packet_count))
    qa_range_error("out of memory");
  out->packets = xmalloc(out->packet_count * sizeof *out->packets);
  out->lengths = xmalloc(out->packet_count * sizeof *out->lengths);
  for (size_t i = 0; i < out->packet_count; i++) {
    out->packets[i] = blocks[i].bytes;
    out->lengths[i] = blocks[i].length;
  }
  free(blocks);
  qa_q2_mvd_encoder_destroy(&encoder);
  network->mvd_seed = capture;
}
void *qa_app_q2_server_network_mvd_recording_attach(qa_app_q2_server_network *network,
    qa_app_q2_recording_sink sink) {
  if (network->ended || network->mvd_owner || !network->mvd_seed)
    qa_range_error("MVD recording requires an unused server seed");
  mvd_recording_owner *owner = calloc(1, sizeof *owner);
  if (!owner) qa_range_error("out of memory");
  owner->sink = sink;
  qa_q2_mvd_encoder_init(&owner->encoder);
  qa_q2_mvd_capture empty = *network->mvd_seed;
  empty.messages = NULL;
  empty.message_count = 0;
  qa_q2_mvd_byte_block *blocks = NULL;
  size_t count = 0;
  if (!qa_q2_mvd_encoder_capture(&owner->encoder, &empty, &blocks, &count))
    qa_range_error("out of memory");
  qa_q2_mvd_byte_blocks_free(blocks, count);
  network->mvd_seed = NULL;
  owner->token = xmalloc(1);
  network->mvd_owner = owner;
  if (!network->broadcast_live) {
    network->has_mvd_tick = true;
    double seconds = 0;
    const qa_simulation_output *output = network->mvd_output;
    seconds = output->snapshot.frame.time.kind == QA_SOURCE_TIME_SECONDS
        ? output->snapshot.frame.time.value * 10 : output->snapshot.frame.time.value / 100;
    network->mvd_tick = floor(seconds + 1e-7);
    network->mvd_message_count = 0;
  }
  return owner->token;
}
void qa_app_q2_server_network_mvd_recording_detach(qa_app_q2_server_network *network,
    void *token) {
  if (network->mvd_owner && network->mvd_owner->token == token) {
    qa_q2_mvd_encoder_destroy(&network->mvd_owner->encoder);
    free(network->mvd_owner->token);
    free(network->mvd_owner);
    network->mvd_owner = NULL;
  }
}
static qa_app_network_phase srv_phase(void *ctx) {
  return qa_app_q2_server_network_phase(ctx);
}
static const void *srv_wire(void *ctx) {
  qa_app_q2_server_network *n = ctx;
  return &n->host.protocol;
}
static const void *const *srv_poll(void *ctx, double now, size_t *out_count) {
  return qa_app_q2_server_network_poll(ctx, now, out_count);
}
static void srv_submit(void *ctx, const void *const *commands, size_t count, double now) {
  qa_app_q2_server_network_submit(ctx, commands, count, now);
}
static void srv_publish(void *ctx, const void *output, const void *const *events,
    size_t count, double now) {
  const qa_simulation_output *simulation = output;
  qa_app_q2_server_network_publish(ctx, output, events, count,
      simulation->snapshot.frame.time.kind == QA_SOURCE_TIME_SECONDS,
      simulation->snapshot.frame.time.value, now);
}
static void srv_heartbeat(void *ctx, double now) {
  qa_app_q2_server_network_heartbeat(ctx, now);
}
static void srv_close(void *ctx) { qa_app_q2_server_network_close(ctx); }
qa_app_application_network qa_app_q2_server_network_as_network(qa_app_q2_server_network *network) {
  qa_app_application_network result = {0};
  result.ctx = network;
  result.role = QA_APP_ROLE_SERVER;
  result.phase = srv_phase;
  result.wire = srv_wire;
  result.heartbeat = srv_heartbeat;
  result.poll = srv_poll;
  result.submit = srv_submit;
  result.publish = srv_publish;
  result.close = srv_close;
  return result;
}


/* Q2ClientNetwork. */
typedef struct pending_usercmd { qa_q2_wire_usercmd command; } pending_usercmd;
struct qa_app_q2_client_network {
  qa_app_q2_client_network_options options;
  qa_app_q2_client_host host;
  qa_net_address remote;
  qa_datagram_transport transport;
  qa_kex_lan_transport lan; bool lan_live;
  qa_q2_client_handshake handshake;
  qa_app_q2_client_receiver *receiver;
  qa_q2_channel channel; bool channel_live;
  qa_app_network_phase state;
  bool has_last_received; double last_received;
  qa_q2_wire_usercmd previous, oldest;
  pending_usercmd *pending; size_t pending_count, pending_cap;
  qa_app_q2_recording_sink recording_sink; bool recording;
  bool recording_waiting_full;
};
static bool host_kex(qa_app_q2_client_network *n) { return is_kex(&n->host.protocol); }
static qa_app_network_phase client_phase(qa_app_q2_client_network *n) {
  if (!n->channel_live || n->state == QA_APP_PHASE_CLOSED
      || n->state == QA_APP_PHASE_REJECTED) return n->state;
  const char *phase = qa_app_q2_client_receiver_phase(n->receiver);
  if (strcmp(phase, "active") == 0) return QA_APP_PHASE_ACTIVE;
  if (strcmp(phase, "closed") == 0) return QA_APP_PHASE_CLOSED;
  return QA_APP_PHASE_LOADING;
}
void qa_app_q2_client_network_command(qa_app_q2_client_network *n, const char *text) {
  if (!n->channel_live) qa_range_error("Q2 client is not connected");
  qa_q2_client_event event = {0};
  event.kind = QA_Q2_CLIENT_COMMAND;
  event.text = (char *)text;
  size_t length = 0;
  uint8_t *bytes = qa_q2_encode_client_control(&event, host_kex(n), &length);
  if (!bytes) qa_range_error("out of memory");
  qa_q2_channel_queue_reliable(&n->channel, bytes, length);
  free(bytes);
}
void qa_app_q2_client_network_userinfo(qa_app_q2_client_network *n, const char *text) {
  if (!n->channel_live) return;
  char *seat = NULL;
  const char *value = text;
  if (host_kex(n)) {
    seat = qa_q2_kex_client_userinfo(text);
    if (!seat) qa_range_error("out of memory");
    value = seat;
  }
  qa_q2_client_event event = {0};
  event.kind = QA_Q2_CLIENT_USERINFO;
  event.text = (char *)value;
  size_t length = 0;
  uint8_t *bytes = qa_q2_encode_client_control(&event, host_kex(n), &length);
  if (!bytes) qa_range_error("out of memory");
  qa_q2_channel_queue_reliable(&n->channel, bytes, length);
  free(bytes);
  free(seat);
}
static void source_command(void *ctx, const char *text) {
  qa_app_q2_client_network_command(ctx, text);
}
static void source_reset_commands(void *ctx) {
  qa_app_q2_client_network *n = ctx;
  memset(&n->previous, 0, sizeof n->previous);
  memset(&n->oldest, 0, sizeof n->oldest);
  n->pending_count = 0;
}
static bool source_closed(void *ctx) {
  qa_app_q2_client_network *n = ctx;
  qa_datagram_transport *base = n->options.transport;
  return base->is_closed(base->ctx);
}
static qa_app_q2_download_preparation downloads_prepare(void *ctx, const qa_app_q2_game_state *state) {
  return qa_app_q2_download_receiver_prepare(ctx, state);
}
static qa_app_q2_download_receive downloads_receive(void *ctx, const qa_q2_server_event *block) {
  return qa_app_q2_download_receiver_receive(ctx, block);
}
static void downloads_close(void *ctx) { qa_app_q2_download_receiver_close(ctx); }
qa_app_q2_client_network *qa_app_q2_client_network_create(qa_app_q2_client_network_options options) {
  qa_app_q2_client_network *n = calloc(1, sizeof *n);
  if (!n) return NULL;
  n->options = options;
  n->host = options.host;
  n->state = QA_APP_PHASE_CHALLENGING;
  if (!qa_net_address_clone(options.remote, &n->remote)) { free(n); return NULL; }
  qa_datagram_transport *base = options.transport;
  n->transport = *base;
  if (host_kex(n)) {
    qa_kex_lan_options lan_options = {0};
    lan_options.role_host = false;
    lan_options.local_players = 1;
    lan_options.server = n->remote;
    if (!qa_kex_lan_transport_init(&n->lan, base, &lan_options)) {
      qa_net_address_destroy(&n->remote);
      free(n);
      return NULL;
    }
    n->lan_live = true;
    n->transport = qa_kex_lan_as_transport(&n->lan);
  }
  qa_app_q2_receiver_source source = {0};
  source.is_demo = false;
  if (options.host.downloads) {
    source.has_downloads = true;
    source.downloads.ctx = (void *)options.host.downloads;
    source.downloads.prepare = downloads_prepare;
    source.downloads.receive = downloads_receive;
    source.downloads.close = downloads_close;
  }
  source.ctx = n;
  source.command = source_command;
  source.reset_commands = source_reset_commands;
  source.closed = source_closed;
  n->receiver = qa_app_q2_client_receiver_create(options.host, source);
  if (!n->receiver) {
    if (n->lan_live) qa_kex_lan_transport_destroy(&n->lan);
    qa_net_address_destroy(&n->remote);
    free(n);
    return NULL;
  }
  double payload = 65507;
  int64_t max_datagram = base->max_datagram_bytes(base->ctx);
  if (max_datagram >= 0) payload = (double)max_datagram;
  payload -= 12;
  if (payload > 1390) payload = 1390;
  if (!qa_q2_client_handshake_init(&n->handshake, &n->remote, &options.host.protocol, 1,
          options.qport, options.host.userinfo, options.host.ctx, true, payload,
          true, 3000, options.host.social_id, options.host.ctx)) {
    qa_app_q2_client_receiver_destroy(n->receiver);
    if (n->lan_live) qa_kex_lan_transport_destroy(&n->lan);
    qa_net_address_destroy(&n->remote);
    free(n);
    return NULL;
  }
  return n;
}
void qa_app_q2_client_network_destroy(qa_app_q2_client_network *network) {
  if (!network) return;
  qa_q2_client_handshake_destroy(&network->handshake);
  qa_app_q2_client_receiver_destroy(network->receiver);
  if (network->channel_live) qa_q2_channel_destroy(&network->channel);
  if (network->lan_live) qa_kex_lan_transport_destroy(&network->lan);
  qa_net_address_destroy(&network->remote);
  free(network->pending);
  free(network);
}
const qa_app_q2_client_network_options *qa_app_q2_client_network_options_of(
    const qa_app_q2_client_network *network) { return &network->options; }
qa_q2_server_message_reader *qa_app_q2_client_network_reader(qa_app_q2_client_network *network) {
  return qa_app_q2_client_receiver_reader(network->receiver);
}
qa_app_q2_client_receiver *qa_app_q2_client_network_receiver(qa_app_q2_client_network *network) {
  return network->receiver;
}
qa_app_network_phase qa_app_q2_client_network_phase(const qa_app_q2_client_network *network) {
  return client_phase((qa_app_q2_client_network *)network);
}
double qa_app_q2_client_network_acknowledged_frame(const qa_app_q2_client_network *network) {
  return qa_app_q2_client_receiver_acknowledged_frame(network->receiver);
}
static void send_pending(qa_app_q2_client_network *n, double now) {
  if (!n->channel_live) return;
  qa_app_network_phase phase = client_phase(n);
  if (phase == QA_APP_PHASE_CLOSED || phase == QA_APP_PHASE_REJECTED) return;
  qa_q2_server_message_reader *reader = qa_app_q2_client_receiver_reader(n->receiver);
  for (size_t i = 0; i < n->pending_count; i++) {
    qa_q2_wire_usercmd command = n->pending[i].command;
    double sequence = (double)qa_q2_channel_get_outgoing_sequence(&n->channel);
    qa_q2_wire_usercmd trio[3] = {n->oldest, n->previous, command};
    size_t length = 0;
    uint8_t *bytes = qa_q2_encode_move(&reader->wire, sequence,
        qa_app_q2_client_receiver_acknowledged_frame(n->receiver), trio, &length);
    if (!bytes) qa_range_error("out of memory");
    qa_q2_channel_send(&n->channel, &n->transport, &n->remote, bytes, length, now);
    free(bytes);
    if (n->host.prediction_sent)
      n->host.prediction_sent(n->host.ctx, sequence, &n->pending[i].command, now);
    n->oldest = n->previous;
    n->previous = command;
  }
  n->pending_count = 0;
  if (qa_q2_channel_should_update(&n->channel, now))
    qa_q2_channel_send(&n->channel, &n->transport, &n->remote, NULL, 0, now);
}
const void *const *qa_app_q2_client_network_poll(qa_app_q2_client_network *n,
    double now, size_t *out_count) {
  *out_count = 0;
  if (n->lan_live) qa_kex_lan_tick(&n->lan, now);
  qa_app_network_phase phase = client_phase(n);
  if (phase == QA_APP_PHASE_CLOSED || phase == QA_APP_PHASE_REJECTED) return NULL;
  if (!n->channel_live && (!n->lan_live || qa_kex_lan_get_ready(&n->lan))) {
    size_t length = 0;
    uint8_t *packet = qa_q2_client_handshake_poll(&n->handshake, now, &length);
    if (packet) {
      n->transport.send(n->transport.ctx, &n->remote, packet, length);
      free(packet);
    }
  }
  for (;;) {
    qa_receive_event packet;
    if (!n->transport.poll(n->transport.ctx, &packet)) break;
    if (packet.kind != QA_RECV_PACKET) {
      if (packet.kind == QA_RECV_ERROR && packet.error) {
        char *text = xmalloc(strlen(packet.error) + 2);
        snprintf(text, strlen(packet.error) + 2, "%s\n", packet.error);
        n->host.print(n->host.ctx, text);
        free(text);
      }
      qa_receive_event_destroy(&packet);
      continue;
    }
    if (!qa_net_same_address(&packet.from, &n->remote, true)) {
      qa_receive_event_destroy(&packet);
      continue;
    }
    n->last_received = now;
    n->has_last_received = true;
    qa_q2_connectionless_message message;
    bool is_oob = qa_q2_read_out_of_band(packet.payload, packet.length,
        host_kex(n), &message);
    if (is_oob) {
      if (strcmp(message.command, "print") == 0) n->host.print(n->host.ctx, message.body);
      qa_q2_client_handshake_receive(&n->handshake, &packet.from, &message);
      if (n->handshake.kind == QA_Q2_HANDSHAKE_CONNECTING) n->state = QA_APP_PHASE_CONNECTING;
      if (n->handshake.kind == QA_Q2_HANDSHAKE_REJECTED) {
        n->state = QA_APP_PHASE_REJECTED;
        n->host.disconnected(n->host.ctx, n->handshake.reason ? n->handshake.reason : "rejected");
      }
      if (n->handshake.kind == QA_Q2_HANDSHAKE_CONNECTED && !n->channel_live) {
        qa_q2_connect_request *request = &n->handshake.request;
        if (n->host.downloads)
          qa_app_q2_download_receiver_set_http_server(
              (void *)n->host.downloads, n->handshake.download_server);
        qa_q2_channel_options channel_options = {0};
        channel_options.side_server = false;
        channel_options.protocol = request->protocol;
        channel_options.channel_new = request->channel_new;
        channel_options.qport = request->qport;
        channel_options.has_payload_bytes = true;
        channel_options.payload_bytes = request->payload_bytes;
        channel_options.has_max_datagram_bytes = true;
        int64_t max_datagram = n->transport.max_datagram_bytes(n->transport.ctx);
        channel_options.max_datagram_bytes = max_datagram < 0 ? 65507 : (double)max_datagram;
        qa_q2_channel_init(&n->channel, &channel_options);
        n->channel_live = true;
        n->state = QA_APP_PHASE_LOADING;
        qa_app_q2_client_network_command(n, "new");
      }
      qa_q2_connectionless_message_free(&message);
    } else if (n->channel_live) {
      qa_q2_channel_receive_result result;
      if (!qa_q2_channel_receive(&n->channel, packet.payload, packet.length, now, &result))
        qa_range_error("out of memory");
      if (result.kind == QA_Q2_CHANNEL_MESSAGE) {
        if (n->host.prediction_acknowledged)
          n->host.prediction_acknowledged(n->host.ctx, result.acknowledged, now);
        size_t count = 0;
        qa_q2_server_record *records = qa_app_q2_client_receiver_receive(
            n->receiver, result.bytes, result.bytes_length, now, &count);
        if (n->recording) {
          uint8_t **messages = xmalloc(count * sizeof *messages);
          size_t *lengths = xmalloc(count * sizeof *lengths);
          size_t message_count = 0;
          for (size_t i = 0; i < count; i++) {
            qa_q2_server_record *record = &records[i];
            if (record->event.kind == QA_Q2_SERVER_DATA) n->recording_waiting_full = true;
            if (record->event.kind == QA_Q2_SERVER_FRAME) {
              if (record->event.frame.has_valid && !record->event.frame.valid) continue;
              if (n->recording_waiting_full && record->event.frame.delta_frame > 0) continue;
              n->recording_waiting_full = false;
            }
            messages[message_count] = record->raw;
            lengths[message_count] = record->raw_length;
            message_count++;
          }
          if (n->recording_waiting_full)
            qa_app_q2_client_receiver_request_full_frame(n->receiver);
          if (message_count > 0) {
            size_t length = 0;
            uint8_t *joined = join_packets(messages, lengths, message_count, &length);
            n->recording_sink.append(n->recording_sink.ctx, "q2", joined, length);
            free(joined);
          }
          free(messages);
          free(lengths);
        }
        qa_q2_server_records_free(records, count);
      }
      qa_q2_channel_receive_result_free(&result);
    }
    qa_receive_event_destroy(&packet);
  }
  double timeout = n->options.has_timeout_milliseconds
      ? n->options.timeout_milliseconds : 120000;
  if (n->has_last_received && now - n->last_received > timeout) {
    qa_app_q2_client_receiver_close(n->receiver);
    n->state = QA_APP_PHASE_REJECTED;
    n->host.disconnected(n->host.ctx, "Connection timed out");
  }
  qa_app_q2_client_receiver_prepare_game_state(n->receiver);
  send_pending(n, now);
  return NULL;
}
void qa_app_q2_client_network_submit(qa_app_q2_client_network *n,
    const void *const *commands, size_t count, double now) {
  (void)now;
  if (client_phase(n) != QA_APP_PHASE_ACTIVE) return;
  if (count > 1)
    qa_range_error("A native Q2 connection carries one player; use independent connections for local seats");
  for (size_t i = 0; i < count; i++) {
    const void *command = n->host.command(n->host.ctx, commands[i]);
    if (n->pending_count == n->pending_cap) {
      n->pending_cap = n->pending_cap ? n->pending_cap * 2 : 8;
      n->pending = realloc(n->pending, n->pending_cap * sizeof *n->pending);
      if (!n->pending) qa_range_error("out of memory");
    }
    n->pending[n->pending_count++].command = *(const qa_q2_wire_usercmd *)command;
  }
}
void qa_app_q2_client_network_publish(qa_app_q2_client_network *n,
    const void *output, const void *const *events, size_t count, double now) {
  (void)n; (void)output; (void)events; (void)count; (void)now;
  qa_range_error("Remote Q2 client cannot publish authoritative server state");
}
void qa_app_q2_client_network_close(qa_app_q2_client_network *n) {
  n->recording = false;
  qa_app_network_phase phase = client_phase(n);
  qa_app_q2_client_receiver_close(n->receiver);
  if (n->transport.is_closed(n->transport.ctx)) return;
  if (n->channel_live && phase != QA_APP_PHASE_CLOSED) {
    qa_app_q2_client_network_command(n, "disconnect");
    qa_q2_channel_send(&n->channel, &n->transport, &n->remote, NULL, 0, now_performance());
  }
  n->state = QA_APP_PHASE_CLOSED;
  n->pending_count = 0;
  n->transport.close(n->transport.ctx);
}
void qa_app_q2_client_network_recording_seed(qa_app_q2_client_network *n,
    qa_app_q2_recording_seed *out) {
  qa_app_q2_client_receiver_seed(n->receiver, out);
}
void *qa_app_q2_client_network_recording_attach(qa_app_q2_client_network *n,
    qa_app_q2_recording_sink sink) {
  if (n->recording) qa_range_error("Q2 connection is already recording");
  if (client_phase(n) != QA_APP_PHASE_ACTIVE)
    qa_range_error("Recording requires an active Q2 connection");
  n->recording_sink = sink;
  n->recording = true;
  n->recording_waiting_full = true;
  qa_app_q2_client_receiver_request_full_frame(n->receiver);
  void *token = xmalloc(1);
  return token;
}
void qa_app_q2_client_network_recording_detach(qa_app_q2_client_network *n, void *token) {
  (void)token;
  n->recording = false;
}
static qa_app_network_phase cli_phase(void *ctx) {
  return qa_app_q2_client_network_phase(ctx);
}
static const void *cli_wire(void *ctx) {
  qa_app_q2_client_network *n = ctx;
  return &n->host.protocol;
}
static const void *const *cli_poll(void *ctx, double now, size_t *out_count) {
  return qa_app_q2_client_network_poll(ctx, now, out_count);
}
static void cli_submit(void *ctx, const void *const *commands, size_t count, double now) {
  qa_app_q2_client_network_submit(ctx, commands, count, now);
}
static void cli_publish(void *ctx, const void *output, const void *const *events,
    size_t count, double now) {
  qa_app_q2_client_network_publish(ctx, output, events, count, now);
}
static void cli_close(void *ctx) { qa_app_q2_client_network_close(ctx); }
qa_app_application_network qa_app_q2_client_network_as_network(qa_app_q2_client_network *network) {
  qa_app_application_network result = {0};
  result.ctx = network;
  result.role = QA_APP_ROLE_CLIENT;
  result.phase = cli_phase;
  result.wire = cli_wire;
  result.heartbeat = NULL;
  result.poll = cli_poll;
  result.submit = cli_submit;
  result.publish = cli_publish;
  result.close = cli_close;
  return result;
}
