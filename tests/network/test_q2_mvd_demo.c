/* Ports of tests/network/q2/demo-header.test.ts, old-demo.test.ts, mvd-encoding.test.ts,
 * mvd-playback.test.ts, demo-viewpoints.test.ts (unit layer) and mvd-mutation.test.ts (MVD
 * case), with TS bytes and seeded-mutation outcomes from a bun oracle (q2_mvd_demo_corpus.h).
 * GPL-2.0-or-later. */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/network/common/js_number.h"
#include "qa/network/q2/codec.h"
#include "qa/network/q2/codecs/mvd.h"
#include "qa/network/q2/demo.h"
#include "qa/network/q2/frames.h"
#include "qa/network/q2/message.h"
#include "qa/network/q2/mvd_encoding.h"
#include "qa/network/q2/mvd_playback.h"
#include "qa/network/q2/mvd_profile.h"
#include "qa/network/q2/mvd_recording.h"
#include "qa/network/q2/server_messages.h"
#include "q2_mvd_demo_corpus.h"

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
  fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

#define SENTINEL 0x5eed

typedef struct bytes { uint8_t *data; size_t length; } bytes;
static bytes unhex(const char *hex) {
  bytes out = {NULL, strlen(hex) / 2};
  out.data = malloc(out.length > 0 ? out.length : 1);
  for (size_t i = 0; i < out.length; i++) {
    unsigned v = 0;
    sscanf(hex + 2 * i, "%2x", &v);
    out.data[i] = (uint8_t)v;
  }
  return out;
}
static bool equals_hex(const uint8_t *data, size_t length, const char *hex) {
  bytes expected = unhex(hex);
  bool same = expected.length == length && memcmp(expected.data, data, length) == 0;
  free(expected.data);
  return same;
}
static bool contains(const char *text, const char *fragment) { return strstr(text, fragment) != NULL; }

/* ---- calls run under qa_try ---- */
typedef enum call_kind {
  CALL_DEMO_HEADER, CALL_READ_DEMO, CALL_MVD_READ, CALL_SELECT, CALL_READER, CALL_MVD_HEADER,
  CALL_PLAY_DEMO
} call_kind;
typedef struct call {
  call_kind kind;
  const uint8_t *data;
  size_t length;
  qa_q2_mvd_playback *playback;
  qa_q2_server_message_reader *reader;
  double number;
  bool ok;
  qa_q2_demo_header header;
  qa_q2_mvd_header mvd_header;
  qa_q2_demo_record *records;
  qa_q2_server_record *server_records;
  qa_q2_demo_step *steps;
  size_t count;
} call;
static void call_body(void *arg) {
  call *c = arg;
  switch (c->kind) {
    case CALL_DEMO_HEADER: c->ok = qa_q2_read_demo_header(c->data, c->length, &c->header); break;
    case CALL_READ_DEMO: c->ok = qa_q2_read_demo(c->data, c->length, &c->records, &c->count); break;
    case CALL_MVD_READ:
      c->ok = qa_q2_mvd_playback_read(c->playback, c->data, c->length, &c->server_records, &c->count);
      break;
    case CALL_SELECT: qa_q2_mvd_playback_select_player(c->playback, c->number); c->ok = true; break;
    case CALL_READER:
      c->ok = qa_q2_server_message_reader_read(c->reader, c->data, c->length, &c->server_records, &c->count);
      break;
    case CALL_MVD_HEADER: c->ok = qa_q2_read_mvd_header(c->data, c->length, &c->mvd_header); break;
    case CALL_PLAY_DEMO:
      c->ok = qa_q2_play_demo(c->data, c->length, c->reader, &c->steps, &c->count);
      break;
  }
}
/* Runs the call; true when it returned, else `caught` holds the raise. */
static bool attempt(call *c, qa_caught_error *caught) {
  c->ok = false;
  c->records = NULL;
  c->server_records = NULL;
  c->steps = NULL;
  c->count = 0;
  memset(&c->mvd_header, 0, sizeof c->mvd_header);
  return qa_try(call_body, c, caught);
}
static void call_release(call *c) {
  if (c->kind == CALL_READ_DEMO) qa_q2_demo_records_free(c->records, c->count);
  if (c->kind == CALL_MVD_READ || c->kind == CALL_READER) qa_q2_server_records_free(c->server_records, c->count);
  if (c->kind == CALL_MVD_HEADER) qa_q2_mvd_header_free(&c->mvd_header);
  if (c->kind == CALL_PLAY_DEMO) qa_q2_demo_steps_free(c->steps, c->count);
  c->records = NULL;
  c->server_records = NULL;
  c->steps = NULL;
  c->count = 0;
}
/* True when the call raised with `fragment` in its message. */
static bool raises(call *c, const char *fragment) {
  qa_caught_error caught;
  if (attempt(c, &caught)) {
    call_release(c);
    fprintf(stderr, "  expected a raise containing \"%s\"\n", fragment);
    return false;
  }
  if (!contains(caught.message, fragment)) {
    fprintf(stderr, "  raised \"%s: %s\", expected \"%s\"\n", caught.name, caught.message, fragment);
    return false;
  }
  return true;
}

/* ---- demo-header.test.ts ---- */
typedef struct protocol_case { const char *kind; double version, revision; } protocol_case;
static const protocol_case PROTOCOLS[] = {
  {"q2-classic", 34, 0}, {"q2-r1q2", 35, 1903}, {"q2-r1q2", 35, 1904}, {"q2-r1q2", 35, 1905},
  {"q2-q2pro", 36, 1015}, {"q2-q2pro", 36, 1017}, {"q2-q2pro", 36, 1018}, {"q2-q2pro", 36, 1019},
  {"q2-q2pro", 36, 1020}, {"q2-q2pro", 36, 1021}, {"q2-q2pro", 36, 1022}, {"q2-q2pro", 36, 1023},
  {"q2-q2pro", 36, 1024}, {"q2-q2pro", 36, 1025}, {"q2-q2pro", 36, 1026},
  {"q2-rerelease", 1038, 0}, {"q2-private-classic", 4038, 0}, {"q2-kex-demo", 2022, 0}, {"q2-kex", 2023, 0},
};
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

static void test_demo_header_protocols(void) {
  CHECK(COUNT(PROTOCOLS) == COUNT(DH_PROTOCOL_DEMOS));
  for (size_t i = 0; i < COUNT(PROTOCOLS); i++) {
    const protocol_case *p = &PROTOCOLS[i];
    bytes demo = unhex(DH_PROTOCOL_DEMOS[i]);
    uint8_t *before = malloc(demo.length);
    memcpy(before, demo.data, demo.length);
    call c = {.kind = CALL_DEMO_HEADER, .data = demo.data, .length = demo.length};
    qa_caught_error caught;
    bool returned = attempt(&c, &caught);
    CHECK(returned && c.ok);
    if (!returned) fprintf(stderr, "  %s %g: %s\n", p->kind, p->revision, caught.message);
    if (returned && c.ok) {
      const qa_q2_demo_header *h = &c.header;
      bool has_revision = strcmp(p->kind, "q2-r1q2") == 0 || strcmp(p->kind, "q2-q2pro") == 0;
      CHECK(strcmp(h->protocol.kind, p->kind) == 0 && h->protocol.version == p->version);
      if (has_revision) CHECK(h->protocol.revision == p->revision);
      CHECK(h->recorded_version == p->version);
      CHECK(strcmp(h->data.gamedir, "ctf") == 0 && h->data.servercount == 73 && h->data.clientnum == 2);
      if (strcmp(p->kind, "q2-r1q2") == 0) {
        CHECK(h->data.has_r1q2_version && h->data.r1q2_version == p->revision);
        CHECK(h->data.has_r1q2_strafejump_hack && h->data.r1q2_strafejump_hack);
      }
      if (strcmp(p->kind, "q2-q2pro") == 0) {
        CHECK(h->data.has_q2pro_version && h->data.q2pro_version == p->revision);
        CHECK(h->data.has_wire_flags && h->data.wire_flags == 7);
      }
      if (p->version >= 1038) CHECK(h->data.has_server_fps && h->data.server_fps == 40);
    }
    CHECK(memcmp(before, demo.data, demo.length) == 0);
    call r = {.kind = CALL_READ_DEMO, .data = demo.data, .length = demo.length};
    CHECK(attempt(&r, &caught) && r.ok && r.count == 1);
    call_release(&r);
    free(before);
    free(demo.data);
  }
}

static void test_demo_header_grammar(void) {
  bytes safe = unhex(DH_SAFE);
  call c = {.kind = CALL_DEMO_HEADER, .data = safe.data, .length = safe.length};
  qa_caught_error caught;
  CHECK(attempt(&c, &caught) && c.ok && strcmp(c.header.protocol.kind, "q2-classic") == 0 &&
        c.header.protocol.version == 34);
  free(safe.data);
  bytes first = unhex(DH_CONFIG_FIRST);
  call f = {.kind = CALL_DEMO_HEADER, .data = first.data, .length = first.length};
  CHECK(raises(&f, "requires serverdata first"));
  free(first.data);
}

static void raises_each(const char *const *list, size_t count, const char *fragment) {
  for (size_t i = 0; i < count; i++) {
    bytes demo = unhex(list[i]);
    call c = {.kind = CALL_DEMO_HEADER, .data = demo.data, .length = demo.length};
    CHECK(raises(&c, fragment));
    free(demo.data);
  }
}
static void test_demo_header_rejects(void) {
  raises_each(DH_BAD_VERSION, COUNT(DH_BAD_VERSION), "Unsupported recorded Q2 protocol");
  raises_each(DH_BAD_R1Q2, COUNT(DH_BAD_R1Q2), "Unsupported recorded R1Q2 revision");
  raises_each(DH_BAD_Q2PRO, COUNT(DH_BAD_Q2PRO), "Unsupported recorded Q2PRO revision");
  size_t finish_length = 0;
  uint8_t *finish = qa_q2_finish_demo(&finish_length);
  CHECK(finish_length == 4 && finish[0] == 255 && finish[3] == 255);
  call c = {.kind = CALL_DEMO_HEADER, .data = finish, .length = finish_length};
  CHECK(raises(&c, "no serverdata"));
  free(finish);
  for (size_t i = 0; i < COUNT(DH_TRUNCATED); i++) {
    bytes demo = unhex(DH_TRUNCATED[i]);
    call t = {.kind = CALL_DEMO_HEADER, .data = demo.data, .length = demo.length};
    qa_caught_error caught;
    CHECK(!attempt(&t, &caught));
    CHECK(strcmp(caught.message, "Truncated Q2 demo packet") == 0 ||
          strcmp(caught.message, "Truncated Q2 demo record header") == 0 ||
          strcmp(caught.message, "Truncated Quake II message") == 0);
    free(demo.data);
  }
  /* readQ2Demo framing on its own: header shortfall, negative and overlong lengths. */
  const uint8_t short_header[] = {1, 0}, negative[] = {0xfe, 0xff, 0xff, 0xff}, overlong[] = {9, 0, 0, 0, 1};
  call a = {.kind = CALL_READ_DEMO, .data = short_header, .length = sizeof short_header};
  CHECK(raises(&a, "Truncated Q2 demo record header"));
  call b = {.kind = CALL_READ_DEMO, .data = negative, .length = sizeof negative};
  CHECK(raises(&b, "Truncated Q2 demo packet"));
  call d = {.kind = CALL_READ_DEMO, .data = overlong, .length = sizeof overlong};
  CHECK(raises(&d, "Truncated Q2 demo packet"));
  /* The -1 terminator ends the generator; trailing bytes after it are never framed. */
  const uint8_t ended[] = {1, 0, 0, 0, 6, 0xff, 0xff, 0xff, 0xff, 1};
  call e = {.kind = CALL_READ_DEMO, .data = ended, .length = sizeof ended};
  qa_caught_error caught;
  CHECK(attempt(&e, &caught) && e.ok && e.count == 1 && e.records[0].offset == 0 &&
        e.records[0].bytes_length == 1 && e.records[0].bytes[0] == 6);
  call_release(&e);
}

/* ---- old-demo.test.ts ---- */
static qa_q2_protocol_identity identity(const char *kind, double version, double revision) {
  qa_q2_protocol_identity id;
  memset(&id, 0, sizeof id);
  id.kind = kind;
  id.version = version;
  id.revision = (int32_t)revision;
  return id;
}
static void reader_init(qa_q2_server_message_reader *r, bool demo, qa_q2_protocol_identity protocol,
                        double max_config_strings) {
  qa_q2_server_message_options o;
  memset(&o, 0, sizeof o);
  o.read_mode_demo = demo;
  o.max_config_strings = max_config_strings;
  o.inventory_slots = 256;
  CHECK(qa_q2_server_message_reader_init(r, protocol, o));
}
/* First frame event of a read (copied out), or false. */
typedef struct frame_view { bool found; double suppressed, server_frame; uint8_t area[2]; size_t area_length;
  int stat2; float origin0; size_t entities; double fov; } frame_view;
static frame_view read_frame_view(qa_q2_server_message_reader *r, const char *hex, size_t prefix,
                                  bool first_only) {
  frame_view v;
  memset(&v, 0, sizeof v);
  bytes b = unhex(hex);
  call c = {.kind = CALL_READER, .reader = r, .data = b.data, .length = prefix > 0 ? prefix : b.length};
  qa_caught_error caught;
  if (attempt(&c, &caught) && c.ok) {
    for (size_t i = 0; i < c.count; i++) {
      if (first_only && i > 0) break;
      if (c.server_records[i].event.kind != QA_Q2_SERVER_FRAME) continue;
      const qa_q2_wire_frame *f = &c.server_records[i].event.frame;
      v.found = true;
      v.suppressed = f->suppressed_count;
      v.server_frame = f->server_frame;
      v.area_length = f->area_bits_length;
      for (size_t k = 0; k < 2 && k < f->area_bits_length; k++) v.area[k] = f->area_bits[k];
      v.stat2 = f->player.stats[2];
      v.entities = f->entity_count;
      v.origin0 = f->entity_count > 0 ? f->entities[0].origin[0] : NAN;
      v.fov = f->player.fov;
      break;
    }
    call_release(&c);
  } else {
    fprintf(stderr, "  reader raised: %s\n", caught.message);
  }
  free(b.data);
  return v;
}
static bool reader_raises(qa_q2_server_message_reader *r, const char *hex, size_t prefix, const char *fragment) {
  bytes b = unhex(hex);
  call c = {.kind = CALL_READER, .reader = r, .data = b.data, .length = prefix > 0 ? prefix : b.length};
  bool ok = raises(&c, fragment);
  free(b.data);
  return ok;
}
static bool reader_reads(qa_q2_server_message_reader *r, const char *hex, qa_q2_server_event_kind *first_kind) {
  bytes b = unhex(hex);
  call c = {.kind = CALL_READER, .reader = r, .data = b.data, .length = b.length};
  qa_caught_error caught;
  bool ok = attempt(&c, &caught) && c.ok;
  if (!ok) fprintf(stderr, "  reader raised: %s\n", caught.message);
  if (ok && first_kind != NULL) *first_kind = c.count > 0 ? c.server_records[0].event.kind : QA_Q2_SERVER_PRIVATE;
  if (ok) call_release(&c);
  free(b.data);
  return ok;
}

static void test_old_demo_opt_in(void) {
  qa_q2_protocol_identity classic = identity("q2-classic", 34, 0);
  qa_q2_server_message_reader r;
  reader_init(&r, false, classic, 2080);
  CHECK(reader_raises(&r, OLD_SD26, 0, "differs from negotiated"));
  qa_q2_server_message_reader_destroy(&r);
  const qa_q2_protocol_identity others[] = {identity("q2-r1q2", 35, 1905), identity("q2-q2pro", 36, 1019),
                                            identity("q2-rerelease", 1038, 0)};
  for (size_t i = 0; i < COUNT(others); i++) {
    reader_init(&r, true, others[i], 2080);
    CHECK(reader_raises(&r, OLD_SD26, 0, "differs from negotiated"));
    qa_q2_server_message_reader_destroy(&r);
  }
  reader_init(&r, true, classic, 2080);
  CHECK(reader_raises(&r, OLD_SD25, 0, "differs from negotiated"));
  qa_q2_server_message_reader_destroy(&r);
  reader_init(&r, true, classic, 2080);
  qa_q2_server_event_kind kind = QA_Q2_SERVER_NOP;
  CHECK(reader_reads(&r, OLD_SD26, &kind) && kind == QA_Q2_SERVER_DATA);
  qa_q2_server_message_reader_destroy(&r);
}

static void append_record(bytes *demo, const char *hex) {
  bytes b = unhex(hex);
  size_t length = 0;
  uint8_t *record = qa_q2_write_demo_record(b.data, b.length, &length);
  CHECK(record != NULL && length == b.length + 4);
  demo->data = realloc(demo->data, demo->length + length);
  memcpy(demo->data + demo->length, record, length);
  demo->length += length;
  free(record);
  free(b.data);
}
static void finish_demo(bytes *demo) {
  size_t length = 0;
  uint8_t *finish = qa_q2_finish_demo(&length);
  demo->data = realloc(demo->data, demo->length + length);
  memcpy(demo->data + demo->length, finish, length);
  demo->length += length;
  free(finish);
}

static void test_old_demo_legacy_frames(void) {
  qa_q2_protocol_identity classic = identity("q2-classic", 34, 0);
  bytes demo = {NULL, 0};
  append_record(&demo, OLD_SD26);
  append_record(&demo, OLD_LEGACY_FIRST);
  append_record(&demo, OLD_LEGACY_SECOND);
  finish_demo(&demo);
  qa_q2_server_message_reader r;
  reader_init(&r, true, classic, 2080);
  call framing = {.kind = CALL_READ_DEMO, .data = demo.data, .length = demo.length};
  qa_caught_error caught;
  CHECK(attempt(&framing, &caught) && framing.ok && framing.count == 3);
  frame_view frames[4];
  size_t frame_count = 0;
  for (size_t i = 0; i < framing.count; i++) {
    call c = {.kind = CALL_READER, .reader = &r, .data = framing.records[i].bytes,
              .length = framing.records[i].bytes_length};
    CHECK(attempt(&c, &caught) && c.ok);
    for (size_t k = 0; k < c.count; k++) {
      if (c.server_records[k].event.kind != QA_Q2_SERVER_FRAME || frame_count >= 4) continue;
      const qa_q2_wire_frame *f = &c.server_records[k].event.frame;
      frame_view *v = &frames[frame_count++];
      v->suppressed = f->suppressed_count;
      v->area_length = f->area_bits_length;
      for (size_t a = 0; a < 2 && a < f->area_bits_length; a++) v->area[a] = f->area_bits[a];
      v->stat2 = f->player.stats[2];
      v->origin0 = f->entity_count > 0 ? f->entities[0].origin[0] : NAN;
    }
    if (c.count > 0 && c.server_records[0].event.kind == QA_Q2_SERVER_FRAME) {
      CHECK(c.server_records[c.count - 1].event.kind == QA_Q2_SERVER_NOP);
    }
    call_release(&c);
  }
  call_release(&framing);
  CHECK(frame_count == 2);
  if (frame_count == 2) {
    CHECK(frames[1].suppressed == 0);
    CHECK(frames[1].area_length == 2 && frames[1].area[0] == 13 && frames[1].area[1] == 24);
    CHECK(frames[1].stat2 == 100 && frames[1].origin0 == 16);
  }
  CHECK(reader_reads(&r, OLD_SD34, NULL));
  frame_view current = read_frame_view(&r, OLD_MODERN_FIRST, 0, true);
  CHECK(current.found && current.suppressed == 5);
  CHECK(reader_reads(&r, OLD_SD26, NULL));
  qa_q2_server_message_reader_reset(&r);
  current = read_frame_view(&r, OLD_MODERN_FIRST, 0, true);
  CHECK(current.found && current.suppressed == 5);
  qa_q2_server_message_reader_destroy(&r);

  /* playQ2Demo over the same recording: one record list per dm2 record. */
  reader_init(&r, true, classic, 2080);
  call play = {.kind = CALL_PLAY_DEMO, .data = demo.data, .length = demo.length, .reader = &r};
  CHECK(attempt(&play, &caught) && play.ok && play.count == 3);
  if (play.ok && play.count == 3) {
    CHECK(play.steps[0].record_count > 0 && play.steps[0].records[0].event.kind == QA_Q2_SERVER_DATA);
    CHECK(play.steps[2].record_count > 0 && play.steps[2].records[0].event.kind == QA_Q2_SERVER_FRAME);
  }
  call_release(&play);
  qa_q2_server_message_reader_destroy(&r);
  /* A damaged later record raises only after the reader consumed the earlier ones. */
  reader_init(&r, true, classic, 2080);
  bytes damaged = {malloc(demo.length), demo.length - 4};
  memcpy(damaged.data, demo.data, demo.length);
  damaged.data[damaged.length - 1] = 0; /* last record body cut short by one byte */
  bytes cut = {damaged.data, damaged.length - 1};
  call bad = {.kind = CALL_PLAY_DEMO, .data = cut.data, .length = cut.length, .reader = &r};
  CHECK(raises(&bad, "Truncated Q2 demo packet"));
  /* The serverdata reached the reader before the raise: a modern-layout frame is now rejected. */
  CHECK(reader_raises(&r, OLD_MODERN_FIRST, 0, ""));
  free(damaged.data);
  qa_q2_server_message_reader_destroy(&r);
  free(demo.data);
}

static void test_old_demo_failed_serverdata(void) {
  qa_q2_server_message_reader r;
  reader_init(&r, true, identity("q2-classic", 34, 0), 2080);
  CHECK(reader_raises(&r, OLD_SD26, 7, "Truncated Quake II message"));
  frame_view event = read_frame_view(&r, OLD_MODERN_FIRST, 0, true);
  CHECK(event.found && event.suppressed == 5);
  CHECK(reader_reads(&r, OLD_SD26, NULL));
  CHECK(reader_raises(&r, OLD_LEGACY_FIRST, 10, "CL_ParseFrame: not playerinfo"));
  qa_q2_server_message_reader_destroy(&r);
}

/* ---- MVD visibility fixtures ---- */
typedef enum vis_mode { VIS_FIXTURE, VIS_ENCODING, VIS_SELECTED } vis_mode;
typedef struct vis_ctx { int sentinel; vis_mode mode; int calls; } vis_ctx;
static size_t vis_entities(void *ctx, qa_q2_wire_entity *entities, size_t count, const qa_q2_wire_player *player,
                           const uint8_t *portal_bits, size_t portal_length) {
  vis_ctx *v = ctx;
  CHECK(v->sentinel == SENTINEL);
  (void)portal_bits;
  (void)portal_length;
  v->calls++;
  if (v->mode != VIS_SELECTED) return count;
  size_t kept = 0;
  for (size_t i = 0; i < count; i++) {
    if (entities[i].number == player->clientnum + 1) entities[kept++] = entities[i];
  }
  return kept;
}
static bool vis_visible(void *ctx, double leaf, bool is_pvs, const qa_q2_wire_player *player,
                        const uint8_t *portal_bits, size_t portal_length) {
  vis_ctx *v = ctx;
  CHECK(v->sentinel == SENTINEL);
  (void)is_pvs;
  (void)player;
  v->calls++;
  if (v->mode == VIS_ENCODING) return leaf == 1;
  if (v->mode == VIS_SELECTED) return true;
  return portal_length > 0 && portal_bits[0] == 0x55;
}
static uint8_t *vis_area_bits(void *ctx, const qa_q2_wire_player *player, const uint8_t *portal_bits,
                              size_t portal_length, size_t *out_length) {
  vis_ctx *v = ctx;
  CHECK(v->sentinel == SENTINEL);
  (void)player;
  v->calls++;
  uint8_t *out = malloc(1);
  out[0] = v->mode == VIS_FIXTURE ? (portal_length > 0 && portal_bits[0] == 0x55 ? 7 : 0) : 1;
  *out_length = 1;
  return out;
}
static bool vis_audible(void *ctx, const float origin[3], const qa_q2_wire_player *player,
                        const uint8_t *portal_bits, size_t portal_length) {
  vis_ctx *v = ctx;
  CHECK(v->sentinel == SENTINEL);
  (void)origin;
  (void)player;
  (void)portal_bits;
  (void)portal_length;
  v->calls++;
  return true;
}
static void vis_origin(void *ctx, const qa_q2_wire_entity *entity, float out[3]) {
  vis_ctx *v = ctx;
  CHECK(v->sentinel == SENTINEL);
  v->calls++;
  out[0] = entity->origin[0];
  out[1] = v->mode == VIS_ENCODING ? 0 : entity->origin[1];
  out[2] = v->mode == VIS_ENCODING ? 0 : entity->origin[2];
}
static void playback_init(qa_q2_mvd_playback *p, vis_ctx *ctx) {
  qa_q2_mvd_visibility visibility = {ctx, vis_entities, vis_visible, vis_area_bits, vis_audible, vis_origin};
  CHECK(qa_q2_mvd_playback_init(p, visibility));
}
/* read() that must succeed; returns the records (caller frees). */
static qa_q2_server_record *must_read(qa_q2_mvd_playback *p, const uint8_t *data, size_t length, size_t *count) {
  call c = {.kind = CALL_MVD_READ, .playback = p, .data = data, .length = length};
  qa_caught_error caught;
  if (!attempt(&c, &caught) || !c.ok) {
    fprintf(stderr, "  mvd read raised: %s\n", caught.message);
    CHECK(false);
    *count = 0;
    return NULL;
  }
  *count = c.count;
  return c.server_records;
}
static void read_and_free(qa_q2_mvd_playback *p, const uint8_t *data, size_t length) {
  size_t count = 0;
  qa_q2_server_record *records = must_read(p, data, length, &count);
  qa_q2_server_records_free(records, count);
}

/* ---- mvd-encoding.test.ts ---- */
typedef struct capture_store {
  qa_q2_wire_player players[2];
  qa_q2_mvd_capture_player entries[2];
  qa_q2_mvd_capture_config configs[2];
  qa_q2_wire_entity entity;
  uint8_t portal[1];
  qa_q2_mvd_capture capture;
} capture_store;
static void make_capture(capture_store *s, double revision, double x) {
  memset(s, 0, sizeof *s);
  double flags = revision == 2011 ? 4 : (revision == 2012 || revision == 2013) ? 12 : 0;
  qa_q2_mvd_profile profile;
  qa_q2_get_mvd_profile(revision, flags, &profile);
  qa_q2_wire_player *player = &s->players[0], *other = &s->players[1];
  qa_q2_wire_player_init(player);
  player->pmove.origin[0] = (int32_t)(x * 8);
  player->pmove.origin_f[0] = (float)(x + 0.5);
  player->fov = 95;
  player->gunindex = profile.extended ? 500 : 5;
  player->stats[profile.extended ? 1 : 0] = 88;
  if (profile.fog) player->q2pro_fog.density = 1234;
  qa_q2_wire_player_init(other);
  other->fov = 90;
  qa_q2_wire_entity_init(&s->entity);
  s->entity.number = 10;
  s->entity.origin[0] = (float)(x + 0.5);
  s->entity.modelindex = profile.extended ? 300 : 3;
  s->entity.solid = profile.extended ? 0x12345678 : 123;
  s->configs[0].index = profile.max_clients_index;
  s->configs[0].value = "2";
  s->configs[1].index = 0;
  s->configs[1].value = "fixture";
  s->portal[0] = 1;
  s->entries[0].number = 0;
  s->entries[0].player = *player;
  s->entries[1].number = 1;
  s->entries[1].player = *other;
  qa_q2_mvd_capture *c = &s->capture;
  c->revision = revision;
  c->flags = flags;
  c->servercount = 1;
  c->gamedir = "baseq2";
  c->dummy = -1;
  c->config_strings = s->configs;
  c->config_string_count = 2;
  c->portal_bits = s->portal;
  c->portal_bits_length = 1;
  c->players = s->entries;
  c->player_count = 2;
  c->entities = &s->entity;
  c->entity_count = 1;
}
static bool blocks_match(const qa_q2_mvd_byte_block *blocks, size_t count, const char *const *hex, size_t hex_count) {
  if (count != hex_count) return false;
  for (size_t i = 0; i < count; i++) {
    if (!equals_hex(blocks[i].bytes, blocks[i].length, hex[i])) return false;
  }
  return true;
}
#define ENC_CASE(rev) {rev, ENC_##rev##_FIRST, COUNT(ENC_##rev##_FIRST), ENC_##rev##_SECOND, COUNT(ENC_##rev##_SECOND)}
typedef struct enc_case { double revision; const char *const *first; size_t first_count; const char *const *second;
  size_t second_count; } enc_case;

static void test_mvd_encoding_profiles(void) {
  const enc_case cases[] = {ENC_CASE(2010), ENC_CASE(2011), ENC_CASE(2012), ENC_CASE(2013), ENC_CASE(3038)};
  for (size_t i = 0; i < COUNT(cases); i++) {
    double revision = cases[i].revision;
    qa_q2_mvd_encoder encoder;
    qa_q2_mvd_encoder_init(&encoder);
    vis_ctx ctx = {SENTINEL, VIS_ENCODING, 0};
    qa_q2_mvd_playback reader;
    playback_init(&reader, &ctx);
    capture_store store;
    make_capture(&store, revision, 100);
    qa_q2_mvd_byte_block *blocks = NULL;
    size_t count = 0;
    CHECK(qa_q2_mvd_encoder_capture(&encoder, &store.capture, &blocks, &count));
    CHECK(blocks_match(blocks, count, cases[i].first, cases[i].first_count));
    for (size_t k = 0; k < count; k++) read_and_free(&reader, blocks[k].bytes, blocks[k].length);
    qa_q2_mvd_byte_blocks_free(blocks, count);
    CHECK(qa_q2_mvd_playback_player_count(&reader) == 2);
    qa_q2_wire_frame frame;
    double clientnum = -1;
    CHECK(qa_q2_mvd_playback_projection(&reader, &clientnum, &frame));
    CHECK(frame.entity_count >= 1 && frame.entities[0].modelindex == (revision == 2010 ? 3 : 300));
    qa_q2_wire_frame_free(&frame);
    make_capture(&store, revision, 125);
    store.capture.player_count = 1; /* players.delete(1) */
    CHECK(qa_q2_mvd_encoder_capture(&encoder, &store.capture, &blocks, &count));
    CHECK(blocks_match(blocks, count, cases[i].second, cases[i].second_count));
    for (size_t k = 0; k < count; k++) read_and_free(&reader, blocks[k].bytes, blocks[k].length);
    qa_q2_mvd_byte_blocks_free(blocks, count);
    CHECK(qa_q2_mvd_playback_player_count(&reader) == 1);
    CHECK(qa_q2_mvd_playback_projection(&reader, &clientnum, &frame));
    CHECK(frame.entity_count >= 1 && frame.entities[0].origin[0] == 125.5f);
    CHECK(frame.entity_count >= 1 && frame.entities[0].solid == (revision == 2010 ? 123 : 0x12345678));
    if (revision == 3038) CHECK(frame.player.pmove.origin_f[0] == 125.5f);
    else CHECK(frame.player.pmove.origin[0] == 1000);
    if (revision == 2013) CHECK(frame.player.q2pro_fog.density == 1234);
    qa_q2_wire_frame_free(&frame);
    qa_q2_mvd_playback_destroy(&reader);
    qa_q2_mvd_encoder_destroy(&encoder);
    CHECK(ctx.calls > 0);
  }
}

typedef struct enc_call { qa_q2_mvd_encoder *e; const qa_q2_mvd_capture *c; qa_q2_mvd_byte_block *blocks;
  size_t count; } enc_call;
static void capture_body(void *arg) {
  enc_call *ec = arg;
  if (qa_q2_mvd_encoder_capture(ec->e, ec->c, &ec->blocks, &ec->count)) {
    qa_q2_mvd_byte_blocks_free(ec->blocks, ec->count);
  }
}
static size_t print_bytes(uint8_t *out, const char *text) {
  size_t n = strlen(text);
  out[0] = 10;
  out[1] = 2;
  memcpy(out + 2, text, n + 1);
  return n + 3;
}
static void test_mvd_encoding_recipients(void) {
  qa_q2_mvd_encoder encoder;
  qa_q2_mvd_encoder_init(&encoder);
  vis_ctx ctx = {SENTINEL, VIS_ENCODING, 0};
  qa_q2_mvd_playback reader;
  playback_init(&reader, &ctx);
  capture_store store;
  make_capture(&store, 2010, 100);
  qa_q2_mvd_byte_block *blocks = NULL;
  size_t count = 0;
  CHECK(qa_q2_mvd_encoder_capture(&encoder, &store.capture, &blocks, &count));
  for (size_t k = 0; k < count; k++) read_and_free(&reader, blocks[k].bytes, blocks[k].length);
  qa_q2_mvd_byte_blocks_free(blocks, count);
  char visible[400] = "";
  for (int i = 0; i < 50; i++) strcat(visible, "visible");
  uint8_t hidden_b[16], visible_b[400], other_b[24], self_b[8];
  qa_q2_mvd_emission messages[4] = {
    {{QA_Q2_MVD_RECIPIENT_PHS, 0, 2}, false, hidden_b, print_bytes(hidden_b, "hidden")},
    {{QA_Q2_MVD_RECIPIENT_PVS, 0, 1}, true, visible_b, print_bytes(visible_b, visible)},
    {{QA_Q2_MVD_RECIPIENT_PLAYER, 1, 0}, true, other_b, print_bytes(other_b, "other player")},
    {{QA_Q2_MVD_RECIPIENT_PLAYER, 0, 0}, true, self_b, print_bytes(self_b, "self")},
  };
  make_capture(&store, 2010, 100);
  store.capture.messages = messages;
  store.capture.message_count = 4;
  CHECK(qa_q2_mvd_encoder_capture(&encoder, &store.capture, &blocks, &count));
  CHECK(blocks_match(blocks, count, ENC_ROUTED, COUNT(ENC_ROUTED)));
  const char *texts[8];
  size_t text_count = 0;
  qa_q2_server_record *all[8];
  size_t all_counts[8];
  for (size_t k = 0; k < count && k < 8; k++) {
    all[k] = must_read(&reader, blocks[k].bytes, blocks[k].length, &all_counts[k]);
    for (size_t r = 0; r < all_counts[k]; r++) {
      if (all[k][r].event.kind == QA_Q2_SERVER_PRINT && text_count < 8) texts[text_count++] = all[k][r].event.text;
    }
  }
  CHECK(text_count == 2 && strcmp(texts[0], visible) == 0 && strcmp(texts[1], "self") == 0);
  for (size_t k = 0; k < count && k < 8; k++) qa_q2_server_records_free(all[k], all_counts[k]);
  qa_q2_mvd_byte_blocks_free(blocks, count);

  /* encodeMvdEmission validation: payload bounds, recipient and leaf ranges. */
  uint8_t big[2048] = {0};
  qa_q2_mvd_emission bad[] = {
    {{QA_Q2_MVD_RECIPIENT_ALL, 0, 0}, false, big, 0},
    {{QA_Q2_MVD_RECIPIENT_ALL, 0, 0}, false, big, 2048},
    {{QA_Q2_MVD_RECIPIENT_PLAYER, 256, 0}, false, big, 1},
    {{QA_Q2_MVD_RECIPIENT_PVS, 0, 65535}, false, big, 1},
    {{QA_Q2_MVD_RECIPIENT_PHS, 0, 1.5}, false, big, 1},
  };
  const char *expected[] = {"MVD routed payload must contain1..2047bytes", "MVD routed payload must contain1..2047bytes",
                            "Invalid MVD recipient", "Invalid MVD leaf", "Invalid MVD leaf"};
  for (size_t i = 0; i < COUNT(bad); i++) {
    /* Routed through capture(): the frame message is built, then the emission raises. */
    make_capture(&store, 2010, 100);
    store.capture.messages = &bad[i];
    store.capture.message_count = 1;
    enc_call ec = {&encoder, &store.capture, NULL, 0};
    qa_caught_error caught;
    CHECK(!qa_try(capture_body, &ec, &caught) && strcmp(caught.message, expected[i]) == 0 &&
          strcmp(caught.name, "RangeError") == 0);
  }
  /* Capture validation raises before any previous state changes. */
  const char *capture_errors[] = {"Invalid MVD configstring", "MVD portal state too large",
                                  "Invalid MVD entity identity", "Invalid MVD player number",
                                  "Unsupported MVD revision 2014", "MVD extended-v2 requires extended limits"};
  uint8_t portal[256] = {0};
  for (size_t i = 0; i < COUNT(capture_errors); i++) {
    make_capture(&store, 2010, 100);
    /* Only a changed world validates indices; TS deltas write any configstring index. */
    if (i == 0) { store.configs[1].index = 2080; store.capture.servercount = 2; }
    if (i == 1) { store.capture.portal_bits = portal; store.capture.portal_bits_length = 256; }
    if (i == 2) store.entity.number = 1024;
    if (i == 3) store.entries[1].number = 256;
    if (i == 4) store.capture.revision = 2014;
    if (i == 5) { store.capture.revision = 2012; store.capture.flags = 8; }
    enc_call ec = {&encoder, &store.capture, NULL, 0};
    qa_caught_error caught;
    CHECK(!qa_try(capture_body, &ec, &caught) && strcmp(caught.message, capture_errors[i]) == 0);
  }
  /* The previous capture is still the routed one: an unchanged capture is a bare frame delta. */
  make_capture(&store, 2010, 100);
  CHECK(qa_q2_mvd_encoder_capture(&encoder, &store.capture, &blocks, &count));
  CHECK(count == 1 && blocks[0].bytes[0] == QA_Q2_MVD_FRAME);
  qa_q2_mvd_byte_blocks_free(blocks, count);
  qa_q2_mvd_playback_destroy(&reader);
  qa_q2_mvd_encoder_destroy(&encoder);
}

/* ---- mvd-playback.test.ts ---- */
static const qa_q2_wire_frame *frame_of(qa_q2_server_record *records, size_t count) {
  for (size_t i = 0; i < count; i++) {
    if (records[i].event.kind == QA_Q2_SERVER_FRAME) return &records[i].event.frame;
  }
  return NULL;
}
static void test_mvd_playback_gamestate(void) {
  vis_ctx ctx = {SENTINEL, VIS_FIXTURE, 0};
  qa_q2_mvd_playback reader;
  playback_init(&reader, &ctx);
  bytes gs = unhex(MVD_GAMESTATE);
  size_t count = 0;
  qa_q2_server_record *records = must_read(&reader, gs.data, gs.length, &count);
  const qa_q2_server_event_kind kinds[] = {QA_Q2_SERVER_DATA, QA_Q2_SERVER_CONFIG_STRING, QA_Q2_SERVER_CONFIG_STRING,
                                           QA_Q2_SERVER_CONFIG_STRING, QA_Q2_SERVER_COMMAND_TEXT, QA_Q2_SERVER_FRAME};
  CHECK(count == COUNT(kinds));
  for (size_t i = 0; i < count && i < COUNT(kinds); i++) CHECK(records[i].event.kind == kinds[i]);
  if (count == COUNT(kinds)) {
    CHECK(records[0].opcode == 12 && records[0].event.data.servercount == 12 &&
          strcmp(records[0].event.data.levelname, "Arena") == 0 && records[0].event.data.attractloop);
    CHECK(records[2].event.index == 30 && strcmp(records[2].event.text, "2") == 0);
    CHECK(strcmp(records[4].event.text, "precache 12\n") == 0);
    /* Every gamestate record carries the full serverdata span as its raw bytes. */
    CHECK(records[5].raw_length == gs.length && memcmp(records[5].raw, gs.data, gs.length) == 0);
  }
  const qa_q2_wire_frame *frame = frame_of(records, count);
  CHECK(frame != NULL);
  if (frame != NULL) {
    CHECK(frame->area_bits_length == 1 && frame->area_bits[0] == 7);
    CHECK(frame->player.fov == 90);
    CHECK(frame->entity_count == 2 && frame->entities[0].origin[0] == 100 && frame->entities[1].origin[0] == -200);
    CHECK(frame->entity_count == 2 && frame->entities[0].angles[1] == 90);
  }
  qa_q2_server_records_free(records, count);
  CHECK(qa_q2_mvd_playback_player_count(&reader) == 2);
  call select = {.kind = CALL_SELECT, .playback = &reader, .number = 1};
  qa_caught_error caught;
  CHECK(attempt(&select, &caught));
  /* Empty delta retains both player states and entity geometry. */
  const uint8_t empty_delta[] = {6, 1, 0x55, 255, 0, 0};
  records = must_read(&reader, empty_delta, sizeof empty_delta, &count);
  CHECK(count >= 1 && records[0].event.kind == QA_Q2_SERVER_FRAME);
  if (count >= 1 && records[0].event.kind == QA_Q2_SERVER_FRAME) {
    const qa_q2_wire_frame *f = &records[0].event.frame;
    CHECK(f->player.fov == 100 && f->server_frame == 2);
    CHECK(f->entity_count == 2 && f->entities[0].origin[0] == 100 && f->entities[1].origin[0] == -200);
  }
  qa_q2_server_records_free(records, count);
  bytes remove = unhex(MVD_REMOVE);
  read_and_free(&reader, remove.data, remove.length);
  CHECK(qa_q2_mvd_playback_player(&reader, 1) == NULL);
  CHECK(raises(&select, "not active"));
  /* Other selections TS rejects: fractional, negative, beyond maxClients. */
  const double invalid[] = {0.5, -1, 2, NAN};
  for (size_t i = 0; i < COUNT(invalid); i++) {
    call s = {.kind = CALL_SELECT, .playback = &reader, .number = invalid[i]};
    CHECK(raises(&s, "MVD player is not active"));
  }
  free(remove.data);
  free(gs.data);
  qa_q2_mvd_playback_destroy(&reader);
}

static bool fresh_read_raises(const uint8_t *data, size_t length, const char *fragment) {
  vis_ctx ctx = {SENTINEL, VIS_FIXTURE, 0};
  qa_q2_mvd_playback reader;
  playback_init(&reader, &ctx);
  call c = {.kind = CALL_MVD_READ, .playback = &reader, .data = data, .length = length};
  bool ok = raises(&c, fragment);
  qa_q2_mvd_playback_destroy(&reader);
  return ok;
}
static void test_mvd_playback_rejects(void) {
  const uint8_t early[] = {6, 0, 255, 0, 0};
  CHECK(fresh_read_raises(early, sizeof early, "before serverdata"));
  bytes gs = unhex(MVD_GAMESTATE);
  CHECK(fresh_read_raises(gs.data, gs.length - 1, "Truncated Quake II message"));
  gs.data[5] = 9999 & 255;
  gs.data[6] = 9999 >> 8;
  CHECK(fresh_read_raises(gs.data, gs.length, "Unsupported MVD revision"));
  free(gs.data);
  CHECK(fresh_read_raises(early, 0, "Invalid MVD message length"));
  uint8_t *huge = calloc(QA_Q2_MVD_MAX_MESSAGE + 1, 1);
  CHECK(fresh_read_raises(huge, QA_Q2_MVD_MAX_MESSAGE + 1, "Invalid MVD message length"));
  free(huge);
  const uint8_t unknown[] = {31};
  CHECK(fresh_read_raises(unknown, sizeof unknown, "Unsupported MVD command 31"));
  /* A routed payload whose length byte is missing ORs to -1: TS slices an empty payload and
   * steps readcount back, so a multicast-all at the very end is an empty embedded read. */
  vis_ctx ctx = {SENTINEL, VIS_FIXTURE, 0};
  qa_q2_mvd_playback reader;
  playback_init(&reader, &ctx);
  const uint8_t dangling[] = {QA_Q2_MVD_MULTICAST_ALL};
  call c = {.kind = CALL_MVD_READ, .playback = &reader, .data = dangling, .length = sizeof dangling};
  qa_caught_error caught;
  bool returned = attempt(&c, &caught);
  CHECK(returned && c.ok && c.count == 0);
  if (!returned) fprintf(stderr, "  dangling multicast raised: %s\n", caught.message);
  call_release(&c);
  const uint8_t dangling_unicast[] = {QA_Q2_MVD_UNICAST};
  call u = {.kind = CALL_MVD_READ, .playback = &reader, .data = dangling_unicast, .length = 1};
  CHECK(raises(&u, "Truncated MVD embedded message"));
  qa_q2_mvd_playback_destroy(&reader);
}

typedef struct layout_bytes { uint8_t data[128]; size_t length; } layout_bytes;
static void lb(layout_bytes *b, uint32_t v) { b->data[b->length++] = (uint8_t)(v & 255); }
static void lw(layout_bytes *b, uint32_t v) { lb(b, v); lb(b, v >> 8); }
static void ll(layout_bytes *b, uint32_t v) { lw(b, v); lw(b, v >> 16); }
static void lt(layout_bytes *b, const char *s) { for (; *s; s++) lb(b, (uint8_t)*s); lb(b, 0); }
static void test_mvd_playback_layouts(void) {
  const int revisions[] = {2011, 2012, 2013, 3038};
  for (size_t i = 0; i < COUNT(revisions); i++) {
    int revision = revisions[i];
    layout_bytes b = {{0}, 0};
    lb(&b, revision == 2011 ? (4 | 4 << 5) : 4);
    ll(&b, 37);
    lw(&b, (uint32_t)revision);
    if (revision == 2012 || revision == 2013) lw(&b, 12);
    ll(&b, 12);
    lt(&b, "baseq2");
    lw(&b, (uint32_t)-1);
    lw(&b, 60);
    lt(&b, "2");
    lw(&b, revision == 3038 ? 12448 : 13630);
    lb(&b, 0);
    lb(&b, 1);
    lw(&b, 2 | 256 | 16384 | (revision == 2013 ? 32768 : 0));
    if (revision == 2013) lb(&b, 2);
    if (revision == 3038) {
      float values[2] = {1234.5f, -6.25f};
      for (int k = 0; k < 2; k++) {
        uint32_t bits;
        memcpy(&bits, &values[k], 4);
        ll(&b, bits);
      }
    } else if (revision >= 2012) {
      lw(&b, 800u << 1);
      lw(&b, (uint32_t)(-1600 * 2));
    } else {
      lw(&b, 800);
      lw(&b, (uint32_t)-1600);
    }
    if (revision == 2013) {
      lb(&b, 2);
      lw(&b, 32768);
      lw(&b, 65535);
    }
    lb(&b, 105);
    if (revision == 3038) {
      ll(&b, 0);
      ll(&b, 1);
      lw(&b, 99);
    } else if (revision >= 2012) {
      lb(&b, 128); lb(&b, 128); lb(&b, 128); lb(&b, 128); lb(&b, 16);
      lw(&b, 99);
    } else {
      ll(&b, 1);
      lw(&b, 99);
    }
    lb(&b, 255);
    lw(&b, 0);
    vis_ctx ctx = {SENTINEL, VIS_FIXTURE, 0};
    qa_q2_mvd_playback reader;
    playback_init(&reader, &ctx);
    size_t count = 0;
    qa_q2_server_record *records = must_read(&reader, b.data, b.length, &count);
    CHECK(count > 0 && records[0].event.kind == QA_Q2_SERVER_DATA);
    qa_q2_server_records_free(records, count);
    qa_q2_wire_frame frame;
    double clientnum = -1;
    CHECK(qa_q2_mvd_playback_projection(&reader, &clientnum, &frame));
    CHECK(clientnum == 1 && frame.player.clientnum == 1);
    CHECK(frame.player.fov == 105);
    CHECK(frame.player.stats[revision == 2011 ? 0 : 32] == 99);
    if (revision == 3038) {
      CHECK(frame.player.pmove.origin_f[0] == 1234.5f && frame.player.pmove.origin_f[1] == -6.25f &&
            frame.player.pmove.origin_f[2] == 0);
    } else {
      CHECK(frame.player.pmove.origin[0] == 800 && frame.player.pmove.origin[1] == -1600 &&
            frame.player.pmove.origin[2] == 0);
    }
    if (revision == 2013) CHECK(frame.player.q2pro_fog.density == 32768);
    qa_q2_wire_frame_free(&frame);
    const char *kind = qa_q2_mvd_playback_get_protocol(&reader).kind;
    CHECK(strcmp(kind, revision == 3038 ? "q2-rerelease" : "q2-q2pro") == 0);
    qa_q2_mvd_playback_destroy(&reader);
  }
}

/* Unit layer of "shared demo receiver loads the recorded world before MVD visibility and
 * rebinds the selected source view": the MvdRecording bytes, their framing, and a view
 * rebound by the consumer between the two recorded messages and at a generator yield. */
typedef struct sink_state { int sentinel; qa_q2_mvd_playback *playback; double views[8]; size_t view_count;
  size_t records; bool select_at_precache; } sink_state;
static void view_sink(void *ctx, qa_q2_server_record *record) {
  sink_state *s = ctx;
  CHECK(s->sentinel == SENTINEL);
  s->records++;
  if (record->event.kind == QA_Q2_SERVER_COMMAND_TEXT && s->select_at_precache) {
    qa_q2_mvd_playback_select_player(s->playback, 1);
  }
  if (record->event.kind == QA_Q2_SERVER_FRAME && s->view_count < 8) {
    s->views[s->view_count++] = record->event.frame.player.clientnum;
  }
  free(record->raw);
  qa_q2_server_event_clear(&record->event);
}
typedef struct recorded { int sentinel; bytes out; } recorded;
static void record_write(void *ctx, const uint8_t *data, size_t length) {
  recorded *r = ctx;
  CHECK(r->sentinel == SENTINEL);
  r->out.data = realloc(r->out.data, r->out.length + length);
  memcpy(r->out.data + r->out.length, data, length);
  r->out.length += length;
}
static void test_mvd_playback_recorded_views(void) {
  bytes gs = unhex(MVD_GAMESTATE);
  const uint8_t delta[] = {6, 1, 0x55, 255, 0, 0};
  recorded rec = {SENTINEL, {NULL, 0}};
  qa_q2_mvd_recording writer;
  qa_q2_mvd_recording_init(&writer, record_write, &rec);
  qa_q2_mvd_recording_append(&writer, gs.data, gs.length);
  qa_q2_mvd_recording_append(&writer, delta, sizeof delta);
  qa_q2_mvd_recording_close(&writer);
  CHECK(equals_hex(rec.out.data, rec.out.length, MVD_RECORDING));
  qa_q2_mvd_byte_block *blocks = NULL;
  size_t count = 0;
  CHECK(qa_q2_read_mvd_recording(rec.out.data, rec.out.length, &blocks, &count) && count == 2);
  vis_ctx ctx = {SENTINEL, VIS_FIXTURE, 0};
  qa_q2_mvd_playback reader;
  playback_init(&reader, &ctx);
  sink_state sink = {SENTINEL, &reader, {0}, 0, 0, false};
  if (count == 2) {
    CHECK(qa_q2_mvd_playback_read_records(&reader, blocks[0].bytes, blocks[0].length, view_sink, &sink));
    const char *map = qa_q2_mvd_playback_config_string(&reader, 33);
    CHECK(map != NULL && strcmp(map, "maps/q2dm1.bsp") == 0);
    qa_q2_mvd_playback_select_player(&reader, 1);
    CHECK(qa_q2_mvd_playback_read_records(&reader, blocks[1].bytes, blocks[1].length, view_sink, &sink));
  }
  CHECK(sink.view_count == 2 && sink.views[0] == 0 && sink.views[1] == 1);
  CHECK(sink.records == 7);
  qa_q2_mvd_byte_blocks_free(blocks, count);
  qa_q2_mvd_playback_destroy(&reader);
  /* A consumer selecting at the precache yield sees the rebound view in the same message's frame. */
  playback_init(&reader, &ctx);
  sink_state early = {SENTINEL, &reader, {0}, 0, 0, true};
  CHECK(qa_q2_mvd_playback_read_records(&reader, gs.data, gs.length, view_sink, &early));
  CHECK(early.view_count == 1 && early.views[0] == 1);
  qa_q2_mvd_playback_destroy(&reader);
  free(rec.out.data);
  free(gs.data);
}

static void test_mvd_playback_selected_projection(void) {
  vis_ctx ctx = {SENTINEL, VIS_SELECTED, 0};
  qa_q2_mvd_playback reader;
  playback_init(&reader, &ctx);
  bytes gs = unhex(MVD_GAMESTATE);
  read_and_free(&reader, gs.data, gs.length);
  qa_q2_wire_frame frame;
  CHECK(qa_q2_mvd_playback_projection(&reader, NULL, &frame));
  CHECK(frame.entity_count == 1 && frame.entities[0].number == 1);
  qa_q2_wire_frame_free(&frame);
  qa_q2_mvd_playback_select_player(&reader, 1);
  CHECK(qa_q2_mvd_playback_projection(&reader, NULL, &frame));
  CHECK(frame.entity_count == 1 && frame.entities[0].number == 2);
  qa_q2_wire_frame_free(&frame);
  CHECK(qa_q2_mvd_playback_player_count(&reader) == 2);
  free(gs.data);
  qa_q2_mvd_playback_destroy(&reader);
}

/* ---- demo-viewpoints.test.ts (reader layer; Q2DemoPlayback view selection is application) ---- */
static void test_kex_viewpoints(void) {
  bytes demo = unhex(KEX_VIEWPOINT_DEMO);
  qa_q2_server_message_reader r;
  reader_init(&r, true, identity("q2-kex-demo", 2022, 0), 12448);
  call play = {.kind = CALL_PLAY_DEMO, .data = demo.data, .length = demo.length, .reader = &r};
  qa_caught_error caught;
  bool returned = attempt(&play, &caught);
  CHECK(returned && play.ok && play.count == 5);
  if (!returned) fprintf(stderr, "  kex demo raised: %s\n", caught.message);
  size_t frames = 0;
  for (size_t i = 0; returned && i < play.count; i++) {
    for (size_t k = 0; k < play.steps[i].record_count; k++) {
      const qa_q2_server_event *e = &play.steps[i].records[k].event;
      if (e->kind == QA_Q2_SERVER_DATA) {
        CHECK(e->data.has_clientnums && e->data.clientnum_count == 2 && e->data.clientnums[0] == 2 &&
              e->data.clientnums[1] == 7);
      }
      if (e->kind != QA_Q2_SERVER_FRAME) continue;
      frames++;
      const qa_q2_wire_frame *f = &e->frame;
      CHECK(f->player.fov == 90 && f->area_bits_length >= 1 && f->area_bits[0] == 1);
      CHECK(f->split_count == 1 && f->split_players[0].player.fov == 110 &&
            f->split_players[0].area_bits_length >= 1 && f->split_players[0].area_bits[0] == 2);
    }
  }
  CHECK(frames == 3);
  call_release(&play);
  qa_q2_server_message_reader_destroy(&r);
  free(demo.data);
}

/* ---- seeded mutation (tests/support/mutation.ts) ---- */
typedef struct rng { uint32_t state; } rng;
static double rng_next(rng *r) {
  r->state += 0x6d2b79f5u;
  uint32_t value = r->state;
  value = (value ^ (value >> 15)) * (value | 1u);
  value ^= value + (value ^ (value >> 7)) * (value | 61u);
  return (double)(value ^ (value >> 14)) / 4294967296.0;
}
static size_t pick(rng *r, size_t limit) { return (size_t)floor(rng_next(r) * (double)limit); }
static bytes mutate(const bytes *source, rng *r) {
  bytes out = {malloc(source->length + 16), source->length};
  memcpy(out.data, source->data, source->length);
  switch (pick(r, 6)) {
    case 0:
      if (out.length > 0) {
        size_t at = pick(r, out.length);
        out.data[at] ^= (uint8_t)(1u << pick(r, 8));
      }
      break;
    case 1:
      for (size_t count = 1 + pick(r, 4); count > 0 && out.length > 0; count--) {
        size_t at = pick(r, out.length);
        out.data[at] = (uint8_t)pick(r, 256);
      }
      break;
    case 2: out.length = pick(r, out.length + 1); break;
    case 3: {
      size_t at = pick(r, out.length + 1), extra = 1 + pick(r, 8);
      memmove(out.data + at + extra, out.data + at, out.length - at);
      for (size_t i = 0; i < extra; i++) out.data[at + i] = (uint8_t)pick(r, 256);
      out.length += extra;
      break;
    }
    case 4: {
      size_t at = pick(r, out.length + 1), room = out.length - at, cut = pick(r, (room < 8 ? room : 8) + 1);
      memmove(out.data + at, out.data + at + cut, out.length - at - cut);
      out.length -= cut;
      break;
    }
    default:
      if (out.length >= 4) {
        static const uint32_t words[] = {0xffffffffu, 0x7fffffffu, 0x80000000u, 0, 0x00010000u};
        size_t at = pick(r, out.length - 3);
        uint32_t word = words[pick(r, 5)];
        for (int i = 0; i < 4; i++) out.data[at + i] = (uint8_t)(word >> (8 * i));
      }
      break;
  }
  return out;
}
typedef struct line_hash { uint32_t hash; FILE *dump; } line_hash;
static void hash_line(line_hash *h, const char *line) {
  /* The oracle hashes TextEncoder output: a TS string char 0x80..0xff is two UTF-8 bytes. */
  for (const char *c = line; *c; c++) {
    uint8_t byte = (uint8_t)*c;
    uint8_t utf8[2] = {byte, 0};
    size_t n = 1;
    if (byte >= 0x80) {
      utf8[0] = (uint8_t)(0xc0 | (byte >> 6));
      utf8[1] = (uint8_t)(0x80 | (byte & 0x3f));
      n = 2;
    }
    for (size_t i = 0; i < n; i++) { h->hash ^= utf8[i]; h->hash *= 0x01000193u; }
  }
  h->hash ^= '\n';
  h->hash *= 0x01000193u;
  if (h->dump != NULL) fprintf(h->dump, "%s\n", line);
}
static char kind_letter(qa_q2_server_event_kind kind) {
  /* First letter of the TS kind: nop disconnect reconnect level-restart server-data print center-print command-text layout
   * achievement config-string baseline frame sound temp-entity muzzle-flash inventory download
   * setting seat damage localized-print fog poi help-path private */
  static const char map[] = {'n', 'd', 'r', 'l', 's', 'p', 'c', 'c', 'l', 'a', 'c', 'b', 'f', 's', 't', 'm',
                             'i', 'd', 's', 's', 'd', 'l', 'f', 'p', 'h', 'p'};
  return (size_t)kind < sizeof map ? map[kind] : '?';
}
typedef enum mut_target { MUT_TARGET_MVD, MUT_TARGET_MVD_HEADER, MUT_TARGET_DEMO } mut_target;
static void outcome(mut_target target, const bytes *mutant, char *line, size_t capacity) {
  qa_caught_error caught;
  char a[40], b[40], c2[40], d[40], e[40];
  if (target == MUT_TARGET_MVD) {
    vis_ctx ctx = {SENTINEL, VIS_FIXTURE, 0};
    qa_q2_mvd_playback reader;
    playback_init(&reader, &ctx);
    call c = {.kind = CALL_MVD_READ, .playback = &reader, .data = mutant->data, .length = mutant->length};
    if (attempt(&c, &caught) && c.ok) {
      size_t at = (size_t)snprintf(line, capacity, "A%zu:", c.count);
      for (size_t i = 0; i < c.count && at + 2 < capacity; i++) line[at++] = kind_letter(c.server_records[i].event.kind);
      snprintf(line + at, capacity - at, ":%zu", qa_q2_mvd_playback_player_count(&reader));
      call_release(&c);
    } else {
      snprintf(line, capacity, "R%s", caught.message);
    }
    qa_q2_mvd_playback_destroy(&reader);
  } else if (target == MUT_TARGET_MVD_HEADER) {
    call c = {.kind = CALL_MVD_HEADER, .data = mutant->data, .length = mutant->length};
    if (attempt(&c, &caught) && c.ok) {
      const qa_q2_mvd_header *h = &c.mvd_header;
      snprintf(line, capacity, "A%s/%s/%s/%s/%s/%zu", qa_net_js_number_text(h->profile.revision, a),
               qa_net_js_number_text(h->profile.flags, b), qa_net_js_number_text(h->max_clients, c2),
               qa_net_js_number_text(h->dummy, d), qa_net_js_number_text(h->frame_offset, e), h->config_string_count);
      call_release(&c);
    } else {
      snprintf(line, capacity, "R%s", caught.message);
    }
  } else {
    char framed[2200];
    call f = {.kind = CALL_READ_DEMO, .data = mutant->data, .length = mutant->length};
    if (attempt(&f, &caught) && f.ok) {
      snprintf(framed, sizeof framed, "%zu", f.count);
      call_release(&f);
    } else {
      snprintf(framed, sizeof framed, "!%s", caught.message);
    }
    call c = {.kind = CALL_DEMO_HEADER, .data = mutant->data, .length = mutant->length};
    if (attempt(&c, &caught) && c.ok) {
      const qa_q2_demo_header *h = &c.header;
      bool has_revision = strcmp(h->protocol.kind, "q2-r1q2") == 0 || strcmp(h->protocol.kind, "q2-q2pro") == 0;
      snprintf(line, capacity, "A%s/%s/%s/%s/%s/%s/%s", framed, qa_net_js_number_text(h->recorded_version, a),
               h->protocol.kind, has_revision ? qa_net_js_number_text(h->protocol.revision, b) : "",
               qa_net_js_number_text(h->data.servercount, c2), h->data.gamedir,
               qa_net_js_number_text(h->data.clientnum, d));
    } else {
      snprintf(line, capacity, "R%s", caught.message);
    }
  }
}
static void run_oracle(const char *name, mut_target target, const bytes *valid, const mutation_oracle *o) {
  rng r = {o->seed};
  line_hash h = {0x811c9dc5u, NULL};
  const char *dir = getenv("QA_MUTATION_DUMP");
  char path[512];
  if (dir != NULL) {
    snprintf(path, sizeof path, "%s/%s_%u.c.txt", dir, name, o->seed);
    h.dump = fopen(path, "w");
  }
  unsigned accepted = 0, rejected = 0;
  static char line[8192];
  for (unsigned index = 0; index < o->count; index++) {
    bytes mutant = mutate(valid, &r);
    outcome(target, &mutant, line, sizeof line);
    if (line[0] == 'A') accepted++;
    else rejected++;
    hash_line(&h, line);
    free(mutant.data);
  }
  if (h.dump != NULL) fclose(h.dump);
  CHECK(accepted == o->accepted && rejected == o->rejected);
  CHECK(h.hash == o->hash);
  if (h.hash != o->hash) {
    fprintf(stderr, "  %s seed %u: C %u/%u hash %08x, TS %u/%u hash %08x\n", name, o->seed, accepted, rejected,
            h.hash, o->accepted, o->rejected, o->hash);
  }
}
static void test_mutations(void) {
  bytes gs = unhex(MVD_GAMESTATE);
  run_oracle("MUT_MVD", MUT_TARGET_MVD, &gs, &MUT_MVD_41);
  run_oracle("MUT_MVD", MUT_TARGET_MVD, &gs, &MUT_MVD_42);
  run_oracle("MUT_MVD", MUT_TARGET_MVD, &gs, &MUT_MVD_43);
  run_oracle("MUT_MVDH", MUT_TARGET_MVD_HEADER, &gs, &MUT_MVDH_46);
  bytes two = {malloc(gs.length + 6), gs.length + 6};
  memcpy(two.data, gs.data, gs.length);
  const uint8_t delta[] = {6, 1, 0x55, 255, 0, 0};
  memcpy(two.data + gs.length, delta, sizeof delta);
  run_oracle("MUT_MVD2", MUT_TARGET_MVD, &two, &MUT_MVD2_44);
  run_oracle("MUT_MVD2", MUT_TARGET_MVD, &two, &MUT_MVD2_45);
  free(two.data);
  free(gs.data);
  bytes demo = unhex(DEMO_MUTATION_VALID);
  run_oracle("MUT_DEMO", MUT_TARGET_DEMO, &demo, &MUT_DEMO_61);
  run_oracle("MUT_DEMO", MUT_TARGET_DEMO, &demo, &MUT_DEMO_62);
  run_oracle("MUT_DEMO", MUT_TARGET_DEMO, &demo, &MUT_DEMO_63);
  free(demo.data);
  bytes r1 = unhex(DEMO_MUTATION_R1Q2);
  run_oracle("MUT_DEMO_R1", MUT_TARGET_DEMO, &r1, &MUT_DEMO_R1_64);
  free(r1.data);
}

/* ---- network.test.ts: classic packet matches the source scalar and MOREBITS layout ---- */
static void test_classic_entity_layout(void) {
  qa_q2_wire_codec wire;
  CHECK(qa_q2_wire_codec_init(&wire, identity("q2-classic", 34, 0)));
  qa_q2_sizebuf m;
  CHECK(qa_q2_message_create(&m, 32768));
  qa_q2_wire_entity from, to;
  qa_q2_wire_entity_init(&from);
  qa_q2_wire_entity_init(&to);
  from.number = 10;
  to.number = 10;
  to.origin[0] = 16;
  to.origin[1] = -8;
  to.origin[2] = 0;
  to.angles[1] = 45;
  qa_q2_protocol_codec *codec = qa_q2_wire_codec_get_codec(&wire);
  codec->write_delta_entity(codec, &m, &from, &to, false, false);
  size_t length = 0;
  uint8_t *out = qa_q2_message_bytes(&m, &length);
  const uint8_t expected[] = {7, 10, 128, 0, 192, 255, 32};
  CHECK(length == sizeof expected && memcmp(out, expected, length) == 0);
  free(out);
  qa_q2_message_destroy(&m);
  qa_q2_wire_codec_destroy(&wire);
}

int main(void) {
  test_classic_entity_layout();
  test_demo_header_protocols();
  test_demo_header_grammar();
  test_demo_header_rejects();
  test_old_demo_opt_in();
  test_old_demo_legacy_frames();
  test_old_demo_failed_serverdata();
  test_mvd_encoding_profiles();
  test_mvd_encoding_recipients();
  test_mvd_playback_gamestate();
  test_mvd_playback_rejects();
  test_mvd_playback_layouts();
  test_mvd_playback_recorded_views();
  test_mvd_playback_selected_projection();
  test_kex_viewpoints();
  test_mutations();
  printf("q2 mvd/demo: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
