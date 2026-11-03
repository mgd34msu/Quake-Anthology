/* Port of the frame test in tests/network/q2/network.test.ts over its seven wire profiles,
 * plus rich frames, with encodeQ2Frame bytes captured from TS. GPL-2.0-or-later. */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/network/q2/codec.h"
#include "qa/network/q2/frames.h"
#include "qa/network/q2/server_messages.h"
#include "q2_depth_corpus.h"

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
  fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static void set3(qa_q2_wire_vec3 v, double x, double y, double z) { v[0] = (float)x; v[1] = (float)y; v[2] = (float)z; }
static uint8_t area_bits[2] = {7, 0};
static qa_q2_wire_frame make_frame(const qa_q2_wire_player *player, qa_q2_wire_entity *entities, size_t count, double server_frame) {
  qa_q2_wire_frame f;
  memset(&f, 0, sizeof f);
  f.server_frame = server_frame; f.delta_frame = -1; f.suppressed_count = 0;
  f.area_bits = area_bits; f.area_bits_length = 2;
  f.player = *player;
  f.entities = entities; f.entity_count = count;
  return f;
}
static void check_bytes(const char *label, const char *kind, uint8_t *got, size_t length, const char *hex) {
  checks++;
  size_t want = strlen(hex) / 2;
  bool same = got && want == length;
  for (size_t i = 0; same && i < want; i++) {
    unsigned v;
    sscanf(hex + 2 * i, "%2x", &v);
    if (got[i] != v) same = false;
  }
  if (!same) {
    failures++;
    fprintf(stderr, "FAIL %s %s\n got ", kind, label);
    for (size_t i = 0; got && i < length; i++) fprintf(stderr, "%02x", got[i]);
    fprintf(stderr, "\n exp %s\n", hex);
  }
  free(got);
}
static void rich_player(qa_q2_wire_player *p) {
  memset(p, 0, sizeof *p);
  p->pmove.pm_type = 2;
  p->pmove.origin[0] = 1600; p->pmove.origin[1] = -800; p->pmove.origin[2] = 2400;
  p->pmove.velocity[0] = 40; p->pmove.velocity[1] = -50; p->pmove.velocity[2] = 60;
  set3(p->pmove.origin_f, 200, -100, 300); set3(p->pmove.velocity_f, 5, -6, 7);
  p->pmove.pm_flags = 3; p->pmove.pm_time = 9; p->pmove.gravity = 800;
  p->pmove.delta_angles[0] = 100; p->pmove.delta_angles[1] = -200; p->pmove.delta_angles[2] = 300;
  p->pmove.viewheight = 22;
  set3(p->viewangles, 10, 20, 30); set3(p->viewoffset, 1, 2, 22); set3(p->kick_angles, 1.5, -1.5, 2);
  set3(p->gunangles, 0.5, 1, -1); set3(p->gunoffset, 1, -1, 0.5);
  p->gunindex = 7; p->gunskin = 2; p->gunframe = 12; p->gunrate = 20;
  p->blend[0] = 0.5f; p->blend[1] = 0.25f; p->blend[2] = 0.125f; p->blend[3] = 0.75f;
  p->damage_blend[0] = 0.25f; p->damage_blend[1] = 0.5f; p->damage_blend[2] = 0.125f; p->damage_blend[3] = 0.5f;
  p->fov = 100; p->rdflags = 4; p->team_id = 1;
  p->stats[0] = 5; p->stats[2] = 100; p->stats[31] = -9;
}
static void rich_entity(qa_q2_wire_entity *e, double number) {
  memset(e, 0, sizeof *e);
  e->number = number;
  set3(e->origin, 320.5, -160.25, 8); set3(e->angles, 11, 22, 33); set3(e->old_origin, 300, -150, 4);
  e->modelindex = 5; e->modelindex2 = 6; e->modelindex3 = 7; e->modelindex4 = 8;
  e->frame = 300; e->skinnum = 2; e->effects = 0x10000; e->renderfx = 64; e->solid = 31; e->sound = 4; e->event = 2;
  e->alpha = 0.5; e->scale = 1.5;
}
static qa_q2_wire_frame *decoded_frame(qa_q2_server_message_reader *reader, const uint8_t *bytes, size_t length,
    qa_q2_server_record **records, size_t *count) {
  CHECK(qa_q2_server_message_reader_read(reader, bytes, length, records, count));
  for (size_t i = 0; i < *count; i++)
    if ((*records)[i].event.kind == QA_Q2_SERVER_FRAME) return &(*records)[i].event.frame;
  return NULL;
}

int main(void) {
  for (size_t row = 0; row < sizeof q2_rows / sizeof *q2_rows; row++) {
    qa_q2_protocol_identity protocol;
    memset(&protocol, 0, sizeof protocol);
    protocol.kind = q2_rows[row].kind;
    protocol.version = (int64_t)q2_rows[row].version;
    protocol.revision = (int64_t)q2_rows[row].revision;
    qa_q2_wire_codec wire;
    CHECK(qa_q2_wire_codec_init(&wire, protocol));
    qa_q2_baseline_list baselines;
    memset(&baselines, 0, sizeof baselines);
    qa_q2_server_message_options options;
    memset(&options, 0, sizeof options);
    options.max_config_strings = 32767; options.inventory_slots = 256;
    qa_q2_server_message_reader reader;
    CHECK(qa_q2_server_message_reader_init(&reader, protocol, options));

    qa_q2_wire_player player;
    memset(&player, 0, sizeof player);
    player.pmove.origin[0] = 16; player.pmove.origin[1] = -8; player.pmove.origin[2] = 24;
    set3(player.pmove.origin_f, 2, -1, 3);
    player.fov = 90;
    player.stats[2] = 100;
    qa_q2_wire_entity entity;
    memset(&entity, 0, sizeof entity);
    entity.number = 42; set3(entity.origin, 32, -16, 8); entity.modelindex = 5; entity.event = 3; entity.solid = 31;
    qa_q2_wire_frame first = make_frame(&player, &entity, 1, 1);
    size_t length = 0;
    uint8_t *bytes = qa_q2_encode_frame(&wire, &first, NULL, &baselines, 4, &length);

    /* The TS test: decoded frames carry the values, and the transient event resets. */
    qa_q2_server_record *records = NULL;
    size_t record_count = 0;
    qa_q2_wire_frame *decoded = decoded_frame(&reader, bytes, length, &records, &record_count);
    CHECK(decoded != NULL);
    if (decoded) {
      CHECK(decoded->area_bits_length == 2 && decoded->area_bits[0] == 7 && decoded->area_bits[1] == 0);
      CHECK(decoded->entity_count == 1 && decoded->entities[0].origin[0] == 32);
      CHECK(decoded->player.stats[2] == 100);
    }
    qa_q2_server_records_free(records, record_count);
    check_bytes("first", q2_rows[row].kind, bytes, length, q2_rows[row].first);

    qa_q2_wire_entity next_entity;
    memset(&next_entity, 0, sizeof next_entity);
    next_entity.number = 42; set3(next_entity.origin, 32, -16, 8); next_entity.modelindex = 5; next_entity.solid = 31;
    qa_q2_wire_frame next = make_frame(&player, &next_entity, 1, 2);
    bytes = qa_q2_encode_frame(&wire, &next, &first, &baselines, 4, &length);
    decoded = decoded_frame(&reader, bytes, length, &records, &record_count);
    CHECK(decoded && decoded->entity_count == 1 && decoded->entities[0].event == 0);
    qa_q2_server_records_free(records, record_count);
    check_bytes("next", q2_rows[row].kind, bytes, length, q2_rows[row].next);

    qa_q2_wire_frame empty = make_frame(&player, NULL, 0, 3);
    bytes = qa_q2_encode_frame(&wire, &empty, &next, &baselines, 4, &length);
    decoded = decoded_frame(&reader, bytes, length, &records, &record_count);
    CHECK(decoded && decoded->entity_count == 0);
    qa_q2_server_records_free(records, record_count);
    check_bytes("empty", q2_rows[row].kind, bytes, length, q2_rows[row].empty);

    /* Every field set, then a delta over the same frame. TS throws for undeclared Q2PRO extensions. */
    if (q2_rows[row].rich) {
      qa_q2_wire_player rich;
      rich_player(&rich);
      qa_q2_wire_entity rich_entities[2];
      rich_entity(&rich_entities[0], 42);
      rich_entity(&rich_entities[1], 300);
      qa_q2_wire_frame r1 = make_frame(&rich, rich_entities, 2, 4);
      bytes = qa_q2_encode_frame(&wire, &r1, NULL, &baselines, 4, &length);
      check_bytes("rich", q2_rows[row].kind, bytes, length, q2_rows[row].rich);
      qa_q2_wire_player rich2 = rich;
      rich2.pmove.origin[0] = 1700;
      set3(rich2.pmove.origin_f, 212.5, -100, 300);
      rich2.stats[2] = 50;
      set3(rich2.viewangles, 11, 20, 30);
      rich2.fov = 90; rich2.gunframe = 13;
      qa_q2_wire_entity changed;
      rich_entity(&changed, 42);
      set3(changed.origin, 321, -160.25, 9);
      changed.frame = 301; changed.event = 0;
      qa_q2_wire_frame r2 = make_frame(&rich2, &changed, 1, 5);
      bytes = qa_q2_encode_frame(&wire, &r2, &r1, &baselines, 4, &length);
      check_bytes("rich delta", q2_rows[row].kind, bytes, length, q2_rows[row].rich2);
    }
    qa_q2_server_message_reader_destroy(&reader);
    qa_q2_wire_codec_destroy(&wire);
  }
  printf("q2_depth: %d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
