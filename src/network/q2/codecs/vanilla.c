/* Port of src/network/q2/codecs/vanilla.ts. GPL-2.0-or-later. */
#include "qa/network/q2/codecs/vanilla.h"

#include <string.h>

#include "qa/network/q2/constants.h"
#include "qa/network/q2/errors.h"

static qa_q2_vanilla_context *self_ctx(qa_q2_protocol_codec *self) {
  return (qa_q2_vanilla_context *)self->ctx;
}

static void write_server_data(qa_q2_protocol_codec *self, qa_q2_sizebuf *msg,
                              const qa_q2_server_data_params *params) {
  (void)self;
  qa_q2_msg_write_byte(msg, QA_Q2_SVC_SERVERDATA);
  qa_q2_msg_write_long(msg, QA_Q2_PROTOCOL_VERSION);
  qa_q2_msg_write_long(msg, params->servercount);
  qa_q2_msg_write_byte(msg, params->attractloop ? 1 : 0);
  qa_q2_msg_write_string(msg, params->gamedir);
  qa_q2_msg_write_short(msg, params->clientnum);
  qa_q2_msg_write_string(msg, params->levelname);
}

static void write_delta_entity(qa_q2_protocol_codec *self, qa_q2_sizebuf *msg,
                               const qa_q2_wire_entity *from,
                               const qa_q2_wire_entity *to, bool force,
                               bool newentity) {
  (void)self;
  qa_q2_msg_write_delta_entity(from, to, msg, force, newentity);
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

static void write_packet_entities_end(qa_q2_protocol_codec *self, qa_q2_sizebuf *msg) {
  (void)self;
  qa_q2_msg_write_short(msg, 0);
}
static void write_packet_entities_begin(qa_q2_protocol_codec *self, qa_q2_sizebuf *msg) {
  (void)self;
  qa_q2_msg_write_byte(msg, QA_Q2_SVC_PACKETENTITIES);
}

static void write_spawn_baseline(qa_q2_protocol_codec *self, qa_q2_sizebuf *msg,
                                 const qa_q2_wire_entity *base) {
  (void)self;
  qa_q2_wire_entity null_state;
  qa_q2_wire_entity_init(&null_state);
  qa_q2_msg_write_byte(msg, QA_Q2_SVC_SPAWNBASELINE);
  qa_q2_msg_write_delta_entity(&null_state, base, msg, true, true);
}

static void write_player_state_delta(qa_q2_protocol_codec *self, qa_q2_sizebuf *msg,
                                     const qa_q2_wire_player *from,
                                     const qa_q2_wire_player *to) {
  (void)self;
  const qa_q2_wire_player *ps = to;
  const qa_q2_wire_player *ops = from;
  int32_t pflags = 0;
  if (ps->pmove.pm_type != ops->pmove.pm_type) pflags |= QA_Q2_PS_M_TYPE;
  if (ps->pmove.origin[0] != ops->pmove.origin[0] ||
      ps->pmove.origin[1] != ops->pmove.origin[1] ||
      ps->pmove.origin[2] != ops->pmove.origin[2]) pflags |= QA_Q2_PS_M_ORIGIN;
  if (ps->pmove.velocity[0] != ops->pmove.velocity[0] ||
      ps->pmove.velocity[1] != ops->pmove.velocity[1] ||
      ps->pmove.velocity[2] != ops->pmove.velocity[2]) pflags |= QA_Q2_PS_M_VELOCITY;
  if (ps->pmove.pm_time != ops->pmove.pm_time) pflags |= QA_Q2_PS_M_TIME;
  if (ps->pmove.pm_flags != ops->pmove.pm_flags) pflags |= QA_Q2_PS_M_FLAGS;
  if (ps->pmove.gravity != ops->pmove.gravity) pflags |= QA_Q2_PS_M_GRAVITY;
  if (ps->pmove.delta_angles[0] != ops->pmove.delta_angles[0] ||
      ps->pmove.delta_angles[1] != ops->pmove.delta_angles[1] ||
      ps->pmove.delta_angles[2] != ops->pmove.delta_angles[2]) {
    pflags |= QA_Q2_PS_M_DELTA_ANGLES;
  }
  if (ps->viewoffset[0] != ops->viewoffset[0] ||
      ps->viewoffset[1] != ops->viewoffset[1] ||
      ps->viewoffset[2] != ops->viewoffset[2]) pflags |= QA_Q2_PS_VIEWOFFSET;
  if (ps->viewangles[0] != ops->viewangles[0] ||
      ps->viewangles[1] != ops->viewangles[1] ||
      ps->viewangles[2] != ops->viewangles[2]) pflags |= QA_Q2_PS_VIEWANGLES;
  if (ps->kick_angles[0] != ops->kick_angles[0] ||
      ps->kick_angles[1] != ops->kick_angles[1] ||
      ps->kick_angles[2] != ops->kick_angles[2]) pflags |= QA_Q2_PS_KICKANGLES;
  if (ps->blend[0] != ops->blend[0] || ps->blend[1] != ops->blend[1] ||
      ps->blend[2] != ops->blend[2] || ps->blend[3] != ops->blend[3]) {
    pflags |= QA_Q2_PS_BLEND;
  }
  if (ps->fov != ops->fov) pflags |= QA_Q2_PS_FOV;
  if (ps->rdflags != ops->rdflags) pflags |= QA_Q2_PS_RDFLAGS;
  if (ps->gunframe != ops->gunframe) pflags |= QA_Q2_PS_WEAPONFRAME;
  pflags |= QA_Q2_PS_WEAPONINDEX;
  qa_q2_msg_write_byte(msg, QA_Q2_SVC_PLAYERINFO);
  qa_q2_msg_write_short(msg, (double)pflags);
  if (pflags & QA_Q2_PS_M_TYPE) qa_q2_msg_write_byte(msg, ps->pmove.pm_type);
  if (pflags & QA_Q2_PS_M_ORIGIN) {
    qa_q2_msg_write_short(msg, (double)ps->pmove.origin[0]);
    qa_q2_msg_write_short(msg, (double)ps->pmove.origin[1]);
    qa_q2_msg_write_short(msg, (double)ps->pmove.origin[2]);
  }
  if (pflags & QA_Q2_PS_M_VELOCITY) {
    qa_q2_msg_write_short(msg, (double)ps->pmove.velocity[0]);
    qa_q2_msg_write_short(msg, (double)ps->pmove.velocity[1]);
    qa_q2_msg_write_short(msg, (double)ps->pmove.velocity[2]);
  }
  if (pflags & QA_Q2_PS_M_TIME) qa_q2_msg_write_byte(msg, ps->pmove.pm_time);
  if (pflags & QA_Q2_PS_M_FLAGS) qa_q2_msg_write_byte(msg, ps->pmove.pm_flags);
  if (pflags & QA_Q2_PS_M_GRAVITY) qa_q2_msg_write_short(msg, ps->pmove.gravity);
  if (pflags & QA_Q2_PS_M_DELTA_ANGLES) {
    qa_q2_msg_write_short(msg, (double)ps->pmove.delta_angles[0]);
    qa_q2_msg_write_short(msg, (double)ps->pmove.delta_angles[1]);
    qa_q2_msg_write_short(msg, (double)ps->pmove.delta_angles[2]);
  }
  if (pflags & QA_Q2_PS_VIEWOFFSET) {
    qa_q2_msg_write_char(msg, (double)ps->viewoffset[0] * 4.0);
    qa_q2_msg_write_char(msg, (double)ps->viewoffset[1] * 4.0);
    qa_q2_msg_write_char(msg, (double)ps->viewoffset[2] * 4.0);
  }
  if (pflags & QA_Q2_PS_VIEWANGLES) {
    qa_q2_msg_write_angle16(msg, (double)ps->viewangles[0]);
    qa_q2_msg_write_angle16(msg, (double)ps->viewangles[1]);
    qa_q2_msg_write_angle16(msg, (double)ps->viewangles[2]);
  }
  if (pflags & QA_Q2_PS_KICKANGLES) {
    qa_q2_msg_write_char(msg, (double)ps->kick_angles[0] * 4.0);
    qa_q2_msg_write_char(msg, (double)ps->kick_angles[1] * 4.0);
    qa_q2_msg_write_char(msg, (double)ps->kick_angles[2] * 4.0);
  }
  if (pflags & QA_Q2_PS_WEAPONINDEX) qa_q2_msg_write_byte(msg, ps->gunindex);
  if (pflags & QA_Q2_PS_WEAPONFRAME) {
    qa_q2_msg_write_byte(msg, ps->gunframe);
    qa_q2_msg_write_char(msg, (double)ps->gunoffset[0] * 4.0);
    qa_q2_msg_write_char(msg, (double)ps->gunoffset[1] * 4.0);
    qa_q2_msg_write_char(msg, (double)ps->gunoffset[2] * 4.0);
    qa_q2_msg_write_char(msg, (double)ps->gunangles[0] * 4.0);
    qa_q2_msg_write_char(msg, (double)ps->gunangles[1] * 4.0);
    qa_q2_msg_write_char(msg, (double)ps->gunangles[2] * 4.0);
  }
  if (pflags & QA_Q2_PS_BLEND) {
    qa_q2_msg_write_byte(msg, (double)ps->blend[0] * 255.0);
    qa_q2_msg_write_byte(msg, (double)ps->blend[1] * 255.0);
    qa_q2_msg_write_byte(msg, (double)ps->blend[2] * 255.0);
    qa_q2_msg_write_byte(msg, (double)ps->blend[3] * 255.0);
  }
  if (pflags & QA_Q2_PS_FOV) qa_q2_msg_write_byte(msg, ps->fov);
  if (pflags & QA_Q2_PS_RDFLAGS) qa_q2_msg_write_byte(msg, ps->rdflags);
  int32_t statbits = 0;
  for (int i = 0; i < QA_Q2_MAX_STATS; i++) {
    if (ps->stats[i] != ops->stats[i]) statbits |= (int32_t)(1u << i);
  }
  qa_q2_msg_write_long(msg, (double)statbits);
  for (int i = 0; i < QA_Q2_MAX_STATS; i++) {
    if (statbits & ((int32_t)(1u << i))) qa_q2_msg_write_short(msg, (double)ps->stats[i]);
  }
}

static void write_frame(qa_q2_protocol_codec *self, qa_q2_sizebuf *msg,
                        const qa_q2_frame_write_params *params,
                        qa_q2_write_entities_fn write_entities, void *ctx) {
  qa_q2_msg_write_byte(msg, QA_Q2_SVC_FRAME);
  qa_q2_msg_write_long(msg, params->framenum);
  qa_q2_msg_write_long(msg, params->lastframe);
  qa_q2_msg_write_byte(msg, params->surpress_count);
  qa_q2_msg_write_byte(msg, params->areabytes);
  qa_q2_sz_write(msg, params->areabits, params->areabits_length,
                 (int64_t)params->areabytes);
  qa_q2_wire_player empty;
  qa_q2_wire_player_init(&empty);
  write_player_state_delta(self, msg,
                           params->ps_from != NULL ? params->ps_from : &empty,
                           params->ps_to);
  write_entities(ctx, msg);
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

static void read_server_data(qa_q2_protocol_codec *self,
                             qa_q2_server_data_result *out) {
  qa_q2_sizebuf *m = self->net_message;
  memset(out, 0, sizeof *out);
  out->servercount = (double)qa_q2_msg_read_long(m);
  out->attractloop = qa_q2_msg_read_byte(m) != 0;
  qa_q2_msg_read_string(m, out->gamedir, sizeof out->gamedir);
  out->clientnum = (double)qa_q2_msg_read_short(m);
  qa_q2_msg_read_string(m, out->levelname, sizeof out->levelname);
  out->server_state = 0;
}

static void read_entity_bits(qa_q2_protocol_codec *self, qa_q2_entity_bits *out) {
  qa_q2_vanilla_context *ctx = self_ctx(self);
  qa_q2_sizebuf *m = self->net_message;
  int32_t total = qa_q2_msg_read_byte(m);
  if (total & QA_Q2_U_MOREBITS1) total |= (int32_t)((uint32_t)qa_q2_msg_read_byte(m) << 8);
  if (total & QA_Q2_U_MOREBITS2) total |= (int32_t)((uint32_t)qa_q2_msg_read_byte(m) << 16);
  if (total & QA_Q2_U_MOREBITS3) total |= (int32_t)((uint32_t)qa_q2_msg_read_byte(m) << 24);
  for (int i = 0; i < 32; i++) {
    /* Int32Array store of readElement + 1: wraps at 2^31. */
    if (total & ((int32_t)(1u << i))) ctx->bitcounts[i] = (int32_t)((uint32_t)ctx->bitcounts[i] + 1u);
  }
  double number = (total & QA_Q2_U_NUMBER16) != 0
                      ? (double)qa_q2_msg_read_short(m)
                      : (double)qa_q2_msg_read_byte(m);
  out->number = number;
  out->bits = (double)(uint32_t)total;
  out->bits_u64 = (uint32_t)total;
}

static void copy_entity(qa_q2_wire_entity *dst, const qa_q2_wire_entity *src) {
  dst->number = src->number;
  qa_q2_vector_copy(src->origin, dst->origin);
  qa_q2_vector_copy(src->angles, dst->angles);
  qa_q2_vector_copy(src->old_origin, dst->old_origin);
  dst->modelindex = src->modelindex;
  dst->modelindex2 = src->modelindex2;
  dst->modelindex3 = src->modelindex3;
  dst->modelindex4 = src->modelindex4;
  dst->frame = src->frame;
  dst->skinnum = src->skinnum;
  dst->effects = src->effects;
  dst->renderfx = src->renderfx;
  dst->solid = src->solid;
  dst->sound = src->sound;
  dst->event = src->event;
}

static void read_delta_entity(qa_q2_protocol_codec *self,
                              const qa_q2_wire_entity *from,
                              qa_q2_wire_entity *to, double number, uint64_t bits64) {
  qa_q2_sizebuf *m = self->net_message;
  int32_t bits = (int32_t)bits64;
  copy_entity(to, from);
  qa_q2_vector_copy(from->origin, to->old_origin);
  to->number = number;
  if (bits & QA_Q2_U_MODEL) to->modelindex = (double)qa_q2_msg_read_byte(m);
  if (bits & QA_Q2_U_MODEL2) to->modelindex2 = (double)qa_q2_msg_read_byte(m);
  if (bits & QA_Q2_U_MODEL3) to->modelindex3 = (double)qa_q2_msg_read_byte(m);
  if (bits & QA_Q2_U_MODEL4) to->modelindex4 = (double)qa_q2_msg_read_byte(m);
  if (bits & QA_Q2_U_FRAME8) to->frame = (double)qa_q2_msg_read_byte(m);
  if (bits & QA_Q2_U_FRAME16) to->frame = (double)qa_q2_msg_read_short(m);
  if ((bits & QA_Q2_U_SKIN8) != 0 && (bits & QA_Q2_U_SKIN16) != 0) {
    to->skinnum = (double)qa_q2_msg_read_long(m);
  } else if (bits & QA_Q2_U_SKIN8) {
    to->skinnum = (double)qa_q2_msg_read_byte(m);
  } else if (bits & QA_Q2_U_SKIN16) {
    to->skinnum = (double)qa_q2_msg_read_short(m);
  }
  if ((bits & (QA_Q2_U_EFFECTS8 | QA_Q2_U_EFFECTS16)) ==
      (QA_Q2_U_EFFECTS8 | QA_Q2_U_EFFECTS16)) {
    to->effects = (double)(uint32_t)qa_q2_msg_read_long(m); /* >>> 0 */
  } else if (bits & QA_Q2_U_EFFECTS8) {
    to->effects = (double)qa_q2_msg_read_byte(m);
  } else if (bits & QA_Q2_U_EFFECTS16) {
    to->effects = (double)qa_q2_msg_read_short(m);
  }
  if ((bits & (QA_Q2_U_RENDERFX8 | QA_Q2_U_RENDERFX16)) ==
      (QA_Q2_U_RENDERFX8 | QA_Q2_U_RENDERFX16)) {
    to->renderfx = (double)qa_q2_msg_read_long(m);
  } else if (bits & QA_Q2_U_RENDERFX8) {
    to->renderfx = (double)qa_q2_msg_read_byte(m);
  } else if (bits & QA_Q2_U_RENDERFX16) {
    to->renderfx = (double)qa_q2_msg_read_short(m);
  }
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
  if (bits & QA_Q2_U_SOLID) to->solid = (double)qa_q2_msg_read_short(m);
}

static void copy_player(qa_q2_wire_player *dst, const qa_q2_wire_player *src) {
  dst->pmove.pm_type = src->pmove.pm_type;
  memcpy(dst->pmove.origin, src->pmove.origin, sizeof dst->pmove.origin);
  memcpy(dst->pmove.velocity, src->pmove.velocity, sizeof dst->pmove.velocity);
  dst->pmove.pm_flags = src->pmove.pm_flags;
  dst->pmove.pm_time = src->pmove.pm_time;
  dst->pmove.gravity = src->pmove.gravity;
  memcpy(dst->pmove.delta_angles, src->pmove.delta_angles,
         sizeof dst->pmove.delta_angles);
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

static void read_player_state_delta(qa_q2_protocol_codec *self, qa_q2_sizebuf *msg,
                                    const qa_q2_wire_player *from,
                                    qa_q2_wire_player *to) {
  (void)self;
  copy_player(to, from);
  qa_q2_wire_player *t = to;
  int32_t flags = qa_q2_msg_read_short(msg);
  if (flags & QA_Q2_PS_M_TYPE) t->pmove.pm_type = (double)qa_q2_msg_read_byte(msg);
  if (flags & QA_Q2_PS_M_ORIGIN) {
    t->pmove.origin[0] = qa_q2_msg_read_short(msg);
    t->pmove.origin[1] = qa_q2_msg_read_short(msg);
    t->pmove.origin[2] = qa_q2_msg_read_short(msg);
  }
  if (flags & QA_Q2_PS_M_VELOCITY) {
    t->pmove.velocity[0] = qa_q2_msg_read_short(msg);
    t->pmove.velocity[1] = qa_q2_msg_read_short(msg);
    t->pmove.velocity[2] = qa_q2_msg_read_short(msg);
  }
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
    t->viewangles[2] = (float)qa_q2_msg_read_angle16(msg);
  }
  if (flags & QA_Q2_PS_KICKANGLES) {
    t->kick_angles[0] = (float)((double)qa_q2_msg_read_char(msg) * 0.25);
    t->kick_angles[1] = (float)((double)qa_q2_msg_read_char(msg) * 0.25);
    t->kick_angles[2] = (float)((double)qa_q2_msg_read_char(msg) * 0.25);
  }
  if (flags & QA_Q2_PS_WEAPONINDEX) t->gunindex = (double)qa_q2_msg_read_byte(msg);
  if (flags & QA_Q2_PS_WEAPONFRAME) {
    t->gunframe = (double)qa_q2_msg_read_byte(msg);
    t->gunoffset[0] = (float)((double)qa_q2_msg_read_char(msg) * 0.25);
    t->gunoffset[1] = (float)((double)qa_q2_msg_read_char(msg) * 0.25);
    t->gunoffset[2] = (float)((double)qa_q2_msg_read_char(msg) * 0.25);
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
  int32_t statbits = qa_q2_msg_read_long(msg);
  for (int i = 0; i < QA_Q2_MAX_STATS; i++) {
    if (statbits & ((int32_t)(1u << i))) t->stats[i] = (int16_t)qa_q2_msg_read_short(msg);
  }
}

static void read_frame_header(qa_q2_protocol_codec *self, uint8_t *areabits,
                              size_t areabits_length, bool read_suppress_byte,
                              qa_q2_frame_header *out) {
  qa_q2_sizebuf *m = self->net_message;
  double serverframe = (double)qa_q2_msg_read_long(m);
  double deltaframe = (double)qa_q2_msg_read_long(m);
  double surpress = 0;
  if (read_suppress_byte) surpress = (double)qa_q2_msg_read_byte(m);
  int32_t len = qa_q2_msg_read_byte(m);
  qa_q2_msg_read_data(m, areabits, areabits_length, len);
  out->serverframe = serverframe;
  out->deltaframe = deltaframe;
  out->surpress_count = surpress;
  out->areabytes = (double)len;
}

static void read_frame_playerstate(qa_q2_protocol_codec *self,
                                   const qa_q2_wire_player *from,
                                   qa_q2_wire_player *to) {
  qa_q2_sizebuf *m = self->net_message;
  int32_t cmd = qa_q2_msg_read_byte(m);
  if (cmd != QA_Q2_SVC_PLAYERINFO) qa_q2_com_error(QA_Q2_ERR_DROP, "CL_ParseFrame: not playerinfo");
  read_player_state_delta(self, m, from, to);
}

static void read_packet_entities_begin(qa_q2_protocol_codec *self) {
  qa_q2_sizebuf *m = self->net_message;
  int32_t cmd = qa_q2_msg_read_byte(m);
  if (cmd != QA_Q2_SVC_PACKETENTITIES) {
    qa_q2_com_error(QA_Q2_ERR_DROP, "CL_ParseFrame: not packetentities");
  }
}

void qa_q2_vanilla_context_init(qa_q2_vanilla_context *ctx, qa_q2_sizebuf *net_message) {
  memset(ctx, 0, sizeof *ctx);
  ctx->net_message = net_message;
  ctx->codec.name = "vanilla";
  ctx->codec.net_message = net_message;
  ctx->codec.ctx = ctx;
  ctx->codec.has_clc_move_checksum = true;
  ctx->codec.clc_move_has_checksum = true;
  ctx->codec.write_server_data = write_server_data;
  ctx->codec.write_delta_entity = write_delta_entity;
  ctx->codec.write_entity_remove = write_entity_remove;
  ctx->codec.write_packet_entities_end = write_packet_entities_end;
  ctx->codec.write_spawn_baseline = write_spawn_baseline;
  ctx->codec.write_player_state_delta = write_player_state_delta;
  ctx->codec.write_packet_entities_begin = write_packet_entities_begin;
  ctx->codec.write_frame = write_frame;
  ctx->codec.write_delta_usercmd = w_usercmd;
  ctx->codec.read_delta_usercmd = r_usercmd;
  ctx->codec.read_server_data = read_server_data;
  ctx->codec.read_entity_bits = read_entity_bits;
  ctx->codec.read_delta_entity = read_delta_entity;
  ctx->codec.read_player_state_delta = read_player_state_delta;
  ctx->codec.read_frame_header = read_frame_header;
  ctx->codec.read_frame_playerstate = read_frame_playerstate;
  ctx->codec.read_packet_entities_begin = read_packet_entities_begin;
}
