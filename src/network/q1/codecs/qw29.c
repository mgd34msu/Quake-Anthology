/* Port of src/network/q1/codecs/qw29.ts. Starts from the QW28 codec
 * (TS spread) and overrides the wide members. GPL-2.0-or-later. */
#include "qa/network/q1/codecs/qw29.h"
#include "qa/network/q1/codecs/qw28.h"
#include <math.h>
#include <string.h>
static double vg(const qa_wire_vector *v, int i) { return qa_wire_vector_get(v, (size_t)i); }
static void vs(qa_wire_vector *v, int i, double x) { qa_wire_vector_set(v, (size_t)i, x); }
static int32_t ji(double n) { double m = fmod(trunc(n), 4294967296.0); if (m < 0) m += 4294967296.0; return (int32_t)(uint32_t)m; }
static void write_entity_header(qa_size_buf *sb, int32_t entnum, int32_t bits_in, int32_t ext) {
  int32_t bits = bits_in;
  if (ext) bits |= QA_QW29_U_EXTEND;
  if (bits & 511) bits |= QA_QW_U_MOREBITS;
  qa_msg_write_short(sb, (entnum & 511) | (bits & ~511));
  if (bits & QA_QW_U_MOREBITS) qa_msg_write_byte(sb, bits & 255);
  if (bits & QA_QW29_U_EXTEND) qa_msg_write_byte(sb, ext);
  if (ext & QA_QW29_U_ENTITY2) qa_msg_write_byte(sb, (entnum >> 9) & 255);
}
static void wc_coord(qa_protocol_codec *c, qa_size_buf *sb, double f, double fl) { (void)c; qa_msg_write_coord_flags(sb, f, (int64_t)fl); }
static void wc_angle(qa_protocol_codec *c, qa_size_buf *sb, double f, double fl) { (void)c; qa_msg_write_angle_flags(sb, f, (int64_t)fl); }
static double rc_coord(qa_protocol_codec *c, double fl) { return qa_message_reader_coord_flags(c->reader, (int64_t)fl); }
static double rc_angle(qa_protocol_codec *c, double fl) { return qa_message_reader_angle_flags(c->reader, (int64_t)fl); }
static void wc_protocol(qa_protocol_codec *c, qa_size_buf *sb, double fl) { (void)c; qa_msg_write_long(sb, QA_PROTOCOL_QW_WIDE); qa_msg_write_long(sb, fl); }
static bool wc_static(qa_protocol_codec *c, qa_size_buf *sb, const qa_q1_wire_entity_state *state, double fl) {
  (void)c;
  if (state->modelindex >= QA_Q1_MAX_MODELS || (ji(state->frame) & (int32_t)0xffff0000u)) return false;
  qa_msg_write_byte(sb, QA_QW_SVC_SPAWNSTATIC);
  qa_msg_write_short(sb, state->modelindex); qa_msg_write_short(sb, state->frame);
  qa_msg_write_byte(sb, state->colormap); qa_msg_write_byte(sb, state->skin);
  qa_msg_write_byte(sb, state->alpha); qa_msg_write_byte(sb, state->scale);
  for (int i = 0; i < 3; i++) { qa_msg_write_coord_flags(sb, vg(&state->origin,i), (int64_t)fl); qa_msg_write_angle_flags(sb, vg(&state->angles,i), (int64_t)fl); }
  return true;
}
static bool wc_ssound(qa_protocol_codec *c, qa_size_buf *sb, const qa_wire_vector *org, double snd, double vol, double atten, double fl) {
  (void)c;
  if (snd > 65535) return false;
  qa_msg_write_byte(sb, QA_QW_SVC_SPAWNSTATICSOUND);
  for (int i = 0; i < 3; i++) qa_msg_write_coord_flags(sb, vg(org,i), (int64_t)fl);
  qa_msg_write_short(sb, snd); qa_msg_write_byte(sb, vol * 255); qa_msg_write_byte(sb, atten * 64);
  return true;
}
static bool wc_sound(qa_protocol_codec *c, qa_size_buf *sb, const qa_q1_sound_message *s, double fl) {
  (void)c;
  if (s->ent >= 1024 || s->channel >= 8) return false;
  if (s->sound_num > 65535) return false;
  int32_t chan = (ji(s->ent) << 3) | ji(s->channel);
  if (s->volume != QA_QW_DEFAULT_SOUND_PACKET_VOLUME) chan |= QA_QW_SND_VOLUME;
  if (s->attenuation != QA_QW_DEFAULT_SOUND_PACKET_ATTENUATION) chan |= QA_QW_SND_ATTENUATION;
  qa_msg_write_byte(sb, QA_QW_SVC_SOUND); qa_msg_write_short(sb, chan);
  if (chan & QA_QW_SND_VOLUME) qa_msg_write_byte(sb, s->volume);
  if (chan & QA_QW_SND_ATTENUATION) qa_msg_write_byte(sb, s->attenuation * 64);
  qa_msg_write_short(sb, s->sound_num);
  for (int i = 0; i < 3; i++) qa_msg_write_coord_flags(sb, vg(&s->origin,i), (int64_t)fl);
  return true;
}
static double rc_pflags(qa_protocol_codec *c) { return qa_message_reader_long(c->reader); }
static double rc_ssidx(qa_protocol_codec *c, double v) { (void)v; return (double)(ji(qa_message_reader_short(c->reader)) & 0xffff); }
static bool wc_de(qa_protocol_codec *c, qa_size_buf *sb, const qa_qw_entity_state *from, const qa_qw_entity_state *to, bool force, double fl) {
  (void)c;
  int32_t bits = 0, ext = 0;
  for (int i = 0; i < 3; i++) { double miss = vg(&to->origin,i) - vg(&from->origin,i); if (miss < -0.1 || miss > 0.1) bits |= QA_QW_U_ORIGIN1 << i; }
  if (vg(&to->angles,0) != vg(&from->angles,0)) bits |= QA_QW_U_ANGLE1;
  if (vg(&to->angles,1) != vg(&from->angles,1)) bits |= QA_QW_U_ANGLE2;
  if (vg(&to->angles,2) != vg(&from->angles,2)) bits |= QA_QW_U_ANGLE3;
  if (to->colormap != from->colormap) bits |= QA_QW_U_COLORMAP;
  if (to->skinnum != from->skinnum) bits |= QA_QW_U_SKIN;
  if (to->frame != from->frame) bits |= QA_QW_U_FRAME;
  if (to->effects != from->effects) bits |= QA_QW_U_EFFECTS;
  if (to->modelindex != from->modelindex) bits |= QA_QW_U_MODEL;
  if ((bits & QA_QW_U_MODEL) && (ji(to->modelindex) & 0xff00) != 0) ext |= QA_QW29_U_MODEL2;
  if ((bits & QA_QW_U_FRAME) && (ji(to->frame) & 0xff00) != 0) ext |= QA_QW29_U_FRAME2;
  if (to->alpha != from->alpha) ext |= QA_QW29_U_ALPHA;
  if (to->scale != from->scale) ext |= QA_QW29_U_SCALE;
  if (ji(to->flags) & QA_QW_U_SOLID) bits |= QA_QW_U_SOLID;
  if (to->number >= QA_QW29_MAX_ENTITY_NUMBER) return false;
  if (!bits && !ext && !force) return true;
  if (to->number >= 512) ext |= QA_QW29_U_ENTITY2;
  if (bits & QA_QW_U_REMOVE) qa_q1_sys_error("U_REMOVE");
  write_entity_header(sb, ji(to->number), bits, ext);
  if (bits & QA_QW_U_MODEL) qa_msg_write_byte(sb, ji(to->modelindex) & 255);
  if (bits & QA_QW_U_FRAME) qa_msg_write_byte(sb, ji(to->frame) & 255);
  if (bits & QA_QW_U_COLORMAP) qa_msg_write_byte(sb, to->colormap);
  if (bits & QA_QW_U_SKIN) qa_msg_write_byte(sb, to->skinnum);
  if (bits & QA_QW_U_EFFECTS) qa_msg_write_byte(sb, to->effects);
  if (bits & QA_QW_U_ORIGIN1) qa_msg_write_coord_flags(sb, vg(&to->origin,0), (int64_t)fl);
  if (bits & QA_QW_U_ANGLE1) qa_msg_write_angle_flags(sb, vg(&to->angles,0), (int64_t)fl);
  if (bits & QA_QW_U_ORIGIN2) qa_msg_write_coord_flags(sb, vg(&to->origin,1), (int64_t)fl);
  if (bits & QA_QW_U_ANGLE2) qa_msg_write_angle_flags(sb, vg(&to->angles,1), (int64_t)fl);
  if (bits & QA_QW_U_ORIGIN3) qa_msg_write_coord_flags(sb, vg(&to->origin,2), (int64_t)fl);
  if (bits & QA_QW_U_ANGLE3) qa_msg_write_angle_flags(sb, vg(&to->angles,2), (int64_t)fl);
  if (ext & QA_QW29_U_MODEL2) qa_msg_write_byte(sb, (ji(to->modelindex) >> 8) & 255);
  if (ext & QA_QW29_U_FRAME2) qa_msg_write_byte(sb, (ji(to->frame) >> 8) & 255);
  if (ext & QA_QW29_U_ALPHA) qa_msg_write_byte(sb, to->alpha);
  if (ext & QA_QW29_U_SCALE) qa_msg_write_byte(sb, to->scale);
  return true;
}
static void wc_rm(qa_protocol_codec *c, qa_size_buf *sb, double entnum) {
  (void)c; write_entity_header(sb, ji(entnum), QA_QW_U_REMOVE, entnum >= 512 ? QA_QW29_U_ENTITY2 : 0);
}
static void wc_end(qa_protocol_codec *c, qa_size_buf *sb) { (void)c; qa_msg_write_short(sb, 0); }
static void rc_dhdr(qa_protocol_codec *c, double word, qa_qw_entity_word *out) {
  qa_qw_entity_word_clear(out);
  int32_t w = ji(word);
  int32_t number = w & 511, bits = w & ~511;
  if (bits & QA_QW_U_MOREBITS) bits |= ji(qa_message_reader_byte(c->reader));
  int32_t ext = 0;
  if (bits & QA_QW29_U_EXTEND) {
    ext = ji(qa_message_reader_byte(c->reader));
    if (ext & QA_QW29_U_ENTITY2) number |= ji(qa_message_reader_byte(c->reader)) << 9;
  }
  out->bits = bits; out->ext = ext; out->number = number; out->remove = (bits & QA_QW_U_REMOVE) != 0;
}
static void rc_de(qa_protocol_codec *c, const qa_qw_entity_state *from, qa_qw_entity_state *to, const qa_qw_entity_word *hdr, double fl) {
  qa_message_reader *r = c->reader;
  qa_qw_entity_state_copy_from(to, from);
  to->number = hdr->number;
  int32_t bits = ji(hdr->bits), ext = ji(hdr->ext);
  to->flags = bits;
  if (bits & QA_QW_U_MODEL) to->modelindex = qa_message_reader_byte(r);
  if (bits & QA_QW_U_FRAME) to->frame = qa_message_reader_byte(r);
  if (bits & QA_QW_U_COLORMAP) to->colormap = qa_message_reader_byte(r);
  if (bits & QA_QW_U_SKIN) to->skinnum = qa_message_reader_byte(r);
  if (bits & QA_QW_U_EFFECTS) to->effects = qa_message_reader_byte(r);
  if (bits & QA_QW_U_ORIGIN1) vs(&to->origin,0,qa_message_reader_coord_flags(r,(int64_t)fl));
  if (bits & QA_QW_U_ANGLE1) vs(&to->angles,0,qa_message_reader_angle_flags(r,(int64_t)fl));
  if (bits & QA_QW_U_ORIGIN2) vs(&to->origin,1,qa_message_reader_coord_flags(r,(int64_t)fl));
  if (bits & QA_QW_U_ANGLE2) vs(&to->angles,1,qa_message_reader_angle_flags(r,(int64_t)fl));
  if (bits & QA_QW_U_ORIGIN3) vs(&to->origin,2,qa_message_reader_coord_flags(r,(int64_t)fl));
  if (bits & QA_QW_U_ANGLE3) vs(&to->angles,2,qa_message_reader_angle_flags(r,(int64_t)fl));
  if (ext & QA_QW29_U_MODEL2) to->modelindex = ji(to->modelindex) | (ji(qa_message_reader_byte(r)) << 8);
  if (ext & QA_QW29_U_FRAME2) to->frame = ji(to->frame) | (ji(qa_message_reader_byte(r)) << 8);
  if (ext & QA_QW29_U_ALPHA) to->alpha = qa_message_reader_byte(r);
  if (ext & QA_QW29_U_SCALE) to->scale = qa_message_reader_byte(r);
}
static void wc_qbase(qa_protocol_codec *c, qa_size_buf *sb, const qa_qw_entity_state *es, double fl) {
  (void)c;
  qa_msg_write_short(sb, es->modelindex); qa_msg_write_short(sb, es->frame);
  qa_msg_write_byte(sb, es->colormap); qa_msg_write_byte(sb, es->skinnum);
  qa_msg_write_byte(sb, es->alpha); qa_msg_write_byte(sb, es->scale);
  for (int i = 0; i < 3; i++) { qa_msg_write_coord_flags(sb, vg(&es->origin,i), (int64_t)fl); qa_msg_write_angle_flags(sb, vg(&es->angles,i), (int64_t)fl); }
}
static void rc_qbase(qa_protocol_codec *c, qa_qw_entity_state *es, double fl) {
  qa_message_reader *r = c->reader;
  es->modelindex = ji(qa_message_reader_short(r)) & 0xffff;
  es->frame = ji(qa_message_reader_short(r)) & 0xffff;
  es->colormap = qa_message_reader_byte(r); es->skinnum = qa_message_reader_byte(r);
  es->alpha = qa_message_reader_byte(r); es->scale = qa_message_reader_byte(r);
  for (int i = 0; i < 3; i++) { vs(&es->origin,i,qa_message_reader_coord_flags(r,(int64_t)fl)); vs(&es->angles,i,qa_message_reader_angle_flags(r,(int64_t)fl)); }
}
static void wc_short(qa_protocol_codec *c, qa_size_buf *sb, double n) { (void)c; qa_msg_write_short(sb, n); }
static double rc_short16(qa_protocol_codec *c) { return (double)(ji(qa_message_reader_short(c->reader)) & 0xffff); }
static void wc_eu29(qa_protocol_codec *c, qa_size_buf *sb, double e, const qa_q1_entity_update *u, double fl) { (void)c;(void)sb;(void)e;(void)u;(void)fl; qa_qw_no_netquake_op(QA_PROTOCOL_QW_WIDE, "svc_update (use writeDeltaEntity)"); }
static void wc_base29(qa_protocol_codec *c, qa_size_buf *sb, double e, qa_q1_wire_entity_state *b, double fl) { (void)c;(void)sb;(void)e;(void)b;(void)fl; qa_qw_no_netquake_op(QA_PROTOCOL_QW_WIDE, "a NetQuake svc_spawnbaseline (use writeQwBaseline)"); }
static void wc_cd29(qa_protocol_codec *c, qa_size_buf *sb, const qa_q1_clientdata_codec *cd, double fl) { (void)c;(void)sb;(void)cd;(void)fl; qa_qw_no_netquake_op(QA_PROTOCOL_QW_WIDE, "svc_clientdata (use svc_playerinfo)"); }
static double rc_ebits29(qa_protocol_codec *c, double b) { (void)c;(void)b; qa_qw_no_netquake_op(QA_PROTOCOL_QW_WIDE, "svc_update (use readDeltaEntityHeader)"); return 0; }
static void rc_etail29(qa_protocol_codec *c, double b, qa_q1_entity_update_tail *o) { (void)c;(void)b;(void)o; qa_qw_no_netquake_op(QA_PROTOCOL_QW_WIDE, "svc_update (use readDeltaEntity)"); }
static void rc_base29(qa_protocol_codec *c, qa_q1_wire_entity_state *b, double v, double f) { (void)c;(void)b;(void)v;(void)f; qa_qw_no_netquake_op(QA_PROTOCOL_QW_WIDE, "a NetQuake svc_spawnbaseline (use readQwBaseline)"); }
static double rc_cbits29(qa_protocol_codec *c) { (void)c; qa_qw_no_netquake_op(QA_PROTOCOL_QW_WIDE, "svc_clientdata"); return 0; }
static void rc_ctail29(qa_protocol_codec *c, double b, qa_q1_clientdata_tail *o) { (void)c;(void)b;(void)o; qa_qw_no_netquake_op(QA_PROTOCOL_QW_WIDE, "svc_clientdata"); }
static void rc_sheader29(qa_protocol_codec *c, double m, qa_q1_sound_header *o) { (void)c;(void)m;(void)o; qa_qw_no_netquake_op(QA_PROTOCOL_QW_WIDE, "NetQuake's svc_sound layout"); }
void qa_create_qw29_codec(qa_message_reader *reader, qa_qw_protocol_codec *out) {
  qa_create_qw28_codec(reader, out);
  out->protocol = QA_PROTOCOL_QW_WIDE; out->name = "QuakeWorld wide";
  out->max_precache = QA_Q1_MAX_MODELS; out->default_flags = QA_QW29_DEFAULT_FLAGS;
  out->max_entity_number = QA_QW29_MAX_ENTITY_NUMBER;
  out->write_coord = wc_coord; out->write_angle = wc_angle;
  out->read_coord = rc_coord; out->read_angle = rc_angle;
  out->write_protocol = wc_protocol; out->write_static = wc_static;
  out->write_static_sound = wc_ssound; out->write_sound = wc_sound;
  out->read_protocol_flags = rc_pflags; out->read_static_sound_index = rc_ssidx;
  out->write_delta_entity = wc_de; out->write_remove_entity = wc_rm;
  out->write_packet_entities_end = wc_end; out->read_delta_entity_header = rc_dhdr;
  out->read_delta_entity = rc_de; out->write_qw_baseline = wc_qbase; out->read_qw_baseline = rc_qbase;
  out->write_model_index = wc_short; out->read_model_index = rc_short16;
  out->write_sound_index = wc_short; out->read_sound_index = rc_short16;
  out->write_precache_count = wc_short; out->read_precache_count = rc_short16;
  out->write_entity_update = wc_eu29; out->write_baseline = wc_base29; out->write_clientdata = wc_cd29;
  out->read_entity_bits = rc_ebits29; out->read_entity_update_tail = rc_etail29;
  out->read_baseline = rc_base29; out->read_clientdata_bits = rc_cbits29;
  out->read_clientdata_tail = rc_ctail29; out->read_sound_header = rc_sheader29;
}
