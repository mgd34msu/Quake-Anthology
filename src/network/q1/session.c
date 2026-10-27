/* Port of src/network/q1/session.ts. GPL-2.0-or-later. */
#include "qa/network/q1/session.h"
#include "qa/foundation/support.h"
#include "qa/network/q1/codecs/codec.h"
#include "qa/network/q1/handshake.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void qa_write_client_string_command(qa_size_buf *s, const char *text) {
  qa_msg_write_byte(s, 4); qa_msg_write_string(s, text);
}
static void stuff(qa_size_buf *s, const char *text) { qa_msg_write_byte(s, 9); qa_msg_write_string(s, text); }
void qa_netquake_signon_init(qa_netquake_signon *s, qa_netquake_seat_identity seat) {
  s->stage = 0; s->seat = seat;
}
uint8_t *qa_netquake_signon_receive(qa_netquake_signon *s, double stage, size_t *out_len) {
  if (stage <= s->stage || stage > 4)
    qa_range_error("Invalid signon stage %g after %g", stage, s->stage);
  s->stage = stage;
  qa_size_buf buf; qa_size_buf_init(&buf, 8000, false);
  if (stage == 1) qa_write_client_string_command(&buf, "prespawn");
  else if (stage == 2) {
    char tmp[1024];
    snprintf(tmp, sizeof tmp, "name \"%s\"\n", s->seat.name);
    qa_write_client_string_command(&buf, tmp);
    int color = (int)s->seat.color;
    snprintf(tmp, sizeof tmp, "color %d %d\n", color >> 4, color & 15);
    qa_write_client_string_command(&buf, tmp);
    if (s->seat.has_extension_flags) {
      snprintf(tmp, sizeof tmp, "ex_flags %g\n", s->seat.extension_flags);
      qa_write_client_string_command(&buf, tmp);
    }
    snprintf(tmp, sizeof tmp, "spawn %s", s->seat.spawn_parameters);
    qa_write_client_string_command(&buf, tmp);
  } else if (stage == 3) qa_write_client_string_command(&buf, "begin");
  uint8_t *out = qa_size_buf_bytes(&buf, out_len);
  qa_size_buf_destroy(&buf); return out;
}
void qa_netquake_signon_first_entity(qa_netquake_signon *s) { if (s->stage == 3) s->stage = 4; }
bool qa_netquake_signon_active(const qa_netquake_signon *s) { return s->stage == 4; }

struct qa_quakeworld_signon_server {
  qa_qw_signon_host host; bool donor_wide, spawned;
  qa_download_source *download; double download_offset;
};
void qa_qw_command_result_destroy(qa_qw_command_result *r) {
  for (size_t i = 0; i < r->count; i++) free(r->messages[i]);
  free(r->messages); free(r->lens); free(r->text); memset(r, 0, sizeof *r);
}
void qa_quakeworld_signon_server_init(qa_quakeworld_signon_server *s, qa_qw_signon_host host, bool donor_wide) {
  memset(s, 0, sizeof *s); s->host = host; s->donor_wide = donor_wide;
}
void qa_quakeworld_signon_server_destroy(qa_quakeworld_signon_server *s) { qa_quakeworld_signon_server_close(s); }
static void push_msg(qa_qw_command_result *r, uint8_t *bytes, size_t len) {
  r->messages = realloc(r->messages, (r->count + 1) * sizeof *r->messages);
  r->lens = realloc(r->lens, (r->count + 1) * sizeof *r->lens);
  r->messages[r->count] = bytes; r->lens[r->count] = len; r->count++;
}
static void fresh_messages(qa_quakeworld_signon_server *s, qa_qw_command_result *r) {
  qa_quakeworld_message data; memset(&data, 0, sizeof data);
  s->host.server_data(s->host.ctx, &data);
  if (data.protocol.version == 29 && !s->donor_wide) {
    s->host.disconnect(s->host.ctx, "Client does not support donor QuakeWorld protocol 29");
    qa_quakeworld_message_destroy(&data);
    return;
  }
  s->spawned = false;
  qa_size_buf sb; qa_size_buf_init(&sb, 1450, false);
  qa_write_quakeworld_server_data(&sb, &data);
  qa_quakeworld_message_destroy(&data);
  size_t n = 0; uint8_t *b = qa_size_buf_bytes(&sb, &n);
  qa_size_buf_destroy(&sb);
  push_msg(r, b, n);
}
static void next_download(qa_quakeworld_signon_server *s, qa_qw_command_result *r) {
  qa_size_buf buf; qa_size_buf_init(&buf, 1450, false);
  qa_download_source *source = s->download;
  if (!source) qa_write_quakeworld_download(&buf, true, 0, NULL, 0);
  else {
    size_t blen = 0;
    uint8_t *bytes = source->read(source->ctx, s->download_offset, 768, &blen);
    s->download_offset += (double)blen;
    double percent = trunc(s->download_offset * 100 / (source->byte_length != 0 ? source->byte_length : 1));
    qa_write_quakeworld_download(&buf, false, percent, bytes, blen);
    free(bytes);
    if (s->download_offset == source->byte_length) { s->download = NULL; source->close(source->ctx); }
  }
  size_t n = 0; uint8_t *b = qa_size_buf_bytes(&buf, &n);
  qa_size_buf_destroy(&buf);
  push_msg(r, b, n);
}
static double parse_int_or(const char *text, const char *fallback) {
  const char *s = text ? text : fallback;
  char *end = NULL;
  long v = strtol(s, &end, 10);
  if (end == s) return NAN;
  return (double)v;
}
qa_qw_command_result qa_quakeworld_signon_server_command(qa_quakeworld_signon_server *s, const char *text,
                                                        qa_download_source *prepared_download, bool has_prepared) {
  qa_qw_command_result result; memset(&result, 0, sizeof result); result.handled = true;
  size_t argc = 0; char **args = qa_quakeworld_command_arguments(text, &argc);
  const char *op = argc > 0 ? args[0] : NULL;
  if (op && strcmp(op, "new") == 0) {
    if (!s->spawned) fresh_messages(s, &result);
  } else if (op && strcmp(op, "download") == 0) {
    qa_download_source *previous = s->download;
    s->download = has_prepared ? prepared_download : NULL;
    if (previous) previous->close(previous->ctx);
    const char *path = argc > 1 ? args[1] : "";
    /* downloadPath validation + host open; validation failure closes (TS catch). */
    size_t pc = 0;
    /* qa_contained_file_parts aborts on bad paths, mirroring the TS throw
     * reaching the crash handler after close() in the catch. */
    if (!has_prepared) {
      char **parts = qa_contained_file_parts(path, &pc);
      qa_contained_file_parts_free(parts, pc);
      s->download = s->host.open_download(s->host.ctx, path);
    }
    s->download_offset = 0;
    next_download(s, &result);
  } else if (op && strcmp(op, "nextdl") == 0) {
    if (s->download) next_download(s, &result);
  } else if (!op || (strcmp(op, "soundlist") != 0 && strcmp(op, "modellist") != 0 && strcmp(op, "prespawn") != 0
             && strcmp(op, "spawn") != 0 && strcmp(op, "begin") != 0)) {
    result.handled = false;
    size_t n = strlen(text) + 1; result.text = malloc(n); memcpy(result.text, text, n);
  } else {
    qa_quakeworld_message data; memset(&data, 0, sizeof data);
    s->host.server_data(s->host.ctx, &data);
    if (s->spawned) {
      qa_quakeworld_message_destroy(&data);
    } else if (parse_int_or(argc > 1 ? args[1] : NULL, "") != data.server_count) {
      qa_quakeworld_message_destroy(&data);
      fresh_messages(s, &result);
    } else {
      qa_message_reader empty; qa_message_reader_init(&empty, NULL, 0);
      qa_protocol_codec codec; qa_create_quakeworld_codec(data.protocol, &empty, &codec);
      double start = parse_int_or(argc > 2 ? args[2] : NULL, "0");
      qa_size_buf buf; qa_size_buf_init(&buf, 1450, false);
      if (strcmp(op, "soundlist") == 0 || strcmp(op, "modellist") == 0) {
        size_t name_count = 0;
        const char *const *names = strcmp(op, "soundlist") == 0
          ? s->host.sounds(s->host.ctx, &name_count) : s->host.models(s->host.ctx, &name_count);
        if (!isfinite(start) || start != trunc(start) || start < 0 || start >= codec.max_precache || start > (double)name_count) {
          qa_size_buf_destroy(&buf); qa_quakeworld_message_destroy(&data);
          fresh_messages(s, &result);
          goto done;
        }
        qa_msg_write_byte(&buf, strcmp(op, "soundlist") == 0 ? 46 : 45);
        codec.write_precache_count(&codec, &buf, start);
        double next = start;
        while (next < (double)name_count && buf.cursize < 725) {
          if (next + 1 >= codec.max_precache) { qa_size_buf_destroy(&buf); qa_range_error("Selected QW profile cannot represent precache"); }
          qa_msg_write_string(&buf, names[(size_t)next]);
          next++;
        }
        qa_msg_write_byte(&buf, 0);
        codec.write_precache_count(&codec, &buf, next < (double)name_count ? next : 0);
        size_t n = 0; uint8_t *b = qa_size_buf_bytes(&buf, &n); push_msg(&result, b, n);
      } else if (strcmp(op, "prespawn") == 0) {
        const uint8_t *const *buffers = NULL; const size_t *lens = NULL; size_t buffer_count = 0;
        s->host.signon_buffers(s->host.ctx, &buffers, &lens, &buffer_count);
        double index = (isfinite(start) && start == trunc(start) && start >= 0 && start < (double)buffer_count) ? start : 0;
        if (index == 0) {
          uint32_t checksum = (uint32_t)parse_int_or(argc > 3 ? args[3] : NULL, "0");
          if (!s->host.accepts_map_checksum(s->host.ctx, checksum)) {
            s->host.disconnect(s->host.ctx, "Map model file does not match");
            qa_size_buf_destroy(&buf); qa_quakeworld_message_destroy(&data);
            goto done;
          }
        }
        if ((size_t)index < buffer_count) qa_sz_write(&buf, buffers[(size_t)index], (long long)lens[(size_t)index], (long long)lens[(size_t)index]);
        char tmp[160];
        if (index + 1 >= (double)buffer_count) snprintf(tmp, sizeof tmp, "cmd spawn %g 0\n", data.server_count);
        else snprintf(tmp, sizeof tmp, "cmd prespawn %g %g\n", data.server_count, index + 1);
        stuff(&buf, tmp);
        size_t n = 0; uint8_t *b = qa_size_buf_bytes(&buf, &n); push_msg(&result, b, n);
      } else if (strcmp(op, "spawn") == 0) {
        if (!isfinite(start) || start != trunc(start) || start < 0 || start > 32) {
          qa_size_buf_destroy(&buf); qa_quakeworld_message_destroy(&data);
          fresh_messages(s, &result);
          goto done;
        }
        size_t *mlens = NULL, mcount = 0;
        uint8_t **messages = s->host.spawn(s->host.ctx, start, &mlens, &mcount);
        for (size_t i = 0; i < mcount; i++) push_msg(&result, messages[i], mlens[i]);
        free(messages); free(mlens);
        stuff(&buf, "skins\n");
        size_t n = 0; uint8_t *b = qa_size_buf_bytes(&buf, &n); push_msg(&result, b, n);
      } else {
        s->host.begin(s->host.ctx);
        s->spawned = true;
      }
      qa_size_buf_destroy(&buf);
      qa_quakeworld_message_destroy(&data);
    }
  }
done:
  for (size_t i = 0; i < argc; i++) free(args[i]);
  free(args);
  return result;
}
void qa_quakeworld_signon_server_close(qa_quakeworld_signon_server *s) {
  qa_download_source *source = s->download; s->download = NULL;
  if (source) source->close(source->ctx);
}
void qa_quakeworld_precache_client_init(qa_quakeworld_precache_client *c) { memset(c, 0, sizeof *c); }
void qa_quakeworld_precache_client_destroy(qa_quakeworld_precache_client *c) {
  for (size_t i = 0; i < c->model_count; i++) free(c->models[i]);
  for (size_t i = 0; i < c->sound_count; i++) free(c->sounds[i]);
  free(c->models); free(c->sounds); memset(c, 0, sizeof *c);
}
static char *dupstr(const char *str) { size_t n = strlen(str) + 1; char *p = malloc(n); memcpy(p, str, n); return p; }
static char *fmt_cmd(const char *verb, double server_count, double next, bool has_next, uint32_t checksum, bool has_checksum) {
  char tmp[160];
  if (has_checksum) snprintf(tmp, sizeof tmp, "%s %g %g %u", verb, server_count, next, checksum);
  else if (has_next) snprintf(tmp, sizeof tmp, "%s %g %g", verb, server_count, next);
  else snprintf(tmp, sizeof tmp, "%s %g", verb, server_count);
  return dupstr(tmp);
}
char **qa_quakeworld_precache_client_receive(qa_quakeworld_precache_client *c, const qa_quakeworld_message *message, size_t *out_count) {
  *out_count = 0;
  if (message->kind && strcmp(message->kind, "server-data") == 0) {
    c->server_count = message->server_count;
    for (size_t i = 0; i < c->model_count; i++) free(c->models[i]);
    for (size_t i = 0; i < c->sound_count; i++) free(c->sounds[i]);
    c->model_count = 0; c->sound_count = 0;
    char **out = malloc(sizeof *out);
    out[0] = fmt_cmd("soundlist", c->server_count, 0, true, 0, false);
    *out_count = 1; return out;
  }
  if (message->kind && (strcmp(message->kind, "sound-list") == 0 || strcmp(message->kind, "model-list") == 0)) {
    bool is_sound = strcmp(message->kind, "sound-list") == 0;
    char ***target = is_sound ? &c->sounds : &c->models;
    size_t *count = is_sound ? &c->sound_count : &c->model_count;
    size_t *cap = is_sound ? &c->sound_cap : &c->model_cap;
    if (message->first > (double)*count) qa_range_error("Precache list skipped entries");
    size_t first = (size_t)message->first;
    for (size_t i = first; i < *count; i++) free((*target)[i]);
    *count = first;
    if (*count + message->name_count > *cap) {
      size_t nc = *count + message->name_count;
      *target = realloc(*target, nc * sizeof **target); *cap = nc;
    }
    for (size_t i = 0; i < message->name_count; i++) (*target)[(*count)++] = dupstr(message->names[i]);
    if (message->next != 0) {
      char **out = malloc(sizeof *out);
      out[0] = fmt_cmd(is_sound ? "soundlist" : "modellist", c->server_count, message->next, true, 0, false);
      *out_count = 1; return out;
    }
  }
  return NULL;
}
char *qa_quakeworld_precache_client_sounds_ready(const qa_quakeworld_precache_client *c) {
  return fmt_cmd("modellist", c->server_count, 0, true, 0, false);
}
char *qa_quakeworld_precache_client_models_ready(const qa_quakeworld_precache_client *c, uint32_t checksum) {
  return fmt_cmd("prespawn", c->server_count, 0, true, checksum, true);
}
char *qa_quakeworld_precache_client_skins_ready(const qa_quakeworld_precache_client *c) {
  return fmt_cmd("begin", c->server_count, 0, false, 0, false);
}
qa_quakeworld_profile qa_choose_quakeworld_protocol(int mode, bool needs_wide) {
  if (mode == 29 || (mode == 0 && needs_wide)) {
    qa_quakeworld_profile p; p.kind = "q1-quakeworld-donor-wide"; p.version = 29; p.flags = 130; p.revision = 0; return p;
  }
  qa_quakeworld_profile p; p.kind = "q1-quakeworld"; p.version = 28; p.flags = 0; p.revision = 0; return p;
}
