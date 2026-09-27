#include "qa/captions.h"
#include "qa/text.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct cue_record {
  qa_caption_cue cue;
  char *storage;
} cue_record;
struct qa_caption_track {
  size_t references, count;
  cue_record *cues;
};
typedef struct cue_view {
  const qa_caption_cue *cue;
  qa_caption_track *track;
  size_t ordinal;
} cue_view;
struct qa_captions {
  uint32_t seat;
  qa_localization *localization;
  cue_view *cues;
  size_t count, capacity, next_ordinal;
  bool visiting;
};
typedef struct caption_asset {
  struct caption_asset *next;
  qa_sha256_digest digest;
  qa_caption_kind kind;
  char *path;
  qa_caption_track *track;
} caption_asset;
struct qa_caption_library {
  caption_asset *first;
};

static bool fail(qa_error *e, qa_status code, const char *message) {
  qa_error_set(e, code, 0, "%s", message);
  return false;
}
static bool copy_cue(const qa_caption_cue *in, cue_record *out, qa_error *e) {
  if (!in || !in->id || !*in->id || !in->text || !isfinite(in->start_ms) ||
      in->start_ms < 0 || !isfinite(in->duration_ms) || in->duration_ms < 0 ||
      !isfinite(in->start_ms + in->duration_ms) ||
      (in->kind != QA_CAPTION_SUBTITLE && in->kind != QA_CAPTION_SOUND) ||
      (in->argument_count && !in->arguments) ||
      in->argument_count > SIZE_MAX / sizeof(char *))
    return fail(e, QA_ERROR_ARGUMENT, "Invalid caption cue");
  size_t bytes = strlen(in->id) + 1,
         lengths[2] = {strlen(in->text) + 1,
                       in->speaker ? strlen(in->speaker) + 1 : 0};
  for (size_t i = 0; i < 2; ++i) {
    if (lengths[i] > SIZE_MAX - bytes)
      return fail(e, QA_ERROR_MEMORY, "Caption text is too large");
    bytes += lengths[i];
  }
  for (size_t i = 0; i < in->argument_count; ++i) {
    if (!in->arguments[i])
      return fail(e, QA_ERROR_ARGUMENT, "Missing caption argument");
    size_t n = strlen(in->arguments[i]) + 1;
    if (n > SIZE_MAX - bytes)
      return fail(e, QA_ERROR_MEMORY, "Caption arguments are too large");
    bytes += n;
  }
  char *storage = malloc(bytes);
  char **arguments = in->argument_count
                         ? malloc(in->argument_count * sizeof(*arguments))
                         : NULL;
  if (!storage || (in->argument_count && !arguments)) {
    free(storage);
    free(arguments);
    return fail(e, QA_ERROR_MEMORY, "Allocating caption text");
  }
  qa_caption_cue cue = *in;
  char *p = storage;
  size_t n = strlen(in->id) + 1;
  memcpy(p, in->id, n);
  cue.id = p;
  p += n;
  memcpy(p, in->text, lengths[0]);
  cue.text = p;
  p += lengths[0];
  if (in->speaker) {
    memcpy(p, in->speaker, lengths[1]);
    cue.speaker = p;
    p += lengths[1];
  }
  for (size_t i = 0; i < in->argument_count; ++i) {
    n = strlen(in->arguments[i]) + 1;
    memcpy(p, in->arguments[i], n);
    arguments[i] = p;
    p += n;
  }
  cue.arguments = (const char *const *)arguments;
  *out = (cue_record){cue, storage};
  return true;
}
static void free_cue(cue_record *cue) {
  free((void *)cue->cue.arguments);
  free(cue->storage);
}
bool qa_caption_track_create(const qa_caption_cue *cues, size_t count,
                             qa_caption_track **out, qa_error *e) {
  if (!out || (count && !cues) || count > SIZE_MAX / sizeof(cue_record))
    return fail(e, QA_ERROR_ARGUMENT, "Invalid caption track");
  qa_caption_track *track = calloc(1, sizeof(*track));
  if (!track)
    return fail(e, QA_ERROR_MEMORY, "Allocating caption track");
  track->references = 1;
  track->cues = count ? calloc(count, sizeof(*track->cues)) : NULL;
  if (count && !track->cues) {
    qa_caption_track_release(track);
    return fail(e, QA_ERROR_MEMORY, "Allocating caption cues");
  }
  for (size_t i = 0; i < count; ++i) {
    cue_record copy;
    if (!copy_cue(&cues[i], &copy, e)) {
      qa_caption_track_release(track);
      return false;
    }
    size_t j;
    for (j = 0; j < track->count; ++j)
      if (!strcmp(track->cues[j].cue.id, copy.cue.id))
        break;
    if (j < track->count)
      free_cue(&track->cues[j]);
    else
      ++track->count;
    track->cues[j] = copy;
  }
  *out = track;
  return true;
}
void qa_caption_track_retain(qa_caption_track *track) {
  if (track)
    ++track->references;
}
void qa_caption_track_release(qa_caption_track *track) {
  if (!track || --track->references)
    return;
  for (size_t i = 0; i < track->count; ++i)
    free_cue(&track->cues[i]);
  free(track->cues);
  free(track);
}
size_t qa_caption_track_count(const qa_caption_track *track) {
  return track ? track->count : 0;
}
const qa_caption_cue *qa_caption_track_at(const qa_caption_track *track,
                                          size_t i) {
  return track && i < track->count ? &track->cues[i].cue : NULL;
}
static bool decimal(const char *first, const char *last, double *out) {
  if (first == last)
    return false;
  double result = 0;
  for (; first != last; ++first) {
    if (*first < '0' || *first > '9')
      return false;
    result = result * 10 + (*first - '0');
  }
  *out = result;
  return isfinite(result);
}
static bool timestamp(const char *text, double *out) {
  const char *end = text + strlen(text), *colon = strchr(text, ':');
  if (!colon)
    return false;
  double h = 0, m, s, ms;
  const char *second = strchr(colon + 1, ':');
  if (second) {
    if (!decimal(text, colon, &h))
      return false;
    text = colon + 1;
    colon = second;
  }
  if (colon - text != 2 || end - colon != 7 ||
      (colon[3] != '.' && colon[3] != ',') || !decimal(text, colon, &m) ||
      !decimal(colon + 1, colon + 3, &s) || !decimal(colon + 4, end, &ms) ||
      m >= 60 || s >= 60)
    return false;
  *out = ((h * 60 + m) * 60 + s) * 1000 + ms;
  return isfinite(*out);
}
static bool whitespace(unsigned char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' ||
         c == '\f';
}
static char *word(char **cursor) {
  char *p = *cursor;
  while (whitespace((unsigned char)*p))
    ++p;
  char *result = p;
  while (*p && !whitespace((unsigned char)*p))
    ++p;
  if (*p)
    *p++ = 0;
  *cursor = p;
  return result;
}
static bool parse_block(char *block, const char *name, qa_caption_kind kind,
                        qa_caption_track *track, size_t *capacity,
                        qa_error *e) {
  char *line = block, *body = NULL;
  while (*line) {
    char *end = strchr(line, '\n');
    if (end)
      *end = 0;
    bool timing = strstr(line, "-->") != NULL;
    if (timing) {
      body = end ? end + 1 : line + strlen(line);
      break;
    }
    if (!end)
      return true;
    line = end + 1;
  }
  if (!body)
    return true;
  char *cursor = line;
  char *start = word(&cursor), *arrow = word(&cursor), *end = word(&cursor);
  double from, to;
  if (strcmp(arrow, "-->") || !timestamp(start, &from) ||
      !timestamp(end, &to) || to < from)
    return fail(e, QA_ERROR_FORMAT, "Invalid subtitle cue timing");
  size_t length = strlen(body);
  while (length && whitespace((unsigned char)body[length - 1]))
    body[--length] = 0;
  char *speaker = NULL;
  if (body[0] == '<' && body[1] == 'v' && whitespace((unsigned char)body[2])) {
    char *close = strchr(body + 3, '>');
    if (close) {
      speaker = body + 3;
      while (whitespace((unsigned char)*speaker))
        ++speaker;
      if (speaker != close) {
        *close = 0;
        body = close + 1;
        length = strlen(body);
        if (length >= 4 && !strcmp(body + length - 4, "</v>"))
          body[length - 4] = 0;
      } else
        speaker = NULL;
    }
  }
  size_t name_length = strlen(name);
  if (name_length > SIZE_MAX - 32)
    return fail(e, QA_ERROR_MEMORY, "Caption namespace is too large");
  char *id = malloc(name_length + 32);
  if (!id)
    return fail(e, QA_ERROR_MEMORY, "Allocating caption cue ID");
  snprintf(id, name_length + 32, "%s:%zu", name, track->count);
  qa_caption_cue cue = {.id = id,
                        .text = body,
                        .speaker = speaker,
                        .start_ms = from,
                        .duration_ms = to - from,
                        .kind = kind};
  if (track->count == *capacity) {
    size_t next = *capacity ? *capacity * 2 : 32;
    if (next < *capacity || next > SIZE_MAX / sizeof(cue_record)) {
      free(id);
      return fail(e, QA_ERROR_MEMORY, "Too many subtitle cues");
    }
    cue_record *records = realloc(track->cues, next * sizeof(*records));
    if (!records) {
      free(id);
      return fail(e, QA_ERROR_MEMORY, "Allocating subtitle cues");
    }
    track->cues = records;
    *capacity = next;
  }
  bool ok = copy_cue(&cue, &track->cues[track->count], e);
  free(id);
  if (ok)
    ++track->count;
  return ok;
}
bool qa_caption_track_parse(qa_bytes bytes, const char *name,
                            qa_caption_kind kind, qa_caption_track **out,
                            qa_error *e) {
  if (!out || !name || (bytes.size && !bytes.data) || bytes.size == SIZE_MAX ||
      (kind != QA_CAPTION_SUBTITLE && kind != QA_CAPTION_SOUND))
    return fail(e, QA_ERROR_ARGUMENT, "Invalid subtitle input");
  qa_buffer decoded;
  if (!qa_utf8_repair(bytes, &decoded, e))
    return false;
  bytes = (qa_bytes){decoded.data, decoded.size};
  char *text = (char *)decoded.data;
  qa_caption_track *track = calloc(1, sizeof(*track));
  if (!text || !track) {
    free(text);
    free(track);
    return fail(e, QA_ERROR_MEMORY, "Allocating subtitle input");
  }
  track->references = 1;
  size_t used = 0,
         at = bytes.size >= 3 && !memcmp(bytes.data, "\xef\xbb\xbf", 3) ? 3 : 0;
  for (; at < bytes.size; ++at) {
    uint8_t c = bytes.data[at];
    if (!c) {
      free(text);
      qa_caption_track_release(track);
      return fail(e, QA_ERROR_FORMAT, "NUL in subtitle text");
    }
    if (c == '\r') {
      c = '\n';
      if (at + 1 < bytes.size && bytes.data[at + 1] == '\n')
        ++at;
    }
    text[used++] = (char)c;
  }
  text[used] = 0;
  char *block = text, *cursor = text;
  size_t capacity = 0;
  bool ok = true;
  while (*cursor) {
    if (*cursor++ != '\n')
      continue;
    char *next = cursor;
    while (*next == ' ' || *next == '\t')
      ++next;
    if (*next != '\n')
      continue;
    cursor[-1] = 0;
    if (!parse_block(block, name, kind, track, &capacity, e)) {
      ok = false;
      break;
    }
    block = cursor = next + 1;
  }
  if (ok)
    ok = parse_block(block, name, kind, track, &capacity, e);
  free(text);
  if (!ok) {
    qa_caption_track_release(track);
    return false;
  }
  *out = track;
  return true;
}
static int compare(const void *a, const void *b) {
  const cue_view *x = a, *y = b;
  if (x->cue->start_ms != y->cue->start_ms)
    return x->cue->start_ms < y->cue->start_ms ? -1 : 1;
  return x->ordinal < y->ordinal ? -1 : x->ordinal > y->ordinal;
}
qa_captions *qa_captions_create(uint32_t seat, qa_localization *catalog,
                                qa_error *e) {
  qa_captions *timeline = calloc(1, sizeof(*timeline));
  if (!timeline) {
    fail(e, QA_ERROR_MEMORY, "Allocating caption timeline");
    return NULL;
  }
  timeline->seat = seat;
  timeline->localization = catalog;
  qa_localization_retain(catalog);
  return timeline;
}
void qa_captions_clear(qa_captions *timeline) {
  if (!timeline || timeline->visiting)
    return;
  for (size_t i = 0; i < timeline->count; ++i)
    qa_caption_track_release(timeline->cues[i].track);
  timeline->count = timeline->next_ordinal = 0;
}
void qa_captions_destroy(qa_captions *timeline) {
  if (!timeline || timeline->visiting)
    return;
  qa_captions_clear(timeline);
  free(timeline->cues);
  qa_localization_release(timeline->localization);
  free(timeline);
}
void qa_captions_localization(qa_captions *timeline, qa_localization *catalog) {
  if (timeline->visiting)
    return;
  qa_localization_retain(catalog);
  qa_localization_release(timeline->localization);
  timeline->localization = catalog;
}
bool qa_captions_replace(qa_captions *timeline, qa_caption_track *track,
                         qa_error *e) {
  if (!timeline || timeline->visiting)
    return fail(e, QA_ERROR_ARGUMENT, "Caption timeline is active");
  size_t count = qa_caption_track_count(track);
  if (count > SIZE_MAX / sizeof(cue_view))
    return fail(e, QA_ERROR_MEMORY, "Caption track is too large");
  cue_view *cues = count ? malloc(count * sizeof(*cues)) : NULL;
  if (count && !cues)
    return fail(e, QA_ERROR_MEMORY, "Allocating caption timeline views");
  for (size_t i = 0; i < count; ++i) {
    qa_caption_track_retain(track);
    cues[i] = (cue_view){&track->cues[i].cue, track, i};
  }
  if (count > 1)
    qsort(cues, count, sizeof(*cues), compare);
  qa_captions_clear(timeline);
  free(timeline->cues);
  timeline->cues = cues;
  timeline->count = timeline->capacity = timeline->next_ordinal = count;
  return true;
}
bool qa_captions_add(qa_captions *timeline, const qa_caption_cue *cue,
                     qa_error *e) {
  if (!timeline || timeline->visiting || timeline->next_ordinal == SIZE_MAX)
    return fail(e, QA_ERROR_ARGUMENT,
                "Caption timeline is active or exhausted");
  qa_caption_track *track;
  if (!qa_caption_track_create(cue, 1, &track, e))
    return false;
  size_t index;
  for (index = 0; index < timeline->count; ++index)
    if (!strcmp(timeline->cues[index].cue->id, cue->id))
      break;
  if (index == timeline->count && timeline->count == timeline->capacity) {
    size_t n = timeline->capacity ? timeline->capacity * 2 : 16;
    if (n < timeline->capacity || n > SIZE_MAX / sizeof(cue_view)) {
      qa_caption_track_release(track);
      return fail(e, QA_ERROR_MEMORY, "Caption timeline is too large");
    }
    cue_view *views = realloc(timeline->cues, n * sizeof(*views));
    if (!views) {
      qa_caption_track_release(track);
      return fail(e, QA_ERROR_MEMORY, "Allocating caption timeline");
    }
    timeline->cues = views;
    timeline->capacity = n;
  }
  size_t ordinal;
  if (index < timeline->count) {
    ordinal = timeline->cues[index].ordinal;
    qa_caption_track_release(timeline->cues[index].track);
  } else {
    ordinal = timeline->next_ordinal++;
    ++timeline->count;
  }
  timeline->cues[index] = (cue_view){&track->cues[0].cue, track, ordinal};
  qsort(timeline->cues, timeline->count, sizeof(*timeline->cues), compare);
  return true;
}
bool qa_captions_remove(qa_captions *timeline, const char *id) {
  if (!timeline || timeline->visiting || !id)
    return false;
  for (size_t i = 0; i < timeline->count; ++i)
    if (!strcmp(timeline->cues[i].cue->id, id)) {
      qa_caption_track_release(timeline->cues[i].track);
      memmove(timeline->cues + i, timeline->cues + i + 1,
              (timeline->count - i - 1) * sizeof(*timeline->cues));
      --timeline->count;
      return true;
    }
  return false;
}
uint32_t qa_captions_seat(const qa_captions *timeline) {
  return timeline->seat;
}
bool qa_captions_visit(qa_captions *timeline, double time_ms,
                       qa_caption_preferences prefs,
                       void (*visit)(void *, const qa_active_caption *),
                       void *context, qa_error *e) {
  if (!timeline || timeline->visiting || !isfinite(time_ms) || !visit)
    return fail(e, QA_ERROR_ARGUMENT, "Invalid caption query");
  timeline->visiting = true;
  for (size_t i = 0; i < timeline->count; ++i) {
    const qa_caption_cue *cue = timeline->cues[i].cue;
    if (cue->start_ms > time_ms)
      break;
    if (time_ms >= cue->start_ms + cue->duration_ms ||
        (cue->kind == QA_CAPTION_SUBTITLE ? !prefs.subtitles
                                          : !prefs.sound_captions))
      continue;
    char text[1024], speaker[1024];
    qa_localize(timeline->localization, cue->text, cue->arguments,
                cue->argument_count, true, false, text, sizeof(text));
    if (prefs.speakers && cue->speaker)
      qa_localize(timeline->localization, cue->speaker, NULL, 0, true, false,
                  speaker, sizeof(speaker));
    qa_active_caption active = {
        cue, text, prefs.speakers && cue->speaker ? speaker : NULL};
    visit(context, &active);
  }
  timeline->visiting = false;
  return true;
}
qa_caption_library *qa_caption_library_create(qa_error *e) {
  qa_caption_library *library = calloc(1, sizeof(*library));
  if (!library)
    fail(e, QA_ERROR_MEMORY, "Allocating caption library");
  return library;
}
static void free_asset(caption_asset *asset) {
  qa_caption_track_release(asset->track);
  free(asset->path);
  free(asset);
}
void qa_caption_library_destroy(qa_caption_library *library) {
  if (!library)
    return;
  while (library->first) {
    caption_asset *next = library->first->next;
    free_asset(library->first);
    library->first = next;
  }
  free(library);
}
void qa_caption_library_trim(qa_caption_library *library) {
  for (caption_asset **link = &library->first; *link;) {
    caption_asset *asset = *link;
    if (asset->track->references == 1) {
      *link = asset->next;
      free_asset(asset);
    } else
      link = &asset->next;
  }
}
bool qa_caption_library_load(qa_caption_library *library, qa_vfs *view,
                             const char *movie, const char *language,
                             qa_caption_kind kind, qa_caption_track **out,
                             qa_error *e) {
  if (!library || !view || !movie || !language || !out)
    return fail(e, QA_ERROR_ARGUMENT, "Invalid caption sidecar request");
  static const char *const names[] = {
      "french", "german",     "italian",  "spanish", "russian",
      "polish", "portuguese", "japanese", "korean",  "chinese"};
  static const char *const codes[] = {"fr", "de", "it", "es", "ru",
                                      "pl", "pt", "ja", "ko", "zh"};
  const char *suffix = NULL;
  for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
    if (!strcmp(language, names[i])) {
      suffix = codes[i];
      break;
    }
  const char *dot = strrchr(movie, '.'), *slash = strrchr(movie, '/');
  size_t length =
      dot && (!slash || dot > slash) ? (size_t)(dot - movie) : strlen(movie);
  if (length > SIZE_MAX - 8)
    return fail(e, QA_ERROR_MEMORY, "Caption sidecar path is too large");
  char *path = malloc(length + 8);
  if (!path)
    return fail(e, QA_ERROR_MEMORY, "Allocating caption sidecar path");
  memcpy(path, movie, length);
  qa_resource *resource = NULL;
  bool ok = true;
  for (size_t i = suffix ? 0 : 2; i < 4; ++i) {
    snprintf(path + length, 8, "%s%s.%s", i < 2 ? "_" : "", i < 2 ? suffix : "",
             i & 1 ? "vtt" : "srt");
    qa_error missing = {0};
    if (qa_vfs_acquire(view, path, &resource, NULL, &missing))
      break;
    if (missing.code != QA_ERROR_NOT_FOUND) {
      if (e)
        *e = missing;
      ok = false;
      break;
    }
  }
  if (!ok || !resource) {
    free(path);
    if (ok)
      *out = NULL;
    return ok;
  }
  caption_asset *asset;
  for (asset = library->first; asset; asset = asset->next)
    if (asset->kind == kind && !strcmp(asset->path, path) &&
        qa_sha256_equal(&asset->digest, qa_resource_digest(resource)))
      break;
  if (!asset) {
    asset = calloc(1, sizeof(*asset));
    if (!asset)
      ok = fail(e, QA_ERROR_MEMORY, "Allocating caption asset");
    else if (!qa_caption_track_parse(qa_resource_bytes(resource), path, kind,
                                     &asset->track, e)) {
      free(asset);
      ok = false;
    } else {
      asset->path = path;
      path = NULL;
      asset->kind = kind;
      asset->digest = *qa_resource_digest(resource);
      asset->next = library->first;
      library->first = asset;
    }
  }
  if (ok) {
    qa_caption_track_retain(asset->track);
    *out = asset->track;
  }
  free(path);
  qa_resource_release(resource);
  return ok;
}
