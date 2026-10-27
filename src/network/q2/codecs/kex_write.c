/* Port of src/network/q2/codecs/kex-write.ts. GPL-2.0-or-later. */
#include "qa/network/q2/codecs/kex_write.h"

#include <math.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/network/q2/codecs/q2repro.h"
#include "qa/network/q2/constants.h"

#define U_MODEL16_KW (1u << 28)
#define U_EFFECTS64_KW (1u << 29)
#define U_ALPHA_KW (1u << 30)
#define PS_MOREBITS_KW (1 << 15)
#define PS_DAMAGE_BLEND_KW (1 << 16)
#define PS_TEAM_KW (1 << 17)

static double protocol_of(qa_q2_protocol_codec *self) {
  qa_q2_kex_writer_binding *b = (qa_q2_kex_writer_binding *)self->ctx;
  return b != NULL && b->protocol_value != NULL ? *b->protocol_value : 2023;
}

static uint32_t width_bits(double value, uint32_t byte_bit, uint32_t short_bit) {
  uint32_t u = (uint32_t)(int32_t)value;
  return (u & 0xffff8000u) != 0 ? byte_bit | short_bit : u > 255u ? short_bit : byte_bit;
}
static int32_t byte_color_of(double v) {
  double c = trunc(v * 255.0);
  if (c < 0) c = 0;
  if (c > 255) c = 255;
  return (int32_t)c;
}
static int32_t fixed_of(double v, double scale) {
  double c = trunc(v * scale);
  if (c < -32768) c = -32768;
  if (c > 32767) c = 32767;
  return (int32_t)c;
}
static void write_width(qa_q2_sizebuf *m, double value, uint32_t bits, uint32_t byte_bit,
                        uint32_t short_bit) {
  if ((bits & (byte_bit | short_bit)) == (byte_bit | short_bit)) qa_q2_msg_write_long(m, value);
  else if ((bits & short_bit) != 0) qa_q2_msg_write_short(m, value);
  else if ((bits & byte_bit) != 0) qa_q2_msg_write_byte(m, value);
}

static void write_delta_entity(qa_q2_protocol_codec *self, qa_q2_sizebuf *m,
                               const qa_q2_wire_entity *from,
                               const qa_q2_wire_entity *to, bool force, bool new_entity) {
  uint32_t bits = 0;
  uint32_t high = 0;
  if (to->origin[0] != from->origin[0]) bits |= (uint32_t)QA_Q2_U_ORIGIN1;
  if (to->origin[1] != from->origin[1]) bits |= (uint32_t)QA_Q2_U_ORIGIN2;
  if (to->origin[2] != from->origin[2]) bits |= (uint32_t)QA_Q2_U_ORIGIN3;
  if (to->angles[0] != from->angles[0]) bits |= (uint32_t)QA_Q2_U_ANGLE1;
  if (to->angles[1] != from->angles[1]) bits |= (uint32_t)QA_Q2_U_ANGLE2;
  if (to->angles[2] != from->angles[2]) bits |= (uint32_t)QA_Q2_U_ANGLE3;
  if (to->frame != from->frame) bits |= to->frame >= 256 ? (uint32_t)QA_Q2_U_FRAME16 : (uint32_t)QA_Q2_U_FRAME8;
  if (to->skinnum != from->skinnum) bits |= width_bits(to->skinnum, (uint32_t)QA_Q2_U_SKIN8, (uint32_t)QA_Q2_U_SKIN16);
  if (to->effects != from->effects || to->morefx != from->morefx) {
    if (to->morefx != 0) bits |= U_EFFECTS64_KW | width_bits(to->morefx, (uint32_t)QA_Q2_U_EFFECTS8, (uint32_t)QA_Q2_U_EFFECTS16);
    else bits |= width_bits(to->effects, (uint32_t)QA_Q2_U_EFFECTS8, (uint32_t)QA_Q2_U_EFFECTS16);
  }
  if (to->renderfx != from->renderfx) bits |= width_bits(to->renderfx, (uint32_t)QA_Q2_U_RENDERFX8, (uint32_t)QA_Q2_U_RENDERFX16);
  if (to->solid != from->solid) bits |= (uint32_t)QA_Q2_U_SOLID;
  if (to->event != 0) bits |= (uint32_t)QA_Q2_U_EVENT;
  double to_models[4] = {to->modelindex, to->modelindex2, to->modelindex3, to->modelindex4};
  double from_models[4] = {from->modelindex, from->modelindex2, from->modelindex3, from->modelindex4};
  uint32_t mflags[4] = {(uint32_t)QA_Q2_U_MODEL, (uint32_t)QA_Q2_U_MODEL2,
                        (uint32_t)QA_Q2_U_MODEL3, (uint32_t)QA_Q2_U_MODEL4};
  for (int i = 0; i < 4; i++) {
    if (to_models[i] != from_models[i]) {
      bits |= mflags[i];
      if (to_models[i] > 255) bits |= U_MODEL16_KW;
    }
  }
  int32_t volume = qa_q2repro_encode_loop_volume(to->loop_volume);
  int32_t attenuation = qa_q2repro_encode_loop_attenuation(to->loop_attenuation);
  bool changed_vol = volume != qa_q2repro_encode_loop_volume(from->loop_volume);
  bool changed_attn = attenuation != qa_q2repro_encode_loop_attenuation(from->loop_attenuation);
  if (to->sound != from->sound || changed_vol || changed_attn) bits |= (uint32_t)QA_Q2_U_SOUND;
  if (new_entity || ((int32_t)to->renderfx & QA_Q2_RF_BEAM) != 0) bits |= (uint32_t)QA_Q2_U_OLDORIGIN;
  if (qa_q2repro_encode_alpha(to->alpha) != qa_q2repro_encode_alpha(from->alpha)) bits |= U_ALPHA_KW;
  if (qa_q2repro_encode_scale(to->scale) != qa_q2repro_encode_scale(from->scale)) high |= 1;
  if (to->instance_bits != from->instance_bits) high |= 2;
  if (to->owner != from->owner) high |= 4;
  if (to->old_frame != from->old_frame) high |= 8;
  if (bits == 0 && high == 0 && !force) return;
  if (high != 0) high = 255;
  qa_q2repro_write_entity_bits_wide(m, bits, high, to->number);
  for (int i = 0; i < 4; i++) {
    if ((bits & mflags[i]) != 0) {
      if ((bits & U_MODEL16_KW) != 0) qa_q2_msg_write_short(m, to_models[i]);
      else qa_q2_msg_write_byte(m, to_models[i]);
    }
  }
  if (bits & (uint32_t)QA_Q2_U_FRAME16) qa_q2_msg_write_short(m, to->frame);
  else if (bits & (uint32_t)QA_Q2_U_FRAME8) qa_q2_msg_write_byte(m, to->frame);
  write_width(m, to->skinnum, bits, (uint32_t)QA_Q2_U_SKIN8, (uint32_t)QA_Q2_U_SKIN16);
  if ((bits & U_EFFECTS64_KW) != 0) qa_q2_msg_write_long(m, to->effects);
  write_width(m, (bits & U_EFFECTS64_KW) != 0 ? to->morefx : to->effects, bits,
              (uint32_t)QA_Q2_U_EFFECTS8, (uint32_t)QA_Q2_U_EFFECTS16);
  write_width(m, to->renderfx, bits, (uint32_t)QA_Q2_U_RENDERFX8, (uint32_t)QA_Q2_U_RENDERFX16);
  if (bits & (uint32_t)QA_Q2_U_SOLID) qa_q2_msg_write_long(m, to->solid);
  bool use_float = protocol_of(self) != 2022 || to->solid != 0;
  for (int i = 0; i < 3; i++) {
    uint32_t obit = i == 0 ? (uint32_t)QA_Q2_U_ORIGIN1 : i == 1 ? (uint32_t)QA_Q2_U_ORIGIN2 : (uint32_t)QA_Q2_U_ORIGIN3;
    if ((bits & obit) != 0) {
      if (use_float) qa_q2_msg_write_float(m, (double)to->origin[i]);
      else qa_q2_msg_write_coord(m, (double)to->origin[i]);
    }
  }
  if (bits & (uint32_t)QA_Q2_U_OLDORIGIN) {
    for (int i = 0; i < 3; i++) {
      if (use_float) qa_q2_msg_write_float(m, (double)to->old_origin[i]);
      else qa_q2_msg_write_coord(m, (double)to->old_origin[i]);
    }
  }
  if (bits & (uint32_t)QA_Q2_U_ANGLE1) qa_q2_msg_write_float(m, (double)to->angles[0]);
  if (bits & (uint32_t)QA_Q2_U_ANGLE2) qa_q2_msg_write_float(m, (double)to->angles[1]);
  if (bits & (uint32_t)QA_Q2_U_ANGLE3) qa_q2_msg_write_float(m, (double)to->angles[2]);
  if (bits & (uint32_t)QA_Q2_U_SOUND) {
    int32_t word = (int32_t)to->sound | (changed_vol ? 1 << 14 : 0) | (changed_attn ? 1 << 15 : 0);
    qa_q2_msg_write_short(m, (double)word);
    if (changed_vol) qa_q2_msg_write_byte(m, (double)volume);
    if (changed_attn) qa_q2_msg_write_byte(m, (double)attenuation);
  }
  if (bits & (uint32_t)QA_Q2_U_EVENT) qa_q2_msg_write_byte(m, to->event);
  if (bits & U_ALPHA_KW) qa_q2_msg_write_byte(m, (double)qa_q2repro_encode_alpha(to->alpha));
  if (high & 1) qa_q2_msg_write_byte(m, (double)qa_q2repro_encode_scale(to->scale));
  if (high & 2) qa_q2_msg_write_byte(m, to->instance_bits);
  if (high & 4) qa_q2_msg_write_short(m, to->owner);
  if (high & 8) qa_q2_msg_write_short(m, to->old_frame);
}

static double delta_angle(const qa_q2_wire_player *state, int index) {
  return state->pmove.delta_angle_float
             ? (double)state->pmove.delta_angles_f[index]
             : qa_q2_short2angle((double)state->pmove.delta_angles[index]);
}

static bool vec_changed(const float a[3], const float b[3]) {
  return a[0] != b[0] || a[1] != b[1] || a[2] != b[2];
}
static bool vec_changed_fixed(const float a[3], const float b[3], double scale) {
  for (int i = 0; i < 3; i++) {
    if (fixed_of((double)a[i], scale) != fixed_of((double)b[i], scale)) return true;
  }
  return false;
}
static bool color_changed(const float a[4], const float b[4]) {
  for (int i = 0; i < 4; i++) {
    if (byte_color_of((double)a[i]) != byte_color_of((double)b[i])) return true;
  }
  return false;
}

static void write_player_state_delta(qa_q2_protocol_codec *self, qa_q2_sizebuf *m,
                                     const qa_q2_wire_player *from,
                                     const qa_q2_wire_player *to) {
  (void)self;
  int32_t flags = 0;
  int32_t gunbits = 0;
  if (to->pmove.pm_type != from->pmove.pm_type) flags |= QA_Q2_PS_M_TYPE;
  if (vec_changed(to->pmove.origin_f, from->pmove.origin_f)) flags |= QA_Q2_PS_M_ORIGIN;
  if (vec_changed(to->pmove.velocity_f, from->pmove.velocity_f)) flags |= QA_Q2_PS_M_VELOCITY;
  if (to->pmove.pm_time != from->pmove.pm_time) flags |= QA_Q2_PS_M_TIME;
  if (to->pmove.pm_flags != from->pmove.pm_flags) flags |= QA_Q2_PS_M_FLAGS;
  if (to->pmove.gravity != from->pmove.gravity) flags |= QA_Q2_PS_M_GRAVITY;
  for (int i = 0; i < 3; i++) {
    if (delta_angle(to, i) != delta_angle(from, i)) flags |= QA_Q2_PS_M_DELTA_ANGLES;
  }
  if (vec_changed_fixed(to->viewoffset, from->viewoffset, 16) ||
      to->pmove.viewheight != from->pmove.viewheight) flags |= QA_Q2_PS_VIEWOFFSET;
  if (vec_changed(to->viewangles, from->viewangles)) flags |= QA_Q2_PS_VIEWANGLES;
  if (vec_changed_fixed(to->kick_angles, from->kick_angles, 1024)) flags |= QA_Q2_PS_KICKANGLES;
  if (color_changed(to->blend, from->blend)) flags |= QA_Q2_PS_BLEND;
  if (color_changed(to->damage_blend, from->damage_blend)) flags |= PS_DAMAGE_BLEND_KW;
  if (to->team_id != from->team_id) flags |= PS_TEAM_KW;
  if (to->fov != from->fov) flags |= QA_Q2_PS_FOV;
  if (to->rdflags != from->rdflags) flags |= QA_Q2_PS_RDFLAGS;
  if (to->gunindex != from->gunindex || to->gunskin != from->gunskin) flags |= QA_Q2_PS_WEAPONINDEX;
  for (int i = 0; i < 3; i++) {
    if (to->gunoffset[i] != from->gunoffset[i]) gunbits |= 1 << i;
    if (to->gunangles[i] != from->gunangles[i]) gunbits |= 1 << (i + 3);
  }
  if (to->gunrate != from->gunrate) gunbits |= 64;
  if (to->gunframe != from->gunframe || gunbits != 0) flags |= QA_Q2_PS_WEAPONFRAME;
  if (flags > 65535) flags |= PS_MOREBITS_KW;
  qa_q2_msg_write_short(m, (double)flags);
  if (flags & PS_MOREBITS_KW) qa_q2_msg_write_short(m, (double)((uint32_t)flags >> 16));
  if (flags & QA_Q2_PS_M_TYPE) qa_q2_msg_write_byte(m, to->pmove.pm_type);
  if (flags & QA_Q2_PS_M_ORIGIN) {
    for (int i = 0; i < 3; i++) qa_q2_msg_write_float(m, (double)to->pmove.origin_f[i]);
  }
  if (flags & QA_Q2_PS_M_VELOCITY) {
    for (int i = 0; i < 3; i++) qa_q2_msg_write_float(m, (double)to->pmove.velocity_f[i]);
  }
  if (flags & QA_Q2_PS_M_TIME) qa_q2_msg_write_short(m, to->pmove.pm_time);
  if (flags & QA_Q2_PS_M_FLAGS) qa_q2_msg_write_short(m, to->pmove.pm_flags);
  if (flags & QA_Q2_PS_M_GRAVITY) qa_q2_msg_write_short(m, to->pmove.gravity);
  if (flags & QA_Q2_PS_M_DELTA_ANGLES) {
    for (int i = 0; i < 3; i++) qa_q2_msg_write_float(m, delta_angle(to, i));
  }
  if (flags & QA_Q2_PS_VIEWOFFSET) {
    for (int i = 0; i < 3; i++) qa_q2_msg_write_short(m, (double)fixed_of((double)to->viewoffset[i], 16));
    qa_q2_msg_write_char(m, to->pmove.viewheight);
  }
  if (flags & QA_Q2_PS_VIEWANGLES) {
    for (int i = 0; i < 3; i++) qa_q2_msg_write_float(m, (double)to->viewangles[i]);
  }
  if (flags & QA_Q2_PS_KICKANGLES) {
    for (int i = 0; i < 3; i++) qa_q2_msg_write_short(m, (double)fixed_of((double)to->kick_angles[i], 1024));
  }
  if (flags & QA_Q2_PS_WEAPONINDEX) {
    int32_t gun = (int32_t)to->gunindex | ((int32_t)to->gunskin << 13);
    qa_q2_msg_write_short(m, (double)gun);
  }
  if (flags & QA_Q2_PS_WEAPONFRAME) {
    if (to->gunframe > 511 || to->gunframe < 0) {
      qa_range_error("KEX gun frame exceeds nine bits");
    }
    int32_t packed = (int32_t)to->gunframe | (gunbits << 9);
    qa_q2_msg_write_short(m, (double)packed);
    for (int i = 0; i < 3; i++) {
      if (gunbits & (1 << i)) qa_q2_msg_write_float(m, (double)to->gunoffset[i]);
    }
    for (int i = 0; i < 3; i++) {
      if (gunbits & (1 << (i + 3))) qa_q2_msg_write_float(m, (double)to->gunangles[i]);
    }
    if (gunbits & 64) qa_q2_msg_write_byte(m, to->gunrate);
  }
  if (flags & QA_Q2_PS_BLEND) {
    for (int i = 0; i < 4; i++) qa_q2_msg_write_byte(m, (double)byte_color_of((double)to->blend[i]));
  }
  if (flags & QA_Q2_PS_FOV) qa_q2_msg_write_byte(m, to->fov);
  if (flags & QA_Q2_PS_RDFLAGS) qa_q2_msg_write_byte(m, to->rdflags);
  for (int half = 0; half < 2; half++) {
    int32_t mask = 0;
    for (int i = 0; i < 32; i++) {
      if (to->stats[i + half * 32] != from->stats[i + half * 32]) mask |= 1 << i;
    }
    qa_q2_msg_write_long(m, (double)mask);
    for (int i = 0; i < 32; i++) {
      if (mask & (1 << i)) qa_q2_msg_write_short(m, (double)to->stats[i + half * 32]);
    }
  }
  if (flags & PS_DAMAGE_BLEND_KW) {
    for (int i = 0; i < 4; i++) qa_q2_msg_write_byte(m, (double)byte_color_of((double)to->damage_blend[i]));
  }
  if (flags & PS_TEAM_KW) qa_q2_msg_write_byte(m, to->team_id);
}

static void w_server_data(qa_q2_protocol_codec *self, qa_q2_sizebuf *m,
                          const qa_q2_server_data_params *params) {
  qa_q2_msg_write_byte(m, 12);
  qa_q2_msg_write_long(m, protocol_of(self));
  qa_q2_msg_write_long(m, params->servercount);
  qa_q2_msg_write_byte(m, params->attractloop ? 1 : 0);
  qa_q2_msg_write_byte(m, params->has_server_fps ? params->server_fps : 40);
  qa_q2_msg_write_string(m, params->gamedir);
  if (params->has_clientnums && params->clientnum_count > 1) {
    if (params->clientnum_count > 8) qa_q2_throw_error("Invalid KEX split player count");
    qa_q2_msg_write_short(m, -2);
    qa_q2_msg_write_short(m, (double)params->clientnum_count);
    for (size_t i = 0; i < params->clientnum_count; i++) qa_q2_msg_write_short(m, params->clientnums[i]);
  } else {
    qa_q2_msg_write_short(m, params->clientnum);
  }
  qa_q2_msg_write_string(m, params->levelname);
}
static void w_remove(qa_q2_protocol_codec *self, qa_q2_sizebuf *m, double number) {
  (void)self;
  qa_q2repro_write_entity_bits_wide(m, 1u << 6, 0, number);
}
static void w_end(qa_q2_protocol_codec *self, qa_q2_sizebuf *m) {
  (void)self;
  qa_q2_msg_write_short(m, 0);
}
static void w_baseline(qa_q2_protocol_codec *self, qa_q2_sizebuf *m,
                       const qa_q2_wire_entity *entity) {
  qa_q2_wire_entity empty;
  qa_q2_wire_entity_init(&empty);
  qa_q2_msg_write_byte(m, 14);
  write_delta_entity(self, m, &empty, entity, true, true);
}
static void w_begin(qa_q2_protocol_codec *self, qa_q2_sizebuf *m) {
  (void)self;
  qa_q2_msg_write_byte(m, 18);
}
static void w_frame(qa_q2_protocol_codec *self, qa_q2_sizebuf *m,
                    const qa_q2_frame_write_params *params,
                    qa_q2_write_entities_fn entities, void *ctx) {
  qa_q2_msg_write_byte(m, 20);
  qa_q2_msg_write_long(m, params->framenum);
  qa_q2_msg_write_long(m, params->lastframe);
  qa_q2_msg_write_byte(m, params->surpress_count);
  qa_q2_msg_write_byte(m, params->areabytes);
  qa_q2_sz_write(m, params->areabits, params->areabits_length, (int64_t)params->areabytes);
  qa_q2_msg_write_byte(m, 17);
  qa_q2_wire_player empty;
  qa_q2_wire_player_init(&empty);
  write_player_state_delta(self, m, params->ps_from != NULL ? params->ps_from : &empty,
                           params->ps_to);
  entities(ctx, m);
}

void qa_q2_kex_fill_writers(qa_q2_protocol_codec *codec,
                            qa_q2_kex_writer_binding *binding) {
  codec->ctx = binding;
  codec->write_server_data = w_server_data;
  codec->write_delta_entity = write_delta_entity;
  codec->write_entity_remove = w_remove;
  codec->write_packet_entities_end = w_end;
  codec->write_spawn_baseline = w_baseline;
  codec->write_player_state_delta = write_player_state_delta;
  codec->write_packet_entities_begin = w_begin;
  codec->write_frame = w_frame;
}
