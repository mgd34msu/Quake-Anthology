/* Port of src/app/bootstrap/q3-client/cinematics.ts.
 * GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "qa/app/bootstrap/q3_client/cinematics.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/js.h"
#include "qa/foundation/js_case.h"
#include "qa/foundation/support.h"
#include "qa/materials/compile.h"
#include "qa/materials/material_finish.h"
#include "qa/media/presentation.h"
#include "qa/media/playback.h"
#include "qa/media/still.h"
#include "qa/persistence/recipe.h"
#include "qa/render/scene/resources.h"
/* TS `throw new Error(message)`. */
#define js_error(...) qa_raise("Error", 0, 0, __VA_ARGS__)
static int next_consumer = 0;
typedef struct cinematic_mode { bool loop, hold, silent, shader; qa_rect rect; } cinematic_mode;
typedef struct source_entry {
  char *path;
  qa_cinematic_source source;
  uint8_t *bytes; /* owned backing for bytes-backed sources */
  char *name;
  qa_resolved_resource_reference resource; /* owned copy */
} source_entry;
typedef struct movie_entry {
  qa_app_application_q3_cinematics *owner;
  qa_cinematic_playback playback;
  qa_cinematic_audio_binding audio;
  char *audio_id; /* `${namespace}:${index}`: audio lane, material target and picture name */
  qa_cinematic_image image;
  bool has_image;
  /* The implicit picture material and the finish results it views. */
  qa_finish_shader_input_result implicit_input;
  qa_finished_shader_result finished;
  qa_compiled_material compiled;
  bool has_compiled;
  qa_picture_asset picture;
  char *path;      /* prepared.source.source */
  char *asset_path;
  cinematic_mode mode;
  qa_rect rect;
} movie_entry;
struct qa_app_application_q3_cinematics {
  qa_app_application_q3_assets *media;
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
  /* JS Map insertion order and pending-token identity. */
  double sequence;
  double movie_order[16], system_order[16], pending_token[16];
  void *probes[16]; /* system handles' current() contexts */
};
static size_t movies_in_order(const qa_app_application_q3_cinematics *c, int out[16]);
static size_t systems_in_order(const qa_app_application_q3_cinematics *c, int out[16]);
static char *xstrdup(const char *s) { char *p = strdup(s != NULL ? s : ""); if (p == NULL) qa_range_error("Out of memory"); return p; }
static source_entry *find_source(qa_app_application_q3_cinematics *c, const char *path) {
  for (size_t i = 0; i < c->source_count; i++)
    if (strcmp(c->sources[i].path, path) == 0) return &c->sources[i];
  return NULL;
}
/* TS assets.assets.images: the image registry of the ApplicationAssets. */
static qa_scene_image_registry *images(const qa_app_application_q3_cinematics *c) {
  qa_app_application_assets *assets = qa_app_application_q3_assets_application(c->media);
  if (assets == NULL) js_error("Q3 cinematics media has no application assets");
  return qa_app_application_assets_images(assets);
}
static double now_sample(void *ctx) {
  qa_app_application_q3_cinematics *c = ctx;
  return c->now(c->now_ctx);
}
static void dev_print(void *ctx, const char *text) {
  qa_app_application_q3_cinematics *c = ctx;
  qa_app_application_q3_assets_print(c->media, text);
}
static bool slot_free(qa_app_application_q3_cinematics *c, int index) {
  return !c->movies_used[index] && !c->systems_used[index] && !c->pending_systems[index];
}
qa_app_application_q3_cinematics *qa_app_application_q3_cinematics_create(
    qa_app_application_q3_assets *media, qa_cinematic_mixer mixer, qa_seat_id seat,
    double (*now)(void *ctx), void *now_ctx,
    const qa_app_system_cinematic_host *system) {
  qa_app_application_q3_cinematics *c = calloc(1, sizeof *c);
  if (c == NULL) qa_range_error("Out of memory");
  c->media = media;
  c->mixer = mixer;
  c->seat = seat;
  c->now = now;
  c->now_ctx = now_ctx;
  if (system != NULL) { c->has_system = true; c->system = *system; }
  snprintf(c->namespace_, sizeof c->namespace_, "q3-cinematic:%d", ++next_consumer);
  return c;
}
static void source_dispose(source_entry *entry) {
  qa_cinematic_source_dispose(&entry->source);
  qa_resolved_resource_release(&entry->resource);
  free(entry->path);
  free(entry->name);
  free(entry->bytes);
}
static void clear_sources(qa_app_application_q3_cinematics *c) {
  for (size_t i = 0; i < c->source_count; i++) source_dispose(&c->sources[i]);
  c->source_count = 0;
}
void qa_app_application_q3_cinematics_destroy(qa_app_application_q3_cinematics *cinematics) {
  if (cinematics == NULL) return;
  qa_app_application_q3_cinematics_close(cinematics);
  for (int i = 0; i < 16; i++) free(cinematics->probes[i]);
  free(cinematics->sources);
  free(cinematics);
}
qa_seat_id qa_app_application_q3_cinematics_seat(const qa_app_application_q3_cinematics *c) { return c->seat; }
typedef struct decode_scope {
  source_entry *entry;
  const char *extension;
  size_t length;
  bool ok;
} decode_scope;
/* source = extension === "pcx" ? cinematicPcx(bytes, name) : cinematicBytes(extension, bytes, name),
 * with the header read the C decoders defer to cinematicDimensions. */
static void decode_source(void *ctx) {
  decode_scope *scope = ctx;
  source_entry *entry = scope->entry;
  if (strcmp(scope->extension, "pcx") == 0) {
    scope->ok = qa_cinematic_pcx(entry->bytes, scope->length, entry->name, &entry->source);
    return;
  }
  qa_cinematic_format format = strcmp(scope->extension, "roq") == 0 ? QA_CINEMATIC_FORMAT_ROQ
    : strcmp(scope->extension, "cin") == 0 ? QA_CINEMATIC_FORMAT_CIN : QA_CINEMATIC_FORMAT_OGV;
  entry->source = qa_cinematic_bytes(format, entry->bytes, scope->length, entry->name);
  int64_t width = 0, height = 0;
  scope->ok = qa_cinematic_dimensions(&entry->source, &width, &height);
}
static bool prepare_mode(qa_app_application_q3_cinematics *c, const char *path, bool restoring,
                         qa_app_ui_cinematic_asset *out) {
  if (c->restoring && !restoring) js_error("Cinematic restore has not completed");
  if (c->closed) js_error("UI cinematics are closed");
  if (find_source(c, path) == NULL) {
    char selected[1024];
    if (strchr(path, '/') != NULL) snprintf(selected, sizeof selected, "%s", path);
    else snprintf(selected, sizeof selected, "video/%s", path);
    /* /\.[^/]+$/ */
    const char *base = strrchr(selected, '/');
    const char *dot = strrchr(base != NULL ? base : selected, '.');
    bool has_ext = dot != NULL && dot[1] != '\0';
    char name[1080];
    if (has_ext) snprintf(name, sizeof name, "%s", selected);
    else snprintf(name, sizeof name, "%s.roq", selected);
    c->pending_loads++;
    qa_content_opened_resource opened;
    bool found = qa_content_mounted_open(qa_app_application_q3_assets_provider(c->media)->mounts, name, NULL, NULL, &opened);
    c->pending_loads--;
    if (c->closed) { if (found) free(opened.bytes); js_error("UI cinematic loaded after close"); }
    if (!found || opened.byte_length == 0) {
      if (found) free(opened.bytes);
      return false; /* MissingCinematicError: Missing cinematic <name> */
    }
    /* name.slice(name.lastIndexOf(".") + 1).toLowerCase() */
    const char *last = strrchr(name, '.');
    last = last == NULL ? name : last + 1;
    char *extension = qa_js_to_lower_case(last);
    if (extension == NULL) qa_range_error("Out of memory");
    bool supported = strcmp(extension, "roq") == 0 || strcmp(extension, "cin") == 0
      || strcmp(extension, "ogv") == 0 || strcmp(extension, "pcx") == 0;
    if (!supported) { free(extension); free(opened.bytes); return false; }
    if (c->source_count == c->source_capacity) {
      size_t cap = c->source_capacity == 0 ? 8 : c->source_capacity * 2;
      source_entry *next = realloc(c->sources, cap * sizeof *next);
      if (next == NULL) qa_range_error("Out of memory");
      c->sources = next; c->source_capacity = cap;
    }
    source_entry *entry = &c->sources[c->source_count];
    memset(entry, 0, sizeof *entry);
    entry->bytes = opened.bytes;
    entry->name = xstrdup(name);
    decode_scope scope = {entry, extension, opened.byte_length, false};
    qa_caught_error caught;
    bool decoded = qa_try(decode_source, &scope, &caught) && scope.ok;
    free(extension);
    if (!decoded) {
      qa_cinematic_source_dispose(&entry->source);
      free(entry->name); free(entry->bytes);
      if (c->closed) js_error("UI cinematic loaded after close");
      return false; /* MissingCinematicError: Undecodable cinematic <name> */
    }
    if (!qa_resolved_resource_copy(opened.reference, &entry->resource)) qa_range_error("Out of memory");
    entry->path = xstrdup(path);
    c->source_count++;
  }
  out->path = find_source(c, path)->path;
  return true;
}
bool qa_app_application_q3_cinematics_prepare(qa_app_application_q3_cinematics *c, const char *path,
                                              qa_app_ui_cinematic_asset *out) {
  return prepare_mode(c, path, false, out);
}
/* images.allocate(width, height, source): the CinematicImage reallocation. */
static qa_renderer_image movie_allocate(void *ctx, int64_t width, int64_t height, bool source_is_resource,
    qa_resolved_resource_reference source_resource, const char *source_name) {
  qa_renderer_image source;
  memset(&source, 0, sizeof source);
  source.source_is_resource = source_is_resource;
  source.source_resource = source_resource;
  source.source_name = source_name;
  return qa_scene_image_registry_allocate(images(ctx), (double)width, (double)height, source)->image;
}
typedef struct resolve_apply {
  void (*apply)(void *ctx, qa_image_resource_operation operation);
  void *apply_ctx;
  qa_scene_image_registry *images;
} resolve_apply;
static void movie_apply(void *ctx, const qa_image_resource_operation *operation) {
  resolve_apply *state = ctx;
  state->apply(state->apply_ctx, *operation);
  qa_scene_image_registry_commit(state->images, *operation);
}
/* ShaderCinematicSource.resolve(apply). */
static qa_renderer_image movie_resolve(void *ctx, void (*apply)(void *apply_ctx, qa_image_resource_operation operation),
                                       void *apply_ctx) {
  movie_entry *movie = ctx;
  if (!movie->playback.has_picture) js_error("UI cinematic draw has no decoded frame");
  resolve_apply state = {apply, apply_ctx, images(movie->owner)};
  /* playback.currentFrame is the playback's own picture (no copy). */
  return qa_cinematic_image_resolve(&movie->image, &movie->playback.picture, qa_cinematic_playback_revision(&movie->playback),
                                    movie_apply, &state);
}
/* onAudio: block => audio.onAudio(block, { kind: "seat", seat }). */
static void movie_on_audio(void *ctx, const qa_cinematic_audio *audio, qa_cinematic_target target) {
  movie_entry *movie = ctx;
  (void)target; /* TS replaces the playback target with this seat */
  qa_cinematic_target seat;
  memset(&seat, 0, sizeof seat);
  seat.kind = QA_CINEMATIC_TARGET_SEAT;
  seat.seat = movie->owner->seat;
  qa_cinematic_audio_on_audio(&movie->audio, audio, seat);
}
static void movie_on_complete(void *ctx, qa_cinematic_end_reason reason, qa_cinematic_target target) {
  (void)ctx; (void)reason; (void)target; /* TS onComplete: () => undefined */
}
/* compileImplicitMaterial's finish service: results stay with the movie. */
static qa_mat_finished_shader movie_finish(void *ctx, qa_mat_finish_input input) {
  movie_entry *movie = ctx;
  qa_finish_shader(input, &movie->finished);
  return movie->finished.shader;
}
static qa_mat_finish_input movie_implicit(void *ctx, qa_mat_finish_implicit_input input) {
  movie_entry *movie = ctx;
  qa_implicit_shader_input(input, &movie->implicit_input);
  return movie->implicit_input.input;
}
/* movie.playback.tick(): the tick's frame copy is not kept. */
static void movie_tick(movie_entry *movie) {
  qa_cinematic_tick tick;
  if (!qa_cinematic_playback_tick(&movie->playback, &tick)) qa_range_error("Out of memory");
  qa_cinematic_tick_dispose(&tick);
}
static void movie_release(movie_entry *movie) {
  if (movie->has_compiled) qa_compiled_material_destroy(&movie->compiled);
  qa_finished_shader_result_free(&movie->finished);
  qa_finish_shader_input_result_free(&movie->implicit_input);
  if (movie->has_image) qa_cinematic_image_destroy(&movie->image);
  free(movie->audio_id);
  free(movie->path);
  free(movie->asset_path);
  memset(movie, 0, sizeof *movie);
}
typedef struct movie_scope { qa_app_application_q3_cinematics *c; int index; const source_entry *prepared; cinematic_mode mode; } movie_scope;
/* The TS try block after the playback exists: image, video source, picture, publication. */
static void movie_body(void *ctx) {
  movie_scope *scope = ctx;
  qa_app_application_q3_cinematics *c = scope->c;
  movie_entry *movie = &c->movies[scope->index];
  int64_t width = 0, height = 0;
  if (!qa_cinematic_dimensions(&scope->prepared->source, &width, &height)) qa_range_error("Out of memory");
  qa_renderer_image source;
  memset(&source, 0, sizeof source);
  source.source_is_resource = true;
  source.source_resource = scope->prepared->resource;
  qa_renderer_image allocated = qa_scene_image_registry_allocate(images(c), (double)width, (double)height, source)->image;
  qa_cinematic_image_init(&movie->image, allocated, movie_allocate, c);
  movie->has_image = true;
  qa_mat_finish_implicit_input input;
  memset(&input, 0, sizeof input);
  input.kind = "picture";
  input.name = movie->audio_id;
  input.profile = qa_default_shader_profile;
  input.base_image.kind = "loaded";
  input.base_image.tmu = 0;
  input.base_image.binding.kind = "video";
  input.base_image.binding.source.source.ctx = movie;
  input.base_image.binding.source.source.resolve = movie_resolve;
  input.base_image.binding.source.image = qa_cinematic_image_image(&movie->image);
  qa_mat_finish_service finish = {movie, movie_finish, movie_implicit};
  movie->compiled = qa_compile_implicit_material(input, finish);
  movie->has_compiled = true;
  memset(&movie->picture, 0, sizeof movie->picture);
  movie->picture.kind = QA_PICTURE_MATERIAL;
  movie->picture.material.name = movie->audio_id;
  movie->picture.material.order = 0;
  movie->picture.material.compiled = &movie->compiled;
}
static void create_movie(qa_app_application_q3_cinematics *c, int index, qa_app_ui_cinematic_asset asset,
                         cinematic_mode mode, const qa_save_value *checkpoint) {
  source_entry *prepared = find_source(c, asset.path);
  if (prepared == NULL) js_error("Unprepared cinematic %s", asset.path);
  char id[96];
  snprintf(id, sizeof id, "%s:%d", c->namespace_, index);
  movie_entry *movie = &c->movies[index];
  memset(movie, 0, sizeof *movie);
  movie->owner = c;
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
  options.on_audio_ctx = movie;
  options.on_audio = movie_on_audio;
  options.on_complete = movie_on_complete;
  options.developer_print_ctx = c;
  options.developer_print = dev_print;
  if (!qa_cinematic_playback_init(&movie->playback, &prepared->source, &options, checkpoint)) {
    movie_release(movie);
    qa_range_error("Out of memory");
  }
  movie_scope scope = {c, index, prepared, mode};
  qa_caught_error caught;
  if (!qa_try(movie_body, &scope, &caught)) {
    /* catch (error) { playback.close(); throw error; } */
    qa_cinematic_playback_destroy(&movie->playback);
    movie_release(movie);
    qa_rethrow(&caught);
  }
  movie->path = xstrdup(prepared->source.source != NULL ? prepared->source.source : "");
  movie->asset_path = xstrdup(asset.path);
  movie->mode = mode;
  movie->rect = mode.rect;
  c->movies_used[index] = true;
  c->movie_order[index] = ++c->sequence;
}
static bool play_mode(qa_app_application_q3_cinematics *c, qa_app_ui_cinematic_asset asset,
                      cinematic_mode mode, double *out_index) {
  if (c->restoring) js_error("Cinematic restore has not completed");
  if (c->closed) js_error("UI cinematics are closed");
  if (find_source(c, asset.path) == NULL) js_error("Unprepared cinematic %s", asset.path);
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
/* The resolved resource as its saved value, in the key order of the opened
 * resource the mounts build ({ requestedPath, provenance, digest, byteLength,
 * resolution, id }); qa_read_resource reads it back. */
static qa_save_value *mount_identity_value(qa_mount_identity identity) {
  qa_save_value *o = qa_save_value_object();
  qa_save_value_set(o, "id", qa_save_value_string(identity.id));
  qa_save_value_set(o, "content", qa_save_value_string(identity.content));
  qa_save_value_set(o, "generation", qa_save_value_number((double)identity.generation));
  return o;
}
static qa_save_value *resource_value(const qa_resolved_resource_reference *resource) {
  static const char *const formats[] = {"pak", "pk3", "kpf", "zip"};
  qa_save_value *o = qa_save_value_object();
  qa_save_value_set(o, "requestedPath", qa_save_value_string(resource->requested_path));
  qa_save_value *provenance = qa_save_value_object();
  qa_save_value *mount = qa_save_value_object();
  if (resource->provenance.kind == QA_CONTENT_MOUNT_ARCHIVE) {
    const qa_archive_mount *archive = resource->provenance.archive_mount;
    qa_save_value_set(provenance, "kind", qa_save_value_string("archive"));
    qa_save_value_set(mount, "kind", qa_save_value_string("archive"));
    qa_save_value_set(mount, "identity", mount_identity_value(archive->identity));
    qa_save_value_set(mount, "archivePath", qa_save_value_string(archive->archive_path));
    qa_save_value_set(mount, "archiveDigest", qa_save_value_string(archive->archive_digest));
    qa_save_value_set(mount, "format", qa_save_value_string(formats[archive->format]));
    qa_save_value_set(provenance, "mount", mount);
    qa_save_value_set(provenance, "memberPath", qa_save_value_string(resource->provenance.member_path));
    qa_save_value_set(provenance, "memberIndex", qa_save_value_number((double)resource->provenance.member_index));
  } else {
    const qa_loose_mount *loose = resource->provenance.loose_mount;
    qa_save_value_set(provenance, "kind", qa_save_value_string("loose"));
    qa_save_value_set(mount, "kind", qa_save_value_string("loose"));
    qa_save_value_set(mount, "identity", mount_identity_value(loose->identity));
    qa_save_value_set(mount, "rootPath", qa_save_value_string(loose->root_path));
    qa_save_value_set(provenance, "mount", mount);
    qa_save_value_set(provenance, "memberPath", qa_save_value_string(resource->provenance.member_path));
  }
  qa_save_value_set(o, "provenance", provenance);
  qa_save_value_set(o, "digest", qa_save_value_string(resource->digest));
  qa_save_value_set(o, "byteLength", qa_save_value_number((double)resource->byte_length));
  qa_save_value *resolution = qa_save_value_object();
  const qa_resource_resolution *r = &resource->resolution;
  switch (r->kind) {
    case QA_RESOURCE_RESOLUTION_DEFAULT_ORDER:
      qa_save_value_set(resolution, "kind", qa_save_value_string("default-order"));
      qa_save_value_set(resolution, "plan", qa_save_value_string(r->plan));
      qa_save_value_set(resolution, "rank", qa_save_value_number((double)r->rank));
      break;
    case QA_RESOURCE_RESOLUTION_PREFIX_ORDER:
      qa_save_value_set(resolution, "kind", qa_save_value_string("prefix-order"));
      qa_save_value_set(resolution, "plan", qa_save_value_string(r->plan));
      qa_save_value_set(resolution, "prefix", qa_save_value_string(r->prefix));
      qa_save_value_set(resolution, "rank", qa_save_value_number((double)r->rank));
      break;
    case QA_RESOURCE_RESOLUTION_LINK:
      qa_save_value_set(resolution, "kind", qa_save_value_string("link"));
      qa_save_value_set(resolution, "plan", qa_save_value_string(r->plan));
      qa_save_value_set(resolution, "sourcePrefix", qa_save_value_string(r->source_prefix));
      qa_save_value_set(resolution, "targetPath", qa_save_value_string(r->target_path));
      break;
  }
  qa_save_value_set(o, "resolution", resolution);
  qa_save_value_set(o, "id", qa_save_value_string(resource->id));
  return o;
}
qa_save_value *qa_app_application_q3_cinematics_capture_checkpoint(qa_app_application_q3_cinematics *c) {
  bool any_pending = false;
  for (int i = 0; i < 16; i++) if (c->pending_systems[i]) any_pending = true;
  if (c->closed || c->restoring || c->pending_loads != 0 || any_pending)
    js_error("Cinematic checkpoint requires an idle live owner");
  bool any_movie = false;
  for (int i = 0; i < 16; i++) if (c->movies_used[i]) any_movie = true;
  if (c->mixer.capture_stream_checkpoint == NULL && any_movie)
    js_error("Cinematic mixer cannot checkpoint its owned PCM lanes");
  for (int i = 0; i < 16; i++)
    if (c->systems_used[i] && c->systems[i].capture_checkpoint == NULL)
      js_error("System cinematic checkpoint requires its attached client transition checkpoint");
  qa_save_value *root = qa_save_value_object();
  qa_save_value_set(root, "version", qa_save_value_number(1));
  int order[16];
  size_t count = systems_in_order(c, order);
  qa_save_value *systems = qa_save_value_array();
  for (size_t n = 0; n < count; n++) {
    int i = order[n];
    qa_app_system_cinematic_handle *system = &c->systems[i];
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
    qa_save_value_set(entry, "resource", resource_value(&c->sources[i].resource));
    qa_save_value_push(sources, entry);
  }
  qa_save_value_set(root, "sources", sources);
  count = movies_in_order(c, order);
  qa_save_value *movies = qa_save_value_array();
  for (size_t n = 0; n < count; n++) {
    int i = order[n];
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
/* Map<number, ...> keys: a handle names a slot only when it is an integer 0..15. */
static bool slot_of(double handle, int *out) {
  if (!(handle >= 0 && handle < 16) || handle != (double)(int)handle) return false;
  *out = (int)handle;
  return true;
}
/* JS Map iteration order: slots by insertion (movie and system creation). */
static size_t movies_in_order(const qa_app_application_q3_cinematics *c, int out[16]) {
  size_t count = 0;
  for (int i = 0; i < 16; i++) if (c->movies_used[i]) out[count++] = i;
  for (size_t a = 1; a < count; a++)
    for (size_t b = a; b > 0 && c->movie_order[out[b - 1]] > c->movie_order[out[b]]; b--) {
      int t = out[b]; out[b] = out[b - 1]; out[b - 1] = t;
    }
  return count;
}
static size_t systems_in_order(const qa_app_application_q3_cinematics *c, int out[16]) {
  size_t count = 0;
  for (int i = 0; i < 16; i++) if (c->systems_used[i]) out[count++] = i;
  for (size_t a = 1; a < count; a++)
    for (size_t b = a; b > 0 && c->system_order[out[b - 1]] > c->system_order[out[b]]; b--) {
      int t = out[b]; out[b] = out[b - 1]; out[b - 1] = t;
    }
  return count;
}
typedef struct current_probe { qa_app_application_q3_cinematics *c; int index; double token; bool installed; } current_probe;
/* current = !closed && (pendingSystems.get(index) === token || (installed && systems.get(index) === installed)) */
static bool system_current(void *ctx) {
  current_probe *p = ctx;
  if (p->c->closed) return false;
  if (p->c->pending_systems[p->index] && p->c->pending_token[p->index] == p->token) return true;
  return p->installed && p->c->systems_used[p->index] && p->c->system_order[p->index] == p->token;
}
typedef struct system_open_ctx {
  qa_app_application_q3_cinematics *c;
  qa_app_q3_screen_cinematic_request request;
  const qa_save_value *checkpoint;
  bool restore;
  current_probe *probe;
  qa_app_system_cinematic_handle movie;
} system_open_ctx;
static void system_open_call(void *ctx) {
  system_open_ctx *o = ctx;
  o->movie = o->restore ? o->c->system.restore(o->c->system.ctx, o->checkpoint, system_current, o->probe)
                        : o->c->system.open(o->c->system.ctx, o->request, system_current, o->probe);
}
static void create_system(qa_app_application_q3_cinematics *c, int index, system_open_ctx *open) {
  double token = ++c->sequence;
  c->pending_systems[index] = true;
  c->pending_token[index] = token;
  /* The probe is the handle's current() context; it lives until the slot is reused or the owner is freed. */
  current_probe *probe = calloc(1, sizeof *probe);
  if (probe == NULL) qa_range_error("Out of memory");
  *probe = (current_probe){c, index, token, false};
  free(c->probes[index]);
  c->probes[index] = probe;
  open->probe = probe;
  qa_caught_error caught;
  bool opened = qa_try(system_open_call, open, &caught);
  bool pending = c->pending_systems[index] && c->pending_token[index] == token;
  if (opened && !system_current(probe)) {
    if (pending) c->pending_systems[index] = false;
    open->movie.stop(open->movie.ctx);
    js_error("System cinematic loaded after close");
  }
  if (opened) {
    probe->installed = true;
    c->systems[index] = open->movie;
    c->systems_used[index] = true;
    c->system_order[index] = token;
  }
  if (pending) c->pending_systems[index] = false;
  if (!opened) qa_rethrow(&caught);
}
void qa_app_application_q3_cinematics_publish_restored(qa_app_application_q3_cinematics *c) {
  if (c->closed || c->restoring) js_error("Cinematic owner cannot publish before restore completes");
  int order[16];
  size_t count = systems_in_order(c, order);
  for (size_t i = 0; i < count; i++)
    if (c->systems[order[i]].publish_restored != NULL) c->systems[order[i]].publish_restored(c->systems[order[i]].ctx);
}
typedef struct src_row { char *path; qa_resolved_resource_reference resource; bool has_resource; } src_row;
typedef struct movie_row { double index; char *asset; cinematic_mode mode; const qa_save_value *playback; const qa_save_value *pcm; } movie_row;
typedef struct sys_row { double index; const qa_save_value *checkpoint; } sys_row;
typedef struct restore_state {
  qa_app_application_q3_cinematics *c;
  const qa_save_value *value;
  qa_save_reader r; bool has_r;
  src_row *sources; size_t source_rows;
  movie_row *movies; size_t movie_rows;
  sys_row *systems; size_t system_rows;
} restore_state;
static void restore_state_release(void *ctx) {
  restore_state *s = ctx;
  for (size_t i = 0; i < s->source_rows; i++) {
    free(s->sources[i].path);
    if (s->sources[i].has_resource) qa_resolved_resource_release(&s->sources[i].resource);
  }
  free(s->sources);
  for (size_t i = 0; i < s->movie_rows; i++) free(s->movies[i].asset);
  free(s->movies);
  free(s->systems);
  if (s->has_r) qa_save_reader_destroy(&s->r);
  memset(s, 0, sizeof *s);
}
static void *rows(size_t count, size_t size) {
  void *p = calloc(count == 0 ? 1 : count, size);
  if (p == NULL) qa_range_error("Out of memory");
  return p;
}
/* Field readers whose reader frees itself on the next field. */
static const char *field_string(const qa_save_reader *parent, const char *name) {
  qa_save_reader field;
  qa_save_reader_field(parent, name, &field);
  const char *value = qa_save_reader_string(&field);
  qa_save_reader_destroy(&field);
  return value;
}
static double field_integer(const qa_save_reader *parent, const char *name) {
  qa_save_reader field;
  qa_save_reader_field(parent, name, &field);
  double value = qa_save_reader_integer_min(&field, 0);
  qa_save_reader_destroy(&field);
  return value;
}
static bool field_boolean(const qa_save_reader *parent, const char *name) {
  qa_save_reader field;
  qa_save_reader_field(parent, name, &field);
  bool value = qa_save_reader_boolean(&field);
  qa_save_reader_destroy(&field);
  return value;
}
static double field_finite(const qa_save_reader *parent, const char *name) {
  qa_save_reader field;
  qa_save_reader_field(parent, name, &field);
  double value = qa_save_reader_finite(&field);
  qa_save_reader_destroy(&field);
  return value;
}
static const qa_save_value *field_value(const qa_save_reader *parent, const char *name) {
  qa_save_reader field;
  qa_save_reader_field(parent, name, &field);
  const qa_save_value *value = field.value;
  qa_save_reader_destroy(&field);
  return value;
}
/* The reads before restoring starts (TS SaveReader pass). */
static void restore_read(restore_state *s) {
  qa_save_reader *r = &s->r;
  qa_save_reader version;
  qa_save_reader_field(r, "version", &version);
  qa_save_reader_literal_number(&version, 1);
  qa_save_reader_destroy(&version);
  qa_save_reader list;
  qa_save_reader_field(r, "sources", &list);
  size_t count = qa_save_reader_list_count(&list);
  s->sources = rows(count, sizeof *s->sources);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader item;
    qa_save_reader_item(&list, i, &item);
    s->sources[i].path = xstrdup(field_string(&item, "path"));
    s->source_rows = i + 1;
    qa_save_reader field;
    qa_save_reader_field(&item, "resource", &field);
    s->sources[i].resource = qa_read_resource(&field);
    s->sources[i].has_resource = true;
    qa_save_reader_destroy(&field);
    qa_save_reader_destroy(&item);
  }
  qa_save_reader_destroy(&list);
  for (size_t i = 0; i < s->source_rows; i++)
    for (size_t j = 0; j < i; j++)
      if (strcmp(s->sources[j].path, s->sources[i].path) == 0) qa_save_reader_fail(r, "duplicate cinematic source");
  bool slots[16] = {0};
  qa_save_reader_field(r, "movies", &list);
  count = qa_save_reader_list_count(&list);
  s->movies = rows(count, sizeof *s->movies);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader item;
    qa_save_reader_item(&list, i, &item);
    movie_row *row = &s->movies[i];
    row->index = field_integer(&item, "index");
    row->asset = xstrdup(field_string(&item, "asset"));
    s->movie_rows = i + 1;
    bool asset_known = false;
    for (size_t j = 0; j < s->source_rows; j++) if (strcmp(s->sources[j].path, row->asset) == 0) asset_known = true;
    if (row->index >= 16 || slots[(int)row->index] || !asset_known) qa_save_reader_fail(&item, "invalid cinematic slot or source");
    slots[(int)row->index] = true;
    qa_save_reader mode, rect;
    qa_save_reader_field(&item, "mode", &mode);
    qa_save_reader_field(&mode, "rect", &rect);
    row->mode.loop = field_boolean(&mode, "loop");
    row->mode.hold = field_boolean(&mode, "hold");
    row->mode.silent = field_boolean(&mode, "silent");
    row->mode.shader = field_boolean(&mode, "shader");
    row->mode.rect.x = field_finite(&rect, "x");
    row->mode.rect.y = field_finite(&rect, "y");
    row->mode.rect.width = field_finite(&rect, "width");
    row->mode.rect.height = field_finite(&rect, "height");
    qa_save_reader_destroy(&rect);
    qa_save_reader_destroy(&mode);
    row->playback = field_value(&item, "playback");
    row->pcm = field_value(&item, "pcm");
    qa_save_reader_destroy(&item);
  }
  qa_save_reader_destroy(&list);
  qa_save_reader systems;
  if (qa_save_reader_field(r, "systems", &systems) && !qa_save_reader_is_undefined(&systems)) {
    count = qa_save_reader_list_count(&systems);
    s->systems = rows(count, sizeof *s->systems);
    for (size_t i = 0; i < count; i++) {
      qa_save_reader item;
      qa_save_reader_item(&systems, i, &item);
      sys_row *row = &s->systems[i];
      row->index = field_integer(&item, "index");
      if (row->index >= 16 || slots[(int)row->index]) qa_save_reader_fail(&item, "duplicate or invalid system cinematic slot");
      slots[(int)row->index] = true;
      row->checkpoint = field_value(&item, "checkpoint");
      s->system_rows = i + 1;
      qa_save_reader_destroy(&item);
    }
  }
  qa_save_reader_destroy(&systems);
}
/* The TS try block while restoring. */
static void restore_body(void *ctx) {
  restore_state *s = ctx;
  qa_app_application_q3_cinematics *c = s->c;
  bool has_restore = c->has_system && c->system.restore != NULL;
  for (size_t i = 0; i < s->source_rows; i++) {
    qa_app_ui_cinematic_asset asset;
    if (!prepare_mode(c, s->sources[i].path, true, &asset)) js_error("Missing cinematic %s", s->sources[i].path);
    source_entry *prepared = find_source(c, s->sources[i].path);
    if (prepared == NULL || strcmp(prepared->resource.id, s->sources[i].resource.id) != 0)
      qa_save_reader_fail(&s->r, "cinematic resource changed: %s", s->sources[i].path);
  }
  if (c->closed) js_error("Cinematic restore completed after close");
  for (size_t i = 0; i < s->system_rows; i++) {
    if (!has_restore) js_error("System cinematic restore requires its attached client transition checkpoint");
    int index = (int)s->systems[i].index;
    system_open_ctx open = {c, {NULL, false, false, false}, s->systems[i].checkpoint, true, NULL, {0}};
    create_system(c, index, &open);
    if (!c->systems_used[index] || c->systems[index].publish_restored == NULL)
      js_error("Restored system cinematic has no publication owner");
  }
  for (size_t i = 0; i < s->movie_rows; i++) {
    qa_app_ui_cinematic_asset asset = {s->movies[i].asset};
    int index = (int)s->movies[i].index;
    create_movie(c, index, asset, s->movies[i].mode, s->movies[i].playback);
    if (c->mixer.restore_stream_checkpoint == NULL) js_error("Cinematic mixer cannot restore its owned PCM lanes");
    char id[96];
    snprintf(id, sizeof id, "%s:%d", c->namespace_, index);
    qa_audio_stream_target target = {id, {QA_AUDIO_AUDIENCE_SEAT, c->seat}, 1};
    c->mixer.restore_stream_checkpoint(c->mixer.ctx, &target, s->movies[i].pcm);
  }
}
void qa_app_application_q3_cinematics_restore_checkpoint(qa_app_application_q3_cinematics *c,
                                                         const qa_save_value *value) {
  bool any = c->source_count != 0 || c->pending_loads != 0;
  for (int i = 0; i < 16; i++) if (c->movies_used[i] || c->systems_used[i] || c->pending_systems[i]) any = true;
  if (c->closed || c->restoring || any) js_error("Cinematic restore requires an unpublished empty owner");
  restore_state state;
  memset(&state, 0, sizeof state);
  state.c = c;
  qa_cleanup node;
  qa_cleanup_push(&node, restore_state_release, &state);
  qa_save_reader_init(&state.r, value, "q3-cinematics");
  state.has_r = true;
  restore_read(&state);
  bool has_restore = c->has_system && c->system.restore != NULL;
  if (state.system_rows != 0 && !has_restore)
    js_error("System cinematic restore requires its attached client transition checkpoint");
  if (c->mixer.restore_stream_checkpoint == NULL && state.movie_rows != 0)
    js_error("Cinematic mixer cannot restore its owned PCM lanes");
  c->restoring = true;
  qa_caught_error caught;
  bool restored = qa_try(restore_body, &state, &caught);
  if (!restored) {
    /* for (const index of [...movies.keys(), ...systems.keys()]) stop(index); sources.clear(); */
    int movies[16], systems[16];
    size_t movie_count = movies_in_order(c, movies), system_count = systems_in_order(c, systems);
    for (size_t i = 0; i < movie_count; i++) qa_app_application_q3_cinematics_stop(c, movies[i]);
    for (size_t i = 0; i < system_count; i++) qa_app_application_q3_cinematics_stop(c, systems[i]);
    clear_sources(c);
  }
  c->restoring = false;
  qa_cleanup_pop(&node);
  restore_state_release(&state);
  if (!restored) qa_rethrow(&caught);
}
double qa_app_application_q3_cinematics_play_guest(qa_app_application_q3_cinematics *c,
                                                   const char *path, qa_rect rect, double bits) {
  if (c->restoring) js_error("Cinematic restore has not completed");
  int32_t flag_bits = qa_js_to_int32(bits);
  if ((flag_bits & 1) != 0) {
    if (c->closed) js_error("UI cinematics are closed");
    if (!c->has_system) js_error("System cinematic requires an attached client transition owner");
    int index = 0;
    while (index < 16 && !slot_free(c, index)) index++;
    if (index >= 16) js_error("CIN_HandleForVideo: none free");
    system_open_ctx open = {c, {path, (flag_bits & 2) != 0, (flag_bits & 4) != 0, (flag_bits & 8) != 0}, NULL, false, NULL, {0}};
    create_system(c, index, &open);
    return index;
  }
  qa_app_ui_cinematic_asset asset;
  if (!prepare_mode(c, path, false, &asset)) return -1;
  if (c->closed) js_error("UI cinematic loaded after close");
  source_entry *prepared = find_source(c, asset.path);
  int order[16];
  size_t count = movies_in_order(c, order);
  for (size_t i = 0; i < count; i++)
    if (prepared != NULL && strcmp(c->movies[order[i]].path, prepared->source.source != NULL ? prepared->source.source : "") == 0)
      return order[i];
  double index = 0;
  cinematic_mode mode = {(flag_bits & 2) != 0, (flag_bits & 4) != 0, (flag_bits & 8) != 0, (flag_bits & 16) != 0, rect};
  if (!play_mode(c, asset, mode, &index)) js_error("CIN_HandleForVideo: none free");
  return index;
}
double qa_app_application_q3_cinematics_run_guest(qa_app_application_q3_cinematics *c, double handle) {
  int index = 0;
  if (!slot_of(handle, &index)) return 2;
  if (c->systems_used[index]) {
    qa_app_system_cinematic_handle *system = &c->systems[index];
    if (system->refresh_status != NULL) system->refresh_status(system->ctx, system);
    if (strcmp(system->status, "held") == 0) return 0;
    if (strcmp(system->status, "playing") == 0 || strcmp(system->status, "paused") == 0) return 1;
    qa_app_application_q3_cinematics_stop(c, handle);
    return 2;
  }
  if (!c->movies_used[index]) return 2;
  movie_tick(&c->movies[index]);
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
static void system_skip(void *ctx) {
  qa_app_system_cinematic_handle *system = ctx;
  system->skip(system->ctx);
}
double qa_app_application_q3_cinematics_stop_guest(qa_app_application_q3_cinematics *c, double handle) {
  int index = 0;
  if (!slot_of(handle, &index)) return 2;
  c->pending_systems[index] = false;
  if (c->systems_used[index]) {
    qa_app_system_cinematic_handle system = c->systems[index];
    double installed = c->system_order[index];
    qa_caught_error caught;
    bool skipped = qa_try(system_skip, &system, &caught);
    /* finally: if (systems.get(handle) === system) systems.delete(handle) */
    if (c->systems_used[index] && c->system_order[index] == installed) c->systems_used[index] = false;
    if (!skipped) qa_rethrow(&caught);
  } else qa_app_application_q3_cinematics_stop(c, handle);
  return 2;
}
void qa_app_application_q3_cinematics_draw_guest(qa_app_application_q3_cinematics *c, double handle, qa_draw2d *draw) {
  int index = 0;
  if (slot_of(handle, &index) && c->movies_used[index])
    qa_app_application_q3_cinematics_draw(c, handle, c->movies[index].rect, draw);
}
void qa_app_application_q3_cinematics_set_extents(qa_app_application_q3_cinematics *c, double handle, qa_rect rect) {
  int index = 0;
  if (slot_of(handle, &index) && c->movies_used[index]) c->movies[index].rect = rect;
}
void qa_app_application_q3_cinematics_run(qa_app_application_q3_cinematics *c, double handle) {
  int index = 0;
  if (!slot_of(handle, &index) || !c->movies_used[index]) return;
  movie_tick(&c->movies[index]);
}
void qa_app_application_q3_cinematics_draw(qa_app_application_q3_cinematics *c, double handle, qa_rect rect, qa_draw2d *draw) {
  int index = 0;
  if (!slot_of(handle, &index) || !c->movies_used[index]) return;
  if (!c->movies[index].playback.has_picture) return; /* playback.currentFrame === null */
  qa_draw2d_draw_pic(draw, rect, &c->movies[index].picture);
}
void qa_app_application_q3_cinematics_stop(qa_app_application_q3_cinematics *c, double handle) {
  int index = 0;
  if (!slot_of(handle, &index)) return;
  c->pending_systems[index] = false;
  if (c->systems_used[index]) {
    qa_app_system_cinematic_handle system = c->systems[index];
    c->systems_used[index] = false;
    system.stop(system.ctx);
    return;
  }
  if (!c->movies_used[index]) return;
  movie_entry *movie = &c->movies[index];
  /* const release = movie.image.release(); movie.playback.close(); images.release(release.image) */
  qa_image_resource_operation release;
  bool has_release = movie->has_image && qa_cinematic_image_release(&movie->image, &release);
  qa_cinematic_playback_destroy(&movie->playback);
  if (has_release) qa_scene_image_registry_release(images(c), release.image);
  c->movies_used[index] = false;
  movie_release(movie);
}
void qa_app_application_q3_cinematics_close(qa_app_application_q3_cinematics *c) {
  if (c->closed) return;
  c->closed = true;
  for (int i = 0; i < 16; i++) c->pending_systems[i] = false;
  int order[16];
  size_t count = movies_in_order(c, order);
  for (size_t i = 0; i < count; i++) qa_app_application_q3_cinematics_stop(c, order[i]);
  count = systems_in_order(c, order);
  for (size_t i = 0; i < count; i++) qa_app_application_q3_cinematics_stop(c, order[i]);
  clear_sources(c);
}
