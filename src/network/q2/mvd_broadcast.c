/* Port of src/network/q2/mvd-broadcast.ts. GPL-2.0-or-later. */
#include "qa/network/q2/mvd_broadcast.h"
#include "qa/foundation/js.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "qa/foundation/support.h"
#include "qa/network/q2/codecs/mvd.h"
#include "qa/network/q2/constants.h"

static char *dup_str(const char *s) {
  size_t n = strlen(s != NULL ? s : "");
  char *out = (char *)malloc(n + 1);
  if (out != NULL) memcpy(out, s != NULL ? s : "", n + 1);
  return out;
}
static bool send_all(int fd, const uint8_t *bytes, size_t length) {
  size_t sent = 0;
  while (sent < length) {
    ssize_t n = send(fd, bytes + sent, length - sent, MSG_NOSIGNAL);
    if (n < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    if (n == 0) return false;
    sent += (size_t)n;
  }
  return true;
}
static void retire(qa_q2_mvd_broadcast *b, qa_q2_mvd_viewer *viewer);
static double monotonic_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec * 1000 + (double)ts.tv_nsec / 1000000;
}

static void viewer_send(qa_q2_mvd_broadcast *b, qa_q2_mvd_viewer *viewer,
                        const uint8_t *bytes, size_t length) {
  (void)b;
  if (viewer->closed) qa_q2_throw_error("GTV viewer retired");
  if (!send_all(viewer->socket_fd, bytes, length)) {
    qa_q2_throw_error("GTV viewer retired");
  }
}
static void free_arg(void *arg) { free(arg); }
static void viewer_send_owned(qa_q2_mvd_broadcast *b, qa_q2_mvd_viewer *viewer,
                              uint8_t *bytes, size_t length) {
  if (bytes == NULL) qa_range_error("out of memory");
  qa_cleanup node;
  qa_cleanup_push(&node, free_arg, bytes);
  viewer_send(b, viewer, bytes, length);
  qa_cleanup_pop(&node);
  free(bytes);
}
static void viewer_send_message(qa_q2_mvd_broadcast *b,
                                qa_q2_mvd_viewer *viewer, double opcode,
                                const uint8_t *body, size_t body_length) {
  size_t length = 0;
  uint8_t *bytes = qa_q2_gtv_server_stream_message(&viewer->stream, opcode, body,
                                                   body_length, &length);
  viewer_send_owned(b, viewer, bytes, length);
}

static void free_latest(qa_q2_mvd_broadcast *b) {
  for (size_t i = 0; i < b->config_count; i++) {
    free((void *)b->configs[i].value);
  }
  free(b->configs);
  free(b->gamedir);
  free(b->portal_bits);
  free(b->players);
  free(b->entities);
  b->configs = NULL;
  b->config_count = 0;
  b->gamedir = NULL;
  b->portal_bits = NULL;
  b->portal_bits_length = 0;
  b->players = NULL;
  b->player_count = 0;
  b->entities = NULL;
  b->entity_count = 0;
  b->has_latest = false;
}
/* Deep-copy capture into latest (messages cleared as in every
 * gamestate re-encode). */
static void remember_latest(qa_q2_mvd_broadcast *b,
                            const qa_q2_mvd_capture *capture) {
  free_latest(b);
  b->revision = capture->revision;
  b->flags = capture->flags;
  b->servercount = capture->servercount;
  b->dummy = capture->dummy;
  b->gamedir = dup_str(capture->gamedir);
  if (b->gamedir == NULL) qa_range_error("out of memory");
  if (capture->config_string_count > 0) {
    b->configs = (qa_q2_mvd_capture_config *)calloc(
        capture->config_string_count, sizeof *b->configs);
    if (b->configs == NULL) qa_range_error("out of memory");
    for (size_t i = 0; i < capture->config_string_count; i++) {
      b->configs[i].index = capture->config_strings[i].index;
      b->configs[i].value = dup_str(capture->config_strings[i].value);
      if (b->configs[i].value == NULL) qa_range_error("out of memory");
    }
    b->config_count = capture->config_string_count;
  }
  if (capture->portal_bits_length > 0) {
    b->portal_bits = (uint8_t *)malloc(capture->portal_bits_length);
    if (b->portal_bits == NULL) qa_range_error("out of memory");
    memcpy(b->portal_bits, capture->portal_bits, capture->portal_bits_length);
    b->portal_bits_length = capture->portal_bits_length;
  }
  if (capture->player_count > 0) {
    b->players = (qa_q2_mvd_capture_player *)malloc(
        capture->player_count * sizeof *b->players);
    if (b->players == NULL) qa_range_error("out of memory");
    memcpy(b->players, capture->players,
           capture->player_count * sizeof *b->players);
    b->player_count = capture->player_count;
  }
  if (capture->entity_count > 0) {
    b->entities =
        (qa_q2_wire_entity *)malloc(capture->entity_count * sizeof *b->entities);
    if (b->entities == NULL) qa_range_error("out of memory");
    memcpy(b->entities, capture->entities,
           capture->entity_count * sizeof *b->entities);
    b->entity_count = capture->entity_count;
  }
  b->has_latest = true;
}
static qa_q2_mvd_capture latest_capture(const qa_q2_mvd_broadcast *b) {
  qa_q2_mvd_capture capture;
  memset(&capture, 0, sizeof capture);
  capture.revision = b->revision;
  capture.flags = b->flags;
  capture.servercount = b->servercount;
  capture.gamedir = b->gamedir;
  capture.dummy = b->dummy;
  capture.config_strings = b->configs;
  capture.config_string_count = b->config_count;
  capture.portal_bits = b->portal_bits;
  capture.portal_bits_length = b->portal_bits_length;
  capture.players = b->players;
  capture.player_count = b->player_count;
  capture.entities = b->entities;
  capture.entity_count = b->entity_count;
  return capture;
}
/* Fresh-encoder gamestate packets for a capture with messages []. */
typedef struct gamestate_owned {
  qa_q2_mvd_encoder *encoder;
  qa_q2_mvd_byte_block *blocks;
  size_t count;
} gamestate_owned;
static void gamestate_release(void *arg) {
  gamestate_owned *held = (gamestate_owned *)arg;
  qa_q2_mvd_byte_blocks_free(held->blocks, held->count);
  qa_q2_mvd_encoder_destroy(held->encoder);
}
static void send_gamestate(qa_q2_mvd_broadcast *b, qa_q2_mvd_viewer *viewer,
                           const qa_q2_mvd_capture *capture) {
  qa_q2_mvd_capture clean = *capture;
  clean.messages = NULL;
  clean.message_count = 0;
  qa_q2_mvd_encoder encoder;
  qa_q2_mvd_encoder_init(&encoder);
  qa_q2_mvd_byte_block *blocks = NULL;
  size_t count = 0;
  if (!qa_q2_mvd_encoder_capture(&encoder, &clean, &blocks, &count)) {
    qa_q2_mvd_encoder_destroy(&encoder);
    qa_range_error("out of memory");
  }
  gamestate_owned held = {&encoder, blocks, count};
  qa_cleanup node;
  qa_cleanup_push(&node, gamestate_release, &held);
  for (size_t i = 0; i < count; i++) {
    viewer_send_message(b, viewer, QA_Q2_GTS_STREAM_DATA, blocks[i].bytes,
                        blocks[i].length);
  }
  qa_cleanup_pop(&node);
  qa_q2_mvd_byte_blocks_free(blocks, count);
  qa_q2_mvd_encoder_destroy(&encoder);
}

void qa_q2_mvd_broadcast_init(qa_q2_mvd_broadcast *b,
                              qa_q2_mvd_broadcast_options options) {
  double max_viewers = options.max_viewers < 0 ? 16 : options.max_viewers;
  if (max_viewers != trunc(max_viewers) || max_viewers < 1) {
    qa_range_error("Invalid GTV viewer limit");
  }
  memset(b, 0, sizeof *b);
  b->options = options;
  b->options.max_viewers = max_viewers;
  qa_q2_mvd_encoder_init(&b->encoder);
  b->has_recording = options.record != NULL;
  if (b->has_recording) {
    qa_q2_mvd_recording_init(&b->recording, options.record, options.ctx);
  }
  b->listener_fd = -1;
}

typedef struct observe_scope {
  qa_q2_mvd_broadcast *b;
  qa_q2_mvd_viewer *viewer;
  const qa_q2_mvd_capture *capture;
  const qa_q2_mvd_byte_block *packets;
  size_t packet_count;
  double sequence;
} observe_scope;
static void observe_body(void *arg) {
  observe_scope *scope = (observe_scope *)arg;
  qa_q2_mvd_viewer *viewer = scope->viewer;
  if (!viewer->active || scope->sequence <= viewer->sequence) return;
  if (viewer->needs_gamestate || viewer->sequence + 1 != scope->sequence) {
    send_gamestate(scope->b, viewer, scope->capture);
  } else {
    for (size_t i = 0; i < scope->packet_count; i++) {
      viewer_send_message(scope->b, viewer, QA_Q2_GTS_STREAM_DATA, scope->packets[i].bytes,
                          scope->packets[i].length);
    }
  }
  viewer->needs_gamestate = false;
  viewer->sequence = scope->sequence;
}

void qa_q2_mvd_broadcast_observe(qa_q2_mvd_broadcast *b,
                                 const qa_q2_mvd_capture *capture) {
  if (b->closed) qa_q2_throw_error("MVD producer is closed");
  qa_q2_mvd_byte_block *packets = NULL;
  size_t packet_count = 0;
  if (!qa_q2_mvd_encoder_capture(&b->encoder, capture, &packets, &packet_count)) {
    qa_range_error("out of memory");
  }
  remember_latest(b, capture);
  double sequence = ++b->sequence;
  for (size_t i = 0; i < packet_count; i++) {
    if (b->has_recording) {
      qa_q2_mvd_recording_append(&b->recording, packets[i].bytes,
                                 packets[i].length);
    }
  }
  double total_length = 0;
  for (size_t i = 0; i < packet_count; i++) {
    total_length += (double)packets[i].length;
  }
  for (size_t vi = 0; vi < b->viewer_count; vi++) {
    qa_q2_mvd_viewer *viewer = &b->viewers[vi];
    if (!viewer->active || viewer->closed) continue;
    viewer->queued_bytes += total_length;
    if (viewer->queued_bytes > 1024 * 1024) {
      viewer->queued_bytes -= total_length;
      retire(b, viewer);
      continue;
    }
    /* TS enqueue(): a failing action retires only this viewer. */
    observe_scope scope = {b, viewer, capture, packets, packet_count, sequence};
    qa_caught_error caught;
    if (!qa_try(observe_body, &scope, &caught)) retire(b, viewer);
    viewer->queued_bytes -= total_length;
  }
  qa_q2_mvd_byte_blocks_free(packets, packet_count);
}

double qa_q2_mvd_broadcast_listen(qa_q2_mvd_broadcast *b, const char *host,
                                  double port) {
  if (b->closed || b->listener_fd >= 0) {
    qa_q2_throw_error("GTV listener already owned or closed");
  }
  if (port != trunc(port) || port < 0 || port > 65535) {
    qa_range_error("Invalid GTV port");
  }
  struct addrinfo hints;
  memset(&hints, 0, sizeof hints);
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_family = AF_UNSPEC;
  hints.ai_flags = AI_PASSIVE;
  char port_text[16];
  (void)snprintf(port_text, sizeof port_text, "%lld", (long long)port);
  struct addrinfo *addresses = NULL;
  if (getaddrinfo(host, port_text, &hints, &addresses) != 0) {
    qa_q2_throw_error("GTV listener has no TCP address");
  }
  int fd = -1;
  for (struct addrinfo *at = addresses; at != NULL; at = at->ai_next) {
    fd = socket(at->ai_family, at->ai_socktype, at->ai_protocol);
    if (fd < 0) continue;
    int one = 1;
    (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    if (bind(fd, at->ai_addr, at->ai_addrlen) == 0 && listen(fd, 16) == 0) break;
    (void)close(fd);
    fd = -1;
  }
  freeaddrinfo(addresses);
  if (fd < 0) qa_q2_throw_error("GTV listener has no TCP address");
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags >= 0) (void)fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  b->listener_fd = fd;
  struct sockaddr_storage address;
  socklen_t length = sizeof address;
  if (getsockname(fd, (struct sockaddr *)&address, &length) != 0) {
    qa_q2_throw_error("GTV listener has no TCP address");
  }
  if (address.ss_family == AF_INET) {
    return (double)ntohs(((struct sockaddr_in *)&address)->sin_port);
  }
  if (address.ss_family == AF_INET6) {
    return (double)ntohs(((struct sockaddr_in6 *)&address)->sin6_port);
  }
  qa_q2_throw_error("GTV listener has no TCP address");
}

/* Retire only marks and closes; removal happens in sweep_closed so
 * a retire inside a framer callback cannot move the viewer (and its
 * framer) out from under the in-progress push. */
static void retire(qa_q2_mvd_broadcast *b, qa_q2_mvd_viewer *viewer) {
  (void)b;
  if (viewer->closed) return;
  viewer->closed = true;
  qa_q2_gtv_server_stream_close(&viewer->stream);
  if (viewer->socket_fd >= 0) {
    (void)shutdown(viewer->socket_fd, SHUT_RDWR);
    (void)close(viewer->socket_fd);
    viewer->socket_fd = -1;
  }
}
static void sweep_closed(qa_q2_mvd_broadcast *b) {
  size_t i = 0;
  while (i < b->viewer_count) {
    if (b->viewers[i].closed) {
      qa_q2_mvd_framer_destroy(&b->viewers[i].frames);
      b->viewers[i] = b->viewers[b->viewer_count - 1];
      b->viewer_count--;
    } else {
      i++;
    }
  }
}
static void attach(qa_q2_mvd_broadcast *b, int fd) {
  sweep_closed(b);
  if (b->closed || (double)b->viewer_count >= b->options.max_viewers) {
    (void)close(fd);
    return;
  }
  if (b->viewer_count == b->viewer_capacity) {
    size_t next = b->viewer_capacity > 0 ? b->viewer_capacity * 2 : 8;
    qa_q2_mvd_viewer *grown =
        (qa_q2_mvd_viewer *)realloc(b->viewers, next * sizeof *grown);
    if (grown == NULL) {
      (void)close(fd);
      return;
    }
    b->viewers = grown;
    b->viewer_capacity = next;
  }
  struct timeval tv;
  tv.tv_sec = 90;
  tv.tv_usec = 0;
  (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
  int one = 1;
  (void)setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
  qa_q2_mvd_viewer *viewer = &b->viewers[b->viewer_count++];
  memset(viewer, 0, sizeof *viewer);
  viewer->socket_fd = fd;
  qa_q2_gtv_server_stream_init(&viewer->stream);
  qa_q2_mvd_framer_init(&viewer->frames, true, 256);
  viewer->needs_gamestate = true;
  viewer->deadline_ms = monotonic_ms() + 10000;
  struct sockaddr_storage peer;
  socklen_t peer_length = sizeof peer;
  if (getpeername(fd, (struct sockaddr *)&peer, &peer_length) == 0) {
    if (peer.ss_family == AF_INET) {
      (void)inet_ntop(AF_INET, &((struct sockaddr_in *)&peer)->sin_addr, viewer->remote_address,
                      sizeof viewer->remote_address);
    } else if (peer.ss_family == AF_INET6) {
      (void)inet_ntop(AF_INET6, &((struct sockaddr_in6 *)&peer)->sin6_addr,
                      viewer->remote_address, sizeof viewer->remote_address);
    }
  }
  /* An unauthenticated connection holds a viewer slot, so it gets a short deadline and a
   * per-address cap. */
  size_t same_host = 0;
  for (size_t i = 0; i < b->viewer_count; i++)
    if (!b->viewers[i].closed &&
        strcmp(b->viewers[i].remote_address, viewer->remote_address) == 0) same_host++;
  if (same_host > 4) retire(b, viewer);
}

typedef struct request_ctx {
  qa_q2_mvd_broadcast *broadcast;
  qa_q2_mvd_viewer *viewer;
} request_ctx;
/* One enqueued TS action, run synchronously in queue order. */
static void handle_request(qa_q2_mvd_broadcast *b, qa_q2_mvd_viewer *viewer,
                           const uint8_t *payload, size_t length) {
  qa_q2_gtv_request request;
  if (!qa_q2_read_gtv_request(payload, length, &request)) {
    qa_range_error("out of memory");
  }
  if (request.kind == QA_Q2_GTV_REQUEST_HELLO) {
    if (viewer->authorized) qa_q2_throw_error("Duplicate GTV hello");
    if (!b->options.authorize(b->options.ctx, &request.hello)) {
      uint8_t op = QA_Q2_GTS_NOACCESS;
      size_t framed_length = 0;
      uint8_t *framed = qa_q2_frame_mvd_message(&op, 1, &framed_length);
      viewer_send_owned(b, viewer, framed, framed_length);
      retire(b, viewer);
      return;
    }
    viewer->flags =
        (double)(qa_js_to_int32(request.hello.flags) & (b->options.command != NULL ? 3 : 1));
    size_t hello_length = 0;
    uint8_t *hello = qa_q2_gtv_server_stream_hello(&viewer->stream, viewer->flags,
                                                   &hello_length);
    viewer_send_owned(b, viewer, hello, hello_length);
    viewer->authorized = true;
    return;
  }
  if (!viewer->authorized) qa_q2_throw_error("GTV request before authorization");
  if (request.kind == QA_Q2_GTV_REQUEST_PING) {
    viewer_send_message(b, viewer, QA_Q2_GTS_PONG, NULL, 0);
  } else if (request.kind == QA_Q2_GTV_REQUEST_START) {
    if (viewer->active) qa_q2_throw_error("GTV already streaming");
    viewer->active = true;
    viewer->needs_gamestate = true;
    viewer_send_message(b, viewer, QA_Q2_GTS_STREAM_START, NULL, 0);
    if (b->has_latest) {
      qa_q2_mvd_capture capture = latest_capture(b);
      double sequence = b->sequence;
      send_gamestate(b, viewer, &capture);
      viewer->needs_gamestate = false;
      viewer->sequence = sequence;
    } else {
      viewer_send_message(b, viewer, QA_Q2_GTS_STREAM_DATA, NULL, 0);
    }
  } else if (request.kind == QA_Q2_GTV_REQUEST_STOP) {
    if (!viewer->active) qa_q2_throw_error("GTV is not streaming");
    viewer->active = false;
    viewer_send_message(b, viewer, QA_Q2_GTS_STREAM_STOP, NULL, 0);
  } else if (request.kind == QA_Q2_GTV_REQUEST_COMMAND) {
    if ((qa_js_to_int32(viewer->flags) & 2) != 0 && b->options.command != NULL) {
      b->options.command(b->options.ctx, request.text);
    }
  }
}
typedef struct request_scope {
  qa_q2_mvd_broadcast *b;
  qa_q2_mvd_viewer *viewer;
  const uint8_t *payload;
  size_t length;
} request_scope;
static void request_body(void *arg) {
  request_scope *scope = (request_scope *)arg;
  if (scope->viewer->closed) return;
  handle_request(scope->b, scope->viewer, scope->payload, scope->length);
}
static bool request_message(void *vctx, const uint8_t *payload, size_t length) {
  request_ctx *c = (request_ctx *)vctx;
  qa_q2_mvd_viewer *viewer = c->viewer;
  if (viewer->closed) return true;
  /* TS enqueue(): byte accounting around the ordered action. */
  viewer->queued_bytes += (double)length;
  if (viewer->queued_bytes > 1024 * 1024) {
    viewer->queued_bytes -= (double)length;
    retire(c->broadcast, viewer);
    return true;
  }
  /* TS writes the magic synchronously after push while request
   * actions run later on the queue, so the magic precedes any hello
   * reply on the wire; mirror that order before handling. */
  if (!viewer->identified && qa_q2_mvd_framer_identified(&viewer->frames)) {
    viewer->identified = true;
    size_t magic_length = 0;
    uint8_t *magic = qa_q2_mvd_magic(&magic_length);
    viewer_send_owned(c->broadcast, viewer, magic, magic_length);
  }
  /* TS enqueue(...).catch(() => retire(viewer)) */
  request_scope scope = {c->broadcast, viewer, payload, length};
  qa_caught_error caught;
  if (!qa_try(request_body, &scope, &caught)) retire(c->broadcast, viewer);
  viewer->queued_bytes -= (double)length;
  return true;
}
typedef struct data_scope {
  qa_q2_mvd_broadcast *b;
  qa_q2_mvd_viewer *viewer;
  const uint8_t *bytes;
  size_t length;
} data_scope;
static void viewer_data_body(qa_q2_mvd_broadcast *b, qa_q2_mvd_viewer *viewer,
                             const uint8_t *bytes, size_t length);
static void data_body(void *arg) {
  data_scope *scope = (data_scope *)arg;
  viewer_data_body(scope->b, scope->viewer, scope->bytes, scope->length);
}
/* TS socket 'data': try { frames.push(...) ... } catch { retire(viewer) } */
static void viewer_data(qa_q2_mvd_broadcast *b, qa_q2_mvd_viewer *viewer,
                        const uint8_t *bytes, size_t length) {
  data_scope scope = {b, viewer, bytes, length};
  qa_caught_error caught;
  if (!qa_try(data_body, &scope, &caught)) retire(b, viewer);
}
static void viewer_data_body(qa_q2_mvd_broadcast *b, qa_q2_mvd_viewer *viewer,
                             const uint8_t *bytes, size_t length) {
  request_ctx ctx = {b, viewer};
  qa_q2_mvd_framer_push(&viewer->frames, bytes, length, request_message, &ctx);
  if (viewer->closed) return;
  if (!viewer->identified && qa_q2_mvd_framer_identified(&viewer->frames)) {
    viewer->identified = true;
    size_t magic_length = 0;
    uint8_t *magic = qa_q2_mvd_magic(&magic_length);
    viewer_send_owned(b, viewer, magic, magic_length);
  }
  if (qa_q2_mvd_framer_finished(&viewer->frames)) retire(b, viewer);
}

size_t qa_q2_mvd_broadcast_poll(qa_q2_mvd_broadcast *b,
                                double wait_milliseconds) {
  if (b->closed) return 0;
  sweep_closed(b);
  size_t slots = b->viewer_count + (b->listener_fd >= 0 ? 1 : 0);
  double now = monotonic_ms();
  for (size_t i = 0; i < b->viewer_count; i++)
    if (!b->viewers[i].authorized && now >= b->viewers[i].deadline_ms) retire(b, &b->viewers[i]);
  sweep_closed(b);
  slots = b->viewer_count + (b->listener_fd >= 0 ? 1 : 0);
  if (slots == 0) return 0;
  struct pollfd *items = (struct pollfd *)calloc(slots, sizeof *items);
  if (items == NULL) return 0;
  size_t base = 0;
  if (b->listener_fd >= 0) {
    items[0].fd = b->listener_fd;
    items[0].events = POLLIN;
    base = 1;
  }
  for (size_t i = 0; i < b->viewer_count; i++) {
    items[base + i].fd = b->viewers[i].socket_fd;
    items[base + i].events = POLLIN;
  }
  /* poll() needs an int: NaN and negatives wait 0, huge waits saturate. */
  int wait = !(wait_milliseconds > 0) ? 0 : wait_milliseconds >= 2147483647.0 ? 2147483647 : (int)wait_milliseconds;
  int ready = poll(items, (nfds_t)slots, wait);
  size_t serviced = 0;
  if (ready > 0) {
    if (base == 1 && (items[0].revents & POLLIN) != 0) {
      for (;;) {
        int fd = accept(b->listener_fd, NULL, NULL);
        if (fd < 0) break;
        attach(b, fd);
        serviced++;
      }
    }
    size_t count = slots - base;
    for (size_t i = 0; i < count; i++) {
      /* Viewers may have been added by attach; index by the fd list
       * captured at poll time and re-resolve against live viewers. */
      int fd = items[base + i].fd;
      short revents = items[base + i].revents;
      if (revents == 0) continue;
      qa_q2_mvd_viewer *viewer = NULL;
      for (size_t k = 0; k < b->viewer_count; k++) {
        if (b->viewers[k].socket_fd == fd) viewer = &b->viewers[k];
      }
      if (viewer == NULL || viewer->closed) continue;
      serviced++;
      if ((revents & (POLLERR | POLLHUP | POLLNVAL)) != 0 &&
          (revents & POLLIN) == 0) {
        retire(b, viewer);
        continue;
      }
      uint8_t buffer[65536];
      ssize_t n = recv(fd, buffer, sizeof buffer, MSG_DONTWAIT);
      if (n <= 0) {
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
          continue;
        }
        retire(b, viewer);
        continue;
      }
      viewer_data(b, viewer, buffer, (size_t)n);
    }
  }
  free(items);
  sweep_closed(b);
  return serviced;
}

size_t qa_q2_mvd_broadcast_viewer_count(const qa_q2_mvd_broadcast *b) {
  size_t count = 0;
  for (size_t i = 0; i < b->viewer_count; i++) {
    if (!b->viewers[i].closed) count++;
  }
  return count;
}

void qa_q2_mvd_broadcast_close(qa_q2_mvd_broadcast *b) {
  if (b->closed) return;
  b->closed = true;
  if (b->has_recording) qa_q2_mvd_recording_close(&b->recording);
  for (size_t i = 0; i < b->viewer_count; i++) retire(b, &b->viewers[i]);
  sweep_closed(b);
  if (b->listener_fd >= 0) {
    (void)close(b->listener_fd);
    b->listener_fd = -1;
  }
}
void qa_q2_mvd_broadcast_destroy(qa_q2_mvd_broadcast *b) {
  qa_q2_mvd_broadcast_close(b);
  free(b->viewers);
  free_latest(b);
  qa_q2_mvd_encoder_destroy(&b->encoder);
  memset(b, 0, sizeof *b);
  b->listener_fd = -1;
}

