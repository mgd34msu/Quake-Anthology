/* Q1 remote-input hardening: NetQuake entity writes against TS bytes, the
 * QuakeWorld connectionless challenge/rcon rules, malformed-input loops over
 * every Q1 decoder family under qa_try, and a QW server that drops only the
 * peer whose datagram is malformed. Expected bytes were dumped from the TS
 * sources under bun (scratchpad q1h_dump.ts). GPL-2.0-or-later. */

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
#include "qa/network/common/endpoint.h"
#include "qa/network/common/transport.h"
#include "qa/network/q1/channels.h"
#include "qa/network/q1/commands.h"
#include "qa/network/q1/demos.h"
#include "qa/network/q1/handshake.h"
#include "qa/network/q1/message.h"
#include "qa/network/q1/netquake.h"
#include "qa/network/q1/profile.h"
#include "qa/network/q1/quakeworld.h"
#include "qa/network/q1/session.h"
#include "qa/app/bootstrap/network/qw_server.h"
#include "qa/app/bootstrap/network/q1_client.h"

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
  fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static char *to_hex(const uint8_t *bytes, size_t length) {
  char *out = malloc(length * 2 + 1);
  for (size_t i = 0; i < length; i++) snprintf(out + i * 2, 3, "%02x", bytes[i]);
  out[length * 2] = '\0';
  return out;
}
static void check_hex(const uint8_t *bytes, size_t length, const char *expected, const char *label) {
  char *hex = to_hex(bytes, length);
  checks++;
  if (strcmp(hex, expected) != 0) {
    failures++;
    fprintf(stderr, "%s: got %s want %s\n", label, hex, expected);
  }
  free(hex);
}
static size_t from_hex(const char *hex, uint8_t *out) {
  size_t n = strlen(hex) / 2;
  for (size_t i = 0; i < n; i++) { unsigned v; sscanf(hex + i * 2, "%2x", &v); out[i] = (uint8_t)v; }
  return n;
}

/* ---- NetQuake entity delta writes (TS writeNetQuakeEntity, serverTime 10) ---- */
static const char *const nq_entity_expected[] = {
  "df3f01030401020824030700ff0e400015","e9442c012c01ffff","9007ff","8d3002ff2c04006000","c144409cff70","8005",
  "df3f01030401020824030700ff0e400015","e9c4172c012c01feff80200201","91800807ff3e","8d3002ff2c04006100","c1c417409cff70ffff11ff","8005",
  "df3f0103040102080000c9421c07000000c2390e000000415515","e9c4172c012c01cdcc4cbe80200201","918008074aff3e","8d3002ff2c00fcffc500004141","c1c417409cff70ffff11ff","8005",
  "df3f0103040102080000c9421c07000000c2390e000000415515","e9c4172c012c01cdcc4cbe80200201","918008074aff3e","8d3002ff2c00fcffc500004141","c1c417409cff70ffff11ff","8005",
  "df3f01030401020824030700ff0e400015","e9c4172c012c01feff80200201","91800807ff3e","8d3002ff2c04006100","c1c417409cff70ffff11ff","8005",
};
static qa_q1_extended_entity_state entity(double number) {
  qa_q1_extended_entity_state s;
  memset(&s, 0, sizeof s);
  s.base.number = number; s.scale = 16;
  return s;
}
static qa_vec3 vec(double x, double y, double z) { qa_vec3 v = {x, y, z}; return v; }
static void test_netquake_entity_writes(void) {
  qa_q1_protocol_identity profiles[5];
  memset(profiles, 0, sizeof profiles);
  profiles[0].kind = "q1-netquake"; profiles[0].version = 15;
  profiles[1].kind = "q1-fitzquake"; profiles[1].version = 666;
  profiles[2].kind = "q1-rmq"; profiles[2].version = 999; profiles[2].flags = 2 | 16;
  profiles[3].kind = "q1-rmq"; profiles[3].version = 999; profiles[3].flags = 2 | 16 | 64 | 128;
  profiles[4].kind = "q1-rmq"; profiles[4].version = 999; profiles[4].flags = 0;
  qa_q1_extended_entity_state states[6], bases[6];
  states[0] = entity(1); bases[0] = entity(1);
  states[0].base.origin = vec(100.5, -32, 8); states[0].base.angles = vec(10, 20, 30);
  states[0].base.model_index = 3; states[0].base.frame = 4; states[0].base.color_map = 1;
  states[0].base.skin = 2; states[0].base.effects = 8;
  states[1] = entity(300); bases[1] = entity(300);
  states[1].base.origin = vec(0.05, 0, -0.2); states[1].base.model_index = 300; states[1].base.frame = 513;
  states[1].alpha = 128; states[1].scale = 32; states[1].step = true;
  states[2] = entity(7); bases[2] = entity(7);
  states[2].lerp_finish_seconds = 12.25; states[2].base.angles = vec(-45, 359, 0.5);
  bases[2].base.angles = vec(-45, 0, 0.5);
  states[3] = entity(2); bases[3] = entity(2);
  states[3].base.origin = vec(4096.125, -8191.5, 12.0625); states[3].base.effects = 300; states[3].base.skin = 255;
  bases[3].base.origin = vec(4096.125, 0, 0);
  states[4] = entity(40000); bases[4] = entity(40000);
  states[4].base.model_index = 65535; states[4].base.frame = 70000; states[4].alpha = 255; states[4].scale = 255;
  states[5] = entity(5); bases[5] = entity(5);
  size_t row = 0;
  for (size_t p = 0; p < 5; p++)
    for (size_t i = 0; i < 6; i++, row++) {
      qa_size_buf sb;
      qa_size_buf_init(&sb, 128, false);
      qa_write_netquake_entity(&sb, profiles[p], &states[i], &bases[i], 10);
      char label[64];
      snprintf(label, sizeof label, "nq entity profile %zu case %zu", p, i);
      check_hex(sb.data, (size_t)sb.cursize, nq_entity_expected[row], label);
      qa_size_buf_destroy(&sb);
    }
}

/* ---- QW connectionless: challenge expiry and rcon backoff (TS q1h_dump.ts) ---- */
static const double challenge_values[] = {100, 200, 300, 400, 0x12345, 0x7fff};
static double next_challenge(void *ctx) { size_t *at = ctx; return challenge_values[(*at)++ % 6]; }
static bool never_blocked(void *ctx, const qa_net_address *from) { (void)from; CHECK(ctx == (void *)0x51); return false; }
static int connects = 0;
static qa_qw_host_result accept_connect(void *ctx, const qa_qw_connect_request *request, double now) {
  (void)request; (void)now;
  CHECK(ctx == (void *)0x51);
  connects++;
  qa_qw_host_result r = {0, NULL};
  return r;
}
static char *status_text(void *ctx) { CHECK(ctx == (void *)0x51); return strdup("stat\n"); }
static char *log_text(void *ctx, double sequence) { (void)sequence; CHECK(ctx == (void *)0x51); return NULL; }
static void run_admin(void *ctx, const char *command, qa_qw_write_fn write, void *write_ctx) {
  CHECK(ctx == (void *)0x51);
  write(write_ctx, "ran:", 4);
  write(write_ctx, command, strlen(command));
}
static const struct { const char *text; double now; const char *reply; } connectionless_rows[] = {
  {"getchallenge\n", 1000, "ffffffff63313030"},
  {"getchallenge\n", 20000, "ffffffff63313030"},
  {"connect 28 2000 100 \"\\\\name\\\\a\"\n", 31000, "ffffffff6a"},
  {"connect 28 2000 100 \"\\\\name\\\\a\"\n", 32000, "ffffffff6e0a426164206368616c6c656e67652e0a"},
  {"getchallenge\n", 40000, "ffffffff63323030"},
  {"rcon x a\n", 50000, "ffffffff6e4261642072636f6e5f70617373776f72642e0a"},
  {"rcon y a\n", 50000, "ffffffff6e4261642072636f6e5f70617373776f72642e0a"},
  {"rcon z a\n", 50000, "ffffffff6e4261642072636f6e5f70617373776f72642e0a"},
  {"rcon pw a\n", 50000, NULL},
  {"rcon pw a\n", 52000, "ffffffff6e72616e3a6120"},
  {"rcon pw b\n", 52001, "ffffffff6e72616e3a6220"},
};
static void init_connectionless(qa_qw_connectionless_server *server, qa_qw_challenges *challenges) {
  memset(server, 0, sizeof *server);
  server->host.ctx = (void *)0x51;
  server->host.password = ""; server->host.spectator_password = ""; server->host.rcon_password = "pw";
  server->host.high_characters = true;
  server->host.blocked = never_blocked; server->host.connect = accept_connect;
  server->host.status = status_text; server->host.log = log_text; server->host.execute_admin = run_admin;
  server->challenges = challenges;
}
static void test_connectionless_rules(void) {
  size_t at = 0;
  qa_qw_challenges challenges;
  qa_qw_challenges_init(&challenges, next_challenge, &at, 1024);
  qa_qw_connectionless_server server;
  init_connectionless(&server, &challenges);
  qa_net_address from;
  memset(&from, 0, sizeof from);
  from.kind = QA_NET_IPV4; from.ipv4[0] = 10; from.ipv4[3] = 1; from.port = 27001;
  for (size_t i = 0; i < sizeof connectionless_rows / sizeof *connectionless_rows; i++) {
    size_t length = 0;
    uint8_t *packet = qa_quakeworld_out_of_band(connectionless_rows[i].text, false, &length);
    size_t *lens = NULL, count = 0;
    uint8_t **replies = qa_qw_connectionless_receive(&server, packet, length, &from,
        connectionless_rows[i].now, &lens, &count);
    if (connectionless_rows[i].reply) {
      CHECK(count == 1);
      if (count == 1) check_hex(replies[0], lens[0], connectionless_rows[i].reply, connectionless_rows[i].text);
    } else CHECK(count == 0);
    for (size_t k = 0; k < count; k++) free(replies[k]);
    free(replies); free(lens); free(packet);
  }
  CHECK(connects == 1);
  CHECK(qa_password_matches("pw", "pw") && !qa_password_matches("", "") && !qa_password_matches("pw", "pW"));
  qa_qw_connectionless_server_destroy(&server);
  qa_qw_challenges_destroy(&challenges);
}

/* ---- malformed-input loops ---- */
static uint64_t rng = 0x9e3779b97f4a7c15ull;
static uint32_t next_random(void) {
  rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;
  return (uint32_t)(rng >> 11);
}
typedef void (*decode_fn)(const uint8_t *bytes, size_t length, void *ctx);
typedef struct fuzz_ctx { decode_fn decode; const uint8_t *bytes; size_t length; void *ctx; } fuzz_ctx;
static void fuzz_body(void *ctx) { fuzz_ctx *f = ctx; f->decode(f->bytes, f->length, f->ctx); }
/* Truncates and mutates a valid seed many times; every decode either succeeds or
 * raises through qa_try. A crash, sanitizer report or leak fails the binary. */
static void fuzz(const char *label, const uint8_t *seed, size_t length, decode_fn decode, void *ctx, int rounds) {
  uint8_t *work = malloc(length + 1);
  int caught_count = 0;
  for (int round = 0; round < rounds; round++) {
    memcpy(work, seed, length);
    size_t cut = round % 3 == 0 ? next_random() % (length + 1) : length;
    int mutations = (int)(next_random() % 4);
    for (int m = 0; m < mutations && cut > 0; m++) work[next_random() % cut] = (uint8_t)next_random();
    fuzz_ctx f = {decode, work, cut, ctx};
    qa_caught_error caught;
    if (!qa_try(fuzz_body, &f, &caught)) caught_count++;
  }
  checks++;
  if (caught_count == rounds && rounds > 0) { failures++; fprintf(stderr, "%s: every round raised\n", label); }
  free(work);
}
typedef struct nq_decode_ctx { qa_q1_protocol_identity protocol; const char *rerelease; } nq_decode_ctx;
static void nq_server_decode(const uint8_t *bytes, size_t length, void *ctx) {
  nq_decode_ctx *c = ctx;
  qa_netquake_decoder d;
  qa_netquake_decoder_init(&d, c->protocol, c->rerelease, true);
  qa_cleanup guard;
  qa_cleanup_push(&guard, (void (*)(void *))qa_netquake_decoder_destroy, &d);
  size_t count = 0;
  qa_netquake_message *messages = qa_netquake_decoder_decode(&d, bytes, length, &count);
  for (size_t i = 0; i < count; i++) qa_netquake_message_destroy(&messages[i]);
  free(messages);
  qa_cleanup_pop(&guard);
  qa_netquake_decoder_destroy(&d);
}
static void nq_client_decode(const uint8_t *bytes, size_t length, void *ctx) {
  nq_decode_ctx *c = ctx;
  size_t count = 0;
  qa_netquake_client_message *messages = qa_decode_netquake_client(bytes, length, c->protocol, &count);
  for (size_t i = 0; i < count; i++) qa_netquake_client_message_destroy(&messages[i]);
  free(messages);
}
static void qw_server_decode(const uint8_t *bytes, size_t length, void *ctx) {
  qa_quakeworld_decoder *d = ctx;
  size_t count = 0;
  qa_quakeworld_message *messages = qa_quakeworld_decoder_decode(d, bytes, length, 70, &count);
  for (size_t i = 0; i < count; i++) qa_quakeworld_message_destroy(&messages[i]);
  free(messages);
}
static void qw_client_decode(const uint8_t *bytes, size_t length, void *ctx) {
  (void)ctx;
  size_t count = 0;
  qa_quakeworld_client_message *messages = qa_decode_quakeworld_client(bytes, length,
      qa_quakeworld_profile_make(28, 0), 9, &count);
  for (size_t i = 0; i < count; i++) qa_quakeworld_client_message_destroy(&messages[i]);
  free(messages);
}
static void control_decode(const uint8_t *bytes, size_t length, void *ctx) {
  (void)ctx;
  qa_netquake_control m;
  qa_decode_netquake_control(bytes, length, &m);
  qa_netquake_control_destroy(&m);
}
static void connectionless_decode(const uint8_t *bytes, size_t length, void *ctx) {
  qa_qw_connectionless_server *server = ctx;
  qa_net_address from;
  memset(&from, 0, sizeof from);
  from.kind = QA_NET_IPV4; from.port = 5;
  size_t *lens = NULL, count = 0;
  uint8_t **replies = qa_qw_connectionless_receive(server, bytes, length, &from, 1000, &lens, &count);
  for (size_t k = 0; k < count; k++) free(replies[k]);
  free(replies); free(lens);
}
static void nq_demo_decode(const uint8_t *bytes, size_t length, void *ctx) {
  (void)ctx;
  qa_netquake_demo_reader reader;
  qa_netquake_demo_reader_init(&reader, bytes, length, 64000);
  qa_netquake_demo_record record;
  while (qa_netquake_demo_reader_next(&reader, &record)) free(record.message);
}
static void qwd_decode(const uint8_t *bytes, size_t length, void *ctx) {
  (void)ctx;
  qa_quakeworld_demo_reader reader;
  qa_quakeworld_demo_reader_init(&reader, bytes, length, 1450);
  qa_quakeworld_demo_playback playback;
  qa_quakeworld_demo_playback_init(&playback, &reader);
  qa_cleanup guard;
  qa_cleanup_push(&guard, (void (*)(void *))qa_quakeworld_demo_playback_destroy, &playback);
  size_t count = 0;
  qa_quakeworld_demo_record *records = qa_quakeworld_demo_playback_drain(&playback, 0, true, &count);
  for (size_t i = 0; i < count; i++) qa_quakeworld_demo_record_destroy(&records[i]);
  free(records);
  qa_cleanup_pop(&guard);
  qa_quakeworld_demo_playback_destroy(&playback);
}
static void nq_channel_decode(const uint8_t *bytes, size_t length, void *ctx) {
  (void)ctx;
  qa_netquake_channel channel;
  qa_netquake_channel_init(&channel, 8000, 1024);
  qa_cleanup guard;
  qa_cleanup_push(&guard, (void (*)(void *))qa_netquake_channel_destroy, &channel);
  qa_channel_delivery delivery;
  uint8_t *reply = NULL; size_t reply_length = 0;
  qa_netquake_channel_receive(&channel, bytes, length, 0, &delivery, &reply, &reply_length);
  free(reply); free(delivery.payload);
  qa_cleanup_pop(&guard);
  qa_netquake_channel_destroy(&channel);
}
static void qw_channel_decode(const uint8_t *bytes, size_t length, void *ctx) {
  (void)ctx;
  qa_quakeworld_channel channel;
  qa_quakeworld_channel_init(&channel, false, 7, 1450, 2500);
  qa_cleanup guard;
  qa_cleanup_push(&guard, (void (*)(void *))qa_quakeworld_channel_destroy, &channel);
  qa_quakeworld_delivery delivery;
  memset(&delivery, 0, sizeof delivery);
  if (qa_quakeworld_channel_receive(&channel, bytes, length, 0, &delivery)) free(delivery.payload);
  qa_cleanup_pop(&guard);
  qa_quakeworld_channel_destroy(&channel);
}
static size_t take_bytes(qa_size_buf *sb, uint8_t *out) { memcpy(out, sb->data, (size_t)sb->cursize); return (size_t)sb->cursize; }
static void test_malformed_inputs(void) {
  uint8_t seed[4096];
  size_t length;
  /* NetQuake server messages, retail 15 and RMQ. */
  qa_q1_protocol_identity profiles[2];
  memset(profiles, 0, sizeof profiles);
  profiles[0].kind = "q1-netquake"; profiles[0].version = 15;
  profiles[1].kind = "q1-rmq"; profiles[1].version = 999; profiles[1].flags = 2 | 16;
  for (int p = 0; p < 2; p++) {
    qa_size_buf sb;
    qa_size_buf_init(&sb, 4096, false);
    qa_netquake_message m;
    memset(&m, 0, sizeof m);
    m.kind = "server-info"; m.protocol = profiles[p]; m.max_clients = 8; m.game_type = 0;
    m.level = (char *)"start";
    char *models[] = {(char *)"maps/start.bsp", (char *)"progs/player.mdl"};
    char *sounds[] = {(char *)"weapons/r_exp3.wav"};
    m.models = models; m.model_count = 2; m.sounds = sounds; m.sound_count = 1;
    qa_write_netquake_message(&sb, profiles[p], &m, "known-retail", true);
    memset(&m, 0, sizeof m); m.kind = "print"; m.text = (char *)"hello\n";
    qa_write_netquake_message(&sb, profiles[p], &m, "known-retail", true);
    memset(&m, 0, sizeof m); m.kind = "sound"; m.entity = 3; m.channel = 1; m.index = 2; m.volume = 200; m.attenuation = 1; m.origin = vec(1, 2, 3);
    qa_write_netquake_message(&sb, profiles[p], &m, "known-retail", true);
    memset(&m, 0, sizeof m); m.kind = "baseline"; m.state = entity(4); m.state.base.model_index = 2;
    qa_write_netquake_message(&sb, profiles[p], &m, "known-retail", true);
    memset(&m, 0, sizeof m); m.kind = "temporary-entity"; m.effect.kind = QA_TE_BEAM; m.effect.type = 5; m.effect.entity = 3;
    qa_write_netquake_message(&sb, profiles[p], &m, "known-retail", true);
    memset(&m, 0, sizeof m); m.kind = "cd-track"; m.track = 2; m.loop_track = 2;
    qa_write_netquake_message(&sb, profiles[p], &m, "known-retail", true);
    memset(&m, 0, sizeof m); m.kind = "client-data"; m.data.view_height = 22; m.data.health = 100; m.data.active_weapon = 1;
    qa_write_netquake_message(&sb, profiles[p], &m, "known-retail", true);
    qa_q1_extended_entity_state e = entity(4), b = entity(4);
    e.base.origin = vec(5, 6, 7); e.base.frame = 300; e.alpha = 7;
    qa_write_netquake_entity(&sb, profiles[p], &e, &b, 1);
    memset(&m, 0, sizeof m); m.kind = "signon"; m.stage = 2;
    qa_write_netquake_message(&sb, profiles[p], &m, "known-retail", true);
    length = take_bytes(&sb, seed);
    qa_size_buf_destroy(&sb);
    nq_decode_ctx ctx = {profiles[p], "q1-rerelease-private"};
    fuzz("netquake server", seed, length, nq_server_decode, &ctx, 3000);
    /* Client move + string command. */
    qa_size_buf_init(&sb, 512, false);
    qa_q1_user_command command;
    memset(&command, 0, sizeof command);
    command.acknowledged_server_time_seconds = 3.5; command.view_angles = vec(10, 20, 0); command.forward_move = 200;
    qa_write_netquake_move(&sb, &command, profiles[p], 0, false);
    qa_write_client_string_command(&sb, "say hi");
    length = take_bytes(&sb, seed);
    qa_size_buf_destroy(&sb);
    fuzz("netquake client", seed, length, nq_client_decode, &ctx, 2000);
  }
  /* QuakeWorld server messages. */
  {
    qa_quakeworld_profile profile = qa_quakeworld_profile_make(28, 0);
    qa_size_buf sb;
    qa_size_buf_init(&sb, 4096, false);
    qa_quakeworld_message m;
    memset(&m, 0, sizeof m);
    m.kind = "server-data"; m.protocol = profile; m.server_count = 3; m.game_directory = (char *)"qw";
    m.player_slot = 1; m.level_name = (char *)"dm3"; m.move_variables.gravity = 800;
    qa_write_quakeworld_message(&sb, profile, &m);
    char *names[] = {(char *)"maps/dm3.bsp", (char *)"progs/player.mdl"};
    memset(&m, 0, sizeof m); m.kind = "model-list"; m.names = names; m.name_count = 2;
    qa_write_quakeworld_message(&sb, profile, &m);
    memset(&m, 0, sizeof m); m.kind = "sound"; m.entity = 3; m.channel = 1; m.index = 2; m.volume = 100; m.attenuation = 2;
    qa_write_quakeworld_message(&sb, profile, &m);
    memset(&m, 0, sizeof m); m.kind = "set-info"; m.slot = 1; m.key = (char *)"team"; m.str_value = (char *)"red";
    qa_write_quakeworld_message(&sb, profile, &m);
    qa_qw_nail nails[2] = {{{1, 2, 3}, 45, 90}, {{-100, 50, 7}, 0, 180}};
    memset(&m, 0, sizeof m); m.kind = "nails"; m.projectiles = nails; m.projectile_count = 2;
    qa_write_quakeworld_message(&sb, profile, &m);
    memset(&m, 0, sizeof m); m.kind = "player"; m.player.number = 1; m.player.flags = 0x7f;
    m.player.origin = vec(1, 2, 3); m.player.command.milliseconds = 13;
    qa_write_quakeworld_message(&sb, profile, &m);
    qa_qw_entity_state states[2];
    for (int i = 0; i < 2; i++) {
      qa_q1_extended_entity_state e = entity(10 + i);
      e.base.model_index = 2 + i; e.base.origin = vec(i, 2, 3);
      qa_qw_wire_entity_from_extended(&e, false, &states[i]);
    }
    qa_write_quakeworld_entities(&sb, profile, states, 2, NULL, 0, NULL);
    uint8_t block[4] = {1, 2, 3, 4};
    qa_write_quakeworld_download(&sb, false, 50, block, 4);
    length = take_bytes(&sb, seed);
    qa_size_buf_destroy(&sb);
    qa_quakeworld_decoder decoder;
    qa_quakeworld_decoder_init(&decoder, profile);
    fuzz("quakeworld server", seed, length, qw_server_decode, &decoder, 3000);
    qa_quakeworld_decoder_destroy(&decoder);
    /* Client move bundle + string command + upload. */
    qa_size_buf_init(&sb, 512, false);
    qa_quakeworld_move move;
    memset(&move, 0, sizeof move);
    move.current.milliseconds = 13; move.current.forward_move = 200; move.loss_percent = 3;
    qa_write_quakeworld_move(&sb, profile, &move, 9);
    qa_write_client_string_command(&sb, "download maps/dm3.bsp");
    qa_write_quakeworld_upload(&sb, block, 4, 10);
    length = take_bytes(&sb, seed);
    qa_size_buf_destroy(&sb);
    fuzz("quakeworld client", seed, length, qw_client_decode, NULL, 3000);
    /* Netchannel headers. */
    uint8_t datagram[32] = {5, 0, 0, 0x80, 3, 0, 0, 0, 7, 0, 4, 'n', 'e', 'w', 0};
    fuzz("quakeworld channel", datagram, 15, qw_channel_decode, NULL, 2000);
  }
  length = from_hex("0010000b00000005010203", seed);
  fuzz("netquake channel", seed, length, nq_channel_decode, NULL, 1000);
  length = from_hex("8000000c025155414b450003", seed);
  fuzz("netquake control", seed, length, control_decode, NULL, 1000);
  length = from_hex("8000001f83312e322e332e343a3236303030007372760065316d3100020803", seed);
  fuzz("netquake control info", seed, length, control_decode, NULL, 1000);
  {
    size_t at = 0;
    qa_qw_challenges challenges;
    qa_qw_challenges_init(&challenges, next_challenge, &at, 1024);
    qa_qw_connectionless_server server;
    init_connectionless(&server, &challenges);
    const char *texts[] = {"connect 28 2000 100 \"\\\\name\\\\a\\\\spectator\\\\1\"\n", "rcon pw status x\n", "getchallenge\n"};
    for (int i = 0; i < 3; i++) {
      uint8_t *packet = qa_quakeworld_out_of_band(texts[i], false, &length);
      memcpy(seed, packet, length);
      free(packet);
      fuzz("connectionless", seed, length, connectionless_decode, &server, 800);
    }
    qa_qw_connectionless_server_destroy(&server);
    qa_qw_challenges_destroy(&challenges);
  }
  /* Demo files: NQ .dem and QWD. */
  {
    size_t header_length = 0;
    uint8_t *header = qa_write_netquake_demo_header(-1, &header_length);
    memcpy(seed, header, header_length);
    free(header);
    length = header_length;
    uint8_t body[3] = {1, 8, 0};
    qa_netquake_demo_record record = {{1, 2, 3}, body, 3};
    for (int i = 0; i < 2; i++) {
      size_t n = 0;
      uint8_t *bytes = qa_write_netquake_demo_record(&record, &n);
      memcpy(seed + length, bytes, n); length += n;
      free(bytes);
    }
    fuzz("netquake demo", seed, length, nq_demo_decode, NULL, 2000);
    length = 0;
    qa_quakeworld_demo_record qwd;
    memset(&qwd, 0, sizeof qwd);
    qwd.kind = "command"; qwd.seconds = 0.5; qwd.command.milliseconds = 12;
    size_t n = 0;
    uint8_t *bytes = qa_write_quakeworld_demo_record(&qwd, &n);
    memcpy(seed + length, bytes, n); length += n; free(bytes);
    uint8_t packet_bytes[12] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 1, 1, 1};
    qwd.kind = "packet"; qwd.message = packet_bytes; qwd.length = 12;
    bytes = qa_write_quakeworld_demo_record(&qwd, &n);
    memcpy(seed + length, bytes, n); length += n; free(bytes);
    qwd.kind = "sequences"; qwd.message = NULL; qwd.length = 0; qwd.outgoing = 4; qwd.incoming = 5;
    bytes = qa_write_quakeworld_demo_record(&qwd, &n);
    memcpy(seed + length, bytes, n); length += n; free(bytes);
    fuzz("quakeworld demo", seed, length, qwd_decode, NULL, 2000);
  }
}

/* ---- QW signon server: hostile download paths and failing sources ---- */
static void signon_server_data(void *ctx, qa_quakeworld_message *out) {
  CHECK(ctx == (void *)0x52);
  memset(out, 0, sizeof *out);
  out->kind = "server-data"; out->protocol = qa_quakeworld_profile_make(28, 0); out->server_count = 3;
  out->game_directory = strdup("qw"); out->level_name = strdup("dm3");
}
static int disconnects = 0;
static void signon_disconnect(void *ctx, const char *reason) { (void)reason; CHECK(ctx == (void *)0x52); disconnects++; }
static int opened = 0, closed = 0;
static uint8_t *failing_read(void *ctx, double offset, double max_bytes, size_t *out_len) {
  (void)offset; (void)max_bytes; (void)out_len;
  CHECK(ctx == (void *)0x53);
  qa_raise("Error", 0, 0, "disk vanished");
}
static void source_close(void *ctx) { CHECK(ctx == (void *)0x53); closed++; }
static qa_download_source failing_source = {(void *)0x53, 10, failing_read, source_close};
static qa_download_source *signon_open(void *ctx, const char *path) {
  (void)path;
  CHECK(ctx == (void *)0x52);
  opened++;
  return &failing_source;
}
typedef struct command_call { qa_quakeworld_signon_server *server; const char *text; qa_qw_command_result result; } command_call;
static void command_body(void *ctx) {
  command_call *call = ctx;
  call->result = qa_quakeworld_signon_server_command(call->server, call->text, NULL, false);
}
static void test_signon_server(void) {
  qa_qw_signon_host host;
  memset(&host, 0, sizeof host);
  host.ctx = (void *)0x52; host.server_data = signon_server_data; host.disconnect = signon_disconnect;
  host.open_download = signon_open;
  qa_quakeworld_signon_server *server = qa_quakeworld_signon_server_create(host, false);
  /* Escaping paths close the transfer and answer "missing" instead of aborting. */
  const char *hostile[] = {"download ../../etc/passwd", "download /abs", "download maps/../../x"};
  for (int i = 0; i < 3; i++) {
    command_call call = {server, hostile[i], {0}};
    qa_caught_error caught;
    CHECK(qa_try(command_body, &call, &caught));
    CHECK(call.result.handled && call.result.count == 1);
    if (call.result.count == 1) check_hex(call.result.messages[0], call.result.lens[0], "29ffff00", hostile[i]);
    qa_qw_command_result_destroy(&call.result);
  }
  CHECK(opened == 0);
  /* A source whose read throws is closed and the error propagates (TS close(); throw). */
  command_call call = {server, "download maps/dm3.bsp", {0}};
  qa_caught_error caught;
  CHECK(!qa_try(command_body, &call, &caught));
  CHECK(strcmp(caught.name, "Error") == 0 && strcmp(caught.message, "disk vanished") == 0);
  CHECK(opened == 1 && closed == 1);
  call.text = "nextdl";
  CHECK(qa_try(command_body, &call, &caught));
  CHECK(call.result.count == 0);
  qa_qw_command_result_destroy(&call.result);
  /* Non-numeric checksum is ToUint32(NaN) = 0, never undefined behavior. */
  call.text = "soundlist x";
  CHECK(qa_try(command_body, &call, &caught));
  qa_qw_command_result_destroy(&call.result);
  qa_quakeworld_signon_server_free(server);
}

/* ---- QW server network: a malformed datagram costs only its peer ---- */
typedef struct fake_transport {
  qa_receive_event queue[16]; size_t head, count;
  int sent;
} fake_transport;
static int ft_address(void *ctx, qa_net_address *out) {
  (void)ctx; memset(out, 0, sizeof *out); out->kind = QA_NET_IPV4; out->port = 27500; return 1;
}
static bool ft_send(void *ctx, const qa_net_address *to, const uint8_t *payload, size_t length) {
  (void)to; (void)payload; (void)length;
  fake_transport *t = ctx; t->sent++; return true;
}
static int ft_poll(void *ctx, qa_receive_event *out) {
  fake_transport *t = ctx;
  if (t->head == t->count) return 0;
  *out = t->queue[t->head++];
  return 1;
}
static void ft_close(void *ctx) { (void)ctx; }
static void ft_push(fake_transport *t, uint8_t last_octet, const uint8_t *bytes, size_t length) {
  qa_receive_event *e = &t->queue[t->count++];
  memset(e, 0, sizeof *e);
  e->kind = QA_RECV_PACKET;
  e->from.kind = QA_NET_IPV4; e->from.ipv4[0] = 10; e->from.ipv4[3] = last_octet; e->from.port = 27001;
  e->payload = malloc(length); memcpy(e->payload, bytes, length); e->length = length;
}
static qa_qw_signon_host player_signon;
static int admitted = 0, dropped = 0;
static char last_drop[256];
static qa_app_qw_admission host_admit(void *ctx, const void *request) {
  (void)request; CHECK(ctx == (void *)0x54);
  qa_app_qw_admission a;
  memset(&a, 0, sizeof a);
  a.accepted = true; a.player.slot = admitted; a.player.client.slot = admitted + 1; a.player.client.generation = 7;
  admitted++;
  return a;
}
static void player_server_data(void *ctx, qa_quakeworld_message *out) {
  (void)ctx;
  memset(out, 0, sizeof *out);
  out->kind = "server-data"; out->protocol = qa_quakeworld_profile_make(28, 0); out->server_count = 1;
  out->player_slot = admitted - 1;
}
static const void *host_signon(void *ctx, qa_app_qw_player player) {
  (void)player; CHECK(ctx == (void *)0x54);
  return &player_signon;
}
static const void *const *host_baselines(void *ctx, qa_app_qw_player player, size_t *out_count) {
  (void)player; CHECK(ctx == (void *)0x54);
  *out_count = 0; return NULL;
}
static void host_disconnect(void *ctx, qa_app_qw_player player, const char *reason) {
  (void)player; CHECK(ctx == (void *)0x54);
  dropped++;
  snprintf(last_drop, sizeof last_drop, "%s", reason);
}
static int printed = 0;
static void host_print(void *ctx, const char *text) { (void)text; CHECK(ctx == (void *)0x54); printed++; }
static double zero_random(void *ctx) { (void)ctx; return 77; }
static void connect_peer(qa_app_qw_server_network *network, fake_transport *transport, uint8_t octet, int qport, double now) {
  char text[96];
  snprintf(text, sizeof text, "connect 28 %d 77 \"\\\\name\\\\p%d\"\n", qport, octet);
  size_t length = 0;
  uint8_t *packet = qa_quakeworld_out_of_band("getchallenge\n", false, &length);
  ft_push(transport, octet, packet, length);
  free(packet);
  packet = qa_quakeworld_out_of_band(text, false, &length);
  ft_push(transport, octet, packet, length);
  free(packet);
  size_t count = 0;
  qa_app_qw_server_network_poll(network, now, &count);
}
static void test_qw_server_drops_only_the_offender(void) {
  memset(&player_signon, 0, sizeof player_signon);
  player_signon.server_data = player_server_data;
  fake_transport transport;
  memset(&transport, 0, sizeof transport);
  qa_datagram_transport datagram;
  memset(&datagram, 0, sizeof datagram);
  datagram.ctx = &transport; datagram.get_address = ft_address; datagram.send = ft_send;
  datagram.poll = ft_poll; datagram.close = ft_close;
  qa_app_qw_server_network_options options;
  memset(&options, 0, sizeof options);
  options.transport = &datagram;
  options.host.ctx = (void *)0x54; options.host.max_clients = 8;
  options.host.admit = host_admit; options.host.signon = host_signon; options.host.baselines = host_baselines;
  options.host.disconnect = host_disconnect; options.host.print = host_print;
  options.random = zero_random;
  qa_app_qw_server_network *network = qa_app_qw_server_network_create(options);
  {
    /* TS readonly wire = { kind: "source", protocol: { kind: "q1-quakeworld", version: 28 } } */
    qa_app_application_network as_network = qa_app_qw_server_network_as_network(network);
    qa_wire_selection wire = as_network.wire(as_network.ctx);
    CHECK(!wire.is_unified && strcmp(wire.protocol.kind, "q1-quakeworld") == 0 && wire.protocol.version == 28);
  }
  connect_peer(network, &transport, 1, 100, 1000);
  connect_peer(network, &transport, 2, 200, 1000);
  qa_app_qw_player players[4];
  CHECK(qa_app_qw_server_network_clients(network, players, 4) == 2);
  /* Peer 1: an unknown clc opcode after a valid header. */
  uint8_t bad[] = {1, 0, 0, 0, 0, 0, 0, 0, 100, 0, 0x63, 1, 2};
  ft_push(&transport, 1, bad, sizeof bad);
  /* Peer 2: a hostile download path inside a valid string command. */
  uint8_t good[64] = {1, 0, 0, 0, 0, 0, 0, 0, 200, 0, 4};
  const char *command = "download ../../x";
  memcpy(good + 11, command, strlen(command) + 1);
  ft_push(&transport, 2, good, 11 + strlen(command) + 1);
  /* Garbage connectionless datagrams never reach a peer. */
  uint8_t junk[] = {255, 255, 255, 255, 'c', 'o', 'n', 'n', 'e', 'c', 't', ' ', '2', '8', ' ', '\xff'};
  ft_push(&transport, 3, junk, sizeof junk);
  size_t count = 0;
  qa_app_qw_server_network_poll(network, 1100, &count);
  CHECK(dropped == 1);
  CHECK(strstr(last_drop, "Unknown QuakeWorld client message") != NULL);
  CHECK(printed >= 1);
  CHECK(qa_app_qw_server_network_clients(network, players, 4) == 1);
  CHECK(players[0].slot == 1);
  qa_app_qw_server_network_close(network);
  CHECK(dropped == 2 && strcmp(last_drop, "Server shutdown") == 0);
  qa_app_qw_server_network_destroy(network);
  for (size_t i = transport.head; i < transport.count; i++) qa_receive_event_destroy(&transport.queue[i]);
}

/* ---- q1-client.ts: a malformed server datagram ends the connection, not the process ---- */
static int client_disconnects = 0;
static char client_reason[256];
static void client_receive(void *ctx, qa_netquake_message *messages, size_t count, double now) {
  (void)messages; (void)count; (void)now; CHECK(ctx == (void *)0x55);
}
static qa_q1_user_command client_command(void *ctx, const void *actor_command) {
  (void)actor_command; CHECK(ctx == (void *)0x55);
  qa_q1_user_command c; memset(&c, 0, sizeof c); return c;
}
static void client_disconnected(void *ctx, const char *reason) {
  CHECK(ctx == (void *)0x55);
  client_disconnects++;
  snprintf(client_reason, sizeof client_reason, "%s", reason);
}
static bool ft_closed(void *ctx) { (void)ctx; return false; }
static void ft_push_port(fake_transport *t, int64_t port, const uint8_t *bytes, size_t length) {
  ft_push(t, 1, bytes, length);
  t->queue[t->count - 1].from.port = port;
}
static void test_netquake_client_drops_malformed_server(void) {
  fake_transport transport;
  memset(&transport, 0, sizeof transport);
  qa_app_q1_client_network_options options;
  memset(&options, 0, sizeof options);
  options.transport.ctx = &transport; options.transport.get_address = ft_address; options.transport.send = ft_send;
  options.transport.poll = ft_poll; options.transport.close = ft_close; options.transport.is_closed = ft_closed;
  options.remote.kind = QA_NET_IPV4; options.remote.ipv4[0] = 10; options.remote.ipv4[3] = 1; options.remote.port = 26000;
  options.host.ctx = (void *)0x55; options.host.receive = client_receive; options.host.command = client_command;
  options.host.disconnected = client_disconnected;
  options.seat.name = "player"; options.seat.spawn_parameters = "";
  qa_app_q1_client_network *client = qa_app_q1_client_network_create(options);
  size_t count = 0;
  qa_app_q1_client_network_poll(client, 0, &count);
  /* CCREP_ACCEPT to port 26001. */
  qa_netquake_control accept;
  memset(&accept, 0, sizeof accept);
  accept.kind = QA_NQ_ACCEPT; accept.port = 26001;
  size_t length = 0;
  uint8_t *bytes = qa_encode_netquake_control(&accept, &length);
  ft_push_port(&transport, 26000, bytes, length);
  free(bytes);
  qa_app_q1_client_network_poll(client, 10, &count);
  CHECK(qa_app_q1_client_network_phase(client) == QA_APP_PHASE_LOADING);
  /* An unreliable datagram whose payload is an unknown server opcode. */
  uint8_t bad[] = {0x00, 0x10, 0x00, 0x0a, 0, 0, 0, 0, 0x7f, 0x7f};
  ft_push_port(&transport, 26001, bad, sizeof bad);
  qa_app_q1_client_network_poll(client, 20, &count);
  CHECK(client_disconnects == 1 && strstr(client_reason, "Unknown") != NULL);
  CHECK(qa_app_q1_client_network_phase(client) == QA_APP_PHASE_REJECTED);
  qa_app_q1_client_network_destroy(client);
  for (size_t i = transport.head; i < transport.count; i++) qa_receive_event_destroy(&transport.queue[i]);
}

int main(void) {
  test_netquake_entity_writes();
  test_connectionless_rules();
  test_malformed_inputs();
  test_signon_server();
  test_qw_server_drops_only_the_offender();
  test_netquake_client_drops_malformed_server();
  printf("q1_hardening: %d checks, %d failures\n", checks, failures);
  return failures != 0;
}
