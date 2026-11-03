/* Port of src/app/bootstrap/campaign-cinematic.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/campaign_cinematic.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/assets.h"
#include "qa/app/bootstrap/audio.h"
#include "qa/app/bootstrap/renderer.h"
#include "qa/audio/engine.h"
#include "qa/audio/streams.h"
#include "qa/content/mods/declaration_writer.h"
#include "qa/content/mounts.h"
#include "qa/foundation/js_case.h"
#include "qa/foundation/support.h"
#include "qa/media/audio.h"
#include "qa/media/playback.h"
#include "qa/media/presentation.h"
#include "qa/media/still.h"
#include "qa/persistence/recipe.h"
#include "qa/persistence/save_image.h"
#include "qa/platform/sdl.h"
#include "qa/render/scene/resources.h"

static long next_cinematic;
enum { STATE_PREPARED, STATE_ACTIVE, STATE_CLOSED };

struct qa_app_campaign_cinematic {
  int state;
  /* request (name owned) */
  char *name;
  bool loop, hold, silent;
  char *path;
  uint8_t *bytes;
  size_t byte_length;
  qa_resolved_resource_reference resource; /* owned copy of resource.reference */
  qa_cinematic_source source;
  qa_cinematic_playback playback;
  bool has_playback;
  qa_fullscreen_cinematic movie;
  bool has_movie;
  qa_app_native_renderer *renderer;
  qa_scene_image_registry *images;
  qa_unified_audio *engine;
  qa_cinematic_audio_binding stream;
  char lane[64];
  double clock;
  qa_app_screen_cinematic_captions *captions;
  bool (*current)(void *ctx);
  void *current_ctx;
  /* startAudio state */
  bool restored, audio_started, ready;
  qa_cinematic_audio *initial_audio;
  size_t initial_count, initial_cap;
  qa_save_value *pending_pcm; /* owned; NULL = TS null */
};

static _Noreturn void fail(const char *format, const char *argument) {
  qa_raise("Error", 0, 0, format, argument);
}
static char *dup_text(const char *text) {
  size_t length = strlen(text) + 1;
  char *copy = malloc(length);
  if (copy == NULL) qa_range_error("Out of memory");
  memcpy(copy, text, length);
  return copy;
}
static bool ends_ci(const char *text, const char *suffix) {
  size_t t = strlen(text), s = strlen(suffix);
  if (t < s) return false;
  for (size_t i = 0; i < s; i++)
    if (tolower((unsigned char)text[t - s + i]) != suffix[i]) return false;
  return true;
}
static qa_save_value *copy_value(const qa_save_value *value) {
  qa_save_value *copy = NULL;
  switch (value->kind) {
    case QA_SAVE_UNDEFINED: copy = qa_save_value_undefined(); break;
    case QA_SAVE_NULL: copy = qa_save_value_null(); break;
    case QA_SAVE_BOOLEAN: copy = qa_save_value_boolean(value->boolean); break;
    case QA_SAVE_NUMBER: copy = qa_save_value_number(value->number); break;
    case QA_SAVE_STRING: copy = qa_save_value_string(value->text); break;
    default:
      copy = calloc(1, sizeof *copy);
      if (copy == NULL) break;
      copy->kind = value->kind;
      if (value->kind == QA_SAVE_BIGINT) copy->text = dup_text(value->text);
      if (value->kind == QA_SAVE_BYTES) {
        copy->bytes = malloc(value->byte_length ? value->byte_length : 1);
        if (copy->bytes == NULL) qa_range_error("Out of memory");
        memcpy(copy->bytes, value->bytes, value->byte_length);
        copy->byte_length = value->byte_length;
      }
      if (value->kind == QA_SAVE_ARRAY) {
        copy->items = calloc(value->item_count ? value->item_count : 1, sizeof *copy->items);
        if (copy->items == NULL) qa_range_error("Out of memory");
        for (size_t i = 0; i < value->item_count; i++) copy->items[i] = copy_value(value->items[i]);
        copy->item_count = value->item_count;
      }
      if (value->kind == QA_SAVE_OBJECT) {
        copy->entries = calloc(value->entry_count ? value->entry_count : 1, sizeof *copy->entries);
        if (copy->entries == NULL) qa_range_error("Out of memory");
        for (size_t i = 0; i < value->entry_count; i++) {
          copy->entries[i].key = dup_text(value->entries[i].key);
          copy->entries[i].value = copy_value(value->entries[i].value);
        }
        copy->entry_count = value->entry_count;
      }
  }
  if (copy == NULL) qa_range_error("Out of memory");
  return copy;
}

/* ---- the ApplicationAudio engine as the cinematic mixer ---- */
static void mixer_queue(void *ctx, const qa_audio_stream_target *target, const qa_stream_pcm *pcm) {
  qa_unified_audio_queue_stream(ctx, *target, pcm);
}
static void mixer_stop(void *ctx, const char *id) { qa_unified_audio_stop_stream(ctx, id); }
static void mixer_pause(void *ctx, const char *id, bool paused) { qa_unified_audio_pause_stream(ctx, id, paused); }
static qa_save_value *mixer_capture(void *ctx, const char *id) { return qa_unified_audio_capture_stream_checkpoint(ctx, id); }
static void mixer_restore(void *ctx, const qa_audio_stream_target *target, const qa_save_value *value) {
  qa_unified_audio_restore_stream_checkpoint(ctx, *target, value);
}

/* ---- CinematicPlayback options ---- */
static double clock_sample(void *ctx) { return ((qa_app_campaign_cinematic *)ctx)->clock; }
static void copy_audio_block(qa_cinematic_audio *out, const qa_cinematic_audio *block) {
  *out = *block;
  size_t bytes = block->sample_count * (block->is_u8 ? sizeof(uint8_t) : sizeof(int16_t));
  void *samples = malloc(bytes ? bytes : 1);
  if (samples == NULL) qa_range_error("Out of memory");
  memcpy(samples, block->is_u8 ? (const void *)block->samples_u8 : (const void *)block->samples_s16, bytes);
  if (block->is_u8) out->samples_u8 = samples;
  else out->samples_s16 = samples;
}
static void push_initial(qa_app_campaign_cinematic *c, const qa_cinematic_audio *block) {
  if (c->initial_count == c->initial_cap) {
    c->initial_cap = c->initial_cap ? c->initial_cap * 2 : 8;
    c->initial_audio = realloc(c->initial_audio, c->initial_cap * sizeof *c->initial_audio);
    if (c->initial_audio == NULL) qa_range_error("Out of memory");
  }
  copy_audio_block(&c->initial_audio[c->initial_count++], block);
}
static void free_audio_block(qa_cinematic_audio *block) {
  free(block->is_u8 ? (void *)block->samples_u8 : (void *)block->samples_s16);
}
static qa_cinematic_target lane_target(qa_app_campaign_cinematic *c) {
  qa_cinematic_target target;
  memset(&target, 0, sizeof target);
  target.kind = QA_CINEMATIC_TARGET_MATERIAL;
  target.id = c->lane;
  return target;
}
static void on_audio(void *ctx, const qa_cinematic_audio *block, qa_cinematic_target target) {
  qa_app_campaign_cinematic *c = ctx;
  (void)target; /* TS forwards { kind: "material", id: lane }, not the playback target */
  if (c->ready) qa_cinematic_audio_on_audio(&c->stream, block, lane_target(c));
  else push_initial(c, block);
}
static void on_audio_reset(void *ctx, qa_cinematic_target target) {
  qa_app_campaign_cinematic *c = ctx;
  if (c->ready) qa_cinematic_audio_on_reset(&c->stream, target);
}
static void on_audio_pause(void *ctx, bool paused, qa_cinematic_target target) {
  qa_app_campaign_cinematic *c = ctx;
  if (c->ready) qa_cinematic_audio_on_pause(&c->stream, paused, target);
}
static void on_complete(void *ctx, qa_cinematic_end_reason reason, qa_cinematic_target target) {
  (void)ctx; (void)reason; (void)target; /* TS onComplete: () => undefined */
}

/* images.allocate(width, height, origin) for the FullscreenCinematic's reallocation. */
static qa_renderer_image allocate_image(void *ctx, int64_t width, int64_t height, bool source_is_resource,
    qa_resolved_resource_reference source_resource, const char *source_name) {
  qa_app_campaign_cinematic *c = ctx;
  qa_renderer_image source;
  memset(&source, 0, sizeof source);
  source.source_is_resource = source_is_resource;
  source.source_resource = source_resource;
  source.source_name = source_name;
  return qa_scene_image_registry_allocate(c->images, (double)width, (double)height, source)->image;
}

static void assert_current(qa_app_campaign_cinematic *c) {
  if (!c->current(c->current_ctx)) qa_raise("Error", 0, 0, "Cinematic preparation belongs to a retired request");
}

static void free_cinematic(qa_app_campaign_cinematic *c) {
  if (c->has_movie) qa_fullscreen_cinematic_destroy(&c->movie);
  if (c->has_playback) qa_cinematic_playback_destroy(&c->playback);
  if (c->source.rgba_owned) qa_cinematic_source_dispose(&c->source);
  for (size_t i = 0; i < c->initial_count; i++) free_audio_block(&c->initial_audio[i]);
  free(c->initial_audio);
  if (c->pending_pcm != NULL) qa_save_value_free(c->pending_pcm);
  qa_resolved_resource_release(&c->resource);
  free(c->bytes);
  free(c->path);
  free(c->name);
  free(c);
}
static void free_cleanup(void *ctx) { free_cinematic(ctx); }

/* TS readAudio(r). */
static qa_cinematic_audio read_audio(const qa_save_reader *r) {
  qa_save_reader field;
  qa_cinematic_audio block;
  memset(&block, 0, sizeof block);
  qa_save_reader_field(r, "signed", &field);
  bool is_signed = qa_save_reader_boolean(&field);
  qa_save_reader_destroy(&field);
  static const double channel_choices[] = {1, 2};
  qa_save_reader_field(r, "channels", &field);
  block.channels = (int)qa_save_reader_choice_number(&field, channel_choices, 2);
  qa_save_reader_destroy(&field);
  qa_save_reader samples;
  qa_save_reader_field(r, "samples", &samples);
  size_t count = qa_save_reader_list_count(&samples);
  int16_t *s16 = is_signed ? malloc((count ? count : 1) * sizeof *s16) : NULL;
  uint8_t *u8 = is_signed ? NULL : malloc(count ? count : 1);
  if (is_signed ? s16 == NULL : u8 == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < count; i++) {
    qa_save_reader sample;
    qa_save_reader_item(&samples, i, &sample);
    double value = qa_save_reader_integer_min(&sample, is_signed ? -32768 : 0);
    if (value > (is_signed ? 32767 : 255)) qa_save_reader_fail(&sample, "PCM sample outside source width");
    if (is_signed) s16[i] = (int16_t)value;
    else u8[i] = (uint8_t)value;
    qa_save_reader_destroy(&sample);
  }
  qa_save_reader_destroy(&samples);
  if (count % (size_t)block.channels != 0) qa_save_reader_fail(r, "PCM samples are not complete frames");
  block.is_u8 = !is_signed;
  block.samples_s16 = s16;
  block.samples_u8 = u8;
  block.sample_count = count;
  qa_save_reader_field(r, "sampleRate", &field);
  block.sample_rate = (int64_t)qa_save_reader_integer_min(&field, 1);
  qa_save_reader_destroy(&field);
  qa_save_reader_field(r, "sourceSample", &field);
  block.source_sample = (int64_t)qa_save_reader_integer_min(&field, 0);
  qa_save_reader_destroy(&field);
  qa_save_reader_field(r, "sourceTime", &field);
  block.source_time = qa_save_reader_finite(&field);
  qa_save_reader_destroy(&field);
  qa_save_reader_field(r, "time", &field);
  block.time = qa_save_reader_finite(&field);
  qa_save_reader_destroy(&field);
  qa_save_reader_field(r, "loop", &field);
  block.loop = (int64_t)qa_save_reader_integer_min(&field, 0);
  qa_save_reader_destroy(&field);
  qa_save_reader_field(r, "resetStream", &field);
  block.reset_stream = qa_save_reader_boolean(&field);
  qa_save_reader_destroy(&field);
  return block;
}

static bool same_resource(const qa_resolved_resource_reference *left, const qa_resolved_resource_reference *right) {
  qa_save_value *a = qa_save_resource_reference_value(left), *b = qa_save_resource_reference_value(right);
  if (a == NULL || b == NULL) qa_range_error("Out of memory");
  char *ja = qa_json_stringify_value(a, NULL), *jb = qa_json_stringify_value(b, NULL);
  bool same = strcmp(ja, jb) == 0;
  free(ja);
  free(jb);
  qa_save_value_free(a);
  qa_save_value_free(b);
  return same;
}

/* TS prepareOwned. saved NULL is TS undefined. */
static qa_app_campaign_cinematic *prepare_images(qa_app_screen_cinematic_request request, qa_content_mounted *mounts,
    qa_scene_image_registry *images, qa_unified_audio *engine, qa_app_native_renderer *renderer, qa_seat_id seat,
    bool (*current)(void *ctx), void *current_ctx, qa_app_screen_cinematic_captions *captions, const qa_save_reader *saved);
static qa_app_campaign_cinematic *prepare_owned(qa_app_screen_cinematic_request request, qa_content_mounted *mounts,
    qa_app_application_assets *assets, qa_app_application_audio *audio, qa_app_native_renderer *renderer, qa_seat_id seat,
    bool (*current)(void *ctx), void *current_ctx, qa_app_screen_cinematic_captions *captions, const qa_save_reader *saved) {
  return prepare_images(request, mounts, qa_app_application_assets_images(assets), qa_app_application_audio_engine(audio),
      renderer, seat, current, current_ctx, captions, saved);
}
/* TS CampaignCinematic.prepare(request, { mounts }, { images }, { engine }, ...): the members TS reads. */
qa_app_campaign_cinematic *qa_app_campaign_cinematic_prepare_engine(qa_app_screen_cinematic_request request,
    const qa_content_mounted *mounts, qa_scene_image_registry *images, qa_unified_audio *engine,
    qa_app_native_renderer *renderer, qa_seat_id seat, bool (*current)(void *ctx), void *current_ctx,
    qa_app_screen_cinematic_captions *captions) {
  return prepare_images(request, (qa_content_mounted *)mounts, images, engine, renderer, seat, current, current_ctx, captions, NULL);
}
static qa_app_campaign_cinematic *prepare_images(qa_app_screen_cinematic_request request, qa_content_mounted *mounts,
    qa_scene_image_registry *images, qa_unified_audio *engine, qa_app_native_renderer *renderer, qa_seat_id seat,
    bool (*current)(void *ctx), void *current_ctx, qa_app_screen_cinematic_captions *captions, const qa_save_reader *saved) {
  qa_app_campaign_cinematic *c = calloc(1, sizeof *c);
  if (c == NULL) qa_range_error("Out of memory");
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, free_cleanup, c);
  c->current = current;
  c->current_ctx = current_ctx;
  c->name = dup_text(request.name);
  c->loop = request.loop;
  c->hold = request.hold;
  c->silent = request.silent;
  c->renderer = renderer;
  c->images = images;
  c->engine = engine;
  c->captions = captions;
  assert_current(c);
  /* selected = /\.[^/]+$/.test(name) ? name : `${name}.roq` */
  const char *slash = strrchr(request.name, '/');
  const char *dot = strrchr(request.name, '.');
  bool has_extension = dot != NULL && (slash == NULL || dot > slash) && dot[1] != '\0';
  size_t selected_size = strlen(request.name) + 5;
  char *selected = malloc(selected_size);
  if (selected == NULL) qa_range_error("Out of memory");
  snprintf(selected, selected_size, has_extension ? "%s" : "%s.roq", request.name);
  c->path = selected;
  bool invalid = selected[0] == '/' || strchr(selected, '\\') != NULL;
  for (const char *part = selected; !invalid;) {
    const char *end = strchr(part, '/');
    size_t length = end != NULL ? (size_t)(end - part) : strlen(part);
    if (length == 0 || (length == 1 && part[0] == '.') || (length == 2 && part[0] == '.' && part[1] == '.')) invalid = true;
    if (end == NULL) break;
    part = end + 1;
  }
  if (invalid) qa_raise("Error", 0, 0, "Invalid cinematic resource path");
  if (strncmp(selected, "video/", 6) != 0 && strncmp(selected, "pics/", 5) != 0) {
    size_t size = strlen(selected) + 7;
    char *path = malloc(size);
    if (path == NULL) qa_range_error("Out of memory");
    snprintf(path, size, "%s/%s", ends_ci(selected, ".pcx") ? "pics" : "video", selected);
    free(selected);
    c->path = path;
  }
  qa_content_opened_resource resource;
  bool found = qa_content_mounted_open(mounts, c->path, NULL, NULL, &resource);
  if (found) { c->bytes = resource.bytes; c->byte_length = resource.byte_length; }
  assert_current(c);
  if (!found && ends_ci(c->path, ".cin")) {
    memcpy(c->path + strlen(c->path) - 3, "ogv", 3);
    found = qa_content_mounted_open(mounts, c->path, NULL, NULL, &resource);
    if (found) { c->bytes = resource.bytes; c->byte_length = resource.byte_length; }
    assert_current(c);
  }
  if (!found) fail("Missing campaign cinematic: %s", c->path);
  if (!qa_resolved_resource_copy(resource.reference, &c->resource)) qa_range_error("Out of memory");
  if (saved != NULL) {
    qa_save_reader field;
    qa_save_reader_field(saved, "resource", &field);
    qa_resolved_resource_reference read = qa_read_resource(&field);
    bool same = same_resource(&read, &c->resource);
    qa_resolved_resource_release(&read);
    if (!same) qa_save_reader_fail(saved, "cinematic resource changed");
    qa_save_reader_destroy(&field);
  }
  const char *last_dot = strrchr(c->path, '.');
  char *extension = qa_js_to_lower_case(last_dot != NULL ? last_dot + 1 : c->path);
  if (extension == NULL) qa_range_error("Out of memory");
  qa_cleanup extension_cleanup;
  qa_cleanup_push(&extension_cleanup, free, extension);
  if (strcmp(extension, "cin") != 0 && strcmp(extension, "roq") != 0 && strcmp(extension, "ogv") != 0 && strcmp(extension, "pcx") != 0)
    fail("Unsupported cinematic format: %s", extension);
  if (strcmp(extension, "pcx") == 0) {
    if (!qa_cinematic_pcx(c->bytes, c->byte_length, c->path, &c->source)) qa_range_error("Out of memory");
  } else {
    qa_cinematic_format format = strcmp(extension, "cin") == 0 ? QA_CINEMATIC_FORMAT_CIN
        : strcmp(extension, "roq") == 0 ? QA_CINEMATIC_FORMAT_ROQ : QA_CINEMATIC_FORMAT_OGV;
    c->source = qa_cinematic_bytes(format, c->bytes, c->byte_length, c->path);
  }
  qa_cleanup_pop(&extension_cleanup);
  free(extension);
  int64_t width = 0, height = 0;
  if (!qa_cinematic_dimensions(&c->source, &width, &height)) qa_range_error("Out of memory");
  snprintf(c->lane, sizeof c->lane, "campaign-cinematic:%ld", ++next_cinematic);
  qa_cinematic_mixer mixer = {c->engine, mixer_queue, mixer_stop, mixer_pause, mixer_capture, mixer_restore};
  c->stream = qa_cinematic_audio_bind(mixer, c->lane, 1);
  qa_save_reader field;
  c->clock = 0;
  if (saved != NULL) {
    qa_save_reader_field(saved, "clock", &field);
    c->clock = qa_save_reader_finite(&field);
    qa_save_reader_destroy(&field);
    qa_save_reader list;
    qa_save_reader_field(saved, "initialAudio", &list);
    size_t count = qa_save_reader_list_count(&list);
    for (size_t i = 0; i < count; i++) {
      qa_save_reader item;
      qa_save_reader_item(&list, i, &item);
      qa_cinematic_audio block = read_audio(&item);
      push_initial(c, &block);
      free_audio_block(&block);
      qa_save_reader_destroy(&item);
    }
    qa_save_reader_destroy(&list);
    qa_save_reader_field(saved, "audioStarted", &field);
    c->audio_started = qa_save_reader_boolean(&field);
    qa_save_reader_destroy(&field);
    qa_save_reader_field(saved, "pcm", &field);
    const qa_save_value *pcm = field.value;
    if (pcm != NULL && pcm->kind != QA_SAVE_NULL) {
      qa_save_reader pcm_reader, rate;
      qa_save_reader_init(&pcm_reader, pcm, "campaign-cinematic.pcm");
      qa_save_reader_field(&pcm_reader, "outputRate", &rate);
      int64_t output_rate = (int64_t)qa_save_reader_integer_min(&rate, 1);
      qa_save_reader_destroy(&rate);
      qa_save_reader_destroy(&pcm_reader);
      qa_raw_audio_stream check;
      if (!qa_raw_audio_stream_restore_checkpoint(&check, pcm, output_rate)) qa_range_error("Out of memory");
      qa_raw_audio_stream_destroy(&check);
      c->pending_pcm = copy_value(pcm);
    }
    qa_save_reader_destroy(&field);
    c->restored = true;
  }
  if (c->clock < 0) qa_raise("Error", 0, 0, "Negative cinematic clock");
  /* The ApplicationAudio engine always has restoreStreamCheckpoint (TS `restoreStream === undefined` cannot hold). */
  qa_cinematic_options options;
  memset(&options, 0, sizeof options);
  options.clock = (qa_media_clock){c, clock_sample};
  options.target.kind = QA_CINEMATIC_TARGET_SEAT;
  options.target.seat = seat;
  options.loop = c->loop;
  options.hold = c->hold;
  options.silent = c->silent;
  options.on_audio = on_audio;
  options.on_audio_ctx = c;
  options.on_audio_reset = on_audio_reset;
  options.on_audio_reset_ctx = c;
  options.on_audio_pause = on_audio_pause;
  options.on_audio_pause_ctx = c;
  options.on_complete = on_complete;
  options.on_complete_ctx = c;
  const qa_save_value *playback_checkpoint = NULL;
  if (saved != NULL) {
    qa_save_reader_field(saved, "playback", &field);
    playback_checkpoint = field.value;
  }
  if (!qa_cinematic_playback_init(&c->playback, &c->source, &options, playback_checkpoint)) qa_range_error("Out of memory");
  if (saved != NULL) qa_save_reader_destroy(&field);
  c->has_playback = true;
  /* try { ... } catch { playback.close(); throw } */
  if (captions != NULL) captions->prepare(captions->ctx, c->path);
  assert_current(c);
  qa_renderer_image origin;
  memset(&origin, 0, sizeof origin);
  origin.source_is_resource = true;
  origin.source_resource = c->resource;
  qa_renderer_image image = qa_scene_image_registry_allocate(c->images, (double)width, (double)height, origin)->image;
  qa_fullscreen_cinematic_init(&c->movie, &c->playback, image, allocate_image, c);
  c->has_movie = true;
  if (saved != NULL) {
    qa_save_reader_field(saved, "fullscreen", &field);
    qa_fullscreen_cinematic_restore_checkpoint(&c->movie, field.value);
    qa_save_reader_destroy(&field);
  }
  qa_cleanup_pop(&cleanup);
  return c;
}
qa_app_campaign_cinematic *qa_app_campaign_cinematic_prepare_content(qa_app_screen_cinematic_request request,
    const qa_content_mounted *mounts, qa_app_application_assets *assets, qa_app_application_audio *audio,
    qa_app_native_renderer *renderer, qa_seat_id seat, bool (*current)(void *ctx), void *current_ctx,
    qa_app_screen_cinematic_captions *captions) {
  return prepare_owned(request, (qa_content_mounted *)mounts, assets, audio, renderer, seat, current, current_ctx, captions, NULL);
}

qa_app_campaign_cinematic *qa_app_campaign_cinematic_restore_content(const qa_save_value *value,
    const qa_content_mounted *mounts, qa_app_application_assets *assets, qa_app_application_audio *audio,
    qa_app_native_renderer *renderer, qa_seat_id seat, bool (*current)(void *ctx), void *current_ctx,
    qa_app_screen_cinematic_captions *captions) {
  qa_save_reader reader, field, saved;
  qa_save_reader_init(&reader, value, "campaign-cinematic");
  qa_save_reader_field(&reader, "version", &field);
  qa_save_reader_literal_number(&field, 1);
  qa_save_reader_destroy(&field);
  qa_save_reader_field(&reader, "request", &saved);
  qa_app_screen_cinematic_request request;
  qa_save_reader_field(&saved, "name", &field);
  char *name = dup_text(qa_save_reader_string(&field));
  qa_save_reader_destroy(&field);
  qa_cleanup name_cleanup;
  qa_cleanup_push(&name_cleanup, free, name);
  qa_save_reader_field(&saved, "loop", &field);
  request.loop = qa_save_reader_boolean(&field);
  qa_save_reader_destroy(&field);
  qa_save_reader_field(&saved, "hold", &field);
  request.hold = qa_save_reader_boolean(&field);
  qa_save_reader_destroy(&field);
  qa_save_reader_field(&saved, "silent", &field);
  request.silent = qa_save_reader_boolean(&field);
  qa_save_reader_destroy(&field);
  qa_save_reader_destroy(&saved);
  request.name = name;
  qa_app_campaign_cinematic *cinematic = prepare_owned(request, (qa_content_mounted *)mounts, assets, audio, renderer, seat, current,
      current_ctx, captions, &reader);
  qa_cleanup_pop(&name_cleanup);
  free(name);
  qa_save_reader_destroy(&reader);
  return cinematic;
}

/* The startAudio closure. */
static void start_audio(qa_app_campaign_cinematic *c) {
  if (!c->audio_started) qa_unified_audio_stop_all(c->engine);
  if (c->restored) {
    qa_audio_stream_target target;
    memset(&target, 0, sizeof target);
    target.id = c->lane;
    target.gain = 1;
    target.audience.kind = QA_AUDIO_AUDIENCE_WORLD;
    qa_unified_audio_restore_stream_checkpoint(c->engine, target, c->pending_pcm);
  }
  c->ready = true;
  for (size_t i = 0; i < c->initial_count; i++) {
    qa_cinematic_audio_on_audio(&c->stream, &c->initial_audio[i], lane_target(c));
    free_audio_block(&c->initial_audio[i]);
  }
  c->initial_count = 0;
  if (qa_cinematic_playback_status(&c->playback) == QA_CINEMATIC_PAUSED)
    qa_cinematic_audio_on_pause(&c->stream, true, c->playback.target);
}

static bool is_current(qa_app_campaign_cinematic *c) { return c->current(c->current_ctx); }

static qa_save_value *audio_block_value(const qa_cinematic_audio *block) {
  qa_save_value *o = qa_save_value_object(), *samples = qa_save_value_array();
  if (o == NULL || samples == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < block->sample_count; i++)
    if (!qa_save_value_push(samples, qa_save_value_number(block->is_u8 ? block->samples_u8[i] : block->samples_s16[i])))
      qa_range_error("Out of memory");
  bool ok = qa_save_value_set(o, "samples", samples) && qa_save_value_set(o, "channels", qa_save_value_number(block->channels))
      && qa_save_value_set(o, "sampleRate", qa_save_value_number((double)block->sample_rate))
      && qa_save_value_set(o, "sourceSample", qa_save_value_number((double)block->source_sample))
      && qa_save_value_set(o, "sourceTime", qa_save_value_number(block->source_time))
      && qa_save_value_set(o, "time", qa_save_value_number(block->time))
      && qa_save_value_set(o, "loop", qa_save_value_number((double)block->loop))
      && qa_save_value_set(o, "resetStream", qa_save_value_boolean(block->reset_stream))
      && qa_save_value_set(o, "signed", qa_save_value_boolean(!block->is_u8));
  if (!ok) qa_range_error("Out of memory");
  return o;
}
/* The captureOwned closure. */
static qa_save_value *capture_owned(qa_app_campaign_cinematic *c) {
  qa_save_value *o = qa_save_value_object(), *request = qa_save_value_object(), *initial = qa_save_value_array();
  if (o == NULL || request == NULL || initial == NULL) qa_range_error("Out of memory");
  bool ok = qa_save_value_set(request, "name", qa_save_value_string(c->name)) && qa_save_value_set(request, "loop", qa_save_value_boolean(c->loop))
      && qa_save_value_set(request, "hold", qa_save_value_boolean(c->hold)) && qa_save_value_set(request, "silent", qa_save_value_boolean(c->silent));
  for (size_t i = 0; ok && i < c->initial_count; i++) ok = qa_save_value_push(initial, audio_block_value(&c->initial_audio[i]));
  qa_save_value *pcm = c->ready ? qa_unified_audio_capture_stream_checkpoint(c->engine, c->lane)
                                : c->pending_pcm != NULL ? copy_value(c->pending_pcm) : NULL;
  if (pcm == NULL) pcm = qa_save_value_null();
  ok = ok && qa_save_value_set(o, "version", qa_save_value_number(1)) && qa_save_value_set(o, "request", request)
      && qa_save_value_set(o, "resource", qa_save_resource_reference_value(&c->resource))
      && qa_save_value_set(o, "clock", qa_save_value_number(c->clock))
      && qa_save_value_set(o, "audioStarted", qa_save_value_boolean(c->ready || c->audio_started))
      && qa_save_value_set(o, "playback", qa_cinematic_playback_capture_checkpoint(&c->playback))
      && qa_save_value_set(o, "fullscreen", qa_fullscreen_cinematic_capture_checkpoint(&c->movie))
      && qa_save_value_set(o, "initialAudio", initial) && qa_save_value_set(o, "pcm", pcm);
  if (!ok) qa_range_error("Out of memory");
  return o;
}

qa_save_value *qa_app_campaign_cinematic_capture_checkpoint(qa_app_campaign_cinematic *cinematic) {
  if (cinematic->state == STATE_CLOSED || !is_current(cinematic)) qa_raise("Error", 0, 0, "Cinematic checkpoint belongs to a retired request");
  return capture_owned(cinematic);
}
void qa_app_campaign_cinematic_activate(qa_app_campaign_cinematic *cinematic) {
  if (cinematic->state == STATE_CLOSED || !is_current(cinematic)) qa_raise("Error", 0, 0, "Cinematic activation belongs to a retired request");
  if (cinematic->state == STATE_ACTIVE) return;
  cinematic->state = STATE_ACTIVE;
  qa_cinematic_status status = qa_cinematic_playback_status(&cinematic->playback);
  if (status != QA_CINEMATIC_ENDED && status != QA_CINEMATIC_STOPPED) start_audio(cinematic);
}
qa_cinematic_timeline qa_app_campaign_cinematic_timeline(qa_app_campaign_cinematic *cinematic) {
  return qa_cinematic_playback_timeline(&cinematic->playback);
}
const char *qa_app_campaign_cinematic_status(qa_app_campaign_cinematic *cinematic) {
  if (cinematic->state == STATE_CLOSED || !is_current(cinematic)) return qa_cinematic_status_name(QA_CINEMATIC_STOPPED);
  return qa_cinematic_status_name(qa_cinematic_playback_status(&cinematic->playback));
}
void qa_app_campaign_cinematic_pause(qa_app_campaign_cinematic *cinematic, bool paused) {
  if (cinematic->state != STATE_CLOSED && is_current(cinematic)) qa_cinematic_playback_pause(&cinematic->playback, paused);
}
void qa_app_campaign_cinematic_skip(qa_app_campaign_cinematic *cinematic) {
  if (cinematic->state != STATE_CLOSED && is_current(cinematic)) qa_cinematic_playback_skip(&cinematic->playback);
}
static bool is_press(const qa_seat_input_event *event) {
  return (event->kind != NULL && strcmp(event->kind, "key") == 0 && event->down && !event->repeat)
      || (event->kind != NULL && strcmp(event->kind, "mouse-button") == 0 && event->down)
      || (event->kind != NULL && strcmp(event->kind, "controller-button") == 0 && event->down);
}
bool qa_app_campaign_cinematic_input(qa_app_campaign_cinematic *cinematic, const qa_seat_input_event *event) {
  if (is_press(event) && qa_cinematic_playback_time_milliseconds(&cinematic->playback) > 1000) qa_app_campaign_cinematic_skip(cinematic);
  else if (strcmp(qa_app_campaign_cinematic_status(cinematic), "held") == 0 && cinematic->clock > 1000 && is_press(event))
    qa_app_campaign_cinematic_skip(cinematic);
  return true;
}
bool qa_app_campaign_cinematic_frame(qa_app_campaign_cinematic *cinematic, double elapsed_milliseconds, double sequence, bool console_open, bool menu_open) {
  if (cinematic->state != STATE_ACTIVE || !is_current(cinematic)) qa_raise("Error", 0, 0, "Fullscreen cinematic is not the active request");
  if (!isfinite(elapsed_milliseconds) || elapsed_milliseconds < 0) qa_raise("RangeError", 0, 0, "Invalid cinematic frame interval");
  cinematic->clock += elapsed_milliseconds;
  qa_sdl_size size = qa_sdl_window_drawable_size(qa_app_native_renderer_window(cinematic->renderer));
  qa_rect viewport = {0, 0, size.width, size.height};
  qa_fullscreen_cinematic_draw draw;
  if (!qa_fullscreen_cinematic_prepare(&cinematic->movie, viewport,
          console_open ? QA_MEDIA_FOCUS_CONSOLE : menu_open ? QA_MEDIA_FOCUS_MENU : QA_MEDIA_FOCUS_GAME, &draw))
    qa_range_error("Out of memory");
  if (!console_open && !menu_open) {
    size_t caption_count = 0;
    qa_render_command *captions = NULL;
    if (!draw.blank && cinematic->captions != NULL) {
      qa_cinematic_timeline timeline = qa_cinematic_playback_timeline(&cinematic->playback);
      qa_app_cinematic_caption_timeline view = {{timeline.source, timeline.source_time_milliseconds, qa_cinematic_status_name(timeline.status)},
          timeline.elapsed_milliseconds};
      captions = cinematic->captions->commands(cinematic->captions->ctx, &view, viewport, &caption_count);
    }
    size_t count = draw.command_count + caption_count + 2;
    qa_render_command *commands = calloc(count, sizeof *commands);
    if (commands == NULL) qa_range_error("Out of memory");
    commands[0].kind = QA_RENDER_COMMAND_DRAW_BUFFER;
    commands[0].buffer = "back";
    commands[0].clear = true;
    if (draw.command_count != 0) memcpy(commands + 1, draw.commands, draw.command_count * sizeof *commands);
    if (caption_count != 0) memcpy(commands + 1 + draw.command_count, captions, caption_count * sizeof *commands);
    commands[count - 1].kind = QA_RENDER_COMMAND_SWAP_BUFFERS;
    qa_render_frame frame = {cinematic->images->owner, sequence, commands, count};
    qa_app_native_renderer_execute(cinematic->renderer, frame);
    free(commands);
    free(captions);
    qa_fullscreen_cinematic_draw_complete(&draw);
  }
  qa_fullscreen_cinematic_draw_dispose(&draw);
  qa_unified_audio_pump(cinematic->engine, false, 0, 0);
  return strcmp(qa_app_campaign_cinematic_status(cinematic), "ended") == 0;
}
void qa_app_campaign_cinematic_close(qa_app_campaign_cinematic *cinematic, double sequence) {
  if (cinematic == NULL || cinematic->state == STATE_CLOSED) return;
  cinematic->state = STATE_CLOSED;
  qa_image_resource_operation operation;
  if (qa_fullscreen_cinematic_close(&cinematic->movie, &operation)) {
    qa_render_command command;
    memset(&command, 0, sizeof command);
    command.kind = QA_RENDER_COMMAND_IMAGE_RESOURCE;
    command.operation = operation;
    qa_render_frame frame = {cinematic->images->owner, sequence, &command, 1};
    qa_app_native_renderer_execute(cinematic->renderer, frame);
  }
}
void qa_app_campaign_cinematic_destroy(qa_app_campaign_cinematic *cinematic) {
  if (cinematic == NULL) return;
  qa_app_campaign_cinematic_close(cinematic, 0);
  free_cinematic(cinematic);
}
