/* Port of the remaining unit-reachable cases of tests/network/q2/network.test.ts:
 * rerelease gunskin and KEX float delta angles, client moves, channel peers over a
 * loopback hub, challenge negotiation, KEX seats, Q2PRO 1026 fields, rerelease
 * compression and lost delta frames, and the IPX packet ceiling. Values are the
 * TS test's own expectations. GPL-2.0-or-later. */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
#include "qa/network/common/loopback.h"
#include "qa/network/q2.h"
#include "qa/network/q2/adapters.h"
#include "qa/network/q2/client_messages.h"
#include "qa/network/q2/handshake.h"
#include "qa/network/q2/server_write.h"

static int checks, failures;
#define CHECK(...) do { checks++; if (!(__VA_ARGS__)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #__VA_ARGS__); } } while (0)

static qa_q2_protocol_identity identity(const char *kind, double version, double revision) {
  qa_q2_protocol_identity p;
  memset(&p, 0, sizeof p);
  p.kind = kind; p.version = version; p.revision = revision;
  return p;
}
static qa_q2_server_message_options reader_options(double max_config_strings) {
  qa_q2_server_message_options options;
  memset(&options, 0, sizeof options);
  options.max_config_strings = max_config_strings; options.inventory_slots = 256;
  return options;
}
static uint8_t area_bits_storage[2] = {7, 0};
/* TS packetFrame(player, entities, serverFrame): borrowed storage, not freed. */
static qa_q2_wire_frame packet_frame(const qa_q2_wire_player *player, qa_q2_wire_entity *entities, size_t count, double server_frame) {
  qa_q2_wire_frame frame;
  memset(&frame, 0, sizeof frame);
  frame.server_frame = server_frame; frame.delta_frame = -1; frame.suppressed_count = 0;
  frame.area_bits = area_bits_storage; frame.area_bits_length = 2;
  frame.player = *player; frame.entities = entities; frame.entity_count = count;
  return frame;
}
/* encodeQ2Frame then reader.read: the frame record's owned frame, copied out. */
static bool decoded_frame(qa_q2_server_message_reader *reader, qa_q2_wire_codec *wire, const qa_q2_wire_frame *frame,
    const qa_q2_wire_frame *old, qa_q2_wire_frame *out) {
  qa_q2_baseline_list baselines;
  memset(&baselines, 0, sizeof baselines);
  size_t length = 0;
  uint8_t *bytes = qa_q2_encode_frame(wire, frame, old, &baselines, 4, &length);
  if (bytes == NULL) return false;
  qa_q2_server_record *records = NULL;
  size_t count = 0;
  bool ok = qa_q2_server_message_reader_read(reader, bytes, length, &records, &count);
  free(bytes);
  bool found = false;
  for (size_t i = 0; ok && i < count && !found; i++)
    if (records[i].event.kind == QA_Q2_SERVER_FRAME) found = qa_q2_wire_frame_clone(&records[i].event.frame, out);
  qa_q2_server_records_free(records, count);
  return found;
}
static size_t read_events(qa_q2_server_message_reader *reader, qa_q2_wire_codec *wire, const qa_q2_server_event *event,
    qa_q2_server_record **records) {
  size_t length = 0, count = 0;
  uint8_t *bytes = qa_q2_encode_server_event(wire, event, &length);
  CHECK(bytes != NULL);
  CHECK(qa_q2_server_message_reader_read(reader, bytes, length, records, &count));
  free(bytes);
  return count;
}

static void test_rerelease_gunskin_and_kex_angles(void) {
  qa_q2_protocol_identity protocol = identity("q2-rerelease", 1038, 0);
  qa_q2_wire_codec wire;
  qa_q2_server_message_reader reader;
  CHECK(qa_q2_wire_codec_init(&wire, protocol) && qa_q2_server_message_reader_init(&reader, protocol, reader_options(32767)));
  qa_q2_wire_player player, changed;
  qa_q2_wire_player_init(&player);
  player.gunindex = 4;
  qa_q2_wire_frame first = packet_frame(&player, NULL, 0, 1), decoded;
  CHECK(decoded_frame(&reader, &wire, &first, NULL, &decoded));
  qa_q2_wire_frame_free(&decoded);
  qa_q2_wire_player_init(&changed);
  changed.gunindex = 4; changed.gunskin = 5;
  qa_q2_wire_frame second = packet_frame(&changed, NULL, 0, 2);
  CHECK(decoded_frame(&reader, &wire, &second, &first, &decoded) && decoded.player.gunskin == 5);
  qa_q2_wire_frame_free(&decoded);
  qa_q2_server_message_reader_destroy(&reader);
  qa_q2_wire_codec_destroy(&wire);

  qa_q2_protocol_identity kex_protocol = identity("q2-kex", 2023, 0);
  CHECK(qa_q2_wire_codec_init(&wire, kex_protocol) && qa_q2_server_message_reader_init(&reader, kex_protocol, reader_options(32767)));
  qa_q2_wire_player kex_player;
  qa_q2_wire_player_init(&kex_player);
  kex_player.pmove.delta_angle_float = true;
  kex_player.pmove.delta_angles_f[1] = 1.9f;
  kex_player.gunrate = 12; kex_player.team_id = 2;
  qa_q2_wire_entity entity;
  qa_q2_wire_entity_init(&entity);
  entity.number = 9; entity.scale = 2; entity.instance_bits = 12; entity.owner = 4; entity.old_frame = 77; entity.morefx = 0x80000000u;
  qa_q2_wire_frame kex_frame = packet_frame(&kex_player, &entity, 1, 1);
  CHECK(decoded_frame(&reader, &wire, &kex_frame, NULL, &decoded));
  double stats[64];
  qa_q2_rerelease_player_state rerelease;
  qa_q2_to_rerelease_player(&decoded.player, stats, &rerelease);
  CHECK(rerelease.movement.delta_angles.y == (double)(float)1.9);
  CHECK(decoded.player.team_id == 2 && decoded.entity_count == 1);
  if (decoded.entity_count == 1)
    CHECK(decoded.entities[0].owner == 4 && decoded.entities[0].old_frame == 77 && decoded.entities[0].morefx == 0x80000000u);
  qa_q2_wire_frame_free(&decoded);
  qa_q2_server_message_reader_destroy(&reader);
  qa_q2_wire_codec_destroy(&wire);
}

static int replay_sentinel;
static double replay_steps[8]; static size_t replay_count;
static void replay_think(void *ctx, const qa_q2_wire_usercmd *command) {
  CHECK(ctx == &replay_sentinel);
  if (replay_count < 8) replay_steps[replay_count++] = command->upmove;
}
typedef struct read_scope { qa_q2_wire_codec *wire; const uint8_t *bytes; size_t length; double sequence; } read_scope;
static void read_body(void *ctx) {
  read_scope *r = ctx;
  qa_q2_client_record *records = NULL;
  size_t count = 0;
  if (qa_q2_read_client_messages(r->wire, r->bytes, r->length, r->sequence, 1, &records, &count))
    qa_q2_client_records_free(records, count);
}
static void test_client_moves(void) {
  qa_q2_wire_codec wire;
  CHECK(qa_q2_wire_codec_init(&wire, identity("q2-classic", 34, 0)));
  qa_q2_wire_usercmd commands[3];
  for (int i = 0; i < 3; i++) qa_q2_wire_usercmd_init(&commands[i]);
  commands[2].msec = 16; commands[2].forwardmove = 200; commands[2].upmove = 200; commands[2].angles[1] = 100;
  size_t length = 0;
  uint8_t *packet = qa_q2_encode_move(&wire, 10, 7, commands, &length);
  qa_q2_client_record *records = NULL;
  size_t count = 0;
  CHECK(packet != NULL && qa_q2_read_client_messages(&wire, packet, length, 10, 1, &records, &count));
  CHECK(count >= 1 && records[0].event.kind == QA_Q2_CLIENT_MOVE && records[0].event.commands[2].upmove == 200);
  qa_q2_client_records_free(records, count);
  read_scope scope = {&wire, packet, length, 11};
  qa_caught_error caught;
  CHECK(!qa_try(read_body, &scope, &caught) && strstr(caught.message, "checksum") != NULL);
  free(packet);
  qa_q2_wire_codec_destroy(&wire);

  qa_q2_wire_codec pro;
  CHECK(qa_q2_wire_codec_init(&pro, identity("q2-q2pro", 36, 1019)));
  qa_q2_clc_batch_move_frame frame = {&commands[2], 1};
  uint8_t *batch = qa_q2_encode_batch_move(&pro, true, 7, &frame, 1, &length);
  CHECK(batch != NULL && qa_q2_read_client_messages(&pro, batch, length, 10, 1, &records, &count));
  CHECK(count >= 1 && records[0].event.kind == QA_Q2_CLIENT_BATCH_MOVE);
  if (count >= 1 && records[0].event.kind == QA_Q2_CLIENT_BATCH_MOVE) {
    const qa_q2_clc_batch_move *decoded = &records[0].event.batch;
    CHECK(decoded->frame_count >= 1 && decoded->frames[0].cmd_count >= 1 && decoded->frames[0].cmds[0].upmove == 200);
    qa_q2_command_replay replay;
    qa_q2_command_replay_init(&replay);
    replay_count = 0;
    qa_q2_command_replay_execute(&replay, &records[0].event, 0, replay_think, &replay_sentinel);
    CHECK(replay_count == 1 && replay_steps[0] == 200);
  }
  qa_q2_client_records_free(records, count);
  free(batch);
  qa_q2_wire_codec_destroy(&pro);
}

static void test_channel_peers(void) {
  qa_loopback_hub hub;
  CHECK(qa_loopback_hub_init(&hub, NULL, NULL, NULL));
  qa_loopback_transport *left = qa_loopback_hub_bind(&hub, "q2-client"), *right = qa_loopback_hub_bind(&hub, "q2-server");
  qa_datagram_transport left_transport = qa_loopback_transport_as_transport(left);
  qa_datagram_transport right_transport = qa_loopback_transport_as_transport(right);
  qa_q2_channel_options options;
  memset(&options, 0, sizeof options);
  options.protocol = identity("q2-rerelease", 1038, 0); options.channel_new = true; options.qport = 17;
  options.has_payload_bytes = true; options.payload_bytes = 512;
  static qa_q2_channel client, server;
  options.side_server = false;
  qa_q2_channel_init(&client, &options);
  options.side_server = true;
  qa_q2_channel_init(&server, &options);
  uint8_t bytes[1500];
  memset(bytes, 45, sizeof bytes);
  qa_q2_channel_queue_reliable(&client, bytes, sizeof bytes);
  uint8_t *received = NULL;
  size_t received_length = 0;
  int rounds = 0;
  do {
    CHECK(qa_q2_channel_send(&client, &left_transport, &right->address, NULL, 0, 100));
    qa_receive_event packet;
    memset(&packet, 0, sizeof packet);
    if (!qa_loopback_transport_poll(right, &packet) || packet.kind != QA_RECV_PACKET) { CHECK(!"packet missing"); break; }
    qa_q2_channel_receive_result result;
    memset(&result, 0, sizeof result);
    CHECK(qa_q2_channel_receive(&server, packet.payload, packet.length, 100, &result));
    if (result.kind == QA_Q2_CHANNEL_MESSAGE) {
      free(received);
      received = result.bytes; received_length = result.bytes_length; result.bytes = NULL;
    }
    qa_q2_channel_receive_result_free(&result);
    qa_receive_event_destroy(&packet);
  } while (qa_q2_channel_get_fragment_pending(&client) && ++rounds < 64);
  CHECK(received != NULL && received_length == sizeof bytes && memcmp(received, bytes, sizeof bytes) == 0);
  free(received);
  CHECK(qa_q2_channel_send(&server, &right_transport, &left->address, NULL, 0, 101));
  qa_receive_event ack;
  memset(&ack, 0, sizeof ack);
  CHECK(qa_loopback_transport_poll(left, &ack) && ack.kind == QA_RECV_PACKET);
  qa_q2_channel_receive_result result;
  memset(&result, 0, sizeof result);
  CHECK(qa_q2_channel_receive(&client, ack.payload, ack.length, 101, &result));
  qa_q2_channel_receive_result_free(&result);
  qa_receive_event_destroy(&ack);
  CHECK(!qa_q2_channel_get_reliable_pending(&client));
  qa_q2_channel_destroy(&client);
  qa_q2_channel_destroy(&server);
  qa_loopback_hub_close(&hub);
  qa_loopback_hub_destroy(&hub);
}

static int userinfo_sentinel, random_sentinel;
static const char *player_userinfo(void *ctx) { CHECK(ctx == &userinfo_sentinel); return "\\name\\player"; }
static double fixed_random(void *ctx) { CHECK(ctx == &random_sentinel); return 123; }
static bool read_oob(const uint8_t *bytes, size_t length, qa_q2_connectionless_message *out) {
  memset(out, 0, sizeof *out);
  return bytes != NULL && qa_q2_read_out_of_band(bytes, length, false, out);
}
static void test_challenge_negotiation(void) {
  qa_net_address remote;
  CHECK(qa_net_make_loopback(&remote, "q2-server"));
  qa_q2_protocol_identity protocol = identity("q2-rerelease", 1038, 0);
  qa_q2_client_handshake client;
  CHECK(qa_q2_client_handshake_init(&client, &remote, &protocol, 1, 0x1234, player_userinfo, &userinfo_sentinel,
                                    true, 4086, false, 0, NULL, NULL));
  qa_q2_challenge_table table;
  CHECK(qa_q2_challenge_table_init(&table, fixed_random, &random_sentinel, 0));
  size_t length = 0;
  uint8_t *bytes = qa_q2_client_handshake_poll(&client, 0, &length);
  qa_q2_connectionless_message message;
  CHECK(read_oob(bytes, length, &message) && strcmp(message.command, "getchallenge") == 0);
  qa_q2_connectionless_message_free(&message); free(bytes);
  bytes = qa_q2_challenge_table_reply(&table, &remote, 1, &protocol, 1, &length);
  CHECK(read_oob(bytes, length, &message));
  qa_q2_client_handshake_receive(&client, &remote, &message);
  qa_q2_connectionless_message_free(&message); free(bytes);
  bytes = qa_q2_client_handshake_poll(&client, 1, &length);
  CHECK(read_oob(bytes, length, &message));
  qa_q2_connect_request request;
  memset(&request, 0, sizeof request);
  CHECK(qa_q2_read_connect(&message, &request));
  CHECK(request.qport == 0x34 && request.payload_bytes == 4086 && request.channel_new);
  CHECK(request.userinfo != NULL && strcmp(request.userinfo, "\\name\\player") == 0);
  qa_q2_connect_request_free(&request);
  qa_q2_connectionless_message_free(&message); free(bytes);
  bytes = qa_q2_out_of_band("client_connect", false, &length);
  CHECK(read_oob(bytes, length, &message));
  qa_q2_client_handshake_receive(&client, &remote, &message);
  qa_q2_connectionless_message_free(&message); free(bytes);
  CHECK(client.kind == QA_Q2_HANDSHAKE_CONNECTED);
  qa_q2_challenge_table_destroy(&table);
  qa_q2_client_handshake_destroy(&client);
  qa_net_address_destroy(&remote);
}

static void test_kex_seats(void) {
  qa_q2_protocol_identity protocol = identity("q2-kex", 2023, 0);
  qa_q2_wire_codec writer;
  qa_q2_server_message_reader reader;
  CHECK(qa_q2_wire_codec_init(&writer, protocol) && qa_q2_server_message_reader_init(&reader, protocol, reader_options(32767)));
  qa_q2_server_event data;
  memset(&data, 0, sizeof data);
  data.kind = QA_Q2_SERVER_DATA;
  data.data.servercount = 1; data.data.attractloop = false; snprintf(data.data.gamedir, sizeof data.data.gamedir, "baseq2");
  data.data.clientnum = 0; data.data.has_clientnums = true; data.data.clientnums[0] = 0; data.data.clientnums[1] = 1;
  data.data.clientnum_count = 2; snprintf(data.data.levelname, sizeof data.data.levelname, "test");
  data.data.server_state = 0; data.data.has_server_fps = true; data.data.server_fps = 40;
  qa_q2_server_record *records = NULL;
  size_t count = read_events(&reader, &writer, &data, &records);
  qa_q2_server_records_free(records, count);
  qa_q2_wire_player zero, one, zero_later, one_later;
  qa_q2_wire_player_init(&zero); qa_q2_wire_player_init(&one);
  zero.stats[0] = 100; one.stats[0] = 75;
  uint8_t split_area = 2;
  qa_q2_split_player split = {&split_area, 1, one};
  qa_q2_wire_frame frame = packet_frame(&zero, NULL, 0, 1), decoded;
  frame.has_split = true; frame.split_players = &split; frame.split_count = 1;
  CHECK(decoded_frame(&reader, &writer, &frame, NULL, &decoded));
  qa_q2_wire_frame_free(&decoded);
  qa_q2_wire_player_init(&zero_later); qa_q2_wire_player_init(&one_later);
  zero_later.stats[0] = 50; one_later.stats[0] = 25;
  split.player = one_later;
  frame = packet_frame(&zero_later, NULL, 0, 2);
  frame.has_split = true; frame.split_players = &split; frame.split_count = 1;
  CHECK(decoded_frame(&reader, &writer, &frame, NULL, &decoded));
  qa_q2_wire_frame_free(&decoded);
  qa_q2_frame_history *history = qa_q2_server_message_reader_get_history(&reader, 0);
  qa_q2_wire_frame *first = qa_q2_frame_history_get(history, 1), *second = qa_q2_frame_history_get(history, 2);
  CHECK(first != NULL && second != NULL);
  if (first && second) {
    CHECK(first->player.stats[0] == 100 && first->split_count == 1 && first->split_players[0].player.stats[0] == 75);
    CHECK(second->player.stats[0] == 50 && second->split_count == 1 && second->split_players[0].player.stats[0] == 25);
  }
  qa_q2_server_event seat;
  memset(&seat, 0, sizeof seat);
  seat.kind = QA_Q2_SERVER_SEAT; seat.seat = 1;
  count = read_events(&reader, &writer, &seat, &records);
  qa_q2_server_records_free(records, count);
  qa_q2_server_event sound;
  memset(&sound, 0, sizeof sound);
  sound.kind = QA_Q2_SERVER_SOUND;
  sound.sound.index = 300; sound.sound.has_position = true; sound.sound.position = (qa_vec3){1.9, -12, 3};
  sound.sound.volume = 1; sound.sound.attenuation = 1;
  count = read_events(&reader, &writer, &sound, &records);
  CHECK(count >= 1 && records[0].seat == 1 && records[0].event.kind == QA_Q2_SERVER_SOUND);
  if (count >= 1 && records[0].event.kind == QA_Q2_SERVER_SOUND)
    CHECK(records[0].event.sound.position.x == (double)(float)1.9);
  qa_q2_server_records_free(records, count);
  qa_q2_server_message_reader_destroy(&reader);
  qa_q2_wire_codec_destroy(&writer);
}

static void test_q2pro_1026(void) {
  qa_q2_protocol_identity protocol = identity("q2-q2pro", 36, 1026);
  qa_q2_wire_codec wire;
  qa_q2_server_message_reader reader;
  CHECK(qa_q2_wire_codec_init(&wire, protocol) && qa_q2_server_message_reader_init(&reader, protocol, reader_options(13630)));
  qa_q2_server_event data;
  memset(&data, 0, sizeof data);
  data.kind = QA_Q2_SERVER_DATA;
  data.data.servercount = 1; snprintf(data.data.gamedir, sizeof data.data.gamedir, "baseq2");
  data.data.clientnum = 0; snprintf(data.data.levelname, sizeof data.data.levelname, "unit1");
  data.data.server_state = 2; data.data.has_wire_flags = true; data.data.wire_flags = 24;
  qa_q2_server_record *records = NULL;
  size_t count = read_events(&reader, &wire, &data, &records);
  qa_q2_server_records_free(records, count);
  qa_q2_wire_player player;
  qa_q2_wire_player_init(&player);
  player.pmove.origin[0] = 400000; player.pmove.origin[1] = -400000; player.pmove.origin[2] = 80;
  player.pmove.velocity[2] = 40000; player.pmove.pm_time = 1000; player.pmove.pm_flags = 512;
  player.gunindex = 2000; player.gunskin = 5; player.clientnum = 300; player.stats[63] = 77; player.damage_blend[0] = 1;
  qa_q2_pro_player_fog fog = {{2, 3, 4}, 1234, 60000, 50000, 456, {9, 8, 7}, {6, 5, 4}, -400000, 400000};
  player.q2pro_fog = fog;
  qa_q2_wire_entity entity;
  qa_q2_wire_entity_init(&entity);
  entity.number = 2000; entity.modelindex = 1500;
  entity.origin[0] = 50000; entity.origin[1] = -50000; entity.origin[2] = 10; entity.angles[1] = 22.5f;
  entity.morefx = 0x80000000u; entity.sound = 1000; entity.loop_volume = 0.5; entity.loop_attenuation = -1; entity.scale = 2;
  qa_q2_wire_frame first = packet_frame(&player, &entity, 1, 1), decoded;
  CHECK(decoded_frame(&reader, &wire, &first, NULL, &decoded));
  CHECK(decoded.player.pmove.origin[0] == 400000 && decoded.player.pmove.origin[1] == -400000 && decoded.player.pmove.origin[2] == 80);
  CHECK(decoded.player.pmove.pm_time == 1000 && decoded.player.pmove.pm_flags == 512);
  CHECK(decoded.player.clientnum == 300 && decoded.player.stats[63] == 77 && decoded.player.gunskin == 5);
  CHECK(memcmp(&decoded.player.q2pro_fog, &fog, sizeof fog) == 0);
  CHECK(decoded.entity_count == 1);
  if (decoded.entity_count == 1)
    CHECK(decoded.entities[0].origin[0] == 50000 && decoded.entities[0].morefx == 0x80000000u && decoded.entities[0].loop_attenuation == -1);
  qa_q2_wire_frame_free(&decoded);
  qa_q2_wire_player next;
  qa_q2_wire_player_init(&next);
  next.pmove.origin[0] = 400008; next.pmove.origin[1] = -400008; next.pmove.origin[2] = 88; next.clientnum = 300;
  qa_q2_wire_frame second = packet_frame(&next, NULL, 0, 2);
  CHECK(decoded_frame(&reader, &wire, &second, &first, &decoded));
  CHECK(decoded.player.pmove.origin[0] == 400008 && decoded.player.pmove.origin[1] == -400008 && decoded.player.pmove.origin[2] == 88);
  CHECK(decoded.player.stats[63] == 0 && decoded.player.damage_blend[0] == 0);
  qa_q2_wire_frame_free(&decoded);
  qa_q2_server_event sound;
  memset(&sound, 0, sizeof sound);
  sound.kind = QA_Q2_SERVER_SOUND;
  sound.sound.index = 1000; sound.sound.entity = 2000; sound.sound.channel = 1;
  sound.sound.has_position = true; sound.sound.position = (qa_vec3){50000, -50000, 10};
  sound.sound.volume = 1; sound.sound.attenuation = 1;
  count = read_events(&reader, &wire, &sound, &records);
  CHECK(count >= 1 && records[0].event.kind == QA_Q2_SERVER_SOUND && records[0].event.sound.position.x == 50000);
  qa_q2_server_records_free(records, count);
  qa_q2_server_message_reader_destroy(&reader);
  qa_q2_wire_codec_destroy(&wire);
}

static void test_rerelease_compression(void) {
  qa_q2_protocol_identity protocol = identity("q2-rerelease", 1038, 0);
  qa_q2_wire_codec wire;
  qa_q2_server_message_reader reader;
  CHECK(qa_q2_wire_codec_init(&wire, protocol) && qa_q2_server_message_reader_init(&reader, protocol, reader_options(32767)));
  qa_q2_channel_options options;
  memset(&options, 0, sizeof options);
  options.protocol = protocol; options.channel_new = true; options.qport = 1;
  static qa_q2_channel server, client;
  options.side_server = true; options.compress = true;
  qa_q2_channel_init(&server, &options);
  options.side_server = false; options.compress = false;
  qa_q2_channel_init(&client, &options);
  char text[601];
  for (int i = 0; i < 100; i++) memcpy(text + i * 6, "quake ", 6);
  text[600] = '\0';
  qa_q2_server_event print;
  memset(&print, 0, sizeof print);
  print.kind = QA_Q2_SERVER_PRINT; print.level = 1; print.text = text;
  size_t length = 0;
  uint8_t *message = qa_q2_encode_server_event(&wire, &print, &length);
  qa_q2_channel_queue_reliable(&server, message, length);
  free(message);
  uint8_t *packet = qa_q2_channel_transmit(&server, NULL, 0, 0, &length);
  qa_q2_channel_receive_result received;
  memset(&received, 0, sizeof received);
  CHECK(packet != NULL && qa_q2_channel_receive(&client, packet, length, 0, &received));
  free(packet);
  CHECK(received.kind == QA_Q2_CHANNEL_MESSAGE && received.bytes_length > 0 && received.bytes[0] == 34);
  qa_q2_server_record *records = NULL;
  size_t count = 0;
  if (received.kind == QA_Q2_CHANNEL_MESSAGE) {
    CHECK(qa_q2_server_message_reader_read(&reader, received.bytes, received.bytes_length, &records, &count));
    CHECK(count >= 1 && records[0].event.kind == QA_Q2_SERVER_PRINT);
    qa_q2_server_records_free(records, count);
  }
  qa_q2_channel_receive_result_free(&received);
  qa_q2_wire_player empty;
  qa_q2_wire_player_init(&empty);
  qa_q2_wire_frame first = packet_frame(&empty, NULL, 0, 1), next = packet_frame(&empty, NULL, 0, 2), third = packet_frame(&empty, NULL, 0, 3), decoded;
  CHECK(decoded_frame(&reader, &wire, &next, &first, &decoded) && decoded.has_valid && !decoded.valid);
  qa_q2_wire_frame_free(&decoded);
  CHECK(decoded_frame(&reader, &wire, &third, NULL, &decoded) && decoded.has_valid && decoded.valid);
  qa_q2_wire_frame_free(&decoded);
  qa_q2_channel_destroy(&server);
  qa_q2_channel_destroy(&client);
  qa_q2_server_message_reader_destroy(&reader);
  qa_q2_wire_codec_destroy(&wire);
}

static void test_ipx_ceiling(void) {
  qa_q2_channel_options options;
  memset(&options, 0, sizeof options);
  options.protocol = identity("q2-q2pro", 36, 1026); options.channel_new = true; options.qport = 17;
  options.has_max_datagram_bytes = true; options.max_datagram_bytes = 1394;
  static qa_q2_channel sender, receiver, classic, rerelease;
  options.side_server = false;
  qa_q2_channel_init(&sender, &options);
  qa_q2_channel_options receiver_options = options;
  receiver_options.side_server = true; receiver_options.has_max_datagram_bytes = false;
  qa_q2_channel_init(&receiver, &receiver_options);
  uint8_t reliable[4096];
  for (size_t i = 0; i < sizeof reliable; i++) reliable[i] = (uint8_t)(i & 255);
  qa_q2_channel_queue_reliable(&sender, reliable, sizeof reliable);
  size_t length = 0;
  uint8_t *packet = qa_q2_channel_transmit(&sender, NULL, 0, 0, &length);
  uint8_t *received = NULL;
  size_t received_length = 0;
  bool within = true;
  for (int rounds = 0; packet != NULL && rounds < 64; rounds++) {
    if (length > 1394) within = false;
    qa_q2_channel_receive_result result;
    memset(&result, 0, sizeof result);
    CHECK(qa_q2_channel_receive(&receiver, packet, length, 0, &result));
    if (result.kind == QA_Q2_CHANNEL_MESSAGE) {
      free(received);
      received = result.bytes; received_length = result.bytes_length; result.bytes = NULL;
    }
    qa_q2_channel_receive_result_free(&result);
    free(packet);
    packet = qa_q2_channel_next_fragment(&sender, 0, &length);
  }
  free(packet);
  CHECK(within);
  CHECK(received != NULL && received_length == sizeof reliable && memcmp(received, reliable, sizeof reliable) == 0);
  free(received);
  qa_q2_channel_options classic_options;
  memset(&classic_options, 0, sizeof classic_options);
  classic_options.protocol = identity("q2-classic", 34, 0); classic_options.channel_new = false; classic_options.qport = 17;
  classic_options.has_max_datagram_bytes = true; classic_options.max_datagram_bytes = 1394;
  qa_q2_channel_init(&classic, &classic_options);
  CHECK(classic.capacity == 1384);
  uint8_t *full = calloc((size_t)classic.capacity, 1), unreliable[4096];
  memset(unreliable, 0, sizeof unreliable);
  qa_q2_channel_queue_reliable(&classic, full, (size_t)classic.capacity);
  packet = qa_q2_channel_transmit(&classic, unreliable, 100, 0, &length);
  CHECK(packet != NULL && length == 1394);
  free(packet); free(full);
  qa_q2_channel_options rerelease_options = classic_options;
  rerelease_options.protocol = identity("q2-rerelease", 1038, 0); rerelease_options.side_server = true;
  qa_q2_channel_init(&rerelease, &rerelease_options);
  full = calloc((size_t)rerelease.capacity, 1);
  qa_q2_channel_queue_reliable(&rerelease, full, (size_t)rerelease.capacity);
  packet = qa_q2_channel_transmit(&rerelease, unreliable, sizeof unreliable, 0, &length);
  CHECK(packet != NULL && length <= 1394);
  free(packet); free(full);
  qa_q2_channel_destroy(&sender); qa_q2_channel_destroy(&receiver);
  qa_q2_channel_destroy(&classic); qa_q2_channel_destroy(&rerelease);
}

int main(void) {
  test_rerelease_gunskin_and_kex_angles();
  test_client_moves();
  test_channel_peers();
  test_challenge_negotiation();
  test_kex_seats();
  test_q2pro_1026();
  test_rerelease_compression();
  test_ipx_ceiling();
  printf("test_q2_network_cases: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
