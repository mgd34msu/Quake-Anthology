/* Port of src/network/q2/codecs/q2pro.ts. GPL-2.0-or-later. */
#include "qa/network/q2/codecs/q2pro.h"

#include <string.h>

#include "qa/network/q2/codecs/clc_batch_move.h"
#include "qa/network/q2/constants.h"

static qa_q2pro_context *ctx_of(qa_q2_protocol_codec *self) {
  return (qa_q2pro_context *)self->ctx;
}

static void write_server_data(qa_q2_protocol_codec *self, qa_q2_sizebuf *msg,
                              const qa_q2_server_data_params *params) {
  qa_q2pro_context *ctx = ctx_of(self);
  qa_q2_msg_write_byte(msg, QA_Q2_SVC_SERVERDATA);
  qa_q2_msg_write_long(msg, QA_Q2_PROTOCOL_VERSION_Q2PRO);
  qa_q2_msg_write_long(msg, params->servercount);
  qa_q2_msg_write_byte(msg, params->attractloop ? 1 : 0);
  qa_q2_msg_write_string(msg, params->gamedir);
  qa_q2_msg_write_short(msg, params->clientnum);
  qa_q2_msg_write_string(msg, params->levelname);
  ctx->features.revision =
      params->has_q2pro_version ? params->q2pro_version : ctx->minor_version;
  double fallback_flags =
      (params->has_q2pro_strafejump_hack && params->q2pro_strafejump_hack ? 1 : 0) |
      (params->has_q2pro_qw_mode && params->q2pro_qw_mode ? 2 : 0) |
      (params->has_q2pro_waterjump_hack && params->q2pro_waterjump_hack ? 4 : 0);
  ctx->features.flags = params->has_wire_flags ? params->wire_flags : fallback_flags;
  qa_q2_msg_write_short(msg, ctx->features.revision);
  qa_q2_msg_write_byte(msg, params->server_state);
  if (ctx->features.revision >= 1024) {
    qa_q2_msg_write_short(msg, ctx->features.flags);
  } else {
    int32_t f = (int32_t)ctx->features.flags;
    qa_q2_msg_write_byte(msg, (f & 1) != 0 ? 1 : 0);
    qa_q2_msg_write_byte(msg, (f & 2) != 0 ? 1 : 0);
    qa_q2_msg_write_byte(msg, (f & 4) != 0 ? 1 : 0);
  }
}

static void write_delta_entity(qa_q2_protocol_codec *self, qa_q2_sizebuf *msg,
                               const qa_q2_wire_entity *from,
                               const qa_q2_wire_entity *to, bool force,
                               bool newentity) {
  qa_q2pro_context *ctx = ctx_of(self);
  qa_q2pro_write_entity(msg, ctx->features, from, to, force, newentity);
}

static void write_entity_remove(qa_q2_protocol_codec *self, qa_q2_sizebuf *msg,
                                double oldnum) {
  (void)self;
  int32_t bits = QA_Q2_U_REMOVE;
  if (oldnum >= 256) bits |= QA_Q2_U_NUMBER16 | QA_Q2_U_MOREBITS1;
  qa_q2_msg_write_byte(msg, (double)(bits & 255));
  if ((bits & 0x0000ff00) != 0) qa_q2_msg_write_byte(msg, (double)((bits >> 8) & 255));
  if ((bits & QA_Q2_U_NUMBER16) != 0) qa_q2_msg_write_short(msg, oldnum);
  else qa_q2_msg_write_byte(msg, oldnum);
}
static void write_entities_end(qa_q2_protocol_codec *self, qa_q2_sizebuf *msg) {
  (void)self;
  qa_q2_msg_write_short(msg, 0);
}
static void noop_w(qa_q2_protocol_codec *self, qa_q2_sizebuf *msg) {
  (void)self;
  (void)msg;
}
static void noop_r(qa_q2_protocol_codec *self) { (void)self; }

static void write_spawn_baseline(qa_q2_protocol_codec *self, qa_q2_sizebuf *msg,
                                 const qa_q2_wire_entity *base) {
  qa_q2pro_context *ctx = ctx_of(self);
  qa_q2_wire_entity null_state;
  qa_q2_wire_entity_init(&null_state);
  qa_q2_msg_write_byte(msg, QA_Q2_SVC_SPAWNBASELINE);
  qa_q2pro_write_entity(msg, ctx->features, &null_state, base, true, true);
}

/* TS encodePlayerStateDelta result carried explicitly. */
typedef struct pro_enc {
  int32_t flags;
  int32_t extraflags;
  uint64_t statbits;
  int32_t numstats;
  int32_t blendbits;
  int32_t fogbits;
  bool extended;
  bool v2;
  const qa_q2_wire_player *ps;
  const qa_q2_wire_player *ops;
} pro_enc;

static pro_enc encode_player(qa_q2pro_context *ctx, const qa_q2_wire_player *from,
                             const qa_q2_wire_player *to) {
  pro_enc e;
  memset(&e, 0, sizeof e);
  e.ps = to;
  e.ops = from;
  e.extended = qa_q2pro_extensions(ctx->features);
  e.v2 = qa_q2pro_extensions_v2(ctx->features);
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
  if (ps->gunindex != ops->gunindex || ps->gunskin != ops->gunskin) e.flags |= QA_Q2_PS_WEAPONINDEX;
  if (ps->gunframe != ops->gunframe) e.flags |= QA_Q2_PS_WEAPONFRAME;
  if (ps->gunoffset[0] != ops->gunoffset[0] || ps->gunoffset[1] != ops->gunoffset[1] || ps->gunoffset[2] != ops->gunoffset[2]) e.extraflags |= QA_Q2_EPS_GUNOFFSET;
  if (ps->gunangles[0] != ops->gunangles[0] || ps->gunangles[1] != ops->gunangles[1] || ps->gunangles[2] != ops->gunangles[2]) e.extraflags |= QA_Q2_EPS_GUNANGLES;
  if (ps->blend[0] != ops->blend[0] || ps->blend[1] != ops->blend[1] || ps->blend[2] != ops->blend[2] || ps->blend[3] != ops->blend[3]) e.flags |= QA_Q2_PS_BLEND;
  if (ps->fov != ops->fov) e.flags |= QA_Q2_PS_FOV;
  if (ps->rdflags != ops->rdflags) e.flags |= QA_Q2_PS_RDFLAGS;
  e.numstats = e.v2 ? 64 : 32;
  for (int i = 0; i < e.numstats; i++) {
    if (ps->stats[i] != ops->stats[i]) e.statbits |= 1ull << i;
  }
  if (e.statbits != 0) e.extraflags |= QA_Q2_EPS_STATS;
  if (ps->clientnum != ops->clientnum) e.extraflags |= 64;
  for (int i = 0; i < 4; i++) {
    if (ps->blend[i] != ops->blend[i]) e.blendbits |= 1 << i;
    if (ps->damage_blend[i] != ops->damage_blend[i]) e.blendbits |= 16 << i;
  }
  if (e.v2 && e.blendbits != 0) e.flags |= QA_Q2_PS_BLEND;
  e.fogbits = qa_q2pro_fog_bits(&ops->q2pro_fog, &ps->q2pro_fog);
  if (e.fogbits != 0) {
    if (ctx->features.revision < 1026) {
      qa_q2_throw_error("Q2PRO player fog requires revision 1026");
    }
    e.flags |= 0x18000;
  }
  return e;
}

static void write_movement(qa_q2_sizebuf *msg, bool v2, int32_t current, int32_t previous) {
  if (v2) qa_q2pro_write_int23(msg, current, previous);
  else qa_q2_msg_write_short(msg, (double)current);
}

static void write_player_body(qa_q2pro_context *ctx, const pro_enc *e, qa_q2_sizebuf *msg) {
  const qa_q2_wire_player *ps = e->ps;
  const qa_q2_wire_player *ops = e->ops;
  int32_t flags = e->flags;
  int32_t extraflags = e->extraflags;
  if (flags & QA_Q2_PS_M_TYPE) qa_q2_msg_write_byte(msg, ps->pmove.pm_type);
  if (flags & QA_Q2_PS_M_ORIGIN) {
    write_movement(msg, e->v2, ps->pmove.origin[0], ops->pmove.origin[0]);
    write_movement(msg, e->v2, ps->pmove.origin[1], ops->pmove.origin[1]);
  }
  if (extraflags & QA_Q2_EPS_M_ORIGIN2) write_movement(msg, e->v2, ps->pmove.origin[2], ops->pmove.origin[2]);
  if (flags & QA_Q2_PS_M_VELOCITY) {
    write_movement(msg, e->v2, ps->pmove.velocity[0], ops->pmove.velocity[0]);
    write_movement(msg, e->v2, ps->pmove.velocity[1], ops->pmove.velocity[1]);
  }
  if (extraflags & QA_Q2_EPS_M_VELOCITY2) write_movement(msg, e->v2, ps->pmove.velocity[2], ops->pmove.velocity[2]);
  if (flags & QA_Q2_PS_M_TIME) {
    if (e->v2) qa_q2_msg_write_short(msg, ps->pmove.pm_time);
    else qa_q2_msg_write_byte(msg, ps->pmove.pm_time);
  }
  if (flags & QA_Q2_PS_M_FLAGS) {
    if (e->v2) qa_q2_msg_write_short(msg, ps->pmove.pm_flags);
    else qa_q2_msg_write_byte(msg, ps->pmove.pm_flags);
  }
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
  if (flags & QA_Q2_PS_WEAPONINDEX) {
    if (e->extended) {
      int32_t gun = (int32_t)ps->gunindex | ((int32_t)ps->gunskin << 13);
      qa_q2_msg_write_short(msg, (double)gun);
    } else {
      if (ps->gunindex > 255 || ps->gunskin != 0) {
        qa_q2_throw_error("Q2PRO gun needs negotiated game extensions");
      }
      qa_q2_msg_write_byte(msg, ps->gunindex);
    }
  }
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
    if (e->v2) {
      qa_q2_msg_write_byte(msg, (double)e->blendbits);
      for (int i = 0; i < 4; i++) {
        if (e->blendbits & (1 << i)) qa_q2_msg_write_byte(msg, (double)ps->blend[i] * 255.0);
      }
      for (int i = 0; i < 4; i++) {
        if (e->blendbits & (16 << i)) qa_q2_msg_write_byte(msg, (double)ps->damage_blend[i] * 255.0);
      }
    } else {
      for (int i = 0; i < 4; i++) qa_q2_msg_write_byte(msg, (double)ps->blend[i] * 255.0);
    }
  }
  if (flags & 0x10000) qa_q2pro_write_fog(msg, e->fogbits, &ps->q2pro_fog);
  if (flags & QA_Q2_PS_FOV) qa_q2_msg_write_byte(msg, ps->fov);
  if (flags & QA_Q2_PS_RDFLAGS) qa_q2_msg_write_byte(msg, ps->rdflags);
  if (extraflags & QA_Q2_EPS_STATS) {
    if (e->v2) qa_q2pro_write_var64(msg, e->statbits);
    else qa_q2_msg_write_long(msg, (double)(uint32_t)e->statbits);
    for (int i = 0; i < e->numstats; i++) {
      if ((e->statbits & (1ull << i)) != 0) qa_q2_msg_write_short(msg, (double)ps->stats[i]);
    }
  }
  if (extraflags & 64) {
    if (ctx->features.revision >= 1022) qa_q2_msg_write_short(msg, ps->clientnum);
    else qa_q2_msg_write_byte(msg, ps->clientnum);
  }
}

static void write_player_state_delta(qa_q2_protocol_codec *self, qa_q2_sizebuf *msg,
                                     const qa_q2_wire_player *from,
                                     const qa_q2_wire_player *to) {
  qa_q2pro_context *ctx = ctx_of(self);
  pro_enc e = encode_player(ctx, from, to);
  qa_q2_msg_write_byte(msg, QA_Q2_SVC_PLAYERINFO);
  qa_q2_msg_write_short(msg, (double)e.flags);
  if (e.flags & 0x8000) qa_q2_msg_write_byte(msg, (double)((uint32_t)e.flags >> 16));
  qa_q2_msg_write_byte(msg, (double)e.extraflags);
  write_player_body(ctx, &e, msg);
}

static void write_frame(qa_q2_protocol_codec *self, qa_q2_sizebuf *msg,
                        const qa_q2_frame_write_params *params,
                        qa_q2_write_entities_fn write_entities, void *wctx) {
  qa_q2pro_context *ctx = ctx_of(self);
  qa_q2_wire_player empty;
  qa_q2_wire_player_init(&empty);
  pro_enc e = encode_player(ctx, params->ps_from != NULL ? params->ps_from : &empty,
                            params->ps_to);
  int32_t extrabits = (e.extraflags & 0x70) << 1;
  qa_q2_msg_write_byte(msg, (double)(QA_Q2_SVC_FRAME | extrabits));
  double deltaframe = params->lastframe;
  int32_t offset = deltaframe == -1 ? 31 : (int32_t)(params->framenum - deltaframe);
  int32_t encoded = ((int32_t)params->framenum & 0x07ffffff) | (offset << 27);
  qa_q2_msg_write_long(msg, (double)encoded);
  int32_t suppress = ((int32_t)params->surpress_count & 0x0f) | ((e.extraflags & 0x0f) << 4);
  qa_q2_msg_write_byte(msg, (double)suppress);
  qa_q2_msg_write_byte(msg, params->areabytes);
  qa_q2_sz_write(msg, params->areabits, params->areabits_length,
                 (int64_t)params->areabytes);
  qa_q2_msg_write_short(msg, (double)e.flags);
  if (e.flags & 0x8000) qa_q2_msg_write_byte(msg, (double)((uint32_t)e.flags >> 16));
  write_player_body(ctx, &e, msg);
  write_entities(wctx, msg);
}

static void w_usercmd(qa_q2_protocol_codec *self, qa_q2_sizebuf *msg,
                      const qa_q2_wire_usercmd *from, const qa_q2_wire_usercmd *cmd) {
  (void)self;
  qa_q2_msg_write_delta_usercmd(msg, from, cmd);
}
static void r_usercmd(qa_q2_protocol_codec *self, qa_q2_sizebuf *msg,
                      const qa_q2_wire_usercmd *from, qa_q2_wire_usercmd *move) {
  (void)self;
  qa_q2_msg_read_delta_usercmd(msg, from, move);
}

static void decode_batch_cmd(void *vctx, qa_q2_bit_reader *br,
                             const qa_q2_wire_usercmd *prev, qa_q2_wire_usercmd *out) {
  (void)vctx;
  qa_q2_batch_move_seed_from_prev(prev, out);
  int32_t has_contents = qa_q2_bit_reader_read_unsigned(br, 1);
  if (has_contents == 0) return;
  int32_t bits = qa_q2_bit_reader_read_unsigned(br, 8);
  if (bits & QA_Q2_CM_ANGLE1) out->angles[0] = (int16_t)qa_q2_read_batch_move_angle_component(br, prev != NULL ? prev->angles[0] : 0);
  if (bits & QA_Q2_CM_ANGLE2) out->angles[1] = (int16_t)qa_q2_read_batch_move_angle_component(br, prev != NULL ? prev->angles[1] : 0);
  if (bits & QA_Q2_CM_ANGLE3) out->angles[2] = (int16_t)qa_q2_bit_reader_read_signed(br, 16);
  if (bits & QA_Q2_CM_FORWARD) out->forwardmove = (double)qa_q2_bit_reader_read_signed(br, 10);
  if (bits & QA_Q2_CM_SIDE) out->sidemove = (double)qa_q2_bit_reader_read_signed(br, 10);
  if (bits & QA_Q2_CM_UP) out->upmove = (double)qa_q2_bit_reader_read_signed(br, 10);
  if (bits & QA_Q2_CM_BUTTONS) {
    int32_t raw = qa_q2_bit_reader_read_unsigned(br, 3);
    out->buttons = (double)((raw & 3) | ((raw & 4) << 5));
  }
  if (bits & QA_Q2_CM_IMPULSE) out->msec = (double)qa_q2_bit_reader_read_unsigned(br, 8);
  else out->msec = prev != NULL ? prev->msec : 0;
}

static void encode_batch_cmd(void *vctx, qa_q2_bit_writer *bw,
                             const qa_q2_wire_usercmd *cmd,
                             const qa_q2_wire_usercmd *prev) {
  (void)vctx;
  qa_q2_wire_usercmd empty;
  qa_q2_wire_usercmd_init(&empty);
  const qa_q2_wire_usercmd *from = prev != NULL ? prev : &empty;
  int32_t bits = 0;
  if (cmd->angles[0] != from->angles[0]) bits |= QA_Q2_CM_ANGLE1;
  if (cmd->angles[1] != from->angles[1]) bits |= QA_Q2_CM_ANGLE2;
  if (cmd->angles[2] != from->angles[2]) bits |= QA_Q2_CM_ANGLE3;
  if (cmd->forwardmove != from->forwardmove) bits |= QA_Q2_CM_FORWARD;
  if (cmd->sidemove != from->sidemove) bits |= QA_Q2_CM_SIDE;
  if (cmd->upmove != from->upmove) bits |= QA_Q2_CM_UP;
  if (cmd->buttons != from->buttons) bits |= QA_Q2_CM_BUTTONS;
  if (cmd->msec != from->msec) bits |= QA_Q2_CM_IMPULSE;
  qa_q2_bit_writer_write_unsigned(bw, bits == 0 ? 0 : 1, 1);
  if (bits == 0) return;
  qa_q2_bit_writer_write_unsigned(bw, bits, 8);
  for (int axis = 0; axis < 2; axis++) {
    int32_t bit = axis == 0 ? QA_Q2_CM_ANGLE1 : QA_Q2_CM_ANGLE2;
    if ((bits & bit) != 0) {
      int32_t value = cmd->angles[axis];
      int32_t delta = value - from->angles[axis];
      bool small = delta >= -128 && delta <= 127;
      qa_q2_bit_writer_write_unsigned(bw, small ? 1 : 0, 1);
      qa_q2_bit_writer_write_signed(bw, small ? delta : value, small ? 8 : 16);
    }
  }
  if (bits & QA_Q2_CM_ANGLE3) qa_q2_bit_writer_write_signed(bw, cmd->angles[2], 16);
  if (bits & QA_Q2_CM_FORWARD) qa_q2_bit_writer_write_signed(bw, (int32_t)cmd->forwardmove, 10);
  if (bits & QA_Q2_CM_SIDE) qa_q2_bit_writer_write_signed(bw, (int32_t)cmd->sidemove, 10);
  if (bits & QA_Q2_CM_UP) qa_q2_bit_writer_write_signed(bw, (int32_t)cmd->upmove, 10);
  if (bits & QA_Q2_CM_BUTTONS) {
    int32_t b = (int32_t)cmd->buttons;
    qa_q2_bit_writer_write_unsigned(bw, (b & 3) | ((b >> 5) & 4), 3);
  }
  if (bits & QA_Q2_CM_IMPULSE) qa_q2_bit_writer_write_unsigned(bw, (int32_t)cmd->msec, 8);
}

static void write_batch_move(qa_q2_protocol_codec *self, qa_q2_sizebuf *msg,
                             bool has_lastframe, double lastframe,
                             const qa_q2_clc_batch_move_frame *frames,
                             size_t frame_count) {
  (void)self;
  bool too_many = false;
  for (size_t i = 0; i < frame_count; i++) {
    if (frames[i].cmd_count > 31) too_many = true;
  }
  if (frame_count < 1 || frame_count >= QA_Q2_MAX_CLC_BATCH_MOVE_FRAMES || too_many) {
    qa_q2_batch_move_error("Q2PRO command batch exceeds wire bounds");
  }
  if (has_lastframe) qa_q2_msg_write_long(msg, lastframe);
  double lightlevel = 0;
  if (frame_count > 0 && frames[frame_count - 1].cmd_count > 0) {
    lightlevel = frames[frame_count - 1].cmds[frames[frame_count - 1].cmd_count - 1].lightlevel;
  }
  qa_q2_msg_write_byte(msg, lightlevel);
  qa_q2_bit_writer bw;
  qa_q2_bit_writer_init(&bw, msg);
  qa_q2_write_batch_move_frames(&bw, frames, frame_count, encode_batch_cmd, NULL);
}

static bool read_batch_move(qa_q2_protocol_codec *self, qa_q2_sizebuf *msg,
                            bool nodelta, double opcode_extra,
                            qa_q2_clc_batch_move *out) {
  (void)self;
  double num_dups = opcode_extra;
  if (num_dups >= QA_Q2_MAX_CLC_BATCH_MOVE_FRAMES - 1) {
    qa_q2_batch_move_error(
        "clc_q2pro_move_batched (36): num_dups out of range (Q2P_ERR_BAD_DATA, q2proto_proto_q2pro.c:2481-2482)");
  }
  double lastframe = nodelta ? -1 : (double)qa_q2_msg_read_long(msg);
  int32_t lightlevel = qa_q2_msg_read_byte(msg);
  if (lightlevel < 0) {
    qa_q2_batch_move_error("clc_q2pro_move_batched (36): truncated message (lightlevel)");
  }
  qa_q2_bit_reader br;
  qa_q2_bit_reader_init(&br, msg);
  out->lastframe = lastframe;
  out->num_dups = num_dups;
  return qa_q2_read_batch_move_frames(&br, (int32_t)num_dups, decode_batch_cmd, NULL,
                                      &out->frames, &out->frame_count);
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
  qa_q2pro_context *ctx = ctx_of(self);
  qa_q2_sizebuf *m = self->net_message;
  memset(out, 0, sizeof *out);
  out->servercount = (double)qa_q2_msg_read_long(m);
  out->attractloop = qa_q2_msg_read_byte(m) != 0;
  qa_q2_msg_read_string(m, out->gamedir, sizeof out->gamedir);
  out->clientnum = (double)qa_q2_msg_read_short(m);
  qa_q2_msg_read_string(m, out->levelname, sizeof out->levelname);
  int32_t version = qa_q2_msg_read_word(m);
  ctx->features.revision = version != 0 ? (double)version : ctx->minor_version;
  out->server_state = (double)qa_q2_msg_read_byte(m);
  if (ctx->features.revision >= 1024) {
    ctx->features.flags = (double)qa_q2_msg_read_word(m);
  } else {
    int32_t a = qa_q2_msg_read_byte(m);
    int32_t b = qa_q2_msg_read_byte(m);
    int32_t c = qa_q2_msg_read_byte(m);
    ctx->features.flags = (double)((a != 0 ? 1 : 0) | (b != 0 ? 2 : 0) | (c != 0 ? 4 : 0));
  }
  int32_t f = (int32_t)ctx->features.flags;
  out->has_q2pro_version = true;
  out->q2pro_version = ctx->features.revision;
  out->has_wire_flags = true;
  out->wire_flags = ctx->features.flags;
  out->has_q2pro_strafejump_hack = true;
  out->q2pro_strafejump_hack = (f & 1) != 0;
  out->has_q2pro_qw_mode = true;
  out->q2pro_qw_mode = (f & 2) != 0;
  out->has_q2pro_waterjump_hack = true;
  out->q2pro_waterjump_hack = (f & 4) != 0;
}

static void read_entity_bits(qa_q2_protocol_codec *self, qa_q2_entity_bits *out) {
  qa_q2pro_read_entity_bits(self->net_message, out);
}
static void read_delta_entity(qa_q2_protocol_codec *self,
                              const qa_q2_wire_entity *from,
                              qa_q2_wire_entity *to, double number, uint64_t bits) {
  qa_q2pro_context *ctx = ctx_of(self);
  qa_q2pro_read_entity(self->net_message, ctx->features, from, to, number, bits);
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
  dst->gunskin = src->gunskin;
  dst->clientnum = src->clientnum;
  dst->q2pro_fog = src->q2pro_fog;
  memcpy(dst->damage_blend, src->damage_blend, sizeof dst->damage_blend);
  dst->gunframe = src->gunframe;
  memcpy(dst->blend, src->blend, sizeof dst->blend);
  dst->fov = src->fov;
  dst->rdflags = src->rdflags;
  memcpy(dst->stats, src->stats, sizeof dst->stats);
}

static void read_player_body(qa_q2pro_context *ctx, qa_q2_sizebuf *msg,
                             const qa_q2_wire_player *from, qa_q2_wire_player *to,
                             int32_t flags, int32_t extraflags) {
  copy_player(to, from);
  qa_q2_wire_player *t = to;
  bool extended = qa_q2pro_extensions(ctx->features);
  bool v2 = qa_q2pro_extensions_v2(ctx->features);
  if (flags & QA_Q2_PS_M_TYPE) t->pmove.pm_type = (double)qa_q2_msg_read_byte(msg);
  if (flags & QA_Q2_PS_M_ORIGIN) {
    t->pmove.origin[0] = v2 ? qa_q2pro_read_int23(msg, from->pmove.origin[0]) : qa_q2_msg_read_short(msg);
    t->pmove.origin[1] = v2 ? qa_q2pro_read_int23(msg, from->pmove.origin[1]) : qa_q2_msg_read_short(msg);
  }
  if (extraflags & QA_Q2_EPS_M_ORIGIN2) t->pmove.origin[2] = v2 ? qa_q2pro_read_int23(msg, from->pmove.origin[2]) : qa_q2_msg_read_short(msg);
  if (flags & QA_Q2_PS_M_VELOCITY) {
    t->pmove.velocity[0] = v2 ? qa_q2pro_read_int23(msg, from->pmove.velocity[0]) : qa_q2_msg_read_short(msg);
    t->pmove.velocity[1] = v2 ? qa_q2pro_read_int23(msg, from->pmove.velocity[1]) : qa_q2_msg_read_short(msg);
  }
  if (extraflags & QA_Q2_EPS_M_VELOCITY2) t->pmove.velocity[2] = v2 ? qa_q2pro_read_int23(msg, from->pmove.velocity[2]) : qa_q2_msg_read_short(msg);
  if (flags & QA_Q2_PS_M_TIME) t->pmove.pm_time = v2 ? (double)qa_q2_msg_read_word(msg) : (double)qa_q2_msg_read_byte(msg);
  if (flags & QA_Q2_PS_M_FLAGS) t->pmove.pm_flags = v2 ? (double)qa_q2_msg_read_word(msg) : (double)qa_q2_msg_read_byte(msg);
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
  if (flags & QA_Q2_PS_WEAPONINDEX) {
    if (extended) {
      int32_t gun = qa_q2_msg_read_word(msg);
      t->gunindex = (double)(gun & 8191);
      t->gunskin = (double)((uint32_t)gun >> 13);
    } else {
      t->gunindex = (double)qa_q2_msg_read_byte(msg);
    }
  }
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
    if (v2) {
      int32_t bbits = qa_q2_msg_read_byte(msg);
      for (int i = 0; i < 4; i++) {
        if (bbits & (1 << i)) t->blend[i] = (float)((double)qa_q2_msg_read_byte(msg) / 255.0);
      }
      for (int i = 0; i < 4; i++) {
        if (bbits & (16 << i)) t->damage_blend[i] = (float)((double)qa_q2_msg_read_byte(msg) / 255.0);
      }
    } else {
      for (int i = 0; i < 4; i++) t->blend[i] = (float)((double)qa_q2_msg_read_byte(msg) / 255.0);
    }
  }
  if (flags & 0x10000) qa_q2pro_read_fog(msg, &from->q2pro_fog, &t->q2pro_fog);
  if (flags & QA_Q2_PS_FOV) t->fov = (double)qa_q2_msg_read_byte(msg);
  if (flags & QA_Q2_PS_RDFLAGS) t->rdflags = (double)qa_q2_msg_read_byte(msg);
  if (extraflags & QA_Q2_EPS_STATS) {
    uint64_t statbits = v2 ? qa_q2pro_read_var64(msg)
                           : (uint64_t)(uint32_t)qa_q2_msg_read_long(msg);
    int n = v2 ? 64 : 32;
    for (int i = 0; i < n; i++) {
      if ((statbits & (1ull << i)) != 0) t->stats[i] = (int16_t)qa_q2_msg_read_short(msg);
    }
  }
  if (extraflags & 64) {
    t->clientnum = ctx->features.revision >= 1022 ? (double)qa_q2_msg_read_short(msg)
                                                   : (double)qa_q2_msg_read_byte(msg);
  }
}

static void read_player_state_delta(qa_q2_protocol_codec *self, qa_q2_sizebuf *msg,
                                    const qa_q2_wire_player *from,
                                    qa_q2_wire_player *to) {
  qa_q2pro_context *ctx = ctx_of(self);
  int32_t flags = qa_q2_msg_read_word(msg);
  if (ctx->features.revision >= 1026 && (flags & 0x8000) != 0) {
    flags |= qa_q2_msg_read_byte(msg) << 16;
  }
  int32_t extraflags = qa_q2_msg_read_byte(msg);
  read_player_body(ctx, msg, from, to, flags, extraflags);
}

static void read_frame_header(qa_q2_protocol_codec *self, uint8_t *areabits,
                              size_t areabits_length, bool read_suppress_byte,
                              qa_q2_frame_header *out) {
  (void)read_suppress_byte;
  qa_q2pro_context *ctx = ctx_of(self);
  qa_q2_sizebuf *m = self->net_message;
  int32_t encoded = qa_q2_msg_read_long(m);
  uint32_t uoffset = (uint32_t)encoded >> 27;
  int32_t serverframe = encoded & 0x07ffffff;
  int32_t deltaframe = uoffset == 31u ? -1 : serverframe - (int32_t)uoffset;
  int32_t high = (ctx->pending_opcode_extrabits >> 1) & 0x70;
  ctx->pending_opcode_extrabits = 0;
  int32_t raw = qa_q2_msg_read_byte(m);
  int32_t low = (raw & 0xf0) >> 4;
  ctx->pending_frame_extraflags = high | low;
  int32_t len = qa_q2_msg_read_byte(m);
  qa_q2_msg_read_data(m, areabits, areabits_length, len);
  out->serverframe = (double)serverframe;
  out->deltaframe = (double)deltaframe;
  out->surpress_count = (double)(raw & 0x0f);
  out->areabytes = (double)len;
}

static void read_frame_playerstate(qa_q2_protocol_codec *self,
                                   const qa_q2_wire_player *from,
                                   qa_q2_wire_player *to) {
  qa_q2pro_context *ctx = ctx_of(self);
  qa_q2_sizebuf *m = self->net_message;
  int32_t flags = qa_q2_msg_read_word(m);
  if (ctx->features.revision >= 1026 && (flags & 0x8000) != 0) {
    flags |= qa_q2_msg_read_byte(m) << 16;
  }
  read_player_body(ctx, m, from, to, flags, ctx->pending_frame_extraflags);
}

void qa_q2pro_note_frame_opcode_extrabits(qa_q2pro_context *ctx, int32_t extrabits) {
  ctx->pending_opcode_extrabits = extrabits;
}

qa_q2_protocol_codec *qa_q2pro_create_codec(qa_q2pro_context *ctx, double minor_version) {
  ctx->features.revision = minor_version;
  ctx->minor_version = minor_version;
  return &ctx->codec;
}

void qa_q2pro_context_init(qa_q2pro_context *ctx, qa_q2_sizebuf *net_message) {
  memset(ctx, 0, sizeof *ctx);
  ctx->net_message = net_message;
  ctx->features.revision = 1015;
  ctx->features.flags = 0;
  ctx->codec.name = "q2pro";
  ctx->codec.net_message = net_message;
  ctx->codec.ctx = ctx;
  ctx->codec.write_server_data = write_server_data;
  ctx->codec.write_delta_entity = write_delta_entity;
  ctx->codec.write_entity_remove = write_entity_remove;
  ctx->codec.write_packet_entities_end = write_entities_end;
  ctx->codec.write_spawn_baseline = write_spawn_baseline;
  ctx->codec.write_player_state_delta = write_player_state_delta;
  ctx->codec.write_packet_entities_begin = noop_w;
  ctx->codec.write_frame = write_frame;
  ctx->codec.write_delta_usercmd = w_usercmd;
  ctx->codec.read_delta_usercmd = r_usercmd;
  ctx->codec.read_server_data = read_server_data;
  ctx->codec.read_entity_bits = read_entity_bits;
  ctx->codec.read_delta_entity = read_delta_entity;
  ctx->codec.read_player_state_delta = read_player_state_delta;
  ctx->codec.read_frame_header = read_frame_header;
  ctx->codec.read_frame_playerstate = read_frame_playerstate;
  ctx->codec.read_packet_entities_begin = noop_r;
  ctx->codec.read_batch_move = read_batch_move;
  ctx->codec.write_batch_move = write_batch_move;
  ctx->codec.read_userinfo_delta = read_userinfo_delta;
  ctx->codec.read_client_setting = read_client_setting;
  qa_q2pro_create_codec(ctx, QA_Q2_PROTOCOL_VERSION_Q2PRO_CURRENT);
}
