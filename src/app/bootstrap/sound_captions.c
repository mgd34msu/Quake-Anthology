/* Port of src/app/bootstrap/sound-captions.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/sound_captions.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
#include "qa/input/support.h"
#include "qa/text/media_captions.h"
typedef struct voice {
  double id;
  qa_audio_voice_event event;
  qa_seat_media_captions *captions;
  char *language;
  bool has_stop; double stop_sample;
} voice;
typedef struct catalog { char *key; qa_seat_media_captions captions; bool live; } catalog;
struct qa_app_seat_sound_captions {
  qa_seat_id seat;
  qa_unified_audio *audio;
  const char *(*language)(void *ctx); void *language_ctx;
  int64_t subscription;
  voice *voices; size_t voice_count, voice_cap;
  catalog *catalogs; size_t catalog_count, catalog_cap;
  bool closed;
  /* Read bridge for the caption catalog currently being prepared. */
  qa_app_sound_caption_content content;
  const char *content_key;
};
static char *xdup(const char *s) { char *r = qa_input_dup(s); if (!r) qa_range_error("Out of memory"); return r; }
static uint8_t *caption_read(void *ctx, const char *path, size_t *out_length) {
  qa_app_seat_sound_captions *c = ctx;
  return c->content.open_bytes(c->content.ctx, c->content_key, path, out_length);
}
static void observe(void *ctx, const qa_audio_voice_event *event) {
  qa_app_seat_sound_captions *c = ctx;
  if (!qa_same_seat(event->seat, c->seat)) return;
  if (event->kind == QA_AUDIO_VOICE_EVENT_START) {
    for (size_t i = 0; i < c->voice_count; i++) {
      if (c->voices[i].id == event->voice_id) {
        free(c->voices[i].language);
        c->voices[i].event = *event;
        c->voices[i].captions = NULL;
        c->voices[i].language = xdup("");
        c->voices[i].has_stop = false;
        return;
      }
    }
    if (c->voice_count == c->voice_cap) { c->voice_cap = c->voice_cap ? c->voice_cap * 2 : 8; c->voices = realloc(c->voices, c->voice_cap * sizeof *c->voices); if (!c->voices) qa_range_error("Out of memory"); }
    voice *v = &c->voices[c->voice_count++];
    v->id = event->voice_id; v->event = *event; v->captions = NULL; v->language = xdup(""); v->has_stop = false; v->stop_sample = 0;
  } else {
    for (size_t i = 0; i < c->voice_count; i++)
      if (c->voices[i].id == event->voice_id) { c->voices[i].has_stop = true; c->voices[i].stop_sample = event->output_sample; }
  }
}
qa_app_seat_sound_captions *qa_app_seat_sound_captions_create(qa_seat_id seat, qa_unified_audio *audio,
                                                              const char *(*language)(void *ctx), void *language_ctx) {
  qa_app_seat_sound_captions *c = calloc(1, sizeof *c);
  if (!c) return NULL;
  c->seat = seat; c->audio = audio; c->language = language; c->language_ctx = language_ctx;
  c->subscription = qa_unified_audio_observe_voices(audio, observe, c);
  return c;
}
void qa_app_seat_sound_captions_destroy(qa_app_seat_sound_captions *c) {
  if (!c) return;
  qa_app_seat_sound_captions_close(c);
  free(c->voices); free(c->catalogs); free(c);
}
void qa_app_seat_sound_captions_prepare(qa_app_seat_sound_captions *c, qa_app_sound_caption_content content) {
  c->content = content;
  qa_audio_voice_clock clock = qa_unified_audio_voice_clock(c->audio);
  for (size_t i = 0; i < c->voice_count;) {
    voice *v = &c->voices[i];
    if (v->has_stop && clock.output_sample >= v->stop_sample) {
      free(v->language);
      memmove(&c->voices[i], &c->voices[i + 1], (c->voice_count - i - 1) * sizeof *c->voices);
      c->voice_count--;
      continue;
    }
    i++;
    const qa_resolved_resource_reference *reference = v->event.sound ? v->event.sound->reference : NULL;
    if (reference == NULL) continue;
    const char *language = c->language(c->language_ctx);
    if (v->captions != NULL && strcmp(v->language, language) == 0) continue;
    const char *content_key = content.content_key(content.ctx, reference);
    char key[4096];
    snprintf(key, sizeof key, "%s:%s:%s", content_key, reference->requested_path, language);
    catalog *found = NULL;
    for (size_t j = 0; j < c->catalog_count; j++) if (strcmp(c->catalogs[j].key, key) == 0) found = &c->catalogs[j];
    if (found == NULL) {
      if (c->closed) continue;
      if (c->catalog_count == c->catalog_cap) { c->catalog_cap = c->catalog_cap ? c->catalog_cap * 2 : 8; c->catalogs = realloc(c->catalogs, c->catalog_cap * sizeof *c->catalogs); if (!c->catalogs) qa_range_error("Out of memory"); }
      found = &c->catalogs[c->catalog_count++];
      found->key = xdup(key);
      found->live = true;
      c->content_key = content_key;
      qa_seat_media_captions_init(&found->captions, c->seat, caption_read, c, NULL, true, NULL);
      if (!qa_seat_media_captions_prepare(&found->captions, reference->requested_path, language)) qa_range_error("Out of memory");
      /* Re-find voice (array may have moved only via catalog realloc; voices stable). */
      v = &c->voices[i - 1];
    }
    if (c->closed) continue;
    v->captions = &found->captions;
    free(v->language);
    v->language = xdup(language);
  }
}
qa_active_caption *qa_app_seat_sound_captions_active(qa_app_seat_sound_captions *c, qa_caption_preferences preferences, size_t *out_count) {
  qa_audio_voice_clock now = qa_unified_audio_voice_clock(c->audio);
  qa_active_caption *out = NULL; size_t count = 0, cap = 0;
  for (size_t i = 0; i < c->voice_count; i++) {
    voice *v = &c->voices[i];
    if (v->captions == NULL || now.output_sample < v->event.output_sample
        || (v->has_stop && now.output_sample >= v->stop_sample)) continue;
    const qa_resolved_resource_reference *reference = v->event.sound ? v->event.sound->reference : NULL;
    if (reference == NULL) continue;
    double source_ms = v->event.source_offset_seconds * 1000 + (now.output_sample - v->event.output_sample) * 1000 / v->event.sample_rate;
    qa_caption_playback_state state = {reference->requested_path, source_ms, now.paused ? "paused" : "playing"};
    qa_active_caption *rows = NULL; size_t row_count = 0;
    if (!qa_seat_media_captions_active(v->captions, state, preferences, &rows, &row_count)) qa_range_error("Out of memory");
    for (size_t j = 0; j < row_count; j++) {
      if (count == cap) { cap = cap ? cap * 2 : 8; out = realloc(out, cap * sizeof *out); if (!out) qa_range_error("Out of memory"); }
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
    qa_seat_media_captions_clear(&c->catalogs[i].captions);
    qa_seat_media_captions_destroy(&c->catalogs[i].captions);
    free(c->catalogs[i].key);
  }
  c->catalog_count = 0;
  for (size_t i = 0; i < c->voice_count; i++) free(c->voices[i].language);
  c->voice_count = 0;
}
