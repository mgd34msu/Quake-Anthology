/* Ports of tests/network/q2/gtv.test.ts, gtv-transport.test.ts, mvd-recording.test.ts and
 * solid.test.ts. GPL-2.0-or-later. */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <zlib.h>

#include "qa/foundation/support.h"
#include "qa/network/q2/codecs/mvd.h"
#include "qa/network/q2/gtv.h"
#include "qa/network/q2/gtv_transport.h"
#include "qa/network/q2/mvd_recording.h"
#include "qa/network/q2/solid.h"

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
  fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

typedef struct buffer { uint8_t *bytes; size_t length; } buffer;
static void buffer_add(buffer *b, const uint8_t *bytes, size_t length) {
  b->bytes = realloc(b->bytes, b->length + length + 1);
  memcpy(b->bytes + b->length, bytes, length);
  b->length += length;
}
static void buffer_take(buffer *b, uint8_t *bytes, size_t length) {
  buffer_add(b, bytes, length);
  free(bytes);
}

/* The sent-chunk queue of the TS test's send closure, with a sentinel ctx. */
typedef struct sent_queue { int sentinel; buffer chunks[32]; size_t count, head; } sent_queue;
static void queue_send(void *ctx, const uint8_t *bytes, size_t length) {
  sent_queue *q = ctx;
  CHECK(q->sentinel == 0x6e7);
  if (q->count < 32) {
    q->chunks[q->count].bytes = NULL;
    q->chunks[q->count].length = 0;
    buffer_add(&q->chunks[q->count++], bytes, length);
  }
}
static buffer queue_shift(sent_queue *q) {
  buffer empty = {NULL, 0};
  return q->head < q->count ? q->chunks[q->head++] : empty;
}

typedef struct event_list { qa_q2_gtv_event items[64]; size_t count; } event_list;
static void receive_into(qa_q2_gtv_client *client, const uint8_t *bytes, size_t length, event_list *out) {
  qa_q2_gtv_event *events = NULL;
  size_t count = 0;
  CHECK(qa_q2_gtv_client_receive(client, bytes, length, &events, &count));
  for (size_t i = 0; i < count && out->count < 64; i++) {
    out->items[out->count] = events[i];
    memset(&events[i], 0, sizeof events[i]);
    out->count++;
  }
  qa_q2_gtv_events_free(events, count);
}
static void events_clear(event_list *list) {
  qa_q2_gtv_events_free(NULL, 0);
  for (size_t i = 0; i < list->count; i++) {
    free(list->items[i].bytes);
    free(list->items[i].reason);
  }
  list->count = 0;
}
static uint8_t *server_message(qa_q2_gtv_server_stream *server, int op, const uint8_t *body, size_t n, size_t *length) {
  uint8_t *bytes = qa_q2_gtv_server_stream_message(server, op, body, n, length);
  CHECK(bytes != NULL);
  return bytes;
}
typedef struct request_list { int sentinel; qa_q2_gtv_request items[4]; size_t count; } request_list;
static bool collect_request(void *ctx, const uint8_t *bytes, size_t length) {
  request_list *list = ctx;
  CHECK(list->sentinel == 0x51);
  if (list->count < 4) CHECK(qa_q2_read_gtv_request(bytes, length, &list->items[list->count++]));
  return true;
}

static void test_gtv_session(void) {
  sent_queue sent;
  memset(&sent, 0, sizeof sent);
  sent.sentinel = 0x6e7;
  qa_q2_gtv_identity identity = {"viewer", "secret", "test"};
  qa_q2_gtv_client client;
  CHECK(qa_q2_gtv_client_init(&client, queue_send, &sent, identity, -1));
  qa_q2_gtv_server_stream server;
  qa_q2_gtv_server_stream_init(&server);
  qa_q2_gtv_client_start(&client);
  size_t magic_length = 0;
  uint8_t *magic = qa_q2_mvd_magic(&magic_length);
  buffer first = queue_shift(&sent);
  CHECK(first.length == magic_length && memcmp(first.bytes, magic, magic_length) == 0);
  free(first.bytes);
  event_list events = {{{0}}, 0};
  for (size_t i = 0; i < magic_length; i++) receive_into(&client, magic + i, 1, &events);
  CHECK(events.count == 0);
  request_list requests;
  memset(&requests, 0, sizeof requests);
  requests.sentinel = 0x51;
  qa_q2_mvd_message_framer framer;
  qa_q2_mvd_framer_init(&framer, false, 256);
  buffer hello_request = queue_shift(&sent);
  qa_q2_mvd_framer_push(&framer, hello_request.bytes, hello_request.length, collect_request, &requests);
  free(hello_request.bytes);
  qa_q2_mvd_framer_destroy(&framer);
  CHECK(requests.count == 1 && requests.items[0].kind == QA_Q2_GTV_REQUEST_HELLO);
  CHECK(strcmp(requests.items[0].hello.username, "viewer") == 0);
  CHECK(strcmp(requests.items[0].hello.password, "secret") == 0);
  CHECK(strcmp(requests.items[0].hello.version, "test") == 0 && requests.items[0].hello.flags == 3);

  size_t hello_length = 0, pong_length = 0;
  uint8_t *hello = qa_q2_gtv_server_stream_hello(&server, 3, &hello_length);
  uint8_t *pong = server_message(&server, QA_Q2_GTS_PONG, NULL, 0, &pong_length);
  buffer joined = {NULL, 0};
  buffer_take(&joined, hello, hello_length);
  buffer_take(&joined, pong, pong_length);
  receive_into(&client, joined.bytes, joined.length, &events);
  free(joined.bytes);
  CHECK(events.count == 2 && events.items[0].kind == QA_Q2_GTV_EVENT_HELLO && events.items[0].flags == 3 &&
        events.items[1].kind == QA_Q2_GTV_EVENT_PONG);
  events_clear(&events);

  qa_q2_gtv_client_request_start(&client, -1);
  size_t started_length = 0, first_length = 0, second_length = 0;
  uint8_t *started = server_message(&server, QA_Q2_GTS_STREAM_START, NULL, 0, &started_length);
  const char *unit = "repeated native dictionary payload ";
  size_t payload_length = strlen(unit) * 100;
  uint8_t *payload = malloc(payload_length);
  for (int i = 0; i < 100; i++) memcpy(payload + i * strlen(unit), unit, strlen(unit));
  uint8_t *first_data = server_message(&server, QA_Q2_GTS_STREAM_DATA, payload, payload_length, &first_length);
  uint8_t *second_data = server_message(&server, QA_Q2_GTS_STREAM_DATA, payload, payload_length, &second_length);
  CHECK(second_length < first_length);
  /* Only the shared dictionary can expand the second packet on its own. */
  uint8_t scratch[8192];
  uLongf scratch_length = sizeof scratch;
  CHECK(uncompress(scratch, &scratch_length, second_data, (uLong)second_length) != Z_OK);
  buffer stream = {NULL, 0};
  buffer_take(&stream, started, started_length);
  buffer_take(&stream, first_data, first_length);
  buffer_take(&stream, second_data, second_length);
  for (size_t i = 0; i < stream.length; i++) receive_into(&client, stream.bytes + i, 1, &events);
  free(stream.bytes);
  CHECK(events.count == 3 && events.items[0].kind == QA_Q2_GTV_EVENT_STARTED);
  CHECK(events.count == 3 && events.items[1].kind == QA_Q2_GTV_EVENT_DATA &&
        events.items[1].bytes_length == payload_length && memcmp(events.items[1].bytes, payload, payload_length) == 0);
  CHECK(events.count == 3 && events.items[2].kind == QA_Q2_GTV_EVENT_DATA &&
        events.items[2].bytes_length == payload_length && memcmp(events.items[2].bytes, payload, payload_length) == 0);
  events_clear(&events);

  size_t length = 0;
  uint8_t *bytes = server_message(&server, QA_Q2_GTS_STREAM_DATA, NULL, 0, &length);
  receive_into(&client, bytes, length, &events);
  free(bytes);
  CHECK(events.count == 1 && events.items[0].kind == QA_Q2_GTV_EVENT_SUSPENDED);
  events_clear(&events);
  bytes = server_message(&server, QA_Q2_GTS_STREAM_DATA, payload, payload_length, &length);
  receive_into(&client, bytes, length, &events);
  free(bytes);
  CHECK(events.count == 2 && events.items[0].kind == QA_Q2_GTV_EVENT_RESUMED && events.items[1].kind == QA_Q2_GTV_EVENT_DATA);
  events_clear(&events);
  qa_q2_gtv_client_request_stop(&client);
  bytes = server_message(&server, QA_Q2_GTS_STREAM_DATA, payload, payload_length, &length);
  receive_into(&client, bytes, length, &events);
  free(bytes);
  CHECK(events.count == 0);
  bytes = server_message(&server, QA_Q2_GTS_STREAM_STOP, NULL, 0, &length);
  receive_into(&client, bytes, length, &events);
  free(bytes);
  CHECK(events.count == 1 && events.items[0].kind == QA_Q2_GTV_EVENT_STOPPED);
  events_clear(&events);
  qa_q2_gtv_client_command(&client, "status");
  qa_q2_gtv_client_close(&client);
  qa_q2_gtv_server_stream_close(&server);
  qa_q2_gtv_client_destroy(&client);
  for (size_t i = sent.head; i < sent.count; i++) free(sent.chunks[i].bytes);
  free(payload);
  free(magic);
}

typedef struct call { int kind; qa_q2_gtv_client *client; const uint8_t *bytes; size_t length; } call;
static void call_body(void *arg) {
  call *c = arg;
  if (c->kind == 0) qa_q2_gtv_client_command(c->client, "status");
  else if (c->kind == 1) {
    event_list events = {{{0}}, 0};
    receive_into(c->client, c->bytes, c->length, &events);
    events_clear(&events);
  } else {
    qa_q2_gtv_request request;
    qa_q2_read_gtv_request(c->bytes, c->length, &request);
  }
}
static bool raises_with(call *c, const char *fragment) {
  qa_caught_error caught;
  if (qa_try(call_body, c, &caught)) return false;
  return strstr(caught.message, fragment) != NULL;
}
static void ignore_send(void *ctx, const uint8_t *bytes, size_t length) { (void)ctx; (void)bytes; (void)length; }
static void test_gtv_refusals(void) {
  qa_q2_gtv_identity identity = {"", "", ""};
  qa_q2_gtv_client client;
  CHECK(qa_q2_gtv_client_init(&client, ignore_send, NULL, identity, 0));
  qa_q2_gtv_server_stream server;
  qa_q2_gtv_server_stream_init(&server);
  qa_q2_gtv_client_start(&client);
  size_t magic_length = 0, hello_length = 0;
  uint8_t *magic = qa_q2_mvd_magic(&magic_length);
  event_list events = {{{0}}, 0};
  receive_into(&client, magic, magic_length, &events);
  uint8_t *hello = qa_q2_gtv_server_stream_hello(&server, 0, &hello_length);
  receive_into(&client, hello, hello_length, &events);
  events_clear(&events);
  call command = {0, &client, NULL, 0};
  CHECK(raises_with(&command, "not negotiated"));
  size_t length = 0;
  uint8_t one = 1;
  uint8_t *data = server_message(&server, QA_Q2_GTS_STREAM_DATA, &one, 1, &length);
  call receive = {1, &client, data, length};
  CHECK(raises_with(&receive, "Unexpected"));
  uint8_t big[257] = {0};
  call request = {2, NULL, big, sizeof big};
  CHECK(raises_with(&request, "length"));
  free(data);
  free(hello);
  free(magic);
  qa_q2_gtv_client_close(&client);
  qa_q2_gtv_server_stream_close(&server);
  qa_q2_gtv_client_destroy(&client);
}

/* mvd-recording.test.ts */
static void record_chunk(void *ctx, const uint8_t *bytes, size_t length) { buffer_add(ctx, bytes, length); }
typedef struct messages { int sentinel; qa_q2_mvd_byte_block items[8]; size_t count; } messages;
static bool collect_message(void *ctx, const uint8_t *bytes, size_t length) {
  messages *m = ctx;
  CHECK(m->sentinel == 0x77);
  if (m->count < 8) {
    m->items[m->count].bytes = malloc(length ? length : 1);
    memcpy(m->items[m->count].bytes, bytes, length);
    m->items[m->count++].length = length;
  }
  return true;
}
typedef struct mvd_call { int kind; const uint8_t *bytes; size_t length; qa_q2_mvd_recording *recording; qa_q2_mvd_message_framer *framer; } mvd_call;
static void mvd_call_body(void *arg) {
  mvd_call *c = arg;
  if (c->kind == 0) {
    qa_q2_mvd_byte_block *blocks = NULL;
    size_t count = 0;
    if (qa_q2_read_mvd_recording(c->bytes, c->length, &blocks, &count)) qa_q2_mvd_byte_blocks_free(blocks, count);
  } else if (c->kind == 1) {
    size_t n = 0;
    free(qa_q2_frame_mvd_message(c->bytes, c->length, &n));
  } else if (c->kind == 2) {
    qa_q2_mvd_recording_append(c->recording, c->bytes, c->length);
  } else {
    messages ignored = {0x77, {{0}}, 0};
    qa_q2_mvd_framer_push(c->framer, c->bytes, c->length, collect_message, &ignored);
  }
}
static bool mvd_raises(mvd_call *c, const char *fragment) {
  qa_caught_error caught;
  if (qa_try(mvd_call_body, c, &caught)) return false;
  return strstr(caught.message, fragment) != NULL;
}
static void test_mvd_recording(void) {
  buffer out = {NULL, 0};
  qa_q2_mvd_recording recording;
  qa_q2_mvd_recording_init(&recording, record_chunk, &out);
  const uint8_t a[] = {4, 37, 0, 0, 0}, b[] = {6, 0, 255, 0, 0};
  qa_q2_mvd_recording_append(&recording, a, sizeof a);
  qa_q2_mvd_recording_append(&recording, b, sizeof b);
  qa_q2_mvd_recording_close(&recording);
  qa_q2_mvd_recording_close(&recording);
  const uint8_t prefix[] = {77, 86, 68, 50, 5, 0};
  CHECK(out.length >= 6 && memcmp(out.bytes, prefix, 6) == 0);
  qa_q2_mvd_message_framer framer;
  qa_q2_mvd_framer_init(&framer, true, QA_Q2_MVD_MAX_MESSAGE);
  messages got = {0x77, {{0}}, 0};
  for (size_t i = 0; i < out.length; i++) qa_q2_mvd_framer_push(&framer, out.bytes + i, 1, collect_message, &got);
  qa_q2_mvd_framer_finish(&framer, true);
  qa_q2_mvd_byte_block *blocks = NULL;
  size_t count = 0;
  CHECK(qa_q2_read_mvd_recording(out.bytes, out.length, &blocks, &count));
  CHECK(got.count == 2 && count == 2);
  for (size_t i = 0; i < count && i < got.count; i++)
    CHECK(blocks[i].length == got.items[i].length && memcmp(blocks[i].bytes, got.items[i].bytes, blocks[i].length) == 0);
  qa_q2_mvd_byte_blocks_free(blocks, count);
  mvd_call truncated = {0, out.bytes, out.length - 1, NULL, NULL};
  CHECK(mvd_raises(&truncated, "Truncated"));
  uint8_t *large = calloc(1, 32769);
  mvd_call oversize = {1, large, 32769, NULL, NULL};
  CHECK(mvd_raises(&oversize, "length"));
  free(large);
  uint8_t one = 1;
  mvd_call closed = {2, &one, 1, &recording, NULL};
  CHECK(mvd_raises(&closed, "closed"));
  mvd_call after = {3, &one, 1, NULL, &framer};
  CHECK(mvd_raises(&after, "after"));
  qa_q2_mvd_framer_destroy(&framer);
  for (size_t i = 0; i < got.count; i++) free(got.items[i].bytes);
  free(out.bytes);
}

/* gtv-transport.test.ts: a contained loopback TCP server on its own thread. */
typedef struct tcp_server { int listener, port; bool authenticated; } tcp_server;
static void send_all(int fd, const uint8_t *bytes, size_t length) {
  while (length > 0) {
    ssize_t n = send(fd, bytes, length, MSG_NOSIGNAL);
    if (n <= 0) return;
    bytes += n;
    length -= (size_t)n;
  }
}
typedef struct server_requests { int sentinel; qa_q2_gtv_request items[4]; size_t count; } server_requests;
static bool server_request(void *ctx, const uint8_t *bytes, size_t length) {
  server_requests *r = ctx;
  if (r->count < 4 && qa_q2_read_gtv_request(bytes, length, &r->items[r->count])) r->count++;
  return true;
}
static void *server_thread(void *arg) {
  tcp_server *s = arg;
  int fd = accept(s->listener, NULL, NULL);
  if (fd < 0) return NULL;
  qa_q2_mvd_message_framer frames;
  qa_q2_mvd_framer_init(&frames, true, 256);
  qa_q2_gtv_server_stream stream;
  qa_q2_gtv_server_stream_init(&stream);
  bool done = false;
  uint8_t chunk[4096];
  while (!done) {
    ssize_t n = recv(fd, chunk, sizeof chunk, 0);
    if (n <= 0) break;
    bool before = qa_q2_mvd_framer_identified(&frames);
    server_requests requests = {0, {{0}}, 0};
    qa_q2_mvd_framer_push(&frames, chunk, (size_t)n, server_request, &requests);
    if (!before && qa_q2_mvd_framer_identified(&frames)) {
      size_t magic_length = 0;
      uint8_t *magic = qa_q2_mvd_magic(&magic_length);
      send_all(fd, magic, magic_length);
      free(magic);
    }
    for (size_t i = 0; i < requests.count; i++) {
      qa_q2_gtv_request *request = &requests.items[i];
      if (getenv("GTV_TRACE")) fprintf(stderr, "server request %d\n", (int)request->kind);
      size_t length = 0;
      if (request->kind == QA_Q2_GTV_REQUEST_HELLO) {
        s->authenticated = strcmp(request->hello.username, "viewer") == 0 &&
                           strcmp(request->hello.password, "test-local") == 0;
        if (!s->authenticated) { done = true; break; }
        uint8_t *hello = qa_q2_gtv_server_stream_hello(&stream, (double)((int)request->hello.flags & 3), &length);
        send_all(fd, hello, length);
        free(hello);
      } else if (request->kind == QA_Q2_GTV_REQUEST_START) {
        uint8_t *start = qa_q2_gtv_server_stream_message(&stream, QA_Q2_GTS_STREAM_START, NULL, 0, &length);
        send_all(fd, start, length);
        free(start);
        const uint8_t data[] = {6, 0, 255, 0, 0};
        uint8_t *message = qa_q2_gtv_server_stream_message(&stream, QA_Q2_GTS_STREAM_DATA, data, sizeof data, &length);
        send_all(fd, message, length);
        free(message);
      }
    }
  }
  /* Drain until the client retires the socket. */
  while (recv(fd, chunk, sizeof chunk, 0) > 0) {}
  close(fd);
  qa_q2_gtv_server_stream_close(&stream);
  qa_q2_mvd_framer_destroy(&frames);
  return NULL;
}
typedef struct transport_seen { int sentinel; uint8_t data[16]; size_t length; bool closed; } transport_seen;
static void transport_event(void *ctx, const qa_q2_gtv_event *event) {
  transport_seen *seen = ctx;
  CHECK(seen->sentinel == 0x7c);
  if (getenv("GTV_TRACE")) fprintf(stderr, "client event %d len %zu\n", (int)event->kind, event->bytes_length);
  if (event->kind == QA_Q2_GTV_EVENT_DATA && event->bytes_length <= sizeof seen->data) {
    memcpy(seen->data, event->bytes, event->bytes_length);
    seen->length = event->bytes_length;
  } else if (event->kind == QA_Q2_GTV_EVENT_CLOSED) {
    seen->closed = true;
  }
}
static void test_gtv_transport(void) {
  tcp_server server = {socket(AF_INET, SOCK_STREAM, 0), 0, false};
  struct sockaddr_in address;
  memset(&address, 0, sizeof address);
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  CHECK(bind(server.listener, (struct sockaddr *)&address, sizeof address) == 0);
  CHECK(listen(server.listener, 4) == 0);
  socklen_t address_length = sizeof address;
  getsockname(server.listener, (struct sockaddr *)&address, &address_length);
  server.port = ntohs(address.sin_port);
  pthread_t thread;
  pthread_create(&thread, NULL, server_thread, &server);
  transport_seen seen = {0x7c, {0}, 0, false};
  qa_q2_gtv_connection_options options;
  memset(&options, 0, sizeof options);
  options.host = "127.0.0.1";
  options.port = server.port;
  options.identity = (qa_q2_gtv_identity){"viewer", "test-local", "fixture"};
  options.timeout_milliseconds = 1000;
  options.event = transport_event;
  options.event_ctx = &seen;
  qa_q2_gtv_connection connection;
  CHECK(qa_q2_gtv_connection_connect(&connection, options));
  qa_q2_gtv_connection_start(&connection, -1);
  for (int i = 0; i < 100 && seen.length == 0 && !seen.closed; i++) qa_q2_gtv_connection_poll(&connection, 20);
  const uint8_t want[] = {6, 0, 255, 0, 0};
  if (seen.length != sizeof want)
    fprintf(stderr, "gtv transport: %zu data bytes, closed %d, authenticated %d\n", seen.length,
            (int)seen.closed, (int)server.authenticated);
  CHECK(seen.length == sizeof want && memcmp(seen.data, want, sizeof want) == 0);
  CHECK(server.authenticated);
  qa_q2_gtv_connection_close(&connection);
  CHECK(qa_q2_gtv_connection_get_closed(&connection));
  pthread_join(thread, NULL);
  qa_q2_gtv_connection_destroy(&connection);
  close(server.listener);
}

/* solid.test.ts (the parts tests/network/test_network_q2.c leaves out) */
static void test_solid(void) {
  qa_bounds r1 = qa_q2_unpack_solid(0x80201810u, QA_Q2_SOLID_R1Q2);
  CHECK(r1.min.x == -16 && r1.min.y == -16 && r1.min.z == -24 && r1.max.x == 16 && r1.max.y == 16 && r1.max.z == 32);
  qa_bounds classic = qa_q2_unpack_solid(8290, QA_Q2_SOLID_SHORT);
  CHECK(classic.min.x == -16 && classic.min.y == -16 && classic.min.z == -24 && classic.max.x == 16 &&
        classic.max.y == 16 && classic.max.z == 32);
  qa_bounds clamped = qa_q2_unpack_solid(
      (double)qa_q2_pack_solid((qa_bounds){{-900, -800, -300}, {900, 800, -100}}, QA_Q2_SOLID_Q2PRO_V2),
      QA_Q2_SOLID_Q2PRO_V2);
  CHECK(clamped.min.x == -255 && clamped.min.y == -255 && clamped.min.z == -255 && clamped.max.x == 255 &&
        clamped.max.y == 255 && clamped.max.z == -32);
  qa_q2_solid_encoding encoding;
  qa_q2_protocol_identity private_classic = {"q2-private-classic", 4038, 0, 0};
  CHECK(qa_q2_get_solid_encoding(private_classic, false, &encoding) && encoding == QA_Q2_SOLID_Q2PRO_V2);
  qa_q2_protocol_identity rerelease = {"q2-rerelease", 1038, 0, 0};
  CHECK(qa_q2_get_solid_encoding(rerelease, false, &encoding) && encoding == QA_Q2_SOLID_Q2PRO_V2);
}

int main(void) {
  test_gtv_session();
  test_gtv_refusals();
  test_mvd_recording();
  test_gtv_transport();
  test_solid();
  printf("q2 gtv/mvd: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
