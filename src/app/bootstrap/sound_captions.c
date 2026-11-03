/* Port of src/app/bootstrap/sound-captions.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/sound_captions.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/content/mounts.h"
#include "qa/foundation/support.h"
#include "qa/text/media_captions.h"
/* A catalog is heap-allocated: voices keep pointers to it while the map grows. */
typedef struct catalog { char *key; qa_content_mounted *mounts; qa_seat_media_captions captions; } catalog;
typedef struct voice {
  double id;
  qa_audio_voice_event event;
  catalog *captions; /* NULL is TS null */
  char *language;
  bool has_stop; double stop_sample;
} voice;
struct qa_app_seat_sound_captions {
  qa_seat_id seat;
  qa_unified_audio *audio;
  const char *(*language)(void *ctx); void *language_ctx;
  void (*warn)(void *ctx, const char *text); void *warn_ctx;
  int64_t subscription;
  voice *voices; size_t voice_count, voice_cap; /* TS Map<number, CaptionVoice>, insertion order */
  catalog **catalogs; size_t catalog_count, catalog_cap;
  bool closed;
};
static char *xdup(const char *s) {
  size_t n = strlen(s) + 1;
  char *r = malloc(n);
  if (r == NULL) qa_range_error("Out of memory");
  memcpy(r, s, n);
  return r;
}
static void warn(qa_app_seat_sound_captions *c, const char *text) {
  if (c->warn != NULL) c->warn(c->warn_ctx, text);
}
static void caption_warn(void *ctx, const char *text) { warn((qa_app_seat_sound_captions *)ctx, text); }
static void voice_delete(qa_app_seat_sound_captions *c, size_t index) {
  free(c->voices[index].language);
  memmove(&c->voices[index], &c->voices[index + 1], (c->voice_count - index - 1) * sizeof *c->voices);
  c->voice_count--;
}
static voice *voice_get(qa_app_seat_sound_captions *c, double id) {
  for (size_t i = 0; i < c->voice_count; i++) if (c->voices[i].id == id) return &c->voices[i];
  return NULL;
}
/* (await mounts.open(path))?.bytes ?? null */
static uint8_t *caption_read(void *ctx, const char *path, size_t *out_length) {
  catalog *entry = ctx;
  qa_content_opened_resource opened;
  if (!qa_content_mounted_open(entry->mounts, path, NULL, NULL, &opened)) return NULL;
  *out_length = opened.byte_length;
  if (opened.bytes == NULL) {
    uint8_t *empty = malloc(1);
    if (empty == NULL) qa_range_error("Out of memory");
    return empty;
  }
  return opened.bytes;
}
static void observe(void *ctx, const qa_audio_voice_event *event) {
  qa_app_seat_sound_captions *c = ctx;
  if (!qa_same_seat(event->seat, c->seat)) return;
  if (event->kind == QA_AUDIO_VOICE_EVENT_START) {
    /* Map.set keeps an existing key's position. */
    voice *existing = voice_get(c, event->voice_id);
    if (existing == NULL) {
      if (c->voice_count == c->voice_cap) {
        c->voice_cap = c->voice_cap ? c->voice_cap * 2 : 8;
        voice *grown = realloc(c->voices, c->voice_cap * sizeof *grown);
        if (grown == NULL) qa_range_error("Out of memory");
        c->voices = grown;
      }
      existing = &c->voices[c->voice_count++];
    } else free(existing->language);
    existing->id = event->voice_id; existing->event = *event; existing->captions = NULL;
    existing->language = xdup(""); existing->has_stop = false; existing->stop_sample = 0;
    return;
  }
  voice *found = voice_get(c, event->voice_id);
  if (found != NULL) { found->has_stop = true; found->stop_sample = event->output_sample; }
  /* prepare() is not called under a QVM cgame, so finished voices are retired here too. */
  double sample = qa_unified_audio_voice_clock(c->audio).output_sample;
  for (size_t i = 0; i < c->voice_count;) {
    if (c->voices[i].has_stop && sample >= c->voices[i].stop_sample) voice_delete(c, i);
    else i++;
  }
}
qa_app_seat_sound_captions *qa_app_seat_sound_captions_create(qa_seat_id seat, qa_unified_audio *audio,
    const char *(*language)(void *ctx), void *language_ctx, void (*warn_fn)(void *ctx, const char *text), void *warn_ctx) {
  qa_app_seat_sound_captions *c = calloc(1, sizeof *c);
  if (c == NULL) return NULL;
  c->seat = seat; c->audio = audio; c->language = language; c->language_ctx = language_ctx;
  c->warn = warn_fn; c->warn_ctx = warn_ctx;
  c->subscription = qa_unified_audio_observe_voices(audio, observe, c);
  return c;
}
void qa_app_seat_sound_captions_destroy(qa_app_seat_sound_captions *c) {
  if (c == NULL) return;
  qa_app_seat_sound_captions_close(c);
  free(c->voices); free(c->catalogs); free(c);
}
static const char *reference_content(const qa_resolved_resource_reference *reference) {
  return reference->provenance.kind == QA_CONTENT_MOUNT_ARCHIVE ? reference->provenance.archive_mount->identity.content
                                                                 : reference->provenance.loose_mount->identity.content;
}
typedef struct prepare_job { catalog *entry; const char *path, *language; } prepare_job;
static void prepare_run(void *ctx) {
  prepare_job *job = ctx;
  if (!qa_seat_media_captions_prepare(&job->entry->captions, job->path, job->language)) qa_range_error("Out of memory");
}
void qa_app_seat_sound_captions_prepare(qa_app_seat_sound_captions *c, qa_app_loaded_content *content) {
  qa_audio_voice_clock clock = qa_unified_audio_voice_clock(c->audio);
  for (size_t i = 0; i < c->voice_count;) {
    voice *v = &c->voices[i];
    if (v->has_stop && clock.output_sample >= v->stop_sample) { voice_delete(c, i); continue; }
    i++;
    const qa_resolved_resource_reference *reference = v->event.sound != NULL ? v->event.sound->reference : NULL;
    if (reference == NULL) continue;
    const char *language = c->language(c->language_ctx);
    if (v->captions != NULL && strcmp(v->language, language) == 0) continue;
    const char *content_id = reference_content(reference);
    size_t key_length = strlen(content_id) + strlen(reference->requested_path) + strlen(language) + 3;
    char *key = malloc(key_length);
    if (key == NULL) qa_range_error("Out of memory");
    snprintf(key, key_length, "%s:%s:%s", content_id, reference->requested_path, language);
    catalog *found = NULL;
    for (size_t j = 0; j < c->catalog_count && found == NULL; j++) if (strcmp(c->catalogs[j]->key, key) == 0) found = c->catalogs[j];
    if (found == NULL) {
      qa_content_mounted *mounts = (qa_content_mounted *)qa_app_loaded_content_for_content(content, content_id);
      if (c->closed) { free(key); continue; }
      if (c->catalog_count == c->catalog_cap) {
        c->catalog_cap = c->catalog_cap ? c->catalog_cap * 2 : 8;
        catalog **grown = realloc(c->catalogs, c->catalog_cap * sizeof *grown);
        if (grown == NULL) qa_range_error("Out of memory");
        c->catalogs = grown;
      }
      found = calloc(1, sizeof *found);
      if (found == NULL) qa_range_error("Out of memory");
      found->key = key; key = NULL;
      found->mounts = mounts;
      qa_seat_media_captions_init(&found->captions, c->seat, caption_read, found, NULL, true, NULL);
      /* TS passes this.warn (a no-op by default), so invalid cues are always skipped. */
      qa_seat_media_captions_set_warn(&found->captions, caption_warn, c);
      c->catalogs[c->catalog_count++] = found;
      /* The sidecar is optional: an unreadable or malformed one leaves this sound without captions. */
      prepare_job job = {found, reference->requested_path, language};
      qa_caught_error caught;
      if (!qa_try(prepare_run, &job, &caught)) {
        size_t length = strlen(reference->requested_path) + strlen(caught.message) + 64;
        char *text = malloc(length);
        if (text == NULL) qa_range_error("Out of memory");
        snprintf(text, length, "Sound captions for %s unavailable: %s\n", reference->requested_path, caught.message);
        warn(c, text);
        free(text);
      }
    }
    free(key);
    if (c->closed) continue;
    v = &c->voices[i - 1];
    v->captions = found;
    free(v->language);
    v->language = xdup(language);
  }
}
qa_active_caption *qa_app_seat_sound_captions_active(qa_app_seat_sound_captions *c, qa_caption_preferences preferences, size_t *out_count) {
  qa_audio_voice_clock now = qa_unified_audio_voice_clock(c->audio);
  qa_active_caption *out = NULL;
  size_t count = 0, cap = 0;
  for (size_t i = 0; i < c->voice_count; i++) {
    voice *v = &c->voices[i];
    if (v->captions == NULL || now.output_sample < v->event.output_sample || (v->has_stop && now.output_sample >= v->stop_sample)) continue;
    const qa_resolved_resource_reference *reference = v->event.sound != NULL ? v->event.sound->reference : NULL;
    if (reference == NULL) continue;
    double source_ms = v->event.source_offset_seconds * 1000 + (now.output_sample - v->event.output_sample) * 1000 / v->event.sample_rate;
    qa_caption_playback_state state = {reference->requested_path, source_ms, now.paused ? "paused" : "playing"};
    qa_active_caption *rows = NULL;
    size_t row_count = 0;
    if (!qa_seat_media_captions_active(&v->captions->captions, state, preferences, &rows, &row_count)) qa_range_error("Out of memory");
    for (size_t j = 0; j < row_count; j++) {
      if (count == cap) {
        cap = cap ? cap * 2 : 8;
        qa_active_caption *grown = realloc(out, cap * sizeof *grown);
        if (grown == NULL) qa_range_error("Out of memory");
        out = grown;
      }
      out[count++] = rows[j];
    }
    free(rows);
  }
  *out_count = count;
  return out;
}
void qa_app_seat_sound_captions_close(qa_app_seat_sound_captions *c) {
  if (c->closed) return;
  c->closed = true;
  qa_unified_audio_unobserve_voices(c->audio, c->subscription);
  for (size_t i = 0; i < c->catalog_count; i++) {
    qa_seat_media_captions_clear(&c->catalogs[i]->captions);
    qa_seat_media_captions_destroy(&c->catalogs[i]->captions);
    free(c->catalogs[i]->key);
    free(c->catalogs[i]);
  }
  c->catalog_count = 0;
  for (size_t i = 0; i < c->voice_count; i++) free(c->voices[i].language);
  c->voice_count = 0;
}
