/* Port of src/network/q2/codecs/r1q2.ts. GPL-2.0-or-later. */
#include "qa/network/q2/codecs/r1q2.h"

#include <math.h>
#include <string.h>

#include "qa/network/q2/constants.h"

static qa_q2_r1_context *ctx_of(qa_q2_protocol_codec *self) {
  return (qa_q2_r1_context *)self->ctx;
}
static bool long_solid(qa_q2_r1_context *ctx) {
  return ctx->minor_version >= QA_Q2_PROTOCOL_VERSION_R1Q2_LONG_SOLID;
}
static bool compressed_moves(qa_q2_r1_context *ctx) {
  return ctx->minor_version >= QA_Q2_PROTOCOL_VERSION_R1Q2_UCMD;
}

static void write_server_data(qa_q2_protocol_codec *self, qa_q2_sizebuf *msg,
                              const qa_q2_server_data_params *params) {
  (void)self;
  qa_q2_msg_write_byte(msg, QA_Q2_SVC_SERVERDATA);
  qa_q2_msg_write_long(msg, QA_Q2_PROTOCOL_VERSION_R1Q2);
  qa_q2_msg_write_long(msg, params->servercount);
  qa_q2_msg_write_byte(msg, params->attractloop ? 1 : 0);
  qa_q2_msg_write_string(msg, params->gamedir);
  qa_q2_msg_write_short(msg, params->clientnum);
  qa_q2_msg_write_string(msg, params->levelname);
  qa_q2_msg_write_byte(msg, 0);
  qa_q2_msg_write_short(msg, params->has_r1q2_version
                                 ? params->r1q2_version
                                 : QA_Q2_PROTOCOL_VERSION_R1Q2_CURRENT);
  qa_q2_msg_write_byte(msg, 0);
  qa_q2_msg_write_byte(msg, params->has_r1q2_strafejump_hack &&
                                    params->r1q2_strafejump_hack
                                ? 1
                                : 0);
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
  (void)qa_q2_msg_read_byte(m);
  out->has_r1q2_version = true;
  out->r1q2_version = (double)qa_q2_msg_read_short(m);
  (void)qa_q2_msg_read_byte(m);
  out->has_r1q2_strafejump_hack = true;
  out->r1q2_strafejump_hack = qa_q2_msg_read_byte(m) != 0;
  out->server_state = 0;
}

static void write_delta_entity(qa_q2_protocol_codec *self, qa_q2_sizebuf *msg,
                               const qa_q2_wire_entity *from,
                               const qa_q2_wire_entity *to, bool force,
                               bool newentity) {
  qa_q2_r1_context *ctx = ctx_of(self);
  if (to->number == 0) qa_q2_com_error_throw(QA_Q2_ERR_FATAL, "Unset entity number");
  if (to->number >= QA_Q2_MAX_EDICTS) {
    qa_q2_com_error_throw(QA_Q2_ERR_FATAL, "Entity number >= MAX_EDICTS");
  }
  int32_t bits = 0;
  if (to->number >= 256) bits |= QA_Q2_U_NUMBER16;
  if (to->origin[0] != from->origin[0]) bits |= QA_Q2_U_ORIGIN1;
  if (to->origin[1] != from->origin[1]) bits |= QA_Q2_U_ORIGIN2;
  if (to->origin[2] != from->origin[2]) bits |= QA_Q2_U_ORIGIN3;
  if (to->angles[0] != from->angles[0]) bits |= QA_Q2_U_ANGLE1;
  if (to->angles[1] != from->angles[1]) bits |= QA_Q2_U_ANGLE2;
  if (to->angles[2] != from->angles[2]) bits |= QA_Q2_U_ANGLE3;
  if (to->skinnum != from->skinnum) {
    uint32_t skin = (uint32_t)(int32_t)to->skinnum;
    if (skin < 256u) bits |= QA_Q2_U_SKIN8;
    else if (skin < 0x8000u) bits |= QA_Q2_U_SKIN16;
    else bits |= QA_Q2_U_SKIN8 | QA_Q2_U_SKIN16;
  }
  if (to->frame != from->frame) bits |= to->frame < 256 ? QA_Q2_U_FRAME8 : QA_Q2_U_FRAME16;
  if (to->effects != from->effects) {
    if (to->effects < 256) bits |= QA_Q2_U_EFFECTS8;
    else if (to->effects < 0x8000) bits |= QA_Q2_U_EFFECTS16;
    else bits |= QA_Q2_U_EFFECTS8 | QA_Q2_U_EFFECTS16;
  }
  if (to->renderfx != from->renderfx) {
    if (to->renderfx < 256) bits |= QA_Q2_U_RENDERFX8;
    else if (to->renderfx < 0x8000) bits |= QA_Q2_U_RENDERFX16;
    else bits |= QA_Q2_U_RENDERFX8 | QA_Q2_U_RENDERFX16;
  }
  if (to->solid != from->solid) bits |= QA_Q2_U_SOLID;
  if (to->event != 0) bits |= QA_Q2_U_EVENT;
  if (to->modelindex != from->modelindex) bits |= QA_Q2_U_MODEL;
  if (to->modelindex2 != from->modelindex2) bits |= QA_Q2_U_MODEL2;
  if (to->modelindex3 != from->modelindex3) bits |= QA_Q2_U_MODEL3;
  if (to->modelindex4 != from->modelindex4) bits |= QA_Q2_U_MODEL4;
  if (to->sound != from->sound) bits |= QA_Q2_U_SOUND;
  if (newentity || ((int32_t)to->renderfx & QA_Q2_RF_BEAM) != 0) bits |= QA_Q2_U_OLDORIGIN;
  if (bits == 0 && !force) return;
  if ((bits & (int32_t)0xff000000u) != 0) bits |= QA_Q2_U_MOREBITS3 | QA_Q2_U_MOREBITS2 | QA_Q2_U_MOREBITS1;
  else if ((bits & 0x00ff0000) != 0) bits |= QA_Q2_U_MOREBITS2 | QA_Q2_U_MOREBITS1;
  else if ((bits & 0x0000ff00) != 0) bits |= QA_Q2_U_MOREBITS1;
  qa_q2_msg_write_byte(msg, (double)(bits & 255));
  if ((bits & (int32_t)0xff000000u) != 0) {
    qa_q2_msg_write_byte(msg, (double)((bits >> 8) & 255));
    qa_q2_msg_write_byte(msg, (double)((bits >> 16) & 255));
    qa_q2_msg_write_byte(msg, (double)((bits >> 24) & 255));
  } else if ((bits & 0x00ff0000) != 0) {
    qa_q2_msg_write_byte(msg, (double)((bits >> 8) & 255));
    qa_q2_msg_write_byte(msg, (double)((bits >> 16) & 255));
  } else if ((bits & 0x0000ff00) != 0) {
    qa_q2_msg_write_byte(msg, (double)((bits >> 8) & 255));
  }
  if (bits & QA_Q2_U_NUMBER16) qa_q2_msg_write_short(msg, to->number);
  else qa_q2_msg_write_byte(msg, to->number);
  if (bits & QA_Q2_U_MODEL) qa_q2_msg_write_byte(msg, to->modelindex);
  if (bits & QA_Q2_U_MODEL2) qa_q2_msg_write_byte(msg, to->modelindex2);
  if (bits & QA_Q2_U_MODEL3) qa_q2_msg_write_byte(msg, to->modelindex3);
  if (bits & QA_Q2_U_MODEL4) qa_q2_msg_write_byte(msg, to->modelindex4);
  if (bits & QA_Q2_U_FRAME8) qa_q2_msg_write_byte(msg, to->frame);
  if (bits & QA_Q2_U_FRAME16) qa_q2_msg_write_short(msg, to->frame);
  if ((bits & QA_Q2_U_SKIN8) != 0 && (bits & QA_Q2_U_SKIN16) != 0) qa_q2_msg_write_long(msg, to->skinnum);
  else if (bits & QA_Q2_U_SKIN8) qa_q2_msg_write_byte(msg, to->skinnum);
  else if (bits & QA_Q2_U_SKIN16) qa_q2_msg_write_short(msg, to->skinnum);
  if ((bits & (QA_Q2_U_EFFECTS8 | QA_Q2_U_EFFECTS16)) == (QA_Q2_U_EFFECTS8 | QA_Q2_U_EFFECTS16)) qa_q2_msg_write_long(msg, to->effects);
  else if (bits & QA_Q2_U_EFFECTS8) qa_q2_msg_write_byte(msg, to->effects);
  else if (bits & QA_Q2_U_EFFECTS16) qa_q2_msg_write_short(msg, to->effects);
  if ((bits & (QA_Q2_U_RENDERFX8 | QA_Q2_U_RENDERFX16)) == (QA_Q2_U_RENDERFX8 | QA_Q2_U_RENDERFX16)) qa_q2_msg_write_long(msg, to->renderfx);
  else if (bits & QA_Q2_U_RENDERFX8) qa_q2_msg_write_byte(msg, to->renderfx);
  else if (bits & QA_Q2_U_RENDERFX16) qa_q2_msg_write_short(msg, to->renderfx);
  if (bits & QA_Q2_U_ORIGIN1) qa_q2_msg_write_coord(msg, (double)to->origin[0]);
  if (bits & QA_Q2_U_ORIGIN2) qa_q2_msg_write_coord(msg, (double)to->origin[1]);
  if (bits & QA_Q2_U_ORIGIN3) qa_q2_msg_write_coord(msg, (double)to->origin[2]);
  if (bits & QA_Q2_U_ANGLE1) qa_q2_msg_write_angle(msg, (double)to->angles[0]);
  if (bits & QA_Q2_U_ANGLE2) qa_q2_msg_write_angle(msg, (double)to->angles[1]);
  if (bits & QA_Q2_U_ANGLE3) qa_q2_msg_write_angle(msg, (double)to->angles[2]);
  if (bits & QA_Q2_U_OLDORIGIN) {
    qa_q2_msg_write_coord(msg, (double)to->old_origin[0]);
    qa_q2_msg_write_coord(msg, (double)to->old_origin[1]);
    qa_q2_msg_write_coord(msg, (double)to->old_origin[2]);
  }
  if (bits & QA_Q2_U_SOUND) qa_q2_msg_write_byte(msg, to->sound);
  if (bits & QA_Q2_U_EVENT) qa_q2_msg_write_byte(msg, to->event);
  if (bits & QA_Q2_U_SOLID) {
    if (long_solid(ctx)) qa_q2_msg_write_long(msg, to->solid);
    else qa_q2_msg_write_short(msg, to->solid);
  }
}

static void read_delta_entity(qa_q2_protocol_codec *self,
                              const qa_q2_wire_entity *from,
                              qa_q2_wire_entity *to, double number, uint64_t bits64) {
  qa_q2_r1_context *ctx = ctx_of(self);
  qa_q2_sizebuf *m = self->net_message;
  int32_t bits = (int32_t)bits64;
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
  to->renderfx = from->renderfx;
  to->solid = from->solid;
  to->sound = from->sound;
  to->event = from->event;
  qa_q2_vector_copy(from->origin, to->old_origin);
  to->number = number;
  if (bits & QA_Q2_U_MODEL) to->modelindex = (double)qa_q2_msg_read_byte(m);
  if (bits & QA_Q2_U_MODEL2) to->modelindex2 = (double)qa_q2_msg_read_byte(m);
  if (bits & QA_Q2_U_MODEL3) to->modelindex3 = (double)qa_q2_msg_read_byte(m);
  if (bits & QA_Q2_U_MODEL4) to->modelindex4 = (double)qa_q2_msg_read_byte(m);
  if (bits & QA_Q2_U_FRAME8) to->frame = (double)qa_q2_msg_read_byte(m);
  if (bits & QA_Q2_U_FRAME16) to->frame = (double)qa_q2_msg_read_word(m);
  if ((bits & QA_Q2_U_SKIN8) != 0 && (bits & QA_Q2_U_SKIN16) != 0) to->skinnum = (double)qa_q2_msg_read_long(m);
  else if (bits & QA_Q2_U_SKIN8) to->skinnum = (double)qa_q2_msg_read_byte(m);
  else if (bits & QA_Q2_U_SKIN16) to->skinnum = (double)qa_q2_msg_read_word(m);
  if ((bits & (QA_Q2_U_EFFECTS8 | QA_Q2_U_EFFECTS16)) == (QA_Q2_U_EFFECTS8 | QA_Q2_U_EFFECTS16)) to->effects = (double)qa_q2_msg_read_long(m);
  else if (bits & QA_Q2_U_EFFECTS8) to->effects = (double)qa_q2_msg_read_byte(m);
  else if (bits & QA_Q2_U_EFFECTS16) to->effects = (double)qa_q2_msg_read_word(m);
  if ((bits & (QA_Q2_U_RENDERFX8 | QA_Q2_U_RENDERFX16)) == (QA_Q2_U_RENDERFX8 | QA_Q2_U_RENDERFX16)) to->renderfx = (double)qa_q2_msg_read_long(m);
  else if (bits & QA_Q2_U_RENDERFX8) to->renderfx = (double)qa_q2_msg_read_byte(m);
  else if (bits & QA_Q2_U_RENDERFX16) to->renderfx = (double)qa_q2_msg_read_word(m);
  if (bits & QA_Q2_U_ORIGIN1) to->origin[0] = (float)qa_q2_msg_read_coord(m);
  if (bits & QA_Q2_U_ORIGIN2) to->origin[1] = (float)qa_q2_msg_read_coord(m);
  if (bits & QA_Q2_U_ORIGIN3) to->origin[2] = (float)qa_q2_msg_read_coord(m);
  if (bits & QA_Q2_U_ANGLE1) to->angles[0] = (float)qa_q2_msg_read_angle(m);
  if (bits & QA_Q2_U_ANGLE2) to->angles[1] = (float)qa_q2_msg_read_angle(m);
  if (bits & QA_Q2_U_ANGLE3) to->angles[2] = (float)qa_q2_msg_read_angle(m);
  if (bits & QA_Q2_U_OLDORIGIN) qa_q2_msg_read_pos(m, to->old_origin);
  if (bits & QA_Q2_U_SOUND) to->sound = (double)qa_q2_msg_read_byte(m);
  if (bits & QA_Q2_U_EVENT) to->event = (double)qa_q2_msg_read_byte(m);
  else to->event = 0;
  if (bits & QA_Q2_U_SOLID) {
    to->solid = long_solid(ctx) ? (double)qa_q2_msg_read_long(m)
                                : (double)qa_q2_msg_read_short(m);
  }
}

/* TS encodePlayerStateDelta result carried explicitly. */
typedef struct r1_enc {
  int32_t flags;
  int32_t extraflags;
  int32_t statbits;
  const qa_q2_wire_player *ps;
} r1_enc;

static r1_enc encode_player(const qa_q2_wire_player *from, const qa_q2_wire_player *to) {
  r1_enc e;
  e.flags = 0;
  e.extraflags = 0;
  e.statbits = 0;
  e.ps = to;
  const qa_q2_wire_player *ps = to;
  const qa_q2_wire_player *ops = from;
  if (ps->pmove.pm_type != ops->pmove.pm_type) e.flags |= QA_Q2_PS_M_TYPE;
  if (ps->pmove.origin[0] != ops->pmove.origin[0] || ps->pmove.origin[1] != ops->pmove.origin[1]) e.flags |= QA_Q2_PS_M_ORIGIN;
  if (ps->pmove.origin[2] != ops->pmove.origin[2]) e.extraflags |= QA_Q2_EPS_M_ORIGIN2;
  if (ps->pmove.velocity[0] != ops->pmove.velocity[0] || ps->pmove.velocity[1] != ops->pmove.velocity[1]) e.flags |= QA_Q2_PS_M_VELOCITY;
  if (ps->pmove.velocity[2] != ops->pmove.velocity[2]) e.extraflags |= QA_Q2_EPS_M_VELOCITY2;
  if (ps->pmove.pm_time != ops->pmove.pm_time) e.flags |= QA_Q2_PS_M_TIME;
  if (ps->pmove.pm_flags != ops->pmove.pm_flags) e.flags |= QA_Q2_PS_M_FLAGS;
  if (ps->pmove.gravity != ops->pmove.gravity) e.flags |= QA_Q2_PS_M_GRAVITY;
  if (ps->pmove.delta_angles[0] != ops->pmove.delta_angles[0] || ps->pmove.delta_angles[1] != ops->pmove.delta_angles[1] || ps->pmove.delta_angles[2] != ops->pmove.delta_angles[2]) e.flags |= QA_Q2_PS_M_DELTA_ANGLES;
  if (ps->viewoffset[0] != ops->viewoffset[0] || ps->viewoffset[1] != ops->viewoffset[1] || ps->viewoffset[2] != ops->viewoffset[2]) e.flags |= QA_Q2_PS_VIEWOFFSET;
  if (ps->viewangles[0] != ops->viewangles[0] || ps->viewangles[1] != ops->viewangles[1]) e.flags |= QA_Q2_PS_VIEWANGLES;
  if (ps->viewangles[2] != ops->viewangles[2]) e.extraflags |= QA_Q2_EPS_VIEWANGLE2;
  if (ps->kick_angles[0] != ops->kick_angles[0] || ps->kick_angles[1] != ops->kick_angles[1] || ps->kick_angles[2] != ops->kick_angles[2]) e.flags |= QA_Q2_PS_KICKANGLES;
  if (ps->blend[0] != ops->blend[0] || ps->blend[1] != ops->blend[1] || ps->blend[2] != ops->blend[2] || ps->blend[3] != ops->blend[3]) e.flags |= QA_Q2_PS_BLEND;
  if (ps->fov != ops->fov) e.flags |= QA_Q2_PS_FOV;
  if (ps->rdflags != ops->rdflags) e.flags |= QA_Q2_PS_RDFLAGS;
  e.flags |= QA_Q2_PS_WEAPONINDEX;
  if (ps->gunframe != ops->gunframe) e.flags |= QA_Q2_PS_WEAPONFRAME;
  if (ps->gunoffset[0] != ops->gunoffset[0] || ps->gunoffset[1] != ops->gunoffset[1] || ps->gunoffset[2] != ops->gunoffset[2]) e.extraflags |= QA_Q2_EPS_GUNOFFSET;
  if (ps->gunangles[0] != ops->gunangles[0] || ps->gunangles[1] != ops->gunangles[1] || ps->gunangles[2] != ops->gunangles[2]) e.extraflags |= QA_Q2_EPS_GUNANGLES;
  for (int i = 0; i < QA_Q2_MAX_STATS; i++) {
    if (ps->stats[i] != ops->stats[i]) e.statbits |= 1 << i;
  }
  if (e.statbits != 0) e.extraflags |= QA_Q2_EPS_STATS;
  return e;
}

static void write_player_body(const r1_enc *e, qa_q2_sizebuf *msg) {
  const qa_q2_wire_player *ps = e->ps;
  int32_t flags = e->flags;
  int32_t extraflags = e->extraflags;
  if (flags & QA_Q2_PS_M_TYPE) qa_q2_msg_write_byte(msg, ps->pmove.pm_type);
  if (flags & QA_Q2_PS_M_ORIGIN) {
    qa_q2_msg_write_short(msg, (double)ps->pmove.origin[0]);
    qa_q2_msg_write_short(msg, (double)ps->pmove.origin[1]);
  }
  if (extraflags & QA_Q2_EPS_M_ORIGIN2) qa_q2_msg_write_short(msg, (double)ps->pmove.origin[2]);
  if (flags & QA_Q2_PS_M_VELOCITY) {
    qa_q2_msg_write_short(msg, (double)ps->pmove.velocity[0]);
    qa_q2_msg_write_short(msg, (double)ps->pmove.velocity[1]);
  }
  if (extraflags & QA_Q2_EPS_M_VELOCITY2) qa_q2_msg_write_short(msg, (double)ps->pmove.velocity[2]);
  if (flags & QA_Q2_PS_M_TIME) qa_q2_msg_write_byte(msg, ps->pmove.pm_time);
  if (flags & QA_Q2_PS_M_FLAGS) qa_q2_msg_write_byte(msg, ps->pmove.pm_flags);
  if (flags & QA_Q2_PS_M_GRAVITY) qa_q2_msg_write_short(msg, ps->pmove.gravity);
  if (flags & QA_Q2_PS_M_DELTA_ANGLES) {
    qa_q2_msg_write_short(msg, (double)ps->pmove.delta_angles[0]);
    qa_q2_msg_write_short(msg, (double)ps->pmove.delta_angles[1]);
    qa_q2_msg_write_short(msg, (double)ps->pmove.delta_angles[2]);
  }
  if (flags & QA_Q2_PS_VIEWOFFSET) {
    qa_q2_msg_write_char(msg, (double)ps->viewoffset[0] * 4.0);
    qa_q2_msg_write_char(msg, (double)ps->viewoffset[1] * 4.0);
    qa_q2_msg_write_char(msg, (double)ps->viewoffset[2] * 4.0);
  }
  if (flags & QA_Q2_PS_VIEWANGLES) {
    qa_q2_msg_write_angle16(msg, (double)ps->viewangles[0]);
    qa_q2_msg_write_angle16(msg, (double)ps->viewangles[1]);
  }
  if (extraflags & QA_Q2_EPS_VIEWANGLE2) qa_q2_msg_write_angle16(msg, (double)ps->viewangles[2]);
  if (flags & QA_Q2_PS_KICKANGLES) {
    qa_q2_msg_write_char(msg, (double)ps->kick_angles[0] * 4.0);
    qa_q2_msg_write_char(msg, (double)ps->kick_angles[1] * 4.0);
    qa_q2_msg_write_char(msg, (double)ps->kick_angles[2] * 4.0);
  }
  if (flags & QA_Q2_PS_WEAPONINDEX) qa_q2_msg_write_byte(msg, ps->gunindex);
  if (flags & QA_Q2_PS_WEAPONFRAME) qa_q2_msg_write_byte(msg, ps->gunframe);
  if (extraflags & QA_Q2_EPS_GUNOFFSET) {
    qa_q2_msg_write_char(msg, (double)ps->gunoffset[0] * 4.0);
    qa_q2_msg_write_char(msg, (double)ps->gunoffset[1] * 4.0);
    qa_q2_msg_write_char(msg, (double)ps->gunoffset[2] * 4.0);
  }
  if (extraflags & QA_Q2_EPS_GUNANGLES) {
    qa_q2_msg_write_char(msg, (double)ps->gunangles[0] * 4.0);
    qa_q2_msg_write_char(msg, (double)ps->gunangles[1] * 4.0);
    qa_q2_msg_write_char(msg, (double)ps->gunangles[2] * 4.0);
  }
  if (flags & QA_Q2_PS_BLEND) {
    qa_q2_msg_write_byte(msg, (double)ps->blend[0] * 255.0);
    qa_q2_msg_write_byte(msg, (double)ps->blend[1] * 255.0);
    qa_q2_msg_write_byte(msg, (double)ps->blend[2] * 255.0);
    qa_q2_msg_write_byte(msg, (double)ps->blend[3] * 255.0);
  }
  if (flags & QA_Q2_PS_FOV) qa_q2_msg_write_byte(msg, ps->fov);
  if (flags & QA_Q2_PS_RDFLAGS) qa_q2_msg_write_byte(msg, ps->rdflags);
  if (extraflags & QA_Q2_EPS_STATS) {
    qa_q2_msg_write_long(msg, (double)e->statbits);
    for (int i = 0; i < QA_Q2_MAX_STATS; i++) {
      if (e->statbits & (1 << i)) qa_q2_msg_write_short(msg, (double)ps->stats[i]);
    }
  }
}

static void write_player_state_delta(qa_q2_protocol_codec *self, qa_q2_sizebuf *msg,
                                     const qa_q2_wire_player *from,
                                     const qa_q2_wire_player *to) {
  (void)self;
  r1_enc e = encode_player(from, to);
  qa_q2_msg_write_short(msg, (double)e.flags);
  qa_q2_msg_write_byte(msg, (double)e.extraflags);
  write_player_body(&e, msg);
}

static void copy_player(qa_q2_wire_player *dst, const qa_q2_wire_player *src) {
  dst->pmove.pm_type = src->pmove.pm_type;
  memcpy(dst->pmove.origin, src->pmove.origin, sizeof dst->pmove.origin);
  memcpy(dst->pmove.velocity, src->pmove.velocity, sizeof dst->pmove.velocity);
  dst->pmove.pm_flags = src->pmove.pm_flags;
  dst->pmove.pm_time = src->pmove.pm_time;
  dst->pmove.gravity = src->pmove.gravity;
  memcpy(dst->pmove.delta_angles, src->pmove.delta_angles, sizeof dst->pmove.delta_angles);
  qa_q2_vector_copy(src->viewangles, dst->viewangles);
  qa_q2_vector_copy(src->viewoffset, dst->viewoffset);
  qa_q2_vector_copy(src->kick_angles, dst->kick_angles);
  qa_q2_vector_copy(src->gunangles, dst->gunangles);
  qa_q2_vector_copy(src->gunoffset, dst->gunoffset);
  dst->gunindex = src->gunindex;
  dst->gunframe = src->gunframe;
  memcpy(dst->blend, src->blend, sizeof dst->blend);
  dst->fov = src->fov;
  dst->rdflags = src->rdflags;
  memcpy(dst->stats, src->stats, sizeof dst->stats);
}

static void read_player_fields(qa_q2_sizebuf *msg, const qa_q2_wire_player *from,
                               qa_q2_wire_player *to, int32_t flags, int32_t extraflags) {
  copy_player(to, from);
  qa_q2_wire_player *t = to;
  if (flags & QA_Q2_PS_M_TYPE) t->pmove.pm_type = (double)qa_q2_msg_read_byte(msg);
  if (flags & QA_Q2_PS_M_ORIGIN) {
    t->pmove.origin[0] = qa_q2_msg_read_short(msg);
    t->pmove.origin[1] = qa_q2_msg_read_short(msg);
  }
  if (extraflags & QA_Q2_EPS_M_ORIGIN2) t->pmove.origin[2] = qa_q2_msg_read_short(msg);
  if (flags & QA_Q2_PS_M_VELOCITY) {
    t->pmove.velocity[0] = qa_q2_msg_read_short(msg);
    t->pmove.velocity[1] = qa_q2_msg_read_short(msg);
  }
  if (extraflags & QA_Q2_EPS_M_VELOCITY2) t->pmove.velocity[2] = qa_q2_msg_read_short(msg);
  if (flags & QA_Q2_PS_M_TIME) t->pmove.pm_time = (double)qa_q2_msg_read_byte(msg);
  if (flags & QA_Q2_PS_M_FLAGS) t->pmove.pm_flags = (double)qa_q2_msg_read_byte(msg);
  if (flags & QA_Q2_PS_M_GRAVITY) t->pmove.gravity = (double)qa_q2_msg_read_short(msg);
  if (flags & QA_Q2_PS_M_DELTA_ANGLES) {
    t->pmove.delta_angles[0] = (int16_t)qa_q2_msg_read_short(msg);
    t->pmove.delta_angles[1] = (int16_t)qa_q2_msg_read_short(msg);
    t->pmove.delta_angles[2] = (int16_t)qa_q2_msg_read_short(msg);
  }
  if (flags & QA_Q2_PS_VIEWOFFSET) {
    t->viewoffset[0] = (float)((double)qa_q2_msg_read_char(msg) * 0.25);
    t->viewoffset[1] = (float)((double)qa_q2_msg_read_char(msg) * 0.25);
    t->viewoffset[2] = (float)((double)qa_q2_msg_read_char(msg) * 0.25);
  }
  if (flags & QA_Q2_PS_VIEWANGLES) {
    t->viewangles[0] = (float)qa_q2_msg_read_angle16(msg);
    t->viewangles[1] = (float)qa_q2_msg_read_angle16(msg);
  }
  if (extraflags & QA_Q2_EPS_VIEWANGLE2) t->viewangles[2] = (float)qa_q2_msg_read_angle16(msg);
  if (flags & QA_Q2_PS_KICKANGLES) {
    t->kick_angles[0] = (float)((double)qa_q2_msg_read_char(msg) * 0.25);
    t->kick_angles[1] = (float)((double)qa_q2_msg_read_char(msg) * 0.25);
    t->kick_angles[2] = (float)((double)qa_q2_msg_read_char(msg) * 0.25);
  }
  if (flags & QA_Q2_PS_WEAPONINDEX) t->gunindex = (double)qa_q2_msg_read_byte(msg);
  if (flags & QA_Q2_PS_WEAPONFRAME) t->gunframe = (double)qa_q2_msg_read_byte(msg);
  if (extraflags & QA_Q2_EPS_GUNOFFSET) {
    t->gunoffset[0] = (float)((double)qa_q2_msg_read_char(msg) * 0.25);
    t->gunoffset[1] = (float)((double)qa_q2_msg_read_char(msg) * 0.25);
    t->gunoffset[2] = (float)((double)qa_q2_msg_read_char(msg) * 0.25);
  }
  if (extraflags & QA_Q2_EPS_GUNANGLES) {
    t->gunangles[0] = (float)((double)qa_q2_msg_read_char(msg) * 0.25);
    t->gunangles[1] = (float)((double)qa_q2_msg_read_char(msg) * 0.25);
    t->gunangles[2] = (float)((double)qa_q2_msg_read_char(msg) * 0.25);
  }
  if (flags & QA_Q2_PS_BLEND) {
    t->blend[0] = (float)((double)qa_q2_msg_read_byte(msg) / 255.0);
    t->blend[1] = (float)((double)qa_q2_msg_read_byte(msg) / 255.0);
    t->blend[2] = (float)((double)qa_q2_msg_read_byte(msg) / 255.0);
    t->blend[3] = (float)((double)qa_q2_msg_read_byte(msg) / 255.0);
  }
  if (flags & QA_Q2_PS_FOV) t->fov = (double)qa_q2_msg_read_byte(msg);
  if (flags & QA_Q2_PS_RDFLAGS) t->rdflags = (double)qa_q2_msg_read_byte(msg);
  if (extraflags & QA_Q2_EPS_STATS) {
    int32_t statbits = qa_q2_msg_read_long(msg);
    for (int i = 0; i < QA_Q2_MAX_STATS; i++) {
      if (statbits & (1 << i)) t->stats[i] = (int16_t)qa_q2_msg_read_short(msg);
    }
  }
}

static void read_player_state_delta(qa_q2_protocol_codec *self, qa_q2_sizebuf *msg,
                                    const qa_q2_wire_player *from,
                                    qa_q2_wire_player *to) {
  (void)self;
  int32_t flags = qa_q2_msg_read_short(msg);
  int32_t extraflags = qa_q2_msg_read_byte(msg);
  read_player_fields(msg, from, to, flags, extraflags);
}

static void write_frame(qa_q2_protocol_codec *self, qa_q2_sizebuf *msg,
                        const qa_q2_frame_write_params *params,
                        qa_q2_write_entities_fn write_entities, void *ctx) {
  (void)self;
  qa_q2_wire_player empty;
  qa_q2_wire_player_init(&empty);
  r1_enc e = encode_player(params->ps_from != NULL ? params->ps_from : &empty,
                           params->ps_to);
  qa_q2_msg_write_byte(msg, (double)(QA_Q2_SVC_FRAME | ((e.extraflags & 0xf0) << 1)));
  int32_t offset = params->lastframe == -1
                       ? 31
                       : (int32_t)(params->framenum - params->lastframe);
  int32_t encoded = ((int32_t)params->framenum & 0x07ffffff) | (offset << 27);
  qa_q2_msg_write_long(msg, (double)encoded);
  qa_q2_msg_write_byte(msg, (double)(((int32_t)params->surpress_count & 0x0f) |
                                     ((e.extraflags & 0x0f) << 4)));
  qa_q2_msg_write_byte(msg, params->areabytes);
  qa_q2_sz_write(msg, params->areabits, params->areabits_length,
                 (int64_t)params->areabytes);
  qa_q2_msg_write_short(msg, (double)e.flags);
  write_player_body(&e, msg);
  write_entities(ctx, msg);
}

static void read_frame_header(qa_q2_protocol_codec *self, uint8_t *areabits,
                              size_t areabits_length, bool read_suppress_byte,
                              qa_q2_frame_header *out) {
  (void)read_suppress_byte;
  qa_q2_r1_context *ctx = ctx_of(self);
  qa_q2_sizebuf *m = self->net_message;
  int32_t encoded = qa_q2_msg_read_long(m);
  uint32_t uoffset = (uint32_t)encoded >> 27;
  int32_t serverframe = encoded & 0x07ffffff;
  int32_t deltaframe = uoffset == 31u ? -1 : serverframe - (int32_t)uoffset;
  int32_t extraflags = ctx->pending_frame_extrabits >> 1;
  int32_t suppress_byte = qa_q2_msg_read_byte(m);
  extraflags |= (suppress_byte & 0xf0) >> 4;
  ctx->pending_frame_extraflags = extraflags;
  int32_t len = qa_q2_msg_read_byte(m);
  qa_q2_msg_read_data(m, areabits, areabits_length, len);
  out->serverframe = (double)serverframe;
  out->deltaframe = (double)deltaframe;
  out->surpress_count = (double)(suppress_byte & 0x0f);
  out->areabytes = (double)len;
}

static void read_frame_playerstate(qa_q2_protocol_codec *self,
                                   const qa_q2_wire_player *from,
                                   qa_q2_wire_player *to) {
  qa_q2_r1_context *ctx = ctx_of(self);
  int32_t flags = qa_q2_msg_read_short(self->net_message);
  read_player_fields(self->net_message, from, to, flags, ctx->pending_frame_extraflags);
}

#define BTN_DBLFORWARD (1 << 2)
#define BTN_DBLSIDE (1 << 3)
#define BTN_DBLUP (1 << 4)
#define BTN_DBL_ANGLE1 (1 << 5)
#define BTN_DBL_ANGLE2 (1 << 6)

static bool dbl_mod(double v, double m) { return fmod(v, m) == 0; }

static void w_usercmd(qa_q2_protocol_codec *self, qa_q2_sizebuf *msg,
                      const qa_q2_wire_usercmd *from, const qa_q2_wire_usercmd *cmd) {
  qa_q2_r1_context *ctx = ctx_of(self);
  bool compressed = compressed_moves(ctx);
  int32_t bits = 0;
  if (cmd->angles[0] != from->angles[0]) bits |= 1 << 0;
  if (cmd->angles[1] != from->angles[1]) bits |= 1 << 1;
  if (cmd->angles[2] != from->angles[2]) bits |= 1 << 2;
  if (cmd->forwardmove != from->forwardmove) bits |= 1 << 3;
  if (cmd->sidemove != from->sidemove) bits |= 1 << 4;
  if (cmd->upmove != from->upmove) bits |= 1 << 5;
  if (cmd->buttons != from->buttons) bits |= 1 << 6;
  if (cmd->impulse != from->impulse) bits |= 1 << 7;
  qa_q2_msg_write_byte(msg, (double)bits);
  int32_t buttons = (bits & (1 << 6)) != 0 ? (int32_t)cmd->buttons : 0;
  if (compressed && (bits & (1 << 6)) != 0) {
    if ((bits & (1 << 3)) != 0 && dbl_mod(cmd->forwardmove, 5)) buttons |= BTN_DBLFORWARD;
    if ((bits & (1 << 4)) != 0 && dbl_mod(cmd->sidemove, 5)) buttons |= BTN_DBLSIDE;
    if ((bits & (1 << 5)) != 0 && dbl_mod(cmd->upmove, 5)) buttons |= BTN_DBLUP;
    if ((bits & (1 << 0)) != 0 && cmd->angles[0] % 64 == 0 &&
        fabs(trunc((double)cmd->angles[0] / 64.0)) < 128) buttons |= BTN_DBL_ANGLE1;
    if ((bits & (1 << 1)) != 0 && cmd->angles[1] % 256 == 0) buttons |= BTN_DBL_ANGLE2;
    qa_q2_msg_write_byte(msg, (double)buttons);
  }
  if (bits & (1 << 0)) {
    if (buttons & BTN_DBL_ANGLE1) qa_q2_msg_write_char(msg, trunc((double)cmd->angles[0] / 64.0));
    else qa_q2_msg_write_short(msg, (double)cmd->angles[0]);
  }
  if (bits & (1 << 1)) {
    if (buttons & BTN_DBL_ANGLE2) qa_q2_msg_write_char(msg, trunc((double)cmd->angles[1] / 256.0));
    else qa_q2_msg_write_short(msg, (double)cmd->angles[1]);
  }
  if (bits & (1 << 2)) qa_q2_msg_write_short(msg, (double)cmd->angles[2]);
  if (bits & (1 << 3)) {
    if (buttons & BTN_DBLFORWARD) qa_q2_msg_write_char(msg, trunc(cmd->forwardmove / 5.0));
    else qa_q2_msg_write_short(msg, cmd->forwardmove);
  }
  if (bits & (1 << 4)) {
    if (buttons & BTN_DBLSIDE) qa_q2_msg_write_char(msg, trunc(cmd->sidemove / 5.0));
    else qa_q2_msg_write_short(msg, cmd->sidemove);
  }
  if (bits & (1 << 5)) {
    if (buttons & BTN_DBLUP) qa_q2_msg_write_char(msg, trunc(cmd->upmove / 5.0));
    else qa_q2_msg_write_short(msg, cmd->upmove);
  }
  if (!compressed && (bits & (1 << 6)) != 0) qa_q2_msg_write_byte(msg, cmd->buttons);
  if (bits & (1 << 7)) qa_q2_msg_write_byte(msg, cmd->impulse);
  qa_q2_msg_write_byte(msg, cmd->msec);
  qa_q2_msg_write_byte(msg, cmd->lightlevel);
}

static void r_usercmd(qa_q2_protocol_codec *self, qa_q2_sizebuf *msg,
                      const qa_q2_wire_usercmd *from, qa_q2_wire_usercmd *move) {
  qa_q2_r1_context *ctx = ctx_of(self);
  bool compressed = compressed_moves(ctx);
  move->msec = from->msec;
  move->buttons = from->buttons;
  move->angles[0] = from->angles[0];
  move->angles[1] = from->angles[1];
  move->angles[2] = from->angles[2];
  move->forwardmove = from->forwardmove;
  move->sidemove = from->sidemove;
  move->upmove = from->upmove;
  move->impulse = from->impulse;
  move->lightlevel = from->lightlevel;
  int32_t bits = qa_q2_msg_read_byte(msg);
  int32_t buttons = 0;
  if (compressed && (bits & (1 << 6)) != 0) buttons = qa_q2_msg_read_byte(msg);
  if (bits & (1 << 0)) move->angles[0] = (int16_t)((buttons & BTN_DBL_ANGLE1) != 0 ? qa_q2_msg_read_char(msg) * 64 : qa_q2_msg_read_short(msg));
  if (bits & (1 << 1)) move->angles[1] = (int16_t)((buttons & BTN_DBL_ANGLE2) != 0 ? qa_q2_msg_read_char(msg) * 256 : qa_q2_msg_read_short(msg));
  if (bits & (1 << 2)) move->angles[2] = (int16_t)qa_q2_msg_read_short(msg);
  if (bits & (1 << 3)) move->forwardmove = (buttons & BTN_DBLFORWARD) != 0 ? (double)qa_q2_msg_read_char(msg) * 5.0 : (double)qa_q2_msg_read_short(msg);
  if (bits & (1 << 4)) move->sidemove = (buttons & BTN_DBLSIDE) != 0 ? (double)qa_q2_msg_read_char(msg) * 5.0 : (double)qa_q2_msg_read_short(msg);
  if (bits & (1 << 5)) move->upmove = (buttons & BTN_DBLUP) != 0 ? (double)qa_q2_msg_read_char(msg) * 5.0 : (double)qa_q2_msg_read_short(msg);
  if (!compressed && (bits & (1 << 6)) != 0) buttons = qa_q2_msg_read_byte(msg);
  if (bits & (1 << 7)) move->impulse = (double)qa_q2_msg_read_byte(msg);
  move->msec = (double)qa_q2_msg_read_byte(msg);
  move->lightlevel = (double)qa_q2_msg_read_byte(msg);
  if ((bits & (1 << 6)) != 0) {
    move->buttons = (double)(buttons & ~(BTN_DBLFORWARD | BTN_DBLSIDE | BTN_DBLUP |
                                         BTN_DBL_ANGLE1 | BTN_DBL_ANGLE2));
  }
}

static void read_entity_bits(qa_q2_protocol_codec *self, qa_q2_entity_bits *out) {
  qa_q2_sizebuf *m = self->net_message;
  int32_t total = qa_q2_msg_read_byte(m);
  if (total & QA_Q2_U_MOREBITS1) total |= qa_q2_msg_read_byte(m) << 8;
  if (total & QA_Q2_U_MOREBITS2) total |= qa_q2_msg_read_byte(m) << 16;
  if (total & QA_Q2_U_MOREBITS3) total |= qa_q2_msg_read_byte(m) << 24;
  out->number = (total & QA_Q2_U_NUMBER16) != 0 ? (double)qa_q2_msg_read_short(m)
                                                : (double)qa_q2_msg_read_byte(m);
  out->bits = (double)(uint32_t)total;
  out->bits_u64 = (uint32_t)total;
}

static void read_client_setting(qa_q2_protocol_codec *self, qa_q2_sizebuf *msg,
                                qa_q2_clc_client_setting *out) {
  (void)self;
  out->index = (double)qa_q2_msg_read_short(msg);
  out->value = (double)qa_q2_msg_read_short(msg);
}

static void noop_begin_write(qa_q2_protocol_codec *self, qa_q2_sizebuf *msg) {
  (void)self;
  (void)msg;
}
static void noop_begin_read(qa_q2_protocol_codec *self) { (void)self; }

static void write_spawn_baseline(qa_q2_protocol_codec *self, qa_q2_sizebuf *msg,
                                 const qa_q2_wire_entity *base) {
  qa_q2_wire_entity null_state;
  qa_q2_wire_entity_init(&null_state);
  qa_q2_msg_write_byte(msg, QA_Q2_SVC_SPAWNBASELINE);
  write_delta_entity(self, msg, &null_state, base, true, true);
}

static void f_entity_remove(qa_q2_protocol_codec *self, qa_q2_sizebuf *msg, double oldnum) {
  qa_q2_r1_context *ctx = ctx_of(self);
  ctx->vanilla.codec.write_entity_remove(&ctx->vanilla.codec, msg, oldnum);
}
static void f_entities_end(qa_q2_protocol_codec *self, qa_q2_sizebuf *msg) {
  qa_q2_r1_context *ctx = ctx_of(self);
  ctx->vanilla.codec.write_packet_entities_end(&ctx->vanilla.codec, msg);
}

void qa_q2_r1_set_frame_extrabits(qa_q2_r1_context *ctx, int32_t extrabits) {
  ctx->pending_frame_extrabits = extrabits;
}

qa_q2_protocol_codec *qa_q2_r1_create_codec(qa_q2_r1_context *ctx, double minor_version) {
  ctx->minor_version = minor_version;
  return &ctx->codec;
}

void qa_q2_r1_context_init(qa_q2_r1_context *ctx, qa_q2_sizebuf *net_message) {
  memset(ctx, 0, sizeof *ctx);
  ctx->net_message = net_message;
  qa_q2_vanilla_context_init(&ctx->vanilla, net_message);
  ctx->codec.name = "r1q2";
  ctx->codec.net_message = net_message;
  ctx->codec.ctx = ctx;
  ctx->codec.write_server_data = write_server_data;
  ctx->codec.write_delta_entity = write_delta_entity;
  ctx->codec.write_entity_remove = f_entity_remove;
  ctx->codec.write_packet_entities_end = f_entities_end;
  ctx->codec.write_spawn_baseline = write_spawn_baseline;
  ctx->codec.write_player_state_delta = write_player_state_delta;
  ctx->codec.write_packet_entities_begin = noop_begin_write;
  ctx->codec.write_frame = write_frame;
  ctx->codec.write_delta_usercmd = w_usercmd;
  ctx->codec.read_delta_usercmd = r_usercmd;
  ctx->codec.read_server_data = read_server_data;
  ctx->codec.read_entity_bits = read_entity_bits;
  ctx->codec.read_delta_entity = read_delta_entity;
  ctx->codec.read_player_state_delta = read_player_state_delta;
  ctx->codec.read_frame_header = read_frame_header;
  ctx->codec.read_frame_playerstate = read_frame_playerstate;
  ctx->codec.read_packet_entities_begin = noop_begin_read;
  ctx->codec.read_client_setting = read_client_setting;
  ctx->minor_version = QA_Q2_PROTOCOL_VERSION_R1Q2_CURRENT;
}
