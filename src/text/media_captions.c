/* Port of src/text/media-captions.ts. GPL-2.0-or-later. */
#include "qa/text/media_captions.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

static const struct {
  const char *language;
  const char *suffix;
} SUFFIXES[] = {{"french", "fr"}, {"german", "de"},   {"italian", "it"},
                {"spanish", "es"}, {"russian", "ru"},  {"polish", "pl"},
                {"portuguese", "pt"}, {"japanese", "ja"}, {"korean", "ko"},
                {"chinese", "zh"}};

static char *dup_range(const char *text, size_t length) {
  char *out = (char *)malloc(length + 1);
  if (out != NULL) {
    memcpy(out, text, length);
    out[length] = '\0';
  }
  return out;
}
static char *format_path(const char *stem, size_t stem_length,
                         const char *middle, const char *extension) {
  size_t need = stem_length + strlen(middle) + strlen(extension) + 1;
  char *out = (char *)malloc(need);
  if (out == NULL) return NULL;
  memcpy(out, stem, stem_length);
  memcpy(out + stem_length, middle, strlen(middle) + 1);
  strcat(out, extension);
  return out;
}

void qa_subtitle_paths_free(char **paths, size_t count) {
  for (size_t i = 0; i < count; i++) free(paths[i]);
  free(paths);
}

bool qa_subtitle_paths(const char *source, const char *language, char ***out,
                       size_t *out_count) {
  *out = NULL;
  *out_count = 0;
  /* stem = source.replace(/\.[^/.]+$/, "") */
  size_t length = strlen(source);
  size_t stem_length = length;
  for (size_t i = length; i-- > 0;) {
    if (source[i] == '/') break;
    if (source[i] == '.' && i + 1 < length) {
      stem_length = i;
      break;
    }
    if (source[i] == '.') break;
  }
  const char *suffix = NULL;
  for (size_t i = 0; i < sizeof SUFFIXES / sizeof SUFFIXES[0]; i++) {
    if (strcmp(language, SUFFIXES[i].language) == 0) suffix = SUFFIXES[i].suffix;
  }
  size_t count = suffix != NULL ? 4 : 2;
  char **paths = (char **)calloc(count, sizeof *paths);
  if (paths == NULL) return false;
  size_t index = 0;
  bool ok = true;
  if (suffix != NULL) {
    char middle[8];
    middle[0] = '_';
    middle[1] = '\0';
    strcat(middle, suffix);
    paths[index++] = format_path(source, stem_length, middle, ".srt");
    paths[index++] = format_path(source, stem_length, middle, ".vtt");
  }
  paths[index++] = format_path(source, stem_length, "", ".srt");
  paths[index++] = format_path(source, stem_length, "", ".vtt");
  for (size_t i = 0; i < count; i++) {
    if (paths[i] == NULL) ok = false;
  }
  if (!ok) {
    qa_subtitle_paths_free(paths, count);
    return false;
  }
  *out = paths;
  *out_count = count;
  return true;
}

static char *key_of(const char *source, const char *language) {
  size_t need = strlen(source) + strlen(language) + 2;
  char *key = (char *)malloc(need);
  if (key == NULL) return NULL;
  memcpy(key, source, strlen(source));
  key[strlen(source)] = ':';
  memcpy(key + strlen(source) + 1, language, strlen(language) + 1);
  return key;
}

static void install_timeline(qa_seat_media_captions *captions,
                             qa_localization_catalog *catalog, bool owned) {
  qa_caption_timeline_destroy(&captions->timeline);
  if (captions->owned_catalog != NULL) {
    qa_localization_catalog_destroy(captions->owned_catalog);
    free(captions->owned_catalog);
    captions->owned_catalog = NULL;
  }
  if (owned) captions->owned_catalog = catalog;
  qa_caption_timeline_init(&captions->timeline, captions->seat, catalog);
}

void qa_seat_media_captions_init(qa_seat_media_captions *captions,
                                 qa_seat_id seat,
                                 qa_localization_read_fn read, void *read_ctx,
                                 qa_localization_catalog *localization,
                                 bool kind_caption,
                                 const qa_live_language *live_language) {
  memset(captions, 0, sizeof *captions);
  captions->seat = seat;
  captions->read = read;
  captions->read_ctx = read_ctx;
  captions->supplied = localization;
  captions->kind_caption = kind_caption;
  if (live_language != NULL) {
    captions->has_live_language = true;
    captions->live_language = *live_language;
  }
  captions->prepared = dup_range("", 0);
  captions->live_request = dup_range("", 0);
  if (localization != NULL) {
    qa_caption_timeline_init(&captions->timeline, seat, localization);
  } else {
    qa_localization_catalog *catalog =
        (qa_localization_catalog *)calloc(1, sizeof *catalog);
    if (catalog != NULL) {
      qa_localization_catalog_init(catalog, seat, QA_LOC_Q1_RERELEASE);
    }
    install_timeline(captions, catalog, true);
  }
}

void qa_seat_media_captions_set_warn(qa_seat_media_captions *captions,
                                     void (*warn)(void *ctx, const char *text), void *ctx) {
  captions->warn = warn;
  captions->warn_ctx = ctx;
}

static void skipped_warn(void *raw, const char *reason) {
  qa_seat_media_captions *captions = (qa_seat_media_captions *)raw;
  size_t length = strlen(reason);
  char *text = (char *)malloc(length + 2);
  if (text == NULL) qa_range_error("Out of memory");
  memcpy(text, reason, length);
  text[length] = '\n';
  text[length + 1] = '\0';
  captions->warn(captions->warn_ctx, text);
  free(text);
}

/* Owned locals of prepare, freed when a raise unwinds through it. */
typedef struct prepare_locals {
  char *key;
  char **paths;
  size_t path_count;
  uint8_t *bytes;
} prepare_locals;

static void prepare_locals_free(void *raw) {
  prepare_locals *locals = (prepare_locals *)raw;
  free(locals->key);
  qa_subtitle_paths_free(locals->paths, locals->path_count);
  free(locals->bytes);
  memset(locals, 0, sizeof *locals);
}

void qa_seat_media_captions_destroy(qa_seat_media_captions *captions) {
  qa_caption_timeline_destroy(&captions->timeline);
  if (captions->owned_catalog != NULL) {
    qa_localization_catalog_destroy(captions->owned_catalog);
    free(captions->owned_catalog);
  }
  free(captions->prepared);
  free(captions->live_request);
  memset(captions, 0, sizeof *captions);
}

bool qa_seat_media_captions_prepare(qa_seat_media_captions *captions,
                                    const char *source,
                                    const char *language) {
  prepare_locals locals = {NULL, NULL, 0, NULL};
  locals.key = key_of(source, language);
  if (locals.key == NULL) return false;
  if (captions->prepared != NULL && strcmp(locals.key, captions->prepared) == 0) {
    free(locals.key);
    return true;
  }
  captions->request++;
  free(captions->prepared);
  captions->prepared = dup_range("", 0);
  qa_caption_timeline_clear(&captions->timeline);
  if (!qa_subtitle_paths(source, language, &locals.paths, &locals.path_count)) {
    free(locals.key);
    return false;
  }
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, prepare_locals_free, &locals);
  bool result = true;
  bool done = false;
  for (size_t i = 0; i < locals.path_count && !done; i++) {
    size_t length = 0;
    locals.bytes = captions->read(captions->read_ctx, locals.paths[i], &length);
    if (locals.bytes == NULL) continue;
    qa_localization_catalog *catalog = captions->supplied;
    bool owned = false;
    if (catalog == NULL) {
      catalog = (qa_localization_catalog *)calloc(1, sizeof *catalog);
      if (catalog == NULL) {
        result = false;
        break;
      }
      qa_load_localization_resources(captions->seat, language, captions->read,
                                     captions->read_ctx, QA_LOC_Q1_RERELEASE,
                                     catalog);
      owned = true;
    }
    install_timeline(captions, catalog, owned);
    qa_caption_cue *cues = NULL;
    size_t cue_count = 0;
    /* new TextDecoder().decode(bytes) drops one leading BOM before the
     * parser drops another; other bytes pass through (ASCII grammar). */
    const char *text = (const char *)locals.bytes;
    if (length >= 3 && locals.bytes[0] == 0xef && locals.bytes[1] == 0xbb &&
        locals.bytes[2] == 0xbf) {
      text += 3;
      length -= 3;
    }
    bool parsed = qa_parse_subtitle_text(
        text, length, locals.paths[i], captions->warn == NULL ? NULL : skipped_warn,
        captions, &cues, &cue_count);
    free(locals.bytes);
    locals.bytes = NULL;
    if (!parsed) {
      result = false;
      break;
    }
    for (size_t c = 0; c < cue_count; c++) cues[c].kind_caption =
        captions->kind_caption;
    bool replaced =
        qa_caption_timeline_replace(&captions->timeline, cues, cue_count);
    qa_caption_cues_free(cues, cue_count);
    if (!replaced) {
      result = false;
      break;
    }
    free(captions->prepared);
    captions->prepared = locals.key;
    locals.key = NULL;
    done = true;
  }
  if (!done && result) {
    free(captions->prepared);
    captions->prepared = locals.key;
    locals.key = NULL;
  }
  qa_cleanup_pop(&cleanup);
  prepare_locals_free(&locals);
  return result;
}

typedef struct live_prepare_job {
  qa_seat_media_captions *captions;
  const char *source;
  const char *language;
  bool result;
} live_prepare_job;

static void live_prepare_run(void *raw) {
  live_prepare_job *job = (live_prepare_job *)raw;
  job->result = qa_seat_media_captions_prepare(job->captions, job->source, job->language);
}

bool qa_seat_media_captions_active(qa_seat_media_captions *captions,
                                   qa_caption_playback_state state,
                                   qa_caption_preferences preferences,
                                   qa_active_caption **out, size_t *out_count) {
  *out = NULL;
  *out_count = 0;
  if (captions->has_live_language) {
    const char *language =
        captions->live_language.read(captions->live_language.ctx);
    char *key = key_of(state.source, language);
    if (key == NULL) return false;
    bool stale = captions->prepared == NULL ||
                 strcmp(key, captions->prepared) != 0;
    if (stale) {
      if (captions->live_request == NULL ||
          strcmp(key, captions->live_request) != 0) {
        free(captions->live_request);
        captions->live_request = key;
        /* Synchronous form of prepare().catch(failed). */
        live_prepare_job job = {captions, state.source, language, true};
        qa_caught_error caught;
        if (!qa_try(live_prepare_run, &job, &caught)) {
          if (captions->live_language.failed != NULL) {
            char text[QA_CAUGHT_MESSAGE_CAPACITY + 64];
            snprintf(text, sizeof text, "%s: %s", caught.name, caught.message);
            captions->live_language.failed(captions->live_language.ctx, text);
          }
        } else if (!job.result) {
          return false;
        }
      } else {
        free(key);
      }
      return true; /* TS returns [] while the new locale loads. */
    }
    free(key);
  }
  captions->timeline.preferences = preferences;
  size_t source_length = strlen(state.source);
  bool prepared_for_source = captions->prepared != NULL &&
                             strncmp(captions->prepared, state.source,
                                     source_length) == 0 &&
                             captions->prepared[source_length] == ':';
  if (!prepared_for_source || strcmp(state.status, "ended") == 0 ||
      strcmp(state.status, "stopped") == 0) {
    return true;
  }
  return qa_caption_timeline_active_at(&captions->timeline,
                                       state.source_time_milliseconds, out,
                                       out_count);
}

void qa_seat_media_captions_clear(qa_seat_media_captions *captions) {
  captions->request++;
  free(captions->prepared);
  captions->prepared = dup_range("", 0);
  free(captions->live_request);
  captions->live_request = dup_range("", 0);
  qa_caption_timeline_clear(&captions->timeline);
}
