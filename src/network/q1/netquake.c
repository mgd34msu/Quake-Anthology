/* Port of src/network/q1/netquake.ts. GPL-2.0-or-later. */
#include "qa/network/q1/netquake.h"
#include "qa/foundation/support.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int32_t ji(double n) { double m = fmod(trunc(n), 4294967296.0); if (m < 0) m += 4294967296.0; return (int32_t)(uint32_t)m; }
static uint32_t ju(double n) { return (uint32_t)ji(n); }
static qa_vec3 vec3_of(double x, double y, double z) { qa_vec3 v; v.x = x; v.y = y; v.z = z; return v; }
static qa_vec3 read_vec_coord(qa_coord_fn coord, void *ctx) { return vec3_of(coord(ctx), coord(ctx), coord(ctx)); }
qa_temporary_entity qa_read_temporary_entity(qa_message_reader *r, qa_coord_fn coord, void *ctx, bool quakeworld) {
  qa_temporary_entity out; memset(&out, 0, sizeof out);
  double type = qa_message_reader_byte(r); out.type = type;
  if (type == 5 || type == 6 || type == 9 || (!quakeworld && type == 13)) {
    out.kind = QA_TE_BEAM;
    out.entity = (double)(ji(qa_message_reader_short(r)) & 65535);
    out.start = read_vec_coord(coord, ctx); out.end = read_vec_coord(coord, ctx);
    return out;
  }
  if (type < 0 || type > 13) {
    char msg[64]; snprintf(msg, sizeof msg, "Unknown temporary entity %g", type);
    qa_q1_packet_error(msg, r->offset - 1);
  }
  double count = (quakeworld && (type == 2 || type == 12)) ? qa_message_reader_byte(r) : 1;
  out.origin = read_vec_coord(coord, ctx); out.count = count;
  if (!quakeworld && type == 12) {
    out.kind = QA_TE_EXPLOSION_COLORS;
    out.color_start = qa_message_reader_byte(r); out.color_length = qa_message_reader_byte(r);
    return out;
  }
  out.kind = QA_TE_POINT; return out;
}
void qa_netquake_message_destroy(qa_netquake_message *m) {
  free(m->text); free(m->level); free(m->str_value);
  for (size_t i = 0; i < m->model_count; i++) free(m->models[i]);
  for (size_t i = 0; i < m->sound_count; i++) free(m->sounds[i]);
  free(m->models); free(m->sounds);
  memset(m, 0, sizeof *m);
}
qa_q1_extended_entity_state qa_nq_entity_state_from_wire(double number, const qa_q1_wire_entity_state *s) {
  qa_q1_extended_entity_state o; memset(&o, 0, sizeof o);
  o.base.number = number;
  o.base.origin = vec3_of(qa_wire_vector_get(&s->origin,0), qa_wire_vector_get(&s->origin,1), qa_wire_vector_get(&s->origin,2));
  o.base.angles = vec3_of(qa_wire_vector_get(&s->angles,0), qa_wire_vector_get(&s->angles,1), qa_wire_vector_get(&s->angles,2));
  o.base.model_index = s->modelindex; o.base.frame = s->frame; o.base.color_map = s->colormap;
  o.base.skin = s->skin; o.base.effects = s->effects;
  o.alpha = s->alpha; o.scale = s->scale; o.lerp_finish_seconds = 0; o.step = false;
  return o;
}
void qa_nq_wire_entity_from_extended(const qa_q1_extended_entity_state *s, qa_q1_wire_entity_state *out) {
  qa_q1_wire_entity_state_init(out);
  qa_wire_vector_set(&out->origin,0,s->base.origin.x); qa_wire_vector_set(&out->origin,1,s->base.origin.y); qa_wire_vector_set(&out->origin,2,s->base.origin.z);
  qa_wire_vector_set(&out->angles,0,s->base.angles.x); qa_wire_vector_set(&out->angles,1,s->base.angles.y); qa_wire_vector_set(&out->angles,2,s->base.angles.z);
  out->modelindex = s->base.model_index; out->frame = s->base.frame; out->colormap = s->base.color_map;
  out->skin = s->base.skin; out->effects = s->base.effects; out->alpha = s->alpha; out->scale = s->scale;
}
void qa_netquake_decoder_init(qa_netquake_decoder *d, qa_q1_protocol_identity protocol,
                              const char *rerelease_messages, bool standard_quake) {
  memset(d, 0, sizeof *d);
  d->protocol = protocol;
  d->rerelease_messages = rerelease_messages ? rerelease_messages : "known-retail";
  d->standard_quake = standard_quake;
  d->flags_value = qa_q1_protocol_flags(protocol);
}
void qa_netquake_decoder_destroy(qa_netquake_decoder *d) { free(d->baselines); memset(d, 0, sizeof *d); }
double qa_netquake_decoder_get_flags(const qa_netquake_decoder *d) { return d->flags_value; }
static qa_q1_wire_entity_state *baseline_get(qa_netquake_decoder *d, double slot) {
  for (size_t i = 0; i < d->baseline_count; i++) if (d->baselines[i].slot == slot) return &d->baselines[i].state;
  return NULL;
}
static void baseline_set(qa_netquake_decoder *d, double slot, const qa_q1_wire_entity_state *s) {
  qa_q1_wire_entity_state *old = baseline_get(d, slot);
  if (old) { *old = *s; return; }
  if (d->baseline_count == d->baseline_cap) { size_t nc = d->baseline_cap ? d->baseline_cap * 2 : 16; d->baselines = realloc(d->baselines, nc * sizeof *d->baselines); d->baseline_cap = nc; }
  d->baselines[d->baseline_count].slot = slot; d->baselines[d->baseline_count].state = *s; d->baseline_count++;
}
void qa_netquake_decoder_capture(const qa_netquake_decoder *d, qa_nq_decoder_capture *out) {
  memset(out, 0, sizeof *out);
  out->version = (double)d->protocol.version; out->flags = d->flags_value; out->time_seconds = d->time_seconds;
  out->count = d->baseline_count;
  out->baselines = malloc((out->count ? out->count : 1) * sizeof *out->baselines);
  for (size_t i = 0; i < out->count; i++) {
    out->baselines[i].slot = d->baselines[i].slot;
    out->baselines[i].state = qa_capture_wire_entity_nq(&d->baselines[i].state);
  }
}
void qa_nq_decoder_capture_destroy(qa_nq_decoder_capture *c) { free(c->baselines); memset(c, 0, sizeof *c); }
void qa_netquake_decoder_restore(qa_netquake_decoder *d, const qa_save_reader *reader) {
  qa_save_reader f_flags, f_ver, f_time, f_base;
  (void)qa_save_reader_field(reader, "flags", &f_flags);
  double flags = qa_save_reader_integer_min(&f_flags, 0); qa_save_reader_destroy(&f_flags);
  (void)qa_save_reader_field(reader, "version", &f_ver);
  double version = qa_save_reader_integer_min(&f_ver, 0); qa_save_reader_destroy(&f_ver);
  d->protocol = qa_netquake_profile(version, flags); d->flags_value = flags;
  (void)qa_save_reader_field(reader, "timeSeconds", &f_time);
  d->time_seconds = qa_save_reader_number(&f_time); qa_save_reader_destroy(&f_time);
  d->baseline_count = 0;
  (void)qa_save_reader_field(reader, "baselines", &f_base);
  size_t n = qa_save_reader_list_count(&f_base);
  for (size_t i = 0; i < n; i++) {
    qa_save_reader item, f_slot, f_state;
    (void)qa_save_reader_item(&f_base, i, &item);
    (void)qa_save_reader_field(&item, "slot", &f_slot);
    double slot = qa_save_reader_integer_min(&f_slot, 0); qa_save_reader_destroy(&f_slot);
    (void)qa_save_reader_field(&item, "state", &f_state);
    qa_q1_wire_entity_state st; qa_read_nq_wire_entity(&f_state, &st); qa_save_reader_destroy(&f_state);
    if (baseline_get(d, slot)) qa_save_reader_fail(reader, "duplicate baseline");
    baseline_set(d, slot, &st);
    qa_save_reader_destroy(&item);
  }
  qa_save_reader_destroy(&f_base);
}
struct coord_ctx { qa_protocol_codec *codec; double flags; };
static double coord_read(void *ctx) { struct coord_ctx *c = ctx; return c->codec->read_coord(c->codec, c->flags); }
static double angle_read(void *ctx) { struct coord_ctx *c = ctx; return c->codec->read_angle(c->codec, c->flags); }
static char **read_list(qa_message_reader *r, double max, size_t *out_count) {
  size_t cap = 8, count = 0; char **values = malloc(cap * sizeof *values);
  for (;;) {
    size_t len = 0; char *v = qa_message_reader_string(r, 2047, false, &len);
    qa_message_reader_finish(r);
    if (v[0] == '\0') { free(v); break; }
    if ((double)count >= max - 1) { free(v); qa_q1_packet_error("Precache overflow", r->offset); }
    if (count == cap) { cap *= 2; values = realloc(values, cap * sizeof *values); }
    values[count++] = v;
  }
  *out_count = count; return values;
}
static qa_q1_extended_entity_state read_entity(qa_netquake_decoder *d, qa_message_reader *r, qa_protocol_codec *codec, double flags, double initial) {
  int32_t bits = ji(initial);
  if (bits & QA_Q1_U_MOREBITS) bits |= ji(qa_message_reader_byte(r)) << 8;
  bits = ji(codec->read_entity_bits(codec, bits));
  double number = (bits & QA_Q1_U_LONGENTITY) ? (double)(ji(qa_message_reader_short(r)) & 65535) : qa_message_reader_byte(r);
  qa_q1_wire_entity_state s;
  qa_q1_wire_entity_state *b = baseline_get(d, number);
  if (b) qa_q1_wire_entity_state_copy_from(&s, b); else qa_q1_wire_entity_state_init(&s);
  if (bits & QA_Q1_U_MODEL) s.modelindex = qa_message_reader_byte(r);
  if (bits & QA_Q1_U_FRAME) s.frame = qa_message_reader_byte(r);
  if (bits & QA_Q1_U_COLORMAP) s.colormap = qa_message_reader_byte(r);
  if (bits & QA_Q1_U_SKIN) s.skin = qa_message_reader_byte(r);
  if (bits & QA_Q1_U_EFFECTS) s.effects = qa_message_reader_byte(r);
  static const int32_t angle_bits[3] = { QA_Q1_U_ANGLE1, QA_Q1_U_ANGLE2, QA_Q1_U_ANGLE3 };
  for (int axis = 0; axis < 3; axis++) {
    if (bits & (QA_Q1_U_ORIGIN1 << axis)) qa_wire_vector_set(&s.origin, (size_t)axis, codec->read_coord(codec, flags));
    if (bits & angle_bits[axis]) qa_wire_vector_set(&s.angles, (size_t)axis, codec->read_angle(codec, flags));
  }
  qa_q1_entity_update_tail tail; qa_q1_entity_update_tail_init(&tail);
  codec->read_entity_update_tail(codec, bits, &tail);
  if (tail.has_alpha) s.alpha = tail.alpha;
  if (tail.has_scale) s.scale = tail.scale;
  if (tail.has_frame2) s.frame = ji(s.frame) | (ji(tail.frame_high) << 8);
  if (tail.has_model2) s.modelindex = ji(s.modelindex) | (ji(tail.model_high) << 8);
  qa_q1_extended_entity_state out = qa_nq_entity_state_from_wire(number, &s);
  out.step = (bits & QA_Q1_U_STEP) != 0;
  out.lerp_finish_seconds = tail.has_lerpfinish ? d->time_seconds + tail.lerpfinish : 0;
  return out;
}
static void read_client_data(qa_netquake_decoder *d, qa_message_reader *r, qa_protocol_codec *codec, qa_netquake_message *m) {
  int32_t bits = ji(codec->read_clientdata_bits(codec));
  double view_height = (bits & QA_Q1_SU_VIEWHEIGHT) ? qa_message_reader_char(r) : 22;
  double ideal_pitch = (bits & QA_Q1_SU_IDEALPITCH) ? qa_message_reader_char(r) : 0;
  double punch[3] = {0,0,0}, velocity[3] = {0,0,0};
  for (int axis = 0; axis < 3; axis++) {
    punch[axis] = (bits & (QA_Q1_SU_PUNCH1 << axis)) ? qa_message_reader_char(r) : 0;
    velocity[axis] = (bits & (QA_Q1_SU_VELOCITY1 << axis)) ? qa_message_reader_char(r) * 16 : 0;
  }
  double items = qa_message_reader_long(r);
  double weapon_frame = (bits & QA_Q1_SU_WEAPONFRAME) ? qa_message_reader_byte(r) : 0;
  double armor = (bits & QA_Q1_SU_ARMOR) ? qa_message_reader_byte(r) : 0;
  double weapon_model = (bits & QA_Q1_SU_WEAPON) ? qa_message_reader_byte(r) : 0;
  double health = qa_message_reader_short(r), ammo = qa_message_reader_byte(r);
  double shells = qa_message_reader_byte(r), nails = qa_message_reader_byte(r);
  double rockets = qa_message_reader_byte(r), cells = qa_message_reader_byte(r), weapon = qa_message_reader_byte(r);
  qa_q1_clientdata_tail tail; qa_q1_clientdata_tail_init(&tail);
  codec->read_clientdata_tail(codec, bits, &tail);
  m->kind = "client-data"; m->weapon_alpha = tail.weaponalpha;
  qa_q1_client_data *cd = &m->data; memset(cd, 0, sizeof *cd);
  cd->view_height = view_height; cd->ideal_pitch = ideal_pitch;
  cd->punch_angles = vec3_of(punch[0], punch[1], punch[2]);
  cd->velocity = vec3_of(velocity[0], velocity[1], velocity[2]);
  cd->items = items; cd->on_ground = (bits & QA_Q1_SU_ONGROUND) != 0; cd->in_water = (bits & QA_Q1_SU_INWATER) != 0;
  cd->weapon_frame = ji(weapon_frame) | (ji(tail.weaponframe_high) << 8);
  cd->armor = ji(armor) | (ji(tail.armor_high) << 8);
  cd->weapon_model = ji(weapon_model) | (ji(tail.weapon_high) << 8);
  cd->health = health;
  cd->ammo = ji(ammo) | (ji(tail.ammo_high) << 8);
  cd->shells = ji(shells) | (ji(tail.shells_high) << 8);
  cd->nails = ji(nails) | (ji(tail.nails_high) << 8);
  cd->rockets = ji(rockets) | (ji(tail.rockets_high) << 8);
  cd->cells = ji(cells) | (ji(tail.cells_high) << 8);
  cd->active_weapon = d->standard_quake ? weapon : (double)(1 << ji(weapon));
}
static void read_private(qa_netquake_decoder *d, qa_message_reader *r, double opd, qa_netquake_message *m) {
  int op = (int)opd; size_t dummy;
  if (strcmp(d->rerelease_messages, "q1-rerelease-private") != 0) {
    char msg[80]; snprintf(msg, sizeof msg, "Unknown or unverified retail service %d", op);
    qa_q1_packet_error(msg, r->offset - 1);
  }
  switch (op) {
    case 38: m->kind = "botchat"; m->text = qa_message_reader_string(r, 2047, false, &dummy); return;
    case 39: m->kind = "spawned-monster"; m->value = qa_message_reader_byte(r); return;
    case 45: m->kind = "set-views"; m->value = qa_message_reader_byte(r); return;
    case 46: m->kind = "ping"; m->slot = qa_message_reader_byte(r); m->value = qa_message_reader_short(r); return;
    case 47: m->kind = "social"; m->slot = qa_message_reader_byte(r); m->str_value = qa_message_reader_string(r, 2047, false, &dummy); return;
    case 48: m->kind = "player-info"; m->slot = qa_message_reader_byte(r); m->str_value = qa_message_reader_string(r, 2047, false, &dummy); return;
    case 49: m->kind = "raw-print"; m->text = qa_message_reader_string(r, 2047, false, &dummy); return;
    case 50: m->kind = "server-vars"; m->text = qa_message_reader_string(r, 2047, false, &dummy); return;
    case 51: m->kind = "sequence"; m->value = qa_message_reader_long(r); return;
    case 53: m->kind = "chat"; m->text = qa_message_reader_string(r, 2047, false, &dummy); return;
    case 54: m->kind = "level-completed"; return;
    case 55: m->kind = "back-to-lobby"; return;
    case 57: {
      double sub = qa_message_reader_byte(r);
      if (sub == 0) { m->kind = "prompt-begin"; m->text = qa_message_reader_string(r, 2047, false, &dummy); m->choices = qa_message_reader_byte(r); return; }
      if (sub == 1) { m->kind = "prompt-choice"; m->text = qa_message_reader_string(r, 2047, false, &dummy); m->impulse = qa_message_reader_byte(r); return; }
      if (sub == 2) { m->kind = "prompt-clear"; return; }
      char msg[64]; snprintf(msg, sizeof msg, "Unknown private prompt %g", sub);
      qa_q1_packet_error(msg, r->offset - 1);
    }
    default: {
      char msg[64]; snprintf(msg, sizeof msg, "Unknown service %d", op);
      qa_q1_packet_error(msg, r->offset - 1);
    }
  }
}
qa_netquake_message *qa_netquake_decoder_decode(qa_netquake_decoder *d, const uint8_t *bytes, size_t length, size_t *out_count) {
  qa_message_reader r; qa_message_reader_init(&r, bytes, length);
  qa_protocol_codec codec; qa_create_netquake_codec(d->protocol, &r, &codec);
  double flags = d->flags_value;
  struct coord_ctx cc = { &codec, flags };
  size_t cap = 8, count = 0;
  qa_netquake_message *messages = calloc(cap, sizeof *messages);
  size_t dummy;
  while (qa_message_reader_remaining(&r) > 0) {
    double op = qa_message_reader_byte(&r);
    qa_netquake_message m; memset(&m, 0, sizeof m);
    if (ji(op) & 128) {
      m.kind = "entity";
      cc.codec = &codec; cc.flags = flags;
      m.state = read_entity(d, &r, &codec, flags, (double)(ji(op) & 127));
    } else {
      cc.codec = &codec; cc.flags = flags;
      switch ((int)op) {
        case 1: m.kind = "nop"; break;
        case 2: m.kind = "disconnect"; break;
        case 3: m.kind = "stat"; m.index = qa_message_reader_byte(&r); m.value = qa_message_reader_long(&r); break;
        case 4: {
          double version = qa_message_reader_long(&r);
          d->protocol = qa_netquake_profile(version, flags);
          qa_create_netquake_codec(d->protocol, &r, &codec); cc.codec = &codec;
          m.kind = "version"; m.version = version; break;
        }
        case 5: m.kind = "set-view"; m.entity = (double)(ji(qa_message_reader_short(&r)) & 65535); break;
        case 6: {
          double mask = qa_message_reader_byte(&r);
          double volume = (ji(mask) & QA_Q1_SND_VOLUME) ? qa_message_reader_byte(&r) : 255;
          double attenuation = (ji(mask) & QA_Q1_SND_ATTENUATION) ? qa_message_reader_byte(&r) / 64 : 1;
          qa_q1_sound_header h; qa_q1_sound_header_init(&h);
          codec.read_sound_header(&codec, mask, &h);
          m.kind = "sound"; m.entity = h.ent; m.channel = h.channel; m.index = h.sound_num;
          m.volume = volume; m.attenuation = attenuation; m.origin = read_vec_coord(coord_read, &cc); break;
        }
        case 7: d->time_seconds = qa_message_reader_float(&r); m.kind = "time"; m.seconds = d->time_seconds; break;
        case 8: m.kind = "print"; m.text = qa_message_reader_string(&r, 2047, false, &dummy); break;
        case 9: m.kind = "stufftext"; m.text = qa_message_reader_string(&r, 2047, false, &dummy); break;
        case 10: m.kind = "set-angle"; m.angles = read_vec_coord(angle_read, &cc); break;
        case 11: {
          double version = qa_message_reader_long(&r);
          double pflags = version == 999 ? (double)ju(qa_message_reader_long(&r)) : 0;
          qa_q1_protocol_identity protocol = qa_netquake_profile(version, pflags);
          d->protocol = protocol;
          qa_create_netquake_codec(protocol, &r, &codec); cc.codec = &codec;
          flags = qa_q1_protocol_flags(protocol); cc.flags = flags; d->flags_value = flags;
          d->baseline_count = 0;
          m.kind = "server-info"; m.protocol = protocol;
          m.max_clients = qa_message_reader_byte(&r); m.game_type = qa_message_reader_byte(&r);
          m.level = qa_message_reader_string(&r, 2047, false, &dummy);
          m.models = read_list(&r, codec.max_precache, &m.model_count);
          m.sounds = read_list(&r, codec.max_precache, &m.sound_count);
          break;
        }
        case 12: m.kind = "light-style"; m.index = qa_message_reader_byte(&r); m.str_value = qa_message_reader_string(&r, 2047, false, &dummy); break;
        case 13: m.kind = "name"; m.slot = qa_message_reader_byte(&r); m.str_value = qa_message_reader_string(&r, 2047, false, &dummy); break;
        case 14: m.kind = "frags"; m.slot = qa_message_reader_byte(&r); m.value = qa_message_reader_short(&r); break;
        case 15: read_client_data(d, &r, &codec, &m); break;
        case 16: {
          uint32_t channel = ju(qa_message_reader_short(&r)) & 65535u;
          m.kind = "stop-sound"; m.entity = channel >> 3; m.channel = channel & 7; break;
        }
        case 17: m.kind = "colors"; m.slot = qa_message_reader_byte(&r); m.value = qa_message_reader_byte(&r); break;
        case 18: {
          m.kind = "particle"; m.origin = read_vec_coord(coord_read, &cc);
          double dx = qa_message_reader_char(&r) / 16, dy = qa_message_reader_char(&r) / 16, dz = qa_message_reader_char(&r) / 16;
          m.direction = vec3_of(dx, dy, dz);
          m.count = qa_message_reader_byte(&r);
          m.color_value = qa_message_reader_byte(&r);
          break;
        }
        case 19: m.kind = "damage"; m.armor = qa_message_reader_byte(&r); m.blood = qa_message_reader_byte(&r); m.source = read_vec_coord(coord_read, &cc); break;
        case 20: case 22: case 42: case 43: {
          bool baseline = ((int)op == 22 || (int)op == 42);
          double num = baseline ? (double)(ji(qa_message_reader_short(&r)) & 65535) : 0;
          qa_q1_wire_entity_state state; qa_q1_wire_entity_state_init(&state);
          codec.read_baseline(&codec, &state, op >= 42 ? 2 : 1, flags);
          if (baseline) baseline_set(d, num, &state);
          m.kind = baseline ? "baseline" : "static";
          m.state = qa_nq_entity_state_from_wire(num, &state);
          break;
        }
        case 23: m.kind = "temporary-entity"; m.effect = qa_read_temporary_entity(&r, coord_read, &cc, false); break;
        case 24: m.kind = "pause"; m.paused = qa_message_reader_byte(&r) != 0; break;
        case 25: m.kind = "signon"; m.stage = qa_message_reader_byte(&r); break;
        case 26: m.kind = "center-print"; m.text = qa_message_reader_string(&r, 2047, false, &dummy); break;
        case 27: m.kind = "killed-monster"; break;
        case 28: m.kind = "found-secret"; break;
        case 29: case 44: {
          qa_vec3 origin = read_vec_coord(coord_read, &cc);
          double index = (double)(ji(codec.read_static_sound_index(&codec, (int)op == 44 ? 2 : 1)) & 65535);
          m.kind = "static-sound"; m.entity = 0; m.channel = 0; m.index = index;
          m.volume = qa_message_reader_byte(&r); m.attenuation = qa_message_reader_byte(&r) / 64; m.origin = origin;
          break;
        }
        case 30: m.kind = "intermission"; break;
        case 31: m.kind = "finale"; m.text = qa_message_reader_string(&r, 2047, false, &dummy); break;
        case 32: m.kind = "cd-track"; m.track = qa_message_reader_byte(&r); m.loop_track = qa_message_reader_byte(&r); break;
        case 33: m.kind = "sell-screen"; break;
        case 34: m.kind = "cutscene"; m.text = qa_message_reader_string(&r, 2047, false, &dummy); break;
        case 37: m.kind = "skybox"; m.text = qa_message_reader_string(&r, 2047, false, &dummy); break;
        case 40: m.kind = "bonus-flash"; break;
        case 41: {
          m.kind = "fog"; m.density = qa_message_reader_byte(&r) / 255;
          double cr = qa_message_reader_byte(&r) / 255, cg = qa_message_reader_byte(&r) / 255, cb = qa_message_reader_byte(&r) / 255;
          m.color = vec3_of(cr, cg, cb);
          m.transition_seconds = qa_message_reader_short(&r) / 100; break;
        }
        case 52: m.kind = "achievement"; m.text = qa_message_reader_string(&r, 2047, false, &dummy); break;
        case 56: {
          double mask = qa_message_reader_byte(&r);
          m.kind = "local-sound";
          m.index = (ji(mask) & QA_Q1_SND_LARGESOUND) ? (double)(ji(qa_message_reader_short(&r)) & 65535) : qa_message_reader_byte(&r);
          break;
        }
        default: read_private(d, &r, op, &m); break;
      }
    }
    qa_message_reader_finish(&r);
    if (count == cap) { cap *= 2; messages = realloc(messages, cap * sizeof *messages); }
    messages[count++] = m;
  }
  *out_count = count; return messages;
}
static qa_q1_user_command q1_cmd(double t, qa_vec3 a, double f, double s, double u, double b, double im) {
  qa_q1_user_command c; c.acknowledged_server_time_seconds = t; c.view_angles = a;
  c.forward_move = f; c.side_move = s; c.up_move = u; c.buttons = b; c.impulse = im; return c;
}
void qa_write_netquake_move(qa_size_buf *sb, const qa_q1_user_command *command,
                            qa_q1_protocol_identity protocol, double flags, bool has_flags) {
  double fl = has_flags ? flags : qa_q1_protocol_flags(protocol);
  qa_message_reader empty; qa_message_reader_init(&empty, NULL, 0);
  qa_protocol_codec codec; qa_create_netquake_codec(protocol, &empty, &codec);
  qa_msg_write_byte(sb, 3);
  qa_msg_write_float(sb, command->acknowledged_server_time_seconds);
  double angs[3] = { command->view_angles.x, command->view_angles.y, command->view_angles.z };
  for (int i = 0; i < 3; i++) {
    if (protocol.version == 15) codec.write_angle(&codec, sb, angs[i], fl);
    else qa_msg_write_angle16(sb, angs[i], (int64_t)fl);
  }
  qa_msg_write_short(sb, command->forward_move);
  qa_msg_write_short(sb, command->side_move);
  qa_msg_write_short(sb, command->up_move);
  qa_msg_write_byte(sb, command->buttons);
  qa_msg_write_byte(sb, command->impulse);
}
void qa_netquake_client_message_destroy(qa_netquake_client_message *m) { free(m->text); memset(m, 0, sizeof *m); }
qa_netquake_client_message *qa_decode_netquake_client(const uint8_t *bytes, size_t length,
                                                      qa_q1_protocol_identity protocol, size_t *out_count) {
  qa_message_reader r; qa_message_reader_init(&r, bytes, length);
  qa_protocol_codec codec; qa_create_netquake_codec(protocol, &r, &codec);
  double flags = qa_q1_protocol_flags(protocol);
  size_t cap = 4, count = 0, dummy;
  qa_netquake_client_message *out = calloc(cap, sizeof *out);
  while (qa_message_reader_remaining(&r) > 0) {
    qa_netquake_client_message m; memset(&m, 0, sizeof m);
    switch ((int)qa_message_reader_byte(&r)) {
      case 1: m.kind = "nop"; break;
      case 2: m.kind = "disconnect"; break;
      case 4: m.kind = "string-command"; m.text = qa_message_reader_string(&r, 2047, false, &dummy); break;
      case 3: {
        m.kind = "move";
        double t = qa_message_reader_float(&r);
        double ax = protocol.version == 15 ? codec.read_angle(&codec, flags) : qa_read_move_angle16(&r, (int64_t)flags);
        double ay = protocol.version == 15 ? codec.read_angle(&codec, flags) : qa_read_move_angle16(&r, (int64_t)flags);
        double az = protocol.version == 15 ? codec.read_angle(&codec, flags) : qa_read_move_angle16(&r, (int64_t)flags);
        m.command = q1_cmd(t, vec3_of(ax, ay, az), qa_message_reader_short(&r), qa_message_reader_short(&r),
                           qa_message_reader_short(&r), qa_message_reader_byte(&r), qa_message_reader_byte(&r));
        break;
      }
      default: qa_q1_packet_error("Unknown client message", r.offset - 1);
    }
    qa_message_reader_finish(&r);
    if (count == cap) { cap *= 2; out = realloc(out, cap * sizeof *out); }
    out[count++] = m;
  }
  *out_count = count; return out;
}
void qa_write_netquake_entity(qa_size_buf *sb, qa_q1_protocol_identity profile,
                              const qa_q1_extended_entity_state *state,
                              const qa_q1_extended_entity_state *baseline, double server_time_seconds) {
  qa_q1_entity_update u; qa_q1_entity_update_init(&u);
  qa_q1_wire_entity_state s; qa_nq_wire_entity_from_extended(state, &s);
  u.origin = s.origin; u.angles = s.angles;
  u.modelindex = s.modelindex; u.frame = s.frame; u.colormap = s.colormap;
  u.skin = s.skin; u.effects = s.effects; u.alpha = s.alpha; u.scale = s.scale;
  u.movetype_step = state->step;
  u.sendinterval = state->lerp_finish_seconds != 0;
  u.lerpfinish = state->lerp_finish_seconds - server_time_seconds;
  qa_nq_wire_entity_from_extended(baseline, &u.baseline);
  qa_message_reader empty; qa_message_reader_init(&empty, NULL, 0);
  qa_protocol_codec codec; qa_create_netquake_codec(profile, &empty, &codec);
  codec.write_entity_update(&codec, sb, state->base.number, &u, qa_q1_protocol_flags(profile));
}
void qa_write_netquake_client_data(qa_size_buf *sb, qa_q1_protocol_identity profile,
                                   const qa_q1_client_data *data, double weapon_alpha, bool standard_quake) {
  qa_q1_clientdata_codec c; qa_q1_clientdata_codec_init(&c);
  c.viewheight = data->view_height; c.idealpitch = data->ideal_pitch;
  qa_wire_vector_set(&c.punchangle,0,data->punch_angles.x); qa_wire_vector_set(&c.punchangle,1,data->punch_angles.y); qa_wire_vector_set(&c.punchangle,2,data->punch_angles.z);
  qa_wire_vector_set(&c.velocity,0,data->velocity.x); qa_wire_vector_set(&c.velocity,1,data->velocity.y); qa_wire_vector_set(&c.velocity,2,data->velocity.z);
  c.items = data->items; c.onground = data->on_ground; c.inwater = data->in_water;
  c.weaponframe = data->weapon_frame; c.armorvalue = data->armor; c.weaponmodelindex = data->weapon_model;
  c.health = data->health; c.currentammo = data->ammo; c.ammo_shells = data->shells;
  c.ammo_nails = data->nails; c.ammo_rockets = data->rockets; c.ammo_cells = data->cells;
  c.weapon = data->active_weapon; c.alpha = weapon_alpha; c.standard_quake = standard_quake;
  qa_message_reader empty; qa_message_reader_init(&empty, NULL, 0);
  qa_protocol_codec codec; qa_create_netquake_codec(profile, &empty, &codec);
  codec.write_clientdata(&codec, sb, &c, qa_q1_protocol_flags(profile));
}
bool qa_write_netquake_sound(qa_size_buf *sb, qa_q1_protocol_identity profile, const qa_netquake_message *m) {
  qa_q1_sound_message s; qa_q1_sound_message_init(&s);
  s.ent = m->entity; s.channel = m->channel; s.sound_num = m->index;
  s.volume = m->volume; s.attenuation = m->attenuation;
  qa_wire_vector_set(&s.origin,0,m->origin.x); qa_wire_vector_set(&s.origin,1,m->origin.y); qa_wire_vector_set(&s.origin,2,m->origin.z);
  qa_message_reader empty; qa_message_reader_init(&empty, NULL, 0);
  qa_protocol_codec codec; qa_create_netquake_codec(profile, &empty, &codec);
  double flags = qa_q1_protocol_flags(profile);
  if (m->kind && strcmp(m->kind, "sound") == 0) return codec.write_sound(&codec, sb, &s, flags);
  return codec.write_static_sound(&codec, sb, &s.origin, s.sound_num, s.volume / 255, s.attenuation, flags);
}
void qa_write_netquake_server_info(qa_size_buf *sb, const qa_netquake_message *m) {
  qa_message_reader empty; qa_message_reader_init(&empty, NULL, 0);
  qa_protocol_codec codec; qa_create_netquake_codec(m->protocol, &empty, &codec);
  qa_msg_write_byte(sb, 11);
  codec.write_protocol(&codec, sb, qa_q1_protocol_flags(m->protocol));
  qa_msg_write_byte(sb, m->max_clients); qa_msg_write_byte(sb, m->game_type);
  qa_msg_write_string(sb, m->level);
  for (size_t i = 0; i < m->model_count; i++) qa_msg_write_string(sb, m->models[i]);
  qa_msg_write_byte(sb, 0);
  for (size_t i = 0; i < m->sound_count; i++) qa_msg_write_string(sb, m->sounds[i]);
  qa_msg_write_byte(sb, 0);
}
void qa_write_netquake_time(qa_size_buf *sb, double seconds) { qa_msg_write_byte(sb, 7); qa_msg_write_float(sb, seconds); }
void qa_write_netquake_stat(qa_size_buf *sb, double index, double value) {
  qa_msg_write_byte(sb, 3); qa_msg_write_byte(sb, index); qa_msg_write_long(sb, value);
}
static void wvec(qa_protocol_codec *codec, qa_size_buf *sb, qa_vec3 v, double flags) {
  codec->write_coord(codec, sb, v.x, flags); codec->write_coord(codec, sb, v.y, flags); codec->write_coord(codec, sb, v.z, flags);
}
static void wtext(qa_size_buf *sb, double op, const char *value) { qa_msg_write_byte(sb, op); qa_msg_write_string(sb, value); }
void qa_write_netquake_message(qa_size_buf *sb, qa_q1_protocol_identity profile, const qa_netquake_message *m,
                               const char *rerelease_messages, bool standard_quake) {
  const char *rr = rerelease_messages ? rerelease_messages : "known-retail";
  qa_message_reader empty; qa_message_reader_init(&empty, NULL, 0);
  qa_protocol_codec codec; qa_create_netquake_codec(profile, &empty, &codec);
  double flags = qa_q1_protocol_flags(profile);
  const char *k = m->kind;
  bool is_private = k && (!strcmp(k,"botchat")||!strcmp(k,"spawned-monster")||!strcmp(k,"set-views")||!strcmp(k,"ping")
    ||!strcmp(k,"social")||!strcmp(k,"player-info")||!strcmp(k,"raw-print")||!strcmp(k,"server-vars")
    ||!strcmp(k,"sequence")||!strcmp(k,"chat")||!strcmp(k,"level-completed")||!strcmp(k,"back-to-lobby")
    ||!strcmp(k,"prompt-begin")||!strcmp(k,"prompt-choice")||!strcmp(k,"prompt-clear"));
  if (is_private && strcmp(rr, "q1-rerelease-private") != 0)
    qa_range_error("Message requires identified donor-private rerelease layout");
  if (!strcmp(k,"nop")) qa_msg_write_byte(sb, 1);
  else if (!strcmp(k,"disconnect")) qa_msg_write_byte(sb, 2);
  else if (!strcmp(k,"stat")) qa_write_netquake_stat(sb, m->index, m->value);
  else if (!strcmp(k,"version")) { qa_msg_write_byte(sb, 4); qa_msg_write_long(sb, m->version); }
  else if (!strcmp(k,"set-view")) { qa_msg_write_byte(sb, 5); qa_msg_write_short(sb, m->entity); }
  else if (!strcmp(k,"sound") || !strcmp(k,"static-sound")) {
    if (!qa_write_netquake_sound(sb, profile, m)) qa_range_error("Sound exceeds selected NetQuake wire");
  }
  else if (!strcmp(k,"time")) qa_write_netquake_time(sb, m->seconds);
  else if (!strcmp(k,"print")) wtext(sb, 8, m->text);
  else if (!strcmp(k,"stufftext")) wtext(sb, 9, m->text);
  else if (!strcmp(k,"set-angle")) {
    qa_msg_write_byte(sb, 10);
    codec.write_angle(&codec, sb, m->angles.x, flags); codec.write_angle(&codec, sb, m->angles.y, flags); codec.write_angle(&codec, sb, m->angles.z, flags);
  }
  else if (!strcmp(k,"server-info")) qa_write_netquake_server_info(sb, m);
  else if (!strcmp(k,"light-style")) { qa_msg_write_byte(sb, 12); qa_msg_write_byte(sb, m->index); qa_msg_write_string(sb, m->str_value); }
  else if (!strcmp(k,"name")) { qa_msg_write_byte(sb, 13); qa_msg_write_byte(sb, m->slot); qa_msg_write_string(sb, m->str_value); }
  else if (!strcmp(k,"frags")) { qa_msg_write_byte(sb, 14); qa_msg_write_byte(sb, m->slot); qa_msg_write_short(sb, m->value); }
  else if (!strcmp(k,"client-data")) qa_write_netquake_client_data(sb, profile, &m->data, m->weapon_alpha, standard_quake);
  else if (!strcmp(k,"stop-sound")) { qa_msg_write_byte(sb, 16); qa_msg_write_short(sb, m->entity * 8 + m->channel); }
  else if (!strcmp(k,"colors")) { qa_msg_write_byte(sb, 17); qa_msg_write_byte(sb, m->slot); qa_msg_write_byte(sb, m->value); }
  else if (!strcmp(k,"particle")) {
    qa_msg_write_byte(sb, 18); wvec(&codec, sb, m->origin, flags);
    qa_msg_write_byte(sb, trunc(m->direction.x * 16)); qa_msg_write_byte(sb, trunc(m->direction.y * 16)); qa_msg_write_byte(sb, trunc(m->direction.z * 16));
    qa_msg_write_byte(sb, m->count); qa_msg_write_byte(sb, m->color_value);
  }
  else if (!strcmp(k,"damage")) { qa_msg_write_byte(sb, 19); qa_msg_write_byte(sb, m->armor); qa_msg_write_byte(sb, m->blood); wvec(&codec, sb, m->source, flags); }
  else if (!strcmp(k,"static")) {
    qa_q1_wire_entity_state w; qa_nq_wire_entity_from_extended(&m->state, &w);
    if (!codec.write_static(&codec, sb, &w, flags)) qa_range_error("Static exceeds selected wire");
  }
  else if (!strcmp(k,"baseline")) {
    qa_q1_wire_entity_state w; qa_nq_wire_entity_from_extended(&m->state, &w);
    codec.write_baseline(&codec, sb, m->state.base.number, &w, flags);
  }
  else if (!strcmp(k,"temporary-entity")) {
    const qa_temporary_entity *e = &m->effect;
    qa_msg_write_byte(sb, 23); qa_msg_write_byte(sb, e->type);
    if (e->kind == QA_TE_BEAM) { qa_msg_write_short(sb, e->entity); wvec(&codec, sb, e->start, flags); wvec(&codec, sb, e->end, flags); }
    else {
      wvec(&codec, sb, e->origin, flags);
      if (e->kind == QA_TE_EXPLOSION_COLORS) { qa_msg_write_byte(sb, e->color_start); qa_msg_write_byte(sb, e->color_length); }
    }
  }
  else if (!strcmp(k,"pause")) { qa_msg_write_byte(sb, 24); qa_msg_write_byte(sb, m->paused ? 1 : 0); }
  else if (!strcmp(k,"signon")) { qa_msg_write_byte(sb, 25); qa_msg_write_byte(sb, m->stage); }
  else if (!strcmp(k,"center-print")) wtext(sb, 26, m->text);
  else if (!strcmp(k,"killed-monster")) qa_msg_write_byte(sb, 27);
  else if (!strcmp(k,"found-secret")) qa_msg_write_byte(sb, 28);
  else if (!strcmp(k,"intermission")) qa_msg_write_byte(sb, 30);
  else if (!strcmp(k,"finale")) wtext(sb, 31, m->text);
  else if (!strcmp(k,"cd-track")) { qa_msg_write_byte(sb, 32); qa_msg_write_byte(sb, m->track); qa_msg_write_byte(sb, m->loop_track); }
  else if (!strcmp(k,"sell-screen")) qa_msg_write_byte(sb, 33);
  else if (!strcmp(k,"cutscene")) wtext(sb, 34, m->text);
  else if (!strcmp(k,"skybox")) wtext(sb, 37, m->text);
  else if (!strcmp(k,"bonus-flash")) qa_msg_write_byte(sb, 40);
  else if (!strcmp(k,"fog")) {
    qa_msg_write_byte(sb, 41); qa_msg_write_byte(sb, m->density * 255);
    qa_msg_write_byte(sb, m->color.x * 255); qa_msg_write_byte(sb, m->color.y * 255); qa_msg_write_byte(sb, m->color.z * 255);
    qa_msg_write_short(sb, m->transition_seconds * 100);
  }
  else if (!strcmp(k,"achievement")) wtext(sb, 52, m->text);
  else if (!strcmp(k,"local-sound")) {
    qa_msg_write_byte(sb, 56);
    qa_msg_write_byte(sb, m->index > 255 ? QA_Q1_SND_LARGESOUND : 0);
    if (m->index > 255) qa_msg_write_short(sb, m->index); else qa_msg_write_byte(sb, m->index);
  }
  else if (!strcmp(k,"botchat")) wtext(sb, 38, m->text);
  else if (!strcmp(k,"spawned-monster")) { qa_msg_write_byte(sb, 39); qa_msg_write_byte(sb, m->value); }
  else if (!strcmp(k,"set-views")) { qa_msg_write_byte(sb, 45); qa_msg_write_byte(sb, m->value); }
  else if (!strcmp(k,"ping")) { qa_msg_write_byte(sb, 46); qa_msg_write_byte(sb, m->slot); qa_msg_write_short(sb, m->value); }
  else if (!strcmp(k,"social") || !strcmp(k,"player-info")) {
    qa_msg_write_byte(sb, !strcmp(k,"social") ? 47 : 48); qa_msg_write_byte(sb, m->slot); qa_msg_write_string(sb, m->str_value);
  }
  else if (!strcmp(k,"raw-print")) wtext(sb, 49, m->text);
  else if (!strcmp(k,"server-vars")) wtext(sb, 50, m->text);
  else if (!strcmp(k,"sequence")) { qa_msg_write_byte(sb, 51); qa_msg_write_long(sb, m->value); }
  else if (!strcmp(k,"chat")) wtext(sb, 53, m->text);
  else if (!strcmp(k,"level-completed")) qa_msg_write_byte(sb, 54);
  else if (!strcmp(k,"back-to-lobby")) qa_msg_write_byte(sb, 55);
  else if (!strcmp(k,"prompt-begin")) { qa_msg_write_byte(sb, 57); qa_msg_write_byte(sb, 0); qa_msg_write_string(sb, m->text); qa_msg_write_byte(sb, m->choices); }
  else if (!strcmp(k,"prompt-choice")) { qa_msg_write_byte(sb, 57); qa_msg_write_byte(sb, 1); qa_msg_write_string(sb, m->text); qa_msg_write_byte(sb, m->impulse); }
  else if (!strcmp(k,"prompt-clear")) { qa_msg_write_byte(sb, 57); qa_msg_write_byte(sb, 2); }
  else qa_range_error("Unreachable message [object Object]");
}
void qa_netquake_move_sender_init(qa_netquake_move_sender *s) { s->messages = 0; }
void qa_netquake_move_sender_reset(qa_netquake_move_sender *s) { s->messages = 0; }
uint8_t *qa_netquake_move_sender_next(qa_netquake_move_sender *s, const qa_q1_user_command *command,
                                      qa_q1_protocol_identity protocol, bool demo_playback, size_t *out_len) {
  if (demo_playback) return NULL;
  if (++s->messages <= 2) return NULL;
  qa_size_buf buffer; qa_size_buf_init(&buffer, 128, false);
  qa_write_netquake_move(&buffer, command, protocol, 0, false);
  uint8_t *out = qa_size_buf_bytes(&buffer, out_len);
  qa_size_buf_destroy(&buffer); return out;
}
