/* Port of src/network/q1/codecs/qw28.ts. GPL-2.0-or-later. */
#include "qa/network/q1/codecs/qw28.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
void qa_qw_no_netquake_op(double protocol, const char *op) {
  char buf[256]; snprintf(buf, sizeof buf, "protocol %g (QuakeWorld) has no %s", protocol, op);
  qa_q1_sys_error(buf);
}
static int32_t js_i32(double n) { uint32_t u; double m = fmod(trunc(n), 4294967296.0); if (m < 0) m += 4294967296.0; u = (uint32_t)m; return (int32_t)u; }
void qa_qw_write_angle(qa_size_buf *sb, double f) { qa_msg_write_byte(sb, (double)(js_i32(trunc((f * 256) / 360)) & 255)); }
void qa_qw_write_angle16(qa_size_buf *sb, double f) { qa_msg_write_short(sb, (double)(js_i32(trunc((f * 65536) / 360)) & 65535)); }
double qa_qw_read_angle16(qa_message_reader *reader) { return qa_message_reader_short(reader) * (360.0 / 65536); }
static double vg(const qa_wire_vector *v, int i) { return qa_wire_vector_get(v, (size_t)i); }
static void vs(qa_wire_vector *v, int i, double x) { qa_wire_vector_set(v, (size_t)i, x); }
void qa_qw_write_delta_usercmd(qa_size_buf *buf, const qa_qw_usercmd *from, const qa_qw_usercmd *cmd) {
  int bits = 0;
  if (vg(&cmd->angles,0) != vg(&from->angles,0)) bits |= QA_QW_CM_ANGLE1;
  if (vg(&cmd->angles,1) != vg(&from->angles,1)) bits |= QA_QW_CM_ANGLE2;
  if (vg(&cmd->angles,2) != vg(&from->angles,2)) bits |= QA_QW_CM_ANGLE3;
  if (cmd->forwardmove != from->forwardmove) bits |= QA_QW_CM_FORWARD;
  if (cmd->sidemove != from->sidemove) bits |= QA_QW_CM_SIDE;
  if (cmd->upmove != from->upmove) bits |= QA_QW_CM_UP;
  if (cmd->buttons != from->buttons) bits |= QA_QW_CM_BUTTONS;
  if (cmd->impulse != from->impulse) bits |= QA_QW_CM_IMPULSE;
  qa_msg_write_byte(buf, bits);
  if (bits & QA_QW_CM_ANGLE1) qa_qw_write_angle16(buf, vg(&cmd->angles,0));
  if (bits & QA_QW_CM_ANGLE2) qa_qw_write_angle16(buf, vg(&cmd->angles,1));
  if (bits & QA_QW_CM_ANGLE3) qa_qw_write_angle16(buf, vg(&cmd->angles,2));
  if (bits & QA_QW_CM_FORWARD) qa_msg_write_short(buf, cmd->forwardmove);
  if (bits & QA_QW_CM_SIDE) qa_msg_write_short(buf, cmd->sidemove);
  if (bits & QA_QW_CM_UP) qa_msg_write_short(buf, cmd->upmove);
  if (bits & QA_QW_CM_BUTTONS) qa_msg_write_byte(buf, cmd->buttons);
  if (bits & QA_QW_CM_IMPULSE) qa_msg_write_byte(buf, cmd->impulse);
  qa_msg_write_byte(buf, cmd->msec);
}
void qa_qw_read_delta_usercmd(qa_message_reader *reader, const qa_qw_usercmd *from, qa_qw_usercmd *move) {
  *move = *from;
  int bits = (int)qa_message_reader_byte(reader);
  if (bits & QA_QW_CM_ANGLE1) vs(&move->angles,0,qa_qw_read_angle16(reader));
  if (bits & QA_QW_CM_ANGLE2) vs(&move->angles,1,qa_qw_read_angle16(reader));
  if (bits & QA_QW_CM_ANGLE3) vs(&move->angles,2,qa_qw_read_angle16(reader));
  if (bits & QA_QW_CM_FORWARD) move->forwardmove = qa_message_reader_short(reader);
  if (bits & QA_QW_CM_SIDE) move->sidemove = qa_message_reader_short(reader);
  if (bits & QA_QW_CM_UP) move->upmove = qa_message_reader_short(reader);
  if (bits & QA_QW_CM_BUTTONS) move->buttons = qa_message_reader_byte(reader);
  if (bits & QA_QW_CM_IMPULSE) move->impulse = qa_message_reader_byte(reader);
  move->msec = qa_message_reader_byte(reader);
}
static void wc_coord(qa_protocol_codec *c, qa_size_buf *sb, double f, double fl) { (void)c;(void)fl; qa_msg_write_coord(sb, f); }
static void wc_angle(qa_protocol_codec *c, qa_size_buf *sb, double f, double fl) { (void)c;(void)fl; qa_qw_write_angle(sb, f); }
static double rc_coord(qa_protocol_codec *c, double fl) { (void)fl; return qa_message_reader_coord(c->reader); }
static double rc_angle(qa_protocol_codec *c, double fl) { (void)fl; return qa_message_reader_angle(c->reader); }
static void wc_protocol(qa_protocol_codec *c, qa_size_buf *sb, double fl) { (void)c;(void)fl; qa_msg_write_long(sb, QA_QW_PROTOCOL_VERSION); }
static bool wc_static(qa_protocol_codec *c, qa_size_buf *sb, const qa_q1_wire_entity_state *state, double fl) {
  (void)c;(void)fl;
  if (state->modelindex >= QA_QW28_MAX_PRECACHE || (js_i32(state->frame) & 0xff00)) return false;
  qa_msg_write_byte(sb, QA_QW_SVC_SPAWNSTATIC);
  qa_msg_write_byte(sb, state->modelindex); qa_msg_write_byte(sb, state->frame);
  qa_msg_write_byte(sb, state->colormap); qa_msg_write_byte(sb, state->skin);
  for (int i = 0; i < 3; i++) { qa_msg_write_coord(sb, vg(&state->origin,i)); qa_qw_write_angle(sb, vg(&state->angles,i)); }
  return true;
}
static bool wc_static_sound(qa_protocol_codec *c, qa_size_buf *sb, const qa_wire_vector *org, double snd, double vol, double atten, double fl) {
  (void)c;(void)fl;
  if (snd >= QA_QW28_MAX_PRECACHE) return false;
  qa_msg_write_byte(sb, QA_QW_SVC_SPAWNSTATICSOUND);
  for (int i = 0; i < 3; i++) qa_msg_write_coord(sb, vg(org,i));
  qa_msg_write_byte(sb, snd); qa_msg_write_byte(sb, vol * 255); qa_msg_write_byte(sb, atten * 64);
  return true;
}
static bool wc_sound(qa_protocol_codec *c, qa_size_buf *sb, const qa_q1_sound_message *s, double fl) {
  (void)c;(void)fl;
  if (s->ent >= 1024 || s->channel >= 8) return false;
  if (s->sound_num >= QA_QW28_MAX_PRECACHE) return false;
  int32_t chan = (js_i32(s->ent) << 3) | js_i32(s->channel);
  if (s->volume != QA_QW_DEFAULT_SOUND_PACKET_VOLUME) chan |= QA_QW_SND_VOLUME;
  if (s->attenuation != QA_QW_DEFAULT_SOUND_PACKET_ATTENUATION) chan |= QA_QW_SND_ATTENUATION;
  qa_msg_write_byte(sb, QA_QW_SVC_SOUND); qa_msg_write_short(sb, chan);
  if (chan & QA_QW_SND_VOLUME) qa_msg_write_byte(sb, s->volume);
  if (chan & QA_QW_SND_ATTENUATION) qa_msg_write_byte(sb, s->attenuation * 64);
  qa_msg_write_byte(sb, s->sound_num);
  for (int i = 0; i < 3; i++) qa_msg_write_coord(sb, vg(&s->origin,i));
  return true;
}
static void wc_entity_update(qa_protocol_codec *c, qa_size_buf *sb, double e, const qa_q1_entity_update *u, double fl) {
  (void)c;(void)sb;(void)e;(void)u;(void)fl; qa_qw_no_netquake_op(QA_QW_PROTOCOL_VERSION, "svc_update (use writeDeltaEntity)");
}
static void wc_baseline(qa_protocol_codec *c, qa_size_buf *sb, double e, qa_q1_wire_entity_state *b, double fl) {
  (void)c;(void)sb;(void)e;(void)b;(void)fl; qa_qw_no_netquake_op(QA_QW_PROTOCOL_VERSION, "a NetQuake svc_spawnbaseline (use writeQwBaseline)");
}
static void wc_clientdata(qa_protocol_codec *c, qa_size_buf *sb, const qa_q1_clientdata_codec *cd, double fl) {
  (void)c;(void)sb;(void)cd;(void)fl; qa_qw_no_netquake_op(QA_QW_PROTOCOL_VERSION, "svc_clientdata (use svc_playerinfo)");
}
static double rc_pflags(qa_protocol_codec *c) { (void)c; return 0; }
static double rc_ebits(qa_protocol_codec *c, double b) { (void)c;(void)b; qa_qw_no_netquake_op(QA_QW_PROTOCOL_VERSION, "svc_update (use readDeltaEntityHeader)"); return 0; }
static void rc_etail(qa_protocol_codec *c, double b, qa_q1_entity_update_tail *o) { (void)c;(void)b;(void)o; qa_qw_no_netquake_op(QA_QW_PROTOCOL_VERSION, "svc_update (use readDeltaEntity)"); }
static void rc_baseline(qa_protocol_codec *c, qa_q1_wire_entity_state *b, double v, double f) { (void)c;(void)b;(void)v;(void)f; qa_qw_no_netquake_op(QA_QW_PROTOCOL_VERSION, "a NetQuake svc_spawnbaseline (use readQwBaseline)"); }
static double rc_cbits(qa_protocol_codec *c) { (void)c; qa_qw_no_netquake_op(QA_QW_PROTOCOL_VERSION, "svc_clientdata"); return 0; }
static void rc_ctail(qa_protocol_codec *c, double b, qa_q1_clientdata_tail *o) { (void)c;(void)b;(void)o; qa_qw_no_netquake_op(QA_QW_PROTOCOL_VERSION, "svc_clientdata"); }
static void rc_sheader(qa_protocol_codec *c, double m, qa_q1_sound_header *o) { (void)c;(void)m;(void)o; qa_qw_no_netquake_op(QA_QW_PROTOCOL_VERSION, "NetQuake's svc_sound layout"); }
static double rc_ssidx(qa_protocol_codec *c, double v) { (void)v; return qa_message_reader_byte(c->reader); }
static void wc_du(qa_protocol_codec *c, qa_size_buf *sb, const qa_qw_usercmd *f, const qa_qw_usercmd *cmd) { (void)c; qa_qw_write_delta_usercmd(sb, f, cmd); }
static void rc_du(qa_protocol_codec *c, const qa_qw_usercmd *f, qa_qw_usercmd *m) { qa_qw_read_delta_usercmd(c->reader, f, m); }
static bool wc_de(qa_protocol_codec *c, qa_size_buf *sb, const qa_qw_entity_state *from, const qa_qw_entity_state *to, bool force, double fl) {
  (void)c;(void)fl;
  int32_t bits = 0;
  for (int i = 0; i < 3; i++) { double miss = vg(&to->origin,i) - vg(&from->origin,i); if (miss < -0.1 || miss > 0.1) bits |= QA_QW_U_ORIGIN1 << i; }
  if (vg(&to->angles,0) != vg(&from->angles,0)) bits |= QA_QW_U_ANGLE1;
  if (vg(&to->angles,1) != vg(&from->angles,1)) bits |= QA_QW_U_ANGLE2;
  if (vg(&to->angles,2) != vg(&from->angles,2)) bits |= QA_QW_U_ANGLE3;
  if (to->colormap != from->colormap) bits |= QA_QW_U_COLORMAP;
  if (to->skinnum != from->skinnum) bits |= QA_QW_U_SKIN;
  if (to->frame != from->frame) bits |= QA_QW_U_FRAME;
  if (to->effects != from->effects) bits |= QA_QW_U_EFFECTS;
  if (to->modelindex != from->modelindex) bits |= QA_QW_U_MODEL;
  if (bits & 511) bits |= QA_QW_U_MOREBITS;
  if (js_i32(to->flags) & QA_QW_U_SOLID) bits |= QA_QW_U_SOLID;
  if (to->number >= QA_QW28_MAX_ENTITY_NUMBER) return false;
  if (!bits && !force) return true;
  int32_t wi = js_i32(to->number) | (bits & ~511);
  if (wi & QA_QW_U_REMOVE) qa_q1_sys_error("U_REMOVE");
  qa_msg_write_short(sb, wi);
  if (bits & QA_QW_U_MOREBITS) qa_msg_write_byte(sb, bits & 255);
  if (bits & QA_QW_U_MODEL) qa_msg_write_byte(sb, to->modelindex);
  if (bits & QA_QW_U_FRAME) qa_msg_write_byte(sb, to->frame);
  if (bits & QA_QW_U_COLORMAP) qa_msg_write_byte(sb, to->colormap);
  if (bits & QA_QW_U_SKIN) qa_msg_write_byte(sb, to->skinnum);
  if (bits & QA_QW_U_EFFECTS) qa_msg_write_byte(sb, to->effects);
  if (bits & QA_QW_U_ORIGIN1) qa_msg_write_coord(sb, vg(&to->origin,0));
  if (bits & QA_QW_U_ANGLE1) qa_qw_write_angle(sb, vg(&to->angles,0));
  if (bits & QA_QW_U_ORIGIN2) qa_msg_write_coord(sb, vg(&to->origin,1));
  if (bits & QA_QW_U_ANGLE2) qa_qw_write_angle(sb, vg(&to->angles,1));
  if (bits & QA_QW_U_ORIGIN3) qa_msg_write_coord(sb, vg(&to->origin,2));
  if (bits & QA_QW_U_ANGLE3) qa_qw_write_angle(sb, vg(&to->angles,2));
  return true;
}
static void wc_rm(qa_protocol_codec *c, qa_size_buf *sb, double entnum) { (void)c; qa_msg_write_short(sb, (double)(js_i32(entnum) | QA_QW_U_REMOVE)); }
static void wc_end(qa_protocol_codec *c, qa_size_buf *sb) { (void)c; qa_msg_write_short(sb, 0); }
static void rc_dhdr(qa_protocol_codec *c, double word, qa_qw_entity_word *out) {
  qa_qw_entity_word_clear(out);
  int32_t w = js_i32(word);
  out->number = w & 511;
  int32_t bits = w & ~511;
  if (bits & QA_QW_U_MOREBITS) bits |= js_i32(qa_message_reader_byte(c->reader));
  out->bits = bits; out->remove = (bits & QA_QW_U_REMOVE) != 0;
}
static void rc_de(qa_protocol_codec *c, const qa_qw_entity_state *from, qa_qw_entity_state *to, const qa_qw_entity_word *hdr, double fl) {
  (void)fl;
  qa_qw_entity_state_copy_from(to, from);
  to->number = hdr->number;
  int32_t bits = js_i32(hdr->bits);
  to->flags = bits;
  qa_message_reader *r = c->reader;
  if (bits & QA_QW_U_MODEL) to->modelindex = qa_message_reader_byte(r);
  if (bits & QA_QW_U_FRAME) to->frame = qa_message_reader_byte(r);
  if (bits & QA_QW_U_COLORMAP) to->colormap = qa_message_reader_byte(r);
  if (bits & QA_QW_U_SKIN) to->skinnum = qa_message_reader_byte(r);
  if (bits & QA_QW_U_EFFECTS) to->effects = qa_message_reader_byte(r);
  if (bits & QA_QW_U_ORIGIN1) vs(&to->origin,0,qa_message_reader_coord(r));
  if (bits & QA_QW_U_ANGLE1) vs(&to->angles,0,qa_message_reader_angle(r));
  if (bits & QA_QW_U_ORIGIN2) vs(&to->origin,1,qa_message_reader_coord(r));
  if (bits & QA_QW_U_ANGLE2) vs(&to->angles,1,qa_message_reader_angle(r));
  if (bits & QA_QW_U_ORIGIN3) vs(&to->origin,2,qa_message_reader_coord(r));
  if (bits & QA_QW_U_ANGLE3) vs(&to->angles,2,qa_message_reader_angle(r));
}
static void wc_qbase(qa_protocol_codec *c, qa_size_buf *sb, const qa_qw_entity_state *es, double fl) {
  (void)c;(void)fl;
  qa_msg_write_byte(sb, es->modelindex); qa_msg_write_byte(sb, es->frame);
  qa_msg_write_byte(sb, es->colormap); qa_msg_write_byte(sb, es->skinnum);
  for (int i = 0; i < 3; i++) { qa_msg_write_coord(sb, vg(&es->origin,i)); qa_qw_write_angle(sb, vg(&es->angles,i)); }
}
static void rc_qbase(qa_protocol_codec *c, qa_qw_entity_state *es, double fl) {
  (void)fl; qa_message_reader *r = c->reader;
  es->modelindex = qa_message_reader_byte(r); es->frame = qa_message_reader_byte(r);
  es->colormap = qa_message_reader_byte(r); es->skinnum = qa_message_reader_byte(r);
  for (int i = 0; i < 3; i++) { vs(&es->origin,i,qa_message_reader_coord(r)); vs(&es->angles,i,qa_message_reader_angle(r)); }
}
static void wc_byte(qa_protocol_codec *c, qa_size_buf *sb, double n) { (void)c; qa_msg_write_byte(sb, n); }
static double rc_byte(qa_protocol_codec *c) { return qa_message_reader_byte(c->reader); }
void qa_create_qw28_codec(qa_message_reader *reader, qa_qw_protocol_codec *out) {
  memset(out, 0, sizeof *out);
  out->reader = reader; out->protocol = QA_QW_PROTOCOL_VERSION; out->name = "QuakeWorld";
  out->max_msglen = QA_Q1_MAX_MSGLEN; out->max_datagram = QA_Q1_MAX_DATAGRAM;
  out->max_precache = QA_QW28_MAX_PRECACHE; out->default_flags = 0;
  out->has_max_entity_number = true; out->max_entity_number = QA_QW28_MAX_ENTITY_NUMBER;
  out->has_max_packet_entities = true; out->max_packet_entities = QA_QW_MAX_PACKET_ENTITIES;
  out->write_coord = wc_coord; out->write_angle = wc_angle;
  out->read_coord = rc_coord; out->read_angle = rc_angle;
  out->write_protocol = wc_protocol; out->write_static = wc_static;
  out->write_static_sound = wc_static_sound; out->write_sound = wc_sound;
  out->write_entity_update = wc_entity_update; out->write_baseline = wc_baseline;
  out->write_clientdata = wc_clientdata; out->read_protocol_flags = rc_pflags;
  out->read_entity_bits = rc_ebits; out->read_entity_update_tail = rc_etail;
  out->read_baseline = rc_baseline; out->read_clientdata_bits = rc_cbits;
  out->read_clientdata_tail = rc_ctail; out->read_sound_header = rc_sheader;
  out->read_static_sound_index = rc_ssidx;
  out->write_delta_usercmd = wc_du; out->read_delta_usercmd = rc_du;
  out->write_delta_entity = wc_de; out->write_remove_entity = wc_rm;
  out->write_packet_entities_end = wc_end; out->read_delta_entity_header = rc_dhdr;
  out->read_delta_entity = rc_de; out->write_qw_baseline = wc_qbase; out->read_qw_baseline = rc_qbase;
  out->write_model_index = wc_byte; out->read_model_index = rc_byte;
  out->write_sound_index = wc_byte; out->read_sound_index = rc_byte;
  out->write_precache_count = wc_byte; out->read_precache_count = rc_byte;
}
