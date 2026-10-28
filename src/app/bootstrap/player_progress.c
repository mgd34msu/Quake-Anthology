/* Port of src/app/bootstrap/player-progress.ts. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "qa/app/bootstrap/player_progress.h"
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "qa/foundation/support.h"
#include "qa/persistence/value.h"
typedef struct owned_event {
  qa_app_player_progress_event view;
  char *kind, *source, *participant, *event, *award, *map;
} owned_event;
struct qa_app_player_progress_store {
  char *file;
  owned_event *events;
  size_t count, capacity;
};
static char *dup_text(const char *text) {
  if (text == NULL) return NULL;
  size_t len = strlen(text);
  char *out = malloc(len + 1);
  if (out == NULL) qa_range_error("Out of memory");
  memcpy(out, text, len + 1);
  return out;
}
static bool is(const char *a, const char *b) { return a != NULL && strcmp(a, b) == 0; }
/* TS decode() over an already-typed event (record path). */
static void validate(qa_app_player_progress_event e) {
  if (!(is(e.source, "q1") || is(e.source, "q2") || is(e.source, "q3"))
      || e.participant == NULL || e.participant[0] == '\0' || e.event == NULL || e.event[0] == '\0')
    qa_range_error("Invalid player progress identity");
  if (is(e.kind, "achievement") && !is(e.source, "q3") && e.award != NULL && e.award[0] != '\0') return;
  if (e.map == NULL || e.map[0] == '\0') qa_range_error("Invalid player progress map");
  if (is(e.kind, "level-completed") && !is(e.source, "q3")) return;
  if (is(e.kind, "match-completed") && isfinite(e.score)) return;
  qa_range_error("Invalid player progress event");
}
static const char *str_field(const qa_save_value *object, const char *key) {
  const qa_save_value *value = qa_save_value_get(object, key);
  return value != NULL && value->kind == QA_SAVE_STRING ? value->text : NULL;
}
static owned_event own_event(qa_app_player_progress_event e) {
  owned_event owned = {0};
  owned.kind = dup_text(e.kind); owned.source = dup_text(e.source);
  owned.participant = dup_text(e.participant); owned.event = dup_text(e.event);
  owned.award = dup_text(e.award); owned.map = dup_text(e.map);
  owned.view.kind = owned.kind; owned.view.source = owned.source;
  owned.view.participant = owned.participant; owned.view.event = owned.event;
  owned.view.award = owned.award; owned.view.map = owned.map; owned.view.score = e.score;
  return owned;
}
static void free_owned(owned_event *e) {
  free(e->kind); free(e->source); free(e->participant); free(e->event); free(e->award); free(e->map);
}
static bool same_identity(const qa_app_player_progress_event *a, const qa_app_player_progress_event *b) {
  return strcmp(a->source, b->source) == 0 && strcmp(a->participant, b->participant) == 0
      && strcmp(a->event, b->event) == 0;
}
static void push(qa_app_player_progress_store *store, owned_event event) {
  if (store->count == store->capacity) {
    size_t cap = store->capacity == 0 ? 8 : store->capacity * 2;
    owned_event *next = realloc(store->events, cap * sizeof *next);
    if (next == NULL) qa_range_error("Out of memory");
    store->events = next; store->capacity = cap;
  }
  store->events[store->count++] = event;
}
static void append_json_string(FILE *out, const char *text) {
  fputc('"', out);
  for (const unsigned char *p = (const unsigned char *)text; *p != '\0'; p++) {
    switch (*p) {
      case '"': fputs("\\\"", out); break;
      case '\\': fputs("\\\\", out); break;
      case '\n': fputs("\\n", out); break;
      case '\r': fputs("\\r", out); break;
      case '\t': fputs("\\t", out); break;
      case '\b': fputs("\\b", out); break;
      case '\f': fputs("\\f", out); break;
      default:
        if (*p < 0x20) fprintf(out, "\\u%04x", *p);
        else fputc(*p, out);
        break;
    }
  }
  fputc('"', out);
}
static void write_events(FILE *out, qa_app_player_progress_store *store, const owned_event *extra) {
  fputs("{\"version\":1,\"events\":[", out);
  size_t total = store->count + (extra != NULL ? 1 : 0);
  for (size_t i = 0; i < total; i++) {
    const qa_app_player_progress_event *e = i < store->count ? &store->events[i].view : &extra->view;
    if (i != 0) fputc(',', out);
    fputs("{\"kind\":", out); append_json_string(out, e->kind);
    fputs(",\"source\":", out); append_json_string(out, e->source);
    fputs(",\"participant\":", out); append_json_string(out, e->participant);
    fputs(",\"event\":", out); append_json_string(out, e->event);
    if (is(e->kind, "achievement")) { fputs(",\"award\":", out); append_json_string(out, e->award); }
    else {
      fputs(",\"map\":", out); append_json_string(out, e->map);
      if (is(e->kind, "match-completed")) {
        if (e->score == (double)(long long)e->score) fprintf(out, ",\"score\":%lld", (long long)e->score);
        else fprintf(out, ",\"score\":%g", e->score);
      }
    }
    fputc('}', out);
  }
  fputs("]}\n", out);
}
static void mkdir_parent(const char *file) {
  const char *slash = strrchr(file, '/');
  if (slash == NULL || slash == file) return;
  char *dir = malloc((size_t)(slash - file) + 1);
  if (dir == NULL) qa_range_error("Out of memory");
  memcpy(dir, file, (size_t)(slash - file)); dir[slash - file] = '\0';
  /* mkdir -p over each prefix. */
  for (char *p = dir + 1; *p != '\0'; p++) {
    if (*p == '/') { *p = '\0'; (void)mkdir(dir, 0777); *p = '/'; }
  }
  (void)mkdir(dir, 0777);
  free(dir);
}
qa_app_player_progress_store *qa_app_player_progress_store_open(const char *file) {
  qa_app_player_progress_store *store = calloc(1, sizeof *store);
  if (store == NULL) qa_range_error("Out of memory");
  store->file = dup_text(file);
  FILE *in = fopen(file, "rb");
  if (in == NULL) {
    if (errno == ENOENT) return store;
    qa_range_error("%s", strerror(errno));
  }
  size_t capacity = 4096, length = 0;
  uint8_t *bytes = malloc(capacity);
  if (bytes == NULL) qa_range_error("Out of memory");
  for (;;) {
    if (length == capacity) {
      capacity *= 2;
      uint8_t *next = realloc(bytes, capacity);
      if (next == NULL) qa_range_error("Out of memory");
      bytes = next;
    }
    size_t read = fread(bytes + length, 1, capacity - length, in);
    length += read;
    if (read == 0) break;
  }
  fclose(in);
  qa_save_value *value = qa_parse_json_value(bytes, length, true);
  free(bytes);
  if (value == NULL) return store; /* torn text opens empty (OOM indistinguishable) */
  const qa_save_value *version = qa_save_value_get(value, "version");
  const qa_save_value *events = qa_save_value_get(value, "events");
  if (value->kind != QA_SAVE_OBJECT || version == NULL || version->kind != QA_SAVE_NUMBER || version->number != 1
      || events == NULL || events->kind != QA_SAVE_ARRAY) {
    qa_save_value_free(value);
    qa_range_error("Invalid player progress file");
  }
  for (size_t i = 0; i < events->item_count; i++) {
    const qa_save_value *item = events->items[i];
    if (item->kind != QA_SAVE_OBJECT) { qa_save_value_free(value); qa_range_error("Invalid player progress identity"); }
    const qa_save_value *score = qa_save_value_get(item, "score");
    qa_app_player_progress_event e = {str_field(item, "kind"), str_field(item, "source"),
        str_field(item, "participant"), str_field(item, "event"), str_field(item, "award"),
        str_field(item, "map"), score != NULL && score->kind == QA_SAVE_NUMBER ? score->number : NAN};
    validate(e);
    owned_event owned = own_event(e);
    for (size_t j = 0; j < store->count; j++) {
      if (same_identity(&store->events[j].view, &owned.view)) {
        qa_save_value_free(value);
        qa_range_error("Duplicate player progress event");
      }
    }
    push(store, owned);
  }
  qa_save_value_free(value);
  return store;
}
void qa_app_player_progress_store_destroy(qa_app_player_progress_store *store) {
  if (store == NULL) return;
  for (size_t i = 0; i < store->count; i++) free_owned(&store->events[i]);
  free(store->events); free(store->file); free(store);
}
size_t qa_app_player_progress_store_list(qa_app_player_progress_store *store,
    const char *participant, const qa_app_player_progress_event ***out) {
  size_t count = 0;
  for (size_t i = 0; i < store->count; i++)
    if (strcmp(store->events[i].view.participant, participant) == 0) count++;
  if (count == 0) { *out = NULL; return 0; }
  const qa_app_player_progress_event **items = malloc(count * sizeof *items);
  if (items == NULL) qa_range_error("Out of memory");
  size_t at = 0;
  for (size_t i = 0; i < store->count; i++)
    if (strcmp(store->events[i].view.participant, participant) == 0) items[at++] = &store->events[i].view;
  *out = items;
  return count;
}
bool qa_app_player_progress_store_record(qa_app_player_progress_store *store, qa_app_player_progress_event input) {
  validate(input);
  for (size_t i = 0; i < store->count; i++)
    if (same_identity(&store->events[i].view, &input)) return false;
  owned_event owned = own_event(input);
  mkdir_parent(store->file);
  size_t tlen = strlen(store->file) + strlen(".pending") + 1;
  char *temporary = malloc(tlen);
  if (temporary == NULL) qa_range_error("Out of memory");
  snprintf(temporary, tlen, "%s.pending", store->file);
  FILE *out = fopen(temporary, "wb");
  if (out == NULL) qa_range_error("%s", strerror(errno));
  (void)fchmod(fileno(out), 0600);
  write_events(out, store, &owned);
  if (fclose(out) != 0) qa_range_error("%s", strerror(errno));
  if (rename(temporary, store->file) != 0) qa_range_error("%s", strerror(errno));
  free(temporary);
  push(store, owned);
  return true;
}
void qa_app_player_progress_store_flush(qa_app_player_progress_store *store) { (void)store; }
char *qa_app_player_progress_identity(qa_app_player_progress_event event) {
  /* JSON.stringify([a,b,c]) with the same escaping as the writer. */
  size_t capacity = strlen(event.source) + strlen(event.participant) + strlen(event.event) + 32;
  char *buffer = NULL;
  size_t size = 0;
  FILE *mem = open_memstream(&buffer, &size);
  if (mem == NULL) qa_range_error("Out of memory");
  fputc('[', mem);
  append_json_string(mem, event.source); fputc(',', mem);
  append_json_string(mem, event.participant); fputc(',', mem);
  append_json_string(mem, event.event);
  fputc(']', mem);
  fclose(mem);
  (void)capacity;
  return buffer;
}
