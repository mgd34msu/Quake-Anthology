/* Port of src/app/bootstrap/player-progress-library.ts. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "qa/app/bootstrap/player_progress_library.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
typedef struct owned_row { qa_ui_library_entry view; char *id, *label, *detail; } owned_row;
struct qa_app_player_progress_library {
  qa_app_progress_store_fn store_fn; void *store_ctx;
  qa_app_progress_participant_fn participant_fn; void *participant_ctx;
  owned_row *rows; size_t row_count;
  char *message;
  double generation;
};
static char *dup_text(const char *text) {
  if (text == NULL) return NULL;
  size_t len = strlen(text);
  char *out = malloc(len + 1);
  if (out == NULL) qa_range_error("Out of memory");
  memcpy(out, text, len + 1);
  return out;
}
static char *format2(const char *format, const char *a, const char *b) {
  char *buffer = NULL; size_t size = 0;
  FILE *mem = open_memstream(&buffer, &size);
  if (mem == NULL) qa_range_error("Out of memory");
  fprintf(mem, format, a, b);
  fclose(mem);
  return buffer;
}
static void clear_rows(qa_app_player_progress_library *library) {
  for (size_t i = 0; i < library->row_count; i++) {
    free(library->rows[i].id); free(library->rows[i].label); free(library->rows[i].detail);
  }
  free(library->rows); library->rows = NULL; library->row_count = 0;
}
static void set_message(qa_app_player_progress_library *library, char *message) {
  free(library->message); library->message = message;
}
qa_app_player_progress_library *qa_app_player_progress_library_create(
    qa_app_progress_store_fn store, void *store_ctx,
    qa_app_progress_participant_fn participant, void *participant_ctx) {
  qa_app_player_progress_library *library = calloc(1, sizeof *library);
  if (library == NULL) qa_range_error("Out of memory");
  library->store_fn = store; library->store_ctx = store_ctx;
  library->participant_fn = participant; library->participant_ctx = participant_ctx;
  library->message = dup_text("");
  return library;
}
void qa_app_player_progress_library_destroy(qa_app_player_progress_library *library) {
  if (library == NULL) return;
  clear_rows(library); free(library->message); free(library);
}
const qa_ui_library_entry *qa_app_player_progress_library_entries(
    qa_app_player_progress_library *library, size_t *out_count) {
  *out_count = library->row_count;
  return library->row_count == 0 ? NULL : &library->rows[0].view;
}
const char *qa_app_player_progress_library_status(qa_app_player_progress_library *library) {
  return library->message;
}
static owned_row entry_of(const qa_app_player_progress_event *event) {
  owned_row row = {0};
  row.id = qa_app_player_progress_identity(*event);
  char family[8];
  size_t len = strlen(event->source);
  if (len >= sizeof family) len = sizeof family - 1;
  for (size_t i = 0; i < len; i++) family[i] = (char)toupper((unsigned char)event->source[i]);
  family[len] = '\0';
  if (strcmp(event->kind, "achievement") == 0) {
    row.label = dup_text(event->award);
    row.detail = format2("%s · Achievement earned", family, "");
    /* trim the helper's trailing space usage: format has no %s for b beyond one */
  } else if (strcmp(event->kind, "level-completed") == 0) {
    row.label = dup_text(event->map);
    row.detail = format2("%s · Level completed", family, "");
  } else {
    row.label = dup_text(event->map);
    char score[64];
    if (event->score == (double)(long long)event->score)
      snprintf(score, sizeof score, "%lld", (long long)event->score);
    else snprintf(score, sizeof score, "%g", event->score);
    row.detail = format2("%s · Match completed · Score %s", family, score);
  }
  row.view.id = row.id; row.view.label = row.label; row.view.detail = row.detail; row.view.unavailable = NULL;
  return row;
}
void qa_app_player_progress_library_load(qa_app_player_progress_library *library) {
  double generation = ++library->generation;
  const char *participant = library->participant_fn(library->participant_ctx);
  char *participant_copy = dup_text(participant);
  clear_rows(library);
  set_message(library, dup_text("Loading progress..."));
  const char *error = NULL;
  qa_app_player_progress_store *store = library->store_fn(library->store_ctx, &error);
  if (generation != library->generation || strcmp(participant_copy, library->participant_fn(library->participant_ctx)) != 0) {
    free(participant_copy); return;
  }
  free(participant_copy);
  if (store == NULL) {
    set_message(library, format2("Cannot read progress: %s", error != NULL ? error : "", ""));
    return;
  }
  const qa_app_player_progress_event **events = NULL;
  size_t count = qa_app_player_progress_store_list(store, participant, &events);
  if (count > 0) {
    library->rows = calloc(count, sizeof *library->rows);
    if (library->rows == NULL) qa_range_error("Out of memory");
    for (size_t i = 0; i < count; i++) library->rows[i] = entry_of(events[i]);
    library->row_count = count;
  }
  free((void *)events);
  if (library->row_count == 0)
    set_message(library, dup_text("No recorded achievements or completed levels for this player."));
  else {
    char *buffer = NULL; size_t size = 0;
    FILE *mem = open_memstream(&buffer, &size);
    if (mem == NULL) qa_range_error("Out of memory");
    fprintf(mem, "%zu progress records", library->row_count);
    fclose(mem);
    set_message(library, buffer);
  }
}
void qa_app_player_progress_library_refresh(qa_app_player_progress_library *library) {
  qa_app_player_progress_library_load(library);
}
void qa_app_player_progress_library_activate(qa_app_player_progress_library *library, const char *id) {
  for (size_t i = 0; i < library->row_count; i++) {
    if (strcmp(library->rows[i].view.id, id) == 0) {
      set_message(library, format2("%s: %s", library->rows[i].view.label,
          library->rows[i].view.detail != NULL ? library->rows[i].view.detail : ""));
      return;
    }
  }
}
static const qa_ui_library_entry *svc_entries(void *ctx, size_t *out_count) {
  return qa_app_player_progress_library_entries(ctx, out_count);
}
static const char *svc_status(void *ctx) { return qa_app_player_progress_library_status(ctx); }
static void svc_refresh(void *ctx) { qa_app_player_progress_library_refresh(ctx); }
static void svc_activate(void *ctx, const char *id) { qa_app_player_progress_library_activate(ctx, id); }
qa_ui_library_menu_service qa_app_player_progress_library_as_service(qa_app_player_progress_library *library) {
  qa_ui_library_menu_service service = {0};
  service.ctx = library;
  service.entries = svc_entries;
  service.status = svc_status;
  service.refresh = svc_refresh;
  service.activate = svc_activate;
  return service;
}
