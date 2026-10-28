/* Port of src/app/bootstrap/q3-client/cinematics.ts.
 * GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "qa/app/bootstrap/q3_client/cinematics.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
#include "qa/media/presentation.h"
#include "qa/media/playback.h"
#include "qa/media/still.h"
#include "qa/persistence/recipe.h"
static int next_consumer = 0;
typedef struct cinematic_mode { bool loop, hold, silent, shader; qa_rect rect; } cinematic_mode;
typedef struct source_entry {
  char *path;
  qa_cinematic_source source;
  uint8_t *bytes; /* owned backing for bytes-backed sources */
  char *name;
  qa_resolved_resource_reference resource;
  bool has_resource;
} source_entry;
typedef struct movie_entry {
  bool used;
  qa_cinematic_playback playback;
  qa_cinematic_audio_binding audio;
  char *audio_id;
  void *image;
  qa_picture_asset picture;
  char *path;      /* prepared.source.source */
  char *asset_path;
  cinematic_mode mode;
  qa_rect rect;
} movie_entry;
struct qa_app_application_q3_cinematics {
  qa_content_mounted *mounts;
  qa_app_q3_cinematics_host host;
  void (*print)(void *ctx, const char *text); void *print_ctx;
  qa_cinematic_mixer mixer;
  qa_seat_id seat;
  double (*now)(void *ctx); void *now_ctx;
  bool has_system;
  qa_app_system_cinematic_host system;
  source_entry *sources; size_t source_count, source_capacity;
  bool closed, restoring;
  int pending_loads;
  bool systems_used[16];
  qa_app_system_cinematic_handle systems[16];
  bool pending_systems[16];
  char namespace_[64];
  movie_entry movies[16];
  bool movies_used[16];
};
static char *xstrdup(const char *s) { char *p = strdup(s != NULL ? s : ""); if (p == NULL) qa_range_error("Out of memory"); return p; }
static source_entry *find_source(qa_app_application_q3_cinematics *c, const char *path) {
  for (size_t i = 0; i < c->source_count; i++)
    if (strcmp(c->sources[i].path, path) == 0) return &c->sources[i];
  return NULL;
}
static double now_sample(void *ctx) {
  qa_app_application_q3_cinematics *c = ctx;
  return c->now(c->now_ctx);
}
static void dev_print(void *ctx, const char *text) {
  qa_app_application_q3_cinematics *c = ctx;
  c->print(c->print_ctx, text);
}
static bool slot_free(qa_app_application_q3_cinematics *c, int index) {
  return !c->movies_used[index] && !c->systems_used[index] && !c->pending_systems[index];
}
qa_app_application_q3_cinematics *qa_app_application_q3_cinematics_create(
    qa_content_mounted *mounts, qa_app_q3_cinematics_host host,
    void (*print)(void *ctx, const char *text), void *print_ctx,
    qa_cinematic_mixer mixer, qa_seat_id seat,
    double (*now)(void *ctx), void *now_ctx,
    const qa_app_system_cinematic_host *system) {
  qa_app_application_q3_cinematics *c = calloc(1, sizeof *c);
  if (c == NULL) qa_range_error("Out of memory");
  c->mounts = mounts;
  c->host = host;
  c->print = print;
  c->print_ctx = print_ctx;
  c->mixer = mixer;
  c->seat = seat;
  c->now = now;
  c->now_ctx = now_ctx;
  if (system != NULL) { c->has_system = true; c->system = *system; }
  snprintf(c->namespace_, sizeof c->namespace_, "q3-cinematic:%d", ++next_consumer);
  return c;
}
void qa_app_application_q3_cinematics_destroy(qa_app_application_q3_cinematics *cinematics) {
  if (cinematics == NULL) return;
  qa_app_application_q3_cinematics_close(cinematics);
  free(cinematics->sources);
  free(cinematics);
}
qa_seat_id qa_app_application_q3_cinematics_seat(const qa_app_application_q3_cinematics *c) { return c->seat; }
static bool prepare_mode(qa_app_application_q3_cinematics *c, const char *path, bool restoring,
                         qa_app_ui_cinematic_asset *out) {
  if (c->restoring && !restoring) qa_range_error("Cinematic restore has not completed");
  if (c->closed) qa_range_error("UI cinematics are closed");
  if (find_source(c, path) == NULL) {
    char selected[1024];
    if (strchr(path, '/') != NULL) snprintf(selected, sizeof selected, "%s", path);
    else snprintf(selected, sizeof selected, "video/%s", path);
    const char *base = strrchr(selected, '/');
    bool has_ext = base != NULL && strchr(base, '.') != NULL;
    char name[1080];
    if (has_ext) snprintf(name, sizeof name, "%s", selected);
    else snprintf(name, sizeof name, "%s.roq", selected);
    c->pending_loads++;
    qa_content_opened_resource opened;
    bool found = qa_content_mounted_open(c->mounts, name, NULL, NULL, &opened);
    c->pending_loads--;
    if (c->closed) { if (found) free(opened.bytes); qa_range_error("UI cinematic loaded after close"); }
    if (!found || opened.byte_length == 0) {
      if (found) free(opened.bytes);
      return false; /* MissingCinematicError: Missing cinematic <name> */
    }
    const char *dot = strrchr(name, '.');
    char ext[16] = "";
    if (dot != NULL) {
      size_t i = 0;
      for (const char *p = dot + 1; *p != '\0' && i < sizeof ext - 1; p++) ext[i++] = (char)tolower((unsigned char)*p);
      ext[i] = '\0';
    }
    bool supported = strcmp(ext, "roq") == 0 || strcmp(ext, "cin") == 0
      || strcmp(ext, "ogv") == 0 || strcmp(ext, "pcx") == 0;
    if (!supported) { free(opened.bytes); return false; }
    if (c->source_count == c->source_capacity) {
      size_t cap = c->source_capacity == 0 ? 8 : c->source_capacity * 2;
      source_entry *next = realloc(c->sources, cap * sizeof *next);
      if (next == NULL) qa_range_error("Out of memory");
      c->sources = next; c->source_capacity = cap;
    }
    source_entry *entry = &c->sources[c->source_count];
    entry->path = xstrdup(path);
    entry->bytes = opened.bytes;
    entry->name = xstrdup(name);
    bool ok;
    if (strcmp(ext, "pcx") == 0) {
      ok = qa_cinematic_pcx(opened.bytes, opened.byte_length, entry->name, &entry->source);
    } else {
      qa_cinematic_format format = strcmp(ext, "roq") == 0 ? QA_CINEMATIC_FORMAT_ROQ
        : strcmp(ext, "cin") == 0 ? QA_CINEMATIC_FORMAT_CIN : QA_CINEMATIC_FORMAT_OGV;
      entry->source = qa_cinematic_bytes(format, opened.bytes, opened.byte_length, entry->name);
      ok = true;
      /* Decode probe mirrors the TS cinematicBytes throw: dimensions
       * are read when a movie is created in TS too, but the byte
       * constructors validate headers eagerly in the landed C
       * decoders through qa_cinematic_dimensions. */
      int64_t w = 0, h = 0;
      if (!qa_cinematic_dimensions(&entry->source, &w, &h)) ok = false;
      (void)w; (void)h;
    }
    if (!ok) {
      free(entry->path); free(entry->name); free(entry->bytes);
      if (c->closed) qa_range_error("UI cinematic loaded after close");
      return false; /* MissingCinematicError: Undecodable cinematic */
    }
    if (opened.reference != NULL) { entry->resource = *opened.reference; entry->has_resource = true; }
    c->source_count++;
  }
  out->path = path;
  return true;
}
bool qa_app_application_q3_cinematics_prepare(qa_app_application_q3_cinematics *c, const char *path,
                                              qa_app_ui_cinematic_asset *out) {
  return prepare_mode(c, path, false, out);
}
static void create_movie(qa_app_application_q3_cinematics *c, int index, qa_app_ui_cinematic_asset asset,
                         cinematic_mode mode, const qa_save_value *checkpoint) {
  source_entry *prepared = find_source(c, asset.path);
  if (prepared == NULL) qa_range_error("Unprepared cinematic %s", asset.path);
  int64_t width = 0, height = 0;
  qa_cinematic_dimensions(&prepared->source, &width, &height);
  char id[96];
  snprintf(id, sizeof id, "%s:%d", c->namespace_, index);
  movie_entry *movie = &c->movies[index];
  memset(movie, 0, sizeof *movie);
  movie->audio_id = xstrdup(id);
  movie->audio = qa_cinematic_audio_bind(c->mixer, movie->audio_id, 1);
  qa_cinematic_options options;
  memset(&options, 0, sizeof options);
  options.clock.ctx = c;
  options.clock.sample = now_sample;
  options.target.kind = mode.shader ? QA_CINEMATIC_TARGET_MATERIAL : QA_CINEMATIC_TARGET_SEAT;
  options.target.seat = c->seat;
  options.target.id = movie->audio_id;
  options.loop = mode.loop;
  options.hold = mode.hold;
  options.silent = mode.silent;
  qa_cinematic_audio_apply(&movie->audio, &options);
  /* TS onAudio wraps the target as this seat; the landed binding
   * already targets its stream id. onComplete is a no-op. */
  options.developer_print_ctx = c;
  options.developer_print = dev_print;
  if (!qa_cinematic_playback_init(&movie->playback, &prepared->source, &options, checkpoint))
    qa_range_error("Out of memory");
  movie->image = c->host.image_allocate(c->host.ctx, (double)width, (double)height, prepared->resource);
  movie->picture = c->host.make_picture(c->host.ctx, movie->audio_id, movie->image);
  movie->path = xstrdup(prepared->source.source != NULL ? prepared->source.source : "");
  movie->asset_path = xstrdup(asset.path);
  movie->mode = mode;
  movie->rect = mode.rect;
  movie->used = true;
  c->movies_used[index] = true;
}
static bool play_mode(qa_app_application_q3_cinematics *c, qa_app_ui_cinematic_asset asset,
                      cinematic_mode mode, double *out_index) {
  if (c->restoring) qa_range_error("Cinematic restore has not completed");
  if (c->closed) qa_range_error("UI cinematics are closed");
  if (find_source(c, asset.path) == NULL) qa_range_error("Unprepared cinematic %s", asset.path);
  int index = 0;
  while (index < 16 && !slot_free(c, index)) index++;
  if (index >= 16) return false;
  create_movie(c, index, asset, mode, NULL);
  *out_index = index;
  return true;
}
bool qa_app_application_q3_cinematics_play(qa_app_application_q3_cinematics *c,
    qa_app_ui_cinematic_asset asset, qa_rect rect, double *out_handle_index) {
  return play_mode(c, asset, (cinematic_mode){true, false, true, false, rect}, out_handle_index);
}
static qa_save_value *rect_value(qa_rect rect) {
  qa_save_value *o = qa_save_value_object();
  qa_save_value_set(o, "x", qa_save_value_number(rect.x));
  qa_save_value_set(o, "y", qa_save_value_number(rect.y));
  qa_save_value_set(o, "width", qa_save_value_number(rect.width));
  qa_save_value_set(o, "height", qa_save_value_number(rect.height));
  return o;
}
qa_save_value *qa_app_application_q3_cinematics_capture_checkpoint(qa_app_application_q3_cinematics *c) {
  bool any_pending = false;
  for (int i = 0; i < 16; i++) if (c->pending_systems[i]) any_pending = true;
  if (c->closed || c->restoring || c->pending_loads != 0 || any_pending)
    qa_range_error("Cinematic checkpoint requires an idle live owner");
  bool any_movie = false;
  for (int i = 0; i < 16; i++) if (c->movies_used[i]) any_movie = true;
  if (c->mixer.capture_stream_checkpoint == NULL && any_movie)
    qa_range_error("Cinematic mixer cannot checkpoint its owned PCM lanes");
  qa_save_value *root = qa_save_value_object();
  qa_save_value_set(root, "version", qa_save_value_number(1));
  qa_save_value *systems = qa_save_value_array();
  for (int i = 0; i < 16; i++) {
    if (!c->systems_used[i]) continue;
    qa_app_system_cinematic_handle *system = &c->systems[i];
    if (system->capture_checkpoint == NULL)
      qa_range_error("System cinematic checkpoint requires its attached client transition checkpoint");
    qa_save_value *entry = qa_save_value_object();
    qa_save_value_set(entry, "index", qa_save_value_number(i));
    qa_save_value_set(entry, "checkpoint", system->capture_checkpoint(system->ctx));
    qa_save_value_push(systems, entry);
  }
  qa_save_value_set(root, "systems", systems);
  qa_save_value *sources = qa_save_value_array();
  for (size_t i = 0; i < c->source_count; i++) {
    qa_save_value *entry = qa_save_value_object();
    qa_save_value_set(entry, "path", qa_save_value_string(c->sources[i].path));
    qa_save_value_set(entry, "resource", c->host.write_resource(c->host.ctx, c->sources[i].resource));
    qa_save_value_push(sources, entry);
  }
  qa_save_value_set(root, "sources", sources);
  qa_save_value *movies = qa_save_value_array();
  for (int i = 0; i < 16; i++) {
    if (!c->movies_used[i]) continue;
    if (c->mixer.capture_stream_checkpoint == NULL)
      qa_range_error("Cinematic mixer cannot checkpoint its owned PCM lanes");
    movie_entry *movie = &c->movies[i];
    qa_save_value *entry = qa_save_value_object();
    qa_save_value_set(entry, "index", qa_save_value_number(i));
    qa_save_value_set(entry, "asset", qa_save_value_string(movie->asset_path));
    qa_save_value *mode = qa_save_value_object();
    qa_save_value_set(mode, "loop", qa_save_value_boolean(movie->mode.loop));
    qa_save_value_set(mode, "hold", qa_save_value_boolean(movie->mode.hold));
    qa_save_value_set(mode, "silent", qa_save_value_boolean(movie->mode.silent));
    qa_save_value_set(mode, "shader", qa_save_value_boolean(movie->mode.shader));
    qa_save_value_set(mode, "rect", rect_value(movie->rect));
    qa_save_value_set(entry, "mode", mode);
    qa_save_value_set(entry, "playback", qa_cinematic_playback_capture_checkpoint(&movie->playback));
    char id[96];
    snprintf(id, sizeof id, "%s:%d", c->namespace_, i);
    qa_save_value_set(entry, "pcm", c->mixer.capture_stream_checkpoint(c->mixer.ctx, id));
    qa_save_value_push(movies, entry);
  }
  qa_save_value_set(root, "movies", movies);
  return root;
}
typedef struct current_probe { qa_app_application_q3_cinematics *c; int index; const void *token; bool installed; } current_probe;
static bool system_current(void *ctx) {
  current_probe *p = ctx;
  if (p->c->closed) return false;
  return p->c->pending_systems[p->index] || p->installed;
}
static void create_system(qa_app_application_q3_cinematics *c, int index,
                          qa_app_system_cinematic_handle (*open)(void *octx, bool (*current)(void *), void *current_ctx),
                          void *open_ctx) {
  c->pending_systems[index] = true;
  current_probe probe = {c, index, NULL, false};
  qa_app_system_cinematic_handle movie = open(open_ctx, system_current, &probe);
  if (!system_current(&probe)) {
    movie.stop(movie.ctx);
    c->pending_systems[index] = false;
    qa_range_error("System cinematic loaded after close");
  }
  probe.installed = true;
  c->systems[index] = movie;
  c->systems_used[index] = true;
  c->pending_systems[index] = false;
}
typedef struct system_open_ctx {
  qa_app_application_q3_cinematics *c;
  qa_app_screen_cinematic_request request;
  const qa_save_value *checkpoint;
  bool restore;
} system_open_ctx;
static qa_app_system_cinematic_handle system_open_call(void *octx, bool (*current)(void *), void *current_ctx) {
  system_open_ctx *o = octx;
  if (o->restore) return o->c->system.restore(o->c->system.ctx, o->checkpoint, current, current_ctx);
  return o->c->system.open(o->c->system.ctx, o->request, current, current_ctx);
}
void qa_app_application_q3_cinematics_publish_restored(qa_app_application_q3_cinematics *c) {
  if (c->closed || c->restoring) qa_range_error("Cinematic owner cannot publish before restore completes");
  for (int i = 0; i < 16; i++)
    if (c->systems_used[i] && c->systems[i].publish_restored != NULL)
      c->systems[i].publish_restored(c->systems[i].ctx);
}
void qa_app_application_q3_cinematics_restore_checkpoint(qa_app_application_q3_cinematics *c,
                                                         const qa_save_value *value) {
  bool any = c->source_count != 0 || c->pending_loads != 0;
  for (int i = 0; i < 16; i++) if (c->movies_used[i] || c->systems_used[i] || c->pending_systems[i]) any = true;
  if (c->closed || c->restoring || any)
    qa_range_error("Cinematic restore requires an unpublished empty owner");
  qa_save_reader r;
  qa_save_reader_init(&r, value, "q3-cinematics");
  qa_save_reader version;
  qa_save_reader_field(&r, "version", &version);
  qa_save_reader_literal_number(&version, 1);
  qa_save_reader_destroy(&version);
  typedef struct src_row { char *path; qa_resolved_resource_reference resource; } src_row;
  typedef struct movie_row { int index; char *asset; cinematic_mode mode; const qa_save_value *playback; const qa_save_value *pcm; } movie_row;
  typedef struct sys_row { int index; const qa_save_value *checkpoint; } sys_row;
  src_row *sources = NULL; size_t source_rows = 0;
  movie_row *movies = NULL; size_t movie_rows = 0;
  sys_row *systems = NULL; size_t system_rows = 0;
  bool slots[16] = {0};
  qa_save_reader list;
  qa_save_reader_field(&r, "sources", &list);
  source_rows = qa_save_reader_list_count(&list);
  sources = calloc(source_rows == 0 ? 1 : source_rows, sizeof *sources);
  if (sources == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < source_rows; i++) {
    qa_save_reader item, field;
    qa_save_reader_item(&list, i, &item);
    qa_save_reader_field(&item, "path", &field);
    sources[i].path = xstrdup(qa_save_reader_string(&field));
    qa_save_reader_destroy(&field);
    for (size_t j = 0; j < i; j++)
      if (strcmp(sources[j].path, sources[i].path) == 0) qa_range_error("duplicate cinematic source");
    qa_save_reader_field(&item, "resource", &field);
    sources[i].resource = qa_read_resource(&field);
    qa_save_reader_destroy(&field);
    qa_save_reader_destroy(&item);
  }
  qa_save_reader_destroy(&list);
  qa_save_reader_field(&r, "movies", &list);
  movie_rows = qa_save_reader_list_count(&list);
  movies = calloc(movie_rows == 0 ? 1 : movie_rows, sizeof *movies);
  if (movies == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < movie_rows; i++) {
    qa_save_reader item, field;
    qa_save_reader_item(&list, i, &item);
    qa_save_reader_field(&item, "index", &field);
    movies[i].index = (int)qa_save_reader_integer_min(&field, 0);
    qa_save_reader_destroy(&field);
    qa_save_reader_field(&item, "asset", &field);
    movies[i].asset = xstrdup(qa_save_reader_string(&field));
    qa_save_reader_destroy(&field);
    bool asset_known = false;
    for (size_t j = 0; j < source_rows; j++) if (strcmp(sources[j].path, movies[i].asset) == 0) asset_known = true;
    if (movies[i].index >= 16 || slots[movies[i].index] || !asset_known)
      qa_range_error("invalid cinematic slot or source");
    slots[movies[i].index] = true;
    qa_save_reader mode, rect;
    qa_save_reader_field(&item, "mode", &mode);
    qa_save_reader_field(&mode, "loop", &field); movies[i].mode.loop = qa_save_reader_boolean(&field); qa_save_reader_destroy(&field);
    qa_save_reader_field(&mode, "hold", &field); movies[i].mode.hold = qa_save_reader_boolean(&field); qa_save_reader_destroy(&field);
    qa_save_reader_field(&mode, "silent", &field); movies[i].mode.silent = qa_save_reader_boolean(&field); qa_save_reader_destroy(&field);
    qa_save_reader_field(&mode, "shader", &field); movies[i].mode.shader = qa_save_reader_boolean(&field); qa_save_reader_destroy(&field);
    qa_save_reader_field(&mode, "rect", &rect);
    qa_save_reader_field(&rect, "x", &field); movies[i].mode.rect.x = qa_save_reader_finite(&field); qa_save_reader_destroy(&field);
    qa_save_reader_field(&rect, "y", &field); movies[i].mode.rect.y = qa_save_reader_finite(&field); qa_save_reader_destroy(&field);
    qa_save_reader_field(&rect, "width", &field); movies[i].mode.rect.width = qa_save_reader_finite(&field); qa_save_reader_destroy(&field);
    qa_save_reader_field(&rect, "height", &field); movies[i].mode.rect.height = qa_save_reader_finite(&field); qa_save_reader_destroy(&field);
    qa_save_reader_destroy(&rect);
    qa_save_reader_destroy(&mode);
    qa_save_reader_field(&item, "playback", &field);
    movies[i].playback = field.value;
    qa_save_reader_destroy(&field);
    qa_save_reader_field(&item, "pcm", &field);
    movies[i].pcm = field.value;
    qa_save_reader_destroy(&field);
    qa_save_reader_destroy(&item);
  }
  qa_save_reader_destroy(&list);
  qa_save_reader systems_reader;
  bool has_systems = qa_save_reader_field(&r, "systems", &systems_reader) && !qa_save_reader_is_undefined(&systems_reader);
  if (has_systems) {
    system_rows = qa_save_reader_list_count(&systems_reader);
    systems = calloc(system_rows == 0 ? 1 : system_rows, sizeof *systems);
    if (systems == NULL) qa_range_error("Out of memory");
    for (size_t i = 0; i < system_rows; i++) {
      qa_save_reader item, field;
      qa_save_reader_item(&systems_reader, i, &item);
      qa_save_reader_field(&item, "index", &field);
      systems[i].index = (int)qa_save_reader_integer_min(&field, 0);
      qa_save_reader_destroy(&field);
      if (systems[i].index >= 16 || slots[systems[i].index])
        qa_range_error("duplicate or invalid system cinematic slot");
      slots[systems[i].index] = true;
      qa_save_reader_field(&item, "checkpoint", &field);
      systems[i].checkpoint = field.value;
      qa_save_reader_destroy(&field);
      qa_save_reader_destroy(&item);
    }
    qa_save_reader_destroy(&systems_reader);
  }
  bool has_restore = c->has_system && c->system.restore != NULL;
  if (system_rows != 0 && !has_restore)
    qa_range_error("System cinematic restore requires its attached client transition checkpoint");
  if (c->mixer.restore_stream_checkpoint == NULL && movie_rows != 0)
    qa_range_error("Cinematic mixer cannot restore its owned PCM lanes");
  c->restoring = true;
  for (size_t i = 0; i < source_rows; i++) {
    qa_app_ui_cinematic_asset asset;
    prepare_mode(c, sources[i].path, true, &asset);
    source_entry *prepared = find_source(c, sources[i].path);
    if (prepared == NULL || !prepared->has_resource || strcmp(prepared->resource.id, sources[i].resource.id) != 0)
      qa_range_error("cinematic resource changed: %s", sources[i].path);
  }
  if (c->closed) qa_range_error("Cinematic restore completed after close");
  for (size_t i = 0; i < system_rows; i++) {
    if (!has_restore) qa_range_error("System cinematic restore requires its attached client transition checkpoint");
    system_open_ctx open_ctx = {c, {NULL, false, false, false}, systems[i].checkpoint, true};
    create_system(c, systems[i].index, system_open_call, &open_ctx);
    if (c->systems[systems[i].index].publish_restored == NULL)
      qa_range_error("Restored system cinematic has no publication owner");
  }
  for (size_t i = 0; i < movie_rows; i++) {
    qa_app_ui_cinematic_asset asset = {movies[i].asset};
    create_movie(c, movies[i].index, asset, movies[i].mode, movies[i].playback);
    if (c->mixer.restore_stream_checkpoint == NULL)
      qa_range_error("Cinematic mixer cannot restore its owned PCM lanes");
    char id[96];
    snprintf(id, sizeof id, "%s:%d", c->namespace_, movies[i].index);
    qa_audio_stream_target target = {id, {QA_AUDIO_AUDIENCE_SEAT, c->seat}, 1};
    c->mixer.restore_stream_checkpoint(c->mixer.ctx, &target, movies[i].pcm);
  }
  c->restoring = false;
  for (size_t i = 0; i < source_rows; i++) free(sources[i].path);
  free(sources);
  for (size_t i = 0; i < movie_rows; i++) free(movies[i].asset);
  free(movies);
  free(systems);
  qa_save_reader_destroy(&r);
}
double qa_app_application_q3_cinematics_play_guest(qa_app_application_q3_cinematics *c,
                                                   const char *path, qa_rect rect, double bits) {
  if (c->restoring) qa_range_error("Cinematic restore has not completed");
  int flag_bits = (int)bits;
  if ((flag_bits & 1) != 0) {
    if (c->closed) qa_range_error("UI cinematics are closed");
    if (!c->has_system) qa_range_error("System cinematic requires an attached client transition owner");
    int index = 0;
    while (index < 16 && !slot_free(c, index)) index++;
    if (index >= 16) qa_range_error("CIN_HandleForVideo: none free");
    system_open_ctx open_ctx = {c, {path, (flag_bits & 2) != 0, (flag_bits & 4) != 0, (flag_bits & 8) != 0}, NULL, false};
    create_system(c, index, system_open_call, &open_ctx);
    return index;
  }
  qa_app_ui_cinematic_asset asset;
  if (!prepare_mode(c, path, false, &asset)) return -1;
  if (c->closed) qa_range_error("UI cinematic loaded after close");
  source_entry *prepared = find_source(c, asset.path);
  for (int i = 0; i < 16; i++)
    if (c->movies_used[i] && prepared != NULL && strcmp(c->movies[i].path, prepared->source.source != NULL ? prepared->source.source : "") == 0)
      return i;
  double index = 0;
  cinematic_mode mode = {(flag_bits & 2) != 0, (flag_bits & 4) != 0, (flag_bits & 8) != 0, (flag_bits & 16) != 0, rect};
  if (!play_mode(c, asset, mode, &index)) qa_range_error("CIN_HandleForVideo: none free");
  return index;
}
double qa_app_application_q3_cinematics_run_guest(qa_app_application_q3_cinematics *c, double handle) {
  int index = (int)handle;
  if (index >= 0 && index < 16 && c->systems_used[index]) {
    qa_app_system_cinematic_handle *system = &c->systems[index];
    if (system->refresh_status != NULL) system->refresh_status(system->ctx, system);
    if (strcmp(system->status, "held") == 0) return 0;
    if (strcmp(system->status, "playing") == 0 || strcmp(system->status, "paused") == 0) return 1;
    qa_app_application_q3_cinematics_stop(c, handle);
    return 2;
  }
  if (index < 0 || index >= 16 || !c->movies_used[index]) return 2;
  qa_cinematic_playback_tick(&c->movies[index].playback, NULL);
  switch (qa_cinematic_playback_source_status(&c->movies[index].playback)) {
    case QA_CINEMATIC_DECODER_PLAYING:
    case QA_CINEMATIC_DECODER_PAUSED: return 1;
    case QA_CINEMATIC_DECODER_LOOPED: return 5;
    case QA_CINEMATIC_DECODER_HELD: return 0;
    case QA_CINEMATIC_DECODER_ENDED:
    case QA_CINEMATIC_DECODER_STOPPED:
      qa_app_application_q3_cinematics_stop(c, handle);
      return 2;
  }
  return 2;
}
double qa_app_application_q3_cinematics_stop_guest(qa_app_application_q3_cinematics *c, double handle) {
  int index = (int)handle;
  if (index >= 0 && index < 16) c->pending_systems[index] = false;
  if (index >= 0 && index < 16 && c->systems_used[index]) {
    qa_app_system_cinematic_handle system = c->systems[index];
    system.skip(system.ctx);
    if (c->systems_used[index]) c->systems_used[index] = false;
  } else {
    qa_app_application_q3_cinematics_stop(c, handle);
  }
  return 2;
}
void qa_app_application_q3_cinematics_draw_guest(qa_app_application_q3_cinematics *c, double handle, qa_draw2d *draw) {
  int index = (int)handle;
  if (index >= 0 && index < 16 && c->movies_used[index])
    qa_app_application_q3_cinematics_draw(c, handle, c->movies[index].rect, draw);
}
void qa_app_application_q3_cinematics_set_extents(qa_app_application_q3_cinematics *c, double handle, qa_rect rect) {
  int index = (int)handle;
  if (index >= 0 && index < 16 && c->movies_used[index]) c->movies[index].rect = rect;
}
void qa_app_application_q3_cinematics_run(qa_app_application_q3_cinematics *c, double handle) {
  int index = (int)handle;
  if (index < 0 || index >= 16 || !c->movies_used[index]) return;
  qa_cinematic_playback_tick(&c->movies[index].playback, NULL);
}
void qa_app_application_q3_cinematics_draw(qa_app_application_q3_cinematics *c, double handle, qa_rect rect, qa_draw2d *draw) {
  int index = (int)handle;
  if (index < 0 || index >= 16 || !c->movies_used[index]) return;
  qa_cinematic_frame frame;
  if (!qa_cinematic_playback_current_frame(&c->movies[index].playback, &frame)) return;
  qa_draw2d_draw_pic(draw, rect, &c->movies[index].picture);
}
void qa_app_application_q3_cinematics_stop(qa_app_application_q3_cinematics *c, double handle) {
  int index = (int)handle;
  if (index < 0 || index >= 16) return;
  c->pending_systems[index] = false;
  if (c->systems_used[index]) {
    qa_app_system_cinematic_handle system = c->systems[index];
    c->systems_used[index] = false;
    system.stop(system.ctx);
    return;
  }
  if (!c->movies_used[index]) return;
  movie_entry *movie = &c->movies[index];
  void *release = movie->image;
  qa_cinematic_playback_destroy(&movie->playback);
  if (release != NULL) c->host.image_release(c->host.ctx, release);
  free(movie->audio_id);
  free(movie->path);
  free(movie->asset_path);
  c->movies_used[index] = false;
}
void qa_app_application_q3_cinematics_close(qa_app_application_q3_cinematics *c) {
  if (c->closed) return;
  c->closed = true;
  for (int i = 0; i < 16; i++) c->pending_systems[i] = false;
  for (int i = 0; i < 16; i++) if (c->movies_used[i]) qa_app_application_q3_cinematics_stop(c, i);
  for (int i = 0; i < 16; i++) if (c->systems_used[i]) qa_app_application_q3_cinematics_stop(c, i);
  for (size_t i = 0; i < c->source_count; i++) {
    qa_cinematic_source_dispose(&c->sources[i].source);
    free(c->sources[i].path);
    free(c->sources[i].name);
    free(c->sources[i].bytes);
  }
  c->source_count = 0;
}
