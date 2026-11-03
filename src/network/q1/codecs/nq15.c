/* Port of src/network/q1/codecs/nq15.ts. GPL-2.0-or-later. */
#include "qa/network/q1/codecs/nq15.h"
#include <math.h>
#include <string.h>
#include "qa/foundation/js_truthy.h"
#include "qa/foundation/js.h"
static double vg(const qa_wire_vector *v, int i) { return qa_wire_vector_get(v, (size_t)i); }
static void vs(qa_wire_vector *v, int i, double x) { qa_wire_vector_set(v, (size_t)i, x); }
/* ToInt32 (NaN and infinities are 0; the fmod form cast NaN to uint32_t). */
static int32_t js_i32(double n) { return qa_js_to_int32(n); }
static void wc_coord(qa_protocol_codec *c, qa_size_buf *sb, double f, double fl) { (void)c;(void)fl; qa_msg_write_coord(sb, f); }
static void wc_angle(qa_protocol_codec *c, qa_size_buf *sb, double f, double fl) { (void)c;(void)fl; qa_msg_write_angle(sb, f); }
static double rc_coord(qa_protocol_codec *c, double fl) { (void)fl; return qa_message_reader_coord(c->reader); }
static double rc_angle(qa_protocol_codec *c, double fl) { (void)fl; return qa_message_reader_angle(c->reader); }
static void wc_protocol(qa_protocol_codec *c, qa_size_buf *sb, double fl) { (void)c;(void)fl; qa_msg_write_long(sb, QA_Q1_PROTOCOL_NETQUAKE); }
static void wc_eu(qa_protocol_codec *c, qa_size_buf *sb, double e, const qa_q1_entity_update *u, double fl) {
  (void)c;(void)fl;
  int32_t bits = 0;
  for (int i = 0; i < 3; i++) { double miss = vg(&u->origin,i) - vg(&u->baseline.origin,i); if (miss < -0.1 || miss > 0.1) bits |= QA_Q1_U_ORIGIN1 << i; }
  if (vg(&u->angles,0) != vg(&u->baseline.angles,0)) bits |= QA_Q1_U_ANGLE1;
  if (vg(&u->angles,1) != vg(&u->baseline.angles,1)) bits |= QA_Q1_U_ANGLE2;
  if (vg(&u->angles,2) != vg(&u->baseline.angles,2)) bits |= QA_Q1_U_ANGLE3;
  if (u->movetype_step) bits |= QA_Q1_U_NOLERP;
  if (u->baseline.colormap != u->colormap) bits |= QA_Q1_U_COLORMAP;
  if (u->baseline.skin != u->skin) bits |= QA_Q1_U_SKIN;
  if (u->baseline.frame != u->frame) bits |= QA_Q1_U_FRAME;
  if (u->baseline.effects != u->effects) bits |= QA_Q1_U_EFFECTS;
  if (u->baseline.modelindex != u->modelindex) bits |= QA_Q1_U_MODEL;
  if (e >= 256) bits |= QA_Q1_U_LONGENTITY;
  if (bits >= 256) bits |= QA_Q1_U_MOREBITS;
  qa_msg_write_byte(sb, bits | QA_Q1_U_SIGNAL);
  if (bits & QA_Q1_U_MOREBITS) qa_msg_write_byte(sb, (uint32_t)bits >> 8);
  if (bits & QA_Q1_U_LONGENTITY) qa_msg_write_short(sb, e); else qa_msg_write_byte(sb, e);
  if (bits & QA_Q1_U_MODEL) qa_msg_write_byte(sb, u->modelindex);
  if (bits & QA_Q1_U_FRAME) qa_msg_write_byte(sb, u->frame);
  if (bits & QA_Q1_U_COLORMAP) qa_msg_write_byte(sb, u->colormap);
  if (bits & QA_Q1_U_SKIN) qa_msg_write_byte(sb, u->skin);
  if (bits & QA_Q1_U_EFFECTS) qa_msg_write_byte(sb, u->effects);
  if (bits & QA_Q1_U_ORIGIN1) qa_msg_write_coord(sb, vg(&u->origin,0));
  if (bits & QA_Q1_U_ANGLE1) qa_msg_write_angle(sb, vg(&u->angles,0));
  if (bits & QA_Q1_U_ORIGIN2) qa_msg_write_coord(sb, vg(&u->origin,1));
  if (bits & QA_Q1_U_ANGLE2) qa_msg_write_angle(sb, vg(&u->angles,1));
  if (bits & QA_Q1_U_ORIGIN3) qa_msg_write_coord(sb, vg(&u->origin,2));
  if (bits & QA_Q1_U_ANGLE3) qa_msg_write_angle(sb, vg(&u->angles,2));
}
static void wc_base(qa_protocol_codec *c, qa_size_buf *sb, double entnum, qa_q1_wire_entity_state *b, double fl) {
  (void)c;(void)fl;
  if (js_i32(b->modelindex) & 0xff00) b->modelindex = 0;
  if (js_i32(b->frame) & 0xff00) b->frame = 0;
  b->alpha = QA_Q1_ENTALPHA_DEFAULT; b->scale = QA_Q1_ENTSCALE_DEFAULT;
  qa_msg_write_byte(sb, QA_Q1_SVC_SPAWNBASELINE); qa_msg_write_short(sb, entnum);
  qa_msg_write_byte(sb, b->modelindex); qa_msg_write_byte(sb, b->frame);
  qa_msg_write_byte(sb, b->colormap); qa_msg_write_byte(sb, b->skin);
  for (int i = 0; i < 3; i++) { qa_msg_write_coord(sb, vg(&b->origin,i)); qa_msg_write_angle(sb, vg(&b->angles,i)); }
}
static bool wc_static(qa_protocol_codec *c, qa_size_buf *sb, const qa_q1_wire_entity_state *state, double fl) {
  (void)c;(void)fl;
  if ((js_i32(state->modelindex) & 0xff00) || (js_i32(state->frame) & 0xff00)) return false;
  qa_msg_write_byte(sb, QA_Q1_SVC_SPAWNSTATIC);
  qa_msg_write_byte(sb, state->modelindex); qa_msg_write_byte(sb, state->frame);
  qa_msg_write_byte(sb, state->colormap); qa_msg_write_byte(sb, state->skin);
  for (int i = 0; i < 3; i++) { qa_msg_write_coord(sb, vg(&state->origin,i)); qa_msg_write_angle(sb, vg(&state->angles,i)); }
  return true;
}
static bool wc_ssound(qa_protocol_codec *c, qa_size_buf *sb, const qa_wire_vector *org, double snd, double vol, double atten, double fl) {
  (void)c;(void)fl;
  if (snd > 255) return false;
  qa_msg_write_byte(sb, QA_Q1_SVC_SPAWNSTATICSOUND);
  for (int i = 0; i < 3; i++) qa_msg_write_coord(sb, vg(org,i));
  qa_msg_write_byte(sb, snd); qa_msg_write_byte(sb, vol * 255); qa_msg_write_byte(sb, atten * 64);
  return true;
}
static bool wc_sound(qa_protocol_codec *c, qa_size_buf *sb, const qa_q1_sound_message *s, double fl) {
  (void)c;(void)fl;
  if (s->ent >= 8192) return false;
  if (s->sound_num >= 256 || s->channel >= 8) return false;
  int mask = 0;
  if (s->volume != QA_Q1_DEFAULT_SOUND_PACKET_VOLUME) mask |= QA_Q1_SND_VOLUME;
  if (s->attenuation != QA_Q1_DEFAULT_SOUND_PACKET_ATTENUATION) mask |= QA_Q1_SND_ATTENUATION;
  qa_msg_write_byte(sb, QA_Q1_SVC_SOUND); qa_msg_write_byte(sb, mask);
  if (mask & QA_Q1_SND_VOLUME) qa_msg_write_byte(sb, s->volume);
  if (mask & QA_Q1_SND_ATTENUATION) qa_msg_write_byte(sb, s->attenuation * 64);
  qa_msg_write_short(sb, (double)((qa_q1_js_shl(js_i32(s->ent), 3)) | js_i32(s->channel)));
  qa_msg_write_byte(sb, s->sound_num);
  for (int i = 0; i < 3; i++) qa_msg_write_coord(sb, vg(&s->origin,i));
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
  qa_msg_write_byte(sb, QA_Q1_SVC_CLIENTDATA); qa_msg_write_short(sb, bits);
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
  else {
    int weapon = 0;
    for (int i = 0; i < 32; i++) if (js_i32(cd->weapon) & (int32_t)(1u << i)) { weapon = i; break; }
    qa_msg_write_byte(sb, weapon);
  }
}
static double rc_pflags(qa_protocol_codec *c) { (void)c; return 0; }
static double rc_ebits(qa_protocol_codec *c, double bits) { (void)c; return bits; }
static void rc_etail(qa_protocol_codec *c, double b, qa_q1_entity_update_tail *out) { (void)c;(void)b; qa_q1_entity_update_tail_clear(out); }
static void rc_base(qa_protocol_codec *c, qa_q1_wire_entity_state *b, double v, double f) {
  (void)v;(void)f; qa_message_reader *r = c->reader;
  b->modelindex = qa_message_reader_byte(r); b->frame = qa_message_reader_byte(r);
  b->colormap = qa_message_reader_byte(r); b->skin = qa_message_reader_byte(r);
  for (int i = 0; i < 3; i++) { vs(&b->origin,i,qa_message_reader_coord(r)); vs(&b->angles,i,qa_message_reader_angle(r)); }
  b->alpha = QA_Q1_ENTALPHA_DEFAULT; b->scale = QA_Q1_ENTSCALE_DEFAULT;
}
static double rc_cbits(qa_protocol_codec *c) { return (double)(js_i32(qa_message_reader_short(c->reader)) & 0xffff); }
static void rc_ctail(qa_protocol_codec *c, double b, qa_q1_clientdata_tail *out) { (void)c;(void)b; qa_q1_clientdata_tail_clear(out); }
static void rc_sheader(qa_protocol_codec *c, double m, qa_q1_sound_header *out) {
  (void)m; int32_t channel = js_i32(qa_message_reader_short(c->reader)) & 0xffff;
  out->ent = channel >> 3; out->channel = channel & 7; out->sound_num = qa_message_reader_byte(c->reader);
}
static double rc_ssidx(qa_protocol_codec *c, double v) { (void)v; return qa_message_reader_byte(c->reader); }
void qa_create_nq15_codec(qa_message_reader *reader, qa_protocol_codec *out) {
  memset(out, 0, sizeof *out);
  out->reader = reader; out->protocol = QA_Q1_PROTOCOL_NETQUAKE; out->name = "NetQuake";
  out->max_msglen = QA_NQ15_MAX_MSGLEN; out->max_datagram = QA_NQ15_MAX_DATAGRAM;
  out->max_precache = QA_NQ15_MAX_PRECACHE; out->default_flags = 0;
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
