/* Port of src/network/q1/qw-recording.ts. GPL-2.0-or-later. */
#include "qa/network/q1/qw_recording.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
enum {
  F_BASELINE, F_LIGHT_STYLE, F_STAT, F_USERINFO, F_SET_INFO, F_SERVER_INFO,
  F_FRAGS, F_PING, F_ENTER_TIME, F_PACKET_LOSS, F_SET_ANGLE, F_SET_VIEW,
  F_PAUSE, F_INTERMISSION, F_FINALE, F_CD_TRACK, F_MAX_SPEED, F_ENTITY_GRAVITY
};
static char *dupstr(const char *s) { if (!s) return NULL; size_t n = strlen(s) + 1; char *p = malloc(n); memcpy(p, s, n); return p; }
static void copy_qw(const qa_quakeworld_message *src, qa_quakeworld_message *dst) {
  *dst = *src;
  dst->text = dupstr(src->text); dst->str_value = dupstr(src->str_value); dst->key = dupstr(src->key);
  dst->game_directory = dupstr(src->game_directory); dst->level_name = dupstr(src->level_name);
  dst->download_bytes = NULL;
  if (src->download_length) {
    dst->download_bytes = malloc(src->download_length);
    memcpy(dst->download_bytes, src->download_bytes, src->download_length);
  }
  dst->projectiles = NULL;
  if (src->projectile_count) {
    dst->projectiles = malloc(src->projectile_count * sizeof *dst->projectiles);
    memcpy(dst->projectiles, src->projectiles, src->projectile_count * sizeof *dst->projectiles);
  }
  dst->names = NULL;
  if (src->name_count) {
    dst->names = malloc(src->name_count * sizeof *dst->names);
    for (size_t i = 0; i < src->name_count; i++) dst->names[i] = dupstr(src->names[i]);
  }
  dst->entities = NULL; dst->entity_count = 0;
}
struct row { bool live; qa_quakeworld_message message; };
struct qa_quakeworld_recording_state {
  struct { int category; double discriminator; size_t row; } *fields; size_t field_count, field_cap;
  char **keys; size_t key_count, key_cap;
  struct { double key_id, slot; size_t id; } *pairs; size_t pair_count, pair_cap;
  double *pair_slots; size_t pair_slot_count, pair_slot_cap;
  struct row *rows; size_t row_count, row_cap;
  qa_quakeworld_message *statics; size_t static_count, static_cap;
};
void qa_quakeworld_recording_state_init(qa_quakeworld_recording_state *s) { memset(s, 0, sizeof *s); }
void qa_quakeworld_recording_state_destroy(qa_quakeworld_recording_state *s) {
  for (size_t i = 0; i < s->key_count; i++) free(s->keys[i]);
  for (size_t i = 0; i < s->row_count; i++) if (s->rows[i].live) qa_quakeworld_message_destroy(&s->rows[i].message);
  for (size_t i = 0; i < s->static_count; i++) qa_quakeworld_message_destroy(&s->statics[i]);
  free(s->fields); free(s->keys); free(s->pairs); free(s->pair_slots); free(s->rows); free(s->statics);
  memset(s, 0, sizeof *s);
}
static double intern_key(qa_quakeworld_recording_state *s, const char *key) {
  for (size_t i = 0; i < s->key_count; i++) if (strcmp(s->keys[i], key) == 0) return (double)i;
  if (s->key_count == s->key_cap) { size_t nc = s->key_cap ? s->key_cap * 2 : 16; s->keys = realloc(s->keys, nc * sizeof *s->keys); s->key_cap = nc; }
  s->keys[s->key_count] = dupstr(key);
  return (double)s->key_count++;
}
static long long find_field(qa_quakeworld_recording_state *s, int category, double discriminator) {
  for (size_t i = 0; i < s->field_count; i++)
    if (s->fields[i].category == category && s->fields[i].discriminator == discriminator) return (long long)s->fields[i].row;
  return -1;
}
static void remove_field_value(qa_quakeworld_recording_state *s, int category, double discriminator, size_t row) {
  for (size_t i = 0; i < s->field_count; i++)
    if (s->fields[i].category == category && s->fields[i].discriminator == discriminator && s->fields[i].row == row) {
      memmove(&s->fields[i], &s->fields[i+1], (s->field_count - i - 1) * sizeof *s->fields);
      s->field_count--; return;
    }
}
static void set_field(qa_quakeworld_recording_state *s, int category, double discriminator, const qa_quakeworld_message *m) {
  long long row = find_field(s, category, discriminator);
  if (row >= 0) {
    qa_quakeworld_message_destroy(&s->rows[row].message);
    copy_qw(m, &s->rows[row].message); s->rows[row].live = true;
    return;
  }
  if (s->field_count == s->field_cap) { size_t nc = s->field_cap ? s->field_cap * 2 : 16; s->fields = realloc(s->fields, nc * sizeof *s->fields); s->field_cap = nc; }
  s->fields[s->field_count].category = category;
  s->fields[s->field_count].discriminator = discriminator;
  s->fields[s->field_count].row = s->row_count;
  s->field_count++;
  if (s->row_count == s->row_cap) { size_t nc = s->row_cap ? s->row_cap * 2 : 16; s->rows = realloc(s->rows, nc * sizeof *s->rows); s->row_cap = nc; }
  s->rows[s->row_count].live = true;
  copy_qw(m, &s->rows[s->row_count].message);
  s->row_count++;
}
static double pair_id(qa_quakeworld_recording_state *s, double slot, const char *key) {
  double key_id = intern_key(s, key);
  for (size_t i = 0; i < s->pair_count; i++)
    if (s->pairs[i].key_id == key_id && s->pairs[i].slot == slot) return (double)s->pairs[i].id;
  if (s->pair_count == s->pair_cap) { size_t nc = s->pair_cap ? s->pair_cap * 2 : 16; s->pairs = realloc(s->pairs, nc * sizeof *s->pairs); s->pair_cap = nc; }
  size_t id = s->pair_slot_count;
  s->pairs[s->pair_count].key_id = key_id; s->pairs[s->pair_count].slot = slot; s->pairs[s->pair_count].id = id; s->pair_count++;
  if (s->pair_slot_count == s->pair_slot_cap) { size_t nc = s->pair_slot_cap ? s->pair_slot_cap * 2 : 16; s->pair_slots = realloc(s->pair_slots, nc * sizeof *s->pair_slots); s->pair_slot_cap = nc; }
  s->pair_slots[s->pair_slot_count++] = slot;
  return (double)id;
}
static void delete_set_info(qa_quakeworld_recording_state *s, double slot) {
  for (size_t pair = 0; pair < s->pair_slot_count; pair++) {
    if (s->pair_slots[pair] != slot) continue;
    long long row = find_field(s, F_SET_INFO, (double)pair);
    if (row < 0) continue;
    qa_quakeworld_message_destroy(&s->rows[row].message);
    s->rows[row].live = false;
    remove_field_value(s, F_SET_INFO, (double)pair, (size_t)row);
  }
}
void qa_quakeworld_recording_state_observe(qa_quakeworld_recording_state *s,
                                           const qa_quakeworld_message *messages, size_t count) {
  for (size_t i = 0; i < count; i++) {
    const qa_quakeworld_message *m = &messages[i];
    const char *k = m->kind;
    if (!strcmp(k, "server-data")) {
      s->field_count = 0;
      for (size_t j = 0; j < s->row_count; j++) if (s->rows[j].live) qa_quakeworld_message_destroy(&s->rows[j].message);
      s->row_count = 0;
      for (size_t j = 0; j < s->static_count; j++) qa_quakeworld_message_destroy(&s->statics[j]);
      s->static_count = 0;
    } else if (!strcmp(k, "static") || !strcmp(k, "static-sound")) {
      if (s->static_count == s->static_cap) { size_t nc = s->static_cap ? s->static_cap * 2 : 16; s->statics = realloc(s->statics, nc * sizeof *s->statics); s->static_cap = nc; }
      copy_qw(m, &s->statics[s->static_count++]);
    } else if (!strcmp(k, "baseline")) set_field(s, F_BASELINE, m->state.base.number, m);
    else if (!strcmp(k, "light-style")) set_field(s, F_LIGHT_STYLE, m->index, m);
    else if (!strcmp(k, "stat")) set_field(s, F_STAT, m->index, m);
    else if (!strcmp(k, "userinfo")) { delete_set_info(s, m->slot); set_field(s, F_USERINFO, m->slot, m); }
    else if (!strcmp(k, "set-info")) set_field(s, F_SET_INFO, pair_id(s, m->slot, m->key), m);
    else if (!strcmp(k, "server-info")) set_field(s, F_SERVER_INFO, intern_key(s, m->key), m);
    else if (!strcmp(k, "frags")) set_field(s, F_FRAGS, m->slot, m);
    else if (!strcmp(k, "ping")) set_field(s, F_PING, m->slot, m);
    else if (!strcmp(k, "enter-time")) set_field(s, F_ENTER_TIME, m->slot, m);
    else if (!strcmp(k, "packet-loss")) set_field(s, F_PACKET_LOSS, m->slot, m);
    else if (!strcmp(k, "set-angle")) set_field(s, F_SET_ANGLE, 0, m);
    else if (!strcmp(k, "set-view")) set_field(s, F_SET_VIEW, 0, m);
    else if (!strcmp(k, "pause")) set_field(s, F_PAUSE, 0, m);
    else if (!strcmp(k, "intermission")) set_field(s, F_INTERMISSION, 0, m);
    else if (!strcmp(k, "finale")) set_field(s, F_FINALE, 0, m);
    else if (!strcmp(k, "cd-track")) set_field(s, F_CD_TRACK, 0, m);
    else if (!strcmp(k, "max-speed")) set_field(s, F_MAX_SPEED, 0, m);
    else if (!strcmp(k, "entity-gravity")) set_field(s, F_ENTITY_GRAVITY, 0, m);
  }
}
static void put_i32le(uint8_t *b, size_t o, int32_t v) {
  uint32_t u = (uint32_t)v;
  b[o]=(uint8_t)u; b[o+1]=(uint8_t)(u>>8); b[o+2]=(uint8_t)(u>>16); b[o+3]=(uint8_t)(u>>24);
}
static int32_t ji(double n) { double m = fmod(trunc(n), 4294967296.0); if (m < 0) m += 4294967296.0; return (int32_t)(uint32_t)m; }
qa_quakeworld_demo_record *qa_quakeworld_recording_state_seed(qa_quakeworld_recording_state *s,
                                                              const qa_quakeworld_message *data,
                                                              const char *const *models, size_t model_count,
                                                              const char *const *sounds, size_t sound_count,
                                                              double seconds, double outgoing, double incoming,
                                                              size_t *out_count) {
  size_t list_msgs = (model_count ? model_count : 1) + (sound_count ? sound_count : 1);
  size_t live_rows = 0;
  for (size_t i = 0; i < s->row_count; i++) if (s->rows[i].live) live_rows++;
  size_t total = 1 + list_msgs + s->static_count + live_rows;
  const qa_quakeworld_message **order = malloc(total * sizeof *order);
  qa_quakeworld_message *synthetic = calloc(list_msgs, sizeof *synthetic);
  size_t synth_used = 0, at = 0;
  order[at++] = data;
  const char *kinds[2] = { "model-list", "sound-list" };
  const char *const *lists[2] = { models, sounds };
  size_t counts[2] = { model_count, sound_count };
  for (int li = 0; li < 2; li++) {
    if (counts[li] == 0) {
      qa_quakeworld_message *sm = &synthetic[synth_used++];
      sm->kind = kinds[li]; sm->first = 0; sm->next = 0;
      order[at++] = sm;
    }
    for (size_t first = 0; first < counts[li]; first++) {
      qa_quakeworld_message *sm = &synthetic[synth_used++];
      sm->kind = kinds[li]; sm->first = (double)first;
      sm->names = malloc(sizeof *sm->names);
      sm->names[0] = (char *)lists[li][first]; /* borrowed for encode only */
      sm->name_count = 1;
      sm->next = first + 1 == counts[li] ? 0 : (double)(first + 1);
      order[at++] = sm;
    }
  }
  for (size_t i = 0; i < s->static_count; i++) order[at++] = &s->statics[i];
  for (size_t i = 0; i < s->row_count; i++) if (s->rows[i].live) order[at++] = &s->rows[i].message;
  qa_quakeworld_demo_record *records = calloc(at + 1, sizeof *records);
  for (size_t i = 0; i < at; i++) {
    qa_size_buf payload; qa_size_buf_init(&payload, 1442, false);
    qa_write_quakeworld_message(&payload, data->protocol, order[i]);
    size_t plen = 0; uint8_t *pbytes = qa_size_buf_bytes(&payload, &plen);
    qa_size_buf_destroy(&payload);
    size_t n = plen + 8;
    uint8_t *bytes = malloc(n);
    put_i32le(bytes, 0, (int32_t)i + 1);
    put_i32le(bytes, 4, ji(outgoing - 1));
    memcpy(bytes + 8, pbytes, plen); free(pbytes);
    records[i].kind = "packet"; records[i].seconds = seconds;
    records[i].message = bytes; records[i].length = n;
  }
  records[at].kind = "sequences"; records[at].seconds = seconds;
  records[at].outgoing = outgoing; records[at].incoming = incoming;
  for (size_t i = 0; i < synth_used; i++) free(synthetic[i].names);
  free(synthetic); free(order);
  *out_count = at + 1;
  return records;
}
