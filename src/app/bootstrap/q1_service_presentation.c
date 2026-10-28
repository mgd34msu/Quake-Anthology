/* Port of src/app/bootstrap/q1-service-presentation.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/q1_service_presentation.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
#include "qa/materials/sky.h"
typedef struct row_entry {
  qa_app_q1_client_row row;
  char *name, *social, *player_info;
} row_entry;
struct qa_app_q1_client_table {
  row_entry *rows; size_t count, capacity;
  qa_app_q1_client_row *view; /* rebuilt on read */
};
static bool is(const char *a, const char *b) { return a != NULL && strcmp(a, b) == 0; }
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
void qa_app_update_q1_client_metadata(qa_app_q1_client_table *table, qa_app_q1_client_metadata_event event) {
  row_entry *row = row_for(table, event.slot);
  if (is(event.kind, "name")) { free(row->name); row->name = dup_text(event.text_value); row->row.name = row->name; }
  else if (is(event.kind, "colors")) row->row.colors = event.number_value;
  else if (is(event.kind, "frags")) row->row.frags = event.number_value;
  else if (is(event.kind, "ping")) { row->row.has_ping = true; row->row.ping = event.number_value; }
  else if (is(event.kind, "social")) { free(row->social); row->social = dup_text(event.text_value); row->row.social = row->social; }
  else if (is(event.kind, "player-info")) { free(row->player_info); row->player_info = dup_text(event.text_value); row->row.player_info = row->player_info; }
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
typedef struct sky_selection {
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
typedef struct retained_entry { char *key; qa_app_q1_service_event event; char *sky_name; char *client_text; } retained_entry;
struct qa_app_q1_service_presentation {
  sky_selection *shared_sky;
  actor_sky *skies; size_t sky_count, sky_capacity;
  content_tables *tables; size_t table_count, table_capacity;
  retained_entry *retained; size_t retained_count, retained_capacity;
  bool closed;
};
static void free_sky(sky_selection *sky) {
  if (sky == NULL) return;
  free(sky->content); free(sky->name); free(sky);
}
static sky_selection *new_sky(qa_content_id content, const char *name) {
  sky_selection *sky = calloc(1, sizeof *sky);
  if (sky == NULL) qa_range_error("Out of memory");
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
      free(presentation->retained[i].sky_name);
      free(presentation->retained[i].client_text);
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
    const qa_app_q1_service_event *events, size_t count) {
  for (size_t i = 0; i < count; i++) {
    const qa_app_q1_service_event *source = &events[i];
    if (is(source->kind, "q1-sky")) {
      sky_selection *value = new_sky(source->content, source->sky_name);
      if (!source->has_recipient) {
        free_sky(presentation->shared_sky);
        presentation->shared_sky = value;
        for (size_t s = 0; s < presentation->sky_count; s++) free_sky(presentation->skies[s].sky);
        presentation->sky_count = 0;
      } else set_actor_sky(presentation, source->recipient, value);
    } else if (is(source->kind, "q1-client")) {
      content_tables *tables = tables_for(presentation, source->content);
      if (!source->has_recipient) {
        qa_app_update_q1_client_metadata(tables->shared, source->client);
        for (size_t r = 0; r < tables->recipient_count; r++)
          qa_app_update_q1_client_metadata(tables->recipients[r].table, source->client);
      } else {
        qa_app_q1_client_table *table = recipient_table_for(tables, source->recipient, true);
        qa_app_update_q1_client_metadata(table, source->client);
      }
    }
  }
}
static int compare_sequence(const void *a, const void *b) {
  double left = ((const qa_app_q1_service_event *)a)->sequence;
  double right = ((const qa_app_q1_service_event *)b)->sequence;
  return left < right ? -1 : left > right ? 1 : 0;
}
static void retain(qa_app_q1_service_presentation *presentation, const char *key, qa_app_q1_service_event event) {
  for (size_t i = 0; i < presentation->retained_count; i++) {
    if (strcmp(presentation->retained[i].key, key) == 0) {
      free(presentation->retained[i].sky_name);
      free(presentation->retained[i].client_text);
      presentation->retained[i].event = event;
      presentation->retained[i].sky_name = dup_text(event.sky_name);
      presentation->retained[i].client_text = dup_text(event.client.text_value);
      presentation->retained[i].event.sky_name = presentation->retained[i].sky_name;
      presentation->retained[i].event.client.text_value = presentation->retained[i].client_text;
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
  entry->event = event;
  entry->sky_name = dup_text(event.sky_name);
  entry->client_text = dup_text(event.client.text_value);
  entry->event.sky_name = entry->sky_name;
  entry->event.client.text_value = entry->client_text;
}
void qa_app_q1_service_presentation_receive(qa_app_q1_service_presentation *presentation,
    const qa_app_q1_service_event *events, size_t event_count) {
  if (presentation->closed) qa_range_error("Q1 service presentation is closed");
  for (size_t i = 0; i < event_count; i++) {
    const qa_app_q1_service_event *source = &events[i];
    if (is(source->kind, "presentation-owner")) {
      if (is(source->event_kind, "refreshed")) {
        /* TS clones each selection object (identity refresh only). */
        continue;
      }
      size_t kept = 0;
      for (size_t r = 0; r < presentation->retained_count; r++) {
        retained_entry *entry = &presentation->retained[r];
        bool same = entry->event.has_owner
            && qa_same_presentation_owner(&entry->event.owner, source->owner);
        if (same) { free(entry->key); free(entry->sky_name); free(entry->client_text); }
        else presentation->retained[kept++] = *entry;
      }
      presentation->retained_count = kept;
      clear_state(presentation, false);
      qa_app_q1_service_event *sorted = calloc(kept == 0 ? 1 : kept, sizeof *sorted);
      if (sorted == NULL) qa_range_error("Out of memory");
      for (size_t r = 0; r < kept; r++) sorted[r] = presentation->retained[r].event;
      qsort(sorted, kept, sizeof *sorted, compare_sequence);
      apply_events(presentation, sorted, kept);
      free(sorted);
    } else if (is(source->kind, "q1-sky") || is(source->kind, "q1-client")) {
      char recipient[64];
      if (source->has_recipient)
        snprintf(recipient, sizeof recipient, "%lld:%lld", (long long)source->recipient.slot,
            (long long)source->recipient.generation);
      else snprintf(recipient, sizeof recipient, "world");
      char key[1024];
      if (is(source->kind, "q1-sky")) snprintf(key, sizeof key, "sky:%s", recipient);
      else snprintf(key, sizeof key, "%s:%lld:%s:%s", source->content,
          (long long)source->client.slot, source->client.kind, recipient);
      retain(presentation, key, *source);
      apply_events(presentation, source, 1);
    }
  }
}
static size_t collect_selections(qa_app_q1_service_presentation *presentation, sky_selection **out) {
  size_t count = 0;
  if (presentation->shared_sky != NULL) out[count++] = presentation->shared_sky;
  for (size_t i = 0; i < presentation->sky_count; i++) out[count++] = presentation->skies[i].sky;
  return count;
}
static bool selection_included(qa_app_q1_service_presentation *presentation, sky_selection *selection) {
  if (presentation->shared_sky == selection) return true;
  for (size_t i = 0; i < presentation->sky_count; i++)
    if (presentation->skies[i].sky == selection) return true;
  return false;
}
static void publish_sky(qa_app_q1_service_presentation *presentation, sky_selection *selection,
    bool has_value) {
  if (presentation->closed || !selection_included(presentation, selection)) return;
  selection->has_value = has_value;
  if (has_value) {
    selection->value.images = selection->images;
    selection->value.image_count = 6;
    selection->value.rotation = 0;
    selection->value.auto_rotate = false;
    selection->value.axis = (qa_vec3){0, 0, 1};
  }
  selection->ready = true;
}
static bool load_sky(sky_selection *selection, const qa_app_q1_service_assets *assets) {
  if (selection->name[0] == '\0') return false;
  bool found = false;
  for (size_t i = 0; i < 6; i++) {
    char path[1024];
    snprintf(path, sizeof path, "gfx/env/%s%s.tga", selection->name, qa_sky_face_suffixes[i]);
    qa_renderer_image image;
    bool loaded = assets->load_texture(assets->ctx, selection->content, path, &image);
    if (!loaded) {
      snprintf(path, sizeof path, "gfx/env/%s%s.png", selection->name, qa_sky_face_suffixes[i]);
      loaded = assets->load_texture(assets->ctx, selection->content, path, &image);
    }
    if (loaded) found = true;
    selection->images[i] = loaded ? image : assets->missing_image(assets->ctx, selection->content);
  }
  return found;
}
void qa_app_q1_service_presentation_prepare(qa_app_q1_service_presentation *presentation,
    const qa_app_q1_service_assets *assets) {
  if (presentation->closed) qa_range_error("Q1 service presentation is closed");
  sky_selection *selections[64];
  size_t count = collect_selections(presentation, selections);
  for (size_t i = 0; i < count && i < 64; i++) {
    if (selections[i]->ready) continue;
    bool found = load_sky(selections[i], assets);
    publish_sky(presentation, selections[i], found);
  }
}
struct qa_app_q1_sky_refresh {
  qa_app_q1_service_presentation *presentation;
  sky_selection **selections;
  bool *found;
  size_t count;
};
qa_app_q1_sky_refresh *qa_app_q1_service_presentation_prepare_image_refresh(
    qa_app_q1_service_presentation *presentation, const qa_app_q1_service_assets *assets) {
  if (presentation->closed) qa_range_error("Q1 service presentation is closed");
  qa_app_q1_sky_refresh *refresh = calloc(1, sizeof *refresh);
  if (refresh == NULL) qa_range_error("Out of memory");
  refresh->presentation = presentation;
  sky_selection *selections[64];
  size_t count = collect_selections(presentation, selections);
  if (count > 64) count = 64;
  refresh->selections = calloc(count == 0 ? 1 : count, sizeof *refresh->selections);
  refresh->found = calloc(count == 0 ? 1 : count, sizeof *refresh->found);
  if (refresh->selections == NULL || refresh->found == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < count; i++) {
    refresh->selections[i] = selections[i];
    refresh->found[i] = load_sky(selections[i], assets);
  }
  refresh->count = count;
  return refresh;
}
void qa_app_q1_sky_refresh_apply(qa_app_q1_sky_refresh *refresh) {
  for (size_t i = 0; i < refresh->count; i++)
    publish_sky(refresh->presentation, refresh->selections[i], refresh->found[i]);
}
void qa_app_q1_sky_refresh_destroy(qa_app_q1_sky_refresh *refresh) {
  if (refresh == NULL) return;
  free(refresh->selections); free(refresh->found); free(refresh);
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
  qsort(out, written, sizeof *out, compare_rows);
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
      free(entry->key); free(entry->sky_name); free(entry->client_text);
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
