/* Port of src/network/q2/gtv.ts. GPL-2.0-or-later. */
#include "qa/network/q2/gtv.h"
#include "qa/foundation/js.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/network/q2/codecs/mvd.h"
#include "qa/network/q2/constants.h"
#include "qa/network/q2/message.h"

static char *dup_str(const char *s) {
  size_t n = strlen(s != NULL ? s : "");
  char *out = (char *)malloc(n + 1);
  if (out != NULL) memcpy(out, s != NULL ? s : "", n + 1);
  return out;
}
/* TS packet(op, body): frameMvdMessage(op || body). */
static uint8_t *gtv_packet(double op, const uint8_t *body, size_t body_length,
                           size_t *out_length) {
  uint8_t *message = (uint8_t *)malloc(body_length + 1);
  if (message == NULL) return NULL;
  message[0] = qa_js_to_uint8(op); /* Uint8Array store of the opcode */
  if (body_length > 0) memcpy(message + 1, body, body_length);
  uint8_t *out = qa_q2_frame_mvd_message(message, body_length + 1, out_length);
  free(message);
  return out;
}
/* TS checkedText: C strings are bytes <=255 and cannot embed NUL, so
 * only a NULL text can violate the non-NUL single-byte rule here. */
static void checked_text(const char *text) {
  if (text == NULL) {
    qa_q2_throw_error("GTV strings must be non-NUL single-byte text");
  }
}

void qa_q2_gtv_events_free(qa_q2_gtv_event *events, size_t count) {
  if (events != NULL) {
    for (size_t i = 0; i < count; i++) {
      free(events[i].bytes);
      free(events[i].reason);
    }
    free(events);
  }
}

/* --- GtvCompression --- */
static void compression_init(qa_q2_gtv_compression *c, bool encode) {
  memset(c, 0, sizeof *c);
  c->encode = encode;
  int status = encode ? deflateInit(&c->stream, Z_DEFAULT_COMPRESSION)
                      : inflateInit(&c->stream);
  if (status != Z_OK) {
    c->failed = true;
    (void)snprintf(c->failure, sizeof c->failure, "%s",
                   c->stream.msg != NULL ? c->stream.msg : "zlib error");
    return;
  }
  c->active = true;
}
static void compression_close(qa_q2_gtv_compression *c) {
  if (c->active) {
    if (c->encode) deflateEnd(&c->stream);
    else inflateEnd(&c->stream);
  }
  c->active = false;
}
/* TS push(): collect all output produced by this input (sync flush
 * when encoding). Malloc'd or NULL on OOM; stream failures abort with
 * the stored zlib message like the TS throw. */
static uint8_t *compression_push(qa_q2_gtv_compression *c, const uint8_t *bytes,
                                 size_t length, size_t *out_length) {
  if (c->failed) qa_q2_throw_error("%s", c->failure);
  size_t capacity = length + 8192;
  uint8_t *out = (uint8_t *)malloc(capacity);
  if (out == NULL) return NULL;
  size_t produced = 0;
  c->stream.next_in = (Bytef *)bytes;
  c->stream.avail_in = (uInt)length;
  int flush = c->encode ? Z_SYNC_FLUSH : Z_NO_FLUSH;
  for (;;) {
    if (produced == capacity) {
      size_t next = capacity * 2;
      uint8_t *grown = (uint8_t *)realloc(out, next);
      if (grown == NULL) {
        free(out);
        return NULL;
      }
      out = grown;
      capacity = next;
    }
    c->stream.next_out = out + produced;
    c->stream.avail_out = (uInt)(capacity - produced);
    uLong before_out = c->stream.total_out;
    int status = c->encode ? deflate(&c->stream, flush) : inflate(&c->stream, flush);
    produced += (size_t)(c->stream.total_out - before_out);
    if (produced > (size_t)QA_Q2_MVD_MAX_MESSAGE * 64) {
      free(out);
      c->failed = true;
      (void)snprintf(c->failure, sizeof c->failure, "GTV decompression output limit exceeded");
      qa_q2_throw_error("%s", c->failure);
    }
    if (status == Z_STREAM_END) break;
    if (status != Z_OK && status != Z_BUF_ERROR) {
      free(out);
      c->failed = true;
      (void)snprintf(c->failure, sizeof c->failure, "%s",
                     c->stream.msg != NULL ? c->stream.msg : "zlib error");
      qa_q2_throw_error("%s", c->failure);
    }
    if (c->stream.avail_in == 0 && c->stream.avail_out != 0) break;
    if (status == Z_BUF_ERROR && c->stream.avail_out != 0) break;
  }
  if (out_length != NULL) *out_length = produced;
  return out;
}

/* --- GtvClient --- */
typedef struct consume_owned {
  qa_q2_sizebuf *message;
  uint8_t *copy;
} consume_owned;
static void consume_owned_release(void *arg) {
  consume_owned *held = (consume_owned *)arg;
  free(held->copy);
  qa_q2_message_destroy(held->message);
}
bool qa_q2_gtv_client_init(qa_q2_gtv_client *client, qa_q2_gtv_send_fn send,
                           void *ctx, qa_q2_gtv_identity identity,
                           double requested_flags) {
  checked_text(identity.username);
  checked_text(identity.password);
  checked_text(identity.version);
  memset(client, 0, sizeof *client);
  client->send = send;
  client->send_ctx = ctx;
  client->username = dup_str(identity.username);
  client->password = dup_str(identity.password);
  client->version = dup_str(identity.version);
  if (client->username == NULL || client->password == NULL ||
      client->version == NULL) {
    free(client->username);
    free(client->password);
    free(client->version);
    return false;
  }
  client->requested_flags =
      requested_flags < 0 ? (QA_Q2_GTF_DEFLATE | QA_Q2_GTF_STRINGCMDS)
                          : requested_flags;
  client->phase = QA_Q2_GTV_MAGIC;
  qa_q2_mvd_framer_init(&client->frames, false, QA_Q2_MVD_MAX_MESSAGE);
  return true;
}
void qa_q2_gtv_client_destroy(qa_q2_gtv_client *client) {
  qa_q2_gtv_client_close(client);
  qa_q2_mvd_framer_destroy(&client->frames);
  free(client->username);
  free(client->password);
  free(client->version);
  memset(client, 0, sizeof *client);
}
static void client_send(qa_q2_gtv_client *client, const uint8_t *bytes,
                        size_t length) {
  client->send(client->send_ctx, bytes, length);
}
static void client_send_owned(qa_q2_gtv_client *client, uint8_t *bytes,
                              size_t length) {
  if (bytes == NULL) qa_range_error("out of memory");
  client_send(client, bytes, length);
  free(bytes);
}
void qa_q2_gtv_client_start(qa_q2_gtv_client *client) {
  if (client->started || client->phase != QA_Q2_GTV_MAGIC) {
    qa_q2_throw_error("GTV already started");
  }
  client->started = true;
  size_t length = 0;
  uint8_t *magic = qa_q2_mvd_magic(&length); /* sets length before it is passed */
  client_send_owned(client, magic, length);
}
void qa_q2_gtv_client_request_start(qa_q2_gtv_client *client,
                                    double max_buffered_packets) {
  if (max_buffered_packets < 0) max_buffered_packets = 10;
  if (client->phase != QA_Q2_GTV_CONNECTED) {
    qa_q2_throw_error("GTV is not ready to start");
  }
  if (max_buffered_packets != trunc(max_buffered_packets) ||
      max_buffered_packets < 0 || max_buffered_packets > 65535) {
    qa_range_error("Invalid GTV buffering request");
  }
  uint8_t body[2];
  body[0] = (uint8_t)(qa_js_to_uint32(max_buffered_packets) & 0xff);
  body[1] = (uint8_t)((qa_js_to_uint32(max_buffered_packets) >> 8) & 0xff);
  size_t length = 0;
  { /* the call sets length before it is passed */
    uint8_t *encoded_ = gtv_packet(QA_Q2_GTC_STREAM_START, body, 2, &length);
    client_send_owned(client, encoded_, length);
  }
  client->phase = QA_Q2_GTV_STARTING;
}
void qa_q2_gtv_client_request_stop(qa_q2_gtv_client *client) {
  if (client->phase != QA_Q2_GTV_READING && client->phase != QA_Q2_GTV_WAITING) {
    qa_q2_throw_error("GTV is not streaming");
  }
  size_t length = 0;
  { /* the call sets length before it is passed */
    uint8_t *encoded_ = gtv_packet(QA_Q2_GTC_STREAM_STOP, NULL, 0, &length);
    client_send_owned(client, encoded_, length);
  }
  client->phase = QA_Q2_GTV_STOPPING;
}
static void client_connected(qa_q2_gtv_client *client) {
  if (client->phase == QA_Q2_GTV_MAGIC || client->phase == QA_Q2_GTV_HELLO ||
      client->phase == QA_Q2_GTV_CLOSED) {
    qa_q2_throw_error("GTV is not connected");
  }
}
void qa_q2_gtv_client_ping(qa_q2_gtv_client *client) {
  client_connected(client);
  size_t length = 0;
  { /* the call sets length before it is passed */
    uint8_t *encoded_ = gtv_packet(QA_Q2_GTC_PING, NULL, 0, &length);
    client_send_owned(client, encoded_, length);
  }
}
void qa_q2_gtv_client_command(qa_q2_gtv_client *client, const char *text) {
  client_connected(client);
  checked_text(text);
  if (((qa_js_to_int32(client->flags)) & QA_Q2_GTF_STRINGCMDS) == 0) {
    qa_q2_throw_error("GTV command forwarding was not negotiated");
  }
  qa_q2_sizebuf message;
  if (!qa_q2_message_create(&message, QA_Q2_MAX_GTC_MSGLEN)) {
    qa_range_error("out of memory");
  }
  /* TS text.slice(0, 150) is in UTF-16 units; bytes here. */
  char truncated[151];
  size_t n = strlen(text != NULL ? text : "");
  if (n > 150) n = 150;
  memcpy(truncated, text != NULL ? text : "", n);
  truncated[n] = '\0';
  qa_q2_msg_write_string(&message, truncated);
  size_t body_length = 0;
  uint8_t *body = qa_q2_message_bytes(&message, &body_length);
  qa_q2_message_destroy(&message);
  if (body == NULL) qa_range_error("out of memory");
  size_t length = 0;
  uint8_t *packet = gtv_packet(QA_Q2_GTC_STRINGCMD, body, body_length, &length);
  free(body);
  client_send_owned(client, packet, length);
}
void qa_q2_gtv_client_close(qa_q2_gtv_client *client) {
  client->phase = QA_Q2_GTV_CLOSED;
  compression_close(&client->compression);
}

typedef struct event_list {
  qa_q2_gtv_event *items;
  size_t count, capacity;
  bool oom;
} event_list;
static bool event_push(event_list *list, qa_q2_gtv_event_kind kind) {
  if (list->count == list->capacity) {
    size_t next = list->capacity > 0 ? list->capacity * 2 : 8;
    qa_q2_gtv_event *grown =
        (qa_q2_gtv_event *)realloc(list->items, next * sizeof *grown);
    if (grown == NULL) {
      list->oom = true;
      return false;
    }
    list->items = grown;
    list->capacity = next;
  }
  memset(&list->items[list->count], 0, sizeof *list->items);
  list->items[list->count].kind = kind;
  list->count++;
  return true;
}
static const char *server_op_name(int32_t opcode) {
  switch (opcode) {
    case QA_Q2_GTS_HELLO: return "GTS_HELLO";
    case QA_Q2_GTS_PONG: return "GTS_PONG";
    case QA_Q2_GTS_STREAM_START: return "GTS_STREAM_START";
    case QA_Q2_GTS_STREAM_STOP: return "GTS_STREAM_STOP";
    case QA_Q2_GTS_STREAM_DATA: return "GTS_STREAM_DATA";
    case QA_Q2_GTS_ERROR: return "GTS_ERROR";
    case QA_Q2_GTS_BADREQUEST: return "GTS_BADREQUEST";
    case QA_Q2_GTS_NOACCESS: return "GTS_NOACCESS";
    case QA_Q2_GTS_DISCONNECT: return "GTS_DISCONNECT";
    case QA_Q2_GTS_RECONNECT: return "GTS_RECONNECT";
    default: return NULL;
  }
}
/* TS consume(). TS throws become aborts; on throw the caller closes. */
static void client_consume(qa_q2_gtv_client *client, const uint8_t *bytes,
                           size_t length, event_list *events) {
  qa_q2_sizebuf message;
  if (!qa_q2_message_create(&message, (int64_t)length)) {
    events->oom = true;
    return;
  }
  uint8_t *copy = (uint8_t *)malloc(length > 0 ? length : 1);
  if (copy == NULL) {
    qa_q2_message_destroy(&message);
    events->oom = true;
    return;
  }
  if (length > 0) memcpy(copy, bytes, length);
  qa_q2_message_load(&message, copy, length);
  consume_owned held = {&message, copy};
  qa_cleanup node;
  qa_cleanup_push(&node, consume_owned_release, &held);
  int32_t opcode = qa_q2_msg_read_byte(&message);
  bool skip_trailing = false;
  if (opcode == QA_Q2_GTS_HELLO) {
    if (client->phase != QA_Q2_GTV_HELLO) qa_q2_throw_error("Unexpected GTV hello");
    client->flags = (double)qa_q2_msg_read_long(&message);
    if ((qa_js_to_int32(client->flags) & ~qa_js_to_int32(client->requested_flags)) != 0) {
      qa_q2_throw_error("Unrequested GTV flags");
    }
    if ((qa_js_to_int32(client->flags) & QA_Q2_GTF_DEFLATE) != 0) {
      compression_init(&client->compression, false);
    }
    client->phase = QA_Q2_GTV_CONNECTED;
    if (event_push(events, QA_Q2_GTV_EVENT_HELLO)) {
      events->items[events->count - 1].flags = client->flags;
    }
  } else if (opcode == QA_Q2_GTS_STREAM_START) {
    if (client->phase != QA_Q2_GTV_STARTING) {
      qa_q2_throw_error("Unexpected GTV start acknowledgement");
    }
    client->phase = QA_Q2_GTV_READING;
    event_push(events, QA_Q2_GTV_EVENT_STARTED);
  } else if (opcode == QA_Q2_GTS_STREAM_STOP) {
    if (client->phase != QA_Q2_GTV_STOPPING) {
      qa_q2_throw_error("Unexpected GTV stop acknowledgement");
    }
    client->phase = QA_Q2_GTV_CONNECTED;
    event_push(events, QA_Q2_GTV_EVENT_STOPPED);
  } else if (opcode == QA_Q2_GTS_STREAM_DATA) {
    skip_trailing = true;
    if (client->phase == QA_Q2_GTV_STOPPING) {
      /* TS returns silently. */
    } else {
      if (client->phase != QA_Q2_GTV_READING && client->phase != QA_Q2_GTV_WAITING) {
        qa_q2_throw_error("Unexpected GTV stream data");
      }
      if (length == 1) {
        client->phase = QA_Q2_GTV_WAITING;
        event_push(events, QA_Q2_GTV_EVENT_SUSPENDED);
      } else {
        if (client->phase == QA_Q2_GTV_WAITING) {
          event_push(events, QA_Q2_GTV_EVENT_RESUMED);
        }
        client->phase = QA_Q2_GTV_READING;
        if (event_push(events, QA_Q2_GTV_EVENT_DATA)) {
          qa_q2_gtv_event *event = &events->items[events->count - 1];
          event->bytes = (uint8_t *)malloc(length - 1 > 0 ? length - 1 : 1);
          if (event->bytes == NULL) {
            events->oom = true;
          } else {
            memcpy(event->bytes, bytes + 1, length - 1);
            event->bytes_length = length - 1;
          }
        }
      }
    }
  } else if (opcode == QA_Q2_GTS_PONG) {
    client_connected(client);
    event_push(events, QA_Q2_GTV_EVENT_PONG);
  } else if (opcode == QA_Q2_GTS_ERROR || opcode == QA_Q2_GTS_BADREQUEST ||
             opcode == QA_Q2_GTS_NOACCESS || opcode == QA_Q2_GTS_DISCONNECT ||
             opcode == QA_Q2_GTS_RECONNECT) {
    qa_q2_gtv_client_close(client);
    if (event_push(events, QA_Q2_GTV_EVENT_CLOSED)) {
      const char *name = server_op_name(opcode);
      events->items[events->count - 1].reason =
          dup_str(name != NULL ? name : "GTV closed");
      if (events->items[events->count - 1].reason == NULL) events->oom = true;
    }
  } else {
    qa_q2_throw_error("Unknown GTV server opcode %d", opcode);
  }
  if (!skip_trailing) {
    qa_q2_message_check_read(&message);
    if (message.readcount != message.cursize) {
      qa_q2_throw_error("Trailing GTV control bytes");
    }
  }
  qa_cleanup_pop(&node);
  free(copy);
  qa_q2_message_destroy(&message);
}

typedef struct consume_ctx {
  qa_q2_gtv_client *client;
  event_list *events;
  bool switched;
} consume_ctx;
static bool consume_message(void *vctx, const uint8_t *bytes, size_t length) {
  consume_ctx *c = (consume_ctx *)vctx;
  bool before = c->client->compression.active;
  client_consume(c->client, bytes, length, c->events);
  c->switched = before != c->client->compression.active;
  return !c->switched && !c->events->oom;
}
static bool consume_plain(void *vctx, const uint8_t *bytes, size_t length) {
  consume_ctx *c = (consume_ctx *)vctx;
  client_consume(c->client, bytes, length, c->events);
  return !c->events->oom;
}

typedef struct receive_owned {
  event_list events;
  uint8_t *owned;
  uint8_t *pending;
} receive_owned;
static void receive_owned_release(void *arg) {
  receive_owned *r = (receive_owned *)arg;
  qa_q2_gtv_events_free(r->events.items, r->events.count);
  free(r->owned);
  free(r->pending);
}
static bool client_receive(qa_q2_gtv_client *client, const uint8_t *input, size_t length,
                           qa_q2_gtv_event **out, size_t *out_count);
static bool client_receive_held(qa_q2_gtv_client *client, const uint8_t *input, size_t length,
                                receive_owned *held);
typedef struct receive_scope {
  qa_q2_gtv_client *client;
  const uint8_t *input;
  size_t length;
  qa_q2_gtv_event **out;
  size_t *out_count;
  bool ok;
} receive_scope;
static void receive_body(void *arg) {
  receive_scope *scope = (receive_scope *)arg;
  scope->ok = client_receive(scope->client, scope->input, scope->length, scope->out,
                             scope->out_count);
}
bool qa_q2_gtv_client_receive(qa_q2_gtv_client *client, const uint8_t *input,
                              size_t length, qa_q2_gtv_event **out,
                              size_t *out_count) {
  *out = NULL;
  *out_count = 0;
  if (!client->started) qa_q2_throw_error("GTV was not started");
  if (client->busy) qa_q2_throw_error("Concurrent GTV receive");
  if (client->phase == QA_Q2_GTV_CLOSED) {
    qa_q2_throw_error("GTV connection is closed");
  }
  client->busy = true;
  /* TS: catch (error) { this.close(); throw error; } finally { this.busy = false; } */
  receive_scope scope = {client, input, length, out, out_count, false};
  qa_caught_error caught;
  bool ok = qa_try(receive_body, &scope, &caught);
  client->busy = false;
  if (!ok) {
    qa_q2_gtv_client_close(client);
    qa_rethrow(&caught);
  }
  return scope.ok;
}
static bool client_receive(qa_q2_gtv_client *client, const uint8_t *input, size_t length,
                           qa_q2_gtv_event **out, size_t *out_count) {
  receive_owned held = {{NULL, 0, 0, false}, NULL, NULL};
  qa_cleanup node;
  qa_cleanup_push(&node, receive_owned_release, &held);
  bool ok = client_receive_held(client, input, length, &held);
  qa_cleanup_pop(&node);
  free(held.owned);
  free(held.pending);
  if (!ok) {
    qa_q2_gtv_events_free(held.events.items, held.events.count);
    return false;
  }
  *out = held.events.items;
  *out_count = held.events.count;
  return true;
}
static bool client_receive_held(qa_q2_gtv_client *client, const uint8_t *input, size_t length,
                                receive_owned *held) {
  event_list *events_ref = &held->events;
#define events (*events_ref)
  const uint8_t *bytes = input;
  size_t bytes_length = length;
#define owned (held->owned)
  if (client->phase == QA_Q2_GTV_MAGIC) {
    size_t count = 4 - client->magic_length < bytes_length
                       ? 4 - client->magic_length
                       : bytes_length;
    if (count > 0) memcpy(client->magic + client->magic_length, bytes, count);
    client->magic_length += count;
    bytes += count;
    bytes_length -= count;
    if (client->magic_length != 4) {
      return true;
    }
    size_t magic_length = 0;
    uint8_t *magic = qa_q2_mvd_magic(&magic_length);
    if (magic == NULL) return false;
    bool matches = memcmp(client->magic, magic, 4) == 0;
    free(magic);
    if (!matches) {
      qa_q2_throw_error("Not a GTV server");
    }
    qa_q2_sizebuf message;
    if (!qa_q2_message_create(&message, QA_Q2_MAX_GTC_MSGLEN)) return false;
    qa_q2_msg_write_short(&message, QA_Q2_GTV_PROTOCOL_VERSION);
    qa_q2_msg_write_long(&message, client->requested_flags);
    qa_q2_msg_write_long(&message, 0);
    qa_q2_msg_write_string(&message, client->username);
    qa_q2_msg_write_string(&message, client->password);
    qa_q2_msg_write_string(&message, client->version);
    size_t body_length = 0;
    uint8_t *body = qa_q2_message_bytes(&message, &body_length);
    qa_q2_message_destroy(&message);
    if (body == NULL) return false;
    size_t hello_length = 0;
    uint8_t *hello = gtv_packet(QA_Q2_GTC_HELLO, body, body_length, &hello_length);
    free(body);
    if (hello == NULL) return false;
    if (hello_length - 2 > QA_Q2_MAX_GTC_MSGLEN) {
      free(hello);
      qa_q2_throw_error("GTV identity exceeds message limit");
    }
    client->phase = QA_Q2_GTV_HELLO;
    client_send(client, hello, hello_length);
    free(hello);
  }
  if (client->compression.active) {
    size_t out_length = 0;
    owned = compression_push(&client->compression, bytes, bytes_length,
                             &out_length);
    if (owned == NULL) return false;
    bytes = owned;
    bytes_length = out_length;
  }
  consume_ctx ctx = {client, &events, false};
  qa_q2_mvd_framer_push(&client->frames, bytes, bytes_length, consume_message,
                        &ctx);
  free(owned);
  owned = NULL;
  if (ctx.switched && client->compression.active) {
    size_t pending_length = 0;
    held->pending = qa_q2_mvd_framer_take_pending(&client->frames, &pending_length);
    if (pending_length != 0) {
      size_t out_length = 0;
      owned = compression_push(&client->compression, held->pending, pending_length,
                               &out_length);
      if (owned == NULL) return false;
      qa_q2_mvd_framer_push(&client->frames, owned, out_length, consume_plain, &ctx);
    }
  }
  if (events.oom) return false;
  if (qa_q2_mvd_framer_finished(&client->frames)) {
    qa_q2_gtv_client_close(client);
    if (event_push(&events, QA_Q2_GTV_EVENT_CLOSED)) {
      events.items[events.count - 1].reason = dup_str("end of stream");
      if (events.items[events.count - 1].reason == NULL) return false;
    }
  }
  return true;
#undef owned
#undef events
}

bool qa_q2_read_gtv_request(const uint8_t *bytes, size_t length,
                            qa_q2_gtv_request *out) {
  if (length < 1 || length > QA_Q2_MAX_GTC_MSGLEN) {
    qa_range_error("Invalid GTV client message length");
  }
  memset(out, 0, sizeof *out);
  qa_q2_sizebuf message;
  if (!qa_q2_message_create(&message, (int64_t)length)) return false;
  uint8_t *copy = (uint8_t *)malloc(length);
  if (copy == NULL) {
    qa_q2_message_destroy(&message);
    return false;
  }
  memcpy(copy, bytes, length);
  qa_q2_message_load(&message, copy, length);
  consume_owned held = {&message, copy};
  qa_cleanup node;
  qa_cleanup_push(&node, consume_owned_release, &held);
  int32_t opcode = qa_q2_msg_read_byte(&message);
  if (opcode == QA_Q2_GTC_HELLO) {
    if (qa_q2_msg_read_word(&message) != QA_Q2_GTV_PROTOCOL_VERSION) {
      qa_q2_throw_error("Unsupported GTV protocol");
    }
    out->kind = QA_Q2_GTV_REQUEST_HELLO;
    out->hello.flags = (double)qa_q2_msg_read_long(&message);
    (void)qa_q2_msg_read_long(&message);
    qa_q2_msg_read_string(&message, out->hello.username,
                          sizeof out->hello.username);
    qa_q2_msg_read_string(&message, out->hello.password,
                          sizeof out->hello.password);
    qa_q2_msg_read_string(&message, out->hello.version,
                          sizeof out->hello.version);
  } else if (opcode == QA_Q2_GTC_PING) {
    out->kind = QA_Q2_GTV_REQUEST_PING;
  } else if (opcode == QA_Q2_GTC_STREAM_START) {
    out->kind = QA_Q2_GTV_REQUEST_START;
    out->max_buffered_packets = (double)qa_q2_msg_read_word(&message);
  } else if (opcode == QA_Q2_GTC_STREAM_STOP) {
    out->kind = QA_Q2_GTV_REQUEST_STOP;
  } else if (opcode == QA_Q2_GTC_STRINGCMD) {
    out->kind = QA_Q2_GTV_REQUEST_COMMAND;
    qa_q2_msg_read_string(&message, out->text, sizeof out->text);
  } else {
    qa_q2_throw_error("Unknown GTV client opcode");
  }
  qa_q2_message_check_read(&message);
  if (message.readcount != message.cursize) {
    qa_q2_throw_error("Trailing GTV request bytes");
  }
  qa_cleanup_pop(&node);
  free(copy);
  qa_q2_message_destroy(&message);
  return true;
}

void qa_q2_gtv_server_stream_init(qa_q2_gtv_server_stream *stream) {
  memset(stream, 0, sizeof *stream);
}
uint8_t *qa_q2_gtv_server_stream_hello(qa_q2_gtv_server_stream *stream,
                                       double flags, size_t *out_length) {
  if (stream->greeted || stream->closed) {
    qa_q2_throw_error("GTV server hello already sent");
  }
  if (((qa_js_to_int32(flags)) & ~(QA_Q2_GTF_DEFLATE | QA_Q2_GTF_STRINGCMDS)) != 0) {
    qa_q2_throw_error("Unknown GTV negotiated flags");
  }
  stream->greeted = true;
  uint8_t body[4];
  int32_t value = qa_js_to_int32(flags);
  body[0] = (uint8_t)((uint32_t)value & 0xff);
  body[1] = (uint8_t)(((uint32_t)value >> 8) & 0xff);
  body[2] = (uint8_t)(((uint32_t)value >> 16) & 0xff);
  body[3] = (uint8_t)(((uint32_t)value >> 24) & 0xff);
  if (((qa_js_to_int32(flags)) & QA_Q2_GTF_DEFLATE) != 0) {
    compression_init(&stream->compression, true);
  }
  return gtv_packet(QA_Q2_GTS_HELLO, body, 4, out_length);
}
uint8_t *qa_q2_gtv_server_stream_message(qa_q2_gtv_server_stream *stream,
                                         double opcode, const uint8_t *body,
                                         size_t body_length,
                                         size_t *out_length) {
  if (!stream->greeted || stream->closed || stream->busy ||
      opcode == QA_Q2_GTS_HELLO /* opcode === GTS_HELLO: 5.5 is not HELLO */) {
    qa_q2_throw_error("Invalid GTV server stream state");
  }
  stream->busy = true;
  size_t length = 0;
  uint8_t *packet = gtv_packet(opcode, body, body_length, &length);
  uint8_t *result = packet;
  if (packet != NULL && stream->compression.active) {
    result = compression_push(&stream->compression, packet, length, out_length);
    free(packet);
  } else if (out_length != NULL) {
    *out_length = length;
  }
  stream->busy = false;
  return result;
}
void qa_q2_gtv_server_stream_close(qa_q2_gtv_server_stream *stream) {
  stream->closed = true;
  compression_close(&stream->compression);
}


