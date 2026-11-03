/* Q2PRO extended fields and codec, KEX demo readers and server-demo footage against bytes and
 * decodes captured from the TypeScript (q2_codec_readers_corpus.h), the first case of
 * tests/network/q2/server-demo.test.ts, and fixed-seed truncate/mutate loops per decoder family.
 * GPL-2.0-or-later. */
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/network/common/js_number.h"
#include "qa/network/q2/codecs/kexdemo.h"
#include "qa/network/q2/codecs/q2pro.h"
#include "qa/network/q2/codecs/q2pro_fields.h"
#include "qa/network/q2/demo.h"
#include "qa/network/q2/message.h"
#include "qa/network/q2/server_demo.h"
#include "qa/network/q2/state.h"
#include "q2_codec_readers_corpus.h"

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
  fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define COUNT(a) (sizeof(a) / sizeof *(a))

/* ---- text builder: the dump format of the bun script ---- */
typedef struct text { char *s; size_t n, cap; } text;
static void add(text *t, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  va_list copy;
  va_copy(copy, ap);
  int need = vsnprintf(NULL, 0, fmt, copy);
  va_end(copy);
  if (t->n + (size_t)need + 1 > t->cap) {
    t->cap = (t->n + (size_t)need + 1) * 2;
    t->s = (char *)realloc(t->s, t->cap);
  }
  vsnprintf(t->s + t->n, (size_t)need + 1, fmt, ap);
  t->n += (size_t)need;
  va_end(ap);
}
static void num(text *t, double v) { add(t, "%s", QA_JS_NUMBER(v)); }
static void nums_f(text *t, const float *v, size_t n) {
  for (size_t i = 0; i < n; i++) { if (i) add(t, ","); num(t, v[i]); }
}
static void nums_d(text *t, const double *v, size_t n) {
  for (size_t i = 0; i < n; i++) { if (i) add(t, ","); num(t, v[i]); }
}
static void nums_i32(text *t, const int32_t *v, size_t n) {
  for (size_t i = 0; i < n; i++) add(t, i ? ",%d" : "%d", (int)v[i]);
}
static void nums_i16(text *t, const int16_t *v, size_t n) {
  for (size_t i = 0; i < n; i++) add(t, i ? ",%d" : "%d", (int)v[i]);
}
static void reset(text *t) { t->n = 0; if (t->s) t->s[0] = '\0'; else add(t, "%s", ""); }
static const char *str(text *t) { if (!t->s) add(t, "%s", ""); return t->s; }

static void ent(text *t, const qa_q2_wire_entity *e) {
  add(t, "n="); num(t, e->number);
  add(t, " o="); nums_f(t, e->origin, 3);
  add(t, " a="); nums_f(t, e->angles, 3);
  add(t, " oo="); nums_f(t, e->old_origin, 3);
  add(t, " m="); num(t, e->modelindex); add(t, ","); num(t, e->modelindex2); add(t, ","); num(t, e->modelindex3); add(t, ","); num(t, e->modelindex4);
  const struct { const char *k; double v; } f[] = {
    {"f", e->frame}, {"sk", e->skinnum}, {"fx", e->effects}, {"rfx", e->renderfx}, {"so", e->solid}, {"snd", e->sound},
    {"ev", e->event}, {"al", e->alpha}, {"sc", e->scale}, {"ib", e->instance_bits}, {"lv", e->loop_volume},
    {"la", e->loop_attenuation}, {"ow", e->owner}, {"of", e->old_frame}, {"mfx", e->morefx}};
  for (size_t i = 0; i < COUNT(f); i++) { add(t, " %s=", f[i].k); num(t, f[i].v); }
}
static void fog(text *t, const qa_q2_pro_player_fog *f) {
  nums_d(t, f->color, 3); add(t, ";"); num(t, f->density); add(t, ";"); num(t, f->sky_factor); add(t, ";");
  num(t, f->height_density); add(t, ";"); num(t, f->height_falloff); add(t, ";");
  nums_d(t, f->height_start_color, 3); add(t, ";"); nums_d(t, f->height_end_color, 3); add(t, ";");
  num(t, f->height_start_distance); add(t, ";"); num(t, f->height_end_distance);
}
static void ply(text *t, const qa_q2_wire_player *p) {
  const qa_q2_wire_pmove *m = &p->pmove;
  add(t, "pt="); num(t, m->pm_type); add(t, " po="); nums_i32(t, m->origin, 3); add(t, " pv="); nums_i32(t, m->velocity, 3);
  add(t, " pf="); num(t, m->pm_flags); add(t, " ptm="); num(t, m->pm_time); add(t, " g="); num(t, m->gravity);
  add(t, " da="); nums_i16(t, m->delta_angles, 3); add(t, " vh="); num(t, m->viewheight);
  add(t, " pof="); nums_f(t, m->origin_f, 3); add(t, " pvf="); nums_f(t, m->velocity_f, 3);
  add(t, " daf="); nums_f(t, m->delta_angles_f, 3); add(t, " dae=%d", m->delta_angle_float ? 1 : 0);
  add(t, " va="); nums_f(t, p->viewangles, 3); add(t, " vo="); nums_f(t, p->viewoffset, 3);
  add(t, " ka="); nums_f(t, p->kick_angles, 3); add(t, " ga="); nums_f(t, p->gunangles, 3); add(t, " go="); nums_f(t, p->gunoffset, 3);
  add(t, " gi="); num(t, p->gunindex); add(t, " gs="); num(t, p->gunskin); add(t, " gf="); num(t, p->gunframe); add(t, " gr="); num(t, p->gunrate);
  add(t, " bl="); nums_f(t, p->blend, 4); add(t, " db="); nums_f(t, p->damage_blend, 4);
  add(t, " fov="); num(t, p->fov); add(t, " rd="); num(t, p->rdflags);
  add(t, " st="); nums_i16(t, p->stats, QA_Q2_MAX_STATS_STORAGE);
  add(t, " tid="); num(t, p->team_id); add(t, " cn="); num(t, p->clientnum); add(t, " fog="); fog(t, &p->q2pro_fog);
}

/* ---- bytes ---- */
static uint8_t *unhex(const char *h, size_t *len) {
  size_t n = strlen(h) / 2;
  uint8_t *b = (uint8_t *)malloc(n > 0 ? n : 1);
  for (size_t i = 0; i < n; i++) { unsigned v; sscanf(h + 2 * i, "%2x", &v); b[i] = (uint8_t)v; }
  *len = n;
  return b;
}
static void tohex(text *t, const qa_q2_sizebuf *m) {
  for (int64_t i = 0; i < m->cursize; i++) add(t, "%02x", m->data[i]);
}
static void reader(qa_q2_sizebuf *m, uint8_t *bytes, size_t len) {
  qa_q2_sizebuf_init_empty(m);
  qa_q2_message_load(m, bytes, len);
}
static void expect(const char *label, const char *got, const char *want) {
  checks++;
  if (strcmp(got, want) != 0) {
    failures++;
    fprintf(stderr, "FAIL %s\n got %s\n exp %s\n", label, got, want);
  }
}
/* ClcBatchMoveError extends Error without setting name, so its TS error.name is "Error"; the C
 * name carries the class for the instanceof test. */
static void caught_text(text *t, const qa_caught_error *c) {
  add(t, "ERR:%s:%s", strcmp(c->name, "ClcBatchMoveError") == 0 ? "Error" : c->name, c->message);
}

/* ---- fixtures mirrored from dump.ts ---- */
static void set3(float *v, double x, double y, double z) { v[0] = (float)x; v[1] = (float)y; v[2] = (float)z; }
static qa_q2_wire_entity mk_ent(int k) {
  qa_q2_wire_entity e;
  qa_q2_wire_entity_init(&e);
  if (k == 1 || k == 4) {
    e.number = k == 4 ? 8192 : 5; set3(e.origin, 320.5, -160.25, 8); set3(e.angles, 11, 22, 33); set3(e.old_origin, 300, -150, 4);
    e.modelindex = 5; e.modelindex2 = 6; e.modelindex3 = 7; e.modelindex4 = 8; e.frame = 300; e.skinnum = 2; e.effects = 0x10000;
    e.renderfx = 64; e.solid = 31; e.sound = 4; e.event = 2;
  }
  if (k == 2) {
    e.number = 300; set3(e.origin, 100000, -200000.125, 3); set3(e.angles, 1, 2, 3); set3(e.old_origin, 1, 2, 3); e.modelindex = 400;
    e.frame = 10; e.skinnum = 70000; e.effects = 300; e.renderfx = 128; e.morefx = 5; e.alpha = 0.5; e.scale = 2; e.sound = 500;
    e.loop_volume = 0.5; e.loop_attenuation = -1;
  }
  if (k == 3) {
    e.number = 7; set3(e.origin, -1, 0, 0.0625); e.alpha = 3; e.scale = 0.01; e.loop_attenuation = 0.25; e.renderfx = 128;
    set3(e.old_origin, 9, 9, 9); e.event = 1;
  }
  return e;
}
static qa_q2_pro_player_fog zero_fog(void) { qa_q2_pro_player_fog f; memset(&f, 0, sizeof f); return f; }
static qa_q2_pro_player_fog fog_a(void) {
  qa_q2_pro_player_fog f = {{10, 20, 300}, 5, -2, 70000, 9, {1, 2, 3}, {4, 5, 6}, 4000, -100000};
  return f;
}
static qa_q2_pro_player_fog fog_n(int k) {
  qa_q2_pro_player_fog f = k == 0 ? zero_fog() : fog_a();
  if (k == 2) { f.density = 6; f.height_end_distance = 0.5; }
  return f;
}
static qa_q2_wire_player mk_ply(int k) {
  qa_q2_wire_player p;
  qa_q2_wire_player_init(&p);
  if (k >= 1) {
    p.pmove.pm_type = 2; p.pmove.origin[0] = 1600; p.pmove.origin[1] = -800; p.pmove.origin[2] = 2400;
    p.pmove.velocity[0] = 40; p.pmove.velocity[1] = -50; p.pmove.velocity[2] = 60; p.pmove.pm_flags = 3; p.pmove.pm_time = 9;
    p.pmove.gravity = 800; p.pmove.delta_angles[0] = 100; p.pmove.delta_angles[1] = -200; p.pmove.delta_angles[2] = 300;
    set3(p.viewangles, 10, 20, 30); set3(p.viewoffset, 1, 2, 22); set3(p.kick_angles, 1.5, -1.5, 2); set3(p.gunangles, 0.5, 1, -1);
    set3(p.gunoffset, 1, -1, 0.5); p.gunindex = 7; p.gunframe = 12;
    p.blend[0] = 0.5f; p.blend[1] = 0.25f; p.blend[2] = 0.125f; p.blend[3] = 0.75f;
    p.damage_blend[0] = 0.25f; p.damage_blend[1] = 0.5f; p.damage_blend[2] = 0.125f; p.damage_blend[3] = 0.5f;
    p.fov = 100; p.rdflags = 4; p.stats[0] = 5; p.stats[2] = 100; p.stats[31] = -9; p.stats[40] = 7; p.clientnum = 3;
  }
  if (k == 2) {
    p.gunskin = 2; p.gunindex = 300; p.pmove.origin[0] = 4194303; p.pmove.origin[1] = -4194304; p.pmove.origin[2] = 100000;
    p.q2pro_fog = fog_a(); p.stats[63] = 1; p.clientnum = 300; p.pmove.pm_time = 1000; p.pmove.pm_flags = 0x1234;
  }
  if (k == 3) { p.damage_blend[0] = 0; p.damage_blend[1] = 0; p.damage_blend[2] = 0; p.damage_blend[3] = 1; }
  return p;
}
static qa_q2_wire_entity mk_kex_ent(int k) {
  qa_q2_wire_entity e;
  qa_q2_wire_entity_init(&e);
  e.number = k == 1 ? 3 : 700;
  if (k == 1) set3(e.origin, 10.5, -20.25, 30); else set3(e.origin, 1e6, 0.1, -3);
  set3(e.angles, 90, 180, 270); set3(e.old_origin, 1, 2, 3);
  e.modelindex = k == 1 ? 2 : 300; e.modelindex2 = 5; e.frame = 400; e.skinnum = 70000; e.effects = 0x12345678; e.morefx = 0x9;
  e.renderfx = 300; e.solid = k == 1 ? 0 : 0x123; e.sound = 1000; e.loop_volume = 0.5; e.loop_attenuation = 2; e.event = 3;
  e.alpha = 0.25; e.scale = 2; e.instance_bits = 5; e.owner = 99; e.old_frame = 7;
  return e;
}

/* ---- q2pro fields ---- */
typedef struct field_job {
  int kind;
  qa_q2_sizebuf *m;
  double a, b;
  uint64_t u;
  qa_q2pro_features features;
  const qa_q2_wire_entity *from, *to;
  bool force, fresh;
  int fog_bits;
  const qa_q2_pro_player_fog *fog;
} field_job;
static void field_run(void *arg) {
  field_job *j = (field_job *)arg;
  switch (j->kind) {
    case 0: qa_q2pro_write_int23_number(j->m, j->a, j->b); break;
    case 1: j->u = qa_q2pro_read_var64(j->m); break;
    case 2: qa_q2pro_write_fog(j->m, j->fog_bits, j->fog); break;
    case 3: qa_q2pro_write_entity(j->m, j->features, j->from, j->to, j->force, j->fresh); break;
  }
}
static void test_fields(void) {
  text t = {0};
  char label[96];
  for (size_t i = 0; i < COUNT(q2pro_int23_writes); i++) {
    qa_q2_sizebuf m; qa_q2_message_create(&m, 64);
    field_job j = {.kind = 0, .m = &m, .a = q2pro_int23_writes[i].current, .b = q2pro_int23_writes[i].previous};
    qa_caught_error c; reset(&t);
    if (qa_try(field_run, &j, &c)) tohex(&t, &m); else caught_text(&t, &c);
    snprintf(label, sizeof label, "int23 write %zu", i);
    expect(label, str(&t), q2pro_int23_writes[i].hex);
    qa_q2_message_destroy(&m);
  }
  for (size_t i = 0; i < COUNT(q2pro_int23_reads); i++) {
    size_t len; uint8_t *b = unhex(q2pro_int23_reads[i].hex, &len); qa_q2_sizebuf m; reader(&m, b, len);
    reset(&t); num(&t, qa_q2pro_read_int23_number(&m, q2pro_int23_reads[i].previous));
    snprintf(label, sizeof label, "int23 read %zu", i);
    expect(label, str(&t), q2pro_int23_reads[i].value);
    CHECK(m.readcount == q2pro_int23_reads[i].readcount);
    free(b);
  }
  /* The Int32Array element form wraps like an Int32Array store. */
  {
    uint8_t b[2] = {0xfe, 0x7f}; qa_q2_sizebuf m; reader(&m, b, 2);
    CHECK(qa_q2pro_read_int23(&m, INT32_MAX) == INT32_MIN + 16382);
  }
  for (size_t i = 0; i < COUNT(q2pro_var64_reads); i++) {
    size_t len; uint8_t *b = unhex(q2pro_var64_reads[i].hex, &len); qa_q2_sizebuf m; reader(&m, b, len);
    field_job j = {.kind = 1, .m = &m}; qa_caught_error c; reset(&t);
    if (qa_try(field_run, &j, &c)) add(&t, "%llu", (unsigned long long)j.u); else caught_text(&t, &c);
    snprintf(label, sizeof label, "var64 read %zu", i);
    expect(label, str(&t), q2pro_var64_reads[i].value);
    CHECK(m.readcount == q2pro_var64_reads[i].readcount);
    free(b);
  }
  for (size_t i = 0; i < COUNT(q2pro_var64_writes); i++) {
    qa_q2_sizebuf m; qa_q2_message_create(&m, 64);
    qa_q2pro_write_var64(&m, q2pro_var64_writes[i].value);
    reset(&t); tohex(&t, &m);
    expect("var64 write", str(&t), q2pro_var64_writes[i].hex);
    qa_q2_message_destroy(&m);
  }
  for (size_t i = 0; i < COUNT(q2pro_fogs); i++) {
    qa_q2_pro_player_fog from = fog_n(q2pro_fogs[i].from), to = fog_n(q2pro_fogs[i].to);
    int bits = qa_q2pro_fog_bits(&from, &to);
    CHECK(bits == q2pro_fogs[i].bits);
    qa_q2_sizebuf m; qa_q2_message_create(&m, 256);
    field_job j = {.kind = 2, .m = &m, .fog_bits = bits, .fog = &to};
    qa_caught_error c; reset(&t);
    text d = {0}; reset(&d);
    if (qa_try(field_run, &j, &c)) {
      tohex(&t, &m);
      qa_q2_sizebuf r; reader(&r, m.data, (size_t)m.cursize);
      qa_q2_pro_player_fog out; qa_q2pro_read_fog(&r, &from, &out); fog(&d, &out);
    } else caught_text(&t, &c);
    snprintf(label, sizeof label, "fog %zu", i);
    expect(label, str(&t), q2pro_fogs[i].hex);
    expect(label, str(&d), q2pro_fogs[i].decoded);
    free(d.s);
    qa_q2_message_destroy(&m);
  }
  for (size_t i = 0; i < COUNT(q2pro_fog_reads); i++) {
    size_t len; uint8_t *b = unhex(q2pro_fog_reads[i].hex, &len); qa_q2_sizebuf m; reader(&m, b, len);
    qa_q2_pro_player_fog from = fog_a(), out; qa_q2pro_read_fog(&m, &from, &out);
    reset(&t); fog(&t, &out);
    snprintf(label, sizeof label, "fog read %zu", i);
    expect(label, str(&t), q2pro_fog_reads[i].decoded);
    CHECK(m.readcount == q2pro_fog_reads[i].readcount);
    /* readQ2ProFog(message, from) into the same storage. */
    reader(&m, b, len); qa_q2pro_read_fog(&m, &from, &from);
    reset(&t); fog(&t, &from);
    expect(label, str(&t), q2pro_fog_reads[i].decoded);
    free(b);
  }
  for (size_t i = 0; i < COUNT(q2pro_entities); i++) {
    qa_q2pro_features features = {q2pro_entities[i].revision, q2pro_entities[i].flags};
    qa_q2_wire_entity from = mk_ent(q2pro_entities[i].from), to = mk_ent(q2pro_entities[i].to);
    qa_q2_sizebuf m; qa_q2_message_create(&m, 256);
    field_job j = {.kind = 3, .m = &m, .features = features, .from = &from, .to = &to,
                   .force = q2pro_entities[i].force, .fresh = q2pro_entities[i].new_entity};
    qa_caught_error c; reset(&t);
    text d = {0}; reset(&d);
    if (qa_try(field_run, &j, &c)) {
      tohex(&t, &m);
      if (m.cursize > 0) {
        qa_q2_sizebuf r; reader(&r, m.data, (size_t)m.cursize);
        qa_q2_entity_bits eb; qa_q2pro_read_entity_bits(&r, &eb);
        qa_q2_wire_entity out; qa_q2_wire_entity_init(&out);
        qa_q2pro_read_entity(&r, features, &from, &out, eb.number, eb.bits_u64);
        add(&d, "bits="); num(&d, eb.bits); add(&d, " rc=%lld ", (long long)r.readcount); ent(&d, &out);
      }
    } else caught_text(&t, &c);
    snprintf(label, sizeof label, "entity %zu", i);
    expect(label, str(&t), q2pro_entities[i].hex);
    expect(label, str(&d), q2pro_entities[i].decoded);
    free(d.s);
    qa_q2_message_destroy(&m);
  }
  for (size_t i = 0; i < COUNT(q2pro_entity_reads); i++) {
    qa_q2pro_features features = {q2pro_entity_reads[i].revision, q2pro_entity_reads[i].flags};
    size_t len; uint8_t *b = unhex(q2pro_entity_reads[i].hex, &len); qa_q2_sizebuf m; reader(&m, b, len);
    qa_q2_entity_bits eb; qa_q2pro_read_entity_bits(&m, &eb);
    qa_q2_wire_entity from = mk_ent(1), out; qa_q2_wire_entity_init(&out);
    qa_q2pro_read_entity(&m, features, &from, &out, eb.number, eb.bits_u64);
    reset(&t); add(&t, "num="); num(&t, eb.number); add(&t, " bits="); num(&t, eb.bits);
    add(&t, " rc=%lld ", (long long)m.readcount); ent(&t, &out);
    snprintf(label, sizeof label, "entity read %zu", i);
    expect(label, str(&t), q2pro_entity_reads[i].decoded);
    free(b);
  }
  free(t.s);
}

/* ---- q2pro codec ---- */
static void write_end(void *ctx, qa_q2_sizebuf *msg) {
  qa_q2_protocol_codec *codec = (qa_q2_protocol_codec *)ctx;
  codec->write_packet_entities_end(codec, msg);
}
typedef struct pro_job {
  int kind;
  qa_q2pro_context *ctx;
  qa_q2_sizebuf *m;
  const qa_q2_wire_player *from, *to;
  int a;
  text *d;
  const qa_q2_clc_batch_move_frame *frames;
  size_t frame_count;
  bool has_lastframe, nodelta;
  double lastframe, dups;
  qa_q2_clc_batch_move move;
  bool move_ok;
} pro_job;
static void server_dump(text *d, const qa_q2_server_data_result *rs, bool hacks) {
  add(d, "sc="); num(d, rs->servercount); add(d, " al=%d gd=%s cn=", rs->attractloop ? 1 : 0, rs->gamedir); num(d, rs->clientnum);
  add(d, " ln=%s ss=", rs->levelname); num(d, rs->server_state); add(d, " v="); num(d, rs->q2pro_version); add(d, " wf="); num(d, rs->wire_flags);
  if (hacks) add(d, " h=%d%d%d", rs->q2pro_strafejump_hack ? 1 : 0, rs->q2pro_qw_mode ? 1 : 0, rs->q2pro_waterjump_hack ? 1 : 0);
}
static void pro_run(void *arg) {
  pro_job *j = (pro_job *)arg;
  qa_q2_protocol_codec *codec = &j->ctx->codec;
  if (j->kind == 0) {
    codec->write_player_state_delta(codec, j->m, j->from, j->to);
    qa_q2_sizebuf r; reader(&r, j->m->data, (size_t)j->m->cursize);
    (void)qa_q2_msg_read_byte(&r);
    qa_q2_wire_player t = mk_ply(0); t.pmove.viewheight = 5; t.gunrate = 9; t.team_id = 4;
    codec->read_player_state_delta(codec, &r, j->from, &t);
    add(j->d, "rc=%lld ", (long long)r.readcount); ply(j->d, &t);
  } else if (j->kind == 1) {
    uint8_t areabits[2] = {7, 0};
    qa_q2_frame_write_params params;
    memset(&params, 0, sizeof params);
    params.framenum = 100; params.lastframe = j->a == 1 ? 98 : -1; params.surpress_count = 3; params.areabytes = 2;
    params.areabits = areabits; params.areabits_length = 2; params.ps_from = j->a == 0 ? NULL : j->from; params.ps_to = j->to;
    codec->write_frame(codec, j->m, &params, write_end, codec);
    qa_q2_sizebuf *net = j->ctx->net_message;
    qa_q2_message_load(net, j->m->data, (size_t)j->m->cursize);
    int32_t op = qa_q2_msg_read_byte(net);
    qa_q2pro_note_frame_opcode_extrabits(j->ctx, op & 0xe0);
    uint8_t got[1] = {0};
    qa_q2_frame_header h;
    codec->read_frame_header(codec, got, 1, true, &h);
    qa_q2_wire_player t = mk_ply(0);
    codec->read_frame_playerstate(codec, j->from, &t);
    add(j->d, "op=%d sf=", (int)op); num(j->d, h.serverframe); add(j->d, " df="); num(j->d, h.deltaframe);
    add(j->d, " sc="); num(j->d, h.surpress_count); add(j->d, " ab="); num(j->d, h.areabytes);
    add(j->d, " a0=%d rc=%lld ", got[0], (long long)net->readcount); ply(j->d, &t);
  } else if (j->kind == 2) {
    codec->write_batch_move(codec, j->m, j->has_lastframe, j->lastframe, j->frames, j->frame_count);
  } else {
    j->move_ok = codec->read_batch_move(codec, j->m, j->nodelta, j->dups, &j->move);
  }
}
static qa_q2_wire_usercmd mk_cmd(double msec, double buttons, double fwd, double side, double up, double light,
                                 int a0, int a1, int a2) {
  qa_q2_wire_usercmd c; qa_q2_wire_usercmd_init(&c);
  c.msec = msec; c.buttons = buttons; c.forwardmove = fwd; c.sidemove = side; c.upmove = up; c.lightlevel = light;
  c.angles[0] = (int16_t)a0; c.angles[1] = (int16_t)a1; c.angles[2] = (int16_t)a2;
  return c;
}
static void move_dump(text *d, const qa_q2_clc_batch_move *mv, int64_t rc, bool full) {
  add(d, "lf="); num(d, mv->lastframe); add(d, " nd="); num(d, mv->num_dups); add(d, " rc=%lld", (long long)rc);
  if (!full) add(d, " frames=%zu", mv->frame_count);
  for (size_t f = 0; f < mv->frame_count; f++) {
    if (!full) { add(d, " %zu", mv->frames[f].cmd_count); continue; }
    add(d, " [");
    for (size_t c = 0; c < mv->frames[f].cmd_count; c++) {
      const qa_q2_wire_usercmd *u = &mv->frames[f].cmds[c];
      if (c) add(d, " ");
      num(d, u->msec); add(d, "/"); num(d, u->buttons); add(d, "/"); nums_i16(d, u->angles, 3); add(d, "/");
      num(d, u->forwardmove); add(d, "/"); num(d, u->sidemove); add(d, "/"); num(d, u->upmove); add(d, "/"); num(d, u->lightlevel);
    }
    add(d, "]");
  }
}
static void test_q2pro_codec(void) {
  text t = {0}, d = {0};
  char label[96];
  for (size_t i = 0; i < COUNT(q2pro_codecs); i++) {
    qa_q2_sizebuf net; qa_q2_sizebuf_init_empty(&net);
    qa_q2pro_context ctx; qa_q2pro_context_init(&ctx, &net);
    qa_q2_protocol_codec *codec = &ctx.codec;
    qa_q2_server_data_params params;
    memset(&params, 0, sizeof params);
    params.servercount = 77; params.gamedir = "baseq2"; params.clientnum = 3; params.levelname = "q2dm1"; params.server_state = 2;
    params.has_q2pro_version = true; params.q2pro_version = q2pro_codecs[i].revision;
    params.has_wire_flags = true; params.wire_flags = q2pro_codecs[i].flags;
    qa_q2_sizebuf m; qa_q2_message_create(&m, 4096);
    codec->write_server_data(codec, &m, &params);
    reset(&t); tohex(&t, &m);
    snprintf(label, sizeof label, "q2pro server data %zu", i);
    expect(label, str(&t), q2pro_codecs[i].server_data);
    qa_q2_message_load(&net, m.data + 5, (size_t)m.cursize - 5);
    static qa_q2_server_data_result rs;
    codec->read_server_data(codec, &rs);
    reset(&d); server_dump(&d, &rs, true); add(&d, " rc=%lld", (long long)net.readcount);
    expect(label, str(&d), q2pro_codecs[i].server_data_decoded);
    qa_q2_message_destroy(&m);
    for (size_t r = 0; r < COUNT(q2pro_codecs[i].rows); r++) {
      const q2pro_player_row *row = &q2pro_codecs[i].rows[r];
      qa_q2_wire_player from = mk_ply(row->from), to = mk_ply(row->to);
      for (int kind = 0; kind < 2; kind++) {
        qa_q2_message_create(&m, 4096);
        reset(&t); reset(&d);
        pro_job j = {.kind = kind, .ctx = &ctx, .m = &m, .from = &from, .to = &to, .a = row->from, .d = &d};
        qa_caught_error c;
        if (qa_try(pro_run, &j, &c)) tohex(&t, &m); else { caught_text(&t, &c); reset(&d); }
        snprintf(label, sizeof label, "q2pro %s %g/%g %d->%d", kind ? "frame" : "delta", q2pro_codecs[i].revision,
                 q2pro_codecs[i].flags, row->from, row->to);
        expect(label, str(&t), kind ? row->frame : row->delta);
        expect(label, str(&d), kind ? row->frame_decoded : row->delta_decoded);
        qa_q2_message_destroy(&m);
      }
    }
    qa_q2_sizebuf_init_empty(&net);
  }
  /* Server data fallbacks and truncated reads. */
  {
    qa_q2_sizebuf net; qa_q2_sizebuf_init_empty(&net);
    qa_q2pro_context ctx; qa_q2pro_context_init(&ctx, &net);
    qa_q2_protocol_codec *codec = &ctx.codec;
    for (size_t i = 0; i < COUNT(q2pro_server_data_extra); i++) {
      const char *label_text = q2pro_server_data_extra[i].label;
      if (strncmp(label_text, "read:", 5) == 0) {
        size_t len; uint8_t *b = unhex(label_text + 5, &len);
        qa_q2_message_load(&net, b, len);
        static qa_q2_server_data_result rs;
        codec->read_server_data(codec, &rs);
        reset(&d); server_dump(&d, &rs, false); add(&d, " rc=%lld", (long long)net.readcount);
        expect(label_text, str(&d), q2pro_server_data_extra[i].value);
        qa_q2_sizebuf_init_empty(&net);
        free(b);
        continue;
      }
      qa_q2_server_data_params params;
      memset(&params, 0, sizeof params);
      params.servercount = -5; params.attractloop = true; params.gamedir = ""; params.clientnum = -1; params.levelname = "x";
      params.server_state = 1;
      if (i == 0) { params.has_q2pro_strafejump_hack = params.q2pro_strafejump_hack = true; params.has_q2pro_waterjump_hack = params.q2pro_waterjump_hack = true; }
      if (i == 1) { params.has_q2pro_version = true; params.q2pro_version = 1020; params.has_q2pro_qw_mode = params.q2pro_qw_mode = true; }
      if (i == 2) { params.has_q2pro_version = true; params.q2pro_version = 1030; params.has_wire_flags = true; params.wire_flags = 0x1ff; }
      qa_q2_sizebuf m; qa_q2_message_create(&m, 4096);
      codec->write_server_data(codec, &m, &params);
      reset(&t); tohex(&t, &m);
      expect(label_text, str(&t), q2pro_server_data_extra[i].value);
      qa_q2_message_destroy(&m);
    }
  }
  /* Batch moves. */
  {
    qa_q2_sizebuf net; qa_q2_sizebuf_init_empty(&net);
    qa_q2pro_context ctx; qa_q2pro_context_init(&ctx, &net);
    qa_q2_wire_usercmd c1 = mk_cmd(16, 0x81, 600, -700, 3.7, 9, 100, -300, 5);
    qa_q2_wire_usercmd c2 = mk_cmd(16, 1, 200, -700, 3.7, 77, 100, 20000, -5);
    qa_q2_wire_usercmd c3 = mk_cmd(33, 0, NAN, 0, 0, 0, 0, 0, 0);
    qa_q2_wire_usercmd first[2] = {c1, c2}, second[1] = {c3}, only[1] = {c1};
    qa_q2_clc_batch_move_frame frames0[2] = {{first, 2}, {second, 1}}, frames1[2] = {{NULL, 0}, {only, 1}};
    size_t row = 0;
    for (int k = 0; k < 2; k++, row++) {
      qa_q2_sizebuf m; qa_q2_message_create(&m, 4096);
      pro_job j = {.kind = 2, .ctx = &ctx, .m = &m, .frames = k ? frames1 : frames0, .frame_count = 2,
                   .has_lastframe = k == 0, .lastframe = 55};
      qa_caught_error c;
      reset(&t); reset(&d);
      if (qa_try(pro_run, &j, &c)) {
        tohex(&t, &m);
        qa_q2_sizebuf r; reader(&r, m.data, (size_t)m.cursize);
        pro_job rj = {.kind = 3, .ctx = &ctx, .m = &r, .nodelta = k == 1, .dups = 1};
        if (qa_try(pro_run, &rj, &c)) { move_dump(&d, &rj.move, r.readcount, true); qa_q2_clc_batch_move_free(&rj.move); }
        else caught_text(&d, &c);
      } else caught_text(&t, &c);
      expect("batch write", str(&t), q2pro_batches[row].hex);
      expect("batch read", str(&d), q2pro_batches[row].decoded);
      qa_q2_message_destroy(&m);
    }
    for (; row < COUNT(q2pro_batches); row++) {
      char hexbuf[64] = {0}, nodelta[8] = {0};
      double dups = 0;
      const char *label_text = q2pro_batches[row].hex;
      const char *p = label_text + 5, *colon = strchr(p, ':');
      memcpy(hexbuf, p, (size_t)(colon - p));
      sscanf(colon + 1, "%7[^:]:%lf", nodelta, &dups);
      size_t len; uint8_t *b = unhex(hexbuf, &len); qa_q2_sizebuf r; reader(&r, b, len);
      pro_job rj = {.kind = 3, .ctx = &ctx, .m = &r, .nodelta = strcmp(nodelta, "true") == 0, .dups = dups};
      qa_caught_error c; reset(&d);
      if (qa_try(pro_run, &rj, &c)) { move_dump(&d, &rj.move, r.readcount, false); qa_q2_clc_batch_move_free(&rj.move); }
      else caught_text(&d, &c);
      expect(label_text, str(&d), q2pro_batches[row].decoded);
      free(b);
    }
  }
  free(t.s); free(d.s);
}

/* ---- KEX readers ---- */
typedef struct kex_job {
  qa_q2_kex_context *k;
  const char *reader;
  text *d;
  int iterations;
} kex_job;
static void kex_run(void *arg) {
  kex_job *j = (kex_job *)arg;
  qa_q2_kex_context *k = j->k;
  text *d = j->d;
  const char *r = j->reader;
  if (strcmp(r, "damage") == 0) {
    qa_q2_kex_damage_indicator *list = NULL; size_t n = 0;
    if (!qa_q2_kex_read_damage(k, &list, &n)) return;
    for (size_t i = 0; i < n; i++) {
      if (i) add(d, " ");
      num(d, list[i].damage); add(d, "/%d%d%d/", list[i].health, list[i].armor, list[i].shield); nums_f(d, list[i].direction, 3);
    }
    free(list);
  } else if (strcmp(r, "poi") == 0) {
    qa_q2_kex_poi p; qa_q2_kex_read_poi(k, &p);
    num(d, p.key); add(d, " "); num(d, p.time); add(d, " "); nums_f(d, p.pos, 3); add(d, " "); num(d, p.image); add(d, " ");
    num(d, p.color); add(d, " "); num(d, p.flags);
  } else if (strcmp(r, "help_path") == 0) {
    qa_q2_kex_help_path p; qa_q2_kex_read_help_path(k, &p);
    add(d, "%d ", p.start ? 1 : 0); nums_f(d, p.pos, 3); add(d, " "); nums_f(d, p.dir, 3);
  } else if (strcmp(r, "muzzleflash3") == 0) {
    qa_q2_kex_muzzleflash3 p; qa_q2_kex_read_muzzleflash3(k, &p);
    num(d, p.entity); add(d, " "); num(d, p.weapon);
  } else if (strcmp(r, "achievement") == 0) {
    char s[2048]; qa_q2_kex_read_achievement(k, s, sizeof s); add(d, "%s", s);
  } else if (strcmp(r, "locprint") == 0) {
    static qa_q2_kex_locprint p; qa_q2_kex_read_locprint(k, &p);
    num(d, p.flags); add(d, " %s [", p.base);
    for (size_t i = 0; i < p.arg_count; i++) add(d, i ? "|%s" : "%s", p.args[i]);
    add(d, "]");
  } else if (strcmp(r, "splitclient") == 0) {
    num(d, qa_q2_kex_read_splitclient(k));
  } else if (strcmp(r, "sound") == 0) {
    qa_q2_kex_sound s; qa_q2_kex_read_sound(k, &s);
    const double v[] = {s.flags, s.index, s.volume, s.attenuation, s.timeofs, s.entity, s.channel};
    for (size_t i = 0; i < COUNT(v); i++) { num(d, v[i]); add(d, " "); }
    if (s.has_pos) nums_f(d, s.pos, 3); else add(d, "null");
  } else if (strcmp(r, "configblast") == 0) {
    qa_q2_kex_configstring_record *list = NULL; size_t n = 0;
    if (!qa_q2_kex_read_configblast(k, &list, &n)) return;
    for (size_t i = 0; i < n; i++) { if (i) add(d, "|"); num(d, list[i].index); add(d, "=%s", list[i].value); }
    qa_q2_kex_configblast_free(list, n);
  } else if (strcmp(r, "spawnbaselineblast") == 0) {
    qa_q2_kex_baseline_entry *list = NULL; size_t n = 0;
    if (!qa_q2_kex_read_spawnbaselineblast(k, &list, &n)) return;
    for (size_t i = 0; i < n; i++) { if (i) add(d, " | "); add(d, "#"); num(d, list[i].entnum); add(d, " "); ent(d, &list[i].state); }
    free(list);
  } else if (strcmp(r, "delta_entity") == 0) {
    for (int i = 0; i < j->iterations; i++) {
      qa_q2_entity_bits eb; k->codec.read_entity_bits(&k->codec, &eb);
      qa_q2_wire_entity from = mk_kex_ent(1), to; qa_q2_wire_entity_init(&to);
      k->codec.read_delta_entity(&k->codec, &from, &to, eb.number, eb.bits_u64);
      if (i) add(d, " | ");
      add(d, "bits="); num(d, eb.bits); add(d, " "); ent(d, &to);
    }
  } else if (strcmp(r, "server_data") == 0) {
    static qa_q2_server_data_result rs; k->codec.read_server_data(&k->codec, &rs);
    add(d, "sc="); num(d, rs.servercount); add(d, " al=%d fps=", rs.attractloop ? 1 : 0); num(d, rs.server_fps);
    add(d, " gd=%s cn=", rs.gamedir); num(d, rs.clientnum); add(d, " cns="); nums_d(d, rs.clientnums, rs.clientnum_count);
    add(d, " ln=%s split=", rs.levelname); num(d, qa_q2_kex_split_player_count(k));
  } else if (strcmp(r, "frame_playerstate") == 0) {
    uint8_t a[2] = {0, 0}; qa_q2_frame_header h;
    k->codec.read_frame_header(&k->codec, a, 2, true, &h);
    qa_q2_wire_player from = mk_ply(1), to = mk_ply(0);
    from.pmove.viewheight = 3; from.gunrate = 4; from.team_id = 2;
    k->codec.read_frame_playerstate(&k->codec, &from, &to);
    add(d, "sf="); num(d, h.serverframe); add(d, " df="); num(d, h.deltaframe); add(d, " sc="); num(d, h.surpress_count);
    add(d, " ab="); num(d, h.areabytes); add(d, " a=%d,%d ", a[0], a[1]); ply(d, &to);
  } else if (strcmp(r, "packet_entities_begin") == 0) {
    k->codec.read_packet_entities_begin(&k->codec); add(d, "ok");
  }
}
static qa_q2_kex_context kex_ctx;
static void kex_decode(double protocol, const char *reader_name, const uint8_t *bytes, size_t len, text *d, int iterations) {
  qa_q2_sizebuf net; qa_q2_sizebuf_init_empty(&net);
  qa_q2_kex_context_init(&kex_ctx, &net);
  qa_q2_kex_set_protocol(&kex_ctx, protocol);
  uint8_t *copy = (uint8_t *)malloc(len > 0 ? len : 1);
  if (len) memcpy(copy, bytes, len);
  qa_q2_message_load(&net, copy, len);
  kex_job j = {&kex_ctx, reader_name, d, iterations};
  qa_caught_error c;
  if (!qa_try(kex_run, &j, &c)) { reset(d); caught_text(d, &c); }
  add(d, " rc=%lld", (long long)net.readcount);
  free(copy);
  qa_q2_kex_context_destroy(&kex_ctx); /* the edict-solid Map's out-of-table keys */
}
static void test_kex(void) {
  text d = {0};
  char label[96];
  for (size_t i = 0; i < COUNT(kex_cases); i++) {
    size_t len; uint8_t *b = unhex(kex_cases[i].hex, &len);
    int iterations = strstr(kex_cases[i].decoded, " | ") != NULL ? 2 : 1;
    reset(&d);
    kex_decode(kex_cases[i].protocol, kex_cases[i].reader, b, len, &d, iterations);
    snprintf(label, sizeof label, "kex %s %zu", kex_cases[i].reader, i);
    expect(label, str(&d), kex_cases[i].decoded);
    free(b);
  }
  free(d.s);
}

/* ---- server demo ---- */
typedef struct demo_job {
  int kind;
  const uint8_t *bytes;
  size_t length;
  qa_q2_server_demo_record *records;
  size_t count;
  bool ok;
  const qa_q2_server_demo_state *state;
  double frame;
  const qa_q2_wire_entity *entities;
  size_t entity_count;
  const qa_q2_byte_span *spans;
  size_t span_count;
  uint8_t *out;
  size_t out_length;
  qa_q2_demo_header header;
} demo_job;
static void demo_run(void *arg) {
  demo_job *j = (demo_job *)arg;
  if (j->kind == 0) j->ok = qa_q2_read_server_demo(j->bytes, j->length, &j->records, &j->count);
  else if (j->kind == 1) j->out = qa_q2_encode_server_demo_signon(j->state, &j->out_length);
  else if (j->kind == 2) j->out = qa_q2_encode_server_demo_frame(j->frame, j->entities, j->entity_count, j->spans, j->span_count, &j->out_length);
  else j->ok = qa_q2_read_demo_header(j->bytes, j->length, &j->header);
}
static void demo_dump(text *d, const qa_q2_server_demo_record *records, size_t count) {
  for (size_t i = 0; i < count; i++) {
    const qa_q2_server_demo_record *r = &records[i];
    if (i) add(d, " || ");
    if (r->kind == QA_Q2_SERVER_DEMO_SIGNON) {
      add(d, "signon sc="); num(d, r->state.servercount); add(d, " gd=%s cs=", r->state.gamedir);
      for (size_t c = 0; c < r->state.config_count; c++) {
        if (c) add(d, ",");
        num(d, r->state.config_strings[c].index); add(d, ":%s", r->state.config_strings[c].value);
      }
    } else {
      add(d, "frame sf="); num(d, r->server_frame); add(d, " ents=[");
      for (size_t e = 0; e < r->entity_count; e++) { if (e) add(d, " / "); ent(d, &r->entities[e]); }
      add(d, "] mc=%zu", r->multicast_count);
      for (size_t m = 0; m < r->multicast_count; m++) {
        const qa_q2_server_event *ev = &r->multicasts[m].event;
        if (ev->kind == QA_Q2_SERVER_PRINT) { add(d, " print:"); num(d, ev->level); add(d, ":%s", ev->text ? ev->text : ""); }
        else add(d, " other:%d", (int)ev->kind);
      }
    }
  }
}
static bool demo_read(const uint8_t *bytes, size_t length, text *d, qa_q2_server_demo_record **keep, size_t *keep_count) {
  demo_job j = {.kind = 0, .bytes = bytes, .length = length};
  qa_caught_error c;
  if (!qa_try(demo_run, &j, &c)) { caught_text(d, &c); return false; }
  if (!j.ok) { add(d, "OOM"); return false; }
  demo_dump(d, j.records, j.count);
  if (keep) { *keep = j.records; *keep_count = j.count; }
  else qa_q2_server_demo_records_free(j.records, j.count);
  return true;
}
static void test_server_demo(void) {
  text t = {0};
  char label[96];
  qa_q2_server_demo_config configs[3] = {{33, "maps/base1.bsp"}, {0, "fixture"}, {5, ""}};
  qa_q2_server_demo_state state = {7, (char *)"baseq2", configs, NULL, 3};
  qa_q2_wire_entity e2, e3, e4, e5;
  qa_q2_wire_entity_init(&e2); e2.number = 2; e2.modelindex = 1; e2.origin[0] = 42; e2.old_origin[0] = 40;
  qa_q2_wire_entity_init(&e3); e3.number = 3;
  qa_q2_wire_entity_init(&e4); e4.number = 300; e4.effects = 8; e4.sound = 3; set3(e4.origin, -1.5, 2.25, 1000); set3(e4.angles, 0, 90, 180);
  qa_q2_wire_entity_init(&e5); e5.number = 301; e5.modelindex = NAN;
  qa_q2_wire_entity frame_ents[4] = {e2, e3, e4, e5}, order[2] = {e4, e2}, big;
  qa_q2_wire_entity_init(&big); big.number = 1024;
  const uint8_t print_bytes[5] = {10, 2, 111, 107, 0};
  qa_q2_byte_span span = {print_bytes, 5};
  qa_q2_server_demo_config bad_index[1] = {{2080, "x"}}, frac_index[1] = {{1.5, "x"}};
  qa_q2_server_demo_state bad_state = {1, (char *)"g", bad_index, NULL, 1}, frac_state = {1, (char *)"g", frac_index, NULL, 1};
  demo_job jobs[] = {
    {.kind = 1, .state = &state},
    {.kind = 2, .frame = 12, .entities = frame_ents, .entity_count = 4, .spans = &span, .span_count = 1},
    {.kind = 2, .frame = 0x7fffffff},
    {.kind = 1, .state = &bad_state},
    {.kind = 1, .state = &frac_state},
    {.kind = 2, .frame = -1},
    {.kind = 2, .frame = 1, .entities = order, .entity_count = 2},
    {.kind = 2, .frame = 1, .entities = &big, .entity_count = 1},
  };
  CHECK(COUNT(jobs) == COUNT(server_demo_encodes));
  uint8_t *signon = NULL, *frame = NULL; size_t signon_len = 0, frame_len = 0;
  for (size_t i = 0; i < COUNT(jobs) && i < COUNT(server_demo_encodes); i++) {
    qa_caught_error c; reset(&t);
    if (qa_try(demo_run, &jobs[i], &c)) {
      for (size_t b = 0; b < jobs[i].out_length; b++) add(&t, "%02x", jobs[i].out[b]);
    } else caught_text(&t, &c);
    snprintf(label, sizeof label, "server demo encode %s", server_demo_encodes[i].label);
    expect(label, str(&t), server_demo_encodes[i].hex);
    if (i == 0) { signon = jobs[i].out; signon_len = jobs[i].out_length; }
    else if (i == 1) { frame = jobs[i].out; frame_len = jobs[i].out_length; }
    else free(jobs[i].out);
  }
  /* server-demo.test.ts: classic server footage preserves native entity-only frame and distinct signon. */
  if (signon && frame && signon_len >= 10 && frame_len >= 6) {
    const uint8_t want_start[10] = {12, 34, 0, 0, 0, 7, 0, 0, 0, 2}, want_update[6] = {20, 12, 0, 0, 0, 18};
    CHECK(memcmp(signon, want_start, 10) == 0);
    CHECK(memcmp(frame, want_update, 6) == 0);
    size_t a_len, b_len;
    uint8_t *a = qa_q2_write_demo_record(signon, signon_len, &a_len), *b = qa_q2_write_demo_record(frame, frame_len, &b_len);
    uint8_t *bytes = (uint8_t *)malloc(a_len + b_len);
    memcpy(bytes, a, a_len); memcpy(bytes + a_len, b, b_len);
    text d = {0}; reset(&d);
    qa_q2_server_demo_record *records = NULL; size_t count = 0;
    CHECK(demo_read(bytes, a_len + b_len, &d, &records, &count));
    CHECK(count == 2);
    if (count == 2) {
      CHECK(records[0].kind == QA_Q2_SERVER_DEMO_SIGNON && records[1].kind == QA_Q2_SERVER_DEMO_FRAME);
      bool found = false;
      for (size_t c = 0; c < records[0].state.config_count; c++)
        if (records[0].state.config_strings[c].index == 33) found = strcmp(records[0].state.config_strings[c].value, "maps/base1.bsp") == 0;
      CHECK(found);
      CHECK(records[1].server_frame == 12);
      CHECK(records[1].entity_count >= 1 && records[1].entities[0].origin[0] == 42 && records[1].entities[0].old_origin[0] == 40);
      CHECK(records[1].multicast_count >= 1 && records[1].multicasts[0].event.kind == QA_Q2_SERVER_PRINT &&
            records[1].multicasts[0].event.level == 2 && strcmp(records[1].multicasts[0].event.text, "ok") == 0);
    }
    qa_q2_server_demo_records_free(records, count);
    demo_job hj = {.kind = 3, .bytes = bytes, .length = a_len + b_len};
    qa_caught_error c;
    CHECK(!qa_try(demo_run, &hj, &c) && strstr(c.message, "Native serverrecord footage") != NULL);
    reset(&d); CHECK(!demo_read(bytes, a_len + b_len - 1, &d, NULL, NULL));
    reset(&d); CHECK(!demo_read(b, b_len, &d, NULL, NULL));
    free(d.s); free(bytes); free(a); free(b);
  }
  free(signon); free(frame);
  for (size_t i = 0; i < COUNT(server_demo_reads); i++) {
    size_t len; uint8_t *b = unhex(server_demo_reads[i].hex, &len);
    reset(&t); demo_read(b, len, &t, NULL, NULL);
    snprintf(label, sizeof label, "server demo read %zu", i);
    expect(label, str(&t), server_demo_reads[i].decoded);
    free(b);
  }
  free(t.s);
}

/* ---- malformed input: truncate and mutate valid bytes, decode under qa_try ---- */
static uint32_t rng = 0x2545f491u;
static uint32_t next_rand(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static uint8_t *mutate(const uint8_t *src, size_t len, size_t *out_len) {
  size_t n = len;
  uint32_t mode = next_rand() % 3;
  if (mode != 1 && len > 0) n = next_rand() % (len + 1);
  uint8_t *b = (uint8_t *)malloc(n > 0 ? n : 1);
  if (n) memcpy(b, src, n);
  if (mode != 0 && n > 0) {
    uint32_t flips = 1 + next_rand() % 4;
    for (uint32_t f = 0; f < flips; f++) b[next_rand() % n] = (uint8_t)next_rand();
  }
  *out_len = n;
  return b;
}
#define ROUNDS 160
typedef struct fuzz_job { int kind; qa_q2_sizebuf *m; qa_q2pro_context *ctx; double dups; } fuzz_job;
static void fuzz_fields(void *arg) {
  fuzz_job *j = (fuzz_job *)arg;
  qa_q2pro_features features = {1025, 16};
  if (j->kind == 0) {
    qa_q2_entity_bits eb; qa_q2pro_read_entity_bits(j->m, &eb);
    qa_q2_wire_entity from = mk_ent(2), to; qa_q2_wire_entity_init(&to);
    qa_q2pro_read_entity(j->m, features, &from, &to, eb.number, eb.bits_u64);
  } else if (j->kind == 1) {
    qa_q2_pro_player_fog from = fog_a(), out; qa_q2pro_read_fog(j->m, &from, &out);
    (void)qa_q2pro_read_var64(j->m);
  } else if (j->kind == 2) {
    qa_q2_protocol_codec *codec = &j->ctx->codec;
    (void)qa_q2_msg_read_byte(j->m);
    qa_q2_wire_player from = mk_ply(1), to = mk_ply(0);
    codec->read_player_state_delta(codec, j->m, &from, &to);
  } else if (j->kind == 3) {
    qa_q2_protocol_codec *codec = &j->ctx->codec;
    int32_t op = qa_q2_msg_read_byte(j->m);
    qa_q2pro_note_frame_opcode_extrabits(j->ctx, op & 0xe0);
    uint8_t a[1]; qa_q2_frame_header h;
    codec->read_frame_header(codec, a, 1, true, &h);
    qa_q2_wire_player from = mk_ply(2), to = mk_ply(0);
    codec->read_frame_playerstate(codec, &from, &to);
  } else if (j->kind == 4) {
    qa_q2_protocol_codec *codec = &j->ctx->codec;
    static qa_q2_server_data_result rs; codec->read_server_data(codec, &rs);
  } else {
    qa_q2_protocol_codec *codec = &j->ctx->codec;
    qa_q2_clc_batch_move mv;
    if (codec->read_batch_move(codec, j->m, false, j->dups, &mv)) qa_q2_clc_batch_move_free(&mv);
  }
}
static void fuzz_one(int kind, const char *hex, qa_q2pro_context *ctx, double dups, int *raised) {
  size_t len; uint8_t *src = unhex(hex, &len);
  for (int round = 0; round < ROUNDS; round++) {
    size_t n; uint8_t *b = mutate(src, len, &n);
    qa_q2_sizebuf *m = ctx ? ctx->net_message : NULL, local;
    if (!m) m = &local;
    reader(m, b, n);
    fuzz_job j = {kind, m, ctx, dups};
    qa_caught_error c;
    if (!qa_try(fuzz_fields, &j, &c)) (*raised)++;
    if (ctx) qa_q2_sizebuf_init_empty(ctx->net_message);
    free(b);
  }
  free(src);
}
static void test_malformed(void) {
  int raised = 0, runs = 0;
  for (size_t i = 0; i < COUNT(q2pro_entities); i++) {
    if (strncmp(q2pro_entities[i].hex, "ERR", 3) == 0) continue;
    fuzz_one(0, q2pro_entities[i].hex, NULL, 0, &raised); runs++;
  }
  for (size_t i = 0; i < COUNT(q2pro_fogs); i++) {
    if (strncmp(q2pro_fogs[i].hex, "ERR", 3) == 0) continue;
    fuzz_one(1, q2pro_fogs[i].hex, NULL, 0, &raised); runs++;
  }
  qa_q2_sizebuf net; qa_q2_sizebuf_init_empty(&net);
  qa_q2pro_context ctx; qa_q2pro_context_init(&ctx, &net);
  for (size_t i = 0; i < COUNT(q2pro_codecs); i++) {
    ctx.features.revision = q2pro_codecs[i].revision; ctx.features.flags = q2pro_codecs[i].flags;
    for (size_t r = 0; r < COUNT(q2pro_codecs[i].rows); r++) {
      const q2pro_player_row *row = &q2pro_codecs[i].rows[r];
      if (strncmp(row->delta, "ERR", 3) != 0) { fuzz_one(2, row->delta, &ctx, 0, &raised); runs++; }
      if (strncmp(row->frame, "ERR", 3) != 0) { fuzz_one(3, row->frame, &ctx, 0, &raised); runs++; }
    }
    fuzz_one(4, q2pro_codecs[i].server_data + 10, &ctx, 0, &raised); runs++;
  }
  fuzz_one(5, q2pro_batches[0].hex, &ctx, 1, &raised); runs++;
  fuzz_one(5, q2pro_batches[0].hex, &ctx, 28, &raised); runs++;
  /* KEX readers: every corpus case, mutated. */
  text d = {0};
  for (size_t i = 0; i < COUNT(kex_cases); i++) {
    size_t len; uint8_t *src = unhex(kex_cases[i].hex, &len);
    for (int round = 0; round < ROUNDS; round++) {
      size_t n; uint8_t *b = mutate(src, len, &n);
      reset(&d);
      kex_decode(kex_cases[i].protocol, kex_cases[i].reader, b, n, &d, 2);
      if (strncmp(d.s, "ERR", 3) == 0) raised++;
      free(b);
    }
    free(src); runs++;
  }
  free(d.s);
  /* Server demos. */
  for (size_t i = 0; i < COUNT(server_demo_reads); i++) {
    size_t len; uint8_t *src = unhex(server_demo_reads[i].hex, &len);
    for (int round = 0; round < ROUNDS; round++) {
      size_t n; uint8_t *b = mutate(src, len, &n);
      text t = {0}; reset(&t);
      if (!demo_read(b, n, &t, NULL, NULL)) raised++;
      free(t.s); free(b);
    }
    free(src); runs++;
  }
  CHECK(runs > 0 && raised > 0);
  printf("malformed: %d inputs x %d rounds, %d raised\n", runs, ROUNDS, raised);
}

int main(void) {
  test_fields();
  test_q2pro_codec();
  test_kex();
  test_server_demo();
  test_malformed();
  printf("test_q2_codec_readers: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
