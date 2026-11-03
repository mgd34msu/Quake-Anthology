/* C checks for src/app/bootstrap/remote-seat-source.ts (RemoteSeatSource) and
 * remote-seats.ts (RemoteSeatChannel). The q1, qw, q2 and q3 sources run over
 * a fake ApplicationTransport (bound through qa_app_open_application_transport's
 * capabilities): each polls (the connection request), receives the server's
 * challenge or accept, polls twice (the connect packet with userinfo) and
 * closes. Every datagram the client sends, the phases and the constructor
 * errors are compared line for line with the TS RemoteSeatSource doing the same
 * (tests/app/oracles/remote_seat_source.ts, its output
 * tests/app/oracles/remote_seat_source.txt; run from the repository root). Every
 * hook checks its sentinel ctx. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/app/bootstrap/remote_application_internal.h"
#include "qa/app/bootstrap/remote_access.h"
#include "qa/app/bootstrap/remote_seat_source.h"
#include "qa/app/bootstrap/remote_seats.h"
#include "qa/app/bootstrap/server_map.h"
#include "qa/foundation/support.h"
#include "qa/network/common/js_number.h"

static int checks, failures;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static int hooks_sentinel, cvars_sentinel;

/* ---- output lines ---- */
static char **lines; static size_t line_count;
static void emit(const char *format, ...) {
  char buffer[4096];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof buffer, format, args);
  va_end(args);
  lines = realloc(lines, (line_count + 1) * sizeof *lines);
  lines[line_count++] = strdup(buffer);
}
static void format_address(const qa_net_address *address, char *out, size_t size) {
  if (address->kind == QA_NET_IPV4)
    snprintf(out, size, "%d.%d.%d.%d:%lld", address->ipv4[0], address->ipv4[1], address->ipv4[2], address->ipv4[3],
        (long long)address->port);
  else snprintf(out, size, "other:%lld", (long long)address->port);
}

/* ---- fake socket ---- */
typedef struct fake_socket {
  qa_net_address local;
  bool closed;
  qa_receive_event inbox[4]; size_t inbox_count;
} fake_socket;
static int fs_address(void *ctx, qa_net_address *out) {
  fake_socket *s = ctx;
  return qa_net_address_clone(&s->local, out);
}
static bool fs_closed(void *ctx) { return ((fake_socket *)ctx)->closed; }
static int64_t fs_max(void *ctx) { (void)ctx; return 1400; }
static bool fs_send(void *ctx, const qa_net_address *to, const uint8_t *payload, size_t length) {
  (void)ctx;
  char address[64];
  format_address(to, address, sizeof address);
  char *hex = malloc(length * 2 + 1);
  for (size_t i = 0; i < length; i++) snprintf(hex + i * 2, 3, "%02x", payload[i]);
  hex[length * 2] = '\0';
  emit("  send %s %s", address, hex);
  free(hex);
  return true;
}
static int fs_poll(void *ctx, qa_receive_event *out) {
  fake_socket *s = ctx;
  if (s->inbox_count == 0) return 0;
  *out = s->inbox[0];
  memmove(s->inbox, s->inbox + 1, (s->inbox_count - 1) * sizeof *s->inbox);
  s->inbox_count--;
  return 1;
}
static long long fs_subscribe(void *ctx, qa_net_listener_fn fn, void *listener_ctx) {
  (void)ctx; (void)fn; (void)listener_ctx;
  return 0;
}
static void fs_unsubscribe(void *ctx, long long id) { (void)ctx; (void)id; }
static void fs_close(void *ctx) { ((fake_socket *)ctx)->closed = true; }
static void fs_socks(void *ctx, const qa_socks_options *options) { (void)ctx; (void)options; }
static bool bind_fake(void *ctx, const qa_udp_bind_options *options, qa_app_udp_socket *out) {
  (void)options;
  memset(out, 0, sizeof *out);
  out->transport = (qa_datagram_transport){ctx, fs_address, fs_closed, fs_max, fs_send, fs_poll, fs_subscribe,
      fs_unsubscribe, fs_close};
  out->socks = (qa_socks_connector){ctx, fs_socks};
  return true;
}
static void push(fake_socket *s, const qa_net_address *from, const uint8_t *bytes, size_t length) {
  qa_receive_event *event = &s->inbox[s->inbox_count++];
  memset(event, 0, sizeof *event);
  event->kind = QA_RECV_PACKET;
  qa_net_address_clone(from, &event->from);
  event->payload = malloc(length);
  memcpy(event->payload, bytes, length);
  event->length = length;
}

/* ---- hooks (every one checks the sentinel) ---- */
static qa_cvar_registry *current_cvars;
static qa_cvar_registry *hook_cvars(void *ctx) { CHECK(ctx == &cvars_sentinel); return current_cvars; }
static const qa_app_options *h_options(void *ctx) { CHECK(ctx == &hooks_sentinel); qa_raise("Error", 0, 0, "options"); }
static qa_content_mounted *h_mounts(void *ctx) { CHECK(ctx == &hooks_sentinel); qa_raise("Error", 0, 0, "mounts"); }
static qa_app_loaded_content *h_content(void *ctx) { CHECK(ctx == &hooks_sentinel); qa_raise("Error", 0, 0, "content"); }
static qa_app_remote_content_mounts *h_current(void *ctx) { CHECK(ctx == &hooks_sentinel); return NULL; }
static qa_app_remote_content_mounts *h_select(void *ctx, const qa_remote_content_selection *selection,
    qa_app_remote_assert_fn assert_current, void *assert_ctx) {
  (void)selection; (void)assert_current; (void)assert_ctx;
  CHECK(ctx == &hooks_sentinel);
  qa_raise("Error", 0, 0, "select");
}
static qa_app_remote_content_mounts *h_refresh(void *ctx, qa_app_remote_assert_fn assert_current, void *assert_ctx) {
  (void)assert_current; (void)assert_ctx;
  CHECK(ctx == &hooks_sentinel);
  qa_raise("Error", 0, 0, "refresh");
}
static qa_app_loaded_content *h_load_q1(void *ctx, const qa_app_q1_remote_world_info *world, bool refresh) {
  (void)world; (void)refresh;
  CHECK(ctx == &hooks_sentinel);
  qa_raise("Error", 0, 0, "load");
}
static qa_app_loaded_content *h_load_q2(void *ctx, const qa_app_q2_game_state *state) {
  (void)state;
  CHECK(ctx == &hooks_sentinel);
  qa_raise("Error", 0, 0, "load");
}
static qa_app_loaded_content *h_load_q3(void *ctx, const qa_app_q1_remote_world_info *world,
    qa_q3_client_connection *connection, qa_app_loaded_content *prepared) {
  (void)world; (void)connection; (void)prepared;
  CHECK(ctx == &hooks_sentinel);
  qa_raise("Error", 0, 0, "load");
}
static void h_initialize(void *ctx, qa_q3_client_connection *connection) { (void)connection; CHECK(ctx == &hooks_sentinel); }
static void h_shutdown(void *ctx) { CHECK(ctx == &hooks_sentinel); }
static void h_cinematic_start(void *ctx, const char *name, qa_app_q2_cinematic_ended_fn ended, void *ended_ctx) {
  (void)name; (void)ended; (void)ended_ctx;
  CHECK(ctx == &hooks_sentinel);
}
static void h_cinematic_stop(void *ctx) { CHECK(ctx == &hooks_sentinel); }
static void h_publish(void *ctx, const qa_simulation_output *output) { (void)output; CHECK(ctx == &hooks_sentinel); }
static void h_disconnected(void *ctx, const char *reason) {
  CHECK(ctx == &hooks_sentinel);
  emit("  disconnected %s", reason);
}
static char *printed[16]; static size_t printed_count;
static void h_print(void *ctx, const char *text) {
  CHECK(ctx == &hooks_sentinel);
  if (printed_count < 16) printed[printed_count++] = strdup(text);
}
static bool h_recording(void *ctx) { CHECK(ctx == &hooks_sentinel); return false; }
static qa_app_remote_seat_source_hooks hooks(void) {
  qa_app_remote_seat_source_hooks h;
  memset(&h, 0, sizeof h);
  h.ctx = &hooks_sentinel;
  h.options = h_options; h.mounts = h_mounts; h.content = h_content; h.current_content = h_current;
  h.select_content = h_select; h.refresh_content = h_refresh;
  h.load_q1_world = h_load_q1; h.load_q2_world = h_load_q2; h.load_q3_world = h_load_q3;
  h.initialize_q3 = h_initialize; h.shutdown_q3 = h_shutdown;
  h.cinematic_start = h_cinematic_start; h.cinematic_stop = h_cinematic_stop;
  h.publish = h_publish; h.disconnected = h_disconnected; h.print = h_print; h.recording = h_recording;
  return h;
}
static void auth_request(void *ctx, qa_app_q3_assert_fn assert_current, void *assert_ctx,
    void (*send)(void *send_ctx, const qa_q3_address *address, const uint8_t *payload, size_t length), void *send_ctx) {
  (void)assert_current; (void)assert_ctx; (void)send; (void)send_ctx;
  CHECK(ctx == &hooks_sentinel);
}

static qa_cvar_registry *registry(qa_command_dialect dialect, const qa_identity_owner *identity) {
  qa_cvar_registry_options options;
  memset(&options, 0, sizeof options);
  options.dialect = dialect;
  options.context.session = identity->session;
  options.context.origin.kind = QA_COMMAND_ORIGIN_LOCAL_CONSOLE;
  qa_cvar_registry *cvars = qa_cvar_registry_create(&options);
  qa_cvar_snapshot snapshot;
  qa_cvar_registry_register(cvars, "name", "Tester", QA_CVAR_FLAG_USER_INFO | QA_CVAR_FLAG_ARCHIVE, &snapshot);
  qa_cvar_read_free(&snapshot);
  qa_cvar_registry_register(cvars, "rate", "25000", QA_CVAR_FLAG_USER_INFO | QA_CVAR_FLAG_ARCHIVE, &snapshot);
  qa_cvar_read_free(&snapshot);
  qa_cvar_registry_register(cvars, "color", "4", QA_CVAR_FLAG_ARCHIVE, &snapshot);
  qa_cvar_read_free(&snapshot);
  return cvars;
}
static qa_app_application_transport *open_transport(fake_socket *socket, qa_app_network_family family) {
  memset(socket, 0, sizeof *socket);
  const int64_t local[4] = {127, 0, 0, 1};
  qa_net_make_ipv4(&socket->local, local, 40000, false);
  qa_app_transport_capabilities capabilities;
  memset(&capabilities, 0, sizeof capabilities);
  capabilities.ctx = socket;
  capabilities.bind_udp = bind_fake;
  qa_app_open_transport_options options;
  memset(&options, 0, sizeof options);
  options.selection.kind = QA_APP_TRANSPORT_UDP;
  options.family = family;
  options.host = "0.0.0.0";
  options.port = 0;
  options.limits = QA_UNIFIED_DATAGRAM_LIMITS_NET;
  return qa_app_open_application_transport(&options, &capabilities);
}
static const char *phase(const qa_app_remote_seat_source *source) {
  return qa_app_network_phase_name(qa_app_remote_seat_network_phase(qa_app_remote_seat_source_network(source)));
}

typedef struct run_case { const char *name; qa_app_remote_seat_family family; qa_app_network_family transport; int port;
  const uint8_t *answer; size_t answer_length; } run_case;
typedef struct poll_state { qa_app_remote_seat_source *source; double now; } poll_state;
static void poll_body(void *ctx) {
  poll_state *state = ctx;
  qa_app_remote_seat_network_poll(qa_app_remote_seat_source_network(state->source), state->now);
}
static void run(const run_case *c) {
  emit("family %s", c->name);
  qa_identity_owner identity;
  char name[64];
  snprintf(name, sizeof name, "remote-seat-%s", c->name);
  CHECK(qa_identity_owner_create(&identity, name));
  qa_session_client *client = qa_session_client_create(qa_identity_owner_client(&identity, 1, 1));
  qa_session_seat *seat = qa_session_seat_create(qa_identity_owner_seat(&identity, 1), client);
  qa_command_dialect dialect = c->family == QA_APP_REMOTE_FAMILY_Q2 ? QA_COMMAND_DIALECT_Q2_CLASSIC
      : c->family == QA_APP_REMOTE_FAMILY_Q3 ? QA_COMMAND_DIALECT_Q3
      : c->family == QA_APP_REMOTE_FAMILY_QW ? QA_COMMAND_DIALECT_Q1_QUAKEWORLD : QA_COMMAND_DIALECT_Q1_NETQUAKE;
  current_cvars = registry(dialect, &identity);
  fake_socket socket;
  qa_app_application_transport *transport = open_transport(&socket, c->transport);
  CHECK(transport != NULL);
  qa_app_presentation_time clock;
  qa_app_presentation_time_init(&clock);
  qa_app_client_download_permission *permission =
      qa_app_create_client_download_permission(current_cvars, c->family == QA_APP_REMOTE_FAMILY_Q3);
  qa_app_remote_seat_source_options options;
  memset(&options, 0, sizeof options);
  options.identity = &identity;
  options.seat = seat;
  options.cvars = hook_cvars; options.cvars_ctx = &cvars_sentinel;
  options.clock = &clock;
  options.transport = transport;
  const int64_t host[4] = {127, 0, 0, 1};
  qa_net_make_ipv4(&options.address, host, c->port, false);
  options.qport = 4242;
  options.download_permission = permission;
  options.hooks = hooks();
  options.family = c->family;
  options.protocol.kind = "q2-classic"; options.protocol.version = 34;
  options.authorization = (qa_app_q3_authorization){&hooks_sentinel, auth_request};
  qa_app_remote_seat_source *source = qa_app_remote_seat_source_create(&options);
  qa_net_address_destroy(&options.address);
  CHECK(source != NULL);
  CHECK(qa_same_client(qa_app_remote_seat_presentation_client(qa_app_remote_seat_source_remote(source)),
      qa_session_client_id(client)));
  char qport[40];
  qa_net_js_number(qa_app_remote_seat_source_qport(source), qport, sizeof qport);
  emit("  qport %s phase %s", qport, phase(source));
  poll_state state = {source, 1000};
  qa_caught_error caught;
  CHECK(qa_try(poll_body, &state, &caught));
  emit("  poll 1000 phase %s", phase(source));
  qa_net_address server;
  qa_net_make_ipv4(&server, host, c->port, false);
  push(&socket, &server, c->answer, c->answer_length);
  state.now = 1100;
  CHECK(qa_try(poll_body, &state, &caught));
  emit("  poll 1100 phase %s", phase(source));
  state.now = 1200;
  CHECK(qa_try(poll_body, &state, &caught));
  emit("  poll 1200 phase %s", phase(source));
  /* TS the remote's actor before admission: none */
  qa_actor_id player;
  CHECK(!qa_app_remote_seat_presentation_player(qa_app_remote_seat_source_remote(source), &player));
  size_t progress = 99;
  (void)qa_app_remote_seat_source_download_progress(source, &progress);
  CHECK(progress == 0);
  char *pure = qa_app_remote_seat_source_pure_command(source, 1);
  CHECK(pure == NULL);
  qa_app_remote_seat_source_close(source);
  emit("  closed phase %s transport %s", phase(source), qa_app_application_transport_closed(transport) ? "true" : "false");
  qa_app_remote_seat_source_close(source); /* idempotent */
  for (size_t i = 0; i < printed_count; i++) {
    emit("  print %s", printed[i]);
    free(printed[i]);
  }
  printed_count = 0;
  qa_app_remote_seat_source_destroy(source);
  qa_net_address_destroy(&server);
  for (size_t i = 0; i < socket.inbox_count; i++) { qa_net_address_destroy(&socket.inbox[i].from); free(socket.inbox[i].payload); }
  qa_app_application_transport_destroy(transport);
  qa_net_address_destroy(&socket.local);
  qa_app_client_download_permission_destroy(permission);
  qa_cvar_registry_destroy(current_cvars);
  qa_session_seat_destroy(seat);
  qa_session_client_destroy(client);
  qa_identity_owner_destroy(&identity);
}

typedef struct error_case { const char *name; qa_app_remote_seat_family family; qa_net_address address; bool permission; } error_case;
static void create_body(void *ctx) { (void)qa_app_remote_seat_source_create(ctx); }
static void errors(void) {
  qa_identity_owner identity;
  CHECK(qa_identity_owner_create(&identity, "remote-seat-errors"));
  qa_session_client *client = qa_session_client_create(qa_identity_owner_client(&identity, 1, 1));
  qa_session_seat *seat = qa_session_seat_create(qa_identity_owner_seat(&identity, 1), client);
  current_cvars = registry(QA_COMMAND_DIALECT_Q1_NETQUAKE, &identity);
  fake_socket socket;
  qa_app_application_transport *transport = open_transport(&socket, QA_APP_FAMILY_Q1);
  qa_app_presentation_time clock;
  qa_app_presentation_time_init(&clock);
  const int64_t node[6] = {1, 2, 3, 4, 5, 6}, host[4] = {127, 0, 0, 1};
  error_case cases[3];
  memset(cases, 0, sizeof cases);
  cases[0].name = "qw-ipx"; cases[0].family = QA_APP_REMOTE_FAMILY_QW;
  qa_net_make_ipx(&cases[0].address, 1, node, 27500);
  cases[1].name = "q3-ipv6"; cases[1].family = QA_APP_REMOTE_FAMILY_Q3;
  cases[1].address.kind = QA_NET_IPV6; cases[1].address.host = strdup("::1"); cases[1].address.port = 27960;
  cases[2].name = "q2-null-permission"; cases[2].family = QA_APP_REMOTE_FAMILY_Q2;
  qa_net_make_ipv4(&cases[2].address, host, 27910, false);
  for (size_t i = 0; i < 3; i++) {
    qa_app_remote_seat_source_options options;
    memset(&options, 0, sizeof options);
    options.identity = &identity;
    options.seat = seat;
    options.cvars = hook_cvars; options.cvars_ctx = &cvars_sentinel;
    options.clock = &clock;
    options.transport = transport;
    options.address = cases[i].address;
    options.qport = 7;
    options.hooks = hooks();
    options.family = cases[i].family;
    options.protocol.kind = "q2-classic"; options.protocol.version = 34;
    options.authorization = (qa_app_q3_authorization){&hooks_sentinel, auth_request};
    qa_caught_error caught;
    if (qa_try(create_body, &options, &caught)) emit("error %s none", cases[i].name);
    else emit("error %s %s", cases[i].name, caught.message);
    qa_net_address_destroy(&cases[i].address);
  }
  qa_app_application_transport_destroy(transport);
  qa_net_address_destroy(&socket.local);
  qa_cvar_registry_destroy(current_cvars);
  qa_session_seat_destroy(seat);
  qa_session_client_destroy(client);
  qa_identity_owner_destroy(&identity);
}

/* ---- RemoteApplication getters over a promoted q2 peer ---- */
typedef struct promoted_state { qa_remote_application *app; qa_actor_id actor; } promoted_state;
static void source_for_actor_body(void *ctx) {
  promoted_state *state = ctx;
  (void)qa_remote_application_source_for_actor(state->app, state->actor);
}
static void send_command_body(void *ctx) { qa_remote_application_send_command(ctx, "say hi"); }
static void promoted(void) {
  emit("promoted q2");
  qa_identity_owner identity;
  CHECK(qa_identity_owner_create(&identity, "remote-seat-promoted"));
  qa_session_client *client = qa_session_client_create(qa_identity_owner_client(&identity, 2, 1));
  qa_session_seat *seat = qa_session_seat_create(qa_identity_owner_seat(&identity, 2), client);
  current_cvars = registry(QA_COMMAND_DIALECT_Q2_CLASSIC, &identity);
  fake_socket socket;
  qa_app_application_transport *transport = open_transport(&socket, QA_APP_FAMILY_Q2);
  qa_app_presentation_time clock;
  qa_app_presentation_time_init(&clock);
  qa_app_client_download_permission *permission = qa_app_create_client_download_permission(current_cvars, false);
  qa_app_remote_seat_source_options options;
  memset(&options, 0, sizeof options);
  options.identity = &identity;
  options.seat = seat;
  options.cvars = hook_cvars; options.cvars_ctx = &cvars_sentinel;
  options.clock = &clock;
  options.transport = transport;
  const int64_t host[4] = {127, 0, 0, 1};
  qa_net_make_ipv4(&options.address, host, 27910, false);
  options.qport = 99;
  options.download_permission = permission;
  options.hooks = hooks();
  options.family = QA_APP_REMOTE_FAMILY_Q2;
  options.protocol.kind = "q2-classic"; options.protocol.version = 34;
  qa_app_remote_seat_source *source = qa_app_remote_seat_source_create(&options);
  qa_net_address_destroy(&options.address);
  qa_remote_seat_peer peer;
  memset(&peer, 0, sizeof peer);
  peer.seat = seat;
  peer.source = source;
  qa_remote_seat_peer *peers[1] = {&peer};
  qa_remote_application *app = calloc(1, sizeof *app);
  app->primary_peer = &peer;
  app->initial_source.kind = QA_REMOTE_SOURCE_RECORDED;
  app->remote_seats = peers; app->remote_seat_count = 1;
  app->launch_options.map = strdup("e1m1");
  app->initial_seat_id = qa_identity_owner_seat(&identity, 0);
  emit("  networkPhase %s", qa_remote_application_network_phase(app));
  char index[40];
  qa_net_js_number((double)qa_remote_application_seat_id(app).index, index, sizeof index);
  emit("  seatId %s", index);
  qa_net_address address = qa_remote_application_network_address(app);
  char text[64];
  format_address(&address, text, sizeof text);
  qa_net_address_destroy(&address);
  emit("  networkAddress %s", text);
  size_t count = 9;
  (void)qa_remote_application_local_players(app, &count);
  emit("  localPlayers %zu", count);
  (void)qa_remote_application_download_progress(app, &count);
  emit("  downloadProgress %zu", count);
  bool same_source = qa_remote_application_source(app) == qa_remote_application_source(app);
  const qa_remote_live_network *network = qa_remote_application_network(app);
  bool same_network = network != NULL && network->q2 == qa_app_remote_seat_source_network(source)->q2
      && network->family == QA_APP_REMOTE_FAMILY_Q2;
  emit("  sameSource %s network %s", same_source ? "true" : "false", same_network ? "true" : "false");
  promoted_state state = {app, qa_identity_owner_actor(&identity, 5, 1)};
  qa_caught_error caught;
  if (qa_try(source_for_actor_body, &state, &caught)) emit("  sourceForActor none");
  else emit("  sourceForActor %s", caught.message);
  emit("  peerWorldCurrent %s", qa_remote_application_peer_world_current(app, &peer) ? "true" : "false");
  peer.map = strdup("e1m1");
  emit("  peerWorldCurrent %s", qa_remote_application_peer_world_current(app, &peer) ? "true" : "false");
  if (qa_try(send_command_body, app, &caught)) emit("  sendCommand none");
  else emit("  sendCommand %s", caught.message);
  emit("  ownsPublishedSource %s", qa_remote_application_owns_published_source(app) ? "true" : "false");
  app->closing = true;
  emit("  ownsPublishedSource closing %s", qa_remote_application_owns_published_source(app) ? "true" : "false");
  app->closing = false;
  peer.closed = true;
  emit("  ownsPublishedSource peer-closed %s", qa_remote_application_owns_published_source(app) ? "true" : "false");
  peer.closed = false;
  qa_remote_application_cancel_downloads(app, NULL);
  qa_remote_application_retry_downloads(app, NULL);
  qa_app_remote_seat_source_close(source);
  qa_app_remote_seat_source_destroy(source);
  free(peer.map);
  free(app->launch_options.map);
  free(app->local_player_rows);
  free(app->progress_rows);
  free(app);
  qa_app_application_transport_destroy(transport);
  qa_net_address_destroy(&socket.local);
  qa_app_client_download_permission_destroy(permission);
  qa_cvar_registry_destroy(current_cvars);
  qa_session_seat_destroy(seat);
  qa_session_client_destroy(client);
  qa_identity_owner_destroy(&identity);
}

/* ---- serverMapPath (server-map.ts; bun: serverMapPath(name)) ---- */
static void server_map(void) {
  static const char *const rows[][2] = {
    {"e1m1", "maps/e1m1.bsp"}, {"maps/q3dm1", "maps/q3dm1.bsp"}, {"base1.bsp", "maps/base1.bsp"},
    {"maps/dm2.bsp", "maps/dm2.bsp"}, {"ctf/x", "maps/ctf/x.bsp"},
  };
  for (size_t i = 0; i < sizeof rows / sizeof rows[0]; i++) {
    char *path = qa_app_server_map_path(rows[i][0]);
    CHECK(strcmp(path, rows[i][1]) == 0);
    free(path);
  }
}

int main(void) {
  server_map();
  errors();
  static const uint8_t q1_accept[] = {128, 0, 0, 9, 129, 145, 101, 0, 0};
  static const char qw[] = "\xff\xff\xff\xff" "c1234567", q2[] = "\xff\xff\xff\xff" "challenge 1234567",
      q3[] = "\xff\xff\xff\xff" "challengeResponse 1234567";
  const run_case cases[] = {
    {"q1", QA_APP_REMOTE_FAMILY_Q1, QA_APP_FAMILY_Q1, 26000, q1_accept, sizeof q1_accept},
    {"qw", QA_APP_REMOTE_FAMILY_QW, QA_APP_FAMILY_QW, 27500, (const uint8_t *)qw, sizeof qw - 1},
    {"q2", QA_APP_REMOTE_FAMILY_Q2, QA_APP_FAMILY_Q2, 27910, (const uint8_t *)q2, sizeof q2 - 1},
    {"q3", QA_APP_REMOTE_FAMILY_Q3, QA_APP_FAMILY_Q3, 27960, (const uint8_t *)q3, sizeof q3 - 1},
  };
  for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) run(&cases[i]);
  promoted();
  FILE *file = fopen("tests/app/oracles/remote_seat_source.txt", "r");
  CHECK(file != NULL);
  size_t at = 0;
  char expected[8192];
  while (file != NULL && fgets(expected, sizeof expected, file) != NULL) {
    expected[strcspn(expected, "\n")] = '\0';
    checks++;
    if (at >= line_count || strcmp(lines[at], expected) != 0) {
      failures++;
      fprintf(stderr, "line %zu:\n  expected: %s\n  actual:   %s\n", at + 1, expected, at < line_count ? lines[at] : "(none)");
    }
    at++;
  }
  if (file != NULL) fclose(file);
  CHECK(at == line_count);
  for (size_t i = 0; i < line_count; i++) free(lines[i]);
  free(lines);
  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
