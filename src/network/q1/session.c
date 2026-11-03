/* Port of src/network/q1/session.ts. GPL-2.0-or-later. */
#include "qa/network/q1/session.h"
#include "qa/network/common/js_number.h"
#include "qa/foundation/support.h"
#include "qa/network/q1/codecs/codec.h"
#include "qa/network/q1/handshake.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/js.h"
void qa_write_client_string_command(qa_size_buf *s, const char *text) {
  qa_msg_write_byte(s, 4); qa_msg_write_string(s, text);
}
static void stuff(qa_size_buf *s, const char *text) { qa_msg_write_byte(s, 9); qa_msg_write_string(s, text); }
void qa_netquake_signon_init(qa_netquake_signon *s, qa_netquake_seat_identity seat) {
  s->stage = 0; s->seat = seat;
}
uint8_t *qa_netquake_signon_receive(qa_netquake_signon *s, double stage, size_t *out_len) {
  if (stage <= s->stage || stage > 4)
    qa_raise("Error", 0, 0, "Invalid signon stage %s after %s", QA_JS_NUMBER(stage), QA_JS_NUMBER(s->stage));
  s->stage = stage;
  qa_size_buf buf; qa_size_buf_init(&buf, 8000, false);
  if (stage == 1) qa_write_client_string_command(&buf, "prespawn");
  else if (stage == 2) {
    char tmp[1024];
    snprintf(tmp, sizeof tmp, "name \"%s\"\n", s->seat.name);
    qa_write_client_string_command(&buf, tmp);
    int color = qa_js_to_int32(s->seat.color); /* color >> 4, color & 15 */
    snprintf(tmp, sizeof tmp, "color %d %d\n", color >> 4, color & 15);
    qa_write_client_string_command(&buf, tmp);
    if (s->seat.has_extension_flags) {
      snprintf(tmp, sizeof tmp, "ex_flags %s\n", QA_JS_NUMBER(s->seat.extension_flags));
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
qa_quakeworld_signon_server *qa_quakeworld_signon_server_create(qa_qw_signon_host host, bool donor_wide) {
  qa_quakeworld_signon_server *s = malloc(sizeof *s);
  if (s) qa_quakeworld_signon_server_init(s, host, donor_wide);
  return s;
}
void qa_quakeworld_signon_server_free(qa_quakeworld_signon_server *s) {
  if (!s) return;
  qa_quakeworld_signon_server_destroy(s);
  free(s);
}
static void push_msg(qa_qw_command_result *r, uint8_t *bytes, size_t len) {
  r->messages = realloc(r->messages, (r->count + 1) * sizeof *r->messages);
  r->lens = realloc(r->lens, (r->count + 1) * sizeof *r->lens);
  r->messages[r->count] = bytes; r->lens[r->count] = len; r->count++;
}
/* Owned state of one command(), released by a caught raise as TS leaves it to GC. */
typedef struct command_scope {
  char **args; size_t argc;
  qa_qw_command_result result;
  qa_quakeworld_message data; bool has_data;
  qa_size_buf buf; bool has_buf;
} command_scope;
static void command_scope_free(void *arg) {
  command_scope *c = arg;
  for (size_t i = 0; i < c->argc; i++) free(c->args[i]);
  free(c->args); c->args = NULL; c->argc = 0;
  qa_qw_command_result_destroy(&c->result);
  if (c->has_data) qa_quakeworld_message_destroy(&c->data);
  c->has_data = false;
  if (c->has_buf) qa_size_buf_destroy(&c->buf);
  c->has_buf = false;
}
static void destroy_message_slot(void *arg) { qa_quakeworld_message_destroy(arg); }
static void destroy_buf_slot(void *arg) { qa_size_buf_destroy(arg); }
static void fresh_messages(qa_quakeworld_signon_server *s, qa_qw_command_result *r) {
  qa_quakeworld_message data; memset(&data, 0, sizeof data);
  s->host.server_data(s->host.ctx, &data);
  if (data.protocol.version == 29 && !s->donor_wide) {
    qa_quakeworld_message_destroy(&data);
    s->host.disconnect(s->host.ctx, "Client does not support donor QuakeWorld protocol 29");
    return;
  }
  s->spawned = false;
  qa_size_buf sb; qa_size_buf_init(&sb, 1450, false);
  qa_cleanup guard_sb, guard_data;
  qa_cleanup_push(&guard_data, destroy_message_slot, &data);
  qa_cleanup_push(&guard_sb, destroy_buf_slot, &sb);
  qa_write_quakeworld_server_data(&sb, &data);
  qa_cleanup_pop(&guard_sb);
  qa_cleanup_pop(&guard_data);
  qa_quakeworld_message_destroy(&data);
  size_t n = 0; uint8_t *b = qa_size_buf_bytes(&sb, &n);
  qa_size_buf_destroy(&sb);
  push_msg(r, b, n);
}
typedef struct read_scope { qa_download_source *source; double offset; uint8_t *bytes; size_t length; } read_scope;
static void read_body(void *ctx) {
  read_scope *scope = ctx;
  scope->bytes = scope->source->read(scope->source->ctx, scope->offset, 768, &scope->length);
}
static void close_body(void *ctx) {
  qa_download_source *source = ctx;
  source->close(source->ctx);
}
typedef struct download_write { qa_size_buf *buf; double percent; const uint8_t *bytes; size_t length; } download_write;
static void download_write_body(void *ctx) {
  download_write *w = ctx;
  qa_write_quakeworld_download(w->buf, false, w->percent, w->bytes, w->length);
}
static void next_download(qa_quakeworld_signon_server *s, qa_qw_command_result *r) {
  qa_size_buf buf; qa_size_buf_init(&buf, 1450, false);
  qa_cleanup guard;
  qa_cleanup_push(&guard, destroy_buf_slot, &buf);
  qa_download_source *source = s->download;
  if (!source) qa_write_quakeworld_download(&buf, true, 0, NULL, 0);
  else {
    /* TS: catch (error) { this.close(); throw error; } around read, write and the final close. */
    qa_caught_error caught;
    read_scope scope = { source, s->download_offset, NULL, 0 };
    if (!qa_try(read_body, &scope, &caught)) { qa_quakeworld_signon_server_close(s); qa_rethrow(&caught); }
    s->download_offset += (double)scope.length;
    download_write w = { &buf, trunc(s->download_offset * 100 / (source->byte_length != 0 ? source->byte_length : 1)), scope.bytes, scope.length };
    bool wrote = qa_try(download_write_body, &w, &caught);
    free(scope.bytes);
    if (!wrote) { qa_quakeworld_signon_server_close(s); qa_rethrow(&caught); }
    if (s->download_offset == source->byte_length) {
      s->download = NULL;
      if (!qa_try(close_body, source, &caught)) { qa_quakeworld_signon_server_close(s); qa_rethrow(&caught); }
    }
  }
  qa_cleanup_pop(&guard);
  size_t n = 0; uint8_t *b = qa_size_buf_bytes(&buf, &n);
  qa_size_buf_destroy(&buf);
  push_msg(r, b, n);
}
/* Number.parseInt(text, 10): whitespace, optional sign, leading decimal digits; NaN when none. */
static double js_parse_int(const char *text) { return qa_js_parse_int(text, 10); }
typedef struct open_scope { qa_quakeworld_signon_server *s; const char *path; qa_download_source *prepared; bool has_prepared; } open_scope;
static void open_body(void *ctx) {
  open_scope *o = ctx;
  size_t pc = 0;
  char **parts = qa_download_path(o->path, &pc);
  qa_contained_file_parts_free(parts, pc);
  o->s->download = o->has_prepared ? o->prepared : o->s->host.open_download(o->s->host.ctx, o->path);
}
static void command_body(qa_quakeworld_signon_server *s, const char *text, qa_download_source *prepared_download,
                         bool has_prepared, command_scope *c) {
  qa_qw_command_result *result = &c->result;
  c->args = qa_quakeworld_command_arguments(text, &c->argc);
  const char *op = c->argc > 0 ? c->args[0] : NULL;
  if (op && strcmp(op, "new") == 0) {
    if (!s->spawned) fresh_messages(s, result);
    return;
  }
  if (op && strcmp(op, "download") == 0) {
    qa_download_source *previous = s->download;
    s->download = has_prepared ? prepared_download : NULL;
    qa_caught_error caught;
    if (previous && !qa_try(close_body, previous, &caught)) { qa_quakeworld_signon_server_close(s); qa_rethrow(&caught); }
    /* TS: try { downloadPath(path); this.download = ... } catch { this.close(); } */
    open_scope o = { s, c->argc > 1 ? c->args[1] : "", prepared_download, has_prepared };
    if (!qa_try(open_body, &o, &caught)) qa_quakeworld_signon_server_close(s);
    s->download_offset = 0;
    next_download(s, result);
    return;
  }
  if (op && strcmp(op, "nextdl") == 0) {
    if (s->download) next_download(s, result);
    return;
  }
  if (!op || (strcmp(op, "soundlist") != 0 && strcmp(op, "modellist") != 0 && strcmp(op, "prespawn") != 0
              && strcmp(op, "spawn") != 0 && strcmp(op, "begin") != 0)) {
    result->handled = false;
    size_t n = strlen(text) + 1; result->text = malloc(n); memcpy(result->text, text, n);
    return;
  }
  memset(&c->data, 0, sizeof c->data);
  s->host.server_data(s->host.ctx, &c->data);
  c->has_data = true;
  const qa_quakeworld_message *data = &c->data;
  if (s->spawned) return;
  if (js_parse_int(c->argc > 1 ? c->args[1] : "") != data->server_count) {
    fresh_messages(s, result);
    return;
  }
  qa_message_reader empty; qa_message_reader_init(&empty, NULL, 0);
  qa_protocol_codec codec; qa_create_quakeworld_codec(data->protocol, &empty, &codec);
  double start = js_parse_int(c->argc > 2 ? c->args[2] : "0");
  qa_size_buf_init(&c->buf, 1450, false);
  c->has_buf = true;
  qa_size_buf *buf = &c->buf;
  if (strcmp(op, "soundlist") == 0 || strcmp(op, "modellist") == 0) {
    size_t name_count = 0;
    const char *const *names = strcmp(op, "soundlist") == 0
      ? s->host.sounds(s->host.ctx, &name_count) : s->host.models(s->host.ctx, &name_count);
    if (!isfinite(start) || start != trunc(start) || start < 0 || start >= codec.max_precache || start > (double)name_count) {
      fresh_messages(s, result);
      return;
    }
    qa_msg_write_byte(buf, strcmp(op, "soundlist") == 0 ? 46 : 45);
    codec.write_precache_count(&codec, buf, start);
    double next = start;
    while (next < (double)name_count && buf->cursize < 725) {
      const char *name = names[(size_t)next];
      if (!name) break;
      if (next + 1 >= codec.max_precache) qa_range_error("Selected QW profile cannot represent precache");
      qa_msg_write_string(buf, name);
      next++;
    }
    qa_msg_write_byte(buf, 0);
    codec.write_precache_count(&codec, buf, next < (double)name_count ? next : 0);
    size_t n = 0; uint8_t *b = qa_size_buf_bytes(buf, &n); push_msg(result, b, n);
    return;
  }
  if (strcmp(op, "prespawn") == 0) {
    const uint8_t *const *buffers = NULL; const size_t *lens = NULL; size_t buffer_count = 0;
    s->host.signon_buffers(s->host.ctx, &buffers, &lens, &buffer_count);
    double index = (isfinite(start) && start == trunc(start) && start >= 0 && start < (double)buffer_count) ? start : 0;
    if (index == 0 && !s->host.accepts_map_checksum(s->host.ctx, qa_js_to_uint32(js_parse_int(c->argc > 3 ? c->args[3] : "0")))) {
      s->host.disconnect(s->host.ctx, "Map model file does not match");
      return;
    }
    if ((size_t)index < buffer_count) qa_sz_write(buf, buffers[(size_t)index], (long long)lens[(size_t)index], (long long)lens[(size_t)index]);
    char tmp[160];
    if (index + 1 >= (double)buffer_count) snprintf(tmp, sizeof tmp, "cmd spawn %s 0\n", QA_JS_NUMBER(data->server_count));
    else snprintf(tmp, sizeof tmp, "cmd prespawn %s %s\n", QA_JS_NUMBER(data->server_count), QA_JS_NUMBER(index + 1));
    stuff(buf, tmp);
    size_t n = 0; uint8_t *b = qa_size_buf_bytes(buf, &n); push_msg(result, b, n);
    return;
  }
  if (strcmp(op, "spawn") == 0) {
    if (!isfinite(start) || start != trunc(start) || start < 0 || start > 32) {
      fresh_messages(s, result);
      return;
    }
    size_t *mlens = NULL, mcount = 0;
    uint8_t **messages = s->host.spawn(s->host.ctx, start, &mlens, &mcount);
    for (size_t i = 0; i < mcount; i++) push_msg(result, messages[i], mlens[i]);
    free(messages); free(mlens);
    stuff(buf, "skins\n");
    size_t n = 0; uint8_t *b = qa_size_buf_bytes(buf, &n); push_msg(result, b, n);
    return;
  }
  s->host.begin(s->host.ctx);
  s->spawned = true;
}
qa_qw_command_result qa_quakeworld_signon_server_command(qa_quakeworld_signon_server *s, const char *text,
                                                        qa_download_source *prepared_download, bool has_prepared) {
  command_scope c;
  memset(&c, 0, sizeof c);
  c.result.handled = true;
  qa_cleanup guard;
  qa_cleanup_push(&guard, command_scope_free, &c);
  command_body(s, text, prepared_download, has_prepared, &c);
  qa_cleanup_pop(&guard);
  qa_qw_command_result result = c.result;
  memset(&c.result, 0, sizeof c.result);
  command_scope_free(&c);
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
  if (has_checksum) snprintf(tmp, sizeof tmp, "%s %s %s %u", verb, QA_JS_NUMBER(server_count), QA_JS_NUMBER(next), checksum);
  else if (has_next) snprintf(tmp, sizeof tmp, "%s %s %s", verb, QA_JS_NUMBER(server_count), QA_JS_NUMBER(next));
  else snprintf(tmp, sizeof tmp, "%s %s", verb, QA_JS_NUMBER(server_count));
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
    if (message->first > (double)*count) qa_raise("Error", 0, 0, "Precache list skipped entries");
    /* Array.prototype.splice(first, length - first, ...names): a negative start counts from the end. */
    double relative = trunc(message->first);
    double start = relative < 0 ? qa_js_max((double)*count + relative, 0) : qa_js_min(relative, (double)*count);
    size_t first = isfinite(start) ? (size_t)start : 0;
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
