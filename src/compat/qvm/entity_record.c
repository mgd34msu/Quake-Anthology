/* Port of src/compat/qvm/entity-record.ts. GPL-2.0-or-later. */
#include "qa/compat/qvm/entity_record.h"

#include <stdlib.h>
#include <string.h>

#include "qa/core/binary/index.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"

/* Mirrors of legacy-presentation.ts (qvm-b slice). */
static const int32_t qvmx_events[] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,48,49,50,51,53,54,55,56,57,58,59,60,61,62,63,64,66,68};
static bool modern(qa_qvm_abi_profile profile) { return profile == NULL || strcmp(profile, "q3-modern") == 0; }
/* Remote servers choose these values, so the client paths pass lenient and unrepresentable ones become none/general/absent. */
int32_t qa_qvmx_event_ex(double value, qa_qvm_abi_profile profile, bool reverse, bool lenient) {
  if (modern(profile)) return qa_int32(value);
  int32_t word = qa_int32(value);
  int32_t event = word & 255, flags = word & ~255;
  int32_t mapped = -1;
  if (reverse) { for (size_t i = 0; i < sizeof qvmx_events / sizeof qvmx_events[0]; i++) if (qvmx_events[i] == event) { mapped = (int32_t)i; break; } }
  else if (event >= 0 && event < (int32_t)(sizeof qvmx_events / sizeof qvmx_events[0])) mapped = qvmx_events[event];
  if (mapped < 0) {
    if (lenient) return 0;
    qa_range_error("QVM event %d is not represented by the selected legacy ABI", (int)event);
  }
  return mapped | flags;
}
int32_t qa_qvmx_event(double value, qa_qvm_abi_profile profile, bool reverse) { return qa_qvmx_event_ex(value, profile, reverse, false); }
int32_t qa_qvmx_entity_type_ex(double value, qa_qvm_abi_profile profile, bool reverse, bool lenient) {
  if (modern(profile)) return qa_int32(value);
  int32_t source_events = reverse ? 13 : 12, target_events = reverse ? 12 : 13;
  int32_t word = qa_int32(value);
  if (word >= source_events) {
    const int32_t event = qa_qvmx_event_ex(word - source_events, profile, reverse, lenient);
    return event == 0 && lenient ? 0 : target_events + event;
  }
  if (reverse && word == 12) {
    if (lenient) return 0;
    qa_range_error("Legacy QVM has no team entity type");
  }
  return word;
}
int32_t qa_qvmx_entity_type(double value, qa_qvm_abi_profile profile, bool reverse) { return qa_qvmx_entity_type_ex(value, profile, reverse, false); }
int32_t qa_qvmx_powerup_bits_ex(double bits, qa_qvm_abi_profile profile, bool lenient) {
  int32_t word = qa_int32(bits);
  if (!modern(profile) && lenient) return word & 0x1ff;
  if (!modern(profile) && (word & ~0x1ff) != 0) qa_range_error("Legacy ball or private powerup has no modern presentation mapping");
  return word;
}
int32_t qa_qvmx_powerup_bits(double bits, qa_qvm_abi_profile profile) { return qa_qvmx_powerup_bits_ex(bits, profile, false); }

int32_t qa_qvmx_configstring(double index, qa_qvm_abi_profile profile) {
  if (modern(profile)) return qa_int32(index);
  int32_t word = qa_int32(index);
  if (word >= 12 && word <= 15) return word + 8;
  if (word >= 16 && word <= 26)
    qa_range_error("Legacy private configstring %d has no declared modern presentation mapping", (int)word);
  return word;
}

double qa_qvm_entity_state_bytes_for(qa_qvm_abi_profile profile) { return modern(profile) ? 208 : 204; }

static void require_record(const qa_qvmx_view *view, qa_qvm_abi_profile profile) {
  double bytes = qa_qvm_entity_state_bytes_for(profile);
  if ((double)view->length < bytes)
    qa_binary_error("QVM entityState_t", 0, "record requires %d bytes, received %zu", (int)bytes, view->length);
}

static qa_vec3 read_vector(const qa_qvmx_view *view, int64_t offset) {
  qa_vec3 v = {qa_qvmx_view_f32(view, offset), qa_qvmx_view_f32(view, offset + 4), qa_qvmx_view_f32(view, offset + 8)};
  return v;
}
static void write_vector(qa_qvmx_view *view, int64_t offset, qa_vec3 value) {
  qa_qvmx_view_set_f32(view, offset, qa_float32(value.x));
  qa_qvmx_view_set_f32(view, offset + 4, qa_float32(value.y));
  qa_qvmx_view_set_f32(view, offset + 8, qa_float32(value.z));
}
static qa_qvm_trajectory read_trajectory(const qa_qvmx_view *view, int64_t offset) {
  qa_qvm_trajectory t;
  t.type = qa_qvmx_view_i32(view, offset); t.time = qa_qvmx_view_i32(view, offset + 4);
  t.duration = qa_qvmx_view_i32(view, offset + 8);
  t.base = read_vector(view, offset + 12); t.delta = read_vector(view, offset + 24);
  return t;
}
static void write_trajectory(qa_qvmx_view *view, int64_t offset, qa_qvm_trajectory value) {
  qa_qvmx_view_set_i32(view, offset, qa_int32(value.type));
  qa_qvmx_view_set_i32(view, offset + 4, qa_int32(value.time));
  qa_qvmx_view_set_i32(view, offset + 8, qa_int32(value.duration));
  write_vector(view, offset + 12, value.base); write_vector(view, offset + 24, value.delta);
}

double qa_qvm_entity_state_get_number(const qa_qvm_entity_state *s) { return qa_qvmx_view_i32(&s->view, 0); }
void qa_qvm_entity_state_set_number(qa_qvm_entity_state *s, double v) { qa_qvmx_view_set_i32((qa_qvmx_view *)&s->view, 0, qa_int32(v)); }
double qa_qvm_entity_state_get_e_type(const qa_qvm_entity_state *s) { int32_t v = qa_qvmx_view_i32(&s->view, 4); return s->source ? v : qa_qvmx_entity_type(v, s->profile, false); }
void qa_qvm_entity_state_set_e_type(qa_qvm_entity_state *s, double v) { qa_qvmx_view_set_i32((qa_qvmx_view *)&s->view, 4, s->source ? qa_int32(v) : qa_qvmx_entity_type_ex(v, s->profile, true, s->lenient)); }
double qa_qvm_entity_state_get_e_flags(const qa_qvm_entity_state *s) { return qa_qvmx_view_i32(&s->view, 8); }
void qa_qvm_entity_state_set_e_flags(qa_qvm_entity_state *s, double v) { qa_qvmx_view_set_i32((qa_qvmx_view *)&s->view, 8, qa_int32(v)); }
qa_qvm_trajectory qa_qvm_entity_state_get_pos(const qa_qvm_entity_state *s) { return read_trajectory(&s->view, 12); }
void qa_qvm_entity_state_set_pos(qa_qvm_entity_state *s, qa_qvm_trajectory v) { write_trajectory((qa_qvmx_view *)&s->view, 12, v); }
qa_qvm_trajectory qa_qvm_entity_state_get_apos(const qa_qvm_entity_state *s) { return read_trajectory(&s->view, 48); }
void qa_qvm_entity_state_set_apos(qa_qvm_entity_state *s, qa_qvm_trajectory v) { write_trajectory((qa_qvmx_view *)&s->view, 48, v); }
double qa_qvm_entity_state_get_time(const qa_qvm_entity_state *s) { return qa_qvmx_view_i32(&s->view, 84); }
void qa_qvm_entity_state_set_time(qa_qvm_entity_state *s, double v) { qa_qvmx_view_set_i32((qa_qvmx_view *)&s->view, 84, qa_int32(v)); }
double qa_qvm_entity_state_get_time2(const qa_qvm_entity_state *s) { return qa_qvmx_view_i32(&s->view, 88); }
void qa_qvm_entity_state_set_time2(qa_qvm_entity_state *s, double v) { qa_qvmx_view_set_i32((qa_qvmx_view *)&s->view, 88, qa_int32(v)); }
qa_vec3 qa_qvm_entity_state_get_origin(const qa_qvm_entity_state *s) { return read_vector(&s->view, 92); }
void qa_qvm_entity_state_set_origin(qa_qvm_entity_state *s, qa_vec3 v) { write_vector((qa_qvmx_view *)&s->view, 92, v); }
qa_vec3 qa_qvm_entity_state_get_origin2(const qa_qvm_entity_state *s) { return read_vector(&s->view, 104); }
void qa_qvm_entity_state_set_origin2(qa_qvm_entity_state *s, qa_vec3 v) { write_vector((qa_qvmx_view *)&s->view, 104, v); }
qa_vec3 qa_qvm_entity_state_get_angles(const qa_qvm_entity_state *s) { return read_vector(&s->view, 116); }
void qa_qvm_entity_state_set_angles(qa_qvm_entity_state *s, qa_vec3 v) { write_vector((qa_qvmx_view *)&s->view, 116, v); }
qa_vec3 qa_qvm_entity_state_get_angles2(const qa_qvm_entity_state *s) { return read_vector(&s->view, 128); }
void qa_qvm_entity_state_set_angles2(qa_qvm_entity_state *s, qa_vec3 v) { write_vector((qa_qvmx_view *)&s->view, 128, v); }
double qa_qvm_entity_state_get_other_entity_num(const qa_qvm_entity_state *s) { return qa_qvmx_view_i32(&s->view, 140); }
void qa_qvm_entity_state_set_other_entity_num(qa_qvm_entity_state *s, double v) { qa_qvmx_view_set_i32((qa_qvmx_view *)&s->view, 140, qa_int32(v)); }
double qa_qvm_entity_state_get_other_entity_num2(const qa_qvm_entity_state *s) { return qa_qvmx_view_i32(&s->view, 144); }
void qa_qvm_entity_state_set_other_entity_num2(qa_qvm_entity_state *s, double v) { qa_qvmx_view_set_i32((qa_qvmx_view *)&s->view, 144, qa_int32(v)); }
double qa_qvm_entity_state_get_ground_entity_num(const qa_qvm_entity_state *s) { return qa_qvmx_view_i32(&s->view, 148); }
void qa_qvm_entity_state_set_ground_entity_num(qa_qvm_entity_state *s, double v) { qa_qvmx_view_set_i32((qa_qvmx_view *)&s->view, 148, qa_int32(v)); }
double qa_qvm_entity_state_get_constant_light(const qa_qvm_entity_state *s) { return qa_qvmx_view_i32(&s->view, 152); }
void qa_qvm_entity_state_set_constant_light(qa_qvm_entity_state *s, double v) { qa_qvmx_view_set_i32((qa_qvmx_view *)&s->view, 152, qa_int32(v)); }
double qa_qvm_entity_state_get_loop_sound(const qa_qvm_entity_state *s) { return qa_qvmx_view_i32(&s->view, 156); }
void qa_qvm_entity_state_set_loop_sound(qa_qvm_entity_state *s, double v) { qa_qvmx_view_set_i32((qa_qvmx_view *)&s->view, 156, qa_int32(v)); }
double qa_qvm_entity_state_get_modelindex(const qa_qvm_entity_state *s) { return qa_qvmx_view_i32(&s->view, 160); }
void qa_qvm_entity_state_set_modelindex(qa_qvm_entity_state *s, double v) { qa_qvmx_view_set_i32((qa_qvmx_view *)&s->view, 160, qa_int32(v)); }
double qa_qvm_entity_state_get_modelindex2(const qa_qvm_entity_state *s) { return qa_qvmx_view_i32(&s->view, 164); }
void qa_qvm_entity_state_set_modelindex2(qa_qvm_entity_state *s, double v) { qa_qvmx_view_set_i32((qa_qvmx_view *)&s->view, 164, qa_int32(v)); }
double qa_qvm_entity_state_get_client_num(const qa_qvm_entity_state *s) { return qa_qvmx_view_i32(&s->view, 168); }
void qa_qvm_entity_state_set_client_num(qa_qvm_entity_state *s, double v) { qa_qvmx_view_set_i32((qa_qvmx_view *)&s->view, 168, qa_int32(v)); }
double qa_qvm_entity_state_get_frame(const qa_qvm_entity_state *s) { return qa_qvmx_view_i32(&s->view, 172); }
void qa_qvm_entity_state_set_frame(qa_qvm_entity_state *s, double v) { qa_qvmx_view_set_i32((qa_qvmx_view *)&s->view, 172, qa_int32(v)); }
double qa_qvm_entity_state_get_solid(const qa_qvm_entity_state *s) { return qa_qvmx_view_i32(&s->view, 176); }
void qa_qvm_entity_state_set_solid(qa_qvm_entity_state *s, double v) { qa_qvmx_view_set_i32((qa_qvmx_view *)&s->view, 176, qa_int32(v)); }
double qa_qvm_entity_state_get_event(const qa_qvm_entity_state *s) { int32_t v = qa_qvmx_view_i32(&s->view, 180); return s->source ? v : qa_qvmx_event(v, s->profile, false); }
void qa_qvm_entity_state_set_event(qa_qvm_entity_state *s, double v) { qa_qvmx_view_set_i32((qa_qvmx_view *)&s->view, 180, s->source ? qa_int32(v) : qa_qvmx_event_ex(v, s->profile, true, s->lenient)); }
double qa_qvm_entity_state_get_event_parm(const qa_qvm_entity_state *s) { return qa_qvmx_view_i32(&s->view, 184); }
void qa_qvm_entity_state_set_event_parm(qa_qvm_entity_state *s, double v) { qa_qvmx_view_set_i32((qa_qvmx_view *)&s->view, 184, qa_int32(v)); }
double qa_qvm_entity_state_get_powerups(const qa_qvm_entity_state *s) { int32_t v = qa_qvmx_view_i32(&s->view, 188); return s->source ? v : qa_qvmx_powerup_bits(v, s->profile); }
void qa_qvm_entity_state_set_powerups(qa_qvm_entity_state *s, double v) { qa_qvmx_view_set_i32((qa_qvmx_view *)&s->view, 188, s->source ? qa_int32(v) : qa_qvmx_powerup_bits_ex(v, s->profile, s->lenient)); }
double qa_qvm_entity_state_get_weapon(const qa_qvm_entity_state *s) { return qa_qvmx_view_i32(&s->view, 192); }
void qa_qvm_entity_state_set_weapon(qa_qvm_entity_state *s, double v) { qa_qvmx_view_set_i32((qa_qvmx_view *)&s->view, 192, qa_int32(v)); }
double qa_qvm_entity_state_get_legs_anim(const qa_qvm_entity_state *s) { return qa_qvmx_view_i32(&s->view, 196); }
void qa_qvm_entity_state_set_legs_anim(qa_qvm_entity_state *s, double v) { qa_qvmx_view_set_i32((qa_qvmx_view *)&s->view, 196, qa_int32(v)); }
double qa_qvm_entity_state_get_torso_anim(const qa_qvm_entity_state *s) { return qa_qvmx_view_i32(&s->view, 200); }
void qa_qvm_entity_state_set_torso_anim(qa_qvm_entity_state *s, double v) { qa_qvmx_view_set_i32((qa_qvmx_view *)&s->view, 200, qa_int32(v)); }
double qa_qvm_entity_state_get_generic1(const qa_qvm_entity_state *s) { return modern(s->profile) ? qa_qvmx_view_i32(&s->view, 204) : 0; }
void qa_qvm_entity_state_set_generic1(qa_qvm_entity_state *s, double v) { if (modern(s->profile)) qa_qvmx_view_set_i32((qa_qvmx_view *)&s->view, 204, qa_int32(v)); else if (v != 0 && !s->lenient) qa_range_error("Legacy QVM entity has no generic1 field"); }

qa_qvm_entity_state qa_qvm_borrow_entity_state(qa_qvmx_view view, qa_qvm_abi_profile profile) {
  require_record(&view, profile);
  qa_qvm_entity_state s = {view, profile == NULL ? "q3-modern" : profile, false, NULL, false};
  return s;
}
qa_qvm_entity_state qa_qvm_entity_state_copy(const qa_qvm_entity_state *state) {
  size_t bytes = (size_t)qa_qvm_entity_state_bytes_for(state->profile);
  uint8_t *copy = malloc(bytes);
  if (copy == NULL) qa_range_error("out of memory");
  memcpy(copy, state->view.bytes, bytes);
  qa_qvm_entity_state s = {{copy, bytes}, state->profile, state->source, copy, state->lenient};
  return s;
}
qa_qvm_entity_state qa_qvm_read_entity_state(qa_qvmx_view view) {
  qa_qvm_entity_state borrowed = qa_qvm_borrow_entity_state(view, NULL);
  return qa_qvm_entity_state_copy(&borrowed);
}
qa_qvm_entity_state qa_qvm_read_source_entity_state(qa_qvmx_view view, qa_qvm_abi_profile profile) {
  require_record(&view, profile);
  qa_qvm_entity_state borrowed = {view, profile, true, NULL, false};
  return qa_qvm_entity_state_copy(&borrowed);
}
static void write_entity_state(qa_qvmx_view view, const qa_qvm_entity_state_fields *f, qa_qvm_abi_profile profile, bool source,
                               bool lenient) {
  require_record(&view, profile);
  qa_qvm_entity_state t = {view, profile == NULL ? "q3-modern" : profile, source, NULL, lenient};
  qa_qvm_entity_state_set_number(&t, f->number);
  qa_qvm_entity_state_set_e_type(&t, f->e_type);
  qa_qvm_entity_state_set_e_flags(&t, f->e_flags);
  qa_qvm_entity_state_set_pos(&t, f->pos);
  qa_qvm_entity_state_set_apos(&t, f->apos);
  qa_qvm_entity_state_set_time(&t, f->time);
  qa_qvm_entity_state_set_time2(&t, f->time2);
  qa_qvm_entity_state_set_origin(&t, f->origin);
  qa_qvm_entity_state_set_origin2(&t, f->origin2);
  qa_qvm_entity_state_set_angles(&t, f->angles);
  qa_qvm_entity_state_set_angles2(&t, f->angles2);
  qa_qvm_entity_state_set_other_entity_num(&t, f->other_entity_num);
  qa_qvm_entity_state_set_other_entity_num2(&t, f->other_entity_num2);
  qa_qvm_entity_state_set_ground_entity_num(&t, f->ground_entity_num);
  qa_qvm_entity_state_set_constant_light(&t, f->constant_light);
  qa_qvm_entity_state_set_loop_sound(&t, f->loop_sound);
  qa_qvm_entity_state_set_modelindex(&t, f->modelindex);
  qa_qvm_entity_state_set_modelindex2(&t, f->modelindex2);
  qa_qvm_entity_state_set_client_num(&t, f->client_num);
  qa_qvm_entity_state_set_frame(&t, f->frame);
  qa_qvm_entity_state_set_solid(&t, f->solid);
  qa_qvm_entity_state_set_event(&t, f->event);
  qa_qvm_entity_state_set_event_parm(&t, f->event_parm);
  qa_qvm_entity_state_set_powerups(&t, f->powerups);
  qa_qvm_entity_state_set_weapon(&t, f->weapon);
  qa_qvm_entity_state_set_legs_anim(&t, f->legs_anim);
  qa_qvm_entity_state_set_torso_anim(&t, f->torso_anim);
  qa_qvm_entity_state_set_generic1(&t, f->generic1);
}
void qa_qvm_write_source_entity_state(qa_qvmx_view view, const qa_qvm_entity_state_fields *state, qa_qvm_abi_profile profile) { write_entity_state(view, state, profile, true, false); }
void qa_qvm_write_entity_state(qa_qvmx_view view, const qa_qvm_entity_state_fields *state, qa_qvm_abi_profile profile) { write_entity_state(view, state, profile, false, false); }
void qa_qvm_write_entity_state_unmapped(qa_qvmx_view view, const qa_qvm_entity_state_fields *state, qa_qvm_abi_profile profile,
                                        bool ignore_unmapped) {
  write_entity_state(view, state, profile, false, ignore_unmapped);
}
void qa_qvm_entity_state_copy_from(qa_qvm_entity_state *state, const qa_qvm_entity_state_fields *source) { write_entity_state(state->view, source, state->profile, state->source, state->lenient); }
void qa_qvm_entity_state_destroy(qa_qvm_entity_state *state) { if (state == NULL) return; free(state->owned_bytes); state->owned_bytes = NULL; }
void qa_qvm_get_entity_state_fields(const qa_qvm_entity_state *state, qa_qvm_entity_state_fields *out) {
  out->number = qa_qvm_entity_state_get_number(state);
  out->e_type = qa_qvm_entity_state_get_e_type(state);
  out->e_flags = qa_qvm_entity_state_get_e_flags(state);
  out->pos = qa_qvm_entity_state_get_pos(state);
  out->apos = qa_qvm_entity_state_get_apos(state);
  out->time = qa_qvm_entity_state_get_time(state);
  out->time2 = qa_qvm_entity_state_get_time2(state);
  out->origin = qa_qvm_entity_state_get_origin(state);
  out->origin2 = qa_qvm_entity_state_get_origin2(state);
  out->angles = qa_qvm_entity_state_get_angles(state);
  out->angles2 = qa_qvm_entity_state_get_angles2(state);
  out->other_entity_num = qa_qvm_entity_state_get_other_entity_num(state);
  out->other_entity_num2 = qa_qvm_entity_state_get_other_entity_num2(state);
  out->ground_entity_num = qa_qvm_entity_state_get_ground_entity_num(state);
  out->constant_light = qa_qvm_entity_state_get_constant_light(state);
  out->loop_sound = qa_qvm_entity_state_get_loop_sound(state);
  out->modelindex = qa_qvm_entity_state_get_modelindex(state);
  out->modelindex2 = qa_qvm_entity_state_get_modelindex2(state);
  out->client_num = qa_qvm_entity_state_get_client_num(state);
  out->frame = qa_qvm_entity_state_get_frame(state);
  out->solid = qa_qvm_entity_state_get_solid(state);
  out->event = qa_qvm_entity_state_get_event(state);
  out->event_parm = qa_qvm_entity_state_get_event_parm(state);
  out->powerups = qa_qvm_entity_state_get_powerups(state);
  out->weapon = qa_qvm_entity_state_get_weapon(state);
  out->legs_anim = qa_qvm_entity_state_get_legs_anim(state);
  out->torso_anim = qa_qvm_entity_state_get_torso_anim(state);
  out->generic1 = qa_qvm_entity_state_get_generic1(state);
}
