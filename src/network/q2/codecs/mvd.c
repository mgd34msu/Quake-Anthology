/* Port of src/network/q2/codecs/mvd.ts. GPL-2.0-or-later. */
#include "qa/network/q2/codecs/mvd.h"
#include "qa/network/common/js_number.h"
#include "qa/foundation/js.h"

#include <math.h>
#include <string.h>

#include "qa/network/q2/codecs/q2pro_fields.h"
#include "qa/network/q2/constants.h"

void qa_q2_msg_write_mvd_cmd(qa_q2_sizebuf *msg, double op, double extrabits) {
  int32_t v = qa_js_to_int32(op) |
              (int32_t)((uint32_t)qa_js_to_int32(extrabits) << QA_Q2_MVD_SVCMD_BITS);
  qa_q2_msg_write_byte(msg, (double)(v & 0xff));
}
void qa_q2_msg_read_mvd_cmd(qa_q2_sizebuf *msg, qa_q2_mvd_cmd *out) {
  int32_t byte = qa_q2_msg_read_byte(msg);
  out->op = (double)(byte & QA_Q2_MVD_SVCMD_MASK);
  out->extrabits = (double)((uint32_t)(byte & 0xff) >> QA_Q2_MVD_SVCMD_BITS);
}
bool qa_q2_msg_valid_mvd_client_number(double number) {
  return number >= 0 && number < QA_Q2_MVD_CLIENTNUM_NONE;
}
void qa_q2_msg_write_mvd_players_end(qa_q2_sizebuf *msg) {
  qa_q2_msg_write_byte(msg, QA_Q2_MVD_CLIENTNUM_NONE);
}

static bool vec3_diff(const float a[3], const float b[3]) {
  return a[0] != b[0] || a[1] != b[1] || a[2] != b[2];
}
static bool vec4_diff(const float a[4], const float b[4]) {
  return a[0] != b[0] || a[1] != b[1] || a[2] != b[2] || a[3] != b[3];
}

void qa_q2_msg_write_delta_mvd_playerstate(qa_q2_sizebuf *msg,
                                           const qa_q2_wire_player *from_or_null,
                                           const qa_q2_wire_player *to_or_null,
                                           double number, bool force) {
  if (!qa_q2_msg_valid_mvd_client_number(number)) {
    qa_q2_throw_error("MSG_WriteDeltaMvdPlayerstate: bad number: %s", QA_JS_NUMBER(number));
  }
  if (to_or_null == NULL) {
    qa_q2_msg_write_byte(msg, number);
    qa_q2_msg_write_short(msg, QA_Q2_PPS_MOREBITS);
    return;
  }
  const qa_q2_wire_player *to = to_or_null;
  qa_q2_wire_player empty;
  qa_q2_wire_player_init(&empty);
  const qa_q2_wire_player *f = from_or_null != NULL ? from_or_null : &empty;
  int32_t pflags = 0;
  if (to->pmove.pm_type != f->pmove.pm_type) pflags |= QA_Q2_PPS_M_TYPE;
  if (to->pmove.origin[0] != f->pmove.origin[0] || to->pmove.origin[1] != f->pmove.origin[1]) pflags |= QA_Q2_PPS_M_ORIGIN;
  if (to->pmove.origin[2] != f->pmove.origin[2]) pflags |= QA_Q2_PPS_M_ORIGIN2;
  if (vec3_diff(to->viewoffset, f->viewoffset)) pflags |= QA_Q2_PPS_VIEWOFFSET;
  int32_t to_yaw[3] = {qa_q2_angle2short((double)to->viewangles[0]),
                       qa_q2_angle2short((double)to->viewangles[1]),
                       qa_q2_angle2short((double)to->viewangles[2])};
  int32_t from_yaw[3] = {qa_q2_angle2short((double)f->viewangles[0]),
                         qa_q2_angle2short((double)f->viewangles[1]),
                         qa_q2_angle2short((double)f->viewangles[2])};
  if (to_yaw[0] != from_yaw[0] || to_yaw[1] != from_yaw[1]) pflags |= QA_Q2_PPS_VIEWANGLES;
  if (to_yaw[2] != from_yaw[2]) pflags |= QA_Q2_PPS_VIEWANGLE2;
  if (vec3_diff(to->kick_angles, f->kick_angles)) pflags |= QA_Q2_PPS_KICKANGLES;
  if (vec4_diff(to->blend, f->blend)) pflags |= QA_Q2_PPS_BLEND;
  if (to->fov != f->fov) pflags |= QA_Q2_PPS_FOV;
  if (to->rdflags != f->rdflags) pflags |= QA_Q2_PPS_RDFLAGS;
  if (to->gunindex != f->gunindex) pflags |= QA_Q2_PPS_WEAPONINDEX;
  if (to->gunframe != f->gunframe) pflags |= QA_Q2_PPS_WEAPONFRAME;
  if (vec3_diff(to->gunoffset, f->gunoffset)) pflags |= QA_Q2_PPS_GUNOFFSET;
  if (vec3_diff(to->gunangles, f->gunangles)) pflags |= QA_Q2_PPS_GUNANGLES;
  int32_t statbits = 0;
  for (int i = 0; i < QA_Q2_MVD_MAX_STATS_OLD; i++) {
    if (to->stats[i] != f->stats[i]) statbits |= (int32_t)(1u << i);
  }
  if (statbits != 0) pflags |= QA_Q2_PPS_STATS;
  if (pflags == 0 && !force) return;
  qa_q2_msg_write_byte(msg, number);
  qa_q2_msg_write_short(msg, (double)(pflags & 0xffff));
  if (pflags & QA_Q2_PPS_M_TYPE) qa_q2_msg_write_byte(msg, to->pmove.pm_type);
  if (pflags & QA_Q2_PPS_M_ORIGIN) {
    qa_q2_msg_write_short(msg, (double)to->pmove.origin[0]);
    qa_q2_msg_write_short(msg, (double)to->pmove.origin[1]);
  }
  if (pflags & QA_Q2_PPS_M_ORIGIN2) qa_q2_msg_write_short(msg, (double)to->pmove.origin[2]);
  if (pflags & QA_Q2_PPS_VIEWOFFSET) {
    for (int i = 0; i < 3; i++) qa_q2_msg_write_char(msg, trunc((double)to->viewoffset[i] * 4.0));
  }
  if (pflags & QA_Q2_PPS_VIEWANGLES) {
    qa_q2_msg_write_short(msg, (double)to_yaw[0]);
    qa_q2_msg_write_short(msg, (double)to_yaw[1]);
  }
  if (pflags & QA_Q2_PPS_VIEWANGLE2) qa_q2_msg_write_short(msg, (double)to_yaw[2]);
  if (pflags & QA_Q2_PPS_KICKANGLES) {
    for (int i = 0; i < 3; i++) qa_q2_msg_write_char(msg, trunc((double)to->kick_angles[i] * 4.0));
  }
  if (pflags & QA_Q2_PPS_WEAPONINDEX) qa_q2_msg_write_byte(msg, to->gunindex);
  if (pflags & QA_Q2_PPS_WEAPONFRAME) qa_q2_msg_write_byte(msg, to->gunframe);
  if (pflags & QA_Q2_PPS_GUNOFFSET) {
    for (int i = 0; i < 3; i++) qa_q2_msg_write_char(msg, trunc((double)to->gunoffset[i] * 4.0));
  }
  if (pflags & QA_Q2_PPS_GUNANGLES) {
    for (int i = 0; i < 3; i++) qa_q2_msg_write_char(msg, trunc((double)to->gunangles[i] * 4.0));
  }
  if (pflags & QA_Q2_PPS_BLEND) {
    for (int i = 0; i < 4; i++) qa_q2_msg_write_byte(msg, trunc((double)to->blend[i] * 255.0));
  }
  if (pflags & QA_Q2_PPS_FOV) qa_q2_msg_write_byte(msg, to->fov);
  if (pflags & QA_Q2_PPS_RDFLAGS) qa_q2_msg_write_byte(msg, to->rdflags);
  if (pflags & QA_Q2_PPS_STATS) {
    qa_q2_msg_write_long(msg, (double)statbits);
    for (int i = 0; i < QA_Q2_MVD_MAX_STATS_OLD; i++) {
      if (statbits & (int32_t)(1u << i)) qa_q2_msg_write_short(msg, (double)to->stats[i]);
    }
  }
}

static void copy_mvd_from(qa_q2_wire_player *ps, const qa_q2_wire_player *from,
                          bool rerelease) {
  ps->pmove.pm_type = from->pmove.pm_type;
  memcpy(ps->pmove.origin, from->pmove.origin, sizeof ps->pmove.origin);
  memcpy(ps->viewoffset, from->viewoffset, sizeof ps->viewoffset);
  memcpy(ps->viewangles, from->viewangles, sizeof ps->viewangles);
  memcpy(ps->kick_angles, from->kick_angles, sizeof ps->kick_angles);
  memcpy(ps->gunangles, from->gunangles, sizeof ps->gunangles);
  memcpy(ps->gunoffset, from->gunoffset, sizeof ps->gunoffset);
  ps->gunindex = from->gunindex;
  if (rerelease) ps->gunskin = from->gunskin;
  ps->gunframe = from->gunframe;
  memcpy(ps->blend, from->blend, sizeof ps->blend);
  if (rerelease) memcpy(ps->damage_blend, from->damage_blend, sizeof ps->damage_blend);
  ps->fov = from->fov;
  ps->rdflags = from->rdflags;
  memcpy(ps->stats, from->stats, sizeof ps->stats);
}

void qa_q2_msg_read_delta_mvd_playerstate_body(
    qa_q2_sizebuf *msg, const qa_q2_wire_player *from_or_null, double number,
    qa_q2_mvd_player_result *out) {
  int32_t pflags = qa_q2_msg_read_short(msg) & 0xffff;
  qa_q2_wire_player_init(&out->ps);
  if (from_or_null != NULL) copy_mvd_from(&out->ps, from_or_null, false);
  out->number = number;
  if (pflags == QA_Q2_PPS_MOREBITS) {
    out->removed = true;
    return;
  }
  out->removed = false;
  qa_q2_wire_player *ps = &out->ps;
  if (pflags & QA_Q2_PPS_M_TYPE) ps->pmove.pm_type = (double)qa_q2_msg_read_byte(msg);
  if (pflags & QA_Q2_PPS_M_ORIGIN) {
    ps->pmove.origin[0] = qa_q2_msg_read_short(msg);
    ps->pmove.origin[1] = qa_q2_msg_read_short(msg);
  }
  if (pflags & QA_Q2_PPS_M_ORIGIN2) ps->pmove.origin[2] = qa_q2_msg_read_short(msg);
  if (pflags & QA_Q2_PPS_VIEWOFFSET) {
    for (int i = 0; i < 3; i++) ps->viewoffset[i] = (float)((double)qa_q2_msg_read_char(msg) * 0.25);
  }
  if (pflags & QA_Q2_PPS_VIEWANGLES) {
    ps->viewangles[0] = (float)qa_q2_short2angle((double)qa_q2_msg_read_short(msg));
    ps->viewangles[1] = (float)qa_q2_short2angle((double)qa_q2_msg_read_short(msg));
  }
  if (pflags & QA_Q2_PPS_VIEWANGLE2) ps->viewangles[2] = (float)qa_q2_short2angle((double)qa_q2_msg_read_short(msg));
  if (pflags & QA_Q2_PPS_KICKANGLES) {
    for (int i = 0; i < 3; i++) ps->kick_angles[i] = (float)((double)qa_q2_msg_read_char(msg) * 0.25);
  }
  if (pflags & QA_Q2_PPS_WEAPONINDEX) ps->gunindex = (double)qa_q2_msg_read_byte(msg);
  if (pflags & QA_Q2_PPS_WEAPONFRAME) ps->gunframe = (double)qa_q2_msg_read_byte(msg);
  if (pflags & QA_Q2_PPS_GUNOFFSET) {
    for (int i = 0; i < 3; i++) ps->gunoffset[i] = (float)((double)qa_q2_msg_read_char(msg) * 0.25);
  }
  if (pflags & QA_Q2_PPS_GUNANGLES) {
    for (int i = 0; i < 3; i++) ps->gunangles[i] = (float)((double)qa_q2_msg_read_char(msg) * 0.25);
  }
  if (pflags & QA_Q2_PPS_BLEND) {
    for (int i = 0; i < 4; i++) ps->blend[i] = (float)((double)qa_q2_msg_read_byte(msg) / 255.0);
  }
  if (pflags & QA_Q2_PPS_FOV) ps->fov = (double)qa_q2_msg_read_byte(msg);
  if (pflags & QA_Q2_PPS_RDFLAGS) ps->rdflags = (double)qa_q2_msg_read_byte(msg);
  if (pflags & QA_Q2_PPS_STATS) {
    uint32_t statbits = (uint32_t)qa_q2_msg_read_long(msg);
    for (int i = 0; i < QA_Q2_MVD_MAX_STATS_OLD; i++) {
      if ((statbits & (1u << i)) != 0) ps->stats[i] = (int16_t)qa_q2_msg_read_short(msg);
    }
  }
}

void qa_q2_msg_read_delta_mvd_playerstate(qa_q2_sizebuf *msg,
                                          const qa_q2_wire_player *from_or_null,
                                          qa_q2_mvd_player_result *out) {
  double number = (double)qa_q2_msg_read_byte(msg);
  qa_q2_msg_read_delta_mvd_playerstate_body(msg, from_or_null, number, out);
}

/* TS numbers: Math.max/min keep NaN, so a NaN component compares changed
 * (NaN !== NaN) and its write stores ToInt32(NaN) = 0. */
static double scaled_short(double x, double scale) {
  double v = trunc(x * scale);
  if (v < -32768) v = -32768;
  if (v > 32767) v = 32767;
  return v;
}
static double blend_byte(float x) {
  double v = trunc((double)x * 255.0);
  if (v < 0) v = 0;
  if (v > 255) v = 255;
  return v;
}
static int32_t packed_gun_index(const qa_q2_wire_player *ps) {
  return (qa_js_to_int32(ps->gunindex) & 0x1fff) |
         ((qa_js_to_int32(ps->gunskin) & 0x7) << QA_Q2_MVD_GUNINDEX_BITS);
}
static bool blend_changed_rr(const qa_q2_wire_player *from,
                             const qa_q2_wire_player *to) {
  for (int i = 0; i < 4; i++) {
    if (blend_byte(to->blend[i]) != blend_byte(from->blend[i])) return true;
    if (blend_byte(to->damage_blend[i]) != blend_byte(from->damage_blend[i])) return true;
  }
  return false;
}
static void write_delta_blend(qa_q2_sizebuf *msg, const qa_q2_wire_player *from,
                              const qa_q2_wire_player *to) {
  int32_t bflags = 0;
  for (int i = 0; i < 4; i++) {
    if (blend_byte(to->blend[i]) != blend_byte(from->blend[i])) bflags |= 1 << i;
    if (blend_byte(to->damage_blend[i]) != blend_byte(from->damage_blend[i])) bflags |= 1 << (4 + i);
  }
  qa_q2_msg_write_byte(msg, (double)bflags);
  for (int i = 0; i < 4; i++) {
    if (bflags & (1 << i)) qa_q2_msg_write_byte(msg, blend_byte(to->blend[i]));
  }
  for (int i = 0; i < 4; i++) {
    if (bflags & (1 << (4 + i))) qa_q2_msg_write_byte(msg, blend_byte(to->damage_blend[i]));
  }
}
static void read_delta_blend(qa_q2_sizebuf *msg, qa_q2_wire_player *ps) {
  int32_t bflags = qa_q2_msg_read_byte(msg);
  for (int i = 0; i < 4; i++) {
    if (bflags & (1 << i)) ps->blend[i] = (float)((double)qa_q2_msg_read_byte(msg) / 255.0);
  }
  for (int i = 0; i < 4; i++) {
    if (bflags & (1 << (4 + i))) ps->damage_blend[i] = (float)((double)qa_q2_msg_read_byte(msg) / 255.0);
  }
}

void qa_q2_msg_write_delta_mvd_playerstate_rerelease(
    qa_q2_sizebuf *msg, const qa_q2_wire_player *from_or_null,
    const qa_q2_wire_player *to_or_null, double number, bool force) {
  if (!qa_q2_msg_valid_mvd_client_number(number)) {
    qa_q2_throw_error("MSG_WriteDeltaMvdPlayerstateRerelease: bad number: %s", QA_JS_NUMBER(number));
  }
  if (to_or_null == NULL) {
    qa_q2_msg_write_byte(msg, number);
    qa_q2_msg_write_short(msg, QA_Q2_PPS_MOREBITS);
    return;
  }
  const qa_q2_wire_player *to = to_or_null;
  qa_q2_wire_player empty;
  qa_q2_wire_player_init(&empty);
  const qa_q2_wire_player *f = from_or_null != NULL ? from_or_null : &empty;
  int32_t pflags = 0;
  if (to->pmove.pm_type != f->pmove.pm_type) pflags |= QA_Q2_PPS_M_TYPE;
  if (to->pmove.origin[0] != f->pmove.origin[0] || to->pmove.origin[1] != f->pmove.origin[1]) pflags |= QA_Q2_PPS_M_ORIGIN;
  if (to->pmove.origin[2] != f->pmove.origin[2]) pflags |= QA_Q2_PPS_M_ORIGIN2;
  double to_vo[3];
  double from_vo[3];
  double to_kick[3];
  double from_kick[3];
  double to_go[3];
  double from_go[3];
  double to_ga[3];
  double from_ga[3];
  for (int i = 0; i < 3; i++) {
    to_vo[i] = scaled_short((double)to->viewoffset[i], QA_Q2_MVD_RERELEASE_VIEWOFFSET_SCALE);
    from_vo[i] = scaled_short((double)f->viewoffset[i], QA_Q2_MVD_RERELEASE_VIEWOFFSET_SCALE);
    to_kick[i] = scaled_short((double)to->kick_angles[i], QA_Q2_MVD_RERELEASE_KICKANGLES_SCALE);
    from_kick[i] = scaled_short((double)f->kick_angles[i], QA_Q2_MVD_RERELEASE_KICKANGLES_SCALE);
    to_go[i] = scaled_short((double)to->gunoffset[i], QA_Q2_MVD_RERELEASE_GUNOFFSET_SCALE);
    from_go[i] = scaled_short((double)f->gunoffset[i], QA_Q2_MVD_RERELEASE_GUNOFFSET_SCALE);
    to_ga[i] = scaled_short((double)to->gunangles[i], QA_Q2_MVD_RERELEASE_GUNANGLES_SCALE);
    from_ga[i] = scaled_short((double)f->gunangles[i], QA_Q2_MVD_RERELEASE_GUNANGLES_SCALE);
  }
  if (to_vo[0] != from_vo[0] || to_vo[1] != from_vo[1] || to_vo[2] != from_vo[2]) pflags |= QA_Q2_PPS_VIEWOFFSET;
  int32_t to_yaw[3] = {qa_q2_angle2short((double)to->viewangles[0]),
                       qa_q2_angle2short((double)to->viewangles[1]),
                       qa_q2_angle2short((double)to->viewangles[2])};
  int32_t from_yaw[3] = {qa_q2_angle2short((double)f->viewangles[0]),
                         qa_q2_angle2short((double)f->viewangles[1]),
                         qa_q2_angle2short((double)f->viewangles[2])};
  if (to_yaw[0] != from_yaw[0] || to_yaw[1] != from_yaw[1]) pflags |= QA_Q2_PPS_VIEWANGLES;
  if (to_yaw[2] != from_yaw[2]) pflags |= QA_Q2_PPS_VIEWANGLE2;
  if (to_kick[0] != from_kick[0] || to_kick[1] != from_kick[1] || to_kick[2] != from_kick[2]) pflags |= QA_Q2_PPS_KICKANGLES;
  if (blend_changed_rr(f, to)) pflags |= QA_Q2_PPS_BLEND;
  if (to->fov != f->fov) pflags |= QA_Q2_PPS_FOV;
  if (to->rdflags != f->rdflags) pflags |= QA_Q2_PPS_RDFLAGS;
  int32_t to_gun = packed_gun_index(to);
  if (to_gun != packed_gun_index(f)) pflags |= QA_Q2_PPS_WEAPONINDEX;
  if (to->gunframe != f->gunframe) pflags |= QA_Q2_PPS_WEAPONFRAME;
  if (to_go[0] != from_go[0] || to_go[1] != from_go[1] || to_go[2] != from_go[2]) pflags |= QA_Q2_PPS_GUNOFFSET;
  if (to_ga[0] != from_ga[0] || to_ga[1] != from_ga[1] || to_ga[2] != from_ga[2]) pflags |= QA_Q2_PPS_GUNANGLES;
  uint64_t statbits = 0;
  for (int i = 0; i < QA_Q2_MVD_MAX_STATS_NEW; i++) {
    if (to->stats[i] != f->stats[i]) statbits |= 1ull << i;
  }
  if (statbits != 0) pflags |= QA_Q2_PPS_STATS;
  if (pflags == 0 && !force) return;
  qa_q2_msg_write_byte(msg, number);
  qa_q2_msg_write_short(msg, (double)(pflags & 0xffff));
  if (pflags & QA_Q2_PPS_M_TYPE) qa_q2_msg_write_byte(msg, to->pmove.pm_type);
  if (pflags & QA_Q2_PPS_M_ORIGIN) {
    qa_q2_msg_write_short(msg, (double)to->pmove.origin[0]);
    qa_q2_msg_write_short(msg, (double)to->pmove.origin[1]);
  }
  if (pflags & QA_Q2_PPS_M_ORIGIN2) qa_q2_msg_write_short(msg, (double)to->pmove.origin[2]);
  if (pflags & QA_Q2_PPS_VIEWOFFSET) {
    for (int i = 0; i < 3; i++) qa_q2_msg_write_short(msg, (double)to_vo[i]);
  }
  if (pflags & QA_Q2_PPS_VIEWANGLES) {
    qa_q2_msg_write_short(msg, (double)to_yaw[0]);
    qa_q2_msg_write_short(msg, (double)to_yaw[1]);
  }
  if (pflags & QA_Q2_PPS_VIEWANGLE2) qa_q2_msg_write_short(msg, (double)to_yaw[2]);
  if (pflags & QA_Q2_PPS_KICKANGLES) {
    for (int i = 0; i < 3; i++) qa_q2_msg_write_short(msg, (double)to_kick[i]);
  }
  if (pflags & QA_Q2_PPS_WEAPONINDEX) qa_q2_msg_write_short(msg, (double)to_gun);
  if (pflags & QA_Q2_PPS_WEAPONFRAME) qa_q2_msg_write_short(msg, to->gunframe);
  if (pflags & QA_Q2_PPS_GUNOFFSET) {
    for (int i = 0; i < 3; i++) qa_q2_msg_write_short(msg, (double)to_go[i]);
  }
  if (pflags & QA_Q2_PPS_GUNANGLES) {
    for (int i = 0; i < 3; i++) qa_q2_msg_write_short(msg, (double)to_ga[i]);
  }
  if (pflags & QA_Q2_PPS_BLEND) write_delta_blend(msg, f, to);
  if (pflags & QA_Q2_PPS_FOV) qa_q2_msg_write_byte(msg, to->fov);
  if (pflags & QA_Q2_PPS_RDFLAGS) qa_q2_msg_write_byte(msg, to->rdflags);
  if (pflags & QA_Q2_PPS_STATS) {
    int64_t signed_bits;
    memcpy(&signed_bits, &statbits, sizeof signed_bits);
    qa_q2_msg_write_long64(msg, signed_bits);
    for (int i = 0; i < QA_Q2_MVD_MAX_STATS_NEW; i++) {
      if ((statbits & (1ull << i)) != 0) qa_q2_msg_write_short(msg, (double)to->stats[i]);
    }
  }
}

void qa_q2_msg_read_delta_mvd_playerstate_rerelease_body(
    qa_q2_sizebuf *msg, const qa_q2_wire_player *from_or_null, double number,
    qa_q2_mvd_player_result *out) {
  int32_t pflags = qa_q2_msg_read_short(msg) & 0xffff;
  qa_q2_wire_player_init(&out->ps);
  if (from_or_null != NULL) copy_mvd_from(&out->ps, from_or_null, true);
  out->number = number;
  if (pflags == QA_Q2_PPS_MOREBITS) {
    out->removed = true;
    return;
  }
  out->removed = false;
  qa_q2_wire_player *ps = &out->ps;
  if (pflags & QA_Q2_PPS_M_TYPE) ps->pmove.pm_type = (double)qa_q2_msg_read_byte(msg);
  if (pflags & QA_Q2_PPS_M_ORIGIN) {
    ps->pmove.origin[0] = qa_q2_msg_read_short(msg);
    ps->pmove.origin[1] = qa_q2_msg_read_short(msg);
  }
  if (pflags & QA_Q2_PPS_M_ORIGIN2) ps->pmove.origin[2] = qa_q2_msg_read_short(msg);
  if (pflags & QA_Q2_PPS_VIEWOFFSET) {
    for (int i = 0; i < 3; i++) ps->viewoffset[i] = (float)((double)qa_q2_msg_read_short(msg) / QA_Q2_MVD_RERELEASE_VIEWOFFSET_SCALE);
  }
  if (pflags & QA_Q2_PPS_VIEWANGLES) {
    ps->viewangles[0] = (float)qa_q2_short2angle((double)qa_q2_msg_read_short(msg));
    ps->viewangles[1] = (float)qa_q2_short2angle((double)qa_q2_msg_read_short(msg));
  }
  if (pflags & QA_Q2_PPS_VIEWANGLE2) ps->viewangles[2] = (float)qa_q2_short2angle((double)qa_q2_msg_read_short(msg));
  if (pflags & QA_Q2_PPS_KICKANGLES) {
    for (int i = 0; i < 3; i++) ps->kick_angles[i] = (float)((double)qa_q2_msg_read_short(msg) / QA_Q2_MVD_RERELEASE_KICKANGLES_SCALE);
  }
  if (pflags & QA_Q2_PPS_WEAPONINDEX) {
    int32_t packed = qa_q2_msg_read_short(msg) & 0xffff;
    ps->gunindex = (double)(packed & 0x1fff);
    ps->gunskin = (double)(((uint32_t)packed >> QA_Q2_MVD_GUNINDEX_BITS) & 0x7u);
  }
  if (pflags & QA_Q2_PPS_WEAPONFRAME) ps->gunframe = (double)qa_q2_msg_read_word(msg);
  if (pflags & QA_Q2_PPS_GUNOFFSET) {
    for (int i = 0; i < 3; i++) ps->gunoffset[i] = (float)((double)qa_q2_msg_read_short(msg) / QA_Q2_MVD_RERELEASE_GUNOFFSET_SCALE);
  }
  if (pflags & QA_Q2_PPS_GUNANGLES) {
    for (int i = 0; i < 3; i++) ps->gunangles[i] = (float)((double)qa_q2_msg_read_short(msg) / QA_Q2_MVD_RERELEASE_GUNANGLES_SCALE);
  }
  if (pflags & QA_Q2_PPS_BLEND) read_delta_blend(msg, ps);
  if (pflags & QA_Q2_PPS_FOV) ps->fov = (double)qa_q2_msg_read_byte(msg);
  if (pflags & QA_Q2_PPS_RDFLAGS) ps->rdflags = (double)qa_q2_msg_read_byte(msg);
  if (pflags & QA_Q2_PPS_STATS) {
    int64_t sb = qa_q2_msg_read_long64(msg);
    uint64_t statbits;
    memcpy(&statbits, &sb, sizeof statbits);
    for (int i = 0; i < QA_Q2_MVD_MAX_STATS_NEW; i++) {
      if ((statbits & (1ull << i)) != 0) {
        int32_t value = qa_q2_msg_read_short(msg);
        if (i < QA_Q2_MAX_STATS_STORAGE) ps->stats[i] = (int16_t)value;
      }
    }
  }
}

void qa_q2_msg_read_delta_mvd_playerstate_rerelease(
    qa_q2_sizebuf *msg, const qa_q2_wire_player *from_or_null,
    qa_q2_mvd_player_result *out) {
  double number = (double)qa_q2_msg_read_byte(msg);
  qa_q2_msg_read_delta_mvd_playerstate_rerelease_body(msg, from_or_null, number, out);
}

void qa_q2_mvd_read_player(qa_q2_sizebuf *message,
                           const qa_q2_wire_player *from_or_null, double number,
                           qa_q2_mvd_profile profile,
                           qa_q2_mvd_player_result *out) {
  if (!profile.extended) {
    qa_q2_msg_read_delta_mvd_playerstate_body(message, from_or_null, number, out);
    return;
  }
  int32_t flags = qa_q2_msg_read_word(message);
  if ((flags & QA_Q2_PPS_MOREBITS) != 0) {
    if (profile.fog) {
      /* JS <<: a truncated read (-1) sets every high bit instead of overflowing. */
      flags |= (int32_t)((uint32_t)qa_q2_msg_read_byte(message) << 16);
    } else {
      out->number = number;
      out->removed = true;
      if (from_or_null != NULL) out->ps = *from_or_null;
      else qa_q2_wire_player_init(&out->ps);
      return;
    }
  }
  qa_q2_wire_player_init(&out->ps);
  qa_q2_wire_player *ps = &out->ps;
  if (from_or_null != NULL) {
    const qa_q2_wire_player *from = from_or_null;
    ps->pmove.pm_type = from->pmove.pm_type;
    memcpy(ps->pmove.origin, from->pmove.origin, sizeof ps->pmove.origin);
    memcpy(ps->pmove.origin_f, from->pmove.origin_f, sizeof ps->pmove.origin_f);
    memcpy(ps->viewoffset, from->viewoffset, sizeof ps->viewoffset);
    memcpy(ps->viewangles, from->viewangles, sizeof ps->viewangles);
    memcpy(ps->kick_angles, from->kick_angles, sizeof ps->kick_angles);
    memcpy(ps->gunoffset, from->gunoffset, sizeof ps->gunoffset);
    memcpy(ps->gunangles, from->gunangles, sizeof ps->gunangles);
    ps->gunindex = from->gunindex;
    ps->gunskin = from->gunskin;
    ps->gunframe = from->gunframe;
    memcpy(ps->blend, from->blend, sizeof ps->blend);
    memcpy(ps->damage_blend, from->damage_blend, sizeof ps->damage_blend);
    memcpy(ps->stats, from->stats, sizeof ps->stats);
    ps->fov = from->fov;
    ps->rdflags = from->rdflags;
    ps->q2pro_fog = from->q2pro_fog;
  }
  if (flags & QA_Q2_PPS_M_TYPE) ps->pmove.pm_type = (double)qa_q2_msg_read_byte(message);
  for (int axis = 0; axis < 3; axis++) {
    int32_t bit = axis == 2 ? QA_Q2_PPS_M_ORIGIN2 : QA_Q2_PPS_M_ORIGIN;
    if ((flags & bit) != 0) {
      if (profile.rerelease) ps->pmove.origin_f[axis] = (float)qa_q2_msg_read_float(message);
      else ps->pmove.origin[axis] = profile.v2 ? qa_q2pro_read_int23(message, ps->pmove.origin[axis]) : qa_q2_msg_read_short(message);
    }
  }
  if (flags & QA_Q2_PPS_VIEWOFFSET) {
    double scale = profile.rerelease ? 16 : 4;
    for (int i = 0; i < 3; i++) {
      double raw = profile.rerelease ? (double)qa_q2_msg_read_short(message) : (double)qa_q2_msg_read_char(message);
      ps->viewoffset[i] = (float)(raw / scale);
    }
  }
  if (flags & QA_Q2_PPS_VIEWANGLES) {
    ps->viewangles[0] = (float)qa_q2_short2angle((double)qa_q2_msg_read_short(message));
    ps->viewangles[1] = (float)qa_q2_short2angle((double)qa_q2_msg_read_short(message));
  }
  if (flags & QA_Q2_PPS_VIEWANGLE2) ps->viewangles[2] = (float)qa_q2_short2angle((double)qa_q2_msg_read_short(message));
  if (flags & QA_Q2_PPS_KICKANGLES) {
    double scale = profile.rerelease ? 1024 : 4;
    for (int i = 0; i < 3; i++) {
      double raw = profile.rerelease ? (double)qa_q2_msg_read_short(message) : (double)qa_q2_msg_read_char(message);
      ps->kick_angles[i] = (float)(raw / scale);
    }
  }
  if (flags & QA_Q2_PPS_WEAPONINDEX) {
    int32_t packed = qa_q2_msg_read_word(message);
    ps->gunindex = (double)(packed & 8191);
    ps->gunskin = (double)((uint32_t)packed >> 13);
  }
  if (flags & QA_Q2_PPS_WEAPONFRAME) {
    ps->gunframe = profile.rerelease ? (double)qa_q2_msg_read_word(message)
                                      : (double)qa_q2_msg_read_byte(message);
  }
  if (flags & QA_Q2_PPS_GUNOFFSET) {
    double scale = profile.rerelease ? 512 : 8;
    for (int i = 0; i < 3; i++) ps->gunoffset[i] = (float)((double)qa_q2_msg_read_short(message) / scale);
  }
  if (flags & QA_Q2_PPS_GUNANGLES) {
    double scale = profile.rerelease ? 4096 : 65536.0 / 360.0;
    for (int i = 0; i < 3; i++) ps->gunangles[i] = (float)((double)qa_q2_msg_read_short(message) / scale);
  }
  if (flags & QA_Q2_PPS_BLEND) {
    if (profile.rerelease || profile.v2) read_delta_blend(message, ps);
    else {
      for (int i = 0; i < 4; i++) ps->blend[i] = (float)((double)qa_q2_msg_read_byte(message) / 255.0);
    }
  }
  if ((flags & (1 << 17)) != 0) qa_q2pro_read_fog(message, &ps->q2pro_fog, &ps->q2pro_fog);
  if (flags & QA_Q2_PPS_FOV) ps->fov = (double)qa_q2_msg_read_byte(message);
  if (flags & QA_Q2_PPS_RDFLAGS) ps->rdflags = (double)qa_q2_msg_read_byte(message);
  if (flags & QA_Q2_PPS_STATS) {
    uint64_t bits;
    if (profile.rerelease) {
      int64_t sb = qa_q2_msg_read_long64(message);
      memcpy(&bits, &sb, sizeof bits);
    } else if (profile.v2) {
      bits = qa_q2pro_read_var64(message);
    } else {
      bits = (uint32_t)qa_q2_msg_read_long(message);
    }
    int n = profile.rerelease || profile.v2 ? 64 : 32;
    for (int i = 0; i < n; i++) {
      if ((bits & (1ull << i)) != 0 && i < QA_Q2_MAX_STATS_STORAGE) {
        ps->stats[i] = (int16_t)qa_q2_msg_read_short(message);
      } else if ((bits & (1ull << i)) != 0) {
        (void)qa_q2_msg_read_short(message);
      }
    }
  }
  out->number = number;
  out->removed = (flags & (1 << 16)) != 0;
}
