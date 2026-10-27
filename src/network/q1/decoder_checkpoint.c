/* Port of src/network/q1/decoder-checkpoint.ts. GPL-2.0-or-later. */
#include "qa/network/q1/decoder_checkpoint.h"
#include <string.h>
qa_captured_wire_entity qa_capture_wire_entity_nq(const qa_q1_wire_entity_state *state) {
  qa_captured_wire_entity o;
  for (int i = 0; i < 3; i++) { o.origin[i] = qa_wire_vector_get(&state->origin, (size_t)i); o.angles[i] = qa_wire_vector_get(&state->angles, (size_t)i); }
  o.modelindex = state->modelindex; o.frame = state->frame; o.colormap = state->colormap;
  o.skin = state->skin; o.alpha = state->alpha; o.scale = state->scale; o.effects = state->effects;
  return o;
}
qa_captured_wire_entity qa_capture_wire_entity_qw(const qa_qw_entity_state *state) {
  qa_captured_wire_entity o;
  for (int i = 0; i < 3; i++) { o.origin[i] = qa_wire_vector_get(&state->origin, (size_t)i); o.angles[i] = qa_wire_vector_get(&state->angles, (size_t)i); }
  o.modelindex = state->modelindex; o.frame = state->frame; o.colormap = state->colormap;
  o.skin = state->skinnum; o.alpha = state->alpha; o.scale = state->scale; o.effects = state->effects;
  return o;
}
qa_captured_qw_wire_entity qa_capture_qw_wire_entity(const qa_qw_entity_state *state) {
  qa_captured_qw_wire_entity o; o.base = qa_capture_wire_entity_qw(state);
  o.number = state->number; o.flags = state->flags; return o;
}
static void read_vector(const qa_save_reader *reader, const char *name, qa_wire_vector *out) {
  qa_save_reader field, item;
  (void)qa_save_reader_field(reader, name, &field);
  if (qa_save_reader_list_count(&field) != 3) qa_save_reader_fail(&field, "expected a three component vector");
  for (size_t i = 0; i < 3; i++) {
    (void)qa_save_reader_item(&field, i, &item);
    qa_wire_vector_set(out, i, qa_save_reader_number(&item));
    qa_save_reader_destroy(&item);
  }
  qa_save_reader_destroy(&field);
}
static double read_int(const qa_save_reader *reader, const char *name, int has_min) {
  qa_save_reader field; (void)qa_save_reader_field(reader, name, &field);
  double v = has_min ? qa_save_reader_integer_min(&field, 0) : qa_save_reader_integer(&field);
  qa_save_reader_destroy(&field); return v;
}
void qa_read_nq_wire_entity(const qa_save_reader *reader, qa_q1_wire_entity_state *out) {
  qa_q1_wire_entity_state_init(out);
  read_vector(reader, "origin", &out->origin); read_vector(reader, "angles", &out->angles);
  out->modelindex = read_int(reader, "modelindex", 1); out->frame = read_int(reader, "frame", 1);
  out->colormap = read_int(reader, "colormap", 1); out->alpha = read_int(reader, "alpha", 1);
  out->scale = read_int(reader, "scale", 1); out->effects = read_int(reader, "effects", 0);
  out->skin = read_int(reader, "skin", 0);
}
void qa_read_qw_wire_entity(const qa_save_reader *reader, qa_qw_entity_state *out) {
  qa_qw_entity_state_init(out);
  read_vector(reader, "origin", &out->origin); read_vector(reader, "angles", &out->angles);
  out->modelindex = read_int(reader, "modelindex", 1); out->frame = read_int(reader, "frame", 1);
  out->colormap = read_int(reader, "colormap", 1); out->alpha = read_int(reader, "alpha", 1);
  out->scale = read_int(reader, "scale", 1); out->effects = read_int(reader, "effects", 0);
  out->skinnum = read_int(reader, "skin", 0);
  out->number = read_int(reader, "number", 1); out->flags = read_int(reader, "flags", 0);
}
