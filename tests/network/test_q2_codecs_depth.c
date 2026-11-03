/* Depth checks for the Q2 wire codecs (message, vanilla, r1q2, q2repro, kex-write,
 * kex-usercmd, mvd), encodeQ2ServerEvent and the contract adapters against bytes and
 * decoded values captured from the TypeScript sources (q2_codecs_depth_corpus.h), plus
 * a fixed-seed truncate/mutate loop over every decoder family under qa_try.
 * GPL-2.0-or-later. */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/network/q2/adapters.h"
#include "qa/network/q2/codec.h"
#include "qa/network/q2/codecs/kex_usercmd.h"
#include "qa/network/q2/codecs/kex_write.h"
#include "qa/network/q2/codecs/mvd.h"
#include "qa/network/q2/codecs/q2repro.h"
#include "qa/network/q2/codecs/r1q2.h"
#include "qa/network/q2/codecs/vanilla.h"
#include "qa/network/q2/message.h"
#include "qa/network/q2/mvd_profile.h"
#include "qa/network/q2/server_messages.h"
#include "qa/network/q2/server_write.h"
#include "qa/network/q2/state.h"
#include "q2_codecs_depth_corpus.h"

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
  fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define COUNT(a) (sizeof (a) / sizeof *(a))

static bool same_number(double a, double b) { return a == b || (isnan(a) && isnan(b)); }

/* ---------- canonical forms (order shared with the dump script) ---------- */
static void ent_from(const double *a, qa_q2_wire_entity *e) {
  qa_q2_wire_entity_init(e);
  int i = 0;
  e->number = a[i++];
  for (int k = 0; k < 3; k++) e->origin[k] = (float)a[i++];
  for (int k = 0; k < 3; k++) e->angles[k] = (float)a[i++];
  for (int k = 0; k < 3; k++) e->old_origin[k] = (float)a[i++];
  e->modelindex = a[i++]; e->modelindex2 = a[i++]; e->modelindex3 = a[i++]; e->modelindex4 = a[i++];
  e->frame = a[i++]; e->skinnum = a[i++]; e->effects = a[i++]; e->renderfx = a[i++]; e->solid = a[i++];
  e->sound = a[i++]; e->event = a[i++]; e->alpha = a[i++]; e->scale = a[i++]; e->instance_bits = a[i++];
  e->loop_volume = a[i++]; e->loop_attenuation = a[i++]; e->owner = a[i++]; e->old_frame = a[i++]; e->morefx = a[i++];
}
static int ent_canon(const qa_q2_wire_entity *e, double *o) {
  int i = 0;
  o[i++] = e->number;
  for (int k = 0; k < 3; k++) o[i++] = e->origin[k];
  for (int k = 0; k < 3; k++) o[i++] = e->angles[k];
  for (int k = 0; k < 3; k++) o[i++] = e->old_origin[k];
  o[i++] = e->modelindex; o[i++] = e->modelindex2; o[i++] = e->modelindex3; o[i++] = e->modelindex4;
  o[i++] = e->frame; o[i++] = e->skinnum; o[i++] = e->effects; o[i++] = e->renderfx; o[i++] = e->solid;
  o[i++] = e->sound; o[i++] = e->event; o[i++] = e->alpha; o[i++] = e->scale; o[i++] = e->instance_bits;
  o[i++] = e->loop_volume; o[i++] = e->loop_attenuation; o[i++] = e->owner; o[i++] = e->old_frame; o[i++] = e->morefx;
  return i;
}
static void player_from(const double *a, qa_q2_wire_player *p) {
  qa_q2_wire_player_init(p);
  qa_q2_wire_pmove *m = &p->pmove;
  int i = 0;
  m->pm_type = a[i++];
  for (int k = 0; k < 3; k++) m->origin[k] = qa_js_to_int32(a[i++]);
  for (int k = 0; k < 3; k++) m->velocity[k] = qa_js_to_int32(a[i++]);
  m->pm_flags = a[i++]; m->pm_time = a[i++]; m->gravity = a[i++];
  for (int k = 0; k < 3; k++) m->delta_angles[k] = qa_js_to_int16(a[i++]);
  m->viewheight = a[i++];
  for (int k = 0; k < 3; k++) m->origin_f[k] = (float)a[i++];
  for (int k = 0; k < 3; k++) m->velocity_f[k] = (float)a[i++];
  for (int k = 0; k < 3; k++) m->delta_angles_f[k] = (float)a[i++];
  m->delta_angle_float = a[i++] != 0;
  float *vecs[5] = {p->viewangles, p->viewoffset, p->kick_angles, p->gunangles, p->gunoffset};
  for (int v = 0; v < 5; v++) for (int k = 0; k < 3; k++) vecs[v][k] = (float)a[i++];
  p->gunindex = a[i++]; p->gunskin = a[i++]; p->gunframe = a[i++]; p->gunrate = a[i++];
  for (int k = 0; k < 4; k++) p->blend[k] = (float)a[i++];
  for (int k = 0; k < 4; k++) p->damage_blend[k] = (float)a[i++];
  p->fov = a[i++]; p->rdflags = a[i++]; p->team_id = a[i++]; p->clientnum = a[i++];
  qa_q2_pro_player_fog *f = &p->q2pro_fog;
  for (int k = 0; k < 3; k++) f->color[k] = a[i++];
  f->density = a[i++]; f->sky_factor = a[i++]; f->height_density = a[i++]; f->height_falloff = a[i++];
  for (int k = 0; k < 3; k++) f->height_start_color[k] = a[i++];
  for (int k = 0; k < 3; k++) f->height_end_color[k] = a[i++];
  f->height_start_distance = a[i++]; f->height_end_distance = a[i++];
  for (int k = 0; k < 64; k++) p->stats[k] = qa_js_to_int16(a[i++]);
}
static int player_canon(const qa_q2_wire_player *p, double *o) {
  const qa_q2_wire_pmove *m = &p->pmove;
  int i = 0;
  o[i++] = m->pm_type;
  for (int k = 0; k < 3; k++) o[i++] = m->origin[k];
  for (int k = 0; k < 3; k++) o[i++] = m->velocity[k];
  o[i++] = m->pm_flags; o[i++] = m->pm_time; o[i++] = m->gravity;
  for (int k = 0; k < 3; k++) o[i++] = m->delta_angles[k];
  o[i++] = m->viewheight;
  for (int k = 0; k < 3; k++) o[i++] = m->origin_f[k];
  for (int k = 0; k < 3; k++) o[i++] = m->velocity_f[k];
  for (int k = 0; k < 3; k++) o[i++] = m->delta_angles_f[k];
  o[i++] = m->delta_angle_float ? 1 : 0;
  const float *vecs[5] = {p->viewangles, p->viewoffset, p->kick_angles, p->gunangles, p->gunoffset};
  for (int v = 0; v < 5; v++) for (int k = 0; k < 3; k++) o[i++] = vecs[v][k];
  o[i++] = p->gunindex; o[i++] = p->gunskin; o[i++] = p->gunframe; o[i++] = p->gunrate;
  for (int k = 0; k < 4; k++) o[i++] = p->blend[k];
  for (int k = 0; k < 4; k++) o[i++] = p->damage_blend[k];
  o[i++] = p->fov; o[i++] = p->rdflags; o[i++] = p->team_id; o[i++] = p->clientnum;
  const qa_q2_pro_player_fog *f = &p->q2pro_fog;
  for (int k = 0; k < 3; k++) o[i++] = f->color[k];
  o[i++] = f->density; o[i++] = f->sky_factor; o[i++] = f->height_density; o[i++] = f->height_falloff;
  for (int k = 0; k < 3; k++) o[i++] = f->height_start_color[k];
  for (int k = 0; k < 3; k++) o[i++] = f->height_end_color[k];
  o[i++] = f->height_start_distance; o[i++] = f->height_end_distance;
  for (int k = 0; k < 64; k++) o[i++] = p->stats[k];
  return i;
}
static void cmd_from(const double *a, qa_q2_wire_usercmd *c) {
  qa_q2_wire_usercmd_init(c);
  c->server_frame = a[0]; c->msec = a[1]; c->buttons = a[2];
  for (int k = 0; k < 3; k++) c->angles[k] = qa_js_to_int16(a[3 + k]);
  c->forwardmove = a[6]; c->sidemove = a[7]; c->upmove = a[8]; c->impulse = a[9]; c->lightlevel = a[10];
}
static int cmd_canon(const qa_q2_wire_usercmd *c, double *o) {
  int i = 0;
  o[i++] = c->server_frame; o[i++] = c->msec; o[i++] = c->buttons;
  for (int k = 0; k < 3; k++) o[i++] = c->angles[k];
  o[i++] = c->forwardmove; o[i++] = c->sidemove; o[i++] = c->upmove; o[i++] = c->impulse; o[i++] = c->lightlevel;
  return i;
}

static size_t from_hex(const char *hex, uint8_t *out, size_t cap) {
  size_t n = strlen(hex) / 2;
  for (size_t i = 0; i < n && i < cap; i++) {
    unsigned v;
    sscanf(hex + 2 * i, "%2x", &v);
    out[i] = (uint8_t)v;
  }
  return n < cap ? n : cap;
}
static void to_hex(const uint8_t *b, size_t n, char *out) {
  for (size_t i = 0; i < n; i++) snprintf(out + 2 * i, 3, "%02x", b[i]);
  out[2 * n] = '\0';
}

/* ---------- writer contexts (TS createVanillaContext/createR1Context/...) ---------- */
typedef struct writers {
  qa_q2_sizebuf dummy;
  qa_q2_vanilla_context vanilla;
  qa_q2_r1_context r1[3]; /* minor 1903, 1904, 1905 */
  qa_q2repro_context rr;
  qa_q2repro_context kex_helpers;
  qa_q2_protocol_codec kex;
  qa_q2_kex_writer_binding kex_binding;
  double kex_proto;
} writers;
static writers W;
static void writers_init(void) {
  memset(&W, 0, sizeof W);
  qa_q2_sizebuf_init_empty(&W.dummy);
  qa_q2_vanilla_context_init(&W.vanilla, &W.dummy);
  const double minors[3] = {1903, 1904, 1905};
  for (int i = 0; i < 3; i++) {
    qa_q2_r1_context_init(&W.r1[i], &W.dummy);
    qa_q2_r1_create_codec(&W.r1[i], minors[i]);
  }
  qa_q2repro_context_init(&W.rr, &W.dummy);
  W.kex_proto = 2022;
  W.kex_binding.protocol_value = &W.kex_proto;
  memset(&W.kex, 0, sizeof W.kex);
  qa_q2_kex_fill_writers(&W.kex, &W.kex_binding);
}

/* ---------- encoder jobs ---------- */
typedef enum job_op {
  J_ENT, J_BASELINE, J_REMOVE, J_END, J_BEGIN, J_PS, J_MVD_NULL_FROM, J_MVD_NULL_TO, J_MVD_BAD,
  J_MVDRR_NULL_FROM, J_MVDRR_NULL_TO, J_MVDRR_BAD, J_MVD_CMDS, J_CMD, J_BATCH, J_FRAME, J_SD, J_SPECIAL
} job_op;
typedef struct job {
  job_op op;
  qa_q2_sizebuf *m;
  const char *name;
  int a, b, index;
  bool force, newent, has_last;
} job;

static qa_q2_protocol_codec *codec_named(const char *name) {
  if (strcmp(name, "vanilla") == 0) return &W.vanilla.codec;
  if (strcmp(name, "r1q2") == 0 || strcmp(name, "r1q2-1905") == 0) return &W.r1[2].codec;
  if (strcmp(name, "r1q2-1903") == 0) return &W.r1[0].codec;
  if (strcmp(name, "q2repro") == 0) return &W.rr.codec;
  if (strcmp(name, "q2repro-classic") == 0) return &W.rr.classic_codec;
  if (strcmp(name, "kex") == 0 || strncmp(name, "kex-", 4) == 0) return &W.kex;
  return NULL;
}

static const char *const SD_GAMEDIR[3] = {"baseq2", "", "mod"};
static const char *const SD_LEVEL[3] = {"The Edge", "q2dm1", "x"};
static const double SD_CLIENTNUMS[9] = {0, 1, 2, 3, 4, 5, 6, 7, 8};
static void sd_params(int i, qa_q2_server_data_params *p) {
  memset(p, 0, sizeof *p);
  p->gamedir = SD_GAMEDIR[i];
  p->levelname = SD_LEVEL[i];
  if (i == 0) { p->servercount = 77; p->attractloop = true; p->clientnum = 3; p->server_state = 2; }
  if (i == 1) {
    p->servercount = -5; p->clientnum = -1; p->server_state = 0x1ff;
    p->has_server_fps = true; p->server_fps = 40; p->has_wire_flags = true; p->wire_flags = 9;
    p->has_protocol_revision = true; p->protocol_revision = 2048;
    p->has_r1q2_version = true; p->r1q2_version = 1903;
    p->has_r1q2_strafejump_hack = true; p->r1q2_strafejump_hack = true;
    p->has_clientnums = true; p->clientnums = SD_CLIENTNUMS; p->clientnum_count = 2;
  }
  if (i == 2) { p->servercount = 1; p->has_clientnums = true; p->clientnums = SD_CLIENTNUMS; p->clientnum_count = 9; }
}

static void write_end_short(void *ctx, qa_q2_sizebuf *msg) {
  CHECK(ctx == &W);
  qa_q2_msg_write_short(msg, 0);
}

static void run_special(job *j) {
  qa_q2_sizebuf *m = j->m;
  const char *n = j->name;
  qa_q2_wire_entity e, f;
  qa_q2_wire_player pa, pb;
  if (strcmp(n, "msg prims") == 0) {
    qa_q2_msg_write_char(m, -1); qa_q2_msg_write_byte(m, 263); qa_q2_msg_write_byte(m, NAN);
    qa_q2_msg_write_short(m, -2); qa_q2_msg_write_short(m, 70000.9);
    qa_q2_msg_write_long(m, (double)0x89abcdefu); qa_q2_msg_write_long(m, -1e10);
    qa_q2_msg_write_float(m, 1.1); qa_q2_msg_write_float(m, 1e40); qa_q2_msg_write_long64(m, -2);
    qa_q2_msg_write_string(m, "abc"); qa_q2_msg_write_string(m, NULL); qa_q2_msg_write_coord(m, -1.06);
    qa_q2_wire_vec3 pos = {1.5f, -2.25f, 4096.0f};
    qa_q2_msg_write_pos(m, pos);
    qa_q2_msg_write_angle(m, -45.5); qa_q2_msg_write_angle(m, 720.3);
    qa_q2_msg_write_angle16(m, 359.99); qa_q2_msg_write_angle16(m, -0.01);
    qa_q2_wire_vec3 up = {0, 0, 1}, slant = {0.6f, -0.8f, 0};
    qa_q2_msg_write_dir(m, up); qa_q2_msg_write_dir(m, slant); qa_q2_msg_write_dir(m, NULL);
    qa_q2_sz_print(m, "hi"); qa_q2_sz_print(m, "there");
  } else if (strcmp(n, "msg print fresh") == 0) {
    qa_q2_sz_print(m, "a"); qa_q2_sz_print(m, "b"); qa_q2_msg_write_byte(m, 9); qa_q2_sz_print(m, "c");
  } else if (strcmp(n, "msg overflow") == 0) {
    qa_q2_msg_write_long(m, 1); qa_q2_msg_write_long(m, 2);
  } else if (strcmp(n, "msg overflow allowed") == 0) {
    m->allow_overflow = true;
    qa_q2_msg_write_long(m, 1); qa_q2_msg_write_short(m, 0x0302); qa_q2_msg_write_long(m, 0x07060504);
  } else if (strcmp(n, "msg overflow too big") == 0) {
    uint8_t zeros[9] = {0};
    m->allow_overflow = true;
    qa_q2_sz_write(m, zeros, 9, 9);
  } else if (strcmp(n, "msg float view") == 0) {
    m->maxsize = 8; qa_q2_msg_write_long(m, 1); qa_q2_msg_write_float(m, 2);
  } else if (strcmp(n, "msg set range") == 0) {
    m->maxsize = 8; qa_q2_msg_write_long(m, 1); qa_q2_msg_write_string(m, "abcd");
  } else if (strcmp(n, "msg write short tail") == 0) {
    m->maxsize = 8; qa_q2_msg_write_long(m, 1); qa_q2_msg_write_short(m, 0x1234); qa_q2_msg_write_long(m, 0x55667788);
  } else if (strcmp(n, "msg entity unset") == 0) {
    qa_q2_wire_entity_init(&f); ent_from(cd_ents[0], &e);
    qa_q2_msg_write_delta_entity(&f, &e, m, true, false);
  } else if (strcmp(n, "msg entity nan") == 0) {
    qa_q2_wire_entity_init(&f); ent_from(cd_ents[1], &e); e.number = NAN;
    qa_q2_msg_write_delta_entity(&f, &e, m, true, false);
  } else if (strcmp(n, "msg entity big") == 0) {
    qa_q2_wire_entity_init(&f); ent_from(cd_ents[1], &e); e.number = 1024;
    qa_q2_msg_write_delta_entity(&f, &e, m, true, false);
  } else if (strcmp(n, "msg entity nan event") == 0) {
    ent_from(cd_ents[1], &f); ent_from(cd_ents[1], &e); e.event = NAN;
    qa_q2_msg_write_delta_entity(&f, &e, m, false, false);
  } else if (strcmp(n, "r1 entity nan event") == 0) {
    ent_from(cd_ents[1], &f); ent_from(cd_ents[1], &e); e.event = NAN;
    W.r1[2].codec.write_delta_entity(&W.r1[2].codec, m, &f, &e, false, false);
  } else if (strcmp(n, "rr entity nan event") == 0) {
    ent_from(cd_ents[1], &f); ent_from(cd_ents[1], &e); e.event = NAN;
    W.rr.codec.write_delta_entity(&W.rr.codec, m, &f, &e, false, false);
  } else if (strcmp(n, "rr ps nan viewoffset") == 0) {
    player_from(cd_players[1], &pa); player_from(cd_players[1], &pb);
    pa.viewoffset[1] = NAN; pb.viewoffset[1] = NAN; pa.kick_angles[0] = NAN; pb.kick_angles[0] = NAN;
    W.rr.codec.write_player_state_delta(&W.rr.codec, m, &pa, &pb);
  } else if (strcmp(n, "kex ps gunskin wrap") == 0) {
    player_from(cd_players[1], &pa); player_from(cd_players[1], &pb);
    pb.gunskin = 0x7ffff; pb.gunindex = 3;
    W.kex.write_player_state_delta(&W.kex, m, &pa, &pb);
  } else {
    fprintf(stderr, "unknown special %s\n", n);
    failures++;
  }
}

static void run_job(void *p) {
  job *j = (job *)p;
  qa_q2_sizebuf *m = j->m;
  qa_q2_wire_entity from_e, to_e;
  qa_q2_wire_player from_p, to_p;
  qa_q2_wire_usercmd from_c, to_c;
  qa_q2_protocol_codec *c = codec_named(j->name);
  switch (j->op) {
    case J_ENT:
      ent_from(cd_ents[j->a], &from_e); ent_from(cd_ents[j->b], &to_e);
      if (strcmp(j->name, "msg") == 0) { qa_q2_msg_write_delta_entity(&from_e, &to_e, m, j->force, j->newent); break; }
      if (strcmp(j->name, "kex-2022") == 0) W.kex_proto = 2022;
      if (strcmp(j->name, "kex-2023") == 0) W.kex_proto = 2023;
      c->write_delta_entity(c, m, &from_e, &to_e, j->force, j->newent);
      break;
    case J_BASELINE:
      W.kex_proto = 2023;
      ent_from(cd_ents[j->a], &to_e);
      c->write_spawn_baseline(c, m, &to_e);
      break;
    case J_REMOVE: c->write_entity_remove(c, m, j->a); break;
    case J_END: c->write_packet_entities_end(c, m); break;
    case J_BEGIN: c->write_packet_entities_begin(c, m); break;
    case J_PS:
      player_from(cd_players[j->a], &from_p); player_from(cd_players[j->b], &to_p);
      if (strcmp(j->name, "mvd") == 0) qa_q2_msg_write_delta_mvd_playerstate(m, &from_p, &to_p, 3, false);
      else if (strcmp(j->name, "mvd-force") == 0) qa_q2_msg_write_delta_mvd_playerstate(m, &from_p, &to_p, 3, true);
      else if (strcmp(j->name, "mvd-rr") == 0) qa_q2_msg_write_delta_mvd_playerstate_rerelease(m, &from_p, &to_p, 3, false);
      else if (strcmp(j->name, "mvd-rr-force") == 0) qa_q2_msg_write_delta_mvd_playerstate_rerelease(m, &from_p, &to_p, 3, true);
      else c->write_player_state_delta(c, m, &from_p, &to_p);
      break;
    case J_MVD_NULL_FROM: player_from(cd_players[1], &to_p); qa_q2_msg_write_delta_mvd_playerstate(m, NULL, &to_p, 4, false); break;
    case J_MVD_NULL_TO: qa_q2_msg_write_delta_mvd_playerstate(m, NULL, NULL, 4, false); break;
    case J_MVD_BAD: qa_q2_msg_write_delta_mvd_playerstate(m, NULL, NULL, 255, false); break;
    case J_MVDRR_NULL_FROM: player_from(cd_players[2], &to_p); qa_q2_msg_write_delta_mvd_playerstate_rerelease(m, NULL, &to_p, 4, false); break;
    case J_MVDRR_NULL_TO: qa_q2_msg_write_delta_mvd_playerstate_rerelease(m, NULL, NULL, 4, false); break;
    case J_MVDRR_BAD: qa_q2_msg_write_delta_mvd_playerstate_rerelease(m, NULL, NULL, -1.5, false); break;
    case J_MVD_CMDS:
      qa_q2_msg_write_mvd_cmd(m, 6, 3); qa_q2_msg_write_mvd_cmd(m, 31, 0x7ffffff); qa_q2_msg_write_mvd_cmd(m, -1, -1);
      qa_q2_msg_write_mvd_players_end(m);
      break;
    case J_CMD:
      cmd_from(cd_cmds[j->a], &from_c); cmd_from(cd_cmds[j->b], &to_c);
      if (strcmp(j->name, "msg") == 0) qa_q2_msg_write_delta_usercmd(m, &from_c, &to_c);
      else if (strcmp(j->name, "kex") == 0) qa_q2_write_kex_usercmd(m, &from_c, &to_c);
      else c->write_delta_usercmd(c, m, &from_c, &to_c);
      break;
    case J_BATCH: {
      qa_q2_wire_usercmd cmds[3][3];
      qa_q2_clc_batch_move_frame frames[3];
      size_t frame_count = 0;
      for (int fi = 0; fi < 3; fi++) {
        size_t n = 0;
        for (int k = 0; k < 3; k++) {
          int index = cd_batch_layouts[j->a][fi][k];
          if (index >= 0) cmd_from(cd_cmds[index], &cmds[fi][n++]);
        }
        if (n == 0) break;
        frames[fi].cmds = cmds[fi];
        frames[fi].cmd_count = n;
        frame_count++;
      }
      c->write_batch_move(c, m, j->has_last, 77, frames, frame_count);
      break;
    }
    case J_FRAME: {
      W.kex_proto = 2023;
      const double *fr = cd_frames[j->a];
      uint8_t area[3] = {1, 2, 3};
      qa_q2_frame_write_params params;
      memset(&params, 0, sizeof params);
      params.framenum = fr[0]; params.lastframe = fr[1]; params.surpress_count = fr[2];
      params.areabits = area; params.areabits_length = 3; params.areabytes = 3;
      if (fr[3] >= 0) { player_from(cd_players[(int)fr[3]], &from_p); params.ps_from = &from_p; }
      player_from(cd_players[(int)fr[4]], &to_p);
      params.ps_to = &to_p;
      c->write_frame(c, m, &params, write_end_short, &W);
      break;
    }
    case J_SD: {
      W.kex_proto = 2023;
      qa_q2_server_data_params params;
      sd_params(j->a, &params);
      c->write_server_data(c, m, &params);
      break;
    }
    case J_SPECIAL: run_special(j); break;
  }
}

static size_t enc_index = 0;
static void enc_check(job *j, const char *label, int64_t cap) {
  if (enc_index >= COUNT(cd_enc)) { failures++; fprintf(stderr, "FAIL enc overflow at %s\n", label); return; }
  checks++;
  if (strcmp(cd_enc[enc_index].label, label) != 0) {
    failures++;
    fprintf(stderr, "FAIL enc order %zu: got %s expected %s\n", enc_index, label, cd_enc[enc_index].label);
  }
  qa_q2_sizebuf m;
  CHECK(qa_q2_message_create(&m, cap));
  j->m = &m;
  qa_caught_error caught;
  memset(&caught, 0, sizeof caught);
  bool ok = qa_try(run_job, j, &caught);
  const char *want_hex = cd_enc[enc_index].hex, *want_error = cd_enc[enc_index].error;
  if (ok) {
    size_t n = 0;
    uint8_t *bytes = qa_q2_message_bytes(&m, &n);
    char *got = malloc(2 * n + 1);
    to_hex(bytes, n, got);
    if (want_hex == NULL || strcmp(got, want_hex) != 0) {
      failures++;
      fprintf(stderr, "FAIL enc %s\n got %s\n exp %s\n", label, got, want_hex ? want_hex : want_error);
    }
    free(got);
    free(bytes);
  } else {
    char text[4200];
    snprintf(text, sizeof text, "%s: %s", caught.name, caught.message);
    if (want_error == NULL || strcmp(text, want_error) != 0) {
      failures++;
      fprintf(stderr, "FAIL enc %s\n got error %s\n exp %s\n", label, text, want_error ? want_error : want_hex);
    }
  }
  qa_q2_message_destroy(&m);
  enc_index++;
}

static void run_encoders(void) {
  static const char *const ent_names[] = {"msg", "r1q2-1903", "r1q2-1905", "q2repro", "kex-2022", "kex-2023"};
  char label[256];
  job j;
  for (size_t w = 0; w < COUNT(ent_names); w++)
    for (size_t p = 0; p < COUNT(cd_epairs); p++) {
      memset(&j, 0, sizeof j);
      j.op = J_ENT; j.name = ent_names[w]; j.a = cd_epairs[p][0]; j.b = cd_epairs[p][1];
      j.force = cd_epairs[p][2] != 0; j.newent = cd_epairs[p][3] != 0;
      snprintf(label, sizeof label, "ent %s %d->%d", j.name, j.a, j.b);
      enc_check(&j, label, 32768);
    }
  static const char *const base_names[] = {"vanilla", "r1q2", "q2repro", "q2repro-classic", "kex"};
  for (size_t w = 0; w < COUNT(base_names); w++) {
    for (int k = 1; k <= 3; k++) {
      memset(&j, 0, sizeof j); j.op = J_BASELINE; j.name = base_names[w]; j.a = k;
      snprintf(label, sizeof label, "baseline %s %d", j.name, k);
      enc_check(&j, label, 32768);
    }
    const int removes[2] = {5, 300};
    for (int k = 0; k < 2; k++) {
      memset(&j, 0, sizeof j); j.op = J_REMOVE; j.name = base_names[w]; j.a = removes[k];
      snprintf(label, sizeof label, "remove %s %d", j.name, removes[k]);
      enc_check(&j, label, 32768);
    }
    memset(&j, 0, sizeof j); j.op = J_END; j.name = base_names[w];
    snprintf(label, sizeof label, "end %s", j.name);
    enc_check(&j, label, 32768);
    memset(&j, 0, sizeof j); j.op = J_BEGIN; j.name = base_names[w];
    snprintf(label, sizeof label, "begin %s", j.name);
    enc_check(&j, label, 32768);
  }
  static const char *const ps_names[] = {"vanilla", "r1q2", "q2repro", "kex", "mvd", "mvd-force", "mvd-rr", "mvd-rr-force"};
  for (size_t w = 0; w < COUNT(ps_names); w++)
    for (size_t p = 0; p < COUNT(cd_ppairs); p++) {
      memset(&j, 0, sizeof j); j.op = J_PS; j.name = ps_names[w]; j.a = cd_ppairs[p][0]; j.b = cd_ppairs[p][1];
      snprintf(label, sizeof label, "ps %s %d->%d", j.name, j.a, j.b);
      enc_check(&j, label, 32768);
    }
  const struct { job_op op; const char *label; } mvd_specials[] = {
    {J_MVD_NULL_FROM, "mvd null-from"}, {J_MVD_NULL_TO, "mvd null-to"}, {J_MVD_BAD, "mvd bad-number"},
    {J_MVDRR_NULL_FROM, "mvd-rr null-from"}, {J_MVDRR_NULL_TO, "mvd-rr null-to"}, {J_MVDRR_BAD, "mvd-rr bad-number"},
    {J_MVD_CMDS, "mvd cmds"}};
  for (size_t k = 0; k < COUNT(mvd_specials); k++) {
    memset(&j, 0, sizeof j); j.op = mvd_specials[k].op; j.name = "";
    enc_check(&j, mvd_specials[k].label, 32768);
  }
  static const char *const cmd_names[] = {"msg", "r1q2-1903", "r1q2-1905", "q2repro", "q2repro-classic", "kex"};
  for (size_t w = 0; w < COUNT(cmd_names); w++)
    for (size_t p = 0; p < COUNT(cd_cpairs); p++) {
      memset(&j, 0, sizeof j); j.op = J_CMD; j.name = cmd_names[w]; j.a = cd_cpairs[p][0]; j.b = cd_cpairs[p][1];
      snprintf(label, sizeof label, "cmd %s %d->%d", j.name, j.a, j.b);
      enc_check(&j, label, 32768);
    }
  static const char *const batch_names[] = {"q2repro", "q2repro-classic"};
  for (size_t w = 0; w < COUNT(batch_names); w++)
    for (int li = 0; li < (int)COUNT(cd_batch_layouts); li++)
      for (int last = 0; last < 2; last++) {
        memset(&j, 0, sizeof j); j.op = J_BATCH; j.name = batch_names[w]; j.a = li; j.has_last = last != 0;
        snprintf(label, sizeof label, "batch %s %d %s", j.name, li, last ? "77" : "null");
        enc_check(&j, label, 32768);
      }
  static const char *const frame_names[] = {"vanilla", "r1q2", "q2repro", "kex"};
  for (size_t w = 0; w < COUNT(frame_names); w++)
    for (int fi = 0; fi < (int)COUNT(cd_frames); fi++) {
      memset(&j, 0, sizeof j); j.op = J_FRAME; j.name = frame_names[w]; j.a = fi;
      snprintf(label, sizeof label, "frame %s %d", j.name, fi);
      enc_check(&j, label, 32768);
    }
  for (size_t w = 0; w < COUNT(base_names); w++)
    for (int i = 0; i < 3; i++) {
      memset(&j, 0, sizeof j); j.op = J_SD; j.name = base_names[w]; j.a = i;
      snprintf(label, sizeof label, "sd %s %d", j.name, i);
      enc_check(&j, label, 32768);
    }
  const struct { const char *label; int64_t cap; } specials[] = {
    {"msg prims", 32768}, {"msg print fresh", 32768}, {"msg overflow", 6}, {"msg overflow allowed", 6},
    {"msg overflow too big", 6}, {"msg float view", 6}, {"msg set range", 6}, {"msg write short tail", 6},
    {"msg entity unset", 32768}, {"msg entity nan", 32768}, {"msg entity big", 32768}, {"msg entity nan event", 32768},
    {"r1 entity nan event", 32768}, {"rr entity nan event", 32768}, {"rr ps nan viewoffset", 32768},
    {"kex ps gunskin wrap", 32768}};
  for (size_t k = 0; k < COUNT(specials); k++) {
    memset(&j, 0, sizeof j); j.op = J_SPECIAL; j.name = specials[k].label;
    enc_check(&j, specials[k].label, specials[k].cap);
  }
  CHECK(enc_index == COUNT(cd_enc));
}

/* ---------- encodeQ2ServerEvent ---------- */
typedef struct event_job {
  qa_q2_wire_codec *wire;
  qa_q2_server_event *event;
  uint8_t *bytes;
  size_t length;
} event_job;
static void run_event(void *p) {
  event_job *j = (event_job *)p;
  j->bytes = qa_q2_encode_server_event(j->wire, j->event, &j->length);
}
static void vec(qa_vec3 *v, double x, double y, double z) { v->x = x; v->y = y; v->z = z; }
static void set3(float *v, double x, double y, double z) { v[0] = (float)x; v[1] = (float)y; v[2] = (float)z; }
static void build_event(int ei, qa_q2_server_event *ev) {
  static double counts[5] = {1, 2, 3, -1, 70000};
  static uint8_t dl[3] = {1, 2, 3};
  static qa_q2_kex_damage_indicator damage[2];
  memset(ev, 0, sizeof *ev);
  switch (ei) {
    case 0: ev->kind = QA_Q2_SERVER_NOP; break;
    case 1: ev->kind = QA_Q2_SERVER_DISCONNECT; break;
    case 2: ev->kind = QA_Q2_SERVER_RECONNECT; break;
    case 3: ev->kind = QA_Q2_SERVER_LEVEL_RESTART; break;
    case 4: {
      ev->kind = QA_Q2_SERVER_DATA;
      qa_q2_server_data_result *d = &ev->data;
      d->servercount = -5; d->attractloop = false; strcpy(d->gamedir, ""); d->clientnum = -1;
      strcpy(d->levelname, "q2dm1"); d->server_state = 0x1ff;
      d->has_server_fps = true; d->server_fps = 40; d->has_wire_flags = true; d->wire_flags = 9;
      d->has_protocol_revision = true; d->protocol_revision = 2048; d->has_r1q2_version = true; d->r1q2_version = 1903;
      d->has_r1q2_strafejump_hack = true; d->r1q2_strafejump_hack = true;
      d->has_clientnums = true; d->clientnums[0] = 0; d->clientnums[1] = 1; d->clientnum_count = 2;
      break;
    }
    case 5: ev->kind = QA_Q2_SERVER_PRINT; ev->level = 2; ev->text = "hello\n"; break;
    case 6: ev->kind = QA_Q2_SERVER_CENTER_PRINT; ev->text = "mid"; break;
    case 7: ev->kind = QA_Q2_SERVER_COMMAND_TEXT; ev->text = "cmd x\n"; break;
    case 8: ev->kind = QA_Q2_SERVER_LAYOUT; ev->text = "xv 0 yv 0"; break;
    case 9: ev->kind = QA_Q2_SERVER_ACHIEVEMENT; ev->text = "ach"; break;
    case 10: ev->kind = QA_Q2_SERVER_CONFIG_STRING; ev->index = 1234; ev->text = "models/x.md2"; break;
    case 11: ev->kind = QA_Q2_SERVER_BASELINE; ent_from(cd_ents[2], &ev->entity); break;
    case 12: {
      ev->kind = QA_Q2_SERVER_TEMPORARY_ENTITY;
      qa_q2_temp_entity *t = &ev->temp;
      t->type = 5; t->field_count = 5;
      t->fields[0].kind = QA_Q2_TEMP_FIELD_INTEGER; t->fields[0].name = QA_Q2_TEMP_NAME_ENTITY1; t->fields[0].integer_value = 300;
      t->fields[1].kind = QA_Q2_TEMP_FIELD_INTEGER; t->fields[1].name = QA_Q2_TEMP_NAME_COUNT; t->fields[1].integer_value = 7;
      t->fields[2].kind = QA_Q2_TEMP_FIELD_INTEGER; t->fields[2].name = QA_Q2_TEMP_NAME_TIME; t->fields[2].integer_value = 123456;
      t->fields[3].kind = QA_Q2_TEMP_FIELD_VECTOR; t->fields[3].name = QA_Q2_TEMP_NAME_POSITION1; vec(&t->fields[3].vector_value, 1.1, -2.2, 3.3);
      t->fields[4].kind = QA_Q2_TEMP_FIELD_VECTOR; t->fields[4].name = QA_Q2_TEMP_NAME_DIRECTION; vec(&t->fields[4].vector_value, 0, 1, 0);
      break;
    }
    case 13: {
      ev->kind = QA_Q2_SERVER_TEMPORARY_ENTITY;
      qa_q2_temp_entity *t = &ev->temp;
      t->type = 128; t->field_count = 2;
      t->fields[0].kind = QA_Q2_TEMP_FIELD_INTEGER; t->fields[0].name = QA_Q2_TEMP_NAME_COUNT; t->fields[0].integer_value = 700;
      t->fields[1].kind = QA_Q2_TEMP_FIELD_VECTOR; t->fields[1].name = QA_Q2_TEMP_NAME_OFFSET; vec(&t->fields[1].vector_value, -100000, 0.125, 524287.875);
      break;
    }
    case 14: ev->kind = QA_Q2_SERVER_MUZZLE_FLASH; ev->flash_entity = 12; ev->flash = 7; ev->monster = false; ev->silenced = true; break;
    case 15: ev->kind = QA_Q2_SERVER_MUZZLE_FLASH; ev->flash_entity = 12; ev->flash = 0x123; ev->monster = true; ev->silenced = false; break;
    case 16: ev->kind = QA_Q2_SERVER_MUZZLE_FLASH; ev->flash_entity = 12; ev->flash = 200; ev->monster = true; ev->silenced = true; break;
    case 17: ev->kind = QA_Q2_SERVER_INVENTORY; ev->counts = counts; ev->count_count = 5; break;
    case 18: ev->kind = QA_Q2_SERVER_DOWNLOAD; ev->percent = 50; ev->has_bytes = true; ev->bytes = dl; ev->bytes_length = 3; break;
    case 19: ev->kind = QA_Q2_SERVER_DOWNLOAD; ev->percent = 0; ev->has_bytes = false; break;
    case 20: ev->kind = QA_Q2_SERVER_SETTING; ev->index = 3; ev->value_number = -7; break;
    case 21: ev->kind = QA_Q2_SERVER_SEAT; ev->seat = 1; break;
    case 22: case 23: case 24: {
      ev->kind = QA_Q2_SERVER_SOUND;
      qa_q2_sound_message *s = &ev->sound;
      s->volume = 1; s->attenuation = 1;
      if (ei == 22) { s->index = 5; }
      if (ei == 23) {
        s->index = 300; s->entity = 9000; s->channel = 3; s->has_position = true; vec(&s->position, 1.5, -2.5, 100.125);
        s->volume = 0.5; s->attenuation = 2; s->delay_seconds = 0.1;
      }
      if (ei == 24) { s->flags = 4; s->index = 5; s->entity = 1; s->channel = 1; }
      break;
    }
    case 25: {
      ev->kind = QA_Q2_SERVER_FOG;
      qa_q2_svc_fog_data *f = &ev->fog;
      f->bits = 0xffff; f->density = 0.5; f->skyfactor = 3; f->red = 1; f->green = 2; f->blue = 3; f->time = 400;
      f->hf_falloff = 1.5; f->hf_density = 2.5; f->hf_start_r = 4; f->hf_start_g = 5; f->hf_start_b = 6; f->hf_start_dist = 70000;
      f->hf_end_r = 7; f->hf_end_g = 8; f->hf_end_b = 9; f->hf_end_dist = -5;
      break;
    }
    case 26: ev->kind = QA_Q2_SERVER_FOG; ev->fog.bits = 0x11; ev->fog.density = 0.25; ev->fog.skyfactor = 1; ev->fog.time = 10; break;
    case 27:
      ev->kind = QA_Q2_SERVER_DAMAGE;
      memset(damage, 0, sizeof damage);
      damage[0].damage = 40; damage[0].health = true; damage[0].shield = true; set3(damage[0].direction, 1, 0, 0);
      damage[1].damage = 3; damage[1].armor = true; set3(damage[1].direction, 0, 0, -1);
      ev->indicators = damage; ev->indicator_count = 2;
      break;
    case 28:
      ev->kind = QA_Q2_SERVER_POI;
      ev->poi.key = 3; ev->poi.time = 5000; set3(ev->poi.pos, 1, 2, 3.5); ev->poi.image = 40; ev->poi.color = 208; ev->poi.flags = 1;
      break;
    case 29:
      ev->kind = QA_Q2_SERVER_HELP_PATH;
      ev->help_path.start = true; set3(ev->help_path.pos, -1, 2, 3); set3(ev->help_path.dir, 0, 1, 0);
      break;
    case 30:
      ev->kind = QA_Q2_SERVER_LOCALIZED_PRINT;
      ev->localized.flags = 2; strcpy(ev->localized.base, "$g_x"); strcpy(ev->localized.args[0], "a"); strcpy(ev->localized.args[1], "bb");
      ev->localized.arg_count = 2;
      break;
    default: break;
  }
}
static void run_events(void) {
  static qa_q2_server_event ev;
  static qa_q2_wire_codec wire;
  int open_proto = -1;
  for (size_t k = 0; k < COUNT(cd_events); k++) {
    int pi = cd_events[k].proto;
    /* TS keeps one wire per protocol: the Q2PRO server-data write updates its features. */
    if (pi != open_proto) {
      if (open_proto >= 0) qa_q2_wire_codec_destroy(&wire);
      qa_q2_protocol_identity protocol;
      memset(&protocol, 0, sizeof protocol);
      protocol.kind = cd_protos[pi].kind;
      protocol.version = (int64_t)cd_protos[pi].version;
      protocol.revision = (int64_t)cd_protos[pi].revision;
      CHECK(qa_q2_wire_codec_init(&wire, protocol));
      if (cd_protos[pi].flags >= 0) qa_q2_wire_codec_accept_q2pro_features(&wire, cd_protos[pi].revision, cd_protos[pi].flags);
      open_proto = pi;
    }
    build_event(cd_events[k].event, &ev);
    event_job j = {&wire, &ev, NULL, 0};
    qa_caught_error caught;
    checks++;
    if (qa_try(run_event, &j, &caught)) {
      char *got = malloc(2 * j.length + 1);
      to_hex(j.bytes, j.length, got);
      if (cd_events[k].hex == NULL || strcmp(got, cd_events[k].hex) != 0) {
        failures++;
        fprintf(stderr, "FAIL event proto %d event %d\n got %s\n exp %s\n", pi, cd_events[k].event, got,
                cd_events[k].hex ? cd_events[k].hex : cd_events[k].error);
      }
      free(got);
      free(j.bytes);
    } else {
      char text[4200];
      snprintf(text, sizeof text, "%s: %s", caught.name, caught.message);
      if (cd_events[k].error == NULL || strcmp(text, cd_events[k].error) != 0) {
        failures++;
        fprintf(stderr, "FAIL event proto %d event %d\n got error %s\n exp %s\n", pi, cd_events[k].event, text,
                cd_events[k].error ? cd_events[k].error : cd_events[k].hex);
      }
    }
  }
  if (open_proto >= 0) qa_q2_wire_codec_destroy(&wire);
}

/* ---------- decoders ---------- */
typedef struct dec_job {
  const char *family;
  int from;
  qa_q2_sizebuf *m;
  double values[2048];
  int count;
  char str[4200];
  qa_q2_clc_batch_move move; /* owned by the job so a raise leaves it to the caller */
} dec_job;

typedef struct dec_ctx {
  qa_q2_vanilla_context vanilla;
  qa_q2_r1_context r1_1903, r1_1905;
  qa_q2repro_context rr;
} dec_ctx;
static dec_ctx D;

static void dec_ent(dec_job *j, qa_q2_protocol_codec *c) {
  qa_q2_entity_bits eb;
  memset(&eb, 0, sizeof eb);
  c->read_entity_bits(c, &eb);
  qa_q2_wire_entity from, to;
  ent_from(cd_ents[j->from], &from);
  qa_q2_wire_entity_init(&to);
  c->read_delta_entity(c, &from, &to, eb.number, eb.bits_u64);
  j->values[0] = eb.number; j->values[1] = eb.bits; j->values[2] = (double)j->m->readcount;
  j->count = 3 + ent_canon(&to, j->values + 3);
}
static void dec_ps(dec_job *j, qa_q2_protocol_codec *c) {
  qa_q2_wire_player from, to;
  player_from(cd_players[j->from], &from);
  qa_q2_wire_player_init(&to);
  c->read_player_state_delta(c, j->m, &from, &to);
  j->values[0] = (double)j->m->readcount;
  j->count = 1 + player_canon(&to, j->values + 1);
}
static void dec_mvd_result(dec_job *j, const qa_q2_mvd_player_result *r) {
  j->values[0] = r->number; j->values[1] = r->removed ? 1 : 0; j->values[2] = (double)j->m->readcount;
  j->count = 3 + player_canon(&r->ps, j->values + 3);
}
static void dec_cmd(dec_job *j, int kind) {
  qa_q2_wire_usercmd from, to;
  cmd_from(cd_cmds[j->from], &from);
  qa_q2_wire_usercmd_init(&to);
  switch (kind) {
    case 0: qa_q2_msg_read_delta_usercmd(j->m, &from, &to); break;
    case 1: D.r1_1903.codec.read_delta_usercmd(&D.r1_1903.codec, j->m, &from, &to); break;
    case 2: D.r1_1905.codec.read_delta_usercmd(&D.r1_1905.codec, j->m, &from, &to); break;
    case 3: D.rr.codec.read_delta_usercmd(&D.rr.codec, j->m, &from, &to); break;
    case 4: D.rr.classic_codec.read_delta_usercmd(&D.rr.classic_codec, j->m, &from, &to); break;
    default: qa_q2_read_kex_usercmd(j->m, &from, &to); break;
  }
  j->values[0] = (double)j->m->readcount;
  j->count = 1 + cmd_canon(&to, j->values + 1);
}
static void dec_batch(dec_job *j, qa_q2_protocol_codec *c, bool nodelta) {
  CHECK(c->read_batch_move(c, j->m, nodelta, 0, &j->move));
  int i = 0;
  j->values[i++] = j->move.lastframe; j->values[i++] = j->move.num_dups; j->values[i++] = (double)j->move.frame_count;
  for (size_t f = 0; f < j->move.frame_count; f++) {
    j->values[i++] = (double)j->move.frames[f].cmd_count;
    for (size_t k = 0; k < j->move.frames[f].cmd_count; k++) i += cmd_canon(&j->move.frames[f].cmds[k], j->values + i);
  }
  j->values[i++] = (double)j->m->readcount;
  j->count = i;
}
static void dec_frame(dec_job *j, qa_q2_protocol_codec *c, bool r1) {
  int32_t raw = qa_q2_msg_read_byte(j->m);
  if (r1) qa_q2_r1_set_frame_extrabits(&D.r1_1905, raw & 0xe0);
  uint8_t area[32];
  memset(area, 0, sizeof area);
  qa_q2_frame_header h;
  memset(&h, 0, sizeof h);
  c->read_frame_header(c, area, sizeof area, true, &h);
  qa_q2_wire_player from, to;
  player_from(cd_players[j->from], &from);
  qa_q2_wire_player_init(&to);
  c->read_frame_playerstate(c, &from, &to);
  int i = 0;
  j->values[i++] = h.serverframe; j->values[i++] = h.deltaframe; j->values[i++] = h.surpress_count; j->values[i++] = h.areabytes;
  for (int k = 0; k < 4; k++) j->values[i++] = area[k];
  j->values[i++] = (double)j->m->readcount;
  j->count = i + player_canon(&to, j->values + i);
}
static void dec_sd(dec_job *j, qa_q2_protocol_codec *c) {
  static qa_q2_server_data_result d;
  c->read_server_data(c, &d);
  int i = 0;
  j->values[i++] = d.servercount; j->values[i++] = d.attractloop ? 1 : 0; j->values[i++] = d.clientnum; j->values[i++] = d.server_state;
  j->values[i++] = d.has_r1q2_version ? d.r1q2_version : -9999;
  j->values[i++] = d.has_r1q2_strafejump_hack ? (d.r1q2_strafejump_hack ? 1 : 0) : -9999;
  j->values[i++] = d.has_protocol_revision ? d.protocol_revision : -9999;
  j->values[i++] = d.has_wire_flags ? d.wire_flags : -9999;
  j->values[i++] = d.has_server_fps ? d.server_fps : -9999;
  j->values[i++] = (double)j->m->readcount;
  j->count = i;
  snprintf(j->str, sizeof j->str, "%s|%s", d.gamedir, d.levelname);
}
static void dec_msg_reads(dec_job *j) {
  qa_q2_sizebuf *m = j->m;
  int i = 0;
  j->values[i++] = qa_q2_msg_read_char(m);
  j->values[i++] = qa_q2_msg_read_byte(m);
  j->values[i++] = qa_q2_msg_read_short(m);
  j->values[i++] = qa_q2_msg_read_word(m);
  j->values[i++] = qa_q2_msg_read_long(m);
  j->values[i++] = qa_q2_msg_read_float(m);
  j->values[i++] = (double)qa_q2_msg_read_long64(m);
  j->values[i++] = qa_q2_msg_read_coord(m);
  j->values[i++] = qa_q2_msg_read_angle(m);
  j->values[i++] = qa_q2_msg_read_angle16(m);
  qa_q2_wire_vec3 pos;
  qa_q2_msg_read_pos(m, pos);
  for (int k = 0; k < 3; k++) j->values[i++] = pos[k];
  char s1[2048], s2[2048];
  qa_q2_msg_read_string(m, s1, sizeof s1);
  qa_q2_msg_read_string_line(m, s2, sizeof s2);
  uint8_t data[4] = {0};
  qa_q2_msg_read_data(m, data, 4, 6);
  for (int k = 0; k < 4; k++) j->values[i++] = data[k];
  j->values[i++] = (double)m->readcount;
  qa_q2_wire_vec3 dir = {0, 0, 0};
  qa_q2_msg_read_dir(m, dir);
  for (int k = 0; k < 3; k++) j->values[i++] = dir[k];
  j->values[i++] = (double)m->readcount;
  qa_q2_message_check_read(m);
  j->count = i;
  snprintf(j->str, sizeof j->str, "%s|%s", s1, s2);
}
static void run_dec(void *p) {
  dec_job *j = (dec_job *)p;
  const char *f = j->family;
  qa_q2_wire_player from;
  qa_q2_wire_player_init(&from);
  /* `from` indexes the table of the family's own state kind. */
  if (j->from >= 0 && (size_t)j->from < COUNT(cd_players)) player_from(cd_players[j->from], &from);
  const qa_q2_wire_player *from_or_null = j->from == 0 ? NULL : &from;
  qa_q2_mvd_player_result r;
  if (strcmp(f, "ent-vanilla") == 0) dec_ent(j, &D.vanilla.codec);
  else if (strcmp(f, "ent-r1q2-1903") == 0) dec_ent(j, &D.r1_1903.codec);
  else if (strcmp(f, "ent-r1q2-1905") == 0) dec_ent(j, &D.r1_1905.codec);
  else if (strcmp(f, "ent-q2repro") == 0) dec_ent(j, &D.rr.codec);
  else if (strcmp(f, "ps-vanilla") == 0) { qa_q2_msg_read_byte(j->m); dec_ps(j, &D.vanilla.codec); }
  else if (strcmp(f, "ps-r1q2") == 0) dec_ps(j, &D.r1_1905.codec);
  else if (strcmp(f, "ps-q2repro") == 0) dec_ps(j, &D.rr.codec);
  else if (strcmp(f, "mvd") == 0) { qa_q2_msg_read_delta_mvd_playerstate(j->m, from_or_null, &r); dec_mvd_result(j, &r); }
  else if (strcmp(f, "mvd-rr") == 0) { qa_q2_msg_read_delta_mvd_playerstate_rerelease(j->m, from_or_null, &r); dec_mvd_result(j, &r); }
  else if (strncmp(f, "mvd-p", 5) == 0) {
    int pi = atoi(f + 5);
    qa_q2_mvd_profile profile;
    qa_q2_get_mvd_profile(cd_mvd_profiles[pi][0], cd_mvd_profiles[pi][1], &profile);
    qa_q2_mvd_read_player(j->m, from_or_null, 3, profile, &r);
    dec_mvd_result(j, &r);
  }
  else if (strcmp(f, "cmd-msg") == 0) dec_cmd(j, 0);
  else if (strcmp(f, "cmd-r1q2-1903") == 0) dec_cmd(j, 1);
  else if (strcmp(f, "cmd-r1q2-1905") == 0) dec_cmd(j, 2);
  else if (strcmp(f, "cmd-q2repro") == 0) dec_cmd(j, 3);
  else if (strcmp(f, "cmd-q2repro-classic") == 0) dec_cmd(j, 4);
  else if (strcmp(f, "cmd-kex") == 0) dec_cmd(j, 5);
  else if (strcmp(f, "batch-q2repro") == 0) dec_batch(j, &D.rr.codec, false);
  else if (strcmp(f, "batch-q2repro-classic") == 0) dec_batch(j, &D.rr.classic_codec, false);
  else if (strcmp(f, "batch-q2repro-nodelta") == 0) dec_batch(j, &D.rr.codec, true);
  else if (strcmp(f, "frame-vanilla") == 0) dec_frame(j, &D.vanilla.codec, false);
  else if (strcmp(f, "frame-r1q2") == 0) dec_frame(j, &D.r1_1905.codec, true);
  else if (strcmp(f, "frame-q2repro") == 0) dec_frame(j, &D.rr.codec, false);
  else if (strcmp(f, "sd-vanilla") == 0) dec_sd(j, &D.vanilla.codec);
  else if (strcmp(f, "sd-r1q2") == 0) dec_sd(j, &D.r1_1905.codec);
  else if (strcmp(f, "sd-q2repro") == 0) dec_sd(j, &D.rr.codec);
  else if (strcmp(f, "mvdcmd") == 0) {
    qa_q2_mvd_cmd a, b;
    qa_q2_msg_read_mvd_cmd(j->m, &a);
    qa_q2_msg_read_mvd_cmd(j->m, &b);
    j->values[0] = a.op; j->values[1] = a.extrabits; j->values[2] = b.op; j->values[3] = b.extrabits;
    j->values[4] = (double)j->m->readcount;
    j->count = 5;
  } else if (strcmp(f, "fog-q2repro") == 0) {
    qa_q2_svc_fog_data fog;
    qa_q2repro_read_fog(&D.rr, &fog);
    const double v[] = {fog.bits, fog.density, fog.skyfactor, fog.red, fog.green, fog.blue, fog.time, fog.hf_falloff, fog.hf_density,
                        fog.hf_start_r, fog.hf_start_g, fog.hf_start_b, fog.hf_start_dist, fog.hf_end_r, fog.hf_end_g, fog.hf_end_b,
                        fog.hf_end_dist, (double)j->m->readcount};
    memcpy(j->values, v, sizeof v);
    j->count = (int)COUNT(v);
  } else if (strcmp(f, "msg-reads") == 0) dec_msg_reads(j);
  else { fprintf(stderr, "unknown family %s\n", f); failures++; }
}

/* Runs one decode over bytes[0..length) starting at `start`; returns false when it raised. */
static bool decode_bytes(dec_job *j, uint8_t *bytes, size_t length, int start, qa_caught_error *caught) {
  static qa_q2_sizebuf m;
  qa_q2_sizebuf_init_empty(&m);
  qa_q2_message_load(&m, bytes, length);
  m.readcount = start;
  memset(&D, 0, sizeof D);
  qa_q2_vanilla_context_init(&D.vanilla, &m);
  qa_q2_r1_context_init(&D.r1_1903, &m);
  qa_q2_r1_create_codec(&D.r1_1903, 1903);
  qa_q2_r1_context_init(&D.r1_1905, &m);
  qa_q2_r1_create_codec(&D.r1_1905, 1905);
  qa_q2repro_context_init(&D.rr, &m);
  j->m = &m;
  j->count = 0;
  j->str[0] = '\0';
  memset(&j->move, 0, sizeof j->move);
  bool ok = qa_try(run_dec, j, caught);
  qa_q2_clc_batch_move_free(&j->move);
  return ok;
}

static void run_decoders(void) {
  static dec_job j;
  static uint8_t bytes[4096];
  for (size_t k = 0; k < COUNT(cd_dec); k++) {
    size_t length = from_hex(cd_dec[k].hex, bytes, sizeof bytes);
    j.family = cd_dec[k].family;
    j.from = cd_dec[k].from;
    qa_caught_error caught;
    memset(&caught, 0, sizeof caught);
    bool ok = decode_bytes(&j, bytes, length, cd_dec[k].start, &caught);
    checks++;
    if (!ok) {
      char text[4200];
      snprintf(text, sizeof text, "%s: %s", caught.name, caught.message);
      if (cd_dec[k].error == NULL || strcmp(text, cd_dec[k].error) != 0) {
        failures++;
        fprintf(stderr, "FAIL dec %zu %s %s: got error %s exp %s\n", k, cd_dec[k].family, cd_dec[k].hex, text,
                cd_dec[k].error ? cd_dec[k].error : "(values)");
      }
      continue;
    }
    if (cd_dec[k].error != NULL) {
      failures++;
      fprintf(stderr, "FAIL dec %zu %s %s: no error, exp %s\n", k, cd_dec[k].family, cd_dec[k].hex, cd_dec[k].error);
      continue;
    }
    bool same = j.count == cd_dec[k].count && strcmp(j.str, cd_dec[k].str) == 0;
    int bad = -1;
    for (int i = 0; same && i < j.count; i++)
      if (!same_number(j.values[i], cd_dec[k].values[i])) { same = false; bad = i; }
    if (!same) {
      failures++;
      fprintf(stderr, "FAIL dec %zu %s %s: count %d/%d str '%s'/'%s'", k, cd_dec[k].family, cd_dec[k].hex, j.count,
              cd_dec[k].count, j.str, cd_dec[k].str);
      if (bad >= 0) fprintf(stderr, " value[%d] %.17g exp %.17g", bad, j.values[bad], cd_dec[k].values[bad]);
      fprintf(stderr, "\n");
    }
  }
}

/* Truncate/mutate every captured decoder input with a fixed-seed PRNG: no crash, no leak,
 * no sanitizer report; raises are the TS throws. */
static uint32_t rng_state = 0x2545f491u;
static uint32_t rng(void) {
  rng_state ^= rng_state << 13;
  rng_state ^= rng_state >> 17;
  rng_state ^= rng_state << 5;
  return rng_state;
}
static void run_malformed(void) {
  static dec_job j;
  static uint8_t base[4096], work[4096];
  size_t runs = 0, raised = 0;
  for (size_t k = 0; k < COUNT(cd_dec); k++) {
    size_t length = from_hex(cd_dec[k].hex, base, sizeof base);
    j.family = cd_dec[k].family;
    j.from = cd_dec[k].from;
    for (size_t cut = 0; cut <= length; cut++) {
      memcpy(work, base, length);
      qa_caught_error caught;
      int start = cd_dec[k].start <= (int)cut ? cd_dec[k].start : 0;
      if (!decode_bytes(&j, work, cut, start, &caught)) raised++;
      runs++;
    }
    for (int round = 0; round < 12; round++) {
      size_t n = length + (rng() % 4);
      if (n > sizeof work) n = sizeof work;
      memcpy(work, base, length);
      for (size_t i = length; i < n; i++) work[i] = (uint8_t)rng();
      int flips = 1 + (int)(rng() % 4);
      for (int f = 0; f < flips && n > 0; f++) work[rng() % n] = (uint8_t)rng();
      qa_caught_error caught;
      if (!decode_bytes(&j, work, n, 0, &caught)) raised++;
      runs++;
    }
  }
  CHECK(runs > 1000);
  printf("malformed decodes: %zu runs, %zu raised\n", runs, raised);
}

/* ---------- adapters ---------- */
static size_t ad_index = 0;
typedef struct ad_job { int which; double values[512]; int count; } ad_job;
static const double classic_stats[4] = {1, 40000, -40000, 7};
static double sixty_five[65];
static double rr_stats[64];
static void fill_view(qa_q2_player_view *v) {
  memset(v, 0, sizeof *v);
  vec(&v->view_angles, 1, 2, 3); vec(&v->view_offset, 0, 0, 22.5); vec(&v->kick_angles, -1, 0.5, 0);
  vec(&v->gun_angles, 0.25, 0, 0); vec(&v->gun_offset, 1, 1, 1);
  v->gun_index = 7; v->gun_frame = 3; v->fov = 90; v->render_flags = 1;
}
static void classic_player(qa_q2_player_state *p, const double *stats, size_t count) {
  memset(p, 0, sizeof *p);
  fill_view(&p->view);
  p->view.stats.values = stats; p->view.stats.count = count;
  p->movement.type = 1;
  p->movement.origin_eighths.values[0] = 16; p->movement.origin_eighths.values[1] = -2147483651.0; p->movement.origin_eighths.values[2] = 4294967301.0;
  for (int k = 0; k < 3; k++) p->movement.velocity_eighths.values[k] = k + 1;
  p->movement.flags = 4; p->movement.time_eight_milliseconds = 5; p->movement.gravity = 800;
  p->movement.delta_angle_shorts.values[0] = 70000; p->movement.delta_angle_shorts.values[1] = -1; p->movement.delta_angle_shorts.values[2] = 32768;
  p->blend.x = 0.1; p->blend.y = 0.2; p->blend.z = 0.3; p->blend.w = 0.4;
}
static int view_flat(const qa_q2_player_view *c, double *o) {
  int i = 0;
  const qa_vec3 *v[5] = {&c->view_angles, &c->view_offset, &c->kick_angles, &c->gun_angles, &c->gun_offset};
  for (int k = 0; k < 5; k++) { o[i++] = v[k]->x; o[i++] = v[k]->y; o[i++] = v[k]->z; }
  o[i++] = c->gun_index; o[i++] = c->gun_frame; o[i++] = c->fov; o[i++] = c->render_flags; o[i++] = (double)c->stats.count;
  for (size_t k = 0; k < c->stats.count; k++) o[i++] = c->stats.values[k];
  return i;
}
static void entity_contract(qa_q2_entity_state *s) {
  memset(s, 0, sizeof *s);
  s->number = 9; vec(&s->origin, 1.1, 2, 3); vec(&s->angles, 4, 5, 6.7); vec(&s->old_origin, 7, 8, 9);
  s->model_indexes[0] = 1; s->model_indexes[1] = 2; s->model_indexes[2] = 300; s->model_indexes[3] = 4;
  s->frame = 5; s->skin = 70000; s->effects = 2147483648.0; s->render_effects = 3; s->solid = 31; s->sound = 2; s->event = 1;
}
static void run_adapter(void *p) {
  ad_job *j = (ad_job *)p;
  double *o = j->values;
  int i = 0;
  qa_q2_wire_entity we;
  qa_q2_wire_player wp;
  qa_q2_wire_usercmd wc;
  qa_q2_entity_state es;
  double storage[64];
  switch (j->which) {
    case 0: entity_contract(&es); qa_q2_from_entity(&es, false, 0, 0, 0, 0, 0, 0, 0, 0, &we); i = ent_canon(&we, o); break;
    case 1: entity_contract(&es); qa_q2_from_entity(&es, true, 0x123456789abcdefull, 0.5, 2, 3, 0.25, -1, 8, 4, &we); i = ent_canon(&we, o); break;
    case 2: {
      ent_from(cd_ents[4], &we); we.morefx = 0x80000001u; we.effects = -2;
      qa_q2_to_entity(&we, &es);
      o[i++] = es.number;
      const qa_vec3 *v[3] = {&es.origin, &es.angles, &es.old_origin};
      for (int k = 0; k < 3; k++) { o[i++] = v[k]->x; o[i++] = v[k]->y; o[i++] = v[k]->z; }
      for (int k = 0; k < 4; k++) o[i++] = es.model_indexes[k];
      o[i++] = es.frame; o[i++] = es.skin; o[i++] = es.effects; o[i++] = es.render_effects; o[i++] = es.solid; o[i++] = es.sound; o[i++] = es.event;
      break;
    }
    case 3: {
      ent_from(cd_ents[4], &we); we.morefx = 0x80000001u; we.effects = -2;
      qa_q2_rerelease_entity_state rs;
      qa_q2_to_rerelease_entity(&we, &rs);
      o[i++] = (double)(rs.effects & 0xffffffffull); o[i++] = (double)(rs.effects >> 32);
      o[i++] = rs.alpha; o[i++] = rs.scale; o[i++] = rs.instance_bits; o[i++] = rs.loop_volume; o[i++] = rs.loop_attenuation;
      o[i++] = rs.owner; o[i++] = rs.old_frame;
      break;
    }
    case 4: { qa_q2_player_state ps; classic_player(&ps, classic_stats, 4); qa_q2_from_player_classic(&ps, &wp); i = player_canon(&wp, o); break; }
    case 5: {
      qa_q2_rerelease_player_state ps;
      memset(&ps, 0, sizeof ps);
      fill_view(&ps.view);
      for (int k = 0; k < 64; k++) rr_stats[k] = k * 1000;
      ps.view.stats.values = rr_stats; ps.view.stats.count = 64;
      ps.movement.type = 2; vec(&ps.movement.origin, 1.5, -2.5, 3); vec(&ps.movement.velocity, 4, 5, 6);
      ps.movement.flags = 7; ps.movement.time_milliseconds = 300; ps.movement.gravity = 600;
      vec(&ps.movement.delta_angles, -90, 180.5, 359.9); ps.movement.view_height = 22;
      ps.gun_skin = 3; ps.gun_rate = 20;
      ps.screen_blend.x = 1; ps.screen_blend.w = 0.5; ps.damage_blend.y = 1; ps.damage_blend.w = 0.25; ps.team_id = 2;
      qa_q2_from_player_rerelease(&ps, &wp);
      i = player_canon(&wp, o);
      break;
    }
    case 6: {
      for (int k = 0; k < 65; k++) sixty_five[k] = 1;
      qa_q2_player_state ps; classic_player(&ps, sixty_five, 65); qa_q2_from_player_classic(&ps, &wp); i = player_canon(&wp, o);
      break;
    }
    case 7: {
      player_from(cd_players[2], &wp);
      qa_q2_player_state ps;
      qa_q2_to_player(&wp, storage, &ps);
      i = view_flat(&ps.view, o);
      o[i++] = ps.movement.type;
      for (int k = 0; k < 3; k++) o[i++] = ps.movement.origin_eighths.values[k];
      for (int k = 0; k < 3; k++) o[i++] = ps.movement.velocity_eighths.values[k];
      o[i++] = ps.movement.flags; o[i++] = ps.movement.time_eight_milliseconds; o[i++] = ps.movement.gravity;
      for (int k = 0; k < 3; k++) o[i++] = ps.movement.delta_angle_shorts.values[k];
      o[i++] = ps.blend.x; o[i++] = ps.blend.y; o[i++] = ps.blend.z; o[i++] = ps.blend.w;
      break;
    }
    case 8: case 9: {
      player_from(cd_players[j->which == 8 ? 2 : 1], &wp);
      qa_q2_rerelease_player_state ps;
      qa_q2_to_rerelease_player(&wp, storage, &ps);
      i = view_flat(&ps.view, o);
      const qa_q2_rerelease_movement_state *mv = &ps.movement;
      o[i++] = mv->type; o[i++] = mv->origin.x; o[i++] = mv->origin.y; o[i++] = mv->origin.z;
      o[i++] = mv->velocity.x; o[i++] = mv->velocity.y; o[i++] = mv->velocity.z;
      o[i++] = mv->flags; o[i++] = mv->time_milliseconds; o[i++] = mv->gravity;
      o[i++] = mv->delta_angles.x; o[i++] = mv->delta_angles.y; o[i++] = mv->delta_angles.z; o[i++] = mv->view_height;
      o[i++] = ps.gun_skin; o[i++] = ps.gun_rate;
      o[i++] = ps.screen_blend.x; o[i++] = ps.screen_blend.y; o[i++] = ps.screen_blend.z; o[i++] = ps.screen_blend.w;
      o[i++] = ps.damage_blend.x; o[i++] = ps.damage_blend.y; o[i++] = ps.damage_blend.z; o[i++] = ps.damage_blend.w;
      o[i++] = ps.team_id;
      break;
    }
    case 10: {
      qa_q2_user_command c;
      memset(&c, 0, sizeof c);
      c.milliseconds = 16; c.angle_shorts.values[0] = 70000; c.angle_shorts.values[1] = -32769; c.angle_shorts.values[2] = 5;
      c.forward_move = 1.5; c.side_move = -2; c.up_move = 3; c.buttons = 1; c.impulse = 2; c.light_level = 9;
      qa_q2_from_command_classic(&c, &wc);
      i = cmd_canon(&wc, o);
      break;
    }
    case 11: {
      qa_q2_rerelease_user_command c;
      memset(&c, 0, sizeof c);
      c.milliseconds = 8; vec(&c.angles, -90, 180, 359.99); c.forward_move = 400.5; c.side_move = -1; c.buttons = 3; c.server_frame = 99;
      qa_q2_from_command_rerelease(&c, &wc);
      i = cmd_canon(&wc, o);
      break;
    }
    case 12: {
      cmd_from(cd_cmds[2], &wc);
      qa_q2_user_command c;
      qa_q2_to_command(&wc, &c);
      o[i++] = c.milliseconds;
      for (int k = 0; k < 3; k++) o[i++] = c.angle_shorts.values[k];
      o[i++] = c.forward_move; o[i++] = c.side_move; o[i++] = c.up_move; o[i++] = c.buttons; o[i++] = c.impulse; o[i++] = c.light_level;
      break;
    }
    case 13: {
      cmd_from(cd_cmds[2], &wc);
      qa_q2_rerelease_user_command c;
      qa_q2_to_rerelease_command(&wc, 55, &c);
      o[i++] = c.milliseconds; o[i++] = c.angles.x; o[i++] = c.angles.y; o[i++] = c.angles.z;
      o[i++] = c.forward_move; o[i++] = c.side_move; o[i++] = c.buttons; o[i++] = c.server_frame;
      break;
    }
    default: break;
  }
  j->count = i;
}
static void run_adapters(void) {
  static const char *const labels[] = {"fromEntity classic", "fromEntity rerelease", "toEntity", "toRereleaseEntity",
    "fromPlayer classic", "fromPlayer rerelease", "fromPlayer too many stats", "toPlayer", "toRereleasePlayer float",
    "toRereleasePlayer short", "fromCommand classic", "fromCommand rerelease", "toCommand", "toRereleaseCommand"};
  CHECK(COUNT(labels) == COUNT(cd_adapters));
  static ad_job j;
  for (ad_index = 0; ad_index < COUNT(labels) && ad_index < COUNT(cd_adapters); ad_index++) {
    CHECK(strcmp(labels[ad_index], cd_adapters[ad_index].label) == 0);
    memset(&j, 0, sizeof j);
    j.which = (int)ad_index;
    qa_caught_error caught;
    checks++;
    if (!qa_try(run_adapter, &j, &caught)) {
      char text[4200];
      snprintf(text, sizeof text, "%s: %s", caught.name, caught.message);
      if (cd_adapters[ad_index].error == NULL || strcmp(text, cd_adapters[ad_index].error) != 0) {
        failures++;
        fprintf(stderr, "FAIL adapter %s: error %s\n", labels[ad_index], text);
      }
      continue;
    }
    bool same = cd_adapters[ad_index].error == NULL && j.count == cd_adapters[ad_index].count;
    int bad = -1;
    for (int i = 0; same && i < j.count; i++)
      if (!same_number(j.values[i], cd_adapters[ad_index].values[i])) { same = false; bad = i; }
    if (!same) {
      failures++;
      fprintf(stderr, "FAIL adapter %s count %d/%d", labels[ad_index], j.count, cd_adapters[ad_index].count);
      if (bad >= 0) fprintf(stderr, " value[%d] %.17g exp %.17g", bad, j.values[bad], cd_adapters[ad_index].values[bad]);
      fprintf(stderr, "\n");
    }
  }
}

int main(void) {
  writers_init();
  run_encoders();
  run_events();
  run_decoders();
  run_adapters();
  run_malformed();
  printf("test_q2_codecs_depth: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
