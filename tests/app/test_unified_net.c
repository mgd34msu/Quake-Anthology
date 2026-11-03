/* Ports tests/network/unified/server.test.ts and client.test.ts, the control encoders against
 * TS bytes, and malformed-input containment at the wire boundary. GPL-2.0-or-later. */
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include "qa/app/bootstrap/network/unified_client.h"
#include "qa/app/bootstrap/network/unified_control.h"
#include "qa/app/bootstrap/network/unified_server.h"
#include "qa/contracts/identity.h"
#include "qa/foundation/support.h"
#include "qa/network/common/loopback.h"
#include "qa/network/unified/channel.h"
#include "qa/network/unified/packet.h"
#include "qa/persistence/value.h"
#include "unified_net_corpus.h"
static int failures, checks;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
  fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
static int sentinel;
#define NONCE "11111111111111111111111111111111"

/* --- server fixture, as server.test.ts fixture() --- */
typedef struct fixture {
  qa_identity_owner identity;
  qa_app_unified_player player;
  qa_loopback_hub hub;
  qa_loopback_transport *server_end, *client_end;
  qa_datagram_transport transport;
  qa_app_unified_composition *composition;
  qa_app_unified_server_network *server;
  qa_unified_channel channel;
  int disconnected;
  bool raise_on_command, component_answer, has_component;
  char printed[4096];
} fixture;
static fixture *active;
static void print_cb(void *ctx, const char *text) {
  CHECK(ctx == &sentinel);
  strncat(active->printed, text, sizeof active->printed - strlen(active->printed) - 1);
}
static qa_app_unified_admission admit_cb(void *ctx, const qa_net_address *address, const char *userinfo) {
  (void)address;
  fixture *f = ctx;
  CHECK(f == active);
  CHECK(strcmp(userinfo, "\\name\\Fixture") == 0);
  qa_app_unified_admission admission = {true, f->player, NULL};
  return admission;
}
static void disconnect_cb(void *ctx, qa_app_unified_player player) {
  (void)player;
  CHECK(ctx == active);
  active->disconnected++;
}
static qa_app_unified_player carried_cb(void *ctx, qa_client_id client) {
  (void)client;
  return ((fixture *)ctx)->player;
}
static void userinfo_cb(void *ctx, qa_app_unified_player player, const char *value) {
  (void)player; (void)value;
  CHECK(ctx == active);
}
static void command_cb(void *ctx, qa_app_unified_player player, const char *name,
    const char *const *args, size_t count) {
  (void)player; (void)name; (void)args; (void)count;
  CHECK(ctx == active);
  if (active->raise_on_command) qa_range_error("source command failed");
}
static bool component_cb(void *ctx, qa_app_unified_player player, qa_presentation_owner owner,
    double generation, const char *const *args, size_t count) {
  (void)player; (void)owner; (void)generation; (void)args; (void)count;
  CHECK(ctx == active);
  return active->component_answer;
}
static qa_actor_command input_cb(void *ctx, qa_app_unified_player player, double sequence,
    const qa_user_command *command, const qa_arsenal_intent *arsenal) {
  CHECK(ctx == active);
  qa_actor_command out;
  memset(&out, 0, sizeof out);
  out.actor = player.actor;
  out.source.kind = QA_COMMAND_SOURCE_REMOTE_CLIENT;
  out.source.client = player.client;
  out.sequence = sequence;
  out.command = *command;
  out.arsenal = arsenal;
  return out;
}
static const qa_app_unified_resource_key *resources_cb(void *ctx, qa_app_unified_player player,
    const void *output, size_t *out_count) {
  (void)player; (void)output;
  CHECK(ctx == active);
  *out_count = 0;
  return NULL;
}
static qa_app_unified_presentation_frame_t frame_cb(void *ctx, qa_app_unified_player player,
    const void *output, double epoch, double acknowledged_input) {
  (void)ctx; (void)player; (void)output; (void)epoch; (void)acknowledged_input;
  qa_range_error("No frame requested in input boundary test");
}
static void pump_to_channel(fixture *f, double now) {
  qa_receive_event event;
  while (qa_loopback_transport_poll(f->client_end, &event)) {
    if (event.kind == QA_RECV_PACKET) {
      size_t count = 0;
      qa_unified_delivery *got = qa_unified_channel_receive(&f->channel, event.payload, event.length, now, &count);
      qa_unified_deliveries_destroy(got, count);
    }
    qa_receive_event_destroy(&event);
  }
}
static void send_flush(fixture *f, double now) {
  size_t *lengths = NULL, count = 0;
  uint8_t **packets = qa_unified_channel_flush(&f->channel, now, &lengths, &count);
  for (size_t i = 0; i < count; i++) {
    qa_loopback_transport_send(f->client_end, &f->server_end->address, packets[i], lengths[i]);
    free(packets[i]);
  }
  free(packets);
  free(lengths);
}
static void fixture_open(fixture *f, bool raise_on_command, bool has_component, bool component_answer) {
  memset(f, 0, sizeof *f);
  active = f;
  f->raise_on_command = raise_on_command;
  f->component_answer = component_answer;
  CHECK(qa_identity_owner_create(&f->identity, "unified-server"));
  f->player.client = qa_identity_owner_client(&f->identity, 0, 0);
  f->player.actor = qa_identity_owner_actor(&f->identity, 1, 0);
  f->player.source_entity = 1;
  CHECK(qa_loopback_hub_init(&f->hub, NULL, NULL, NULL));
  f->server_end = qa_loopback_hub_bind(&f->hub, "server");
  f->client_end = qa_loopback_hub_bind(&f->hub, "client");
  f->transport = qa_loopback_transport_as_transport(f->server_end);
  qa_save_value *recipe = qa_parse_json_value((const uint8_t *)corpus_recipe, strlen(corpus_recipe), false);
  CHECK(recipe != NULL);
  f->composition = qa_app_unified_create_composition(recipe, NULL, 0);
  qa_save_value_free(recipe);
  CHECK(strcmp(f->composition->digest, corpus_digest) == 0);
  qa_app_unified_server_options options;
  memset(&options, 0, sizeof options);
  options.transport = &f->transport;
  options.composition = f->composition;
  options.print_ctx = &sentinel;
  options.print = print_cb;
  options.host.ctx = f;
  options.host.mode = "deathmatch";
  options.host.max_clients = 2;
  options.host.session = f->identity.session;
  options.host.admit = admit_cb;
  options.host.disconnect = disconnect_cb;
  options.host.carried_player = carried_cb;
  options.host.userinfo = userinfo_cb;
  options.host.command = command_cb;
  if (has_component) options.host.component_command = component_cb;
  options.host.input = input_cb;
  options.host.resources = resources_cb;
  options.host.frame = frame_cb;
  f->server = qa_app_unified_server_network_create(options);
  size_t count = 0;
  qa_loopback_transport_send(f->client_end, &f->server_end->address, (const uint8_t *)corpus_hello, strlen(corpus_hello));
  qa_app_unified_server_network_poll(f->server, 0, &count);
  qa_receive_event event;
  CHECK(qa_loopback_transport_poll(f->client_end, &event));
  CHECK(event.kind == QA_RECV_PACKET);
  qa_app_unified_handshake challenge;
  CHECK(qa_app_unified_decode_handshake(event.payload, event.length, &challenge));
  CHECK(challenge.kind == QA_APP_UNIFIED_CHALLENGE);
  qa_app_unified_handshake connect = {0};
  connect.kind = QA_APP_UNIFIED_CONNECT;
  connect.nonce = challenge.nonce;
  connect.token = challenge.token;
  size_t length = 0;
  uint8_t *bytes = qa_app_unified_encode_handshake(&connect, &length);
  qa_loopback_transport_send(f->client_end, &f->server_end->address, bytes, length);
  free(bytes);
  qa_unified_channel_init(&f->channel, challenge.token, NULL);
  qa_app_unified_handshake_free(&challenge);
  qa_receive_event_destroy(&event);
  qa_app_unified_server_network_poll(f->server, 1, &count);
  pump_to_channel(f, 1);
  qa_unified_channel_queue_reliable(&f->channel, (const uint8_t *)corpus_ready, strlen(corpus_ready));
  for (double now = 2; now < 6; now++) {
    send_flush(f, now);
    qa_app_unified_server_network_poll(f->server, now, &count);
    pump_to_channel(f, now);
  }
  CHECK(qa_app_unified_server_network_clients(f->server, NULL, 0) == 1);
}
static void fixture_close(fixture *f) {
  qa_app_unified_server_network_close(f->server);
  qa_app_unified_server_network_destroy(f->server);
  qa_unified_channel_destroy(&f->channel);
  qa_app_unified_composition_destroy(f->composition);
  qa_loopback_hub_close(&f->hub);
  qa_loopback_hub_destroy(&f->hub);
  qa_identity_owner_destroy(&f->identity);
}
static void queue_inputs(fixture *f, const qa_app_unified_input *inputs, size_t count) {
  qa_app_unified_input_batch batch;
  memset(&batch, 0, sizeof batch);
  batch.epoch = 1;
  batch.commands = (qa_app_unified_input *)inputs;
  batch.command_count = count;
  size_t length = 0;
  uint8_t *bytes = qa_app_unified_encode_inputs(&batch, &length);
  qa_unified_channel_queue_frame(&f->channel, bytes, length, 1);
  free(bytes);
}
static qa_app_unified_input netquake_input(double sequence, double impulse) {
  qa_app_unified_input input;
  memset(&input, 0, sizeof input);
  input.sequence = sequence;
  input.command.kind = QA_USER_COMMAND_Q1;
  input.command.as.q1.forward_move = 200;
  input.command.as.q1.view_angles.y = sequence;
  input.command.as.q1.impulse = impulse;
  return input;
}
static void test_disconnect_removes_queued_inputs(void) {
  fixture f;
  fixture_open(&f, false, false, false);
  qa_app_unified_input input = netquake_input(7, 0);
  queue_inputs(&f, &input, 1);
  qa_app_unified_control control = {0};
  control.kind = QA_APP_UNIFIED_DISCONNECT;
  control.reason = "fixture done";
  size_t length = 0;
  uint8_t *bytes = qa_app_unified_encode_control(&control, &length);
  qa_unified_channel_queue_reliable(&f.channel, bytes, length);
  free(bytes);
  size_t *lengths = NULL, count = 0;
  uint8_t **packets = qa_unified_channel_flush(&f.channel, 10, &lengths, &count);
  for (int pass = 0; pass < 2; pass++)
    for (size_t i = 0; i < count; i++) {
      qa_unified_packet packet;
      CHECK(qa_decode_unified_packet(packets[i], lengths[i], &packet));
      if ((packet.kind == QA_UNIFIED_FRAME) == (pass == 0))
        qa_loopback_transport_send(f.client_end, &f.server_end->address, packets[i], lengths[i]);
    }
  for (size_t i = 0; i < count; i++) free(packets[i]);
  free(packets);
  free(lengths);
  size_t polled = 99;
  qa_app_unified_server_network_poll(f.server, 10, &polled);
  CHECK(polled == 0);
  CHECK(f.disconnected == 1);
  CHECK(qa_app_unified_server_network_clients(f.server, NULL, 0) == 0);
  CHECK(qa_app_unified_server_network_phase(f.server) == QA_APP_PHASE_ACTIVE);
  fixture_close(&f);
}
static void test_command_failure_drops_only_peer(void) {
  fixture f;
  fixture_open(&f, true, false, false);
  qa_app_unified_control control = {0};
  control.kind = QA_APP_UNIFIED_COMMAND;
  control.epoch = 1;
  control.name = "source_command";
  size_t length = 0;
  uint8_t *bytes = qa_app_unified_encode_control(&control, &length);
  qa_unified_channel_queue_reliable(&f.channel, bytes, length);
  free(bytes);
  send_flush(&f, 10);
  size_t polled = 99;
  qa_app_unified_server_network_poll(f.server, 10, &polled);
  CHECK(polled == 0);
  CHECK(f.disconnected == 1);
  CHECK(qa_app_unified_server_network_clients(f.server, NULL, 0) == 0);
  CHECK(qa_app_unified_server_network_phase(f.server) == QA_APP_PHASE_ACTIVE);
  CHECK(strstr(f.printed, "disconnected: source command failed\n") != NULL);
  fixture_close(&f);
}
static void test_malformed_input_drops_only_peer(void) {
  fixture f;
  fixture_open(&f, false, false, false);
  const uint8_t bad[] = {255, 0, 17};
  qa_unified_channel_queue_frame(&f.channel, bad, sizeof bad, 1);
  send_flush(&f, 10);
  size_t polled = 99;
  qa_app_unified_server_network_poll(f.server, 10, &polled);
  CHECK(polled == 0);
  CHECK(f.disconnected == 1);
  CHECK(qa_app_unified_server_network_phase(f.server) == QA_APP_PHASE_ACTIVE);
  CHECK(strstr(f.printed, "disconnected: ") != NULL);
  fixture_close(&f);
}
static void test_netquake_burst(void) {
  fixture f;
  fixture_open(&f, false, false, false);
  qa_app_unified_input inputs[3] = {netquake_input(7, 9), netquake_input(8, 0), netquake_input(9, 0)};
  queue_inputs(&f, inputs, 3);
  send_flush(&f, 10);
  size_t polled = 0;
  const qa_actor_command *commands = qa_app_unified_server_network_poll(f.server, 10, &polled);
  CHECK(polled == 1);
  if (polled == 1) {
    CHECK(commands[0].sequence == 9);
    CHECK(commands[0].command.kind == QA_USER_COMMAND_Q1);
    CHECK(commands[0].command.as.q1.impulse == 9);
    CHECK(commands[0].command.as.q1.view_angles.y == 9);
  }
  fixture_close(&f);
}
static void test_burst_merges_arsenal(void) {
  fixture f;
  fixture_open(&f, false, false, false);
  qa_app_unified_input inputs[2] = {netquake_input(7, 0), netquake_input(8, 0)};
  inputs[0].has_arsenal = true;
  inputs[0].arsenal.provider = "q1:weapons";
  inputs[0].arsenal.weapon = "q1:axe";
  inputs[0].arsenal.has_impulse = true;
  inputs[0].arsenal.impulse = 4;
  inputs[0].arsenal.use_holdable = true;
  inputs[1].has_arsenal = true;
  inputs[1].arsenal.provider = "q1:weapons";
  queue_inputs(&f, inputs, 2);
  send_flush(&f, 10);
  size_t polled = 0;
  const qa_actor_command *commands = qa_app_unified_server_network_poll(f.server, 10, &polled);
  CHECK(polled == 1);
  if (polled == 1) {
    CHECK(commands[0].sequence == 8);
    CHECK(commands[0].arsenal != NULL);
    if (commands[0].arsenal) {
      CHECK(strcmp(commands[0].arsenal->provider, "q1:weapons") == 0);
      CHECK(commands[0].arsenal->weapon && strcmp(commands[0].arsenal->weapon, "q1:axe") == 0);
      CHECK(commands[0].arsenal->use_holdable);
      CHECK(commands[0].arsenal->has_impulse && commands[0].arsenal->impulse == 4);
    }
  }
  fixture_close(&f);
}
static void test_out_of_bounds_inputs(void) {
  for (int which = 0; which < 3; which++) {
    fixture f;
    fixture_open(&f, false, false, false);
    qa_app_unified_input input;
    memset(&input, 0, sizeof input);
    input.sequence = 7;
    if (which < 2) {
      input.command.kind = QA_USER_COMMAND_Q3;
      input.command.as.q3.server_time_milliseconds = 100;
      if (which == 0) input.command.as.q3.forward_move = 128;
      else input.command.as.q3.right_move = -129;
    } else {
      input.command.kind = QA_USER_COMMAND_Q2_RERELEASE;
      input.command.as.q2_rerelease.milliseconds = 256;
    }
    queue_inputs(&f, &input, 1);
    send_flush(&f, 10);
    size_t polled = 99;
    qa_app_unified_server_network_poll(f.server, 10, &polled);
    CHECK(polled == 0);
    CHECK(f.disconnected == 1);
    CHECK(qa_app_unified_server_network_phase(f.server) == QA_APP_PHASE_ACTIVE);
    fixture_close(&f);
  }
}
static void test_bounds_admitted(void) {
  fixture f;
  fixture_open(&f, false, false, false);
  qa_app_unified_input input;
  memset(&input, 0, sizeof input);
  input.sequence = 7;
  input.command.kind = QA_USER_COMMAND_Q3;
  input.command.as.q3.server_time_milliseconds = 100;
  input.command.as.q3.forward_move = 127;
  input.command.as.q3.right_move = -128;
  queue_inputs(&f, &input, 1);
  send_flush(&f, 10);
  size_t polled = 0;
  const qa_actor_command *commands = qa_app_unified_server_network_poll(f.server, 10, &polled);
  CHECK(polled == 1 && commands[0].sequence == 7);
  CHECK(f.disconnected == 0);
  fixture_close(&f);
}
static void test_retired_component_command(void) {
  fixture f;
  fixture_open(&f, false, true, false);
  qa_app_unified_control control = {0};
  control.kind = QA_APP_UNIFIED_COMPONENT_COMMAND;
  control.epoch = 1;
  control.owner.provider = "mod:retired";
  control.owner.generation = 1;
  const char *args[] = {"score"};
  control.args = args;
  control.arg_count = 1;
  size_t length = 0;
  uint8_t *bytes = qa_app_unified_encode_control(&control, &length);
  qa_unified_channel_queue_reliable(&f.channel, bytes, length);
  free(bytes);
  send_flush(&f, 10);
  size_t polled = 99;
  qa_app_unified_server_network_poll(f.server, 10, &polled);
  CHECK(polled == 0);
  CHECK(f.disconnected == 1);
  CHECK(qa_app_unified_server_network_phase(f.server) == QA_APP_PHASE_ACTIVE);
  fixture_close(&f);
}
/* The remote crash this file exists for: a bad authenticated packet costs its peer, never the
 * server, for every shape a client can send. */
static uint32_t lfsr = 0x1234567;
static uint32_t next_random(void) { lfsr ^= lfsr << 13; lfsr ^= lfsr >> 17; lfsr ^= lfsr << 5; return lfsr; }
static void test_mutated_control_never_kills_server(void) {
  const char *seeds[] = {corpus_ready, corpus_offer, corpus_inputs, corpus_hello,
    "[", "{\"schema\":\"qts-control\",\"version\":1,\"value\":{\"kind\":\"resources\",\"epoch\":1,\"resources\":[{}]}}",
    "{\"schema\":\"qts-control\",\"version\":1,\"value\":{\"kind\":\"command\",\"epoch\":1,\"name\":\"1bad\",\"args\":[\"a\"]}}",
    "{\"schema\":\"qts-control\",\"version\":1,\"value\":{\"kind\":\"component-command\",\"epoch\":1,\"args\":[]}}",
    "{\"schema\":\"qts-control\",\"version\":1,\"value\":{\"kind\":\"components\",\"epoch\":1,\"update\":{\"sources\":[{}]}}}",
    "{\"schema\":\"qts-input\",\"version\":1,\"value\":{\"epoch\":1,\"commands\":[{\"sequence\":1,\"command\":{\"kind\":\"q3\"}}]}}"};
  for (size_t s = 0; s < sizeof seeds / sizeof seeds[0]; s++)
    for (int round = 0; round < 40; round++) {
      fixture f;
      fixture_open(&f, false, false, false);
      size_t length = strlen(seeds[s]);
      uint8_t *bytes = malloc(length + 1);
      memcpy(bytes, seeds[s], length);
      if (round > 0) {
        int mutations = 1 + (int)(next_random() % 4);
        for (int m = 0; m < mutations; m++) bytes[next_random() % length] = (uint8_t)next_random();
        if (next_random() % 3 == 0) length = next_random() % length + 1;
      }
      if (round % 2 == 0) qa_unified_channel_queue_reliable(&f.channel, bytes, length);
      else qa_unified_channel_queue_frame(&f.channel, bytes, length, 1);
      free(bytes);
      send_flush(&f, 10);
      size_t polled = 0;
      qa_app_unified_server_network_poll(f.server, 10, &polled);
      CHECK(qa_app_unified_server_network_phase(f.server) == QA_APP_PHASE_ACTIVE);
      fixture_close(&f);
    }
}
typedef struct deep_args { const uint8_t *bytes; size_t length; } deep_args;
static void deep_body(void *ctx) {
  const deep_args *args = ctx;
  qa_app_unified_control control = qa_app_unified_decode_control(args->bytes, args->length);
  qa_app_unified_control_free(&control);
}
/* A packet of open brackets would otherwise recurse the decoder off the stack. */
static void test_deep_json_is_a_decode_error(void) {
  size_t depth = 2 * 1024 * 1024;
  uint8_t *bytes = malloc(depth);
  memset(bytes, '[', depth);
  deep_args args = {bytes, depth};
  qa_caught_error caught;
  CHECK(!qa_try(deep_body, &args, &caught));
  CHECK(strcmp(caught.name, "RangeError") == 0);
  free(bytes);
}
/* TS: the challenge token is a keyed hash of source, nonce and window; a connect carrying any
 * other token allocates nothing, and an admitted peer that never readies is dropped at 60 s. */
static void test_forged_connect_and_signon_timeout(void) {
  fixture f;
  fixture_open(&f, false, false, false);
  qa_app_unified_handshake connect = {0};
  connect.kind = QA_APP_UNIFIED_CONNECT;
  connect.nonce = "22222222222222222222222222222222";
  connect.token = "abcdefabcdefabcdefabcdefabcdefab";
  size_t length = 0, polled = 0;
  uint8_t *bytes = qa_app_unified_encode_handshake(&connect, &length);
  qa_loopback_transport *other = qa_loopback_hub_bind(&f.hub, "forger");
  qa_loopback_transport_send(other, &f.server_end->address, bytes, length);
  free(bytes);
  qa_app_unified_server_network_poll(f.server, 7, &polled);
  qa_receive_event event;
  CHECK(!qa_loopback_transport_poll(other, &event));
  CHECK(qa_app_unified_server_network_clients(f.server, NULL, 0) == 1);
  /* A real second peer from `other` that completes the handshake but never sends ready. */
  qa_app_unified_handshake hello = {0};
  hello.kind = QA_APP_UNIFIED_HELLO;
  hello.nonce = "33333333333333333333333333333333";
  bytes = qa_app_unified_encode_handshake(&hello, &length);
  qa_loopback_transport_send(other, &f.server_end->address, bytes, length);
  free(bytes);
  qa_app_unified_server_network_poll(f.server, 8, &polled);
  CHECK(qa_loopback_transport_poll(other, &event));
  qa_app_unified_handshake challenge;
  CHECK(qa_app_unified_decode_handshake(event.payload, event.length, &challenge));
  qa_receive_event_destroy(&event);
  connect.nonce = challenge.nonce;
  connect.token = challenge.token;
  bytes = qa_app_unified_encode_handshake(&connect, &length);
  qa_loopback_transport_send(other, &f.server_end->address, bytes, length);
  free(bytes);
  qa_unified_channel unready;
  qa_unified_channel_init(&unready, challenge.token, NULL);
  qa_app_unified_handshake_free(&challenge);
  qa_app_unified_server_network_poll(f.server, 9, &polled);
  while (qa_loopback_transport_poll(other, &event)) qa_receive_event_destroy(&event);
  f.printed[0] = '\0';
  /* The silent ready fixture peer times out at 30 s. The unready one keeps its connection alive
   * with frames (ignored before ready) and is dropped at 60 s by the signon limit. */
  for (double now = 20000; now <= 50000; now += 15000) {
    const uint8_t keepalive[] = {'x'};
    qa_unified_channel_queue_frame(&unready, keepalive, sizeof keepalive, 0);
    size_t *lengths = NULL, count = 0;
    uint8_t **packets = qa_unified_channel_flush(&unready, now, &lengths, &count);
    for (size_t i = 0; i < count; i++) {
      qa_loopback_transport_send(other, &f.server_end->address, packets[i], lengths[i]);
      free(packets[i]);
    }
    free(packets);
    free(lengths);
    qa_app_unified_server_network_poll(f.server, now, &polled);
    while (qa_loopback_transport_poll(other, &event)) qa_receive_event_destroy(&event);
  }
  CHECK(strstr(f.printed, "disconnected: Connection timed out\n") != NULL);
  CHECK(strstr(f.printed, "Signon timed out") == NULL);
  CHECK(f.disconnected == 1);
  qa_app_unified_server_network_poll(f.server, 60010, &polled);
  CHECK(strstr(f.printed, "disconnected: Signon timed out\n") != NULL);
  CHECK(f.disconnected == 1);
  qa_unified_channel_destroy(&unready);
  fixture_close(&f);
}
static void test_handshake_is_null_on_garbage(void) {
  qa_app_unified_handshake decoded;
  CHECK(!qa_app_unified_decode_handshake((const uint8_t *)"junk", 4, &decoded));
  CHECK(!qa_app_unified_decode_handshake((const uint8_t *)corpus_ready, strlen(corpus_ready), &decoded));
  const char *bad_nonce = "{\"schema\":\"qts-connect\",\"version\":1,\"value\":{\"kind\":\"hello\",\"nonce\":\"xy\"}}";
  CHECK(!qa_app_unified_decode_handshake((const uint8_t *)bad_nonce, strlen(bad_nonce), &decoded));
  CHECK(qa_app_unified_decode_handshake((const uint8_t *)corpus_hello, strlen(corpus_hello), &decoded));
  CHECK(decoded.kind == QA_APP_UNIFIED_HELLO && strcmp(decoded.nonce, NONCE) == 0);
  qa_app_unified_handshake_free(&decoded);
}
static void test_control_bytes_match_ts(void) {
  qa_app_unified_control control = {0};
  control.kind = QA_APP_UNIFIED_READY;
  control.epoch = 1;
  control.composition_digest = corpus_digest;
  control.userinfo = "\\name\\Fixture";
  size_t length = 0;
  uint8_t *bytes = qa_app_unified_encode_control(&control, &length);
  CHECK(length == strlen(corpus_ready) && memcmp(bytes, corpus_ready, length) == 0);
  free(bytes);
  qa_app_unified_input input = netquake_input(7, 0);
  input.command.as.q1.view_angles.y = 0;
  qa_app_unified_input_batch batch;
  memset(&batch, 0, sizeof batch);
  batch.epoch = 1;
  batch.commands = &input;
  batch.command_count = 1;
  bytes = qa_app_unified_encode_inputs(&batch, &length);
  CHECK(length == strlen(corpus_inputs) && memcmp(bytes, corpus_inputs, length) == 0);
  free(bytes);
  qa_app_unified_handshake hello = {0};
  hello.kind = QA_APP_UNIFIED_HELLO;
  hello.nonce = NONCE;
  bytes = qa_app_unified_encode_handshake(&hello, &length);
  CHECK(length == strlen(corpus_hello) && memcmp(bytes, corpus_hello, length) == 0);
  free(bytes);
}
/* --- client.test.ts --- */
typedef struct client_fixture {
  qa_identity_owner identity;
  qa_loopback_hub hub;
  qa_loopback_transport *client_end, *server_end;
  qa_datagram_transport transport;
  qa_app_unified_remote *remote;
  qa_app_unified_client_network *network;
  char failures[512]; int failure_count;
} client_fixture;
static void client_disconnected(void *ctx, const char *reason) {
  client_fixture *f = ctx;
  f->failure_count++;
  snprintf(f->failures, sizeof f->failures, "%s", reason);
}
static const char *client_userinfo(void *ctx) {
  CHECK(ctx == &sentinel);
  return "\\name\\Test";
}
static void client_open(client_fixture *f) {
  memset(f, 0, sizeof *f);
  CHECK(qa_identity_owner_create(&f->identity, "unified-client-test"));
  CHECK(qa_loopback_hub_init(&f->hub, NULL, NULL, NULL));
  f->client_end = qa_loopback_hub_bind(&f->hub, "client");
  f->server_end = qa_loopback_hub_bind(&f->hub, "server");
  f->transport = qa_loopback_transport_as_transport(f->client_end);
  qa_app_unified_remote_options remote;
  memset(&remote, 0, sizeof remote);
  remote.ctx = f;
  remote.session = f->identity.session;
  remote.client = qa_identity_owner_client(&f->identity, 3, 0);
  remote.seat = qa_identity_owner_seat(&f->identity, 2);
  remote.disconnected = client_disconnected;
  f->remote = qa_app_unified_remote_create(remote);
  CHECK(f->remote != NULL);
  qa_app_unified_client_options options;
  memset(&options, 0, sizeof options);
  options.transport = &f->transport;
  options.remote = f->server_end->address;
  options.host = f->remote;
  options.userinfo_ctx = &sentinel;
  options.userinfo = client_userinfo;
  f->network = qa_app_unified_client_network_create(options);
}
static void client_close(client_fixture *f) {
  qa_app_unified_client_network_close(f->network);
  qa_app_unified_client_network_destroy(f->network);
  qa_app_unified_remote_destroy(f->remote);
  qa_loopback_hub_close(&f->hub);
  qa_loopback_hub_destroy(&f->hub);
  qa_identity_owner_destroy(&f->identity);
}
typedef struct client_poll { client_fixture *f; double now; } client_poll;
static void client_poll_body(void *ctx) {
  client_poll *poll = ctx;
  size_t count = 0;
  qa_app_unified_client_network_poll(poll->f->network, poll->now, &count);
}
static bool client_poll_try(client_fixture *f, double now, qa_caught_error *caught) {
  client_poll poll = {f, now};
  return qa_try(client_poll_body, &poll, caught);
}
static void test_client_handshake_times_out(void) {
  client_fixture f;
  client_open(&f);
  qa_caught_error caught;
  CHECK(client_poll_try(&f, 100, &caught));
  CHECK(qa_app_unified_client_network_phase(f.network) == QA_APP_PHASE_CHALLENGING);
  CHECK(!client_poll_try(&f, 120101, &caught));
  CHECK(strstr(caught.message, "timed out") != NULL);
  CHECK(qa_loopback_transport_closed(f.client_end));
  CHECK(qa_app_unified_client_network_phase(f.network) == QA_APP_PHASE_CLOSED);
  CHECK(f.failure_count == 1 && strcmp(f.failures, "Unified connection timed out") == 0);
  client_close(&f);
}
static void client_send_challenge(client_fixture *f, const qa_loopback_transport *from, const char *nonce, const char *token) {
  qa_app_unified_handshake challenge = {0};
  challenge.kind = QA_APP_UNIFIED_CHALLENGE;
  challenge.nonce = nonce;
  challenge.token = token;
  size_t length = 0;
  uint8_t *bytes = qa_app_unified_encode_handshake(&challenge, &length);
  qa_loopback_transport_send((qa_loopback_transport *)from, &f->client_end->address, bytes, length);
  free(bytes);
}
static void test_client_challenge_matching(void) {
  client_fixture f;
  client_open(&f);
  qa_caught_error caught;
  CHECK(client_poll_try(&f, 0, &caught));
  qa_receive_event event;
  CHECK(qa_loopback_transport_poll(f.server_end, &event));
  qa_app_unified_handshake hello;
  CHECK(qa_app_unified_decode_handshake(event.payload, event.length, &hello));
  CHECK(hello.kind == QA_APP_UNIFIED_HELLO);
  char nonce[33];
  snprintf(nonce, sizeof nonce, "%s", hello.nonce);
  qa_app_unified_handshake_free(&hello);
  qa_receive_event_destroy(&event);
  const char *token = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
  qa_loopback_transport *other = qa_loopback_hub_bind(&f.hub, "other");
  client_send_challenge(&f, other, nonce, token);
  client_send_challenge(&f, f.server_end, "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", token);
  CHECK(client_poll_try(&f, 1, &caught));
  CHECK(qa_app_unified_client_network_phase(f.network) == QA_APP_PHASE_CHALLENGING);
  CHECK(!qa_loopback_transport_poll(f.server_end, &event));
  client_send_challenge(&f, f.server_end, nonce, token);
  CHECK(client_poll_try(&f, 2, &caught));
  CHECK(qa_app_unified_client_network_phase(f.network) == QA_APP_PHASE_CONNECTING);
  CHECK(qa_loopback_transport_poll(f.server_end, &event));
  qa_app_unified_handshake connect;
  CHECK(qa_app_unified_decode_handshake(event.payload, event.length, &connect));
  CHECK(connect.kind == QA_APP_UNIFIED_CONNECT && strcmp(connect.nonce, nonce) == 0 && strcmp(connect.token, token) == 0);
  qa_app_unified_handshake_free(&connect);
  qa_receive_event_destroy(&event);
  qa_app_unified_client_network_close(f.network);
  CHECK(client_poll_try(&f, 10000, &caught));
  CHECK(!qa_loopback_transport_poll(f.server_end, &event));
  client_close(&f);
}
static void test_client_malformed_control_rejects(void) {
  client_fixture f;
  client_open(&f);
  qa_caught_error caught;
  CHECK(client_poll_try(&f, 0, &caught));
  qa_receive_event event;
  CHECK(qa_loopback_transport_poll(f.server_end, &event));
  qa_app_unified_handshake hello;
  CHECK(qa_app_unified_decode_handshake(event.payload, event.length, &hello));
  const char *token = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
  client_send_challenge(&f, f.server_end, hello.nonce, token);
  qa_app_unified_handshake_free(&hello);
  qa_receive_event_destroy(&event);
  CHECK(client_poll_try(&f, 1, &caught));
  qa_unified_channel channel;
  qa_unified_channel_init(&channel, token, NULL);
  const uint8_t bad[] = {'n', 'o', 't', ' ', 'j', 's', 'o', 'n'};
  qa_unified_channel_queue_reliable(&channel, bad, sizeof bad);
  size_t *lengths = NULL, count = 0;
  uint8_t **packets = qa_unified_channel_flush(&channel, 2, &lengths, &count);
  for (size_t i = 0; i < count; i++) {
    qa_loopback_transport_send(f.server_end, &f.client_end->address, packets[i], lengths[i]);
    free(packets[i]);
  }
  free(packets);
  free(lengths);
  CHECK(!client_poll_try(&f, 2, &caught));
  /* TS decodeCheckpointValue now wraps JSON.parse failures (bun: SaveFormatError
     "checkpoint: JSON Parse error: Unexpected identifier \"not\""). */
  CHECK(strcmp(caught.name, "SaveFormatError") == 0);
  CHECK(f.failure_count == 1);
  CHECK(qa_app_unified_client_network_phase(f.network) == QA_APP_PHASE_CLOSED);
  qa_unified_channel_destroy(&channel);
  client_close(&f);
}
int main(void) {
  test_control_bytes_match_ts();
  test_handshake_is_null_on_garbage();
  test_forged_connect_and_signon_timeout();
  test_deep_json_is_a_decode_error();
  test_disconnect_removes_queued_inputs();
  test_command_failure_drops_only_peer();
  test_malformed_input_drops_only_peer();
  test_netquake_burst();
  test_burst_merges_arsenal();
  test_out_of_bounds_inputs();
  test_bounds_admitted();
  test_retired_component_command();
  test_mutated_control_never_kills_server();
  test_client_handshake_times_out();
  test_client_challenge_matching();
  test_client_malformed_control_rejects();
  if (failures) { fprintf(stderr, "%d failures of %d checks\n", failures, checks); return 1; }
  printf("test_unified_net: %d checks passed\n", checks);
  return 0;
}
