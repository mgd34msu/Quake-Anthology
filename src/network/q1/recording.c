/* Port of src/network/q1/recording.ts. GPL-2.0-or-later. */
#include "qa/network/q1/recording.h"
#include "qa/foundation/support.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static char *dupstr(const char *s) { if (!s) return NULL; size_t n = strlen(s) + 1; char *p = malloc(n); memcpy(p, s, n); return p; }
static void copy_message(const qa_netquake_message *src, qa_netquake_message *dst) {
  *dst = *src;
  dst->text = dupstr(src->text); dst->level = dupstr(src->level); dst->str_value = dupstr(src->str_value);
  dst->models = NULL; dst->sounds = NULL;
  if (src->model_count) {
    dst->models = malloc(src->model_count * sizeof *dst->models);
    for (size_t i = 0; i < src->model_count; i++) dst->models[i] = dupstr(src->models[i]);
  }
  if (src->sound_count) {
    dst->sounds = malloc(src->sound_count * sizeof *dst->sounds);
    for (size_t i = 0; i < src->sound_count; i++) dst->sounds[i] = dupstr(src->sounds[i]);
  }
}
struct field_entry { char *key; qa_netquake_message message; };
struct qa_netquake_recording_state {
  bool has_info; qa_netquake_message info;
  qa_netquake_message *statics; size_t static_count, static_cap;
  struct field_entry *fields; size_t field_count, field_cap;
};
void qa_netquake_recording_state_init(qa_netquake_recording_state *s) { memset(s, 0, sizeof *s); }
void qa_netquake_recording_state_destroy(qa_netquake_recording_state *s) {
  if (s->has_info) qa_netquake_message_destroy(&s->info);
  for (size_t i = 0; i < s->static_count; i++) qa_netquake_message_destroy(&s->statics[i]);
  for (size_t i = 0; i < s->field_count; i++) { free(s->fields[i].key); qa_netquake_message_destroy(&s->fields[i].message); }
  free(s->statics); free(s->fields); memset(s, 0, sizeof *s);
}
static void set_field(qa_netquake_recording_state *s, const char *key, const qa_netquake_message *m) {
  for (size_t i = 0; i < s->field_count; i++) if (strcmp(s->fields[i].key, key) == 0) {
    qa_netquake_message_destroy(&s->fields[i].message);
    copy_message(m, &s->fields[i].message);
    return;
  }
  if (s->field_count == s->field_cap) { size_t nc = s->field_cap ? s->field_cap * 2 : 16; s->fields = realloc(s->fields, nc * sizeof *s->fields); s->field_cap = nc; }
  s->fields[s->field_count].key = dupstr(key);
  copy_message(m, &s->fields[s->field_count].message);
  s->field_count++;
}
static void clear_fields(qa_netquake_recording_state *s) {
  for (size_t i = 0; i < s->field_count; i++) { free(s->fields[i].key); qa_netquake_message_destroy(&s->fields[i].message); }
  s->field_count = 0;
  for (size_t i = 0; i < s->static_count; i++) qa_netquake_message_destroy(&s->statics[i]);
  s->static_count = 0;
}
void qa_netquake_recording_state_observe(qa_netquake_recording_state *s,
                                         const qa_netquake_message *messages, size_t count) {
  char keybuf[128];
  for (size_t i = 0; i < count; i++) {
    const qa_netquake_message *m = &messages[i];
    const char *k = m->kind;
    if (!strcmp(k, "server-info")) {
      if (s->has_info) qa_netquake_message_destroy(&s->info);
      copy_message(m, &s->info); s->has_info = true;
      clear_fields(s);
    } else if (!strcmp(k, "static") || !strcmp(k, "static-sound")) {
      if (s->static_count == s->static_cap) { size_t nc = s->static_cap ? s->static_cap * 2 : 16; s->statics = realloc(s->statics, nc * sizeof *s->statics); s->static_cap = nc; }
      copy_message(m, &s->statics[s->static_count++]);
    } else if (!strcmp(k, "baseline")) {
      snprintf(keybuf, sizeof keybuf, "baseline:%g", m->state.base.number);
      set_field(s, keybuf, m);
    } else if (!strcmp(k, "name") || !strcmp(k, "social") || !strcmp(k, "player-info")
               || !strcmp(k, "frags") || !strcmp(k, "colors") || !strcmp(k, "ping")) {
      snprintf(keybuf, sizeof keybuf, "%s:%g", k, m->slot);
      set_field(s, keybuf, m);
    } else if (!strcmp(k, "stat") || !strcmp(k, "light-style")) {
      snprintf(keybuf, sizeof keybuf, "%s:%g", k, m->index);
      set_field(s, keybuf, m);
    } else if (!strcmp(k, "set-view") || !strcmp(k, "set-angle") || !strcmp(k, "client-data")
               || !strcmp(k, "cd-track") || !strcmp(k, "time") || !strcmp(k, "skybox")
               || !strcmp(k, "finale") || !strcmp(k, "cutscene") || !strcmp(k, "intermission")) {
      set_field(s, k, m);
    }
  }
}
uint8_t **qa_netquake_recording_state_seed(qa_netquake_recording_state *s,
                                           qa_q1_protocol_identity protocol,
                                           size_t **out_lens, size_t *out_count) {
  if (!s->has_info) qa_range_error("NetQuake recording has no server info");
  size_t total = 1 + 1 + s->static_count + s->field_count + 2;
  const qa_netquake_message **order = malloc(total * sizeof *order);
  qa_netquake_message signon1, signon2, signon3;
  memset(&signon1, 0, sizeof signon1); signon1.kind = "signon"; signon1.stage = 1;
  memset(&signon2, 0, sizeof signon2); signon2.kind = "signon"; signon2.stage = 2;
  memset(&signon3, 0, sizeof signon3); signon3.kind = "signon"; signon3.stage = 3;
  size_t at = 0;
  order[at++] = &s->info;
  order[at++] = &signon1;
  for (size_t i = 0; i < s->static_count; i++) order[at++] = &s->statics[i];
  for (size_t i = 0; i < s->field_count; i++) order[at++] = &s->fields[i].message;
  order[at++] = &signon2;
  order[at++] = &signon3;
  uint8_t **out = malloc(at * sizeof *out);
  size_t *lens = malloc(at * sizeof *lens);
  for (size_t i = 0; i < at; i++) {
    qa_size_buf buf; qa_size_buf_init(&buf, 65536, false);
    qa_write_netquake_message(&buf, protocol, order[i], "known-retail", true);
    out[i] = qa_size_buf_bytes(&buf, &lens[i]);
    qa_size_buf_destroy(&buf);
  }
  free(order);
  *out_lens = lens; *out_count = at;
  return out;
}
