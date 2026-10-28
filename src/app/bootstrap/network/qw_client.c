/* Port of src/app/bootstrap/network/qw-client.ts.
 * GPL-2.0-or-later. */
#include "qa/app/bootstrap/network/qw_client.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/content/catalog/index.h"
#include "qa/foundation/support.h"
#include "qa/network/q1/channels.h"
#include "qa/network/q1/commands.h"
#include "qa/network/q1/handshake.h"
#include "qa/network/q1/message.h"
#include "qa/network/q1/quakeworld.h"
#include "qa/network/q1/qw_recording.h"
#include "qa/network/q1/session.h"
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
static qa_quakeworld_profile qw_profile(void) { return qa_quakeworld_profile_make(28, 0); }
typedef struct command_entry { double sequence; qa_qw_user_command command; } command_entry;
typedef struct download_queue {
  char **paths; size_t path_count; size_t index;
  qa_app_qw_download_category category;
} download_queue;
struct qa_app_qw_client_network {
  qa_app_qw_client_network_options options;
  /* Landed recording state is caller-storage with unpublished size. */
  _Alignas(16) unsigned char recording_state[2048];
  bool recording; qa_app_qw_recording_sink recording_sink;
  bool recording_waiting;
  qa_qw_connect_client handshake; bool handshake_init;
  char *userinfo;
  bool skin_pass_pending, begun;
  qa_quakeworld_channel channel;
  qa_quakeworld_decoder decoder;
  qa_app_network_phase state;
  bool connected;
  bool has_last_received; double last_received;
  double last_sent;
  bool has_data; qa_quakeworld_message data;
  char **models; size_t model_count, model_cap;
  char **sounds; size_t sound_count, sound_cap;
  download_queue queue; bool has_queue;
  bool has_last_delta; double last_delta;
  qa_qw_user_command oldest, previous;
  command_entry *commands; size_t command_count, command_cap;
};
static qa_quakeworld_recording_state *recording_state(qa_app_qw_client_network *n) {
  return (qa_quakeworld_recording_state *)n->recording_state;
}
static void names_clear(char ***names, size_t *count) {
  for (size_t i = 0; i < *count; i++) free((*names)[i]);
  *count = 0;
}
static void names_push(char ***names, size_t *count, size_t *cap, const char *value) {
  if (*count == *cap) {
    *cap = *cap ? *cap * 2 : 16;
    *names = realloc(*names, *cap * sizeof **names);
    if (!*names) qa_range_error("out of memory");
  }
  (*names)[(*count)++] = copy_text(value);
}
void qa_app_qw_client_network_command(qa_app_qw_client_network *n, const char *text) {
  if (!n->connected) qa_range_error("QuakeWorld client is not connected");
  qa_size_buf buffer;
  if (!qa_size_buf_init(&buffer, 1450, false)) qa_range_error("out of memory");
  qa_write_client_string_command(&buffer, text);
  qa_quakeworld_channel_queue_reliable(&n->channel, buffer.data, (size_t)buffer.cursize);
  qa_size_buf_destroy(&buffer);
}
static void queue_free(download_queue *queue) {
  for (size_t i = 0; i < queue->path_count; i++) free(queue->paths[i]);
  free(queue->paths);
  memset(queue, 0, sizeof *queue);
}
void qa_app_qw_client_network_resume_downloads(qa_app_qw_client_network *n) {
  if (!n->has_queue || !n->has_data) return;
  download_queue *queue = &n->queue;
  qa_app_qw_download_category category = queue->category;
  while (queue->index < queue->path_count) {
    const char *path = queue->paths[queue->index++];
    if (!path || path[0] == '*') continue;
    if (n->options.host.has_downloads) {
      qa_app_qw_download_request_result result = n->options.host.downloads.request(
          n->options.host.downloads.ctx, path, category);
      if (result == QA_APP_QW_DL_WAITING) return;
    }
  }
  n->has_queue = false;
  queue_free(queue);
  char line[160];
  if (category == QA_APP_QW_DL_SOUND) {
    snprintf(line, sizeof line, "modellist %.17g 0", n->data.server_count);
    qa_app_qw_client_network_command(n, line);
  } else if (category == QA_APP_QW_DL_MODEL) {
    double checksum = n->options.host.game_state(n->options.host.ctx, &n->data,
        (const char *const *)n->models, n->model_count,
        (const char *const *)n->sounds, n->sound_count);
    snprintf(line, sizeof line, "prespawn %.17g 0 %lld", n->data.server_count,
        (long long)(int32_t)checksum);
    qa_app_qw_client_network_command(n, line);
  } else {
    if (n->skin_pass_pending) {
      qa_app_qw_client_network_refresh_skins(n);
      return;
    }
    if (n->options.host.has_skins) {
      n->options.host.skins.loading(n->options.host.skins.ctx, false);
      n->options.host.skins.prepare(n->options.host.skins.ctx);
    }
    if (!n->begun && n->state != QA_APP_PHASE_ACTIVE) {
      snprintf(line, sizeof line, "begin %.17g", n->data.server_count);
      qa_app_qw_client_network_command(n, line);
      n->begun = true;
    }
  }
}
void qa_app_qw_client_network_refresh_skins(qa_app_qw_client_network *n) {
  n->skin_pass_pending = true;
  if (n->has_queue || !n->has_data) return;
  n->skin_pass_pending = false;
  if (n->options.host.has_skins)
    n->options.host.skins.loading(n->options.host.skins.ctx, true);
  size_t count = 0;
  const char *const *names = n->options.host.has_skins
      ? n->options.host.skins.names(n->options.host.skins.ctx, &count) : NULL;
  queue_free(&n->queue);
  n->queue.paths = xmalloc(count * sizeof *n->queue.paths);
  n->queue.path_count = count;
  for (size_t i = 0; i < count; i++) n->queue.paths[i] = copy_text(names[i]);
  n->queue.category = QA_APP_QW_DL_SKIN;
  n->queue.index = 0;
  n->has_queue = true;
  qa_app_qw_client_network_resume_downloads(n);
}
typedef struct collect_ctx { qa_app_qw_client_network *n; } collect_ctx;
static void sync_userinfo(qa_app_qw_client_network *n) {
  const char *text = n->options.userinfo(n->options.userinfo_ctx);
  for (const char *p = text; *p; p++)
    if (*p == '"' || *p == '\n' || *p == '\r') qa_range_error("Invalid QW userinfo");
  if (strcmp(text, n->userinfo) == 0) return;
  if (n->connected) {
    qa_qw_info previous, current;
    qa_qw_info_init(&previous);
    qa_qw_info_init(&current);
    qa_quakeworld_info_parse(n->userinfo, &previous);
    qa_quakeworld_info_parse(text, &current);
    for (size_t i = 0; i < current.count; i++) {
      const char *key = current.entries[i].key;
      const char *value = current.entries[i].value;
      const char *old = qa_qw_info_get(&previous, key);
      if (!old || strcmp(old, value) != 0) {
        char *line = xmalloc(strlen(key) + strlen(value) + 16);
        snprintf(line, strlen(key) + strlen(value) + 16, "setinfo \"%s\" \"%s\"", key, value);
        qa_app_qw_client_network_command(n, line);
        free(line);
      }
    }
    for (size_t i = 0; i < previous.count; i++) {
      const char *key = previous.entries[i].key;
      if (!qa_qw_info_get(&current, key)) {
        char *line = xmalloc(strlen(key) + 16);
        snprintf(line, strlen(key) + 16, "setinfo \"%s\" \"%s\"", key, "");
        qa_app_qw_client_network_command(n, line);
        free(line);
      }
    }
    qa_qw_info_destroy(&previous);
    qa_qw_info_destroy(&current);
  } else {
    qa_qw_connect_client handshake;
    if (!qa_qw_connect_client_init(&handshake, n->options.qport, text)) qa_range_error("out of memory");
    handshake.kind = n->handshake.kind;
    handshake.sent_at = n->handshake.sent_at;
    handshake.challenge = n->handshake.challenge;
    qa_qw_connect_client_destroy(&n->handshake);
    n->handshake = handshake;
  }
  free(n->userinfo);
  n->userinfo = copy_text(text);
}
static bool all_digits(const char *value) {
  if (!value[0]) return false;
  for (const char *p = value; *p; p++)
    if (*p < '0' || *p > '9') return false;
  return true;
}
static bool records(qa_app_qw_client_network *n, qa_quakeworld_message *messages,
    size_t count, double now) {
  for (size_t i = 0; i < count; i++) {
    qa_quakeworld_message *message = &messages[i];
    const char *kind = message->kind ? message->kind : "";
    if (strcmp(kind, "server-data") == 0) {
      qa_quakeworld_profile profile = message->protocol;
      if (!profile.kind || strcmp(profile.kind, "q1-quakeworld") != 0 || message->player_slot >= 32)
        qa_range_error("Remote QW requires native protocol 28 and a valid player slot");
      qa_remote_content_selection selection;
      if (!qa_remote_content_selection_make("q1-quakeworld",
              message->game_directory ? message->game_directory : "", &selection))
        qa_range_error("out of memory");
      qa_remote_content_selection_free(&selection);
      if (n->options.host.has_downloads)
        n->options.host.downloads.close(n->options.host.downloads.ctx);
      n->options.host.server_data(n->options.host.ctx, message);
      if (n->has_data) qa_quakeworld_message_destroy(&n->data);
      n->data = *message;
      memset(message, 0, sizeof *message);
      message->kind = "server-data";
      n->has_data = true;
      names_clear(&n->models, &n->model_count);
      names_clear(&n->sounds, &n->sound_count);
      n->skin_pass_pending = false;
      n->begun = false;
      n->has_last_delta = false;
      n->command_count = 0;
      memset(&n->oldest, 0, sizeof n->oldest);
      memset(&n->previous, 0, sizeof n->previous);
      n->state = QA_APP_PHASE_LOADING;
      queue_free(&n->queue);
      n->has_queue = false;
      char line[96];
      snprintf(line, sizeof line, "soundlist %.17g 0", n->data.server_count);
      qa_app_qw_client_network_command(n, line);
    } else if (strcmp(kind, "sound-list") == 0 || strcmp(kind, "model-list") == 0) {
      if (!n->has_data) qa_range_error("QW list before serverdata");
      bool sound = strcmp(kind, "sound-list") == 0;
      char ***list = sound ? &n->sounds : &n->models;
      size_t *list_count = sound ? &n->sound_count : &n->model_count;
      size_t *list_cap = sound ? &n->sound_cap : &n->model_cap;
      if (message->first != (double)*list_count) qa_range_error("Non-contiguous QW precache list");
      for (size_t j = 0; j < message->name_count; j++)
        names_push(list, list_count, list_cap, message->names[j]);
      if (message->next != 0) {
        char line[96];
        snprintf(line, sizeof line, "%s %.17g %.17g", sound ? "soundlist" : "modellist",
            n->data.server_count, message->next);
        qa_app_qw_client_network_command(n, line);
      } else {
        queue_free(&n->queue);
        n->queue.paths = xmalloc(*list_count * sizeof *n->queue.paths);
        n->queue.path_count = *list_count;
        for (size_t j = 0; j < *list_count; j++) {
          if (sound) {
            n->queue.paths[j] = xmalloc(strlen((*list)[j]) + 7);
            snprintf(n->queue.paths[j], strlen((*list)[j]) + 7, "sound/%s", (*list)[j]);
          } else n->queue.paths[j] = copy_text((*list)[j]);
        }
        n->queue.category = sound ? QA_APP_QW_DL_SOUND : QA_APP_QW_DL_MODEL;
        n->queue.index = 0;
        n->has_queue = true;
        qa_app_qw_client_network_resume_downloads(n);
      }
    } else if (strcmp(kind, "download") == 0) {
      if (!n->options.host.has_downloads) qa_range_error("Unsolicited QW download");
      qa_app_qw_download_receive_result result = n->options.host.downloads.receive(
          n->options.host.downloads.ctx, message);
      if (result != QA_APP_QW_DL_RX_WAITING) qa_app_qw_client_network_resume_downloads(n);
    } else if (strcmp(kind, "stufftext") == 0) {
      char *text = copy_text(message->text ? message->text : "");
      char *cursor = text;
      while (cursor) {
        char *newline = strchr(cursor, '\n');
        if (newline) *newline = '\0';
        size_t arg_count = 0;
        char **args = qa_quakeworld_command_arguments(cursor, &arg_count);
        if (!args) qa_range_error("out of memory");
        const char *name = arg_count ? args[0] : NULL;
        if (name && strcmp(name, "cmd") == 0 && arg_count > 1
            && (strcmp(args[1], "prespawn") == 0 || strcmp(args[1], "spawn") == 0)) {
          bool numeric = true;
          for (size_t j = 2; j < arg_count; j++)
            if (!all_digits(args[j])) numeric = false;
          if (numeric && arg_count > 2 && n->has_data
              && strtod(args[2], NULL) == n->data.server_count) {
            size_t total = 1;
            for (size_t j = 1; j < arg_count; j++) total += strlen(args[j]) + 1;
            char *line = xmalloc(total);
            size_t used = 0;
            for (size_t j = 1; j < arg_count; j++) {
              if (j > 1) line[used++] = ' ';
              size_t len = strlen(args[j]);
              memcpy(line + used, args[j], len);
              used += len;
            }
            line[used] = '\0';
            qa_app_qw_client_network_command(n, line);
            free(line);
          }
        } else if (name && strcmp(name, "skins") == 0) n->skin_pass_pending = true;
        else if (name && strcmp(name, "reconnect") == 0) {
          n->state = QA_APP_PHASE_LOADING;
          qa_app_qw_client_network_command(n, "new");
        } else if (name && strcmp(name, "fullserverinfo") != 0) {
          char *line = xmalloc(strlen(cursor) + 32);
          snprintf(line, strlen(cursor) + 32, "Unhandled QW server command: %s\n", cursor);
          n->options.host.print(n->options.host.ctx, line);
          free(line);
        }
        for (size_t j = 0; j < arg_count; j++) free(args[j]);
        free(args);
        cursor = newline ? newline + 1 : NULL;
        if (cursor && !*cursor && !newline) cursor = NULL;
        if (cursor && !*cursor) cursor = NULL;
      }
      free(text);
    } else if (strcmp(kind, "packet-entities") == 0) {
      n->last_delta = message->sequence;
      n->has_last_delta = true;
      n->state = QA_APP_PHASE_ACTIVE;
    } else if (strcmp(kind, "invalid-delta") == 0) n->has_last_delta = false;
    else if (strcmp(kind, "disconnect") == 0) {
      n->state = QA_APP_PHASE_CLOSED;
      n->options.host.disconnected(n->options.host.ctx, "Server disconnected");
    }
  }
  const void **borrowed = xmalloc(count * sizeof *borrowed);
  for (size_t i = 0; i < count; i++) borrowed[i] = &messages[i];
  n->options.host.receive(n->options.host.ctx, borrowed, count, now);
  free(borrowed);
  if (n->skin_pass_pending && !n->has_queue) qa_app_qw_client_network_refresh_skins(n);
  return n->state == QA_APP_PHASE_CLOSED;
}
static void transmit(qa_app_qw_client_network *n, const uint8_t *bytes, size_t length, double now) {
  n->last_sent = now;
  size_t packet_length = 0;
  uint8_t *packet = qa_quakeworld_channel_transmit(&n->channel, bytes, length, now, false, &packet_length);
  if (!packet) qa_range_error("out of memory");
  n->options.transport->send(n->options.transport->ctx, &n->options.remote, packet, packet_length);
  free(packet);
}
qa_app_qw_client_network *qa_app_qw_client_network_create(qa_app_qw_client_network_options options) {
  qa_app_qw_client_network *n = calloc(1, sizeof *n);
  if (!n) return NULL;
  n->options = options;
  n->userinfo = copy_text("");
  n->state = QA_APP_PHASE_CONNECTING;
  n->last_sent = -1.0 / 0.0;
  qa_quakeworld_recording_state_init(recording_state(n));
  if (!qa_qw_connect_client_init(&n->handshake, options.qport, "")) { free(n->userinfo); free(n); return NULL; }
  n->handshake_init = true;
  if (!qa_quakeworld_channel_init(&n->channel, true, options.qport, 0, 0)) {
    qa_qw_connect_client_destroy(&n->handshake);
    free(n->userinfo);
    free(n);
    return NULL;
  }
  qa_quakeworld_decoder_init(&n->decoder, qw_profile());
  return n;
}
void qa_app_qw_client_network_destroy(qa_app_qw_client_network *network) {
  if (!network) return;
  qa_quakeworld_recording_state_destroy(recording_state(network));
  if (network->handshake_init) qa_qw_connect_client_destroy(&network->handshake);
  qa_quakeworld_channel_destroy(&network->channel);
  qa_quakeworld_decoder_destroy(&network->decoder);
  if (network->has_data) qa_quakeworld_message_destroy(&network->data);
  names_clear(&network->models, &network->model_count);
  free(network->models);
  names_clear(&network->sounds, &network->sound_count);
  free(network->sounds);
  queue_free(&network->queue);
  free(network->commands);
  free(network->userinfo);
  free(network);
}
qa_app_network_phase qa_app_qw_client_network_phase(const qa_app_qw_client_network *n) {
  return n->state;
}
const void *const *qa_app_qw_client_network_poll(qa_app_qw_client_network *n,
    double now, size_t *out_count) {
  *out_count = 0;
  if (n->state == QA_APP_PHASE_CLOSED || n->state == QA_APP_PHASE_REJECTED) return NULL;
  if (!n->has_last_received) {
    n->last_received = now;
    n->has_last_received = true;
  }
  sync_userinfo(n);
  if (!n->connected) {
    size_t length = 0;
    uint8_t *bytes = qa_qw_connect_client_next(&n->handshake, now, &length);
    if (bytes) {
      n->options.transport->send(n->options.transport->ctx, &n->options.remote, bytes, length);
      free(bytes);
    }
  }
  for (;;) {
    qa_receive_event packet;
    if (!n->options.transport->poll(n->options.transport->ctx, &packet)) break;
    if (packet.kind != QA_RECV_PACKET
        || !qa_net_same_address(&packet.from, &n->options.remote, true)) {
      qa_receive_event_destroy(&packet);
      continue;
    }
    bool oob = packet.length >= 4 && packet.payload[0] == 255 && packet.payload[1] == 255
        && packet.payload[2] == 255 && packet.payload[3] == 255;
    if (!n->connected) {
      if (!oob) { qa_receive_event_destroy(&packet); continue; }
      qa_qw_connect_client_receive(&n->handshake, packet.payload, packet.length);
      if (n->handshake.kind == 3) {
        n->state = QA_APP_PHASE_REJECTED;
        n->options.host.disconnected(n->options.host.ctx,
            n->handshake.reason ? n->handshake.reason : "rejected");
        qa_receive_event_destroy(&packet);
        return NULL;
      }
      if (n->handshake.kind == 2) {
        n->connected = true;
        n->state = QA_APP_PHASE_LOADING;
        n->last_received = now;
        transmit(n, NULL, 0, now);
        qa_app_qw_client_network_command(n, "new");
      }
      qa_receive_event_destroy(&packet);
      continue;
    }
    if (oob) { qa_receive_event_destroy(&packet); continue; }
    qa_quakeworld_delivery delivery = {0};
    int received = qa_quakeworld_channel_receive(&n->channel, packet.payload,
        packet.length, now, &delivery);
    if (!received) { qa_receive_event_destroy(&packet); continue; }
    n->last_received = now;
    n->has_last_received = true;
    if (n->options.host.prediction_acknowledged)
      n->options.host.prediction_acknowledged(n->options.host.ctx, delivery.acknowledged, now);
    size_t count = 0;
    qa_quakeworld_message *messages = qa_quakeworld_decoder_decode(&n->decoder,
        delivery.payload, delivery.length, delivery.sequence, &count);
    qa_quakeworld_recording_state_observe(recording_state(n), messages, count);
    bool closed = records(n, messages, count, now);
    for (size_t i = 0; i < count; i++)
      if (messages[i].kind && strcmp(messages[i].kind, "packet-entities") == 0
          && !messages[i].has_delta_sequence)
        n->recording_waiting = false;
    if (!n->recording_waiting && n->recording) {
      qa_quakeworld_demo_record record = {0};
      record.kind = "packet";
      record.seconds = now / 1000;
      record.message = packet.payload;
      record.length = packet.length;
      n->recording_sink.append(n->recording_sink.ctx, &record);
    }
    for (size_t i = 0; i < count; i++) qa_quakeworld_message_destroy(&messages[i]);
    free(messages);
    free(delivery.payload);
    qa_receive_event_destroy(&packet);
    if (closed) return NULL;
  }
  double timeout = n->options.has_timeout_milliseconds ? n->options.timeout_milliseconds : 120000;
  if (now - n->last_received > timeout) {
    n->state = QA_APP_PHASE_REJECTED;
    n->options.host.disconnected(n->options.host.ctx, "Connection timed out");
    return NULL;
  }
  if (n->connected && qa_quakeworld_channel_can_packet(&n->channel, now)
      && (n->state != QA_APP_PHASE_ACTIVE || now - n->last_sent >= 1000))
    transmit(n, NULL, 0, now);
  return NULL;
}
static const qa_qw_user_command *command_at(qa_app_qw_client_network *n, double sequence) {
  for (size_t i = 0; i < n->command_count; i++)
    if (n->commands[i].sequence == sequence) return &n->commands[i].command;
  return NULL;
}
void qa_app_qw_client_network_submit(qa_app_qw_client_network *n,
    const void *const *commands, size_t count, double now) {
  if (n->state != QA_APP_PHASE_ACTIVE) return;
  if (count > 1) qa_range_error("A QW connection carries one player");
  for (size_t i = 0; i < count; i++) {
    qa_qw_user_command command = n->options.host.command(n->options.host.ctx, commands[i]);
    double sequence = qa_quakeworld_channel_outgoing_sequence(&n->channel);
    qa_vec3 teleport = {0, 0, 0};
    if (n->options.host.take_spectator_teleport
        && n->options.host.take_spectator_teleport(n->options.host.ctx, &teleport)) {
      qa_size_buf reliable;
      if (!qa_size_buf_init(&reliable, 7, false)) qa_range_error("out of memory");
      qa_msg_write_byte(&reliable, 6);
      qa_msg_write_coord(&reliable, teleport.x);
      qa_msg_write_coord(&reliable, teleport.y);
      qa_msg_write_coord(&reliable, teleport.z);
      qa_quakeworld_channel_queue_reliable(&n->channel, reliable.data, (size_t)reliable.cursize);
      qa_size_buf_destroy(&reliable);
    }
    if (n->recording) {
      qa_quakeworld_demo_record record = {0};
      record.kind = "command";
      record.seconds = now / 1000;
      record.command = command;
      record.view_angles = command.angles;
      n->recording_sink.append(n->recording_sink.ctx, &record);
    }
    qa_size_buf buffer;
    if (!qa_size_buf_init(&buffer, 256, false)) qa_range_error("out of memory");
    const qa_qw_user_command *oldest = command_at(n, sequence - 2);
    const qa_qw_user_command *previous = command_at(n, sequence - 1);
    qa_qw_user_command zero = {0};
    n->oldest = oldest ? *oldest : zero;
    n->previous = previous ? *previous : zero;
    qa_quakeworld_move bundle = {n->oldest, n->previous, command, 0};
    qa_write_quakeworld_move(&buffer, qw_profile(), &bundle, sequence);
    bool has_delta = !n->recording_waiting && n->has_last_delta
        && sequence - n->last_delta < 63;
    if (has_delta) {
      qa_msg_write_byte(&buffer, 5);
      qa_msg_write_byte(&buffer, (double)((int64_t)n->last_delta & 255));
    }
    qa_quakeworld_decoder_record_delta_request(&n->decoder, sequence,
        has_delta, has_delta ? n->last_delta : 0);
    if (n->command_count == n->command_cap) {
      n->command_cap = n->command_cap ? n->command_cap * 2 : 16;
      n->commands = realloc(n->commands, n->command_cap * sizeof *n->commands);
      if (!n->commands) qa_range_error("out of memory");
    }
    n->commands[n->command_count].sequence = sequence;
    n->commands[n->command_count].command = command;
    n->command_count++;
    if (n->options.host.prediction_sent)
      n->options.host.prediction_sent(n->options.host.ctx, sequence, &command, now);
    size_t kept = 0;
    for (size_t j = 0; j < n->command_count; j++)
      if (n->commands[j].sequence > sequence - 64) n->commands[kept++] = n->commands[j];
    n->command_count = kept;
    transmit(n, buffer.data, (size_t)buffer.cursize, now);
    qa_size_buf_destroy(&buffer);
  }
}
void qa_app_qw_client_network_publish(qa_app_qw_client_network *n,
    const void *output, const void *const *events, size_t count, double now) {
  (void)n; (void)output; (void)events; (void)count; (void)now;
  qa_range_error("Remote client cannot publish authoritative state");
}
void qa_app_qw_client_network_close(qa_app_qw_client_network *n) {
  if (n->options.transport->is_closed(n->options.transport->ctx)) return;
  if (n->connected && n->state != QA_APP_PHASE_CLOSED) {
    qa_app_qw_client_network_command(n, "drop");
    transmit(n, NULL, 0, 0);
  }
  if (n->options.host.has_downloads) n->options.host.downloads.close(n->options.host.downloads.ctx);
  n->state = QA_APP_PHASE_CLOSED;
  n->options.transport->close(n->options.transport->ctx);
}
qa_quakeworld_demo_record *qa_app_qw_client_network_recording_seed(
    qa_app_qw_client_network *n, size_t *out_count) {
  if (n->state != QA_APP_PHASE_ACTIVE || !n->has_data)
    qa_range_error("Recording requires an active QuakeWorld connection");
  return qa_quakeworld_recording_state_seed(recording_state(n), &n->data,
      (const char *const *)n->models, n->model_count,
      (const char *const *)n->sounds, n->sound_count,
      (n->has_last_received ? n->last_received : 0) / 1000,
      qa_quakeworld_channel_outgoing_sequence(&n->channel),
      qa_quakeworld_channel_incoming_sequence(&n->channel), out_count);
}
void *qa_app_qw_client_network_recording_attach(qa_app_qw_client_network *n,
    qa_app_qw_recording_sink sink) {
  if (n->recording || n->state == QA_APP_PHASE_CLOSED || n->state == QA_APP_PHASE_REJECTED)
    qa_range_error("QuakeWorld recording cannot attach");
  n->recording_waiting = n->state == QA_APP_PHASE_ACTIVE;
  n->has_last_delta = false;
  n->recording_sink = sink;
  n->recording = true;
  return xmalloc(1);
}
void qa_app_qw_client_network_recording_detach(qa_app_qw_client_network *n, void *token) {
  (void)token;
  n->recording = false;
}
static qa_app_network_phase net_phase(void *ctx) { return qa_app_qw_client_network_phase(ctx); }
static const void *net_wire(void *ctx) {
  (void)ctx;
  static qa_quakeworld_profile profile;
  profile = qw_profile();
  return &profile;
}
static const void *const *net_poll(void *ctx, double now, size_t *out_count) {
  return qa_app_qw_client_network_poll(ctx, now, out_count);
}
static void net_submit(void *ctx, const void *const *commands, size_t count, double now) {
  qa_app_qw_client_network_submit(ctx, commands, count, now);
}
static void net_publish(void *ctx, const void *output, const void *const *events,
    size_t count, double now) {
  qa_app_qw_client_network_publish(ctx, output, events, count, now);
}
static void net_close(void *ctx) { qa_app_qw_client_network_close(ctx); }
qa_app_application_network qa_app_qw_client_network_as_network(qa_app_qw_client_network *network) {
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
