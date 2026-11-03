/* RemoteApplication over loopback UDP against a C server, compared with the TS RemoteApplication.
 *
 * For each family (q1 NetQuake, qw, q2, q3, unified, unified over the mixed q2-q1-q3 preset, and clients whose profile
 * differs from the unified server's recipe: Q2 and Q3 profiles on the Q1 server, a Q1 profile on a Q2 server) the C
 * server is the real Application (qa_application, application.ts) opened --dedicated with its native listener on
 * 127.0.0.1 (as the TS network tests open theirs). The client connects through a UDP relay in this process, which
 * records every datagram the client sends. Two clients are run against a fresh server each: the C
 * qa_remote_application in this process, and the TS RemoteApplication under bun (tests/app/oracles/remote_loopback.ts)
 * driven over a pipe in lockstep. Each run's transcript holds the client's datagrams through signon, the first
 * snapshots the client decodes, its network phase changes and its console output; the two transcripts are compared
 * line by line.
 *
 * Wall clock and randomness are fixed on both sides: this binary defines clock_gettime(CLOCK_MONOTONIC) as a virtual
 * clock the harness advances, and getrandom as a fixed byte pattern; the oracle replaces performance.now and
 * crypto.getRandomValues with the same clock and pattern. The C server shares this process's clock, so in both runs it
 * sees the same times and the same random values.
 *
 *   test_remote_loopback                 every family
 *   test_remote_loopback <family>        one family (q1, qw, q2, q3, unified, unified-mixed, unified-cross,
 *                                        unified-cross-q1, unified-cross-q3)
 *   test_remote_loopback -v [family]     also print both transcripts
 * REMOTE_LOOPBACK_BUN=0 skips the TS run. The corpus is QTS_TEST_CORPUS (default ~/qfiles); user content roots are
 * $TMPDIR/remote-loopback-{server,client}. GPL-2.0-or-later. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "qa/app/bootstrap/application_class.h"
#include "qa/app/bootstrap/options.h"
#include "qa/app/bootstrap/remote_application_class.h"
#include "qa/foundation/support.h"
#include "qa/network/common/endpoint.h"
#include "qa/network/common/js_number.h"

static int checks, failures;
static bool verbose;
#define CHECK(cond)                                                     \
  do {                                                                  \
    checks++;                                                           \
    if (!(cond)) {                                                      \
      failures++;                                                       \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
    }                                                                   \
  } while (0)

/* ---- the virtual clock and the fixed random pattern ---------------------------------------------------------------- */
/* Milliseconds; both sides start at the same value and advance only through advance_clock. */
static double virtual_now = 1000;
static bool virtual_clock;
int clock_gettime(clockid_t id, struct timespec *ts) {
  if (virtual_clock && (id == CLOCK_MONOTONIC || id == CLOCK_MONOTONIC_RAW || id == CLOCK_BOOTTIME)) {
    double whole = (double)(int64_t)(virtual_now / 1000.0);
    ts->tv_sec = (time_t)whole;
    ts->tv_nsec = (long)((virtual_now - whole * 1000.0) * 1e6);
    return 0;
  }
  return (int)syscall(SYS_clock_gettime, id, ts);
}
/* Byte i of every request is 0x5a + 7*i (mod 256): stateless, so callers in any order see the same values. */
ssize_t getrandom(void *buffer, size_t length, unsigned int flags) {
  (void)flags;
  uint8_t *bytes = buffer;
  for (size_t i = 0; i < length; i++) bytes[i] = (uint8_t)(0x5a + 7 * i);
  return (ssize_t)length;
}
static void advance_clock(double milliseconds) { virtual_now += milliseconds; }

/* ---- text helpers -------------------------------------------------------------------------------------------------- */
typedef struct text { char *bytes; size_t length, capacity; } text;
static void text_append_n(text *t, const char *value, size_t n) {
  if (t->length + n + 1 > t->capacity) {
    size_t capacity = t->capacity == 0 ? 4096 : t->capacity;
    while (capacity < t->length + n + 1) capacity *= 2;
    t->bytes = realloc(t->bytes, capacity);
    if (t->bytes == NULL) abort();
    t->capacity = capacity;
  }
  memcpy(t->bytes + t->length, value, n);
  t->length += n;
  t->bytes[t->length] = '\0';
}
static void text_append(text *t, const char *value) { text_append_n(t, value, strlen(value)); }
static void text_appendf(text *t, const char *format, ...) __attribute__((format(printf, 2, 3)));
static void text_appendf(text *t, const char *format, ...) {
  char line[8192];
  va_list args;
  va_start(args, format);
  vsnprintf(line, sizeof line, format, args);
  va_end(args);
  text_append(t, line);
}
static void text_free(text *t) { free(t->bytes); memset(t, 0, sizeof *t); }
#define N(value) QA_JS_NUMBER(value)

/* ---- hosts (sentinel contexts) ------------------------------------------------------------------------------------- */
typedef struct host_state { unsigned magic; text console; } host_state;
#define SERVER_MAGIC 0x5e4e4e41u
#define CLIENT_MAGIC 0xc11e4e7bu
static host_state *host_of(void *ctx, unsigned magic) {
  host_state *state = ctx;
  if (state == NULL || state->magic != magic) abort();
  return state;
}
static void server_print(void *ctx, const char *value) { text_append(&host_of(ctx, SERVER_MAGIC)->console, value); }
static void client_print(void *ctx, const char *value) { text_append(&host_of(ctx, CLIENT_MAGIC)->console, value); }

static const char *corpus_root(void) {
  const char *root = getenv("QTS_TEST_CORPUS");
  if (root != NULL && root[0] != '\0') return root;
  static char home[512];
  snprintf(home, sizeof home, "%s/qfiles", getenv("HOME") != NULL ? getenv("HOME") : "");
  return home;
}
static void user_root(const char *name, char *out, size_t capacity) {
  const char *tmp = getenv("TMPDIR");
  snprintf(out, capacity, "%s/remote-loopback-%s", tmp != NULL && tmp[0] != '\0' ? tmp : "/tmp", name);
}
static void wipe(const char *path) {
  char command[1200];
  snprintf(command, sizeof command, "rm -rf '%s'", path);
  if (system(command) != 0 || mkdir(path, 0777) != 0) { perror(path); abort(); }
}

/* ---- families ------------------------------------------------------------------------------------------------------ */
typedef struct family_case {
  const char *name;
  const char *server[12]; size_t server_count;  /* after the content roots, before --dedicated */
  const char *listen;                           /* --listen / --listen-q2 / --listen-unified */
  const char *client[12]; size_t client_count;  /* after the content roots, before --connect-* */
  const char *connect;
} family_case;
static const family_case families[] = {
  {"q1", {"--game", "q1-classic-id1", "--map", "e1m1", "--movement", "q1", "--character", "q1"}, 8, "--listen",
      {"--game", "q1-classic-id1", "--map", "e1m1", "--movement", "q1", "--character", "q1"}, 8, "--connect-q1"},
  {"qw", {"--game", "q1-quakeworld", "--map", "e1m1", "--movement", "q1", "--character", "q1"}, 8, "--listen",
      {"--game", "q1-quakeworld", "--map", "e1m1", "--movement", "q1", "--character", "q1"}, 8, "--connect-qw"},
  {"q2", {"--game", "q2-classic-baseq2", "--map", "base1", "--movement", "q2", "--character", "q2", "--mode", "coop"}, 10,
      "--listen-q2", {"--game", "q2-classic-baseq2", "--movement", "q2", "--character", "q2", "--mode", "coop"}, 8,
      "--connect-q2"},
  {"q3", {"--game", "q3-baseq3", "--map", "q3dm1", "--movement", "q3", "--character", "q3", "--mode", "deathmatch"}, 10,
      "--listen", {"--game", "q3-baseq3", "--map", "q3dm1", "--movement", "q3", "--character", "q3", "--mode", "deathmatch"},
      10, "--connect-q3"},
  {"unified", {"--game", "q1-classic-id1", "--map", "e1m1", "--movement", "q1", "--character", "q1"}, 8, "--listen-unified",
      {"--game", "q1-classic-id1", "--map", "e1m1", "--movement", "q1", "--character", "q1"}, 8, "--connect-unified"},
  {"unified-mixed", {"--preset", "q2-q1-q3"}, 2, "--listen-unified", {"--preset", "q2-q1-q3"}, 2, "--connect-unified"},
  /* Clients whose profile differs from the served recipe: adoptUnifiedProfile takes the served recipe's profile over
   * the loaded content's mounts (the profile-change path). A Q2 profile against the Q1 unified server, a Q1 profile
   * against a Q2 unified server, a Q3 profile against the Q1 unified server. */
  {"unified-cross", {"--game", "q1-classic-id1", "--map", "e1m1", "--movement", "q1", "--character", "q1"}, 8,
      "--listen-unified", {"--game", "q2-classic-baseq2", "--movement", "q2", "--character", "q2"}, 6, "--connect-unified"},
  {"unified-cross-q1", {"--game", "q2-classic-baseq2", "--map", "base1", "--movement", "q2", "--character", "q2", "--mode",
      "coop"}, 10, "--listen-unified", {"--game", "q1-classic-id1", "--movement", "q1", "--character", "q1"}, 6,
      "--connect-unified"},
  {"unified-cross-q3", {"--game", "q1-classic-id1", "--map", "e1m1", "--movement", "q1", "--character", "q1"}, 8,
      "--listen-unified", {"--game", "q3-baseq3", "--movement", "q3", "--character", "q3"}, 6, "--connect-unified"},
};

#define FRAME_MILLISECONDS 50
#define MAX_EXCHANGES 200
#define SNAPSHOTS 4

/* ---- the UDP relay ------------------------------------------------------------------------------------------------- */
/* A lane per server port: the client talks to the lane's front socket, the relay forwards from its one back socket to
 * that server port. A NetQuake accept names the server's per-client port; the relay opens a lane for it and rewrites
 * the accepted port to the lane's front port (the only payload it changes), so later datagrams pass through it too. */
#define RELAY_LANES 8
typedef struct relay_lane { int front; uint16_t front_port, server_port; struct sockaddr_in client; bool has_client; } relay_lane;
typedef struct relay { relay_lane lanes[RELAY_LANES]; size_t lane_count; int back; uint16_t port, back_port; } relay;
/* *out_port 0 binds any port; otherwise that port (the back socket of the second run of a family). */
static int bind_loopback(uint16_t *out_port) {
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) { perror("socket"); abort(); }
  struct sockaddr_in address = {0};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(out_port != NULL ? *out_port : 0);
  if (bind(fd, (struct sockaddr *)&address, sizeof address) != 0) { perror("bind"); abort(); }
  socklen_t length = sizeof address;
  getsockname(fd, (struct sockaddr *)&address, &length);
  if (out_port != NULL) *out_port = ntohs(address.sin_port);
  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
  return fd;
}
static relay_lane *relay_lane_for(relay *r, uint16_t server_port) {
  for (size_t i = 0; i < r->lane_count; i++) if (r->lanes[i].server_port == server_port) return &r->lanes[i];
  if (r->lane_count == RELAY_LANES) abort();
  relay_lane *lane = &r->lanes[r->lane_count++];
  memset(lane, 0, sizeof *lane);
  lane->front_port = 0;
  lane->front = bind_loopback(&lane->front_port);
  lane->server_port = server_port;
  return lane;
}
/* back_port: 0 for any port, or the port the family's other run used. The server sees every client datagram from
 * the back socket, and a unified server's connect token is an HMAC over that address, so both runs of a family send
 * from the same port. */
static void relay_open(relay *r, uint16_t server_port, uint16_t back_port) {
  memset(r, 0, sizeof *r);
  r->back_port = back_port;
  r->back = bind_loopback(&r->back_port);
  r->port = relay_lane_for(r, server_port)->front_port;
}
static void relay_close(relay *r) {
  for (size_t i = 0; i < r->lane_count; i++) close(r->lanes[i].front);
  close(r->back);
}
static void hex_line(text *transcript, const char *label, const uint8_t *bytes, size_t n) {
  text_append(transcript, label);
  for (size_t i = 0; i < n; i++) text_appendf(transcript, "%02x", bytes[i]);
  text_append(transcript, "\n");
}
/* Client -> server: each datagram is forwarded and, while recording, written as "send <hex>". */
static size_t relay_upstream(relay *r, text *transcript, bool record) {
  uint8_t buffer[65536];
  size_t count = 0;
  for (size_t i = 0; i < r->lane_count; i++) {
    relay_lane *lane = &r->lanes[i];
    for (;;) {
      struct sockaddr_in from;
      socklen_t length = sizeof from;
      ssize_t n = recvfrom(lane->front, buffer, sizeof buffer, 0, (struct sockaddr *)&from, &length);
      if (n < 0) break;
      lane->client = from; lane->has_client = true;
      struct sockaddr_in server = {0};
      server.sin_family = AF_INET;
      server.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
      server.sin_port = htons(lane->server_port);
      sendto(r->back, buffer, (size_t)n, 0, (struct sockaddr *)&server, sizeof server);
      count++;
      if (record) hex_line(transcript, i == 0 ? "send " : "send+ ", buffer, (size_t)n);
    }
  }
  return count;
}
/* Server -> client: written as "recv <length>". */
static void relay_downstream(relay *r, text *transcript, bool record) {
  uint8_t buffer[65536];
  for (;;) {
    struct sockaddr_in from;
    socklen_t length = sizeof from;
    ssize_t n = recvfrom(r->back, buffer, sizeof buffer, 0, (struct sockaddr *)&from, &length);
    if (n < 0) break;
    relay_lane *lane = relay_lane_for(r, ntohs(from.sin_port));
    if (n == 9 && buffer[0] == 0x80 && buffer[1] == 0 && buffer[2] == 0 && buffer[3] == 9 && buffer[4] == 0x81) {
      uint32_t port = (uint32_t)buffer[5] | (uint32_t)buffer[6] << 8 | (uint32_t)buffer[7] << 16 | (uint32_t)buffer[8] << 24;
      relay_lane *accepted = relay_lane_for(r, (uint16_t)port);
      accepted->client = lane->client; accepted->has_client = lane->has_client;
      if (record) text_appendf(transcript, "accept %s\n", (uint16_t)port == lane->server_port ? "same-port" : "new-port");
      buffer[5] = (uint8_t)accepted->front_port; buffer[6] = (uint8_t)(accepted->front_port >> 8); buffer[7] = 0; buffer[8] = 0;
    }
    if (lane->has_client) sendto(lane->front, buffer, (size_t)n, 0, (struct sockaddr *)&lane->client, sizeof lane->client);
    if (record) text_appendf(transcript, "recv %zd\n", n);
  }
}

/* ---- the C server -------------------------------------------------------------------------------------------------- */
typedef struct server_run { const family_case *spec; host_state host; qa_application *app; qa_app_application_command command;
  char root[1024]; const char *argv[24]; } server_run;
static void server_open_body(void *ctx) {
  server_run *run = ctx;
  size_t argc = 0;
  run->argv[argc++] = "--content-root"; run->argv[argc++] = corpus_root();
  run->argv[argc++] = "--user-content-root"; run->argv[argc++] = run->root;
  for (size_t i = 0; i < run->spec->server_count; i++) run->argv[argc++] = run->spec->server[i];
  run->argv[argc++] = "--dedicated";
  run->argv[argc++] = run->spec->listen; run->argv[argc++] = "0";
  run->argv[argc++] = "--bind"; run->argv[argc++] = "127.0.0.1";
  qa_content_root_search search = {corpus_root(), NULL};
  run->command = qa_app_parse_application_command(run->argv, argc, search, NULL);
  if (strcmp(run->command.kind, "run") != 0) qa_raise("Error", 0, 0, "server: expected a run command");
  qa_application_host host;
  memset(&host, 0, sizeof host);
  host.ctx = &run->host;
  host.print = server_print;
  run->app = qa_application_open(run->command.options, host, NULL, NULL, NULL);
}
static void server_step_body(void *ctx) { (void)qa_application_step(ctx, FRAME_MILLISECONDS); }
static void server_close_body(void *ctx) {
  server_run *run = ctx;
  if (run->app != NULL) { qa_application_close(run->app); qa_application_destroy(run->app); run->app = NULL; }
}

/* ---- the transcript of a client step's output ---------------------------------------------------------------------- */
static void dump_output(text *t, const qa_simulation_output *output) {
  const qa_world_snapshot *s = &output->snapshot;
  text_appendf(t, "snapshot frame %s time %s actors %zu bodies %zu inventories %zu configurations %zu entities %zu lights %zu events %zu\n",
      N(s->frame.frame), N(s->frame.time.value), s->actor_count, s->body_count, s->inventory_count,
      s->configuration_count, s->scene.entity_count, s->scene.light_count, output->event_count);
  for (size_t i = 0; i < s->actor_count; i++)
    text_appendf(t, "actor %lld:%lld %s\n", (long long)s->actors[i].id.slot, (long long)s->actors[i].id.generation,
        s->actors[i].definition != NULL ? s->actors[i].definition : "-");
  for (size_t i = 0; i < s->body_count; i++) {
    const qa_body_state *b = &s->bodies[i].body;
    text_appendf(t, "body %lld origin %s %s %s angles %s %s %s velocity %s %s %s\n", (long long)s->bodies[i].actor.slot,
        N(b->origin.x), N(b->origin.y), N(b->origin.z), N(b->angles.x), N(b->angles.y), N(b->angles.z),
        N(b->velocity.x), N(b->velocity.y), N(b->velocity.z));
  }
  for (size_t i = 0; i < output->event_count; i++) {
    static const char *const kinds[] = {"sound", "damage", "transition", "message"};
    text_appendf(t, "event %s %s\n", N(output->events[i].sequence), kinds[output->events[i].payload.kind]);
  }
}
/* Moves the complete console lines of host into the transcript as "console <line>". */
static void flush_console(text *t, host_state *host, bool all) {
  char *cursor = host->console.bytes;
  if (cursor == NULL) return;
  size_t consumed = 0;
  for (;;) {
    char *end = strchr(cursor + consumed, '\n');
    if (end == NULL) break;
    text_append(t, "console ");
    text_append_n(t, cursor + consumed, (size_t)(end - (cursor + consumed)));
    text_append(t, "\n");
    consumed = (size_t)(end - cursor) + 1;
  }
  if (all && consumed < host->console.length) {
    text_appendf(t, "console %s\n", cursor + consumed);
    consumed = host->console.length;
  }
  memmove(cursor, cursor + consumed, host->console.length - consumed + 1);
  host->console.length -= consumed;
}

/* ---- client drivers ------------------------------------------------------------------------------------------------ */
typedef struct client_driver client_driver;
struct client_driver {
  bool (*open)(client_driver *driver, const char *const *argv, size_t argc, text *transcript);
  /* Steps the client; appends its console lines and output dump; *phase receives the network phase. */
  bool (*step)(client_driver *driver, text *transcript, char *phase, size_t phase_capacity, bool *has_output);
  void (*close)(client_driver *driver, text *transcript);
};

/* The C RemoteApplication. */
typedef struct c_client { client_driver base; host_state host; qa_remote_application_host remote_host;
  qa_remote_application *app; qa_app_application_command command; const char *const *argv; size_t argc;
  text *transcript; char *phase; size_t phase_capacity; bool has_output; } c_client;
static void c_open_body(void *ctx) {
  c_client *c = ctx;
  qa_content_root_search search = {corpus_root(), NULL};
  c->command = qa_app_parse_application_command(c->argv, c->argc, search, NULL);
  if (strcmp(c->command.kind, "run") != 0) qa_raise("Error", 0, 0, "client: expected a run command");
  memset(&c->remote_host, 0, sizeof c->remote_host);
  c->remote_host.base.ctx = &c->host;
  c->remote_host.base.print = client_print;
  c->app = qa_remote_application_open(&c->command.options, &c->remote_host);
}
static bool c_report(c_client *c, const char *operation, const qa_caught_error *caught, text *transcript) {
  flush_console(transcript, &c->host, true);
  text_appendf(transcript, "error %s %s: %s\n", operation, caught->name, caught->message);
  return false;
}
static bool c_open(client_driver *driver, const char *const *argv, size_t argc, text *transcript) {
  c_client *c = (c_client *)driver;
  c->host.magic = CLIENT_MAGIC;
  c->argv = argv; c->argc = argc;
  qa_caught_error caught;
  bool ok = qa_try(c_open_body, c, &caught);
  if (!ok) return c_report(c, "open", &caught, transcript);
  flush_console(transcript, &c->host, false);
  return true;
}
static void c_step_body(void *ctx) {
  c_client *c = ctx;
  qa_simulation_output output;
  c->has_output = qa_remote_application_step(c->app, FRAME_MILLISECONDS, &output);
  flush_console(c->transcript, &c->host, false);
  const char *phase = qa_remote_application_network_phase(c->app);
  snprintf(c->phase, c->phase_capacity, "%s", phase != NULL ? phase : "null");
  if (c->has_output) dump_output(c->transcript, &output);
}
static bool c_step(client_driver *driver, text *transcript, char *phase, size_t phase_capacity, bool *has_output) {
  c_client *c = (c_client *)driver;
  c->transcript = transcript; c->phase = phase; c->phase_capacity = phase_capacity;
  qa_caught_error caught;
  if (!qa_try(c_step_body, c, &caught)) return c_report(c, "step", &caught, transcript);
  *has_output = c->has_output;
  return true;
}
static void c_close_body(void *ctx) {
  c_client *c = ctx;
  qa_remote_application_close(c->app);
}
static void c_close(client_driver *driver, text *transcript) {
  c_client *c = (c_client *)driver;
  if (c->app != NULL) {
    qa_caught_error caught;
    if (!qa_try(c_close_body, c, &caught)) (void)c_report(c, "close", &caught, transcript);
    qa_remote_application_destroy(c->app);
    c->app = NULL;
  }
  flush_console(transcript, &c->host, true);
  qa_app_application_command_destroy(&c->command);
  text_free(&c->host.console);
}

/* The TS RemoteApplication under bun, driven over pipes: "open", "step <ms> <now>", "close"; each answer ends "done". */
typedef struct ts_client { client_driver base; pid_t pid; FILE *in, *out; } ts_client;
static bool ts_request(ts_client *c, const char *request, text *transcript, char *phase, size_t phase_capacity,
    bool *has_output) {
  fputs(request, c->in);
  fflush(c->in);
  char line[1 << 16];
  bool ok = true;
  while (fgets(line, sizeof line, c->out) != NULL) {
    if (strcmp(line, "done\n") == 0) return ok;
    if (strncmp(line, "phase ", 6) == 0) {
      if (phase != NULL) { snprintf(phase, phase_capacity, "%s", line + 6); phase[strcspn(phase, "\n")] = '\0'; }
      continue;
    }
    if (strncmp(line, "snapshot ", 9) == 0 && has_output != NULL) *has_output = true;
    if (strncmp(line, "error ", 6) == 0) ok = false;
    text_append(transcript, line);
  }
  text_append(transcript, "error bun exited\n");
  return false;
}
static bool ts_open(client_driver *driver, const char *const *argv, size_t argc, text *transcript) {
  ts_client *c = (ts_client *)driver;
  int to_child[2], from_child[2];
  if (pipe(to_child) != 0 || pipe(from_child) != 0) abort();
  char here[1024], script[1200], ts_root[1200];
  if (getcwd(here, sizeof here) == NULL) abort();
  snprintf(script, sizeof script, "%s/tests/app/oracles/remote_loopback.ts", here);
  snprintf(ts_root, sizeof ts_root, "%s/../quake-anthology-ts", here);
  c->pid = fork();
  if (c->pid == 0) {
    dup2(to_child[0], 0); dup2(from_child[1], 1);
    close(to_child[1]); close(from_child[0]);
    if (chdir(ts_root) != 0) _exit(127);
    execlp("bun", "bun", script, (char *)NULL);
    _exit(127);
  }
  close(to_child[0]); close(from_child[1]);
  c->in = fdopen(to_child[1], "w");
  c->out = fdopen(from_child[0], "r");
  text request = {0};
  text_appendf(&request, "open %s", N(virtual_now));
  for (size_t i = 0; i < argc; i++) text_appendf(&request, "\t%s", argv[i]);
  text_append(&request, "\n");
  bool ok = ts_request(c, request.bytes, transcript, NULL, 0, NULL);
  text_free(&request);
  return ok;
}
static bool ts_step(client_driver *driver, text *transcript, char *phase, size_t phase_capacity, bool *has_output) {
  ts_client *c = (ts_client *)driver;
  char request[128];
  snprintf(request, sizeof request, "step %d %s\n", FRAME_MILLISECONDS, N(virtual_now));
  *has_output = false;
  return ts_request(c, request, transcript, phase, phase_capacity, has_output);
}
static void ts_close(client_driver *driver, text *transcript) {
  ts_client *c = (ts_client *)driver;
  if (c->in == NULL) return;
  (void)ts_request(c, "close\n", transcript, NULL, 0, NULL);
  fclose(c->in); fclose(c->out);
  int status = 0;
  waitpid(c->pid, &status, 0);
  c->in = c->out = NULL;
}

/* Replaces "127.0.0.1:<port>" with "127.0.0.1:<label>": the ephemeral ports differ per run. */
static void normalize_port(text *t, unsigned port, const char *label) {
  char needle[32], replacement[64];
  int length = snprintf(needle, sizeof needle, "127.0.0.1:%u", port);
  int replacement_length = snprintf(replacement, sizeof replacement, "127.0.0.1:<%s>", label);
  size_t from = 0;
  char *hit;
  while (t->bytes != NULL && (hit = strstr(t->bytes + from, needle)) != NULL) {
    size_t offset = (size_t)(hit - t->bytes);
    if (hit[length] >= '0' && hit[length] <= '9') { from = offset + 1; continue; }
    size_t tail = t->length - offset - (size_t)length;
    for (int i = 0; i < replacement_length; i++) text_append(t, " "); /* room */
    memmove(t->bytes + offset + replacement_length, t->bytes + offset + length, tail + 1);
    memcpy(t->bytes + offset, replacement, (size_t)replacement_length);
    t->length = offset + (size_t)replacement_length + tail;
    t->bytes[t->length] = '\0';
    from = offset + (size_t)replacement_length;
  }
}
static void normalize_ports(text *t, const relay *r) {
  for (size_t i = 0; i < r->lane_count; i++) {
    normalize_port(t, r->lanes[i].front_port, "relay");
    normalize_port(t, r->lanes[i].server_port, "server");
  }
  normalize_port(t, r->back_port, "peer");
}

/* ---- one run: a fresh server, one client ---------------------------------------------------------------------------- */
static bool run_family(const family_case *spec, client_driver *client, text *transcript, text *server_console,
    uint16_t *back_port) {
  virtual_now = 1000;
  virtual_clock = true;
  server_run server;
  memset(&server, 0, sizeof server);
  server.spec = spec;
  server.host.magic = SERVER_MAGIC;
  user_root("server", server.root, sizeof server.root);
  wipe(server.root);
  qa_caught_error caught;
  if (!qa_try(server_open_body, &server, &caught)) {
    text_appendf(transcript, "error server open %s: %s\n", caught.name, caught.message);
    text_append(server_console, server.host.console.bytes != NULL ? server.host.console.bytes : "");
    text_free(&server.host.console);
    qa_app_application_command_destroy(&server.command);
    return false;
  }
  qa_net_address address;
  if (!qa_application_network_address(server.app, &address)) qa_raise("Error", 0, 0, "server did not bind");
  relay r;
  relay_open(&r, address.port, *back_port);
  *back_port = r.back_port;
  qa_net_address_destroy(&address);

  char client_root[1024], connect[64];
  user_root("client", client_root, sizeof client_root);
  wipe(client_root);
  snprintf(connect, sizeof connect, "127.0.0.1:%u", r.port);
  const char *argv[32];
  size_t argc = 0;
  argv[argc++] = "--content-root"; argv[argc++] = corpus_root();
  argv[argc++] = "--user-content-root"; argv[argc++] = client_root;
  for (size_t i = 0; i < spec->client_count; i++) argv[argc++] = spec->client[i];
  argv[argc++] = spec->connect; argv[argc++] = connect;
  argv[argc++] = "--renderer"; argv[argc++] = "cpu";
  argv[argc++] = "--width"; argv[argc++] = "160";
  argv[argc++] = "--height"; argv[argc++] = "120";
  argv[argc++] = "--hidden";

  bool ok = client->open(client, argv, argc, transcript);
  char phase[64] = "", last_phase[64] = "";
  size_t snapshots = 0;
  for (size_t exchange = 0; ok && exchange < MAX_EXCHANGES && snapshots < SNAPSHOTS; exchange++) {
    text_appendf(transcript, "exchange %zu\n", exchange);
    advance_clock(FRAME_MILLISECONDS / 2.0);
    bool has_output = false;
    ok = client->step(client, transcript, phase, sizeof phase, &has_output);
    if (strcmp(phase, last_phase) != 0) { text_appendf(transcript, "phase %s\n", phase); snprintf(last_phase, sizeof last_phase, "%s", phase); }
    if (has_output) snapshots++;
    relay_upstream(&r, transcript, true);
    advance_clock(FRAME_MILLISECONDS / 2.0);
    if (!qa_try(server_step_body, server.app, &caught)) {
      text_appendf(transcript, "error server step %s: %s\n", caught.name, caught.message);
      ok = false;
    }
    relay_downstream(&r, transcript, true);
  }
  CHECK(snapshots == SNAPSHOTS);
  if (verbose) { fputs(transcript->bytes != NULL ? transcript->bytes : "", stdout); fputs("---- (before close)\n", stdout); fflush(stdout); }
  client->close(client, transcript);
  relay_upstream(&r, transcript, true);
  if (!qa_try(server_close_body, &server, &caught))
    text_appendf(server_console, "error server close %s: %s\n", caught.name, caught.message);
  normalize_ports(transcript, &r);
  relay_close(&r);
  normalize_ports(&server.host.console, &r);
  text_append(server_console, server.host.console.bytes != NULL ? server.host.console.bytes : "");
  text_free(&server.host.console);
  qa_app_application_command_destroy(&server.command);
  virtual_clock = false;
  return ok;
}

/* Line by line: reports the first differing lines (up to 30) and the counts. */
static bool compare(const char *name, const char *c, const char *ts) {
  size_t differences = 0, line = 0;
  const char *a = c != NULL ? c : "", *b = ts != NULL ? ts : "";
  while (*a != '\0' || *b != '\0') {
    const char *ea = strchr(a, '\n'), *eb = strchr(b, '\n');
    size_t la = ea == NULL ? strlen(a) : (size_t)(ea - a), lb = eb == NULL ? strlen(b) : (size_t)(eb - b);
    line++;
    if (la != lb || memcmp(a, b, la) != 0) {
      if (differences < 30)
        fprintf(stderr, "%s line %zu\n  C : %.*s\n  TS: %.*s\n", name, line, (int)(la > 400 ? 400 : la), a,
            (int)(lb > 400 ? 400 : lb), b);
      differences++;
    }
    a += la + (ea != NULL ? 1 : 0);
    b += lb + (eb != NULL ? 1 : 0);
  }
  printf("%s: %zu lines, %zu differing\n", name, line, differences);
  return differences == 0;
}

int main(int argc, char **argv) {
  signal(SIGPIPE, SIG_IGN);
  setvbuf(stdout, NULL, _IOLBF, 0); /* LeakSanitizer exits without flushing stdio */
  int first = 1;
  if (argc > 1 && strcmp(argv[1], "-v") == 0) { verbose = true; first = 2; }
  const char *only = argc > first ? argv[first] : NULL;
  setenv("SDL_VIDEODRIVER", "offscreen", 0);
  if (getenv("SDL_AUDIODRIVER") == NULL) setenv("SDL_AUDIODRIVER", "dummy", 1);
  const char *bun = getenv("REMOTE_LOOPBACK_BUN");
  bool with_bun = bun == NULL || strcmp(bun, "0") != 0;
  for (size_t i = 0; i < sizeof families / sizeof families[0]; i++) {
    const family_case *spec = &families[i];
    if (only != NULL && strcmp(only, spec->name) != 0) continue;
    text c_transcript = {0}, c_server = {0};
    uint16_t back_port = 0;
    c_client c;
    memset(&c, 0, sizeof c);
    c.base.open = c_open; c.base.step = c_step; c.base.close = c_close;
    const char *c_run = getenv("REMOTE_LOOPBACK_C");
    if (c_run == NULL || strcmp(c_run, "0") != 0) {
      bool c_ok = run_family(spec, &c.base, &c_transcript, &c_server, &back_port);
      CHECK(c_ok);
    }
    if (verbose) printf("---- C %s ----\n%s---- C %s server ----\n%s", spec->name, c_transcript.bytes, spec->name,
        c_server.bytes != NULL ? c_server.bytes : "");
    if (with_bun) {
      text ts_transcript = {0}, ts_server = {0};
      ts_client t;
      memset(&t, 0, sizeof t);
      t.base.open = ts_open; t.base.step = ts_step; t.base.close = ts_close;
      bool ts_ok = run_family(spec, &t.base, &ts_transcript, &ts_server, &back_port);
      CHECK(ts_ok);
      if (verbose) printf("---- TS %s ----\n%s", spec->name, ts_transcript.bytes);
      CHECK(compare(spec->name, c_transcript.bytes, ts_transcript.bytes));
      CHECK(compare("server", c_server.bytes, ts_server.bytes));
      text_free(&ts_transcript); text_free(&ts_server);
    }
    text_free(&c_transcript); text_free(&c_server);
  }
  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
