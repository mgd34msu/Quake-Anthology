/* Port of src/network/q2/codecs/kexdemo.ts. GPL-2.0-or-later. */
#include "qa/network/q2/codecs/kexdemo.h"

#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "qa/foundation/support.h"
#include "qa/network/q2/codecs/kex_usercmd.h"
#include "qa/network/q2/constants.h"

#define U_MODEL16_KD (1u << 28)
#define U_EFFECTS64_KD (1u << 29)
#define U_ALPHA_KD (1u << 30)
#define PS_MOREBITS_KD (1 << 15)
#define PS_DAMAGE_BLEND_KD (1 << 16)
#define PS_TEAM_KD (1 << 17)
#define COORD_SHORT_SCALE_KD 0.125

static qa_q2_kex_context *ctx_of(qa_q2_protocol_codec *self) {
  /* The writer binding is the context's first member (see header). */
  return (qa_q2_kex_context *)self->ctx;
}

void qa_q2_kex_set_protocol(qa_q2_kex_context *ctx, double protocol) {
  ctx->protocol = protocol;
}
double qa_q2_kex_split_player_count(const qa_q2_kex_context *ctx) {
  return ctx->split_players;
}
bool qa_q2_kex_is_demo_protocol(const qa_q2_kex_context *ctx) {
  return ctx->protocol == QA_Q2_KEX_PROTOCOL_DEMOS;
}

static void read_server_data(qa_q2_protocol_codec *self,
                             qa_q2_server_data_result *out) {
  qa_q2_kex_context *ctx = ctx_of(self);
  qa_q2_sizebuf *m = self->net_message;
  memset(ctx->edict_solid_known, 0, sizeof ctx->edict_solid_known);
  memset(ctx->edict_solid, 0, sizeof ctx->edict_solid);
  memset(out, 0, sizeof *out);
  out->servercount = (double)qa_q2_msg_read_long(m);
  out->attractloop = qa_q2_msg_read_byte(m) != 0;
  out->has_server_fps = true;
  out->server_fps = (double)qa_q2_msg_read_byte(m);
  qa_q2_msg_read_string(m, out->gamedir, sizeof out->gamedir);
  double clientnum = (double)qa_q2_msg_read_short(m);
  size_t count = 0;
  if (clientnum == -2) {
    int32_t split = qa_q2_msg_read_short(m);
    if (split < 1 || split > 8) {
      qa_q2_com_error_throw(QA_Q2_ERR_DROP, "Invalid KEX split count");
    }
    for (int32_t i = 0; i < split; i++) {
      out->clientnums[count++] = (double)qa_q2_msg_read_short(m);
    }
    clientnum = count > 0 ? out->clientnums[0] : -1;
  } else {
    out->clientnums[count++] = clientnum;
  }
  out->has_clientnums = true;
  out->clientnum_count = count;
  out->clientnum = clientnum;
  ctx->split_players = (double)count;
  qa_q2_msg_read_string(m, out->levelname, sizeof out->levelname);
  out->server_state = 0;
}

static void read_entity_bits(qa_q2_protocol_codec *self, qa_q2_entity_bits *out) {
  qa_q2_kex_context *ctx = ctx_of(self);
  qa_q2repro_read_entity_bits_wide(&ctx->rerelease, out);
}

static void read_delta_entity(qa_q2_protocol_codec *self,
                              const qa_q2_wire_entity *from,
                              qa_q2_wire_entity *to, double number, uint64_t bits64) {
  qa_q2_kex_context *ctx = ctx_of(self);
  qa_q2_sizebuf *m = self->net_message;
  uint32_t bits = (uint32_t)bits64;
  double bits_d = (double)bits64;
  *to = *from;
  to->number = number;
  bool model16 = (bits & U_MODEL16_KD) != 0;
  double *models[4] = {&to->modelindex, &to->modelindex2, &to->modelindex3, &to->modelindex4};
  uint32_t mbits[4] = {(uint32_t)QA_Q2_U_MODEL, (uint32_t)QA_Q2_U_MODEL2,
                       (uint32_t)QA_Q2_U_MODEL3, (uint32_t)QA_Q2_U_MODEL4};
  for (int i = 0; i < 4; i++) {
    if ((bits & mbits[i]) != 0) {
      *models[i] = model16 ? (double)qa_q2_msg_read_word(m) : (double)qa_q2_msg_read_byte(m);
    }
  }
  if (bits & (uint32_t)QA_Q2_U_FRAME8) to->frame = (double)qa_q2_msg_read_byte(m);
  else if (bits & (uint32_t)QA_Q2_U_FRAME16) to->frame = (double)qa_q2_msg_read_word(m);
  if ((bits & ((uint32_t)QA_Q2_U_SKIN8 | (uint32_t)QA_Q2_U_SKIN16)) == ((uint32_t)QA_Q2_U_SKIN8 | (uint32_t)QA_Q2_U_SKIN16)) to->skinnum = (double)qa_q2_msg_read_long(m);
  else if (bits & (uint32_t)QA_Q2_U_SKIN16) to->skinnum = (double)qa_q2_msg_read_word(m);
  else if (bits & (uint32_t)QA_Q2_U_SKIN8) to->skinnum = (double)qa_q2_msg_read_byte(m);
  uint32_t low_effects = 0;
  if ((bits & U_EFFECTS64_KD) != 0) low_effects = (uint32_t)qa_q2_msg_read_long(m);
  uint32_t effects32 = 0;
  uint32_t combo = bits & ((uint32_t)QA_Q2_U_EFFECTS8 | (uint32_t)QA_Q2_U_EFFECTS16);
  if (combo == ((uint32_t)QA_Q2_U_EFFECTS8 | (uint32_t)QA_Q2_U_EFFECTS16)) effects32 = (uint32_t)qa_q2_msg_read_long(m);
  else if ((bits & (uint32_t)QA_Q2_U_EFFECTS16) != 0) effects32 = (uint32_t)qa_q2_msg_read_word(m);
  else if ((bits & (uint32_t)QA_Q2_U_EFFECTS8) != 0) effects32 = (uint32_t)qa_q2_msg_read_byte(m);
  if ((bits & (U_EFFECTS64_KD | (uint32_t)QA_Q2_U_EFFECTS8 | (uint32_t)QA_Q2_U_EFFECTS16)) != 0) {
    if ((bits & U_EFFECTS64_KD) != 0) {
      to->effects = (double)low_effects;
      to->morefx = (double)effects32;
    } else {
      to->effects = (double)effects32;
      to->morefx = 0;
    }
  }
  if ((bits & ((uint32_t)QA_Q2_U_RENDERFX8 | (uint32_t)QA_Q2_U_RENDERFX16)) == ((uint32_t)QA_Q2_U_RENDERFX8 | (uint32_t)QA_Q2_U_RENDERFX16)) to->renderfx = (double)qa_q2_msg_read_long(m);
  else if (bits & (uint32_t)QA_Q2_U_RENDERFX16) to->renderfx = (double)qa_q2_msg_read_word(m);
  else if (bits & (uint32_t)QA_Q2_U_RENDERFX8) to->renderfx = (double)qa_q2_msg_read_byte(m);
  bool nonzero_solid;
  int ent = (int)number;
  if ((bits & (uint32_t)QA_Q2_U_SOLID) != 0) {
    to->solid = (double)(uint32_t)qa_q2_msg_read_long(m);
    nonzero_solid = to->solid != 0;
    if (ent >= 0 && ent < QA_Q2_MAX_EDICTS) {
      ctx->edict_solid_known[ent] = true;
      ctx->edict_solid[ent] = nonzero_solid;
    }
  } else {
    nonzero_solid = ent >= 0 && ent < QA_Q2_MAX_EDICTS && ctx->edict_solid_known[ent]
                        ? ctx->edict_solid[ent]
                        : false;
  }
  bool high_precision = ctx->protocol != QA_Q2_KEX_PROTOCOL_DEMOS || nonzero_solid;
  uint32_t obits[3] = {(uint32_t)QA_Q2_U_ORIGIN1, (uint32_t)QA_Q2_U_ORIGIN2, (uint32_t)QA_Q2_U_ORIGIN3};
  for (int i = 0; i < 3; i++) {
    if ((bits & obits[i]) != 0) {
      to->origin[i] = high_precision ? (float)qa_q2_msg_read_float(m)
                                     : (float)((double)qa_q2_msg_read_short(m) * COORD_SHORT_SCALE_KD);
    }
  }
  if ((bits & (uint32_t)QA_Q2_U_OLDORIGIN) != 0) {
    for (int i = 0; i < 3; i++) {
      to->old_origin[i] = high_precision ? (float)qa_q2_msg_read_float(m)
                                         : (float)((double)qa_q2_msg_read_short(m) * COORD_SHORT_SCALE_KD);
    }
  }
  uint32_t abits[3] = {(uint32_t)QA_Q2_U_ANGLE1, (uint32_t)QA_Q2_U_ANGLE2, (uint32_t)QA_Q2_U_ANGLE3};
  for (int i = 0; i < 3; i++) {
    if ((bits & abits[i]) != 0) to->angles[i] = (float)qa_q2_msg_read_float(m);
  }
  if ((bits & (uint32_t)QA_Q2_U_SOUND) != 0) {
    int32_t word = qa_q2_msg_read_word(m);
    to->sound = (double)(word & 0x3fff);
    if (word & QA_Q2REPRO_SOUND_FLAG_VOLUME) to->loop_volume = qa_q2repro_decode_loop_volume((double)qa_q2_msg_read_byte(m));
    if (word & QA_Q2REPRO_SOUND_FLAG_ATTENUATION) to->loop_attenuation = qa_q2repro_decode_loop_attenuation((double)qa_q2_msg_read_byte(m));
  }
  if ((bits & (uint32_t)QA_Q2_U_EVENT) != 0) to->event = (double)qa_q2_msg_read_byte(m);
  else to->event = 0;
  if ((bits & U_ALPHA_KD) != 0) to->alpha = qa_q2repro_decode_alpha((double)qa_q2_msg_read_byte(m));
  if (qa_q2repro_bits_has_hi(bits_d, QA_Q2REPRO_HI_SCALE)) to->scale = qa_q2repro_decode_scale((double)qa_q2_msg_read_byte(m));
  if (qa_q2repro_bits_has_hi(bits_d, QA_Q2REPRO_HI_MOREFX16)) to->instance_bits = (double)qa_q2_msg_read_byte(m);
  if (qa_q2repro_bits_has_hi(bits_d, QA_Q2_KEX_HI_OWNER)) to->owner = (double)qa_q2_msg_read_word(m);
  if (qa_q2repro_bits_has_hi(bits_d, QA_Q2_KEX_HI_OLDFRAME)) to->old_frame = (double)qa_q2_msg_read_word(m);
}

static int32_t read_kex_flags(qa_q2_sizebuf *m) {
  int32_t flags = qa_q2_msg_read_word(m);
  if ((flags & PS_MOREBITS_KD) != 0) flags |= qa_q2_msg_read_word(m) << 16;
  return flags;
}

static void read_player_fields(qa_q2_sizebuf *m, const qa_q2_wire_player *from,
                               qa_q2_wire_player *to, int32_t flags) {
  to->pmove.pm_type = from->pmove.pm_type;
  memcpy(to->pmove.origin, from->pmove.origin, sizeof to->pmove.origin);
  memcpy(to->pmove.velocity, from->pmove.velocity, sizeof to->pmove.velocity);
  memcpy(to->pmove.origin_f, from->pmove.origin_f, sizeof to->pmove.origin_f);
  memcpy(to->pmove.velocity_f, from->pmove.velocity_f, sizeof to->pmove.velocity_f);
  to->pmove.pm_flags = from->pmove.pm_flags;
  to->pmove.pm_time = from->pmove.pm_time;
  to->pmove.gravity = from->pmove.gravity;
  memcpy(to->pmove.delta_angles, from->pmove.delta_angles, sizeof to->pmove.delta_angles);
  memcpy(to->pmove.delta_angles_f, from->pmove.delta_angles_f, sizeof to->pmove.delta_angles_f);
  to->pmove.delta_angle_float = from->pmove.delta_angle_float;
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
  to->team_id = from->team_id;
  if (flags & QA_Q2_PS_M_TYPE) to->pmove.pm_type = (double)qa_q2_msg_read_byte(m);
  if (flags & QA_Q2_PS_M_ORIGIN) {
    double x = qa_q2_msg_read_float(m);
    double y = qa_q2_msg_read_float(m);
    double z = qa_q2_msg_read_float(m);
    to->pmove.origin_f[0] = (float)x;
    to->pmove.origin_f[1] = (float)y;
    to->pmove.origin_f[2] = (float)z;
    to->pmove.origin[0] = qa_q2repro_pm_float_to_short(x);
    to->pmove.origin[1] = qa_q2repro_pm_float_to_short(y);
    to->pmove.origin[2] = qa_q2repro_pm_float_to_short(z);
  }
  if (flags & QA_Q2_PS_M_VELOCITY) {
    double x = qa_q2_msg_read_float(m);
    double y = qa_q2_msg_read_float(m);
    double z = qa_q2_msg_read_float(m);
    to->pmove.velocity_f[0] = (float)x;
    to->pmove.velocity_f[1] = (float)y;
    to->pmove.velocity_f[2] = (float)z;
    to->pmove.velocity[0] = qa_q2repro_pm_float_to_short(x);
    to->pmove.velocity[1] = qa_q2repro_pm_float_to_short(y);
    to->pmove.velocity[2] = qa_q2repro_pm_float_to_short(z);
  }
  if (flags & QA_Q2_PS_M_TIME) to->pmove.pm_time = (double)qa_q2_msg_read_word(m);
  if (flags & QA_Q2_PS_M_FLAGS) to->pmove.pm_flags = (double)qa_q2_msg_read_word(m);
  if (flags & QA_Q2_PS_M_GRAVITY) to->pmove.gravity = (double)qa_q2_msg_read_short(m);
  if (flags & QA_Q2_PS_M_DELTA_ANGLES) {
    to->pmove.delta_angle_float = true;
    for (int i = 0; i < 3; i++) {
      to->pmove.delta_angles_f[i] = (float)qa_q2_msg_read_float(m);
      to->pmove.delta_angles[i] = (int16_t)qa_q2_angle2short((double)to->pmove.delta_angles_f[i]);
    }
  }
  if (flags & QA_Q2_PS_VIEWOFFSET) {
    to->viewoffset[0] = (float)((double)qa_q2_msg_read_short(m) / QA_Q2REPRO_VIEWOFFSET_SCALE);
    to->viewoffset[1] = (float)((double)qa_q2_msg_read_short(m) / QA_Q2REPRO_VIEWOFFSET_SCALE);
    to->viewoffset[2] = (float)((double)qa_q2_msg_read_short(m) / QA_Q2REPRO_VIEWOFFSET_SCALE);
    to->pmove.viewheight = (double)qa_q2_msg_read_char(m);
  }
  if (flags & QA_Q2_PS_VIEWANGLES) {
    to->viewangles[0] = (float)qa_q2_msg_read_float(m);
    to->viewangles[1] = (float)qa_q2_msg_read_float(m);
    to->viewangles[2] = (float)qa_q2_msg_read_float(m);
  }
  if (flags & QA_Q2_PS_KICKANGLES) {
    to->kick_angles[0] = (float)((double)qa_q2_msg_read_short(m) / QA_Q2REPRO_KICK_ANGLE_SCALE);
    to->kick_angles[1] = (float)((double)qa_q2_msg_read_short(m) / QA_Q2REPRO_KICK_ANGLE_SCALE);
    to->kick_angles[2] = (float)((double)qa_q2_msg_read_short(m) / QA_Q2REPRO_KICK_ANGLE_SCALE);
  }
  if (flags & QA_Q2_PS_WEAPONINDEX) {
    int32_t gun = qa_q2_msg_read_word(m);
    to->gunindex = (double)(gun & QA_Q2REPRO_GUNINDEX_MASK);
    to->gunskin = (double)((uint32_t)gun >> QA_Q2REPRO_GUNINDEX_BITS);
  }
  if (flags & QA_Q2_PS_WEAPONFRAME) {
    int32_t gunbits = qa_q2_msg_read_word(m);
    to->gunframe = (double)(gunbits & 0x1ff);
    gunbits = (int32_t)((uint32_t)gunbits >> 9);
    if (gunbits & 1) to->gunoffset[0] = (float)qa_q2_msg_read_float(m);
    if (gunbits & 2) to->gunoffset[1] = (float)qa_q2_msg_read_float(m);
    if (gunbits & 4) to->gunoffset[2] = (float)qa_q2_msg_read_float(m);
    if (gunbits & 8) to->gunangles[0] = (float)qa_q2_msg_read_float(m);
    if (gunbits & 16) to->gunangles[1] = (float)qa_q2_msg_read_float(m);
    if (gunbits & 32) to->gunangles[2] = (float)qa_q2_msg_read_float(m);
    if (gunbits & QA_Q2_KEX_GUNBIT_GUNRATE) to->gunrate = (double)qa_q2_msg_read_byte(m);
  }
  if (flags & QA_Q2_PS_BLEND) {
    for (int i = 0; i < 4; i++) to->blend[i] = (float)((double)qa_q2_msg_read_byte(m) / 255.0);
  }
  if (flags & QA_Q2_PS_FOV) to->fov = (double)qa_q2_msg_read_byte(m);
  if (flags & QA_Q2_PS_RDFLAGS) to->rdflags = (double)qa_q2_msg_read_byte(m);
  uint32_t sb1 = (uint32_t)qa_q2_msg_read_long(m);
  for (int i = 0; i < 32; i++) {
    if ((sb1 & (1u << i)) != 0) {
      int32_t value = qa_q2_msg_read_short(m);
      if (i < QA_Q2_MAX_STATS_STORAGE) to->stats[i] = (int16_t)value;
    }
  }
  uint32_t sb2 = (uint32_t)qa_q2_msg_read_long(m);
  for (int i = 0; i < 32; i++) {
    if ((sb2 & (1u << i)) != 0) {
      int32_t value = qa_q2_msg_read_short(m);
      int idx = 32 + i;
      if (idx < QA_Q2_MAX_STATS_STORAGE) to->stats[idx] = (int16_t)value;
    }
  }
  if (flags & PS_DAMAGE_BLEND_KD) {
    for (int i = 0; i < 4; i++) to->damage_blend[i] = (float)((double)qa_q2_msg_read_byte(m) / 255.0);
  }
  if (flags & PS_TEAM_KD) to->team_id = (double)qa_q2_msg_read_byte(m);
}

static void read_player_state_delta(qa_q2_protocol_codec *self, qa_q2_sizebuf *msg,
                                    const qa_q2_wire_player *from,
                                    qa_q2_wire_player *to) {
  (void)msg; /* TS reads from the bound net_message. */
  int32_t flags = read_kex_flags(self->net_message);
  read_player_fields(self->net_message, from, to, flags);
}

static void read_frame_header(qa_q2_protocol_codec *self, uint8_t *areabits,
                              size_t areabits_length, bool read_suppress_byte,
                              qa_q2_frame_header *out) {
  (void)read_suppress_byte;
  qa_q2_sizebuf *m = self->net_message;
  out->serverframe = (double)qa_q2_msg_read_long(m);
  out->deltaframe = (double)qa_q2_msg_read_long(m);
  out->surpress_count = (double)qa_q2_msg_read_byte(m);
  int32_t len = qa_q2_msg_read_byte(m);
  qa_q2_msg_read_data(m, areabits, areabits_length, len);
  out->areabytes = (double)len;
}

static void read_frame_playerstate(qa_q2_protocol_codec *self,
                                   const qa_q2_wire_player *from,
                                   qa_q2_wire_player *to) {
  qa_q2_sizebuf *m = self->net_message;
  int32_t cmd = qa_q2_msg_read_byte(m);
  if (cmd != QA_Q2_SVC_PLAYERINFO) {
    qa_q2_com_error_throw(QA_Q2_ERR_DROP, "kexdemo: CL_ParseFrame: not playerinfo (got %d)", (int)cmd);
  }
  int32_t flags = read_kex_flags(m);
  read_player_fields(m, from, to, flags);
}

static void read_packet_entities_begin(qa_q2_protocol_codec *self) {
  int32_t cmd = qa_q2_msg_read_byte(self->net_message);
  if (cmd != QA_Q2_SVC_PACKETENTITIES) {
    qa_q2_com_error_throw(QA_Q2_ERR_DROP, "kexdemo: CL_ParseFrame: not packetentities (got %d)", (int)cmd);
  }
}

static void w_usercmd(qa_q2_protocol_codec *self, qa_q2_sizebuf *msg,
                      const qa_q2_wire_usercmd *from, const qa_q2_wire_usercmd *cmd) {
  (void)self;
  qa_q2_write_kex_usercmd(msg, from, cmd);
}
static void r_usercmd(qa_q2_protocol_codec *self, qa_q2_sizebuf *msg,
                      const qa_q2_wire_usercmd *from, qa_q2_wire_usercmd *move) {
  (void)self;
  qa_q2_read_kex_usercmd(msg, from, move);
}

#define MAX_DAMAGE_INDICATORS 4

bool qa_q2_kex_read_damage(qa_q2_kex_context *ctx,
                           qa_q2_kex_damage_indicator **out, size_t *out_count) {
  qa_q2_sizebuf *m = ctx->net_message;
  int32_t count = qa_q2_msg_read_byte(m);
  size_t kept = count > MAX_DAMAGE_INDICATORS ? MAX_DAMAGE_INDICATORS
                : count > 0                   ? (size_t)count
                                              : 0;
  qa_q2_kex_damage_indicator *result =
      (qa_q2_kex_damage_indicator *)calloc(kept > 0 ? kept : 1, sizeof *result);
  if (result == NULL) return false;
  for (int32_t i = 0; i < count; i++) {
    int32_t encoded = qa_q2_msg_read_byte(m);
    float direction[3];
    qa_q2_msg_read_dir(m, direction);
    if (i >= MAX_DAMAGE_INDICATORS) continue;
    result[i].damage = (double)(encoded & 0x1f);
    result[i].health = (encoded & 0x20) != 0;
    result[i].armor = (encoded & 0x40) != 0;
    result[i].shield = (encoded & 0x80) != 0;
    memcpy(result[i].direction, direction, sizeof direction);
  }
  *out = result;
  *out_count = kept;
  return true;
}

void qa_q2_kex_read_poi(qa_q2_kex_context *ctx, qa_q2_kex_poi *out) {
  qa_q2_sizebuf *m = ctx->net_message;
  out->key = (double)qa_q2_msg_read_word(m);
  out->time = (double)qa_q2_msg_read_word(m);
  out->pos[0] = (float)qa_q2_msg_read_float(m);
  out->pos[1] = (float)qa_q2_msg_read_float(m);
  out->pos[2] = (float)qa_q2_msg_read_float(m);
  out->image = (double)qa_q2_msg_read_word(m);
  out->color = (double)qa_q2_msg_read_byte(m);
  out->flags = (double)qa_q2_msg_read_byte(m);
}

void qa_q2_kex_read_help_path(qa_q2_kex_context *ctx, qa_q2_kex_help_path *out) {
  qa_q2_sizebuf *m = ctx->net_message;
  out->start = qa_q2_msg_read_byte(m) != 0;
  out->pos[0] = (float)qa_q2_msg_read_float(m);
  out->pos[1] = (float)qa_q2_msg_read_float(m);
  out->pos[2] = (float)qa_q2_msg_read_float(m);
  qa_q2_msg_read_dir(m, out->dir);
}

void qa_q2_kex_read_muzzleflash3(qa_q2_kex_context *ctx,
                                 qa_q2_kex_muzzleflash3 *out) {
  out->entity = (double)qa_q2_msg_read_short(ctx->net_message);
  out->weapon = (double)qa_q2_msg_read_word(ctx->net_message);
}

size_t qa_q2_kex_read_achievement(qa_q2_kex_context *ctx, char *out,
                                  size_t out_capacity) {
  return qa_q2_msg_read_string(ctx->net_message, out, out_capacity);
}

void qa_q2_kex_read_locprint(qa_q2_kex_context *ctx, qa_q2_kex_locprint *out) {
  qa_q2_sizebuf *m = ctx->net_message;
  memset(out, 0, sizeof *out);
  out->flags = (double)qa_q2_msg_read_byte(m);
  qa_q2_msg_read_string(m, out->base, sizeof out->base);
  int32_t num_args = qa_q2_msg_read_byte(m);
  if (num_args > QA_Q2_KEX_LOCPRINT_ARGS_MAX) {
    qa_q2_com_error_throw(
        QA_Q2_ERR_DROP,
        "kexdemo: svc_rr_locprint num_args %d exceeds MAX_LOCALIZATION_ARGS (%d)",
        (int)num_args, QA_Q2_KEX_LOCPRINT_ARGS_MAX);
  }
  out->arg_count = num_args > 0 ? (size_t)num_args : 0;
  for (size_t i = 0; i < out->arg_count; i++) {
    qa_q2_msg_read_string(m, out->args[i], sizeof out->args[i]);
  }
}

double qa_q2_kex_read_splitclient(qa_q2_kex_context *ctx) {
  return (double)qa_q2_msg_read_byte(ctx->net_message);
}

void qa_q2_kex_read_sound(qa_q2_kex_context *ctx, qa_q2_kex_sound *out) {
  qa_q2_sizebuf *m = ctx->net_message;
  memset(out, 0, sizeof *out);
  int32_t flags = qa_q2_msg_read_byte(m);
  out->flags = (double)flags;
  out->index = (double)qa_q2_msg_read_word(m);
  out->volume = (flags & 1) != 0 ? (double)qa_q2_msg_read_byte(m) / 255.0 : 1.0;
  out->attenuation = (flags & 2) != 0 ? (double)qa_q2_msg_read_byte(m) / 64.0 : 1.0;
  out->timeofs = (flags & 16) != 0 ? (double)qa_q2_msg_read_byte(m) / 1000.0 : 0;
  if ((flags & 8) != 0) {
    uint32_t entchan = (flags & 64) != 0 ? (uint32_t)qa_q2_msg_read_long(m)
                                         : (uint32_t)qa_q2_msg_read_word(m);
    out->entity = (double)(entchan >> 3);
    out->channel = (double)(entchan & 7u);
  }
  if ((flags & 4) != 0) {
    out->has_pos = true;
    if (!qa_q2_kex_is_demo_protocol(ctx)) {
      out->pos[0] = (float)qa_q2_msg_read_float(m);
      out->pos[1] = (float)qa_q2_msg_read_float(m);
      out->pos[2] = (float)qa_q2_msg_read_float(m);
      return;
    }
    out->pos[0] = (float)((double)qa_q2_msg_read_short(m) * COORD_SHORT_SCALE_KD);
    out->pos[1] = (float)((double)qa_q2_msg_read_short(m) * COORD_SHORT_SCALE_KD);
    out->pos[2] = (float)((double)qa_q2_msg_read_short(m) * COORD_SHORT_SCALE_KD);
  }
}

/* TS inflateInternal: zlib-wrapped inflate; ComError on failure. */
static uint8_t *inflate_internal(qa_q2_sizebuf *m, int32_t compressed_len,
                                 size_t *out_length) {
  uint8_t *compressed =
      (uint8_t *)malloc(compressed_len > 0 ? (size_t)compressed_len : 1);
  if (compressed == NULL) return NULL;
  qa_q2_msg_read_data(m, compressed, compressed_len > 0 ? (size_t)compressed_len : 0,
                      compressed_len);
  size_t capacity = 4096;
  size_t length = 0;
  uint8_t *out = (uint8_t *)malloc(capacity);
  if (out == NULL) {
    free(compressed);
    return NULL;
  }
  z_stream stream;
  memset(&stream, 0, sizeof stream);
  if (inflateInit(&stream) != Z_OK) {
    const char *z = stream.msg != NULL ? stream.msg : "zlib error";
    free(compressed);
    free(out);
    qa_q2_com_error_throw(QA_Q2_ERR_DROP, "kexdemo: zlib inflate failed: %s", z);
  }
  stream.next_in = compressed;
  stream.avail_in = compressed_len > 0 ? (uInt)compressed_len : 0;
  int status = Z_OK;
  while (status == Z_OK) {
    if (length == capacity) {
      capacity *= 2;
      uint8_t *grown = (uint8_t *)realloc(out, capacity);
      if (grown == NULL) {
        inflateEnd(&stream);
        free(compressed);
        free(out);
        return NULL;
      }
      out = grown;
    }
    stream.next_out = out + length;
    stream.avail_out = (uInt)(capacity - length);
    status = inflate(&stream, Z_NO_FLUSH);
    length = stream.total_out;
  }
  const char *zmsg = stream.msg;
  inflateEnd(&stream);
  free(compressed);
  if (status != Z_STREAM_END) {
    const char *z = zmsg != NULL ? zmsg : "zlib error";
    free(out);
    qa_q2_com_error_throw(QA_Q2_ERR_DROP, "kexdemo: zlib inflate failed: %s", z);
  }
  *out_length = length;
  return out;
}

bool qa_q2_kex_read_configblast(qa_q2_kex_context *ctx,
                                qa_q2_kex_configstring_record **out,
                                size_t *out_count) {
  qa_q2_sizebuf *m = ctx->net_message;
  int32_t compressed_len = qa_q2_msg_read_word(m);
  (void)qa_q2_msg_read_word(m);
  size_t inflated_len = 0;
  uint8_t *inflated = inflate_internal(m, compressed_len, &inflated_len);
  if (inflated == NULL) return false;
  qa_q2_sizebuf buf;
  qa_q2_sizebuf_init_empty(&buf);
  qa_q2_sz_init(&buf, inflated, inflated_len, (int64_t)inflated_len);
  buf.cursize = (int64_t)inflated_len;
  qa_q2_msg_begin_reading(&buf);
  size_t capacity = 16;
  size_t count = 0;
  qa_q2_kex_configstring_record *records =
      (qa_q2_kex_configstring_record *)calloc(capacity, sizeof *records);
  if (records == NULL) {
    free(inflated);
    return false;
  }
  while (buf.readcount < buf.cursize) {
    if (count == capacity) {
      capacity *= 2;
      qa_q2_kex_configstring_record *grown = (qa_q2_kex_configstring_record *)realloc(
          records, capacity * sizeof *records);
      if (grown == NULL) {
        qa_q2_kex_configblast_free(records, count);
        free(inflated);
        return false;
      }
      records = grown;
    }
    double index = (double)qa_q2_msg_read_word(&buf);
    char value[2048];
    qa_q2_msg_read_string(&buf, value, sizeof value);
    size_t vlen = strlen(value);
    char *owned = (char *)malloc(vlen + 1);
    if (owned == NULL) {
      qa_q2_kex_configblast_free(records, count);
      free(inflated);
      return false;
    }
    memcpy(owned, value, vlen + 1);
    records[count].index = index;
    records[count].value = owned;
    count++;
  }
  free(inflated);
  *out = records;
  *out_count = count;
  return true;
}

void qa_q2_kex_configblast_free(qa_q2_kex_configstring_record *records,
                                size_t count) {
  if (records != NULL) {
    for (size_t i = 0; i < count; i++) free(records[i].value);
    free(records);
  }
}

bool qa_q2_kex_read_spawnbaselineblast(qa_q2_kex_context *ctx,
                                       qa_q2_kex_baseline_entry **out,
                                       size_t *out_count) {
  qa_q2_sizebuf *m = ctx->net_message;
  int32_t compressed_len = qa_q2_msg_read_word(m);
  (void)qa_q2_msg_read_word(m);
  size_t inflated_len = 0;
  uint8_t *inflated = inflate_internal(m, compressed_len, &inflated_len);
  if (inflated == NULL) return false;
  /* Swap the bound message over the inflated stream like TS. */
  uint8_t *saved_data = m->data;
  size_t saved_len = m->data_length;
  int64_t saved_cursize = m->cursize;
  int64_t saved_readcount = m->readcount;
  int64_t saved_maxsize = m->maxsize;
  bool saved_owns = m->owns_data;
  m->data = inflated;
  m->data_length = inflated_len;
  m->cursize = (int64_t)inflated_len;
  m->readcount = 0;
  m->maxsize = (int64_t)inflated_len;
  m->owns_data = false;
  ctx->rerelease.net_message = m;
  size_t capacity = 16;
  size_t count = 0;
  qa_q2_kex_baseline_entry *results =
      (qa_q2_kex_baseline_entry *)calloc(capacity, sizeof *results);
  bool ok = results != NULL;
  while (ok && m->readcount < m->cursize) {
    if (count == capacity) {
      capacity *= 2;
      qa_q2_kex_baseline_entry *grown =
          (qa_q2_kex_baseline_entry *)realloc(results, capacity * sizeof *results);
      if (grown == NULL) {
        ok = false;
        break;
      }
      results = grown;
    }
    qa_q2_entity_bits eb;
    qa_q2repro_read_entity_bits_wide(&ctx->rerelease, &eb);
    qa_q2_wire_entity empty;
    qa_q2_wire_entity_init(&empty);
    qa_q2_wire_entity state;
    qa_q2_wire_entity_init(&state);
    read_delta_entity(&ctx->codec, &empty, &state, eb.number, eb.bits_u64);
    state.number = eb.number;
    results[count].entnum = eb.number;
    results[count].state = state;
    count++;
  }
  m->data = saved_data;
  m->data_length = saved_len;
  m->cursize = saved_cursize;
  m->readcount = saved_readcount;
  m->maxsize = saved_maxsize;
  m->owns_data = saved_owns;
  free(inflated);
  if (!ok) {
    free(results);
    return false;
  }
  *out = results;
  *out_count = count;
  return true;
}

void qa_q2_kex_context_init(qa_q2_kex_context *ctx, qa_q2_sizebuf *net_message) {
  memset(ctx, 0, sizeof *ctx);
  ctx->net_message = net_message;
  ctx->protocol = QA_Q2_KEX_PROTOCOL;
  ctx->split_players = 1;
  ctx->writer_binding.protocol_value = &ctx->protocol;
  qa_q2repro_context_init(&ctx->rerelease, net_message);
  memset(&ctx->codec, 0, sizeof ctx->codec);
  ctx->codec.name = "kexdemo";
  ctx->codec.net_message = net_message;
  qa_q2_kex_fill_writers(&ctx->codec, &ctx->writer_binding);
  ctx->codec.read_delta_usercmd = r_usercmd;
  ctx->codec.write_delta_usercmd = w_usercmd;
  ctx->codec.read_server_data = read_server_data;
  ctx->codec.read_entity_bits = read_entity_bits;
  ctx->codec.read_delta_entity = read_delta_entity;
  ctx->codec.read_player_state_delta = read_player_state_delta;
  ctx->codec.read_frame_header = read_frame_header;
  ctx->codec.read_frame_playerstate = read_frame_playerstate;
  ctx->codec.read_packet_entities_begin = read_packet_entities_begin;
}
