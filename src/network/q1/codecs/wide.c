/* Port of src/network/q1/codecs/wide.ts. GPL-2.0-or-later. */
#include "qa/network/q1/codecs/wide.h"
#include <math.h>
#include <string.h>
#include "qa/foundation/js_truthy.h"
#include "qa/foundation/js.h"
static double vg(const qa_wire_vector *v, int i) { return qa_wire_vector_get(v, (size_t)i); }
static void vs(qa_wire_vector *v, int i, double x) { qa_wire_vector_set(v, (size_t)i, x); }
/* ToInt32: NaN and infinities are 0 (the old fmod form cast NaN to uint32_t). */
static int32_t ji(double n) { return qa_js_to_int32(n); }
static uint32_t ju(double n) { return (uint32_t)ji(n); }
static void wc_coord(qa_protocol_codec *c, qa_size_buf *sb, double f, double fl) { (void)c; qa_msg_write_coord_flags(sb, f, qa_js_to_int32(fl)); }
static void wc_angle(qa_protocol_codec *c, qa_size_buf *sb, double f, double fl) { (void)c; qa_msg_write_angle_flags(sb, f, qa_js_to_int32(fl)); }
static double rc_coord(qa_protocol_codec *c, double fl) { return qa_message_reader_coord_flags(c->reader, qa_js_to_int32(fl)); }
static double rc_angle(qa_protocol_codec *c, double fl) { return qa_message_reader_angle_flags(c->reader, qa_js_to_int32(fl)); }
static void wc_protocol(qa_protocol_codec *c, qa_size_buf *sb, double fl) {
  qa_msg_write_long(sb, c->protocol);
  if (c->is_rmq) qa_msg_write_long(sb, fl);
}
static void wc_eu(qa_protocol_codec *c, qa_size_buf *sb, double e, const qa_q1_entity_update *u, double fl) {
  (void)c;
  int32_t bits = 0;
  for (int i = 0; i < 3; i++) { double miss = vg(&u->origin,i) - vg(&u->baseline.origin,i); if (miss < -0.1 || miss > 0.1) bits |= QA_Q1_U_ORIGIN1 << i; }
  if (vg(&u->angles,0) != vg(&u->baseline.angles,0)) bits |= QA_Q1_U_ANGLE1;
  if (vg(&u->angles,1) != vg(&u->baseline.angles,1)) bits |= QA_Q1_U_ANGLE2;
  if (vg(&u->angles,2) != vg(&u->baseline.angles,2)) bits |= QA_Q1_U_ANGLE3;
  if (u->movetype_step) bits |= QA_Q1_U_STEP;
  if (u->baseline.colormap != u->colormap) bits |= QA_Q1_U_COLORMAP;
  if (u->baseline.skin != u->skin) bits |= QA_Q1_U_SKIN;
  if (u->baseline.frame != u->frame) bits |= QA_Q1_U_FRAME;
  if (u->baseline.effects != u->effects) bits |= QA_Q1_U_EFFECTS;
  if (u->baseline.modelindex != u->modelindex) bits |= QA_Q1_U_MODEL;
  if (u->baseline.alpha != u->alpha) bits |= QA_Q1_U_ALPHA;
  if (u->baseline.scale != u->scale) bits |= QA_Q1_U_SCALE;
  if ((bits & QA_Q1_U_FRAME) && (ji(u->frame) & 0xff00)) bits |= QA_Q1_U_FRAME2;
  if ((bits & QA_Q1_U_MODEL) && (ji(u->modelindex) & 0xff00)) bits |= QA_Q1_U_MODEL2;
  if (u->sendinterval) bits |= QA_Q1_U_LERPFINISH;
  if (bits >= 65536) bits |= QA_Q1_U_EXTEND1;
  if (bits >= 16777216) bits |= QA_Q1_U_EXTEND2;
  if (e >= 256) bits |= QA_Q1_U_LONGENTITY;
  if (bits >= 256) bits |= QA_Q1_U_MOREBITS;
  qa_msg_write_byte(sb, bits | QA_Q1_U_SIGNAL);
  if (bits & QA_Q1_U_MOREBITS) qa_msg_write_byte(sb, (uint32_t)bits >> 8);
  if (bits & QA_Q1_U_EXTEND1) qa_msg_write_byte(sb, (uint32_t)bits >> 16);
  if (bits & QA_Q1_U_EXTEND2) qa_msg_write_byte(sb, (uint32_t)bits >> 24);
  if (bits & QA_Q1_U_LONGENTITY) qa_msg_write_short(sb, e); else qa_msg_write_byte(sb, e);
  if (bits & QA_Q1_U_MODEL) qa_msg_write_byte(sb, u->modelindex);
  if (bits & QA_Q1_U_FRAME) qa_msg_write_byte(sb, u->frame);
  if (bits & QA_Q1_U_COLORMAP) qa_msg_write_byte(sb, u->colormap);
  if (bits & QA_Q1_U_SKIN) qa_msg_write_byte(sb, u->skin);
  if (bits & QA_Q1_U_EFFECTS) qa_msg_write_byte(sb, u->effects);
  if (bits & QA_Q1_U_ORIGIN1) qa_msg_write_coord_flags(sb, vg(&u->origin,0), qa_js_to_int32(fl));
  if (bits & QA_Q1_U_ANGLE1) qa_msg_write_angle_flags(sb, vg(&u->angles,0), qa_js_to_int32(fl));
  if (bits & QA_Q1_U_ORIGIN2) qa_msg_write_coord_flags(sb, vg(&u->origin,1), qa_js_to_int32(fl));
  if (bits & QA_Q1_U_ANGLE2) qa_msg_write_angle_flags(sb, vg(&u->angles,1), qa_js_to_int32(fl));
  if (bits & QA_Q1_U_ORIGIN3) qa_msg_write_coord_flags(sb, vg(&u->origin,2), qa_js_to_int32(fl));
  if (bits & QA_Q1_U_ANGLE3) qa_msg_write_angle_flags(sb, vg(&u->angles,2), qa_js_to_int32(fl));
  if (bits & QA_Q1_U_ALPHA) qa_msg_write_byte(sb, u->alpha);
  if (bits & QA_Q1_U_SCALE) qa_msg_write_byte(sb, u->scale);
  if (bits & QA_Q1_U_FRAME2) qa_msg_write_byte(sb, ji(u->frame) >> 8);
  if (bits & QA_Q1_U_MODEL2) qa_msg_write_byte(sb, ji(u->modelindex) >> 8);
  if (bits & QA_Q1_U_LERPFINISH) qa_msg_write_byte(sb, qa_q1_rint(u->lerpfinish * 255));
}
static void wc_base(qa_protocol_codec *c, qa_size_buf *sb, double entnum, qa_q1_wire_entity_state *b, double fl) {
  (void)c;
  int bits = 0;
  if (ji(b->modelindex) & 0xff00) bits |= QA_Q1_B_LARGEMODEL;
  if (ji(b->frame) & 0xff00) bits |= QA_Q1_B_LARGEFRAME;
  if (b->alpha != QA_Q1_ENTALPHA_DEFAULT) bits |= QA_Q1_B_ALPHA;
  if (b->scale != QA_Q1_ENTSCALE_DEFAULT) bits |= QA_Q1_B_SCALE;
  qa_msg_write_byte(sb, bits ? QA_Q1_svc_spawnbaseline2 : QA_Q1_SVC_SPAWNBASELINE);
  qa_msg_write_short(sb, entnum);
  if (bits) qa_msg_write_byte(sb, bits);
  if (bits & QA_Q1_B_LARGEMODEL) qa_msg_write_short(sb, b->modelindex); else qa_msg_write_byte(sb, b->modelindex);
  if (bits & QA_Q1_B_LARGEFRAME) qa_msg_write_short(sb, b->frame); else qa_msg_write_byte(sb, b->frame);
  qa_msg_write_byte(sb, b->colormap); qa_msg_write_byte(sb, b->skin);
  for (int i = 0; i < 3; i++) { qa_msg_write_coord_flags(sb, vg(&b->origin,i), qa_js_to_int32(fl)); qa_msg_write_angle_flags(sb, vg(&b->angles,i), qa_js_to_int32(fl)); }
  if (bits & QA_Q1_B_ALPHA) qa_msg_write_byte(sb, b->alpha);
  if (bits & QA_Q1_B_SCALE) qa_msg_write_byte(sb, b->scale);
}
static bool wc_static(qa_protocol_codec *c, qa_size_buf *sb, const qa_q1_wire_entity_state *state, double fl) {
  int bits = 0;
  if (ji(state->modelindex) & 0xff00) bits |= QA_Q1_B_LARGEMODEL;
  if (ji(state->frame) & 0xff00) bits |= QA_Q1_B_LARGEFRAME;
  if (state->alpha != QA_Q1_ENTALPHA_DEFAULT) bits |= QA_Q1_B_ALPHA;
  if (c->is_rmq && state->scale != QA_Q1_ENTSCALE_DEFAULT) bits |= QA_Q1_B_SCALE;
  if (bits) { qa_msg_write_byte(sb, QA_Q1_svc_spawnstatic2); qa_msg_write_byte(sb, bits); }
  else qa_msg_write_byte(sb, QA_Q1_SVC_SPAWNSTATIC);
  if (bits & QA_Q1_B_LARGEMODEL) qa_msg_write_short(sb, state->modelindex); else qa_msg_write_byte(sb, state->modelindex);
  if (bits & QA_Q1_B_LARGEFRAME) qa_msg_write_short(sb, state->frame); else qa_msg_write_byte(sb, state->frame);
  qa_msg_write_byte(sb, state->colormap); qa_msg_write_byte(sb, state->skin);
  for (int i = 0; i < 3; i++) { qa_msg_write_coord_flags(sb, vg(&state->origin,i), qa_js_to_int32(fl)); qa_msg_write_angle_flags(sb, vg(&state->angles,i), qa_js_to_int32(fl)); }
  if (bits & QA_Q1_B_ALPHA) qa_msg_write_byte(sb, state->alpha);
  if (bits & QA_Q1_B_SCALE) qa_msg_write_byte(sb, state->scale);
  return true;
}
static bool wc_ssound(qa_protocol_codec *c, qa_size_buf *sb, const qa_wire_vector *org, double snd, double vol, double atten, double fl) {
  (void)c;
  bool large = snd > 255;
  qa_msg_write_byte(sb, large ? QA_Q1_svc_spawnstaticsound2 : QA_Q1_SVC_SPAWNSTATICSOUND);
  for (int i = 0; i < 3; i++) qa_msg_write_coord_flags(sb, vg(org,i), qa_js_to_int32(fl));
  if (large) qa_msg_write_short(sb, snd); else qa_msg_write_byte(sb, snd);
  qa_msg_write_byte(sb, vol * 255); qa_msg_write_byte(sb, atten * 64);
  return true;
}
static bool wc_sound(qa_protocol_codec *c, qa_size_buf *sb, const qa_q1_sound_message *s, double fl) {
  (void)c;
  int mask = 0;
  if (s->volume != QA_Q1_DEFAULT_SOUND_PACKET_VOLUME) mask |= QA_Q1_SND_VOLUME;
  if (s->attenuation != QA_Q1_DEFAULT_SOUND_PACKET_ATTENUATION) mask |= QA_Q1_SND_ATTENUATION;
  if (s->ent >= 8192) mask |= QA_Q1_SND_LARGEENTITY;
  if (s->sound_num >= 256 || s->channel >= 8) mask |= QA_Q1_SND_LARGESOUND;
  qa_msg_write_byte(sb, QA_Q1_SVC_SOUND); qa_msg_write_byte(sb, mask);
  if (mask & QA_Q1_SND_VOLUME) qa_msg_write_byte(sb, s->volume);
  if (mask & QA_Q1_SND_ATTENUATION) qa_msg_write_byte(sb, s->attenuation * 64);
  if (mask & QA_Q1_SND_LARGEENTITY) { qa_msg_write_short(sb, s->ent); qa_msg_write_byte(sb, s->channel); }
  else qa_msg_write_short(sb, (double)((qa_q1_js_shl(ji(s->ent), 3)) | ji(s->channel)));
  if (mask & QA_Q1_SND_LARGESOUND) qa_msg_write_short(sb, s->sound_num); else qa_msg_write_byte(sb, s->sound_num);
  for (int i = 0; i < 3; i++) qa_msg_write_coord_flags(sb, vg(&s->origin,i), qa_js_to_int32(fl));
  return true;
}
static void wc_cd(qa_protocol_codec *c, qa_size_buf *sb, const qa_q1_clientdata_codec *cd, double fl) {
  (void)c;(void)fl;
  int32_t bits = 0;
  if (cd->viewheight != QA_Q1_DEFAULT_VIEWHEIGHT) bits |= QA_Q1_SU_VIEWHEIGHT;
  if (qa_js_truthy(cd->idealpitch)) bits |= QA_Q1_SU_IDEALPITCH; /* JS truthiness: NaN is falsy */
  bits |= QA_Q1_SU_ITEMS;
  if (cd->onground) bits |= QA_Q1_SU_ONGROUND;
  if (cd->inwater) bits |= QA_Q1_SU_INWATER;
  for (int i = 0; i < 3; i++) {
    if (qa_js_truthy(vg(&cd->punchangle,i))) bits |= QA_Q1_SU_PUNCH1 << i;
    if (qa_js_truthy(vg(&cd->velocity,i))) bits |= QA_Q1_SU_VELOCITY1 << i;
  }
  if (qa_js_truthy(cd->weaponframe)) bits |= QA_Q1_SU_WEAPONFRAME;
  if (qa_js_truthy(cd->armorvalue)) bits |= QA_Q1_SU_ARMOR;
  bits |= QA_Q1_SU_WEAPON;
  if ((bits & QA_Q1_SU_WEAPON) && (ji(cd->weaponmodelindex) & 0xff00)) bits |= QA_Q1_SU_WEAPON2;
  if (ji(cd->armorvalue) & 0xff00) bits |= QA_Q1_SU_ARMOR2;
  if (ji(cd->currentammo) & 0xff00) bits |= QA_Q1_SU_AMMO2;
  if (ji(cd->ammo_shells) & 0xff00) bits |= QA_Q1_SU_SHELLS2;
  if (ji(cd->ammo_nails) & 0xff00) bits |= QA_Q1_SU_NAILS2;
  if (ji(cd->ammo_rockets) & 0xff00) bits |= QA_Q1_SU_ROCKETS2;
  if (ji(cd->ammo_cells) & 0xff00) bits |= QA_Q1_SU_CELLS2;
  if ((bits & QA_Q1_SU_WEAPONFRAME) && (ji(cd->weaponframe) & 0xff00)) bits |= QA_Q1_SU_WEAPONFRAME2;
  if ((bits & QA_Q1_SU_WEAPON) && cd->alpha != QA_Q1_ENTALPHA_DEFAULT) bits |= QA_Q1_SU_WEAPONALPHA;
  if (bits >= 65536) bits |= QA_Q1_SU_EXTEND1;
  if (bits >= 16777216) bits |= QA_Q1_SU_EXTEND2;
  qa_msg_write_byte(sb, QA_Q1_SVC_CLIENTDATA); qa_msg_write_short(sb, bits);
  if (bits & QA_Q1_SU_EXTEND1) qa_msg_write_byte(sb, (uint32_t)bits >> 16);
  if (bits & QA_Q1_SU_EXTEND2) qa_msg_write_byte(sb, (uint32_t)bits >> 24);
  if (bits & QA_Q1_SU_VIEWHEIGHT) qa_msg_write_byte(sb, cd->viewheight);
  if (bits & QA_Q1_SU_IDEALPITCH) qa_msg_write_byte(sb, cd->idealpitch);
  for (int i = 0; i < 3; i++) {
    if (bits & (QA_Q1_SU_PUNCH1 << i)) qa_msg_write_byte(sb, vg(&cd->punchangle,i));
    if (bits & (QA_Q1_SU_VELOCITY1 << i)) qa_msg_write_byte(sb, vg(&cd->velocity,i) / 16);
  }
  qa_msg_write_long(sb, cd->items);
  if (bits & QA_Q1_SU_WEAPONFRAME) qa_msg_write_byte(sb, cd->weaponframe);
  if (bits & QA_Q1_SU_ARMOR) qa_msg_write_byte(sb, cd->armorvalue);
  if (bits & QA_Q1_SU_WEAPON) qa_msg_write_byte(sb, cd->weaponmodelindex);
  qa_msg_write_short(sb, cd->health);
  qa_msg_write_byte(sb, cd->currentammo); qa_msg_write_byte(sb, cd->ammo_shells);
  qa_msg_write_byte(sb, cd->ammo_nails); qa_msg_write_byte(sb, cd->ammo_rockets); qa_msg_write_byte(sb, cd->ammo_cells);
  if (cd->standard_quake) qa_msg_write_byte(sb, cd->weapon);
  else { int w = 0; for (int i = 0; i < 32; i++) if (ji(cd->weapon) & (int32_t)(1u << i)) { w = i; break; } qa_msg_write_byte(sb, w); }
  if (bits & QA_Q1_SU_WEAPON2) qa_msg_write_byte(sb, ji(cd->weaponmodelindex) >> 8);
  if (bits & QA_Q1_SU_ARMOR2) qa_msg_write_byte(sb, ji(cd->armorvalue) >> 8);
  if (bits & QA_Q1_SU_AMMO2) qa_msg_write_byte(sb, ji(cd->currentammo) >> 8);
  if (bits & QA_Q1_SU_SHELLS2) qa_msg_write_byte(sb, ji(cd->ammo_shells) >> 8);
  if (bits & QA_Q1_SU_NAILS2) qa_msg_write_byte(sb, ji(cd->ammo_nails) >> 8);
  if (bits & QA_Q1_SU_ROCKETS2) qa_msg_write_byte(sb, ji(cd->ammo_rockets) >> 8);
  if (bits & QA_Q1_SU_CELLS2) qa_msg_write_byte(sb, ji(cd->ammo_cells) >> 8);
  if (bits & QA_Q1_SU_WEAPONFRAME2) qa_msg_write_byte(sb, ji(cd->weaponframe) >> 8);
  if (bits & QA_Q1_SU_WEAPONALPHA) qa_msg_write_byte(sb, cd->alpha);
}
static double rc_pflags(qa_protocol_codec *c) { return c->is_rmq ? (double)ju(qa_message_reader_long(c->reader)) : 0; }
static double rc_ebits(qa_protocol_codec *c, double bits_in) {
  int32_t bits = ji(bits_in);
  if (bits & QA_Q1_U_EXTEND1) bits |= qa_q1_js_shl(ji(qa_message_reader_byte(c->reader)), 16);
  if (bits & QA_Q1_U_EXTEND2) bits |= qa_q1_js_shl(ji(qa_message_reader_byte(c->reader)), 24);
  return bits;
}
static void rc_etail(qa_protocol_codec *c, double bd, qa_q1_entity_update_tail *out) {
  int32_t bits = ji(bd); qa_message_reader *r = c->reader;
  qa_q1_entity_update_tail_clear(out);
  if (bits & QA_Q1_U_ALPHA) { out->has_alpha = true; out->alpha = qa_message_reader_byte(r); }
  if (bits & QA_Q1_U_SCALE) { out->has_scale = true; out->scale = qa_message_reader_byte(r); }
  if (bits & QA_Q1_U_FRAME2) { out->has_frame2 = true; out->frame_high = qa_message_reader_byte(r); }
  if (bits & QA_Q1_U_MODEL2) { out->has_model2 = true; out->model_high = qa_message_reader_byte(r); }
  if (bits & QA_Q1_U_LERPFINISH) { out->has_lerpfinish = true; out->lerpfinish = qa_message_reader_byte(r) / 255; }
}
static void rc_base(qa_protocol_codec *c, qa_q1_wire_entity_state *b, double version, double fl) {
  qa_message_reader *r = c->reader;
  int32_t bits = version == 2 ? ji(qa_message_reader_byte(r)) : 0;
  b->modelindex = (bits & QA_Q1_B_LARGEMODEL) ? (double)(ji(qa_message_reader_short(r)) & 65535) : qa_message_reader_byte(r);
  b->frame = (bits & QA_Q1_B_LARGEFRAME) ? (double)(ji(qa_message_reader_short(r)) & 65535) : qa_message_reader_byte(r);
  b->colormap = qa_message_reader_byte(r); b->skin = qa_message_reader_byte(r);
  for (int i = 0; i < 3; i++) { vs(&b->origin,i,qa_message_reader_coord_flags(r,qa_js_to_int32(fl))); vs(&b->angles,i,qa_message_reader_angle_flags(r,qa_js_to_int32(fl))); }
  b->alpha = (bits & QA_Q1_B_ALPHA) ? qa_message_reader_byte(r) : QA_Q1_ENTALPHA_DEFAULT;
  b->scale = (bits & QA_Q1_B_SCALE) ? qa_message_reader_byte(r) : QA_Q1_ENTSCALE_DEFAULT;
}
static double rc_cbits(qa_protocol_codec *c) {
  int32_t bits = ji(qa_message_reader_short(c->reader)) & 0xffff;
  if (bits & QA_Q1_SU_EXTEND1) bits |= qa_q1_js_shl(ji(qa_message_reader_byte(c->reader)), 16);
  if (bits & QA_Q1_SU_EXTEND2) bits |= qa_q1_js_shl(ji(qa_message_reader_byte(c->reader)), 24);
  return bits;
}
static void rc_ctail(qa_protocol_codec *c, double bd, qa_q1_clientdata_tail *out) {
  int32_t bits = ji(bd); qa_message_reader *r = c->reader;
  qa_q1_clientdata_tail_clear(out);
  if (bits & QA_Q1_SU_WEAPON2) out->weapon_high = qa_message_reader_byte(r);
  if (bits & QA_Q1_SU_ARMOR2) out->armor_high = qa_message_reader_byte(r);
  if (bits & QA_Q1_SU_AMMO2) out->ammo_high = qa_message_reader_byte(r);
  if (bits & QA_Q1_SU_SHELLS2) out->shells_high = qa_message_reader_byte(r);
  if (bits & QA_Q1_SU_NAILS2) out->nails_high = qa_message_reader_byte(r);
  if (bits & QA_Q1_SU_ROCKETS2) out->rockets_high = qa_message_reader_byte(r);
  if (bits & QA_Q1_SU_CELLS2) out->cells_high = qa_message_reader_byte(r);
  if (bits & QA_Q1_SU_WEAPONFRAME2) out->weaponframe_high = qa_message_reader_byte(r);
  out->weaponalpha = (bits & QA_Q1_SU_WEAPONALPHA) ? qa_message_reader_byte(r) : QA_Q1_ENTALPHA_DEFAULT;
}
static void rc_sheader(qa_protocol_codec *c, double md, qa_q1_sound_header *out) {
  int32_t mask = ji(md); qa_message_reader *r = c->reader;
  if (mask & QA_Q1_SND_LARGEENTITY) { out->ent = ji(qa_message_reader_short(r)) & 0xffff; out->channel = qa_message_reader_byte(r); }
  else { int ch = ji(qa_message_reader_short(r)) & 0xffff; out->ent = ch >> 3; out->channel = ch & 7; }
  out->sound_num = (mask & QA_Q1_SND_LARGESOUND) ? (double)(ji(qa_message_reader_short(r)) & 0xffff) : qa_message_reader_byte(r);
}
static double rc_ssidx(qa_protocol_codec *c, double version) {
  return version == 2 ? (double)(ji(qa_message_reader_short(c->reader)) & 0xffff) : qa_message_reader_byte(c->reader);
}
void qa_make_wide_codec(qa_message_reader *reader, double protocol, const char *name,
                        double default_flags, bool is_rmq, qa_protocol_codec *out) {
  memset(out, 0, sizeof *out);
  out->reader = reader; out->protocol = protocol; out->name = name;
  out->max_msglen = QA_WIDE_MAX_MSGLEN; out->max_datagram = QA_WIDE_MAX_DATAGRAM;
  out->max_precache = QA_WIDE_MAX_PRECACHE; out->default_flags = default_flags; out->is_rmq = is_rmq;
  out->write_coord = wc_coord; out->write_angle = wc_angle;
  out->read_coord = rc_coord; out->read_angle = rc_angle;
  out->write_protocol = wc_protocol; out->write_entity_update = wc_eu;
  out->write_baseline = wc_base; out->write_static = wc_static;
  out->write_static_sound = wc_ssound; out->write_sound = wc_sound;
  out->write_clientdata = wc_cd; out->read_protocol_flags = rc_pflags;
  out->read_entity_bits = rc_ebits; out->read_entity_update_tail = rc_etail;
  out->read_baseline = rc_base; out->read_clientdata_bits = rc_cbits;
  out->read_clientdata_tail = rc_ctail; out->read_sound_header = rc_sheader;
  out->read_static_sound_index = rc_ssidx;
}
