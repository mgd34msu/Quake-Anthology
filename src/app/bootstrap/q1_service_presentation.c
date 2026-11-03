/* Port of src/app/bootstrap/q1-service-presentation.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/q1_service_presentation.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
#include "qa/materials/sky.h"
#include "qa/foundation/js.h"
#include "qa/network/common/js_number.h"
typedef struct row_entry {
  qa_app_q1_client_row row;
  char *name, *social, *player_info;
} row_entry;
struct qa_app_q1_client_table {
  row_entry *rows; size_t count, capacity;
  qa_app_q1_client_row *view; /* rebuilt on read */
};
static bool is(const char *a, const char *b) { return a != NULL && strcmp(a, b) == 0; }
static char *format_text(const char *format, ...) {
  va_list args;
  va_start(args, format);
  int length = vsnprintf(NULL, 0, format, args);
  va_end(args);
  if (length < 0) qa_range_error("Out of memory");
  char *out = malloc((size_t)length + 1);
  if (out == NULL) qa_range_error("Out of memory");
  va_start(args, format);
  vsnprintf(out, (size_t)length + 1, format, args);
  va_end(args);
  return out;
}
static char *dup_text(const char *text) {
  if (text == NULL) return NULL;
  size_t len = strlen(text);
  char *out = malloc(len + 1);
  if (out == NULL) qa_range_error("Out of memory");
  memcpy(out, text, len + 1);
  return out;
}
qa_app_q1_client_table *qa_app_q1_client_table_create(void) {
  qa_app_q1_client_table *table = calloc(1, sizeof *table);
  if (table == NULL) qa_range_error("Out of memory");
  return table;
}
void qa_app_q1_client_table_destroy(qa_app_q1_client_table *table) {
  if (table == NULL) return;
  for (size_t i = 0; i < table->count; i++) {
    free(table->rows[i].name); free(table->rows[i].social); free(table->rows[i].player_info);
  }
  free(table->rows); free(table->view); free(table);
}
static row_entry *row_for(qa_app_q1_client_table *table, double slot) {
  for (size_t i = 0; i < table->count; i++)
    if (table->rows[i].row.slot == slot) return &table->rows[i];
  if (table->count == table->capacity) {
    size_t cap = table->capacity == 0 ? 8 : table->capacity * 2;
    row_entry *next = realloc(table->rows, cap * sizeof *next);
    if (next == NULL) qa_range_error("Out of memory");
    table->rows = next; table->capacity = cap;
  }
  row_entry *row = &table->rows[table->count++];
  memset(row, 0, sizeof *row);
  row->row.slot = slot;
  row->name = dup_text("");
  row->row.name = row->name;
  return row;
}
static qa_app_q1_client_table *clone_table(const qa_app_q1_client_table *source) {
  qa_app_q1_client_table *table = qa_app_q1_client_table_create();
  for (size_t i = 0; i < source->count; i++) {
    row_entry *row = row_for(table, source->rows[i].row.slot);
    *row = source->rows[i];
    row->name = dup_text(source->rows[i].name);
    row->social = dup_text(source->rows[i].social);
    row->player_info = dup_text(source->rows[i].player_info);
    row->row.name = row->name;
    row->row.social = row->social;
    row->row.player_info = row->player_info;
  }
  return table;
}
void qa_app_update_q1_client_metadata(qa_app_q1_client_table *table, const qa_app_sim_q1_client_metadata_event *event) {
  row_entry *row = row_for(table, event->slot);
  if (is(event->kind, "name")) { free(row->name); row->name = dup_text(event->text); row->row.name = row->name; }
  else if (is(event->kind, "colors")) row->row.colors = event->value;
  else if (is(event->kind, "frags")) row->row.frags = event->value;
  else if (is(event->kind, "ping")) { row->row.has_ping = true; row->row.ping = event->value; }
  else if (is(event->kind, "social")) { free(row->social); row->social = dup_text(event->text); row->row.social = row->social; }
  else if (is(event->kind, "player-info")) { free(row->player_info); row->player_info = dup_text(event->text); row->row.player_info = row->player_info; }
}
size_t qa_app_q1_client_table_rows(const qa_app_q1_client_table *table, const qa_app_q1_client_row **out) {
  qa_app_q1_client_table *mutable_table = (qa_app_q1_client_table *)table;
  free(mutable_table->view);
  mutable_table->view = calloc(table->count == 0 ? 1 : table->count, sizeof *mutable_table->view);
  if (mutable_table->view == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < table->count; i++) mutable_table->view[i] = table->rows[i].row;
  *out = mutable_table->view;
  return table->count;
}
/* TS SkySelection. serial is the object identity: a TS spread clone
 * ({ ...sky }) is a new object, here a new serial. */
typedef struct sky_selection {
  double serial;
  char *content;
  char *name;
  bool ready;
  qa_q2_sky_view value;
  qa_renderer_image images[6];
  bool has_value;
} sky_selection;
typedef struct actor_sky { qa_actor_id actor; sky_selection *sky; } actor_sky;
typedef struct recipient_table { qa_actor_id actor; qa_app_q1_client_table *table; } recipient_table;
typedef struct content_tables {
  char *content;
  qa_app_q1_client_table *shared;
  recipient_table *recipients; size_t recipient_count, recipient_capacity;
} content_tables;
typedef struct retained_entry { char *key; qa_app_sim_presentation_event event; /* owned copy */ } retained_entry;
struct qa_app_q1_service_presentation {
  sky_selection *shared_sky;
  actor_sky *skies; size_t sky_count, sky_capacity;
  content_tables *tables; size_t table_count, table_capacity;
  retained_entry *retained; size_t retained_count, retained_capacity;
  double next_serial;
  bool closed;
};
static void free_sky(sky_selection *sky) {
  if (sky == NULL) return;
  free(sky->content); free(sky->name); free(sky);
}
static sky_selection *new_sky(qa_app_q1_service_presentation *presentation, qa_content_id content, const char *name) {
  sky_selection *sky = calloc(1, sizeof *sky);
  if (sky == NULL) qa_range_error("Out of memory");
  sky->serial = ++presentation->next_serial;
  sky->content = dup_text(content);
  sky->name = dup_text(name);
  return sky;
}
qa_app_q1_service_presentation *qa_app_q1_service_presentation_create(void) {
  qa_app_q1_service_presentation *presentation = calloc(1, sizeof *presentation);
  if (presentation == NULL) qa_range_error("Out of memory");
  return presentation;
}
static void clear_state(qa_app_q1_service_presentation *presentation, bool retained) {
  if (retained) {
    for (size_t i = 0; i < presentation->retained_count; i++) {
      free(presentation->retained[i].key);
      qa_app_sim_presentation_event_release(&presentation->retained[i].event);
    }
    presentation->retained_count = 0;
  }
  free_sky(presentation->shared_sky); presentation->shared_sky = NULL;
  for (size_t i = 0; i < presentation->sky_count; i++) free_sky(presentation->skies[i].sky);
  presentation->sky_count = 0;
  for (size_t i = 0; i < presentation->table_count; i++) {
    content_tables *tables = &presentation->tables[i];
    free(tables->content);
    qa_app_q1_client_table_destroy(tables->shared);
    for (size_t r = 0; r < tables->recipient_count; r++)
      qa_app_q1_client_table_destroy(tables->recipients[r].table);
    free(tables->recipients);
  }
  presentation->table_count = 0;
}
void qa_app_q1_service_presentation_destroy(qa_app_q1_service_presentation *presentation) {
  if (presentation == NULL) return;
  clear_state(presentation, true);
  free(presentation->skies); free(presentation->tables); free(presentation->retained);
  free(presentation);
}
static void set_actor_sky(qa_app_q1_service_presentation *presentation, qa_actor_id actor, sky_selection *sky) {
  for (size_t i = 0; i < presentation->sky_count; i++) {
    if (qa_same_actor(presentation->skies[i].actor, actor)) {
      free_sky(presentation->skies[i].sky);
      presentation->skies[i].sky = sky;
      return;
    }
  }
  if (presentation->sky_count == presentation->sky_capacity) {
    size_t cap = presentation->sky_capacity == 0 ? 4 : presentation->sky_capacity * 2;
    actor_sky *next = realloc(presentation->skies, cap * sizeof *next);
    if (next == NULL) qa_range_error("Out of memory");
    presentation->skies = next; presentation->sky_capacity = cap;
  }
  presentation->skies[presentation->sky_count].actor = actor;
  presentation->skies[presentation->sky_count].sky = sky;
  presentation->sky_count++;
}
static sky_selection *actor_sky_of(qa_app_q1_service_presentation *presentation, qa_actor_id actor) {
  for (size_t i = 0; i < presentation->sky_count; i++)
    if (qa_same_actor(presentation->skies[i].actor, actor)) return presentation->skies[i].sky;
  return NULL;
}
static content_tables *tables_for(qa_app_q1_service_presentation *presentation, qa_content_id content) {
  for (size_t i = 0; i < presentation->table_count; i++)
    if (strcmp(presentation->tables[i].content, content) == 0) return &presentation->tables[i];
  if (presentation->table_count == presentation->table_capacity) {
    size_t cap = presentation->table_capacity == 0 ? 4 : presentation->table_capacity * 2;
    content_tables *next = realloc(presentation->tables, cap * sizeof *next);
    if (next == NULL) qa_range_error("Out of memory");
    presentation->tables = next; presentation->table_capacity = cap;
  }
  content_tables *tables = &presentation->tables[presentation->table_count++];
  memset(tables, 0, sizeof *tables);
  tables->content = dup_text(content);
  tables->shared = qa_app_q1_client_table_create();
  return tables;
}
static qa_app_q1_client_table *recipient_table_for(content_tables *tables, qa_actor_id actor, bool create) {
  for (size_t i = 0; i < tables->recipient_count; i++)
    if (qa_same_actor(tables->recipients[i].actor, actor)) return tables->recipients[i].table;
  if (!create) return NULL;
  if (tables->recipient_count == tables->recipient_capacity) {
    size_t cap = tables->recipient_capacity == 0 ? 4 : tables->recipient_capacity * 2;
    recipient_table *next = realloc(tables->recipients, cap * sizeof *next);
    if (next == NULL) qa_range_error("Out of memory");
    tables->recipients = next; tables->recipient_capacity = cap;
  }
  recipient_table *entry = &tables->recipients[tables->recipient_count++];
  entry->actor = actor;
  entry->table = clone_table(tables->shared);
  return entry->table;
}
static void apply_events(qa_app_q1_service_presentation *presentation,
    const qa_app_sim_presentation_event *const *events, size_t count) {
  for (size_t i = 0; i < count; i++) {
    const qa_app_sim_presentation_event *source = events[i];
    if (is(source->kind, "q1-sky")) {
      sky_selection *value = new_sky(presentation, source->content, source->q1_sky.name);
      if (!source->has_recipient) {
        free_sky(presentation->shared_sky);
        presentation->shared_sky = value;
        for (size_t s = 0; s < presentation->sky_count; s++) free_sky(presentation->skies[s].sky);
        presentation->sky_count = 0;
      } else set_actor_sky(presentation, source->recipient, value);
    } else if (is(source->kind, "q1-client")) {
      content_tables *tables = tables_for(presentation, source->content);
      if (!source->has_recipient) {
        qa_app_update_q1_client_metadata(tables->shared, &source->q1_client);
        for (size_t r = 0; r < tables->recipient_count; r++)
          qa_app_update_q1_client_metadata(tables->recipients[r].table, &source->q1_client);
      } else {
        qa_app_q1_client_table *table = recipient_table_for(tables, source->recipient, true);
        qa_app_update_q1_client_metadata(table, &source->q1_client);
      }
    }
  }
}
static int compare_sequence(const void *a, const void *b) {
  double left = (*(const qa_app_sim_presentation_event *const *)a)->sequence;
  double right = (*(const qa_app_sim_presentation_event *const *)b)->sequence;
  return left < right ? -1 : left > right ? 1 : 0;
}
/* TS this.retained.set(key, source): a replaced key keeps its Map position. */
static void retain(qa_app_q1_service_presentation *presentation, const char *key, const qa_app_sim_presentation_event *event) {
  qa_app_sim_presentation_event copy;
  if (!qa_app_sim_presentation_event_copy(&copy, event)) qa_range_error("Out of memory");
  for (size_t i = 0; i < presentation->retained_count; i++) {
    if (strcmp(presentation->retained[i].key, key) == 0) {
      qa_app_sim_presentation_event_release(&presentation->retained[i].event);
      presentation->retained[i].event = copy;
      return;
    }
  }
  if (presentation->retained_count == presentation->retained_capacity) {
    size_t cap = presentation->retained_capacity == 0 ? 8 : presentation->retained_capacity * 2;
    retained_entry *next = realloc(presentation->retained, cap * sizeof *next);
    if (next == NULL) qa_range_error("Out of memory");
    presentation->retained = next; presentation->retained_capacity = cap;
  }
  retained_entry *entry = &presentation->retained[presentation->retained_count++];
  entry->key = dup_text(key);
  entry->event = copy;
}
void qa_app_q1_service_presentation_receive(qa_app_q1_service_presentation *presentation,
    const qa_app_sim_presentation_event *events, size_t event_count) {
  if (presentation->closed) qa_range_error("Q1 service presentation is closed");
  for (size_t i = 0; i < event_count; i++) {
    const qa_app_sim_presentation_event *source = &events[i];
    if (is(source->kind, "presentation-owner")) {
      if (is(source->presentation_owner.kind, "refreshed")) {
        /* TS replaces each selection with a spread clone: a new identity, so
         * a refresh prepared before this no longer publishes to it. */
        if (presentation->shared_sky != NULL) presentation->shared_sky->serial = ++presentation->next_serial;
        for (size_t s = 0; s < presentation->sky_count; s++) presentation->skies[s].sky->serial = ++presentation->next_serial;
        continue;
      }
      size_t kept = 0;
      for (size_t r = 0; r < presentation->retained_count; r++) {
        retained_entry *entry = &presentation->retained[r];
        bool same = qa_same_presentation_owner(entry->event.has_owner ? &entry->event.owner : NULL,
                                               source->presentation_owner.owner);
        if (same) { free(entry->key); qa_app_sim_presentation_event_release(&entry->event); }
        else presentation->retained[kept++] = *entry;
      }
      presentation->retained_count = kept;
      clear_state(presentation, false);
      const qa_app_sim_presentation_event **sorted = calloc(kept == 0 ? 1 : kept, sizeof *sorted);
      if (sorted == NULL) qa_range_error("Out of memory");
      for (size_t r = 0; r < kept; r++) sorted[r] = &presentation->retained[r].event;
      qa_js_sort(sorted, kept, sizeof *sorted, compare_sequence);
      apply_events(presentation, sorted, kept);
      free(sorted);
    } else if (is(source->kind, "q1-sky") || is(source->kind, "q1-client")) {
      char recipient[64];
      if (source->has_recipient)
        snprintf(recipient, sizeof recipient, "%lld:%lld", (long long)source->recipient.slot,
            (long long)source->recipient.generation);
      else snprintf(recipient, sizeof recipient, "world");
      char *key;
      if (is(source->kind, "q1-sky")) key = format_text("sky:%s", recipient);
      else key = format_text("%s:%s:%s:%s", source->content, QA_JS_NUMBER(source->q1_client.slot), source->q1_client.kind, recipient);
      retain(presentation, key, source);
      free(key);
      apply_events(presentation, &source, 1);
    }
  }
}
/* TS selections(): the shared sky then each actor sky. */
static sky_selection **collect_selections(qa_app_q1_service_presentation *presentation, size_t *out_count) {
  sky_selection **out = calloc(presentation->sky_count + 1, sizeof *out);
  if (out == NULL) qa_range_error("Out of memory");
  size_t count = 0;
  if (presentation->shared_sky != NULL) out[count++] = presentation->shared_sky;
  for (size_t i = 0; i < presentation->sky_count; i++) out[count++] = presentation->skies[i].sky;
  *out_count = count;
  return out;
}
/* The selection whose identity is serial, or NULL when TS
 * selections().includes(selection) is false. */
static sky_selection *current_selection(qa_app_q1_service_presentation *presentation, double serial) {
  if (presentation->shared_sky != NULL && presentation->shared_sky->serial == serial) return presentation->shared_sky;
  for (size_t i = 0; i < presentation->sky_count; i++)
    if (presentation->skies[i].sky->serial == serial) return presentation->skies[i].sky;
  return NULL;
}
/* TS load result: Sky | null. */
typedef struct loaded_sky { bool found; qa_renderer_image images[6]; } loaded_sky;
static void publish_sky(qa_app_q1_service_presentation *presentation, double serial, const loaded_sky *sky) {
  sky_selection *selection = current_selection(presentation, serial);
  if (presentation->closed || selection == NULL) return;
  selection->has_value = sky->found;
  if (sky->found) {
    memcpy(selection->images, sky->images, sizeof selection->images);
    selection->value.images = selection->images;
    selection->value.image_count = 6;
    selection->value.rotation = 0;
    selection->value.auto_rotate = false;
    selection->value.axis = (qa_vec3){0, 0, 1};
  }
  selection->ready = true;
}
static loaded_sky load_sky(const sky_selection *selection, const qa_app_q1_service_assets *assets) {
  loaded_sky sky;
  memset(&sky, 0, sizeof sky);
  if (selection->name[0] == '\0') return sky;
  for (size_t i = 0; i < 6; i++) {
    char *path = format_text("gfx/env/%s%s.tga", selection->name, qa_sky_face_suffixes[i]);
    qa_renderer_image image;
    bool loaded = assets->load_texture(assets->ctx, selection->content, path, &image);
    free(path);
    if (!loaded) {
      path = format_text("gfx/env/%s%s.png", selection->name, qa_sky_face_suffixes[i]);
      loaded = assets->load_texture(assets->ctx, selection->content, path, &image);
      free(path);
    }
    if (loaded) sky.found = true;
    sky.images[i] = loaded ? image : assets->missing_image(assets->ctx, selection->content);
  }
  return sky;
}
void qa_app_q1_service_presentation_prepare(qa_app_q1_service_presentation *presentation,
    const qa_app_q1_service_assets *assets) {
  if (presentation->closed) qa_range_error("Q1 service presentation is closed");
  size_t count = 0;
  sky_selection **selections = collect_selections(presentation, &count);
  double *serials = calloc(count + 1, sizeof *serials);
  if (serials == NULL) qa_range_error("Out of memory");
  size_t pending = 0;
  for (size_t i = 0; i < count; i++) if (!selections[i]->ready) serials[pending++] = selections[i]->serial;
  free(selections);
  for (size_t i = 0; i < pending; i++) {
    sky_selection *selection = current_selection(presentation, serials[i]);
    if (selection == NULL) continue;
    loaded_sky sky = load_sky(selection, assets);
    publish_sky(presentation, serials[i], &sky);
  }
  free(serials);
}
struct qa_app_q1_sky_refresh {
  qa_app_q1_service_presentation *presentation;
  double *serials;
  loaded_sky *skies;
  size_t count;
};
qa_app_q1_sky_refresh *qa_app_q1_service_presentation_prepare_image_refresh(
    qa_app_q1_service_presentation *presentation, const qa_app_q1_service_assets *assets) {
  if (presentation->closed) qa_range_error("Q1 service presentation is closed");
  qa_app_q1_sky_refresh *refresh = calloc(1, sizeof *refresh);
  if (refresh == NULL) qa_range_error("Out of memory");
  refresh->presentation = presentation;
  size_t count = 0;
  sky_selection **selections = collect_selections(presentation, &count);
  refresh->serials = calloc(count + 1, sizeof *refresh->serials);
  refresh->skies = calloc(count + 1, sizeof *refresh->skies);
  if (refresh->serials == NULL || refresh->skies == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < count; i++) {
    refresh->serials[i] = selections[i]->serial;
    refresh->skies[i] = load_sky(selections[i], assets);
  }
  free(selections);
  refresh->count = count;
  return refresh;
}
void qa_app_q1_sky_refresh_apply(qa_app_q1_sky_refresh *refresh) {
  for (size_t i = 0; i < refresh->count; i++)
    publish_sky(refresh->presentation, refresh->serials[i], &refresh->skies[i]);
}
void qa_app_q1_sky_refresh_destroy(qa_app_q1_sky_refresh *refresh) {
  if (refresh == NULL) return;
  free(refresh->serials); free(refresh->skies); free(refresh);
}
const qa_q2_sky_view *qa_app_q1_service_presentation_view(qa_app_q1_service_presentation *presentation,
    qa_actor_id actor) {
  sky_selection *value = actor_sky_of(presentation, actor);
  if (value == NULL) value = presentation->shared_sky;
  if (value == NULL || !value->has_value) return NULL;
  return &value->value;
}
static int compare_rows(const void *a, const void *b) {
  double left = ((const qa_app_q1_client_row *)a)->slot;
  double right = ((const qa_app_q1_client_row *)b)->slot;
  return left < right ? -1 : left > right ? 1 : 0;
}
size_t qa_app_q1_service_presentation_clients(qa_app_q1_service_presentation *presentation,
    qa_content_id content, qa_actor_id actor, qa_app_q1_client_row *out, size_t capacity) {
  content_tables *tables = NULL;
  for (size_t i = 0; i < presentation->table_count; i++)
    if (strcmp(presentation->tables[i].content, content) == 0) tables = &presentation->tables[i];
  if (tables == NULL) return 0;
  qa_app_q1_client_table *table = recipient_table_for(tables, actor, false);
  if (table == NULL) table = tables->shared;
  const qa_app_q1_client_row *rows = NULL;
  size_t count = qa_app_q1_client_table_rows(table, &rows);
  size_t written = count < capacity ? count : capacity;
  memcpy(out, rows, written * sizeof *out);
  qa_js_sort(out, written, sizeof *out, compare_rows);
  return written;
}
void qa_app_q1_service_presentation_retire(qa_app_q1_service_presentation *presentation, qa_actor_id actor) {
  size_t kept = 0;
  for (size_t i = 0; i < presentation->sky_count; i++) {
    if (qa_same_actor(presentation->skies[i].actor, actor)) free_sky(presentation->skies[i].sky);
    else presentation->skies[kept++] = presentation->skies[i];
  }
  presentation->sky_count = kept;
  for (size_t t = 0; t < presentation->table_count; t++) {
    content_tables *tables = &presentation->tables[t];
    size_t kept_recipients = 0;
    for (size_t r = 0; r < tables->recipient_count; r++) {
      if (qa_same_actor(tables->recipients[r].actor, actor))
        qa_app_q1_client_table_destroy(tables->recipients[r].table);
      else tables->recipients[kept_recipients++] = tables->recipients[r];
    }
    tables->recipient_count = kept_recipients;
  }
  size_t kept_retained = 0;
  for (size_t i = 0; i < presentation->retained_count; i++) {
    retained_entry *entry = &presentation->retained[i];
    if (entry->event.has_recipient && qa_same_actor(entry->event.recipient, actor)) {
      free(entry->key); qa_app_sim_presentation_event_release(&entry->event);
    } else presentation->retained[kept_retained++] = *entry;
  }
  presentation->retained_count = kept_retained;
}
void qa_app_q1_service_presentation_reset(qa_app_q1_service_presentation *presentation) {
  clear_state(presentation, true);
}
void qa_app_q1_service_presentation_close(qa_app_q1_service_presentation *presentation) {
  qa_app_q1_service_presentation_reset(presentation);
  presentation->closed = true;
}
