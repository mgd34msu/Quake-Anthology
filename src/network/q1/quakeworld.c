/* Port of src/network/q1/quakeworld.ts. GPL-2.0-or-later. */
#include "qa/network/q1/quakeworld.h"
#include "qa/network/common/js_number.h"
#include "qa/foundation/support.h"
#include "qa/network/q1/codecs/codec.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/js.h"
/* ToInt32 (NaN and infinities are 0; the fmod form cast NaN to uint32_t). */
static int32_t ji(double n) { return qa_js_to_int32(n); }
static uint32_t ju(double n) { return (uint32_t)ji(n); }
static qa_vec3 v3(double x, double y, double z) { qa_vec3 v; v.x = x; v.y = y; v.z = z; return v; }
static double vg(const qa_wire_vector *v, int i) { return qa_wire_vector_get(v, (size_t)i); }
static void vs(qa_wire_vector *v, int i, double x) { qa_wire_vector_set(v, (size_t)i, x); }
void qa_quakeworld_message_destroy(qa_quakeworld_message *m) {
  free(m->text); free(m->str_value); free(m->key); free(m->game_directory); free(m->level_name);
  free(m->download_bytes); free(m->projectiles); free(m->entities);
  for (size_t i = 0; i < m->name_count; i++) free(m->names[i]);
  free(m->names);
  memset(m, 0, sizeof *m);
}
qa_qw_user_command qa_qw_command_from_wire(const qa_qw_usercmd *command) {
  qa_qw_user_command c;
  c.milliseconds = command->msec;
  c.angles = v3(vg(&command->angles,0), vg(&command->angles,1), vg(&command->angles,2));
  c.forward_move = command->forwardmove; c.side_move = command->sidemove; c.up_move = command->upmove;
  c.buttons = command->buttons; c.impulse = command->impulse;
  return c;
}
void qa_qw_wire_command_from_contract(const qa_qw_user_command *command, qa_qw_usercmd *out) {
  qa_qw_usercmd_init(out);
  out->msec = command->milliseconds;
  vs(&out->angles,0,command->angles.x); vs(&out->angles,1,command->angles.y); vs(&out->angles,2,command->angles.z);
  out->forwardmove = command->forward_move; out->sidemove = command->side_move; out->upmove = command->up_move;
  out->buttons = command->buttons; out->impulse = command->impulse;
}
qa_quakeworld_entity qa_qw_entity_from_wire(const qa_qw_entity_state *s) {
  qa_quakeworld_entity o; memset(&o, 0, sizeof o);
  o.quakeworld_flags = s->flags;
  o.state.base.number = s->number;
  o.state.base.origin = v3(vg(&s->origin,0), vg(&s->origin,1), vg(&s->origin,2));
  o.state.base.angles = v3(vg(&s->angles,0), vg(&s->angles,1), vg(&s->angles,2));
  o.state.base.model_index = s->modelindex; o.state.base.frame = s->frame;
  o.state.base.color_map = s->colormap; o.state.base.skin = s->skinnum; o.state.base.effects = s->effects;
  o.state.alpha = s->alpha; o.state.scale = s->scale; o.state.lerp_finish_seconds = 0; o.state.step = false;
  return o;
}
void qa_qw_wire_entity_from_extended(const qa_q1_extended_entity_state *s, bool solid, qa_qw_entity_state *out) {
  qa_qw_entity_state_init(out);
  out->number = s->base.number;
  vs(&out->origin,0,s->base.origin.x); vs(&out->origin,1,s->base.origin.y); vs(&out->origin,2,s->base.origin.z);
  vs(&out->angles,0,s->base.angles.x); vs(&out->angles,1,s->base.angles.y); vs(&out->angles,2,s->base.angles.z);
  out->modelindex = s->base.model_index; out->frame = s->base.frame; out->colormap = s->base.color_map;
  out->skinnum = s->base.skin; out->effects = s->base.effects; out->alpha = s->alpha; out->scale = s->scale;
  out->flags = solid ? QA_QW_U_SOLID : 0;
}
void qa_quakeworld_decoder_init(qa_quakeworld_decoder *d, qa_quakeworld_profile protocol) {
  memset(d, 0, sizeof *d); d->protocol = protocol; d->player_model_index = 0;
}
void qa_quakeworld_decoder_destroy(qa_quakeworld_decoder *d) {
  free(d->baselines);
  for (size_t i = 0; i < d->frame_count; i++) free(d->frames[i].states);
  free(d->frames); free(d->delta_requests); memset(d, 0, sizeof *d);
}
static qa_qw_entity_state *qw_baseline_get(qa_quakeworld_decoder *d, double slot) {
  for (size_t i = 0; i < d->baseline_count; i++) if (d->baselines[i].slot == slot) return &d->baselines[i].state;
  return NULL;
}
static void qw_baseline_set(qa_quakeworld_decoder *d, double slot, const qa_qw_entity_state *s) {
  qa_qw_entity_state *old = qw_baseline_get(d, slot);
  if (old) { *old = *s; return; }
  if (d->baseline_count == d->baseline_cap) { size_t nc = d->baseline_cap ? d->baseline_cap * 2 : 16; d->baselines = realloc(d->baselines, nc * sizeof *d->baselines); d->baseline_cap = nc; }
  d->baselines[d->baseline_count].slot = slot; d->baselines[d->baseline_count].state = *s; d->baseline_count++;
}
static qa_qw_frame_entry *frame_get(qa_quakeworld_decoder *d, double seq) {
  for (size_t i = 0; i < d->frame_count; i++) if (d->frames[i].sequence == seq) return &d->frames[i];
  return NULL;
}
static void frame_set(qa_quakeworld_decoder *d, double seq, qa_qw_entity_state *states, size_t count) {
  qa_qw_frame_entry *old = frame_get(d, seq);
  if (old) { free(old->states); old->states = states; old->count = count; return; }
  if (d->frame_count == d->frame_cap) { size_t nc = d->frame_cap ? d->frame_cap * 2 : 8; d->frames = realloc(d->frames, nc * sizeof *d->frames); d->frame_cap = nc; }
  d->frames[d->frame_count].sequence = seq; d->frames[d->frame_count].states = states; d->frames[d->frame_count].count = count; d->frame_count++;
}
static qa_qw_delta_entry *delta_get(qa_quakeworld_decoder *d, double seq) {
  for (size_t i = 0; i < d->delta_count; i++) if (d->delta_requests[i].sequence == seq) return &d->delta_requests[i];
  return NULL;
}
static void delta_set(qa_quakeworld_decoder *d, double seq, bool has_base, double base) {
  qa_qw_delta_entry *old = delta_get(d, seq);
  if (old) { old->has_base = has_base; old->base = base; return; }
  if (d->delta_count == d->delta_cap) { size_t nc = d->delta_cap ? d->delta_cap * 2 : 8; d->delta_requests = realloc(d->delta_requests, nc * sizeof *d->delta_requests); d->delta_cap = nc; }
  d->delta_requests[d->delta_count].sequence = seq; d->delta_requests[d->delta_count].has_base = has_base; d->delta_requests[d->delta_count].base = base; d->delta_count++;
}
void qa_quakeworld_decoder_record_delta_request(qa_quakeworld_decoder *d, double command_sequence, bool has_base, double base_sequence) {
  delta_set(d, command_sequence, has_base, base_sequence);
  size_t w = 0;
  for (size_t i = 0; i < d->delta_count; i++)
    if (!(d->delta_requests[i].sequence <= command_sequence - 64)) d->delta_requests[w++] = d->delta_requests[i];
  d->delta_count = w;
}
void qa_quakeworld_decoder_capture(const qa_quakeworld_decoder *d, qa_qw_decoder_capture *out) {
  memset(out, 0, sizeof *out);
  out->version = (double)d->protocol.version; out->flags = qa_q1_protocol_flags(d->protocol);
  out->player_model_index = d->player_model_index;
  out->baseline_count = d->baseline_count;
  out->baselines = malloc((out->baseline_count ? out->baseline_count : 1) * sizeof *out->baselines);
  for (size_t i = 0; i < out->baseline_count; i++) {
    out->baselines[i].slot = d->baselines[i].slot;
    out->baselines[i].state = qa_capture_qw_wire_entity(&d->baselines[i].state);
  }
  out->frame_count = d->frame_count;
  out->frames = calloc(out->frame_count ? out->frame_count : 1, sizeof *out->frames);
  for (size_t i = 0; i < out->frame_count; i++) {
    out->frames[i].sequence = d->frames[i].sequence; out->frames[i].count = d->frames[i].count;
    out->frames[i].states = malloc((d->frames[i].count ? d->frames[i].count : 1) * sizeof *out->frames[i].states);
    for (size_t j = 0; j < d->frames[i].count; j++) out->frames[i].states[j] = qa_capture_qw_wire_entity(&d->frames[i].states[j]);
  }
  out->delta_count = d->delta_count;
  out->delta_requests = malloc((out->delta_count ? out->delta_count : 1) * sizeof *out->delta_requests);
  for (size_t i = 0; i < out->delta_count; i++) {
    out->delta_requests[i].sequence = d->delta_requests[i].sequence;
    out->delta_requests[i].has_base = d->delta_requests[i].has_base;
    out->delta_requests[i].base = d->delta_requests[i].base;
  }
}
void qa_qw_decoder_capture_destroy(qa_qw_decoder_capture *c) {
  free(c->baselines);
  for (size_t i = 0; i < c->frame_count; i++) free(c->frames[i].states);
  free(c->frames); free(c->delta_requests); memset(c, 0, sizeof *c);
}
void qa_quakeworld_decoder_restore(qa_quakeworld_decoder *d, const qa_save_reader *reader) {
  qa_save_reader fv, ff, fp, fb, fr, fd;
  (void)qa_save_reader_field(reader, "version", &fv);
  double version = qa_save_reader_integer_min(&fv, 0); qa_save_reader_destroy(&fv);
  (void)qa_save_reader_field(reader, "flags", &ff);
  double flags = qa_save_reader_integer_min(&ff, 0); qa_save_reader_destroy(&ff);
  d->protocol = qa_quakeworld_profile_make(version, flags);
  (void)qa_save_reader_field(reader, "playerModelIndex", &fp);
  d->player_model_index = qa_save_reader_integer_min(&fp, 0); qa_save_reader_destroy(&fp);
  d->baseline_count = 0;
  for (size_t i = 0; i < d->frame_count; i++) free(d->frames[i].states);
  d->frame_count = 0; d->delta_count = 0;
  (void)qa_save_reader_field(reader, "baselines", &fb);
  size_t nb = qa_save_reader_list_count(&fb);
  for (size_t i = 0; i < nb; i++) {
    qa_save_reader item, fs, fst;
    (void)qa_save_reader_item(&fb, i, &item);
    (void)qa_save_reader_field(&item, "slot", &fs);
    double slot = qa_save_reader_integer_min(&fs, 0); qa_save_reader_destroy(&fs);
    (void)qa_save_reader_field(&item, "state", &fst);
    qa_qw_entity_state st; qa_read_qw_wire_entity(&fst, &st); qa_save_reader_destroy(&fst);
    if (qw_baseline_get(d, slot)) qa_save_reader_fail(reader, "duplicate baseline");
    qw_baseline_set(d, slot, &st);
    qa_save_reader_destroy(&item);
  }
  qa_save_reader_destroy(&fb);
  (void)qa_save_reader_field(reader, "frames", &fr);
  size_t nf = qa_save_reader_list_count(&fr);
  for (size_t i = 0; i < nf; i++) {
    qa_save_reader item, fs, fst;
    (void)qa_save_reader_item(&fr, i, &item);
    (void)qa_save_reader_field(&item, "sequence", &fs);
    double seq = qa_save_reader_integer(&fs); qa_save_reader_destroy(&fs);
    (void)qa_save_reader_field(&item, "states", &fst);
    size_t ns = qa_save_reader_list_count(&fst);
    qa_qw_entity_state *states = malloc((ns ? ns : 1) * sizeof *states);
    for (size_t j = 0; j < ns; j++) {
      qa_save_reader si; (void)qa_save_reader_item(&fst, j, &si);
      qa_read_qw_wire_entity(&si, &states[j]); qa_save_reader_destroy(&si);
    }
    qa_save_reader_destroy(&fst);
    if (frame_get(d, seq)) qa_save_reader_fail(reader, "duplicate frame");
    frame_set(d, seq, states, ns);
    qa_save_reader_destroy(&item);
  }
  qa_save_reader_destroy(&fr);
  (void)qa_save_reader_field(reader, "deltaRequests", &fd);
  size_t nd = qa_save_reader_list_count(&fd);
  for (size_t i = 0; i < nd; i++) {
    qa_save_reader item, fs, fbase;
    (void)qa_save_reader_item(&fd, i, &item);
    (void)qa_save_reader_field(&item, "sequence", &fs);
    double seq = qa_save_reader_integer(&fs); qa_save_reader_destroy(&fs);
    (void)qa_save_reader_field(&item, "base", &fbase);
    bool has_base = !qa_save_reader_is_null(&fbase);
    double base = has_base ? qa_save_reader_integer(&fbase) : 0;
    qa_save_reader_destroy(&fbase);
    if (delta_get(d, seq)) qa_save_reader_fail(reader, "duplicate delta request");
    delta_set(d, seq, has_base, base);
    qa_save_reader_destroy(&item);
  }
  qa_save_reader_destroy(&fd);
}
struct qw_coord_ctx { qa_protocol_codec *codec; double flags; };
static double qw_coord_read(void *ctx) { struct qw_coord_ctx *c = ctx; return c->codec->read_coord(c->codec, c->flags); }
static double qw_angle_read(void *ctx) { struct qw_coord_ctx *c = ctx; return c->codec->read_angle(c->codec, c->flags); }
static qa_vec3 qw_vec(qa_coord_fn fn, void *ctx) {
  /* TS object literal order: x, y, z. */
  double x = fn(ctx), y = fn(ctx), z = fn(ctx);
  return v3(x, y, z);
}
/* JS `a << n` on an Int32 operand, without C's undefined left shift of negatives. */
static int32_t jshl(int32_t a, int n) { return (int32_t)((uint32_t)a << n); }
static qa_qw_player_state read_player(qa_quakeworld_decoder *d, qa_message_reader *r, qa_protocol_codec *codec, double flags) {
  double number = qa_message_reader_byte(r);
  if (number >= 32) qa_q1_packet_error("Player slot out of range", r->offset - 1);
  int32_t bits = ji(qa_message_reader_short(r)) & 65535;
  double ox = codec->read_coord(codec, flags), oy = codec->read_coord(codec, flags), oz = codec->read_coord(codec, flags);
  qa_vec3 origin = v3(ox, oy, oz);
  double frame = qa_message_reader_byte(r);
  double milliseconds = (bits & QA_QW_PF_MSEC) ? qa_message_reader_byte(r) : 0;
  qa_qw_usercmd cmd; qa_qw_usercmd_init(&cmd);
  if (bits & QA_QW_PF_COMMAND) {
    qa_qw_usercmd empty; qa_qw_usercmd_init(&empty);
    codec->read_delta_usercmd(codec, &empty, &cmd);
  }
  double vx = (bits & QA_QW_PF_VELOCITY1) ? qa_message_reader_short(r) : 0;
  double vy = (bits & QA_QW_PF_VELOCITY2) ? qa_message_reader_short(r) : 0;
  double vz = (bits & QA_QW_PF_VELOCITY3) ? qa_message_reader_short(r) : 0;
  qa_vec3 velocity = v3(vx, vy, vz);
  qa_qw_player_state out; memset(&out, 0, sizeof out);
  out.number = number; out.flags = bits; out.origin = origin; out.frame = frame;
  out.milliseconds = milliseconds; out.command = qa_qw_command_from_wire(&cmd); out.velocity = velocity;
  out.model_index = (bits & QA_QW_PF_MODEL) ? codec->read_model_index(codec) : d->player_model_index;
  out.skin = (bits & QA_QW_PF_SKINNUM) ? qa_message_reader_byte(r) : 0;
  out.effects = (bits & QA_QW_PF_EFFECTS) ? qa_message_reader_byte(r) : 0;
  out.weapon_frame = (bits & QA_QW_PF_WEAPONFRAME) ? qa_message_reader_byte(r) : 0;
  return out;
}
static int cmp_entity_number(const void *a, const void *b) {
  double x = ((const qa_qw_entity_state *)a)->number, y = ((const qa_qw_entity_state *)b)->number;
  return x < y ? -1 : x > y ? 1 : 0;
}
static void free_states_slot(void *arg) { free(*(qa_qw_entity_state **)arg); }
static void read_entities(qa_quakeworld_decoder *d, qa_message_reader *r, qa_protocol_codec *codec, double flags,
                          double sequence, bool delta, qa_quakeworld_message *m) {
  bool has_requested = delta;
  double requested = delta ? qa_message_reader_byte(r) : 0;
  bool has_base = false; double base_sequence = 0;
  if (has_requested && delta_get(d, sequence)) {
    qa_qw_delta_entry *sel = delta_get(d, sequence);
    if (sel->has_base && frame_get(d, sel->base) && sequence - sel->base < 63) { has_base = true; base_sequence = sel->base; }
  } else if (has_requested) {
    for (size_t i = 0; i < d->frame_count; i++) {
      double previous = d->frames[i].sequence;
      if (((int64_t)previous & 255) == (int64_t)requested && previous < sequence && (!has_base || previous > base_sequence)) {
        has_base = true; base_sequence = previous;
      }
    }
  }
  /* states map keyed by number: dynamic array, linear lookup. */
  size_t scap = 16, scount = 0;
  qa_qw_entity_state *states = malloc(scap * sizeof *states);
  qa_cleanup states_guard;
  qa_cleanup_push(&states_guard, free_states_slot, &states);
  if (has_base) {
    qa_qw_frame_entry *fr = frame_get(d, base_sequence);
    if (fr) for (size_t i = 0; i < fr->count; i++) {
      if (scount == scap) { scap *= 2; states = realloc(states, scap * sizeof *states); }
      states[scount++] = fr->states[i];
    }
  }
  bool invalid = delta && !has_base;
  double previous_number = 0;
  for (;;) {
    double wordd = (double)(ji(qa_message_reader_short(r)) & 65535);
    qa_message_reader_finish(r);
    if (wordd == 0) break;
    qa_qw_entity_word h; qa_qw_entity_word_init(&h);
    codec->read_delta_entity_header(codec, wordd, &h);
    if (h.number <= previous_number) qa_q1_packet_error("Unsorted packet entities", r->offset);
    previous_number = h.number;
    if (h.remove) {
      if (!delta) qa_q1_packet_error("Removal in full packet entities", r->offset);
      for (size_t i = 0; i < scount; i++) if (states[i].number == h.number) {
        memmove(&states[i], &states[i+1], (scount - i - 1) * sizeof *states); scount--; break;
      }
      continue;
    }
    qa_qw_entity_state state; qa_qw_entity_state_init(&state);
    qa_qw_entity_state *base = NULL;
    for (size_t i = 0; i < scount; i++) if (states[i].number == h.number) base = &states[i];
    qa_qw_entity_state empty; qa_qw_entity_state_init(&empty);
    if (!base) base = qw_baseline_get(d, h.number);
    if (!base) base = &empty;
    qa_qw_entity_state base_copy = *base;
    codec->read_delta_entity(codec, &base_copy, &state, &h, flags);
    bool replaced = false;
    for (size_t i = 0; i < scount; i++) if (states[i].number == h.number) { states[i] = state; replaced = true; break; }
    if (!replaced) {
      if (scount == scap) { scap *= 2; states = realloc(states, scap * sizeof *states); }
      states[scount++] = state;
    }
    qa_message_reader_finish(r);
  }
  /* Checked once merged: a delta can add entities before the removals later in the same message. */
  if ((double)scount > codec->max_packet_entities) qa_q1_packet_error("Packet entities overflow", r->offset);
  qa_cleanup_pop(&states_guard);
  if (invalid) {
    free(states);
    m->kind = "invalid-delta"; m->sequence = sequence; m->requested = has_requested ? requested : 0;
    return;
  }
  qa_js_sort(states, scount, sizeof *states, cmp_entity_number);
  qa_qw_entity_state *kept = malloc((scount ? scount : 1) * sizeof *kept);
  memcpy(kept, states, scount * sizeof *kept);
  frame_set(d, sequence, kept, scount);
  size_t w = 0;
  for (size_t i = 0; i < d->frame_count; i++) {
    if (d->frames[i].sequence <= sequence - 64) free(d->frames[i].states);
    else d->frames[w++] = d->frames[i];
  }
  d->frame_count = w;
  m->kind = "packet-entities"; m->sequence = sequence;
  m->has_delta_sequence = has_base; m->delta_sequence = base_sequence;
  m->entity_count = scount;
  m->entities = malloc((scount ? scount : 1) * sizeof *m->entities);
  for (size_t i = 0; i < scount; i++) m->entities[i] = qa_qw_entity_from_wire(&states[i]);
  free(states);
}
typedef struct decode_scope {
  qa_quakeworld_message **messages; size_t *count; qa_quakeworld_message *current;
} decode_scope;
/* A caught PacketError abandons the decode; the partial list is garbage, as in TS. */
static void decode_scope_free(void *arg) {
  decode_scope *scope = arg;
  for (size_t i = 0; i < *scope->count; i++) qa_quakeworld_message_destroy(&(*scope->messages)[i]);
  free(*scope->messages);
  qa_quakeworld_message_destroy(scope->current);
}
qa_quakeworld_message *qa_quakeworld_decoder_decode(qa_quakeworld_decoder *d, const uint8_t *bytes, size_t length,
                                                    double sequence, size_t *out_count) {
  qa_message_reader r; qa_message_reader_init(&r, bytes, length);
  qa_protocol_codec codec; qa_create_quakeworld_codec(d->protocol, &r, &codec);
  double flags = qa_q1_protocol_flags(d->protocol);
  struct qw_coord_ctx cc = { &codec, flags };
  size_t cap = 8, count = 0, dummy;
  qa_quakeworld_message *messages = calloc(cap, sizeof *messages);
  qa_quakeworld_message m;
  memset(&m, 0, sizeof m);
  decode_scope scope = { &messages, &count, &m };
  qa_cleanup guard;
  qa_cleanup_push(&guard, decode_scope_free, &scope);
  while (qa_message_reader_remaining(&r) > 0) {
    double opd = qa_message_reader_byte(&r); int op = (int)opd;
    memset(&m, 0, sizeof m);
    cc.codec = &codec; cc.flags = flags;
    switch (op) {
      case 1: m.kind = "nop"; break;
      case 2: m.kind = "disconnect"; break;
      case 3: case 38:
        m.kind = "stat"; m.index = qa_message_reader_byte(&r);
        m.value = op == 3 ? qa_message_reader_byte(&r) : qa_message_reader_long(&r); break;
      case 5: m.kind = "set-view"; m.entity = (double)(ji(qa_message_reader_short(&r)) & 65535); break;
      case 6: {
        uint32_t word = ju(qa_message_reader_short(&r)) & 65535u;
        double volume = (word & QA_QW_SND_VOLUME) ? qa_message_reader_byte(&r) : 255;
        double attenuation = (word & QA_QW_SND_ATTENUATION) ? qa_message_reader_byte(&r) / 64 : 1;
        m.kind = "sound"; m.entity = (word >> 3) & 1023; m.channel = word & 7;
        m.index = codec.read_sound_index(&codec); m.origin = qw_vec(qw_coord_read, &cc);
        m.volume = volume; m.attenuation = attenuation; break;
      }
      case 8: m.kind = "print"; m.level = qa_message_reader_byte(&r); m.text = qa_message_reader_string(&r, 2047, false, &dummy); break;
      case 9: m.kind = "stufftext"; m.text = qa_message_reader_string(&r, 2047, false, &dummy); break;
      case 10: m.kind = "set-angle"; m.angles = qw_vec(qw_angle_read, &cc); break;
      case 11: {
        double version = qa_message_reader_long(&r);
        double pflags = version == 29 ? (double)ju(qa_message_reader_long(&r)) : 0;
        qa_quakeworld_profile profile = qa_quakeworld_profile_make(version, pflags);
        d->protocol = profile;
        qa_create_quakeworld_codec(profile, &r, &codec); cc.codec = &codec;
        flags = qa_q1_protocol_flags(profile); cc.flags = flags;
        d->baseline_count = 0;
        for (size_t i = 0; i < d->frame_count; i++) free(d->frames[i].states);
        d->frame_count = 0; d->delta_count = 0;
        m.kind = "server-data"; m.protocol = profile;
        m.server_count = qa_message_reader_long(&r);
        m.game_directory = qa_message_reader_string(&r, 2047, false, &dummy);
        double slot = qa_message_reader_byte(&r);
        m.level_name = qa_message_reader_string(&r, 2047, false, &dummy);
        qa_qw_move_variables *mv = &m.move_variables;
        mv->gravity = qa_message_reader_float(&r); mv->stop_speed = qa_message_reader_float(&r);
        mv->max_speed = qa_message_reader_float(&r); mv->spectator_max_speed = qa_message_reader_float(&r);
        mv->accelerate = qa_message_reader_float(&r); mv->air_accelerate = qa_message_reader_float(&r);
        mv->water_accelerate = qa_message_reader_float(&r); mv->friction = qa_message_reader_float(&r);
        mv->water_friction = qa_message_reader_float(&r); mv->entity_gravity = qa_message_reader_float(&r);
        m.player_slot = ji(slot) & 127; m.spectator = (ji(slot) & 128) != 0;
        break;
      }
      case 12: m.kind = "light-style"; m.index = qa_message_reader_byte(&r); m.str_value = qa_message_reader_string(&r, 2047, false, &dummy); break;
      case 14: case 36: case 37: case 53:
        m.kind = op == 14 ? "frags" : op == 36 ? "ping" : op == 37 ? "enter-time" : "packet-loss";
        m.slot = qa_message_reader_byte(&r);
        m.value = op == 37 ? qa_message_reader_float(&r) : op == 53 ? qa_message_reader_byte(&r) : qa_message_reader_short(&r);
        break;
      case 16: {
        uint32_t word = ju(qa_message_reader_short(&r)) & 65535u;
        m.kind = "stop-sound"; m.entity = word >> 3; m.channel = word & 7; break;
      }
      case 19: m.kind = "damage"; m.armor = qa_message_reader_byte(&r); m.blood = qa_message_reader_byte(&r); m.source = qw_vec(qw_coord_read, &cc); break;
      case 20: case 22: {
        qa_qw_entity_state s; qa_qw_entity_state_init(&s);
        s.number = op == 22 ? (double)(ji(qa_message_reader_short(&r)) & 65535) : 0;
        codec.read_qw_baseline(&codec, &s, flags);
        if (op == 22) qw_baseline_set(d, s.number, &s);
        m.kind = op == 22 ? "baseline" : "static";
        m.state = qa_qw_entity_from_wire(&s).state;
        break;
      }
      case 23: m.kind = "temporary-entity"; m.effect = qa_read_temporary_entity(&r, qw_coord_read, &cc, true); break;
      case 24: m.kind = "pause"; m.paused = qa_message_reader_byte(&r) != 0; break;
      case 26: m.kind = "center-print"; m.text = qa_message_reader_string(&r, 2047, false, &dummy); break;
      case 27: m.kind = "killed-monster"; break;
      case 28: m.kind = "found-secret"; break;
      case 29:
        m.kind = "static-sound"; m.entity = 0; m.channel = 0; m.origin = qw_vec(qw_coord_read, &cc);
        m.index = codec.read_sound_index(&codec);
        m.volume = qa_message_reader_byte(&r); m.attenuation = qa_message_reader_byte(&r) / 64; break;
      case 30: m.kind = "intermission"; m.origin = qw_vec(qw_coord_read, &cc); m.angles = qw_vec(qw_angle_read, &cc); break;
      case 31: m.kind = "finale"; m.text = qa_message_reader_string(&r, 2047, false, &dummy); break;
      case 32: m.kind = "cd-track"; m.track = qa_message_reader_byte(&r); break;
      case 33: m.kind = "sell-screen"; break;
      case 34: case 35: m.kind = "kick"; m.degrees = op == 34 ? -2 : -4; break;
      case 39: m.kind = "muzzle-flash"; m.entity = (double)(ji(qa_message_reader_short(&r)) & 65535); break;
      case 40: m.kind = "userinfo"; m.slot = qa_message_reader_byte(&r); m.user_id = qa_message_reader_long(&r); m.str_value = qa_message_reader_string(&r, 2047, false, &dummy); break;
      case 41: {
        double size = qa_message_reader_short(&r), percent = qa_message_reader_byte(&r);
        m.kind = "download";
        if (size == -1) m.download_missing = true;
        else { m.download_missing = false; m.download_percent = percent; m.download_bytes = qa_message_reader_bytes(&r, (long long)size, &m.download_length); }
        break;
      }
      case 42: m.kind = "player"; m.player = read_player(d, &r, &codec, flags); break;
      case 43: {
        double cnt = qa_message_reader_byte(&r);
        m.kind = "nails"; m.projectile_count = cnt > 0 ? (size_t)cnt : 0;
        m.projectiles = malloc((m.projectile_count ? m.projectile_count : 1) * sizeof *m.projectiles);
        for (size_t i = 0; i < m.projectile_count; i++) {
          int32_t b0 = ji(qa_message_reader_byte(&r)), b1 = ji(qa_message_reader_byte(&r)), b2 = ji(qa_message_reader_byte(&r));
          int32_t b3 = ji(qa_message_reader_byte(&r)), b4 = ji(qa_message_reader_byte(&r)), b5 = ji(qa_message_reader_byte(&r));
          /* Truncated reads yield -1, which JS shifts as Int32. */
          m.projectiles[i].origin = v3((double)jshl(b0 | ((b1 & 15) << 8), 1) - 4096,
                                       (double)jshl((int32_t)((uint32_t)b1 >> 4) | jshl(b2, 4), 1) - 4096,
                                       (double)jshl(b3 | ((b4 & 15) << 8), 1) - 4096);
          m.projectiles[i].pitch = trunc(360.0 * ((uint32_t)b4 >> 4) / 16);
          m.projectiles[i].yaw = trunc(360.0 * b5 / 256);
        }
        break;
      }
      case 44: m.kind = "choke-count"; m.count = qa_message_reader_byte(&r); break;
      case 45: case 46: {
        double first = codec.read_precache_count(&codec);
        /* Names accumulate in m so a raise mid-list frees them through the scope. */
        size_t ncap = 8;
        m.names = malloc(ncap * sizeof *m.names);
        m.name_count = 0;
        for (;;) {
          size_t ncount = m.name_count;
          if (ncount == ncap) { ncap *= 2; m.names = realloc(m.names, ncap * sizeof *m.names); }
          char *name = qa_message_reader_string(&r, 2047, false, &dummy);
          m.names[m.name_count++] = name;
          qa_message_reader_finish(&r);
          if (name[0] == '\0') { free(name); m.name_count--; break; }
          if (first + (double)ncount + 1 >= codec.max_precache) qa_q1_packet_error("QW precache overflow", r.offset);
          if (op == 45 && strcmp(name, "progs/player.mdl") == 0) d->player_model_index = first + (double)m.name_count;
        }
        m.kind = op == 45 ? "model-list" : "sound-list";
        m.first = first;
        m.next = codec.read_precache_count(&codec);
        break;
      }
      case 47: case 48: read_entities(d, &r, &codec, flags, sequence, op == 48, &m); break;
      case 49: case 50:
        m.kind = op == 49 ? "max-speed" : "entity-gravity"; m.value = qa_message_reader_float(&r); break;
      case 51:
        m.kind = "set-info"; m.slot = qa_message_reader_byte(&r);
        m.key = qa_message_reader_string(&r, 2047, false, &dummy);
        m.str_value = qa_message_reader_string(&r, 2047, false, &dummy); break;
      case 52:
        m.kind = "server-info";
        m.key = qa_message_reader_string(&r, 2047, false, &dummy);
        m.str_value = qa_message_reader_string(&r, 2047, false, &dummy); break;
      default: {
        char msg[64]; snprintf(msg, sizeof msg, "Unknown QuakeWorld service %d", op);
        qa_q1_packet_error(msg, r.offset - 1);
      }
    }
    qa_message_reader_finish(&r);
    if (count == cap) { cap *= 2; messages = realloc(messages, cap * sizeof *messages); }
    messages[count++] = m;
    memset(&m, 0, sizeof m);
  }
  qa_cleanup_pop(&guard);
  *out_count = count; return messages;
}
static int cmp_double(const void *a, const void *b) {
  double x = *(const double *)a, y = *(const double *)b;
  return x < y ? -1 : x > y ? 1 : 0;
}
void qa_write_quakeworld_entities(qa_size_buf *sb, qa_quakeworld_profile profile,
                                  const qa_qw_entity_state *states, size_t count,
                                  const qa_qw_baseline_map_entry *baseline, size_t baseline_count,
                                  const qa_qw_previous_frame *previous) {
  qa_message_reader empty; qa_message_reader_init(&empty, NULL, 0);
  qa_protocol_codec codec; qa_create_quakeworld_codec(profile, &empty, &codec);
  double flags = qa_q1_protocol_flags(profile);
  if ((double)count > codec.max_packet_entities) qa_range_error("Too many QuakeWorld packet entities");
  qa_msg_write_byte(sb, previous ? 48 : 47);
  if (previous) qa_msg_write_byte(sb, ji(previous->sequence) & 255);
  size_t old_count = previous ? previous->count : 0;
  size_t ncap = count + old_count + 1, ncount = 0;
  double *numbers = malloc(ncap * sizeof *numbers);
  for (size_t i = 0; i < old_count; i++) numbers[ncount++] = previous->states[i].number;
  for (size_t i = 0; i < count; i++) {
    bool seen = false;
    for (size_t j = 0; j < ncount; j++) if (numbers[j] == states[i].number) seen = true;
    if (!seen) numbers[ncount++] = states[i].number;
  }
  qa_js_sort(numbers, ncount, sizeof *numbers, cmp_double);
  for (size_t ni = 0; ni < ncount; ni++) {
    double number = numbers[ni];
    const qa_qw_entity_state *state = NULL, *prior = NULL;
    for (size_t i = 0; i < count; i++) if (states[i].number == number) state = &states[i];
    for (size_t i = 0; i < old_count; i++) if (previous->states[i].number == number) prior = &previous->states[i];
    if (!state) { codec.write_remove_entity(&codec, sb, number); continue; }
    const qa_qw_entity_state *from = prior;
    qa_qw_entity_state empty_state; qa_qw_entity_state_init(&empty_state);
    if (!from) {
      for (size_t i = 0; i < baseline_count; i++) if (baseline[i].number == number) from = baseline[i].state;
    }
    if (!from) from = &empty_state;
    if (!codec.write_delta_entity(&codec, sb, from, state, prior == NULL, flags)) {
      free(numbers);
      qa_range_error("Entity %s exceeds QuakeWorld profile", QA_JS_NUMBER(number));
    }
  }
  free(numbers);
  codec.write_packet_entities_end(&codec, sb);
}
void qa_write_quakeworld_player(qa_size_buf *sb, qa_quakeworld_profile profile, const qa_qw_player_state *state) {
  qa_message_reader empty; qa_message_reader_init(&empty, NULL, 0);
  qa_protocol_codec c; qa_create_quakeworld_codec(profile, &empty, &c);
  double flags = qa_q1_protocol_flags(profile);
  int32_t bits = ji(state->flags);
  qa_msg_write_byte(sb, 42); qa_msg_write_byte(sb, state->number); qa_msg_write_short(sb, bits);
  c.write_coord(&c, sb, state->origin.x, flags); c.write_coord(&c, sb, state->origin.y, flags); c.write_coord(&c, sb, state->origin.z, flags);
  qa_msg_write_byte(sb, state->frame);
  if (bits & QA_QW_PF_MSEC) qa_msg_write_byte(sb, state->milliseconds);
  if (bits & QA_QW_PF_COMMAND) {
    qa_qw_usercmd empty_cmd; qa_qw_usercmd_init(&empty_cmd);
    qa_qw_usercmd wire; qa_qw_wire_command_from_contract(&state->command, &wire);
    c.write_delta_usercmd(&c, sb, &empty_cmd, &wire);
  }
  if (bits & QA_QW_PF_VELOCITY1) qa_msg_write_short(sb, state->velocity.x);
  if (bits & QA_QW_PF_VELOCITY2) qa_msg_write_short(sb, state->velocity.y);
  if (bits & QA_QW_PF_VELOCITY3) qa_msg_write_short(sb, state->velocity.z);
  if (bits & QA_QW_PF_MODEL) c.write_model_index(&c, sb, state->model_index);
  if (bits & QA_QW_PF_SKINNUM) qa_msg_write_byte(sb, state->skin);
  if (bits & QA_QW_PF_EFFECTS) qa_msg_write_byte(sb, state->effects);
  if (bits & QA_QW_PF_WEAPONFRAME) qa_msg_write_byte(sb, state->weapon_frame);
}
void qa_write_quakeworld_server_data(qa_size_buf *sb, const qa_quakeworld_message *m) {
  qa_message_reader empty; qa_message_reader_init(&empty, NULL, 0);
  qa_protocol_codec c; qa_create_quakeworld_codec(m->protocol, &empty, &c);
  qa_msg_write_byte(sb, 11);
  c.write_protocol(&c, sb, qa_q1_protocol_flags(m->protocol));
  qa_msg_write_long(sb, m->server_count);
  qa_msg_write_string(sb, m->game_directory);
  qa_msg_write_byte(sb, ji(m->player_slot) | (m->spectator ? 128 : 0));
  qa_msg_write_string(sb, m->level_name);
  const qa_qw_move_variables *v = &m->move_variables;
  double vals[10] = { v->gravity, v->stop_speed, v->max_speed, v->spectator_max_speed, v->accelerate,
                      v->air_accelerate, v->water_accelerate, v->friction, v->water_friction, v->entity_gravity };
  for (int i = 0; i < 10; i++) qa_msg_write_float(sb, vals[i]);
}
void qa_write_quakeworld_download(qa_size_buf *sb, bool missing, double percent, const uint8_t *bytes, size_t length) {
  qa_msg_write_byte(sb, 41);
  if (missing) { qa_msg_write_short(sb, -1); qa_msg_write_byte(sb, 0); }
  else {
    if (length > 768) qa_range_error("QuakeWorld download block exceeds source block size");
    qa_msg_write_short(sb, (double)length); qa_msg_write_byte(sb, percent);
    qa_sz_write(sb, bytes, (long long)length, (long long)length);
  }
}
static void qw_coord_vec(qa_protocol_codec *c, qa_size_buf *sb, qa_vec3 v, double flags) {
  c->write_coord(c, sb, v.x, flags); c->write_coord(c, sb, v.y, flags); c->write_coord(c, sb, v.z, flags);
}
static void qw_angle_vec(qa_protocol_codec *c, qa_size_buf *sb, qa_vec3 v, double flags) {
  c->write_angle(c, sb, v.x, flags); c->write_angle(c, sb, v.y, flags); c->write_angle(c, sb, v.z, flags);
}
void qa_write_quakeworld_message(qa_size_buf *sb, qa_quakeworld_profile profile, const qa_quakeworld_message *m) {
  qa_message_reader empty; qa_message_reader_init(&empty, NULL, 0);
  qa_protocol_codec c; qa_create_quakeworld_codec(profile, &empty, &c);
  double flags = qa_q1_protocol_flags(profile);
  const char *k = m->kind;
  if (!strcmp(k,"nop")) qa_msg_write_byte(sb, 1);
  else if (!strcmp(k,"disconnect")) qa_msg_write_byte(sb, 2);
  else if (!strcmp(k,"stat")) {
    bool small = m->value >= 0 && m->value <= 255;
    qa_msg_write_byte(sb, small ? 3 : 38); qa_msg_write_byte(sb, m->index);
    if (small) qa_msg_write_byte(sb, m->value); else qa_msg_write_long(sb, m->value);
  }
  else if (!strcmp(k,"set-view")) { qa_msg_write_byte(sb, 5); qa_msg_write_short(sb, m->entity); }
  else if (!strcmp(k,"sound")) {
    if (m->entity < 0 || m->entity >= 1024 || m->channel < 0 || m->channel >= 8 || m->index >= c.max_precache)
      qa_range_error("Sound exceeds selected QuakeWorld wire");
    int32_t mask = (ji(m->entity) << 3) | ji(m->channel);
    if (m->volume != 255) mask |= QA_QW_SND_VOLUME;
    if (m->attenuation != 1) mask |= QA_QW_SND_ATTENUATION;
    qa_msg_write_byte(sb, 6); qa_msg_write_short(sb, mask);
    if (mask & QA_QW_SND_VOLUME) qa_msg_write_byte(sb, m->volume);
    if (mask & QA_QW_SND_ATTENUATION) qa_msg_write_byte(sb, m->attenuation * 64);
    c.write_sound_index(&c, sb, m->index);
    qw_coord_vec(&c, sb, m->origin, flags);
  }
  else if (!strcmp(k,"print")) { qa_msg_write_byte(sb, 8); qa_msg_write_byte(sb, m->level); qa_msg_write_string(sb, m->text); }
  else if (!strcmp(k,"stufftext")) { qa_msg_write_byte(sb, 9); qa_msg_write_string(sb, m->text); }
  else if (!strcmp(k,"set-angle")) { qa_msg_write_byte(sb, 10); qw_angle_vec(&c, sb, m->angles, flags); }
  else if (!strcmp(k,"server-data")) qa_write_quakeworld_server_data(sb, m);
  else if (!strcmp(k,"light-style")) { qa_msg_write_byte(sb, 12); qa_msg_write_byte(sb, m->index); qa_msg_write_string(sb, m->str_value); }
  else if (!strcmp(k,"frags")) { qa_msg_write_byte(sb, 14); qa_msg_write_byte(sb, m->slot); qa_msg_write_short(sb, m->value); }
  else if (!strcmp(k,"stop-sound")) { qa_msg_write_byte(sb, 16); qa_msg_write_short(sb, m->entity * 8 + m->channel); }
  else if (!strcmp(k,"damage")) { qa_msg_write_byte(sb, 19); qa_msg_write_byte(sb, m->armor); qa_msg_write_byte(sb, m->blood); qw_coord_vec(&c, sb, m->source, flags); }
  else if (!strcmp(k,"static") || !strcmp(k,"baseline")) {
    bool is_static = !strcmp(k,"static");
    qa_msg_write_byte(sb, is_static ? 20 : 22);
    if (!is_static) qa_msg_write_short(sb, m->state.base.number);
    qa_qw_entity_state w; qa_qw_wire_entity_from_extended(&m->state, false, &w);
    c.write_qw_baseline(&c, sb, &w, flags);
  }
  else if (!strcmp(k,"temporary-entity")) {
    const qa_temporary_entity *e = &m->effect;
    if (e->kind == QA_TE_EXPLOSION_COLORS) qa_raise("Error", 0, 0, "NetQuake color explosion has no QuakeWorld opcode");
    qa_msg_write_byte(sb, 23); qa_msg_write_byte(sb, e->type);
    if (e->kind == QA_TE_BEAM) { qa_msg_write_short(sb, e->entity); qw_coord_vec(&c, sb, e->start, flags); qw_coord_vec(&c, sb, e->end, flags); }
    else {
      if (e->type == 2 || e->type == 12) qa_msg_write_byte(sb, e->count);
      qw_coord_vec(&c, sb, e->origin, flags);
    }
  }
  else if (!strcmp(k,"pause")) { qa_msg_write_byte(sb, 24); qa_msg_write_byte(sb, m->paused ? 1 : 0); }
  else if (!strcmp(k,"center-print")) { qa_msg_write_byte(sb, 26); qa_msg_write_string(sb, m->text); }
  else if (!strcmp(k,"killed-monster")) qa_msg_write_byte(sb, 27);
  else if (!strcmp(k,"found-secret")) qa_msg_write_byte(sb, 28);
  else if (!strcmp(k,"static-sound")) {
    qa_msg_write_byte(sb, 29); qw_coord_vec(&c, sb, m->origin, flags);
    c.write_sound_index(&c, sb, m->index);
    qa_msg_write_byte(sb, m->volume); qa_msg_write_byte(sb, m->attenuation * 64);
  }
  else if (!strcmp(k,"intermission")) { qa_msg_write_byte(sb, 30); qw_coord_vec(&c, sb, m->origin, flags); qw_angle_vec(&c, sb, m->angles, flags); }
  else if (!strcmp(k,"finale")) { qa_msg_write_byte(sb, 31); qa_msg_write_string(sb, m->text); }
  else if (!strcmp(k,"cd-track")) { qa_msg_write_byte(sb, 32); qa_msg_write_byte(sb, m->track); }
  else if (!strcmp(k,"sell-screen")) qa_msg_write_byte(sb, 33);
  else if (!strcmp(k,"kick")) {
    if (m->degrees != -2 && m->degrees != -4) qa_range_error("QuakeWorld kick must be -2 or -4");
    qa_msg_write_byte(sb, m->degrees == -2 ? 34 : 35);
  }
  else if (!strcmp(k,"ping")) { qa_msg_write_byte(sb, 36); qa_msg_write_byte(sb, m->slot); qa_msg_write_short(sb, m->value); }
  else if (!strcmp(k,"enter-time")) { qa_msg_write_byte(sb, 37); qa_msg_write_byte(sb, m->slot); qa_msg_write_float(sb, m->value); }
  else if (!strcmp(k,"muzzle-flash")) { qa_msg_write_byte(sb, 39); qa_msg_write_short(sb, m->entity); }
  else if (!strcmp(k,"userinfo")) { qa_msg_write_byte(sb, 40); qa_msg_write_byte(sb, m->slot); qa_msg_write_long(sb, m->user_id); qa_msg_write_string(sb, m->str_value); }
  else if (!strcmp(k,"download")) qa_write_quakeworld_download(sb, m->download_missing, m->download_percent, m->download_bytes, m->download_length);
  else if (!strcmp(k,"player")) qa_write_quakeworld_player(sb, profile, &m->player);
  else if (!strcmp(k,"nails")) {
    if (m->projectile_count > 255) qa_range_error("Too many nail projectiles");
    qa_msg_write_byte(sb, 43); qa_msg_write_byte(sb, (double)m->projectile_count);
    for (size_t i = 0; i < m->projectile_count; i++) {
      const qa_qw_nail *p = &m->projectiles[i];
      int32_t x = ji(trunc((p->origin.x + 4096) / 2)), y = ji(trunc((p->origin.y + 4096) / 2)), z = ji(trunc((p->origin.z + 4096) / 2));
      int32_t pitch = ji(trunc(p->pitch * 16 / 360)) & 15, yaw = ji(trunc(p->yaw * 256 / 360)) & 255;
      int32_t bs[6] = { x & 255, ((x >> 8) & 15) | ((y & 15) << 4), (int32_t)((uint32_t)y >> 4) & 255, z & 255, ((z >> 8) & 15) | (pitch << 4), yaw };
      for (int j = 0; j < 6; j++) qa_msg_write_byte(sb, bs[j]);
    }
  }
  else if (!strcmp(k,"choke-count")) { qa_msg_write_byte(sb, 44); qa_msg_write_byte(sb, m->count); }
  else if (!strcmp(k,"model-list") || !strcmp(k,"sound-list")) {
    qa_msg_write_byte(sb, !strcmp(k,"model-list") ? 45 : 46);
    c.write_precache_count(&c, sb, m->first);
    for (size_t i = 0; i < m->name_count; i++) qa_msg_write_string(sb, m->names[i]);
    qa_msg_write_byte(sb, 0);
    c.write_precache_count(&c, sb, m->next);
  }
  else if (!strcmp(k,"max-speed") || !strcmp(k,"entity-gravity")) {
    qa_msg_write_byte(sb, !strcmp(k,"max-speed") ? 49 : 50); qa_msg_write_float(sb, m->value);
  }
  else if (!strcmp(k,"set-info")) { qa_msg_write_byte(sb, 51); qa_msg_write_byte(sb, m->slot); qa_msg_write_string(sb, m->key); qa_msg_write_string(sb, m->str_value); }
  else if (!strcmp(k,"server-info")) { qa_msg_write_byte(sb, 52); qa_msg_write_string(sb, m->key); qa_msg_write_string(sb, m->str_value); }
  else if (!strcmp(k,"packet-loss")) { qa_msg_write_byte(sb, 53); qa_msg_write_byte(sb, m->slot); qa_msg_write_byte(sb, m->value); }
  else qa_raise("Error", 0, 0, "Unreachable message [object Object]");
}
