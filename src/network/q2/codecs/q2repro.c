/* Port of src/network/q2/codecs/q2repro.ts. GPL-2.0-or-later. */
#include "qa/network/q2/codecs/q2repro.h"
#include "qa/foundation/js.h"
#include "qa/foundation/js_truthy.h"

#include <math.h>
#include <string.h>

#include "qa/network/q2/codecs/clc_batch_move.h"
#include "qa/network/q2/constants.h"

#define U_ANGLE16_RR (1u << 13)
#define U_MODEL16_RR (1u << 28)
#define U_MOREFX8_RR (1u << 29)
#define U_ALPHA_RR (1u << 30)
#define U_MOREBITS4_RR (1u << 31)
#define EPS_GUNRATE_RR (1 << 7)
#define GUNOFFSET_SCALE_RR 512
#define GUNANGLE_SCALE_RR 4096

static qa_q2repro_binding *bind_of(qa_q2_protocol_codec *self) {
  return (qa_q2repro_binding *)self->ctx;
}

double qa_q2repro_combine_bits(double lo, double hi) {
  return (double)qa_js_to_uint32(lo) + hi * 4294967296.0; /* (lo >>> 0) + hi * 2^32 */
}
bool qa_q2repro_bits_has_hi(double bits, double hi_flag) {
  return (qa_js_to_int32(floor(bits / 4294967296.0)) & qa_js_to_int32(hi_flag)) != 0;
}
double qa_q2repro_encode_alpha(double x) {
  if (x == 0) return 0;
  double v = trunc(x * 255.0); /* a TS number: NaN stays NaN, huge values clamp */
  if (v < 1) v = 1;
  if (v > 255) v = 255;
  return v;
}
double qa_q2repro_decode_alpha(double b) { return b == 0 ? 0 : b / 255.0; }
double qa_q2repro_encode_scale(double x) {
  if (x == 0) return 0;
  double v = trunc(x * 16.0); /* a TS number: NaN stays NaN, huge values clamp */
  if (v < 1) v = 1;
  if (v > 255) v = 255;
  return v;
}
double qa_q2repro_decode_scale(double b) { return b == 0 ? 0 : b / 16.0; }
double qa_q2repro_encode_loop_volume(double x) {
  if (x == 0) return 0;
  double v = trunc(x * 255.0);
  if (v < 0) v = 0;
  if (v > 255) v = 255;
  if (v == 255) v = 0;
  return v;
}
double qa_q2repro_decode_loop_volume(double b) { return b == 0 ? 0 : b / 255.0; }
double qa_q2repro_encode_loop_attenuation(double x) {
  if (x == -1) return 192;
  double v = trunc(x * 64.0);
  if (v < 0) v = 0;
  if (v > 255) v = 255;
  if (v == 192) v = 0;
  return v;
}
double qa_q2repro_decode_loop_attenuation(double b) {
  return b == 192 ? -1 : b / 64.0;
}
static int32_t clamp_i16(double x) {
  /* TS clampInt16 passes NaN (a peer's MSG_ReadFloat) through; every result is
   * stored into an Int32Array/Int16Array, where NaN becomes 0. */
  if (isnan(x)) return 0;
  if (x < -32768) return -32768;
  if (x > 32767) return 32767;
  return (int32_t)x;
}
/* TS encodeFixed16 = clampInt16(Math.trunc(x * scale)) kept as a number: the
 * plain-array copies compare NaN as changed (NaN !== NaN) and the short write
 * stores ToInt32(NaN) = 0. */
static double encode_fixed16(double x, double scale) {
  double v = trunc(x * scale);
  if (v < -32768) return -32768;
  if (v > 32767) return 32767;
  return v;
}
int32_t qa_q2repro_pm_float_to_short(double f) {
  /* Math.round rounds half up (toward +inf on .5). */
  return clamp_i16(qa_js_round(f * 8.0));
}
static int width_of(double value, bool uint16_safe) {
  uint32_t v = qa_js_to_uint32(value); /* value >>> 0 */
  uint32_t mask32 = uint16_safe ? 0xffff0000u : 0xffff8000u;
  if ((v & mask32) != 0) return 2;
  if ((v & 0xff00u) != 0) return 1;
  return 0;
}

void qa_q2repro_write_entity_bits_wide(qa_q2_sizebuf *msg, uint32_t lo0, uint32_t hi,
                                       double entnum) {
  uint32_t lo = lo0;
  if (entnum >= 256) lo |= (uint32_t)QA_Q2_U_NUMBER16;
  if (hi != 0) lo |= U_MOREBITS4_RR | (uint32_t)QA_Q2_U_MOREBITS3 | (uint32_t)QA_Q2_U_MOREBITS2 | (uint32_t)QA_Q2_U_MOREBITS1;
  else if ((lo & 0xff000000u) != 0) lo |= (uint32_t)QA_Q2_U_MOREBITS3 | (uint32_t)QA_Q2_U_MOREBITS2 | (uint32_t)QA_Q2_U_MOREBITS1;
  else if ((lo & 0x00ff0000u) != 0) lo |= (uint32_t)QA_Q2_U_MOREBITS2 | (uint32_t)QA_Q2_U_MOREBITS1;
  else if ((lo & 0x0000ff00u) != 0) lo |= (uint32_t)QA_Q2_U_MOREBITS1;
  qa_q2_msg_write_byte(msg, (double)(lo & 0xffu));
  if ((lo & (uint32_t)QA_Q2_U_MOREBITS1) != 0) qa_q2_msg_write_byte(msg, (double)((lo >> 8) & 0xffu));
  if ((lo & (uint32_t)QA_Q2_U_MOREBITS2) != 0) qa_q2_msg_write_byte(msg, (double)((lo >> 16) & 0xffu));
  if ((lo & (uint32_t)QA_Q2_U_MOREBITS3) != 0) qa_q2_msg_write_byte(msg, (double)((lo >> 24) & 0xffu));
  if ((lo & U_MOREBITS4_RR) != 0) qa_q2_msg_write_byte(msg, (double)(hi & 0xffu));
  if ((lo & (uint32_t)QA_Q2_U_NUMBER16) != 0) qa_q2_msg_write_short(msg, entnum);
  else qa_q2_msg_write_byte(msg, entnum);
}

void qa_q2repro_read_entity_bits_wide(qa_q2repro_context *ctx,
                                      qa_q2_entity_bits *out) {
  qa_q2_sizebuf *m = ctx->net_message;
  int32_t lo = qa_q2_msg_read_byte(m);
  if (lo & QA_Q2_U_MOREBITS1) lo |= (int32_t)((uint32_t)qa_q2_msg_read_byte(m) << 8);
  if (lo & QA_Q2_U_MOREBITS2) lo |= (int32_t)((uint32_t)qa_q2_msg_read_byte(m) << 16);
  if (lo & QA_Q2_U_MOREBITS3) lo |= (int32_t)((uint32_t)qa_q2_msg_read_byte(m) << 24);
  int32_t hi = 0;
  if ((lo & (int32_t)U_MOREBITS4_RR) != 0) hi = qa_q2_msg_read_byte(m);
  out->number = (lo & QA_Q2_U_NUMBER16) != 0 ? (double)qa_q2_msg_read_word(m)
                                             : (double)qa_q2_msg_read_byte(m);
  out->bits = qa_q2repro_combine_bits((double)lo, (double)hi);
  out->bits_u64 = (uint32_t)lo | ((uint64_t)(uint32_t)hi << 32);
}

static void write_server_data_as(qa_q2_sizebuf *msg, double protocol,
                                 const qa_q2_server_data_params *params) {
  qa_q2_msg_write_byte(msg, QA_Q2_SVC_SERVERDATA);
  qa_q2_msg_write_long(msg, protocol);
  qa_q2_msg_write_long(msg, params->servercount);
  qa_q2_msg_write_byte(msg, params->attractloop ? 1 : 0);
  qa_q2_msg_write_string(msg, params->gamedir);
  qa_q2_msg_write_short(msg, params->clientnum);
  qa_q2_msg_write_string(msg, params->levelname);
  qa_q2_msg_write_short(msg, params->has_protocol_revision ? params->protocol_revision : 1024);
  qa_q2_msg_write_byte(msg, (double)(qa_js_to_int32(params->server_state) & 0xff));
  qa_q2_msg_write_short(msg, params->has_wire_flags ? params->wire_flags : 0);
  qa_q2_msg_write_byte(msg, params->has_server_fps ? params->server_fps : 10);
}
static void w_server_data(qa_q2_protocol_codec *self, qa_q2_sizebuf *msg,
                          const qa_q2_server_data_params *params) {
  (void)self;
  write_server_data_as(msg, QA_Q2_PROTOCOL_VERSION_RERELEASE, params);
}
static void w_server_data_classic(qa_q2_protocol_codec *self, qa_q2_sizebuf *msg,
                                  const qa_q2_server_data_params *params) {
  (void)self;
  write_server_data_as(msg, QA_Q2_PROTOCOL_VERSION_RERELEASE_CLASSIC, params);
}

static void write_delta_entity(qa_q2_protocol_codec *self, qa_q2_sizebuf *msg,
                               const qa_q2_wire_entity *from,
                               const qa_q2_wire_entity *to, bool force,
                               bool newentity) {
  (void)self;
  uint32_t lo = 0;
  uint32_t hi = 0;
  if (to->origin[0] != from->origin[0]) lo |= (uint32_t)QA_Q2_U_ORIGIN1;
  if (to->origin[1] != from->origin[1]) lo |= (uint32_t)QA_Q2_U_ORIGIN2;
  if (to->origin[2] != from->origin[2]) lo |= (uint32_t)QA_Q2_U_ORIGIN3;
  int32_t to_angle[3] = {qa_q2_angle2short((double)to->angles[0]),
                         qa_q2_angle2short((double)to->angles[1]),
                         qa_q2_angle2short((double)to->angles[2])};
  int32_t from_angle[3] = {qa_q2_angle2short((double)from->angles[0]),
                           qa_q2_angle2short((double)from->angles[1]),
                           qa_q2_angle2short((double)from->angles[2])};
  if (to_angle[0] != from_angle[0]) lo |= (uint32_t)QA_Q2_U_ANGLE1;
  if (to_angle[1] != from_angle[1]) lo |= (uint32_t)QA_Q2_U_ANGLE2;
  if (to_angle[2] != from_angle[2]) lo |= (uint32_t)QA_Q2_U_ANGLE3;
  if ((lo & ((uint32_t)QA_Q2_U_ANGLE1 | (uint32_t)QA_Q2_U_ANGLE2 | (uint32_t)QA_Q2_U_ANGLE3)) != 0) lo |= U_ANGLE16_RR;
  if (to->skinnum != from->skinnum) {
    int w = width_of(to->skinnum, true);
    lo |= w == 2 ? ((uint32_t)QA_Q2_U_SKIN8 | (uint32_t)QA_Q2_U_SKIN16)
                 : w == 1 ? (uint32_t)QA_Q2_U_SKIN16 : (uint32_t)QA_Q2_U_SKIN8;
  }
  if (to->frame != from->frame) lo |= to->frame >= 256 ? (uint32_t)QA_Q2_U_FRAME16 : (uint32_t)QA_Q2_U_FRAME8;
  if (to->effects != from->effects) {
    int w = width_of(to->effects, true);
    lo |= w == 2 ? ((uint32_t)QA_Q2_U_EFFECTS8 | (uint32_t)QA_Q2_U_EFFECTS16)
                 : w == 1 ? (uint32_t)QA_Q2_U_EFFECTS16 : (uint32_t)QA_Q2_U_EFFECTS8;
  }
  if (to->morefx != from->morefx) {
    int w = width_of(to->morefx, true);
    if (w == 0) lo |= U_MOREFX8_RR;
    else if (w == 1) hi |= QA_Q2REPRO_HI_MOREFX16;
    else { lo |= U_MOREFX8_RR; hi |= QA_Q2REPRO_HI_MOREFX16; }
  }
  if (to->renderfx != from->renderfx) {
    int w = width_of(to->renderfx, true);
    lo |= w == 2 ? ((uint32_t)QA_Q2_U_RENDERFX8 | (uint32_t)QA_Q2_U_RENDERFX16)
                 : w == 1 ? (uint32_t)QA_Q2_U_RENDERFX16 : (uint32_t)QA_Q2_U_RENDERFX8;
  }
  if (to->solid != from->solid) lo |= (uint32_t)QA_Q2_U_SOLID;
  if (qa_js_truthy(to->event)) lo |= (uint32_t)QA_Q2_U_EVENT;
  if (to->modelindex != from->modelindex) lo |= (uint32_t)QA_Q2_U_MODEL;
  if (to->modelindex2 != from->modelindex2) lo |= (uint32_t)QA_Q2_U_MODEL2;
  if (to->modelindex3 != from->modelindex3) lo |= (uint32_t)QA_Q2_U_MODEL3;
  if (to->modelindex4 != from->modelindex4) lo |= (uint32_t)QA_Q2_U_MODEL4;
  if (((lo & (uint32_t)QA_Q2_U_MODEL) != 0 && to->modelindex > 255) ||
      ((lo & (uint32_t)QA_Q2_U_MODEL2) != 0 && to->modelindex2 > 255) ||
      ((lo & (uint32_t)QA_Q2_U_MODEL3) != 0 && to->modelindex3 > 255) ||
      ((lo & (uint32_t)QA_Q2_U_MODEL4) != 0 && to->modelindex4 > 255)) lo |= U_MODEL16_RR;
  if (to->sound != from->sound) lo |= (uint32_t)QA_Q2_U_SOUND;
  bool vol_changed = to->loop_volume != from->loop_volume;
  bool attn_changed = to->loop_attenuation != from->loop_attenuation;
  if (newentity || (qa_js_to_int32(to->renderfx) & QA_Q2_RF_BEAM) != 0) lo |= (uint32_t)QA_Q2_U_OLDORIGIN;
  if (to->alpha != from->alpha) lo |= U_ALPHA_RR;
  if (to->scale != from->scale) hi |= QA_Q2REPRO_HI_SCALE;
  if (lo == 0 && hi == 0 && !force) return;
  qa_q2repro_write_entity_bits_wide(msg, lo, hi, to->number);
  double models[4] = {to->modelindex, to->modelindex2, to->modelindex3, to->modelindex4};
  uint32_t mbits[4] = {(uint32_t)QA_Q2_U_MODEL, (uint32_t)QA_Q2_U_MODEL2,
                       (uint32_t)QA_Q2_U_MODEL3, (uint32_t)QA_Q2_U_MODEL4};
  for (int i = 0; i < 4; i++) {
    if ((lo & mbits[i]) != 0) {
      if ((lo & U_MODEL16_RR) != 0) qa_q2_msg_write_short(msg, models[i]);
      else qa_q2_msg_write_byte(msg, models[i]);
    }
  }
  if (lo & (uint32_t)QA_Q2_U_FRAME16) qa_q2_msg_write_short(msg, to->frame);
  else if (lo & (uint32_t)QA_Q2_U_FRAME8) qa_q2_msg_write_byte(msg, to->frame);
  if ((lo & ((uint32_t)QA_Q2_U_SKIN8 | (uint32_t)QA_Q2_U_SKIN16)) == ((uint32_t)QA_Q2_U_SKIN8 | (uint32_t)QA_Q2_U_SKIN16)) qa_q2_msg_write_long(msg, to->skinnum);
  else if (lo & (uint32_t)QA_Q2_U_SKIN16) qa_q2_msg_write_short(msg, to->skinnum);
  else if (lo & (uint32_t)QA_Q2_U_SKIN8) qa_q2_msg_write_byte(msg, to->skinnum);
  if ((lo & ((uint32_t)QA_Q2_U_EFFECTS8 | (uint32_t)QA_Q2_U_EFFECTS16)) == ((uint32_t)QA_Q2_U_EFFECTS8 | (uint32_t)QA_Q2_U_EFFECTS16)) qa_q2_msg_write_long(msg, to->effects);
  else if (lo & (uint32_t)QA_Q2_U_EFFECTS16) qa_q2_msg_write_short(msg, to->effects);
  else if (lo & (uint32_t)QA_Q2_U_EFFECTS8) qa_q2_msg_write_byte(msg, to->effects);
  if ((lo & ((uint32_t)QA_Q2_U_RENDERFX8 | (uint32_t)QA_Q2_U_RENDERFX16)) == ((uint32_t)QA_Q2_U_RENDERFX8 | (uint32_t)QA_Q2_U_RENDERFX16)) qa_q2_msg_write_long(msg, to->renderfx);
  else if (lo & (uint32_t)QA_Q2_U_RENDERFX16) qa_q2_msg_write_short(msg, to->renderfx);
  else if (lo & (uint32_t)QA_Q2_U_RENDERFX8) qa_q2_msg_write_byte(msg, to->renderfx);
  if (lo & (uint32_t)QA_Q2_U_ORIGIN1) qa_q2_msg_write_float(msg, (double)to->origin[0]);
  if (lo & (uint32_t)QA_Q2_U_ORIGIN2) qa_q2_msg_write_float(msg, (double)to->origin[1]);
  if (lo & (uint32_t)QA_Q2_U_ORIGIN3) qa_q2_msg_write_float(msg, (double)to->origin[2]);
  if (lo & (uint32_t)QA_Q2_U_ANGLE1) qa_q2_msg_write_short(msg, (double)to_angle[0]);
  if (lo & (uint32_t)QA_Q2_U_ANGLE2) qa_q2_msg_write_short(msg, (double)to_angle[1]);
  if (lo & (uint32_t)QA_Q2_U_ANGLE3) qa_q2_msg_write_short(msg, (double)to_angle[2]);
  if (lo & (uint32_t)QA_Q2_U_OLDORIGIN) {
    qa_q2_msg_write_float(msg, (double)to->old_origin[0]);
    qa_q2_msg_write_float(msg, (double)to->old_origin[1]);
    qa_q2_msg_write_float(msg, (double)to->old_origin[2]);
  }
  if (lo & (uint32_t)QA_Q2_U_SOUND) {
    int32_t word = qa_js_to_int32(to->sound) & 0x3fff;
    if (attn_changed) word |= QA_Q2REPRO_SOUND_FLAG_ATTENUATION;
    if (vol_changed) word |= QA_Q2REPRO_SOUND_FLAG_VOLUME;
    qa_q2_msg_write_short(msg, (double)word);
    if (word & QA_Q2REPRO_SOUND_FLAG_VOLUME) qa_q2_msg_write_byte(msg, qa_q2repro_encode_loop_volume(to->loop_volume));
    if (word & QA_Q2REPRO_SOUND_FLAG_ATTENUATION) qa_q2_msg_write_byte(msg, qa_q2repro_encode_loop_attenuation(to->loop_attenuation));
  }
  if (lo & (uint32_t)QA_Q2_U_EVENT) qa_q2_msg_write_byte(msg, to->event);
  if (lo & (uint32_t)QA_Q2_U_SOLID) qa_q2_msg_write_long(msg, to->solid);
  if ((hi & QA_Q2REPRO_HI_MOREFX16) != 0 && (lo & U_MOREFX8_RR) != 0) qa_q2_msg_write_long(msg, to->morefx);
  else if ((hi & QA_Q2REPRO_HI_MOREFX16) != 0) qa_q2_msg_write_short(msg, to->morefx);
  else if ((lo & U_MOREFX8_RR) != 0) qa_q2_msg_write_byte(msg, to->morefx);
  if (lo & U_ALPHA_RR) qa_q2_msg_write_byte(msg, qa_q2repro_encode_alpha(to->alpha));
  if (hi & QA_Q2REPRO_HI_SCALE) qa_q2_msg_write_byte(msg, qa_q2repro_encode_scale(to->scale));
}

typedef struct rr_enc {
  int32_t flags;
  int32_t extraflags;
  double to_viewoffset[3];
  int32_t to_viewangle[3];
  double to_kick[3];
  double to_gunoffset[3];
  double to_gunangles[3];
  int32_t blend_bits;
  int32_t damage_blend_bits;
  uint64_t statbits;
  const qa_q2_wire_player *to;
} rr_enc;

static int32_t byte_color(float x) { return qa_js_to_int32(trunc((double)x * 255.0)) & 0xff; }

static rr_enc encode_player(const qa_q2_wire_player *from, const qa_q2_wire_player *to) {
  rr_enc e;
  memset(&e, 0, sizeof e);
  e.to = to;
  if (to->pmove.pm_type != from->pmove.pm_type) e.flags |= QA_Q2_PS_M_TYPE;
  if (to->pmove.origin_f[0] != from->pmove.origin_f[0] || to->pmove.origin_f[1] != from->pmove.origin_f[1]) e.flags |= QA_Q2_PS_M_ORIGIN;
  if (to->pmove.origin_f[2] != from->pmove.origin_f[2]) e.extraflags |= QA_Q2_EPS_M_ORIGIN2;
  if (to->pmove.velocity_f[0] != from->pmove.velocity_f[0] || to->pmove.velocity_f[1] != from->pmove.velocity_f[1]) e.flags |= QA_Q2_PS_M_VELOCITY;
  if (to->pmove.velocity_f[2] != from->pmove.velocity_f[2]) e.extraflags |= QA_Q2_EPS_M_VELOCITY2;
  if (to->pmove.pm_time != from->pmove.pm_time) e.flags |= QA_Q2_PS_M_TIME;
  if (to->pmove.pm_flags != from->pmove.pm_flags) e.flags |= QA_Q2_PS_M_FLAGS;
  if (to->pmove.gravity != from->pmove.gravity) e.flags |= QA_Q2_PS_M_GRAVITY;
  if (to->pmove.delta_angles[0] != from->pmove.delta_angles[0] || to->pmove.delta_angles[1] != from->pmove.delta_angles[1] || to->pmove.delta_angles[2] != from->pmove.delta_angles[2]) e.flags |= QA_Q2_PS_M_DELTA_ANGLES;
  if (to->pmove.viewheight != from->pmove.viewheight) e.flags |= QA_Q2_PS_RR_VIEWHEIGHT;
  for (int i = 0; i < 3; i++) {
    e.to_viewoffset[i] = encode_fixed16((double)to->viewoffset[i], QA_Q2REPRO_VIEWOFFSET_SCALE);
  }
  bool vo_changed = false;
  for (int i = 0; i < 3; i++) {
    if (e.to_viewoffset[i] != encode_fixed16((double)from->viewoffset[i], QA_Q2REPRO_VIEWOFFSET_SCALE)) vo_changed = true;
  }
  if (vo_changed) e.flags |= QA_Q2_PS_VIEWOFFSET;
  for (int i = 0; i < 3; i++) e.to_viewangle[i] = qa_q2_angle2short((double)to->viewangles[i]);
  int32_t from_va[3];
  for (int i = 0; i < 3; i++) from_va[i] = qa_q2_angle2short((double)from->viewangles[i]);
  if (e.to_viewangle[0] != from_va[0] || e.to_viewangle[1] != from_va[1]) e.flags |= QA_Q2_PS_VIEWANGLES;
  if (e.to_viewangle[2] != from_va[2]) e.extraflags |= QA_Q2_EPS_VIEWANGLE2;
  bool kick_changed = false;
  for (int i = 0; i < 3; i++) {
    e.to_kick[i] = encode_fixed16((double)to->kick_angles[i], QA_Q2REPRO_KICK_ANGLE_SCALE);
    if (e.to_kick[i] != encode_fixed16((double)from->kick_angles[i], QA_Q2REPRO_KICK_ANGLE_SCALE)) kick_changed = true;
  }
  if (kick_changed) e.flags |= QA_Q2_PS_KICKANGLES;
  for (int i = 0; i < 4; i++) {
    if (byte_color(to->blend[i]) != byte_color(from->blend[i])) e.blend_bits |= (int32_t)(1u << i);
    if (byte_color(to->damage_blend[i]) != byte_color(from->damage_blend[i])) e.damage_blend_bits |= (int32_t)(1u << i);
  }
  if (e.blend_bits != 0 || e.damage_blend_bits != 0) e.flags |= QA_Q2_PS_BLEND;
  if (to->fov != from->fov) e.flags |= QA_Q2_PS_FOV;
  if (to->rdflags != from->rdflags) e.flags |= QA_Q2_PS_RDFLAGS;
  if (to->gunindex != from->gunindex || to->gunskin != from->gunskin) e.flags |= QA_Q2_PS_WEAPONINDEX;
  if (to->gunframe != from->gunframe) e.flags |= QA_Q2_PS_WEAPONFRAME;
  bool go_changed = false;
  bool ga_changed = false;
  for (int i = 0; i < 3; i++) {
    e.to_gunoffset[i] = encode_fixed16((double)to->gunoffset[i], GUNOFFSET_SCALE_RR);
    if (e.to_gunoffset[i] != encode_fixed16((double)from->gunoffset[i], GUNOFFSET_SCALE_RR)) go_changed = true;
    e.to_gunangles[i] = encode_fixed16((double)to->gunangles[i], GUNANGLE_SCALE_RR);
    if (e.to_gunangles[i] != encode_fixed16((double)from->gunangles[i], GUNANGLE_SCALE_RR)) ga_changed = true;
  }
  if (go_changed) e.extraflags |= QA_Q2_EPS_GUNOFFSET;
  if (ga_changed) e.extraflags |= QA_Q2_EPS_GUNANGLES;
  for (int i = 0; i < QA_Q2_MAX_STATS_STORAGE; i++) {
    if (to->stats[i] != from->stats[i]) e.statbits |= 1ull << i;
  }
  if (e.statbits != 0) e.extraflags |= QA_Q2_EPS_STATS;
  if (to->gunrate != from->gunrate) e.extraflags |= EPS_GUNRATE_RR;
  return e;
}

static void write_player_body(const rr_enc *e, qa_q2_sizebuf *msg) {
  const qa_q2_wire_player *to = e->to;
  int32_t flags = e->flags;
  int32_t extraflags = e->extraflags;
  if (flags & QA_Q2_PS_M_TYPE) qa_q2_msg_write_byte(msg, to->pmove.pm_type);
  if (flags & QA_Q2_PS_M_ORIGIN) {
    qa_q2_msg_write_float(msg, (double)to->pmove.origin_f[0]);
    qa_q2_msg_write_float(msg, (double)to->pmove.origin_f[1]);
  }
  if (extraflags & QA_Q2_EPS_M_ORIGIN2) qa_q2_msg_write_float(msg, (double)to->pmove.origin_f[2]);
  if (flags & QA_Q2_PS_M_VELOCITY) {
    qa_q2_msg_write_float(msg, (double)to->pmove.velocity_f[0]);
    qa_q2_msg_write_float(msg, (double)to->pmove.velocity_f[1]);
  }
  if (extraflags & QA_Q2_EPS_M_VELOCITY2) qa_q2_msg_write_float(msg, (double)to->pmove.velocity_f[2]);
  if (flags & QA_Q2_PS_M_TIME) qa_q2_msg_write_short(msg, to->pmove.pm_time);
  if (flags & QA_Q2_PS_M_FLAGS) qa_q2_msg_write_short(msg, to->pmove.pm_flags);
  if (flags & QA_Q2_PS_M_GRAVITY) qa_q2_msg_write_short(msg, to->pmove.gravity);
  if (flags & QA_Q2_PS_M_DELTA_ANGLES) {
    qa_q2_msg_write_short(msg, (double)to->pmove.delta_angles[0]);
    qa_q2_msg_write_short(msg, (double)to->pmove.delta_angles[1]);
    qa_q2_msg_write_short(msg, (double)to->pmove.delta_angles[2]);
  }
  if (flags & QA_Q2_PS_VIEWOFFSET) {
    qa_q2_msg_write_short(msg, (double)e->to_viewoffset[0]);
    qa_q2_msg_write_short(msg, (double)e->to_viewoffset[1]);
    qa_q2_msg_write_short(msg, (double)e->to_viewoffset[2]);
  }
  if (flags & QA_Q2_PS_VIEWANGLES) {
    qa_q2_msg_write_short(msg, (double)e->to_viewangle[0]);
    qa_q2_msg_write_short(msg, (double)e->to_viewangle[1]);
  }
  if (extraflags & QA_Q2_EPS_VIEWANGLE2) qa_q2_msg_write_short(msg, (double)e->to_viewangle[2]);
  if (flags & QA_Q2_PS_KICKANGLES) {
    qa_q2_msg_write_short(msg, (double)e->to_kick[0]);
    qa_q2_msg_write_short(msg, (double)e->to_kick[1]);
    qa_q2_msg_write_short(msg, (double)e->to_kick[2]);
  }
  if (flags & QA_Q2_PS_WEAPONINDEX) {
    int32_t gun = (qa_js_to_int32(to->gunindex) & 0xffff) |
                  ((int32_t)((uint32_t)qa_js_to_int32(to->gunskin) << QA_Q2REPRO_GUNINDEX_BITS) & 0xffff);
    qa_q2_msg_write_short(msg, (double)gun);
  }
  if (flags & QA_Q2_PS_WEAPONFRAME) qa_q2_msg_write_short(msg, to->gunframe);
  if (extraflags & QA_Q2_EPS_GUNOFFSET) {
    qa_q2_msg_write_short(msg, (double)e->to_gunoffset[0]);
    qa_q2_msg_write_short(msg, (double)e->to_gunoffset[1]);
    qa_q2_msg_write_short(msg, (double)e->to_gunoffset[2]);
  }
  if (extraflags & QA_Q2_EPS_GUNANGLES) {
    qa_q2_msg_write_short(msg, (double)e->to_gunangles[0]);
    qa_q2_msg_write_short(msg, (double)e->to_gunangles[1]);
    qa_q2_msg_write_short(msg, (double)e->to_gunangles[2]);
  }
  if (flags & QA_Q2_PS_BLEND) {
    qa_q2_msg_write_byte(msg, (double)((e->blend_bits & 0xf) | ((e->damage_blend_bits & 0xf) << 4)));
    for (int i = 0; i < 4; i++) {
      if (e->blend_bits & ((int32_t)(1u << i))) qa_q2_msg_write_byte(msg, (double)byte_color(to->blend[i]));
    }
    for (int i = 0; i < 4; i++) {
      if (e->damage_blend_bits & ((int32_t)(1u << i))) qa_q2_msg_write_byte(msg, (double)byte_color(to->damage_blend[i]));
    }
  }
  if (flags & QA_Q2_PS_FOV) qa_q2_msg_write_byte(msg, to->fov);
  if (flags & QA_Q2_PS_RDFLAGS) qa_q2_msg_write_byte(msg, to->rdflags);
  if (extraflags & QA_Q2_EPS_STATS) {
    int64_t signed_bits;
    uint64_t ub = e->statbits;
    memcpy(&signed_bits, &ub, sizeof signed_bits);
    qa_q2_msg_write_long64(msg, signed_bits);
    for (int i = 0; i < QA_Q2_MAX_STATS_STORAGE; i++) {
      if ((e->statbits & (1ull << i)) != 0) qa_q2_msg_write_short(msg, (double)to->stats[i]);
    }
  }
  if (extraflags & EPS_GUNRATE_RR) qa_q2_msg_write_byte(msg, to->gunrate);
  if (flags & QA_Q2_PS_RR_VIEWHEIGHT) qa_q2_msg_write_char(msg, to->pmove.viewheight);
}

static void write_player_state_delta(qa_q2_protocol_codec *self, qa_q2_sizebuf *msg,
                                     const qa_q2_wire_player *from,
                                     const qa_q2_wire_player *to) {
  (void)self;
  rr_enc e = encode_player(from, to);
  qa_q2_msg_write_byte(msg, QA_Q2_SVC_PLAYERINFO);
  qa_q2_msg_write_short(msg, (double)e.flags);
  qa_q2_msg_write_byte(msg, (double)e.extraflags);
  write_player_body(&e, msg);
}

static void w_usercmd(qa_q2_protocol_codec *self, qa_q2_sizebuf *msg,
                      const qa_q2_wire_usercmd *from, const qa_q2_wire_usercmd *cmd) {
  (void)self;
  if (cmd->upmove != from->upmove) {
    qa_q2_com_error_throw(QA_Q2_ERR_DROP,
                          "q2repro (1038) non-batched clc_move cannot encode upmove changes; use batched movement");
  }
  qa_q2_msg_write_delta_usercmd(msg, from, cmd);
}
static void w_usercmd_classic(qa_q2_protocol_codec *self, qa_q2_sizebuf *msg,
                              const qa_q2_wire_usercmd *from,
                              const qa_q2_wire_usercmd *cmd) {
  (void)self;
  qa_q2_msg_write_delta_usercmd(msg, from, cmd);
}
static void r_usercmd(qa_q2_protocol_codec *self, qa_q2_sizebuf *msg,
                      const qa_q2_wire_usercmd *from, qa_q2_wire_usercmd *move) {
  (void)self;
  int64_t at = msg->readcount;
  if (at >= 0 && (uint64_t)at < (uint64_t)msg->data_length) {
    int32_t bits = msg->data[at];
    if ((bits & QA_Q2_CM_UP) != 0) {
      qa_q2_throw_error("q2repro (1038) non-batched movement has reserved CM_UP bit");
    }
  }
  qa_q2_msg_read_delta_usercmd(msg, from, move);
}
static void r_usercmd_classic(qa_q2_protocol_codec *self, qa_q2_sizebuf *msg,
                              const qa_q2_wire_usercmd *from,
                              qa_q2_wire_usercmd *move) {
  (void)self;
  qa_q2_msg_read_delta_usercmd(msg, from, move);
}

typedef struct batch_ctx {
  bool classic;
} batch_ctx;

static void decode_batch_cmd(void *vctx, qa_q2_bit_reader *br,
                             const qa_q2_wire_usercmd *prev, qa_q2_wire_usercmd *out) {
  batch_ctx *bc = (batch_ctx *)vctx;
  qa_q2_batch_move_seed_from_prev(prev, out);
  int32_t has_contents = qa_q2_bit_reader_read_unsigned(br, 1);
  if (has_contents == 0) return;
  int32_t bits = qa_q2_bit_reader_read_unsigned(br, 8);
  if (bits & QA_Q2_CM_ANGLE1) out->angles[0] = (int16_t)qa_q2_read_batch_move_angle_component(br, prev != NULL ? prev->angles[0] : 0);
  if (bits & QA_Q2_CM_ANGLE2) out->angles[1] = (int16_t)qa_q2_read_batch_move_angle_component(br, prev != NULL ? prev->angles[1] : 0);
  if (bits & QA_Q2_CM_ANGLE3) out->angles[2] = (int16_t)qa_q2_bit_reader_read_signed(br, 16);
  if (bits & QA_Q2_CM_FORWARD) out->forwardmove = (double)qa_q2_bit_reader_read_signed(br, 10);
  if (bits & QA_Q2_CM_SIDE) out->sidemove = (double)qa_q2_bit_reader_read_signed(br, 10);
  if (bits & QA_Q2_CM_UP) {
    if (!bc->classic) {
      qa_q2_batch_move_error("clc_q2pro_move_batched (1038): CM_UP bit set -- q2repro's own decoder rejects this (Q2P_ERR_BAD_DATA, q2proto_proto_q2repro.c:2662-2663)");
    }
    out->upmove = (double)qa_q2_bit_reader_read_signed(br, 10);
  }
  if (bits & QA_Q2_CM_BUTTONS) out->buttons = (double)qa_q2_bit_reader_read_unsigned(br, 8);
  if (bits & QA_Q2_CM_IMPULSE) out->msec = (double)qa_q2_bit_reader_read_unsigned(br, 8);
  else out->msec = prev != NULL ? prev->msec : 0;
}

static int32_t clamp512(double v) {
  double c = v < -512 ? -512 : v > 511 ? 511 : v;
  return qa_js_to_int32(c); /* Math.max(-512, Math.min(511, NaN)) is NaN; writeSigned stores 0 */
}

static void encode_batch_cmd(void *vctx, qa_q2_bit_writer *bw,
                             const qa_q2_wire_usercmd *cmd,
                             const qa_q2_wire_usercmd *prev) {
  batch_ctx *bc = (batch_ctx *)vctx;
  qa_q2_bit_writer_write_unsigned(bw, 1, 1);
  int32_t bits = QA_Q2_CM_IMPULSE;
  int32_t p0 = prev != NULL ? prev->angles[0] : 0;
  int32_t p1 = prev != NULL ? prev->angles[1] : 0;
  int32_t p2 = prev != NULL ? prev->angles[2] : 0;
  if (cmd->angles[0] != p0) bits |= QA_Q2_CM_ANGLE1;
  if (cmd->angles[1] != p1) bits |= QA_Q2_CM_ANGLE2;
  if (cmd->angles[2] != p2) bits |= QA_Q2_CM_ANGLE3;
  if (cmd->forwardmove != (prev != NULL ? prev->forwardmove : 0)) bits |= QA_Q2_CM_FORWARD;
  if (cmd->sidemove != (prev != NULL ? prev->sidemove : 0)) bits |= QA_Q2_CM_SIDE;
  if (bc->classic && cmd->upmove != (prev != NULL ? prev->upmove : 0)) bits |= QA_Q2_CM_UP;
  if (cmd->buttons != (prev != NULL ? prev->buttons : 0)) bits |= QA_Q2_CM_BUTTONS;
  qa_q2_bit_writer_write_unsigned(bw, bits, 8);
  if (bits & QA_Q2_CM_ANGLE1) {
    qa_q2_bit_writer_write_unsigned(bw, 0, 1);
    qa_q2_bit_writer_write_signed(bw, cmd->angles[0], 16);
  }
  if (bits & QA_Q2_CM_ANGLE2) {
    qa_q2_bit_writer_write_unsigned(bw, 0, 1);
    qa_q2_bit_writer_write_signed(bw, cmd->angles[1], 16);
  }
  if (bits & QA_Q2_CM_ANGLE3) qa_q2_bit_writer_write_signed(bw, cmd->angles[2], 16);
  if (bits & QA_Q2_CM_FORWARD) qa_q2_bit_writer_write_signed(bw, clamp512(cmd->forwardmove), 10);
  if (bits & QA_Q2_CM_SIDE) qa_q2_bit_writer_write_signed(bw, clamp512(cmd->sidemove), 10);
  if (bits & QA_Q2_CM_UP) qa_q2_bit_writer_write_signed(bw, clamp512(trunc(cmd->upmove)), 10);
  if (bits & QA_Q2_CM_BUTTONS) qa_q2_bit_writer_write_unsigned(bw, qa_js_to_int32(cmd->buttons), 8);
  qa_q2_bit_writer_write_unsigned(bw, qa_js_to_int32(cmd->msec), 8);
}

static void write_batch_move_impl(qa_q2_sizebuf *msg, bool has_lastframe, double lastframe,
                                  const qa_q2_clc_batch_move_frame *frames,
                                  size_t frame_count, bool classic) {
  if (has_lastframe) qa_q2_msg_write_long(msg, lastframe);
  qa_q2_msg_write_byte(msg, frame_count > 0 ? (double)(frame_count - 1) : -1);
  double lightlevel = 0;
  if (classic) {
    for (size_t i = 0; i < frame_count; i++) {
      for (size_t j = 0; j < frames[i].cmd_count; j++) lightlevel = frames[i].cmds[j].lightlevel;
    }
  }
  qa_q2_msg_write_byte(msg, (double)(qa_js_to_int32(lightlevel) & 0xff));
  batch_ctx bc = {classic};
  qa_q2_bit_writer bw;
  qa_q2_bit_writer_init(&bw, msg);
  qa_q2_write_batch_move_frames(&bw, frames, frame_count, encode_batch_cmd, &bc);
}
static void w_batch(qa_q2_protocol_codec *self, qa_q2_sizebuf *msg, bool has_lastframe,
                    double lastframe, const qa_q2_clc_batch_move_frame *frames,
                    size_t frame_count) {
  (void)self;
  write_batch_move_impl(msg, has_lastframe, lastframe, frames, frame_count, false);
}
static void w_batch_classic(qa_q2_protocol_codec *self, qa_q2_sizebuf *msg,
                            bool has_lastframe, double lastframe,
                            const qa_q2_clc_batch_move_frame *frames,
                            size_t frame_count) {
  (void)self;
  write_batch_move_impl(msg, has_lastframe, lastframe, frames, frame_count, true);
}

static bool read_batch_move_impl(qa_q2_sizebuf *msg, bool nodelta, bool classic,
                                 qa_q2_clc_batch_move *out) {
  double lastframe = nodelta ? -1 : (double)qa_q2_msg_read_long(msg);
  int32_t num_dups = qa_q2_msg_read_byte(msg);
  if (num_dups < 0) {
    qa_q2_batch_move_error("clc_q2pro_move_batched (1038): truncated message (num_dups)");
  }
  if (num_dups >= QA_Q2_MAX_CLC_BATCH_MOVE_FRAMES - 1) {
    qa_q2_batch_move_error("clc_q2pro_move_batched (1038): num_dups out of range (Q2P_ERR_BAD_DATA, q2proto_proto_q2repro.c:2687-2688)");
  }
  int32_t lightlevel = qa_q2_msg_read_byte(msg);
  if (lightlevel < 0) {
    qa_q2_batch_move_error("clc_q2pro_move_batched (1038): truncated message (lightlevel)");
  }
  batch_ctx bc = {classic};
  qa_q2_bit_reader br;
  qa_q2_bit_reader_init(&br, msg);
  out->lastframe = lastframe;
  out->num_dups = (double)num_dups;
  bool ok = qa_q2_read_batch_move_frames(&br, num_dups, decode_batch_cmd, &bc,
                                         &out->frames, &out->frame_count);
  if (ok && classic) {
    for (size_t i = 0; i < out->frame_count; i++) {
      for (size_t j = 0; j < out->frames[i].cmd_count; j++) {
        out->frames[i].cmds[j].lightlevel = (double)lightlevel;
      }
    }
  }
  return ok;
}
static bool r_batch(qa_q2_protocol_codec *self, qa_q2_sizebuf *msg, bool nodelta,
                    double opcode_extra, qa_q2_clc_batch_move *out) {
  (void)self;
  (void)opcode_extra;
  return read_batch_move_impl(msg, nodelta, false, out);
}
static bool r_batch_classic(qa_q2_protocol_codec *self, qa_q2_sizebuf *msg, bool nodelta,
                            double opcode_extra, qa_q2_clc_batch_move *out) {
  (void)self;
  (void)opcode_extra;
  return read_batch_move_impl(msg, nodelta, true, out);
}

static void read_userinfo_delta(qa_q2_protocol_codec *self, qa_q2_sizebuf *msg,
                                qa_q2_clc_userinfo_delta *out) {
  (void)self;
  qa_q2_msg_read_string(msg, out->name, sizeof out->name);
  qa_q2_msg_read_string(msg, out->value, sizeof out->value);
}
static void read_client_setting(qa_q2_protocol_codec *self, qa_q2_sizebuf *msg,
                                qa_q2_clc_client_setting *out) {
  (void)self;
  out->index = (double)qa_q2_msg_read_short(msg);
  out->value = (double)qa_q2_msg_read_short(msg);
}

static void read_server_data(qa_q2_protocol_codec *self,
                             qa_q2_server_data_result *out) {
  qa_q2_sizebuf *m = self->net_message;
  memset(out, 0, sizeof *out);
  out->servercount = (double)qa_q2_msg_read_long(m);
  out->attractloop = qa_q2_msg_read_byte(m) != 0;
  qa_q2_msg_read_string(m, out->gamedir, sizeof out->gamedir);
  out->clientnum = (double)qa_q2_msg_read_short(m);
  qa_q2_msg_read_string(m, out->levelname, sizeof out->levelname);
  out->has_protocol_revision = true;
  out->protocol_revision = (double)qa_q2_msg_read_word(m);
  out->server_state = (double)qa_q2_msg_read_byte(m);
  out->has_wire_flags = true;
  out->wire_flags = (double)qa_q2_msg_read_word(m);
  out->has_server_fps = true;
  out->server_fps = (double)qa_q2_msg_read_byte(m);
}

static void read_entity_bits(qa_q2_protocol_codec *self, qa_q2_entity_bits *out) {
  qa_q2repro_read_entity_bits_wide(bind_of(self)->context, out);
}

static void read_delta_entity(qa_q2_protocol_codec *self,
                              const qa_q2_wire_entity *from,
                              qa_q2_wire_entity *to, double number, uint64_t bits64) {
  qa_q2_sizebuf *m = self->net_message;
  uint32_t bits = (uint32_t)bits64;
  double bits_d = (double)bits64;
  to->number = from->number;
  qa_q2_vector_copy(from->origin, to->origin);
  qa_q2_vector_copy(from->angles, to->angles);
  qa_q2_vector_copy(from->old_origin, to->old_origin);
  to->modelindex = from->modelindex;
  to->modelindex2 = from->modelindex2;
  to->modelindex3 = from->modelindex3;
  to->modelindex4 = from->modelindex4;
  to->frame = from->frame;
  to->skinnum = from->skinnum;
  to->effects = from->effects;
  to->morefx = from->morefx;
  to->renderfx = from->renderfx;
  to->solid = from->solid;
  to->sound = from->sound;
  to->event = from->event;
  to->alpha = from->alpha;
  to->scale = from->scale;
  to->loop_volume = from->loop_volume;
  to->loop_attenuation = from->loop_attenuation;
  qa_q2_vector_copy(from->origin, to->old_origin);
  to->number = number;
  bool model16 = (bits & U_MODEL16_RR) != 0;
  double *models[4] = {&to->modelindex, &to->modelindex2, &to->modelindex3, &to->modelindex4};
  uint32_t mbits[4] = {(uint32_t)QA_Q2_U_MODEL, (uint32_t)QA_Q2_U_MODEL2,
                       (uint32_t)QA_Q2_U_MODEL3, (uint32_t)QA_Q2_U_MODEL4};
  for (int i = 0; i < 4; i++) {
    if ((bits & mbits[i]) != 0) {
      *models[i] = model16 ? (double)qa_q2_msg_read_word(m) : (double)qa_q2_msg_read_byte(m);
    }
  }
  if (bits & (uint32_t)QA_Q2_U_FRAME16) to->frame = (double)qa_q2_msg_read_word(m);
  else if (bits & (uint32_t)QA_Q2_U_FRAME8) to->frame = (double)qa_q2_msg_read_byte(m);
  if ((bits & ((uint32_t)QA_Q2_U_SKIN8 | (uint32_t)QA_Q2_U_SKIN16)) == ((uint32_t)QA_Q2_U_SKIN8 | (uint32_t)QA_Q2_U_SKIN16)) to->skinnum = (double)qa_q2_msg_read_long(m);
  else if (bits & (uint32_t)QA_Q2_U_SKIN16) to->skinnum = (double)qa_q2_msg_read_word(m);
  else if (bits & (uint32_t)QA_Q2_U_SKIN8) to->skinnum = (double)qa_q2_msg_read_byte(m);
  if ((bits & ((uint32_t)QA_Q2_U_EFFECTS8 | (uint32_t)QA_Q2_U_EFFECTS16)) == ((uint32_t)QA_Q2_U_EFFECTS8 | (uint32_t)QA_Q2_U_EFFECTS16)) to->effects = (double)qa_q2_msg_read_long(m);
  else if (bits & (uint32_t)QA_Q2_U_EFFECTS16) to->effects = (double)qa_q2_msg_read_word(m);
  else if (bits & (uint32_t)QA_Q2_U_EFFECTS8) to->effects = (double)qa_q2_msg_read_byte(m);
  if ((bits & ((uint32_t)QA_Q2_U_RENDERFX8 | (uint32_t)QA_Q2_U_RENDERFX16)) == ((uint32_t)QA_Q2_U_RENDERFX8 | (uint32_t)QA_Q2_U_RENDERFX16)) to->renderfx = (double)qa_q2_msg_read_long(m);
  else if (bits & (uint32_t)QA_Q2_U_RENDERFX16) to->renderfx = (double)qa_q2_msg_read_word(m);
  else if (bits & (uint32_t)QA_Q2_U_RENDERFX8) to->renderfx = (double)qa_q2_msg_read_byte(m);
  if (bits & (uint32_t)QA_Q2_U_ORIGIN1) to->origin[0] = (float)qa_q2_msg_read_float(m);
  if (bits & (uint32_t)QA_Q2_U_ORIGIN2) to->origin[1] = (float)qa_q2_msg_read_float(m);
  if (bits & (uint32_t)QA_Q2_U_ORIGIN3) to->origin[2] = (float)qa_q2_msg_read_float(m);
  uint32_t abits[3] = {(uint32_t)QA_Q2_U_ANGLE1, (uint32_t)QA_Q2_U_ANGLE2, (uint32_t)QA_Q2_U_ANGLE3};
  for (int i = 0; i < 3; i++) {
    if ((bits & abits[i]) != 0) {
      if ((bits & U_ANGLE16_RR) != 0) to->angles[i] = (float)qa_q2_short2angle((double)qa_q2_msg_read_short(m));
      else to->angles[i] = (float)((double)qa_q2_msg_read_byte(m) * (360.0 / 256.0));
    }
  }
  if (bits & (uint32_t)QA_Q2_U_OLDORIGIN) {
    to->old_origin[0] = (float)qa_q2_msg_read_float(m);
    to->old_origin[1] = (float)qa_q2_msg_read_float(m);
    to->old_origin[2] = (float)qa_q2_msg_read_float(m);
  }
  if (bits & (uint32_t)QA_Q2_U_SOUND) {
    int32_t word = qa_q2_msg_read_word(m);
    to->sound = (double)(word & 0x3fff);
    if (word & QA_Q2REPRO_SOUND_FLAG_VOLUME) to->loop_volume = qa_q2repro_decode_loop_volume((double)qa_q2_msg_read_byte(m));
    if (word & QA_Q2REPRO_SOUND_FLAG_ATTENUATION) to->loop_attenuation = qa_q2repro_decode_loop_attenuation((double)qa_q2_msg_read_byte(m));
  }
  if (bits & (uint32_t)QA_Q2_U_EVENT) to->event = (double)qa_q2_msg_read_byte(m);
  else to->event = 0;
  if (bits & (uint32_t)QA_Q2_U_SOLID) to->solid = (double)qa_q2_msg_read_long(m);
  if (qa_q2repro_bits_has_hi(bits_d, QA_Q2REPRO_HI_MOREFX16) && (bits & U_MOREFX8_RR) != 0) to->morefx = (double)qa_q2_msg_read_long(m);
  else if (qa_q2repro_bits_has_hi(bits_d, QA_Q2REPRO_HI_MOREFX16)) to->morefx = (double)qa_q2_msg_read_word(m);
  else if ((bits & U_MOREFX8_RR) != 0) to->morefx = (double)qa_q2_msg_read_byte(m);
  if (bits & U_ALPHA_RR) to->alpha = qa_q2repro_decode_alpha((double)qa_q2_msg_read_byte(m));
  if (qa_q2repro_bits_has_hi(bits_d, QA_Q2REPRO_HI_SCALE)) to->scale = qa_q2repro_decode_scale((double)qa_q2_msg_read_byte(m));
}

static void read_player_fields(qa_q2_sizebuf *msg, const qa_q2_wire_player *from,
                               qa_q2_wire_player *to, int32_t flags, int32_t extraflags) {
  to->pmove.pm_type = from->pmove.pm_type;
  memcpy(to->pmove.origin, from->pmove.origin, sizeof to->pmove.origin);
  memcpy(to->pmove.velocity, from->pmove.velocity, sizeof to->pmove.velocity);
  memcpy(to->pmove.origin_f, from->pmove.origin_f, sizeof to->pmove.origin_f);
  memcpy(to->pmove.velocity_f, from->pmove.velocity_f, sizeof to->pmove.velocity_f);
  to->pmove.pm_flags = from->pmove.pm_flags;
  to->pmove.pm_time = from->pmove.pm_time;
  to->pmove.gravity = from->pmove.gravity;
  memcpy(to->pmove.delta_angles, from->pmove.delta_angles, sizeof to->pmove.delta_angles);
  to->pmove.viewheight = from->pmove.viewheight;
  memcpy(to->viewangles, from->viewangles, sizeof to->viewangles);
  memcpy(to->viewoffset, from->viewoffset, sizeof to->viewoffset);
  memcpy(to->kick_angles, from->kick_angles, sizeof to->kick_angles);
  memcpy(to->gunangles, from->gunangles, sizeof to->gunangles);
  memcpy(to->gunoffset, from->gunoffset, sizeof to->gunoffset);
  to->gunindex = from->gunindex;
  to->gunskin = from->gunskin;
  to->gunframe = from->gunframe;
  to->gunrate = from->gunrate;
  memcpy(to->blend, from->blend, sizeof to->blend);
  memcpy(to->damage_blend, from->damage_blend, sizeof to->damage_blend);
  to->fov = from->fov;
  to->rdflags = from->rdflags;
  memcpy(to->stats, from->stats, sizeof to->stats);
  if (flags & QA_Q2_PS_M_TYPE) to->pmove.pm_type = (double)qa_q2_msg_read_byte(msg);
  double ox = (double)to->pmove.origin_f[0];
  double oy = (double)to->pmove.origin_f[1];
  double oz = (double)to->pmove.origin_f[2];
  if (flags & QA_Q2_PS_M_ORIGIN) {
    ox = qa_q2_msg_read_float(msg);
    oy = qa_q2_msg_read_float(msg);
  }
  if (extraflags & QA_Q2_EPS_M_ORIGIN2) oz = qa_q2_msg_read_float(msg);
  if ((flags & QA_Q2_PS_M_ORIGIN) != 0 || (extraflags & QA_Q2_EPS_M_ORIGIN2) != 0) {
    to->pmove.origin_f[0] = (float)ox;
    to->pmove.origin_f[1] = (float)oy;
    to->pmove.origin_f[2] = (float)oz;
    to->pmove.origin[0] = qa_q2repro_pm_float_to_short(ox);
    to->pmove.origin[1] = qa_q2repro_pm_float_to_short(oy);
    to->pmove.origin[2] = qa_q2repro_pm_float_to_short(oz);
  }
  double vx = (double)to->pmove.velocity_f[0];
  double vy = (double)to->pmove.velocity_f[1];
  double vz = (double)to->pmove.velocity_f[2];
  if (flags & QA_Q2_PS_M_VELOCITY) {
    vx = qa_q2_msg_read_float(msg);
    vy = qa_q2_msg_read_float(msg);
  }
  if (extraflags & QA_Q2_EPS_M_VELOCITY2) vz = qa_q2_msg_read_float(msg);
  if ((flags & QA_Q2_PS_M_VELOCITY) != 0 || (extraflags & QA_Q2_EPS_M_VELOCITY2) != 0) {
    to->pmove.velocity_f[0] = (float)vx;
    to->pmove.velocity_f[1] = (float)vy;
    to->pmove.velocity_f[2] = (float)vz;
    to->pmove.velocity[0] = qa_q2repro_pm_float_to_short(vx);
    to->pmove.velocity[1] = qa_q2repro_pm_float_to_short(vy);
    to->pmove.velocity[2] = qa_q2repro_pm_float_to_short(vz);
  }
  if (flags & QA_Q2_PS_M_TIME) to->pmove.pm_time = (double)qa_q2_msg_read_word(msg);
  if (flags & QA_Q2_PS_M_FLAGS) to->pmove.pm_flags = (double)qa_q2_msg_read_word(msg);
  if (flags & QA_Q2_PS_M_GRAVITY) to->pmove.gravity = (double)qa_q2_msg_read_short(msg);
  if (flags & QA_Q2_PS_M_DELTA_ANGLES) {
    to->pmove.delta_angles[0] = (int16_t)qa_q2_msg_read_short(msg);
    to->pmove.delta_angles[1] = (int16_t)qa_q2_msg_read_short(msg);
    to->pmove.delta_angles[2] = (int16_t)qa_q2_msg_read_short(msg);
  }
  if (flags & QA_Q2_PS_VIEWOFFSET) {
    to->viewoffset[0] = (float)((double)qa_q2_msg_read_short(msg) / QA_Q2REPRO_VIEWOFFSET_SCALE);
    to->viewoffset[1] = (float)((double)qa_q2_msg_read_short(msg) / QA_Q2REPRO_VIEWOFFSET_SCALE);
    to->viewoffset[2] = (float)((double)qa_q2_msg_read_short(msg) / QA_Q2REPRO_VIEWOFFSET_SCALE);
  }
  if (flags & QA_Q2_PS_VIEWANGLES) {
    to->viewangles[0] = (float)qa_q2_short2angle((double)qa_q2_msg_read_short(msg));
    to->viewangles[1] = (float)qa_q2_short2angle((double)qa_q2_msg_read_short(msg));
  }
  if (extraflags & QA_Q2_EPS_VIEWANGLE2) to->viewangles[2] = (float)qa_q2_short2angle((double)qa_q2_msg_read_short(msg));
  if (flags & QA_Q2_PS_KICKANGLES) {
    to->kick_angles[0] = (float)((double)qa_q2_msg_read_short(msg) / QA_Q2REPRO_KICK_ANGLE_SCALE);
    to->kick_angles[1] = (float)((double)qa_q2_msg_read_short(msg) / QA_Q2REPRO_KICK_ANGLE_SCALE);
    to->kick_angles[2] = (float)((double)qa_q2_msg_read_short(msg) / QA_Q2REPRO_KICK_ANGLE_SCALE);
  }
  if (flags & QA_Q2_PS_WEAPONINDEX) {
    int32_t gun = qa_q2_msg_read_word(msg);
    to->gunindex = (double)(gun & QA_Q2REPRO_GUNINDEX_MASK);
    to->gunskin = (double)((uint32_t)gun >> QA_Q2REPRO_GUNINDEX_BITS);
  }
  if (flags & QA_Q2_PS_WEAPONFRAME) to->gunframe = (double)qa_q2_msg_read_word(msg);
  if (extraflags & QA_Q2_EPS_GUNOFFSET) {
    to->gunoffset[0] = (float)((double)qa_q2_msg_read_short(msg) / GUNOFFSET_SCALE_RR);
    to->gunoffset[1] = (float)((double)qa_q2_msg_read_short(msg) / GUNOFFSET_SCALE_RR);
    to->gunoffset[2] = (float)((double)qa_q2_msg_read_short(msg) / GUNOFFSET_SCALE_RR);
  }
  if (extraflags & QA_Q2_EPS_GUNANGLES) {
    to->gunangles[0] = (float)((double)qa_q2_msg_read_short(msg) / GUNANGLE_SCALE_RR);
    to->gunangles[1] = (float)((double)qa_q2_msg_read_short(msg) / GUNANGLE_SCALE_RR);
    to->gunangles[2] = (float)((double)qa_q2_msg_read_short(msg) / GUNANGLE_SCALE_RR);
  }
  if (flags & QA_Q2_PS_BLEND) {
    int32_t bbits = qa_q2_msg_read_byte(msg);
    for (int i = 0; i < 4; i++) {
      if (bbits & ((int32_t)(1u << i))) to->blend[i] = (float)((double)qa_q2_msg_read_byte(msg) / 255.0);
    }
    for (int i = 0; i < 4; i++) {
      if (bbits & (1 << (i + 4))) to->damage_blend[i] = (float)((double)qa_q2_msg_read_byte(msg) / 255.0);
    }
  }
  if (flags & QA_Q2_PS_FOV) to->fov = (double)qa_q2_msg_read_byte(msg);
  if (flags & QA_Q2_PS_RDFLAGS) to->rdflags = (double)qa_q2_msg_read_byte(msg);
  if (extraflags & QA_Q2_EPS_STATS) {
    int64_t sb = qa_q2_msg_read_long64(msg);
    uint64_t statbits;
    memcpy(&statbits, &sb, sizeof statbits);
    for (int i = 0; i < QA_Q2_MAX_STATS_STORAGE; i++) {
      if ((statbits & (1ull << i)) != 0) to->stats[i] = (int16_t)qa_q2_msg_read_short(msg);
    }
  }
  if (extraflags & EPS_GUNRATE_RR) to->gunrate = (double)qa_q2_msg_read_byte(msg);
  if (flags & QA_Q2_PS_RR_VIEWHEIGHT) to->pmove.viewheight = (double)qa_q2_msg_read_char(msg);
}

static void read_player_state_delta(qa_q2_protocol_codec *self, qa_q2_sizebuf *msg,
                                    const qa_q2_wire_player *from,
                                    qa_q2_wire_player *to) {
  (void)self;
  int32_t flags = qa_q2_msg_read_word(msg);
  int32_t extraflags = qa_q2_msg_read_byte(msg);
  read_player_fields(msg, from, to, flags, extraflags);
}

static void noop_w(qa_q2_protocol_codec *self, qa_q2_sizebuf *msg) {
  (void)self;
  (void)msg;
}
static void noop_r(qa_q2_protocol_codec *self) { (void)self; }

static void write_frame(qa_q2_protocol_codec *self, qa_q2_sizebuf *msg,
                        const qa_q2_frame_write_params *params,
                        qa_q2_write_entities_fn write_entities, void *wctx) {
  (void)self;
  qa_q2_msg_write_byte(msg, QA_Q2_SVC_FRAME);
  double deltaframe = params->lastframe;
  int32_t offset = deltaframe == -1 ? 31 : qa_js_to_int32(params->framenum - deltaframe);
  int32_t encoded = (qa_js_to_int32(params->framenum) & 0x07ffffff) | (int32_t)((uint32_t)offset << 27);
  qa_q2_msg_write_long(msg, (double)encoded);
  qa_q2_msg_write_byte(msg, params->surpress_count != 0 ? 1 : 0);
  qa_q2_wire_player empty;
  qa_q2_wire_player_init(&empty);
  rr_enc e = encode_player(params->ps_from != NULL ? params->ps_from : &empty, params->ps_to);
  qa_q2_msg_write_byte(msg, (double)e.extraflags);
  qa_q2_msg_write_byte(msg, params->areabytes);
  qa_q2_sz_write(msg, params->areabits, params->areabits_length,
                 (int64_t)params->areabytes);
  qa_q2_msg_write_short(msg, (double)e.flags);
  write_player_body(&e, msg);
  write_entities(wctx, msg);
}

static void read_frame_header(qa_q2_protocol_codec *self, uint8_t *areabits,
                              size_t areabits_length, bool read_suppress_byte,
                              qa_q2_frame_header *out) {
  (void)read_suppress_byte;
  qa_q2repro_context *ctx = bind_of(self)->context;
  qa_q2_sizebuf *m = self->net_message;
  int32_t encoded = qa_q2_msg_read_long(m);
  uint32_t uoffset = (uint32_t)encoded >> 27;
  int32_t serverframe = encoded & 0x07ffffff;
  int32_t deltaframe = uoffset == 31u ? -1 : serverframe - (int32_t)uoffset;
  int32_t frame_flags = qa_q2_msg_read_byte(m) & 0x0f;
  ctx->pending_frame_extraflags = qa_q2_msg_read_byte(m);
  int32_t len = qa_q2_msg_read_byte(m);
  qa_q2_msg_read_data(m, areabits, areabits_length, len);
  out->serverframe = (double)serverframe;
  out->deltaframe = (double)deltaframe;
  out->surpress_count = (frame_flags & 1) != 0 ? 1 : 0;
  out->areabytes = (double)len;
}

static void read_frame_playerstate(qa_q2_protocol_codec *self,
                                   const qa_q2_wire_player *from,
                                   qa_q2_wire_player *to) {
  qa_q2repro_context *ctx = bind_of(self)->context;
  int32_t flags = qa_q2_msg_read_word(self->net_message);
  read_player_fields(self->net_message, from, to, flags, ctx->pending_frame_extraflags);
}

void qa_q2repro_read_fog(qa_q2repro_context *ctx, qa_q2_svc_fog_data *out) {
  qa_q2_sizebuf *m = ctx->net_message;
  int32_t bits = qa_q2_msg_read_byte(m);
  if ((bits & QA_Q2_SVC_FOG_BIT_MORE_BITS) != 0) bits |= (int32_t)((uint32_t)qa_q2_msg_read_byte(m) << 8);
  qa_q2_svc_fog_data_init(out);
  out->bits = (double)bits;
  if (bits & QA_Q2_SVC_FOG_BIT_DENSITY) {
    out->density = qa_q2_msg_read_float(m);
    out->skyfactor = (double)qa_q2_msg_read_byte(m);
  }
  if (bits & QA_Q2_SVC_FOG_BIT_R) out->red = (double)qa_q2_msg_read_byte(m);
  if (bits & QA_Q2_SVC_FOG_BIT_G) out->green = (double)qa_q2_msg_read_byte(m);
  if (bits & QA_Q2_SVC_FOG_BIT_B) out->blue = (double)qa_q2_msg_read_byte(m);
  if (bits & QA_Q2_SVC_FOG_BIT_TIME) out->time = (double)qa_q2_msg_read_word(m);
  if (bits & QA_Q2_SVC_FOG_BIT_HEIGHTFOG_FALLOFF) out->hf_falloff = qa_q2_msg_read_float(m);
  if (bits & QA_Q2_SVC_FOG_BIT_HEIGHTFOG_DENSITY) out->hf_density = qa_q2_msg_read_float(m);
  if (bits & QA_Q2_SVC_FOG_BIT_HEIGHTFOG_START_R) out->hf_start_r = (double)qa_q2_msg_read_byte(m);
  if (bits & QA_Q2_SVC_FOG_BIT_HEIGHTFOG_START_G) out->hf_start_g = (double)qa_q2_msg_read_byte(m);
  if (bits & QA_Q2_SVC_FOG_BIT_HEIGHTFOG_START_B) out->hf_start_b = (double)qa_q2_msg_read_byte(m);
  if (bits & QA_Q2_SVC_FOG_BIT_HEIGHTFOG_START_DIST) out->hf_start_dist = (double)qa_q2_msg_read_long(m);
  if (bits & QA_Q2_SVC_FOG_BIT_HEIGHTFOG_END_R) out->hf_end_r = (double)qa_q2_msg_read_byte(m);
  if (bits & QA_Q2_SVC_FOG_BIT_HEIGHTFOG_END_G) out->hf_end_g = (double)qa_q2_msg_read_byte(m);
  if (bits & QA_Q2_SVC_FOG_BIT_HEIGHTFOG_END_B) out->hf_end_b = (double)qa_q2_msg_read_byte(m);
  if (bits & QA_Q2_SVC_FOG_BIT_HEIGHTFOG_END_DIST) out->hf_end_dist = (double)qa_q2_msg_read_long(m);
}

static void f_remove(qa_q2_protocol_codec *self, qa_q2_sizebuf *msg, double oldnum) {
  qa_q2repro_context *ctx = bind_of(self)->context;
  ctx->vanilla.codec.write_entity_remove(&ctx->vanilla.codec, msg, oldnum);
}
static void f_end(qa_q2_protocol_codec *self, qa_q2_sizebuf *msg) {
  qa_q2repro_context *ctx = bind_of(self)->context;
  ctx->vanilla.codec.write_packet_entities_end(&ctx->vanilla.codec, msg);
}
static void write_spawn_baseline(qa_q2_protocol_codec *self, qa_q2_sizebuf *msg,
                                 const qa_q2_wire_entity *base) {
  qa_q2_wire_entity null_state;
  qa_q2_wire_entity_init(&null_state);
  qa_q2_msg_write_byte(msg, QA_Q2_SVC_SPAWNBASELINE);
  write_delta_entity(self, msg, &null_state, base, true, true);
}

static void fill_codec(qa_q2_protocol_codec *codec, qa_q2_sizebuf *m,
                       qa_q2repro_binding *binding, bool classic) {
  memset(codec, 0, sizeof *codec);
  codec->name = classic ? "q2repro-classic" : "q2repro";
  codec->net_message = m;
  codec->ctx = binding;
  codec->write_server_data = classic ? w_server_data_classic : w_server_data;
  codec->write_delta_entity = write_delta_entity;
  codec->write_entity_remove = f_remove;
  codec->write_packet_entities_end = f_end;
  codec->write_spawn_baseline = write_spawn_baseline;
  codec->write_player_state_delta = write_player_state_delta;
  codec->write_packet_entities_begin = noop_w;
  codec->write_frame = write_frame;
  codec->write_delta_usercmd = classic ? w_usercmd_classic : w_usercmd;
  codec->read_delta_usercmd = classic ? r_usercmd_classic : r_usercmd;
  codec->read_server_data = read_server_data;
  codec->read_entity_bits = read_entity_bits;
  codec->read_delta_entity = read_delta_entity;
  codec->read_player_state_delta = read_player_state_delta;
  codec->read_frame_header = read_frame_header;
  codec->read_frame_playerstate = read_frame_playerstate;
  codec->read_packet_entities_begin = noop_r;
  codec->read_batch_move = classic ? r_batch_classic : r_batch;
  codec->write_batch_move = classic ? w_batch_classic : w_batch;
  codec->read_userinfo_delta = read_userinfo_delta;
  codec->read_client_setting = read_client_setting;
}

void qa_q2repro_context_init(qa_q2repro_context *ctx, qa_q2_sizebuf *net_message) {
  memset(ctx, 0, sizeof *ctx);
  ctx->net_message = net_message;
  qa_q2_vanilla_context_init(&ctx->vanilla, net_message);
  ctx->binding.context = ctx;
  ctx->binding.classic = false;
  ctx->classic_binding.context = ctx;
  ctx->classic_binding.classic = true;
  fill_codec(&ctx->codec, net_message, &ctx->binding, false);
  fill_codec(&ctx->classic_codec, net_message, &ctx->classic_binding, true);
}
